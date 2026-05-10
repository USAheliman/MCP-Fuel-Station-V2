#pragma once
#include <Arduino.h>

// ═══════════════════════════════════════════════════════════════════
// MCP Fuel Station V2 — Power Management
// Short press: menu/action  Hold 1.5s: back  Hold 5s: shutdown
// FALLING-edge ISR captures press even during ~60ms display refresh
// ═══════════════════════════════════════════════════════════════════

#define BTN_DEBOUNCE_MS    50
#define BTN_SHORT_PRESS_MS 30
#define BTN_BACK_PRESS_MS  3000
#define BTN_LONG_PRESS_MS  8000
#define SCREEN_STANDBY_MS  600000UL
#define AUTO_SHUTDOWN_MS   900000UL

typedef void (*ShortPressCb)();
typedef void (*BackPressCb)();
typedef void (*ShutdownCb)();

void Power_Init(ShortPressCb onShortPress, BackPressCb onBackPress, ShutdownCb onShutdown);
void Power_Update();
void Power_Shutdown();
void Power_UpdateActivity();
bool Power_IsStandby();
void Power_ExitStandby();
