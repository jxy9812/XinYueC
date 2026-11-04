#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""第四轮：每段独立开关串口（用完立即释放，遇占用退避重试）。

待确认项：
  1. float32 低字到底是随机还是有规律（单读 vs 块读）
  2. 原始 4 字节序列留证
  3. 波特率实证（9600 / 19200 / 115200）
  4. RTT 随读长的构成
  5. 非法功能码是否真有 0x80 异常帧
"""
import collections
import struct
import sys
import time

sys.path.insert(0, __import__("os").path.dirname(__import__("os").path.dirname(__import__("os").path.abspath(__file__))))
from probe_temphum import (build, crc16, f32, read_regs, recv_frame, transact)

PORT, UNIT = "COM17", 4


class Port:
    """上下文管理器：打开 → 用 → 立即关闭；被占则退避重试。"""

    def __init__(self, baud=115200, parity="N"):
        self.baud, self.parity, self.sp = baud, parity, None

    def __enter__(self):
        import serial
        last = None
        for i in range(20):
            try:
                self.sp = serial.Serial(PORT, self.baud, bytesize=8,
                                        parity=self.parity, stopbits=1,
                                        timeout=0.05, write_timeout=1.0)
                time.sleep(0.06)
                self.sp.reset_input_buffer()
                return self.sp
            except Exception as exc:                   # noqa: BLE001
                last = exc
                time.sleep(0.5 * (1 + i % 4))
        raise last

    def __exit__(self, *exc):
        if self.sp:
            try:
                self.sp.close()
            except Exception:                          # noqa: BLE001
                pass
        return False


def sect(t):
    print("\n=== %s %s" % (t, "=" * max(0, 60 - len(t))), flush=True)


def main():
    sect("1. float32 低字：单读 vs 块读（各 50 次）")
    with Port() as sp:
        solo3, solo5, blk3, blk5 = (collections.Counter() for _ in range(4))
        for _ in range(50):
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
        print("  0003H 单读: %s" % dict(solo3))
        print("  0003H 块读: %s" % dict(blk3))
        print("  0005H 单读: %s" % dict(solo5))
        print("  0005H 块读: %s" % dict(blk5))

    sect("2. float32 原始 4 字节序列（8 帧）")
    with Port() as sp:
        for i in range(8):
            r, _, e = read_regs(sp, UNIT, 0x04, 0x0000, 6, 0.4)
            if e:
                print("  %d %s" % (i, e))
                continue
            print("  %d  T u16=%d  f=%04X %04X -> %.4f"
                  "  | H u16=%d  f=%04X %04X -> %.4f"
                  % (i, r[0], r[2], r[3], f32(r[2:4]),
                     r[1], r[4], r[5], f32(r[4:6])))

    sect("3. 波特率实证")
    for baud in (115200, 9600, 19200, 38400):
        try:
            with Port(baud) as sp:
                r, rtt, e = read_regs(sp, UNIT, 0x04, 0x0000, 2, 0.5)
                print("  %6d 8N1 -> %s" % (baud,
                                           ("OK %s (%.1fms)" % (r, rtt))
                                           if not e else e))
        except Exception as exc:                       # noqa: BLE001
            print("  %6d -> 打不开 %s" % (baud, exc))

    sect("4. RTT 随读长（各 60 次）")
    with Port() as sp:
        for n in (1, 2, 3, 6, 10, 20):
            vals = []
            for _ in range(60):
                r, rtt, e = read_regs(sp, UNIT, 0x04, 0x0000, n, 0.4)
                if not e:
                    vals.append(rtt)
            vals.sort()
            if vals:
                print("  count=%-3d 帧长 %2d B  p50=%6.2f  p95=%6.2f ms"
                      % (n, 3 + n * 2 + 2, vals[len(vals) // 2],
                         vals[int(len(vals) * .95)]))

    sect("5. 非法功能码 0x63（120 次，看有无 0x80 异常帧）")
    with Port() as sp:
        exc, none = collections.Counter(), 0
        for _ in range(120):
            raw, rtt, e = transact(sp, UNIT, 0x63, 0x0000, 2, 0.2)
            if raw and len(raw) == 5 and raw[1] & 0x80 and \
                    crc16(raw[:-2]) == struct.unpack("<H", raw[-2:])[0]:
                exc["异常帧 0x%02X/0x%02X" % (raw[1], raw[2])] += 1
            elif raw:
                exc["非标帧 len=%d %s" % (len(raw), raw.hex())] += 1
            else:
                none += 1
        print("  静默超时 %d/120" % none)
        for k, v in exc.most_common():
            print("  %-34s x%d" % (k, v))

    sect("6. 站号全扫 1~247（超时 0.10s，约 25s）")
    with Port() as sp:
        found = []
        for u in range(1, 248):
            r, rtt, e = read_regs(sp, u, 0x04, 0x0000, 2, 0.10)
            if not e:
                found.append((u, r))
        print("  应答: %s" % found)


if __name__ == "__main__":
    main()
