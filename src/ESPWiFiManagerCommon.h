// SPDX-License-Identifier: MIT
// Gemeinsame Utilities für AP- und STA-Manager:
// - IP-Parsing und STA-Netzkonfiguration aus /config.json (NVS)
// - Credential-Storage (load/save/erase) in NVS
// - Geschützte JSON-Whitelist und rekursive Bereinigung (LittleFS)
// - NTP-Synchronisation mit Fallback-Servern
// - Gemeinsamer File-Streamer von LittleFS zu beliebigen WebServer-Instanzen
#pragma once
#include <Arduino.h>
#include <FS.h>
#include <LittleFS.h>
#include <vector>
#include <initializer_list>
#include <IPAddress.h>
#include <ESPAsyncWebServer.h>

constexpr uint16_t ELEGANT_OTA_PORT = 81;

// IP-Parsing aus Text ("a.b.c.d") nach IPAddress
bool parseIpText(const String& text, IPAddress& out);

// STA-Konfiguration aus /config.json anwenden (DHCP oder statische IP)
void applyStaConfigFromWifiJson();

// Zugangsdaten (SSID/PW) laden/speichern/löschen in NVS (/config.json)
void loadCredentials(String& ssid, String& password);
void saveCredentials(const String& ssid, const String& password);
void eraseCredentials();

// NVS-Storage für komplette Config (JSON-String)
bool loadConfigJson(String& jsonOut);
bool saveConfigJson(const String& jsonText);
void eraseConfigJson();

// Geschützte JSON-Dateien setzen/prüfen und .json rekursiv löschen (unter Beachtung der Whitelist)
void setProtectedJsons(std::initializer_list<const char*> names);
bool isProtectedJson(const String& name);
void eraseJsonInDir(const char* dirPath);

// NTP-Sync mit Standard- oder in /config.json hinterlegten Servern
bool syncTimeDefault(uint8_t tries = 20);

// ArduinoOTA: Lazy-Init/Handler sowie Partition-Check
void ensureArduinoOta(const String& hostname, uint16_t port, const String& password);
void handleArduinoOta();
bool checkOtaAndDataPartitions(bool logDetails = false);

// Gemeinsames Streaming einer Datei aus LittleFS über einen WebServer; optional mit Cache-Header
template<typename TServer>
bool serveFileFromLittleFS(TServer& server, const String& path, const String& contentType, bool cacheable = false) {
	if (!LittleFS.exists(path)) return false;
	File file = LittleFS.open(path, "r");
	if (!file || file.isDirectory()) return false;

	if (cacheable) server.sendHeader("Cache-Control", "public, max-age=86400");
	else           server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");

	server.streamFile(file, contentType);
	file.close();
	return true;
}

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
