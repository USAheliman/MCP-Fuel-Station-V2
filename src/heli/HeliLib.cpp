#include "HeliLib.h"

// ═══════════════════════════════════════════════════════════════════
// MCP Fuel Station V2 — Heli Model Library Implementation
// Storage: LittleFS /models/<name>/config.json
// Mirrors V1 SD structure but uses JSON (same as V1 ESP32 side)
// ═══════════════════════════════════════════════════════════════════

HeliModel heliModels[MAX_MODELS];
int       numModels        = 0;
int       activeModelIndex = 0;
int       previewModelIndex= 0;

static void mkdirp(const char* path)
{
    char tmp[64];
    strncpy(tmp, path, sizeof(tmp) - 1);
    for (char* p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            if (!LittleFS.exists(tmp)) LittleFS.mkdir(tmp);
            *p = '/';
        }
    }
    if (!LittleFS.exists(tmp)) LittleFS.mkdir(tmp);
}

void HeliLib_Init()
{
    if (!LittleFS.begin(false, "/littlefs", 10, "littlefs")) {
        Serial.println("HeliLib: LittleFS mount failed — formatting");
        LittleFS.begin(true, "/littlefs", 10, "littlefs");
    }
    if (!LittleFS.exists("/models")) LittleFS.mkdir("/models");
}

void HeliLib_LoadDefaults()
{
    numModels = 4;

    strncpy(heliModels[0].name, "BO 105",   MODEL_NAME_LEN-1);
    heliModels[0].tankVolumeMl=2000; heliModels[0].hasTankSensor=true;
    heliModels[0].fillSpeed=500;     heliModels[0].drainSpeed=500; heliModels[0].purgeSecs=3;

    strncpy(heliModels[1].name, "Whiplash", MODEL_NAME_LEN-1);
    heliModels[1].tankVolumeMl=1000; heliModels[1].hasTankSensor=false;
    heliModels[1].fillSpeed=500;     heliModels[1].drainSpeed=500; heliModels[1].purgeSecs=3;

    strncpy(heliModels[2].name, "Alouette", MODEL_NAME_LEN-1);
    heliModels[2].tankVolumeMl=1500; heliModels[2].hasTankSensor=true;
    heliModels[2].fillSpeed=500;     heliModels[2].drainSpeed=500; heliModels[2].purgeSecs=3;

    strncpy(heliModels[3].name, "EC 145",   MODEL_NAME_LEN-1);
    heliModels[3].tankVolumeMl=3000; heliModels[3].hasTankSensor=true;
    heliModels[3].fillSpeed=500;     heliModels[3].drainSpeed=500; heliModels[3].purgeSecs=3;

    for (int i = 0; i < 4; i++) {
        heliModels[i].totalFills=0; heliModels[i].totalDrains=0;
        heliModels[i].totalFillMl=0; heliModels[i].totalDrainMl=0;
        heliModels[i].hasImage=false;
    }
    activeModelIndex  = 0;
    previewModelIndex = 0;
    Serial.println("HeliLib: using default models");
}

void HeliLib_Load()
{
    numModels = 0;

    // Load active index
    if (LittleFS.exists("/activeModel.txt")) {
        File f = LittleFS.open("/activeModel.txt", "r");
        if (f) { activeModelIndex = f.readStringUntil('\n').toInt(); f.close(); }
    }

    // Scan /models/ directory — identical to V1 ESP32 loadModelsFromFS()
    File root = LittleFS.open("/models");
    if (!root || !root.isDirectory()) {
        HeliLib_LoadDefaults();
        return;
    }

    File entry = root.openNextFile();
    while (entry && numModels < MAX_MODELS) {
        if (entry.isDirectory()) {
            String cfgPath = String("/models/") + entry.name() + "/config.json";
            if (LittleFS.exists(cfgPath)) {
                File cfg = LittleFS.open(cfgPath, "r");
                if (cfg) {
                    JsonDocument doc;
                    DeserializationError err = deserializeJson(doc, cfg);
                    cfg.close();
                    if (!err && !doc["archived"].as<bool>()) {
                        HeliModel &m = heliModels[numModels];
                        strncpy(m.name, doc["name"] | entry.name(), MODEL_NAME_LEN - 1);
                        m.tankVolumeMl  = doc["tankVol"]    | 2000;
                        m.hasTankSensor = doc["sensor"]     | false;
                        m.fillSpeed     = doc["fillSpd"]    | 500;
                        m.drainSpeed    = doc["drainSpd"]   | 500;
                        m.purgeSecs     = doc["purge"]      | 3;
                        m.slowFillPct   = doc["slowFill"]   | 0;
                        m.totalFills    = doc["totalFills"] | 0;
                        m.totalDrains   = doc["totalDrains"]| 0;
                        m.totalFillMl   = doc["totalFillMl"]| 0;
                        m.totalDrainMl  = doc["totalDrainMl"]| 0;
                        String imgPath  = String("/models/") + entry.name() + "/thumb.jpg";
                        m.hasImage      = LittleFS.exists(imgPath);
                        numModels++;
                    }
                }
            }
        }
        entry = root.openNextFile();
    }

    if (numModels == 0) {
        HeliLib_LoadDefaults();
        return;
    }

    activeModelIndex  = constrain(activeModelIndex,  0, numModels - 1);
    previewModelIndex = activeModelIndex;
    Serial.printf("HeliLib: loaded %d models, active=%d\n", numModels, activeModelIndex);
}

void HeliLib_Save(int idx)
{
    if (idx < 0 || idx >= numModels) return;
    HeliModel &m = heliModels[idx];

    String dir = String("/models/") + m.name;
    mkdirp(dir.c_str());

    String cfgPath = dir + "/config.json";
    File f = LittleFS.open(cfgPath, "w");
    if (!f) { Serial.printf("HeliLib: could not write %s\n", cfgPath.c_str()); return; }

    JsonDocument doc;
    doc["name"]        = m.name;
    doc["tankVol"]     = m.tankVolumeMl;
    doc["sensor"]      = m.hasTankSensor;
    doc["fillSpd"]     = m.fillSpeed;
    doc["drainSpd"]    = m.drainSpeed;
    doc["purge"]       = m.purgeSecs;
    doc["slowFill"]    = m.slowFillPct;
    doc["totalFills"]  = m.totalFills;
    doc["totalDrains"] = m.totalDrains;
    doc["totalFillMl"] = m.totalFillMl;
    doc["totalDrainMl"]= m.totalDrainMl;
    doc["archived"]    = false;
    serializeJson(doc, f);
    f.close();
    Serial.printf("HeliLib: saved %s\n", m.name);
}

void HeliLib_SaveAll()
{
    for (int i = 0; i < numModels; i++) HeliLib_Save(i);
    HeliLib_SaveActiveIndex();
}

void HeliLib_SaveActiveIndex()
{
    File f = LittleFS.open("/activeModel.txt", "w");
    if (f) { f.println(activeModelIndex); f.close(); }
}

void HeliLib_Delete(const char* name)
{
    // Mark archived in JSON (mirrors V1 ESP32 deleteModelFiles)
    String cfgPath = String("/models/") + name + "/config.json";
    if (LittleFS.exists(cfgPath)) {
        File f = LittleFS.open(cfgPath, "r");
        JsonDocument doc;
        deserializeJson(doc, f);
        f.close();
        doc["archived"] = true;
        File fw = LittleFS.open(cfgPath, "w");
        serializeJson(doc, fw);
        fw.close();
    }
    // Remove from RAM array
    for (int i = 0; i < numModels; i++) {
        if (strcmp(heliModels[i].name, name) == 0) {
            for (int j = i; j < numModels - 1; j++) heliModels[j] = heliModels[j + 1];
            numModels--;
            activeModelIndex  = constrain(activeModelIndex,  0, max(0, numModels - 1));
            previewModelIndex = constrain(previewModelIndex, 0, max(0, numModels - 1));
            HeliLib_SaveActiveIndex();
            Serial.printf("HeliLib: deleted %s\n", name);
            return;
        }
    }
}

int HeliLib_Find(const char* name)
{
    for (int i = 0; i < numModels; i++)
        if (strcmp(heliModels[i].name, name) == 0) return i;
    return -1;
}
