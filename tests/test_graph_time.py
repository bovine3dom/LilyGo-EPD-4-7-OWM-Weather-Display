#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check timestamp positions, clipped lines and missing-data gaps."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parent.parent
source = (root / "LilyGo-EPD-4-7-OWM-Weather-Display.ino").read_text()
start = source.index("int GraphY(")
end = source.index("\nvoid DrawGraph(", start)
plot = source[start:end]
harness = r'''
#include "graph_time.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <vector>
using std::max;
using std::min;
template<class T> T constrain(T value, T low, T high) { return min(high, max(low, value)); }
#define Black 0
struct Line { int x1, y1, x2, y2; };
std::vector<Line> lines, bars, pixels;
void drawLine(int x1, int y1, int x2, int y2, int) { lines.push_back({x1,y1,x2,y2}); }
void drawPixel(int x, int y, int) { pixels.push_back({x,y,x,y}); }
void fillRect(int x, int y, int width, int height, int) { bars.push_back({x,y,width,height}); }
'''+plot+r'''
int main() {
    const time_t now = 1800000000, end = now + 3 * 86400;
    assert(GraphTimeX(now, now, end, 60, 175) == 60);
    assert(GraphTimeX(now + 3 * 3600, now, end, 60, 175) == 67);
    assert(GraphTimeX(now + 24 * 3600, now, end, 60, 175) == 118);
    assert(GraphTimeX(end, now, end, 60, 175) == 235);
    time_t left, right;
    assert(ClipGraphTime(now - 3600, now + 3600, now, end, 10800, left, right));
    assert(left == now && right == now + 3600);
    assert(!ClipGraphTime(now + 3600, now + 7 * 3600, now, end, 16200, left, right));
    time_t times[] = {now + 3 * 3600, now + 6 * 3600, now + 12 * 3600, now + 15 * 3600, now + 18 * 3600};
    float values[] = {1, 2, 3, NAN, 5};
    GraphSeries series{times, values, 5, 16200};
    PlotGraphSeries(series, now, end, 60, 410, 175, 100, 0, 10, false, false);
    assert(lines.size() == 2 && lines[0].x1 == 67 && lines[0].x2 == 74);
    assert(pixels.size() == 8 && pixels[0].x1 == 67 && pixels[2].x1 == 74);
    lines.clear(); pixels.clear();
    PlotGraphSeries(series, now, end, 60, 410, 175, 100, 0, 10, false, true);
    assert(bars.size() == 4 && bars[0].x1 == 61 && bars[0].x2 == 6);
    lines.clear(); pixels.clear();
    time_t historyTimes[] = {now - 3 * 86400 - 1800, now - 2 * 86400 - 1800,
                             now - 86400 - 1800, now - 1800};
    float historyValues[] = {1, 2, 3, 4};
    GraphSeries history{historyTimes, historyValues, 4, 36 * 3600};
    PlotGraphSeries(history, now - 3 * 86400, now, 60, 410, 175, 100, 0, 10, false, false);
    assert(lines.size() == 6 && lines[0].x1 == 60);
    assert(pixels.size() == 6 && pixels.back().x1 < 235);
}
'''
with tempfile.TemporaryDirectory() as directory:
    cpp = Path(directory) / "graph-test.cpp"
    exe = Path(directory) / "graph-test"
    cpp.write_text(harness)
    subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-I", str(root), str(cpp), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print("Graph timestamp and missing-data tests passed")
