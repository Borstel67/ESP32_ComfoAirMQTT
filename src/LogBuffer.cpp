// SPDX-License-Identifier: MIT
// Implementierung des Ring-Puffers für Log-Nachrichten.
// Puffergröße: BUF_SIZE Bytes; ältere Einträge werden bei Überlauf verworfen.
// Thread-sicher: geloggt wird aus loop() und aus dem AsyncTCP-Task.
#include "LogBuffer.h"
#include <cstdarg>
#include <mutex>

namespace {
    // Puffergröße in Bytes (4 KiB).
    constexpr size_t BUF_SIZE = 4096;
    // Maximale Länge einer formatierten Zeile im Puffer.
    constexpr size_t LINE_SIZE = 256;

    char       s_buf[BUF_SIZE] = {0}; // Zeichenpuffer (nullterminiert)
    size_t     s_len = 0;             // aktuelle Belegung in Bytes
    std::mutex s_mtx;                 // schützt s_buf/s_len

    // Hängt n Bytes an s_buf an; verdrängt bei Überlauf älteste Daten.
    void appendRaw(const char* data, size_t n) {
        if (!data || n == 0) return;

        // Wenn die Eingabe größer als der Puffer ist, nur das Ende behalten.
        if (n >= BUF_SIZE) {
            data += (n - (BUF_SIZE - 1));
            n = BUF_SIZE - 1;
        }

        std::lock_guard<std::mutex> lock(s_mtx);

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

    // Kürzt eine übergelaufene Zeile so, dass sie mit Newline endet.
    size_t terminateTruncated(char* tmp) {
        tmp[LINE_SIZE - 2] = '\n';
        tmp[LINE_SIZE - 1] = '\0';
        return LINE_SIZE - 1;
    }
} // namespace

void awm_logf(const char* fmt, ...) {
    char tmp[LINE_SIZE];
    va_list ap;
    va_start(ap, fmt);
    const int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);

    if (n < 0) return;

    if (static_cast<size_t>(n) < sizeof(tmp)) {
        // Normalfall: einmal formatieren, zweimal ausgeben.
        Serial.write(reinterpret_cast<const uint8_t*>(tmp), static_cast<size_t>(n));
        appendRaw(tmp, static_cast<size_t>(n));
        return;
    }

    // Überlange Zeile: seriell vollständig, im Puffer gekürzt.
    char* big = static_cast<char*>(malloc(static_cast<size_t>(n) + 1));
    if (big) {
        va_start(ap, fmt);
        vsnprintf(big, static_cast<size_t>(n) + 1, fmt, ap);
        va_end(ap);
        Serial.write(reinterpret_cast<const uint8_t*>(big), static_cast<size_t>(n));
        free(big);
        appendRaw(tmp, terminateTruncated(tmp));
    } else {
        const size_t len = terminateTruncated(tmp);
        Serial.write(reinterpret_cast<const uint8_t*>(tmp), len);
        appendRaw(tmp, len);
    }
}

void awm_logbuf_clear() {
    std::lock_guard<std::mutex> lock(s_mtx);
    s_len = 0;
    s_buf[0] = '\0';
}

void awm_logbuf_copyTo(String& out) {
    // Erst Speicher reservieren (kann blockieren), dann unter Sperre kopieren.
    out = "";
    out.reserve(BUF_SIZE);
    std::lock_guard<std::mutex> lock(s_mtx);
    out.concat(s_buf, s_len);
}
