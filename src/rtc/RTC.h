#pragma once
#include <Arduino.h>

// ═══════════════════════════════════════════════════════════════════
// MCP Fuel Station V2 — DS3231 RTC wrapper
// I2C shared bus; Wire must be started before RTC_Init.
// ═══════════════════════════════════════════════════════════════════

void     RTC_Init();
bool     RTC_NTPSync();   // returns true on success; call after WiFi connects
bool     RTC_IsRunning();

// Fills buf with "10 May 2026  14:32" — display use
void     RTC_GetDateTimeStr(char* buf, int maxLen);

// Fills buf with "2026-05-10 14:32:01" — ISO format for log CSV
void     RTC_GetLogTimestamp(char* buf, int maxLen);

// Fills buf with "10 May 14:32" — compact format for screen header
void     RTC_GetHeaderStr(char* buf, int maxLen);

// Unix timestamp (seconds since 1970) — for log file use
uint32_t RTC_GetEpoch();
