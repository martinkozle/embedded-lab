// Conway's Game of Life on a 64x32 grid that wraps at the edges. A cell is
// born with exactly 3 neighbours and survives with 2 or 3. When the board
// settles into a loop it reseeds itself. A: reseed now.
#include "shade.h"

namespace {

constexpr int GW = 64, GH = 32, CELL = 2;
uint8_t grid[GH][GW], next[GH][GW];
uint32_t history[8];
int sameFor = 0, gen = 0;

void seed() {
  for (int y = 0; y < GH; y++)
    for (int x = 0; x < GW; x++) grid[y][x] = random(100) < 33;
  sameFor = gen = 0;
}

void start() { seed(); }

void update(const Input &in) {
  if (in.a) { seed(); play(SND_MOVE); }
  uint32_t hash = 2166136261u;
  for (int y = 0; y < GH; y++)
    for (int x = 0; x < GW; x++) {
      int n = 0;
      for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++)
          if (dx || dy) n += grid[(y + dy + GH) % GH][(x + dx + GW) % GW];
      next[y][x] = n == 3 || (n == 2 && grid[y][x]);
      if (next[y][x]) hash = (hash ^ (y * GW + x)) * 16777619u;
    }
  memcpy(grid, next, sizeof grid);
  gen++;

  // Stuck in a loop of up to 8 steps (still lifes, blinkers)? Reseed soon.
  bool repeat = false;
  for (uint32_t h : history) repeat |= h == hash;
  memmove(history + 1, history, sizeof history - sizeof history[0]);
  history[0] = hash;
  sameFor = repeat ? sameFor + 1 : 0;
  if (sameFor > 60) seed();

  oled.clearBuffer();
  for (int y = 0; y < GH; y++)
    for (int x = 0; x < GW; x++)
      if (grid[y][x]) oled.drawBox(x * CELL, y * CELL, CELL, CELL);
  char s[16];
  snprintf(s, sizeof s, "gen %d", gen);
  drawLabel(1, 6, s);
  oled.sendBuffer();
}

}  // namespace

const Game LIFE = {"Life", nullptr, start, update};
