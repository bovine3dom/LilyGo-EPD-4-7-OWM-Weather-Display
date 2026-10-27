#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check Transitous parsing, time zones, cancellations, and missing real-time data."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parent.parent
library = root / ".pio/libdeps/T5_4_7Inc_Plus_V2/ArduinoJson/src"
harness = r'''
#include "transitous.h"
#include <cassert>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <string>
int main() {
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1); tzset();
    std::string data = R"JSON({"stopTimes":[
        {"routeShortName":"L2","headsign":"Airport Terminal","realTime":true,"place":{"scheduledDeparture":"2027-03-28T00:59:00Z","departure":"2027-03-28T01:02:00Z","departureTimestampReason":"PREDICTED"}},
        {"routeShortName":"L1","headsign":"North Terminus","realTime":true,"place":{"scheduledDeparture":"2027-03-28T01:04:00Z","departure":"2027-03-28T01:04:00Z","departureTimestampReason":"NO_RT_DATA"}},
        {"routeShortName":"L3","headsign":"Harbour","cancelled":true,"place":{"scheduledDeparture":"2027-10-31T01:30:00Z"}},
        {"routeShortName":"L1","headsign":"Hospital","place":{"scheduledDeparture":"2027-01-01T12:30:00+01:00"}},
        {"routeShortName":"L1","headsign":"Invalid","place":{"scheduledDeparture":"invalid"}}
    ]})JSON";
    TramDeparture rows[8] = {}; size_t count = 0;
    assert(ParseTransitous(data.data(), data.size(), rows, 8, count, 0) && count == 3);
    assert(!strcmp(rows[0].scheduled, "12:30") && !strcmp(rows[0].expected, "--:--"));
    assert(!strcmp(rows[1].line, "L2") && !strcmp(rows[1].scheduled, "01:59") && !strcmp(rows[1].expected, "03:02"));
    assert(!strcmp(rows[2].scheduled, "03:04") && !strcmp(rows[2].expected, "--:--"));
    tm utc = {}; utc.tm_year = 2027 - 1900; utc.tm_mon = 0; utc.tm_mday = 1; utc.tm_hour = 12; utc.tm_min = 15;
    time_t cutoff = timegm(&utc);
    data = R"JSON({"stopTimes":[
        {"routeShortName":"Z","headsign":"Late in the list","place":{"scheduledDeparture":"2027-01-01T13:00:00Z"}},
        {"routeShortName":"A","headsign":"Delayed","realTime":true,"place":{"scheduledDeparture":"2027-01-01T12:10:00Z","departure":"2027-01-01T12:16:00Z","departureTimestampReason":"PREDICTED"}},
        {"routeShortName":"B","headsign":"Exactly at cutoff","realTime":true,"place":{"scheduledDeparture":"2027-01-01T12:20:00Z","departure":"2027-01-01T12:15:00Z","departureTimestampReason":"PREDICTED"}},
        {"routeShortName":"C","headsign":"Scheduled only","place":{"scheduledDeparture":"2027-01-01T12:16:00Z"}},
        {"routeShortName":"D","headsign":"Scheduled exactly","place":{"scheduledDeparture":"2027-01-01T12:15:00Z"}},
        {"routeShortName":"E","headsign":"Ignore inconsistent estimate","realTime":true,"place":{"scheduledDeparture":"2027-01-01T12:40:00Z","departure":"2027-01-01T12:01:00Z","departureTimestampReason":"INCONSISTENT_RT"}},
        {"routeShortName":"F","headsign":"Cancelled","tripCancelled":true,"place":{"scheduledDeparture":"2027-01-01T12:30:00Z"}},
        {"routeShortName":"G","headsign":"Invalid estimate","realTime":true,"place":{"scheduledDeparture":"2027-01-01T12:20:00Z","departure":"invalid","departureTimestampReason":"PREDICTED"}},
        {"routeShortName":"H","headsign":"No scheduled time","realTime":true,"place":{"departure":"2027-01-01T12:21:00Z","departureTimestampReason":"PREDICTED"}},
        {"routeShortName":"I","headsign":"Early tram","realTime":true,"place":{"scheduledDeparture":"2027-01-01T12:45:00Z","departure":"2027-01-01T12:17:00Z","departureTimestampReason":"PREDICTED"}}
    ]})JSON";
    std::string limited = data, zero = data;
    assert(ParseTransitous(data.data(), data.size(), rows, 8, count, cutoff) && count == 7);
    assert(!strcmp(rows[0].line, "A") && !strcmp(rows[0].expected, "13:16"));
    assert(!strcmp(rows[1].line, "C") && !strcmp(rows[1].expected, "--:--"));
    assert(!strcmp(rows[2].line, "I") && !strcmp(rows[2].expected, "13:17"));
    assert(!strcmp(rows[3].line, "G") && !strcmp(rows[3].expected, "--:--"));
    assert(!strcmp(rows[4].line, "H") && !strcmp(rows[4].scheduled, "--:--") && !strcmp(rows[4].expected, "13:21"));
    assert(!strcmp(rows[5].line, "E") && !strcmp(rows[5].expected, "--:--"));
    assert(!strcmp(rows[6].line, "Z"));
    assert(ParseTransitous(limited.data(), limited.size(), rows, 3, count, cutoff) && count == 3);
    assert(!strcmp(rows[0].line, "A") && !strcmp(rows[1].line, "C") && !strcmp(rows[2].line, "I"));
    assert(ParseTransitous(zero.data(), zero.size(), nullptr, 0, count, cutoff) && count == 0);
    data = R"JSON({"stopTimes":[]})JSON";
    assert(ParseTransitous(data.data(), data.size(), rows, 8, count, cutoff) && count == 0);
    data = R"JSON({"error":"bad"})JSON";
    assert(!ParseTransitous(data.data(), data.size(), rows, 8, count, cutoff));
    data = "{";
    assert(!ParseTransitous(data.data(), data.size(), rows, 8, count, cutoff));
}
'''
with tempfile.TemporaryDirectory() as directory:
    cpp = Path(directory) / "test.cpp"
    executable = Path(directory) / "test"
    cpp.write_text(harness)
    subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-I", str(root), "-I", str(library),
                    str(cpp), str(root / "transitous_parse.cpp"), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
print("Transitous parsing tests passed")
