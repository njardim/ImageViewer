#!/usr/bin/env bash
# Development setup for Linux and Claude Code cloud sessions (Ubuntu 24.04).
#
# The cloud proxy blocks download.qt.io and GitHub release assets, so Qt 6.11 is
# built from the GitHub source mirror into /opt/qt6 (qtbase + qtshadertools,
# ~15 min on 4 cores). Imaging libraries come from apt; their versions are older
# than production (vcpkg, see vcpkg.json) but sufficient to compile and test.
#
# Afterwards:  cmake --preset linux-system && cmake --build --preset linux-system
#              tests/smoke.sh build/linux-system/imageViewer
set -euo pipefail

QT_TAG="${QT_TAG:-v6.11.2}"
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
    libxcb-glx0-dev libxrender-dev libxi-dev xvfb xauth \
    libopenimageio-dev openimageio-tools liblcms2-dev

# Ubuntu's OpenImageIO CMake package lists /usr/include/opencv4 even without OpenCV installed.
$sudo mkdir -p /usr/include/opencv4

if [ ! -x "$PREFIX/bin/qsb" ]; then
    mkdir -p "$WORK" && cd "$WORK"
    [ -d qtbase ] || git clone --depth 1 --branch "$QT_TAG" https://github.com/qt/qtbase.git
    [ -d qtshadertools ] || git clone --depth 1 --branch "$QT_TAG" https://github.com/qt/qtshadertools.git
    cmake -S qtbase -B build-qtbase -G Ninja -DCMAKE_INSTALL_PREFIX="$PREFIX" -DCMAKE_BUILD_TYPE=Release \
        -DQT_BUILD_EXAMPLES=OFF -DQT_BUILD_TESTS=OFF -DFEATURE_sql=OFF -DFEATURE_dbus=OFF -DFEATURE_openssl=OFF \
        -DFEATURE_printsupport=OFF -DFEATURE_icu=OFF -DFEATURE_vulkan=ON -DFEATURE_xcb=ON -DFEATURE_xcb_xlib=ON
    cmake --build build-qtbase --parallel "$(nproc)"
    $sudo cmake --install build-qtbase
    cmake -S qtshadertools -B build-qsb -G Ninja -DCMAKE_PREFIX_PATH="$PREFIX" -DCMAKE_INSTALL_PREFIX="$PREFIX" \
        -DCMAKE_BUILD_TYPE=Release -DQT_BUILD_EXAMPLES=OFF -DQT_BUILD_TESTS=OFF
    cmake --build build-qsb --parallel "$(nproc)"
    $sudo cmake --install build-qsb
fi
echo "Qt installed in $PREFIX"
