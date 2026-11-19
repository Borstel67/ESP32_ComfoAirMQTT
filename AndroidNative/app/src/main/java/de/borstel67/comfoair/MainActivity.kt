package de.borstel67.comfoair

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.viewModels
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.pager.HorizontalPager
import androidx.compose.foundation.pager.rememberPagerState
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Air
import androidx.compose.material.icons.filled.Build
import androidx.compose.material.icons.filled.Dashboard
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material.icons.filled.Tune
import androidx.compose.material3.Icon
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.NavigationBarItemDefaults
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Snackbar
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlinx.coroutines.launch
import org.json.JSONObject
import kotlin.math.roundToInt

class MainActivity : ComponentActivity() {
    private val vm: ComfoViewModel by viewModels()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContent { ComfoTheme { ComfoApp(vm) } }
    }

    // Anlagenwerte nur abfragen, solange die App sichtbar ist
    override fun onStart() {
        super.onStart()
        vm.startPolling()
    }

    override fun onStop() {
        vm.stopPolling()
        super.onStop()
    }
}

private enum class Tab(val label: String, val title: String, val icon: ImageVector) {
    Dashboard("Dashboard", "ComfoAir", Icons.Filled.Dashboard),
    Control("Steuerung", "Steuerung", Icons.Filled.Tune),
    ComfoSettings("ComfoAir", "ComfoAir Settings", Icons.Filled.Build),
    Config("App", "App", Icons.Filled.Settings),
}

@OptIn(ExperimentalFoundationApi::class)
@Composable
fun ComfoApp(vm: ComfoViewModel) {
    // Ohne gespeicherte Adresse direkt die Konfiguration öffnen
    // Der Pager erlaubt das Wischen zwischen den drei Seiten; die Leiste unten folgt ihm.
    val pager = rememberPagerState(initialPage = if (vm.host.isBlank()) Tab.Config.ordinal else 0) { Tab.entries.size }
    val current = Tab.entries[pager.currentPage]
    val scope = rememberCoroutineScope()
    val snack = remember { SnackbarHostState() }

    LaunchedEffect(vm.message) {
        vm.message?.let { snack.showSnackbar(it); vm.consumeMessage() }
    }

    Scaffold(
        containerColor = AppColors.Bg,
        snackbarHost = {
            SnackbarHost(snack) { Snackbar(it, containerColor = AppColors.Card, contentColor = AppColors.Text) }
        },
        topBar = {
            val online = vm.error == null && vm.state != null
            Column(Modifier.padding(start = 12.dp, end = 12.dp, top = 12.dp)) {
                AppHeader(
                    title = current.title,
                    subtitle = subtitle(vm),
                    icon = if (current == Tab.Dashboard) Icons.Filled.Air else current.icon,
                    badge = when {
                        vm.host.isBlank() -> null
                        online -> "online"
                        vm.error != null -> "offline"
                        else -> null
                    },
                    badgeOk = online,
                )
            }
        },
        bottomBar = {
            NavigationBar(containerColor = AppColors.Card) {
                Tab.entries.forEach { t ->
                    NavigationBarItem(
                        selected = current == t,
                        onClick = { scope.launch { pager.animateScrollToPage(t.ordinal) } },
                        icon = { Icon(t.icon, null) },
                        label = { Text(t.label) },
                        colors = NavigationBarItemDefaults.colors(
                            selectedIconColor = AppColors.Accent, selectedTextColor = AppColors.Accent,
                            indicatorColor = AppColors.Ring,
                            unselectedIconColor = AppColors.Muted, unselectedTextColor = AppColors.Muted,
                        ),
                    )
                }
            }
        },
    ) { pad ->
        HorizontalPager(
            state = pager,
            modifier = Modifier.padding(pad).fillMaxSize(),
            verticalAlignment = Alignment.Top,
        ) { page ->
            // Jede Seite scrollt für sich, damit die Position beim Wischen erhalten bleibt.
            Column(
                Modifier
                    .fillMaxSize()
                    .verticalScroll(rememberScrollState())
                    .padding(12.dp),
                verticalArrangement = Arrangement.spacedBy(12.dp),
            ) {
                vm.error?.let { Hint("Keine Verbindung: $it", color = AppColors.Bad) }
                when (Tab.entries[page]) {
                    Tab.Dashboard -> DashboardScreen(vm)
                    Tab.Control -> ControlScreen(vm)
                    Tab.ComfoSettings -> ComfoSettingsScreen(vm)
                    Tab.Config -> ConfigScreen(vm)
                }
                Copyright()
            }
        }
    }
}

private fun subtitle(vm: ComfoViewModel): String = when {
    vm.host.isBlank() -> "Keine Adresse eingetragen"
    vm.state == null -> "…"
    vm.state?.optBoolean("ok", true) == false -> "Anlage antwortet unvollständig"
    else -> vm.host
}

@Composable
private fun Copyright() {
    val ctx = LocalContext.current
    val version = remember {
        runCatching { ctx.packageManager.getPackageInfo(ctx.packageName, 0).versionName }.getOrNull().orEmpty()
    }
    Text(
        "© 2026 R.Wisser · v$version",
        Modifier.fillMaxWidth().padding(vertical = 8.dp),
        fontSize = 11.sp, color = AppColors.Muted, textAlign = TextAlign.Center,
    )
}

@Composable
internal fun EmptyState(vm: ComfoViewModel) {
    Hint(
        if (vm.host.isBlank()) "Bitte zuerst die Adresse des ESP32 eintragen (App)." else "Lade …",
        Modifier.padding(4.dp),
    )
}

// ── Dashboard (ComfoAirDriver.html, Live-Werte) ──────────────────────────────

private fun JSONObject.obj(key: String): JSONObject = optJSONObject(key) ?: JSONObject()

private fun levelText(level: Int?) = when (level) {
    null -> "–"
    0 -> "Auto"
    else -> "Stufe $level"
}

@Composable
fun DashboardScreen(vm: ComfoViewModel) {
    val s = vm.state
    if (s == null) { EmptyState(vm); return }
    val stale = vm.error != null   // Werte veraltet
    val t = s.obj("temps")
    val fan = s.obj("fan")
    val vent = s.obj("vent")
    val bypass = s.obj("bypass")
    val flap = s.obj("flap")
    val manual = s.optBoolean("manualModeActive")

    Column(Modifier.alpha(if (stale) 0.5f else 1f), verticalArrangement = Arrangement.spacedBy(12.dp)) {
        Section("Temperaturen", trailing = { Hint("(Verlauf: letzte 5 min)") }) {
            TileRow {
                Tile("T1 Außenluft", fmt(t.dbl("t1"), 1, "°C"), "Außen", history = vm.outsideHist)
                Tile("T2 Zuluft", fmt(t.dbl("t2"), 1, "°C"), "in die Räume", history = vm.supplyHist)
            }
            TileRow {
                Tile("T3 Abluft", fmt(t.dbl("t3"), 1, "°C"), "aus den Räumen", history = vm.exhaustHist)
                Tile("T4 Fortluft", fmt(t.dbl("t4"), 1, "°C"), "nach außen", history = vm.outletHist)
            }
        }

        Section("Lüftung") {
            TileRow {
                Tile("Stufe", levelText(vent.optInt("current", -1).takeIf { vent.has("current") }), "Komfort ${fmt(t.dbl("comfortC"), 1, "°C")}")
                Tile(
                    "Soll Zu-/Abluft",
                    "${vent.optInt("supplyCur")} / ${vent.optInt("exhaustCur")} %",
                    "aktuelle Vorgabe",
                )
            }
            TileRow {
                Tile("Zuluft-Ventilator", "${fan.optInt("supplyPct")} %", "${fan.optInt("supplyRpm")} U/min")
                Tile("Abluft-Ventilator", "${fan.optInt("exhaustPct")} %", "${fan.optInt("exhaustRpm")} U/min")
            }
        }

        Section("Bypass & Vorheizer") {
            TileRow {
                Tile(
                    "Bypass", "${flap.optInt("bypassPercent")} %",
                    "Stufe ${bypass.optInt("stage")} · Sommermodus " + if (bypass.optInt("summerMode") == 1) "ja" else "nein",
                )
                Tile("Vorheizer-Klappe", preheaterText(flap.optInt("preheaterState", -1)), "ADC ${flap.optInt("preheaterAdc")}")
            }
            Chips {
                Chip("Manueller Modus " + if (manual) "an" else "aus", manual)
                val d5 = s.obj("statusD5")
                Chip("Bypass " + if (d5.optInt("bypassPresent") == 1) "vorhanden" else "nicht vorhanden", d5.optInt("bypassPresent") == 1)
                Chip("Vorheizer " + if (d5.optInt("preheaterPresent") == 1) "vorhanden" else "nicht vorhanden", d5.optInt("preheaterPresent") == 1)
            }
        }
    }
}

private fun preheaterText(state: Int) = when (state) {
    0 -> "geschlossen"
    1 -> "offen"
    3 -> "Stopp"
    else -> "unbekannt"
}

// ── Steuerung (ComfoAirDriver.html, Schreiben) ───────────────────────────────

@Composable
fun ControlScreen(vm: ComfoViewModel) {
    val s = vm.state
    if (s == null) { EmptyState(vm); return }
    val current = s.obj("vent").optInt("current", 0)
    val comfort = s.obj("temps").dbl("comfortC")
    var temp by remember(comfort) { mutableStateOf(comfort?.roundToInt()?.toString() ?: "20") }
    val manual = s.optBoolean("manualModeActive")

    AppCard {
        Sec("Ventilator-Stufe") {
            // Index 0..3 entspricht Stufe 1..4; bei anderer Stufe ist kein Knopf markiert.
            ModeRow(listOf("1", "2", "3", "4"), current - 1) { vm.setLevel(it + 1) }
            Hint("Aktuell: ${levelText(current)}. Die Stufe wird sofort an die Anlage gesendet.")
        }
        Sec("Komforttemperatur", divider = true) {
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.Bottom) {
                AppField(temp, { temp = it }, Modifier.weight(1f), keyboard = KeyboardType.Number, suffix = "°C")
                AppButton("Setzen", {
                    val v = temp.trim().toIntOrNull()
                    if (v == null || v !in -10..40) vm.message = "Ungültige Komforttemperatur (−10 … 40)."
                    else vm.setComfort(v)
                }, primary = true)
            }
        }
        Sec("Manueller Modus", divider = true, trailing = { StatusDot(manual) }) {
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                AppButton("Starten", { vm.manualStart() }, Modifier.weight(1f), primary = !manual)
                AppButton("Beenden", { vm.manualEnd() }, Modifier.weight(1f), primary = manual)
            }
            Hint("Der manuelle Modus (Testmodus) schaltet die Anlage in den RS232-Handbetrieb, z. B. für die Klappenprüfung.")
        }
    }
}
