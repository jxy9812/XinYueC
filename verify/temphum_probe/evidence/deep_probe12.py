#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""残尾帧定性（纯主动轮询）+ 复现厂家测试软件帧。

厂家帧（用户提供）：
  发 04 03 00 32 00 01 25 90      ← 读保持寄存器 0x0032H，无收
  发 04 04 00 00 00 02 71 9E      ← 读输入寄存器 0000H x2
  收 04 04 04 00 D9 02 88 7E 79

实验：
  A. 复现厂家两条请求，看设备是否应答
  B. 只发 unit=5（不存在的站号）→ 残尾是否仍出现（判别是否为他方流量）
  C. 只发 unit=4 → 对照
  D. 残尾出现时刻与厂家轮询周期（~200ms）的关系
"""
import struct
import sys
import time

sys.path.insert(0, __import__("os").path.dirname(
    __import__("os").path.dirname(__import__("os").path.abspath(__file__))))
from probe_temphum import build, crc16

PORT = "COM17"
VENDOR_F03_0032 = bytes.fromhex("0403003200012590")
VENDOR_F04_0000x2 = bytes.fromhex("040400000002719E")
VENDOR_F04_RESP = bytes.fromhex("040404 00D9 0288 7E79".replace(" ", ""))


def open_port():
    import serial
    for i in range(40):
        try:
            sp = serial.Serial(PORT, 115200, bytesize=8, parity="N",
                               stopbits=1, timeout=0.02, write_timeout=1.0)
            time.sleep(0.06)
            sp.reset_input_buffer()
            return sp
        except Exception:                              # noqa: BLE001
            time.sleep(1.0)
    return None


def raw_exchange(sp, frame, wait=0.25):
    sp.reset_input_buffer()
    sp.write(frame)
    sp.flush()
    buf = b""
    deadline = time.perf_counter() + wait
    while time.perf_counter() < deadline:
        c = sp.read(256)
        if c:
            buf += c
            # 有帧就再等一小段抓残尾
            if len(buf) >= 3 and buf[1] not in (0x03, 0x04, 0x06) or \
                    len(buf) >= 3 + buf[2] + 2:
                time.sleep(0.02)
                buf += sp.read(256)
                break
        else:
            time.sleep(0.001)
    return buf


def poll(sp, unit, addr, cnt, n, label):
    resid = []
    t0 = time.perf_counter()
    for i in range(n):
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
        extra = sp.read(256)
        if extra:
            resid.append((round(time.perf_counter() - t0, 4), extra.hex()))
        time.sleep(0.01)
    print("  %-30s 残尾 %d/%d" % (label, len(resid), n))
    for r in resid[:8]:
        print("      t=%-8s %s" % r)
    return resid


def main():
    sp = open_port()
    if not sp:
        print("打不开串口（被占用）")
        return
    try:
        print("=== A. 复现厂家测试软件的两条请求 ===")
        for name, frm in (("FC03 0032H x1", VENDOR_F03_0032),
                          ("FC04 0000H x2", VENDOR_F04_0000x2)):
            got = raw_exchange(sp, frm)
            if not got:
                print("  %-16s -> 无应答" % name)
            else:
                ok = (len(got) >= 5 and
                      crc16(got[:len(got) - 2]) ==
                      struct.unpack("<H", got[-2:])[0])
                print("  %-16s -> %s  CRC=%s" %
                      (name, got.hex().upper(),
                       "OK" if ok else "BAD"))

        print("\n=== B. 只发 unit=5（不存在的站号），残尾是否仍出现 ===")
        b = poll(sp, 5, 0x0000, 6, 400, "unit=5 addr=0 cnt=6")

        print("\n=== C. 只发 unit=4（正常应答），对照组 ===")
        c = poll(sp, 4, 0x0000, 6, 400, "unit=4 addr=0 cnt=6")

        print("\n=== D. 结论 ===")
        if b and not c:
            print("  仅在无应答站号下出现 ⇒ 残尾来自【总线上的其他流量】")
        elif b and c:
            print("  两组都出现 ⇒ 与站号无关，判别依据见残尾时刻分布")
        else:
            print("  unit=5 下未出现 ⇒ 残尾与本机请求【绑定】")
        if c:
            ts = [x[0] for x in c]
            if len(ts) > 2:
                gaps = [round(ts[i + 1] - ts[i], 3)
                        for i in range(len(ts) - 1)]
                print("  unit=4 残尾时刻间隔: %s" % gaps)
    finally:
        sp.close()


if __name__ == "__main__":
    main()
