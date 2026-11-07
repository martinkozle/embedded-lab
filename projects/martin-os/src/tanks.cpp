// Tanks: top-down tank battle. Defend the core at the bottom of a generated
// fort from 20 enemy tanks per level. Brick walls can be shot away, steel
// can't (until fully upgraded), water stops tanks but not shells, bushes
// hide whatever is under them.
// Stick: drive. A: fire (hold to keep firing).
#include "os.h"
#include "shade.h"

namespace {

constexpr int TW = 24, TH = 16, TS = 4;  // map in tiles of 4x4 px: 96x64
constexpr int FW = TW * TS;              // field width; the HUD is to its right
enum Tile : uint8_t { EMPTY, BRICK, STEEL, WATER, BUSH, BASE };
uint8_t map[TH][TW];

enum Type { PLAYER, BASIC, FAST, SHOOTER, ARMOR };
struct TypeInfo { float speed, shellSpeed; int hp, score, fireOneIn; };
const TypeInfo INFO[] = {
    {1.0f, 2.6f, 1, 0, 0},     // player
    {0.45f, 2.0f, 1, 100, 45}, // basic: slow
    {1.1f, 2.2f, 1, 200, 55},  // fast
    {0.7f, 3.2f, 1, 300, 25},  // shooter: fast shells, fires often
    {0.45f, 2.2f, 4, 400, 40}, // armor: takes 4 hits
};

struct Tank {
  bool alive, spawning, carrier;
  int type, dir, hp, anim, hitFlash;
  int x, y;           // top-left pixel; tanks are 8x8
  float acc;          // sub-pixel movement
  uint32_t since, turnAt, shieldUntil;
};
constexpr int MAX_ENEMIES = 5;
Tank player, enemies[MAX_ENEMIES];

struct Shell { bool alive; float x, y; int dir; float speed; bool mine, steel; int owner; };  // owner: enemy index, -1 player
constexpr int MAX_SHELLS = 12;
Shell shells[MAX_SHELLS];

struct Boom { bool alive; int x, y; uint32_t at; bool big; };
Boom booms[8];

enum Power { P_UPGRADE, P_BOMB, P_FREEZE, P_FORTIFY, P_SHIELD, P_LIFE, NUM_POWERS };
struct { bool alive; int type, x, y; uint32_t until; } power;

enum Phase { INTRO, PLAY, LOST_BASE, CLEAR, OVER };
Phase phase;
uint32_t phaseAt;
int level, score, best, lives, upgrade;
int queue[20], queued, killed;
uint32_t nextSpawn, freezeUntil, fortifyUntil, respawnAt, fireAt;
bool newBest, baseDead;
int spawnTurn = 0;

const Note SND_FIRE[] = {{880, 15}, {620, 20}, {0, 0}};
const Note SND_BRICK[] = {{180, 15}, {120, 20}, {0, 0}};
const Note SND_STEEL[] = {{2000, 10}, {1500, 15}, {0, 0}};
const Note SND_KILL[] = {{300, 20}, {180, 25}, {100, 45}, {0, 0}};
const Note SND_HIT[] = {{1200, 15}, {0, 0}};
const Note SND_DIE[] = {{400, 60}, {300, 60}, {200, 80}, {120, 160}, {0, 0}};
const Note SND_POWER[] = {{784, 40}, {1047, 40}, {1319, 40}, {1568, 80}, {0, 0}};
const Note SND_INTRO[] = {{392, 90}, {523, 90}, {659, 90}, {523, 90}, {784, 220}, {0, 0}};

constexpr int DX[] = {0, 1, 0, -1}, DY[] = {-1, 0, 1, 0};  // up right down left
constexpr int BASE_X = 44, BASE_Y = 56, P_SPAWN_X = 32, P_SPAWN_Y = 56;
const int SPAWN_X[] = {0, 44, 88};

// --- Map ------------------------------------------------------------------------
void setFort(uint8_t t) {
  for (int ty = 13; ty < TH; ty++)
    for (int tx = 10; tx <= 13; tx++)
      if (map[ty][tx] != BASE) map[ty][tx] = t;
}

// Map blocks are 2x2 tiles, mirrored left-right so levels look built.
void setBlock(int bx, int by, uint8_t t) {
  if (bx < 0 || bx > 5 || by < 0 || by > 7) return;
  for (int m = 0; m < 2; m++) {
    int cx = m ? 11 - bx : bx;
    for (int dy = 0; dy < 2; dy++)
      for (int dx = 0; dx < 2; dx++) map[by * 2 + dy][cx * 2 + dx] = t;
  }
}

bool passable2(int tx, int ty) {  // can a tank get through, shooting bricks?
  for (int dy = 0; dy < 2; dy++)
    for (int dx = 0; dx < 2; dx++) {
      uint8_t t = map[ty + dy][tx + dx];
      if (t == STEEL || t == WATER || t == BASE) return false;
    }
  return true;
}

bool connected() {  // every spawn point can reach the player's spawn
  static uint8_t seen[TH][TW];
  int qx[TW * TH], qy[TW * TH];
  for (int s = 0; s < 3; s++) {
    memset(seen, 0, sizeof seen);
    int head = 0, tail = 0;
    qx[tail] = SPAWN_X[s] / TS; qy[tail++] = 0;
    seen[0][SPAWN_X[s] / TS] = 1;
    bool ok = false;
    while (head < tail) {
      int x = qx[head], y = qy[head++];
      if (x == P_SPAWN_X / TS && y == P_SPAWN_Y / TS) { ok = true; break; }
      for (int d = 0; d < 4; d++) {
        int nx = x + DX[d], ny = y + DY[d];
        if (nx < 0 || ny < 0 || nx > TW - 2 || ny > TH - 2 || seen[ny][nx] || !passable2(nx, ny)) continue;
        seen[ny][nx] = 1;
        qx[tail] = nx; qy[tail++] = ny;
      }
    }
    if (!ok) return false;
  }
  return true;
}

void generate() {
  for (int attempt = 0; attempt < 40; attempt++) {
    memset(map, EMPTY, sizeof map);
    int features = 11 + random(5);
    for (int f = 0; f < features; f++) {
      int k = random(100), bx = random(6), by = random(1, 7);
      if (k < 55) {  // a brick wall
        bool horiz = random(2), steelBit = random(100) < 8 + level * 3;
        int len = random(2, 5);
        for (int i = 0; i < len; i++)
          setBlock(bx + (horiz ? i : 0), by + (horiz ? 0 : i), steelBit && i == len / 2 ? STEEL : BRICK);
      } else if (k < 68) {  // a steel block, more of them later on
        setBlock(bx, by, random(100) < 45 + level * 5 ? STEEL : BRICK);
      } else if (k < 80) {  // a pond
        setBlock(bx, by, WATER);
        setBlock(bx + 1, by, WATER);
      } else {  // a bush patch
        for (int i = 0; i < 4; i++) setBlock(bx + i % 2, by + i / 2, BUSH);
      }
    }
    // Keep the spawn points, the player's spawn and the fort clear.
    setBlock(0, 0, EMPTY); setBlock(5, 0, EMPTY);
    setBlock(4, 7, EMPTY); setBlock(5, 7, EMPTY); setBlock(5, 6, EMPTY);
    setFort(BRICK);
    for (int ty = 14; ty < TH; ty++)
      for (int tx = 11; tx <= 12; tx++) map[ty][tx] = BASE;
    if (connected()) return;
  }
  // Very unlucky: an open field with just the fort.
  memset(map, EMPTY, sizeof map);
  setFort(BRICK);
  for (int ty = 14; ty < TH; ty++)
    for (int tx = 11; tx <= 12; tx++) map[ty][tx] = BASE;
}

// --- Tanks ----------------------------------------------------------------------
bool solidTile(int tx, int ty) {
  if (tx < 0 || ty < 0 || tx >= TW || ty >= TH) return true;
  uint8_t t = map[ty][tx];
  return t == BRICK || t == STEEL || t == WATER || t == BASE;
}

bool overlaps(int ax, int ay, int bx, int by) { return abs(ax - bx) < 8 && abs(ay - by) < 8; }

bool blocked(const Tank &me, int x, int y) {
  if (x < 0 || y < 0 || x > FW - 8 || y > 64 - 8) return true;
  for (int ty = y / TS; ty <= (y + 7) / TS; ty++)
    for (int tx = x / TS; tx <= (x + 7) / TS; tx++)
      if (solidTile(tx, ty)) return true;
  if (&me != &player && player.alive && overlaps(x, y, player.x, player.y) && !overlaps(me.x, me.y, player.x, player.y))
    return true;
  for (auto &e : enemies)
    if (&e != &me && e.alive && overlaps(x, y, e.x, e.y) && !overlaps(me.x, me.y, e.x, e.y)) return true;
  return false;
}

// Turn, snapping to the 4 px grid across the new direction so the tank
// lines up with corridors. Then drive `speed` px. False if it bumped.
bool drive(Tank &t, int dir, float speed) {
  if (dir != t.dir) {
    if ((dir & 1) != (t.dir & 1)) {
      int sx = t.x, sy = t.y;
      if (dir & 1) sy = (t.y + 2) / 4 * 4;
      else sx = (t.x + 2) / 4 * 4;
      if (!blocked(t, sx, sy)) { t.x = sx; t.y = sy; }
    }
    t.dir = dir;
  }
  t.acc += speed;
  bool moved = true;
  while (t.acc >= 1) {
    t.acc -= 1;
    int nx = t.x + DX[dir], ny = t.y + DY[dir];
    if (blocked(t, nx, ny)) { moved = false; t.acc = 0; break; }
    t.x = nx; t.y = ny;
    t.anim++;
  }
  return moved;
}

int shellsOf(int owner) {
  int n = 0;
  for (auto &s : shells) n += s.alive && s.owner == owner;
  return n;
}

void fire(const Tank &t, int owner) {
  bool mine = owner < 0;
  for (auto &s : shells) {
    if (s.alive) continue;
    float speed = INFO[t.type].shellSpeed + (mine && upgrade >= 1 ? 1.2f : 0);
    s = {true, float(t.x + 3 + DX[t.dir] * 4), float(t.y + 3 + DY[t.dir] * 4), t.dir, speed, mine,
         mine && upgrade >= 3, owner};
    if (mine) play(SND_FIRE);
    return;
  }
}

void boom(int x, int y, bool big) {
  for (auto &b : booms)
    if (!b.alive) { b = {true, x, y, millis(), big}; return; }
}

// --- Rules ----------------------------------------------------------------------
void startLevel() {
  generate();
  for (auto &e : enemies) e.alive = false;
  for (auto &s : shells) s.alive = false;
  for (auto &b : booms) b.alive = false;
  power.alive = false;
  baseDead = false;
  // The 20 enemies: tougher kinds show up as levels go on.
  for (int i = 0; i < 20; i++) {
    int r = random(100);
    int armor = min(30, level * 4), shooter = min(30, 5 + level * 4), fast = 25;
    queue[i] = r < armor ? ARMOR : r < armor + shooter ? SHOOTER : r < armor + shooter + fast ? FAST : BASIC;
  }
  queued = killed = 0;
  player = {};
  player.alive = true;
  player.type = PLAYER;
  player.x = P_SPAWN_X; player.y = P_SPAWN_Y;
  player.shieldUntil = millis() + 3000;
  freezeUntil = fortifyUntil = 0;
  nextSpawn = millis() + 1800;
  phase = INTRO;
  phaseAt = millis();
  play(SND_INTRO);
  Serial.printf("tanks: level %d\n", level);
}

void start() {
  level = 1;
  score = 0;
  lives = 3;
  upgrade = 0;
  newBest = false;
  best = loadBest("tanks");
  startLevel();
}

void endGame() {
  phase = OVER;
  phaseAt = millis();
  newBest = score > best;
  if (newBest) { best = score; saveBest("tanks", best); }
  play(newBest ? SND_BEST : SND_OVER);
  Serial.printf("tanks: game over, level %d score %d\n", level, score);
}

void killPlayer() {
  boom(player.x + 4, player.y + 4, true);
  player.alive = false;
  upgrade = 0;
  play(SND_DIE);
  if (--lives < 0) { phase = LOST_BASE; phaseAt = millis(); }
  else respawnAt = millis() + 1200;
}

void killEnemy(Tank &e) {
  e.alive = false;
  killed++;
  score += INFO[e.type].score;
  boom(e.x + 4, e.y + 4, true);
  play(SND_KILL);
  if (e.carrier) {  // drops a power-up somewhere on the field
    power = {true, (int)random(NUM_POWERS), (int)random(0, TW / 2) * 8, (int)random(1, TH / 2 - 1) * 8,
             millis() + 12000};
    if (power.x > FW - 8) power.x = FW - 8;
  }
}

void collect() {
  score += 500;
  play(SND_POWER);
  switch (power.type) {
    case P_UPGRADE: upgrade = min(3, upgrade + 1); break;
    case P_BOMB:
      for (auto &e : enemies)
        if (e.alive && !e.spawning) killEnemy(e);
      break;
    case P_FREEZE: freezeUntil = millis() + 8000; break;
    case P_FORTIFY: setFort(STEEL); fortifyUntil = millis() + 15000; break;
    case P_SHIELD: player.shieldUntil = millis() + 10000; break;
    case P_LIFE: lives++; break;
  }
  power.alive = false;
}

// A shell's front edge entered a wall: knock out the 2 tiles across its path.
bool hitWall(Shell &s, int px, int py) {
  int tx = px / TS, ty = py / TS;
  if (!solidTile(tx, ty) || (tx >= 0 && ty >= 0 && tx < TW && ty < TH && map[ty][tx] == WATER)) return false;
  int cx = lroundf(s.x) + 1, cy = lroundf(s.y) + 1;  // shell centre
  bool sound = false, steel = false;
  for (int k = -2; k <= 1; k += 3) {
    int ax = s.dir & 1 ? tx : (cx + k) / TS, ay = s.dir & 1 ? (cy + k) / TS : ty;
    if (ax < 0 || ay < 0 || ax >= TW || ay >= TH) continue;
    uint8_t &t = map[ay][ax];
    if (t == BRICK) { t = EMPTY; sound = true; }
    else if (t == STEEL) { steel = true; if (s.steel) { t = EMPTY; sound = true; } }
    else if (t == BASE && phase == PLAY) { baseDead = true; phase = LOST_BASE; phaseAt = millis(); boom(BASE_X + 4, BASE_Y + 4, true); play(SND_DIE); }
  }
  if (s.mine) play(sound ? SND_BRICK : steel ? SND_STEEL : SND_BRICK);
  boom(cx, cy, false);
  return true;
}

void updateShells() {
  for (auto &s : shells) {
    if (!s.alive) continue;
    for (float moved = 0; moved < s.speed && s.alive; moved += 1) {
      s.x += DX[s.dir];
      s.y += DY[s.dir];
      int x = lroundf(s.x), y = lroundf(s.y);
      if (x < 0 || y < 0 || x > FW - 2 || y > 62) { s.alive = false; boom(x + 1, y + 1, false); break; }
      // The two pixels on the front edge.
      int fx0 = x + (s.dir == 1), fy0 = y + (s.dir == 2);
      int fx1 = fx0 + (s.dir & 1 ? 0 : 1), fy1 = fy0 + (s.dir & 1 ? 1 : 0);
      if (hitWall(s, fx0, fy0) || hitWall(s, fx1, fy1)) { s.alive = false; break; }
      if (s.mine) {
        for (auto &e : enemies) {
          if (!e.alive || e.spawning || x + 1 < e.x || x > e.x + 7 || y + 1 < e.y || y > e.y + 7) continue;
          s.alive = false;
          if (--e.hp <= 0) killEnemy(e);
          else { e.hitFlash = 4; play(SND_HIT); }
          break;
        }
      } else if (player.alive && x + 1 >= player.x && x <= player.x + 7 && y + 1 >= player.y && y <= player.y + 7) {
        s.alive = false;
        if (millis() >= player.shieldUntil) killPlayer();
        else play(SND_STEEL);
      }
    }
  }
  // Shells that meet cancel out.
  for (auto &a : shells)
    for (auto &b : shells)
      if (a.alive && b.alive && a.mine && !b.mine && fabsf(a.x - b.x) < 3 && fabsf(a.y - b.y) < 3) {
        a.alive = b.alive = false;
        boom(lroundf(a.x) + 1, lroundf(a.y) + 1, false);
      }
}

int chooseDir(const Tank &e) {
  int r = random(100);
  auto toward = [&](int tx, int ty) {
    int dx = tx - e.x, dy = ty - e.y;
    if (abs(dx) > abs(dy) ? random(100) < 70 : random(100) < 30) return dx > 0 ? 1 : 3;
    return dy > 0 ? 2 : 0;
  };
  if (r < 45) return toward(BASE_X, BASE_Y);
  if (r < 65 && player.alive) return toward(player.x, player.y);
  return random(4);
}

void updateEnemies(uint32_t now) {
  // Spawning: up to 4 at a time (5 from level 5), one every couple of seconds.
  int onField = 0;
  for (auto &e : enemies) onField += e.alive;
  int maxOn = level >= 5 ? 5 : 4;
  if (queued < 20 && onField < maxOn && now >= nextSpawn) {
    for (int tries = 0; tries < 3; tries++) {
      int sx = SPAWN_X[spawnTurn];
      spawnTurn = (spawnTurn + 1) % 3;
      bool free = !(player.alive && overlaps(sx, 0, player.x, player.y));
      for (auto &e : enemies) free &= !(e.alive && overlaps(sx, 0, e.x, e.y));
      if (!free) continue;
      for (auto &e : enemies) {
        if (e.alive) continue;
        int type = queue[queued];
        e = {true, true, queued == 3 || queued == 10 || queued == 17, type, 2, INFO[type].hp, 0, 0,
             sx, 0, 0, now, now + 2000, 0};
        queued++;
        break;
      }
      break;
    }
    nextSpawn = now + max(1100, 2400 - level * 120);
  }

  bool frozen = now < freezeUntil;
  for (auto &e : enemies) {
    if (!e.alive) continue;
    if (e.hitFlash) e.hitFlash--;
    if (e.spawning) {
      if (now - e.since > 900) e.spawning = false;
      continue;
    }
    if (frozen) continue;
    const TypeInfo &info = INFO[e.type];
    bool moved = drive(e, e.dir, info.speed);
    if ((!moved && random(100) < 40) || now >= e.turnAt) {
      drive(e, chooseDir(e), 0);
      e.turnAt = now + random(800, 3000);
    }
    int oneIn = moved ? info.fireOneIn : max(6, info.fireOneIn / 3);  // stuck: dig
    int idx = &e - enemies;
    if (shellsOf(idx) == 0 && random(oneIn) == 0) fire(e, idx);  // one shell each
  }
}

// --- Drawing ----------------------------------------------------------------------
// Tank pixels in the "facing up" frame; u across, v down, both 0..7.
bool tankPixel(int type, int u, int v, int anim) {
  if (v >= 1 && (u == 0 || u == 7)) return true;                       // tread edges
  if (v >= 1 && (u == 1 || u == 6)) return ((v + anim) & 1) == 0;      // rolling treads
  bool barrel = type == SHOOTER ? (u == 2 || u == 5) && v <= 3 : (u == 3 || u == 4) && v <= 3;
  if (barrel) return true;
  if (u < 2 || u > 5 || v < 3) return false;
  bool edge = u == 2 || u == 5 || v == 3 || v == 7;
  switch (type) {
    case PLAYER: return true;
    case FAST: return v >= 4 && (u == 2 || u == 5 || v == 4 || v == 7);
    case ARMOR: return !(u >= 3 && u <= 4 && v >= 4 && v <= 6) || v == 5;
    case SHOOTER: return edge || (v == 5 && (u == 3 || u == 4));
    default: return edge;
  }
}

void drawTank(const Tank &t, bool invert) {
  if (invert) oled.drawBox(t.x, t.y, 8, 8);
  oled.setDrawColor(invert ? 0 : 1);
  for (int v = 0; v < 8; v++)
    for (int u = 0; u < 8; u++) {
      if (!tankPixel(t.type, u, v, t.anim)) continue;
      int x, y;
      switch (t.dir) {
        case 0: x = u; y = v; break;
        case 1: x = 7 - v; y = u; break;
        case 2: x = 7 - u; y = 7 - v; break;
        default: x = v; y = 7 - u; break;
      }
      oled.drawPixel(t.x + x, t.y + y);
    }
  oled.setDrawColor(1);
}

const char *const CORE[8] = {"...##...", "..####..", ".##..##.", "##.##.##",
                             "##.##.##", ".##..##.", "..####..", "...##..."};
const char *const RUBBLE[8] = {"........", "........", "#.......", ".#..#..#",
                               "#.##.#..", ".######.", "#.#####.", "########"};
const char *const POWER_ICONS[NUM_POWERS][6] = {
    {"..##..", "..##..", "######", "######", "..##..", "..##.."},  // upgrade
    {"....#.", "...#..", ".####.", "######", "######", ".####."},  // bomb
    {"#.##.#", ".####.", "##..##", "##..##", ".####.", "#.##.#"},  // freeze
    {"#.#.#.", "######", "##.###", "######", "###.##", "######"},  // fortify
    {"######", "#....#", "#.##.#", ".#..#.", ".#..#.", "..##.."},  // shield
    {"..##..", "#.##.#", "######", "#.##.#", "######", "#....#"},  // extra life
};

void drawMap(bool bushes) {
  uint8_t *buf = oled.getBufferPtr();
  int wave = (frame / 8) & 3;
  for (int ty = 0; ty < TH; ty++)
    for (int tx = 0; tx < TW; tx++) {
      uint8_t t = map[ty][tx];
      if ((t == BUSH) != bushes) continue;
      for (int py = 0; py < 4; py++)
        for (int px = 0; px < 4; px++) {
          int x = tx * TS + px, y = ty * TS + py;
          bool on;
          switch (t) {
            case BRICK: on = py == 0 ? px < 3 : py == 2 ? px != 1 : false; break;
            case STEEL: on = px == 0 || py == 0 || px == 3 || py == 3; break;
            case WATER: on = ((px + wave) & 3) == 0 && (py & 1) == ((px + wave) >> 2 & 1); break;
            case BUSH: on = ((x + y) & 1) == 0; break;
            default: continue;
          }
          if (t == BUSH) buf[(y >> 3) * W + x] &= ~(1 << (y & 7));  // hide what's under
          if (on) plot(buf, x, y);
        }
    }
}

void drawSprite8(int x, int y, const char *const rows[8]) {
  for (int v = 0; v < 8; v++)
    for (int u = 0; u < 8; u++)
      if (rows[v][u] == '#') oled.drawPixel(x + u, y + v);
}

void drawHud() {
  constexpr int X = FW + 2;
  oled.drawVLine(FW, 0, H);
  oled.setFont(u8g2_font_4x6_tf);
  char s[16];
  snprintf(s, sizeof s, "L%d", level);
  oled.drawStr(X, 6, s);
  // One dot per enemy still to come.
  for (int i = 0; i < 20 - queued; i++) oled.drawBox(X + (i % 5) * 6, 10 + (i / 5) * 4, 3, 2);
  snprintf(s, sizeof s, "P%d", max(0, lives));
  oled.drawStr(X, 35, s);
  for (int i = 0; i < upgrade; i++) oled.drawBox(X + 14 + i * 4, 31, 2, 4);
  snprintf(s, sizeof s, "%d", score);
  oled.drawStr(X, 45, s);
  uint32_t now = millis();
  if (now < freezeUntil) oled.drawStr(X, 54, "FREEZE");
  else if (now < fortifyUntil) oled.drawStr(X, 54, "FORT");
  oled.drawStr(X, 63, "A:fire");
}

void drawField(uint32_t now) {
  oled.clearBuffer();
  drawMap(false);
  drawSprite8(BASE_X, BASE_Y, baseDead ? RUBBLE : CORE);
  if (power.alive && (power.until - now > 3000 || (frame / 3) % 2)) {
    oled.drawBox(power.x, power.y, 8, 8);
    oled.setDrawColor(0);
    for (int v = 0; v < 6; v++)
      for (int u = 0; u < 6; u++)
        if (POWER_ICONS[power.type][v][u] == '#') oled.drawPixel(power.x + 1 + u, power.y + 1 + v);
    oled.setDrawColor(1);
  }
  if (player.alive) {
    drawTank(player, false);
    if (now < player.shieldUntil && (frame & 1))
      oled.drawCircle(player.x + 3, player.y + 3, 6);  // shield shimmer
  }
  for (auto &e : enemies) {
    if (!e.alive) continue;
    if (e.spawning) {  // a twinkling star where it beams in
      int r = 1 + (frame % 4) * 1;
      int cx = e.x + 4, cy = e.y + 4;
      oled.drawHLine(cx - r - 1, cy, 2 * r + 3);
      oled.drawVLine(cx, cy - r - 1, 2 * r + 3);
      oled.drawPixel(cx - r / 2, cy - r / 2); oled.drawPixel(cx + r / 2, cy + r / 2);
      oled.drawPixel(cx - r / 2, cy + r / 2); oled.drawPixel(cx + r / 2, cy - r / 2);
      continue;
    }
    drawTank(e, e.hitFlash > 0 || (e.carrier && (frame / 4) % 2));
  }
  for (auto &s : shells)
    if (s.alive) oled.drawBox(lroundf(s.x), lroundf(s.y), 2, 2);
  drawMap(true);  // bushes over everything
  for (auto &b : booms) {
    if (!b.alive) continue;
    uint32_t t = now - b.at;
    uint32_t life = b.big ? 450 : 150;
    if (t > life) { b.alive = false; continue; }
    int r = b.big ? 2 + t / 40 : 1 + t / 50;
    oled.drawCircle(b.x, b.y, r);
    if (b.big) {  // debris flying out
      for (int k = 0; k < 8; k++) {
        float a = k * 0.785f + b.x;
        oled.drawPixel(b.x + lroundf(cosf(a) * r * 1.6f), b.y + lroundf(sinf(a) * r * 1.6f));
      }
      if (t < 120) oled.drawDisc(b.x, b.y, r - 1);
    }
  }
  drawHud();
}

// --- Frame --------------------------------------------------------------------------
void update(const Input &in) {
  uint32_t now = millis();
  if (phase == OVER) {
    drawGameOver(score, best, newBest);
    if (in.press && now - phaseAt > 800) start();
    return;
  }

  if (phase == INTRO) {
    drawField(now);
    uint32_t t = now - phaseAt;
    // Curtains open from the middle.
    int gap = t < 700 ? 0 : min(32, (int)(t - 700) / 12);
    oled.setDrawColor(0);
    oled.drawBox(0, 0, FW, 32 - gap);
    oled.drawBox(0, 32 + gap, FW, 32 - gap);
    oled.setDrawColor(1);
    if (gap < 8) {
      char s[16];
      snprintf(s, sizeof s, "LEVEL %d", level);
      oled.setFont(u8g2_font_6x10_tf);
      oled.drawStr((FW - oled.getStrWidth(s)) / 2, 36, s);
    }
    oled.sendBuffer();
    if (t > 1200) phase = PLAY;
    return;
  }

  if (phase == PLAY) {
    // Player: drive along the stick's stronger axis.
    if (player.alive) {
      float ax = fabsf(in.x), ay = fabsf(in.y);
      if (max(ax, ay) > 0.35f) {
        int dir = ax > ay ? (in.x > 0 ? 1 : 3) : (in.y > 0 ? 2 : 0);
        drive(player, dir, INFO[PLAYER].speed);
      }
      int maxShells = upgrade >= 2 ? 2 : 1;
      if ((in.a || (in.aHeld && now >= fireAt)) && shellsOf(-1) < maxShells) {
        fire(player, -1);
        fireAt = now + 250;
      }
      if (power.alive && overlaps(player.x, player.y, power.x, power.y)) collect();
    } else if (now >= respawnAt && lives >= 0) {
      player.alive = true;
      player.x = P_SPAWN_X; player.y = P_SPAWN_Y;
      player.dir = 0;
      player.shieldUntil = now + 3000;
    }
    updateEnemies(now);
    updateShells();
    if (power.alive && now > power.until) power.alive = false;
    if (fortifyUntil && now > fortifyUntil) { setFort(BRICK); fortifyUntil = 0; }

    if (killed >= 20) {
      phase = CLEAR;
      phaseAt = now;
      score += 1000;
      play(SND_CLEAR);
    }
  }

  drawField(now);
  if (phase == CLEAR) {
    oled.setFont(u8g2_font_6x10_tf);
    oled.setDrawColor(0);
    oled.drawBox(14, 22, 68, 22);
    oled.setDrawColor(1);
    oled.drawFrame(14, 22, 68, 22);
    oled.drawStr(18, 32, "CLEAR!");
    oled.setFont(u8g2_font_4x6_tf);
    oled.drawStr(18, 40, "bonus 1000");
    if (now - phaseAt > 2200) { level++; startLevel(); }
  }
  if (phase == LOST_BASE) {
    updateShells();
    if (now - phaseAt > 1800) endGame();
  }
  oled.sendBuffer();
}

}  // namespace

const Game TANKS = {"Tanks", "tanks", start, update};
