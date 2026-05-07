#include "Display.h"
#include "../../include/pins.h"
#include "../heli/HeliLib.h"
#include <LittleFS.h>
#include <TJpg_Decoder.h>

// ═══════════════════════════════════════════════════════════════════
// MCP Fuel Station V2 — TFT Display  ILI9341 320×240 landscape
//
// Field-optimised UI: large fonts, model photo, minimal chrome.
//
// SCREEN_GAUGE   — live gauges, model photo, status (home screen)
// SCREEN_ACTION  — FILL / DRAIN / MODEL / SESSION / NETWORK
// SCREEN_HELI    — model browser (select active model)
// SCREEN_SESSION — session stats
// SCREEN_NET     — network / OTA info
// ═══════════════════════════════════════════════════════════════════

static TFT_eSPI tft = TFT_eSPI();

static DisplayScreen currentScreen   = SCREEN_GAUGE;
static int           heliScrollIdx   = 0;
static bool          needsFullRedraw = true;

static GaugeData lastData;
static bool      lastDataInit = false;

static char gNetStaIP[20] = "";
static char gNetApIP[20]  = "";
static int  actionSel     = 0;

// ─────────────────────────────────────────────────────────────────
// Layout constants  (320×240 landscape)
//
//  y=  0..17   Header bar
//  y= 18..117  Upper panels (model image left | live data right)
//  y=118..211  Arc gauge zone
//  y=212..239  Big status bar
// ─────────────────────────────────────────────────────────────────
#define HDR_H    18
#define PNL_L_W  106     // left panel width  (model image)
#define PNL_TOP  HDR_H
#define PNL_BOT  118     // upper panels bottom / arc zone top
#define PNL_MX   213     // centre x of right panel  (106+320)/2
#define ARC_CX   160
#define ARC_CY   210     // arc base; arc top ≈ 120 ≈ PNL_BOT
#define MSG_Y    212     // status bar y
#define MSG_H    28      // → fills y=212..239

// ─────────────────────────────────────────────────────────────────
// Public helpers
// ─────────────────────────────────────────────────────────────────
void Display_SetNetworkIP(const char* staIP, const char* apIP)
{
    strncpy(gNetStaIP, staIP ? staIP : "", sizeof(gNetStaIP) - 1);
    strncpy(gNetApIP,  apIP  ? apIP  : "", sizeof(gNetApIP)  - 1);
}

void Display_SetBrightness(uint8_t val) { ledcWrite(TFT_BL_LEDC_CHANNEL, val); }

// TJpgDec output callback — writes decoded blocks straight to TFT
static bool jpgOutput(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bmp)
{
    if (y >= TFT_H) return 0;
    tft.pushImage(x, y, w, h, bmp);
    yield();   // keep watchdog fed during slow decode
    return 1;
}

void Display_Init()
{
    ledcSetup(TFT_BL_LEDC_CHANNEL, 5000, 8);
    ledcAttachPin(PIN_TFT_BL, TFT_BL_LEDC_CHANNEL);
    Display_SetBrightness(200);
    tft.init();
    tft.invertDisplay(false);
    tft.setRotation(1);
    tft.fillScreen(COL_BG);
    needsFullRedraw = true;

    TJpgDec.setJpgScale(2);
    TJpgDec.setSwapBytes(true);
    TJpgDec.setCallback(jpgOutput);

    Serial.println("Display: init OK");
}

DisplayScreen Display_CurrentScreen()     { return currentScreen; }
int           Display_SelectedHeliIndex() { return heliScrollIdx; }
int           Display_GetActionSel()      { return actionSel; }

void Display_SetScreen(DisplayScreen s)
{
    currentScreen   = s;
    needsFullRedraw = true;
    if (s == SCREEN_HELI)   heliScrollIdx = activeModelIndex;
    if (s == SCREEN_ACTION) actionSel     = 0;
}

// ─────────────────────────────────────────────────────────────────
// Arc helper — coloured semicircle strip
// ─────────────────────────────────────────────────────────────────
static void drawArc(int cx, int cy, int r, int sw,
                    float frac, uint16_t cFill, uint16_t cBg)
{
    const int steps = 180;
    for (int i = 0; i <= steps; i++) {
        float    a   = PI - (i / (float)steps) * PI;
        uint16_t col = (a >= PI - frac * PI) ? cFill : cBg;
        for (int w = 0; w < sw; w++) {
            int rr = r - w;
            tft.drawPixel(cx + (int)(cosf(a) * rr + 0.5f),
                          cy - (int)(sinf(a) * rr + 0.5f), col);
        }
    }
}

// ─────────────────────────────────────────────────────────────────
// SCREEN 0 — Gauge
// ─────────────────────────────────────────────────────────────────

// Coloured initial-letter placeholder when model has no photo
static void drawModelPlaceholder(const char* name)
{
    tft.fillRect(0, PNL_TOP, PNL_L_W, PNL_BOT - PNL_TOP, 0x0841);
    tft.drawRect(0, PNL_TOP, PNL_L_W, PNL_BOT - PNL_TOP, COL_HEADER);
    char ini[2] = { name[0], '\0' };
    tft.setTextColor(COL_GREY, 0x0841);
    tft.setTextDatum(MC_DATUM);
    tft.setTextSize(4);
    tft.drawString(ini, PNL_L_W / 2, PNL_TOP + (PNL_BOT - PNL_TOP) / 2);
}

// Render model JPEG thumbnail (scale=2); fall back to placeholder
static void drawModelImage(const char* name)
{
    char path[64];
    snprintf(path, sizeof(path), "/models/%s/thumb.jpg", name);
    if (LittleFS.exists(path)) {
        tft.setViewport(0, PNL_TOP, PNL_L_W, PNL_BOT - PNL_TOP);
        TJpgDec.setJpgScale(2);
        TJpgDec.drawFsJpg(0, PNL_TOP, path, LittleFS);
        tft.resetViewport();
    } else {
        drawModelPlaceholder(name);
    }
}

static void drawGaugeFull(const GaugeData &d)
{
    tft.fillScreen(COL_BG);
    char buf[32];

    // ── Header ─────────────────────────────────────────────────────
    tft.fillRect(0, 0, TFT_W, HDR_H, COL_HEADER);
    tft.setTextSize(1);
    snprintf(buf, sizeof(buf), "BAT %d%%", d.battPct);
    tft.setTextColor(d.battPct < 20 ? MSG_WARN : COL_GREY, COL_HEADER);
    tft.setTextDatum(TL_DATUM);
    tft.drawString(buf, 4, 5);
    tft.setTextColor(COL_WHITE, COL_HEADER);
    tft.setTextDatum(TC_DATUM);
    tft.drawString(d.heliName, TFT_W / 2, 5);
    tft.setTextColor(d.pumpRunning ? MSG_FILLING : COL_DIM, COL_HEADER);
    tft.setTextDatum(TR_DATUM);
    tft.drawString(d.pumpRunning ? "PUMP ON" : "IDLE", TFT_W - 4, 5);

    // ── Panel dividers ─────────────────────────────────────────────
    tft.drawFastVLine(PNL_L_W, PNL_TOP, PNL_BOT - PNL_TOP, COL_HEADER);
    tft.drawFastHLine(0, PNL_BOT, TFT_W, COL_HEADER);

    // ── Left panel: model image ────────────────────────────────────
    drawModelImage(d.heliName);

    // ── Right panel: live data ─────────────────────────────────────
    tft.setTextSize(1);

    // Top row: flow rate
    tft.setTextColor(ARC_MAIN_TK, COL_BG);
    tft.setTextDatum(TL_DATUM);
    tft.drawString("FLW", 110, 21);
    snprintf(buf, sizeof(buf), "%d ml/m", d.flowMlMin);
    tft.setTextColor(COL_GREY, COL_BG);
    tft.setTextDatum(TR_DATUM);
    tft.drawString(buf, 316, 21);

    // Second row: volume / target
    tft.setTextColor(ARC_OUTER_TK, COL_BG);
    tft.setTextDatum(TL_DATUM);
    tft.drawString("VOL", 110, 31);
    if (d.targetMl > 0)
        snprintf(buf, sizeof(buf), "%d/%d ml", d.volumeMl, d.targetMl);
    else
        snprintf(buf, sizeof(buf), "%d ml", d.volumeMl);
    tft.setTextColor(COL_GREY, COL_BG);
    tft.setTextDatum(TR_DATUM);
    tft.drawString(buf, 316, 31);

    // Big main tank percentage — font 4 = 24×32 px per char
    snprintf(buf, sizeof(buf), "%d%%", (int)d.mainTankPct);
    tft.setTextColor(COL_WHITE, COL_BG);
    tft.setTextDatum(MC_DATUM);
    tft.setTextSize(4);
    tft.drawString(buf, PNL_MX, 58);

    tft.setTextSize(1);
    tft.setTextColor(COL_DIM, COL_BG);
    tft.setTextDatum(TC_DATUM);
    tft.drawString("MAIN TANK", PNL_MX, 76);

    // Divider
    tft.drawFastHLine(PNL_L_W + 6, 86, TFT_W - PNL_L_W - 12, COL_HEADER);

    // Supply bar + %
    tft.setTextColor(COL_DIM, COL_BG);
    tft.drawString("SUPPLY", PNL_MX, 91);

    const int BAR_X = PNL_L_W + 6;
    const int BAR_W = TFT_W - PNL_L_W - 12;
    tft.fillRect(BAR_X, 101, BAR_W, 8, 0x0841);
    int barFill = (int)(BAR_W * d.outerTankPct / 100.0f);
    if (barFill > 0) tft.fillRect(BAR_X, 101, barFill, 8, ARC_OUTER_TK);

    snprintf(buf, sizeof(buf), "%d%%", (int)d.outerTankPct);
    tft.setTextColor(ARC_OUTER_TK, COL_BG);
    tft.drawString(buf, PNL_MX, 111);

    // ── Arc gauges ─────────────────────────────────────────────────
    drawArc(ARC_CX, ARC_CY, 90, 14, d.outerTankPct / 100.0f, ARC_OUTER_TK, 0x0200);
    drawArc(ARC_CX, ARC_CY, 73, 12, d.mainTankPct  / 100.0f, ARC_MAIN_TK,  0x0004);
    drawArc(ARC_CX, ARC_CY, 58, 10, d.pumpSpeedPct / 100.0f, ARC_PUMP,     0x2000);
    drawArc(ARC_CX, ARC_CY, 45,  8, d.pressurePct  / 100.0f, ARC_PRESSURE, 0x2002);

    // Arc legend — left side (x<80 is clear of all arcs at these y values)
    static const struct { const char* lbl; uint16_t col; } leg[4] = {
        { "SUPPLY", ARC_OUTER_TK },
        { "TANK",   ARC_MAIN_TK  },
        { "PUMP",   ARC_PUMP     },
        { "PRESS",  ARC_PRESSURE },
    };
    tft.setTextSize(1);
    tft.setTextDatum(TL_DATUM);
    for (int i = 0; i < 4; i++) {
        int ly = PNL_BOT + 8 + i * 14;
        tft.fillRect(2, ly + 2, 6, 6, leg[i].col);
        tft.setTextColor(COL_DIM, COL_BG);
        tft.drawString(leg[i].lbl, 12, ly);
    }

    // ── Status bar (large, coloured) ───────────────────────────────
    tft.fillRect(0, MSG_Y, TFT_W, MSG_H, COL_MSG_BG);
    tft.setTextColor(d.msgColour, COL_MSG_BG);
    tft.setTextDatum(MC_DATUM);
    tft.setTextSize(2);
    tft.drawString(d.message, TFT_W / 2, MSG_Y + MSG_H / 2);

    lastData     = d;
    lastDataInit = true;
}

static void updateGauge(const GaugeData &d)
{
    // Full redraw only when uninitialised or model changes (new photo needed).
    // Pump state change must NOT trigger fillScreen — it blocks for JPEG decode duration,
    // inflating apparent button hold time and triggering spurious back/shutdown.
    if (!lastDataInit || strcmp(d.heliName, lastData.heliName) != 0) {
        drawGaugeFull(d);
        return;
    }

    // Pump state changed — patch header status text only, no fillScreen or JPEG
    if (d.pumpRunning != lastData.pumpRunning) {
        tft.fillRect(TFT_W - 76, 0, 76, HDR_H, COL_HEADER);
        tft.setTextColor(d.pumpRunning ? MSG_FILLING : COL_DIM, COL_HEADER);
        tft.setTextDatum(TR_DATUM);
        tft.setTextSize(1);
        tft.drawString(d.pumpRunning ? "PUMP ON" : "IDLE", TFT_W - 4, 5);
    }

    // Arcs — repaint in-place; drawArc() fills its own background pixels, no clear needed
    bool arcChanged = fabsf(d.outerTankPct - lastData.outerTankPct) > 0.5f ||
                      fabsf(d.mainTankPct  - lastData.mainTankPct)  > 0.5f ||
                      fabsf(d.pumpSpeedPct - lastData.pumpSpeedPct) > 0.5f ||
                      fabsf(d.pressurePct  - lastData.pressurePct)  > 0.5f;
    if (arcChanged) {
        drawArc(ARC_CX, ARC_CY, 90, 14, d.outerTankPct / 100.0f, ARC_OUTER_TK, 0x0200);
        drawArc(ARC_CX, ARC_CY, 73, 12, d.mainTankPct  / 100.0f, ARC_MAIN_TK,  0x0004);
        drawArc(ARC_CX, ARC_CY, 58, 10, d.pumpSpeedPct / 100.0f, ARC_PUMP,     0x2000);
        drawArc(ARC_CX, ARC_CY, 45,  8, d.pressurePct  / 100.0f, ARC_PRESSURE, 0x2002);
    }

    // Status bar
    if (strcmp(d.message, lastData.message) != 0 || d.msgColour != lastData.msgColour) {
        tft.fillRect(0, MSG_Y, TFT_W, MSG_H, COL_MSG_BG);
        tft.setTextColor(d.msgColour, COL_MSG_BG);
        tft.setTextDatum(MC_DATUM);
        tft.setTextSize(2);
        tft.drawString(d.message, TFT_W / 2, MSG_Y + MSG_H / 2);
    }

    // Right panel — update only changed text regions, no full clear
    bool liveChanged = d.flowMlMin != lastData.flowMlMin ||
                       d.volumeMl  != lastData.volumeMl  ||
                       d.targetMl  != lastData.targetMl  ||
                       fabsf(d.mainTankPct  - lastData.mainTankPct)  > 0.5f ||
                       fabsf(d.outerTankPct - lastData.outerTankPct) > 0.5f;
    if (liveChanged) {
        char buf[32];
        const int RX = PNL_L_W + 1;
        const int RW = TFT_W - PNL_L_W - 1;

        tft.fillRect(RX, 20, RW, 10, COL_BG);
        snprintf(buf, sizeof(buf), "%d ml/m", d.flowMlMin);
        tft.setTextColor(COL_GREY, COL_BG);
        tft.setTextDatum(TR_DATUM);
        tft.setTextSize(1);
        tft.drawString(buf, 316, 21);

        tft.fillRect(RX, 30, RW, 10, COL_BG);
        if (d.targetMl > 0)
            snprintf(buf, sizeof(buf), "%d/%d ml", d.volumeMl, d.targetMl);
        else
            snprintf(buf, sizeof(buf), "%d ml", d.volumeMl);
        tft.drawString(buf, 316, 31);

        tft.fillRect(RX, 40, RW, 36, COL_BG);
        snprintf(buf, sizeof(buf), "%d%%", (int)d.mainTankPct);
        tft.setTextColor(COL_WHITE, COL_BG);
        tft.setTextDatum(MC_DATUM);
        tft.setTextSize(4);
        tft.drawString(buf, PNL_MX, 58);

        const int BAR_X = PNL_L_W + 6;
        const int BAR_W = TFT_W - PNL_L_W - 12;
        tft.fillRect(BAR_X, 101, BAR_W, 8, 0x0841);
        int barFill = (int)(BAR_W * d.outerTankPct / 100.0f);
        if (barFill > 0) tft.fillRect(BAR_X, 101, barFill, 8, ARC_OUTER_TK);
        tft.fillRect(RX, 110, RW, 8, COL_BG);
        snprintf(buf, sizeof(buf), "%d%%", (int)d.outerTankPct);
        tft.setTextColor(ARC_OUTER_TK, COL_BG);
        tft.setTextDatum(TC_DATUM);
        tft.setTextSize(1);
        tft.drawString(buf, PNL_MX, 111);
    }

    // Battery (header left region only)
    if (d.battPct != lastData.battPct) {
        tft.fillRect(0, 0, 72, HDR_H, COL_HEADER);
        char buf[12];
        snprintf(buf, sizeof(buf), "BAT %d%%", d.battPct);
        tft.setTextColor(d.battPct < 20 ? MSG_WARN : COL_GREY, COL_HEADER);
        tft.setTextDatum(TL_DATUM);
        tft.setTextSize(1);
        tft.drawString(buf, 4, 5);
    }

    lastData = d;
}

// ─────────────────────────────────────────────────────────────────
// SCREEN 1 — Action selector
//
// 5 large rows: FILL | DRAIN | MODEL | SESSION | NETWORK
// Turn encoder to scroll, press to execute, hold 1.5s = back
// ─────────────────────────────────────────────────────────────────
struct ActionRow { const char* label; const char* sub; uint16_t accent; };

static const ActionRow kActions[ACTION_COUNT] = {
    { "FILL",         "Fill model tank",       ARC_OUTER_TK },
    { "DRAIN",        "Empty model tank",      ARC_PUMP     },
    { "SELECT MODEL", nullptr,                 ARC_MAIN_TK  },
    { "SESSION",      "Flow & volume stats",   0x07FF       },
    { "NETWORK/OTA",  "WiFi & firmware",       COL_GREY     },
};

static void drawActionScreen(const GaugeData &d)
{
    tft.fillScreen(COL_BG);

    // Header
    tft.fillRect(0, 0, TFT_W, HDR_H, COL_HEADER);
    tft.setTextColor(COL_WHITE, COL_HEADER);
    tft.setTextDatum(TC_DATUM);
    tft.setTextSize(1);
    tft.drawString("SELECT ACTION", TFT_W / 2, 5);
    char batBuf[12];
    snprintf(batBuf, sizeof(batBuf), "BAT %d%%", d.battPct);
    tft.setTextColor(d.battPct < 20 ? MSG_WARN : COL_GREY, COL_HEADER);
    tft.setTextDatum(TL_DATUM);
    tft.drawString(batBuf, 4, 5);

    const int ROW_H  = 38;
    const int START_Y = HDR_H + 2;

    for (int i = 0; i < ACTION_COUNT; i++) {
        int y   = START_Y + i * ROW_H;
        bool sel = (i == actionSel);
        uint16_t bg = sel ? 0x2104 : COL_BG;

        tft.fillRect(0,  y, TFT_W, ROW_H - 1, bg);
        tft.fillRect(0,  y, 5,     ROW_H - 1, kActions[i].accent);

        // Main label — font size 2 for outdoor readability
        tft.setTextSize(2);
        tft.setTextColor(sel ? COL_WHITE : COL_GREY, bg);
        tft.setTextDatum(TL_DATUM);
        tft.drawString(kActions[i].label, 12, y + 4);

        // Sub-label — "MODEL" row shows current model name
        const char* sub = (i == ACTION_MODEL) ? d.heliName : kActions[i].sub;
        if (sub && *sub) {
            tft.setTextSize(1);
            tft.setTextColor(sel ? COL_GREY : COL_DIM, bg);
            tft.drawString(sub, 14, y + 26);
        }

        // Selection arrow
        if (sel) {
            tft.setTextSize(2);
            tft.setTextColor(kActions[i].accent, bg);
            tft.setTextDatum(TR_DATUM);
            tft.drawString(">", TFT_W - 6, y + 8);
        }
    }

    // Footer hint
    tft.fillRect(0, 222, TFT_W, 18, COL_MSG_BG);
    tft.setTextColor(COL_DIM, COL_MSG_BG);
    tft.setTextDatum(TC_DATUM);
    tft.setTextSize(1);
    tft.drawString("TURN=scroll   PRESS=select   HOLD=back", TFT_W / 2, 226);
}

// ─────────────────────────────────────────────────────────────────
// SCREEN 2 — Model browser
// ─────────────────────────────────────────────────────────────────
static void drawModelScreen()
{
    tft.fillScreen(COL_BG);
    tft.fillRect(0, 0, TFT_W, HDR_H, COL_HEADER);
    tft.setTextColor(COL_WHITE, COL_HEADER);
    tft.setTextDatum(TC_DATUM);
    tft.setTextSize(1);
    tft.drawString("SELECT MODEL", TFT_W / 2, 5);

    const int ROW_H   = 40;
    const int VISIBLE = 5;
    int startIdx = constrain(heliScrollIdx - 2, 0, max(0, numModels - VISIBLE));

    for (int i = 0; i < VISIBLE && (startIdx + i) < numModels; i++) {
        int idx  = startIdx + i;
        int y    = HDR_H + 2 + i * ROW_H;
        bool sel = (idx == heliScrollIdx);
        uint16_t bg   = sel ? 0x0820 : COL_BG;
        uint16_t bord = sel ? ARC_OUTER_TK : COL_HEADER;

        tft.fillRect(2, y, TFT_W - 4, ROW_H - 2, bg);
        tft.drawRect(2, y, TFT_W - 4, ROW_H - 2, bord);

        // Model name — size 2 for visibility
        tft.setTextSize(2);
        tft.setTextColor(sel ? COL_WHITE : COL_GREY, bg);
        tft.setTextDatum(TL_DATUM);
        tft.drawString(heliModels[idx].name, 8, y + 4);

        // Details line
        char sub[40];
        snprintf(sub, sizeof(sub), "%d ml   Fills: %u",
                 heliModels[idx].tankVolumeMl,
                 heliModels[idx].totalFills);
        tft.setTextSize(1);
        tft.setTextColor(sel ? COL_GREY : COL_DIM, bg);
        tft.drawString(sub, 10, y + 27);

        // Active indicator
        if (idx == activeModelIndex) {
            tft.setTextColor(ARC_OUTER_TK, bg);
            tft.setTextDatum(TR_DATUM);
            tft.setTextSize(1);
            tft.drawString("ACTIVE", TFT_W - 8, y + 4);
        }
    }

    // Footer
    tft.fillRect(0, 222, TFT_W, 18, COL_MSG_BG);
    tft.setTextColor(COL_DIM, COL_MSG_BG);
    tft.setTextDatum(TC_DATUM);
    tft.setTextSize(1);
    tft.drawString("TURN=scroll   PRESS=select   HOLD=back", TFT_W / 2, 226);
}

// ─────────────────────────────────────────────────────────────────
// SCREEN 3 — Session stats
//
// Static chrome (labels, dividers, header, footer) drawn once on
// full-draw.  Values refreshed in-place (no fillScreen) to avoid flash.
// ─────────────────────────────────────────────────────────────────

#define SES_ROW_H   44
#define SES_START_Y (HDR_H + 2)
#define SES_ROWS     4

static const struct { const char* lbl; uint16_t col; } kSession[SES_ROWS] = {
    { "Dispensed", ARC_MAIN_TK },
    { "Flow rate", ARC_PUMP    },
    { "Target",    0xFFE0      },  // yellow — more visible than grey
    { "Supply",    ARC_OUTER_TK},
};

static void sessionValues(const GaugeData &d)
{
    char buf[24];
    const char* fmts[SES_ROWS] = { "%d ml", "%d ml/m", "%d ml", "%d%%" };
    int         vals[SES_ROWS] = { d.volumeMl, d.flowMlMin, d.targetMl, (int)d.outerTankPct };

    tft.setTextDatum(TR_DATUM);
    tft.setTextSize(2);
    for (int i = 0; i < SES_ROWS; i++) {
        int y = SES_START_Y + i * SES_ROW_H;
        snprintf(buf, sizeof(buf), fmts[i], vals[i]);
        // Erase only the right half of the row then redraw value
        tft.fillRect(TFT_W / 2, y + 2, TFT_W / 2, SES_ROW_H - 6, COL_BG);
        tft.setTextColor(COL_WHITE, COL_BG);
        tft.drawString(buf, TFT_W - 8, y + 14);
    }

    // Supply bar
    const int bary = SES_START_Y + SES_ROWS * SES_ROW_H + 2;
    const int BX = 8, BW = TFT_W - 16;
    tft.fillRect(BX, bary, BW, 10, 0x0841);
    int fill = (int)(BW * d.outerTankPct / 100.0f);
    if (fill > 0) tft.fillRect(BX, bary, fill, 10, ARC_OUTER_TK);
}

static void drawSessionScreen(const GaugeData &d, bool full = true)
{
    if (full) {
        tft.fillScreen(COL_BG);
        tft.fillRect(0, 0, TFT_W, HDR_H, COL_HEADER);
        tft.setTextColor(COL_WHITE, COL_HEADER);
        tft.setTextDatum(TC_DATUM);
        tft.setTextSize(1);
        tft.drawString("SESSION STATS", TFT_W / 2, 5);

        tft.setTextDatum(TL_DATUM);
        tft.setTextSize(2);
        for (int i = 0; i < SES_ROWS; i++) {
            int y = SES_START_Y + i * SES_ROW_H;
            tft.setTextColor(kSession[i].col, COL_BG);
            tft.drawString(kSession[i].lbl, 8, y + 2);
            tft.drawFastHLine(0, y + SES_ROW_H - 1, TFT_W, COL_HEADER);
        }

        tft.fillRect(0, 222, TFT_W, 18, COL_MSG_BG);
        tft.setTextColor(COL_DIM, COL_MSG_BG);
        tft.setTextDatum(TC_DATUM);
        tft.setTextSize(1);
        tft.drawString("HOLD = back", TFT_W / 2, 226);
    }

    sessionValues(d);
}

// ─────────────────────────────────────────────────────────────────
// SCREEN 4 — Network / OTA
//
// Layout (top → bottom):
//   ACCESS POINT section  — name, AP IP
//   divider
//   WIFI section          — station IP or "not connected"
//   browser hint
//   footer
// ─────────────────────────────────────────────────────────────────
static void drawNetScreen()
{
    tft.fillScreen(COL_BG);
    tft.fillRect(0, 0, TFT_W, HDR_H, COL_HEADER);
    tft.setTextColor(COL_WHITE, COL_HEADER);
    tft.setTextDatum(TC_DATUM);
    tft.setTextSize(1);
    tft.drawString("NETWORK / OTA", TFT_W / 2, 5);

    int y = HDR_H + 10;
    tft.setTextDatum(TC_DATUM);

    // ── ACCESS POINT section ──────────────────────────────────────
    tft.setTextColor(COL_GREY, COL_BG);
    tft.setTextSize(1);
    tft.drawString("ACCESS POINT", TFT_W / 2, y);  y += 14;

    // AP name — large, cyan, most important piece of info
    tft.setTextColor(0x07FF, COL_BG);
    tft.setTextSize(2);
    tft.drawString("fuelstation.local", TFT_W / 2, y);  y += 24;

    // AP IP
    tft.setTextColor(COL_WHITE, COL_BG);
    tft.setTextSize(2);
    tft.drawString(gNetApIP[0] ? gNetApIP : "---", TFT_W / 2, y);  y += 26;

    // ── Divider ───────────────────────────────────────────────────
    tft.drawFastHLine(16, y, TFT_W - 32, COL_HEADER);  y += 10;

    // ── WiFi / Station section ────────────────────────────────────
    tft.setTextColor(COL_GREY, COL_BG);
    tft.setTextSize(1);
    tft.drawString("WIFI", TFT_W / 2, y);  y += 14;

    bool connected = strlen(gNetStaIP) > 0 && strcmp(gNetStaIP, "0.0.0.0") != 0;
    if (connected) {
        tft.setTextColor(ARC_OUTER_TK, COL_BG);
        tft.setTextSize(2);
        tft.drawString(gNetStaIP, TFT_W / 2, y);  y += 24;
    } else {
        tft.setTextColor(MSG_WARN, COL_BG);
        tft.setTextSize(2);
        tft.drawString("Not connected", TFT_W / 2, y);  y += 24;
    }

    // ── Browser hint ──────────────────────────────────────────────
    y += 8;
    tft.setTextColor(COL_GREY, COL_BG);
    tft.setTextSize(1);
    tft.drawString("Open in browser to update firmware", TFT_W / 2, y);

    tft.fillRect(0, 222, TFT_W, 18, COL_MSG_BG);
    tft.setTextColor(COL_DIM, COL_MSG_BG);
    tft.setTextSize(1);
    tft.drawString("HOLD = back", TFT_W / 2, 226);
}

// ─────────────────────────────────────────────────────────────────
// Public update — called at 10 Hz from main loop
// ─────────────────────────────────────────────────────────────────
void Display_Update(const GaugeData &data)
{
    if (needsFullRedraw) {
        needsFullRedraw = false;
        switch (currentScreen) {
            case SCREEN_GAUGE:   drawGaugeFull(data);    break;
            case SCREEN_ACTION:  drawActionScreen(data); break;
            case SCREEN_HELI:    drawModelScreen();      break;
            case SCREEN_SESSION: drawSessionScreen(data);break;
            case SCREEN_NET:     drawNetScreen();        break;
            default: break;
        }
        return;
    }
    if (currentScreen == SCREEN_GAUGE)   updateGauge(data);
    if (currentScreen == SCREEN_SESSION) {
        static int   lastVol = -1, lastFlow = -1, lastTarget = -1;
        static float lastSupply = -1.0f;
        if (data.volumeMl   != lastVol    ||
            data.flowMlMin  != lastFlow   ||
            data.targetMl   != lastTarget ||
            fabsf(data.outerTankPct - lastSupply) > 0.5f) {
            lastVol    = data.volumeMl;
            lastFlow   = data.flowMlMin;
            lastTarget = data.targetMl;
            lastSupply = data.outerTankPct;
            drawSessionScreen(data, false);
        }
    }
}

// ─────────────────────────────────────────────────────────────────
// Encoder
// ─────────────────────────────────────────────────────────────────
void Display_EncoderScroll(int delta)
{
    if (currentScreen == SCREEN_ACTION) {
        actionSel = constrain(actionSel + delta, 0, ACTION_COUNT - 1);
        needsFullRedraw = true;
    } else if (currentScreen == SCREEN_HELI && numModels > 0) {
        heliScrollIdx = constrain(heliScrollIdx + delta, 0, numModels - 1);
        needsFullRedraw = true;
    } else if (currentScreen == SCREEN_GAUGE) {
        if (delta != 0) Display_SetScreen(SCREEN_ACTION);
    }
}

void Display_EncoderPress()
{
    if (currentScreen == SCREEN_HELI) {
        activeModelIndex = heliScrollIdx;
        Display_SetScreen(SCREEN_GAUGE);
    }
}
