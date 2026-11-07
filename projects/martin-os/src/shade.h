// Helpers for the demos: writing pixels straight into the screen buffer,
// grey shades by ordered dithering, and lines that may run off screen.
#pragma once
#include "os.h"

// 4x4 Bayer matrix: a pixel with shade s (0..16) is lit when s is above its
// cell, so shade 8 lights half the pixels in an even checker-like pattern.
constexpr uint8_t BAYER[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

// The SSD1306 buffer: 8 pages of 128 bytes; each byte is 8 pixels down.
inline void plot(uint8_t *buf, int x, int y) { buf[(y >> 3) * W + x] |= 1 << (y & 7); }
inline void plotShade(uint8_t *buf, int x, int y, int s) {
  if (s > BAYER[y & 3][x & 3]) plot(buf, x, y);
}

// Bresenham line that skips pixels off screen (u8g2's drawLine wants
// on-screen coordinates).
inline void lineClip(int x0, int y0, int x1, int y1) {
  int dx = abs(x1 - x0), dy = -abs(y1 - y0);
  int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;
  for (int guard = 0; guard < 600; guard++) {
    if (x0 >= 0 && x0 < W && y0 >= 0 && y0 < H) oled.drawPixel(x0, y0);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}

// A label in a black box, for demo names over busy pictures.
inline void drawLabel(int x, int y, const char *s) {
  oled.setFont(u8g2_font_4x6_tf);
  int w = oled.getStrWidth(s);
  oled.setDrawColor(0);
  oled.drawBox(x - 1, y - 6, w + 2, 7);
  oled.setDrawColor(1);
  oled.drawStr(x, y, s);
}
