#pragma once
#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

// ═══════════════════════════════════════════════════════════════════
// MCP Fuel Station V2 — Heli Model Library
// Ported from V1 SD/model system → LittleFS JSON (already on ESP32)
// ═══════════════════════════════════════════════════════════════════

#define MAX_MODELS      20
#define MODEL_NAME_LEN  24

struct HeliModel {
    char     name[MODEL_NAME_LEN];
    int      tankVolumeMl;
    bool     hasTankSensor;
    int      fillSpeed;        // ml/min setpoint
    int      drainSpeed;       // ml/min setpoint
    int      purgeSecs;        // overflow purge duration (0 = skip)
    uint32_t totalFills;
    uint32_t totalDrains;
    uint32_t totalFillMl;
    uint32_t totalDrainMl;
    bool     hasImage;
};

// ── Global library state ──────────────────────────────────────────
extern HeliModel heliModels[MAX_MODELS];
extern int       numModels;
extern int       activeModelIndex;
extern int       previewModelIndex;

// ── API ───────────────────────────────────────────────────────────
void HeliLib_Init();
void HeliLib_Load();
void HeliLib_Save(int idx);
void HeliLib_SaveAll();
void HeliLib_Delete(const char* name);
int  HeliLib_Find(const char* name);   // returns index or -1
void HeliLib_LoadDefaults();
void HeliLib_SaveActiveIndex();
