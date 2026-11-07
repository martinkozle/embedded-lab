// Plasma, a demoscene classic: four sine waves (across, down, diagonal and
// rings from a moving centre) added up per pixel, shaded by dithering.
// Integer sine table, so it's fast. Stick: speed and wave size. A: banding.
#include "shade.h"

namespace {

int8_t sine[256];
uint8_t dist[H][W];  // distance from the middle, for the rings
int t = 0, bands = 0;
float scale = 2;

void start() {
  for (int i = 0; i < 256; i++) sine[i] = lroundf(127 * sinf(i * TWO_PI / 256));
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++) dist[y][x] = min(255, (int)lroundf(hypotf(x - W / 2, (y - H / 2) * 1.0f) * 4));
}

void update(const Input &in) {
  if (in.a) { bands = (bands + 1) % 3; play(SND_MOVE); }
  t += 3 + lroundf(in.x * 4);
  scale += (2 + in.y * 1.5f - scale) * 0.1f;
  int k = lroundf(scale * 8);  // fixed point, 8 = 1
  int ox = sine[(t / 2) & 255] / 6, oy = sine[(t / 3 + 64) & 255] / 12;  // ring centre drifts
  oled.clearBuffer();
  uint8_t *buf = oled.getBufferPtr();
  const int period[] = {32, 64, 16};
  int p = period[bands];
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++) {
      int dx = constrain(x + ox, 0, W - 1), dy = constrain(y + oy, 0, H - 1);
      int v = sine[(x * k / 8 + t) & 255] + sine[(y * k * 2 / 8 - t * 2) & 255] +
              sine[((x + y) * k / 8 + t * 3 / 2) & 255] + sine[(dist[dy][dx] * k / 16 - t * 2) & 255];
      int i = ((v + 512) * p / 512) % p;  // wrap into bands
      int s = i < p / 2 ? i * 32 / p : (p - i) * 32 / p;
      plotShade(buf, x, y, min(16, s));
    }
  oled.sendBuffer();
}

}  // namespace

const Game PLASMA = {"Plasma", nullptr, start, update};
