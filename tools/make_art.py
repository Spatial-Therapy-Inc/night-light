#!/usr/bin/env python3
"""Turns a 466 x 466 picture into art/mushroom.rgb565, the bytes the firmware embeds: RGB565, little endian, with
ordered dithering so the soft glow does not show bands on the 16-bit panel.

  magick art/mushroom.png -depth 8 RGB:- | python3 tools/make_art.py art/mushroom.rgb565

Standard library only. To change the picture: edit art/mushroom.svg (or use your own 466 x 466 image on pure black),
render it to a PNG, and run the line above.
"""
import struct
import sys

SIZE = 466
BAYER = ((0, 8, 2, 10), (12, 4, 14, 6), (3, 11, 1, 9), (15, 7, 13, 5))


def dither(value, bits, threshold):
    drop = 8 - bits
    return min((value + (threshold << drop >> 4)) >> drop, (1 << bits) - 1)


def main():
    rgb = sys.stdin.buffer.read()
    if len(rgb) != SIZE * SIZE * 3:
        sys.exit(f"expected {SIZE * SIZE * 3} bytes of RGB, got {len(rgb)}")
    out = bytearray()
    for y in range(SIZE):
        for x in range(SIZE):
            r, g, b = rgb[(y * SIZE + x) * 3 : (y * SIZE + x) * 3 + 3]
            t = BAYER[y & 3][x & 3]
            out += struct.pack("<H", dither(r, 5, t) << 11 | dither(g, 6, t) << 5 | dither(b, 5, t))
    with open(sys.argv[1], "wb") as f:
        f.write(out)


if __name__ == "__main__":
    main()
