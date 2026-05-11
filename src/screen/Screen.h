#pragma once
#include <Arduino.h>

// ═══════════════════════════════════════════════════════════════════
// MCP Fuel Station V2 — Screen Module
// ST7796S 3.5" 480×320 IPS landscape, full-sprite rendering
// ═══════════════════════════════════════════════════════════════════

// ── Screen IDs ───────────────────────────────────────────────────
enum ScreenID {
    SCREEN_HOME  = 0,
    SCREEN_MODEL = 1,
    SCREEN_NET   = 2,
    SCREEN_HELP  = 3,
    SCREEN_COUNT = 4
};

// ── Action button indices (HOME idle button strip) ───────────────
#define ACTION_FILL    0
#define ACTION_DRAIN   1
#define ACTION_MODEL   2
#define ACTION_NET     3
#define ACTION_HELP    4
#define ACTION_COUNT   5

// ── Message bar colours (RGB565) ─────────────────────────────────
#define MSG_IDLE     0x07FF   // cyan
#define MSG_FILLING  0x07E0   // green
#define MSG_DRAINING 0xFD20   // orange
#define MSG_WARN     0xF800   // red
#define MSG_COMPLETE 0xFFFF   // white

// ── Live data fed from main loop ─────────────────────────────────
struct ScreenData {
    float    outerTankPct;   // supply tank %
    float    mainTankPct;    // model tank %
    float    pumpSpeedPct;
    float    pressurePct;
    int      flowMlMin;
    int      volumeMl;
    int      targetMl;
    int      supplyMl;
    int      supplyCapMl;
    int      supplyLowMl;
    char     modelName[24];
    char     message[48];
    uint16_t msgColour;
    int      battPct;
    bool     pumpRunning;
    bool     tankFull;
    bool     sensorFitted;
};

// ── Public API ───────────────────────────────────────────────────
void     Screen_Init();
void     Screen_ShowSplash();
void     Screen_Update(const ScreenData& data);
void     Screen_SetScreen(ScreenID s);
ScreenID Screen_CurrentScreen();
uint32_t Screen_GetScreenAge();
void     Screen_SetBrightness(uint8_t val);
void     Screen_EncoderScroll(int delta);
void     Screen_EncoderPress();
void     Screen_SetNetworkIP(const char* staIP, const char* apIP);
int      Screen_GetActionSel();

// Post-pump review — stay on fill/drain screen after pump stops
void     Screen_SetPostPump(bool isDrain);  // enter: shows START+BACK, BACK highlighted
void     Screen_ClearPostPump();            // exit: reset to idle HOME state
bool     Screen_IsPostPump();
int      Screen_GetPumpBtnSel();            // 0=STOP(pump on) or START(post-pump), 1=BACK
