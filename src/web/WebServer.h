#pragma once
#include <Arduino.h>

// ═══════════════════════════════════════════════════════════════════
// MCP Fuel Station V2 — Web Server
// Ported directly from V1 esp32_fuel_station/src/main.cpp
// Wi-Fi AP (+ optional home WiFi), REST API, WebSocket live data
// mDNS: http://fuelstation.local
// ═══════════════════════════════════════════════════════════════════

// ── Wi-Fi ─────────────────────────────────────────────────────────
#define AP_SSID  "MCP-FuelStation-V2"
#define AP_PASS  "fuelpump1"
// Home WiFi credentials are loaded from /wifi.json at runtime.

// ── OTA ───────────────────────────────────────────────────────────
#define OTA_MAX_FW_BYTES  (2 * 1024 * 1024)   // 2 MB upload limit
#define OTA_MAX_SLOTS     3

void WebServer_Init();
void WebServer_Update();                        // call every loop
void WebServer_BroadcastState(const String &json);
String WebServer_GetLocalIP();
void WebServer_SetCommandHandler(void (*fn)(const String &cmd));
String WebServer_GetSavedSSID();                // "" if not configured
