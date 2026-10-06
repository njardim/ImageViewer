"""Output-stage fidelity test (criteria H2, H4, H6): offscreen GPU render vs. specification.

Writes a synthetic linear HDR corpus, renders it with `imageViewer --render` for
several outputs (SDR, EDR, scRGB, PQ; tone mapping on and off; exposure), and
checks two things:
  1. the harness itself: GPU result == color::applyOutputStage() (exit code 0);
  2. independently of the C++ code, on the GPU result: identity up to the
     BT.2390 knee (computed here from the ITU-R formula), never above the
     output peak, monotonic, content peak lands on the output peak, and plain
     clipping when tone mapping is off.

usage: python3 tests/render_test.py <imageViewer> [vulkan|opengl]
needs: Xvfb (the Vulkan and OpenGL loaders need a display), numpy
"""
import os
import subprocess
import sys
import tempfile
import time

import numpy as np

exe = os.path.abspath(sys.argv[1])
rhi = sys.argv[2] if len(sys.argv) > 2 else "vulkan"
work = os.environ.get("RENDER_TEST_DIR") or tempfile.mkdtemp(prefix="imageviewer-render-")
os.makedirs(work, exist_ok=True)

WHITE_NITS = 203.0  # BT.2408 reference white = working value 1.0
PQ_M1, PQ_M2 = 2610 / 16384, 2523 / 4096 * 128
PQ_C1, PQ_C2, PQ_C3 = 3424 / 4096, 2413 / 4096 * 32, 2392 / 4096 * 32


def pq_encode(nits):
    y = np.clip(np.asarray(nits, dtype=np.float64) / 10000, 0, 1) ** PQ_M1
    return ((PQ_C1 + PQ_C2 * y) / (1 + PQ_C3 * y)) ** PQ_M2


def pq_decode(e):
    p = np.clip(np.asarray(e, dtype=np.float64), 0, 1) ** (1 / PQ_M2)
    return 10000 * (np.maximum(p - PQ_C1, 0) / (PQ_C2 - PQ_C3 * p)) ** (1 / PQ_M1)


def srgb_decode(v):
    v = np.asarray(v, dtype=np.float64)
    return np.where(v <= 0.04045, v / 12.92, ((v + 0.055) / 1.055) ** 2.4)


def bt2390_knee_nits(source_nits, target_nits):
    """ITU-R BT.2390 §5.4.1 with zero black levels: KS = 1.5 * maxLum - 0.5 (PQ domain)."""
    max_lum = pq_encode(target_nits) / pq_encode(source_nits)
    return float(pq_decode(max(1.5 * max_lum - 0.5, 0.0) * pq_encode(source_nits)))


# Linear BT.709 -> BT.2020 (ITU-R BT.2087); the PQ output is checked back in BT.709.
BT709_TO_BT2020 = np.array([[0.6274039, 0.3292830, 0.0433131],
                            [0.0690973, 0.9195404, 0.0113623],
                            [0.0163914, 0.0880133, 0.8955953]])


def pq_decode_bt709(e):
    return pq_decode(e) @ np.linalg.inv(BT709_TO_BT2020).T


def write_pfm(path, rgb):
    h, w, _ = rgb.shape
    with open(path, "wb") as f:
        f.write(f"PF\n{w} {h}\n-1.0\n".encode())
        f.write(np.ascontiguousarray(rgb[::-1], dtype="<f4").tobytes())


def read_pfm(path):
    with open(path, "rb") as f:
        assert f.readline().strip() == b"PF"
        w, h = map(int, f.readline().split())
        scale = float(f.readline())
        data = np.frombuffer(f.read(), dtype="<f4" if scale < 0 else ">f4")
    return data.reshape(h, w, 3)[::-1].astype(np.float64)


# Corpus: 512 log-spaced levels from 0.001 to 30x SDR white (0.2 to 6090 nits), in rows of
# 4 pixels per hue. Row 0 is neutral; the last hue has components outside BT.709.
levels = np.geomspace(1e-3, 30.0, 512)
hues = [(1.0, 1.0, 1.0), (1.0, 0.2, 0.05), (0.05, 0.1, 1.0), (1.2, -0.04, -0.02)]
corpus = np.concatenate([np.repeat((levels[:, None] * np.array(h))[None], 4, axis=0) for h in hues])
corpus_path = os.path.join(work, "corpus.pfm")
write_pfm(corpus_path, corpus)
levels16 = levels.astype(np.float16).astype(np.float64)  # the decoder stores RGBA16F
content_peak = corpus.max()  # brightest component, working units (36.0 is exact in FP16)
content_luminance = (corpus @ np.array([0.2126, 0.7152, 0.0722])).max()

# (name, harness arguments, scale per working unit, peak and nits per output unit, decoder)
linear = lambda v: v
CASES = [
    ("sdr", ["--output", "sdr"], 1.0, 1.0, WHITE_NITS, srgb_decode, True),
    ("sdr-clip", ["--output", "sdr", "--no-tonemap"], 1.0, 1.0, WHITE_NITS, srgb_decode, False),
    ("sdr-exposure", ["--output", "sdr", "--exposure", "-6"], 1.0, 1.0, WHITE_NITS, srgb_decode, True),
    ("edr", ["--output", "edr", "--peak", "4"], 1.0, 4.0, WHITE_NITS, linear, True),
    ("scrgb", ["--output", "scrgb", "--white", "240", "--peak", "600"], 3.0, 7.5, 80.0, linear, True),
    ("pq", ["--output", "pq", "--white", "203", "--peak", "1000"], 203.0, 1000.0, 1.0, pq_decode_bt709, True),
    ("pq-clip", ["--output", "pq", "--peak", "1000", "--no-tonemap"], 203.0, 1000.0, 1.0, pq_decode_bt709, False),
]

display = ":98"
env = dict(os.environ, DISPLAY=display, QT_QPA_PLATFORM="xcb", IMAGEVIEWER_RHI=rhi, LC_ALL="C.UTF-8")
xvfb = subprocess.Popen(["Xvfb", display, "-screen", "0", "640x480x24", "-nolisten", "tcp"],
                        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
failures = []
try:
    time.sleep(1.0)
    for name, args, scale, peak, nits_per_unit, decode, tone_map in CASES:
        pfm = os.path.join(work, f"{rhi}-{name}.pfm")
        run = subprocess.run([exe, "--render", corpus_path, "--pfm", pfm, *args], env=env,
                             capture_output=True, text=True, timeout=120)
        print(f"--- {rhi} {name}\n{run.stdout.strip()}")
        if run.returncode != 0:
            failures.append(f"{name}: harness exit {run.returncode} {run.stderr.strip()[-500:]}")
            continue

        out = decode(read_pfm(pfm))  # linear BT.709, output units
        exposure = 2.0 ** float(args[args.index("--exposure") + 1]) if "--exposure" in args else 1.0
        k = exposure * scale
        source = levels16 * k                    # neutral row as decoded (FP16), output units
        neutral = out[0]                         # first row is neutral: R = G = B
        source_peak = content_peak * k
        tone_mapped = tone_map and content_luminance * k > peak  # same activation rule as Renderer::stageFor
        problems = []
        if np.abs(neutral[:, 0] - neutral[:, 1]).max() > 1e-4 * max(1.0, peak):
            problems.append("neutral input is not neutral on output")
        y = neutral[:, 1]
        if y.max() > peak * (1 + 1e-4):
            problems.append(f"output {y.max():.6g} above peak {peak}")
        if np.any(np.diff(y) < -1e-5 * peak):
            problems.append("output not monotonic")
        if tone_mapped:
            knee = bt2390_knee_nits(source_peak * nits_per_unit, peak * nits_per_unit) / nits_per_unit
            below = source < knee * 0.999
            err = np.abs(y[below] - source[below]) / np.maximum(1.0, source[below])
            if err.max() > 2e-4:
                problems.append(f"not identity below the knee ({knee:.4g}): max error {err.max():.3g}")
            # The brightest component in the corpus must land exactly on the output peak (hue row 3).
            brightest = out.max()
            if abs(brightest - peak) > 1e-3 * peak:
                problems.append(f"content peak maps to {brightest:.6g}, expected {peak}")
            print(f"    knee {knee * nits_per_unit:.1f} nits, identity below it: max error {err.max():.2g}")
        else:
            expected = np.minimum(source, peak)
            err = np.abs(y - expected) / np.maximum(1.0, expected)
            if err.max() > 2e-4:
                problems.append(f"not identity/clip: max error {err.max():.3g}")
            print(f"    identity/clip: max error {err.max():.2g}")
        failures += [f"{name}: {p}" for p in problems]
finally:
    xvfb.terminate()
    xvfb.wait(10)

if failures:
    print("FAIL\n  " + "\n  ".join(failures))
    sys.exit(1)
print(f"{rhi}: all {len(CASES)} output-stage cases pass")
