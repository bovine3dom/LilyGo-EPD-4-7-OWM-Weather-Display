// SPDX-License-Identifier: GPL-3.0-only
#include "battery_history.h"
#include <algorithm>

namespace {
constexpr uint32_t SampleInterval = 2 * 3600, MaximumGap = 3 * 86400;
constexpr uint16_t RechargeRise = 80;

void FinishTrace(BatteryHistory &h, uint16_t floor)
{
    if (h.descending && h.count >= 15 && h.minimum <= floor + 50 &&
        h.active[h.count - 1].time - h.active[0].time >= 4 * 86400 &&
        h.active[0].millivolts >= floor + 200)
    {
        h.learnedCount = h.count;
        std::copy(h.active, h.active + h.count, h.learned);
    }
}

void StartTrace(BatteryHistory &h, uint32_t now, uint16_t mv, uint16_t floor)
{
    h.count = 0;
    h.descending = false;
    h.minimum = mv;
    if (mv >= floor + (4200 - floor) * 4 / 5)
        h.active[h.count++] = {now, mv};
}
}

bool RecordBatterySample(BatteryHistory &h, uint32_t now, uint16_t mv, uint16_t floor)
{
    if (now < 1600000000 || mv < 2500 || mv > 5000 || floor < 3000 || floor > 4000) return false;
    if (h.version != 1 || h.floor != floor || h.count > BatteryHistoryCapacity || h.learnedCount > BatteryHistoryCapacity)
    {
        h = BatteryHistory{};
        h.floor = floor;
    }
    if (h.lastTime && now <= h.lastTime) return false;
    if (h.lastTime && now - h.lastTime < SampleInterval) return false;
    bool gap = h.lastTime && now - h.lastTime > MaximumGap;
    h.lastTime = now;
    if (!h.count || gap || mv > h.minimum + RechargeRise)
    {
        if (h.count && !gap && mv > h.minimum + RechargeRise) FinishTrace(h, floor);
        StartTrace(h, now, mv, floor);
        return true;
    }
    h.minimum = std::min(h.minimum, mv);
    if (!h.descending)
    {
        auto &peak = h.active[0];
        if (mv + RechargeRise > peak.millivolts)
        {
            peak.millivolts = std::max(peak.millivolts, mv);
            peak.time = now;
            return true;
        }
        h.descending = true;
    }
    if (h.count == BatteryHistoryCapacity)
    {
        StartTrace(h, now, mv, floor);
        return true;
    }
    h.active[h.count++] = {now, mv};
    if (mv <= floor)
    {
        FinishTrace(h, floor);
        h.count = 0;
        h.descending = false;
    }
    return true;
}

int64_t LearnedBatterySeconds(const BatteryHistory &h, uint16_t mv)
{
    if (h.learnedCount < 2 || h.learnedCount > BatteryHistoryCapacity) return -1;
    const auto *samples = h.learned;
    uint32_t end = samples[h.learnedCount - 1].time;
    if (end <= samples[0].time) return -1;
    if (mv >= samples[0].millivolts) return end - samples[0].time;
    uint16_t previous = samples[0].millivolts;
    uint32_t previousTime = samples[0].time;
    for (unsigned i = 1; i < h.learnedCount; ++i)
    {
        if (samples[i].time <= previousTime || samples[i].time > end) return -1;
        uint16_t current = std::min(previous, samples[i].millivolts);
        if (mv >= current && current < previous)
        {
            uint32_t elapsed = uint64_t(samples[i].time - previousTime) * (previous - mv) / (previous - current);
            return end - previousTime - elapsed;
        }
        previous = current;
        previousTime = samples[i].time;
    }
    return 0;
}
