/*
 * File: kit.h
 * Purpose: Draws a screen "spec" (JSON) on the 800x480 black/white panel.
 *
 * A spec is what the LLM coach answers with (coach.h): a nested layout of columns and rows
 * and, in every field, one element of a fixed kit (chart, number, text, ...). This code
 * decides nothing. It draws and collects warnings when something does not fit, so that the
 * coach can send them back to the LLM.
 *
 * Spec:  { "frame": {time, date, weather, today, next, week, energy} | "fullscreen": true,
 *          "header": {label, right, title}, "content": <node>, "sentence": "..." }
 * Node:  { "columns": [node...] } | { "rows": [node...] } | { "element": "<name>", ... }
 *        optional on every node: "weight", "box" (true | "thick"), "divider", "gap"
 *
 * Uses only plain drawing calls of the EPaper class (drawPixel, fillRect, drawString), so the
 * PC emulator draws the same code.
 */
#pragma once
#include <math.h>
#include <functional>
#include <string>
#include <vector>
#include <ArduinoJson.h>
#include "ui.h"

namespace kit {

typedef JsonVariantConst Node;
struct Box {
  float x0, y0, x1, y1;
  float w() const { return x1 - x0; }
  float h() const { return y1 - y0; }
};
struct Pt { float x, y; };
struct TextPos { float x, w; };

static const int W = 800, H = 480, PAD = 16;
enum Pattern { P_BLACK, P_GRAY, P_LIGHT, P_HATCHED, P_DOTS, P_HLINES, P_WHITE };

inline int lineH(int size) { return size >= 56 ? 62 : size >= 32 ? 40 : size >= 24 ? 30 : 23; }
inline int rnd(float v) { return (int)lroundf(v); }

// ----------------------------------------------------------------- JSON helpers
inline bool has(Node n, const char* key) { return !n[key].isNull(); }
inline bool isNum(Node n) { return n.is<float>() || n.is<long>(); }
// number of a node; text that is a number counts too ("8.06")
inline float num(Node n, float def = NAN) {
  if (isNum(n)) return n.as<float>();
  if (n.is<const char*>()) {
    const char* s = n.as<const char*>();
    char* end = nullptr;
    float v = strtof(s, &end);
    if (end != s && *end == 0) return v;
  }
  return def;
}
inline std::string str(Node n, const char* def = "") {
  if (n.is<const char*>()) return n.as<const char*>();
  if (n.is<bool>()) return n.as<bool>() ? "true" : "false";
  if (isNum(n)) {
    char b[32];
    float v = n.as<float>();
    if (v == floorf(v) && fabsf(v) < 1e9f) snprintf(b, sizeof(b), "%ld", (long)v);
    else snprintf(b, sizeof(b), "%.6g", (double)v);
    return b;
  }
  return def;
}
inline std::string str(Node n, const char* key, const char* def) { return str(n[key], def); }
// Python-like truth: missing, false, 0, "" and empty lists are false
inline bool truthy(Node n) {
  if (n.isNull()) return false;
  if (n.is<bool>()) return n.as<bool>();
  if (isNum(n)) return n.as<float>() != 0;
  if (n.is<const char*>()) return n.as<const char*>()[0] != 0;
  return n.size() > 0;
}
inline bool truthy(Node n, const char* key) { return truthy(n[key]); }
inline bool is(Node n, const char* key, const char* value) { return str(n[key]) == value; }
inline int count(Node n) { return (int)n.size(); }

inline Pattern pat(const std::string& name, Pattern def = P_BLACK) {
  static const char* names[] = {"black", "gray", "light", "hatched", "dots", "hlines", "white"};
  for (int i = 0; i < 7; i++) if (name == names[i]) return (Pattern)i;
  return def;
}
inline Pattern pat(Node n, const char* key, Pattern def = P_BLACK) { return pat(str(n[key]), def); }
inline bool patTest(Pattern p, int x, int y) {
  switch (p) {
    case P_BLACK: return true;
    case P_GRAY: return (x + y) % 2 == 0;
    case P_LIGHT: return x % 2 == 0 && y % 2 == 0;
    case P_HATCHED: return (x + y) % 6 < 2;
    case P_DOTS: return x % 5 < 2 && y % 5 < 2;
    case P_HLINES: return y % 4 == 0;
    default: return false;
  }
}

// Formats a number: "m:ss" (seconds), "h:mm" and "clock" (hours), or a pattern such as
// "{:.1f}", "{:+.0f} %", "{}". Without a pattern: no decimals.
inline std::string fmt(float v, const std::string& f = "") {
  char b[48];
  if (f == "m:ss") { long s = lroundf(v); snprintf(b, sizeof(b), "%ld:%02ld", s / 60, s % 60); return b; }
  if (f == "h:mm") { long m = lroundf(v * 60); snprintf(b, sizeof(b), "%ld:%02ld", m / 60, m % 60); return b; }
  if (f == "clock") { long m = ((lroundf(v * 60) % 1440) + 1440) % 1440; snprintf(b, sizeof(b), "%ld:%02ld", m / 60, m % 60); return b; }
  size_t open = f.find('{'), close = f.find('}');
  if (f.empty() || open == std::string::npos || close == std::string::npos || close < open) { snprintf(b, sizeof(b), "%.0f", (double)v); return b; }
  std::string spec = f.substr(open + 1, close - open - 1);
  bool plus = spec.find('+') != std::string::npos;
  size_t dot = spec.find('.');
  if (spec.empty() || spec == ":") {
    if (v == floorf(v)) snprintf(b, sizeof(b), "%ld", (long)v); else snprintf(b, sizeof(b), "%.6g", (double)v);
  } else if (spec.find('d') != std::string::npos) {
    snprintf(b, sizeof(b), plus ? "%+ld" : "%ld", lroundf(v));
  } else {
    int dec = dot != std::string::npos ? atoi(spec.c_str() + dot + 1) : 6;
    snprintf(b, sizeof(b), plus ? "%+.*f" : "%.*f", dec, (double)v);
  }
  return f.substr(0, open) + b + f.substr(close + 1);
}

// ----------------------------------------------------------------- canvas
struct Canvas {
  std::vector<std::string> warn;
  struct Lab { float x0, y0, x1, y1; std::string t; };
  std::vector<Lab> labels;

  void warning(const std::string& w) { warn.push_back(w); }

  // --- pixels and shapes. Coordinates are pixels; rectangles include both corners.
  void px(int x, int y, bool black = true) { if (x >= 0 && y >= 0 && x < W && y < H) epaper.drawPixel(x, y, black ? TFT_BLACK : TFT_WHITE); }
  void block(float x0, float y0, float x1, float y1, bool black = true) {
    int a = rnd(fminf(x0, x1)), b = rnd(fminf(y0, y1)), c = rnd(fmaxf(x0, x1)), d = rnd(fmaxf(y0, y1));
    a = a < 0 ? 0 : a; b = b < 0 ? 0 : b; c = c >= W ? W - 1 : c; d = d >= H ? H - 1 : d;
    if (c >= a && d >= b) epaper.fillRect(a, b, c - a + 1, d - b + 1, black ? TFT_BLACK : TFT_WHITE);
  }
  void rect(Box b, int width = 1) {   // outline, drawn inwards
    for (int i = 0; i < width; i++) {
      block(b.x0 + i, b.y0 + i, b.x1 - i, b.y0 + i); block(b.x0 + i, b.y1 - i, b.x1 - i, b.y1 - i);
      block(b.x0 + i, b.y0 + i, b.x0 + i, b.y1 - i); block(b.x1 - i, b.y0 + i, b.x1 - i, b.y1 - i);
    }
  }
  void polygon(const std::vector<Pt>& pts, bool black = true) { scan(pts, [&](int x, int y) { px(x, y, black); }); }
  void line(float x0, float y0, float x1, float y1, int width = 1, bool black = true) {
    if (width <= 1) {
      int ax = rnd(x0), ay = rnd(y0), bx = rnd(x1), by = rnd(y1);
      int dx = abs(bx - ax), sx = ax < bx ? 1 : -1, dy = -abs(by - ay), sy = ay < by ? 1 : -1, e = dx + dy;
      for (;;) {
        px(ax, ay, black);
        if (ax == bx && ay == by) break;
        int e2 = 2 * e;
        if (e2 >= dy) { e += dy; ax += sx; }
        if (e2 <= dx) { e += dx; ay += sy; }
      }
      return;
    }
    float len = hypotf(x1 - x0, y1 - y0), h = width / 2.0f;
    if (len < 0.5f) { block(x0 - h, y0 - h, x0 + h - 1, y0 + h - 1, black); return; }
    if (rnd(y0) == rnd(y1)) { block(x0, y0 - (width - 1) / 2, x1, y0 - (width - 1) / 2 + width - 1, black); return; }
    if (rnd(x0) == rnd(x1)) { block(x0 - (width - 1) / 2, y0, x0 - (width - 1) / 2 + width - 1, y1, black); return; }
    float nx = -(y1 - y0) / len * h, ny = (x1 - x0) / len * h;
    polygon({{x0 + nx, y0 + ny}, {x1 + nx, y1 + ny}, {x1 - nx, y1 - ny}, {x0 - nx, y0 - ny}}, black);
  }
  void polyline(const std::vector<Pt>& pts, int width = 1, bool closed = false) {
    for (size_t i = 0; i + 1 < pts.size(); i++) line(pts[i].x, pts[i].y, pts[i + 1].x, pts[i + 1].y, width);
    if (closed && pts.size() > 2) line(pts.back().x, pts.back().y, pts[0].x, pts[0].y, width);
    if (width >= 3) for (auto& p : pts) disc(p.x, p.y, width / 2.0f);
  }
  // fill: 0 = outline only, 1 = black, 2 = white inside with black outline, 3 = white without outline
  void ellipse(float x0, float y0, float x1, float y1, int fill = 0, int width = 1) {
    float cx = (x0 + x1) / 2, cy = (y0 + y1) / 2, rx = (x1 - x0) / 2 + 0.5f, ry = (y1 - y0) / 2 + 0.5f;
    if (rx <= 0 || ry <= 0) return;
    float ix = rx - width, iy = ry - width;
    for (int y = (int)floorf(y0); y <= (int)ceilf(y1); y++)
      for (int x = (int)floorf(x0); x <= (int)ceilf(x1); x++) {
        float dx = x - cx, dy = y - cy;
        if (dx * dx / (rx * rx) + dy * dy / (ry * ry) > 1) continue;
        bool inner = ix > 0 && iy > 0 && dx * dx / (ix * ix) + dy * dy / (iy * iy) <= 1;
        if (fill == 1) px(x, y);
        else if (fill == 3) px(x, y, false);
        else if (!inner) px(x, y);
        else if (fill == 2) px(x, y, false);
      }
  }
  void disc(float cx, float cy, float r, bool black = true) { ellipse(cx - r, cy - r, cx + r, cy + r, black ? 1 : 3); }
  void circle(float cx, float cy, float r, int width = 1) { ellipse(cx - r, cy - r, cx + r, cy + r, 0, width); }

  // --- pattern fills. They overwrite: where the pattern has no dot the area turns white.
  void fill(Box b, Pattern p) {
    int x0 = rnd(b.x0), y0 = rnd(b.y0), x1 = rnd(b.x1), y1 = rnd(b.y1);
    x0 = x0 < 0 ? 0 : x0; y0 = y0 < 0 ? 0 : y0; x1 = x1 > W ? W : x1; y1 = y1 > H ? H : y1;
    if (x1 <= x0 || y1 <= y0) return;
    if (p == P_BLACK || p == P_WHITE) { epaper.fillRect(x0, y0, x1 - x0, y1 - y0, p == P_BLACK ? TFT_BLACK : TFT_WHITE); return; }
    for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) px(x, y, patTest(p, x, y));
  }
  void fillPoly(const std::vector<Pt>& pts, Pattern p) { scan(pts, [&](int x, int y) { px(x, y, patTest(p, x, y)); }); }
  // every pixel inside a polygon (even-odd rule, pixel centres)
  void scan(const std::vector<Pt>& pts, const std::function<void(int, int)>& put) {
    if (pts.size() < 3) return;
    float lo = pts[0].y, hi = pts[0].y;
    for (auto& p : pts) { lo = fminf(lo, p.y); hi = fmaxf(hi, p.y); }
    std::vector<float> xs;
    for (int y = (int)floorf(lo); y <= (int)ceilf(hi); y++) {
      if (y < 0 || y >= H) continue;
      xs.clear();
      for (size_t i = 0; i < pts.size(); i++) {
        Pt a = pts[i], b = pts[(i + 1) % pts.size()];
        if ((a.y <= y && b.y > y) || (b.y <= y && a.y > y)) xs.push_back(a.x + (y - a.y) / (b.y - a.y) * (b.x - a.x));
      }
      std::sort(xs.begin(), xs.end());
      for (size_t i = 0; i + 1 < xs.size(); i += 2)
        for (int x = (int)ceilf(xs[i] - 0.5f); x <= (int)floorf(xs[i + 1] + 0.5f); x++) put(x, y);
    }
  }
  void hdots(float x0, float x1, float y, int step = 4) { for (int x = rnd(x0); x < rnd(x1); x += step) px(x, rnd(y)); }
  void vdots(float x, float y0, float y1, int step = 4) { for (int y = rnd(y0); y < rnd(y1); y += step) px(rnd(x), y); }
  void hdash(float x0, float x1, float y, int on = 8, int off = 5) {
    for (float x = rnd(x0); x < x1; x += on + off) block(x, y - 1, fminf(x + on, x1), y);
  }
  void check(float x, float y, float s = 14, bool black = true) {
    line(x, y + s * 0.55f, x + s * 0.38f, y + s * 0.9f, 2, black);
    line(x + s * 0.38f, y + s * 0.9f, x + s, y + s * 0.1f, 2, black);
  }
  void arrow(float x0, float y0, float x1, float y1) {
    line(x0, y0, x1, y1, 2);
    float a = atan2f(y1 - y0, x1 - x0);
    polygon({{x1, y1}, {x1 - 9 * cosf(a + 0.45f), y1 - 9 * sinf(a + 0.45f)}, {x1 - 9 * cosf(a - 0.45f), y1 - 9 * sinf(a - 0.45f)}});
  }

  // --- text. Sizes 18, 24, 32, 56. The fonts have one weight per size; "bold" at 18 and 24
  // is drawn by printing the text twice, one pixel apart.
  static bool fake(int size, bool bold) { return bold && size < 32; }
  float tw(const std::string& t, int size = 18, bool bold = false) { return t.empty() ? 0 : textW(plain(t).c_str(), size) + (fake(size, bold) ? 1 : 0); }
  // The fonts hold Latin letters only. Characters an LLM likes to write are replaced by plain ones.
  static std::string plain(const std::string& t) {
    static const char* MAP[][2] = {{"\xC3\x97", "x"}, {"\xE2\x88\x92", "-"}, {"\xE2\x80\x93", "-"}, {"\xE2\x80\x94", "-"}, {"\xE2\x80\x9C", "\""},
                                   {"\xE2\x80\x9D", "\""}, {"\xE2\x80\x9E", "\""}, {"\xE2\x80\x98", "'"}, {"\xE2\x80\x99", "'"}, {"\xE2\x80\xA6", "..."},
                                   {"\xE2\x89\x88", "~"}, {"\xE2\x86\x92", "->"}, {"\xE2\x80\xA2", "-"}, {"\xC2\xB7", "-"}, {"\xC2\xA0", " "}};
    if (t.find_first_of("\xC2\xC3\xE2") == std::string::npos) return t;
    std::string out = t;
    for (auto& m : MAP) for (size_t p; (p = out.find(m[0])) != std::string::npos;) out.replace(p, strlen(m[0]), m[1]);
    return out;
  }
  void put(const std::string& t, float x, float y, int size, bool bold, bool white) {
    useFont(size);
    epaper.setTextDatum(TL_DATUM);
    if (white) epaper.setTextColor(TFT_WHITE, TFT_BLACK); else epaper.setTextColor(TFT_BLACK, TFT_WHITE);
    static const int dy[] = {6, 8, 10, 17};   // the font's line box starts higher than the letters
    int top = rnd(y) + dy[size >= 56 ? 3 : size >= 32 ? 2 : size >= 24 ? 1 : 0];
    std::string p = plain(t);
    epaper.drawString(p.c_str(), rnd(x), top);
    if (fake(size, bold)) epaper.drawString(p.c_str(), rnd(x) + 1, top);
  }
  // y is the top edge of the line. anchor 'l', 'm' or 'r'. maxw > 0 shortens the text and warns.
  // lo/hi keep the text inside a horizontal range.
  TextPos text(float x, float y, std::string t, int size = 18, bool bold = false, char anchor = 'l', bool white = false,
               float maxw = -1, float lo = NAN, float hi = NAN) {
    if (maxw >= 0 && tw(t, size, bold) > maxw + 1) {
      warning("text too long: \"" + t + "\"");
      while (!t.empty() && tw(t + "...", size, bold) > maxw) {
        do { t.pop_back(); } while (!t.empty() && ((unsigned char)t.back() & 0xC0) == 0x80);
      }
      t += "...";
    }
    float w = tw(t, size, bold);
    if (anchor == 'm') x -= w / 2; else if (anchor == 'r') x -= w;
    if (!isnan(hi)) x = fminf(x, hi - w);
    if (!isnan(lo)) x = fmaxf(x, lo);
    put(t, x, y, size, bold, white);
    return {(float)rnd(x), w};
  }
  // Small label on a white background, so that it stays readable on top of lines. Warns on overlap.
  TextPos label(float x, float y, const std::string& t, char anchor = 'm', float lo = NAN, float hi = NAN, bool bold = false) {
    float w = tw(t, 18, bold);
    float x0 = x - (anchor == 'm' ? w / 2 : anchor == 'r' ? w : 0);
    if (!isnan(hi)) x0 = fminf(x0, hi - w);
    if (!isnan(lo)) x0 = fmaxf(x0, lo);
    for (auto& l : labels)
      if (x0 < l.x1 && l.x0 < x0 + w && y < l.y1 && l.y0 < y + 20) warning("labels overlap: \"" + l.t + "\" and \"" + t + "\"");
    labels.push_back({x0, y, x0 + w, y + 20, t});
    block(x0 - 2, y + 3, x0 + w + 2, y + 22, false);
    put(t, x0, y, 18, bold, false);
    return {x0, w};
  }
  std::vector<std::string> wrap(const std::string& t, int size, float maxw, bool bold = false) {
    std::vector<std::string> lines;
    size_t start = 0;
    for (;;) {
      size_t nl = t.find('\n', start);
      std::string para = t.substr(start, nl == std::string::npos ? std::string::npos : nl - start), cur;
      size_t i = 0;
      while (i < para.size()) {
        while (i < para.size() && para[i] == ' ') i++;
        size_t j = i;
        while (j < para.size() && para[j] != ' ') j++;
        if (j == i) break;
        std::string piece = para.substr(i, j - i);
        if (!cur.empty() && tw(cur + " " + piece, size, bold) > maxw) { lines.push_back(cur); cur = piece; }
        else cur = cur.empty() ? piece : cur + " " + piece;
        i = j;
      }
      lines.push_back(cur);
      if (nl == std::string::npos) break;
      start = nl + 1;
    }
    return lines;
  }
};

// Small title above an element; returns the box that is left below it.
inline Box title(Canvas& c, Node node, Box box) {
  if (!truthy(node, "title")) return box;
  c.text(box.x0, box.y0 - 2, str(node["title"]), 18, false, 'l', false, box.w());
  return {box.x0, box.y0 + 24, box.x1, box.y1};
}
// Point on a circle: 0 degrees is at the top, clockwise.
inline Pt pol(float cx, float cy, float r, float deg) {
  float t = (deg - 90) * (float)M_PI / 180;
  return {cx + r * cosf(t), cy + r * sinf(t)};
}
// Ring segment between two radii and two angles as a polygon.
inline std::vector<Pt> sector(float cx, float cy, float r0, float r1, float a0, float a1) {
  std::vector<Pt> pts;
  int n = (int)fmaxf(2, fabsf(a1 - a0) / 3);
  for (int i = 0; i <= n; i++) pts.push_back(pol(cx, cy, r1, a0 + (a1 - a0) * i / n));
  for (int i = n; i >= 0; i--) pts.push_back(pol(cx, cy, r0, a0 + (a1 - a0) * i / n));
  return pts;
}

// ----------------------------------------------------------------- charts: shared parts
typedef std::function<float(float)> Scale;

// Index of a data point: a number, or one of the texts in node["x"].
inline int indexOf(Node node, Node key) {
  if (key.is<const char*>()) {
    int i = 0;
    for (Node x : node["x"].as<JsonArrayConst>()) { if (str(x) == key.as<const char*>()) return i; i++; }
    return 0;
  }
  return (int)num(key, 0);
}
inline int marksTop(Node marks) {
  for (Node m : marks.as<JsonArrayConst>()) {
    std::string t = str(m["type"]);
    if ((t == "circle" || t == "range" || t == "event" || t == "arrow") && truthy(m, "text")) return 26;
  }
  return 8;
}
// Which x labels to print so that they do not run into each other (always the last one).
inline std::vector<int> pickLabels(Canvas& c, Node labels, Node show, const Scale& fx, int n) {
  std::vector<int> out;
  if (!show.isNull()) { for (Node i : show.as<JsonArrayConst>()) out.push_back((int)num(i, 0)); return out; }
  if (count(labels) == 0) return out;
  float widest = 0;
  for (Node l : labels.as<JsonArrayConst>()) widest = fmaxf(widest, c.tw(str(l)));
  float dx = n > 1 ? fabsf(fx(1) - fx(0)) : 999;
  int step = (int)fmaxf(1, ceilf((widest + 12) / fmaxf(dx, 1)));
  for (int i = n - 1; i >= 0; i -= step) if (i < count(labels)) out.push_back(i);
  return out;
}
struct Axis { std::vector<float> ticks; std::vector<std::string> labels; Box plot; };
inline Axis yAxis(Canvas& c, Node y, float vmin, float vmax, Box box, float top) {
  Axis a;
  std::string f = str(y["format"]);
  if (has(y, "marks")) { for (Node t : y["marks"].as<JsonArrayConst>()) a.ticks.push_back(num(t, 0)); }
  else a.ticks = {vmin, vmax};
  float left = 0;
  for (size_t i = 0; i < a.ticks.size(); i++) {
    std::string l = fmt(a.ticks[i], f);
    if (truthy(y, "unit") && i == a.ticks.size() - 1) l += " " + str(y["unit"]);
    a.labels.push_back(l);
    left = fmaxf(left, c.tw(l));
  }
  a.plot = {box.x0 + left + 8, box.y0 + top, box.x1 - 6, box.y1 - 26};
  return a;
}
struct LegendItem { std::string name; std::function<void(float, float)> draw; };
// Right-aligned in the top line of the box.
inline void legend(Canvas& c, const std::vector<LegendItem>& items, Box box) {
  float x = box.x1;
  for (int i = (int)items.size() - 1; i >= 0; i--) {
    x -= c.tw(items[i].name);
    c.text(x, box.y0 - 2, items[i].name);
    x -= 30;
    items[i].draw(x, box.y0 + 10);
    x -= 14;
  }
}
// Annotations of line and bars. phase 0 is drawn below the data, phase 1 on top.
inline void drawMarks(Canvas& c, Node node, Node marks, int phase, Box plot, Box box, const Scale& fx, const Scale& fy, float half,
                      const std::function<float(int, Node)>& valueAt) {
  for (Node m : marks.as<JsonArrayConst>()) {
    std::string art = str(m["type"]), text = str(m["text"]);
    if (phase == 0) {
      if (art == "band") {
        float ya = fy(num(m["from"], 0)), yb = fy(num(m["to"], 0));
        c.fill({plot.x0, fminf(ya, yb), plot.x1, fmaxf(ya, yb)}, pat(m, "pattern", P_LIGHT));
      }
      if (art == "range" && truthy(m, "pattern"))
        c.fill({fx(indexOf(node, m["from"])) - half, plot.y0, fx(indexOf(node, m["to"])) + half, plot.y1}, pat(m, "pattern"));
      continue;
    }
    if (art == "threshold") {
      float y = fy(num(m["value"], 0));
      c.hdash(plot.x0, plot.x1, y);
      if (!text.empty() && !truthy(m, "legend")) c.label(plot.x1 - 2, is(m, "position", "below") ? y + 2 : y - 24, text, 'r');
    } else if (art == "range") {
      float xa = fx(indexOf(node, m["from"])) - half, xb = fx(indexOf(node, m["to"])) + half, y = plot.y0 - 4;
      c.polyline({{xa, y + 6}, {xa, y}, {xb, y}, {xb, y + 6}}, 2);
      if (!text.empty()) c.label((xa + xb) / 2, y - 24, text, 'm', box.x0, box.x1);
    } else if (art == "event") {
      float x = fx(indexOf(node, m["at"]));
      c.vdots(x, plot.y0, plot.y1, 3);
      c.polygon({{x - 5, plot.y0 - 6}, {x + 5, plot.y0 - 6}, {x, plot.y0 + 1}});
      if (!text.empty()) c.label(x, plot.y0 - 28, text, 'm', box.x0, box.x1);
    } else if (art == "circle" || art == "arrow") {
      int i = indexOf(node, m["at"]);
      float x = fx(i), y = fy(valueAt(i, m));
      if (art == "circle") {
        float r = num(m["radius"], 10);
        c.circle(x, y, r, 2);
        if (!text.empty()) c.label(x, is(m, "position", "below") ? y + r + 2 : y - r - 24, text, 'm', box.x0, box.x1, true);
      } else {
        float dx = num(m["dx"], 40), dy = num(m["dy"], -34), tx = x + dx, ty = y + dy;
        c.arrow(tx, ty, x + (dx > 0 ? 4 : -4), y + (dy < 0 ? -4 : 4));
        if (!text.empty()) c.label(tx + (dx > 0 ? 4 : -4), dy < 0 ? ty - 22 : ty, text, dx > 0 ? 'l' : 'r', box.x0, box.x1, true);
      }
    }
  }
}
// The values of a list as numbers, NAN for null.
inline std::vector<float> floats(Node list) {
  std::vector<float> v;
  for (Node x : list.as<JsonArrayConst>()) v.push_back(num(x));
  return v;
}

// ----------------------------------------------------------------- elements: line, bars
inline void el_line(Canvas& c, Node node, Box box) {
  box = title(c, node, box);
  Node rows = node["series"], marks = node["annotate"], y = node["y"];
  std::vector<std::vector<float>> series;
  std::vector<float> vals;
  size_t n = 0;
  for (Node r : rows.as<JsonArrayConst>()) {
    series.push_back(floats(r["values"]));
    n = std::max(n, series.back().size());
    for (float v : series.back()) if (!isnan(v)) vals.push_back(v);
  }
  if (series.empty() || vals.empty()) { c.text(box.x0, box.y0, "no data"); return; }
  std::vector<Node> bands;
  for (Node m : marks.as<JsonArrayConst>()) {
    if (is(m, "type", "threshold")) vals.push_back(num(m["value"], 0));
    if (is(m, "type", "band")) { vals.push_back(num(m["from"], 0)); vals.push_back(num(m["to"], 0)); if (truthy(m, "text")) bands.push_back(m); }
  }
  float lo = *std::min_element(vals.begin(), vals.end()), hi = *std::max_element(vals.begin(), vals.end());
  float span = hi - lo != 0 ? hi - lo : 1;
  float vmin = num(y["min"], lo - span * 0.1f), vmax = num(y["max"], hi + span * 0.1f);
  bool hasLegend = series.size() > 1 || !bands.empty();
  Axis ax = yAxis(c, y, vmin, vmax, box, marksTop(marks) + (hasLegend ? 24 : 0));
  Box p = ax.plot;
  bool flip = truthy(y, "reversed");
  Scale fx = [&](float i) { return p.x0 + 6 + (p.w() - 12) * (n > 1 ? i / (n - 1) : 0.5f); };
  Scale fy = [&](float v) { float t = (v - vmin) / (vmax - vmin); return flip ? p.y0 + p.h() * t : p.y1 - p.h() * t; };
  auto valueAt = [&](int i, Node m) -> float {
    if (has(m, "value")) return num(m["value"], 0);
    size_t s = (size_t)num(m["series_index"], 0);
    return s < series.size() && i >= 0 && i < (int)series[s].size() && !isnan(series[s][i]) ? series[s][i] : vmin;
  };
  drawMarks(c, node, marks, 0, p, box, fx, fy, 6, valueAt);
  if (truthy(node, "area")) {
    std::vector<Pt> pts;
    for (size_t i = 0; i < series[0].size(); i++) if (!isnan(series[0][i])) pts.push_back({fx(i), fy(series[0][i])});
    if (pts.size() > 1) {
      float base = flip ? p.y0 : p.y1;
      pts.insert(pts.begin(), {pts[0].x, base});
      pts.push_back({pts.back().x, base});
      c.fillPoly(pts, node["area"].is<const char*>() ? pat(node, "area", P_LIGHT) : P_LIGHT);
    }
  }
  for (size_t i = 0; i < ax.ticks.size(); i++) {
    c.hdots(p.x0, p.x1, fy(ax.ticks[i]));
    c.text(p.x0 - 6, fy(ax.ticks[i]) - 12, ax.labels[i], 18, false, 'r');
  }
  c.line(p.x0, p.y1, p.x1, p.y1);
  int k = 0;
  for (Node r : rows.as<JsonArrayConst>()) {
    std::string style = str(r, "style", "thick");
    std::vector<Pt> seg;
    const std::vector<float>& v = series[k++];
    for (size_t i = 0; i <= v.size(); i++) {
      if (i == v.size() || isnan(v[i])) {
        if (seg.size() > 1) {
          if (style == "dotted") {
            for (size_t j = 0; j + 1 < seg.size(); j++) {
              int steps = (int)fmaxf(1, hypotf(seg[j + 1].x - seg[j].x, seg[j + 1].y - seg[j].y) / 5);
              for (int s = 0; s < steps; s++) {
                float x = seg[j].x + (seg[j + 1].x - seg[j].x) * s / steps, yy = seg[j].y + (seg[j + 1].y - seg[j].y) * s / steps;
                c.block(x, yy, x + 1, yy + 1);
              }
            }
          } else c.polyline(seg, style == "thick" ? 3 : 1);
        } else if (seg.size() == 1) c.disc(seg[0].x, seg[0].y, 2);
        seg.clear();
      } else {
        seg.push_back({fx(i), fy(v[i])});
        if (truthy(r, "points")) c.disc(fx(i), fy(v[i]), 4);
      }
    }
  }
  Node xl = node["x"];
  for (int i : pickLabels(c, xl, node["x_show"], fx, (int)n)) {
    c.line(fx(i), p.y1, fx(i), p.y1 + 4);
    c.text(fx(i), p.y1 + 3, str(xl[i]), 18, false, 'm', false, -1, box.x0, box.x1);
  }
  drawMarks(c, node, marks, 1, p, box, fx, fy, 6, valueAt);
  std::vector<LegendItem> items;
  if (series.size() > 1)
    for (Node r : rows.as<JsonArrayConst>()) {
      std::string style = str(r, "style", "thick");
      items.push_back({str(r["name"]), [&c, style](float x, float yy) {
        if (style == "dotted") { for (int s = 0; s < 24; s += 5) c.block(x + s, yy, x + s + 1, yy + 1); }
        else c.line(x, yy, x + 24, yy, style == "thick" ? 3 : 1);
      }});
    }
  for (Node m : bands) {
    Pattern pt = pat(m, "pattern", P_LIGHT);
    items.push_back({str(m["text"]), [&c, pt](float x, float yy) { c.fill({x + 2, yy - 7, x + 24, yy + 7}, pt); }});
  }
  if (!items.empty()) legend(c, items, box);
}

inline void el_bars(Canvas& c, Node node, Box box) {
  box = title(c, node, box);
  Node marks = node["annotate"], y = node["y"];
  struct Stack { std::string name; Pattern pattern; std::vector<float> v; };
  std::vector<Stack> stacks;
  if (truthy(node, "stacks")) { for (Node s : node["stacks"].as<JsonArrayConst>()) stacks.push_back({str(s["name"]), pat(s, "pattern"), floats(s["values"])}); }
  else stacks.push_back({"", pat(node, "pattern"), floats(node["values"])});
  int n = (int)stacks[0].v.size();
  if (n == 0) { c.text(box.x0, box.y0, "no data"); return; }
  std::vector<float> totals(n, 0);
  for (auto& s : stacks) for (int i = 0; i < n && i < (int)s.v.size(); i++) if (!isnan(s.v[i])) totals[i] += s.v[i];
  float top = *std::max_element(totals.begin(), totals.end());
  std::vector<Node> lines;
  for (Node m : marks.as<JsonArrayConst>())
    if (is(m, "type", "threshold")) { top = fmaxf(top, num(m["value"], 0)); if (truthy(m, "legend") && truthy(m, "text")) lines.push_back(m); }
  float vmin = num(y["min"], 0), vmax = num(y["max"], top * 1.12f != 0 ? top * 1.12f : 1);
  float head = fmaxf(marksTop(marks), truthy(node, "show_values") ? 22 : 0) + (stacks.size() > 1 || !lines.empty() ? 24 : 0);
  Axis ax = yAxis(c, y, vmin, vmax, box, head);
  Box p = ax.plot;
  float band = p.w() / n, bw = fmaxf(3, fminf(band * 0.72f, 60));
  Scale fx = [&](float i) { return p.x0 + band * (i + 0.5f); };
  Scale fy = [&](float v) { return p.y1 - p.h() * (v - vmin) / (vmax - vmin); };
  auto valueAt = [&](int i, Node m) -> float { return has(m, "value") ? num(m["value"], 0) : (i >= 0 && i < n ? totals[i] : 0); };
  drawMarks(c, node, marks, 0, p, box, fx, fy, band / 2, valueAt);
  for (size_t i = 0; i < ax.ticks.size(); i++) {
    if (ax.ticks[i] != vmin) c.hdots(p.x0, p.x1, fy(ax.ticks[i]));
    c.text(p.x0 - 6, fy(ax.ticks[i]) - 12, ax.labels[i], 18, false, 'r');
  }
  auto inList = [&](Node list, int i) { for (Node k : list.as<JsonArrayConst>()) if ((int)num(k, -1) == i) return true; return false; };
  std::string vf = has(node, "value_format") ? str(node["value_format"]) : str(y["format"]);
  for (int i = 0; i < n; i++) {
    float base = 0;
    for (auto& s : stacks) {
      float v = i < (int)s.v.size() && !isnan(s.v[i]) ? s.v[i] : 0;
      if (v == 0 || base + v <= vmin) { base += v; continue; }
      Pattern pt = s.pattern;
      if (has(node, "highlight") && stacks.size() == 1) pt = inList(node["highlight"], i) ? P_BLACK : P_GRAY;
      Box b = {fx(i) - bw / 2, fy(base + v), fx(i) + bw / 2, fy(fmaxf(base, vmin))};
      c.fill(b, pt);
      c.rect({(float)rnd(b.x0), (float)rnd(b.y0), (float)rnd(b.x1), (float)rnd(b.y1)});
      base += v;
    }
    if (inList(node["missing"], i)) c.text(fx(i), p.y1 - 24, "?", 18, false, 'm');
    if (truthy(node, "show_values") && totals[i] != 0) c.text(fx(i), fy(totals[i]) - 23, fmt(totals[i], vf), 18, false, 'm');
  }
  c.line(p.x0, p.y1, p.x1, p.y1);
  Node xl = node["x"];
  for (int i : pickLabels(c, xl, node["x_show"], fx, n)) c.text(fx(i), p.y1 + 3, str(xl[i]), 18, false, 'm', false, -1, box.x0, box.x1);
  drawMarks(c, node, marks, 1, p, box, fx, fy, band / 2, valueAt);
  std::vector<LegendItem> items;
  if (stacks.size() > 1) {
    for (auto& s : stacks) {
      Pattern pt = s.pattern;
      items.push_back({s.name, [&c, pt](float x, float yy) { c.fill({x + 4, yy - 7, x + 24, yy + 7}, pt); c.rect({x + 4, yy - 7, x + 24, yy + 7}); }});
    }
  } else for (Node m : lines) items.push_back({str(m["text"]), [&c](float x, float yy) { c.hdash(x, x + 24, yy); }});
  if (!items.empty()) legend(c, items, box);
}

// ----------------------------------------------------------------- elements: number, text, list
inline void el_number(Canvas& c, Node node, Box box) {
  float w = box.w(), h = box.h();
  std::string value = str(node["value"]), unit = str(node["unit"]), head = str(node["title"]);
  std::vector<std::string> sub;
  if (truthy(node, "sub")) sub = c.wrap(str(node["sub"]), 18, w);
  auto full = [&](int s) { return c.tw(value, s, true) + (unit.empty() ? 0 : c.tw(" " + unit, 24)); };
  auto need = [&](int s) { return (head.empty() ? 0 : 24) + lineH(s) + 23 * (int)sub.size(); };
  int size = 56;
  if (need(56) > h || full(56) > w) size = 32;
  if (need(32) > h || full(32) > w) size = size == 56 ? 56 : 24;   // tight field: smaller figure before anything is dropped
  while (need(size) > h && !sub.empty()) sub.pop_back();       // the figure matters more than the line below it
  if (need(size) > h || full(size) > w)
    c.warning("number does not fit: " + (head.empty() ? value : head) + " (the field is too small: use fewer rows beside each other or a one-line sentence)");
  float y = box.y0 + fmaxf(0, (h - need(size)) / 2), cx = (box.x0 + box.x1) / 2;
  if (!head.empty()) { c.text(cx, y, head, 18, false, 'm', false, w, box.x0); y += 24; }
  float vx = cx - full(size) / 2;
  c.text(vx, y - (size == 56 ? 10 : size == 32 ? 4 : 1), value, size, true);
  if (!unit.empty()) c.text(vx + c.tw(value, size, true), y + (size == 56 ? 20 : size == 32 ? 4 : -1), " " + unit, 24);
  y += lineH(size);
  for (auto& l : sub) { c.text(cx, y, l, 18, false, 'm', false, -1, box.x0); y += 23; }
}

inline int sizeOf(Node node, const char* def = "normal") {
  std::string s = str(node, "size", def);
  return s == "small" ? 18 : s == "large" ? 32 : s == "huge" ? 56 : 24;
}

inline void el_text(Canvas& c, Node node, Box box) {
  bool inv = truthy(node, "inverted"), frame = truthy(node, "border"), bold = truthy(node, "bold");
  float pad = inv || frame ? 12 : 0;
  if (inv) c.block(box.x0, box.y0, box.x1, box.y1);
  else if (frame) c.rect(box, 2);
  float y = box.y0 + pad;
  if (truthy(node, "title")) { c.text(box.x0 + pad, y - 2, str(node["title"]), 18, false, 'l', inv, box.w() - 2 * pad); y += 24; }
  std::string content = str(node["content"]);
  int order[3], on = 0;
  order[on++] = sizeOf(node);
  for (int s : {24, 18}) if (s < order[0]) order[on++] = s;
  std::vector<std::string> lines;
  int size = order[0];
  bool fits = false;
  for (int i = 0; i < on && !fits; i++) {
    size = order[i];
    lines = c.wrap(content, size, box.w() - 2 * pad, bold);
    fits = (int)lines.size() * lineH(size) <= box.y1 - pad - y;
  }
  if (!fits) c.warning("text does not fit: \"" + content.substr(0, 40) + "...\"");
  bool middle = has(node, "middle") ? truthy(node, "middle") : (inv || frame);
  if (middle) y += fmaxf(0, (box.y1 - pad - y - (int)lines.size() * lineH(size)) / 2);
  for (auto& l : lines) {
    if (y + lineH(size) > box.y1 + 4) break;
    if (truthy(node, "centered")) c.text((box.x0 + box.x1) / 2, y, l, size, bold, 'm', inv);
    else c.text(box.x0 + pad, y, l, size, bold, 'l', inv);
    y += lineH(size);
  }
}

inline void el_list(Canvas& c, Node node, Box box) {
  box = title(c, node, box);
  float x0 = box.x0, y = box.y0;
  int size = sizeOf(node), n = 0;
  for (Node p : node["points"].as<JsonArrayConst>()) {
    n++;
    std::string z = str(p, "sign", "dot");
    float cy = y + lineH(size) / 2.0f + 1;
    if (z == "check") c.check(x0 + 2, cy - 8, 15);
    else if (z == "warn") { c.ellipse(x0, cy - 10, x0 + 20, cy + 10, 1); c.block(x0 + 9, cy - 6, x0 + 11, cy + 1, false); c.block(x0 + 9, cy + 4, x0 + 11, cy + 6, false); }
    else if (z == "open") c.ellipse(x0 + 2, cy - 8, x0 + 18, cy + 8, 0, 2);
    else if (z == "number") { c.ellipse(x0, cy - 11, x0 + 22, cy + 11, 0, 2); c.text(x0 + 11, cy - 13, std::to_string(n), 18, true, 'm'); }
    else c.ellipse(x0 + 6, cy - 4, x0 + 14, cy + 4, 1);
    float tx = x0 + 32, kw = 0;
    if (truthy(p, "lead")) { kw = c.tw(str(p["lead"]) + " ", size, true); c.text(tx, y, str(p["lead"]), size, true); }
    for (auto& l : c.wrap(str(p["text"]), size, box.x1 - tx - kw)) {
      if (y + lineH(size) > box.y1 + 4) { c.warning("list does not fit the height"); return; }
      c.text(tx + kw, y, l, size);
      y += lineH(size);
    }
    y += 4;
  }
}

// ----------------------------------------------------------------- the other elements
}  // namespace kit
#include "kit_charts.h"   // scatter, timebars, grid, scale, shares, table, week
#include "kit_shapes.h"   // ring, gauge, clock, route, body, stages, radar, month, hbars
#include "kit_more.h"     // blocks, seesaw, battery, sunarc, trends, mountain, timeline, waterfall, compass, wave, buttons
namespace kit {

typedef void (*Element)(Canvas&, Node, Box);
struct ElementEntry { const char* name; Element draw; };
static const ElementEntry ELEMENTS[] = {
    {"line", el_line}, {"bars", el_bars}, {"number", el_number}, {"text", el_text}, {"list", el_list},
    {"scatter", el_scatter}, {"timebars", el_timebars}, {"grid", el_grid}, {"scale", el_scale}, {"shares", el_shares},
    {"table", el_table}, {"week", el_week},
    {"ring", el_ring}, {"gauge", el_gauge}, {"clock", el_clock}, {"route", el_route}, {"body", el_body}, {"stages", el_stages},
    {"radar", el_radar}, {"month", el_month}, {"hbars", el_hbars},
    {"blocks", el_blocks}, {"seesaw", el_seesaw}, {"battery", el_battery}, {"sunarc", el_sunarc}, {"trends", el_trends},
    {"mountain", el_mountain}, {"timeline", el_timeline}, {"waterfall", el_waterfall}, {"compass", el_compass}, {"wave", el_wave},
    {"buttons", el_buttons}};
inline Element findElement(const std::string& name) {
  for (auto& e : ELEMENTS) if (name == e.name) return e.draw;
  return nullptr;
}

inline void layout(Canvas& c, Node node, Box box) {
  if (truthy(node, "box")) {
    c.rect(box, is(node, "box", "thick") ? 3 : 1);
    box = {box.x0 + 12, box.y0 + 8, box.x1 - 12, box.y1 - 8};
  }
  for (int pass = 0; pass < 2; pass++) {
    const char* key = pass == 0 ? "columns" : "rows";
    bool horizontal = pass == 0;
    if (!node[key].is<JsonArrayConst>() || has(node, "element")) continue;
    JsonArrayConst kids = node[key].as<JsonArrayConst>();
    int n = (int)kids.size();
    if (n == 0) return;
    float gap = num(node["gap"], horizontal ? 20 : 12), total = 0;
    for (Node k : kids) total += num(k["weight"], 1);
    float span = (horizontal ? box.w() : box.h()) - gap * (n - 1), pos = horizontal ? box.x0 : box.y0;
    bool divider = has(node, "divider") ? truthy(node, "divider") : horizontal;
    int i = 0;
    for (Node k : kids) {
      float size = span * num(k["weight"], 1) / total;
      Box b = horizontal ? Box{pos, box.y0, pos + size, box.y1} : Box{box.x0, pos, box.x1, pos + size};
      layout(c, k, {(float)rnd(b.x0), (float)rnd(b.y0), (float)rnd(b.x1), (float)rnd(b.y1)});
      pos += size + gap;
      if (++i < n && divider) {
        float m = rnd(pos - gap / 2);
        if (horizontal) c.line(m, box.y0, m, box.y1); else c.line(box.x0, m, box.x1, m);
      }
    }
    return;
  }
  std::string name = str(node["element"]);
  Element draw = findElement(name);
  if (!draw) { c.warning("unknown element \"" + name + "\""); return; }
  if (box.w() < 110 || box.h() < 44) {
    char b[80];
    snprintf(b, sizeof(b), "field too small for %s: %dx%d", name.c_str(), rnd(box.w()), rnd(box.h()));
    c.warning(b);
  }
  draw(c, node, box);
}

// Draws the whole screen into the display buffer (no refresh). Returns the warnings.
inline std::vector<std::string> render(Node spec) {
  Canvas c;
  epaper.fillScreen(TFT_WHITE);
  float top = 4, bottom = H - 6;
  Node r = spec["frame"];
  if (!r.isNull() && !truthy(spec, "fullscreen")) {
    c.text(PAD, 4, str(r["time"]), 56, true);
    c.text(186, 14, str(r["date"]), 24);
    c.text(186, 48, str(r["weather"]), 18, false, 'l', false, W - 186 - PAD);
    c.line(0, 90, W, 90);
    c.line(0, 391, W, 391);
    c.text(PAD, 395, "TODAY", 18);
    c.text(PAD, 415, str(r["today"]), 24, true, 'l', false, 470);
    c.text(PAD, 448, str(r["next"]), 18, false, 'l', false, 500);
    c.text(W - PAD, 395, str(r["week"]), 18, false, 'r');
    c.text(W - PAD, 415, str(r["energy"]), 24, true, 'r');
    c.text(W - PAD, 448, str(r, "hint", "Hold button and speak"), 18, false, 'r');
    top = 94; bottom = 386;
  }
  float y = top;
  Node k = spec["header"];
  if (truthy(k, "label")) {
    std::string right = str(k["right"]);
    c.text(PAD, y, str(k["label"]), 18, false, 'l', false, W - 2 * PAD - (right.empty() ? 0 : c.tw(right) + 12));
    if (!right.empty()) c.text(W - PAD, y, right, 18, false, 'r');
    y += 22;
  }
  if (truthy(k, "title")) { c.text(PAD, y - 2, str(k["title"]), 32, false, 'l', false, W - 2 * PAD); y += 44; }
  std::vector<std::string> lines;
  if (truthy(spec, "sentence")) lines = c.wrap(str(spec["sentence"]), 24, W - 2 * PAD);
  if (lines.size() > 2) { c.warning("sentence longer than two lines"); lines.resize(2); }
  float sy = bottom - 30 * (int)lines.size() - (lines.empty() ? 0 : 2);
  for (size_t i = 0; i < lines.size(); i++) c.text(PAD, sy + 30 * i, lines[i], 24);
  if (spec["content"].isNull()) c.warning("the spec needs a \"content\" field");
  else layout(c, spec["content"], {(float)PAD, y + 4, (float)(W - PAD), sy - (lines.empty() ? 0 : 8)});
  return c.warn;
}

}  // namespace kit
