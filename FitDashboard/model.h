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
#include <vector>
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
  int stressMin[4] = {0, 0, 0, 0};  // minutes at rest, low, medium, high stress
};

// ----------------------------------------------------------------- activities
struct Activity {
  long long id = 0;
  long day = -1;
  int startMin = 0;           // minutes after midnight, local time
  bool run = false;
  char name[28] = "";
  float km = 0;
  float minutes = 0;          // moving time
  int avgHr = 0, maxHr = 0;
  float te = 0;               // aerobic training effect
  float anTe = 0;             // anaerobic training effect
  float zoneMin[5] = {0, 0, 0, 0, 0};  // minutes in heart-rate zones 1-5
  bool strength = false;
  char type[12] = "";         // kind of run from Garmin's training-effect label
  float best5k = 0, best10k = 0, bestHalf = 0;  // fastest splits inside the run, seconds
  float load = 0;             // TRIMP (metrics.h)
};

// ----------------------------------------------------------------- weather
struct Weather {
  bool ok = false;
  float tempNow = 0, feelsNow = 0, windNow = 0;
  int codeNow = 0;
  float rain2h = 0;             // mm in the next 2 hours (15-minute data)
  static const int HOURS = 48;   // from today 00:00
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

// ----------------------------------------------------------------- coach
// Longer history and the values derived from it: what to do today and why.
struct CoachNight {
  long day = -1;
  float sleepH = -1;
  int bedMin = -1, wakeMin = -1;   // minutes after 12:00 of the previous day
  int deepMin = 0, awakeMin = 0;
  // the watch was taken off or put on during the night
  bool incomplete() const { return sleepH > 0 && ((deepMin == 0 && awakeMin == 0) || sleepH < 3); }
};
struct CoachDay { long day = -1; int rhr = -1, bbMin = -1, bbMax = -1, charged = -1, drained = -1, steps = -1, intensity = 0; };
struct CoachRun {
  long day = -1;
  float km = 0, minutes = 0, te = 0, anTe = 0, zoneMin[5] = {0, 0, 0, 0, 0};
  int avgHr = 0, maxHr = 0;
  char type[12] = "";
};
struct CoachHour { long day; int hour; float temp, feels, precip, wind; int prob, code; };
struct RunWindow { long day = -1; int hour = -1; float temp = 0, feels = 0, wind = 0, score = 1e9; int prob = 0; bool dark = false; };

struct Coach {
  std::vector<CoachNight> nights;    // oldest first, COACH_DAYS
  std::vector<CoachDay> days;        // oldest first, COACH_DAYS
  std::vector<CoachRun> runs;        // oldest first (from the activity list)
  std::vector<long> strengthDays;    // days with a recorded strength session
  int zoneFloor[5] = {100, 120, 140, 160, 180};
  std::vector<std::pair<int, int>> dayHr;   // minute of day, bpm
  std::vector<CoachHour> hours;      // 7 days from today 00:00
  int sunrise[7] = {-1, -1, -1, -1, -1, -1, -1}, sunset[7] = {-1, -1, -1, -1, -1, -1, -1};
  long wxFirstDay = -1;
  std::vector<float> runKm, runPace, runHr, runAlt;   // newest run along its distance, NAN = gap
  long runDay = -1;
  long long runId = 0;               // activity the run track belongs to
  // derived (metrics.h)
  float chargePerHour = 9.5f, drainPerHour = 4.0f;   // body battery points per hour asleep / awake
  int usualBed = -1, usualWake = -1;                 // minutes after noon
  RunWindow win[7];
  int bestDay[2] = {-1, -1};
  char coachHead[48] = "", coachBody[200] = "";
  char tipHead[40] = "", tipBody[120] = "";
  int bbBedProjected = -1;
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
  Coach c;
  Metrics m;
  long todayNum = 0;
  int nowMin = 0;                     // minutes after midnight when metrics were computed
  char garminStatus[40] = "";         // empty = ok
  char weatherStatus[40] = "";
  char updatedAt[6] = "--:--";
  int hrMax = DEFAULT_HR_MAX;
  int weeklyGoalKm = DEFAULT_WEEKLY_GOAL_KM;
};
