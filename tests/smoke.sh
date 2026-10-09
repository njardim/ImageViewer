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
check orange.hdr 'pixel\[0,0\]: +1 0\.5 0 a=1' "Radiance HDR read as linear values"
check gray.zfile 'pixel\[0,0\]: +0\.50[12][0-9]* 0\.50[12][0-9]* 0\.50[12][0-9]* a=1' "zfile depth value"

# The registry (D-38): every format that names a test file is in this build and decodes it
# (HEIC, which depends on the system, above). CI builds with every decoder
# (IMAGEVIEWER_REQUIRE_ALL_DECODERS), so a missing one fails there; a local build with fewer
# libraries (no libheif 1.23, no GraphicsMagick, Qt without some plugins) skips it.
while IFS='|' read -r id name decoder available caps extensions test; do
    id="$(echo "$id" | xargs)"; decoder="$(echo "$decoder" | xargs)"; available="$(echo "$available" | xargs)"; test="$(echo "$test" | xargs)"
    [ -z "$test" ] || [ "$id" = heic ] && continue
    if [ "$id" = svg ]; then
        if [ "$available" != yes ] && [ -n "${CI:-}" ]; then
            echo "FAIL svg: not in this build (Qt's SVG image plugin missing)"
            failures=$((failures + 1))
        else
            echo "skip svg: decoded only in the graphical interface (Qt lays out SVG text with its font database)"
        fi
        continue
    fi
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
    printf '[Default Applications]\nimage/png=beta.desktop\n[Removed Associations]\nimage/png=sub-removed.desktop\n' >"$xdg/config/mimeapps.list"
    png="$data/alpha8.png"
    url="file://$(cd "$data" && pwd)/alpha8.png"
    expected="$(printf 'Beta PT\tbeta.desktop\t/opt/beta app/beta|--title=Beta PT|%s\nAlpha Viewer\talpha.desktop\t/usr/bin/alpha|--open|a\\b $1|x|y|%s' "$url" "$(cd "$data" && pwd)/alpha8.png")"
    listed="$(XDG_DATA_HOME="$xdg/data" XDG_DATA_DIRS="$xdg/dirs" XDG_CONFIG_HOME="$xdg/config" XDG_CONFIG_DIRS="$xdg/dirs" \
              LC_ALL=pt_PT.UTF-8 LANG=pt_PT.UTF-8 "$exe" --open-with "$png" 2>/dev/null || true)"
    rm -rf "$xdg"
    if [ "$listed" = "$expected" ]; then
        echo "ok   Open With: desktop entries, default first, Exec escapes, quoting and field codes"
    else
        echo "FAIL Open With: got"; printf '%s\n' "$listed" | sed 's/^/     /'
        echo "     expected"; printf '%s\n' "$expected" | sed 's/^/     /'
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
