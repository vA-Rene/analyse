// Part of kit.h (included from there): scatter, timebars, grid, scale, shares, table, week.
#pragma once
namespace kit {

inline void scatter_dot(Canvas& c, const std::string& form, float x, float y, float r = 5) {
  if (form == "circle") c.ellipse(x - r, y - r, x + r, y + r, 2, 2);
  else c.ellipse(x - r, y - r, x + r, y + r, 1);
}

// Points of groups against two number axes, optional mean line per group and arrows with text.
inline void el_scatter(Canvas& c, Node node, Box box) {
  box = title(c, node, box);
  Node groups = node["groups"], xa = node["x_axis"], ya = node["y"];
  std::vector<float> xs, ys;
  for (Node g : groups.as<JsonArrayConst>())
    for (Node p : g["points"].as<JsonArrayConst>()) {
      int i0 = 0, i1 = 1;
      float x = num(p[i0]), y = num(p[i1]);
      if (isnan(x) || isnan(y)) continue;
      xs.push_back(x); ys.push_back(y);
    }
  if (xs.empty()) { c.text(box.x0, box.y0, "no data"); c.warning("scatter: no points"); return; }
  float xmin = num(xa["min"], *std::min_element(xs.begin(), xs.end())), xmax = num(xa["max"], *std::max_element(xs.begin(), xs.end()));
  float ymin = num(ya["min"], *std::min_element(ys.begin(), ys.end())), ymax = num(ya["max"], *std::max_element(ys.begin(), ys.end()));
  if (xmax == xmin) { c.warning("scatter: x axis has no range"); xmax = xmin + 1; }
  if (ymax == ymin) { c.warning("scatter: y axis has no range"); ymax = ymin + 1; }
  Axis ax = yAxis(c, ya, ymin, ymax, box, 26);
  Box p = ax.plot;
  bool flip = truthy(ya, "reversed");
  Scale fx = [&](float v) { return p.x0 + 8 + (p.w() - 16) * (v - xmin) / (xmax - xmin); };
  Scale fy = [&](float v) {
    float t = (p.h() - 16) * (v - ymin) / (ymax - ymin);
    return flip ? p.y0 + 8 + t : p.y1 - 8 - t;
  };
  for (size_t i = 0; i < ax.ticks.size(); i++) {
    c.hdots(p.x0, p.x1, fy(ax.ticks[i]));
    c.text(p.x0 - 6, fy(ax.ticks[i]) - 12, ax.labels[i], 18, false, 'r');
  }
  c.polyline({{p.x0, p.y0}, {p.x0, p.y1}, {p.x1, p.y1}});
  std::vector<float> xt;
  if (has(xa, "marks")) { for (Node t : xa["marks"].as<JsonArrayConst>()) xt.push_back(num(t, 0)); }
  else xt = {xmin, xmax};
  std::string xf = str(xa["format"]);
  for (size_t i = 0; i < xt.size(); i++) {
    c.line(fx(xt[i]), p.y1, fx(xt[i]), p.y1 + 4);
    std::string l = fmt(xt[i], xf);
    if (truthy(xa, "unit") && i == xt.size() - 1) l += " " + str(xa["unit"]);
    c.text(fx(xt[i]), p.y1 + 3, l, 18, false, 'm', false, -1, box.x0, box.x1);
  }
  std::vector<LegendItem> items;
  for (Node g : groups.as<JsonArrayConst>()) {
    std::string form = str(g, "form", "dot");
    for (Node pt : g["points"].as<JsonArrayConst>()) {
      int i0 = 0, i1 = 1;
      float x = num(pt[i0]), y = num(pt[i1]);
      if (isnan(x) || isnan(y)) { c.warning("scatter: a point needs two numbers"); continue; }
      scatter_dot(c, form, fx(x), fy(y));
    }
    if (truthy(g, "mean")) {
      int i0 = 0, i1 = 1;
      float mx = num(g["mean"][i0]), my = num(g["mean"][i1]);
      if (isnan(mx) || isnan(my)) c.warning("scatter: mean needs two numbers");
      else c.hdash(p.x0, fx(mx), fy(my), 3, 3);
    }
    items.push_back({str(g["name"]), [&c, form](float x, float yy) { scatter_dot(c, form, x + 14, yy); }});
  }
  legend(c, items, box);
  for (Node m : node["annotate"].as<JsonArrayConst>()) {
    if (!is(m, "type", "arrow")) continue;
    float vx = num(m["x"]), vy = num(m["y"]);
    if (isnan(vx) || isnan(vy)) { c.warning("scatter: arrow needs x and y"); continue; }
    float x = fx(vx), yy = fy(vy), dx = num(m["dx"], 40), dy = num(m["dy"], -34);
    c.arrow(x + dx, yy + dy, x + (dx > 0 ? 5 : -5), yy + (dy < 0 ? -5 : 5));
    if (truthy(m, "text"))
      c.label(x + dx + (dx > 0 ? 4 : -4), dy < 0 ? yy + dy - 22 : yy + dy, str(m["text"]), dx > 0 ? 'l' : 'r', box.x0, box.x1, true);
  }
}

// Horizontal bars on a time axis, for example the sleep window of each night.
inline void el_timebars(Canvas& c, Node node, Box box) {
  box = title(c, node, box);
  float x0 = box.x0, y0 = box.y0, x1 = box.x1, y1 = box.y1;
  Node rows = node["rows"], a = node["axis"], lines = node["lines"];
  int n = count(rows);
  if (n == 0) { c.text(x0, y0, "no data"); c.warning("timebars: no rows"); return; }
  float from = num(a["from"]), to = num(a["to"]);
  if (isnan(from) || isnan(to) || from == to) {
    c.warning("timebars: axis needs \"from\" and \"to\"");
    if (isnan(from)) from = 0;
    if (isnan(to) || to == from) to = from + 24;
  }
  float left = 0;
  for (Node r : rows.as<JsonArrayConst>()) left = fmaxf(left, c.tw(str(r["name"])));
  left += 10;
  float top = 0;
  for (Node l : lines.as<JsonArrayConst>()) if (truthy(l, "text")) top = 24;
  float px0 = x0 + left, px1 = x1 - 8, py0 = y0 + top, py1 = y1 - 26;
  Scale fx = [&](float h) { return px0 + (px1 - px0) * (h - from) / (to - from); };
  float rh = (py1 - py0) / n;
  if (rh < 15) c.warning("timebars: rows too tight");
  bool clock = str(a, "format", "clock") == "clock";
  for (Node t : a["marks"].as<JsonArrayConst>()) {
    float v = num(t, 0);
    c.vdots(fx(v), py0, py1, 4);
    c.text(fx(v), py1 + 3, clock ? fmt(v, "clock") : fmt(v), 18, false, 'm', false, -1, x0, x1);
  }
  c.line(px0, py1, px1, py1);
  bool strong = false;
  for (Node r : rows.as<JsonArrayConst>()) if (truthy(r, "highlight")) strong = true;
  int i = 0;
  for (Node r : rows.as<JsonArrayConst>()) {
    float cy = py0 + rh * (i++ + 0.5f);
    c.text(x0, cy - 13, str(r["name"]), 18, truthy(r, "highlight"));
    float rf = num(r["from"]), rt = num(r["to"]);
    if (isnan(rf)) { c.text(fx(from) + 8, cy - 13, str(r, "text", "no data")); continue; }
    if (isnan(rt)) { c.warning("timebars: row \"" + str(r["name"]) + "\" has no \"to\""); continue; }
    float hh = fminf(rh * 0.36f, 9);
    Box b = {fx(rf), cy - hh, fx(rt), cy + hh};
    c.fill(b, truthy(r, "highlight") || !strong ? P_BLACK : P_GRAY);
    c.rect({(float)rnd(b.x0), (float)rnd(b.y0), (float)rnd(b.x1), (float)rnd(b.y1)});
  }
  for (Node l : lines.as<JsonArrayConst>()) {
    float v = num(l["value"]);
    if (isnan(v)) { c.warning("timebars: line needs a value"); continue; }
    float x = fx(v);
    for (int yy = rnd(py0); yy < rnd(py1); yy += 8) c.line(x, yy, x, yy + 4, 2);
    if (truthy(l, "text")) c.label(x, y0 - 2, str(l["text"]), 'm', x0, x1);
  }
}

// Calendar grid: one column per week, seven rows, fill by level 0-3.
inline void el_grid(Canvas& c, Node node, Box box) {
  box = title(c, node, box);
  float x0 = box.x0, y0 = box.y0, x1 = box.x1, y1 = box.y1;
  Node weeks = node["weeks"], marks = node["annotate"];
  int n = count(weeks);
  if (n == 0) { c.text(x0, y0, "no data"); c.warning("grid: no weeks"); return; }
  float top = 4;
  for (Node m : marks.as<JsonArrayConst>()) if (truthy(m, "text")) top = 28;
  float left = 30;
  float cell = fminf((x1 - x0 - left) / n, (y1 - y0 - top - 26) / 7);
  float gx = x0 + left, gy = y0 + top;
  c.text(x0, gy + cell / 2 - 13, "Mo");
  c.text(x0, gy + cell * 6 + cell / 2 - 13, "Su");
  bool bad = false;
  int wi = 0;
  for (Node col : weeks.as<JsonArrayConst>()) {
    int di = 0;
    for (Node lvl : col.as<JsonArrayConst>()) {
      float cx = gx + cell * (wi + 0.5f), cy = gy + cell * (di + 0.5f);
      di++;
      if (lvl.isNull()) continue;
      float v = num(lvl);
      if (isnan(v) || v < 0 || v > 3 || v != floorf(v)) { bad = true; continue; }
      static const float share[] = {0, 0.2f, 0.32f, 0.45f};
      float s = v == 0 ? 1 : cell * share[(int)v];
      // same pixels as the Python drawing, which cuts the corners down to whole pixels
      c.block(floorf(cx - s), floorf(cy - s), floorf(cx + s), floorf(cy + s));
    }
    wi++;
  }
  if (bad) c.warning("grid: levels must be 0, 1, 2, 3 or null");
  for (Node m : node["months"].as<JsonArrayConst>()) {
    int i0 = 0, i1 = 1;
    c.text(gx + cell * num(m[i0], 0), gy + cell * 7 + 3, str(m[i1]));
  }
  for (Node m : marks.as<JsonArrayConst>()) {
    float f = num(m["from"]), t = num(m["to"]);
    if (isnan(f) || isnan(t)) { c.warning("grid: annotation needs \"from\" and \"to\""); continue; }
    float xa = gx + cell * f, xb = gx + cell * (t + 1);
    if (!has(m, "border") || truthy(m, "border")) c.rect({xa - 1, gy - 2, xb + 1, gy + cell * 7 + 1}, 2);
    if (truthy(m, "text")) c.label((xa + xb) / 2, gy - 28, str(m["text"]), 'm', x0, x1, true);
  }
}

// A band with segments (for example heart rate zones), pointers and a bracket.
inline void el_scale(Canvas& c, Node node, Box box) {
  box = title(c, node, box);
  float x0 = box.x0, y0 = box.y0, x1 = box.x1, y1 = box.y1;
  Node segs = node["segments"];
  int n = count(segs);
  if (n == 0) { c.text(x0, y0, "no data"); c.warning("scale: no segments"); return; }
  int first = 0, last = n - 1;
  float vmin = num(segs[first]["from"]), vmax = num(segs[last]["to"]);
  if (isnan(vmin) || isnan(vmax) || vmin == vmax) {
    c.warning("scale: segments need \"from\" and \"to\"");
    if (isnan(vmin)) vmin = 0;
    if (isnan(vmax) || vmax == vmin) vmax = vmin + 1;
  }
  Scale fx = [&](float v) { return x0 + 4 + (x1 - x0 - 8) * (v - vmin) / (vmax - vmin); };
  float by0 = y0 + (y1 - y0) / 2 - 2, by1 = by0 + 32;
  std::string f = str(node["format"]);
  for (Node s : segs.as<JsonArrayConst>()) {
    float sf = num(s["from"]), st = num(s["to"]);
    if (isnan(sf) || isnan(st)) continue;
    Box b = {fx(sf), by0, fx(st), by1};
    c.fill(b, pat(s, "pattern", P_WHITE));
    c.rect({(float)rnd(b.x0), (float)rnd(b.y0), (float)rnd(b.x1), (float)rnd(b.y1)});
    std::string name = str(s["name"]);
    if (!name.empty() && c.tw(name) < b.x1 - b.x0 - 4) c.text((b.x0 + b.x1) / 2, by1 + 24, name, 18, false, 'm');
    c.text(b.x0, by1 + 2, fmt(sf, f), 18, false, 'm', false, -1, x0, x1);
  }
  c.text(fx(vmax), by1 + 2, fmt(vmax, f) + (truthy(node, "unit") ? " " + str(node["unit"]) : ""), 18, false, 'r');
  Node b = node["range"];
  if (truthy(b)) {
    float rf = num(b["from"]), rt = num(b["to"]);
    if (isnan(rf) || isnan(rt)) c.warning("scale: range needs \"from\" and \"to\"");
    else {
      float xa = fx(rf), xb = fx(rt), yy = by0 - 8;
      c.polyline({{xa, yy + 6}, {xa, yy}, {xb, yy}, {xb, yy + 6}}, 2);
      if (truthy(b, "text")) c.label((xa + xb) / 2, yy - 25, str(b["text"]), 'm', x0, x1, true);
    }
  }
  for (Node z : node["pointer"].as<JsonArrayConst>()) {
    float v = num(z["value"]);
    if (isnan(v)) { c.warning("scale: pointer needs a value"); continue; }
    float x = fx(v), yy = by0 - 8;
    bool raised = truthy(z, "raised");
    c.polygon({{x - 7, yy - 10}, {x + 7, yy - 10}, {x, yy + 4}});
    if (truthy(z, "text")) c.label(x, yy - 34 - (raised ? 24 : 0), str(z["text"]), 'm', x0, x1, true);
    if (raised) c.line(x, yy - 34, x, yy - 10);
  }
}

// Horizontal 100 % bars, one row per comparison.
inline void el_shares(Canvas& c, Node node, Box box) {
  box = title(c, node, box);
  float x0 = box.x0, y0 = box.y0, x1 = box.x1, y1 = box.y1;
  Node rows = node["rows"];
  int n = count(rows);
  if (n == 0) { c.text(x0, y0, "no data"); c.warning("shares: no rows"); return; }
  float left = 0;
  for (Node r : rows.as<JsonArrayConst>()) left = fmaxf(left, c.tw(str(r["name"])));
  left += 12;
  float avail = y1 - y0 - 28;
  float rh = fminf(avail / n, 58), bh = fminf(28, rh - 26);
  int i = 0;
  for (Node r : rows.as<JsonArrayConst>()) {
    float yy = y0 + rh * i++;
    c.text(x0, yy + bh / 2 - 12, str(r["name"]), 18, truthy(r, "highlight"));
    float total = 0;
    for (Node t : r["parts"].as<JsonArrayConst>()) total += fmaxf(0, num(t["value"], 0));
    if (total == 0) total = 1;
    float x = x0 + left;
    for (Node t : r["parts"].as<JsonArrayConst>()) {
      float v = fmaxf(0, num(t["value"], 0));
      float w = (x1 - x0 - left) * v / total;
      c.fill({x, yy, x + w, yy + bh}, pat(t, "pattern", P_WHITE));
      c.rect({(float)rnd(x), (float)rnd(yy), (float)rnd(x + w), (float)rnd(yy + bh)});
      std::string pct = std::to_string((int)lroundf(100 * v / total)) + " %";
      if (w > c.tw(pct) + 4) c.text(x + w / 2, yy + bh + 1, pct, 18, false, 'm');
      x += w;
    }
  }
  float x = x0 + left, yy = y1 - 24;
  int first = 0;
  for (Node t : rows[first]["parts"].as<JsonArrayConst>()) {
    std::string name = str(t["name"]);
    c.fill({x, yy + 4, x + 22, yy + 20}, pat(t, "pattern", P_WHITE));
    c.rect({x, yy + 4, x + 22, yy + 20});
    c.text(x + 28, yy, name);
    x += 28 + c.tw(name) + 22;
  }
  if (x > x1) c.warning("shares: legend too wide");
}

inline void el_table(Canvas& c, Node node, Box box) {
  box = title(c, node, box);
  float x0 = box.x0, y0 = box.y0, x1 = box.x1, y1 = box.y1;
  Node cols = node["columns"];
  int nc = count(cols);
  if (nc == 0) { c.text(x0, y0, "no data"); c.warning("table: no columns"); return; }
  int size = sizeOf(node);
  float total = 0;
  std::vector<float> widths;
  std::vector<char> align;
  for (Node col : cols.as<JsonArrayConst>()) {
    float w = num(col["width"], 1);
    if (!(w > 0)) w = 1;
    widths.push_back(w);
    total += w;
    std::string a = str(col, "align", "l");
    align.push_back(a == "r" ? 'r' : a == "m" ? 'm' : 'l');
  }
  std::vector<float> xs = {x0};
  for (float w : widths) xs.push_back(xs.back() + (x1 - x0) * w / total);
  auto cell = [&](int i, float y, const std::string& t, int s, bool bold, bool white) {
    if (align[i] == 'r') c.text(xs[i + 1] - 8, y, t, s, bold, 'r', white, xs[i + 1] - xs[i] - 10);
    else if (align[i] == 'm') c.text((xs[i] + xs[i + 1]) / 2, y, t, s, bold, 'm', white);
    else c.text(xs[i] + 6, y, t, s, bold, 'l', white, xs[i + 1] - xs[i] - 10);
  };
  float y = y0;
  int i = 0;
  for (Node col : cols.as<JsonArrayConst>()) cell(i++, y - 2, str(col["name"]), 18, false, false);
  y += 24;
  c.line(x0, y, x1, y, 2);
  y += 3;
  float rh = lineH(size) + 2;
  bool firstBold = truthy(node, "first_bold"), tooMany = false;
  int r = 0;
  for (Node row : node["rows"].as<JsonArrayConst>()) {
    if (y + rh > y1 + 4) { c.warning("table does not fit the height"); break; }
    bool inv = false;
    for (Node k : node["highlight"].as<JsonArrayConst>()) if (isNum(k) && (int)num(k, -1) == r) inv = true;
    if (inv) c.block(x0, y, x1, y + rh - 1);
    i = 0;
    for (Node t : row.as<JsonArrayConst>()) {
      if (i >= nc) { tooMany = true; break; }
      cell(i, y, str(t), size, i == 0 && firstBold, inv);
      i++;
    }
    y += rh;
    if (!inv) c.hdots(x0, x1, y - 1, 3);
    r++;
  }
  if (tooMany) c.warning("table: a row has more cells than columns");
}

inline void el_week(Canvas& c, Node node, Box box) {
  box = title(c, node, box);
  float x0 = box.x0, y0 = box.y0, x1 = box.x1, y1 = box.y1;
  Node days = node["days"];
  int n = count(days);
  if (n == 0) { c.text(x0, y0, "no data"); c.warning("week: no days"); return; }
  float cw = (x1 - x0) / n;
  int i = 0;
  for (Node d : days.as<JsonArrayConst>()) {
    float cx = x0 + cw * (i + 0.5f);
    if (i) c.line(x0 + cw * i, y0 + 2, x0 + cw * i, y1 - 2);
    i++;
    std::string day = str(d["day"]);
    bool today = truthy(d, "today"), important = truthy(d, "important");
    float w = c.tw(day, 24, true);
    if (today) c.block(cx - w / 2 - 6, y0 + 3, cx + w / 2 + 6, y0 + 31);
    c.text(cx, y0, day, 24, true, 'm', today);
    float y = y0 + 36;
    for (Node l : d["rows"].as<JsonArrayConst>())
      for (auto& part : c.wrap(str(l), 18, cw - 8)) {
        c.text(cx, y, part, 18, important, 'm', false, cw - 6);
        y += 22;
      }
    if (truthy(d, "done")) c.check(cx - 8, y + 4, 16);
    if (y > y1 + 4) c.warning("week: " + day + " does not fit the height");
  }
}

}  // namespace kit
