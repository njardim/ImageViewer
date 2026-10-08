#!/usr/bin/env python3
"""Animation and slideshow test on Xvfb, driven with xdotool (E6, E11, decision D-40).

A folder holds an animated GIF (red, green, blue frames of 400 ms, looping) and a yellow
still image. The colour in the middle of the window shows what is on screen:
  1. the animation plays: red, green and blue follow one another;
  2. K pauses it (the colour stays put), "." and "," step one frame forward and back,
     K plays again;
  3. S starts the slideshow (1 s per image, from the settings): the yellow image follows
     by itself; Esc stops it and the image stays.

usage: python3 tests/animation_test.py <imageViewer> [vulkan|opengl]
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
with open(os.path.join(config, "Cristallumnis", "imageViewer.conf"), "w") as f:
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
            fail(f"{what}: imageViewer exited with code {app.returncode}")
        seen = colour()
        if seen in names:
            print(f"ok   {what}: {seen} ({time.monotonic() - start:.1f} s)")
            return seen
        time.sleep(POLL_S)
    fail(f"{what}: not after {DEADLINE_S} s (see {shot_path})")


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
                found = xdotool("search", "--onlyvisible", "--name", "imageViewer$").stdout.split()
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
            xdotool("key", "k")
            time.sleep(0.3)
            paused = colour()
            if paused not in NEXT:
                fail(f"after K: {paused} on screen")
            stays("K pauses the animation", paused, 1.5)
            xdotool("key", "period")
            wait_for("'.' shows the next frame", {NEXT[paused]})
            stays("and it stays", NEXT[paused], 1.0)
            xdotool("key", "comma")
            wait_for("',' goes back a frame", {paused})
            xdotool("key", "k")
            wait_for("K plays again", {NEXT[paused]})

            # 3. S: the slideshow moves on by itself; Esc stops it.
            xdotool("key", "s")
            wait_for("S: the slideshow shows the next image", {"yellow"})
            wait_for("the slideshow goes on (the folder loops)", {"red", "green", "blue"})
            wait_for("and comes back to the still image", {"yellow"})
            xdotool("key", "Escape")
            stays("Esc stops the slideshow", "yellow", 2.5)

            xdotool("key", "ctrl+q")
            app.wait(15)
        finally:
            if app.poll() is None:
                app.terminate()
                app.wait(10)
finally:
    xvfb.stop(server)

if app.returncode != 0:
    fail(f"imageViewer exited with code {app.returncode}")
print(f"{rhi}: all animation checks passed")
