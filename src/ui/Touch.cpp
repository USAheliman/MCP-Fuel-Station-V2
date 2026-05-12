#include "Touch.h"
#include "../../include/pins.h"
#include <Wire.h>
#include <Preferences.h>

// ═══════════════════════════════════════════════════════════════════
// GT911 minimal driver — I2C polling, no library dependency.
// Status register 0x814E; point data at 0x8150 (8 bytes/point, LE).
// Tap fires on finger lift with the coordinates captured at touch-down.
// ═══════════════════════════════════════════════════════════════════

#define GT911_ADDR      0x5D
#define REG_STATUS_HI   0x81
#define REG_STATUS_LO   0x4E   // 0x814E — buffer-status + touch count
#define REG_PT1_HI      0x81
#define REG_PT1_LO      0x50   // 0x8150 — point 1: id,xl,xh,yl,yh,szl,szh,rsv
#define TAP_DEBOUNCE_MS 300

static TouchTapCb gOnTap      = nullptr;
static bool       gWasTouched = false;
static uint32_t   gLastTapMs  = 0;
static bool       gFound      = false;

// Last tap — both raw (GT911 native) and calibrated (display pixels)
static uint16_t   gRawTapX    = 0;
static uint16_t   gRawTapY    = 0;
static int        gTapX       = 0;
static int        gTapY       = 0;

// Calibration — linear scale + offset applied to raw values
static float gCalScaleX  =  480.0f / 65536.0f;
static float gCalOffsetX =  0.0f;
static float gCalScaleY  = -320.0f / 65536.0f;
static float gCalOffsetY =  320.0f;
static bool  gCalibrated =  false;

// Calibration mode — fires TouchCalCb instead of TouchTapCb for 2 taps
static bool        gCalMode  = false;
static int         gCalPoint = 0;
static TouchCalCb  gCalCb    = nullptr;

// ── I2C helpers ─────────────────────────────────────────────────

static int readStatus()
{
    Wire1.beginTransmission(GT911_ADDR);
    Wire1.write(REG_STATUS_HI);
    Wire1.write(REG_STATUS_LO);
    if (Wire1.endTransmission(false) != 0) return -1;
    Wire1.requestFrom((uint8_t)GT911_ADDR, (uint8_t)1);
    if (!Wire1.available()) return -1;
    return Wire1.read();
}

static void clearBuffer()
{
    Wire1.beginTransmission(GT911_ADDR);
    Wire1.write(REG_STATUS_HI);
    Wire1.write(REG_STATUS_LO);
    Wire1.write((uint8_t)0x00);
    Wire1.endTransmission();
}

static void readPointRaw(uint16_t& rx, uint16_t& ry)
{
    Wire1.beginTransmission(GT911_ADDR);
    Wire1.write(REG_PT1_HI);
    Wire1.write(REG_PT1_LO);
    Wire1.endTransmission(false);
    Wire1.requestFrom((uint8_t)GT911_ADDR, (uint8_t)8);
    if (Wire1.available() < 8) { rx = ry = 0; return; }
    Wire1.read();                                       // track_id
    uint8_t xl = Wire1.read();
    uint8_t xh = Wire1.read();
    uint8_t yl = Wire1.read();
    uint8_t yh = Wire1.read();
    Wire1.read(); Wire1.read(); Wire1.read();            // size + reserved
    rx = (uint16_t)xl | ((uint16_t)xh << 8);
    ry = (uint16_t)yl | ((uint16_t)yh << 8);
}

static void applyCalibration(uint16_t rx, uint16_t ry, int& x, int& y)
{
    x = constrain((int)(rx * gCalScaleX + gCalOffsetX + 0.5f), 0, 479);
    y = constrain((int)(ry * gCalScaleY + gCalOffsetY + 0.5f), 0, 319);
}

// ── Public API ───────────────────────────────────────────────────

void Touch_Init(TouchTapCb onTap)
{
    gOnTap = onTap;
    pinMode(PIN_TP_INT, INPUT);

    Wire1.begin(PIN_TP_SDA, PIN_TP_SCL);
    Wire1.setClock(400000);
    Wire1.setTimeout(10);

    int status = readStatus();
    gFound = (status >= 0);
    if (gFound) {
        clearBuffer();
        Serial.println("Touch: GT911 OK (0x5D)");
    } else {
        Serial.println("Touch: GT911 not found — touch disabled");
    }
}

void Touch_Update()
{
    if (!gFound) return;

    uint32_t now = millis();
    static uint32_t lastPollMs = 0;
    if (now - lastPollMs < 50) return;
    lastPollMs = now;

    int status = readStatus();
    if (status < 0) return;

    bool    bufReady = (status & 0x80) != 0;
    uint8_t count    = status & 0x0F;

    if (!bufReady) return;

    if (count > 0) {
        readPointRaw(gRawTapX, gRawTapY);
        clearBuffer();
        applyCalibration(gRawTapX, gRawTapY, gTapX, gTapY);
        gWasTouched = true;
    } else {
        clearBuffer();
        if (gWasTouched) {
            gWasTouched = false;
            if (now - gLastTapMs >= TAP_DEBOUNCE_MS) {
                gLastTapMs = now;
                if (gCalMode) {
                    if (gCalCb) gCalCb(gCalPoint, gRawTapX, gRawTapY);
                    gCalPoint++;
                    if (gCalPoint >= 2) { gCalMode = false; gCalCb = nullptr; }
                } else {
                    if (gOnTap) gOnTap(gTapX, gTapY);
                }
            }
        }
    }
}

void Touch_StartCalibration(TouchCalCb cb)
{
    gCalMode  = true;
    gCalPoint = 0;
    gCalCb    = cb;
}

void Touch_CommitCalibration(uint16_t r0x, uint16_t r0y, uint16_t r1x, uint16_t r1y)
{
    // Require at least 500 raw units separation on each axis.
    // If both taps were near the same spot the scale explodes and breaks touch.
    int dx = abs((int)r1x - (int)r0x);
    int dy = abs((int)r1y - (int)r0y);
    if (dx < 500 || dy < 500) {
        Serial.printf("Touch: cal REJECTED — raw spread too small dx=%d dy=%d\n", dx, dy);
        return;
    }
    gCalScaleX  = (float)(CAL_DISP_X2 - CAL_DISP_X1) / (float)((int)r1x - (int)r0x);
    gCalOffsetX = CAL_DISP_X1 - r0x * gCalScaleX;
    gCalScaleY  = (float)(CAL_DISP_Y2 - CAL_DISP_Y1) / (float)((int)r1y - (int)r0y);
    gCalOffsetY = CAL_DISP_Y1 - r0y * gCalScaleY;
    gCalibrated = true;
    Serial.printf("Touch: cal OK  sx=%.5f ox=%.1f  sy=%.5f oy=%.1f\n",
                  gCalScaleX, gCalOffsetX, gCalScaleY, gCalOffsetY);
}

void Touch_SaveCalibration()
{
    Preferences prefs;
    prefs.begin("touch", false);
    prefs.putFloat("sx",  gCalScaleX);
    prefs.putFloat("ox",  gCalOffsetX);
    prefs.putFloat("sy",  gCalScaleY);
    prefs.putFloat("oy",  gCalOffsetY);
    prefs.putBool ("cal", true);
    prefs.end();
    Serial.printf("Touch: cal saved sx=%.6f ox=%.2f sy=%.6f oy=%.2f\n",
                  gCalScaleX, gCalOffsetX, gCalScaleY, gCalOffsetY);
}

void Touch_LoadCalibration()
{
    // Default formula (480/65536, -320/65536 inverted) is correct for this panel.
    // Wipe any previously stored calibration that may have overwritten it.
    Preferences prefs;
    prefs.begin("touch", false);
    prefs.clear();
    prefs.end();
    Serial.println("Touch: using built-in defaults (cal NVS cleared)");
}

bool Touch_IsCalibrated() { return gCalibrated; }
