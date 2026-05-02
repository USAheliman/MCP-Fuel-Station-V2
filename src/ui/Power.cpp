#include "Power.h"
#include "../../include/pins.h"
#include "../display/Display.h"
#include "../pump/Pump.h"

// ═══════════════════════════════════════════════════════════════════
// MCP Fuel Station V2 — Power Management
// ═══════════════════════════════════════════════════════════════════

static ShortPressCb gShortPressCb = nullptr;
static ShutdownCb   gShutdownCb   = nullptr;

static uint32_t btnPressMs    = 0;
static bool     btnWasPressed = false;
static bool     screenStandby = false;
static uint32_t lastActivityMs = 0;
static bool     shutdownPending = false;

void Power_Init(ShortPressCb onShortPress, ShutdownCb onShutdown)
{
    gShortPressCb = onShortPress;
    gShutdownCb   = onShutdown;

    // GPIO16: read encoder SW timing (Pololu A already pulled via diode)
    pinMode(PIN_POLOLU_A_GPIO, INPUT_PULLUP);
    // GPIO17: Pololu OFF — keep LOW normally
    pinMode(PIN_POLOLU_OFF, OUTPUT);
    digitalWrite(PIN_POLOLU_OFF, LOW);

    lastActivityMs = millis();
    Serial.println("Power: init OK");
}

void Power_UpdateActivity()
{
    lastActivityMs = millis();
}

bool Power_IsStandby() { return screenStandby; }

void Power_ExitStandby()
{
    screenStandby = false;
    Display_SetBrightness(200);
    lastActivityMs = millis();
}

static void EnterStandby()
{
    screenStandby = true;
    Display_SetBrightness(8);
}

void Power_Shutdown()
{
    shutdownPending = true;
    Pump_Stop();

    if (gShutdownCb) gShutdownCb();

    Display_SetBrightness(0);

    // Wait for button release before pulsing OFF
    while (digitalRead(PIN_POLOLU_A_GPIO) == LOW) delay(10);
    delay(100);

    // Pulse Pololu OFF pin HIGH → cuts power
    digitalWrite(PIN_POLOLU_OFF, HIGH);
    delay(200);
    digitalWrite(PIN_POLOLU_OFF, LOW);

    // If USB powered, spin here
    while (true) delay(100);
}

void Power_Update()
{
    if (shutdownPending) return;

    bool btnPressed = (digitalRead(PIN_POLOLU_A_GPIO) == LOW);
    uint32_t now   = millis();

    if (btnPressed && !btnWasPressed) {
        if (btnPressMs == 0) btnPressMs = now;
        if ((now - btnPressMs) >= BTN_DEBOUNCE_MS) btnWasPressed = true;
    }
    else if (btnPressed && btnWasPressed) {
        if ((now - btnPressMs) >= BTN_LONG_PRESS_MS) Power_Shutdown();
    }
    else if (!btnPressed && btnWasPressed) {
        uint32_t dur = now - btnPressMs;
        btnWasPressed = false;
        btnPressMs    = 0;
        if (dur >= BTN_SHORT_PRESS_MS) {
            Power_UpdateActivity();
            if (screenStandby) Power_ExitStandby();
            else if (gShortPressCb) gShortPressCb();
        }
    }
    else {
        btnPressMs = 0;
    }

    // Screen standby / auto shutdown
    if (!PumpEnabled) {
        uint32_t idle = now - lastActivityMs;
        if (!screenStandby && idle >= SCREEN_STANDBY_MS) EnterStandby();
        if (idle >= AUTO_SHUTDOWN_MS) Power_Shutdown();
    } else {
        Power_UpdateActivity();  // keep alive while pumping
    }
}
