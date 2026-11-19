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
    // updateConfigJson() liest den aktuellen Stand, ergänzt die neuen Werte
    // und schreibt nur bei tatsächlicher Änderung zurück ins NVS.
    static bool persistLevelToConfig(int level) {
        // Speichert den zuletzt gesetzten Vent-Level (0–4) in config.json.
        return updateConfigJson([&](JsonDocument& doc) {
            doc["setLevel"] = clampInt(level, 0, 4);
        });
    }

    static bool persistComfortToConfig(int tempC) {
        // Speichert die Komforttemperatur (°C) in config.json.
        return updateConfigJson([&](JsonDocument& doc) {
            doc["setComfortTempC"] = clampInt(tempC, -40, 100);
        });
    }

    static bool persistManualDelayToConfig(int delaySeconds) {
        // Speichert den Auto-Stop-Delay (0–600s) in config.json.
        return updateConfigJson([&](JsonDocument& doc) {
            doc["testAutoStopSec"] = clampInt(delaySeconds, 0, 600);
        });
    }

    static bool persistManualAnalogToConfig(int supply, int exhaust, int postheater) {
        // Speichert die letzten Analog-Sollwerte (0–100%) in config.json.
        return updateConfigJson([&](JsonDocument& doc) {
            doc["manualAnalogSupply"] = clampInt(supply, 0, 100);
            doc["manualAnalogExhaust"] = clampInt(exhaust, 0, 100);
            doc["manualAnalogPostheater"] = clampInt(postheater, 0, 100);
        });
    }

    static bool persistStatusToConfig(int preheater, int bypass, int type, int size, int options, int enthalpy, int ewt) {
        // Speichert die Anlagenkonfiguration (0x00D7-Parameter) in config.json.
        return updateConfigJson([&](JsonDocument& doc) {
            auto s = doc.createNestedObject("setStatus");
            s["preheater"] = clampInt(preheater, 0, 1);
            s["bypass"] = clampInt(bypass, 0, 1);
            s["type"] = clampInt(type, 1, 2);
            s["size"] = clampInt(size, 1, 2);
            s["options"] = clampInt(options, 0, 255);
            s["enthalpy"] = clampInt(enthalpy, 0, 2);
            s["ewt"] = clampInt(ewt, 0, 2);
        });
    }

    static bool persistVentToConfig(int ea, int el, int em, int sa, int sl, int sm, int eh, int sh) {
        // Speichert die Ventilationsstufenprozente in config.json.
        return updateConfigJson([&](JsonDocument& doc) {
            auto v = doc.createNestedObject("setVent");
            v["exhaustAbsent"] = clampInt(ea, 0, 100);
            v["exhaustLow"] = clampInt(el, 0, 100);
            v["exhaustMedium"] = clampInt(em, 0, 100);
            v["supplyAbsent"] = clampInt(sa, 0, 100);
            v["supplyLow"] = clampInt(sl, 0, 100);
            v["supplyMedium"] = clampInt(sm, 0, 100);
            v["exhaustHigh"] = clampInt(eh, 0, 100);
            v["supplyHigh"] = clampInt(sh, 0, 100);
        });
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
}

namespace {
// RAII-Sperre für eine RS232-Transaktion (rekursiv: verschachtelte sendCommand()-Aufrufe sind erlaubt)
struct IoLock {
    SemaphoreHandle_t m;
    bool ok;
    explicit IoLock(SemaphoreHandle_t mtx, uint32_t timeoutMs = 4000)
        : m(mtx), ok(mtx && xSemaphoreTakeRecursive(mtx, pdMS_TO_TICKS(timeoutMs)) == pdTRUE) {}
    ~IoLock() { if (ok) xSemaphoreGiveRecursive(m); }
    IoLock(const IoLock&) = delete;
    IoLock& operator=(const IoLock&) = delete;
};
}

ComfoAirDriver::ComfoAirDriver(HardwareSerial& port) : _serial(&port), _ready(false) {
    _ioMutex = xSemaphoreCreateRecursiveMutex();
    _mqttCfgMutex = xSemaphoreCreateMutex();
}

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
			data = std::move(rawData);
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

    IoLock lock(_ioMutex);
    if (!lock.ok) {
        AWM_LOGW("RS232 belegt – cmd=0x%04X übersprungen", command);
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

bool ComfoAirDriver::setRs232Mode(uint8_t mode, std::vector<uint8_t>& response) {
    uint16_t rc; return sendCommand(0x009B, { mode }, rc, response);
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

// 0x0099 – Ventilationsstufe setzen
bool ComfoAirDriver::setLevel(uint8_t level) {
    std::vector<uint8_t> payload{ level };
    uint16_t respCmd = 0;
    std::vector<uint8_t> resp;
    // Antwort ist 0x009A mit optionalem Payload (kann leer sein)
    const bool ok = sendCommand(0x0099, payload, respCmd, resp);
    if (ok) _pollRequested = true;   // Snapshot zeitnah aktualisieren
    return ok;
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
        _pollRequested = true;
        _mqttDirty = true;
    }
    return ok;
}

// 0x00D3 – Komforttemperatur Raw setzen
bool ComfoAirDriver::setComfortTemperatureRaw(uint8_t tempRaw) {
    if (!_ready) {
        AWM_LOGE("RS232 nicht bereit – ist die serielle Verbindung gesteckt?");
        return false;
    }

    IoLock lock(_ioMutex);
    if (!lock.ok) return false;

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
    serviceManualModeAutoStop();
    uint32_t now = millis();
    const bool requested = _pollRequested;
    if (!force && !requested && now - _lastPoll < minIntervalMs) return _stateOk;
    _pollRequested = false;
    _lastPoll = now;
    _mqttDirty = true; // Snapshots ändern sich -> nächster mqttLoop() veröffentlicht

    // Schnelle Werte: jeder Zyklus
    _fanValid   = getFanStatus(_fan);
    _tempsValid = getTemperatureStatus(_temps);

    // Antwortet die Anlage gar nicht (RS232 nicht gesteckt/Anlage aus), den Zyklus abkürzen,
    // statt den loop() mit ~15 weiteren ACK-Timeouts zu blockieren.
    if (!_fanValid && !_tempsValid) {
        _ainValid = _ventValid = _anlagenconfValid = _flapValid = _bypassValid = false;
        _tempsD1Valid = false;
        _stateOk = false;
        AWM_LOGW("Poll: Anlage antwortet nicht (Fan/Temp) – Zyklus abgebrochen");
        return false;
    }

    _ainValid         = getAnalogInputs(_ain);
    _ventValid        = getVentilationLevel(_vent);
    _anlagenconfValid = getAnlagenconf(_anlagenconf);
    _flapValid        = getFlapStatus(_flap);
    _bypassValid      = getBypassStatusDF(_bypass);

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

    // Seltene Werte (ändern sich praktisch nie): beim ersten Mal, bei force und danach alle 60 s
    if (force || !_slowPolled || now - _lastSlowPoll >= 60000UL) {
        _lastSlowPoll = now;
        _slowPolled = true;

        bool inputsOk  = getInputs(_inputs);
        bool sens97Ok  = getSensorData97(_sens97);
        bool ewtOk     = getEwtStatusEB(_ewt);
        _preheatValid  = getPreheatStatusE1(_preheat);
        bool runtimeOk = getRuntimeDD(_runtime);
        bool errorsOk  = getErrorsD9(_errors);
        _analogCfgValid = getAnalogConfig(_analogCfg);
        _delayValid     = getDelayTimes(_delay);
        _keyStatusValid = getKeyStatus(_keyStatus);

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

        _slowOk = inputsOk && sens97Ok && ewtOk && _preheatValid && runtimeOk && errorsOk
               && _analogCfgValid && _delayValid && _keyStatusValid;
    }

    // Global nur für Diagnose/Kompatibilität
    const bool fastOk = _fanValid && _tempsValid && _ainValid && _ventValid
                     && _anlagenconfValid && _flapValid && _bypassValid;
    _stateOk = fastOk && _slowOk;
    if (!_stateOk) AWM_LOGW("Poll teilweise fehlgeschlagen (Best-Effort aktiv)");
    return _stateOk;
}

// JSON für /api/comfo/state aus den gepollten Snapshots – ohne eigenen RS232-Zugriff.
bool ComfoAirDriver::fillStateJson(JsonDocument& doc) {
    auto decodeTemp = [](int8_t raw) { return (static_cast<float>(raw) / 2.0f) - 20.0f; };

    const bool core = _fanValid && _tempsValid && _ventValid && _flapValid && _bypassValid && _anlagenconfValid;
    doc["ok"] = core;
    doc["stateOk"] = _stateOk;
    doc["ageMs"] = millis() - _lastPoll;
    doc["manualModeActive"] = _manualModeActive;

    if (_tempsValid) {
        auto t = doc.createNestedObject("temps");
        t["t1"] = decodeTemp(_temps.t1Outside);
        t["t2"] = decodeTemp(_temps.t2Supply);
        t["t3"] = decodeTemp(_temps.t3Exhaust);
        t["t4"] = decodeTemp(_temps.t4Outlet);
        if (_tempsD1Valid) t["comfortC"] = decodeTemp(_tempsD1.comfortTemp);
        else if (_comfortTempCmdValid) t["comfortC"] = _comfortTempCmdC;
    }

    if (_fanValid) {
        auto f = doc.createNestedObject("fan");
        f["supplyPct"] = _fan.supplyPercent; f["exhaustPct"] = _fan.exhaustPercent;
        f["supplyRpm"] = _fan.supplyRpm;     f["exhaustRpm"] = _fan.exhaustRpm;
    }

    if (_ventValid) {
        auto v = doc.createNestedObject("vent");
        v["current"]       = _vent.currentLevel;
        v["supplyCur"]     = _vent.supplyCurrent;
        v["exhaustCur"]    = _vent.exhaustCurrent;
        v["supplyHigh"]    = _vent.supplyHigh;
        v["exhaustHigh"]   = _vent.exhaustHigh;
        v["exhaustAbsent"] = _vent.exhaustAbsent;
        v["exhaustLow"]    = _vent.exhaustLow;
        v["exhaustMedium"] = _vent.exhaustMedium;
        v["supplyAbsent"]  = _vent.supplyAbsent;
        v["supplyLow"]     = _vent.supplyLow;
        v["supplyMedium"]  = _vent.supplyMedium;
    }

    if (_bypassValid) {
        auto b = doc.createNestedObject("bypass");
        b["factor"] = _bypass.factor;
        b["stage"] = _bypass.stage;
        b["correction"] = _bypass.correction;
        b["summerMode"] = _bypass.summerMode;
    }

    if (_anlagenconfValid) {
        auto s = doc.createNestedObject("statusD5");
        s["preheaterPresent"] = _anlagenconf.preheaterPresent;
        s["bypassPresent"]    = _anlagenconf.bypassPresent;
        s["type"]             = _anlagenconf.type;
        s["size"]             = _anlagenconf.size;
        s["options"]          = _anlagenconf.options;
        s["activeStatus1"]    = _anlagenconf.activeStatus1;
        s["activeStatus2"]    = _anlagenconf.activeStatus2;
        s["activeStatus3"]    = _anlagenconf.activeStatus3;
        s["enthalpyPresent"]  = _anlagenconf.enthalpyPresent;
        s["ewtPresent"]       = _anlagenconf.ewtPresent;
    }

    if (_flapValid) {
        auto fl = doc.createNestedObject("flap");
        fl["bypassPercent"]  = _flap.bypassPercent;
        fl["preheaterState"] = _flap.preheaterState;
        fl["bypassAdc"]      = _flap.bypassAdc;
        fl["preheaterAdc"]   = _flap.preheaterAdc;
    }

    auto m = doc.createNestedObject("manual");
    m["active"] = _manualModeActive;
    m["requestedBypass"] = _manualRequestedBypass;
    m["requestedPreheater"] = _manualRequestedPreheater;
    m["requestedBypassText"] = getManualRequestedBypassText();
    m["requestedPreheaterText"] = getManualRequestedPreheaterText();

    if (_rsModeValid) doc["rs232Mode"] = _rsMode;
    return core;
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

    const String& b = _mqttTopicPrefixS;

    AWM_LOGI("MQTT RX topic='%s' payload='%s'", t.c_str(), msg.c_str());

    // Kommando = Topic ohne "<prefix>/cmd/"; leer bei fremdem Topic.
    const String cmdBase = b + "/cmd/";
    const String cmd = t.startsWith(cmdBase) ? t.substring(cmdBase.length()) : String();

    auto publishCmdResult = [&](const String& name, bool ok, const String& err = "") {
        StaticJsonDocument<192> d;
        d["cmd"] = name;
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
    if (cmd == "level/bereich") {
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
    if (cmd == "comfort/bereich" || cmd == "comfort" || cmd == "comfort/json") {
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
        if (ok) requestPoll(); // Poll + Publish im nächsten loop()-Durchlauf
        publishCmdResult("comfort", ok, ok ? "" : "setComfort failed");
        return;
    }

    // -------------------------------------------------------------------------
    // rs232Mode (0..4)
    // -------------------------------------------------------------------------
    if (cmd == "rs232Mode/bereich") {
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
    if (cmd == "manual/bereich") {
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
    if (cmd == "manual/flaps/bereich") {
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
    if (cmd == "manual/analog/bereich") {
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
            requestPoll(); // Poll + Publish im nächsten loop()-Durchlauf
        }
        publishCmdResult("manual/analog", ok, ok ? "" : "setManualAnalog failed");
        return;
    }

    // -------------------------------------------------------------------------
    // status (Pflichtfelder + Range wie HTML-API)
    // -------------------------------------------------------------------------
    if (cmd == "status/bereich") {
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
            requestPoll(); // Poll + Publish im nächsten loop()-Durchlauf
        }
        publishCmdResult("status", ok, ok ? "" : "setStatus failed");
        return;
    }

    // -------------------------------------------------------------------------
    // vent (Pflichtfelder + 0..100 wie HTML-API)
    // -------------------------------------------------------------------------
    if (cmd == "vent/bereich") {
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
            requestPoll(); // Poll + Publish im nächsten loop()-Durchlauf
        }
        publishCmdResult("vent", ok, ok ? "" : "setVentilationLevels failed");
        return;
    }

    // -------------------------------------------------------------------------
    // levelStep (zusätzlicher MQTT-Komfortbefehl)
    // -------------------------------------------------------------------------
    if (cmd == "levelStep/bereich") {
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

    const String& b = _mqttTopicPrefixS; // bereits normalisiert (applyMqttCfgOwned)

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

    for (size_t i = 0; i < sizeof(kCmdTopicSuffixes) / sizeof(kCmdTopicSuffixes[0]); ++i) {
        sub(b + kCmdTopicSuffixes[i]);
    }

    _mqttLastByTopic.clear();
    _mqtt.publish(onlineTopic.c_str(), "{\"online\":1}", true);
    _mqtt.publish((b + "/status/command/bereich").c_str(), "{\"cmd\":\"\",\"ok\":true}", true);

    // Cache ist leer -> der nächste mqttPublishState() sendet den kompletten Snapshot.
    _mqttDirty = true;

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
    IoLock lock(_ioMutex);
    if (!lock.ok) return false;
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
    IoLock lock(_ioMutex);
    if (!lock.ok) return false;
    while (_serial->available()) _serial->read();

    // Protokoll: 0x00CF mit 9 Byte Payload, letztes Byte reserviert = 0x00
    std::vector<uint8_t> payload{
        exhaustAbsent, exhaustLow, exhaustMedium,
        supplyAbsent, supplyLow, supplyMedium,
        exhaustHigh, supplyHigh,
        0x00
    };

    writeFrame(0x00CF, payload);
    const bool ok = readAck(150);
    if (ok) _pollRequested = true;
    return ok;
}

bool ComfoAirDriver::startManualMode() {
    // 0x0001 – Manuellen Modus starten.
    // Bei Erfolg (Antwort 0x0002): Klappenverriegelungen zurücksetzen.
    uint16_t rc = 0;
    std::vector<uint8_t> resp;
    _mqttDirty = true;
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
        _mqttDirty = true;
        return true;
    }
    return false;
}

bool ComfoAirDriver::setManualAnalog(uint8_t supplyPercent, uint8_t exhaustPercent, uint8_t postheaterPercent) {
    // 0x0007 – Analogausgänge im manuellen Modus setzen (0–100%).
    // // Startet manuellen Modus automatisch falls noch nicht aktiv.
    // Fallback: nur ACK akzeptieren falls kein Antwort-Frame kommt.

    auto inRange = [](uint8_t v) { return v <= 100; };
    if (!inRange(supplyPercent) || !inRange(exhaustPercent) || !inRange(postheaterPercent)) return false;

    IoLock lock(_ioMutex);
    if (!lock.ok) return false;

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

    IoLock lock(_ioMutex);
    if (!lock.ok) return false;

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
    _mqttDirty = true;
    return true;
}

bool ComfoAirDriver::setManualFlapsWithAutoStop(uint8_t bypass, uint8_t preheater, uint16_t delaySeconds) {
    // Ruft setManualFlaps() auf und armt danach den Auto-Stop-Timer.
    // delaySeconds == 0: kein Auto-Stop (Projektrichtlinie: manueller Stop).

    if (!setManualFlaps(bypass, preheater)) return false;

    _manualFlapsLastDelaySec = delaySeconds;
    _mqttDirty = true;

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
        _mqttDirty = true;
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

// Nur aus dem loop()-Task aufrufen (PubSubClient ist nicht threadsicher).
bool ComfoAirDriver::mqttApplyConfig(const ComfoMqttConfig& cfg, bool enabled, bool publishNow) {
    applyMqttCfgOwned(cfg);
    _mqtt.setServer(_mqttCfg.host, _mqttCfg.port);
    _mqttEnabled = enabled;

    if (_mqtt.connected()) _mqtt.disconnect();
    _mqttLastByTopic.clear();
    _lastMqttReconnect = millis() - 3000; // sofortiger Verbindungsversuch
    _mqttDirty = true;

    if (!_mqttEnabled) return true;
    if (!mqttEnsureConnected()) return false;

    if (publishNow) mqttPublishState();
    return true;
}

// Threadsicher: merkt die neue Konfiguration vor; mqttLoop() übernimmt sie im loop()-Task.
void ComfoAirDriver::mqttRequestConfig(const ComfoMqttConfig& cfg, bool enabled) {
    if (!_mqttCfgMutex || xSemaphoreTake(_mqttCfgMutex, pdMS_TO_TICKS(1000)) != pdTRUE) return;
    _pendHost = cfg.host ? cfg.host : "";
    _pendUser = cfg.user ? cfg.user : "";
    _pendPass = cfg.pass ? cfg.pass : "";
    _pendClientId = cfg.clientId ? cfg.clientId : "";
    _pendTopicPrefix = cfg.topicPrefix ? cfg.topicPrefix : "";
    _pendPort = cfg.port;
    _pendEnabled = enabled;
    _mqttCfgPending = true;
    xSemaphoreGive(_mqttCfgMutex);
}

void ComfoAirDriver::mqttApplyPendingConfig() {
    if (!_mqttCfgPending || !_mqttCfgMutex) return;
    if (xSemaphoreTake(_mqttCfgMutex, 0) != pdTRUE) return; // nächster Durchlauf

    const String host = _pendHost, user = _pendUser, pass = _pendPass;
    const String clientId = _pendClientId, prefix = _pendTopicPrefix;
    const int port = _pendPort;
    const bool enabled = _pendEnabled;
    _mqttCfgPending = false;
    xSemaphoreGive(_mqttCfgMutex);

    const ComfoMqttConfig cfg{ host.c_str(), port, user.c_str(), pass.c_str(), clientId.c_str(), prefix.c_str() };
    const bool ok = mqttApplyConfig(cfg, enabled, true);
    AWM_LOGI("MQTT-Konfiguration übernommen (%s)",
             !enabled ? "deaktiviert" : (ok ? "verbunden" : "Verbindung ausstehend"));
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
    _mqttDirty = true; // Publish in mqttLoop() (Aufruf kommt auch aus dem AsyncTCP-Task)
}

void ComfoAirDriver::mqttLoop() {
    serviceManualModeAutoStop();
    mqttApplyPendingConfig();

    if (!_mqttEnabled) return;
    if (!mqttEnsureConnected()) return; // prüft auch den WiFi-Status

    _mqtt.loop();

    // Nur bei geändertem Zustand veröffentlichen (plus 1-s-Sicherheitsnetz),
    // statt in jedem loop()-Durchlauf alle JSON-Dokumente neu aufzubauen.
    if (_mqttDirty || millis() - _lastMqttPublish >= 1000) mqttPublishState();
}

void ComfoAirDriver::mqttPublishState() {
    if (!_mqttEnabled || !_mqtt.connected()) return;
    _mqttDirty = false;
    _lastMqttPublish = millis();

    const String& b = _mqttTopicPrefixS;

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