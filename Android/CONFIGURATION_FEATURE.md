# 🚀 ComfoAir App v1.1 - Server Konfiguration Feature

## ✨ Neue Features

### ✅ Dynamische Server-URL Eingabe
- Beim ersten Start: Dialog mit Host & Port Eingabe
- **Speichert automatisch** für nächsten Start
- Standardwert: `192.168.1.100:8000`

### ✅ Settings Activity
- Native Android UI für Einstellungen
- Host/IP Adresse ändern
- Port anpassen
- Validierung vor Speichern
- Live-Feedback (✅ oder ❌)

### ✅ JavaScript Bridge
- Web-App kann `ComfoAir.openSettings()` aufrufen
- Settings aus Web-UI öffnen
- Seamless Integration

---

## 🎯 Wie es funktioniert

### 1️⃣ **Beim ersten Start**
```
App startet
    ↓
Prüfe SharedPreferences
    ↓
Wenn leer: Zeige Eingabe-Dialog
    ↓
Nutzer gibt 192.168.1.100:8000 ein
    ↓
Speichere in SharedPreferences
    ↓
Lade PWA von der Adresse
```

### 2️⃣ **Nächster Start**
```
App startet
    ↓
Prüfe SharedPreferences
    ↓
Wenn gespeichert: Lade PWA direkt
    ↓
Schneller Start! (Keine Eingabe)
```

### 3️⃣ **Settings ändern**
```
Benutzer öffnet Settings Activity
    ↓
Sieht aktuelle Werte
    ↓
Ändert Host/Port
    ↓
Klick "Speichern"
    ↓
Validierung prüft Eingaben
    ↓
Speichert neue Werte
    ↓
Beim Neustart: Neue Adresse wird genutzt
```

---

## 📱 Neue Komponenten

### MainActivity.kt
```kotlin
// SharedPreferences für Persistenz
private lateinit var prefs: SharedPreferences

// Erste Installation prüfen
if (!hasValidServerConfig()) {
    showFirstLaunchDialog()
}

// Eingabe-Dialog mit Host & Port
private fun showFirstLaunchDialog() { ... }

// JavaScript Bridge für Web-App
webView.addJavascriptInterface(JavaScriptBridge(this), "ComfoAir")
```

### SettingsActivity.kt (NEU)
```kotlin
// Native Android Settings UI
// - Host/IP Eingabe
// - Port Eingabe
// - Validierung (Host nicht leer, Port ist Zahl)
// - Speichern & Feedback
```

### AndroidManifest.xml
```xml
<!-- SettingsActivity registrieren -->
<activity
    android:name=".SettingsActivity"
    android:parentActivityName=".MainActivity" />
```

---

## 💾 Datenspeicherung (SharedPreferences)

Die App speichert lokal:

```
SharedPreferences: "ComfoAirPreferences"
├── server_host    = "192.168.1.100"  (String)
├── server_port    = "8000"           (String)
└── server_protocol = "http"          (String)
```

Speicherort auf Device:
```
/data/data/comfoair.borstel.com/shared_prefs/ComfoAirPreferences.xml
```

---

## 🔧 Verwendung aus Web-App

Die PWA kann jetzt Settings öffnen:

```javascript
// Aus einer HTML-Seite:
if (window.ComfoAir) {
    ComfoAir.openSettings();  // Öffnet Settings Activity
}
```

Beispiel HTML:
```html
<button onclick="ComfoAir.openSettings()">
    ⚙️ Einstellungen
</button>
```

---

## ✅ Features (Implementiert)

- ✓ Dialog beim ersten Start
- ✓ SharedPreferences Speicherung
- ✓ Native Settings Activity
- ✓ Validierung (Host nicht leer, Port ist Zahl)
- ✓ JavaScript Bridge
- ✓ Error-Handling
- ✓ User-Feedback (✅/❌)
- ✓ Automatisches Speichern

---

## 🚀 Installation & Test

### 1. Neue APK bauen
```bash
cd Android
./gradlew assembleDebug
```

### 2. Installieren
```bash
adb install -r app/build/outputs/apk/debug/app-debug.apk
```

### 3. Test-Szenarien

**Szenario A: Erste Installation**
```
1. App starten
2. Dialog erscheint: "ComfoAir Server Konfiguration"
3. Gib "192.168.1.100" als Host ein
4. Gib "8000" als Port ein
5. Klick "Verbinden"
6. PWA lädt von http://192.168.1.100:8000
```

**Szenario B: Zweiter Start**
```
1. App starten
2. Dialog NICHT gezeigt (gespeicherte Werte genutzt)
3. PWA lädt sofort
```

**Szenario C: Einstellungen ändern**
```
1. App läuft
2. Öffne Settings (z.B. via Menu oder Web-Button)
3. Ändere Host zu "10.0.2.2"
4. Ändere Port zu "8443"
5. Klick "Speichern"
6. ✅ Confirmation
7. Beende App
8. App neu starten → verbindet zu 10.0.2.2:8443
```

---

## 📊 Technische Details

### Gradle Dependencies
```kotlin
// Bereits vorhanden:
implementation(libs.androidx.appcompat)
// Nutzen: AlertDialog für First-Launch-Dialog
```

### API Levels
- Min: API 24 (Android 7.0)
- Target: API 36 (Android 15)
- Getestet auf: API 24+

### Speicher
- SharedPreferences: ~500 Bytes
- Kein zusätzlicher RAM

---

## 🎨 UI Screenshots

### First Launch Dialog
```
┌─────────────────────────────────────┐
│ ComfoAir Server Konfiguration       │
│                                     │
│ Gib die Adresse des                │
│ ComfoAir-Servers ein:              │
│                                     │
│ Host/IP Adresse:                   │
│ [192.168.1.100        ]            │
│                                     │
│ Port:                              │
│ [8000                 ]            │
│                                     │
│ [   Verbinden  ] [  Abbrechen  ]   │
└─────────────────────────────────────┘
```

### Settings Activity
```
┌─────────────────────────────────────┐
│ ComfoAir - Einstellungen            │
│                                     │
│ Server Konfiguration                │
│                                     │
│ Host/IP Adresse:                   │
│ [192.168.1.100        ]            │
│                                     │
│ Port:                              │
│ [8000                 ]            │
│                                     │
│ Neue Einstellungen werden beim     │
│ Neustart angewendet.               │
│                                     │
│ [  Speichern  ] [  Zurück  ]       │
└─────────────────────────────────────┘
```

---

## 🔐 Sicherheit

### ✅ Implementiert
- Input Validierung (Host nicht leer, Port ist Zahl)
- SharedPreferences im privaten App-Verzeichnis
- Keine Hardcoded Credentials

### ⚠️ Zu beachten
- SharedPreferences im Klartext gespeichert
- Für Produktionsumgebung: Verschlüsselung erwägen
- Keine URL-Validation (User kann ungültige URLs eingeben)

---

## 📝 Changelog v1.1

- ✨ **Neu**: Dynamische Server-URL Eingabe
- ✨ **Neu**: Settings Activity
- ✨ **Neu**: JavaScript Bridge
- 🔧 **Verbessert**: First-Launch Experience
- 🐛 **Behoben**: Hardcoded localhost entfernt

---

## 🔄 Update-Pfad

### Für bestehende Nutzer
1. Alte App deinstallieren (optional)
2. Neue APK installieren
3. Beim Start: Konfigurationsdialog zeigt sich
4. Server-Adresse eingeben
5. Fertig!

### Für neue Nutzer
1. App installieren
2. Start: Konfigurationsdialog
3. Sofort einsatzbereit

---

## 🎯 Nächste Verbesserungen

- [ ] History der letzten Server (Dropdown)
- [ ] Connection Test vor Speichern
- [ ] Favorites/Presets
- [ ] Timeout-Konfiguration
- [ ] HTTPS Zertifikat Verwaltung

---

## 📞 Support

**Problem: Dialog wird nicht angezeigt?**
- App-Daten löschen: `adb shell pm clear comfoair.borstel.com`
- Erneut starten

**Problem: Falsche URL gespeichert?**
- Öffne Settings Activity
- Ändere Host/Port
- Speichern

**Problem: App lädt PWA nicht?**
- Prüfe: Host/Port korrekt?
- Ist HTTP-Server läuft?
- Firewall blockiert?
- Check Logs: `adb logcat | grep ComfoAir`

---

**Version:** 1.1  
**Release Date:** 13. Juni 2026  
**Status:** ✅ Production Ready

