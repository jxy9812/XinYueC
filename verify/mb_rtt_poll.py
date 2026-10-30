#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""Modbus RTU 往返时延：阻塞读 vs 毫秒轮询 对照（同一驱动、同一链路）。

用途：判定 15.625ms 量化来自「驱动数据可见性」还是「应用唤醒节拍」。

用法:
    python verify/mb_rtt_poll.py --port COM17 --baud 115200 --mode poll --period-ms 1
    python verify/mb_rtt_poll.py --port COM17 --baud 115200 --mode block
"""
import argparse
import struct
import time
import statistics
from collections import Counter


def crc16(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc


def build(unit, func, addr, count):
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
    ap.add_argument('--n', type=int, default=150)
    ap.add_argument('--gap-ms', type=float, default=30.0)
    ap.add_argument('--mode', choices=['block', 'poll'], default='block')
    ap.add_argument('--period-ms', type=float, default=1.0)
    args = ap.parse_args()

    import serial
    port = serial.Serial(args.port, args.baud, timeout=1.0, write_timeout=1.0)
    frame = build(args.unit, args.func, args.addr, args.count)
    port.reset_input_buffer()
    dts = []
    errs = 0
    for _ in range(args.n):
        t0 = time.perf_counter()
        port.write(frame)
        port.flush()
        if args.mode == 'block':
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
        else:
            deadline = t0 + 1.0
            got = False
            while time.perf_counter() < deadline:
                if port.in_waiting >= 5:
                    port.read(port.in_waiting)
                    got = True
                    break
                time.sleep(args.period_ms / 1000.0)
            if not got:
                errs += 1
                continue
        dts.append((time.perf_counter() - t0) * 1000.0)
        time.sleep(args.gap_ms / 1000.0)
    port.close()
    if not dts:
        print('no reply (errors=%d)' % errs)
        return
    s = sorted(dts)
    print('[%s] RTT(ms) n=%d err=%d p50=%.2f p90=%.2f p99=%.2f max=%.2f' % (
        args.mode, len(dts), errs, statistics.median(dts), s[int(len(s) * .9)],
        s[int(len(s) * .99)], s[-1]))
    print('  取样分布:', Counter(round(d) for d in dts).most_common(10))
    q = sum(1 for d in dts if abs(d / 15.625 - round(d / 15.625)) < 0.08)
    print('  15.625ms 整数倍比例: %.1f%%' % (100.0 * q / len(dts)))


if __name__ == '__main__':
    main()
