// SPDX-License-Identifier: MIT
// Webserver für den verbundenen STA-Modus mit UI-, API-, DNS- und OTA-Unterstützung.
#ifndef ESP_WIFI_MANAGER_STA_H
#define ESP_WIFI_MANAGER_STA_H

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
#include "ComfoAirDriver.h"

class ESPWiFiManagerSTA {
public:
	ESPWiFiManagerSTA();

	// Startet den STA-Webserver auf dem angegebenen Port.
	bool startSTA(uint16_t port = 80);

	// Beendet den STA-Webserver.
	void stopSTA();

	// Bearbeitet anstehende Client- und DNS-Anfragen.
	void handleClient();

	// Setzt das Präfix für statische HTML-Ressourcen.
	void setHtmlPathPrefix(const String& prefix);

	// Setzt den Hostnamen des Geräts.
	void setHostname(const String& host);

	// Aktiviert oder deaktiviert die DHCP-Konfiguration.
	void enableDhcp(bool enabled);

	// Setzt die statische Netzwerkkonfiguration.
	void setStaticIP(const IPAddress& ip, const IPAddress& gateway, const IPAddress& subnet,
		const IPAddress& dns1 = IPAddress(0, 0, 0, 0), const IPAddress& dns2 = IPAddress(0, 0, 0, 0));

	// Verknüpft den ComfoAir-Treiber mit dem Webserver.
	void setComfoAir(ComfoAirDriver* drv) { comfo = drv; }

	// Gibt zurück, ob der Webserver aktuell läuft.
	bool isRunning() const { return serverRunning; }

	// Gibt zurück, ob der DNS-Server aktuell läuft.
	bool isDnsRunning() const { return dnsRunning; }

	// Gibt den konfigurierten Serverport zurück.
	uint16_t getPort() const { return serverPort; }

private:
	// Registriert alle HTTP-Routen des Webservers.
	void setupHTTPRoutes();

	// Liefert die Startseite aus.
	void handleRoot(AsyncWebServerRequest* request);

	// Liefert die Einstellungsseite aus.
	void handleSettings(AsyncWebServerRequest* request);

	// Liefert die ComfoAir-Dokumentationsseite aus.
	void handleComfoDoc(AsyncWebServerRequest* request);

	// Liefert die Log-Seite aus.
	void handleLogPage(AsyncWebServerRequest* request);

	// Liefert die Logdaten aus.
	void handleLogs(AsyncWebServerRequest* request);

	// Liefert das Stylesheet aus.
	void handleCss(AsyncWebServerRequest* request);

	// Behandelt unbekannte HTTP-Anfragen.
	void handleNotFound(AsyncWebServerRequest* request);

	// Liefert die aktuelle STA-Konfiguration aus.
	void handleStaConfigGet(AsyncWebServerRequest* request);

	// Speichert die STA-Konfiguration.
	void handleStaConfigSave(AsyncWebServerRequest* request);

	// Speichert die STA-Konfiguration als JSON-Datei.
	bool saveStaConfigToJson(bool useDhcp,
		const String& ip,
		const String& gw,
		const String& sn,
		const String& dns1,
		const String& dns2,
		const String& host,
		const String& ntp1,
		const String& ntp2);

	// Löscht gespeicherte Konfigurationsdaten.
	void handleErase(AsyncWebServerRequest* request);

	// Liefert die Dokumentation der ComfoAir-Einstellungen aus.
	void handleComfoSettingsDoc(AsyncWebServerRequest* request);

	// Liefert die Dokumentation der MQTT-Einstellungen aus.
	void handleMqttSettingsDoc(AsyncWebServerRequest* request);

	// Startet das Gerät neu.
	void handleReboot(AsyncWebServerRequest* request);

	// Startet den DNS-Server.
	void startDNS();

	// Beendet den DNS-Server.
	void stopDNS();

	// Führt bei Bedarf eine Captive-Portal-Umleitung aus.
	bool captivePortalRedirect(AsyncWebServerRequest* request);

	// Leitet auf die Startseite um.
	void redirectToRoot(AsyncWebServerRequest* request);

	// Lädt gespeicherte Zugangsdaten und Konfigurationswerte.
	void loadCredentials();

	// Parst eine IP-Adresse aus einem Textwert.
	bool parseIP(const String& text, IPAddress& out);

	AsyncWebServer staServer;
	DNSServer dns;

	String htmlPathPrefix;
	String hostname;
	uint16_t serverPort;
	bool serverRunning;
	bool routesReady = false;
	bool dnsRunning;
	bool captiveEnabled;

	bool dhcpEnabled;
	IPAddress staticIP;
	IPAddress staticGW;
	IPAddress staticSN;
	IPAddress staticDNS1;
	IPAddress staticDNS2;
	String ntp1;
	String ntp2;

	ComfoAirDriver* comfo = nullptr;
};

#endif // ESP_WIFI_MANAGER_STA_H
