#!/usr/bin/env bash
# ImageViewer's SVG renderer (D-90) for local builds (Linux, macOS): resvg at the commit the overlay
# port pins, with the port's own crate and build logic (packaging/vcpkg/ports/imageviewer-svg), into
# PREFIX (default /usr/local), where CMake finds it. CI builds the same through vcpkg.
#
# Needs git, CMake and rustup's cargo (the toolchain of the port's rust-toolchain.toml is installed
# on first use).
#
# Afterwards:  cmake --preset linux-system && cmake --build --preset linux-system
set -euo pipefail

port="$(cd "$(dirname "$0")/../packaging/vcpkg/ports/imageviewer-svg" && pwd)"
PREFIX="${PREFIX:-/usr/local}"
WORK="${WORK:-$HOME/imageviewer-svg-src}"

commit="$(sed -n 's/^ *REF \([0-9a-f]\{40\}\) # .*/\1/p' "$port/portfile.cmake")"
[[ "$commit" =~ ^[0-9a-f]{40}$ ]] || { echo "no pinned commit in $port/portfile.cmake" >&2; exit 1; }

# A fresh tree at the pinned commit (fetched by its hash, so the source is the one the port builds).
rm -rf "$WORK"
mkdir -p "$WORK"
git -C "$WORK" init -q resvg
git -C "$WORK/resvg" fetch -q --depth 1 https://github.com/linebender/resvg "$commit"
git -C "$WORK/resvg" checkout -q FETCH_HEAD

sudo=""
[ "$(id -u)" -ne 0 ] && [ ! -w "$PREFIX" ] && sudo="sudo"
mkdir -p "$WORK/stage"
cmake -DSOURCE_DIR="$WORK/resvg" -DBUILD_DIR="$WORK/build" -DPREFIX="$WORK/stage" -P "$port/imageviewer-svg.cmake"
mv "$WORK/stage/share/imageviewer-svg/licences.txt" "$WORK/stage/share/imageviewer-svg/copyright"
$sudo mkdir -p "$PREFIX"
$sudo cp -R "$WORK/stage/." "$PREFIX/"
echo "ImageViewer's SVG renderer installed in $PREFIX ($(sed -n 's/^version = "\(.*\)"/resvg \1/p' "$port/Cargo.toml" | head -1))"
