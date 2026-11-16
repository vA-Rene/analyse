/*
 * File: model.h
 * Purpose: Plain data structures for everything the screens show. Filled by
 * parse.h (raw API answers) and metrics.h (derived values). No Arduino or
 * network code here, so the same structures also work in the PC test renderer.
 */
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "config.h"

// ----------------------------------------------------------------- dates
// Days since 1970-01-01 for a civil date (no time zone involved).
inline long dayNumber(int y, int m, int d) {
  y -= m <= 2;
  long era = (y >= 0 ? y : y - 399) / 400;
  long yoe = y - era * 400;
  long doy = (153L * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}
// "2026-11-14..." -> day number, -1 if malformed
inline long dayNumberFromIso(const char* s) {
  if (!s || strlen(s) < 10) return -1;
  int y = atoi(s), m = atoi(s + 5), d = atoi(s + 8);
  if (y < 2000 || m < 1 || m > 12 || d < 1 || d > 31) return -1;
  return dayNumber(y, m, d);
}
inline void isoFromDayNumber(long z, char* out /* 11 bytes */) {
  z += 719468;
  long era = (z >= 0 ? z : z - 146096) / 146097;
  long doe = z - era * 146097;
  long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  long y = yoe + era * 400;
  long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  long mp = (5 * doy + 2) / 153;
  long d = doy - (153 * mp + 2) / 5 + 1;
  long m = mp + (mp < 10 ? 3 : -9);
  y += m <= 2;
  snprintf(out, 11, "%04ld-%02ld-%02ld", y, m, d);
}
// 0 = Monday ... 6 = Sunday
inline int weekdayMon0(long dayNum) { return (int)((dayNum + 3) % 7 + 7) % 7; }

// decimal comma
inline void fmtDec(char* out, size_t n, float v, int decimals) {
  snprintf(out, n, "%.*f", decimals, v);
  for (char* p = out; *p; p++) if (*p == '.') *p = ',';
}

// ----------------------------------------------------------------- health
struct HealthDay {
  long day = -1;          // day number
  int rhr = -1;           // resting heart rate, -1 = unknown or implausible
  int bbMax = -1, bbMin = -1;
  int stress = -1;
  int intensity = 0;      // moderate + 2 x vigorous minutes (Garmin's weighting)
  float sleepH = -1;      // hours asleep in the night ending on this day
  float resp = -1;        // average breaths per minute during that sleep
  int bedMin = -1;        // falling asleep, minutes after 12:00 (so 23:30 = 690, 01:30 = 810)
};

struct HealthToday {
  bool ok = false;
  int bbNow = -1, bbWake = -1, bbHigh = -1, bbLow = -1;
  int rhr = -1, rhr7 = -1;
  int stressAvg = -1;
  long steps = -1;
  int intensityWeek = -1;           // filled from the daily list
  float sleepH = -1, deepH = 0, lightH = 0, remH = 0, awakeH = 0;
  int sleepStartMin = -1, sleepEndMin = -1;  // minutes after midnight
  float resp = -1;
  static const int CURVE_MAX = 96;
  uint8_t bbCurve[CURVE_MAX];      // today's body battery, oldest first
  int bbCurveN = 0;
};

// ----------------------------------------------------------------- activities
struct Activity {
  long day = -1;
  int startMin = 0;           // minutes after midnight, local time
  bool run = false;
  char name[28] = "";
  float km = 0;
  float minutes = 0;          // moving time
  int avgHr = 0, maxHr = 0;
  float te = 0;               // aerobic training effect
  float best5k = 0, best10k = 0, bestHalf = 0;  // fastest splits inside the run, seconds
  float load = 0;             // TRIMP (metrics.h)
};

// ----------------------------------------------------------------- weather
struct Weather {
  bool ok = false;
  float tempNow = 0, feelsNow = 0, windNow = 0;
  int codeNow = 0;
  float rain2h = 0;             // mm in the next 2 hours (15-minute data)
  static const int HOURS = 36;
  int hourN = 0;
  long hourDay[HOURS];          // day number of each hourly entry
  int hourOfDay[HOURS];
  float temp[HOURS], feels[HOURS], precip[HOURS], wind[HOURS];
  int prob[HOURS], code[HOURS];
  int sunriseMin = -1, sunsetMin = -1;          // today
  int sunriseTomorrowMin = -1, sunsetTomorrowMin = -1;
  static const int DAYS = 4;    // today + 3
  int dayN = 0;
  long dDay[DAYS];
  float tMin[DAYS], tMax[DAYS];
  int pMax[DAYS], dCode[DAYS];
};

// ----------------------------------------------------------------- derived values
struct Prediction { const char* label; float seconds; };

struct Metrics {
  // load model
  float ctl = 0, atl = 0;              // fitness (42 d) and fatigue (7 d)
  float ctlWeekAgo = 0;
  float ctlSeries[CHART_LOAD_DAYS];
  float atlSeries[CHART_LOAD_DAYS];
  // weeks
  float weekKm[WEEKS_SHOWN];          // oldest first, last = this week
  long weekStart[WEEKS_SHOWN];
  uint8_t runDaysThisWeek = 0;        // bit 0 = Monday
  int todayWeekday = 0;               // 0 = Monday
  int lastRunIdx = -1;                // index into activities
  int daysSinceRun = -1;
  // health
  int rhrBaseline = -1;               // median of earlier nights
  float respBaseline = -1;
  int baselineNights = 0;
  int healthStatus = 0;               // 0 ok, 1 watch, 2 warning, -1 not enough data
  char healthText[64] = "";
  float sleepAvgH = -1;
  int bedAvgMin = -1;
  int intensityWeek = 0;
  // recommendation
  int recoveryLevel = 0;              // 0 rest, 1 easy, 2 normal, 3 quality
  char recoveryWord[24] = "";
  char recTitle[40] = "";
  char recReason[64] = "";
  // weather for running
  char windowText[64] = "";
  char clothesText[64] = "";
  char rainText[40] = "";
  char darkText[32] = "";
  // predictions
  float vdot = 0;
  int predN = 0;
  Prediction pred[4];
};

// ----------------------------------------------------------------- global state
struct DashboardData {
  HealthToday today;
  HealthDay days[HEALTH_DAYS];        // oldest first, last = today
  static const int MAX_ACT = 60;
  Activity acts[MAX_ACT];             // oldest first
  int actN = 0;
  Weather wx;
  Metrics m;
  long todayNum = 0;
  int nowMin = 0;                     // minutes after midnight when metrics were computed
  char garminStatus[40] = "";         // empty = ok
  char weatherStatus[40] = "";
  char updatedAt[6] = "--:--";
  int hrMax = DEFAULT_HR_MAX;
  int weeklyGoalKm = DEFAULT_WEEKLY_GOAL_KM;
};
