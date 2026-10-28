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

assert page.count('drawString(940, 16, updated, RIGHT)') == 2
assert page.count('FormatClock(updatedAt, updated, sizeof(updated), true)') == 2
assert 'int y = 73 + i * 57;' in page
assert 'drawString(480 + j * 95, y, groups[i].times[j], LEFT)' in page
assert 'TramGroup groups[8]' in page and 'FetchTransitous(rows, 32, count)' in source
assert 'drawString(20, 16, TRAM_STOP_TITLE, LEFT)' in page
assert 20 + measure("Spleen24Bold", "X" * 32)[0] < 601 - 6
assert 760 + 37 < 940 - measure("Spleen24", "23:59:59")[0]
assert 940 <= 960
assert 16 + measure("Spleen24", "23:59:59")[1] < 55 < 73
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
assert 'if (platform.length() > 30)' in rail
assert 'FitTramDestination(rows[i].destination, 24)' in rail
assert 24 + measure("Spleen24", "X" * 24)[0] < 324
assert 324 + measure("Spleen24", "Scheduled")[0] < 445
assert 445 + measure("Spleen24", "Cancelled")[0] < 570
assert 570 + measure("Spleen24", "X" * 30)[0] <= 960
assert 16 + measure("Spleen24", "Scheduled")[1] < 55 < 73
assert 10 + measure("Spleen24Bold", "X" * 8)[1] <= 150 - 101  # Weather header above compass.
assert 'drawString(190, 10, String(updated) + " | " + next, LEFT)' in source
assert 5 + measure("Spleen24Bold", "X" * 12)[0] < 190
assert 'strftime(updated, sizeof(updated), "%Y-%m-%d %H:%M", &localTime)' in source
assert 190 + measure("Spleen24", "2026-10-28 23:59 | 08:00")[0] < 544
assert 568 < 601 - 6 and 760 + 37 < 800
assert 601 + 42 < 675 - 6 and 675 + 42 < 760 - 6
assert 833 + 9 * len("4.3v 99d23:59") <= 960
assert 'setFont(Battery18)' in source
# Graph titles and historical ticks fit between adjacent graphs and the screen edge.
assert '"Wave height (m)"' in source and '"jellies - nice -- vf"' in source
assert '&waterSeries, true, false, now)' in source
assert '&vfSeries, true, true, now)' in source
assert 'String(value + 0.01f, Y1Max < 10 ? 1 : 0), RIGHT)' in source
graph_width, graph_height = 175, 100
graph_x = (960 - graph_width * 4) // 5 + 8
graph_y = 540 - graph_height - 30
gap = graph_x + graph_width
assert graph_x + gap + graph_width + 3 < graph_x + 2 * gap - 10 - measure("Spleen24", "400")[0]
for wave, temp, precip in (("Wave height (m)", "Temperature (°C)", "Rainfall (mm)"),
                           ("Wave height (ft)", "Temperature (°F)", "Snowfall (in)")):
    titles = (wave, temp, "jellies - nice -- vf", precip)
    previous_right = 0
    for index, title in enumerate(titles):
        width, height = measure("Spleen24", title)
        center = graph_x + index * gap + (5 if index == 3 else 0) - 20 + graph_width // 2
        assert previous_right < center - width // 2 and center + (width + 1) // 2 < 960
        assert graph_y - 34 + height < graph_y
        previous_right = center + (width + 1) // 2
for graph in range(4):
    previous_right = 0
    left = graph_x + graph * gap + (5 if graph == 3 else 0)
    for index in range(4):
        label = f"{index - 3 if graph == 2 else index}d"
        width, height = measure("Spleen24", label)
        anchor = left + graph_width * index // 3
        x = anchor if index == 0 else anchor - width if index == 3 else anchor - width // 2
        assert previous_right <= x and left <= x and x + width <= left + graph_width
        assert graph_y + graph_height + 1 + height <= 540
        previous_right = x + width
for row in range(10):
    y = 73 + row * 48
    assert y + measure("Spleen24", "Cancelled")[1] <= 540
    if row < 9:
        assert y + measure("Spleen24", "Cancelled")[1] < y + 35
        assert y + 35 + 2 < y + 48
print("Tram, rail, and loading overlay bounds passed")
