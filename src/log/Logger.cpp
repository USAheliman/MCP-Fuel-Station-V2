#include "Logger.h"
#include "../rtc/RTC.h"
#include <LittleFS.h>
#include <math.h>

// ════════════════════════════════════════════════════════════════
// Storage layout
// ════════════════════════════════════════════════════════════════
#define LOG_DIR      "/logs"
#define LOG_FILE     "/logs/log.csv"
#define LOG_OLD      "/logs/log_old.csv"
#define LOG_MAX_BYTES (512UL * 1024UL)   // rotate at 512 KB (~5000 entries)

static const char CSV_HEADER[] =
    "Timestamp,Epoch,Level,Category,Event,Detail,Val1,Val2\n";

// ════════════════════════════════════════════════════════════════
// Helpers
// ════════════════════════════════════════════════════════════════
static const char* levelStr(LogLevel l)
{
    switch (l) {
        case LOG_WARN:  return "WARN";
        case LOG_ERROR: return "ERROR";
        case LOG_FAULT: return "FAULT";
        default:        return "INFO";
    }
}

static const char* catStr(LogCategory c)
{
    switch (c) {
        case CAT_PUMP:    return "PUMP";
        case CAT_SENSOR:  return "SENSOR";
        case CAT_POWER:   return "POWER";
        case CAT_NETWORK: return "NETWORK";
        default:          return "SYSTEM";
    }
}

// ════════════════════════════════════════════════════════════════
// Public API
// ════════════════════════════════════════════════════════════════
void Logger_Init()
{
    if (!LittleFS.exists(LOG_DIR))
        LittleFS.mkdir(LOG_DIR);

    // Write CSV header if log file does not yet exist
    if (!LittleFS.exists(LOG_FILE)) {
        File f = LittleFS.open(LOG_FILE, "w");
        if (f) { f.print(CSV_HEADER); f.close(); }
    }
    Serial.printf("Logger: init OK  current=%u B  archive=%u B\n",
                  (unsigned)Logger_CurrentSize(), (unsigned)Logger_ArchiveSize());
}

void Logger_Write(LogLevel level, LogCategory cat, const char* event,
                  const char* detail, float val1, float val2)
{
    // ── Rotate if file is too large ───────────────────────────────
    size_t curSize = Logger_CurrentSize();
    bool needHeader = (curSize == 0);

    if (curSize >= LOG_MAX_BYTES) {
        if (LittleFS.exists(LOG_OLD)) LittleFS.remove(LOG_OLD);
        LittleFS.rename(LOG_FILE, LOG_OLD);
        needHeader = true;
        Serial.println("Logger: rotated log — archive updated");
    }

    // ── Open for append ───────────────────────────────────────────
    File f = LittleFS.open(LOG_FILE, "a");
    if (!f) {
        Serial.println("Logger: ERROR — cannot open log for write");
        return;
    }
    if (needHeader) f.print(CSV_HEADER);

    // ── Timestamp ─────────────────────────────────────────────────
    char ts[24];
    uint32_t epoch = 0;
    if (RTC_IsRunning()) {
        RTC_GetLogTimestamp(ts, sizeof(ts));
        epoch = RTC_GetEpoch();
    } else {
        snprintf(ts, sizeof(ts), "T+%lus", millis() / 1000);
    }

    // ── Numeric values ────────────────────────────────────────────
    char v1[16] = "", v2[16] = "";
    if (!isnan(val1)) snprintf(v1, sizeof(v1), "%.1f", val1);
    if (!isnan(val2)) snprintf(v2, sizeof(v2), "%.2f", val2);

    // ── CSV line — quote Detail field to handle any commas ────────
    char line[220];
    snprintf(line, sizeof(line), "%s,%lu,%s,%s,%s,\"%s\",%s,%s\n",
             ts, (unsigned long)epoch,
             levelStr(level), catStr(cat),
             event,
             detail ? detail : "",
             v1, v2);

    f.print(line);
    f.close();

    // ── Echo to Serial ────────────────────────────────────────────
    Serial.printf("[%s] %s/%s  %s  %s %s %s\n",
                  ts, catStr(cat), levelStr(level),
                  event,
                  detail ? detail : "",
                  v1, v2);
}

size_t Logger_CurrentSize()
{
    File f = LittleFS.open(LOG_FILE, "r");
    if (!f) return 0;
    size_t s = f.size();
    f.close();
    return s;
}

size_t Logger_ArchiveSize()
{
    File f = LittleFS.open(LOG_OLD, "r");
    if (!f) return 0;
    size_t s = f.size();
    f.close();
    return s;
}
