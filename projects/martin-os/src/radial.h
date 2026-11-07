// The ring picker, "compass" style: up to 8 circles around a centre, and a
// hand in the middle that swings after the stick like a compass needle. The
// picked circle is filled; the white fill glides to a new pick.
//
// Rings also spiral: out of the centre when they appear (expand), back into
// it when they go away (collapse), for transitions between screens.
#pragma once
#include "os.h"

constexpr int RADIAL_MAX = 8;
constexpr int RING_CX = 32, RING_CY = 31;  // centre of the ring on screen
constexpr int RING_FREE_R = 6;  // the middle up to this radius is free for apps

// A soft click when the pick changes, at most one per 150 ms, so spinning
// the stick round ticks gently instead of buzzing.
void radialTick();

struct Radial {
  int n = 1;         // how many items
  int selected = 0;

  // New contents: the ring starts collapsed and spirals out.
  void reset(int count, int sel);
  void collapse() { scaleTarget = 0; }
  bool collapsed() const { return scaleTarget == 0 && scale < 0.06f; }
  bool update(const Input &in);  // true when the selection changed
  // `inside` draws item i's icon or number, centred on (x, y). Draw in XOR
  // (setDrawColor(2)) so it shows on white as well as on black.
  void draw(void (*inside)(int i, int x, int y)) const;
  void itemPos(int i, int &x, int &y) const;  // where circle i is drawn now

  // Animation state; positions are in item steps (0 = top, clockwise).
  float size[RADIAL_MAX] = {};  // 0..1, how much each circle is magnified
  float hPos = 0, hVel = 0;     // the white fill
  float pPos = 0, pVel = 0;     // the hand
  float scale = 0, scaleVel = 0, scaleTarget = 1;  // 0 = in the centre

  // Set by apps for effects: extra turns of spin, a squeeze toward the
  // middle (1 = normal), and a shake offset in pixels.
  float spin = 0, squeeze = 1;
  int shakeX = 0, shakeY = 0;
};

// Exact centring helpers for the items.
void drawNumberAt(int x, int y, int value);                // 6x10 digits, XOR
void drawIconAt(int x, int y, const char *const icon[9]);  // 9x9, XOR
