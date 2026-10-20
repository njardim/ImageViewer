#!/usr/bin/env python3
"""The application's icons for the three systems, from packaging/icons/imageviewer.png (D-66).

  imageviewer.ico   Windows (the executable's resource, packaging/icons/imageviewer.rc):
                    16, 20, 24, 32, 40, 48, 64, 128 and 256 px.
  imageviewer.icns  macOS (the bundle's CFBundleIconFile): 16 to 1024 px. The artwork sits on
                    Apple's icon grid, 824 of 1024 px with the rest transparent, so that it is as
                    large as the other applications' icons in the Dock and Finder.
  imageviewer.png   Linux and the About dialog, compiled into the executable (:/imageviewer.png),
                    which also gives every window its icon.

Sizes the source does not have are made by Lanczos resampling in linear light, so that downscaled
edges keep their weight. Run it again after replacing the source:
    python3 scripts/make_icons.py
needs: Pillow, numpy
"""
import os

import numpy as np
from PIL import Image

HERE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "packaging", "icons")
ICO_SIZES = [16, 20, 24, 32, 40, 48, 64, 128, 256]
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


def main():
    source = Image.open(os.path.join(HERE, "imageviewer.png")).convert("RGBA")
    if source.width != source.height:
        raise SystemExit("the source must be square")
    largest = resized(source, 256)
    largest.save(os.path.join(HERE, "imageviewer.ico"),
                 sizes=[(s, s) for s in ICO_SIZES], append_images=[resized(source, s) for s in ICO_SIZES[:-1]])
    icns = on_apple_grid(source, 1024)
    icns.save(os.path.join(HERE, "imageviewer.icns"),
              append_images=[on_apple_grid(source, s) for s in (16, 32, 64, 128, 256, 512)])
    print(f"wrote imageviewer.ico and imageviewer.icns in {os.path.normpath(HERE)}")


if __name__ == "__main__":
    main()
