#!/usr/bin/env python3
"""PQ and HLG corpus (Phase 1, criteria H1 and H2; decision D-65), and its reference.

Each image is a ramp of 256 signal levels from 0 to 1 (PQ: 0 to 10,000 nits) across, in four
rows of BT.2020 colour: neutral (E, E, E), warm (E, 0.75 E, 0.5 E), and the BT.2020 red and
green primaries (E, 0, 0) and (0, E, 0), which lie outside BT.709. The reference below is
written from the standards alone (ITU-R BT.2100 and SMPTE ST 2084, primaries from their
chromaticities), never from the viewer's code (§10: the product's engine is not the reference).

Files:
  - PNG, 16 bits, with a cICP chunk (9/16/0/1 and 9/18/0/1): written here, at test time.
  - JPEG XL, 16 bits lossless, with the colour encoding Rec.2100 PQ or HLG; and AVIF, 12 bits
    lossless (RGB, matrix 0) with nclx 9/16/0/1 or 9/18/0/1: committed in tests/data
    (pq-ramp.jxl, hlg-ramp.jxl, pq-ramp.avif, hlg-ramp.avif), made from this script's PNGs by
        python3 tests/hdr_corpus.py --data
    which runs (libjxl 0.7; FFmpeg 6.1 with libaom):
        cjxl ramp16.png X.jxl -d 0 -x color_space=RGB_D65_202_Rel_PeQ   (HLG: ..._Rel_HLG)
        ffmpeg -i ramp12.png -pix_fmt gbrp12le -c:v libaom-av1 -aom-params lossless=1
               -still-picture 1 -color_primaries bt2020 -color_trc smpte2084 (HLG: arib-std-b67)
               -colorspace rgb -color_range pc X.avif
    Both encoders store the codes exactly (checked by decoding them again); the AVIF's are the
    ramp's own 12-bit codes, given to FFmpeg in 16 bits by bit replication.

usage: python3 tests/hdr_corpus.py [--data | <directory>]
"""
import os
import struct
import subprocess
import sys
import tempfile
import zlib

import numpy as np

LEVELS = 256
ROWS = [(1.0, 1.0, 1.0), (1.0, 0.75, 0.5), (1.0, 0.0, 0.0), (0.0, 1.0, 0.0)]  # signal multipliers
REFERENCE_WHITE_NITS = 203.0  # ITU-R BT.2408: 1.0 of the viewer's working space
HLG_PEAK_NITS = 1000.0        # the nominal display of BT.2100's HLG OOTF (D-P15)
TRANSFERS = {"pq": 16, "hlg": 18}

# SMPTE ST 2084
M1, M2 = 2610 / 16384, 2523 / 4096 * 128
C1, C2, C3 = 3424 / 4096, 2413 / 4096 * 32, 2392 / 4096 * 32


def pq_to_nits(e):
    p = np.clip(np.asarray(e, dtype=np.float64), 0, 1) ** (1 / M2)
    return 10000 * (np.maximum(p - C1, 0) / (C2 - C3 * p)) ** (1 / M1)


def nits_to_pq(nits):
    y = np.clip(np.asarray(nits, dtype=np.float64) / 10000, 0, 1) ** M1
    return ((C1 + C2 * y) / (1 + C3 * y)) ** M2


# ITU-R BT.2100 HLG
HLG_A = 0.17883277
HLG_B = 1 - 4 * HLG_A
HLG_C = 0.5 - HLG_A * np.log(4 * HLG_A)
BT2020_LUMA = np.array([0.2627, 0.6780, 0.0593])


def hlg_to_nits(e):
    """Display light of the nominal 1000-nit display: inverse OETF, then the OOTF (gamma 1.2)."""
    e = np.clip(np.asarray(e, dtype=np.float64), 0, 1)
    scene = np.where(e <= 0.5, e * e / 3, (np.exp((e - HLG_C) / HLG_A) + HLG_B) / 12)
    gamma = 1.2 + 0.42 * np.log10(HLG_PEAK_NITS / 1000)
    ys = scene @ BT2020_LUMA
    return HLG_PEAK_NITS * (ys ** (gamma - 1))[..., None] * scene


def rgb_to_xyz(primaries, white):
    xyz = np.array([[x / y, 1.0, (1 - x - y) / y] for x, y in primaries]).T
    w = np.array([white[0] / white[1], 1.0, (1 - white[0] - white[1]) / white[1]])
    return xyz * np.linalg.solve(xyz, w)


D65 = (0.3127, 0.3290)
BT709_TO_BT2020 = np.linalg.solve(rgb_to_xyz([(0.708, 0.292), (0.170, 0.797), (0.131, 0.046)], D65),
                                  rgb_to_xyz([(0.64, 0.33), (0.30, 0.60), (0.15, 0.06)], D65))


def codes(bits):
    """The ramp as integer codes of `bits`: rows x levels x RGB."""
    top = (1 << bits) - 1
    signal = np.linspace(0, 1, LEVELS)
    return np.rint(np.array(ROWS)[:, None, :] * signal[None, :, None] * top).astype(np.uint16)


def reference_nits(kind, code, bits):
    """BT.2020 linear display light in nits for the stored codes."""
    e = code.astype(np.float64) / ((1 << bits) - 1)
    return pq_to_nits(e) if kind == "pq" else hlg_to_nits(e)


def write_png16(path, code, cicp=None):
    """RGB, 16 bits, with a cICP chunk (primaries, transfer, matrix, full range) when given."""
    height, width, _ = code.shape
    raw = b"".join(b"\0" + code[y].astype(">u2").tobytes() for y in range(height))

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 16, 2, 0, 0, 0))
    if cicp:
        png += chunk(b"cICP", bytes(cicp))
    png += chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


def write_pngs(directory):
    """pq.png and hlg.png (16 bits, cICP) into `directory`; returns their paths by kind."""
    paths = {}
    for kind, transfer in TRANSFERS.items():
        paths[kind] = os.path.join(directory, f"{kind}.png")
        write_png16(paths[kind], codes(16), (9, transfer, 0, 1))
    return paths


# The committed files: (name, kind, bits of their codes).
DATA_FILES = [("pq-ramp.jxl", "pq", 16), ("hlg-ramp.jxl", "hlg", 16), ("pq-ramp.avif", "pq", 12),
              ("hlg-ramp.avif", "hlg", 12)]


def write_data(data_dir):
    with tempfile.TemporaryDirectory() as work:
        ramp16, ramp12 = os.path.join(work, "ramp16.png"), os.path.join(work, "ramp12.png")
        write_png16(ramp16, codes(16))
        c12 = codes(12)
        write_png16(ramp12, (c12 << 4) | (c12 >> 8))  # 12-bit codes in 16 bits by bit replication
        for name, kind, _ in DATA_FILES:
            out = os.path.join(data_dir, name)
            if name.endswith(".jxl"):
                space = "RGB_D65_202_Rel_PeQ" if kind == "pq" else "RGB_D65_202_Rel_HLG"
                command = ["cjxl", ramp16, out, "-d", "0", "-x", f"color_space={space}"]
            else:
                trc = "smpte2084" if kind == "pq" else "arib-std-b67"
                command = ["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-i", ramp12, "-pix_fmt", "gbrp12le",
                           "-c:v", "libaom-av1", "-aom-params", "lossless=1", "-still-picture", "1",
                           "-color_primaries", "bt2020", "-color_trc", trc, "-colorspace", "rgb", "-color_range", "pc", out]
            subprocess.run(command, check=True, stdout=subprocess.DEVNULL)
            if name.endswith(".avif"):  # the codes, decoded again: planes G, B, R
                planes = subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-i", out, "-f", "rawvideo",
                                         "-pix_fmt", "gbrp12le", "-"], check=True, capture_output=True).stdout
                g, b, r = np.frombuffer(planes, dtype="<u2").reshape(3, len(ROWS), LEVELS)
                if not np.array_equal(np.stack([r, g, b], axis=-1), c12):
                    sys.exit(f"{out}: the codes did not survive the encoder")
            print(f"wrote {out}")


if __name__ == "__main__":
    if len(sys.argv) == 2 and sys.argv[1] == "--data":
        write_data(os.path.join(os.path.dirname(os.path.abspath(__file__)), "data"))
    elif len(sys.argv) == 2:
        for path in write_pngs(sys.argv[1]).values():
            print(f"wrote {path}")
    else:
        sys.exit(__doc__)
