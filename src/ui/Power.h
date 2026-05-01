#pragma once
#include <Arduino.h>

// ═══════════════════════════════════════════════════════════════════
// MCO Fuel Station V2 — Power Management
// Pololu SV Mini power switch control via GPIO16/17
// Encoder SW doubles as power button (OR gate → Pololu A pin)
// Short press: screen/function  Long press 3s+: shutdown
// ═══════════════════════════════════════════════════════════════════

#define BTN_DEBOUNCE_MS     50
#define BTN_LONG_PRESS_MS 3000
#define BTN_SHORT_PRESS_MS  200
#define SCREEN_STANDBY_MS  600000UL   // 10 min
#define AUTO_SHUTDOWN_MS   900000UL   // 15 min

// Callback types
typedef void (*ShortPressCb)();
typedef void (*ShutdownCb)();

void Power_Init(ShortPressCb onShortPress, ShutdownCb onShutdown);
void Power_Update();              // call every loop
void Power_Shutdown();            // save + cut power
void Power_UpdateActivity();      // reset idle timer
bool Power_IsStandby();
void Power_ExitStandby();
