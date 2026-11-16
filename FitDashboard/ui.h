/*
 * File: ui.h
 * Purpose: Draws the three screens on the 800x480 black/white panel.
 *   Screen 1  Overview: health left, running + weather right
 *   Screen 2  Health details (14 days)
 *   Screen 3  Training details (form, weeks, runs, predictions, 3-day weather)
 * Uses only plain drawing calls of the Seeed_GFX EPaper class, so the PC test
 * renderer can draw the same code with a stand-in class.
 */
#pragma once
#include <math.h>
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
static const int COL_SPLIT = 330;           // screen 1: x of the vertical divider
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

// ================================================================= SCREEN 1
inline void drawOverview(const DashboardData& D, const struct tm& now, int batteryPct) {
  const HealthToday& t = D.today;
  const Metrics& m = D.m;
  char buf[64], b2[32];
  drawHeader(D, now, SCREEN_OVERVIEW, nullptr, batteryPct);
  vline(COL_SPLIT, HEADER_H + 8, SCR_H - HEADER_H - 16);

  // ---------------- left: health
  const int L = 16, R = COL_SPLIT - 16;
  sectionLabel("ERHOLUNG", L, 66);
  text(m.recoveryWord, R, 66, 18, MR_DATUM);
  if (D.garminStatus[0]) {
    text("Garmin:", L, 130, 24);
    text(D.garminStatus, L, 162, 18, ML_DATUM, false, R - L);
  } else {
    int bb = t.bbNow >= 0 ? t.bbNow : t.bbHigh;
    snprintf(buf, sizeof(buf), "%s", bb >= 0 ? "" : "--");
    if (bb >= 0) snprintf(buf, sizeof(buf), "%d", bb);
    text(buf, L, 118, 56, ML_DATUM);
    int bx = L + textW(buf, 56) + 14;
    text("Body Battery", bx, 104, 24);
    if (t.bbWake >= 0) snprintf(buf, sizeof(buf), "Aufwachen %d", t.bbWake);
    else snprintf(buf, sizeof(buf), "Höchstwert %d", t.bbHigh);
    text(buf, bx, 132, 18);
  }
  // today's body battery curve
  const int cy0 = 154, ch = 50;
  dottedH(L, cy0, R - L, 6);
  dottedH(L, cy0 + ch, R - L, 6);
  if (t.bbCurveN >= 2) {
    float v[HealthToday::CURVE_MAX];
    for (int i = 0; i < t.bbCurveN; i++) v[i] = t.bbCurve[i];
    // x axis = whole day 0-24 h, the curve ends at "now"
    int wNow = (int)((R - L) * (D.nowMin / 1440.0f));
    if (wNow < 20) wNow = 20;
    lineChart(L, cy0, wNow, ch, v, t.bbCurveN, 0, 100);
    for (int i = 0; i < t.bbCurveN; i++) {
      int cx = L + (int)((float)i * (wNow - 1) / (t.bbCurveN - 1));
      int yy = cy0 + ch - 1 - (int)(v[i] / 100.0f * (ch - 1));
      for (int yy2 = yy + 2; yy2 < cy0 + ch; yy2 += 3) if ((cx + yy2) % 3 == 0) epaper.drawPixel(cx, yy2, TFT_BLACK);
    }
  }
  text("0", L, cy0 + ch + 12, 18, ML_DATUM);
  text("12", L + (R - L) / 2, cy0 + ch + 12, 18, MC_DATUM);
  text("24 Uhr", R, cy0 + ch + 12, 18, MR_DATUM);
  hline(L, 230, R - L);

  // sleep
  sectionLabel("Schlaf", L, 248);
  if (t.sleepH > 0) {
    fmtHours(buf, sizeof(buf), t.sleepH);
    text(buf, L, 278, 32);
    float tot = t.deepH + t.lightH + t.remH + t.awakeH;
    snprintf(buf, sizeof(buf), "Tief %d %% • REM %d %%", (int)lroundf(t.deepH / tot * 100), (int)lroundf(t.remH / tot * 100));
    text(buf, R, 278, 18, MR_DATUM);
    // phase bar: deep black, light hatched, REM shaded, awake white
    int x = L, bw = R - L, y = 300, bh = 12;
    float parts[4] = {t.deepH, t.lightH, t.remH, t.awakeH};
    for (int k = 0; k < 4; k++) {
      int pw = (int)lroundf(parts[k] / tot * bw);
      if (k == 3) pw = L + bw - x;
      if (k == 0) epaper.fillRect(x, y, pw, bh, TFT_BLACK);
      else if (k == 1) hatch(x, y, pw, bh);
      else if (k == 2) shade(x, y, pw, bh);
      x += pw;
    }
    epaper.drawRect(L, y, bw, bh, TFT_BLACK);
  } else {
    text("keine Daten", L, 278, 24);
    text("Uhr nachts tragen", R, 278, 18, MR_DATUM);
  }

  // resting HR
  sectionLabel("Ruhepuls", L, 334);
  if (t.rhr > 0) {
    snprintf(buf, sizeof(buf), "%d", t.rhr);
    text(buf, L, 362, 32);
    if (m.rhrBaseline > 0) {
      int d = t.rhr - m.rhrBaseline;
      snprintf(buf, sizeof(buf), "%+d zur Basis %d", d, m.rhrBaseline);
      text(buf, R - 16, 362, 18, MR_DATUM);
      trendMark(R - 5, 362, (float)d, 1.5f);
    } else text("Basis ab 3 Nächten", R, 362, 18, MR_DATUM);
  } else text("--", L, 362, 32);

  // stress + steps
  sectionLabel("Stress", L, 396);
  if (t.stressAvg >= 0) {
    snprintf(buf, sizeof(buf), "%d", t.stressAvg);
    text(buf, L, 424, 32);
    const char* w = t.stressAvg < 26 ? "ruhig" : (t.stressAvg < 51 ? "mittel" : "hoch");
    if (t.steps >= 0) snprintf(buf, sizeof(buf), "%s • %ld Schritte", w, t.steps);
    else snprintf(buf, sizeof(buf), "%s", w);
    text(buf, R, 424, 18, MR_DATUM);
  } else text("--", L, 424, 32);

  // health check line
  int sy = 446;
  if (m.healthStatus == 2) {
    epaper.fillRoundRect(L, sy, R - L, 28, 6, TFT_BLACK);
    statusMark(L + 14, sy + 14, 2);
    text(m.healthText, L + 28, sy + 14, 18, ML_DATUM, true, R - L - 34);
  } else {
    statusMark(L + 8, sy + 14, m.healthStatus);
    text(m.healthText, L + 24, sy + 14, 18, ML_DATUM, false, R - L - 24);
  }

  // ---------------- right: running + weather
  const int X = COL_SPLIT + 16, RR = SCR_W - 16;
  sectionLabel("LAUFEN", X, 66);
  if (m.lastRunIdx >= 0) {
    if (m.daysSinceRun == 0) snprintf(buf, sizeof(buf), "letzter Lauf heute");
    else if (m.daysSinceRun == 1) snprintf(buf, sizeof(buf), "letzter Lauf gestern");
    else snprintf(buf, sizeof(buf), "letzter Lauf vor %d Tagen", m.daysSinceRun);
    text(buf, RR, 66, 18, MR_DATUM);
  }
  text(m.recTitle, X, 100, 32, ML_DATUM, false, RR - X);
  text(m.recReason, X, 130, 18, ML_DATUM, false, RR - X);
  if (m.windowText[0]) {
    text(m.windowText, X, 160, 24, ML_DATUM, false, RR - X);
    text(m.clothesText, X, 188, 18, ML_DATUM, false, RR - X);
  }
  hline(X, 206, RR - X);

  // weather now
  const Weather& w = D.wx;
  if (w.ok) {
    drawIcon64(X - 4, 214, w.codeNow);
    snprintf(buf, sizeof(buf), "%d°", (int)lroundf(w.tempNow));
    text(buf, X + 70, 236, 32);
    snprintf(buf, sizeof(buf), "gefühlt %d°", (int)lroundf(w.feelsNow));
    text(buf, X + 70, 266, 18);
    text(m.rainText, X, 300, 18);
    text(m.darkText, X, 324, 18);

    // next 15 hours: temperature line + rain probability bars
    const int gx = X + 196, gw = RR - gx, gy = 216, gh = 62, by = 288, bh = 34;
    int n = w.hourN < 15 ? w.hourN : 15;
    float tv[15], lo = 1e9, hi = -1e9;
    for (int i = 0; i < n; i++) { tv[i] = w.temp[i]; lo = fminf(lo, tv[i]); hi = fmaxf(hi, tv[i]); }
    if (hi - lo < 4) { float mid = (hi + lo) / 2; lo = mid - 2; hi = mid + 2; }
    lineChart(gx, gy, gw, gh, tv, n, lo, hi);
    snprintf(buf, sizeof(buf), "%d°", (int)lroundf(hi));
    text(buf, gx - 6, gy + 4, 18, MR_DATUM);
    snprintf(buf, sizeof(buf), "%d°", (int)lroundf(lo));
    text(buf, gx - 6, gy + gh - 4, 18, MR_DATUM);
    float slot = (float)gw / n;
    for (int i = 0; i < n; i++) {
      int bx = gx + (int)(i * slot) + 1, bwid = (int)slot - 2;
      int hh = (int)(w.prob[i] / 100.0f * bh);
      epaper.drawFastHLine(bx, by + bh, bwid, TFT_BLACK);
      if (hh > 0) { if (w.prob[i] >= 50) epaper.fillRect(bx, by + bh - hh, bwid, hh, TFT_BLACK); else shade(bx, by + bh - hh, bwid, hh); }
      if (i % 3 == 0) { snprintf(buf, sizeof(buf), "%02d", w.hourOfDay[i]); text(buf, bx, by + bh + 12, 18, ML_DATUM); }
    }
  } else {
    text("Wetter nicht verfügbar", X, 250, 24);
    if (D.weatherStatus[0]) text(D.weatherStatus, X, 280, 18, ML_DATUM, false, RR - X);
  }
  hline(X, 352, RR - X);

  // week
  sectionLabel("WOCHE", X, 370);
  snprintf(buf, sizeof(buf), "Fitness %d", (int)lroundf(m.ctl));
  text(buf, RR - 16, 370, 18, MR_DATUM);
  trendMark(RR - 5, 370, m.ctl - m.ctlWeekAgo, 0.5f);
  float km = m.weekKm[WEEKS_SHOWN - 1];
  fmtDec(b2, sizeof(b2), km, 1);
  snprintf(buf, sizeof(buf), "%s / %d km", b2, D.weeklyGoalKm);
  text(buf, X, 402, 24);
  // day dots
  const int dx0 = RR - 7 * 34 + 17;
  for (int d = 0; d < 7; d++) {
    int cx = dx0 + d * 34;
    text(WD_SHORT[d], cx, 388, 18, MC_DATUM);
    bool ran = m.runDaysThisWeek & (1 << d);
    if (ran) epaper.fillCircle(cx, 412, 7, TFT_BLACK);
    else epaper.drawCircle(cx, 412, 7, TFT_BLACK);
    if (d == m.todayWeekday) epaper.drawCircle(cx, 412, 10, TFT_BLACK);
  }
  progressBar(X, 426, RR - X, 14, km / (D.weeklyGoalKm > 0 ? D.weeklyGoalKm : 1));
  if (m.lastRunIdx >= 0) {
    const Activity& a = D.acts[m.lastRunIdx];
    char d[16], p[12], k[12];
    fmtDayShort(d, sizeof(d), a.day);
    fmtPace(p, sizeof(p), a.minutes * 60 / (a.km > 0 ? a.km : 1));
    fmtDec(k, sizeof(k), a.km, 1);
    if (a.avgHr > 0) snprintf(buf, sizeof(buf), "Zuletzt %s  %s km • %s /km • Puls %d", d, k, p, a.avgHr);
    else snprintf(buf, sizeof(buf), "Zuletzt %s  %s km • %s /km", d, k, p);
    text(buf, X, 460, 18, ML_DATUM, false, RR - X);
  } else text("Noch kein Lauf mit der Uhr", X, 460, 18);
}

// ================================================================= SCREEN 2
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

inline void drawHealth(const DashboardData& D, const struct tm& now, int batteryPct) {
  const Metrics& m = D.m;
  const HealthToday& t = D.today;
  char buf[64], b2[24];
  drawHeader(D, now, SCREEN_HEALTH, "Gesundheit • 14 Tage", batteryPct);
  const int L1 = 16, R1 = 384, L2 = 416, R2 = 784;
  long firstDay = D.days[0].day;
  vline(400, HEADER_H + 8, 334);

  // P1 body battery range per day
  panelTitle("Body Battery (Tief bis Hoch)", L1, 64);
  {
    const int y = 80, h = 100, n = HEALTH_DAYS;
    float slot = (float)(R1 - L1) / n;
    dottedH(L1, y, R1 - L1, 6); dottedH(L1, y + h / 2, R1 - L1, 6); hline(L1, y + h, R1 - L1);
    for (int i = 0; i < n; i++) {
      const HealthDay& d = D.days[i];
      if (d.bbMax < 0) continue;
      int bx = L1 + (int)(i * slot) + 4, bw = (int)slot - 8;
      int ytop = y + h - (int)(d.bbMax / 100.0f * h), ybot = y + h - (int)((d.bbMin < 0 ? 0 : d.bbMin) / 100.0f * h);
      epaper.fillRect(bx, ytop, bw, ybot - ytop > 2 ? ybot - ytop : 2, TFT_BLACK);
    }
    dayAxis(L1, R1 - L1, y + h + 14, n, firstDay);
  }

  // P2 sleep per night
  if (m.sleepAvgH > 0) { fmtHours(b2, sizeof(b2), m.sleepAvgH); snprintf(buf, sizeof(buf), "Schnitt %s • Linie 8 h", b2); }
  else snprintf(buf, sizeof(buf), "keine Nächte");
  panelTitle("Schlaf pro Nacht", L2, 64, buf, R2);
  {
    const int y = 80, h = 100, n = HEALTH_DAYS;
    const float maxH = 10;
    float slot = (float)(R2 - L2) / n;
    hline(L2, y + h, R2 - L2);
    int goalY = y + h - (int)(8 / maxH * h);
    dottedH(L2, goalY, R2 - L2, 3);
    for (int i = 0; i < n; i++) {
      const HealthDay& d = D.days[i];
      if (d.sleepH <= 0) continue;
      int bx = L2 + (int)(i * slot) + 4, bw = (int)slot - 8;
      int hh = (int)(fminf(d.sleepH, maxH) / maxH * h);
      epaper.fillRect(bx, y + h - hh, bw, hh, TFT_BLACK);
    }
    dayAxis(L2, R2 - L2, y + h + 14, n, firstDay);
  }
  hline(L1, 212, R2 - L1);

  // P3 resting HR
  if (m.rhrBaseline > 0) snprintf(buf, sizeof(buf), "Basis %d", m.rhrBaseline); else snprintf(buf, sizeof(buf), "Basis ab 3 Nächten");
  panelTitle("Ruhepuls", L1, 230, buf, R1);
  {
    const int y = 248, h = 96, n = HEALTH_DAYS;
    float v[HEALTH_DAYS], lo = 1e9, hi = -1e9; int cnt = 0;
    for (int i = 0; i < n; i++) {
      v[i] = D.days[i].rhr > 0 ? (float)D.days[i].rhr : NAN;
      if (!isnan(v[i])) { lo = fminf(lo, v[i]); hi = fmaxf(hi, v[i]); cnt++; }
    }
    if (cnt) {
      lo = floorf(lo - 3); hi = ceilf(hi + 3);
      float slot = (float)(R1 - L1 - 30) / n;
      int px = -1, py = -1;
      for (int i = 0; i < n; i++) {
        if (isnan(v[i])) continue;
        int cx = L1 + 30 + (int)(i * slot + slot / 2), cy = y + h - (int)((v[i] - lo) / (hi - lo) * h);
        if (px >= 0) thickLine(px, py, cx, cy);
        epaper.fillCircle(cx, cy, 4, TFT_BLACK);
        px = cx; py = cy;
      }
      if (m.rhrBaseline > 0) dottedH(L1 + 30, y + h - (int)((m.rhrBaseline - lo) / (hi - lo) * h), R1 - L1 - 30, 3);
      snprintf(buf, sizeof(buf), "%d", (int)hi); text(buf, L1, y + 6, 18);
      snprintf(buf, sizeof(buf), "%d", (int)lo); text(buf, L1, y + h - 6, 18);
    } else text("noch keine Werte", L1, y + h / 2, 24);
  }

  // P4 last night
  panelTitle("Letzte Nacht", L2, 230);
  if (t.sleepH > 0) {
    char a[12], b[12], d[16];
    fmtHours(d, sizeof(d), t.sleepH);
    if (t.sleepStartMin >= 0 && t.sleepEndMin >= 0) {
      snprintf(a, sizeof(a), "%02d:%02d", t.sleepStartMin / 60, t.sleepStartMin % 60);
      snprintf(b, sizeof(b), "%02d:%02d", t.sleepEndMin / 60, t.sleepEndMin % 60);
      snprintf(buf, sizeof(buf), "%s – %s  •  %s", a, b, d);
    } else snprintf(buf, sizeof(buf), "%s", d);
    text(buf, L2, 262, 24);
    float tot = t.deepH + t.lightH + t.remH + t.awakeH;
    int x = L2, bw = R2 - L2, y = 280, bh = 20;
    float parts[4] = {t.deepH, t.lightH, t.remH, t.awakeH};
    static const char* names[4] = {"Tief", "Leicht", "REM", "Wach"};
    for (int k = 0; k < 4; k++) {
      int pw = (int)lroundf(parts[k] / tot * bw);
      if (k == 3) pw = L2 + bw - x;
      if (k == 0) epaper.fillRect(x, y, pw, bh, TFT_BLACK);
      else if (k == 1) hatch(x, y, pw, bh);
      else if (k == 2) shade(x, y, pw, bh);
      x += pw;
    }
    epaper.drawRect(L2, y, bw, bh, TFT_BLACK);
    for (int k = 0; k < 4; k++) {
      int sx = L2 + (k % 2) * 130, sy = 308 + (k / 2) * 21;
      if (k == 0) epaper.fillRect(sx, sy, 12, 12, TFT_BLACK);
      else if (k == 1) { hatch(sx, sy, 12, 12); epaper.drawRect(sx, sy, 12, 12, TFT_BLACK); }
      else if (k == 2) { shade(sx, sy, 12, 12); epaper.drawRect(sx, sy, 12, 12, TFT_BLACK); }
      else epaper.drawRect(sx, sy, 12, 12, TFT_BLACK);
      snprintf(buf, sizeof(buf), "%s %d %%", names[k], (int)lroundf(parts[k] / tot * 100));
      text(buf, sx + 18, sy + 6, 18);
    }
    if (t.resp > 0) {
      fmtDec(b2, sizeof(b2), t.resp, 0);
      if (m.respBaseline > 0) { char c[12]; fmtDec(c, sizeof(c), m.respBaseline, 1); snprintf(buf, sizeof(buf), "Atmung %s /min (Basis %s)", b2, c); }
      else snprintf(buf, sizeof(buf), "Atmung %s /min", b2);
      text(buf, L2, 362, 18, ML_DATUM, false, R2 - L2);
    }
  } else text("keine Schlafdaten", L2, 270, 24);
  hline(L1, 376, R2 - L1);

  // bottom: stress, intensity minutes, check
  const int by = 394;
  sectionLabel("Stress heute", L1, by);
  if (t.stressAvg >= 0) { snprintf(buf, sizeof(buf), "%d", t.stressAvg); text(buf, L1, by + 34, 32); }
  else text("--", L1, by + 34, 32);
  int s7 = 0, s7n = 0;
  for (int i = HEALTH_DAYS - 7; i < HEALTH_DAYS; i++) if (D.days[i].stress >= 0) { s7 += D.days[i].stress; s7n++; }
  if (s7n) { snprintf(buf, sizeof(buf), "7 Tage %d", s7 / s7n); text(buf, L1 + 70, by + 38, 18); }

  sectionLabel("Intensität (Woche)", 230, by);
  snprintf(buf, sizeof(buf), "%d / 150", m.intensityWeek);
  text(buf, 230, by + 34, 32);
  progressBar(230, by + 58, 250, 12, m.intensityWeek / 150.0f);

  sectionLabel("Gesundheits-Check", 520, by);
  if (m.healthStatus == 2) {
    epaper.fillRoundRect(520, by + 18, R2 - 520, 34, 6, TFT_BLACK);
    statusMark(534, by + 35, 2);
    text(m.healthText, 548, by + 35, 18, ML_DATUM, true, R2 - 556);
  } else {
    statusMark(528, by + 35, m.healthStatus);
    text(m.healthText, 544, by + 35, 18, ML_DATUM, false, R2 - 544);
  }
  if (m.bedAvgMin >= 0) {
    int bm = (m.bedAvgMin + 720) % 1440;
    snprintf(buf, sizeof(buf), "Einschlafen meist %02d:%02d", bm / 60, bm % 60);
    text(buf, 520, by + 62, 18);
  }
}

// ================================================================= SCREEN 3
inline void drawTraining(const DashboardData& D, const struct tm& now, int batteryPct) {
  const Metrics& m = D.m;
  char buf[64], b2[24];
  drawHeader(D, now, SCREEN_TRAINING, "Training", batteryPct);
  const int L1 = 16, R1 = 384, L2 = 416, R2 = 784;
  vline(400, HEADER_H + 8, 322);

  // P1 form chart
  panelTitle("Form", L1, 64);
  {  // inline legend: solid = fitness, dashed = fatigue
    snprintf(buf, sizeof(buf), "Ermüdung %d", (int)lroundf(m.atl));
    int x2 = R1 - textW(buf, 18);
    text(buf, x2, 64, 18);
    dashedLine(x2 - 26, 64, x2 - 6, 64);
    snprintf(b2, sizeof(b2), "Fitness %d", (int)lroundf(m.ctl));
    int x1 = x2 - 40 - textW(b2, 18) - 14;
    text(b2, x1, 64, 18);
    trendMark(x1 + textW(b2, 18) + 9, 64, m.ctl - m.ctlWeekAgo, 0.5f);
    thickLine(x1 - 26, 64, x1 - 6, 64);
  }
  {
    const int y = 84, h = 96;
    float hi = 10;
    for (int i = 0; i < CHART_LOAD_DAYS; i++) hi = fmaxf(hi, fmaxf(m.ctlSeries[i], m.atlSeries[i]));
    hi = ceilf(hi / 10) * 10;
    dottedH(L1, y, R1 - L1, 6); hline(L1, y + h, R1 - L1);
    lineChart(L1, y, R1 - L1, h, m.ctlSeries, CHART_LOAD_DAYS, 0, hi);
    lineChart(L1, y, R1 - L1, h, m.atlSeries, CHART_LOAD_DAYS, 0, hi, true);
    snprintf(buf, sizeof(buf), "%d", (int)hi); text(buf, L1 + 2, y + 10, 18);
    text("vor 6 Wochen", L1, y + h + 14, 18);
    text("heute", R1, y + h + 14, 18, MR_DATUM);
  }

  // P2 weekly km
  snprintf(buf, sizeof(buf), "Ziel %d km", D.weeklyGoalKm);
  panelTitle("Wochen-km", L2, 64, buf, R2);
  {
    const int y = 92, h = 88, n = WEEKS_SHOWN;
    float hi = (float)D.weeklyGoalKm * 1.2f;
    for (int i = 0; i < n; i++) hi = fmaxf(hi, m.weekKm[i]);
    float slot = (float)(R2 - L2) / n;
    hline(L2, y + h, R2 - L2);
    int gy = y + h - (int)(D.weeklyGoalKm / hi * h);
    dottedH(L2, gy, R2 - L2, 3);
    for (int i = 0; i < n; i++) {
      int bx = L2 + (int)(i * slot) + 6, bw = (int)slot - 12;
      int hh = (int)(m.weekKm[i] / hi * h);
      if (hh > 0) { if (i == n - 1) shade(bx, y + h - hh, bw, hh); else epaper.fillRect(bx, y + h - hh, bw, hh, TFT_BLACK); }
      if (i == n - 1) epaper.drawRect(bx, y + h - hh, bw, hh > 0 ? hh : 1, TFT_BLACK);
      snprintf(buf, sizeof(buf), "%d", (int)lroundf(m.weekKm[i]));
      if (m.weekKm[i] > 0) text(buf, bx + bw / 2, y + h - hh - 10, 18, MC_DATUM);
      char iso[11]; isoFromDayNumber(m.weekStart[i], iso);
      snprintf(buf, sizeof(buf), "%d.%d", atoi(iso + 8), atoi(iso + 5));
      if (i % 2 == 1) text(buf, bx + bw / 2, y + h + 14, 18, MC_DATUM);
    }
  }
  hline(L1, 212, R2 - L1);

  // P3 last runs
  panelTitle("Letzte Läufe", L1, 230);
  {
    int y = 258, shown = 0;
    static const int cx[5] = {L1, L1 + 110, L1 + 180, L1 + 250, L1 + 310};
    text("km", cx[1] + 40, y, 18, MR_DATUM); text("Pace", cx[2] + 52, y, 18, MR_DATUM);
    text("Puls", cx[3] + 44, y, 18, MR_DATUM); text("TE", R1, y, 18, MR_DATUM);
    hline(L1, y + 12, R1 - L1);
    y += 30;
    for (int i = D.actN - 1; i >= 0 && shown < 4; i--) {
      const Activity& a = D.acts[i];
      if (!a.run) continue;
      fmtDayShort(buf, sizeof(buf), a.day); text(buf, cx[0], y, 18);
      fmtDec(b2, sizeof(b2), a.km, 1); text(b2, cx[1] + 40, y, 18, MR_DATUM);
      fmtPace(b2, sizeof(b2), a.minutes * 60 / (a.km > 0 ? a.km : 1)); text(b2, cx[2] + 52, y, 18, MR_DATUM);
      snprintf(b2, sizeof(b2), "%d", a.avgHr); text(a.avgHr ? b2 : "-", cx[3] + 44, y, 18, MR_DATUM);
      fmtDec(b2, sizeof(b2), a.te, 1); text(a.te > 0 ? b2 : "-", R1, y, 18, MR_DATUM);
      y += 26; shown++;
    }
    if (!shown) text("Noch kein Lauf mit der Uhr", L1, y, 18);
  }

  // P4 predictions
  panelTitle("Prognose", L2, 230, "aus Pace + Puls, 6 Wochen", R2);
  if (m.predN) {
    int y = 264;
    for (int k = 0; k < m.predN; k++) {
      text(m.pred[k].label, L2, y, 24);
      fmtDuration(buf, sizeof(buf), m.pred[k].seconds);
      text(buf, R2, y, 24, MR_DATUM);
      y += 28;
    }
  } else {
    text("Noch zu wenig Daten", L2, 270, 24);
    text("Ab dem ersten Lauf über 20 min", L2, 300, 18);
    text("mit Puls erscheinen hier", L2, 324, 18);
    text("Zeiten für 5 km bis Marathon.", L2, 348, 18);
  }
  hline(L1, 376, R2 - L1);

  // bottom: 3-day weather
  const Weather& w = D.wx;
  if (w.ok) {
    for (int k = 1; k < w.dayN && k <= 3; k++) {
      int x = L1 + (k - 1) * 260;
      drawIcon64(x, 390, w.dCode[k]);
      text(WD_LONG[weekdayMon0(w.dDay[k])], x + 74, 400, 24);
      snprintf(buf, sizeof(buf), "%d° / %d°", (int)lroundf(w.tMin[k]), (int)lroundf(w.tMax[k]));
      text(buf, x + 74, 430, 24);
      snprintf(buf, sizeof(buf), "Regen %d %%", w.pMax[k]);
      text(buf, x + 74, 458, 18);
    }
  } else text("Wetter nicht verfügbar", L1, 430, 24);
}

inline void drawScreen(int screen, const DashboardData& D, const struct tm& now, int batteryPct) {
  epaper.fillScreen(TFT_WHITE);
  if (screen == SCREEN_HEALTH) drawHealth(D, now, batteryPct);
  else if (screen == SCREEN_TRAINING) drawTraining(D, now, batteryPct);
  else drawOverview(D, now, batteryPct);
}
