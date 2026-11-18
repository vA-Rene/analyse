/*
 * File: metrics.h
 * Purpose: Everything the dashboard works out from the raw data:
 *  - training load (TRIMP), fitness (42-day) and fatigue (7-day)
 *  - weekly kilometres and run days
 *  - resting-HR / breathing baseline and an infection hint
 *  - today's recommendation, the best running window, clothing
 *  - race time predictions (Riegel formula)
 * All rules are deliberately simple so the screen can explain itself.
 */
#pragma once
#include <math.h>
#include <algorithm>
#include <vector>
#include "model.h"

inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Banister TRIMP for one activity (male weighting)
inline float activityLoad(const Activity& a, int hrRest, int hrMax) {
  if (a.avgHr > hrRest && hrMax > hrRest) {
    float r = clampf((float)(a.avgHr - hrRest) / (float)(hrMax - hrRest), 0, 1);
    return a.minutes * r * 0.64f * expf(1.92f * r);
  }
  return a.minutes * TRIMP_PER_MIN_NO_HR * (a.run ? 1.0f : 0.5f);
}

// ----------------------------------------------------------------- Daniels VDOT
// oxygen cost of running at v metres/minute
inline float vo2AtSpeed(float v) { return -4.60f + 0.182258f * v + 0.000104f * v * v; }
// fraction of VO2max that can be held for t minutes
inline float vo2Fraction(float t) { return 0.8f + 0.1894393f * expf(-0.012778f * t) + 0.2989558f * expf(-0.1932605f * t); }
inline float vdotFromRace(float metres, float minutes) {
  return minutes > 0 ? vo2AtSpeed(metres / minutes) / vo2Fraction(minutes) : 0;
}
// heart-rate reserve as fraction of VO2 reserve (Swain); only for runs above 50 % HRR
inline float vdotFromHeartRate(float metres, float minutes, int avgHr, int hrRest, int hrMax) {
  if (avgHr <= 0 || minutes <= 0 || hrMax <= hrRest) return 0;
  float hrr = (float)(avgHr - hrRest) / (float)(hrMax - hrRest);
  if (hrr < 0.5f || hrr > 1.0f) return 0;
  return 3.5f + (vo2AtSpeed(metres / minutes) - 3.5f) / hrr;
}
// race time in seconds for a distance at a given VDOT (bisection)
inline float raceSeconds(float metres, float vdot) {
  float lo = 5, hi = 600;
  for (int i = 0; i < 40; i++) {
    float mid = (lo + hi) / 2;
    if (vdotFromRace(metres, mid) > vdot) lo = mid; else hi = mid;
  }
  return (lo + hi) / 2 * 60;
}

inline int medianInt(int* v, int n) {
  for (int i = 1; i < n; i++) for (int j = i; j > 0 && v[j] < v[j - 1]; j--) { int t = v[j]; v[j] = v[j - 1]; v[j - 1] = t; }
  return n ? v[n / 2] : -1;
}

// ----------------------------------------------------------------- health baseline + infection hint
inline void computeHealth(DashboardData& D) {
  Metrics& m = D.m;
  int rhr[HEALTH_DAYS], rn = 0, resp[HEALTH_DAYS], pn = 0, bed[HEALTH_DAYS], bn = 0;
  float sleepSum = 0; int sleepN = 0;
  for (int i = 0; i < HEALTH_DAYS - 1; i++) {  // earlier days only, today is compared against them
    const HealthDay& h = D.days[i];
    if (h.rhr > 0 && h.sleepH > 3) rhr[rn++] = h.rhr;          // only nights the watch was worn
    if (h.resp > 0) resp[pn++] = (int)lroundf(h.resp * 10);
  }
  for (int i = 0; i < HEALTH_DAYS; i++) {
    const HealthDay& h = D.days[i];
    if (h.sleepH > 3) { sleepSum += h.sleepH; sleepN++; }
    if (h.bedMin >= 0) bed[bn++] = h.bedMin;
  }
  m.baselineNights = rn;
  m.rhrBaseline = rn >= 3 ? medianInt(rhr, rn) : -1;
  m.respBaseline = pn >= 3 ? medianInt(resp, pn) / 10.0f : -1;
  m.sleepAvgH = sleepN ? sleepSum / sleepN : -1;
  m.bedAvgMin = bn ? medianInt(bed, bn) : -1;

  // intensity minutes this week (Monday to today)
  m.intensityWeek = 0;
  int wd = weekdayMon0(D.todayNum);
  for (int i = HEALTH_DAYS - 1 - wd; i < HEALTH_DAYS; i++) if (i >= 0) m.intensityWeek += D.days[i].intensity;

  const HealthToday& t = D.today;
  if (m.rhrBaseline < 0 || t.rhr < 0) {
    m.healthStatus = -1;
    snprintf(m.healthText, sizeof(m.healthText), "Basis: %d von 3 Nächten", m.baselineNights);
    return;
  }
  int dRhr = t.rhr - m.rhrBaseline;
  float dResp = (t.resp > 0 && m.respBaseline > 0) ? t.resp - m.respBaseline : 0;
  if (dRhr >= 5 && dResp >= 1.5f) {
    m.healthStatus = 2;
    snprintf(m.healthText, sizeof(m.healthText), "Puls +%d, Atmung erhöht", dRhr);
  } else if (dRhr >= 5 || dResp >= 1.5f) {
    m.healthStatus = 1;
    if (dRhr >= 5) snprintf(m.healthText, sizeof(m.healthText), "Ruhepuls +%d über Basis", dRhr);
    else snprintf(m.healthText, sizeof(m.healthText), "Atmung nachts erhöht");
  } else {
    m.healthStatus = 0;
    snprintf(m.healthText, sizeof(m.healthText), "Keine Auffälligkeiten");
  }
}

// ----------------------------------------------------------------- load model, weeks, last run
inline void computeTraining(DashboardData& D) {
  Metrics& m = D.m;
  int hrRest = m.rhrBaseline > 0 ? m.rhrBaseline : DEFAULT_HR_REST;
  int hrMax = D.hrMax;
  for (int i = 0; i < D.actN; i++) if (D.acts[i].maxHr > hrMax) hrMax = D.acts[i].maxHr;

  float daily[LOAD_DAYS] = {0};
  long first = D.todayNum - (LOAD_DAYS - 1);
  m.lastRunIdx = -1;
  for (int i = 0; i < D.actN; i++) {
    Activity& a = D.acts[i];
    a.load = activityLoad(a, hrRest, hrMax);
    if (a.day >= first && a.day <= D.todayNum) daily[a.day - first] += a.load;
    if (a.run) m.lastRunIdx = i;
  }
  float atl = 0, ctl = 0;
  for (int d = 0; d < LOAD_DAYS; d++) {
    atl += (daily[d] - atl) / 7.0f;
    ctl += (daily[d] - ctl) / 42.0f;
    int k = d - (LOAD_DAYS - CHART_LOAD_DAYS);
    if (k >= 0) { m.ctlSeries[k] = ctl; m.atlSeries[k] = atl; }
    if (d == LOAD_DAYS - 8) m.ctlWeekAgo = ctl;
  }
  m.ctl = ctl;
  m.atl = atl;

  // weekly kilometres (Monday-based), oldest first
  m.todayWeekday = weekdayMon0(D.todayNum);
  long thisMonday = D.todayNum - m.todayWeekday;
  m.runDaysThisWeek = 0;
  for (int w = 0; w < WEEKS_SHOWN; w++) {
    m.weekStart[w] = thisMonday - 7L * (WEEKS_SHOWN - 1 - w);
    m.weekKm[w] = 0;
  }
  for (int i = 0; i < D.actN; i++) {
    const Activity& a = D.acts[i];
    if (!a.run) continue;
    long wk = (a.day - m.weekStart[0]);
    if (wk < 0) continue;
    int w = (int)(wk / 7);
    if (w < WEEKS_SHOWN) m.weekKm[w] += a.km;
    if (a.day >= thisMonday) m.runDaysThisWeek |= (1 << (a.day - thisMonday));
  }
  m.daysSinceRun = m.lastRunIdx >= 0 ? (int)(D.todayNum - D.acts[m.lastRunIdx].day) : -1;

  // race predictions (Daniels' VDOT) from the runs of the last 42 days:
  //  - every run >= 20 min with heart rate: pace scaled by heart-rate reserve
  //  - hard efforts (average HR >= 88 % of max): fastest 5/10/21 km split as a race
  // The median of the easy-run estimates and the best hard effort are combined (max).
  float est[DashboardData::MAX_ACT]; int en = 0; float hard = 0;
  for (int i = 0; i < D.actN; i++) {
    const Activity& a = D.acts[i];
    if (!a.run || D.todayNum - a.day > 42 || a.minutes < 20 || a.km < 3) continue;
    float v = vdotFromHeartRate(a.km * 1000, a.minutes, a.avgHr, hrRest, hrMax);
    if (v > 0) est[en++] = v;
    if (a.avgHr >= 0.88f * hrMax) {
      const float dist[3] = {5000, 10000, 21097.5f};
      const float sec[3] = {a.best5k, a.best10k, a.bestHalf};
      for (int k = 0; k < 3; k++) if (sec[k] > 0) hard = fmaxf(hard, vdotFromRace(dist[k], sec[k] / 60));
    }
  }
  for (int i = 1; i < en; i++) for (int j = i; j > 0 && est[j] < est[j - 1]; j--) { float t = est[j]; est[j] = est[j - 1]; est[j - 1] = t; }
  m.vdot = fmaxf(en ? est[en / 2] : 0, hard);
  m.predN = 0;
  if (m.vdot >= 20) {
    static const char* labels[4] = {"5 km", "10 km", "Halbmarathon", "Marathon"};
    const float target[4] = {5000, 10000, 21097.5f, 42195};
    for (int k = 0; k < 4; k++) m.pred[m.predN++] = {labels[k], raceSeconds(target[k], m.vdot)};
  }
}

// ----------------------------------------------------------------- weather for running
inline bool isDark(const Weather& w, long day, int hour, long today) {
  int rise = day == today ? w.sunriseMin : w.sunriseTomorrowMin;
  int set = day == today ? w.sunsetMin : w.sunsetTomorrowMin;
  if (rise < 0 || set < 0) return false;
  int mid = hour * 60 + 30;
  return mid < rise || mid > set;
}

inline void clothingFor(float feels, int prob, float wind, bool dark, char* out, size_t n) {
  const char* base;
  if (feels < 0) base = "Lang, Mütze, Handschuhe";
  else if (feels < 5) base = "Lang + Handschuhe";
  else if (feels < 10) base = "Lang, Langarm";
  else if (feels < 16) base = "Kurz + Langarm";
  else base = "Kurz/kurz";
  snprintf(out, n, "%s%s%s%s", base, prob >= 40 ? ", Regenjacke" : "", (wind >= 25 && prob < 40) ? ", Windjacke" : "",
           dark ? ", Licht" : "");
}

inline void computeWeatherAdvice(DashboardData& D) {
  Metrics& m = D.m;
  const Weather& w = D.wx;
  m.windowText[0] = m.clothesText[0] = m.rainText[0] = m.darkText[0] = 0;
  if (!w.ok) return;
  int nowHour = D.nowMin / 60;

  // plan today until 21:00, after 19:00 plan tomorrow 07-20
  bool tomorrow = nowHour >= 19;
  long planDay = tomorrow ? D.todayNum + 1 : D.todayNum;
  int from = tomorrow ? 7 : nowHour, to = tomorrow ? 20 : 21;
  int best = -1; float bestScore = 1e9;
  for (int i = 0; i < w.hourN; i++) {
    if (w.hourDay[i] != planDay || w.hourOfDay[i] < from || w.hourOfDay[i] >= to) continue;
    bool dark = isDark(w, w.hourDay[i], w.hourOfDay[i], D.todayNum);
    float score = w.prob[i] + w.precip[i] * 40 + (dark ? 35 : 0) + (w.wind[i] > 25 ? 15 : 0) +
                  fabsf(w.feels[i] - 10) * 1.2f + (w.hourOfDay[i] < 7 ? 20 : 0);
    if (score < bestScore) { bestScore = score; best = i; }
  }
  if (best >= 0) {
    int h = w.hourOfDay[best];
    bool dark = isDark(w, w.hourDay[best], h, D.todayNum);
    const char* rain = w.prob[best] < 20 ? "trocken" : (w.prob[best] < 50 ? "evtl. Regen" : "Regen");
    snprintf(m.windowText, sizeof(m.windowText), "%s%02d–%02d Uhr • %s • %d°", tomorrow ? "Morgen " : "",
             h, h + 1, rain, (int)lroundf(w.temp[best]));
    clothingFor(w.feels[best], w.prob[best], w.wind[best], dark, m.clothesText, sizeof(m.clothesText));
  }

  // dry until / rain from
  int rainAt = -1, first = -1;   // the hourly list starts at midnight
  for (int i = 0; i < w.hourN; i++) {
    if (w.hourDay[i] < D.todayNum || (w.hourDay[i] == D.todayNum && w.hourOfDay[i] < nowHour)) continue;
    if (first < 0) first = i;
    if (i - first >= 24) break;
    if (w.prob[i] >= 50) { rainAt = i; break; }
  }
  if (rainAt >= 0 && rainAt == first) snprintf(m.rainText, sizeof(m.rainText), "Regen jetzt");
  else if (rainAt > 0) snprintf(m.rainText, sizeof(m.rainText), "trocken bis %02d:00", w.hourOfDay[rainAt]);
  else snprintf(m.rainText, sizeof(m.rainText), "trocken (24 h)");

  int set = w.sunsetMin;
  if (set >= 0 && D.nowMin < set) snprintf(m.darkText, sizeof(m.darkText), "dunkel ab %02d:%02d", set / 60, set % 60);
  else if (w.sunriseTomorrowMin >= 0)
    snprintf(m.darkText, sizeof(m.darkText), "hell ab %02d:%02d", w.sunriseTomorrowMin / 60, w.sunriseTomorrowMin % 60);
}

// ----------------------------------------------------------------- today's recommendation
inline void computeRecommendation(DashboardData& D) {
  Metrics& m = D.m;
  const HealthToday& t = D.today;
  int bb = t.bbWake > 0 ? t.bbWake : t.bbHigh;
  float sleep = t.sleepH;
  int dRhr = (t.rhr > 0 && m.rhrBaseline > 0) ? t.rhr - m.rhrBaseline : 0;
  bool fatigued = m.atl > m.ctl * 1.4f && m.atl > 30;
  bool ranToday = m.daysSinceRun == 0;

  // recovery word for the left column
  if (bb < 0) strcpy(m.recoveryWord, "keine Daten");
  else if (bb >= 75) strcpy(m.recoveryWord, "gut erholt");
  else if (bb >= 50) strcpy(m.recoveryWord, "erholt");
  else if (bb >= 30) strcpy(m.recoveryWord, "etwas müde");
  else strcpy(m.recoveryWord, "erschöpft");

  if (D.garminStatus[0]) {
    m.recoveryLevel = 2;
    strcpy(m.recTitle, "Nach Gefühl laufen");
    strcpy(m.recReason, "Keine aktuellen Garmin-Daten");
  } else if (m.healthStatus == 2) {
    m.recoveryLevel = 0;
    strcpy(m.recTitle, "Pause");
    snprintf(m.recReason, sizeof(m.recReason), "Ruhepuls und Atmung erhöht");
  } else if (ranToday) {
    m.recoveryLevel = 1;
    strcpy(m.recTitle, "Erledigt – erholen");
    char k[12]; fmtDec(k, sizeof(k), D.acts[m.lastRunIdx].km, 1);
    snprintf(m.recReason, sizeof(m.recReason), "Heute schon %s km gelaufen", k);
  } else if ((bb >= 0 && bb < 30) || (sleep > 0 && sleep < 5.5f)) {
    m.recoveryLevel = 0;
    strcpy(m.recTitle, "Ruhetag");
    if (bb >= 0 && bb < 30) snprintf(m.recReason, sizeof(m.recReason), "Body Battery nur %d", bb);
    else { char s[12]; fmtDec(s, sizeof(s), sleep, 1); snprintf(m.recReason, sizeof(m.recReason), "Nur %s h Schlaf", s); }
  } else if (fatigued || m.healthStatus == 1 || (bb >= 0 && bb < 50)) {
    m.recoveryLevel = 1;
    strcpy(m.recTitle, "Locker 30–40 min");
    if (m.healthStatus == 1) snprintf(m.recReason, sizeof(m.recReason), "%s", m.healthText);
    else if (fatigued) snprintf(m.recReason, sizeof(m.recReason), "Ermüdung hoch (%d zu Fitness %d)", (int)m.atl, (int)m.ctl);
    else snprintf(m.recReason, sizeof(m.recReason), "Body Battery %d", bb);
  } else if (bb >= 75 && sleep >= 7 && dRhr <= 2 && m.daysSinceRun != 0 && m.atl <= m.ctl * 1.2f + 5) {
    m.recoveryLevel = 3;
    bool weekend = m.todayWeekday >= 5;
    strcpy(m.recTitle, weekend ? "Langer Lauf möglich" : "Tempo möglich");
    char s[12]; fmtDec(s, sizeof(s), sleep, 1);
    snprintf(m.recReason, sizeof(m.recReason), "Body Battery %d, %s h Schlaf", bb, s);
  } else {
    m.recoveryLevel = 2;
    strcpy(m.recTitle, "Locker 40–50 min");
    char s[12]; fmtDec(s, sizeof(s), sleep, 1);
    if (bb >= 0 && sleep > 0) snprintf(m.recReason, sizeof(m.recReason), "Body Battery %d, %s h Schlaf", bb, s);
    else strcpy(m.recReason, "Wenig Erholungsdaten");
  }
}

// ================================================================= coach
// Personal rates, running windows for the week, the sentence for today and the
// lever for tonight. Works on the longer history in D.c (model.h).
static const char* WEEKDAY_NAME[7] = {"Montag", "Dienstag", "Mittwoch", "Donnerstag", "Freitag", "Samstag", "Sonntag"};
inline float medianF(std::vector<float> v) {
  if (v.empty()) return NAN;
  std::sort(v.begin(), v.end());
  return v.size() % 2 ? v[v.size() / 2] : (v[v.size() / 2 - 1] + v[v.size() / 2]) / 2;
}
inline int afterNoon(int minOfDay) { return (minOfDay + 720) % 1440; }
inline void fmtClock(char* out, size_t n, int minAfterNoon) {
  int m = (minAfterNoon + 720) % 1440;
  snprintf(out, n, "%02d:%02d", m / 60, m % 60);
}

// ----------------------------------------------------------------- coach logic
inline const CoachDay* coachDay(const Coach& X, long day) {
  for (auto& d : X.days) if (d.day == day) return &d;
  return nullptr;
}

inline void computeCoach(DashboardData& D) {
  Coach& X = D.c;
  const Metrics& m = D.m;
  // runs and strength days from the activity list
  X.runs.clear();
  X.strengthDays.clear();
  for (int i = 0; i < D.actN; i++) {
    const Activity& a = D.acts[i];
    if (a.strength) X.strengthDays.push_back(a.day);
    if (!a.run) continue;
    CoachRun r;
    r.day = a.day; r.km = a.km; r.minutes = a.minutes; r.te = a.te; r.anTe = a.anTe;
    r.avgHr = a.avgHr; r.maxHr = a.maxHr;
    for (int z = 0; z < 5; z++) r.zoneMin[z] = a.zoneMin[z];
    strcpy(r.type, a.type);
    X.runs.push_back(r);
  }
  for (auto& w : X.win) w = RunWindow();
  X.bestDay[0] = X.bestDay[1] = -1;
  X.coachHead[0] = X.coachBody[0] = X.tipHead[0] = X.tipBody[0] = 0;
  X.bbBedProjected = -1;
  X.chargePerHour = 9.5f; X.drainPerHour = 4.0f;
  X.usualBed = X.usualWake = -1;

  const HealthToday& t = D.today;
  // personal rates from nights with the watch on
  std::vector<float> rate, drain, bed, wake;
  for (auto& n : X.nights) {
    bed.push_back(n.bedMin);
    wake.push_back(n.wakeMin);
    const CoachDay* d = coachDay(X, n.day);
    if (!d || n.sleepH < 3) continue;
    if (d->charged > 0 && d->bbMax < 100) rate.push_back(d->charged / n.sleepH);
    if (d->drained > 0 && d->day != D.todayNum) drain.push_back(d->drained / (24 - n.sleepH));
  }
  if (rate.size() >= 3) X.chargePerHour = medianF(rate);
  if (drain.size() >= 3) X.drainPerHour = medianF(drain);
  if (bed.size() >= 3) { X.usualBed = (int)medianF(bed); X.usualWake = (int)medianF(wake); }

  // running windows for 7 days (same scoring as the firmware)
  int nowHour = D.nowMin / 60;
  for (auto& h : X.hours) {
    int k = (int)(h.day - X.wxFirstDay);
    if (k < 0 || k > 6 || h.hour < 7 || h.hour > 20) continue;
    if (h.day == D.todayNum && h.hour < nowHour) continue;
    int mid = h.hour * 60 + 30;
    bool dark = mid < X.sunrise[k] || mid > X.sunset[k];
    float score = h.prob + h.precip * 40 + (dark ? 35 : 0) + (h.wind > 25 ? 15 : 0) + fabsf(h.feels - 10) * 1.2f;
    if (score < X.win[k].score) X.win[k] = {h.day, h.hour, h.temp, h.feels, h.wind, score, h.prob, dark};
  }
  std::vector<int> order;
  for (int k = 0; k < 7; k++) if (X.win[k].hour >= 0) order.push_back(k);
  std::sort(order.begin(), order.end(), [&](int a, int b) { return X.win[a].score < X.win[b].score; });
  for (int i = 0; i < 2 && i < (int)order.size(); i++) X.bestDay[i] = order[i];
  if (X.bestDay[1] >= 0 && X.bestDay[0] > X.bestDay[1]) std::swap(X.bestDay[0], X.bestDay[1]);

  // ---- coach sentence: the one thing with the biggest effect today
  const CoachRun* last = X.runs.empty() ? nullptr : &X.runs.back();
  float z[5] = {0, 0, 0, 0, 0}, zTot = 0;
  int recent = 0, base = 0;
  for (auto& r : X.runs) {
    if (D.todayNum - r.day > 14) continue;
    recent++;
    if (!strcmp(r.type, "Grundlage") || !strcmp(r.type, "Erholung") || !strcmp(r.type, "Ausdauer")) base++;
    for (int i = 0; i < 5; i++) { z[i] += r.zoneMin[i]; zTot += r.zoneMin[i]; }
  }
  float hardShare = zTot > 0 ? (z[2] + z[3] + z[4]) / zTot : 0;
  bool nightOk = t.sleepH > 0 && !(t.deepH == 0 && t.awakeH == 0) && t.sleepH >= 3;
  int bb = t.bbWake > 0 ? t.bbWake : t.bbHigh;
  bool tired = (bb >= 0 && bb < 35) || (nightOk && t.sleepH < 5.5f);
  float kmWeek = m.weekKm[WEEKS_SHOWN - 1], kmPrev = m.weekKm[WEEKS_SHOWN - 2];
  char wd[16] = "";
  if (last) snprintf(wd, sizeof(wd), "%s", WEEKDAY_NAME[weekdayMon0(last->day)]);

  if (D.garminStatus[0]) {
    strcpy(X.coachHead, "Heute nach Gefühl");
    snprintf(X.coachBody, sizeof(X.coachBody), "Garmin: %s. Lauf, wie es sich gut anfühlt.", D.garminStatus);
  } else if (m.healthStatus == 2) {
    strcpy(X.coachHead, "Heute Pause");
    strcpy(X.coachBody, "Ruhepuls und Atmung sind erhöht. Ein Tag Ruhe kostet nichts.");
  } else if (m.daysSinceRun == 0) {
    strcpy(X.coachHead, "Für heute erledigt");
    strcpy(X.coachBody, "Der Lauf ist drin. Besser wirst du jetzt in der Erholung.");
  } else if (last && D.todayNum - last->day == 1 && (last->te >= 3.5f || last->anTe >= 2)) {
    strcpy(X.coachHead, "Heute locker oder Pause");
    snprintf(X.coachBody, sizeof(X.coachBody),
             "Gestern war fordernd. Dein Körper baut jetzt auf, lass ihn.");
  } else if (tired) {
    strcpy(X.coachHead, "Heute Pause oder Spaziergang");
    strcpy(X.coachBody, "Wenig Energie im Tank. Morgen läuft es dafür besser.");
  } else if (kmPrev > 10 && kmWeek > kmPrev * 1.4f) {
    strcpy(X.coachHead, "Heute lieber Pause");
    snprintf(X.coachBody, sizeof(X.coachBody),
             "Schon %d km mehr als letzte Woche. Sehnen wachsen langsam.", (int)(kmWeek - kmPrev));
  } else if (last && recent >= 1 && hardShare >= 0.5f) {
    snprintf(X.coachHead, sizeof(X.coachHead), "Heute locker, Puls unter %d", X.zoneFloor[2]);
    float lastHard = last->zoneMin[2] + last->zoneMin[3] + last->zoneMin[4];
    snprintf(X.coachBody, sizeof(X.coachBody),
             "Am %s liefen %d von %d min ab Zone 3. Langsamer baut mehr Grundlage auf.", wd, (int)lroundf(lastHard), (int)lroundf(last->minutes));
  } else if (recent >= 4 && base == recent && bb >= 70) {
    strcpy(X.coachHead, "Zeit für Tempo");
    snprintf(X.coachBody, sizeof(X.coachBody),
             "Deine letzten %d Läufe waren locker. 6 x 3 min zügig stärken das Herz.", recent);
  } else {
    snprintf(X.coachHead, sizeof(X.coachHead), "%s", m.recTitle);
    snprintf(X.coachBody, sizeof(X.coachBody), "%s. Locker laufen baut Grundlage auf.", m.recReason);
  }

  // ---- evening lever: earlier to bed
  if (X.usualBed >= 0 && t.bbNow >= 0) {
    int nowRel = D.nowMin < 5 * 60 ? D.nowMin + 720 : D.nowMin - 720;   // minutes after noon, negative in the morning
    auto wakeBb = [&](int bedAfterNoon) {
      float awakeH = (bedAfterNoon - nowRel) / 60.0f;
      if (awakeH < 0) awakeH = 0;
      float atBed = fmaxf(5, t.bbNow - X.drainPerHour * awakeH);
      float sleepH = (X.usualWake - bedAfterNoon) / 60.0f - 0.3f;
      return fminf(100, atBed + X.chargePerHour * sleepH);
    };
    X.bbBedProjected = (int)fmaxf(5, t.bbNow - X.drainPerHour * fmaxf(0, (X.usualBed - nowRel) / 60.0f));
    float usual = wakeBb(X.usualBed);
    int target = -1;
    for (int b = X.usualBed - 30; b >= X.usualBed - 150 && b >= 600; b -= 30) {  // not before 22:00
      int snapped = b / 30 * 30;
      if (wakeBb(snapped) - usual >= 10) { target = snapped; break; }
    }
    if (target >= 0) {
      char c[8];
      fmtClock(c, sizeof(c), target);
      snprintf(X.tipHead, sizeof(X.tipHead), "%s ins Bett", c);
      snprintf(X.tipBody, sizeof(X.tipBody), "Dann startest du morgen mit ~%d statt ~%d.",
               (int)lroundf(wakeBb(target) / 5) * 5, (int)lroundf(usual / 5) * 5);
    }
  }
  if (!X.tipHead[0] && X.nights.size() >= 4) {
    int lo = 9999, hi = -1, n = 0;
    for (auto& nn : X.nights) if (D.todayNum - nn.day < 14) { lo = std::min(lo, nn.bedMin); hi = std::max(hi, nn.bedMin); n++; }
    if (n >= 4 && hi - lo >= 120) {
      char a[8], b[8];
      fmtClock(a, sizeof(a), lo); fmtClock(b, sizeof(b), hi);
      strcpy(X.tipHead, "Feste Schlafenszeit");
      snprintf(X.tipBody, sizeof(X.tipBody), "Mal %s, mal %s. Gleiche Zeit lädt besser.", a, b);
    }
  }
  if (!X.tipHead[0]) {
    strcpy(X.tipHead, "Alles im Lot");
    strcpy(X.tipBody, "Heute gibt es nichts zu verbessern.");
  }
}

inline void computeAll(DashboardData& D) {
  computeHealth(D);
  computeTraining(D);
  computeWeatherAdvice(D);
  computeRecommendation(D);
  computeCoach(D);
}

