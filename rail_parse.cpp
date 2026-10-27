// SPDX-License-Identifier: GPL-3.0-only
#include "rail.h"
#include "transitous.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {
struct Probability { char platform[16]; double value; };

bool contiguous(const char *a, const char *b)
{
    if (strlen(a) == 1 && strlen(b) == 1 && *a >= 'A' && *a < 'Z' && *b == *a + 1) return true;
    char *endA, *endB;
    long x = strtol(a, &endA, 10), y = strtol(b, &endB, 10);
    return *a && *b && !*endA && !*endB && y == x + 1;
}

void formatPlatform(JsonObjectConst train, JsonObjectConst prediction, char out[64])
{
    const char *official = train["platform"]["track"] | "";
    snprintf(out, 64, "%s", official);
    Probability high[32] = {}, low[32] = {}, groups[32] = {};
    size_t nHigh = 0, nLow = 0, nGroups = 0;
    for (JsonObjectConst p : prediction["probabilities"].as<JsonArrayConst>()) {
        const char *name = p["platform"] | "";
        double value = p["prob"] | 0.0;
        if (!*name || strlen(name) >= sizeof(high[0].platform) || value <= 0.05 || value > 1) continue;
        Probability item = {};
        snprintf(item.platform, sizeof(item.platform), "%s", name);
        item.value = value;
        if (value >= 0.30 && nHigh < 32) high[nHigh++] = item;
        else if (value < 0.30 && nLow < 32) low[nLow++] = item;
    }
    std::sort(high, high + nHigh, [](const Probability &a, const Probability &b) { return a.value > b.value; });
    std::sort(low, low + nLow, [](const Probability &a, const Probability &b) {
        char *endA, *endB;
        long x = strtol(a.platform, &endA, 10), y = strtol(b.platform, &endB, 10);
        return !*endA && !*endB && x != y ? x < y : strcmp(a.platform, b.platform) < 0;
    });
    for (size_t i = 0; i < nLow;) {
        size_t end = i + 1;
        double sum = low[i].value;
        while (end < nLow && contiguous(low[end - 1].platform, low[end].platform)) sum += low[end++].value;
        Probability &group = groups[nGroups++];
        if (end == i + 1) snprintf(group.platform, sizeof(group.platform), "%s", low[i].platform);
        else snprintf(group.platform, sizeof(group.platform), "%.7s-%.7s", low[i].platform, low[end - 1].platform);
        group.value = sum;
        i = end;
    }
    std::sort(groups, groups + nGroups, [](const Probability &a, const Probability &b) { return a.value > b.value; });
    unsigned shown = 0;
    for (size_t i = 0; i < nHigh + nGroups && shown < 2; ++i) {
        const Probability &p = i < nHigh ? high[i] : groups[i - nHigh];
        if (p.value < 0.1) continue;
        size_t used = strlen(out);
        snprintf(out + used, 64 - used, "%s%s (%d%%)", shown ? ", " : (*out ? " | " : ""),
                 p.platform, (int)lround(p.value * 100));
        ++shown;
    }
    if (!*out) snprintf(out, 64, "--");
}
}

bool ParseRail(JsonArrayConst departures, JsonArrayConst predictions, RailDeparture *rows,
               size_t capacity, size_t &count, time_t cutoff, const char *station)
{
    count = 0;
    if (departures.isNull()) return false;
    size_t index = 0;
    for (JsonObjectConst train : departures) {
        if (strcmp(train["direction"] | "", "Departure") || strcmp(train["uic"] | "", station)) continue;
        JsonObjectConst prediction = predictions[index++];
        const char *destination = train["traffic"]["destination"] | "";
        time_t scheduled = 0, expected = 0;
        bool hasScheduled = ParseTransitTime(train["scheduledTime"], scheduled);
        bool hasExpected = ParseTransitTime(train["actualTime"], expected);
        if (!*destination || (!hasScheduled && !hasExpected)) continue;
        time_t effective = hasExpected ? expected : scheduled;
        if (effective < cutoff) continue;
        size_t position = 0;
        while (position < count && rows[position].effectiveTime <= effective) ++position;
        if (position >= capacity) continue;
        RailDeparture row = {};
        row.effectiveTime = effective;
        snprintf(row.destination, sizeof(row.destination), "%s", destination);
        if (!hasScheduled || !FormatTransitTime(scheduled, row.scheduled)) snprintf(row.scheduled, sizeof(row.scheduled), "--:--");
        const char *status = train["informationStatus"]["trainStatus"] | "";
        if (strstr(status, "SUPPRESSION")) snprintf(row.expected, sizeof(row.expected), "Cancelled");
        else if (!hasExpected || !FormatTransitTime(expected, row.expected)) snprintf(row.expected, sizeof(row.expected), "--:--");
        formatPlatform(train, prediction, row.platform);
        if (count < capacity) ++count;
        for (size_t i = count - 1; i > position; --i) rows[i] = rows[i - 1];
        rows[position] = row;
    }
    return true;
}
