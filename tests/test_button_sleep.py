#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check production and debug button wake configuration."""
from pathlib import Path
import subprocess
import tempfile

source = (Path(__file__).resolve().parent.parent / "LilyGo-EPD-4-7-OWM-Weather-Display.ino").read_text()
start = source.index("void BeginSleep()")
opening = source.index("{", start)
depth, end = 1, opening + 1
while depth:
    depth += (source[end] == "{") - (source[end] == "}")
    end += 1
function = source[start:end]
harness = r'''
#include <cassert>
#include <cstdint>
#include <ctime>
#include <string>
#include <algorithm>
using std::min;
#define EPDPARTIAL 1
#define T5_47_PLUS_V2 1
#define INPUT_PULLUP 1
#define GPIO_NUM_0 0
#define GPIO_NUM_21 21
#define ESP_EXT1_WAKEUP_ALL_LOW 0
struct String : std::string {
    String(unsigned long) {}
    String(double, int) {}
};
struct { void println(const std::string&) {} } Serial;
unsigned long ticks = 0, StartTime = 0;
long SleepTimer = 0, RetrySleepSeconds = 3600;
bool pin0 = true, pin21 = true, poweredOff = false;
uint64_t wakeMask = 0, sleepUs = 0;
int wakeMode = -1;
void PowerOffPartialDisplay() { poweredOff = true; }
void pinMode(int pin, int) { if (pin == 0) assert(poweredOff); }
int digitalRead(int pin) { return pin == 0 ? pin0 : pin21; }
unsigned long millis() { return ticks; }
void delay(unsigned long value) { ticks += value; }
void rtc_gpio_pullup_en(int) {}
void rtc_gpio_pulldown_dis(int) {}
void esp_sleep_enable_ext1_wakeup(uint64_t pins, int mode) { wakeMask = pins; wakeMode = mode; }
void esp_sleep_enable_timer_wakeup(uint64_t us) { sleepUs = us; }
void esp_deep_sleep_start() { throw 1; }
long SecondsUntilNextUpdate(time_t) { return 3600; }
long NextSleepSeconds(time_t, long seconds, bool, bool, bool, time_t, time_t, long debug) {
    return debug ? min(seconds, debug) : seconds;
}
long WeatherRetrySeconds(time_t) { return 0; }
struct Preferences {
    bool begin(const char*, bool) { return true; }
    bool getBool(const char*, bool d) { return d; }
    uint64_t getULong64(const char*, uint64_t d) { return d; }
    void end() {}
};
'''
harness += function
harness += r'''
void run(bool boot, bool tram, uint64_t mask, long expected) {
    pin0 = boot; pin21 = tram; wakeMask = sleepUs = 0; wakeMode = -1; ticks = 0; poweredOff = false;
    try { BeginSleep(); assert(false); } catch (int) {}
    assert(wakeMask == mask && (mask == 0 || wakeMode == ESP_EXT1_WAKEUP_ALL_LOW));
    assert(sleepUs == (uint64_t)expected * 1000000);
    assert(ticks == (boot && tram ? 0 : 5000));
}
int main() {
    const long normal = DEBUG_SERIAL_WAKE_SECONDS ? DEBUG_SERIAL_WAKE_SECONDS : 3600;
    const long held = DEBUG_SERIAL_WAKE_SECONDS ? DEBUG_SERIAL_WAKE_SECONDS : 60;
    run(true, true, (1ULL << 21) | 1, normal);
    run(false, true, 1ULL << 21, held);
    run(true, false, 1, held);
    run(false, false, 0, held);
}
'''
with tempfile.TemporaryDirectory() as directory:
    cpp = Path(directory) / "sleep.cpp"
    cpp.write_text(harness)
    for wake in (0, 30):
        exe = Path(directory) / f"sleep{wake}"
        subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", f"-DDEBUG_SERIAL_WAKE_SECONDS={wake}",
                        str(cpp), "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)
print("Button sleep configuration tests passed")
