// Shared hardware and helpers for the Stick Arcade games.
//
// Wiring (see hardware.md):
//   OLED   GND->GND  VCC->3V3  SCL->GPIO22  SDA->GPIO21
//   Stick  GND->GND  +5V->3V3  VRX->GPIO32  VRY->GPIO33  SW->GPIO25
//   Buzzer (HW-508 passive)  S->GPIO27  -->GND  middle pin unconnected
//   Button (KY-004 module)   S->GPIO26  -->GND  middle->3V3, pressed = LOW
#pragma once
#include <Arduino.h>
#include <U8g2lib.h>

constexpr int W = 128;
constexpr int H = 64;

// The I2C OLED at its default 400 kHz bus takes ~41 ms to receive a frame,
// so 24 fps is the most it can show. We aim for exactly that.
constexpr uint32_t FRAME_MS = 42;

extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C oled;
extern uint32_t frame;  // counts up once per frame, handy for blinking

// What the player is doing this frame.
struct Input {
  float x, y;                     // stick, -1..1 each, 0 inside the dead zone
  bool up, down, left, right;     // true on the frame the stick is flicked
  bool press;                     // "select": a short stick press, or button A
  bool a;                         // button A pressed this frame
  bool aHeld;                     // button A is down
  bool hold;                      // stick button held down for 0.7 s (once)
};

void arcadeBegin();     // call once from setup()
Input readInput();      // call once per frame
void updateSound();     // call every loop()

// For steering: squares the stick value (keeping its sign), so small tilts
// and leftover drift move very little and full tilt still moves fully.
inline float curve(float v) { return v * fabsf(v); }

// Sounds: a list of {Hz, ms} notes ending with {0, 0}. Hz 0 is a rest.
struct Note {
  uint16_t hz, ms;
};
void play(const Note *tune);
extern const Note SND_START[], SND_COIN[], SND_OVER[], SND_BEST[],
    SND_MOVE[], SND_BOOT[], SND_CLEAR[], SND_DASH[];

// Best scores live in flash (NVS), one namespace per game.
int loadBest(const char *ns);
void saveBest(const char *ns, int best);

void drawCentered(int y, const char *text);

// Shared game-over screen: score, best, and how to go on.
void drawGameOver(int score, int best, bool newBest);

// Every game provides these.
struct Game {
  const char *name;
  const char *prefsNs;          // where its best score is saved
  void (*start)();              // reset and begin a new round
  void (*update)(const Input &);  // one frame: move, then draw
};
extern const Game DODGE;
extern const Game SNAKE;
extern const Game MAZE;
extern const Game CRYPT;
extern const Game BRICKS;
extern const Game STACKER;
