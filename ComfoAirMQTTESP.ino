// SPDX-License-Identifier: MIT
// Haupt-Sketch für ComfoAirMQTTESP:
// - Verbindet ESP32 mit WiFi (AP-Provisioning / STA)
// - Bedient den ComfoAir 350 RS232-Treiber
// - Veröffentlicht Gerätestatus über MQTT
// - Stellt HTTP-Webinterface und OTA-Update bereit
#include <HTTPClient.h>
#define AWM_LOG_TAG "MAIN"

#include <Arduino.h>
#include <src/AWM_Logging.h>
#include <src/LogBuffer.h>
#include <src/ESPWiFiManagerAP.h>
#include <src/ESPWiFiManagerSTA.h>
#include <src/ESPWiFiManagerCommon.h>
#include <WiFiClient.h>
#include "src/ComfoAirDriver.h"
#include <ArduinoJson.h>

// ── Hardware-Konfiguration ────────────────────────────────────────────────────
// RS232-TTL-Adapter zur ComfoAir (Serial2 des ESP32)
#define COMFO_RX_PIN 16  // ESP32 RX2
#define COMFO_TX_PIN 17  // ESP32 TX2

// ── Manager-Instanzen ─────────────────────────────────────────────────────────
ESPWiFiManagerAP  wifiManagerAP;   // AP-Modus + Captive-Portal
ESPWiFiManagerSTA wifiManagerSTA;  // STA-Modus + HTTP-API
ComfoAirDriver    comfoAir(Serial2); // ComfoAir RS232-Treiber

// Abstand zwischen zwei pollAll()-Aufrufen in Millisekunden.
const uint32_t COMFO_POLL_MS = 5000;

#ifndef LED_PIN
#define LED_PIN 2   // Status-LED (LOW=aus, HIGH=an)
#endif

#ifndef BTN_PIN
#define BTN_PIN 0   // Provisioning-Taster (LOW-aktiv); -1 zum Deaktivieren
#endif

// ── NTP / Zeitzone ────────────────────────────────────────────────────────────
static const uint32_t SHORT_PRESS_MS      = 700;
static const char*    NTP1                = "pool.ntp.org";
static const char*    NTP2                = "time.google.com";
static const long     GMT_OFFSET_SEC      = 0;
static const int      DAYLIGHT_OFFSET_SEC = 0;
static const uint16_t OTA_PORT            = 3232;
static const char*    DEVICE_HOSTNAME     = "ESPDevice";

// ── Laufzustand ───────────────────────────────────────────────────────────────
bool portalActive = false; // true wenn Captive-Portal aktiv
bool staStarted   = false; // true wenn STA-HTTP-Server gestartet

// ── LED-Zustandsautomat (Sketch-eigene LED) ───────────────────────────────────
enum LedMode { LED_OFF, LED_ON, LED_BLINK_SLOW, LED_BLINK_FAST };
LedMode   ledMode   = LED_BLINK_SLOW;
uint32_t  ledTicker = 0;
bool      ledState  = false;

// ── Taster-Zustand ────────────────────────────────────────────────────────────
#if (BTN_PIN >= 0)
bool     btnPrev      = true;  // vorheriger Taster-Pegel
uint32_t btnPressedAt = 0;     // Zeitstempel des letzten Drucks
#endif

// ── MQTT-Standardkonfiguration ────────────────────────────────────────────────
const char* MQTT_HOST      = "192.168.1.10";
const int   MQTT_PORT      = 1883;
const char* MQTT_USER      = "";
const char* MQTT_PASS      = "";
const char* MQTT_CLIENT_ID = "ComfoAirESP32";

// Laufzeitwerte aus config.json (überschreiben die Defaults oben)
static bool   gMqttEnabled     = true;
static String gMqttHost        = MQTT_HOST;
static int    gMqttPort        = MQTT_PORT;
static String gMqttUser        = MQTT_USER;
static String gMqttPass        = MQTT_PASS;
static String gMqttClientId    = MQTT_CLIENT_ID;
static String gMqttTopicPrefix = "comfoair";

// Liest MQTT-Konfiguration aus config.json und überschreibt die Laufzeitwerte.
static void loadMqttConfigFromJson() {
    String json;
    if (!loadConfigJson(json)) return;

    StaticJsonDocument<1024> doc;
    if (deserializeJson(doc, json)) return;

    gMqttEnabled     = doc["mqttEnabled"]     | gMqttEnabled;
    gMqttHost        = doc["mqttHost"]        | gMqttHost.c_str();
    gMqttPort        = doc["mqttPort"]        | gMqttPort;
    gMqttUser        = doc["mqttUser"]        | gMqttUser.c_str();
    gMqttPass        = doc["mqttPass"]        | gMqttPass.c_str();
    gMqttClientId    = doc["mqttClientId"]    | gMqttClientId.c_str();
    gMqttTopicPrefix = doc["mqttTopicPrefix"] | gMqttTopicPrefix.c_str();
}

// TCP-Client-Instanz für den MQTT-Broker.
WiFiClient mqttNet;

// Gibt den aktuellen WiFi-Hostnamen zurück; Fallback auf DEVICE_HOSTNAME.
static String currentOtaHostname() {
    String h = WiFi.getHostname();
    if (!h.length()) h = DEVICE_HOSTNAME;
    return h;
}

// Initialisiert ArduinoOTA falls noch nicht aktiv.
static void ensureOtaReady() {
    ensureArduinoOta(currentOtaHostname(), OTA_PORT, "");
}

// Setzt das Sketch-LED-Muster und den GPIO-Ausgang.
void setLedMode(LedMode m) {
    ledMode = m;
    if (m == LED_ON)  { digitalWrite(LED_PIN, HIGH); ledState = true;  }
    if (m == LED_OFF) { digitalWrite(LED_PIN, LOW);  ledState = false; }
}

// Nicht-blockierende LED-Steuerung; muss in loop() aufgerufen werden.
void updateLed() {
    const uint32_t now = millis();
    switch (ledMode) {
    case LED_ON:
    case LED_OFF:
        break;
    case LED_BLINK_SLOW:
        if (now - ledTicker >= 800) {
            ledTicker = now;
            ledState = !ledState;
            digitalWrite(LED_PIN, ledState ? HIGH : LOW);
        }
        break;
    case LED_BLINK_FAST:
        if (now - ledTicker >= 200) {
            ledTicker = now;
            ledState = !ledState;
            digitalWrite(LED_PIN, ledState ? HIGH : LOW);
        }
        break;
    }
}

// Gibt die aktuelle NTP-Zeit auf der seriellen Konsole aus.
void printTimeIfReady() {
    time_t t = time(nullptr);
    struct tm* tm_info = localtime(&t);
    char buf[32];
    if (tm_info && strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm_info)) {
        AWM_LOGI("Time: %s", buf);
    } else {
        AWM_LOGW("Zeit noch nicht gesetzt.");
    }
}

// Öffnet das Captive-Portal sofort; setzt LED auf Schnellblinken.
void startPortalNow(const char* reason) {
    AWM_LOGI("Starte Captive-Portal (%s)...", reason);
    wifiManagerAP.openPortal();
    portalActive = true;
    setLedMode(LED_BLINK_FAST);
    ensureOtaReady();
}

// Versucht WiFi-STA-Verbindung; öffnet bei Timeout das Captive-Portal.
void connectWiFiSTAOrWiFiAP(uint32_t timeoutMs = 8000) {
    AWM_LOGI("wifiManagerAP begin() + Verbindungsversuch...");
    wifiManagerAP.begin();
    wifiManagerAP.run();

    setLedMode(LED_BLINK_SLOW);

    uint32_t t0 = millis();
    while (!wifiManagerAP.isConnected() && millis() - t0 < timeoutMs) {
        updateLed();
        delay(100);
        yield();
    }

    if (wifiManagerAP.isConnected()) {
        setLedMode(LED_ON);
        AWM_LOGI("WiFi verbunden. IP: %s", WiFi.localIP().toString().c_str());

        configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP1, NTP2);
        delay(800);
        printTimeIfReady();
        portalActive = false;

        wifiManagerSTA.setHostname(DEVICE_HOSTNAME);
        AWM_LOGI("Starte WiFi-STA-Server...");
        staStarted = wifiManagerSTA.startSTA();
        AWM_LOGI("%s", staStarted ? "STA-Server läuft." : "STA-Server-Start fehlgeschlagen.");
        ensureArduinoOta(DEVICE_HOSTNAME, OTA_PORT, "");
    } else {
        AWM_LOGW("Keine WiFi-Verbindung möglich. Öffne Portal...");
        staStarted = false;
        startPortalNow("connect timeout");
    }
}

// ── Arduino Setup ─────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(200);

    // Config aus NVS laden und im Log ausgeben.
    String dbg;
    if (loadConfigJson(dbg)) {
        AWM_LOGI("config.json aus NVS: %s", dbg.c_str());
    } else {
        AWM_LOGW("config.json nicht im NVS gefunden.");
    }

    // ComfoAir-Treiber mit beiden Managern verknüpfen.
    wifiManagerAP.setComfoAir(&comfoAir);
    wifiManagerSTA.setComfoAir(&comfoAir);

    wifiManagerAP.setHostname(DEVICE_HOSTNAME);
    wifiManagerSTA.setHostname(DEVICE_HOSTNAME);

    // RS232-TTL zur ComfoAir initialisieren (9600 Baud, 8N1).
    comfoAir.begin(9600, COMFO_RX_PIN, COMFO_TX_PIN);

    // MQTT-Konfiguration aus config.json laden und anwenden.
    loadMqttConfigFromJson();

    ComfoMqttConfig cfg{
        gMqttHost.c_str(),
        gMqttPort,
        gMqttUser.c_str(),
        gMqttPass.c_str(),
        gMqttClientId.c_str(),
        gMqttTopicPrefix.c_str()
    };
    comfoAir.mqttInit(mqttNet, cfg);
    comfoAir.mqttSetEnabled(gMqttEnabled);

    // WiFi-Verbindung herstellen oder Portal öffnen.
    connectWiFiSTAOrWiFiAP(8000);
}

// ── Arduino Loop ──────────────────────────────────────────────────────────────
void loop() {
    // AP-Manager: Portal/Verbindungsverwaltung, OTA, interne LED.
    wifiManagerAP.update();

    // STA-HTTP-Server: DNS-Anfragen bearbeiten.
    wifiManagerSTA.handleClient();

    // ArduinoOTA-Updates verarbeiten.
    handleArduinoOta();

    // Sketch-eigene LED aktualisieren.
    updateLed();

#if (BTN_PIN >= 0)
    // Kurzer Tastendruck öffnet das Captive-Portal.
    bool btnNow = digitalRead(BTN_PIN);
    if (btnPrev && !btnNow) {
        btnPressedAt = millis();
    } else if (!btnPrev && btnNow) {
        uint32_t dur = millis() - btnPressedAt;
        if (dur >= SHORT_PRESS_MS) {
            Serial.println("Kurzer Druck -> Portal öffnen");
            startPortalNow("short press");
        }
    }
    btnPrev = btnNow;
#endif

    // STA-Server nachstarten, falls WiFi verbunden aber Server noch nicht läuft.
    static uint32_t lastStaRetry = 0;
    if (wifiManagerAP.isConnected() && !wifiManagerSTA.isRunning()
        && millis() - lastStaRetry > 3000) {
        lastStaRetry = millis();
        staStarted = wifiManagerSTA.startSTA();
        ensureOtaReady();
    }

    // Periodischer Reconnect-Versuch wenn weder WiFi noch Portal aktiv.
    static uint32_t lastRetry = 0;
    if (!portalActive && !wifiManagerAP.isConnected()
        && millis() - lastRetry > 15000) {
        lastRetry = millis();
        AWM_LOGW("WiFi nicht verbunden -> erneuter Versuch");
        connectWiFiSTAOrWiFiAP(5000);
    }

    // OTA sicherstellen, solange Netzwerk aktiv.
    if (wifiManagerAP.isConnected() || portalActive) {
        ensureOtaReady();
    }

    // ComfoAir-Status periodisch abfragen (alle COMFO_POLL_MS ms).
    comfoAir.pollAll(COMFO_POLL_MS);

    // MQTT-Verbindung halten und Istwerte veröffentlichen.
    comfoAir.mqttLoop();
}