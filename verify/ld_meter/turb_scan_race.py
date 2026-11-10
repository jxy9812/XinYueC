#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""浊度仪（站号 1 / 3，另一型号）RS485 扫描 + 并发争抢复现。

两个模式：
  scan : 独占串口，扫多站号 × 多地址 × 多功能码，找出哪台设备在线、
         它的寄存器在哪儿、数据按哪种口径解。
  race : 两个线程各开一个串口实例同时读同一 tty，复现 hsfsrv 里
         「流量计应答被截成 32 字节 / 浊度仪无应答」的现象。

板端用法：
    PYTHONPATH=/root/pyserv python3 turb_scan_race.py --mode scan
    PYTHONPATH=/root/pyserv python3 turb_scan_race.py --mode race
"""
import argparse
import struct
import sys
import threading
import time

sys.path.insert(0, "/root/pyserv")
import serial  # noqa: E402


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


def hexs(b):
    return " ".join(f"{x:02X}" for x in b)


def decode_candidates(data):
    """把寄存器字节按多种常见口径解一遍，供比对。"""
    out = []
    if len(data) >= 4:
        v0, v1 = struct.unpack(">2H", data[:4])
        cdab = (v1 << 16) | v0
        abcd = (v0 << 16) | v1
        out.append(f"CDAB_float={struct.unpack('>f', struct.pack('>I', cdab))[0]:.4f}")
        out.append(f"ABCD_float={struct.unpack('>f', struct.pack('>I', abcd))[0]:.4f}")
        out.append(f"CDAB_u32={cdab} /100={cdab/100.0:.4f}")
        out.append(f"ABCD_u32={abcd} /100={abcd/100.0:.4f}")
    if len(data) >= 2:
        out.append(f"u16={struct.unpack('>H', data[:2])[0]}")
    if len(data) >= 4:
        out.append(f"i32={struct.unpack('>i', data[:4])[0]}")
    return "  ".join(out)


def transact(port, unit, fc, addr, count, timeout=0.35, quiet_ms=4.0):
    """发一帧，按静默收帧；返回 (frame, elapsed_ms)。"""
    port.reset_input_buffer()
    req = build(unit, fc, addr, count)
    port.write(req)
    port.flush()
    buf = bytearray()
    last = None
    t0 = time.time()
    while time.time() - t0 < timeout:
        chunk = port.read(256)
        now = time.time()
        if chunk:
            buf += chunk
            last = now
            continue
        if buf and last is not None and (now - last) * 1000.0 >= quiet_ms:
            break
    return bytes(buf), (time.time() - t0) * 1000.0


def mode_scan(args):
    port = serial.Serial(args.port, args.baud, timeout=0.02, write_timeout=1.0)
    time.sleep(0.2)
    port.reset_input_buffer()
    print(f"=== 独占扫描 {args.port} @ {args.baud} ===")
    for unit in args.units:
        hits = 0
        print(f"\n########## 站号 {unit} ##########")
        for fc in (0x03, 0x04):
            for addr in range(0x0000, args.addr_end, 1):
                frame, dt = transact(port, unit, fc, addr, 1)
                if not frame:
                    continue
                hits += 1
                ok = False
                if len(frame) >= 4:
                    c = crc16(frame[:-2])
                    ok = frame[-2] == (c & 0xFF) and frame[-1] == ((c >> 8) & 0xFF)
                body = frame[3:-2] if len(frame) >= 5 else b""
                print(f"  fc={fc:#04x} addr={addr:#06x} → {hexs(frame):<28} "
                      f"crc_ok={ok} t={dt:.0f}ms  {decode_candidates(body)}")
        # 连读 2 寄存器（浊度值标准口径）
        for fc in (0x03, 0x04):
            frame, dt = transact(port, unit, fc, 0x0000, 2)
            if frame:
                ok = False
                if len(frame) >= 4:
                    c = crc16(frame[:-2])
                    ok = frame[-2] == (c & 0xFF) and frame[-1] == ((c >> 8) & 0xFF)
                body = frame[3:-2] if len(frame) >= 5 else b""
                print(f"  fc={fc:#04x} addr=0x0000 ×2 → {hexs(frame):<28} "
                      f"crc_ok={ok} t={dt:.0f}ms  {decode_candidates(body)}")
        if not hits:
            print("  >>> 该站号全地址无任何应答（设备不在线/未接线/波特率不符）")
    port.close()


def mode_race(args):
    """两个独立串口实例并发读同一 tty —— 复现 hsfsrv 的抢字节问题。"""
    print(f"=== 并发争抢复现 {args.port} @ {args.baud} ===")
    print("   线程A: 站号5 流量计 FC03 0x1010×22 (需 49 字节)")
    print("   线程B: 站号1 浊度仪 FC03 0x0000×2 (需 9 字节)\n")
    results = {}

    def worker(tag, unit, fc, addr, count, need):
        port = serial.Serial(args.port, args.baud, timeout=0.02, write_timeout=1.0)
        lens = {}
        bad = 0
        try:
            for i in range(30):
                frame, dt = transact(port, unit, fc, addr, count, timeout=0.4)
                lens[len(frame)] = lens.get(len(frame), 0) + 1
                if len(frame) < need:
                    bad += 1
                time.sleep(0.01)
        finally:
            port.close()
        results[tag] = (lens, bad)

    ta = threading.Thread(target=worker, args=("A流量计", 5, 0x03, 0x1010, 22, 49))
    tb = threading.Thread(target=worker, args=("B浊度仪", 1, 0x03, 0x0000, 2, 9))
    ta.start()
    tb.start()
    ta.join()
    tb.join()
    for tag, (lens, bad) in results.items():
        print(f"  {tag}: 长度分布 {lens}  残帧/无应答 {bad}/30")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--mode", default="scan", choices=["scan", "race"])
    ap.add_argument("--port", default="/dev/ttyS4")
    ap.add_argument("--baud", type=int, default=9600)
    ap.add_argument("--units", default="1,3")
    ap.add_argument("--addr-end", type=lambda s: int(s, 0), default=0x0020)
    args = ap.parse_args()
    args.units = [int(u) for u in args.units.split(",") if u.strip()]
    if args.mode == "scan":
        mode_scan(args)
    else:
        mode_race(args)


if __name__ == "__main__":
    main()
