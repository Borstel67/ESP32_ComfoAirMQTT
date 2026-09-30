# Copilot Instructions

## Projektrichtlinien
- Im Manueller Modus gilt: Wenn die Auto-Stop-Zeit 0 ist, soll kein automatischer Stop erfolgen; Stop wird dann manuell gesetzt.
- MQTT-Statuswerte sollen nur bei tatsächlicher Änderung veröffentlicht werden (Change-Only Publishing).
- MQTT soll ausschließlich JSON-Topics verwenden; nicht-JSON-Topics sollen entfernt werden. MQTT-Topics sollen auf den Suffix /bereich umgestellt werden (statt /json), trotz bisheriger JSON-Topic-Regel.
- Nur Befehle/Sollwerte sollen unter /bereich veröffentlicht werden; alle Istwerte/Statuswerte weiterhin unter /json.
- Bei Home-Assistant-MQTT-Entitäten in diesem Projekt soll `default_entity_id` statt `object_id` verwendet werden.
- Der Ordner `homeassistant\...` im Repository ist nur ein Platzhalter; maßgeblich sind die Dateien an der tatsächlichen `/config`-Position in Home Assistant.
- Achte darauf, dass das Symbol der Manual-Mode-Entität in Home Assistant verwendet wird, anstatt sich auf den Text zu konzentrieren.
- Binde keine unnötigen Home-Assistant-Skripte ein, wenn sie nicht benötigt werden.
- Beim MQTT-Befehl für Level soll der Wert zuerst persistent gespeichert und erst danach per RS232 geschrieben werden.