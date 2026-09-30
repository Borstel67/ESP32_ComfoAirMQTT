// ComfoAirDriver.cpp
// RS232-Treiber für Zehnder ComfoAir 350:
// - Frame-Protokoll (senden, empfangen, Escaping, Checksumme)
// - Alle Lese-/Schreibkommandos 0x0001..0x00EB
// - Manueller Modus mit Auto-Stop und Klappenverriegelung
// - MQTT-Integration (Change-Only Publishing, Reconnect, Subscriptions)
#include "ComfoAirDriver.h"
#include <ArduinoJson.h>
#include <WiFi.h> // neu: für WiFi.status()
#include "ESPWiFiManagerCommon.h" // loadConfigJson/saveConfigJson

ComfoAirDriver* ComfoAirDriver::_cbInstance = nullptr;

namespace {
    // Hilfsfunktionen für persistente config.json-Operationen.
    // Alle Funktionen lesen den aktuellen Stand, ergänzen die neuen Werte
    // und schreiben das Dokument zurück ins NVS.
    static int clampCfg(int v, int lo, int hi) {
        if (v < lo) return lo;
        if (v > hi) return hi;
        return v;
    }

    static bool saveCfgDoc(StaticJsonDocument<2048>& doc) {
        String out; serializeJson(doc, out);
        return saveConfigJson(out);
    }

    static void loadCfgDoc(StaticJsonDocument<2048>& doc) {
        String json;
        if (loadConfigJson(json)) deserializeJson(doc, json);
    }

    static bool persistLevelToConfig(int level) {
        // Speichert den zuletzt gesetzten Vent-Level (0–4) in config.json.
        StaticJsonDocument<2048> doc; loadCfgDoc(doc);
        doc["setLevel"] = clampCfg(level, 0, 4);
        return saveCfgDoc(doc);
    }

    static bool persistComfortToConfig(int tempC) {
        // Speichert die Komforttemperatur (°C) in config.json.
        StaticJsonDocument<2048> doc; loadCfgDoc(doc);
        doc["setComfortTempC"] = clampCfg(tempC, -40, 100);
        return saveCfgDoc(doc);
    }

    static bool persistManualActiveToConfig(bool active) {
        // Speichert den manuellen Modus-Zustand (true/false) in config.json.
        StaticJsonDocument<2048> doc; loadCfgDoc(doc);
        doc["manualActive"] = active;
        return saveCfgDoc(doc);
    }

    static bool persistManualDelayToConfig(int delaySeconds) {
        // Speichert den Auto-Stop-Delay (0–600s) in config.json.
        StaticJsonDocument<2048> doc; loadCfgDoc(doc);
        doc["testAutoStopSec"] = clampCfg(delaySeconds, 0, 600);
        return saveCfgDoc(doc);
    }

    static bool persistManualAnalogToConfig(int supply, int exhaust, int postheater) {
        // Speichert die letzten Analog-Sollwerte (0–100%) in config.json.
        StaticJsonDocument<2048> doc; loadCfgDoc(doc);
        doc["manualAnalogSupply"] = clampCfg(supply, 0, 100);
        doc["manualAnalogExhaust"] = clampCfg(exhaust, 0, 100);
        doc["manualAnalogPostheater"] = clampCfg(postheater, 0, 100);
        return saveCfgDoc(doc);
    }

    static bool persistStatusToConfig(int preheater, int bypass, int type, int size, int options, int enthalpy, int ewt) {
        // Speichert die Anlagenkonfiguration (0x00D7-Parameter) in config.json.
        StaticJsonDocument<2048> doc; loadCfgDoc(doc);
        auto s = doc.createNestedObject("setStatus");
        s["preheater"] = clampCfg(preheater, 0, 1);
        s["bypass"] = clampCfg(bypass, 0, 1);
        s["type"] = clampCfg(type, 1, 2);
        s["size"] = clampCfg(size, 1, 2);
        s["options"] = clampCfg(options, 0, 255);
        s["enthalpy"] = clampCfg(enthalpy, 0, 2);
        s["ewt"] = clampCfg(ewt, 0, 2);
        return saveCfgDoc(doc);
    }

    static bool persistVentToConfig(int ea, int el, int em, int sa, int sl, int sm, int eh, int sh) {
        // Speichert die Ventilationsstufenprozente in config.json.
        StaticJsonDocument<2048> doc; loadCfgDoc(doc);
        auto v = doc.createNestedObject("setVent");
        v["exhaustAbsent"] = clampCfg(ea, 0, 100);
        v["exhaustLow"] = clampCfg(el, 0, 100);
        v["exhaustMedium"] = clampCfg(em, 0, 100);
        v["supplyAbsent"] = clampCfg(sa, 0, 100);
        v["supplyLow"] = clampCfg(sl, 0, 100);
        v["supplyMedium"] = clampCfg(sm, 0, 100);
        v["exhaustHigh"] = clampCfg(eh, 0, 100);
        v["supplyHigh"] = clampCfg(sh, 0, 100);
        return saveCfgDoc(doc);
    }

    static bool tryParseIntLoose(const String& in, const char* key, int& out) {
        String m = in;
        m.trim();

        // 1) JSON-Objekt oder JSON-Skalar
        {
            StaticJsonDocument<128> d;
            if (!deserializeJson(d, m)) {
                if (d.is<JsonObject>()) {
                    out = d[key] | -32768;
                    if (out != -32768) return true;
                } else if (d.is<int>()) {
                    out = d.as<int>();
                    return true;
                }
            }
        }

        // 2) Fragment: "key": 1  -> zu {"key":1} wrappen
        if (m.indexOf(':') >= 0 && m.indexOf('{') < 0) {
            StaticJsonDocument<128> d;
            String wrapped = "{" + m + "}";
            if (!deserializeJson(d, wrapped)) {
                out = d[key] | -32768;
                if (out != -32768) return true;
            }
        }

        // 3) Reine Zahl
        char* endPtr = nullptr;
        long v = strtol(m.c_str(), &endPtr, 10);
        if (endPtr && *endPtr == '\0') {
            out = (int)v;
            return true;
        }

        return false;
    }
    static String normalizeTopicPrefix(String p) {
        p.trim();
        while (p.startsWith("/")) p.remove(0, 1);
        while (p.endsWith("/")) p.remove(p.length() - 1, 1);
        if (p.isEmpty()) p = "comfoair";
        return p;
    }
}

static String toHex(const std::vector<uint8_t>& v) {
    // Wandelt einen Byte-Vektor in einen HEX-String um (z.B. für Logging).
    static const char* k = "0123456789ABCDEF";
    String s; s.reserve(v.size() * 2);
    for (uint8_t b : v) { s += k[b >> 4]; s += k[b & 0x0F]; }
    return s;
}

ComfoAirDriver::ComfoAirDriver(HardwareSerial& port) : _serial(&port), _ready(false) {}

bool ComfoAirDriver::begin(uint32_t baud, int8_t rxPin, int8_t txPin) {
	_serial->begin(baud, SERIAL_8N1, rxPin, txPin);
	_ready = true;
	AWM_LOGI("Serial init ok, RX=%d TX=%d baud=%lu", rxPin, txPin, static_cast<unsigned long>(baud));
	return _ready;
}

// 6.1 Checksumme: Summe (cmd hi, cmd lo, len, data) + 173, nur niederwertiges Byte
uint8_t ComfoAirDriver::computeChecksum(uint16_t command, const std::vector<uint8_t>& data) {
	uint32_t sum = 0;
	sum += (command >> 8) & 0xFF;
	sum += (command)&0xFF;
	sum += data.size();
	for (size_t i = 0; i < data.size(); ++i) sum += data[i];
	sum += 173;
	return static_cast<uint8_t>(sum & 0xFF);
}

// Hilfsfunktion: Frame schreiben inkl. Escaping von 0x07
void ComfoAirDriver::writeFrame(uint16_t command, const std::vector<uint8_t>& payload) {
	if (!_ready) return;
	AWM_LOGD("Tx cmd=0x%04X len=%u", command, static_cast<unsigned>(payload.size()));

	std::vector<uint8_t> out;
	out.reserve(6 + payload.size() * 2);

	out.push_back(CA_START_HI);
	out.push_back(CA_START_LO);
	out.push_back((command >> 8) & 0xFF);
	out.push_back(command & 0xFF);
	out.push_back(static_cast<uint8_t>(payload.size()));

	// Daten mit Verdopplung von 0x07
	for (uint8_t b : payload) {
		out.push_back(b);
		if (b == 0x07) out.push_back(0x07);
	}

	out.push_back(computeChecksum(command, payload));
	out.push_back(CA_END_HI);
	out.push_back(CA_END_LO);

	_serial->write(out.data(), out.size());
	_serial->flush();
}

// Hilfsfunktion: Ack lesen
bool ComfoAirDriver::readAck(uint32_t timeoutMs) {
	const uint32_t t0 = millis();
	while (millis() - t0 < timeoutMs) {
		if (_serial->available() >= 2) {
			uint8_t hi = _serial->read();
			uint8_t lo = _serial->read();
			bool ok = (hi == CA_ACK_HI && lo == CA_ACK_LO);
			if (ok) AWM_LOGD("ACK ok");
			return ok;
		}
		delay(1);
	}
	AWM_LOGW("ACK timeout");
	return false;
}

// Hilfsfunktion: Frame lesen und de-escapen
bool ComfoAirDriver::readFrame(uint16_t& command, std::vector<uint8_t>& data, uint32_t timeoutMs) {
	const uint32_t t0 = millis();
	enum State { WAIT_START_HI, WAIT_START_LO, CMD_HI, CMD_LO, LEN, DATA, CHECK, END_HI, END_LO };
	State st = WAIT_START_HI;

	uint8_t len = 0;
	std::vector<uint8_t> rawData;

	while (millis() - t0 < timeoutMs) {
		if (!_serial->available()) { delay(1); continue; }
		uint8_t b = _serial->read();

		switch (st) {
		case WAIT_START_HI:
			if (b == CA_START_HI) st = WAIT_START_LO;
			break;
		case WAIT_START_LO:
			if (b == CA_START_LO) st = CMD_HI; else st = WAIT_START_HI;
			break;
		case CMD_HI:
			command = static_cast<uint16_t>(b) << 8;
			st = CMD_LO;
			break;
		case CMD_LO:
			command |= b;
			st = LEN;
			break;
		case LEN:
			len = b;
			st = (len == 0) ? CHECK : DATA;
			break;
		case DATA:
			rawData.push_back(b);

    // De-escape: doppelte 0x07 -> einfaches 0x07.
    // Wichtig: nicht endlos warten, falls nach 0x07 kein weiteres Byte kommt.
    if (b == 0x07) {
        const uint32_t waitStart = millis();
        while (!_serial->available() && millis() - waitStart < timeoutMs) {
            delay(1);
        }

        if (_serial->available() && _serial->peek() == 0x07) {
            _serial->read();
        }
    }

    if (rawData.size() >= len) st = CHECK;
    break;
		case CHECK: {
			uint8_t expected = computeChecksum(command, rawData);
			if (expected != b) {
				AWM_LOGW("Checksum fail cmd=0x%04X exp=0x%02X got=0x%02X", command, expected, b);
				return false;
			}
			st = END_HI;
			break;
		}
		case END_HI:
			if (b != CA_END_HI) return false;
			st = END_LO;
			break;
		case END_LO:
			if (b != CA_END_LO) return false;
			data = rawData;
			AWM_LOGD("Rx cmd=0x%04X len=%u", command, static_cast<unsigned>(data.size()));
			return true;
		}
	}
	AWM_LOGW("Frame timeout");
	return false;
}

// Generischer Sender mit Ack + Antwort
bool ComfoAirDriver::sendCommand(uint16_t command,
                                 const std::vector<uint8_t>& payload,
                                 uint16_t& responseCommand,
                                 std::vector<uint8_t>& responsePayload,
                                 uint32_t timeoutMs) {
    if (!_ready) {
        AWM_LOGE("RS232 nicht bereit – ist die serielle Verbindung gesteckt?");
        return false;
    }

    while (_serial->available()) _serial->read();

    writeFrame(command, payload); // FIX

    bool ackOk = readAck(100);
    if (!ackOk) {
        AWM_LOGE("Kein ACK für cmd=0x%04X (Serial verbunden?)", command);
        return false;
    }

    bool ok = readFrame(responseCommand, responsePayload, timeoutMs);
    if (!ok) AWM_LOGW("Keine Antwort für cmd=0x%04X", command);
    return ok;
}

// 7.0
bool ComfoAirDriver::getBootloaderVersion(std::vector<uint8_t>& response) {
    uint16_t rc; return sendCommand(0x0067, {}, rc, response);
}
bool ComfoAirDriver::getFirmwareVersion(std::vector<uint8_t>& response) {
    uint16_t rc; return sendCommand(0x0069, {}, rc, response);
}
bool ComfoAirDriver::getConnectorBoardVersion(std::vector<uint8_t>& response) {
    uint16_t rc; return sendCommand(0x00A1, {}, rc, response);
}
bool ComfoAirDriver::setRs232Mode(uint8_t mode, std::vector<uint8_t>& response) {
    uint16_t rc; return sendCommand(0x009B, { mode }, rc, response);
}

// 8.0
// 0x0033 – Daten abrufen (liefert Antwort-Kommando + Payload roh)
bool ComfoAirDriver::easeRequestData(uint16_t& responseCommand,
                                     std::vector<uint8_t>& responsePayload) {
    return sendCommand(0x0033, {}, responseCommand, responsePayload);
}

// Kompatibilitäts-Wrapper: nur Payload zurückgeben
bool ComfoAirDriver::easeGetData(std::vector<uint8_t>& response) {
	uint16_t rc;
	return easeRequestData(rc, response);
}

// 0x0035 – CC-Ease Parameter
bool ComfoAirDriver::easeSetParams(const ComfoEaseParams& params) {
	std::vector<uint8_t> payload;
	payload.reserve(5);
	// Byte[1] RTC Tag (0=Sa,1=So,2=Mo,3=Di,4=Mi,5=Do,6=Fr)
	payload.push_back(params.rtcDay);
	// Byte[2] RTC Stunde (0..23)
	payload.push_back(params.rtcHour);
	// Byte[3] RTC Minute (0..59)
	payload.push_back(params.rtcMinute);
	// Byte[4] Temperatur (°C*) Rohwert, z.B. (T+20)*2
 payload.push_back(static_cast<uint8_t>(params.tempRaw));
	// Byte[5] Version CC-Ease (Bits 7..4=Major, 3..0=Minor)
	payload.push_back(params.versionEase);

	uint16_t rc;
	std::vector<uint8_t> resp;
	return sendCommand(0x0035, payload, rc, resp); // Antwort 0x003C (Display), hier nicht dekodiert
}

// 0x0037 – CC-Ease Tasten Status
bool ComfoAirDriver::easeSendKeyStatus(const ComfoEaseKeyStatus& keys) {
	std::vector<uint8_t> payload;
	payload.reserve(7);
	// Byte[1] Ventilator (0..255 = 0..4080 ms)
	payload.push_back(keys.ventMs);
	// Byte[2] Modus
	payload.push_back(keys.modeMs);
	// Byte[3] Uhr
	payload.push_back(keys.clockMs);
	// Byte[4] Temperatur
	payload.push_back(keys.tempMs);
	// Byte[5] Plus
	payload.push_back(keys.plusMs);
	// Byte[6] Minus
 payload.push_back(keys.minusMs);
	// Byte[7] Status-Bits (Bit0 = Blinken an/aus)
	payload.push_back(keys.status);

	uint16_t rc;
	std::vector<uint8_t> resp;
	return sendCommand(0x0037, payload, rc, resp); // Antwort 0x003C
}

// 0x0039 – Empfangenes RF Kommando weiterleiten
bool ComfoAirDriver::easeSendRfCommand(const ComfoRfRxCommand& cmd) {
	std::vector<uint8_t> payload;
	payload.reserve(16);
	// Byte[1] Modul Typ Empfänger
	payload.push_back(cmd.receiverType);
	// Byte[2] Modul ID Empfänger
	payload.push_back(cmd.receiverId);
	// Byte[3] Modul Typ Sender
	payload.push_back(cmd.senderType);
	// Byte[4] Modul ID Sender
	payload.push_back(cmd.senderId);
	// Byte[5] Lifetime
	payload.push_back(cmd.lifetime);
	// Byte[6] Datentyp
	payload.push_back(cmd.dataType);
	// Byte[7..16] Datenbyte 1..10
	for (size_t i = 0; i < 10; ++i) payload.push_back(cmd.data[i]);

	uint16_t rc;
	std::vector<uint8_t> resp;
	return sendCommand(0x0039, payload, rc, resp); // Antwort 0x0040
}

// Bestehende Roh-Variante für Payload (falls noch verwendet)
bool ComfoAirDriver::easeSetParams(const std::vector<uint8_t>& payload) {
	uint16_t rc; std::vector<uint8_t> resp;
	return sendCommand(0x0035, payload, rc, resp);
}

// 7.0 Allgemein – komfortable Getter

// Hilfsfunktion: rohen Versions-Puffer (0x0068/0x006A) in ComfoVersionInfo dekodieren
static bool parseVersionVector(const std::vector<uint8_t>& resp, ComfoVersionInfo& info) {
	// Erwartet: mind. 13 Bytes: [1]=maj, [2]=min, [3]=beta, [4..13]=Name
	if (resp.size() < 13) return false;

	info.versionMajor = resp[0];  // Byte[1] Version Major
	info.versionMinor = resp[1];  // Byte[2] Version Minor
	info.beta         = resp[2];  // Byte[3] Beta

	// Byte[4..13] = Gerätename (10 ASCII-Zeichen)
	for (size_t i = 0; i < 10; ++i) {
    info.deviceName[i] = static_cast<char>(resp[2 + i]);
}
	info.deviceName[10] = '\0';

	return true;
}

bool ComfoAirDriver::getBootloaderVersionInfo(ComfoVersionInfo& info) {
	std::vector<uint8_t> resp;
	uint16_t rc;
	// 0x0067 (PC an ComfoAir) → Antwort 0x0068 mit Versions-Daten
	if (!sendCommand(0x0067, {}, rc, resp)) return false;
	// optional: prüfen, ob rc == 0x0068 ist
	return parseVersionVector(resp, info);
}

bool ComfoAirDriver::getFirmwareVersionInfo(ComfoVersionInfo& info) {
	std::vector<uint8_t> resp;
	uint16_t rc;
	// 0x0069 (PC an ComfoAir) → Antwort 0x006A mit Firmware-Version
	if (!sendCommand(0x0069, {}, rc, resp)) return false;
	// optional: prüfen, ob rc == 0x006A ist
	return parseVersionVector(resp, info);
}

bool ComfoAirDriver::getConnectorBoardVersionInfo(ComfoConnectorVersionInfo& info) {
	std::vector<uint8_t> resp;
	uint16_t rc;
	// 0x00A1 (PC an ComfoAir) → Antwort 0x00A2 mit Konnektorplatinen-Version
	if (!sendCommand(0x00A1, {}, rc, resp)) return false;
	// Erwartetes Format:
	// Byte[1]  = Version Major
	// Byte[2]  = Version Minor
	// Byte[3–12] = Gerätename (10 ASCII)
	// Byte[13] = Version CC-Ease (Bits 7..4=Major, 3..0=Minor)
	// Byte[14] = Version CC-Luxe (Bits 7..4=Major, 3..0=Minor)
	if (resp.size() < 14) return false;

	info.versionMajor = resp[0];  // Byte[1]
	info.versionMinor = resp[1];  // Byte[2]

	// Gerätename aus Byte[3..12]
	for (size_t i = 0; i < 10; ++i) {
    info.deviceName[i] = static_cast<char>(resp[2 + i]);
}
	info.deviceName[10] = '\0';

	uint8_t ease = resp[12];      // Byte[13] Version CC-Ease
	uint8_t luxe = resp[13];      // Byte[14] Version CC-Luxe

	// CC-Ease: Bit 7..4 = Major, Bit 3..0 = Minor
	info.ccEaseMajor = (ease >> 4) & 0x0F;
	info.ccEaseMinor = ease & 0x0F;

	// CC-Luxe: Bit 7..4 = Major, Bit 3..0 = Minor
	info.ccLuxeMajor = (luxe >> 4) & 0x0F;
	info.ccLuxeMinor = luxe & 0x0F;

	return true;
}

// Nur den aktuellen RS232-Status (Byte[1] der 0x009C-Antwort) lesen
bool ComfoAirDriver::getRs232Mode(uint8_t& mode) {
    std::vector<uint8_t> resp;
    uint16_t rc;
    if (!sendCommand(0x009C, {}, rc, resp)) return false; // 0x009C = Status lesen
    if (resp.empty()) return false;
    mode = resp[0];
    return true;
}

// 7.1 – Lesekommandos dekodiert

bool ComfoAirDriver::getInputs(ComfoInputs& out) {
	std::vector<uint8_t> resp;
	uint16_t rc;
	if (!sendCommand(0x0003, {}, rc, resp)) return false;
	if (resp.size() < 2) return false;
	out.stageSwitch = resp[0]; // Byte[1]
	out.switches    = resp[1]; // Byte[2]
	return true;
}

bool ComfoAirDriver::getFanStatus(ComfoFanStatus& out) {
	std::vector<uint8_t> resp;
	uint16_t rc;
	if (!sendCommand(0x000B, {}, rc, resp)) return false;
	if (resp.size() < 6) return false;

	out.supplyPercent  = resp[0]; // Byte[1]
	out.exhaustPercent = resp[1]; // Byte[2]

	// Rohwerte little-endian zu rpm umrechnen; 0 -> 0 rpm
	uint16_t rawSup = (static_cast<uint16_t>(resp[2]) << 8) | resp[3];
	uint16_t rawExh = (static_cast<uint16_t>(resp[4]) << 8) | resp[5];
	out.supplyRpm  = (rawSup == 0) ? 0 : static_cast<uint16_t>(1875000 / rawSup);
	out.exhaustRpm = (rawExh == 0) ? 0 : static_cast<uint16_t>(1875000 / rawExh);

	AWM_LOGI("Fan: supply=%u%% (%u rpm) exhaust=%u%% (%u rpm)",
	         out.supplyPercent, out.supplyRpm, out.exhaustPercent, out.exhaustRpm);
	return true;
}

bool ComfoAirDriver::getFlapStatus(ComfoFlapStatus& out) {
	std::vector<uint8_t> resp;
	uint16_t rc = 0;
	if (!sendCommand(0x000D, {}, rc, resp)) return false;

	// Erwartete Antwort auf 0x000D ist 0x000E
	if (rc != 0x000E) {
		AWM_LOGW("Flap: unerwartetes Antwortkommando 0x%04X (erwartet 0x000E)", rc);
		return false;
	}

	if (resp.size() < 4) return false;

	out.bypassPercent  = resp[0]; // Byte[1] (0xFF = undefiniert)
	out.preheaterState = resp[1]; // Byte[2] (1=Offen / 0=Zu / 2=Unbekannt)
	out.bypassAdc      = resp[2]; // Byte[3] ADC Rohdaten
	out.preheaterAdc   = resp[3]; // Byte[4] ADC Rohdaten

	AWM_LOGI("Flap: bypass=%u%% preheater=%u adcBy=%u adcPre=%u",
	         out.bypassPercent, out.preheaterState, out.bypassAdc, out.preheaterAdc);
	return true;
}

bool ComfoAirDriver::getTemperatureStatus(ComfoTempStatus& out) {
	std::vector<uint8_t> resp;
	uint16_t rc;
	if (!sendCommand(0x000F, {}, rc, resp)) return false;
	if (resp.size() < 4) return false;
	out.t1Outside = static_cast<int8_t>(resp[0]); // Byte[1]
	out.t2Supply  = static_cast<int8_t>(resp[1]); // Byte[2]
	out.t3Exhaust = static_cast<int8_t>(resp[2]); // Byte[3]
	out.t4Outlet  = static_cast<int8_t>(resp[3]); // Byte[4]
	AWM_LOGI("Temps: T1=%d T2=%d T3=%d T4=%d (°C*)",
             out.t1Outside, out.t2Supply, out.t3Exhaust, out.t4Outlet);
	return true;
}

bool ComfoAirDriver::getKeyStatus(uint8_t& keyStatus) {
	std::vector<uint8_t> resp;
	uint16_t rc;
	if (!sendCommand(0x0011, {}, rc, resp)) return false;
	if (resp.empty()) return false;
	keyStatus = resp[0]; // Byte[1]
	AWM_LOGI("KeyStatus: 0x%02X", keyStatus);
	return true;
}

bool ComfoAirDriver::getAnalogInputs(ComfoAnalogInputs& out) {
	std::vector<uint8_t> resp;
	uint16_t rc;
	if (!sendCommand(0x0013, {}, rc, resp)) return false;
	if (resp.size() < 4) return false;
	out.a1 = resp[0];
	out.a2 = resp[1];
	out.a3 = resp[2];
	out.a4 = resp[3];
	AWM_LOGI("AnalogIn: A1=%u A2=%u A3=%u A4=%u", out.a1, out.a2, out.a3, out.a4);
	return true;
}

bool ComfoAirDriver::getSensorData97(ComfoSensorData97& out) {
	std::vector<uint8_t> resp;
	uint16_t rc;
	if (!sendCommand(0x0097, {}, rc, resp)) return false;
	if (resp.size() < 17) return false;
	out.enthalpyTemp      = static_cast<int8_t>(resp[0]);
	out.enthalpyHumidity  = resp[1];
	out.analog1Percent    = resp[2];
	out.analog2Percent    = resp[3];
	out.enthalpyCoeff     = resp[4];
	out.enthalpyTimer     = resp[5];
	out.reserved          = resp[6]; // sollte 0x00 sein
	out.a1UpDesired       = resp[7];
	out.a1DownDesired     = resp[8];
	out.a2UpDesired       = resp[9];
	out.a2DownDesired     = resp[10];
	out.analog3Percent    = resp[11];
	out.analog4Percent    = resp[12];
	out.a3UpDesired       = resp[13];
	out.a3DownDesired     = resp[14];
	out.a4UpDesired       = resp[15];
	out.a4DownDesired     = resp[16];
	AWM_LOGI("Sensor97: enthT=%d enthH=%u A1%%=%u A2%%=%u A3%%=%u A4%%=%u timer=%u coeff=%u",
             out.enthalpyTemp, out.enthalpyHumidity, out.analog1Percent, out.analog2Percent,
             out.analog3Percent, out.analog4Percent, out.enthalpyTimer, out.enthalpyCoeff);
	return true;
}

bool ComfoAirDriver::getAnalogConfig(ComfoAnalogConfig& out) {
	std::vector<uint8_t> resp;
	uint16_t rc;
	if (!sendCommand(0x009D, {}, rc, resp)) return false;
	if (resp.size() < 19) return false;
	out.presentFlags  = resp[0];  // Byte[1]
	out.controlFlags  = resp[1];  // Byte[2]
	out.polarityFlags = resp[2];  // Byte[3]
	out.a1Min         = resp[3];  // Byte[4]
	out.a1Max         = resp[4];  // Byte[5]
	out.a1Setpoint    = resp[5];  // Byte[6]
	out.a2Min         = resp[6];  // Byte[7]
	out.a2Max         = resp[7];  // Byte[8]
	out.a2Setpoint    = resp[8];  // Byte[9]
	out.a3Min         = resp[9];  // Byte[10]
	out.a3Max         = resp[10]; // Byte[11]
	out.a3Setpoint    = resp[11]; // Byte[12]
	out.a4Min         = resp[12]; // Byte[13]
	out.a4Max         = resp[13]; // Byte[14]
	out.a4Setpoint    = resp[14]; // Byte[15]
	out.rfMin         = resp[15]; // Byte[16]
	out.rfMax         = resp[16]; // Byte[17]
	out.rfSetpoint    = resp[17]; // Byte[18]
	out.priority      = resp[18]; // Byte[19]
	AWM_LOGI("AnalogCfg: pres=0x%02X ctrl=0x%02X pol=0x%02X A1 %u-%u sp=%u A2 %u-%u sp=%u "
             "A3 %u-%u sp=%u A4 %u-%u sp=%u RF %u-%u sp=%u prio=%u",
             out.presentFlags, out.controlFlags, out.polarityFlags,
             out.a1Min, out.a1Max, out.a1Setpoint, out.a2Min, out.a2Max, out.a2Setpoint,
             out.a3Min, out.a3Max, out.a3Setpoint, out.a4Min, out.a4Max, out.a4Setpoint,
             out.rfMin, out.rfMax, out.rfSetpoint, out.priority);
	return true;
}

bool ComfoAirDriver::getDelayTimes(ComfoDelayTimes& out) {
	std::vector<uint8_t> resp;
	uint16_t rc;
	if (!sendCommand(0x00C9, {}, rc, resp)) return false;
	if (resp.size() < 8) return false;
	out.bathOnDelay   = resp[0];
	out.bathOffDelay  = resp[1];
	out.l1OffDelay    = resp[2];
	out.boostDuration = resp[3];
	out.filterWeeks   = resp[4];
	out.rfHighShort   = resp[5];
	out.rfHighLong    = resp[6];
	out.hoodOffDelay  = resp[7];
	AWM_LOGI("Delays: bathOn=%u bathOff=%u l1Off=%u boost=%u filterW=%u rfShort=%u rfLong=%u hoodOff=%u",
             out.bathOnDelay, out.bathOffDelay, out.l1OffDelay, out.boostDuration,
             out.filterWeeks, out.rfHighShort, out.rfHighLong, out.hoodOffDelay);
	return true;
}

bool ComfoAirDriver::getVentilationLevel(ComfoVentilationLevel& out) {
	std::vector<uint8_t> resp;
	uint16_t rc;
	if (!sendCommand(0x00CD, {}, rc, resp)) return false;
	if (resp.size() < 12) return false;
	out.exhaustAbsent   = resp[0];
	out.exhaustLow      = resp[1];
	out.exhaustMedium   = resp[2];
	out.supplyAbsent    = resp[3];
	out.supplyLow       = resp[4];
	out.supplyMedium    = resp[5];
	out.exhaustCurrent  = resp[6];
	out.supplyCurrent   = resp[7];
	out.currentLevel    = resp[8];
	out.supplyFanActive = resp[9];
	out.exhaustHigh     = resp[10];
	out.supplyHigh      = resp[11];
	AWM_LOGI("Vent: exh abs/low/mid/high=%u/%u/%u/%u sup abs/low/mid/high=%u/%u/%u/%u curr=%u fanActive=%u",
             out.exhaustAbsent, out.exhaustLow, out.exhaustMedium, out.exhaustHigh,
             out.supplyAbsent, out.supplyLow, out.supplyMedium, out.supplyHigh,
             out.currentLevel, out.supplyFanActive);
	return true;
}

bool ComfoAirDriver::getTempsD1(ComfoTempsD1& out) {
	std::vector<uint8_t> resp;
	uint16_t rc;
	if (!sendCommand(0x00D1, {}, rc, resp)) return false;
	if (resp.size() < 9) return false;
	out.comfortTemp    = static_cast<int8_t>(resp[0]);
	out.t1Outside      = static_cast<int8_t>(resp[1]);
	out.t2Supply       = static_cast<int8_t>(resp[2]);
	out.t3Exhaust      = static_cast<int8_t>(resp[3]);
	out.t4Outlet       = static_cast<int8_t>(resp[4]);
	out.sensorPresent  = resp[5];
	out.tEwt           = static_cast<int8_t>(resp[6]);
	out.tPostHeater    = static_cast<int8_t>(resp[7]);
	out.tHood          = static_cast<int8_t>(resp[8]);
	AWM_LOGI("TempsD1: comfort=%d T1=%d T2=%d T3=%d T4=%d EWT=%d postHeat=%d hood=%d sensors=0x%02X",
             out.comfortTemp, out.t1Outside, out.t2Supply, out.t3Exhaust, out.t4Outlet,
             out.tEwt, out.tPostHeater, out.tHood, out.sensorPresent);
	return true;
}

bool ComfoAirDriver:: getAnlagenconf(ComfoAnlagenconf& out) {
	std::vector<uint8_t> resp;
	uint16_t rc = 0;
	if (!sendCommand(0x00D5, {}, rc, resp)) return false;
	if (rc != 0x00D6 || resp.size() < 11) return false;

	out.preheaterPresent = resp[0];
	out.bypassPresent    = resp[1];
	out.type             = resp[2];
	out.size             = resp[3];
	out.options          = resp[4];
	out.reserved6        = resp[5];
	out.activeStatus1    = resp[6];
	out.activeStatus2    = resp[7];
	out.activeStatus3    = resp[8];
	out.enthalpyPresent  = resp[9];
	out.ewtPresent       = resp[10];
	out.raw              = resp;
	return true;
}

bool ComfoAirDriver::getErrorsD9(ComfoErrorD9& out) {
	std::vector<uint8_t> resp;
	uint16_t rc;
	if (!sendCommand(0x00D9, {}, rc, resp)) return false;
	out.raw = resp;
	AWM_LOGI("ErrorsD9 len=%u (roh)", (unsigned)out.raw.size());
	return true;
}

static uint32_t read24(const std::vector<uint8_t>& v, size_t idx) {
	// idx: Start-Index auf HighByte (Byte[n])
	if (v.size() < idx + 3) return 0;
	// Protokoll: Byte[3] = LowByte, also Big-Endian-ähnlich -> wir nehmen (hi<<16 | mid<<8 | lo)
	uint32_t hi  = v[idx];
	uint32_t mid = v[idx + 1];
	uint32_t lo  = v[idx + 2];
	return (hi << 16) | (mid << 8) | lo;
}

bool ComfoAirDriver::getRuntimeDD(ComfoRuntimeDD& out) {
	std::vector<uint8_t> resp;
	uint16_t rc;
	if (!sendCommand(0x00DD, {}, rc, resp)) return false;
	if (resp.size() < 20) return false;
	out.hoursAbsent     = read24(resp, 0);  // Byte[1-3]
	out.hoursLow        = read24(resp, 3);  // Byte[4-6]
	out.hoursMedium     = read24(resp, 6);  // Byte[7-9]
	out.hoursFrost      = (resp[9]  << 8) | resp[10];  // Byte[10-11]
	out.hoursPreheater  = (resp[11] << 8) | resp[12];  // Byte[12-13]
	out.hoursBypassOpen = (resp[13] << 8) | resp[14];  // Byte[14-15]
	out.hoursFilter     = (resp[15] << 8) | resp[16];  // Byte[16-17]
	out.hoursHigh       = read24(resp, 17);            // Byte[18-20]
	AWM_LOGI("RuntimeDD: abs=%lu low=%lu mid=%lu high=%lu frost=%u preheat=%u bypass=%u filter=%u",
             (unsigned long)out.hoursAbsent, (unsigned long)out.hoursLow,
             (unsigned long)out.hoursMedium, (unsigned long)out.hoursHigh,
             out.hoursFrost, out.hoursPreheater, out.hoursBypassOpen, out.hoursFilter);
	return true;
}

bool ComfoAirDriver::getBypassStatusDF(ComfoBypassStatusDF& out) {
	std::vector<uint8_t> resp;
	uint16_t rc = 0;
	if (!sendCommand(0x00DF, {}, rc, resp)) return false;

	// Erwartete Antwort auf 0x00DF ist 0x00E0
	if (rc != 0x00E0) {
		AWM_LOGW("BypassDF: unerwartetes Antwortkommando 0x%04X (erwartet 0x00E0)", rc);
		return false;
	}

	if (resp.size() < 7) return false;

	out.reserved1  = resp[0]; // Byte[1]
	out.reserved2  = resp[1]; // Byte[2]
	out.factor     = resp[2]; // Byte[3]
	out.stage      = resp[3]; // Byte[4]
	out.correction = resp[4]; // Byte[5]
	out.reserved3  = resp[5]; // Byte[6]
	out.summerMode = resp[6]; // Byte[7]

	AWM_LOGI("BypassDF: factor=%u stage=%u korr=%u summer=%u",
	         out.factor, out.stage, out.correction, out.summerMode);
	return true;
}

bool ComfoAirDriver::getPreheatStatusE1(ComfoPreheatStatusE1& out) {
	std::vector<uint8_t> resp;
	uint16_t rc;
	if (!sendCommand(0x00E1, {}, rc, resp)) return false;
	if (resp.size() < 6) return false;
	out.flapStatus  = resp[0];
	out.frostActive = resp[1];
	out.preheatOn   = resp[2];
	out.frostMins   = (resp[3] << 8) | resp[4];
	out.frostSafety = resp[5];
	AWM_LOGI("PreheatE1: flap=%u frost=%u preheat=%u mins=%u safety=%u",
             out.flapStatus, out.frostActive, out.preheatOn, out.frostMins, out.frostSafety);
	return true;
}

bool ComfoAirDriver::getRfStatusE5(ComfoRfStatusE5& out) {
	std::vector<uint8_t> resp;
	uint16_t rc;
	if (!sendCommand(0x00E5, {}, rc, resp)) return false;
	if (resp.size() < 7) return false;
	out.rfAddr4       = resp[0]; // Low
	out.rfAddr3       = resp[1];
	out.rfAddr2       = resp[2];
	out.rfAddr1       = resp[3]; // High (hier als 8 Bit, bei Bedarf 16 Bit bauen)
	out.rfId          = resp[4];
	out.modulePresent = resp[5];
	out.selfLearning  = resp[6];
	AWM_LOGI("RF E5: addr=%u.%u.%u.%u id=%u present=%u learn=%u",
             out.rfAddr1, out.rfAddr2, out.rfAddr3, out.rfAddr4, out.rfId, out.modulePresent, out.selfLearning);
	return true;
}

bool ComfoAirDriver::getPreheatHistoryE9(ComfoPreheatHistoryE9& out) {
	std::vector<uint8_t> resp;
	uint16_t rc;
	if (!sendCommand(0x00E9, {}, rc, resp)) return false;
	if (resp.size() < 8) return false;
	for (size_t i = 0; i < 8; ++i) out.values[i] = static_cast<int8_t>(resp[i]);
	AWM_LOGI("PreheatHistE9: %d,%d,%d,%d,%d,%d,%d,%d",
             out.values[0], out.values[1], out.values[2], out.values[3],
             out.values[4], out.values[5], out.values[6], out.values[7]);
	return true;
}

bool ComfoAirDriver::getEwtStatusEB(ComfoEwtStatusEB& out) {
	std::vector<uint8_t> resp;
	uint16_t rc;
	if (!sendCommand(0x00EB, {}, rc, resp)) return false;
	if (resp.size() < 7) return false;
	out.ewtLow      = static_cast<int8_t>(resp[0]);
	out.ewtHigh     = static_cast<int8_t>(resp[1]);
	out.ewtSpeedUp  = resp[2];
	out.hoodSpeedUp = resp[3];
	out.heaterPower = resp[4];
	out.heaterI     = resp[5];
	out.heaterTSet  = static_cast<int8_t>(resp[6]);
	AWM_LOGI("EWT EB: low=%d high=%d speedUp=%u hoodUp=%u power=%u I=%u tSet=%d",
             out.ewtLow, out.ewtHigh, out.ewtSpeedUp, out.hoodSpeedUp,
             out.heaterPower, out.heaterI, out.heaterTSet);
	return true;
}

// 8.1 – Dekoder

bool ComfoAirDriver::parseParams38(const std::vector<uint8_t>& p, ComfoParams38& out) const {
    if (p.size() < 5) return false;
    out.rtcDay            = p[0]; // Byte[1]
    out.rtcHour           = p[1]; // Byte[2]
    out.rtcMinute         = p[2]; // Byte[3]
    out.backlightTimeout  = p[3]; // Byte[4] (fest 30)
    out.backlightLevel    = p[4]; // Byte[5] (fest 100%)
    return true;
}

bool ComfoAirDriver::parseDisplay3C(const std::vector<uint8_t>& p, ComfoDisplayState& out) const {
    if (p.size() < 10) return false;
    out.b1  = p[0];  // Byte[1] Tage/Doppelpunkt
    out.b2  = p[1];  // Byte[2] 1A/1B/1C + AUTO/MANUAL/FILTER/I/E
    out.b3  = p[2];  // Byte[3] 2A..2G + FAN
    out.b4  = p[3];  // Byte[4] 3A..3G + Küchenhaube
    out.b5  = p[4];  // Byte[5] 4A..4G + Vorheizung
    out.b6  = p[5];  // Byte[6] 5A..5G + Frost
    out.b7  = p[6];  // Byte[7] 6A..6G + EWT
    out.b8  = p[7];  // Byte[8] 7A..7G + Nachheizung
    out.b9  = p[8];  // Byte[9] 8A..8G + Punkt
    out.b10 = p[9];  // Byte[10] Grad/Bypass/X1/X2/X3/Haus/Zuluft/Abluft
    return true;
}

// 0x0099 – Ventilationsstufe setzen
bool ComfoAirDriver::setLevel(uint8_t level) {
    std::vector<uint8_t> payload{ level };
    uint16_t respCmd = 0;
    std::vector<uint8_t> resp;
    // Antwort ist 0x009A mit optionalem Payload (kann leer sein)
    return sendCommand(0x0099, payload, respCmd, resp);
}

// 0x00D3 – Komforttemperatur in °C setzen (Hilfsfunktion)
bool ComfoAirDriver::setComfortTemperatureC(int8_t physTempC) {
    int16_t raw = (static_cast<int16_t>(physTempC) + 20) * 2;
    if (raw < 0) raw = 0;
    if (raw > 255) raw = 255;

    bool ok = setComfortTemperatureRaw(static_cast<uint8_t>(raw));
    if (ok) {
        _comfortTempCmdC = physTempC;
        _comfortTempCmdValid = true;
    }
    return ok;
}

// 0x00D3 – Komforttemperatur Raw setzen
bool ComfoAirDriver::setComfortTemperatureRaw(uint8_t tempRaw) {
    if (!_ready) {
        AWM_LOGE("RS232 nicht bereit – ist die serielle Verbindung gesteckt?");
        return false;
    }

    // 1) Standardweg: ACK + Antwortframe (0x00D4)
    {
        uint16_t rc = 0;
        std::vector<uint8_t> resp;
        if (sendCommand(0x00D3, { tempRaw }, rc, resp)) {
            return true;
        }
    }

    // 2) Fallback: nur ACK (einige Geräte reagieren so)
    while (_serial->available()) _serial->read();
    writeFrame(0x00D3, { tempRaw });

    bool ackOk = readAck(150);
    if (!ackOk) {
        AWM_LOGE("Kein ACK für cmd=0x00D3");
        return false;
    }

    AWM_LOGW("0x00D3: nur ACK erhalten (ohne Response-Frame), als Erfolg gewertet");
    return true;
}

bool ComfoAirDriver::pollAll(uint32_t minIntervalMs, bool force) {
    serviceTestModeAutoStop();
    uint32_t now = millis();
    if (!force && now - _lastPoll < minIntervalMs) return _stateOk;
    _lastPoll = now;

    // Primäre Reads
    _fanValid        = getFanStatus(_fan);
    _tempsValid      = getTemperatureStatus(_temps);
    _ainValid        = getAnalogInputs(_ain);
    _ventValid       = getVentilationLevel(_vent);
    bool inputsOk    = getInputs(_inputs);
    bool sens97Ok    = getSensorData97(_sens97);
    _anlagenconfValid= getAnlagenconf(_anlagenconf);
    bool ewtOk       = getEwtStatusEB(_ewt);
    _flapValid       = getFlapStatus(_flap);
    _bypassValid     = getBypassStatusDF(_bypass);
    _preheatValid    = getPreheatStatusE1(_preheat);
    bool runtimeOk   = getRuntimeDD(_runtime);
    bool errorsOk    = getErrorsD9(_errors);

    _analogCfgValid  = getAnalogConfig(_analogCfg);
    _delayValid      = getDelayTimes(_delay);
    _keyStatusValid  = getKeyStatus(_keyStatus);

    // Optional: TempsD1
    static bool tempsD1Supported = true;
    static bool tempsD1Warned = false;
    if (tempsD1Supported) {
        _tempsD1Valid = getTempsD1(_tempsD1);
        if (!_tempsD1Valid) {
            tempsD1Supported = false;
            if (!tempsD1Warned) {
                AWM_LOGW("TempsD1 nicht lesbar (kein ACK) – deaktiviert");
                tempsD1Warned = true;
            }
        }
    } else {
        _tempsD1Valid = false;
    }

    // Optional: RS232 Mode
    static bool rsModeSupported = true;
    static bool rsModeWarned = false;
    if (rsModeSupported) {
        _rsModeValid = getRs232Mode(_rsMode);
        if (!_rsModeValid) {
            rsModeSupported = false;
            if (!rsModeWarned) {
                AWM_LOGW("RS232 Mode nicht lesbar (kein ACK) – deaktiviert");
                rsModeWarned = true;
            }
        }
    } else {
        _rsModeValid = false;
    }

    // Global nur für Diagnose/Kompatibilität
    bool ok = true;
    ok &= _fanValid && _tempsValid && _ainValid && _ventValid;
    ok &= inputsOk && sens97Ok && _anlagenconfValid && ewtOk;
    ok &= _flapValid && _bypassValid && _preheatValid;
    ok &= runtimeOk && errorsOk;
    ok &= _analogCfgValid && _delayValid && _keyStatusValid;

    _stateOk = ok;
    if (!ok) AWM_LOGW("Poll teilweise/komplett fehlgeschlagen (Best-Effort aktiv)");
    return ok;
}

bool ComfoAirDriver::mqttInit(WiFiClient& net, const ComfoMqttConfig& cfg) {
    _net = &net;
    applyMqttCfgOwned(cfg);
    _mqtt.setClient(net);

    // NEU: große JSON-Statuspayloads erlauben (manual/analogConfig)
    _mqtt.setBufferSize(1024);

    _mqtt.setServer(_mqttCfg.host, _mqttCfg.port);
    _cbInstance = this;
    _mqtt.setCallback(&ComfoAirDriver::mqttThunk);
    _mqttEnabled = true;
    return true;
}

void ComfoAirDriver::mqttSetEnabled(bool enabled) {
    _mqttEnabled = enabled;
    if (!enabled && _mqtt.connected()) {
        _mqtt.disconnect();
    }
}

bool ComfoAirDriver::mqttIsEnabled() const {
    return _mqttEnabled;
}

void ComfoAirDriver::mqttThunk(char* topic, uint8_t* payload, unsigned int length) {
    if (_cbInstance) _cbInstance->onMqttMessage(topic, payload, length);
}

void ComfoAirDriver::onMqttMessage(char* topic, uint8_t* payload, unsigned int length) {
    String t(topic ? topic : "");
    t.trim();
    while (t.endsWith("/")) t.remove(t.length() - 1);

    String msg; msg.reserve(length + 1);
    for (unsigned i = 0; i < length; i++) msg += (char)payload[i];
    msg.trim();

    const String b = String(_mqttCfg.topicPrefix);

    AWM_LOGI("MQTT RX topic='%s' payload='%s'", t.c_str(), msg.c_str());

    auto publishCmdResult = [&](const String& cmd, bool ok, const String& err = "") {
        StaticJsonDocument<192> d;
        d["cmd"] = cmd;
        d["ok"] = ok;
        if (!ok && err.length()) d["error"] = err;
        String out; serializeJson(d, out);
        _mqtt.publish((b + "/status/command/bereich").c_str(), out.c_str(), true);
    };

    auto parseJsonObj = [&](auto& d) -> bool {
        if (deserializeJson(d, msg)) return false;
        return d.template is<JsonObject>();
    };

    // -------------------------------------------------------------------------
    // level (wie HTML-API: 0..4)
    // -------------------------------------------------------------------------
    if (t == b + "/cmd/level/bereich") {
        int level = -1;

        // akzeptiert {"level":2}, "level":2, 2
        StaticJsonDocument<128> d;
        if (!deserializeJson(d, msg)) {
            if (d.is<JsonObject>()) level = d["level"] | -1;
            else if (d.is<int>())   level = d.as<int>();
        }
        if (level < 0 && !tryParseIntLoose(msg, "level", level)) {
            level = msg.toInt();
        }

        if (level < 0 || level > 4) {
            publishCmdResult("level", false, "level must be 0..4");
            return;
        }

        // Projektrichtlinie: zuerst persistieren, dann RS232 schreiben
        if (!persistLevelToConfig(level)) {
            publishCmdResult("level", false, "persist failed");
            return;
        }

        bool ok = setLevel((uint8_t)level);
        if (!ok) { delay(50); ok = setLevel((uint8_t)level); }

        if (ok) {
            // Sofort lokalen Zustand setzen, nicht direkt durch pollAll überschreiben
            _vent.currentLevel = static_cast<uint8_t>(level);
            _ventValid = true;
            mqttPublishState();
        }

        publishCmdResult("level", ok, ok ? "" : "setLevel failed");
        return;
    }

    // -------------------------------------------------------------------------
    // comfort (wie HTML-API: clamp -40..100)
    // -------------------------------------------------------------------------
    if (t == b + "/cmd/comfort/bereich" || t == b + "/cmd/comfort" || t == b + "/cmd/comfort/json") {
        int tempC = INT_MIN;

        StaticJsonDocument<128> d;
        if (!deserializeJson(d, msg)) {
            if (d.is<JsonObject>()) {
                tempC = d["tempC"] | d["comfortC"] | d["value"] | INT_MIN;
            } else if (d.is<int>()) {
                tempC = d.as<int>();
            }
        }

        if (tempC == INT_MIN) {
            if (!tryParseIntLoose(msg, "tempC", tempC) &&
                !tryParseIntLoose(msg, "comfortC", tempC) &&
                !tryParseIntLoose(msg, "value", tempC)) {
                tempC = msg.toInt();
            }
        }

        // HTML-Verhalten: clamp statt reject
        if (tempC < -40) tempC = -40;
        if (tempC > 100) tempC = 100;

        if (!persistComfortToConfig(tempC)) {
            publishCmdResult("comfort", false, "persist failed");
            return;
        }

        bool ok = setComfortTemperatureC((int8_t)tempC);
        if (ok) {
            pollAll(0, true);
            mqttPublishState();
        }
        publishCmdResult("comfort", ok, ok ? "" : "setComfort failed");
        return;
    }

    // -------------------------------------------------------------------------
    // rs232Mode (0..4)
    // -------------------------------------------------------------------------
    if (t == b + "/cmd/rs232Mode/bereich") {
        StaticJsonDocument<128> d;
        if (!parseJsonObj(d)) { publishCmdResult("rs232Mode", false, "invalid json object"); return; }

        int mode = d["mode"] | -1;
        std::vector<uint8_t> resp;
        bool ok = (mode >= 0 && mode <= 4) ? setRs232Mode((uint8_t)mode, resp) : false;

        publishCmdResult("rs232Mode", ok, ok ? "" : "mode must be 0..4");
        return;
    }

    // -------------------------------------------------------------------------
    // manual start/end
    // -------------------------------------------------------------------------
    if (t == b + "/cmd/manual/bereich") {
        StaticJsonDocument<192> d;
        if (!parseJsonObj(d)) { publishCmdResult("manual", false, "invalid json object"); return; }

        bool ok = false;
        if (d.containsKey("active")) {
            bool wanted = d["active"];
            ok = wanted ? startManualMode() : endManualMode();
        } else {
            String action = d["action"] | "";
            action.toLowerCase();
            if (action == "start") ok = startManualMode();
            else if (action == "end" || action == "stop") ok = endManualMode();
            else { publishCmdResult("manual", false, "use active:true/false or action:start/end"); return; }
        }

        if (ok) mqttPublishState();
        publishCmdResult("manual", ok);
        return;
    }

    // -------------------------------------------------------------------------
    // manual flaps (bypass/preheater: 0/1/3, delay 0..600)
    // -------------------------------------------------------------------------
    if (t == b + "/cmd/manual/flaps/bereich") {
        StaticJsonDocument<256> d;
        if (!parseJsonObj(d)) { publishCmdResult("manual/flaps", false, "invalid json object"); return; }

        if (!d.containsKey("bypass") || !d.containsKey("preheater")) {
            publishCmdResult("manual/flaps", false, "missing bypass/preheater");
            return;
        }

        int by = d["bypass"] | -1;
        int ph = d["preheater"] | -1;
        int ds = d["delaySeconds"] | 0;

        auto validFlap = [](int v) { return v == 0 || v == 1 || v == 3; };
        if (!validFlap(by) || !validFlap(ph)) {
            publishCmdResult("manual/flaps", false, "bypass/preheater must be 0,1,3");
            return;
        }
        if (ds < 0 || ds > 600) {
            publishCmdResult("manual/flaps", false, "delaySeconds must be 0..600");
            return;
        }

        bool ok = setManualFlapsWithAutoStop((uint8_t)by, (uint8_t)ph, (uint16_t)ds);
        if (ok) {
            persistManualDelayToConfig(ds);
            mqttPublishState();
        }
        publishCmdResult("manual/flaps", ok, ok ? "" : "setManualFlapsWithAutoStop failed");
        return;
    }

    // -------------------------------------------------------------------------
    // manual analog (0..100)
    // -------------------------------------------------------------------------
    if (t == b + "/cmd/manual/analog/bereich") {
        StaticJsonDocument<256> d;
        if (!parseJsonObj(d)) { publishCmdResult("manual/analog", false, "invalid json object"); return; }

        if (!d.containsKey("supply") || !d.containsKey("exhaust") || !d.containsKey("postheater")) {
            publishCmdResult("manual/analog", false, "missing supply/exhaust/postheater");
            return;
        }

        int s = d["supply"] | -1;
        int e = d["exhaust"] | -1;
        int p = d["postheater"] | -1;

        auto inRange100 = [](int v) { return v >= 0 && v <= 100; };
        if (!inRange100(s) || !inRange100(e) || !inRange100(p)) {
            publishCmdResult("manual/analog", false, "supply/exhaust/postheater must be 0..100");
            return;
        }

        bool ok = setManualAnalog((uint8_t)s, (uint8_t)e, (uint8_t)p);
        if (ok) {
            persistManualAnalogToConfig(s, e, p);
            pollAll(0, true);
            mqttPublishState();
        }
        publishCmdResult("manual/analog", ok, ok ? "" : "setManualAnalog failed");
        return;
    }

    // -------------------------------------------------------------------------
    // status (Pflichtfelder + Range wie HTML-API)
    // -------------------------------------------------------------------------
    if (t == b + "/cmd/status/bereich") {
        StaticJsonDocument<384> d;
        if (!parseJsonObj(d)) { publishCmdResult("status", false, "invalid json object"); return; }

        const char* required[] = { "preheater", "bypass", "type", "size", "options", "enthalpy", "ewt" };
        for (auto key : required) {
            if (!d.containsKey(key)) {
                publishCmdResult("status", false, String("missing arg: ") + key);
                return;
            }
        }

        int preheater = d["preheater"] | -1;
        int bypass    = d["bypass"] | -1;
        int type      = d["type"] | -1;
        int size      = d["size"] | -1;
        int options   = d["options"] | -1;
        int enthalpy  = d["enthalpy"] | -1;
        int ewt       = d["ewt"] | -1;

        if ((preheater < 0 || preheater > 1) ||
            (bypass < 0 || bypass > 1) ||
            (type < 1 || type > 2) ||
            (size < 1 || size > 2) ||
            (options < 0 || options > 255) ||
            (enthalpy < 0 || enthalpy > 2) ||
            (ewt < 0 || ewt > 2)) {
            publishCmdResult("status", false, "invalid value range");
            return;
        }

        bool ok = setStatus(
            (uint8_t)preheater, (uint8_t)bypass, (uint8_t)type, (uint8_t)size,
            (uint8_t)options, (uint8_t)enthalpy, (uint8_t)ewt);

        if (ok) {
            persistStatusToConfig(preheater, bypass, type, size, options, enthalpy, ewt);
            pollAll(0, true);
            mqttPublishState();
        }
        publishCmdResult("status", ok, ok ? "" : "setStatus failed");
        return;
    }

    // -------------------------------------------------------------------------
    // vent (Pflichtfelder + 0..100 wie HTML-API)
    // -------------------------------------------------------------------------
    if (t == b + "/cmd/vent/bereich") {
        StaticJsonDocument<384> d;
        if (!parseJsonObj(d)) { publishCmdResult("vent", false, "invalid json object"); return; }

        const char* required[] = {
            "exhaustAbsent","exhaustLow","exhaustMedium",
            "supplyAbsent","supplyLow","supplyMedium",
            "exhaustHigh","supplyHigh"
        };
        for (auto key : required) {
            if (!d.containsKey(key)) {
                publishCmdResult("vent", false, String("missing arg: ") + key);
                return;
            }
        }

        int ea = d["exhaustAbsent"] | -1;
        int el = d["exhaustLow"] | -1;
        int em = d["exhaustMedium"] | -1;
        int sa = d["supplyAbsent"] | -1;
        int sl = d["supplyLow"] | -1;
        int sm = d["supplyMedium"] | -1;
        int eh = d["exhaustHigh"] | -1;
        int sh = d["supplyHigh"] | -1;

        auto inRange = [](int v) { return v >= 0 && v <= 100; };
        if (!inRange(ea) || !inRange(el) || !inRange(em) ||
            !inRange(sa) || !inRange(sl) || !inRange(sm) ||
            !inRange(eh) || !inRange(sh)) {
            publishCmdResult("vent", false, "values must be 0..100");
            return;
        }

        bool ok = setVentilationLevels(
            (uint8_t)ea, (uint8_t)el, (uint8_t)em, (uint8_t)sa,
            (uint8_t)sl, (uint8_t)sm, (uint8_t)eh, (uint8_t)sh);

        if (ok) {
            persistVentToConfig(ea, el, em, sa, sl, sm, eh, sh);
            pollAll(0, true);
            mqttPublishState();
        }
        publishCmdResult("vent", ok, ok ? "" : "setVentilationLevels failed");
        return;
    }

    // -------------------------------------------------------------------------
    // levelStep (zusätzlicher MQTT-Komfortbefehl)
    // -------------------------------------------------------------------------
    if (t == b + "/cmd/levelStep/bereich") {
        StaticJsonDocument<128> d;
        if (!parseJsonObj(d)) { publishCmdResult("levelStep", false, "invalid json object"); return; }

        String action = d["action"] | "";
        int delta = d["delta"] | 0;

        if (delta == 0) {
            action.toLowerCase();
            if (action == "up") delta = 1;
            else if (action == "down") delta = -1;
            else { publishCmdResult("levelStep", false, "use action: up|down or delta"); return; }
        }

        int current = -1;
        if (_ventValid) current = _vent.currentLevel;

        if (current < 0 || current > 4) {
            ComfoVentilationLevel v{};
            if (getVentilationLevel(v)) {
                _vent = v;
                _ventValid = true;
                current = v.currentLevel;
            }
        }

        if (current < 0 || current > 4) {
            publishCmdResult("levelStep", false, "current level unavailable");
            return;
        }

        int target = current + delta;
        if (target < 0) target = 0;
        if (target > 4) target = 4;

        // Projektrichtlinie: zuerst persistieren, dann RS232 schreiben
        if (!persistLevelToConfig(target)) {
            publishCmdResult("levelStep", false, "persist failed");
            return;
        }

        bool ok = setLevel((uint8_t)target);
        if (ok) {
            // Sofort lokalen Zustand setzen, nicht direkt durch pollAll überschreiben
            _vent.currentLevel = (uint8_t)target;
            _ventValid = true;
            mqttPublishState();
        }

        publishCmdResult("levelStep", ok, ok ? "" : "setLevel failed");
        return;
    }

    AWM_LOGW("MQTT RX unbekanntes Topic: '%s'", t.c_str());
    publishCmdResult("unknown", false, String("unknown topic: ") + t);
    return;
}

bool ComfoAirDriver::mqttEnsureConnected() {
    if (!_mqttEnabled || !_net) return false;
    if (WiFi.status() != WL_CONNECTED) return false;
    if (_mqtt.connected()) return true;
    if (millis() - _lastMqttReconnect < 3000) return false;
    _lastMqttReconnect = millis();

    String b = _mqttCfg.topicPrefix ? _mqttCfg.topicPrefix : "";
    b.trim();
    while (b.startsWith("/")) b.remove(0, 1);
    while (b.endsWith("/")) b.remove(b.length() - 1, 1);
    if (b.isEmpty()) b = "comfoair";

    const String onlineTopic = b + "/status/system/json";

    const bool ok = _mqtt.connect(
        _mqttCfg.clientId,
        _mqttCfg.user,
        _mqttCfg.pass,
        onlineTopic.c_str(),
        1,
        true,
        "{\"online\":0}"
    );

    if (!ok) {
        AWM_LOGW("MQTT connect rc=%d", _mqtt.state());
        return false;
    }

    auto sub = [&](const String& topic) {
        const bool s = _mqtt.subscribe(topic.c_str());
        AWM_LOGI("MQTT subscribe %s => %s", topic.c_str(), s ? "ok" : "fail");
        return s;
    };

    // Command-Topics
    constexpr const char* kCmdTopicSuffixes[] = {
        "/cmd/level/bereich",
        "/cmd/comfort/bereich",
        "/cmd/comfort",
        "/cmd/comfort/json",
        "/cmd/rs232Mode/bereich",
        "/cmd/manual/bereich",
        "/cmd/manual/flaps/bereich",
        "/cmd/manual/analog/bereich",
        "/cmd/status/bereich",
        "/cmd/vent/bereich",
        "/cmd/levelStep/bereich"
    };
    static_assert(sizeof(kCmdTopicSuffixes) / sizeof(kCmdTopicSuffixes[0]) > 0, "No MQTT command topics configured");

    auto isKnownCmdTopic = [&](const String& topic) -> bool {
        for (auto s : kCmdTopicSuffixes) {
            if (topic == b + s) return true;
        }
        return false;
    };

    for (size_t i = 0; i < sizeof(kCmdTopicSuffixes) / sizeof(kCmdTopicSuffixes[0]); ++i) {
        sub(b + kCmdTopicSuffixes[i]);
    }

    _mqttLastByTopic.clear();
    _mqtt.publish(onlineTopic.c_str(), "{\"online\":1}", true);
    _mqtt.publish((b + "/status/command/bereich").c_str(), "{\"cmd\":\"\",\"ok\":true}", true);

    pollAll(0, true);
    mqttPublishState();

    AWM_LOGI("MQTT verbunden");
    return true;
}

bool ComfoAirDriver::setStatus(uint8_t preheaterPresent,
    uint8_t bypassPresent,
    uint8_t type,
    uint8_t size,
    uint8_t options,
    uint8_t enthalpyPresent,
    uint8_t ewtPresent) {
    if (!_ready) return false;
    while (_serial->available()) _serial->read();

    std::vector<uint8_t> payload{
        preheaterPresent, bypassPresent, type, size, options, 0x00, enthalpyPresent, ewtPresent
    };

    writeFrame(0x00D7, payload);
    return readAck(150);
}

bool ComfoAirDriver::setVentilationLevels(uint8_t exhaustAbsent,
    uint8_t exhaustLow,
    uint8_t exhaustMedium,
    uint8_t supplyAbsent,
    uint8_t supplyLow,
    uint8_t supplyMedium,
    uint8_t exhaustHigh,
    uint8_t supplyHigh) {
    if (!_ready) return false;
    while (_serial->available()) _serial->read();

    // Protokoll: 0x00CF mit 9 Byte Payload, letztes Byte reserviert = 0x00
    std::vector<uint8_t> payload{
        exhaustAbsent, exhaustLow, exhaustMedium,
        supplyAbsent, supplyLow, supplyMedium,
        exhaustHigh, supplyHigh,
        0x00
    };

    writeFrame(0x00CF, payload);
    return readAck(150);
}

bool ComfoAirDriver::startManualMode() {
    // 0x0001 – Manuellen Modus starten.
    // Bei Erfolg (Antwort 0x0002): Klappenverriegelungen zurücksetzen.
    uint16_t rc = 0;
    std::vector<uint8_t> resp;
    if (!sendCommand(0x0001, {}, rc, resp)) {
        _manualModeActive = false;
        return false;
    }
    _manualModeActive = (rc == 0x0002);
    if (_manualModeActive) {
        _testFlapsLatchBypass = 3;
        _testFlapsLatchPreheater = 3;
    }
    return _manualModeActive;
}

bool ComfoAirDriver::endManualMode() {
    // 0x0019 – Manuellen Modus beenden.
    // Bei Erfolg (Antwort 0x001A): Auto-Stop und Verriegelungen zurücksetzen.
    uint16_t rc = 0;
    std::vector<uint8_t> resp;
    if (!sendCommand(0x0019, {}, rc, resp)) return false;
    if (rc == 0x001A) {
        _testFlapsAutoStopArmed = false;
        _testFlapsLatchBypass = 3;
        _testFlapsLatchPreheater = 3;
        _manualModeActive = false;
        return true;
    }
    return false;
}

bool ComfoAirDriver::setManualOutputs(uint8_t relaisBits, uint8_t feedbackBits) {
    uint16_t rc; std::vector<uint8_t> resp;
    return sendCommand(0x0005, { relaisBits, feedbackBits }, rc, resp);
}

bool ComfoAirDriver::setManualAnalog(uint8_t supplyPercent, uint8_t exhaustPercent, uint8_t postheaterPercent) {
    // 0x0007 – Analogausgänge im manuellen Modus setzen (0–100%).
    // // Startet manuellen Modus automatisch falls noch nicht aktiv.
    // Fallback: nur ACK akzeptieren falls kein Antwort-Frame kommt.

    auto inRange = [](uint8_t v) { return v <= 100; };
    if (!inRange(supplyPercent) || !inRange(exhaustPercent) || !inRange(postheaterPercent)) return false;

    // Sicherstellen, dass manueller Modus aktiv ist
    if (!_manualModeActive) {
        if (!startManualMode()) return false;
    }

    uint16_t rc = 0;
    std::vector<uint8_t> resp;
    if (sendCommand(0x0007, { supplyPercent, exhaustPercent, postheaterPercent }, rc, resp)) {
        _manualAnalogSupplyCmd = supplyPercent;
        _manualAnalogExhaustCmd = exhaustPercent;
        _manualAnalogPostheaterCmd = postheaterPercent;
        return true;
    }

    while (_serial->available()) _serial->read();
    writeFrame(0x0007, { supplyPercent, exhaustPercent, postheaterPercent });
    if (!readAck(150)) return false;

    _manualAnalogSupplyCmd = supplyPercent;
    _manualAnalogExhaustCmd = exhaustPercent;
    _manualAnalogPostheaterCmd = postheaterPercent;
    return true;
}

bool ComfoAirDriver::setManualFlaps(uint8_t bypass, uint8_t preheater) {
    // 0x0009 – Klappensteuerung im manuellen Modus (0=Zu, 1=Offen, 3=Stop).
    // Aktualisiert Verriegelungs- und Anzeigestatus.
    // Fallback: nur ACK akzeptieren falls kein Antwort-Frame kommt.

    auto valid = [](uint8_t v) { return v == 0 || v == 1 || v == 3; };
    if (!valid(bypass) || !valid(preheater)) return false;

    uint16_t rc = 0;
    std::vector<uint8_t> resp;
    if (!sendCommand(0x0009, { bypass, preheater }, rc, resp)) {
        while (_serial->available()) _serial->read();
        writeFrame(0x0009, { bypass, preheater });
        if (!readAck(150)) return false;
    }

    _testFlapsLatchBypass = (bypass == 3) ? 3 : bypass;
    _testFlapsLatchPreheater = (preheater == 3) ? 3 : preheater;

    // Neu: persistente Anzeige nur bei explizitem "zu/offen".
    // "3" (stop/unverriegelt) setzt NICHT zurück.
    if (bypass == 0 || bypass == 1) _manualRequestedBypass = bypass;
    if (preheater == 0 || preheater == 1) _manualRequestedPreheater = preheater;
    return true;
}

bool ComfoAirDriver::setManualFlapsWithAutoStop(uint8_t bypass, uint8_t preheater, uint16_t delaySeconds) {
    // Ruft setManualFlaps() auf und armt danach den Auto-Stop-Timer.
    // delaySeconds == 0: kein Auto-Stop (Projektrichtlinie: manueller Stop).

    if (!setManualFlaps(bypass, preheater)) return false;

    _manualFlapsLastDelaySec = delaySeconds;

    _testFlapsAutoStopArmed = false;
    if (delaySeconds == 0) return true; // laut Richtlinie: 0 => kein Auto-Stop

    const bool bypassNeedsStop = (bypass == 0 || bypass == 1);
    const bool preNeedsStop = ( preheater == 0 || preheater == 1);
    if (!bypassNeedsStop && !preNeedsStop) return true;

    _testFlapsAutoStopBypass = bypassNeedsStop ? 3 : bypass;
    _testFlapsAutoStopPreheater = preNeedsStop ? 3 : preheater;
    _testFlapsAutoStopDueMs = millis() + static_cast<uint32_t>(delaySeconds) * 1000UL;
    _testFlapsAutoStopArmed = true;
    return true;
}

void ComfoAirDriver::serviceManualModeAutoStop() {
    // Prüft ob der Auto-Stop-Timer abgelaufen ist und setzt Klappen auf Stop.
    // Bei Fehler: Retry nach 500ms.

    if (!_testFlapsAutoStopArmed) return;
    if ((int32_t)(millis() - _testFlapsAutoStopDueMs) < 0) return;

    if (setManualFlaps(_testFlapsAutoStopBypass, _testFlapsAutoStopPreheater)) {
        _testFlapsAutoStopArmed = false;
    }
    else {
        _testFlapsAutoStopDueMs = millis() + 500;
    }
}

const char* ComfoAirDriver::flapReqToText(uint8_t v) {
    switch (v) {
    case 0: return "zu";
    case 1: return "offen";
    case 3: return "stop";
    default: return "stop";
    }
}

const char* ComfoAirDriver::getManualRequestedBypassText() const {
    return flapReqToText(_manualRequestedBypass);
}

const char* ComfoAirDriver::getManualRequestedPreheaterText() const {
    return flapReqToText(_manualRequestedPreheater);
}

bool ComfoAirDriver::mqttApplyConfig(const ComfoMqttConfig& cfg, bool enabled, bool publishNow) {
    applyMqttCfgOwned(cfg);
    _mqtt.setServer(_mqttCfg.host, _mqttCfg.port);
    _mqttEnabled = enabled;

    if (_mqtt.connected()) _mqtt.disconnect();
    _mqttLastByTopic.clear();
    _lastMqttReconnect = 0;

    if (!_mqttEnabled) return true;
    if (!mqttEnsureConnected()) return false;

    if (publishNow) {
        pollAll(0, true);
        mqttPublishState();
    }
    return true;
}

void ComfoAirDriver::applyMqttCfgOwned(const ComfoMqttConfig& cfg) {
    _mqttHostS = cfg.host ? cfg.host : "";
    _mqttUserS = cfg.user ? cfg.user : "";
    _mqttPassS = cfg.pass ? cfg.pass : "";
    _mqttClientIdS = cfg.clientId ? cfg.clientId : "";
    _mqttTopicPrefixS = normalizeTopicPrefix(cfg.topicPrefix ? cfg.topicPrefix : "comfoair");

    _mqttCfg.host = _mqttHostS.c_str();
    _mqttCfg.port = cfg.port;
    _mqttCfg.user = _mqttUserS.c_str();
    _mqttCfg.pass = _mqttPassS.c_str();
    _mqttCfg.clientId = _mqttClientIdS.c_str();
    _mqttCfg.topicPrefix = _mqttTopicPrefixS.c_str();
}

void ComfoAirDriver::setManualAutoStopDelaySecFromConfig(uint16_t sec) {
    if (sec > 600) sec = 600;
    _manualFlapsLastDelaySec = sec;
    if (_mqttEnabled && _mqtt.connected()) {
        mqttPublishState();
    }
}

void ComfoAirDriver::mqttLoop() {
    serviceManualModeAutoStop();

    if (!_mqttEnabled) return;
    if (WiFi.status() != WL_CONNECTED) return;
    if (!mqttEnsureConnected()) return;

    _mqtt.loop();
    mqttPublishState();
}

void ComfoAirDriver::mqttPublishState() {
    if (!_mqttEnabled || !_mqtt.connected()) return;

    const String b = String(_mqttCfg.topicPrefix ? _mqttCfg.topicPrefix : "comfoair");

    auto areaTopic = [&](const char* area) -> String {
        return b + "/status/" + String(area);
    };

    auto publishChanged = [&](const String& topic, const String& payload, bool retained = true) -> bool {
        for (auto& it : _mqttLastByTopic) {
            if (it.first == topic) {
                if (it.second == payload) return false;
                it.second = payload;
                _mqtt.publish(topic.c_str(), payload.c_str(), retained);
                return true;
            }
        }
        _mqttLastByTopic.push_back({ topic, payload });
        _mqtt.publish(topic.c_str(), payload.c_str(), retained);
        return true;
    };

    auto pubJsonChanged = [&](const String& topic, JsonDocument& doc) {
        String out;
        serializeJson(doc, out);
        publishChanged(topic, out, true);
    };

    auto decodeTempC = [](int8_t raw) -> float {
        return (static_cast<float>(raw) / 2.0f) - 20.0f;
    };

    // Meta
    {
        StaticJsonDocument<192> d;
        d["ok"] = _stateOk;
        d["manualModeActive"] = _manualModeActive;
        d["rs232Mode"] = _rsModeValid ? _rsMode : 0;
        if (_keyStatusValid) d["keyStatus"] = _keyStatus;
        pubJsonChanged(areaTopic("meta"), d);
    }

    // Fan
    if (_fanValid) {
        StaticJsonDocument<160> d;
        d["supplyPct"] = _fan.supplyPercent;
        d["exhaustPct"] = _fan.exhaustPercent;
        d["supplyRpm"] = _fan.supplyRpm;
        d["exhaustRpm"] = _fan.exhaustRpm;
        pubJsonChanged(areaTopic("fan"), d);
    }

    // Temps
    if (_tempsValid) {
        StaticJsonDocument<256> d;
        d["t1Raw"] = static_cast<int>(_temps.t1Outside);
        d["t2Raw"] = static_cast<int>(_temps.t2Supply);
        d["t3Raw"] = static_cast<int>(_temps.t3Exhaust);
        d["t4Raw"] = static_cast<int>(_temps.t4Outlet);
        d["t1C"] = decodeTempC(_temps.t1Outside);
        d["t2C"] = decodeTempC(_temps.t2Supply);
        d["t3C"] = decodeTempC(_temps.t3Exhaust);
        d["t4C"] = decodeTempC(_temps.t4Outlet);
        if (_tempsD1Valid) d["comfortC"] = static_cast<int>(decodeTempC(_tempsD1.comfortTemp));
        else if (_comfortTempCmdValid) d["comfortC"] = _comfortTempCmdC;
        pubJsonChanged(areaTopic("temps"), d);
    }

    // Vent
    if (_ventValid) {
        StaticJsonDocument<256> d;
        d["current"] = _vent.currentLevel;
        d["supplyCur"] = _vent.supplyCurrent;
        d["exhaustCur"] = _vent.exhaustCurrent;
        d["supplyHigh"] = _vent.supplyHigh;
        d["exhaustHigh"] = _vent.exhaustHigh;
        d["exhaustAbsent"] = _vent.exhaustAbsent;
        d["exhaustLow"] = _vent.exhaustLow;
        d["exhaustMedium"] = _vent.exhaustMedium;
        d["supplyAbsent"] = _vent.supplyAbsent;
        d["supplyLow"] = _vent.supplyLow;
        d["supplyMedium"] = _vent.supplyMedium;
        pubJsonChanged(areaTopic("vent"), d);
    }

    // Manual
    {
        StaticJsonDocument<256> d;
        d["active"] = _manualModeActive;
        d["autoStopArmed"] = _testFlapsAutoStopArmed;
        d["autoStopDelaySec"] = _manualFlapsLastDelaySec;
        d["requestedBypass"] = _manualRequestedBypass;
        d["requestedPreheater"] = _manualRequestedPreheater;
        d["requestedBypassText"] = getManualRequestedBypassText();
        d["requestedPreheaterText"] = getManualRequestedPreheaterText();
        pubJsonChanged(areaTopic("manual"), d);
        pubJsonChanged(areaTopic("manual/json"), d);
    }
}