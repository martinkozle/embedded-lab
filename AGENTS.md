# embedded-lab

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
- `scripts/oled-capture.py`: records the OLED of a board running Martin OS to
  PNG or GIF. The GIFs in `docs/media/` (shown in `README.md`) come from it.

## Agent skills

Project skills live in `.claude/skills/` and are managed with the `skills` CLI.
`skills-lock.json` records where each one came from, with a content hash.

- Add: `npx skills add <owner/repo> -s <skill> -a claude-code -y`. Read the
  SKILL.md first, and reject skills that conflict with this file (unpinned
  versions, `pip install`, `pio device monitor`).
- Restore from the lock file: `npx skills experimental_install`.
- Remove: `npx skills remove -s <skill> -y` (this keeps the lock file in sync).

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

## Rewiring

Whatever firmware is in flash runs as soon as the board gets power. Code
written for the old wiring can drive a pin against whatever the new wiring
connects it to. So, for any wiring change:

1. **Erase first:** run `pio run -t erase` from any project. With an empty flash, every
   pin stays an input after reset and drives nothing.
2. **Have the user unplug USB, rewire, then plug it back in.** Never ask for a
   rewire while the board is powered.
3. **Check the new wiring** against the pin rules and the strapping pins in
   `hardware.md` (GPIO0, 2, 5, 12, 15) before flashing, then flash the
   firmware for the new layout.
4. **Update "Current wiring"** in `hardware.md`.

For trivial changes (an LED with a resistor, a button to GND), unplugging
alone is enough and step 1 can be skipped. Do the full procedure when a pin
connects to another chip's output or a bus (I2C/SPI/UART), anything at 5 V, a
relay or motor driver, or a strapping pin.

## Starting a new project

1. `mkdir -p projects/<name>/src`
2. Write `platformio.ini` with one `[env:<board>]` per target board, pinned as
   above. Board IDs come from `hardware.md` or `pio boards <filter>`.
3. Print something on `Serial` at 115200 baud in `setup()` so the loop above
   can confirm the firmware is running.

## Commits

- Commit only when the user asks. The repo is public on GitHub as
  `martinkozle/embedded-lab`.
- Author and committer: `Martin Popovski <martinkozle@yahoo.com>`, never a
  work address. Commits are GPG-signed (`commit.gpgsign` and
  `user.signingkey` are set in this repo's git config). Before pushing,
  check with `git log --format='%h %ae %G?'`.
- Never commit photos without stripping their metadata (EXIF, GPS) first.
