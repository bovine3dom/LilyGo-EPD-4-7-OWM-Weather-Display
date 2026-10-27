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

bool loadBitmap(uint8_t *bitmap, uint32_t expectedHash)
{
    File file = SPIFFS.open(ImagePath, "r");
    bool ok = file && file.size() == BitmapSize &&
              file.read(bitmap, BitmapSize) == BitmapSize && checksum(bitmap) == expectedHash;
    file.close();
    return ok;
}
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
    bool valid = esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER &&
                 prefs.getBool("valid", false) && loadBitmap(previous, prefs.getUInt("hash", 0));
    unsigned partials = valid ? prefs.getUInt("partials", 0) : 0;
    time_t now = time(nullptr);
    struct tm localTime;
    localtime_r(&now, &localTime);
    uint32_t today = (localTime.tm_year + 1900) * 512 + localTime.tm_yday;
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
        // The driver's idle task retains this pointer until deep sleep.
        static EPD_Painter1bit display(EPD1B_PRESET);
        display.setAutoShutdown(false);
        display.setIdleTimeout(0);
        ok = display.begin();
        if (ok)
        {
            Serial.printf("Display %s refresh\n", full ? "full" : "partial");
            if (full)
                display.clear();
            else
                display.adoptScreen(previous);
            display.paint(next);
            display.end();
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
    if (!ok)
        Serial.println("Display update or state save failed; a full refresh will be needed");
    prefs.end();
    free(next);
    free(previous);
    return ok;
}
#endif
