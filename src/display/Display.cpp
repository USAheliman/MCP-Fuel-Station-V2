#include "Display.h"
#include "../../include/pins.h"
#include "../heli/HeliLib.h"
#include <LittleFS.h>
#include <TJpg_Decoder.h>
#include <math.h>

static TFT_eSPI    tft;
static TFT_eSprite spr(&tft);

// ════════════════════════════════════════════════════════════════
// Photo cache — same /models/<name>/thumb.jpg the web page uses
// Scale 2 decodes a 320×240 image to exactly 160×120
// ════════════════════════════════════════════════════════════════
#define PHOTO_W   160
#define PHOTO_H   120
#define PHOTO_X    20    // centred in 200px left panel: (200-160)/2
#define PHOTO_Y    52

static uint16_t* photoBuf      = nullptr;
static char      photoModel[24] = "";
static bool      photoHasImage  = false;
static bool      gCapturing     = false;

// ════════════════════════════════════════════════════════════════
// Screen state
// ════════════════════════════════════════════════════════════════
static DisplayScreen currentScreen   = SCREEN_GAUGE;
static bool          needsFullRedraw = true;
static int           heliScrollIdx   = 0;
static int           actionSel       = 0;
static char          gNetStaIP[20]   = "";
static char          gNetApIP[20]    = "";
static uint32_t      lastScreenChgMs = 0;

// ════════════════════════════════════════════════════════════════
// Animated bar values
// ════════════════════════════════════════════════════════════════
static float animSupply   = 0.0f;
static float animMain     = 0.0f;
static float animBatt     = 0.0f;
static float animPressure = 0.0f;
static float animPump     = 0.0f;

static int slideOffset = 0;

// ════════════════════════════════════════════════════════════════
// Layout — 480×320 landscape
//
//  y=  0.. 43   Header   (44 px)
//  y= 44..281   Content  (238 px)
//    x=  0..199   Left panel — dark card, photo
//    x=200..479   Right panel — data bars
//  y=282..319   Status bar  (38 px)
//
//  Right panel sections:
//    MN_Y  = 50   Model tank  : label+% line, bar at +20 h=16 → ends 86
//    SUP_Y = 90   Supply tank : same structure             → ends 126
//    separator at 130
//    CTX_HEAD=134  Context header (size 2)
//    CTX_R1=154   Row 1: label+val, bar at +20 h=14       → ends 188
//    CTX_R2=196   Row 2                                   → ends 230
//    CTX_R3=238   Row 3                                   → ends 272
//    SB_Y=282     Status bar (10px clearance)
// ════════════════════════════════════════════════════════════════
#define HDR_H      44
#define SB_Y      282
#define SB_H       38

#define LP_W      200
#define RP_X      208
#define RP_W      264

#define MN_Y       50
#define SUP_Y      90
#define CTX_HEAD  134
#define CTX_R1    154
#define CTX_R2    196
#define CTX_R3    238
#define CTX_BAR_H  14

// ════════════════════════════════════════════════════════════════
// Colour helpers
// ════════════════════════════════════════════════════════════════
static inline uint16_t tankColor(float p) {
    return (p >= 60.0f) ? COL_GREEN : (p >= 30.0f) ? COL_ORANGE : COL_RED;
}
static inline uint16_t battColor(float p) {
    return (p >= 50.0f) ? COL_GREEN : (p >= 20.0f) ? COL_YELLOW : COL_RED;
}
static inline uint16_t dimColor(uint16_t c, float f) {
    uint8_t r = (uint8_t)(((c >> 11) & 0x1F) * f);
    uint8_t g = (uint8_t)(((c >>  5) & 0x3F) * f);
    uint8_t b = (uint8_t)(( c        & 0x1F) * f);
    return ((uint16_t)r << 11) | ((uint16_t)g << 5) | b;
}

// ════════════════════════════════════════════════════════════════
// drawPillBar
// ════════════════════════════════════════════════════════════════
static void drawPillBar(int x, int y, int w, int h, float pct,
                        uint16_t fillCol, uint16_t bgCol,
                        float targetFrac = -1.0f)
{
    int r = h / 2;
    spr.fillRoundRect(x, y, w, h, r, bgCol);
    int fw = (int)(w * constrain(pct / 100.0f, 0.0f, 1.0f) + 0.5f);
    if (fw >= h)
        spr.fillRoundRect(x, y, fw, h, r, fillCol);
    else if (fw > 0)
        spr.fillCircle(x + r, y + r, r, fillCol);
    if (targetFrac >= 0.0f) {
        int tx = x + (int)(w * constrain(targetFrac, 0.0f, 1.0f) + 0.5f);
        spr.fillRect(max(x, tx - 1), y - 2, 3, h + 4, COL_YELLOW);
    }
}

// ════════════════════════════════════════════════════════════════
// drawCtxBar — label + value on one line (size 2), pill bar below
// ════════════════════════════════════════════════════════════════
static void drawCtxBar(int rowY, const char* label, float pct,
                       uint16_t col, const char* val,
                       float targetFrac = -1.0f)
{
    spr.setTextSize(2);
    spr.setTextDatum(TL_DATUM);
    spr.setTextColor(col, COL_BG);
    spr.drawString(label, RP_X, rowY);
    spr.setTextDatum(TR_DATUM);
    spr.setTextColor(COL_WHITE, COL_BG);
    spr.drawString(val, RP_X + RP_W, rowY);
    drawPillBar(RP_X, rowY + 20, RP_W, CTX_BAR_H, pct, col, COL_TRACK, targetFrac);
}

// ════════════════════════════════════════════════════════════════
// TJpgDec callback — capture JPEG tiles to photoBuf
// setSwapBytes(true) ensures pixels are in the same uint16_t format
// as the sprite buffer, so direct memcpy into sprite works.
// ════════════════════════════════════════════════════════════════
static bool jpgOutput(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bmp)
{
    if (gCapturing && photoBuf) {
        for (int row = 0; row < (int)h; row++) {
            int dy = y + row;
            if (dy < 0 || dy >= PHOTO_H) continue;
            for (int col = 0; col < (int)w; col++) {
                int dx = x + col;
                if (dx >= 0 && dx < PHOTO_W)
                    photoBuf[dy * PHOTO_W + dx] = bmp[row * w + col];
            }
        }
        return 1;
    }
    if (y >= TFT_H) return 0;
    tft.pushImage(x, y, w, h, bmp);
    yield();
    return 1;
}

static void loadPhoto(const char* name)
{
    photoHasImage = false;
    if (!photoBuf) {
        Serial.println("Display: photoBuf is null, skipping photo load");
        strncpy(photoModel, name, sizeof(photoModel) - 1);
        return;
    }
    memset(photoBuf, 0, PHOTO_W * PHOTO_H * sizeof(uint16_t));

    char path[64];
    snprintf(path, sizeof(path), "/models/%s/thumb.jpg", name);
    Serial.printf("Display: loadPhoto '%s' -> %s  exists=%d\n",
                  name, path, (int)LittleFS.exists(path));

    if (LittleFS.exists(path)) {
        gCapturing = true;
        TJpgDec.setJpgScale(2);
        JRESULT res = TJpgDec.drawFsJpg(0, 0, path, LittleFS);
        gCapturing    = false;
        photoHasImage = (res == 0);
        Serial.printf("Display: decode result=%d  photoHasImage=%d\n", res, (int)photoHasImage);
    }

    strncpy(photoModel, name, sizeof(photoModel) - 1);
    photoModel[sizeof(photoModel) - 1] = '\0';
}

// ════════════════════════════════════════════════════════════════
// embedPhoto — blit photoBuf directly into sprite pixel buffer
// Called inside renderGauge, before spr.pushSprite.
// setSwapBytes(true): TJpgDec produces pixels in the same uint16_t
// format the sprite/SPI chain expects — direct memcpy is correct.
// ════════════════════════════════════════════════════════════════
static void embedPhoto()
{
    if (!photoHasImage || !photoBuf) return;
    // photoBuf holds big-endian bytes (TJpgDec.setSwapBytes(true)).
    // Sprite stores big-endian internally, so pushImage with _swapBytes=false copies as-is.
    spr.pushImage(PHOTO_X, PHOTO_Y, PHOTO_W, PHOTO_H, photoBuf);
}

// ════════════════════════════════════════════════════════════════
// Public API
// ════════════════════════════════════════════════════════════════
void Display_SetBrightness(uint8_t val) { ledcWrite(TFT_BL_LEDC_CHANNEL, val); }

void Display_SetNetworkIP(const char* staIP, const char* apIP)
{
    strncpy(gNetStaIP, staIP ? staIP : "", sizeof(gNetStaIP) - 1);
    strncpy(gNetApIP,  apIP  ? apIP  : "", sizeof(gNetApIP)  - 1);
}

DisplayScreen Display_CurrentScreen()     { return currentScreen; }
int           Display_SelectedHeliIndex() { return heliScrollIdx; }
int           Display_GetActionSel()      { return actionSel; }
uint32_t      Display_GetScreenAge()      { return millis() - lastScreenChgMs; }

void Display_SetScreen(DisplayScreen s)
{
    if (s == currentScreen) return;
    currentScreen   = s;
    needsFullRedraw = true;
    slideOffset     = TFT_W;
    lastScreenChgMs = millis();
    if (s == SCREEN_HELI)   heliScrollIdx = activeModelIndex;
    if (s == SCREEN_ACTION) actionSel     = 0;
}

void Display_Init()
{
    ledcSetup(TFT_BL_LEDC_CHANNEL, 5000, 8);
    ledcAttachPin(PIN_TFT_BL, TFT_BL_LEDC_CHANNEL);
    Display_SetBrightness(200);

    tft.init();
    tft.invertDisplay(true);
    tft.setRotation(1);
    tft.fillScreen(COL_BG);

    spr.createSprite(TFT_W, TFT_H);

    photoBuf = (uint16_t*)ps_malloc(PHOTO_W * PHOTO_H * sizeof(uint16_t));
    if (!photoBuf) photoBuf = (uint16_t*)malloc(PHOTO_W * PHOTO_H * sizeof(uint16_t));
    Serial.printf("Display: photoBuf=%p  (%d bytes)\n",
                  photoBuf, PHOTO_W * PHOTO_H * (int)sizeof(uint16_t));

    TJpgDec.setJpgScale(2);
    TJpgDec.setSwapBytes(true);   // outputs big-endian bytes matching sprite's internal format
    TJpgDec.setCallback(jpgOutput);

    lastScreenChgMs = millis();
    Serial.println("Display: init OK");
}

// ════════════════════════════════════════════════════════════════
// SCREEN 0 — Model / Gauge
// ════════════════════════════════════════════════════════════════
static void renderGauge(const GaugeData &d)
{
    spr.fillScreen(COL_BG);
    char buf[40];

    // ── Header ───────────────────────────────────────────────────
    spr.fillRect(0, 0, TFT_W, HDR_H, COL_PANEL);
    spr.drawFastHLine(0, HDR_H - 1, TFT_W, COL_GREY);

    snprintf(buf, sizeof(buf), "BATT %d%%", d.battPct);
    spr.setTextDatum(TL_DATUM);
    spr.setTextColor(battColor((float)d.battPct), COL_PANEL);
    spr.setTextSize(1);
    spr.drawString(buf, 8, 8);

    spr.setTextDatum(TC_DATUM);
    spr.setTextColor(COL_WHITE, COL_PANEL);
    spr.setTextSize(2);
    spr.drawString("MCP Fuel Station", TFT_W / 2, 10);

    spr.setTextDatum(TR_DATUM);
    spr.setTextSize(1);
    spr.setTextColor(d.pumpRunning ? MSG_FILLING : COL_GREEN, COL_PANEL);
    spr.drawString(d.pumpRunning ? "FILLING" : "IDLE", TFT_W - 6, 8);
    spr.setTextColor(COL_GREEN, COL_PANEL);
    spr.drawString(FW_VERSION, TFT_W - 6, 26);

    // ── Left panel ───────────────────────────────────────────────
    spr.fillRect(0, HDR_H, LP_W, SB_Y - HDR_H, COL_BG);
    spr.drawFastVLine(LP_W, HDR_H, SB_Y - HDR_H, COL_GREY);

    // Placeholder (only drawn when no image loaded)
    if (!photoHasImage) {
        const uint16_t ac[] = { COL_BLUE, COL_ORANGE, COL_CYAN, COL_PURPLE, COL_GREEN };
        uint16_t acc = ac[d.heliName[0] ? (uint8_t)d.heliName[0] % 5 : 0];
        spr.fillRoundRect(PHOTO_X, PHOTO_Y, PHOTO_W, PHOTO_H, 8, dimColor(acc, 0.25f));
        spr.drawRoundRect(PHOTO_X, PHOTO_Y, PHOTO_W, PHOTO_H, 8, acc);
        char ini[2] = { d.heliName[0] ? (char)toupper((unsigned char)d.heliName[0]) : '?', 0 };
        spr.setTextDatum(MC_DATUM);
        spr.setTextColor(acc, dimColor(acc, 0.25f));
        spr.setTextSize(6);
        spr.drawString(ini, PHOTO_X + PHOTO_W / 2, PHOTO_Y + PHOTO_H / 2);
    } else {
        // Photo pixels written directly into sprite buffer — no post-push needed
        embedPhoto();
    }

    // Sub-info below photo
    {
        const int infoY = PHOTO_Y + PHOTO_H + 6;   // y=178
        spr.setTextDatum(TC_DATUM);
        spr.setTextSize(2);

        spr.setTextColor(COL_WHITE, COL_BG);
        spr.drawString(d.heliName, LP_W / 2, infoY);

        if (activeModelIndex >= 0) {
            snprintf(buf, sizeof(buf), "%d ml", heliModels[activeModelIndex].tankVolumeMl);
            spr.setTextColor(COL_YELLOW, COL_BG);
            spr.drawString(buf, LP_W / 2, infoY + 22);

            bool sns = heliModels[activeModelIndex].hasTankSensor;
            spr.setTextColor(sns ? COL_GREEN : COL_ORANGE, COL_BG);
            spr.drawString(sns ? "SENSOR: YES" : "SENSOR: NO", LP_W / 2, infoY + 44);
        }
    }

    // ── Right panel — MODEL TANK (first) ─────────────────────────
    spr.setTextSize(2);
    spr.setTextDatum(TL_DATUM);
    spr.setTextColor(COL_CYAN, COL_BG);
    spr.drawString("MODEL TANK", RP_X, MN_Y);

    spr.setTextDatum(TR_DATUM);
    spr.setTextColor(COL_WHITE, COL_BG);
    if (d.pumpRunning && d.volumeMl > 0 && activeModelIndex >= 0 &&
        heliModels[activeModelIndex].tankVolumeMl > 0) {
        snprintf(buf, sizeof(buf), "%d / %dml",
                 d.volumeMl,
                 heliModels[activeModelIndex].tankVolumeMl);
    } else if (activeModelIndex >= 0 && heliModels[activeModelIndex].tankVolumeMl > 0) {
        snprintf(buf, sizeof(buf), "%dml",
                 heliModels[activeModelIndex].tankVolumeMl);
    } else {
        snprintf(buf, sizeof(buf), "%d%%", (int)(animMain + 0.5f));
    }
    spr.drawString(buf, RP_X + RP_W, MN_Y);

    float targetFrac = -1.0f;
    if (d.targetMl > 0 && activeModelIndex >= 0 &&
        heliModels[activeModelIndex].tankVolumeMl > 0)
        targetFrac = (float)d.targetMl / heliModels[activeModelIndex].tankVolumeMl;
    drawPillBar(RP_X, MN_Y + 20, RP_W, 16, animMain, COL_BLUE, COL_TRACK, targetFrac);

    spr.drawFastHLine(RP_X, SUP_Y - 4, RP_W, COL_PANEL);

    // ── Right panel — SUPPLY TANK (second) ───────────────────────
    spr.setTextSize(2);
    spr.setTextDatum(TL_DATUM);
    spr.setTextColor(COL_CYAN, COL_BG);
    spr.drawString("SUPPLY TANK", RP_X, SUP_Y);

    spr.setTextDatum(TR_DATUM);
    spr.setTextColor(tankColor(animSupply), COL_BG);
    if (d.supplyCapMl > 0)
        snprintf(buf, sizeof(buf), "%.1fL", d.supplyMl / 1000.0f);
    else
        snprintf(buf, sizeof(buf), "%d%%", (int)(animSupply + 0.5f));
    spr.drawString(buf, RP_X + RP_W, SUP_Y);

    {
        float lowFrac = (d.supplyCapMl > 0)
                        ? (float)d.supplyLowMl / d.supplyCapMl
                        : -1.0f;
        drawPillBar(RP_X, SUP_Y + 20, RP_W, 16, animSupply,
                    tankColor(animSupply), COL_TRACK, lowFrac);
    }

    // ── Context section ───────────────────────────────────────────
    spr.drawFastHLine(RP_X, CTX_HEAD - 4, RP_W, COL_PANEL);

    if (d.pumpRunning) {
        spr.setTextSize(2);
        spr.setTextDatum(TL_DATUM);
        spr.setTextColor(MSG_FILLING, COL_BG);
        spr.drawString("PUMP ACTIVE", RP_X, CTX_HEAD);

        snprintf(buf, sizeof(buf), "%d%%", (int)(d.pumpSpeedPct + 0.5f));
        drawCtxBar(CTX_R1, "PUMP SPEED", d.pumpSpeedPct, COL_ORANGE, buf);

        float flowPct    = constrain((float)d.flowMlMin / 2000.0f * 100.0f, 0.0f, 100.0f);
        float flowTarget = (activeModelIndex >= 0 && heliModels[activeModelIndex].fillSpeed > 0)
                           ? (float)heliModels[activeModelIndex].fillSpeed / 2000.0f
                           : -1.0f;
        snprintf(buf, sizeof(buf), "%d ml/m", d.flowMlMin);
        drawCtxBar(CTX_R2, "FLOW RATE", flowPct, COL_CYAN, buf, flowTarget);

    } else {

        snprintf(buf, sizeof(buf), "%d%%", d.battPct);
        drawCtxBar(CTX_R1, "BATTERY", animBatt, battColor(animBatt), buf);

        if (d.sensorFitted) {
            snprintf(buf, sizeof(buf), "%d%%", (int)(animPressure + 0.5f));
            drawCtxBar(CTX_R2, "PRESSURE", animPressure, COL_PURPLE, buf);
        } else {
            spr.setTextSize(2);
            spr.setTextDatum(TL_DATUM);
            spr.setTextColor(COL_GREY, COL_BG);
            spr.drawString("PRESSURE", RP_X, CTX_R2);
            spr.setTextSize(1);
            spr.setTextColor(COL_GREY, COL_BG);
            spr.drawString("sensor not fitted", RP_X, CTX_R2 + 20);
        }
    }

    // ── Status bar ───────────────────────────────────────────────
    uint16_t sbBg = d.pumpRunning
        ? dimColor(d.msgColour, 0.55f + 0.45f * sinf(millis() * 0.004f))
        : COL_PANEL;
    spr.fillRect(0, SB_Y, TFT_W, SB_H, sbBg);
    spr.drawFastHLine(0, SB_Y, TFT_W, COL_GREY);
    spr.setTextDatum(MC_DATUM);
    spr.setTextColor(d.pumpRunning ? COL_WHITE : d.msgColour, sbBg);
    spr.setTextSize(2);
    spr.drawString(d.message, TFT_W / 2, SB_Y + SB_H / 2);
}

// ════════════════════════════════════════════════════════════════
// SCREEN 1 — Action selector
// ════════════════════════════════════════════════════════════════
struct ActionRow { const char* label; const char* sub; uint16_t accent; };
static const ActionRow kActions[ACTION_COUNT] = {
    { "FILL",          "Fill model tank",        MSG_FILLING },
    { "DRAIN",         "Empty model tank",       COL_ORANGE  },
    { "SELECT MODEL",  nullptr,                  COL_BLUE    },
    { "SESSION",       "Flow & volume stats",    COL_CYAN    },
    { "NETWORK / OTA", "WiFi & firmware update", COL_GREY    },
};

static void renderAction(const GaugeData &d)
{
    spr.fillScreen(COL_BG);
    spr.fillRect(0, 0, TFT_W, 32, COL_HEADER);
    spr.setTextDatum(TC_DATUM);
    spr.setTextColor(COL_WHITE, COL_HEADER);
    spr.setTextSize(2);
    spr.drawString("SELECT ACTION", TFT_W / 2, 8);

    const int ROW_H   = 48;
    const int START_Y = 32;

    for (int i = 0; i < ACTION_COUNT; i++) {
        int y    = START_Y + i * ROW_H;
        bool sel = (i == actionSel);
        uint16_t bg = sel ? 0x2104 : COL_BG;

        spr.fillRect(0, y, TFT_W,   ROW_H - 1, bg);
        spr.fillRect(0, y, 6,       ROW_H - 1, kActions[i].accent);
        if (sel) spr.drawRect(6, y, TFT_W - 6, ROW_H - 1, kActions[i].accent);

        spr.setTextSize(2);
        spr.setTextColor(sel ? COL_WHITE : COL_GREY, bg);
        spr.setTextDatum(TL_DATUM);
        spr.drawString(kActions[i].label, 14, y + 8);

        const char* sub = (i == ACTION_MODEL) ? d.heliName : kActions[i].sub;
        if (sub && *sub) {
            spr.setTextSize(1);
            spr.setTextColor(sel ? kActions[i].accent : COL_GREY, bg);
            spr.drawString(sub, 16, y + 34);
        }

        if (sel) {
            spr.setTextSize(2);
            spr.setTextColor(kActions[i].accent, bg);
            spr.setTextDatum(TR_DATUM);
            spr.drawString(">", TFT_W - 8, y + 8);
        }
    }

    spr.fillRect(0, SB_Y, TFT_W, SB_H, COL_PANEL);
    spr.drawFastHLine(0, SB_Y, TFT_W, COL_GREY);
    spr.setTextDatum(MC_DATUM);
    spr.setTextColor(COL_GREY, COL_PANEL);
    spr.setTextSize(1);
    spr.drawString("TURN = scroll     PRESS = select     HOLD = back", TFT_W / 2, SB_Y + SB_H / 2);
}

// ════════════════════════════════════════════════════════════════
// SCREEN 2 — Model browser  (4 rows × 62px)
// ════════════════════════════════════════════════════════════════
static void renderModel()
{
    spr.fillScreen(COL_BG);
    spr.fillRect(0, 0, TFT_W, 32, COL_HEADER);
    spr.setTextDatum(TC_DATUM);
    spr.setTextColor(COL_WHITE, COL_HEADER);
    spr.setTextSize(2);
    spr.drawString("SELECT MODEL", TFT_W / 2, 8);

    const int ROW_H   = 62;
    const int VISIBLE = 4;
    int start = constrain(heliScrollIdx - 1, 0, max(0, numModels - VISIBLE));

    for (int i = 0; i < VISIBLE && (start + i) < numModels; i++) {
        int  idx  = start + i;
        int  y    = 32 + i * ROW_H;
        bool sel  = (idx == heliScrollIdx);
        uint16_t bg  = sel ? 0x0820 : COL_BG;
        uint16_t brd = sel ? MSG_FILLING : COL_PANEL;

        spr.fillRect(2, y, TFT_W - 4, ROW_H - 2, bg);
        spr.drawRect(2, y, TFT_W - 4, ROW_H - 2, brd);

        spr.setTextSize(2);
        spr.setTextColor(sel ? COL_WHITE : COL_GREY, bg);
        spr.setTextDatum(TL_DATUM);
        spr.drawString(heliModels[idx].name, 10, y + 4);

        if (idx == activeModelIndex) {
            spr.setTextDatum(TR_DATUM);
            spr.setTextColor(MSG_FILLING, bg);
            spr.setTextSize(1);
            spr.drawString("ACTIVE", TFT_W - 8, y + 8);
        }

        // Line 1: key parameters
        char line1[64];
        snprintf(line1, sizeof(line1), "%dml  F:%d/m  D:%d/m  P:%ds  SNS:%s",
                 heliModels[idx].tankVolumeMl,
                 heliModels[idx].fillSpeed,
                 heliModels[idx].drainSpeed,
                 heliModels[idx].purgeSecs,
                 heliModels[idx].hasTankSensor ? "Y" : "N");
        spr.setTextSize(1);
        spr.setTextColor(sel ? COL_WHITE : COL_CYAN, bg);
        spr.setTextDatum(TL_DATUM);
        spr.drawString(line1, 10, y + 26);

        // Line 2: lifetime stats
        char line2[64];
        snprintf(line2, sizeof(line2), "Fills:%u  Drains:%u  Total:%.1fL",
                 heliModels[idx].totalFills,
                 heliModels[idx].totalDrains,
                 heliModels[idx].totalFillMl / 1000.0f);
        spr.setTextColor(sel ? COL_CYAN : COL_GREY, bg);
        spr.drawString(line2, 10, y + 38);
    }

    spr.fillRect(0, SB_Y, TFT_W, SB_H, COL_PANEL);
    spr.drawFastHLine(0, SB_Y, TFT_W, COL_GREY);
    spr.setTextDatum(MC_DATUM);
    spr.setTextColor(COL_GREY, COL_PANEL);
    spr.setTextSize(1);
    spr.drawString("TURN = scroll     PRESS = select     HOLD = back", TFT_W / 2, SB_Y + SB_H / 2);
}

// ════════════════════════════════════════════════════════════════
// SCREEN 3 — Session stats
// ════════════════════════════════════════════════════════════════
static void renderSession(const GaugeData &d)
{
    spr.fillScreen(COL_BG);
    spr.fillRect(0, 0, TFT_W, 32, COL_HEADER);
    spr.setTextDatum(TC_DATUM);
    spr.setTextColor(COL_WHITE, COL_HEADER);
    spr.setTextSize(2);
    spr.drawString("SESSION", TFT_W / 2, 8);

    const char* lbls[4]    = { "Dispensed",  "Flow rate",  "Target",    "Supply"     };
    uint16_t    lblCols[4] = { COL_CYAN,     COL_ORANGE,   COL_YELLOW,  MSG_FILLING  };
    char        vals[4][24];
    uint16_t    valCols[4] = { COL_WHITE, COL_WHITE, COL_YELLOW, tankColor(d.outerTankPct) };
    snprintf(vals[0], 24, "%d ml",   d.volumeMl);
    snprintf(vals[1], 24, "%d ml/m", d.flowMlMin);
    snprintf(vals[2], 24, "%d ml",   d.targetMl);
    snprintf(vals[3], 24, "%d%%",   (int)d.outerTankPct);

    const int ROW_H   = 56;
    const int START_Y = 34;

    for (int i = 0; i < 4; i++) {
        int y = START_Y + i * ROW_H;
        spr.drawFastHLine(0, y + ROW_H - 1, TFT_W, COL_PANEL);

        spr.setTextDatum(TL_DATUM);
        spr.setTextSize(2);
        spr.setTextColor(lblCols[i], COL_BG);
        spr.drawString(lbls[i], 10, y + 10);

        spr.setTextDatum(TR_DATUM);
        spr.setTextSize(3);
        spr.setTextColor(valCols[i], COL_BG);
        spr.drawString(vals[i], TFT_W - 10, y + 14);
    }

    int bary = START_Y + 4 * ROW_H + 4;
    if (bary < SB_Y - 14) {
        int bw = TFT_W - 16, r = 6;
        spr.fillRoundRect(8, bary, bw, 12, r, COL_TRACK);
        int fill = (int)(bw * constrain(d.outerTankPct / 100.0f, 0.0f, 1.0f));
        if (fill >= 12)  spr.fillRoundRect(8, bary, fill, 12, r, tankColor(d.outerTankPct));
        else if (fill > 0) spr.fillCircle(8 + r, bary + 6, 6, tankColor(d.outerTankPct));
    }

    spr.fillRect(0, SB_Y, TFT_W, SB_H, COL_PANEL);
    spr.drawFastHLine(0, SB_Y, TFT_W, COL_GREY);
    spr.setTextDatum(MC_DATUM);
    spr.setTextColor(COL_GREY, COL_PANEL);
    spr.setTextSize(1);
    spr.drawString("PRESS = back to gauge", TFT_W / 2, SB_Y + SB_H / 2);
}

// ════════════════════════════════════════════════════════════════
// SCREEN 4 — Network / OTA
// ════════════════════════════════════════════════════════════════
static void renderNet()
{
    spr.fillScreen(COL_BG);
    spr.fillRect(0, 0, TFT_W, 32, COL_HEADER);
    spr.setTextDatum(TC_DATUM);
    spr.setTextColor(COL_WHITE, COL_HEADER);
    spr.setTextSize(2);
    spr.drawString("NETWORK / OTA", TFT_W / 2, 8);

    spr.setTextDatum(TR_DATUM);
    spr.setTextSize(1);
    spr.setTextColor(COL_GREY, COL_HEADER);
    spr.drawString(FW_VERSION, TFT_W - 4, 20);

    int y = 50;
    spr.setTextDatum(TC_DATUM);

    spr.setTextColor(COL_GREY, COL_BG);
    spr.setTextSize(1);
    spr.drawString("ACCESS POINT", TFT_W / 2, y);  y += 18;

    spr.setTextColor(COL_CYAN, COL_BG);
    spr.setTextSize(3);
    spr.drawString("fuelstation.local", TFT_W / 2, y);  y += 36;

    spr.setTextColor(COL_WHITE, COL_BG);
    spr.setTextSize(2);
    spr.drawString(gNetApIP[0] ? gNetApIP : "---", TFT_W / 2, y);  y += 34;

    spr.drawFastHLine(16, y, TFT_W - 32, COL_PANEL);  y += 16;

    spr.setTextColor(COL_GREY, COL_BG);
    spr.setTextSize(1);
    spr.drawString("WIFI", TFT_W / 2, y);  y += 18;

    bool connected = strlen(gNetStaIP) > 0 && strcmp(gNetStaIP, "0.0.0.0") != 0;
    spr.setTextColor(connected ? MSG_FILLING : MSG_WARN, COL_BG);
    spr.setTextSize(2);
    spr.drawString(connected ? gNetStaIP : "Not connected", TFT_W / 2, y);  y += 32;

    spr.setTextColor(COL_GREY, COL_BG);
    spr.setTextSize(1);
    spr.drawString("Open browser to configure or update firmware", TFT_W / 2, y);

    spr.fillRect(0, SB_Y, TFT_W, SB_H, COL_PANEL);
    spr.drawFastHLine(0, SB_Y, TFT_W, COL_GREY);
    spr.setTextDatum(MC_DATUM);
    spr.setTextColor(COL_GREY, COL_PANEL);
    spr.setTextSize(1);
    spr.drawString("PRESS = back to gauge", TFT_W / 2, SB_Y + SB_H / 2);
}

// ════════════════════════════════════════════════════════════════
// Display_Update — called ~10 Hz from main loop
// ════════════════════════════════════════════════════════════════
void Display_Update(const GaugeData &data)
{
    const float spd = 0.15f;
    animSupply   += (data.outerTankPct   - animSupply)   * spd;
    animMain     += (data.mainTankPct    - animMain)     * spd;
    animBatt     += ((float)data.battPct - animBatt)     * spd;
    animPressure += (data.pressurePct    - animPressure) * spd;

    bool doRender = (currentScreen == SCREEN_GAUGE) || needsFullRedraw || (slideOffset > 0);

    if (!doRender && currentScreen == SCREEN_SESSION) {
        static int   lastVol = -1, lastFlow = -1, lastTarget = -1;
        static float lastSup = -1.0f;
        if (data.volumeMl != lastVol || data.flowMlMin != lastFlow ||
            data.targetMl != lastTarget || fabsf(data.outerTankPct - lastSup) > 0.5f) {
            lastVol = data.volumeMl; lastFlow = data.flowMlMin;
            lastTarget = data.targetMl; lastSup = data.outerTankPct;
            doRender = true;
        }
    }

    if (!doRender) return;
    needsFullRedraw = false;

    if (currentScreen == SCREEN_GAUGE && strcmp(data.heliName, photoModel) != 0)
        loadPhoto(data.heliName);

    switch (currentScreen) {
        case SCREEN_GAUGE:   renderGauge(data);  break;
        case SCREEN_ACTION:  renderAction(data); break;
        case SCREEN_HELI:    renderModel();       break;
        case SCREEN_SESSION: renderSession(data); break;
        case SCREEN_NET:     renderNet();         break;
        default: break;
    }

    if (slideOffset > 0) {
        const int step = TFT_W / 4;
        slideOffset = max(0, slideOffset - step);
        if (slideOffset > 0)
            spr.fillRect(TFT_W - slideOffset, 0, slideOffset, TFT_H, COL_BG);
    }

    spr.pushSprite(0, 0);
    // Photo is already embedded in the sprite via embedPhoto() — no post-push needed
}

// ════════════════════════════════════════════════════════════════
// Encoder
// ════════════════════════════════════════════════════════════════
void Display_EncoderScroll(int delta)
{
    if (currentScreen == SCREEN_ACTION) {
        actionSel = constrain(actionSel + delta, 0, ACTION_COUNT - 1);
        needsFullRedraw = true;
    } else if (currentScreen == SCREEN_HELI && numModels > 0) {
        heliScrollIdx = constrain(heliScrollIdx + delta, 0, numModels - 1);
        needsFullRedraw = true;
    } else if (currentScreen == SCREEN_GAUGE && delta != 0) {
        Display_SetScreen(SCREEN_ACTION);
    }
}

void Display_EncoderPress()
{
    if (currentScreen == SCREEN_HELI) {
        activeModelIndex = heliScrollIdx;
        Display_SetScreen(SCREEN_GAUGE);
    }
}
