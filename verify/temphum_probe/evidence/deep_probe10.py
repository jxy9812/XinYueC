#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""主动轮询定性：残尾帧的时间分布（用于判断是否外部主站并发）。

只发本机请求、只统计本机应答，不依赖被动监听。
"""
import struct
import sys
import time

sys.path.insert(0, __import__("os").path.dirname(
    __import__("os").path.dirname(__import__("os").path.abspath(__file__))))
from probe_temphum import build, crc16, read_regs

PORT, UNIT = "COM17", 4


def main():
    import serial
    sp = None
    for i in range(30):
        try:
            sp = serial.Serial(PORT, 115200, bytesize=8, parity="N",
                               stopbits=1, timeout=0.02, write_timeout=1.0)
            break
        except Exception:                              # noqa: BLE001
            time.sleep(1.0)
    if not sp:
        print("打不开串口（被占用）")
        return
    time.sleep(0.06)
    try:
        print("=== 主动轮询 2000 帧，记录残尾帧的时间位置 ===")
        t0 = time.time()
        marks = []
        rtts = []
        for i in range(2000):
            f = build(UNIT, 0x04, 0x0000, 6)
            sp.reset_input_buffer()
            sp.write(f)
            sp.flush()
            ta = time.perf_counter()
            buf = b""
            deadline = ta + 0.3
            while time.perf_counter() < deadline and len(buf) < 17:
                c = sp.read(17 - len(buf))
                if c:
                    buf += c
                else:
                    time.sleep(0.001)
            tgot = time.perf_counter()
            extra = sp.read(512)
            rtts.append((tgot - ta) * 1000.0)
            ok = (len(buf) == 17 and buf[0] == UNIT and buf[1] == 0x04 and
                  crc16(buf[:-2]) == struct.unpack("<H", buf[-2:])[0])
            if not ok or extra:
                marks.append((i, round(tgot - t0, 3), len(buf),
                              extra.hex() if extra else ""))
            time.sleep(0.01)
        dur = time.time() - t0
        print("  总帧 %d  用时 %.1fs  异常 %d 帧 (%.2f%%)"
              % (2000, dur, len(marks), len(marks) / 20.0))
        print("  异常序列（下标, 时刻s, 主帧长, 余量）:")
        for m in marks[:40]:
            print("    %s" % (m,))
        if len(marks) > 1:
            gaps = [marks[i + 1][0] - marks[i][0] for i in range(len(marks) - 1)]
            print("  相邻异常间隔（帧数）: %s" % gaps[:30])
            print("  间隔 min=%d max=%d 均值=%.1f"
                  % (min(gaps), max(gaps), sum(gaps) / len(gaps)))
        s = sorted(rtts)
        print("  RTT p50 %.2f p95 %.2f p99 %.2f max %.2f ms"
              % (s[len(s) // 2], s[int(len(s) * .95)],
                 s[int(len(s) * .99)], s[-1]))
    finally:
        sp.close()


if __name__ == "__main__":
    main()
