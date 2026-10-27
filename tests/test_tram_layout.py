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
for text in ("Loading weather...", "Loading trams...", "Loading trains...", "Weather unavailable"):
    width, height = measure("Spleen32", text)
    assert 480 - width // 2 > 260 and 480 + width // 2 < 700
    assert 210 < 232 and 232 + height < 286
rail = source.split("void DisplayRailBoard(", 1)[1].split("bool DisplayLoading(", 1)[0]
assert 'int y = 73 + i * 48;' in rail and 'FetchRail(rows, 10, count)' in source
assert 'drawString(940, 16, updated, RIGHT)' in rail
assert '"Destination"' not in rail
assert 20 + measure("Spleen24Bold", "X" * 8 + " - DEPARTURES")[0] < 324
assert '"Platform"' not in rail
assert 760 + 37 < 940 - measure("Spleen24", "23:59")[0]
assert 'if (platform.length() > 30)' in rail
assert 'FitTramDestination(rows[i].destination, 24)' in rail
assert 24 + measure("Spleen24", "X" * 24)[0] < 324
assert 324 + measure("Spleen24", "Scheduled")[0] < 445
assert 445 + measure("Spleen24", "Cancelled")[0] < 570
assert 570 + measure("Spleen24", "X" * 30)[0] <= 960
assert 16 + measure("Spleen24", "Scheduled")[1] < 55 < 73
assert 10 + measure("Spleen24Bold", "X" * 8)[1] <= 150 - 101  # Weather header above compass.
assert 190 + measure("Spleen24", "Wed, 28 Oct 2026 @ 23:59:59")[0] < 544
assert 568 < 601 - 6 and 760 + 37 < 800
assert 601 + 42 < 675 - 6 and 675 + 42 < 760 - 6
assert 833 + 9 * len("4.3v 99d23:59") <= 960
assert 'setFont(Battery18)' in source
for row in range(10):
    y = 73 + row * 48
    assert y + measure("Spleen24", "Cancelled")[1] <= 540
    if row < 9:
        assert y + measure("Spleen24", "Cancelled")[1] < y + 35
        assert y + 35 + 2 < y + 48
print("Tram, rail, and loading overlay bounds passed")
