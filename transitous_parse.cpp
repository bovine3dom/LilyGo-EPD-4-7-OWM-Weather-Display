// SPDX-License-Identifier: GPL-3.0-only
#include "transitous.h"
#include <ArduinoJson.h>
#include <cstdio>
#include <cstring>
#include <ctime>

namespace {
bool parseTime(const char *value, time_t &timestamp)
{
    if (!value) return false;
    tm utc = {};
    char *end = strptime(value, "%Y-%m-%dT%H:%M:%S", &utc);
    if (!end) return false;
    if (*end == '.') {
        ++end;
        while (*end >= '0' && *end <= '9') ++end;
    }
    int offset = 0;
    if (*end == '+' || *end == '-') {
        int hours, minutes;
        char tail;
        if (sscanf(end + 1, "%2d:%2d%c", &hours, &minutes, &tail) != 2 || hours > 23 || minutes > 59) return false;
        offset = (hours * 60 + minutes) * 60 * (*end == '+' ? 1 : -1);
    } else if (strcmp(end, "Z") != 0) return false;
    int year = utc.tm_year + 1900, month = utc.tm_mon + 1;
    year -= month <= 2;
    int era = (year >= 0 ? year : year - 399) / 400;
    unsigned yearOfEra = year - era * 400;
    unsigned dayOfYear = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + utc.tm_mday - 1;
    unsigned dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
    int64_t days = static_cast<int64_t>(era) * 146097 + dayOfEra - 719468;
    timestamp = days * 86400 + utc.tm_hour * 3600 + utc.tm_min * 60 + utc.tm_sec - offset;
    return true;
}

bool formatTime(time_t timestamp, char output[6])
{
    tm local;
    return localtime_r(&timestamp, &local) && strftime(output, 6, "%H:%M", &local) == 5;
}

bool isDesiredStop(JsonObjectConst place, const TramStop &target)
{
    const char *name = place["name"] | "";
    double lat = place["lat"] | 999.0, lon = place["lon"] | 999.0;
    return !strcmp(name, target.name) && lat >= target.latitude - TramSearchHalfWidth &&
           lat <= target.latitude + TramSearchHalfWidth && lon >= target.longitude - TramSearchHalfWidth &&
           lon <= target.longitude + TramSearchHalfWidth;
}
}

bool FindTramStop(const char *json, size_t length, char *stopId, size_t capacity, const TramStop &target)
{
    if (!capacity) return false;
    stopId[0] = '\0';
    StaticJsonDocument<512> filter;
    JsonObject f = filter.to<JsonArray>().createNestedObject();
    f["name"] = true;
    f["stopId"] = true;
    f["lat"] = true;
    f["lon"] = true;
    f["modes"].to<JsonArray>().add(true);
    DynamicJsonDocument doc(8192);
    if (deserializeJson(doc, json, length, DeserializationOption::Filter(filter))) return false;
    JsonArrayConst stops = doc.as<JsonArrayConst>();
    if (stops.isNull()) return false;
    double nearest = 1e9;
    for (JsonObjectConst stop : stops) {
        if (!isDesiredStop(stop, target)) continue;
        bool tram = false;
        for (const char *mode : stop["modes"].as<JsonArrayConst>())
            if (mode && !strcmp(mode, "TRAM")) tram = true;
        const char *id = stop["stopId"] | "";
        if (!tram || !*id || strlen(id) >= capacity) continue;
        double lat = stop["lat"].as<double>() - target.latitude;
        double lon = stop["lon"].as<double>() - target.longitude;
        double distance = lat * lat + lon * lon;
        if (distance < nearest) {
            nearest = distance;
            strcpy(stopId, id);
        }
    }
    return stopId[0] != '\0';
}

bool IsTramStopResponse(const char *json, size_t length, const TramStop &target)
{
    StaticJsonDocument<256> filter;
    filter["place"]["name"] = true;
    filter["place"]["lat"] = true;
    filter["place"]["lon"] = true;
    StaticJsonDocument<512> doc;
    return !deserializeJson(doc, json, length, DeserializationOption::Filter(filter)) &&
           isDesiredStop(doc["place"].as<JsonObjectConst>(), target);
}

bool EncodeTransitousId(const char *id, char *encoded, size_t capacity)
{
    const char hex[] = "0123456789ABCDEF";
    size_t used = 0;
    if (!capacity || !id || !*id) return false;
    for (const unsigned char *p = reinterpret_cast<const unsigned char *>(id); *p; ++p) {
        bool plain = (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
                     (*p >= '0' && *p <= '9') || *p == '-' || *p == '_' || *p == '.' || *p == '~';
        if (used + (plain ? 1 : 3) >= capacity) { encoded[0] = '\0'; return false; }
        if (plain) encoded[used++] = *p;
        else { encoded[used++] = '%'; encoded[used++] = hex[*p >> 4]; encoded[used++] = hex[*p & 15]; }
    }
    encoded[used] = '\0';
    return true;
}

size_t GroupTramDepartures(const TramDeparture *rows, size_t count, TramGroup *groups, size_t capacity)
{
    size_t used = 0;
    for (size_t i = 0; i < count; ++i) {
        size_t group = 0;
        while (group < used && (strcmp(groups[group].line, rows[i].line) ||
                                strcmp(groups[group].destination, rows[i].destination))) ++group;
        if (group == used) {
            if (used == capacity) continue;
            groups[used++] = {rows[i].line, rows[i].destination, {}, 0};
        }
        if (groups[group].count < 5)
            groups[group].times[groups[group].count++] = rows[i].expected[0] == '-' ? rows[i].scheduled : rows[i].expected;
    }
    return used;
}

bool ParseTransitous(char *json, size_t length, TramDeparture *rows, size_t capacity, size_t &count, time_t cutoffTime)
{
    count = 0;
    StaticJsonDocument<768> filter;
    JsonObject f = filter.createNestedArray("stopTimes").createNestedObject();
    f["routeShortName"] = true;
    f["headsign"] = true;
    f["realTime"] = true;
    f["cancelled"] = true;
    f["tripCancelled"] = true;
    f["place"]["scheduledDeparture"] = true;
    f["place"]["departure"] = true;
    f["place"]["departureTimestampReason"] = true;
    f["place"]["cancelled"] = true;
    DynamicJsonDocument doc(32768);
    if (deserializeJson(doc, json, length, DeserializationOption::Filter(filter))) return false;
    JsonArrayConst events = doc["stopTimes"].as<JsonArrayConst>();
    if (events.isNull()) return false;
    for (JsonObjectConst event : events) {
        const char *line = event["routeShortName"];
        const char *destination = event["headsign"];
        JsonObjectConst place = event["place"];
        if (!line || !*line || !destination || !*destination || event["cancelled"] == true ||
            event["tripCancelled"] == true || place["cancelled"] == true) continue;
        time_t scheduled = 0, expected = 0;
        bool hasSchedule = parseTime(place["scheduledDeparture"], scheduled);
        const char *reason = place["departureTimestampReason"] | "";
        bool predicted = event["realTime"] == true &&
                         (!strcmp(reason, "PREDICTED") || !strcmp(reason, "PROPAGATED") || !strcmp(reason, "OBSERVED")) &&
                         parseTime(place["departure"], expected);
        if (!predicted && !hasSchedule) continue;
        time_t effective = predicted ? expected : scheduled;
        if (effective <= cutoffTime) continue;
        size_t position = 0;
        while (position < count && rows[position].effectiveTime <= effective) ++position;
        if (position >= capacity) continue;
        TramDeparture row = {};
        row.effectiveTime = effective;
        if (!hasSchedule || !formatTime(scheduled, row.scheduled))
            snprintf(row.scheduled, sizeof(row.scheduled), "--:--");
        snprintf(row.line, sizeof(row.line), "%s", line);
        snprintf(row.destination, sizeof(row.destination), "%s", destination);
        if (predicted) {
            if (!formatTime(expected, row.expected)) continue;
        } else snprintf(row.expected, sizeof(row.expected), "--:--");
        if (count < capacity) ++count;
        for (size_t i = count - 1; i > position; --i) rows[i] = rows[i - 1];
        rows[position] = row;
    }
    return true;
}
