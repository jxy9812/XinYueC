#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""离线定性：残尾 8 字节是哪条更长帧的尾部？（穷举 unit/fc/bc 前缀）"""
import struct
import sys

sys.path.insert(0, __import__("os").path.dirname(
    __import__("os").path.dirname(__import__("os").path.abspath(__file__))))
from probe_temphum import crc16

RES = ['028d8001000315e7', '028c800100032827', '028e8001000351e7']


def crc_le(b):
    return struct.unpack("<H", b[-2:])[0]


print("=== 假设1：残尾是 FC03/FC04 应答的【后 8 字节】===")
print("  帧型 = unit fc bc <data...> crc，残尾 = 最后8字节")
print("  - 前缀 = unit fc bc + (bc-6) 字节数据；bc=6 时前缀恰为 unit fc bc")
hits = []
for h in RES:
    b = bytes.fromhex(h)
    data6, crcb = b[:6], b[6:8]
    for unit in range(0, 256):
        for fc in range(0x01, 0x100):
            frame = bytes([unit, fc, 0x06]) + data6 + crcb
            if crc16(frame[:-2]) == crc_le(frame):
                hits.append(("bc=6", h, unit, fc, frame.hex()))
            # bc=8: 帧 = unit fc 08 <2字节数据> + data6 + crc
            for pre2 in range(0, 0x10000, 1):
                pass
            break
print("  bc=6 精确命中 %d 条" % len(hits))
for x in hits[:10]:
    print("   ", x)

print("\n=== 假设2：残尾是 FC03/FC04 应答的【后 8 字节】，bc=8（多 2 字节前缀数据）===")
hits2 = []
for h in RES:
    b = bytes.fromhex(h)
    data6, crcb = b[:6], b[6:8]
    for unit in range(0, 256):
        for fc in (0x03, 0x04):
            for hi in range(0x100):
                for lo in range(0x100):
                    frame = bytes([unit, fc, 0x08, hi, lo]) + data6 + crcb
                    if crc16(frame[:-2]) == crc_le(frame):
                        hits2.append((h, unit, fc, hi, lo, frame.hex()))
print("  命中 %d 条" % len(hits2))
for x in hits2[:10]:
    print("   ", x)

print("\n=== 假设3：残尾本身就是完整帧（unit, fc, 4字节数据, crc）===")
for h in RES:
    b = bytes.fromhex(h)
    print("  %s -> unit=%d fc=0x%02X data=%s CRC自洽=%s"
          % (h, b[0], b[1], b[2:6].hex(), crc16(b[:-2]) == crc_le(b)))
print("  字节规律: b0b1 = u16 值(652~654，与湿度 u16 同量级)；b2b3=0x8001 恒定；"
      "b4b5=0x0003 恒定")
print("  - 这 6 字节是【3 个寄存器】：[湿度u16, 0x8001, 0x0003]")

print("\n=== 假设4：0x8001 是寄存器【地址】- 另一条请求的地址/数量字段 ===")
print("  若解释为 Modbus 请求尾部: addr=0x8001, cnt=0x0003")
print("  即某个主站发了 FC03/FC04 读 0x8001H 起 3 个寄存器")
print("  且请求 CRC 恰好也覆盖了 addr/cnt -- 概率 1/65536，故不是巧合")
