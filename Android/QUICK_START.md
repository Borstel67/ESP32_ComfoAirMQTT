# ⚡ Quick Start: ComfoAir PWA + Android App

## 🎯 Ziel erreicht!
Du hast jetzt:
- ✅ Eine installierbare **Progressive Web App (PWA)**
- ✅ Eine **native Android App** mit WebView
- ✅ **Icons** in verschiedenen Größen
- ✅ **HTTPS** mit Self-Signed Zertifikat
- ✅ **Offline-Funktionalität** via Service Worker

---

## 🚀 Los geht's (3 Optionen)

### **Option 1: PWA im Browser testen (5 Min)**
```bash
cd Android/app/sampledata

# HTTP-Server starten
python -m http.server 8000

# Öffne: http://localhost:8000
# Chrome: Install → "Add to Home screen"
```

### **Option 2: PWA mit HTTPS (10 Min)**
```bash
cd Android/app/sampledata

# HTTPS-Server starten (SSL-Cert wird auto-generiert)
python https_server.py

# Öffne: https://localhost:8443
# Browser-Warnung ignorieren (Self-Signed)
```

### **Option 3: Android App bauen (30 Min)**
```bash
# 1. Android Studio öffnen
# 2. Projekt öffnen: Android/
# 3. Device/Emulator verbinden
# 4. Menu: Build → Build APK(s)
# 5. App installiert & startet

# Oder Command Line:
cd Android
./gradlew installDebug
```

---

## 🔗 Die wichtigsten Dateien

| Datei | Zweck |
|-------|-------|
| `app/sampledata/manifest.json` | App-Metadaten |
| `app/sampledata/service-worker.js` | Offline-Caching |
| `app/src/main/kotlin/.../MainActivity.kt` | Android WebView |
| `app/src/main/AndroidManifest.xml` | Permissions & Activities |
| `https_server.py` | HTTPS Dev-Server |

---

## 📱 Android WebView URL anpassen

**🆕 v1.1: Automatisch beim Start konfigurierbar!**

Die App fragt jetzt beim ersten Start nach der Server-Adresse:

```
App starten
    ↓
Dialog: "Host/IP Adresse?" → z.B. 192.168.1.100
         "Port?" → z.B. 8000
    ↓
Speichert automatisch
    ↓
Nächster Start: Keine Eingabe nötig!
```

**Einstellungen später ändern:**
- Öffne Settings Activity (über Web-UI oder direkt)
- Ändere Host/Port
- Speichern
- Beim Neustart: Neue Adresse wird genutzt

Siehe: `CONFIGURATION_FEATURE.md` für Details

---

## ✅ Checkliste zum Starten

- [ ] Python HTTP-Server funktioniert
- [ ] PWA im Chrome installierbar
- [ ] Service Worker offline verfügbar
- [ ] Android Studio öffnet Projekt
- [ ] Gradle Build erfolgreich
- [ ] App startet auf Device/Emulator

---

## 🎉 Fertig!

Deine ComfoAir PWA ist jetzt:
- 📦 **Installierbar** auf Android
- 🔌 **Offline-fähig** via Service Worker
- 🎨 **Schick** mit Icons & Theme
- 🔒 **Secure** mit HTTPS-Zertifikat
- 🎯 **🆕 v1.1: Konfigurierbar!** Server-URL beim Start
- ⚙️ **🆕 Settings Activity** für Änderungen
- 🚀 **Production-ready** (mit kleinen Anpassungen)

---

## 🆕 v1.1 - Highlights

- ✨ Dynamische Server-Konfiguration (kein Hardcode mehr!)
- ✨ Settings Activity für Host/Port Änderungen
- ✨ Automatisches Speichern in SharedPreferences
- ✨ JavaScript Bridge für Web-UI Integration

Siehe: `CONFIGURATION_FEATURE.md`

---

## 📚 Nächste Schritte

1. **Push-Benachrichtigungen** - Service Worker Push API
2. **Datenbank** - IndexedDB für offline Daten
3. **App-Store** - Google Play Store Publishing
4. **Branding** - Custom Icons & Splash Screen
5. **Analytics** - Google Analytics für PWA

Siehe: `ANDROID_APP_GUIDE.md` & `PWA_README.md`

