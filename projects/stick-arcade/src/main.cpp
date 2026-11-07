// Stick Arcade: pick a game with the joystick, press the stick to play.
// Hold the stick button down for a moment to come back to this menu.
#include "arcade.h"

const Game *const GAMES[] = {&DODGE, &SNAKE, &MAZE, &CRYPT, &BRICKS};
constexpr int NUM_GAMES = sizeof GAMES / sizeof GAMES[0];

const Game *current = nullptr;  // nullptr while the menu is showing
int choice = 0;
int bests[NUM_GAMES];

void openMenu() {
  current = nullptr;
  for (int i = 0; i < NUM_GAMES; i++) bests[i] = loadBest(GAMES[i]->prefsNs);
}

void updateMenu(const Input &in) {
  if (in.up || in.down) {
    choice = (choice + (in.down ? 1 : NUM_GAMES - 1)) % NUM_GAMES;
    play(SND_MOVE);
  }
  if (in.press) {
    current = GAMES[choice];
    Serial.printf("starting %s\n", current->name);
    current->start();
    return;
  }

  oled.clearBuffer();
  oled.setFont(u8g2_font_9x15B_tf);
  drawCentered(13, "STICK ARCADE");
  oled.setFont(u8g2_font_6x10_tf);
  // Show 4 games at a time; the list scrolls to keep the choice visible.
  constexpr int VISIBLE = 4;
  static int first = 0;
  if (choice < first) first = choice;
  if (choice >= first + VISIBLE) first = choice - VISIBLE + 1;
  for (int i = first; i < min(NUM_GAMES, first + VISIBLE); i++) {
    int y = 25 + (i - first) * 10;
    if (i == choice) oled.drawStr(8, y, ">");
    oled.drawStr(20, y, GAMES[i]->name);
    char hi[16];
    snprintf(hi, sizeof hi, "hi %d", bests[i]);
    oled.drawStr(W - 8 - oled.getStrWidth(hi), y, hi);
  }
  if (first > 0) oled.drawTriangle(W - 3, 16, W - 5, 18, W - 1, 18);  // more above
  if (first + VISIBLE < NUM_GAMES) oled.drawTriangle(W - 3, 57, W - 5, 55, W - 1, 55);  // more below
  oled.setFont(u8g2_font_5x8_tf);
  drawCentered(63, "up/down, press to play");
  oled.sendBuffer();
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\nstick-arcade starting");
  arcadeBegin();
  openMenu();
  play(SND_BOOT);
}

void loop() {
  updateSound();
  static uint32_t lastFrame = 0;
  uint32_t now = millis();
  if (now - lastFrame < FRAME_MS) return;
  lastFrame = now;
  frame++;

  Input in = readInput();
  if (current && in.hold) {
    Serial.println("back to menu");
    openMenu();
    return;
  }
  if (current) current->update(in);
  else updateMenu(in);
}
