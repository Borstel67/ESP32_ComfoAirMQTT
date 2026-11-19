// SPDX-License-Identifier: MIT
// Gemeinsame HTTP-API-Handler für AP- und STA-Webserver.
#include "ComfoHttpApi.h"
#include "ESPWiFiManagerCommon.h"
#include <ArduinoJson.h>
#include <math.h>

namespace {
    const char* const kJson = "application/json";
    const char* const kNoDriver = "{\"ok\":false,\"error\":\"No driver\"}";
    const char* const kConfigReadFailed = "{\"ok\":false,\"error\":\"config read failed\"}";

    void sendJson(AsyncWebServerRequest* request, int code, const JsonDocument& doc, bool noStore = false) {
        String out;
        serializeJson(doc, out);
        AsyncWebServerResponse* res = request->beginResponse(code, kJson, out);
        if (noStore) res->addHeader("Cache-Control", "no-store");
        request->send(res);
    }

    void sendError(AsyncWebServerRequest* request, int code, const String& error) {
        request->send(code, kJson, String("{\"ok\":false,\"error\":\"") + error + "\"}");
    }

    void sendBadRequest(AsyncWebServerRequest* request, const String& error) {
        sendError(request, 400, error);
    }

    void sendResult(AsyncWebServerRequest* request, bool ok, const char* error) {
        if (ok) request->send(200, kJson, "{\"ok\":true}");
        else    sendError(request, 200, error);
    }

    // Prüft, ob der Treiber gesetzt ist; sendet sonst 503.
    bool requireDriver(AsyncWebServerRequest* request, ComfoAirDriver* comfo) {
        if (comfo) return true;
        request->send(503, kJson, kNoDriver);
        return false;
    }

    // Prüft, ob alle Pflichtparameter vorhanden sind; sendet sonst 400.
    template <size_t N>
    bool requireArgs(AsyncWebServerRequest* request, const char* const (&keys)[N]) {
        for (auto key : keys) {
            if (!request->hasArg(key)) {
                sendBadRequest(request, String("missing arg: ") + key);
                return false;
            }
        }
        return true;
    }

    bool inRange(int v, int lo, int hi) { return v >= lo && v <= hi; }
}

namespace ComfoHttpApi {

void state(AsyncWebServerRequest* request, ComfoAirDriver* comfo) {
    if (!requireDriver(request, comfo)) return;

    // Liefert den von pollAll() gepflegten Snapshot. Kein RS232-Zugriff im Async-TCP-Task:
    // sonst blockiert der Handler sekundenlang und kollidiert mit dem Polling im loop().
    DynamicJsonDocument doc(1536);
    comfo->fillStateJson(doc);
    sendJson(request, 200, doc, true);
}

void setLevel(AsyncWebServerRequest* request, ComfoAirDriver* comfo) {
    if (!requireDriver(request, comfo)) return;
    if (!request->hasArg("level")) { sendBadRequest(request, "missing arg: level"); return; }

    const int level = request->arg("level").toInt(); // 0..4
    if (!inRange(level, 0, 4)) { sendBadRequest(request, "level must be 0..4"); return; }

    sendResult(request, comfo->setLevel(static_cast<uint8_t>(level)), "setLevel failed");
}

void setComfort(AsyncWebServerRequest* request, ComfoAirDriver* comfo) {
    if (!requireDriver(request, comfo)) return;
    if (!request->hasArg("tempC")) { sendBadRequest(request, "missing arg: tempC"); return; }

    const int tc = clampInt(static_cast<int>(lroundf(request->arg("tempC").toFloat())), -40, 100);
    sendResult(request, comfo->setComfortTemperatureC(static_cast<int8_t>(tc)), "setComfort failed (0x00D3)");
}

void setStatus(AsyncWebServerRequest* request, ComfoAirDriver* comfo) {
    if (!requireDriver(request, comfo)) return;

    static const char* const required[] = { "preheater", "bypass", "type", "size", "options", "enthalpy", "ewt" };
    if (!requireArgs(request, required)) return;

    const int preheater = request->arg("preheater").toInt(); // 0/1
    const int bypass    = request->arg("bypass").toInt();    // 0/1
    const int type      = request->arg("type").toInt();      // 1/2
    const int size      = request->arg("size").toInt();      // 1/2
    const int options   = request->arg("options").toInt();   // 0..255
    const int enthalpy  = request->arg("enthalpy").toInt();  // 0/1/2
    const int ewt       = request->arg("ewt").toInt();       // 0/1/2

    if (!inRange(preheater, 0, 1) || !inRange(bypass, 0, 1) ||
        !inRange(type, 1, 2) || !inRange(size, 1, 2) ||
        !inRange(options, 0, 255) || !inRange(enthalpy, 0, 2) || !inRange(ewt, 0, 2)) {
        sendBadRequest(request, "invalid value range");
        return;
    }

    const bool ok = comfo->setStatus(
        static_cast<uint8_t>(preheater), static_cast<uint8_t>(bypass),
        static_cast<uint8_t>(type), static_cast<uint8_t>(size),
        static_cast<uint8_t>(options), static_cast<uint8_t>(enthalpy),
        static_cast<uint8_t>(ewt));
    sendResult(request, ok, "setStatus failed");
}

void setVentilationLevels(AsyncWebServerRequest* request, ComfoAirDriver* comfo) {
    if (!requireDriver(request, comfo)) return;

    static const char* const required[] = {
        "exhaustAbsent", "exhaustLow", "exhaustMedium",
        "supplyAbsent", "supplyLow", "supplyMedium",
        "exhaustHigh", "supplyHigh"
    };
    if (!requireArgs(request, required)) return;

    uint8_t values[8];
    for (size_t i = 0; i < 8; ++i) {
        const int v = request->arg(required[i]).toInt();
        if (!inRange(v, 0, 100)) { sendBadRequest(request, "values must be 0..100"); return; }
        values[i] = static_cast<uint8_t>(v);
    }

    const bool ok = comfo->setVentilationLevels(
        values[0], values[1], values[2], values[3], values[4], values[5], values[6], values[7]);
    sendResult(request, ok, "setVentilationLevels failed");
}

void manualStart(AsyncWebServerRequest* request, ComfoAirDriver* comfo) {
    if (!requireDriver(request, comfo)) return;
    sendResult(request, comfo->startManualMode(), "startTestMode failed");
}

void manualFlaps(AsyncWebServerRequest* request, ComfoAirDriver* comfo) {
    if (!requireDriver(request, comfo)) return;

    const int bypass = request->arg("bypass").toInt();
    const int preheater = request->arg("preheater").toInt();
    auto valid = [](int v) { return v == 0 || v == 1 || v == 3; };
    if (!valid(bypass) || !valid(preheater)) { sendBadRequest(request, "bypass/preheater must be 0,1,3"); return; }

    const int delaySec = request->hasArg("delaySeconds") ? request->arg("delaySeconds").toInt() : loadTestAutoStopSec();
    if (!inRange(delaySec, 0, 600)) { sendBadRequest(request, "delaySeconds must be 0..600"); return; }

    const bool ok = comfo->setManualFlapsWithAutoStop(
        static_cast<uint8_t>(bypass), static_cast<uint8_t>(preheater), static_cast<uint16_t>(delaySec));
    if (ok) {
        saveTestAutoStopSec(delaySec);                                               // ComfoAirSettings -> config.json
        comfo->setManualAutoStopDelaySecFromConfig(static_cast<uint16_t>(delaySec)); // config -> MQTT
    }
    sendResult(request, ok, "setTestFlaps failed");
}

void manualEnd(AsyncWebServerRequest* request, ComfoAirDriver* comfo) {
    if (!requireDriver(request, comfo)) return;
    sendResult(request, comfo->endManualMode(), "endTestMode failed");
}

void manualAnalog(AsyncWebServerRequest* request, ComfoAirDriver* comfo) {
    if (!requireDriver(request, comfo)) return;

    const int supply = request->arg("supply").toInt();
    const int exhaust = request->arg("exhaust").toInt();
    const int postheater = request->arg("postheater").toInt();

    if (!inRange(supply, 0, 100) || !inRange(exhaust, 0, 100) || !inRange(postheater, 0, 100)) {
        sendBadRequest(request, "supply/exhaust/postheater must be 0..100");
        return;
    }

    const bool ok = comfo->setManualAnalog(
        static_cast<uint8_t>(supply), static_cast<uint8_t>(exhaust), static_cast<uint8_t>(postheater));
    if (ok) {
        ManualUiCfg cfg{};
        loadManualUiCfg(cfg);
        cfg.supply = supply;
        cfg.exhaust = exhaust;
        cfg.postheater = postheater;
        saveManualUiCfg(cfg);
    }
    sendResult(request, ok, "setTestAnalog failed");
}

void manualConfigGet(AsyncWebServerRequest* request) {
    ManualUiCfg cfg{};
    if (!loadManualUiCfg(cfg)) { request->send(500, kJson, kConfigReadFailed); return; }

    StaticJsonDocument<256> d;
    d["ok"] = true;
    d["delaySeconds"] = cfg.delaySeconds;
    d["supply"] = cfg.supply;
    d["exhaust"] = cfg.exhaust;
    d["postheater"] = cfg.postheater;
    sendJson(request, 200, d);
}

void manualConfigSave(AsyncWebServerRequest* request, ComfoAirDriver* comfo) {
    ManualUiCfg cfg{};
    loadManualUiCfg(cfg); // bestehende Werte als Fallback

    if (request->hasArg("delaySeconds")) cfg.delaySeconds = request->arg("delaySeconds").toInt();
    if (request->hasArg("supply"))       cfg.supply       = request->arg("supply").toInt();
    if (request->hasArg("exhaust"))      cfg.exhaust      = request->arg("exhaust").toInt();
    if (request->hasArg("postheater"))   cfg.postheater   = request->arg("postheater").toInt();

    if (!inRange(cfg.delaySeconds, 0, 600) || !inRange(cfg.supply, 0, 100) ||
        !inRange(cfg.exhaust, 0, 100) || !inRange(cfg.postheater, 0, 100)) {
        sendBadRequest(request, "invalid range");
        return;
    }

    const bool ok = saveManualUiCfg(cfg);
    if (ok && comfo) comfo->setManualAutoStopDelaySecFromConfig(static_cast<uint16_t>(cfg.delaySeconds)); // config -> MQTT
    sendResult(request, ok, "save failed");
}

void mqttConfigGet(AsyncWebServerRequest* request) {
    MqttCfg c;
    if (!loadMqttCfg(c)) { request->send(500, kJson, kConfigReadFailed); return; }

    StaticJsonDocument<512> doc;
    doc["ok"] = true;
    doc["enabled"] = c.enabled;
    doc["host"] = c.host;
    doc["port"] = c.port;
    doc["user"] = c.user;
    doc["pass"] = c.pass;
    doc["clientId"] = c.clientId;
    doc["topicPrefix"] = c.topicPrefix;
    sendJson(request, 200, doc);
}

void mqttConfigSave(AsyncWebServerRequest* request, ComfoAirDriver* comfo) {
    MqttCfg c;
    if (!loadMqttCfg(c)) { request->send(500, kJson, kConfigReadFailed); return; }

    if (request->hasArg("enabled"))     c.enabled = toBoolArg(request->arg("enabled"));
    if (request->hasArg("host"))        c.host = request->arg("host");
    if (request->hasArg("port"))        c.port = request->arg("port").toInt();
    if (request->hasArg("user"))        c.user = request->arg("user");
    if (request->hasArg("pass"))        c.pass = request->arg("pass");
    if (request->hasArg("clientId"))    c.clientId = request->arg("clientId");
    if (request->hasArg("topicPrefix")) c.topicPrefix = request->arg("topicPrefix");

    c.host.trim();
    c.topicPrefix = normalizeTopicPrefix(c.topicPrefix);

    if (c.enabled && c.host.isEmpty()) { sendBadRequest(request, "host required when mqtt enabled"); return; }
    if (!inRange(c.port, 1, 65535))    { sendBadRequest(request, "port must be 1..65535"); return; }

    if (!saveMqttCfg(c)) { sendError(request, 500, "save failed"); return; }

    // PubSubClient ist nicht threadsicher und der Verbindungsaufbau blockiert:
    // die Übernahme erfolgt deshalb im nächsten mqttLoop() (loop()-Task).
    if (comfo) {
        const ComfoMqttConfig cfg{
            c.host.c_str(), c.port, c.user.c_str(), c.pass.c_str(), c.clientId.c_str(), c.topicPrefix.c_str()
        };
        comfo->mqttRequestConfig(cfg, c.enabled);
    }
    request->send(200, kJson, "{\"ok\":true,\"applied\":\"pending\"}");
}

void registerRoutes(AsyncWebServer& server, ComfoAirDriver*& comfo) {
    using Req = AsyncWebServerRequest;

    server.on("/api/comfo/state", HTTP_GET, [&comfo](Req* r) { state(r, comfo); });
    server.on("/api/comfo/setLevel", HTTP_POST, [&comfo](Req* r) { setLevel(r, comfo); });
    server.on("/api/comfo/setComfort", HTTP_POST, [&comfo](Req* r) { setComfort(r, comfo); });
    server.on("/api/comfo/setStatus", HTTP_POST, [&comfo](Req* r) { setStatus(r, comfo); });
    server.on("/api/comfo/setVentilationLevels", HTTP_POST, [&comfo](Req* r) { setVentilationLevels(r, comfo); });

    // "manual" ist der aktuelle Pfad, "test" der Alt-Alias.
    for (const char* base : { "/api/comfo/manual", "/api/comfo/test" }) {
        const String b(base);
        server.on((b + "/start").c_str(), HTTP_POST, [&comfo](Req* r) { manualStart(r, comfo); });
        server.on((b + "/flaps").c_str(), HTTP_POST, [&comfo](Req* r) { manualFlaps(r, comfo); });
        server.on((b + "/end").c_str(), HTTP_POST, [&comfo](Req* r) { manualEnd(r, comfo); });
        server.on((b + "/analog").c_str(), HTTP_POST, [&comfo](Req* r) { manualAnalog(r, comfo); });
        server.on((b + "/config").c_str(), HTTP_GET, [](Req* r) { manualConfigGet(r); });
        server.on((b + "/config").c_str(), HTTP_POST, [&comfo](Req* r) { manualConfigSave(r, comfo); });
    }

    server.on("/api/mqtt/config", HTTP_GET, [](Req* r) { mqttConfigGet(r); });
    server.on("/api/mqtt/config", HTTP_POST, [&comfo](Req* r) { mqttConfigSave(r, comfo); });
}

} // namespace ComfoHttpApi
