/*
 * File: parse.h
 * Purpose: Turn Garmin Connect Personal API and Open-Meteo JSON answers into
 * the structures in model.h. Each parser has a matching filter so the ESP32
 * only keeps the fields it needs while streaming large answers.
 */
#pragma once
#include <ArduinoJson.h>
#include <math.h>
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
                        "averageStressLevel", "totalSteps"}) f[k] = true;
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

// ----------------------------------------------------------------- Garmin: daily list (14 days)
// GET /usersummary-service/v2/dailysummaries?startDate=..&endDate=..
inline void filterDailySummaries(JsonDocument& f) {
  JsonObject d = f["data"][0].to<JsonObject>();
  d["calendarDate"] = true;
  d["heartRate"]["restingValue"] = true;
  d["bodyBattery"]["maxValue"] = true;
  d["bodyBattery"]["minValue"] = true;
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
inline int parseDailySummaries(const JsonDocument& doc, DashboardData& D) {
  int n = 0;
  for (JsonObjectConst d : doc["data"].as<JsonArrayConst>()) {
    HealthDay* h = healthDayFor(D, dayNumberFromIso(d["calendarDate"]));
    if (!h) continue;
    h->rhr = jsonInt(d["heartRate"]["restingValue"]);
    if (h->rhr > 85) h->rhr = -1;
    h->bbMax = jsonInt(d["bodyBattery"]["maxValue"]);
    h->bbMin = jsonInt(d["bodyBattery"]["minValue"]);
    h->stress = jsonInt(d["stress"]["avgLevel"]);
    h->intensity = jsonInt(d["intensityMinutes"]["moderate"], 0) + 2 * jsonInt(d["intensityMinutes"]["vigorous"], 0);
    n++;
  }
  return n;
}

// ----------------------------------------------------------------- Garmin: sleep list (14 nights)
// GET /sleep-service/sleep/dailySleepSummariesByDate?startDate=..&endDate=..
inline void filterSleepRange(JsonDocument& f) {
  JsonObject d = f[0].to<JsonObject>();
  d["calendarDate"] = true;
  d["sleepTimeSeconds"] = true;
  d["sleepStartTimestampLocal"] = true;
  d["averageRespirationValue"] = true;
}
inline int parseSleepRange(const JsonDocument& doc, DashboardData& D) {
  int n = 0;
  for (JsonObjectConst s : doc.as<JsonArrayConst>()) {
    HealthDay* h = healthDayFor(D, dayNumberFromIso(s["calendarDate"]));
    if (!h || s["sleepTimeSeconds"].isNull()) continue;
    h->sleepH = s["sleepTimeSeconds"].as<float>() / 3600.0f;
    h->resp = jsonFloat(s["averageRespirationValue"]);
    int start = localMinutesFromMs(s["sleepStartTimestampLocal"]);
    if (start >= 0) h->bedMin = (start + 720) % 1440;  // minutes after noon
    n++;
  }
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
  for (const char* k : {"activityName", "startTimeLocal", "distance", "duration", "movingDuration", "averageHR",
                        "maxHR", "aerobicTrainingEffect", "fastestSplit_5000", "fastestSplit_10000",
                        "fastestSplit_21098"}) a[k] = true;
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
    if (x.day < 0) continue;
    x.startMin = minutesFromIsoTime(st);
    const char* type = a["activityType"]["typeKey"] | "";
    x.run = strstr(type, "running") != nullptr;
    strncpy(x.name, a["activityName"] | "", sizeof(x.name) - 1);
    x.km = jsonFloat(a["distance"], 0) / 1000.0f;
    float sec = jsonFloat(a["movingDuration"], 0);
    if (sec <= 0) sec = jsonFloat(a["duration"], 0);
    x.minutes = sec / 60.0f;
    x.avgHr = jsonInt(a["averageHR"], 0);
    x.maxHr = jsonInt(a["maxHR"], 0);
    x.te = jsonFloat(a["aerobicTrainingEffect"], 0);
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
    "&forecast_hours=36"
    "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max,sunrise,sunset"
    "&forecast_days=4";

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
