#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check source timestamps, missing values and independent data failures."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parent.parent
library = root / ".pio/libdeps/T5_4_7Inc_Plus_V2/ArduinoJson/src"
harness = r'''
#include "snorkel.h"
#include <ArduinoJson.h>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

bool parse(DynamicJsonDocument &doc, SnorkelData &data) {
    std::string json;
    serializeJson(doc, json);
    std::vector<char> buffer(json.begin(), json.end());
    return ParseSnorkel(buffer.data(), buffer.size(), data);
}
int main() {
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1); tzset();
    tm start = {}; start.tm_year = 127; start.tm_mon = 2; start.tm_mday = 27; start.tm_isdst = -1;
    time_t base = mktime(&start);
    DynamicJsonDocument doc(16384);
    JsonArray forecast = doc.createNestedArray("forecast");
    for (size_t i = 0; i < 24; ++i) {
        time_t timestamp = base + i * 3 * 3600;
        tm local; localtime_r(&timestamp, &local);
        char text[20]; strftime(text, sizeof(text), "%Y-%m-%dT%H:%M:%S", &local);
        JsonObject row = forecast.createNestedObject();
        row["time"] = text; row["wave_m"] = (i + 1) / 10.0f; row["water_c"] = 17.0f + i / 10.0f;
    }
    for (const char *timestamp : {"2027-03-28T02:30:00", "2027-10-31T02:30:00"}) {
        JsonObject row = forecast.createNestedObject(); row["time"] = timestamp; row["wave_m"] = 1; row["water_c"] = 18;
    }
    JsonArray jelly = doc.createNestedArray("jellies");
    for (int i = 0; i < 7; ++i) {
        JsonObject row = jelly.createNestedObject(); row["end"] = base - (6 - i) * 86400;
        row["nice"] = i + 1; row["vf"] = 7 - i;
    }
    // The source is Paris local time even if the device uses a different timezone.
    setenv("TZ", "UTC0", 1); tzset();
    SnorkelData data;
    assert(parse(doc, data) && data.wavesAvailable && data.waterAvailable && data.jelliesAvailable);
    assert(data.forecastCount == 24 && data.jellyCount == 7);
    for (size_t i = 0; i < 24; ++i) assert(data.forecastTimes[i] == base + static_cast<time_t>(i) * 3 * 3600);
    assert(std::fabs(data.waves[0] - 0.1f) < 0.001f && std::fabs(data.water[23] - 19.3f) < 0.001f);
    assert(data.jellyTimes[0] == base - 6 * 86400 && data.jellyTimes[6] == base);
    assert(data.nice[0] == 1 && data.nice[1] == 2 && data.vf[6] == 1);
    jelly[1]["vf"] = nullptr;
    assert(parse(doc, data) && data.jelliesAvailable && std::isnan(data.vf[1]));
    jelly[1]["vf"] = 401;
    assert(parse(doc, data) && std::isnan(data.vf[1]));
    forecast[5]["wave_m"] = nullptr;
    assert(parse(doc, data) && data.wavesAvailable && data.waterAvailable && std::isnan(data.waves[5]));
    char bad[] = "{";
    assert(!ParseSnorkel(bad, 1, data) && !data.wavesAvailable && !data.jelliesAvailable);
}
'''
with tempfile.TemporaryDirectory() as directory:
    cpp = Path(directory) / "snorkel-test.cpp"
    exe = Path(directory) / "snorkel-test"
    cpp.write_text(harness)
    subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-I", str(root), "-I", str(library),
                    str(cpp), str(root / "snorkel_parse.cpp"), str(root / "transitous_parse.cpp"),
                    "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print("Snorkel source-time and missing-data tests passed")
