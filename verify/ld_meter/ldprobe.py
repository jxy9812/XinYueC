#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""LD 系列电磁流量计 Modbus-RTU 探针。

用法:
    python -I ldprobe.py [--port COM18] [--baud 9600] [--addr 1]

输出:
    - 单帧读 0x1010~0x1025 共 22 个寄存器（协议文档 2-1 的完整变量区）
    - 逐项解析：瞬时流量/流速/百分比/电导比/正反向累积（整数+小数）/单位/报警
    - 额外：扫地址 1..9、扫波特率，帮助确认当前仪表设置
"""
import argparse
import struct
import sys
import time

import serial


def crc16(data: bytes) -> bytes:
    """标准 Modbus CRC16（init 0xFFFF, poly 0xA001），低字节先上线。"""
    c = 0xFFFF
    for b in data:
        c ^= b
        for _ in range(8):
            c = (c >> 1) ^ 0xA001 if c & 1 else c >> 1
    return bytes([c & 0xFF, c >> 8])


def build_fc04(slave: int, addr: int, count: int) -> bytes:
    body = struct.pack('>BBHH', slave, 0x04, addr, count)
    return body + crc16(body)


def transact(port: serial.Serial, frame: bytes, timeout: float = 1.0) -> bytes:
    port.reset_input_buffer()
    port.write(frame)
    port.flush()
    deadline = time.time() + timeout
    buf = bytearray()
    expected = 5 + 2 * struct.unpack('>H', frame[4:6])[0]
    while time.time() < deadline:
        chunk = port.read(port.in_waiting or 1)
        if chunk:
            buf += chunk
            if len(buf) >= expected:
                break
    return bytes(buf)


def decode_regs(resp: bytes):
    """校验应答并返回寄存器列表（uint16）。"""
    if len(resp) < 5:
        return None, '应答过短: %s' % resp.hex(' ').upper()
    if crc16(resp[:-2]) != resp[-2:]:
        return None, 'CRC 不符: %s' % resp.hex(' ').upper()
    fc = resp[1]
    if fc & 0x80:
        return None, '异常应答 fc=0x%02X code=0x%02X' % (fc, resp[2])
    byte_count = resp[2]
    if len(resp) < 3 + byte_count + 2:
        return None, '数据长度不足'
    n = byte_count // 2
    regs = [struct.unpack('>H', resp[3 + 2 * i:5 + 2 * i])[0] for i in range(n)]
    return regs, None


def f32(hi: int, lo: int) -> float:
    return struct.unpack('>f', struct.pack('>HH', hi, lo))[0]


FLOW_UNITS = ['L/S', 'L/M', 'L/H', 'M3/S', 'M3/M', 'M3/H',
              'T/S', 'T/M', 'T/H', 'GPS', 'GPM', 'GPH']
TOTAL_UNITS = ['L', 'M3', 'T', 'USG']


def dump_all(regs):
    print('\n===== 变量区 0x1010~0x1025（%d 个寄存器）=====' % len(regs))
    print('原始: ' + ' '.join('%04X' % r for r in regs))

    if len(regs) < 22:
        print('!! 寄存器数不足 22，无法完整解析')
        return

    inst_flow = f32(regs[0], regs[1])
    inst_speed = f32(regs[2], regs[3])
    percent = f32(regs[4], regs[5])
    conduct = f32(regs[6], regs[7])
    fwd_int = (regs[8] << 16) | regs[9]
    fwd_frac = f32(regs[10], regs[11])
    rev_int = (regs[12] << 16) | regs[13]
    rev_frac = f32(regs[14], regs[15])
    flow_unit = regs[16]
    total_unit = regs[17]
    alarms = regs[18:22]

    print('\n--- 物理量 ---')
    print('瞬时流量   %12.4f   [%s]' % (inst_flow, FLOW_UNITS[flow_unit] if flow_unit < 12 else '?'))
    print('瞬时流速   %12.4f   m/s' % inst_speed)
    print('流量百分比 %12.4f   %%' % percent)
    print('流体电导比 %12.4f' % conduct)
    print('正向累积   %12.4f   = 整数 %d + 小数 %.6f' % (fwd_int + fwd_frac, fwd_int, fwd_frac))
    print('反向累积   %12.4f   = 整数 %d + 小数 %.6f' % (rev_int + rev_frac, rev_int, rev_frac))

    print('\n--- 单位 ---')
    print('瞬时流量单位 代码=%d → %s' % (flow_unit, FLOW_UNITS[flow_unit] if flow_unit < 12 else '未知'))
    print('累积总量单位 代码=%d → %s' % (total_unit, TOTAL_UNITS[total_unit] if total_unit < 4
                                         else '代码%d(C型精度档)' % total_unit))

    print('\n--- 报警（1=报警）---')
    names = ['上限报警', '下限报警', '空管报警', '系统报警']
    any_alarm = False
    for name, v in zip(names, alarms):
        if v:
            print('%s  报警' % name)
            any_alarm = True
    if not any_alarm:
        print('无报警')


def scan_address(port, baud):
    print('\n===== 扫从站地址 1..9 (FC04 0x1010 x2) =====')
    for a in range(1, 10):
        r = transact(port, build_fc04(a, 0x1010, 2), timeout=0.4)
        if r:
            regs, err = decode_regs(r)
            if regs:
                print('  地址 %d 有响应：瞬时流量 = %.4f' % (a, f32(regs[0], regs[1])))
            else:
                print('  地址 %d 响应但异常：%s' % (a, err))
        else:
            print('  地址 %d 无响应' % a)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--port', default='COM18')
    ap.add_argument('--baud', type=int, default=9600)
    ap.add_argument('--addr', type=int, default=1)
    ap.add_argument('--scan', action='store_true', help='扫描从站地址 1..9')
    args = ap.parse_args()

    print('打开 %s @ %d 8N1 …' % (args.port, args.baud))
    try:
        port = serial.Serial(args.port, args.baud, bytesize=8, parity='N',
                             stopbits=1, timeout=0.2)
    except Exception as e:
        print('打开串口失败: %s' % e)
        return 2

    try:
        frame = build_fc04(args.addr, 0x1010, 22)
        print('\n请求帧: %s' % frame.hex(' ').upper())
        resp = transact(port, frame, timeout=2.0)
        print('应答帧: %s' % resp.hex(' ').upper() if resp else '应答帧: (无响应)')

        regs, err = decode_regs(resp)
        if regs is None:
            print('\n解析失败: %s' % err)
            print('\n尝试只读 2 个寄存器（排除从站连读上限）…')
            resp2 = transact(port, build_fc04(args.addr, 0x1010, 2), timeout=1.0)
            print('应答帧: %s' % (resp2.hex(' ').upper() if resp2 else '(无响应)'))
            regs2, err2 = decode_regs(resp2)
            if regs2:
                print('小窗口可读，瞬时流量 = %.4f' % f32(regs2[0], regs2[1]))
            else:
                print('小窗口也失败: %s' % err2)
                if args.scan:
                    scan_address(port, args.baud)
                return 1
        else:
            dump_all(regs)

        if args.scan:
            scan_address(port, args.baud)
        return 0
    finally:
        port.close()


if __name__ == '__main__':
    sys.exit(main())
