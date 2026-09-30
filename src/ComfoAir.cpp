// SPDX-License-Identifier: MIT
#include "ComfoAir.h"
#include "AWM_Logging.h"

static constexpr uint8_t START1 = 0x07;
static constexpr uint8_t START2 = 0xF0;
static constexpr uint8_t END1   = 0x07;
static constexpr uint8_t END2   = 0x0F;
static constexpr uint8_t ACK1   = 0x07;
static constexpr uint8_t ACK2   = 0xF3;

ComfoAir::ComfoAir() {}

bool ComfoAir::begin(HardwareSerial& serial, int8_t rxPin, int8_t txPin, uint32_t baud) {
	_serial = &serial;
	if (rxPin >= 0 && txPin >= 0) {
		_serial->begin(baud, SERIAL_8N1, rxPin, txPin);
	} else {
		_serial->begin(baud, SERIAL_8N1);
	}
	return true;
}

void ComfoAir::setTimeout(uint32_t ms) { _timeoutMs = ms; }

uint8_t ComfoAir::calcChecksum(uint16_t cmd, const std::vector<uint8_t>& data) const {
	uint32_t sum = (cmd >> 8) + (cmd & 0xFF) + static_cast<uint32_t>(data.size());
	for (auto b : data) sum += b;
	sum += 173;
	return static_cast<uint8_t>(sum & 0xFF);
}

bool ComfoAir::sendFrame(uint16_t cmd, const std::vector<uint8_t>& data) {
	if (!_serial) return false;
	std::vector<uint8_t> frame;
	frame.reserve(2 + 2 + 1 + data.size() * 2 + 3);
	frame.push_back(START1);
	frame.push_back(START2);
	frame.push_back(static_cast<uint8_t>(cmd >> 8));
	frame.push_back(static_cast<uint8_t>(cmd & 0xFF));
	frame.push_back(static_cast<uint8_t>(data.size()));
	for (auto b : data) {
		frame.push_back(b);
		if (b == 0x07) frame.push_back(0x07); // Stuffing laut Protokoll
	}
	frame.push_back(calcChecksum(cmd, data));
	frame.push_back(END1);
	frame.push_back(END2);
	_serial->write(frame.data(), frame.size());
	_serial->flush();
	return true;
}

bool ComfoAir::readAck(uint32_t timeoutMs) {
	uint8_t b1 = 0, b2 = 0;
	if (!readByte(b1, timeoutMs) || !readByte(b2, timeoutMs)) return false;
	return b1 == ACK1 && b2 == ACK2;
}

void ComfoAir::sendAck() {
	if (_serial) {
		uint8_t ack[2] = { ACK1, ACK2 };
		_serial->write(ack, 2);
		_serial->flush();
	}
}

bool ComfoAir::readByte(uint8_t& b, uint32_t timeoutMs) {
	if (!_serial) return false;
	int c = _serial->read();
	uint32_t t0 = millis();
	while (c < 0 && (millis() - t0) < timeoutMs) {
		delay(1);
		c = _serial->read();
	}
	if (c < 0) return false;
	b = static_cast<uint8_t>(c);
	return true;
}

bool ComfoAir::readFrame(Frame& out, uint32_t timeoutMs) {
	uint8_t b = 0;
	if (!readByte(b, timeoutMs) || b != START1) return false;
	if (!readByte(b, timeoutMs) || b != START2) return false;

	uint8_t cmdH = 0, cmdL = 0, len = 0;
	if (!readByte(cmdH, timeoutMs)) return false;
	if (!readByte(cmdL, timeoutMs)) return false;
	if (!readByte(len, timeoutMs)) return false;

	out.cmd = (static_cast<uint16_t>(cmdH) << 8) | cmdL;

	std::vector<uint8_t> data;
	data.reserve(len);
	while (data.size() < len) {
		if (!readByte(b, timeoutMs)) return false;
		if (b == 0x07) {
			uint8_t next = 0;
			if (!readByte(next, timeoutMs)) return false;
			if (next == 0x07) {
				data.push_back(0x07); // entfalten
			} else {
				// Unerwartetes Pattern
				return false;
			}
		} else {
			data.push_back(b);
		}
	}

	uint8_t cs = 0;
	if (!readByte(cs, timeoutMs)) return false;

	uint8_t e1 = 0, e2 = 0;
	if (!readByte(e1, timeoutMs) || !readByte(e2, timeoutMs)) return false;
	if (e1 != END1 || e2 != END2) return false;

	const uint8_t expected = calcChecksum(out.cmd, data);
	if (cs != expected) {
		AWM_LOGW("ComfoAir: Checksumme falsch (empf=%02X, soll=%02X)", cs, expected);
		return false;
	}

	out.data = std::move(data);
	return true;
}

bool ComfoAir::transact(uint16_t cmd, const std::vector<uint8_t>& payload, Frame& response) {
	if (!sendFrame(cmd, payload)) return false;
	if (!readAck(_timeoutMs)) {
		AWM_LOGW("ComfoAir: ACK nach Request fehlt");
		return false;
	}
	if (!readFrame(response, _timeoutMs)) {
		AWM_LOGW("ComfoAir: Antwort-Frame fehlt");
		return false;
	}
	// Antwort-Kommando = Request + 1
	const uint16_t expectedCmd = static_cast<uint16_t>(cmd + 1);
	if (response.cmd != expectedCmd) {
		AWM_LOGW("ComfoAir: Unerwartetes Kommando 0x%04X (erwartet 0x%04X)", response.cmd, expectedCmd);
	}
	sendAck(); // Bestätigung zurück an ComfoAir
	return true;
}

// ===== High-Level API =====

bool ComfoAir::readFirmware(FirmwareInfo& out) {
	Frame rsp;
	if (!transact(0x0069, {}, rsp)) return false;
	if (rsp.data.size() < 13) return false;
	out.major = rsp.data[0];
	out.minor = rsp.data[1];
	out.beta  = rsp.data[2];
	out.name.clear();
	for (size_t i = 3; i < rsp.data.size(); ++i) out.name += static_cast<char>(rsp.data[i]);
	_lastFw = out; _fwValid = true;
	return true;
}

bool ComfoAir::readBootloader(BootloaderInfo& out) {
	Frame rsp;
	if (!transact(0x0067, {}, rsp)) return false;
	if (rsp.data.size() < 13) return false;
	out.major = rsp.data[0];
	out.minor = rsp.data[1];
	out.beta  = rsp.data[2];
	out.name.clear();
	for (size_t i = 3; i < rsp.data.size(); ++i) out.name += static_cast<char>(rsp.data[i]);
	_lastBoot = out; _bootValid = true;
	return true;
}

bool ComfoAir::readConnector(ConnectorInfo& out) {
	Frame rsp;
	if (!transact(0x00A1, {}, rsp)) return false;
	if (rsp.data.size() < 14) return false;
	out.major = rsp.data[0];
	out.minor = rsp.data[1];
	out.name.clear();
	for (size_t i = 2; i < 12 && i < rsp.data.size(); ++i) out.name += static_cast<char>(rsp.data[i]);
	out.ccEaseMaj = (rsp.data[12] >> 4) & 0x0F;
	out.ccEaseMin = rsp.data[12] & 0x0F;
	out.ccLuxeMaj = (rsp.data[13] >> 4) & 0x0F;
	out.ccLuxeMin = rsp.data[13] & 0x0F;
	_lastConn = out; _connValid = true;
	return true;
}

bool ComfoAir::readFanStatus(FanStatus& out) {
	// unverändert + Cache
	Frame rsp;
	if (!transact(0x000B, {}, rsp)) return false;
	if (rsp.data.size() < 6) return false;
	out.supplyPct  = rsp.data[0];
	out.exhaustPct = rsp.data[1];
	uint16_t rawSup = (static_cast<uint16_t>(rsp.data[2]) << 8) | rsp.data[3];
	uint16_t rawExh = (static_cast<uint16_t>(rsp.data[4]) << 8) | rsp.data[5];
	out.supplyRpm  = decodeRpm(rawSup);
	out.exhaustRpm = decodeRpm(rawExh);
	_lastFan = out; _fanValid = true;
	return true;
}

bool ComfoAir::readTemperatures(Temperatures& out) {
	// unverändert + Cache
	Frame rsp;
	if (!transact(0x000F, {}, rsp)) return false;
	if (rsp.data.size() < 4) return false;
	out.t1_outdoor = decodeTemp(rsp.data[0]);
	out.t2_supply  = decodeTemp(rsp.data[1]);
	out.t3_extract = decodeTemp(rsp.data[2]);
	out.t4_exhaust = decodeTemp(rsp.data[3]);
	_lastTemps = out; _tempValid = true;
	return true;
}

bool ComfoAir::readStageStatus(StageStatus& out) {
	// unverändert + Cache
	Frame rsp;
	if (!transact(0x00CD, {}, rsp)) return false;
	if (rsp.data.size() < 14) return false;
	out.exhaustAway = rsp.data[0];
	out.exhaustL1   = rsp.data[1];
	out.exhaustL2   = rsp.data[2];
	out.supplyAway  = rsp.data[3];
	out.supplyL1    = rsp.data[4];
	out.supplyL2    = rsp.data[5];
	out.exhaustNow  = rsp.data[6];
	out.supplyNow   = rsp.data[7];
	out.currentStage = rsp.data[8];
	out.exhaustL3   = rsp.data[10];
	out.supplyL3    = rsp.data[11];
	_lastStage = out; _stageValid = true;
	return true;
}

bool ComfoAir::setStage(Stage s) {
	std::vector<uint8_t> payload = { static_cast<uint8_t>(s) };
	if (!sendFrame(0x0099, payload)) return false;
	if (!readAck(_timeoutMs)) return false;
	return true;
}

bool ComfoAir::setRs232Mode(Rs232Mode req, Rs232Mode& confirmed) {
	std::vector<uint8_t> payload = { static_cast<uint8_t>(req) };
	Frame rsp;
	if (!transact(0x009B, payload, rsp)) return false;
	if (rsp.data.empty()) return false;
	confirmed = static_cast<Rs232Mode>(rsp.data[0]);
	return true;
}

// ===== Helpers =====
float ComfoAir::decodeTemp(uint8_t raw) {
	return (static_cast<float>(raw) * 0.5f) - 20.0f;
}

uint16_t ComfoAir::decodeRpm(uint16_t raw) {
	if (raw == 0) return 0;
	return static_cast<uint16_t>(1875000UL / raw);
}

// ===== CC-Ease (Kapitel 8.0) =====

bool ComfoAir::readCcEaseData(CcEaseData& out) {
	Frame rsp;
	if (!transact(0x0033, {}, rsp)) return false; // Antwort kann 0x38/0x3E/0x40/0x98/0x9C/0xAA/0xCE/0xD2/0xE0/0xE2/0xEC sein
	out.cmd = rsp.cmd;
	out.payload = rsp.data;
	_lastCcData = out; _ccDataValid = true;
	return true;
}

bool ComfoAir::readCcEaseParams(CcEaseParams& out) {
	Frame rsp;
	if (!transact(0x0035, {}, rsp)) return false; // Antwort 0x003C laut Doku
	if (rsp.data.size() < 5) return false;
	out.day    = rsp.data[0];
	out.hour   = rsp.data[1];
	out.minute = rsp.data[2];
	out.tempC  = decodeTemp(rsp.data[3]);
	out.verMaj = (rsp.data[4] >> 4) & 0x0F;
	out.verMin = rsp.data[4] & 0x0F;
	_lastCcParams = out; _ccParamsValid = true;
	return true;
}

bool ComfoAir::readCcEaseKeys(CcEaseKeys& out) {
	Frame rsp;
	if (!transact(0x0037, {}, rsp)) return false; // Antwort 0x003C
	if (rsp.data.size() < 7) return false;
	out.fan   = rsp.data[0];
	out.mode  = rsp.data[1];
	out.clock = rsp.data[2];
	out.temp  = rsp.data[3];
	out.plus  = rsp.data[4];
	out.minus = rsp.data[5];
	out.status= rsp.data[6];
	_lastCcKeys = out; _ccKeysValid = true;
	return true;
}

bool ComfoAir::readCcEaseRf(CcEaseRfCmd& out) {
	Frame rsp;
	if (!transact(0x0039, {}, rsp)) return false; // Antwort 0x0040
	if (rsp.data.size() < 16) return false;
	out.rxType   = rsp.data[0];
	out.rxId     = rsp.data[1];
	out.txType   = rsp.data[2];
	out.txId     = rsp.data[3];
	out.lifetime = rsp.data[4];
	out.datatype = rsp.data[5];
	for (size_t i = 0; i < 10; ++i) out.data[i] = rsp.data[6 + i];
	_lastCcRf = out; _ccRfValid = true;
	return true;
}

bool ComfoAir::setCcEaseParams(uint8_t day, uint8_t hour, uint8_t minute, float tempC) {
    std::vector<uint8_t> payload = {
        day, hour, minute,
        encodeTemp(tempC), // (T+20)*2
        0x10 | 0x0 // Version Major/Minor (falls unbekannt auf 0)
    };
    if (!sendFrame(0x0038, payload)) return false;
    return readAck(_timeoutMs);
}

bool ComfoAir::setCcEaseDisplay(const CcEaseDisplay& d) {
    std::vector<uint8_t> p = {
        d.line1, d.line2, d.line3, d.line4, d.line5,
        d.line6, d.line7, d.line8, d.line10
    };
    if (!sendFrame(0x003C, p)) return false;
    return readAck(_timeoutMs);
}

bool ComfoAir::setCcEaseRfAddress(uint32_t addr) {
    std::vector<uint8_t> p = {
        static_cast<uint8_t>(addr & 0xFF),
        static_cast<uint8_t>((addr >> 8) & 0xFF),
        static_cast<uint8_t>((addr >> 16) & 0xFF),
        static_cast<uint8_t>((addr >> 24) & 0xFF)
    };
    if (!sendFrame(0x003E, p)) return false;
    return readAck(_timeoutMs);
}

bool ComfoAir::sendCcEaseRf(const CcEaseRfTx& tx) {
    std::vector<uint8_t> p;
    p.reserve(21);
    p.push_back(tx.rxType);
    p.push_back(tx.rxId);
    p.push_back(tx.txType);
    p.push_back(tx.txId);
    p.push_back(tx.lifetime);
    p.push_back(tx.datatype);
    for (auto b : tx.data) p.push_back(b);
    // RF Adresse (Low..High)
    p.push_back(static_cast<uint8_t>(tx.rfAddr & 0xFF));
    p.push_back(static_cast<uint8_t>((tx.rfAddr >> 8) & 0xFF));
    p.push_back(static_cast<uint8_t>((tx.rfAddr >> 16) & 0xFF));
    p.push_back(static_cast<uint8_t>((tx.rfAddr >> 24) & 0xFF));
    p.push_back(tx.control);
    if (!sendFrame(0x0040, p)) return false;
    return readAck(_timeoutMs);
}