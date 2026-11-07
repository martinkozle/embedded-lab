#include "radial.h"

constexpr float POINT = 0.55f;  // stick past this points at an item
constexpr float SWITCH = 0.6f;  // in item steps: a bit past halfway, so the
                                // pick doesn't flicker on the boundary
constexpr float RING = 22, SMALL = 7, BIG = 9;  // pixels
constexpr float SPIN = 0.3f;    // turns the ring spins while it spirals

static const Note SND_TICK[] = {{2093, 5}, {0, 0}};

void radialTick() {
  static uint32_t last = 0;
  if (millis() - last < 150) return;
  last = millis();
  play(SND_TICK);
}

static float wrapDiff(float d, int n) {  // shortest way round, -n/2..n/2
  while (d > n / 2.0f) d -= n;
  while (d < -n / 2.0f) d += n;
  return d;
}

static float wrapPos(float p, int n) {
  while (p < 0) p += n;
  while (p >= n) p -= n;
  return p;
}

// A spring: overshoots a little and settles, which reads as "alive".
static void spring(float &pos, float &vel, float target, int n, float k, float damp) {
  vel = vel * (1 - damp) + wrapDiff(target - pos, n) * k;
  pos = wrapPos(pos + vel, n);
}

void Radial::reset(int count, int sel) {
  n = constrain(count, 1, RADIAL_MAX);
  selected = sel < n ? sel : 0;
  for (int i = 0; i < n; i++) size[i] = i == selected;
  hPos = pPos = selected;
  hVel = pVel = 0;
  scale = scaleVel = 0;
  scaleTarget = 1;
}

bool Radial::update(const Input &in) {
  bool changed = false;
  bool pointing = scaleTarget > 0 && hypotf(in.x, in.y) > POINT;
  float aim = selected;
  if (pointing) {
    float a = atan2f(in.x, -in.y);  // 0 up, clockwise
    if (a < 0) a += TWO_PI;
    aim = a / (TWO_PI / n);
    if (fabsf(wrapDiff(aim - selected, n)) > SWITCH) {
      selected = (int)lroundf(aim) % n;
      changed = true;
    }
  }
  // Circles near where the stick points grow a little.
  for (int i = 0; i < n; i++) {
    float t = pointing ? max(0.0f, 1 - fabsf(wrapDiff(aim - i, n))) : (i == selected);
    size[i] += (t - size[i]) * 0.45f;
  }
  spring(hPos, hVel, selected, n, 0.3f, 0.45f);
  spring(pPos, pVel, aim, n, 0.35f, 0.4f);  // the hand follows the stick live

  if (scaleTarget > 0) {  // spiral out with a little overshoot
    scaleVel = scaleVel * 0.62f + (1 - scale) * 0.22f;
    scale += scaleVel;
  } else {  // spiral in, quickly and without bounce
    scale += (0 - scale) * 0.38f;
  }
  return changed;
}

// Where item step `i` is, on a ring scaled by s (spinning as it shrinks).
static void ringPos(const Radial &R, float i, float s, int &x, int &y) {
  float a = (i / R.n + SPIN * (1 - s) + R.spin) * TWO_PI;
  float r = RING * s * R.squeeze;
  x = RING_CX + R.shakeX + lroundf(r * sinf(a));
  y = RING_CY + R.shakeY - lroundf(r * cosf(a));
}

void Radial::itemPos(int i, int &x, int &y) const {
  ringPos(*this, i, max(0.0f, scale), x, y);
}

void Radial::draw(void (*inside)(int i, int x, int y)) const {
  float s = max(0.0f, scale);
  if (s < 0.03f) return;
  for (int i = 0; i < n; i++) {
    int x, y;
    ringPos(*this, i, s, x, y);
    oled.drawCircle(x, y, max(1L, lroundf((SMALL + (BIG - SMALL) * size[i]) * min(1.0f, s))));
  }
  // The white fill, slightly smaller while it travels between circles.
  float moving = min(1.0f, fabsf(wrapDiff(selected - hPos, n)));
  int hx, hy;
  ringPos(*this, hPos, s, hx, hy);
  oled.drawDisc(hx, hy, max(1L, lroundf(((SMALL + (BIG - SMALL) * size[selected]) - 2.5f * moving) * min(1.0f, s))));

  // The hand: a needle from the edge of the middle to just short of the
  // circles, spinning along with the ring when it spirals.
  float a = (pPos / n + SPIN * (1 - s) + spin) * TWO_PI;
  float sn = sinf(a), cs = cosf(a);
  float rBase = (RING_FREE_R + 2) * min(1.0f, s);
  float rTip = max(rBase + 2, (RING * squeeze - SMALL - 2) * s);
  int tx = RING_CX + lroundf(rTip * sn), ty = RING_CY - lroundf(rTip * cs);
  int bx = RING_CX + lroundf(rBase * sn), by = RING_CY - lroundf(rBase * cs);
  int ox = lroundf(4 * cs * min(1.0f, s)), oy = lroundf(4 * sn * min(1.0f, s));
  oled.drawTriangle(tx, ty, bx + ox, by + oy, bx - ox, by - oy);
  oled.drawCircle(RING_CX, RING_CY, max(1L, lroundf(RING_FREE_R * min(1.0f, s))));

  if (s > 0.6f) {  // icons and numbers only once there's room for them
    for (int i = 0; i < n; i++) {
      int x, y;
      ringPos(*this, i, s, x, y);
      inside(i, x, y);
    }
  }
  oled.setDrawColor(1);
}

// u8g2_font_6x10 digits are all 5x7 pixels on a 6-pixel step. Circles are an
// odd number of pixels across, so odd-sized glyphs sit exactly in the middle.
void drawNumberAt(int x, int y, int value) {
  char s[4];
  snprintf(s, sizeof s, "%d", constrain(value, 0, 99));
  int w = 6 * strlen(s) - 1;
  oled.setFont(u8g2_font_6x10_tf);
  oled.setFontMode(1);
  oled.setDrawColor(2);
  oled.drawStr(x - w / 2, y + 4, s);  // the 7 rows end 1 above the baseline
  oled.setDrawColor(1);
}

void drawIconAt(int x, int y, const char *const icon[9]) {
  oled.setDrawColor(2);
  for (int row = 0; row < 9; row++)
    for (int col = 0; col < 9; col++)
      if (icon[row][col] == '#') oled.drawPixel(x - 4 + col, y - 4 + row);
  oled.setDrawColor(1);
}
