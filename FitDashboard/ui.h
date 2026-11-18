/*
 * File: ui.h
 * Purpose: Draws the three screens on the 800x480 black/white panel.
 *   Screen 1  Today: what the coach suggests and why, the day on one time axis
 *             (energy, temperature, rain, daylight, best hour to run), body, week, tonight
 *   Screen 2  Body: sleep rhythm, overnight charge, resting heart rate, heart rate today
 *   Screen 3  Running: last run along its distance, heart-rate zones, weeks, runs, 7-day running weather
 * Uses only plain drawing calls of the Seeed_GFX EPaper class, so the PC test
 * renderer can draw the same code with a stand-in class.
 */
#pragma once
#include <math.h>
#include <algorithm>
#include <string>
#include <vector>
#include "model.h"
#include "icons.h"
#include "fonts/NotoSansMedium18.h"
#include "fonts/NotoSansMedium24.h"
#include "fonts/NotoSansSemiBold32.h"
#include "fonts/NotoSansBold56.h"

extern EPaper epaper;

enum ScreenId { SCREEN_OVERVIEW = 0, SCREEN_HEALTH = 1, SCREEN_TRAINING = 2, SCREEN_COUNT = 3 };

static const int SCR_W = 800, SCR_H = 480;
static const int HEADER_H = 46;
static const int CLOCK_X = 676, CLOCK_Y = 0, CLOCK_W = 124, CLOCK_H = 44;  // partial-refresh window

// ----------------------------------------------------------------- text
static const uint8_t* activeFont = nullptr;
inline void useFont(int size) {
  const uint8_t* f = NotoSansMedium18;
  if (size >= 56) f = NotoSansBold56;
  else if (size >= 32) f = NotoSansSemiBold32;
  else if (size >= 24) f = NotoSansMedium24;
  if (f != activeFont) { epaper.loadFont(f); activeFont = f; }
}
// maxW > 0: shorten the text with "…" until it fits into maxW pixels
inline void text(const char* s, int x, int y, int size, uint8_t datum = ML_DATUM, bool inverse = false, int maxW = 0) {
  useFont(size);
  epaper.setTextDatum(datum);
  if (inverse) epaper.setTextColor(TFT_WHITE, TFT_BLACK);
  else epaper.setTextColor(TFT_BLACK, TFT_WHITE);
  if (maxW > 0 && epaper.textWidth(s) > maxW) {
    char buf[96];
    size_t n = strlen(s) < sizeof(buf) - 4 ? strlen(s) : sizeof(buf) - 4;
    memcpy(buf, s, n);
    buf[n] = 0;
    while (n > 0) {
      do { n--; } while (n > 0 && (buf[n] & 0xC0) == 0x80);  // step back one UTF-8 character
      while (n > 0 && buf[n - 1] == ' ') n--;
      memcpy(buf + n, "…", 4);
      if (epaper.textWidth(buf) <= maxW) break;
    }
    epaper.drawString(buf, x, y);
    return;
  }
  epaper.drawString(s, x, y);
}
inline int textW(const char* s, int size) { useFont(size); return epaper.textWidth(s); }

static const char* WD_SHORT[7] = {"Mo", "Di", "Mi", "Do", "Fr", "Sa", "So"};
static const char* WD_LONG[7] = {"Montag", "Dienstag", "Mittwoch", "Donnerstag", "Freitag", "Samstag", "Sonntag"};
static const char* MONTHS[12] = {"Januar", "Februar", "März", "April", "Mai", "Juni", "Juli", "August",
                                 "September", "Oktober", "November", "Dezember"};

inline void fmtPace(char* out, size_t n, float secPerKm) {
  int s = (int)lroundf(secPerKm);
  snprintf(out, n, "%d:%02d", s / 60, s % 60);
}
inline void fmtDuration(char* out, size_t n, float sec) {
  int s = (int)lroundf(sec);
  if (s >= 3600) snprintf(out, n, "%d:%02d:%02d", s / 3600, (s / 60) % 60, s % 60);
  else snprintf(out, n, "%d:%02d", s / 60, s % 60);
}
inline void fmtHours(char* out, size_t n, float h) {
  int mins = (int)lroundf(h * 60);
  snprintf(out, n, "%d h %02d", mins / 60, mins % 60);
}
inline void fmtDayShort(char* out, size_t n, long day) {
  char iso[11];
  isoFromDayNumber(day, iso);
  snprintf(out, n, "%s %d.%d.", WD_SHORT[weekdayMon0(day)], atoi(iso + 8), atoi(iso + 5));
}

// ----------------------------------------------------------------- shapes
inline void hline(int x, int y, int w) { epaper.drawFastHLine(x, y, w, TFT_BLACK); }
inline void vline(int x, int y, int h) { epaper.drawFastVLine(x, y, h, TFT_BLACK); }
inline void dottedH(int x, int y, int w, int gap = 4) { for (int i = 0; i < w; i += gap) epaper.drawPixel(x + i, y, TFT_BLACK); }
// 1-bit "grey": every n-th pixel black
inline void shade(int x, int y, int w, int h, int n = 2) {
  for (int j = 0; j < h; j++)
    for (int i = (j % n); i < w; i += n) epaper.drawPixel(x + i, y + j, TFT_BLACK);
}
inline void hatch(int x, int y, int w, int h) {  // diagonal lines
  for (int j = 0; j < h; j++)
    for (int i = 0; i < w; i++) if (((i + j) % 4) == 0) epaper.drawPixel(x + i, y + j, TFT_BLACK);
}
inline void thickLine(int x0, int y0, int x1, int y1) {
  epaper.drawLine(x0, y0, x1, y1, TFT_BLACK);
  epaper.drawLine(x0, y0 + 1, x1, y1 + 1, TFT_BLACK);
}
inline void dashedLine(int x0, int y0, int x1, int y1) {
  int steps = abs(x1 - x0) > abs(y1 - y0) ? abs(x1 - x0) : abs(y1 - y0);
  for (int i = 0; i <= steps; i++) {
    if ((i / 4) % 2) continue;
    int x = x0 + (x1 - x0) * i / (steps ? steps : 1), y = y0 + (y1 - y0) * i / (steps ? steps : 1);
    epaper.drawPixel(x, y, TFT_BLACK);
    epaper.drawPixel(x, y + 1, TFT_BLACK);
  }
}
inline void progressBar(int x, int y, int w, int h, float frac) {
  frac = frac < 0 ? 0 : (frac > 1 ? 1 : frac);
  epaper.drawRoundRect(x, y, w, h, h / 2, TFT_BLACK);
  int fw = (int)((w - 4) * frac);
  if (fw > 0) epaper.fillRoundRect(x + 2, y + 2, fw, h - 4, (h - 4) / 2, TFT_BLACK);
}
inline void sectionLabel(const char* s, int x, int y) { text(s, x, y, 18, ML_DATUM); }
// NotoSans has no arrow or check-mark glyphs, so these are drawn.
// small triangle pointing up (delta > dead) or down (delta < -dead), centred at x, y
inline void trendMark(int x, int y, float delta, float dead) {
  if (delta > dead) epaper.fillTriangle(x - 6, y + 4, x + 6, y + 4, x, y - 6, TFT_BLACK);
  else if (delta < -dead) epaper.fillTriangle(x - 6, y - 4, x + 6, y - 4, x, y + 6, TFT_BLACK);
}
// status: 0 check mark, 1 exclamation mark, 2 inverted exclamation, -1 dot; x, y = centre
inline void statusMark(int x, int y, int status) {
  if (status == 0) { thickLine(x - 6, y, x - 2, y + 5); thickLine(x - 2, y + 5, x + 6, y - 6); }
  else if (status == -1) epaper.fillCircle(x, y, 3, TFT_BLACK);
  else {
    uint16_t fg = status == 2 ? TFT_WHITE : TFT_BLACK;
    if (status == 1) epaper.fillCircle(x, y, 8, TFT_BLACK);
    epaper.fillRect(x - 1, y - 5, 3, 7, status == 1 ? TFT_WHITE : fg);
    epaper.fillRect(x - 1, y + 4, 3, 2, status == 1 ? TFT_WHITE : fg);
  }
}

// WMO weather code -> 64x64 icon of icons.h
inline const unsigned char* weatherIcon(int code) {
  if (code == 0) return icon_01;
  if (code <= 2) return icon_02;
  if (code == 3) return icon_03;
  if (code == 45 || code == 48) return icon_50;
  if ((code >= 71 && code <= 77) || code == 85 || code == 86) return icon_13;
  if (code >= 95) return icon_11;
  if (code >= 51) return icon_09;
  return icon_default;
}
inline void drawIcon64(int x, int y, int code) {  // x, y = top-left
  epaper.drawBitmap(x, y, weatherIcon(code), 64, 64, TFT_WHITE, TFT_BLACK);
}

// ----------------------------------------------------------------- header + clock
inline void drawClock(const struct tm& now) {
  char buf[8];
  snprintf(buf, sizeof(buf), "%02d:%02d", now.tm_hour, now.tm_min);
  text(buf, SCR_W - 14, CLOCK_Y + 22, 32, MR_DATUM);
}

inline void drawHeader(const DashboardData& D, const struct tm& now, int screen, const char* title, int batteryPct) {
  char buf[48];
  int wd = (now.tm_wday + 6) % 7;
  if (title) snprintf(buf, sizeof(buf), "%s", title);
  else snprintf(buf, sizeof(buf), "%s, %d. %s", WD_LONG[wd], now.tm_mday, MONTHS[now.tm_mon]);
  text(buf, 16, 23, 24, ML_DATUM);
  // page dots
  for (int i = 0; i < SCREEN_COUNT; i++) {
    int cx = 400 - 18 + i * 18;
    if (i == screen) epaper.fillCircle(cx, 23, 5, TFT_BLACK);
    else epaper.drawCircle(cx, 23, 5, TFT_BLACK);
  }
  // battery + data age
  if (batteryPct >= 0) {
    epaper.drawRect(470, 15, 28, 15, TFT_BLACK);
    epaper.fillRect(498, 19, 3, 7, TFT_BLACK);
    int fw = (int)(24 * batteryPct / 100);
    if (fw > 0) epaper.fillRect(472, 17, fw, 11, TFT_BLACK);
    snprintf(buf, sizeof(buf), "%d %%", batteryPct);
    text(buf, 508, 23, 18, ML_DATUM);
  }
  snprintf(buf, sizeof(buf), "Stand %s", D.updatedAt);
  text(buf, CLOCK_X - 10, 23, 18, MR_DATUM);
  drawClock(now);
  hline(0, HEADER_H, SCR_W);
}

// ----------------------------------------------------------------- charts
// line through values (NAN = gap); returns nothing; y grows downwards
inline void lineChart(int x, int y, int w, int h, const float* v, int n, float lo, float hi, bool dashed = false) {
  if (n < 2 || hi <= lo) return;
  int px = -1, py = -1;
  for (int i = 0; i < n; i++) {
    if (isnan(v[i])) { px = -1; continue; }
    int cx = x + (int)((float)i * (w - 1) / (n - 1));
    int cy = y + h - 1 - (int)((v[i] - lo) / (hi - lo) * (h - 1));
    if (px >= 0) { if (dashed) dashedLine(px, py, cx, cy); else thickLine(px, py, cx, cy); }
    px = cx; py = cy;
  }
}

// ----------------------------------------------------------------- panel helpers
inline void panelTitle(const char* s, int x, int y, const char* right = nullptr, int rx = 0) {
  text(s, x, y, 18, ML_DATUM);
  if (right) text(right, rx, y, 18, MR_DATUM);
}

inline void dayAxis(int x, int w, int y, int n, long firstDay) {
  float slot = (float)w / n;
  for (int i = 0; i < n; i++) {
    long day = firstDay + i;
    if ((n - 1 - i) % 2) continue;  // every other day, ending with today
    text(WD_SHORT[weekdayMon0(day)], x + (int)(i * slot + slot / 2), y, 18, MC_DATUM);
  }
}

// word wrap; returns the y after the last line
inline int wrapText(const char* s, int x, int y, int size, int maxW, int lineH, int maxLines) {
  std::string line, word;
  int lines = 0;
  auto flush = [&]() { text(line.c_str(), x, y + lines * lineH, size, ML_DATUM, false, maxW); lines++; line.clear(); };
  for (const char* p = s;; p++) {
    if (*p == ' ' || *p == 0) {
      std::string trial = line.empty() ? word : line + " " + word;
      if (textW(trial.c_str(), size) > maxW && !line.empty()) {
        if (lines == maxLines - 1) { line = trial; flush(); return y + lines * lineH; }
        flush();
        line = word;
      } else line = trial;
      word.clear();
      if (*p == 0) break;
    } else word += *p;
  }
  if (!line.empty()) flush();
  return y + lines * lineH;
}
inline void invLabel(const char* s, int cx, int y, int size = 18) {
  int w = textW(s, size) + 12;
  epaper.fillRect(cx - w / 2, y - 12, w, 24, TFT_BLACK);
  text(s, cx, y, size, MC_DATUM, true);
}
inline const char* rainWord(int prob) { return prob < 20 ? "trocken" : (prob < 50 ? "evtl. Regen" : "Regen"); }

// ================================================================= Coach SCREEN 1: today
inline void drawToday(const DashboardData& D, const struct tm& now, int batteryPct) {
  const Coach& X = D.c;
  const HealthToday& t = D.today;
  const Metrics& m = D.m;
  char buf[96], b2[32];
  drawHeader(D, now, 0, nullptr, batteryPct);
  const int L = 16, R = SCR_W - 16;

  // ---- coach
  const int EX = 606;
  sectionLabel("COACH", L, 64);
  text(X.coachHead, L, 96, 32, ML_DATUM, false, EX - 30 - L);
  wrapText(X.coachBody, L, 130, 24, EX - 30 - L, 27, 2);
  vline(EX - 16, 56, 132);
  sectionLabel("ENERGIE", EX, 64);
  int bb = t.bbNow >= 0 ? t.bbNow : t.bbHigh;
  if (bb >= 0) text(m.recoveryWord, R, 64, 18, MR_DATUM);
  if (bb >= 0) snprintf(buf, sizeof(buf), "%d", bb); else strcpy(buf, "--");
  text(buf, EX, 112, 56, ML_DATUM);
  int bx = EX + textW(buf, 56) + 10;
  if (t.bbWake >= 0) { text("Aufwachen", bx, 100, 18); snprintf(buf, sizeof(buf), "%d", t.bbWake); text(buf, bx, 124, 24); }
  const CoachDay* td = coachDay(X, D.todayNum);
  if (td && td->charged > 0 && t.sleepH > 0) {
    fmtHours(b2, sizeof(b2), t.sleepH);
    snprintf(buf, sizeof(buf), "Nacht +%d in %s", td->charged, b2);
    text(buf, EX, 160, 18, ML_DATUM, false, R - EX);
  }
  hline(L, 192, R - L);

  // ---- the day on one time axis: energy, temperature, rain, daylight
  const int AX = 62, AW = R - AX;                 // 0-24 h
  auto hx = [&](float hour) { return AX + (int)(hour * AW / 24.0f); };
  sectionLabel("DEIN TAG", L, 210);
  int k0 = (int)(D.todayNum - X.wxFirstDay);
  const RunWindow* w = (k0 >= 0 && k0 < 7 && X.win[k0].hour >= 0 && D.nowMin < 19 * 60) ? &X.win[k0] : nullptr;
  if (w) {
    char cl[64];
    clothingFor(w->feels, w->prob, w->wind, w->dark, cl, sizeof(cl));
    snprintf(buf, sizeof(buf), "Lauf am besten %02d–%02d Uhr • %s • %s", w->hour, w->hour + 1, rainWord(w->prob), cl);
  } else if (k0 + 1 < 7 && X.win[k0 + 1].hour >= 0) {
    const RunWindow& n = X.win[k0 + 1];
    snprintf(buf, sizeof(buf), "Morgen am besten %02d–%02d Uhr • %s • %d°", n.hour, n.hour + 1, rainWord(n.prob), (int)lroundf(n.temp));
  } else buf[0] = 0;
  if (D.weatherStatus[0]) snprintf(buf, sizeof(buf), "Wetter: %s", D.weatherStatus);
  text(buf, R, 210, 18, MR_DATUM, false, 620);

  const int cy = 226, ch = 64;                    // energy chart
  dottedH(AX, cy, AW, 6); dottedH(AX, cy + ch / 2, AW, 6); hline(AX, cy + ch, AW);
  text("100", AX - 8, cy + 2, 18, MR_DATUM);
  text("50", AX - 8, cy + ch / 2, 18, MR_DATUM);
  int nowX = hx(D.nowMin / 60.0f);
  if (t.bbCurveN >= 2) {
    int px = -1, py = -1;
    for (int i = 0; i < t.bbCurveN; i++) {
      int x = AX + (int)((float)i * (nowX - AX) / (t.bbCurveN - 1));
      int y = cy + ch - (int)(t.bbCurve[i] / 100.0f * ch);
      if (px >= 0) { thickLine(px, py, x, y); epaper.drawLine(px, py + 2, x, y + 2, TFT_BLACK); }
      for (int yy = y + 4; yy < cy + ch; yy += 3) if ((x + yy) % 3 == 0) epaper.drawPixel(x, yy, TFT_BLACK);
      px = x; py = y;
    }
    // projection until the usual bedtime
    if (X.usualBed >= 0 && X.bbBedProjected >= 0) {
      float bedHour = fminf(24, X.usualBed / 60.0f + 12);
      int ex = hx(bedHour);
      float atEnd = fmaxf(5, t.bbNow - X.drainPerHour * (bedHour - D.nowMin / 60.0f));
      if (ex > px + 12) {
        int ey = cy + ch - (int)(atEnd / 100.0f * ch);
        dashedLine(px, py, ex, ey);
        snprintf(buf, sizeof(buf), "~%d", (int)lroundf(atEnd / 5) * 5);
        text(buf, ex - 6, ey - 14 < cy + 10 ? cy + 10 : ey - 14, 18, MR_DATUM);
      }
    }
  }
  // temperature row, rain lane, daylight strip
  const int ty = 304, ry = 316, rh = 20, sy = ry + rh + 3;
  text("°C", AX - 8, ty, 18, MR_DATUM);
  text("Regen", AX - 8, ry + rh / 2 + 1, 18, MR_DATUM);
  for (auto& h : X.hours) {
    if (h.day != D.todayNum) continue;
    int x0 = hx(h.hour), x1 = hx(h.hour + 1);
    int hh = (int)(h.prob / 100.0f * rh);
    if (hh > 0) { if (h.prob >= 50) epaper.fillRect(x0 + 1, ry + rh - hh, x1 - x0 - 2, hh, TFT_BLACK); else shade(x0 + 1, ry + rh - hh, x1 - x0 - 2, hh); }
    if (h.hour % 3 == 1 && h.hour > 1) { snprintf(buf, sizeof(buf), "%d°", (int)lroundf(h.temp)); text(buf, (x0 + x1) / 2, ty, 18, MC_DATUM); }
    int mid = h.hour * 60 + 30;
    if (k0 >= 0 && k0 < 7 && (mid < X.sunrise[k0] || mid > X.sunset[k0])) epaper.fillRect(x0, sy, x1 - x0, 5, TFT_BLACK);
  }
  hline(AX, ry + rh, AW);
  for (int hgrid = 0; hgrid <= 24; hgrid += 6) {
    int x = hx(hgrid);
    snprintf(buf, sizeof(buf), hgrid == 24 ? "24 Uhr" : "%d", hgrid);
    text(buf, hgrid == 24 ? R : x, sy + 18, 18, hgrid == 0 ? ML_DATUM : (hgrid == 24 ? MR_DATUM : MC_DATUM));
    if (hgrid > 0 && hgrid < 24) for (int y = cy; y < cy + ch; y += 5) epaper.drawPixel(x, y, TFT_BLACK);
  }
  // now marker and run window
  for (int y = cy - 2; y < sy + 5; y += 2) epaper.drawPixel(nowX, y, TFT_BLACK);
  epaper.fillTriangle(nowX - 5, cy - 8, nowX + 5, cy - 8, nowX, cy - 1, TFT_BLACK);
  if (w) {
    int x0 = hx(w->hour), x1 = hx(w->hour + 1);
    epaper.drawRect(x0 - 1, cy - 4, x1 - x0 + 2, sy + 9 - cy, TFT_BLACK);
    epaper.drawRect(x0 - 2, cy - 5, x1 - x0 + 4, sy + 11 - cy, TFT_BLACK);
  }
  hline(L, 374, R - L);

  // ---- bottom: body, week, tonight
  const int C2 = 282, C3 = 548;
  vline(C2 - 12, 382, 90); vline(C3 - 12, 382, 90);
  sectionLabel("KÖRPER", L, 392);
  if (t.rhr > 0) {
    snprintf(buf, sizeof(buf), "Ruhepuls %d", t.rhr);
    text(buf, L, 418, 18);
    if (m.rhrBaseline > 0) {
      int d = t.rhr - m.rhrBaseline;
      if (d == 0) strcpy(buf, "wie immer"); else snprintf(buf, sizeof(buf), "%d %s", abs(d), d > 0 ? "höher" : "tiefer");
      text(buf, C2 - 24 - 16, 418, 18, MR_DATUM);
      trendMark(C2 - 30, 418, (float)d, 1.5f);
    }
  }
  if (t.sleepH > 0) {
    fmtHours(b2, sizeof(b2), t.sleepH);
    snprintf(buf, sizeof(buf), "Schlaf %s", b2);
    text(buf, L, 442, 18);
    bool inc = t.deepH == 0 && t.awakeH == 0;
    if (inc) text("lückenhaft?", C2 - 24, 442, 18, MR_DATUM);
    else if (m.sleepAvgH > 0) {
      int dm = (int)lroundf((t.sleepH - m.sleepAvgH) * 60);
      snprintf(buf, sizeof(buf), "%s%d min", dm >= 0 ? "+" : "-", abs(dm));
      text(buf, C2 - 24, 442, 18, MR_DATUM);
    }
  } else if (!D.garminStatus[0]) text("Schlaf: Uhr nachts tragen", L, 442, 18);
  statusMark(L + 8, 466, m.healthStatus);
  text(m.healthText, L + 24, 466, 18, ML_DATUM, false, C2 - 24 - L - 24);

  sectionLabel("WOCHE", C2, 392);
  float km = m.weekKm[WEEKS_SHOWN - 1];
  fmtDec(b2, sizeof(b2), km, 1);
  snprintf(buf, sizeof(buf), "%s / %d km", b2, D.weeklyGoalKm);
  text(buf, C3 - 24, 392, 18, MR_DATUM);
  const int dw = (C3 - 24 - C2) / 7;
  for (int d = 0; d < 7; d++) {
    int cx = C2 + d * dw + dw / 2;
    bool ran = m.runDaysThisWeek & (1 << d);
    bool best = false;
    for (int b = 0; b < 2; b++) if (X.bestDay[b] >= 0 && weekdayMon0(X.win[X.bestDay[b]].day) == d && X.win[X.bestDay[b]].day - D.todayNum < 7 - m.todayWeekday) best = true;
    if (ran) invLabel(WD_SHORT[d], cx, 418);
    else text(WD_SHORT[d], cx, 418, 18, MC_DATUM);
    if (d == m.todayWeekday) epaper.drawRect(cx - 16, 404, 32, 28, TFT_BLACK);
    if (best && !ran && d >= m.todayWeekday) epaper.fillTriangle(cx - 5, 439, cx + 5, 439, cx, 433, TFT_BLACK);
  }
  progressBar(C2, 443, C3 - 24 - C2, 10, km / (D.weeklyGoalKm > 0 ? D.weeklyGoalKm : 1));
  if (X.bestDay[0] >= 0) {
    const RunWindow& a = X.win[X.bestDay[0]];
    if (X.bestDay[1] >= 0) {
      const RunWindow& b = X.win[X.bestDay[1]];
      snprintf(buf, sizeof(buf), "Am besten %s %d, %s %d Uhr", WD_SHORT[weekdayMon0(a.day)], a.hour, WD_SHORT[weekdayMon0(b.day)], b.hour);
    } else snprintf(buf, sizeof(buf), "Am besten %s %d Uhr", WD_SHORT[weekdayMon0(a.day)], a.hour);
    text(buf, C2, 468, 18, ML_DATUM, false, C3 - 24 - C2);
  }

  sectionLabel("HEUTE ABEND", C3, 392);
  text(X.tipHead, C3, 420, 24, ML_DATUM, false, R - C3);
  wrapText(X.tipBody, C3, 446, 18, R - C3, 22, 2);
}

// ================================================================= Coach SCREEN 2: body
inline void drawBody(const DashboardData& D, const struct tm& now, int batteryPct) {
  const Coach& X = D.c;
  const Metrics& m = D.m;
  const HealthToday& t = D.today;
  char buf[96], b2[24], b3[24];
  drawHeader(D, now, 1, "Körper • 14 Tage", batteryPct);
  const int L1 = 16, R1 = 384, L2 = 416, R2 = 784, N = 14;
  long first = D.todayNum - (N - 1);
  vline(400, HEADER_H + 8, 322);
  auto nightFor = [&](long day) -> const CoachNight* { for (auto& n : X.nights) if (n.day == day) return &n; return nullptr; };

  // P1 sleep rhythm: one bar per night from falling asleep to waking up
  int lo = 9999, hi = -1, cnt = 0;
  for (long d = first; d <= D.todayNum; d++) if (auto n = nightFor(d)) { lo = std::min(lo, n->bedMin); hi = std::max(hi, n->bedMin); cnt++; }
  if (cnt >= 2) { fmtClock(b2, sizeof(b2), lo); fmtClock(b3, sizeof(b3), hi); snprintf(buf, sizeof(buf), "ins Bett %s bis %s", b2, b3); }
  else snprintf(buf, sizeof(buf), "%d Nächte mit Uhr", cnt);
  panelTitle("Schlaf-Rhythmus", L1, 64, buf, R1);
  {
    const int y = 82, h = 106, gx = L1 + 28, gw = R1 - gx;
    const int tMin = 540, tMax = 1380;            // 21:00 to 11:00, minutes after noon
    auto ty = [&](int mAfterNoon) { return y + (int)((float)(mAfterNoon - tMin) / (tMax - tMin) * h); };
    for (int hr : {720, 1080}) {                     // midnight and 6 o'clock
      dottedH(gx, ty(hr), gw, 5);
      snprintf(buf, sizeof(buf), "%d", ((hr + 720) % 1440) / 60);
      text(buf, gx - 6, ty(hr), 18, MR_DATUM);
    }
    float slot = (float)gw / N;
    for (int i = 0; i < N; i++) {
      const CoachNight* n = nightFor(first + i);
      int bx = gx + (int)(i * slot) + 5, bw = (int)slot - 10;
      if (!n) continue;
      int y0 = ty(std::max(tMin, n->bedMin)), y1 = ty(std::min(tMax, n->wakeMin));
      if (n->incomplete()) { hatch(bx, y0, bw, y1 - y0); epaper.drawRect(bx, y0, bw, y1 - y0, TFT_BLACK); }
      else epaper.fillRect(bx, y0, bw, y1 - y0, TFT_BLACK);
    }
    dayAxis(gx, gw, y + h + 14, N, first);
  }

  // P2 overnight charge
  snprintf(buf, sizeof(buf), "~%d Punkte je Stunde", (int)lroundf(X.chargePerHour));
  panelTitle("Nacht-Aufladung", L2, 64, buf, R2);
  {
    const int y = 96, h = 92;
    float slot = (float)(R2 - L2) / N;
    hline(L2, y + h, R2 - L2);
    for (int i = 0; i < N; i++) {
      const CoachNight* n = nightFor(first + i);
      const CoachDay* d = coachDay(X, first + i);
      if (!n || !d || d->charged <= 0) continue;
      int bx = L2 + (int)(i * slot) + 3, bw = (int)slot - 6;
      int hh = (int)(std::min(100, d->charged) / 100.0f * h);
      if (n->incomplete()) { hatch(bx, y + h - hh, bw, hh); epaper.drawRect(bx, y + h - hh, bw, hh, TFT_BLACK); }
      else epaper.fillRect(bx, y + h - hh, bw, hh, TFT_BLACK);
      snprintf(buf, sizeof(buf), "%d", d->charged);
      text(buf, bx + bw / 2, y + h - hh - 10, 18, MC_DATUM);
    }
    dayAxis(L2, R2 - L2, y + h + 14, N, first);
  }
  hline(L1, 214, R2 - L1);

  // P3 resting heart rate, 30 days
  if (m.rhrBaseline > 0 && t.rhr > 0) snprintf(buf, sizeof(buf), "normal %d • heute %d", m.rhrBaseline, t.rhr);
  else snprintf(buf, sizeof(buf), "Basis ab 3 Nächten");
  panelTitle("Ruhepuls • 30 Tage", L1, 232, buf, R1);
  {
    const int y = 252, h = 92, n = 30, gx = L1 + 30, gw = R1 - gx;
    float lo2 = 1e9, hi2 = -1e9;
    std::vector<std::pair<int, int>> pts;
    for (auto& d : X.days) {
      int i = (int)(d.day - (D.todayNum - (n - 1)));
      if (i < 0 || i >= n || d.rhr <= 0) continue;
      pts.push_back({i, d.rhr}); lo2 = fminf(lo2, d.rhr); hi2 = fmaxf(hi2, d.rhr);
    }
    if (pts.size() >= 2) {
      lo2 = floorf(lo2 - 3); hi2 = ceilf(hi2 + 3);
      auto py = [&](float v) { return y + h - (int)((v - lo2) / (hi2 - lo2) * h); };
      if (m.rhrBaseline > 0) dottedH(gx, py(m.rhrBaseline), gw, 3);
      int px = -1, pyy = -1, pi = -99;
      for (auto& p : pts) {
        int cx = gx + (int)((p.first + 0.5f) * gw / n), cyy = py(p.second);
        if (px >= 0 && p.first - pi <= 2) thickLine(px, pyy, cx, cyy);
        else if (px >= 0) dashedLine(px, pyy, cx, cyy);
        epaper.fillCircle(cx, cyy, 3, TFT_BLACK);
        px = cx; pyy = cyy; pi = p.first;
      }
      epaper.drawCircle(px, pyy, 7, TFT_BLACK);
      snprintf(buf, sizeof(buf), "%d", (int)hi2); text(buf, L1, y + 6, 18);
      snprintf(buf, sizeof(buf), "%d", (int)lo2); text(buf, L1, y + h - 6, 18);
      text("vor 30 Tagen", gx, y + h + 14, 18);
      text("heute", R1, y + h + 14, 18, MR_DATUM);
    } else text("noch keine Werte", L1, y + h / 2, 24);
  }

  // P4 heart rate over the day
  {
    int lo3 = 999, hi3 = 0, loMin = 0;
    for (auto& p : X.dayHr) { if (p.second < lo3) { lo3 = p.second; loMin = p.first; } hi3 = std::max(hi3, p.second); }
    if (X.dayHr.size() >= 4) snprintf(buf, sizeof(buf), "Tief %d um %02d:%02d", lo3, loMin / 60, loMin % 60);
    else buf[0] = 0;
    panelTitle("Puls heute", L2, 232, buf, R2);
    const int y = 252, h = 92, gx = L2 + 30, gw = R2 - gx;
    if (X.dayHr.size() >= 4) {
      float a = floorf((lo3 - 5) / 10.0f) * 10, b = ceilf((hi3 + 5) / 10.0f) * 10;
      dottedH(gx, y, gw, 6); hline(gx, y + h, gw);
      int px = -1, py = -1;
      for (auto& p : X.dayHr) {
        int cx = gx + (int)(p.first / 1440.0f * gw), cyy = y + h - (int)((p.second - a) / (b - a) * h);
        if (px >= 0 && cx - px < 12) thickLine(px, py, cx, cyy);
        px = cx; py = cyy;
      }
      snprintf(buf, sizeof(buf), "%d", (int)b); text(buf, L2, y + 6, 18);
      snprintf(buf, sizeof(buf), "%d", (int)a); text(buf, L2, y + h - 6, 18);
      text("0", gx, y + h + 14, 18); text("12", gx + gw / 2, y + h + 14, 18, MC_DATUM); text("24 Uhr", R2, y + h + 14, 18, MR_DATUM);
    } else text("noch keine Werte", L2, y + h / 2, 24);
  }
  hline(L1, 376, R2 - L1);

  // bottom: stress split, last night, what the dashboard has learned
  const int B2 = 282, B3 = 548;
  vline(B2 - 12, 384, 88); vline(B3 - 12, 384, 88);
  sectionLabel("Stress heute", L1, 394);
  if (t.stressAvg >= 0) { snprintf(buf, sizeof(buf), "%d", t.stressAvg); text(buf, B2 - 24, 394, 18, MR_DATUM); }
  {
    int tot = t.stressMin[0] + t.stressMin[1] + t.stressMin[2] + t.stressMin[3];
    int x = L1, bw = B2 - 24 - L1, y = 410, bh = 16;
    if (tot > 0) {
      for (int k = 0; k < 4; k++) {
        int pw = k == 3 ? L1 + bw - x : (int)lroundf((float)t.stressMin[k] / tot * bw);
        if (k == 1) shade(x, y, pw, bh, 3); else if (k == 2) hatch(x, y, pw, bh); else if (k == 3) epaper.fillRect(x, y, pw, bh, TFT_BLACK);
        x += pw;
      }
      epaper.drawRect(L1, y, bw, bh, TFT_BLACK);
      snprintf(buf, sizeof(buf), "%d %% Ruhe", (int)lroundf(100.0f * t.stressMin[0] / tot));
      text(buf, L1, 444, 18);
      snprintf(buf, sizeof(buf), "%d min mittel bis hoch", t.stressMin[2] + t.stressMin[3]);
      text(buf, L1, 466, 18, ML_DATUM, false, bw);
    } else text("noch keine Werte", L1, 430, 18);
  }
  sectionLabel("Letzte Nacht", B2, 394);
  if (t.sleepH > 0) {
    fmtHours(b2, sizeof(b2), t.sleepH);
    text(b2, B3 - 24, 394, 18, MR_DATUM);
    float tot = t.deepH + t.lightH + t.remH + t.awakeH;
    int x = B2, bw = B3 - 24 - B2, y = 410, bh = 16;
    float parts[4] = {t.deepH, t.lightH, t.remH, t.awakeH};
    for (int k = 0; k < 4; k++) {
      int pw = k == 3 ? B2 + bw - x : (int)lroundf(parts[k] / tot * bw);
      if (k == 0) epaper.fillRect(x, y, pw, bh, TFT_BLACK); else if (k == 1) hatch(x, y, pw, bh); else if (k == 2) shade(x, y, pw, bh);
      x += pw;
    }
    epaper.drawRect(B2, y, bw, bh, TFT_BLACK);
    snprintf(buf, sizeof(buf), "Tief %d %% • REM %d %%", (int)lroundf(t.deepH / tot * 100), (int)lroundf(t.remH / tot * 100));
    text(buf, B2, 444, 18);
    if (t.deepH == 0 && t.awakeH == 0) text("Messung lückenhaft?", B2, 466, 18);
    else if (t.resp > 0) { fmtDec(b2, sizeof(b2), t.resp, 0); snprintf(buf, sizeof(buf), "Atmung %s /min", b2); text(buf, B2, 466, 18); }
  } else text("Uhr nachts tragen", B2, 430, 18);

  sectionLabel("Gelernt", B3, 394);
  {
    int n = (int)X.nights.size();
    if (n < 14) snprintf(buf, sizeof(buf), "%d von 14 Nächten mit Uhr. Ab 14 zeige ich, was deinen Schlaf verbessert.", n);
    else snprintf(buf, sizeof(buf), "Pro Stunde Schlaf lädst du ~%d Punkte, wach verlierst du ~%d je Stunde.",
                  (int)lroundf(X.chargePerHour), (int)lroundf(X.drainPerHour));
    wrapText(buf, B3, 420, 18, R2 - B3, 22, 3);
  }
}

// ================================================================= Coach SCREEN 3: running
inline void drawRun(const DashboardData& D, const struct tm& now, int batteryPct) {
  const Coach& X = D.c;
  const Metrics& m = D.m;
  char buf[96], b2[24], b3[24];
  drawHeader(D, now, 2, "Laufen", batteryPct);
  const int L1 = 16, R2 = 784, SP = 500;
  const CoachRun* last = X.runs.empty() ? nullptr : &X.runs.back();

  // P1 last run: pace, heart rate and elevation over the distance
  if (last) {
    fmtDayShort(b2, sizeof(b2), last->day);
    fmtDec(b3, sizeof(b3), last->km, 1);
    char p[12];
    fmtPace(p, sizeof(p), last->minutes * 60 / (last->km > 0 ? last->km : 1));
    snprintf(buf, sizeof(buf), "%s  %s km • %s /km • Puls %d", b2, b3, p, last->avgHr);
    text(buf, L1, 64, 18, ML_DATUM, false, SP - 16 - L1 - 100);
    text(last->type, SP - 16, 64, 18, MR_DATUM);
  } else text("Noch kein Lauf mit der Uhr", L1, 64, 18);
  {
    const int gx = L1 + 44, gw = SP - 16 - gx - 34, y = 84, h = 100;
    int n = (int)X.runKm.size();
    std::vector<float> ps, hs, as;
    if (n >= 10 && last && X.runDay == last->day)
      for (int i = 0; i < n; i++) { if (!isnan(X.runPace[i])) ps.push_back(X.runPace[i]); if (!isnan(X.runHr[i])) hs.push_back(X.runHr[i]); if (!isnan(X.runAlt[i])) as.push_back(X.runAlt[i]); }
    if (ps.size() >= 10 && !hs.empty()) {
      std::sort(ps.begin(), ps.end()); std::sort(hs.begin(), hs.end()); std::sort(as.begin(), as.end());
      float pLo = floorf(ps[ps.size() / 10] / 30) * 30 - 30, pHi = ceilf(ps[ps.size() * 9 / 10] / 30) * 30 + 30;  // fast, slow
      float hLo = floorf(hs[hs.size() / 20] / 10) * 10 - 10, hHi = ceilf(hs.back() / 10) * 10;
      float aLo = as.empty() ? 0 : as.front(), aHi = as.empty() ? 1 : fmaxf(as.back(), aLo + 20);
      float total = X.runKm.back();
      auto smooth = [&](const std::vector<float>& v, int r) {
        std::vector<float> o(v.size(), NAN);
        for (int i = 0; i < (int)v.size(); i++) {
          float sum = 0; int c = 0;
          for (int j = i - r; j <= i + r; j++) if (j >= 0 && j < (int)v.size() && !isnan(v[j])) { sum += v[j]; c++; }
          if (c > r && !isnan(v[i])) o[i] = sum / c;
        }
        return o;
      };
      std::vector<float> pace = smooth(X.runPace, 4), hr = smooth(X.runHr, 3);
      auto cx = [&](int i) { return gx + (int)(X.runKm[i] / total * gw); };
      // elevation as light area in the lower third
      for (int i = 0; i < n; i++) {
        if (isnan(X.runAlt[i])) continue;
        int top = y + h - 3 - (int)((X.runAlt[i] - aLo) / (aHi - aLo) * (h / 4));
        int x0 = cx(i), x1 = i + 1 < n ? cx(i + 1) : x0 + 2;
        shade(x0, top, x1 - x0 + 1, y + h - top, 4);
      }
      dottedH(gx, y, gw, 6); hline(gx, y + h, gw);
      int px = -1, py = -1, qx = -1, qy = -1;
      for (int i = 0; i < n; i++) {
        if (!isnan(hr[i])) {
          int yy = y + h - (int)((fminf(fmaxf(hr[i], hLo), hHi) - hLo) / (hHi - hLo) * h);
          if (qx >= 0) epaper.drawLine(qx, qy, cx(i), yy, TFT_BLACK);  // thin = heart rate
          qx = cx(i); qy = yy;
        }
        if (isnan(pace[i])) { px = -1; continue; }
        float pv = fminf(fmaxf(pace[i], pLo), pHi);
        int yy = y + (int)((pv - pLo) / (pHi - pLo) * h);   // faster = higher
        if (px >= 0) thickLine(px, py, cx(i), yy);
        px = cx(i); py = yy;
      }
      fmtPace(b2, sizeof(b2), pLo); text(b2, gx - 6, y + 6, 18, MR_DATUM);
      fmtPace(b2, sizeof(b2), pHi); text(b2, gx - 6, y + h - 6, 18, MR_DATUM);
      snprintf(buf, sizeof(buf), "%d", (int)hHi); text(buf, gx + gw + 6, y + 6, 18);
      snprintf(buf, sizeof(buf), "%d", (int)hLo); text(buf, gx + gw + 6, y + h - 6, 18);
      for (int k = 0; k <= (int)total; k += 2) {
        int x = gx + (int)(k / total * gw);
        vline(x, y + h, 4);
        snprintf(buf, sizeof(buf), "%d", k);
        text(buf, x, y + h + 14, 18, MC_DATUM);
      }
      text("km", gx - 14, y + h + 14, 18, MR_DATUM);
      // legend
      int lx = gx;
      thickLine(lx, 218, lx + 22, 218); text("Pace", lx + 28, 218, 18); lx += 28 + textW("Pace", 18) + 22;
      hline(lx, 218, 22); text("Puls", lx + 28, 218, 18); lx += 28 + textW("Puls", 18) + 22;
      shade(lx, 212, 22, 12, 4); epaper.drawRect(lx, 212, 22, 12, TFT_BLACK); text("Höhe", lx + 28, 218, 18);
    } else text("Verlauf nach dem nächsten Lauf", gx, y + h / 2, 18);
  }
  vline(SP, HEADER_H + 8, 180);

  // P2 heart-rate zones of the last 14 days
  {
    const int X0 = SP + 16;
    float z[5] = {0, 0, 0, 0, 0}, tot = 0; int runs = 0;
    for (auto& r : X.runs) if (D.todayNum - r.day <= 14) { runs++; for (int i = 0; i < 5; i++) { z[i] += r.zoneMin[i]; tot += r.zoneMin[i]; } }
    snprintf(buf, sizeof(buf), "%d Läufe, 14 Tage", runs);
    panelTitle("Pulszonen", X0, 64, runs ? buf : "keine Läufe", R2);
    float mx = 1;
    for (int i = 0; i < 5; i++) mx = fmaxf(mx, z[i]);
    const int bx = X0 + 112, bw = R2 - bx - 62;
    for (int i = 4; i >= 0; i--) {
      int y = 86 + (4 - i) * 23;
      if (i < 4) snprintf(buf, sizeof(buf), "Z%d %d–%d", i + 1, X.zoneFloor[i], X.zoneFloor[i + 1]);
      else snprintf(buf, sizeof(buf), "Z5 ab %d", X.zoneFloor[4]);
      text(buf, X0, y, 18);
      int w = (int)(z[i] / mx * bw);
      if (w > 0) epaper.fillRect(bx, y - 7, w, 14, TFT_BLACK);
      else vline(bx, y - 7, 14);
      snprintf(buf, sizeof(buf), "%d min", (int)lroundf(z[i]));
      text(buf, R2, y, 18, MR_DATUM);
    }
    if (tot > 0) {
      int hard = (int)lroundf((z[2] + z[3] + z[4]) / tot * 100);
      if (hard >= 50) snprintf(buf, sizeof(buf), "%d %% ab Zone 3: zu hart", hard);
      else if (z[3] + z[4] < 0.05f * tot && runs >= 4) snprintf(buf, sizeof(buf), "Kaum Zone 4–5: Tempo fehlt");
      else snprintf(buf, sizeof(buf), "%d %% locker: gute Mischung", 100 - hard);
      text(buf, X0, 212, 18, ML_DATUM, false, R2 - X0);
    }
  }
  hline(L1, 236, R2 - L1);

  // P3 weekly km
  const int R1 = 384, L2 = 416;
  snprintf(buf, sizeof(buf), "Ziel %d km • Fitness %d", D.weeklyGoalKm, (int)lroundf(m.ctl));
  panelTitle("Wochen-km", L1, 254, buf, R1);
  {
    const int y = 280, h = 62, n = WEEKS_SHOWN;
    float hi = (float)D.weeklyGoalKm * 1.15f;
    for (int i = 0; i < n; i++) hi = fmaxf(hi, m.weekKm[i]);
    float slot = (float)(R1 - L1) / n;
    hline(L1, y + h, R1 - L1);
    dottedH(L1, y + h - (int)(D.weeklyGoalKm / hi * h), R1 - L1, 3);
    for (int i = 0; i < n; i++) {
      int bx = L1 + (int)(i * slot) + 6, bw = (int)slot - 12;
      int hh = (int)(m.weekKm[i] / hi * h);
      if (hh > 0) { if (i == n - 1) { shade(bx, y + h - hh, bw, hh); epaper.drawRect(bx, y + h - hh, bw, hh, TFT_BLACK); } else epaper.fillRect(bx, y + h - hh, bw, hh, TFT_BLACK); }
      if (m.weekKm[i] > 0) { snprintf(buf, sizeof(buf), "%d", (int)lroundf(m.weekKm[i])); text(buf, bx + bw / 2, y + h - hh - 10, 18, MC_DATUM); }
      char iso[11]; isoFromDayNumber(m.weekStart[i], iso);
      snprintf(buf, sizeof(buf), "%d.%d.", atoi(iso + 8), atoi(iso + 5));
      if (i % 2 == 1) text(buf, bx + bw / 2, y + h + 14, 18, MC_DATUM);
    }
  }
  vline(400, 244, 122);

  // P4 last runs with efficiency (metres per heartbeat)
  {
    const int y0 = 254;
    static const int c[5] = {L2, L2 + 138, L2 + 196, L2 + 244, L2 + 300};
    text("Läufe", c[0], y0, 18); text("km", c[1], y0, 18, MR_DATUM); text("Pace", c[2], y0, 18, MR_DATUM);
    text("Puls", c[3], y0, 18, MR_DATUM); text("m/Schlag", R2, y0, 18, MR_DATUM);
    hline(L2, y0 + 12, R2 - L2);
    int y = y0 + 30, shown = 0;
    for (int i = (int)X.runs.size() - 1; i >= 0 && shown < 4; i--, shown++, y += 24) {
      const CoachRun& r = X.runs[i];
      fmtDayShort(buf, sizeof(buf), r.day); text(buf, c[0], y, 18);
      fmtDec(b2, sizeof(b2), r.km, 1); text(b2, c[1], y, 18, MR_DATUM);
      fmtPace(b2, sizeof(b2), r.minutes * 60 / (r.km > 0 ? r.km : 1)); text(b2, c[2], y, 18, MR_DATUM);
      snprintf(b2, sizeof(b2), "%d", r.avgHr); text(r.avgHr ? b2 : "-", c[3], y, 18, MR_DATUM);
      if (r.avgHr > 0 && r.minutes > 0) { fmtDec(b2, sizeof(b2), r.km * 1000 / r.minutes / r.avgHr, 2); text(b2, R2, y, 18, MR_DATUM); }
    }
    if (!shown) text("Noch kein Lauf mit der Uhr", L2, y, 18);
  }
  hline(L1, 370, R2 - L1);

  // bottom: running weather for 7 days, best hour per day
  {
    float slot = (float)(R2 - L1) / 7;
    for (int k = 0; k < 7; k++) {
      int x = L1 + (int)(k * slot), cxm = x + (int)(slot / 2);
      const RunWindow& w = X.win[k];
      long day = X.wxFirstDay + k;
      bool best = k == X.bestDay[0] || k == X.bestDay[1];
      const char* name = day == D.todayNum ? "Heute" : WD_LONG[weekdayMon0(day)];
      char shortName[16];
      snprintf(shortName, sizeof(shortName), "%s", day == D.todayNum ? "Heute" : WD_SHORT[weekdayMon0(day)]);
      (void)name;
      if (best) invLabel(shortName, cxm, 390, 24); else text(shortName, cxm, 390, 24, MC_DATUM);
      if (w.hour >= 0) {
        snprintf(buf, sizeof(buf), "%d–%d Uhr", w.hour, w.hour + 1);
        text(buf, cxm, 418, 18, MC_DATUM);
        snprintf(buf, sizeof(buf), "%d° • %d %%", (int)lroundf(w.temp), w.prob);
        text(buf, cxm, 440, 18, MC_DATUM);
      } else text("vorbei", cxm, 418, 18, MC_DATUM);
      // rain probability 6-22 h as mini bars
      const int sx = x + 8, sw = (int)slot - 16, sy = 454, sh = 16;
      hline(sx, sy + sh, sw);
      for (auto& hh : X.hours) {
        if (hh.day != day || hh.hour < 6 || hh.hour >= 22) continue;
        int x0 = sx + (hh.hour - 6) * sw / 16, x1 = sx + (hh.hour - 5) * sw / 16;
        int bh = (int)(hh.prob / 100.0f * sh);
        if (bh > 0) { if (hh.prob >= 50) epaper.fillRect(x0, sy + sh - bh, x1 - x0 - 1, bh, TFT_BLACK); else shade(x0, sy + sh - bh, x1 - x0 - 1, bh); }
        if (hh.hour == w.hour) epaper.fillTriangle((x0 + x1) / 2 - 4, sy + sh + 7, (x0 + x1) / 2 + 4, sy + sh + 7, (x0 + x1) / 2, sy + sh + 2, TFT_BLACK);
      }
      if (k > 0) vline(x, 380, 92);
    }
  }
}

inline void drawScreen(int screen, const DashboardData& D, const struct tm& now, int batteryPct) {
  epaper.fillScreen(TFT_WHITE);
  if (screen == SCREEN_HEALTH) drawBody(D, now, batteryPct);
  else if (screen == SCREEN_TRAINING) drawRun(D, now, batteryPct);
  else drawToday(D, now, batteryPct);
}
