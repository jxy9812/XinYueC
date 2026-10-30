#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""Modbus RTU 往返时延测量（pyserial 直连，绕开 XinYueC 框架）。

用途：判定 485 链路上「请求→应答」的时延到底来自 USB 串口驱动
（CH343 PollingPeriod / 系统时钟量化），还是来自应用层。

用法:
    python verify/mb_rtt_latency.py --port COM17 --baud 115200 --unit 1
"""
import argparse
import struct
import time
import statistics


def crc16(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc


def build(unit: int, func: int, addr: int, count: int) -> bytes:
    body = struct.pack(">BBH", unit, func, addr) + struct.pack(">H", count)
    return body + struct.pack("<H", crc16(body))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--port', default='COM17')
    ap.add_argument('--baud', type=int, default=115200)
    ap.add_argument('--unit', type=int, default=1)
    ap.add_argument('--func', type=int, default=3)
    ap.add_argument('--addr', type=int, default=0)
    ap.add_argument('--count', type=int, default=1)
    ap.add_argument('--n', type=int, default=200)
    ap.add_argument('--gap-ms', type=float, default=50.0)
    args = ap.parse_args()

    import serial
    port = serial.Serial(args.port, args.baud, timeout=1.0, write_timeout=1.0)
    frame = build(args.unit, args.func, args.addr, args.count)
    port.reset_input_buffer()
    dts = []
    errs = 0
    for i in range(args.n):
        t0 = time.perf_counter()
        port.write(frame)
        port.flush()
        head = port.read(3)
        if len(head) < 3:
            errs += 1
            continue
        n = head[2]
        need = 3 + n + 2
        rest = port.read(need - len(head)) if need > len(head) else b''
        if len(rest) < need - len(head):
            errs += 1
            continue
        t1 = time.perf_counter()
        dts.append((t1 - t0) * 1000.0)
        time.sleep(args.gap_ms / 1000.0)
    port.close()
    if not dts:
        print('no reply (errors=%d) — 从站地址/功能码可能不对' % errs)
        return
    s = sorted(dts)
    print('RTT(ms) n=%d err=%d  p50=%.2f p90=%.2f p99=%.2f max=%.2f' % (
        len(dts), errs, statistics.median(dts), s[int(len(s) * .9)],
        s[int(len(s) * .99)], s[-1]))
    from collections import Counter
    c = Counter(round(d) for d in dts)
    print('取样分布:', c.most_common(12))
    q = sum(1 for d in dts if abs(d / 15.625 - round(d / 15.625)) < 0.08)
    print('为 15.625ms 整数倍的比例: %.1f%%' % (100.0 * q / len(dts)))


if __name__ == '__main__':
    main()
