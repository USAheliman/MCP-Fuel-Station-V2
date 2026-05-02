#include "WebServer.h"
#include "../heli/HeliLib.h"
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <esp_ota_ops.h>

// ═══════════════════════════════════════════════════════════════════
// MCP Fuel Station V2 — Web Server
// Connects to HOME_SSID (SilverLining) + always raises AP fallback.
// REST API, WebSocket live data, and OTA firmware management.
// OTA page: http://<ip>/ota  or  http://fuelstation.local/ota
// ═══════════════════════════════════════════════════════════════════

static WebServer        httpServer(80);
static WebSocketsServer wsServer(81);
static String           lastJsonState = "{}";

// ── OTA state ─────────────────────────────────────────────────────
#define OTA_DIR "/ota"

struct OtaSlot { String label; size_t size; String date; };
static OtaSlot  otaSlots[OTA_MAX_SLOTS];
static int      otaSlotCount      = 0;
static bool     otaInstallPending = false;
static int      otaInstallIdx     = 0;
static File     otaUploadFile;
static size_t   otaUploadBytes    = 0;
static bool     otaUploadOk       = false;

// ── Embedded OTA web page ─────────────────────────────────────────
static const char OTA_HTML[] PROGMEM = R"OTA(<!DOCTYPE html>
<html lang="en"><head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>MCP Fuel Station - OTA</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:-apple-system,sans-serif;background:#0f172a;color:#e2e8f0;padding:16px}
h1{font-size:20px;color:#4ade80;margin-bottom:2px}
.sub{font-size:13px;color:#64748b;margin-bottom:20px}
.card{background:#1e293b;border-radius:12px;padding:16px;margin-bottom:16px}
.card h2{font-size:12px;color:#94a3b8;text-transform:uppercase;letter-spacing:.07em;margin-bottom:12px}
.slot{display:flex;align-items:center;gap:12px;padding:10px 0;border-bottom:1px solid #334155}
.slot:last-child{border:none}
.si{flex:1;min-width:0}
.sn{font-size:14px;font-weight:600;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.sm{font-size:12px;color:#64748b;margin-top:2px}
.badge{background:#166534;color:#4ade80;font-size:10px;padding:1px 5px;border-radius:4px;margin-left:6px;vertical-align:middle}
.btn{border:none;padding:10px 18px;border-radius:8px;font-size:14px;font-weight:600;cursor:pointer}
.bg{background:#16a34a;color:#fff}
.bb{background:#2563eb;color:#fff;width:100%;margin-top:12px;padding:14px;font-size:16px}
.btn:disabled{background:#334155;color:#475569;cursor:not-allowed}
.drop{border:2px dashed #334155;border-radius:8px;padding:28px 16px;text-align:center;cursor:pointer}
.drop.hf{border-color:#4ade80}
.di{font-size:28px;margin-bottom:6px}
.dt{font-size:14px;color:#64748b}
.drop.hf .dt{color:#4ade80}
#pb{display:none;margin-top:10px}
.pbg{background:#1e3a5f;border-radius:4px;height:8px;overflow:hidden}
.pf{height:100%;background:#3b82f6;border-radius:4px;width:0;transition:width .3s}
#st{margin-top:8px;font-size:13px;color:#64748b;min-height:16px}
.empty{text-align:center;color:#475569;font-size:13px;padding:12px 0}
</style></head><body>
<h1>MCP Fuel Station</h1>
<div class="sub">Over-the-Air Firmware Update</div>
<div class="card">
<h2>Stored Versions</h2>
<div id="slots"><div class="empty">Loading...</div></div>
</div>
<div class="card">
<h2>Upload New Firmware</h2>
<label class="drop" id="dz">
<input type="file" id="fi" accept=".bin" style="display:none">
<div class="di">&#128230;</div>
<div class="dt" id="dt">Tap to select .bin file</div>
</label>
<div id="pb"><div class="pbg"><div class="pf" id="pf"></div></div></div>
<div id="st"></div>
<button class="btn bb" id="ub" disabled>Upload &amp; Install</button>
</div>
<script>
var sel=null;
function esc(s){return s.replace(/&/g,'&amp;').replace(/</g,'&lt;');}
function fmt(b){return b>1048576?(b/1048576).toFixed(1)+' MB':b>1024?(b/1024).toFixed(0)+' KB':b+' B';}
function st(m,c){var e=document.getElementById('st');e.textContent=m;e.style.color=c===1?'#4ade80':c===0?'#f87171':'#94a3b8';}
function loadSlots(){
  fetch('/ota/versions').then(function(r){return r.json();}).then(function(v){
    if(!v||!v.length){document.getElementById('slots').innerHTML='<div class="empty">No versions stored — upload below</div>';return;}
    var h='';
    v.forEach(function(s,i){
      h+='<div class="slot"><div class="si"><div class="sn">'+esc(s.label)+(s.current?'<span class="badge">RUNNING</span>':'')+'</div><div class="sm">'+fmt(s.size)+' &bull; '+esc(s.date)+'</div></div><button class="btn bg" onclick="ins('+i+')">Install</button></div>';
    });
    document.getElementById('slots').innerHTML=h;
  }).catch(function(){document.getElementById('slots').innerHTML='<div class="empty" style="color:#f87171">Failed to load</div>';});
}
function ins(slot){
  if(!confirm('Install version '+slot+'? Device will reboot.'))return;
  st('Flashing...','');
  fetch('/ota/install?slot='+slot,{method:'POST'}).then(function(r){return r.json();}).then(function(d){
    if(d.ok)st('Done! Rebooting in ~3s...',1);
    else st('Error: '+(d.error||'unknown'),0);
  }).catch(function(){st('Install request failed',0);});
}
var dz=document.getElementById('dz');
dz.addEventListener('click',function(){document.getElementById('fi').click();});
document.getElementById('fi').addEventListener('change',function(e){
  sel=e.target.files[0];
  if(sel){document.getElementById('dt').textContent=sel.name+' ('+fmt(sel.size)+')';dz.classList.add('hf');document.getElementById('ub').disabled=false;}
});
dz.addEventListener('dragover',function(e){e.preventDefault();});
dz.addEventListener('drop',function(e){
  e.preventDefault();var f=e.dataTransfer&&e.dataTransfer.files[0];
  if(f){sel=f;document.getElementById('dt').textContent=f.name+' ('+fmt(f.size)+')';dz.classList.add('hf');document.getElementById('ub').disabled=false;}
});
document.getElementById('ub').addEventListener('click',function(){
  if(!sel)return;
  var fd=new FormData();fd.append('firmware',sel,sel.name);
  var xhr=new XMLHttpRequest();xhr.open('POST','/ota/upload');
  document.getElementById('pb').style.display='block';
  document.getElementById('ub').disabled=true;
  st('Uploading...','');
  xhr.upload.onprogress=function(e){
    if(e.lengthComputable){var p=Math.round(e.loaded/e.total*100);document.getElementById('pf').style.width=p+'%';st('Uploading '+p+'%...','');}
  };
  xhr.onload=function(){
    document.getElementById('pf').style.width='100%';
    if(xhr.status===200){
      st('Upload done — flashing...','');loadSlots();
      setTimeout(function(){
        fetch('/ota/install?slot=0',{method:'POST'}).then(function(r){return r.json();}).then(function(d){
          if(d.ok)st('Done! Rebooting in ~3s...',1);
          else{st('Upload OK, flash failed: '+(d.error||'?'),0);document.getElementById('ub').disabled=false;}
        }).catch(function(){st('Upload OK, install request failed',0);document.getElementById('ub').disabled=false;});
      },500);
    } else {
      st('Upload failed (HTTP '+xhr.status+')',0);document.getElementById('ub').disabled=false;
    }
  };
  xhr.onerror=function(){st('Network error',0);document.getElementById('ub').disabled=false;};
  xhr.send(fd);
});
loadSlots();
</script></body></html>
)OTA";

// ── OTA helpers ───────────────────────────────────────────────────
static void otaLoadManifest()
{
    otaSlotCount = 0;
    if (!LittleFS.exists(OTA_DIR "/manifest.json")) return;
    File f = LittleFS.open(OTA_DIR "/manifest.json", "r");
    if (!f) return;
    JsonDocument doc;
    if (deserializeJson(doc, f) != DeserializationError::Ok) { f.close(); return; }
    f.close();
    for (JsonObject o : doc.as<JsonArray>()) {
        if (otaSlotCount >= OTA_MAX_SLOTS) break;
        otaSlots[otaSlotCount++] = { o["label"] | "firmware", (size_t)(o["size"] | 0), o["date"] | "?" };
    }
}

static void otaSaveManifest()
{
    if (!LittleFS.exists(OTA_DIR)) LittleFS.mkdir(OTA_DIR);
    File f = LittleFS.open(OTA_DIR "/manifest.json", "w");
    if (!f) return;
    f.print("[");
    for (int i = 0; i < otaSlotCount; i++) {
        if (i > 0) f.print(",");
        f.print("{\"label\":\""); f.print(otaSlots[i].label);
        f.print("\",\"size\":");  f.print(otaSlots[i].size);
        f.print(",\"date\":\"");  f.print(otaSlots[i].date); f.print("\"}");
    }
    f.print("]");
    f.close();
}

// Rotate fw files: fw1→fw2, fw0→fw1 (oldest dropped), then slot entries
static void otaRotateSlots()
{
    String fw2 = String(OTA_DIR) + "/fw2.bin";
    String fw1 = String(OTA_DIR) + "/fw1.bin";
    String fw0 = String(OTA_DIR) + "/fw0.bin";
    if (LittleFS.exists(fw2)) LittleFS.remove(fw2);
    if (LittleFS.exists(fw1)) LittleFS.rename(fw1, fw2);
    if (LittleFS.exists(fw0)) LittleFS.rename(fw0, fw1);
}

static bool otaInstallFromFS(int slot)
{
    String path = String(OTA_DIR) + "/fw" + slot + ".bin";
    File f = LittleFS.open(path, "r");
    if (!f) { Serial.println("OTA: file not found"); return false; }

    size_t fsize = f.size();
    Serial.printf("OTA: installing slot %d  %u bytes\n", slot, fsize);

    const esp_partition_t* part = esp_ota_get_next_update_partition(NULL);
    if (!part) { f.close(); Serial.println("OTA: no update partition"); return false; }

    esp_ota_handle_t handle = 0;
    if (esp_ota_begin(part, fsize, &handle) != ESP_OK) {
        f.close(); Serial.println("OTA: begin failed"); return false;
    }

    uint8_t buf[1024];
    bool err = false;
    while (f.available() && !err) {
        int n = f.read(buf, sizeof(buf));
        if (n > 0 && esp_ota_write(handle, buf, n) != ESP_OK) err = true;
    }
    f.close();

    if (err || esp_ota_end(handle) != ESP_OK) {
        Serial.println("OTA: write/end failed"); return false;
    }
    if (esp_ota_set_boot_partition(part) != ESP_OK) {
        Serial.println("OTA: set boot partition failed"); return false;
    }

    // Record which slot is now running (read back on next boot)
    File fi = LittleFS.open(OTA_DIR "/last_slot.txt", "w");
    if (fi) { fi.print(slot); fi.close(); }

    Serial.printf("OTA: slot %d installed OK\n", slot);
    return true;
}

// ── Forward declarations ──────────────────────────────────────────
static void handleRoot();
static void handleGetModels();
static void handlePostModel();
static void handleDeleteModel();
static void handleGetImage();
static void handlePostImage();
static void handleImgChunk();
static void handleGetStation();
static void handleNotFound();
static void handleOtaPage();
static void handleOtaVersions();
static void webSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length);
static void mkdirp(const char* path);
static void saveModelToFS(int idx);
static void syncModel(int idx);

// ── Init ──────────────────────────────────────────────────────────
void WebServer_Init()
{
    // AP mode — always available (field fallback)
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(AP_SSID, AP_PASS);
    delay(500);
    Serial.printf("AP: %s  IP: %s\n", AP_SSID, WiFi.softAPIP().toString().c_str());

    // Home WiFi (SilverLining via build flag)
    if (strlen(HOME_SSID) > 0) {
        Serial.printf("Connecting to %s ", HOME_SSID);
        WiFi.begin(HOME_SSID, HOME_PASS);
        int tries = 0;
        while (WiFi.status() != WL_CONNECTED && tries++ < 20) {
            delay(500); Serial.print(".");
        }
        Serial.println();
        if (WiFi.status() == WL_CONNECTED)
            Serial.printf("WiFi: %s\n", WiFi.localIP().toString().c_str());
        else
            Serial.println("WiFi not found — AP only");
    }

    if (MDNS.begin("fuelstation"))
        Serial.println("mDNS: http://fuelstation.local");

    // Load OTA manifest
    if (!LittleFS.exists(OTA_DIR)) LittleFS.mkdir(OTA_DIR);
    otaLoadManifest();

    // ── HTTP routes ───────────────────────────────────────────────
    httpServer.on("/",              HTTP_GET,    handleRoot);
    httpServer.on("/api/models",    HTTP_GET,    handleGetModels);
    httpServer.on("/api/model",     HTTP_POST,   handlePostModel);
    httpServer.on("/api/model",     HTTP_DELETE, handleDeleteModel);
    httpServer.on("/api/station",   HTTP_GET,    handleGetStation);
    httpServer.on("/image",         HTTP_GET,    handleGetImage);
    httpServer.on("/image",         HTTP_POST,   handlePostImage);
    httpServer.on("/imgchunk",      HTTP_POST,   handleImgChunk);

    // OTA endpoints
    httpServer.on("/ota",           HTTP_GET,    handleOtaPage);
    httpServer.on("/ota/versions",  HTTP_GET,    handleOtaVersions);

    httpServer.on("/ota/install",   HTTP_POST, [](){
        int slot = httpServer.arg("slot").toInt();
        if (slot < 0 || slot >= otaSlotCount) {
            httpServer.send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid slot\"}");
            return;
        }
        otaInstallPending = true;
        otaInstallIdx     = slot;
        httpServer.sendHeader("Connection", "close");
        httpServer.send(200, "application/json", "{\"ok\":true}");
    });

    httpServer.on("/ota/upload", HTTP_POST,
        []() {
            // Completion
            if (otaUploadOk && otaUploadBytes > 0) {
                otaRotateSlots();
                String tmp = String(OTA_DIR) + "/upload.tmp";
                String fw0 = String(OTA_DIR) + "/fw0.bin";
                if (LittleFS.exists(fw0)) LittleFS.remove(fw0);
                LittleFS.rename(tmp, fw0);

                // Rotate manifest entries
                for (int i = min(otaSlotCount, OTA_MAX_SLOTS - 1); i > 0; i--)
                    otaSlots[i] = otaSlots[i - 1];
                String label = httpServer.arg("name").length()
                               ? httpServer.arg("name") : "firmware.bin";
                otaSlots[0] = { label, otaUploadBytes, "just now" };
                if (otaSlotCount < OTA_MAX_SLOTS) otaSlotCount++;
                otaSaveManifest();

                httpServer.sendHeader("Access-Control-Allow-Origin", "*");
                httpServer.send(200, "application/json", "{\"ok\":true}");
            } else {
                LittleFS.remove(String(OTA_DIR) + "/upload.tmp");
                httpServer.send(500, "application/json", "{\"ok\":false,\"error\":\"Upload failed\"}");
            }
        },
        []() {
            // Chunked upload
            HTTPUpload& up = httpServer.upload();
            if (up.status == UPLOAD_FILE_START) {
                otaUploadOk    = false;
                otaUploadBytes = 0;
                if (!LittleFS.exists(OTA_DIR)) LittleFS.mkdir(OTA_DIR);
                String tmp = String(OTA_DIR) + "/upload.tmp";
                if (LittleFS.exists(tmp)) LittleFS.remove(tmp);
                otaUploadFile  = LittleFS.open(tmp, "w");
                Serial.printf("OTA upload: %s\n", up.filename.c_str());
            } else if (up.status == UPLOAD_FILE_WRITE) {
                if (otaUploadFile && (otaUploadBytes + up.currentSize) <= OTA_MAX_FW_BYTES) {
                    otaUploadFile.write(up.buf, up.currentSize);
                    otaUploadBytes += up.currentSize;
                } else if (otaUploadBytes + up.currentSize > OTA_MAX_FW_BYTES) {
                    // Oversized — abort
                    if (otaUploadFile) { otaUploadFile.close(); }
                    LittleFS.remove(String(OTA_DIR) + "/upload.tmp");
                    Serial.println("OTA upload: file too large");
                }
            } else if (up.status == UPLOAD_FILE_END) {
                if (otaUploadFile) {
                    otaUploadFile.close();
                    otaUploadOk = (otaUploadBytes > 0);
                    Serial.printf("OTA upload: %u bytes OK=%d\n", otaUploadBytes, otaUploadOk);
                }
            }
        }
    );

    // HTML UI update (legacy)
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

    // Deferred OTA install — response was already sent
    if (otaInstallPending) {
        otaInstallPending = false;
        if (otaInstallFromFS(otaInstallIdx)) {
            delay(200);
            esp_restart();
        }
    }
}

void WebServer_BroadcastState(const String &json)
{
    lastJsonState = json;
    wsServer.broadcastTXT(json.c_str());
}

String WebServer_GetLocalIP()
{
    if (WiFi.status() == WL_CONNECTED)
        return WiFi.localIP().toString();
    return WiFi.softAPIP().toString();
}

// ── WebSocket ─────────────────────────────────────────────────────
static void webSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length)
{
    if (type == WStype_CONNECTED) {
        if (lastJsonState.length() > 2)
            wsServer.sendTXT(num, lastJsonState.c_str());
    } else if (type == WStype_TEXT) {
        String cmd = String((char*)payload).substring(0, length);
        Serial.printf("WS cmd: %s\n", cmd.c_str());
    }
}

// ── OTA page handlers ─────────────────────────────────────────────
static void handleOtaPage()
{
    httpServer.send_P(200, "text/html", OTA_HTML);
}

static void handleOtaVersions()
{
    // Check which slot was last installed
    int lastSlot = -1;
    if (LittleFS.exists(OTA_DIR "/last_slot.txt")) {
        File f = LittleFS.open(OTA_DIR "/last_slot.txt", "r");
        if (f) { lastSlot = f.readString().toInt(); f.close(); }
    }

    String json = "[";
    for (int i = 0; i < otaSlotCount; i++) {
        if (i > 0) json += ",";
        json += "{\"label\":\"" + otaSlots[i].label + "\",";
        json += "\"size\":"    + String(otaSlots[i].size) + ",";
        json += "\"date\":\""  + otaSlots[i].date + "\",";
        json += "\"current\":" + String(i == lastSlot ? "true" : "false") + "}";
    }
    json += "]";
    httpServer.sendHeader("Access-Control-Allow-Origin", "*");
    httpServer.send(200, "application/json", json);
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

// ── REST handlers ─────────────────────────────────────────────────
static void handleRoot()
{
    if (LittleFS.exists("/index.html")) {
        File f = LittleFS.open("/index.html", "r");
        httpServer.streamFile(f, "text/html");
        f.close();
    } else {
        httpServer.send(200, "text/html",
            "<h2>MCP Fuel Station V2</h2>"
            "<p><a href='/ota'>OTA Firmware Update</a></p>");
    }
}

static void handleGetModels()
{
    String json = "[";
    for (int i = 0; i < numModels; i++) {
        if (i > 0) json += ",";
        HeliModel &m = heliModels[i];
        json += "{\"name\":\"" + String(m.name) + "\","
                "\"tankVol\":"    + m.tankVolumeMl  + ","
                "\"sensor\":"     + (m.hasTankSensor ? "true" : "false") + ","
                "\"fillSpd\":"    + m.fillSpeed      + ","
                "\"drainSpd\":"   + m.drainSpeed     + ","
                "\"purge\":"      + m.purgeSecs      + ","
                "\"totalFills\":" + m.totalFills     + ","
                "\"totalDrains\":" + m.totalDrains   + ","
                "\"hasImage\":"   + (m.hasImage ? "true" : "false") + ","
                "\"active\":"     + (i == activeModelIndex ? "true" : "false") + "}";
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
