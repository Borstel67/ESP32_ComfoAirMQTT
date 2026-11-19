// SPDX-License-Identifier: MIT
// AP-/Captive-Portal-Manager:
// - SoftAP + DNS-Catch-All + HTTP-Routen für Provisioning
// - Fallback-Strategien und Auto-Reconnect
// - Button-/LED-Handling, WiFi-Scan, OTA (AsyncElegantOTA), mDNS
#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#define AWM_VERSION        "2.0.3"
#define AWM_VERSION_MAJOR  2
#define AWM_VERSION_MINOR  0
#define AWM_VERSION_PATCH  3

#include <Arduino.h>

#if defined(ESP32)
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#else
#error "Plattform nicht unterstützt (nur ESP32)"
#endif

#include <FS.h>
#include <LittleFS.h>
#include <DNSServer.h>
#include "ComfoAirDriver.h" // falls noch nicht drin

class ESPWiFiManagerAP {
public:
	enum class FallbackPolicy : uint8_t { ON_FAIL, NO_CREDENTIALS_ONLY, SMART_RETRIES, BUTTON_ONLY, NEVER };
	enum class LedPattern : uint8_t { OFF, ON, BLINK_SLOW, BLINK_FAST, BLINK_DOUBLE, BLINK_TRIPLE };

	ESPWiFiManagerAP(uint8_t ledPin = 2, uint8_t buttonPin = 0);

	void begin();
	void run();
	void update();

	void setHtmlPathPrefix(const String& prefix);
	void setHostname(const String& host);
	void setAPCredentials(const String& ssid, const String& pass);
	void setCaptivePortal(bool enabled);
	void setPortalTimeout(uint32_t seconds);
	void setAPClientCheck(bool enabled);
	void setWebClientCheck(bool enabled);
	void openPortal();
	void closePortal();
	bool isPortalActive() const;

	void setFallbackPolicy(FallbackPolicy p);
	void setSmartRetries(uint8_t maxRetries, uint32_t windowMs);
	void enableButtonPortal(bool enable);
	void setAutoReconnect(bool habilitado);

	bool isConnected();
	bool connectToWiFiSTA();
	bool tieneCredenciales() const;

	void setLedAuto(bool enable);
	void setLedPatternManual(LedPattern p);

	void setReconnectBackoffMs(uint32_t ms);
	void setReconnectAttemptMs(uint32_t ms);
	void setExternalApActive(bool active);
	bool isExternalApActive() const;
    bool startMDNS(const String& host);
    bool mdnsStarted = false;

    void setComfoAir(ComfoAirDriver* drv) { comfo = drv; } // NEU

private:
	void setupAP();
	void startAP();
	void stopAP();
	void setupHTTPRoutes();
	void startDNS();
	void stopDNS();
	bool captivePortalRedirect(AsyncWebServerRequest* request);
	bool portalHasTimedOut();
	void redirectToRoot(AsyncWebServerRequest* request);
	uint8_t softAPStationCount();

	void handleRoot(AsyncWebServerRequest* request);
	void handleSettings(AsyncWebServerRequest* request);
	void handleComfoDoc(AsyncWebServerRequest* request);   // NEU
	void handleLogPage(AsyncWebServerRequest* request);    // NEU
	void handleLogs(AsyncWebServerRequest* request);       // NEU
	void handleSave(AsyncWebServerRequest* request);
	void handleScan(AsyncWebServerRequest* request);
	void handleNotFound(AsyncWebServerRequest* request);
	void mostrarPaginaError(const String& mensajeFallback, AsyncWebServerRequest* request);
	void handleCss(AsyncWebServerRequest* request);
	void handleErase(AsyncWebServerRequest* request);
	void handleComfoSettingsDoc(AsyncWebServerRequest* request); // NEU
	void handleMqttSettingsDoc(AsyncWebServerRequest* request);      // NEU

	void loadCredentials();
	void eraseCredentials();

	void ledAutoUpdate();
	void ledTask();
	void ledSet(LedPattern p);

	String ssid, password;
	String htmlPathPrefix = "/";

	AsyncWebServer apServer{ 80 };
	DNSServer dns;
	bool portalActive = false;
	bool routesReady = false;
	bool dnsRunning = false;

	IPAddress apIP{ 192,168,4,1 }, apGW{ 192,168,4,1 }, apSN{ 255,255,255,0 };
	String hostname;
	String apSSID = "WiFi Manager";
	String apPASS = "123456789";

	bool     captiveEnabled = true;
	uint32_t portalTimeoutMs = 0;
	bool     apClientCheck = false;
	bool     webClientCheck = true;
	unsigned long portalStart = 0;
	unsigned long lastHttpAccess = 0;

	FallbackPolicy fallbackPolicy = FallbackPolicy::NO_CREDENTIALS_ONLY;
	bool allowButtonPortal = true;
	uint8_t  maxFailRetries = 3;
	uint32_t failWindowMs = 60000;

	bool connected = false;
	bool autoReconnect = true;

	bool scanning = false;
	unsigned long scanningUntil = 0;

	uint8_t ledPin, buttonPin;

	bool        ledAuto = true;
	LedPattern  ledPat = LedPattern::OFF;
	uint8_t     ledOut = LOW;
	uint8_t     ledStep = 0;
	unsigned long ledT0 = 0;

	uint32_t reconnectBackoffMs = 10000;
	uint32_t reconnectAttemptMs = 5000;

	bool externalApActive = false;

	ComfoAirDriver* comfo = nullptr; // NEU
};

#endif // WIFI_MANAGER_H
