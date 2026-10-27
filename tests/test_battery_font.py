#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check the battery font and its supported characters."""
from pathlib import Path
import re
import zlib

root = Path(__file__).resolve().parent.parent
source = (root / "fonts/battery_9x18.h").read_text()
data = bytes(int(value, 16) for value in re.findall(
    r"0x([0-9a-fA-F]{2})", source.split("static const GFXglyph", 1)[0]))
section = source.split("static const GFXglyph Battery18Glyphs[] = {", 1)[1].split("};", 1)[0]
glyphs = [tuple(map(int, row)) for row in re.findall(
    r"\{(\d+),(\d+),(\d+),(-?\d+),(-?\d+),(\d+),(\d+)\}", section)]
codes = [0x20, *range(0x2d, 0x3b), ord("d"), ord("v")]
assert len(glyphs) == len(codes) == 17
assert {ord(c) for c in "4.3v 99d23:59--"} <= set(codes)
assert "{0x20,0x20,0},{0x2d,0x3a,1},{0x64,0x64,15},{0x76,0x76,16}" in source
for code, (width, height, advance, left, top, size, offset) in zip(codes, glyphs):
    assert (width, height, advance, left, top) == (9, 18, 9, 0, 18)
    bitmap = zlib.decompress(data[offset:offset + size])
    assert len(bitmap) == 5 * 18
    assert all((b & 15) in (0, 15) and (b >> 4) in (0, 15) for b in bitmap)
    assert bool(any(bitmap)) == (code != 0x20)
print("Battery font tests passed")
