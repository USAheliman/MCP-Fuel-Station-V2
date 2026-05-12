#pragma once
#include <Arduino.h>

// ═══════════════════════════════════════════════════════════════════
// MCP Fuel Station V2 — GT911 touch input
// Wire1 (SDA=47, SCL=41), INT=42, no RST.
// Tap fires on finger lift, matching the physical button's behaviour.
// ═══════════════════════════════════════════════════════════════════

// Known display positions used for 2-point calibration
#define CAL_DISP_X1  40
#define CAL_DISP_Y1  40
#define CAL_DISP_X2  440
#define CAL_DISP_Y2  280

typedef void (*TouchTapCb)(int x, int y);
typedef void (*TouchCalCb)(int point, uint16_t rawX, uint16_t rawY);

void Touch_Init(TouchTapCb onTap);
void Touch_Update();

// Calibration
void Touch_StartCalibration(TouchCalCb cb);
void Touch_CommitCalibration(uint16_t r0x, uint16_t r0y, uint16_t r1x, uint16_t r1y);
void Touch_SaveCalibration();
void Touch_LoadCalibration();
bool Touch_IsCalibrated();
