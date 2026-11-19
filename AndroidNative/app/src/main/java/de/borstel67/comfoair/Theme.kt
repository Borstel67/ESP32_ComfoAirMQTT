package de.borstel67.comfoair

import androidx.compose.animation.core.LinearOutSlowInEasing
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxScope
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.IntrinsicSize
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ArrowDropDown
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Slider
import androidx.compose.material3.SliderDefaults
import androidx.compose.material3.Surface
import androidx.compose.material3.Switch
import androidx.compose.material3.SwitchDefaults
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TimePicker
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.rememberTimePickerState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.focus.onFocusChanged
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.StrokeJoin
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.text.input.VisualTransformation
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import org.json.JSONObject
import java.util.Locale
import kotlin.math.roundToInt

// Farben und Maße aus data/WifiManager.css, damit App und Weboberfläche gleich aussehen.
object AppColors {
    val Bg = Color(0xFF0B1220)
    val Card = Color(0xFF121A2B)
    val Ring = Color(0xFF1B2A4A)
    val Muted = Color(0xFF9FB0CF)
    val Text = Color(0xFFE6EFFF)
    val Label = Color(0xFFCBD6EF)
    val Accent = Color(0xFF4CC9F0)
    val Accent2 = Color(0xFF4361EE)
    val Ok = Color(0xFF2EC27E)
    val Bad = Color(0xFFFF6B6B)
    val Tile = Color(0xFF0F1730)
    val TileRing = Color(0xFF1C2C50)
    val Input = Color(0xFF0E162B)
    val InputRing = Color(0xFF1E2B4A)
    val InputFocus = Color(0xFF385A9B)
    val BtnBg = Color(0xFF0F1830)
    val BtnRing = Color(0xFF24406C)
    val Gradient = Brush.linearGradient(listOf(Accent2, Accent))
}

private val CardShape = RoundedCornerShape(14.dp)
private val TileShape = RoundedCornerShape(12.dp)
private val InputShape = RoundedCornerShape(10.dp)

@Composable
fun ComfoTheme(content: @Composable () -> Unit) {
    val c = AppColors
    val scheme = darkColorScheme(
        primary = c.Accent, onPrimary = Color(0xFF00212B),
        primaryContainer = c.Accent2, onPrimaryContainer = Color.White,
        secondary = c.Accent2, onSecondary = Color.White,
        secondaryContainer = c.Ring, onSecondaryContainer = c.Text,
        background = c.Bg, onBackground = c.Text,
        surface = c.Card, onSurface = c.Text,
        surfaceVariant = c.Tile, onSurfaceVariant = c.Label,
        surfaceContainer = c.Card, surfaceContainerHigh = c.Tile, surfaceContainerHighest = c.Ring,
        outline = c.InputRing, outlineVariant = c.Ring,
        error = c.Bad,
    )
    MaterialTheme(colorScheme = scheme, content = content)
}

// ── Karten und Abschnitte (.card / .sec) ─────────────────────────────────────

@Composable
fun AppCard(modifier: Modifier = Modifier, content: @Composable ColumnScope.() -> Unit) {
    Column(
        modifier
            .fillMaxWidth()
            .clip(CardShape)
            .background(AppColors.Card)
            .border(1.dp, AppColors.Ring, CardShape),
        content = content,
    )
}

/** Abschnitt innerhalb einer [AppCard]; [divider] = Trennlinie darüber (.sec + .sec). */
@Composable
fun Sec(
    title: String?,
    divider: Boolean = false,
    trailing: @Composable (() -> Unit)? = null,
    content: @Composable ColumnScope.() -> Unit,
) {
    if (divider) HorizontalDivider(color = AppColors.Ring)
    Column(Modifier.fillMaxWidth().padding(12.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
        if (title != null) {
            Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                Text(title, fontSize = 15.sp, fontWeight = FontWeight.Bold, color = AppColors.Text)
                trailing?.invoke()
            }
        }
        content()
    }
}

@Composable
fun Section(
    title: String?,
    trailing: @Composable (() -> Unit)? = null,
    content: @Composable ColumnScope.() -> Unit,
) = AppCard { Sec(title, trailing = trailing, content = content) }

@Composable
fun Hint(text: String, modifier: Modifier = Modifier, color: Color = AppColors.Muted) {
    Text(text, modifier, fontSize = 12.sp, color = color, lineHeight = 16.sp)
}

/** Rückmeldung unter einem Formular (.hint.ok / .hint.err). */
@Composable
fun Msg(text: String?, ok: Boolean) {
    if (!text.isNullOrBlank()) Hint(text, color = if (ok) AppColors.Ok else AppColors.Bad)
}

// ── Kopfzeile (.header) ──────────────────────────────────────────────────────

@Composable
fun AppHeader(title: String, subtitle: String, icon: ImageVector, badge: String?, badgeOk: Boolean) {
    val shape = RoundedCornerShape(12.dp)
    Row(
        Modifier
            .fillMaxWidth()
            .clip(shape)
            .background(Brush.verticalGradient(listOf(AppColors.Card, AppColors.Card.copy(alpha = 0.85f))))
            .border(1.dp, AppColors.Ring, shape)
            .padding(horizontal = 12.dp, vertical = 10.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(10.dp),
    ) {
        Logo(icon)
        Column(Modifier.weight(1f)) {
            Text(title, fontSize = 16.sp, fontWeight = FontWeight.Bold, color = AppColors.Text)
            Hint(subtitle)
        }
        if (badge != null) {
            val col = if (badgeOk) AppColors.Ok else AppColors.Bad
            Text(
                badge, fontSize = 12.sp, color = col,
                modifier = Modifier
                    .border(1.dp, col.copy(alpha = 0.5f), CircleShape)
                    .padding(horizontal = 10.dp, vertical = 5.dp),
            )
        }
    }
}

@Composable
private fun Logo(icon: ImageVector) {
    val pulse by rememberInfiniteTransition(label = "pulse")
        .animateFloat(0f, 1f, infiniteRepeatable(tween(2400, easing = LinearOutSlowInEasing)), label = "p")
    Box(
        Modifier
            .size(28.dp)
            .drawBehind {
                // wie @keyframes pulse: Ring wächst auf 12 px und blendet bei 70 % aus
                val k = (pulse / 0.7f).coerceAtMost(1f)
                val spread = k * 12.dp.toPx()
                drawRoundRect(
                    AppColors.Accent.copy(alpha = 0.35f * (1f - k)),
                    topLeft = Offset(-spread, -spread),
                    size = Size(size.width + 2 * spread, size.height + 2 * spread),
                    cornerRadius = CornerRadius(8.dp.toPx() + spread),
                )
            }
            .clip(RoundedCornerShape(8.dp))
            .background(AppColors.Gradient),
        contentAlignment = Alignment.Center,
    ) {
        Icon(icon, null, Modifier.size(18.dp), tint = Color.White)
    }
}

// ── Kacheln, Chips, Verlauf (.tiles / .chip / .spark) ────────────────────────

/** Zwei Kacheln nebeneinander, gleich hoch. */
@Composable
fun TileRow(content: @Composable RowScope.() -> Unit) {
    Row(
        Modifier.fillMaxWidth().height(IntrinsicSize.Min),
        horizontalArrangement = Arrangement.spacedBy(8.dp),
        content = content,
    )
}

@Composable
fun RowScope.Tile(
    label: String,
    value: String,
    sub: String? = null,
    error: Boolean = false,
    history: List<Double?>? = null,
    content: (@Composable () -> Unit)? = null,
) {
    Column(
        Modifier
            .weight(1f)
            .fillMaxHeight()
            .clip(TileShape)
            .background(AppColors.Tile)
            .border(1.dp, AppColors.TileRing, TileShape)
            .padding(horizontal = 12.dp, vertical = 10.dp),
    ) {
        Text(label, fontSize = 12.sp, color = AppColors.Muted)
        if (content != null) content()
        else Text(
            value, fontSize = 20.sp, fontWeight = FontWeight.SemiBold,
            color = if (error) AppColors.Bad else AppColors.Text,
            modifier = Modifier.padding(top = 2.dp),
        )
        if (!sub.isNullOrBlank()) Text(sub, fontSize = 12.sp, color = AppColors.Muted, modifier = Modifier.padding(top = 2.dp))
        if (history != null) Spark(history)
    }
}

const val HISTORY_POINTS = 60

/** Verlaufslinie über min…max der gesammelten Werte; fehlende Werte unterbrechen die Linie. */
@Composable
fun Spark(values: List<Double?>) {
    Canvas(Modifier.fillMaxWidth().padding(top = 6.dp).height(34.dp)) {
        val pts = values.filterNotNull()
        if (pts.size < 2) return@Canvas
        var lo = pts.min()
        var hi = pts.max()
        if (hi - lo < 1e-9) { lo -= 0.5; hi += 0.5 }
        val path = Path()
        var pen = false
        values.forEachIndexed { i, v ->
            if (v == null) { pen = false; return@forEachIndexed }
            val x = i / (HISTORY_POINTS - 1f) * size.width
            val y = (30f - ((v - lo) / (hi - lo)).toFloat() * 26f) / 34f * size.height
            if (pen) path.lineTo(x, y) else path.moveTo(x, y)
            pen = true
        }
        drawPath(path, AppColors.Accent, style = Stroke(1.5.dp.toPx(), cap = StrokeCap.Round, join = StrokeJoin.Round))
    }
}

@Composable
fun Swatch(color: Color?, size: Int = 18) {
    Box(
        Modifier
            .size(size.dp)
            .clip(RoundedCornerShape(6.dp))
            .background(color ?: Color.Transparent)
            .border(1.dp, Color.White.copy(alpha = 0.25f), RoundedCornerShape(6.dp)),
    )
}

@Composable
fun Chip(text: String, on: Boolean, color: Color? = null) {
    val shape = CircleShape
    Row(
        Modifier
            .clip(shape)
            .background(AppColors.Tile)
            .border(1.dp, if (on) Color(0xFF2D5AA8) else AppColors.TileRing, shape)
            .padding(start = 6.dp, end = 10.dp, top = 4.dp, bottom = 4.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(6.dp),
    ) {
        if (color != null) Swatch(color)
        Text(text, fontSize = 13.sp, color = AppColors.Text)
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
fun Chips(content: @Composable () -> Unit) {
    FlowRow(
        Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.spacedBy(8.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp),
    ) { content() }
}

@Composable
fun StatusDot(on: Boolean) {
    Box(Modifier.size(9.dp).clip(CircleShape).background(if (on) AppColors.Ok else AppColors.Bad))
}

// ── Eingabe (.input / .btn / .row) ───────────────────────────────────────────

@Composable
private fun InputBox(modifier: Modifier = Modifier, focused: Boolean = false, content: @Composable BoxScope.() -> Unit) {
    Box(
        modifier
            .fillMaxWidth()
            .heightIn(min = 44.dp)
            .clip(InputShape)
            .background(AppColors.Input)
            .border(1.dp, if (focused) AppColors.InputFocus else AppColors.InputRing, InputShape)
            .padding(horizontal = 12.dp, vertical = 10.dp),
        contentAlignment = Alignment.CenterStart,
        content = content,
    )
}

@Composable
fun FieldLabel(text: String) {
    Text(text, fontSize = 14.sp, color = AppColors.Label, modifier = Modifier.padding(bottom = 4.dp))
}

@Composable
fun AppField(
    value: String,
    onChange: (String) -> Unit,
    modifier: Modifier = Modifier,
    label: String? = null,
    placeholder: String? = null,
    keyboard: KeyboardType = KeyboardType.Text,
    password: Boolean = false,
    suffix: String? = null,
) {
    var focused by remember { mutableStateOf(false) }
    Column(modifier) {
        if (label != null) FieldLabel(label)
        Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            BasicTextField(
                value, onChange,
                modifier = Modifier.weight(1f).onFocusChanged { focused = it.isFocused },
                singleLine = true,
                textStyle = TextStyle(color = AppColors.Text, fontSize = 16.sp),
                cursorBrush = SolidColor(AppColors.Accent),
                keyboardOptions = KeyboardOptions(keyboardType = keyboard),
                visualTransformation = if (password) PasswordVisualTransformation() else VisualTransformation.None,
                decorationBox = { inner ->
                    InputBox(focused = focused) {
                        if (value.isEmpty() && placeholder != null) {
                            Text(placeholder, fontSize = 16.sp, color = AppColors.Muted.copy(alpha = 0.6f))
                        }
                        inner()
                    }
                },
            )
            if (suffix != null) Text(suffix, fontSize = 14.sp, color = AppColors.Muted)
        }
    }
}

@Composable
fun AppButton(
    text: String,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    primary: Boolean = false,
    ghost: Boolean = false,
    enabled: Boolean = true,
) {
    val shape = InputShape
    Box(
        modifier
            .alpha(if (enabled) 1f else 0.6f)
            .heightIn(min = 44.dp)
            .clip(shape)
            .let {
                when {
                    primary -> it.background(AppColors.Gradient)
                    ghost -> it.border(1.dp, AppColors.BtnRing, shape)
                    else -> it.background(AppColors.BtnBg).border(1.dp, AppColors.BtnRing, shape)
                }
            }
            .clickable(enabled = enabled, onClick = onClick)
            .padding(horizontal = 14.dp, vertical = 10.dp),
        contentAlignment = Alignment.Center,
    ) {
        Text(text, fontSize = 16.sp, color = if (primary) Color.White else Color(0xFFE7F0FF), textAlign = TextAlign.Center)
    }
}

/** Betriebsart-Auswahl (UHR / EIN / AUS): aktive Taste im Verlauf. */
@Composable
fun ModeRow(labels: List<String>, selected: Int, onSelect: (Int) -> Unit) {
    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
        labels.forEachIndexed { i, l ->
            AppButton(l, { onSelect(i) }, Modifier.weight(1f), primary = selected == i)
        }
    }
}

@Composable
fun SwitchRow(label: String, checked: Boolean, onChange: (Boolean) -> Unit) {
    Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
        Text(label, Modifier.weight(1f), fontSize = 14.sp, color = AppColors.Label)
        Switch(
            checked, onChange,
            colors = SwitchDefaults.colors(
                checkedTrackColor = AppColors.Accent2, checkedThumbColor = Color.White,
                uncheckedTrackColor = AppColors.Input, uncheckedBorderColor = AppColors.InputRing,
                uncheckedThumbColor = AppColors.Muted,
            ),
        )
    }
}

@Composable
fun <T> AppDropdown(
    selected: String,
    options: List<Pair<T, String>>,
    onSelect: (T) -> Unit,
    modifier: Modifier = Modifier,
    label: String? = null,
) {
    var open by remember { mutableStateOf(false) }
    Column(modifier) {
        if (label != null) FieldLabel(label)
        Box {
            InputBox(Modifier.clickable { open = true }) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text(selected, Modifier.weight(1f), fontSize = 16.sp, color = AppColors.Text)
                    Icon(Icons.Filled.ArrowDropDown, null, tint = AppColors.Muted)
                }
            }
            DropdownMenu(open, { open = false }) {
                options.forEach { (v, t) ->
                    DropdownMenuItem(text = { Text(t) }, onClick = { onSelect(v); open = false })
                }
            }
        }
    }
}

// ── Dialoge: Farbe und Uhrzeit (ersetzen <input type=color|time>) ────────────

@Composable
private fun AppDialog(onDismiss: () -> Unit, content: @Composable ColumnScope.() -> Unit) {
    Dialog(onDismiss, DialogProperties(usePlatformDefaultWidth = false)) {
        Surface(
            Modifier.padding(24.dp),
            shape = CardShape, color = AppColors.Card,
            border = androidx.compose.foundation.BorderStroke(1.dp, AppColors.Ring),
        ) {
            Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(12.dp), content = content)
        }
    }
}

@Composable
private fun DialogButtons(onCancel: () -> Unit, onOk: () -> Unit) {
    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.End) {
        TextButton(onCancel) { Text("Abbrechen", color = AppColors.Muted) }
        TextButton(onOk) { Text("OK", color = AppColors.Accent) }
    }
}

internal fun parseHex(hex: String): Color? = runCatching { Color(android.graphics.Color.parseColor(hex)) }.getOrNull()

private fun toHex(c: Color) =
    String.format(Locale.US, "#%02x%02x%02x", (c.red * 255).roundToInt(), (c.green * 255).roundToInt(), (c.blue * 255).roundToInt())

/** Farbwahl als Feld mit Vorschau; Tippen öffnet RGB-Regler. Wert im Format #rrggbb. */
@Composable
fun ColorField(label: String, hex: String, onChange: (String) -> Unit, modifier: Modifier = Modifier) {
    var open by remember { mutableStateOf(false) }
    Column(modifier) {
        FieldLabel(label)
        Box(
            Modifier
                .width(64.dp).height(40.dp)
                .clip(InputShape)
                .background(AppColors.Input)
                .border(1.dp, AppColors.InputRing, InputShape)
                .padding(3.dp)
                .clickable { open = true },
        ) {
            Box(Modifier.fillMaxSize().clip(RoundedCornerShape(7.dp)).background(parseHex(hex) ?: Color.Transparent))
        }
    }
    if (open) ColorPickerDialog(parseHex(hex) ?: Color.Black, { open = false }) { onChange(it); open = false }
}

@Composable
private fun ColorPickerDialog(initial: Color, onDismiss: () -> Unit, onPick: (String) -> Unit) {
    var r by remember { mutableFloatStateOf(initial.red * 255) }
    var g by remember { mutableFloatStateOf(initial.green * 255) }
    var b by remember { mutableFloatStateOf(initial.blue * 255) }
    val current = Color(r.roundToInt(), g.roundToInt(), b.roundToInt())
    AppDialog(onDismiss) {
        Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(12.dp)) {
            Swatch(current, 44)
            Text(toHex(current), fontSize = 18.sp, fontWeight = FontWeight.SemiBold, color = AppColors.Text)
        }
        ChannelSlider("R", r, Color(0xFFFF5A5A)) { r = it }
        ChannelSlider("G", g, Color(0xFF4ADE80)) { g = it }
        ChannelSlider("B", b, Color(0xFF60A5FA)) { b = it }
        DialogButtons(onDismiss) { onPick(toHex(current)) }
    }
}

@Composable
private fun ChannelSlider(name: String, value: Float, color: Color, onChange: (Float) -> Unit) {
    Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        Text(name, color = AppColors.Label, modifier = Modifier.width(16.dp))
        Slider(
            value, onChange, Modifier.width(240.dp), valueRange = 0f..255f,
            colors = SliderDefaults.colors(thumbColor = color, activeTrackColor = color, inactiveTrackColor = AppColors.Ring),
        )
        Text(value.roundToInt().toString(), color = AppColors.Muted, fontSize = 12.sp, modifier = Modifier.width(28.dp))
    }
}

/** Uhrzeit HH:MM als Feld; Tippen öffnet den Zeitwähler. */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun TimeField(value: String, onChange: (String) -> Unit, modifier: Modifier = Modifier) {
    var open by remember { mutableStateOf(false) }
    InputBox(modifier.clickable { open = true }) {
        Text(value.ifEmpty { "--:--" }, fontSize = 16.sp, color = AppColors.Text)
    }
    if (open) {
        val parts = value.split(":")
        val state = rememberTimePickerState(parts.getOrNull(0)?.toIntOrNull() ?: 0, parts.getOrNull(1)?.toIntOrNull() ?: 0, true)
        AppDialog({ open = false }) {
            TimePicker(state)
            DialogButtons({ open = false }) {
                onChange(String.format(Locale.US, "%02d:%02d", state.hour, state.minute))
                open = false
            }
        }
    }
}

// ── JSON/Format-Helfer ───────────────────────────────────────────────────────

/** Zahl aus JSON; null/fehlend (Sensor nicht verfügbar) → null. */
internal fun JSONObject.dbl(key: String): Double? =
    if (!has(key) || isNull(key)) null else optDouble(key).takeIf { !it.isNaN() }

internal fun fmt(v: Double?, decimals: Int, unit: String = ""): String =
    if (v == null) "–" else String.format(Locale.GERMANY, "%.${decimals}f", v) + if (unit.isEmpty()) "" else " $unit"
