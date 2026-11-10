#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""两个实验（独占 ttyS4 时跑）：

实验A：复现并定量「非阻塞读 + 静默阈值」对 9600 长帧的截断。
    hsfsrv 用 pyserial timeout=0（非阻塞）+ RTU_IDLE_MS=3 静默判帧尾，
    流量计 49 字节应答被截成 32 字节。本实验用同一口径扫不同静默阈值，
    看截断是否随阈值放宽而消失 ⇒ 确认根因并给出修复阈值。

实验B：全串口 × 全站号扫描，找那台「另一型号」浊度仪（现场说站号 3，
    但 ttyS4 站号 3 实测全静默，可能接在别的口/别的站号/别的波特率）。
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


def tx(port, u, fc, a, n, idle_ms, timeout=1.2):
    """与 hsfsrv 同口径：非阻塞 read + 静默 idle_ms 判帧尾。"""
    port.reset_input_buffer()
    port.write(build(u, fc, a, n))
    port.flush()
    buf = bytearray()
    last = None
    t0 = time.time()
    while time.time() - t0 < timeout:
        ch = port.read(256)
        now = time.time()
        if ch:
            buf += ch
            last = now
            continue
        if buf and last and (now - last) * 1000.0 >= idle_ms:
            break
    return bytes(buf)


def experiment_a():
    print("=== 实验A：非阻塞读 + 静默阈值 对 49 字节长帧的影响 ===")
    print("   目标：站号5 流量计 FC03 0x1010×22（应答 49 字节）\n")
    for idle in (1.0, 2.0, 3.0, 4.0, 5.0, 8.0, 12.0, 20.0):
        port = serial.Serial("/dev/ttyS4", 9600, timeout=0, write_timeout=1.0)
        lens = {}
        try:
            for _ in range(20):
                f = tx(port, 5, 0x03, 0x1010, 22, idle)
                lens[len(f)] = lens.get(len(f), 0) + 1
                time.sleep(0.02)
        finally:
            port.close()
        full = lens.get(49, 0)
        print(f"  idle={idle:>5.1f}ms → 完整帧 {full}/20   长度分布 {lens}")
    print("\n  对照：9600 的 3.5 字符时间 = 3.5×11/9600 = 4.01ms")
    print("        115200 的 3.5 字符时间 = 0.33ms（原注释按此设 3ms）")


def experiment_b():
    print("\n=== 实验B：全串口 × 全站号 × 多波特率 扫描 ===")
    ports = ["/dev/ttyS3", "/dev/ttyS4", "/dev/ttyS5",
             "/dev/ttyS7", "/dev/ttyS8", "/dev/ttyS9"]
    bauds = (9600, 115200, 19200, 4800)
    for path in ports:
        for baud in bauds:
            try:
                port = serial.Serial(path, baud, timeout=0, write_timeout=1.0)
            except Exception as exc:
                print(f"  {path} @{baud}: 打开失败（{type(exc).__name__}）")
                continue
            hits = []
            try:
                for u in range(1, 9):
                    f = tx(port, u, 0x03, 0x0000, 2, idle_ms=8.0, timeout=0.25)
                    if f:
                        hits.append(f"u{u}/fc03/len{len(f)}"
                                    f"/{' '.join('%02X' % x for x in f)}")
            finally:
                port.close()
            if hits:
                print(f"  {path} @{baud}:")
                for h in hits:
                    print("      ", h)


if __name__ == "__main__":
    which = sys.argv[1] if len(sys.argv) > 1 else "both"
    if which in ("a", "both"):
        experiment_a()
    if which in ("b", "both"):
        experiment_b()
