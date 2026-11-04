#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""参数区与鲁棒性验证：
  1) FC03 读参数区（地址/波特率/管径/单位/校验模式等关键项）
  2) 连读上限探测（一次最多能读多少个寄存器）
  3) FC03 交叉访问变量区（验证文档说的"变量区只吃 FC04、参数区只吃 FC03"）
  4) 不同波特率扫描（确认当前设置与仪表菜单参数 3 的对应关系）

用法: python -I ldparam.py [--port COM18] [--baud 9600] [--addr 5]
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
    """发一帧等完整应答（长度由字节数字段推出，或遇异常帧按 5 字节）。"""
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
            if buf[1] & 0x80:
                want = 5                     # 异常帧: addr fc code crc crc
            else:
                want = 3 + buf[2] + 2        # addr fc bytes data crc16
        if want is not None and len(buf) >= want:
            break
    return bytes(buf)


def parse(resp, want):
    """返回 (regs, err)。"""
    if len(resp) < 5:
        return None, '无应答'
    if crc16(resp[:-2]) != resp[-2:]:
        return None, 'CRC不符: ' + resp.hex(' ').upper()
    if resp[1] & 0x80:
        return None, '异常应答 fc=0x%02X code=0x%02X' % (resp[1], resp[2])
    n = resp[2] // 2
    if len(resp) < 3 + resp[2] + 2:
        return None, '长度不足'
    return [struct.unpack('>H', resp[3 + 2 * i:5 + 2 * i])[0] for i in range(n)], None


BAUD_CODES = {0: 300, 1: 600, 2: 1200, 3: 2400, 4: 4800,
              5: 9600, 6: 14400, 7: 19200, 8: 38400}
PARITY_CODES = {0: '无校验 停1', 1: '奇校验 停1', 2: '偶校验 停1',
                3: '无校验 停2', 4: '奇校验 停2', 5: '偶校验 停2'}

PARAM_NAMES = {
    0x0000: ('语言', '中文/英文'),
    0x0001: ('仪表通讯地址', '1~99'),
    0x0002: ('仪表通讯速度', '查表 4-3'),
    0x0003: ('测量管道口径', '查表 4-4'),
    0x0006: ('流量单位', '查表 4-6'),
    0x0007: ('测量阻尼时间', '查表 4-7'),
    0x000A: ('流量积算单位', '查表 4-9'),
    0x000C: ('电流输出类型', '4mA / 4-20mA'),
    0x0012: ('频率输出范围', ''),
    0x001F: ('出厂标定系数', ''),
    0x0026: ('励磁方式选择', '方式1/2/3'),
    0x002F: ('通讯校验模式', '查表 4-24'),
}


def fmt_param(addr, val):
    name, hint = PARAM_NAMES.get(addr, ('', ''))
    extra = ''
    if addr == 0x0002:
        extra = ' → %s bps' % BAUD_CODES.get(val, '未知')
    elif addr == 0x002F:
        extra = ' → %s' % PARITY_CODES.get(val, '未知')
    return '0x%04X %-12s = %-6d%s' % (addr, name, val, extra)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--port', default='COM18')
    ap.add_argument('--baud', type=int, default=9600)
    ap.add_argument('--addr', type=int, default=5)
    a = ap.parse_args()

    print('=' * 70)
    print('参数区读取（FC03）  %s @ %d  地址 %d' % (a.port, a.baud, a.addr))
    print('=' * 70)
    port = serial.Serial(a.port, a.baud, bytesize=8, parity='N',
                         stopbits=1, timeout=0.2)
    try:
        for addr in (0x0000, 0x0001, 0x0002, 0x0003, 0x0006,
                     0x0007, 0x000A, 0x000C, 0x0012, 0x001F,
                     0x0026, 0x002F):
            resp = transact(port, build(a.addr, 0x03, addr, 1))
            regs, err = parse(resp, 1)
            if regs:
                print('  ' + fmt_param(addr, regs[0]))
            else:
                print('  0x%04X 读取失败: %s  [%s]' %
                      (addr, err, resp.hex(' ').upper() if resp else ''))

        print('\n' + '=' * 70)
        print('连读上限探测（FC04 从 0x1010 起读 N 个）')
        print('=' * 70)
        for n in (2, 8, 16, 22, 23, 30, 64, 100, 125):
            resp = transact(port, build(a.addr, 0x04, 0x1010, n), timeout=1.5)
            regs, err = parse(resp, n)
            if regs:
                print('  N=%-3d OK  (%d 个寄存器)' % (n, len(regs)))
            else:
                print('  N=%-3d 失败: %s' % (n, err))

        print('\n' + '=' * 70)
        print('交叉访问验证（文档称变量区只吃 FC04、参数区只吃 FC03）')
        print('=' * 70)
        resp = transact(port, build(a.addr, 0x03, 0x1010, 2))
        regs, err = parse(resp, 2)
        print('  FC03 读变量区 0x1010: %s' %
              ('成功 %s' % [hex(x) for x in regs] if regs else '拒绝 → ' + err))

        resp = transact(port, build(a.addr, 0x04, 0x0001, 1))
        regs, err = parse(resp, 1)
        print('  FC04 读参数区 0x0001: %s' %
              ('成功 %s' % [hex(x) for x in regs] if regs else '拒绝 → ' + err))

        print('\n' + '=' * 70)
        print('从站地址全扫 1..20')
        print('=' * 70)
        found = []
        for slave in range(1, 21):
            resp = transact(port, build(slave, 0x04, 0x1010, 2), timeout=0.3)
            regs, err = parse(resp, 2)
            if regs:
                flow = struct.unpack('>f', struct.pack('>HH', regs[0], regs[1]))[0]
                print('  地址 %-2d 有响应  瞬时流量=%.4f' % (slave, flow))
                found.append(slave)
        print('  共发现 %d 个从站: %s' % (len(found), found))

        print('\n' + '=' * 70)
        print('波特率扫描（在地址 %d 上，逐个波特率试读一次）' % a.addr)
        print('=' * 70)
        for baud in (1200, 2400, 4800, 9600, 19200, 38400):
            try:
                tmp = serial.Serial(a.port, baud, bytesize=8, parity='N',
                                    stopbits=1, timeout=0.3)
            except Exception as e:
                print('  %-6d 打不开: %s' % (baud, e))
                continue
            resp = transact(tmp, build(a.addr, 0x04, 0x1010, 2), timeout=0.4)
            regs, err = parse(resp, 2)
            print('  %-6d %s' % (baud, '有响应 ✓' if regs else '无响应（%s）' % err))
            tmp.close()
        return 0
    finally:
        port.close()


if __name__ == '__main__':
    sys.exit(main())
