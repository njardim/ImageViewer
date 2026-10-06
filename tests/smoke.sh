#!/usr/bin/env bash
# Headless smoke test of the decode + colour pipeline: `imageViewer --info`.
# Usage: tests/smoke.sh <path-to-imageViewer-executable>
set -euo pipefail

exe="$1"
data="$(cd "$(dirname "$0")/data" && pwd)"
export QT_QPA_PLATFORM=offscreen
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
# Linear EXR above SDR white keeps its value and straight alpha.
check hdr4.exr 'max: +4x SDR white' "EXR value 4.0 preserved (HDR headroom)"
check hdr4.exr 'pixel\[0,0\]: +4 4 4 a=0\.5' "premultiplied alpha round-trips"
# EXIF orientation 6 rotates 300x200 into 200x300 exactly once.
check orient6.jpg 'size: +200x300' "EXIF orientation 6 applied once"

if [ "$failures" -ne 0 ]; then
    echo "$failures check(s) failed"
    exit 1
fi
echo "all checks passed"
