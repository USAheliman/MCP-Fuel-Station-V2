#include "WebServer.h"
#include "../heli/HeliLib.h"
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

// ═══════════════════════════════════════════════════════════════════
// MCO Fuel Station V2 — Web Server
// Ported directly from V1 esp32_fuel_station/src/main.cpp
// All REST API endpoints, WebSocket, image upload preserved
// HOME_SSID intentionally left blank — set at build time if needed
// ═══════════════════════════════════════════════════════════════════

static WebServer        httpServer(80);
static WebSocketsServer wsServer(81);

static String lastJsonState = "{}";

// Forward declarations
static void handleRoot();
static void handleGetModels();
static void handlePostModel();
static void handleDeleteModel();
static void handleGetImage();
static void handlePostImage();
static void handleImgChunk();
static void handleGetStation();
static void handleNotFound();
static void webSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length);
static void mkdirp(const char* path);
static void saveModelToFS(int idx);
static void syncModel(int idx);

// ── Init ─────────────────────────────────────────────────────────
void WebServer_Init()
{
    // AP mode — always available at the field
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(AP_SSID, AP_PASS);
    delay(500);
    Serial.printf("AP: %s  IP: %s\n", AP_SSID, WiFi.softAPIP().toString().c_str());

    // Optional home WiFi for development
    if (strlen(HOME_SSID) > 0) {
        WiFi.begin(HOME_SSID, HOME_PASS);
        int tries = 0;
        while (WiFi.status() != WL_CONNECTED && tries++ < 20) {
            delay(500); Serial.print(".");
        }
        Serial.println();
        if (WiFi.status() == WL_CONNECTED)
            Serial.printf("Home WiFi: %s\n", WiFi.localIP().toString().c_str());
        else
            Serial.println("Home WiFi not found — AP only");
    }

    // mDNS
    if (MDNS.begin("fuelstation"))
        Serial.println("mDNS: http://fuelstation.local");

    // HTTP routes — identical to V1
    httpServer.on("/",             HTTP_GET,    handleRoot);
    httpServer.on("/api/models",   HTTP_GET,    handleGetModels);
    httpServer.on("/api/model",    HTTP_POST,   handlePostModel);
    httpServer.on("/api/model",    HTTP_DELETE, handleDeleteModel);
    httpServer.on("/api/station",  HTTP_GET,    handleGetStation);
    httpServer.on("/image",        HTTP_GET,    handleGetImage);
    httpServer.on("/image",        HTTP_POST,   handlePostImage);
    httpServer.on("/imgchunk",     HTTP_POST,   handleImgChunk);

    // OTA HTML update
    httpServer.on("/update-html", HTTP_POST, [](){
        httpServer.sendHeader("Access-Control-Allow-Origin", "*");
        httpServer.send(200, "application/json", "{\"ok\":true}");
    }, [](){
        HTTPUpload& upload = httpServer.upload();
        static File uf;
        if (upload.status == UPLOAD_FILE_START)
            uf = LittleFS.open("/index.html", "w");
        else if (upload.status == UPLOAD_FILE_WRITE && uf)
            uf.write(upload.buf, upload.currentSize);
        else if (upload.status == UPLOAD_FILE_END && uf)
            uf.close();
    });

    // Debug endpoints
    httpServer.on("/debug/fs", HTTP_GET, [](){
        String out;
        File root = LittleFS.open("/");
        File f    = root.openNextFile();
        while (f) { out += String(f.name()) + " (" + f.size() + ")\n"; f = root.openNextFile(); }
        httpServer.send(200, "text/plain", out);
    });

    httpServer.onNotFound(handleNotFound);
    const char* hdr[] = {"Content-Length", "Content-Type"};
    httpServer.collectHeaders(hdr, 2);
    httpServer.begin();
    Serial.println("HTTP server: port 80");

    wsServer.begin();
    wsServer.onEvent(webSocketEvent);
    Serial.println("WebSocket server: port 81");
}

void WebServer_Update()
{
    httpServer.handleClient();
    wsServer.loop();
}

void WebServer_BroadcastState(const String &json)
{
    lastJsonState = json;
    wsServer.broadcastTXT(json.c_str());
}

// ── WebSocket ─────────────────────────────────────────────────────
static void webSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length)
{
    if (type == WStype_CONNECTED) {
        if (lastJsonState.length() > 2)
            wsServer.sendTXT(num, lastJsonState.c_str());
    }
    else if (type == WStype_TEXT) {
        // Commands from browser forwarded as-is (same CMD: protocol as V1)
        String cmd = String((char*)payload).substring(0, length);
        Serial.printf("WS cmd: %s\n", cmd.c_str());
        // TODO: parse CMD: and dispatch to main loop state machine
        // (will be wired in Phase 2 — web ↔ fill/drain control)
    }
}

// ── Helpers ───────────────────────────────────────────────────────
static void mkdirp(const char* path)
{
    char tmp[64];
    strncpy(tmp, path, sizeof(tmp) - 1);
    if (!LittleFS.exists("/models")) LittleFS.mkdir("/models");
    for (char* p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            if (!LittleFS.exists(tmp)) LittleFS.mkdir(tmp);
            *p = '/';
        }
    }
    if (!LittleFS.exists(tmp)) LittleFS.mkdir(tmp);
}

static int b64val(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

static void writeBase64ToFile(const String& b64, const String& path, bool append)
{
    File f = LittleFS.open(path, append ? "a" : "w");
    if (!f) return;
    int blen = b64.length();
    for (int i = 0; i < blen; i += 4) {
        int v0 = b64val(b64[i]);
        int v1 = (i+1 < blen) ? b64val(b64[i+1]) : -1;
        int v2 = (i+2 < blen) ? b64val(b64[i+2]) : -1;
        int v3 = (i+3 < blen) ? b64val(b64[i+3]) : -1;
        if (v0 < 0 || v1 < 0) break;
        f.write((uint8_t)((v0 << 2) | (v1 >> 4)));
        if (v2 >= 0) f.write((uint8_t)(((v1 & 0xF) << 4) | (v2 >> 2)));
        if (v3 >= 0) f.write((uint8_t)(((v2 & 0x3) << 6) | v3));
    }
    f.close();
}

// ── REST handlers (ported 1:1 from V1) ───────────────────────────
static void handleRoot()
{
    if (LittleFS.exists("/index.html")) {
        File f = LittleFS.open("/index.html", "r");
        httpServer.streamFile(f, "text/html");
        f.close();
    } else {
        httpServer.send(200, "text/html",
            "<h2>MCO Fuel Station V2</h2><p>Upload index.html via /update-html</p>");
    }
}

static void handleGetModels()
{
    String json = "[";
    for (int i = 0; i < numModels; i++) {
        if (i > 0) json += ",";
        HeliModel &m = heliModels[i];
        json += "{\"name\":\"" + String(m.name) + "\","
                "\"tankVol\":"  + m.tankVolumeMl  + ","
                "\"sensor\":"   + (m.hasTankSensor ? "true" : "false") + ","
                "\"fillSpd\":"  + m.fillSpeed      + ","
                "\"drainSpd\":" + m.drainSpeed     + ","
                "\"purge\":"    + m.purgeSecs      + ","
                "\"totalFills\":" + m.totalFills   + ","
                "\"totalDrains\":" + m.totalDrains + ","
                "\"hasImage\":" + (m.hasImage ? "true" : "false") + ","
                "\"active\":"   + (i == activeModelIndex ? "true" : "false") + "}";
    }
    json += "]";
    httpServer.sendHeader("Access-Control-Allow-Origin", "*");
    httpServer.send(200, "application/json", json);
}

static void handlePostModel()
{
    if (!httpServer.hasArg("plain")) { httpServer.send(400, "text/plain", "No body"); return; }
    JsonDocument doc;
    if (deserializeJson(doc, httpServer.arg("plain"))) {
        httpServer.send(400, "text/plain", "Bad JSON"); return;
    }
    const char* name = doc["name"] | "";
    if (!strlen(name)) { httpServer.send(400, "text/plain", "Missing name"); return; }

    int idx = HeliLib_Find(name);
    if (idx < 0) {
        if (numModels >= MAX_MODELS) { httpServer.send(400, "text/plain", "Max models"); return; }
        idx = numModels++;
        memset(&heliModels[idx], 0, sizeof(HeliModel));
    }
    HeliModel &m = heliModels[idx];
    strncpy(m.name, name, MODEL_NAME_LEN - 1);
    m.tankVolumeMl  = doc["tankVol"]  | m.tankVolumeMl;
    m.hasTankSensor = doc["sensor"]   | m.hasTankSensor;
    m.fillSpeed     = doc["fillSpd"]  | m.fillSpeed;
    m.drainSpeed    = doc["drainSpd"] | m.drainSpeed;
    m.purgeSecs     = doc["purge"]    | m.purgeSecs;
    HeliLib_Save(idx);

    httpServer.sendHeader("Access-Control-Allow-Origin", "*");
    httpServer.send(200, "application/json", "{\"ok\":true,\"idx\":" + String(idx) + "}");
}

static void handleDeleteModel()
{
    String name = httpServer.arg("name");
    if (!name.length()) { httpServer.send(400, "text/plain", "Missing name"); return; }
    HeliLib_Delete(name.c_str());
    httpServer.sendHeader("Access-Control-Allow-Origin", "*");
    httpServer.send(200, "application/json", "{\"ok\":true}");
}

static void handleGetStation()
{
    httpServer.sendHeader("Access-Control-Allow-Origin", "*");
    httpServer.send(200, "application/json", lastJsonState);
}

static void handleGetImage()
{
    String name = httpServer.arg("name");
    if (!name.length()) { httpServer.send(400, "text/plain", "Missing name"); return; }
    String path = "/models/" + name + "/thumb.jpg";
    if (!LittleFS.exists(path)) {
        String svg = "<svg xmlns='http://www.w3.org/2000/svg' width='320' height='220'>"
                     "<rect width='320' height='220' fill='#111827'/>"
                     "<text x='160' y='120' text-anchor='middle' fill='#4B5563' "
                     "font-size='16' font-family='sans-serif'>" + name + "</text></svg>";
        httpServer.sendHeader("Access-Control-Allow-Origin", "*");
        httpServer.send(200, "image/svg+xml", svg);
        return;
    }
    File f = LittleFS.open(path, "r");
    httpServer.sendHeader("Access-Control-Allow-Origin", "*");
    httpServer.streamFile(f, "image/jpeg");
    f.close();
}

static void handlePostImage()
{
    String name = httpServer.arg("name");
    if (!name.length()) { httpServer.send(400, "text/plain", "Missing name"); return; }
    String dir  = "/models/" + name;
    mkdirp(dir.c_str());
    writeBase64ToFile(httpServer.arg("plain"), dir + "/thumb.jpg", false);
    int idx = HeliLib_Find(name.c_str());
    if (idx >= 0) heliModels[idx].hasImage = true;
    httpServer.sendHeader("Access-Control-Allow-Origin", "*");
    httpServer.send(200, "application/json", "{\"ok\":true}");
}

static void handleImgChunk()
{
    String name     = httpServer.arg("name");
    int    chunkIdx = httpServer.arg("chunk").toInt();
    int    total    = httpServer.arg("total").toInt();
    if (!name.length()) { httpServer.send(400, "text/plain", "Bad request"); return; }
    String dir  = "/models/" + name;
    mkdirp(dir.c_str());
    writeBase64ToFile(httpServer.arg("plain"), dir + "/thumb.jpg", chunkIdx > 0);
    if (chunkIdx == total - 1) {
        int idx = HeliLib_Find(name.c_str());
        if (idx >= 0) heliModels[idx].hasImage = true;
    }
    httpServer.sendHeader("Access-Control-Allow-Origin", "*");
    httpServer.send(200, "application/json", "{\"ok\":true}");
}

static void handleNotFound()
{
    httpServer.send(404, "text/plain", "Not found");
}
