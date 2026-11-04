# -*- coding: utf-8 -*-
"""485 串口现场诊断工具（板端跑）。

    python3 tools/serial_probe.py                       # 用缺省配置扫
    python3 tools/serial_probe.py --port /dev/ttyS3 --baud 115200
    python3 tools/serial_probe.py --units 1,2,3,4,5,6,7,8 --baud 9600,115200

它回答三个问题：
  1. **串口层正常吗** —— 能否打开、配置、写入（不看有没有应答）
  2. **总线上有从站在应答吗** —— 按站号 × 波特率 × 功能码组合扫描
  3. **某个具体设备通吗** —— 对命中的从站打印原始帧

[为什么需要它] 「设备采不到数据」的原因分布在四层：串口被占 / 串口坏 /
从站地址波特率不符 / 物理线未接。逐层排除比盲改代码快得多。
本工具一次把前三层都测掉；若全无应答，基本可断定物理线或从站电源问题。
"""

import argparse
import struct
import sys


def crc16(data):
    """Modbus CRC16（多项式 0xA001）。"""
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc


def build_request(unit, function, address, count):
    body = bytes([unit, function, address >> 8, address & 0xFF,
                  count >> 8, count & 0xFF])
    return body + struct.pack("<H", crc16(body))


def open_port(port, baud):
    """打开串口；失败返回 (None, 原因)。"""
    try:
        import serial
    except ImportError:
        return None, "未安装 pyserial（apt-get install python3-serial）"
    try:
        return serial.Serial(port, baud, timeout=0.3, write_timeout=1.0,
                             exclusive=True), ""
    except Exception as exc:                            # noqa: BLE001
        return None, "%s: %s" % (type(exc).__name__, exc)


def check_port_layer(port, baud):
    """第 1 层：串口能不能用（与总线是否有人应答无关）。"""
    print("=== 1. 串口层 ===")
    ser, err = open_port(port, baud)
    if ser is None:
        print("  [FAIL] %s 打不开：%s" % (port, err))
        print("         常见原因：被别的进程占用（fuser -v %s）、"
              "设备节点不存在、权限不足" % port)
        return False
    try:
        print("  [OK] 已打开 %s（%d 8%s%d）"
              % (ser.port, ser.baudrate,
                 "N" if ser.parity == "N" else ser.parity,
                 ser.stopbits))
        ser.write(b"\x00" * 8)
        ser.flush()
        print("  [OK] 可写入（8 字节无异常）")
        return True
    except Exception as exc:                            # noqa: BLE001
        print("  [FAIL] 写入失败：%s" % exc)
        return False
    finally:
        ser.close()


def scan_bus(port, baud_list, units, verbose=False):
    """第 2/3 层：扫描总线上有哪些从站在应答。

    :returns: [(unit, baud, function, address, response_hex), ...]
    """
    print("\n=== 2. 总线扫描（站号 × 波特率 × 功能码）===")
    hits = []
    # 每站试三种读法：M512 状态字、温湿度输入寄存器、保持寄存器 0
    probes = ((3, 0x3000, 1), (4, 0x0000, 2), (3, 0x0000, 1))
    for baud in baud_list:
        ser, err = open_port(port, baud)
        if ser is None:
            print("  @%d 打开失败：%s" % (baud, err))
            continue
        found_at_baud = 0
        try:
            for unit in units:
                for function, address, count in probes:
                    request = build_request(unit, function, address, count)
                    try:
                        ser.reset_input_buffer()
                        ser.write(request)
                        ser.flush()
                        response = ser.read(64)
                    except Exception as exc:            # noqa: BLE001
                        print("  @%d unit=%d 读写出错：%s" % (baud, unit, exc))
                        response = b""
                    if response:
                        found_at_baud += 1
                        hits.append((unit, baud, function, address,
                                     response.hex(" ")))
                        print("  [HIT] @%d unit=%-3d fc=%d addr=0x%04X -> %s"
                              % (baud, unit, function, address,
                                 response.hex(" ")))
                    elif verbose:
                        print("        @%d unit=%-3d fc=%d addr=0x%04X 无应答"
                              % (baud, unit, function, address))
        finally:
            ser.close()
        if found_at_baud == 0:
            print("  @%d：站号 %s 全部无应答"
                  % (baud, ",".join(str(u) for u in units)))
    return hits


def main(argv=None):
    ap = argparse.ArgumentParser(description="485 串口现场诊断")
    ap.add_argument("--port", default="/dev/ttyS3")
    ap.add_argument("--baud", default="115200",
                    help="一个或多个波特率，逗号分隔（如 115200,9600）")
    ap.add_argument("--units", default="1,2,3,4,5",
                    help="要扫的从站号，逗号分隔")
    ap.add_argument("--verbose", action="store_true", help="打印每个探测")
    args = ap.parse_args(argv)

    try:
        baud_list = [int(b) for b in args.baud.split(",") if b.strip()]
        units = [int(u) for u in args.units.split(",") if u.strip()]
    except ValueError:
        print("--baud / --units 需要逗号分隔的整数", file=sys.stderr)
        return 2
    if not baud_list or not units:
        print("--baud / --units 不能为空", file=sys.stderr)
        return 2

    print("485 诊断：%s 波特率 %s 站号 %s\n"
          % (args.port, baud_list, units))

    port_ok = check_port_layer(args.port, baud_list[0])
    if not port_ok:
        print("\n=== 结论 ===\n串口层就不通，先解决占用/权限/设备节点问题。")
        return 1

    hits = scan_bus(args.port, baud_list, units, args.verbose)

    print("\n=== 结论 ===")
    if hits:
        pairs = sorted({(h[0], h[1]) for h in hits})
        print("发现 %d 个可应答的从站：" % len(pairs))
        for unit, baud in pairs:
            print("  从站 %d @ %d bps" % (unit, baud))
        print("\n把这些站号/波特率填进 cfg/hsf_server_py.json 的 serial.unit_ids"
              " 与 baudrate 即可。")
        return 0
    print("串口层正常（能打开/配置/写入），但总线上**没有任何从站应答**。")
    print("按可能性排查：")
    print("  1. 485 的 A/B 线没接到板子 ttyS3 的 485 端子（最常见）")
    print("  2. 从站没有上电")
    print("  3. 从站地址/波特率与扫描范围不符（用 --units/--baud 扩大范围）")
    print("  4. 从站要求 8E1/8O1 之类的校验（本工具固定 8N1）")
    print("\n注：RS485 半双工不会自发自收，所以「发出去收不到」本身是正常的；")
    print("    判断链路只能看有没有**从站**应答。")
    return 1


if __name__ == "__main__":
    sys.exit(main())
