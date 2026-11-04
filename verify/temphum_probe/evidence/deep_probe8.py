#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""第七轮：抓「异常帧」原始字节，判定是设备真错帧还是串口层错位。"""
import struct
import sys
import time

sys.path.insert(0, __import__("os").path.dirname(
    __import__("os").path.dirname(__import__("os").path.abspath(__file__))))
from probe_temphum import build, crc16

PORT, UNIT = "COM17", 4


def resync(buf):
    """在字节流里滑动窗口找 CRC 正确的帧，返回 (offset, frame)。"""
    for off in range(len(buf) - 4):
        for ln in range(5, len(buf) - off + 1):
            cand = buf[off:off + ln]
            if cand[0] != UNIT:
                continue
            if crc16(cand[:-2]) == struct.unpack("<H", cand[-2:])[0]:
                return off, cand
    return None, None


def main():
    import serial
    sp = None
    for i in range(24):
        try:
            sp = serial.Serial(PORT, 115200, bytesize=8, parity="N",
                               stopbits=1, timeout=0.02, write_timeout=1.0)
            break
        except Exception:                              # noqa: BLE001
            time.sleep(0.5)
    if not sp:
        print("打不开串口")
        return
    time.sleep(0.06)
    try:
        print("=== 500 次 FC04 x6，异常帧全量留证 ===")
        bad = 0
        shifted = 0
        hexdumps = []
        for i in range(500):
            sp.reset_input_buffer()
            sp.write(build(UNIT, 0x04, 0x0000, 6))
            sp.flush()
            buf = b""
            deadline = time.time() + 0.3
            while time.time() < deadline and len(buf) < 17:
                c = sp.read(17 - len(buf))
                if c:
                    buf += c
                else:
                    time.sleep(0.002)
            # 继续吸 30ms 看有没有多出来的字节
            time.sleep(0.03)
            extra = sp.read(512)
            full = buf + extra
            ok = (len(buf) == 17 and buf[0] == UNIT and buf[1] == 0x04
                  and crc16(buf[:-2]) == struct.unpack("<H", buf[-2:])[0])
            if ok and not extra:
                continue
            bad += 1
            off, frame = resync(full)
            if off is not None and off > 0:
                shifted += 1
            if len(hexdumps) < 8:
                hexdumps.append((i, buf.hex(), extra.hex() if extra else "",
                                 off, frame.hex() if frame else None))
            time.sleep(0.02)
        print("  异常 %d/500（其中错位后可重新同步 %d 次）" % (bad, shifted))
        for i, b, e, off, fr in hexdumps:
            print("  #%-4d 主帧 %s" % (i, b))
            if e:
                print("        余量 %s" % e)
            print("        重同步偏移=%s 帧=%s" % (off, fr))
    finally:
        sp.close()


if __name__ == "__main__":
    main()
