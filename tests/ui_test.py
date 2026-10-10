#!/usr/bin/env python3
"""Interaction test on Xvfb, driven like a user with xdotool (decisions D-29, D-31).

Three small noise images (asymmetric, so every flip and rotation is distinguishable) are
opened from a folder; every step waits until the expected pixels are on screen, exactly:
  1. hovering the right edge shows the "next" button; clicking it shows the next image,
     and the left edge goes back;
  2. H, V, R and Shift+R flip and rotate the view (compared with numpy's flips/rotations);
  3. Delete asks for confirmation, Return moves the file to the trash, the next image follows;
  4. Ctrl+Z restores it (no stale trash record); stepping to a neighbour uses the preload
     cache (log); F2 renames; Shift+Delete always asks (Return cancels) and then deletes;
     a file created, deleted or rewritten by another program shows up, goes away or is
     shown again (folder watching); Shift+I shows the top overlay without moving the image
     by a pixel (E14); B shows the checkerboard behind a transparent image; Apply in the
     Settings saves a change while the dialog stays open, and Cancel keeps it;
  5. Q quits and the session (last file, window geometry) is in the settings file;
  6. after a restart with settings written by 0.1 under the earlier file name (imageViewer.conf),
     the file is taken over, the settings still apply and move to [app], and
     the old default side-zone width (200 px) becomes the new one (100 px);
  7. zoom modes, zoom lock and the title bar (D-51), read from the window title with every
     detail on: fit to the window, X (a user shortcut for Fit to Width, D-52) fits the width,
     1 is 100 %, L keeps it for the next image and L again lets it fit; the window takes
     an image's size within 50 % of the screen;
  8. shortcuts (D-52): an unknown key name in the settings leaves the defaults, Shift+1 runs
     the command bound to "!" before Actual Size ("1"), and a key recorded in the Shortcuts
     tab replaces the first shortcut while the others stay;
  9. a still pointer hides after 2 s and a move shows it again; T and C, toggled from the
     keyboard, are saved as settings (D-56).
The application runs with its own HOME, XDG_CONFIG_HOME and XDG_DATA_HOME (trash), so the
user's settings and trash are never touched.

usage: python3 tests/ui_test.py <ImageViewer> [vulkan|opengl]
needs: Xvfb, xdotool, xwd (x11-apps), ImageMagick `convert`, libXfixes, numpy, Pillow
env:   UI_TEST_DIR  where to keep the files, the log and the last screenshot (default: a temp dir)
"""
import configparser
import ctypes
import ctypes.util
import os
import re
import shutil
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
# A reused UI_TEST_DIR must not carry an earlier run's settings or trash into this one.
for d in (home, config):
    shutil.rmtree(d, ignore_errors=True)
for d in (pictures, config, data):
    os.makedirs(d, exist_ok=True)
log_path = os.path.join(work, f"viewer-{rhi}.log")
shot_path = os.path.join(work, f"screen-{rhi}.png")

refs = {}
for i, name in enumerate(("a", "b", "c", "e")):
    pixels = np.random.default_rng(i + 1).integers(0, 256, (64, 96, 3), dtype=np.uint8)
    if name != "e":  # e.png is created later, by "another program"
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
            fail(f"{what}: ImageViewer exited with code {app.returncode}")
        screen = grab()
        if screen is not None and find(screen, ref) is not None:
            print(f"ok   {what} ({time.monotonic() - start:.1f} s)")
            return
        time.sleep(POLL_S)
    fail(f"{what}: not on screen after {DEADLINE_S} s (see {shot_path})")


def wait_until(what, condition):
    start = time.monotonic()
    while time.monotonic() - start < DEADLINE_S:
        if app.poll() is not None:
            fail(f"{what}: ImageViewer exited with code {app.returncode}")
        if condition():
            print(f"ok   {what} ({time.monotonic() - start:.1f} s)")
            return
        time.sleep(POLL_S)
    fail(f"{what}: not after {DEADLINE_S} s (see {shot_path} and {log_path})")


def log_text():
    with open(log_path, encoding="utf-8", errors="replace") as f:
        return f.read()


def find_dialog(title):
    """Window id of a visible dialog with this exact title, waiting for it."""
    found = []
    start = time.monotonic()
    while not found and time.monotonic() - start < DEADLINE_S:
        found = xdotool("search", "--onlyvisible", "--name", f"^{title}$").stdout.split()
        time.sleep(POLL_S)
    return found[0] if found else None


def xdotool(*args):
    return subprocess.run(["xdotool", *args], env=env, capture_output=True, text=True, timeout=30)


def window_geometry():
    found = xdotool("search", "--sync", "--onlyvisible", "--name", "ImageViewer$")
    for wid in found.stdout.split():
        info = dict(line.split("=", 1) for line in xdotool("getwindowgeometry", "--shell", wid).stdout.split())
        if int(info["WIDTH"]) >= 640:  # the viewer, not a dialog
            return int(info["X"]), int(info["Y"]), int(info["WIDTH"]), int(info["HEIGHT"])
    fail("viewer window not found")


def fail(message):
    print(f"FAIL: {rhi}: {message}")
    raise SystemExit(1)


class CursorImage(ctypes.Structure):  # XFixesCursorImage
    _fields_ = [("x", ctypes.c_short), ("y", ctypes.c_short), ("width", ctypes.c_ushort),
                ("height", ctypes.c_ushort), ("xhot", ctypes.c_ushort), ("yhot", ctypes.c_ushort),
                ("cursor_serial", ctypes.c_ulong), ("pixels", ctypes.POINTER(ctypes.c_ulong)),
                ("atom", ctypes.c_ulong), ("name", ctypes.c_char_p)]


def open_xfixes():
    x11 = ctypes.CDLL(ctypes.util.find_library("X11") or "libX11.so.6")
    xfixes = ctypes.CDLL(ctypes.util.find_library("Xfixes") or "libXfixes.so.3")
    x11.XOpenDisplay.restype = ctypes.c_void_p
    x11.XOpenDisplay.argtypes = [ctypes.c_char_p]
    x11.XFree.argtypes = [ctypes.c_void_p]
    xfixes.XFixesQueryVersion.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_int)]
    xfixes.XFixesGetCursorImage.restype = ctypes.POINTER(CursorImage)
    xfixes.XFixesGetCursorImage.argtypes = [ctypes.c_void_p]
    connection = x11.XOpenDisplay(display.encode())
    major, minor = ctypes.c_int(6), ctypes.c_int(0)
    if not connection or not xfixes.XFixesQueryVersion(connection, ctypes.byref(major), ctypes.byref(minor)):
        fail("XFixes is not available on the X server")

    def pointer_hidden():
        """The pointer's image is blank (Qt's BlankCursor): every pixel fully transparent."""
        image = xfixes.XFixesGetCursorImage(connection)
        if not image:
            fail("XFixesGetCursorImage failed")
        c = image.contents
        hidden = not any(c.pixels[i] >> 24 for i in range(c.width * c.height))
        x11.XFree(image)
        return hidden
    return pointer_hidden


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
            while True:  # the chevron is white on the dark button, in the middle of the 100 px zone
                screen = grab()
                cx, cy = x + w - 50, y + h // 2
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
            for key, expected, what in (("shift+w", b[:, ::-1], "Shift+W flips horizontally"), ("shift+w", b, "Shift+W again restores"),
                                        ("shift+h", b[::-1], "Shift+H flips vertically"), ("shift+h", b, "Shift+H again restores"),
                                        ("r", np.rot90(b, -1), "R rotates clockwise"),
                                        ("shift+r", b, "Shift+R rotates back")):
                xdotool("key", key)
                wait_for(what, expected)

            # 3. Move to trash, with the confirmation dialog (on by default).
            xdotool("key", "Delete")
            dialogs = []
            start = time.monotonic()
            while not dialogs and time.monotonic() - start < DEADLINE_S:
                dialogs = xdotool("search", "--onlyvisible", "--name", "^ImageViewer$").stdout.split()
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

            # 4. Undo, preloading, rename, permanent delete, folder watching, top overlay.
            xdotool("key", "ctrl+z")
            wait_for("Ctrl+Z restores the trashed image", refs["b"])
            info = os.path.join(data, "Trash", "info", "b.png.trashinfo")
            if not os.path.exists(os.path.join(pictures, "b.png")) or os.path.exists(trashed) or os.path.exists(info):
                fail("b.png not restored cleanly (file back in the folder, no trash record left)")
            print("ok   b.png back in the folder, trash record removed")

            # c.png was the image on screen before the undo, and is b's next neighbour.
            cache_hits = log_text().count("shown from the cache: c.png")
            xdotool("key", "Right")
            wait_for("Right shows the next image again", refs["c"])
            wait_until("it came from the preload cache",
                       lambda: log_text().count("shown from the cache: c.png") > cache_hits)

            xdotool("key", "F2")
            rename = find_dialog("Rename")
            if not rename:
                fail("F2 did not open the Rename dialog")
            xdotool("windowfocus", "--sync", rename)
            xdotool("type", "--delay", "50", "d")  # replaces the selected "c", keeps ".png"
            xdotool("key", "Return")
            wait_until("F2 renames c.png to d.png",
                       lambda: os.path.exists(os.path.join(pictures, "d.png"))
                       and not os.path.exists(os.path.join(pictures, "c.png")))
            wait_for("the renamed image stays on screen", refs["c"])

            xdotool("key", "shift+Delete")
            box = find_dialog("ImageViewer")
            if not box:
                fail("Shift+Delete did not ask for confirmation")
            xdotool("windowfocus", "--sync", box)
            xdotool("key", "Return")  # the default button is Cancel
            wait_until("Return closes the confirmation",
                       lambda: not xdotool("search", "--onlyvisible", "--name", "^ImageViewer$").stdout.split())
            if not os.path.exists(os.path.join(pictures, "d.png")):
                fail("Return deleted the file: Cancel must be the default")
            print("ok   Shift+Delete asks first; Return cancels")
            xdotool("key", "shift+Delete")
            box = find_dialog("ImageViewer")
            if not box:
                fail("Shift+Delete did not ask for confirmation the second time")
            xdotool("windowfocus", "--sync", box)
            xdotool("key", "Tab")  # from Cancel to Delete
            xdotool("key", "space")
            wait_until("Shift+Delete then Delete removes d.png for good",
                       lambda: not os.path.exists(os.path.join(pictures, "d.png")))
            if os.path.exists(os.path.join(data, "Trash", "files", "d.png")):
                fail("d.png went to the trash instead of being deleted")
            wait_for("the previous image takes its place", refs["b"])

            # Another program adds a file, then deletes the one on screen.
            Image.fromarray(refs["e"].astype(np.uint8)).save(os.path.join(pictures, "e.png"))

            def end_shows_e():
                xdotool("key", "End")
                time.sleep(0.5)
                screen = grab()
                return screen is not None and find(screen, refs["e"]) is not None
            wait_until("a file created by another program appears in the folder", end_shows_e)
            os.remove(os.path.join(pictures, "e.png"))
            wait_for("the image deleted by another program is replaced", refs["b"])
            # An editor saves new content into the file on screen: it is decoded again.
            Image.fromarray(refs["e"].astype(np.uint8)).save(os.path.join(pictures, "b.png"))
            wait_for("an image rewritten by another program is shown again", refs["e"])

            # E14: the top overlay appears without moving or changing the image.
            screen = grab()
            before = find(screen, refs["e"]) if screen is not None else None
            if before is None:
                fail("image not found before showing the top overlay")

            def overlay_shown():
                shot = grab()
                if shot is None:
                    return False
                band = shot[y + 8:y + 48, x + w // 2 - 150:x + w // 2 + 150]
                return (band.min(axis=2) > 200).sum() > 30 and find(shot, refs["e"]) == before
            xdotool("mousemove", "--sync", str(x + w // 2), str(y + h // 2))
            xdotool("key", "shift+i")
            wait_until("Shift+I shows the top overlay, the image stays exactly in place", overlay_shown)

            # Checkerboard: a fully transparent image shows 8-pixel cells of the background
            # (#212121) and of a lighter grey, anchored to the image's top-left corner.
            Image.new("RGBA", (96, 64), (255, 0, 0, 0)).save(os.path.join(pictures, "f.png"))
            cells = (np.add.outer(np.arange(64) // 8, np.arange(96) // 8) % 2).astype(bool)

            def image_area(shot, dx, dy):
                cx, cy = x + (w - 96) // 2 + dx, y + (h - 64) // 2 + dy
                area = shot[cy:cy + 64, cx:cx + 96]
                return area if area.shape[:2] == (64, 96) else None

            def matches(expect_dark, expect_light):
                xdotool("key", "shift+Right")  # the last image (D-48; End is its second shortcut)
                shot = grab()
                for dy in (-1, 0, 1):  # the centred image may sit half a pixel either way
                    for dx in (-1, 0, 1):
                        area = image_area(shot, dx, dy) if shot is not None else None
                        if area is not None and np.abs(area[~cells] - expect_dark).max() <= 2 \
                                and np.abs(area[cells] - expect_light).max() <= 2:
                            return True
                return False
            # Before B: nothing but the background where the transparent image lies.
            wait_until("Shift+Right shows the transparent image over the plain background", lambda: matches(0x21, 0x21))
            xdotool("key", "b")
            wait_until("B shows the checkerboard behind the transparent image", lambda: matches(0x21, 0x40))

            # Settings: Apply takes effect while the dialog stays open, and Cancel keeps it.
            settings_path = os.path.join(config, "Cristallumnis", "ImageViewer.conf")

            def trash_confirmation_off():
                with open(settings_path, encoding="utf-8") as f:
                    return re.search(r"(?m)^confirmTrash=false$", f.read()) is not None

            def settings_open():
                return bool(xdotool("search", "--onlyvisible", "--name", "^Settings$").stdout.split())
            xdotool("key", "ctrl+comma")
            dialog = find_dialog("Settings")
            if not dialog:
                fail("Ctrl+, did not open the Settings dialog")
            xdotool("windowfocus", "--sync", dialog)
            xdotool("key", "Tab", "Tab", "space")  # past the language list: "Confirm before…" off
            info = dict(line.split("=", 1) for line in xdotool("getwindowgeometry", "--shell", dialog).stdout.split())
            xdotool("mousemove", "--sync", str(int(info["X"]) + int(info["WIDTH"]) - 50),
                    str(int(info["Y"]) + int(info["HEIGHT"]) - 24))
            xdotool("click", "1")  # Apply, the rightmost button
            wait_until("Apply saves the change while the dialog stays open", trash_confirmation_off)
            if not settings_open():
                fail("Apply closed the Settings dialog")
            xdotool("key", "Escape")
            wait_until("Cancel closes the Settings dialog", lambda: not settings_open())
            if not trash_confirmation_off():
                fail("Cancel undid what Apply had applied")

            # 5. Quit (Q, D-55); the session is saved.
            xdotool("key", "q")
            app.wait(15)
            if app.returncode != 0:
                print(log_text())
                fail(f"ImageViewer exited with code {app.returncode}")
            settings_file = os.path.join(config, "Cristallumnis", "ImageViewer.conf")
            store = configparser.ConfigParser(interpolation=None)
            store.read(settings_file)
            last = store.get("session", "lastFile", fallback="")
            if os.path.basename(last) != "f.png" or not store.get("session", "geometry", fallback="").startswith("@Rect("):
                fail(f"session not saved as expected in {settings_file}: lastFile={last!r}")
            print("ok   session saved (last file, window geometry)")

            # 6. Settings of 0.1 (group "general", which Qt writes as [%General] in INI files)
            #    load after a restart and are saved again under [app]: with "do not ask again"
            #    from 0.1, Delete moves the file to the trash at once. The side zones' old
            #    default width (200 px, stored by every save up to 0.2) becomes the new 100 px;
            #    the panels' old defaults (60 %, no outline, stored up to 0.3) become 70 % and
            #    the outline (D-49), and the overlay's in full screen (on hover) becomes "always".
            with open(settings_file, encoding="utf-8") as f:
                text = f.read()
            start = text.index("[app]")
            end = text.find("\n[", start + 1)
            text = text[:start] + text[end + 1 if end >= 0 else len(text):]
            text += "\n[%General]\nconfirmTrash=false\nlanguage=\nreopenLastImage=false\n"
            text = re.sub(r"(?m)^version=\d+$", "version=1", text)
            text = re.sub(r"(?m)^sideZoneWidth=\d+$", "sideZoneWidth=200", text)
            text = re.sub(r"(?m)^backgroundOpacity=\d+$", "backgroundOpacity=60", text)
            text = re.sub(r"(?m)^outline=\w+$", "outline=false", text)
            text = re.sub(r"(?m)^fullScreen=\w+$", "fullScreen=hover", text)
            # Up to 0.3 the application, and so its settings file, was called "imageViewer".
            earlier_file = os.path.join(config, "Cristallumnis", "imageViewer.conf")
            with open(earlier_file, "w", encoding="utf-8") as f:
                f.write(text)
            os.remove(settings_file)
            app = subprocess.Popen([exe, os.path.join(pictures, "a.png")], env=env, stdout=log, stderr=log)
            wait_for("restart shows the first image", refs["a"])
            xdotool("key", "Delete")
            wait_until("Delete trashes at once (the 0.1 setting was read)",
                       lambda: not os.path.exists(os.path.join(pictures, "a.png")))
            if xdotool("search", "--onlyvisible", "--name", "^ImageViewer$").stdout.split():
                fail("a confirmation was shown although the 0.1 settings said not to ask")
            xdotool("key", "q")
            app.wait(15)
            with open(settings_file, encoding="utf-8") as f:
                text = f.read()
            if "[%General]" in text or "confirmTrash=false" not in text.split("[app]", 1)[-1].split("\n[", 1)[0]:
                fail(f"settings not migrated to [app] in {settings_file}")
            if not re.search(r"(?m)^sideZoneWidth=100$", text) or not re.search(r"(?m)^version=3$", text):
                fail(f"the old 200 px side zones were not migrated to 100 px in {settings_file}")
            if not re.search(r"(?m)^backgroundOpacity=70$", text) or not re.search(r"(?m)^outline=true$", text):
                fail(f"the panels' old default style was not migrated in {settings_file}")
            if not re.search(r"(?m)^fullScreen=always$", text):
                fail(f"the overlay's old full-screen default (on hover) was not migrated in {settings_file}")
            if os.path.exists(earlier_file):
                fail("the settings file of the earlier name was not taken over")
            print("ok   0.3 settings file taken over; 0.1 settings migrated to [app]; old default side zones now 100 px; "
                  "panels 70 % and outlined; overlay always shown in full screen")

            # 7. Zoom modes, zoom lock and the title bar (D-51).
            zoom_dir = os.path.join(work, "zoom")
            os.makedirs(zoom_dir)
            for name, size, colour in (("big1.png", (2400, 1800), (200, 60, 30)), ("big2.png", (2400, 1800), (30, 60, 200)),
                                       ("small.png", (400, 300), (60, 200, 30))):
                Image.new("RGB", size, colour).save(os.path.join(zoom_dir, name))

            def set_option(text, group, key, value):
                if re.search(rf"(?m)^\[{group}\]$", text) is None:
                    text = text.rstrip("\n") + f"\n\n[{group}]\n"
                section = re.search(rf"(?ms)^\[{group}\]\n.*?(?=^\[|\Z)", text)
                body = re.sub(rf"(?m)^{key}=.*\n?", "", section.group(0))
                return text[:section.start()] + body.replace(f"[{group}]\n", f"[{group}]\n{key}={value}\n", 1) + text[section.end():]

            def edit_settings(*options):
                with open(settings_file, encoding="utf-8") as f:
                    text = f.read()
                for group, key, value in options:
                    text = set_option(text, group, key, value)
                with open(settings_file, "w", encoding="utf-8") as f:
                    f.write(text)

            def viewer():
                for wid in xdotool("search", "--onlyvisible", "--name", "ImageViewer$").stdout.split():
                    name = xdotool("getwindowname", wid).stdout.strip()
                    info = dict(line.split("=", 1) for line in xdotool("getwindowgeometry", "--shell", wid).stdout.split())
                    return name, int(info["WIDTH"]), int(info["HEIGHT"])
                return "", 0, 0

            def title_zoom(name, expected):
                def check():
                    title, width, height = viewer()
                    m = re.fullmatch(rf"{re.escape(name)} — [\d,]+ / [\d,]+ — [\d,]+ × [\d,]+ — .+ — ([\d.]+) % — ImageViewer",
                                 title)  # numbers as QLocale writes them: 1,200
                    want = expected(width, height)
                    return m is not None and abs(float(m.group(1)) - want) <= 0.06
                return check

            edit_settings(("window", "title", "everything"), ("shortcuts", "FitWidth", "X"))  # D-52
            app = subprocess.Popen([exe, os.path.join(zoom_dir, "big1.png")], env=env, stdout=log, stderr=log)
            fit = lambda w, h: round(min(w / 2400, h / 1800, 1.0) * 100, 1)
            wait_until("the title shows the name, position, size, file size and fitted zoom", title_zoom("big1.png", fit))
            xdotool("key", "x")
            wait_until("X, the user's shortcut for Fit to Width (D-52), fits the width", title_zoom("big1.png", lambda w, h: round(min(w / 2400, 1.0) * 100, 1)))
            xdotool("key", "1")
            wait_until("1 shows 100 %", title_zoom("big1.png", lambda w, h: 100.0))
            xdotool("key", "z")
            xdotool("key", "Right")
            wait_until("with the zoom locked (Z), the next image keeps 100 %", title_zoom("big2.png", lambda w, h: 100.0))
            xdotool("key", "z")
            xdotool("key", "Left")
            wait_until("unlocked, an image fits again", title_zoom("big1.png", fit))
            xdotool("key", "q")
            app.wait(15)

            edit_settings(("window", "matchImage", "every"), ("window", "matchImagePercent", "50"),
                          ("window", "title", "name"))
            app = subprocess.Popen([exe, os.path.join(zoom_dir, "small.png")], env=env, stdout=log, stderr=log)
            wait_until("the window takes the size of a small image (400 × 300)",
                       lambda: viewer() == ("small.png — ImageViewer", 400, 300))
            xdotool("key", "Home")
            # 2400 × 1800 within 50 % of 1600 × 1000: 800 × 500 at most, so 667 × 500.
            wait_until("a large image's window stays within 50 % of the screen, with its aspect ratio",
                       lambda: (lambda t, w, h: t == "big1.png — ImageViewer" and abs(w - 667) <= 1 and h == 500)(*viewer()))
            xdotool("key", "q")
            app.wait(15)

            # 8. Shortcuts (D-52): a hand-written unknown key leaves the defaults; a shifted symbol
            # ("!", Shift+1 on a US keyboard) is matched before the digit (Actual Size); the
            # Shortcuts tab records a key in place of the first shortcut and keeps the others.
            edit_settings(("window", "matchImage", "never"), ("shortcuts", "Next", "Foo"), ("shortcuts", "AboutQt", "!"))
            app = subprocess.Popen([exe, os.path.join(zoom_dir, "small.png")], env=env, stdout=log, stderr=log)
            wait_until("small.png shown", lambda: viewer()[0] == "small.png — ImageViewer")
            xdotool("key", "Right")
            wait_until("Right still goes to the next image when the settings name an unknown key (Next=Foo)",
                       lambda: viewer()[0] == "big1.png — ImageViewer")
            xdotool("key", "shift+1")
            about = find_dialog("About Qt")
            if not about:
                fail("Shift+1 did not run the command bound to \"!\" (About Qt)")
            print("ok   Shift+1 runs the command bound to \"!\", not Actual Size")
            xdotool("windowfocus", "--sync", about)
            xdotool("key", "Escape")
            wait_until("About Qt closes", lambda: not xdotool("search", "--onlyvisible", "--name", "^About Qt$").stdout.split())

            def stored_previous():
                with open(settings_file, encoding="utf-8") as f:
                    return re.search(r'(?m)^Previous="N; PgUp; Backspace"$', f.read()) is not None
            xdotool("key", "ctrl+comma")
            dialog = find_dialog("Settings")
            if not dialog:
                fail("Ctrl+, did not open the Settings dialog")
            xdotool("windowfocus", "--sync", dialog)
            xdotool("key", "ctrl+shift+Tab", "Tab")  # the last tab, Shortcuts; then its list
            xdotool("type", "--delay", "50", "Previous")  # Previous Image: Left, PgUp, Backspace
            xdotool("key", "Tab")
            xdotool("key", "n")  # recorded in the Shortcut field in place of Left
            time.sleep(0.5)
            xdotool("key", "Tab", "Tab")  # through the Alternative field, unchanged
            info = dict(line.split("=", 1) for line in xdotool("getwindowgeometry", "--shell", dialog).stdout.split())
            xdotool("mousemove", "--sync", str(int(info["X"]) + int(info["WIDTH"]) - 50),
                    str(int(info["Y"]) + int(info["HEIGHT"]) - 24))
            xdotool("click", "1")  # Apply
            wait_until("a key recorded in the Shortcuts tab replaces the first shortcut, the others stay", stored_previous)
            xdotool("key", "Escape")
            xdotool("key", "q")
            app.wait(15)

            # 9. The pointer and the toggles (D-56): still over the image, the pointer hides after
            # 2 s (the default) and a move shows it again; T and C are saved as settings.
            pointer_hidden = open_xfixes()
            app = subprocess.Popen([exe, os.path.join(zoom_dir, "small.png")], env=env, stdout=log, stderr=log)
            wait_until("small.png shown", lambda: viewer()[0] == "small.png — ImageViewer")
            x, y, w, h = window_geometry()
            xdotool("mousemove", "--sync", str(x + w // 2), str(y + h // 2))
            moved = time.monotonic()
            wait_until("a still pointer hides", pointer_hidden)
            if time.monotonic() - moved < 1.8:
                fail(f"the pointer hid {time.monotonic() - moved:.1f} s after a move, before 2 s")
            xdotool("mousemove", "--sync", str(x + w // 2 + 20), str(y + h // 2))
            wait_until("a move shows the pointer again", lambda: not pointer_hidden())

            def stored(pattern):
                with open(settings_file, encoding="utf-8") as f:
                    return re.search(pattern, f.read()) is not None
            xdotool("key", "t", "c")
            wait_until("T and C toggled from the keyboard are saved as settings",
                       lambda: stored(r"(?m)^toneMap=false$") and stored(r"(?m)^clipWarning=true$"))
            xdotool("key", "q")
            app.wait(15)
        finally:
            if app.poll() is None:
                app.terminate()
                app.wait(10)
finally:
    xvfb.stop(server)

if app.returncode != 0:
    print(open(log_path, encoding="utf-8", errors="replace").read())
    fail(f"ImageViewer exited with code {app.returncode}")
print(f"{rhi}: all interaction checks passed")
