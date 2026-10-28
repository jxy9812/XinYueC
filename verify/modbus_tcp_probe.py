#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""Modbus TCP 主站探测（联调用 / 服务端核心对照实验）。

用途：板子(192.168.0.10)跑 XinYueC 的 Modbus 从站（TCP 通道），本机
做主站读保持寄存器——用于与串口 RTU 路径做对照，判断服务端核心
（processRequest 派发）是否正常。

用法:
    python verify/modbus_tcp_probe.py --host 192.168.0.10 --port 15020 --unit 1
"""
import argparse
import socket
import struct
import sys
import time


def crc16(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc


def build_adu(unit: int, func: int, addr: int, count: int) -> bytes:
    # PDU：功能码(1) + 起始地址(2) + 数量(2)
    pdu = struct.pack(">BHH", func, addr, count)
    mbap = struct.pack(">HHH", unit, len(pdu) + 1, 0x0000)
    return mbap + pdu


def parse_adu(frame: bytes):
    if len(frame) < 9:
        return None, "帧过短: %r" % frame
    unit, length = struct.unpack(">HH", frame[:4])
    pdu = frame[7:7 + length - 1]
    if not pdu:
        return None, "无 PDU"
    func = pdu[0]
    if func & 0x80:
        return None, "异常响应 func=0x%02X code=0x%02X" % (func, pdu[1])
    if func in (0x03, 0x04):
        nbytes = pdu[1]
        payload = pdu[2:2 + nbytes]
        regs = list(struct.unpack(">%dH" % (len(payload) // 2), payload))
        return {"unit": unit, "func": func, "regs": regs}, None
    if func in (0x01, 0x02):
        nbytes = pdu[1]
        bits = []
        for byte in pdu[2:2 + nbytes]:
            for i in range(8):
                bits.append((byte >> i) & 1)
        return {"unit": unit, "func": func, "bits": bits}, None
    return {"unit": unit, "func": func, "raw": pdu.hex()}, None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="192.168.0.10")
    ap.add_argument("--port", type=int, default=15020)
    ap.add_argument("--unit", type=int, default=1)
    ap.add_argument("--func", type=int, default=3)
    ap.add_argument("--addr", type=int, default=0)
    ap.add_argument("--count", type=int, default=4)
    ap.add_argument("--timeout", type=float, default=2.0)
    ap.add_argument("--repeat", type=int, default=1)
    args = ap.parse_args()

    for i in range(args.repeat):
        try:
            with socket.create_connection((args.host, args.port),
                                          timeout=args.timeout) as sock:
                sock.settimeout(args.timeout)
                sock.sendall(build_adu(args.unit, args.func, args.addr,
                                       args.count))
                deadline = time.time() + args.timeout
                buf = b""
                while time.time() < deadline:
                    try:
                        chunk = sock.recv(4096)
                    except socket.timeout:
                        break
                    if not chunk:
                        break
                    buf += chunk
                    if len(buf) >= 9:
                        need = 6 + struct.unpack(">H", buf[4:6])[0]
                        if len(buf) >= need:
                            break
        except Exception as exc:  # noqa: BLE001
            print("连接 %s:%d 失败: %s" % (args.host, args.port, exc))
            return 3

        if not buf:
            print("  无应答（超时）")
            continue
        info, err = parse_adu(buf)
        if err:
            print("  %s raw=%s" % (err, buf.hex()))
            continue
        print("  OK %s" % info)
        time.sleep(0.2)
    return 0


if __name__ == "__main__":
    sys.exit(main())
