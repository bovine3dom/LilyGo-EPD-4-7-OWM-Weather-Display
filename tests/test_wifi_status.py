#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check the connection warning and intentional Wi-Fi shutdown."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parent.parent
source = (root / "LilyGo-EPD-4-7-OWM-Weather-Display.ino").read_text()

def function(signature):
    start = source.index(signature)
    pos = source.index("{", start)
    depth, end = 1, pos + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]

harness = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
using String = std::string;
#define WL_CONNECTED 1
#define WL_DISCONNECTED 6
#define WIFI_STA 1
#define WIFI_OFF 0
#define Black 0
#define White 255
bool WiFiConnectionFailed = false, canConnect = true, reportAP = true;
char CachedWiFiSSID[33] = {};
uint8_t CachedWiFiBSSID[6] = {};
int32_t CachedWiFiChannel = 0;
uint8_t ap[6] = {2, 1, 2, 3, 4, 5};
int apChannel = 6, wifi_signal = 0, lines = 0, circles = 0, clears = 0;
unsigned long ticks = 0;
String ssid = "network", password = "password";
struct Attempt { int channel; bool bssid; unsigned long timeout; };
std::vector<Attempt> attempts;
struct WiFiMock {
    bool on = false, connected = false;
    int channelRequested = 0; bool pinned = false;
    void disconnect(bool = true) { connected = false; }
    void mode(int m) { on = m == WIFI_STA; if (!on) connected = false; }
    void setAutoConnect(bool) {}
    void setAutoReconnect(bool) {}
    void begin(const char*, const char*, int channel = 0, const uint8_t *bssid = nullptr) {
        channelRequested = channel; pinned = bssid != nullptr;
        connected = on && canConnect && (!pinned || (channel == apChannel && !memcmp(bssid, ap, 6)));
    }
    int waitForConnectResult(unsigned long timeout = 60000) {
        attempts.push_back({channelRequested, pinned, timeout});
        ticks += connected ? (pinned ? 200 : 900) : 5000;
        return status();
    }
    int status() { return connected ? WL_CONNECTED : WL_DISCONNECTED; }
    int RSSI() { return -55; }
    const uint8_t *BSSID() { return reportAP ? ap : nullptr; }
    int channel() { return apChannel; }
} WiFi;
struct { void println(const String&) {} template<class... T> void printf(const char*, T...) {} } Serial;
unsigned long millis() { return ticks; }
void fillRect(int x, int y, int w, int h, int color) { assert(x == 544 && y == 9 && w == 29 && h == 25 && color == White); ++clears; }
void drawLine(int x1, int y1, int x2, int y2, int) { assert(x1 >= 544 && x2 <= 573 && y1 >= 9 && y2 <= 34); ++lines; }
void fillCircle(int x, int y, int r, int) { assert(x-r >= 544 && x+r <= 573 && y-r >= 9 && y+r <= 34); ++circles; }
'''
harness += function("uint8_t StartWiFi()") + "\n" + function("void StopWiFi()") + "\n" + function("void DrawWiFiStatus()")
harness += r'''
int main() {
    // First connection scans and records the access point.
    assert(StartWiFi() == WL_CONNECTED && !WiFiConnectionFailed && wifi_signal == -55);
    assert(attempts.size() == 1 && !attempts[0].bssid && attempts[0].timeout == 60000);
    assert(CachedWiFiChannel == 6 && !strcmp(CachedWiFiSSID, "network") && !memcmp(CachedWiFiBSSID, ap, 6));
    StopWiFi(); assert(!WiFiConnectionFailed);
    DrawWiFiStatus(); assert(clears == 1 && lines == 0 && circles == 0);
    // RTC cache survives sleep; a matching AP uses the fast attempt only.
    attempts.clear(); assert(StartWiFi() == WL_CONNECTED);
    assert(attempts.size() == 1 && attempts[0].bssid && attempts[0].channel == 6 && attempts[0].timeout == 5000);
    StopWiFi();
    // A changed channel or BSSID falls back to a normal scan and refreshes the cache.
    apChannel = 11; attempts.clear(); assert(StartWiFi() == WL_CONNECTED);
    assert(attempts.size() == 2 && attempts[0].bssid && !attempts[1].bssid && attempts[1].timeout == 60000);
    assert(CachedWiFiChannel == 11); StopWiFi();
    ap[5] = 9; attempts.clear(); assert(StartWiFi() == WL_CONNECTED);
    assert(attempts.size() == 2 && attempts[0].bssid && !attempts[1].bssid && CachedWiFiBSSID[5] == 9); StopWiFi();
    // If both attempts fail, the warning remains visible and the stale cache is discarded.
    canConnect = false; attempts.clear(); assert(StartWiFi() != WL_CONNECTED && WiFiConnectionFailed);
    assert(attempts.size() == 2 && CachedWiFiChannel == 0);
    StopWiFi(); assert(WiFiConnectionFailed);
    DrawWiFiStatus(); assert(clears == 2 && lines > 0 && circles == 1);
    attempts.clear(); assert(StartWiFi() != WL_CONNECTED && attempts.size() == 1 && !attempts[0].bssid);
    StopWiFi();
    lines = circles = 0; canConnect = true;
    assert(StartWiFi() == WL_CONNECTED && !WiFiConnectionFailed); StopWiFi();
    DrawWiFiStatus(); assert(clears == 3 && lines == 0 && circles == 0);
    // A changed SSID bypasses the old cache. Missing AP data disables caching.
    ssid = "another"; attempts.clear(); assert(StartWiFi() == WL_CONNECTED);
    assert(attempts.size() == 1 && !attempts[0].bssid && !strcmp(CachedWiFiSSID, "another")); StopWiFi();
    reportAP = false; attempts.clear(); assert(StartWiFi() == WL_CONNECTED && CachedWiFiChannel == 0); StopWiFi();
    reportAP = true; ssid = std::string(33, 'a'); attempts.clear();
    assert(StartWiFi() == WL_CONNECTED && CachedWiFiChannel == 0); StopWiFi();
}
'''
with tempfile.TemporaryDirectory() as directory:
    cpp = Path(directory) / "wifi.cpp"
    executable = Path(directory) / "wifi"
    cpp.write_text(harness)
    subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", str(cpp), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
print("Wi-Fi warning and shutdown tests passed")
