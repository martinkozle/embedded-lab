// Wear counter: how many times each piece of clothing was worn since its
// last wash. Point the stick at an item; tap A to count a wear.
// Hold A to mark it washed:
//   0.3-1 s    the middle fills; let go to cancel
//   1-1.7 s    the middle is full; let go to wash this item
//   1.7-3.2 s  the ring spins up; let go to cancel
//   3.2 s on   the ring is ready to blow; let go to wash the whole group
// Press the stick for the next group.
//
// Washing is celebrated. One item: its circle bursts into soap bubbles.
// Everything: the ring spins up like a washing machine and gets sucked
// into the middle; letting go once it's ready blows it up (flash, shockwave, sparks, a cloud of bubbles that float up and pop),
// with "CLEAN!" bouncing in and a fanfare. Then the ring spirals back out.
#include <Preferences.h>

#include "items.h"
#include "os.h"

namespace {

constexpr uint32_t TAP_MS = 300;    // shorter than this is a tap: +1
constexpr uint32_t ITEM_MS = 1000;  // held this long: let go to wash the item
constexpr uint32_t SPIN_MS = 1700;  // the spin-up starts; letting go cancels
constexpr uint32_t ALL_MS = 3200;   // held this long: let go to wash the group

const Note SND_PLUS[] = {{1319, 40}, {1760, 70}, {0, 0}};
const Note SND_STAGE[] = {{1568, 25}, {0, 0}};
const Note SND_WASH[] = {{1568, 60}, {1175, 60}, {784, 120}, {0, 0}};
const Note SND_CANCEL[] = {{392, 50}, {294, 80}, {0, 0}};
const Note SND_GROUP[] = {{880, 30}, {1175, 50}, {0, 0}};
// A rumbling boom (quick random-ish low notes), then a fanfare.
const Note SND_BOOM[] = {{196, 25}, {131, 25}, {220, 25}, {98, 25}, {165, 25},
                         {87, 25}, {147, 25}, {73, 40}, {0, 120},
                         {523, 70}, {659, 70}, {784, 70}, {1047, 70}, {1319, 70},
                         {1568, 70}, {0, 60}, {1568, 80}, {2093, 350}, {0, 0}};
Note chargeNote[] = {{400, 60}, {0, 0}};  // the spin-up whine, retuned per frame
Note popNote[] = {{2000, 6}, {0, 0}};     // a bubble popping

uint8_t counts[NUM_GROUPS][RADIAL_MAX];
int group = 0;
Radial ring;
uint32_t plusAt = 0;      // when the last +1 happened, for the "+1" flash
bool aWas = false;        // A was down last frame
bool aBlocked = false;    // A was already down when the app opened
uint32_t aSince = 0;
int stage = 0;            // while A is held: 0 cancel, 1 wash item, 2 spinning
                          // up (cancel), 3 wash group
int nextGroup = -1;       // the group to show once this ring has spiralled in

// --- Celebration ------------------------------------------------------------------
// Bubbles are hollow circles with a little drag that drift upward and pop at
// a random moment; sparks are short streaks that fly out and fade.
struct Bubble { float x, y, vx, vy; uint16_t popAt; uint8_t r; bool alive; };
struct Spark { float x, y, vx, vy; };
constexpr int MAX_BUBBLES = 40, MAX_SPARKS = 24;
Bubble bubbles[MAX_BUBBLES];
Spark sparks[MAX_SPARKS];
uint32_t boomAt = 0;      // when the group wash blew up, 0 if not running
uint32_t burstAt = 0;     // when the last bubble burst started
constexpr uint32_t BOOM_MS = 2600;   // how long the whole show lasts
constexpr uint32_t RESPAWN_MS = 1300;  // when the ring comes back
float spinVel = 0;        // the ring's spin-up, in turns per frame
bool respawned = true;

void addBubbles(int x, int y, int count, float speed, uint16_t lifeMin, uint16_t lifeMax) {
  for (int i = 0, made = 0; i < MAX_BUBBLES && made < count; i++) {
    if (bubbles[i].alive) continue;
    float a = random(0, 6283) / 1000.0f, v = speed * random(30, 100) / 100.0f;
    bubbles[i] = {float(x), float(y), v * cosf(a), v * sinf(a),
                  (uint16_t)random(lifeMin, lifeMax), (uint8_t)random(1, 4), true};
    made++;
  }
}

void updateBubbles(uint32_t sinceBurst) {
  static uint32_t lastPop = 0;
  for (auto &b : bubbles) {
    if (!b.alive) continue;
    b.vx *= 0.9f;
    b.vy = b.vy * 0.9f - 0.12f;  // soap floats up
    b.x += b.vx + sinf((millis() + b.popAt) / 120.0f) * 0.3f;  // and wobbles
    b.y += b.vy;
    if (sinceBurst >= b.popAt || b.y < -4) {
      b.alive = false;
      if (b.y > 0) {  // a tiny 4-point star where it popped
        oled.drawHLine(b.x - 2, b.y, 5);
        oled.drawVLine(b.x, b.y - 2, 5);
      }
      if (millis() - lastPop > 70 && sinceBurst > 1100) {  // after the fanfare
        lastPop = millis();
        popNote[0].hz = random(1800, 3200);
        play(popNote);
      }
      continue;
    }
    oled.drawCircle(lroundf(b.x), lroundf(b.y), b.r);
  }
}

void drawBoom(uint32_t t) {
  if (t < 90) {  // white flash
    oled.drawBox(0, 0, W, H);
    return;
  }
  if (t < 700) {  // shockwave: two rings racing out
    int r = (t - 90) / 6;
    oled.drawCircle(RING_CX, RING_CY, r);
    if (r > 6) oled.drawCircle(RING_CX, RING_CY, r - 6);
  }
  if (t < 600) {  // sparks: streaks along their direction
    for (auto &s : sparks) {
      s.x += s.vx;
      s.y += s.vy;
      s.vx *= 0.88f;
      s.vy *= 0.88f;
      oled.drawLine(lroundf(s.x), lroundf(s.y), lroundf(s.x - s.vx * 2), lroundf(s.y - s.vy * 2));
    }
  }
  // CLEAN! drops in from above and bounces.
  float drop = t / 1000.0f;
  int y = 30 - lroundf(30 * expf(-5 * drop) * fabsf(cosf(drop * 11)));
  oled.setFont(u8g2_font_9x15B_tf);
  oled.setDrawColor(0);
  oled.drawBox(66, y - 13, W - 66, 16);
  oled.setDrawColor(1);
  oled.drawStr(70, y, "CLEAN!");
  if (t > 600) {
    oled.setFont(u8g2_font_5x8_tf);
    char s[24];
    snprintf(s, sizeof s, "%s: all 0", GROUPS[group].name);
    oled.drawStr(68, 46, s);
  }
}

int itemCount(int g) {
  int n = 0;
  while (n < RADIAL_MAX && GROUPS[g].items[n]) n++;
  return n;
}

// Each count is saved under a key made from its group and item name (NVS
// keys max out at 15 characters, so it's a hash). Reordering or adding items
// keeps every count with its item; renaming an item starts it from 0.
void countKey(int g, int i, char key[12]) {
  uint32_t h = 2166136261u;  // FNV-1a over "group/item"
  for (const char *c = GROUPS[g].name; *c; c++) h = (h ^ (uint8_t)*c) * 16777619u;
  h = (h ^ '/') * 16777619u;
  for (const char *c = GROUPS[g].items[i]; *c; c++) h = (h ^ (uint8_t)*c) * 16777619u;
  snprintf(key, 12, "c%08lx", (unsigned long)h);
}

void loadCounts() {
  Preferences p;
  p.begin("wear", true);
  char key[12];
  for (int g = 0; g < NUM_GROUPS; g++)
    for (int i = 0; i < itemCount(g); i++) {
      countKey(g, i, key);
      counts[g][i] = p.getUChar(key, 0);
    }
  p.end();
}

void saveCount(int g, int i) {
  Preferences p;
  p.begin("wear", false);
  char key[12];
  countKey(g, i, key);
  p.putUChar(key, counts[g][i]);
  p.end();
}

void start() {
  loadCounts();
  boomAt = 0;
  respawned = true;
  spinVel = 0;
  ring.spin = 0;
  ring.squeeze = 1;
  for (auto &b : bubbles) b.alive = false;
  ring.reset(itemCount(group), ring.selected);
  nextGroup = -1;
  aBlocked = true;  // the A press that opened the app isn't a wear
  aWas = false;
  stage = 0;
  for (int g = 0; g < NUM_GROUPS; g++)
    for (int i = 0; i < itemCount(g); i++)
      Serial.printf("  %s / %s: %d\n", GROUPS[g].name, GROUPS[g].items[i], counts[g][i]);
}

void drawCount(int i, int x, int y) { drawNumberAt(x, y, counts[group][i]); }

void drawMiddle(uint32_t held) {
  constexpr int R = RING_FREE_R;
  if (held >= TAP_MS) {
    float p1 = min(1.0f, (held - TAP_MS) / float(ITEM_MS - TAP_MS));
    oled.drawDisc(RING_CX, RING_CY, lroundf(R * p1));
    if (stage == 3) {
      oled.setFont(u8g2_font_4x6_tf);
      oled.setFontMode(1);
      oled.setDrawColor(2);
      oled.drawStr(RING_CX - 5, RING_CY + 3, "ALL");
      oled.setDrawColor(1);
    }
  } else if (millis() - plusAt < 500) {
    oled.setFont(u8g2_font_4x6_tf);
    oled.drawStr(RING_CX - 3, RING_CY + 3, "+1");
  }
}

void drawPanel() {
  const Group &g = GROUPS[group];
  constexpr int X = 67;
  char s[24];
  oled.setFont(u8g2_font_5x8_tf);
  oled.drawStr(X, 7, g.name);
  snprintf(s, sizeof s, "%d/%d", group + 1, NUM_GROUPS);
  oled.drawStr(W - oled.getStrWidth(s), 7, s);
  oled.drawHLine(X, 10, W - X);

  oled.setFont(u8g2_font_6x10_tf);
  oled.drawStr(X, 23, g.items[ring.selected]);

  int c = counts[group][ring.selected];
  oled.setFont(u8g2_font_9x15B_tf);
  snprintf(s, sizeof s, "%d", c);
  oled.drawStr(X, 40, s);
  int numW = oled.getStrWidth(s);
  oled.setFont(u8g2_font_5x8_tf);
  oled.drawStr(X + numW + 3, 40, c == 1 ? "wear" : "wears");

  // What the buttons do right now.
  const char *l1 = "tap A: wore it", *l2 = "hold A: washed", *l3 = "press: group";
  if (stage == 0 && aWas && !aBlocked && millis() - aSince >= TAP_MS) {
    l1 = "let go: cancel"; l2 = ""; l3 = "";
  } else if (stage == 1) {
    l1 = "let go: washed"; l2 = "keep holding:"; l3 = "wash all";
  } else if (stage == 2) {
    l1 = "let go: cancel"; l2 = "keep holding:"; l3 = "wash all";
  } else if (stage == 3) {
    l1 = "let go: wash"; l2 = "ALL in group"; l3 = "";
  }
  oled.setFont(u8g2_font_4x6_tf);
  oled.drawStr(X, 51, l1);
  oled.drawStr(X, 57, l2);
  oled.drawStr(X, 63, l3);
}

void update(const Input &in) {
  uint32_t now = millis();
  int n = itemCount(group);

  // The group wash show: nothing else happens until it's over.
  if (boomAt) {
    uint32_t t = now - boomAt;
    if (!respawned && t >= RESPAWN_MS) {
      respawned = true;
      ring.spin = 0;
      ring.squeeze = 1;
      ring.reset(n, ring.selected);
    }
    if (respawned) ring.update(Input{});
    oled.clearBuffer();
    if (respawned) ring.draw(drawCount);
    updateBubbles(t);
    drawBoom(t);
    oled.sendBuffer();
    if (t >= BOOM_MS) {
      boomAt = 0;
      aBlocked = true;  // ignore A until it's let go
    }
    return;
  }

  if (ring.update(in)) radialTick();

  if (aBlocked && !in.aHeld) aBlocked = false;
  uint32_t held = 0;
  if (!aBlocked) {
    if (in.aHeld && !aWas) { aSince = now; stage = 0; }
    if (in.aHeld) {
      held = now - aSince;
      int s = held >= ALL_MS ? 3 : held >= SPIN_MS ? 2 : held >= ITEM_MS ? 1 : 0;
      if (s > stage) {
        stage = s;
        if (s != 2) play(SND_STAGE);  // stage 2 has the spin-up whine instead
      }
    } else if (aWas) {  // just let go
      const char *item = GROUPS[group].items[ring.selected];
      if (now - aSince < TAP_MS) {
        uint8_t &c = counts[group][ring.selected];
        if (c < 255) c++;
        saveCount(group, ring.selected);
        plusAt = now;
        ring.size[ring.selected] = 2.2f;  // the circle pops, then settles
        play(SND_PLUS);
        Serial.printf("+1 %s / %s -> %d\n", GROUPS[group].name, item, c);
      } else if (stage == 1) {
        counts[group][ring.selected] = 0;
        saveCount(group, ring.selected);
        int x, y;
        ring.itemPos(ring.selected, x, y);
        addBubbles(x, y, 10, 2.5f, 300, 900);
        burstAt = now;
        ring.size[ring.selected] = 2.4f;
        play(SND_WASH);
        Serial.printf("washed %s / %s\n", GROUPS[group].name, item);
      } else if (stage == 3) {
        for (int i = 0; i < n; i++) {
          counts[group][i] = 0;
          saveCount(group, i);
        }
        // Kaboom.
        boomAt = burstAt = now;
        respawned = false;
        ring.scale = 0;
        spinVel = 0;
        for (auto &b : bubbles) b.alive = false;
        addBubbles(RING_CX, RING_CY, MAX_BUBBLES, 6, 700, 2500);
        for (auto &sp : sparks) {
          float a = random(0, 6283) / 1000.0f, v = random(30, 70) / 10.0f;
          sp = {float(RING_CX), float(RING_CY), v * cosf(a), v * sinf(a)};
        }
        play(SND_BOOM);
        Serial.printf("washed all %s\n", GROUPS[group].name);
      } else {
        play(SND_CANCEL);
      }
      stage = 0;
    }
    aWas = in.aHeld;
  }

  // Next group: this ring spirals into the middle, the next one out of it.
  if (in.stickPress && nextGroup < 0) {
    nextGroup = (group + 1) % NUM_GROUPS;
    ring.collapse();
    play(SND_GROUP);
  }
  if (nextGroup >= 0 && ring.collapsed()) {
    group = nextGroup;
    nextGroup = -1;
    ring.reset(itemCount(group), 0);
  }

  // Spin-up: the ring spins faster and faster and is sucked toward the
  // middle, whining higher; once it's ready it shakes.
  ring.shakeX = ring.shakeY = 0;
  if (held >= SPIN_MS) {
    float p = min(1.0f, (held - SPIN_MS) / float(ALL_MS - SPIN_MS));
    spinVel = 0.005f + 0.11f * p * p;
    ring.squeeze += ((1 - 0.5f * p) - ring.squeeze) * 0.3f;
    if (p >= 1) {
      ring.shakeX = random(-1, 2);
      ring.shakeY = random(-1, 2);
    }
    static uint32_t lastWhine = 0;
    if (now - lastWhine > 60 && held > SPIN_MS) {
      lastWhine = now;
      chargeNote[0].hz = 300 + 1700 * p + (p >= 1 ? random(0, 200) : 0);
      play(chargeNote);
    }
  } else {  // let go early: wind down
    spinVel *= 0.88f;
    ring.squeeze += (1 - ring.squeeze) * 0.25f;
  }
  ring.spin += spinVel;
  if (spinVel < 0.0005f) ring.spin += (lroundf(ring.spin) - ring.spin) * 0.2f;  // settle upright

  oled.clearBuffer();
  ring.draw(drawCount);
  drawMiddle(held);
  drawPanel();
  if (now - burstAt < 1500) updateBubbles(now - burstAt);
  oled.sendBuffer();
}

}  // namespace

const Game WEAR = {"Wear", nullptr, start, update};
