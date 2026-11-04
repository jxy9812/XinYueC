#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""残尾帧身份：修正 bc 枚举漏洞（上一版漏掉 bc=6），并验字节规律。"""
import struct
import sys

sys.path.insert(0, __import__("os").path.dirname(
    __import__("os").path.dirname(__import__("os").path.abspath(__file__))))
from probe_temphum import crc16

EXTRAS = ['028d8001000315e7', '028c800100032827', '028e8001000351e7']


def crcv(b):
    return struct.unpack("<H", b[-2:])[0]


print("=== 1. 字节规律：第2字节 是否 == 第1字节的低位 ===")
for h in EXTRAS:
    b = bytes.fromhex(h)
    print("  %s  b0=0x%02X b1=0x%02X -> b1==b0? %s   "
          "作为 u16: b0b1=%d  b2b3=%d  b4b5=%d"
          % (h, b[0], b[1], b[1] == b[0],
             struct.unpack(">H", b[0:2])[0],
             struct.unpack(">H", b[2:4])[0],
             struct.unpack(">H", b[4:6])[0]))

print("\n=== 2. 残尾作为「某条应答的最后 8 字节」：穷举 bc（含 bc=6）===")
hits = []
for h in EXTRAS:
    b = bytes.fromhex(h)
    body6, crcb = b[:6], b[6:8]
    for unit in range(1, 9):
        for fc in (0x03, 0x04):
            for bc in range(2, 250, 2):
                ndata = bc
                # 帧长 = 3 + bc + 2；残尾占最后 8 字节 ⇒ 前缀长 = bc - 3
                pre_len = bc - 3
                if pre_len < 1:
                    continue
                # 前缀 = unit fc bc + (bc-6) 个数据字节（未知，枚举全 0 不可行）
                # 仅当 bc==6 时前缀恰好是 unit fc bc，残尾6字节全是数据 → 可直接验
                if bc == 6:
                    frame = bytes([unit, fc, bc]) + body6 + crcb
                    if len(frame) == 11 and crc16(frame[:-2]) == crcv(frame):
                        hits.append((h, unit, fc, bc, frame.hex()))
                # 另一类：残尾 = 完整帧（bc 恰为残尾前 6 字节对应）
print("  bc=6 精确命中 %d 条" % len(hits))
for x in hits[:20]:
    print("   %s -> unit=%d FC%02X bc=%d 完整帧=%s" % x)

print("\n=== 3. 残尾本身作为完整帧的字段解释 ===")
for h in EXTRAS:
    b = bytes.fromhex(h)
    print("  %s  [addr=0x%02X][fc=0x%02X][data=%s][crc=%s]"
          % (h, b[0], b[1], b[2:6].hex(), b[6:8].hex()))
    print("     若按 Modbus 异常帧(5B) 解析: 数据应为 1 字节异常码，此处 4 字节 → 不是")

print("\n=== 4. 「寄存器序列」视角：3 个寄存器 = 湿度u16 / 0x8001 / 0x0003 ===")
for h in EXTRAS:
    b = bytes.fromhex(h)
    r = struct.unpack(">HHH", b[:6])
    print("  %s -> [%d, 0x%04X, %d]  (0x%04X=%d)"
          % (h, r[0], r[1], r[2], r[1], r[1]))
print("  0x8001=32769 恒定；0x0003=3 恒定；第一个随测量变化")

print("\n=== 5. 假设：这是本机【另一条请求】的应答尾（前一帧被截断）===")
print("  检验法：把 body6 当作 3 个寄存器，与本机 FC04 0000H~0002H 的内容比对")
print("  本机实测 FC04 0000H x6: 温度u16=00D5 湿度u16=028D "
      "温度f32=41AB 1EDB 湿度f32=4282 xxxx")
print("  残尾第1寄存器 0x028D == 湿度u16 ✓")
print("  残尾第2寄存器 0x8001 != 0x41AB ✗ ⇒ 不是本模块测量寄存器序列")

print("\n=== 6. 是否可能是【另一个从站地址 2】的独立帧？ ===")
for h in EXTRAS:
    b = bytes.fromhex(h)
    print("  %s 若 addr=0x02，则 func=0x%02X —— 但该字节等于 addr 的低位，" % (h, b[1]))
    print("     说明这是「值」而非「功能码」，即该帧不是标准 Modbus 帧")
