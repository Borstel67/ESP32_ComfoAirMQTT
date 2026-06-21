#ifndef COMFOAIRDRIVER_H
#define COMFOAIRDRIVER_H
#pragma once
#include <Arduino.h>
#include <vector>
#include <WiFiClient.h>
#include <PubSubClient.h>

#ifndef AWM_LOG_TAG
#define AWM_LOG_TAG "COMFO"
#endif
#include "AWM_Logging.h"

// Protokoll-Konstanten
static constexpr uint8_t CA_START_HI = 0x07;
static constexpr uint8_t CA_START_LO = 0xF0;
static constexpr uint8_t CA_END_HI   = 0x07;
static constexpr uint8_t CA_END_LO   = 0x0F;
static constexpr uint8_t CA_ACK_HI   = 0x07;
static constexpr uint8_t CA_ACK_LO   = 0xF3;

// Standard-Pins für ESP32 (anpassen bei Bedarf)
#ifndef COMFO_RX_PIN
#define COMFO_RX_PIN 16
#endif
#ifndef COMFO_TX_PIN
#define COMFO_TX_PIN 17
#endif

// ----------------------
// Strukturen zuerst
// ----------------------
struct ComfoVersionInfo { uint8_t versionMajor=0, versionMinor=0, beta=0; char deviceName[11]{}; uint8_t ccEaseMajor=0, ccEaseMinor=0; uint8_t ccLuxeMajor=0, ccLuxeMinor=0; };
struct ComfoConnectorVersionInfo { uint8_t versionMajor=0, versionMinor=0; char deviceName[11]{}; uint8_t ccEaseMajor=0, ccEaseMinor=0; uint8_t ccLuxeMajor=0, ccLuxeMinor=0; };
struct ComfoInputs { uint8_t stageSwitch=0; uint8_t switches=0; };
struct ComfoFanStatus { uint8_t supplyPercent=0, exhaustPercent=0; uint16_t supplyRpm=0, exhaustRpm=0; };
struct ComfoFlapStatus {
    uint8_t bypassPercent=0;   // Byte[1]: Bypass (%) (0xFF = undefiniert)
    uint8_t preheaterState=0;  // Byte[2]: Vorheizung (1=Offen / 0=Zu / 2=Unbekannt)
    uint8_t bypassAdc=0;       // Byte[3]: Bypass Motorstrom (ADC Rohdaten)
    uint8_t preheaterAdc=0;    // Byte[4]: Vorheizung Motorstrom (ADC Rohdaten)
};
struct ComfoTempStatus { int8_t t1Outside=0, t2Supply=0, t3Exhaust=0, t4Outlet=0; };
struct ComfoAnalogInputs { uint8_t a1=0,a2=0,a3=0,a4=0; };
struct ComfoSensorData97 {
    int8_t enthalpyTemp=0; uint8_t enthalpyHumidity=0;
    uint8_t analog1Percent=0, analog2Percent=0, enthalpyCoeff=0, enthalpyTimer=0, reserved=0;
    uint8_t a1UpDesired=0, a1DownDesired=0, a2UpDesired=0, a2DownDesired=0;
    uint8_t analog3Percent=0, analog4Percent=0, a3UpDesired=0, a3DownDesired=0, a4UpDesired=0, a4DownDesired=0;
};
struct ComfoAnalogConfig { uint8_t presentFlags=0, controlFlags=0, polarityFlags=0; uint8_t a1Min=0,a1Max=0,a1Setpoint=0,a2Min=0,a2Max=0,a2Setpoint=0,a3Min=0,a3Max=0,a3Setpoint=0,a4Min=0,a4Max=0,a4Setpoint=0,rfMin=0,rfMax=0,rfSetpoint=0,priority=0; };
struct ComfoDelayTimes { uint8_t bathOnDelay=0,bathOffDelay=0,l1OffDelay=0,boostDuration=0,filterWeeks=0,rfHighShort=0,rfHighLong=0,hoodOffDelay=0; };
struct ComfoVentilationLevel { uint8_t exhaustAbsent=0, exhaustLow=0, exhaustMedium=0, supplyAbsent=0, supplyLow=0, supplyMedium=0, exhaustCurrent=0, supplyCurrent=0, currentLevel=0, supplyFanActive=0, exhaustHigh=0, supplyHigh=0; };
struct ComfoTempsD1 { int8_t comfortTemp=0,t1Outside=0,t2Supply=0,t3Exhaust=0,t4Outlet=0; uint8_t sensorPresent=0; int8_t tEwt=0,tPostHeater=0,tHood=0; };
struct ComfoAnlagenconf {
    uint8_t preheaterPresent = 0; // Byte[1]
    uint8_t bypassPresent    = 0; // Byte[2]
    uint8_t type             = 0; // Byte[3]
    uint8_t size             = 0; // Byte[4]
    uint8_t options          = 0; // Byte[5]
    uint8_t reserved6        = 0; // Byte[6]
    uint8_t activeStatus1    = 0; // Byte[7]
    uint8_t activeStatus2    = 0; // Byte[8]
    uint8_t activeStatus3    = 0; // Byte[9]
    uint8_t enthalpyPresent  = 0; // Byte[10]
    uint8_t ewtPresent       = 0; // Byte[11]

    std::vector<uint8_t> raw;
};
struct ComfoErrorD9 { std::vector<uint8_t> raw; };
struct ComfoRuntimeDD { uint32_t hoursAbsent=0,hoursLow=0,hoursMedium=0,hoursHigh=0; uint16_t hoursFrost=0,hoursPreheater=0,hoursBypassOpen=0,hoursFilter=0; };
struct ComfoBypassStatusDF {
    uint8_t reserved1=0;   // Byte[1]  = 0x00
    uint8_t reserved2=0;   // Byte[2]  = 0x00
    uint8_t factor=0;      // Byte[3]  = Bypass Faktor
    uint8_t stage=0;       // Byte[4]  = Bypass Stufe
    uint8_t correction=0;  // Byte[5]  = Bypass Korrektur
    uint8_t reserved3=0;   // Byte[6]  = 0x00
    uint8_t summerMode=0;  // Byte[7]  = 1 Sommer / 0 Winter
};
struct ComfoPreheatStatusE1 { uint8_t flapStatus=0,frostActive=0,preheatOn=0; uint16_t frostMins=0; uint8_t frostSafety=0; };
struct ComfoRfStatusE5 { uint16_t rfAddr4=0; uint8_t rfAddr3=0, rfAddr2=0; uint16_t rfAddr1=0; uint8_t rfId=0, modulePresent=0, selfLearning=0; };
struct ComfoPreheatHistoryE9 { int8_t values[8] = {0}; };
struct ComfoEwtStatusEB { int8_t ewtLow=0, ewtHigh=0; uint8_t ewtSpeedUp=0, hoodSpeedUp=0, heaterPower=0, heaterI=0; int8_t heaterTSet=0; };
struct ComfoEaseParams { uint8_t rtcDay=0, rtcHour=0, rtcMinute=0, tempRaw=0, versionEase=0; };
struct ComfoEaseKeyStatus { uint8_t ventMs=0, modeMs=0, clockMs=0, tempMs=0, plusMs=0, minusMs=0, status=0; };
struct ComfoRfRxCommand { uint8_t receiverType=0, receiverId=0, senderType=0, senderId=0, lifetime=0, dataType=0; uint8_t data[10]{}; };
struct ComfoParams38 { uint8_t rtcDay=0, rtcHour=0, rtcMinute=0, backlightTimeout=0, backlightLevel=0; };
struct ComfoDisplayState { uint8_t b1=0,b2=0,b3=0,b4=0,b5=0,b6=0,b7=0,b8=0,b9=0,b10=0; };

// MQTT-Konfiguration
struct ComfoMqttConfig { const char* host; int port; const char* user; const char* pass; const char* clientId; const char* topicPrefix; };

// ----------------------
// Klasse danach
// ----------------------
class ComfoAirDriver {
public:
    explicit ComfoAirDriver(HardwareSerial& port);
    bool begin(uint32_t baud = 9600, int8_t rxPin = COMFO_RX_PIN, int8_t txPin = COMFO_TX_PIN);

    // Poll & MQTT
    bool pollAll(uint32_t minIntervalMs = 5000, bool force = false); // füllt interne Snapshots
    bool mqttInit(WiFiClient& net, const ComfoMqttConfig& cfg);
    void mqttSetEnabled(bool enabled);          // NEU
    bool mqttIsEnabled() const;                 // NEU
    void mqttLoop(); // kümmert sich um Reconnect + Publish zyklisch
    bool mqttApplyConfig(const ComfoMqttConfig& cfg, bool enabled, bool publishNow = true); // NEU

    // 6.1 Checksumme
    static uint8_t computeChecksum(uint16_t command, const std::vector<uint8_t>& data);

    // 7.0 Allgemein
    bool getBootloaderVersion(std::vector<uint8_t>& response);
    bool getFirmwareVersion(std::vector<uint8_t>& response);
    bool getConnectorBoardVersion(std::vector<uint8_t>& response);
    bool setRs232Mode(uint8_t mode, std::vector<uint8_t>& response);

    // 7.0 Allgemein – komfortable, dekodierte Varianten
    bool getBootloaderVersionInfo(ComfoVersionInfo& info);              // 0x0067 → 0x0068
    bool getFirmwareVersionInfo(ComfoVersionInfo& info);                // 0x0069 → 0x006A
    bool getConnectorBoardVersionInfo(ComfoConnectorVersionInfo& info); // 0x00A1 → 0x00A2

    // RS232-Modus lesen (aus Antwort auf 0x009B/0x009C)
    bool getRs232Mode(uint8_t& mode);  // 0x00 = ohne Verbindung, 0x01=Nur PC, 0x02=Nur CC-Ease, 0x03=PC Master, 0x04=PC Logmodus

    // 7.1 Lesekommandos
    bool getInputs(ComfoInputs& out);                     // 0x0003
    bool getFanStatus(ComfoFanStatus& out);               // 0x000B
    bool getFlapStatus(ComfoFlapStatus& out);             // 0x000D
    bool getTemperatureStatus(ComfoTempStatus& out);      // 0x000F
    bool getKeyStatus(uint8_t& keyStatus);                // 0x0011
    bool getAnalogInputs(ComfoAnalogInputs& out);         // 0x0013
    bool getSensorData97(ComfoSensorData97& out);         // 0x0097
    bool getAnalogConfig(ComfoAnalogConfig& out);         // 0x009D
    bool getDelayTimes(ComfoDelayTimes& out);             // 0x00C9
    bool getVentilationLevel(ComfoVentilationLevel& out); // 0x00CD
    bool getTempsD1(ComfoTempsD1& out);                   // 0x00D1
    bool  getAnlagenconf(ComfoAnlagenconf& out);                 // 0x00D5
    bool getErrorsD9(ComfoErrorD9& out);                  // 0x00D9
    bool getRuntimeDD(ComfoRuntimeDD& out);               // 0x00DD
    bool getBypassStatusDF(ComfoBypassStatusDF& out);     // 0x00DF
    bool getPreheatStatusE1(ComfoPreheatStatusE1& out);   // 0x00E1
    bool getRfStatusE5(ComfoRfStatusE5& out);             // 0x00E5
    bool getPreheatHistoryE9(ComfoPreheatHistoryE9& out); // 0x00E9
    bool getEwtStatusEB(ComfoEwtStatusEB& out);           // 0x00EB

    // 8.0 CC-Ease an ComfoAir
    bool easeRequestData(uint16_t& responseCommand,
                         std::vector<uint8_t>& responsePayload);   // 0x0033
    bool easeSetParams(const ComfoEaseParams& params);              // 0x0035
    bool easeSendKeyStatus(const ComfoEaseKeyStatus& keys);         // 0x0037
    bool easeSendRfCommand(const ComfoRfRxCommand& cmd);            // 0x0039

    // Alte Roh-Varianten (Kompatibilität)
    bool easeGetData(std::vector<uint8_t>& response);               // 0x0033
    bool easeSetParams(const std::vector<uint8_t>& payload);        // 0x0035/0x0037 per Payload

    // 8.1 – Dekoder für Antworten (ComfoAir -> CC-Ease)
    bool parseParams38(const std::vector<uint8_t>& payload, ComfoParams38& out) const;
    bool parseDisplay3C(const std::vector<uint8_t>& payload, ComfoDisplayState& out) const;

    // 9.0 Manueller Modus
    bool startManualMode(); // 0x0001 -> 0x0002
    bool endManualMode();   // 0x0019 -> 0x001A
    bool isManualModeActive() const { return _manualModeActive; }

    // Kompatibilität (alt)
    bool startTestMode() { return startManualMode(); }
    bool endTestMode()   { return endManualMode(); }
    bool isTestModeActive() const { return isManualModeActive(); }

    // 0x0005 – Ausgänge setzen
    bool setManualOutputs(uint8_t relaisBits, uint8_t feedbackBits);
    bool setTestOutputs(uint8_t relaisBits, uint8_t feedbackBits) { return setManualOutputs(relaisBits, feedbackBits); }

    // 0x0007 – Analogausgänge setzen
    bool setManualAnalog(uint8_t supplyPercent, uint8_t exhaustPercent, uint8_t postheaterPercent);
    bool setTestAnalog(uint8_t supplyPercent, uint8_t exhaustPercent, uint8_t postheaterPercent) { return setManualAnalog(supplyPercent, exhaustPercent, postheaterPercent); }

    // 0x0009 – Klappen setzen
    bool setManualFlaps(uint8_t bypass, uint8_t preheater);
    bool setTestFlaps(uint8_t bypass, uint8_t preheater) { return setManualFlaps(bypass, preheater); }

    // 0x0009 mit Auto-Stop
    bool setManualFlapsWithAutoStop(uint8_t bypass, uint8_t preheater, uint16_t delaySeconds);
    bool setTestFlapsWithAutoStop(uint8_t bypass, uint8_t preheater, uint16_t delaySeconds) { return setManualFlapsWithAutoStop(bypass, preheater, delaySeconds); }

    void serviceManualModeAutoStop();
    void serviceTestModeAutoStop() { serviceManualModeAutoStop(); }

    // Generischer Sender
    bool sendCommand(uint16_t command,
                     const std::vector<uint8_t>& payload,
                     uint16_t& responseCommand,
                     std::vector<uint8_t>& responsePayload,
                     uint32_t timeoutMs = 500);

    // 7.2 Schreibkommandos (PC an ComfoAir)
    bool setLevel(uint8_t level);

    bool setAnalogConfig(uint8_t presentFlags,
                         uint8_t controlFlags,
                         uint8_t polarityFlags,
                         uint8_t a1Min, uint8_t a1Max, uint8_t a1Set,
                         uint8_t a2Min, uint8_t a2Max, uint8_t a2Set,
                         uint8_t a3Min, uint8_t a3Max, uint8_t a3Set,
                         uint8_t a4Min, uint8_t a4Max, uint8_t a4Set,
                         uint8_t rfMin, uint8_t rfMax, uint8_t rfSet,
                         uint8_t priority);

    bool setDelayTimes(uint8_t bathOnDelay,
                       uint8_t bathOffDelay,
                       uint8_t l1OffDelay,
                       uint8_t boostMinutes,
                       uint8_t filterWeeks,
                       uint8_t rfHighShort,
                       uint8_t rfHighLong,
                       uint8_t hoodOffDelay);

    bool setVentilationLevels(uint8_t exhaustAbsent,
                              uint8_t exhaustLow,
                              uint8_t exhaustMedium,
                              uint8_t supplyAbsent,
                              uint8_t supplyLow,
                              uint8_t supplyMedium,
                              uint8_t exhaustHigh,
                              uint8_t supplyHigh);

    bool setComfortTemperatureRaw(uint8_t tempRaw);
    bool setComfortTemperatureC(int8_t physTempC);

    bool setStatus(uint8_t preheaterPresent,
                   uint8_t bypassPresent,
                   uint8_t type,
                   uint8_t size,
                   uint8_t options,
                   uint8_t enthalpyPresent,
                   uint8_t ewtPresent);

    bool resetAndSelftest(uint8_t resetErrors,
                          uint8_t resetSettings,
                          uint8_t startSelftest,
                          uint8_t resetFilterHours);

    bool setEwtAndHeaterRaw(uint8_t ewtLowRaw,
                            uint8_t ewtHighRaw,
                            uint8_t ewtSpeedUp,
                            uint8_t hoodSpeedUp,
                            uint8_t heaterTargetRaw);

    bool setEwtAndHeaterC(int8_t ewtLowC,
                          int8_t ewtHighC,
                          uint8_t ewtSpeedUp,
                          uint8_t hoodSpeedUp,
                          int8_t heaterTargetC);

    uint8_t getManualRequestedBypass() const { return _manualRequestedBypass; }
    uint8_t getManualRequestedPreheater() const { return _manualRequestedPreheater; }
    const char* getManualRequestedBypassText() const;
    const char* getManualRequestedPreheaterText() const;

    bool isManualRequestedBypassKnown() const { return _manualRequestedBypass == 0 || _manualRequestedBypass == 1; }
    bool isManualRequestedBypassOpen() const { return _manualRequestedBypass == 1; }
    bool isManualRequestedBypassClosed() const { return _manualRequestedBypass == 0; }

    bool isManualRequestedPreheaterKnown() const { return _manualRequestedPreheater == 0 || _manualRequestedPreheater == 1; }
    bool isManualRequestedPreheaterOpen() const { return _manualRequestedPreheater == 1; }
    bool isManualRequestedPreheaterClosed() const { return _manualRequestedPreheater == 0; }

    // Auto-Stop-Wert aus config.json in Runtime/MQTT spiegeln
    void setManualAutoStopDelaySecFromConfig(uint16_t sec);

private:
    HardwareSerial* _serial;
    bool _ready;

    // MQTT intern
    bool mqttEnsureConnected();
    void mqttPublishState();
    static void mqttThunk(char* topic, uint8_t* payload, unsigned int length);
    void onMqttMessage(char* topic, uint8_t* payload, unsigned int length);

    PubSubClient _mqtt;
    WiFiClient*  _net{nullptr};
    ComfoMqttConfig _mqttCfg{};
    uint32_t _lastMqttReconnect{0};
    uint32_t _lastMqttPublish{0};
    bool _mqttEnabled{false};

    // NEU: owning storage für MQTT-Strings
    String _mqttHostS;
    String _mqttUserS;
    String _mqttPassS;
    String _mqttClientIdS;
    String _mqttTopicPrefixS;

    void applyMqttCfgOwned(const ComfoMqttConfig& cfg); // NEU

    // NEU: Change-Only Cache, wird bei Reconnect zurückgesetzt
    std::vector<std::pair<String, String>> _mqttLastByTopic;

    // Snapshots
    ComfoFanStatus        _fan{};
    ComfoTempStatus       _temps{};
    ComfoAnalogInputs     _ain{};
    ComfoVentilationLevel _vent{};
    ComfoInputs           _inputs{};
    ComfoSensorData97     _sens97{};
    ComfoAnlagenconf         _anlagenconf{};
    ComfoEwtStatusEB      _ewt{};
    ComfoFlapStatus       _flap{};
    ComfoBypassStatusDF   _bypass{};
    ComfoPreheatStatusE1  _preheat{};
    ComfoRuntimeDD        _runtime{};
    ComfoErrorD9          _errors{};
    ComfoTempsD1          _tempsD1{};   // NEU: für Komforttemperatur/Temp-Dekodierung in MQTT
    uint8_t               _rsMode{0};
    bool                  _stateOk{false};

    // Best-effort Validitätsflags pro Bereich
    bool _fanValid{false};
    bool _tempsValid{false};
    bool _ventValid{false};
    bool _flapValid{false};
    bool _bypassValid{false};
    bool _anlagenconfValid{false};
    bool _ainValid{false};
    bool _preheatValid{false};
    bool _analogCfgValid{false};
    bool _delayValid{false};
    bool _keyStatusValid{false};
    bool _tempsD1Valid{false};
    bool _rsModeValid{false};

    uint32_t _lastPoll{0};

    static ComfoAirDriver* _cbInstance;

    bool readAck(uint32_t timeoutMs);
    bool readFrame(uint16_t& command, std::vector<uint8_t>& data, uint32_t timeoutMs);
    void writeFrame(uint16_t command, const std::vector<uint8_t>& payload);

    // Manueller ModusAuto-Stop (Klappen)
    bool     _testFlapsAutoStopArmed{false};
    uint32_t _testFlapsAutoStopDueMs{0};
    uint8_t  _testFlapsAutoStopBypass{3};
    uint8_t  _testFlapsAutoStopPreheater{3};

    // Manueller ModusKlappen-Verriegelung (1=Auf verriegelt, 0=Zu verriegelt, 3=unverriegelt/Stop)
    uint8_t  _testFlapsLatchBypass{3};
    uint8_t  _testFlapsLatchPreheater{3};

    // Testmodus-Status (UI/API)
    bool _manualModeActive{false}; // NEU

    // NEU: zuletzt gesetzte Manual-Sollwerte für MQTT
    uint8_t  _manualAnalogSupplyCmd{0};
    uint8_t  _manualAnalogExhaustCmd{0};
    uint8_t  _manualAnalogPostheaterCmd{0};
    uint16_t _manualFlapsLastDelaySec{0};

    ComfoAnalogConfig     _analogCfg{};
    ComfoDelayTimes       _delay{};
    uint8_t               _keyStatus{0};

    static const char* flapReqToText(uint8_t v);

    // Persistente Anzeige der letzten expliziten Anforderung:
    // 0=zu, 1=offen, 3=unbekannt/nicht gesetzt.
    // Wird NUR bei neuem 0/1-Befehl geändert.
    uint8_t _manualRequestedBypass{3};
    uint8_t _manualRequestedPreheater{3};

    int8_t _comfortTempCmdC{23};
    bool   _comfortTempCmdValid{false};
};

#endif // COMFOAIRDRIVER_H