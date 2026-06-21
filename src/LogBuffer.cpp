// SPDX-License-Identifier: MIT
// Implementierung des Ring-Puffers für Log-Nachrichten.
// Puffergröße: BUF_SIZE Bytes; ältere Einträge werden bei Überlauf verworfen.
#include "LogBuffer.h"
#include <cstdarg>

namespace {
    // Puffergröße in Bytes (4 KiB).
    constexpr size_t BUF_SIZE = 4096;

    static char   s_buf[BUF_SIZE] = {0}; // Zeichenpuffer (nullterminiert)
    static size_t s_len = 0;              // aktuelle Belegung in Bytes

    // Hängt n Bytes an s_buf an; verdrängt bei Überlauf älteste Daten.
    void appendRaw(const char* data, size_t n) {
        if (!data || n == 0) return;

        // Wenn die Eingabe größer als der Puffer ist, nur das Ende behalten.
        if (n >= BUF_SIZE) {
            data += (n - (BUF_SIZE - 1));
            n = BUF_SIZE - 1;
        }

        // Platz schaffen: älteste Bytes am Anfang verwerfen.
        if (n > (BUF_SIZE - 1 - s_len)) {
            size_t drop = n - (BUF_SIZE - 1 - s_len) + 1;
            if (drop > s_len) drop = s_len;
            memmove(s_buf, s_buf + drop, s_len - drop);
            s_len -= drop;
        }

        memcpy(s_buf + s_len, data, n);
        s_len += n;
        s_buf[s_len] = '\0';
    }
} // namespace

void awm_logbuf_append(const char* msg) {
    if (!msg) return;
    appendRaw(msg, strlen(msg));
}

void awm_logbuf_appendf(const char* fmt, ...) {
    char tmp[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);

    if (n < 0) return;

    // Bei Überlauf des temporären Puffers: letzte Zeile mit Newline abschließen.
    if (static_cast<size_t>(n) >= sizeof(tmp)) {
        tmp[sizeof(tmp) - 2] = '\n';
        tmp[sizeof(tmp) - 1] = '\0';
        n = sizeof(tmp) - 1;
    }

    appendRaw(tmp, static_cast<size_t>(n));
}

void awm_logbuf_clear() {
    s_len = 0;
    s_buf[0] = '\0';
}

void awm_logbuf_copyTo(String& out) {
    out = String(s_buf);
}