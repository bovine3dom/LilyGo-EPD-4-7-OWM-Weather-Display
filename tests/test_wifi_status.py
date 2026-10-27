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
#include <string>
using String = std::string;
#define WL_CONNECTED 1
#define WIFI_STA 1
#define WIFI_OFF 0
#define Black 0
#define White 255
bool WiFiConnectionFailed = false, canConnect = true;
int wifi_signal = 0, lines = 0, circles = 0, clears = 0;
String ssid = "network", password = "password";
struct IPAddress { IPAddress(int, int, int, int) {} String toString() { return "address"; } };
struct WiFiMock {
    bool on = false, connected = false;
    void disconnect(bool = true) { connected = false; }
    void mode(int m) { on = m == WIFI_STA; }
    void setAutoConnect(bool) {}
    void setAutoReconnect(bool) {}
    void begin(const char*, const char*) { connected = on && canConnect; }
    int waitForConnectResult() { return status(); }
    int status() { return connected ? WL_CONNECTED : 0; }
    int RSSI() { return -55; }
    IPAddress localIP() { return {0, 0, 0, 0}; }
} WiFi;
struct { void println(const String&) {} template<class... T> void printf(const char*, T...) {} } Serial;
void delay(int) {}
void fillRect(int x, int y, int w, int h, int color) { assert(x == 544 && y == 9 && w == 29 && h == 25 && color == White); ++clears; }
void drawLine(int x1, int y1, int x2, int y2, int) { assert(x1 >= 544 && x2 <= 573 && y1 >= 9 && y2 <= 34); ++lines; }
void fillCircle(int x, int y, int r, int) { assert(x-r >= 544 && x+r <= 573 && y-r >= 9 && y+r <= 34); ++circles; }
'''
harness += function("uint8_t StartWiFi()") + "\n" + function("void StopWiFi()") + "\n" + function("void DrawWiFiStatus()")
harness += r'''
int main() {
    canConnect = false;
    assert(StartWiFi() != WL_CONNECTED && WiFiConnectionFailed);
    StopWiFi(); assert(WiFiConnectionFailed);
    DrawWiFiStatus(); assert(clears == 1 && lines > 0 && circles == 1);
    lines = circles = 0; canConnect = true;
    assert(StartWiFi() == WL_CONNECTED && !WiFiConnectionFailed && wifi_signal == -55);
    StopWiFi(); assert(!WiFiConnectionFailed);
    DrawWiFiStatus(); assert(clears == 2 && lines == 0 && circles == 0);
}
'''
with tempfile.TemporaryDirectory() as directory:
    cpp = Path(directory) / "wifi.cpp"
    executable = Path(directory) / "wifi"
    cpp.write_text(harness)
    subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", str(cpp), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
print("Wi-Fi warning and shutdown tests passed")
