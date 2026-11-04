#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""离线钉死「残尾帧」的真实身份。

对 2000 帧主动轮询捕获的 11 个残尾样本，穷举所有可能的 Modbus RTU 帧解释：
  A. 残尾本身是完整帧（CRC 自洽）→ 是哪个功能码？
  B. 残尾是某条更长应答的尾部 → 找出被截掉的前缀
  C. 残尾是某个请求帧的片段
"""
import struct
import sys

sys.path.insert(0, __import__("os").path.dirname(
    __import__("os").path.dirname(__import__("os").path.abspath(__file__))))
from probe_temphum import crc16

EXTRAS = [
    '028d8001000315e7', '028d8001000315e7', '028c800100032827',
    '028d8001000315e7', '028d8001000315e7', '028d8001000315e7',
    '028c800100032827', '028d8001000315e7', '028c800100032827',
    '028c800100032827', '028c800100032827',
]


def crcb(b):
    return struct.unpack("<H", b[-2:])[0]


print("=== A. 残尾自身是否 CRC 自洽？各字段是什么？===")
for h in EXTRAS:
    b = bytes.fromhex(h)
    print("  %s  addr=%d func=0x%02X data=%s crc=%s  自洽=%s"
          % (h, b[0], b[1], b[2:6].hex(), b[6:8].hex(),
             crc16(b[:-2]) == crcb(b)))

print("\n=== B. 残尾首 2 字节是否等于当时的湿度 u16？ ===")
# 已知同期实测湿度 u16 在 652~654 之间
for h in EXTRAS[:3]:
    b = bytes.fromhex(h)
    print("  %s -> 首2字节 = %d (湿度 u16 量级 652~654)" % (h, struct.unpack(">H", b[:2])[0]))

print("\n=== C. 穷举：残尾能否是某条完整应答的【后 8 字节】？===")
# 候选应答：FC03/FC04，addr∈{1..8}，count∈{1..12}
hits = []
for h in EXTRAS[:3]:
    b = bytes.fromhex(h)
    tail6 = b[:6]              # 假设这 6 字节是数据尾部
    for unit in range(1, 9):
        for fc in (0x03, 0x04):
            for n in range(1, 13):
                for bc in range(2 * n - 5, 2 * n + 1, 2):
                    if bc <= 0 or bc > 0xFA:
                        continue
                    frame = bytes([unit, fc, bc]) + tail6 + b[6:8]
                    if len(frame) != 3 + bc + 2:
                        continue
                    if crc16(frame[:-2]) == struct.unpack("<H", frame[-2:])[0]:
                        hits.append((h, unit, fc, n, bc, frame.hex()))
print("  命中 %d 条" % len(hits))
for x in hits[:20]:
    print("   %s -> unit=%d FC%02X count=%d bc=%d 完整帧=%s" % x)

print("\n=== D. 穷举：残尾能否是【请求帧】的尾部（说明从站回显了？）===")
hits2 = []
for h in EXTRAS[:3]:
    b = bytes.fromhex(h)
    for unit in range(0, 9):
        for fc in (0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x0F, 0x10):
            for addr in range(0x0000, 0x0020):
                for cnt in range(0, 0x0010):
                    frame = bytes([unit, fc]) + struct.pack(">HH", addr, cnt) + b[6:8]
                    if len(frame) != 8:
                        continue
                    if crc16(frame[:-2]) == struct.unpack("<H", frame[-2:])[0]:
                        hits2.append((h, unit, fc, addr, cnt, frame.hex()))
print("  命中 %d 条" % len(hits2))
for x in hits2[:20]:
    print("   %s -> unit=%d FC%02X addr=0x%04X cnt=%d 帧=%s" % x)

print("\n=== E. 残尾的功能码字节 0x8D/0x8C/0x8E 是什么？ ===")
for h in EXTRAS[:3]:
    b = bytes.fromhex(h)
    fc = b[1]
    print("  %s func=0x%02X  -> 若视为异常响应(0x80|原FC): 原FC=0x%02X"
          % (h, fc, fc & 0x7F))
print("  0x8D&0x7F=0x0D(读FIFO队列) 0x8C&0x7F=0x0C(读串口统计) 0x8E&0x7F=0x0E(读通讯事件计数)")
print("  这些都是非常规功能码，且数据段 4 字节不像异常码(应为1字节)")

print("\n=== F. 数据段 0x8001 0x0003 的含义试探 ===")
print("  若视为 FC03/FC04 起始地址+数量: addr=0x8001 cnt=3")
print("  若视为寄存器值: 0x8001=32769, 0x0003=3")
print("  组合 0x028D 0x8001 0x0003 恰好是 6 字节 = 3 个寄存器")

print("\n=== G. 复算：把 6 字节数据当 3 个寄存器，检查是否是本机曾读到的值 ===")
print("  残尾的 3 个寄存器 = %s" %
      [hex(struct.unpack(">H", EXTRAS[0][i:i + 4])[0]) for i in (0, 4, 8)])
print("  本机 FC04 0000H x6 实测: 温度u16=00D5 湿度u16=028D "
      "温度f32=41AB 1EDB 湿度f32=4282 xxxx")
print("  残尾第1个寄存器 028D == 湿度 u16 ✓")
print("  残尾第2个寄存器 8001 —— 与温度 f32 高字 41AB 不符")
