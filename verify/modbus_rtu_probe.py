#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""Modbus RTU 主站探测（联调用）。

用途：板子(192.168.0.10)跑 XinYueC 的 Modbus 从站（串口 RTU），本机
经 USB-485 转换器做主站，逐个轮询从站地址，看谁应答——用于判定板上
哪一路 UART 接了 485。

用法:
    python verify/modbus_rtu_probe.py --port COM17 --baud 9600 --unit 1
    python verify/modbus_rtu_probe.py --port COM17 --unit 1 --units 1,2,3
    python verify/modbus_rtu_probe.py --port COM17 --func 3 --addr 0 --count 4

依赖: pyserial（pip install pyserial）。
"""
import argparse
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


def build_frame(unit: int, func: int, addr: int, count: int) -> bytes:
    # RTU 请求体：从站地址(1) + 功能码(1) + 起始地址(2) + 数量(2)
    body = struct.pack(">BBH", unit, func, addr) + struct.pack(">H", count)
    return body + struct.pack("<H", crc16(body))


def parse_frame(frame: bytes):
    if len(frame) < 3:
        return None, "帧过短: %r" % frame
    unit, func = frame[0], frame[1]
    if func & 0x80:
        return None, "异常响应 func=0x%02X code=0x%02X" % (func, frame[2])
    if func in (0x01, 0x02, 0x03, 0x04):
        if len(frame) < 4:
            return None, "帧过短"
        nbytes = frame[2]
        payload = frame[3:3 + nbytes]
        if func in (0x01, 0x02):
            bits = []
            for byte in payload:
                for i in range(8):
                    bits.append((byte >> i) & 1)
            return {"unit": unit, "func": func, "bits": bits}, None
        regs = list(struct.unpack(">%dH" % (len(payload) // 2), payload))
        return {"unit": unit, "func": func, "regs": regs}, None
    if func in (0x05, 0x06):
        value = struct.unpack(">H", frame[2:4])[0]
        return {"unit": unit, "func": func, "value": value}, None
    return {"unit": unit, "func": func, "raw": frame.hex()}, None


def transact(serial_port, unit, func, addr, count, timeout):
    frame = build_frame(unit, func, addr, count)
    serial_port.reset_input_buffer()
    serial_port.write(frame)
    serial_port.flush()
    deadline = time.time() + timeout
    buf = b""
    while time.time() < deadline:
        chunk = serial_port.read(256)
        if chunk:
            buf += chunk
            # 期望长度：地址1+功能码1+字节数1+数据N+CRC2
            if func in (0x01, 0x02, 0x03, 0x04):
                if len(buf) >= 3:
                    nbytes = buf[2]
                    if len(buf) >= 3 + nbytes + 2:
                        break
            elif len(buf) >= 8:
                break
        else:
            time.sleep(0.01)
    return buf


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", required=True, help="串口，如 COM17")
    ap.add_argument("--baud", type=int, default=9600)
    ap.add_argument("--unit", type=int, default=1, help="从站地址")
    ap.add_argument("--units", default="", help="逗号分隔的多个从站地址")
    ap.add_argument("--func", type=int, default=3, help="功能码（3=读保持寄存器）")
    ap.add_argument("--addr", type=int, default=0, help="起始地址")
    ap.add_argument("--count", type=int, default=4, help="寄存器数量")
    ap.add_argument("--timeout", type=float, default=1.5)
    ap.add_argument("--repeat", type=int, default=1, help="重复次数")
    args = ap.parse_args()

    try:
        import serial  # pyserial
    except ImportError:
        print("缺少 pyserial：pip install pyserial", file=sys.stderr)
        return 2

    units = [int(u) for u in args.units.split(",") if u.strip()] or [args.unit]

    try:
        sp = serial.Serial(
            args.port, args.baud, bytesize=8, parity="N", stopbits=1,
            timeout=0.2, write_timeout=1.0)
    except Exception as exc:  # noqa: BLE001
        print("打开 %s 失败: %s" % (args.port, exc), file=sys.stderr)
        return 3

    print("主站 %s @%d 8N1，从站地址 %s" % (args.port, args.baud, units))
    try:
        for i in range(args.repeat):
            for unit in units:
                frame = transact(sp, unit, args.func, args.addr,
                                 args.count, args.timeout)
                if not frame:
                    print("  unit=%-4d 无应答（超时 %.1fs）" % (unit, args.timeout))
                    continue
                if crc16(frame[:-2]) != struct.unpack("<H", frame[-2:])[0]:
                    print("  unit=%-4d CRC 校验失败: %s" % (unit, frame.hex()))
                    continue
                info, err = parse_frame(frame)
                if err:
                    print("  unit=%-4d %s raw=%s" % (unit, err, frame.hex()))
                    continue
                if "regs" in info:
                    print("  unit=%-4d OK regs=%s" % (unit, info["regs"]))
                elif "bits" in info:
                    print("  unit=%-4d OK bits=%s" % (unit, info["bits"]))
                else:
                    print("  unit=%-4d OK %s" % (unit, info))
            if i + 1 < args.repeat:
                time.sleep(0.3)
    finally:
        sp.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
