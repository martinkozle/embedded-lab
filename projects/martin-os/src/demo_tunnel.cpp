// Tunnel: an endless zoom through nested polygons. Each polygon sits exactly
// inside the one around it, turned by half a corner (pi/n), which makes it
// smaller by cos(pi/n): triangles halve, squares shrink by 1/sqrt(2). Zooming
// by that factor brings back the same picture, so the zoom never ends.
// Stick: bend the tunnel. A: next shape. Stick press: filled / outlines.
#include "shade.h"

namespace {

const int SIDES[] = {3, 4, 5, 6, 8};
int shape = 0;
bool filled = true;
float t = 0, spin = 0, bendX = 0, bendY = 0;

void start() { t = spin = bendX = bendY = 0; }

void update(const Input &in) {
  if (in.a) { shape = (shape + 1) % 5; play(SND_MOVE); }
  if (in.stickPress) { filled = !filled; play(SND_MOVE); }
  int n = SIDES[shape];
  float shrink = cosf(PI / n);  // inner polygon size / outer
  float lnF = -logf(shrink);    // log of the zoom per ring
  t += 0.035f / (lnF / logf(2.0f));  // same zoom speed for every shape
  if (t >= 2) t -= 2;           // two rings on, the picture repeats exactly
  spin += 0.012f;
  bendX += (curve(in.x) * 30 - bendX) * 0.1f;
  bendY += (curve(in.y) * 18 - bendY) * 0.1f;
  float now = millis() / 1000.0f;
  float wobX = 7 * sinf(now * 0.7f), wobY = 4 * sinf(now * 0.9f);

  // Ring j has radius e^((j + t) * lnF). Draw from the smallest ring that
  // still covers the whole screen down to ones too small to see.
  int jMax = (int)ceilf(logf(80 / shrink) / lnF - t);
  int jMin = (int)floorf(logf(0.8f) / lnF - t);
  oled.clearBuffer();
  for (int j = jMax; j >= jMin; j--) {
    float r = expf((j + t) * lnF);
    float deep = 1 - min(1.0f, r / 70);  // small rings are far away: bend more
    float cx = W / 2 + (bendX + wobX) * deep, cy = H / 2 + (bendY + wobY) * deep;
    float rot = j * PI / n + spin;
    int px[8], py[8];
    for (int k = 0; k < n; k++) {
      float a = rot + k * TWO_PI / n;
      px[k] = lroundf(cx + r * cosf(a));
      py[k] = lroundf(cy + r * sinf(a));
    }
    if (filled) {
      oled.setDrawColor(((j % 2) + 2) % 2);
      for (int k = 0; k < n; k++)
        oled.drawTriangle(lroundf(cx), lroundf(cy), px[k], py[k], px[(k + 1) % n], py[(k + 1) % n]);
    } else {
      oled.setDrawColor(1);
      for (int k = 0; k < n; k++) lineClip(px[k], py[k], px[(k + 1) % n], py[(k + 1) % n]);
    }
  }
  oled.setDrawColor(1);
  oled.sendBuffer();
}

}  // namespace

const Game TUNNEL = {"Tunnel", nullptr, start, update};
