// SPDX-License-Identifier: MIT
// Logging-Makros für AyresWiFiManager:
//   AWM_LOGE / AWM_LOGW / AWM_LOGI / AWM_LOGD / AWM_LOGV
//
// Konfigurierbar über:
//   AWM_ENABLE_LOG  0=aus / 1=an           (Standard: 1)
//   AWM_LOG_LEVEL   1=E 2=W 3=I 4=D 5=V   (Standard: DEBUG)
//   AWM_LOG_TAG     Präfix-String           (Standard: "AWM")
//
// Ausgabe: seriell + awm_logbuf (Ring-Puffer für HTTP-Logabruf)
#pragma once
#include <Arduino.h>
#include "LogBuffer.h"

#ifndef AWM_ENABLE_LOG
#  define AWM_ENABLE_LOG 1
#endif

#ifndef AWM_LOG_LEVEL
#  define AWM_LOG_LEVEL AWM_L_DEBUG
#endif

#ifndef AWM_LOG_TAG
#  define AWM_LOG_TAG "AWM"
#endif

// Log-Level-Konstanten
#define AWM_L_ERROR   1
#define AWM_L_WARN    2
#define AWM_L_INFO    3
#define AWM_L_DEBUG   4
#define AWM_L_VERBOSE 5

#if AWM_ENABLE_LOG

  // Internes Makro: formatiert einmal, schreibt in Serial und in den Ring-Puffer.
  #define AWM__PRINTF(_name, fmt, ...) \
    awm_logf("[" AWM_LOG_TAG "] " _name ": " fmt "\n", ##__VA_ARGS__)

  #if (AWM_LOG_LEVEL >= AWM_L_ERROR)
    #define AWM_LOGE(fmt, ...) AWM__PRINTF("E", fmt, ##__VA_ARGS__)
  #else
    #define AWM_LOGE(...) do{}while(0)
  #endif

  #if (AWM_LOG_LEVEL >= AWM_L_WARN)
    #define AWM_LOGW(fmt, ...) AWM__PRINTF("W", fmt, ##__VA_ARGS__)
  #else
    #define AWM_LOGW(...) do{}while(0)
  #endif

  #if (AWM_LOG_LEVEL >= AWM_L_INFO)
    #define AWM_LOGI(fmt, ...) AWM__PRINTF("I", fmt, ##__VA_ARGS__)
  #else
    #define AWM_LOGI(...) do{}while(0)
  #endif

  #if (AWM_LOG_LEVEL >= AWM_L_DEBUG)
    #define AWM_LOGD(fmt, ...) AWM__PRINTF("D", fmt, ##__VA_ARGS__)
  #else
    #define AWM_LOGD(...) do{}while(0)
  #endif

  #if (AWM_LOG_LEVEL >= AWM_L_VERBOSE)
    #define AWM_LOGV(fmt, ...) AWM__PRINTF("V", fmt, ##__VA_ARGS__)
  #else
    #define AWM_LOGV(...) do{}while(0)
  #endif

#else
  // Logging vollständig deaktiviert – alle Makros sind No-Ops.
  #define AWM_LOGE(...) do{}while(0)
  #define AWM_LOGW(...) do{}while(0)
  #define AWM_LOGI(...) do{}while(0)
  #define AWM_LOGD(...) do{}while(0)
  #define AWM_LOGV(...) do{}while(0)
#endif
