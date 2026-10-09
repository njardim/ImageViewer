#!/usr/bin/env python3
"""Writes the test files that no common tool writes correctly (decision D-38): each one is
built here from its specification, so that the registry's formats have test files of known
content. Content: sRGB (255, 128, 0), 6x4 unless said otherwise.

The other test files of the registry were written by tools from the same 6x4 orange image
(orange.ppm):
- GraphicsMagick 1.3.48, `gm convert orange.ppm gm.<ext>`: gm.pcx, gm.dcx, gm.pict, gm.wpg,
  gm.miff, gm.ras, gm.viff, gm.mat, gm.cin, gm.psd, gm.xpm, gm.xbm, gm.wbmp; gm.vicar in gray;
  gm.otb from an 8x4 bitmap, left half black.
- oiiotool 2.4, `oiiotool orange.ppm -o orange.<ext>`: orange.dpx, .hdr, .bmp, .ico,
  .sgi, .iff, .tga, .rla; gray.zfile from the green channel as float.
- Pillow 12.3: orange.dds (16x16 RGBA), orange.icns (16x16); alpha8.bmp and alpha8.sgi from
  alpha8.png (straight alpha, which oiiotool would premultiply); transparent.gif, noloop.gif and
  loop2.gif (no loop count, and a count of 2).
- cjxl 0.7, `cjxl camera.jpg camera.jxl` (lossless recompression, EXIF in a Brotli box).

usage: python3 tests/longtail_data.py <output directory>
"""
import os
import struct
import sys

out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "data")


def write(name, data):
    with open(os.path.join(out, name), "wb") as f:
        f.write(data)


# GIMP XCF (version 0, RLE tiles as GIMP saves them): a 6x4 canvas, an opaque orange layer
# under a 3x4 blue layer at 50 % opacity (128/255) in the top-left corner.
def xcf():
    def rle_constant(count, value):  # one repeat run; count <= 127
        return bytes([count - 1, value])

    def layer(width, height, rgba, opacity, name):
        n = width * height
        tile = b"".join(rle_constant(n, c) for c in rgba)  # one channel after another
        props = (struct.pack(">III", 6, 4, opacity) + struct.pack(">III", 8, 4, 1)  # opacity, visible
                 + struct.pack(">IIii", 15, 8, 0, 0) + struct.pack(">II", 0, 0))  # offsets, end
        header = struct.pack(">III", width, height, 1) + struct.pack(">I", len(name) + 1) + name + b"\0" + props
        return header, tile, width, height

    width, height = 6, 4
    layers = [layer(3, 4, (0, 0, 255, 255), 128, b"blue"),  # first in the file = topmost
              layer(6, 4, (255, 128, 0, 255), 255, b"orange")]
    data = bytearray(b"gimp xcf file\0" + struct.pack(">III", width, height, 0))
    data += struct.pack(">IIB", 17, 1, 1) + struct.pack(">II", 0, 0)  # PROP_COMPRESSION RLE, PROP_END
    table = len(data)
    data += bytes(4 * (len(layers) + 1) + 4)  # layer offsets, 0, channel offsets: 0
    offsets = []
    for header, tile, w, h in layers:
        offsets.append(len(data))
        data += header
        hierarchy_at = len(data)
        data += bytes(8)  # hierarchy offset, layer mask offset (none)
        hierarchy = len(data)
        data += struct.pack(">III", w, h, 4)
        level_at = len(data)
        data += bytes(8)  # level offset, end of levels
        level = len(data)
        data += struct.pack(">II", w, h)
        tile_at = len(data)
        data += bytes(8)  # tile offset, end of tiles
        tile_offset = len(data)
        data += tile
        struct.pack_into(">II", data, hierarchy_at, hierarchy, 0)
        struct.pack_into(">I", data, level_at, level)
        struct.pack_into(">I", data, tile_at, tile_offset)
    for i, offset in enumerate(offsets):
        struct.pack_into(">I", data, table + 4 * i, offset)
    write("layers.xcf", bytes(data))


# DICOM (Part 10, explicit VR little endian): 4x3 16-bit MONOCHROME2; pixel (0,0) = 32768,
# the others span 0..65535.
def dicom():
    def element(group, elem, vr, value):
        if vr in (b"OB", b"OW", b"UN", b"SQ"):
            return struct.pack("<HH", group, elem) + vr + b"\0\0" + struct.pack("<I", len(value)) + value
        return struct.pack("<HH", group, elem) + vr + struct.pack("<H", len(value)) + value

    def us(v):
        return struct.pack("<H", v)

    syntax = b"1.2.840.10008.1.2.1\0"  # even length
    meta = (element(0x0002, 0x0001, b"OB", b"\0\1") + element(0x0002, 0x0002, b"UI", b"1.2.840.10008.5.1.4.1.1.7\0")
            + element(0x0002, 0x0003, b"UI", b"1.2.3.4\0") + element(0x0002, 0x0010, b"UI", syntax))
    group_length = element(0x0002, 0x0000, b"UL", struct.pack("<I", len(meta)))
    values = [32768, 0, 65535, 16384, 49152, 8192, 24576, 40960, 57344, 4096, 12288, 20480]
    pixels = struct.pack("<12H", *values)
    dataset = (element(0x0028, 0x0002, b"US", us(1)) + element(0x0028, 0x0004, b"CS", b"MONOCHROME2 ")
               + element(0x0028, 0x0010, b"US", us(3)) + element(0x0028, 0x0011, b"US", us(4))
               + element(0x0028, 0x0100, b"US", us(16)) + element(0x0028, 0x0101, b"US", us(16))
               + element(0x0028, 0x0102, b"US", us(15)) + element(0x0028, 0x0103, b"US", us(0))
               + element(0x7FE0, 0x0010, b"OW", pixels))
    write("gray16.dcm", bytes(128) + b"DICM" + group_length + meta + dataset)


# PlayStation TIM, 24 bits per pixel: 6x4 sRGB (255, 128, 0). The width is counted in
# 16-bit units (18 bytes per row = 9).
def tim():
    pixels = bytes([255, 128, 0]) * 24
    block = struct.pack("<IHHHH", 12 + len(pixels), 0, 0, 9, 4) + pixels
    write("rgb.tim", struct.pack("<II", 0x10, 3) + block)


# Dr. Halo CUT, 8 bits, no palette file: 6x4 of gray 128. Each row: its byte count, one run
# of 6, the end-of-row 0.
def cut():
    row = struct.pack("<H", 3) + bytes([0x86, 128, 0])
    write("gray.cut", struct.pack("<HHH", 6, 4, 0) + row * 4)


# MacPaint: 512-byte header, 720 rows of 576 pixels, PackBits; left half black, right half
# white (MacPaint bit 1 = black).
def macpaint():
    row = bytes([256 - 35, 0xFF, 256 - 35, 0x00])  # 36 bytes of 0xFF, 36 of 0x00
    write("bw.mac", bytes(512) + row * 720)


# Alias PIX, 24 bits: 6x4 sRGB (255, 128, 0), one run (count, blue, green, red) per row.
def pix():
    write("rgb.pix", struct.pack(">HHHHH", 6, 4, 0, 0, 24) + bytes([6, 0, 128, 255]) * 4)


# Softimage PIC: one mixed run-length packet (R, G, B), each row a single run of 6 pixels.
# (OpenImageIO 2.4 reads uncompressed PIC packets as black; real files use run lengths.)
def softimage():
    header = struct.pack(">If80s4sHHfHH", 0x5380F634, 3.71, b"imageViewer test", b"PICT", 6, 4, 1.0, 3, 0)
    packet = bytes([0, 8, 2, 0x80 | 0x40 | 0x20])  # last packet, 8 bits, mixed run length, R G B
    write("orange.pic", header + packet + bytes([128 + 5, 255, 128, 0]) * 4)


# Windows cursor: 16x16, 32-bit BGRA with its AND mask (rows padded to 4 bytes), hotspot
# (1, 1). Pillow's ICO writer leaves the mask rows unpadded, which Qt rejects.
def cursor():
    w = h = 16
    xor = bytes([0, 128, 255, 255]) * (w * h)
    mask = bytes(4) * h
    dib = struct.pack("<IiiHHIIiiII", 40, w, 2 * h, 1, 32, 0, len(xor) + len(mask), 0, 0, 0, 0) + xor + mask
    entry = struct.pack("<BBBBHHII", w, h, 0, 0, 1, 1, len(dib), 6 + 16)
    write("orange.cur", struct.pack("<HHH", 0, 2, 1) + entry + dib)


# FITS (NOST 100-2.0), BITPIX 8: NAXIS1 is the width, NAXIS2 the height and NAXIS3 the colour
# planes, R then G then B, each 6x4; header and data padded to 2880-byte blocks. (oiiotool 2.4
# wrote the channels along NAXIS1, which OpenImageIO 3.2 reads, as the standard says, as width.)
def fits():
    cards = ["SIMPLE  =                    T", "BITPIX  =                    8", "NAXIS   =                    3",
             "NAXIS1  =                    6", "NAXIS2  =                    4", "NAXIS3  =                    3", "END"]
    header = "".join(card.ljust(80) for card in cards).encode("ascii")
    header += b" " * (-len(header) % 2880)
    data = bytes([255]) * 24 + bytes([128]) * 24 + bytes(24)
    write("orange.fits", header + data + bytes(-len(data) % 2880))
    # A volume (NAXIS3 = 5 > 4): 5 slices of 6x4, slice k of gray 200 - 40k. OpenImageIO reads
    # every slice into the buffer; a buffer sized for one slice overflowed (0.4 review).
    cards[5] = "NAXIS3  =                    5"
    header = "".join(card.ljust(80) for card in cards).encode("ascii")
    header += b" " * (-len(header) % 2880)
    data = b"".join(bytes([200 - 40 * k]) * 24 for k in range(5))
    write("volume.fits", header + data + bytes(-len(data) % 2880))
    # Rows differ: FITS stores the bottom row first, so the top-left pixel is the last row's (128).
    # OpenImageIO 3.2.1.1 read every row one off (0.4 review, our fits-row-offset patch).
    cards = cards[:5] + ["END"]
    cards[2] = "NAXIS   =                    2"
    header = "".join(card.ljust(80) for card in cards).encode("ascii")
    header += b" " * (-len(header) % 2880)
    data = b"".join(bytes([value]) * 6 for value in (32, 64, 96, 128))
    write("rows.fits", header + data + bytes(-len(data) % 2880))


def svg():
    write("orange.svg", b'<svg xmlns="http://www.w3.org/2000/svg" width="6" height="4">'
                        b'<rect width="6" height="4" fill="#ff8000"/></svg>\n')


for make in (xcf, dicom, tim, cut, macpaint, pix, softimage, cursor, fits, svg):
    make()
