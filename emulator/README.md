# Fit Dashboard Emulator

Renders the three screens on the PC with exactly the firmware code
(`../FitDashboard/model.h`, `parse.h`, `metrics.h`, `ui.h`, fonts and icons).
Only the display class is replaced (`stub/fake_epaper.h`): it draws into an
800x480 black/white image instead of the e-paper panel.

```bash
./run.sh --fetch                       # fresh Garmin + Open-Meteo data, render
./run.sh --time 07:15                  # render cached data at another time of day
./run.sh --date 2026-11-15 --time 21:30
./run.sh --scenario warn               # also: nogarmin, noweather, nosleep
```

Output: `out/all_screens.png` (screens 1-3 stacked).
The run reports texts that run past the screen edge and characters missing in the font.

Needs `g++`, Python 3 with Pillow. Garmin key from `GARMIN_API_KEY` or
`../FitDashboard/secrets.h`. `data/` and `out/` contain personal health data and are git-ignored.

Not emulated: partial refresh/ghosting of the real panel, button handling, Wi-Fi/TLS.
Text is drawn with a 50 % threshold on the anti-aliased glyphs, close to the panel but not identical.
