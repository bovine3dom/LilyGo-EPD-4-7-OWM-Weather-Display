// SPDX-License-Identifier: GPL-3.0-only
#include "snorkel.h"
#include "bounded_response.h"
#include "transitous_ca.h"
#include ".pio/private_config.h"
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>

bool FetchSnorkel(SnorkelData &data)
{
    data = SnorkelData{};
    if (!SNORKEL_URL[0]) return false;
    constexpr size_t capacity = 16 * 1024;
    uint8_t *buffer = static_cast<uint8_t *>(ps_malloc(capacity));
    if (!buffer) return false;
    WiFiClient plain;
    WiFiClientSecure secure;
    String url(SNORKEL_URL);
    WiFiClient *client = &plain;
    if (url.startsWith("https://")) {
        secure.setCACert(TransitousRootCA);
        client = &secure;
    }
    HTTPClient http;
    http.setConnectTimeout(3000);
    http.setTimeout(15000);
    bool ok = false;
    if (http.begin(*client, url)) {
        http.addHeader("Accept", "application/json");
        int status = http.GET();
        if (status == HTTP_CODE_OK) {
            BoundedResponse response(buffer, capacity);
            if (http.writeToStream(&response) > 0)
                ok = ParseSnorkel(reinterpret_cast<char *>(buffer), response.used, data);
        }
        if (ok) Serial.printf("Snorkel data: waves=%d, water=%d, jellies=%d\n", data.wavesAvailable, data.waterAvailable, data.jelliesAvailable);
        else Serial.printf("Snorkel data unavailable: %d\n", status);
        http.end();
    }
    free(buffer);
    return ok;
}
