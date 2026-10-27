#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise page transitions and interrupted display recovery."""
from pathlib import Path
import subprocess
import tempfile

source = (Path(__file__).resolve().parent.parent / "LilyGo-EPD-4-7-OWM-Weather-Display.ino").read_text()

def function(signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]

harness = r'''
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <map>
#include <string>
using String = std::string;
std::map<std::string, uint64_t> values;
int cause = 0, fetches = 0, restores = 0, trams = 0, weather = 0, errors = 0, boards = 0;
bool saveOk = true, restoreOk = true, displayOk = true, fetchOk = true;
bool AwakeTramButtonPressed = false;
time_t now = 1700000000;
uint8_t pixels[1], *framebuffer = pixels;
#define EPDPARTIAL 1
#define ESP_SLEEP_WAKEUP_EXT0 2
#define WL_CONNECTED 1
int esp_sleep_get_wakeup_cause() { return cause; }
time_t mockTime(time_t *) { return now; }
#define time mockTime
struct Preferences {
    bool begin(const char *, bool) { return true; }
    bool getBool(const char *k, bool d) { return values.count(k) ? values[k] : d; }
    uint64_t getULong64(const char *k, uint64_t d) { return values.count(k) ? values[k] : d; }
    size_t putBool(const char *k, bool v) { values[k] = v; return 1; }
    size_t putULong64(const char *k, uint64_t v) { values[k] = v; return sizeof(v); }
    void end() {}
};
struct { void println(const char*) {} template<class... T> void printf(const char*, T...) {} } Serial;
bool SaveWeatherImage() { return saveOk; }
bool LoadWeatherImage(uint8_t*) { ++restores; return restoreOk; }
bool UpdatePartialDisplay(uint8_t*, bool) { return displayOk; }
bool DisplayLoading(const char *s) { if (std::string(s).find("trams") != std::string::npos) ++trams; else if (std::string(s).find("unavailable") != std::string::npos) ++errors; else ++weather; return displayOk; }
int StartWiFi() { return WL_CONNECTED; }
bool SetupTime() { return true; }
void StopWiFi() {}
struct TramDeparture {};
void *ps_malloc(size_t size) { return malloc(size); }
bool FetchTransitous(TramDeparture*, size_t capacity, size_t &count) { assert(capacity == 32); ++fetches; count = 0; return fetchOk; }
void DisplayTramBoard(const TramDeparture*, size_t, time_t, bool) { ++boards; }
'''
harness += function("bool RestoreWeatherPage(") + "\n" + function("bool RunTramPage()")
harness += r'''
int main() {
    cause = ESP_SLEEP_WAKEUP_EXT0;
    RunTramPage();
    assert(trams == 1 && fetches == 1 && boards == 1 && values["active"] && !values["dirty"]);
    assert(values["end"] == (uint64_t)now + 300 && values["next"] == (uint64_t)now + 60);
    cause = 0; now += 30; RunTramPage(); assert(fetches == 1);
    cause = ESP_SLEEP_WAKEUP_EXT0; RunTramPage();
    assert(weather == 1 && restores == 1 && !values["active"] && !values["dirty"] && values["weather_wake"]);
    cause = 0; assert(!RunTramPage() && !values["weather_wake"]);
    values["active"] = 0; values["dirty"] = 1; restoreOk = false; cause = 0;
    RunTramPage(); assert(values["dirty"] && fetches == 1);
    restoreOk = true; RunTramPage(); assert(!values["dirty"] && !values["active"]);
    values["active"] = 1; values["dirty"] = 1; values["end"] = 0; values["next"] = 0;
    RunTramPage(); assert(fetches == 2 && !values["dirty"] && values["end"] == (uint64_t)now + 300);
    now += 61; displayOk = false; RunTramPage(); assert(fetches == 3 && values["dirty"]);
    displayOk = true; now += 61; RunTramPage(); assert(fetches == 4 && !values["dirty"]);
    now += 180; RunTramPage(); assert(!values["active"] && !values["dirty"]);
    saveOk = false; restoreOk = true; cause = 0; AwakeTramButtonPressed = true; RunTramPage();
    assert(values["active"] && fetches == 5 && trams == 2 && !AwakeTramButtonPressed);
    cause = ESP_SLEEP_WAKEUP_EXT0; RunTramPage(); assert(!values["active"]);
    restoreOk = false; RunTramPage();
    assert(!values["active"] && values["dirty"] && errors == 1 && fetches == 5);
    restoreOk = true; saveOk = true; RunTramPage();
    assert(values["active"] && !values["dirty"] && fetches == 6 && trams == 3);
}
'''
with tempfile.TemporaryDirectory() as directory:
    cpp = Path(directory) / "state.cpp"
    exe = Path(directory) / "state"
    cpp.write_text(harness)
    subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", str(cpp), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print("Tram state recovery tests passed")
