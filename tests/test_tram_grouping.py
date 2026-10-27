#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check grouping, ordering, and the five-departure limit."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parent.parent
library = root / ".pio/libdeps/T5_4_7Inc_Plus_V2/ArduinoJson/src"
harness = r'''
#include "transitous.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
void set(TramDeparture &row, const char *line, const char *destination, const char *scheduled, const char *expected) {
    snprintf(row.line, sizeof(row.line), "%s", line);
    snprintf(row.destination, sizeof(row.destination), "%s", destination);
    snprintf(row.scheduled, sizeof(row.scheduled), "%s", scheduled);
    snprintf(row.expected, sizeof(row.expected), "%s", expected);
}
int main() {
    TramDeparture rows[10] = {};
    set(rows[0], "L2", "Beta", "10:00", "10:02");
    set(rows[1], "L1", "Beta", "10:03", "--:--");
    const char *times[] = {"10:04", "10:05", "10:06", "10:07", "10:08", "10:09"};
    for (int i = 0; i < 6; ++i) set(rows[i + 2], "L1", "Alpha", "11:00", times[i]);
    set(rows[8], "L2", "Alpha", "10:10", "--:--");
    set(rows[9], "L1", "Beta", "10:11", "10:12");
    TramGroup groups[8] = {};
    size_t n = GroupTramDepartures(rows, 10, groups, 8);
    assert(n == 4);
    assert(!strcmp(groups[0].line, "L2") && !strcmp(groups[0].destination, "Beta"));
    assert(!strcmp(groups[0].times[0], "10:02"));
    assert(!strcmp(groups[1].line, "L1") && !strcmp(groups[1].destination, "Beta"));
    assert(groups[1].count == 2 && !strcmp(groups[1].times[0], "10:03") && !strcmp(groups[1].times[1], "10:12"));
    assert(!strcmp(groups[2].line, "L1") && !strcmp(groups[2].destination, "Alpha"));
    assert(groups[2].count == 5 && !strcmp(groups[2].times[0], "10:04") && !strcmp(groups[2].times[4], "10:08"));
    assert(!strcmp(groups[3].line, "L2") && !strcmp(groups[3].destination, "Alpha"));
    n = GroupTramDepartures(rows, 10, groups, 2);
    assert(n == 2 && !strcmp(groups[0].line, "L2") && !strcmp(groups[1].line, "L1"));
    assert(GroupTramDepartures(rows, 10, nullptr, 0) == 0);
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1); tzset();
    std::string json = R"JSON({"stopTimes":[
      {"routeShortName":"L1","headsign":"Alpha","realTime":true,"place":{"scheduledDeparture":"2027-01-01T12:45:00Z","departure":"2027-01-01T12:17:00Z","departureTimestampReason":"PREDICTED"}},
      {"routeShortName":"L2","headsign":"Beta","place":{"scheduledDeparture":"2027-01-01T12:16:00Z"}},
      {"routeShortName":"L1","headsign":"Alpha","place":{"scheduledDeparture":"2027-01-01T12:20:00Z"}}
    ]})JSON";
    size_t count = 0;
    assert(ParseTransitous(json.data(), json.size(), rows, 10, count, 0) && count == 3);
    assert(GroupTramDepartures(rows, count, groups, 8) == 2);
    assert(!strcmp(groups[0].line, "L2") && !strcmp(groups[0].times[0], "13:16"));
    assert(!strcmp(groups[1].line, "L1") && !strcmp(groups[1].times[0], "13:17") && !strcmp(groups[1].times[1], "13:20"));
}
'''
with tempfile.TemporaryDirectory() as directory:
    cpp = Path(directory) / "group.cpp"
    executable = Path(directory) / "group"
    cpp.write_text(harness)
    subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-I", str(root), "-I", str(library),
                    str(cpp), str(root / "transitous_parse.cpp"), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
print("Tram grouping tests passed")
