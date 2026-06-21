// SPDX-License-Identifier: MIT
// ESPWiFiManagerCommon.cpp
// Gemeinsame Hilfsfunktionen für AP- und STA-Manager:
// - NVS-Zugriff (config.json als JSON-String)
// - IP-Parsing und WiFi-Konfiguration
// - Credential-Verwaltung
// - JSON-Datei-Whitelist und rekursives Löschen
// - NTP-Synchronisation
// - ArduinoOTA (Lazy-Init)
// - Partition-Check (OTA + Dateisystem)
// - ManualUiCfg und normalizeHostname (gemeinsam für AP + STA)
#include "ESPWiFiManagerCommon.h"
#include <ArduinoJson.h>
#include <time.h>
#include <WiFi.h> // nur ESP32
#include <ArduinoOTA.h>
#include <esp_partition.h>
#include <esp_ota_ops.h>
#include <Preferences.h>

// Exakte Whitelist-Einträge für geschützte JSON-Dateien
static std::vector<String> _protectedExact;
static bool     _otaStarted = false;
static uint16_t _otaPort    = 3232;

static const char* kNvsNs  = "awm";
static const char* kNvsKey = "config";

// NVS-Helper
bool loadConfigJson(String& jsonOut) {
	// Liest die config.json aus NVS (Namespace "awm", Key "config").
	Preferences p;
	if (!p.begin(kNvsNs, true)) return false;
	jsonOut = p.getString(kNvsKey, "");
	p.end();
	return jsonOut.length() > 0;
}
bool saveConfigJson(const String& jsonText) {
	// Schreibt config.json ins NVS; leerer String gilt als Erfolg.
	Preferences p;
	if (!p.begin(kNvsNs, false)) return false;
	size_t w = p.putString(kNvsKey, jsonText);
	p.end();
	return (w > 0) || jsonText.isEmpty();
}
void eraseConfigJson() {
	// Löscht den config.json-Eintrag aus dem NVS.
	Preferences p;
	if (p.begin(kNvsNs, false)) {
		p.remove(kNvsKey);
		p.end();
	}
}

// Wandelt "a.b.c.d" nach IPAddress; false bei ungültigem Format
bool parseIpText(const String& text, IPAddress& out) {
	// Parst "a.b.c.d" nach IPAddress; false bei ungültigem Format.
	int p[4] = {0,0,0,0};
	if (sscanf(text.c_str(), "%d.%d.%d.%d", &p[0], &p[1], &p[2], &p[3]) != 4) return false;
	out = IPAddress(p[0], p[1], p[2], p[3]);
	return true;
}

// Liest /config.json und setzt WiFi.config auf DHCP oder statisch; bei Fehler Fallback auf DHCP
void applyStaConfigFromWifiJson() {
	// Liest DHCP/IP-Konfiguration aus config.json und wendet sie an.
	// Fallback auf DHCP bei Fehler oder fehlenden Werten.
	bool dhcp = true;
	IPAddress ip(0,0,0,0), gw(0,0,0,0), sn(0,0,0,0), dns1(0,0,0,0), dns2(0,0,0,0);

	String json;
	if (loadConfigJson(json)) {
		StaticJsonDocument<320> doc;
		if (!deserializeJson(doc, json)) {
			dhcp = doc["dhcp"] | true;
			parseIpText(String((const char*)(doc["ip"]   | "")), ip);
			parseIpText(String((const char*)(doc["gw"]   | "")), gw);
			parseIpText(String((const char*)(doc["sn"]   | "")), sn);
			parseIpText(String((const char*)(doc["dns1"] | "")), dns1);
			parseIpText(String((const char*)(doc["dns2"] | "")), dns2);
		}
	}

	if (dhcp) {
		WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);
	} else if (ip != IPAddress(0,0,0,0)) {
		if (!WiFi.config(ip, gw, sn, dns1, dns2)) {
			WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE); // Fallback auf DHCP bei Fehler
		}
	}
}

// Liest SSID/PW aus /config.json; belässt Ausgabe leer bei Fehler
void loadCredentials(String& ssid, String& password) {
	// Liest SSID und Passwort aus config.json; belässt Ausgabe leer bei Fehler.
	String json;
	if (!loadConfigJson(json)) return;
	StaticJsonDocument<192> doc;
	DeserializationError error = deserializeJson(doc, json);
	if (error) return;
	ssid = doc["ssid"].as<String>();
	password = doc["password"].as<String>();
}

// Schreibt SSID/PW nach /config.json; preserves andere Keys falls vorhanden
void saveCredentials(const String& ssid, const String& password) {
	// Schreibt SSID und Passwort in config.json; erhält andere Keys.
	StaticJsonDocument<320> doc;
	String json;
	if (loadConfigJson(json)) deserializeJson(doc, json);
	doc["ssid"] = ssid;
	doc["password"] = password;

	String out;
	serializeJson(doc, out);
	saveConfigJson(out);
}

// Löscht alle nicht-geschützten JSON-Dateien im Root
void eraseCredentials() {
	// Löscht die gesamte config.json aus dem NVS (Neustart erforderlich).
	eraseConfigJson();
}

// Setzt Whitelist-Einträge für geschützte .json (mit führendem '/')
void setProtectedJsons(std::initializer_list<const char*> names) {
	// Setzt die Whitelist der geschützten JSON-Dateien (mit führendem '/').
	_protectedExact.clear();
	for (auto n : names) {
		String s(n ? n : "");
		if (!s.length()) continue;
		if (!s.startsWith("/")) s = "/" + s;
		_protectedExact.push_back(s);
	}
}

// Prüft, ob ein Name in der Whitelist liegt (Case-insensitive)
bool isProtectedJson(const String& name) {
	// Prüft ob name in der Whitelist liegt (Case-insensitive, mit '/' normalisiert).
	String n = name;
	if (!n.startsWith("/")) n = "/" + n;
	for (const auto& ex : _protectedExact) {
		if (n.equalsIgnoreCase(ex)) return true;
	}
	return false;
}

// Rekursives Löschen von .json unterhalb dirPath (ESP32)
void eraseJsonInDir(const char* dirPath) {
	// Löscht alle .json-Dateien unterhalb von dirPath rekursiv,
	// außer den in der Whitelist geschützten.
	if (!dirPath || !*dirPath) return;
	File dir = LittleFS.open(dirPath);
	if (!dir || !dir.isDirectory()) return;
	String base = dirPath;
	if (!base.endsWith("/")) base += "/";
	for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
		String name = f.name();
		String full = name;
		if (!full.startsWith("/")) full = base + full;
		const bool isDir = f.isDirectory();
		f.close();
		if (isDir) {
			eraseJsonInDir(full.c_str());
		} else if (full.endsWith(".json") && !isProtectedJson(full)) {
			LittleFS.remove(full);
		}
	}
	dir.close();
}

// NTP-Sync mit bis zu 'tries' Versuchen; Server optional aus /config.json
bool syncTimeDefault(uint8_t tries) {
	// Konfiguriert NTP (Server aus config.json oder Defaults) und
	// wartet bis zu 'tries' × 200ms auf eine gültige Systemzeit.
	String n1 = "pool.ntp.org", n2 = "time.nist.gov";
	String json;
	if (loadConfigJson(json)) {
		StaticJsonDocument<320> d;
		if (!deserializeJson(d, json)) {
			n1 = d["ntp1"] | n1;
			n2 = d["ntp2"] | n2;
		}
	}
	configTime(0, 0, n1.c_str(), n2.c_str());
	for (uint8_t i = 0; i < tries; ++i) {
		time_t now = time(nullptr);
		if (now > 100000) return true;
		delay(200);
	}
	return false;
}

// ArduinoOTA: Lazy-Init, schützt vor Mehrfach-Initialisierung
void ensureArduinoOta(const String& hostname, uint16_t port, const String& password) {
	// Lazy-Init für ArduinoOTA: wird nur einmalig initialisiert.
	// Setzt Hostname, Port und optionales Passwort.
#if defined(ESP32)
	if (WiFi.getMode() == WIFI_OFF) return; // kein Netzwerk aktiv
#endif
	if (_otaStarted) return;

	String host = hostname;
#if defined(ESP32)
	if (!host.length()) host = WiFi.getHostname();
#endif
	if (!host.length()) host = "esp32";

	_otaPort = (port == 0) ? 3232 : port;
	ArduinoOTA.setPort(_otaPort);
	ArduinoOTA.setHostname(host.c_str());
	if (password.length()) ArduinoOTA.setPassword(password.c_str());

	ArduinoOTA.onStart([]() { Serial.println("[OTA] Start"); });
	ArduinoOTA.onEnd([]() { Serial.println("[OTA] Ende"); });
	ArduinoOTA.onError([](ota_error_t error) { Serial.printf("[OTA] Fehler %u\n", static_cast<unsigned>(error)); });

	ArduinoOTA.begin();
	_otaStarted = true;
	Serial.printf("[OTA] Bereit unter %s:%u\n", host.c_str(), static_cast<unsigned>(_otaPort));
}

void handleArduinoOta() {
	// Muss in loop() aufgerufen werden, damit OTA-Updates verarbeitet werden.
	if (_otaStarted) ArduinoOTA.handle();
}

// Partition-Check: ≥2 OTA-App-Slots und ≥1 DATA-(SPIFFS/FFat/LittleFS)
bool checkOtaAndDataPartitions(bool logDetails) {
	// Prüft ob mindestens 2 OTA-App-Slots und 1 Dateisystem-Partition vorhanden sind.
	// logDetails=true gibt alle Partitionen auf Serial aus.
	size_t otaCount = 0;
	size_t dataFsCount = 0;

	const esp_partition_t* running = esp_ota_get_running_partition();
	const esp_partition_t* boot    = esp_ota_get_boot_partition();

	// APP-Partitionen
	esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, nullptr);
	while (it) {
		const esp_partition_t* p = esp_partition_get(it);
		const bool isOta = p->subtype >= ESP_PARTITION_SUBTYPE_APP_OTA_MIN && p->subtype <= ESP_PARTITION_SUBTYPE_APP_OTA_MAX;
		if (isOta) otaCount++;
		if (logDetails) {
			Serial.printf("[PART] APP  %-12s type=0x%02x sub=0x%02x size=%.1fk @0x%06x %s%s\n",
				p->label, p->type, p->subtype, p->size / 1024.0f, p->address,
				(p == running ? "(running) " : ""), (p == boot ? "(boot)" : ""));
		}
		it = esp_partition_next(it);
	}
	esp_partition_iterator_release(it);

	// DATA-Partitionen (SPIFFS/FFat/LittleFS)
	it = esp_partition_find(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, nullptr);
	while (it) {
		const esp_partition_t* p = esp_partition_get(it);
		const bool isFsSubtype = (p->subtype == ESP_PARTITION_SUBTYPE_DATA_SPIFFS) || (p->subtype == ESP_PARTITION_SUBTYPE_DATA_FAT);
		const bool isFsLabel   = String(p->label).startsWith("spiffs") || String(p->label).startsWith("littlefs") || String(p->label).startsWith("ffat");
		const bool isFs = isFsSubtype || isFsLabel;
		if (isFs) dataFsCount++;
		if (logDetails) {
			Serial.printf("[PART] DATA %-12s type=0x%02x sub=0x%02x size=%.1fk @0x%06x %s\n",
				p->label, p->type, p->subtype, p->size / 1024.0f, p->address, isFs ? "(FS)" : "");
		}
		it = esp_partition_next(it);
	}
	esp_partition_iterator_release(it);

	const bool ok = (otaCount >= 2) && (dataFsCount >= 1);
	if (logDetails) {
		Serial.printf("[PART] OTA-Slots=%u, DATA-FS=%u => %s\n",
			(unsigned)otaCount, (unsigned)dataFsCount, ok ? "OK" : "NICHT OK");
	}
	return ok;
}

// AsyncWebServer-Variante
bool serveFileFromLittleFS(AsyncWebServerRequest* request, const String& path, const String& contentType, bool cacheable) {
	if (!request) return false;
	if (!LittleFS.exists(path)) return false;
	AsyncWebServerResponse* res = request->beginResponse(LittleFS, path, contentType);
	if (!res) return false;
	res->addHeader("Cache-Control", cacheable ? "public, max-age=86400" : "no-cache, no-store, must-revalidate");
	request->send(res);
	return true;
}

int clampInt(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

String normalizeHostname(const String& raw) {
    String h = raw;
    h.trim();
    h.toLowerCase();

    String out;
    out.reserve(h.length());

    bool prevDash = false;
    for (size_t i = 0; i < h.length(); ++i) {
        const char c = h[i];
        const bool isAlphaNum = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');

        if (isAlphaNum) {
            out += c;
            prevDash = false;
        } else if ((c == '-' || c == '_' || c == ' ' || c == '.') && !prevDash && out.length() > 0) {
            out += '-';
            prevDash = true;
        }
    }

    while (out.length() && out[0] == '-') out.remove(0, 1);
    while (out.length() && out[out.length() - 1] == '-') out.remove(out.length() - 1, 1);

    if (out.isEmpty()) out = "esp32";
    if (out.length() > 63) out = out.substring(0, 63);

    return out;
}

bool loadManualUiCfg(ManualUiCfg& cfg) {
    String json;
    if (!loadConfigJson(json)) return true;

    StaticJsonDocument<1024> doc;
    if (deserializeJson(doc, json)) return false;

    cfg.delaySeconds = clampInt(doc["testAutoStopSec"] | cfg.delaySeconds, 0, 600);
    cfg.supply = clampInt(doc["manualAnalogSupply"] | cfg.supply, 0, 100);
    cfg.exhaust = clampInt(doc["manualAnalogExhaust"] | cfg.exhaust, 0, 100);
    cfg.postheater = clampInt(doc["manualAnalogPostheater"] | cfg.postheater, 0, 100);

    return true;
}

bool saveManualUiCfg(const ManualUiCfg& cfgIn) {
    ManualUiCfg cfg = cfgIn;
    cfg.delaySeconds = clampInt(cfg.delaySeconds, 0, 600);
    cfg.supply = clampInt(cfg.supply, 0, 100);
    cfg.exhaust = clampInt(cfg.exhaust, 0, 100);
    cfg.postheater = clampInt(cfg.postheater, 0, 100);

    String json;
    StaticJsonDocument<1024> doc;
    if (loadConfigJson(json)) deserializeJson(doc, json);

    doc["testAutoStopSec"] = cfg.delaySeconds;
    doc["manualAnalogSupply"] = cfg.supply;
    doc["manualAnalogExhaust"] = cfg.exhaust;
    doc["manualAnalogPostheater"] = cfg.postheater;

    String out;
    serializeJson(doc, out);
    return saveConfigJson(out);
}

int loadTestAutoStopSec() {
    ManualUiCfg cfg{};
    return loadManualUiCfg(cfg) ? cfg.delaySeconds : 0;
}

bool saveTestAutoStopSec(int sec) {
    ManualUiCfg cfg{};
    loadManualUiCfg(cfg);
    cfg.delaySeconds = sec;
    return saveManualUiCfg(cfg);
}