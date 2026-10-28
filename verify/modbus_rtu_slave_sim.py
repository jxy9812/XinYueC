#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""Modbus RTU 从站模拟器（联调用）。

用途：把本机（或板子）当 Modbus RTU **从站**，让 XinYueC 的 Modbus 主站
（demo 主站页 / ATV61 面板）去连它，从而验证主站侧的收发与轮询。

支持功能码：FC01 读线圈、FC02 读离散输入、FC03 读保持寄存器、
FC04 读输入寄存器、FC05 写单线圈、FC06 写单寄存器、FC0F 写多线圈、
FC10 写多寄存器；另对未支持的功能码回异常码 0x01（非法功能码）、
越界地址回 0x02（非法数据地址）。

故障注入（--fault）：
    none      正常
    drop      收到合法帧但不回（模拟从站无响应）
    garbage   收到合法帧后先回一串垃圾字节再回正常应答
    slow      应答前延迟 --slow-ms 毫秒
    badaddr   只处理 --unit 指定地址，其他地址不应答
    wrongfc   把收到的功能码改成 0x63 回（触发主站侧异常解析路径）

用法:
    python verify/modbus_rtu_slave_sim.py --port COM17 --unit 1
    python verify/modbus_rtu_slave_sim.py --port COM17 --coils 8 --holding 16
    python verify/modbus_rtu_slave_sim.py --port COM17 --fault slow --slow-ms 300

依赖: pyserial（pip install pyserial）。
"""
import argparse
import struct
import sys
import threading
import time

# ---- 异常码 ----
EX_ILLEGAL_FUNCTION = 0x01
EX_ILLEGAL_ADDRESS = 0x02
EX_ILLEGAL_VALUE = 0x03
EX_DEVICE_FAILURE = 0x04


def crc16(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc


def frame_crc_ok(frame: bytes) -> bool:
    if len(frame) < 4:
        return False
    return crc16(frame[:-2]) == struct.unpack("<H", frame[-2:])[0]


class SlaveSim:
    """Modbus RTU 从站：线圈/离散输入/保持寄存器/输入寄存器四区。"""

    def __init__(self, unit, coils, discrete, holding, inputs,
                 fault="none", slow_ms=0):
        self.unit = unit
        self.coils = coils            # 可写线圈（DO）
        self.discrete = discrete      # 只读离散输入（DI）
        self.holding = holding        # 可写保持寄存器（HR）
        self.inputs = inputs          # 只读输入寄存器（IR）
        self.fault = fault
        self.slow_ms = slow_ms
        self.stats = {"rx": 0, "tx": 0, "err": 0, "drop": 0}

    # ---------- 各区取值 ----------
    def _bits(self, table, start, qty):
        out = []
        for i in range(start, start + qty):
            out.append(table[i] if 0 <= i < len(table) else 0)
        return out

    def _regs(self, table, start, qty):
        out = []
        for i in range(start, start + qty):
            out.append(table[i] if 0 <= i < len(table) else 0)
        return out

    @staticmethod
    def _pack_bits(bits):
        data = bytearray((len(bits) + 7) // 8)
        for i, b in enumerate(bits):
            if b:
                data[i >> 3] |= 1 << (i & 7)
        return bytes(data)

    @staticmethod
    def _pack_regs(regs):
        return b"".join(struct.pack(">H", r & 0xFFFF) for r in regs)

    # ---------- 请求处理 ----------
    def handle(self, req: bytes):
        """返回应答字节（None = 不应答）。"""
        addr, fc = req[0], req[1]
        data = req[2:-2]

        if self.fault == "badaddr" and addr != self.unit:
            return None
        if addr != self.unit and addr != 0:
            return None                      # 非本机地址：静默丢弃

        if fc in (0x01, 0x02):
            if len(data) < 4:
                return self._exc(addr, fc, EX_ILLEGAL_VALUE)
            start, qty = struct.unpack(">HH", data[:4])
            table = self.coils if fc == 0x01 else self.discrete
            if qty < 1 or qty > 2000:
                return self._exc(addr, fc, EX_ILLEGAL_VALUE)
            if start + qty > len(table):
                return self._exc(addr, fc, EX_ILLEGAL_ADDRESS)
            payload = self._pack_bits(self._bits(table, start, qty))
            return self._resp(addr, fc, bytes([len(payload)]) + payload)

        if fc in (0x03, 0x04):
            if len(data) < 4:
                return self._exc(addr, fc, EX_ILLEGAL_VALUE)
            start, qty = struct.unpack(">HH", data[:4])
            table = self.holding if fc == 0x03 else self.inputs
            if qty < 1 or qty > 125:
                return self._exc(addr, fc, EX_ILLEGAL_VALUE)
            if start + qty > len(table):
                return self._exc(addr, fc, EX_ILLEGAL_ADDRESS)
            payload = self._pack_regs(self._regs(table, start, qty))
            return self._resp(addr, fc, bytes([len(payload)]) + payload)

        if fc == 0x05:                      # 写单线圈
            if len(data) < 4:
                return self._exc(addr, fc, EX_ILLEGAL_VALUE)
            start, val = struct.unpack(">HH", data[:4])
            if start >= len(self.coils):
                return self._exc(addr, fc, EX_ILLEGAL_ADDRESS)
            if val not in (0x0000, 0xFF00):
                return self._exc(addr, fc, EX_ILLEGAL_VALUE)
            self.coils[start] = 1 if val else 0
            return self._resp(addr, fc, data[:4])   # 回显

        if fc == 0x06:                      # 写单寄存器
            if len(data) < 4:
                return self._exc(addr, fc, EX_ILLEGAL_VALUE)
            start, val = struct.unpack(">HH", data[:4])
            if start >= len(self.holding):
                return self._exc(addr, fc, EX_ILLEGAL_ADDRESS)
            self.holding[start] = val
            return self._resp(addr, fc, data[:4])   # 回显

        if fc == 0x0F:                      # 写多线圈
            if len(data) < 5:
                return self._exc(addr, fc, EX_ILLEGAL_VALUE)
            start, qty = struct.unpack(">HH", data[:4])
            nbytes = data[4]
            if qty < 1 or qty > 1968 or nbytes != (qty + 7) // 8:
                return self._exc(addr, fc, EX_ILLEGAL_VALUE)
            if start + qty > len(self.coils):
                return self._exc(addr, fc, EX_ILLEGAL_ADDRESS)
            for i in range(qty):
                bit = (data[5 + (i >> 3)] >> (i & 7)) & 1
                self.coils[start + i] = bit
            return self._resp(addr, fc, struct.pack(">HH", start, qty))

        if fc == 0x10:                      # 写多寄存器
            if len(data) < 5:
                return self._exc(addr, fc, EX_ILLEGAL_VALUE)
            start, qty = struct.unpack(">HH", data[:4])
            nbytes = data[4]
            if qty < 1 or qty > 123 or nbytes != qty * 2:
                return self._exc(addr, fc, EX_ILLEGAL_VALUE)
            if start + qty > len(self.holding):
                return self._exc(addr, fc, EX_ILLEGAL_ADDRESS)
            for i in range(qty):
                self.holding[start + i] = struct.unpack(
                    ">H", data[5 + i * 2:7 + i * 2])[0]
            return self._resp(addr, fc, struct.pack(">HH", start, qty))

        return self._exc(addr, fc, EX_ILLEGAL_FUNCTION)

    def _resp(self, addr, fc, payload):
        body = bytes([addr, fc]) + payload
        return body + struct.pack("<H", crc16(body))

    def _exc(self, addr, fc, code):
        body = bytes([addr, fc | 0x80, code])
        return body + struct.pack("<H", crc16(body))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", required=True)
    ap.add_argument("--baud", type=int, default=9600)
    ap.add_argument("--unit", type=int, default=1)
    ap.add_argument("--coils", type=int, default=8)
    ap.add_argument("--discrete", type=int, default=8)
    ap.add_argument("--holding", type=int, default=16)
    ap.add_argument("--inputs", type=int, default=16)
    ap.add_argument("--fault", default="none",
                    choices=["none", "drop", "garbage", "slow", "badaddr",
                             "wrongfc"])
    ap.add_argument("--slow-ms", type=int, default=0)
    ap.add_argument("--seconds", type=float, default=0.0,
                    help="运行时长（0=常驻）")
    args = ap.parse_args()

    try:
        import serial
    except ImportError:
        print("缺少 pyserial：pip install pyserial", file=sys.stderr)
        return 2

    sim = SlaveSim(args.unit, [0] * args.coils, [0] * args.discrete,
                   [0] * args.holding, [0] * args.inputs,
                   args.fault, args.slow_ms)

    try:
        sp = serial.Serial(args.port, args.baud, bytesize=8, parity="N",
                           stopbits=1, timeout=0.05, write_timeout=1.0)
    except Exception as exc:  # noqa: BLE001
        print("打开 %s 失败: %s" % (args.port, exc), file=sys.stderr)
        return 3

    print("从站模拟器 %s @%d 8N1 unit=%d fault=%s（线圈%d/离散%d/保持%d/输入%d）"
          % (args.port, args.baud, args.unit, args.fault, args.coils,
             args.discrete, args.holding, args.inputs))
    buf = bytearray()
    deadline = time.time() + args.seconds if args.seconds else None

    try:
        while deadline is None or time.time() < deadline:
            chunk = sp.read(256)
            if chunk:
                buf.extend(chunk)
                # 按「缓冲里能解析出合法帧」切帧
                while len(buf) >= 4:
                    fc = buf[1]
                    if fc in (0x01, 0x02, 0x03, 0x04, 0x05, 0x06):
                        need = 8
                    elif fc in (0x0F, 0x10):
                        # addr1+fc1+start2+qty2+nbytes1+data+CRC2=9+nbytes
                        # nbytes 在 buf[6]（0..3=addr/fc/start，4..5=qty）
                        if len(buf) < 7:
                            break
                        need = 9 + buf[6]
                    else:
                        # 未知功能码：先按最短帧(4)试 CRC，不行丢首字节滑窗
                        if frame_crc_ok(bytes(buf[:4])):
                            need = 4
                        else:
                            del buf[0]
                            continue
                    if len(buf) < need:
                        break
                    frame = bytes(buf[:need])
                    if not frame_crc_ok(frame):
                        del buf[0]
                        continue
                    del buf[:need]
                    sim.stats["rx"] += 1
                    resp = sim.handle(frame)
                    if resp is None:
                        sim.stats["drop"] += 1
                        print("  收到 %s -> 不应答" % frame.hex())
                        continue
                    if frame[0] == 0:
                        # 广播：执行但不回（RTU 规范）
                        print("  广播 %s -> 执行不回" % frame.hex())
                        continue
                    if args.fault == "drop":
                        sim.stats["drop"] += 1
                        print("  收到 %s -> 丢弃(fault=drop)" % frame.hex())
                        continue
                    if args.fault == "wrongfc":
                        resp = bytes([resp[0], 0x63]) + resp[2:]
                    if args.fault == "garbage":
                        sp.write(b"\x00\x00\x00")
                        time.sleep(0.02)
                    if args.fault == "slow":
                        time.sleep(args.slow_ms / 1000.0)
                    sp.write(resp)
                    sim.stats["tx"] += 1
                    print("  收到 %s -> 应答 %s" % (frame.hex(), resp.hex()))
                continue
            time.sleep(0.005)
    except KeyboardInterrupt:
        pass
    finally:
        sp.close()
    print("统计: %s" % sim.stats)
    return 0


if __name__ == "__main__":
    sys.exit(main())
