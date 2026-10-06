"""On-screen fidelity test (Linux, Xvfb): F8 at 100 % in SDR.

Opens a synthetic 8-bit sRGB ramp in imageViewer on a virtual X display, grabs
the screen and requires the image region to match the file exactly. This
exercises the whole chain: decode -> linear scRGB FP16 -> GPU -> sRGB encode.

usage: python3 tests/screen_test.py <imageViewer> [vulkan|opengl]
needs: Xvfb, xwd (x11-apps), ImageMagick `convert`, numpy, Pillow
"""
import os
import subprocess
import sys
import tempfile
import time

import numpy as np
from PIL import Image

exe = os.path.abspath(sys.argv[1])
rhi = sys.argv[2] if len(sys.argv) > 2 else "vulkan"
work = tempfile.mkdtemp(prefix="imageviewer-screen-")

ramp = np.tile(np.arange(256, dtype=np.uint8), (64, 1))
reference = np.stack([ramp, ramp[:, ::-1], np.full_like(ramp, 128)], axis=-1)
image_path = os.path.join(work, "ramp8.png")
Image.fromarray(reference).save(image_path)

display = ":97"
env = dict(os.environ, DISPLAY=display, QT_QPA_PLATFORM="xcb", IMAGEVIEWER_RHI=rhi,
           IMAGEVIEWER_OUTPUT="sdr", LC_ALL="C.UTF-8", QT_LOGGING_RULES="imageviewer.*=true")
xvfb = subprocess.Popen(["Xvfb", display, "-screen", "0", "1600x1000x24", "-nolisten", "tcp"],
                        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
log_path = os.path.join(work, "viewer.log")
shot_path = os.path.join(work, "screen.png")
try:
    time.sleep(1.5)
    with open(log_path, "w") as log:
        app = subprocess.Popen([exe, image_path], env=env, stdout=log, stderr=log)
        try:
            time.sleep(6)
            subprocess.run(f"xwd -root -silent -display {display} | convert xwd:- {shot_path}", shell=True, check=True)
        finally:
            app.terminate()
            app.wait(10)
finally:
    xvfb.terminate()
    xvfb.wait(10)

screen = np.asarray(Image.open(shot_path).convert("RGB")).astype(int)
ref = reference.astype(int)
h, w = ref.shape[:2]
best = None
for y in range(screen.shape[0] - h + 1):
    for x in range(screen.shape[1] - w + 1):
        if abs(screen[y, x] - ref[0, 0]).max() > 2 or abs(screen[y + h - 1, x + w - 1] - ref[-1, -1]).max() > 2:
            continue
        diff = np.abs(screen[y:y + h, x:x + w] - ref)
        if best is None or diff.max() < best[0]:
            best = (int(diff.max()), x, y, float((diff > 0).mean()))

print(open(log_path).read())
if best is None:
    print(f"FAIL: image not found on screen (see {shot_path})")
    sys.exit(1)
print(f"{rhi}: image at ({best[1]},{best[2]}), max |diff| = {best[0]} levels, {best[3] * 100:.2f}% pixels differ")
sys.exit(0 if best[0] == 0 else 2)
