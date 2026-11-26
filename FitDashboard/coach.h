/*
 * File: coach.h
 * Purpose: The LLM coach. A question in, one drawn screen out.
 *
 *   coachFacts()   figures per period, computed here from the data the device already has, so
 *                  that the LLM only picks them and never calculates or types a number
 *   coachPrompt()  rules + kit description (coach_prompt.h) + facts + question
 *   coachCheck()   names what the LLM referenced (title, unit, x labels come from code), fills
 *                  in the references, rejects typed numbers, draws with kit.h
 *   coachAsk()     asks the LLM, sends drawing problems back for a corrected answer, and
 *                  leaves either a clean answer or a plain fallback screen in the display buffer
 *
 * The network call is not in here: the caller passes a function (wolfSSL on the device, curl in
 * the PC emulator), so this file has no Arduino dependency.
 */
#pragma once
#include <math.h>
#include <functional>
#include <map>
#include <string>
#include <vector>
#include <ArduinoJson.h>
#include "model.h"
#include "kit.h"
#include "coach_prompt.h"

#define COACH_HOST   "generativelanguage.googleapis.com"
#define COACH_PATH   "/v1beta/interactions"
#ifndef COACH_MODEL
#define COACH_MODEL  "gemini-3.5-flash-lite"
#endif
#ifndef COACH_ROUNDS
#define COACH_ROUNDS 3      // LLM calls at most per question, including corrections
#endif

namespace coach {

typedef std::vector<std::string> Problems;
// POST a JSON body to https://COACH_HOST COACH_PATH. Returns the HTTP status (negative: network error).
typedef std::function<int(const std::string& body, std::string& answer)> PostFn;

static const char* WEEKDAY[7] = {"Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday"};
static const char* MONTH[12] = {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};

// ----------------------------------------------------------------- small helpers
inline double r1(double v) { return round(v * 10) / 10; }
inline double r2(double v) { return round(v * 100) / 100; }
inline std::string iso(long day) { char b[11]; isoFromDayNumber(day, b); return b; }
inline std::string clockOf(double hours) { return kit::fmt((float)hours, "clock"); }
inline long mondayOf(long day) { return day - weekdayMon0(day); }
inline long monthStart(long day) { std::string s = iso(day); return dayNumber(atoi(s.c_str()), atoi(s.c_str() + 5), 1); }
inline std::string dayDot(long day) { std::string s = iso(day); char b[12]; snprintf(b, sizeof(b), "%d.%d.", atoi(s.c_str() + 8), atoi(s.c_str() + 5)); return b; }

struct Stats {
  int n = 0; double sum = 0, lo = 0, hi = 0;
  void add(double v) { if (n == 0 || v < lo) lo = v; if (n == 0 || v > hi) hi = v; sum += v; n++; }
  // {avg, min, max} or null when there is no value
  void put(JsonVariant out, int digits) const {
    if (!n) { out.set(nullptr); return; }
    auto r = [&](double v) -> double { double f = pow(10, digits); return round(v * f) / f; };
    if (digits == 0) { out["avg"] = (long)lround(sum / n); out["min"] = (long)lround(lo); out["max"] = (long)lround(hi); }
    else { out["avg"] = r(sum / n); out["min"] = r(lo); out["max"] = r(hi); }
  }
};

// ----------------------------------------------------------------- facts
// Everything about one stretch of days, already summed and averaged.
inline void period(JsonObject out, const DashboardData& D, long first, long last, bool daily) {
  const Coach& X = D.c;
  out["from"] = iso(first);
  out["to"] = iso(last);
  int n = (int)(last - first + 1);
  long dataFrom = D.todayNum;   // first day with daily values (nights and days are loaded for COACH_DAYS only)
  for (auto& d : X.days) if (d.day >= 0 && d.day < dataFrom) dataFrom = d.day;
  for (auto& s : X.nights) if (s.day >= 0 && s.day < dataFrom) dataFrom = s.day;
  bool covered = first >= dataFrom;
  if (covered) {
    std::vector<const CoachNight*> night(n, nullptr);
    std::vector<const CoachDay*> day(n, nullptr);
    for (auto& s : X.nights) if (s.day >= first && s.day <= last && s.sleepH > 0 && !s.incomplete()) night[s.day - first] = &s;
    for (auto& d : X.days) if (d.day >= first && d.day <= last) day[d.day - first] = &d;
    Stats sleep, bed, wake, rhr, high, low;
    for (int i = 0; i < n; i++) {
      if (night[i]) { sleep.add(night[i]->sleepH); if (night[i]->bedMin >= 0) bed.add(12 + night[i]->bedMin / 60.0); if (night[i]->wakeMin >= 0) wake.add(12 + night[i]->wakeMin / 60.0); }
      if (day[i]) { if (day[i]->rhr > 0) rhr.add(day[i]->rhr); if (day[i]->bbMax >= 0) high.add(day[i]->bbMax); if (day[i]->bbMin >= 0) low.add(day[i]->bbMin); }
    }
    JsonObject sl = out["sleep"].to<JsonObject>();
    sl["nights"] = sleep.n;
    sleep.put(sl["hours"].to<JsonVariant>(), 2);
    if (bed.n) sl["bedtime_avg"] = clockOf(bed.sum / bed.n); else sl["bedtime_avg"] = nullptr;
    if (wake.n) sl["wake_avg"] = clockOf(wake.sum / wake.n); else sl["wake_avg"] = nullptr;
    rhr.put(out["resting_hr"].to<JsonVariant>(), 0);
    high.put(out["energy_high"].to<JsonVariant>(), 0);
    low.put(out["energy_low"].to<JsonVariant>(), 0);
    if (daily) {
      JsonObject dl = out["daily"].to<JsonObject>();
      JsonArray labels = dl["labels"].to<JsonArray>(), sh = dl["sleep_h"].to<JsonArray>(), bt = dl["bedtime"].to<JsonArray>(),
                wk = dl["wake"].to<JsonArray>(), bh = dl["bedtime_h"].to<JsonArray>(), wh = dl["wake_h"].to<JsonArray>(),
                rh = dl["resting_hr"].to<JsonArray>(), eh = dl["energy_high"].to<JsonArray>(), el = dl["energy_low"].to<JsonArray>(),
                tm = dl["training_min"].to<JsonArray>(), ld = dl["load"].to<JsonArray>();
      for (int i = 0; i < n; i++) {
        long d = first + i;
        if (n <= 7) labels.add(std::string(WEEKDAY[weekdayMon0(d)]).substr(0, 3)); else labels.add(std::to_string(atoi(iso(d).c_str() + 8)));
        const CoachNight* s = night[i];
        if (s) sh.add(r2(s->sleepH)); else sh.add(nullptr);
        if (s && s->bedMin >= 0) { bt.add(clockOf(12 + s->bedMin / 60.0)); bh.add(r2(12 + s->bedMin / 60.0)); } else { bt.add(nullptr); bh.add(nullptr); }
        if (s && s->wakeMin >= 0) { wk.add(clockOf(12 + s->wakeMin / 60.0)); wh.add(r2(12 + s->wakeMin / 60.0)); } else { wk.add(nullptr); wh.add(nullptr); }
        if (day[i] && day[i]->rhr > 0) rh.add(day[i]->rhr); else rh.add(nullptr);
        if (day[i] && day[i]->bbMax >= 0) eh.add(day[i]->bbMax); else eh.add(nullptr);
        if (day[i] && day[i]->bbMin >= 0) el.add(day[i]->bbMin); else el.add(nullptr);
        double minutes = 0, load = 0;
        for (int a = 0; a < D.actN; a++) if (D.acts[a].day == d) { minutes += D.acts[a].minutes; load += D.acts[a].load; }
        tm.add((long)lround(minutes));
        ld.add((long)lround(load));
      }
    }
  }
  JsonObject tr = out["training"].to<JsonObject>();
  double minutes = 0, km = 0, load = 0, zone[5] = {0, 0, 0, 0, 0};
  int sessions = 0;
  struct Sport { int n = 0; double minutes = 0, km = 0; const Activity* far = nullptr; };
  std::map<std::string, Sport> by;
  for (int a = 0; a < D.actN; a++) {
    const Activity& x = D.acts[a];
    if (x.day < first || x.day > last) continue;
    sessions++; minutes += x.minutes; km += x.km; load += x.load;
    for (int z = 0; z < 5; z++) zone[z] += x.zoneMin[z];
    Sport& s = by[x.sport];
    s.n++; s.minutes += x.minutes; s.km += x.km;
    if (!s.far || x.km > s.far->km || (x.km == s.far->km && x.minutes > s.far->minutes)) s.far = &x;
  }
  tr["sessions"] = sessions;
  tr["hours"] = r1(minutes / 60);
  tr["minutes"] = (long)lround(minutes);
  tr["duration"] = kit::fmt((float)(minutes / 60), "h:mm");
  tr["km"] = r1(km);
  tr["load"] = (long)lround(load);
  JsonObject bs = tr["by_sport"].to<JsonObject>();
  for (auto& kv : by) {
    JsonObject o = bs[kv.first].to<JsonObject>();
    o["sessions"] = kv.second.n;
    o["hours"] = r1(kv.second.minutes / 60);
    o["km"] = r1(kv.second.km);
    JsonObject f = o["longest"].to<JsonObject>();
    f["km"] = r1(kv.second.far->km);
    f["minutes"] = (long)lround(kv.second.far->minutes);
    f["date"] = iso(kv.second.far->day);
  }
  double zsum = zone[0] + zone[1] + zone[2] + zone[3] + zone[4];
  JsonArray zm = tr["zone_minutes"].to<JsonArray>();
  for (int z = 0; z < 5; z++) zm.add((long)lround(zone[z]));
  if (zsum > 0) { JsonArray zp = tr["zone_share_pct"].to<JsonArray>(); for (int z = 0; z < 5; z++) zp.add((long)lround(100 * zone[z] / zsum)); }
  else tr["zone_share_pct"] = nullptr;
}

inline double loadBetween(const DashboardData& D, long first, long last) {
  double sum = 0;
  for (int a = 0; a < D.actN; a++) if (D.acts[a].day >= first && D.acts[a].day <= last) sum += D.acts[a].load;
  return sum;
}

// Figures per period for the prompt and for filling in the references.
inline void coachFacts(const DashboardData& D, JsonDocument& doc) {
  doc.clear();
  JsonObject out = doc.to<JsonObject>();
  long t = D.todayNum, mon = mondayOf(t), m1 = monthStart(t);
  auto P = [&](const char* name, long a, long b, bool daily) { period(out[name].to<JsonObject>(), D, a, b, daily); };
  P("today", t, t, false);
  P("yesterday", t - 1, t - 1, false);
  P("this_week", mon, t, true);
  P("last_week", mon - 7, mon - 1, true);
  P("last_7_days", t - 6, t, true);
  P("last_28_days", t - 27, t, true);
  P("this_month", m1, t, true);
  P("last_month", monthStart(m1 - 1), m1 - 1, false);

  JsonArray acts = out["activities_last_28_days"].to<JsonArray>();
  for (int a = 0; a < D.actN; a++) {
    const Activity& x = D.acts[a];
    if (x.day < t - 27 || x.day > t) continue;
    JsonObject o = acts.add<JsonObject>();
    char start[16];
    snprintf(start, sizeof(start), "%02d:%02d", (x.startMin / 60) % 24, x.startMin % 60);
    o["date"] = iso(x.day); o["start"] = std::string(start); o["sport"] = std::string(x.sport);
    o["km"] = r1(x.km); o["minutes"] = (long)lround(x.minutes);
    if (x.avgHr > 0) o["avg_hr"] = x.avgHr; else o["avg_hr"] = nullptr;
    o["load"] = (long)lround(x.load);
    JsonArray z = o["zone_minutes"].to<JsonArray>();
    for (int k = 0; k < 5; k++) z.add((long)lround(x.zoneMin[k]));
  }

  JsonObject w = out["last_12_weeks"].to<JsonObject>();
  JsonArray wl = w["labels"].to<JsonArray>(), wh = w["hours"].to<JsonArray>(), wld = w["load"].to<JsonArray>(), ws = w["sessions"].to<JsonArray>(),
            wk = w["km"].to<JsonArray>();
  static const char* SP[4] = {"run", "bike", "swim", "strength"};
  JsonArray sph[4], runKm = w["run_km"].to<JsonArray>();
  for (int k = 0; k < 4; k++) sph[k] = w[std::string(SP[k]) + "_hours"].to<JsonArray>();
  for (int i = 11; i >= 0; i--) {
    long a = mon - 7 * i, b = a + 6 > t ? t : a + 6;
    double minutes = 0, load = 0, km = 0, sm[4] = {0, 0, 0, 0}, rkm = 0;
    int n = 0;
    for (int k = 0; k < D.actN; k++) {
      const Activity& x = D.acts[k];
      if (x.day < a || x.day > b) continue;
      n++; minutes += x.minutes; load += x.load; km += x.km;
      for (int s = 0; s < 4; s++) if (!strcmp(x.sport, SP[s])) sm[s] += x.minutes;
      if (x.run) rkm += x.km;
    }
    wl.add(dayDot(a)); wh.add(r1(minutes / 60)); wld.add((long)lround(load)); ws.add(n); wk.add(r1(km)); runKm.add(r1(rkm));
    for (int s = 0; s < 4; s++) sph[s].add(r1(sm[s] / 60));
  }

  // Judgements are made here, in words, so that the LLM does not weigh numbers itself.
  JsonObject lb = out["load_balance"].to<JsonObject>();
  long acute = lround(loadBetween(D, t - 6, t)), chronic = lround(loadBetween(D, t - 34, t - 7) / 4);
  lb["last_7_days"] = acute;
  lb["usual_per_week_before"] = chronic;
  if (chronic > 0) {
    long ratio = lround(100.0 * acute / chronic);
    lb["ratio_pct"] = ratio;
    lb["verdict"] = ratio > 150 ? "far more than usual" : ratio > 130 ? "clearly more than usual" : ratio >= 80 ? "about as usual" : ratio >= 50 ? "clearly less than usual" : "far less than usual";
  } else { lb["ratio_pct"] = nullptr; lb["verdict"] = "no usual load to compare with"; }

  JsonObject rc = out["recovery"].to<JsonObject>();
  Stats r3, r28, s3, s28;
  for (auto& d : D.c.days) if (d.rhr > 0 && d.day > t - 28 && d.day <= t) { r28.add(d.rhr); if (d.day > t - 3) r3.add(d.rhr); }
  for (auto& s : D.c.nights) if (s.sleepH > 0 && !s.incomplete() && s.day > t - 28 && s.day <= t) { s28.add(s.sleepH); if (s.day > t - 3) s3.add(s.sleepH); }
  if (r3.n) rc["resting_hr_last_3_days"] = (long)lround(r3.sum / r3.n); else rc["resting_hr_last_3_days"] = nullptr;
  if (r28.n) rc["resting_hr_usual"] = (long)lround(r28.sum / r28.n); else rc["resting_hr_usual"] = nullptr;
  if (r3.n && r28.n) {
    double diff = r3.sum / r3.n - r28.sum / r28.n;
    rc["resting_hr_verdict"] = diff >= 5 ? "clearly raised" : diff >= 3 ? "slightly raised" : diff <= -3 ? "lower than usual" : "as usual";
  }
  if (s3.n) rc["sleep_h_last_3_nights"] = r2(s3.sum / s3.n); else rc["sleep_h_last_3_nights"] = nullptr;
  if (s28.n) rc["sleep_h_usual"] = r2(s28.sum / s28.n); else rc["sleep_h_usual"] = nullptr;
  if (D.today.bbNow >= 0) rc["energy_now"] = D.today.bbNow; else rc["energy_now"] = nullptr;
  if (D.today.bbWake >= 0) rc["energy_at_wake_up"] = D.today.bbWake; else rc["energy_at_wake_up"] = nullptr;
  if (D.m.healthStatus >= 0) rc["health_check"] = D.m.healthStatus == 0 ? "nothing unusual" : D.m.healthStatus == 1 ? "watch it" : "signs of an infection";

  JsonObject ft = out["fitness"].to<JsonObject>();
  ft["fitness_42_days"] = (long)lround(D.m.ctl);
  ft["fitness_a_week_ago"] = (long)lround(D.m.ctlWeekAgo);
  ft["fatigue_7_days"] = (long)lround(D.m.atl);
  double dctl = D.m.ctl - D.m.ctlWeekAgo;
  ft["trend"] = dctl > 1.5 ? "rising" : dctl < -1.5 ? "falling" : "steady";
  ft["note"] = "fitness is the average daily training load of 42 days, fatigue that of 7 days";

  JsonObject hz = out["hr_zones"].to<JsonObject>();
  JsonArray fl = hz["floors"].to<JsonArray>(), nm = hz["names"].to<JsonArray>();
  for (int z = 0; z < 5; z++) { fl.add(D.c.zoneFloor[z]); nm.add(std::string("Z") + std::to_string(z + 1)); }
  hz["max_hr"] = D.hrMax;
  if (D.wx.ok) {
    JsonObject wx = out["weather"].to<JsonObject>();
    wx["temp_c_now"] = (long)lround(D.wx.tempNow);
    wx["feels_like_c_now"] = (long)lround(D.wx.feelsNow);
    wx["rain_mm_next_2_hours"] = r1(D.wx.rain2h);
  }
}

// The frame around every answer is filled by code.
inline void coachFrame(const DashboardData& D, const struct tm& now, JsonObject fr, const char* hint) {
  char b[64];
  snprintf(b, sizeof(b), "%d:%02d", now.tm_hour, now.tm_min);
  fr["time"] = std::string(b);
  snprintf(b, sizeof(b), "%s, %d %s", WEEKDAY[weekdayMon0(D.todayNum)], now.tm_mday, MONTH[now.tm_mon]);
  fr["date"] = std::string(b);
  if (D.wx.ok) { snprintf(b, sizeof(b), "%ld°, feels like %ld°", lround(D.wx.tempNow), lround(D.wx.feelsNow)); fr["weather"] = std::string(b); }
  else fr["weather"] = "";
  int today = 0;
  double minutes = 0;
  for (int a = 0; a < D.actN; a++) {
    if (D.acts[a].day == D.todayNum) today++;
    if (D.acts[a].day >= mondayOf(D.todayNum) && D.acts[a].day <= D.todayNum) minutes += D.acts[a].minutes;
  }
  snprintf(b, sizeof(b), "%d session%s today", today, today == 1 ? "" : "s");
  fr["today"] = std::string(b);
  fr["next"] = "";
  snprintf(b, sizeof(b), "Week %.1f h", minutes / 60);
  fr["week"] = std::string(b);
  if (D.today.bbNow >= 0) { snprintf(b, sizeof(b), "Energy %d", D.today.bbNow); fr["energy"] = std::string(b); }
  else fr["energy"] = "Energy -";
  fr["hint"] = hint;
}

// ----------------------------------------------------------------- references
// Follows "a.b.2.c" through objects and lists. Sets `error` if the path does not exist.
inline JsonVariantConst lookup(JsonVariantConst data, const std::string& path, std::string& error) {
  JsonVariantConst node = data;
  size_t start = 0;
  while (start <= path.size()) {
    size_t dot = path.find('.', start);
    std::string part = path.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
    bool digits = !part.empty() && part.find_first_not_of("0123456789") == std::string::npos;
    bool member = false;   // present, even when its value is null
    if (node.is<JsonObjectConst>()) for (JsonPairConst kv : node.as<JsonObjectConst>()) if (part == kv.key().c_str()) { member = true; break; }
    if (member) node = node[part.c_str()];
    else if (node.is<JsonArrayConst>() && digits && (size_t)atoi(part.c_str()) < node.size()) node = node[(size_t)atoi(part.c_str())];
    else {
      error = "unknown reference \"" + path + "\": \"" + part + "\" does not exist there (available: ";
      if (node.is<JsonObjectConst>()) { bool first = true; for (JsonPairConst kv : node.as<JsonObjectConst>()) { error += (first ? "" : ", ") + std::string(kv.key().c_str()); first = false; } }
      else if (node.is<JsonArrayConst>()) error += "a list of " + std::to_string(node.size()) + ", positions 0 to " + std::to_string((int)node.size() - 1);
      else error += "a single value";
      error += ")";
      return JsonVariantConst();
    }
    if (dot == std::string::npos) break;
    start = dot + 1;
  }
  return node;
}

inline bool pathChar(char ch) { return (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_' || ch == '.'; }
// A "{path}" or "{path:format}" at position i of s. Returns its length, 0 if there is none.
inline size_t refAt(const std::string& s, size_t i, std::string& path, std::string& format) {
  if (s[i] != '{') return 0;
  size_t j = i + 1;
  while (j < s.size() && pathChar(s[j])) j++;
  if (j == i + 1 || j >= s.size()) return 0;
  path = s.substr(i + 1, j - i - 1);
  format.clear();
  if (s[j] == ':') {
    size_t k = s.find('}', j);
    if (k == std::string::npos || s.find('{', j) < k) return 0;
    format = s.substr(j + 1, k - j - 1);
    j = k;
  }
  return s[j] == '}' ? j - i + 1 : 0;
}
// The path if v is exactly one reference ("@a.b", "{a.b}" or "{a.b:.1f}"), else "".
inline std::string oneRef(JsonVariantConst v) {
  if (!v.is<const char*>()) return "";
  std::string s = v.as<const char*>(), path, format;
  if (!s.empty() && s[0] == '@') return s.substr(1);
  return !s.empty() && refAt(s, 0, path, format) == s.size() ? path : "";
}
// Number-like pieces of a text ("8.06", "22:37", "2026"); not those glued to a letter such as Z2.
inline std::vector<std::string> tokens(const std::string& s, bool all = false) {
  std::vector<std::string> out;
  for (size_t i = 0; i < s.size();) {
    if (!isdigit((unsigned char)s[i])) { i++; continue; }
    size_t j = i;
    while (j < s.size() && (isdigit((unsigned char)s[j]) || ((s[j] == '.' || s[j] == ':') && j + 1 < s.size() && isdigit((unsigned char)s[j + 1])))) j++;
    char before = i > 0 ? s[i - 1] : ' ';
    if (all || !(isalpha((unsigned char)before) || before == '.' || before == ':')) out.push_back(s.substr(i, j - i));
    i = j;
  }
  return out;
}

// ----------------------------------------------------------------- names from code
struct Meaning { bool known = false; std::string label, unit, period, x; bool clock = false; };

// What a reference means, in words. The name on the screen is written by code, so that the
// LLM cannot put a wrong name on a right number.
inline Meaning describe(const std::string& path, JsonVariantConst data) {
  static const char* PERIOD[][2] = {{"today", "today"}, {"yesterday", "yesterday"}, {"this_week", "this week"}, {"last_week", "last week"},
                                    {"last_7_days", "last 7 days"}, {"last_28_days", "last 28 days"}, {"this_month", "this month"}, {"last_month", "last month"}};
  static const char* LABEL[][3] = {
      {"sleep.hours.avg", "Sleep per night", "h"}, {"sleep.hours.min", "Shortest night", "h"}, {"sleep.hours.max", "Longest night", "h"},
      {"sleep.nights", "Nights recorded", ""}, {"sleep.bedtime_avg", "Usual bedtime", ""}, {"sleep.wake_avg", "Usual wake-up", ""},
      {"resting_hr.avg", "Resting HR", "bpm"}, {"resting_hr.min", "Lowest resting HR", "bpm"}, {"resting_hr.max", "Highest resting HR", "bpm"},
      {"energy_high.avg", "Energy peak per day", ""}, {"energy_high.min", "Lowest energy peak", ""}, {"energy_high.max", "Highest energy peak", ""},
      {"energy_low.avg", "Energy low per day", ""}, {"energy_low.min", "Lowest energy", ""}, {"energy_low.max", "Highest energy low", ""},
      {"training.sessions", "Sessions", ""}, {"training.hours", "Training time", "h"}, {"training.minutes", "Training time", "min"},
      {"training.duration", "Training time", "h"}, {"training.km", "Distance", "km"}, {"training.load", "Training load", ""},
      {"training.zone_minutes", "Minutes per zone", "min"}, {"training.zone_share_pct", "Share per zone", "%"},
      {"daily.sleep_h", "Sleep per night", "h"}, {"daily.bedtime_h", "Bedtime", ""}, {"daily.wake_h", "Wake-up", ""},
      {"daily.resting_hr", "Resting HR", "bpm"}, {"daily.energy_high", "Energy peak per day", ""}, {"daily.energy_low", "Energy low per day", ""},
      {"daily.training_min", "Training minutes per day", "min"}, {"daily.load", "Training load per day", ""},
      {"load_balance.last_7_days", "Load, last 7 days", ""}, {"load_balance.usual_per_week_before", "Usual load per week", ""},
      {"load_balance.ratio_pct", "Load vs usual", "%"}, {"load_balance.verdict", "Training load", ""},
      {"recovery.resting_hr_last_3_days", "Resting HR, last 3 days", "bpm"}, {"recovery.resting_hr_usual", "Usual resting HR", "bpm"},
      {"recovery.resting_hr_verdict", "Resting HR", ""}, {"recovery.sleep_h_last_3_nights", "Sleep, last 3 nights", "h"},
      {"recovery.sleep_h_usual", "Usual sleep", "h"}, {"recovery.energy_now", "Energy now", ""}, {"recovery.energy_at_wake_up", "Energy at wake-up", ""},
      {"recovery.health_check", "Health check", ""},
      {"fitness.fitness_42_days", "Fitness", ""}, {"fitness.fitness_a_week_ago", "Fitness a week ago", ""}, {"fitness.fatigue_7_days", "Fatigue", ""},
      {"fitness.trend", "Fitness trend", ""}, {"hr_zones.max_hr", "Max heart rate", "bpm"},
      {"weather.temp_c_now", "Temperature now", "°C"}, {"weather.feels_like_c_now", "Feels like", "°C"}, {"weather.rain_mm_next_2_hours", "Rain, next 2 hours", "mm"},
      {"last_12_weeks.hours", "Training hours", "h"}, {"last_12_weeks.load", "Training load", ""}, {"last_12_weeks.sessions", "Sessions", ""},
      {"last_12_weeks.km", "Distance", "km"}, {"last_12_weeks.run_km", "Run distance", "km"}, {"last_12_weeks.run_hours", "Run hours", "h"},
      {"last_12_weeks.bike_hours", "Bike hours", "h"}, {"last_12_weeks.swim_hours", "Swim hours", "h"}, {"last_12_weeks.strength_hours", "Strength hours", "h"}};
  Meaning m;
  std::vector<std::string> parts;
  for (size_t a = 0;;) { size_t dot = path.find('.', a); parts.push_back(path.substr(a, dot == std::string::npos ? std::string::npos : dot - a)); if (dot == std::string::npos) break; a = dot + 1; }
  std::string head = parts[0];
  for (auto& p : PERIOD) if (head == p[0]) { m.period = p[1]; parts.erase(parts.begin()); break; }
  if (parts.empty()) return m;
  int index = -1;
  if (parts.back().find_first_not_of("0123456789") == std::string::npos && !parts.back().empty()) { index = atoi(parts.back().c_str()); parts.pop_back(); }
  std::string key;
  for (auto& p : parts) key += (key.empty() ? "" : ".") + p;
  m.clock = key == "daily.bedtime_h" || key == "daily.wake_h";
  auto cap = [](std::string s) { if (!s.empty()) s[0] = toupper(s[0]); return s; };
  if (parts.size() >= 4 && parts[0] == "training" && parts[1] == "by_sport") {
    std::string kind = parts[2], what = parts[3] + (parts.size() > 4 ? "." + parts[4] : "");
    std::string noun = kind == "bike" ? "ride" : kind == "strength" ? "strength session" : kind == "other" ? "other session" : kind;
    if (what == "sessions") { m.label = cap(kind) + " sessions"; }
    else if (what == "hours") { m.label = cap(kind) + " time"; m.unit = "h"; }
    else if (what == "km") { m.label = cap(kind) + " distance"; m.unit = "km"; }
    else if (what == "longest.km") { m.label = "Longest " + noun; m.unit = "km"; }
    else if (what == "longest.minutes") { m.label = "Longest " + noun; m.unit = "min"; }
    else if (what == "longest.date") { m.label = "Date of longest " + noun; }
    else return m;
  } else if ((key == "training.zone_minutes" || key == "training.zone_share_pct") && index >= 0) {
    m.label = (key == "training.zone_minutes" ? "Time in Z" : "Share in Z") + std::to_string(index + 1);
    m.unit = key == "training.zone_minutes" ? "min" : "%";
  } else if (key == "hr_zones.floors" && index >= 0) {
    m.label = "Z" + std::to_string(index + 1) + " starts at"; m.unit = "bpm";
  } else if (parts.size() == 2 && parts[0] == "activities_last_28_days" && index < 0) {
    return m;
  } else if (parts.size() == 3 && parts[0] == "activities_last_28_days") {
    JsonVariantConst x = data["activities_last_28_days"][atoi(parts[1].c_str())];
    if (x.isNull()) return m;
    std::string date = x["date"] | "", what = parts[2];
    std::string who = cap(x["sport"] | "") + " " + std::to_string(atoi(date.c_str() + 8)) + "." + std::to_string(atoi(date.c_str() + 5)) + "., ";
    if (what == "km") { m.label = who + "distance"; m.unit = "km"; }
    else if (what == "minutes") { m.label = who + "duration"; m.unit = "min"; }
    else if (what == "avg_hr") { m.label = who + "average HR"; m.unit = "bpm"; }
    else if (what == "load") { m.label = who + "load"; }
    else return m;
  } else {
    bool found = false;
    for (auto& l : LABEL) if (key == l[0]) { m.label = l[1]; m.unit = l[2]; found = true; break; }
    if (!found) return m;
    if (key.compare(0, 14, "last_12_weeks.") == 0) {
      if (index < 0) { m.label += " per week"; m.x = "@last_12_weeks.labels"; }
      else m.label += ", week of " + std::string(data["last_12_weeks"]["labels"][index] | "?");
    } else if (key.compare(0, 6, "daily.") == 0 && index < 0) m.x = "@" + head + ".daily.labels";
  }
  m.known = true;
  return m;
}

// Writes names, units and x labels that follow from the references. Works on the answer before
// the values are filled in. `trusted` collects the texts written here (they are not checked again).
inline void relabel(JsonVariant node, JsonVariantConst data, std::vector<std::string>& trusted) {
  if (node.is<JsonArray>()) { for (JsonVariant v : node.as<JsonArray>()) relabel(v, data, trusted); return; }
  if (!node.is<JsonObject>()) return;
  for (JsonPair kv : node.as<JsonObject>()) relabel(kv.value(), data, trusted);
  std::string kind = node["element"] | "";
  auto meaning = [&](JsonVariantConst v) { std::string p = oneRef(v); return p.empty() ? Meaning() : describe(p, data); };
  if (kind == "number") {
    Meaning d = meaning(node["value"]);
    if (d.known) {
      std::string t = d.label;
      if (!d.period.empty() && t.size() + d.period.size() <= 28) t += ", " + d.period;
      else if (!d.period.empty() && !kit::truthy(node["sub"])) { std::string p = d.period; p[0] = toupper(p[0]); node["sub"] = p; trusted.push_back(p); }
      node["title"] = t;
      node["unit"] = d.unit;
      trusted.push_back(t);
    }
  } else if (kind == "seesaw") {
    for (const char* side : {"left", "right"}) {
      Meaning d = meaning(node[side]["value"]);
      if (!d.known) continue;
      std::string name = d.label == "Load, last 7 days" ? "Last 7 days" : d.label == "Usual load per week" ? "Usual week" : d.label;   // little room above a seesaw box
      node[side]["name"] = name;
      trusted.push_back(name);
    }
  } else if (kind == "line" || kind == "bars") {
    JsonVariant holder = node;
    if (node["series"].is<JsonArray>() && node["series"].size() == 1) holder = node["series"][0];
    std::string ref = oneRef(holder["values"]);
    Meaning d = ref.empty() ? Meaning() : describe(ref, data);
    if (d.known && !d.x.empty()) {
      std::string t = d.label + (d.period.empty() ? "" : ", " + d.period);
      node["title"] = t;
      trusted.push_back(t);
      node["x"] = d.x;
      if (d.clock) {   // clock times: bars from zero say nothing, and the axis shows times
        node["element"] = "line";
        node.remove("values"); node.remove("show_values"); node.remove("value_format");
        node["y"].to<JsonObject>()["format"] = "clock";
        JsonObject s = node["series"].to<JsonArray>().add<JsonObject>();
        s["values"] = "@" + ref;
        s["points"] = true;
      }
    }
  } else if (kind == "hbars" || kind == "shares") {
    // nothing to rename, but the unit of a single referenced list is known
  }
}

// ----------------------------------------------------------------- filling in and checking
inline bool strictKey(const std::string& k) { return k == "value" || k == "values" || k == "points" || k == "pointer" || k == "mean"; }
inline bool freeKey(const std::string& k) {   // layout, never data
  static const char* FREE[] = {"weight", "gap", "size", "width", "height", "dx", "dy", "radius", "marks", "min", "max", "at", "series_index",
                               "start_weekday", "annotate", "x_axis", "axis", "y", "frame", "value_format", "format"};
  for (auto f : FREE) if (k == f) return true;
  return false;
}

struct Resolver {
  JsonVariantConst data;
  Problems* problems;
  const std::vector<std::string>* trusted;
  std::vector<double> nums;          // every number the LLM was given
  std::vector<std::string> words;    // and every number-like piece of text

  void learn(JsonVariantConst node) {
    if (node.is<JsonObjectConst>()) { for (JsonPairConst kv : node.as<JsonObjectConst>()) learn(kv.value()); }
    else if (node.is<JsonArrayConst>()) { for (JsonVariantConst v : node.as<JsonArrayConst>()) learn(v); }
    else if (node.is<const char*>()) { for (auto& w : tokens(node.as<const char*>(), true)) learnWord(w); }
    else if (node.is<double>() && !node.is<bool>()) nums.push_back(node.as<double>());
  }
  void learnWord(const std::string& w) {
    words.push_back(w);
    if (w.find(':') == std::string::npos && w.find('.') == w.rfind('.')) nums.push_back(atof(w.c_str()));
  }
  // A typed number passes if it was given, as it stands or rounded.
  bool given(const std::string& tok) const {
    for (auto& w : words) if (w == tok) return true;
    if (tok.find(':') != std::string::npos || tok.find('.') != tok.rfind('.')) return false;
    size_t dot = tok.find('.');
    double v = atof(tok.c_str()), tol = 0.5 * pow(10, -(double)(dot == std::string::npos ? 0 : tok.size() - dot - 1)) + 1e-9;
    for (double x : nums) if (fabs(x - v) <= tol) return true;
    return false;
  }
  void problem(const std::string& p) { for (auto& q : *problems) if (q == p) return; if (problems->size() < 12) problems->push_back(p); }

  void text(const std::string& s, const std::string& key, bool strict, JsonVariant out) {
    for (auto& t : *trusted) if (t == s) { out.set(s); return; }
    if (!s.empty() && s[0] == '@') {
      std::string err;
      JsonVariantConst v = lookup(data, s.substr(1), err);
      if (!err.empty()) { problem(err); out.set(s); } else out.set(v);
      return;
    }
    std::string filled, plain, path, format;
    for (size_t i = 0; i < s.size();) {
      size_t len = refAt(s, i, path, format);
      if (!len) { filled += s[i]; plain += s[i]; i++; continue; }
      i += len;
      plain += ' ';
      std::string err;
      JsonVariantConst v = lookup(data, path, err);
      if (!err.empty()) { problem(err); filled += "{" + path + "}"; }
      else if (v.is<JsonArrayConst>() || v.is<JsonObjectConst>()) { problem("\"{" + path + "}\" is a whole list and cannot stand inside a text. Use one item or a single figure"); filled += "{" + path + "}"; }
      else if (v.isNull()) filled += "no data";
      else if (!format.empty() && kit::isNum(v)) filled += kit::fmt(v.as<float>(), "{:" + format + "}");
      else filled += kit::str(v);
    }
    for (auto& tok : tokens(plain)) {
      if (strict) problem("\"" + key + "\": the number " + tok + " in \"" + s + "\" is typed in. This field takes only references (\"@path\" or \"{path}\")");
      else if (!given(tok)) problem("the number " + tok + " in \"" + s + "\" is not in the athlete data. Use a reference or leave it out");
    }
    out.set(filled);
  }
  void walk(JsonVariantConst in, JsonVariant out, const std::string& key, bool strict, bool free) {
    if (in.is<JsonObjectConst>()) {
      JsonObject o = out.to<JsonObject>();
      for (JsonPairConst kv : in.as<JsonObjectConst>()) {
        std::string k = kv.key().c_str();
        walk(kv.value(), o[k].to<JsonVariant>(), k, strict || strictKey(k), free || freeKey(k));
      }
    } else if (in.is<JsonArrayConst>()) {
      JsonArray a = out.to<JsonArray>();
      for (JsonVariantConst v : in.as<JsonArrayConst>()) walk(v, a.add<JsonVariant>(), key, strict, free);
    } else if (in.is<const char*>()) {
      std::string s = in.as<const char*>(), p, f;
      bool ref = !s.empty() && s[0] == '@';
      for (size_t i = 0; i < s.size() && !ref; i++) ref = refAt(s, i, p, f) > 0;
      if (free && !ref) out.set(s); else text(s, key, strict && !free, out);
    } else {
      if (in.is<double>() && !in.is<bool>() && !free) {
        std::string v = kit::str(in);
        if (strict) problem("\"" + key + "\": " + v + " is typed in. This field takes only references, for example \"@this_week.sleep.hours.avg\"");
        else if (!given(v)) problem("\"" + key + "\": " + v + " is not in the athlete data");
      }
      out.set(in);
    }
  }
  // Charts need numbers: a text such as "22:14" in a list of values would draw nothing.
  void numeric(JsonVariantConst node) {
    if (node.is<JsonArrayConst>()) { for (JsonVariantConst v : node.as<JsonArrayConst>()) numeric(v); return; }
    if (!node.is<JsonObjectConst>()) return;
    std::string kind = node["element"] | "";
    if (kind == "line" || kind == "bars" || kind == "hbars" || kind == "scatter") {
      std::string bad;
      std::function<void(JsonVariantConst)> leaves = [&](JsonVariantConst v) {
        if (v.is<JsonObjectConst>()) { leaves(v["values"]); leaves(v["value"]); }
        else if (v.is<JsonArrayConst>()) { for (JsonVariantConst w : v.as<JsonArrayConst>()) leaves(w); }
        else if (v.is<const char*>() && isnan(kit::num(v)) && bad.empty()) bad = v.as<const char*>();
      };
      leaves(node["values"]); leaves(node["series"]); leaves(node["stacks"]);
      if (!bad.empty()) problem("\"" + kind + "\" needs numbers, but got text such as \"" + bad + "\". For clock times use a series ending in \"_h\"");
    }
    for (JsonPairConst kv : node.as<JsonObjectConst>()) numeric(kv.value());
  }
};

// Names, fills in and draws one answer. `spec` is the LLM's answer and is changed (names);
// `drawn` receives the spec with all values filled in. Returns the problems; empty = clean.
inline Problems coachCheck(JsonDocument& spec, JsonVariantConst facts, JsonObjectConst frame, const std::string& question, JsonDocument& drawn) {
  Problems problems;
  if (!spec.is<JsonObject>() || spec["content"].isNull()) { problems.push_back("the spec needs a \"content\" field"); return problems; }
  std::vector<std::string> trusted;
  relabel(spec["content"], facts, trusted);
  Resolver r{facts, &problems, &trusted};
  r.learn(facts);
  for (auto& w : tokens(question, true)) r.learnWord(w);
  drawn.clear();
  r.walk(spec.as<JsonVariantConst>(), drawn.to<JsonVariant>(), "", false, false);
  r.numeric(drawn["content"]);
  if (!kit::truthy(drawn["fullscreen"])) drawn["frame"].set(frame);
  for (auto& w : kit::render(drawn.as<JsonVariantConst>())) r.problem(w);
  return problems;
}

// What the device shows when no clean answer came out. Built by code, no LLM text in it.
inline std::string coachSorry(const std::string& question, JsonObjectConst frame, const char* reason) {
  JsonDocument d;
  d["frame"].set(frame);
  d["header"]["label"] = "NO ANSWER";
  d["header"]["title"] = "I could not answer that cleanly";
  d["content"]["element"] = "text";
  std::string shown = question.substr(0, 140);
  while (!shown.empty() && shown.size() < question.size() && ((unsigned char)question[shown.size()] & 0xC0) == 0x80) shown.pop_back();   // not inside a UTF-8 character
  d["content"]["content"] = "Your question: " + shown;
  d["sentence"] = reason;
  kit::render(d.as<JsonVariantConst>());
  std::string screen;
  serializeJson(d, screen);
  return screen;
}

// Draws a screen kept from an earlier answer (CoachResult::screen) again.
inline void coachRedraw(const std::string& screen) {
  JsonDocument d;
  if (!deserializeJson(d, screen)) kit::render(d.as<JsonVariantConst>());
}

// ----------------------------------------------------------------- talking to the LLM
inline std::string coachPrompt(JsonVariantConst facts, const DashboardData& D, const struct tm& now, const std::string& question) {
  std::string data;
  serializeJson(facts, data);
  char when[80];
  snprintf(when, sizeof(when), "Now: %s, %s, %02d:%02d.", WEEKDAY[weekdayMon0(D.todayNum)], iso(D.todayNum).c_str(), now.tm_hour, now.tm_min);
  return std::string(COACH_RULES) + "\n\n" + COACH_KIT + "\n\n" + when + "\nAthlete data:\n" + data +
         "\n\nBefore you answer, check: no digit typed by you in any value or text, only references; the period of every reference "
         "matches the question; header, chart and sentence say the same. If the question asks for a longer time than the data covers, "
         "show the longest period there is and say in the sentence that the data only goes back that far, in words (\"twelve weeks\").\nQuestion: " + question;
}

// The answer text out of the API's reply: steps[] of type model_output, content[] of type text.
inline std::string coachAnswerText(const std::string& reply) {
  JsonDocument filter, doc;
  filter["steps"][0]["type"] = true;
  filter["steps"][0]["content"][0]["type"] = true;
  filter["steps"][0]["content"][0]["text"] = true;
  filter["output_text"] = true;
  if (deserializeJson(doc, reply, DeserializationOption::Filter(filter))) return "";
  if (doc["output_text"].is<const char*>()) return doc["output_text"].as<const char*>();
  std::string text;
  for (JsonVariantConst step : doc["steps"].as<JsonArrayConst>())
    if (std::string(step["type"] | "") == "model_output")
      for (JsonVariantConst c : step["content"].as<JsonArrayConst>())
        if (std::string(c["type"] | "") == "text") text += c["text"] | "";
  return text;
}

// A spoken question: the recording as base64 and its type ("audio/wav"). The LLM hears it once, in the first
// round, and writes what it understood into the spec's field "question"; later rounds work with that text.
struct CoachAudio {
  std::string base64;
  const char* mime = "audio/wav";
};
#define COACH_SPOKEN "(spoken, in the audio recording. Write what is asked into the field \"question\" of the spec: in English, " \
                     "translated if another language is spoken, as one or two plain sentences without filler words and repetitions. " \
                     "If you hear no question, return {\"question\": \"\"} and nothing else.)"

struct CoachResult {
  bool answered = false;     // false: the fallback screen is in the display buffer
  int rounds = 0, httpStatus = 0;
  Problems problems;         // of the last round
  std::string lastAnswer;    // raw text of the last LLM answer
  std::string question;      // the question as text (for a spoken one: what the LLM understood)
  std::string screen;        // what is in the display buffer, as a spec with all values filled in (for coachRedraw)
};

// Asks the question and leaves the screen to show in the display buffer (the caller refreshes the panel).
// `log` (optional) is called after every round with the round number, the raw answer and its problems.
inline CoachResult coachAsk(const DashboardData& D, const struct tm& now, const std::string& question, const PostFn& post, const char* hint = "",
                            const std::function<void(int, const std::string&, const Problems&)>& log = nullptr, const CoachAudio* audio = nullptr) {
  CoachResult res;
  res.question = audio ? "" : question;
  JsonDocument facts, frameDoc;
  coachFacts(D, facts);
  coachFrame(D, now, frameDoc.to<JsonObject>(), hint);
  JsonObjectConst frame = frameDoc.as<JsonObjectConst>();
  std::string base = coachPrompt(facts, D, now, audio ? COACH_SPOKEN : question);
  std::string prompt = base;
  Problems earlier;
  for (int round = 1; round <= COACH_ROUNDS; round++) {
    res.rounds = round;
    std::string body, reply;
    {
      JsonDocument req;
      req["model"] = COACH_MODEL;
      if (audio && res.question.empty()) {     // not understood yet: text and recording together
        JsonArray in = req["input"].to<JsonArray>();
        JsonObject t = in.add<JsonObject>();
        t["type"] = "text";
        t["text"] = prompt;
        JsonObject a = in.add<JsonObject>();
        a["type"] = "audio";
        a["data"] = "@AUDIO@";                 // put in below: ArduinoJson strings end at 64 kB
        a["mime_type"] = audio->mime;
      } else req["input"] = prompt;
      req["store"] = false;
      req["response_format"]["type"] = "text";
      req["response_format"]["mime_type"] = "application/json";
      serializeJson(req, body);
      size_t at = body.rfind("@AUDIO@");
      if (audio && res.question.empty() && at != std::string::npos) body.replace(at, 7, audio->base64);
    }
    res.httpStatus = post(body, reply);
    if (res.httpStatus != 200) {
      res.screen = coachSorry(res.question, frame, res.httpStatus == 429 ? "The daily limit of the AI service is used up." : "The AI service did not answer. Please try again later.");
      return res;
    }
    res.lastAnswer = coachAnswerText(reply);
    JsonDocument spec, drawn;
    std::string text = res.lastAnswer;
    size_t a = text.find('{'), b = text.rfind('}');     // tolerate a code fence around the JSON
    if (a != std::string::npos && b != std::string::npos && b > a) text = text.substr(a, b - a + 1);
    DeserializationError err = deserializeJson(spec, text);
    if (!err && audio && res.question.empty()) {
      res.question = spec["question"] | "";
      if (res.question.empty()) {              // silence or no question in the recording
        if (log) log(round, res.lastAnswer, {});
        res.screen = coachSorry("(nothing understood)", frame, "I did not hear a question. Hold the button and speak.");
        return res;
      }
      base = coachPrompt(facts, D, now, res.question);
    }
    if (err) res.problems = {std::string("the answer was not valid JSON: ") + err.c_str()};
    else res.problems = coachCheck(spec, facts.as<JsonVariantConst>(), frame, res.question, drawn);
    if (log) log(round, res.lastAnswer, res.problems);
    if (res.problems.empty()) { res.answered = true; serializeJson(drawn, res.screen); return res; }
    prompt = base + "\n\nYour previous answer:\n" + res.lastAnswer + "\n\nThe drawing code reported these problems. Return a corrected spec:";
    for (auto& p : res.problems) prompt += "\n- " + p;
    if (!earlier.empty()) {
      prompt += "\n\nProblems of earlier attempts, which must not come back:";
      for (auto& p : earlier) prompt += "\n- " + p;
    }
    for (auto& p : res.problems) if (earlier.size() < 12) earlier.push_back(p);
  }
  res.screen = coachSorry(res.question, frame, "Please ask again, maybe in other words.");
  return res;
}

}  // namespace coach
