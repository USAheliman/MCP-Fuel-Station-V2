#include "Sensors.h"
#include "../../include/pins.h"
#include <Wire.h>

// ═══════════════════════════════════════════════════════════════════
// MCP Fuel Station V2 — Sensors Implementation
// ═══════════════════════════════════════════════════════════════════

volatile uint32_t fillPulses  = 0;
volatile uint32_t drainPulses = 0;
float fillPulsesPerLiter  = FILL_PULSES_DEFAULT;
float drainPulsesPerLiter = DRAIN_PULSES_DEFAULT;

float filteredBattV   = 0.0f;
int   cellCount       = 0;

static bool battFilterInit = false;

// ── Flow ISRs ─────────────────────────────────────────────────────
static void IRAM_ATTR FillFlowISR()  { fillPulses++;  }
static void IRAM_ATTR DrainFlowISR() { drainPulses++; }

// ── LiPo % lookup (ported 1:1 from V1) ──────────────────────────
int Sensors_LiPoPct(float vPerCell)
{
    static const float V[] = {
        4.20f, 4.15f, 4.11f, 4.08f, 4.02f, 3.98f, 3.95f, 3.91f,
        3.87f, 3.85f, 3.84f, 3.82f, 3.80f, 3.78f, 3.75f, 3.73f, 3.70f
    };
    static const int P[] = {
        100, 95, 90, 85, 80, 75, 70, 60,
         50, 45, 40, 30, 20, 15, 10,  5,  0
    };
    const int N = sizeof(P) / sizeof(P[0]);
    if (vPerCell >= V[0])   return 100;
    if (vPerCell <= V[N-1]) return 0;
    for (int i = 0; i < N - 1; i++) {
        if (vPerCell <= V[i] && vPerCell >= V[i+1]) {
            float t   = (vPerCell - V[i+1]) / (V[i] - V[i+1]);
            float pct = P[i+1] + t * (P[i] - P[i+1]);
            return constrain((int)(pct + 0.5f), 0, 100);
        }
    }
    return 0;
}

void Sensors_DetectCellCount(float packV)
{
    if (cellCount != 0) return;
    if (packV < 4.0f) return;  // ADC not settled yet — wait for valid reading
    if      (packV >= 9.0f)  cellCount = 3;
    else if (packV >= 6.0f)  cellCount = 2;
    else                     cellCount = 1;
}

// ── I2C bus scanner ─────────────────────────────────────────────
static void i2cScan()
{
    Serial.println("I2C bus scan:");
    int found = 0;
    for (uint8_t addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        uint8_t err = Wire.endTransmission();
        if (err == 0) {
            Serial.printf("  0x%02X: device found\n", addr);
            found++;
        }
    }
    if (found == 0) Serial.println("  (no devices found)");
}

// ── I2C bus recovery ─────────────────────────────────────────────
// Releases any device holding SDA/SCL low by clocking SCL 9 times,
// then issuing a STOP condition — standard I2C stuck-bus recovery.
static void i2cBusRecover()
{
    pinMode(PIN_I2C_SCL, OUTPUT);
    pinMode(PIN_I2C_SDA, INPUT);
    for (int i = 0; i < 9; i++) {
        digitalWrite(PIN_I2C_SCL, HIGH); delayMicroseconds(5);
        digitalWrite(PIN_I2C_SCL, LOW);  delayMicroseconds(5);
    }
    // STOP condition: SDA low → SCL high → SDA high
    pinMode(PIN_I2C_SDA, OUTPUT);
    digitalWrite(PIN_I2C_SDA, LOW);
    delayMicroseconds(5);
    digitalWrite(PIN_I2C_SCL, HIGH); delayMicroseconds(5);
    digitalWrite(PIN_I2C_SDA, HIGH); delayMicroseconds(5);
    // Release both lines
    pinMode(PIN_I2C_SDA, INPUT);
    pinMode(PIN_I2C_SCL, INPUT);
}

// ── Init ─────────────────────────────────────────────────────────
void Sensors_Init()
{
    // Flow sensors
    pinMode(PIN_FILL_FLOW,   INPUT_PULLUP);
    pinMode(PIN_DRAIN_FLOW,  INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(PIN_FILL_FLOW),  FillFlowISR,  RISING);
    attachInterrupt(digitalPinToInterrupt(PIN_DRAIN_FLOW), DrainFlowISR, RISING);

    // Tank sensors
    pinMode(PIN_TANK_FULL,   INPUT);       // external divider — no internal pull
    pinMode(PIN_TANK_DETECT, INPUT);       // 15k external pull-up to 3.3V

    // Battery ADC
    analogReadResolution(BATT_ADC_BITS);
    analogSetAttenuation(ADC_11db);        // full 0-3.3V range

    // I2C — DS3231 (RTC)
    i2cBusRecover();
    delay(50);
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    Wire.setClock(400000);
    Wire.setTimeout(10);

    // Scan the bus to see all devices
    i2cScan();

    Serial.println("Sensors: init OK");
}

// ── Update (call every loop) ─────────────────────────────────────
void Sensors_Update()
{
    // Battery — read and filter at ~10Hz
    static uint32_t lastBattMs = 0;
    uint32_t now = millis();
    if (now - lastBattMs >= 100) {
        lastBattMs = now;
        int raw  = analogRead(PIN_BATT_ADC);
        float vadc = (raw / 4095.0f) * BATT_ADC_VREF;
        float vbat = vadc * BATT_DIVIDER_RATIO;
        if (!battFilterInit) { filteredBattV = vbat; battFilterInit = true; }
        else filteredBattV += BATT_FILTER_ALPHA * (vbat - filteredBattV);
        Sensors_DetectCellCount(filteredBattV);
    }

}

bool Sensors_IsTankFull()
{
    int raw    = digitalRead(PIN_TANK_FULL);
    bool active = (TANK_FULL_ACTIVE_HIGH ? (raw == HIGH) : (raw == LOW));
    static uint8_t count = 0;
    if (active) { if (count < 255) count++; }
    else count = 0;
    return (count >= 2);
}

bool Sensors_IsTankSensorFitted()
{
    // GPIO7 is pulled HIGH by 15k. LOW means detect wire (pin4 of CN3)
    // is connected to GND inside the sensor shell.
    return (digitalRead(PIN_TANK_DETECT) == TANK_DETECT_CONNECTED);
}

float Sensors_BattVoltage()  { return filteredBattV; }

int Sensors_BattPct()
{
    if (cellCount <= 0) return 0;
    return Sensors_LiPoPct(filteredBattV / (float)cellCount);
}
