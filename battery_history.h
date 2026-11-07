// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <stdint.h>

constexpr unsigned BatteryHistoryCapacity = 128;
struct BatterySample {
    uint32_t time;
    uint16_t millivolts;
};
struct BatteryHistory {
    uint32_t version = 1;
    uint32_t lastTime = 0;
    uint16_t floor = 0, count = 0, learnedCount = 0, minimum = 0;
    bool descending = false;
    BatterySample active[BatteryHistoryCapacity] = {};
    BatterySample learned[BatteryHistoryCapacity] = {};
};

bool RecordBatterySample(BatteryHistory &history, uint32_t now, uint16_t millivolts, uint16_t floor);
int64_t LearnedBatterySeconds(const BatteryHistory &history, uint16_t millivolts);
