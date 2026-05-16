#include "WebServer.h"
#include "../heli/HeliLib.h"
#include "../log/Logger.h"
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
static void           (*sCmdHandler)(const String&) = nullptr;

void WebServer_SetCommandHandler(void (*fn)(const String& cmd)) { sCmdHandler = fn; }

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

// ── Filesystem file upload state ─────────────────────────────────
static File     fsUploadFile;
static bool     fsUploadOk        = false;

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
    Logger_Write(LOG_INFO, CAT_SYSTEM, "OTA_INSTALL_START", path.c_str(), (float)fsize);

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
    Logger_Write(LOG_INFO, CAT_SYSTEM, "OTA_INSTALL_OK", nullptr, (float)slot);
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
    Serial.printf("AP: %s  IP: %s\n", AP_SSID, WiFi.softAPIP().toString().c_str());

    // Home WiFi — start async; caller checks WL_CONNECTED in loop()
    if (strlen(HOME_SSID) > 0) {
        Serial.printf("Connecting to %s (async)\n", HOME_SSID);
        WiFi.begin(HOME_SSID, HOME_PASS);
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

    httpServer.on("/logo.png", HTTP_GET, [](){
        File f = LittleFS.open("/logo.png", "r");
        if (!f) { httpServer.send(404, "text/plain", "not found"); return; }
        httpServer.sendHeader("Cache-Control", "max-age=3600");
        httpServer.streamFile(f, "image/png");
        f.close();
    });

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
                    Logger_Write(otaUploadOk ? LOG_INFO : LOG_ERROR,
                                 CAT_SYSTEM, "OTA_UPLOAD",
                                 up.filename.c_str(), (float)otaUploadBytes);
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

    // Generic web-file OTA: POST /fs/upload with multipart field "file"
    // Filename from upload header determines LittleFS path (must not start /ota/)
    httpServer.on("/fs/upload", HTTP_POST,
        []() {
            if (fsUploadOk)
                httpServer.send(200, "application/json", "{\"ok\":true}");
            else
                httpServer.send(500, "application/json", "{\"ok\":false,\"error\":\"Upload failed\"}");
        },
        []() {
            HTTPUpload& up = httpServer.upload();
            if (up.status == UPLOAD_FILE_START) {
                fsUploadOk = false;
                String path = up.filename;
                if (!path.startsWith("/")) path = "/" + path;
                if (path.startsWith("/ota/")) return;   // protect OTA dir
                if (LittleFS.exists(path)) LittleFS.remove(path);
                fsUploadFile = LittleFS.open(path, "w");
                Serial.printf("FS upload: %s\n", path.c_str());
            } else if (up.status == UPLOAD_FILE_WRITE) {
                if (fsUploadFile) fsUploadFile.write(up.buf, up.currentSize);
            } else if (up.status == UPLOAD_FILE_END) {
                if (fsUploadFile) { fsUploadFile.close(); fsUploadOk = true; }
                Serial.printf("FS upload done: %u bytes\n", up.totalSize);
            }
        }
    );

    httpServer.on("/debug/fs", HTTP_GET, [](){
        String out;
        File root = LittleFS.open("/");
        File f    = root.openNextFile();
        while (f) { out += String(f.name()) + " (" + f.size() + ")\n"; f = root.openNextFile(); }
        httpServer.send(200, "text/plain", out);
    });

    // ── Log viewer ────────────────────────────────────────────────
    static const char LOG_PAGE[] =
        "<!DOCTYPE html><html lang='en'><head>"
        "<meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>MCP Fuel Station &mdash; Log</title><style>"
        "*{box-sizing:border-box;margin:0;padding:0}"
        "body{font-family:system-ui,sans-serif;font-size:14px;background:#f0f2f5}"
        "header{background:#c0392b;color:#fff;padding:11px 16px;display:flex;justify-content:space-between;align-items:center;flex-wrap:wrap;gap:6px}"
        "header h1{font-size:16px;font-weight:700}"
        "#st{font-size:12px;opacity:.85}"
        ".ib{background:#2c3e50;color:#ecf0f1;padding:6px 16px;font-size:12px;display:flex;gap:16px;flex-wrap:wrap}"
        ".ib strong{color:#fff}"
        ".ct{padding:8px 16px;background:#fff;border-bottom:1px solid #dde;display:flex;gap:7px;flex-wrap:wrap;align-items:center}"
        ".pl{padding:4px 12px;border:2px solid #bbb;border-radius:16px;cursor:pointer;font-size:12px;font-weight:600;background:#fff}"
        ".pl.on{background:#2980b9;border-color:#2980b9;color:#fff}"
        ".pl.w{border-color:#e67e22}.pl.w.on{background:#e67e22;border-color:#e67e22}"
        ".pl.e{border-color:#e74c3c}.pl.e.on{background:#e74c3c;border-color:#e74c3c}"
        ".pl.f{border-color:#8e44ad}.pl.f.on{background:#8e44ad;border-color:#8e44ad}"
        "input[type=text]{padding:5px 9px;border:1px solid #ccc;border-radius:4px;font-size:13px;min-width:100px;flex:1;max-width:220px}"
        ".btn{padding:6px 13px;border:none;border-radius:5px;cursor:pointer;font-size:12px;font-weight:600;text-decoration:none;display:inline-block}"
        ".g{background:#27ae60;color:#fff}.b{background:#1a6b3a;color:#fff}.r{background:#c0392b;color:#fff}.d{background:#555;color:#fff}"
        ".wr{overflow-x:auto;padding:0 8px 24px}"
        "table{width:100%;border-collapse:collapse;margin-top:6px;font-size:12px}"
        "th{background:#2c3e50;color:#ecf0f1;padding:7px 9px;text-align:left;white-space:nowrap;font-size:11px;text-transform:uppercase;letter-spacing:.4px}"
        "td{padding:5px 9px;border-bottom:1px solid #eef;vertical-align:middle}"
        ".ri{background:#fff}.rw{background:#fffaee}.re{background:#fff2f2}.rf{background:#f9f2ff}"
        ".bk{display:inline-block;padding:2px 7px;border-radius:10px;font-size:10px;font-weight:800}"
        ".bi{background:#ecf0f1;color:#7f8c8d}.bw{background:#f39c12;color:#fff}.be{background:#e74c3c;color:#fff}.bf{background:#8e44ad;color:#fff}"
        ".ct2{color:#95a5a6;font-size:11px}.ev{font-weight:600}.dt{color:#444;max-width:180px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}"
        ".nm{font-family:monospace;color:#2980b9}.ts{white-space:nowrap;font-family:monospace;font-size:11px;color:#666}"
        "@media(max-width:600px){th,td{padding:4px 6px}header h1{font-size:13px}}"
        "</style></head><body>"
        "<header><h1>&#9981; MCP Fuel Station &mdash; Event Log</h1><span id='st'>Connecting&hellip;</span></header>"
        "<div class='ib'>"
        "<span>Log: <strong id='fs'>&mdash;</strong></span>"
        "<span>Archive: <strong id='as'>&mdash;</strong></span>"
        "<span>Entries: <strong id='ec'>&mdash;</strong></span>"
        "<span>Showing: <strong id='sc'>&mdash;</strong></span>"
        "<span id='lt'></span></div>"
        "<div class='ct'>"
        "<span style='font-size:11px;font-weight:700;color:#888;text-transform:uppercase'>Filter:</span>"
        "<button class='pl on' onclick='lv(\"ALL\",this)'>All</button>"
        "<button class='pl' onclick='lv(\"INFO\",this)'>Info</button>"
        "<button class='pl w' onclick='lv(\"WARN\",this)'>Warn</button>"
        "<button class='pl e' onclick='lv(\"ERROR\",this)'>Error</button>"
        "<button class='pl f' onclick='lv(\"FAULT\",this)'>Fault</button>"
        "&nbsp;<input type='text' id='q' placeholder='&#128269; Search&hellip;' oninput='rnd()'>"
        "&nbsp;<a class='btn g' href='/api/log/download'>&#8615; Download Log</a>"
        "<a class='btn b' href='/api/log/download?archive=1'>&#8615; Archive</a>"
        "<button class='btn r' onclick='clr()'>&#128465; Clear</button>"
        "<button class='btn d' onclick='ld()'>&#8635; Refresh</button>"
        "&nbsp;<a class='btn' style='background:#34495e;color:#fff' href='/'>&#8592; Fuel Station</a>"
        "</div>"
        "<div class='wr'><table>"
        "<thead><tr><th>#</th><th>Timestamp</th><th>Level</th><th>Category</th><th>Event</th><th>Detail</th><th>Val 1</th><th>Val 2</th></tr></thead>"
        "<tbody id='tb'></tbody></table></div>"
        "<script>"
        "var rw=[],al='ALL';"
        "function parse(t){"
          "return t.split('\\n').filter(function(l){return l.trim()&&!/^Timestamp/.test(l);})"
          ".map(function(l){var c=pl(l);return{ts:c[0]||'',ep:c[1]||0,lv:c[2]||'INFO',cat:c[3]||'',ev:c[4]||'',dt:c[5]||'',v1:c[6]||'',v2:c[7]||''};}).reverse();}"
        "function pl(s){"
          "var r=[],c='',q=false;"
          "for(var i=0;i<s.length;i++){var x=s[i];if(x=='\"')q=!q;else if(x==','&&!q){r.push(c.trim());c='';}else c+=x;}"
          "r.push(c.trim());return r;}"
        "function lc(n){return{INFO:'bi',WARN:'bw',ERROR:'be',FAULT:'bf'}[n]||'bi';}"
        "function rc(n){return{INFO:'ri',WARN:'rw',ERROR:'re',FAULT:'rf'}[n]||'ri';}"
        "function lv(n,b){al=n;document.querySelectorAll('.pl').forEach(function(p){p.classList.remove('on');});b.classList.add('on');rnd();}"
        "function rnd(){"
          "var q=(document.getElementById('q').value||'').toLowerCase();"
          "var f=rw.filter(function(r){"
            "if(al!='ALL'&&r.lv!=al)return false;"
            "if(q&&(r.ts+r.lv+r.cat+r.ev+r.dt+r.v1+r.v2).toLowerCase().indexOf(q)<0)return false;"
            "return true;});"
          "document.getElementById('sc').textContent=f.length;"
          "var MAX=500,s=f.slice(0,MAX);"
          "document.getElementById('tb').innerHTML=s.map(function(r,i){"
            "return'<tr class=\"'+rc(r.lv)+'\"><td style=\"color:#bbb;font-size:10px\">'+(f.length-i)+'</td>'"
            "+'<td class=\"ts\">'+r.ts+'</td>'"
            "+'<td><span class=\"bk '+lc(r.lv)+'\">'+r.lv+'</span></td>'"
            "+'<td class=\"ct2\">'+r.cat+'</td>'"
            "+'<td class=\"ev\">'+r.ev+'</td>'"
            "+'<td class=\"dt\" title=\"'+r.dt+'\">'+r.dt+'</td>'"
            "+'<td class=\"nm\">'+r.v1+'</td>'"
            "+'<td class=\"nm\">'+r.v2+'</td></tr>';}).join('')"
          "+(f.length>MAX?'<tr><td colspan=\"8\" style=\"text-align:center;padding:10px;color:#888;font-size:12px\">Showing newest '+MAX+' of '+f.length+' &mdash; download CSV for full log</td></tr>':'');}"
        "function ld(){"
          "document.getElementById('st').textContent='Loading…';"
          "fetch('/api/log/data').then(function(r){return r.text();}).then(function(t){"
            "rw=parse(t);"
            "document.getElementById('fs').textContent=(t.length/1024).toFixed(1)+' KB';"
            "document.getElementById('ec').textContent=rw.length;"
            "if(rw.length)document.getElementById('lt').textContent='Latest: '+rw[0].ts;"
            "document.getElementById('st').textContent='Updated '+new Date().toLocaleTimeString();"
            "rnd();"
          "}).catch(function(e){document.getElementById('st').textContent='Error: '+e;});"
          "fetch('/api/log/archive_size').then(function(r){return r.json();}).then(function(d){document.getElementById('as').textContent=d.kb+' KB';}).catch(function(){document.getElementById('as').textContent='none';});}"
        "function clr(){"
          "if(!confirm('Delete ALL log entries? This cannot be undone.'))return;"
          "fetch('/api/log/clear',{method:'POST'}).then(function(r){return r.json();}).then(function(d){"
            "if(d.ok){rw=[];rnd();document.getElementById('st').textContent='Log cleared';"
            "document.getElementById('ec').textContent='0';document.getElementById('fs').textContent='0 KB';}});}"
        "ld();setInterval(ld,30000);"
        "</script></body></html>";

    httpServer.on("/log", HTTP_GET, []() {
        httpServer.send(200, "text/html", LOG_PAGE);
    });

    httpServer.on("/api/log/data", HTTP_GET, []() {
        File f = LittleFS.open("/logs/log.csv", "r");
        if (!f) { httpServer.send(200, "text/plain", "Timestamp,Epoch,Level,Category,Event,Detail,Val1,Val2\n"); return; }
        httpServer.sendHeader("Cache-Control", "no-store");
        httpServer.streamFile(f, "text/plain");
        f.close();
    });

    httpServer.on("/api/log/download", HTTP_GET, []() {
        bool archive = httpServer.hasArg("archive");
        const char* path = archive ? "/logs/log_old.csv" : "/logs/log.csv";
        File f = LittleFS.open(path, "r");
        if (!f) { httpServer.send(404, "text/plain", "no log"); return; }
        const char* fname = archive ? "fuelstation_log_archive.csv" : "fuelstation_log.csv";
        char cd[64];
        snprintf(cd, sizeof(cd), "attachment; filename=\"%s\"", fname);
        httpServer.sendHeader("Content-Disposition", cd);
        httpServer.streamFile(f, "text/csv");
        f.close();
    });

    httpServer.on("/api/log/archive_size", HTTP_GET, []() {
        File f = LittleFS.open("/logs/log_old.csv", "r");
        char buf[32];
        if (f) {
            snprintf(buf, sizeof(buf), "{\"kb\":%.1f}", f.size() / 1024.0f);
            f.close();
        } else {
            strncpy(buf, "{\"kb\":0}", sizeof(buf));
        }
        httpServer.send(200, "application/json", buf);
    });

    httpServer.on("/api/log/clear", HTTP_POST, []() {
        LittleFS.remove("/logs/log.csv");
        LittleFS.remove("/logs/log_old.csv");
        Logger_Init();   // recreate file with fresh header
        httpServer.send(200, "application/json", "{\"ok\":true}");
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
        char netJson[72];
        if (WiFi.status() == WL_CONNECTED)
            snprintf(netJson, sizeof(netJson), "{\"homeIP\":\"%s\"}", WiFi.localIP().toString().c_str());
        else
            strncpy(netJson, "{\"homeIP\":\"\"}", sizeof(netJson));
        wsServer.sendTXT(num, netJson);
    } else if (type == WStype_TEXT) {
        String cmd = String((char*)payload).substring(0, length);
        Serial.printf("WS cmd: %s\n", cmd.c_str());
        if (sCmdHandler) sCmdHandler(cmd);
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
        httpServer.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
        httpServer.sendHeader("Pragma", "no-cache");
        httpServer.streamFile(f, "text/html");
        f.close();
    } else {
        httpServer.send(200, "text/html",
            "<!DOCTYPE html><html><head>"
            "<meta charset='utf-8'>"
            "<meta name='viewport' content='width=device-width,initial-scale=1'>"
            "<title>MCP Fuel Station</title>"
            "<style>"
            "*{box-sizing:border-box;margin:0;padding:0}"
            "body{font-family:system-ui,sans-serif;background:#1a1a2e;color:#fff;"
                 "display:flex;flex-direction:column;align-items:center;justify-content:center;"
                 "min-height:100vh;padding:24px}"
            "h1{color:#e74c3c;font-size:26px;margin-bottom:6px}"
            ".sub{color:#7f8c8d;font-size:13px;margin-bottom:36px}"
            ".btn{display:block;width:260px;padding:15px 0;border-radius:8px;"
                 "text-decoration:none;font-size:16px;font-weight:600;"
                 "text-align:center;margin:10px 0}"
            ".ota{background:#c0392b;color:#fff}"
            ".log{background:#27ae60;color:#fff}"
            "</style></head><body>"
            "<h1>&#9981; MCP Fuel Station</h1>"
            "<p class='sub'>" FW_VERSION "</p>"
            "<a class='btn ota' href='/ota'>&#8679; OTA Firmware Update</a>"
            "<a class='btn log' href='/log'>&#128203; Event Log</a>"
            "</body></html>");
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
