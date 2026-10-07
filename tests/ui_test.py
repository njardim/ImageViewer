#!/usr/bin/env python3
"""Interaction test on Xvfb, driven like a user with xdotool (decisions D-29, D-31).

Three small noise images (asymmetric, so every flip and rotation is distinguishable) are
opened from a folder; every step waits until the expected pixels are on screen, exactly:
  1. hovering the right edge shows the "next" button; clicking it shows the next image,
     and the left edge goes back;
  2. H, V, R and Shift+R flip and rotate the view (compared with numpy's flips/rotations);
  3. Delete asks for confirmation, Return moves the file to the trash, the next image follows;
  4. Ctrl+Q quits and the session (last file, window geometry) is in the settings file.
The application runs with its own HOME, XDG_CONFIG_HOME and XDG_DATA_HOME (trash), so the
user's settings and trash are never touched.

usage: python3 tests/ui_test.py <imageViewer> [vulkan|opengl]
needs: Xvfb, xdotool, xwd (x11-apps), ImageMagick `convert`, numpy, Pillow
env:   UI_TEST_DIR  where to keep the files, the log and the last screenshot (default: a temp dir)
"""
import configparser
import os
import subprocess
import sys
import tempfile
import time

import numpy as np
from PIL import Image

import xvfb

DEADLINE_S = 60
POLL_S = 0.3

exe = os.path.abspath(sys.argv[1])
rhi = sys.argv[2] if len(sys.argv) > 2 else "vulkan"
work = os.environ.get("UI_TEST_DIR") or tempfile.mkdtemp(prefix="imageviewer-ui-")
home = os.path.join(work, "home")
pictures = os.path.join(home, "pictures")
config = os.path.join(work, "config")
data = os.path.join(home, ".local", "share")
for d in (pictures, config, data):
    os.makedirs(d, exist_ok=True)
log_path = os.path.join(work, f"viewer-{rhi}.log")
shot_path = os.path.join(work, f"screen-{rhi}.png")

refs = {}
for i, name in enumerate(("a", "b", "c")):
    pixels = np.random.default_rng(i + 1).integers(0, 256, (64, 96, 3), dtype=np.uint8)
    Image.fromarray(pixels).save(os.path.join(pictures, f"{name}.png"))
    refs[name] = pixels.astype(int)


def grab():
    result = subprocess.run(f"xwd -root -silent -display {display} | convert xwd:- {shot_path}",
                            shell=True, stderr=subprocess.DEVNULL)
    if result.returncode != 0:
        return None
    return np.asarray(Image.open(shot_path).convert("RGB")).astype(int)


def find(screen, ref):
    """Top-left of an exact copy of `ref` on the screen, or None."""
    h, w = ref.shape[:2]
    ys, xs = np.nonzero(np.all(screen[: screen.shape[0] - h + 1, : screen.shape[1] - w + 1] == ref[0, 0], axis=2))
    for y, x in zip(ys, xs):
        if np.array_equal(screen[y:y + h, x:x + w], ref):
            return int(x), int(y)
    return None


def wait_for(what, ref):
    start = time.monotonic()
    while time.monotonic() - start < DEADLINE_S:
        if app.poll() is not None:
            fail(f"{what}: imageViewer exited with code {app.returncode}")
        screen = grab()
        if screen is not None and find(screen, ref) is not None:
            print(f"ok   {what} ({time.monotonic() - start:.1f} s)")
            return
        time.sleep(POLL_S)
    fail(f"{what}: not on screen after {DEADLINE_S} s (see {shot_path})")


def xdotool(*args):
    return subprocess.run(["xdotool", *args], env=env, capture_output=True, text=True, timeout=30)


def window_geometry():
    found = xdotool("search", "--sync", "--onlyvisible", "--name", "imageViewer$")
    for wid in found.stdout.split():
        info = dict(line.split("=", 1) for line in xdotool("getwindowgeometry", "--shell", wid).stdout.split())
        if int(info["WIDTH"]) >= 640:  # the viewer, not a dialog
            return int(info["X"]), int(info["Y"]), int(info["WIDTH"]), int(info["HEIGHT"])
    fail("viewer window not found")


def fail(message):
    print(f"FAIL: {rhi}: {message}")
    raise SystemExit(1)


server, display = xvfb.start("1600x1000x24")
env = dict(os.environ, DISPLAY=display, QT_QPA_PLATFORM="xcb", IMAGEVIEWER_RHI=rhi, IMAGEVIEWER_OUTPUT="sdr",
           LC_ALL="C.UTF-8", LANGUAGE="en", HOME=home, XDG_CONFIG_HOME=config, XDG_DATA_HOME=data,
           QT_LOGGING_RULES="imageviewer.*=true")
app = None
try:
    with open(log_path, "w") as log:
        app = subprocess.Popen([exe, os.path.join(pictures, "a.png")], env=env, stdout=log, stderr=log)
        try:
            wait_for("first image shown", refs["a"])
            x, y, w, h = window_geometry()

            # 1. Side zones: the button appears on hover, a click navigates.
            xdotool("mousemove", "--sync", str(x + w - 60), str(y + h // 2))
            start = time.monotonic()
            while True:  # the chevron is white on the dark button, in the middle of the 200 px zone
                screen = grab()
                cx, cy = x + w - 100, y + h // 2
                if screen is not None and (screen[cy - 20:cy + 20, cx - 20:cx + 20].min(axis=2) > 220).any():
                    print(f"ok   next button shown on hover ({time.monotonic() - start:.1f} s)")
                    break
                if time.monotonic() - start > DEADLINE_S:
                    fail(f"next button not shown on hover (see {shot_path})")
                time.sleep(POLL_S)
            xdotool("click", "1")
            wait_for("right side click shows the next image", refs["b"])
            xdotool("mousemove", "--sync", str(x + 60), str(y + h // 2))
            xdotool("click", "1")
            wait_for("left side click shows the previous image", refs["a"])
            xdotool("mousemove", "--sync", str(x + w // 2), str(y + h // 4))  # away from the zones
            xdotool("key", "Right")
            wait_for("Right key shows the next image", refs["b"])

            # 2. View transforms (screen space: flips and quarter turns of what is shown).
            b = refs["b"]
            for key, expected, what in (("h", b[:, ::-1], "H flips horizontally"), ("h", b, "H again restores"),
                                        ("v", b[::-1], "V flips vertically"), ("v", b, "V again restores"),
                                        ("r", np.rot90(b, -1), "R rotates clockwise"),
                                        ("shift+r", b, "Shift+R rotates back")):
                xdotool("key", key)
                wait_for(what, expected)

            # 3. Move to trash, with the confirmation dialog (on by default).
            xdotool("key", "Delete")
            dialogs = []
            start = time.monotonic()
            while not dialogs and time.monotonic() - start < DEADLINE_S:
                dialogs = xdotool("search", "--onlyvisible", "--name", "^imageViewer$").stdout.split()
                time.sleep(POLL_S)
            if not dialogs:
                fail("Delete did not ask for confirmation")
            if not os.path.exists(os.path.join(pictures, "b.png")):
                fail("b.png was moved before the confirmation")
            print("ok   Delete asks for confirmation first")
            xdotool("windowfocus", "--sync", dialogs[0])
            xdotool("key", "Return")
            wait_for("after the trash, the next image is shown", refs["c"])
            trashed = os.path.join(data, "Trash", "files", "b.png")
            if os.path.exists(os.path.join(pictures, "b.png")) or not os.path.exists(trashed):
                fail(f"b.png was not moved to {os.path.dirname(trashed)}")
            print("ok   b.png is in the trash, not in the folder")

            # 4. Quit; the session is saved.
            xdotool("key", "ctrl+q")
            app.wait(15)
        finally:
            if app.poll() is None:
                app.terminate()
                app.wait(10)
finally:
    xvfb.stop(server)

if app.returncode != 0:
    print(open(log_path, encoding="utf-8", errors="replace").read())
    fail(f"imageViewer exited with code {app.returncode}")
settings_file = os.path.join(config, "Cristallumnis", "imageViewer.conf")
store = configparser.ConfigParser(interpolation=None)
store.read(settings_file)
last = store.get("session", "lastFile", fallback="")
if os.path.basename(last) != "c.png" or not store.get("session", "geometry", fallback="").startswith("@Rect("):
    fail(f"session not saved as expected in {settings_file}: lastFile={last!r}")
print("ok   session saved (last file, window geometry)")
print(f"{rhi}: all interaction checks passed")
