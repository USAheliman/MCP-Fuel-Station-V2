#include "Pump.h"
#include "../../include/pins.h"

// ═══════════════════════════════════════════════════════════════════
// MCP Fuel Station V2 — Pump Control Implementation
// Ported directly from V1 Teensy src/main.cpp
// BTS7960 LEDC PWM (replaces Teensy analogWrite)
// ═══════════════════════════════════════════════════════════════════

#define LEDC_RPWM_CH  2
#define LEDC_LPWM_CH  3
#define LEDC_FREQ     2000
#define LEDC_BITS     8

float mlPerMinPerPwm      = 0.0f;
float drainMlPerMinPerPwm = 0.0f;
bool  PumpEnabled         = false;

bool    closedLoopActive       = false;
int     closedLoopTargetMlMin  = 0;
float   closedLoopIntegral     = 0.0f;
float   closedLoopPwmFloat     = 0.0f;
int     closedLoopCurrentPwm   = 0;
static  uint8_t clSettledCount  = 0;

bool    drainClosedLoopActive      = false;
bool    drainClosedLoopHasSettled  = false;
int     drainClosedLoopTargetMlMin = 0;
float   drainClosedLoopIntegral    = 0.0f;
float   drainClosedLoopPwmFloat    = 0.0f;
int     drainClosedLoopCurrentPwm  = 0;
static  uint8_t drainClSettledCount = 0;

static int   currentSpeedSigned = 0;
static int   targetSpeedSigned  = 0;
static float clSmoothedPwm      = 0.0f;  // smoothed CL PWM fed to hardware

// ── Init ─────────────────────────────────────────────────────────
void Pump_Init()
{
    pinMode(PIN_PUMP_REN, OUTPUT);
    pinMode(PIN_PUMP_LEN, OUTPUT);
    digitalWrite(PIN_PUMP_REN, LOW);
    digitalWrite(PIN_PUMP_LEN, LOW);

    ledcSetup(LEDC_RPWM_CH, LEDC_FREQ, LEDC_BITS);
    ledcSetup(LEDC_LPWM_CH, LEDC_FREQ, LEDC_BITS);
    ledcAttachPin(PIN_PUMP_RPWM, LEDC_RPWM_CH);
    ledcAttachPin(PIN_PUMP_LPWM, LEDC_LPWM_CH);

    ledcWrite(LEDC_RPWM_CH, 0);
    ledcWrite(LEDC_LPWM_CH, 0);

    Serial.println("Pump: init OK");
}

void Pump_Enable()
{
    digitalWrite(PIN_PUMP_REN, HIGH);
    digitalWrite(PIN_PUMP_LEN, HIGH);
}

void Pump_Disable()
{
    ledcWrite(LEDC_RPWM_CH, 0);
    ledcWrite(LEDC_LPWM_CH, 0);
    digitalWrite(PIN_PUMP_REN, LOW);
    digitalWrite(PIN_PUMP_LEN, LOW);
}

void Pump_SetOutput(int signedSpeed)
{
    signedSpeed = constrain(signedSpeed, -255, 255);
    int mag = abs(signedSpeed);
    if (signedSpeed > 0) {
        ledcWrite(LEDC_LPWM_CH, 0);
        ledcWrite(LEDC_RPWM_CH, mag);
    } else if (signedSpeed < 0) {
        ledcWrite(LEDC_RPWM_CH, 0);
        ledcWrite(LEDC_LPWM_CH, mag);
    } else {
        ledcWrite(LEDC_RPWM_CH, 0);
        ledcWrite(LEDC_LPWM_CH, 0);
    }
}

void Pump_SetTarget(int signedSpeed)
{
    targetSpeedSigned = constrain(signedSpeed, -255, 255);
}

void Pump_UpdateRamp()
{
    static uint32_t lastMs = 0;
    uint32_t now = millis();
    if (now - lastMs < RAMP_INTERVAL_MS) return;
    lastMs = now;

    int desired;
    if (!PumpEnabled) {
        desired = 0;
    } else if (closedLoopActive) {
        // Smooth small corrections; fall back to fast step for large gaps (startup)
        int gap  = closedLoopCurrentPwm - (int)(clSmoothedPwm + 0.5f);
        int step = (abs(gap) > CL_SMOOTH_THRESHOLD) ? RAMP_STEP_UP : CL_SMOOTH_STEP;
        if (gap > 0) clSmoothedPwm += min(step, gap);
        else         clSmoothedPwm -= min(step, -gap);
        desired = +(int)(clSmoothedPwm + 0.5f);
    } else if (drainClosedLoopActive) {
        int gap  = drainClosedLoopCurrentPwm - (int)(clSmoothedPwm + 0.5f);
        int step = (abs(gap) > CL_SMOOTH_THRESHOLD) ? RAMP_STEP_UP : CL_SMOOTH_STEP;
        if (gap > 0) clSmoothedPwm += min(step, gap);
        else         clSmoothedPwm -= min(step, -gap);
        desired = -(int)(clSmoothedPwm + 0.5f);
    } else {
        desired = targetSpeedSigned;
    }

    if ((currentSpeedSigned > 0 && desired < 0) ||
        (currentSpeedSigned < 0 && desired > 0)) desired = 0;
    if (currentSpeedSigned == desired) return;

    int diff = desired - currentSpeedSigned;
    int step = (abs(desired) > abs(currentSpeedSigned)) ? RAMP_STEP_UP : RAMP_STEP_DOWN;
    if (diff > 0) currentSpeedSigned += min(step, diff);
    else          currentSpeedSigned -= min(step, -diff);

    Pump_SetOutput(currentSpeedSigned);
}

void Pump_Stop()
{
    PumpEnabled                = false;
    closedLoopActive           = false;
    closedLoopIntegral         = 0.0f;
    closedLoopPwmFloat         = 0.0f;
    clSettledCount             = 0;
    drainClosedLoopActive      = false;
    drainClosedLoopIntegral    = 0.0f;
    drainClosedLoopPwmFloat    = 0.0f;
    drainClSettledCount        = 0;
    drainClosedLoopHasSettled  = false;
    targetSpeedSigned          = 0;
    currentSpeedSigned         = 0;
    clSmoothedPwm              = 0.0f;
    Pump_SetOutput(0);
    Pump_Disable();
}

void Pump_ResetFillLoop()
{
    closedLoopIntegral    = 0.0f;
    closedLoopPwmFloat    = (float)MIN_PWM;
    closedLoopCurrentPwm  = MIN_PWM;
    clSettledCount        = 0;
    clSmoothedPwm         = (float)currentSpeedSigned;  // start smooth from current hw speed
}

void Pump_ResetDrainLoop()
{
    drainClosedLoopIntegral    = 0.0f;
    drainClosedLoopPwmFloat    = (float)MIN_PWM;
    drainClosedLoopCurrentPwm  = MIN_PWM;
    drainClSettledCount        = 0;
    drainClosedLoopHasSettled  = false;
    clSmoothedPwm              = (float)abs(currentSpeedSigned);  // start smooth from current hw speed
}

// ── Conversion helpers (ported 1:1 from V1) ──────────────────────
int Pump_PwmToMlMin(int pwm)
{
    if (mlPerMinPerPwm <= 0.0f) return 0;
    int u = pwm - MIN_PWM;
    if (u <= 0) return 0;
    return (int)(u * mlPerMinPerPwm + 0.5f);
}

int Pump_MlMinToPwm(int mlMin)
{
    if (mlPerMinPerPwm <= 0.0f) return MIN_PWM;
    if (mlMin <= 0) return 0;
    return constrain(MIN_PWM + (int)((float)mlMin / mlPerMinPerPwm + 0.5f), MIN_PWM, MAX_PWM);
}

int Pump_DrainPwmToMlMin(int pwm)
{
    if (drainMlPerMinPerPwm <= 0.0f) return 0;
    int u = pwm - MIN_PWM;
    if (u <= 0) return 0;
    return (int)(u * drainMlPerMinPerPwm + 0.5f);
}

int Pump_DrainMlMinToPwm(int mlMin)
{
    if (drainMlPerMinPerPwm <= 0.0f) return MIN_PWM;
    if (mlMin <= 0) return 0;
    return constrain(MIN_PWM + (int)((float)mlMin / drainMlPerMinPerPwm + 0.5f), MIN_PWM, MAX_PWM);
}

// ── Closed loop fill PI (ported 1:1 from V1) ─────────────────────
void Pump_UpdateFillClosedLoop(int actualFlowMlMin)
{
    if (!closedLoopActive || !PumpEnabled) return;

    float error      = (float)(closedLoopTargetMlMin - actualFlowMlMin);
    float newIntegral = closedLoopIntegral;
    if (actualFlowMlMin > 0)
        newIntegral = constrain(closedLoopIntegral + error, -CL_INTEGRAL_MAX, CL_INTEGRAL_MAX);

    float output = (CL_KP * error) + (CL_KI * newIntegral);
    if (actualFlowMlMin == 0) output = constrain(output, -3.0f, 3.0f);

    closedLoopPwmFloat += output;
    closedLoopPwmFloat  = constrain(closedLoopPwmFloat, (float)MIN_PWM, (float)MAX_PWM);
    int newPwm = (int)(closedLoopPwmFloat + 0.5f);

    bool atLimit = (newPwm == MIN_PWM || newPwm == MAX_PWM);
    if (!atLimit) closedLoopIntegral = newIntegral;

    closedLoopCurrentPwm = newPwm;  // ramp picks this up smoothly each tick

    // Self-correct mlPerMinPerPwm when settled
    if (fabsf(error) <= CL_SETTLED_TOLERANCE && !atLimit && closedLoopCurrentPwm > MIN_PWM) {
        clSettledCount++;
        if (clSettledCount >= CL_SETTLED_COUNT) {
            int usable = closedLoopCurrentPwm - MIN_PWM;
            if (usable > 0) {
                float nf = (float)actualFlowMlMin / (float)usable;
                mlPerMinPerPwm = mlPerMinPerPwm * 0.8f + nf * 0.2f;
            }
            clSettledCount = 0;
        }
    } else {
        clSettledCount = 0;
    }
}

// ── Closed loop drain PI (ported 1:1 from V1) ────────────────────
void Pump_UpdateDrainClosedLoop(int actualFlowMlMin)
{
    if (!drainClosedLoopActive || !PumpEnabled) return;

    float error      = (float)(drainClosedLoopTargetMlMin - actualFlowMlMin);
    float newIntegral = drainClosedLoopIntegral;
    if (actualFlowMlMin > 0)
        newIntegral = constrain(drainClosedLoopIntegral + error, -CL_INTEGRAL_MAX, CL_INTEGRAL_MAX);

    float output = (CL_KP * error) + (CL_KI * newIntegral);
    if (actualFlowMlMin == 0) output = constrain(output, -3.0f, 3.0f);

    drainClosedLoopPwmFloat += output;
    drainClosedLoopPwmFloat  = constrain(drainClosedLoopPwmFloat, (float)MIN_PWM, (float)MAX_PWM);
    int newPwm = (int)(drainClosedLoopPwmFloat + 0.5f);

    bool atLimit = (newPwm == MIN_PWM || newPwm == MAX_PWM);
    if (!atLimit) drainClosedLoopIntegral = newIntegral;

    drainClosedLoopCurrentPwm = newPwm;  // ramp picks this up smoothly each tick

    if (fabsf(error) <= CL_SETTLED_TOLERANCE && !atLimit && drainClosedLoopCurrentPwm > MIN_PWM) {
        drainClSettledCount++;
        if (drainClSettledCount >= CL_SETTLED_COUNT) {
            int usable = drainClosedLoopCurrentPwm - MIN_PWM;
            if (usable > 0) {
                float nf = (float)actualFlowMlMin / (float)usable;
                drainMlPerMinPerPwm = drainMlPerMinPerPwm * 0.8f + nf * 0.2f;
            }
            drainClSettledCount = 0;
            drainClosedLoopHasSettled = true;
        }
    } else {
        drainClSettledCount = 0;
    }
}
