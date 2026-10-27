#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check button glyph placement and nearby header bounds."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parent.parent
source = (root / "LilyGo-EPD-4-7-OWM-Weather-Display.ino").read_text()
start = source.index("void DrawButtonHints(ButtonHintPage page)")
end = source.index("\nString FitTramDestination", start)
function = source[start:end]
for name, following, page in (("DisplayTramBoard", "DisplayRailBoard", "TramPage"),
                              ("DisplayRailBoard", "DisplayLoading", "RailPage"),
                              ("DisplayWeather", "DisplayGeneralInfoSection", "WeatherPage")):
    section = source.split("void " + name + "(", 1)[1].split(following + "(", 1)[0]
    assert f"DrawButtonHints({page});" in section
assert 'DisplayStatusSection(600, 20, wifi_signal)' in source
assert 'drawString(5, 10, City, LEFT)' in source

harness = r'''
#include <cassert>
#include "button_icons.h"
#define T5_47_PLUS_V2 1
#define Black 0
int counts[3] = {}, triangles = 0;
ButtonHintPage currentPage = WeatherPage;
void fillTriangle(int tipX, int tipY, int leftX, int leftY, int rightX, int rightY, int) {
    int centers[] = {601, 675, 760};
    assert(triangles < 3 && tipX == centers[triangles++]);
    int top = currentPage == WeatherPage ? 16 : 22;
    assert(tipY == top && leftY == top + 12 && rightY == top + 12 && leftX == tipX - 6 && rightX == tipX + 6);
}
void drawPixel(int x, int y, int) {
    int top = currentPage == WeatherPage ? 16 : 22;
    assert(y >= top - 3 && y <= top + 16);
    int centers[] = {601, 675, 760};
    for (int i = 0; i < 3; ++i) {
        int offset = i == 2 ? 8 : 13;
        if (x >= centers[i] + offset && x <= centers[i] + offset + 29) {
            int glyph = (i == 0 && currentPage == TramPage) || (i == 1 && currentPage == RailPage) ? 3 : i;
            assert(ButtonIcons[glyph][y - top + 3][x - centers[i] - offset] == '#');
            ++counts[i]; return;
        }
    }
    assert(false);
}
'''+function+r'''
int main() {
    for (int page = 0; page < 3; ++page) {
        currentPage = static_cast<ButtonHintPage>(page);
        triangles = 0;
        for (int &count : counts) count = 0;
        DrawButtonHints(currentPage);
        assert(triangles == 3);
        for (int count : counts) assert(count > 20);
    }
    for (int i = 0; i < 4; ++i)
        for (int y = 0; y < 20; ++y) assert(ButtonIcons[i][y][30] == '\0');
}
'''
with tempfile.TemporaryDirectory() as directory:
    cpp = Path(directory) / "icons.cpp"
    executable = Path(directory) / "icons"
    cpp.write_text(harness)
    subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-I", str(root), str(cpp), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
print("Button glyph and header bounds passed")
