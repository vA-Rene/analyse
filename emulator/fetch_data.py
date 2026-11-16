#!/usr/bin/env python3
"""Fetch the same Garmin + Open-Meteo answers the Fit Dashboard firmware loads,
and store them in emulator/data/ for the emulator.

Garmin key: environment variable GARMIN_API_KEY, otherwise SECRET_GARMIN_API_KEY
from ../FitDashboard/secrets.h. Location: DEFAULT_LATITUDE/LONGITUDE from config.h.
"""
import datetime as dt
import json
import os
import re
import sys
import urllib.error
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
SKETCH = os.path.join(HERE, "..", "FitDashboard")
DATA = os.path.join(HERE, "data")
GARMIN = "https://personal.connectapi.garmin.com"


def define(path, name):
    try:
        m = re.search(r'#define\s+' + name + r'\s+("?)([^"\n]*)\1', open(path, encoding="utf-8").read())
        return m.group(2).strip().rstrip("f") if m else ""
    except OSError:
        return ""


def request(url, headers=None, method="GET"):
    req = urllib.request.Request(url, headers=headers or {}, method=method, data=b"" if method == "POST" else None)
    with urllib.request.urlopen(req, timeout=30) as r:
        return r.read()


def main():
    key = os.environ.get("GARMIN_API_KEY") or define(os.path.join(SKETCH, "secrets.h"), "SECRET_GARMIN_API_KEY")
    lat = define(os.path.join(SKETCH, "config.h"), "DEFAULT_LATITUDE") or "49.79"
    lon = define(os.path.join(SKETCH, "config.h"), "DEFAULT_LONGITUDE") or "9.95"
    query = "".join(re.findall(r'"([^"]*)"', re.search(r"OPEN_METEO_QUERY\s*=(.*?);",
                    open(os.path.join(SKETCH, "parse.h"), encoding="utf-8").read(), re.S).group(1)))
    os.makedirs(DATA, exist_ok=True)

    w = request(f"https://api.open-meteo.com/v1/forecast?latitude={lat}&longitude={lon}{query}")
    open(os.path.join(DATA, "weather.json"), "wb").write(w)
    print(f"weather.json      {len(w):6d} B")

    if not key:
        print("Kein Garmin-Key (GARMIN_API_KEY oder secrets.h) - nur Wetter aktualisiert.")
        return 1
    try:
        token = request(GARMIN + "/personalapitoken-service/personal-api/personal-api-key/token",
                        {"Authorization": "ApiKey " + key, "Accept": "*/*"}, "POST").decode().strip().strip('"')
    except urllib.error.HTTPError as e:
        print(f"Garmin-Token: HTTP {e.code}")
        return 1
    today = dt.date.today()
    iso = lambda d: d.isoformat()
    first = today - dt.timedelta(days=13)          # HEALTH_DAYS = 14
    acts_from = today - dt.timedelta(days=55)      # LOAD_DAYS = 56
    calls = {
        "summary.json": f"/usersummary-service/usersummary/daily?calendarDate={iso(today)}",
        "sleep.json": f"/sleep-service/sleep/dailySleep?date={iso(today)}",
        "bodybattery.json": f"/wellness-service/wellness/bodyBattery/reports/daily?startDate={iso(today)}&endDate={iso(today)}&chunks=96",
        "days.json": f"/usersummary-service/v2/dailysummaries?startDate={iso(first)}&endDate={iso(today)}",
        "sleeprange.json": f"/sleep-service/sleep/dailySleepSummariesByDate?startDate={iso(first)}&endDate={iso(today)}",
        "activities.json": f"/activitylist-service/activities/search/activities?startDate={iso(acts_from)}&limit=100",
    }
    for name, path in calls.items():
        try:
            body = request(GARMIN + path, {"Authorization": "Bearer " + token, "Accept": "*/*"})
        except urllib.error.HTTPError as e:
            print(f"{name:17s} HTTP {e.code}")
            continue
        open(os.path.join(DATA, name), "wb").write(body)
        print(f"{name:17s} {len(body):6d} B")
    return 0


if __name__ == "__main__":
    sys.exit(main())
