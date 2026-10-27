#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check rail times, prediction matching, and platform formatting."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parent.parent
library = root / ".pio/libdeps/T5_4_7Inc_Plus_V2/ArduinoJson/src"
harness = r'''
#include "rail.h"
#include <cassert>
#include <cstring>
#include <cstdlib>
int main() {
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1); tzset();
    DynamicJsonDocument doc(12000), predictions(12000);
    const char *json = R"JSON([
      {"direction":"Arrival","uic":"123","traffic":{"destination":"ignore"}},
      {"direction":"Departure","uic":"123","traffic":{"destination":"late"},"scheduledTime":"2027-03-28T01:10:00Z","actualTime":"2027-03-28T01:20:00Z","platform":{"track":"C","isTrackactive":false}},
      {"direction":"Departure","uic":"123","traffic":{"destination":"earlier"},"scheduledTime":"2027-03-28T01:15:00Z","actualTime":"2027-03-28T01:05:00Z","platform":{"track":""}},
      {"direction":"Departure","uic":"123","traffic":{"destination":"cancelled"},"scheduledTime":"2027-03-28T01:30:00Z","informationStatus":{"trainStatus":"SUPPRESSION_TOTALE"}},
      {"direction":"Departure","uic":"123","traffic":{"destination":"past"},"scheduledTime":"2027-03-28T00:00:00Z"},
      {"direction":"Departure","uic":"other","traffic":{"destination":"wrong station"},"scheduledTime":"2027-03-28T01:10:00Z"}
    ])JSON";
    const char *pred = R"JSON({"predictions":[
      {"probabilities":[{"platform":"A","prob":0.65},{"platform":"B","prob":0.31}]},
      {"probabilities":[{"platform":"A","prob":0.08},{"platform":"B","prob":0.08},{"platform":"D","prob":0.04}]},
      {"probabilities":[]}, {"probabilities":[]}
    ]})JSON";
    assert(!deserializeJson(doc, json)); assert(!deserializeJson(predictions, pred));
    RailDeparture rows[5] = {}; size_t count;
    assert(ParseRail(doc.as<JsonArrayConst>(), predictions["predictions"].as<JsonArrayConst>(), rows, 5, count, 1806192001, "123"));
    assert(count == 3);
    assert(!strcmp(rows[0].destination, "earlier") && !strcmp(rows[0].scheduled, "03:15") && !strcmp(rows[0].expected, "03:05"));
    assert(!strcmp(rows[0].platform, "A-B (16%)"));
    assert(!strcmp(rows[1].destination, "late") && !strcmp(rows[1].platform, "C | A (65%), B (31%)"));
    assert(!strcmp(rows[2].expected, "Cancelled") && !strcmp(rows[2].platform, "--"));
    // Expected time controls the cutoff; a departure exactly at the cutoff remains visible.
    assert(ParseRail(doc.as<JsonArrayConst>(), predictions["predictions"].as<JsonArrayConst>(), rows, 5, count, 1806195900, "123") && count == 3);
    assert(ParseRail(doc.as<JsonArrayConst>(), predictions["predictions"].as<JsonArrayConst>(), rows, 5, count, 1806195901, "123") && count == 2);
    assert(!strcmp(rows[0].destination, "late"));
    assert(ParseRail(doc.as<JsonArrayConst>(), JsonArrayConst(), rows, 1, count, 1806192001, "123") && count == 1);
    assert(!strcmp(rows[0].platform, "--"));
    assert(!ParseRail(JsonArrayConst(), JsonArrayConst(), rows, 1, count, 0, "123"));
}
'''
with tempfile.TemporaryDirectory() as directory:
    cpp = Path(directory) / "rail.cpp"
    executable = Path(directory) / "rail"
    cpp.write_text(harness)
    subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-I", str(root), "-I", str(library),
                    str(cpp), str(root / "rail_parse.cpp"), str(root / "transitous_parse.cpp"), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
print("Rail parsing and prediction tests passed")
