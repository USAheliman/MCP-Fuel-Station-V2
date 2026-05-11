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
#include "screen/Screen.h"
#include "rtc/RTC.h"
#include "ui/Power.h"
#include "web/WebServer.h"

// ═══════════════════════════════════════════════════════════════════
// MCP Fuel Station V2 — main.cpp
// ESP32-S3 N16R8 — single board
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
#define SAG_TRIP_COUNT    6      // 6 × 500 ms = 3 s sustained low voltage
#define SAG_HYST_PER_CELL 0.05f
static uint8_t lowBattCount = 0;

// ── Screen data ───────────────────────────────────────────────────
static ScreenData gDisplay;
static bool gLastPumpWasDrain = false;   // for post-pump START restart

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
static uint32_t fillCalSnapshot  = 0;
static uint32_t drainCalSnapshot = 0;

// ── Web command state ─────────────────────────────────────────────
static int pendingWebCmd = 0;

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
void BeginFill();
void BeginDrain();
void StopFill();
void BeginOverflowPurge();

// ── JSON string escape helper ─────────────────────────────────────
static String jStr(const char* s)
{
    String r = "\"";
    for (const char* p = s; *p; ++p) {
        if (*p == '"' || *p == '\\') r += '\\';
        r += *p;
    }
    return r + "\"";
}

// ── Message colour → CSS class ────────────────────────────────────
static const char* msgColorClass(uint16_t c)
{
    if (c == MSG_FILLING)  return "green";
    if (c == MSG_DRAINING) return "orange";
    if (c == MSG_WARN)     return "flash";
    if (c == MSG_COMPLETE) return "white";
    return "cyan";
}

// ── Build full WebSocket state JSON ──────────────────────────────
static String BuildWebStateJson()
{
    HeliModel& m    = heliModels[activeModelIndex];
    float packV     = Sensors_BattVoltage();
    int   cells     = max(1, cellCount);
    float cellV     = packV / (float)cells;
    int   supPct    = supplyTankCapacityMl > 0
                      ? constrain((int)(100.0f * supplyTankRemainingMl / supplyTankCapacityMl), 0, 100) : 0;
    bool  supLow    = supplyTankRemainingMl < supplyLowThresholdMl;
    int   fillPct   = m.tankVolumeMl > 0
                      ? constrain((int)(100.0f * lastFillVolumeMl  / m.tankVolumeMl), 0, 100) : 0;
    int   drainPct  = m.tankVolumeMl > 0
                      ? constrain((int)(100.0f * lastDrainVolumeMl / m.tankVolumeMl), 0, 100) : 0;

    bool isDraining = drainClosedLoopActive || autoFillSequence == AF_PURGING;
    int  fillFlow   = (!isDraining && closedLoopActive) ? gDisplay.flowMlMin : 0;
    int  drainFlow  = isDraining ? gDisplay.flowMlMin : 0;

    int page = lowBatteryLatched ? 4
             : (PumpEnabled && closedLoopActive)      ? 2
             : (PumpEnabled && drainClosedLoopActive) ? 3 : 1;

    noInterrupts();
    uint32_t fp = fillPulses, dp = drainPulses;
    interrupts();

    String j = "{";
    j += "\"version\":"      + jStr(FW_VERSION) + ",";
    j += "\"page\":"         + String(page)     + ",";
    j += "\"modelName\":"    + jStr(m.name)     + ",";
    j += "\"tankVol\":"      + String(m.tankVolumeMl) + ",";
    j += "\"sensor\":"       + jStr(m.hasTankSensor ? "YES" : "NO") + ",";
    j += "\"fillSpd\":"      + String(m.fillSpeed)    + ",";
    j += "\"drainSpd\":"     + String(m.drainSpeed)   + ",";
    j += "\"supplyPct\":"    + String(supPct)          + ",";
    j += "\"supplyLow\":"    + String(supLow ? "true" : "false") + ",";
    j += "\"supplyMl\":"     + String(supplyTankRemainingMl) + ",";
    j += "\"supplyCapMl\":"  + String(supplyTankCapacityMl)  + ",";
    j += "\"battPct\":"      + String(gDisplay.battPct) + ",";
    j += "\"battType\":"     + jStr((String(cells) + "S").c_str()) + ",";
    j += "\"packV\":"        + String(packV, 2)  + ",";
    j += "\"cellV\":"        + String(cellV, 2)  + ",";
    j += "\"cellCount\":"    + String(cells)     + ",";
    j += "\"fillFlow\":"     + String(fillFlow)  + ",";
    j += "\"fillVol\":"      + String(lastFillVolumeMl)  + ",";
    j += "\"fillTarget\":"   + String(targetFillMl)      + ",";
    j += "\"fillPct\":"      + String(fillPct)           + ",";
    j += "\"fillSpeedSlider\":" + String(closedLoopTargetMlMin > 0 ? closedLoopTargetMlMin : m.fillSpeed) + ",";
    j += "\"drainFlow\":"    + String(drainFlow)         + ",";
    j += "\"drainVol\":"     + String(lastDrainVolumeMl) + ",";
    j += "\"drainPct\":"     + String(drainPct)          + ",";
    j += "\"drainSpeedSlider\":" + String(drainClosedLoopTargetMlMin > 0 ? drainClosedLoopTargetMlMin : m.drainSpeed) + ",";
    j += "\"pumpOn\":"       + String(PumpEnabled ? "true" : "false") + ",";
    j += "\"message\":"      + jStr(gDisplay.message) + ",";
    j += "\"msgColor\":"     + jStr(msgColorClass(gDisplay.msgColour)) + ",";
    j += "\"lowBat\":"       + String(lowBatteryLatched ? "true" : "false") + ",";
    j += "\"activeModel\":"  + String(activeModelIndex)  + ",";
    j += "\"previewModel\":" + String(previewModelIndex) + ",";
    j += "\"fillCalPulses\":"  + String(fillCalActive  ? fp : 0) + ",";
    j += "\"drainCalPulses\":" + String(drainCalActive ? dp : 0) + ",";

    // Models list
    j += "\"models\":[";
    for (int i = 0; i < numModels; i++) {
        if (i > 0) j += ",";
        j += "{\"name\":"        + jStr(heliModels[i].name) + ",";
        j += "\"totalFills\":"   + String(heliModels[i].totalFills) + "}";
    }
    j += "],";

    // Setup stats for active model
    j += "\"setupStats\":{";
    j += "\"purge\":"       + String(m.purgeSecs)   + ",";
    j += "\"totalFills\":"  + String(m.totalFills)  + ",";
    j += "\"totalDrains\":" + String(m.totalDrains) + ",";
    j += "\"totalFillVol\":"  + String(m.totalFillMl  / 1000.0f, 1) + ",";
    j += "\"totalDrainVol\":" + String(m.totalDrainMl / 1000.0f, 1) + ",";
    j += "\"totalVol\":"      + String((m.totalFillMl + m.totalDrainMl) / 1000.0f, 1);
    j += "},";

    // Station-wide totals
    uint32_t stFills = 0, stDrains = 0, stFillMl = 0, stDrainMl = 0;
    for (int i = 0; i < numModels; i++) {
        stFills   += heliModels[i].totalFills;
        stDrains  += heliModels[i].totalDrains;
        stFillMl  += heliModels[i].totalFillMl;
        stDrainMl += heliModels[i].totalDrainMl;
    }
    j += "\"station\":{";
    j += "\"cap\":"         + jStr((String(supplyTankCapacityMl  / 1000.0f, 1) + "L").c_str()) + ",";
    j += "\"rem\":"         + jStr((String(supplyTankRemainingMl / 1000.0f, 1) + "L").c_str()) + ",";
    j += "\"low\":"         + jStr((String(supplyLowThresholdMl  / 1000.0f, 1) + "L").c_str()) + ",";
    j += "\"fillCal\":"     + jStr(String(fillPulsesPerLiter,  1).c_str()) + ",";
    j += "\"drainCal\":"    + jStr(String(drainPulsesPerLiter, 1).c_str()) + ",";
    j += "\"volume\":"      + jStr((String(supplyTankRemainingMl / 1000.0f, 1) + "L").c_str()) + ",";
    j += "\"flowDrop\":"    + jStr((String(tankEmptyFlowDropPct) + "%").c_str()) + ",";
    j += "\"emptyDelay\":"  + jStr((String((int)(tankEmptyMinRunMs / 1000)) + "s").c_str()) + ",";
    j += "\"cutoff\":"      + jStr((String(cutoffVPerCell, 2) + "V").c_str()) + ",";
    j += "\"totalFills\":"  + String(stFills)  + ",";
    j += "\"totalDrains\":" + String(stDrains) + ",";
    j += "\"fillVol\":"     + jStr((String(stFillMl  / 1000.0f, 1) + "L").c_str()) + ",";
    j += "\"drainVol\":"    + jStr((String(stDrainMl / 1000.0f, 1) + "L").c_str()) + ",";
    j += "\"netVol\":"      + jStr((String(stFillMl > stDrainMl ? (stFillMl - stDrainMl) / 1000.0f : 0.0f, 1) + "L").c_str()) + ",";
    j += "\"capMl\":"       + String(supplyTankCapacityMl)  + ",";
    j += "\"lowMl\":"       + String(supplyLowThresholdMl)  + ",";
    j += "\"fillCalRaw\":"  + String((int)(fillPulsesPerLiter  * 10)) + ",";
    j += "\"drainCalRaw\":" + String((int)(drainPulsesPerLiter * 10)) + ",";
    j += "\"flowDropRaw\":" + String(tankEmptyFlowDropPct)  + ",";
    j += "\"emptyDelayRaw\":" + String((int)(tankEmptyMinRunMs / 1000)) + ",";
    j += "\"cutoffRaw\":"   + String((int)(cutoffVPerCell * 100));
    j += "}";

    j += "}";
    return j;
}

// ── WebSocket command handler ─────────────────────────────────────
void OnWebCommand(const String& raw)
{
    if (!raw.startsWith("CMD:")) return;
    String body = raw.substring(4);

    // CMD:1000:N — speed slider (fill or drain depending on active pump)
    int colonPos = body.indexOf(':');
    if (colonPos >= 0) {
        int baseCmd = body.substring(0, colonPos).toInt();
        int val     = body.substring(colonPos + 1).toInt();
        if (baseCmd == 1000) {
            val = constrain(val, 50, 3000);
            if (drainClosedLoopActive) {
                drainClosedLoopTargetMlMin = val;
                heliModels[activeModelIndex].drainSpeed = val;
            } else {
                closedLoopTargetMlMin = val;
                heliModels[activeModelIndex].fillSpeed = val;
            }
        } else if (baseCmd == 8020) {
            if (val >= 0 && val < numModels) {
                activeModelIndex = val;
                HeliLib_SaveActiveIndex();
                strncpy(gDisplay.modelName, heliModels[activeModelIndex].name, sizeof(gDisplay.modelName) - 1);
            }
        }
        return;
    }

    int cmd = body.toInt();

    // Two-step: apply value to previously received parameter command
    if (pendingWebCmd != 0) {
        int pCmd = pendingWebCmd;
        pendingWebCmd = 0;
        switch (pCmd) {
            case 7010: supplyTankCapacityMl  = max(100, cmd); SaveStationToFS(); break;
            case 7012: supplyLowThresholdMl  = max(0,   cmd); SaveStationToFS(); break;
            case 7013: tankEmptyFlowDropPct  = constrain(cmd, 5, 90); SaveStationToFS(); break;
            case 7014: tankEmptyMinRunMs = (uint32_t)constrain(cmd, 1, 60) * 1000; SaveStationToFS(); break;
            case 7016: cutoffVPerCell = constrain(cmd / 100.0f, 3.0f, 4.2f); SaveStationToFS(); break;
            case 7020: fillPulsesPerLiter  = max(1.0f, cmd / 10.0f); SaveStationToFS(); break;
            case 7021: drainPulsesPerLiter = max(1.0f, cmd / 10.0f); SaveStationToFS(); break;
            case 7032:
                if (cmd > 0 && fillCalSnapshot > 0)
                    fillPulsesPerLiter = (float)fillCalSnapshot / (float)cmd * 1000.0f;
                fillCalActive = false;
                SaveStationToFS();
                Serial.printf("Fill cal: %.1f ppl\n", fillPulsesPerLiter);
                break;
            case 7035:
                if (cmd > 0 && drainCalSnapshot > 0)
                    drainPulsesPerLiter = (float)drainCalSnapshot / (float)cmd * 1000.0f;
                drainCalActive = false;
                SaveStationToFS();
                Serial.printf("Drain cal: %.1f ppl\n", drainPulsesPerLiter);
                break;
            case 6001: heliModels[activeModelIndex].tankVolumeMl = max(100, cmd); HeliLib_Save(activeModelIndex); break;
            case 6002: heliModels[activeModelIndex].fillSpeed    = constrain(cmd, 50, 3000); HeliLib_Save(activeModelIndex); break;
            case 6003: heliModels[activeModelIndex].hasTankSensor = (cmd != 0); HeliLib_Save(activeModelIndex); break;
            case 6004: heliModels[activeModelIndex].purgeSecs    = constrain(cmd, 0, 30); HeliLib_Save(activeModelIndex); break;
            case 6005: heliModels[activeModelIndex].drainSpeed   = constrain(cmd, 50, 3000); HeliLib_Save(activeModelIndex); break;
            case 8020:
                if (cmd >= 0 && cmd < numModels) {
                    activeModelIndex = cmd;
                    HeliLib_SaveActiveIndex();
                    strncpy(gDisplay.modelName, heliModels[activeModelIndex].name, sizeof(gDisplay.modelName) - 1);
                }
                break;
        }
        return;
    }

    // Single-step commands
    switch (cmd) {
        case 1:  break;  // main→fill navigation, no hardware action
        case 11: BeginFill();  break;
        case 2:  break;  // main→drain navigation, no hardware action
        case 12: BeginDrain(); break;
        case 3:  StopFill();   break;
        case 7011: supplyTankRemainingMl = supplyTankCapacityMl; SaveStationToFS(); break;
        case 7018: lowBatteryLatched = false; lowBattCount = 0; SetMessage("Batt latch cleared", MSG_IDLE); break;
        case 7030:
            noInterrupts(); fillPulses = 0; interrupts();
            fillCalSnapshot = 0;
            fillCalActive   = true;
            fillCalStartMs  = millis();
            BeginFill();
            break;
        case 7031:
            noInterrupts(); fillCalSnapshot = fillPulses; interrupts();
            fillCalActive = false;
            StopFill();
            break;
        case 7032: pendingWebCmd = 7032; break;
        case 7033:
            noInterrupts(); drainPulses = 0; interrupts();
            drainCalSnapshot = 0;
            drainCalActive   = true;
            drainCalStartMs  = millis();
            BeginDrain();
            break;
        case 7034:
            noInterrupts(); drainCalSnapshot = drainPulses; interrupts();
            drainCalActive = false;
            StopFill();
            break;
        case 7035: pendingWebCmd = 7035; break;
        // Two-step station / model param commands:
        case 7010: case 7012: case 7013: case 7014: case 7016:
        case 7020: case 7021:
        case 6001: case 6002: case 6003: case 6004: case 6005:
        case 8020:
            pendingWebCmd = cmd; break;
        // UI navigation only — no hardware action:
        case 4000: case 4020: case 4030: break;
    }
}

// ── Fill / Drain session begin ────────────────────────────────────
void BeginFill()
{
    if (lowBatteryLatched) return;
    gLastPumpWasDrain = false;
    Screen_ClearPostPump();
    if (!fillCalActive && heliModels[activeModelIndex].hasTankSensor && !Sensors_IsTankSensorFitted()) {
        SetMessage("Connect Tank Full Sensor", MSG_WARN);
        return;
    }
    if (!fillCalActive && heliModels[activeModelIndex].hasTankSensor && Sensors_IsTankFull()) return;

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
    gLastPumpWasDrain = true;
    Screen_ClearPostPump();
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

    // Auto-stop conditions — all bypassed during calibration
    if (PumpEnabled && autoFillSequence != AF_PURGING && !fillCalActive) {
        if (targetFillMl > 0 && lastFillVolumeMl >= targetFillMl) {
            Pump_Stop(); BeginOverflowPurge(); return;
        }
        if (heliModels[activeModelIndex].hasTankSensor && Sensors_IsTankSensorFitted() && Sensors_IsTankFull()) {
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
        Screen_SetPostPump(false);
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
    gDisplay.pumpSpeedPct = MAX_PWM > MIN_PWM
        ? constrain((int)(100.0f * drainClosedLoopCurrentPwm / MAX_PWM), 0, 100) : 0;
    gDisplay.pressurePct  = constrain((int)(Sensors_PressureBar() / 4.0f * 100.0f), 0, 100);

    // Tank empty detection (ported from V1)
    if (PumpEnabled && flowMlMin > drainPeakFlowMlMin) {
        drainPeakFlowMlMin = flowMlMin;
        tankEmptyCount     = 0;
    }
    bool gated = !drainClosedLoopActive || drainClosedLoopHasSettled
                 || (drainClosedLoopCurrentPwm >= (int)(MAX_PWM * 0.5f));
    // Tank empty auto-stop bypassed during calibration
    if (!drainCalActive && PumpEnabled && (millis() - drainStartMs) >= tankEmptyMinRunMs && gated) {
        if (drainPeakFlowMlMin < TANK_EMPTY_MIN_PEAK_FLOW) {
            Pump_Stop();
            heliModels[activeModelIndex].totalDrains++;
            heliModels[activeModelIndex].totalDrainMl += (uint32_t)lastDrainVolumeMl;
            HeliLib_Save(activeModelIndex);
            SetMessage("Tank was empty", MSG_WARN);
            Screen_SetPostPump(true);
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
                    Screen_SetPostPump(true);
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
            Screen_SetPostPump(false);
        }
    }
}

// ── Battery check ─────────────────────────────────────────────────
static void UpdateBattery()
{
    static uint32_t lastMs = 0;
    if (millis() < 5000) return;          // skip first 5 s — ADC not settled
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

    if (PumpEnabled) {
        // During fill/drain: encoder adjusts pump speed in 50 ml/min steps
        const int step = 50;
        if (closedLoopActive) {
            closedLoopTargetMlMin = constrain(closedLoopTargetMlMin + (acc > 0 ? step : -step), 50, 3000);
            heliModels[activeModelIndex].fillSpeed = closedLoopTargetMlMin;
            char buf[32]; snprintf(buf, sizeof(buf), "Speed: %d ml/min", closedLoopTargetMlMin);
            SetMessage(buf, MSG_FILLING);
        } else if (drainClosedLoopActive) {
            drainClosedLoopTargetMlMin = constrain(drainClosedLoopTargetMlMin + (acc > 0 ? step : -step), 50, 3000);
            heliModels[activeModelIndex].drainSpeed = drainClosedLoopTargetMlMin;
            char buf[32]; snprintf(buf, sizeof(buf), "Speed: %d ml/min", drainClosedLoopTargetMlMin);
            SetMessage(buf, MSG_DRAINING);
        }
    } else {
        Screen_EncoderScroll(acc > 0 ? 1 : -1);
    }
    acc = 0;
}

static bool pendingModelSave = false;

// ── Power button short press ──────────────────────────────────────
//
// Button bounce on mechanical release produces a spurious second press
// ~40ms after the real one.  A single timestamp guard at the very top
// covers ALL scenarios — pump running, idle, screen switch — without
// needing per-branch guards that could be bypassed.
void OnShortPress()
{
    static uint32_t lastPressMs = 0;
    uint32_t now = millis();
    if (now - lastPressMs < 600) { Power_UpdateActivity(); return; }
    lastPressMs = now;

    if (PumpEnabled) {
        // STOP is always highlighted while pump runs — button always stops
        StopFill();
        bool wasDrain = drainClosedLoopActive || (autoFillSequence == AF_PURGING);
        Screen_SetPostPump(wasDrain);
        Power_UpdateActivity();
        return;
    }

    if (Screen_IsPostPump()) {
        int sel = Screen_GetPumpBtnSel();
        if (sel == 0) {
            // START selected — restart same operation
            if (gLastPumpWasDrain) BeginDrain();
            else                   BeginFill();
        } else {
            // BACK selected — return to HOME
            Screen_ClearPostPump();
            Screen_SetScreen(SCREEN_HOME);
        }
        Power_UpdateActivity();
        return;
    }

    switch (Screen_CurrentScreen()) {
        case SCREEN_HOME: {
            int sel = Screen_GetActionSel();
            switch (sel) {
                case ACTION_FILL:  BeginFill();  break;
                case ACTION_DRAIN: BeginDrain(); break;
                case ACTION_MODEL: Screen_SetScreen(SCREEN_MODEL); break;
                case ACTION_NET:   Screen_SetScreen(SCREEN_NET);   break;
            }
            break;
        }
        case SCREEN_MODEL:
            Screen_EncoderPress();
            strncpy(gDisplay.modelName, heliModels[activeModelIndex].name,
                    sizeof(gDisplay.modelName) - 1);
            SetMessage(heliModels[activeModelIndex].name, MSG_IDLE);
            pendingModelSave = true;
            break;
        case SCREEN_NET:
            Screen_SetScreen(SCREEN_HOME);
            break;
        default:
            Screen_SetScreen(SCREEN_HOME);
            break;
    }
    Power_UpdateActivity();
}

// ── Power button back (3s hold) ──────────────────────────────────
void OnBackPress()
{
    if (PumpEnabled) {
        StopFill();
        // Long-hold back while pumping = stop and go straight home
        Screen_ClearPostPump();
        Screen_SetScreen(SCREEN_HOME);
        Power_UpdateActivity();
        return;
    }
    if (Screen_IsPostPump()) {
        Screen_ClearPostPump();
    }
    Screen_SetScreen(SCREEN_HOME);
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
    // Wait up to 3 s for USB CDC to enumerate so boot messages are visible
    { uint32_t t0 = millis(); while (!Serial && millis() - t0 < 3000) yield(); }
    delay(100);
    Serial.printf("\nMCP Fuel Station V2  %s  %s %s\n",
                  FW_VERSION, FW_BUILD_DATE, FW_BUILD_TIME);

    HeliLib_Init();
    HeliLib_Load();
    LoadStationFromFS();

    Sensors_Init();
    RTC_Init();
    Pump_Init();
    Screen_Init();
    Power_Init(OnShortPress, OnBackPress, OnShutdown);
    WebServer_Init();
    WebServer_SetCommandHandler(OnWebCommand);
    RTC_NTPSync();   // sync DS3231 from NTP while WiFi is fresh

    // Push network IPs to screen (shown on SCREEN_NET)
    {
        String staIP = WebServer_GetLocalIP();
        String apIP  = WiFi.softAPIP().toString();
        Screen_SetNetworkIP(staIP.c_str(), apIP.c_str());

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

    strncpy(gDisplay.modelName, heliModels[activeModelIndex].name,
            sizeof(gDisplay.modelName) - 1);
    gDisplay.battPct      = Sensors_BattPct();
    gDisplay.outerTankPct = supplyTankCapacityMl > 0
        ? constrain((int)(100.0f * supplyTankRemainingMl / supplyTankCapacityMl), 0, 100) : 0;

    Screen_SetScreen(SCREEN_HOME);
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

    if (pendingModelSave) {
        pendingModelSave = false;
        HeliLib_SaveActiveIndex();
    }

    // Update display data from active model
    strncpy(gDisplay.modelName, heliModels[activeModelIndex].name,
            sizeof(gDisplay.modelName) - 1);
    gDisplay.pumpRunning  = PumpEnabled;
    gDisplay.sensorFitted = Sensors_IsTankSensorFitted();
    gDisplay.tankFull     = Sensors_IsTankFull();
    gDisplay.supplyMl     = supplyTankRemainingMl;
    gDisplay.supplyCapMl  = supplyTankCapacityMl;
    gDisplay.supplyLowMl  = supplyLowThresholdMl;

    // Zero pump-specific fields when idle so the display transitions cleanly
    if (!PumpEnabled) {
        gDisplay.pumpSpeedPct = 0;
        gDisplay.flowMlMin    = 0;
        gDisplay.mainTankPct  = 0;
        gDisplay.volumeMl     = 0;
    }

    // Clear startup IP message after 6 s; then show normal idle text
    static bool ipMsgCleared = false;
    if (!ipMsgCleared && now >= 6000) {
        ipMsgCleared = true;
        SetMessage("", MSG_IDLE);
    }

    // Refresh screen at ~10Hz
    static uint32_t lastDisplayMs = 0;
    if (now - lastDisplayMs >= 100) {
        lastDisplayMs = now;
        Screen_Update(gDisplay);
    }

    // Broadcast WebSocket state at ~4Hz
    static uint32_t lastWsMs = 0;
    if (now - lastWsMs >= 250) {
        lastWsMs = now;
        WebServer_BroadcastState(BuildWebStateJson());
    }

    WebServer_Update();
}
