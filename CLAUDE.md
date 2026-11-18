# Fit Dashboard

Persönliches E-Paper-Dashboard (Gesundheit, Lauftraining, Wetter) für das Seeed TRMNL 7.5" DIY Kit.
Alles läuft auf dem ESP32, kein eigener Server. Nur freie APIs. Kommunikation mit dem Nutzer auf Deutsch.

## Struktur

- `FitDashboard/`: Arduino-Firmware (der eigentliche Code)
  - `FitDashboard.ino`: Setup/Loop, WLAN, Zeit, Akku, Tasten, Setup-Portal, Datenabruf, Light Sleep
  - `config.h`: Ort, Ziele, Puls-Werte, Intervalle; `secrets.h` (gitignored, Vorlage `secrets.example.h`)
  - `model.h` (Datenstrukturen, `Coach` = längere Historie + abgeleitete Aussagen), `parse.h` (JSON-Filter + Parser, FIT-Leser für den letzten Lauf), `metrics.h` (TRIMP/Fitness/Ermüdung, Infekt-Check, Empfehlung, `computeCoach`: Coach-Satz, Abend-Hebel, Lauffenster für 7 Tage), `ui.h` (3 Screens: Heute, Körper, Laufen)
  - `unzip.h`: entpackt das ZIP mit der FIT-Datei (Inflate aus dem ESP32-ROM)
  - `https_tls13.h`: wolfSSL-TLS-1.3-Client (Garmin ist TLS-1.3-only, mbedTLS im ESP32-Core kann das nicht)
  - `ota.h`: OTA-Update bei jedem Datenabruf (`version.json` unter `OTA_MANIFEST_URL`), ECDSA-signiert gegen `ota_pubkey.h`, Rollback bis zum ersten erfolgreichen Abruf
  - `driver.h`, `icons.h`, `fonts/`: aus Vector eInk Dashboard übernommen (GPL-3.0)
- `emulator/`: PC-Emulator, rendert die Screens mit genau den Firmware-Headern (`stub/fake_epaper.h` ersetzt das Display)
- `tools/ota_release.py`: `keygen` (einmalig, privater Schlüssel in `~/.config/fitdashboard/`, nie ins Repo), `build` (arduino-cli mit `-DFITDASH_RELEASE`, ohne `secrets.h`), `release <bin>`

## Hardware

XIAO ESP32-S3 Plus (8 MB PSRAM, 16 MB Flash), EE04-Board, 800×480 s/w (UC8179).
Tasten: KEY1 (D1) Aktualisieren, KEY2 (D2) weiter, KEY3 (D4) zurück, 5× drücken = Setup-Portal.

## Bauen

Arduino IDE, Board `XIAO_ESP32S3_PLUS`, PSRAM „OPI PSRAM“, Partition „16M Flash (3MB APP/9.9MB FATFS)“ (Pflicht, Firmware ~2,05 MB).
Bibliothek `wolfssl` (5.8.4) mit Zusatzzeilen in `user_settings.h`, siehe Kopfkommentar in `https_tls13.h`.
Upload: B gedrückt halten beim Einstecken, hochladen, R drücken.

Kompilieren ohne IDE: arduino-cli mit FQBN `esp32:esp32:XIAO_ESP32S3_Plus:PSRAM=opi,PartitionScheme=app3M_fat9M_16MB,FlashSize=16M`.

## Release (OTA)

`FW_BUILD` und `FW_VERSION` in `config.h` erhöhen, `python3 tools/ota_release.py build`, dann `release/FitDashboard.bin` und `release/version.json` als GitHub-Release mit Tag `v<FW_VERSION>` hochladen. Release-Binaries dürfen nie `secrets.h` enthalten (das Werkzeug bricht dann ab); Zugangsdaten liegen auf dem Gerät im NVS.

## Emulator

```
cd emulator && ./run.sh --fetch          # Daten holen, bauen, rendern
./run.sh --time 07:15 --scenario warn    # Szenarien: normal|warn|nogarmin|noweather|nosleep
```
Design-Prototypen, nur im Emulator, nicht in der Firmware (bauen auf dem Firmware-Code auf): `--v3a|--v3b|--v3c` (`proto_v3.h`: Befund-Maschine mit Karten, Briefing, Tagesverlauf; `out/v3*.png`) sowie `--v4` (`proto_v4.h`: Longevity-Säulen, `out/v4_longevity.png`) und `--v5` (`proto_v5.h`: ein Screen mit wechselndem Befund, Bedienung per Sprache als Mock-up, `out/v5_ein_screen.png`). Aktuell bevorzugte Richtung: `--v6` (`proto_v6.h`, `out/v6_elemente.png`): ein Screen mit festem Rahmen, in der Mitte wechselnde Elemente (Titel, Bild oder Kennzahl, ein erklärender Satz), per Sprache abrufbar.
Ergebnis der Firmware-Screens: `emulator/out/all_screens.png`. Meldet Textüberläufe und fehlende Glyphen; nach jeder UI-Änderung laufen lassen.

## Datenquellen

- Abrufe je Zyklus: Tageswerte, letzte Nacht, Body Battery, Tages- und Schlafliste (`COACH_DAYS` = 30, die Schlafliste lehnt längere Zeiträume mit HTTP 400 ab), Aktivitäten (`ACTIVITY_DAYS`), Tagespuls; Pulszonen einmal am Tag, FIT-Datei nur bei neuem Lauf.
- Garmin Connect Personal API: `POST .../personal-api-key/token` mit `Authorization: ApiKey <key>` → JWT (24 h), dann `Bearer`. Nur lesend, kein HRV/VO2max/Training Load. `Accept: */*` nötig (sonst 406).
- Open-Meteo (frei, ohne Key), Ort Würzburg.
- Keine Strava-API (kostenpflichtig).

## Regeln

- Schriften (NotoSans 18/24/32/56) haben keine Pfeile, ✓, „·“, „Ø“: solche Zeichen zeichnen statt schreiben.
- `secrets.h` nie committen oder in Ausgaben zeigen.
- Texte kurz halten, `text()` in `ui.h` kürzt mit „…“, das soll im Normalfall nicht greifen.
