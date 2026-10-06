#!/usr/bin/env bash
# Headless smoke test of the decode + colour pipeline: `imageViewer --info`.
# `--info` runs on a QCoreApplication (no platform plugin), so this also works on
# packaged binaries on machines without a display.
# Usage: tests/smoke.sh <path-to-imageViewer-executable>
set -euo pipefail

exe="$1"
data="$(cd "$(dirname "$0")/data" && pwd)"
failures=0

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

if [ "$failures" -ne 0 ]; then
    echo "$failures check(s) failed"
    exit 1
fi
echo "all checks passed"
