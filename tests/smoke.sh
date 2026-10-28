#!/usr/bin/env bash
# Headless smoke test of the decode + colour pipeline: `ImageViewer --info`.
# `--info` runs on a QCoreApplication (no platform plugin), so this also works on
# packaged binaries on machines without a display.
# Usage: tests/smoke.sh <path-to-ImageViewer-executable>
set -euo pipefail
# Windows: a GUI program without a console sends its log to the debugger, not to stderr.
export QT_FORCE_STDERR_LOGGING=1

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
# The peak that drives tone mapping is what can reach the screen: 8 at alpha 0.5 over a
# background of at most SDR white is 0.5·8 + 0.5 = 4.5 (it was the straight 8 before 0.3).
check hdr4.exr 'max: +4\.5x SDR white' "EXR peak as seen over the background"
check hdr4.exr 'pixel\[0,0\]: +8 8 8 a=0\.5' "EXR premultiplied alpha read as such"
# PNG alpha is straight: every pixel (200,100,50,128) -> sRGB-decoded colour, alpha 128/255.
check alpha8.png 'pixel\[0,0\]: +0\.577[0-9]* 0\.127[0-9]* 0\.031[0-9]* a=0\.50' "PNG straight alpha kept straight"
# The same pixel from BMP and SGI (Pillow 12.3): their readers keep straight alpha without
# saying so, which must not be read as premultiplied (it was, up to 0.3-dev: 1 0.572 0.126).
for f in alpha8.bmp alpha8.sgi; do
    check "$f" 'pixel\[0,0\]: +0\.577[0-9]* 0\.127[0-9]* 0\.031[0-9]* a=0\.50' "straight alpha kept straight"
done
# A palette GIF whose colour 0 is transparent (OpenImageIO gives the alpha channel index as 4
# of 4; it was dropped up to 0.3-dev).
check transparent.gif 'pixel\[0,0\]: +0 0 0 a=0$' "GIF transparency kept"
# EXIF orientation 6 rotates 300x200 into 200x300 exactly once.
check orient6.jpg 'size: +200x300' "EXIF orientation 6 applied once"

# EXIF shooting data for the information panel (D-34); the model holds a tab and a BEL
# character, which must not reach the panel. Made with Pillow 12.3.
check camera.jpg 'camera: +Cristallumnis \| Cristallumnis Test Camera X1 \| 50mm F1\.8 \| 1/250 s \| f/2\.8 \| ISO 400 \| 50 mm \| 2026-10-07T12:34:56$' "EXIF camera data read and cleaned"
# camera.jpg recompressed losslessly by cjxl 0.7: the EXIF is a Brotli-compressed box (brob).
check camera.jxl 'camera: +Cristallumnis \| Cristallumnis Test Camera X1 \| 50mm F1\.8 \| 1/250 s \| f/2\.8 \| ISO 400 \| 50 mm \| 2026-10-07T12:34:56$' "JPEG XL EXIF box (compressed)"

check rows2.pfm 'colour: +linear' "PFM read as linear (OIIO labels it Rec709)"
check rows2.pfm 'pixel\[0,0\]: +0\.25 0\.5 1 a=1' "PFM rows stored bottom to top are flipped"
# PNG gAMA without sRGB: the exact exponent, (128/255)^(1/0.45) and (128/255)^0.5.
check gamma-2.22.png 'pixel\[0,0\]: +0\.216[0-9]* 0\.216[0-9]* 0\.216[0-9]* a=1' "PNG gAMA 45000: exact exponent 2.222"
check gamma-0.5.png 'pixel\[0,0\]: +0\.708[0-9]* 0\.708[0-9]* 0\.708[0-9]* a=1' "PNG gAMA 200000: exponent 0.5"
check orange.ppm 'pixel\[0,0\]: +1 0\.215[89][0-9]* 0 a=1' "8-bit PPM read as sRGB, not Rec709 (D-54)"

# Codecs enabled in the vcpkg build (vcpkg.json). Pixel (0,0) is sRGB (255,128,0) -> linear (1, 0.2158, 0).
# Made with Pillow 12.3 (WebP, GIF, JPEG 2000, AVIF) and oiiotool 2.4 (TIFF).
check lossless.webp 'size: +6x4' "WebP dimensions"
check lossless.webp 'pixel\[0,0\]: +1 0\.215[89][0-9]* 0 a=1' "WebP lossless pixel exact"
check palette.gif 'size: +5x3' "GIF dimensions"
check palette.gif 'pixel\[0,0\]: +1 0\.215[89][0-9]* 0 a=1' "GIF palette colour exact"
check lossless.jp2 'size: +7x5' "JPEG 2000 dimensions"
check lossless.jp2 'pixel\[0,0\]: +1 0\.215[89][0-9]* 0 a=1' "JPEG 2000 lossless pixel exact"
# HTJ2K (JPEG 2000 Part 15), lossless, made with OpenJPH 0.32.0's ojph_compress: precincts of 2x2
# give code-blocks one row high, the case of our OpenJPH patch (D-73). OpenImageIO decodes it with
# OpenJPH in the CI builds, with OpenJPEG where it has no OpenJPH.
check htj2k.jph 'size: +7x5' "HTJ2K dimensions"
check htj2k.jph 'pixel\[0,0\]: +1 0\.215[89][0-9]* 0 a=1' "HTJ2K lossless pixel exact"
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

# Animation (E6): three 8x6 frames, red, green and blue, shown 100, 200 and 300 ms, looping
# forever (Pillow 12.3; the JPEG XL with cjxl 0.7 from the GIF). blend.png composites a
# half-transparent green over red (APNG blend OVER): sRGB (127, 128, 0) -> linear (0.212, 0.216, 0).
for f in anim.gif anim.webp anim.jxl anim.png; do
    check "$f" 'frames: +3, loops 0, ms 100 200 300$' "animation: 3 frames and their durations"
    check "$f" 'frame px: +1\.000 0\.000 0\.000 \| 0\.000 1\.000 0\.000 \| 0\.000 0\.000 1\.000$' "animation: every frame's colour"
done
# APNG (0.5 review): a first frame smaller than the canvas after a default image left out of the
# animation (legal), and a 70 KB XMP chunk before acTL, past the 64 KiB detection reads. A header
# of 4294967295 x 4294967295 (D-81): its pixel count overflowed 64 bits and passed every limit, and
# the first frame, at row 100000, was written far outside a 4-byte canvas (a crash).
apngs="$(mktemp -d)"
python3 - "$data/anim.png" "$apngs" <<'PY'
import struct, sys, zlib
def chunk(t, d): return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
def raw(w, h, rgba): return zlib.compress(b''.join(b'\x00' + bytes(rgba) * w for _ in range(h)))
seq = [0]
def frame(w, h, x, y, rgba):
    fc = chunk(b'fcTL', struct.pack('>IIIIIHHBB', seq[0], w, h, x, y, 1, 10, 0, 0))
    fd = chunk(b'fdAT', struct.pack('>I', seq[0] + 1) + raw(w, h, rgba)); seq[0] += 2
    return fc + fd
png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 4, 4, 8, 6, 0, 0, 0)) + chunk(b'acTL', struct.pack('>II', 2, 0))
png += chunk(b'IDAT', raw(4, 4, (0, 0, 255, 255))) + frame(2, 2, 1, 1, (255, 0, 0, 255)) + frame(4, 4, 0, 0, (0, 255, 0, 255))
open(sys.argv[2] + '/subrect.png', 'wb').write(png + chunk(b'IEND', b''))
src = open(sys.argv[1], 'rb').read()
open(sys.argv[2] + '/xmp70k.png', 'wb').write(src[:33] + chunk(b'iTXt', b'XML:com.adobe.xmp\0\0\0\0\0' + b' ' * 70000) + src[33:])
seq[0] = 0
png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 0xffffffff, 0xffffffff, 8, 6, 0, 0, 0)) + chunk(b'acTL', struct.pack('>II', 2, 0))
png += chunk(b'IDAT', raw(1, 1, (0, 0, 255, 255))) + frame(1, 1, 0, 100000, (255, 0, 0, 255)) + frame(1, 1, 0, 0, (0, 255, 0, 255))
open(sys.argv[2] + '/overflow.png', 'wb').write(png + chunk(b'IEND', b''))
PY
while read -r name count; do
    if "$exe" --info "$apngs/$name" 2>/dev/null | grep -Eq "^frames: +$count,"; then
        echo "ok   $name: played as an APNG ($count frames)"
    else
        echo "FAIL $name: not played as an APNG"
        failures=$((failures + 1))
    fi
done <<'CASES'
subrect.png 2
xmp70k.png 3
CASES
code=0
"$exe" --info "$apngs/overflow.png" >"$apngs/overflow.out" 2>/dev/null || code=$?
if [ "$code" = 1 ] && grep -q 'invalid dimensions (4294967295' "$apngs/overflow.out"; then
    echo "ok   overflow.png: an APNG of 4294967295 × 4294967295 is refused"
else
    echo "FAIL overflow.png: exit $code, expected 1 with \"invalid dimensions\""
    failures=$((failures + 1))
fi
rm -rf "$apngs"
# AVIF image sequences (libheif 1.23 track API; CI requires it, local builds against an older
# distribution libheif skip these): the same three frames, Pillow 12.3 at quality 100, which
# is still lossy (blue comes back as 0.99).
if "$exe" --formats 2>/dev/null | grep -Eq '^avifs \| .* \| yes \|'; then
    check anim.avif 'codec: +libheif \(sequence\)' "AVIF sequence read by libheif"
    check anim.avif 'frames: +3, loops 0, ms 100 200 300$' "AVIF sequence: frames and durations"
    check anim.avif 'frame px: +(1\.000|0\.99[0-9]) 0\.00[0-9] 0\.00[0-9] \| 0\.00[0-9] (1\.000|0\.99[0-9]) 0\.00[0-9] \| 0\.00[0-9] 0\.00[0-9] (1\.000|0\.99[0-9])$' "AVIF sequence: every frame's colour"
    check anim-narrow.avif 'frame px: +1\.000 0\.2[12][0-9] 0\.000 \| 0\.000 1\.000 0\.000$' "AVIF sequence in narrow range expanded once"
else
    echo "skip anim.avif: this build reads no AVIF sequences (libheif older than 1.23)"
fi
# Narrow-range AVIF (nclx full_range_flag 0): libheif hands over full-range RGB, which must
# not be expanded a second time (OpenImageIO 3 passes the flag on as CICP).
# Colour stored premultiplied, (128, 64, 0) at alpha 128 (libheif, lossless): shown once divided.
check premultiplied.avif 'pixel\[0,0\]: +1 0\.21[0-9]* 0 a=0\.50' "AVIF premultiplied alpha divided once"
# The colour of AVIF stills comes from libheif: OpenImageIO reads no ICC profile from them, and
# no nclx box when an ICC profile is there too (both written by libavif).
check p3icc.avif 'colour: +ICC: Display P3' "AVIF ICC profile read"
check p3icc.avif 'pixel\[0,0\]: +1\.22[0-9]* -0\.04[0-9]* -0\.019' "AVIF P3 red -> scRGB (1.225, -0.042, -0.019)"
check pq-icc.avif 'colour: +CICP 9/16/6/1 ' "AVIF with nclx and ICC: CICP first (D-22)"
check narrow.avif 'pixel\[0,0\]: +(1|0\.9[89][0-9]*) 0\.2[12][0-9]* ' "AVIF narrow range expanded once"
check blend.png 'frames: +3, loops 2, ms 50 60 70$' "APNG loop count"
# GIF loops as browsers play them: no NETSCAPE block once, a count N (repeats) N + 1 times.
check noloop.gif 'frames: +3, loops 1, ' "GIF without a loop count plays once"
check loop2.gif 'frames: +3, loops 3, ' "GIF loop count 2 plays 3 times"
check blend.png 'frame px: +1\.000 0\.000 0\.000 \| 0\.212 0\.216 0\.000 \| 0\.000 0\.000 1\.000$' "APNG blend over the previous frame"

# The decode worker (D-38): GraphicsMagick in its own process, for the long tail. CI requires
# it; local builds without GraphicsMagick skip these. Test files: tests/longtail_data.py.
# sRGB (255, 128, 0) -> linear (1, 0.2158, 0); gray 128 -> 0.2158; layers.xcf composites a
# blue layer at 50 % over orange: sRGB (127, 63, 128) -> (0.212, 0.0497, 0.2158).
if "$exe" --formats 2>/dev/null | grep -Eq '^pcx \| .* \| GraphicsMagick \| yes \|'; then
    for f in gm.pcx gm.dcx gm.pict gm.miff gm.ras gm.viff gm.mat rgb.tim rgb.pix; do
        check "$f" 'codec: +GraphicsMagick [A-Z]+ \(worker\)' "read by GraphicsMagick in the decode worker"
        check "$f" 'pixel\[0,0\]: +1 0\.215[89][0-9]* 0 a=1' "colour exact"
    done
    check layers.xcf 'pixel\[0,0\]: +0\.212[0-9]* 0\.049[0-9]* 0\.215[89][0-9]* a=1' "GIMP layers composited (50 % blue over orange)"
    check gray16.dcm 'source: +3 ch, 16 bits' "DICOM read at 16 bits"
    check gray16.dcm 'pixel\[0,0\]: +0\.214[0-9]* 0\.214[0-9]* 0\.214[0-9]* a=1' "DICOM 16-bit value 32768"
    check gray.cut 'pixel\[0,0\]: +0\.215[89][0-9]* 0\.215[89][0-9]* 0\.215[89][0-9]* a=1' "CUT gray level (no palette file)"
    check gm.vicar 'pixel\[0,0\]: +0\.309[0-9]* 0\.309[0-9]* 0\.309[0-9]* a=1' "VICAR gray level"
    check bw.mac 'size: +576x720' "MacPaint page size"
    check bw.mac 'pixel\[0,0\]: +0 0 0 a=1' "MacPaint black pixel"
    check gm.otb 'pixel\[0,0\]: +0 0 0 a=1' "OTA bitmap black pixel"
    check gm.wpg 'size: +6x4' "WordPerfect graphic dimensions"
else
    echo "skip decode worker: this build has no GraphicsMagick"
fi

# HEIC (D-39): the system's decoder where there is one (ImageIO on macOS; WIC on Windows with
# Microsoft's HEIF and HEVC extensions), else OpenImageIO if its libheif has an HEVC decoder
# (a distribution's), else a note on how to get one. orange.heic: heif-enc 1.17, quality 95,
# 4:2:0, from sRGB (255,128,0) -> linear (1, 0.2158, 0), within 0.02 (lossy).
heic_out="$("$exe" --info "$data/orange.heic" 2>&1 || true)"
heic_available="$("$exe" --formats 2>/dev/null | awk -F' *[|] *' '$1 == "heic" { print $4 }')"
if grep -q 'HEVC' <<<"$heic_out" && [ "$heic_available" = system-missing ]; then
    echo "ok   orange.heic: no HEVC decoder on this system, the note says how to get one"
elif grep -q 'HEVC' <<<"$heic_out" && { [ "$(uname -s | cut -c1-5)" = MINGW ] || [ "$(uname -s | cut -c1-4)" = MSYS ]; }; then
    # WIC lists the HEIF extension, which decodes nothing without the HEVC one.
    echo "skip orange.heic: Windows has the HEIF extension but no HEVC decoder; the note says how to get one"
elif ! awk '/^pixel\[0,0\]:/ { d = ($2 - 1)^2 + ($3 - 0.2158)^2 + $4^2; found = 1 } END { exit !(found && d <= 0.0004) }' <<<"$heic_out"; then
    echo "FAIL orange.heic: expected pixel (1, 0.2158, 0) within 0.02, or the HEVC note"
    echo "$heic_out" | sed 's/^/     /'
    failures=$((failures + 1))
elif [ "$heic_available" = yes ] && ! grep -Eq 'codec: +(ImageIO \(macOS\)|WIC \(Windows\))' <<<"$heic_out"; then
    echo "FAIL orange.heic: the system's decoder is available but did not decode it"
    echo "$heic_out" | sed 's/^/     /'
    failures=$((failures + 1))
else
    echo "ok   orange.heic: decoded by $(sed -n 's/^codec: *//p' <<<"$heic_out"), pixel within 0.02"
fi

# Formats read by OpenImageIO and Qt, test files written by oiiotool, GraphicsMagick and
# Pillow or built by tests/longtail_data.py: sRGB (255, 128, 0) -> linear (1, 0.2158, 0).
for f in orange.bmp orange.tga orange.ico orange.cur orange.sgi orange.dds orange.iff orange.rla orange.pic \
         orange.fits gm.xpm; do
    if "$exe" --formats 2>/dev/null | grep -q " | yes | .* | $f\$"; then # the registry check below reports the others
        check "$f" 'pixel\[0,0\]: +1 0\.215[89][0-9]* 0 a=1' "colour exact"
    fi
done
# A cursor holding a PNG (Pillow 12.3, 8x8 orange): Qt hands the PNG to our plugin, which reads it
# with OpenImageIO, never to Qt's own copy of libpng (D-68, D-71).
if "$exe" --formats 2>/dev/null | grep -q "^ *cur | .* | yes | "; then
    said="$(QT_LOGGING_RULES='qt.gui.imageio.reader.debug=true' "$exe" --info "$data/png.cur" 2>&1 || true)"
    if grep -q 'OiioImagePlugin.* can read the format "png"' <<<"$said" && grep -Eq 'pixel\[0,0\]: +1 0\.215[89][0-9]* 0 a=1' <<<"$said"; then
        echo "ok   png.cur: the PNG inside read by OpenImageIO (our Qt plugin), colour exact"
    else
        echo "FAIL png.cur: the PNG inside was not read by OiioImagePlugin, or not exactly"
        grep -E 'imageio|pixel|error' <<<"$said" | sed 's/^/     /'
        failures=$((failures + 1))
    fi
fi
# A FITS volume: every slice is read (a one-slice buffer overflowed), the first one shown.
if "$exe" --formats 2>/dev/null | grep -q "^ *fits | .* | yes | "; then
    check volume.fits 'size: +6x4' "FITS volume (5 slices) read whole, one slice shown"
    check rows.fits 'pixel\[0,0\]: +0\.215[89][0-9]* 0\.215[89][0-9]* 0\.215[89][0-9]* a=1' "FITS rows bottom first, none shifted"
fi
# Softimage PIC content under the name of another format: our PIC reader, never OpenImageIO's,
# which crashed on the truncated ones (D-45, 0.4 review).
check pic-named.svg 'codec: +ImageViewer \(Softimage PIC\)' "PIC content named .svg read by our PIC reader"
for f in pic-truncated.svg pic-truncated.dcm; do
    code=0
    "$exe" --info "$data/$f" >/dev/null 2>&1 || code=$?
    if [ "$code" = 1 ]; then
        echo "ok   $f: a truncated PIC under another name fails cleanly"
    else
        echo "FAIL $f: exit $code, expected 1 (OpenImageIO's PIC reader crashes on it)"
        failures=$((failures + 1))
    fi
done
# SVG (D-90): the renderer behind imageviewer_svg.h (resvg) in the decode worker, without a display.
if "$exe" --formats 2>/dev/null | grep -q "^ *svg | .* | yes | "; then
    check orange.svg 'codec: +resvg [0-9.]+ \(worker\)' "SVG drawn by resvg in the decode worker"
    check orange.svg 'pixel\[0,0\]: +1 0\.215[89][0-9]* 0 a=1' "SVG colour sRGB (255, 128, 0)"
    check orange.svgz 'pixel\[0,0\]: +1 0\.215[89][0-9]* 0 a=1' "svgz inflated before the renderer"
    check text.svg 'pixel\[0,0\]: +1 1 1 a=1' "SVG text drawn with the system's fonts"
    check IV_claude_v5_mix_v1.svg 'size: +1024x1024' "the owner's SVG (17 filters, 13 masks) at its own size"
    # A worker given no request refuses it (exit 2), and a render past the worker's 30 s ends there:
    # the viewer says why and goes on (D-38).
    code=0
    "$exe" --decode-worker SVG </dev/null >/dev/null 2>&1 || code=$?
    if [ "$code" = 2 ]; then echo "ok   SVG worker: no request, refused"; else echo "FAIL SVG worker: exit $code without a request, expected 2"; failures=$((failures + 1)); fi
    slow="$(mktemp -d)/slow.svg"
    printf '<svg xmlns="http://www.w3.org/2000/svg" width="8000" height="8000"><filter id="n" x="0" y="0" width="1" height="1"><feTurbulence baseFrequency="0.9" numOctaves="10"/><feGaussianBlur stdDeviation="40"/></filter><rect width="8000" height="8000" filter="url(#n)"/><rect width="8000" height="8000" filter="url(#n)" opacity="0.5"/></svg>\n' >"$slow"
    started=$(date +%s)
    said="$("$exe" --info "$slow" 2>/dev/null || true)"
    took=$(($(date +%s) - started))
    rm -rf "$(dirname "$slow")"
    if grep -q 'took longer than 30 s and was stopped' <<<"$said" && [ "$took" -lt 45 ]; then
        echo "ok   SVG worker: a render past 30 s stopped (${took} s), the viewer says why"
    else
        echo "FAIL SVG worker: a render past 30 s ($took s): $said"
        failures=$((failures + 1))
    fi
    # A filter region 25 times its canvas (resvg allocates such a layer whole): the renderer's memory
    # limit ends the worker at once, and the viewer says so (it took 2 GiB and 30 s before, D-90).
    bomb="$(mktemp -d)/region.svg"
    printf '<svg xmlns="http://www.w3.org/2000/svg" width="3000" height="3000"><filter id="b" filterUnits="userSpaceOnUse" x="-6000" y="-6000" width="15000" height="15000"><feGaussianBlur stdDeviation="2"/></filter><rect width="3000" height="3000" fill="#f80" filter="url(#b)"/></svg>\n' >"$bomb"
    started=$(date +%s)
    said="$("$exe" --info "$bomb" 2>/dev/null || true)"
    took=$(($(date +%s) - started))
    rm -rf "$(dirname "$bomb")"
    if grep -q 'Not enough memory to decode the image' <<<"$said" && [ "$took" -lt 20 ]; then
        echo "ok   SVG worker: a filter region past the renderer's memory limit refused (${took} s)"
    else
        echo "FAIL SVG worker: a filter region past the memory limit ($took s): $said"
        failures=$((failures + 1))
    fi
    # What starting a worker costs on this machine (its libraries load): a record, not a check.
    # Milliseconds from Perl, which every runner has (macOS's date knows no %N).
    now_ms() { perl -MTime::HiRes=time -e 'printf "%d\n", time * 1000'; }
    started=$(now_ms)
    for _ in 1 2 3 4 5 6 7 8 9 10; do "$exe" --decode-worker SVG </dev/null >/dev/null 2>&1 || true; done
    echo "info SVG worker: a start costs $((($(now_ms) - started) / 10)) ms here"
elif [ -n "${CI:-}" ]; then
    echo "FAIL svg: no SVG renderer in this build"
    failures=$((failures + 1))
else
    echo "skip svg: no SVG renderer in this build (scripts/build-svg.sh)"
fi
check orange.hdr 'pixel\[0,0\]: +1 0\.5 0 a=1' "Radiance HDR read as linear values"
check gray.zfile 'pixel\[0,0\]: +0\.50[12][0-9]* 0\.50[12][0-9]* 0\.50[12][0-9]* a=1' "zfile depth value"

# The registry (D-38): every format that names a test file is in this build and decodes it
# (HEIC, which depends on the system, above). CI builds with every decoder
# (IMAGEVIEWER_REQUIRE_ALL_DECODERS), so a missing one fails there; a local build with fewer
# libraries (no libheif 1.23, no GraphicsMagick, Qt without some plugins) skips it.
while IFS='|' read -r id name decoder available caps extensions test; do
    id="$(echo "$id" | xargs)"; decoder="$(echo "$decoder" | xargs)"; available="$(echo "$available" | xargs)"; test="$(echo "$test" | xargs)"
    [ -z "$test" ] || [ "$id" = heic ] && continue
    if [ "$available" != yes ] && [ -z "${CI:-}" ]; then
        echo "skip $id: not in this build ($test)"
    elif [ "$available" != yes ]; then
        echo "FAIL $id: has a test file ($test) but is not available in this build"
        failures=$((failures + 1))
    elif "$exe" --info "$data/$test" >/dev/null 2>&1; then
        echo "ok   $id: $test decodes"
    else
        echo "FAIL $id: $test does not decode"
        failures=$((failures + 1))
        if [ "$decoder" = GraphicsMagick ] && [ -z "${worker_diagnosed:-}" ]; then
            # Once: why the decode worker refused, from the viewer's log and from the worker itself.
            worker_diagnosed=1
            # (Each command may fail: this script stops at the first unchecked failure.)
            { QT_LOGGING_RULES='imageviewer.*=true' "$exe" --info "$data/$test" 2>&1 >/dev/null || true; } | sed 's/^/     log: /'
            said="$(mktemp)"
            code=0
            "$exe" --decode-worker "$(echo "$id" | tr a-z A-Z)" 1000000 <"$data/$test" >/dev/null 2>"$said" || code=$?
            echo "     worker run directly: exit code $code"
            sed 's/^/     worker: /' "$said"
            # The same with GraphicsMagick's own event log (configuration, coders, exceptions).
            code=0
            MAGICK_DEBUG=configure,coder,exception "$exe" --decode-worker "$(echo "$id" | tr a-z A-Z)" 1000000 \
                <"$data/$test" >/dev/null 2>"$said" || code=$?
            echo "     worker with MAGICK_DEBUG: exit code $code"
            grep -v -e '^ *Tried: ' "$said" | head -n 80 | sed 's/^/     gm: /' || true
            rm -f "$said"
        fi
    fi
done < <("$exe" --formats 2>/dev/null)

# Open With (D-53). Linux: the desktop entries of a private XDG tree, so the result is exact:
# the default from mimeapps.list first, a hidden entry, a missing TryExec, another MIME type and a
# removed association left out, the Exec line's escapes, quoting and field codes undone and expanded.
# macOS: Preview is among the applications for a PNG. Elsewhere it runs and lists without failing.
case "$(uname -s)" in
Linux)
    xdg="$(mktemp -d)"
    apps="$xdg/data/applications"
    mkdir -p "$apps/sub" "$xdg/config" "$xdg/dirs"
    # The string escapes (\s, \\) are undone before the quoting: "\\\\" in quotes is one backslash.
    cat >"$apps/alpha.desktop" <<'ENTRY'
[Desktop Entry]
Type=Application
Name=Alpha Viewer
Exec=/usr/bin/alpha --open "a\\\\b \\$1" x\sy %f
MimeType=image/png;
ENTRY
    printf '[Desktop Entry]\nType=Application\nName=Beta\nName[pt]=Beta PT\nExec="/opt/beta app/beta" --title=%%c %%U\nMimeType=image/jpeg;image/png;\n' >"$apps/beta.desktop"
    printf '[Desktop Entry]\nType=Application\nName=Gone\nHidden=true\nExec=gone %%f\nMimeType=image/png;\n' >"$apps/gone.desktop"
    printf '[Desktop Entry]\nType=Application\nName=Missing\nTryExec=/nonexistent/missing\nExec=missing %%f\nMimeType=image/png;\n' >"$apps/missing.desktop"
    printf '[Desktop Entry]\nType=Application\nName=Jpeg Only\nExec=jpeg %%f\nMimeType=image/jpeg;\n' >"$apps/jpeg.desktop"
    printf '[Desktop Entry]\nType=Application\nName=Removed\nExec=/bin/echo\nMimeType=image/png;\n' >"$apps/sub/removed.desktop"
    # A field code inside quotes (forbidden by the specification) would reach a shell unquoted: no command.
    printf '[Desktop Entry]\nType=Application\nName=Shell\nExec=sh -c "echo %%f"\nMimeType=image/png;\n' >"$apps/shell.desktop"
    # Nor inside a word given to a shell ("echo\ %f" is the script "echo <file>"): no command.
    cat >"$apps/shellword.desktop" <<'ENTRY'
[Desktop Entry]
Type=Application
Name=Shell Word
Exec=sh -c echo\\ %f
MimeType=image/png;
ENTRY
    # Nor as the shell's script: the argument of -c (the name is the code) or its first operand
    # (the content is the code). After the script it is data: "$1" of the script.
    printf '[Desktop Entry]\nType=Application\nName=Shell Script\nExec=sh -c %%f\nMimeType=image/png;\n' >"$apps/shellscript.desktop"
    printf '[Desktop Entry]\nType=Application\nName=Shell File\nExec=/bin/sh %%f\nMimeType=image/png;\n' >"$apps/shellfile.desktop"
    cat >"$apps/shelldata.desktop" <<'ENTRY'
[Desktop Entry]
Type=Application
Name=Shell Data
Exec=sh -c "viewer \\"\\$1\\"" sh %f
MimeType=image/png;
ENTRY
    # The shell's options before its script: those that take an argument ("-o errexit"), those
    # that start with '+', and "-" ending them; "--rcfile" runs its argument. A script before the
    # file is a command: the file is its $1.
    printf '[Desktop Entry]\nType=Application\nName=Shell Option\nExec=sh -o errexit %%f\nMimeType=image/png;\n' >"$apps/shelloption.desktop"
    printf '[Desktop Entry]\nType=Application\nName=Shell Option Code\nExec=sh -c -o errexit %%f\nMimeType=image/png;\n' >"$apps/shelloptioncode.desktop"
    printf '[Desktop Entry]\nType=Application\nName=Shell Plus\nExec=sh +e %%f\nMimeType=image/png;\n' >"$apps/shellplus.desktop"
    printf '[Desktop Entry]\nType=Application\nName=Shell Dash\nExec=sh - %%f\nMimeType=image/png;\n' >"$apps/shelldash.desktop"
    printf '[Desktop Entry]\nType=Application\nName=Shell Rcfile\nExec=bash --rcfile %%f\nMimeType=image/png;\n' >"$apps/shellrcfile.desktop"
    printf '[Desktop Entry]\nType=Application\nName=Shell Run\nExec=sh /opt/run.sh %%f\nMimeType=image/png;\n' >"$apps/shellrun.desktop"
    # Every argument holding the file counts, not only the first ("env %u sh %f": the second is the
    # shell's script, D-82); and the program is never the file itself.
    printf '[Desktop Entry]\nType=Application\nName=Shell Second\nExec=env %%u sh %%f\nMimeType=image/png;\n' >"$apps/shellsecond.desktop"
    printf '[Desktop Entry]\nType=Application\nName=Program File\nExec=%%f\nMimeType=image/png;\n' >"$apps/programfile.desktop"
    printf '[Default Applications]\nimage/png=beta.desktop\n[Removed Associations]\nimage/png=sub-removed.desktop\n' >"$xdg/config/mimeapps.list"
    png="$data/alpha8.png"
    url="file://$(cd "$data" && pwd)/alpha8.png"
    file="$(cd "$data" && pwd)/alpha8.png"
    expected="$(printf 'Beta PT\tbeta.desktop\t/opt/beta app/beta|--title=Beta PT|%s\nAlpha Viewer\talpha.desktop\t/usr/bin/alpha|--open|a\\b $1|x|y|%s\nProgram File\tprogramfile.desktop\t\nShell\tshell.desktop\t\nShell Dash\tshelldash.desktop\t\nShell Data\tshelldata.desktop\tsh|-c|viewer "$1"|sh|%s\nShell File\tshellfile.desktop\t\nShell Option\tshelloption.desktop\t\nShell Option Code\tshelloptioncode.desktop\t\nShell Plus\tshellplus.desktop\t\nShell Rcfile\tshellrcfile.desktop\t\nShell Run\tshellrun.desktop\tsh|/opt/run.sh|%s\nShell Script\tshellscript.desktop\t\nShell Second\tshellsecond.desktop\t\nShell Word\tshellword.desktop\t' "$url" "$file" "$file" "$file")"
    listed="$(XDG_DATA_HOME="$xdg/data" XDG_DATA_DIRS="$xdg/dirs" XDG_CONFIG_HOME="$xdg/config" XDG_CONFIG_DIRS="$xdg/dirs" \
              LC_ALL=pt_PT.UTF-8 LANG=pt_PT.UTF-8 "$exe" --open-with "$png" 2>/dev/null || true)"
    # An image named like a shell is not taken for one: "--file=%f" with a file named "sh" stays.
    rm -rf "$apps" "$xdg/config/mimeapps.list" && mkdir -p "$apps"
    printf '[Desktop Entry]\nType=Application\nName=Embedded\nExec=viewer --file=%%f\nMimeType=image/png;\n' >"$apps/embedded.desktop"
    mkdir "$xdg/named" && cp "$png" "$xdg/named/sh"
    named="$(XDG_DATA_HOME="$xdg/data" XDG_DATA_DIRS="$xdg/dirs" XDG_CONFIG_HOME="$xdg/config" XDG_CONFIG_DIRS="$xdg/dirs" \
             "$exe" --open-with "$xdg/named/sh" 2>/dev/null || true)"
    named_expected="$(printf 'Embedded\tembedded.desktop\tviewer|--file=%s' "$xdg/named/sh")"
    rm -rf "$xdg"
    if [ "$listed" = "$expected" ] && [ "$named" = "$named_expected" ]; then
        echo "ok   Open With: desktop entries, default first, Exec escapes, quoting and field codes (never inside quotes, a shell's word, its script or an option's argument, nor the program; a file named sh is no shell)"
    else
        echo "FAIL Open With: got"; printf '%s\n' "$listed" "$named" | sed 's/^/     /'
        echo "     expected"; printf '%s\n' "$expected" "$named_expected" | sed 's/^/     /'
        failures=$((failures + 1))
    fi
    ;;
Darwin)
    if "$exe" --open-with "$data/alpha8.png" 2>/dev/null | grep -q '^Preview	'; then
        echo "ok   Open With: Preview listed for a PNG"
    else
        echo "FAIL Open With: Preview not listed for a PNG"
        failures=$((failures + 1))
    fi
    ;;
*)
    if "$exe" --open-with "$data/alpha8.png" >/dev/null 2>&1; then
        echo "ok   Open With: the applications for a PNG are listed"
    else
        echo "FAIL Open With: --open-with failed"
        failures=$((failures + 1))
    fi
    ;;
esac

if [ "$failures" -ne 0 ]; then
    echo "$failures check(s) failed"
    exit 1
fi
echo "all checks passed"
