#!/usr/bin/env python3
"""Animation and slideshow test on Xvfb, driven with xdotool (E6, E11, decision D-40).

A folder holds an animated GIF (red, green, blue frames of 400 ms, looping) and a yellow
still image. The colour in the middle of the window shows what is on screen:
  1. the animation plays: red, green and blue follow one another;
  2. K pauses it (the colour stays put), "." and "," step one frame forward and back,
     K plays again;
  3. S starts the slideshow (1 s per image, from the settings): the yellow image follows
     by itself; Esc stops it and the image stays.
  4. The slideshow's toolbar (D-65), below the top overlay: pause and stop while it runs; a
     click on pause holds the image and turns it into play, play goes on; a still pointer
     hides the toolbar after the time in the settings, a move shows it again; stop ends it.
  5. With "Start the slideshow in full screen", S fills the screen and Esc gives the window back.

usage: python3 tests/animation_test.py <ImageViewer> [vulkan|opengl]
needs: Xvfb, xdotool, xwd (x11-apps), ImageMagick `convert`, numpy, Pillow
env:   ANIMATION_TEST_DIR  where to keep the files, the log and the last screenshot
"""
import os
import shutil
import subprocess
import sys
import tempfile
import time

import numpy as np
from PIL import Image

import xvfb

DEADLINE_S = 30
POLL_S = 0.1

exe = os.path.abspath(sys.argv[1])
rhi = sys.argv[2] if len(sys.argv) > 2 else "vulkan"
work = os.environ.get("ANIMATION_TEST_DIR") or tempfile.mkdtemp(prefix="imageviewer-animation-")
shutil.rmtree(work, ignore_errors=True)
pictures = os.path.join(work, "pictures")
config = os.path.join(work, "config")
os.makedirs(pictures)
os.makedirs(os.path.join(config, "Cristallumnis"))
log_path = os.path.join(work, f"viewer-{rhi}.log")
shot_path = os.path.join(work, f"screen-{rhi}.png")

COLOURS = {"red": (255, 0, 0), "green": (0, 255, 0), "blue": (0, 0, 255), "yellow": (255, 255, 0)}
frames = [Image.new("RGB", (160, 120), COLOURS[c]) for c in ("red", "green", "blue")]
frames[0].save(os.path.join(pictures, "a-anim.gif"), save_all=True, append_images=frames[1:], duration=400, loop=0)
Image.new("RGB", (160, 120), COLOURS["yellow"]).save(os.path.join(pictures, "b-still.png"))
with open(os.path.join(config, "Cristallumnis", "ImageViewer.conf"), "w") as f:
    f.write("[General]\nversion=2\n\n[navigation]\nslideshowSeconds=1\n\n[window]\nshowInfo=false\n")


def fail(message):
    print(f"FAIL: {rhi}: {message}")
    if os.path.exists(log_path):
        print(open(log_path, encoding="utf-8", errors="replace").read())
    sys.exit(1)


def xdotool(*args):
    return subprocess.run(["xdotool", *args], env=env, capture_output=True, text=True, timeout=30)


def colour():
    """Name of the colour in the middle of the viewer, or None."""
    result = subprocess.run(f"xwd -root -silent -display {display} | convert xwd:- {shot_path}", shell=True,
                            stderr=subprocess.DEVNULL)
    if result.returncode != 0:
        return None
    screen = np.asarray(Image.open(shot_path).convert("RGB")).astype(int)
    pixel = screen[geometry[1] + geometry[3] // 2, geometry[0] + geometry[2] // 2]
    for name, rgb in COLOURS.items():
        if np.abs(pixel - rgb).max() <= 2:
            return name
    return None


def wait_for(what, names):
    start = time.monotonic()
    while time.monotonic() - start < DEADLINE_S:
        if app.poll() is not None:
            fail(f"{what}: ImageViewer exited with code {app.returncode}")
        seen = colour()
        if seen in names:
            print(f"ok   {what}: {seen} ({time.monotonic() - start:.1f} s)")
            return seen
        time.sleep(POLL_S)
    fail(f"{what}: not after {DEADLINE_S} s (see {shot_path})")


def toolbar():
    """(glyph, x, y) of the slideshow toolbar's first button: "pause" or "play", with its
    centre on the screen; None when the toolbar is not shown. The toolbar's glyphs are the only
    near-white pixels in the window's top band."""
    result = subprocess.run(f"xwd -root -silent -display {display} | convert xwd:- {shot_path}", shell=True,
                            stderr=subprocess.DEVNULL)
    if result.returncode != 0:
        return None
    screen = np.asarray(Image.open(shot_path).convert("RGB")).astype(int)
    x0, y0, w, _ = geometry
    band = screen[y0:y0 + 150, x0:x0 + w]
    ys, xs = np.nonzero(band.min(axis=2) >= 225)
    if len(xs) < 40:
        return None
    # Two glyphs: the first is everything left of the widest gap between white columns.
    columns = np.unique(xs)
    split = columns[np.argmax(np.diff(columns))] if len(columns) > 1 else columns[-1]
    first = xs <= split
    left, right = xs[first].min(), xs[first].max()
    middle = (left + right) // 2
    # Pause is two bars with a gap in the middle; play's triangle is solid there.
    glyph = "play" if np.any(xs[first] == middle) else "pause"
    return glyph, x0 + int(xs[first].mean()), y0 + int(ys[first].mean())


def toolbar_is(what, glyph):
    start = time.monotonic()
    while time.monotonic() - start < DEADLINE_S:
        if app.poll() is not None:
            fail(f"{what}: ImageViewer exited with code {app.returncode}")
        seen = toolbar()
        if (seen[0] if seen else None) == glyph:
            print(f"ok   {what} ({time.monotonic() - start:.1f} s)")
            return seen
        time.sleep(POLL_S)
    fail(f"{what}: toolbar shows {seen[0] if seen else 'nothing'} instead of {glyph} (see {shot_path})")


def stays(what, name, seconds):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        seen = colour()
        if seen != name:
            fail(f"{what}: {seen} instead of {name}")
        time.sleep(POLL_S)
    print(f"ok   {what}: {name} for {seconds:.1f} s")


NEXT = {"red": "green", "green": "blue", "blue": "red"}
PREVIOUS = {v: k for k, v in NEXT.items()}

server, display = xvfb.start("1280x800x24")
env = dict(os.environ, DISPLAY=display, QT_QPA_PLATFORM="xcb", IMAGEVIEWER_RHI=rhi, IMAGEVIEWER_OUTPUT="sdr",
           LC_ALL="C.UTF-8", LANGUAGE="en", HOME=work, XDG_CONFIG_HOME=config, XDG_DATA_HOME=work,
           QT_LOGGING_RULES="imageviewer.*=true")
app = None
try:
    with open(log_path, "w") as log:
        app = subprocess.Popen([exe, os.path.join(pictures, "a-anim.gif")], env=env, stdout=log, stderr=log)
        try:
            found = []
            start = time.monotonic()
            while not found and time.monotonic() - start < DEADLINE_S:
                found = xdotool("search", "--onlyvisible", "--name", "ImageViewer$").stdout.split()
                time.sleep(POLL_S)
            if not found:
                fail("viewer window not found")
            info = dict(line.split("=", 1) for line in xdotool("getwindowgeometry", "--shell", found[0]).stdout.split())
            geometry = (int(info["X"]), int(info["Y"]), int(info["WIDTH"]), int(info["HEIGHT"]))
            xdotool("windowfocus", "--sync", found[0])

            # 1. It plays: every colour comes in turn.
            first = wait_for("the animation is shown", {"red", "green", "blue"})
            second = wait_for("the next frame follows by itself", {NEXT[first]})
            wait_for("and the one after it", {NEXT[second]})

            # 2. K pauses; "." and "," step; K plays again.
            xdotool("key", "space")
            time.sleep(0.3)
            paused = colour()
            if paused not in NEXT:
                fail(f"after K: {paused} on screen")
            stays("K pauses the animation", paused, 1.5)
            xdotool("key", "ctrl+Right")
            wait_for("'.' shows the next frame", {NEXT[paused]})
            stays("and it stays", NEXT[paused], 1.0)
            xdotool("key", "ctrl+Left")
            wait_for("',' goes back a frame", {paused})
            xdotool("key", "space")
            wait_for("K plays again", {NEXT[paused]})

            # 3. S: the slideshow moves on by itself; Esc stops it.
            xdotool("key", "s")
            wait_for("S: the slideshow shows the next image", {"yellow"})
            wait_for("the slideshow goes on (the folder loops)", {"red", "green", "blue"})
            wait_for("and comes back to the still image", {"yellow"})
            xdotool("key", "Escape")
            stays("Esc stops the slideshow", "yellow", 2.5)
            if toolbar() is not None:
                fail("the slideshow's toolbar stayed after Esc")

            # 4. The toolbar (D-65).
            centre = (geometry[0] + geometry[2] // 2, geometry[1] + geometry[3] // 2)
            xdotool("mousemove", "--sync", str(centre[0]), str(centre[1] + 150))  # off the image and the toolbar
            xdotool("key", "s")
            glyph, bx, by = toolbar_is("S shows the toolbar with pause", "pause")
            xdotool("mousemove", "--sync", str(bx), str(by))
            xdotool("click", "1")
            toolbar_is("a click on pause turns it into play", "play")
            held = colour()
            stays("and the slideshow holds its image", held, 2.5)
            xdotool("click", "1")
            toolbar_is("a click on play turns it back into pause", "pause")
            wait_for("and the slideshow goes on", set(COLOURS) - {held})
            xdotool("mousemove", "--sync", str(centre[0]), str(centre[1] + 150))
            moved = time.monotonic()
            toolbar_is("a still pointer hides the toolbar", None)
            if time.monotonic() - moved < 1.8:
                fail(f"the toolbar hid {time.monotonic() - moved:.1f} s after a move, before 2 s")
            xdotool("mousemove", "--sync", str(centre[0] + 20), str(centre[1] + 150))
            glyph, bx, by = toolbar_is("a move shows it again", "pause")
            stop_x = 2 * centre[0] - bx  # the stop button mirrors the first about the middle
            xdotool("mousemove", "--sync", str(stop_x), str(by))
            wait_for("the slideshow comes to the still image", {"yellow"})  # the animation changes by itself
            xdotool("click", "1")
            toolbar_is("a click on stop takes the toolbar away", None)
            stays("and stops the slideshow", "yellow", 2.5)
            xdotool("key", "q")
            app.wait(15)

            # 5. The slideshow in full screen, given back by Esc.
            with open(os.path.join(config, "Cristallumnis", "ImageViewer.conf"), "a") as f:
                f.write("\n[navigation]\nslideshowFullScreen=true\n")
            app = subprocess.Popen([exe, os.path.join(pictures, "b-still.png")], env=env, stdout=log, stderr=log)
            wait_for("the still image is shown again", {"yellow"})

            # Xvfb has no window manager to resize the window: the viewer's log says what it asked for.
            def logged(what, count):
                start = time.monotonic()
                while time.monotonic() - start < DEADLINE_S:
                    with open(log_path, encoding="utf-8", errors="replace") as f:
                        if f.read().count(what) >= count:
                            print(f"ok   {what} ({time.monotonic() - start:.1f} s)")
                            return
                    time.sleep(POLL_S)
                fail(f"no {what!r} in the log")
            with open(log_path, encoding="utf-8", errors="replace") as f:
                windowed = f.read().count("visibility QWindow::Windowed")
            xdotool("windowfocus", "--sync", xdotool("search", "--onlyvisible", "--name", "ImageViewer$").stdout.split()[0])
            xdotool("key", "s")
            logged("visibility QWindow::FullScreen", 1)
            xdotool("key", "Escape")
            logged("visibility QWindow::Windowed", windowed + 1)
            xdotool("key", "q")
            app.wait(15)
        finally:
            if app.poll() is None:
                app.terminate()
                app.wait(10)
finally:
    xvfb.stop(server)

if app.returncode != 0:
    fail(f"ImageViewer exited with code {app.returncode}")
print(f"{rhi}: all animation checks passed")
