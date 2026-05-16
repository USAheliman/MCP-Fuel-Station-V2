#include "Power.h"
#include "../../include/pins.h"
#include "../screen/Screen.h"
#include "../pump/Pump.h"

// ═══════════════════════════════════════════════════════════════════
// MCP Fuel Station V2 — Power Management
//
// FALLING-edge ISR records press timestamp with hardware precision so
// presses that occur during the ~60ms Display_Update() block are never
// missed.  Release detection and hold-duration logic stay in the main
// loop where it is safe to call callbacks.
// ═══════════════════════════════════════════════════════════════════

static ShortPressCb gShortPressCb = nullptr;
static BackPressCb  gBackPressCb  = nullptr;
static ShutdownCb   gShutdownCb   = nullptr;

// ── ISR state ─────────────────────────────────────────────────────
static volatile uint32_t isrPressMs = 0;
static volatile bool     isrHasFall = false;

static void IRAM_ATTR OnBtnFall()
{
    uint32_t t = millis();
    if (!isrHasFall || (t - isrPressMs) > BTN_DEBOUNCE_MS) {
        isrPressMs = t;
        isrHasFall = true;
    }
}

// ── Main-loop state ───────────────────────────────────────────────
static uint32_t btnPressMs    = 0;
static bool     btnWasPressed = false;
static bool     backPressFired = false;
static bool     screenStandby = false;
static uint32_t lastActivityMs = 0;
static bool     shutdownPending = false;

void Power_Init(ShortPressCb onShortPress, BackPressCb onBackPress, ShutdownCb onShutdown)
{
    gShortPressCb = onShortPress;
    gBackPressCb  = onBackPress;
    gShutdownCb   = onShutdown;

    pinMode(PIN_POLOLU_A_GPIO, INPUT_PULLUP);
    pinMode(PIN_POLOLU_OFF, OUTPUT);
    digitalWrite(PIN_POLOLU_OFF, LOW);

    attachInterrupt(digitalPinToInterrupt(PIN_POLOLU_A_GPIO), OnBtnFall, FALLING);

    lastActivityMs = millis();
    Serial.println("Power: init OK");
}

void Power_UpdateActivity() { lastActivityMs = millis(); }
bool Power_IsStandby()      { return screenStandby; }

void Power_ExitStandby()
{
    screenStandby  = false;
    Screen_SetBrightness(200);
    lastActivityMs = millis();
}

static void EnterStandby()
{
    screenStandby = true;
    Screen_SetBrightness(8);
}

void Power_Shutdown()
{
    shutdownPending = true;
    Pump_Stop();
    if (gShutdownCb) gShutdownCb();   // plays BuzzerShutdown + saves state
    Screen_SetBrightness(0);
    while (digitalRead(PIN_POLOLU_A_GPIO) == LOW) delay(10);
    delay(100);
    digitalWrite(PIN_POLOLU_OFF, HIGH);
    delay(200);
    digitalWrite(PIN_POLOLU_OFF, LOW);
    while (true) delay(100);
}

void Power_Update()
{
    if (shutdownPending) return;

    uint32_t now     = millis();
    bool     btnHeld = (digitalRead(PIN_POLOLU_A_GPIO) == LOW);

    // Consume ISR press timestamp — only if pin is still LOW.
    // If GPIO16 returned HIGH before this loop ran, the edge was a noise spike
    // shorter than one loop cycle (≤60 ms during display render); discard it.
    if (isrHasFall && !btnWasPressed) {
        noInterrupts();
        isrHasFall = false;
        interrupts();
        if (digitalRead(PIN_POLOLU_A_GPIO) == LOW) {
            btnPressMs     = millis();   // measure hold from now, not from ISR
            btnWasPressed  = true;
            backPressFired = false;
        }
    }

    if (btnWasPressed) {
        uint32_t held = now - btnPressMs;

        if (!btnHeld) {
            // Released
            bool wasMed   = backPressFired;
            btnWasPressed  = false;
            btnPressMs     = 0;
            backPressFired = false;
            if (!wasMed && held >= BTN_SHORT_PRESS_MS) {
                Power_UpdateActivity();
                if (screenStandby) Power_ExitStandby();
                else if (gShortPressCb) gShortPressCb();
            }
        } else {
            if (held >= BTN_LONG_PRESS_MS) {
                Power_Shutdown();
            } else if (held >= BTN_BACK_PRESS_MS && !backPressFired) {
                backPressFired = true;
                Power_UpdateActivity();
                if (!screenStandby && gBackPressCb) gBackPressCb();
            }
        }
    }

    // Standby / auto-shutdown
    // Refresh 'now' — callbacks above (e.g. StopFill + SaveStationToFS) can take
    // hundreds of ms and call Power_UpdateActivity(), leaving lastActivityMs > stale
    // 'now'.  Unsigned subtraction then wraps to ~4 billion, firing EnterStandby
    // immediately and making the screen appear to shut off on a normal press.
    now = millis();
    if (!PumpEnabled) {
        uint32_t idle = now - lastActivityMs;
        if (!screenStandby && idle >= SCREEN_STANDBY_MS) EnterStandby();
        if (idle >= AUTO_SHUTDOWN_MS) Power_Shutdown();
    } else {
        Power_UpdateActivity();
    }
}
