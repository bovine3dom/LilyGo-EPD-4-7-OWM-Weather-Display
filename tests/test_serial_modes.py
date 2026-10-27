#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check the reset serial window in normal and debug builds."""
from pathlib import Path
import subprocess
import tempfile

source = (Path(__file__).resolve().parent.parent / "LilyGo-EPD-4-7-OWM-Weather-Display.ino").read_text()
start = source.index("void InitialiseSystem()")
opening = source.index("{", start)
depth, end = 1, opening + 1
while depth:
    depth += (source[end] == "{") - (source[end] == "}")
    end += 1
initialise = source[start:end]
start = source.index("bool PollTramButton()")
end = source.index("\n#if T5_47_PLUS_V2\nvoid WaitForRailButtonRelease()", start)
poll = source[start:end]
start = source.index("void WaitForRailButtonRelease()")
end = source.index("\n#endif", start)
release = source[start:end]
harness = r'''
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <climits>
#define EPDPARTIAL 1
#define T5_47_PLUS_V2 1
#define INPUT_PULLUP 1
#define ESP_SLEEP_WAKEUP_EXT0 2
#define ESP_SLEEP_WAKEUP_EXT1 3
#define ESP_SLEEP_WAKEUP_TIMER 1
#define ESP_EXT1_WAKEUP_ALL_LOW 0
#define GPIO_NUM_0 0
#define GPIO_NUM_21 21
#define EPD_WIDTH 8
#define EPD_HEIGHT 8
using String = std::string;
unsigned long ticks = 0, StartTime = 0, pressedAt = ULONG_MAX, railPressedAt = ULONG_MAX;
bool AwakeTramButtonPressed = false, AwakeRailButtonPressed = false, bootHeld = false;
uint64_t ButtonWakePins = 0;
int cause = 0;
int digitalRead(int pin) { return pin == 0 ? !(bootHeld || (ticks >= railPressedAt && ticks - railPressedAt < 100)) : ticks < pressedAt; }
void pinMode(int, int) {}
void rtc_gpio_deinit(int) {}
void esp_sleep_enable_ext1_wakeup(uint64_t, int) {}
void esp_sleep_enable_timer_wakeup(uint64_t) {}
void esp_deep_sleep_start() { throw 1; }
uint64_t esp_sleep_get_ext1_wakeup_status() { return 1ULL << 21; }
struct Preferences {
    bool begin(const char*, bool) { return true; }
    void putULong64(const char*, uint64_t) {}
    uint64_t getULong64(const char*, uint64_t) { return 0; }
    void end() {}
};
uint8_t *framebuffer = nullptr;
unsigned long millis() { return ticks; }
void delay(unsigned long value) { ticks += value; }
int esp_sleep_get_wakeup_cause() { return cause; }
int esp_reset_reason() { return 0; }
void *ps_calloc(size_t n, size_t size) { return calloc(n, size); }
struct SerialMock {
    void begin(int) {}
    explicit operator bool() const { return false; }
    void println(const String&) {}
    template<class... T> void printf(const char*, T...) {}
} Serial;
'''
harness += poll + "\n" + release + "\n" + initialise
harness += r'''
int main() {
    cause = 0; ticks = 0; InitialiseSystem(); assert(ticks == 11000); free(framebuffer);
    cause = 0; ticks = 0; pressedAt = 3000; AwakeTramButtonPressed = false; InitialiseSystem();
    assert(ticks == 3000 && AwakeTramButtonPressed); free(framebuffer);
    pressedAt = ULONG_MAX; AwakeTramButtonPressed = false;
    cause = ESP_SLEEP_WAKEUP_EXT0; ticks = 0; InitialiseSystem(); assert(ticks == 0); free(framebuffer);
    cause = ESP_SLEEP_WAKEUP_EXT1; ticks = 0; InitialiseSystem(); assert(ticks == 0); free(framebuffer);
    ticks = 0; bootHeld = true;
    try { InitialiseSystem(); assert(false); } catch (int) { assert(ticks == 5000); }
    bootHeld = false; AwakeRailButtonPressed = false;
#if DEBUG_SERIAL_WAKE_SECONDS > 0
    cause = ESP_SLEEP_WAKEUP_TIMER; ticks = 0; railPressedAt = 3000; InitialiseSystem();
    assert(ticks == 3100 && AwakeRailButtonPressed); free(framebuffer);
    railPressedAt = ULONG_MAX; AwakeRailButtonPressed = false;
#endif
    cause = ESP_SLEEP_WAKEUP_TIMER; ticks = 0; InitialiseSystem();
#if DEBUG_SERIAL_WAKE_SECONDS > 0
    assert(ticks == 6000);
#else
    assert(ticks == 0);
#endif
    free(framebuffer);
}
'''
with tempfile.TemporaryDirectory() as directory:
    cpp = Path(directory) / "serial.cpp"
    cpp.write_text(harness)
    for wake in (0, 30):
        executable = Path(directory) / f"serial{wake}"
        subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-DDEBUG_FAKE_WEATHER=0",
                        f"-DDEBUG_SERIAL_WAKE_SECONDS={wake}", str(cpp), "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)
print("Normal and debug serial window tests passed")
