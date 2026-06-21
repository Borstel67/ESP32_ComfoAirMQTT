# ComfoAirMQTTESP

`ComfoAirMQTTESP` ist ein ESP32-basiertes Gateway für eine **Zehnder ComfoAir 350** Lüftungsanlage.  
Das Projekt verbindet die ComfoAir über RS232 mit einem ESP32, liest Betriebsdaten aus, stellt ein Webinterface bereit und veröffentlicht Statuswerte sowie Steuerfunktionen über MQTT.

Ziel ist die einfache Integration der Lüftungsanlage in Systeme wie **Home Assistant**, **Node-RED**, eigene MQTT-Anwendungen oder andere Smart-Home-Plattformen.

---

## Überblick

Der ESP32 kommuniziert über einen RS232-TTL-Wandler mit der ComfoAir 350.  
Die ausgelesenen Daten werden intern dekodiert, über das Webinterface angezeigt und per MQTT veröffentlicht.

Typischer Aufbau:

    Zehnder ComfoAir 350
            │
            │ RS232
            │
      RS232-TTL-Wandler
            │
            │ UART
            │
          ESP32
       ┌────┴─────┐
       │          │
    Webinterface  MQTT
       │          │
    Browser   Home Assistant / Node-RED / MQTT-Broker

---

## Hauptfunktionen

- RS232-Kommunikation mit der Zehnder ComfoAir 350
- MQTT-Anbindung mit JSON-Payloads
- Webinterface für Statusanzeige und Steuerung
- Captive Portal zur WLAN-Ersteinrichtung
- OTA-Updates über Webinterface
- Speicherung der Konfiguration im ESP32-NVS
- Change-Only MQTT Publishing
- Manueller Modus für Diagnose und direkte Aktorsteuerung
- Auto-Stop für manuelle Klappensteuerung
- Logausgabe über seriellen Monitor und Webinterface

---

## Unterstützte ComfoAir-Funktionen

Das Projekt liest und verarbeitet unter anderem:

- Lüfterstatus
- aktuelle Lüftungsstufe
- Zuluft- und Abluft-Prozentwerte
- Zuluft- und Abluft-Drehzahl
- Temperaturen T1 bis T4
- Komforttemperatur
- Bypass-Status
- Sommermodus
- Vorheizung
- EWT-Status
- Anlagenkonfiguration
- Laufzeiten
- Fehlerstatus
- Analogkonfiguration
- Klappenstatus
- RS232-Modus

Schreibfunktionen:

- Lüftungsstufe setzen
- Komforttemperatur setzen
- Anlagenkonfiguration setzen
- Ventilationsstufen setzen
- Manuellen Modus starten/beenden
- Klappen im manuellen Modus setzen
- Analogausgänge im manuellen Modus setzen

---

## Hardware

| Komponente | Beschreibung |
|---|---|
| ESP32 | Hauptcontroller mit WLAN |
| RS232-TTL-Wandler | z.B. MAX3232 |
| Zehnder ComfoAir 350 | Lüftungsanlage |
| MQTT-Broker | z.B. Mosquitto oder Home Assistant MQTT |
| Optional: LED | Statusanzeige |
| Optional: Taster | Öffnen des Captive Portals |

Standard-Pins:

| Funktion | ESP32-Pin |
|---|---|
| ComfoAir RX | GPIO 16 |
| ComfoAir TX | GPIO 17 |
| Status-LED | GPIO 2 |
| Taster | GPIO 0 |

Die RS232-Verbindung darf nicht direkt mit dem ESP32 verbunden werden.  
Es wird ein geeigneter Pegelwandler wie ein MAX3232 benötigt.

---

## Software-Architektur

| Datei / Ordner | Aufgabe |
|---|---|
| `ComfoAirMQTTESP.ino` | Hauptsketch, Setup, Loop, WiFi-Start, MQTT-Initialisierung |
| `src/ComfoAirDriver.h/.cpp` | RS232-Protokoll, ComfoAir-Kommandos, MQTT-Publishing |
| `src/ESPWiFiManagerAP.h/.cpp` | Access Point, Captive Portal, Provisioning |
| `src/ESPWiFiManagerSTA.h/.cpp` | Webserver im verbundenen WLAN, HTTP-API |
| `src/ESPWiFiManagerCommon.h/.cpp` | Gemeinsame Funktionen für NVS, OTA, NTP, IP, Config |
| `src/AWM_Logging.h` | Logging-Makros |
| `src/LogBuffer.h/.cpp` | Ringpuffer für Logausgabe |
| `data/` | HTML, CSS und Webressourcen für LittleFS |
| `homeassistant/` | Beispiel-/Platzhalterdateien für Home Assistant |
| `config/` | Beispielkonfigurationen |

---

## Webinterface

Das Webinterface wird direkt vom ESP32 ausgeliefert.

Wichtige Seiten:

| Pfad | Beschreibung |
|---|---|
| `/` | Startseite |
| `/ComfoAirDriver.html` | Live-Daten und Schnellsteuerung |
| `/ComfoAirSettings.html` | Erweiterte ComfoAir-Einstellungen |
| `/MQTTSettings.html` | MQTT-Konfiguration |
| `/log.html` | Logausgabe |
| `/update` | OTA-Update über ElegantOTA |

### `ComfoAirDriver.html`

Diese Seite zeigt Live-Daten und bietet direkte Steuerung:

- aktuelle Lüfterstufe
- aktuelle Zuluft-/Abluftwerte
- Temperaturen T1 bis T4
- Bypass-Status
- Sommermodus
- manueller Modus Ein/Aus
- Bypass-Anzeige
- Komforttemperatur setzen
- Lüfterstufe setzen

### `ComfoAirSettings.html`

Diese Seite enthält erweiterte Einstellungen:

- Anlagenkonfiguration
- Ventilationsstufen
- Komforttemperatur
- manueller Modus
- Bypass-/Vorheizungs-Klappensteuerung
- Auto-Stop-Zeit
- manuelle Analogausgänge
- Neustart des Controllers

---

## Manueller Modus

Der manuelle Modus erlaubt direkte Steuerung einzelner Aktoren der ComfoAir.

Unterstützte Funktionen:

| Funktion | Beschreibung |
|---|---|
| Manuellen Modus starten | Aktiviert direkte Steuerung |
| Manuellen Modus beenden | Beendet direkte Steuerung |
| Bypass setzen | `0=Zu`, `1=Offen`, `3=Stop` |
| Vorheizung setzen | `0=Zu`, `1=Offen`, `3=Stop` |
| Zuluft setzen | Analogausgang 0–100 % |
| Abluft setzen | Analogausgang 0–100 % |
| Nachheizung setzen | Analogausgang 0–100 % |

### Auto-Stop

Für Klappenbefehle kann eine Auto-Stop-Zeit gesetzt werden.

| Wert | Verhalten |
|---|---|
| `delaySeconds > 0` | Klappen werden nach Ablauf automatisch auf `Stop` gesetzt |
| `delaySeconds = 0` | Kein automatischer Stop; Stop erfolgt manuell |

Projektregel:  
Wenn die Auto-Stop-Zeit `0` ist, erfolgt kein automatischer Stop.

---

## MQTT

Das MQTT-Topic-Präfix ist konfigurierbar.  
Standard:

    comfoair

### MQTT-Grundsätze

- Statuswerte werden als JSON veröffentlicht.
- Befehle werden als JSON empfangen.
- MQTT-Veröffentlichung erfolgt Change-Only, also nur bei tatsächlicher Änderung.
- Befehle und Sollwerte werden unter `/bereich` verwendet.
- Status-/Istwerte werden unter Status-Topics veröffentlicht.

---

## MQTT-Befehle

Basis: `${b}` = MQTT-Topic-Präfix, z.B. `comfoair`.

| Topic | Payload | Beschreibung |
|---|---|---|
| `${b}/cmd/level/bereich` | `{"level":0}` bis `{"level":4}` | Lüfterstufe setzen |
| `${b}/cmd/comfort/bereich` | `{"tempC":21}` | Komforttemperatur setzen |
| `${b}/cmd/rs232Mode/bereich` | `{"mode":0}` bis `{"mode":4}` | RS232-Modus setzen |
| `${b}/cmd/manual/bereich` | `{"active":true}` | Manuellen Modus starten |
| `${b}/cmd/manual/bereich` | `{"active":false}` | Manuellen Modus beenden |
| `${b}/cmd/manual/flaps/bereich` | `{"bypass":1,"preheater":3,"delaySeconds":0}` | Klappen setzen |
| `${b}/cmd/manual/analog/bereich` | `{"supply":30,"exhaust":30,"postheater":0}` | Analogausgänge setzen |
| `${b}/cmd/status/bereich` | JSON | Anlagenstatus setzen |
| `${b}/cmd/vent/bereich` | JSON | Ventilationsstufen setzen |
| `${b}/cmd/levelStep/bereich` | `{"action":"up"}` | Lüfterstufe erhöhen |
| `${b}/cmd/levelStep/bereich` | `{"action":"down"}` | Lüfterstufe verringern |
| `${b}/cmd/levelStep/bereich` | `{"delta":1}` | Lüfterstufe relativ ändern |

### Rückmeldung auf Befehle

Befehlsergebnisse werden veröffentlicht auf:

    ${b}/status/command/bereich

Beispiel Erfolg:

    {"cmd":"level","ok":true}

Beispiel Fehler:

    {"cmd":"level","ok":false,"error":"level 0..4"}

---

## MQTT-Statuswerte

Statuswerte werden retained veröffentlicht und nur bei Änderung gesendet.

| Topic | Beschreibung |
|---|---|
| `${b}/status/system/json` | Online-Status / Last Will |
| `${b}/status/meta` | Allgemeiner Status |
| `${b}/status/fan` | Lüfterwerte |
| `${b}/status/temps` | Temperaturen |
| `${b}/status/vent` | Ventilationswerte |
| `${b}/status/flap` | Klappenstatus |
| `${b}/status/bypass` | Bypassstatus |
| `${b}/status/summer/json` | Sommer-/Wintermodus |
| `${b}/status/manual` | Manueller Modus |
| `${b}/status/manual/json` | Manueller Modus als offizielles JSON-Topic |
| `${b}/status/anlagenconf` | Anlagenkonfiguration |
| `${b}/status/ain` | Analoge Eingänge |
| `${b}/status/preheat` | Vorheizung |
| `${b}/status/delay` | Verzögerungszeiten |
| `${b}/status/analogConfig` | Analogkonfiguration |

### Beispiel: Lüfterstatus

    Topic:
    comfoair/status/fan

    Payload:
    {
      "supplyPct": 35,
      "exhaustPct": 35,
      "supplyRpm": 1230,
      "exhaustRpm": 1180
    }

### Beispiel: Temperaturen

    Topic:
    comfoair/status/temps

    Payload:
    {
      "t1C": 8.5,
      "t2C": 18.0,
      "t3C": 21.5,
      "t4C": 12.0,
      "comfortC": 21
    }

---

## Konfiguration

Die Konfiguration wird im NVS des ESP32 als JSON gespeichert.

Wichtige Felder:

| Feld | Beschreibung |
|---|---|
| `ssid` | WLAN-SSID |
| `password` | WLAN-Passwort |
| `hostname` | Hostname des ESP32 |
| `dhcp` | DHCP aktiv/inaktiv |
| `ip` | Statische IP |
| `gw` | Gateway |
| `sn` | Subnetzmaske |
| `dns1` | Primärer DNS |
| `dns2` | Sekundärer DNS |
| `ntp1` | Primärer NTP-Server |
| `ntp2` | Sekundärer NTP-Server |
| `mqttEnabled` | MQTT aktivieren |
| `mqttHost` | MQTT-Broker |
| `mqttPort` | MQTT-Port |
| `mqttUser` | MQTT-Benutzer |
| `mqttPass` | MQTT-Passwort |
| `mqttClientId` | MQTT-Client-ID |
| `mqttTopicPrefix` | MQTT-Topic-Präfix |
| `testAutoStopSec` | Auto-Stop-Zeit für manuellen Modus |
| `manualAnalogSupply` | letzter manueller Zuluftwert |
| `manualAnalogExhaust` | letzter manueller Abluftwert |
| `manualAnalogPostheater` | letzter manueller Nachheizungswert |

---

## Erste Inbetriebnahme

1. ESP32 flashen.
2. LittleFS-Webdateien aus `data/` hochladen.
3. ESP32 starten.
4. Wenn keine WLAN-Daten vorhanden sind, startet das Captive Portal.
5. WLAN-Zugangsdaten konfigurieren.
6. MQTT-Broker konfigurieren.
7. ESP32 neu starten.
8. Webinterface öffnen.
9. ComfoAir-Livewerte prüfen.
10. MQTT-Topics im Broker kontrollieren.

---

## Build & Flash

### Arduino IDE / Visual Studio mit Arduino-Erweiterung

1. ESP32-Board auswählen.
2. Benötigte Bibliotheken installieren.
3. LittleFS-Dateien aus `data/` hochladen.
4. Sketch `ComfoAirMQTTESP.ino` kompilieren.
5. Firmware flashen.
6. Seriellen Monitor mit `115200` Baud öffnen.

### Benötigte Bibliotheken

- `ArduinoJson`
- `PubSubClient`
- `ElegantOTA`
- `ESPAsyncWebServer`
- `AsyncTCP`
- `LittleFS`
- `DNSServer`
- `ESPmDNS`
- `ArduinoOTA`
- `Preferences`

---

## OTA-Update

OTA ist über das Webinterface erreichbar:

    http://<esp-ip>/update

Im AP-Modus ist OTA ebenfalls verfügbar, wenn das Captive Portal aktiv ist.

---

## Logging

Das Projekt verwendet `AWM_Logging.h`.

Logs werden ausgegeben über:

- seriellen Monitor
- internen Ringpuffer
- Webinterface `log.html`

Log-Level:

| Makro | Bedeutung |
|---|---|
| `AWM_LOGE` | Fehler |
| `AWM_LOGW` | Warnung |
| `AWM_LOGI` | Information |
| `AWM_LOGD` | Debug |
| `AWM_LOGV` | Verbose |

---

## Home Assistant

Das Projekt ist für MQTT-Integration in Home Assistant vorbereitet.

Hinweise:

- Home-Assistant-Dateien im Repository dienen als Beispiel oder Platzhalter.
- Maßgeblich sind die Dateien an der tatsächlichen `/config`-Position in Home Assistant.
- Bei Home-Assistant-MQTT-Entitäten soll `default_entity_id` statt `object_id` verwendet werden.
- Statuswerte werden nur bei Änderung veröffentlicht.
- Befehle/Sollwerte werden über `/bereich` verarbeitet.

---

## Fehlerbehebung

### Keine WLAN-Verbindung

- Captive Portal öffnen.
- SSID und Passwort prüfen.
- Signalstärke prüfen.
- ESP32 neu starten.

### Keine MQTT-Verbindung

- Broker-IP prüfen.
- Port prüfen.
- Benutzer/Passwort prüfen.
- `mqttEnabled` prüfen.
- Logseite öffnen.

### Keine ComfoAir-Daten

- RS232-TTL-Wandler prüfen.
- RX/TX vertauscht prüfen.
- Masseverbindung prüfen.
- Baudrate prüfen: `9600`
- Seriellen Log prüfen.

### Webinterface lädt nicht

- LittleFS-Daten hochladen.
- IP-Adresse prüfen.
- Captive Portal prüfen.
- Browser-Cache leeren.

---

## Sicherheitshinweise

- Arbeiten an Lüftungsanlagen nur durchführen, wenn die elektrischen Anschlüsse bekannt sind.
- RS232 nicht direkt an ESP32-GPIO anschließen.
- Immer Pegelwandler verwenden.
- Bei Unsicherheit Fachpersonal hinzuziehen.

---

## Lizenz

MIT

© 2026 R. Wisser