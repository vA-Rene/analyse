// PC stand-in for Seeed_GFX EPaper: 1-bit framebuffer + VLW smooth fonts.
#pragma once
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <vector>
#include <map>
#include <algorithm>
enum { TL_DATUM = 0, TC_DATUM, TR_DATUM, ML_DATUM, MC_DATUM, MR_DATUM, BL_DATUM, BC_DATUM, BR_DATUM };
#define TFT_BLACK 0x0000
#define TFT_WHITE 0xFFFF
struct Glyph { int32_t w, h, adv, dY, dX; const uint8_t* bmp; };
class EPaper {
 public:
  static const int W = 800, H = 480;
  uint8_t px[W * H];  // 0 black, 1 white
  int datum = 0; uint16_t fg = TFT_BLACK, bg = TFT_WHITE;
  std::map<uint32_t, Glyph> glyphs; int yAdvance = 0, maxAscent = 0, maxDescent = 0;
  int overflow = 0;
  static int32_t rd(const uint8_t* p) { return (int32_t)((p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]); }
  void loadFont(const uint8_t* f) {
    glyphs.clear();
    int n = rd(f); yAdvance = rd(f + 8);
    const uint8_t* m = f + 24; const uint8_t* b = m + n * 28;
    maxAscent = maxDescent = 0;
    for (int i = 0; i < n; i++, m += 28) {
      Glyph g{rd(m + 8), rd(m + 4), rd(m + 12), rd(m + 16), rd(m + 20), b};
      uint32_t u = rd(m);
      glyphs[u] = g; b += g.w * g.h;
      if (u > 0x20 && u < 0xA0) { if (g.dY > maxAscent) maxAscent = g.dY; if (g.h - g.dY > maxDescent) maxDescent = g.h - g.dY; }
    }
  }
  static std::vector<uint32_t> utf8(const char* s) {
    std::vector<uint32_t> out;
    for (const unsigned char* p = (const unsigned char*)s; *p;) {
      uint32_t c = *p;
      if (c < 0x80) { p++; }
      else if ((c & 0xE0) == 0xC0) { c = ((c & 0x1F) << 6) | (p[1] & 0x3F); p += 2; }
      else if ((c & 0xF0) == 0xE0) { c = ((c & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F); p += 3; }
      else { c = ((c & 0x07) << 18) | ((p[1] & 0x3F) << 12) | ((p[2] & 0x3F) << 6) | (p[3] & 0x3F); p += 4; }
      out.push_back(c);
    }
    return out;
  }
  std::vector<uint32_t> missing;
  int textWidth(const char* s) { int w = 0; for (uint32_t c : utf8(s)) { auto it = glyphs.find(c); if (it != glyphs.end()) w += it->second.adv; else { w += yAdvance / 4; missing.push_back(c);}
      // NotoSans has no arrows/dingbats: the font file contains only empty boxes for them
      if ((c >= 0x2190 && c <= 0x21FF) || (c >= 0x2700 && c <= 0x27BF)) missing.push_back(c); } return w; }
  int fontHeight() { return yAdvance; }
  void setTextDatum(uint8_t d) { datum = d; }
  void setTextColor(uint16_t f, uint16_t b) { fg = f; bg = b; }
  void drawString(const char* s, int x, int y) {
    int w = textWidth(s), h = yAdvance;
    if (datum % 3 == 1) x -= w / 2; else if (datum % 3 == 2) x -= w;
    if (datum / 3 == 1) y -= h / 2; else if (datum / 3 == 2) y -= h;
    if (x < 0 || x + w > W || y < 0 || y + h > H) overflow++;
    for (uint32_t c : utf8(s)) {
      auto it = glyphs.find(c);
      if (it == glyphs.end()) { x += yAdvance / 4; continue; }
      const Glyph& g = it->second;
      for (int j = 0; j < g.h; j++) for (int i = 0; i < g.w; i++)
        if (g.bmp[j * g.w + i] >= 128) drawPixel(x + g.dX + i, y + maxAscent - g.dY + j, fg);
      x += g.adv;
    }
  }
  void drawPixel(int x, int y, uint16_t c) { if (x >= 0 && y >= 0 && x < W && y < H) px[y * W + x] = c ? 1 : 0; }
  void fillScreen(uint16_t c) { memset(px, c ? 1 : 0, sizeof(px)); }
  void fillRect(int x, int y, int w, int h, uint16_t c) { for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) drawPixel(x + i, y + j, c); }
  void drawFastHLine(int x, int y, int w, uint16_t c) { fillRect(x, y, w, 1, c); }
  void drawFastVLine(int x, int y, int h, uint16_t c) { fillRect(x, y, 1, h, c); }
  void drawRect(int x, int y, int w, int h, uint16_t c) { drawFastHLine(x, y, w, c); drawFastHLine(x, y + h - 1, w, c); drawFastVLine(x, y, h, c); drawFastVLine(x + w - 1, y, h, c); }
  void drawLine(int x0, int y0, int x1, int y1, uint16_t c) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1, dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1, e = dx + dy;
    for (;;) { drawPixel(x0, y0, c); if (x0 == x1 && y0 == y1) break; int e2 = 2 * e; if (e2 >= dy) { e += dy; x0 += sx; } if (e2 <= dx) { e += dx; y0 += sy; } }
  }
  void drawCircle(int cx, int cy, int r, uint16_t c) { for (int a = 0; a < 360 * 4; a++) { double t = a * 3.14159265 / 720; drawPixel(cx + (int)lround(r * cos(t)), cy + (int)lround(r * sin(t)), c); } }
  void fillCircle(int cx, int cy, int r, uint16_t c) { for (int j = -r; j <= r; j++) for (int i = -r; i <= r; i++) if (i * i + j * j <= r * r) drawPixel(cx + i, cy + j, c); }
  void fillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t c) {
    int minx = std::min({x0, x1, x2}), maxx = std::max({x0, x1, x2}), miny = std::min({y0, y1, y2}), maxy = std::max({y0, y1, y2});
    auto e = [](int ax, int ay, int bx, int by, int px, int py) { return (bx - ax) * (py - ay) - (by - ay) * (px - ax); };
    for (int y = miny; y <= maxy; y++) for (int x = minx; x <= maxx; x++) {
      int a = e(x0, y0, x1, y1, x, y), b = e(x1, y1, x2, y2, x, y), d = e(x2, y2, x0, y0, x, y);
      if ((a >= 0 && b >= 0 && d >= 0) || (a <= 0 && b <= 0 && d <= 0)) drawPixel(x, y, c);
    }
  }
  void drawRoundRect(int x, int y, int w, int h, int r, uint16_t c) { drawRect(x, y, w, h, c); }
  void fillRoundRect(int x, int y, int w, int h, int r, uint16_t c) { fillRect(x, y, w, h, c); }
  void drawBitmap(int x, int y, const uint8_t* b, int w, int h, uint16_t f, uint16_t bgc) {
    int bw = (w + 7) / 8;
    for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) drawPixel(x + i, y + j, (b[j * bw + i / 8] & (0x80 >> (i % 8))) ? f : bgc);
  }
  void writePbm(const char* path) {
    FILE* f = fopen(path, "wb"); fprintf(f, "P5\n%d %d\n255\n", W, H);
    for (int i = 0; i < W * H; i++) fputc(px[i] ? 255 : 0, f); fclose(f);
  }
};
