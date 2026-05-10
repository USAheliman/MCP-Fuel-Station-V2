#pragma once
#include <Arduino.h>

// ═══════════════════════════════════════════════════════════════════
// MCP Fuel Station V2 — DS3231 RTC wrapper
// I2C shared with ABP2 pressure sensor; Wire must be started first.
// ═══════════════════════════════════════════════════════════════════

void     RTC_Init();
bool     RTC_IsRunning();

// Fills buf with "10 May 2026  14:32" — for logging / full timestamp
void     RTC_GetDateTimeStr(char* buf, int maxLen);

// Fills buf with "10 May 14:32" — compact format for screen header
void     RTC_GetHeaderStr(char* buf, int maxLen);

// Unix timestamp (seconds since 1970) — for log file use
uint32_t RTC_GetEpoch();
