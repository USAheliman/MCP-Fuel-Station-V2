#pragma once
#include <Arduino.h>
#include <TFT_eSPI.h>

// ═══════════════════════════════════════════════════════════════════
// MCP Fuel Station V2 — TFT Display
// ILI9341 2.8" 320×240 landscape
//
// SCREEN_GAUGE   — rainbow arc gauge, live readouts
// SCREEN_ACTION  — full-screen action selector (fill/drain/model/…)
// SCREEN_HELI    — helicopter model browser
// SCREEN_SESSION — session statistics
// SCREEN_NET     — network / OTA info
// ═══════════════════════════════════════════════════════════════════

#define TFT_W  320
#define TFT_H  240

// Colour palette (RGB565)
#define COL_BG        0x0000
#define COL_HEADER    0x1082
#define COL_MSG_BG    0x0820
#define COL_WHITE     0xFFFF
#define COL_GREY      0x7BEF
#define COL_DIM       0x2945

// Arc colours
#define ARC_OUTER_TK  0x07E0   // green
#define ARC_MAIN_TK   0x001F   // blue
#define ARC_PUMP      0xFD20   // orange
#define ARC_PRESSURE  0x780F   // purple

// Message bar colours
#define MSG_IDLE      0x07FF
#define MSG_FILLING   0x07E0
#define MSG_DRAINING  0xFD20
#define MSG_WARN      0xF800
#define MSG_COMPLETE  0xFFFF

enum DisplayScreen {
    SCREEN_GAUGE   = 0,
    SCREEN_ACTION  = 1,
    SCREEN_HELI    = 2,
    SCREEN_SESSION = 3,
    SCREEN_NET     = 4,
    SCREEN_COUNT   = 5
};

// Action screen items
#define ACTION_FILL    0
#define ACTION_DRAIN   1
#define ACTION_MODEL   2
#define ACTION_SESSION 3
#define ACTION_NETWORK 4
#define ACTION_COUNT   5

// Gauge data fed from main loop
struct GaugeData {
    float outerTankPct;
    float mainTankPct;
    float pumpSpeedPct;
    float pressurePct;
    int   flowMlMin;
    int   volumeMl;
    int   targetMl;
    char  heliName[24];
    char  message[48];
    uint16_t msgColour;
    int   battPct;
    bool  pumpRunning;
    bool  tankFull;
    bool  sensorFitted;
};

// ── API ───────────────────────────────────────────────────────────
void Display_Init();
void Display_SetScreen(DisplayScreen s);
DisplayScreen Display_CurrentScreen();
void Display_Update(const GaugeData &data);
void Display_SetBrightness(uint8_t val);
void Display_EncoderScroll(int delta);
void Display_EncoderPress();
int  Display_SelectedHeliIndex();
void Display_SetNetworkIP(const char* staIP, const char* apIP);
int  Display_GetActionSel();
