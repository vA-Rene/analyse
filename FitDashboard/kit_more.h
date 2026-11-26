// Part of kit.h (included from there).
// Elements: blocks, seesaw, battery, sunarc, trends, mountain, timeline, waterfall, compass, wave, buttons.
#pragma once
namespace kit {

// Structure of a session: width is duration, height is intensity.
inline void el_blocks(Canvas& c, Node node, Box box) {
  box = title(c, node, box);
  float x0 = box.x0, y0 = box.y0, x1 = box.x1, y1 = box.y1;
  Node parts = node["parts"];
  float total = 0;
  for (Node p : parts.as<JsonArrayConst>()) total += fmaxf(0, num(p["min"], 0));
  if (total <= 0) { c.text(x0, y0, "no data"); c.warning("blocks: \"parts\" with \"min\" is missing"); return; }
  float py1 = y1 - 26, py0 = y0 + 24, x = x0;
  for (Node p : parts.as<JsonArrayConst>()) {
    float w = (x1 - x0) * fmaxf(0, num(p["min"], 0)) / total;
    float height = num(p["height"], 0);
    float hgt = (py1 - py0) * height;
    Box b = {x, py1 - hgt, x + w, py1};
    c.fill(b, height > 0.75f ? P_BLACK : height > 0.45f ? P_GRAY : P_LIGHT);
    c.rect({(float)rnd(b.x0), (float)rnd(b.y0), (float)rnd(b.x1), (float)rnd(b.y1)});
    std::string t = str(p["text"]), sub = str(p["sub"]);
    if (!t.empty() && c.tw(t) < w + 20) c.text(x + w / 2, py1 - hgt - 24, t, 18, false, 'm', false, -1, x0, x1);
    if (!sub.empty() && c.tw(sub) < w - 2) c.text(x + w / 2, py1 + 3, sub, 18, false, 'm');
    x += w;
  }
  c.line(x0, py1, x1, py1);
}

inline void el_seesaw(Canvas& c, Node node, Box box) {
  box = title(c, node, box);
  float x0 = box.x0, x1 = box.x1, y1 = box.y1;
  Node L = node["left"], R = node["right"];
  float lv = num(L["value"]), rv = num(R["value"]);
  if (isnan(lv) || isnan(rv)) { c.warning("seesaw: \"left\" and \"right\" need a \"value\""); if (isnan(lv)) lv = 0; if (isnan(rv)) rv = 0; }
  float cx = (x0 + x1) / 2, base = y1 - 4, py = base - 40, half = (x1 - x0) * 0.36f;
  float a = fmaxf(-11, fminf(11, (lv - rv) * 0.35f)) * (float)M_PI / 180;
  float lx = cx - half * cosf(a), ly = py + half * sinf(a);
  float rx = cx + half * cosf(a), ry = py - half * sinf(a);
  c.polygon({{cx, py}, {cx - 26, base}, {cx + 26, base}});
  c.line(lx, ly, rx, ry, 6);
  float top = fmaxf(lv, rv);
  for (int k = 0; k < 2; k++) {
    Node s = k == 0 ? L : R;
    float bx = k == 0 ? lx : rx, by = k == 0 ? ly : ry, v = k == 0 ? lv : rv;
    bool heavy = k == 0 ? lv >= rv : rv > lv;
    float size = 40 + 34 * (top > 0 ? fmaxf(0, fminf(1, v / top)) : 1);
    Box b = {bx - size / 2, by - 4 - size, bx + size / 2, by - 4};
    c.block(b.x0, b.y0, b.x1, b.y1, heavy);
    if (!heavy) c.rect({(float)rnd(b.x0), (float)rnd(b.y0), (float)rnd(b.x1), (float)rnd(b.y1)}, 2);
    c.text(bx, by - 4 - size / 2 - 16, has(s, "text") ? str(s["text"]) : str(s["value"]), 24, true, 'm', heavy);
    c.text(bx, by - 4 - size - 26, str(s["name"]), 18, false, 'm', false, -1, x0, x1);
  }
}

inline void el_battery(Canvas& c, Node node, Box box) {
  box = title(c, node, box);
  float x0 = box.x0, y0 = box.y0, x1 = box.x1, y1 = box.y1;
  std::string sub = str(node["sub"]);
  float low = sub.empty() ? 0 : 26;
  float h = fminf(y1 - y0 - low, (x1 - x0) * 0.5f);
  float w = fminf(x1 - x0 - 16, h * 2.3f);
  if (h < 20 || w < 30) { c.warning("battery does not fit"); return; }
  float bx0 = (x0 + x1) / 2 - w / 2 - 6;
  float by0 = y0 + (y1 - y0 - low - h) / 2;
  c.rect({(float)rnd(bx0), (float)rnd(by0), (float)rnd(bx0 + w), (float)rnd(by0 + h)}, 4);
  c.block(bx0 + w, by0 + h * 0.3f, bx0 + w + 12, by0 + h * 0.7f);
  float v = num(node["value"]);
  if (isnan(v)) { c.warning("battery: \"value\" is missing"); v = 0; }
  v = fmaxf(0, fminf(1, v));
  c.fill({bx0 + 8, by0 + 8, bx0 + 8 + (w - 16) * v, by0 + h - 8}, v < 0.25f ? P_BLACK : P_GRAY);
  std::string t = str(node["center"]);
  int size = h > 90 ? 56 : 32;
  float tw = c.tw(t, size, true), lh = lineH(size);
  Box plate = {(float)rnd(bx0 + w / 2 - tw / 2 - 8), (float)rnd(by0 + h / 2 - lh / 2 + 4), (float)rnd(bx0 + w / 2 + tw / 2 + 8), (float)rnd(by0 + h / 2 + lh / 2)};
  c.block(plate.x0, plate.y0, plate.x1, plate.y1, false);
  c.rect(plate);
  c.text(bx0 + w / 2, by0 + h / 2 - lh / 2 - (size == 56 ? 6 : 0), t, size, true, 'm');
  if (!sub.empty()) c.text((x0 + x1) / 2, y1 - 24, sub, 18, false, 'm', false, x1 - x0, x0);
}

// Sun arc from sunrise to sunset with a marked time window.
inline void el_sunarc(Canvas& c, Node node, Box box) {
  box = title(c, node, box);
  float x0 = box.x0, y0 = box.y0, x1 = box.x1, y1 = box.y1;
  float rise = num(node["sunrise"]), down = num(node["sunset"]);
  if (isnan(rise) || isnan(down) || down <= rise) { c.text(x0, y0, "no data"); c.warning("sunarc: \"sunrise\" and \"sunset\" are missing or not in order"); return; }
  float hy = y1 - 28, xa = x0 + 40, xb = x1 - 40;
  float cx = (xa + xb) / 2, rx = (xb - xa) / 2;
  float ry = fminf(hy - y0 - 30, rx);
  auto P = [&](float t) -> Pt {
    float a = (float)M_PI * (t - rise) / (down - rise);
    return {cx - rx * cosf(a), hy - ry * sinf(a)};
  };
  c.line(x0, hy, x1, hy, 2);
  for (int i = 0; i <= 120; i++) {
    Pt p = P(rise + (down - rise) * i / 120);
    c.block(p.x, p.y, p.x + 1, p.y + 1);
  }
  Node f = node["window"];
  float from = num(f["from"]), to = num(f["to"]);
  if (truthy(f)) {
    if (isnan(from) || isnan(to)) c.warning("sunarc: \"window\" needs \"from\" and \"to\"");
    else {
      std::vector<Pt> seg;
      for (int i = 0; i <= 20; i++) seg.push_back(P(from + (to - from) * i / 20));
      c.polyline(seg, 9);
      if (truthy(f, "text")) c.label(seg[10].x, seg[10].y - 34, str(f["text"]), 'm', x0, x1, true);
    }
  }
  c.text(xa, hy + 3, fmt(rise, "clock"), 18, false, 'm');
  c.text(xb, hy + 3, fmt(down, "clock"), 18, false, 'm');
  float now = num(node["now"]);
  if (!isnan(now) && rise < now && now < down) {
    Pt p = P(now);
    for (int k = 0; k < 8; k++) {
      Pt a = pol(p.x, p.y, 13, k * 45), b = pol(p.x, p.y, 19, k * 45);
      c.line(a.x, a.y, b.x, b.y, 2);
    }
    c.ellipse(p.x - 10, p.y - 10, p.x + 10, p.y + 10, 2, 2);
  }
}

// Polyline exactly two pixels thick (Canvas::line with width 2 comes out heavier on slopes).
inline void trends_line2(Canvas& c, const std::vector<Pt>& pts, bool closed = false) {
  size_t n = pts.size();
  for (size_t i = 0; i + 1 < n || (closed && n > 2 && i < n); i++) {
    Pt a = pts[i], b = pts[(i + 1) % n];
    bool flat = fabsf(b.x - a.x) >= fabsf(b.y - a.y);
    c.line(a.x, a.y, b.x, b.y);
    c.line(a.x + (flat ? 0 : 1), a.y + (flat ? 1 : 0), b.x + (flat ? 0 : 1), b.y + (flat ? 1 : 0));
  }
}

// Several values below each other, each with a small curve and a direction.
inline void el_trends(Canvas& c, Node node, Box box) {
  box = title(c, node, box);
  float x0 = box.x0, y0 = box.y0, x1 = box.x1, y1 = box.y1;
  Node rows = node["rows"];
  int n = (int)rows.as<JsonArrayConst>().size();
  if (n == 0) { c.text(x0, y0, "no data"); c.warning("trends: \"rows\" is missing"); return; }
  float rh = fminf(44, (y1 - y0) / n);
  int size = rh >= 30 ? 24 : 18;
  float left = 0, right = 0;
  for (Node r : rows.as<JsonArrayConst>()) {
    left = fmaxf(left, c.tw(str(r["name"]), size));
    right = fmaxf(right, c.tw(str(r["value"]), size, true));
  }
  left += 16; right += 34;
  int i = 0;
  for (Node r : rows.as<JsonArrayConst>()) {
    float cy = y0 + rh * (i + 0.5f);
    c.text(x0, cy - lineH(size) / 2.0f, str(r["name"]), size);
    std::vector<float> vals;
    for (float v : floats(r["values"])) if (!isnan(v)) vals.push_back(v);
    float sx0 = x0 + left, sx1 = x1 - right - 10;
    if (!vals.empty() && sx1 > sx0) {
      float lo = *std::min_element(vals.begin(), vals.end()), hi = *std::max_element(vals.begin(), vals.end());
      float span = hi - lo != 0 ? hi - lo : 1;
      std::vector<Pt> pts;
      for (size_t k = 0; k < vals.size(); k++)
        pts.push_back({vals.size() > 1 ? sx0 + (sx1 - sx0) * k / (vals.size() - 1) : (sx0 + sx1) / 2, cy + rh * 0.3f - rh * 0.6f * ((vals[k] - lo) / span)});
      trends_line2(c, pts);
      c.ellipse(pts.back().x - 4, pts.back().y - 4, pts.back().x + 4, pts.back().y + 4, 1);
    }
    c.text(x1 - 28, cy - lineH(size) / 2.0f, str(r["value"]), size, true, 'r');
    float ax = x1 - 12;
    std::string t = str(r, "trend", "flat");
    if (t == "up") c.polygon({{ax, cy - 8}, {ax - 8, cy + 6}, {ax + 8, cy + 6}});
    else if (t == "down") c.polygon({{ax, cy + 8}, {ax - 8, cy - 6}, {ax + 8, cy - 6}});
    else c.block(ax - 8, cy - 2, ax + 8, cy + 2);
    if (i) c.hdots(x0, x1, y0 + rh * i, 4);
    i++;
  }
}

// 4808 -> "4,808" (as Python's "{:,}")
inline std::string mountain_thousands(float v) {
  char b[32];
  if (isnan(v)) return "";
  if (v == floorf(v) && fabsf(v) < 1e9f) snprintf(b, sizeof(b), "%ld", labs((long)v));
  else snprintf(b, sizeof(b), "%.1f", (double)fabsf(v));
  std::string s = b;
  size_t end = s.find('.');
  if (end == std::string::npos) end = s.size();
  for (int k = (int)end - 3; k > 0; k -= 3) s.insert(k, ",");
  return v < 0 ? "-" + s : s;
}

// Metres climbed as mountains: each full mountain is one summit.
inline void el_mountain(Canvas& c, Node node, Box box) {
  box = title(c, node, box);
  float x0 = box.x0, y0 = box.y0, x1 = box.x1, y1 = box.y1;
  float value = num(node["value"]), summit = num(node["summit"]);
  if (isnan(value) || isnan(summit) || summit <= 0) { c.text(x0, y0, "no data"); c.warning("mountain: \"value\" and \"summit\" are missing"); return; }
  float ratio = fmaxf(0, value / summit);
  int n = (int)fmaxf(1, fminf(5, ceilf(ratio)));   // at least one (empty) mountain
  float by = y1 - 4, hh = y1 - y0 - 34;
  float bw = fminf((x1 - x0) / n - 8, hh * 1.5f);
  float start = (x0 + x1) / 2 - (bw + 8) * n / 2;
  static const float shape[7][2] = {{0, 0}, {0.22f, 0.48f}, {0.3f, 0.42f}, {0.5f, 1}, {0.62f, 0.7f}, {0.7f, 0.76f}, {1, 0}};
  for (int i = 0; i < n; i++) {
    float f = fmaxf(0, fminf(1, ratio - i));
    float ox = start + (bw + 8) * i;
    std::vector<Pt> P, R;
    for (auto& s : shape) {
      P.push_back({ox + bw * s[0], by - hh * s[1]});
      R.push_back({(float)rnd(P.back().x), (float)rnd(P.back().y)});
    }
    int cut = rnd(by - hh * f);   // filled below this line only
    Pattern pt = f >= 1 ? P_BLACK : P_GRAY;
    c.scan(R, [&](int x, int y) { if (y > cut) c.px(x, y, patTest(pt, x, y)); });
    trends_line2(c, P, true);
    if (i == 0) c.text(ox + bw * 0.5f, by - hh - 26, str(node["name"]) + " " + mountain_thousands(summit) + " m", 18, false, 'm', false, -1, x0, x1);
  }
}

// Timeline with labelled points, alternating above and below.
inline void el_timeline(Canvas& c, Node node, Box box) {
  box = title(c, node, box);
  float x0 = box.x0, y0 = box.y0, x1 = box.x1, y1 = box.y1;
  float from = num(node["from"]), to = num(node["to"]);
  if (isnan(from) || isnan(to) || from == to) { c.text(x0, y0, "no data"); c.warning("timeline: \"from\" and \"to\" are missing or equal"); return; }
  float cy = (y0 + y1) / 2;
  auto fx = [&](float v) { return x0 + 24 + (x1 - x0 - 48) * (v - from) / (to - from); };
  for (Node s : node["spans"].as<JsonArrayConst>()) {
    float a = fx(num(s["from"], from)), b = fx(num(s["to"], to));
    c.fill({a, cy - 8, b, cy + 8}, pat(s, "pattern", P_GRAY));
    c.rect({(float)rnd(a), (float)rnd(cy - 8), (float)rnd(b), (float)rnd(cy + 8)});
  }
  c.line(fx(from), cy, fx(to), cy, 3);
  int i = 0;
  for (Node p : node["points"].as<JsonArrayConst>()) {
    float at = num(p["at"]);
    bool up = has(p, "above") ? truthy(p, "above") : i % 2 == 0;
    i++;
    if (isnan(at)) { c.warning("timeline: a point has no \"at\""); continue; }
    float x = fx(at);
    std::string t = str(p["text"]), sub = str(p["sub"]);
    c.line(x, cy - 16, x, cy + 16, 2);
    c.ellipse(x - 6, cy - 6, x + 6, cy + 6, truthy(p, "highlight") ? 1 : 2, 2);
    if (up) {
      if (!sub.empty()) {
        c.label(x, cy - 66, t, 'm', x0, x1, true);
        c.label(x, cy - 44, sub, 'm', x0, x1);
      } else c.label(x, cy - 44, t, 'm', x0, x1, true);
    } else {
      c.label(x, cy + 20, t, 'm', x0, x1, true);
      if (!sub.empty()) c.label(x, cy + 42, sub, 'm', x0, x1);
    }
  }
}

// A number as Python's str() prints it: 42 -> "42", 42.0 -> "42.0" unless everything was whole numbers.
inline std::string waterfall_str(float v, bool whole) {
  char b[32];
  if (whole && fabsf(v) < 1e9f) snprintf(b, sizeof(b), "%ld", lroundf(v));
  else {
    snprintf(b, sizeof(b), "%.6g", (double)v);
    if (!strpbrk(b, ".e")) strncat(b, ".0", sizeof(b) - strlen(b) - 1);
  }
  return b;
}

// How a number comes about: start, steps up and down, result.
inline void el_waterfall(Canvas& c, Node node, Box box) {
  box = title(c, node, box);
  float x0 = box.x0, y0 = box.y0, x1 = box.x1, y1 = box.y1;
  Node steps = node["steps"], first = node["start"], last = node["end"];
  if (isnan(num(first["value"]))) c.warning("waterfall: \"start\" needs a \"value\"");
  struct Item { std::string name; float a, b; Pattern pattern; std::string label; };
  std::vector<Item> items;
  float sum = num(first["value"], 0), top = sum;
  bool whole = !first["value"].is<float>() || first["value"].is<long>();
  for (Node s : steps.as<JsonArrayConst>()) if (s["value"].is<float>() && !s["value"].is<long>()) whole = false;
  items.push_back({str(first["name"]), 0, sum, P_BLACK, waterfall_str(sum, whole)});
  for (Node s : steps.as<JsonArrayConst>()) {
    float v = num(s["value"], 0);
    // the fonts have no real minus sign, a hyphen stands in for it
    items.push_back({str(s["name"]), sum, sum + v, v < 0 ? P_HATCHED : P_GRAY, (v > 0 ? "+" : "-") + waterfall_str(fabsf(v), whole)});
    sum += v;
    top = fmaxf(top, sum);
  }
  items.push_back({str(last["name"]), 0, sum, P_BLACK, waterfall_str(sum, whole)});
  float vmax = top * 1.08f;
  if (!(vmax > 0)) vmax = 1;
  float py0 = y0 + 24, py1 = y1 - 26;
  float band = (x1 - x0) / items.size();
  auto fy = [&](float v) { return py1 - (py1 - py0) * v / vmax; };
  for (size_t i = 0; i < items.size(); i++) {
    Item& it = items[i];
    float cx = x0 + band * (i + 0.5f);
    Box bb = {cx - band * 0.34f, fy(fmaxf(it.a, it.b)), cx + band * 0.34f, fy(fminf(it.a, it.b))};
    c.fill(bb, it.pattern);
    c.rect({(float)rnd(bb.x0), (float)rnd(bb.y0), (float)rnd(bb.x1), (float)rnd(bb.y1)});
    c.text(cx, bb.y0 - 24, it.label, 18, true, 'm');
    c.text(cx, py1 + 3, it.name, 18, false, 'm', false, band - 2, x0);
    if (i + 1 < items.size()) c.hdots(cx + band * 0.34f, cx + band * 0.66f, fy(it.b), 3);
  }
  c.line(x0, py1, x1, py1);
}

inline void el_compass(Canvas& c, Node node, Box box) {
  box = title(c, node, box);
  float x0 = box.x0, y0 = box.y0, x1 = box.x1, y1 = box.y1;
  float r = fminf(x1 - x0, y1 - y0) / 2 - 26;
  if (r < 12) { c.warning("compass does not fit"); return; }
  float cx = (x0 + x1) / 2, cy = (y0 + y1) / 2;
  c.ellipse(cx - r, cy - r, cx + r, cy + r, 0, 2);
  static const char* names[] = {"N", "E", "S", "W"};
  for (int k = 0; k < 4; k++) {
    float a = k * 90;
    Pt p = pol(cx, cy, r + 15, a);
    c.text(p.x, p.y - 13, names[k], 18, true, 'm');
    Pt q0 = pol(cx, cy, r - 8, a), q1 = pol(cx, cy, r, a);
    c.line(q0.x, q0.y, q1.x, q1.y, 2);
  }
  float a = num(node["wind_from"]);
  if (isnan(a)) c.warning("compass: \"wind_from\" is missing");
  else {
    Pt s = pol(cx, cy, r * 0.82f, a), e = pol(cx, cy, r * 0.7f, a + 180);
    c.line(s.x, s.y, e.x, e.y, 6);
    c.polygon({pol(cx, cy, r * 0.86f, a + 180), pol(cx, cy, r * 0.58f, a + 180 - 16), pol(cx, cy, r * 0.58f, a + 180 + 16)});
  }
  float b = num(node["heading"]);
  if (!isnan(b)) {
    for (int k = 0; k < (int)(r * 0.7f); k += 8) {
      Pt p = pol(cx, cy, k, b);
      c.ellipse(p.x - 2, p.y - 2, p.x + 2, p.y + 2, 1);
    }
    std::vector<Pt> tip = {pol(cx, cy, r * 0.82f, b), pol(cx, cy, r * 0.62f, b - 12), pol(cx, cy, r * 0.62f, b + 12)};
    c.polygon(tip, false);
    c.polyline(tip, 1, true);
  }
}

inline void el_wave(Canvas& c, Node node, Box box) {
  std::vector<float> vals = floats(node["values"]);
  if (vals.empty()) { c.warning("wave: \"values\" is missing"); return; }
  float band = box.w() / vals.size(), cy = (box.y0 + box.y1) / 2;
  for (size_t i = 0; i < vals.size(); i++) {
    float v = isnan(vals[i]) ? 0 : fmaxf(0, fminf(1, vals[i]));
    float hgt = fmaxf(3, box.h() / 2 * v);
    c.block(box.x0 + band * i + band * 0.25f, cy - hgt, box.x0 + band * i + band * 0.75f, cy + hgt);
  }
}

// Choice via the buttons on the device.
inline void el_buttons(Canvas& c, Node node, Box box) {
  box = title(c, node, box);
  float x0 = box.x0, y0 = box.y0, x1 = box.x1, y1 = box.y1;
  Node opts = node["options"];
  int n = (int)opts.as<JsonArrayConst>().size(), i = 0;
  if (n == 0) { c.warning("buttons: \"options\" is missing"); return; }
  float gap = 16, w = (x1 - x0 - gap * (n - 1)) / n;
  for (Node o : opts.as<JsonArrayConst>()) {
    float bx = x0 + (w + gap) * i++;
    c.rect({(float)rnd(bx), y0, (float)rnd(bx + w), y1}, truthy(o, "highlight") ? 3 : 1);
    c.ellipse(bx + 12, y0 + 12, bx + 48, y0 + 48, 1);
    c.text(bx + 30, y0 + 14, str(o["button"]), 24, true, 'm', true);
    c.text(bx + 58, y0 + 18, "Button", 18);
    float y = y0 + 58;
    for (auto& l : c.wrap(str(o["text"]), 24, w - 24)) {
      if (y + 30 > y1) { c.warning("buttons: text does not fit"); break; }
      c.text(bx + 12, y, l, 24);
      y += 30;
    }
  }
}

}  // namespace kit
