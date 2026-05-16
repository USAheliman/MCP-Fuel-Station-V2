#pragma once
#include <Arduino.h>

// ═══════════════════════════════════════════════════════════════════
// MCP Fuel Station V2 — Sensors
// Flow, tank full/detect, battery voltage
// ═══════════════════════════════════════════════════════════════════

// ── Flow sensor calibration ──────────────────────────────────────
#define FILL_PULSES_DEFAULT  1696.0f
#define DRAIN_PULSES_DEFAULT 1696.0f
#define HZ_PER_LPM           57.0f

extern volatile uint32_t fillPulses;
extern volatile uint32_t drainPulses;
extern float fillPulsesPerLiter;
extern float drainPulsesPerLiter;

// ── Battery ───────────────────────────────────────────────────────
// 100k/33k divider: Vbat = Vadc * (100k+33k)/33k = Vadc * 4.030
#define BATT_DIVIDER_RATIO  4.030f
#define BATT_ADC_VREF       3.3f
#define BATT_ADC_BITS       12      // ESP32-S3 12-bit ADC
#define BATT_FILTER_ALPHA   0.05f   // heavy filtering — voltage changes slowly

extern float filteredBattV;
extern int   cellCount;

// LiPo % lookup — ported directly from V1
int  Sensors_LiPoPct(float vPerCell);
void Sensors_DetectCellCount(float packV);

// ── Tank detection ────────────────────────────────────────────────
// GPIO7 reads LOW when sensor shell detect wire is connected to GND
// GPIO6 reads tank full signal (active high after divider)
#define TANK_FULL_ACTIVE_HIGH  1
#define TANK_DETECT_CONNECTED  LOW   // pulled LOW when sensor shell GND present

// ── API ───────────────────────────────────────────────────────────
void Sensors_Init();
void Sensors_Update();          // call every loop — non-blocking

bool Sensors_IsTankFull();
bool Sensors_IsTankSensorFitted();
float Sensors_BattVoltage();
int   Sensors_BattPct();
