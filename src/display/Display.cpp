#include "Display.h"
#include "../../include/pins.h"
#include "../heli/HeliLib.h"

// ═══════════════════════════════════════════════════════════════════
// MCP Fuel Station V2 — TFT Display Implementation
// ST7735S 128×160
// Screen 0: Rainbow gauge  Screen 1: Heli select  Screen 2: Session
// ═══════════════════════════════════════════════════════════════════

static TFT_eSPI tft = TFT_eSPI();

static DisplayScreen  currentScreen    = SCREEN_GAUGE;
static int            heliScrollIdx    = 0;   // heli list cursor
static bool           needsFullRedraw  = true;

// Last sent values — only redraw changed fields
static GaugeData lastData;
static bool      lastDataInit = false;

// Network screen IP storage
static char gNetStaIP[20] = "";
static char gNetApIP[20]  = "";

void Display_SetNetworkIP(const char* staIP, const char* apIP)
{
    strncpy(gNetStaIP, staIP ? staIP : "", sizeof(gNetStaIP) - 1);
    strncpy(gNetApIP,  apIP  ? apIP  : "", sizeof(gNetApIP)  - 1);
}

// ── Backlight LEDC ───────────────────────────────────────────────
void Display_SetBrightness(uint8_t val)
{
    ledcWrite(TFT_BL_LEDC_CHANNEL, val);
}

// ── Init ─────────────────────────────────────────────────────────
void Display_Init()
{
    ledcSetup(TFT_BL_LEDC_CHANNEL, 5000, 8);
    ledcAttachPin(PIN_TFT_BL, TFT_BL_LEDC_CHANNEL);
    Display_SetBrightness(200);

    tft.init();
    tft.setRotation(1);   // landscape — 160 wide, 128 tall
    tft.fillScreen(COL_BG);
    needsFullRedraw = true;
    Serial.println("Display: init OK");
}

DisplayScreen Display_CurrentScreen() { return currentScreen; }
int Display_SelectedHeliIndex()       { return heliScrollIdx; }

void Display_SetScreen(DisplayScreen s)
{
    currentScreen   = s;
    needsFullRedraw = true;
    if (s == SCREEN_HELI) heliScrollIdx = activeModelIndex;
}

// ─────────────────────────────────────────────────────────────────
// Helper: draw a semi-circle arc segment (top half, centre bottom)
// cx,cy = arc centre   r = radius   strokeW = stroke width
// startPct/endPct = 0.0–1.0 arc fill fraction
// ─────────────────────────────────────────────────────────────────
static void drawArc(int cx, int cy, int r, int strokeW,
                    float fillFrac, uint16_t colFill, uint16_t colBg)
{
    // Draw background arc then filled arc using pixel-stepping
    // (TFT_eSPI doesn't have a native partial arc — we step degrees)
    int steps = 90;  // 180° / 2° steps = 90 points — fast enough at this size
    for (int i = 0; i <= steps; i++) {
        float angle   = PI - (i / (float)steps) * PI;  // PI→0 left to right
        float fillEnd = PI - fillFrac * PI;
        uint16_t col  = (angle >= fillEnd) ? colFill : colBg;
        for (int w = 0; w < strokeW; w++) {
            int rr = r - w;
            int x  = cx + (int)(cosf(angle) * rr + 0.5f);
            int y  = cy - (int)(sinf(angle) * rr + 0.5f);
            tft.drawPixel(x, y, col);
        }
    }
}

// ─────────────────────────────────────────────────────────────────
// SCREEN 0 — Gauge
// ─────────────────────────────────────────────────────────────────
static void drawGaugeFull(const GaugeData &d)
{
    tft.fillScreen(COL_BG);

    // ── Header strip (heli name) ─────────────────────────────────
    tft.fillRect(0, 0, 160, 14, COL_HEADER);
    tft.setTextColor(COL_WHITE, COL_HEADER);
    tft.setTextSize(1);
    tft.setTextDatum(TC_DATUM);
    tft.drawString(d.heliName, 80, 3);

    // ── Message bar ───────────────────────────────────────────────
    tft.fillRect(0, 114, 160, 14, COL_MSG_BG);
    tft.setTextColor(d.msgColour, COL_MSG_BG);
    tft.setTextDatum(TC_DATUM);
    tft.drawString(d.message, 80, 116);

    // ── Rainbow arcs (centre at x=80, y=110, drawn upward) ───────
    // Outermost first (largest radius), innermost last
    int cx = 80, cy = 110;
    // Arc 1: outer tank (green)
    drawArc(cx, cy, 52, 9, d.outerTankPct / 100.0f,  ARC_OUTER_TK, 0x0200);
    // Arc 2: main tank (blue)
    drawArc(cx, cy, 42, 8, d.mainTankPct  / 100.0f,  ARC_MAIN_TK,  0x0004);
    // Arc 3: pump speed (orange)
    drawArc(cx, cy, 33, 7, d.pumpSpeedPct / 100.0f,  ARC_PUMP,     0x2000);
    // Arc 4: pressure (purple)
    drawArc(cx, cy, 25, 6, d.pressurePct  / 100.0f,  ARC_PRESSURE, 0x2002);

    // ── Centre readout ────────────────────────────────────────────
    char buf[16];
    snprintf(buf, sizeof(buf), "%d%%", (int)d.mainTankPct);
    tft.setTextColor(COL_WHITE, COL_BG);
    tft.setTextDatum(MC_DATUM);
    tft.setTextSize(2);
    tft.drawString(buf, cx, cy - 8);
    tft.setTextSize(1);
    tft.setTextColor(COL_GREY, COL_BG);
    tft.drawString("MAIN TANK", cx, cy + 6);

    // ── Battery indicator (top right) ─────────────────────────────
    snprintf(buf, sizeof(buf), "%d%%", d.battPct);
    tft.setTextColor(d.battPct < 20 ? 0xF800 : COL_GREY, COL_HEADER);
    tft.setTextDatum(TR_DATUM);
    tft.drawString(buf, 158, 3);

    // ── Arc colour key (left edge, tiny) ─────────────────────────
    tft.fillRect(0, 16, 3, 8,  ARC_OUTER_TK);
    tft.fillRect(0, 26, 3, 8,  ARC_MAIN_TK);
    tft.fillRect(0, 36, 3, 8,  ARC_PUMP);
    tft.fillRect(0, 46, 3, 8,  ARC_PRESSURE);
    tft.setTextColor(COL_DIM, COL_BG);
    tft.setTextDatum(TL_DATUM);
    tft.setTextSize(1);
    tft.drawString("OTR", 5, 18);
    tft.drawString("MNT", 5, 28);
    tft.drawString("PMP", 5, 38);
    tft.drawString("PSI", 5, 48);

    // ── Flow readout (right of arcs) ─────────────────────────────
    snprintf(buf, sizeof(buf), "%dml/m", d.flowMlMin);
    tft.setTextColor(COL_GREY, COL_BG);
    tft.setTextDatum(TR_DATUM);
    tft.drawString(buf, 158, 60);

    lastData     = d;
    lastDataInit = true;
}

static void updateGauge(const GaugeData &d)
{
    // Redraw only changed areas to reduce flicker
    bool heliChanged = lastDataInit && strcmp(d.heliName, lastData.heliName) != 0;
    bool msgChanged  = lastDataInit && (strcmp(d.message, lastData.message) != 0 ||
                                        d.msgColour != lastData.msgColour);
    bool arcChanged  = !lastDataInit ||
                       fabsf(d.outerTankPct - lastData.outerTankPct) > 0.5f ||
                       fabsf(d.mainTankPct  - lastData.mainTankPct)  > 0.5f ||
                       fabsf(d.pumpSpeedPct - lastData.pumpSpeedPct) > 0.5f ||
                       fabsf(d.pressurePct  - lastData.pressurePct)  > 0.5f;
    bool flowChanged = lastDataInit && d.flowMlMin != lastData.flowMlMin;

    // For simplicity full redraw if arcs changed (pixel-draw is fast enough)
    if (arcChanged || heliChanged || !lastDataInit) {
        drawGaugeFull(d);
        return;
    }

    if (msgChanged) {
        tft.fillRect(0, 114, 160, 14, COL_MSG_BG);
        tft.setTextColor(d.msgColour, COL_MSG_BG);
        tft.setTextDatum(TC_DATUM);
        tft.setTextSize(1);
        tft.drawString(d.message, 80, 116);
    }
    if (flowChanged) {
        char buf[16];
        tft.fillRect(100, 58, 60, 10, COL_BG);
        snprintf(buf, sizeof(buf), "%dml/m", d.flowMlMin);
        tft.setTextColor(COL_GREY, COL_BG);
        tft.setTextDatum(TR_DATUM);
        tft.setTextSize(1);
        tft.drawString(buf, 158, 60);
    }
    lastData = d;
}

// ─────────────────────────────────────────────────────────────────
// SCREEN 1 — Heli select
// ─────────────────────────────────────────────────────────────────
static void drawHeliScreen()
{
    tft.fillScreen(COL_BG);
    tft.fillRect(0, 0, 160, 13, COL_HEADER);
    tft.setTextColor(COL_WHITE, COL_HEADER);
    tft.setTextDatum(TC_DATUM);
    tft.setTextSize(1);
    tft.drawString("SELECT HELI", 80, 3);

    // Show up to 4 entries
    int visibleRows = 4;
    int startIdx    = max(0, heliScrollIdx - 1);
    startIdx        = min(startIdx, max(0, numModels - visibleRows));

    for (int i = 0; i < visibleRows && (startIdx + i) < numModels; i++) {
        int    idx    = startIdx + i;
        int    y      = 16 + i * 26;
        bool   sel    = (idx == heliScrollIdx);
        uint16_t bg   = sel ? 0x0820 : COL_BG;
        uint16_t bord = sel ? 0x07E0 : COL_HEADER;

        tft.fillRect(2, y, 156, 24, bg);
        tft.drawRect(2, y, 156, 24, bord);

        tft.setTextColor(sel ? COL_WHITE : COL_GREY, bg);
        tft.setTextDatum(TL_DATUM);
        tft.setTextSize(1);
        tft.drawString(heliModels[idx].name, 6, y + 4);

        char sub[32];
        snprintf(sub, sizeof(sub), "%dml  Sensor:%s",
                 heliModels[idx].tankVolumeMl,
                 heliModels[idx].hasTankSensor ? "Y" : "N");
        tft.setTextColor(COL_DIM, bg);
        tft.drawString(sub, 6, y + 14);
    }

    // Instructions
    tft.fillRect(0, 120, 160, 8, COL_HEADER);
    tft.setTextColor(COL_GREY, COL_HEADER);
    tft.setTextDatum(TC_DATUM);
    tft.drawString("TURN=scroll  PRESS=select", 80, 121);
}

// ─────────────────────────────────────────────────────────────────
// SCREEN 2 — Session
// ─────────────────────────────────────────────────────────────────
static void drawSessionScreen(const GaugeData &d)
{
    tft.fillScreen(COL_BG);
    tft.fillRect(0, 0, 160, 13, COL_HEADER);
    tft.setTextColor(COL_WHITE, COL_HEADER);
    tft.setTextDatum(TC_DATUM);
    tft.setTextSize(1);
    tft.drawString("SESSION", 80, 3);

    // Key-value rows
    struct Row { const char* label; char value[20]; };
    Row rows[4];
    snprintf(rows[0].value, 20, "%d ml",    d.volumeMl);
    snprintf(rows[1].value, 20, "%d ml/m",  d.flowMlMin);
    snprintf(rows[2].value, 20, "%.1f bar", d.pressurePct * 4.0f / 100.0f);
    snprintf(rows[3].value, 20, "%d / %dml",d.volumeMl, d.targetMl);
    rows[0].label = "Dispensed";
    rows[1].label = "Flow rate";
    rows[2].label = "Pressure";
    rows[3].label = "Fill";

    for (int i = 0; i < 4; i++) {
        int y = 16 + i * 18;
        tft.setTextColor(COL_GREY, COL_BG);
        tft.setTextDatum(TL_DATUM);
        tft.setTextSize(1);
        tft.drawString(rows[i].label, 4, y);
        tft.setTextColor(COL_WHITE, COL_BG);
        tft.setTextDatum(TR_DATUM);
        tft.drawString(rows[i].value, 156, y);
        tft.drawFastHLine(0, y + 14, 160, COL_HEADER);
    }

    // Tank bars
    int y = 92;
    tft.setTextColor(COL_GREY, COL_BG);
    tft.setTextDatum(TL_DATUM);
    tft.drawString("Supply", 4, y);
    tft.fillRect(54, y,  100, 7, COL_HEADER);
    tft.fillRect(54, y, (int)(d.outerTankPct), 7, ARC_OUTER_TK);

    y = 106;
    tft.drawString("Main", 4, y);
    tft.fillRect(54, y,  100, 7, COL_HEADER);
    tft.fillRect(54, y, (int)(d.mainTankPct), 7, ARC_MAIN_TK);

    // Footer
    tft.fillRect(0, 120, 160, 8, COL_HEADER);
    tft.setTextColor(COL_GREY, COL_HEADER);
    tft.setTextDatum(TC_DATUM);
    tft.drawString("PRESS=reset session", 80, 121);
}

// ─────────────────────────────────────────────────────────────────
// SCREEN 3 — Network / OTA
// ─────────────────────────────────────────────────────────────────
static void drawNetScreen()
{
    tft.fillScreen(COL_BG);
    tft.fillRect(0, 0, 160, 14, COL_HEADER);
    tft.setTextColor(COL_WHITE, COL_HEADER);
    tft.setTextDatum(TC_DATUM);
    tft.setTextSize(1);
    tft.drawString("NETWORK / OTA", 80, 3);

    int y = 22;
    tft.setTextDatum(TC_DATUM);
    tft.setTextSize(1);

    bool connected = strlen(gNetStaIP) > 0 && strcmp(gNetStaIP, "0.0.0.0") != 0;
    if (connected) {
        tft.setTextColor(COL_DIM, COL_BG);
        tft.drawString("WiFi: SilverLining", 80, y);
        y += 14;
        // Show STA IP in larger text (size 2 = 12px/char)
        tft.setTextColor(COL_WHITE, COL_BG);
        tft.setTextSize(2);
        tft.drawString(gNetStaIP, 80, y);
        y += 22;
        tft.setTextSize(1);
    } else {
        tft.setTextColor(MSG_WARN, COL_BG);
        tft.drawString("WiFi not connected", 80, y);
        y += 14;
        tft.setTextColor(COL_DIM, COL_BG);
    }

    tft.setTextColor(COL_DIM, COL_BG);
    char apLine[28];
    snprintf(apLine, sizeof(apLine), "AP: %s", gNetApIP);
    tft.drawString(apLine, 80, y);
    y += 12;

    tft.setTextColor(ARC_OUTER_TK, COL_BG);
    tft.drawString("fuelstation.local/ota", 80, y);
    y += 12;

    tft.setTextColor(COL_DIM, COL_BG);
    tft.drawString("Open in phone browser", 80, y);

    // Footer
    tft.fillRect(0, 120, 160, 8, COL_HEADER);
    tft.setTextColor(COL_GREY, COL_HEADER);
    tft.setTextDatum(TC_DATUM);
    tft.drawString("PRESS=back to gauge", 80, 121);
}

// ─────────────────────────────────────────────────────────────────
// Screen nav indicator (4 dots at top right)
// ─────────────────────────────────────────────────────────────────
static void drawNavDots(DisplayScreen s)
{
    // 4 dots: shift left by 5px so last dot stays at x=158
    for (int i = 0; i < (int)SCREEN_COUNT; i++) {
        uint16_t col = (i == (int)s) ? 0x07FF : COL_HEADER;
        tft.fillCircle(143 + i * 5, 7, 2, col);
    }
}

// ─────────────────────────────────────────────────────────────────
// Public update
// ─────────────────────────────────────────────────────────────────
void Display_Update(const GaugeData &data)
{
    if (needsFullRedraw) {
        needsFullRedraw = false;
        switch (currentScreen) {
            case SCREEN_GAUGE:   drawGaugeFull(data);      drawNavDots(currentScreen); break;
            case SCREEN_HELI:    drawHeliScreen();         drawNavDots(currentScreen); break;
            case SCREEN_SESSION: drawSessionScreen(data);  drawNavDots(currentScreen); break;
            case SCREEN_NET:     drawNetScreen();          drawNavDots(currentScreen); break;
            default: break;
        }
        return;
    }

    switch (currentScreen) {
        case SCREEN_GAUGE:   updateGauge(data); break;
        case SCREEN_SESSION: drawSessionScreen(data); break;
        default: break;
    }
}

// ── Encoder input ─────────────────────────────────────────────────
void Display_EncoderScroll(int delta)
{
    if (currentScreen == SCREEN_HELI && numModels > 0) {
        heliScrollIdx = constrain(heliScrollIdx + delta, 0, numModels - 1);
        needsFullRedraw = true;
    } else {
        // On other screens, scroll changes active screen
        int next = ((int)currentScreen + delta + SCREEN_COUNT) % SCREEN_COUNT;
        Display_SetScreen((DisplayScreen)next);
    }
}

void Display_EncoderPress()
{
    if (currentScreen == SCREEN_HELI) {
        activeModelIndex = heliScrollIdx;
        Display_SetScreen(SCREEN_GAUGE);
    }
}
