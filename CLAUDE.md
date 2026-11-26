# Fit Dashboard

Persönliches E-Paper-Dashboard (Gesundheit, Lauftraining, Wetter) für das Seeed TRMNL 7.5" DIY Kit.
Alles läuft auf dem ESP32, kein eigener Server. Nur freie APIs. Kommunikation mit dem Nutzer auf Deutsch.

## Struktur

- `FitDashboard/`: Arduino-Firmware (der eigentliche Code)
  - `FitDashboard.ino`: Setup/Loop, WLAN, Zeit, Akku, Tasten, Setup-Portal, Datenabruf, Light Sleep
  - `config.h`: Ort, Ziele, Puls-Werte, Intervalle; `secrets.h` (gitignored, Vorlage `secrets.example.h`)
  - `model.h` (Datenstrukturen, `Coach` = längere Historie + abgeleitete Aussagen), `parse.h` (JSON-Filter + Parser, FIT-Leser für den letzten Lauf), `metrics.h` (TRIMP/Fitness/Ermüdung, Infekt-Check, Empfehlung, `computeCoach`: Coach-Satz, Abend-Hebel, Lauffenster für 7 Tage), `ui.h` (3 Screens: Heute, Körper, Laufen)
  - `kit.h` (+ `kit_charts.h`, `kit_shapes.h`, `kit_more.h`): zeichnet eine Screen-Beschreibung (JSON, verschachtelte Spalten/Zeilen, in jedem Feld eines von 32 Elementen) und sammelt Meldungen, wenn etwas nicht passt
  - `coach.h`: LLM-Coach (Gemini): Kennzahlen je Zeitraum aus den Gerätedaten, Prompt, Namen und Einheiten aus dem Code, Zahlenprüfung (das LLM darf nur auf Daten verweisen, keine Zahl tippen), Korrekturrunden, Ersatz-Screen. Baut die Anfragen, sendet sie aber nicht selbst (`PostFn`). `coach_prompt.h` ist generiert (`python3 designs/coach.py --firmware-header x`), nicht von Hand ändern
  - `phone.h`: Handy als Mikrofon und als Weg zu Gemini: Webseite im Heim-WLAN (die Tastatur des Handys macht aus Sprache Text), Empfang der Frage (`POST /ask`, nur mit Header `X-Fitdash`), danach holt die Seite jede Anfrage des Coachs ab (`GET /job`), schickt sie mit dem Gemini-Key aus dem Browser-Speicher des Handys an Google und bringt die Antwort zurück (`POST /answer`). Der Key liegt nie auf dem Gerät, die Seite hat einen Knopf zum Löschen. Dazu die Display-Zustände. Ohne Arduino-Abhängigkeit, läuft auch im Emulator
  - `unzip.h`: entpackt das ZIP mit der FIT-Datei (Inflate aus dem ESP32-ROM)
  - `https_tls13.h`: wolfSSL-TLS-1.3-Client (Garmin ist TLS-1.3-only, mbedTLS im ESP32-Core kann das nicht)
  - `ota.h`: OTA-Update bei jedem Datenabruf (`version.json` unter `OTA_MANIFEST_URL`), ECDSA-signiert gegen `ota_pubkey.h`, Rollback bis zum ersten erfolgreichen Abruf
  - `driver.h`, `icons.h`, `fonts/`: aus Vector eInk Dashboard übernommen (GPL-3.0)
- `emulator/`: PC-Emulator, rendert die Screens mit genau den Firmware-Headern (`stub/fake_epaper.h` ersetzt das Display)
- `tools/ota_release.py`: `keygen` (einmalig, privater Schlüssel in `~/.config/fitdashboard/`, nie ins Repo), `build` (arduino-cli mit `-DFITDASH_RELEASE`, ohne `secrets.h`), `release <bin>`

## Hardware

XIAO ESP32-S3 Plus (8 MB PSRAM, 16 MB Flash), EE04-Board, 800×480 s/w (UC8179).
Tasten: KEY1 (D1) Aktualisieren, KEY2 (D2) weiter, KEY3 (D4) zurück, 5× drücken = Setup-Portal.
Vierter Screen „Coach“: dort geht das Gerät mit KEY1 ins WLAN, zeigt eine Adresse (`http://fitdash.local` und die IP) und wartet 90 s (`PHONE_WINDOW_MS`) auf eine Frage, die man auf dieser Seite am Handy diktiert und sendet (oder eine der festen Fragen aus `COACH_QUESTIONS` antippt). Das Gerät hat keinen Gemini-Key: die Seite fragt einmal danach, speichert ihn im Browser des Handys und stellt die Anfragen an Google. Jede Taste bricht ab.

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
Synthetische Testdaten: `emulator/synth/` (fünf erfundene Sportler, 01.01.2026 bis heute, `./run.sh --synth laeufer|radfahrer|schwimmer|triathlet|halbprofi --date … --time …`, Lösungsblatt je Ordner in `truth.json`, `python3 synth/sweep.py` prüft jeden Tag des Jahres).
Coach und Baukasten mit dem Firmware-Code testen: `./ask.sh laeufer "How was my sleep this week?"` (echter Gemini-Aufruf, Key aus `.env`, Bild `out/coach.png`), `./phone.sh laeufer [--dry] [--lan]` (Handy-Seite auf `http://127.0.0.1:8080`, wartet auf eine Frage, dann Coach; den Gemini-Aufruf macht die Seite mit dem dort eingetragenen Key, `--dry` hört nach der Frage auf, Bilder `out/phone_*.png`), `./emulator --spec datei.json --png out/x.pbm` zeichnet eine Beschreibung, `python3 kit_compare.py` vergleicht alle `designs/specs` mit dem Python-Zeichner.
Ergebnis der Firmware-Screens: `emulator/out/all_screens.png`. Meldet Textüberläufe und fehlende Glyphen; nach jeder UI-Änderung laufen lassen.

## Zielbild (noch nicht umgesetzt)

Das Dashboard soll ein eigenes Gerät sein, mit dem man täglich spricht, statt einer weiteren Handy-App. Feste Screens passen nie für alle Sportarten und Niveaus.

- Bedienung: Taste drücken und sprechen (kein dauerhaft lauschendes Aktivierungswort).
- Ein LLM als Coach beantwortet Fragen aus den eigenen Daten („Wie war mein Schlaf diese Woche, soll ich morgen den Halbmarathon laufen?“) und legt sich dabei anhand der Daten fest; die Entscheidung bleibt beim Nutzer.
- Das LLM steuert nur, was der Code vorgibt: Es wählt und füllt Elemente aus dem v6-Baukasten (Diagramm, Rahmen, Text). Kennzahlen rechnet weiter der Code (`metrics.h`), das LLM wählt aus, analysiert und formuliert.
- Handy-App höchstens, um unterwegs in den Trainingsplan zu schauen.
- Offen: Modellwahl (API oder selbst gehostet), Backend, Gedächtnis für Plan und Befinden, eingebautes Mikrofon und Sprachausgabe (das Kit hat beides nicht; die Frage kommt bis dahin als Text vom Handy, `phone.h`).

## Datenquellen

- Abrufe je Zyklus: Tageswerte, letzte Nacht, Body Battery, Tages- und Schlafliste (`COACH_DAYS` = 30, die Schlafliste lehnt längere Zeiträume mit HTTP 400 ab), Aktivitäten (`ACTIVITY_DAYS`), Tagespuls; Pulszonen einmal am Tag, FIT-Datei nur bei neuem Lauf.
- Garmin Connect Personal API: `POST .../personal-api-key/token` mit `Authorization: ApiKey <key>` → JWT (24 h), dann `Bearer`. Nur lesend. Zusätzlich abrufbar, von der Firmware noch nicht genutzt: VO2max (`/metrics-service/metrics/maxmet/daily/{von}/{bis}`), Gewicht (`/weight-service/weight/range/{von}/{bis}`), Schritte als Tagesliste, Aktivität als JSON (`/activity-service/activity/{id}` und `/details`). HRV antwortet leer; Training Load, Readiness, Trainingsstatus, Rennprognosen, SpO2 gibt es nicht (404). `Accept: */*` nötig (sonst 406).
- Open-Meteo (frei, ohne Key), Ort Würzburg.
- Keine Strava-API (kostenpflichtig).

## Regeln

- Schriften (NotoSans 18/24/32/56) haben keine Pfeile, ✓, „·“, „Ø“: solche Zeichen zeichnen statt schreiben.
- `secrets.h` nie committen oder in Ausgaben zeigen.
- Texte kurz halten, `text()` in `ui.h` kürzt mit „…“, das soll im Normalfall nicht greifen.
