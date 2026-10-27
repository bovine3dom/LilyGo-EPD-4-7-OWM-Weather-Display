#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise the firmware scheduler with local times and DST transitions."""

from pathlib import Path
import subprocess
import tempfile

source = (Path(__file__).resolve().parent.parent / "LilyGo-EPD-4-7-OWM-Weather-Display.ino").read_text()
assert "ConvertUnixTime(WxForecast[index].Dt)" in source


def function(signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


harness = r'''
#include <cassert>
#include <cstdlib>
#include <ctime>
#include <cstdio>
#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
using std::min;
using std::max;
#define DEBUG_FAKE_WEATHER 0
std::map<std::string, uint64_t> saved;
bool prefOk = true;
struct Preferences {
    bool begin(const char*, bool) { return prefOk; }
    uint32_t getUInt(const char *key, uint32_t fallback) { return saved.count(key) ? saved[key] : fallback; }
    uint64_t getULong64(const char *key, uint64_t fallback) { return saved.count(key) ? saved[key] : fallback; }
    size_t putUInt(const char *key, uint32_t value) { saved[key] = value; return sizeof(value); }
    size_t putULong64(const char *key, uint64_t value) { saved[key] = value; return sizeof(value); }
    void end() {}
};
const int WakeupHour = 8, SleepHour = 21;
int UpdateIntervalHours = 1, CurrentHour = 0, CurrentMin = 0;
long Delta = 30;
const int ESP_SLEEP_WAKEUP_TIMER = 1;
int wakeCause = ESP_SLEEP_WAKEUP_TIMER;
int esp_sleep_get_wakeup_cause() { return wakeCause; }
'''
harness += "\n".join(function(signature) for signature in (
    "int BatteryUpdateInterval(", "bool IsWeatherHour(", "bool IsScheduledUpdate(", "long SecondsUntilNextUpdate(",
    "long NextSleepSeconds(", "uint32_t WeatherSlot(", "const char *WeatherScheduleNamespace(",
    "bool ClaimWeatherAttempt(", "void MarkWeatherSuccess(", "long WeatherRetrySeconds(", "float SumOfPrecip("
))
harness += r'''
static time_t local(int year, int month, int day, int hour, int minute) {
    tm t = {}; t.tm_year = year - 1900; t.tm_mon = month - 1; t.tm_mday = day;
    t.tm_hour = hour; t.tm_min = minute; t.tm_isdst = -1; return mktime(&t);
}
int main() {
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1); tzset();
    tm utc = {}; utc.tm_year = 2027 - 1900; utc.tm_mon = 2; utc.tm_mday = 28;
    utc.tm_hour = 0; utc.tm_min = 59; time_t transition = timegm(&utc);
    tm before, after; localtime_r(&transition, &before); transition += 60; localtime_r(&transition, &after);
    assert(before.tm_hour == 1 && before.tm_min == 59 && before.tm_isdst == 0);
    assert(after.tm_hour == 3 && after.tm_min == 0 && after.tm_isdst > 0);
    utc.tm_mon = 9; utc.tm_mday = 31; transition = timegm(&utc);
    localtime_r(&transition, &before); transition += 60; localtime_r(&transition, &after);
    assert(before.tm_hour == 2 && before.tm_min == 59 && before.tm_isdst > 0);
    assert(after.tm_hour == 2 && after.tm_min == 0 && after.tm_isdst == 0);
    assert(BatteryUpdateInterval(4.20f) == 1);
    assert(BatteryUpdateInterval(4.15f) == 1);
    assert(BatteryUpdateInterval(4.14f) == 6);
    assert(BatteryUpdateInterval(3.95f) == 6);
    assert(BatteryUpdateInterval(3.94f) == 12);
    assert(BatteryUpdateInterval(3.75f) == 12);
    assert(BatteryUpdateInterval(3.74f) == 24);
    assert(BatteryUpdateInterval(0.0f) == 1);
    float rain[25]; for (int i = 0; i < 24; ++i) rain[i] = 1; rain[24] = 1000;
    assert(SumOfPrecip(rain, 24) == 24);
    CurrentMin = 0;
    for (int hour = 0; hour < 24; ++hour) {
        CurrentHour = hour;
        assert(IsScheduledUpdate() == (hour >= 8 && hour <= 21));
    }
    CurrentHour = 12; CurrentMin = 2; assert(IsScheduledUpdate());
    wakeCause = 0; CurrentHour = 23; assert(IsScheduledUpdate());
    wakeCause = ESP_SLEEP_WAKEUP_TIMER; CurrentMin = 0;
    UpdateIntervalHours = 6;
    for (int hour = 0; hour < 24; ++hour) {
        CurrentHour = hour;
        assert(IsScheduledUpdate() == (hour == 8 || hour == 14 || hour == 20));
    }
    assert(SecondsUntilNextUpdate(local(2027, 1, 1, 8, 0)) == 6 * 3600 + 30);
    assert(SecondsUntilNextUpdate(local(2027, 1, 1, 20, 0)) == 12 * 3600 + 30);
    UpdateIntervalHours = 12;
    assert(SecondsUntilNextUpdate(local(2027, 1, 1, 8, 0)) == 12 * 3600 + 30);
    UpdateIntervalHours = 24;
    assert(SecondsUntilNextUpdate(local(2027, 1, 1, 8, 0)) == 24 * 3600 + 30);
    UpdateIntervalHours = 1;
    assert(SecondsUntilNextUpdate(local(2027, 1, 1, 7, 0)) == 3600 + 30);
    assert(SecondsUntilNextUpdate(local(2027, 1, 1, 20, 0)) == 3600 + 30);
    assert(SecondsUntilNextUpdate(local(2027, 1, 1, 21, 0)) == 11 * 3600 + 30);
    assert(SecondsUntilNextUpdate(local(2027, 3, 27, 21, 0)) == 10 * 3600 + 30);
    assert(SecondsUntilNextUpdate(local(2027, 10, 30, 21, 0)) == 12 * 3600 + 30);
    time_t now = local(2027, 1, 1, 8, 5);
    assert(ClaimWeatherAttempt(now));
    assert(!ClaimWeatherAttempt(now + 30));
    assert(WeatherRetrySeconds(now + 30) == 870);
    assert(ClaimWeatherAttempt(now + 900));
    MarkWeatherSuccess(now + 900);
    assert(!ClaimWeatherAttempt(now + 1800));
    assert(WeatherRetrySeconds(now + 1800) == 0);
    assert(ClaimWeatherAttempt(now + 3600));
    assert(WeatherRetrySeconds(now + 3630) == 870);
    UpdateIntervalHours = 6; assert(WeatherRetrySeconds(now + 3630) == 0); UpdateIntervalHours = 1;
    prefOk = false; assert(!ClaimWeatherAttempt(now + 7200)); prefOk = true;
    assert(NextSleepSeconds(now, 3600, false, false, false, 0, 0, 0) == 3600);
    assert(NextSleepSeconds(now, 3600, false, false, false, 0, 0, 30) == 30);
    assert(NextSleepSeconds(now, 3600, true, false, false, now + 60, now + 300, 0) == 60);
    assert(NextSleepSeconds(now, 3600, true, false, false, now + 60, now + 20, 0) == 20);
    assert(NextSleepSeconds(now, 3600, true, false, false, now - 1, now + 300, 0) == 1);
    assert(NextSleepSeconds(0, 3600, true, false, false, 0, 0, 0) == 60);
    assert(NextSleepSeconds(now, 3600, false, true, false, 0, 0, 0) == 60);
    assert(NextSleepSeconds(now, 3600, false, false, true, 0, 0, 0) == 1);
    puts("Schedule and DST transition tests passed");
}
'''
with tempfile.TemporaryDirectory() as directory:
    cpp = Path(directory) / "schedule.cpp"
    executable = Path(directory) / "schedule"
    cpp.write_text(harness)
    subprocess.run(["g++", "-std=c++11", "-Wall", "-Wextra", str(cpp), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
