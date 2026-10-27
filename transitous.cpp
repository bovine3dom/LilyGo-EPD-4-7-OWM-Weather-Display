// SPDX-License-Identifier: GPL-3.0-only
#include "transitous.h"
#include "transitous_ca.h"
#include "bounded_response.h"
#include ".pio/private_config.h"
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

namespace {
constexpr size_t MaxResponse = 160 * 1024;
constexpr char StopsEndpoint[] = "https://api.transitous.org/api/v6/map/stops?grouped=true&modes=TRAM&";
constexpr TramStop TargetStop = {TRAM_STOP_NAME, TRAM_STOP_LATITUDE, TRAM_STOP_LONGITUDE};
constexpr char DeparturesEndpoint[] = "https://api.transitous.org/api/v6/stoptimes?mode=TRAM&n=32&stopId=";
RTC_DATA_ATTR char cachedStopId[256] = {};
RTC_DATA_ATTR time_t cachedStopUntil = 0;

int RequestJson(const String &url, uint8_t *data, size_t &length)
{
    length = 0;
    WiFiClientSecure client;
    client.setCACert(TransitousRootCA);
    HTTPClient http;
    http.setConnectTimeout(10000);
    http.setTimeout(10000);
    if (!http.begin(client, url)) return -1;
    http.setUserAgent(String("LilyGo-Weather-Display/2.5 (+https://github.com/bovine3dom/LilyGo-EPD-4-7-OWM-Weather-Display; ") + TRANSITOUS_CONTACT + ")");
    int status = http.GET();
    if (status == HTTP_CODE_OK) {
        BoundedResponse response(data, MaxResponse);
        if (http.writeToStream(&response) > 0) length = response.used;
        else status = -1;
    }
    http.end();
    return status;
}

bool ResolveTramStop(uint8_t *data, char *stopId, size_t capacity)
{
    size_t length;
    int status = RequestJson(String(StopsEndpoint) + TRAM_STOP_BBOX, data, length);
    bool ok = status == HTTP_CODE_OK && FindTramStop(reinterpret_cast<const char *>(data), length, stopId, capacity, TargetStop);
    if (!ok) Serial.printf("Transitous stop lookup failed: %d\n", status);
    return ok;
}

int RequestDepartures(uint8_t *data, size_t &length)
{
    char encoded[sizeof(cachedStopId) * 3];
    if (!EncodeTransitousId(cachedStopId, encoded, sizeof(encoded))) return -1;
    time_t cutoff = time(nullptr) + TRAM_MINIMUM_SECONDS;
    tm utc;
    char queryTime[32];
    if (!gmtime_r(&cutoff, &utc) || !strftime(queryTime, sizeof(queryTime), "%Y-%m-%dT%H%%3A%M%%3A%SZ", &utc)) return -1;
    return RequestJson(String(DeparturesEndpoint) + encoded + "&time=" + queryTime, data, length);
}
}

bool FetchTransitous(TramDeparture *rows, size_t capacity, size_t &count)
{
    count = 0;
    if (!TRANSITOUS_CONTACT[0]) {
        Serial.println("Transitous contact is missing from .env");
        return false;
    }
    auto *data = static_cast<uint8_t *>(ps_malloc(MaxResponse));
    if (!data) return false;
    cachedStopId[sizeof(cachedStopId) - 1] = '\0';
    if (!cachedStopId[0] || time(nullptr) >= cachedStopUntil) {
        char resolved[sizeof(cachedStopId)];
        if (ResolveTramStop(data, resolved, sizeof(resolved))) {
            strcpy(cachedStopId, resolved);
            cachedStopUntil = time(nullptr) + 24 * 60 * 60;
        } else if (!cachedStopId[0]) {
            free(data);
            return false;
        } else {
            cachedStopUntil = time(nullptr) + 5 * 60;
            Serial.println("Using cached tram stop after lookup failure");
        }
    }
    size_t length = 0;
    int status = RequestDepartures(data, length);
    bool correct = status == HTTP_CODE_OK && IsTramStopResponse(reinterpret_cast<const char *>(data), length, TargetStop);
    if (status == 404 || (status == HTTP_CODE_OK && !correct)) {
        char previous[sizeof(cachedStopId)], resolved[sizeof(cachedStopId)];
        strcpy(previous, cachedStopId);
        cachedStopId[0] = '\0';
        cachedStopUntil = 0;
        if (ResolveTramStop(data, resolved, sizeof(resolved)) && strcmp(previous, resolved)) {
            strcpy(cachedStopId, resolved);
            cachedStopUntil = time(nullptr) + 24 * 60 * 60;
            status = RequestDepartures(data, length);
            correct = status == HTTP_CODE_OK && IsTramStopResponse(reinterpret_cast<const char *>(data), length, TargetStop);
        }
    }
    bool ok = correct && ParseTransitous(reinterpret_cast<char *>(data), length, rows, capacity, count,
                                          time(nullptr) + TRAM_MINIMUM_SECONDS);
    if (!ok) Serial.printf("Transitous departures failed: %d\n", status);
    free(data);
    return ok;
}
