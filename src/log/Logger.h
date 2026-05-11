#pragma once
#include <Arduino.h>

// ═══════════════════════════════════════════════════════════════════
// MCP Fuel Station V2 — Event Logger
// Writes timestamped CSV to LittleFS /logs/log.csv.
// Rotates to log_old.csv when current file exceeds LOG_MAX_BYTES.
// ═══════════════════════════════════════════════════════════════════

enum LogLevel {
    LOG_INFO  = 0,  // normal operation
    LOG_WARN  = 1,  // degraded / advisory
    LOG_ERROR = 2,  // recoverable fault
    LOG_FAULT = 3   // serious fault (crash, hardware fail)
};

enum LogCategory {
    CAT_SYSTEM  = 0,
    CAT_PUMP    = 1,
    CAT_SENSOR  = 2,
    CAT_POWER   = 3,
    CAT_NETWORK = 4
};

// Call once after LittleFS is mounted (after HeliLib_Init).
void Logger_Init();

// Write one log entry.  val1/val2 are optional numeric values —
// pass NAN to omit.  detail is optional free-text (may be nullptr).
void Logger_Write(LogLevel level, LogCategory cat, const char* event,
                  const char* detail = nullptr,
                  float val1 = NAN, float val2 = NAN);

// File sizes in bytes (0 if file absent).
size_t Logger_CurrentSize();
size_t Logger_ArchiveSize();

// Pump-state persistence across power loss.
// Call SavePumpState at pump start and every ~10 s during pumping.
// Call ClearPumpState at every normal stop path.
// Logger_Init() checks for a leftover state file and logs PUMP_INTERRUPTED if found.
void Logger_SavePumpState(const char* op, const char* model, int targetMl, int volMl);
void Logger_ClearPumpState();
