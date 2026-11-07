// Solids: spinning wireframes with perspective. Cube, octahedron,
// icosahedron, and a tesseract: a 4D cube, turned in 4D then projected to
// 3D and on to the screen, so it seems to turn itself inside out.
// Stick: give it a spin. A: next shape.
#include "shade.h"

namespace {

constexpr int MAXV = 16, MAXE = 32;
float vx[MAXV], vy[MAXV], vz[MAXV], vw[MAXV];
int nv = 0, ne = 0;
uint8_t e0[MAXE], e1[MAXE];
int shape = 0;
const char *const NAMES[] = {"cube", "octahedron", "icosahedron", "tesseract"};
float ax = 0.3f, ay = 0, az = 0, aw = 0, velX = 0.012f, velY = 0.02f;

void addV(float x, float y, float z, float w = 0) {
  vx[nv] = x; vy[nv] = y; vz[nv] = z; vw[nv] = w; nv++;
}

// Edges join the closest pairs of corners: true for all of these shapes.
void makeEdges() {
  float best = 1e9;
  for (int i = 0; i < nv; i++)
    for (int j = i + 1; j < nv; j++) {
      float d = sq(vx[i] - vx[j]) + sq(vy[i] - vy[j]) + sq(vz[i] - vz[j]) + sq(vw[i] - vw[j]);
      best = min(best, d);
    }
  ne = 0;
  for (int i = 0; i < nv; i++)
    for (int j = i + 1; j < nv && ne < MAXE; j++) {
      float d = sq(vx[i] - vx[j]) + sq(vy[i] - vy[j]) + sq(vz[i] - vz[j]) + sq(vw[i] - vw[j]);
      if (d < best * 1.01f) { e0[ne] = i; e1[ne] = j; ne++; }
    }
}

void build() {
  nv = 0;
  if (shape == 0) {
    for (int i = 0; i < 8; i++) addV(i & 1 ? 1 : -1, i & 2 ? 1 : -1, i & 4 ? 1 : -1);
  } else if (shape == 1) {
    for (int s = -1; s <= 1; s += 2) { addV(1.5f * s, 0, 0); addV(0, 1.5f * s, 0); addV(0, 0, 1.5f * s); }
  } else if (shape == 2) {
    const float P = 1.618034f, K = 0.85f;  // golden ratio; K sizes it
    for (int a = -1; a <= 1; a += 2)
      for (int b = -1; b <= 1; b += 2) {
        addV(0, a * K, b * P * K); addV(a * K, b * P * K, 0); addV(b * P * K, 0, a * K);
      }
  } else {
    for (int i = 0; i < 16; i++)
      addV(i & 1 ? 1 : -1, i & 2 ? 1 : -1, i & 4 ? 1 : -1, i & 8 ? 1 : -1);
  }
  makeEdges();
}

void start() { build(); }

void rot(float &a, float &b, float ang) {
  float c = cosf(ang), s = sinf(ang), na = a * c - b * s;
  b = a * s + b * c;
  a = na;
}

void update(const Input &in) {
  if (in.a) { shape = (shape + 1) % 4; build(); play(SND_MOVE); }
  velY += (0.02f + in.x * 0.08f - velY) * 0.08f;
  velX += (0.012f + in.y * 0.08f - velX) * 0.08f;
  ax += velX; ay += velY; az += 0.004f; aw += 0.015f;

  int sx[MAXV], sy[MAXV];
  for (int i = 0; i < nv; i++) {
    float x = vx[i], y = vy[i], z = vz[i], w = vw[i];
    if (shape == 3) {  // turn in the planes that involve the 4th axis
      rot(x, w, aw);
      rot(z, w, aw * 0.7f);
      float f4 = 2.2f / (3.2f - w);  // 4D to 3D: far in w looks smaller
      x *= f4; y *= f4; z *= f4;
    }
    rot(y, z, ax);
    rot(x, z, ay);
    rot(x, y, az);
    float f = 46 / (z + 5);  // 3D to 2D perspective
    sx[i] = lroundf(W / 2 + x * f);
    sy[i] = lroundf(H / 2 + y * f);
  }
  oled.clearBuffer();
  for (int e = 0; e < ne; e++) lineClip(sx[e0[e]], sy[e0[e]], sx[e1[e]], sy[e1[e]]);
  for (int i = 0; i < nv; i++)
    if (sx[i] > 0 && sx[i] < W - 1 && sy[i] > 0 && sy[i] < H - 1) oled.drawDisc(sx[i], sy[i], 1);
  drawLabel(1, 6, NAMES[shape]);
  oled.sendBuffer();
}

}  // namespace

const Game SOLIDS = {"Solids", nullptr, start, update};
