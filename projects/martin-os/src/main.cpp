// Martin OS: a home screen of apps on a ring, with folders (Games, Demos).
// Point the stick at one, press A (or the stick) to open it. Hold the stick
// button to go back: from an app to its folder, from a folder to home.
// The screen turns off after a minute without input; any touch wakes it.
//
// Transitions: at boot a spirograph draws itself; opening an app spirals the
// ring into the middle; going home blooms a sunflower spiral (the golden
// angle), which then swirls into the middle and the home ring spirals out.
#include "os.h"
#include "radial.h"

// 9x9 icons, '#' is a lit pixel. Odd sizes centre exactly in the circles.
const char *const ICON_WEAR[9] = {
    ".##...##.", "#########", "#########", "#.#####.#", "..#####..",
    "..#####..", "..#####..", "..#####..", "..#####.."};
const char *const ICON_DODGE[9] = {
    "....#....", "....#....", "...###...", "...###...", "..#####..",
    ".#######.", "#########", "##.###.##", "#...#...#"};
const char *const ICON_SNAKE[9] = {
    "..#####..", ".##...##.", ".##......", "..##.....", "...###...",
    ".....##..", "......##.", ".##...##.", "..#####.."};
const char *const ICON_MAZE[9] = {
    "#########", "#.....#.#", "#.###.#.#", "#.#...#.#", "#.#.###.#",
    "#.#.....#", "#.#####.#", "#.......#", "####.####"};
const char *const ICON_CRYPT[9] = {
    "...###...", ".#######.", ".#######.", "##..#..##", "##..#..##",
    "#########", "#########", "#########", "#.##.##.#"};
const char *const ICON_BRICKS[9] = {
    "####.####", "####.####", ".........", "##.###.##", "##.###.##",
    ".........", "....#....", ".........", "..#####.."};
const char *const ICON_STACKER[9] = {
    "...###...", "...###...", "...###...", "#########", "#########",
    "#########", ".........", "###...###", "###...###"};

const char *const ICON_TANKS[9] = {
    "....#....", "....#....", "##..#..##", "#.#####.#", "###...###",
    "#.#...#.#", "###...###", "#.#####.#", "##.....##"};
const char *const ICON_GAMES[9] = {
    ".........", ".#######.", "##.###.##", "#...#.#.#", "##.###.##",
    "#########", "###...###", "##.....##", "........."};
const char *const ICON_DEMOS[9] = {
    "#########", "#.......#", "#.#####.#", "#.#...#.#", "#.#.#.#.#",
    "#.#.###.#", "#.#.....#", "#.#######", "#........"};
const char *const ICON_TUNNEL[9] = {
    "....#....", "...#.#...", "..#...#..", ".#######.", ".##...##.",
    "#.#...#.#", "#..#.#..#", "#...#...#", "#########"};
const char *const ICON_MANDEL[9] = {
    ".....#...", "....###..", "...#####.", ".#.#####.", "#########",
    ".#.#####.", "...#####.", "....###..", ".....#..."};
const char *const ICON_JULIA[9] = {
    "...###...", ".##...#..", "#...#..#.", "#..#.#.#.", "#.#...#.#",
    ".#.#.#..#", ".#..#...#", "..#...##.", "...###..."};
const char *const ICON_PLASMA[9] = {
    ".........", ".##...##.", "#..#.#..#", "....#....", ".........",
    ".##...##.", "#..#.#..#", "....#....", "........."};
const char *const ICON_SOLIDS[9] = {
    "...######", "..#....##", ".#....#.#", "######..#", "#....#..#",
    "#....#..#", "#....#.#.", "#....##..", "######..."};
const char *const ICON_CHAOS[9] = {
    "....#....", "..#.#.#..", "...###...", ".#..#..#.", "..#.#.#..",
    "...###...", ".#..#..#.", "..#.#.#..", "....#...."};
const char *const ICON_LIFE[9] = {
    "....##...", "....##...", ".........", ".......##", ".......##",
    ".........", ".##.##.##", ".##.##.##", "........."};

// A ring entry is an app, or a folder that opens another ring.
struct Entry {
  const char *name;
  const char *blurb;  // shown under the name; nullptr shows the best score
  const char *const *icon;
  const Game *game;   // nullptr for a folder
  int folder;         // the menu it opens, or -1
};
const Entry HOME_ENTRIES[] = {
    {"Wear", "clothes", ICON_WEAR, &WEAR, -1},
    {"Games", "7 games", ICON_GAMES, nullptr, 1},
    {"Demos", "math art", ICON_DEMOS, nullptr, 2},
};
const Entry GAME_ENTRIES[] = {
    {"Dodge", nullptr, ICON_DODGE, &DODGE, -1},     {"Snake", nullptr, ICON_SNAKE, &SNAKE, -1},
    {"Maze 3D", nullptr, ICON_MAZE, &MAZE, -1},     {"Crypt", nullptr, ICON_CRYPT, &CRYPT, -1},
    {"Bricks", nullptr, ICON_BRICKS, &BRICKS, -1},  {"Stacker", nullptr, ICON_STACKER, &STACKER, -1},
    {"Tanks", nullptr, ICON_TANKS, &TANKS, -1},
};
const Entry DEMO_ENTRIES[] = {
    {"Tunnel", "endless zoom", ICON_TUNNEL, &TUNNEL, -1},
    {"Mandelbrot", "fractal dive", ICON_MANDEL, &MANDEL, -1},
    {"Julia", "fractal morph", ICON_JULIA, &JULIA, -1},
    {"Plasma", "sine waves", ICON_PLASMA, &PLASMA, -1},
    {"Solids", "3D and 4D", ICON_SOLIDS, &SOLIDS, -1},
    {"Chaos", "chaos game", ICON_CHAOS, &CHAOS, -1},
    {"Life", "Conway", ICON_LIFE, &LIFE, -1},
};
struct Menu {
  const char *title;
  const Entry *entries;
  int n;
  int parent;  // -1 for home
};
#define MENU(title, list, parent) {title, list, sizeof list / sizeof list[0], parent}
const Menu MENUS[] = {
    MENU("MARTIN OS", HOME_ENTRIES, -1),
    MENU("GAMES", GAME_ENTRIES, 0),
    MENU("DEMOS", DEMO_ENTRIES, 0),
};
constexpr int NUM_MENUS = sizeof MENUS / sizeof MENUS[0];

enum Mode { BOOT, MENU_RING, LEAVING, APP, GOING_HOME };
Mode mode = BOOT;
uint32_t modeSince = 0;
int menu = 0;                 // the ring on screen, or the one the app came from
int menuSel[NUM_MENUS] = {};  // last pick in every menu
const Game *current = nullptr;
const Game *pendingGame = nullptr;  // where LEAVING goes: an app ...
int pendingMenu = -1;               // ... or a menu
Radial ring;
int bests[RADIAL_MAX];

const Note SND_OPEN[] = {{659, 35}, {880, 35}, {1319, 60}, {0, 0}};
const Note SND_BACK[] = {{1319, 35}, {880, 35}, {659, 60}, {0, 0}};
const Note SND_HOME[] = {{1568, 45}, {1319, 45}, {1047, 45}, {784, 45}, {0, 120},
                         {523, 50}, {784, 90}, {0, 0}};
const Note SND_SPLASH[] = {{523, 90}, {659, 90}, {784, 90}, {988, 90}, {1047, 90},
                           {0, 180}, {1568, 60}, {2093, 160}, {0, 0}};

void setMode(Mode m) {
  mode = m;
  modeSince = millis();
}

void enterMenu(int m) {
  menu = m;
  current = nullptr;
  const Menu &M = MENUS[m];
  for (int i = 0; i < M.n; i++) {
    const Game *g = M.entries[i].game;
    bests[i] = g && g->prefsNs ? loadBest(g->prefsNs) : -1;
  }
  ring.reset(M.n, menuSel[m]);
  setMode(MENU_RING);
}

void drawEntryIcon(int i, int x, int y) { drawIconAt(x, y, MENUS[menu].entries[i].icon); }

void drawMenuPanel() {
  const Menu &M = MENUS[menu];
  const Entry &e = M.entries[ring.selected];
  constexpr int X = 67;
  oled.setFont(u8g2_font_5x8_tf);
  oled.drawStr(X, 7, M.title);
  oled.drawHLine(X, 10, W - X);
  oled.setFont(u8g2_font_6x10_tf);
  oled.drawStr(X, 25, e.name);
  oled.setFont(u8g2_font_5x8_tf);
  if (e.blurb) {
    oled.drawStr(X, 37, e.blurb);
  } else if (bests[ring.selected] >= 0) {
    char s[16];
    snprintf(s, sizeof s, "best %d", bests[ring.selected]);
    oled.drawStr(X, 37, s);
  }
  oled.setFont(u8g2_font_4x6_tf);
  oled.drawStr(X, M.parent >= 0 ? 57 : 63, "A: open");
  if (M.parent >= 0) oled.drawStr(X, 63, "hold: back");
}

void updateMenu(const Input &in) {
  if (mode == MENU_RING) {
    if (ring.update(in)) radialTick();
    const Menu &M = MENUS[menu];
    if (in.press) {
      const Entry &e = M.entries[ring.selected];
      pendingGame = e.game;
      pendingMenu = e.folder;
      ring.collapse();
      play(SND_OPEN);
      setMode(LEAVING);
    } else if (in.hold && M.parent >= 0) {
      pendingGame = nullptr;
      pendingMenu = M.parent;
      ring.collapse();
      play(SND_BACK);
      setMode(LEAVING);
    }
  } else {  // LEAVING: wait for the ring to spiral in, then go
    ring.update(Input{});
    if (ring.collapsed()) {
      menuSel[menu] = ring.selected;
      if (pendingGame) {
        current = pendingGame;
        Serial.printf("opening %s\n", current->name);
        current->start();
        setMode(APP);
        return;
      }
      enterMenu(pendingMenu);
    }
  }
  oled.clearBuffer();
  ring.draw(drawEntryIcon);
  drawMenuPanel();
  oled.sendBuffer();
}

// Going home: a sunflower. Seed k sits at angle k * 137.5 degrees (the golden
// angle) and distance sqrt(k), which packs seeds evenly with no gaps, the way
// real sunflower heads grow. It blooms out, then swirls into the middle.
constexpr uint32_t BLOOM_MS = 1100;

void drawBloom(uint32_t t) {
  constexpr float GOLDEN = 2.39996323f;  // radians, = pi * (3 - sqrt(5))
  constexpr int SEEDS = 170;
  float grow = min(1.0f, t / 600.0f);
  grow = 1 - (1 - grow) * (1 - grow) * (1 - grow);  // ease out
  float sink = t < 650 ? 1 : max(0.0f, 1 - (t - 650) / float(BLOOM_MS - 650));
  sink = sink * sink;
  float spin = t / 1000.0f * 1.2f + (1 - sink) * 4;  // swirls faster as it sinks
  int seeds = lroundf(SEEDS * grow);
  for (int k = 1; k <= seeds; k++) {
    float r = 2.3f * sqrtf(k) * sink;
    float a = k * GOLDEN + spin;
    int x = W / 2 + lroundf(r * cosf(a)), y = H / 2 + lroundf(r * sinf(a));
    if (k > seeds - 12 || k % 3 == 0) oled.drawDisc(x, y, 1);  // fresh seeds are fat
    else oled.drawPixel(x, y);
  }
}

// Boot: a spirograph (hypotrochoid): a 3-tooth wheel rolling inside a
// 5-tooth ring, with the pen 5 units from the wheel's centre. It closes
// after 3 trips round. Then the name appears in the middle.
constexpr uint32_t BOOT_MS = 2200;

void drawSplash(uint32_t t) {
  constexpr float RR = 5, rr = 3, dd = 5, SCALE = 29 / 7.0f;  // 7 = RR - rr + dd
  constexpr int STEPS = 240;
  float drawn = min(1.0f, t / 1300.0f);
  drawn = 1 - (1 - drawn) * (1 - drawn);  // ease out
  int steps = lroundf(STEPS * drawn);
  float spin = t / 1000.0f * 0.6f;
  int px = 0, py = 0;
  for (int k = 0; k <= steps; k++) {
    float th = k * 6 * PI / STEPS;
    float x = (RR - rr) * cosf(th) + dd * cosf((RR - rr) / rr * th);
    float y = (RR - rr) * sinf(th) - dd * sinf((RR - rr) / rr * th);
    float xr = x * cosf(spin) - y * sinf(spin), yr = x * sinf(spin) + y * cosf(spin);
    int sx = W / 2 + lroundf(xr * SCALE), sy = H / 2 + lroundf(yr * SCALE);
    if (k > 0) oled.drawLine(px, py, sx, sy);
    px = sx;
    py = sy;
  }
  if (t > 1400) {
    oled.setFont(u8g2_font_9x15B_tf);
    const char *name = "MARTIN OS";
    int w = oled.getStrWidth(name);
    oled.setDrawColor(0);
    oled.drawBox((W - w) / 2 - 3, H / 2 - 8, w + 6, 16);
    oled.setDrawColor(1);
    oled.drawFrame((W - w) / 2 - 3, H / 2 - 8, w + 6, 16);
    oled.drawStr((W - w) / 2, H / 2 + 5, name);
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\nmartin-os starting");
  osBegin();
  setMode(BOOT);
  play(SND_SPLASH);
}

void loop() {
  updateSound();
  static uint32_t lastFrame = 0;
  uint32_t now = millis();
  if (now - lastFrame < FRAME_MS) return;
  lastFrame = now;
  frame++;

  Input in = readInput();

  // Screen sleep. The touch that wakes the screen does nothing else: input
  // is ignored until everything is let go. Demos keep the screen on.
  constexpr uint32_t SLEEP_MS = 60000;
  static uint32_t lastInput = 0;
  static bool asleep = false, waking = false;
  bool any = in.x != 0 || in.y != 0 || in.aHeld || digitalRead(25) == LOW;
  if (any || (mode == APP && menu == 2)) lastInput = now;
  if (asleep) {
    if (!any) return;
    asleep = false;
    waking = true;
    oled.setPowerSave(0);
  }
  if (waking) {
    if (!any) waking = false;
    in = Input{};
  }
  if (now - lastInput > SLEEP_MS) {
    asleep = true;
    oled.setPowerSave(1);
    Serial.println("screen off");
    return;
  }

  uint32_t t = now - modeSince;
  switch (mode) {
    case BOOT:
      if (t >= BOOT_MS || in.press) { enterMenu(0); break; }  // any press skips
      oled.clearBuffer();
      drawSplash(t);
      oled.sendBuffer();
      break;
    case MENU_RING:
    case LEAVING:
      updateMenu(in);
      break;
    case APP:
      if (in.hold) {
        Serial.println("back");
        play(SND_HOME);
        setMode(GOING_HOME);
        break;
      }
      current->update(in);
      break;
    case GOING_HOME:
      if (t >= BLOOM_MS) { enterMenu(menu); break; }
      oled.clearBuffer();
      drawBloom(t);
      oled.sendBuffer();
      break;
  }
}
