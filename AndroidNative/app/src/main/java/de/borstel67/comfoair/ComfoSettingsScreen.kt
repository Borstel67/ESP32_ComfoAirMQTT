package de.borstel67.comfoair

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.runtime.Composable
import androidx.compose.runtime.MutableState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import org.json.JSONObject

// ── ComfoAir Settings (ComfoAirSettings.html) ────────────────────────────────

@Composable
fun ComfoSettingsScreen(vm: ComfoViewModel) {
    val s = vm.state
    if (s == null) { EmptyState(vm); return }
    StatusCard(vm, s.optJSONObject("statusD5") ?: JSONObject())
    VentLevelsCard(vm, s.optJSONObject("vent") ?: JSONObject())
    ManualCard(vm, s)
}

/** Formularfelder, die neu gefüllt werden, sobald die Anlage andere Werte meldet. */
@Composable
private fun fields(src: JSONObject, keys: List<Pair<String, String>>): Map<String, MutableState<String>> =
    keys.associate { (k, _) ->
        val v = if (src.has(k)) src.optInt(k).toString() else ""
        k to remember(v) { mutableStateOf(v) }
    }

/** Zwei Eingabefelder je Zeile. */
@Composable
private fun FieldGrid(keys: List<Pair<String, String>>, vals: Map<String, MutableState<String>>, suffix: String? = null) {
    keys.chunked(2).forEach { row ->
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            row.forEach { (k, label) ->
                val st = vals.getValue(k)
                AppField(st.value, { st.value = it }, Modifier.weight(1f), label = label, keyboard = KeyboardType.Number, suffix = suffix)
            }
        }
    }
}

private fun parse(vals: Map<String, MutableState<String>>) = vals.mapValues { it.value.value.trim().toIntOrNull() }

// ── Anlagenkonfiguration (0x00D7) ────────────────────────────────────────────

private val statusKeys = listOf(
    "preheater" to "Vorheizung (0/1)",
    "bypass" to "Bypass (0/1)",
    "type" to "Typ (1=links/2=rechts)",
    "size" to "Größe (1=groß/2=klein)",
    "options" to "Optionen (0–255)",
    "enthalpy" to "Enthalpie (0–2)",
    "ewt" to "EWT (0–2)",
)

/** Feldname im Formular → Schlüssel in statusD5 bzw. erlaubter Bereich. */
private val statusSource = mapOf(
    "preheater" to "preheaterPresent", "bypass" to "bypassPresent", "type" to "type", "size" to "size",
    "options" to "options", "enthalpy" to "enthalpyPresent", "ewt" to "ewtPresent",
)
private val statusRange = mapOf(
    "preheater" to 0..1, "bypass" to 0..1, "type" to 1..2, "size" to 1..2,
    "options" to 0..255, "enthalpy" to 0..2, "ewt" to 0..2,
)

@Composable
private fun StatusCard(vm: ComfoViewModel, d5: JSONObject) {
    // statusD5-Schlüssel auf die Formularnamen abbilden
    val src = JSONObject().apply { statusSource.forEach { (f, k) -> if (d5.has(k)) put(f, d5.optInt(k)) } }
    val vals = fields(src, statusKeys)
    Section("Anlagenkonfiguration") {
        FieldGrid(statusKeys, vals)
        AppButton("Setzen", {
            val p = parse(vals)
            if (p.any { (k, v) -> v == null || v !in statusRange.getValue(k) }) vm.message = "Ungültige Werte in der Anlagenkonfiguration."
            else vm.setStatus(p.mapValues { it.value!! })
        }, Modifier.fillMaxWidth(), primary = true)
    }
}

// ── Ventilationsstufen (0x00CF) ──────────────────────────────────────────────

private val ventKeys = listOf(
    "supplyAbsent" to "Zuluft Stufe 1", "exhaustAbsent" to "Abluft Stufe 1",
    "supplyLow" to "Zuluft Stufe 2", "exhaustLow" to "Abluft Stufe 2",
    "supplyMedium" to "Zuluft Stufe 3", "exhaustMedium" to "Abluft Stufe 3",
    "supplyHigh" to "Zuluft Stufe 4", "exhaustHigh" to "Abluft Stufe 4",
)

@Composable
private fun VentLevelsCard(vm: ComfoViewModel, vent: JSONObject) {
    val vals = fields(vent, ventKeys)
    Section("Ventilationsstufen") {
        FieldGrid(ventKeys, vals, suffix = "%")
        AppButton("Setzen", {
            val p = parse(vals)
            if (p.values.any { it == null || it !in 0..100 }) vm.message = "Alle Werte müssen zwischen 0 und 100 liegen."
            else vm.setVentilationLevels(p.mapValues { it.value!! })
        }, Modifier.fillMaxWidth(), primary = true)
    }
}

// ── Manueller Modus: Klappen, Auto-Stopp, Analogausgänge ─────────────────────

private val flapValues = listOf(0, 1, 3)   // Zu / Offen / Stopp
private val flapLabels = listOf("Zu", "Offen", "Stopp")

private val analogKeys = listOf(
    "supply" to "Zulüfter",
    "exhaust" to "Ablüfter",
    "postheater" to "Nachheizung",
)

@Composable
private fun ManualCard(vm: ComfoViewModel, s: JSONObject) {
    val manualActive = s.optBoolean("manualModeActive")
    val manual = s.optJSONObject("manual") ?: JSONObject()
    // config.json hat Vorrang, sonst die zuletzt gesendeten Analogwerte aus dem Live-Zustand
    val cfg = vm.manualCfg ?: manual.optJSONObject("analogCmd") ?: JSONObject()

    var delay by remember(cfg.optInt("delaySeconds")) { mutableStateOf(cfg.optInt("delaySeconds").toString()) }
    val analog = fields(cfg, analogKeys)
    var bypass by remember { mutableIntStateOf(3) }
    var preheater by remember { mutableIntStateOf(3) }

    fun delaySec() = (delay.trim().toIntOrNull() ?: 0).coerceIn(0, 600)
    fun sendFlaps() = vm.manualFlaps(bypass, preheater, delaySec())

    AppCard {
        Sec("Manueller Modus", trailing = { StatusDot(manualActive) }) {
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                AppButton("Ein", { vm.manualStart() }, Modifier.weight(1f), primary = !manualActive)
                AppButton("Aus", {
                    bypass = 3; preheater = 3
                    vm.manualEnd()
                }, Modifier.weight(1f), primary = manualActive)
            }
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.Bottom) {
                AppField(delay, { delay = it }, Modifier.weight(1f), label = "Auto-Stopp (0 = manuell)", keyboard = KeyboardType.Number, suffix = "s")
                AppButton("Setzen", {
                    val a = parse(analog)
                    vm.saveAutoStop(a["supply"] ?: 0, a["exhaust"] ?: 0, a["postheater"] ?: 0, delaySec())
                }, primary = true)
            }
        }
        Sec("Klappen", divider = true) {
            FieldLabel("Bypass")
            ModeRow(flapLabels, flapValues.indexOf(bypass)) { bypass = flapValues[it]; sendFlaps() }
            Chips { Chip("Bypass: " + manual.optString("requestedBypassText", "unbekannt").lowercase(), manual.optString("requestedBypassText").equals("offen", true)) }
            FieldLabel("Vorheizung")
            ModeRow(flapLabels, flapValues.indexOf(preheater)) { preheater = flapValues[it]; sendFlaps() }
            Chips { Chip("Vorheizung: " + manual.optString("requestedPreheaterText", "unbekannt").lowercase(), manual.optString("requestedPreheaterText").equals("offen", true)) }
            Hint("Die Auswahl wird sofort gesendet (0x0009); bei Auto-Stopp > 0 stoppt der Motor nach der eingestellten Zeit.")
        }
        Sec("Analogausgänge", divider = true) {
            analogKeys.forEach { (k, label) ->
                val st = analog.getValue(k)
                AppField(st.value, { st.value = it }, label = label, keyboard = KeyboardType.Number, suffix = "%")
            }
            AppButton("Setzen", {
                val a = parse(analog)
                if (a.values.any { it == null || it !in 0..100 }) vm.message = "Alle Werte müssen zwischen 0 und 100 liegen."
                else vm.manualAnalog(a.getValue("supply")!!, a.getValue("exhaust")!!, a.getValue("postheater")!!, delaySec())
            }, Modifier.fillMaxWidth(), primary = true)
        }
    }
}
