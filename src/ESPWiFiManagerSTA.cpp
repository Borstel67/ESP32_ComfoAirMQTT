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

#include <HTTPClient.h>
#include <esp_wifi.h>

#include <time.h>
#include <math.h> // falls noch nicht vorhanden

#ifndef ELEGANTOTA_USE_ASYNC_WEBSERVER
#define ELEGANTOTA_USE_ASYNC_WEBSERVER 1
#endif
#include <ElegantOTA.h>

// Helper lokal im .cpp (oberhalb der Handler)
namespace {

	struct MqttCfg {
		bool enabled = true;
		String host = "";
		int port = 1883;
		String user = "";
		String pass = "";
		String clientId = "ComfoAirESP32";
		String topicPrefix = "comfoair";
	};

	static bool toBoolArg(const String& s) {
		String v = s;
		v.toLowerCase();
		return v == "1" || v == "true" || v == "on" || v == "yes";
	}

	static bool loadMqttCfg(MqttCfg& c) {
		String json;
		if (!loadConfigJson(json)) return true; // Defaults

		StaticJsonDocument<1024> doc;
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

	static bool saveMqttCfg(const MqttCfg& c) {
		String json;
		StaticJsonDocument<1024> doc;
		if (loadConfigJson(json)) deserializeJson(doc, json);

		doc["mqttEnabled"]     = c.enabled;
		doc["mqttHost"]        = c.host;
		doc["mqttPort"]        = c.port;
		doc["mqttUser"]        = c.user;
		doc["mqttPass"]        = c.pass;
		doc["mqttClientId"]    = c.clientId;
		doc["mqttTopicPrefix"] = c.topicPrefix;

		String out;
		serializeJson(doc, out);
		return saveConfigJson(out);
	}

}

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
	staServer.on("/api/comfo/state", HTTP_GET, [this](AsyncWebServerRequest* r){ handleComfoState(r); });
	staServer.on("/api/comfo/setLevel", HTTP_POST, [this](AsyncWebServerRequest* r){ handleComfoSetLevel(r); });
	staServer.on("/api/comfo/setComfort", HTTP_POST, [this](AsyncWebServerRequest* r){ handleComfoSetComfort(r); });
	staServer.on("/api/comfo/setStatus", HTTP_POST, [this](AsyncWebServerRequest* r){ handleComfoSetStatus(r); });
	staServer.on("/api/comfo/setVentilationLevels", HTTP_POST, [this](AsyncWebServerRequest* r){ handleComfoSetVentilationLevels(r); });
	staServer.on("/api/comfo/manual/start", HTTP_POST, [this](AsyncWebServerRequest* r) { handleComfoTestStart(r); });
	staServer.on("/api/comfo/manual/flaps", HTTP_POST, [this](AsyncWebServerRequest* r) { handleComfoTestFlaps(r); });
	staServer.on("/api/comfo/manual/end", HTTP_POST, [this](AsyncWebServerRequest* r) { handleComfoTestEnd(r); });
	staServer.on("/api/comfo/manual/analog", HTTP_POST, [this](AsyncWebServerRequest* r) { handleComfoTestAnalog(r); });
	staServer.on("/api/comfo/manual/config", HTTP_GET, [this](AsyncWebServerRequest* r) { handleComfoTestConfigGet(r); });
	staServer.on("/api/comfo/manual/config", HTTP_POST, [this](AsyncWebServerRequest* r) { handleComfoTestConfigSave(r); });
	staServer.on("/MQTTSettings.html", HTTP_GET, [this](AsyncWebServerRequest* r){ handleMqttSettingsDoc(r); });
	staServer.on("/api/mqtt/config", HTTP_GET,  [this](AsyncWebServerRequest* r){ handleMqttConfigGet(r); });
	staServer.on("/api/mqtt/config", HTTP_POST, [this](AsyncWebServerRequest* r){ handleMqttConfigSave(r); });

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

	StaticJsonDocument<512> doc;
	String json;
	if (loadConfigJson(json)) deserializeJson(doc, json);

	doc["dhcp"] = useDhcp;
	if (useDhcp) {
		doc.remove("ip"); doc.remove("gw"); doc.remove("sn"); doc.remove("dns1"); doc.remove("dns2");
	} else {
		doc["ip"] = ip; doc["gw"] = gw; doc["sn"] = sn; doc["dns1"] = dns1; doc["dns2"] = dns2;
	}
	doc["hostname"] = host;
	doc["ntp1"] = ntp1v;
	doc["ntp2"] = ntp2v;

	String out;
	serializeJson(doc, out);
	if (!saveConfigJson(out)) { AWM_LOGE("❌ Konnte /config.json nicht schreiben"); return false; }
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

void ESPWiFiManagerSTA::handleComfoState(AsyncWebServerRequest* request) {
    if (!comfo) { request->send(503, "application/json", "{\"ok\":false,\"error\":\"No driver\"}"); return; }
    StaticJsonDocument<1024> doc;
    ComfoFanStatus fan; ComfoTempStatus temps; ComfoAnalogInputs ain; ComfoVentilationLevel vent;
    ComfoBypassStatusDF bypass;
    ComfoAnlagenconf st;
    ComfoFlapStatus flap;
    ComfoTempsD1 td1;
    uint8_t rsMode = 0; bool ok = true;

    ok &= comfo->getFanStatus(fan);
    ok &= comfo->getTemperatureStatus(temps);
    ok &= comfo->getAnalogInputs(ain);
    ok &= comfo->getVentilationLevel(vent);
    ok &= comfo->getBypassStatusDF(bypass);
    ok &= comfo-> getAnlagenconf(st);
    ok &= comfo->getFlapStatus(flap);
    ok &= comfo->getTempsD1(td1);

    bool rsOk = comfo->getRs232Mode(rsMode);
    if (!rsOk) AWM_LOGW("RS232 Mode (API) nicht lesbar – übersprungen");

    doc["ok"] = ok;
    doc["manualModeActive"] = comfo->isManualModeActive(); // NEU

    auto decodeTemp = [](int8_t raw) { return (static_cast<float>(raw) / 2.0f) - 20.0f; };
    auto t = doc.createNestedObject("temps");
    t["t1"] = decodeTemp(temps.t1Outside);
    t["t2"] = decodeTemp(temps.t2Supply);
    t["t3"] = decodeTemp(temps.t3Exhaust);
    t["t4"] = decodeTemp(temps.t4Outlet);
    t["comfortC"] = decodeTemp(td1.comfortTemp);

    auto f = doc.createNestedObject("fan");
    f["supplyPct"] = fan.supplyPercent; f["exhaustPct"] = fan.exhaustPercent;
    f["supplyRpm"] = fan.supplyRpm;     f["exhaustRpm"] = fan.exhaustRpm;

    auto v = doc.createNestedObject("vent");
    v["current"]      = vent.currentLevel;
    v["supplyCur"]    = vent.supplyCurrent;
    v["exhaustCur"]   = vent.exhaustCurrent;
    v["supplyHigh"]   = vent.supplyHigh;
    v["exhaustHigh"]  = vent.exhaustHigh;

    // neu: Sollwerte 0x00CD
    v["exhaustAbsent"] = vent.exhaustAbsent;
    v["exhaustLow"]    = vent.exhaustLow;
    v["exhaustMedium"] = vent.exhaustMedium;
    v["supplyAbsent"]  = vent.supplyAbsent;
    v["supplyLow"]     = vent.supplyLow;
    v["supplyMedium"]  = vent.supplyMedium;

    auto b = doc.createNestedObject("bypass");
    b["factor"] = bypass.factor;
    b["stage"] = bypass.stage;
    b["correction"] = bypass.correction;
    b["summerMode"] = bypass.summerMode;

    auto s = doc.createNestedObject("statusD5");
    s["preheaterPresent"] = st.preheaterPresent;
    s["bypassPresent"]    = st.bypassPresent;
    s["type"]             = st.type;
    s["size"]             = st.size;
    s["options"]          = st.options;
    s["activeStatus1"]    = st.activeStatus1;
    s["activeStatus2"]    = st.activeStatus2;
    s["activeStatus3"]    = st.activeStatus3;
    s["enthalpyPresent"]  = st.enthalpyPresent;
    s["ewtPresent"]       = st.ewtPresent;

    auto fl = doc.createNestedObject("flap");
    fl["bypassPercent"]  = flap.bypassPercent;
    fl["preheaterState"] = flap.preheaterState;
    fl["bypassAdc"]      = flap.bypassAdc;
    fl["preheaterAdc"]   = flap.preheaterAdc;

    // NEU
    auto m = doc.createNestedObject("manual");
    m["active"] = comfo->isManualModeActive();
    m["requestedBypass"] = comfo->getManualRequestedBypass();
    m["requestedPreheater"] = comfo->getManualRequestedPreheater();
    m["requestedBypassText"] = comfo->getManualRequestedBypassText();
    m["requestedPreheaterText"] = comfo->getManualRequestedPreheaterText();

    if (rsOk) doc["rs232Mode"] = rsMode;
    String out; serializeJson(doc, out);
    request->send(200, "application/json", out);
}

void ESPWiFiManagerSTA::handleComfoSetLevel(AsyncWebServerRequest* request) {
    if (!comfo) { request->send(503, "application/json", "{\"ok\":false,\"error\":\"No driver\"}"); return; }
    int level = request->arg("level").toInt(); // 0..4
    if (level < 0 || level > 4) {
        request->send(400, "application/json", "{\"ok\":false,\"error\":\"level must be 0..4\"}");
        return;
    }
    bool ok = comfo->setLevel((uint8_t)level);
    request->send(200, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"setLevel failed\"}");
}

void ESPWiFiManagerSTA::handleComfoSetComfort(AsyncWebServerRequest* request) {
    if (!comfo) { request->send(503, "application/json", "{\"ok\":false,\"error\":\"No driver\"}"); return; }

    float tempC = request->arg("tempC").toFloat();
    int tc = (int)lroundf(tempC);
    if (tc < -40) tc = -40;
    if (tc > 100) tc = 100;

    bool ok = comfo->setComfortTemperatureC((int8_t)tc);
    request->send(200, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"setComfort failed (0x00D3)\"}");
}

void ESPWiFiManagerSTA::handleComfoSetStatus(AsyncWebServerRequest* request) {
    if (!comfo) {
        request->send(503, "application/json", "{\"ok\":false,\"error\":\"No driver\"}");
        return;
    }

    const char* required[] = { "preheater", "bypass", "type", "size", "options", "enthalpy", "ewt" };
    for (auto key : required) {
        if (!request->hasArg(key)) {
            request->send(400, "application/json", String("{\"ok\":false,\"error\":\"missing arg: ") + key + "\"}");
            return;
        }
    }

    int preheater = request->arg("preheater").toInt(); // 0/1
    int bypass    = request->arg("bypass").toInt();    // 0/1
    int type      = request->arg("type").toInt();      // 1/2
    int size      = request->arg("size").toInt();      // 1/2
    int options   = request->arg("options").toInt();   // 0..255
    int enthalpy  = request->arg("enthalpy").toInt();  // 0/1/2
    int ewt       = request->arg("ewt").toInt();       // 0/1/2

    if ((preheater < 0 || preheater > 1) ||
        (bypass < 0 || bypass > 1) ||
        (type < 1 || type > 2) ||
        (size < 1 || size > 2) ||
        (options < 0 || options > 255) ||
        (enthalpy < 0 || enthalpy > 2) ||
        (ewt < 0 || ewt > 2)) {
        request->send(400, "application/json", "{\"ok\":false,\"error\":\"invalid value range\"}");
        return;
    }

    bool ok = comfo->setStatus(
        static_cast<uint8_t>(preheater),
        static_cast<uint8_t>(bypass),
        static_cast<uint8_t>(type),
        static_cast<uint8_t>(size),
        static_cast<uint8_t>(options),
        static_cast<uint8_t>(enthalpy),
        static_cast<uint8_t>(ewt));

    request->send(200, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"setStatus failed\"}");
}

void ESPWiFiManagerSTA::handleComfoSetVentilationLevels(AsyncWebServerRequest* request) {
    if (!comfo) {
        request->send(503, "application/json", "{\"ok\":false,\"error\":\"No driver\"}");
        return;
    }

    const char* required[] = {
        "exhaustAbsent","exhaustLow","exhaustMedium",
        "supplyAbsent","supplyLow","supplyMedium",
        "exhaustHigh","supplyHigh"
    };

    for (auto key : required) {
        if (!request->hasArg(key)) {
            request->send(400, "application/json",
                String("{\"ok\":false,\"error\":\"missing arg: ") + key + "\"}");
            return;
        }
    }

    int exhaustAbsent = request->arg("exhaustAbsent").toInt();
    int exhaustLow    = request->arg("exhaustLow").toInt();
    int exhaustMedium = request->arg("exhaustMedium").toInt();
    int supplyAbsent  = request->arg("supplyAbsent").toInt();
    int supplyLow     = request->arg("supplyLow").toInt();
    int supplyMedium  = request->arg("supplyMedium").toInt();
    int exhaustHigh   = request->arg("exhaustHigh").toInt();
    int supplyHigh    = request->arg("supplyHigh").toInt();

    auto inRange = [](int v) { return v >= 0 && v <= 100; };
    if (!inRange(exhaustAbsent) || !inRange(exhaustLow) || !inRange(exhaustMedium) ||
        !inRange(supplyAbsent)  || !inRange(supplyLow)  || !inRange(supplyMedium)  ||
        !inRange(exhaustHigh)   || !inRange(supplyHigh)) {
        request->send(400, "application/json", "{\"ok\":false,\"error\":\"values must be 0..100\"}");
        return;
    }

    bool ok = comfo->setVentilationLevels(
        static_cast<uint8_t>(exhaustAbsent),
        static_cast<uint8_t>(exhaustLow),
        static_cast<uint8_t>(exhaustMedium),
        static_cast<uint8_t>(supplyAbsent),
        static_cast<uint8_t>(supplyLow),
        static_cast<uint8_t>(supplyMedium),
        static_cast<uint8_t>(exhaustHigh),
        static_cast<uint8_t>(supplyHigh)
    );

    request->send(200, "application/json",
        ok ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"setVentilationLevels failed\"}");
}

void ESPWiFiManagerSTA::handleComfoSettingsDoc(AsyncWebServerRequest* request) {
	String path = htmlPathPrefix + "ComfoAirSettings.html";
	if (!serveFileFromLittleFS(request, path, "text/html")) {
		request->send(404, "text/html", "<h1>Fehler: ComfoAirSettings.html nicht gefunden</h1>");
		return;
	}
}

void ESPWiFiManagerSTA::handleComfoTestStart(AsyncWebServerRequest* request) {
    if (!comfo) {
        request->send(503, "application/json", "{\"ok\":false,\"error\":\"No driver\"}");
        return;
    }
    bool ok = comfo->startTestMode();
    request->send(200, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"startTestMode failed\"}");
}

void ESPWiFiManagerSTA::handleComfoTestFlaps(AsyncWebServerRequest* request) {
    if (!comfo) { request->send(503, "application/json", "{\"ok\":false,\"error\":\"No driver\"}"); return; }

    int bypass = request->arg("bypass").toInt();
    int preheater = request->arg("preheater").toInt();
    auto valid = [](int v) { return v == 0 || v == 1 || v == 3; };
    if (!valid(bypass) || !valid(preheater)) {
        request->send(400, "application/json", "{\"ok\":false,\"error\":\"bypass/preheater must be 0,1,3\"}");
        return;
    }

    int delay = request->hasArg("delaySeconds") ? request->arg("delaySeconds").toInt() : loadTestAutoStopSec();
    if (delay < 0 || delay > 600) {
        request->send(400, "application/json", "{\"ok\":false,\"error\":\"delaySeconds must be 0..600\"}");
        return;
    }

    bool ok = comfo->setTestFlapsWithAutoStop((uint8_t)bypass, (uint8_t)preheater, (uint16_t)delay);
    if (ok) {
        saveTestAutoStopSec(delay); // ComfoAirSettings -> config.json
        comfo->setManualAutoStopDelaySecFromConfig((uint16_t)delay); // config -> MQTT
    }
    request->send(200, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"setTestFlaps failed\"}");
}

void ESPWiFiManagerSTA::handleComfoTestEnd(AsyncWebServerRequest* request) {
    if (!comfo) { request->send(503, "application/json", "{\"ok\":false,\"error\":\"No driver\"}"); return; }
    bool ok = comfo->endTestMode();
    request->send(200, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"endTestMode failed\"}");
}

void ESPWiFiManagerSTA::handleComfoTestAnalog(AsyncWebServerRequest* request) {
    if (!comfo) { request->send(503, "application/json", "{\"ok\":false,\"error\":\"No driver\"}"); return; }

    int supply = request->arg("supply").toInt();
    int exhaust = request->arg("exhaust").toInt();
    int postheater = request->arg("postheater").toInt();

    auto valid = [](int v) { return v >= 0 && v <= 100; };
    if (!valid(supply) || !valid(exhaust) || !valid(postheater)) {
        request->send(400, "application/json", "{\"ok\":false,\"error\":\"supply/exhaust/postheater must be 0..100\"}");
        return;
    }

    bool ok = comfo->setTestAnalog((uint8_t)supply, (uint8_t)exhaust, (uint8_t)postheater);

    if (ok) {
        ManualUiCfg cfg{};
        loadManualUiCfg(cfg);
        cfg.supply = supply;
        cfg.exhaust = exhaust;
        cfg.postheater = postheater;
        saveManualUiCfg(cfg);
    }

    request->send(200, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"setTestAnalog failed\"}");
}

// NEU
void ESPWiFiManagerSTA::handleComfoTestConfigGet(AsyncWebServerRequest* request) {
    ManualUiCfg cfg{};
    if (!loadManualUiCfg(cfg)) {
        request->send(500, "application/json", "{\"ok\":false,\"error\":\"config read failed\"}");
        return;
    }

    StaticJsonDocument<256> d;
    d["ok"] = true;
    d["delaySeconds"] = cfg.delaySeconds;
    d["supply"] = cfg.supply;
    d["exhaust"] = cfg.exhaust;
    d["postheater"] = cfg.postheater;

    String out;
    serializeJson(d, out);
    request->send(200, "application/json", out);
}

// NEU
void ESPWiFiManagerSTA::handleComfoTestConfigSave(AsyncWebServerRequest* request) {
    ManualUiCfg cfg{};
    loadManualUiCfg(cfg); // bestehende Werte als Fallback

    if (request->hasArg("delaySeconds")) cfg.delaySeconds = request->arg("delaySeconds").toInt();
    if (request->hasArg("supply"))       cfg.supply = request->arg("supply").toInt();
    if (request->hasArg("exhaust"))      cfg.exhaust = request->arg("exhaust").toInt();
    if (request->hasArg("postheater"))   cfg.postheater = request->arg("postheater").toInt();

    if (cfg.delaySeconds < 0 || cfg.delaySeconds > 600 ||
        cfg.supply < 0 || cfg.supply > 100 ||
        cfg.exhaust < 0 || cfg.exhaust > 100 ||
        cfg.postheater < 0 || cfg.postheater > 100) {
        request->send(400, "application/json", "{\"ok\":false,\"error\":\"invalid range\"}");
        return;
    }

    bool ok = saveManualUiCfg(cfg);
    if (ok && comfo) {
        comfo->setManualAutoStopDelaySecFromConfig((uint16_t)cfg.delaySeconds); // config -> MQTT
    }
    request->send(200, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"save failed\"}");
}
void ESPWiFiManagerSTA::handleMqttSettingsDoc(AsyncWebServerRequest* request) {
	String path = htmlPathPrefix + "MQTTSettings.html";
	if (!serveFileFromLittleFS(request, path, "text/html")) {
		request->send(404, "text/html", "<h1>Fehler: MQTTSettings.html nicht gefunden</h1>");
		return;
	}
}

// gleiche MqttCfg/Helper wie in AP.cpp verwenden und dann:
void ESPWiFiManagerSTA::handleMqttConfigGet(AsyncWebServerRequest* request) {
	MqttCfg c;
	if (!loadMqttCfg(c)) {
		request->send(500, "application/json", "{\"ok\":false,\"error\":\"config read failed\"}");
		return;
	}

	StaticJsonDocument<512> doc;
	doc["ok"] = true;
	doc["enabled"] = c.enabled;
	doc["host"] = c.host;
	doc["port"] = c.port;
	doc["user"] = c.user;
	doc["pass"] = c.pass;
	doc["clientId"] = c.clientId;
	doc["topicPrefix"] = c.topicPrefix;

	String out;
	serializeJson(doc, out);
	request->send(200, "application/json", out);
}

void ESPWiFiManagerSTA::handleMqttConfigSave(AsyncWebServerRequest* request) {
	MqttCfg c;
	if (!loadMqttCfg(c)) {
		request->send(500, "application/json", "{\"ok\":false,\"error\":\"config read failed\"}");
		return;
	}

	if (request->hasArg("enabled"))     c.enabled = toBoolArg(request->arg("enabled"));
	if (request->hasArg("host"))        c.host = request->arg("host");
	if (request->hasArg("port"))        c.port = request->arg("port").toInt();
	if (request->hasArg("user"))        c.user = request->arg("user");
	if (request->hasArg("pass"))        c.pass = request->arg("pass");
	if (request->hasArg("clientId"))    c.clientId = request->arg("clientId");
	if (request->hasArg("topicPrefix")) c.topicPrefix = request->arg("topicPrefix");

	c.host.trim();

	if (c.enabled && c.host.isEmpty()) {
	    request->send(400, "application/json",
	                  "{\"ok\":false,\"error\":\"host required when mqtt enabled\"}");
	    return;
	}

	if (c.port < 1 || c.port > 65535) {
		request->send(400, "application/json",
	                  "{\"ok\":false,\"error\":\"port must be 1..65535\"}");
		return;
	}

	if (!saveMqttCfg(c)) {
		request->send(500, "application/json",
	                  "{\"ok\":false,\"error\":\"save failed\"}");
		return;
	}

	if (comfo) {
        ComfoMqttConfig cfg{
            c.host.c_str(),
            c.port,
            c.user.c_str(),
            c.pass.c_str(),
            c.clientId.c_str(),
            c.topicPrefix.c_str()
        };

        const bool applied = comfo->mqttApplyConfig(cfg, c.enabled, true);
        if (!applied) {
            request->send(200, "application/json",
                          "{\"ok\":true,\"applied\":false,\"error\":\"mqtt connect/publish failed\"}");
            return;
        }
	}

	request->send(200, "application/json", "{\"ok\":true,\"applied\":true}");
}