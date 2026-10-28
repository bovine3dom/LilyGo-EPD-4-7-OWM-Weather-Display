#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check weather sources and request deduplication in both build modes."""
from pathlib import Path
import subprocess
import tempfile

source = (Path(__file__).resolve().parent.parent / "LilyGo-EPD-4-7-OWM-Weather-Display.ino").read_text()
start = source.index("void setup()")
opening = source.index("{", start)
depth, end = 1, opening + 1
while depth:
    depth += (source[end] == "{") - (source[end] == "}")
    end += 1
setup = source[start:end]
harness = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <ctime>
using byte = unsigned char;
#define EPDPARTIAL 1
#define T5_47_PLUS_V2 1
#define WL_CONNECTED 1
const int ESP_SLEEP_WAKEUP_TIMER = 1;
int wakeCause = ESP_SLEEP_WAKEUP_TIMER;
int esp_sleep_get_wakeup_cause() { return wakeCause; }
time_t now = 1700000000;
time_t mockTime(time_t*) { return now; }
#define time mockTime
int fakeCalls = 0, apiCalls = 0, renders = 0, wifiCalls = 0, sleeps = 0, successes = 0, warnings = 0, snorkelCalls = 0;
bool tramActive = false, displayOk = true, clockAvailable = true, wifiOk = true, snorkelOk = true;
char SNORKEL_URL[32] = {};
constexpr int max_readings = 24;
struct Forecast { time_t Dt = 0; } WxForecast[max_readings];
struct SnorkelData {} Snorkel;
uint64_t lastSlot = 0;
float BatteryVoltage;
int UpdateIntervalHours, CurrentHour = 12, CurrentMin = 0;
uint8_t *framebuffer = nullptr;
const int WakeupHour = 8;
struct SerialMock { void println(const char*) {} template<class... T> void printf(const char*, T...) {} } Serial;
struct WiFiClient {};
void InitialiseSystem() {}
bool LoadConfiguration() { return true; }
bool RunTramPage() { return tramActive; }
void BeginSleep() { ++sleeps; }
float ReadBatteryVoltage() { return 4.2; }
int BatteryUpdateInterval(float) { return 1; }
bool UpdateLocalTime() { return clockAvailable; }
int StartWiFi() { ++wifiCalls; return wifiOk ? WL_CONNECTED : 0; }
bool SetupTime() { return wifiOk; }
bool LoadDisplayedImage(uint8_t*) { return true; }
void DrawWiFiStatus() { ++warnings; }
void UpdateBatteryLifeEstimate(time_t) {}
bool IsScheduledUpdate() { return true; }
bool ClaimWeatherAttempt(time_t value) { uint64_t slot = value / 3600; if (slot == lastSlot) return false; lastSlot = slot; return true; }
void MarkWeatherSuccess(time_t) { ++successes; }
void LoadFakeWeather(time_t) { ++fakeCalls; }
bool obtainWeatherData(WiFiClient &, const char*) { ++apiCalls; return true; }
bool FetchSnorkel(SnorkelData&) { ++snorkelCalls; return snorkelOk; }
void FetchWeatherParallel(bool &current, bool &forecast, SnorkelData *snorkel) {
    apiCalls += 2; current = forecast = true;
    if (snorkel) FetchSnorkel(*snorkel);
}
void StopWiFi() {}
void DisplayWeather() { ++renders; }
bool UpdatePartialDisplay(uint8_t*, bool) { return displayOk; }
'''
harness += setup
harness += r'''
int main() {
    setup();
#if DEBUG_FAKE_WEATHER
    assert(fakeCalls == 1 && apiCalls == 0 && wifiCalls == 0);
#else
    assert(fakeCalls == 0 && apiCalls == 2 && wifiCalls == 1);
#endif
    assert(renders == 1 && sleeps == 1 && successes == 1);
    now += 30; setup();
    assert(renders == 1 && sleeps == 2);
#if DEBUG_FAKE_WEATHER
    assert(fakeCalls == 1 && apiCalls == 0);
#else
    assert(apiCalls == 2);
#endif
    now += 3600; displayOk = false; setup();
    assert(successes == 1);
#if DEBUG_FAKE_WEATHER
    assert(fakeCalls == 2 && apiCalls == 0);
#else
    assert(apiCalls == 4);
#endif
    tramActive = true; now += 3600; setup();
#if DEBUG_FAKE_WEATHER
    assert(fakeCalls == 2 && apiCalls == 0);
#else
    assert(apiCalls == 4);
#endif
    tramActive = false; clockAvailable = false; wifiOk = false; setup();
    assert(warnings == 1);
#if DEBUG_FAKE_WEATHER && DEBUG_SERIAL_WAKE_SECONDS > 0
    clockAvailable = wifiOk = displayOk = true; wakeCause = 0;
    int before = renders; setup();
    assert(renders == before + 1 && apiCalls == 0);
#endif
    strcpy(SNORKEL_URL, "http://test");
    clockAvailable = wifiOk = displayOk = true; wakeCause = ESP_SLEEP_WAKEUP_TIMER;
    now += 3600; int beforeRender = renders; setup();
    assert(snorkelCalls == 1 && renders == beforeRender + 1);
    now += 30; setup(); assert(snorkelCalls == 1);
    snorkelOk = false; now += 3600; setup();
    assert(snorkelCalls == 2 && renders == beforeRender + 2);
}
'''
with tempfile.TemporaryDirectory() as directory:
    cpp = Path(directory) / "modes.cpp"
    cpp.write_text(harness)
    for fake in (0, 1):
        for wake in (0, 30):
            executable = Path(directory) / f"mode{fake}_{wake}"
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", f"-DDEBUG_FAKE_WEATHER={fake}",
                            f"-DDEBUG_SERIAL_WAKE_SECONDS={wake}", str(cpp), "-o", str(executable)], check=True)
            subprocess.run([str(executable)], check=True)
print("Weather mode and request deduplication tests passed")
