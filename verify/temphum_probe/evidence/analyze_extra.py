#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""离线分析：异常帧余量字节是否为「另一台主机应答」的完整合法帧。"""
import struct
import sys

sys.path.insert(0, __import__("os").path.dirname(
    __import__("os").path.dirname(__import__("os").path.abspath(__file__))))
from probe_temphum import crc16, build

EXTRAS = ["028e8001000351e7", "028d8001000315e7", "028e8001000351e7"]
MAINS = ["04040c00d5028e41ab1edb4282d172b569",
         "04040c00d5028d41ab1edb4282c812aa21",
         "04040c00d5028e41ab03834282cfe29ee5"]


def scan(buf: bytes):
    """滑动窗口找 CRC 正确的合法帧（任意从站地址）。"""
    hits = []
    for off in range(len(buf) - 4):
        for ln in range(5, len(buf) - off + 1):
            cand = buf[off:off + ln]
            crc = struct.unpack("<H", cand[-2:])[0]
            if crc16(cand[:-2]) == crc:
                hits.append((off, cand))
    return hits


print("=== 余量字节的帧结构分析 ===")
for h in EXTRAS:
    b = bytes.fromhex(h)
    print("\n余量 %s (len=%d)" % (h, len(b)))
    print("  按字节: %s" % " ".join("%02X" % x for x in b))
    print("  crc16(全-2) = 0x%04X, 帧尾 = 0x%04X -> %s"
          % (crc16(b[:-2]), struct.unpack("<H", b[-2:])[0],
             "整帧 CRC 正确" if crc16(b[:-2]) == struct.unpack("<H", b[-2:])[0]
             else "整帧 CRC 不匹配"))
    # 假设它是被截断的应答：尝试补全为 FC03/FC04 应答
    for fc in (0x03, 0x04, 0x06):
        for unit in range(1, 8):
            for n in range(1, 8):
                cand = bytes([unit, fc, n * 2]) + b
                want = unit, fc, n * 2, n * 2 + 5
                if len(cand) == want[3]:
                    if crc16(cand[:-2]) == struct.unpack("<H", cand[-2:])[0]:
                        print("  补全为 unit=%d FC%02X count=%d 合法" %
                              (unit, fc, n))
    hits = scan(b)
    print("  内部合法帧: %s" % ([(o, f.hex()) for o, f in hits] or "无"))

print("\n\n=== 主帧 + 余量 拼接后的重同步 ===")
for m, e in zip(MAINS, EXTRAS):
    full = bytes.fromhex(m + e)
    hits = scan(full)
    print("\n拼接 %s" % full.hex())
    for o, f in hits[:6]:
        print("  off=%-3d %s  (unit=%d fc=0x%02X bc=%d)"
              % (o, f.hex(), f[0], f[1], f[2] if f[1] in (3, 4) else -1))

print("\n\n=== 关键：余量里的 028E / 8001 / 0003 是否与 0000H~0005H 内容吻合 ===")
print("  典型输入寄存器: T=0x00D5(213) H=0x028E(654) "
      "Tf=0x41AB/0x1EDB Hf=0x4282/0xD172")
print("  余量含 0x028E (= 湿度 u16) 与 0x0003，像是另一帧应答的中段")
print("\n=== 我的请求帧（对照）===")
print("  FC04 0000H x6 = %s" % build(4, 0x04, 0x0000, 6).hex())
print("  FC03 0000H x5 = %s" % build(4, 0x03, 0x0000, 5).hex())
