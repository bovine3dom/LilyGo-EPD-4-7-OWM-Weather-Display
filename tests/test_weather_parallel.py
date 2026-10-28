#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise parallel requests and failure fallbacks with host mocks."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parent.parent
headers = {
    "Arduino.h": r'''
#pragma once
#include <chrono>
#include <cstdint>
#include <string>
using String = std::string;
inline uint32_t millis() { using namespace std::chrono; return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count(); }
struct SerialMock { template<class... T> void printf(const char*, T...) {} };
inline SerialMock Serial;
''',
    "WiFiClient.h": '#pragma once\nstruct WiFiClient {};\n',
    "freertos/FreeRTOS.h": r'''
#pragma once
#include <cstdint>
using TaskHandle_t = void*;
using BaseType_t = int;
#define pdPASS 1
#define pdTRUE 1
#define portMAX_DELAY 0xffffffffu
''',
    "freertos/task.h": r'''
#pragma once
#include "FreeRTOS.h"
TaskHandle_t xTaskGetCurrentTaskHandle();
BaseType_t xTaskCreate(void (*task)(void*), const char *name, unsigned, void *arg, int, TaskHandle_t*);
void xTaskNotifyGive(TaskHandle_t);
uint32_t ulTaskNotifyTake(BaseType_t, uint32_t timeout);
void vTaskDelete(TaskHandle_t);
''',
    ".pio/private_config.h": '#define DEBUG_SERIAL_WAKE_SECONDS 30\n',
}
harness = r'''
#include "weather_parallel.h"
#include "Arduino.h"
#include "WiFiClient.h"
#include "freertos/task.h"
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

bool fail_task = false;
int weatherCalls = 0, forecastCalls = 0, snorkelCalls = 0, weatherFailures = 0, forecastFailures = 0;
std::atomic<int> active{0}, peak{0};
std::mutex mutex;
std::condition_variable cv;
bool notified = false;
std::thread worker;
void request() {
    int n = ++active, old = peak.load();
    while (old < n && !peak.compare_exchange_weak(old, n)) {}
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    --active;
}
TaskHandle_t xTaskGetCurrentTaskHandle() { return reinterpret_cast<void*>(1); }
BaseType_t xTaskCreate(void (*task)(void*), const char*, unsigned, void *arg, int, TaskHandle_t*) {
    if (fail_task) return 0;
    worker = std::thread([=] { task(arg); }); return pdPASS;
}
void xTaskNotifyGive(TaskHandle_t) { { std::lock_guard<std::mutex> lock(mutex); notified = true; } cv.notify_one(); }
uint32_t ulTaskNotifyTake(BaseType_t, uint32_t timeout) {
    std::unique_lock<std::mutex> lock(mutex);
    if (!timeout && !notified) return 0;
    cv.wait(lock, [] { return notified; }); notified = false; return 1;
}
void vTaskDelete(TaskHandle_t) {}
bool obtainWeatherData(WiFiClient&, const String &type) {
    request();
    if (type == "weather") return ++weatherCalls > weatherFailures;
    assert(type == "forecast"); return ++forecastCalls > forecastFailures;
}
bool FetchSnorkel(SnorkelData &data) { request(); ++snorkelCalls; data.jelliesAvailable = true; return true; }
void reset() {
    if (worker.joinable()) worker.join();
    fail_task = false; weatherCalls = forecastCalls = snorkelCalls = weatherFailures = forecastFailures = 0;
    active = peak = 0; notified = false;
}
void run(bool &current, bool &forecast, SnorkelData *snorkel) {
    FetchWeatherParallel(current, forecast, snorkel);
    if (worker.joinable()) worker.join();
}
int main() {
    bool current, forecast; SnorkelData data;
    run(current, forecast, &data);
    assert(current && forecast && data.jelliesAvailable && peak == 2);
    assert(weatherCalls == 1 && forecastCalls == 1 && snorkelCalls == 1);
    reset(); weatherFailures = forecastFailures = 1;
    run(current, forecast, &data);
    assert(current && forecast && weatherCalls == 2 && forecastCalls == 2 && snorkelCalls == 1);
    reset(); weatherFailures = 2;
    run(current, forecast, &data);
    assert(!current && forecast && weatherCalls == 2 && forecastCalls == 1 && snorkelCalls == 1);
    reset(); forecastFailures = 2;
    run(current, forecast, &data);
    assert(current && !forecast && weatherCalls == 1 && forecastCalls == 2 && snorkelCalls == 1);
    reset(); fail_task = true;
    run(current, forecast, &data);
    assert(current && forecast && peak == 1 && snorkelCalls == 1);
    reset(); fail_task = true; weatherFailures = 2;
    run(current, forecast, &data);
    assert(!current && forecast && snorkelCalls == 0);
    reset(); run(current, forecast, nullptr);
    assert(current && forecast && snorkelCalls == 0);
}
'''
with tempfile.TemporaryDirectory() as directory:
    directory = Path(directory)
    for name, content in headers.items():
        path = directory / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content)
    (directory / "weather_parallel.cpp").write_text((root / "weather_parallel.cpp").read_text())
    cpp, exe = directory / "test.cpp", directory / "test"
    cpp.write_text(harness)
    subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-pthread", "-I", str(directory),
                    "-I", str(root), str(cpp), str(directory / "weather_parallel.cpp"), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print("Parallel request and fallback tests passed")
