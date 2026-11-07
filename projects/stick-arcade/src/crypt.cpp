// Crypt: a first-person shooter in a haunted crypt.
//
// Each level is a fresh cluster of stone rooms joined by short passages, with
// the odd shortcut loop and pillars in big rooms. Find the key, then reach the
// barred exit gate. Ghosts float toward you, skeletons keep their distance and
// throw bones. Hearts heal you.
//
// Controls (one stick, one button):
//   stick forward/back   walk
//   stick left/right     turn, or strafe around your target while locked on
//   tap A                fire the staff; locks onto what you hit for 2 s
//   hold A, release      throw a holy orb that explodes (5 s cooldown)
//   stick press          show the map for a few seconds
// Score: 10 per kill, 50 per level cleared.
//
// Aiming borrows from console shooters that had only one stick: shots snap
// to an enemy roughly under the crosshair (like Doom's auto-aim), and hitting
// one locks on (like Metroid Prime), so left/right sidesteps around it while
// the view stays on target.
//
// Rendering builds on the Maze 3D raycaster (see maze.cpp) and adds a brick
// texture, a depth buffer (wall distance per column, to hide sprites behind
// walls) and sprites: 16x16 one-bit pictures scaled by distance, with a black
// outline so they stand out against the bricks.
#include "arcade.h"

namespace {

constexpr const char *NS = "crypt";
constexpr int MW = 32, MH = 24;      // map size in tiles (2 px each on the map)
constexpr float MOVE_SPEED = 0.07f;  // tiles per frame at full tilt
constexpr float STRAFE_SPEED = 0.06f;
constexpr float TURN_SPEED = 0.08f;  // radians per frame at full tilt
constexpr float LOCK_TURN = 0.12f;   // how fast the view follows a locked target
constexpr float RADIUS = 0.2f;       // how close you can get to a wall
constexpr float FAR = 7.0f;          // beyond this, walls are outlines only
constexpr int MAX_HP = 5;
constexpr uint32_t SHOT_COOLDOWN = 300;
constexpr uint32_t LOCK_MS = 2000;     // lock lasts this long after a hit
constexpr uint32_t CHARGE_MS = 350;    // hold A this long to start an orb
constexpr uint32_t ORB_COOLDOWN = 5000;
constexpr float ORB_RANGE = 2.0f;      // explosion radius, in tiles
constexpr uint32_t HURT_GRACE = 900;   // no damage again for this long
constexpr uint32_t MAP_MS = 2500;
constexpr int MAX_ENTS = 40;

enum Tile : uint8_t { FLOOR = 0, WALL = 1, GATE = 2 };
uint8_t map_[MH][MW];

// --- Sprites -------------------------------------------------------------------
struct Bitmap {
  uint16_t shape[16], mask[16];  // mask = shape grown by one pixel (outline)
};

const char *const GHOST_ART[16] = {
    "......####......", "....########....", "...##########...", "..############..",
    "..###..##..###..", "..###..##..###..", ".##############.", ".##############.",
    ".#####....#####.", ".####......####.", ".##############.", ".##############.",
    ".##############.", ".##.###..###.##.", ".#...##..##...#.", "................"};
const char *const SKELETON_ART[16] = {
    ".....######.....", "....########....", "....#..##..#....", "....########....",
    ".....#.##.#.....", "......####......", "...##########...", "..#....##....#..",
    "..#.########.#..", "..#....##....#..", "..#.########.#..", ".......##.......",
    "......####......", ".....##..##.....", ".....#....#.....", "....##....##...."};
const char *const KEY_ART[16] = {
    "................", "................", "................", "................",
    "..####..........", ".#....#.........", "#..##..#########", "#..##..#########",
    ".#....#....##.##", "..####.....##.##", "................", "................",
    "................", "................", "................", "................"};
const char *const HEART_ART[16] = {
    "................", "..####....####..", ".######..######.", "################",
    "################", "################", ".##############.", "..############..",
    "...##########...", "....########....", ".....######.....", "......####......",
    ".......##.......", "................", "................", "................"};
const char *const BONE_ART[16] = {
    "................", "................", "................", "................",
    "................", "................", ".##..........##.", "################",
    "################", ".##..........##.", "................", "................",
    "................", "................", "................", "................"};
const char *const ORB_ART[16] = {
    "................", ".....######.....", "...##########...", "..####....####..",
    "..###......###..", ".###...##...###.", ".###..####..###.", ".##..######..##.",
    ".##..######..##.", ".###..####..###.", ".###...##...###.", "..###......###..",
    "..####....####..", "...##########...", ".....######.....", "................"};

Bitmap ghostBmp, skeletonBmp, keyBmp, heartBmp, boneBmp, orbBmp;

void build(Bitmap &b, const char *const art[16]) {
  for (int y = 0; y < 16; y++) {
    b.shape[y] = 0;
    for (int x = 0; x < 16; x++)
      if (art[y][x] == '#') b.shape[y] |= 1 << (15 - x);
  }
  for (int y = 0; y < 16; y++) {
    uint16_t m = 0;
    for (int dy = -1; dy <= 1; dy++) {
      int yy = y + dy;
      if (yy < 0 || yy > 15) continue;
      uint16_t s = b.shape[yy];
      m |= s | (s << 1) | (s >> 1);
    }
    b.mask[y] = m;
  }
}

// --- Sounds --------------------------------------------------------------------
const Note SND_SHOT[] = {{1500, 25}, {1000, 25}, {600, 30}, {0, 0}};
const Note SND_HIT[] = {{220, 40}, {0, 0}};
const Note SND_KILL[] = {{330, 50}, {262, 50}, {196, 90}, {0, 0}};
const Note SND_HURT[] = {{140, 90}, {110, 120}, {0, 0}};
const Note SND_NOPE[] = {{196, 80}, {0, 40}, {196, 80}, {0, 0}};
const Note SND_THROW[] = {{400, 30}, {600, 30}, {800, 40}, {0, 0}};
const Note SND_BOOM[] = {{220, 40}, {160, 60}, {120, 80}, {90, 140}, {0, 0}};
const Note SND_READY[] = {{1760, 30}, {0, 0}};

// --- World state -----------------------------------------------------------------
enum Kind : uint8_t { GHOST, SKELETON, KEY, HEART, BONE, ORB, BOOM };
struct Ent {
  bool alive;
  Kind kind;
  float x, y;
  float vx, vy;      // bones and orbs
  int8_t hp;
  bool awake;        // enemies notice you once they see you
  uint32_t timer;    // skeletons: next throw
  uint32_t born;     // orbs and explosions
  uint32_t hurtAt;   // flashes when hit
  // Filled in each frame by the sprite projection:
  float depth, screenX, size;
};
Ent ents[MAX_ENTS];
Ent *lockTarget = nullptr;
uint32_t lockUntil;

bool isEnemy(const Ent &e) { return e.alive && (e.kind == GHOST || e.kind == SKELETON); }

float px, py, dirX, dirY, planeX, planeY;
float zbuf[W];  // wall distance per screen column, for hiding sprites

// Walking distance from the player to every tile. Enemies step downhill on
// it, which leads them around walls to you (recomputed when you change tile).
int16_t flow[MH][MW];
int flowTX = -1, flowTY = -1;

enum class Phase { Play, Clear, Over };
Phase phase;
int level, hp, kills, score, best;
bool hasKey, newBest;
uint32_t phaseStart, shotAt, hurtAt, mapUntil, orbAt, aDownSince;
bool charging, orbReadyBeeped;
const char *msg = nullptr;
uint32_t msgUntil;
float walkPhase;

void say(const char *text, uint32_t ms = 1500) {
  msg = text;
  msgUntil = millis() + ms;
}

bool solid(int tx, int ty) {
  if (tx < 0 || ty < 0 || tx >= MW || ty >= MH) return true;
  return map_[ty][tx] != FLOOR;
}
bool solidAt(float x, float y) { return solid((int)x, (int)y); }

Ent *spawn(Kind k, float x, float y) {
  for (auto &e : ents) {
    if (e.alive) continue;
    e = Ent{};
    e.alive = true;
    e.kind = k;
    e.x = x;
    e.y = y;
    e.hp = k == GHOST ? 2 : k == SKELETON ? 3 : 1;
    e.timer = millis() + 1500 + random(1500);
    e.born = millis();
    return &e;
  }
  return nullptr;
}

// Breadth-first flood fill: walking distance from (sx, sy) to every floor
// tile, -1 where you can't walk.
void bfs(int sx, int sy, int16_t dist[MH][MW]) {
  static uint8_t qx[MW * MH], qy[MW * MH];
  for (int y = 0; y < MH; y++)
    for (int x = 0; x < MW; x++) dist[y][x] = -1;
  if (solid(sx, sy)) return;
  int head = 0, tail = 0;
  qx[tail] = sx;
  qy[tail++] = sy;
  dist[sy][sx] = 0;
  const int DX[] = {1, -1, 0, 0}, DY[] = {0, 0, 1, -1};
  while (head < tail) {
    int x = qx[head], y = qy[head++];
    for (int d = 0; d < 4; d++) {
      int nx = x + DX[d], ny = y + DY[d];
      if (solid(nx, ny) || dist[ny][nx] >= 0) continue;
      dist[ny][nx] = dist[y][x] + 1;
      qx[tail] = nx;
      qy[tail++] = ny;
    }
  }
}

// --- Level generation ---------------------------------------------------------
// Rooms grow outward from a first room in the middle: each new room is
// attached to a random existing one, either sharing a wall with a doorway or
// joined by a passage only 2-3 tiles long. Then a couple of extra doorways
// are opened where they make a real shortcut, so the layout has loops.
// A flood fill from the start finds the farthest room for the exit gate and
// the next farthest for the key, so you have to explore.
struct Room {
  int x, y, w, h;  // interior: x..x+w-1, y..y+h-1
  int cx() const { return x + w / 2; }
  int cy() const { return y + h / 2; }
};
Room rooms[12];
int numRooms;

bool roomFits(const Room &r) {
  if (r.x < 1 || r.y < 1 || r.x + r.w > MW - 1 || r.y + r.h > MH - 1) return false;
  for (int i = 0; i < numRooms; i++) {
    const Room &o = rooms[i];
    bool apartX = o.x >= r.x + r.w + 1 || r.x >= o.x + o.w + 1;
    bool apartY = o.y >= r.y + r.h + 1 || r.y >= o.y + o.h + 1;
    if (!apartX && !apartY) return false;  // need at least one wall between
  }
  return true;
}

void carveRoom(const Room &r) {
  for (int y = r.y; y < r.y + r.h; y++)
    for (int x = r.x; x < r.x + r.w; x++) map_[y][x] = FLOOR;
}

bool tryAttach() {
  const Room &a = rooms[random(numRooms)];
  Room r{0, 0, (int)random(4, 8), (int)random(4, 7)};
  int gap = random(1, 4);  // 1 = doorway in a shared wall, 2-3 = short passage
  int side = random(4);
  int door;
  if (side < 2) {  // right or left: rooms overlap in rows
    r.x = side == 0 ? a.x + a.w + gap : a.x - gap - r.w;
    r.y = random(a.y - r.h + 1, a.y + a.h);
    int lo = max(a.y, r.y), hi = min(a.y + a.h, r.y + r.h) - 1;
    if (lo > hi) return false;
    door = random(lo, hi + 1);
  } else {  // below or above: rooms overlap in columns
    r.y = side == 2 ? a.y + a.h + gap : a.y - gap - r.h;
    r.x = random(a.x - r.w + 1, a.x + a.w);
    int lo = max(a.x, r.x), hi = min(a.x + a.w, r.x + r.w) - 1;
    if (lo > hi) return false;
    door = random(lo, hi + 1);
  }
  if (!roomFits(r)) return false;
  rooms[numRooms++] = r;
  carveRoom(r);
  for (int i = 0; i < gap; i++) {
    if (side == 0) map_[door][a.x + a.w + i] = FLOOR;
    else if (side == 1) map_[door][a.x - 1 - i] = FLOOR;
    else if (side == 2) map_[a.y + a.h + i][door] = FLOOR;
    else map_[a.y - 1 - i][door] = FLOOR;
  }
  return true;
}

void addLoops(int16_t dist[MH][MW]) {
  int opened = 0;
  for (int tries = 0; tries < 400 && opened < 2; tries++) {
    int x = random(1, MW - 1), y = random(1, MH - 1);
    if (map_[y][x] != WALL) continue;
    int a = -1, b = -1;
    if (!solid(x - 1, y) && !solid(x + 1, y) && solid(x, y - 1) && solid(x, y + 1)) {
      a = dist[y][x - 1];
      b = dist[y][x + 1];
    } else if (!solid(x, y - 1) && !solid(x, y + 1) && solid(x - 1, y) && solid(x + 1, y)) {
      a = dist[y - 1][x];
      b = dist[y + 1][x];
    }
    if (a < 0 || b < 0 || abs(a - b) < 12) continue;  // only real shortcuts
    map_[y][x] = FLOOR;
    opened++;
  }
}

void addPillars(int skipRoom) {
  for (int i = 0; i < numRooms; i++) {
    if (i == skipRoom) continue;
    const Room &r = rooms[i];
    // Pillars stay 2 tiles from the walls, so doorways are never blocked.
    if (r.w >= 7 && r.h >= 5) {
      map_[r.cy()][r.x + 2] = WALL;
      map_[r.cy()][r.x + r.w - 3] = WALL;
    } else if (r.w >= 5 && r.h >= 5) {
      map_[r.cy()][r.cx()] = WALL;
    }
  }
}

bool randomFloorIn(const Room &r, float &x, float &y) {
  for (int i = 0; i < 20; i++) {
    int tx = r.x + random(r.w), ty = r.y + random(r.h);
    if (solid(tx, ty)) continue;
    x = tx + 0.5f;
    y = ty + 0.5f;
    return true;
  }
  return false;
}

void rotateView(float a) {
  float c = cosf(a), s = sinf(a);
  float d = dirX;
  dirX = dirX * c - dirY * s;
  dirY = d * s + dirY * c;
  float p = planeX;
  planeX = planeX * c - planeY * s;
  planeY = p * s + planeY * c;
}

// True if the key and the gate can both be reached from the start.
bool levelOk() {
  static int16_t dist[MH][MW];
  bfs((int)px, (int)py, dist);
  bool keyOk = false, gateOk = false;
  for (auto &e : ents)
    if (e.alive && e.kind == KEY && dist[(int)e.y][(int)e.x] >= 0) keyOk = true;
  for (int y = 1; y < MH - 1; y++)
    for (int x = 1; x < MW - 1; x++)
      if (map_[y][x] == GATE &&
          (dist[y][x - 1] >= 0 || dist[y][x + 1] >= 0 || dist[y - 1][x] >= 0 || dist[y + 1][x] >= 0))
        gateOk = true;
  return keyOk && gateOk;
}

void generateOnce();

void generate() {
  for (int attempt = 1; attempt <= 20; attempt++) {
    generateOnce();
    if (levelOk()) return;
    Serial.printf("crypt: level attempt %d unusable, trying again\n", attempt);
  }
}

void generateOnce() {
  memset(map_, WALL, sizeof map_);
  for (auto &e : ents) e.alive = false;
  lockTarget = nullptr;

  int want = min(9, 3 + level);
  numRooms = 0;
  Room first{0, 0, (int)random(4, 7), (int)random(4, 6)};
  first.x = MW / 2 - first.w / 2;
  first.y = MH / 2 - first.h / 2;
  rooms[numRooms++] = first;
  carveRoom(first);
  for (int tries = 0; tries < 600 && numRooms < want; tries++) tryAttach();

  static int16_t dist[MH][MW];
  bfs(first.cx(), first.cy(), dist);
  addLoops(dist);
  addPillars(0);
  bfs(first.cx(), first.cy(), dist);

  // A room's corner tile is never a pillar, so it is always reachable.
  auto roomDist = [&](int i) { return dist[rooms[i].y][rooms[i].x]; };
  int exitRoom = numRooms - 1, keyRoom = numRooms - 1;
  for (int i = 1; i < numRooms; i++)
    if (roomDist(i) > roomDist(exitRoom)) exitRoom = i;
  int keyD = -1;
  for (int i = 1; i < numRooms; i++)
    if (i != exitRoom && roomDist(i) > keyD) {
      keyD = roomDist(i);
      keyRoom = i;
    }
  if (numRooms < 3) keyRoom = exitRoom;

  // The gate: a wall tile on the exit room's edge with solid rock behind it.
  const Room &er = rooms[exitRoom];
  for (int tries = 0; tries < 60; tries++) {
    int side = random(4), gx, gy, bx, by;
    if (side == 0) { gx = er.x + random(er.w); gy = er.y - 1; bx = gx; by = gy - 1; }
    else if (side == 1) { gx = er.x + random(er.w); gy = er.y + er.h; bx = gx; by = gy + 1; }
    else if (side == 2) { gx = er.x - 1; gy = er.y + random(er.h); bx = gx - 1; by = gy; }
    else { gx = er.x + er.w; gy = er.y + random(er.h); bx = gx + 1; by = gy; }
    if (map_[gy][gx] != WALL || !solid(bx, by)) continue;
    if (bx >= 0 && by >= 0 && bx < MW && by < MH && map_[by][bx] != WALL) continue;
    map_[gy][gx] = GATE;
    break;
  }

  float x, y;
  if (randomFloorIn(rooms[keyRoom], x, y)) spawn(KEY, x, y);

  // Enemies, never in the start room. Skeletons join from level 2.
  int enemies = min(20, 2 + level * 2);
  for (int i = 0; i < enemies && numRooms > 1; i++) {
    if (!randomFloorIn(rooms[1 + random(numRooms - 1)], x, y)) continue;
    bool skel = level >= 2 && random(3) == 0;
    spawn(skel ? SKELETON : GHOST, x, y);
  }
  for (int i = 0; i < 1 + level / 2 && numRooms > 1; i++)
    if (randomFloorIn(rooms[1 + random(numRooms - 1)], x, y)) spawn(HEART, x, y);

  // Start in the middle of room 0, facing the most open direction.
  px = first.cx() + 0.5f;
  py = first.cy() + 0.5f;
  float bestA = 0;
  int bestRun = -1;
  for (int d = 0; d < 4; d++) {
    float a = d * HALF_PI;
    int run = 0;
    while (run < 20 && !solidAt(px + cosf(a) * (run + 1), py + sinf(a) * (run + 1))) run++;
    if (run > bestRun) {
      bestRun = run;
      bestA = a;
    }
  }
  dirX = cosf(bestA);
  dirY = sinf(bestA);
  planeX = -dirY * 0.66f;
  planeY = dirX * 0.66f;
  hasKey = false;
  mapUntil = 0;
  flowTX = -1;
  phase = Phase::Play;
  Serial.printf("crypt level %d: %d rooms, %d enemies\n", level, numRooms, enemies);
}

void start() {
  static bool built = false;
  if (!built) {
    build(ghostBmp, GHOST_ART);
    build(skeletonBmp, SKELETON_ART);
    build(keyBmp, KEY_ART);
    build(heartBmp, HEART_ART);
    build(boneBmp, BONE_ART);
    build(orbBmp, ORB_ART);
    built = true;
  }
  level = 1;
  hp = MAX_HP;
  kills = 0;
  score = 0;
  best = loadBest(NS);
  newBest = false;
  uint32_t now = millis();
  shotAt = hurtAt = 0;
  orbAt = now - ORB_COOLDOWN;
  orbReadyBeeped = true;
  charging = false;
  msg = nullptr;
  generate();
  say("find the key", 2000);
  play(SND_START);
}

void endRound() {
  phase = Phase::Over;
  if (score > best) {
    best = score;
    newBest = true;
    saveBest(NS, best);
  }
  Serial.printf("crypt over: score %d (best %d)\n", score, best);
  play(newBest ? SND_BEST : SND_OVER);
}

void hurt() {
  uint32_t now = millis();
  if (now - hurtAt < HURT_GRACE) return;
  hurtAt = now;
  hp--;
  play(SND_HURT);
  if (hp <= 0) endRound();
}

void kill(Ent &e) {
  e.alive = false;
  if (lockTarget == &e) lockTarget = nullptr;
  kills++;
  score += 10;
}

// Can you see from (x0, y0) to (x1, y1) without a wall in between?
bool lineOfSight(float x0, float y0, float x1, float y1) {
  float dx = x1 - x0, dy = y1 - y0;
  float len = sqrtf(dx * dx + dy * dy);
  int steps = (int)(len / 0.2f);
  for (int i = 1; i < steps; i++) {
    float t = (float)i / steps;
    if (solidAt(x0 + dx * t, y0 + dy * t)) return false;
  }
  return true;
}

void explode(float x, float y) {
  spawn(BOOM, x, y);
  play(SND_BOOM);
  for (auto &e : ents) {
    if (!isEnemy(e)) continue;
    float dx = e.x - x, dy = e.y - y;
    if (dx * dx + dy * dy > ORB_RANGE * ORB_RANGE) continue;
    e.hurtAt = millis();
    e.awake = true;
    e.hp -= 3;
    if (e.hp <= 0) kill(e);
  }
}

// --- Movement and enemies ----------------------------------------------------------
bool clearAt(float x, float y, bool &gate) {
  const float o[4][2] = {{-RADIUS, -RADIUS}, {RADIUS, -RADIUS}, {-RADIUS, RADIUS}, {RADIUS, RADIUS}};
  bool ok = true;
  for (auto &c : o) {
    int tx = (int)(x + c[0]), ty = (int)(y + c[1]);
    if (tx >= 0 && ty >= 0 && tx < MW && ty < MH && map_[ty][tx] == GATE) gate = true;
    if (solid(tx, ty)) ok = false;
  }
  return ok;
}

bool lockValid() {
  if (!lockTarget || !isEnemy(*lockTarget) || millis() > lockUntil) return false;
  float dx = lockTarget->x - px, dy = lockTarget->y - py;
  return dx * dx + dy * dy < 12 * 12 && lineOfSight(px, py, lockTarget->x, lockTarget->y);
}

void movePlayer(const Input &in) {
  bool locked = lockValid();
  if (!locked) lockTarget = nullptr;
  float side = 0;  // strafe amount, + is to the right
  if (locked) {
    // Swing the view toward the target; the stick's left/right sidesteps.
    float want = atan2f(lockTarget->y - py, lockTarget->x - px);
    float diff = want - atan2f(dirY, dirX);
    while (diff > PI) diff -= TWO_PI;
    while (diff < -PI) diff += TWO_PI;
    rotateView(constrain(diff, -LOCK_TURN, LOCK_TURN));
    side = curve(in.x) * STRAFE_SPEED;
  } else {
    float turn = curve(in.x) * TURN_SPEED;
    if (turn != 0) rotateView(turn);
  }

  float fwd = -curve(in.y) * MOVE_SPEED;
  if (fwd == 0 && side == 0) return;
  walkPhase += (fabsf(fwd) + fabsf(side)) * 3;
  // Right of the view is the camera plane's direction: (-dirY, dirX).
  float mx = dirX * fwd - dirY * side, my = dirY * fwd + dirX * side;
  bool gate = false;
  if (clearAt(px + mx, py, gate)) px += mx;
  if (clearAt(px, py + my, gate)) py += my;
  if (!gate) return;
  if (!hasKey) {
    if (!msg || millis() > msgUntil) {
      say("the gate is locked");
      play(SND_NOPE);
    }
    return;
  }
  score += 50;
  level++;
  phase = Phase::Clear;
  phaseStart = millis();
  play(SND_CLEAR);
}

// Moves an enemy one step along the walking-distance map toward the player.
void chase(Ent &e, float speed) {
  int tx = (int)e.x, ty = (int)e.y;
  float gx, gy;  // where to head for
  if (tx == flowTX && ty == flowTY) {
    gx = px;
    gy = py;
  } else {
    const int DX[] = {1, -1, 0, 0}, DY[] = {0, 0, 1, -1};
    int bestD = flow[ty][tx] >= 0 ? flow[ty][tx] : 9999, bestK = -1;
    for (int k = 0; k < 4; k++) {
      int nx = tx + DX[k], ny = ty + DY[k];
      if (solid(nx, ny) || flow[ny][nx] < 0) continue;
      if (flow[ny][nx] < bestD) {
        bestD = flow[ny][nx];
        bestK = k;
      }
    }
    if (bestK < 0) return;
    // First line up with the middle of the tile, then step across. Moving
    // only along rows and columns keeps enemies out of the walls.
    gx = tx + 0.5f + DX[bestK];
    gy = ty + 0.5f + DY[bestK];
    if (DX[bestK] != 0 && fabsf(e.y - (ty + 0.5f)) > speed) gx = e.x, gy = ty + 0.5f;
    if (DY[bestK] != 0 && fabsf(e.x - (tx + 0.5f)) > speed) gx = tx + 0.5f, gy = e.y;
  }
  float dx = gx - e.x, dy = gy - e.y;
  float d = sqrtf(dx * dx + dy * dy);
  if (d < 0.001f) return;
  float step = min(speed, d);
  float nx = e.x + dx / d * step, ny = e.y + dy / d * step;
  if (!solidAt(nx, e.y)) e.x = nx;
  if (!solidAt(e.x, ny)) e.y = ny;
}

void updateEnts() {
  uint32_t now = millis();
  int ptx = (int)px, pty = (int)py;
  if (ptx != flowTX || pty != flowTY) {
    bfs(ptx, pty, flow);
    flowTX = ptx;
    flowTY = pty;
  }

  for (auto &e : ents) {
    if (!e.alive) continue;
    float dx = px - e.x, dy = py - e.y;
    float d = sqrtf(dx * dx + dy * dy);

    switch (e.kind) {
      case KEY:
        if (d < 0.5f) {
          e.alive = false;
          hasKey = true;
          say("KEY! find the gate", 2000);
          play(SND_COIN);
        }
        break;
      case HEART:
        if (d < 0.5f && hp < MAX_HP) {
          e.alive = false;
          hp++;
          play(SND_COIN);
        }
        break;
      case GHOST:
        if (!e.awake && d < 8 && lineOfSight(e.x, e.y, px, py)) e.awake = true;
        if (e.awake && d > 0.4f) chase(e, 0.024f + level * 0.002f);
        if (d < 0.55f) hurt();
        break;
      case SKELETON: {
        bool sees = d < 10 && lineOfSight(e.x, e.y, px, py);
        if (sees) e.awake = true;
        if (e.awake && (!sees || d > 3.0f)) chase(e, 0.02f);  // keep some distance
        if (sees && now >= e.timer) {
          e.timer = now + 2000 + random(1200);
          if (Ent *b = spawn(BONE, e.x, e.y)) {
            b->vx = dx / d * 0.11f;
            b->vy = dy / d * 0.11f;
          }
        }
        if (d < 0.55f) hurt();
        break;
      }
      case BONE:
        e.x += e.vx;
        e.y += e.vy;
        if (solidAt(e.x, e.y)) e.alive = false;
        else if (d < 0.35f) {
          e.alive = false;
          hurt();
        }
        break;
      case ORB: {
        float nx = e.x + e.vx, ny = e.y + e.vy;
        bool boom = solidAt(nx, ny) || now - e.born > 3000;
        for (auto &o : ents) {
          if (!isEnemy(o)) continue;
          float ox = o.x - nx, oy = o.y - ny;
          if (ox * ox + oy * oy < 0.5f * 0.5f) boom = true;
        }
        if (boom) {
          e.alive = false;
          explode(e.x, e.y);
        } else {
          e.x = nx;
          e.y = ny;
        }
        break;
      }
      case BOOM:
        if (now - e.born > 350) e.alive = false;
        break;
    }
    if (phase != Phase::Play) return;
  }
}

// --- Drawing ----------------------------------------------------------------------
void drawWalls() {
  int prevTile = -1, prevSide = -1;
  for (int x = 0; x < W; x++) {
    float camX = 2.0f * x / W - 1;
    float rx = dirX + planeX * camX, ry = dirY + planeY * camX;
    int mx = (int)px, my = (int)py;
    float ddx = rx == 0 ? 1e30f : fabsf(1 / rx);
    float ddy = ry == 0 ? 1e30f : fabsf(1 / ry);
    int sx = rx < 0 ? -1 : 1, sy = ry < 0 ? -1 : 1;
    float distX = (rx < 0 ? px - mx : mx + 1 - px) * ddx;
    float distY = (ry < 0 ? py - my : my + 1 - py) * ddy;
    int side = 0;
    for (int guard = 0; guard < 64; guard++) {
      if (distX < distY) { distX += ddx; mx += sx; side = 0; }
      else { distY += ddy; my += sy; side = 1; }
      if (solid(mx, my)) break;
    }
    float dist = side == 0 ? distX - ddx : distY - ddy;
    if (dist < 0.05f) dist = 0.05f;
    zbuf[x] = dist;
    uint8_t tile = (mx < 0 || my < 0 || mx >= MW || my >= MH) ? (uint8_t)WALL : map_[my][mx];

    // Where along the wall the ray hit, 0..1: the texture's x.
    float wallX = side == 0 ? py + dist * ry : px + dist * rx;
    wallX -= floorf(wallX);

    int h = (int)(H / dist);
    int top = H / 2 - h / 2;
    int y0 = max(0, top), y1 = min(H - 1, H / 2 + h / 2);

    int tileId = my * MW + mx;
    bool edge = tileId != prevTile || side != prevSide;
    prevTile = tileId;
    prevSide = side;

    if (edge) {
      oled.drawVLine(x, y0, y1 - y0 + 1);
    } else if (tile == GATE) {
      // Iron bars. Once you have the key they shimmer.
      bool show = !hasKey || (frame / 2) % 3 != 0;
      if (show && (int)(wallX * 8) % 2 == 0) oled.drawVLine(x, y0, y1 - y0 + 1);
      else if (show)
        for (int y = y0; y <= y1; y++) {
          float v = (float)(y - top) / h;
          if (fabsf(v - 0.25f) < 0.03f || fabsf(v - 0.75f) < 0.03f) oled.drawPixel(x, y);
        }
    } else if (dist < FAR) {
      // Bricks: 4 rows per wall, every other row shifted by half a brick.
      float joint = 2.8f / h;          // about one pixel, in brick widths
      float vStep = 4.0f / h, v = (y0 + 1 - top) * vStep;
      for (int y = y0 + 1; y < y1; y++, v += vStep) {
        int row = (int)v;
        float u = wallX * 2 + (row & 1) * 0.5f;
        bool mortar = v - row < vStep || u - floorf(u) < joint;
        bool dots = side == 1 && (x & 1) == 0 && (y & 3) == 0;  // shade one side
        if (mortar || dots) oled.drawPixel(x, y);
      }
    }
    oled.drawPixel(x, y0);
    oled.drawPixel(x, y1);
  }
}

// Works out where each thing would appear on screen (camera space).
void project() {
  float invDet = 1.0f / (planeX * dirY - dirX * planeY);
  for (auto &e : ents) {
    if (!e.alive) continue;
    float sx = e.x - px, sy = e.y - py;
    float tx = invDet * (dirY * sx - dirX * sy);
    e.depth = invDet * (-planeY * sx + planeX * sy);  // distance ahead
    e.screenX = W / 2 * (1 + tx / max(e.depth, 0.01f));
    float scale = e.kind == BONE || e.kind == ORB ? 0.35f
                : e.kind == KEY || e.kind == HEART ? 0.45f : 0.9f;
    e.size = scale * H / max(e.depth, 0.01f);
  }
}

void drawBoom(const Ent &e) {
  if (e.depth < 0.25f) return;
  float t = (millis() - e.born) / 350.0f;  // 0..1
  int cx = (int)e.screenX, cy = H / 2;
  if (cx < 0 || cx >= W || e.depth >= zbuf[cx] + 0.5f) return;
  int r = (int)((0.2f + t * ORB_RANGE) * H / 2 / e.depth);
  oled.setDrawColor(2);
  oled.drawCircle(cx, cy, min(r, 90));
  if (r > 4) oled.drawCircle(cx, cy, min(r * 2 / 3, 90));
  oled.setDrawColor(1);
}

void drawSprite(const Ent &e) {
  if (e.kind == BOOM) {
    drawBoom(e);
    return;
  }
  if (e.depth < 0.25f || e.size < 2) return;
  const Bitmap *b;
  float lift;  // vertical offset in world units: + is down
  switch (e.kind) {
    case GHOST:
      b = &ghostBmp;
      lift = sinf((frame + (int)(e.x * 10)) * 0.15f) * 0.08f - 0.05f;  // bobbing
      if ((frame + (int)(e.y * 7)) % 7 == 0) return;                     // flicker
      break;
    case SKELETON: b = &skeletonBmp; lift = 0.05f; break;
    case KEY: b = &keyBmp; lift = 0.28f; break;
    case HEART: b = &heartBmp; lift = 0.28f; break;
    case ORB: b = &orbBmp; lift = 0.05f; break;
    default: b = &boneBmp; lift = 0; break;
  }
  if (millis() - e.hurtAt < 120 && (frame & 1)) return;  // blink when hit
  int size = (int)e.size;
  int left = (int)(e.screenX - size / 2);
  int top = (int)(H / 2 + lift * H / e.depth - size / 2);
  for (int x = max(0, left); x < min(W, left + size); x++) {
    if (e.depth >= zbuf[x]) continue;  // a wall is in front here
    int u = (x - left) * 16 / size;
    uint16_t bit = 1 << (15 - u);
    for (int y = max(0, top); y < min(H, top + size); y++) {
      int v = (y - top) * 16 / size;
      if (b->shape[v] & bit) {
        oled.setDrawColor(1);
        oled.drawPixel(x, y);
      } else if (b->mask[v] & bit) {
        oled.setDrawColor(0);
        oled.drawPixel(x, y);
      }
    }
  }
  oled.setDrawColor(1);
}

void drawSprites() {
  // Far to near, so nearer things cover farther ones.
  Ent *order[MAX_ENTS];
  int n = 0;
  for (auto &e : ents)
    if (e.alive && e.depth > 0.25f) order[n++] = &e;
  for (int i = 1; i < n; i++)
    for (int j = i; j > 0 && order[j]->depth > order[j - 1]->depth; j--) {
      Ent *t = order[j];
      order[j] = order[j - 1];
      order[j - 1] = t;
    }
  for (int i = 0; i < n; i++) drawSprite(*order[i]);
}

// Corner brackets around the locked target.
void drawLock() {
  if (!lockTarget) return;
  const Ent &e = *lockTarget;
  if (e.depth < 0.25f) return;
  int s = max(6, (int)(e.size * 0.6f));
  int x0 = (int)e.screenX - s, x1 = (int)e.screenX + s;
  int y0 = H / 2 - s, y1 = H / 2 + s;
  int c = max(2, s / 3);
  oled.setDrawColor(2);
  oled.drawHLine(x0, y0, c); oled.drawVLine(x0, y0 + 1, c - 1);
  oled.drawHLine(x1 - c + 1, y0, c); oled.drawVLine(x1, y0 + 1, c - 1);
  oled.drawHLine(x0, y1, c); oled.drawVLine(x0, y1 - c + 1, c - 1);
  oled.drawHLine(x1 - c + 1, y1, c); oled.drawVLine(x1, y1 - c + 1, c - 1);
  oled.setDrawColor(1);
}

// The staff in your hand, swaying as you walk. It flashes when it fires and
// its orb swells while you charge a throw. The outline follows its shape.
void drawStaff() {
  int bob = (int)(sinf(walkPhase) * 2);
  int ox = 86, oy = 46 + abs(bob);
  uint32_t now = millis();
  int orbR = 3;
  if (charging) orbR = 3 + min<uint32_t>(3, (now - aDownSince - CHARGE_MS) / 120);
  oled.setDrawColor(0);
  for (int k = -1; k <= 2; k++) oled.drawLine(ox + 8 + k, H - 1, ox + k, oy + 3);
  oled.drawDisc(ox, oy, orbR + 1);
  oled.setDrawColor(1);
  oled.drawLine(ox + 8, H - 1, ox, oy + 3);
  oled.drawLine(ox + 9, H - 1, ox + 1, oy + 3);
  if (charging && (frame & 1)) oled.drawDisc(ox, oy, orbR);
  else oled.drawCircle(ox, oy, orbR);
  if (now - shotAt < 90) {
    oled.drawDisc(ox, oy, orbR);
    for (int i = 0; i < 8; i++) {
      float a = i * (PI / 4);
      oled.drawLine(ox + cosf(a) * 5, oy + sinf(a) * 5, ox + cosf(a) * 8, oy + sinf(a) * 8);
    }
  }
}

// Picks what a shot would hit: the locked target if you still see it, or
// else the nearest visible enemy near the crosshair (auto-aim).
Ent *aimTarget() {
  if (lockValid() && lockTarget->depth > 0.25f) return lockTarget;
  Ent *target = nullptr;
  for (auto &e : ents) {
    if (!isEnemy(e) || e.depth < 0.25f) continue;
    float slack = max(W * 0.1f, e.size * 0.5f);  // ~7 degrees, or the body
    if (fabsf(e.screenX - W / 2) > slack) continue;
    if (!lineOfSight(px, py, e.x, e.y)) continue;
    if (!target || e.depth < target->depth) target = &e;
  }
  return target;
}

void fire() {
  shotAt = millis();
  play(SND_SHOT);
  Ent *t = aimTarget();
  if (!t) return;
  lockTarget = t;
  lockUntil = millis() + LOCK_MS;
  t->hurtAt = millis();
  t->awake = true;
  if (--t->hp <= 0) {
    kill(*t);
    play(SND_KILL);
  } else {
    play(SND_HIT);
    float kx = t->x + dirX * 0.25f, ky = t->y + dirY * 0.25f;  // knockback
    if (!solidAt(kx, ky)) {
      t->x = kx;
      t->y = ky;
    }
  }
}

void throwOrb() {
  orbAt = millis();
  orbReadyBeeped = false;
  float vx = dirX, vy = dirY;
  if (lockValid()) {  // aim at the locked target
    float dx = lockTarget->x - px, dy = lockTarget->y - py;
    float d = sqrtf(dx * dx + dy * dy);
    vx = dx / d;
    vy = dy / d;
  }
  if (Ent *o = spawn(ORB, px + vx * 0.3f, py + vy * 0.3f)) {
    o->vx = vx * 0.16f;
    o->vy = vy * 0.16f;
  }
  play(SND_THROW);
}

void drawHud() {
  uint32_t now = millis();
  oled.setDrawColor(0);
  oled.drawBox(0, 0, MAX_HP * 7 + 12, 8);
  oled.setDrawColor(1);
  for (int i = 0; i < MAX_HP; i++) {
    int x = 1 + i * 7;
    if (i < hp) {  // a tiny filled heart
      oled.drawBox(x, 2, 5, 2);
      oled.drawBox(x + 1, 4, 3, 1);
      oled.drawPixel(x + 2, 5);
      oled.drawPixel(x + 1, 1);
      oled.drawPixel(x + 3, 1);
    } else {
      oled.drawPixel(x + 2, 3);
    }
  }
  if (hasKey) {  // a tiny key
    int x = MAX_HP * 7 + 3;
    oled.drawFrame(x, 2, 3, 3);
    oled.drawHLine(x + 3, 3, 4);
    oled.drawPixel(x + 6, 4);
  }

  // Orb meter, bottom left: fills over the cooldown, blinks when ready.
  uint32_t charged = min<uint32_t>(now - orbAt, ORB_COOLDOWN);
  oled.setDrawColor(0);
  oled.drawBox(0, H - 7, 24, 7);
  oled.setDrawColor(1);
  oled.drawFrame(1, H - 6, 22, 5);
  if (charged < ORB_COOLDOWN || (frame / 6) % 2) oled.drawBox(1, H - 6, 22 * charged / ORB_COOLDOWN, 5);

  char right[16];
  snprintf(right, sizeof right, "L%d %d", level, score);
  oled.setFont(u8g2_font_5x8_tf);
  int w = oled.getStrWidth(right);
  oled.setDrawColor(0);
  oled.drawBox(W - w - 2, 0, w + 2, 9);
  oled.setDrawColor(1);
  oled.drawStr(W - w - 1, 7, right);

  // Crosshair, drawn inverted so it shows on both black and white.
  oled.setDrawColor(2);
  oled.drawHLine(W / 2 - 2, H / 2, 2);
  oled.drawHLine(W / 2 + 1, H / 2, 2);
  oled.drawVLine(W / 2, H / 2 - 2, 2);
  oled.drawVLine(W / 2, H / 2 + 1, 2);
  oled.setDrawColor(1);

  if (msg && now < msgUntil) {
    int mw = oled.getStrWidth(msg);
    oled.setDrawColor(0);
    oled.drawBox((W - mw) / 2 - 2, H - 19, mw + 4, 10);
    oled.setDrawColor(1);
    drawCentered(H - 11, msg);
  }
}

void drawMap() {
  constexpr int S = 2;
  int ox = (W - MW * S) / 2, oy = H - MH * S;
  for (int y = 0; y < MH; y++)
    for (int x = 0; x < MW; x++) {
      uint8_t t = map_[y][x];
      if (t == WALL) oled.drawBox(ox + x * S, oy + y * S, S, S);
      else if (t == GATE && (frame / 3) % 2) oled.drawBox(ox + x * S, oy + y * S, S, S);
    }
  oled.setDrawColor(0);
  for (auto &e : ents)  // the key shows as a blinking hole
    if (e.alive && e.kind == KEY && (frame / 2) % 2)
      oled.drawBox(ox + (int)e.x * S, oy + (int)e.y * S, S, S);
  int cx = ox + (int)(px * S), cy = oy + (int)(py * S);
  oled.drawBox(cx - 1, cy - 1, 3, 3);
  oled.setDrawColor(1);
  if (frame % 2) oled.drawPixel(cx, cy);
  oled.drawLine(cx, cy, cx + (int)(dirX * 4), cy + (int)(dirY * 4));
}

void update(const Input &in) {
  uint32_t now = millis();
  if (phase == Phase::Over) {
    if (in.press) start();
    drawGameOver(score, best, newBest);
    return;
  }
  if (phase == Phase::Clear) {
    if (now - phaseStart > 1800) {
      hp = min(MAX_HP, hp + 1);  // a little healing between levels
      generate();
      say("find the key", 2000);
    }
    oled.clearBuffer();
    oled.setFont(u8g2_font_9x15B_tf);
    drawCentered(24, "ESCAPED!");
    oled.setFont(u8g2_font_5x8_tf);
    char line[32];
    snprintf(line, sizeof line, "score %d  kills %d", score, kills);
    drawCentered(40, line);
    snprintf(line, sizeof line, "descending to level %d", level);
    drawCentered(52, line);
    oled.sendBuffer();
    return;
  }

  // Button A: tap fires at once; keep holding to charge an orb (if it's
  // recharged), release to throw it. The stick press shows the map.
  bool shoot = in.a && now - shotAt >= SHOT_COOLDOWN;
  static bool aWas = false;
  bool orbReady = now - orbAt >= ORB_COOLDOWN;
  if (in.a) aDownSince = now;
  if (in.aHeld && orbReady && now - aDownSince >= CHARGE_MS) charging = true;
  bool throwNow = charging && !in.aHeld && aWas;
  if (!in.aHeld) charging = false;
  aWas = in.aHeld;
  if (orbReady && !orbReadyBeeped) {
    orbReadyBeeped = true;
    play(SND_READY);
  }
  if (in.press && !in.a) mapUntil = now + MAP_MS;

  movePlayer(in);
  if (phase != Phase::Play) return;
  if (throwNow) throwOrb();
  updateEnts();
  if (phase != Phase::Play) return;

  uint32_t t0 = micros();
  oled.clearBuffer();
  if (now < mapUntil) {
    drawMap();
  } else {
    drawWalls();
    project();
    if (shoot) fire();
    drawSprites();
    drawLock();
    drawStaff();
  }
  drawHud();
  static uint32_t lastLog = 0;  // drawing time on Serial, every 2 s
  if (now - lastLog >= 2000) {
    lastLog = now;
    Serial.printf("crypt draw %lu us\n", (unsigned long)(micros() - t0));
  }
  if (now - hurtAt < 150) {  // flash the whole screen when you're hit
    oled.setDrawColor(2);
    oled.drawBox(0, 0, W, H);
    oled.setDrawColor(1);
  }
  oled.sendBuffer();
}

}  // namespace

const Game CRYPT = {"Crypt", NS, start, update};
