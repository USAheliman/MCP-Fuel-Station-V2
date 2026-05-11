#include "Screen.h"
#include "../../include/pins.h"
#include "../../include/version.h"
#include "../../include/splash_image.h"
#include "../heli/HeliLib.h"
#include "../rtc/RTC.h"
#include <TFT_eSPI.h>
#include <TJpg_Decoder.h>
#include <LittleFS.h>
#include <math.h>

// ════════════════════════════════════════════════════════════════
// Hardware
// ════════════════════════════════════════════════════════════════
static TFT_eSPI    tft;
static TFT_eSprite spr(&tft);

// ════════════════════════════════════════════════════════════════
// Layout — 480×320 landscape
//
//  y=  0.. 35   Header    (36 px) — date/time, title, fw version
//  y= 36..283   Content   (248 px)
//    x=  0..181   Left panel  — photo + model info + STOP
//    x=182         Separator line
//    x=184..479   Right panel — data bars / action buttons
//  y=284..319   Message bar (36 px) — all screens
//
// ════════════════════════════════════════════════════════════════
#define SCR_W     480
#define SCR_H     320
#define HDR_H      36
#define MSG_H      36
#define CONTENT_Y  HDR_H
#define CONTENT_H  (SCR_H - HDR_H - MSG_H)   // 248
#define MSG_Y      (SCR_H - MSG_H)            // 284

#define LEFT_W     182
#define SEP_X      LEFT_W
#define RIGHT_X    (LEFT_W + 2)
#define RIGHT_W    (SCR_W - RIGHT_X)          // 296

#define PHOTO_W    160
#define PHOTO_H    120
#define PHOTO_X    ((LEFT_W - PHOTO_W) / 2)   // 11
#define PHOTO_Y    (CONTENT_Y + 8)            // 44

// ════════════════════════════════════════════════════════════════
// Colour palette (RGB565)
// ════════════════════════════════════════════════════════════════
#define C_BG      0x0841   // dark navy background
#define C_BLACK   0x0000   // pure black — left panel so photo blends seamlessly
#define C_HDR     0x1082   // header / message bar background
#define C_CARD    0x0422   // selected-row card fill
#define C_SEP     0x2104   // separator lines and bar tracks
#define C_TRACK   0x2104
#define C_ACCENT  0x07FF   // cyan accent
#define C_WHITE   0xFFFF
#define C_GREY    0x7BEF
#define C_GREEN   0x07E0
#define C_ORANGE  0xFD20
#define C_RED     0xF800
#define C_YELLOW  0xFFE0
#define C_BLUE    0x4BBF
#define C_PURPLE  0xB81F

// ════════════════════════════════════════════════════════════════
// State
// ════════════════════════════════════════════════════════════════
static ScreenID   gCurScreen   = SCREEN_HOME;
static uint32_t   gScreenChgMs = 0;
static ScreenData gLastData;
static int        gActionSel   = 0;
static int        gModelScroll = 0;
static char       gNetStaIP[24] = "";
static char       gNetApIP[24]  = "";

// Post-pump review state
static bool gPostPump    = false;   // true = pump stopped, review screen shown
static bool gPostIsDrain = false;   // remember fill vs drain for colour/restart
static int  gPumpBtnSel  = 0;       // 0=STOP(running)/START(post), 1=BACK

// ── Animated bar values ──────────────────────────────────────────
static float animSupply   = 0.0f;
static float animMain     = 0.0f;
static float animBatt     = 0.0f;
static float animPressure = 0.0f;

// ── Photo cache ──────────────────────────────────────────────────
static uint16_t* photoBuf       = nullptr;
static char      photoModel[24] = "";
static bool      photoHasImage  = false;
static bool      gCapturing     = false;

// ════════════════════════════════════════════════════════════════
// Colour helpers
// ════════════════════════════════════════════════════════════════
static inline uint16_t tankCol(float p) {
    return (p >= 60.0f) ? C_GREEN : (p >= 30.0f) ? C_ORANGE : C_RED;
}
static inline uint16_t battCol(float p) {
    return (p >= 50.0f) ? C_GREEN : (p >= 20.0f) ? C_YELLOW : C_RED;
}

// ════════════════════════════════════════════════════════════════
// Bar primitives
// ════════════════════════════════════════════════════════════════

// Rounded pill progress bar with optional target marker
static void pillBar(int x, int y, int w, int h, float pct,
                    uint16_t fillCol, float markFrac = -1.0f)
{
    int r = h / 2;
    spr.fillRoundRect(x, y, w, h, r, C_TRACK);
    int fw = (int)(w * constrain(pct / 100.0f, 0.0f, 1.0f) + 0.5f);
    if (fw >= h)      spr.fillRoundRect(x, y, fw, h, r, fillCol);
    else if (fw > 0)  spr.fillCircle(x + r, y + r, r, fillCol);
    if (markFrac >= 0.0f) {
        int mx = x + (int)(w * constrain(markFrac, 0.0f, 1.0f) + 0.5f);
        spr.fillRect(max(x, mx - 1), y - 2, 3, h + 4, C_YELLOW);
    }
}

// Standard bar row: size-2 label left, white value right, bar below
// label at y, bar at y+20, height barH
static void dataBar(int x, int y, int w, int barH,
                    const char* lbl, float pct, uint16_t col,
                    const char* val, float markFrac = -1.0f)
{
    spr.setTextSize(2);
    spr.setTextDatum(TL_DATUM);
    spr.setTextColor(col, C_BG);
    spr.drawString(lbl, x, y);
    spr.setTextDatum(TR_DATUM);
    spr.setTextColor(C_WHITE, C_BG);
    spr.drawString(val, x + w, y);
    pillBar(x, y + 20, w, barH, pct, col, markFrac);
}

// Compact bar row: size-2 label, bar at y+20, height barH
static void dataBarSm(int x, int y, int w, int barH,
                      const char* lbl, float pct, uint16_t col,
                      const char* val, float markFrac = -1.0f)
{
    spr.setTextSize(2);
    spr.setTextDatum(TL_DATUM);
    spr.setTextColor(col, C_BG);
    spr.drawString(lbl, x, y);
    spr.setTextDatum(TR_DATUM);
    spr.setTextColor(C_WHITE, C_BG);
    spr.drawString(val, x + w, y);
    pillBar(x, y + 20, w, barH, pct, col, markFrac);
}

// ════════════════════════════════════════════════════════════════
// Photo — TJpgDec callback writes to PSRAM buffer only.
// Never touches TFT directly (would bypass sprite, corrupt display).
// ════════════════════════════════════════════════════════════════
static bool jpgOutput(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bmp)
{
    if (!gCapturing || !photoBuf) return 0;
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

// Splash callback — writes decoded JPEG blocks directly into the sprite.
static bool jpgSplashOutput(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bmp)
{
    spr.pushImage(x, y, w, h, bmp);
    return 1;
}

static void loadPhoto(const char* name)
{
    photoHasImage = false;
    if (!photoBuf) {
        strncpy(photoModel, name, sizeof(photoModel) - 1);
        return;
    }
    memset(photoBuf, 0, PHOTO_W * PHOTO_H * sizeof(uint16_t));

    char path[64];
    snprintf(path, sizeof(path), "/models/%s/thumb.jpg", name);
    Serial.printf("Screen: loadPhoto '%s' -> %s  exists=%d\n",
                  name, path, (int)LittleFS.exists(path));

    if (LittleFS.exists(path)) {
        gCapturing = true;
        TJpgDec.setJpgScale(2);
        JRESULT res = TJpgDec.drawFsJpg(0, 0, path, LittleFS);
        gCapturing    = false;
        photoHasImage = (res == 0);
        Serial.printf("Screen: decode result=%d  hasImage=%d\n", res, (int)photoHasImage);
    }

    strncpy(photoModel, name, sizeof(photoModel) - 1);
    photoModel[sizeof(photoModel) - 1] = '\0';
}

static void blitPhoto()
{
    if (!photoHasImage || !photoBuf) return;
    spr.pushImage(PHOTO_X, PHOTO_Y, PHOTO_W, PHOTO_H, photoBuf);
}

// ════════════════════════════════════════════════════════════════
// Header — drawn on every screen
// ════════════════════════════════════════════════════════════════
static void renderHeader(const char* title)
{
    spr.fillRect(0, 0, SCR_W, HDR_H, C_HDR);
    spr.drawFastHLine(0, HDR_H - 1, SCR_W, C_GREY);

    // Date / time — size 2, green, cached once per second
    // Format: "10 May 14:32" (no year keeps it compact at 144px)
    static char  cachedDT[24] = "";
    static uint32_t lastRtcMs = 0;
    uint32_t now = millis();
    if (now - lastRtcMs >= 1000 || !cachedDT[0]) {
        lastRtcMs = now;
        RTC_GetHeaderStr(cachedDT, sizeof(cachedDT));
    }
    spr.setTextSize(2);
    spr.setTextDatum(ML_DATUM);
    spr.setTextColor(C_GREEN, C_HDR);
    spr.drawString(cachedDT[0] ? cachedDT : "--:--", 6, HDR_H / 2);

    // Screen title — centred, size 2 white
    spr.setTextSize(2);
    spr.setTextDatum(MC_DATUM);
    spr.setTextColor(C_WHITE, C_HDR);
    spr.drawString(title, SCR_W / 2, HDR_H / 2);

    // Firmware version — right, size 1 green
    spr.setTextSize(1);
    spr.setTextDatum(MR_DATUM);
    spr.setTextColor(C_GREEN, C_HDR);
    spr.drawString(FW_VERSION, SCR_W - 6, HDR_H / 2);
}

// ════════════════════════════════════════════════════════════════
// Message bar — drawn on every screen (bottom strip)
// ════════════════════════════════════════════════════════════════
static void renderMsgBar(const ScreenData& d)
{
    uint16_t bg = C_HDR;
    uint16_t fg = d.msgColour;

    if (d.pumpRunning) {
        // Pulse background gently in pump accent colour
        float f = 0.35f + 0.25f * sinf(millis() * 0.004f);
        uint8_t r = (uint8_t)(((d.msgColour >> 11) & 0x1F) * f);
        uint8_t g = (uint8_t)(((d.msgColour >>  5) & 0x3F) * f);
        uint8_t b = (uint8_t)(( d.msgColour        & 0x1F) * f);
        bg = ((uint16_t)r << 11) | ((uint16_t)g << 5) | b;
        fg = C_WHITE;
    }

    spr.fillRect(0, MSG_Y, SCR_W, MSG_H, bg);
    spr.drawFastHLine(0, MSG_Y, SCR_W, C_GREY);
    spr.setTextDatum(MC_DATUM);
    spr.setTextSize(2);
    spr.setTextColor(fg, bg);
    spr.drawString(d.message, SCR_W / 2, MSG_Y + MSG_H / 2);
}

// ════════════════════════════════════════════════════════════════
// Left panel — photo, model info, STOP button (pump only)
// ════════════════════════════════════════════════════════════════
static void renderLeftPanel(const ScreenData& d)
{
    spr.fillRect(0, CONTENT_Y, LEFT_W, CONTENT_H, C_BLACK);
    spr.drawFastVLine(SEP_X, CONTENT_Y, CONTENT_H, C_GREY);

    // Photo or initial-letter placeholder
    if (!photoHasImage) {
        static const uint16_t kAccent[] = { C_BLUE, C_ORANGE, C_ACCENT, C_PURPLE, C_GREEN };
        uint8_t ai = d.modelName[0] ? (uint8_t)d.modelName[0] % 5 : 0;
        uint16_t ac = kAccent[ai];
        uint8_t cr = (ac >> 11) & 0x1F;
        uint8_t cg = (ac >>  5) & 0x3F;
        uint8_t cb =  ac        & 0x1F;
        uint16_t dim = ((uint16_t)(cr / 4) << 11) | ((uint16_t)(cg / 4) << 5) | (cb / 4);
        spr.fillRoundRect(PHOTO_X, PHOTO_Y, PHOTO_W, PHOTO_H, 8, dim);
        spr.drawRoundRect(PHOTO_X, PHOTO_Y, PHOTO_W, PHOTO_H, 8, ac);
        char ini[2] = { d.modelName[0] ? (char)toupper((unsigned char)d.modelName[0]) : '?', 0 };
        spr.setTextDatum(MC_DATUM);
        spr.setTextColor(ac, dim);
        spr.setTextSize(6);
        spr.drawString(ini, PHOTO_X + PHOTO_W / 2, PHOTO_Y + PHOTO_H / 2);
    } else {
        blitPhoto();
    }

    // Model name
    const int nameY = PHOTO_Y + PHOTO_H + 8;   // y=172
    spr.setTextDatum(TC_DATUM);
    spr.setTextSize(2);
    spr.setTextColor(C_WHITE, C_BLACK);
    spr.drawString(d.modelName, LEFT_W / 2, nameY);

    // Tank volume and sensor status
    if (activeModelIndex >= 0) {
        char buf[24];
        snprintf(buf, sizeof(buf), "%d ml", heliModels[activeModelIndex].tankVolumeMl);
        spr.setTextSize(2);
        spr.setTextColor(C_YELLOW, C_BLACK);
        spr.drawString(buf, LEFT_W / 2, nameY + 20);

        bool sns = heliModels[activeModelIndex].hasTankSensor;
        spr.setTextColor(sns ? C_GREEN : C_ORANGE, C_BLACK);
        spr.drawString(sns ? "SENSOR: YES" : "SENSOR: NO", LEFT_W / 2, nameY + 40);
    }

    // Two-button row — pump running: [STOP*][BACK]  post-pump: [START][BACK*]
    if (d.pumpRunning || gPostPump) {
        const int btnY  = MSG_Y - 8 - 30;        // y=246
        const int btnH  = 30;
        const int btnX1 = 8;
        const int btnX2 = 94;                     // 8 + 80 + 6
        const int btnW  = 80;

        spr.setTextDatum(MC_DATUM);
        spr.setTextSize(2);

        if (d.pumpRunning) {
            // STOP — always highlighted while pump runs
            spr.fillRoundRect(btnX1, btnY, btnW, btnH, 6, C_RED);
            spr.drawRoundRect(btnX1, btnY, btnW, btnH, 6, C_WHITE);
            spr.setTextColor(C_WHITE, C_RED);
            spr.drawString("STOP", btnX1 + btnW / 2, btnY + btnH / 2);
            // BACK — dim
            spr.drawRoundRect(btnX2, btnY, btnW, btnH, 6, C_GREY);
            spr.setTextColor(C_GREY, C_BLACK);
            spr.drawString("BACK", btnX2 + btnW / 2, btnY + btnH / 2);
        } else {
            // START — highlighted if selected (gPumpBtnSel==0)
            bool startSel = (gPumpBtnSel == 0);
            if (startSel) {
                spr.fillRoundRect(btnX1, btnY, btnW, btnH, 6, C_GREEN);
                spr.drawRoundRect(btnX1, btnY, btnW, btnH, 6, C_WHITE);
                spr.setTextColor(C_BG, C_GREEN);
            } else {
                spr.drawRoundRect(btnX1, btnY, btnW, btnH, 6, C_GREEN);
                spr.setTextColor(C_GREEN, C_BG);
            }
            spr.drawString("START", btnX1 + btnW / 2, btnY + btnH / 2);
            // BACK — highlighted if selected (gPumpBtnSel==1)
            bool backSel = (gPumpBtnSel == 1);
            if (backSel) {
                spr.fillRoundRect(btnX2, btnY, btnW, btnH, 6, C_ACCENT);
                spr.drawRoundRect(btnX2, btnY, btnW, btnH, 6, C_WHITE);
                spr.setTextColor(C_BG, C_ACCENT);
            } else {
                spr.drawRoundRect(btnX2, btnY, btnW, btnH, 6, C_ACCENT);
                spr.setTextColor(C_ACCENT, C_BG);
            }
            spr.drawString("BACK", btnX2 + btnW / 2, btnY + btnH / 2);
        }
    }
}

// ════════════════════════════════════════════════════════════════
// Right panel — HOME screen
//
// Idle: 4 data bars + 4 action buttons
//
//   MODEL TANK   label y=44  bar y=64  h=14   ends 78
//   SUPPLY TANK  label y=88  bar y=108 h=14   ends 122
//   separator    y=130
//   PRESSURE     label y=140 bar y=160 h=14   ends 174
//   BATTERY      label y=192 bar y=212 h=14   ends 226
//   action btns  y=250 h=28
//
// Pump running: 4 compact bars (size-2 label + bar, 36px rows)
//
//   MODEL TANK   label y=44  bar y=64  h=14   ends 78
//   SUPPLY TANK  label y=88  bar y=108 h=14   ends 122
//   separator    y=130
//   PUMP SPEED   label y=134 bar y=154 h=12   ends 166
//   FLOW RATE    label y=170 bar y=190 h=12   ends 202
//   PRESSURE     label y=206 bar y=226 h=12   ends 238
//   BATTERY      label y=242 bar y=262 h=12   ends 274
// ════════════════════════════════════════════════════════════════
static void renderRightHome(const ScreenData& d)
{
    const int X = RIGHT_X, W = RIGHT_W;
    spr.fillRect(X, CONTENT_Y, W, CONTENT_H, C_BG);

    char buf[40];
    // During post-pump review, use the remembered fill/drain type for colour
    bool isDrain = d.pumpRunning ? (d.msgColour == MSG_DRAINING) : gPostIsDrain;
    uint16_t pumpCol = isDrain ? C_ORANGE : C_GREEN;

    // ── MODEL TANK (both modes) ───────────────────────────────────
    if ((d.pumpRunning || gPostPump) && d.volumeMl > 0 &&
        activeModelIndex >= 0 && heliModels[activeModelIndex].tankVolumeMl > 0) {
        snprintf(buf, sizeof(buf), "%d / %d ml",
                 d.volumeMl, heliModels[activeModelIndex].tankVolumeMl);
    } else if (activeModelIndex >= 0 && heliModels[activeModelIndex].tankVolumeMl > 0) {
        snprintf(buf, sizeof(buf), "%d ml", heliModels[activeModelIndex].tankVolumeMl);
    } else {
        snprintf(buf, sizeof(buf), "%d%%", (int)(animMain + 0.5f));
    }
    float tankMark = -1.0f;
    if (d.targetMl > 0 && activeModelIndex >= 0 &&
        heliModels[activeModelIndex].tankVolumeMl > 0)
        tankMark = (float)d.targetMl / heliModels[activeModelIndex].tankVolumeMl;
    dataBar(X, 44, W, 14, "MODEL TANK", animMain, C_BLUE, buf, tankMark);

    // ── SUPPLY TANK (both modes) ──────────────────────────────────
    if (d.supplyCapMl > 0)
        snprintf(buf, sizeof(buf), "%.1f L", d.supplyMl / 1000.0f);
    else
        snprintf(buf, sizeof(buf), "%d%%", (int)(animSupply + 0.5f));
    float supMark = (d.supplyCapMl > 0)
        ? (float)d.supplyLowMl / (float)d.supplyCapMl : -1.0f;
    dataBar(X, 88, W, 14, "SUPPLY TANK", animSupply,
            tankCol(animSupply), buf, supMark);

    // Separator
    spr.drawFastHLine(X + 4, 130, W - 8, C_SEP);

    // ── Idle: 2 more bars + buttons ───────────────────────────────
    if (!d.pumpRunning && !gPostPump) {

        snprintf(buf, sizeof(buf), "%d%%", (int)(animPressure + 0.5f));
        dataBar(X, 140, W, 14, "PRESSURE", animPressure, C_PURPLE, buf);

        snprintf(buf, sizeof(buf), "%d%%", d.battPct);
        dataBar(X, 192, W, 14, "BATTERY", animBatt, battCol(animBatt), buf);

        // Action buttons
        static const char*     kLabels[ACTION_COUNT]  = { "FILL", "DRAIN", "MODEL", "NET" };
        static const uint16_t  kAccent[ACTION_COUNT]  = { C_GREEN, C_ORANGE, C_BLUE, C_ACCENT };
        const int BTN_Y  = 250;
        const int BTN_H  = 28;
        int slot = (W - 12) / ACTION_COUNT;   // ~71 px per slot
        for (int i = 0; i < ACTION_COUNT; i++) {
            int bx  = X + 6 + i * slot;
            int bw  = slot - 4;
            bool sel = (i == gActionSel);
            if (sel) {
                spr.fillRoundRect(bx, BTN_Y, bw, BTN_H, 5, kAccent[i]);
                spr.setTextColor(C_BG, kAccent[i]);
            } else {
                spr.drawRoundRect(bx, BTN_Y, bw, BTN_H, 5, kAccent[i]);
                spr.setTextColor(kAccent[i], C_BG);
            }
            spr.setTextDatum(MC_DATUM);
            spr.setTextSize(1);
            spr.drawString(kLabels[i], bx + bw / 2, BTN_Y + BTN_H / 2);
        }

    // ── Pump running: 4 compact bars ─────────────────────────────
    } else {

        snprintf(buf, sizeof(buf), "%d%%", (int)(d.pumpSpeedPct + 0.5f));
        dataBarSm(X, 134, W, 12, "PUMP SPEED", d.pumpSpeedPct, pumpCol, buf);

        float flowPct  = constrain(d.flowMlMin / 2000.0f * 100.0f, 0.0f, 100.0f);
        float flowMark = -1.0f;
        if (activeModelIndex >= 0) {
            int tgt = isDrain ? heliModels[activeModelIndex].drainSpeed
                              : heliModels[activeModelIndex].fillSpeed;
            if (tgt > 0) flowMark = (float)tgt / 2000.0f;
        }
        snprintf(buf, sizeof(buf), "%d ml/m", d.flowMlMin);
        dataBarSm(X, 170, W, 12, "FLOW RATE", flowPct, pumpCol, buf, flowMark);

        snprintf(buf, sizeof(buf), "%d%%", (int)(animPressure + 0.5f));
        dataBarSm(X, 206, W, 12, "PRESSURE", animPressure, C_PURPLE, buf);

        snprintf(buf, sizeof(buf), "%d%%", d.battPct);
        dataBarSm(X, 242, W, 12, "BATTERY", animBatt, battCol(animBatt), buf);
    }
}

// ════════════════════════════════════════════════════════════════
// Screen: MODEL BROWSER — full width, 3 visible rows
// Each row 82 px: name (size 2) @ y+8, specs (size 2) @ y+30, stats (size 2) @ y+52
// ════════════════════════════════════════════════════════════════
static void renderModelBrowser()
{
    spr.fillRect(0, CONTENT_Y, SCR_W, CONTENT_H, C_BG);

    const int ROW_H   = 82;
    const int VISIBLE = 3;
    int start = constrain(gModelScroll - 1, 0, max(0, numModels - VISIBLE));

    for (int i = 0; i < VISIBLE && (start + i) < numModels; i++) {
        int  idx = start + i;
        int  y   = CONTENT_Y + i * ROW_H;
        bool sel = (idx == gModelScroll);

        uint16_t bg  = sel ? C_CARD : C_BG;
        uint16_t brd = sel ? C_ACCENT : C_SEP;

        spr.fillRect(2, y + 2, SCR_W - 4, ROW_H - 4, bg);
        spr.drawRect(2, y + 2, SCR_W - 4, ROW_H - 4, brd);

        // Selection arrow
        if (sel) {
            spr.setTextSize(2);
            spr.setTextColor(C_ACCENT, bg);
            spr.setTextDatum(TL_DATUM);
            spr.drawString(">", 6, y + 10);
        }

        // Model name
        spr.setTextSize(2);
        spr.setTextColor(sel ? C_WHITE : C_GREY, bg);
        spr.setTextDatum(TL_DATUM);
        spr.drawString(heliModels[idx].name, 22, y + 8);

        // ACTIVE badge
        if (idx == activeModelIndex) {
            spr.setTextSize(1);
            spr.setTextDatum(TR_DATUM);
            spr.setTextColor(C_GREEN, bg);
            spr.drawString("ACTIVE", SCR_W - 8, y + 12);
        }

        // Specs line — abbreviated to fit ~40 chars at size 2
        char line1[64];
        snprintf(line1, sizeof(line1), "%dml  F:%d  D:%d  Prg:%ds  Snsr:%s",
                 heliModels[idx].tankVolumeMl,
                 heliModels[idx].fillSpeed,
                 heliModels[idx].drainSpeed,
                 heliModels[idx].purgeSecs,
                 heliModels[idx].hasTankSensor ? "YES" : "NO");
        spr.setTextSize(2);
        spr.setTextDatum(TL_DATUM);
        spr.setTextColor(sel ? C_ACCENT : C_GREY, bg);
        spr.drawString(line1, 10, y + 30);

        // Stats line
        char line2[48];
        snprintf(line2, sizeof(line2), "Fills:%u  Drains:%u  Vol:%.1fL",
                 heliModels[idx].totalFills,
                 heliModels[idx].totalDrains,
                 heliModels[idx].totalFillMl / 1000.0f);
        spr.setTextColor(sel ? C_WHITE : C_GREY, bg);
        spr.drawString(line2, 10, y + 52);
    }
}

// ════════════════════════════════════════════════════════════════
// Screen: NETWORK / OTA
// ════════════════════════════════════════════════════════════════
static void renderNetwork()
{
    spr.fillRect(0, CONTENT_Y, SCR_W, CONTENT_H, C_BG);

    int y = CONTENT_Y + 20;
    spr.setTextDatum(TC_DATUM);

    spr.setTextSize(1);
    spr.setTextColor(C_GREY, C_BG);
    spr.drawString("ACCESS POINT", SCR_W / 2, y);   y += 20;

    spr.setTextSize(3);
    spr.setTextColor(C_ACCENT, C_BG);
    spr.drawString("fuelstation.local", SCR_W / 2, y);  y += 40;

    spr.setTextSize(2);
    spr.setTextColor(C_WHITE, C_BG);
    spr.drawString(gNetApIP[0] ? gNetApIP : "---", SCR_W / 2, y);  y += 40;

    spr.drawFastHLine(16, y, SCR_W - 32, C_SEP);  y += 22;

    spr.setTextSize(1);
    spr.setTextColor(C_GREY, C_BG);
    spr.drawString("WIFI", SCR_W / 2, y);  y += 20;

    bool conn = strlen(gNetStaIP) > 0 && strcmp(gNetStaIP, "0.0.0.0") != 0;
    spr.setTextSize(2);
    spr.setTextColor(conn ? C_GREEN : C_RED, C_BG);
    spr.drawString(conn ? gNetStaIP : "Not connected", SCR_W / 2, y);  y += 36;

    spr.setTextSize(1);
    spr.setTextColor(C_GREY, C_BG);
    spr.drawString("Open browser to configure or update firmware", SCR_W / 2, y);
}

// ════════════════════════════════════════════════════════════════
// Public API
// ════════════════════════════════════════════════════════════════
void Screen_SetBrightness(uint8_t val) { ledcWrite(TFT_BL_LEDC_CHANNEL, val); }

void Screen_SetNetworkIP(const char* sta, const char* ap)
{
    strncpy(gNetStaIP, sta ? sta : "", sizeof(gNetStaIP) - 1);
    strncpy(gNetApIP,  ap  ? ap  : "", sizeof(gNetApIP)  - 1);
}

ScreenID Screen_CurrentScreen() { return gCurScreen; }
int      Screen_GetActionSel()  { return gActionSel; }
uint32_t Screen_GetScreenAge()  { return millis() - gScreenChgMs; }

void Screen_SetScreen(ScreenID s)
{
    gPostPump   = false;
    gPumpBtnSel = 0;
    if (s == gCurScreen) return;
    gCurScreen   = s;
    gScreenChgMs = millis();
    if (s == SCREEN_MODEL) gModelScroll = activeModelIndex;
    if (s == SCREEN_HOME)  gActionSel   = 0;
}

void Screen_SetPostPump(bool isDrain)
{
    gPostPump    = true;
    gPostIsDrain = isDrain;
    gPumpBtnSel  = 1;   // BACK highlighted by default
}

void Screen_ClearPostPump()
{
    gPostPump   = false;
    gPumpBtnSel = 0;
}

bool Screen_IsPostPump()    { return gPostPump; }
int  Screen_GetPumpBtnSel() { return gPumpBtnSel; }

void Screen_EncoderScroll(int delta)
{
    switch (gCurScreen) {
        case SCREEN_HOME:
            if (gPostPump)
                gPumpBtnSel = constrain(gPumpBtnSel + delta, 0, 1);
            else
                gActionSel = constrain(gActionSel + delta, 0, ACTION_COUNT - 1);
            break;
        case SCREEN_MODEL:
            if (numModels > 0)
                gModelScroll = constrain(gModelScroll + delta, 0, numModels - 1);
            break;
        default: break;
    }
}

void Screen_EncoderPress()
{
    if (gCurScreen == SCREEN_MODEL) {
        activeModelIndex = gModelScroll;
        Screen_SetScreen(SCREEN_HOME);
    }
}

void Screen_Init()
{
    ledcSetup(TFT_BL_LEDC_CHANNEL, 5000, 8);
    ledcAttachPin(PIN_TFT_BL, TFT_BL_LEDC_CHANNEL);
    Screen_SetBrightness(200);

    tft.init();
    tft.invertDisplay(true);
    tft.setRotation(1);
    tft.fillScreen(C_BG);

    spr.createSprite(SCR_W, SCR_H);

    photoBuf = (uint16_t*)ps_malloc(PHOTO_W * PHOTO_H * sizeof(uint16_t));
    if (!photoBuf)
        photoBuf = (uint16_t*)malloc(PHOTO_W * PHOTO_H * sizeof(uint16_t));
    Serial.printf("Screen: photoBuf=%p (%d bytes)\n",
                  photoBuf, PHOTO_W * PHOTO_H * (int)sizeof(uint16_t));

    TJpgDec.setJpgScale(2);
    TJpgDec.setSwapBytes(true);   // sprite stores big-endian; TJpgDec matches
    TJpgDec.setCallback(jpgOutput);

    gScreenChgMs = millis();
    Serial.println("Screen: init OK");
}

void Screen_ShowSplash()
{
    spr.fillSprite(0xFFFF);   // white canvas matches image background
    TJpgDec.setJpgScale(1);
    TJpgDec.setCallback(jpgSplashOutput);
    TJpgDec.drawJpg(0, 0, SPLASH_JPG, SPLASH_JPG_LEN);
    TJpgDec.setJpgScale(2);
    TJpgDec.setCallback(jpgOutput);   // restore for photo thumbnails

    // Title bar at the bottom
    spr.fillRect(0, SCR_H - 44, SCR_W, 44, C_RED);
    spr.setTextDatum(MC_DATUM);
    spr.setTextFont(4);
    spr.setTextColor(C_WHITE, C_RED);
    spr.drawString("MCP Fuel Station", SCR_W / 2, SCR_H - 22);
    spr.setTextFont(1);

    spr.pushSprite(0, 0);
}

// ════════════════════════════════════════════════════════════════
// Screen_Update — called ~10 Hz from main loop
// ════════════════════════════════════════════════════════════════
void Screen_Update(const ScreenData& data)
{
    gLastData = data;

    // Smooth bar animation
    const float spd = 0.15f;
    animSupply   += (data.outerTankPct       - animSupply)   * spd;
    animMain     += (data.mainTankPct        - animMain)     * spd;
    animBatt     += ((float)data.battPct     - animBatt)     * spd;
    animPressure += (data.pressurePct        - animPressure) * spd;

    // Reload photo when model changes (HOME screen only)
    if (gCurScreen == SCREEN_HOME &&
        strcmp(data.modelName, photoModel) != 0)
        loadPhoto(data.modelName);

    // Compose full frame into sprite
    spr.fillScreen(C_BG);

    switch (gCurScreen) {
        case SCREEN_HOME: {
            const char* title =
                data.pumpRunning ? (data.msgColour == MSG_DRAINING ? "DRAIN MODE" : "FILL MODE") :
                gPostPump        ? (gPostIsDrain                   ? "DRAIN MODE" : "FILL MODE") :
                "HOME";
            renderHeader(title);
            renderLeftPanel(data);
            renderRightHome(data);
            break;
        }
        case SCREEN_MODEL:
            renderHeader("MODEL BROWSER");
            renderModelBrowser();
            break;
        case SCREEN_NET:
            renderHeader("NETWORK / OTA");
            renderNetwork();
            break;
        default: break;
    }

    renderMsgBar(data);
    spr.pushSprite(0, 0);
}
