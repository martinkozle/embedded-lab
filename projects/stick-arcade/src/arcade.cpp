#include "arcade.h"

#include <Preferences.h>
#include <Wire.h>

constexpr int PIN_SDA = 21;
constexpr int PIN_SCL = 22;
constexpr int PIN_VRX = 32;  // ADC1
constexpr int PIN_VRY = 33;  // ADC1
constexpr int PIN_SW = 25;   // pressed = LOW, needs the internal pull-up
constexpr int PIN_BUZZER = 27;
constexpr int PIN_BTN = 26;  // button module, pressed = LOW

// ADC counts around the resting centre that count as "not touched". This
// stick doesn't spring back to exactly the same spot, so it's generous.
constexpr int DEADZONE = 450;
constexpr uint32_t HOLD_MS = 700;     // how long a press counts as "hold"

// Volume: the share of each wave the pin is HIGH, out of 255. 128 (half) is
// the loudest a piezo gets; smaller is quieter.
constexpr uint32_t VOLUME = 8;

U8G2_SSD1306_128X64_NONAME_F_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE);
uint32_t frame = 0;

static int centerX, centerY;  // resting stick readings, measured at boot

// --- Sound -----------------------------------------------------------------
const Note SND_START[] = {{523, 70}, {659, 70}, {784, 110}, {0, 0}};
const Note SND_COIN[] = {{1319, 40}, {1760, 70}, {0, 0}};
const Note SND_OVER[] = {{392, 120}, {330, 120}, {262, 120}, {196, 300}, {0, 0}};
const Note SND_BEST[] = {{392, 120}, {330, 120}, {262, 160}, {0, 120},
                         {523, 90}, {659, 90}, {784, 90}, {1047, 250}, {0, 0}};
const Note SND_MOVE[] = {{1047, 15}, {0, 0}};
const Note SND_CLEAR[] = {{784, 80}, {988, 80}, {1175, 80}, {1568, 220}, {0, 0}};
const Note SND_DASH[] = {{600, 25}, {900, 25}, {1300, 40}, {0, 0}};
const Note SND_BOOT[] = {{880, 60}, {0, 40}, {1175, 90}, {0, 0}};

static const Note *tune = nullptr;  // the tune playing now, nullptr when silent
static uint32_t noteStart;

static void playNote(const Note &n) {
  if (n.hz == 0) {
    ledcWrite(PIN_BUZZER, 0);
  } else {
    ledcChangeFrequency(PIN_BUZZER, n.hz, 8);
    ledcWrite(PIN_BUZZER, VOLUME);
  }
  noteStart = millis();
}

void play(const Note *t) {
  tune = t;
  playNote(*tune);
}

void updateSound() {
  if (!tune || millis() - noteStart < tune->ms) return;
  tune++;
  if (tune->ms == 0) {  // {0, 0} marks the end
    tune = nullptr;
    ledcWrite(PIN_BUZZER, 0);
    return;
  }
  playNote(*tune);
}

// --- Input -----------------------------------------------------------------
// The joystick is mounted so both raw axes run opposite to the screen.
static int readX() { return 4095 - analogRead(PIN_VRX); }
static int readY() { return 4095 - analogRead(PIN_VRY); }

// Turns a stick reading into -1..1, with a dead zone around the centre.
static float axis(int raw, int center) {
  int d = raw - center;
  if (abs(d) < DEADZONE) return 0;
  float span = d > 0 ? 4095 - center - DEADZONE : center - DEADZONE;
  float v = (d > 0 ? d - DEADZONE : d + DEADZONE) / span;
  return constrain(v, -1.0f, 1.0f);
}

Input readInput() {
  Input in{};
  int rawX = readX(), rawY = readY();
  in.x = axis(rawX, centerX);
  in.y = axis(rawY, centerY);
  static uint32_t lastLog = 0;  // shows drift on Serial, once a second
  if (millis() - lastLog >= 1000) {
    lastLog = millis();
    Serial.printf("stick raw x=%d y=%d (centre %d %d) -> %.2f %.2f\n", rawX, rawY,
                  centerX, centerY, in.x, in.y);
  }

  // A flick fires once when the stick goes past 60 %, and re-arms only after
  // it comes back under 30 %, so holding it doesn't repeat.
  static bool armed = true;
  float m = max(fabsf(in.x), fabsf(in.y));
  if (armed && m > 0.6f) {
    armed = false;
    if (fabsf(in.x) > fabsf(in.y)) (in.x > 0 ? in.right : in.left) = true;
    else (in.y > 0 ? in.down : in.up) = true;
  } else if (m < 0.3f) {
    armed = true;
  }

  // Short press fires on release; hold fires once while still held.
  // Sampling once per frame (42 ms) also rides out switch bounce.
  static bool wasDown = false, holdFired = false;
  static uint32_t downSince = 0;
  bool down = digitalRead(PIN_SW) == LOW;
  uint32_t now = millis();
  if (down && !wasDown) {
    downSince = now;
    holdFired = false;
  }
  if (down && !holdFired && now - downSince >= HOLD_MS) {
    in.hold = true;
    holdFired = true;
  }
  if (!down && wasDown && !holdFired) in.press = true;
  wasDown = down;

  static bool aWasDown = false;
  in.aHeld = digitalRead(PIN_BTN) == LOW;
  in.a = in.aHeld && !aWasDown;
  aWasDown = in.aHeld;
  if (in.a) in.press = true;
  return in;
}

// --- Best scores ------------------------------------------------------------
int loadBest(const char *ns) {
  Preferences p;
  p.begin(ns, true);
  int best = p.getInt("best", 0);
  p.end();
  return best;
}

void saveBest(const char *ns, int best) {
  Preferences p;
  p.begin(ns, false);
  p.putInt("best", best);
  p.end();
}

// --- Drawing -----------------------------------------------------------------
void drawCentered(int y, const char *text) {
  oled.setCursor((W - oled.getStrWidth(text)) / 2, y);
  oled.print(text);
}

void drawGameOver(int score, int best, bool newBest) {
  oled.clearBuffer();
  oled.setFont(u8g2_font_9x15B_tf);
  drawCentered(16, "GAME OVER");
  oled.setFont(u8g2_font_5x8_tf);
  char line[24];
  snprintf(line, sizeof line, "score %d", score);
  drawCentered(29, line);
  snprintf(line, sizeof line, newBest ? "NEW BEST!" : "best %d", best);
  drawCentered(40, line);
  if ((frame / 12) % 2 == 0) drawCentered(53, "press: play again");
  drawCentered(63, "hold: menu");
  oled.sendBuffer();
}

// --- Setup ---------------------------------------------------------------------
void arcadeBegin() {
  pinMode(PIN_SW, INPUT_PULLUP);
  pinMode(PIN_BTN, INPUT_PULLUP);
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

  Wire.begin(PIN_SDA, PIN_SCL);
  oled.begin();
  randomSeed(esp_random());

  ledcAttach(PIN_BUZZER, 2000, 8);
}
