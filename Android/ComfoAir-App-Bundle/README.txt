# 📱 ComfoAir Android App - Installationspaket

## 🎉 Willkommen!

Dieses Paket enthält alles, was du brauchst um die **ComfoAir Progressive Web App** als native Android-App zu installieren.

---

## 📦 Paketinhalt

```
ComfoAir-App-Bundle/
├── apk/                          # Installierbare APK Dateien
│   ├── ComfoAir-Debug.apk        ✅ Sofort installierbar!
│   └── ComfoAir-Release-Unsigned.apk  (braucht Signierung für Play Store)
│
├── docs/                         # Dokumentation
│   ├── QUICK_START.md            ⚡ Schnelleinstieg (3 Minuten)
│   ├── APK_INSTALLATION.md       📱 Detaillierte Installation
│   ├── ANDROID_APP_GUIDE.md      🔧 Technischer Guide
│   └── PWA_README.md             💻 Web App Dokumentation
│
├── scripts/                      # Installation & Setup
│   ├── install-apk.bat           (Windows - Doppelklick!)
│   └── install-apk.sh            (Linux/Mac)
│
├── assets/                       # PWA Ressourcen
│   ├── manifest.json             (App-Metadaten)
│   ├── service-worker.js         (Offline-Caching)
│   └── Zehnder-*.png             (App Icons)
│
└── README.txt                    (diese Datei)
```

---

## 🚀 Schnellstart (2 Minuten)

### Option 1: Windows (einfachste Methode)

1. **Device anschließen** (oder Emulator starten)
2. **`scripts/install-apk.bat`** doppelt klicken
3. ✅ Fertig! App startet automatisch

### Option 2: Manuell (ADB)

```bash
# Terminal/CMD öffnen

# App installieren
adb install -r apk/ComfoAir-Debug.apk

# App starten
adb shell am start -n comfoair.borstel.com/.MainActivity
```

### Option 3: Android Studio

1. Android Studio öffnen
2. **File → Open** → Android-Projekt-Root
3. **Run → Run 'app'**

---

## 📋 Voraussetzungen

- **Android Device** (min. Android 7.0 / API 24)
- **oder** Android Emulator
- **ADB** (Android Debug Bridge) - optional für manuelles Setup
- **USB Debugging** aktiviert (auf dem Device)

### ADB installieren (falls nicht vorhanden)

**Windows:**
```bash
# Mit Chocolatey
choco install android-sdk

# Oder von hier: https://developer.android.com/studio/releases/platform-tools
```

**Mac:**
```bash
brew install android-platform-tools
```

**Linux:**
```bash
sudo apt install android-tools-adb
```

---

## 📱 Was macht die App?

### Features
✅ **PWA Installation** - Zu Home Screen hinzufügen
✅ **Offline-Modus** - Funktioniert ohne Netzwerk
✅ **MQTT Steuerung** - Vollständige Komfort-Kontrolle
✅ **WiFi Manager** - Device-Konfiguration
✅ **Live Logs** - Serial Log Viewer
✅ **Adaptive Icons** - Schicke Darstellung

### Screenshot-Struktur
- **Start Screen** - WiFi STA oder AP Mode
- **Komfort** - ComfoAir 350 Steuerung
- **Settings** - WiFi, MQTT, Device Konfiguration
- **Logs** - Serial Monitor

---

## 🔧 Konfiguration

### URL anpassen

Die App sucht die Web-UI unter `http://localhost:8000` - das funktioniert nicht auf dem Device!

**Lösung:** Du musst entweder:

1. **Einen HTTP-Server starten** (auf PC mit IP z.B. 192.168.1.100):
   ```bash
   cd sampledata/
   python -m http.server 8000
   ```

2. **Oder die URL in der App anpassen:**
   - Projekt in Android Studio öffnen
   - `app/src/main/kotlin/comfoair/borstel/com/MainActivity.kt` öffnen
   - Zeile ~75 - `host` Variable anpassen:
   ```kotlin
   val host = "192.168.1.100"  // ← Deine PC-IP
   ```
   - Neu bauen: `./gradlew assembleDebug`

---

## 🐛 Troubleshooting

### "Unbekannte Quelle" / Installation blockiert

**Problem:** Android blockiert Installation von unbekannten Quellen

**Lösung:**
1. Settings → Apps & Notifications → Special App Access
2. "Install unknown apps" → deine File-App aktivieren
3. Erneut versuchen

### App startet aber WebView lädt nicht

**Problem:** Schwarzer Screen

**Checks:**
- [ ] HTTP-Server läuft? (`python -m http.server 8000`)
- [ ] Device im gleichen Netzwerk?
- [ ] Firewall blockiert Port 8000?
- [ ] Richtige IP in MainActivity.kt?

**Debug:**
```bash
adb logcat | grep "WebView"
# Fehlerausgaben prüfen
```

### APK Installation schlägt fehl

```bash
# Alte Version deinstallieren
adb uninstall comfoair.borstel.com

# Nochmal versuchen
adb install -r apk/ComfoAir-Debug.apk
```

---

## 🔐 Release für Google Play Store

Die `ComfoAir-Release-Unsigned.apk` ist **noch NICHT signiert**!

Für den Play Store:

1. Keystore generieren (einmalig):
   ```bash
   keytool -genkey -v -keystore comfoair-release.keystore \
     -keyalg RSA -keysize 2048 -validity 10000 -alias comfoair
   ```

2. APK signieren:
   ```bash
   jarsigner -verbose -sigalg SHA256withRSA -digestalg SHA-256 \
     -keystore comfoair-release.keystore \
     apk/ComfoAir-Release-Unsigned.apk comfoair
   ```

3. Weitere Infos in `docs/APK_INSTALLATION.md`

---

## 📚 Dokumentation

- **QUICK_START.md** - 3 Optionen zum Installieren
- **APK_INSTALLATION.md** - Signierung & Play Store
- **ANDROID_APP_GUIDE.md** - Technischer Deep-Dive
- **PWA_README.md** - Web App Konfiguration

---

## 🎯 Nächste Schritte

- [ ] App installieren & testen
- [ ] Offline-Modus prüfen
- [ ] MQTT Steuerung konfigurieren
- [ ] Icons anpassen
- [ ] Für Play Store signieren (optional)

---

## 💬 Support

Falls Probleme auftreten:

1. **Logs anschauen:** `adb logcat | grep comfoair`
2. **Dokumentation lesen:** siehe `docs/`
3. **Google suchen:** "Android WebView [dein Problem]"

---

## ✨ Credits

- **Basiert auf:** ComfoAir 350 MQTT ESP32 Projekt
- **Framework:** Progressive Web App + Kotlin
- **Icons:** Zehnder (adaptiert)

---

**Viel Spaß mit der App!** 🚀

Ver. 1.0 | Juni 2026

