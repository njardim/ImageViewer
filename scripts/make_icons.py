#!/usr/bin/env python3
"""The application's icons for the three systems, from packaging/icons/imageviewer-source.png, the
owner's artwork (1028 px, sRGB; D-66, D-78).

  imageviewer.ico   Windows (the executable's resource, packaging/icons/imageviewer.rc): every size
                    Windows asks for at its display scales, 16 to 256 px (256 is the largest it uses).
  imageviewer.icns  macOS (the bundle's CFBundleIconFile): 16 to 1024 px, @1x and @2x, as iconutil
                    writes them. The artwork sits on Apple's icon grid, 824 of 1024 px with the rest
                    transparent, so that it is as large as the other applications' icons in the Dock
                    and Finder.
  imageviewer.png   1024 px, compiled into the executable (:/imageviewer.png): the windows' icon on
                    Windows and Linux, and the About dialog, sharp up to four device pixels per point.

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
from PIL import Image

HERE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "packaging", "icons")
# Largest first in both files: the systems pick by size, and a viewer that shows a file's first
# image shows the sharpest.
ICO_SIZES = [256, 128, 96, 80, 72, 64, 60, 48, 40, 36, 32, 30, 24, 20, 16]
# The .icns entries iconutil writes, PNG data in each: (type, pixels).
ICNS_ENTRIES = [(b"ic10", 1024), (b"ic09", 512), (b"ic14", 512), (b"ic08", 256), (b"ic13", 256),
                (b"ic07", 128), (b"ic12", 64), (b"ic11", 32), (b"icp5", 32), (b"icp4", 16)]
APPLE_GRID = 824 / 1024  # the artwork's share of a macOS icon


def to_linear(c):
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def to_srgb(c):
    return np.where(c <= 0.0031308, c * 12.92, 1.055 * np.clip(c, 0, None) ** (1 / 2.4) - 0.055)


def resized(image, size):
    """Lanczos in linear light, premultiplied, so that edges and translucency keep their weight."""
    a = np.asarray(image.convert("RGBA"), dtype=np.float64) / 255.0
    rgb, alpha = to_linear(a[..., :3]) * a[..., 3:], a[..., 3:]
    planes = [Image.fromarray(p.astype(np.float32), mode="F").resize((size, size), Image.LANCZOS)
              for p in np.moveaxis(np.concatenate([rgb, alpha], axis=-1), -1, 0)]
    out = np.stack([np.asarray(p, dtype=np.float64) for p in planes], axis=-1)
    alpha = np.clip(out[..., 3:], 0, 1)
    rgb = to_srgb(np.clip(out[..., :3] / np.maximum(alpha, 1e-6), 0, 1))
    return Image.fromarray(np.rint(np.concatenate([rgb, alpha], axis=-1) * 255).astype(np.uint8), "RGBA")


def on_apple_grid(image, size):
    canvas = Image.new("RGBA", (size, size))
    inner = round(size * APPLE_GRID)
    offset = (size - inner) // 2
    canvas.alpha_composite(resized(image, inner), (offset, offset))
    return canvas


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


def write_icns(path, source):
    grid = {size: png_bytes(on_apple_grid(source, size)) for size in {s for _, s in ICNS_ENTRIES}}
    body = b"".join(kind + struct.pack(">I", 8 + len(grid[size])) + grid[size] for kind, size in ICNS_ENTRIES)
    with open(path, "wb") as f:
        f.write(b"icns" + struct.pack(">I", 8 + len(body)) + body)


def main():
    source = Image.open(os.path.join(HERE, "imageviewer-source.png")).convert("RGBA")
    if source.width != source.height or source.width < 1024:
        raise SystemExit("the source must be square and at least 1024 px")
    resized(source, 1024).save(os.path.join(HERE, "imageviewer.png"), optimize=True)
    write_ico(os.path.join(HERE, "imageviewer.ico"), [resized(source, s) for s in ICO_SIZES])
    write_icns(os.path.join(HERE, "imageviewer.icns"), source)
    print(f"wrote imageviewer.png, imageviewer.ico and imageviewer.icns in {os.path.normpath(HERE)}")


if __name__ == "__main__":
    main()
