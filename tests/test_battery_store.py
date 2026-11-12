#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check alternating battery-history snapshots and interrupted writes."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parent.parent
header = r'''
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>
extern std::map<std::string, std::vector<uint8_t>> files;
extern size_t writeBudget;
struct File {
    std::vector<uint8_t> *data = nullptr;
    size_t pos = 0;
    explicit operator bool() const { return data; }
    size_t size() const { return data ? data->size() : 0; }
    size_t read(uint8_t *out, size_t n) {
        if (!data) return 0;
        n = std::min(n, data->size() - pos); std::memcpy(out, data->data() + pos, n); pos += n; return n;
    }
    size_t write(const uint8_t *in, size_t n) {
        if (!data) return 0;
        n = std::min(n, writeBudget); writeBudget -= n;
        data->insert(data->end(), in, in + n); pos += n; return n;
    }
    void flush() {}
    void close() {}
};
struct SPIFFSClass {
    File open(const char *path, const char *mode) {
        if (*mode == 'w') { auto &data = files[path]; data.clear(); return {&data}; }
        auto it = files.find(path); return it == files.end() ? File{} : File{&it->second};
    }
};
extern SPIFFSClass SPIFFS;
'''
harness = r'''
#include "battery_store.h"
#include "SPIFFS.h"
#include <cassert>
#include <limits>
std::map<std::string, std::vector<uint8_t>> files;
size_t writeBudget = std::numeric_limits<size_t>::max();
SPIFFSClass SPIFFS;
int main() {
    BatteryHistory h; h.floor = 3712; h.count = 1; h.active[0] = {1700000000, 4200};
    BatteryStore store;
    assert(!LoadBatteryHistory(h, store) && store.slot == -1);
    assert(SaveBatteryHistory(h, store) && store.slot == 0 && store.generation == 1);
    BatteryHistory loaded; BatteryStore reboot;
    assert(LoadBatteryHistory(loaded, reboot) && loaded.active[0].millivolts == 4200);
    for (unsigned i = 0; i < 200; ++i) {
        h.active[0].time++; h.active[0].millivolts = 4000 + i;
        assert(SaveBatteryHistory(h, store));
        assert(LoadBatteryHistory(loaded, reboot));
        assert(reboot.generation == store.generation && loaded.active[0].millivolts == h.active[0].millivolts);
    }
    uint16_t previous = h.active[0].millivolts;
    h.active[0].millivolts = 3700; writeBudget = 100;
    assert(!SaveBatteryHistory(h, store));
    assert(LoadBatteryHistory(loaded, reboot) && loaded.active[0].millivolts == previous);
    writeBudget = std::numeric_limits<size_t>::max();
    assert(SaveBatteryHistory(h, store));
    files[store.slot == 0 ? "/battery-trace0.bin" : "/battery-trace1.bin"][40] ^= 0x20;
    assert(LoadBatteryHistory(loaded, reboot) && loaded.active[0].millivolts == previous);
    files.clear(); h.floor = 2000;
    assert(!SaveBatteryHistory(h, store));
    assert(!LoadBatteryHistory(loaded, reboot));
}
'''
with tempfile.TemporaryDirectory() as directory:
    d = Path(directory)
    (d / 'SPIFFS.h').write_text(header)
    (d / 'test.cpp').write_text(harness)
    subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-I', str(d), '-I', str(root),
                    str(d / 'test.cpp'), str(root / 'battery_store.cpp'), '-o', str(d / 'test')], check=True)
    subprocess.run([str(d / 'test')], check=True)
print('Battery storage and interrupted-write tests passed')
