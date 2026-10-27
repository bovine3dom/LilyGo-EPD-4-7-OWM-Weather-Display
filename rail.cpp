// SPDX-License-Identifier: GPL-3.0-only
#include "rail.h"
#include "rail_ca.h"
#include "bounded_response.h"
#include "transitous_ca.h"
#include ".pio/private_config.h"
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

namespace {
constexpr size_t MaxResponse = 160 * 1024;
struct PsramAllocator {
    void *allocate(size_t size) { return ps_malloc(size); }
    void deallocate(void *ptr) { free(ptr); }
    void *reallocate(void *ptr, size_t size) { return ps_realloc(ptr, size); }
};
using PsramJsonDocument = BasicJsonDocument<PsramAllocator>;

int request(const String &url, const char *ca, uint8_t *data, size_t &length, size_t postLength = 0)
{
    WiFiClientSecure client;
    client.setCACert(ca);
    HTTPClient http;
    http.setConnectTimeout(10000);
    http.setTimeout(10000);
    if (!http.begin(client, url)) return -1;
    http.setUserAgent("LilyGo-Weather-Display/2.5");
    http.addHeader("Accept", "application/json");
    if (postLength) http.addHeader("Content-Type", "application/json");
    int status = postLength ? http.POST(data, postLength) : http.GET();
    length = 0;
    if (status == HTTP_CODE_OK) {
        BoundedResponse response(data, MaxResponse);
        if (http.writeToStream(&response) > 0) length = response.used;
        else status = -1;
    }
    http.end();
    return status;
}

size_t predictionPayload(JsonArrayConst trains, char *buffer, size_t capacity, time_t now)
{
    tm utc;
    char timestamp[32];
    if (!gmtime_r(&now, &utc) || !strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", &utc)) return 0;
    int start = snprintf(buffer, capacity, "{\"ts\":\"%s\",\"station\":\"%s\",\"data\":[", timestamp, RAIL_STATION_ID);
    if (start < 0 || static_cast<size_t>(start) >= capacity) return 0;
    size_t used = start, count = 0;
    for (JsonObjectConst train : trains) {
        if (strcmp(train["direction"] | "", "Departure") || strcmp(train["uic"] | "", RAIL_STATION_ID)) continue;
        size_t bytes = measureJson(train);
        if (used + bytes + (count ? 1 : 0) + 2 >= capacity) return 0;
        if (count++) buffer[used++] = ',';
        used += serializeJson(train, buffer + used, capacity - used);
    }
    if (!count) return 0;
    buffer[used++] = ']'; buffer[used++] = '}';
    return used;
}
}

bool FetchRail(RailDeparture *rows, size_t capacity, size_t &count)
{
    count = 0;
    if (!RAIL_STATION_ID[0]) { Serial.println("Rail station is missing from .env"); return false; }
    uint8_t *board = static_cast<uint8_t *>(ps_malloc(MaxResponse));
    uint8_t *exchange = static_cast<uint8_t *>(ps_malloc(MaxResponse));
    if (!board || !exchange) { free(board); free(exchange); return false; }
    size_t length = 0;
    int status = request(String("https://www.garesetconnexions.sncf/schedule-table/Departures/") + RAIL_STATION_ID,
                         RailRootCA, board, length);
    bool ok = false;
    if (status == HTTP_CODE_OK) {
        PsramJsonDocument doc(160 * 1024);
        if (!deserializeJson(doc, reinterpret_cast<char *>(board), length)) {
            JsonArrayConst departures = doc.is<JsonArray>() ? doc.as<JsonArrayConst>() : doc["data"].as<JsonArrayConst>();
            PsramJsonDocument predictions(64 * 1024);
            size_t payload = predictionPayload(departures, reinterpret_cast<char *>(exchange), MaxResponse, time(nullptr));
            if (payload) {
                size_t responseLength;
                int result = request("https://compute.olie.science/on_voie_tous/predict", TransitousRootCA, exchange, responseLength, payload);
                if (result != HTTP_CODE_OK || deserializeJson(predictions, reinterpret_cast<char *>(exchange), responseLength))
                    Serial.printf("Rail predictions unavailable: %d\n", result);
            }
            ok = ParseRail(departures, predictions["predictions"].as<JsonArrayConst>(), rows, capacity, count,
                           time(nullptr) + RAIL_MINIMUM_SECONDS, RAIL_STATION_ID);
        }
    }
    if (!ok) Serial.printf("Rail departures unavailable: %d\n", status);
    free(exchange);
    free(board);
    return ok;
}
