// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <ArduinoJson.h>
#include <stddef.h>
#include <time.h>

struct RailDeparture {
    char destination[160];
    char scheduled[6];
    char expected[12];
    char platform[64];
    time_t effectiveTime;
};

bool ParseRail(JsonArrayConst departures, JsonArrayConst predictions, RailDeparture *rows,
               size_t capacity, size_t &count, time_t cutoff, const char *station);
bool FetchRail(RailDeparture *rows, size_t capacity, size_t &count);
