#pragma once
#include <Arduino.h>

// ═══════════════════════════════════════════════════════════════════
// MCO Fuel Station V2 — Web Server
// Ported directly from V1 esp32_fuel_station/src/main.cpp
// Wi-Fi AP (+ optional home WiFi), REST API, WebSocket live data
// mDNS: http://fuelstation.local
// ═══════════════════════════════════════════════════════════════════

// ── Wi-Fi ─────────────────────────────────────────────────────────
#define AP_SSID  "MCP-FuelStation-V2"
#define AP_PASS  "fuelpump1"
// Home WiFi for development — leave blank for field use
#define HOME_SSID  ""
#define HOME_PASS  ""

void WebServer_Init();
void WebServer_Update();                        // call every loop
void WebServer_BroadcastState(const String &json);
