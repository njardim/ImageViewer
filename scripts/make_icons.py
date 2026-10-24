#!/usr/bin/env python3
"""The application's icons for the three systems, from packaging/icons/imageviewer-1024x1024.png,
the owner's artwork and the icons' only source (1024 px, sRGB; D-66, D-78, D-79).

  imageviewer.png   1024 px, the artwork itself, compiled into the executable (:/imageviewer.png):
                    the windows' icon on Windows and Linux, and the About dialog.
  imageviewer.ico   Windows (the executable's resource, packaging/icons/imageviewer.rc): every size
                    Windows asks for at its display scales, 16 to 256 px (256 is the largest it uses).
  imageviewer.icns  macOS (the bundle's CFBundleIconFile): 16 to 1024 px, @1x and @2x, as iconutil
                    writes them. The artwork sits on Apple's icon grid, an 824 px tile centred on
                    1024 px with 100 px of margin, cut to Apple's shape: as large as the other
                    applications' icons in the Dock and Finder, and on macOS 26, which clips every
                    icon to its own shape, opaque up to that shape and never past it (an icon that
                    is not gets a grey plate behind it, shrunk).

Every size is made by Lanczos resampling in linear light, premultiplied, so that downscaled edges and
translucency keep their weight; the PNG entries are stored at zlib's highest compression. Run it
again after replacing the source:
    python3 scripts/make_icons.py
needs: Pillow, numpy
"""
import io
import os
import struct

import numpy as np
from PIL import Image, ImageDraw

HERE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "packaging", "icons")
SOURCE = "imageviewer-1024x1024.png"
# Largest first in both files: the systems pick by size, and a viewer that shows a file's first
# image shows the sharpest.
ICO_SIZES = [256, 128, 96, 80, 72, 64, 60, 48, 40, 36, 32, 30, 24, 20, 16]
# The .icns entries iconutil writes, PNG data in each: (type, pixels).
ICNS_ENTRIES = [(b"ic10", 1024), (b"ic09", 512), (b"ic14", 512), (b"ic08", 256), (b"ic13", 256),
                (b"ic07", 128), (b"ic12", 64), (b"ic11", 32), (b"icp5", 32), (b"icp4", 16)]
# Apple's icon grid, in pixels of a 1024 px icon: the tile and the radius of its continuous corners.
APPLE_CANVAS, APPLE_TILE, APPLE_RADIUS = 1024, 824, 185.4
BLEED = 2  # pixels of artwork past the tile on each side, cut away by the shape
# A continuous corner (the rounded rectangle of iOS 7 and of macOS since Big Sur): three cubic
# Béziers from the top edge to the right edge around a corner at the origin, in radii, y downwards.
CONTINUOUS_CORNER = [
    ((-1.52866483, 0), (-1.08849323, 0), (-0.86840694, 0), (-0.63149399, 0.07491100)),
    ((-0.63149399, 0.07491100), (-0.37282392, 0.16905899), (-0.16905899, 0.37282392), (-0.07491100, 0.63149399)),
    ((-0.07491100, 0.63149399), (0, 0.86840694), (0, 1.08849323), (0, 1.52866483)),
]


def to_linear(c):
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def to_srgb(c):
    return np.where(c <= 0.0031308, c * 12.92, 1.055 * np.clip(c, 0, None) ** (1 / 2.4) - 0.055)


def linear_premultiplied(image):
    a = np.asarray(image.convert("RGBA"), dtype=np.float64) / 255.0
    return np.concatenate([to_linear(a[..., :3]) * a[..., 3:], a[..., 3:]], axis=-1)


def resample(pixels, size):
    """Lanczos for the colour, the area each pixel covers for the alpha: edges stay sharp, and none of
    Lanczos's ringing reaches past the artwork's outline as a faint halo."""
    def planes(kind):
        return np.stack([np.asarray(Image.fromarray(p.astype(np.float32), mode="F").resize((size, size), kind),
                                    dtype=np.float64) for p in np.moveaxis(pixels, -1, 0)], axis=-1)
    sharp, area = planes(Image.LANCZOS), planes(Image.BOX)
    # The colour of a pixel Lanczos leaves almost transparent comes from the area average instead.
    colour = np.where(sharp[..., 3:] > 0.05, sharp, area)
    straight = colour[..., :3] / np.maximum(colour[..., 3:], 1e-6)
    return np.concatenate([straight * area[..., 3:], area[..., 3:]], axis=-1)


def to_image(pixels):
    alpha = np.clip(pixels[..., 3:], 0, 1)
    rgb = to_srgb(np.clip(pixels[..., :3] / np.maximum(alpha, 1e-6), 0, 1))
    return Image.fromarray(np.rint(np.concatenate([rgb, alpha], axis=-1) * 255).astype(np.uint8), "RGBA")


def apple_shape(supersample=8):
    """Apple's icon shape on its 1024 px canvas: the coverage of every pixel, 0 to 1."""
    scale = supersample
    r, low, high = APPLE_RADIUS * scale, (APPLE_CANVAS - APPLE_TILE) / 2 * scale, (APPLE_CANVAS + APPLE_TILE) / 2 * scale
    corner = []
    for p in CONTINUOUS_CORNER:
        for t in np.linspace(0, 1, 64, endpoint=False):
            w = ((1 - t) ** 3, 3 * (1 - t) ** 2 * t, 3 * (1 - t) * t ** 2, t ** 3)
            corner.append((sum(k * q[0] for k, q in zip(w, p)) * r, sum(k * q[1] for k, q in zip(w, p)) * r))
    corner.append((0, CONTINUOUS_CORNER[-1][-1][1] * r))
    outline = ([(high + x, low + y) for x, y in corner] + [(high - y, high + x) for x, y in corner] +
               [(low - x, high - y) for x, y in corner] + [(low + y, low - x) for x, y in corner])
    canvas = Image.new("L", (APPLE_CANVAS * scale,) * 2, 0)
    ImageDraw.Draw(canvas).polygon(outline, fill=255)
    # Pillow fills a pixel whose centre is inside on the left and top edges, but up to half a pixel
    # past the right and bottom ones, and rounds rows and columns a little differently: the shape is
    # symmetric, so the top-left quarter, averaged with its mirror on the diagonal, makes all four.
    half = APPLE_CANVAS * scale // 2
    quarter = np.asarray(canvas, dtype=np.float64)[:half, :half] / 255.0
    quarter = (quarter + quarter.T) / 2
    full = np.block([[quarter, quarter[:, ::-1]], [quarter[::-1, :], quarter[::-1, ::-1]]])
    return full.reshape(APPLE_CANVAS, scale, APPLE_CANVAS, scale).mean(axis=(1, 3))[..., None]


def on_apple_grid(source):
    """The macOS icon at 1024 px, linear and premultiplied: the artwork on Apple's tile, cut to its shape.
    The artwork bleeds two pixels past the tile, as print does past its cut: where its own corners meet
    the straight sides, they leave the edge a fraction of a pixel later than Apple's."""
    side = APPLE_TILE + 2 * BLEED
    offset = (APPLE_CANVAS - side) // 2
    canvas = np.zeros((APPLE_CANVAS, APPLE_CANVAS, 4))
    canvas[offset:offset + side, offset:offset + side] = resample(linear_premultiplied(source), side)
    shape = apple_shape()
    alpha = canvas[..., 3:]
    # Opaque within the shape: macOS 26 draws an icon that is not on a grey plate.
    if (alpha[shape > 0] < 0.99).any():
        raise SystemExit("the artwork does not fill Apple's icon shape")
    straight = canvas[..., :3] / np.maximum(alpha, 1e-6)
    return np.concatenate([straight * shape, shape], axis=-1)


def png_bytes(image):
    data = io.BytesIO()
    image.save(data, "PNG", optimize=True)
    return data.getvalue()


def write_ico(path, images):
    """An ICONDIR with one PNG entry per size (Windows Vista and later read PNG at every size)."""
    header = struct.pack("<HHH", 0, 1, len(images))
    offset = len(header) + 16 * len(images)
    entries, blobs = b"", b""
    for image in images:
        data = png_bytes(image)
        side = image.width if image.width < 256 else 0  # 0 means 256
        entries += struct.pack("<BBBBHHII", side, side, 0, 0, 1, 32, len(data), offset)
        blobs += data
        offset += len(data)
    with open(path, "wb") as f:
        f.write(header + entries + blobs)


def write_icns(path, icon):
    sizes = {size: png_bytes(to_image(icon if size == APPLE_CANVAS else resample(icon, size)))
             for size in {s for _, s in ICNS_ENTRIES}}
    body = b"".join(kind + struct.pack(">I", 8 + len(sizes[size])) + sizes[size] for kind, size in ICNS_ENTRIES)
    with open(path, "wb") as f:
        f.write(b"icns" + struct.pack(">I", 8 + len(body)) + body)


def main():
    source = Image.open(os.path.join(HERE, SOURCE)).convert("RGBA")
    if source.size != (1024, 1024):
        raise SystemExit(f"{SOURCE} must be 1024 x 1024 px")
    artwork = linear_premultiplied(source)
    source.save(os.path.join(HERE, "imageviewer.png"), optimize=True)
    write_ico(os.path.join(HERE, "imageviewer.ico"), [to_image(resample(artwork, s)) for s in ICO_SIZES])
    write_icns(os.path.join(HERE, "imageviewer.icns"), on_apple_grid(source))
    print(f"wrote imageviewer.png, imageviewer.ico and imageviewer.icns in {os.path.normpath(HERE)}")


if __name__ == "__main__":
    main()
