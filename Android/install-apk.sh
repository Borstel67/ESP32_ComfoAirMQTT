#!/bin/bash
# ComfoAir APK Installer Script
# Installiert die App auf verbundenem Device/Emulator

set -e

APK_PATH="app/build/outputs/apk/debug/app-debug.apk"
PACKAGE_NAME="comfoair.borstel.com"
ACTIVITY_NAME=".MainActivity"

echo "🚀 ComfoAir APK Installer"
echo "=========================="
echo ""

# 1. Check ADB
if ! command -v adb &> /dev/null; then
    echo "❌ ADB nicht gefunden!"
    echo "Bitte installiere Android SDK Platform Tools"
    exit 1
fi

echo "✓ ADB gefunden"

# 2. Check Devices
DEVICES=$(adb devices -l | grep -E "device\s|emulator" | wc -l)
if [ "$DEVICES" -eq 0 ]; then
    echo "❌ Kein Device/Emulator verbunden!"
    echo "Bitte verbinde ein Device mit USB oder starte Emulator"
    adb devices -l
    exit 1
fi

echo "✓ Device(s) gefunden:"
adb devices -l

# 3. Check APK
if [ ! -f "$APK_PATH" ]; then
    echo "❌ APK nicht gefunden: $APK_PATH"
    echo "Bitte baue erst die APK: ./gradlew assembleDebug"
    exit 1
fi

APK_SIZE=$(du -h "$APK_PATH" | cut -f1)
echo "✓ APK gefunden ($APK_SIZE)"
echo ""

# 4. Install
echo "📱 Installiere App..."
adb install -r "$APK_PATH"

# 5. Start
echo "🚀 Starte App..."
adb shell am start -n "$PACKAGE_NAME/$ACTIVITY_NAME"

# 6. Logs
echo ""
echo "📊 Live-Logs (Drücke CTRL+C zum Beenden):"
adb logcat -s "WebView:D" "comfoair*" 2>/dev/null || adb logcat

echo ""
echo "✅ Installation abgeschlossen!"
