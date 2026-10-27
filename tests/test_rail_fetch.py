#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise rail requests and prediction failures with mocked HTTPS."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parent.parent
library = root / ".pio/libdeps/T5_4_7Inc_Plus_V2/ArduinoJson/src"
headers = {
    "Arduino.h": r'''
#pragma once
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
using String = std::string;
inline void *ps_malloc(size_t n) { return malloc(n); }
inline void *ps_realloc(void *p, size_t n) { return realloc(p, n); }
struct Stream {
    virtual ~Stream() = default;
    virtual size_t write(uint8_t) = 0;
    virtual size_t write(const uint8_t*, size_t) = 0;
    virtual int available() = 0;
    virtual int read() = 0;
    virtual int peek() = 0;
    virtual void flush() = 0;
};
struct SerialMock { void println(const char*) {} template<class... T> void printf(const char*, T...) {} };
inline SerialMock Serial;
''',
    "WiFiClientSecure.h": r'''
#pragma once
struct WiFiClientSecure { void setCACert(const char *cert) { if (!cert) abort(); } };
''',
    "HTTPClient.h": r'''
#pragma once
#include "Arduino.h"
#include "WiFiClientSecure.h"
#include <vector>
#define HTTP_CODE_OK 200
struct Reply { int status; std::string body; };
extern std::vector<Reply> replies;
extern std::string posted;
extern size_t replyIndex;
struct HTTPClient {
    std::string body;
    void setConnectTimeout(int) {}
    void setTimeout(int) {}
    bool begin(WiFiClientSecure&, const String&) { return true; }
    void setUserAgent(const String&) {}
    void addHeader(const char*, const char*) {}
    int GET() { auto r = replies.at(replyIndex++); body = r.body; return r.status; }
    int POST(uint8_t *data, size_t n) { posted.assign((char*)data, n); return GET(); }
    int writeToStream(Stream *s) { return s->write((const uint8_t*)body.data(), body.size()); }
    void end() {}
};
''',
    ".pio/private_config.h": '#define RAIL_STATION_ID "123"\n#define RAIL_MINIMUM_SECONDS 900\n',
}
harness = r'''
#include <cassert>
#include <string>
#include <vector>
#include "HTTPClient.h"
std::vector<Reply> replies;
std::string posted;
size_t replyIndex = 0;
#include "rail.cpp"
int main() {
    const char *board = R"JSON([
      {"direction":"Arrival","uic":"123"},
      {"direction":"Departure","uic":"123","scheduledTime":"2099-01-01T12:00:00Z","actualTime":"2099-01-01T12:05:00Z","traffic":{"destination":"Example"},"platform":{"track":"B"}},
      {"direction":"Departure","uic":"999"}
    ])JSON";
    const char *prediction = R"JSON({"predictions":[{"probabilities":[{"platform":"A","prob":0.8}]}]})JSON";
    replies = {{200, board}, {200, prediction}};
    RailDeparture rows[9]; size_t count = 0;
    assert(FetchRail(rows, 9, count) && count == 1);
    assert(!strcmp(rows[0].platform, "B | A (80%)"));
    DynamicJsonDocument payload(4096);
    assert(!deserializeJson(payload, posted));
    assert(payload["station"] == "123" && payload["data"].size() == 1);
    assert(payload["data"][0]["traffic"]["destination"] == "Example");
    replies = {{200, board}, {503, ""}}; replyIndex = 0;
    assert(FetchRail(rows, 9, count) && count == 1 && !strcmp(rows[0].platform, "B"));
    replies = {{403, ""}}; replyIndex = 0;
    assert(!FetchRail(rows, 9, count));
    replies = {{200, "{"}}; replyIndex = 0;
    assert(!FetchRail(rows, 9, count));
}
'''
with tempfile.TemporaryDirectory() as directory:
    directory = Path(directory)
    for name, content in headers.items():
        path = directory / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content)
    (directory / "rail.cpp").write_text((root / "rail.cpp").read_text())
    cpp = directory / "test.cpp"
    exe = directory / "test"
    cpp.write_text(harness)
    subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-I", str(directory), "-I", str(root),
                    "-I", str(library), str(cpp), str(root / "rail_parse.cpp"), str(root / "transitous_parse.cpp"),
                    "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print("Rail request and fallback tests passed")
