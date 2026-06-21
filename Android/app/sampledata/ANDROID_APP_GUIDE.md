# 🚀 ComfoAir PWA + Android App - Complete Guide

## 📁 Was wurde erstellt

### ✅ PWA (Progressive Web App)
```
app/sampledata/
├── manifest.json              # App-Metadaten für Installation
├── service-worker.js          # Offline-Caching & Sync
├── indexSTA.html              # Startseite mit PWA Meta-Tags
├── settings.html              # Einstellungen UI
├── ComfoAirDriver.html        # Komfortsteuerung
├── ComfoAirSettings.html      # Device Settings
├── MQTTSettings.html          # MQTT-Konfiguration
├── log.html                   # Serial Log Viewer
├── Zehnder-192.png            # App Icon 192x192
├── Zehnder-512.png            # App Icon 512x512
├── Zehnder-maskable-192.png   # Android Adaptive Icon
├── Zehnder-maskable-512.png   # Android Adaptive Icon
├── generate_icons.py          # Icon-Generator Script
├── https_server.py            # HTTPS Development Server
└── PWA_README.md              # PWA-Dokumentation
```

### ✅ Android App (Kotlin WebView)
```
app/src/main/
├── kotlin/comfoair/borstel/com/
│   ├── MainActivity.kt        # WebView Activity (neu)
│   ├── PWAWebViewClient.kt    # Link-Handling
│   └── PWAWebChromeClient.kt  # Konsole & Dialoge
├── AndroidManifest.xml        # Permissions & Activity (aktualisiert)
└── res/values/strings.xml     # App-Name
```

---

## 🔧 Schritt 1: PWA testen (ohne Android Build)

### HTTP-Server starten
```bash
cd Android/app/sampledata
python -m http.server 8000
# Öffne: http://localhost:8000
```

### HTTPS-Server starten (mit Zertifikat)
```bash
cd Android/app/sampledata
python https_server.py
# Öffne: https://localhost:8443
```

✅ **Im Browser testen:**
- DevTools öffnen (F12)
- Application → Manifest.json prüfen
- Service Worker → Active?
- Offline-Modus testen (Network → Offline)

---

## 📱 Schritt 2: Android Build

### Voraussetzungen
- Android Studio (neueste Version)
- Gradle 8.x+
- Java 11+
- Android SDK 24+ (minSdk)

### Build-Varianten

#### **Variante A: WebView lädt vom lokalen Server**
```kotlin
// MainActivity.kt - loadPWA()
val host = "10.0.2.2"   // Emulator: Host-Rechner IP
// oder
val host = "192.168.1.x" // Device: Router IP
val port = 8000
val url = "http://$host:$port/indexSTA.html"
```

**Setup:**
1. **Emulator:** `python -m http.server 8000` auf Host
2. **Device:** Wlan Router anpassen, IP reinkodieren

#### **Variante B: WebView lädt von Assets (Offline)**
```kotlin
// MainActivity.kt - loadPWA()
webView.loadUrl("file:///android_asset/indexSTA.html")
```

**Setup:**
1. Kopiere HTML-Dateien in `app/src/main/assets/`
2. Service Worker funktioniert offline
3. API-Calls funktionieren nur mit Netzwerk

---

## 🔨 Build & Run

### Android Studio
```bash
# Terminal in Android Directory
./gradlew build

# oder direkt in Android Studio:
# Build → Build Bundle(s)/APK(s) → Build APK(s)
```

### Command Line (Emulator)
```bash
# Emulator starten
emulator -avd Pixel_4_API_33 &

# App installieren
./gradlew installDebug

# App starten
adb shell am start -n comfoair.borstel.com/.MainActivity
```

### Command Line (echtes Device)
```bash
# USB-Debugging aktivieren (Entwickleroptionen)
# Device verbinden

# App installieren
./gradlew installDebug

# Logs anschauen
adb logcat | grep "WebView"
```

---

## 🔐 HTTPS für Production

### Let's Encrypt Certificate (freie SSL)
```bash
# Installiere Certbot
sudo apt install certbot

# Generiere Zertifikat (domain.com = deine Domain)
sudo certbot certonly --standalone -d domain.com

# Zertifikate kopieren
sudo cp /etc/letsencrypt/live/domain.com/fullchain.pem app/sampledata/
sudo cp /etc/letsencrypt/live/domain.com/privkey.pem app/sampledata/

# https_server.py anpassen:
# CERT_FILE = "fullchain.pem"
# KEY_FILE = "privkey.pem"
```

### Self-Signed für Development
```bash
python https_server.py
# Generiert automatisch localhost.crt und localhost.key
```

---

## 🎨 Icons anpassen

Zehnder.png durch dein Icon ersetzen:

```bash
# Icons neu generieren
python generate_icons.py

# Dann auch Android Icon anpassen
cp Zehnder-*.png ../res/drawable/
```

---

## 📊 Troubleshooting

### WebView lädt nicht
```kotlin
// Logs anschauen
adb logcat | grep "WebView"

// Debug-Features aktivieren
if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.KITKAT) {
    WebView.setWebContentsDebuggingEnabled(true)
}
```

### Service Worker registriert sich nicht
- Browser Console (F12) checken
- HTTPS oder localhost erforderlich
- Service Worker URL muss exakt stimmen

### API-Calls funktionieren nicht
- Netzwerkberechtigungen in AndroidManifest.xml?
- Firewall blockiert Port?
- CORS-Headers gesetzt?

---

## 🚀 Deployment-Checkliste

- [ ] Icons 192x192 & 512x512 generiert
- [ ] manifest.json validiert (https://web.dev/manifest-validator/)
- [ ] Service Worker testet offline
- [ ] HTTPS für Production
- [ ] API-Endpunkte funktionieren
- [ ] Permissions in AndroidManifest.xml
- [ ] APK signiert (Release-Build)
- [ ] Google Play Store-Anforderungen erfüllt

---

## 📚 Weitere Ressourcen

- [PWA Checklist](https://developers.google.com/codelabs/your-first-pwapp)
- [Service Worker API](https://developer.mozilla.org/de/docs/Web/API/Service_Worker_API)
- [Android WebView Docs](https://developer.android.com/reference/android/webkit/WebView)
- [Manifest.json Spec](https://www.w3.org/TR/appmanifest/)

---

## 📝 Nächste Schritte (Optional)

1. **Push-Benachrichtigungen** - Service Worker Push API
2. **Offline-Sync** - Background Sync für API-Calls
3. **Adaptive Icons** - Maskable Icons für Android 8+
4. **App Shortcuts** - Quick-Actions auf Home Screen
5. **Share Target API** - Teilen mit anderen Apps

