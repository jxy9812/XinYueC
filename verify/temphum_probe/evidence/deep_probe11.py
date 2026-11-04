#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""决定性测试：残尾帧是否与我们的请求绑定。

A 组：只发 unit=5（无应答站号）→ 若残尾仍出现 ⇒ 模块自发输出
B 组：只发 unit=4（正常应答）→ 对照组
C 组：发 unit=4 但改读不同寄存器 → 看残尾是否随之变化
"""
import struct
import sys
import time

sys.path.insert(0, __import__("os").path.dirname(
    __import__("os").path.dirname(__import__("os").path.abspath(__file__))))
from probe_temphum import build, crc16

PORT = "COM17"


def run(sp, unit, addr, cnt, n, label):
    """发 n 次 FC04(addr,cnt)，统计残尾出现次数与内容。"""
    resid = []
    for _ in range(n):
        f = build(unit, 0x04, addr, cnt)
        sp.reset_input_buffer()
        sp.write(f)
        sp.flush()
        buf = b""
        deadline = time.perf_counter() + 0.25
        want = 3 + cnt * 2 + 2
        while time.perf_counter() < deadline and len(buf) < want:
            c = sp.read(want - len(buf))
            if c:
                buf += c
            else:
                time.sleep(0.001)
        extra = sp.read(512)
        if extra:
            resid.append(extra.hex())
        time.sleep(0.01)
    print("  %-28s 残尾 %d/%d" % (label, len(resid), n))
    for r in resid[:6]:
        print("      %s" % r)
    return resid


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
        print("=== A. 只发 unit=5（无应答站号），看残尾是否仍出现 ===")
        a = run(sp, 5, 0x0000, 6, 400, "unit=5 addr=0 cnt=6")

        print("\n=== B. 只发 unit=4（正常应答），对照组 ===")
        b = run(sp, 4, 0x0000, 6, 400, "unit=4 addr=0 cnt=6")

        print("\n=== C. unit=4 读不同寄存器，残尾内容是否随之变化 ===")
        c1 = run(sp, 4, 0x0000, 2, 300, "unit=4 addr=0 cnt=2")
        c2 = run(sp, 4, 0x0000, 20, 300, "unit=4 addr=0 cnt=20")

        print("\n=== D. 结论 ===")
        print("  A(unit5, 无应答站号) 残尾 %d 次 -> %s"
              % (len(a), "自发输出（非应答）" if a else "与应答绑定"))
        print("  B(unit4, 正常)      残尾 %d 次" % len(b))
        print("  C cnt=2 残尾 %d 次；cnt=20 残尾 %d 次" % (len(c1), len(c2)))
    finally:
        sp.close()


if __name__ == "__main__":
    main()
