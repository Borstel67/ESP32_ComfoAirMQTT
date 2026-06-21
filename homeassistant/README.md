# ComfoAir MQTT für Home Assistant

Dieses Paket wird in Home Assistant OS unter  
`/config/custom_components/comfoair_mqtt` abgelegt und von dort eingebunden.

## 1) Zielstruktur in HA OS
/config/custom_components/comfoair_mqtt/
  
  package.yaml  
  dashboard.yaml  
  secrets.example.yaml  
  README.md

  
## 2) Dateien aus diesem Projekt kopieren

- `homeassistant/config/custom_components/comfoair_mqtt/package.yaml` → `/config/custom_components/comfoair_mqtt/package.yaml`
- `homeassistant/config/custom_components/comfoair_mqtt/dashboard.yaml` → `/config/custom_components/comfoair_mqtt/dashboard.yaml`
- `homeassistant/README.md` → `/config/custom_components/comfoair_mqtt/README.md` (optional)

## 3) `configuration.yaml` ergänzen

In `/config/configuration.yaml`:


## 4) `secrets.yaml` ergänzen

In `/config/secrets.yaml` die MQTT-Topics hinterlegen, z. B.:

````````

## 5) `Neustart`

- Home Assistant neu starten.
- Danach Entitäten und Skripte aus dem Package prüfen.
- Falls das Dashboard nicht sichtbar ist: Lovelace-Config prüfen oder Dashboard manuell in YAML-Modus anlegen.

## Hinweise

- MQTT-Topic-Prefix (`comfoair`) bei Bedarf anpassen.
- Befehle/Sollwerte: `/cmd/.../bereich`
- Command-Feedback: `/status/command/bereich`
- System-Online: `/status/system/json`
- Weitere Istwerte: `/status/...` (gemäß `package.yaml`)

Wenn gewünscht, liefere ich im nächsten Schritt die gleiche Umstellung direkt für `config\custom_components\comfoair_mqtt\package.yaml` und `config\configuration.yaml` als finalen, konsistenten Stand.
