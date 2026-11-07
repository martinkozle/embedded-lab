// Snake: tilt the stick to turn. Eat the dots to grow and score; each one
// also makes the snake a little faster. Hitting a wall or yourself ends it.
// Hold button A to go twice as fast: food eaten that way is worth 2 points.
#include "os.h"

namespace {

constexpr const char *NS = "snake";
constexpr int CELL = 4;   // pixels per grid cell
constexpr int COLS = 31;  // 31 * 4 = 124 px, inside a 126 px wide frame
constexpr int ROWS = 13;  // 13 * 4 = 52 px, below the score bar
constexpr int X0 = 1, Y0 = 11;  // top-left pixel of cell (0, 0)
constexpr int MAX_LEN = COLS * ROWS;

// The body is a ring buffer: head at index `head`, tail `len - 1` behind it.
uint8_t bx[MAX_LEN], by[MAX_LEN];
int head, len;
int dx, dy;          // direction of the last step
int wantX, wantY;    // direction for the next step, from the stick
int foodX, foodY;
uint32_t lastStep;
int score, eaten, best;
bool boosting;  // button A held: double speed, double points
bool over, newBest;

bool onSnake(int x, int y) {
  for (int i = 0; i < len; i++) {
    int k = (head - i + MAX_LEN) % MAX_LEN;
    if (bx[k] == x && by[k] == y) return true;
  }
  return false;
}

void placeFood() {
  do {
    foodX = random(0, COLS);
    foodY = random(0, ROWS);
  } while (onSnake(foodX, foodY));
}

uint32_t stepMs() {
  uint32_t ms = max(70, 160 - eaten * 4);
  return boosting ? ms / 2 : ms;
}

void start() {
  len = 3;
  head = 2;
  for (int i = 0; i < len; i++) {
    bx[i] = COLS / 2 - 2 + i;
    by[i] = ROWS / 2;
  }
  dx = wantX = 1;
  dy = wantY = 0;
  placeFood();
  score = 0;
  eaten = 0;
  best = loadBest(NS);
  lastStep = millis();
  over = false;
  newBest = false;
  play(SND_START);
}

void endRound() {
  over = true;
  if (score > best) {
    best = score;
    newBest = true;
    saveBest(NS, best);
  }
  Serial.printf("snake over: score %d (best %d)\n", score, best);
  play(newBest ? SND_BEST : SND_OVER);
}

void step() {
  dx = wantX;
  dy = wantY;
  int nx = bx[head] + dx, ny = by[head] + dy;
  if (nx < 0 || nx >= COLS || ny < 0 || ny >= ROWS) {
    endRound();
    return;
  }
  bool eating = nx == foodX && ny == foodY;
  // The tail moves away this step unless we grow, so it is safe to enter.
  if (!eating) len--;
  if (onSnake(nx, ny)) {
    endRound();
    return;
  }
  head = (head + 1) % MAX_LEN;
  bx[head] = nx;
  by[head] = ny;
  len++;
  if (eating) {  // the tail stayed, so the snake is one longer
    eaten++;
    score += boosting ? 2 : 1;
    play(SND_COIN);
    if (len >= MAX_LEN) {
      endRound();  // the board is full: nothing left to eat
      return;
    }
    placeFood();
  }
}

void update(const Input &in) {
  if (over) {
    if (in.press) start();
    drawGameOver(score, best, newBest);
    return;
  }

  // Steer by the stronger axis once the stick is past halfway. Turning
  // straight back into your own neck is ignored.
  if (max(fabsf(in.x), fabsf(in.y)) > 0.5f) {
    int nx = 0, ny = 0;
    if (fabsf(in.x) > fabsf(in.y)) nx = in.x > 0 ? 1 : -1;
    else ny = in.y > 0 ? 1 : -1;
    if (nx != -dx || ny != -dy) {
      wantX = nx;
      wantY = ny;
    }
  }

  boosting = in.aHeld;
  uint32_t now = millis();
  if (now - lastStep >= stepMs()) {
    lastStep = now;
    step();
    if (over) return;
  }

  oled.clearBuffer();
  oled.setFont(u8g2_font_5x8_tf);
  oled.setCursor(0, 7);
  oled.printf("%d", score);
  oled.setCursor(W - 6 * 6, 7);
  oled.printf("hi %d", best);
  oled.drawFrame(0, Y0 - 1, COLS * CELL + 2, ROWS * CELL + 2);
  for (int i = 0; i < len; i++) {
    int k = (head - i + MAX_LEN) % MAX_LEN;
    oled.drawBox(X0 + bx[k] * CELL, Y0 + by[k] * CELL, CELL - 1, CELL - 1);
  }
  if ((frame / 3) % 4 != 0)  // blinking food
    oled.drawFrame(X0 + foodX * CELL, Y0 + foodY * CELL, CELL - 1, CELL - 1);
  oled.sendBuffer();
}

}  // namespace

const Game SNAKE = {"Snake", NS, start, update};
