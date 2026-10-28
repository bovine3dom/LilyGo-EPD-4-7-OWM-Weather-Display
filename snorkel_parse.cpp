// SPDX-License-Identifier: GPL-3.0-only
#include "snorkel.h"
#include "transitous.h"
#include <ArduinoJson.h>
#include <cmath>
#include <cstring>

namespace {
// Meteociel uses Paris local time. Skip ambiguous or nonexistent DST hours.
bool parisTimestamp(const char *value, time_t &result)
{
    if (!value || strlen(value) < 19) return false;
    tm parsed = {};
    char *end = strptime(value, "%Y-%m-%dT%H:%M:%S", &parsed);
    if (!end) return false;
    if (*end == '.') { ++end; while (*end >= '0' && *end <= '9') ++end; }
    if (*end) return false;
    char utc[21];
    memcpy(utc, value, 19); utc[19] = 'Z'; utc[20] = '\0';
    time_t wall;
    if (!ParseTransitTime(utc, wall)) return false;
    tm calendar;
    char check[20];
    if (!gmtime_r(&wall, &calendar) || strftime(check, sizeof(check), "%Y-%m-%dT%H:%M:%S", &calendar) != 19 ||
        strncmp(check, value, 19)) return false;
    int month = calendar.tm_mon + 1, day = calendar.tm_mday, hour = calendar.tm_hour;
    int lastSunday = 31 - (calendar.tm_wday + 31 - day) % 7;
    if ((month == 3 || month == 10) && day == lastSunday && hour == 2) return false;
    bool summer = (month > 3 && month < 10) ||
                  (month == 3 && (day > lastSunday || (day == lastSunday && hour >= 3))) ||
                  (month == 10 && (day < lastSunday || (day == lastSunday && hour < 2)));
    result = wall - (summer ? 7200 : 3600);
    return true;
}

float number(JsonVariantConst value, float low, float high)
{
    if (!value.is<float>()) return NAN;
    float result = value.as<float>();
    return std::isfinite(result) && result >= low && result <= high ? result : NAN;
}
}

bool ParseSnorkel(char *json, size_t length, SnorkelData &data)
{
    data = SnorkelData{};
    DynamicJsonDocument doc(12 * 1024);
    if (deserializeJson(doc, json, length)) return false;
    for (JsonObjectConst row : doc["forecast"].as<JsonArrayConst>()) {
        if (data.forecastCount == SnorkelForecastMax) break;
        time_t time;
        if (!parisTimestamp(row["time"], time)) continue;
        size_t i = data.forecastCount++;
        data.forecastTimes[i] = time;
        data.waves[i] = number(row["wave_m"], 0, 100);
        data.water[i] = number(row["water_c"], -5, 50);
        data.wavesAvailable |= std::isfinite(data.waves[i]);
        data.waterAvailable |= std::isfinite(data.water[i]);
    }
    for (JsonObjectConst row : doc["jellies"].as<JsonArrayConst>()) {
        if (data.jellyCount == SnorkelJellyMax) break;
        if (!row["end"].is<int64_t>() || row["end"].as<int64_t>() < 1600000000) continue;
        size_t i = data.jellyCount++;
        data.jellyTimes[i] = static_cast<time_t>(row["end"].as<int64_t>());
        data.nice[i] = number(row["nice"], 0, 400);
        data.vf[i] = number(row["vf"], 0, 400);
        data.jelliesAvailable |= std::isfinite(data.nice[i]) || std::isfinite(data.vf[i]);
    }
    return data.wavesAvailable || data.waterAvailable || data.jelliesAvailable;
}
