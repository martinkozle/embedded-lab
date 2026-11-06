#!/usr/bin/env python3
"""Read a serial port for a fixed time and print what arrives, then exit.

Non-interactive replacement for `pio device monitor`, so agents can read
device output. Run it with PlatformIO's bundled Python (it ships pyserial):

    "$(uv tool dir)/platformio/bin/python" scripts/serial-log.py [--port P] [--baud 115200] [--seconds 10] [--reset]
"""

import argparse
import glob
import sys
import time

import serial


def find_port() -> str:
    candidates = sorted(
        glob.glob("/dev/cu.usbserial*")
        + glob.glob("/dev/cu.usbmodem*")
        + glob.glob("/dev/cu.wchusbserial*")
        + glob.glob("/dev/cu.SLAB_USBtoUART*")
        + glob.glob("/dev/ttyUSB*")
        + glob.glob("/dev/ttyACM*")
    )
    if not candidates:
        sys.exit("No USB serial port found. Is the board plugged in?")
    if len(candidates) > 1:
        sys.exit(f"Several serial ports found, pass --port: {candidates}")
    return candidates[0]


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--port")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--seconds", type=float, default=10)
    ap.add_argument("--reset", action="store_true", help="pulse EN via RTS to restart the board first")
    args = ap.parse_args()

    port = args.port or find_port()
    with serial.Serial(port, args.baud, timeout=0.2) as ser:
        if args.reset:
            ser.dtr = False
            ser.rts = True
            time.sleep(0.1)
            ser.rts = False
        deadline = time.monotonic() + args.seconds
        while time.monotonic() < deadline:
            data = ser.read(4096)
            if data:
                sys.stdout.write(data.decode("utf-8", errors="replace"))
                sys.stdout.flush()


if __name__ == "__main__":
    main()
