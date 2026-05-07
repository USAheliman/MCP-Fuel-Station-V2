#pragma once
#include <Arduino.h>
#include <TFT_eSPI.h>

// ═══════════════════════════════════════════════════════════════════
// MCP Fuel Station V2 — TFT Display
// ST7796S 3.5" 480×320 IPS landscape
// ═══════════════════════════════════════════════════════════════════

#define TFT_W  480
#define TFT_H  320

// ── Colour palette (RGB565) ──────────────────────────────────────
#define COL_BG       0x0000
#define COL_HEADER   0x1082
#define COL_CARD     0x0822   // left panel dark card
#define COL_PANEL    0x0841
#define COL_TRACK    0x4208   // progress bar empty track (dark grey, visible on black)
#define COL_WHITE    0xFFFF
#define COL_YELLOW   0xFFE0
#define COL_CYAN     0x07FF
#define COL_GREY     0x7BEF
#define COL_DIM      0x2945
#define COL_GREEN    0x07E0
#define COL_ORANGE   0xFD20
#define COL_RED      0xF800
#define COL_BLUE     0x4BBF
#define COL_PURPLE   0xB81F

// ── Status bar colours ───────────────────────────────────────────
#define MSG_IDLE     0x07FF
#define MSG_FILLING  0x07E0
#define MSG_DRAINING 0xFD20
#define MSG_WARN     0xF800
#define MSG_COMPLETE 0xFFFF

// ── Screen IDs ───────────────────────────────────────────────────
enum DisplayScreen {
    SCREEN_GAUGE   = 0,
    SCREEN_ACTION  = 1,
    SCREEN_HELI    = 2,
    SCREEN_SESSION = 3,
    SCREEN_NET     = 4,
    SCREEN_COUNT   = 5
};

// ── Action items ─────────────────────────────────────────────────
#define ACTION_FILL    0
#define ACTION_DRAIN   1
#define ACTION_MODEL   2
#define ACTION_SESSION 3
#define ACTION_NETWORK 4
#define ACTION_COUNT   5

// ── Live data fed from main loop ─────────────────────────────────
struct GaugeData {
    float    outerTankPct;
    float    mainTankPct;
    float    pumpSpeedPct;
    float    pressurePct;
    int      flowMlMin;
    int      volumeMl;
    int      targetMl;
    int      supplyMl;
    int      supplyCapMl;
    int      supplyLowMl;
    char     heliName[24];
    char     message[48];
    uint16_t msgColour;
    int      battPct;
    bool     pumpRunning;
    bool     tankFull;
    bool     sensorFitted;
};

// ── Public API ───────────────────────────────────────────────────
void          Display_Init();
void          Display_SetScreen(DisplayScreen s);
DisplayScreen Display_CurrentScreen();
uint32_t      Display_GetScreenAge();
void          Display_Update(const GaugeData &data);
void          Display_SetBrightness(uint8_t val);
void          Display_EncoderScroll(int delta);
void          Display_EncoderPress();
int           Display_SelectedHeliIndex();
void          Display_SetNetworkIP(const char* staIP, const char* apIP);
int           Display_GetActionSel();
