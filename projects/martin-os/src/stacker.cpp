// Stacker: a falling-blocks puzzle. Pieces made of four squares drop into a
// 10 x 20 well; fill a whole row to clear it. Every 10 rows the level goes up
// and pieces fall faster. The game ends when a new piece has no room.
//
// Controls:
//   stick left/right   move (hold to keep sliding)
//   stick down         drop faster (1 point per row)
//   flick stick up     drop straight down and lock (2 points per row)
//   button A           rotate clockwise
//   stick press        put the piece in the hold box / swap it back
// Points for rows cleared at once: 1 = 100, 2 = 300, 3 = 500, 4 = 800,
// times the level.
#include "os.h"

namespace {

constexpr const char *NS = "stacker";
constexpr int BW = 10, BH = 20;      // well size in cells
constexpr int CELL = 3;              // pixels per cell (2 px block + 1 px gap)
constexpr int BX = 49, BY = 2;       // top-left pixel of the well
constexpr uint32_t DAS_DELAY = 170;  // hold this long before sliding repeats
constexpr uint32_t DAS_REPEAT = 55;
constexpr uint32_t SOFT_DROP_MS = 35;
constexpr uint32_t LOCK_MS = 450;    // time on the ground before a piece locks
constexpr int MAX_LOCK_RESETS = 12;  // moving on the ground restarts the timer
constexpr uint32_t CLEAR_MS = 250;   // rows flash this long before vanishing

// The 7 pieces, as cells in their starting rotation inside a size x size box.
struct PieceDef {
  uint8_t size;
  int8_t cells[4][2];
};
const PieceDef DEFS[7] = {
    {4, {{0, 1}, {1, 1}, {2, 1}, {3, 1}}},  // straight
    {2, {{0, 0}, {1, 0}, {0, 1}, {1, 1}}},  // square
    {3, {{1, 0}, {0, 1}, {1, 1}, {2, 1}}},  // tee
    {3, {{1, 0}, {2, 0}, {0, 1}, {1, 1}}},  // step right
    {3, {{0, 0}, {1, 0}, {1, 1}, {2, 1}}},  // step left
    {3, {{0, 0}, {0, 1}, {1, 1}, {2, 1}}},  // hook left
    {3, {{2, 0}, {0, 1}, {1, 1}, {2, 1}}},  // hook right
};

uint8_t well[BH][BW];  // 0 = empty, otherwise filled

struct Piece {
  int type, rot, x, y;
};
Piece cur;
int nextType, holdType;  // holdType -1 = empty
bool holdUsed;           // you may hold once per piece

uint8_t bag[7];  // pieces come in shuffled sets of all seven
int bagPos = 7;

int score, best, lines, level;
bool over, newBest;
uint32_t lastFall, lockStart, clearStart, dasAt;
int lockResets, dasDir;
uint32_t fullRows;  // bit per row, while rows are flashing

const Note SND_ROT[] = {{1175, 12}, {0, 0}};
const Note SND_LOCK[] = {{196, 25}, {0, 0}};
const Note SND_LINE[] = {{784, 50}, {1047, 80}, {0, 0}};
const Note SND_FOUR[] = {{784, 60}, {988, 60}, {1175, 60}, {1568, 160}, {0, 0}};

int nextFromBag() {
  if (bagPos >= 7) {
    for (int i = 0; i < 7; i++) bag[i] = i;
    for (int i = 6; i > 0; i--) {  // shuffle
      int j = random(i + 1);
      uint8_t t = bag[i];
      bag[i] = bag[j];
      bag[j] = t;
    }
    bagPos = 0;
  }
  return bag[bagPos++];
}

// The piece's 4 cells in well coordinates. Rotating clockwise maps a cell
// (x, y) in its box to (size - 1 - y, x).
void cellsOf(const Piece &p, int out[4][2]) {
  const PieceDef &d = DEFS[p.type];
  for (int i = 0; i < 4; i++) {
    int x = d.cells[i][0], y = d.cells[i][1];
    for (int r = 0; r < p.rot; r++) {
      int t = x;
      x = d.size - 1 - y;
      y = t;
    }
    out[i][0] = p.x + x;
    out[i][1] = p.y + y;
  }
}

bool fits(const Piece &p) {
  int c[4][2];
  cellsOf(p, c);
  for (auto &q : c) {
    if (q[0] < 0 || q[0] >= BW || q[1] >= BH) return false;
    if (q[1] >= 0 && well[q[1]][q[0]]) return false;  // above the top is free
  }
  return true;
}

uint32_t fallMs() { return max<int>(70, 800 - (level - 1) * 70); }

bool spawn(int type) {
  cur = {type, 0, BW / 2 - DEFS[type].size / 2, type == 0 ? -1 : 0};
  lockStart = 0;
  lockResets = 0;
  lastFall = millis();
  return fits(cur);
}

void endRound() {
  over = true;
  if (score > best) {
    best = score;
    newBest = true;
    saveBest(NS, best);
  }
  Serial.printf("stacker over: score %d, %d lines (best %d)\n", score, lines, best);
  play(newBest ? SND_BEST : SND_OVER);
}

void spawnNext() {
  int t = nextType;
  nextType = nextFromBag();
  holdUsed = false;
  if (!spawn(t)) endRound();
}

void start() {
  memset(well, 0, sizeof well);
  bagPos = 7;
  score = lines = 0;
  level = 1;
  best = loadBest(NS);
  over = newBest = false;
  holdType = -1;
  fullRows = 0;
  dasDir = 0;
  nextType = nextFromBag();
  spawnNext();
  play(SND_START);
}

// A successful move while resting on the ground restarts the lock timer, a
// limited number of times, so you can slide a piece into place.
void touched() {
  if (lockStart && lockResets < MAX_LOCK_RESETS) {
    lockStart = millis();
    lockResets++;
  }
}

bool tryMove(int dx, int dy) {
  Piece p = cur;
  p.x += dx;
  p.y += dy;
  if (!fits(p)) return false;
  cur = p;
  return true;
}

void rotate() {
  Piece p = cur;
  p.rot = (p.rot + 1) % 4;
  // If it doesn't fit, nudge it sideways or up ("wall kicks").
  const int kicks[][2] = {{0, 0}, {-1, 0}, {1, 0}, {-2, 0}, {2, 0}, {0, -1}};
  for (auto &k : kicks) {
    Piece q = p;
    q.x += k[0];
    q.y += k[1];
    if (fits(q)) {
      cur = q;
      touched();
      play(SND_ROT);
      return;
    }
  }
}

void lockPiece() {
  int c[4][2];
  cellsOf(cur, c);
  bool aboveTop = false;
  for (auto &q : c) {
    if (q[1] < 0) aboveTop = true;
    else well[q[1]][q[0]] = cur.type + 1;
  }
  if (aboveTop) {  // locked sticking out of the well
    endRound();
    return;
  }
  fullRows = 0;
  for (int y = 0; y < BH; y++) {
    bool full = true;
    for (int x = 0; x < BW && full; x++) full = well[y][x] != 0;
    if (full) fullRows |= 1u << y;
  }
  if (fullRows) {
    clearStart = millis();
    int n = __builtin_popcount(fullRows);
    play(n == 4 ? SND_FOUR : SND_LINE);
  } else {
    play(SND_LOCK);
    spawnNext();
  }
}

void finishClear() {
  int n = 0;
  for (int y = 0; y < BH; y++) {
    if (!(fullRows & (1u << y))) continue;
    n++;
    for (int yy = y; yy > 0; yy--) memcpy(well[yy], well[yy - 1], BW);
    memset(well[0], 0, BW);
  }
  fullRows = 0;
  const int points[] = {0, 100, 300, 500, 800};
  score += points[n] * level;
  int oldLevel = level;
  lines += n;
  level = 1 + lines / 10;
  if (level > oldLevel) play(SND_START);
  spawnNext();
}

void hardDrop() {
  int rows = 0;
  while (tryMove(0, 1)) rows++;
  score += rows * 2;
  lockPiece();
}

void hold() {
  if (holdUsed) return;
  holdUsed = true;
  play(SND_ROT);
  if (holdType < 0) {
    holdType = cur.type;
    int t = nextType;
    nextType = nextFromBag();
    if (!spawn(t)) endRound();
  } else {
    int t = holdType;
    holdType = cur.type;
    if (!spawn(t)) endRound();
  }
  holdUsed = true;
}

// --- Drawing --------------------------------------------------------------------
void drawCell(int px, int py, bool ghost) {
  if (ghost) oled.drawPixel(px, py);  // the landing preview: one dot per cell
  else oled.drawBox(px, py, CELL - 1, CELL - 1);
}

void drawMini(int type, int cx, int cy) {  // a piece centred at (cx, cy)
  if (type < 0) return;
  const PieceDef &d = DEFS[type];
  int minX = 9, maxX = -1, minY = 9, maxY = -1;
  for (auto &c : d.cells) {
    minX = min<int>(minX, c[0]); maxX = max<int>(maxX, c[0]);
    minY = min<int>(minY, c[1]); maxY = max<int>(maxY, c[1]);
  }
  int w = (maxX - minX + 1) * CELL, h = (maxY - minY + 1) * CELL;
  for (auto &c : d.cells)
    drawCell(cx - w / 2 + (c[0] - minX) * CELL, cy - h / 2 + (c[1] - minY) * CELL, false);
}

void draw() {
  oled.clearBuffer();
  oled.drawFrame(BX - 2, BY - 2, BW * CELL + 3, BH * CELL + 3);

  bool flashOn = (millis() / 60) % 2;
  for (int y = 0; y < BH; y++) {
    bool flashing = fullRows & (1u << y);
    if (flashing && !flashOn) continue;
    for (int x = 0; x < BW; x++)
      if (well[y][x]) drawCell(BX + x * CELL, BY + y * CELL, false);
  }

  if (!fullRows && !over) {
    Piece g = cur;  // ghost: where the piece would land
    while (true) {
      Piece n = g;
      n.y++;
      if (!fits(n)) break;
      g = n;
    }
    int c[4][2];
    cellsOf(g, c);
    for (auto &q : c)
      if (q[1] >= 0) drawCell(BX + q[0] * CELL, BY + q[1] * CELL, true);
    cellsOf(cur, c);
    for (auto &q : c)
      if (q[1] >= 0) drawCell(BX + q[0] * CELL, BY + q[1] * CELL, false);
  }

  oled.setFont(u8g2_font_5x8_tf);
  // Left side: hold box and score.
  oled.drawStr(2, 8, "HOLD");
  oled.drawFrame(1, 10, 22, 16);
  if (holdUsed && holdType >= 0 && (frame / 8) % 2) {
    // dimmed: can't swap again until the next piece
  } else {
    drawMini(holdType, 12, 18);
  }
  oled.drawStr(2, 38, "SCORE");
  char buf[12];
  snprintf(buf, sizeof buf, "%d", score);
  oled.drawStr(2, 47, buf);
  snprintf(buf, sizeof buf, "hi %d", best);
  oled.drawStr(2, 60, buf);

  // Right side: next piece, level and lines.
  int rx = BX + BW * CELL + 6;
  oled.drawStr(rx + 1, 8, "NEXT");
  oled.drawFrame(rx, 10, 22, 16);
  drawMini(nextType, rx + 11, 18);
  snprintf(buf, sizeof buf, "LV %d", level);
  oled.drawStr(rx, 38, buf);
  snprintf(buf, sizeof buf, "LN %d", lines);
  oled.drawStr(rx, 48, buf);
  oled.sendBuffer();
}

void update(const Input &in) {
  if (over) {
    if (in.press) start();
    drawGameOver(score, best, newBest);
    return;
  }
  uint32_t now = millis();

  if (fullRows) {  // rows flashing before they vanish
    if (now - clearStart >= CLEAR_MS) finishClear();
    draw();
    return;
  }

  if (in.a) rotate();
  if (in.press && !in.a) hold();
  if (in.up) {
    hardDrop();
    if (over) return;
    draw();
    return;
  }

  // Sideways: one step on the flick, then repeat while held.
  int dir = in.x > 0.5f ? 1 : in.x < -0.5f ? -1 : 0;
  if (dir != dasDir) {
    dasDir = dir;
    if (dir && tryMove(dir, 0)) touched();
    dasAt = now + DAS_DELAY;
  } else if (dir && now >= dasAt) {
    if (tryMove(dir, 0)) touched();
    dasAt = now + DAS_REPEAT;
  }

  // Falling, faster while the stick is held down.
  bool soft = in.y > 0.5f;
  uint32_t interval = soft ? min(SOFT_DROP_MS, fallMs()) : fallMs();
  if (now - lastFall >= interval) {
    lastFall = now;
    if (tryMove(0, 1)) {
      if (soft) score += 1;
      lockStart = 0;
    } else if (!lockStart) {
      lockStart = now;
    }
  }
  // Resting on something: lock after a moment unless it can fall again.
  if (lockStart) {
    Piece below = cur;
    below.y++;
    if (fits(below)) lockStart = 0;
    else if (now - lockStart >= LOCK_MS) {
      lockPiece();
      if (over) return;
    }
  }
  draw();
}

}  // namespace

const Game STACKER = {"Stacker", NS, start, update};
