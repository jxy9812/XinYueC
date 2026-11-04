#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""第八轮：被动监听总线 10 s（不发送任何字节）。

若收到字节 → 总线上另有主机在主动查询（会污染我们的应答窗口）。
"""
import sys
import time

sys.path.insert(0, __import__("os").path.dirname(
    __import__("os").path.dirname(__import__("os").path.abspath(__file__))))

PORT = "COM17"


def main():
    import serial
    sp = None
    for i in range(24):
        try:
            sp = serial.Serial(PORT, 115200, bytesize=8, parity="N",
                               stopbits=1, timeout=0.05, write_timeout=1.0)
            break
        except Exception:                              # noqa: BLE001
            time.sleep(0.5)
    if not sp:
        print("打不开串口")
        return
    try:
        print("=== 被动监听 10 s（不发任何字节）@115200 ===")
        sp.reset_input_buffer()
        t0 = time.time()
        buf = b""
        marks = []
        while time.time() - t0 < 10:
            c = sp.read(256)
            if c:
                marks.append((time.time() - t0, len(c)))
                buf += c
        print("  收到 %d 字节，分 %d 段" % (len(buf), len(marks)))
        for t, n in marks[:20]:
            print("    t=%6.3fs  +%d 字节" % (t, n))
        if buf:
            for i in range(0, min(len(buf), 160), 32):
                print("    %s" % buf[i:i + 32].hex())
        else:
            print("  总线静默（无其他主机在查询）")
    finally:
        sp.close()


if __name__ == "__main__":
    main()
