package de.borstel67.comfoair

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import org.json.JSONObject
import java.net.HttpURLConnection
import java.net.URL
import java.net.URLEncoder

/** HTTP-Zugriff auf die REST-API des ESP32 (/api/comfo/…, /api/mqtt/…, /api/sta/…). */
class ComfoApi(private val host: () -> String) {

    private fun baseUrl(): String {
        val h = host().trim().trimEnd('/')
        return if (h.startsWith("http://") || h.startsWith("https://")) h else "http://$h"
    }

    /**
     * Der ESP32 liest ~8 Werte nacheinander per RS232; fehlt einer, meldet er "ok":false,
     * liefert die übrigen aber trotzdem. Das ist kein Verbindungsfehler → Daten behalten.
     */
    suspend fun state(): JSONObject = request("GET", "/api/comfo/state", strict = false, readTimeoutMs = 20000)

    /** Lüftungsstufe 0 (Auto) … 4. */
    suspend fun setLevel(level: Int) { post("/api/comfo/setLevel", mapOf("level" to level.toString())) }

    suspend fun setComfort(tempC: Int) { post("/api/comfo/setComfort", mapOf("tempC" to tempC.toString())) }

    /** Soll-Lüfterstufen in Prozent (alle acht Werte werden gemeinsam geschrieben). */
    suspend fun setVentilationLevels(values: Map<String, Int>) {
        post("/api/comfo/setVentilationLevels", values.mapValues { it.value.toString() })
    }

    /** Anlagenkonfiguration (0x00D7): preheater, bypass, type, size, options, enthalpy, ewt. */
    suspend fun setStatus(values: Map<String, Int>) {
        post("/api/comfo/setStatus", values.mapValues { it.value.toString() })
    }

    suspend fun manualStart() { post("/api/comfo/manual/start") }
    suspend fun manualEnd() { post("/api/comfo/manual/end") }

    /** Klappen im manuellen Modus: 0 = zu, 1 = offen, 3 = Stopp; delaySeconds 0 = kein Auto-Stopp. */
    suspend fun manualFlaps(bypass: Int, preheater: Int, delaySeconds: Int) {
        post(
            "/api/comfo/manual/flaps",
            mapOf("bypass" to "$bypass", "preheater" to "$preheater", "delaySeconds" to "$delaySeconds"),
        )
    }

    /** Analogausgänge im manuellen Modus (0 – 100 %). */
    suspend fun manualAnalog(supply: Int, exhaust: Int, postheater: Int) {
        post("/api/comfo/manual/analog", mapOf("supply" to "$supply", "exhaust" to "$exhaust", "postheater" to "$postheater"))
    }

    /** Gespeicherte Werte des manuellen Modus (config.json): delaySeconds, supply, exhaust, postheater. */
    suspend fun manualConfig(): JSONObject = request("GET", "/api/comfo/manual/config")
    suspend fun saveManualConfig(values: Map<String, Int>) {
        post("/api/comfo/manual/config", values.mapValues { it.value.toString() })
    }

    suspend fun mqtt(): JSONObject = request("GET", "/api/mqtt/config")
    suspend fun saveMqtt(fields: Map<String, String>) { post("/api/mqtt/config", fields) }

    /** WLAN-Parameter des STA-Modus (DHCP/statisch, IP, Gateway, Subnetz, DNS, Hostname, NTP). */
    suspend fun wifi(): JSONObject = request("GET", "/api/sta/config")
    suspend fun saveWifi(fields: Map<String, String>) { post("/api/sta/config", fields) }

    /** Löscht die WLAN-Zugangsdaten; das Gerät startet danach im AP-Modus neu. */
    suspend fun eraseWifi() { post("/erase") }

    suspend fun reboot() { post("/reboot") }

    suspend fun logs(): String = withContext(Dispatchers.IO) { raw("GET", "/api/logs", null) }

    suspend fun clearLogs(): String = withContext(Dispatchers.IO) { raw("GET", "/api/logs?clear=1", null) }
    // Hinweis: ESPAsyncWebServer wertet "clear" per hasArg aus, der Wert ist egal.

    private suspend fun post(path: String, fields: Map<String, String> = emptyMap()): JSONObject =
        request(
            "POST", path,
            fields.entries.joinToString("&") { (k, v) -> "$k=${URLEncoder.encode(v, "UTF-8")}" },
        )

    private suspend fun request(
        method: String, path: String, body: String? = null,
        strict: Boolean = true, readTimeoutMs: Int = 8000,
    ): JSONObject =
        withContext(Dispatchers.IO) {
            val text = raw(method, path, body, readTimeoutMs)
            val json = if (text.isBlank()) JSONObject() else JSONObject(text)
            // Das Gerät meldet Fehler teils mit HTTP 200 und "ok":false
            if (strict && json.has("ok") && !json.optBoolean("ok", true)) {
                throw java.io.IOException(json.optString("error").ifEmpty { "Gerät meldet Fehler" })
            }
            json
        }

    private fun raw(method: String, path: String, body: String?, readTimeoutMs: Int = 8000): String {
        val conn = URL(baseUrl() + path).openConnection() as HttpURLConnection
        try {
            conn.requestMethod = method
            conn.connectTimeout = 5000
            conn.readTimeout = readTimeoutMs
            // Keine Keep-Alive-Verbindungen: der ESP32 schließt sie oft einseitig → "unexpected end of stream"
            conn.setRequestProperty("Connection", "close")
            if (method == "POST") {
                conn.doOutput = true
                conn.setRequestProperty("Content-Type", "application/x-www-form-urlencoded")
                conn.outputStream.use { it.write((body ?: "").toByteArray()) }
            }
            val code = conn.responseCode
            val stream = if (code in 200..299) conn.inputStream else conn.errorStream
            val text = stream?.bufferedReader()?.use { it.readText() }.orEmpty()
            if (code !in 200..299) {
                val msg = runCatching { JSONObject(text).optString("error") }.getOrDefault("")
                throw java.io.IOException(msg.ifEmpty { "HTTP $code" })
            }
            return text
        } finally {
            conn.disconnect()
        }
    }
}
