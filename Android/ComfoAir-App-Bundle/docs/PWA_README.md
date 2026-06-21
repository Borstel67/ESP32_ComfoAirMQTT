# PWA-Installation & Testing

## 🚀 **Wie man die PWA benutzt**

### 1. **Lokales Testing (Desktop)**
```bash
# Option A: Python SimpleHTTPServer
cd Android/app/sampledata
python -m http.server 8000

# Option B: Node http-server (falls installiert)
npx http-server -p 8000
```
Dann in Browser: `http://localhost:8000`

### 2. **Android testen**
- Öffne `http://192.168.x.x:8000` auf Android-Gerät
- Chrome sollte Installations-Prompt zeigen
- Oder: Menu → "Add to Home screen"

### 3. **Offline-Modus testen**
1. App installt haben
2. DevTools öffnen (F12)
3. Network → "Offline" checkbox aktivieren
4. Seite neuladen → sollte aus Cache laden
5. Nur API-Calls schlagen fehl (normal)

---

## 📦 **Was wurde hinzugefügt**

| Datei | Zweck |
|-------|-------|
| `manifest.json` | App-Metadaten (Name, Icons, Display-Mode) |
| `service-worker.js` | Offline-Caching & Background-Sync |
| HTML Meta-Tags | PWA-Installierbarkeit & Mobile-Optimierung |

---

## 🔑 **Service Worker Logik**

✅ **GECACHT** (offline verfügbar):
- HTML-Dateien (indexSTA.html, settings.html, etc.)
- CSS (WifiManager.css)
- PNG-Bilder (Zehnder.png)

❌ **NICHT GECACHT** (erfordern Netzwerk):
- API-Calls: `/api/comfo/state`, `/api/mqtt/*`, etc.
- Live-Sensordaten

---

## 🎨 **Icons einrichten**

Falls `Zehnder.png` zu klein ist, erstelle verschiedene Größen:
```bash
# Mit ImageMagick
convert Zehnder.png -resize 192x192 Zehnder-192.png
convert Zehnder.png -resize 512x512 Zehnder-512.png
convert Zehnder.png -resize 192x192 -background white -alpha remove Zehnder-maskable-192.png
convert Zehnder.png -resize 512x512 -background white -alpha remove Zehnder-maskable-512.png
```

---

## 🔗 **Nächste Schritte (optional)**

1. **Android Wrapper** - WebView-App in Kotlin
2. **Push-Benachrichtigungen** - Service Worker Push API
3. **HTTPS** - Empfohlen für Production (viele Features benötigen HTTPS)
4. **Web App Icon** - Custom Icon für verschiedene Geräte-DPI

---

## ✨ **Besonderheiten dieser PWA**

- 🌍 **Responsive Design** - Funktioniert auf Mobile & Desktop
- 📱 **Install-prompt** - Nutzer können zur Home Screen hinzufügen
- 🔌 **Offline-first** - UI lädt auch ohne Netzwerk
- 🎯 **Smart Caching** - API-Calls nicht gecacht (immer fresh)
- 🎨 **Theme-Color** - Status-Bar in App-Farbe

