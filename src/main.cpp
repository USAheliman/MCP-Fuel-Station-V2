#include <Arduino.h>
#include <Wire.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <ESP32Encoder.h>

#include "pins.h"
#include "version.h"

#include "heli/HeliLib.h"
#include "sensors/Sensors.h"
#include "pump/Pump.h"
#include "display/Display.h"
#include "ui/Power.h"
#include "web/WebServer.h"

// ═══════════════════════════════════════════════════════════════════
// MCP Fuel Station V2 — main.cpp
// ESP32-S3 N16R8 — single board
// All V1 Teensy logic ported here, Nextion replaced by TFT + encoder
// ═══════════════════════════════════════════════════════════════════

// ── Station config (ported from V1) ──────────────────────────────
#define SUPPLY_TANK_DEFAULT_ML  20000
#define SUPPLY_LOW_DEFAULT_ML    2000

int   supplyTankCapacityMl  = SUPPLY_TANK_DEFAULT_ML;
int   supplyTankRemainingMl = SUPPLY_TANK_DEFAULT_ML;
int   supplyAtSessionStartMl= SUPPLY_TANK_DEFAULT_ML;
int   supplyLowThresholdMl  = SUPPLY_LOW_DEFAULT_ML;

// ── Flow session state ────────────────────────────────────────────
int lastFillVolumeMl  = 0;
int lastDrainVolumeMl = 0;
int targetFillMl      = 0;
int targetDrainMl     = 0;

// ── Tank empty detection (ported from V1) ─────────────────────────
#define TANK_EMPTY_FLOW_DROP_DEFAULT  30
#define TANK_EMPTY_MIN_RUN_MS_DEFAULT 8000
#define TANK_EMPTY_CONFIRM_COUNT       4
#define TANK_EMPTY_MIN_PEAK_FLOW     200

int      tankEmptyFlowDropPct = TANK_EMPTY_FLOW_DROP_DEFAULT;
uint32_t tankEmptyMinRunMs    = TANK_EMPTY_MIN_RUN_MS_DEFAULT;
int      drainPeakFlowMlMin   = 0;
uint32_t drainStartMs         = 0;
uint8_t  tankEmptyCount       = 0;

// ── Auto drain-then-fill sequence ────────────────────────────────
enum AutoFillState { AF_NONE, AF_DRAIN_PENDING, AF_DRAINING, AF_FILLING, AF_PURGING };
AutoFillState autoFillSequence = AF_NONE;
uint32_t autoFillTransitionMs  = 0;
uint32_t purgeStartMs          = 0;
#define AUTO_FILL_PAUSE_MS 2000

// ── Low battery latch ─────────────────────────────────────────────
float cutoffVPerCell   = 3.82f;
bool  lowBatteryLatched = false;
#define SAG_TRIP_COUNT    2
#define SAG_HYST_PER_CELL 0.05f
static uint8_t lowBattCount = 0;

// ── Display data ──────────────────────────────────────────────────
static GaugeData gDisplay;

// ── Message helpers ───────────────────────────────────────────────
static void SetMessage(const char* msg, uint16_t colour)
{
    strncpy(gDisplay.message, msg, sizeof(gDisplay.message) - 1);
    gDisplay.msgColour = colour;
}

// ── Flow UI state (ported from V1 FlowUiState) ───────────────────
struct FlowUiState {
    uint32_t lastMs     = 0;
    uint32_t lastPulses = 0;
    int      lastSentFlow = -1;
    int      lastSentVol  = -1;
};
static FlowUiState gFillUi;
static FlowUiState gDrainUi;

static void ResetFlowUi(FlowUiState &s)
{
    s.lastMs      = 0;
    s.lastPulses  = 0;
    s.lastSentFlow= -1;
    s.lastSentVol = -1;
}

// ── Calibration ───────────────────────────────────────────────────
bool     fillCalActive    = false;
bool     drainCalActive   = false;
uint32_t fillCalStartMs   = 0;
uint32_t drainCalStartMs  = 0;

// ── Station config save/load ──────────────────────────────────────
void SaveStationToFS()
{
    File f = LittleFS.open("/station.json", "w");
    if (!f) return;
    String j = "{";
    j += "\"supCap\":"   + String(supplyTankCapacityMl)  + ",";
    j += "\"supRem\":"   + String(supplyTankRemainingMl) + ",";
    j += "\"supLow\":"   + String(supplyLowThresholdMl)  + ",";
    j += "\"fillPpl\":"  + String((int)(fillPulsesPerLiter  * 10)) + ",";
    j += "\"drainPpl\":" + String((int)(drainPulsesPerLiter * 10)) + ",";
    j += "\"mlPpwm\":"   + String((int)(mlPerMinPerPwm        * 100)) + ",";
    j += "\"dmlPpwm\":"  + String((int)(drainMlPerMinPerPwm   * 100)) + ",";
    j += "\"emptyDrop\":" + String(tankEmptyFlowDropPct)  + ",";
    j += "\"emptyDelay\":" + String((int)(tankEmptyMinRunMs / 1000)) + ",";
    j += "\"cutoff\":"   + String((int)(cutoffVPerCell * 100));
    j += "}";
    f.print(j);
    f.close();
    Serial.println("Station: saved");
}

void LoadStationFromFS()
{
    if (!LittleFS.exists("/station.json")) return;
    File f = LittleFS.open("/station.json", "r");
    if (!f) return;

    JsonDocument doc;
    if (deserializeJson(doc, f) != DeserializationError::Ok) { f.close(); return; }
    f.close();

    supplyTankCapacityMl  = doc["supCap"]  | SUPPLY_TANK_DEFAULT_ML;
    supplyTankRemainingMl = doc["supRem"]  | SUPPLY_TANK_DEFAULT_ML;
    supplyLowThresholdMl  = doc["supLow"]  | SUPPLY_LOW_DEFAULT_ML;

    int fillX10  = doc["fillPpl"]  | 0;
    int drainX10 = doc["drainPpl"] | 0;
    if (fillX10  > 0) fillPulsesPerLiter  = fillX10  / 10.0f;
    if (drainX10 > 0) drainPulsesPerLiter = drainX10 / 10.0f;

    int mX100  = doc["mlPpwm"]  | 0;
    int dmX100 = doc["dmlPpwm"] | 0;
    if (mX100  > 0) mlPerMinPerPwm       = mX100  / 100.0f;
    if (dmX100 > 0) drainMlPerMinPerPwm  = dmX100 / 100.0f;

    tankEmptyFlowDropPct = doc["emptyDrop"]  | TANK_EMPTY_FLOW_DROP_DEFAULT;
    int ds = doc["emptyDelay"] | 8;
    tankEmptyMinRunMs    = (uint32_t)constrain(ds, 1, 60) * 1000;

    int cutX100 = doc["cutoff"] | 0;
    if (cutX100 > 0) cutoffVPerCell = constrain(cutX100 / 100.0f, 3.0f, 4.0f);

    Serial.printf("Station: loaded  supply=%d/%d ml\n",
                  supplyTankRemainingMl, supplyTankCapacityMl);
}

// ── Forward declarations ──────────────────────────────────────────
void BeginOverflowPurge();
void BeginDrain();

// ── Fill / Drain session begin ────────────────────────────────────
void BeginFill()
{
    if (lowBatteryLatched) return;
    if (heliModels[activeModelIndex].hasTankSensor && Sensors_IsTankFull()) return;

    noInterrupts(); fillPulses = 0; interrupts();
    lastFillVolumeMl       = 0;
    supplyAtSessionStartMl = supplyTankRemainingMl;
    targetFillMl           = heliModels[activeModelIndex].tankVolumeMl;
    ResetFlowUi(gFillUi);

    closedLoopTargetMlMin = heliModels[activeModelIndex].fillSpeed;
    Pump_ResetFillLoop();
    closedLoopActive      = true;

    Pump_Enable();
    PumpEnabled = true;
    Pump_SetTarget(+MIN_PWM);

    SetMessage("Filling", MSG_FILLING);
}

void BeginDrain()
{
    if (lowBatteryLatched) return;

    noInterrupts(); drainPulses = 0; interrupts();
    lastDrainVolumeMl      = 0;
    targetDrainMl          = heliModels[activeModelIndex].tankVolumeMl;
    supplyAtSessionStartMl = supplyTankRemainingMl;
    drainPeakFlowMlMin     = 0;
    tankEmptyCount         = 0;
    drainStartMs           = millis();
    ResetFlowUi(gDrainUi);

    drainClosedLoopTargetMlMin = heliModels[activeModelIndex].drainSpeed;
    Pump_ResetDrainLoop();
    drainClosedLoopActive = true;

    Pump_Enable();
    PumpEnabled = true;
    Pump_SetTarget(-MIN_PWM);

    SetMessage("Draining", MSG_DRAINING);
}

void StopFill()
{
    Pump_Stop();
    SaveStationToFS();
    SetMessage("Stopped", MSG_IDLE);
    autoFillSequence = AF_NONE;
}

// ── Flow update (ported from V1 UpdateFillUiAndStops) ────────────
static void UpdateFillFlow(uint32_t now)
{
    noInterrupts();
    uint32_t p = fillPulses;
    interrupts();

    if (gFillUi.lastMs == 0) { gFillUi.lastMs = now - 500; gFillUi.lastPulses = 0; }
    if (now - gFillUi.lastMs < 500) return;

    float dt = (now - gFillUi.lastMs) / 1000.0f;
    gFillUi.lastMs = now;
    uint32_t dp    = p - gFillUi.lastPulses;
    gFillUi.lastPulses = p;

    float hz          = (dt > 0.0f) ? (dp / dt) : 0.0f;
    int   flowMlMin   = (int)(hz / HZ_PER_LPM * 1000.0f + 0.5f);
    int   volumeMl    = (int)((float)p / fillPulsesPerLiter * 1000.0f + 0.5f);

    lastFillVolumeMl = volumeMl;
    Pump_UpdateFillClosedLoop(flowMlMin);

    gDisplay.flowMlMin = flowMlMin;
    gDisplay.volumeMl  = volumeMl;
    gDisplay.targetMl  = targetFillMl;
    gDisplay.mainTankPct = targetFillMl > 0
        ? constrain((int)(100.0f * volumeMl / targetFillMl), 0, 100) : 0;

    supplyTankRemainingMl = supplyAtSessionStartMl - volumeMl;
    gDisplay.outerTankPct = supplyTankCapacityMl > 0
        ? constrain((int)(100.0f * supplyTankRemainingMl / supplyTankCapacityMl), 0, 100) : 0;
    gDisplay.pumpSpeedPct = MAX_PWM > MIN_PWM
        ? constrain((int)(100.0f * closedLoopCurrentPwm / MAX_PWM), 0, 100) : 0;
    gDisplay.pressurePct  = constrain((int)(Sensors_PressureBar() / 4.0f * 100.0f), 0, 100);

    // Auto-stop conditions
    if (PumpEnabled && autoFillSequence != AF_PURGING) {
        if (targetFillMl > 0 && lastFillVolumeMl >= targetFillMl) {
            Pump_Stop(); BeginOverflowPurge(); return;
        }
        if (heliModels[activeModelIndex].hasTankSensor && Sensors_IsTankFull()) {
            Pump_Stop(); BeginOverflowPurge(); return;
        }
    }
}

// ── Overflow purge (ported from V1) ──────────────────────────────
void BeginOverflowPurge()
{
    int secs = heliModels[activeModelIndex].purgeSecs;
    if (secs <= 0) {
        heliModels[activeModelIndex].totalFills++;
        heliModels[activeModelIndex].totalFillMl += (uint32_t)lastFillVolumeMl;
        HeliLib_Save(activeModelIndex);
        SetMessage("Complete", MSG_COMPLETE);
        autoFillSequence = AF_NONE;
        return;
    }
    autoFillSequence = AF_PURGING;
    purgeStartMs     = millis();
    noInterrupts(); drainPulses = 0; interrupts();
    int purgeSpd = drainMlPerMinPerPwm > 0.0f
        ? Pump_DrainMlMinToPwm(heliModels[activeModelIndex].drainSpeed) : MIN_PWM;
    Pump_Enable();
    PumpEnabled = true;
    Pump_SetTarget(-purgeSpd);
    SetMessage("Purging...", MSG_DRAINING);
}

// ── Drain flow update ─────────────────────────────────────────────
static void UpdateDrainFlow(uint32_t now)
{
    noInterrupts();
    uint32_t p = drainPulses;
    interrupts();

    if (gDrainUi.lastMs == 0) { gDrainUi.lastMs = now - 500; gDrainUi.lastPulses = 0; }
    if (now - gDrainUi.lastMs < 500) return;

    float dt = (now - gDrainUi.lastMs) / 1000.0f;
    gDrainUi.lastMs = now;
    uint32_t dp     = p - gDrainUi.lastPulses;
    gDrainUi.lastPulses = p;

    float hz        = (dt > 0.0f) ? (dp / dt) : 0.0f;
    int   flowMlMin = (int)(hz / HZ_PER_LPM * 1000.0f + 0.5f);
    int   volumeMl  = (int)((float)p / drainPulsesPerLiter * 1000.0f + 0.5f);

    lastDrainVolumeMl = volumeMl;
    Pump_UpdateDrainClosedLoop(flowMlMin);

    gDisplay.flowMlMin = flowMlMin;
    gDisplay.volumeMl  = volumeMl;
    gDisplay.targetMl  = targetDrainMl;

    int drainRef = targetDrainMl > 0 ? targetDrainMl : heliModels[activeModelIndex].tankVolumeMl;
    gDisplay.mainTankPct = drainRef > 0
        ? constrain(100 - (int)(100.0f * volumeMl / drainRef), 0, 100) : 0;

    supplyTankRemainingMl = constrain(supplyAtSessionStartMl + volumeMl, 0, supplyTankCapacityMl);
    gDisplay.outerTankPct = supplyTankCapacityMl > 0
        ? constrain((int)(100.0f * supplyTankRemainingMl / supplyTankCapacityMl), 0, 100) : 0;

    // Tank empty detection (ported from V1)
    if (PumpEnabled && flowMlMin > drainPeakFlowMlMin) {
        drainPeakFlowMlMin = flowMlMin;
        tankEmptyCount     = 0;
    }
    bool gated = !drainClosedLoopActive || drainClosedLoopHasSettled
                 || (drainClosedLoopCurrentPwm >= (int)(MAX_PWM * 0.5f));
    if (PumpEnabled && (millis() - drainStartMs) >= tankEmptyMinRunMs && gated) {
        if (drainPeakFlowMlMin < TANK_EMPTY_MIN_PEAK_FLOW) {
            Pump_Stop();
            heliModels[activeModelIndex].totalDrains++;
            heliModels[activeModelIndex].totalDrainMl += (uint32_t)lastDrainVolumeMl;
            HeliLib_Save(activeModelIndex);
            SetMessage("Tank was empty", MSG_WARN);
            return;
        }
        int thresh = drainPeakFlowMlMin * (100 - tankEmptyFlowDropPct) / 100;
        if (flowMlMin < thresh) {
            tankEmptyCount++;
            if (tankEmptyCount >= TANK_EMPTY_CONFIRM_COUNT) {
                Pump_Stop();
                heliModels[activeModelIndex].totalDrains++;
                heliModels[activeModelIndex].totalDrainMl += (uint32_t)lastDrainVolumeMl;
                HeliLib_Save(activeModelIndex);
                if (autoFillSequence == AF_DRAINING) {
                    autoFillTransitionMs = millis();
                    SetMessage("Drain done — filling...", MSG_IDLE);
                } else {
                    SetMessage("Tank empty", MSG_COMPLETE);
                    autoFillSequence = AF_NONE;
                }
            }
        } else tankEmptyCount = 0;
    }
}

// ── Auto sequence update ──────────────────────────────────────────
static void UpdateAutoSequence(uint32_t now)
{
    if (autoFillSequence == AF_DRAIN_PENDING && now - autoFillTransitionMs >= 500) {
        autoFillSequence = AF_DRAINING;
        BeginDrain();
    }
    if (autoFillSequence == AF_DRAINING && !PumpEnabled && autoFillTransitionMs > 0) {
        if (now - autoFillTransitionMs >= AUTO_FILL_PAUSE_MS) {
            autoFillTransitionMs = 0;
            autoFillSequence     = AF_FILLING;
            noInterrupts(); fillPulses = 0; interrupts();
            lastFillVolumeMl       = 0;
            supplyAtSessionStartMl = supplyTankRemainingMl;
            ResetFlowUi(gFillUi);
            BeginFill();
        }
    }
    if (autoFillSequence == AF_FILLING && !PumpEnabled) BeginOverflowPurge();
    if (autoFillSequence == AF_PURGING && PumpEnabled) {
        if ((now - purgeStartMs) >= (uint32_t)(heliModels[activeModelIndex].purgeSecs * 1000)) {
            Pump_Stop();
            autoFillSequence = AF_NONE;
            heliModels[activeModelIndex].totalFills++;
            heliModels[activeModelIndex].totalFillMl += (uint32_t)lastFillVolumeMl;
            HeliLib_Save(activeModelIndex);
            SetMessage("Complete", MSG_COMPLETE);
        }
    }
}

// ── Battery check ─────────────────────────────────────────────────
static void UpdateBattery()
{
    static uint32_t lastMs = 0;
    if (millis() - lastMs < 500) return;
    lastMs = millis();

    float packV   = Sensors_BattVoltage();
    int   cellCnt = cellCount;
    if (cellCnt <= 0) return;
    float vCell = packV / (float)cellCnt;

    gDisplay.battPct = Sensors_BattPct();

    if (!lowBatteryLatched) {
        if (vCell <= cutoffVPerCell) { if (lowBattCount < 255) lowBattCount++; }
        else if (vCell >= cutoffVPerCell + SAG_HYST_PER_CELL) lowBattCount = 0;
        if (lowBattCount >= SAG_TRIP_COUNT) {
            lowBatteryLatched = true;
            Pump_Stop();
            SetMessage("LOW BATTERY!", MSG_WARN);
            Serial.printf("Low battery: %.2fV/cell\n", vCell);
        }
    }
}

// ── Rotary encoder ────────────────────────────────────────────────
static ESP32Encoder encoder;
static int32_t       lastEncPos = 0;

static void encoderPoll()
{
    int32_t pos = (int32_t)encoder.getCount();
    if (pos == lastEncPos) return;

    static int32_t  acc = 0;
    static uint32_t lastScrollMs = 0;
    uint32_t now = millis();

    acc += (pos - lastEncPos);
    lastEncPos = pos;

    // attachHalfQuad = 2 raw counts per physical detent; wait for a full detent
    if (acc > -2 && acc < 2) return;
    if (now - lastScrollMs < 50) { acc = 0; return; }

    lastScrollMs = now;
    Power_UpdateActivity();
    Display_EncoderScroll(acc > 0 ? 1 : -1);
    acc = 0;
}

// ── Power button short press ──────────────────────────────────────
void OnShortPress()
{
    // Cycle to next screen
    DisplayScreen next = (DisplayScreen)(((int)Display_CurrentScreen() + 1) % SCREEN_COUNT);
    Display_SetScreen(next);
    Power_UpdateActivity();
}

// ── Shutdown callback — save before power cut ─────────────────────
void OnShutdown()
{
    HeliLib_SaveAll();
    SaveStationToFS();
    Serial.println("Shutdown: saved all");
}

// ── Buzzer tones ──────────────────────────────────────────────────
static void BuzzerTone(uint32_t freqHz, uint32_t durationMs)
{
    ledcSetup(BUZZER_LEDC_CHANNEL, freqHz, 8);
    ledcAttachPin(PIN_BUZZER, BUZZER_LEDC_CHANNEL);
    ledcWrite(BUZZER_LEDC_CHANNEL, 128);
    delay(durationMs);
    ledcWrite(BUZZER_LEDC_CHANNEL, 0);
}

static void BuzzerStartup()
{
    BuzzerTone(880, 80); delay(40);
    BuzzerTone(1320, 120);
}

// ─────────────────────────────────────────────────────────────────
// SETUP
// ─────────────────────────────────────────────────────────────────
void setup()
{
    Serial.begin(115200);
    delay(300);
    Serial.printf("\nMCP Fuel Station V2  %s  %s %s\n",
                  FW_VERSION, FW_BUILD_DATE, FW_BUILD_TIME);

    HeliLib_Init();
    HeliLib_Load();
    LoadStationFromFS();

    Sensors_Init();
    Pump_Init();
    Display_Init();
    Power_Init(OnShortPress, OnShutdown);
    WebServer_Init();

    // Push network IPs to display (shown on SCREEN_NET, screen 3)
    {
        String staIP = WebServer_GetLocalIP();
        String apIP  = WiFi.softAPIP().toString();
        Display_SetNetworkIP(staIP.c_str(), apIP.c_str());

        // Show IP in the message bar for first 6 seconds after boot
        char ipMsg[40];
        snprintf(ipMsg, sizeof(ipMsg), "OTA: %s/ota", staIP.c_str());
        SetMessage(ipMsg, MSG_IDLE);
    }

    ESP32Encoder::useInternalWeakPullResistors = puType::UP;
    encoder.attachHalfQuad(PIN_ENC_DT, PIN_ENC_CLK);
    encoder.setCount(0);
    lastEncPos = 0;

    BuzzerStartup();

    strncpy(gDisplay.heliName, heliModels[activeModelIndex].name,
            sizeof(gDisplay.heliName) - 1);
    gDisplay.battPct = Sensors_BattPct();

    Display_SetScreen(SCREEN_GAUGE);
    Serial.println("Setup complete — entering loop");
}

// ─────────────────────────────────────────────────────────────────
// LOOP
// ─────────────────────────────────────────────────────────────────
void loop()
{
    uint32_t now = millis();

    encoderPoll();
    Sensors_Update();
    Pump_UpdateRamp();

    // Flow tracking
    if (PumpEnabled && autoFillSequence != AF_PURGING) {
        if (closedLoopActive)      UpdateFillFlow(now);
        if (drainClosedLoopActive) UpdateDrainFlow(now);
    }

    UpdateAutoSequence(now);
    UpdateBattery();
    Power_Update();

    // Update display data from active model
    strncpy(gDisplay.heliName, heliModels[activeModelIndex].name,
            sizeof(gDisplay.heliName) - 1);
    gDisplay.pumpRunning  = PumpEnabled;
    gDisplay.sensorFitted = Sensors_IsTankSensorFitted();
    gDisplay.tankFull     = Sensors_IsTankFull();

    // Clear startup IP message after 6 s; then show normal idle text
    static bool ipMsgCleared = false;
    if (!ipMsgCleared && now >= 6000) {
        ipMsgCleared = true;
        SetMessage("MCP Fuel Station V2", MSG_IDLE);
    }

    // Refresh TFT at ~10Hz
    static uint32_t lastDisplayMs = 0;
    if (now - lastDisplayMs >= 100) {
        lastDisplayMs = now;
        Display_Update(gDisplay);
    }

    // Broadcast WebSocket state at ~4Hz
    static uint32_t lastWsMs = 0;
    if (now - lastWsMs >= 250) {
        lastWsMs = now;
        // Build state JSON (same structure as V1 BroadcastStateToESP32)
        char json[512];
        HeliModel &m = heliModels[activeModelIndex];
        int supplyPct = supplyTankCapacityMl > 0
            ? (int)(100.0f * supplyTankRemainingMl / supplyTankCapacityMl) : 0;
        snprintf(json, sizeof(json),
            "{"
            "\"version\":\"%s\","
            "\"modelName\":\"%s\","
            "\"tankVol\":%d,"
            "\"sensor\":\"%s\","
            "\"supplyPct\":%d,"
            "\"supplyMl\":%d,"
            "\"fillFlow\":%d,"
            "\"fillVol\":%d,"
            "\"fillTarget\":%d,"
            "\"pumpOn\":%s,"
            "\"battPct\":%d,"
            "\"pressureBar\":\"%.2f\","
            "\"message\":\"%s\","
            "\"activeModel\":%d"
            "}",
            FW_VERSION,
            m.name,
            m.tankVolumeMl,
            m.hasTankSensor ? "YES" : "NO",
            supplyPct,
            supplyTankRemainingMl,
            gDisplay.flowMlMin,
            lastFillVolumeMl,
            targetFillMl,
            PumpEnabled ? "true" : "false",
            gDisplay.battPct,
            Sensors_PressureBar(),
            gDisplay.message,
            activeModelIndex
        );
        WebServer_BroadcastState(String(json));
    }

    WebServer_Update();
}
