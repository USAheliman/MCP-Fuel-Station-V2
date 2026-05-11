#pragma once
#include <Arduino.h>

// ═══════════════════════════════════════════════════════════════════
// MCP Fuel Station V2 — Pump Control
// BTS7960 H-bridge, closed-loop PI controller, ramp
// Ported 1:1 from V1 Teensy src/main.cpp
// ═══════════════════════════════════════════════════════════════════

// ── PWM limits ───────────────────────────────────────────────────
#define MIN_PWM  25
#define MAX_PWM  225

// ── Ramp ─────────────────────────────────────────────────────────
// At 50 ms interval: STEP_UP=5 → full range in ~2 s, STEP_DOWN=10 → stop in ~1 s
#define RAMP_INTERVAL_MS  50
#define RAMP_STEP_UP       5
#define RAMP_STEP_DOWN    10

// ── Closed loop PI ───────────────────────────────────────────────
#define CL_KP              0.015f
#define CL_KI              0.010f
#define CL_INTEGRAL_MAX   50.0f
#define CL_SETTLED_TOLERANCE 20   // ml/min
#define CL_SETTLED_COUNT      5

// ── Calibration ───────────────────────────────────────────────────
#define CAL_PWM  100

extern float mlPerMinPerPwm;
extern float drainMlPerMinPerPwm;
extern bool  PumpEnabled;

// Fill closed loop state
extern bool    closedLoopActive;
extern int     closedLoopTargetMlMin;
extern float   closedLoopIntegral;
extern float   closedLoopPwmFloat;
extern int     closedLoopCurrentPwm;

// Drain closed loop state
extern bool    drainClosedLoopActive;
extern bool    drainClosedLoopHasSettled;
extern int     drainClosedLoopTargetMlMin;
extern float   drainClosedLoopIntegral;
extern float   drainClosedLoopPwmFloat;
extern int     drainClosedLoopCurrentPwm;

// ── API ───────────────────────────────────────────────────────────
void Pump_Init();
void Pump_Enable();
void Pump_Disable();
void Pump_SetTarget(int signedSpeed);   // + = fill, - = drain
void Pump_SetOutput(int signedSpeed);
void Pump_UpdateRamp();                 // call every loop
void Pump_Stop();                       // immediate stop + disable

void Pump_UpdateFillClosedLoop(int actualFlowMlMin);
void Pump_UpdateDrainClosedLoop(int actualFlowMlMin);
void Pump_ResetFillLoop();
void Pump_ResetDrainLoop();

// Conversion helpers (ported from V1)
int Pump_PwmToMlMin(int pwm);
int Pump_MlMinToPwm(int mlMin);
int Pump_DrainPwmToMlMin(int pwm);
int Pump_DrainMlMinToPwm(int mlMin);
