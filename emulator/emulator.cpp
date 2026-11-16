// Fit Dashboard PC emulator
// Renders the three screens with exactly the firmware's model/parse/metrics/ui code
// into 800x480 black/white images. The display class is replaced by stub/fake_epaper.h.
//
//   ./emulator --data data --out out [--date 2026-11-15] [--time 07:15] [--battery 78]
//              [--scenario normal|warn|nogarmin|noweather|nosleep]
#include <cmath>
#include <ctime>
#include <fstream>
#include <sstream>
#include <string>
#include "fake_epaper.h"
#define constrain(v, a, b) ((v) < (a) ? (a) : ((v) > (b) ? (b) : (v)))
#include "ArduinoJson.h"
EPaper epaper;
#include "model.h"
#include "parse.h"
#include "metrics.h"
#include "ui.h"

static std::string slurp(const std::string& p) {
  std::ifstream f(p);
  std::stringstream s;
  s << f.rdbuf();
  return s.str();
}

static bool load(const std::string& p, JsonDocument& doc, void (*filt)(JsonDocument&)) {
  std::string s = slurp(p);
  if (s.empty()) { printf("  (missing %s)\n", p.c_str()); return false; }
  JsonDocument filter;
  if (filt) filt(filter);
  DeserializationError e = filt ? deserializeJson(doc, s, DeserializationOption::Filter(filter)) : deserializeJson(doc, s);
  if (e) { printf("  JSON error in %s: %s\n", p.c_str(), e.c_str()); return false; }
  return true;
}

int main(int argc, char** argv) {
  std::string dataDir = "data", outDir = "out", scenario = "normal";
  time_t t = time(nullptr);
  struct tm now = *localtime(&t);
  int battery = 78;
  for (int i = 1; i + 1 < argc; i += 2) {
    std::string k = argv[i], v = argv[i + 1];
    if (k == "--data") dataDir = v;
    else if (k == "--out") outDir = v;
    else if (k == "--scenario") scenario = v;
    else if (k == "--battery") battery = atoi(v.c_str());
    else if (k == "--date") { now.tm_year = atoi(v.c_str()) - 1900; now.tm_mon = atoi(v.c_str() + 5) - 1; now.tm_mday = atoi(v.c_str() + 8); }
    else if (k == "--time") { now.tm_hour = atoi(v.c_str()); now.tm_min = atoi(v.c_str() + 3); }
  }
  mktime(&now);  // fills tm_wday

  static DashboardData D;
  D.todayNum = dayNumber(now.tm_year + 1900, now.tm_mon + 1, now.tm_mday);
  D.nowMin = now.tm_hour * 60 + now.tm_min;
  snprintf(D.updatedAt, sizeof(D.updatedAt), "%02d:%02d", now.tm_hour, now.tm_min);
  resetHealthDays(D);

  if (scenario != "nogarmin") {
    JsonDocument d;
    if (load(dataDir + "/summary.json", d, filterUserSummary)) parseUserSummary(d, D.today);
    if (load(dataDir + "/sleep.json", d, filterDailySleep)) parseDailySleep(d, D.today);
    if (load(dataDir + "/bodybattery.json", d, filterBodyBattery)) parseBodyBattery(d, D.today);
    if (load(dataDir + "/days.json", d, filterDailySummaries)) parseDailySummaries(d, D);
    if (load(dataDir + "/sleeprange.json", d, filterSleepRange)) parseSleepRange(d, D);
    if (load(dataDir + "/activities.json", d, filterActivities)) parseActivities(d, D);
  } else {
    snprintf(D.garminStatus, sizeof(D.garminStatus), "API-Key ungültig (HTTP 401)");
  }
  if (scenario != "noweather") {
    JsonDocument d;
    if (load(dataDir + "/weather.json", d, nullptr)) parseWeather(d, D.wx);
  } else {
    snprintf(D.weatherStatus, sizeof(D.weatherStatus), "Open-Meteo HTTP -1");
  }
  if (scenario == "nosleep") {
    D.today.sleepH = -1;
    for (auto& h : D.days) h.sleepH = -1;
  }
  if (scenario == "warn") {  // three normal nights, today clearly elevated
    for (int i = HEALTH_DAYS - 4; i < HEALTH_DAYS - 1; i++) { D.days[i].rhr = 48; D.days[i].sleepH = 7.5f; D.days[i].resp = 14; }
    D.today.rhr = 56; D.today.resp = 16.5f;
    D.days[HEALTH_DAYS - 1].rhr = 56;
  }

  computeAll(D);
  const Metrics& m = D.m;
  printf("Werte: Body Battery %d (Aufwachen %d), Ruhepuls %d, Schlaf %.2f h, %d Aktivitäten\n", D.today.bbNow, D.today.bbWake,
         D.today.rhr, D.today.sleepH, D.actN);
  printf("Empfehlung: %s | %s\nFenster: %s | %s | %s | %s\nCheck: %s\n", m.recTitle, m.recReason, m.windowText, m.clothesText,
         m.rainText, m.darkText, m.healthText);

  int problems = 0;
  for (int s = 0; s < SCREEN_COUNT; s++) {
    epaper.overflow = 0;
    epaper.missing.clear();
    drawScreen(s, D, now, battery);
    char p[512];
    snprintf(p, sizeof(p), "%s/screen%d.pbm", outDir.c_str(), s + 1);
    epaper.writePbm(p);
    printf("Screen %d:", s + 1);
    bool ok = true;
    if (epaper.overflow) { printf(" ! %d Text(e) ragen über den Rand", epaper.overflow); ok = false; }
    if (!epaper.missing.empty()) {
      printf(" ! fehlende Zeichen:");
      for (auto c : epaper.missing) printf(" U+%04X", c);
      ok = false;
    }
    if (ok) printf(" ok"); else problems++;
    printf("\n");
  }
  return problems ? 2 : 0;
}
