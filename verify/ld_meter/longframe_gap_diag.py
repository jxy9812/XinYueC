#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""「长帧后恢复期」方向性测试（板端，只读）。

已知：站5(49B) 之后立刻问站1 → 基本无应答；间隔 ≥200ms 恢复 10/10。
本脚本确认：
  1) 站5 → 站2 是否同样受影响（还是只针对站1）
  2) 站1/站2 → 站5 是否影响流量计
  3) 站5 → 站5 背靠背
  4) 站1 → 站1 背靠背（自发自收是否也有恢复期）
  5) 恢复期精确门槛（0/25/50/75/100/125/150/175/200/250ms）

用法：
    PYTHONPATH=/root/pyserv python3 longframe_gap_diag.py
"""
import argparse
import sys
import time

try:
    import serial
except ImportError:
    sys.path.insert(0, "/root/pyserv")
    import serial


def crc16(data):
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc


def build(unit, fc, addr, count):
    body = bytes([unit, fc]) + addr.to_bytes(2, "big") + count.to_bytes(2, "big")
    c = crc16(body)
    return body + bytes([c & 0xFF, (c >> 8) & 0xFF])


def read_exact(port, want, timeout):
    buf = bytearray()
    t0 = time.time()
    while len(buf) < want and (time.time() - t0) < timeout:
        chunk = port.read(want - len(buf))
        if chunk:
            buf += chunk
    return bytes(buf)


def transact(port, unit, fc, addr, count, timeout=0.6):
    port.reset_input_buffer()
    port.write(build(unit, fc, addr, count))
    port.flush()
    hdr = read_exact(port, 3, timeout)
    if len(hdr) < 3:
        return hdr
    rest = read_exact(port, (2 if hdr[1] & 0x80 else hdr[2] + 2), timeout)
    return hdr + rest


T1 = (1, 0x03, 0x0000, 2, 9)
T2 = (2, 0x03, 0x0000, 2, 9)
F5 = (5, 0x04, 0x1010, 22, 49)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="/dev/ttyS4")
    ap.add_argument("--baud", type=int, default=9600)
    ap.add_argument("--rounds", type=int, default=10)
    args = ap.parse_args()

    port = serial.Serial(args.port, args.baud, timeout=0.02, write_timeout=1.0)
    try:
        port.reset_input_buffer()
        time.sleep(0.2)
        port.reset_input_buffer()

        def pair(a, b, gap_ms, rounds=None):
            rounds = rounds or args.rounds
            ok_b = 0
            ok_a = 0
            for _ in range(rounds):
                fa = transact(port, *a[:4])
                if len(fa) >= a[4]:
                    ok_a += 1
                time.sleep(gap_ms / 1000.0)
                fb = transact(port, *b[:4])
                if len(fb) >= b[4]:
                    ok_b += 1
                time.sleep(0.05)
            return ok_a, ok_b

        print("=== 1. 方向性：A → 间隔 → B，看 B 的成功率 ===")
        tests = [
            ("站5(49B) → 站2(9B)", F5, T2),
            ("站2(9B)  → 站1(9B)", T2, T1),
            ("站1(9B)  → 站2(9B)", T1, T2),
            ("站1(9B)  → 站5(49B)", T1, F5),
            ("站2(9B)  → 站5(49B)", T2, F5),
            ("站5(49B) → 站5(49B)", F5, F5),
            ("站1(9B)  → 站1(9B)", T1, T1),
        ]
        for tag, a, b in tests:
            row = []
            for gap in (0, 50, 200):
                oa, ob = pair(a, b, gap)
                row.append(f"gap{gap}ms: A{oa}/{args.rounds} B{ob}/{args.rounds}")
            print(f"  {tag:<24} " + "   ".join(row))

        print("\n=== 2. 站5 → 站1 恢复期门槛精扫（每档 10 轮） ===")
        for gap in (0, 25, 50, 75, 100, 125, 150, 175, 200, 225, 250, 300):
            _, ok1 = pair(F5, T1, gap, rounds=10)
            bar = "█" * ok1
            print(f"  gap={gap:>3}ms  站1 {ok1:>2}/10  {bar}")

        print("\n=== 3. 站5 → 站2 恢复期门槛精扫（每档 10 轮） ===")
        for gap in (0, 25, 50, 75, 100, 125, 150, 175, 200):
            _, ok2 = pair(F5, T2, gap, rounds=10)
            bar = "█" * ok2
            print(f"  gap={gap:>3}ms  站2 {ok2:>2}/10  {bar}")
    finally:
        port.close()
        print("\n=== 完成（串口已释放） ===")


if __name__ == "__main__":
    main()
