// Dodge: steer the ship anywhere on the screen. Rocks fall from the top and
// get faster over time; touching one ends the round. Stars are worth 10
// points each, and you also score 1 point per second survived.
#include "arcade.h"

namespace {

// The namespace name predates the arcade menu; kept so old best scores stay.
constexpr const char *NS = "stick-dodge";
constexpr float SHIP_SPEED = 3.0f;  // pixels per frame at full tilt
constexpr float SHIP_R = 2.5f;      // collision radius
constexpr int MAX_ROCKS = 10;

struct Rock {
  bool alive;
  float x, y, vy, r;
};
Rock rocks[MAX_ROCKS];
float starX, starY;
float shipX, shipY;
uint32_t playStart, lastSpawn;
int bonus, score, best;
bool over, newBest;

void placeStar() {
  starX = random(6, W - 6);
  starY = random(14, H - 6);
}

void start() {
  shipX = W / 2;
  shipY = H - 8;
  for (auto &r : rocks) r.alive = false;
  placeStar();
  bonus = 0;
  score = 0;
  best = loadBest(NS);
  playStart = millis();
  lastSpawn = playStart;
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
  Serial.printf("dodge over: score %d (best %d)\n", score, best);
  play(newBest ? SND_BEST : SND_OVER);
}

void update(const Input &in) {
  if (over) {
    if (in.press) start();
    drawGameOver(score, best, newBest);
    return;
  }

  uint32_t now = millis();
  float seconds = (now - playStart) / 1000.0f;

  shipX = constrain(shipX + in.x * SHIP_SPEED, 3.0f, W - 4.0f);
  shipY = constrain(shipY + in.y * SHIP_SPEED, 12.0f, H - 4.0f);

  // Spawn rocks faster and faster: every 900 ms at the start, 250 ms at best.
  uint32_t spawnEvery = max(250.0f, 900.0f - seconds * 15.0f);
  if (now - lastSpawn >= spawnEvery) {
    lastSpawn = now;
    for (auto &r : rocks) {
      if (r.alive) continue;
      r.alive = true;
      r.r = random(2, 6);
      r.x = random(r.r, W - r.r);
      r.y = 9 - r.r;
      r.vy = 0.8f + seconds * 0.03f + random(0, 100) / 100.0f;
      break;
    }
  }

  score = (int)seconds + bonus;

  for (auto &r : rocks) {
    if (!r.alive) continue;
    r.y += r.vy;
    if (r.y - r.r > H) r.alive = false;
    float dx = r.x - shipX, dy = r.y - shipY;
    float reach = r.r + SHIP_R;
    if (dx * dx + dy * dy < reach * reach) {
      endRound();
      return;
    }
  }

  float sx = starX - shipX, sy = starY - shipY;
  if (sx * sx + sy * sy < 5 * 5) {
    bonus += 10;
    placeStar();
    play(SND_COIN);
  }

  oled.clearBuffer();
  oled.setFont(u8g2_font_5x8_tf);
  oled.setCursor(0, 7);
  oled.printf("%d", score);
  oled.setCursor(W - 6 * 6, 7);
  oled.printf("hi %d", best);
  oled.drawHLine(0, 9, W);
  for (auto &r : rocks)
    if (r.alive) oled.drawDisc(r.x, r.y, r.r);
  if ((frame / 3) % 4 != 0) {  // blinking plus sign
    oled.drawHLine(starX - 2, starY, 5);
    oled.drawVLine(starX, starY - 2, 5);
  }
  oled.drawTriangle(shipX, shipY - 3, shipX - 3, shipY + 3, shipX + 3, shipY + 3);
  oled.sendBuffer();
}

}  // namespace

const Game DODGE = {"Dodge", NS, start, update};
