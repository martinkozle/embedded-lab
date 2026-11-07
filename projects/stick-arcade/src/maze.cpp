// Maze 3D: walk through a maze in first person and find the exit door
// (the striped wall) before the clock runs out. Every exit gives you more
// time and a bigger maze. Score = mazes cleared.
//
// Controls: stick forward/back walks, left/right turns. Button A (or a short
// stick press) shows the map for 2 seconds but costs 3 seconds of time.
//
// The 3D view is a raycaster: for each of the 128 screen columns we cast a
// ray from the player across the tile grid until it hits a wall. The nearer
// the wall, the taller the column we draw. Walls facing two directions get
// two different dot patterns so corners stand out, and far walls are drawn
// as outlines only, which reads as depth on a black-and-white screen.
#include "arcade.h"

namespace {

constexpr const char *NS = "maze";
constexpr int MAX_CELLS = 11;                 // biggest maze: 11 x 11 rooms
constexpr int MAX_TILES = MAX_CELLS * 2 + 1;  // rooms plus the walls between
constexpr float MOVE_SPEED = 0.07f;           // tiles per frame at full tilt
constexpr float TURN_SPEED = 0.08f;           // radians per frame at full tilt
constexpr float RADIUS = 0.2f;                // how close you can get to a wall
constexpr float FAR = 6.0f;                   // beyond this, outlines only
constexpr uint32_t MAP_MS = 2000;
constexpr int32_t MAP_COST_MS = 3000;

enum Tile : uint8_t { FLOOR = 0, WALL = 1, DOOR = 2 };
uint8_t map_[MAX_TILES][MAX_TILES];
int tiles;  // the current maze is tiles x tiles

float px, py;          // player position, in tiles
float dirX, dirY;      // the way we face
float planeX, planeY;  // the camera plane, sets the field of view

enum class Phase { Walk, Clear, Over };
Phase phase;
int level, best;
int32_t timeLeftMs;
uint32_t lastMs, phaseStart, mapUntil;
int lastBeepSecond;
bool newBest;

// --- Maze generation ---------------------------------------------------------
// Depth-first "recursive backtracker": walk to random unvisited neighbours,
// knocking down walls, and back up when stuck. The result has exactly one
// path between any two rooms. The room reached at the greatest depth gets
// the exit door, so the exit is always far from the start.
void generate(int cells) {
  tiles = cells * 2 + 1;
  for (int y = 0; y < tiles; y++)
    for (int x = 0; x < tiles; x++) map_[y][x] = WALL;

  static uint8_t stackX[MAX_CELLS * MAX_CELLS], stackY[MAX_CELLS * MAX_CELLS];
  static bool seen[MAX_CELLS][MAX_CELLS];
  memset(seen, 0, sizeof seen);
  int sp = 0, deepest = 0, endX = 0, endY = 0;
  stackX[sp] = 0;
  stackY[sp] = 0;
  sp++;
  seen[0][0] = true;
  map_[1][1] = FLOOR;
  const int DX[] = {1, -1, 0, 0}, DY[] = {0, 0, 1, -1};

  while (sp > 0) {
    int cx = stackX[sp - 1], cy = stackY[sp - 1];
    int options[4], n = 0;
    for (int d = 0; d < 4; d++) {
      int nx = cx + DX[d], ny = cy + DY[d];
      if (nx >= 0 && nx < cells && ny >= 0 && ny < cells && !seen[ny][nx])
        options[n++] = d;
    }
    if (n == 0) {
      sp--;
      continue;
    }
    int d = options[random(n)];
    int nx = cx + DX[d], ny = cy + DY[d];
    seen[ny][nx] = true;
    map_[cy * 2 + 1 + DY[d]][cx * 2 + 1 + DX[d]] = FLOOR;  // the wall between
    map_[ny * 2 + 1][nx * 2 + 1] = FLOOR;
    stackX[sp] = nx;
    stackY[sp] = ny;
    sp++;
    if (sp > deepest) {
      deepest = sp;
      endX = nx;
      endY = ny;
    }
  }

  // Turn one wall of the deepest room into the door.
  int tx = endX * 2 + 1, ty = endY * 2 + 1;
  for (int d = 0; d < 4; d++) {
    if (map_[ty + DY[d]][tx + DX[d]] == WALL) {
      map_[ty + DY[d]][tx + DX[d]] = DOOR;
      break;
    }
  }
}

int mazeCells() { return min(MAX_CELLS, 3 + level); }

void startMaze() {
  generate(mazeCells());
  px = py = 1.5f;  // centre of the first room
  // Face along the open corridor out of the first room.
  float a = map_[1][2] == FLOOR ? 0 : HALF_PI;
  dirX = cosf(a);
  dirY = sinf(a);
  planeX = -dirY * 0.66f;  // 0.66 gives a ~66 degree field of view
  planeY = dirX * 0.66f;
  phase = Phase::Walk;
  mapUntil = 0;
}

void start() {
  level = 1;
  best = loadBest(NS);
  timeLeftMs = 40000;
  lastMs = millis();
  lastBeepSecond = -1;
  newBest = false;
  startMaze();
  play(SND_START);
}

int score() { return level - 1; }  // mazes cleared

void endRound() {
  phase = Phase::Over;
  if (score() > best) {
    best = score();
    newBest = true;
    saveBest(NS, best);
  }
  Serial.printf("maze over: cleared %d (best %d)\n", score(), best);
  play(newBest ? SND_BEST : SND_OVER);
}

// --- Movement ------------------------------------------------------------------
uint8_t tileAt(float x, float y) {
  int tx = (int)x, ty = (int)y;
  if (tx < 0 || ty < 0 || tx >= tiles || ty >= tiles) return WALL;
  return map_[ty][tx];
}

// Is a circle of RADIUS at (x, y) clear of walls? Reports a door touch.
bool free_(float x, float y, bool &door) {
  const float ox[] = {-RADIUS, RADIUS, -RADIUS, RADIUS};
  const float oy[] = {-RADIUS, -RADIUS, RADIUS, RADIUS};
  bool ok = true;
  for (int i = 0; i < 4; i++) {
    uint8_t t = tileAt(x + ox[i], y + oy[i]);
    if (t == DOOR) door = true;
    if (t != FLOOR) ok = false;
  }
  return ok;
}

void move(const Input &in) {
  // Turn. Rotating the direction and the camera plane together.
  float turn = curve(in.x) * TURN_SPEED;
  if (turn != 0) {
    float c = cosf(turn), s = sinf(turn);
    float d = dirX;
    dirX = dirX * c - dirY * s;
    dirY = d * s + dirY * c;
    float p = planeX;
    planeX = planeX * c - planeY * s;
    planeY = p * s + planeY * c;
  }

  // Walk. Stick up is negative y. Try each axis on its own so you slide
  // along walls instead of sticking to them.
  float step = -curve(in.y) * MOVE_SPEED;
  if (step == 0) return;
  bool door = false;
  float nx = px + dirX * step, ny = py + dirY * step;
  if (free_(nx, py, door)) px = nx;
  if (free_(px, ny, door)) py = ny;
  if (door) {
    int bonus = mazeCells() * 4 + 5;  // seconds, more for bigger mazes
    timeLeftMs += bonus * 1000;
    Serial.printf("maze %d cleared, +%d s\n", level, bonus);
    level++;
    phase = Phase::Clear;
    phaseStart = millis();
    play(SND_CLEAR);
  }
}

// --- Drawing -------------------------------------------------------------------
void drawView() {
  int prevTile = -1, prevSide = -1;
  for (int x = 0; x < W; x++) {
    float camX = 2.0f * x / W - 1;  // -1 on the left edge, +1 on the right
    float rx = dirX + planeX * camX, ry = dirY + planeY * camX;
    int mx = (int)px, my = (int)py;
    float ddx = rx == 0 ? 1e30f : fabsf(1 / rx);
    float ddy = ry == 0 ? 1e30f : fabsf(1 / ry);
    int sx = rx < 0 ? -1 : 1, sy = ry < 0 ? -1 : 1;
    float distX = (rx < 0 ? px - mx : mx + 1 - px) * ddx;
    float distY = (ry < 0 ? py - my : my + 1 - py) * ddy;

    // Step from tile edge to tile edge until we hit something (DDA).
    int side = 0;
    uint8_t hit = FLOOR;
    for (int guard = 0; guard < 64 && hit == FLOOR; guard++) {
      if (distX < distY) {
        distX += ddx;
        mx += sx;
        side = 0;
      } else {
        distY += ddy;
        my += sy;
        side = 1;
      }
      hit = (mx < 0 || my < 0 || mx >= tiles || my >= tiles) ? (uint8_t)WALL : map_[my][mx];
    }
    // Distance measured straight ahead, not along the ray (no fisheye).
    float dist = side == 0 ? distX - ddx : distY - ddy;
    if (dist < 0.05f) dist = 0.05f;

    int h = (int)(H / dist);
    int y0 = max(0, H / 2 - h / 2), y1 = min(H - 1, H / 2 + h / 2);

    // A vertical edge wherever a new block or a new face starts.
    int tileId = my * MAX_TILES + mx;
    bool edge = tileId != prevTile || side != prevSide;
    prevTile = tileId;
    prevSide = side;

    if (edge || (hit == DOOR && x % 3 != 0)) {
      oled.drawVLine(x, y0, y1 - y0 + 1);
    } else if (dist < FAR) {
      for (int y = y0 + 1; y < y1; y++) {
        bool on = side == 0 ? ((x + y) & 1) == 0           // dense dots
                            : (x & 1) == 0 && (y & 3) == 0;  // sparse dots
        if (on) oled.drawPixel(x, y);
      }
    }
    oled.drawPixel(x, y0);  // top and bottom outline of every wall
    oled.drawPixel(x, y1);
  }
}

void drawMap() {
  int s = tiles <= 15 ? 3 : 2;  // pixels per tile
  int ox = (W - tiles * s) / 2, oy = (H - tiles * s) / 2 + 4;
  for (int y = 0; y < tiles; y++)
    for (int x = 0; x < tiles; x++) {
      if (map_[y][x] == WALL) oled.drawBox(ox + x * s, oy + y * s, s, s);
      else if (map_[y][x] == DOOR && (frame / 3) % 2) oled.drawBox(ox + x * s, oy + y * s, s, s);
    }
  // You: a blinking dot with a short line showing where you face.
  int cx = ox + (int)(px * s), cy = oy + (int)(py * s);
  oled.setDrawColor(frame % 2 ? 1 : 0);
  oled.drawBox(cx - 1, cy - 1, 3, 3);
  oled.setDrawColor(1);
  oled.drawLine(cx, cy, cx + (int)(dirX * 5), cy + (int)(dirY * 5));
}

void drawHud() {
  char left[12], right[12];
  snprintf(left, sizeof left, "%lds", (long)max<int32_t>(0, (timeLeftMs + 999) / 1000));
  snprintf(right, sizeof right, "MAZE %d", level);
  oled.setFont(u8g2_font_5x8_tf);
  int wl = oled.getStrWidth(left), wr = oled.getStrWidth(right);
  oled.setDrawColor(0);
  oled.drawBox(0, 0, wl + 2, 9);
  oled.drawBox(W - wr - 2, 0, wr + 2, 9);
  oled.setDrawColor(1);
  oled.drawStr(1, 7, left);
  oled.drawStr(W - wr - 1, 7, right);
}

void update(const Input &in) {
  if (phase == Phase::Over) {
    if (in.press) start();
    drawGameOver(score(), best, newBest);
    return;
  }

  uint32_t now = millis();
  uint32_t dt = now - lastMs;
  lastMs = now;

  if (phase == Phase::Clear) {
    // A short pause on the "cleared" banner; the clock is stopped.
    if (now - phaseStart > 1600) startMaze();
    oled.clearBuffer();
    oled.setFont(u8g2_font_9x15B_tf);
    drawCentered(26, "ESCAPED!");
    oled.setFont(u8g2_font_5x8_tf);
    char line[32];
    snprintf(line, sizeof line, "next: %d x %d maze", mazeCells(), mazeCells());
    drawCentered(42, line);
    snprintf(line, sizeof line, "time left %lds", (long)(timeLeftMs / 1000));
    drawCentered(54, line);
    oled.sendBuffer();
    return;
  }

  timeLeftMs -= dt;
  if (timeLeftMs <= 0) {
    timeLeftMs = 0;
    endRound();
    return;
  }
  int sec = timeLeftMs / 1000;
  if (sec < 5 && sec != lastBeepSecond) {  // tick through the last 5 seconds
    lastBeepSecond = sec;
    play(SND_MOVE);
  }

  if (in.press && now >= mapUntil) {
    mapUntil = now + MAP_MS;
    timeLeftMs -= MAP_COST_MS;
    play(SND_MOVE);
  }

  move(in);
  if (phase != Phase::Walk) return;

  oled.clearBuffer();
  if (now < mapUntil) drawMap();
  else drawView();
  drawHud();
  oled.sendBuffer();
}

}  // namespace

const Game MAZE = {"Maze 3D", NS, start, update};
