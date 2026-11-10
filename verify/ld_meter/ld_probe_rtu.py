#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""LD 电磁流量计 RS485 原始帧探测（板端 ttyS4 / 9600 8N1）。

用途：在不依赖 hsfsrv 的情况下，直接抓 Modbus-RTU 原始收发帧，
      用于判定「设备是否应答 / 应答是否完整 / 站号与波特率对不对」。

用法（板端）：
    PYTHONPATH=/root/pyserv python3 ld_probe_rtu.py --port /dev/ttyS4 --baud 9600
"""
import argparse
import sys
import time

try:
    import serial
except ImportError:  # 板端 pyserial 解包 wheel 在 /root/pyserv
    sys.path.insert(0, "/root/pyserv")
    import serial


def crc16(data: bytes) -> int:
    """标准 Modbus CRC16（poly 0xA001, init 0xFFFF），返回低字节在前的两字节。"""
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc >>= 1
    return crc


def build(unit: int, fc: int, addr: int, count: int) -> bytes:
    body = bytes([unit, fc]) + addr.to_bytes(2, "big") + count.to_bytes(2, "big")
    c = crc16(body)
    return body + bytes([c & 0xFF, (c >> 8) & 0xFF])


def hexs(b: bytes) -> str:
    return " ".join(f"{x:02X}" for x in b)


def expected_len(frame: bytes) -> int:
    """按 Modbus-RTU 应答长度规则算期望字节数（用于判完整）。"""
    if len(frame) < 2:
        return 0
    fc = frame[1]
    if fc & 0x80:
        return 5  # 异常应答: unit, fc|0x80, code, crc2
    if fc in (0x01, 0x02, 0x03, 0x04):
        return 5 + frame[2]  # unit, fc, bytecount, data, crc2
    return 0


def read_idle(port, idle_ms: float, timeout: float):
    """模拟 hsfsrv 的收帧方式：读到「静默 idle_ms」或超时为止。

    这正是 modbus.py `_read_until_idle` 的口径（RTU_IDLE_MS=3ms）。
    """
    buf = bytearray()
    last = None
    t0 = time.time()
    while True:
        chunk = port.read(256)
        now = time.time()
        if chunk:
            buf += chunk
            last = now
            continue
        if buf and last is not None and (now - last) * 1000.0 >= idle_ms:
            break
        if now - t0 > timeout:
            break
    return bytes(buf)


def read_fill(port, want: int, timeout: float):
    """按长度补齐（阻塞读，不在中间 sleep）。"""
    buf = bytearray()
    t0 = time.time()
    while len(buf) < want and (time.time() - t0) < timeout:
        chunk = port.read(want - len(buf))
        if chunk:
            buf += chunk
    return bytes(buf)


def probe(port, unit, fc, addr, count, timeout, idle_ms=3.0, note=""):
    """发一帧，收两遍：先按静默口径（复现客户端），再补齐（拿真帧）。"""
    req = build(unit, fc, addr, count)
    port.reset_input_buffer()
    port.write(req)
    port.flush()

    t0 = time.time()
    idle_raw = read_idle(port, idle_ms, timeout)
    t_idle = (time.time() - t0) * 1000.0

    need = 5 + count * 2 if fc in (0x01, 0x02, 0x03, 0x04) else 0
    full = idle_raw
    t_fill = 0.0
    if need and len(idle_raw) < need:
        t1 = time.time()
        full = idle_raw + read_fill(port, need - len(idle_raw), timeout)
        t_fill = (time.time() - t1) * 1000.0

    def crc_ok_of(frame):
        if len(frame) < 4:
            return False
        c = crc16(frame[:-2])
        return frame[-2] == (c & 0xFF) and frame[-1] == ((c >> 8) & 0xFF)

    print(f"--- {note} unit={unit} fc={fc:#04x} addr={addr:#06x} count={count}")
    print(f"    REQ        : {hexs(req)}")
    print(f"    idle收({idle_ms:g}ms) : {hexs(idle_raw) if idle_raw else '(无)'}"
          f"  len={len(idle_raw)} t={t_idle:.1f}ms")
    print(f"    补齐后全帧 : {hexs(full) if full else '(无)'}"
          f"  len={len(full)} need={need} crc_ok={crc_ok_of(full)}"
          f" t_fill={t_fill:.1f}ms")
    if len(idle_raw) != len(full):
        print(f"    >> 静默口径截断了 {len(full) - len(idle_raw)} 字节"
              f"（迟到字节 {[hex(x) for x in full[len(idle_raw):]]}）")
    return full


def parse_vars(frame):
    """把变量区应答解析成工程值（22 寄存器，高字在前、大端）。"""
    import struct
    if len(frame) < 49:
        return None
    data = frame[3:47]
    regs = list(struct.unpack(">22H", data))

    def f32(hi, lo):
        raw = ((hi & 0xFFFF) << 16) | (lo & 0xFFFF)
        return struct.unpack(">f", struct.pack(">I", raw))[0]

    def u32(hi, lo):
        return ((hi & 0xFFFF) << 16) | (lo & 0xFFFF)

    flow = f32(regs[0], regs[1])
    speed = f32(regs[2], regs[3])
    pct = f32(regs[4], regs[5])
    cond = f32(regs[6], regs[7])
    fwd_int = u32(regs[8], regs[9])
    fwd_dec = f32(regs[10], regs[11])
    rev_int = u32(regs[12], regs[13])
    rev_dec = f32(regs[14], regs[15])
    flow_unit = regs[16]
    total_unit = regs[17]
    return {
        "瞬时流量": flow, "瞬时流速": speed, "流量百分比": pct,
        "电导比": cond,
        "正向累积": fwd_int + fwd_dec, "正向整数": fwd_int, "正向小数": fwd_dec,
        "反向累积": rev_int + rev_dec,
        "流量单位码": flow_unit, "累积单位码": total_unit,
        "上限报警": bool(regs[18]), "下限报警": bool(regs[19]),
        "空管报警": bool(regs[20]), "系统报警": bool(regs[21]),
    }


UNIT_NAMES = {0: "m³/s", 1: "L/s", 2: "L/min", 3: "m³/h", 4: "t/h", 5: "m³/h",
               6: "kg/h", 7: "kg/min", 8: "t/min", 9: "lb/h", 10: "gal/min",
               11: "gal/h"}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="/dev/ttyS4")
    ap.add_argument("--baud", type=int, default=9600)
    ap.add_argument("--timeout", type=float, default=1.5)
    ap.add_argument("--units", default="1,5")
    ap.add_argument("--idle-ms", type=float, default=3.0)
    args = ap.parse_args()

    units = [int(u) for u in args.units.split(",") if u.strip()]

    print(f"=== 打开 {args.port} @ {args.baud} 8N1 ===")
    port = serial.Serial(
        port=args.port, baudrate=args.baud, bytesize=serial.EIGHTBITS,
        parity=serial.PARITY_NONE, stopbits=serial.STOPBITS_ONE,
        timeout=0.02, write_timeout=1.0,
    )
    try:
        port.reset_input_buffer()
        time.sleep(0.2)
        port.reset_input_buffer()

        good = []
        for unit in units:
            for fc in (0x04, 0x03):
                f = probe(port, unit, fc, 0x1010, 22, args.timeout,
                          args.idle_ms, note="变量区(瞬时流量等)")
                if f and len(f) >= 49:
                    good.append((unit, fc, f))
            for addr, tag in ((0x0001, "通讯地址"), (0x0002, "通讯速度"),
                              (0x0006, "流量单位"), (0x000A, "积算单位"),
                              (0x002F, "校验模式")):
                probe(port, unit, 0x03, addr, 1, args.timeout,
                      args.idle_ms, note=f"参数区 {tag}")

        print("\n=== 变量区解析 ===")
        for unit, fc, f in good:
            v = parse_vars(f)
            if not v:
                continue
            print(f"[unit={unit} fc={fc:#04x}] "
                  f"流量={v['瞬时流量']:.4f} {UNIT_NAMES.get(v['流量单位码'], v['流量单位码'])}"
                  f"  流速={v['瞬时流速']:.4f}  百分比={v['流量百分比']:.2f}%"
                  f"  电导比={v['电导比']:.3f}")
            print(f"    正向累积={v['正向累积']} (整数{v['正向整数']}+小数{v['正向小数']:.4f})"
                  f"  累积单位码={v['累积单位码']}")
            print(f"    反向累积={v['反向累积']}")
            print(f"    报警: 上限={v['上限报警']} 下限={v['下限报警']} "
                  f"空管={v['空管报警']} 系统={v['系统报警']}")

        print("\n=== 连续读变量区 10 次（按完整帧口径）===")
        lens, okc = {}, 0
        for i in range(10):
            f = probe(port, units[0], 0x04, 0x1010, 22, args.timeout,
                      args.idle_ms, note=f"#{i+1}")
            lens[len(f)] = lens.get(len(f), 0) + 1
            if len(f) >= 49:
                okc += 1
            time.sleep(0.05)
        print(f"\n完整帧 {okc}/10  长度分布 {lens}")

        print("\n=== 静默口径对比：3ms vs 10ms（同一帧收法）===")
        for idle in (3.0, 4.5, 10.0):
            n_ok = 0
            for i in range(10):
                f = probe(port, units[0], 0x04, 0x1010, 22, args.timeout,
                          idle, note=f"idle={idle:g}ms #{i+1}")
                if len(f) >= 49:
                    n_ok += 1
                time.sleep(0.05)
            print(f"  idle={idle:g}ms → 完整帧 {n_ok}/10")
    finally:
        port.close()


if __name__ == "__main__":
    main()
