#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""Modbus RTU 主站全场景联调器。

用途：对被测从站（板端 demo 从站 / modbus_rtu_slave_sim.py）做覆盖式
轮询，把各种「情况」都试一遍：

  正常路径   FC01/02/03/04 读、FC05/06 写单、FC0F/10 写多、广播(地址0)
  异常路径   越界地址(0x02)、非法数量(0x03)、非法功能码(0x01)、
             不存在的从站地址（应无应答）
  脏数据     前导垃圾字节、半帧+合法帧拼接、CRC 错帧
  时序       连续快速轮询（背靠背）、大数量读（125 寄存器）

用法:
    python verify/modbus_rtu_master_sweep.py --port COM17 --unit 1
    python verify/modbus_rtu_master_sweep.py --port COM17 --reg-max 16

输出：每场景 PASS/FAIL 一行 + 末尾汇总，退出码=失败场景数。
依赖: pyserial。
"""
import argparse
import struct
import sys
import time

PASS, FAIL = 0, 1
results = []


def crc16(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc


def build(unit, fc, payload: bytes) -> bytes:
    body = bytes([unit, fc]) + payload
    return body + struct.pack("<H", crc16(body))


def read_frame(sp, timeout=1.5):
    deadline = time.time() + timeout
    buf = b""
    while time.time() < deadline:
        chunk = sp.read(256)
        if chunk:
            buf += chunk
        elif buf:
            break                       # 静默期=帧结束
        else:
            time.sleep(0.005)
    return buf


def check(name, ok, note=""):
    results.append((name, ok, note))
    print("  [%s] %s%s" % ("PASS" if ok else "FAIL", name,
                           ("  " + note) if note else ""))
    return PASS if ok else FAIL


class Master:
    def __init__(self, sp):
        self.sp = sp

    def rt(self, unit, fc, payload, timeout=1.5, pre_garbage=b""):
        self.sp.reset_input_buffer()
        if pre_garbage:
            self.sp.write(pre_garbage)
            self.sp.flush()
            time.sleep(0.05)
        self.sp.write(build(unit, fc, payload))
        self.sp.flush()
        return read_frame(self.sp, timeout)

    @staticmethod
    def valid(resp) -> bool:
        return (len(resp) >= 4 and
                crc16(resp[:-2]) == struct.unpack("<H", resp[-2:])[0])

    def expect(self, name, resp, unit=None, fc=None, exc=None, payload=None):
        if not resp:
            return check(name, False, "无应答")
        if not self.valid(resp):
            return check(name, False, "CRC错 %s" % resp.hex())
        r_unit, r_fc = resp[0], resp[1]
        if exc is not None:
            ok = (r_fc == (fc | 0x80) and r_unit == unit and resp[2] == exc)
            return check(name, ok, "%s (异常码=%02x)" % (resp.hex(), resp[2]))
        ok = r_unit == unit and r_fc == fc
        if payload is not None:
            ok = ok and resp[2:-2] == payload
        return check(name, ok, resp.hex())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", required=True)
    ap.add_argument("--baud", type=int, default=9600)
    ap.add_argument("--unit", type=int, default=1, help="被测从站地址")
    ap.add_argument("--ghost", type=int, default=9,
                    help="总线上不存在的从站地址（无应答验证）")
    ap.add_argument("--reg-max", type=int, default=16,
                    help="从站保持寄存器区大小（越界测试用）")
    ap.add_argument("--timeout", type=float, default=1.5)
    args = ap.parse_args()

    try:
        import serial
    except ImportError:
        print("缺少 pyserial：pip install pyserial", file=sys.stderr)
        return 2

    try:
        sp = serial.Serial(args.port, args.baud, bytesize=8, parity="N",
                           stopbits=1, timeout=0.05, write_timeout=1.0)
    except Exception as exc:  # noqa: BLE001
        print("打开 %s 失败: %s" % (args.port, exc), file=sys.stderr)
        return 3

    m = Master(sp)
    u = args.unit
    tmo = args.timeout

    print("== 正常路径 ==")
    # FC06 写 -> FC03 读回（多值）
    m.expect("FC06 写 HR0=0x1234", m.rt(u, 6, struct.pack(">HH", 0, 0x1234)),
             unit=u, fc=6, payload=struct.pack(">HH", 0, 0x1234))
    r = m.rt(u, 3, struct.pack(">HH", 0, 2))
    ok = m.valid(r) and r[1] == 3 and r[2] == 4 and \
        struct.unpack(">H", r[3:5])[0] == 0x1234
    check("FC03 读回 HR0=0x1234", ok, r.hex() if r else "无应答")

    # FC05 写线圈 -> FC01 读回（只判 bit0：其余位是历史写入的合法残留）
    m.expect("FC05 写线圈0=ON", m.rt(u, 5, struct.pack(">HH", 0, 0xFF00)),
             unit=u, fc=5, payload=struct.pack(">HH", 0, 0xFF00))
    r = m.rt(u, 1, struct.pack(">HH", 0, 8))
    ok = m.valid(r) and r[1] == 1 and r[2] == 1 and (r[3] & 0x01) == 0x01
    check("FC01 读回线圈0=ON", ok, r.hex() if r else "无应答")

    # FC0F 写多线圈 -> FC01 读回
    bits = bytes([0b10100101])
    m.expect("FC0F 写线圈0..7=0xA5",
             m.rt(u, 0x0F, struct.pack(">HH", 0, 8) + bytes([1]) + bits),
             unit=u, fc=0x0F, payload=struct.pack(">HH", 0, 8))
    r = m.rt(u, 1, struct.pack(">HH", 0, 8))
    ok = m.valid(r) and r[1] == 1 and r[3] == 0xA5
    check("FC01 读回线圈=0xA5", ok, r.hex() if r else "无应答")

    # FC10 写多寄存器 -> FC03 读回
    vals = struct.pack(">4H", 11, 22, 33, 44)
    m.expect("FC10 写 HR4..7",
             m.rt(u, 0x10, struct.pack(">HH", 4, 4) + bytes([8]) + vals),
             unit=u, fc=0x10, payload=struct.pack(">HH", 4, 4))
    r = m.rt(u, 3, struct.pack(">HH", 4, 4))
    ok = m.valid(r) and r[1] == 3 and r[2] == 8 and r[3:-2] == vals
    check("FC03 读回 HR4..7", ok, r.hex() if r else "无应答")

    # FC04 读输入寄存器 / FC02 读离散输入（只读区，值可为 0）
    r = m.rt(u, 4, struct.pack(">HH", 0, 2))
    ok = m.valid(r) and r[1] == 4 and r[2] == 4
    check("FC04 读 IR0..1", ok, r.hex() if r else "无应答")
    r = m.rt(u, 2, struct.pack(">HH", 0, 8))
    ok = m.valid(r) and r[1] == 2 and r[2] == 1
    check("FC02 读 DI0..7", ok, r.hex() if r else "无应答")

    # 大数量读（125 寄存器：协议上限）。从站区小于 125 时预期异常 0x02
    # （非法数据地址），区够大时预期回满 250 字节数据。
    r = m.rt(u, 3, struct.pack(">HH", 0, 125))
    ok = False
    note = r.hex()[:40] if r else "无应答"
    if m.valid(r):
        if r[1] == 3 and r[2] == 250:
            ok = True                       # 区够大：回满
        elif r[1] == (3 | 0x80) and r[2] in (0x02, 0x03):
            ok = True                       # 区不够：异常 0x02/0x03
    check("FC03 读 125 寄存器（协议上限）", ok, note)

    print("== 广播 ==")
    # 地址 0：写后无应答（RTU 规范：广播不回）
    sp.reset_input_buffer()
    sp.write(build(0, 6, struct.pack(">HH", 0, 0x55AA)))
    sp.flush()
    r = read_frame(sp, 1.2)
    check("广播 FC06 写后无应答", len(r) == 0,
          ("意外应答 " + r.hex()) if r else "静默(正确)")
    # 广播写的值应落在数据区（若从站实现了广播写）
    r = m.rt(u, 3, struct.pack(">HH", 0, 1))
    ok = m.valid(r) and r[1] == 3
    check("广播后单播 FC03 仍通", ok, r.hex() if r else "无应答")

    print("== 异常路径 ==")
    m.expect("越界地址 FC03@0xFFFF → 异常0x02",
             m.rt(u, 3, struct.pack(">HH", 0xFFFF, 1)),
             unit=u, fc=3, exc=0x02)
    m.expect("非法数量 FC03 数量=0 → 异常0x03",
             m.rt(u, 3, struct.pack(">HH", 0, 0)),
             unit=u, fc=3, exc=0x03)
    m.expect("数量超限 FC03 数量=126 → 异常0x03",
             m.rt(u, 3, struct.pack(">HH", 0, 126)),
             unit=u, fc=3, exc=0x03)
    m.expect("非法功能码 FC63 → 异常0x01",
             m.rt(u, 0x63, b""),
             unit=u, fc=0x63, exc=0x01)
    # 不存在的从站：应无应答
    r = m.rt(args.ghost, 3, struct.pack(">HH", 0, 1), timeout=tmo)
    check("地址 %d（不存在）无应答" % args.ghost, len(r) == 0,
          ("意外应答 " + r.hex()) if r else "静默(正确)")

    print("== 脏数据恢复 ==")
    # 前导垃圾 + 合法帧（从站必须滑窗重同步）
    r = m.rt(u, 3, struct.pack(">HH", 0, 1), pre_garbage=b"\x00\x00\x00")
    ok = m.valid(r) and r[1] == 3
    check("前导垃圾后 FC03 仍应答", ok, r.hex() if r else "无应答")
    # 半帧 + 完整帧拼接（从站必须丢弃半帧、解出完整帧）
    sp.reset_input_buffer()
    sp.write(b"\x01\x03\x00")           # 半帧（无 CRC）
    sp.flush()
    time.sleep(0.1)
    sp.write(build(u, 3, struct.pack(">HH", 0, 1)))
    sp.flush()
    r = read_frame(sp, tmo)
    ok = m.valid(r) and len(r) > 0 and r[1] == 3
    check("半帧+完整帧拼接后 FC03 应答", ok, r.hex() if r else "无应答")
    # CRC 错帧：从站应丢弃并不应答
    sp.reset_input_buffer()
    bad = bytearray(build(u, 3, struct.pack(">HH", 0, 1)))
    bad[-1] ^= 0xFF
    sp.write(bytes(bad))
    sp.flush()
    r = read_frame(sp, 1.2)
    check("CRC 错帧被丢弃（无应答）", len(r) == 0,
          ("意外应答 " + r.hex()) if r else "静默(正确)")
    # 脏数据之后正常帧仍通（恢复力）
    r = m.rt(u, 3, struct.pack(">HH", 0, 1))
    ok = m.valid(r) and r[1] == 3
    check("脏数据后 FC03 恢复", ok, r.hex() if r else "无应答")

    print("== 时序压力 ==")
    # 背靠背快速轮询 20 次
    fails = 0
    t0 = time.time()
    for _ in range(20):
        r = m.rt(u, 3, struct.pack(">HH", 0, 2), timeout=tmo)
        if not (m.valid(r) and r[1] == 3):
            fails += 1
    dt = time.time() - t0
    check("背靠背 FC03 x20", fails == 0, "失败%d次, %.1fs(%.0fms/次)"
          % (fails, dt, dt * 1000 / 20))
    # 读 sizes 变化 1..n
    okall = True
    for n in (1, 2, 3, 7, 16):
        r = m.rt(u, 3, struct.pack(">HH", 0, n))
        if not (m.valid(r) and r[1] == 3 and r[2] == n * 2):
            okall = False
            break
    check("FC03 数量 1/2/3/7/16 全通", okall)

    sp.close()
    print("\n== 汇总 ==")
    bad = [r for r in results if not r[1]]
    print("共 %d 场景，PASS %d，FAIL %d"
          % (len(results), len(results) - len(bad), len(bad)))
    for name, _, note in bad:
        print("  FAIL: %s  %s" % (name, note))
    return len(bad)


if __name__ == "__main__":
    sys.exit(main())
