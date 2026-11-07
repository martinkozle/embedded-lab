// Bricks: a brick-breaker with generated levels.
//
// Steer the paddle with the stick, press A (or the stick) to launch. Where
// the ball meets the paddle sets its angle: the edges send it off sideways.
// Clear every breakable brick to go to the next level.
//
// Bricks: solid = 1 hit, striped = 2 hits, hatched steel = can't break.
// Broken bricks sometimes drop a capsule; catch it with the paddle:
//   W wider paddle   M two extra balls   S slower balls   + extra life
//
// Each level is generated: a pattern style (scatter, stripes, diamond,
// checker, pyramid, columns) is picked at random, then filled left-to-right
// and mirrored so the layout is symmetric. Later levels get more rows, more
// tough bricks, steel, and a faster ball.
#include "os.h"

namespace {

constexpr const char *NS = "bricks";
constexpr int COLS = 15, MAX_ROWS = 8;
constexpr int BW = 8, BH = 4;   // brick size in pixels
constexpr int FX = 4, FY = 12;  // top-left of the brick field
constexpr int TOP = 9;          // the ball bounces off this line (below HUD)
constexpr int PY = 60;          // paddle top
constexpr int PADDLE_W = 16, WIDE_W = 26;
constexpr float PADDLE_SPEED = 4.0f;
constexpr int MAX_BALLS = 3, MAX_CAPS = 3;
constexpr int MAX_LIVES = 5;
constexpr uint32_t POWER_MS = 10000;

enum Brick : uint8_t { NONE = 0, NORMAL, HARD, CRACKED, STEEL };
uint8_t grid[MAX_ROWS][COLS];

struct Ball {
  bool alive;
  float x, y, vx, vy;
};
Ball balls[MAX_BALLS];

enum CapKind : uint8_t { CAP_WIDE, CAP_MULTI, CAP_SLOW, CAP_LIFE };
struct Cap {
  bool alive;
  CapKind kind;
  float x, y;
};
Cap caps[MAX_CAPS];

float paddleX;  // centre of the paddle
bool stuck;     // ball resting on the paddle, waiting for launch
int level, lives, score, best;
bool over, newBest;
uint32_t wideUntil, slowUntil, clearedAt;
bool clearing;

const Note SND_PADDLE[] = {{523, 20}, {0, 0}};
const Note SND_BRICK[] = {{988, 25}, {0, 0}};
const Note SND_TOUGH[] = {{659, 25}, {0, 0}};
const Note SND_STEEL[] = {{262, 25}, {0, 0}};
const Note SND_WALL[] = {{392, 10}, {0, 0}};
const Note SND_LOST[] = {{330, 100}, {247, 100}, {196, 200}, {0, 0}};

int paddleW() { return millis() < wideUntil ? WIDE_W : PADDLE_W; }

float ballSpeed() {
  float s = min(2.6f, 1.7f + level * 0.1f);
  return millis() < slowUntil ? s * 0.65f : s;
}

int breakableLeft() {
  int n = 0;
  for (auto &row : grid)
    for (uint8_t b : row)
      if (b != NONE && b != STEEL) n++;
  return n;
}

// --- Level generation ---------------------------------------------------------
void generate() {
  memset(grid, NONE, sizeof grid);
  int rows = min(MAX_ROWS, 4 + level / 2);
  int style = random(6);
  int hardPct = min(45, 5 + level * 5);
  int steelPct = level >= 3 ? min(12, level * 2) : 0;
  const int MID = COLS / 2;  // column 7 is the mirror line
  for (int r = 0; r < rows; r++) {
    for (int c = 0; c <= MID; c++) {
      int fromMid = MID - c;
      bool on;
      switch (style) {
        case 0: on = random(100) < 55 + level * 3; break;                     // scatter
        case 1: on = r % 2 == 0 || random(100) < 15; break;                   // stripes
        case 2: on = fromMid + abs(r - rows / 2) * 2 <= 6 + (int)random(2); break;  // diamond
        case 3: on = (r + c) % 2 == 0; break;                                 // checker
        case 4: on = fromMid <= r + 1; break;                                 // pyramid
        default: on = c % 3 != 2; break;                                      // columns
      }
      if (!on) continue;
      uint8_t b = NORMAL;
      int roll = random(100);
      if (roll < steelPct && r > 0 && r < rows - 1) b = STEEL;
      else if (roll < steelPct + hardPct) b = HARD;
      grid[r][c] = b;
      grid[r][COLS - 1 - c] = b;
    }
  }
  // Too sparse? Sprinkle in more normal bricks (still mirrored).
  for (int tries = 0; breakableLeft() < 14 && tries < 200; tries++) {
    int r = random(rows), c = random(MID + 1);
    if (grid[r][c] == NONE) grid[r][c] = grid[r][COLS - 1 - c] = NORMAL;
  }
  Serial.printf("bricks level %d: style %d, %d rows, %d to break\n", level, style, rows,
                breakableLeft());
}

void resetBall() {
  for (auto &b : balls) b.alive = false;
  for (auto &c : caps) c.alive = false;
  balls[0] = {true, paddleX, PY - 2.0f, 0, 0};
  stuck = true;
  wideUntil = slowUntil = 0;
}

void start() {
  level = 1;
  lives = 3;
  score = 0;
  best = loadBest(NS);
  over = newBest = clearing = false;
  paddleX = W / 2;
  generate();
  resetBall();
  play(SND_START);
}

void endRound() {
  over = true;
  if (score > best) {
    best = score;
    newBest = true;
    saveBest(NS, best);
  }
  Serial.printf("bricks over: score %d (best %d)\n", score, best);
  play(newBest ? SND_BEST : SND_OVER);
}

// --- Ball physics ---------------------------------------------------------------
// Returns the brick cell under (x, y), or nullptr.
uint8_t *brickAt(float x, float y) {
  int c = (int)floorf((x - FX) / BW), r = (int)floorf((y - FY) / BH);
  if (c < 0 || c >= COLS || r < 0 || r >= MAX_ROWS) return nullptr;
  return grid[r][c] != NONE ? &grid[r][c] : nullptr;
}

void dropCap(float x, float y) {
  if (random(100) >= 14) return;
  for (auto &c : caps) {
    if (c.alive) continue;
    int roll = random(100);
    CapKind k = roll < 35 ? CAP_WIDE : roll < 65 ? CAP_MULTI : roll < 90 ? CAP_SLOW : CAP_LIFE;
    c = {true, k, x, y};
    return;
  }
}

void hitBrick(uint8_t *b, float x, float y) {
  switch (*b) {
    case STEEL: play(SND_STEEL); return;
    case HARD: *b = CRACKED; score += 5; play(SND_TOUGH); return;
    default:
      score += *b == CRACKED ? 15 : 10;
      *b = NONE;
      play(SND_BRICK);
      dropCap(x, y);
  }
}

// Keeps the speed constant and the angle away from flat horizontal, which
// would make the ball crawl side to side forever.
void normalize(Ball &b) {
  float s = ballSpeed();
  float len = sqrtf(b.vx * b.vx + b.vy * b.vy);
  if (len < 0.01f) return;
  b.vx = b.vx / len * s;
  b.vy = b.vy / len * s;
  float minVy = s * 0.35f;
  if (fabsf(b.vy) < minVy) {
    b.vy = b.vy < 0 ? -minVy : minVy;
    float vx2 = s * s - minVy * minVy;
    b.vx = (b.vx < 0 ? -1 : 1) * sqrtf(vx2);
  }
}

void moveBall(Ball &b) {
  normalize(b);
  // Small sub-steps so a fast ball can't skip through a brick.
  int steps = (int)ceilf(max(fabsf(b.vx), fabsf(b.vy)));
  for (int i = 0; i < steps && b.alive; i++) {
    float nx = b.x + b.vx / steps;
    if (nx < 1 || nx > W - 2) {
      b.vx = -b.vx;
      play(SND_WALL);
    } else if (uint8_t *br = brickAt(nx, b.y)) {
      b.vx = -b.vx;
      hitBrick(br, nx, b.y);
    } else {
      b.x = nx;
    }
    float ny = b.y + b.vy / steps;
    if (ny < TOP + 1) {
      b.vy = fabsf(b.vy);
      play(SND_WALL);
    } else if (uint8_t *br = brickAt(b.x, ny)) {
      b.vy = -b.vy;
      hitBrick(br, b.x, ny);
    } else if (b.vy > 0 && ny >= PY - 1 && b.y < PY && fabsf(b.x - paddleX) <= paddleW() / 2 + 1) {
      // Off the paddle: -1 at the left end, +1 at the right, up to 60 degrees.
      float off = constrain((b.x - paddleX) / (paddleW() / 2.0f), -1.0f, 1.0f);
      float a = off * 1.05f;
      float s = ballSpeed();
      b.vx = sinf(a) * s;
      b.vy = -cosf(a) * s;
      play(SND_PADDLE);
    } else {
      b.y = ny;
    }
    if (b.y > H + 2) b.alive = false;
  }
}

// --- Frame ------------------------------------------------------------------------
void draw() {
  oled.clearBuffer();
  oled.setFont(u8g2_font_5x8_tf);
  oled.setCursor(0, 7);
  oled.printf("%d", score);
  char mid[12];
  snprintf(mid, sizeof mid, "L%d", level);
  drawCentered(7, mid);
  for (int i = 0; i < lives; i++) oled.drawDisc(W - 3 - i * 6, 4, 2);
  oled.drawHLine(0, TOP - 1, W);

  for (int r = 0; r < MAX_ROWS; r++)
    for (int c = 0; c < COLS; c++) {
      int x = FX + c * BW, y = FY + r * BH;
      switch (grid[r][c]) {
        case NORMAL: oled.drawBox(x, y, BW - 1, BH - 1); break;
        case HARD:  // solid with a dark stripe through the middle
          oled.drawBox(x, y, BW - 1, BH - 1);
          oled.setDrawColor(0);
          oled.drawHLine(x + 1, y + 1, BW - 3);
          oled.setDrawColor(1);
          break;
        case CRACKED: oled.drawFrame(x, y, BW - 1, BH - 1); break;
        case STEEL:  // frame with a hatch
          oled.drawFrame(x, y, BW - 1, BH - 1);
          for (int k = 1; k < BW - 2; k += 2) oled.drawPixel(x + k, y + 1);
          break;
      }
    }

  oled.setFont(u8g2_font_4x6_tf);
  for (auto &c : caps) {
    if (!c.alive) continue;
    int x = (int)c.x - 4, y = (int)c.y - 3;
    oled.drawRFrame(x, y, 9, 7, 2);
    const char *label = c.kind == CAP_WIDE ? "W" : c.kind == CAP_MULTI ? "M" : c.kind == CAP_SLOW ? "S" : "+";
    oled.drawStr(x + 3, y + 6, label);
  }

  int pw = paddleW();
  oled.drawRBox((int)paddleX - pw / 2, PY, pw, 3, 1);
  for (auto &b : balls)
    if (b.alive) oled.drawBox((int)b.x - 1, (int)b.y - 1, 2, 2);

  if (stuck && (frame / 12) % 2 == 0) {
    oled.setFont(u8g2_font_5x8_tf);
    drawCentered(54, "press A");
  }
  if (clearing) {
    oled.setDrawColor(0);
    oled.drawBox(24, 24, 80, 16);
    oled.setDrawColor(1);
    oled.drawFrame(24, 24, 80, 16);
    oled.setFont(u8g2_font_5x8_tf);
    drawCentered(35, "LEVEL CLEAR!");
  }
  oled.sendBuffer();
}

void update(const Input &in) {
  if (over) {
    if (in.press) start();
    drawGameOver(score, best, newBest);
    return;
  }
  uint32_t now = millis();

  if (clearing) {  // short pause, then the next level
    if (now - clearedAt > 1500) {
      clearing = false;
      level++;
      generate();
      resetBall();
    }
    draw();
    return;
  }

  int pw = paddleW();
  paddleX = constrain(paddleX + curve(in.x) * PADDLE_SPEED, pw / 2.0f, W - pw / 2.0f);

  if (stuck) {
    balls[0].x = paddleX;
    balls[0].y = PY - 2;
    if (in.press) {  // launch slightly to one side so it isn't straight up
      stuck = false;
      float a = random(2) ? 0.35f : -0.35f;
      balls[0].vx = sinf(a);
      balls[0].vy = -cosf(a);
      play(SND_PADDLE);
    }
  } else {
    for (auto &b : balls)
      if (b.alive) moveBall(b);
  }

  for (auto &c : caps) {
    if (!c.alive) continue;
    c.y += 0.7f;
    if (c.y > H + 4) {
      c.alive = false;
    } else if (c.y >= PY - 2 && c.y <= PY + 3 && fabsf(c.x - paddleX) <= paddleW() / 2 + 4) {
      c.alive = false;
      play(SND_COIN);
      switch (c.kind) {
        case CAP_WIDE: wideUntil = now + POWER_MS; break;
        case CAP_SLOW: slowUntil = now + POWER_MS; break;
        case CAP_LIFE: lives = min(MAX_LIVES, lives + 1); break;
        case CAP_MULTI: {
          const Ball *src = nullptr;
          for (auto &b : balls)
            if (b.alive) src = &b;
          if (!src || stuck) break;
          Ball base = *src;
          float spread[] = {-0.5f, 0.5f};
          int k = 0;
          for (auto &b : balls) {
            if (b.alive || k >= 2) continue;
            float a = atan2f(base.vx, -base.vy) + spread[k++];
            b = {true, base.x, base.y, sinf(a), -cosf(a)};
          }
          break;
        }
      }
    }
  }

  bool anyBall = false;
  for (auto &b : balls) anyBall |= b.alive;
  if (!anyBall) {
    lives--;
    if (lives <= 0) {
      endRound();
      return;
    }
    play(SND_LOST);
    resetBall();
  }

  if (breakableLeft() == 0) {
    score += 100;
    clearing = true;
    clearedAt = now;
    play(SND_CLEAR);
  }
  draw();
}

}  // namespace

const Game BRICKS = {"Bricks", NS, start, update};
