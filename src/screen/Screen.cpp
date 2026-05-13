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
#define MSG_H      24
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

// Model browser card layout (3 cards × 82px = 246px within CONTENT_H=248)
#define THUMB_W    40
#define THUMB_H    30
#define MB_CARD_H  82

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
static int        gModelScroll  = 0;
static int        gBrowserStart = 0;   // first visible card index
static uint16_t   gThumbBuf[4][THUMB_W * THUMB_H];
static int        gThumbIdx[4] = {-1, -1, -1, -1};
static char       gNetStaIP[24] = "";
static char       gNetApIP[24]  = "";
static ScreenID   gHelpFromScreen = SCREEN_HOME;

// Post-pump review state
static bool gPostPump    = false;   // true = pump stopped, review screen shown
static bool gPostIsDrain = false;   // remember fill vs drain for colour/restart
static int  gPumpBtnSel  = 0;       // 0=STOP(running)/START(post), 1=BACK

// Calibration screen state
static int gCalPoint = 0;           // 0 = first target, 1 = second target

// Help screen encoder selection: 0 = Return, 1 = Calibrate Touch
static int gHelpSel = 0;

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

// Active JPEG decode target — set before each TJpgDec call
static uint16_t* gJpgBuf  = nullptr;
static int       gJpgBufW = PHOTO_W;
static int       gJpgBufH = PHOTO_H;

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
    if (!gCapturing || !gJpgBuf) return 0;
    for (int row = 0; row < (int)h; row++) {
        int dy = y + row;
        if (dy < 0 || dy >= gJpgBufH) continue;
        for (int col = 0; col < (int)w; col++) {
            int dx = x + col;
            if (dx >= 0 && dx < gJpgBufW)
                gJpgBuf[dy * gJpgBufW + dx] = bmp[row * w + col];
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
        gJpgBuf   = photoBuf;
        gJpgBufW  = PHOTO_W;
        gJpgBufH  = PHOTO_H;
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

static void loadThumb(int modelIdx, int slot)
{
    gThumbIdx[slot] = -1;
    if (modelIdx < 0 || modelIdx >= numModels) return;
    if (!heliModels[modelIdx].hasImage || !photoBuf) return;
    char path[64];
    snprintf(path, sizeof(path), "/models/%s/thumb.jpg", heliModels[modelIdx].name);
    if (!LittleFS.exists(path)) return;

    // Decode at scale=2 into photoBuf (same as HOME screen — works for any source size)
    memset(photoBuf, 0, PHOTO_W * PHOTO_H * sizeof(uint16_t));
    gJpgBuf   = photoBuf;
    gJpgBufW  = PHOTO_W;
    gJpgBufH  = PHOTO_H;
    gCapturing = true;
    TJpgDec.setJpgScale(2);
    JRESULT res = TJpgDec.drawFsJpg(0, 0, path, LittleFS);
    gCapturing = false;
    photoModel[0] = '\0';   // mark photoBuf as dirty so HOME reloads its photo

    if (res != 0) return;

    // Point-sample 160×120 → 40×30 (factor 4 in both axes)
    for (int row = 0; row < THUMB_H; row++)
        for (int col = 0; col < THUMB_W; col++)
            gThumbBuf[slot][row * THUMB_W + col] =
                photoBuf[(row * 4) * PHOTO_W + (col * 4)];

    gThumbIdx[slot] = modelIdx;
}

static void loadVisibleThumbs()
{
    for (int i = 0; i < 3; i++) {
        int idx = gBrowserStart + i;
        if (idx >= numModels) { gThumbIdx[i] = -1; continue; }
        if (gThumbIdx[i] != idx) loadThumb(idx, i);
    }
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

    // Firmware version — right, size 2 green (matches RTC)
    spr.setTextSize(2);
    spr.setTextDatum(MR_DATUM);
    spr.setTextColor(C_GREEN, C_HDR);
    spr.drawString(FW_VERSION, SCR_W - 6, HDR_H / 2);
}

// ════════════════════════════════════════════════════════════════
// Message bar — drawn on every screen (bottom strip)
// Layout: [?] 48px | separator | message text
// ════════════════════════════════════════════════════════════════
static void renderMsgBar(const ScreenData& d)
{
    uint16_t bg = C_HDR;
    uint16_t fg = d.msgColour;

    if (d.pumpRunning) {
        float f = 0.35f + 0.25f * sinf(millis() * 0.004f);
        uint8_t r = (uint8_t)(((d.msgColour >> 11) & 0x1F) * f);
        uint8_t g = (uint8_t)(((d.msgColour >>  5) & 0x3F) * f);
        uint8_t b = (uint8_t)(( d.msgColour        & 0x1F) * f);
        bg = ((uint16_t)r << 11) | ((uint16_t)g << 5) | b;
        fg = C_WHITE;
    }

    spr.fillRect(0, MSG_Y, SCR_W, MSG_H, bg);
    spr.drawFastHLine(0, MSG_Y, SCR_W, C_GREY);

    // Help button — left 46px, highlighted when HELP action is selected
    bool helpSel = (gCurScreen == SCREEN_HOME && gActionSel == ACTION_HELP)
                || (gCurScreen == SCREEN_HELP);
    uint16_t hBg = helpSel ? C_ACCENT : bg;
    uint16_t hFg = helpSel ? C_BG    : C_ACCENT;
    if (helpSel) spr.fillRoundRect(3, MSG_Y + 3, 40, MSG_H - 6, 4, hBg);
    spr.drawRoundRect(3, MSG_Y + 3, 40, MSG_H - 6, 4, C_ACCENT);
    spr.setTextDatum(MC_DATUM);
    spr.setTextSize(2);
    spr.setTextColor(hFg, hBg);
    spr.drawString("?", 23, MSG_Y + MSG_H / 2);

    // Vertical separator
    spr.drawFastVLine(48, MSG_Y + 2, MSG_H - 4, C_GREY);

    // Message text — centred in remaining strip
    spr.setTextDatum(MC_DATUM);
    spr.setTextSize(2);
    spr.setTextColor(fg, bg);
    spr.drawString(d.message, (48 + SCR_W) / 2, MSG_Y + MSG_H / 2);
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

    // Bottom row — idle: [RESET SUPPLY]  pump/post-pump: [STOP/START][BACK]
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
    } else {
        // RESET SUPPLY — full-width button aligned with right-panel action strip
        const int btnY = 250;
        const int btnH = 28;
        const int btnX = 8;
        const int btnW = LEFT_W - 16;   // 166px
        bool resetSel = (gActionSel == ACTION_RESET);
        if (resetSel) {
            spr.fillRoundRect(btnX, btnY, btnW, btnH, 5, C_ACCENT);
            spr.setTextColor(C_BLACK, C_ACCENT);
        } else {
            spr.drawRoundRect(btnX, btnY, btnW, btnH, 5, C_ACCENT);
            spr.setTextColor(C_ACCENT, C_BLACK);
        }
        spr.setTextDatum(MC_DATUM);
        spr.setTextSize(2);
        spr.drawString("RESET SUPPLY", btnX + btnW / 2, btnY + btnH / 2);
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
        snprintf(buf, sizeof(buf), "%.1fL / %.1fL",
                 d.supplyMl / 1000.0f, d.supplyCapMl / 1000.0f);
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

        snprintf(buf, sizeof(buf), "%.2fV/c  %d%%", d.cellV, d.battPct);
        dataBar(X, 192, W, 14, "BATTERY", animBatt, battCol(animBatt), buf);

        // Action buttons — draw 4 (FILL/DRAIN/MODEL/NET); HELP in msg bar; RESET in left panel.
        static const char*     kLabels[ACTION_COUNT]  = { "FILL", "DRAIN", "MODEL", "NET", "HELP", "RESET" };
        static const uint16_t  kAccent[ACTION_COUNT]  = { C_GREEN, C_ORANGE, C_BLUE, C_ACCENT, C_YELLOW, C_ACCENT };
        const int BTN_DRAW = ACTION_HELP;   // 4 buttons in strip: FILL DRAIN MODEL NET
        const int BTN_Y  = 250;
        const int BTN_H  = 28;
        int slot = (W - 12) / BTN_DRAW;          // ~71 px per slot
        for (int i = 0; i < BTN_DRAW; i++) {
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
            spr.setTextSize(2);
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
// Screen: MODEL BROWSER — 3 cards × 82px with photo thumbnails
//
// Layout (CONTENT_H=248):
//   3 cards × 82px = 246px, 2px background at bottom
//
// Each card:
//   [40×30 thumb]  Name                      ◄ ACTIVE
//                  Tank ml  F:speed  D:speed  Sns:Y  Prg:Xs
//                  Fills:N  Drains:N  Vol:N.NL
//
// Encoder scrolls; encoder press selects.
// ════════════════════════════════════════════════════════════════
static void renderModelBrowser()
{
    loadVisibleThumbs();

    spr.fillRect(0, CONTENT_Y, SCR_W, CONTENT_H, C_BG);

    static const uint16_t kAccentCols[] = { C_BLUE, C_ORANGE, C_ACCENT, C_PURPLE, C_GREEN };

    for (int i = 0; i < 3; i++) {
        int  idx = gBrowserStart + i;
        if (idx >= numModels) break;

        int      y   = CONTENT_Y + i * MB_CARD_H;
        bool     sel = (idx == gModelScroll);
        uint16_t bg  = sel ? C_CARD : C_BG;
        uint16_t brd = sel ? C_ACCENT : C_SEP;

        // Card background and border
        spr.fillRect(0, y, SCR_W, MB_CARD_H, bg);
        spr.drawRect(1, y + 1, SCR_W - 2, MB_CARD_H - 2, brd);

        // ── Thumbnail (40×30) centred vertically ─────────────────
        const int TX = 5;
        const int TY = y + (MB_CARD_H - THUMB_H) / 2;   // y+26
        if (gThumbIdx[i] == idx) {
            spr.pushImage(TX, TY, THUMB_W, THUMB_H, gThumbBuf[i]);
        } else {
            uint8_t  ai  = heliModels[idx].name[0]
                           ? (uint8_t)heliModels[idx].name[0] % 5 : 0;
            uint16_t ac  = kAccentCols[ai];
            uint8_t  cr  = (ac >> 11) & 0x1F;
            uint8_t  cg  = (ac >>  5) & 0x3F;
            uint8_t  cb  =  ac        & 0x1F;
            uint16_t dim = ((uint16_t)(cr/4) << 11) | ((uint16_t)(cg/4) << 5) | (cb/4);
            spr.fillRoundRect(TX, TY, THUMB_W, THUMB_H, 4, dim);
            spr.drawRoundRect(TX, TY, THUMB_W, THUMB_H, 4, ac);
            char ini[2] = { heliModels[idx].name[0]
                            ? (char)toupper((unsigned char)heliModels[idx].name[0]) : '?', 0 };
            spr.setTextDatum(MC_DATUM);
            spr.setTextColor(ac, dim);
            spr.setTextSize(3);
            spr.drawString(ini, TX + THUMB_W / 2, TY + THUMB_H / 2);
        }

        // ── Text block to the right of thumbnail ─────────────────
        const int TXT_X = TX + THUMB_W + 8;   // 53

        // Name row (+ ACTIVE badge)
        spr.setTextDatum(TL_DATUM);
        spr.setTextSize(2);
        spr.setTextColor(sel ? C_WHITE : C_GREY, bg);
        spr.drawString(heliModels[idx].name, TXT_X, y + 5);

        if (idx == activeModelIndex) {
            spr.setTextSize(1);
            spr.setTextDatum(TR_DATUM);
            spr.setTextColor(C_GREEN, bg);
            spr.drawString("ACTIVE", SCR_W - 6, y + 8);
        }

        // Specs row
        char spec[56];
        snprintf(spec, sizeof(spec), "%dml  F:%d  D:%d  Sns:%s  Prg:%ds",
                 heliModels[idx].tankVolumeMl,
                 heliModels[idx].fillSpeed,
                 heliModels[idx].drainSpeed,
                 heliModels[idx].hasTankSensor ? "Y" : "N",
                 heliModels[idx].purgeSecs);
        spr.setTextSize(2);
        spr.setTextDatum(TL_DATUM);
        spr.setTextColor(sel ? C_ACCENT : C_GREY, bg);
        spr.drawString(spec, TXT_X, y + 27);

        // Stats row
        char stats[48];
        snprintf(stats, sizeof(stats), "Fills:%u  Drains:%u  Vol:%.1fL",
                 heliModels[idx].totalFills,
                 heliModels[idx].totalDrains,
                 heliModels[idx].totalFillMl / 1000.0f);
        spr.setTextSize(2);
        spr.setTextColor(sel ? C_WHITE : C_GREY, bg);
        spr.drawString(stats, TXT_X, y + 49);

        // Separator between cards
        if (i < 2) spr.drawFastHLine(1, y + MB_CARD_H - 1, SCR_W - 2, C_SEP);
    }

    // Scroll position indicator — thin bar on far right (x=476..479)
    if (numModels > 3) {
        const int SB_H = 3 * MB_CARD_H;
        int thumbH = max(20, SB_H * 3 / numModels);
        int thumbY = CONTENT_Y + (SB_H - thumbH) * gBrowserStart / (numModels - 3);
        spr.fillRect(476, CONTENT_Y, 4, SB_H, C_SEP);
        spr.fillRect(476, thumbY, 4, thumbH, C_ACCENT);
    }
}

// ════════════════════════════════════════════════════════════════
// Screen: HELP — context-sensitive, set by gHelpFromScreen
// ════════════════════════════════════════════════════════════════

// Index matches helpContext() return value:
//   0 = HOME idle   1 = HOME filling   2 = HOME draining
//   3 = MODEL browser   4 = NETWORK
static const char* const kHelpLines[5][16] = {
    { // 0 — HOME (idle)
      "  HOME SCREEN",
      "",
      "  Encoder scrolls through actions.",
      "  Press button to activate.",
      "",
      "  FILL   Fill helicopter fuel tank",
      "  DRAIN  Drain fuel from tank",
      "  MODEL  Select active helicopter",
      "  NET    Network & web access",
      "  HELP   Show this help screen",
      "",
      "  Status bars: Model tank, Supply,",
      "  Pressure & Battery level.",
      nullptr, nullptr, nullptr
    },
    { // 1 — HOME filling
      "  FILL OPERATION",
      "",
      "  Filling the helicopter fuel tank.",
      "  Auto-stops when:",
      "   Tank-full sensor triggers, or",
      "   Target volume is reached.",
      "",
      "  ENCODER  Adjust pump speed",
      "  BUTTON   Stop pump immediately",
      "",
      "  After stop:",
      "  START to refill,  BACK for Home.",
      nullptr, nullptr, nullptr, nullptr
    },
    { // 2 — HOME draining
      "  DRAIN OPERATION",
      "",
      "  Draining the helicopter fuel tank.",
      "  Auto-stops when tank is empty",
      "  (flow rate drops sharply).",
      "",
      "  ENCODER  Adjust pump speed",
      "  BUTTON   Stop pump immediately",
      "",
      "  After stop:",
      "  START to re-drain, BACK for Home.",
      "",
      "  Flow threshold set in web Setup.",
      nullptr, nullptr, nullptr
    },
    { // 3 — MODEL browser
      "  MODEL BROWSER",
      "",
      "  ENCODER    Scroll models",
      "  BUTTON     Select (activate) model",
      "  LONG PRESS Return to Home",
      "",
      "  Each entry shows:",
      "   Tank volume in ml",
      "   Fill & Drain speed (ml/min)",
      "   Purge time & sensor status",
      "   Total fills and volume pumped",
      "",
      "  ACTIVE = currently selected.",
      "  Full setup via web interface.",
      nullptr, nullptr
    },
    { // 4 — NETWORK
      "  NETWORK & WEB ACCESS",
      "",
      "  Access Point (always active):",
      "   SSID: MCP-FuelStation-V2",
      "   URL:  fuelstation.local",
      "",
      "  Home WiFi: IP shown when live.",
      "   Same URL works home network.",
      "",
      "  WEB PAGES at fuelstation.local:",
      "   Main    Status dashboard",
      "   Fill/Drain  Pump control",
      "   Setup   Models & calibration",
      "   Station Stats & OTA update",
      "   Log     fuelstation.local/log",
      nullptr
    }
};

static int helpContext()
{
    if (gHelpFromScreen == SCREEN_MODEL) return 3;
    if (gHelpFromScreen == SCREEN_NET)   return 4;
    if (gLastData.pumpRunning || gPostPump)
        return (gLastData.msgColour == MSG_DRAINING || gPostIsDrain) ? 2 : 1;
    return 0;
}

static void renderHelp()
{
    spr.fillRect(0, CONTENT_Y, SCR_W, CONTENT_H, C_BG);

    // "Press encoder to return" hint
    spr.setTextDatum(TC_DATUM);
    spr.setTextSize(1);
    spr.setTextColor(C_GREY, C_BG);
    spr.drawString("Press encoder button to return", SCR_W / 2, CONTENT_Y + 6);
    spr.drawFastHLine(16, CONTENT_Y + 18, SCR_W - 32, C_SEP);

    const char* const* lines = kHelpLines[helpContext()];
    int y = CONTENT_Y + 26;

    for (int i = 0; i < 16 && lines[i] != nullptr; i++) {
        if (y + 16 > MSG_Y - 4) break;
        if (lines[i][0] == '\0') {
            y += 8;
            continue;
        }
        bool isTitle = (i == 0);
        bool isSub   = (lines[i][0] == ' ' && lines[i][1] == ' ' &&
                        lines[i][2] == ' ');  // 3+ leading spaces
        uint16_t col = isTitle ? C_ACCENT : (isSub ? C_GREY : C_WHITE);
        spr.setTextDatum(TL_DATUM);
        spr.setTextSize(2);
        spr.setTextColor(col, C_BG);
        spr.drawString(lines[i], 8, y);
        y += 18;
    }

}

// ════════════════════════════════════════════════════════════════
// Screen: CALIBRATION — 2-point touch alignment
// ════════════════════════════════════════════════════════════════
static void renderCalibration()
{
    // Target positions — must match CAL_DISP_X1/Y1/X2/Y2 in Touch.h
    static const int kCalX[2] = {40, 440};
    static const int kCalY[2] = {40, 280};
    int p = constrain(gCalPoint, 0, 1);

    spr.fillRect(0, CONTENT_Y, SCR_W, CONTENT_H, C_BG);

    spr.setTextDatum(TC_DATUM);
    spr.setTextSize(2);
    spr.setTextColor(C_ACCENT, C_BG);
    spr.drawString("TOUCH CALIBRATION", SCR_W / 2, CONTENT_Y + 12);

    spr.setTextSize(2);
    spr.setTextColor(C_WHITE, C_BG);
    spr.drawString("Tap the crosshair", SCR_W / 2, CONTENT_Y + 40);

    char buf[16];
    snprintf(buf, sizeof(buf), "Point %d of 2", p + 1);
    spr.setTextSize(2);
    spr.setTextColor(C_GREY, C_BG);
    spr.drawString(buf, SCR_W / 2, CONTENT_Y + 62);

    // Crosshair at target position
    int cx = kCalX[p], cy = kCalY[p];
    const int ARM = 18;
    spr.drawFastHLine(cx - ARM, cy, ARM * 2 + 1, C_ACCENT);
    spr.drawFastVLine(cx, cy - ARM, ARM * 2 + 1, C_ACCENT);
    spr.drawCircle(cx, cy, 10, C_ACCENT);
    spr.drawCircle(cx, cy, 11, C_ACCENT);
}

// ════════════════════════════════════════════════════════════════
// Screen: NETWORK / OTA
//
//  Two side-by-side cards (y=44, h=112):
//    Left  — Access Point info (mDNS + AP IP)
//    Right — Home WiFi status + STA IP
//  Full-width log card (y=166, h=68) with Font4 URL
//  Hint strip at y=252
// ════════════════════════════════════════════════════════════════
static void renderNetwork()
{
    spr.fillRect(0, CONTENT_Y, SCR_W, CONTENT_H, C_BG);

    const int CARD_Y = CONTENT_Y + 8;          // 44
    const int CARD_H = 112;
    const int CARD_W = (SCR_W - 24) / 2;       // 228
    const int CARD_L = 8;
    const int CARD_R = CARD_L + CARD_W + 8;    // 244
    const int cx     = CARD_L + CARD_W / 2;    // 122 — left card centre
    const int rx     = CARD_R + CARD_W / 2;    // 358 — right card centre

    spr.setTextDatum(TC_DATUM);

    // ── Access Point card ────────────────────────────────────────
    spr.fillRoundRect(CARD_L, CARD_Y, CARD_W, CARD_H, 6, C_HDR);
    spr.drawRoundRect(CARD_L, CARD_Y, CARD_W, CARD_H, 6, C_ACCENT);

    spr.setTextSize(1);
    spr.setTextColor(C_GREY, C_HDR);
    spr.drawString("ACCESS POINT", cx, CARD_Y + 10);

    spr.setTextSize(2);
    spr.setTextColor(C_ACCENT, C_HDR);
    spr.drawString("fuelstation.local", cx, CARD_Y + 32);

    spr.setTextSize(2);
    spr.setTextColor(C_WHITE, C_HDR);
    spr.drawString(gNetApIP[0] ? gNetApIP : "---", cx, CARD_Y + 58);

    spr.setTextSize(1);
    spr.setTextColor(C_GREEN, C_HDR);
    spr.drawString("Always active", cx, CARD_Y + 90);

    // ── Home WiFi card ───────────────────────────────────────────
    bool conn = strlen(gNetStaIP) > 0 && strcmp(gNetStaIP, "0.0.0.0") != 0;

    spr.fillRoundRect(CARD_R, CARD_Y, CARD_W, CARD_H, 6, C_HDR);
    spr.drawRoundRect(CARD_R, CARD_Y, CARD_W, CARD_H, 6, conn ? C_GREEN : C_ORANGE);

    spr.setTextSize(1);
    spr.setTextColor(C_GREY, C_HDR);
    spr.drawString("HOME WIFI", rx, CARD_Y + 10);

    spr.setTextSize(2);
    spr.setTextColor(conn ? C_GREEN : C_ORANGE, C_HDR);
    spr.drawString(conn ? "CONNECTED" : "NOT CONNECTED", rx, CARD_Y + 32);

    spr.setTextSize(2);
    spr.setTextColor(C_WHITE, C_HDR);
    spr.drawString(conn ? gNetStaIP : "---", rx, CARD_Y + 58);

    spr.setTextSize(1);
    spr.setTextColor(C_GREY, C_HDR);
    spr.drawString(conn ? "via home network" : "Use AP to connect", rx, CARD_Y + 90);

    // ── Event Log card ───────────────────────────────────────────
    const int LOG_Y = CARD_Y + CARD_H + 10;    // 166
    const int LOG_H = 68;

    spr.fillRoundRect(8, LOG_Y, SCR_W - 16, LOG_H, 6, C_HDR);
    spr.drawRoundRect(8, LOG_Y, SCR_W - 16, LOG_H, 6, C_PURPLE);

    spr.setTextSize(1);
    spr.setTextColor(C_GREY, C_HDR);
    spr.drawString("EVENT LOG", SCR_W / 2, LOG_Y + 8);

    spr.setTextFont(4);
    spr.setTextColor(C_ACCENT, C_HDR);
    spr.drawString("fuelstation.local/log", SCR_W / 2, LOG_Y + 26);
    spr.setTextFont(1);

    // ── Hint strip ───────────────────────────────────────────────
    spr.setTextSize(1);
    spr.setTextColor(C_GREY, C_BG);
    spr.drawString("Open browser to configure or update firmware  |  OTA update available via WIFI",
                   SCR_W / 2, LOG_Y + LOG_H + 12);
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
void     Screen_SetCalPoint(int p) { gCalPoint = constrain(p, 0, 1); }

void Screen_SetScreen(ScreenID s)
{
    if (s == SCREEN_HELP) {
        gHelpFromScreen = gCurScreen;   // remember which screen opened help
        gHelpSel        = 0;
    } else {
        gPostPump   = false;
        gPumpBtnSel = 0;
    }
    if (s == gCurScreen) return;
    gCurScreen   = s;
    gScreenChgMs = millis();
    if (s == SCREEN_MODEL) {
        gModelScroll  = constrain(activeModelIndex, 0, max(0, numModels - 1));
        gBrowserStart = constrain(gModelScroll - 1, 0, max(0, numModels - 3));
        for (int i = 0; i < 4; i++) gThumbIdx[i] = -1;
    }
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
            if (numModels > 0) {
                gModelScroll = constrain(gModelScroll + delta, 0, numModels - 1);
                if (gModelScroll < gBrowserStart)
                    gBrowserStart = gModelScroll;
                else if (gModelScroll >= gBrowserStart + 3)
                    gBrowserStart = gModelScroll - 2;
            }
            break;
        case SCREEN_HELP:
            break;
        default: break;
    }
}

void Screen_EncoderPress()
{
    if (gCurScreen == SCREEN_HELP) {
        // Return to wherever help was opened from
        gCurScreen   = gHelpFromScreen;
        gScreenChgMs = millis();
        return;
    }
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
        case SCREEN_HELP:
            renderHeader("HELP");
            renderHelp();
            break;
        case SCREEN_CAL:
            renderHeader("CALIBRATION");
            renderCalibration();
            break;
        default: break;
    }

    renderMsgBar(data);
    spr.pushSprite(0, 0);
}
