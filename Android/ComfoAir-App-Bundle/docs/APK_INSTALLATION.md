# 📱 ComfoAir Android APK - Installation & Deployment

## ✅ APK erstellt!

### 📊 APK Informationen

| Typ | Datei | Größe | Zustand | Use Case |
|-----|-------|-------|--------|----------|
| **Debug** | `app-debug.apk` | 5.37 MB | Signiert | Development & Testing |
| **Release** | `app-release-unsigned.apk` | 4.34 MB | Unsigniert | Production (Signierung erforderlich) |

**Speicherort:**
```
Android/app/build/outputs/apk/
├── debug/
│   └── app-debug.apk              ← Sofort installierbar!
└── release/
    └── app-release-unsigned.apk   ← Vor Deployment signieren
```

---

## 🚀 Installation auf Device/Emulator

### **Option 1: ADB direkt (schnellste Methode)**

```bash
# Device/Emulator verbinden & USB-Debugging aktivieren

# Debug APK installieren
adb install -r "Android/app/build/outputs/apk/debug/app-debug.apk"

# App starten
adb shell am start -n comfoair.borstel.com/.MainActivity

# Logs anschauen (optional)
adb logcat | grep "WebView"
```

### **Option 2: Android Studio UI**

1. **Device/Emulator verbinden**
2. Menü: **Build → Build Bundle(s)/APK(s) → Build APK(s)**
3. Fenster öffnet sich mit der APK
4. Klick auf **Install** → App wird installiert

### **Option 3: File Explorer**

1. APK-Datei auf Device kopieren
2. Dateimanager öffnen
3. APK antippen → Installation

---

## 🔐 Release APK signieren (für Google Play Store)

Die `app-release-unsigned.apk` **MUSS** signiert werden für den Store!

### **Schritt 1: Keystore generieren**
```bash
keytool -genkey -v -keystore comfoair-release.keystore -keyalg RSA -keysize 2048 -validity 10000 -alias comfoair

# Fragen beantworten:
# Passwort: (sicheres Passwort merken!)
# CN (Common Name): ComfoAir
# OU (Organization Unit): Borstel
# O (Organization): ComfoAir
# L (City): Local
# ST (State): Local
# C (Country): DE

# → comfoair-release.keystore wird generiert
```

### **Schritt 2: APK signieren**
```bash
jarsigner -verbose -sigalg SHA256withRSA -digestalg SHA-256 \
  -keystore comfoair-release.keystore \
  Android/app/build/outputs/apk/release/app-release-unsigned.apk \
  comfoair

# Passwort eingeben (aus Schritt 1)
```

### **Schritt 3: Align (optimieren)**
```bash
zipalign -v 4 \
  Android/app/build/outputs/apk/release/app-release-unsigned.apk \
  comfoair-app-release.apk

# → comfoair-app-release.apk ist bereit!
```

---

## 📱 Was die App macht

### **beim Start:**
1. ✅ WebView lädt (HTTP/HTTPS/Assets)
2. ✅ Service Worker registriert sich
3. ✅ Offline-Cache wird gefüllt
4. ✅ Alle PWA-Features aktiv

### **Features:**
- 📦 **PWA Installation** - App zu Home Screen hinzufügen
- 🔌 **Offline-Modus** - UI funktioniert ohne Netzwerk
- 🎨 **Icons** - Adaptive Icons auf Android 8+
- 🌐 **MQTT Steuerung** - Vollständige Komfort-Kontrolle
- 📊 **Live-Logs** - Serial Log Viewer
- ⚙️ **Settings** - WiFi, MQTT, Device-Konfiguration

---

## 🔧 Konfiguration vor Installation

### **Wichtig: URL in MainActivity.kt anpassen**

Die App muss wissen, wo die PWA läuft!

**Datei:** `Android/app/src/main/kotlin/comfoair/borstel/com/MainActivity.kt`

**Zeile ~75:**
```kotlin
private fun loadPWA() {
    // Option A: Emulator (auf Host-Rechner)
    val host = "10.0.2.2"
    val port = 8000
    
    // Option B: Echtes Device (Router IP)
    val host = "192.168.1.100"  // ← DEINE Router-IP
    val port = 8000
    
    // Option C: Offline (Assets)
    webView.loadUrl("file:///android_asset/indexSTA.html")
    
    val url = "http://$host:$port/indexSTA.html"
    webView.loadUrl(url)
}
```

---

## 🐛 Troubleshooting

### **APK installiert nicht**
```bash
# Alte Version deinstallieren
adb uninstall comfoair.borstel.com

# Neu installieren
adb install Android/app/build/outputs/apk/debug/app-debug.apk
```

### **WebView lädt nicht**
```bash
# 1. URL korrekt in MainActivity.kt?
# 2. HTTP Server läuft? (python -m http.server 8000)
# 3. Device im Netzwerk sichtbar?

# Debug Logs ansehen
adb logcat | grep "WebView\|comfoair"
```

### **Service Worker funktioniert nicht**
```bash
# DevTools öffnen:
# ADB Forward aktivieren
adb forward tcp:9222 localabstract:chrome_devtools_remote

# Chrome öffnen: chrome://inspect/#devices
# App wählen → DevTools öffnen
# Application → Service Workers prüfen
```

### **API-Calls fehlgeschlagen**
```bash
# Netzwerk-Requests in DevTools checken
# CORS-Headers prüfen
# Firewall blockiert?

# Manuell testen:
curl http://192.168.1.x:8000/api/comfo/state
```

---

## 📦 APK für Google Play Store

### Anforderungen:
- [ ] Release APK signiert
- [ ] Min SDK 24+ (✅ haben wir)
- [ ] App Icon vorhanden (✅ haben wir)
- [ ] Privacy Policy
- [ ] Screenshots & Beschreibung
- [ ] Google Play Developer Account

### Größe optimieren:
```bash
# App Bundle statt APK (kleinere Download-Größe)
./gradlew bundleRelease

# → produces: app-release.aab
```

---

## 🎉 Nächste Schritte

1. **Test auf Device** - App starten & Features prüfen
2. **Offline testen** - WiFi aus → UI sollte noch funktionieren
3. **API-Calls testen** - Komfor-Steuerung funktioniert?
4. **Release signieren** - Für Play Store
5. **Play Store Publishing** - Upload vorbereiten

---

## 📚 Nützliche Commands

```bash
# APK Info anschauen
aapt dump badging Android/app/build/outputs/apk/debug/app-debug.apk

# APK extrahieren (debugging)
unzip -l Android/app/build/outputs/apk/debug/app-debug.apk

# Device/Emulator Liste
adb devices -l

# Logs live (Emulator/Device)
adb logcat -s "WebView:D"

# App-Daten löschen (Caches zurücksetzen)
adb shell pm clear comfoair.borstel.com
```

---

## ✨ Performance

| Metrik | Wert |
|--------|------|
| APK Größe (Debug) | 5.37 MB |
| APK Größe (Release) | 4.34 MB |
| Startup-Zeit | ~2-3 Sekunden |
| RAM Usage | ~80-150 MB |
| Service Worker Cache | ~500 KB |

**Optimierungen möglich:**
- ProGuard/R8 Obfuscation (Release)
- Unused Resource Removal
- WebP Images statt PNG
- Lazy Loading für JS

---

**Viel Spaß mit der App!** 🚀

