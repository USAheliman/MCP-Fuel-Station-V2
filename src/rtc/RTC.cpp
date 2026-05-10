#include "RTC.h"
#include "../../include/pins.h"
#include <Wire.h>
#include <RTClib.h>

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
    if (rtc.lostPower()) {
        Serial.println("RTC: lost power / no battery — time not set");
        // Uncomment to set to firmware compile time as fallback:
        // rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    }
    Serial.println("RTC: init OK");
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
