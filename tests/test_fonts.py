#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check the generated Spleen glyph data."""

from pathlib import Path
import re
import zlib

source = (Path(__file__).resolve().parent.parent / "fonts" / "spleen.h").read_text()
for name in ("Spleen16", "Spleen24", "Spleen32", "Spleen24Bold", "Spleen48Bold", "Spleen64Bold"):
    data_section = re.search(rf"static const uint8_t {name}Data\[\] = \{{(.*?)\}};", source, re.S).group(1)
    glyph_section = re.search(rf"static const GFXglyph {name}Glyphs\[\] = \{{(.*?)\}};", source, re.S).group(1)
    data = bytes(int(value, 16) for value in re.findall(r"0x([0-9a-fA-F]{2})", data_section))
    glyphs = [tuple(map(int, row)) for row in re.findall(
        r"\{(\d+),(\d+),(\d+),(-?\d+),(-?\d+),(\d+),(\d+)\}", glyph_section)]
    assert len(glyphs) == 191
    for width, height, advance, left, top, length, offset in glyphs:
        bitmap = zlib.decompress(data[offset:offset + length])
        assert len(bitmap) == ((width + 1) // 2) * height
        assert all((b & 15) in (0, 15) and (b >> 4) in (0, 15) for b in bitmap)
    degree = glyphs[95 + 0xb0 - 0xa0]
    assert any(zlib.decompress(data[degree[6]:degree[6] + degree[5]]))
print("Generated Spleen font tests passed")
