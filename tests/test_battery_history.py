#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise discharge learning, charging and uncertain gaps."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parent.parent
source = r'''
#include "battery_history.h"
#include <cassert>
#include <cstring>

constexpr uint32_t start = 1700000000, hour = 3600;
constexpr uint16_t floor = 3712;

void discharge(BatteryHistory &h, bool reachesFloor) {
    // Include a charged plateau followed by an eight-day discharge.
    assert(RecordBatterySample(h, start, 4250, floor));
    assert(!RecordBatterySample(h, start + hour, 4249, floor));
    assert(RecordBatterySample(h, start + 2 * hour, 4250, floor));
    for (unsigned i = 1; i <= 96; ++i) {
        uint16_t mv = 4165 - i * (reachesFloor ? 453 : 420) / 96;
        assert(RecordBatterySample(h, start + (2 + 2 * i) * hour, mv, floor));
    }
}

int main() {
    BatteryHistory h;
    assert(LearnedBatterySeconds(h, 4000) == -1);
    discharge(h, true);
    assert(h.count == 0 && h.learnedCount >= 15);
    assert(LearnedBatterySeconds(h, floor) == 0);
    auto early = LearnedBatterySeconds(h, 4100), late = LearnedBatterySeconds(h, 3800);
    assert(early > late && late > 0);
    BatteryHistory persisted;
    std::memcpy(&persisted, &h, sizeof(h));
    assert(LearnedBatterySeconds(persisted, 4000) == LearnedBatterySeconds(h, 4000));
    assert(RecordBatterySample(h, start + 220 * hour, 4200, floor));
    assert(h.learnedCount == persisted.learnedCount);
    assert(!RecordBatterySample(h, start, 4200, floor));
    assert(RecordBatterySample(h, start + 224 * hour, 4200, floor + 1));
    assert(h.learnedCount == 0);

    BatteryHistory recharge;
    discharge(recharge, false);
    assert(recharge.learnedCount == 0 && recharge.count > 15);
    assert(RecordBatterySample(recharge, start + 196 * hour, 4200, floor));
    assert(recharge.learnedCount > 15); // Ends at the last real sample, not at the later charging voltage.
    assert(recharge.learned[recharge.learnedCount - 1].millivolts > floor);

    BatteryHistory gap;
    discharge(gap, false);
    assert(RecordBatterySample(gap, start + 300 * hour, 4200, floor));
    assert(gap.learnedCount == 0);
    BatteryHistory shortTrace;
    assert(RecordBatterySample(shortTrace, start, 4200, floor));
    assert(RecordBatterySample(shortTrace, start + 2 * hour, 4050, floor));
    assert(RecordBatterySample(shortTrace, start + 4 * hour, floor, floor));
    assert(shortTrace.learnedCount == 0);
    assert(!RecordBatterySample(shortTrace, start + 6 * hour, 0, floor));
}
'''
with tempfile.TemporaryDirectory() as directory:
    cpp, exe = Path(directory) / 'test.cpp', Path(directory) / 'test'
    cpp.write_text(source)
    subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-I', str(root), str(cpp),
                    str(root / 'battery_history.cpp'), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('Battery discharge history tests passed')

firmware = (root / 'LilyGo-EPD-4-7-OWM-Weather-Display.ino').read_text()
def function(signature):
    start = firmware.index(signature)
    opening = firmware.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (firmware[end] == '{') - (firmware[end] == '}')
        end += 1
    return firmware[start:end]

integration = r'''
#include "battery_history.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <ctime>
#include <vector>
using std::min; using std::max;
template<class T> T constrain(T v, T lo, T hi) { return min(hi, max(lo, v)); }
#define BATTERY_FLOOR_MV 3712
#define BATTERY_RUNTIME_DAYS 10
float BatteryVoltage = 4.2f;
int64_t BatteryLifeEstimateSeconds = -1;
std::vector<unsigned char> storage;
struct Preferences {
    bool begin(const char*, bool) { return true; }
    size_t getBytesLength(const char*) { return storage.size(); }
    size_t getBytes(const char*, void *p, size_t n) { std::memcpy(p, storage.data(), n); return n; }
    size_t putBytes(const char*, const void *p, size_t n) {
        auto *data = static_cast<const unsigned char *>(p); storage.assign(data, data + n); return n;
    }
    void end() {}
};
struct { void println(const char*) {} template<class... T> void printf(const char*, T...) {} } Serial;
'''
integration += '\n'.join(function(name) for name in (
    'float BatteryCurvePercentage(', 'uint8_t BatteryPercentage(', 'int BatteryUpdateInterval(',
    'void UpdateBatteryLifeEstimate('))
integration += r'''
int main() {
    UpdateBatteryLifeEstimate(0);
    assert(BatteryLifeEstimateSeconds == 8 * 86400 && storage.empty());
    constexpr uint32_t start = 1700000000;
    UpdateBatteryLifeEstimate(start);
    assert(storage.size() == sizeof(BatteryHistory));
    BatteryVoltage = 3.712f;
    UpdateBatteryLifeEstimate(start + 3600);
    assert(BatteryLifeEstimateSeconds == 0 && BatteryUpdateInterval(BatteryVoltage) == 16);
    BatteryVoltage = 4.2f;
    UpdateBatteryLifeEstimate(start + 7200);
    for (unsigned i = 1; i <= 96; ++i) {
        BatteryVoltage = (4165 - i * 453 / 96) / 1000.0f;
        UpdateBatteryLifeEstimate(start + (2 + 2 * i) * 3600);
    }
    BatteryHistory saved;
    std::memcpy(&saved, storage.data(), sizeof(saved));
    assert(saved.learnedCount >= 15);
    BatteryVoltage = 4.0f;
    UpdateBatteryLifeEstimate(start + 198 * 3600);
    assert(BatteryLifeEstimateSeconds > 0 && BatteryLifeEstimateSeconds < 8 * 86400);
}
'''
with tempfile.TemporaryDirectory() as directory:
    cpp, exe = Path(directory) / 'integration.cpp', Path(directory) / 'integration'
    cpp.write_text(integration)
    subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-I', str(root), str(cpp),
                    str(root / 'battery_history.cpp'), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('Battery estimate and persistence tests passed')
