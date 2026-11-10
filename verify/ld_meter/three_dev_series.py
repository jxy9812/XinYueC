#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""三设备时间序列采样（板端，只读）——用于文档里的「有什么数据」实证。

采样口径（各按真机正确字节序）：
  站1 WZT-701A      FC03 0x0000×2  CDAB float32            = 浊度 NTU
  站2 ZHEYI-5100ZDY FC03 0x0000×4  DCBA float32            = 浊度 NTU + 温度 ℃
  站5 LD 流量计      FC04 0x1010×22 大端（高字在前 ABCD）   = 22 个变量

用法：
    PYTHONPATH=/root/pyserv python3 three_dev_series.py --seconds 40 --interval 2
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


def transact(port, unit, fc, addr, count, timeout=0.6):
    port.reset_input_buffer()
    port.write(build(unit, fc, addr, count))
    port.flush()
    hdr = read_exact(port, 3, timeout)
    if len(hdr) < 3:
        return b""
    rest = read_exact(port, (2 if hdr[1] & 0x80 else hdr[2] + 2), timeout)
    return hdr + rest


def f32_cdab(b4):
    return struct.unpack(">f", b4[2:4] + b4[0:2])[0]


def f32_dcba(b4):
    return struct.unpack(">f", b4[::-1])[0]


def f32_abcd(hi, lo):
    return struct.unpack(">f", struct.pack(">I", ((hi & 0xFFFF) << 16) | (lo & 0xFFFF)))[0]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="/dev/ttyS4")
    ap.add_argument("--baud", type=int, default=9600)
    ap.add_argument("--seconds", type=int, default=40)
    ap.add_argument("--interval", type=float, default=2.0)
    args = ap.parse_args()

    port = serial.Serial(args.port, args.baud, timeout=0.02, write_timeout=1.0)
    try:
        port.reset_input_buffer()
        time.sleep(0.2)
        port.reset_input_buffer()

        print(f"# 采样 {args.seconds}s / 每 {args.interval}s 一轮，只读")
        print(f"# 时间      站1浊度(NTU,CDAB)  站2浊度(NTU,DCBA)  站2温度(℃)  站5流量(m³/h)  站5电导比  空管")
        t_end = time.time() + args.seconds
        rows = []
        while time.time() < t_end:
            ts = time.strftime("%H:%M:%S")

            # 站1 浊度
            t1 = float("nan")
            f = transact(port, 1, 0x03, 0x0000, 2)
            if len(f) >= 9:
                t1 = f32_cdab(f[3:7])
            time.sleep(0.20)      # 实测：站1 在长帧后需 ≥100ms；站间统一留 200ms

            # 站2 浊度 + 温度（连续 4 个寄存器，同一帧快照）
            t2 = temp2 = float("nan")
            f = transact(port, 2, 0x03, 0x0000, 4)
            if len(f) >= 13:
                t2 = f32_dcba(f[3:7])
                temp2 = f32_dcba(f[7:11])
            time.sleep(0.05)

            # 站5 流量计 22 变量
            flow = cond = float("nan")
            empty = "-"
            f = transact(port, 5, 0x04, 0x1010, 22)
            if len(f) >= 49:
                regs = struct.unpack(">22H", f[3:47])
                flow = f32_abcd(regs[0], regs[1])
                cond = f32_abcd(regs[6], regs[7])
                empty = "是" if regs[20] else "否"
            time.sleep(0.20)

            def fmt(v, n=4):
                return "  读取失败  " if v != v else f"{v:<{n+6}.{n}g}"

            print(f"{ts}  {fmt(t1):<16}{fmt(t2):<18}{fmt(temp2,3):<12}"
                  f"{fmt(flow):<14}{fmt(cond,1):<10}{empty}")
            rows.append((ts, t1, t2, temp2, flow, cond))
            time.sleep(max(0.0, args.interval - 0.55))

        ok1 = sum(1 for r in rows if r[1] == r[1])
        ok2 = sum(1 for r in rows if r[2] == r[2])
        ok5 = sum(1 for r in rows if r[4] == r[4])
        n = len(rows)
        print(f"\n# 采集轮次 {n}：站1 {ok1}/{n}  站2 {ok2}/{n}  站5 {ok5}/{n}")
        for idx, name, unit in ((1, "站1浊度", "NTU"), (2, "站2浊度", "NTU"),
                                (3, "站2温度", "℃"), (4, "站5流量", "m³/h")):
            vals = [r[idx] for r in rows if r[idx] == r[idx]]
            if vals:
                print(f"# {name:<8} 最小 {min(vals):.6g}  最大 {max(vals):.6g}  "
                      f"均值 {sum(vals)/len(vals):.6g}  {unit}")
    finally:
        port.close()
        print("\n=== 完成（串口已释放） ===")


if __name__ == "__main__":
    main()
