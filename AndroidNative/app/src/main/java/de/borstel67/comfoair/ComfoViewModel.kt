package de.borstel67.comfoair

import android.app.Application
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import org.json.JSONObject

class ComfoViewModel(app: Application) : AndroidViewModel(app) {

    private val prefs = app.getSharedPreferences("comfoair", 0)

    var host by mutableStateOf(prefs.getString("host", "") ?: "")
        private set
    var state by mutableStateOf<JSONObject?>(null)
        private set
    var mqtt by mutableStateOf<JSONObject?>(null)
        private set
    var wifi by mutableStateOf<JSONObject?>(null)
        private set
    var manualCfg by mutableStateOf<JSONObject?>(null)
        private set
    var log by mutableStateOf<String?>(null)
        private set
    var error by mutableStateOf<String?>(null)
        private set
    var message by mutableStateOf<String?>(null)

    /** Verlauf der letzten 5 min (60 × 5 s) für die Sparklines; null = kein Messwert. */
    var outsideHist by mutableStateOf<List<Double?>>(emptyList())
        private set
    var supplyHist by mutableStateOf<List<Double?>>(emptyList())
        private set
    var exhaustHist by mutableStateOf<List<Double?>>(emptyList())
        private set
    var outletHist by mutableStateOf<List<Double?>>(emptyList())
        private set

    private val api = ComfoApi { host }
    private var pollJob: Job? = null
    private var loadingConfig = false
    private var failures = 0

    init {
        if (host.isNotBlank()) loadConfig()
    }

    fun updateHost(value: String) {
        host = value.trim()
        prefs.edit().putString("host", host).apply()
        state = null; mqtt = null; wifi = null; manualCfg = null; log = null; failures = 0
        outsideHist = emptyList(); supplyHist = emptyList(); exhaustHist = emptyList(); outletHist = emptyList()
        error = null
        if (host.isNotBlank()) loadConfig()
    }

    /** Anlagenwerte alle 5 s abfragen, solange die App sichtbar ist. */
    fun startPolling() {
        if (pollJob?.isActive == true) return
        pollJob = viewModelScope.launch {
            while (isActive) {
                if (host.isNotBlank()) refreshState()
                delay(POLL_MS)
            }
        }
    }

    fun stopPolling() {
        pollJob?.cancel()
        pollJob = null
    }

    private suspend fun refreshState() {
        try {
            state = api.state()
            failures = 0
            error = null
            pushHistory()
            if (mqtt == null && wifi == null) loadConfig()
        } catch (e: Exception) {
            // Einzelne Aussetzer (ESP32 beschäftigt, RS232-Timeout) nicht sofort als offline werten
            if (++failures >= OFFLINE_AFTER) error = describe(e)
        }
    }

    private fun describe(e: Exception) = when (e) {
        is java.net.SocketTimeoutException -> "Zeitüberschreitung – ESP32 antwortet nicht"
        is java.net.UnknownHostException -> "Adresse nicht auflösbar"
        is java.net.ConnectException -> "Verbindung abgelehnt"
        else -> e.message ?: e.javaClass.simpleName
    }

    private fun pushHistory() {
        val t = state?.optJSONObject("temps")
        outsideHist = (outsideHist + t?.dbl("t1")).takeLast(HISTORY_POINTS)
        supplyHist = (supplyHist + t?.dbl("t2")).takeLast(HISTORY_POINTS)
        exhaustHist = (exhaustHist + t?.dbl("t3")).takeLast(HISTORY_POINTS)
        outletHist = (outletHist + t?.dbl("t4")).takeLast(HISTORY_POINTS)
    }

    fun loadConfig() {
        if (loadingConfig) return
        loadingConfig = true
        viewModelScope.launch {
            mqtt = runCatching { api.mqtt() }.getOrNull()
            wifi = runCatching { api.wifi() }.getOrNull()
            manualCfg = runCatching { api.manualConfig() }.getOrNull()
            loadingConfig = false
        }
    }

    /** Schreibzugriff mit Rückmeldung; danach sofort den Zustand neu laden. */
    private fun act(okText: String, reload: Boolean = true, block: suspend () -> Unit) {
        viewModelScope.launch {
            try {
                block()
                message = okText
                if (reload) refreshState()
            } catch (e: Exception) {
                message = "Fehler: ${e.message ?: e.javaClass.simpleName}"
            }
        }
    }

    fun setLevel(level: Int) = act("Stufe gesetzt.") { api.setLevel(level) }

    fun setComfort(tempC: Int) = act("Komforttemperatur gesetzt.") { api.setComfort(tempC) }

    fun setVentilationLevels(values: Map<String, Int>) =
        act("Lüfterstufen gespeichert.") { api.setVentilationLevels(values) }

    fun setStatus(values: Map<String, Int>) =
        act("Anlagenkonfiguration (0x00D7) gesendet.") { api.setStatus(values) }

    fun manualStart() = act("Manueller Modus gestartet.") { api.manualStart() }
    fun manualEnd() = act("Manueller Modus beendet.") { api.manualEnd() }

    fun manualFlaps(bypass: Int, preheater: Int, delaySeconds: Int) =
        act(if (delaySeconds > 0) "Klappen gesetzt, Auto-Stopp in ${delaySeconds}s." else "Klappen gesetzt, Stopp manuell.") {
            api.manualFlaps(bypass, preheater, delaySeconds)
            manualCfg = runCatching { api.manualConfig() }.getOrNull() ?: manualCfg
        }

    /** Analogausgänge setzen und zusammen mit der Auto-Stopp-Zeit in config.json speichern. */
    fun manualAnalog(supply: Int, exhaust: Int, postheater: Int, delaySeconds: Int) =
        act("Analogausgänge gesetzt und gespeichert.") {
            api.manualAnalog(supply, exhaust, postheater)
            saveManualCfg(supply, exhaust, postheater, delaySeconds)
        }

    fun saveAutoStop(supply: Int, exhaust: Int, postheater: Int, delaySeconds: Int) =
        act("Auto-Stopp-Zeit gespeichert: ${delaySeconds}s", reload = false) {
            saveManualCfg(supply, exhaust, postheater, delaySeconds)
        }

    private suspend fun saveManualCfg(supply: Int, exhaust: Int, postheater: Int, delaySeconds: Int) {
        api.saveManualConfig(
            mapOf("delaySeconds" to delaySeconds, "supply" to supply, "exhaust" to exhaust, "postheater" to postheater),
        )
        manualCfg = runCatching { api.manualConfig() }.getOrNull() ?: manualCfg
    }

    fun saveMqtt(fields: Map<String, String>) = act("MQTT gespeichert.", reload = false) {
        api.saveMqtt(fields)
        mqtt = api.mqtt()
    }

    fun saveWifi(fields: Map<String, String>) =
        act("Gespeichert. Das Gerät startet neu.", reload = false) { api.saveWifi(fields) }

    fun eraseWifi() =
        act("WLAN-Daten gelöscht. Das Gerät startet im AP-Modus neu.", reload = false) { api.eraseWifi() }

    fun reboot() = act("Neustart ausgelöst.", reload = false) { api.reboot() }

    fun loadLog() = act("Log geladen.", reload = false) { log = api.logs() }
    fun clearLog() = act("Log gelöscht.", reload = false) { log = api.clearLogs() }

    fun consumeMessage() { message = null }

    private companion object {
        const val POLL_MS = 5000L
        const val OFFLINE_AFTER = 3
    }
}
