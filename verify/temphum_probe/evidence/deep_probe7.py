#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""第六轮：迟到应答帧 / 超时后残留。

现象：偶发「上一帧的应答迟到」，把下一帧的应答挤歪。本脚本只读、不改配置。
"""
import struct
import sys
import time

sys.path.insert(0, __import__("os").path.dirname(
    __import__("os").path.dirname(__import__("os").path.abspath(__file__))))
from probe_temphum import build, crc16, read_regs

PORT, UNIT = "COM17", 4


class Port:
    def __enter__(self):
        import serial
        last = None
        for i in range(24):
            try:
                self.sp = serial.Serial(PORT, 115200, bytesize=8, parity="N",
                                        stopbits=1, timeout=0.02,
                                        write_timeout=1.0)
                time.sleep(0.06)
                self.sp.reset_input_buffer()
                return self.sp
            except Exception as exc:                   # noqa: BLE001
                last = exc
                time.sleep(0.4 * (1 + i % 5))
        raise last

    def __exit__(self, *e):
        try:
            self.sp.close()
        except Exception:                              # noqa: BLE001
            pass
        return False


def main():
    print("=== A. 1000 次超短超时(3ms) 触发超时，再等 200ms 看有无迟到帧 ===")
    with Port() as sp:
        late = 0
        for i in range(300):
            f = build(UNIT, 0x04, 0x0000, 2)
            sp.reset_input_buffer()
            sp.write(f)
            sp.flush()
            t0 = time.time()
            got = sp.read(9)                        # timeout=20ms
            if not got:
                time.sleep(0.2)
                late_frame = sp.read(256)
                if late_frame:
                    late += 1
                    if late <= 5:
                        print("  #%d 超时后收到迟到帧 %s" % (i, late_frame.hex()))
            time.sleep(0.03)
        print("  迟到帧 %d/300" % late)

    print("\n=== B. 大量读写混合后清空缓冲，检查是否有半帧残留 ===")
    with Port() as sp:
        for _ in range(200):
            read_regs(sp, UNIT, 0x04, 0x0000, 6, 0.25)
        time.sleep(0.3)
        resid = sp.read(512)
        print("  静默 300ms 后残留字节: %s" % (resid.hex() if resid else "无（干净）"))

    print("\n=== C. 300 次连续块读，统计「帧错位/CRC 错」发生率 ===")
    with Port() as sp:
        bad = 0
        for i in range(300):
            r, rtt, e = read_regs(sp, UNIT, 0x04, 0x0000, 6, 0.3)
            if e:
                bad += 1
                if bad <= 5:
                    print("  #%d %s" % (i, e))
        print("  失败/错位 %d/300" % bad)

    print("\n=== D. 静默 2s 后首帧 RTT（是否有握手/唤醒开销）===")
    with Port() as sp:
        for i in range(3):
            time.sleep(2.0)
            r, rtt, e = read_regs(sp, UNIT, 0x04, 0x0000, 2, 0.5)
            print("  静默2s后 rtt=%.2fms %s" % (rtt, r if not e else e))


if __name__ == "__main__":
    main()
