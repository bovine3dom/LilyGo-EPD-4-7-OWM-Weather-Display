// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <stddef.h>
#include <time.h>

constexpr size_t SnorkelForecastMax = 64, SnorkelJellyMax = 16;
struct SnorkelData {
    time_t forecastTimes[SnorkelForecastMax] = {}, jellyTimes[SnorkelJellyMax] = {};
    float waves[SnorkelForecastMax] = {}, water[SnorkelForecastMax] = {};
    float nice[SnorkelJellyMax] = {}, vf[SnorkelJellyMax] = {};
    size_t forecastCount = 0, jellyCount = 0;
    bool wavesAvailable = false, waterAvailable = false, jelliesAvailable = false;
};

bool ParseSnorkel(char *json, size_t length, SnorkelData &data);
bool FetchSnorkel(SnorkelData &data);
