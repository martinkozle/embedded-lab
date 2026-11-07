// Mandelbrot and Julia sets. For each pixel, z -> z^2 + c is repeated until
// |z| > 2 or we give up; the count picks the shade. Mandelbrot: c is the
// pixel, z starts at 0. Julia: z starts at the pixel, c is one fixed number.
// Both CPU cores share the work: the second core takes every other row.
#include "shade.h"

namespace {

uint8_t iters[H][W];  // 255 = never escaped (inside the set)

struct Job {
  float x0, y0, step;  // complex coordinates of pixel (0, 0), and per pixel
  bool julia;
  float jr, ji;
  int maxIter;
} job;

TaskHandle_t worker = nullptr;
SemaphoreHandle_t goSem, doneSem;

void renderRows(int first) {
  const Job j = job;
  for (int y = first; y < H; y += 2) {
    float ci = j.y0 + y * j.step;
    for (int x = 0; x < W; x++) {
      float cr = j.x0 + x * j.step;
      float zr = j.julia ? cr : 0, zi = j.julia ? ci : 0;
      float kr = j.julia ? j.jr : cr, ki = j.julia ? j.ji : ci;
      float zr2 = zr * zr, zi2 = zi * zi;
      int i = 0;
      while (i < j.maxIter && zr2 + zi2 < 4) {
        zi = 2 * zr * zi + ki;
        zr = zr2 - zi2 + kr;
        zr2 = zr * zr;
        zi2 = zi * zi;
        i++;
      }
      iters[y][x] = i >= j.maxIter ? 255 : min(i, 254);
    }
  }
}

void workerTask(void *) {
  for (;;) {
    xSemaphoreTake(goSem, portMAX_DELAY);
    renderRows(1);
    xSemaphoreGive(doneSem);
  }
}

uint32_t render() {  // returns the time it took in ms
  if (!worker) {
    goSem = xSemaphoreCreateBinary();
    doneSem = xSemaphoreCreateBinary();
    xTaskCreatePinnedToCore(workerTask, "fractal", 4096, nullptr, 1, &worker, 0);
  }
  uint32_t t0 = millis();
  xSemaphoreGive(goSem);
  renderRows(0);
  xSemaphoreTake(doneSem, portMAX_DELAY);
  return millis() - t0;
}

// Shades: inside is black; outside, bands that rise and fall with the count
// (a triangle wave), shifted by `cycle` so the bands flow.
void shadeToScreen(int cycle, int bandScale) {
  oled.clearBuffer();
  uint8_t *buf = oled.getBufferPtr();
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++) {
      int i = iters[y][x];
      if (i == 255) continue;
      int v = (i * bandScale + cycle) & 31;
      plotShade(buf, x, y, v < 16 ? v + 1 : 32 - v);
    }
}

void logTime(const char *name, uint32_t ms, int maxIter) {
  static uint32_t last = 0;
  if (millis() - last < 2000) return;
  last = millis();
  Serial.printf("%s: %lu ms per frame, %d max iterations\n", name, (unsigned long)ms, maxIter);
}

// --- Mandelbrot: zoom in on a spot until floats run out of digits, then zoom
// back out, and on to the next spot. A: next spot.
struct Spot { float x, y; const char *name; };
const Spot SPOTS[] = {
    {-0.743643887f, 0.131825904f, "seahorse valley"},
    {-0.77568377f, 0.13646737f, "spiral"},
    {-1.250660f, 0.020120f, "mini brot"},
    {-0.1601f, 1.0377f, "dendrite"},
    {0.2549870f, 0.0005f, "elephant valley"},
};
constexpr int NUM_SPOTS = sizeof SPOTS / sizeof SPOTS[0];
int spot = 0;
float width = 3.5f;  // of the view, in complex units
int zoomDir = 1;
int cycle = 0;

void mStart() { width = 3.5f; zoomDir = 1; }

void mUpdate(const Input &in) {
  if (in.a) { spot = (spot + 1) % NUM_SPOTS; width = 3.5f; zoomDir = 1; play(SND_MOVE); }
  float speed = 1 + max(0.0f, -in.y) * 2;  // stick up: zoom faster
  if (zoomDir > 0) {
    width *= powf(0.965f, speed);
    if (width < 6e-5f) zoomDir = -1;  // floats can't see any finer
  } else {
    width *= 1.12f;
    if (width > 3.5f) { width = 3.5f; zoomDir = 1; spot = (spot + 1) % NUM_SPOTS; }
  }
  float zoom = 3.5f / width;
  job = {SPOTS[spot].x - width / 2, SPOTS[spot].y - width * H / W / 2, width / W, false, 0, 0,
         min(240, 40 + (int)(14 * log2f(zoom)))};
  uint32_t ms = render();
  logTime("mandelbrot", ms, job.maxIter);
  cycle = (cycle + 1) & 31;
  shadeToScreen(cycle, 2);
  char s[24];
  snprintf(s, sizeof s, "x%.0f", zoom);
  drawLabel(1, 6, s);
  if (width > 1.5f) drawLabel(1, H - 1, SPOTS[spot].name);
  oled.sendBuffer();
}

// --- Julia: c travels around a circle of radius 0.7885, where the sets are
// at their most curly. Stick left/right: travel faster or backwards.
float angle = 0;

void jStart() {}

void jUpdate(const Input &in) {
  angle += 0.012f + in.x * 0.05f;
  float cr = 0.7885f * cosf(angle), ci = 0.7885f * sinf(angle);
  constexpr float VIEW = 3.4f;
  job = {-VIEW / 2, -VIEW * H / W / 2, VIEW / W, true, cr, ci, 48};
  uint32_t ms = render();
  logTime("julia", ms, job.maxIter);
  cycle = (cycle + 1) & 31;
  shadeToScreen(cycle, 3);
  char s[32];
  snprintf(s, sizeof s, "c=%.2f%+.2fi", cr, ci);
  drawLabel(1, 6, s);
  oled.sendBuffer();
}

}  // namespace

const Game MANDEL = {"Mandelbrot", nullptr, mStart, mUpdate};
const Game JULIA = {"Julia", nullptr, jStart, jUpdate};
