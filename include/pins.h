#pragma once

// ═══════════════════════════════════════════════════════════════════
// MCO Fuel Station V2 — Pin Definitions
// ESP32-S3 DevKitC-1 N16R8
// Circuit Diagram Rev 2.0
// ═══════════════════════════════════════════════════════════════════

// ── Power management ─────────────────────────────────────────────────
#define PIN_POLOLU_A_GPIO   16   // GPIO16 → D3 1N4148 → Pololu A pin (OR gate)
#define PIN_POLOLU_OFF      17   // Pulse HIGH = forced shutdown
// Note: encoder SW → D2 → same Pololu A pin (OR gate)
// Short press = firmware function via GPIO16 timing
// Long press 3s+ = firmware pulses GPIO17 HIGH

// ── Battery voltage ADC ──────────────────────────────────────────────
#define PIN_BATT_ADC         1   // 100k/33k divider → max 3.13V at 12.6V
#define BATT_R_SERIES    100000  // 100k series resistor
#define BATT_R_SHUNT      33000  // 33k shunt resistor

// ── BTS7960 motor driver ─────────────────────────────────────────────
#define PIN_PUMP_RPWM        2   // Fill direction PWM
#define PIN_PUMP_REN         8   // Right enable — hold HIGH
#define PIN_PUMP_LPWM        9   // Drain direction PWM
#define PIN_PUMP_LEN        10   // Left enable — hold HIGH

// ── Audio ─────────────────────────────────────────────────────────────
#define PIN_BUZZER           3   // Passive piezo — ledcWriteTone()
#define BUZZER_LEDC_CHANNEL  0

// ── Flow sensors (5V → 3.3V via 7.5k/15k divider) ───────────────────
#define PIN_FILL_FLOW        4   // Fill flow sensor pulse
#define PIN_DRAIN_FLOW       5   // Drain flow sensor pulse

// ── Tank sensors ─────────────────────────────────────────────────────
#define PIN_TANK_FULL        6   // Tank full sensor (7.5k/15k divider)
#define PIN_TANK_DETECT      7   // Tank sensor detect pin4 (15k pull-up to 3.3V)
                                 // Reads LOW when sensor shell GND wire connected

// ── TFT display ST7735S (SPI2) ───────────────────────────────────────
#define PIN_TFT_SCK         11   // SPI2 clock
#define PIN_TFT_MOSI        12   // SPI2 MOSI
#define PIN_TFT_CS          13   // Chip select (active LOW)
#define PIN_TFT_DC          14   // Data/Command
#define PIN_TFT_RST         15   // Reset (active LOW)
#define PIN_TFT_BL          48   // Backlight PWM
#define TFT_BL_LEDC_CHANNEL  1

// ── I2C bus (shared DS3231 + ABP2) ───────────────────────────────────
#define PIN_I2C_SDA         18
#define PIN_I2C_SCL         39
#define I2C_ADDR_RTC      0x68   // DS3231 RTC
#define I2C_ADDR_PRESSURE 0x28   // Honeywell ABP2 0-4 bar

// ── Rotary encoder KY-040 ────────────────────────────────────────────
#define PIN_ENC_CLK         40   // CLK — interrupt driven
#define PIN_ENC_DT          21   // DT  — direction
// PIN_POLOLU_A_GPIO (16) doubles as encoder SW timing input

// ── Reserved / do not use ────────────────────────────────────────────
// GPIO19/20 — USB D-/D+
// GPIO26-33 — internal flash
// GPIO35-37 — PSRAM
// GPIO45/46 — strapping pins

// ── Spare ─────────────────────────────────────────────────────────────
// GPIO34, GPIO38, GPIO41, GPIO42, GPIO47
