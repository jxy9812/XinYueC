#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""浊度仪字节序判定专项（板端，只读）。

目的：站2（ZHEYI-5100ZDY）的 float32 字节序与站1（WZT-701A）不同。
      本脚本对同一组寄存器打印 4 种字节序的解，用「面板显示值」判真伪。

四种口径（以寄存器 0x0000=A、0x0001=B 表示，线上字节为 A 的高/低、B 的高/低）：
  ABCD  高字在前、字内高字节在前   → 标准大端 Modbus
  CDAB  低字在前、字内高字节在前   → 站1 WZT-701A 用的就是这个
  BADC  高字在前、字内低字节在前
  DCBA  低字在前、字内低字节在前   → 等价于「小端 float32」，站2 疑似

用法：
    PYTHONPATH=/root/pyserv python3 zheyi_probe.py --unit 2
"""
import argparse
import struct
import sys
import time

try:
    import serial
except ImportError:
    sys.path.insert(0, "/root/pyserv")
    import serial


def crc16(data):
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc


def build(unit, fc, addr, count):
    body = bytes([unit, fc]) + addr.to_bytes(2, "big") + count.to_bytes(2, "big")
    c = crc16(body)
    return body + bytes([c & 0xFF, (c >> 8) & 0xFF])


def read_exact(port, want, timeout):
    buf = bytearray()
    t0 = time.time()
    while len(buf) < want and (time.time() - t0) < timeout:
        chunk = port.read(want - len(buf))
        if chunk:
            buf += chunk
    return bytes(buf)


def transact(port, unit, fc, addr, count, timeout=0.5):
    port.reset_input_buffer()
    port.write(build(unit, fc, addr, count))
    port.flush()
    hdr = read_exact(port, 3, timeout)
    if len(hdr) < 3:
        return b""
    rest = read_exact(port, (2 if hdr[1] & 0x80 else hdr[2] + 2), timeout)
    return hdr + rest


def orders(b4):
    """四种字节序解 float32（b4 = 线上的 4 个字节，按顺序）。"""
    return {
        "ABCD": struct.unpack(">f", b4)[0],
        "CDAB": struct.unpack(">f", b4[2:4] + b4[0:2])[0],
        "BADC": struct.unpack(">f", bytes([b4[1], b4[0], b4[3], b4[2]]))[0],
        "DCBA": struct.unpack(">f", b4[::-1])[0],
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="/dev/ttyS4")
    ap.add_argument("--baud", type=int, default=9600)
    ap.add_argument("--unit", type=int, default=2)
    ap.add_argument("--rounds", type=int, default=5)
    ap.add_argument("--interval", type=float, default=1.0)
    args = ap.parse_args()

    port = serial.Serial(args.port, args.baud, timeout=0.02, write_timeout=1.0)
    try:
        port.reset_input_buffer()
        time.sleep(0.2)
        port.reset_input_buffer()

        print(f"=== 站{args.unit} 字节序判定：读 0x0000×4（浊度 + 温度） ===")
        print(f"    {'轮':<3}{'原始字节':<22}{'ABCD':>13}{'CDAB':>13}{'BADC':>13}{'DCBA':>13}")

        for r in range(args.rounds):
            f = transact(port, args.unit, 0x03, 0x0000, 4)
            if len(f) < 13:
                print(f"    {r+1:<3}读失败 RSP[{len(f)}]")
                time.sleep(args.interval)
                continue
            b = f[3:11]
            d1 = orders(b[0:4])
            d2 = orders(b[4:8])
            raw = " ".join(f"{x:02X}" for x in b)
            print(f"    {r+1:<3}{raw:<22}"
                  + "".join(f"{d1[k]:>13.5g}" for k in ("ABCD", "CDAB", "BADC", "DCBA")))
            print(f"       {'(上=0x0000/1)':<20}"
                  + "".join(f"{d2[k]:>13.5g}" for k in ("ABCD", "CDAB", "BADC", "DCBA"))
                  + "   (上=0x0002/3)")
            time.sleep(args.interval)

        print(f"\n=== 站{args.unit} 块读 0x0000×24（一次拿全，避免拼接错位） ===")
        f = transact(port, args.unit, 0x03, 0x0000, 24)
        if len(f) >= 3 + 48 + 2:
            regs = struct.unpack(">24H", f[3:51])
            print(f"    原始 24 寄存器: {[f'{v:04X}' for v in regs]}")
            print(f"\n    {'地址':<9}{'原始字':<9}{'DCBA_f32(本对)':>18}{'说明'}")
            for i in range(0, 24, 2):
                pair = struct.pack(">2H", regs[i], regs[i + 1])
                d = orders(pair)
                print(f"    0x{i:04X}/0x{i+1:04X}  0x{regs[i]:04X},0x{regs[i+1]:04X}"
                      f"{d['DCBA']:>18.6g}   {d['CDAB']:.6g}(CDAB)")
        else:
            print(f"    失败 RSP[{len(f)}]")

        print(f"\n=== 站{args.unit} 单读 0x0000~0x0013（逐点，核对块读一致性） ===")
        for addr in range(0x00, 0x14):
            f = transact(port, args.unit, 0x03, addr, 1)
            if f and not (f[1] & 0x80) and f[2] >= 2:
                v = struct.unpack(">H", f[3:5])[0]
                print(f"    0x{addr:04X} = 0x{v:04X} ({v})")
            else:
                print(f"    0x{addr:04X} → {'异常' if f else '无应答'}")
            time.sleep(0.01)
    finally:
        port.close()
        print("\n=== 完成（串口已释放） ===")


if __name__ == "__main__":
    main()
