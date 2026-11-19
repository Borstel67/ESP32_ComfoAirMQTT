package de.borstel67.comfoair

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

@Composable
fun ConfigScreen(vm: ComfoViewModel) {
    ConnectionCard(vm)
    if (vm.host.isBlank()) return

    MqttCard(vm)
    WifiCards(vm)
    LogCard(vm)
    DeviceCard(vm)
}

@Composable
private fun ConnectionCard(vm: ComfoViewModel) {
    var host by remember(vm.host) { mutableStateOf(vm.host) }
    Section("Verbindung zum Gerät") {
        AppField(host, { host = it }, label = "ESP32-Adresse", placeholder = "192.168.1.50", keyboard = KeyboardType.Uri)
        AppButton("Speichern & verbinden", { vm.updateHost(host) }, Modifier.fillMaxWidth(), primary = true)
        Hint("Die App spricht per HTTP direkt mit dem ESP32 im selben WLAN (IP oder Hostname).")
    }
}

// ── MQTT (MQTTSettings.html) ─────────────────────────────────────────────────

@Composable
private fun MqttCard(vm: ComfoViewModel) {
    val m = vm.mqtt ?: return
    var enabled by remember(m) { mutableStateOf(m.optBoolean("enabled")) }
    var host by remember(m) { mutableStateOf(m.optString("host")) }
    var port by remember(m) { mutableStateOf(m.optInt("port", 1883).toString()) }
    var user by remember(m) { mutableStateOf(m.optString("user")) }
    var pass by remember(m) { mutableStateOf(m.optString("pass")) }
    var clientId by remember(m) { mutableStateOf(m.optString("clientId")) }
    var prefix by remember(m) { mutableStateOf(m.optString("topicPrefix")) }

    AppCard {
        Sec("MQTT – Broker") {
            SwitchRow("MQTT aktivieren", enabled) { enabled = it }
            AppField(host, { host = it }, label = "Host", placeholder = "192.168.1.10", keyboard = KeyboardType.Uri)
            AppField(port, { port = it }, label = "Port", keyboard = KeyboardType.Number)
            AppField(user, { user = it }, label = "Benutzer")
            AppField(pass, { pass = it }, label = "Passwort", password = true)
        }
        Sec("MQTT – Veröffentlichung", divider = true) {
            AppField(clientId, { clientId = it }, label = "Client-ID")
            AppField(prefix, { prefix = it }, label = "Topic-Präfix")
        }
    }
    AppButton("MQTT speichern", {
        vm.saveMqtt(
            mapOf(
                "enabled" to if (enabled) "1" else "0",
                "host" to host.trim(),
                "port" to (port.trim().toIntOrNull() ?: 0).toString(),
                "user" to user, "pass" to pass,
                "clientId" to clientId.trim(), "topicPrefix" to prefix.trim(),
            ),
        )
    }, Modifier.fillMaxWidth(), primary = true)
}

// ── WLAN (settings.html, STA-Teil) ───────────────────────────────────────────

@Composable
private fun WifiCards(vm: ComfoViewModel) {
    val w = vm.wifi ?: return

    var dhcp by remember(w) { mutableStateOf(w.optBoolean("dhcp", true)) }
    var ip by remember(w) { mutableStateOf(w.optString("ip")) }
    var gw by remember(w) { mutableStateOf(w.optString("gw")) }
    var sn by remember(w) { mutableStateOf(w.optString("sn")) }
    var dns1 by remember(w) { mutableStateOf(w.optString("dns1")) }
    var dns2 by remember(w) { mutableStateOf(w.optString("dns2")) }
    var ntp1 by remember(w) { mutableStateOf(w.optString("ntp1")) }
    var ntp2 by remember(w) { mutableStateOf(w.optString("ntp2")) }
    var hostname by remember(w) { mutableStateOf(w.optString("hostname")) }
    var confirmErase by remember { mutableStateOf(false) }

    AppCard {
        Sec("WLAN – Netzwerk") {
            Hint("SSID und Passwort lassen sich nur im AP-Modus (Captive-Portal) ändern.")
            AppField(hostname, { hostname = it }, label = "Hostname", placeholder = "comfoair")
        }
        Sec("Adressierung", divider = true) {
            ModeRow(listOf("DHCP", "Statisch"), if (dhcp) 0 else 1) { dhcp = it == 0 }
            if (!dhcp) {
                AppField(ip, { ip = it }, label = "IP-Adresse", placeholder = "192.168.1.50", keyboard = KeyboardType.Uri)
                AppField(gw, { gw = it }, label = "Gateway", placeholder = "192.168.1.1", keyboard = KeyboardType.Uri)
                AppField(sn, { sn = it }, label = "Subnetz", placeholder = "255.255.255.0", keyboard = KeyboardType.Uri)
                AppField(dns1, { dns1 = it }, label = "DNS 1", placeholder = "1.1.1.1", keyboard = KeyboardType.Uri)
                AppField(dns2, { dns2 = it }, label = "DNS 2", placeholder = "8.8.8.8", keyboard = KeyboardType.Uri)
                Hint("Nach einer Änderung der IP-Adresse die ESP32-Adresse oben anpassen.")
            }
        }
        Sec("Zeitserver", divider = true) {
            AppField(ntp1, { ntp1 = it }, label = "NTP 1", placeholder = "de.pool.ntp.org")
            AppField(ntp2, { ntp2 = it }, label = "NTP 2", placeholder = "ptbtime1.ptb.de")
        }
    }
    AppButton("WLAN speichern (Neustart)", {
        vm.saveWifi(
            mapOf(
                "dhcp" to if (dhcp) "1" else "0",
                "ip" to ip.trim(), "gw" to gw.trim(), "sn" to sn.trim(),
                "dns1" to dns1.trim(), "dns2" to dns2.trim(),
                "hostname" to hostname.trim(),
                "ntp1" to ntp1.trim(), "ntp2" to ntp2.trim(),
            ),
        )
    }, Modifier.fillMaxWidth(), primary = true)

    Section("WLAN – Gefährlicher Bereich") {
        Hint("Löscht die WLAN-Zugangsdaten und startet das Gerät neu. Danach ist es nur noch über den eigenen Access Point (192.168.4.1) erreichbar.")
        AppButton("Anmeldedaten löschen und neu starten", { confirmErase = true }, Modifier.fillMaxWidth())
    }

    if (confirmErase) {
        AlertDialog(
            onDismissRequest = { confirmErase = false },
            containerColor = AppColors.Card, titleContentColor = AppColors.Text, textContentColor = AppColors.Label,
            text = { Text("WLAN-Zugangsdaten wirklich löschen? Das Gerät startet im AP-Modus neu und ist für die App nicht mehr erreichbar.") },
            confirmButton = {
                TextButton({
                    confirmErase = false
                    vm.eraseWifi()
                }) { Text("Löschen", color = AppColors.Bad) }
            },
            dismissButton = { TextButton({ confirmErase = false }) { Text("Abbrechen", color = AppColors.Muted) } },
        )
    }
}

// ── Log und Neustart ─────────────────────────────────────────────────────────

@Composable
private fun LogCard(vm: ComfoViewModel) {
    Section("Geräte-Log") {
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            AppButton("Laden", { vm.loadLog() }, Modifier.weight(1f))
            AppButton("Leeren", { vm.clearLog() }, Modifier.weight(1f), ghost = true)
        }
        vm.log?.let { text ->
            Column(Modifier.fillMaxWidth().heightIn(max = 320.dp).verticalScroll(rememberScrollState())) {
                Text(
                    text.ifBlank { "(leer)" },
                    fontSize = 11.sp, lineHeight = 14.sp, fontFamily = FontFamily.Monospace,
                    color = AppColors.Label, modifier = Modifier.padding(2.dp),
                )
            }
        }
    }
}

@Composable
private fun DeviceCard(vm: ComfoViewModel) {
    var confirm by remember { mutableStateOf(false) }
    Section("Gerät") {
        AppButton("ESP32 neu starten", { confirm = true }, Modifier.fillMaxWidth())
    }
    if (confirm) {
        AlertDialog(
            onDismissRequest = { confirm = false },
            containerColor = AppColors.Card, textContentColor = AppColors.Label,
            text = { Text("ESP32 jetzt neu starten?") },
            confirmButton = {
                TextButton({ confirm = false; vm.reboot() }) { Text("Neustart", color = AppColors.Accent) }
            },
            dismissButton = { TextButton({ confirm = false }) { Text("Abbrechen", color = AppColors.Muted) } },
        )
    }
}
