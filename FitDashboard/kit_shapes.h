// Part of kit.h (included from there): ring, gauge, clock, route, body, stages, radar, month, hbars.
#pragma once
namespace kit {

// Outline of a polygon, one pixel wide, through the rounded corner points.
inline void shapes_outline(Canvas& c, const std::vector<Pt>& pts) {
  std::vector<Pt> r;
  for (auto& p : pts) r.push_back({(float)rnd(p.x), (float)rnd(p.y)});
  c.polyline(r, 1, true);
}
// Thin lines and frames are cut off to whole pixels (as the Python drawing library does), not rounded.
// The small amount keeps a value that is a whole number except for float noise on that number.
inline float shapes_cut(float v) { return floorf(v + 0.01f); }
inline void shapes_thin(Canvas& c, float x0, float y0, float x1, float y1) { c.line(shapes_cut(x0), shapes_cut(y0), shapes_cut(x1), shapes_cut(y1)); }
// Rounds like Python: a half goes to the even neighbour.
inline float shapes_round(float v) { return nearbyintf(v); }
// Rectangle outline in black or white, drawn inwards.
inline void shapes_frame(Canvas& c, float x0, float y0, float x1, float y1, int width, bool black) {
  for (int i = 0; i < width; i++) {
    c.block(x0 + i, y0 + i, x1 - i, y0 + i, black); c.block(x0 + i, y1 - i, x1 - i, y1 - i, black);
    c.block(x0 + i, y0 + i, x0 + i, y1 - i, black); c.block(x1 - i, y0 + i, x1 - i, y1 - i, black);
  }
}

inline void el_ring(Canvas& c, Node node, Box box) {
  box = title(c, node, box);
  std::string sub = str(node["sub"]);
  float h = box.h() - (sub.empty() ? 0 : 24);
  float r = fminf(box.w(), h) / 2 - 2;
  float cx = (box.x0 + box.x1) / 2, cy = box.y0 + h / 2;
  float t = fmaxf(10, r * 0.26f);
  float w = num(node["value"]);
  if (isnan(w)) { c.warning("ring: \"value\" is missing or not a number"); w = 0; }
  w = fmaxf(0, fminf(1, w));
  if (r > t) {
    if (w > 0) c.fillPoly(sector(cx, cy, r - t, r, 0, 360 * w), P_BLACK);
    c.circle(cx, cy, r);
    c.circle(cx, cy, r - t);
  } else c.warning("ring: field too small");
  int size = r - t > 48 ? 32 : 24;
  c.text(cx, cy - lineH(size) / 2.0f - 2, str(node["center"]), size, true, 'm');
  if (!sub.empty()) c.text(cx, box.y1 - 24, sub, 18, false, 'm', false, box.w(), box.x0);
}

inline void el_gauge(Canvas& c, Node node, Box box) {
  box = title(c, node, box);
  float r = fminf(box.w() / 2 - 4, box.h() - 46);
  float cx = (box.x0 + box.x1) / 2, cy = box.y0 + r + 2;
  JsonArrayConst segs = node["segments"].as<JsonArrayConst>();
  int n = count(node["segments"]);
  if (n == 0) c.warning("gauge: \"segments\" is missing or empty");
  if (r < 8) { c.warning("gauge: field too small"); return; }
  float prev = 0;
  std::string first, last;
  int i = 0;
  for (Node s : segs) {
    float to = fmaxf(0, fminf(1, num(s["to"], 1)));
    std::vector<Pt> p = sector(cx, cy, r * 0.7f, r, -90 + 180 * prev, -90 + 180 * to);
    c.fillPoly(p, pat(s, "pattern", P_WHITE));
    shapes_outline(c, p);
    prev = to;
    if (i++ == 0) first = str(s["name"]);
    last = str(s["name"]);
  }
  float v = num(node["value"]);
  if (isnan(v)) c.warning("gauge: \"value\" is missing or not a number");
  else {
    Pt tip = pol(cx, cy, r * 0.96f, -90 + 180 * fmaxf(0, fminf(1, v)));
    c.line(cx, cy, tip.x, tip.y, 5);
  }
  c.disc(cx, cy, 9);
  c.text(cx - r, cy + 4, first, 18);
  c.text(cx + r, cy + 4, last, 18, false, 'r');
  c.text(cx, cy + 6, str(node["center"]), 32, true, 'm');
}

// 24-hour dial, midnight at the top.
inline void el_clock(Canvas& c, Node node, Box box) {
  box = title(c, node, box);
  float r = fminf(box.w(), box.h()) / 2 - 24;
  float cx = (box.x0 + box.x1) / 2, cy = (box.y0 + box.y1) / 2;
  if (r < 8) { c.warning("clock: field too small"); return; }
  for (const char* key : {"boegen", "arcs"}) {
    for (Node b : node[key].as<JsonArrayConst>()) {
      float a0 = num(b["from"]) * 15, a1 = num(b["to"]) * 15;
      if (isnan(a0) || isnan(a1)) { c.warning("clock: arc without \"from\" and \"to\""); continue; }
      if (a1 < a0) a1 += 360;
      if (a1 - a0 > 360) a1 = a0 + 360;
      std::vector<Pt> p = sector(cx, cy, r * 0.6f, r, a0, a1);
      c.fillPoly(p, pat(b, "pattern", P_BLACK));
      shapes_outline(c, p);
    }
  }
  c.circle(cx, cy, r);
  c.circle(cx, cy, r * 0.6f);
  for (int hh = 0; hh < 24; hh++) {
    Pt a = pol(cx, cy, r, hh * 15), b = pol(cx, cy, r + (hh % 6 == 0 ? 6 : 3), hh * 15);
    shapes_thin(c, a.x, a.y, b.x, b.y);
  }
  auto mark = [&](float hh, const std::string& t) {
    Pt p = pol(cx, cy, r + 17, hh * 15);
    c.text(p.x, p.y - 12, t, 18, false, 'm');
  };
  if (node["marks"].is<JsonArrayConst>()) {
    for (Node m : node["marks"].as<JsonArrayConst>()) if (!isnan(num(m))) mark(num(m), str(m));
  } else for (int hh : {0, 6, 12, 18}) mark(hh, std::to_string(hh));
  float a = num(node["pointer"]) * 15;
  if (!isnan(a)) c.polygon({pol(cx, cy, r * 0.56f, a), pol(cx, cy, r * 0.4f, a - 9), pol(cx, cy, r * 0.4f, a + 9)});
  if (truthy(node, "center")) c.text(cx, cy - 16, str(node["center"]), 24, true, 'm');
}

inline void el_route(Canvas& c, Node node, Box box) {
  box = title(c, node, box);
  std::vector<Pt> pts;
  for (Node p : node["points"].as<JsonArrayConst>()) {
    float x = num(p[0]), y = num(p[1]);
    if (!isnan(x) && !isnan(y)) pts.push_back({x, y});
  }
  if (pts.empty()) { c.warning("route: \"points\" is missing or empty"); c.text(box.x0, box.y0, "no data"); return; }
  if ((int)pts.size() != count(node["points"])) c.warning("route: points that are not [x, y] were skipped");
  float xa = pts[0].x, xb = pts[0].x, ya = pts[0].y, yb = pts[0].y;
  for (auto& p : pts) { xa = fminf(xa, p.x); xb = fmaxf(xb, p.x); ya = fminf(ya, p.y); yb = fmaxf(yb, p.y); }
  float s = fminf((box.w() - 30) / (xb - xa != 0 ? xb - xa : 1), (box.h() - 30) / (yb - ya != 0 ? yb - ya : 1));
  float ox = (box.x0 + box.x1) / 2 - s * (xb + xa) / 2, oy = (box.y0 + box.y1) / 2 - s * (yb + ya) / 2;
  int n = (int)pts.size();
  for (auto& p : pts) p = {ox + s * p.x, oy + s * p.y};
  c.polyline(pts, 3);
  if (truthy(node, "highlight")) {
    int a = (int)num(node["highlight"][0], -1), b = (int)num(node["highlight"][1], -1);
    if (a < 0 || b < a || a >= n) c.warning("route: \"highlight\" needs two indexes [from, to] inside the points");
    else c.polyline(std::vector<Pt>(pts.begin() + a, pts.begin() + std::min(b, n - 1) + 1), 8);
  }
  c.ellipse(pts[0].x - 7, pts[0].y - 7, pts[0].x + 7, pts[0].y + 7, 2, 2);
  c.disc(pts.back().x, pts.back().y, 6);
  for (Node m : node["marks"].as<JsonArrayConst>()) {
    int at = (int)num(m["at"], -1);
    if (at < 0 || at >= n) { c.warning("route: mark outside the points: \"" + str(m["text"]) + "\""); continue; }
    float x = pts[at].x, y = pts[at].y, dx = num(m["dx"], 8), dy = num(m["dy"], -24);
    c.ellipse(x - 4, y - 4, x + 4, y + 4, 2, 2);
    c.label(x + dx, y + dy, str(m["text"]), dx >= 0 ? 'l' : 'r', box.x0, box.x1, truthy(m, "bold"));
  }
}

inline void el_body(Canvas& c, Node node, Box box) {
  box = title(c, node, box);
  static const struct { const char* name; float x, y; } PARTS[] = {
      {"head", 50, 18}, {"neck", 50, 34}, {"shoulder_l", 38, 46}, {"shoulder_r", 62, 46}, {"back", 50, 72},
      {"hip", 50, 100}, {"thigh_l", 42, 125}, {"thigh_r", 58, 125}, {"knee_l", 35, 150}, {"knee_r", 65, 150},
      {"shin_l", 33, 170}, {"shin_r", 67, 170}, {"foot_l", 31, 192}, {"foot_r", 69, 192}};
  static const float LIMBS[7][4] = {{50, 34, 50, 100}, {50, 46, 24, 92}, {50, 46, 76, 92}, {50, 100, 35, 150}, {35, 150, 31, 192},
                                    {50, 100, 65, 150}, {65, 150, 69, 192}};
  float s = (box.h() - 8) / 200;
  if (s <= 0) { c.warning("body: field too small"); return; }
  auto fx = [&](float v) { return box.x0 + 14 + v * s; };
  auto fy = [&](float v) { return box.y0 + 4 + v * s; };
  int w = std::max(4, rnd(9 * s));
  for (auto& l : LIMBS) {
    c.line(fx(l[0]), fy(l[1]), fx(l[2]), fy(l[3]), w);
    c.disc(fx(l[2]), fy(l[3]), w / 2.0f);
  }
  c.disc(fx(50), fy(18), 14 * s);
  float tx = fx(100) + 26, tw = fmaxf(0, box.x1 - tx);
  for (Node m : node["annotate"].as<JsonArrayConst>()) {
    std::string part = str(m["part"]);
    int k = -1;
    for (int i = 0; i < 14; i++) if (part == PARTS[i].name) k = i;
    if (k < 0) { c.warning("body: unknown part \"" + part + "\""); continue; }
    float px = fx(PARTS[k].x), py = fy(PARTS[k].y), r = fmaxf(11, 14 * s);
    c.circle(px, py, r, 3);
    shapes_thin(c, px + r, py, tx - 6, py);
    c.text(tx, py - 24, str(m["text"]), 18, true, 'l', false, tw);
    if (truthy(m, "sub")) c.text(tx, py - 2, str(m["sub"]), 18, false, 'l', false, tw);
  }
}

// Step chart, for example sleep stages over the night.
inline void el_stages(Canvas& c, Node node, Box box) {
  box = title(c, node, box);
  Node names = node["stages"], a = node["axis"];
  int n = count(names);
  if (n == 0) { c.warning("stages: \"stages\" is missing or empty"); c.text(box.x0, box.y0, "no data"); return; }
  std::vector<Pattern> pats;
  for (Node p : node["pattern"].as<JsonArrayConst>()) pats.push_back(pat(str(p)));
  if (pats.empty()) pats = {P_WHITE, P_HATCHED, P_GRAY, P_BLACK};
  float from = num(a["from"]), to = num(a["to"]);
  if (isnan(from) || isnan(to) || from == to) {
    c.warning("stages: \"axis\" needs \"from\" and \"to\"");
    if (isnan(from)) from = 0;
    if (isnan(to) || to == from) to = from + 1;
  }
  float left = 0;
  for (Node s : names.as<JsonArrayConst>()) left = fmaxf(left, c.tw(str(s)));
  left += 10;
  float px0 = box.x0 + left, px1 = box.x1 - 8, py0 = box.y0, py1 = box.y1 - 26;
  float rh = (py1 - py0) / n;
  auto fx = [&](float v) { return px0 + (px1 - px0) * (v - from) / (to - from); };
  int i = 0;
  for (Node s : names.as<JsonArrayConst>()) {
    c.text(box.x0, py0 + rh * (i + 0.5f) - 13, str(s));
    c.hdots(px0, px1, shapes_round(py0 + rh * (i + 0.5f)), 6);
    i++;
  }
  for (Node t : a["marks"].as<JsonArrayConst>())
    if (!isnan(num(t))) c.text(fx(num(t)), py1 + 3, fmt(num(t), "clock"), 18, false, 'm', false, -1, box.x0, box.x1);
  shapes_thin(c, px0, py1, px1, py1);
  float last = NAN;
  for (Node s : node["spans"].as<JsonArrayConst>()) {
    float st = num(s["stage"]), sa = num(s["from"]), sb = num(s["to"]);
    if (isnan(st) || isnan(sa) || isnan(sb) || st < 0 || st >= n) { c.warning("stages: span without valid \"stage\", \"from\" and \"to\""); continue; }
    int k = (int)st;
    float cy = py0 + rh * (k + 0.5f);
    Box b = {fx(sa), cy - rh * 0.34f, fx(sb), cy + rh * 0.34f};
    if (!isnan(last)) shapes_thin(c, b.x0, last, b.x0, cy);
    c.fill(b, pats[k % pats.size()]);
    c.rect({(float)rnd(b.x0), (float)rnd(b.y0), (float)rnd(b.x1), (float)rnd(b.y1)});
    last = cy;
  }
}

inline void el_radar(Canvas& c, Node node, Box box) {
  box = title(c, node, box);
  Node axes = node["axes"], series = node["series"];
  int n = count(axes);
  if (n < 3) { c.warning("radar: needs at least three \"axes\""); c.text(box.x0, box.y0, "no data"); return; }
  bool two = count(series) > 1;
  float widest = 0;
  for (Node a : axes.as<JsonArrayConst>()) widest = fmaxf(widest, c.tw(str(a)));
  float r = fminf(box.w() / 2 - widest - 10, (box.h() - (two ? 24 : 0)) / 2 - 24);
  if (r < 8) { c.warning("radar: field too small"); return; }
  float cx = (box.x0 + box.x1) / 2, cy = box.y0 + 24 + r;
  for (float f : {0.5f, 1.0f}) {
    std::vector<Pt> p;
    for (int i = 0; i < n; i++) { Pt q = pol(cx, cy, r * f, 360.0f * i / n); p.push_back({shapes_cut(q.x), shapes_cut(q.y)}); }
    c.polyline(p, 1, true);
  }
  int i = 0;
  for (Node name : axes.as<JsonArrayConst>()) {
    Pt p = pol(cx, cy, r, 360.0f * i / n), q = pol(cx, cy, r + 8, 360.0f * i / n);
    shapes_thin(c, cx, cy, p.x, p.y);
    char anchor = fabsf(q.x - cx) < 6 ? 'm' : q.x > cx ? 'l' : 'r';
    c.text(q.x, q.y - (q.y < cy - r * 0.8f ? 24 : q.y < cy + r * 0.5f ? 12 : 4), str(name), 18, false, anchor);
    i++;
  }
  int k = 0;
  for (Node row : series.as<JsonArrayConst>()) {
    std::vector<Pt> P;
    int j = 0;
    for (Node v : row["values"].as<JsonArrayConst>()) { P.push_back(pol(cx, cy, r * num(v, 0), 360.0f * j / n)); j++; }
    if (P.empty()) { c.warning("radar: series without \"values\""); k++; continue; }
    if (k == 0) {
      c.fillPoly(P, P_LIGHT);
      c.polyline(P, 3, true);
      for (auto& p : P) c.disc(p.x, p.y, 4);
    } else {
      for (size_t e = 0; e < P.size(); e++) {
        Pt a = P[e], b = P[(e + 1) % P.size()];
        int steps = (int)fmaxf(1, hypotf(b.x - a.x, b.y - a.y) / 6);
        for (int s = 0; s < steps; s++) {
          float x = a.x + (b.x - a.x) * s / steps, y = a.y + (b.y - a.y) * s / steps;
          c.block(x - 1, y - 1, x + 1, y + 1);
        }
      }
    }
    k++;
  }
  if (two) {
    std::vector<LegendItem> items;
    items.push_back({str(series[0]["name"]), [&c](float x, float y) { c.line(x, y, x + 24, y, 3); }});
    items.push_back({str(series[1]["name"]), [&c](float x, float y) { for (int s = 0; s < 24; s += 6) c.block(x + s, y - 1, x + s + 2, y + 1); }});
    legend(c, items, {box.x0, box.y1 - 22, box.x1, box.y1});
  }
}

inline void el_month(Canvas& c, Node node, Box box) {
  box = title(c, node, box);
  float fs = num(node["start_weekday"]), fn = num(node["days"]);
  if (isnan(fs) || isnan(fn)) c.warning("month: needs \"start_weekday\" (0 = Monday) and \"days\"");
  if (!isnan(fs) && (fs < 0 || fs > 6)) c.warning("month: \"start_weekday\" must be 0 to 6");
  if (!isnan(fn) && (fn < 1 || fn > 31)) c.warning("month: \"days\" must be 1 to 31");
  int start = isnan(fs) ? 0 : (int)fmaxf(0, fminf(6, fs)), n = isnan(fn) ? 0 : (int)fmaxf(0, fminf(31, fn));
  int rows = std::max(1, (start + n + 6) / 7);
  float cw = box.w() / 7, ch = (box.h() - 22) / rows;
  static const char* DAYS[] = {"Mo", "Tu", "We", "Th", "Fr", "Sa", "Su"};
  for (int i = 0; i < 7; i++) c.text(box.x0 + cw * (i + 0.5f), box.y0 - 3, DAYS[i], 18, false, 'm');
  Node marks = node["marks"];
  int today = (int)num(node["today"], -1);
  for (int day = 1; day <= n; day++) {
    int k = start + day - 1;
    float fx = box.x0 + cw * (k % 7), fy = box.y0 + 22 + ch * (k / 7);
    float bx = shapes_cut(fx), by = shapes_cut(fy), ex = shapes_cut(fx + cw), ey = shapes_cut(fy + ch);
    bool inv = false;
    for (Node h : node["highlight"].as<JsonArrayConst>()) if ((int)num(h, -1) == day) inv = true;
    if (inv) c.block(bx, by, ex, ey); else c.rect({bx, by, ex, ey});
    if (day == today) shapes_frame(c, shapes_cut(fx + 2), shapes_cut(fy + 2), shapes_cut(fx + cw - 2), shapes_cut(fy + ch - 2), 2, !inv);
    c.text(fx + 6, fy + ch / 2 - 13, std::to_string(day), 18, false, 'l', inv);
    float sx = fx + cw - 16, sy = fy + ch / 2;
    std::string m = str(marks[std::to_string(day).c_str()]);
    if (m == "dot") c.disc(sx, sy, 5, !inv);
    else if (m == "large") c.block(sx - 8, sy - 8, sx + 8, sy + 8, !inv);
    else if (m == "ring") { c.disc(sx, sy, 6, !inv); c.disc(sx, sy, 4, inv); }
    else if (m == "cross") { c.line(sx - 6, sy - 6, sx + 6, sy + 6, 2, !inv); c.line(sx - 6, sy + 6, sx + 6, sy - 6, 2, !inv); }
    else if (!m.empty()) c.warning("month: unknown mark \"" + m + "\"");
  }
}

inline void el_hbars(Canvas& c, Node node, Box box) {
  box = title(c, node, box);
  Node rows = node["rows"], sw = node["threshold"];
  int n = count(rows);
  if (n == 0) { c.warning("hbars: \"rows\" is missing or empty"); c.text(box.x0, box.y0, "no data"); return; }
  bool hasSw = truthy(sw) && !isnan(num(sw["value"]));
  if (truthy(sw) && !hasSw) c.warning("hbars: \"threshold\" needs a \"value\"");
  float top = hasSw && truthy(sw, "text") ? 24 : 0;
  float left = 0, right = 0, most = hasSw ? num(sw["value"]) : 0;
  bool strong = false;
  for (Node r : rows.as<JsonArrayConst>()) {
    left = fmaxf(left, c.tw(str(r["name"]), 18, truthy(r, "highlight")));
    right = fmaxf(right, c.tw(str(r["text"])));
    most = fmaxf(most, num(r["value"], 0));
    if (truthy(r, "highlight")) strong = true;
  }
  left += 12; right += 10;
  float vmax = num(node["max"], most);
  if (!(vmax > 0)) vmax = 1;
  float rh = fminf(36, (box.h() - top) / n);
  if (rh < 22) c.warning("hbars: rows too tight");
  auto fx = [&](float v) { return box.x0 + left + (box.w() - left - right) * v / vmax; };
  int i = 0;
  for (Node r : rows.as<JsonArrayConst>()) {
    float cy = box.y0 + top + rh * (i + 0.5f), v = num(r["value"]);
    bool hl = truthy(r, "highlight");
    if (isnan(v)) { c.warning("hbars: row without \"value\": " + str(r["name"])); v = 0; }
    c.text(box.x0, cy - 13, str(r["name"]), 18, hl);
    Box b = {box.x0 + left, cy - rh * 0.3f, fx(v), cy + rh * 0.3f};
    c.fill(b, pat(r, "pattern", hl || !strong ? P_BLACK : P_GRAY));
    c.rect({(float)rnd(b.x0), (float)rnd(b.y0), (float)rnd(b.x1), (float)rnd(b.y1)});
    c.text(b.x1 + 8, cy - 13, str(r["text"]), 18, hl);
    i++;
  }
  if (hasSw) {
    float x = fx(num(sw["value"]));
    for (int yy = rnd(box.y0 + top); yy < rnd(box.y0 + top + rh * n); yy += 8) c.block(x - 1, yy, x, yy + 4);
    if (truthy(sw, "text")) c.label(x, box.y0 - 2, str(sw["text"]), 'm', box.x0, box.x1);
  }
}

}  // namespace kit
