#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""第三轮：float32 低字定性 / 波特率实证 / 异常帧 / 从站延迟构成。
"""
import collections
import struct
import sys
import time

sys.path.insert(0, __import__("os").path.dirname(__import__("os").path.dirname(__import__("os").path.abspath(__file__))))
from probe_temphum import (build, crc16, f32, read_regs, recv_frame, transact)

PORT, UNIT = "COM17", 4


def open_port(baud, parity="N"):
    import serial
    last = None
    for i in range(12):
        try:
            sp = serial.Serial(PORT, baud, bytesize=8, parity=parity,
                               stopbits=1, timeout=0.05, write_timeout=1.0)
            time.sleep(0.08)
            sp.reset_input_buffer()
            return sp
        except Exception as exc:                       # noqa: BLE001
            last = exc
            time.sleep(1.0)
    raise last


def main():
    print("=== 1. float32 低字单独读 vs 块读（各 60 次）===")
    sp = open_port(115200)
    try:
        solo3 = collections.Counter()
        solo5 = collections.Counter()
        blk3 = collections.Counter()
        blk5 = collections.Counter()
        for _ in range(60):
            r, _, e = read_regs(sp, UNIT, 0x04, 0x0003, 1, 0.4)
            if not e:
                solo3[r[0]] += 1
            r, _, e = read_regs(sp, UNIT, 0x04, 0x0005, 1, 0.4)
            if not e:
                solo5[r[0]] += 1
            r, _, e = read_regs(sp, UNIT, 0x04, 0x0000, 6, 0.4)
            if not e:
                blk3[r[3]] += 1
                blk5[r[5]] += 1
        print("  0003H 单读 : %s" % dict(solo3))
        print("  0003H 块读 : %s" % dict(blk3))
        print("  0005H 单读 : %s" % dict(solo5))
        print("  0005H 块读 : %s" % dict(blk5))
    finally:
        sp.close()

    print("\n=== 2. float32 完整 4 字节序列（10 帧原始十六进制）===")
    sp = open_port(115200)
    try:
        for i in range(10):
            r, _, e = read_regs(sp, UNIT, 0x04, 0x0000, 6, 0.4)
            if e:
                print("  %2d %s" % (i, e))
                continue
            print("  %2d  0000-0001=%04X %04X  0002-0003=%04X %04X -> %s"
                  "  0004-0005=%04X %04X -> %s"
                  % (i, r[0], r[1], r[2], r[3],
                     ("%.6f" % f32(r[2:4])),
                     r[4], r[5], ("%.6f" % f32(r[4:6]))))
    finally:
        sp.close()

    print("\n=== 3. 波特率实证：9600 / 19200 / 115200 ===")
    for baud in (9600, 19200, 115200):
        try:
            sp = open_port(baud)
            r, rtt, e = read_regs(sp, UNIT, 0x04, 0x0000, 2, 0.6)
            print("  %6d 8N1 -> %s (rtt %.1fms)"
                  % (baud, ("OK %s" % r) if not e else e, rtt))
            sp.close()
        except Exception as exc:                       # noqa: BLE001
            print("  %6d -> %s" % (baud, exc))

    print("\n=== 4. 从站延迟构成：1~6 个字读的 RTT（各 100 次中位数）===")
    sp = open_port(115200)
    try:
        for n in (1, 2, 6, 10, 20):
            vals = []
            for _ in range(100):
                r, rtt, e = read_regs(sp, UNIT, 0x04, 0x0000, n, 0.4)
                if not e:
                    vals.append(rtt)
            vals.sort()
            print("  count=%-3d 回包 %2d 字节  p50=%.2fms  p95=%.2fms"
                  % (n, 3 + n * 2 + 2,
                     vals[len(vals) // 2], vals[int(len(vals) * .95)]))
    finally:
        sp.close()

    print("\n=== 5. 异常帧采样（非法 FC，200 次，看是否有 0x80 异常码）===")
    sp = open_port(115200)
    try:
        exc = collections.Counter()
        none = 0
        for _ in range(200):
            raw, rtt, e = transact(sp, UNIT, 0x63, 0x0000, 2, 0.2)
            if raw and len(raw) >= 3 and raw[1] & 0x80 and \
                    len(raw) == 5 and crc16(raw[:-2]) == \
                    struct.unpack("<H", raw[-2:])[0]:
                exc["0x%02X/0x%02X" % (raw[1], raw[2])] += 1
            elif raw:
                exc["非标帧 len=%d %s" % (len(raw), raw.hex())] += 1
            else:
                none += 1
        print("  静默超时 %d/200" % none)
        for k, v in exc.most_common(10):
            print("  %-40s x%d" % (k, v))
    finally:
        sp.close()

    print("\n=== 6. 站号扫描 1~247（超时 0.12s，快扫）===")
    sp = open_port(115200)
    try:
        found = []
        for u in range(1, 248):
            r, rtt, e = read_regs(sp, u, 0x04, 0x0000, 2, 0.12)
            if not e:
                found.append((u, r))
        print("  应答: %s" % found)
    finally:
        sp.close()


if __name__ == "__main__":
    main()
