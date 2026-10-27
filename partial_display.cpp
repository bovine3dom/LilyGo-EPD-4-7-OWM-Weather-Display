// SPDX-License-Identifier: GPL-3.0-only
#include "partial_display.h"

#if EPDPARTIAL
#include <Arduino.h>
#include <EPD_Painter_1bit.h>
#include <epd1b_power.h>
#include <epd_driver.h>
#include <esp_rom_gpio.h>
#include <soc/gpio_sig_map.h>
#include <Preferences.h>
#include <SPIFFS.h>
#include <esp_sleep.h>
#include <time.h>

namespace {
constexpr int Width = 960;
constexpr int Height = 540;
constexpr size_t BitmapSize = Width * Height / 8;
constexpr unsigned FullRefreshAfter = 5;
const char *const ImagePath = "/display-1bit.bin";
const char *const WeatherPath = "/weather-1bit.bin";
EPD_Painter1bit *activeDisplay = nullptr;
bool updatedThisWake = false;

uint32_t checksum(const uint8_t *data)
{
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < BitmapSize; ++i)
        hash = (hash ^ data[i]) * 16777619u;
    return hash;
}

void makeBitmap(const uint8_t *gray, uint8_t *bitmap)
{
    static const uint8_t threshold[4][4] = {
        {0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}
    };
    memset(bitmap, 0, BitmapSize);
    for (int y = 0; y < Height; ++y)
        for (int x = 0; x < Width; ++x)
        {
            uint8_t packed = gray[(y * Width + x) / 2];
            uint8_t shade = x & 1 ? packed >> 4 : packed & 15;
            if (shade == 0 || (shade < 15 && shade <= threshold[y % 4][x % 4]))
                bitmap[(y * Width + x) / 8] |= 0x80 >> (x % 8);
        }
}

bool loadBitmap(const char *path, uint8_t *bitmap, uint32_t expectedHash)
{
    File file = SPIFFS.open(path, "r");
    bool ok = file && file.size() == BitmapSize &&
              file.read(bitmap, BitmapSize) == BitmapSize && checksum(bitmap) == expectedHash;
    file.close();
    return ok;
}
}

bool SaveWeatherImage()
{
    uint8_t *bitmap = static_cast<uint8_t *>(ps_malloc(BitmapSize));
    if (!bitmap) return false;
    Preferences display, weather;
    bool ok = display.begin("display1bit", true) && display.getBool("valid", false) &&
              loadBitmap(ImagePath, bitmap, display.getUInt("hash", 0));
    display.end();
    if (ok && weather.begin("weather1bit", false))
    {
        ok = weather.putBool("valid", false) == 1;
        File file = ok ? SPIFFS.open(WeatherPath, "w") : File();
        uint8_t buffer[512];
        size_t written = 0;
        while (file && written < BitmapSize)
        {
            size_t chunk = min(sizeof(buffer), BitmapSize - written);
            memcpy(buffer, bitmap + written, chunk);
            size_t count = file.write(buffer, chunk);
            written += count;
            if (count != chunk) break;
        }
        ok = written == BitmapSize;
        file.close();
        ok = ok && weather.putUInt("hash", checksum(bitmap)) == sizeof(uint32_t) &&
             weather.putBool("valid", true) == 1;
        weather.end();
    }
    else ok = false;
    free(bitmap);
    return ok;
}

bool LoadImage(const char *path, const char *name, uint8_t *framebuffer)
{
    uint8_t *bitmap = static_cast<uint8_t *>(ps_malloc(BitmapSize));
    if (!bitmap) return false;
    Preferences prefs;
    bool ok = prefs.begin(name, true) && prefs.getBool("valid", false) &&
              loadBitmap(path, bitmap, prefs.getUInt("hash", 0));
    prefs.end();
    if (ok)
        for (size_t i = 0; i < BitmapSize; ++i)
            for (int pair = 0; pair < 4; ++pair)
                framebuffer[i * 4 + pair] = ((bitmap[i] & (0x80 >> (pair * 2))) ? 0 : 0x0F) |
                                             ((bitmap[i] & (0x40 >> (pair * 2))) ? 0 : 0xF0);
    free(bitmap);
    return ok;
}

bool LoadWeatherImage(uint8_t *framebuffer)
{
    return LoadImage(WeatherPath, "weather1bit", framebuffer);
}

bool LoadDisplayedImage(uint8_t *framebuffer)
{
    return LoadImage(ImagePath, "display1bit", framebuffer);
}

void SetBatteryMeasurementPower(bool on)
{
    static epd1b_powerctl_H716 power;
    if (on)
    {
        power.begin(EPD1B_PRESET.shift);
        power.powerOn();
    }
    else
        power.powerOff();
}

void PowerOffPartialDisplay()
{
    if (activeDisplay)
    {
        activeDisplay->end();
        activeDisplay = nullptr;
    }
    const auto &pins = EPD1B_PRESET.shift;
    for (int pin : {pins.data, pins.clk, pins.strobe})
    {
        pinMode(pin, OUTPUT);
        esp_rom_gpio_connect_out_signal(pin, SIG_GPIO_OUT_IDX, false, false);
    }
    epd_poweroff_all();
}

bool UpdatePartialDisplay(const uint8_t *framebuffer, bool morningRefresh)
{
    uint8_t *next = static_cast<uint8_t *>(ps_malloc(BitmapSize));
    uint8_t *previous = static_cast<uint8_t *>(ps_malloc(BitmapSize));
    if (!next || !previous)
    {
        Serial.println("Display bitmap allocation failed");
        free(next);
        free(previous);
        return false;
    }
    makeBitmap(framebuffer, next);

    Preferences prefs;
    if (!prefs.begin("display1bit", false))
    {
        Serial.println("Display state unavailable");
        free(next);
        free(previous);
        return false;
    }
    auto cause = esp_sleep_get_wakeup_cause();
    bool valid = (updatedThisWake || cause == ESP_SLEEP_WAKEUP_TIMER || cause == ESP_SLEEP_WAKEUP_EXT0) &&
                 prefs.getBool("valid", false) && loadBitmap(ImagePath, previous, prefs.getUInt("hash", 0));
    unsigned partials = valid ? prefs.getUInt("partials", 0) : 0;
    time_t now = time(nullptr);
    struct tm localTime;
    localtime_r(&now, &localTime);
    uint32_t today = now >= 1600000000 ? (localTime.tm_year + 1900) * 512 + localTime.tm_yday : prefs.getUInt("full_day", 0);
    bool full = !valid || morningRefresh || partials >= FullRefreshAfter || prefs.getUInt("full_day", 0) != today;
    if (!full && memcmp(previous, next, BitmapSize) == 0)
    {
        Serial.println("Display unchanged");
        prefs.end();
        free(next);
        free(previous);
        return true;
    }

    // Invalidate the saved image before touching the panel or its file.
    bool ok = prefs.putBool("valid", false) == 1;
    if (ok)
    {
        // Keep the driver alive for multiple updates in this wake cycle.
        static EPD_Painter1bit display(EPD1B_PRESET);
        if (!activeDisplay)
        {
            display.setAutoShutdown(false);
            display.setIdleTimeout(0);
            if (display.begin()) activeDisplay = &display;
        }
        ok = activeDisplay != nullptr;
        if (ok)
        {
            Serial.printf("Display %s refresh\n", full ? "full" : "partial");
            if (full)
                display.clear();
            else
                display.adoptScreen(previous);
            display.paint(next);
            partials = full ? 0 : partials + 1;

            bool removed = !SPIFFS.exists(ImagePath) || SPIFFS.remove(ImagePath);
            File file = removed ? SPIFFS.open(ImagePath, "w") : File();
            size_t written = 0;
            uint8_t buffer[512];
            while (file && written < BitmapSize)
            {
                size_t chunk = min(sizeof(buffer), BitmapSize - written);
                memcpy(buffer, next + written, chunk);
                size_t count = file.write(buffer, chunk);
                written += count;
                if (count != chunk)
                    break;
            }
            ok = written == BitmapSize;
            if (file)
                file.flush();
            file.close();
            ok = ok && prefs.putUInt("hash", checksum(next)) == sizeof(uint32_t) &&
                 prefs.putUInt("partials", partials) == sizeof(uint32_t) &&
                 (!full || prefs.putUInt("full_day", today) == sizeof(uint32_t)) &&
                 prefs.putBool("valid", true) == 1;
        }
    }
    if (ok) updatedThisWake = true;
    else Serial.println("Display update or state save failed; a full refresh will be needed");
    prefs.end();
    free(next);
    free(previous);
    return ok;
}
#endif
