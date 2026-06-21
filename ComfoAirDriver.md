# ComfoAirDriver – Dokumentation

## Überblick
`ComfoAirDriver` ist ein Arduino/ESP32-Treiber für die RS232-Schnittstelle der Zehnder ComfoAir. Er bildet Frames, escaped `0x07`, berechnet Checksummen, verarbeitet ACKs und bietet dekodierte Getter/Setter für Protokoll-Sektionen 7.x, 8.x und 9.x.

## Initialisierung
- Hardware: `HardwareSerial` (ESP32 typisch `Serial1`).
- Pins (Default): `RX=16`, `TX=17` (`COMFO_RX_PIN` / `COMFO_TX_PIN` anpassbar).
- Baud: 9600, 8N1.
- Aufruf: `begin(baud=9600, rxPin=COMFO_RX_PIN, txPin=COMFO_TX_PIN)`; setzt `_ready` und loggt den Start.

## Protokoll-Grundlagen
- Start: `07 F0`, Ende: `07 0F`, ACK: `07 F3`.
- Checksumme: (cmd_hi + cmd_lo + len + payload… + 173) & 0xFF.
- Escaping: `0x07` im Payload wird verdoppelt; Empfang entfernt das Duplikat.
- `sendCommand` handhabt Senden, ACK (100 ms) und Antwort (Timeout default 500 ms).

## Hilfsfunktionen (Implementierung)
- `computeChecksum(cmd, data)`: bildet Protokoll-Checksumme.
- `writeFrame(cmd, payload)`: baut Frame (Start, Cmd, Len, Payload mit Escaping, Checksumme, End) und sendet/flush’t.
- `readAck(timeoutMs=100)`: liest 2 Bytes und vergleicht mit ACK-Konstanten; Timeout → false.
- `readFrame(cmdOut, dataOut, timeoutMs)`: liest Frame mit Zustandsautomat, de-escaped `0x07`, prüft Checksumme und End-Bytes; bei Erfolg füllt `cmdOut` und `dataOut`.
- `sendCommand(cmd, payload, respCmd, respPayload, timeoutMs=500)`: leert RX-Puffer, sendet Frame, liest ACK, dann Antwort-Frame; gibt false bei Timeout/Checksum-Fehler/kein ACK.

## Öffentliche API (Auswahl + Werte)

### Versionen / RS232 (7.0)
- `getBootloaderVersion(std::vector<uint8_t>& raw)` – Antwort 0x0068.
- `getFirmwareVersion(std::vector<uint8_t>& raw)` – Antwort 0x006A.
- `getConnectorBoardVersion(std::vector<uint8_t>& raw)` – Antwort 0x00A2.
- Dekodiert: `getBootloaderVersionInfo(ComfoVersionInfo&)`, `getFirmwareVersionInfo(...)`, `getConnectorBoardVersionInfo(ComfoConnectorVersionInfo&)`.
- `setRs232Mode(uint8_t mode, std::vector<uint8_t>& resp)` (0x009B): 0x00=Aus, 0x01=Nur PC, 0x03=PC Master, 0x04=Log.
- `getRs232Mode(uint8_t& mode)` liest Status aus 0x009C (0x00..0x04 wie oben, 0x02=Nur CC-Ease).

### Lesekommandos (7.1, dekodiert)
- `getInputs(ComfoInputs&)` (0x0003): Byte1 Stufenschalter (0x01=L1, 0x02=L2); Byte2 Schalteingänge.
- `getFanStatus(ComfoFanStatus&)` (0x000B): Byte1/2 %; Byte3/4 Supply-RPM; Byte5/6 Exhaust-RPM.
- `getFlapStatus(ComfoFlapStatus&)` (0x000D): Byte1 Bypass%; Byte2 Vorheizung offen/zu/unklar; Byte3/4 Motor-ADC.
- `getTemperatureStatus(ComfoTempStatus&)` (0x000F): T1..T4 als °C* (typ. (T+20)*2).
- `getKeyStatus(uint8_t&)` (0x0011): Byte1 Tastenstatus.
- `getAnalogInputs(ComfoAnalogInputs&)` (0x0013): Byte1-4 A1..A4 (0..255 ≈ 0..10V).
- `getSensorData97(ComfoSensorData97&)` (0x0097): Enthalpie T/H, Analog-% und Sollwerte, Timer, Coeff.
- `getAnalogConfig(ComfoAnalogConfig&)` (0x009D): Präsenz/Regelung/Polarität, Min/Max/Soll A1..A4, RF, Priorität.
- `getDelayTimes(ComfoDelayTimes&)` (0x00C9): Bäder-/Boost-/Filter-/RF-/Hauben-Verzögerungen.
- `getVentilationLevel(ComfoVentilationLevel&)` (0x00CD): % Abwesend/Low/Mid/Aktuell/High + aktueller Level.
- `getTempsD1(ComfoTempsD1&)` (0x00D1): Komforttemp, T1..T4, Sensorflags, EWT, Nachheizung, Haube (°C*).
- ` getAnlagenconf(ComfoAnlagenconf&)` (0x00D5 roh), `getErrorsD9(ComfoErrorD9&)` (0x00D9 roh).
- `getRuntimeDD(ComfoRuntimeDD&)` (0x00DD): 24-Bit Stunden Abwesend/Low/Mid/High, Frost, Vorheizung, Bypass, Filter.
- `getBypassStatusDF(ComfoBypassStatusDF&)` (0x00DF): Faktor, Stage, Korrektur, SommerMode.
- `getPreheatStatusE1(ComfoPreheatStatusE1&)` (0x00E1): Klappe, Frost aktiv, Vorheizung, Frost-Minuten, Safety.
- `getRfStatusE5(ComfoRfStatusE5&)` (0x00E5): RF-Adressen, ID, Modul vorhanden, Selbstlern-Flag.
- `getPreheatHistoryE9(ComfoPreheatHistoryE9&)` (0x00E9): 8 Werte °C Historie.
- `getEwtStatusEB(ComfoEwtStatusEB&)` (0x00EB): EWT Low/High °C, SpeedUps, Heater Power/I, Solltemp.

### Schreibkommandos (7.2)
- `setLevel(uint8_t level)` (0x0099): 0x00=Auto, 0x01=Abwesend, 0x02=Niedrig, 0x03=Mittel, 0x04=Hoch.
- `setAnalogConfig(...)` (0x009F): Flags/Min/Max/Soll A1..A4 + RF + Priorität wie bei `getAnalogConfig`.
- `setDelayTimes(...)` (0x00CB): Badein, Badeaus, L1-Aus, Boost-Min, Filter-Wochen, RF-Short/Long, Haube-Aus.
- `setVentilationLevels(...)` (0x00CF): % für Abwesend/Low/Mid/High (Exhaust/Supply).
- `setComfortTemperatureRaw(uint8_t)` / `setComfortTemperatureC(int8_t)` (0x00D3, Formel (T+20)*2).
- `setStatus(...)` (0x00D7): Präsenzflags Vorheizung/Bypass/Enthalpie/EWT, Typ, Größe, Optionen.
- `resetAndSelftest(...)` (0x00DB): Flags für Fehler/Settings/Filterstunden/Selbsttest.
- `setEwtAndHeaterRaw(...)` / `setEwtAndHeaterC(...)` (0x00ED): EWT Low/High, SpeedUps, Heater-Soll (°C*).

### CC-Ease (8.0/8.1)
- Roh: `easeRequestData(...)` (0x0033), `easeGetData(...)`, `easeSetParams(const std::vector<uint8_t>&)`.
- Komfort: `easeSetParams(const ComfoEaseParams&)` (RTC, TempRaw, Version), `easeSendKeyStatus(const ComfoEaseKeyStatus&)` (Tasten ms + Status), `easeSendRfCommand(const ComfoRfRxCommand&)`.
- Dekoder (ComfoAir → CC-Ease): `parseParams38(...)` (RTC, Backlight), `parseDisplay3C(...)` (Segmente/Symbole b1..b10).

### Manueller Modus(9.0)
- `startTestMode()` (0x0001→0x0002), `endTestMode()` (0x0019→0x001A).
- `setTestOutputs(...)` (0x0005): relaisBits Bit0=Vorh.-Relais, Bit1=Vorh.-Triac, Bit2=EWT-Versorgung, Bit3=EWT-Richtung, Bit4=Haube, Bit5=Fehler; feedbackBits Bit0=Filter-LED.
- `setTestAnalog(...)` (0x0007): Supply/Exhaust/Postheater in %.
- `setTestFlaps(...)` (0x0009): bypass/preheater 0=zu,1=auf,3=Stop.

## Datenstrukturen (Kurz)
- Versionen: `ComfoVersionInfo` (Maj/Min/Beta/Name), `ComfoConnectorVersionInfo` (+CC-Ease/Luxe Maj/Min).
- IO/Status: `ComfoInputs`, `ComfoFanStatus`, `ComfoFlapStatus`, `ComfoTempStatus`, `ComfoAnalogInputs`, `ComfoSensorData97`, `ComfoAnalogConfig`, `ComfoDelayTimes`, `ComfoVentilationLevel`, `ComfoTempsD1`.
- Diagnose: `ComfoAnlagenconf` (roh), `ComfoErrorD9` (roh), `ComfoRuntimeDD`, `ComfoBypassStatusDF`, `ComfoPreheatStatusE1`, `ComfoRfStatusE5`, `ComfoPreheatHistoryE9`, `ComfoEwtStatusEB`.
- CC-Ease: `ComfoEaseParams`, `ComfoEaseKeyStatus`, `ComfoRfRxCommand`, `ComfoParams38`, `ComfoDisplayState`.

## Leseablauf / wann werden Daten gelesen
- Alle Getter rufen intern `sendCommand` auf.
- Ablauf:
  1) `writeFrame` sendet das Kommando (Escaping + Checksumme).
  2) `readAck` wartet bis zu 100 ms auf ACK (`07 F3`), sonst `false`.
  3) `readFrame` wartet bis `timeoutMs` (Default 500 ms) auf Antwort:
     - Start `07 F0`, dann Command, Length, Payload (ent-escaped), Checksumme, End `07 0F`.
     - Checksumme wird geprüft, Fehler → `false`.
- Vor dem Senden leert `sendCommand` den RX-Puffer (`while available()`), um alte Bytes zu verwerfen.
- Keine Antwort/Checksum-Fehler → Getter gibt `false` zurück.
