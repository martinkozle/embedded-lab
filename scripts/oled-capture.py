#!/usr/bin/env python3
"""Record the OLED of a board running Martin OS, and save PNGs or a GIF.

The board streams its 128x64 screen buffer over serial at 460800 baud while
recording (see
serviceCapture() in projects/martin-os/src/os.cpp). Frames are drawn like a
real OLED: pixels scaled up with a thin dark gap around each one.

Run it with PlatformIO's bundled Python (it ships pyserial):

    "$(uv tool dir)/platformio/bin/python" scripts/oled-capture.py --seconds 15 --gif docs/media/wear.gif
    "$(uv tool dir)/platformio/bin/python" scripts/oled-capture.py --seconds 1 --png docs/media/home.png

Opening the port restarts the board (macOS toggles the reset line), so every
clip starts with the boot animation unless trimmed. Options: --reset forces
a restart, --save/--load keep a long recording to cut clips from with
--start/--end (seconds into it),
--trim-start/--trim-end cut seconds off the clip, --scale sets the pixel size.
Needs ImageMagick (`magick`) to assemble the GIF.
"""

import argparse
import glob
import os
import subprocess
import sys
import tempfile
import time
import zlib
import struct

import serial

W, H, FRAME = 128, 64, 1024
CAP_BAUD = 460800  # 921600 garbles on macOS with this board's CH34x USB chip
ON, OFF, GAP = (210, 232, 255), (10, 14, 22), (0, 0, 0)  # cool white OLED


def find_port() -> str:
    ports = sorted(glob.glob("/dev/cu.usbserial*") + glob.glob("/dev/cu.wchusbserial*")
                   + glob.glob("/dev/cu.SLAB_USBtoUART*") + glob.glob("/dev/ttyUSB*"))
    if len(ports) != 1:
        sys.exit(f"Need exactly one serial port, found {ports}; pass --port")
    return ports[0]


def record(port: str, seconds: float, reset: bool) -> list[tuple[int, bytes]]:
    frames = []
    ser = serial.Serial()
    ser.port, ser.baudrate, ser.timeout = port, 115200, 0.05
    ser.dtr = ser.rts = False
    ser.open()
    with ser:
        if reset:
            ser.rts = True
            time.sleep(0.1)
            ser.rts = False
        # On macOS opening the port restarts the board anyway. Wait for it to
        # boot (its banner), or 1.5 s if it didn't restart, before asking.
        seen = b""
        deadline = time.monotonic() + 1.5
        while time.monotonic() < deadline and b"martin-os starting" not in seen:
            seen = (seen + ser.read(512))[-256:]
        if b"martin-os starting" in seen:
            time.sleep(0.6)  # setup() finishing
        ser.reset_input_buffer()
        ser.write(f"CAP {CAP_BAUD}\n".encode())
        ser.flush()
        time.sleep(0.2)  # the board reads it at its next frame
        ser.baudrate = CAP_BAUD
        ser.reset_input_buffer()
        buf = bytearray()
        end = time.monotonic() + seconds
        print(f"recording {seconds:.0f} s ...", file=sys.stderr)
        try:
            while time.monotonic() < end:
                buf += ser.read(8192)
                while True:
                    i = buf.find(b"\xa5\x5a")
                    if i < 0:
                        del buf[:-1]
                        break
                    if len(buf) < i + 6 + FRAME + 1:
                        del buf[:i]
                        break
                    t = int.from_bytes(buf[i + 2:i + 6], "little")
                    data = bytes(buf[i + 6:i + 6 + FRAME])
                    check = 0
                    for b in data:
                        check ^= b
                    if check == buf[i + 6 + FRAME]:
                        frames.append((t, data))
                        del buf[:i + 7 + FRAME]
                    else:
                        del buf[:i + 1]  # a false start; look further on
        except KeyboardInterrupt:
            pass
        ser.write(b"END\n")
        ser.flush()
        time.sleep(0.05)
        ser.baudrate = 115200
    print(f"got {len(frames)} frames", file=sys.stderr)
    return frames


def pixel(data: bytes, x: int, y: int) -> bool:
    return bool(data[(y >> 3) * W + x] >> (y & 7) & 1)


def render(data: bytes, scale: int) -> bytes:
    """RGB rows of the screen, each pixel a (scale-1) square plus a 1 px gap."""
    out = bytearray()
    for y in range(H):
        row = bytearray()
        for x in range(W):
            c = ON if pixel(data, x, y) else OFF
            row += bytes(c) * (scale - 1) + bytes(GAP)
        gap_row = bytes(GAP) * (W * scale)
        out += bytes(row) * (scale - 1) + gap_row
    return bytes(out)


def write_png(path: str, rgb: bytes, w: int, h: int) -> None:
    raw = b"".join(b"\x00" + rgb[y * w * 3:(y + 1) * w * 3] for y in range(h))

    def chunk(kind: bytes, body: bytes) -> bytes:
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))

    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--port")
    ap.add_argument("--seconds", type=float, default=10)
    ap.add_argument("--reset", action="store_true")
    ap.add_argument("--gif")
    ap.add_argument("--png", help="save the last frame")
    ap.add_argument("--trim-start", type=float, default=0)
    ap.add_argument("--trim-end", type=float, default=0)
    ap.add_argument("--save", help="also save the raw recording, to cut clips from later")
    ap.add_argument("--load", help="use a saved recording instead of recording")
    ap.add_argument("--start", type=float, help="clip start, seconds into the recording")
    ap.add_argument("--end", type=float, help="clip end, seconds into the recording")
    ap.add_argument("--scale", type=int, default=4)
    args = ap.parse_args()

    if args.load:
        raw = open(args.load, "rb").read()
        frames = [(int.from_bytes(raw[i:i + 4], "little"), raw[i + 4:i + 4 + FRAME])
                  for i in range(0, len(raw), 4 + FRAME)]
    else:
        frames = record(args.port or find_port(), args.seconds, args.reset)
    if not frames:
        sys.exit("no frames: is Martin OS with screen capture flashed?")
    if args.save:
        with open(args.save, "wb") as f:
            for t, data in frames:
                f.write(t.to_bytes(4, "little") + data)
    first = frames[0][0]
    t0, t1 = first + args.trim_start * 1000, frames[-1][0] - args.trim_end * 1000
    if args.start is not None:
        t0 = first + args.start * 1000
    if args.end is not None:
        t1 = first + args.end * 1000
    frames = [f for f in frames if t0 <= f[0] <= t1]
    w, h = W * args.scale, H * args.scale

    if args.png:
        os.makedirs(os.path.dirname(args.png) or ".", exist_ok=True)
        write_png(args.png, render(frames[-1][1], args.scale), w, h)
        print(f"wrote {args.png}", file=sys.stderr)
    if args.gif:
        os.makedirs(os.path.dirname(args.gif) or ".", exist_ok=True)
        # Merge repeated frames into one longer one; GIF delays are in 1/100 s.
        clips = []
        for i, (t, data) in enumerate(frames):
            nxt = frames[i + 1][0] if i + 1 < len(frames) else t + 42
            if clips and clips[-1][1] == data:
                clips[-1][0] += nxt - t
            else:
                clips.append([nxt - t, data])
        with tempfile.TemporaryDirectory() as tmp:
            cmd = ["magick"]
            for k, (ms, data) in enumerate(clips):
                path = os.path.join(tmp, f"{k:05d}.png")
                write_png(path, render(data, args.scale), w, h)
                cmd += ["-delay", str(max(2, round(ms / 10))), path]
            cmd += ["-loop", "0", "-layers", "Optimize", args.gif]
            subprocess.run(cmd, check=True)
        print(f"wrote {args.gif} ({len(clips)} frames, {os.path.getsize(args.gif) // 1024} KB)", file=sys.stderr)


if __name__ == "__main__":
    main()
