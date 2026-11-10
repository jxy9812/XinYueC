#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""逐次 read 的到达时序探针：定位「49 字节只收到 32 字节」的丢字节机制。

对比三种读法（同一请求、同一设备）：
  A 非阻塞(timeout=0) + 静默判定        ← hsfsrv 现状
  B 非阻塞(timeout=0) + 不设静默，只按长度收满  ← 候选修法
  C 阻塞(timeout=0.02)                  ← 手工探测能收全的读法

输出每次 read 返回的 (毫秒时间戳, 字节数)，让「交付间隙」和「丢失」一目了然。
"""
import sys
import time

sys.path.insert(0, "/root/pyserv")
import serial  # noqa: E402


def crc16(d):
    c = 0xFFFF
    for b in d:
        c ^= b
        for _ in range(8):
            c = (c >> 1) ^ 0xA001 if c & 1 else c >> 1
    return c


def build(u, fc, a, n):
    body = bytes([u, fc]) + a.to_bytes(2, "big") + n.to_bytes(2, "big")
    c = crc16(body)
    return body + bytes([c & 0xFF, (c >> 8) & 0xFF])


def timeline(port, unit, fc, addr, count, mode, need, total_timeout=2.0):
    port.reset_input_buffer()
    t0 = time.time()
    port.write(build(unit, fc, addr, count))
    port.flush()
    buf = bytearray()
    ev = []
    idle_ms = 3.0
    while time.time() - t0 < total_timeout:
        ch = port.read(256)
        now = time.time()
        el = (now - t0) * 1000.0
        if ch:
            buf += ch
            ev.append((round(el, 1), len(ch)))
            if mode == "B" and len(buf) >= need:
                break
            if mode == "C" and len(buf) >= need:
                break
            continue
        ev.append((round(el, 1), 0))
        if mode == "A" and buf and el - ev[len(ev) - 2][0] >= idle_ms:
            break
        if mode == "B" and buf and len(buf) >= need:
            break
        time.sleep(0.0002)
    return bytes(buf), ev


def run(mode, serial_timeout, unit, fc, addr, count, need, rounds=6):
    port = serial.Serial("/dev/ttyS4", 9600, timeout=serial_timeout,
                          write_timeout=1.0)
    try:
        for i in range(rounds):
            frame, ev = timeline(port, unit, fc, addr, count, mode, need)
            ok = "OK " if len(frame) >= need else "BAD"
            evs = " ".join(f"{t}ms:{n}" for t, n in ev[:14])
            print(f"  [{ok}] #{i+1} len={len(frame):3d}/{need}  {evs}")
            time.sleep(0.05)
    finally:
        port.close()


if __name__ == "__main__":
    print("=== A：非阻塞(timeout=0) + 3ms 静默判定（hsfsrv 现状）===")
    run("A", 0, 5, 0x03, 0x1010, 22, 49)
    print("\n=== B：非阻塞(timeout=0) + 不设静默，按长度收满 ===")
    run("B", 0, 5, 0x03, 0x1010, 22, 49)
    print("\n=== C：阻塞(timeout=0.02)（手工探测读法）===")
    run("C", 0.02, 5, 0x03, 0x1010, 22, 49)
