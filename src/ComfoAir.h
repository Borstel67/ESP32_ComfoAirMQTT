// SPDX-License-Identifier: MIT
// ComfoAir RS232 Protokoll-Implementierung
#pragma once
#include <Arduino.h>
#include <HardwareSerial.h>
#include <vector>
#include <array>

class ComfoAir {
public:
	// Ventilationsstufen laut Doku 0x00 0x99
	enum class Stage : uint8_t { Auto = 0, Away = 1, Low = 2, Medium = 3, High = 4 };

	enum class Rs232Mode : uint8_t {
		None = 0x00, PcOnly = 0x01, PcMaster = 0x03, PcLog = 0x04, CcEaseOnly = 0x02
	};

	struct FirmwareInfo {
		uint8_t major = 0, minor = 0, beta = 0;
		String  name;
	};

	using BootloaderInfo = FirmwareInfo; // gleicher Aufbau

	struct ConnectorInfo {
		uint8_t major = 0, minor = 0;
		String  name;
		uint8_t ccEaseMaj = 0, ccEaseMin = 0;
		uint8_t ccLuxeMaj = 0, ccLuxeMin = 0;
	};

	struct FanStatus {
		uint8_t supplyPct = 0;
		uint8_t exhaustPct = 0;
		uint16_t supplyRpm = 0;
		uint16_t exhaustRpm = 0;
	};

	struct Temperatures {
		float t1_outdoor = NAN;
		float t2_supply  = NAN;
		float t3_extract = NAN;
		float t4_exhaust = NAN;
	};

	struct Inputs {
		uint8_t stageSwitch = 0;   // Bit0=L1, Bit1=L2
		uint8_t switches    = 0;   // Bit0=Bad, Bit1=Küchenhaube, Bit2=Ext.Filter, Bit3=WTW, Bit4=Bad2
	};

	struct FlapStatus {
		uint8_t bypassPct = 0xFF;  // Bypass (%) 0xFF=undef
		uint8_t preheat   = 2;     // Vorheizung: 1=offen,0=zu,2=unbekannt
		uint8_t bypassAdc = 0;     // Bypass Motorstrom (ADC)
		uint8_t preheatAdc= 0;     // Vorheizung Motorstrom (ADC)
	};

	struct ButtonsStatus { uint8_t raw = 0; }; // 0x00 nichts, 0xFF Fehler

	struct Analogs { uint8_t a1=0,a2=0,a3=0,a4=0; }; // 0..255 = 0..10V

	struct SensorData {
		float enthalpyTemp = NAN;  // Enthalpie-Sensor Temp (°C*)
		uint8_t enthalpyHum = 0;   // Enthalpie-Sensor Feuchte (%)
		uint8_t a1=0,a2=0;         // Analog 1/2 (%)
		uint8_t enthalpyCoeff=0;   // Enthalpie Koeffizient (%)
		uint8_t enthalpyTimer=0;   // 0..240 (=0..2880 Min)
		uint8_t a1_to=0,a1_from=0,a2_to=0,a2_from=0;
		uint8_t a3=0,a4=0,a3_to=0,a3_from=0,a4_to=0,a4_from=0;
	};

	struct AnalogConfig {
		uint8_t present=0, control=0, polarity=0; // Bits: 0x01=A1,0x02=A2,0x04=A3,0x08=A4,0x10=RF
		uint8_t a1Min=0,a1Max=0,a1Set=0;
		uint8_t a2Min=0,a2Max=0,a2Set=0;
		uint8_t a3Min=0,a3Max=0,a3Set=0;
		uint8_t a4Min=0,a4Max=0,a4Set=0;
		uint8_t rfMin=0,rfMax=0,rfSet=0;
		uint8_t priority=0; // 0=Analoge Eingänge, 1=Wochenprogramm
	};

	struct Delays {
		uint8_t bathOn=0,bathOff=0; // Bad Ein-/Ausschaltverz. (Min)
		uint8_t l1Off=0;            // L1 Ausschaltverz. (Min)
		uint8_t boost=0;            // Stoßlüftung (Min)
		uint8_t filterWeeks=0;      // Filter Zähler (Wochen)
		uint8_t rfShort=0, rfLong=0;// RF hoch Zeit kurz/lang (Min)
		uint8_t hoodOff=0;          // Küchenhaube Ausschaltverz. (Min)
	};

	struct StageStatus {
		uint8_t exhaustAway=0;   // Abluft abwesend (%)
		uint8_t exhaustL1=0;     // Abluft L1 (%)
		uint8_t exhaustL2=0;     // Abluft L2 (%)
		uint8_t supplyAway=0;    // Zuluft abwesend (%)
		uint8_t supplyL1=0;      // Zuluft L1 (%)
		uint8_t supplyL2=0;      // Zuluft L2 (%)
		uint8_t exhaustL3=0;     // Abluft L3 (%)
		uint8_t supplyL3=0;      // Zuluft L3 (%)
		uint8_t exhaustNow=0;    // Abluft aktuell (%)
		uint8_t supplyNow=0;     // Zuluft aktuell (%)
		uint8_t currentStage=0;  // 0=Auto,1=Away,2=L1,3=L2,4=L3
	};

	struct TemperaturesEx {
		float comfort=NAN;       // Komforttemp (°C*)
		float t1=NAN,t2=NAN,t3=NAN,t4=NAN; // Außen, Zuluft, Abluft, Fortluft
		uint8_t sensorsPresent=0; // Bit0=T1,1=T2,2=T3,3=T4,4=EWT,5=Nachheiz.,6=Küchenhaube
		float ewt=NAN, postHeat=NAN, hood=NAN; // EWT, Nachheizung, Küchenhaube (°C*)
	};

	struct SystemStatus {
		uint8_t preheatPresent=0; // 1=anwesend
		uint8_t bypassPresent=0;  // 1=anwesend
		uint8_t type=0;           // 1=links, 2=rechts
		uint8_t size=0;           // 1=groß, 2=klein
		uint8_t options=0;        // Bit0=Feuerstätte,1=Küchenhaube,2=Nachheizung,6=Nachheiz PWM
		uint8_t active1=0;        // Bits P10..P17
		uint8_t active2=0;        // Bits P18..P19
		uint8_t active3=0;        // Bits P90..P96
		uint8_t enthalpyPresent=0;// 1=ja,2=ohne Fühler
		uint8_t ewtPresent=0;     // 1=geregelt,2=ungeregelt
	};

	struct Faults {
		uint8_t curA=0, curE=0, lastA=0, lastE=0, prevA=0, prevE=0, preprevA=0, preprevE=0;
		uint8_t filter=0;        // 0=OK,1=Filter voll
		uint8_t curEA=0, lastEA=0, prevEA=0, preprevEA=0;
		uint8_t curAhi=0, lastAhi=0, prevAhi=0, preprevAhi=0;
	};

	struct Hours {
		uint32_t away=0, l1=0, l2=0, frost=0, preheat=0, bypassOpen=0, filter=0, l3=0; // h
	};

	struct BypassCtrl {
		uint8_t reserved1=0,reserved2=0;
		uint8_t factor=0;     // Bypass Faktor
		uint8_t level=0;      // Bypass Stufe
		uint8_t correction=0; // Bypass Korrektur
		uint8_t reserved3=0;
		uint8_t summer=0;     // 1=Sommer,0=Winter
	};

	struct PreheatStatus {
		uint8_t flap=2;        // 1 offen /0 zu /2 unbekannt
		uint8_t frost=0;       // 1 aktiv
		uint8_t preheat=0;     // 1 aktiv
		uint16_t frostMinutes=0; // Min
		uint8_t frostSafety=0;   // 1=extra sicher /4=sicher
	};

	struct RfStatus {
		uint8_t addr4=0, addr3=0, addr2=0, addr1=0; // RF Adresse
		uint8_t id=0;             // RF ID
		uint8_t modulePresent=0;  // Modul anwesend
		uint8_t learnMode=0;      // Selbstlernmodus aktiv
	};

	struct PreheatHistory { uint8_t oldest=0, newest=0; }; // °C* (roh)

	struct EwtHeating {
		float ewtLow=NAN, ewtHigh=NAN; // EWT niedrig/hoch (°C*)
		uint8_t ewtSpeedUp=0;          // EWT speed up (%)
		uint8_t hoodSpeedUp=0;         // Küchenhaube speed up (%)
		uint8_t postPower=0;           // Nachheizung Leistung
		uint8_t postPowerI=0;          // Nachheizung I-Parameter
		float postTarget=NAN;          // Nachheizung T gewünscht (°C*)
	};

	// CC-Ease (Kapitel 8.0)
	struct CcEaseData {                       // 0x0033 Antwortrahmen (durchgereicht)
		uint16_t cmd = 0;                     // Antwort-Kommando
		std::vector<uint8_t> payload;         // Nutzdaten
	};

	struct CcEaseParams {                     // 0x0035 -> 0x003C
		uint8_t day=0;    // 0=Sa..6=Fr
		uint8_t hour=0;   // 0..23
		uint8_t minute=0; // 0..59
		float tempC=NAN;  // (T+20)*2 dekodiert in °C
		uint8_t verMaj=0, verMin=0; // CC-Ease Version
	};

	struct CcEaseKeys {                       // 0x0037 -> 0x003C
		uint8_t fan=0;    // Tastendruckdauer Ventilator (0..255 *16ms)
		uint8_t mode=0;   // Modus
		uint8_t clock=0;  // Uhr
		uint8_t temp=0;   // Temperatur
		uint8_t plus=0;   // +
		uint8_t minus=0;  // -
		uint8_t status=0; // Bit0=Blinken an/aus
	};

	struct CcEaseRfCmd {                      // 0x0039 -> 0x0040
		uint8_t rxType=0; // Modultyp Empfänger
		uint8_t rxId=0;   // Modul-ID Empfänger
		uint8_t txType=0; // Modultyp Sender
		uint8_t txId=0;   // Modul-ID Sender
		uint8_t lifetime=0;
		uint8_t datatype=0;
		std::array<uint8_t,10> data{}; // Datenbyte 1..10
	};

	// CC-Ease Display (0x003C)
	struct CcEaseDisplay {
		uint8_t line1=0, line2=0, line3=0, line4=0, line5=0,
				line6=0, line7=0, line8=0, line10=0; // Bits: Wochentage/Symbole siehe Tabelle
	};

	// CC-Ease RF-TX (0x0040)
	struct CcEaseRfTx {
		uint8_t rxType=0, rxId=0, txType=0, txId=0, lifetime=0, datatype=0;
		std::array<uint8_t,10> data{}; // Datenbytes 1..10
		uint32_t rfAddr = 0;           // RF-Adresse (Low->High)
		uint8_t control = 0;           // 0x01 repeat prev, 0x02 250ms Pause, 0x04 auf Senderadresse empfangen
	};

	ComfoAir();

	bool begin(HardwareSerial& serial, int8_t rxPin = -1, int8_t txPin = -1, uint32_t baud = 9600);
	void setTimeout(uint32_t ms);

	// Firmware-Version abrufen (0x0069 -> 0x006A)
	bool readFirmware(FirmwareInfo& out);
	// Bootloader-Version abrufen (0x0067 -> 0x0068)
	bool readBootloader(BootloaderInfo& out);
	// Konnektorplatinen-Version abrufen (0x00A1 -> 0x00A2)
	bool readConnector(ConnectorInfo& out);
	// Ventilatorstatus abrufen (0x000B -> 0x000C)
	bool readFanStatus(FanStatus& out);
	// Temperaturstatus (T1..T4) abrufen (0x000F -> 0x0010)
	bool readTemperatures(Temperatures& out);
	// Ventilationsstufe setzen (0x0099) – ACK-only
	bool setStage(Stage s);
	// RS232-Modus setzen und bestätigten Modus zurückgeben (0x009B -> 0x009C)
	bool setRs232Mode(Rs232Mode req, Rs232Mode& confirmed);
	// Ventilationsstufe/Stage-Status abrufen (0x0007 -> 0x0008)
	bool readStageStatus(StageStatus& out);
	// Neue Lese-APIs (Lesekommandos 7.1)
	// Eingänge abrufen (0x0003 -> 0x0004)
	bool readInputs(Inputs& out);
	// Klappenstatus abrufen (0x000D -> 0x000E)
	bool readFlaps(FlapStatus& out);
	// Tastenstatus abrufen (0x0011 -> 0x0012)
	bool readButtons(ButtonsStatus& out);
	// Analoge Eingänge abrufen (0x0013 -> 0x0014)
	bool readAnalogs(Analogs& out);
	// Sensordaten (Enthalpie/Analog-Ziele) abrufen (0x0097 -> 0x0098)
	bool readSensorData(SensorData& out);
	// Analog-Konfiguration abrufen (0x009D -> 0x009E)
	bool readAnalogConfig(AnalogConfig& out);
	// Zeit-Verzögerungen abrufen (0x00C9 -> 0x00CA)
	bool readDelays(Delays& out);
	// Erweiterte Temperaturen abrufen (0x00D1 -> 0x00D2)
	bool readTempsEx(TemperaturesEx& out);
	// Systemstatus abrufen (0x00D5 -> 0x00D6)
	bool readSystemStatus(SystemStatus& out);
	// Störungen abrufen (0x00D9 -> 0x00DA)
	bool readFaults(Faults& out);
	// Betriebsstunden abrufen (0x00DD -> 0x00DE)
	bool readHours(Hours& out);
	// Bypassregelung-Status abrufen (0x00DF -> 0x00E0)
	bool readBypassCtrl(BypassCtrl& out);
	// Vorheizungsstatus abrufen (0x00E1 -> 0x00E2)
	bool readPreheatStatus(PreheatStatus& out);
	// RF-Status abrufen (0x00E5 -> 0x00E6)
	bool readRfStatus(RfStatus& out);
	// Vorheizungshistorie abrufen (0x00E9 -> 0x00EA)
	bool readPreheatHistory(PreheatHistory& out);
	// EWT/Nachheizung abrufen (0x00EB -> 0x00EC)
	bool readEwtHeating(EwtHeating& out);

	// CC-Ease lesen (Kapitel 8.0)
	// Rohdaten/Weiterleitung für CC-Ease-Abfragen (0x0033 -> diverse Antworten)
	bool readCcEaseData(CcEaseData& out);
	// CC-Ease Parameter abrufen (0x0035 -> 0x003C)
	bool readCcEaseParams(CcEaseParams& out);
	// CC-Ease Tastenstatus abrufen (0x0037 -> 0x003C)
	bool readCcEaseKeys(CcEaseKeys& out);
	// Empfangenes RF-Kommando (CC-Ease) abrufen (0x0039 -> 0x0040)
	bool readCcEaseRf(CcEaseRfCmd& out);

	// CC-Ease setzen (Kapitel 8.1)
	// CC-Ease Parameter setzen (Zeit/Temp) (0x0038, ACK-only)
	bool setCcEaseParams(uint8_t day, uint8_t hour, uint8_t minute, float tempC);
	// CC-Ease Display-LED/Symbole setzen (0x003C, ACK-only)
	bool setCcEaseDisplay(const CcEaseDisplay& d);
	// CC-Ease RF-Adresse setzen (0x003E, ACK-only)
	bool setCcEaseRfAddress(uint32_t addr);
	// CC-Ease RF-Kommando senden (0x0040, ACK-only)
	bool sendCcEaseRf(const CcEaseRfTx& tx);

	// Letzte Werte abrufen
	const FirmwareInfo&   lastFirmware()     const { return _lastFw; }
	const BootloaderInfo& lastBootloader()   const { return _lastBoot; }
	const ConnectorInfo&  lastConnector()    const { return _lastConn; }
	const FanStatus&      lastFanStatus()    const { return _lastFan; }
	const Temperatures&   lastTemperatures() const { return _lastTemps; }
	const StageStatus&    lastStageStatus()  const { return _lastStage; }
	const Inputs&         lastInputs() const { return _lastInputs; }
	const FlapStatus&     lastFlaps() const { return _lastFlaps; }
	const ButtonsStatus&  lastButtons() const { return _lastButtons; }
	const Analogs&        lastAnalogs() const { return _lastAnalogs; }
	const SensorData&     lastSensorData() const { return _lastSensor; }
	const AnalogConfig&   lastAnalogConfig() const { return _lastAnalogCfg; }
	const Delays&         lastDelays() const { return _lastDelays; }
	const TemperaturesEx& lastTempsEx() const { return _lastTempsEx; }
	const SystemStatus&   lastSystemStatus() const { return _lastStatusSys; }
	const Faults&         lastFaults() const { return _lastFaults; }
	const Hours&          lastHours() const { return _lastHours; }
	const BypassCtrl&     lastBypassCtrl() const { return _lastBypass; }
	const PreheatStatus&  lastPreheatStatus() const { return _lastPreheat; }
	const RfStatus&       lastRfStatus() const { return _lastRf; }
	const PreheatHistory& lastPreheatHistory() const { return _lastPreheatHist; }
	const EwtHeating&     lastEwtHeating() const { return _lastEwt; }

	// CC-Ease
	const CcEaseData&    lastCcEaseData() const { return _lastCcData; }
	const CcEaseParams&  lastCcEaseParams() const { return _lastCcParams; }
	const CcEaseKeys&    lastCcEaseKeys() const { return _lastCcKeys; }
	const CcEaseRfCmd&   lastCcEaseRf() const { return _lastCcRf; }

	bool hasFirmware()     const { return _fwValid; }
	bool hasBootloader()   const { return _bootValid; }
	bool hasConnector()    const { return _connValid; }
	bool hasFanStatus()    const { return _fanValid; }
	bool hasTemperatures() const { return _tempValid; }
	bool hasStageStatus()  const { return _stageValid; }
	bool hasInputs()       const { return _inputsValid; }
	bool hasFlaps()        const { return _flapsValid; }
	bool hasButtons()      const { return _buttonsValid; }
	bool hasAnalogs()      const { return _analogsValid; }
	bool hasSensorData()   const { return _sensorValid; }
	bool hasAnalogConfig() const { return _analogCfgValid; }
	bool hasDelays()       const { return _delaysValid; }
	bool hasTempsEx()      const { return _tempsExValid; }
	bool hasSystemStatus() const { return _statusSysValid; }
	bool hasFaults()       const { return _faultsValid; }
	bool hasHours()        const { return _hoursValid; }
	bool hasBypassCtrl()   const { return _bypassValid; }
	bool hasPreheatStatus()const { return _preheatValid; }
	bool hasRfStatus()     const { return _rfValid; }
	bool hasPreheatHistory() const { return _preheatHistValid; }
	bool hasEwtHeating()   const { return _ewtValid; }
	bool hasCcEaseData()   const { return _ccDataValid; }
	bool hasCcEaseParams() const { return _ccParamsValid; }
	bool hasCcEaseKeys()   const { return _ccKeysValid; }
	bool hasCcEaseRf()     const { return _ccRfValid; }

private:
	struct Frame {
		uint16_t cmd = 0;
		std::vector<uint8_t> data;
	};

	bool transact(uint16_t cmd, const std::vector<uint8_t>& payload, Frame& response);
	bool sendFrame(uint16_t cmd, const std::vector<uint8_t>& data);
	bool readAck(uint32_t timeoutMs);
	bool readFrame(Frame& out, uint32_t timeoutMs);
	void sendAck();

	uint8_t calcChecksum(uint16_t cmd, const std::vector<uint8_t>& data) const;
	bool readByte(uint8_t& b, uint32_t timeoutMs);

	static float decodeTemp(uint8_t raw);      // (raw / 2) - 20
	static uint16_t decodeRpm(uint16_t raw);   // rpm = 1_875_000 / raw
	static uint32_t read24(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16); }
	static uint8_t encodeTemp(float c) {
		int v = lroundf((c + 20.0f) * 2.0f);
		if (v < 0) v = 0; if (v > 255) v = 255;
		return static_cast<uint8_t>(v);
	}

	HardwareSerial* _serial = nullptr;
	uint32_t _timeoutMs = 400; // Standard-Timeout pro Lesevorgang

	FirmwareInfo  _lastFw{};      bool _fwValid=false;
	BootloaderInfo _lastBoot{};    bool _bootValid=false;
	ConnectorInfo _lastConn{};     bool _connValid=false;
	FanStatus     _lastFan{};      bool _fanValid=false;
	Temperatures  _lastTemps{};    bool _tempValid=false;
	StageStatus   _lastStage{};    bool _stageValid=false;
	Inputs _lastInputs{};          bool _inputsValid=false;
	FlapStatus _lastFlaps{};       bool _flapsValid=false;
	ButtonsStatus _lastButtons{};  bool _buttonsValid=false;
	Analogs _lastAnalogs{};        bool _analogsValid=false;
	SensorData _lastSensor{};      bool _sensorValid=false;
	AnalogConfig _lastAnalogCfg{}; bool _analogCfgValid=false;
	Delays _lastDelays{};          bool _delaysValid=false;
	TemperaturesEx _lastTempsEx{}; bool _tempsExValid=false;
	SystemStatus _lastStatusSys{}; bool _statusSysValid=false;
	Faults _lastFaults{};          bool _faultsValid=false;
	Hours _lastHours{};            bool _hoursValid=false;
	BypassCtrl _lastBypass{};      bool _bypassValid=false;
	PreheatStatus _lastPreheat{};  bool _preheatValid=false;
	RfStatus _lastRf{};            bool _rfValid=false;
	PreheatHistory _lastPreheatHist{}; bool _preheatHistValid=false;
	EwtHeating _lastEwt{};         bool _ewtValid=false;

	CcEaseData   _lastCcData{};   bool _ccDataValid=false;
	CcEaseParams _lastCcParams{}; bool _ccParamsValid=false;
	CcEaseKeys   _lastCcKeys{};   bool _ccKeysValid=false;
	CcEaseRfCmd  _lastCcRf{};     bool _ccRfValid=false;
};