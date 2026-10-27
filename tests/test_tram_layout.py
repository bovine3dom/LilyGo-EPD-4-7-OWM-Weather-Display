#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check the departure board text bounds and spacing."""
from pathlib import Path
import re

root = Path(__file__).resolve().parent.parent
source = (root / "LilyGo-EPD-4-7-OWM-Weather-Display.ino").read_text()
page = source.split("void DisplayTramBoard(", 1)[1].split("bool RunTramPage()", 1)[0]
fonts = (root / "fonts" / "spleen.h").read_text()

def measure(font, text):
    section = re.search(rf"static const GFXglyph {font}Glyphs\[\] = \{{(.*?)\}};", fonts, re.S).group(1)
    glyphs = [tuple(map(int, row)) for row in re.findall(
        r"\{(\d+),(\d+),(\d+),(-?\d+),(-?\d+),(\d+),(\d+)\}", section)]
    def glyph(char):
        code = ord(char)
        return glyphs[code - 32 if code < 127 else 95 + code - 160]
    return sum(glyph(c)[2] for c in text), max(glyph(c)[1] for c in text)

assert 'drawString(410, 16, updated, LEFT)' in page
assert 'int y = 73 + i * 57;' in page
assert 'drawString(480 + j * 95, y, groups[i].times[j], LEFT)' in page
assert 'TramGroup groups[8]' in page and 'FetchTransitous(rows, 32, count)' in source
assert 'drawString(20, 16, TRAM_STOP_TITLE, LEFT)' in page
assert 20 + measure("Spleen24Bold", "X" * 32)[0] < 410
assert 410 + measure("Spleen24", "23:59")[0] < 960
assert 16 + measure("Spleen24", "23:59")[1] < 55 < 73
assert 24 + measure("Spleen32", "L12345")[0] < 130
assert 130 + measure("Spleen32", "X" * 20)[0] < 480
for column in range(5):
    x = 480 + column * 95
    assert x + measure("Spleen32", "23:59")[0] <= 960
    if column < 4:
        assert x + measure("Spleen32", "23:59")[0] < x + 95
for row in range(8):
    y = 73 + row * 57
    assert y + measure("Spleen32", "23:59")[1] < y + 44
    assert y + 44 + 2 <= 540
    if row < 7:
        assert y + 44 + 2 < y + 57
loading = source.split("bool DisplayLoading(", 1)[1].split("bool RestoreWeatherPage(", 1)[0]
assert 'fillRect(260, 210, 440, 76, White)' in loading
assert 'drawString(480, 232, message, CENTER)' in loading
for text in ("Loading weather...", "Loading trams...", "Weather unavailable"):
    width, height = measure("Spleen32", text)
    assert 480 - width // 2 > 260 and 480 + width // 2 < 700
    assert 210 < 232 and 232 + height < 286
print("Tram board and loading overlay bounds passed")
