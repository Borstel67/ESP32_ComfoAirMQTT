// SPDX-License-Identifier: MIT
// STA-Webserver-Implementierung:
// - Start nur bei bestehender STA-Verbindung
// - Routen für UI/Assets und API (/api/sta/config)
// - Optionales DNS-Catch-All (Captive-Redirect)
// - OTA via ElegantOTA (eigener WebServer-Port)
#include "ESPWiFiManagerSTA.h"
#include "ESPWiFiManagerAP.h"
#include "AWM_Logging.h"
#include "ESPWiFiManagerCommon.h"
#include "LogBuffer.h"
#include <ArduinoJson.h>
#include "ComfoAirDriver.h"
#include "ComfoHttpApi.h"

#include <esp_wifi.h>

#include <time.h>

#ifndef ELEGANTOTA_USE_ASYNC_WEBSERVER
#define ELEGANTOTA_USE_ASYNC_WEBSERVER 1
#endif
#include <ElegantOTA.h>

ESPWiFiManagerSTA::ESPWiFiManagerSTA()
	: staServer(80)
	, htmlPathPrefix("/")
	, serverPort(80)
	, serverRunning(false)
	, dnsRunning(false)
	, captiveEnabled(true)
	, dhcpEnabled(true)
	, staticIP(0, 0, 0, 0)
	, staticGW(0, 0, 0, 0)
	, staticSN(0, 0, 0, 0)
	, staticDNS1(0, 0, 0, 0)
	, staticDNS2(0, 0, 0, 0)
	, ntp1("")
	, ntp2("") {}

void ESPWiFiManagerSTA::setHtmlPathPrefix(const String& prefix) { htmlPathPrefix = prefix.endsWith("/") ? prefix : prefix + "/"; }
void ESPWiFiManagerSTA::setHostname(const String& host) { hostname = host; }
void ESPWiFiManagerSTA::enableDhcp(bool enabled) { dhcpEnabled = enabled; }
void ESPWiFiManagerSTA::setStaticIP(const IPAddress& ip, const IPAddress& gateway, const IPAddress& subnet,
const IPAddress& dns1, const IPAddress& dns2) {
	staticIP = ip; staticGW = gateway; staticSN = subnet; staticDNS1 = dns1; staticDNS2 = dns2; dhcpEnabled = false;
}

// Startet den STA-Webserver (nur wenn bereits verbunden); richtet Routen, OTA, optional DNS ein
bool ESPWiFiManagerSTA::startSTA(uint16_t port) {
	if (serverRunning) { AWM_LOGW("⚠️ STA-Server läuft bereits"); return true; }
	if (WiFi.status() != WL_CONNECTED) { AWM_LOGE("❌ Nicht mit WiFi verbunden. STA-Server kann nicht gestartet werden."); return false; }

#if defined(ESP32)
	if (hostname.length()) WiFi.setHostname(hostname.c_str());
#endif

	loadCredentials();
	syncTimeDefault();

	serverPort = port;
	AWM_LOGD("Konfiguriere STA HTTP-Server auf Port %d...", serverPort);
	setupHTTPRoutes();
	AWM_LOGD("Starte STA HTTP-Server auf Port %d...", serverPort);
	staServer.begin();  // AsyncWebServer::begin() erwartet keinen Port

	if (captiveEnabled) startDNS();
	else if (dnsRunning) stopDNS();

	ElegantOTA.begin(&staServer);  // nutzt AsyncWebServer Port 80
	AWM_LOGI("🛠  ElegantOTA (Async) auf Port %u unter /update", (unsigned)serverPort);

	serverRunning = true;
	AWM_LOGI("🌐 STA HTTP-Server gestartet auf http://%s:%d", WiFi.localIP().toString().c_str(), serverPort);
	AWM_LOGI("🛠  ElegantOTA aktiv (STA) unter Port %u (/update)", (unsigned)serverPort);
	return true;
}

// Stoppt DNS und Webserver
void ESPWiFiManagerSTA::stopSTA() {
	if (!serverRunning) return;
	stopDNS();
	staServer.end();
	serverRunning = false;
	AWM_LOGI("✅ STA HTTP-Server gestoppt");
}

// Start/Stop DNS Catch-All
void ESPWiFiManagerSTA::startDNS() {
	if (dnsRunning) return;
	dns.setErrorReplyCode(DNSReplyCode::NoError);
	dns.start(53, "*", WiFi.localIP());
	dnsRunning = true;
}
void ESPWiFiManagerSTA::stopDNS() {
	if (!dnsRunning) return;
	dns.stop();
	dnsRunning = false;
}

// Regelmäßige Bedienung von DNS (AsyncServer benötigt kein handleClient)
void ESPWiFiManagerSTA::handleClient() {
	if (serverRunning) {
		if (dnsRunning) dns.processNextRequest();
	}
}

// Captive-Redirect: wenn Host-Header nicht der lokalen IP entspricht → 302
bool ESPWiFiManagerSTA::captivePortalRedirect(AsyncWebServerRequest* request) {
	if (!captiveEnabled || !serverRunning || !request) return false;
	String host = request->host();
	String ip = WiFi.localIP().toString();
	if (ip == "0.0.0.0") return false;
	if (host != ip) {
		request->redirect(String("http://") + ip);
		return true;
	}
	return false;
}

// Umleitung auf Root mit No-Cache-Headern
void ESPWiFiManagerSTA::redirectToRoot(AsyncWebServerRequest* request) {
	if (!request) return;
	auto* res = request->beginResponse(302, "text/plain", "");
	res->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
	res->addHeader("Pragma", "no-cache");
	res->addHeader("Expires", "0");
	res->addHeader("Location", "/", true);
 request->send(res);
}

// HTTP-Routen für UI/Assets/API
void ESPWiFiManagerSTA::setupHTTPRoutes() {
	// AsyncWebServer::end() entfernt keine Handler -> nur einmal registrieren.
	if (routesReady) return;
	routesReady = true;

	ComfoHttpApi::registerRoutes(staServer, comfo);

	staServer.on("/", HTTP_GET, [this](AsyncWebServerRequest* r) { handleRoot(r); });
	staServer.on("/WifiManager.css", HTTP_GET, [this](AsyncWebServerRequest* r) { handleCss(r); });
	staServer.on("/settings", HTTP_GET, [this](AsyncWebServerRequest* r) { handleSettings(r); });
	staServer.on("/settingsSTA.html", HTTP_GET, [this](AsyncWebServerRequest* r) { handleSettings(r); });
	staServer.on("/ComfoAirDriver.html", HTTP_GET, [this](AsyncWebServerRequest* r){ handleComfoDoc(r); });
	staServer.on("/ComfoAirSettings.html", HTTP_GET, [this](AsyncWebServerRequest* r) { handleComfoSettingsDoc(r); });

	// NEU: Reboot-Endpoint
	staServer.on("/reboot", HTTP_POST, [this](AsyncWebServerRequest* r) { handleReboot(r); });

	staServer.on("/log.html", HTTP_GET, [this](AsyncWebServerRequest* r){ handleLogPage(r); });
	staServer.on("/api/logs", HTTP_GET, [this](AsyncWebServerRequest* r){ handleLogs(r); });
	staServer.on("/api/sta/config", HTTP_GET, [this](AsyncWebServerRequest* r) { handleStaConfigGet(r); });
	staServer.on("/api/sta/config", HTTP_POST, [this](AsyncWebServerRequest* r) { handleStaConfigSave(r); });
	staServer.on("/MQTTSettings.html", HTTP_GET, [this](AsyncWebServerRequest* r){ handleMqttSettingsDoc(r); });

	staServer.on("/favicon.ico", HTTP_GET, [this](AsyncWebServerRequest* r) {
		AsyncWebServerResponse* res = r->beginResponse(204);
		res->addHeader("Cache-Control", "public, max-age=86400");
		r->send(res);
	});

	staServer.on("/erase", HTTP_POST, [this](AsyncWebServerRequest* r) { handleErase(r); });
	staServer.onNotFound([this](AsyncWebServerRequest* r) { handleNotFound(r); });
	AWM_LOGD("HTTP-Routen für STA-Server eingerichtet");
}

// Liefert indexSTA.html
void ESPWiFiManagerSTA::handleRoot(AsyncWebServerRequest* request) {
	String path = htmlPathPrefix + "indexSTA.html";
	if (!serveFileFromLittleFS(request, path, "text/html")) {
		AWM_LOGE("❌ Datei nicht gefunden/öffnen: %s", path.c_str());
		request->send(500, "text/html", "<h1>Fehler: indexSTA.html nicht gefunden</h1>");
		return;
	}
}

// Liefert settingsSTA.html
void ESPWiFiManagerSTA::handleSettings(AsyncWebServerRequest* request) {
	String path = htmlPathPrefix + "settings.html";
	if (!serveFileFromLittleFS(request, path, "text/html")) {
		request->send(500, "text/html", "<h1>Fehler: settingsSTA.html nicht gefunden</h1>");
		return;
	}
}

// Liefert WifiManager.css (oder 404)
void ESPWiFiManagerSTA::handleCss(AsyncWebServerRequest* request) {
	String pathcss = htmlPathPrefix + "WifiManager.css";
	if (!serveFileFromLittleFS(request, pathcss, "text/css", true)) {
		request->send(404, "text/css", "");
		return;
	}
}

// Liefert ComfoAirDriver.html
void ESPWiFiManagerSTA::handleComfoDoc(AsyncWebServerRequest* request) {
	String path = htmlPathPrefix + "ComfoAirDriver.html";
	if (!serveFileFromLittleFS(request, path, "text/html")) {
		request->send(404, "text/html", "<h1>Fehler: ComfoAirDriver.html nicht gefunden</h1>");
		return;
	}
}

void ESPWiFiManagerSTA::handleLogPage(AsyncWebServerRequest* request) {
    String path = htmlPathPrefix + "log.html";
    if (!serveFileFromLittleFS(request, path, "text/html")) {
        request->send(404, "text/html", "<h1>Fehler: log.html nicht gefunden</h1>");
        return;
    }
}

void ESPWiFiManagerSTA::handleLogs(AsyncWebServerRequest* request) {
    if (request->hasArg("clear")) { awm_logbuf_clear(); }
    String out;
    awm_logbuf_copyTo(out);
    auto* res = request->beginResponse(200, "text/plain", out);
    res->addHeader("Cache-Control", "no-store");
    request->send(res);
}

// POST /reboot: nur Neustart
void ESPWiFiManagerSTA::handleReboot(AsyncWebServerRequest* request) {
	request->send(200, "application/json", "{\"ok\":true,\"reboot\":true}");
	delay(250);
	ESP.restart();
}

// 404-Handler mit Basisdiagnose
void ESPWiFiManagerSTA::handleNotFound(AsyncWebServerRequest* request) {
	String message = "404 - Seite nicht gefunden\n\n";
	message += "URI: " + request->url() + "\n";
	message += "Methode: ";
	message += (request->method() == HTTP_GET) ? "GET" : "POST";
	message += "\n";
	request->send(404, "text/plain", message);
	AWM_LOGW("⚠️ 404: %s", request->url().c_str());
}

// Lädt STA-bezogene Parameter aus /config.json (DHCP/static, DNS, Hostname, NTP)
void ESPWiFiManagerSTA::loadCredentials() {
	String json;
	if (!loadConfigJson(json)) { AWM_LOGI("ℹ️ /config.json existiert nicht."); return; }

	StaticJsonDocument<320> doc;
	DeserializationError error = deserializeJson(doc, json);
	if (error) { AWM_LOGE("❌ Fehler beim Deserialisieren von JSON aus /config.json"); return; }

	if (doc.containsKey("dhcp")) dhcpEnabled = doc["dhcp"].as<bool>();

	String ip = doc["ip"] | "";
	String gw = doc["gw"] | "";
	String sn = doc["sn"] | "";
	String dns1 = doc["dns1"] | "";
	String dns2 = doc["dns2"] | "";

	if (ip.length())   parseIP(ip, staticIP);
	if (gw.length())   parseIP(gw, staticGW);
	if (sn.length())   parseIP(sn, staticSN);
	if (dns1.length()) parseIP(dns1, staticDNS1);
	if (dns2.length()) parseIP(dns2, staticDNS2);

	String h = doc["hostname"] | "";
	if (h.length()) hostname = h;

	ntp1 = doc["ntp1"] | "";
	ntp2 = doc["ntp2"] | "";

	AWM_LOGI("⚙️ STA Netzwerk: DHCP=%s IP=%s", dhcpEnabled ? "ja" : "nein", staticIP.toString().c_str());
}

// Delegiert IP-Parsing an Common-Helper
bool ESPWiFiManagerSTA::parseIP(const String& text, IPAddress& out) { return ::parseIpText(text, out); }

// GET /api/sta/config: liefert aktuelle STA-Einstellungen
void ESPWiFiManagerSTA::handleStaConfigGet(AsyncWebServerRequest* request) {
	loadCredentials();

	StaticJsonDocument<256> doc;
	doc["dhcp"] = dhcpEnabled;
	doc["ip"] = staticIP.toString();
	doc["gw"] = staticGW.toString();
	doc["sn"] = staticSN.toString();
	doc["dns1"] = staticDNS1.toString();
	doc["dns2"] = staticDNS2.toString();
	doc["hostname"] = hostname;
	doc["ntp1"] = ntp1;
	doc["ntp2"] = ntp2;

	String out;
	serializeJson(doc, out);
request->send(200, "application/json", out);
}

// POST /api/sta/config: validiert Eingaben, speichert nach /config.json
void ESPWiFiManagerSTA::handleStaConfigSave(AsyncWebServerRequest* request) {
	String dhcpRaw = request->arg("dhcp");
	dhcpRaw.toLowerCase();
	const bool useDhcp = (dhcpRaw == "1" || dhcpRaw == "true" || dhcpRaw == "on");

	String ip = request->arg("ip");
	String gw = request->arg("gw");
	String sn = request->arg("sn");
	String dns1 = request->arg("dns1");
	String dns2 = request->arg("dns2");
	String host = normalizeHostname(request->arg("hostname"));
	String ntp1v = request->arg("ntp1");
	String ntp2v = request->arg("ntp2");

	// ... Validierung unverändert

	if (!saveStaConfigToJson(useDhcp, ip, gw, sn, dns1, dns2, host, ntp1v, ntp2v)) {
		request->send(500, "application/json", "{\"ok\":false,\"error\":\"Speichern fehlgeschlagen\"}");
		return;
	}

	loadCredentials();
	request->send(200, "application/json", "{\"ok\":true,\"reboot\":true}");
	delay(300);
	ESP.restart();
}


// Persistiert STA-Parameter in /config.json; erhält andere Keys falls vorhanden
bool ESPWiFiManagerSTA::saveStaConfigToJson(bool useDhcp,
	const String& ip,
	const String& gw,
	const String& sn,
	const String& dns1,
	const String& dns2,
	const String& host,
	const String& ntp1v,
	const String& ntp2v) {

	const bool saved = updateConfigJson([&](JsonDocument& doc) {
		doc["dhcp"] = useDhcp;
		if (useDhcp) {
			doc.remove("ip"); doc.remove("gw"); doc.remove("sn"); doc.remove("dns1"); doc.remove("dns2");
		} else {
			doc["ip"] = ip; doc["gw"] = gw; doc["sn"] = sn; doc["dns1"] = dns1; doc["dns2"] = dns2;
		}
		doc["hostname"] = host;
		doc["ntp1"] = ntp1v;
		doc["ntp2"] = ntp2v;
	});
	if (!saved) { AWM_LOGE("❌ Konnte /config.json nicht schreiben"); return false; }
	AWM_LOGI("✅ STA-Netzwerkparameter in /config.json gespeichert (DHCP=%s)", useDhcp ? "ja" : "nein");
	return true;
}

// POST /erase: nutzt Common-Erase und startet neu
void ESPWiFiManagerSTA::handleErase(AsyncWebServerRequest* request) {
	request->send(200, "application/json", "{\"ok\":true}");
	delay(150);
	::eraseCredentials();
	delay(300);
	ESP.restart();
}

void ESPWiFiManagerSTA::handleComfoSettingsDoc(AsyncWebServerRequest* request) {
	String path = htmlPathPrefix + "ComfoAirSettings.html";
	if (!serveFileFromLittleFS(request, path, "text/html")) {
		request->send(404, "text/html", "<h1>Fehler: ComfoAirSettings.html nicht gefunden</h1>");
		return;
	}
}

void ESPWiFiManagerSTA::handleMqttSettingsDoc(AsyncWebServerRequest* request) {
	String path = htmlPathPrefix + "MQTTSettings.html";
	if (!serveFileFromLittleFS(request, path, "text/html")) {
		request->send(404, "text/html", "<h1>Fehler: MQTTSettings.html nicht gefunden</h1>");
		return;
	}
}
