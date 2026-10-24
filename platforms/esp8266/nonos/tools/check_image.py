#!/usr/bin/env python
"""ESP8266 app image health check — catch the "length/CRC overwrote code" bug.

The 2026-10-23 crash root cause: `fix_rodata_pos.py` wrote the image length
and CRC32 at fixed offsets 0x1010/0x1014.  ESP8266 app images have no
size/CRC fields there (that is the ESP32 extended header), so those bytes
overwrote the first 8 bytes of `.irom0.text` — the entry instructions of
the section's first function.  The CPU then fetched garbage and died with
IllegalInstructionCause at a fixed epc1.

This check compares the bytes actually in the image against the ELF, so the
same class of bug is caught before it reaches the board.

Usage:
    check_image.py [elf] [bin]
Defaults: esp8266/nonos/build/fw_final.elf and esp8266/nonos/fw_final.bin
"""
import os
import struct
import subprocess
import sys

ELF = sys.argv[1] if len(sys.argv) > 1 else "esp8266/nonos/build/fw_final.elf"
BIN = sys.argv[2] if len(sys.argv) > 2 else "esp8266/nonos/fw_final.bin"
OBJDUMP = r"C:\esp\toolchain-xtensa\bin\xtensa-lx106-elf-objdump.exe"

APP_OFFSET = 0x1000
CACHE_BASE = 0x40200000
CHECK_BYTES = 32


def section_bytes(elf, sec, count):
    """First `count` bytes of an ELF section (via objdump -s)."""
    r = subprocess.run([OBJDUMP, "-s", "-j", sec, elf],
                       capture_output=True, text=True)
    out = b""
    for line in r.stdout.splitlines():
        parts = line.split()
        if len(parts) < 2 or not parts[0].strip().isdigit():
            continue
        for h in parts[1:5]:
            if len(h) == 8:
                try:
                    out += bytes.fromhex(h)
                except ValueError:
                    pass
        if len(out) >= count:
            break
    return out[:count]


def main():
    if not os.path.exists(ELF) or not os.path.exists(BIN):
        print("missing: %s / %s" % (ELF, BIN))
        return 2
    img = open(BIN, "rb").read()
    elf_head = section_bytes(ELF, ".irom0.text", CHECK_BYTES)
    if not elf_head:
        print("cannot read .irom0.text from %s" % ELF)
        return 2

    # The first flash-resident segment starts at the app header + 8 bytes.
    seg0_off = APP_OFFSET + 8
    addr, size = struct.unpack_from("<II", img, seg0_off)
    img_head = img[seg0_off + 8: seg0_off + 8 + CHECK_BYTES]

    print("image      : %s (%d bytes)" % (BIN, len(img)))
    print("segment 0  : addr=0x%08x size=0x%x (file off 0x%x)" % (addr, size, seg0_off + 8))
    print("elf  .irom0.text[0:%d] : %s" % (CHECK_BYTES, elf_head.hex()))
    print("bin  seg0    [0:%d] : %s" % (CHECK_BYTES, img_head.hex()))

    if addr < CACHE_BASE:
        print("OK: segment 0 is not flash-resident; nothing to compare")
        return 0
    # VMA x maps to flash offset x - CACHE_BASE.
    want_off = addr - CACHE_BASE
    print("expected file offset for this segment: 0x%x" % want_off)
    if img[want_off:want_off + CHECK_BYTES] != elf_head:
        print("MISMATCH: the image bytes at the cache-mapped offset do not match "
              "the ELF section head.")
        print("  -> the image layout is wrong; the CPU will fetch the wrong "
              "bytes at runtime (this is the 0x1010 length/CRC bug class).")
        return 1
    print("OK: image bytes match the ELF section head (cache mapping intact)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
