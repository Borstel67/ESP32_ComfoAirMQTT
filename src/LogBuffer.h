// SPDX-License-Identifier: MIT
// Ring-Puffer für Log-Nachrichten:
// Hält die letzten Meldungen im DRAM und ermöglicht
// Abruf über HTTP (/api/logs).
#pragma once
#include <Arduino.h>

// Formatiert einmal und schreibt das Ergebnis auf Serial und in den Ring-Puffer.
void awm_logf(const char* fmt, ...);

// Leert den Ring-Puffer.
void awm_logbuf_clear();

// Kopiert den gesamten Pufferinhalt in einen Arduino-String.
void awm_logbuf_copyTo(String& out);