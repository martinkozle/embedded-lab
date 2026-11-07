// Stick Dodge
//
// Steer the ship with the joystick. Rocks fall from the top and get faster
// over time; touching one ends the game. Stars are worth 10 points each, and
// you also score 1 point per second survived. Press the stick to start.
// The best score survives power-off (saved in flash with Preferences).
//
// Wiring (see hardware.md):
//   OLED  GND->GND  VCC->3V3  SCL->GPIO22  SDA->GPIO21
//   Stick GND->GND  +5V->3V3  VRX->GPIO32  VRY->GPIO33  SW->GPIO25
#include <Arduino.h>
#include <Preferences.h>
#include <U8g2lib.h>
#include <Wire.h>

constexpr int PIN_SDA = 21;
constexpr int PIN_SCL = 22;
constexpr int PIN_VRX = 32;
constexpr int PIN_VRY = 33;
constexpr int PIN_SW = 25;

constexpr int W = 128;
constexpr int H = 64;
constexpr uint32_t FRAME_MS = 33;  // ~30 fps
constexpr int DEADZONE = 250;      // ADC counts around the resting centre
constexpr float SHIP_SPEED = 3.0f; // pixels per frame at full tilt
constexpr float SHIP_R = 2.5f;     // collision radius
constexpr int MAX_ROCKS = 10;

U8G2_SSD1306_128X64_NONAME_F_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE);
Preferences prefs;

enum class State { Title, Play, Over };
State state = State::Title;

struct Rock {
  bool alive;
  float x, y, vy, r;
};
Rock rocks[MAX_ROCKS];

struct Star {
  bool alive;
  float x, y;
};
Star star;

float shipX, shipY;
int centerX, centerY;  // resting stick readings, measured at boot
uint32_t playStart, frame, lastSpawn;
int bonus, score, best;
bool newBest;

// The joystick is mounted so both raw axes run opposite to the screen.
int readX() { return 4095 - analogRead(PIN_VRX); }
int readY() { return 4095 - analogRead(PIN_VRY); }

// Turns a stick reading into -1..1, with a dead zone around the centre.
float axis(int raw, int center) {
  int d = raw - center;
  if (abs(d) < DEADZONE) return 0;
  float span = d > 0 ? 4095 - center - DEADZONE : center - DEADZONE;
  float v = (d > 0 ? d - DEADZONE : d + DEADZONE) / span;
  return constrain(v, -1.0f, 1.0f);
}

// True once per press (sampled every frame, which also rides out bounce).
bool buttonPressed() {
  static bool wasDown = false;
  bool down = digitalRead(PIN_SW) == LOW;
  bool pressed = down && !wasDown;
  wasDown = down;
  return pressed;
}

void placeStar() {
  star.alive = true;
  star.x = random(6, W - 6);
  star.y = random(14, H - 6);
}

void startGame() {
  shipX = W / 2;
  shipY = H - 8;
  for (auto &r : rocks) r.alive = false;
  placeStar();
  bonus = 0;
  score = 0;
  frame = 0;
  playStart = millis();
  lastSpawn = playStart;
  newBest = false;
  state = State::Play;
}

void drawShip(int x, int y) {
  oled.drawTriangle(x, y - 3, x - 3, y + 3, x + 3, y + 3);
}

void drawStar(int x, int y) {
  // a small blinking plus sign
  if ((frame / 4) % 4 == 0) return;
  oled.drawHLine(x - 2, y, 5);
  oled.drawVLine(x, y - 2, 5);
}

void updatePlay() {
  uint32_t now = millis();
  float seconds = (now - playStart) / 1000.0f;

  // Move the ship.
  shipX += axis(readX(), centerX) * SHIP_SPEED;
  shipY += axis(readY(), centerY) * SHIP_SPEED;
  shipX = constrain(shipX, 3.0f, W - 4.0f);
  shipY = constrain(shipY, 12.0f, H - 4.0f);

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

  // Move rocks and check for hits.
  for (auto &r : rocks) {
    if (!r.alive) continue;
    r.y += r.vy;
    if (r.y - r.r > H) r.alive = false;
    float dx = r.x - shipX, dy = r.y - shipY;
    float reach = r.r + SHIP_R;
    if (dx * dx + dy * dy < reach * reach) {
      score = (int)seconds + bonus;
      if (score > best) {
        best = score;
        newBest = true;
        prefs.putInt("best", best);
      }
      Serial.printf("game over: score %d (best %d)\n", score, best);
      state = State::Over;
      return;
    }
  }

  // Grab the star.
  float sx = star.x - shipX, sy = star.y - shipY;
  if (sx * sx + sy * sy < 5 * 5) {
    bonus += 10;
    placeStar();
  }

  score = (int)seconds + bonus;

  oled.clearBuffer();
  oled.setCursor(0, 7);
  oled.printf("%d", score);
  oled.setCursor(W - 6 * 6, 7);
  oled.printf("hi %d", best);
  oled.drawHLine(0, 9, W);
  for (auto &r : rocks)
    if (r.alive) oled.drawDisc(r.x, r.y, r.r);
  drawStar(star.x, star.y);
  drawShip(shipX, shipY);
  oled.sendBuffer();
}

void drawCentered(int y, const char *text) {
  oled.setCursor((W - oled.getStrWidth(text)) / 2, y);
  oled.print(text);
}

void drawTitle() {
  oled.clearBuffer();
  oled.setFont(u8g2_font_9x15B_tf);
  drawCentered(20, "STICK DODGE");
  oled.setFont(u8g2_font_5x8_tf);
  drawCentered(34, "dodge rocks, grab stars");
  char line[24];
  snprintf(line, sizeof line, "best %d", best);
  drawCentered(46, line);
  if ((frame / 15) % 2 == 0) drawCentered(60, "press the stick");
  oled.sendBuffer();
}

void drawOver() {
  oled.clearBuffer();
  oled.setFont(u8g2_font_9x15B_tf);
  drawCentered(18, "GAME OVER");
  oled.setFont(u8g2_font_5x8_tf);
  char line[24];
  snprintf(line, sizeof line, "score %d", score);
  drawCentered(32, line);
  snprintf(line, sizeof line, newBest ? "NEW BEST!" : "best %d", best);
  drawCentered(44, line);
  if ((frame / 15) % 2 == 0) drawCentered(60, "press to play again");
  oled.sendBuffer();
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\nstick-dodge starting");

  pinMode(PIN_SW, INPUT_PULLUP);
  analogReadResolution(12);

  // Measure where the stick rests. Don't touch it during the first moment.
  long sx = 0, sy = 0;
  for (int i = 0; i < 32; i++) {
    sx += readX();
    sy += readY();
    delay(2);
  }
  centerX = sx / 32;
  centerY = sy / 32;
  Serial.printf("stick centre x=%d y=%d\n", centerX, centerY);

  prefs.begin("stick-dodge", false);
  best = prefs.getInt("best", 0);

  Wire.begin(PIN_SDA, PIN_SCL);
  oled.begin();
  randomSeed(esp_random());
}

void loop() {
  static uint32_t lastFrame = 0;
  uint32_t now = millis();
  if (now - lastFrame < FRAME_MS) return;
  lastFrame = now;
  frame++;

  bool pressed = buttonPressed();
  switch (state) {
    case State::Title:
      drawTitle();
      if (pressed) startGame();
      break;
    case State::Play:
      oled.setFont(u8g2_font_5x8_tf);
      updatePlay();
      break;
    case State::Over:
      drawOver();
      if (pressed) startGame();
      break;
  }
}
