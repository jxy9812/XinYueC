"""Capture serial output from the ESP8266 board and print it.

Usage: x_capture.py <port> <seconds> [baud]
Opens the port, drains anything pending, then records for the given
duration and prints everything received (decoded leniently).
"""
import sys
import time

import serial


def main():
    port = sys.argv[1] if len(sys.argv) > 1 else "COM2"
    seconds = float(sys.argv[2]) if len(sys.argv) > 2 else 30.0
    baud = int(sys.argv[3]) if len(sys.argv) > 3 else 74880
    reset = "reset" in sys.argv[4:]

    ser = serial.Serial(port, baud, timeout=0.2)

    if reset:
        # Pulse DTR/RTS the way esptool does: RTS low -> EN low -> high,
        # which puts the ESP8266 into its bootloader for a moment and then
        # lets it boot. Record from the reset pulse: no pre-drain, so the
        # whole boot banner is captured.
        ser.dtr = False
        ser.rts = True
        time.sleep(0.1)
        ser.rts = False
    else:
        # Discard whatever the board already emitted before we attached.
        deadline = time.time() + 0.5
        while time.time() < deadline:
            try:
                ser.read(4096)
            except Exception:
                pass

    buf = bytearray()
    end = time.time() + seconds
    while time.time() < end:
        try:
            chunk = ser.read(4096)
        except Exception as exc:
            print("[capture] read error: %s" % exc)
            break
        if chunk:
            buf += chunk
            sys.stdout.write(chunk.decode("utf-8", errors="replace"))
            sys.stdout.flush()
    ser.close()
    print("\n[capture] %d bytes total" % len(buf))
    return 0


if __name__ == "__main__":
    sys.exit(main())
