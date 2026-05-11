#include "RTC.h"
#include "../../include/pins.h"
#include <Wire.h>
#include <RTClib.h>
#include <time.h>

static RTC_DS3231 rtc;
static bool       rtcOk = false;

void RTC_Init()
{
    // Wire.begin() is called earlier by Sensors_Init() — do not call again
    rtcOk = rtc.begin(&Wire);
    if (!rtcOk) {
        Serial.println("RTC: DS3231 not found on I2C");
        return;
    }
    if (rtc.lostPower())
        Serial.println("RTC: lost power / no battery — time not set");
    Serial.println("RTC: init OK");
}

void RTC_NTPSync()
{
    if (!rtcOk) return;

    // UK timezone: GMT in winter, BST (UTC+1) in summer
    configTime(0, 0, "pool.ntp.org", "time.google.com");
    setenv("TZ", "GMT0BST,M3.5.0/1,M10.5.0", 1);
    tzset();

    // Block up to 2 s for NTP response — usually <500 ms on home WiFi
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo, 2000)) {
        Serial.println("RTC: NTP sync failed (timeout)");
        return;
    }

    rtc.adjust(DateTime(
        timeinfo.tm_year + 1900,
        timeinfo.tm_mon  + 1,
        timeinfo.tm_mday,
        timeinfo.tm_hour,
        timeinfo.tm_min,
        timeinfo.tm_sec
    ));
    Serial.printf("RTC: NTP sync OK — %02d/%02d/%04d %02d:%02d:%02d\n",
                  timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900,
                  timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
}

bool RTC_IsRunning() { return rtcOk; }

void RTC_GetDateTimeStr(char* buf, int maxLen)
{
    if (!buf || maxLen < 2) return;
    if (!rtcOk) { buf[0] = '\0'; return; }

    static const char* kMonths[] = {
        "Jan","Feb","Mar","Apr","May","Jun",
        "Jul","Aug","Sep","Oct","Nov","Dec"
    };
    DateTime now = rtc.now();
    snprintf(buf, maxLen, "%02d %s %04d  %02d:%02d",
             now.day(),
             kMonths[constrain((int)now.month() - 1, 0, 11)],
             now.year(),
             now.hour(),
             now.minute());
}

void RTC_GetHeaderStr(char* buf, int maxLen)
{
    if (!buf || maxLen < 2) return;
    if (!rtcOk) { buf[0] = '\0'; return; }

    static const char* kMonths[] = {
        "Jan","Feb","Mar","Apr","May","Jun",
        "Jul","Aug","Sep","Oct","Nov","Dec"
    };
    DateTime now = rtc.now();
    snprintf(buf, maxLen, "%02d %s %02d:%02d",
             now.day(),
             kMonths[constrain((int)now.month() - 1, 0, 11)],
             now.hour(),
             now.minute());
}

uint32_t RTC_GetEpoch()
{
    if (!rtcOk) return 0;
    return rtc.now().unixtime();
}
