# Embedded playground

A monorepo of small toy projects for ESP32 and Arduino-type boards, mostly
written by agents. Every project is a self-contained PlatformIO project.

## Layout

- `projects/<name>/`: one PlatformIO project per idea (`platformio.ini`, `src/`).
- `lib/<LibName>/`: code shared between projects. Projects pick it up through
  `lib_extra_dirs = ../../lib` in their `platformio.ini`.
- `hardware.md`: the boards and parts we own, and how they are wired.
  **Read it before writing pin numbers or choosing a board.** Update it when
  wiring changes or new hardware arrives.
- `scripts/serial-log.py`: reads serial output for a fixed time (see below).

## Toolchain

PlatformIO Core, installed with `uv tool install platformio` (`pio` on PATH).
Do not install it with the system Python.

## Pin every version

Projects must keep building years later. In every `platformio.ini`:

- pin the platform to an exact version: `platform = platformio/espressif32 @ 8.0.0`,
  never a bare `espressif32`;
- pin libraries to exact versions in `lib_deps` (`owner/Name @ 1.2.3`).

When starting a new project, copy the pins from the newest existing project
unless there is a reason to move forward, and say so when you do.

## The agent loop

Run these from the project directory (`projects/<name>/`):

```sh
pio run                      # build
pio run -t upload            # build and flash (auto-detects the port)
"$(uv tool dir)/platformio/bin/python" ../../scripts/serial-log.py --seconds 10
```

- `pio device monitor` is interactive; do not use it. Use `serial-log.py`,
  which prints the output for `--seconds` and exits. Add `--reset` to restart
  the board first and catch boot output.
- Find connected boards with `pio device list` or `ls /dev/cu.*`
  (classic ESP32 boards show up as `/dev/cu.usbserial-*`).
- If no port appears, the board is not detected: check the cable (many are
  power-only) and the USB dongle chain before debugging software.
- The first build of a platform version downloads the toolchain into
  `~/.platformio`; it takes a few minutes.

## Starting a new project

1. `mkdir -p projects/<name>/src`
2. Write `platformio.ini` with one `[env:<board>]` per target board, pinned as
   above. Board IDs come from `hardware.md` or `pio boards <filter>`.
3. Print something on `Serial` at 115200 baud in `setup()` so the loop above
   can confirm the firmware is running.
