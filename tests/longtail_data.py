#!/usr/bin/env python3
"""Writes the test files of the long-tail formats that GraphicsMagick reads but cannot write
(decision D-38): each one is built here from its specification, so that every format of the
registry has a test file of known content. The others (gm.pcx, gm.dcx, gm.pict, gm.wpg,
gm.miff, gm.ras, gm.viff, gm.vicar, gm.mat, gm.otb) were written by GraphicsMagick 1.3.48
from a 6x4 image of sRGB (255, 128, 0), `gm convert orange.ppm gm.<ext>` (VICAR in gray, OTB
from an 8x4 bitmap, left half black).

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


for make in (xcf, dicom, tim, cut, macpaint, pix):
    make()
