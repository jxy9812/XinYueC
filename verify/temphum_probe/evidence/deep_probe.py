#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""深挖：float32 低字随机性 / 块读长度上限 / 帧间隔 / 校验位 / 站号唯一性。
"""
import struct
import sys
import time

sys.path.insert(0, __import__("os").path.dirname(__import__("os").path.dirname(__import__("os").path.abspath(__file__))))
from probe_temphum import (build, crc16, f32, read_regs, recv_frame,
                           decode_u16_temp)

PORT, BAUD, UNIT, TMO = "COM17", 115200, 4, 0.4


def open_port(baud=BAUD, parity="N"):
    import serial
    sp = serial.Serial(PORT, baud, bytesize=8, parity=parity, stopbits=1,
                       timeout=0.05, write_timeout=1.0)
    time.sleep(0.08)
    sp.reset_input_buffer()
    return sp


def main():
    sp = open_port()
    try:
        print("=== 1. float32 低字稳定性（40 次块读 0000H x6）===")
        hi_t, lo_t, hi_h, lo_h = [], [], [], []
        for _ in range(40):
            regs, rtt, err = read_regs(sp, UNIT, 0x04, 0x0000, 6, TMO)
            if err:
                continue
            hi_t.append(regs[2]); lo_t.append(regs[3])
            hi_h.append(regs[4]); lo_h.append(regs[5])
        print("  温度 float 高字(0002H) 不同值 %d 个: %s"
              % (len(set(hi_t)), sorted(set(hi_t))[:6]))
        print("  温度 float 低字(0003H) 不同值 %d 个: %s"
              % (len(set(lo_t)), sorted(set(lo_t))[:8]))
        print("  湿度 float 高字(0004H) 不同值 %d 个: %s"
              % (len(set(hi_h)), sorted(set(hi_h))[:6]))
        print("  湿度 float 低字(0005H) 不同值 %d 个: %s"
              % (len(set(lo_h)), sorted(set(lo_h))[:8]))
        print("  样本 BE 解码: %s" % [round(f32(p), 3)
                                      for p in list(zip(hi_t, lo_t))[:5]])
        print("  仅高字+零低字解码: %s"
              % [round(f32((h, 0)), 3) for h in hi_t[:5]])

        print("\n=== 2. u16 与 float 高字口径对照（20 次）===")
        for _ in range(6):
            regs, rtt, err = read_regs(sp, UNIT, 0x04, 0x0000, 6, TMO)
            if err:
                continue
            t16 = regs[0] * 0.1
            tf = f32((regs[2], 0))
            h16 = regs[1] * 0.1
            hf = f32((regs[4], 0))
            print("  T: u16=%.1f  float(hi,0)=%.4f  Δ=%.4f | "
                  "H: u16=%.1f  float(hi,0)=%.4f  Δ=%.4f"
                  % (t16, tf, tf - t16, h16, hf, hf - h16))
            time.sleep(0.05)

        print("\n=== 3. FC04 块读长度上限（起始 0000H）===")
        for n in (17, 20, 21, 24, 32, 40, 50, 64, 100, 120):
            regs, rtt, err = read_regs(sp, UNIT, 0x04, 0x0000, n, TMO)
            print("  count=%-4d -> %s" % (n, "OK %d regs 末3=%s (%.1fms)"
                                          % (len(regs), regs[-3:], rtt)
                                          if not err else err))

        print("\n=== 4. FC03 块读长度上限（起始 0000H）===")
        for n in (14, 17, 20, 21, 24, 32, 64, 120):
            regs, rtt, err = read_regs(sp, UNIT, 0x03, 0x0000, n, TMO)
            print("  count=%-4d -> %s" % (n, "OK %d regs 末3=%s"
                                          % (len(regs), regs[-3:])
                                          if not err else err))

        print("\n=== 5. 地址上界精查（FC04 count=1）===")
        for addr in (0x0010, 0x0011, 0x0012, 0x0013, 0x0014, 0x0015):
            regs, rtt, err = read_regs(sp, UNIT, 0x04, addr, 1, TMO)
            print("  %04XH -> %s" % (addr, regs if not err else err))

        print("\n=== 6. 帧间隔要求（每次读前插入 N ms 静默）===")
        for gap_ms in (0, 0.5, 1, 2, 3, 5, 10):
            ok = fail = 0
            for _ in range(40):
                if gap_ms:
                    time.sleep(gap_ms / 1000.0)
                regs, rtt, err = read_regs(sp, UNIT, 0x04, 0x0000, 2, 0.25)
                if err:
                    fail += 1
                else:
                    ok += 1
            print("  gap=%5.1fms  OK %2d/40  失败 %d" % (gap_ms, ok, fail))

        print("\n=== 7. 逐字节发送（模拟慢主机/无缓冲）===")
        frame = build(UNIT, 0x04, 0x0000, 2)
        for delay_ms in (0, 1, 2, 3):
            sp.reset_input_buffer()
            for b in frame:
                sp.write(bytes([b]))
                sp.flush()
                if delay_ms:
                    time.sleep(delay_ms / 1000.0)
            raw = recv_frame(sp, 9, 0.3)
            print("  字节间 %dms -> %s" % (delay_ms,
                                           "应答 %s" % raw.hex() if raw else "无应答"))
    finally:
        sp.close()

    print("\n=== 8. 校验位探测（同一站号 4，8E1 / 8O1 / 8N2 / 8N1）===")
    for parity, stop in (("N", 1), ("E", 1), ("O", 1), ("N", 2)):
        try:
            p = open_port(BAUD, parity)
            p.stopbits = stop
            regs, rtt, err = read_regs(p, UNIT, 0x04, 0x0000, 2, 0.4)
            print("  8%s%d -> %s" % (parity, stop,
                                     "OK %s" % regs if not err else err))
            p.close()
        except Exception as exc:                       # noqa: BLE001
            print("  8%s%d -> 打开失败 %s" % (parity, stop, exc))

    print("\n=== 9. 站号唯一性（1~16 全扫，FC04 x2）===")
    sp = open_port()
    try:
        found = []
        for u in range(1, 17):
            regs, rtt, err = read_regs(sp, u, 0x04, 0x0000, 2, 0.25)
            if not err:
                found.append((u, regs))
                print("  unit=%-3d OK %s" % (u, regs))
        print("  应答站号: %s" % [u for u, _ in found])
    finally:
        sp.close()

    print("\n=== 10. 单帧 RTT 明细（新连接首帧 vs 稳态）===")
    sp = open_port()
    try:
        for i in range(5):
            regs, rtt, err = read_regs(sp, UNIT, 0x04, 0x0000, 2, TMO)
            print("  稳态第%d帧 rtt=%.2fms %s" % (i + 1, rtt,
                                                  regs if not err else err))
    finally:
        sp.close()


if __name__ == "__main__":
    main()
