"""Output-stage fidelity test (criteria H2, H4, H6): offscreen GPU render vs. specification.

Writes a synthetic linear HDR corpus, renders it with `ImageViewer --render` for
several outputs (SDR, EDR, scRGB, PQ; tone mapping on and off; exposure), and
checks two things:
  1. the harness itself: GPU result == color::applyOutputStage() (exit code 0);
  2. independently of the C++ code, on the GPU result: identity up to the
     BT.2390 knee (computed here from the ITU-R formula), never above the
     output peak, monotonic, content peak lands on the output peak, hue kept
     by the tone mapping (D-15), and plain clipping when tone mapping is off.
The backend that actually rendered must be the one requested. It also runs
`ImageViewer --panel-check` in each output: the information panels' contrast (D-49).

PQ and HLG files (H1, H2; D-65): ramps in PNG (cICP), JPEG XL and AVIF from tests/hdr_corpus.py
are decoded and drawn on an extended-range output that neither clips nor maps them (H1: the
decoded light against ST 2084 and BT.2100, within 0.1 %), and on a PQ output with a
10,000-nit peak (H2: content within the display's range comes out in the same absolute nits).
A local build whose decoders cannot read a file's colour (OpenImageIO 2.4 ignores cICP) or
depth skips it; CI ($CI set) fails instead.

SVG (D-71, D-90): files that name an image, a style sheet or an SVG outside the document (beside
it, by path, by entity, by an ATTLIST default, inside an embedded SVG) must draw nothing from it;
embedded PNG, JPEG, GIF and WebP images must still show; an svgz inflating past the limit fails.
Ten feature cases (clip path, drop shadow, CSS selectors, mask, turbulence, blending, a reflected
gradient, a rotated pattern, a nested SVG, an Illustrator export's classes) are drawn within a few
levels of Chromium 153's rendering (tests/data/svg-cases.chromium.png), the owner's artwork too
(tests/data/IV_claude_v5_mix_v1.chromium.png); two cases with text draw it, in whichever fonts the
system has.

usage: python3 tests/render_test.py <ImageViewer> [vulkan|opengl]
  Linux:          Xvfb + xcb; Vulkan (default) or OpenGL.
  Windows, macOS: offscreen platform; the native backend (D3D11, Metal), no argument.
exit:  0 pass, 1 fail, 4 no usable GPU/QRhi on this machine (the harness's own code)
needs: numpy, Pillow; on Linux also Xvfb
env:   RENDER_TEST_DIR  where to keep the corpus and the GPU results (default: a temp dir)
"""
import base64
import gzip
import os
import platform
import re
import struct
import subprocess
import sys
import tempfile
import zlib

import numpy as np
from PIL import Image

import hdr_corpus as hdr

SYSTEM = platform.system()
NATIVE = {"Windows": "d3d11", "Darwin": "metal"}
BACKEND_NAMES = {"vulkan": "Vulkan", "opengl": "OpenGL", "d3d11": "D3D11", "metal": "Metal"}
GPU_UNAVAILABLE = 4  # ImageViewer --render: the GPU/QRhi could not be initialised
PQ_NEUTRAL_TOLERANCE = 5e-5  # PQ code value: 1/20 of a 10-bit step (1/1023), far below a visible error

sys.stdout.reconfigure(encoding="utf-8", errors="replace")
exe = os.path.abspath(sys.argv[1])
rhi = sys.argv[2] if len(sys.argv) > 2 else NATIVE.get(SYSTEM, "vulkan")
allowed = ("vulkan", "opengl") if SYSTEM == "Linux" else (NATIVE.get(SYSTEM),)
if rhi not in allowed:
    sys.exit(f"usage: render_test.py <ImageViewer> [{'|'.join(filter(None, allowed))}] (on {SYSTEM})")
work = os.environ.get("RENDER_TEST_DIR") or tempfile.mkdtemp(prefix="imageviewer-render-")
os.makedirs(work, exist_ok=True)

def srgb_decode(v):
    v = np.asarray(v, dtype=np.float64)
    return np.where(v <= 0.04045, v / 12.92, ((v + 0.055) / 1.055) ** 2.4)


def bt2390_knee_nits(source_nits, target_nits):
    """ITU-R BT.2390 §5.4.1 with zero black levels: KS = 1.5 * maxLum - 0.5 (PQ domain)."""
    max_lum = hdr.nits_to_pq(target_nits) / hdr.nits_to_pq(source_nits)
    return float(hdr.pq_to_nits(max(1.5 * max_lum - 0.5, 0.0) * hdr.nits_to_pq(source_nits)))


# The PQ output is checked back in linear BT.709 (SMPTE ST 2084 and BT.2087: hdr_corpus.py).
def pq_decode_bt709(e):
    return hdr.pq_to_nits(e) @ np.linalg.inv(hdr.BT709_TO_BT2020).T


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
corpus16 = corpus.astype(np.float16).astype(np.float64)
# First row of each hue whose components are all positive: (1, 0.2, 0.05) and (0.05, 0.1, 1).
HUE_ROWS = [4 * i for i, h in enumerate(hues) if min(h) > 0 and max(h) > min(h)]
content_peak = corpus.max()  # brightest component, working units (36.0 is exact in FP16)
content_luminance = (corpus @ np.array([0.2126, 0.7152, 0.0722])).max()

# (name, harness arguments, scale per working unit, peak and nits per output unit, decoder)
linear = lambda v: v
CASES = [
    ("sdr", ["--output", "sdr"], 1.0, 1.0, hdr.REFERENCE_WHITE_NITS, srgb_decode, True),
    ("sdr-clip", ["--output", "sdr", "--no-tonemap"], 1.0, 1.0, hdr.REFERENCE_WHITE_NITS, srgb_decode, False),
    ("sdr-exposure", ["--output", "sdr", "--exposure", "-6"], 1.0, 1.0, hdr.REFERENCE_WHITE_NITS, srgb_decode, True),
    ("edr", ["--output", "edr", "--peak", "4"], 1.0, 4.0, hdr.REFERENCE_WHITE_NITS, linear, True),
    ("scrgb", ["--output", "scrgb", "--white", "240", "--peak", "600"], 3.0, 7.5, 80.0, linear, True),
    ("pq", ["--output", "pq", "--white", "203", "--peak", "1000"], 203.0, 1000.0, 1.0, pq_decode_bt709, True),
    ("pq-clip", ["--output", "pq", "--peak", "1000", "--no-tonemap"], 203.0, 1000.0, 1.0, pq_decode_bt709, False),
]

env = dict(os.environ)
server = None
if SYSTEM == "Linux":  # the Vulkan and OpenGL loaders need an X display
    import xvfb
    server, display = xvfb.start()
    env.update(DISPLAY=display, QT_QPA_PLATFORM="xcb", IMAGEVIEWER_RHI=rhi, LC_ALL="C.UTF-8")
else:
    env.update(QT_QPA_PLATFORM="offscreen")
expected_backend = BACKEND_NAMES[rhi]
failures = []
formats = subprocess.run([exe, "--formats"], env=dict(env, QT_QPA_PLATFORM="offscreen"), capture_output=True,
                         text=True, encoding="utf-8", errors="replace", timeout=120).stdout
svg_renderer = re.search(r"^ *svg \| .* \| yes \| ", formats, re.MULTILINE) is not None
try:
    for name, args, scale, peak, nits_per_unit, decode, tone_map in CASES:
        pfm = os.path.join(work, f"{rhi}-{name}.pfm")
        run = subprocess.run([exe, "--render", corpus_path, "--pfm", pfm, *args], env=env, capture_output=True,
                             text=True, encoding="utf-8", errors="replace", timeout=120)
        print(f"--- {rhi} {name}\n{run.stdout.strip()}")
        if run.returncode == GPU_UNAVAILABLE:
            print(f"GPU unavailable: {run.stderr.strip()[-500:]}")
            sys.exit(GPU_UNAVAILABLE)
        if run.returncode != 0:
            failures.append(f"{name}: harness exit {run.returncode} {run.stderr.strip()[-500:]}")
            continue
        backend = re.search(r"^backend:\s+(\S+)", run.stdout, re.MULTILINE)
        if not backend or backend.group(1) != expected_backend:
            failures.append(f"{name}: rendered with {backend.group(1) if backend else 'an unknown backend'}, "
                            f"expected {expected_backend}")
            continue

        raw = read_pfm(pfm)
        out = decode(raw)  # linear BT.709, output units
        exposure = 2.0 ** float(args[args.index("--exposure") + 1]) if "--exposure" in args else 1.0
        k = exposure * scale
        source = levels16 * k                    # neutral row as decoded (FP16), output units
        neutral = out[0]                         # first row is neutral: R = G = B
        source_peak = content_peak * k
        tone_mapped = tone_map and content_luminance * k > peak  # same activation rule as Renderer::stageFor
        problems = []
        if decode is pq_decode_bt709:
            # Judged on the PQ signal itself: the BT.709 -> BT.2020 rows sum to 1, so grey stays R = G = B.
            # In nits, the GPU error the harness accepts (D3D11 WARP: 9.2e-6 in code value, 1 % of a
            # 10-bit step) is amplified by the PQ curve and the inverse matrix to 0.3 nits at 1000 nits.
            spread = np.ptp(raw[0], axis=1).max()
            if spread > PQ_NEUTRAL_TOLERANCE:
                problems.append(f"neutral input is not neutral on output: PQ channels differ by {spread:.3g}")
        elif np.ptp(neutral, axis=1).max() > 1e-4 * max(1.0, peak):
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
            # D-15 scales all components by one ratio: R:G:B must be the input's (FP16) ratios.
            hue_err = 0.0
            for row in HUE_ROWS:
                ref = int(np.argmax(corpus16[row, 0]))
                ratio_in = corpus16[row] / corpus16[row][:, ref:ref + 1]
                ratio_out = out[row] / out[row][:, ref:ref + 1]
                hue_err = max(hue_err, float(np.abs(ratio_out / ratio_in - 1).max()))
            if hue_err > 1e-3:
                problems.append(f"hue not preserved: R:G:B ratios off by up to {hue_err:.3g} (relative)")
            print(f"    knee {knee * nits_per_unit:.1f} nits, identity below it: max error {err.max():.2g}, "
                  f"hue ratio error {hue_err:.2g}")
        else:
            expected = np.minimum(source, peak)
            err = np.abs(y - expected) / np.maximum(1.0, expected)
            if err.max() > 2e-4:
                problems.append(f"not identity/clip: max error {err.max():.3g}")
            print(f"    identity/clip: max error {err.max():.2g}")
        failures += [f"{name}: {p}" for p in problems]

    # Wide gamut at the peak (F3): Display P3 red is R = 1.2246 in linear BT.709 but within
    # BT.2020, so an extended-range output whose peak is SDR white (EDR without headroom,
    # e.g. an external monitor on macOS) must keep it; it was clipped per BT.709 channel to 1.
    p3 = os.path.join(os.path.dirname(os.path.abspath(__file__)), "data", "p3red16.png")
    run = subprocess.run([exe, "--render", p3, "--output", "edr", "--peak", "1"], env=env, capture_output=True,
                         text=True, encoding="utf-8", errors="replace", timeout=120)
    pixel = re.search(r"^pixel\[0,0\]:\s+(\S+)\s+(\S+)\s+(\S+)", run.stdout, re.MULTILINE)
    print(f"--- {rhi} wide gamut at the peak\n{run.stdout.strip()}")
    if run.returncode != 0 or not pixel:
        failures.append(f"wide gamut: harness exit {run.returncode} {run.stderr.strip()[-300:]}")
    elif abs(float(pixel.group(1)) - 1.2246) > 2e-3:
        failures.append(f"wide gamut: P3 red R = {pixel.group(1)} at an SDR peak, expected 1.2246 (not clipped)")

    # PQ and HLG files (H1, H2; D-65). The error of a pixel is its largest channel error over its
    # brightest channel (a hue's zero channel is judged against the light around it), with a
    # floor of 0.05 nits near black, where FP16 storage (RGBA16F) is coarser than 0.1 %.
    data_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "data")
    pngs = hdr.write_pngs(work)
    hdr_files = [(pngs["pq"], "pq", 16), (pngs["hlg"], "hlg", 16)] + \
        [(os.path.join(data_dir, name), kind, bits) for name, kind, bits in hdr.DATA_FILES]
    for path, kind, bits in hdr_files:
        name = os.path.basename(path)
        info = subprocess.run([exe, "--info", path], env=dict(env, QT_QPA_PLATFORM="offscreen"), capture_output=True,
                              text=True, encoding="utf-8", errors="replace", timeout=120).stdout
        depth = re.search(r"^source:\s+\d+ ch, (\d+) bits", info, re.MULTILINE)
        colour = re.search(r"^colour:\s+(.*)$", info, re.MULTILINE)
        wanted = "PQ" if kind == "pq" else "HLG"
        if not depth or not colour or wanted not in colour.group(1) or int(depth.group(1)) < bits:
            seen = f"{colour.group(1) if colour else '?'}, {depth.group(1) if depth else '?'} bits"
            if os.environ.get("CI"):
                failures.append(f"{name}: read as {seen}, not {wanted} at {bits} bits")
            else:
                print(f"--- {name}: skipped, this build reads it as {seen}")
            continue
        reference = hdr.reference_nits(kind, hdr.codes(bits), bits)  # BT.2020 linear, nits
        brightness = np.maximum(reference.max(axis=-1, keepdims=True), 0.05)
        for case, args, to_nits, limit in (
                ("H1 decoded light", ["--output", "edr", "--peak", "100", "--no-tonemap"],
                 lambda raw: raw @ hdr.BT709_TO_BT2020.T * hdr.REFERENCE_WHITE_NITS, 1e-3),
                ("H2 on a 10,000-nit PQ output", ["--output", "pq", "--white", "203", "--peak", "10000"],
                 hdr.pq_to_nits, 1e-3)):
            pfm = os.path.join(work, f"{rhi}-{name}-{case[:2]}.pfm")
            run = subprocess.run([exe, "--render", path, "--pfm", pfm, *args], env=env, capture_output=True,
                                 text=True, encoding="utf-8", errors="replace", timeout=120)
            if run.returncode != 0:
                failures.append(f"{name} {case}: harness exit {run.returncode} {run.stderr.strip()[-300:]}")
                continue
            nits = to_nits(read_pfm(pfm))
            error = (np.abs(nits - reference) / brightness).max(axis=-1)
            row, level = np.unravel_index(np.argmax(error), error.shape)
            print(f"--- {rhi} {name} {case}: max error {error.max():.2g} at level {level} of row {row} "
                  f"({reference[row, level].round(3)} nits, got {nits[row, level].round(3)})")
            if error.max() > limit:
                failures.append(f"{name} {case}: error {error.max():.3g} above {limit} at level {level} of row {row}")

    # The SVG cases need the renderer (scripts/build-svg.sh): a build without it skips them, CI
    # ($CI set) fails instead.
    if not svg_renderer:
        if os.environ.get("CI"):
            failures.append("SVG: no SVG renderer in this build")
        else:
            print("--- SVG: skipped, no SVG renderer in this build (scripts/build-svg.sh)")
    else:
        # SVG (D-71, D-90): nothing outside the document is read, as browsers show an SVG used as an image.
        # Qt's renderer opened a file an <image> names (beside the SVG, at any path, or on Windows at
        # \\server\share, where the system presents the user's credentials) and an xml-stylesheet's
        # style sheet; resvg's default resolver reads such files too, which our shim refuses. Every case
        # draws red over the whole 8 x 8 canvas if it reads the red PNG or the red style sheet written
        # here; black is what is left (nothing over the black ground).
        # The images an SVG embeds still show, through the renderer's own decoders (D-90; OpenImageIO's
        # under Qt, D-68): orange JPEG, GIF and WebP test files, a blue PNG, also inside an SVG inside the
        # SVG, and in an svgz file. An svgz that inflates past the limit is refused rather than allocated.
        def png_bytes(rgb):
            rows = b"".join(b"\0" + bytes(rgb) * 8 for _ in range(8))
            chunk = lambda kind, body: struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))
            return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 8, 8, 8, 2, 0, 0, 0))
                    + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))

        def data_url(kind, body):
            return f"data:{kind};base64,{base64.b64encode(body).decode()}"

        svg_dir = os.path.join(work, "svg")
        os.makedirs(svg_dir, exist_ok=True)
        red_png = os.path.join(svg_dir, "red.png")
        with open(red_png, "wb") as f:
            f.write(png_bytes((255, 0, 0)))
        red_css = os.path.join(svg_dir, "red.css")
        with open(red_css, "w", encoding="utf-8") as f:
            f.write("rect { fill: red; }\n")
        red_url = "file:///" + red_png.replace("\\", "/").lstrip("/")
        svg_open = ('<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" '
                    'width="8" height="8">')
        # preserveAspectRatio="none": the 5x3 GIF and 6x4 WebP fill the 8x8 box, so pixel (0,0) is theirs.
        # SVG's default (xMidYMid meet, as Chromium draws it) would letterbox them; Qt's renderer stretched
        # them regardless, which these cases relied on before D-90.
        image = lambda href: f'{svg_open}<image xlink:href="{href}" width="8" height="8" preserveAspectRatio="none"/></svg>'
        data_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "data")
        embedded = lambda name, kind: image(data_url(kind, open(os.path.join(data_dir, name), "rb").read()))
        BLACK, BLUE, ORANGE = (0.0, 0.0, 0.0), (0.0, 0.0, 1.0), (1.0, 0.502, 0.0)
        svg_cases = [
            ("a file beside it", image("red.png"), BLACK),
            ("a file by its path", image(red_png.replace("\\", "/")), BLACK),
            ("a file URL", image(red_url), BLACK),
            ("a style sheet", f'<?xml-stylesheet type="text/css" href="{red_css.replace(chr(92), "/")}"?>\n'
                              f'{svg_open}<rect width="8" height="8"/></svg>', BLACK),
            ("an entity", f'<!DOCTYPE svg [<!ENTITY p "{red_png.replace(chr(92), "/")}">]>\n{image("&p;")}', BLACK),
            ("an ATTLIST default", f'<!DOCTYPE svg [<!ATTLIST image xlink:href CDATA "{red_png.replace(chr(92), "/")}">]>\n'
                                   f'{svg_open}<image width="8" height="8"/></svg>', BLACK),
            ("an SVG inside it", image(data_url("image/svg+xml", image(red_png.replace("\\", "/")).encode())), BLACK),
            ("an embedded PNG", image(data_url("image/png", png_bytes((0, 0, 255)))), BLUE),
            ("an embedded JPEG", embedded("orange.jpg", "image/jpeg"), ORANGE),
            ("an embedded GIF", embedded("palette.gif", "image/gif"), ORANGE),
            ("an embedded WebP", embedded("lossless.webp", "image/webp"), ORANGE),
            ("a PNG in an SVG inside it", image(data_url("image/svg+xml", image(data_url("image/png", png_bytes((0, 0, 255)))).encode())), BLUE),
        ]
        for n, (case, text, expected) in enumerate(svg_cases + [("svgz", None, BLUE)]):
            path = os.path.join(svg_dir, f"case{n}.svg" if text is not None else "case.svgz")
            if text is None:
                with gzip.open(path, "wb") as f:
                    f.write(svg_cases[7][1].encode())
            else:
                with open(path, "w", encoding="utf-8") as f:
                    f.write(text)
            run = subprocess.run([exe, "--render", path, "--output", "sdr"], env=env, capture_output=True, text=True,
                                 encoding="utf-8", errors="replace", timeout=120)
            pixel = re.search(r"^pixel\[0,0\]:\s+(\S+)\s+(\S+)\s+(\S+)", run.stdout, re.MULTILINE)
            got = tuple(float(v) for v in pixel.groups()) if pixel else None
            print(f"--- {rhi} SVG, {case}: {got}")
            if run.returncode != 0 or got is None:
                failures.append(f"SVG, {case}: harness exit {run.returncode} {run.stdout.strip()[-200:]} {run.stderr.strip()[-200:]}")
            elif max(abs(g - e) for g, e in zip(got, expected)) > 0.02:
                failures.append(f"SVG, {case}: pixel {got}, expected {expected}")
        bomb = os.path.join(svg_dir, "bomb.svgz")
        with gzip.open(bomb, "wb", compresslevel=9) as f:
            f.write(svg_open.encode())
            for _ in range(272):  # 272 MiB of spaces: past the 256 MiB an SVG may inflate to
                f.write(b" " * (1 << 20))
            f.write(b"</svg>")
        run = subprocess.run([exe, "--render", bomb, "--output", "sdr"], env=env, capture_output=True, text=True,
                             encoding="utf-8", errors="replace", timeout=120)
        print(f"--- {rhi} SVG, an svgz inflating past the limit: exit {run.returncode}")
        if run.returncode != 1 or "Cannot decode" not in run.stdout:
            failures.append(f"SVG bomb: exit {run.returncode}, expected 1 with 'Cannot decode': {run.stdout.strip()[-200:]}")

        # SVG features (D-78 (6), D-90) against Chromium's rendering: the viewer's GPU result (sRGB over
        # black, translucency composited in linear light, D-21) and Chromium's pixels composited the same
        # way. Measured at D-90: at most 4.7 levels on average (the turbulence) and 2.3 % of the pixels
        # more than 16 levels apart (the rotated pattern's edges).
        srgb_encode = lambda v: np.where(np.clip(v, 0, 1) <= 0.0031308, np.clip(v, 0, 1) * 12.92,
                                         1.055 * np.clip(v, 0, 1) ** (1 / 2.4) - 0.055)

        def over_black(rgba):  # 8-bit straight RGBA -> sRGB over black, in linear light
            rgba = np.asarray(rgba, dtype=np.float64) / 255
            return srgb_encode(srgb_decode(rgba[..., :3]) * rgba[..., 3:])

        def write_svg(name, body):
            path = os.path.join(svg_dir, name.replace(" ", "-") + ".svg")
            with open(path, "w", encoding="utf-8") as f:
                f.write(feature_open + body + "</svg>\n")
            return path

        def render_svg(path):  # the GPU result, or None (a failure recorded)
            pfm = os.path.splitext(os.path.join(svg_dir, os.path.basename(path)))[0] + ".pfm"
            run = subprocess.run([exe, "--render", path, "--output", "sdr", "--pfm", pfm], env=env, capture_output=True,
                                 text=True, encoding="utf-8", errors="replace", timeout=180)
            if run.returncode != 0:
                failures.append(f"SVG {os.path.basename(path)}: harness exit {run.returncode} {run.stdout.strip()[-200:]} "
                                f"{run.stderr.strip()[-200:]}")
                return None
            return read_pfm(pfm)

        def against(name, ours, reference, mean_limit, share_limit):
            levels = np.abs(ours - reference).max(axis=2) * 255
            mean, share = levels.mean(), 100 * (levels > 16).mean()
            print(f"--- {rhi} SVG {name} against Chromium: {mean:.2f} levels on average, {share:.2f} % beyond 16")
            if mean > mean_limit or share > share_limit:
                failures.append(f"SVG {name}: {mean:.2f} levels on average, {share:.2f} % beyond 16, "
                                f"against Chromium (limits {mean_limit}, {share_limit} %)")

        feature_open = ('<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" '
                        'width="128" height="128" viewBox="0 0 128 128">')
        features = [  # in the order of svg-cases.chromium.png
            ("clipPath", '<defs><clipPath id="c"><circle cx="64" cy="64" r="40"/></clipPath></defs>'
                         '<rect width="128" height="128" fill="#f00" clip-path="url(#c)"/>'),
            ("feDropShadow", '<defs><filter id="f" x="-50%" y="-50%" width="200%" height="200%">'
                             '<feDropShadow dx="8" dy="8" stdDeviation="4" flood-color="#000" flood-opacity="0.7"/></filter></defs>'
                             '<rect width="128" height="128" fill="#fff"/><rect x="24" y="24" width="64" height="64" fill="#3a6" filter="url(#f)"/>'),
            ("CSS selectors", '<style>#a{fill:#26c} .b{fill:#e90}</style><rect id="a" width="64" height="128"/>'
                              '<rect class="b" x="64" width="64" height="128"/>'),
            ("mask", '<defs><linearGradient id="g"><stop offset="0" stop-color="#fff"/><stop offset="1" stop-color="#000"/></linearGradient>'
                     '<mask id="m"><rect width="128" height="128" fill="url(#g)"/></mask></defs>'
                     '<rect width="128" height="128" fill="#c3c" mask="url(#m)"/>'),
            ("feTurbulence", '<defs><filter id="t"><feTurbulence type="fractalNoise" baseFrequency="0.05" numOctaves="3" seed="2"/>'
                             '<feColorMatrix type="saturate" values="0"/></filter></defs><rect width="128" height="128" filter="url(#t)"/>'),
            ("mix-blend-mode", '<rect width="128" height="128" fill="#08f"/>'
                               '<circle cx="64" cy="64" r="48" fill="#f80" style="mix-blend-mode:multiply"/>'),
            ("reflected gradient", '<defs><linearGradient id="r" x1="0" x2="0.25" spreadMethod="reflect"><stop offset="0" stop-color="#f00"/>'
                                   '<stop offset="1" stop-color="#00f"/></linearGradient></defs><rect width="128" height="128" fill="url(#r)"/>'),
            ("rotated pattern", '<defs><pattern id="p" width="16" height="16" patternUnits="userSpaceOnUse" patternTransform="rotate(30)">'
                                '<rect width="8" height="16" fill="#0f0"/></pattern></defs><rect width="128" height="128" fill="url(#p)"/>'),
            ("nested svg", '<svg x="32" y="32" width="64" height="64" viewBox="0 0 10 10"><rect width="10" height="10" fill="#f60"/>'
                           '<circle cx="5" cy="5" r="3" fill="#06f"/></svg>'),
            ("Illustrator classes", '<defs><style>.cls-1{fill:#1d71b8;}.cls-2{fill:none;stroke:#e30613;stroke-miterlimit:10;'
                                    'stroke-width:6px;}.cls-3{fill:url(#lg);}</style><linearGradient id="lg" x1="0" y1="0" x2="128" '
                                    'y2="128" gradientUnits="userSpaceOnUse"><stop offset="0" stop-color="#fc0"/><stop offset="1" '
                                    'stop-color="#0c9"/></linearGradient></defs><rect class="cls-3" width="128" height="128"/>'
                                    '<circle class="cls-1" cx="64" cy="64" r="36"/><path class="cls-2" d="M16,112L112,16"/>'),
        ]
        strip = np.asarray(Image.open(os.path.join(data_dir, "svg-cases.chromium.png")).convert("RGBA"))
        for n, (name, body) in enumerate(features):
            ours = render_svg(write_svg(name, body))
            if ours is not None:
                against(name, ours, over_black(strip[:, n * 128:(n + 1) * 128]), 6.0, 4.0)
        # Text, in whichever fonts the system has: white glyphs on nothing, so some bright pixels where
        # the text runs and none in the corners it does not reach.
        texts = [
            ("textPath", '<defs><path id="p" d="M10,90 Q64,10 118,90"/></defs><text font-family="sans-serif" font-size="18" '
                         'fill="#fff"><textPath href="#p" xlink:href="#p">Curved text here</textPath></text>',
             (slice(20, 95), slice(5, 123)), (slice(100, 128), slice(0, 128))),
            ("text", '<text x="8" y="80" font-family="sans-serif" font-size="40" font-weight="bold" fill="#fff">Hello</text>',
             (slice(45, 85), slice(8, 128)), (slice(0, 30), slice(0, 128))),
        ]
        for name, body, inside, outside in texts:
            ours = render_svg(write_svg(name, body))
            if ours is None:
                continue
            lit, dark = ours[inside].max(), ours[outside].max()
            print(f"--- {rhi} SVG {name}: brightest {lit:.2f} where the text runs, {dark:.2f} where it does not")
            if lit < 0.9 or dark > 0.01:
                failures.append(f"SVG {name}: brightest {lit:.2f} where the text runs, {dark:.2f} where it does not")
        # The owner's artwork (17 filters, 13 masks; D-79, D-90) at its own size, reduced to 256 px as
        # its reference was: by 4 x 4 boxes in linear light. Measured at D-90: 1.5 levels on average.
        full = render_svg(os.path.join(data_dir, "IV_claude_v5_mix_v1.svg"))
        if full is not None:
            small = srgb_encode(srgb_decode(full).reshape(256, 4, 256, 4, 3).mean(axis=(1, 3)))
            reference = over_black(np.asarray(Image.open(os.path.join(data_dir, "IV_claude_v5_mix_v1.chromium.png")).convert("RGBA")))
            against("owner's artwork", small, reference, 4.0, 2.0)

    # The information panels (D-49): their background darkens SDR white as in SDR in every
    # output, their labels keep 4.5:1 against it, and against their outline over the brightest
    # content the output shows (the harness measures and judges; exit 3 is a failed check).
    for name, args in (("sdr", []), ("edr", ["--peak", "4"]), ("scrgb", ["--white", "240", "--peak", "600"]),
                       ("pq", ["--white", "203", "--peak", "1000"])):
        run = subprocess.run([exe, "--panel-check", "--output", name, *args], env=env, capture_output=True,
                             text=True, encoding="utf-8", errors="replace", timeout=120)
        print(f"--- {rhi} panels {name}\n{run.stdout.strip()}")
        if run.returncode != 0:
            failures.append(f"panels {name}: exit {run.returncode} {run.stdout.strip()[-300:]} {run.stderr.strip()[-300:]}")
finally:
    if server:
        xvfb.stop(server)

if failures:
    print("FAIL\n  " + "\n  ".join(failures))
    sys.exit(1)
print(f"{rhi}: all {len(CASES)} output-stage cases, the wide-gamut case, the PQ/HLG files"
      + (" and the SVG cases pass" if svg_renderer else " pass (SVG skipped)"))
