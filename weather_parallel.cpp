// SPDX-License-Identifier: GPL-3.0-only
#include "weather_parallel.h"
#include ".pio/private_config.h"
#include <Arduino.h>
#include <WiFiClient.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

bool obtainWeatherData(WiFiClient &client, const String &type);

namespace {
struct Pending {
    TaskHandle_t parent;
    SnorkelData *snorkel;
    uint32_t elapsed = 0;
};

void snorkelTask(void *arg)
{
    auto *pending = static_cast<Pending *>(arg);
    uint32_t started = millis();
    FetchSnorkel(*pending->snorkel);
    pending->elapsed = millis() - started;
    xTaskNotifyGive(pending->parent);
    vTaskDelete(nullptr);
}
}

void FetchWeatherParallel(bool &current, bool &forecast, SnorkelData *snorkel)
{
    current = forecast = false;
    Pending pending{xTaskGetCurrentTaskHandle(), snorkel};
    // The setup task owns its notification slot while the worker runs.
    ulTaskNotifyTake(pdTRUE, 0);
    uint32_t started = millis();
    bool startedSnorkel = snorkel && xTaskCreate(snorkelTask, "snorkel", 8192, &pending, 1, nullptr) == pdPASS;
    WiFiClient client;
    for (int attempt = 0; attempt < 2 && (!current || !forecast); ++attempt)
    {
        if (!current) current = obtainWeatherData(client, "weather");
        if (!forecast) forecast = obtainWeatherData(client, "forecast");
    }
    uint32_t weatherMs = millis() - started;
    if (startedSnorkel) ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    else if (snorkel && current && forecast)
    {
        uint32_t snorkelStart = millis();
        FetchSnorkel(*snorkel);
        pending.elapsed = millis() - snorkelStart;
    }
#if DEBUG_SERIAL_WAKE_SECONDS > 0
    Serial.printf("Parallel requests: weather=%lu snorkel=%lu total=%lu ms\n",
                  (unsigned long)weatherMs, (unsigned long)pending.elapsed,
                  (unsigned long)(millis() - started));
#endif
}
