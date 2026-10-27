#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise stop caching and recovery with mocked HTTP responses."""
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
#define RTC_DATA_ATTR
inline void *ps_malloc(size_t n) { return malloc(n); }
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
struct WiFiClientSecure { void setCACert(const char*) {} };
''',
    "HTTPClient.h": r'''
#pragma once
#include "Arduino.h"
#include "WiFiClientSecure.h"
#include <cassert>
#include <vector>
#define HTTP_CODE_OK 200
struct Reply { int status; std::string body; };
extern std::vector<Reply> replies;
extern std::vector<std::string> urls;
extern size_t replyIndex;
struct HTTPClient {
    std::string url, agent, body;
    void setConnectTimeout(int) {}
    void setTimeout(int) {}
    bool begin(WiFiClientSecure&, const String &value) { url = value; return true; }
    void setUserAgent(const String &value) { agent = value; }
    int GET() {
        assert(agent.find("test@example.org") != std::string::npos);
        assert(replyIndex < replies.size());
        urls.push_back(url);
        Reply r = replies[replyIndex++]; body = r.body;
        return r.status;
    }
    int writeToStream(Stream *stream) { return stream->write(reinterpret_cast<const uint8_t*>(body.data()), body.size()); }
    void end() {}
};
''',
    ".pio/private_config.h": '''#define TRANSITOUS_CONTACT "test@example.org"
#define TRAM_STOP_NAME "Sample Stop"
#define TRAM_STOP_LATITUDE 51.5
#define TRAM_STOP_LONGITUDE -0.12
#define TRAM_STOP_BBOX "min=51.495%2C-0.125&max=51.505%2C-0.115"
#define TRAM_MINIMUM_SECONDS 900
''',
}
harness = r'''
#include <cassert>
#include <cstring>
#include <string>
#include <vector>
#include "HTTPClient.h"
std::vector<Reply> replies;
std::vector<std::string> urls;
size_t replyIndex = 0;
#include "transitous.cpp"
std::string stops(const char *id) {
    return std::string(R"JSON([{"name":"Sample Stop","lat":51.5000,"lon":-0.1200,"modes":["TRAM"],"stopId":")JSON") + id + R"JSON("}])JSON";
}
const char *board = R"JSON({"place":{"name":"Sample Stop","lat":51.5000,"lon":-0.1200},"stopTimes":[]})JSON";
const char *wrong = R"JSON({"place":{"name":"Sample Stop","lat":48.85,"lon":2.35},"stopTimes":[]})JSON";
int main() {
    TramDeparture rows[8]; size_t count;
    replies = {{200, stops("feed:one")}, {200, board}};
    assert(FetchTransitous(rows, 8, count) && count == 0 && urls.size() == 2);
    assert(urls[0].find("/map/stops?") != std::string::npos);
    assert(urls[1].find("stopId=feed%3Aone") != std::string::npos);
    replies.push_back({200, board});
    assert(FetchTransitous(rows, 8, count) && urls.size() == 3);
    replies.insert(replies.end(), {{404, ""}, {200, stops("feed:two")}, {200, board}});
    assert(FetchTransitous(rows, 8, count) && urls.size() == 6);
    assert(urls[5].find("stopId=feed%3Atwo") != std::string::npos);
    replies.insert(replies.end(), {{200, wrong}, {200, stops("feed:three")}, {200, board}});
    assert(FetchTransitous(rows, 8, count) && urls.size() == 9);
    assert(urls[8].find("stopId=feed%3Athree") != std::string::npos);
    cachedStopUntil = time(nullptr) - 1;
    replies.insert(replies.end(), {{500, ""}, {200, board}});
    assert(FetchTransitous(rows, 8, count) && urls.size() == 11);
    replies.push_back({200, board});
    assert(FetchTransitous(rows, 8, count) && urls.size() == 12);
    cachedStopId[0] = '\0'; cachedStopUntil = 0;
    replies.push_back({500, ""});
    assert(!FetchTransitous(rows, 8, count) && urls.size() == 13);
    replies.insert(replies.end(), {{200, stops("feed:four")}, {404, ""}, {200, stops("feed:four")}});
    assert(!FetchTransitous(rows, 8, count) && urls.size() == 16 && !cachedStopId[0]);
}
'''
with tempfile.TemporaryDirectory() as directory:
    directory = Path(directory)
    for name, content in headers.items():
        path = directory / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content)
    (directory / "transitous.cpp").write_text((root / "transitous.cpp").read_text())
    cpp = directory / "test.cpp"
    executable = directory / "test"
    cpp.write_text(harness)
    subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-I", str(directory), "-I", str(root),
                    "-I", str(library), str(cpp), str(root / "transitous_parse.cpp"), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
print("Transitous stop cache tests passed")
