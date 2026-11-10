#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""站1（WZT-701A）间歇无应答定性（板端，只读）。

现象：与其它站交替访问时，站1 常零应答（超时 0 字节），独占时也有约 1/10 丢失。
本脚本用四组对照把原因二分：
  A 独占连续读，扫描帧间隔
  B 与站5（49 字节长帧）交替，扫描「长帧→站1」的间隔
  C 与站2（9 字节短帧）交替
  D 站1 用不同寄存器数/功能码（看是否与帧长相关）

用法：
    PYTHONPATH=/root/pyserv python3 turb1_flaky_diag.py
"""
import argparse
import struct
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


def transact(port, unit, fc, addr, count, timeout=0.5, flush=True):
    if flush:
        port.reset_input_buffer()
    port.write(build(unit, fc, addr, count))
    port.flush()
    hdr = read_exact(port, 3, timeout)
    if len(hdr) < 3:
        return hdr
    rest = read_exact(port, (2 if hdr[1] & 0x80 else hdr[2] + 2), timeout)
    return hdr + rest


def run_batch(port, tag, seq, rounds, gap_ms):
    """seq = [(unit, fc, addr, count, need), ...]；返回 (成功数, 总数, 各站统计)。"""
    stat = {}
    for _ in range(rounds):
        for unit, fc, addr, count, need in seq:
            f = transact(port, unit, fc, addr, count)
            key = f"站{unit}"
            ok, tot = stat.get(key, (0, 0))
            stat[key] = (ok + (1 if len(f) >= need else 0), tot + 1)
            time.sleep(gap_ms / 1000.0)
    detail = "  ".join(f"{k}:{v[0]}/{v[1]}" for k, v in stat.items())
    print(f"  {tag:<38} {detail}")
    return stat


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="/dev/ttyS4")
    ap.add_argument("--baud", type=int, default=9600)
    ap.add_argument("--rounds", type=int, default=10)
    args = ap.parse_args()

    T1 = (1, 0x03, 0x0000, 2, 9)      # 站1 浊度
    T2 = (2, 0x03, 0x0000, 2, 9)      # 站2 浊度
    F5 = (5, 0x04, 0x1010, 22, 49)    # 站5 流量计

    port = serial.Serial(args.port, args.baud, timeout=0.02, write_timeout=1.0)
    try:
        port.reset_input_buffer()
        time.sleep(0.2)
        port.reset_input_buffer()

        print("=== A. 站1 独占，帧间隔扫描 ===")
        for gap in (0, 10, 20, 50, 100, 200, 500):
            run_batch(port, f"站1 独占, 间隔 {gap}ms ×{args.rounds}",
                      [T1], args.rounds, gap)

        print("\n=== B. 站5(49B) → 站1，间隔扫描 ===")
        for gap in (0, 10, 20, 50, 100, 200):
            run_batch(port, f"站5→站1, 间隔 {gap}ms ×{args.rounds}",
                      [F5, T1], args.rounds, gap)

        print("\n=== C. 站2(9B) → 站1，间隔扫描 ===")
        for gap in (0, 10, 20, 50, 100):
            run_batch(port, f"站2→站1, 间隔 {gap}ms ×{args.rounds}",
                      [T2, T1], args.rounds, gap)

        print("\n=== D. 站1 不同帧长/功能码（各自独占 ×20） ===")
        cases = [("0x0000×1 FC03", 0x03, 0x0000, 1, 7),
                 ("0x0000×2 FC03", 0x03, 0x0000, 2, 9),
                 ("0x0000×4 FC03", 0x03, 0x0000, 4, 13),
                 ("0x0000×8 FC03", 0x03, 0x0000, 8, 21),
                 ("0x0000×2 FC04", 0x04, 0x0000, 2, 9)]
        for tag, fc, addr, count, need in cases:
            ok = 0
            for _ in range(20):
                f = transact(port, 1, fc, addr, count)
                if len(f) >= need:
                    ok += 1
                time.sleep(0.05)
            print(f"  {tag:<22} {ok}/20")

        print("\n=== E. 不 flush 输入缓冲（排除 flush 吃掉应答） ===")
        ok = 0
        for _ in range(20):
            f = transact(port, 1, 0x03, 0x0000, 2, flush=False)
            if len(f) >= 9:
                ok += 1
            time.sleep(0.05)
        print(f"  站1 无 flush          {ok}/20")

        print("\n=== F. 失败帧逐条记录（间隔 20ms，站5→站1 ×20） ===")
        miss = 0
        for i in range(20):
            f5 = transact(port, *F5[:4])
            time.sleep(0.02)
            f1 = transact(port, *T1[:4])
            if len(f1) < 9:
                miss += 1
                print(f"  #{i+1:>2} 站5={len(f5)}B OK  站1 无应答")
        print(f"  >>> 站1 丢失 {miss}/20")
    finally:
        port.close()
        print("\n=== 完成（串口已释放） ===")


if __name__ == "__main__":
    main()
