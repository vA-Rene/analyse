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
    snprintf(m.healthText, sizeof(m.healthText), "Basiswerte: %d von 3 Nächten", m.baselineNights);
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
  int rainAt = -1;
  for (int i = 0; i < w.hourN && i < 24; i++) if (w.prob[i] >= 50) { rainAt = i; break; }
  if (rainAt == 0) snprintf(m.rainText, sizeof(m.rainText), "Regen jetzt");
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

inline void computeAll(DashboardData& D) {
  computeHealth(D);
  computeTraining(D);
  computeWeatherAdvice(D);
  computeRecommendation(D);
}
