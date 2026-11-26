/*
 * File: parse.h
 * Purpose: Turn Garmin Connect Personal API and Open-Meteo JSON answers into
 * the structures in model.h. Each parser has a matching filter so the ESP32
 * only keeps the fields it needs while streaming large answers.
 */
#pragma once
#include <ArduinoJson.h>
#include <math.h>
#include <algorithm>
#include "model.h"

// ----------------------------------------------------------------- helpers
inline int jsonInt(JsonVariantConst v, int fallback = -1) {
  return v.isNull() ? fallback : (int)lround(v.as<double>());
}
inline float jsonFloat(JsonVariantConst v, float fallback = -1) {
  return v.isNull() ? fallback : v.as<float>();
}
// Garmin "...Local" timestamps are milliseconds whose clock time is local time.
inline int localMinutesFromMs(JsonVariantConst v) {
  if (v.isNull()) return -1;
  long long s = v.as<long long>() / 1000LL;
  return (int)((s % 86400LL + 86400LL) % 86400LL) / 60;
}
inline int minutesFromIsoTime(const char* s) {  // "...T07:32" or "... 07:32"
  if (!s || strlen(s) < 16) return -1;
  return atoi(s + 11) * 60 + atoi(s + 14);
}

// ----------------------------------------------------------------- Garmin: today's summary
// GET /usersummary-service/usersummary/daily?calendarDate=YYYY-MM-DD
inline void filterUserSummary(JsonDocument& f) {
  for (const char* k : {"restingHeartRate", "lastSevenDaysAvgRestingHeartRate", "bodyBatteryMostRecentValue",
                        "bodyBatteryAtWakeTime", "bodyBatteryHighestValue", "bodyBatteryLowestValue",
                        "averageStressLevel", "totalSteps", "restStressDuration", "lowStressDuration",
                        "mediumStressDuration", "highStressDuration"}) f[k] = true;
}
inline bool parseUserSummary(const JsonDocument& doc, HealthToday& t) {
  if (!doc.is<JsonObjectConst>()) return false;
  t.rhr = jsonInt(doc["restingHeartRate"]);
  if (t.rhr > 85) t.rhr = -1;  // implausible on days the watch was barely worn
  t.rhr7 = jsonInt(doc["lastSevenDaysAvgRestingHeartRate"]);
  t.bbNow = jsonInt(doc["bodyBatteryMostRecentValue"]);
  t.bbWake = jsonInt(doc["bodyBatteryAtWakeTime"]);
  t.bbHigh = jsonInt(doc["bodyBatteryHighestValue"]);
  t.bbLow = jsonInt(doc["bodyBatteryLowestValue"]);
  t.stressAvg = jsonInt(doc["averageStressLevel"]);
  t.steps = doc["totalSteps"].isNull() ? -1 : doc["totalSteps"].as<long>();
  static const char* stressKey[4] = {"restStressDuration", "lowStressDuration", "mediumStressDuration", "highStressDuration"};
  for (int i = 0; i < 4; i++) t.stressMin[i] = jsonInt(doc[stressKey[i]], 0) / 60;
  t.ok = true;
  return true;
}

// ----------------------------------------------------------------- Garmin: last night
// GET /sleep-service/sleep/dailySleep?date=YYYY-MM-DD
inline void filterDailySleep(JsonDocument& f) {
  for (const char* k : {"sleepTimeSeconds", "deepSleepSeconds", "lightSleepSeconds", "remSleepSeconds",
                        "awakeSleepSeconds", "sleepStartTimestampLocal", "sleepEndTimestampLocal",
                        "averageRespirationValue"}) f[k] = true;
}
inline void parseDailySleep(const JsonDocument& doc, HealthToday& t) {
  if (doc["sleepTimeSeconds"].isNull()) { t.sleepH = -1; return; }
  t.sleepH = doc["sleepTimeSeconds"].as<float>() / 3600.0f;
  t.deepH = jsonFloat(doc["deepSleepSeconds"], 0) / 3600.0f;
  t.lightH = jsonFloat(doc["lightSleepSeconds"], 0) / 3600.0f;
  t.remH = jsonFloat(doc["remSleepSeconds"], 0) / 3600.0f;
  t.awakeH = jsonFloat(doc["awakeSleepSeconds"], 0) / 3600.0f;
  t.sleepStartMin = localMinutesFromMs(doc["sleepStartTimestampLocal"]);
  t.sleepEndMin = localMinutesFromMs(doc["sleepEndTimestampLocal"]);
  t.resp = jsonFloat(doc["averageRespirationValue"]);
}

// ----------------------------------------------------------------- Garmin: daily list (COACH_DAYS)
// GET /usersummary-service/v2/dailysummaries?startDate=..&endDate=..
inline void filterDailySummaries(JsonDocument& f) {
  JsonObject d = f["data"][0].to<JsonObject>();
  d["calendarDate"] = true;
  d["heartRate"]["restingValue"] = true;
  d["bodyBattery"]["maxValue"] = true;
  d["bodyBattery"]["minValue"] = true;
  d["bodyBattery"]["chargedValue"] = true;
  d["bodyBattery"]["drainedValue"] = true;
  d["movement"]["steps"]["value"] = true;
  d["stress"]["avgLevel"] = true;
  d["intensityMinutes"]["moderate"] = true;
  d["intensityMinutes"]["vigorous"] = true;
}
inline HealthDay* healthDayFor(DashboardData& D, long day) {
  long first = D.todayNum - (HEALTH_DAYS - 1);
  if (day < first || day > D.todayNum) return nullptr;
  HealthDay* h = &D.days[day - first];
  h->day = day;
  return h;
}
inline void resetHealthDays(DashboardData& D) {
  for (int i = 0; i < HEALTH_DAYS; i++) {
    D.days[i] = HealthDay();
    D.days[i].day = D.todayNum - (HEALTH_DAYS - 1) + i;
  }
}
// days before GARMIN_FIRST_DAY are not used
inline long garminFirstDay() {
  long d = dayNumberFromIso(GARMIN_FIRST_DAY);
  return d < 0 ? 0 : d;
}
inline int parseDailySummaries(const JsonDocument& doc, DashboardData& D) {
  int n = 0;
  D.c.days.clear();
  for (JsonObjectConst d : doc["data"].as<JsonArrayConst>()) {
    CoachDay v;
    v.day = dayNumberFromIso(d["calendarDate"]);
    if (v.day < garminFirstDay() || v.day > D.todayNum) continue;
    v.rhr = jsonInt(d["heartRate"]["restingValue"]);
    if (v.rhr > 85) v.rhr = -1;  // implausible on days the watch was barely worn
    v.bbMax = jsonInt(d["bodyBattery"]["maxValue"]);
    v.bbMin = jsonInt(d["bodyBattery"]["minValue"]);
    v.charged = jsonInt(d["bodyBattery"]["chargedValue"]);
    v.drained = jsonInt(d["bodyBattery"]["drainedValue"]);
    v.steps = jsonInt(d["movement"]["steps"]["value"]);
    v.intensity = jsonInt(d["intensityMinutes"]["moderate"], 0) + 2 * jsonInt(d["intensityMinutes"]["vigorous"], 0);
    D.c.days.push_back(v);
    n++;
    HealthDay* h = healthDayFor(D, v.day);
    if (!h) continue;
    h->rhr = v.rhr;
    h->bbMax = v.bbMax;
    h->bbMin = v.bbMin;
    h->stress = jsonInt(d["stress"]["avgLevel"]);
    h->intensity = v.intensity;
  }
  std::sort(D.c.days.begin(), D.c.days.end(), [](const CoachDay& a, const CoachDay& b) { return a.day < b.day; });
  return n;
}

// ----------------------------------------------------------------- Garmin: sleep list (COACH_DAYS)
// GET /sleep-service/sleep/dailySleepSummariesByDate?startDate=..&endDate=..
inline void filterSleepRange(JsonDocument& f) {
  JsonObject d = f[0].to<JsonObject>();
  d["calendarDate"] = true;
  d["sleepTimeSeconds"] = true;
  d["sleepStartTimestampLocal"] = true;
  d["sleepEndTimestampLocal"] = true;
  d["deepSleepSeconds"] = true;
  d["awakeSleepSeconds"] = true;
  d["averageRespirationValue"] = true;
}
inline int parseSleepRange(const JsonDocument& doc, DashboardData& D) {
  int n = 0;
  D.c.nights.clear();
  for (JsonObjectConst s : doc.as<JsonArrayConst>()) {
    long day = dayNumberFromIso(s["calendarDate"]);
    if (day < garminFirstDay() || day > D.todayNum || s["sleepTimeSeconds"].isNull()) continue;
    int start = localMinutesFromMs(s["sleepStartTimestampLocal"]), end = localMinutesFromMs(s["sleepEndTimestampLocal"]);
    CoachNight c;
    c.day = day;
    c.sleepH = s["sleepTimeSeconds"].as<float>() / 3600.0f;
    if (start >= 0) c.bedMin = (start + 720) % 1440;  // minutes after noon
    if (end >= 0) c.wakeMin = (end + 720) % 1440;
    c.deepMin = jsonInt(s["deepSleepSeconds"], 0) / 60;
    c.awakeMin = jsonInt(s["awakeSleepSeconds"], 0) / 60;
    if (c.bedMin >= 0 && c.wakeMin >= 0) D.c.nights.push_back(c);
    n++;
    HealthDay* h = healthDayFor(D, day);
    if (!h) continue;
    h->sleepH = c.sleepH;
    h->resp = jsonFloat(s["averageRespirationValue"]);
    h->bedMin = c.bedMin;
  }
  std::sort(D.c.nights.begin(), D.c.nights.end(), [](const CoachNight& a, const CoachNight& b) { return a.day < b.day; });
  return n;
}

inline int constrain_int(int v) { return v < 0 ? 0 : (v > 100 ? 100 : v); }

// ----------------------------------------------------------------- Garmin: body battery today
// GET /wellness-service/wellness/bodyBattery/reports/daily?startDate=today&endDate=today&chunks=96
inline void filterBodyBattery(JsonDocument& f) {
  f[0]["bodyBatteryValuesArray"] = true;
}
inline void parseBodyBattery(const JsonDocument& doc, HealthToday& t) {
  JsonArrayConst vals = doc[0]["bodyBatteryValuesArray"].as<JsonArrayConst>();
  int total = 0;
  for (JsonArrayConst p : vals) if (!p[1].isNull()) total++;
  int step = total > HealthToday::CURVE_MAX ? (total + HealthToday::CURVE_MAX - 1) / HealthToday::CURVE_MAX : 1;
  t.bbCurveN = 0;
  int i = 0;
  for (JsonArrayConst p : vals) {
    if (p[1].isNull()) continue;
    if (i++ % step == 0 && t.bbCurveN < HealthToday::CURVE_MAX) t.bbCurve[t.bbCurveN++] = (uint8_t)constrain_int(p[1].as<int>());
  }
}

// ----------------------------------------------------------------- Garmin: activities
// GET /activitylist-service/activities/search/activities?startDate=..&limit=100
inline void filterActivities(JsonDocument& f) {
  JsonObject a = f[0].to<JsonObject>();
  for (const char* k : {"activityId", "activityName", "startTimeLocal", "distance", "duration", "movingDuration", "averageHR",
                        "maxHR", "aerobicTrainingEffect", "anaerobicTrainingEffect", "aerobicTrainingEffectMessage",
                        "hrTimeInZone_1", "hrTimeInZone_2", "hrTimeInZone_3", "hrTimeInZone_4", "hrTimeInZone_5",
                        "fastestSplit_5000", "fastestSplit_10000", "fastestSplit_21098"}) a[k] = true;
  a["activityType"]["typeKey"] = true;
}
inline int parseActivities(const JsonDocument& doc, DashboardData& D) {
  JsonArrayConst arr = doc.as<JsonArrayConst>();
  int total = arr.size();
  D.actN = 0;
  // the API answers newest first; keep the newest MAX_ACT and store oldest first
  int skip = total > DashboardData::MAX_ACT ? total - DashboardData::MAX_ACT : 0;
  for (int i = total - 1; i >= 0; i--) {
    if (total - 1 - i < skip) continue;
    JsonObjectConst a = arr[i];
    Activity& x = D.acts[D.actN];
    x = Activity();
    const char* st = a["startTimeLocal"] | "";
    x.day = dayNumberFromIso(st);
    if (x.day < garminFirstDay()) continue;
    x.id = a["activityId"] | 0LL;
    x.startMin = minutesFromIsoTime(st);
    const char* type = a["activityType"]["typeKey"] | "";
    x.run = strstr(type, "running") != nullptr;
    x.strength = strstr(type, "strength") != nullptr;
    strcpy(x.sport, x.run ? "run" : x.strength ? "strength" : (strstr(type, "cycling") || strstr(type, "biking") || strstr(type, "ride")) ? "bike"
                    : strstr(type, "swim") ? "swim" : "other");
    strncpy(x.name, a["activityName"] | "", sizeof(x.name) - 1);
    x.km = jsonFloat(a["distance"], 0) / 1000.0f;
    float sec = jsonFloat(a["movingDuration"], 0);
    if (sec <= 0) sec = jsonFloat(a["duration"], 0);
    x.minutes = sec / 60.0f;
    x.avgHr = jsonInt(a["averageHR"], 0);
    x.maxHr = jsonInt(a["maxHR"], 0);
    x.te = jsonFloat(a["aerobicTrainingEffect"], 0);
    x.anTe = jsonFloat(a["anaerobicTrainingEffect"], 0);
    static const char* zoneKey[5] = {"hrTimeInZone_1", "hrTimeInZone_2", "hrTimeInZone_3", "hrTimeInZone_4", "hrTimeInZone_5"};
    for (int z = 0; z < 5; z++) x.zoneMin[z] = jsonFloat(a[zoneKey[z]], 0) / 60.0f;
    const char* msg = a["aerobicTrainingEffectMessage"] | "";   // e.g. MAINTAINING_AEROBIC_BASE_7, IMPACTING_TEMPO_22
    const char* kind = "Lauf";
    if (strstr(msg, "RECOVERY")) kind = "Erholung";
    else if (strstr(msg, "TEMPO")) kind = "Tempo";
    else if (strstr(msg, "THRESHOLD")) kind = "Schwelle";
    else if (strstr(msg, "VO2")) kind = "VO2max";
    else if (strstr(msg, "ENDURANCE")) kind = "Ausdauer";
    else if (strstr(msg, "BASE") || strstr(msg, "AEROBIC")) kind = "Grundlage";
    if (x.anTe >= 2.0f && !strstr(msg, "TEMPO")) kind = "Intervalle";
    strcpy(x.type, kind);
    x.best5k = jsonFloat(a["fastestSplit_5000"], 0);
    x.best10k = jsonFloat(a["fastestSplit_10000"], 0);
    x.bestHalf = jsonFloat(a["fastestSplit_21098"], 0);
    D.actN++;
  }
  return D.actN;
}

// ----------------------------------------------------------------- Open-Meteo
inline const char* OPEN_METEO_QUERY =
    "&timezone=Europe%2FBerlin"
    "&current=temperature_2m,apparent_temperature,weather_code,wind_speed_10m"
    "&minutely_15=precipitation&forecast_minutely_15=8"
    "&hourly=temperature_2m,apparent_temperature,precipitation_probability,precipitation,wind_speed_10m,weather_code"
    "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max,sunrise,sunset"
    "&forecast_days=7";

// The hourly list starts today at 00:00 and covers 7 days. Weather keeps the first
// 48 hours, the coach keeps all of them for the running windows of the week.
inline void parseCoachWeather(const JsonDocument& doc, Coach& c) {
  JsonObjectConst h = doc["hourly"];
  JsonArrayConst t = h["time"];
  if (t.size() == 0) return;
  c.hours.clear();
  c.hours.reserve(t.size());
  for (int i = 0; i < (int)t.size(); i++) {
    const char* ts = t[i];
    c.hours.push_back({dayNumberFromIso(ts), atoi(ts + 11), jsonFloat(h["temperature_2m"][i], 0),
                       jsonFloat(h["apparent_temperature"][i], 0), jsonFloat(h["precipitation"][i], 0),
                       jsonFloat(h["wind_speed_10m"][i], 0), jsonInt(h["precipitation_probability"][i], 0),
                       jsonInt(h["weather_code"][i], 0)});
  }
  c.wxFirstDay = dayNumberFromIso(doc["daily"]["time"][0]);
  for (int i = 0; i < 7; i++) {
    c.sunrise[i] = minutesFromIsoTime(doc["daily"]["sunrise"][i]);
    c.sunset[i] = minutesFromIsoTime(doc["daily"]["sunset"][i]);
  }
}

inline bool parseWeather(const JsonDocument& doc, Weather& w) {
  if (doc["current"].isNull() || doc["hourly"].isNull()) return false;
  w.tempNow = jsonFloat(doc["current"]["temperature_2m"], 0);
  w.feelsNow = jsonFloat(doc["current"]["apparent_temperature"], 0);
  w.windNow = jsonFloat(doc["current"]["wind_speed_10m"], 0);
  w.codeNow = jsonInt(doc["current"]["weather_code"], 0);
  w.rain2h = 0;
  for (JsonVariantConst v : doc["minutely_15"]["precipitation"].as<JsonArrayConst>()) w.rain2h += v.as<float>();
  JsonObjectConst h = doc["hourly"];
  w.hourN = 0;
  JsonArrayConst times = h["time"];
  for (int i = 0; i < (int)times.size() && w.hourN < Weather::HOURS; i++) {
    const char* t = times[i];
    int k = w.hourN++;
    w.hourDay[k] = dayNumberFromIso(t);
    w.hourOfDay[k] = atoi(t + 11);
    w.temp[k] = jsonFloat(h["temperature_2m"][i], 0);
    w.feels[k] = jsonFloat(h["apparent_temperature"][i], 0);
    w.prob[k] = jsonInt(h["precipitation_probability"][i], 0);
    w.precip[k] = jsonFloat(h["precipitation"][i], 0);
    w.wind[k] = jsonFloat(h["wind_speed_10m"][i], 0);
    w.code[k] = jsonInt(h["weather_code"][i], 0);
  }
  JsonObjectConst d = doc["daily"];
  JsonArrayConst dt = d["time"];
  w.dayN = 0;
  for (int i = 0; i < (int)dt.size() && w.dayN < Weather::DAYS; i++) {
    int k = w.dayN++;
    w.dDay[k] = dayNumberFromIso(dt[i]);
    w.tMin[k] = jsonFloat(d["temperature_2m_min"][i], 0);
    w.tMax[k] = jsonFloat(d["temperature_2m_max"][i], 0);
    w.pMax[k] = jsonInt(d["precipitation_probability_max"][i], 0);
    w.dCode[k] = jsonInt(d["weather_code"][i], 0);
  }
  w.sunriseMin = minutesFromIsoTime(d["sunrise"][0]);
  w.sunsetMin = minutesFromIsoTime(d["sunset"][0]);
  w.sunriseTomorrowMin = minutesFromIsoTime(d["sunrise"][1]);
  w.sunsetTomorrowMin = minutesFromIsoTime(d["sunset"][1]);
  w.ok = w.hourN > 0;
  return w.ok;
}

// ----------------------------------------------------------------- Garmin: heart-rate zones
// GET /biometric-service/heartRateZones/v2
inline void parseHrZones(const JsonDocument& doc, Coach& c) {
  for (JsonObjectConst z : doc.as<JsonArrayConst>()) {
    if (strcmp(z["sport"] | "", "RUNNING")) continue;
    static const char* k[5] = {"zone1Floor", "zone2Floor", "zone3Floor", "zone4Floor", "zone5Floor"};
    for (int i = 0; i < 5; i++) c.zoneFloor[i] = jsonInt(z[k[i]], c.zoneFloor[i]);
  }
}

// ----------------------------------------------------------------- Garmin: heart rate over the day
// GET /wellness-service/wellness/dailyHeartRate?date=YYYY-MM-DD
inline void filterDayHr(JsonDocument& f) {
  f["startTimestampLocal"] = true;
  f["startTimestampGMT"] = true;
  f["heartRateValues"] = true;
}
inline void parseDayHr(const JsonDocument& doc, Coach& c) {
  // the samples carry UTC milliseconds; the two start stamps give the local offset
  const char* sl = doc["startTimestampLocal"] | "";
  const char* sg = doc["startTimestampGMT"] | "";
  int offMin = (strlen(sl) >= 16 && strlen(sg) >= 16) ? ((atoi(sl + 11) - atoi(sg + 11) + 24) % 24) * 60 : 60;
  c.dayHr.clear();
  for (JsonArrayConst p : doc["heartRateValues"].as<JsonArrayConst>()) {
    if (p[1].isNull()) continue;
    long long s = p[0].as<long long>() / 1000 + offMin * 60;
    c.dayHr.push_back({(int)((s % 86400) / 60), p[1].as<int>()});
  }
}

// ----------------------------------------------------------------- Garmin: one run from its FIT file
// GET /download-service/files/activity/{id} answers with a ZIP that holds one FIT file.
// Minimal FIT reader: calls onRecord(distance m, speed m/s, heart rate, altitude m or NAN)
// for every "record" message (global number 20) that has distance and speed.
template <typename F>
inline bool fitRecords(const uint8_t* raw, size_t len, F onRecord) {
  if (len < 14 || raw[0] < 12 || memcmp(raw + 8, ".FIT", 4) != 0) return false;
  size_t pos = raw[0];
  size_t end = pos + ((size_t)raw[4] | ((size_t)raw[5] << 8) | ((size_t)raw[6] << 16) | ((size_t)raw[7] << 24));
  if (end > len) end = len;
  struct Field { uint8_t num, size; };
  struct Def { bool used = false, bigEndian = false; uint16_t global = 0; uint8_t n = 0; uint16_t devSize = 0; Field f[80]; };
  static Def* defs = nullptr;                       // 16 local message types
  if (!defs) defs = new Def[16];
  for (int i = 0; i < 16; i++) defs[i].used = false;
  while (pos < end) {
    uint8_t h = raw[pos++];
    bool compressed = h & 0x80;
    int local = compressed ? (h >> 5) & 3 : h & 0x0F;
    if (!compressed && (h & 0x40)) {                // definition message
      if (pos + 5 > end) return false;
      Def& d = defs[local];
      d.bigEndian = raw[pos + 1] != 0;
      d.global = d.bigEndian ? (raw[pos + 2] << 8) | raw[pos + 3] : raw[pos + 2] | (raw[pos + 3] << 8);
      int n = raw[pos + 4];
      pos += 5;
      if (pos + 3 * (size_t)n > end) return false;
      d.n = n > 80 ? 80 : n;
      size_t skipped = 0;                           // fields beyond the table are skipped as bytes
      for (int i = 0; i < n; i++) {
        if (i < 80) d.f[i] = {raw[pos + 3 * i], raw[pos + 3 * i + 1]};
        else skipped += raw[pos + 3 * i + 1];
      }
      pos += 3 * (size_t)n;
      d.devSize = (uint16_t)skipped;
      if (h & 0x20) {                               // developer fields
        if (pos >= end) return false;
        int nd = raw[pos++];
        if (pos + 3 * (size_t)nd > end) return false;
        for (int i = 0; i < nd; i++) d.devSize += raw[pos + 3 * i + 1];
        pos += 3 * (size_t)nd;
      }
      d.used = true;
      continue;
    }
    const Def& d = defs[local];
    if (!d.used) return false;
    bool hasDist = false, hasSpeed = false, hasSpeedE = false, hasAlt = false, hasAltE = false;
    uint32_t dist = 0, speed = 0, speedE = 0, alt = 0, altE = 0, hr = 0;
    for (int i = 0; i < d.n; i++) {
      uint8_t sz = d.f[i].size;
      if (pos + sz > end) return false;
      if (d.global == 20 && (sz == 1 || sz == 2 || sz == 4)) {
        uint32_t v = 0, invalid = sz == 1 ? 0xFFu : (sz == 2 ? 0xFFFFu : 0xFFFFFFFFu);
        for (int b = 0; b < sz; b++) v |= (uint32_t)raw[pos + (d.bigEndian ? sz - 1 - b : b)] << (8 * b);
        if (v != invalid) switch (d.f[i].num) {
          case 5: dist = v; hasDist = true; break;
          case 6: speed = v; hasSpeed = true; break;
          case 73: speedE = v; hasSpeedE = true; break;
          case 2: alt = v; hasAlt = true; break;
          case 78: altE = v; hasAltE = true; break;
          case 3: hr = v; break;
        }
      }
      pos += sz;
    }
    pos += d.devSize;
    if (d.global == 20 && hasDist && (hasSpeed || hasSpeedE)) {
      float a = hasAltE ? altE / 5.0f - 500 : (hasAlt ? alt / 5.0f - 500 : NAN);
      onRecord(dist / 100.0f, (hasSpeedE ? speedE : speed) / 1000.0f, (int)hr, a);
    }
  }
  return true;
}

// Pace, heart rate and altitude of a run in RUN_POINTS steps along its distance.
static const int RUN_POINTS = 160;
inline bool parseFitRun(const uint8_t* raw, size_t len, Coach& c) {
  float total = 0;
  int count = 0;
  if (!fitRecords(raw, len, [&](float dist, float, int, float) { if (dist > total) total = dist; count++; })) return false;
  if (count < 10 || total < 100) return false;
  struct Bin { float v = 0, hr = 0, alt = 0; int n = 0, hrN = 0, altN = 0; };
  std::vector<Bin> bins(RUN_POINTS);
  float step = total / RUN_POINTS;
  fitRecords(raw, len, [&](float dist, float v, int hr, float alt) {
    int k = (int)(dist / step);
    Bin& b = bins[k < 0 ? 0 : (k >= RUN_POINTS ? RUN_POINTS - 1 : k)];
    b.v += v; b.n++;
    if (hr > 0) { b.hr += hr; b.hrN++; }
    if (!isnan(alt)) { b.alt += alt; b.altN++; }
  });
  c.runKm.assign(RUN_POINTS, 0);
  c.runPace.assign(RUN_POINTS, NAN);
  c.runHr.assign(RUN_POINTS, NAN);
  c.runAlt.assign(RUN_POINTS, NAN);
  for (int k = 0; k < RUN_POINTS; k++) {
    const Bin& b = bins[k];
    c.runKm[k] = (k + 0.5f) * step / 1000.0f;
    if (!b.n) continue;
    float v = b.v / b.n;
    if (v > 0.8f) c.runPace[k] = roundf(1000.0f / v);   // s/km, gap while standing
    if (b.hrN) c.runHr[k] = roundf(b.hr / b.hrN);
    if (b.altN) c.runAlt[k] = b.alt / b.altN;
  }
  return true;
}
