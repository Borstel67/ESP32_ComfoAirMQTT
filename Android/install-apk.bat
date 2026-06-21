@echo off
REM ComfoAir APK Installer Script (Windows)
REM Installiert die App auf verbundenem Device/Emulator

setlocal enabledelayedexpansion

set APK_PATH=app\build\outputs\apk\debug\app-debug.apk
set PACKAGE_NAME=comfoair.borstel.com
set ACTIVITY_NAME=.MainActivity

echo.
echo ^>^>^> ComfoAir APK Installer
echo ===========================
echo.

REM 1. Check ADB
where adb >nul 2>nul
if errorlevel 1 (
    echo [X] ADB nicht gefunden!
    echo Bitte installiere Android SDK Platform Tools
    pause
    exit /b 1
)

echo [OK] ADB gefunden

REM 2. List Devices
echo.
echo [INFO] Verbundene Devices:
adb devices -l

REM 3. Check APK
if not exist "%APK_PATH%" (
    echo.
    echo [X] APK nicht gefunden: %APK_PATH%
    echo Bitte baue erst die APK: gradlew assembleDebug
    pause
    exit /b 1
)

for %%A in ("%APK_PATH%") do set "APK_SIZE=%%~zA"
set /A SIZE_MB=APK_SIZE/1024/1024
echo [OK] APK gefunden (%SIZE_MB% MB)

REM 4. Install
echo.
echo [*] Installiere App...
adb install -r "%APK_PATH%"

if errorlevel 1 (
    echo.
    echo [X] Installation fehlgeschlagen!
    echo Stelle sicher, dass:
    echo   - Device/Emulator verbunden ist (adb devices)
    echo   - USB-Debugging aktiviert ist
    echo   - App nicht schon installiert ist (adb uninstall %PACKAGE_NAME%)
    pause
    exit /b 1
)

REM 5. Start
echo.
echo [*] Starte App...
adb shell am start -n "%PACKAGE_NAME%/%ACTIVITY_NAME%"

REM 6. Success
echo.
echo [OK] Installation abgeschlossen!
echo.
echo [INFO] Tipps:
echo   - App sollte jetzt auf dem Device laufen
echo   - Logs anschauen: adb logcat -s WebView
echo   - Auf alte Version zurück: adb uninstall %PACKAGE_NAME%
echo.
pause
