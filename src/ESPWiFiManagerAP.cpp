/*
 *  SPDX-License-Identifier: MIT
 *  AyresWiFiManager — Implementierung (Quellcode)
 *  ---------------------------------------------------------------
 *  @file      AyresWiFiManager.cpp
 *  @version   2.0.3
 *
 *  (gekürzt: Header-Comments)
 */

// SPDX-License-Identifier: MIT
// AP-/Captive-Portal-Implementierung:
// - SoftAP + DNS-Catch-All + HTTP-Routen für Provisioning
// - OTA via ElegantOTA (eigener WebServer-Port), mDNS-Unterstützung
// - Fallback-Strategien, Auto-Reconnect, LED/Button-Handling
#include "ESPWiFiManagerAP.h"
#include "AWM_Logging.h"
#include "LogBuffer.h"
#include "ESPWiFiManagerCommon.h"
#include "ComfoHttpApi.h"
#include <ArduinoJson.h>
#include <ctype.h>

#include <ESPmDNS.h>
#include <esp_wifi.h>
#ifndef ELEGANTOTA_USE_ASYNC_WEBSERVER
#define ELEGANTOTA_USE_ASYNC_WEBSERVER 1
#endif
#include <ElegantOTA.h>
#include <time.h>

// kein AsyncElegantOTA mehr

namespace {
	String g_configJsonBackup;
	bool   g_backupHasData = false;

	// Backup-Callbacks über ElegantOTA
	void ensureElegantOtaCallbacks() {
		static bool registered = false;
		if (registered) return;
		registered = true;

		ElegantOTA.onStart([]() {
			g_backupHasData = false;
			if (loadConfigJson(g_configJsonBackup) && g_configJsonBackup.length()) {
				g_backupHasData = true;
				AWM_LOGI("📦 OTA: config.json aus NVS gesichert (%u Bytes)", (unsigned)g_configJsonBackup.length());
			} else {
				AWM_LOGW("⚠️ OTA: config.json nicht lesbar, kein Backup");
			}
		});
		ElegantOTA.onEnd([](bool success) {
			if (success && g_backupHasData && g_configJsonBackup.length()) {
				if (!saveConfigJson(g_configJsonBackup)) {
					AWM_LOGE("❌ OTA: config.json nicht schreibbar (NVS)");
				} else {
					AWM_LOGI("✅ OTA: config.json restauriert (NVS)");
				}
			}
			g_configJsonBackup = ""; g_backupHasData = false;
		});
	}
} // namespace

ESPWiFiManagerAP::ESPWiFiManagerAP(uint8_t ledPin_, uint8_t buttonPin_)
	: apServer(80), ledPin(ledPin_), buttonPin(buttonPin_) {}

void ESPWiFiManagerAP::setHtmlPathPrefix(const String& prefix) { htmlPathPrefix = prefix.endsWith("/") ? prefix : prefix + "/"; }
void ESPWiFiManagerAP::setHostname(const String& host) { hostname = host; }
void ESPWiFiManagerAP::setAPCredentials(const String& ssid_, const String& pass_) { apSSID = ssid_; apPASS = pass_; }
void ESPWiFiManagerAP::setCaptivePortal(bool enabled) { captiveEnabled = enabled; }
void ESPWiFiManagerAP::setPortalTimeout(uint32_t seconds) { portalTimeoutMs = seconds * 1000UL; }
void ESPWiFiManagerAP::setAPClientCheck(bool enabled) { apClientCheck = enabled; }
void ESPWiFiManagerAP::setWebClientCheck(bool enabled) { webClientCheck = enabled; }
bool ESPWiFiManagerAP::isPortalActive() const { return portalActive; }
void ESPWiFiManagerAP::openPortal() { startAP(); }
void ESPWiFiManagerAP::closePortal() { stopAP(); }
void ESPWiFiManagerAP::setFallbackPolicy(FallbackPolicy p) { fallbackPolicy = p; }
void ESPWiFiManagerAP::setSmartRetries(uint8_t maxRetries, uint32_t windowMs) { maxFailRetries = maxRetries; failWindowMs = windowMs; }
void ESPWiFiManagerAP::enableButtonPortal(bool enable) { allowButtonPortal = enable; }
void ESPWiFiManagerAP::setReconnectBackoffMs(uint32_t ms) { reconnectBackoffMs = (ms < 1000) ? 1000 : ms; AWM_LOGI("⚙️  Wiederverbindungs-Backoff = %lu ms", (unsigned long)reconnectBackoffMs); }
void ESPWiFiManagerAP::setReconnectAttemptMs(uint32_t ms) { reconnectAttemptMs = (ms < 1000) ? 1000 : ms; AWM_LOGI("⚙️  Versuchsfenster = %lu ms", (unsigned long)reconnectAttemptMs); }
void ESPWiFiManagerAP::setExternalApActive(bool active) { externalApActive = active; AWM_LOGI("⚙️  Externer AP aktiv: %s", externalApActive ? "ja" : "nein"); }
bool ESPWiFiManagerAP::isExternalApActive() const { return externalApActive; }
void ESPWiFiManagerAP::setAutoReconnect(bool habilitado) { autoReconnect = habilitado; WiFi.setAutoReconnect(habilitado); }

// Initialisiert GPIO, WiFi-Basis, LittleFS und lädt Credentials
void ESPWiFiManagerAP::begin() {
	pinMode(ledPin, OUTPUT);
	digitalWrite(ledPin, LOW);
	pinMode(buttonPin, INPUT_PULLUP);

	WiFi.persistent(false);
	WiFi.setAutoReconnect(true);
	WiFi.setSleep(false);
	esp_wifi_set_ps(WIFI_PS_NONE);

	// Nutze die Partition mit Label "littlefs" (siehe partitions.csv)
	if (!LittleFS.begin(true, "/littlefs", 10, "littlefs")) {
		AWM_LOGE("❌ Fehler beim Montieren von LittleFS (Partition 'littlefs')");
		return;
	}

	loadCredentials();
}

// Führt Button-Check, Verbindungsversuch und Fallback-Strategie aus
void ESPWiFiManagerAP::run() {
	AWM_LOGI("🔔 Schaltfläche: 2–5s öffnet Portal | ≥5s löscht Anmeldedaten");

	unsigned long startTime = millis();
	bool pressed = false;

	ledSet(LedPattern::BLINK_SLOW);
	while (millis() - startTime < 2000) {
		if (digitalRead(buttonPin) == LOW) { pressed = true; break; }
		ledTask(); delay(10);
	}
	ledSet(LedPattern::OFF);

	if (pressed) {
		unsigned long t0 = millis();
		while (digitalRead(buttonPin) == LOW) {
			unsigned long held = millis() - t0;
			if (held >= 5000) {
				setLedPatternManual(LedPattern::BLINK_TRIPLE);
				AWM_LOGW("🩹 Hold ≥5s → Anmeldedaten löschen und neu starten");
				eraseCredentials();
				delay(900);
				ESP.restart();
			}
			else if (held >= 2000) setLedPatternManual(LedPattern::BLINK_DOUBLE);
			else                  setLedPatternManual(LedPattern::BLINK_FAST);
			ledTask(); delay(10);
		}
		unsigned long held = millis() - t0;
		if (held >= 2000 && held < 5000 && allowButtonPortal) {
			AWM_LOGI("🟢 Hold 2–5s → Portal öffnen");
			setLedAuto(true);
			startAP();
			return;
		}
		setLedAuto(true);
	}

	if (connectToWiFiSTA()) {
		AWM_LOGI("✅ WiFi-Verbindung erfolgreich.");
		stopAP();
		syncTimeDefault();
		ledSet(LedPattern::ON);
		connected = true;
		return;
	}

	switch (fallbackPolicy) {
	case FallbackPolicy::ON_FAIL:             AWM_LOGI("🟡 Verbindung fehlgeschlagen → Portal öffnen (ON_FAIL)"); startAP(); break;
	case FallbackPolicy::NO_CREDENTIALS_ONLY: if (!tieneCredenciales()) { AWM_LOGI("🟡 Keine Anmeldedaten → Portal öffnen"); startAP(); } else { AWM_LOGI("🟠 Mit Anmeldedaten → kein Portal (NO_CREDENTIALS_ONLY)"); } break;
	case FallbackPolicy::SMART_RETRIES:       AWM_LOGI("🟠 SMART_RETRIES aktiv → Portal erst nach mehreren Fehlschlägen"); break;
	case FallbackPolicy::BUTTON_ONLY:         AWM_LOGI("🟠 BUTTON_ONLY → Portal nicht automatisch öffnen"); break;
	case FallbackPolicy::NEVER:               AWM_LOGI("🟠 NEVER → Portal nicht automatisch öffnen"); break;
	}
}

// Bedient DNS/OTA, Reconnect-Logik, LED und Portal-Timeout
void ESPWiFiManagerAP::update() {
	if (dnsRunning) dns.processNextRequest();
	ledAutoUpdate();
	ledTask();

	if (portalActive && portalHasTimedOut()) {
		AWM_LOGW("⏳ Portal Timeout → schließen");
		stopAP();
	}
}

// Richtet SoftAP ein (IP, SSID/Pass) und setzt optional Hostname
void ESPWiFiManagerAP::setupAP() {
	WiFi.mode(WIFI_AP);
	WiFi.softAPConfig(apIP, apGW, apSN);
	WiFi.softAP(apSSID.c_str(), apPASS.c_str());
#if defined(ESP32)
	if (hostname.length()) WiFi.softAPsetHostname(hostname.c_str());
#endif
	AWM_LOGI("📡 AP: %s | IP %s", apSSID.c_str(), apIP.toString().c_str());
}

// Startet Portal: AP, Routen, OTA, DNS (optional), LED, Timer
void ESPWiFiManagerAP::startAP() {
	if (portalActive) return;
	setupAP();
	setupHTTPRoutes();
	ensureElegantOtaCallbacks();
	ElegantOTA.begin(&apServer);   // nutzt AsyncWebServer Port 80
	AWM_LOGI("🛠  ElegantOTA (Async) auf Port 80 unter /update");
	apServer.begin();
	if (captiveEnabled) startDNS();
	else if (dnsRunning) stopDNS();
	portalActive = true;
	portalStart = millis();
	lastHttpAccess = portalStart;
	AWM_LOGI("🌐 Captive Portal aktiv auf 192.168.4.1 (GET /, /scan, POST /save, POST /erase)");
	ledSet(LedPattern::BLINK_SLOW);
}

// Stoppt Portal, DNS, AP (sofern kein externer AP gewünscht) und setzt WiFi-Modus passend
void ESPWiFiManagerAP::stopAP() {
	if (!portalActive) return;
	stopDNS();
	apServer.end();

	if (!externalApActive) WiFi.softAPdisconnect(true);
	else AWM_LOGI("🔒 Externer AP aktiv → SoftAP bleibt an.");

	portalActive = false;

	if (externalApActive) WiFi.mode(WIFI_AP);
	else {
		if (!ssid.isEmpty()) WiFi.mode(WIFI_STA);
		else                 WiFi.mode(WIFI_OFF);
	}

	AWM_LOGI("✅ Captive Portal gestoppt");
}

// DNS-Catch-All für Captive-Portal
void ESPWiFiManagerAP::startDNS() {
	if (dnsRunning) return;
	dns.setErrorReplyCode(DNSReplyCode::NoError);
	dns.start(53, "*", WiFi.softAPIP());
	dnsRunning = true;
}
void ESPWiFiManagerAP::stopDNS() {
	if (!dnsRunning) return;
	dns.stop();
	dnsRunning = false;
}

// HTTP-Umleitung auf Root (302)
void ESPWiFiManagerAP::redirectToRoot(AsyncWebServerRequest* request) {
	if (!request) return;
	auto* res = request->beginResponse(302, "text/plain", "");
	res->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
	res->addHeader("Pragma", "no-cache");
	res->addHeader("Expires", "0");
	res->addHeader("Location", "/", true);
	request->send(res);
}

// Routen für Portal inkl. Captive-spezifischer Pfade
void ESPWiFiManagerAP::setupHTTPRoutes() {
	// AsyncWebServer::end() entfernt keine Handler -> nur einmal registrieren,
	// sonst wächst die Handler-Liste bei jedem Öffnen des Portals.
	if (routesReady) return;
	routesReady = true;

	ComfoHttpApi::registerRoutes(apServer, comfo);
	apServer.on("/MQTTSettings.html", HTTP_GET, [this](AsyncWebServerRequest* r){ handleMqttSettingsDoc(r); });

	apServer.on("/", HTTP_GET, [this](AsyncWebServerRequest* r) { handleRoot(r); });
	apServer.on("/settings", HTTP_GET, [this](AsyncWebServerRequest* r) { handleSettings(r); });
	apServer.on("/settingsAP.html", HTTP_GET, [this](AsyncWebServerRequest* r) { handleSettings(r); });
	apServer.on("/ComfoAirDriver.html", HTTP_GET, [this](AsyncWebServerRequest* r){ handleComfoDoc(r); });
	apServer.on("/ComfoAirSettings.html", HTTP_GET, [this](AsyncWebServerRequest* r){ handleComfoSettingsDoc(r); }); // NEU
	apServer.on("/log.html", HTTP_GET, [this](AsyncWebServerRequest* r){ handleLogPage(r); });
	apServer.on("/api/logs", HTTP_GET, [this](AsyncWebServerRequest* r){ handleLogs(r); });
	apServer.on("/save", HTTP_POST, [this](AsyncWebServerRequest* r) { handleSave(r); });
	apServer.on("/WifiManager.css", HTTP_GET, [this](AsyncWebServerRequest* r) { handleCss(r); });
	apServer.on("/scan", HTTP_GET, [this](AsyncWebServerRequest* r) { handleScan(r); });
	apServer.on("/scan.json", HTTP_GET, [this](AsyncWebServerRequest* r) { handleScan(r); });
	apServer.on("/erase", HTTP_POST, [this](AsyncWebServerRequest* r) { handleErase(r); });

	if (captiveEnabled) {
		apServer.on("/generate_204", HTTP_GET, [this](AsyncWebServerRequest* r) { redirectToRoot(r); });
		apServer.on("/gen_204", HTTP_GET, [this](AsyncWebServerRequest* r) { redirectToRoot(r); });
		apServer.on("/hotspot-detect.html", HTTP_GET, [this](AsyncWebServerRequest* r) { redirectToRoot(r); });
		apServer.on("/connecttest.txt", HTTP_GET, [this](AsyncWebServerRequest* r) { redirectToRoot(r); });
		apServer.on("/ncsi.txt", HTTP_GET, [this](AsyncWebServerRequest* r) { redirectToRoot(r); });
		apServer.on("/fwlink", HTTP_GET, [this](AsyncWebServerRequest* r) { redirectToRoot(r); });
	}

	apServer.on("/favicon.ico", HTTP_GET, [this](AsyncWebServerRequest* r) {
		AsyncWebServerResponse* res = r->beginResponse(204);
		res->addHeader("Cache-Control", "public, max-age=86400");
		r->send(res);
	});
	apServer.onNotFound([this](AsyncWebServerRequest* r) { handleNotFound(r); });
}

// Captive-Redirect: andere Host-Header → 302 auf AP-IP
bool ESPWiFiManagerAP::captivePortalRedirect(AsyncWebServerRequest* request) {
	if (!portalActive || !captiveEnabled || !request) return false;

	String host = request->host();
	String ap = WiFi.softAPIP().toString();
	if (ap == "0.0.0.0") ap = apIP.toString();

	if (host != ap) {
		request->redirect(String("http://") + ap);
		return true;
	}
	return false;
}

// Anzahl verbundener Stationen am SoftAP
uint8_t ESPWiFiManagerAP::softAPStationCount() { return WiFi.softAPgetStationNum(); }

// Timeout-Check fürs Portal; optional verlängert bei AP-/HTTP-Aktivität
bool ESPWiFiManagerAP::portalHasTimedOut() {
	if (portalTimeoutMs == 0) return false;
	if (apClientCheck && softAPStationCount() > 0) { portalStart = millis(); return false; }
	unsigned long base = webClientCheck ? lastHttpAccess : portalStart;
	return (millis() - base) > portalTimeoutMs;
}

// Liefert indexAP.html aus LittleFS (Captive-Redirect vorher)
void ESPWiFiManagerAP::handleRoot(AsyncWebServerRequest* request) {
	if (captivePortalRedirect(request)) return;
	lastHttpAccess = millis();

	String path = htmlPathPrefix + "indexAP.html";
	if (!serveFileFromLittleFS(request, path, "text/html")) {
		AWM_LOGE("❌ Datei nicht gefunden/öffnen: %s", path.c_str());
		request->send(500, "text/html", "<h1>Fehler: indexAP.html nicht gefunden</h1>");
		return;
	}
}

// Liefert settingsAP.html
void ESPWiFiManagerAP::handleSettings(AsyncWebServerRequest* request) {
	if (captivePortalRedirect(request)) return;
	lastHttpAccess = millis();
	String path = htmlPathPrefix + "settings.html";
	if (!serveFileFromLittleFS(request, path, "text/html")) {
		request->send(500, "text/html", "<h1>Fehler: settingsAP.html nicht gefunden</h1>");
		return;
	}
}

// Liefert ComfoAirDriver.html
void ESPWiFiManagerAP::handleComfoDoc(AsyncWebServerRequest* request) {
	if (captivePortalRedirect(request)) return;
	lastHttpAccess = millis();
	String path = htmlPathPrefix + "ComfoAirDriver.html";
	if (!serveFileFromLittleFS(request, path, "text/html")) {
		request->send(404, "text/html", "<h1>Fehler: ComfoAirDriver.html nicht gefunden</h1>");
		return;
	}
}

// Liefert ComfoAirSettings.html
void ESPWiFiManagerAP::handleComfoSettingsDoc(AsyncWebServerRequest* request) {
	if (captivePortalRedirect(request)) return;
	lastHttpAccess = millis();

	String path = htmlPathPrefix + "ComfoAirSettings.html";
	if (!serveFileFromLittleFS(request, path, "text/html")) {
		request->send(404, "text/html", "<h1>Fehler: ComfoAirSettings.html nicht gefunden</h1>");
		return;
	}
}

// Liefert WifiManager.css; 404 wenn nicht vorhanden
void ESPWiFiManagerAP::handleCss(AsyncWebServerRequest* request) {
	if (captivePortalRedirect(request)) return;
	lastHttpAccess = millis();
	String pathcss = htmlPathPrefix + "WifiManager.css";
	if (!serveFileFromLittleFS(request, pathcss, "text/css", true)) {
		request->send(404, "text/css", "");
		return;
	}
}

// 302 auf Root für unbekannte Routen (bei aktivem Portal)
void ESPWiFiManagerAP::handleNotFound(AsyncWebServerRequest* request) {
	if (captivePortalRedirect(request)) return;
	lastHttpAccess = millis();
	request->redirect("/");
	AWM_LOGW("⚠️ 404: %s", request->url().c_str());
}

// POST /save: Credentials + Netzparameter speichern, dann Neustart
void ESPWiFiManagerAP::handleSave(AsyncWebServerRequest* request) {
	if (captivePortalRedirect(request)) return;
	lastHttpAccess = millis();

	String inSsid = request->arg("ssid");
	String inPass = request->arg("password");
	String inDhcp = request->arg("dhcp");
	String inIp   = request->arg("ip");
	String inGw   = request->arg("gw");
	String inSn   = request->arg("sn");
	String inDns1 = request->arg("dns1");
	String inDns2 = request->arg("dns2");
	String inHost = normalizeHostname(request->arg("hostname"));
	String inNtp1 = request->arg("ntp1");
	String inNtp2 = request->arg("ntp2");

	StaticJsonDocument<320> doc;
	doc["ssid"] = inSsid;
	doc["password"] = inPass;
	doc["dhcp"] = (inDhcp == "true");
	doc["ip"]   = inIp;
	doc["gw"]   = inGw;
	doc["sn"]   = inSn;
	doc["dns1"] = inDns1;
	doc["dns2"] = inDns2;
	doc["hostname"] = inHost;
	doc["ntp1"] = inNtp1;
	doc["ntp2"] = inNtp2;

	String out;
	serializeJson(doc, out);
	if (!saveConfigJson(out)) { mostrarPaginaError("Fehler beim Speichern von Anmeldedaten (NVS).", request); return; }

	if (LittleFS.exists(htmlPathPrefix + "success.html")) {
		serveFileFromLittleFS(request, htmlPathPrefix + "success.html", "text/html");
	} else {
		request->send(200, "text/html", "<h1>Gespeichert. Starte neu...</h1>");
	}

	delay(1000);
	ESP.restart();
}

// POST /erase: löscht alle nicht-geschützten JSON und startet neu
void ESPWiFiManagerAP::handleErase(AsyncWebServerRequest* request) {
	if (captivePortalRedirect(request)) return;
	lastHttpAccess = millis();

	request->send(200, "application/json", "{\"ok\":true}");
	delay(150);
	eraseCredentials();
	delay(300);
	ESP.restart();
}

// GET /scan(.json): synchroner Scan; liefert Liste {ssid,rssi,secure}
void ESPWiFiManagerAP::handleScan(AsyncWebServerRequest* request) {
	lastHttpAccess = millis();
	AWM_LOGI("🔍 Scanne WiFi-Netzwerke (SYNC, AP+STA)…");

	WiFi.mode(WIFI_AP_STA);
	delay(50);

	int st = WiFi.scanComplete();
	if (st == WIFI_SCAN_RUNNING) WiFi.scanDelete();

	scanning = true;
	scanningUntil = millis() + 1500;

	int n = WiFi.scanNetworks(false, false);
	if (n < 0) {
		scanning = false;
		request->send(200, "application/json", "[]");
		AWM_LOGW("⚠️ Scan fehlgeschlagen, gebe [] zurück");
		return;
	}

	size_t cap = 64U + (size_t)n * 64U;
	if (cap < 512U) cap = 512U;
	DynamicJsonDocument doc(cap);
	JsonArray arr = doc.to<JsonArray>();

	for (int i = 0; i < n; ++i) {
		String s = WiFi.SSID(i);
		if (!s.length()) continue;
		JsonObject o = arr.createNestedObject();
		o["ssid"] = s;
		o["rssi"] = WiFi.RSSI(i);
		o["secure"] = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
	}

	WiFi.scanDelete();
	scanning = false;

	String out;
	serializeJson(arr, out);
	request->send(200, "application/json", out);
	AWM_LOGI("✅ Scan OK: %d Netzwerke", (int)arr.size());
}

// Zeigt Fehlerseite oder Fallback-Text an
void ESPWiFiManagerAP::mostrarPaginaError(const String & mensajeFallback, AsyncWebServerRequest* request) {
	if (LittleFS.exists(htmlPathPrefix + "error.html")) {
		serveFileFromLittleFS(request, htmlPathPrefix + "error.html", "text/html");
	} else {
		request->send(500, "text/html", "<h1>Fehler: " + mensajeFallback + "</h1>");
	}
}

// Verbindet als STA mit gespeicherten Credentials; setzt mDNS und NTP bei Erfolg
bool ESPWiFiManagerAP::connectToWiFiSTA() {
	if (!tieneCredenciales()) return false;

	WiFi.mode(WIFI_STA);
	WiFi.disconnect(false, false);
	applyStaConfigFromWifiJson();

#if defined(ESP32)
	String h = normalizeHostname(hostname);
	if (h.length()) WiFi.setHostname(h.c_str());
#endif

	WiFi.begin(ssid.c_str(), password.c_str());

	AWM_LOGI("Verbinde mit %s", ssid.c_str());

	const uint32_t TOUT_MS = 15000;
	uint32_t t0 = millis();
	while (millis() - t0 < TOUT_MS) {
		if (WiFi.status() == WL_CONNECTED) {
			AWM_LOGI("Verbunden. IP: %s", WiFi.localIP().toString().c_str());
# if defined(ESP32)
			WiFi.setSleep(false);
# endif
			syncTimeDefault();
			startMDNS(h.length() ? h : WiFi.getHostname());
			connected = true;
			return true;
		}
		delay(250);
	}

	AWM_LOGW("⏱️ Timeout erreicht. Konnte nicht verbinden.");
	connected = false;
	return false;
}

// Liefert aktuellen Verbindungsstatus
bool ESPWiFiManagerAP::isConnected() { connected = (WiFi.status() == WL_CONNECTED); return connected; }

// LED-Automatik ein/aus
void ESPWiFiManagerAP::setLedAuto(bool enable) { ledAuto = enable; if (ledAuto) ledSet(LedPattern::OFF); }
// LED-Muster manuell setzen (deaktiviert Automatik)
void ESPWiFiManagerAP::setLedPatternManual(LedPattern p) { ledAuto = false; ledSet(p); }
// Interner Setter für LED-Pattern
void ESPWiFiManagerAP::ledSet(LedPattern p) { ledPat = p; ledStep = 0; ledT0 = millis(); }

// Automatische LED-Zuordnung je nach Zustand (scannen/Portal/verbunden/aus)
void ESPWiFiManagerAP::ledAutoUpdate() {
	if (!ledAuto) return;
	LedPattern want = LedPattern::OFF;
	if (scanning || (millis() < scanningUntil))      want = LedPattern::BLINK_FAST;
	else if (portalActive)                           want = LedPattern::BLINK_SLOW;
	else if (WiFi.status() == WL_CONNECTED)          want = LedPattern::ON;
	if (want != ledPat) ledSet(want);
}

// LED-Ausgabe basierend auf aktuellem Pattern (nicht-blockierend)
void ESPWiFiManagerAP::ledTask() {
	const unsigned long now = millis();
	auto write = [&](uint8_t v) { if (ledOut != v) { ledOut = v; digitalWrite(ledPin, v); } };

	switch (ledPat) {
	case LedPattern::OFF: write(LOW); break;
	case LedPattern::ON:  write(HIGH); break;
	case LedPattern::BLINK_SLOW:  write(((now - ledT0) % 1000) < 500 ? HIGH : LOW); break;
	case LedPattern::BLINK_FAST:  write(((now - ledT0) % 200)  < 100 ? HIGH : LOW); break;
	case LedPattern::BLINK_DOUBLE: {
		static const uint16_t seq[] = { 120,120,120,640 };
		static const uint8_t  on[]  = { 1,  0,  1,  0 };
		if (now - ledT0 >= seq[ledStep]) { ledT0 = now; ledStep = (ledStep + 1) % 4; write(on[ledStep] ? HIGH : LOW); }
	} break;
	case LedPattern::BLINK_TRIPLE: {
		static const uint16_t seq[] = { 100,100,100,100,100,500 };
		static const uint8_t  on[]  = { 1,  0,  1,  0,  1,  0 };
		if (now - ledT0 >= seq[ledStep]) { ledT0 = now; ledStep = (ledStep + 1) % 6; write(on[ledStep] ? HIGH : LOW); }
	} break;
	}
}

// Prüft, ob gültige Credentials vorhanden sind
bool ESPWiFiManagerAP::tieneCredenciales() const {
	String s, p;
	::loadCredentials(s, p);
	return !s.isEmpty() && !p.isEmpty();
}

// Lädt Credentials via Common-Helper und optional Hostname aus /config.json
void ESPWiFiManagerAP::loadCredentials() {
	String s, p;
	::loadCredentials(s, p);
	if (s.isEmpty() || p.isEmpty()) { AWM_LOGI("ℹ️ /config.json existiert nicht oder enthält keine gültigen Anmeldedaten."); return; }
	ssid = s; password = p;

	String json;
	if (loadConfigJson(json)) {
		StaticJsonDocument<320> d;
		if (!deserializeJson(d, json)) {
			String h = d["hostname"] | "";
			if (h.length()) hostname = normalizeHostname(h);
		}
	}
}

// Löscht Credentials via Common-Helper (achtet auf Whitelist)
void ESPWiFiManagerAP::eraseCredentials() { ::eraseCredentials(); AWM_LOGI("🧹 .json Bereinigung abgeschlossen (respektiere geschützte)."); }

// mDNS-Steuerung für AP/STA
bool ESPWiFiManagerAP::startMDNS(const String& host) {
    String h = host.length() ? host : String("esp32");
    if (mdnsStarted) { MDNS.end(); mdnsStarted = false; }
    if (!MDNS.begin(h.c_str())) { AWM_LOGW("⚠️ MDNS start fehlgeschlagen (%s)", h.c_str()); return false; }
    MDNS.addService("http", "tcp", 80);
    mdnsStarted = true;
    AWM_LOGI("✅ MDNS aktiv: %s.local", h.c_str());
    return true;
}

void ESPWiFiManagerAP::handleLogPage(AsyncWebServerRequest* request) {
    if (captivePortalRedirect(request)) return;
    lastHttpAccess = millis();

    String path = htmlPathPrefix + "log.html";
    if (!serveFileFromLittleFS(request, path, "text/html")) {
        request->send(404, "text/html", "<h1>Fehler: log.html nicht gefunden</h1>");
        return;
    }
}

void ESPWiFiManagerAP::handleLogs(AsyncWebServerRequest* request) {
    if (captivePortalRedirect(request)) return;
    lastHttpAccess = millis();

    if (request->hasArg("clear")) { awm_logbuf_clear(); }
    String out;
    awm_logbuf_copyTo(out);

    auto* res = request->beginResponse(200, "text/plain", out);
    res->addHeader("Cache-Control", "no-store");
    request->send(res);
}

// nahe handleComfoSettingsDoc ergänzen
void ESPWiFiManagerAP::handleMqttSettingsDoc(AsyncWebServerRequest* request) {
    if (captivePortalRedirect(request)) return;
    lastHttpAccess = millis();

    String path = htmlPathPrefix + "MQTTSettings.html";
    if (!serveFileFromLittleFS(request, path, "text/html")) {
        request->send(404, "text/html", "<h1>Fehler: MQTTSettings.html nicht gefunden</h1>");
        return;
    }
}
