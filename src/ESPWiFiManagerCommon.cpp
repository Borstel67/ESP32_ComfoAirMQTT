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
#include <Preferences.h>
#include <esp_ota_ops.h>
#include <mutex>

// Exakte Whitelist-Einträge für geschützte JSON-Dateien
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

bool updateConfigJson(const std::function<void(JsonDocument&)>& mutate) {
	static std::mutex mtx;
	std::lock_guard<std::mutex> lock(mtx);

	String json;
	DynamicJsonDocument doc(2048);
	const bool had = loadConfigJson(json);
	if (had && deserializeJson(doc, json)) doc.clear();

	mutate(doc);

	String out;
	serializeJson(doc, out);
	if (had && out == json) return true; // unverändert -> kein NVS-Schreibzugriff
	return saveConfigJson(out);
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

// Löscht alle nicht-geschützten JSON-Dateien im Root
void eraseCredentials() {
	// Löscht die gesamte config.json aus dem NVS (Neustart erforderlich).
	eraseConfigJson();
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

bool isArduinoOtaStarted() { return _otaStarted; }

void handleArduinoOta() {
	// Muss in loop() aufgerufen werden, damit OTA-Updates verarbeitet werden.
	if (_otaStarted) ArduinoOTA.handle();
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

    return updateConfigJson([&](JsonDocument& doc) {
        doc["testAutoStopSec"] = cfg.delaySeconds;
        doc["manualAnalogSupply"] = cfg.supply;
        doc["manualAnalogExhaust"] = cfg.exhaust;
        doc["manualAnalogPostheater"] = cfg.postheater;
    });
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

bool loadMqttCfg(MqttCfg& c) {
    String json;
    if (!loadConfigJson(json)) return true; // Defaults

    DynamicJsonDocument doc(2048);
    if (deserializeJson(doc, json)) return false;

    c.enabled     = doc["mqttEnabled"] | c.enabled;
    c.host        = doc["mqttHost"] | c.host.c_str();
    c.port        = doc["mqttPort"] | c.port;
    c.user        = doc["mqttUser"] | c.user.c_str();
    c.pass        = doc["mqttPass"] | c.pass.c_str();
    c.clientId    = doc["mqttClientId"] | c.clientId.c_str();
    c.topicPrefix = doc["mqttTopicPrefix"] | c.topicPrefix.c_str();
    return true;
}

bool saveMqttCfg(const MqttCfg& c) {
    return updateConfigJson([&](JsonDocument& doc) {
        doc["mqttEnabled"]     = c.enabled;
        doc["mqttHost"]        = c.host;
        doc["mqttPort"]        = c.port;
        doc["mqttUser"]        = c.user;
        doc["mqttPass"]        = c.pass;
        doc["mqttClientId"]    = c.clientId;
        doc["mqttTopicPrefix"] = c.topicPrefix;
    });
}

String normalizeTopicPrefix(String p) {
    p.trim();
    while (p.startsWith("/")) p.remove(0, 1);
    while (p.endsWith("/")) p.remove(p.length() - 1, 1);
    if (p.isEmpty()) p = "comfoair";
    return p;
}

bool toBoolArg(const String& s) {
    String v = s;
    v.toLowerCase();
    return v == "1" || v == "true" || v == "on" || v == "yes";
}

// ── OTA-Rollback ──────────────────────────────────────────────────────────────
// Überschreibt den schwachen Hook des Arduino-Cores: Der Core bestätigt eine neue
// Firmware sonst sofort beim Start; so bleibt sie unbestätigt, bis otaRollbackService()
// sie als funktionsfähig erkannt hat.
extern "C" bool verifyRollbackLater() { return true; }

namespace {
    enum class OtaVerify : uint8_t { UNKNOWN, PENDING, DONE };
    OtaVerify s_otaVerify = OtaVerify::UNKNOWN;
    uint32_t  s_otaHealthySince = 0;
    bool      s_otaWasHealthy = false;
}

bool otaRollbackPending() {
    if (s_otaVerify == OtaVerify::UNKNOWN) {
        esp_ota_img_states_t st;
        const esp_partition_t* running = esp_ota_get_running_partition();
        const bool pending = running && esp_ota_get_state_partition(running, &st) == ESP_OK
                          && st == ESP_OTA_IMG_PENDING_VERIFY;
        s_otaVerify = pending ? OtaVerify::PENDING : OtaVerify::DONE;
        if (pending) {
            Serial.printf("[OTA] Neue Firmware auf Probe: Bestaetigung nach %lu s stabilem Betrieb, "
                          "sonst Rollback nach %lu s\n",
                          (unsigned long)(OTA_CONFIRM_STABLE_MS / 1000), (unsigned long)(OTA_ROLLBACK_TIMEOUT_MS / 1000));
        }
    }
    return s_otaVerify == OtaVerify::PENDING;
}

void otaRollbackService(bool healthy) {
    if (!otaRollbackPending()) return;
    const uint32_t now = millis();

    if (healthy) {
        if (!s_otaWasHealthy) { s_otaWasHealthy = true; s_otaHealthySince = now; }
        if (now - s_otaHealthySince >= OTA_CONFIRM_STABLE_MS) {
            const esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
            s_otaVerify = OtaVerify::DONE;
            Serial.printf("[OTA] Neue Firmware bestaetigt (%s)\n", err == ESP_OK ? "ok" : esp_err_to_name(err));
            return;
        }
    } else {
        s_otaWasHealthy = false;
    }

    if (now >= OTA_ROLLBACK_TIMEOUT_MS) {
        Serial.println("[OTA] Neue Firmware nicht funktionsfaehig -> Rollback auf vorherige Version");
        Serial.flush();
        // Kehrt bei Erfolg nicht zurück (Neustart in die vorherige Firmware).
        const esp_err_t err = esp_ota_mark_app_invalid_rollback_and_reboot();
        Serial.printf("[OTA] Rollback nicht moeglich (%s) - Firmware bleibt aktiv\n", esp_err_to_name(err));
        s_otaVerify = OtaVerify::DONE;
    }
}
