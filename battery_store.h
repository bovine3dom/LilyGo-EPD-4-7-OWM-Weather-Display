// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "battery_history.h"

struct BatteryStore {
    uint32_t generation = 0;
    int slot = -1;
};

bool LoadBatteryHistory(BatteryHistory &history, BatteryStore &store);
bool SaveBatteryHistory(const BatteryHistory &history, BatteryStore &store);
