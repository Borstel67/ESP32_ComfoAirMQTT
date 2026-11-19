// SPDX-License-Identifier: MIT
// Gemeinsame Utilities für AP- und STA-Manager:
// - IP-Parsing und STA-Netzkonfiguration aus /config.json (NVS)
// - Credential-Storage (load/erase) und config.json-Zugriff in NVS
// - NTP-Synchronisation mit Fallback-Servern
// - File-Streamer von LittleFS für den AsyncWebServer
// - Manual-UI- und MQTT-Einstellungen
#pragma once
#include <Arduino.h>
#include <FS.h>
#include <LittleFS.h>
#include <IPAddress.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <functional>

// IP-Parsing aus Text ("a.b.c.d") nach IPAddress
bool parseIpText(const String& text, IPAddress& out);

// STA-Konfiguration aus /config.json anwenden (DHCP oder statische IP)
void applyStaConfigFromWifiJson();

// Zugangsdaten (SSID/PW) laden/löschen in NVS (/config.json)
void loadCredentials(String& ssid, String& password);
void eraseCredentials();

// NVS-Storage für komplette Config (JSON-String)
bool loadConfigJson(String& jsonOut);
bool saveConfigJson(const String& jsonText);
void eraseConfigJson();

// Lädt config.json, wendet 'mutate' auf das Dokument an und schreibt es zurück.
// Geschrieben wird nur, wenn sich der Inhalt geändert hat (schont den Flash).
// Thread-sicher: serialisiert Read-Modify-Write zwischen loop() und AsyncTCP-Task.
bool updateConfigJson(const std::function<void(JsonDocument&)>& mutate);

// NTP-Sync mit Standard- oder in /config.json hinterlegten Servern
bool syncTimeDefault(uint8_t tries = 20);

// ArduinoOTA: Lazy-Init/Handler
void ensureArduinoOta(const String& hostname, uint16_t port, const String& password);
void handleArduinoOta();
bool isArduinoOtaStarted();

// OTA-Rollback: Eine per OTA eingespielte Firmware startet "auf Probe" (PENDING_VERIFY).
// - Stürzt sie ab oder startet neu, bevor sie bestätigt wurde, bootet der Bootloader
//   automatisch wieder die vorherige Firmware.
// - otaRollbackService() bestätigt sie, sobald 'healthy' OTA_CONFIRM_STABLE_MS am Stück gilt,
//   und löst nach OTA_ROLLBACK_TIMEOUT_MS ohne Bestätigung selbst den Rollback aus.
constexpr uint32_t OTA_CONFIRM_STABLE_MS   = 30000;
constexpr uint32_t OTA_ROLLBACK_TIMEOUT_MS = 300000;
bool otaRollbackPending();              // true, solange die laufende Firmware noch unbestätigt ist
void otaRollbackService(bool healthy);  // zyklisch aus loop() aufrufen

// AsyncWebServer-Variante
bool serveFileFromLittleFS(AsyncWebServerRequest* request, const String& path, const String& contentType, bool cacheable = false);

struct ManualUiCfg {
    int delaySeconds = 0; // testAutoStopSec
    int supply = 0;       // manualAnalogSupply
    int exhaust = 0;      // manualAnalogExhaust
    int postheater = 0;   // manualAnalogPostheater
};

int clampInt(int v, int lo, int hi);
String normalizeHostname(const String& raw);

bool loadManualUiCfg(ManualUiCfg& cfg);
bool saveManualUiCfg(const ManualUiCfg& cfg);

int loadTestAutoStopSec();
bool saveTestAutoStopSec(int sec);

// MQTT-Einstellungen aus config.json (gemeinsam für Sketch, AP- und STA-Webserver)
struct MqttCfg {
    bool   enabled = true;
    String host;
    int    port = 1883;
    String user;
    String pass;
    String clientId = "ComfoAirESP32";
    String topicPrefix = "comfoair";
};

// Lädt MQTT-Werte; nicht vorhandene Keys behalten den Wert aus 'c'. false nur bei defektem JSON.
bool loadMqttCfg(MqttCfg& c);
bool saveMqttCfg(const MqttCfg& c);

// Entfernt Leerraum und führende/abschließende '/'; leer -> "comfoair".
String normalizeTopicPrefix(String prefix);

// "1"/"true"/"on"/"yes" (Case-insensitive) -> true
bool toBoolArg(const String& s);
