"""On-screen fidelity test (Linux, Xvfb): F8 at 100 % in SDR.

Opens a synthetic 8-bit sRGB ramp in imageViewer on a virtual X display, grabs
the screen and requires the image region to match the file exactly. This
exercises the whole chain: decode -> linear scRGB FP16 -> GPU -> sRGB encode.

The screen is polled until the image appears (or a deadline passes) instead of
sleeping for a fixed time: on CI runners the first lavapipe/llvmpipe frame can
take several seconds (run 4 failed a fixed 6 s wait).

usage: python3 tests/screen_test.py <imageViewer> [vulkan|opengl]
needs: Xvfb, xwd (x11-apps), ImageMagick `convert`, numpy, Pillow
env:   SCREEN_TEST_DIR  where to keep the log and the last screenshot (default: a temp dir)
"""
import os
import subprocess
import sys
import tempfile
import time

import numpy as np
from PIL import Image

DEADLINE_S = 60
POLL_S = 0.5

exe = os.path.abspath(sys.argv[1])
rhi = sys.argv[2] if len(sys.argv) > 2 else "vulkan"
work = os.environ.get("SCREEN_TEST_DIR") or tempfile.mkdtemp(prefix="imageviewer-screen-")
os.makedirs(work, exist_ok=True)

ramp = np.tile(np.arange(256, dtype=np.uint8), (64, 1))
reference = np.stack([ramp, ramp[:, ::-1], np.full_like(ramp, 128)], axis=-1)
image_path = os.path.join(work, "ramp8.png")
Image.fromarray(reference).save(image_path)
ref = reference.astype(int)
h, w = ref.shape[:2]

display = ":97"
env = dict(os.environ, DISPLAY=display, QT_QPA_PLATFORM="xcb", IMAGEVIEWER_RHI=rhi,
           IMAGEVIEWER_OUTPUT="sdr", LC_ALL="C.UTF-8", QT_LOGGING_RULES="imageviewer.*=true",
           QT_MESSAGE_PATTERN="%{time process} %{category}: %{message}")
log_path = os.path.join(work, f"viewer-{rhi}.log")
shot_path = os.path.join(work, f"screen-{rhi}.png")


def grab():
    """Screen as an int array, or None while the X server is not accepting connections."""
    result = subprocess.run(f"xwd -root -silent -display {display} | convert xwd:- {shot_path}",
                            shell=True, stderr=subprocess.DEVNULL)
    if result.returncode != 0 or not os.path.exists(shot_path):
        return None
    return np.asarray(Image.open(shot_path).convert("RGB")).astype(int)


def locate(screen):
    """Best placement of the reference on screen as (max |diff|, x, y, fraction differing), or None."""
    sh, sw = screen.shape[:2]
    if sh < h or sw < w:
        return None
    # Candidates: top-left and bottom-right pixels both close to the reference corners.
    first = (np.abs(screen - ref[0, 0]) <= 2).all(axis=-1)
    last = (np.abs(screen - ref[-1, -1]) <= 2).all(axis=-1)
    candidates = np.argwhere(first[:sh - h + 1, :sw - w + 1] & last[h - 1:, w - 1:])
    best = None
    for y, x in candidates:
        diff = np.abs(screen[y:y + h, x:x + w] - ref)
        if best is None or diff.max() < best[0]:
            best = (int(diff.max()), int(x), int(y), float((diff > 0).mean()))
    return best


xvfb = subprocess.Popen(["Xvfb", display, "-screen", "0", "1600x1000x24", "-nolisten", "tcp"],
                        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
best = None
try:
    start = time.monotonic()
    while grab() is None:
        if time.monotonic() - start > 10:
            sys.exit("FAIL: Xvfb did not start")
        time.sleep(0.2)
    with open(log_path, "w") as log:
        app = subprocess.Popen([exe, image_path], env=env, stdout=log, stderr=log)
        try:
            start = time.monotonic()
            while time.monotonic() - start < DEADLINE_S and app.poll() is None:
                time.sleep(POLL_S)
                screen = grab()
                found = locate(screen) if screen is not None else None
                if found is not None and (best is None or found[0] <= best[0]):
                    best = found
                if best is not None and best[0] == 0:
                    break
            elapsed = time.monotonic() - start
        finally:
            app.terminate()
            app.wait(10)
finally:
    xvfb.terminate()
    xvfb.wait(10)

print(open(log_path).read())
if app.returncode not in (None, -15, 0) and best is None:
    print(f"FAIL: {rhi}: imageViewer exited with code {app.returncode}")
    sys.exit(1)
if best is None:
    print(f"FAIL: {rhi}: image not found on screen after {elapsed:.1f} s (see {shot_path})")
    sys.exit(1)
print(f"{rhi}: image at ({best[1]},{best[2]}) after {elapsed:.1f} s, max |diff| = {best[0]} levels, "
      f"{best[3] * 100:.2f}% pixels differ")
sys.exit(0 if best[0] == 0 else 2)
