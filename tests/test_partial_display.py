#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise bitmap conversion and refresh-state recovery with host mocks."""

from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parent.parent
headers = {
    "Arduino.h": r'''
#pragma once
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <initializer_list>
struct SerialMock { template<class... T> void printf(const char*, T...) {} void println(const char*) {} };
extern SerialMock Serial;
inline void *ps_malloc(size_t n) { return malloc(n); }
template<class T> T min(T a, T b) { return a < b ? a : b; }
#define OUTPUT 1
inline void pinMode(int, int) {}
''',
    "Preferences.h": r'''
#pragma once
#include <map>
#include <string>
#include <cstdint>
extern std::map<std::string, uint32_t> values;
extern bool fail_invalidate;
struct Preferences {
    bool begin(const char*, bool) { return true; }
    bool getBool(const char *k, bool d) { return values.count(k) ? values[k] : d; }
    uint32_t getUInt(const char *k, uint32_t d) { return values.count(k) ? values[k] : d; }
    size_t putBool(const char *k, bool v) { if (!v && fail_invalidate) return 0; values[k] = v; return 1; }
    size_t putUInt(const char *k, uint32_t v) { values[k] = v; return 4; }
    void end() {}
};
''',
    "SPIFFS.h": r'''
#pragma once
#include <vector>
#include <cstdint>
#include <cstring>
extern std::vector<uint8_t> stored, weatherStored;
extern bool fail_write, fail_remove, fail_seek;
extern size_t bytes_written;
extern int writes_before_failure;
struct File {
    std::vector<uint8_t> *data = nullptr;
    size_t pos = 0;
    explicit operator bool() const { return data != nullptr; }
    size_t size() { return data->size(); }
    size_t read(uint8_t *p, size_t n) { if (data->size() - pos < n) return 0; memcpy(p, data->data() + pos, n); pos += n; return n; }
    bool seek(size_t p) { if (fail_seek || p > data->size()) return false; pos = p; return true; }
    size_t write(const uint8_t *p, size_t n) {
        if (fail_write || writes_before_failure == 0) return 0;
        if (writes_before_failure > 0) --writes_before_failure;
        if (data->size() < pos + n) data->resize(pos + n);
        memcpy(data->data() + pos, p, n); pos += n; bytes_written += n; return n;
    }
    void close() {}
    void flush() {}
};
struct FS {
    bool exists(const char*) { return !stored.empty(); }
    bool remove(const char*) { if (fail_remove) return false; stored.clear(); return true; }
    File open(const char *path, const char *mode) {
        auto &data = strstr(path, "weather") ? weatherStored : stored;
        if (*mode == 'r' && data.empty()) return {};
        if (*mode == 'w') data.clear();
        return {&data};
    }
};
extern FS SPIFFS;
''',
    "esp_sleep.h": r'''
#pragma once
#define ESP_SLEEP_WAKEUP_TIMER 1
#define ESP_SLEEP_WAKEUP_EXT0 2
#define ESP_SLEEP_WAKEUP_EXT1 3
extern int cause;
inline int esp_sleep_get_wakeup_cause() { return cause; }
''',
    "EPD_Painter_1bit.h": r'''
#pragma once
#include <cstdint>
extern int fulls, paints, adopts, begins, ends;
struct Shift { int data, clk, strobe; };
struct Preset { Shift shift; };
static Preset EPD1B_PRESET;
struct EPD_Painter1bit {
    EPD_Painter1bit(Preset) {}
    void setAutoShutdown(bool) {}
    void setIdleTimeout(int) {}
    bool begin() { ++begins; return true; }
    void clear() { ++fulls; }
    void adoptScreen(const uint8_t*) { ++adopts; }
    void paint(const uint8_t*) { ++paints; }
    void end() { ++ends; }
};
''',
    "epd1b_power.h": r'''
#pragma once
#include "EPD_Painter_1bit.h"
struct epd1b_powerctl_H716 { void begin(Shift) {} void powerOn() {} void powerOff() {} };
''',
    "epd_driver.h": "#pragma once\ninline void epd_poweroff_all() {}\n",
    "esp_rom_gpio.h": "#pragma once\ninline void esp_rom_gpio_connect_out_signal(int,int,bool,bool) {}\n",
    "soc/gpio_sig_map.h": "#pragma once\n#define SIG_GPIO_OUT_IDX 256\n",
}

harness = r'''
#include <cassert>
#include <vector>
#include <map>
#include <string>
#include "Arduino.h"
#include "SPIFFS.h"
SerialMock Serial; FS SPIFFS;
std::vector<uint8_t> stored, weatherStored;
std::map<std::string, uint32_t> values;
bool fail_invalidate = false, fail_write = false, fail_remove = false, fail_seek = false;
size_t bytes_written = 0;
int writes_before_failure = -1;
int cause = 0, fulls = 0, paints = 0, adopts = 0, begins = 0, ends = 0;
#include "SOURCE"
int main() {
    std::vector<uint8_t> gray(Width * Height / 2, 0), bitmap(BitmapSize);
    makeBitmap(gray.data(), bitmap.data());
    for (auto b : bitmap) assert(b == 255);
    std::fill(gray.begin(), gray.end(), 255);
    makeBitmap(gray.data(), bitmap.data());
    for (auto b : bitmap) assert(b == 0);
    std::fill(gray.begin(), gray.end(), 0x88);
    makeBitmap(gray.data(), bitmap.data());
    int bits = 0; for (auto b : bitmap) bits += __builtin_popcount(b);
    assert(bits == Width * Height / 2);
    std::fill(gray.begin(), gray.end(), 255);
    assert(UpdatePartialDisplay(gray.data(), false) && fulls == 1 && values["valid"] == 1);
    assert(SaveWeatherImage());
    std::vector<uint8_t> restored(gray.size(), 0);
    assert(LoadWeatherImage(restored.data()) && restored == gray);
    weatherStored[0] = 0x80;
    assert(!LoadWeatherImage(restored.data()));
    values["hash"] = checksum(weatherStored.data());
    assert(LoadWeatherImage(restored.data()) && restored[0] == 0xF0);
    weatherStored[0] = 0;
    values["hash"] = checksum(weatherStored.data());
    cause = ESP_SLEEP_WAKEUP_TIMER;
    assert(UpdatePartialDisplay(gray.data(), false) && paints == 1);
    for (int i = 0; i < 5; ++i) {
        gray[i] = 0; bytes_written = 0;
        assert(UpdatePartialDisplay(gray.data(), false) && bytes_written == 512);
        makeBitmap(gray.data(), bitmap.data()); assert(stored == bitmap);
    }
    assert(adopts == 5 && values["partials"] == 5 && fulls == 1);
    gray[6] = 0; assert(UpdatePartialDisplay(gray.data(), false) && fulls == 2);
    assert(UpdatePartialDisplay(gray.data(), true) && fulls == 3);
    --values["full_day"]; assert(UpdatePartialDisplay(gray.data(), false) && fulls == 4);
    stored[5] ^= 1; gray[7] = 0;
    assert(UpdatePartialDisplay(gray.data(), false) && fulls == 5);
    fail_write = true; gray[8] = 0;
    assert(!UpdatePartialDisplay(gray.data(), false) && values["valid"] == 0);
    fail_write = false; gray[9] = 0;
    assert(UpdatePartialDisplay(gray.data(), false) && fulls == 6);
    fail_remove = true; std::fill(gray.begin() + 10000, gray.begin() + 85000, 0);
    assert(!UpdatePartialDisplay(gray.data(), false) && values["valid"] == 0);
    fail_remove = false; gray[11] = 0;
    assert(UpdatePartialDisplay(gray.data(), false) && fulls == 7);
    assert(begins == 1 && ends == 0);
    updatedThisWake = false; cause = ESP_SLEEP_WAKEUP_EXT1;
    gray[12] = 0; assert(UpdatePartialDisplay(gray.data(), false) && fulls == 7);
    updatedThisWake = false; cause = 0; assert(UpdatePartialDisplay(gray.data(), false) && fulls == 8);
    fail_invalidate = true; cause = ESP_SLEEP_WAKEUP_TIMER; gray[13] = 0;
    int before = paints; assert(!UpdatePartialDisplay(gray.data(), false) && paints == before);
    fail_invalidate = false; fail_seek = true;
    assert(!UpdatePartialDisplay(gray.data(), false) && values["valid"] == 0);
    fail_seek = false; bytes_written = 0;
    assert(UpdatePartialDisplay(gray.data(), false) && bytes_written == BitmapSize);
    makeBitmap(gray.data(), bitmap.data()); assert(stored == bitmap);
    for (int i = 45; i < 77; ++i) gray[i * 2048] = 0;
    bytes_written = 0; assert(UpdatePartialDisplay(gray.data(), false) && bytes_written == 32 * 512);
    makeBitmap(gray.data(), bitmap.data()); assert(stored == bitmap);
    for (int i = 80; i < 113; ++i) gray[i * 2048] = 0;
    bytes_written = 0; assert(UpdatePartialDisplay(gray.data(), false) && bytes_written == BitmapSize);
    makeBitmap(gray.data(), bitmap.data()); assert(stored == bitmap);
    gray[120 * 2048] = 0; gray[122 * 2048] = 0;
    writes_before_failure = 1;
    assert(!UpdatePartialDisplay(gray.data(), false) && values["valid"] == 0);
    writes_before_failure = -1; updatedThisWake = false; cause = ESP_SLEEP_WAKEUP_TIMER;
    bytes_written = 0; assert(UpdatePartialDisplay(gray.data(), false) && bytes_written == BitmapSize);
    makeBitmap(gray.data(), bitmap.data()); assert(stored == bitmap);
    PowerOffPartialDisplay();
    assert(ends == 1);
    puts("Bitmap and refresh-state tests passed");
}
'''.replace("SOURCE", str(root / "partial_display.cpp"))

with tempfile.TemporaryDirectory() as directory:
    directory = Path(directory)
    for name, content in headers.items():
        path = directory / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content)
    cpp = directory / "partial.cpp"
    executable = directory / "partial"
    cpp.write_text(harness)
    subprocess.run(["g++", "-std=c++17", "-DEPDPARTIAL=1", "-I", str(directory),
                    str(cpp), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
