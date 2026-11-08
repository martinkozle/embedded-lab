# Martin OS

A home station on the ESP32: a home screen of apps on a ring (Wear, and the
Games and Demos folders), with the
joystick, button A, the 0.96" OLED and the buzzer. The wiring is in
`hardware.md`. The first useful app is a wear counter for clothes; the rest
are games (this project used to be `stick-arcade`).

## Controls everywhere

- **Point the stick** at a circle on the ring to pick it. A compass hand in
  the middle follows the stick; the white fill glides to the picked circle.
- Rings spiral out of the middle when they appear and back in when they go.
  Boot shows a spirograph, going home a golden-angle sunflower spiral.
- **A** (or a **stick press**) opens the picked app or folder.
- **Hold the stick button for about 0.7 s** to go back: from an app to its
  folder, from a folder to home.
- The screen turns off after a minute without input. The touch that wakes it
  does nothing else.

## Wear counter (`wear.cpp`)

Counts how many times each item was worn since its last wash. No limits or
reminders: it only keeps the count.

- **Point** at an item, **tap A**: +1 wear.
- **Hold A**:
  - 0.3–1 s: the middle fills. Let go to cancel.
  - 1–1.7 s: the middle is full. Let go to mark that item washed (back to 0).
  - 1.7–3.2 s: the ring spins up. Let go to cancel.
  - from 3.2 s: the ring shakes, ready. Let go to wash the whole group.
- **Press the stick**: next group (the ring spirals in and the next one out).
- Each +1 makes the circle pop. Washing one item bursts it into soap bubbles.
  Washing a group blows the spun-up ring up:
  flash, shockwave, sparks, bubbles, a bouncing "CLEAN!" and a fanfare.

Groups and items are in `src/items.h` (up to 8 items per group). Counts are
saved in NVS under a key hashed from the group and item name, so reordering
or adding items keeps the counts; renaming an item starts it from 0.

## Games

| Game | File | Controls |
|---|---|---|
| Dodge | `dodge.cpp` | Stick steers the ship. A dashes (2 s cooldown). |
| Snake | `snake.cpp` | Stick turns. Hold A to go double speed for double points. |
| Maze 3D | `maze.cpp` | Raycaster maze against the clock. Stick walks and turns. A shows the map (costs 3 s). |
| Crypt | `crypt.cpp` | First-person shooter in generated rooms. Tap A fires with auto-aim and locks on; while locked, left/right strafes. Hold A to throw an exploding orb. Stick press shows the map. |
| Bricks | `bricks.cpp` | Brick-breaker with generated, mirrored levels. Stick moves the paddle, A launches. |
| Tanks | `tanks.cpp` | Top-down tank battle: defend the core in a generated, mirrored fort from 20 tanks per level. Bricks can be shot away; steel, water and bushes too. Carrier tanks (flashing) drop power-ups. Stick drives, A fires (hold to keep firing). |
| Stacker | `stacker.cpp` | Falling-blocks puzzle. Left/right moves, down drops faster, flick up drops hard, A rotates, stick press holds. |

On a game-over screen, **A** or a **stick press** plays again. Best scores are
saved in NVS, one namespace per game. `pio run -t erase` wipes them and the
wear counts.

## Demos

Math animations, in the Demos folder. Hold the stick to leave; the screen
doesn't go to sleep while one runs.

| Demo | File | What it is | Controls |
|---|---|---|---|
| Tunnel | `demo_tunnel.cpp` | Endless zoom through nested polygons, each one inscribed in the last | Stick bends the tunnel. A: next shape. Press: filled / outlines. |
| Mandelbrot | `demo_fractal.cpp` | Zooms into a spot until floats run out of digits (x50000), then out and on to the next | Stick up zooms faster. A: next spot. |
| Julia | `demo_fractal.cpp` | Julia set for c travelling round a circle | Stick left/right: speed and direction. |
| Plasma | `demo_plasma.cpp` | Four sine waves added up per pixel | Stick: speed and wave size. A: banding. |
| Solids | `demo_solids.cpp` | Perspective wireframes: cube, octahedron, icosahedron, 4D tesseract | Stick spins it. A: next shape. |
| Chaos | `demo_chaos.cpp` | Chaos game: fern, Sierpinski, dragon, Levy C | A: next (or every 9 s). |
| Life | `demo_life.cpp` | Conway's Game of Life on a wrapping 64x32 grid | A: reseed (also when it settles). |

The fractals render on both CPU cores (a FreeRTOS task on core 0 takes every
other row) and print their frame time to serial every 2 s. Shades of grey
come from 4x4 ordered dithering (`shade.h`).

## Code layout

- `os.h` / `os.cpp`: what every app shares.
  - screen (`oled`), and the frame pacing at 24 fps, the most the OLED takes over 400 kHz I2C;
  - `readInput()`: stick with dead zone, flicks, short/long press, button A;
  - the non-blocking buzzer tunes (`play()`);
  - saved best scores (`loadBest` / `saveBest`);
  - `drawCentered` and the shared game-over screen.
- `radial.h` / `radial.cpp`: the compass ring picker and its spiral animation, and
  exact centring for numbers (6x10 digits are 5x7 px) and 9x9 icons.
- `main.cpp`: the menus (home and folders), app icons, boot and home-coming animations,
  screen sleep and the main loop.
- One `.cpp` per app. Each exports a `Game` struct with a name, an NVS
  namespace for its best score (`nullptr` if none), `start()` and
  `update(const Input &)`.

## Adding an app

1. Write `src/<app>.cpp`. Keep its code in an anonymous namespace, and end
   it with `const Game NAME = {"Name", "nvs-ns", start, update};`.
2. Declare `extern const Game NAME;` in `os.h`.
3. Add an `Entry` with a 9x9 icon to a menu in `main.cpp` (`HOME_ENTRIES`,
   `GAME_ENTRIES` or `DEMO_ENTRIES`). A ring holds 8.

`update()` runs once per frame. It reads the input, moves things, then
draws the whole frame between `oled.clearBuffer()` and `oled.sendBuffer()`.
For steering, use `curve(in.x)`: it squares the stick value, which hides
the joystick's drift.

## Screen capture

`scripts/oled-capture.py` (repo root) records the screen to PNG or GIF. It
sends `CAP 460800`; the board switches to that baud rate and streams every
frame (`0xA5 0x5A`, millis, the 1024-byte buffer, an XOR check) until `END`.
The screen doesn't sleep while recording.

## Serial output

- At boot: the stick centre.
- Once a second: the raw stick values, to check drift.
- Wear counter: every count when it opens, and each +1 or wash.
- Games: a line when a level or round ends; Crypt prints its drawing time every 2 s.
