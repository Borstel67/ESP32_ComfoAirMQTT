// SPDX-License-Identifier: MIT
// Ring-Puffer für Log-Nachrichten:
// Hält die letzten Meldungen im DRAM und ermöglicht
// Abruf über HTTP (/api/logs).
#pragma once
#include <Arduino.h>

// Hängt eine fertige Zeichenkette an den Ring-Puffer an.
void awm_logbuf_append(const char* msg);

// Formatiert printf-artig und hängt das Ergebnis an den Ring-Puffer an.
void awm_logbuf_appendf(const char* fmt, ...);

// Leert den Ring-Puffer.
void awm_logbuf_clear();

// Kopiert den gesamten Pufferinhalt in einen Arduino-String.
void awm_logbuf_copyTo(String& out);