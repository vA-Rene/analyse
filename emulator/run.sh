#!/usr/bin/env bash
# Build the emulator (if needed), optionally fetch fresh data, render all screens to out/*.png
#   ./run.sh [--fetch] [--time HH:MM] [--date YYYY-MM-DD] [--scenario normal|warn|nogarmin|noweather|nosleep]
set -e
cd "$(dirname "$0")"
FETCH=0; ARGS=()
for a in "$@"; do [ "$a" = "--fetch" ] && FETCH=1 || ARGS+=("$a"); done
SRC=(emulator.cpp ../FitDashboard/*.h stub/*.h)
if [ ! -x emulator ] || [ -n "$(find "${SRC[@]}" -newer emulator 2>/dev/null)" ]; then
  g++ -std=gnu++17 -O1 -w -I stub -I third_party -I ../FitDashboard -include cmath emulator.cpp -o emulator
fi
[ $FETCH = 1 ] && python3 -I fetch_data.py
mkdir -p out
set +e; ./emulator --data data --out out "${ARGS[@]}"; rc=$?; set -e
python3 -I - <<'PY'
import os
from PIL import Image
imgs = [Image.open(f"out/screen{i}.pbm").convert("L") for i in (1, 2, 3)]
sheet = Image.new("L", (800, 3 * 480 + 2 * 12), 128)
for i, im in enumerate(imgs): sheet.paste(im, (0, i * 492))
sheet.save("out/all_screens.png")
for f in os.listdir("out"):
    if f != "all_screens.png": os.remove(os.path.join("out", f))
print("Bild: out/all_screens.png")
PY
exit $rc
