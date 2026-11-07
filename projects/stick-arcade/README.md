# Stick Arcade

A menu of small games for the ESP32, with the joystick, a button, the
0.96" OLED and the buzzer. The wiring is in `hardware.md`.

## Controls in every game

- **Stick up/down** in the menu picks a game. **A** or a **stick press** starts it.
- **Hold the stick button for about 0.7 s** to go back to the menu from any game.
- On the game-over screen, **A** or a **stick press** plays again.
- Best scores are saved in NVS (flash), one namespace per game.
  `pio run -t erase` wipes them.

## Games

| Game | File | Controls |
|---|---|---|
| Dodge | `dodge.cpp` | Stick steers the ship. A dashes (2 s cooldown). |
| Snake | `snake.cpp` | Stick turns. Hold A to go double speed for double points. |
| Maze 3D | `maze.cpp` | Raycaster maze against the clock. Stick walks and turns. A shows the map (costs 3 s). |
| Crypt | `crypt.cpp` | First-person shooter in generated rooms. Tap A fires with auto-aim and locks on; while locked, left/right strafes. Hold A to throw an exploding orb. Stick press shows the map. |
| Bricks | `bricks.cpp` | Brick-breaker with generated, mirrored levels. Stick moves the paddle, A launches. |
| Stacker | `stacker.cpp` | Falling-blocks puzzle. Left/right moves, down drops faster, flick up drops hard, A rotates, stick press holds. |

## Code layout

- `arcade.h` / `arcade.cpp`: the parts the games share.
  - screen (`oled`), and the frame pacing at 24 fps, the most the OLED takes over 400 kHz I2C;
  - `readInput()`: stick with dead zone, flicks, short/long press, button A;
  - the non-blocking buzzer tunes (`play()`);
  - saved best scores (`loadBest` / `saveBest`);
  - `drawCentered` and the shared game-over screen.
- `main.cpp`: the menu (scrolls, 4 games visible) and the main loop.
- One `.cpp` per game. Each exports a `Game` struct with a name, an NVS
  namespace, `start()` and `update(const Input &)`.

## Adding a game

1. Write `src/<game>.cpp`. Keep its code in an anonymous namespace, and end
   it with `const Game NAME = {"Name", "nvs-ns", start, update};`.
2. Declare `extern const Game NAME;` in `arcade.h`.
3. Add `&NAME` to `GAMES` in `main.cpp`.

`update()` runs once per frame. It reads the input, moves things, then
draws the whole frame between `oled.clearBuffer()` and `oled.sendBuffer()`.
For steering, use `curve(in.x)`: it squares the stick value, which hides
the joystick's drift.

## Serial output

- At boot: the stick centre.
- Once a second: the raw stick values, to check drift.
- When a level or round ends: a line from each game.
- Every 2 s in Crypt: the drawing time.
