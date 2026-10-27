// ESP32 Weather Display and a LilyGo EPD 4.7" Display, obtains Open Weather Map data, decodes and then displays it.
// This software, the ideas and concepts is Copyright (c) David Bird 2021. All rights to this software are reserved.
// #################################################################################################################

#include <Arduino.h>           // In-built
#include <esp_task_wdt.h>      // In-built
#include "freertos/FreeRTOS.h" // In-built
#include "freertos/task.h"     // In-built
#include "epd_driver.h"        // https://github.com/Xinyuan-LilyGO/LilyGo-EPD47
#include "esp_adc_cal.h"       // In-built
#include <driver/rtc_io.h>

#include <ArduinoJson.h> // https://github.com/bblanchon/ArduinoJson
#include <HTTPClient.h>  // In-built

#include <WiFi.h> // In-built
#include <Preferences.h>
#include <SPI.h>  // In-built
#include <time.h> // In-built
#include "SPIFFS.h"
#include "FS.h"

#include ".pio/private_config.h"
#include "owm_credentials.h"
#include "forecast_record.h"
#include "partial_display.h"
#include "transitous.h"
#include "lang.h"

#define SCREEN_WIDTH EPD_WIDTH
#define SCREEN_HEIGHT EPD_HEIGHT

//################  VERSION  ##################################################
String version = "2.5 / 4.7in"; // Programme version, see change log at end
//################ VARIABLES ##################################################

enum alignment
{
    LEFT,
    RIGHT,
    CENTER
};

#define White     0xFF
#define LightGrey 0xBB
#define Grey      0x88
#define DarkGrey  0x44
#define Black     0x00

#define autoscale_on  true
#define autoscale_off false
#define barchart_on   true
#define barchart_off  false

boolean LargeIcon = true;
boolean SmallIcon = false;
#define Large 20 // For icon drawing
#define Small 8  // For icon drawing
String Time_str = "--:--:--";
String Date_str = "-- --- ----";
int wifi_signal, CurrentHour = 0, CurrentMin = 0, CurrentSec = 0, EventCnt = 0;
float BatteryVoltage = 0.0f;
int64_t BatteryLifeEstimateSeconds = -1;
//################ PROGRAM VARIABLES and OBJECTS ##########################################
#define max_readings 24 // Limited to 3-days here, but could go to 5-days = 40
const int ForecastWidth = 90;

Forecast_record_type WxConditions[1];
Forecast_record_type WxForecast[max_readings];

float pressure_readings[max_readings]    = {0};
float temperature_readings[max_readings] = {0};
float humidity_readings[max_readings]    = {0};
float rain_readings[max_readings]        = {0};
float snow_readings[max_readings]        = {0};

// Refresh from 08:00 through 21:00. Battery tiers use 1, 6, 12, or 24 hours.
const int WakeupHour = 8;
const int SleepHour = 21;
int UpdateIntervalHours = 1;
bool AwakeTramButtonPressed = false;
const long RetrySleepSeconds = 60 * 60;
long StartTime = 0;
long SleepTimer = 0;
long Delta = 30; // ESP32 RTC speed compensation

#include "fonts/spleen.h"

GFXfont currentFont;
uint8_t *framebuffer;

float ReadBatteryVoltage()
{
#if CONFIG_IDF_TARGET_ESP32
    const uint8_t pin = 36;
    const adc_unit_t unit = ADC_UNIT_1;
#else
    const uint8_t pin = 14;
    const adc_unit_t unit = ADC_UNIT_2;
#endif
    analogSetPinAttenuation(pin, ADC_11db);
    esp_adc_cal_characteristics_t adc_chars;
    esp_adc_cal_value_t calibration = esp_adc_cal_characterize(unit, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, 1100, &adc_chars);
    if (calibration == ESP_ADC_CAL_VAL_NOT_SUPPORTED)
    {
        Serial.println("Battery ADC calibration unavailable");
        return 0.0f;
    }
#if EPDPARTIAL
    SetBatteryMeasurementPower(true);
#else
    epd_poweron();
#endif
    delay(10);
    uint32_t raw = 0;
    for (int sample = 0; sample < 8; ++sample)
    {
        raw += analogRead(pin);
        delay(2);
    }
    uint32_t averageRaw = raw / 8;
#if EPDPARTIAL
    SetBatteryMeasurementPower(false);
    PowerOffPartialDisplay();
#else
    epd_poweroff_all();
#endif
    float voltage = esp_adc_cal_raw_to_voltage(averageRaw, &adc_chars) * 2.0f / 1000.0f;
    Serial.printf("Battery ADC raw %u, calibration %d -> %.3f V\n", averageRaw, (int)calibration, voltage);
    return voltage >= 1.0f && voltage <= 5.0f ? voltage : 0.0f;
}

int BatteryUpdateInterval(float voltage)
{
    if (!(voltage >= 1.0f && voltage <= 5.0f) || voltage >= 4.15f)
        return 1;
    if (voltage >= 3.95f)
        return 6;
    if (voltage >= 3.75f)
        return 12;
    return 24;
}

uint8_t BatteryPercentage(float voltage)
{
    if (voltage >= 4.20f)
        return 100;
    if (voltage <= 3.50f)
        return 0;
    float percentage = 2836.9625f * powf(voltage, 4) - 43987.4889f * powf(voltage, 3) +
                       255233.8134f * powf(voltage, 2) - 656689.7123f * voltage + 632041.7303f;
    return (uint8_t)constrain((int)lroundf(percentage), 0, 100);
}

void UpdateBatteryLifeEstimate(time_t now)
{
    if (BatteryVoltage < 1.0f || now < 1600000000)
        return;

    Preferences prefs;
    if (!prefs.begin("battlife2", false))
        return;
    float anchorPercent = prefs.getFloat("anchor_pct", -1.0f);
    time_t anchorTime = (time_t)prefs.getULong64("anchor_time", 0);
    float drainPercentPerDay = prefs.getFloat("drain_rate", 0.0f);
    float percent = BatteryPercentage(BatteryVoltage);
    bool save = false;

    if (anchorPercent < 0.0f || anchorPercent > 100.0f || anchorTime == 0 || now <= anchorTime || now - anchorTime > 30 * 86400)
    {
        anchorPercent = percent;
        anchorTime = now;
        drainPercentPerDay = 0.0f;
        save = true;
    }
    else if (percent >= anchorPercent + 5.0f)
    {
        anchorPercent = percent;
        anchorTime = now;
        drainPercentPerDay = 0.0f;
        save = true;
    }
    else if (anchorPercent - percent >= 1.0f && now - anchorTime >= 3600)
    {
        float observedRate = (anchorPercent - percent) * 86400.0f / difftime(now, anchorTime);
        drainPercentPerDay = drainPercentPerDay > 0.0f ? (drainPercentPerDay + observedRate) / 2.0f : observedRate;
        anchorPercent = percent;
        anchorTime = now;
        save = true;
    }

    if (save)
    {
        prefs.putFloat("anchor_pct", anchorPercent);
        prefs.putULong64("anchor_time", (uint64_t)anchorTime);
        prefs.putFloat("drain_rate", drainPercentPerDay);
    }
    if (drainPercentPerDay > 0.0f)
    {
        double seconds = percent * 86400.0 / drainPercentPerDay;
        const double maxSeconds = (99 * 24 * 60 + 23 * 60 + 59) * 60.0;
        BatteryLifeEstimateSeconds = (int64_t)constrain(seconds, 0.0, maxSeconds);
    }
    prefs.end();
}

String FormatBatteryLifeEstimate()
{
    if (BatteryLifeEstimateSeconds < 0)
        return "--d--:--";

    uint64_t minutes = BatteryLifeEstimateSeconds / 60;
    const uint64_t maxMinutes = 99 * 24 * 60 + 23 * 60 + 59;
    if (minutes > maxMinutes)
        minutes = maxMinutes;
    char estimate[10];
    snprintf(estimate, sizeof(estimate), "%02ud%02u:%02u",
             (unsigned)(minutes / (24 * 60)), (unsigned)(minutes / 60 % 24), (unsigned)(minutes % 60));
    return String(estimate);
}

bool IsWeatherHour(int hour)
{
    return hour >= WakeupHour && hour <= SleepHour && (hour - WakeupHour) % UpdateIntervalHours == 0;
}

bool IsScheduledUpdate()
{
    return esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_TIMER || IsWeatherHour(CurrentHour);
}

long SecondsUntilNextUpdate(time_t now)
{
    struct tm nextUpdate;
    localtime_r(&now, &nextUpdate);
    int currentHour = nextUpdate.tm_hour;
    int nextHour;
    if (currentHour < WakeupHour)
        nextHour = WakeupHour;
    else if (currentHour >= SleepHour)
        nextHour = 24 + WakeupHour;
    else
    {
        nextHour = WakeupHour + ((currentHour - WakeupHour) / UpdateIntervalHours + 1) * UpdateIntervalHours;
        if (nextHour > SleepHour)
            nextHour = 24 + WakeupHour;
    }
    nextUpdate.tm_mday += nextHour / 24;
    nextUpdate.tm_hour = nextHour % 24;
    nextUpdate.tm_min = 0;
    nextUpdate.tm_sec = 0;
    nextUpdate.tm_isdst = -1;
    return (long)difftime(mktime(&nextUpdate), now) + Delta;
}

long NextSleepSeconds(time_t now, long weatherSeconds, bool active, bool dirty, bool weatherWake, time_t next, time_t end, long debugSeconds)
{
    long result = weatherSeconds;
    if (active)
    {
        long untilNext = now >= 1600000000 && next ? max(1L, (long)(next - now)) : 60;
        if (end && now >= 1600000000) untilNext = min(untilNext, max(1L, (long)(end - now)));
        result = min(result, untilNext);
    }
    else if (dirty)
        result = min(result, 60L);
    else if (weatherWake)
        result = min(result, 1L);
    if (debugSeconds > 0) result = min(result, debugSeconds);
    return max(1L, result);
}

void BeginSleep()
{
#if EPDPARTIAL
    PowerOffPartialDisplay();
#else
    epd_poweroff_all();
#endif
    time_t now = time(nullptr);
    long weatherSeconds = now >= 1600000000 ? SecondsUntilNextUpdate(now) : RetrySleepSeconds;
    bool active = false, dirty = false, weatherWake = false;
    time_t next = 0, end = 0;
#if EPDPARTIAL
    Preferences page;
    if (page.begin("tram-test", true))
    {
        active = page.getBool("active", false);
        dirty = page.getBool("dirty", false);
        weatherWake = page.getBool("weather_wake", false);
        next = page.getULong64("next", 0);
        end = page.getULong64("end", 0);
        page.end();
    }
    else dirty = true;
#endif
    SleepTimer = NextSleepSeconds(now, weatherSeconds, active, dirty, weatherWake, next, end, DEBUG_SERIAL_WAKE_SECONDS);
    if (!active && !dirty)
    {
        long retrySeconds = WeatherRetrySeconds(now);
        if (retrySeconds > 0) SleepTimer = min(SleepTimer, retrySeconds);
    }
#if T5_47_PLUS_V2
    pinMode(21, INPUT_PULLUP);
    unsigned long releaseStart = millis();
    while (!digitalRead(21) && millis() - releaseStart < 5000)
        delay(10);
    if (digitalRead(21))
    {
        rtc_gpio_pullup_en(GPIO_NUM_21);
        rtc_gpio_pulldown_dis(GPIO_NUM_21);
        esp_sleep_enable_ext0_wakeup(GPIO_NUM_21, 0);
    }
#endif
    esp_sleep_enable_timer_wakeup(SleepTimer * 1000000LL);                                        // in Secs, 1000000LL converts to Secs as unit = 1uSec
    Serial.println("Awake for : " + String((millis() - StartTime) / 1000.0, 3) + "-secs");
    Serial.println("Entering " + String(SleepTimer) + " (secs) of sleep time");
    Serial.println("Starting deep-sleep period...");
    esp_deep_sleep_start(); // Sleep for e.g. 30 minutes
}

boolean SetupTime()
{
    configTime(gmtOffset_sec, daylightOffset_sec, const_cast<const char *>(ntpServer.c_str()), "time.nist.gov"); //(gmtOffset_sec, daylightOffset_sec, ntpServer)
    setenv("TZ", const_cast<const char *>(Timezone.c_str()), 1);                                                 //setenv()adds the "TZ" variable to the environment with a value TimeZone, only used if set to 1, 0 means no change
    tzset();                                                                   // Set the TZ environment variable
    Serial.println("Timezone: " + Timezone);
    delay(100);
    return UpdateLocalTime();
}

uint8_t StartWiFi()
{
    Serial.println("\r\nConnecting to: " + String(ssid));
    IPAddress dns(8, 8, 8, 8); // Use Google DNS
    WiFi.disconnect();
    WiFi.mode(WIFI_STA); // switch off AP
    WiFi.setAutoConnect(true);
    WiFi.setAutoReconnect(true);
    WiFi.begin(const_cast<const char *>(ssid.c_str()), const_cast<const char *>(password.c_str()));
    if (WiFi.waitForConnectResult() != WL_CONNECTED)
    {
        Serial.printf("STA: Failed!\n");
        WiFi.disconnect(false);
        delay(500);
        WiFi.begin(const_cast<const char *>(ssid.c_str()), const_cast<const char *>(password.c_str()));
    }
    if (WiFi.status() == WL_CONNECTED)
    {
        wifi_signal = WiFi.RSSI(); // Get Wifi Signal strength now, because the WiFi will be turned off to save power!
        Serial.println("WiFi connected at: " + WiFi.localIP().toString());
    }
    else
        Serial.println("WiFi connection *** FAILED ***");
    return WiFi.status();
}

void StopWiFi()
{
    WiFi.disconnect();
    WiFi.mode(WIFI_OFF);
    Serial.println("WiFi switched Off");
}

bool PollTramButton()
{
#if T5_47_PLUS_V2
    if (!digitalRead(21)) AwakeTramButtonPressed = true;
#endif
    return AwakeTramButtonPressed;
}

void InitialiseSystem()
{
    StartTime = millis();
#if T5_47_PLUS_V2
    pinMode(21, INPUT_PULLUP);
#endif
    Serial.begin(115200);
    if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_EXT0)
    {
        unsigned long serialWaitStart = millis();
        while (!Serial && millis() - serialWaitStart < 1000 && !PollTramButton())
            delay(10);
        unsigned long window = esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_TIMER ? 10000 :
                               (DEBUG_SERIAL_WAKE_SECONDS > 0 ? 5000 : 0);
        unsigned long windowStart = millis();
        while (millis() - windowStart < window && !PollTramButton())
            delay(10);
    }
    Serial.println(String(__FILE__) + "\nStarting...");
    Serial.printf("Reset reason %d; wake cause %d; fake weather %d; debug wake %d s\n",
                  (int)esp_reset_reason(), (int)esp_sleep_get_wakeup_cause(),
                  DEBUG_FAKE_WEATHER, DEBUG_SERIAL_WAKE_SECONDS);
#if !EPDPARTIAL
    epd_init();
#endif
    framebuffer = (uint8_t *)ps_calloc(sizeof(uint8_t), EPD_WIDTH * EPD_HEIGHT / 2);
    if (!framebuffer)
        Serial.println("Memory alloc failed!");
    memset(framebuffer, 0xFF, EPD_WIDTH * EPD_HEIGHT / 2);
}

void loop()
{
    // Nothing to do here
    delay(1000);
}

String FitTramDestination(const char *destination)
{
    String text(destination);
    int count = 0, truncateAt = 0;
    for (unsigned i = 0; i < text.length(); ++i)
        if ((static_cast<uint8_t>(text[i]) & 0xC0) != 0x80)
        {
            if (++count == 18) truncateAt = i;
            if (count == 21) return text.substring(0, truncateAt) + "...";
        }
    return text;
}

void DisplayTramBoard(const TramDeparture *rows, size_t count, time_t updatedAt, bool ok)
{
    memset(framebuffer, 0xFF, EPD_WIDTH * EPD_HEIGHT / 2);
    setFont(Spleen24Bold);
    drawString(20, 16, TRAM_STOP_TITLE, LEFT);
    char updated[6] = "--:--";
    if (updatedAt)
    {
        struct tm localTime;
        localtime_r(&updatedAt, &localTime);
        strftime(updated, sizeof(updated), "%H:%M", &localTime);
    }
    setFont(Spleen24);
    drawString(410, 16, updated, LEFT);
    drawFastHLine(20, 55, 920, Black);
    setFont(Spleen32);
    TramGroup groups[8] = {};
    size_t groupCount = ok ? GroupTramDepartures(rows, count, groups, 8) : 0;
    if (!ok || !groupCount)
        drawString(24, 73, ok ? "No departures found" : "Departures unavailable", LEFT);
    for (size_t i = 0; i < groupCount; ++i)
    {
        int y = 73 + i * 57;
        drawString(24, y, groups[i].line, LEFT);
        drawString(130, y, FitTramDestination(groups[i].destination), LEFT);
        for (size_t j = 0; j < groups[i].count; ++j)
            drawString(480 + j * 95, y, groups[i].times[j], LEFT);
        if (i + 1 < groupCount) fillRect(20, y + 44, 920, 2, LightGrey);
    }
}

bool DisplayLoading(const char *message)
{
    if (!LoadDisplayedImage(framebuffer))
        memset(framebuffer, 0xFF, EPD_WIDTH * EPD_HEIGHT / 2);
    fillRect(260, 210, 440, 76, White);
    drawRect(260, 210, 440, 76, Black);
    setFont(Spleen32);
    drawString(480, 232, message, CENTER);
    return UpdatePartialDisplay(framebuffer, false);
}

bool RestoreWeatherPage(Preferences &page)
{
    if (!LoadWeatherImage(framebuffer) || !UpdatePartialDisplay(framebuffer, false))
        return false;
    if (page.putBool("weather_wake", true) != 1 || page.putBool("dirty", false) != 1) return false;
    Serial.println("Weather image restored");
    return true;
}

bool RunTramPage()
{
#if EPDPARTIAL
    Preferences page;
    if (!page.begin("tram-test", false)) return true;
    bool active = page.getBool("active", false);
    bool button = esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0 || AwakeTramButtonPressed;
    AwakeTramButtonPressed = false;
    if (!active && page.getBool("dirty", false))
    {
        if (!RestoreWeatherPage(page))
        {
            Serial.println("Cannot restore saved weather image");
            if (button) DisplayLoading("Weather unavailable");
            page.end();
            return true;
        }
        if (!button) { page.end(); return true; }
    }
    if (button && active)
    {
        if (page.putBool("dirty", true) == 1 && page.putBool("active", false) == 1)
        {
            DisplayLoading("Loading weather...");
            RestoreWeatherPage(page);
        }
        page.end();
        return true;
    }
    if (button && !active)
    {
        if (!SaveWeatherImage() && !LoadWeatherImage(framebuffer))
        {
            Serial.println("No usable weather image");
            page.putBool("dirty", true);
            DisplayLoading("Weather unavailable");
            page.end();
            return true;
        }
        if (page.putULong64("end", 0) != sizeof(uint64_t) ||
            page.putULong64("next", 0) != sizeof(uint64_t) ||
            page.putBool("dirty", true) != 1 || page.putBool("active", true) != 1)
        {
            Serial.println("Cannot start tram page");
            page.end();
            return true;
        }
        active = true;
        DisplayLoading("Loading trams...");
    }
    if (!active)
    {
        if (page.getBool("weather_wake", false) && page.putBool("weather_wake", false) != 1)
        {
            page.end();
            return true;
        }
        page.end();
        return false;
    }

    time_t now = time(nullptr);
    if (now < 1600000000)
    {
        bool synced = StartWiFi() == WL_CONNECTED && SetupTime();
        StopWiFi();
        if (!synced) { Serial.println("Clock unavailable for departures"); page.end(); return true; }
        now = time(nullptr);
    }
    time_t end = page.getULong64("end", 0);
    if (!end)
    {
        end = now + 300;
        if (page.putULong64("end", end) != sizeof(uint64_t)) { page.end(); return true; }
    }
    if (now >= end)
    {
        if (page.putBool("dirty", true) == 1 && page.putBool("active", false) == 1)
            RestoreWeatherPage(page);
    }
    else if (now >= (time_t)page.getULong64("next", 0) &&
             page.putULong64("next", now + 60) == sizeof(uint64_t) &&
             page.putBool("dirty", true) == 1)
    {
        TramDeparture *rows = static_cast<TramDeparture *>(ps_malloc(32 * sizeof(TramDeparture)));
        size_t count = 0;
        bool ok = rows && StartWiFi() == WL_CONNECTED && FetchTransitous(rows, 32, count);
        StopWiFi();
        DisplayTramBoard(rows, count, ok ? time(nullptr) : 0, ok);
        free(rows);
        if (UpdatePartialDisplay(framebuffer, false)) page.putBool("dirty", false);
        Serial.printf("Transitous: %s, %u departures\n", ok ? "OK" : "failed", (unsigned)count);
    }
    page.end();
    return true;
#else
    return false;
#endif
}
bool LoadConfiguration()
{
    if (!SPIFFS.begin()) return false;
    File file = SPIFFS.open("/config.json", "r");
    StaticJsonDocument<1024> settings;
    DeserializationError error = deserializeJson(settings, file);
    file.close();
    if (error) return false;
    ssid = settings["WLAN"]["ssid"].as<String>();
    password = settings["WLAN"]["password"].as<String>();
    apikey = settings["OpenWeather"]["apikey"].as<String>();
    server = settings["OpenWeather"]["server"].as<String>();
    City = WEATHER_CITY;
    Country = WEATHER_COUNTRY;
    Hemisphere = settings["OpenWeather"]["hemisphere"].as<String>();
    Units = settings["OpenWeather"]["units"].as<String>();
    ntpServer = settings["ntp"]["server"].as<String>();
    Timezone = settings["ntp"]["timezone"] | "UTC0";
    setenv("TZ", Timezone.c_str(), 1);
    tzset();
    return true;
}

uint32_t WeatherSlot(time_t now)
{
    struct tm local;
    localtime_r(&now, &local);
    return ((local.tm_year + 1900) * 512 + local.tm_yday) * 24 + local.tm_hour;
}

const char *WeatherScheduleNamespace()
{
    return DEBUG_FAKE_WEATHER ? "weather-test" : "weather-sched";
}

bool ClaimWeatherAttempt(time_t now)
{
    Preferences prefs;
    if (!prefs.begin(WeatherScheduleNamespace(), false)) return false;
    bool ok = prefs.getUInt("success", 0) != WeatherSlot(now) &&
              prefs.getULong64("retry_after", 0) <= (uint64_t)now &&
              prefs.putULong64("retry_after", (uint64_t)now + 15 * 60) == sizeof(uint64_t);
    prefs.end();
    return ok;
}

void MarkWeatherSuccess(time_t now)
{
    Preferences prefs;
    if (!prefs.begin(WeatherScheduleNamespace(), false)) return;
    if (prefs.putUInt("success", WeatherSlot(now)) == sizeof(uint32_t))
        prefs.putULong64("retry_after", 0);
    prefs.end();
}

long WeatherRetrySeconds(time_t now)
{
    if (now < 1600000000) return 0;
    struct tm local;
    localtime_r(&now, &local);
    if (!IsWeatherHour(local.tm_hour)) return 0;
    Preferences prefs;
    if (!prefs.begin(WeatherScheduleNamespace(), true)) return 0;
    uint64_t retry = prefs.getULong64("retry_after", 0);
    bool pending = retry && prefs.getUInt("success", 0) != WeatherSlot(now);
    prefs.end();
    return pending ? max(1L, (long)((int64_t)retry - now)) : 0;
}

#if DEBUG_FAKE_WEATHER
void LoadFakeWeather(time_t now)
{
    auto &current = WxConditions[0];
    current.Forecast0 = "scattered clouds";
    current.Icon = "02d";
    current.Temperature = 20.5;
    current.Humidity = 62;
    current.High = 23;
    current.Low = 16;
    current.Pressure = 1015;
    current.Trend = "=";
    current.Winddir = 225;
    current.Windspeed = 3.4;
    current.Cloudcover = 35;
    current.Visibility = 10000;
    current.Sunrise = now - 3 * 3600;
    current.Sunset = now + 5 * 3600;
    for (int i = 0; i < max_readings; ++i)
    {
        auto &forecast = WxForecast[i];
        forecast.Dt = now + (i + 1) * 3 * 3600;
        forecast.Icon = i % 3 ? "02d" : "01d";
        forecast.Temperature = 17 + i % 7;
        forecast.Low = forecast.Temperature - 2;
        forecast.High = forecast.Temperature + 2;
        forecast.Humidity = 55 + i % 10;
        forecast.Pressure = 1010 + i % 8;
        forecast.Rainfall = i % 5 == 0 ? 0.5 : 0;
    }
}
#endif

void setup()
{
    InitialiseSystem();
    if (!LoadConfiguration())
    {
        Serial.println("Configuration unavailable; upload the SPIFFS image");
        BeginSleep();
        return;
    }
    if (RunTramPage())
    {
        BeginSleep();
        return;
    }

    BatteryVoltage = ReadBatteryVoltage();
    UpdateIntervalHours = BatteryUpdateInterval(BatteryVoltage);
    Serial.printf("Battery estimate %.2f V; refresh interval %d h\n", BatteryVoltage, UpdateIntervalHours);

    bool wifiUsed = false, wifiConnected = false;
    bool timeSynced = time(nullptr) >= 1600000000 && UpdateLocalTime();
    if (!timeSynced)
    {
        wifiUsed = true;
        wifiConnected = StartWiFi() == WL_CONNECTED;
        timeSynced = wifiConnected && SetupTime();
    }
    if (timeSynced) UpdateBatteryLifeEstimate(time(nullptr));
    bool updateDue = timeSynced && IsScheduledUpdate() && ClaimWeatherAttempt(time(nullptr));
    bool RxWeather = false, RxForecast = false;
    if (updateDue)
    {
#if DEBUG_FAKE_WEATHER
        Serial.println("Using fake weather data");
        LoadFakeWeather(time(nullptr));
        RxWeather = RxForecast = true;
#else
        if (!wifiConnected)
        {
            wifiUsed = true;
            wifiConnected = StartWiFi() == WL_CONNECTED;
        }
        if (wifiConnected)
        {
            byte Attempts = 1;
            WiFiClient client;
            while ((!RxWeather || !RxForecast) && Attempts <= 2)
            {
                if (!RxWeather) RxWeather = obtainWeatherData(client, "weather");
                if (!RxForecast) RxForecast = obtainWeatherData(client, "forecast");
                Attempts++;
            }
        }
#endif
    }
    if (wifiUsed) StopWiFi();
    if (updateDue && RxWeather && RxForecast)
    {
        Serial.println("Received all weather data...");
        DisplayWeather();
#if EPDPARTIAL
        bool displayed = UpdatePartialDisplay(framebuffer, CurrentHour == WakeupHour);
#else
        epd_poweron();
        epd_clear();
        edp_update();
        bool displayed = true;
#endif
        if (displayed) MarkWeatherSuccess(time(nullptr));
        else Serial.println("Weather display update failed");
    }
    else if (updateDue)
        Serial.println("Weather update failed; keeping existing display");
    else if (timeSynced)
        Serial.println("Outside scheduled update times");
    BeginSleep();
}
void Convert_Readings_to_Imperial()
{ // Only the first 3-hours are used
    WxConditions[0].Pressure = hPa_to_inHg(WxConditions[0].Pressure);
    WxForecast[0].Rainfall = mm_to_inches(WxForecast[0].Rainfall);
    WxForecast[0].Snowfall = mm_to_inches(WxForecast[0].Snowfall);
}

bool DecodeWeather(WiFiClient &json, String Type)
{
    Serial.print(F("\nCreating object...and "));
    DynamicJsonDocument doc(64 * 1024);                      // allocate the JsonDocument
    DeserializationError error = deserializeJson(doc, json); // Deserialize the JSON document
    if (error)
    { // Test if parsing succeeds.
        Serial.print(F("deserializeJson() failed: "));
        Serial.println(error.c_str());
        return false;
    }
    // convert it to a JsonObject
    JsonObject root = doc.as<JsonObject>();
    Serial.println(" Decoding " + Type + " data");
    if (Type == "weather")
    {
        // All Serial.println statements are for diagnostic purposes and some are not required, remove if not needed with //
        //WxConditions[0].lon         = root["coord"]["lon"].as<float>();              Serial.println(" Lon: " + String(WxConditions[0].lon));
        //WxConditions[0].lat         = root["coord"]["lat"].as<float>();              Serial.println(" Lat: " + String(WxConditions[0].lat));
        WxConditions[0].Main0 = root["weather"][0]["main"].as<const char *>();
        Serial.println("Main: " + String(WxConditions[0].Main0));
        WxConditions[0].Forecast0 = root["weather"][0]["description"].as<const char *>();
        Serial.println("For0: " + String(WxConditions[0].Forecast0));
        //WxConditions[0].Forecast1   = root["weather"][1]["description"].as<char*>(); Serial.println("For1: " + String(WxConditions[0].Forecast1));
        //WxConditions[0].Forecast2   = root["weather"][2]["description"].as<char*>(); Serial.println("For2: " + String(WxConditions[0].Forecast2));
        WxConditions[0].Icon = root["weather"][0]["icon"].as<const char *>();
        Serial.println("Icon: " + String(WxConditions[0].Icon));
        WxConditions[0].Temperature = root["main"]["temp"].as<float>();
        Serial.println("Temp: " + String(WxConditions[0].Temperature));
        WxConditions[0].Pressure = root["main"]["pressure"].as<float>();
        Serial.println("Pres: " + String(WxConditions[0].Pressure));
        WxConditions[0].Humidity = root["main"]["humidity"].as<float>();
        Serial.println("Humi: " + String(WxConditions[0].Humidity));
        WxConditions[0].Low = root["main"]["temp_min"].as<float>();
        Serial.println("TLow: " + String(WxConditions[0].Low));
        WxConditions[0].High = root["main"]["temp_max"].as<float>();
        Serial.println("THig: " + String(WxConditions[0].High));
        WxConditions[0].Windspeed = root["wind"]["speed"].as<float>();
        Serial.println("WSpd: " + String(WxConditions[0].Windspeed));
        WxConditions[0].Winddir = root["wind"]["deg"].as<float>();
        Serial.println("WDir: " + String(WxConditions[0].Winddir));
        WxConditions[0].Cloudcover = root["clouds"]["all"].as<int>();
        Serial.println("CCov: " + String(WxConditions[0].Cloudcover)); // in % of cloud cover
        WxConditions[0].Visibility = root["visibility"].as<int>();
        Serial.println("Visi: " + String(WxConditions[0].Visibility)); // in metres
        WxConditions[0].Rainfall = root["rain"]["1h"].as<float>();
        Serial.println("Rain: " + String(WxConditions[0].Rainfall));
        WxConditions[0].Snowfall = root["snow"]["1h"].as<float>();
        Serial.println("Snow: " + String(WxConditions[0].Snowfall));
        //WxConditions[0].Country     = root["sys"]["country"].as<char*>();            Serial.println("Ctry: " + String(WxConditions[0].Country));
        WxConditions[0].Sunrise = root["sys"]["sunrise"].as<int>();
        Serial.println("SRis: " + String(WxConditions[0].Sunrise));
        WxConditions[0].Sunset = root["sys"]["sunset"].as<int>();
        Serial.println("SSet: " + String(WxConditions[0].Sunset));
        WxConditions[0].Timezone = root["timezone"].as<int>();
        Serial.println("TZon: " + String(WxConditions[0].Timezone));
    }
    if (Type == "forecast")
    {
        //Serial.println(json);
        Serial.print(F("\nReceiving Forecast period - ")); //------------------------------------------------
        JsonArray list = root["list"];
        for (byte r = 0; r < max_readings; r++)
        {
            Serial.println("\nPeriod-" + String(r) + "--------------");
            WxForecast[r].Dt = list[r]["dt"].as<int>();
            WxForecast[r].Temperature = list[r]["main"]["temp"].as<float>();
            Serial.println("Temp: " + String(WxForecast[r].Temperature));
            WxForecast[r].Low = list[r]["main"]["temp_min"].as<float>();
            Serial.println("TLow: " + String(WxForecast[r].Low));
            WxForecast[r].High = list[r]["main"]["temp_max"].as<float>();
            Serial.println("THig: " + String(WxForecast[r].High));
            WxForecast[r].Pressure = list[r]["main"]["pressure"].as<float>();
            Serial.println("Pres: " + String(WxForecast[r].Pressure));
            WxForecast[r].Humidity = list[r]["main"]["humidity"].as<float>();
            Serial.println("Humi: " + String(WxForecast[r].Humidity));
            //WxForecast[r].Forecast0         = list[r]["weather"][0]["main"].as<char*>();        Serial.println("For0: " + String(WxForecast[r].Forecast0));
            //WxForecast[r].Forecast1         = list[r]["weather"][1]["main"].as<char*>();        Serial.println("For1: " + String(WxForecast[r].Forecast1));
            //WxForecast[r].Forecast2         = list[r]["weather"][2]["main"].as<char*>();        Serial.println("For2: " + String(WxForecast[r].Forecast2));
            WxForecast[r].Icon = list[r]["weather"][0]["icon"].as<const char *>();
            Serial.println("Icon: " + String(WxForecast[r].Icon));
            //WxForecast[r].Description       = list[r]["weather"][0]["description"].as<char*>(); Serial.println("Desc: " + String(WxForecast[r].Description));
            //WxForecast[r].Cloudcover        = list[r]["clouds"]["all"].as<int>();               Serial.println("CCov: " + String(WxForecast[r].Cloudcover)); // in % of cloud cover
            //WxForecast[r].Windspeed         = list[r]["wind"]["speed"].as<float>();             Serial.println("WSpd: " + String(WxForecast[r].Windspeed));
            //WxForecast[r].Winddir           = list[r]["wind"]["deg"].as<float>();               Serial.println("WDir: " + String(WxForecast[r].Winddir));
            WxForecast[r].Rainfall = list[r]["rain"]["3h"].as<float>();
            Serial.println("Rain: " + String(WxForecast[r].Rainfall));
            WxForecast[r].Snowfall = list[r]["snow"]["3h"].as<float>();
            Serial.println("Snow: " + String(WxForecast[r].Snowfall));
            WxForecast[r].Period = list[r]["dt_txt"].as<const char *>();
            Serial.println("Peri: " + String(WxForecast[r].Period));
        }
        //------------------------------------------
        float pressure_trend = WxForecast[0].Pressure - WxForecast[2].Pressure; // Measure pressure slope between ~now and later
        pressure_trend = ((int)(pressure_trend * 10)) / 10.0;                   // Remove any small variations less than 0.1
        WxConditions[0].Trend = "=";
        if (pressure_trend > 0)
            WxConditions[0].Trend = "+";
        if (pressure_trend < 0)
            WxConditions[0].Trend = "-";
        if (pressure_trend == 0)
            WxConditions[0].Trend = "0";

        if (Units == "I")
            Convert_Readings_to_Imperial();
    }
    return true;
}
//#########################################################################################
String ConvertUnixTime(int unix_time)
{
    // Returns either '21:12  ' or ' 09:12pm' depending on Units mode
    time_t tm = unix_time;
    struct tm *now_tm = localtime(&tm);
    char output[40];
    if (Units == "M")
    {
        strftime(output, sizeof(output), "%H:%M %d/%m/%y", now_tm);
    }
    else
    {
        strftime(output, sizeof(output), "%I:%M%P %m/%d/%y", now_tm);
    }
    return output;
}
//#########################################################################################
bool obtainWeatherData(WiFiClient &client, const String &RequestType)
{
    const String units = (Units == "M" ? "metric" : "imperial");
    client.stop(); // close connection before sending a new request
    HTTPClient http;
    String uri = "/data/2.5/" + RequestType + "?q=" + City + "," + Country + "&APPID=" + apikey + "&mode=json&units=" + units + "&lang=" + Language;
    if (RequestType != "weather")
    {
        uri += "&cnt=" + String(max_readings);
    }
    http.begin(client, server, 80, uri); //http.begin(uri,test_root_ca); //HTTPS example connection
    int httpCode = http.GET();
    if (httpCode == HTTP_CODE_OK)
    {
        if (!DecodeWeather(http.getStream(), RequestType))
            return false;
        client.stop();
        http.end();
        return true;
    }
    else
    {
        Serial.printf("Weather %s request failed: HTTP %d (%s)\n", RequestType.c_str(), httpCode, http.errorToString(httpCode).c_str());
        client.stop();
        http.end();
        return false;
    }
    http.end();
    return true;
}

float mm_to_inches(float value_mm)
{
    return 0.0393701 * value_mm;
}

float hPa_to_inHg(float value_hPa)
{
    return 0.02953 * value_hPa;
}

int JulianDate(int d, int m, int y)
{
    int mm, yy, k1, k2, k3, j;
    yy = y - (int)((12 - m) / 10);
    mm = m + 9;
    if (mm >= 12)
        mm = mm - 12;
    k1 = (int)(365.25 * (yy + 4712));
    k2 = (int)(30.6001 * mm + 0.5);
    k3 = (int)((int)((yy / 100) + 49) * 0.75) - 38;
    // 'j' for dates in Julian calendar:
    j = k1 + k2 + d + 59 + 1;
    if (j > 2299160)
        j = j - k3; // 'j' is the Julian date at 12h UT (Universal Time) For Gregorian calendar:
    return j;
}

float SumOfPrecip(float DataArray[], int readings)
{
    float sum = 0;
    for (int i = 0; i < readings; i++)
    {
        sum += DataArray[i];
    }
    return sum;
}

String TitleCase(String text)
{
    if (text.length() > 0)
    {
        String temp_text = text.substring(0, 1);
        temp_text.toUpperCase();
        return temp_text + text.substring(1); // Title-case the string
    }
    else
        return text;
}

double NormalizedMoonPhase(int d, int m, int y)
{
    int j = JulianDate(d, m, y);
    //Calculate approximate moon phase
    double Phase = (j + 4.867) / 29.53059;
    return (Phase - (int)Phase);
}

void DisplayWeather()
{                                               // 4.7" e-paper display is 960x540 resolution
    DisplayStatusSection(600, 20, wifi_signal); // Wi-Fi signal strength and Battery voltage
    DisplayGeneralInfoSection();                // Top line of the display
    DisplayDisplayWindSection(137, 150, WxConditions[0].Winddir, WxConditions[0].Windspeed, 100);
    DisplayAstronomySection(5, 255);     // Astronomy section Sun rise/set, Moon phase and Moon icon
    DisplayMainWeatherSection(320, 110); // Centre section of display for Location, temperature, Weather report, current Wx Symbol
    DisplayWeatherIcon(810, 130);        // Display weather icon    scale = Large;
    DisplayForecastSection(320, 220);    // 3hr forecast boxes
}

void DisplayGeneralInfoSection()
{
    setFont(Spleen24Bold);
    drawString(5, 10, City, LEFT);
    setFont(Spleen24);
    drawString(190, 10, Date_str + " @ " + Time_str, LEFT);
}

void DisplayWeatherIcon(int x, int y)
{
    DisplayConditionsSection(x, y, WxConditions[0].Icon, LargeIcon);
}

void DisplayMainWeatherSection(int x, int y)
{
    DisplayTemperatureSection(x, y - 40);
    DisplayForecastTextSection(x - 55, y + 25);
    DisplayPressureSection(x - 25, y + 90, WxConditions[0].Pressure, WxConditions[0].Trend);
}

void DisplayDisplayWindSection(int x, int y, float angle, float windspeed, int Cradius)
{
    arrow(x, y, Cradius - 22, angle, 18, 33); // Show wind direction on outer circle of width and length
    setFont(Spleen24);
    int dxo, dyo, dxi, dyi;
    drawCircle(x, y, Cradius, Black);       // Draw compass circle
    drawCircle(x, y, Cradius + 1, Black);   // Draw compass circle
    drawCircle(x, y, Cradius * 0.7, Black); // Draw compass inner circle
    for (float a = 0; a < 360; a = a + 22.5)
    {
        dxo = Cradius * cos((a - 90) * PI / 180);
        dyo = Cradius * sin((a - 90) * PI / 180);
        if (a == 45)
            drawString(dxo + x + 15, dyo + y - 27, TXT_NE, CENTER);
        if (a == 135)
            drawString(dxo + x + 20, dyo + y - 11, TXT_SE, CENTER);
        if (a == 225)
            drawString(dxo + x - 20, dyo + y - 11, TXT_SW, CENTER);
        if (a == 315)
            drawString(dxo + x - 15, dyo + y - 27, TXT_NW, CENTER);
        dxi = dxo * 0.9;
        dyi = dyo * 0.9;
        drawLine(dxo + x, dyo + y, dxi + x, dyi + y, Black);
        dxo = dxo * 0.7;
        dyo = dyo * 0.7;
        dxi = dxo * 0.9;
        dyi = dyo * 0.9;
        drawLine(dxo + x, dyo + y, dxi + x, dyi + y, Black);
    }
    drawString(x, y - Cradius - 29, TXT_N, CENTER);
    drawString(x, y + Cradius + 1, TXT_S, CENTER);
    drawString(x - Cradius - 15, y - 14, TXT_W, CENTER);
    drawString(x + Cradius + 10, y - 14, TXT_E, CENTER);
    setFont(Spleen16);
    drawString(x + 3, y + 70, String(angle, 0) + "°", CENTER);
    setFont(Spleen24);
    drawString(x, y - 50, WindDegToOrdinalDirection(angle), CENTER);
    setFont(Spleen64Bold);
    drawString(x + 3, y - 38, String(windspeed, 1), CENTER);
    setFont(Spleen32);
    drawString(x, y + 21, (Units == "M" ? "m/s" : "mph"), CENTER);
}

String WindDegToOrdinalDirection(float winddirection)
{
    if (winddirection >= 348.75 || winddirection < 11.25)
        return TXT_N;
    if (winddirection >= 11.25 && winddirection < 33.75)
        return TXT_NNE;
    if (winddirection >= 33.75 && winddirection < 56.25)
        return TXT_NE;
    if (winddirection >= 56.25 && winddirection < 78.75)
        return TXT_ENE;
    if (winddirection >= 78.75 && winddirection < 101.25)
        return TXT_E;
    if (winddirection >= 101.25 && winddirection < 123.75)
        return TXT_ESE;
    if (winddirection >= 123.75 && winddirection < 146.25)
        return TXT_SE;
    if (winddirection >= 146.25 && winddirection < 168.75)
        return TXT_SSE;
    if (winddirection >= 168.75 && winddirection < 191.25)
        return TXT_S;
    if (winddirection >= 191.25 && winddirection < 213.75)
        return TXT_SSW;
    if (winddirection >= 213.75 && winddirection < 236.25)
        return TXT_SW;
    if (winddirection >= 236.25 && winddirection < 258.75)
        return TXT_WSW;
    if (winddirection >= 258.75 && winddirection < 281.25)
        return TXT_W;
    if (winddirection >= 281.25 && winddirection < 303.75)
        return TXT_WNW;
    if (winddirection >= 303.75 && winddirection < 326.25)
        return TXT_NW;
    if (winddirection >= 326.25 && winddirection < 348.75)
        return TXT_NNW;
    return "?";
}

void DisplayTemperatureSection(int x, int y)
{
    setFont(Spleen48Bold);
    String temperature = String(WxConditions[0].Temperature, 1) + "°";
    int bx, by, width, height, cx = 0, cy = 0;
    get_text_bounds(&currentFont, temperature.c_str(), &cx, &cy, &bx, &by, &width, &height, NULL);
    drawString(x - 30, y - 15, temperature, LEFT);
    drawString(max(x + 103, x - 30 + width + 12), y - 15, String(WxConditions[0].Humidity, 0) + "%", LEFT);
    setFont(Spleen32);
    drawString(x - 30, y + 29, String(WxConditions[0].High, 0) + "° | " + String(WxConditions[0].Low, 0) + "°", LEFT);
}

void DisplayForecastTextSection(int x, int y)
{
#define lineWidth 34
    setFont(Spleen24);
    //Wx_Description = WxConditions[0].Main0;          // e.g. typically 'Clouds'
    String Wx_Description = WxConditions[0].Forecast0; // e.g. typically 'overcast clouds' ... you choose which
    Wx_Description.replace(".", "");                   // remove any '.'
    int spaceRemaining = 0, p = 0, charCount = 0, Width = lineWidth;
    while (p < Wx_Description.length())
    {
        if (Wx_Description.substring(p, p + 1) == " ")
            spaceRemaining = p;
        if (charCount > Width - 1)
        { // '~' is the end of line marker
            Wx_Description = Wx_Description.substring(0, spaceRemaining) + "~" + Wx_Description.substring(spaceRemaining + 1);
            charCount = 0;
        }
        p++;
        charCount++;
    }
    if (WxForecast[0].Rainfall > 0)
        Wx_Description += " (" + String(WxForecast[0].Rainfall, 1) + String((Units == "M" ? "mm" : "in")) + ")";
    //Wx_Description = wordWrap(Wx_Description, lineWidth);
    String Line1 = Wx_Description.substring(0, Wx_Description.indexOf("~"));
    String Line2 = Wx_Description.substring(Wx_Description.indexOf("~") + 1);
    drawString(x + 30, y + 5, TitleCase(Line1), LEFT);
    if (Line1 != Line2)
        drawString(x + 30, y + 30, Line2, LEFT);
}

void DisplayPressureSection(int x, int y, float pressure, String slope)
{
    setFont(Spleen32);
    DrawPressureAndTrend(x - 25, y + 10, pressure, slope);
    if (WxConditions[0].Visibility > 0)
    {
        Visibility(x + 145, y, String(WxConditions[0].Visibility) + "M");
        x += 150; // Draw the text in the same positions if one is zero, otherwise in-line
    }
    if (WxConditions[0].Cloudcover > 0)
        CloudCover(x + 145, y, WxConditions[0].Cloudcover);
}

void DisplayForecastWeather(int x, int y, int index)
{
    int fwidth = ForecastWidth;
    x = x + fwidth * index;
    DisplayConditionsSection(x + fwidth / 2, y + 90, WxForecast[index].Icon, SmallIcon);
    setFont(Spleen24);
    drawString(x + fwidth / 2, y + 30, String(ConvertUnixTime(WxForecast[index].Dt).substring(0, 5)), CENTER);
    drawString(x + fwidth / 2, y + 125, String(WxForecast[index].High, 0) + "°/" + String(WxForecast[index].Low, 0) + "°", CENTER);
}

void DisplayAstronomySection(int x, int y)
{
    setFont(Spleen24);
    drawString(x + 5, y + 30, ConvertUnixTime(WxConditions[0].Sunrise).substring(0, 5) + " " + TXT_SUNRISE, LEFT);
    drawString(x + 5, y + 50, ConvertUnixTime(WxConditions[0].Sunset).substring(0, 5) + " " + TXT_SUNSET, LEFT);
    time_t now = time(NULL);
    struct tm *now_utc = gmtime(&now);
    const int day_utc = now_utc->tm_mday;
    const int month_utc = now_utc->tm_mon + 1;
    const int year_utc = now_utc->tm_year + 1900;
    drawString(x + 5, y + 70, MoonPhase(day_utc, month_utc, year_utc, Hemisphere), LEFT);
    DrawMoon(x + 160, y - 15, day_utc, month_utc, year_utc, Hemisphere);
}

void DrawMoon(int x, int y, int dd, int mm, int yy, String hemisphere)
{
    const int diameter = 75;
    double Phase = NormalizedMoonPhase(dd, mm, yy);
    hemisphere.toLowerCase();
    if (hemisphere == "south")
        Phase = 1 - Phase;
    // Draw dark part of moon
    fillCircle(x + diameter - 1, y + diameter, diameter / 2 + 1, LightGrey);
    const int number_of_lines = 90;
    for (double Ypos = 0; Ypos <= number_of_lines / 2; Ypos++)
    {
        double Xpos = sqrt(number_of_lines / 2 * number_of_lines / 2 - Ypos * Ypos);
        // Determine the edges of the lighted part of the moon
        double Rpos = 2 * Xpos;
        double Xpos1, Xpos2;
        if (Phase < 0.5)
        {
            Xpos1 = -Xpos;
            Xpos2 = Rpos - 2 * Phase * Rpos - Xpos;
        }
        else
        {
            Xpos1 = Xpos;
            Xpos2 = Xpos - 2 * Phase * Rpos + Rpos;
        }
        // Draw light part of moon
        double pW1x = (Xpos1 + number_of_lines) / number_of_lines * diameter + x;
        double pW1y = (number_of_lines - Ypos) / number_of_lines * diameter + y;
        double pW2x = (Xpos2 + number_of_lines) / number_of_lines * diameter + x;
        double pW2y = (number_of_lines - Ypos) / number_of_lines * diameter + y;
        double pW3x = (Xpos1 + number_of_lines) / number_of_lines * diameter + x;
        double pW3y = (Ypos + number_of_lines) / number_of_lines * diameter + y;
        double pW4x = (Xpos2 + number_of_lines) / number_of_lines * diameter + x;
        double pW4y = (Ypos + number_of_lines) / number_of_lines * diameter + y;
        drawLine(pW1x, pW1y, pW2x, pW2y, White);
        drawLine(pW3x, pW3y, pW4x, pW4y, White);
    }
    drawCircle(x + diameter - 1, y + diameter, diameter / 2, Black);
}

String MoonPhase(int d, int m, int y, String hemisphere)
{
    int c, e;
    double jd;
    int b;
    if (m < 3)
    {
        y--;
        m += 12;
    }
    ++m;
    c = 365.25 * y;
    e = 30.6 * m;
    jd = c + e + d - 694039.09; /* jd is total days elapsed */
    jd /= 29.53059;             /* divide by the moon cycle (29.53 days) */
    b = jd;                     /* int(jd) -> b, take integer part of jd */
    jd -= b;                    /* subtract integer part to leave fractional part of original jd */
    b = jd * 8 + 0.5;           /* scale fraction from 0-8 and round by adding 0.5 */
    b = b & 7;                  /* 0 and 8 are the same phase so modulo 8 for 0 */
    if (hemisphere == "south")
        b = 7 - b;
    if (b == 0)
        return TXT_MOON_NEW; // New;              0%  illuminated
    if (b == 1)
        return TXT_MOON_WAXING_CRESCENT; // Waxing crescent; 25%  illuminated
    if (b == 2)
        return TXT_MOON_FIRST_QUARTER; // First quarter;   50%  illuminated
    if (b == 3)
        return TXT_MOON_WAXING_GIBBOUS; // Waxing gibbous;  75%  illuminated
    if (b == 4)
        return TXT_MOON_FULL; // Full;            100% illuminated
    if (b == 5)
        return TXT_MOON_WANING_GIBBOUS; // Waning gibbous;  75%  illuminated
    if (b == 6)
        return TXT_MOON_THIRD_QUARTER; // Third quarter;   50%  illuminated
    if (b == 7)
        return TXT_MOON_WANING_CRESCENT; // Waning crescent; 25%  illuminated
    return "";
}

void DisplayForecastSection(int x, int y)
{
    for (int f = 0; f < max_readings && x + (f + 1) * ForecastWidth <= SCREEN_WIDTH; ++f)
        DisplayForecastWeather(x, y, f);
    int r = 0;
    do
    { // Pre-load temporary arrays with with data - because C parses by reference and remember that[1] has already been converted to I units
        if (Units == "I")
            pressure_readings[r] = WxForecast[r].Pressure * 0.02953;
        else
            pressure_readings[r] = WxForecast[r].Pressure;
        if (Units == "I")
            rain_readings[r] = WxForecast[r].Rainfall * 0.0393701;
        else
            rain_readings[r] = WxForecast[r].Rainfall;
        if (Units == "I")
            snow_readings[r] = WxForecast[r].Snowfall * 0.0393701;
        else
            snow_readings[r] = WxForecast[r].Snowfall;
        temperature_readings[r] = WxForecast[r].Temperature;
        humidity_readings[r] = WxForecast[r].Humidity;
        r++;
    } while (r < max_readings);
    int gwidth = 175, gheight = 100;
    int gx = (SCREEN_WIDTH - gwidth * 4) / 5 + 8;
    int gy = (SCREEN_HEIGHT - gheight - 30);
    int gap = gwidth + gx;
    // (x,y,width,height,MinValue, MaxValue, Title, Data Array, AutoScale, ChartMode)
    DrawGraph(gx + 0 * gap, gy, gwidth, gheight, 900, 1050, Units == "M" ? TXT_PRESSURE_HPA : TXT_PRESSURE_IN, pressure_readings, max_readings, autoscale_on, barchart_off); // TODO: swap this for wave height or visibility
    DrawGraph(gx + 1 * gap, gy, gwidth, gheight, 10, 30, Units == "M" ? TXT_TEMPERATURE_C : TXT_TEMPERATURE_F, temperature_readings, max_readings, autoscale_on, barchart_off);
    DrawGraph(gx + 2 * gap, gy, gwidth, gheight, 0, 100, TXT_HUMIDITY_PERCENT, humidity_readings, max_readings, autoscale_off, barchart_off);
    if (SumOfPrecip(rain_readings, max_readings) >= SumOfPrecip(snow_readings, max_readings))
        DrawGraph(gx + 3 * gap + 5, gy, gwidth, gheight, 0, 30, Units == "M" ? TXT_RAINFALL_MM : TXT_RAINFALL_IN, rain_readings, max_readings, autoscale_on, barchart_on);
    else
        DrawGraph(gx + 3 * gap + 5, gy, gwidth, gheight, 0, 30, Units == "M" ? TXT_SNOWFALL_MM : TXT_SNOWFALL_IN, snow_readings, max_readings, autoscale_on, barchart_on);
}

void DisplayConditionsSection(int x, int y, String IconName, bool IconSize)
{
    Serial.println("Icon name: " + IconName);
    if (IconName == "01d" || IconName == "01n")
        Sunny(x, y, IconSize, IconName);
    else if (IconName == "02d" || IconName == "02n")
        MostlySunny(x, y, IconSize, IconName);
    else if (IconName == "03d" || IconName == "03n")
        Cloudy(x, y, IconSize, IconName);
    else if (IconName == "04d" || IconName == "04n")
        MostlySunny(x, y, IconSize, IconName);
    else if (IconName == "09d" || IconName == "09n")
        ChanceRain(x, y, IconSize, IconName);
    else if (IconName == "10d" || IconName == "10n")
        Rain(x, y, IconSize, IconName);
    else if (IconName == "11d" || IconName == "11n")
        Tstorms(x, y, IconSize, IconName);
    else if (IconName == "13d" || IconName == "13n")
        Snow(x, y, IconSize, IconName);
    else if (IconName == "50d")
        Haze(x, y, IconSize, IconName);
    else if (IconName == "50n")
        Fog(x, y, IconSize, IconName);
    else
        Nodata(x, y, IconSize, IconName);
}

void arrow(int x, int y, int asize, float aangle, int pwidth, int plength)
{
    float dx = (asize - 10) * cos((aangle - 90) * PI / 180) + x; // calculate X position
    float dy = (asize - 10) * sin((aangle - 90) * PI / 180) + y; // calculate Y position
    float x1 = 0;
    float y1 = plength;
    float x2 = pwidth / 2;
    float y2 = pwidth / 2;
    float x3 = -pwidth / 2;
    float y3 = pwidth / 2;
    float angle = aangle * PI / 180 - 135;
    float xx1 = x1 * cos(angle) - y1 * sin(angle) + dx;
    float yy1 = y1 * cos(angle) + x1 * sin(angle) + dy;
    float xx2 = x2 * cos(angle) - y2 * sin(angle) + dx;
    float yy2 = y2 * cos(angle) + x2 * sin(angle) + dy;
    float xx3 = x3 * cos(angle) - y3 * sin(angle) + dx;
    float yy3 = y3 * cos(angle) + x3 * sin(angle) + dy;
    fillTriangle(xx1, yy1, xx3, yy3, xx2, yy2, Black);
}

void DrawSegment(int x, int y, int o1, int o2, int o3, int o4, int o11, int o12, int o13, int o14)
{
    drawLine(x + o1, y + o2, x + o3, y + o4, Black);
    drawLine(x + o11, y + o12, x + o13, y + o14, Black);
}

void DrawPressureAndTrend(int x, int y, float pressure, String slope)
{
    drawString(x + 25, y - 22, String(pressure, (Units == "M" ? 0 : 1)) + (Units == "M" ? "hPa" : "in"), LEFT);
    if (slope == "+")
    {
        DrawSegment(x, y, 0, 0, 8, -8, 8, -8, 16, 0);
        DrawSegment(x - 1, y, 0, 0, 8, -8, 8, -8, 16, 0);
    }
    else if (slope == "0")
    {
        DrawSegment(x, y, 8, -8, 16, 0, 8, 8, 16, 0);
        DrawSegment(x - 1, y, 8, -8, 16, 0, 8, 8, 16, 0);
    }
    else if (slope == "-")
    {
        DrawSegment(x, y, 0, 0, 8, 8, 8, 8, 16, 0);
        DrawSegment(x - 1, y, 0, 0, 8, 8, 8, 8, 16, 0);
    }
}

void DisplayStatusSection(int x, int y, int rssi)
{
    setFont(Spleen24);
    DrawRSSI(x + 305, y + 15, rssi);
    DrawBattery(x - 45, y + 4);
}

void DrawRSSI(int x, int y, int rssi)
{
    int WIFIsignal = 0;
    int xpos = 1;
    for (int _rssi = -100; _rssi <= rssi; _rssi = _rssi + 20)
    {
        if (_rssi <= -20)
            WIFIsignal = 30; //            <-20dbm displays 5-bars
        if (_rssi <= -40)
            WIFIsignal = 24; //  -40dbm to  -21dbm displays 4-bars
        if (_rssi <= -60)
            WIFIsignal = 18; //  -60dbm to  -41dbm displays 3-bars
        if (_rssi <= -80)
            WIFIsignal = 12; //  -80dbm to  -61dbm displays 2-bars
        if (_rssi <= -100)
            WIFIsignal = 6; // -100dbm to  -81dbm displays 1-bar
        fillRect(x + xpos * 8, y - WIFIsignal, 6, WIFIsignal, Black);
        xpos++;
    }
}

boolean UpdateLocalTime()
{
    struct tm timeinfo;
    char time_output[30], day_output[30], update_time[30];
    while (!getLocalTime(&timeinfo, 5000))
    { // Wait for 5-sec for time to synchronise
        Serial.println("Failed to obtain time");
        return false;
    }
    CurrentHour = timeinfo.tm_hour;
    CurrentMin = timeinfo.tm_min;
    CurrentSec = timeinfo.tm_sec;
    //See http://www.cplusplus.com/reference/ctime/strftime/
    Serial.println(&timeinfo, "%a %b %d %Y   %H:%M:%S"); // Displays: Saturday, June 24 2017 14:05:49
    if (Units == "M")
    {
        sprintf(day_output, "%s, %02u %s %04u", weekday_D[timeinfo.tm_wday], timeinfo.tm_mday, month_M[timeinfo.tm_mon], (timeinfo.tm_year) + 1900);
        strftime(update_time, sizeof(update_time), "%H:%M:%S", &timeinfo); // Creates: '@ 14:05:49'   and change from 30 to 8 <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
        sprintf(time_output, "%s", update_time);
    }
    else
    {
        strftime(day_output, sizeof(day_output), "%a %b-%d-%Y", &timeinfo); // Creates  'Sat May-31-2019'
        strftime(update_time, sizeof(update_time), "%r", &timeinfo);        // Creates: '@ 02:05:49pm'
        sprintf(time_output, "%s", update_time);
    }
    Date_str = day_output;
    Time_str = time_output;
    return true;
}

void DrawBattery(int x, int y)
{
    float voltage = BatteryVoltage;
    if (voltage > 1)
    { // Only display if there is a valid reading
        uint8_t percentage = BatteryPercentage(voltage);
        Serial.println("\nVoltage = " + String(voltage));
        drawRect(x + 25, y - 5, 40, 15, Black);
        fillRect(x + 65, y - 1, 4, 7, Black);
        fillRect(x + 27, y - 3, 36 * percentage / 100.0, 11, Black);
        drawString(x + 85, y - 14, String(percentage) + "%  " + String(voltage, 1) + "v ~" + FormatBatteryLifeEstimate(), LEFT);
    }
}

// Symbols are drawn on a relative 10x10grid and 1 scale unit = 1 drawing unit
void addcloud(int x, int y, int scale, int linesize)
{
    fillCircle(x - scale * 3, y, scale, Black);                                                              // Left most circle
    fillCircle(x + scale * 3, y, scale, Black);                                                              // Right most circle
    fillCircle(x - scale, y - scale, scale * 1.4, Black);                                                    // left middle upper circle
    fillCircle(x + scale * 1.5, y - scale * 1.3, scale * 1.75, Black);                                       // Right middle upper circle
    fillRect(x - scale * 3 - 1, y - scale, scale * 6, scale * 2 + 1, Black);                                 // Upper and lower lines
    fillCircle(x - scale * 3, y, scale - linesize, White);                                                   // Clear left most circle
    fillCircle(x + scale * 3, y, scale - linesize, White);                                                   // Clear right most circle
    fillCircle(x - scale, y - scale, scale * 1.4 - linesize, White);                                         // left middle upper circle
    fillCircle(x + scale * 1.5, y - scale * 1.3, scale * 1.75 - linesize, White);                            // Right middle upper circle
    fillRect(x - scale * 3 + 2, y - scale + linesize - 1, scale * 5.9, scale * 2 - linesize * 2 + 2, White); // Upper and lower lines
}

void addrain(int x, int y, int scale, bool IconSize)
{
    if (IconSize == SmallIcon)
    {
        setFont(Spleen16);
        drawString(x - 25, y + 12, "///////", LEFT);
    }
    else
    {
        setFont(Spleen24);
        drawString(x - 60, y + 25, "///////", LEFT);
    }
}

void addsnow(int x, int y, int scale, bool IconSize)
{
    if (IconSize == SmallIcon)
    {
        setFont(Spleen16);
        drawString(x - 25, y + 15, "* * * *", LEFT);
    }
    else
    {
        setFont(Spleen24);
        drawString(x - 60, y + 30, "* * * *", LEFT);
    }
}

void addtstorm(int x, int y, int scale)
{
    y = y + scale / 2;
    for (int i = 0; i < 5; i++)
    {
        drawLine(x - scale * 4 + scale * i * 1.5 + 0, y + scale * 1.5, x - scale * 3.5 + scale * i * 1.5 + 0, y + scale, Black);
        if (scale != Small)
        {
            drawLine(x - scale * 4 + scale * i * 1.5 + 1, y + scale * 1.5, x - scale * 3.5 + scale * i * 1.5 + 1, y + scale, Black);
            drawLine(x - scale * 4 + scale * i * 1.5 + 2, y + scale * 1.5, x - scale * 3.5 + scale * i * 1.5 + 2, y + scale, Black);
        }
        drawLine(x - scale * 4 + scale * i * 1.5, y + scale * 1.5 + 0, x - scale * 3 + scale * i * 1.5 + 0, y + scale * 1.5 + 0, Black);
        if (scale != Small)
        {
            drawLine(x - scale * 4 + scale * i * 1.5, y + scale * 1.5 + 1, x - scale * 3 + scale * i * 1.5 + 0, y + scale * 1.5 + 1, Black);
            drawLine(x - scale * 4 + scale * i * 1.5, y + scale * 1.5 + 2, x - scale * 3 + scale * i * 1.5 + 0, y + scale * 1.5 + 2, Black);
        }
        drawLine(x - scale * 3.5 + scale * i * 1.4 + 0, y + scale * 2.5, x - scale * 3 + scale * i * 1.5 + 0, y + scale * 1.5, Black);
        if (scale != Small)
        {
            drawLine(x - scale * 3.5 + scale * i * 1.4 + 1, y + scale * 2.5, x - scale * 3 + scale * i * 1.5 + 1, y + scale * 1.5, Black);
            drawLine(x - scale * 3.5 + scale * i * 1.4 + 2, y + scale * 2.5, x - scale * 3 + scale * i * 1.5 + 2, y + scale * 1.5, Black);
        }
    }
}

void addsun(int x, int y, int scale, bool IconSize)
{
    int linesize = 5;
    fillRect(x - scale * 2, y, scale * 4, linesize, Black);
    fillRect(x, y - scale * 2, linesize, scale * 4, Black);
    drawLine(x - scale * 1.3, y - scale * 1.3, x + scale * 1.3, y + scale * 1.3, Black);
    drawLine(x - scale * 1.3, y + scale * 1.3, x + scale * 1.3, y - scale * 1.3, Black);
    if (IconSize == LargeIcon)
    {
        drawLine(1 + x - scale * 1.3, y - scale * 1.3, 1 + x + scale * 1.3, y + scale * 1.3, Black);
        drawLine(2 + x - scale * 1.3, y - scale * 1.3, 2 + x + scale * 1.3, y + scale * 1.3, Black);
        drawLine(3 + x - scale * 1.3, y - scale * 1.3, 3 + x + scale * 1.3, y + scale * 1.3, Black);
        drawLine(1 + x - scale * 1.3, y + scale * 1.3, 1 + x + scale * 1.3, y - scale * 1.3, Black);
        drawLine(2 + x - scale * 1.3, y + scale * 1.3, 2 + x + scale * 1.3, y - scale * 1.3, Black);
        drawLine(3 + x - scale * 1.3, y + scale * 1.3, 3 + x + scale * 1.3, y - scale * 1.3, Black);
    }
    fillCircle(x, y, scale * 1.3, White);
    fillCircle(x, y, scale, Black);
    fillCircle(x, y, scale - linesize, White);
}

void addfog(int x, int y, int scale, int linesize, bool IconSize)
{
    if (IconSize == SmallIcon)
    {
        y -= 10;
        linesize = 1;
    }
    for (int i = 0; i < 6; i++)
    {
        fillRect(x - scale * 3, y + scale * 1.5, scale * 6, linesize, Black);
        fillRect(x - scale * 3, y + scale * 2.0, scale * 6, linesize, Black);
        fillRect(x - scale * 3, y + scale * 2.5, scale * 6, linesize, Black);
    }
}

void Sunny(int x, int y, bool IconSize, String IconName)
{
    int scale = Small, Offset = 10;
    if (IconSize == LargeIcon)
    {
        scale = Large;
        Offset = 35;
    }
    else
        y = y - 3; // Shift up small sun icon
    if (IconName.endsWith("n"))
        addmoon(x, y + Offset, scale, IconSize);
    scale = scale * 1.6;
    addsun(x, y, scale, IconSize);
}

void MostlySunny(int x, int y, bool IconSize, String IconName)
{
    int scale = Small, linesize = 5, Offset = 10;
    if (IconSize == LargeIcon)
    {
        scale = Large;
        Offset = 35;
    }
    if (IconName.endsWith("n"))
        addmoon(x, y + Offset, scale, IconSize);
    addsun(x - scale * 1.8, y - scale * 1.8, scale, IconSize);
    addcloud(x, y, scale, linesize);
}

void MostlyCloudy(int x, int y, bool IconSize, String IconName)
{
    int scale = Small, linesize = 5, Offset = 10;
    if (IconSize == LargeIcon)
    {
        scale = Large;
        Offset = 35;
    }
    if (IconName.endsWith("n"))
        addmoon(x, y + Offset, scale, IconSize);
    addcloud(x, y, scale, linesize);
    addsun(x - scale * 1.8, y - scale * 1.8, scale, IconSize);
}

void Cloudy(int x, int y, bool IconSize, String IconName)
{
    int scale = Small, linesize = 5, Offset = 10;
    if (IconSize == LargeIcon)
    {
        scale = Large;
        Offset = 35;
    }
    if (IconName.endsWith("n"))
        addmoon(x, y + Offset, scale, IconSize);
    addcloud(x + 15, y - 22, scale / 2, linesize); // Cloud top right
    addcloud(x - 10, y - 18, scale / 2, linesize); // Cloud top left
    addcloud(x, y, scale, linesize);               // Main cloud
}

void Rain(int x, int y, bool IconSize, String IconName)
{
    int scale = Small, linesize = 5, Offset = 10;
    if (IconSize == LargeIcon)
    {
        scale = Large;
        Offset = 35;
    }
    if (IconName.endsWith("n"))
        addmoon(x, y + Offset, scale, IconSize);
    addcloud(x, y, scale, linesize);
    addrain(x, y, scale, IconSize);
}

void ExpectRain(int x, int y, bool IconSize, String IconName)
{
    int scale = Small, linesize = 5, Offset = 10;
    if (IconSize == LargeIcon)
    {
        scale = Large;
        Offset = 35;
    }
    if (IconName.endsWith("n"))
        addmoon(x, y + Offset, scale, IconSize);
    addsun(x - scale * 1.8, y - scale * 1.8, scale, IconSize);
    addcloud(x, y, scale, linesize);
    addrain(x, y, scale, IconSize);
}

void ChanceRain(int x, int y, bool IconSize, String IconName)
{
    int scale = Small, linesize = 5, Offset = 10;
    ;
    if (IconSize == LargeIcon)
    {
        scale = Large;
        Offset = 35;
    }
    if (IconName.endsWith("n"))
        addmoon(x, y + Offset, scale, IconSize);
    addsun(x - scale * 1.8, y - scale * 1.8, scale, IconSize);
    addcloud(x, y, scale, linesize);
    addrain(x, y, scale, IconSize);
}

void Tstorms(int x, int y, bool IconSize, String IconName)
{
    int scale = Small, linesize = 5, Offset = 10;
    if (IconSize == LargeIcon)
    {
        scale = Large;
        Offset = 35;
    }
    if (IconName.endsWith("n"))
        addmoon(x, y + Offset, scale, IconSize);
    addcloud(x, y, scale, linesize);
    addtstorm(x, y, scale);
}

void Snow(int x, int y, bool IconSize, String IconName)
{
    int scale = Small, linesize = 5, Offset = 10;
    if (IconSize == LargeIcon)
    {
        scale = Large;
        Offset = 35;
    }
    if (IconName.endsWith("n"))
        addmoon(x, y + Offset, scale, IconSize);
    addcloud(x, y, scale, linesize);
    addsnow(x, y, scale, IconSize);
}

void Fog(int x, int y, bool IconSize, String IconName)
{
    int scale = Small, linesize = 5, Offset = 10;
    if (IconSize == LargeIcon)
    {
        scale = Large;
        Offset = 35;
    }
    if (IconName.endsWith("n"))
        addmoon(x, y + Offset, scale, IconSize);
    addcloud(x, y - 5, scale, linesize);
    addfog(x, y - 5, scale, linesize, IconSize);
}

void Haze(int x, int y, bool IconSize, String IconName)
{
    int scale = Small, linesize = 5, Offset = 10;
    if (IconSize == LargeIcon)
    {
        scale = Large;
        Offset = 35;
    }
    if (IconName.endsWith("n"))
        addmoon(x, y + Offset, scale, IconSize);
    addsun(x, y - 5, scale * 1.4, IconSize);
    addfog(x, y - 5, scale * 1.4, linesize, IconSize);
}

void CloudCover(int x, int y, int CCover)
{
    addcloud(x - 9, y + 2, Small * 0.3, 2); // Cloud top left
    addcloud(x + 3, y - 2, Small * 0.3, 2); // Cloud top right
    addcloud(x, y + 10, Small * 0.6, 2);    // Main cloud
    drawString(x + 20, y - 12, String(CCover) + "%", LEFT);
}

void Visibility(int x, int y, String Visi)
{
    float start_angle = 0.52, end_angle = 2.61, Offset = 8;
    int r = 14;
    for (float i = start_angle; i < end_angle; i = i + 0.05)
    {
        drawPixel(x + r * cos(i), y - r / 2 + r * sin(i) + Offset, Black);
        drawPixel(x + r * cos(i), 1 + y - r / 2 + r * sin(i) + Offset, Black);
    }
    start_angle = 3.61;
    end_angle = 5.78;
    for (float i = start_angle; i < end_angle; i = i + 0.05)
    {
        drawPixel(x + r * cos(i), y + r / 2 + r * sin(i) + Offset, Black);
        drawPixel(x + r * cos(i), 1 + y + r / 2 + r * sin(i) + Offset, Black);
    }
    fillCircle(x, y + Offset, r / 4, Black);
    drawString(x + 20, y - 12, Visi, LEFT);
}

void addmoon(int x, int y, int scale, bool IconSize)
{
    if (IconSize == LargeIcon)
    {
        fillCircle(x - 85, y - 100, uint16_t(scale * 0.8), Black);
        fillCircle(x - 57, y - 100, uint16_t(scale * 1.6), White);
    }
    else
    {
        fillCircle(x - 28, y - 37, uint16_t(scale * 1.0), Black);
        fillCircle(x - 20, y - 37, uint16_t(scale * 1.6), White);
    }
}

void Nodata(int x, int y, bool IconSize, String IconName)
{
    if (IconSize == LargeIcon)
        setFont(Spleen64Bold);
    else
        setFont(Spleen24);
    drawString(x - 3, y - 10, "?", CENTER);
}

/* (C) D L BIRD
    This function will draw a graph on a ePaper/TFT/LCD display using data from an array containing data to be graphed.
    The variable 'max_readings' determines the maximum number of data elements for each array. Call it with the following parametric data:
    x_pos-the x axis top-left position of the graph
    y_pos-the y-axis top-left position of the graph, e.g. 100, 200 would draw the graph 100 pixels along and 200 pixels down from the top-left of the screen
    width-the width of the graph in pixels
    height-height of the graph in pixels
    Y1_Max-sets the scale of plotted data, for example 5000 would scale all data to a Y-axis of 5000 maximum
    data_array1 is parsed by value, externally they can be called anything else, e.g. within the routine it is called data_array1, but externally could be temperature_readings
    auto_scale-a logical value (TRUE or FALSE) that switches the Y-axis autoscale On or Off
    barchart_on-a logical value (TRUE or FALSE) that switches the drawing mode between barhcart and line graph
    barchart_colour-a sets the title and graph plotting colour
    If called with Y!_Max value of 500 and the data never goes above 500, then autoscale will retain a 0-500 Y scale, if on, the scale increases/decreases to match the data.
    auto_scale_margin, e.g. if set to 1000 then autoscale increments the scale by 1000 steps.
*/
void DrawGraph(int x_pos, int y_pos, int gwidth, int gheight, float Y1Min, float Y1Max, String title, float DataArray[], int readings, boolean auto_scale, boolean barchart_mode)
{
#define auto_scale_margin 0 // Sets the autoscale increment, so axis steps up fter a change of e.g. 3
#define y_minor_axis 5      // 5 y-axis division markers
    setFont(Spleen24);
    int maxYscale = -10000;
    int minYscale = 10000;
    int last_x, last_y;
    float x2, y2;
    if (auto_scale == true)
    {
        for (int i = 1; i < readings; i++)
        {
            if (DataArray[i] >= maxYscale)
                maxYscale = DataArray[i];
            if (DataArray[i] <= minYscale)
                minYscale = DataArray[i];
        }
        maxYscale = round(maxYscale + auto_scale_margin); // Auto scale the graph and round to the nearest value defined, default was Y1Max
        Y1Max = round(maxYscale + 0.5);
        if (minYscale != 0)
            minYscale = round(minYscale - auto_scale_margin); // Auto scale the graph and round to the nearest value defined, default was Y1Min
        Y1Min = round(minYscale);
    }
    // Draw the graph
    last_x = x_pos + 1;
    last_y = y_pos + (Y1Max - constrain(DataArray[1], Y1Min, Y1Max)) / (Y1Max - Y1Min) * gheight;
    drawRect(x_pos, y_pos, gwidth + 3, gheight + 2, Grey);
    drawString(x_pos - 20 + gwidth / 2, y_pos - 34, title, CENTER);
    for (int gx = 0; gx < readings; gx++)
    {
        x2 = x_pos + gx * gwidth / (readings - 1) - 1; // max_readings is the global variable that sets the maximum data that can be plotted
        y2 = y_pos + (Y1Max - constrain(DataArray[gx], Y1Min, Y1Max)) / (Y1Max - Y1Min) * gheight + 1;
        if (barchart_mode)
        {
            fillRect(last_x + 2, y2, (gwidth / readings) - 1, y_pos + gheight - y2 + 2, Black);
        }
        else
        {
            drawLine(last_x, last_y - 1, x2, y2 - 1, Black); // Two lines for hi-res display
            drawLine(last_x, last_y, x2, y2, Black);
        }
        last_x = x2;
        last_y = y2;
    }
    //Draw the Y-axis scale
#define number_of_dashes 20
    for (int spacing = 0; spacing <= y_minor_axis; spacing++)
    {
        for (int j = 0; j < number_of_dashes; j++)
        { // Draw dashed graph grid lines
            if (spacing < y_minor_axis)
                drawFastHLine((x_pos + 3 + j * gwidth / number_of_dashes), y_pos + (gheight * spacing / y_minor_axis), gwidth / (2 * number_of_dashes), Grey);
        }
        if ((Y1Max - (float)(Y1Max - Y1Min) / y_minor_axis * spacing) < 5 || title == TXT_PRESSURE_IN)
        {
            drawString(x_pos - 10, y_pos + gheight * spacing / y_minor_axis - 14, String((Y1Max - (float)(Y1Max - Y1Min) / y_minor_axis * spacing + 0.01), 1), RIGHT);
        }
        else
        {
            if (Y1Min < 1 && Y1Max < 10)
            {
                drawString(x_pos - 3, y_pos + gheight * spacing / y_minor_axis - 14, String((Y1Max - (float)(Y1Max - Y1Min) / y_minor_axis * spacing + 0.01), 1), RIGHT);
            }
            else
            {
                drawString(x_pos - 7, y_pos + gheight * spacing / y_minor_axis - 14, String((Y1Max - (float)(Y1Max - Y1Min) / y_minor_axis * spacing + 0.01), 0), RIGHT);
            }
        }
    }
    for (int i = 0; i < 3; i++)
    {
        drawString(20 + x_pos + gwidth / 3 * i, y_pos + gheight + 1, String(i) + "d", LEFT);
        if (i < 2)
            drawFastVLine(x_pos + gwidth / 3 * i + gwidth / 3, y_pos, gheight, LightGrey);
    }
}

void drawString(int x, int y, String text, alignment align)
{
    char *data = const_cast<char *>(text.c_str());
    int x1, y1; //the bounds of x,y and w and h of the variable 'text' in pixels.
    int w, h;
    int xx = x, yy = y;
    get_text_bounds(&currentFont, data, &xx, &yy, &x1, &y1, &w, &h, NULL);
    if (align == RIGHT)
        x = x - w;
    if (align == CENTER)
        x = x - w / 2;
    int cursor_y = y + h;
    write_string(&currentFont, data, &x, &cursor_y, framebuffer);
}

void fillCircle(int x, int y, int r, uint8_t color)
{
    epd_fill_circle(x, y, r, color, framebuffer);
}

void drawFastHLine(int16_t x0, int16_t y0, int length, uint16_t color)
{
    epd_draw_hline(x0, y0, length, color, framebuffer);
}

void drawFastVLine(int16_t x0, int16_t y0, int length, uint16_t color)
{
    epd_draw_vline(x0, y0, length, color, framebuffer);
}

void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color)
{
    epd_write_line(x0, y0, x1, y1, color, framebuffer);
}

void drawCircle(int x0, int y0, int r, uint8_t color)
{
    epd_draw_circle(x0, y0, r, color, framebuffer);
}

void drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color)
{
    epd_draw_rect(x, y, w, h, color, framebuffer);
}

void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color)
{
    epd_fill_rect(x, y, w, h, color, framebuffer);
}

void fillTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                  int16_t x2, int16_t y2, uint16_t color)
{
    epd_fill_triangle(x0, y0, x1, y1, x2, y2, color, framebuffer);
}

void drawPixel(int x, int y, uint8_t color)
{
    epd_draw_pixel(x, y, color, framebuffer);
}

void setFont(GFXfont const &font)
{
    currentFont = font;
}

void edp_update()
{
    epd_draw_grayscale_image(epd_full_screen(), framebuffer); // Update the screen
}
/*
   1085 lines of code 28-01-2021
*/
