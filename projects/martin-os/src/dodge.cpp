// Dodge: steer the ship anywhere on the screen. Rocks fall from the top and
// get faster over time; touching one ends the round. Stars are worth 10
// points each, and you also score 1 point per second survived.
// Button A dashes: a quick burst of speed, ready again after 2 seconds.
#include "os.h"

namespace {

// The namespace name predates the arcade menu; kept so old best scores stay.
constexpr const char *NS = "stick-dodge";
constexpr float SHIP_SPEED = 3.0f;  // pixels per frame at full tilt
constexpr float SHIP_R = 2.5f;      // collision radius
constexpr int MAX_ROCKS = 10;
constexpr uint32_t DASH_MS = 250;       // how long a dash lasts
constexpr uint32_t DASH_COOLDOWN = 2000;  // from one dash to the next
constexpr float DASH_BOOST = 3.0f;      // speed multiplier while dashing

struct Rock {
  bool alive;
  float x, y, vy, r;
};
Rock rocks[MAX_ROCKS];
float starX, starY;
float shipX, shipY;
uint32_t playStart, lastSpawn, dashAt;
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
  dashAt = playStart - DASH_COOLDOWN;  // ready right away
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

  // Button A: a short burst of speed in the direction you're steering.
  if (in.a && now - dashAt >= DASH_COOLDOWN && (in.x != 0 || in.y != 0)) {
    dashAt = now;
    play(SND_DASH);
  }
  float speed = SHIP_SPEED * (now - dashAt < DASH_MS ? DASH_BOOST : 1.0f);
  shipX = constrain(shipX + curve(in.x) * speed, 3.0f, W - 4.0f);
  shipY = constrain(shipY + curve(in.y) * speed, 12.0f, H - 4.0f);

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
  // Dash meter in the middle of the top bar: fills up, full = ready.
  uint32_t charged = min<uint32_t>(now - dashAt, DASH_COOLDOWN);
  oled.drawFrame(W / 2 - 12, 2, 24, 5);
  oled.drawBox(W / 2 - 12, 2, 24 * charged / DASH_COOLDOWN, 5);
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
