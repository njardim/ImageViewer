#!/usr/bin/env bash
# Development setup for Linux and Claude Code cloud sessions (Ubuntu 24.04).
#
# The cloud proxy blocks download.qt.io and GitHub release assets, so Qt 6.12 is
# built from the GitHub source mirror into /opt/qt6 (qtbase, qtshadertools, the
# Linguist tools of qttools, qtsvg and qtimageformats, ~25 min on 4 cores). A Qt of
# another version already in /opt/qt6 is replaced. Its image plugins are the packages'
# (D-67, D-68): SVG, ICNS, ICO and WBMP. Imaging libraries come from apt; their
# versions are older than production (vcpkg, see vcpkg.json) but sufficient to
# compile and test.
#
# Afterwards:  cmake --preset linux-system && cmake --build --preset linux-system
#              tests/smoke.sh build/linux-system/ImageViewer
set -euo pipefail

# The version CI uses (packaging/qt-version.txt, D-62).
QT_TAG="${QT_TAG:-v$(tr -d '[:space:]' < "$(dirname "$0")/../packaging/qt-version.txt")}"
PREFIX="${PREFIX:-/opt/qt6}"
WORK="${WORK:-$HOME/qt-src}"

sudo=""
[ "$(id -u)" -ne 0 ] && sudo="sudo"

$sudo apt-get update -q
$sudo apt-get install -y -q build-essential cmake ninja-build perl python3 pkg-config \
    libgl-dev libegl-dev libvulkan-dev mesa-vulkan-drivers libgl1-mesa-dri libfontconfig1-dev libfreetype-dev \
    libxkbcommon-dev libxkbcommon-x11-dev libx11-dev libx11-xcb-dev libxcb1-dev libxcb-cursor-dev \
    libxcb-icccm4-dev libxcb-image0-dev libxcb-keysyms1-dev libxcb-randr0-dev libxcb-render-util0-dev \
    libxcb-shape0-dev libxcb-shm0-dev libxcb-sync-dev libxcb-xfixes0-dev libxcb-xinerama0-dev libxcb-xkb-dev \
    libxcb-glx0-dev libxcb-util-dev libxcb-render0-dev libxcb-xinput-dev libxrender-dev libxi-dev libxext-dev \
    libxfixes-dev libsm-dev libice-dev xvfb xauth x11-apps xdotool imagemagick python3-numpy python3-pil \
    libopenimageio-dev openimageio-tools libtiff-dev liblcms2-dev libjxl-dev libwebp-dev libheif-dev

# Ubuntu's OpenImageIO CMake package lists /usr/include/opencv4 even without OpenCV installed.
$sudo mkdir -p /usr/include/opencv4

installed="$(sed -n 's/^set(PACKAGE_VERSION "\([0-9.]*\)")$/\1/p' "$PREFIX/lib/cmake/Qt6/Qt6ConfigVersionImpl.cmake" 2>/dev/null || true)"
plugins="$PREFIX/plugins/imageformats"
if [ "v$installed" != "$QT_TAG" ] || [ ! -x "$PREFIX/bin/qsb" ] || [ ! -x "$PREFIX/bin/lupdate" ] \
    || [ ! -e "$plugins/libqsvg.so" ] || [ -e "$plugins/libqjpeg.so" ]; then
    mkdir -p "$WORK" && cd "$WORK"
    for module in qtbase qtshadertools qttools qtsvg qtimageformats; do
        # A source tree of another version is cloned again.
        if [ -d "$module" ] && [ "$(git -C "$module" describe --tags --exact-match 2>/dev/null)" != "$QT_TAG" ]; then
            rm -rf "${WORK:?}/$module" "${WORK:?}/build-$module"
        fi
        [ -d "$module" ] || git clone --depth 1 --branch "$QT_TAG" "https://github.com/qt/$module.git"
    done
    [ -z "$installed" ] || $sudo rm -rf "${PREFIX:?}" # never mix two versions in one prefix
    cmake -S qtbase -B build-qtbase -G Ninja -DCMAKE_INSTALL_PREFIX="$PREFIX" -DCMAKE_BUILD_TYPE=Release \
        -DQT_BUILD_EXAMPLES=OFF -DQT_BUILD_TESTS=OFF -DFEATURE_sql=OFF -DFEATURE_dbus=OFF -DFEATURE_openssl=OFF \
        -DFEATURE_printsupport=OFF -DFEATURE_icu=OFF -DFEATURE_vulkan=ON -DFEATURE_xcb=ON -DFEATURE_xcb_xlib=ON
    cmake --build build-qtbase --parallel "$(nproc)"
    $sudo cmake --install build-qtbase
    cmake -S qtshadertools -B build-qtshadertools -G Ninja -DCMAKE_PREFIX_PATH="$PREFIX" \
        -DCMAKE_INSTALL_PREFIX="$PREFIX" -DCMAKE_BUILD_TYPE=Release -DQT_BUILD_EXAMPLES=OFF -DQT_BUILD_TESTS=OFF
    cmake --build build-qtshadertools --parallel "$(nproc)"
    $sudo cmake --install build-qtshadertools
    # lupdate and lrelease, which the build needs for the UI translations (D-27).
    cmake -S qttools -B build-qttools -G Ninja -DCMAKE_PREFIX_PATH="$PREFIX" -DCMAKE_INSTALL_PREFIX="$PREFIX" \
        -DCMAKE_BUILD_TYPE=Release -DQT_BUILD_EXAMPLES=OFF -DQT_BUILD_TESTS=OFF -DFEATURE_linguist=ON \
        -DFEATURE_designer=OFF -DFEATURE_assistant=OFF -DFEATURE_qdoc=OFF -DFEATURE_clang=OFF -DFEATURE_pixeltool=OFF \
        -DFEATURE_qtdiag=OFF -DFEATURE_qtplugininfo=OFF -DFEATURE_distancefieldgenerator=OFF -DFEATURE_qev=OFF \
        -DFEATURE_qtattributionsscanner=OFF
    cmake --build build-qttools --parallel "$(nproc)"
    $sudo cmake --install build-qttools
    # SVG, and ICNS and WBMP (qtimageformats). Then every other image plugin (qtbase's JPEG and GIF,
    # qtimageformats' TGA, TIFF, WebP...) goes, with its CMake package, which would refuse a
    # plugin file that is missing.
    cmake -S qtsvg -B build-qtsvg -G Ninja -DCMAKE_PREFIX_PATH="$PREFIX" -DCMAKE_INSTALL_PREFIX="$PREFIX" \
        -DCMAKE_BUILD_TYPE=Release -DQT_BUILD_EXAMPLES=OFF -DQT_BUILD_TESTS=OFF
    cmake --build build-qtsvg --parallel "$(nproc)"
    $sudo cmake --install build-qtsvg
    cmake -S qtimageformats -B build-qtimageformats -G Ninja -DCMAKE_PREFIX_PATH="$PREFIX" -DCMAKE_INSTALL_PREFIX="$PREFIX" \
        -DCMAKE_BUILD_TYPE=Release -DQT_BUILD_EXAMPLES=OFF -DQT_BUILD_TESTS=OFF
    cmake --build build-qtimageformats --parallel "$(nproc)"
    $sudo cmake --install build-qtimageformats
    for f in "$plugins"/*; do
        name="$(basename "$f")"; name="${name#libq}"; name="${name%.so}"
        case "$name" in svg|icns|ico|wbmp) continue ;; esac
        $sudo rm -f "$f"
        $sudo find "$PREFIX/lib/cmake" -iname "Qt6Q${name}Plugin*.cmake" -delete
    done
fi
echo "Qt installed in $PREFIX"
