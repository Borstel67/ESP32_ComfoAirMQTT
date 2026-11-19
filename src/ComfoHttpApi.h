// SPDX-License-Identifier: MIT
// Gemeinsame HTTP-API-Handler für AP- und STA-Webserver (/api/comfo/*, /api/mqtt/config).
// Lesende Handler nutzen den Snapshot des Treibers (keine RS232-Last im AsyncTCP-Task),
// schreibende Handler sind über den Treiber-Mutex mit loop() synchronisiert.
#pragma once
#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include "ComfoAirDriver.h"

namespace ComfoHttpApi {
    void state(AsyncWebServerRequest* request, ComfoAirDriver* comfo);
    void setLevel(AsyncWebServerRequest* request, ComfoAirDriver* comfo);
    void setComfort(AsyncWebServerRequest* request, ComfoAirDriver* comfo);
    void setStatus(AsyncWebServerRequest* request, ComfoAirDriver* comfo);
    void setVentilationLevels(AsyncWebServerRequest* request, ComfoAirDriver* comfo);

    void manualStart(AsyncWebServerRequest* request, ComfoAirDriver* comfo);
    void manualFlaps(AsyncWebServerRequest* request, ComfoAirDriver* comfo);
    void manualEnd(AsyncWebServerRequest* request, ComfoAirDriver* comfo);
    void manualAnalog(AsyncWebServerRequest* request, ComfoAirDriver* comfo);
    void manualConfigGet(AsyncWebServerRequest* request);
    void manualConfigSave(AsyncWebServerRequest* request, ComfoAirDriver* comfo);

    void mqttConfigGet(AsyncWebServerRequest* request);
    void mqttConfigSave(AsyncWebServerRequest* request, ComfoAirDriver* comfo);

    // Registriert alle /api/comfo/*- und /api/mqtt/config-Routen auf 'server'.
    // 'comfo' wird als Referenz gehalten: der Zeiger darf auch später gesetzt werden,
    // muss aber so lange leben wie der Server.
    void registerRoutes(AsyncWebServer& server, ComfoAirDriver*& comfo);
}
