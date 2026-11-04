#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""连续采样验证：确认一帧读取稳定、数据随时间变化、空管报警状态。

用法: python -I ldstream.py [--port COM18] [--baud 9600] [--addr 5] [--seconds 20]
"""
import argparse
import struct
import sys
import time

import serial


def crc16(data: bytes) -> bytes:
    c = 0xFFFF
    for b in data:
        c ^= b
        for _ in range(8):
            c = (c >> 1) ^ 0xA001 if c & 1 else c >> 1
    return bytes([c & 0xFF, c >> 8])


def build_fc04(slave, addr, count):
    body = struct.pack('>BBHH', slave, 0x04, addr, count)
    return body + crc16(body)


def read_block(port, slave, timeout=1.0):
    frame = build_fc04(slave, 0x1010, 22)
    port.reset_input_buffer()
    port.write(frame)
    port.flush()
    deadline = time.time() + timeout
    buf = bytearray()
    while time.time() < deadline:
        chunk = port.read(port.in_waiting or 1)
        if chunk:
            buf += chunk
            if len(buf) >= 5 + 44:
                break
    resp = bytes(buf)
    if len(resp) < 5 or crc16(resp[:-2]) != resp[-2:]:
        return None
    if resp[1] & 0x80:
        return None
    n = resp[2] // 2
    return [struct.unpack('>H', resp[3 + 2 * i:5 + 2 * i])[0] for i in range(n)]


def f32(hi, lo):
    return struct.unpack('>f', struct.pack('>HH', hi, lo))[0]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--port', default='COM18')
    ap.add_argument('--baud', type=int, default=9600)
    ap.add_argument('--addr', type=int, default=5)
    ap.add_argument('--seconds', type=float, default=20.0)
    args = ap.parse_args()

    port = serial.Serial(args.port, args.baud, bytesize=8, parity='N',
                         stopbits=1, timeout=0.2)
    print('连续采样 %s@%d 地址%d，%.0f 秒…\n' % (args.port, args.baud, args.addr, args.seconds))
    print('%8s %10s %12s %10s %10s %12s %6s' %
          ('t(s)', '流量', '流速', '百分比', '电导比', '正向累积', '报警'))
    print('-' * 84)

    t0 = time.time()
    ok = 0
    fail = 0
    last = None
    changed = False
    while time.time() - t0 < args.seconds:
        regs = read_block(port, args.addr)
        el = time.time() - t0
        if regs is None or len(regs) < 22:
            fail += 1
            print('%8.2f %10s' % (el, '读取失败'))
            time.sleep(0.2)
            continue
        ok += 1
        flow = f32(regs[0], regs[1])
        speed = f32(regs[2], regs[3])
        pct = f32(regs[4], regs[5])
        cond = f32(regs[6], regs[7])
        total = ((regs[8] << 16) | regs[9]) + f32(regs[10], regs[11])
        alarm = '空管' if regs[20] else '-'
        cur = (flow, speed, total)
        if last is not None and cur != last:
            changed = True
        last = cur
        print('%8.2f %10.4f %12.6f %10.2f %10.1f %12.4f %6s' %
              (el, flow, speed, pct, cond, total, alarm))
        time.sleep(0.5)

    print('-' * 84)
    print('读取成功 %d 次，失败 %d 次，数据是否有变化: %s' %
          (ok, fail, '是' if changed else '否（静止/空管）'))
    port.close()
    return 0


if __name__ == '__main__':
    sys.exit(main())
