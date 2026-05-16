#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <LittleFS.h>

// ── Model management ──────────────────────────────────────────────────────────
#define MAX_MODELS       20
#define MODEL_NAME_LEN   24
#define THUMB_W          320
#define THUMB_H          220

struct ESP32Model {
  char     name[MODEL_NAME_LEN];
  int      tankVolumeMl;
  bool     hasTankSensor;
  int      fillSpeed;
  int      drainSpeed;
  int      purgeSecs;
  uint32_t totalFills;
  uint32_t totalDrains;
  uint32_t totalFillMl;
  uint32_t totalDrainMl;
  bool     hasImage;
  bool     archived;
};

ESP32Model esp32Models[MAX_MODELS];
int        esp32NumModels  = 0;
int        esp32ActiveIdx  = 0;

// ── WiFi credentials ──────────────────────────────────────────────────────────
#define AP_SSID     "MCP-FuelStation"
#define AP_PASS     "fuelpump1"
// Home network for development — comment out for field use
#define HOME_SSID   "SilverLining"
#define HOME_PASS   "KLHj0b01"
#define USE_HOME_WIFI true   // tries home WiFi but AP always runs regardless

// ── Serial to Teensy ──────────────────────────────────────────────────────────
#define TEENSY_SERIAL   Serial1
#define TEENSY_BAUD     115200
#define TEENSY_RX_PIN   16
#define TEENSY_TX_PIN   17

// ── Servers ───────────────────────────────────────────────────────────────────
WebServer         httpServer(80);
WebSocketsServer  wsServer(81);
WiFiServer        imgServer(82);  // Raw TCP for image uploads

// Increase body size limit for image uploads (100KB)
#define HTTP_UPLOAD_BUFLEN 100*1024

// ── State ─────────────────────────────────────────────────────────────────────
String lastJsonState = "{}";
uint8_t connectedClients = 0;

// ── Forward declarations ──────────────────────────────────────────────────────
void handleRoot();
void handleNotFound();
void handleGetModels();
void handleGetImage();
void handlePostModel();
void handlePostImage();
void handleDeleteModel();
void handleImgChunk();
static void mkdirp(const char* path);


void handleGetStation();
void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length);
void processTeensyLine(const String &line);
void broadcastState(const String &json);
void loadModelsFromFS();
void saveModelToFS(int idx);
void saveActiveIdx();
void syncModelToTeen(int idx);
void deleteModelFiles(const char* name);

// ── HTML page served from LittleFS /index.html ───────────────────────────────

// =============================================================================
void setup()
{
  Serial.begin(115200);
  delay(500);
  Serial.println("\nMCP Fuel Station ESP32 Bridge starting...");

  // LittleFS — don't format on fail to preserve stored models
  if (!LittleFS.begin(false))
  {
    Serial.println("LittleFS mount failed - trying format once");
    if (!LittleFS.begin(true))
      Serial.println("LittleFS format failed!");
    else
      Serial.println("LittleFS formatted and mounted");
  }
  else
    Serial.println("LittleFS mounted");

  // Ensure /models directory exists
  if (!LittleFS.exists("/models")) LittleFS.mkdir("/models");

  // Load models from filesystem
  loadModelsFromFS();

  // Teensy serial
  TEENSY_SERIAL.begin(TEENSY_BAUD, SERIAL_8N1, TEENSY_RX_PIN, TEENSY_TX_PIN);

  // WiFi — always start AP, optionally also connect to home network (AP+STA mode)
  WiFi.mode(WIFI_AP_STA);

  // Always start the AP so phone can always connect
  WiFi.softAP(AP_SSID, AP_PASS);
  delay(500);  // let AP settle before starting STA
  Serial.printf("AP started: %s  IP: %s\n", AP_SSID, WiFi.softAPIP().toString().c_str());

  // Also try home WiFi if enabled — gives phone internet access at home
  if (USE_HOME_WIFI)
  {
    Serial.printf("Connecting to home WiFi: %s...\n", HOME_SSID);
    WiFi.begin(HOME_SSID, HOME_PASS);
    int retries = 0;
    while (WiFi.status() != WL_CONNECTED && retries < 20)
    {
      delay(500);
      Serial.print(".");
      retries++;
    }
    Serial.println();
    if (WiFi.status() == WL_CONNECTED)
    {
      Serial.printf("Home WiFi connected! IP: %s\n", WiFi.localIP().toString().c_str());
      Serial.printf("Access from home network: http://%s\n", WiFi.localIP().toString().c_str());
    }
    else
      Serial.println("Home WiFi not found — AP only mode");
  }

  // HTTP server
  httpServer.on("/", handleRoot);
  // OTA update for index.html — POST new HTML to /update-html
  httpServer.on("/update-html", HTTP_POST, [](){
    httpServer.sendHeader("Access-Control-Allow-Origin", "*");
    httpServer.send(200, "application/json", "{\"ok\":true}");
  }, [](){
    HTTPUpload& upload = httpServer.upload();
    static File updateFile;
    if (upload.status == UPLOAD_FILE_START) {
      updateFile = LittleFS.open("/index.html", "w");
      Serial.println("HTML update started");
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      if (updateFile) updateFile.write(upload.buf, upload.currentSize);
    } else if (upload.status == UPLOAD_FILE_END) {
      if (updateFile) { updateFile.close(); Serial.printf("HTML update done: %d bytes\n", upload.totalSize); }
    }
  });
  httpServer.on("/api/models",        HTTP_GET,    handleGetModels);
  httpServer.on("/debug/fs",          HTTP_GET,    [](){
    String out = "";
    File root = LittleFS.open("/");
    File f = root.openNextFile();
    while(f){ out += String(f.name()) + " (" + f.size() + " bytes)\n"; f = root.openNextFile(); }
    httpServer.send(200, "text/plain", out);
  });
  httpServer.on("/debug/format",      HTTP_GET,    [](){
    LittleFS.format();
    LittleFS.begin();
    LittleFS.mkdir("/models");
    esp32NumModels = 0;
    esp32ActiveIdx = 0;
    httpServer.send(200, "text/plain", "LittleFS formatted and remounted");
  });
  httpServer.on("/api/model",         HTTP_POST,   handlePostModel);
  httpServer.on("/api/model",         HTTP_DELETE, handleDeleteModel);
  httpServer.on("/api/station",       HTTP_GET,    handleGetStation);
  httpServer.on("/image",             HTTP_GET,    handleGetImage);
  httpServer.on("/image",             HTTP_POST,   handlePostImage);
  httpServer.on("/imgchunk",          HTTP_POST,   handleImgChunk);
  httpServer.onNotFound(handleNotFound);
  // Collect Content-Length header for image uploads
  const char* headerKeys[] = {"Content-Length", "Content-Type"};
  httpServer.collectHeaders(headerKeys, 2);
  httpServer.begin();
  Serial.println("HTTP server started on port 80");

  // mDNS — accessible as http://fuelstation.local
  if (MDNS.begin("fuelstation"))
    Serial.println("mDNS started: http://fuelstation.local");

  // WebSocket server
  wsServer.begin();
  wsServer.onEvent(webSocketEvent);
  Serial.println("WebSocket server started on port 81");

  // Raw TCP image upload server
  imgServer.begin();
  Serial.println("Image upload server started on port 82");
}

// =============================================================================
void loop()
{
  httpServer.handleClient();
  wsServer.loop();

  // Handle image upload TCP connections on port 82
  WiFiClient imgClient = imgServer.available();
  if (imgClient)
  {
    // Protocol: first line is "name=MODELNAME\n", then raw JPEG bytes
    uint32_t timeout = millis() + 5000;
    String nameLine = "";
    while (imgClient.connected() && millis() < timeout)
    {
      if (imgClient.available())
      {
        char c = imgClient.read();
        if (c == '\n') break;
        nameLine += c;
      }
    }
    nameLine.trim();
    if (nameLine.startsWith("name="))
    {
      String modelName = nameLine.substring(5);
      String dir = "/models/" + modelName;
      mkdirp(dir.c_str());
      String path = dir + "/thumb.jpg";
      File f = LittleFS.open(path, "w");
      if (f)
      {
        uint8_t buf[512];
        int total = 0;
        timeout = millis() + 5000;
        while (imgClient.connected() && millis() < timeout)
        {
          int avail = imgClient.available();
          if (avail > 0)
          {
            int rd = imgClient.read(buf, min(avail, (int)sizeof(buf)));
            if (rd > 0) { f.write(buf, rd); total += rd; timeout = millis() + 2000; }
          }
          else delay(1);
        }
        f.close();
        // Update hasImage
        for (int i = 0; i < esp32NumModels; i++)
          if (strcmp(esp32Models[i].name, modelName.c_str()) == 0)
            { esp32Models[i].hasImage = true; break; }
        Serial.printf("TCP Image saved: %s (%d bytes)\n", path.c_str(), total);
        imgClient.print("OK\n");
      }
      else imgClient.print("ERROR: could not open file\n");
    }
    imgClient.stop();
  }

  // Read lines from Teensy
  static String teensyBuf = "";
  while (TEENSY_SERIAL.available())
  {
    char c = TEENSY_SERIAL.read();
    if (c == '\n')
    {
      teensyBuf.trim();
      if (teensyBuf.length() > 0)
        processTeensyLine(teensyBuf);
      teensyBuf = "";
    }
    else
    {
      teensyBuf += c;
    }
  }
}

// =============================================================================
// Pending model sync from Teensy - variables declared globally above
void processTeensyLine(const String &line)
{
  if (line.startsWith("WS:"))
  {
    String json = line.substring(3);
    lastJsonState = json;
    broadcastState(json);
  }
}

// =============================================================================
void broadcastState(const String &json)
{
  wsServer.broadcastTXT(json.c_str());
}

// =============================================================================
void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length)
{
  switch (type)
  {
    case WStype_CONNECTED:
      connectedClients++;
      Serial.printf("WS client %d connected\n", num);
      // Send current state to new client
      if (lastJsonState.length() > 2)
        wsServer.sendTXT(num, lastJsonState.c_str());
      // Send network info
      {
        char netJson[128];
        if (WiFi.status() == WL_CONNECTED)
          snprintf(netJson, sizeof(netJson), "{\"homeIP\":\"%s\"}", WiFi.localIP().toString().c_str());
        else
          snprintf(netJson, sizeof(netJson), "{\"homeIP\":\"\"}");
        wsServer.sendTXT(num, netJson);
      }
      break;

    case WStype_DISCONNECTED:
      if (connectedClients > 0) connectedClients--;
      Serial.printf("WS client %d disconnected\n", num);
      break;

    case WStype_TEXT:
    {
      // Command from browser → forward to Teensy
      String cmd = String((char*)payload).substring(0, length);
      Serial.printf("WS cmd from client %d: %s\n", num, cmd.c_str());
      // Forward as CMD:value\n to Teensy
      TEENSY_SERIAL.println(cmd);
      break;
    }

    default:
      break;
  }
}

// Helper to create directories recursively
static void mkdirp(const char* path)
{
  char tmp[64];
  strncpy(tmp, path, sizeof(tmp) - 1);
  if (!LittleFS.exists("/models")) LittleFS.mkdir("/models");
  for (char* p = tmp + 1; *p; p++)
  {
    if (*p == '/')
    {
      *p = 0;
      if (!LittleFS.exists(tmp)) LittleFS.mkdir(tmp);
      *p = '/';
    }
  }
  if (!LittleFS.exists(tmp)) LittleFS.mkdir(tmp);
}

void loadModelsFromFS()
{
  esp32NumModels = 0;

  // Load active index
  if (LittleFS.exists("/activeModel.txt"))
  {
    File f = LittleFS.open("/activeModel.txt", "r");
    if (f) { esp32ActiveIdx = f.readStringUntil('\n').toInt(); f.close(); }
  }

  // Scan /models/ directory
  File root = LittleFS.open("/models");
  if (!root || !root.isDirectory()) return;

  File entry = root.openNextFile();
  while (entry && esp32NumModels < MAX_MODELS)
  {
    if (entry.isDirectory())
    {
      // Read config.json
      String cfgPath = String("/models/") + entry.name() + "/config.json";
      if (LittleFS.exists(cfgPath))
      {
        File cfg = LittleFS.open(cfgPath, "r");
        if (cfg)
        {
          StaticJsonDocument<512> doc;
          DeserializationError err = deserializeJson(doc, cfg);
          cfg.close();
          if (!err && !doc["archived"].as<bool>())
          {
            ESP32Model &m = esp32Models[esp32NumModels];
            strncpy(m.name,    doc["name"]   | entry.name(), MODEL_NAME_LEN - 1);
            m.tankVolumeMl     = doc["tankVol"]   | 2000;
            m.hasTankSensor    = doc["sensor"]    | false;
            m.fillSpeed        = doc["fillSpd"]   | 500;
            m.drainSpeed       = doc["drainSpd"]  | 500;
            m.purgeSecs        = doc["purge"]     | 3;
            m.totalFills       = doc["totalFills"]  | 0;
            m.totalDrains      = doc["totalDrains"] | 0;
            m.totalFillMl      = doc["totalFillMl"] | 0;
            m.totalDrainMl     = doc["totalDrainMl"]| 0;
            m.archived         = false;
            // Check for image
            String imgPath = String("/models/") + entry.name() + "/thumb.jpg";
            m.hasImage = LittleFS.exists(imgPath);
            esp32NumModels++;
          }
        }
      }
    }
    entry = root.openNextFile();
  }
  esp32ActiveIdx = constrain(esp32ActiveIdx, 0, max(0, esp32NumModels - 1));
  Serial.printf("LittleFS: loaded %d models\n", esp32NumModels);
}

void saveModelToFS(int idx)
{
  if (idx < 0 || idx > MAX_MODELS) return;
  ESP32Model &m = esp32Models[idx];

  // Create directory
  String dir = String("/models/") + m.name;
  mkdirp(dir.c_str());

  String cfgPath = dir + "/config.json";
  File f = LittleFS.open(cfgPath, "w");
  if (!f) return;

  StaticJsonDocument<512> doc;
  doc["name"]        = m.name;
  doc["tankVol"]     = m.tankVolumeMl;
  doc["sensor"]      = m.hasTankSensor;
  doc["fillSpd"]     = m.fillSpeed;
  doc["drainSpd"]    = m.drainSpeed;
  doc["purge"]       = m.purgeSecs;
  doc["totalFills"]  = m.totalFills;
  doc["totalDrains"] = m.totalDrains;
  doc["totalFillMl"] = m.totalFillMl;
  doc["totalDrainMl"]= m.totalDrainMl;
  doc["archived"]    = m.archived;
  serializeJson(doc, f);
  f.close();
}

void saveActiveIdx()
{
  File f = LittleFS.open("/activeModel.txt", "w");
  if (f) { f.println(esp32ActiveIdx); f.close(); }
}

void deleteModelFiles(const char* name)
{
  String dir = String("/models/") + name;
  // Mark as archived in config rather than deleting
  String cfgPath = dir + "/config.json";
  if (LittleFS.exists(cfgPath))
  {
    File f = LittleFS.open(cfgPath, "r");
    StaticJsonDocument<512> doc;
    deserializeJson(doc, f);
    f.close();
    doc["archived"] = true;
    File fw = LittleFS.open(cfgPath, "w");
    serializeJson(doc, fw);
    fw.close();
  }
}

void syncModelToTeen(int idx)
{
  if (idx < 0 || idx >= esp32NumModels) return;
  ESP32Model &m = esp32Models[idx];
  // Send model config to Teensy as a special command
  char buf[256];
  snprintf(buf, sizeof(buf),
    "MDLSYNC:%s,%d,%d,%d,%d,%d,%lu,%lu,%lu,%lu\n",
    m.name, m.tankVolumeMl, m.hasTankSensor ? 1 : 0,
    m.fillSpeed, m.drainSpeed, m.purgeSecs,
    (unsigned long)m.totalFills, (unsigned long)m.totalDrains,
    (unsigned long)m.totalFillMl, (unsigned long)m.totalDrainMl
  );
  TEENSY_SERIAL.print(buf);
}

// ── API handlers ──────────────────────────────────────────────────────────────

void handleGetModels()
{
  String json = "[";
  for (int i = 0; i < esp32NumModels; i++)
  {
    if (i > 0) json += ",";
    ESP32Model &m = esp32Models[i];
    json += "{\"name\":\"" + String(m.name) + "\","
            "\"tankVol\":" + m.tankVolumeMl + ","
            "\"sensor\":"  + (m.hasTankSensor ? "true" : "false") + ","
            "\"fillSpd\":" + m.fillSpeed + ","
            "\"drainSpd\":"+ m.drainSpeed + ","
            "\"purge\":"   + m.purgeSecs + ","
            "\"totalFills\":" + m.totalFills + ","
            "\"totalDrains\":" + m.totalDrains + ","
            "\"totalFillMl\":" + m.totalFillMl + ","
            "\"totalDrainMl\":" + m.totalDrainMl + ","
            "\"hasImage\":" + (m.hasImage ? "true" : "false") + ","
            "\"active\":"  + (i == esp32ActiveIdx ? "true" : "false") + "}";
  }
  json += "]";
  httpServer.sendHeader("Access-Control-Allow-Origin", "*");
  httpServer.send(200, "application/json", json);
}

void handleGetImage()
{
  String name = httpServer.arg("name");
  if (name.length() == 0) { httpServer.send(400, "text/plain", "Missing name"); return; }

  String path = "/models/" + name + "/thumb.jpg";
  if (!LittleFS.exists(path))
  {
    // Return placeholder SVG
    String svg = "<svg xmlns='http://www.w3.org/2000/svg' width='320' height='220'>"
                 "<rect width='320' height='220' fill='#111827'/>"
                 "<text x='160' y='100' text-anchor='middle' fill='#374151' font-size='48'>🚁</text>"
                 "<text x='160' y='150' text-anchor='middle' fill='#374151' font-size='16'>" + name + "</text>"
                 "</svg>";
    httpServer.sendHeader("Access-Control-Allow-Origin", "*");
    httpServer.send(200, "image/svg+xml", svg);
    return;
  }

  File f = LittleFS.open(path, "r");
  httpServer.sendHeader("Access-Control-Allow-Origin", "*");
  httpServer.streamFile(f, "image/jpeg");
  f.close();
}

void handlePostModel()
{
  if (!httpServer.hasArg("plain")) { httpServer.send(400, "text/plain", "No body"); return; }

  StaticJsonDocument<512> doc;
  DeserializationError err = deserializeJson(doc, httpServer.arg("plain"));
  if (err) { httpServer.send(400, "text/plain", "Bad JSON"); return; }

  const char* name = doc["name"] | "";
  if (strlen(name) == 0) { httpServer.send(400, "text/plain", "Missing name"); return; }

  // Find existing or create new
  int idx = -1;
  for (int i = 0; i < esp32NumModels; i++)
    if (strcmp(esp32Models[i].name, name) == 0) { idx = i; break; }

  if (idx < 0)
  {
    if (esp32NumModels >= MAX_MODELS) { httpServer.send(400, "text/plain", "Max models reached"); return; }
    idx = esp32NumModels++;
    memset(&esp32Models[idx], 0, sizeof(ESP32Model));
  }

  ESP32Model &m = esp32Models[idx];
  strncpy(m.name, name, MODEL_NAME_LEN - 1);
  m.tankVolumeMl  = doc["tankVol"]  | m.tankVolumeMl;
  m.hasTankSensor = doc["sensor"]   | m.hasTankSensor;
  m.fillSpeed     = doc["fillSpd"]  | m.fillSpeed;
  m.drainSpeed    = doc["drainSpd"] | m.drainSpeed;
  m.purgeSecs     = doc["purge"]    | m.purgeSecs;
  m.archived      = false;

  saveModelToFS(idx);
  syncModelToTeen(idx);

  httpServer.sendHeader("Access-Control-Allow-Origin", "*");
  httpServer.send(200, "application/json", "{\"ok\":true,\"idx\":" + String(idx) + "}");
}

void handlePostImage()
{
  String name = httpServer.arg("name");
  if (name.length() == 0)
  { httpServer.send(400, "text/plain", "Missing name"); return; }

  String dir = "/models/" + name;
  mkdirp(dir.c_str());
  String path = dir + "/thumb.jpg";

  // Body is base64-encoded JPEG — decode and write
  const String& b64 = httpServer.arg("plain");
  if (b64.length() == 0)
  { httpServer.send(400, "text/plain", "No image data"); return; }

  File f = LittleFS.open(path, "w");
  if (!f) { httpServer.send(500, "text/plain", "Could not open file"); return; }

  // Base64 decode
  const char* b64chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  auto b64val = [](char c) -> int {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
  };

  size_t written = 0;
  uint8_t buf[3];
  int blen = b64.length();
  for (int i = 0; i < blen; i += 4)
  {
    int v0 = b64val(b64[i]);
    int v1 = (i+1 < blen) ? b64val(b64[i+1]) : -1;
    int v2 = (i+2 < blen) ? b64val(b64[i+2]) : -1;
    int v3 = (i+3 < blen) ? b64val(b64[i+3]) : -1;
    if (v0 < 0 || v1 < 0) break;
    buf[0] = (v0 << 2) | (v1 >> 4);
    f.write(buf[0]); written++;
    if (v2 >= 0) { buf[1] = ((v1 & 0xF) << 4) | (v2 >> 2); f.write(buf[1]); written++; }
    if (v3 >= 0) { buf[2] = ((v2 & 0x3) << 6) | v3;        f.write(buf[2]); written++; }
  }
  f.close();

  for (int i = 0; i < esp32NumModels; i++)
    if (strcmp(esp32Models[i].name, name.c_str()) == 0)
      { esp32Models[i].hasImage = true; break; }

  Serial.printf("Image saved: %s (%d bytes)\n", path.c_str(), written);
  httpServer.sendHeader("Access-Control-Allow-Origin", "*");
  httpServer.send(200, "application/json", "{\"ok\":true}");
}

// ── Chunked image upload ──────────────────────────────────────────────────────
static int b64val(char c) {
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
  int written = 0;
  for (int i = 0; i < blen; i += 4)
  {
    int v0 = b64val(b64[i]);
    int v1 = (i+1 < blen) ? b64val(b64[i+1]) : -1;
    int v2 = (i+2 < blen) ? b64val(b64[i+2]) : -1;
    int v3 = (i+3 < blen) ? b64val(b64[i+3]) : -1;
    if (v0 < 0 || v1 < 0) break;
    uint8_t b0 = (v0 << 2) | (v1 >> 4); f.write(b0); written++;
    if (v2 >= 0) { uint8_t b1 = ((v1&0xF)<<4)|(v2>>2); f.write(b1); written++; }
    if (v3 >= 0) { uint8_t b2 = ((v2&0x3)<<6)|v3;      f.write(b2); written++; }
  }
  f.close();
}

void handleImgChunk()
{
  String name  = httpServer.arg("name");
  int chunkIdx = httpServer.arg("chunk").toInt();
  int total    = httpServer.arg("total").toInt();
  const String& body = httpServer.arg("plain");

  Serial.printf("ImgChunk: name=%s chunk=%d/%d bodyLen=%d\n", 
    name.c_str(), chunkIdx, total, body.length());

  if (name.length() == 0 || body.length() == 0)
  { httpServer.send(400, "text/plain", "Bad request"); return; }

  String dir  = "/models/" + name;
  String path = dir + "/thumb.jpg";
  mkdirp(dir.c_str());

  writeBase64ToFile(body, path, chunkIdx > 0);
  yield();  // allow HTTP server to process other requests

  if (chunkIdx == total - 1)
  {
    for (int i = 0; i < esp32NumModels; i++)
      if (strcmp(esp32Models[i].name, name.c_str()) == 0)
        { esp32Models[i].hasImage = true; break; }
    File dbg = LittleFS.open(path, "r");
    Serial.printf("Image complete: %s (%d bytes)\n", path.c_str(), dbg ? dbg.size() : 0);
    if (dbg) dbg.close();
  }

  httpServer.sendHeader("Access-Control-Allow-Origin", "*");
  httpServer.send(200, "application/json", "{\"ok\":true}");
}

void handleDeleteModel()
{
  String name = httpServer.arg("name");
  if (name.length() == 0) { httpServer.send(400, "text/plain", "Missing name"); return; }

  deleteModelFiles(name.c_str());

  // Remove from RAM array
  for (int i = 0; i < esp32NumModels; i++)
  {
    if (strcmp(esp32Models[i].name, name.c_str()) == 0)
    {
      // Shift array
      for (int j = i; j < esp32NumModels - 1; j++)
        esp32Models[j] = esp32Models[j + 1];
      esp32NumModels--;
      if (esp32ActiveIdx >= esp32NumModels) esp32ActiveIdx = max(0, esp32NumModels - 1);
      saveActiveIdx();
      break;
    }
  }

  // Tell Teensy to remove the model
  char buf[64];
  snprintf(buf, sizeof(buf), "MDLDEL:%s\n", name.c_str());
  TEENSY_SERIAL.print(buf);

  httpServer.sendHeader("Access-Control-Allow-Origin", "*");
  httpServer.send(200, "application/json", "{\"ok\":true}");
}

void handleGetStation()
{
  // Proxy station data from last JSON state
  httpServer.sendHeader("Access-Control-Allow-Origin", "*");
  httpServer.send(200, "application/json", lastJsonState);
}

// =============================================================================
void handleRoot()
{
  if (LittleFS.exists("/index.html"))
  {
    File f = LittleFS.open("/index.html", "r");
    httpServer.streamFile(f, "text/html");
    f.close();
  }
  else
  {
    httpServer.send(200, "text/html",
      "<h2>MCP Fuel Station</h2>"
      "<p>Upload filesystem image to load web app.</p>");
  }
}

void handleNotFound()
{
  httpServer.send(404, "text/plain", "Not found");
}

