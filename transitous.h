// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <stddef.h>
#include <time.h>

constexpr double TramSearchHalfWidth = 0.005;

struct TramStop {
    const char *name;
    double latitude;
    double longitude;
};

struct TramDeparture {
    char line[7];
    char destination[160];
    char scheduled[6];
    char expected[7];
    time_t effectiveTime;
};

struct TramGroup {
    const char *line;
    const char *destination;
    const char *times[5];
    size_t count;
};

bool ParseTransitTime(const char *value, time_t &timestamp);
bool FormatTransitTime(time_t timestamp, char output[6]);
bool FindTramStop(const char *json, size_t length, char *stopId, size_t capacity, const TramStop &target);
bool IsTramStopResponse(const char *json, size_t length, const TramStop &target);
bool EncodeTransitousId(const char *id, char *encoded, size_t capacity);
// Rows are sorted by effectiveTime; groups follow the first departure.
size_t GroupTramDepartures(const TramDeparture *rows, size_t count, TramGroup *groups, size_t capacity);
bool ParseTransitous(char *json, size_t length, TramDeparture *rows, size_t capacity, size_t &count, time_t cutoffTime);
bool FetchTransitous(TramDeparture *rows, size_t capacity, size_t &count);
