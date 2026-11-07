// Chaos game: start anywhere, then over and over pick one of a few simple
// maps at random (shrink, turn, shift) and apply it. The points pile up into
// a fractal: Barnsley's fern, Sierpinski's triangle, the Heighway dragon,
// the Levy C curve. Draws on top of itself; a new one every 9 s. A: next.
#include "shade.h"

namespace {

// x' = a x + b y + e,  y' = c x + d y + f, picked with probability p.
struct Map { float a, b, c, d, e, f, p; };
struct Ifs { const char *name; Map maps[4]; int n; bool sideways; };
const Ifs SETS[] = {
    {"fern", {{0, 0, 0, 0.16f, 0, 0, 0.01f}, {0.85f, 0.04f, -0.04f, 0.85f, 0, 1.6f, 0.85f},
              {0.2f, -0.26f, 0.23f, 0.22f, 0, 1.6f, 0.07f}, {-0.15f, 0.28f, 0.26f, 0.24f, 0, 0.44f, 0.07f}}, 4, true},
    {"sierpinski", {{0.5f, 0, 0, 0.5f, 0, 0, 0.34f}, {0.5f, 0, 0, 0.5f, 0.5f, 0, 0.33f},
                    {0.5f, 0, 0, 0.5f, 0.25f, 0.433f, 0.33f}}, 3, false},
    {"dragon", {{0.5f, -0.5f, 0.5f, 0.5f, 0, 0, 0.5f}, {-0.5f, -0.5f, 0.5f, -0.5f, 1, 0, 0.5f}}, 2, false},
    {"levy C", {{0.5f, -0.5f, 0.5f, 0.5f, 0, 0, 0.5f}, {0.5f, 0.5f, -0.5f, 0.5f, 0.5f, 0.5f, 0.5f}}, 2, false},
};
constexpr int NUM_SETS = sizeof SETS / sizeof SETS[0];
int set = 0;
float px = 0, py = 0, minX, maxX, minY, maxY, scale, offX, offY;
uint32_t setSince = 0;
bool fresh = true;

void step() {
  const Ifs &s = SETS[set];
  float r = random(0, 10000) / 10000.0f;
  int k = 0;
  while (k < s.n - 1 && r > s.maps[k].p) { r -= s.maps[k].p; k++; }
  const Map &m = s.maps[k];
  float nx = m.a * px + m.b * py + m.e;
  py = m.c * px + m.d * py + m.f;
  px = nx;
}

void begin() {
  px = py = 0;
  for (int i = 0; i < 30; i++) step();  // settle onto the fractal
  minX = minY = 1e9; maxX = maxY = -1e9;
  for (int i = 0; i < 4000; i++) {  // measure it, to fit it on screen
    step();
    float x = SETS[set].sideways ? py : px, y = SETS[set].sideways ? px : py;
    minX = min(minX, x); maxX = max(maxX, x); minY = min(minY, y); maxY = max(maxY, y);
  }
  scale = min((W - 4) / (maxX - minX), (H - 4) / (maxY - minY));
  offX = (W - (maxX - minX) * scale) / 2;
  offY = (H - (maxY - minY) * scale) / 2;
  setSince = millis();
  fresh = true;
}

void start() { begin(); }

void update(const Input &in) {
  if (in.a || millis() - setSince > 9000) {
    set = (set + 1) % NUM_SETS;
    begin();
    if (in.a) play(SND_MOVE);
  }
  if (fresh) { oled.clearBuffer(); fresh = false; }
  uint8_t *buf = oled.getBufferPtr();
  for (int i = 0; i < 350; i++) {
    step();
    float x = SETS[set].sideways ? py : px, y = SETS[set].sideways ? px : py;
    int sx = lroundf(offX + (x - minX) * scale), sy = lroundf(H - 1 - (offY + (y - minY) * scale));
    if (sx >= 0 && sx < W && sy >= 0 && sy < H) plot(buf, sx, sy);
  }
  drawLabel(1, 6, SETS[set].name);
  oled.sendBuffer();
}

}  // namespace

const Game CHAOS = {"Chaos", nullptr, start, update};
