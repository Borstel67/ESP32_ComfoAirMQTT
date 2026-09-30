// SPDX-License-Identifier: MIT
// Gemeinsame HTTP-API-Handler für AP- und STA-Webserver.
#include "ComfoHttpApi.h"
#include "ESPWiFiManagerCommon.h"
#include <ArduinoJson.h>
#include <math.h>

namespace {
    const char* kNoDriver = "{\"ok\":false,\"error\":\"No driver\"}";

    void sendJson(AsyncWebServerRequest* request, int code, const JsonDocument& doc) {
        String out;
        serializeJson(doc, out);
        request->send(code, "application/json", out);
    }

    void sendResult(AsyncWebServerRequest* request, bool ok, const char* error) {
        if (ok) request->send(200, "application/json", "{\"ok\":true}");
        else    request->send(200, "application/json", String("{\"ok\":false,\"error\":\"") + error + "\"}");
    }

    void sendBadRequest(AsyncWebServerRequest* request, const String& error) {
        request->send(400, "application/json", String("{\"ok\":false,\"error\":\"") + error + "\"}");
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

    bool toBoolArg(const String& s) {
        String v = s;
        v.toLowerCase();
        return v == "1" || v == "true" || v == "on" || v == "yes";
    }
}

namespace ComfoHttpApi {

void state(AsyncWebServerRequest* request, ComfoAirDriver* comfo) {
    if (!comfo) { request->send(503, "application/json", kNoDriver); return; }

    ComfoSnapshot s;
    comfo->getSnapshot(s);

    JsonDocument doc;
    doc["ok"] = s.ok;
    doc["manualModeActive"] = s.manualModeActive;

    JsonObject f = doc["fan"].to<JsonObject>();
    f["supplyPct"] = s.fan.supplyPercent; f["exhaustPct"] = s.fan.exhaustPercent;
    f["supplyRpm"] = s.fan.supplyRpm;     f["exhaustRpm"] = s.fan.exhaustRpm;

    JsonObject t = doc["temps"].to<JsonObject>();
    t["t1"] = ComfoAirDriver::decodeTempC(s.temps.t1Outside);
    t["t2"] = ComfoAirDriver::decodeTempC(s.temps.t2Supply);
    t["t3"] = ComfoAirDriver::decodeTempC(s.temps.t3Exhaust);
    t["t4"] = ComfoAirDriver::decodeTempC(s.temps.t4Outlet);
    if (s.tempsD1Valid)         t["comfortC"] = ComfoAirDriver::decodeTempC(s.tempsD1.comfortTemp);
    else if (s.comfortCmdValid) t["comfortC"] = s.comfortCmdC;

    JsonObject a = doc["ain"].to<JsonObject>();
    a["a1"] = s.ain.a1; a["a2"] = s.ain.a2; a["a3"] = s.ain.a3; a["a4"] = s.ain.a4;

    JsonObject v = doc["vent"].to<JsonObject>();
    v["current"]       = s.vent.currentLevel;
    v["supplyCur"]     = s.vent.supplyCurrent;
    v["exhaustCur"]    = s.vent.exhaustCurrent;
    v["supplyHigh"]    = s.vent.supplyHigh;
    v["exhaustHigh"]   = s.vent.exhaustHigh;
    v["exhaustAbsent"] = s.vent.exhaustAbsent;
    v["exhaustLow"]    = s.vent.exhaustLow;
    v["exhaustMedium"] = s.vent.exhaustMedium;
    v["supplyAbsent"]  = s.vent.supplyAbsent;
    v["supplyLow"]     = s.vent.supplyLow;
    v["supplyMedium"]  = s.vent.supplyMedium;

    JsonObject b = doc["bypass"].to<JsonObject>();
    b["factor"] = s.bypass.factor; b["stage"] = s.bypass.stage;
    b["correction"] = s.bypass.correction; b["summerMode"] = s.bypass.summerMode;

    JsonObject st = doc["statusD5"].to<JsonObject>();
    st["preheaterPresent"] = s.anlagenconf.preheaterPresent;
    st["bypassPresent"]    = s.anlagenconf.bypassPresent;
    st["type"]             = s.anlagenconf.type;
    st["size"]             = s.anlagenconf.size;
    st["options"]          = s.anlagenconf.options;
    st["activeStatus1"]    = s.anlagenconf.activeStatus1;
    st["activeStatus2"]    = s.anlagenconf.activeStatus2;
    st["activeStatus3"]    = s.anlagenconf.activeStatus3;
    st["enthalpyPresent"]  = s.anlagenconf.enthalpyPresent;
    st["ewtPresent"]       = s.anlagenconf.ewtPresent;

    JsonObject fl = doc["flap"].to<JsonObject>();
    fl["bypassPercent"]  = s.flap.bypassPercent;
    fl["preheaterState"] = s.flap.preheaterState;
    fl["bypassAdc"]      = s.flap.bypassAdc;
    fl["preheaterAdc"]   = s.flap.preheaterAdc;

    JsonObject m = doc["manual"].to<JsonObject>();
    m["active"]                 = s.manualModeActive;
    m["requestedBypass"]        = s.manualRequestedBypass;
    m["requestedPreheater"]     = s.manualRequestedPreheater;
    m["requestedBypassText"]    = comfo->getManualRequestedBypassText();
    m["requestedPreheaterText"] = comfo->getManualRequestedPreheaterText();

    if (s.rsModeValid) doc["rs232Mode"] = s.rsMode;
    sendJson(request, 200, doc);
}

void setLevel(AsyncWebServerRequest* request, ComfoAirDriver* comfo) {
    if (!comfo) { request->send(503, "application/json", kNoDriver); return; }
    if (!request->hasArg("level")) { sendBadRequest(request, "missing arg: level"); return; }

    const int level = request->arg("level").toInt(); // 0..4
    if (level < 0 || level > 4) { sendBadRequest(request, "level must be 0..4"); return; }

    sendResult(request, comfo->setLevel(static_cast<uint8_t>(level)), "setLevel failed");
}

void setComfort(AsyncWebServerRequest* request, ComfoAirDriver* comfo) {
    if (!comfo) { request->send(503, "application/json", kNoDriver); return; }
    if (!request->hasArg("tempC")) { sendBadRequest(request, "missing arg: tempC"); return; }

    const int tc = clampInt(static_cast<int>(lroundf(request->arg("tempC").toFloat())), -40, 100);
    sendResult(request, comfo->setComfortTemperatureC(static_cast<int8_t>(tc)), "setComfort failed (0x00D3)");
}

void setStatus(AsyncWebServerRequest* request, ComfoAirDriver* comfo) {
    if (!comfo) { request->send(503, "application/json", kNoDriver); return; }

    static const char* const required[] = { "preheater", "bypass", "type", "size", "options", "enthalpy", "ewt" };
    if (!requireArgs(request, required)) return;

    const int preheater = request->arg("preheater").toInt(); // 0/1
    const int bypass    = request->arg("bypass").toInt();    // 0/1
    const int type      = request->arg("type").toInt();      // 1/2
    const int size      = request->arg("size").toInt();      // 1/2
    const int options   = request->arg("options").toInt();   // 0..255
    const int enthalpy  = request->arg("enthalpy").toInt();  // 0/1/2
    const int ewt       = request->arg("ewt").toInt();       // 0/1/2

    if ((preheater < 0 || preheater > 1) ||
        (bypass < 0 || bypass > 1) ||
        (type < 1 || type > 2) ||
        (size < 1 || size > 2) ||
        (options < 0 || options > 255) ||
        (enthalpy < 0 || enthalpy > 2) ||
        (ewt < 0 || ewt > 2)) {
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
    if (!comfo) { request->send(503, "application/json", kNoDriver); return; }

    static const char* const required[] = {
        "exhaustAbsent", "exhaustLow", "exhaustMedium",
        "supplyAbsent", "supplyLow", "supplyMedium",
        "exhaustHigh", "supplyHigh"
    };
    if (!requireArgs(request, required)) return;

    int values[8];
    for (size_t i = 0; i < 8; ++i) {
        values[i] = request->arg(required[i]).toInt();
        if (values[i] < 0 || values[i] > 100) { sendBadRequest(request, "values must be 0..100"); return; }
    }

    const bool ok = comfo->setVentilationLevels(
        static_cast<uint8_t>(values[0]), static_cast<uint8_t>(values[1]),
        static_cast<uint8_t>(values[2]), static_cast<uint8_t>(values[3]),
        static_cast<uint8_t>(values[4]), static_cast<uint8_t>(values[5]),
        static_cast<uint8_t>(values[6]), static_cast<uint8_t>(values[7]));
    sendResult(request, ok, "setVentilationLevels failed");
}

void manualStart(AsyncWebServerRequest* request, ComfoAirDriver* comfo) {
    if (!comfo) { request->send(503, "application/json", kNoDriver); return; }
    sendResult(request, comfo->startManualMode(), "startTestMode failed");
}

void manualFlaps(AsyncWebServerRequest* request, ComfoAirDriver* comfo) {
    if (!comfo) { request->send(503, "application/json", kNoDriver); return; }

    const int bypass = request->arg("bypass").toInt();
    const int preheater = request->arg("preheater").toInt();
    auto valid = [](int v) { return v == 0 || v == 1 || v == 3; };
    if (!valid(bypass) || !valid(preheater)) { sendBadRequest(request, "bypass/preheater must be 0,1,3"); return; }

    const int delaySec = request->hasArg("delaySeconds") ? request->arg("delaySeconds").toInt() : loadTestAutoStopSec();
    if (delaySec < 0 || delaySec > 600) { sendBadRequest(request, "delaySeconds must be 0..600"); return; }

    const bool ok = comfo->setManualFlapsWithAutoStop(
        static_cast<uint8_t>(bypass), static_cast<uint8_t>(preheater), static_cast<uint16_t>(delaySec));
    if (ok) {
        saveTestAutoStopSec(delaySec);                                          // ComfoAirSettings -> config.json
        comfo->setManualAutoStopDelaySecFromConfig(static_cast<uint16_t>(delaySec)); // config -> MQTT
    }
    sendResult(request, ok, "setTestFlaps failed");
}

void manualEnd(AsyncWebServerRequest* request, ComfoAirDriver* comfo) {
    if (!comfo) { request->send(503, "application/json", kNoDriver); return; }
    sendResult(request, comfo->endManualMode(), "endTestMode failed");
}

void manualAnalog(AsyncWebServerRequest* request, ComfoAirDriver* comfo) {
    if (!comfo) { request->send(503, "application/json", kNoDriver); return; }

    const int supply = request->arg("supply").toInt();
    const int exhaust = request->arg("exhaust").toInt();
    const int postheater = request->arg("postheater").toInt();

    auto valid = [](int v) { return v >= 0 && v <= 100; };
    if (!valid(supply) || !valid(exhaust) || !valid(postheater)) {
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
    if (!loadManualUiCfg(cfg)) {
        request->send(500, "application/json", "{\"ok\":false,\"error\":\"config read failed\"}");
        return;
    }

    JsonDocument d;
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

    if (cfg.delaySeconds < 0 || cfg.delaySeconds > 600 ||
        cfg.supply < 0 || cfg.supply > 100 ||
        cfg.exhaust < 0 || cfg.exhaust > 100 ||
        cfg.postheater < 0 || cfg.postheater > 100) {
        sendBadRequest(request, "invalid range");
        return;
    }

    const bool ok = saveManualUiCfg(cfg);
    if (ok && comfo) comfo->setManualAutoStopDelaySecFromConfig(static_cast<uint16_t>(cfg.delaySeconds));
    sendResult(request, ok, "save failed");
}

void mqttConfigGet(AsyncWebServerRequest* request) {
    MqttCfg c;
    if (!loadMqttCfg(c)) {
        request->send(500, "application/json", "{\"ok\":false,\"error\":\"config read failed\"}");
        return;
    }

    JsonDocument doc;
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
    c.topicPrefix.trim();
    while (c.topicPrefix.startsWith("/")) c.topicPrefix.remove(0, 1);
    while (c.topicPrefix.endsWith("/"))   c.topicPrefix.remove(c.topicPrefix.length() - 1, 1);
    if (c.topicPrefix.isEmpty()) c.topicPrefix = "comfoair";

    if (c.enabled && c.host.isEmpty()) { sendBadRequest(request, "host required when mqtt enabled"); return; }
    if (c.port < 1 || c.port > 65535)  { sendBadRequest(request, "port must be 1..65535"); return; }

    if (!saveMqttCfg(c)) {
        request->send(500, "application/json", "{\"ok\":false,\"error\":\"save failed\"}");
        return;
    }

    // PubSubClient ist nicht threadsicher: Übernahme erfolgt im nächsten mqttLoop() (loop()-Task).
    if (comfo) {
        const ComfoMqttConfig cfg{
            c.host.c_str(), c.port, c.user.c_str(), c.pass.c_str(), c.clientId.c_str(), c.topicPrefix.c_str()
        };
        comfo->mqttRequestConfig(cfg, c.enabled);
    }
    request->send(200, "application/json", "{\"ok\":true,\"applied\":\"pending\"}");
}

} // namespace ComfoHttpApi
