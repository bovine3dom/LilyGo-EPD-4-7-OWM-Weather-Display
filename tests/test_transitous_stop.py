#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check stop selection, location validation, and stop-ID encoding."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parent.parent
library = root / ".pio/libdeps/T5_4_7Inc_Plus_V2/ArduinoJson/src"
harness = r'''
#include "transitous.h"
#include <cassert>
#include <cstring>
#include <string>
int main() {
    const char *stops = R"JSON([
      {"name":"Sample Stop","stopId":"outside","lat":48.85,"lon":2.35,"modes":["TRAM"]},
      {"name":"Sample Stop","stopId":"bus-only","lat":51.5000,"lon":-0.1200,"modes":["BUS"]},
      {"name":"Other","stopId":"wrong-name","lat":51.5000,"lon":-0.1200,"modes":["TRAM"]},
      {"name":"Sample Stop","stopId":"farther","lat":51.503,"lon":-0.117,"modes":["TRAM"]},
      {"name":"Sample Stop","stopId":"feed:stop/a b","lat":51.5000,"lon":-0.1200,"modes":["BUS","TRAM"]}
    ])JSON";
    char id[128], encoded[384];
    const TramStop target = {"Sample Stop", 51.5, -0.12};
    assert(FindTramStop(stops, strlen(stops), id, sizeof(id), target) && !strcmp(id, "feed:stop/a b"));
    assert(EncodeTransitousId(id, encoded, sizeof(encoded)) && !strcmp(encoded, "feed%3Astop%2Fa%20b"));
    assert(!EncodeTransitousId(id, encoded, 4));
    assert(!EncodeTransitousId("", encoded, sizeof(encoded)));
    const char *missing = R"JSON([{"name":"Sample Stop","stopId":"x","lat":51.5000,"lon":-0.1200,"modes":["BUS"]}])JSON";
    assert(!FindTramStop(missing, strlen(missing), id, sizeof(id), target));
    assert(!FindTramStop(stops, strlen(stops), id, 1, target));
    const char *correct = R"JSON({"place":{"name":"Sample Stop","lat":51.5000,"lon":-0.1200},"stopTimes":[]})JSON";
    const char *wrong = R"JSON({"place":{"name":"Sample Stop","lat":48.85,"lon":2.35},"stopTimes":[]})JSON";
    assert(IsTramStopResponse(correct, strlen(correct), target));
    assert(!IsTramStopResponse(wrong, strlen(wrong), target));
    assert(!IsTramStopResponse("{}", 2, target));
    assert(!FindTramStop("{", 1, id, sizeof(id), target));
}
'''
with tempfile.TemporaryDirectory() as directory:
    cpp = Path(directory) / "stop.cpp"
    executable = Path(directory) / "stop"
    cpp.write_text(harness)
    subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-I", str(root), "-I", str(library),
                    str(cpp), str(root / "transitous_parse.cpp"), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
print("Transitous stop selection tests passed")
