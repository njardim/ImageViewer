#!/usr/bin/env bash
# Headless smoke test of the decode + colour pipeline: `imageViewer --info`.
# `--info` runs on a QCoreApplication (no platform plugin), so this also works on
# packaged binaries on machines without a display.
# Usage: tests/smoke.sh <path-to-imageViewer-executable>
set -euo pipefail

exe="$1"
data="$(cd "$(dirname "$0")/data" && pwd)"
failures=0

# The formats this build reads, by decoder: a record in every CI log, and a check that the
# option works on the packaged binary.
"$exe" --formats | sed 's/^/     /'

check() { # file, extended regex expected in the --info output, description
    local out
    out="$("$exe" --info "$data/$1" 2>/dev/null || true)"
    if grep -Eq "$2" <<<"$out"; then
        echo "ok   $1: $3"
    else
        echo "FAIL $1: $3 (expected /$2/)"
        echo "$out" | sed 's/^/     /'
        failures=$((failures + 1))
    fi
}

# Display P3 red must survive as an out-of-gamut scRGB value (unbounded ICC transform).
check p3red16.png 'colour: +ICC: Display P3' "embedded ICC profile recognised"
check p3red16.png 'pixel\[0,0\]: +1\.22[0-9]* -0\.04[0-9]* -0\.019' "P3 red -> scRGB (1.225, -0.042, -0.020)"
# Linear EXR above SDR white: the file stores premultiplied (4,4,4,0.5), i.e. colour 8 at alpha 0.5.
check hdr4.exr 'max: +8x SDR white' "EXR straight colour 8.0 preserved (HDR headroom)"
check hdr4.exr 'pixel\[0,0\]: +8 8 8 a=0\.5' "EXR premultiplied alpha read as such"
# PNG alpha is straight: every pixel (200,100,50,128) -> sRGB-decoded colour, alpha 128/255.
check alpha8.png 'pixel\[0,0\]: +0\.577[0-9]* 0\.127[0-9]* 0\.031[0-9]* a=0\.50' "PNG straight alpha kept straight"
# EXIF orientation 6 rotates 300x200 into 200x300 exactly once.
check orient6.jpg 'size: +200x300' "EXIF orientation 6 applied once"

# EXIF shooting data for the information panel (D-34); the model holds a tab and a BEL
# character, which must not reach the panel. Made with Pillow 12.3.
check camera.jpg 'camera: +Cristallumnis \| Cristallumnis Test Camera X1 \| 50mm F1\.8 \| 1/250 s \| f/2\.8 \| ISO 400 \| 50 mm \| 2026-10-07T12:34:56$' "EXIF camera data read and cleaned"

check rows2.pfm 'colour: +linear' "PFM read as linear (OIIO labels it Rec709)"
check rows2.pfm 'pixel\[0,0\]: +0\.25 0\.5 1 a=1' "PFM rows stored bottom to top are flipped"

# Codecs enabled in the vcpkg build (vcpkg.json). Pixel (0,0) is sRGB (255,128,0) -> linear (1, 0.2158, 0).
# Made with Pillow 12.3 (WebP, GIF, JPEG 2000, AVIF) and oiiotool 2.4 (TIFF).
check lossless.webp 'size: +6x4' "WebP dimensions"
check lossless.webp 'pixel\[0,0\]: +1 0\.215[89][0-9]* 0 a=1' "WebP lossless pixel exact"
check palette.gif 'size: +5x3' "GIF dimensions"
check palette.gif 'pixel\[0,0\]: +1 0\.215[89][0-9]* 0 a=1' "GIF palette colour exact"
check lossless.jp2 'size: +7x5' "JPEG 2000 dimensions"
check lossless.jp2 'pixel\[0,0\]: +1 0\.215[89][0-9]* 0 a=1' "JPEG 2000 lossless pixel exact"
check flat.avif 'size: +8x6' "AVIF dimensions"
check flat.avif 'pixel\[0,0\]: +(1|0\.9[89][0-9]*) 0\.2[12][0-9]* ' "AVIF pixel within 1-2 codes (lossy, 4:4:4)"
# 16-bit TIFF: G = 33024/65535 lies between two 8-bit codes, so only a 16-bit decode gives 0.2177.
check rgb16.tif 'source: +3 ch, 16 bits' "TIFF read at 16 bits"
check rgb16.tif 'pixel\[0,0\]: +1 0\.217[5-8][0-9]* 0 a=1' "TIFF 16-bit value not quantised to 8 bits"

# Decoders used directly (D-38). JPEG XL made with cjxl 0.7 from 16-bit PNGs: pixel (0,0) is
# sRGB (16384, 32768, 49152)/65535, a value only a 16-bit decode reproduces; pq.jxl is
# BT.2020/PQ at code 0.7518 everywhere, i.e. 1000 nits. The WebP carries the Display P3 ICC
# profile of p3red16.png (cwebp -metadata icc), which OpenImageIO's reader ignored.
check gradient.jxl 'codec: +libjxl' "JPEG XL read by libjxl"
check gradient.jxl 'source: +3 ch, 16 bits' "JPEG XL at 16 bits"
check gradient.jxl 'pixel\[0,0\]: +0\.0508[0-9]* 0\.21398[0-9]* 0\.52246[0-9]* a=1' "JPEG XL 16-bit sRGB value exact"
check pq.jxl 'colour: +JPEG XL: BT\.2020, PQ.*\[HDR\]' "JPEG XL colour encoding (BT.2020, PQ)"
check pq.jxl 'max: +4\.92[0-9]*x SDR white \(99[89]\.[0-9]* nits\)' "JPEG XL PQ 1000 nits"
check p3red.webp 'colour: +ICC: Display P3' "WebP ICC profile read"
check p3red.webp 'pixel\[0,0\]: +1\.22[0-9]* -0\.04[0-9]* -0\.019' "WebP P3 red -> scRGB"

# The registry (D-38): every format that names a test file is in this build and decodes it.
while IFS='|' read -r id name decoder available caps extensions test; do
    id="$(echo "$id" | xargs)"; available="$(echo "$available" | xargs)"; test="$(echo "$test" | xargs)"
    [ -z "$test" ] && continue
    if [ "$available" != yes ]; then
        echo "FAIL $id: has a test file ($test) but is not available in this build"
        failures=$((failures + 1))
    elif "$exe" --info "$data/$test" >/dev/null 2>&1; then
        echo "ok   $id: $test decodes"
    else
        echo "FAIL $id: $test does not decode"
        failures=$((failures + 1))
    fi
done < <("$exe" --formats 2>/dev/null)

if [ "$failures" -ne 0 ]; then
    echo "$failures check(s) failed"
    exit 1
fi
echo "all checks passed"
