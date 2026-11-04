#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""补充验证：
  1) FC03 与 FC04 对变量区的等价性（读同一批值比对）
  2) 参数区连读上限（FC03）
  3) 参数区越界行为
  4) 波特率扫描（先关主串口再逐个试）

用法: python -I ldextra.py [--port COM18] [--baud 9600] [--addr 5]
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


def build(slave, fc, addr, count):
    body = struct.pack('>BBHH', slave, fc, addr, count)
    return body + crc16(body)


def transact(port, frame, timeout=1.0):
    port.reset_input_buffer()
    port.write(frame)
    port.flush()
    deadline = time.time() + timeout
    buf = bytearray()
    want = None
    while time.time() < deadline:
        chunk = port.read(port.in_waiting or 1)
        if not chunk:
            continue
        buf += chunk
        if want is None and len(buf) >= 3:
            want = 5 if (buf[1] & 0x80) else 3 + buf[2] + 2
        if want is not None and len(buf) >= want:
            break
    return bytes(buf)


def parse(resp):
    if len(resp) < 5:
        return None, '无应答'
    if crc16(resp[:-2]) != resp[-2:]:
        return None, 'CRC不符'
    if resp[1] & 0x80:
        return None, '异常 fc=0x%02X code=0x%02X' % (resp[1], resp[2])
    n = resp[2] // 2
    if len(resp) < 3 + resp[2] + 2:
        return None, '长度不足'
    return [struct.unpack('>H', resp[3 + 2 * i:5 + 2 * i])[0] for i in range(n)], None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--port', default='COM18')
    ap.add_argument('--baud', type=int, default=9600)
    ap.add_argument('--addr', type=int, default=5)
    a = ap.parse_args()

    port = serial.Serial(a.port, a.baud, bytesize=8, parity='N',
                         stopbits=1, timeout=0.2)
    try:
        print('=' * 70)
        print('① FC03 vs FC04 读同一变量区（0x1010 起 22 个）')
        print('=' * 70)
        r04, e04 = parse(transact(port, build(a.addr, 0x04, 0x1010, 22)))
        r03, e03 = parse(transact(port, build(a.addr, 0x03, 0x1010, 22)))
        print('  FC04: %s' % ('OK ' + ' '.join('%04X' % x for x in r04) if r04 else e04))
        print('  FC03: %s' % ('OK ' + ' '.join('%04X' % x for x in r03) if r03 else e03))
        if r04 and r03:
            print('  两者一致: %s' % ('是' if r04 == r03 else '否 ← 差异!'))

        print('\n' + '=' * 70)
        print('② 参数区连读上限（FC03 从 0x0000 起）')
        print('=' * 70)
        for n in (1, 8, 16, 32, 52, 64, 72, 80, 100):
            regs, err = parse(transact(port, build(a.addr, 0x03, 0x0000, n), 1.5))
            print('  N=%-4d %s' % (n, 'OK (%d 个)' % len(regs) if regs else '失败: ' + err))

        print('\n' + '=' * 70)
        print('③ 参数区越界（文档表止于 0x0047）')
        print('=' * 70)
        for addr, cnt in ((0x0040, 1), (0x0047, 1), (0x0048, 1), (0x0100, 1), (0xFFFF, 1)):
            regs, err = parse(transact(port, build(a.addr, 0x03, addr, cnt)))
            print('  0x%04X: %s' % (addr, 'OK = %d' % regs[0] if regs else '拒绝 → ' + err))

        print('\n' + '=' * 70)
        print('④ 变量区越界（文档表止于 0x1025）')
        print('=' * 70)
        for addr, cnt in ((0x1020, 6), (0x1026, 1), (0x1030, 1), (0x1100, 1)):
            regs, err = parse(transact(port, build(a.addr, 0x04, addr, cnt)))
            print('  0x%04X x%d: %s' % (addr, cnt,
                  'OK = ' + ' '.join(str(x) for x in regs) if regs else '拒绝 → ' + err))
    finally:
        port.close()

    print('\n' + '=' * 70)
    print('⑤ 波特率扫描（已释放主串口）')
    print('=' * 70)
    for baud in (1200, 2400, 4800, 9600, 19200, 38400):
        try:
            tmp = serial.Serial(a.port, baud, bytesize=8, parity='N',
                                stopbits=1, timeout=0.3)
        except Exception as e:
            print('  %-6d 打不开: %s' % (baud, e))
            continue
        regs, err = parse(transact(tmp, build(a.addr, 0x04, 0x1010, 2), 0.5))
        print('  %-6d %s' % (baud, '有响应 ' if regs else '无响应'))
        tmp.close()
        time.sleep(0.15)
    return 0


if __name__ == '__main__':
    sys.exit(main())
