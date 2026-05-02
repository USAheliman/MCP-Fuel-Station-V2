#pragma once
#include <Arduino.h>
#include <TFT_eSPI.h>

// ═══════════════════════════════════════════════════════════════════
// MCO Fuel Station V2 — TFT Display
// ST7735S 128×160, 3 screens navigated by rotary encoder
//
// Screen 0 — GAUGE    (rainbow arc, heli name, message bar)
// Screen 1 — HELI     (select heli from library, encoder scroll)
// Screen 2 — SESSION  (numeric readouts, session stats)
// ═══════════════════════════════════════════════════════════════════

#define TFT_W  128
#define TFT_H  160

// Colour palette (RGB565)
#define COL_BG        0x0000   // black background
#define COL_HEADER    0x1082   // dark grey header strip
#define COL_MSG_BG    0x0820   // dark status bar
#define COL_WHITE     0xFFFF
#define COL_GREY      0x7BEF
#define COL_DIM       0x2945

// Rainbow arc colours (outermost → innermost)
#define ARC_OUTER_TK  0x07E0   // green  — outer/supply tank
#define ARC_MAIN_TK   0x001F   // blue   — main/heli tank
#define ARC_PUMP      0xFD20   // orange — pump speed
#define ARC_PRESSURE  0x780F   // purple — pressure

// Message bar colours
#define MSG_IDLE      0x07FF   // cyan
#define MSG_FILLING   0x07E0   // green
#define MSG_DRAINING  0xFD20   // orange
#define MSG_WARN      0xF800   // red
#define MSG_COMPLETE  0xFFFF   // white

enum DisplayScreen {
    SCREEN_GAUGE   = 0,
    SCREEN_HELI    = 1,
    SCREEN_SESSION = 2,
    SCREEN_NET     = 3,
    SCREEN_COUNT   = 4
};

// ── Gauge data fed from main loop ────────────────────────────────
struct GaugeData {
    float outerTankPct;    // 0-100 supply/outer tank
    float mainTankPct;     // 0-100 heli tank
    float pumpSpeedPct;    // 0-100 pump speed
    float pressurePct;     // 0-100 pressure (0-4 bar)
    int   flowMlMin;       // actual flow rate
    int   volumeMl;        // dispensed this session
    int   targetMl;        // fill/drain target
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
void Display_Update(const GaugeData &data);   // non-blocking redraw
void Display_SetBrightness(uint8_t val);      // 0-255
void Display_EncoderScroll(int delta);        // +1/-1 for heli select
void Display_EncoderPress();                  // confirm heli selection
int  Display_SelectedHeliIndex();
void Display_SetNetworkIP(const char* staIP, const char* apIP);
