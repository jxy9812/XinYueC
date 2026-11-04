#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""中盛科技 温湿度采集模块（RS485 版, ZS-SHT30-1T-1H）主站探测工具。

针对《温湿度采集系列使用手册（RS485 版）》V2.1 第 2 章寄存器表做实测核对：
输入寄存器 0000H~0005H（温度/湿度，uint16 与 float32 双表达）、
保持寄存器 000AH 站号 / 000BH 波特率 / 003DH 校验。

用法:
    python verify/temphum_probe.py --port COM17 --baud 115200 --unit 4
    python verify/temphum_probe.py --port COM17 --unit 4 --scan-baud
    python verify/temphum_probe.py --port COM17 --unit 4 --watch 30

依赖: pyserial。
"""
import argparse
import struct
import sys
import time

# ---------------------------------------------------------------- Modbus RTU


def crc16(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc


def build(unit: int, func: int, addr: int, count: int) -> bytes:
    body = struct.pack(">BBHH", unit, func, addr, count)
    return body + struct.pack("<H", crc16(body))


def expected_len(unit: int, func: int, count: int) -> int:
    if func in (0x01, 0x02, 0x03, 0x04):
        nbytes = count * 2 if func in (0x03, 0x04) else (count + 7) // 8
        return 1 + 1 + 1 + nbytes + 2
    return 8


def recv_frame(sp, want: int, timeout: float) -> bytes:
    deadline = time.time() + timeout
    buf = b""
    while time.time() < deadline:
        chunk = sp.read(want - len(buf) if want > len(buf) else 1)
        if chunk:
            buf += chunk
            if len(buf) >= 3 and (buf[0] & 0x80 == 0) and len(buf) >= want:
                break
            if len(buf) >= 5 and buf[1] & 0x80:      # 异常响应固定 5 字节
                break
        else:
            time.sleep(0.002)
    return buf


def transact(sp, unit: int, func: int, addr: int, count: int,
             timeout: float = 0.5, gap: float = 0.0):
    """一问一答。返回 (raw_frame, rtt_ms, error_str)。"""
    frame = build(unit, func, addr, count)
    sp.reset_input_buffer()
    t0 = time.perf_counter()
    sp.write(frame)
    sp.flush()
    raw = recv_frame(sp, expected_len(unit, func, count), timeout)
    rtt = (time.perf_counter() - t0) * 1000.0
    if gap:
        time.sleep(gap)
    if not raw:
        return None, rtt, "timeout"
    if len(raw) < 5:
        return raw, rtt, "short frame %r" % raw
    if crc16(raw[:-2]) != struct.unpack("<H", raw[-2:])[0]:
        return raw, rtt, "crc mismatch %s" % raw.hex()
    if raw[1] & 0x80:
        code = raw[2] if len(raw) > 2 else -1
        return raw, rtt, "exception fc=0x%02X code=0x%02X" % (raw[1], code)
    return raw, rtt, None


def read_regs(sp, unit, func, addr, count, timeout=0.5):
    raw, rtt, err = transact(sp, unit, func, addr, count, timeout)
    if err:
        return None, rtt, err
    nbytes = raw[2]
    if len(raw) < 3 + nbytes + 2:
        return None, rtt, "数据不足 %s" % raw.hex()
    regs = list(struct.unpack(">%dH" % (nbytes // 2), raw[3:3 + nbytes]))
    return regs, rtt, None


def write_reg(sp, unit, addr, value, timeout=0.5):
    body = struct.pack(">BBHH", unit, 0x06, addr, value & 0xFFFF)
    frame = body + struct.pack("<H", crc16(body))
    sp.reset_input_buffer()
    sp.write(frame)
    sp.flush()
    raw = recv_frame(sp, 8, timeout)
    if not raw:
        return None, "timeout"
    if crc16(raw[:-2]) != struct.unpack("<H", raw[-2:])[0]:
        return raw, "crc mismatch"
    return raw, None


# ---------------------------------------------------------------- 数据解析

def decode_u16_temp(raw: int) -> float:
    """手册 2.1：<10000 为正温度；>=10000 为负温度 -1*(v-10000)*0.1。"""
    if raw >= 10000:
        return -1.0 * (raw - 10000) * 0.1
    return raw * 0.1


def decode_u16_hum(raw: int) -> float:
    return raw * 0.1


def f32(pair) -> float:
    """两个寄存器拼 32 位大端浮点（手册：大端模式）。"""
    return struct.unpack(">f", struct.pack(">HH", pair[0], pair[1]))[0]


def f32_le(pair) -> float:
    """小端（字交换）对照，用于判端序。"""
    return struct.unpack(">f", struct.pack(">HH", pair[1], pair[0]))[0]


def sane(v: float, lo: float, hi: float) -> bool:
    return lo <= v <= hi and v == v


# ---------------------------------------------------------------- 各探测段

def sect(title):
    print("\n=== %s " % title + "=" * max(0, 62 - len(title)))


def probe_identity(sp, unit, tmo):
    sect("身份/存活：FC04 读输入寄存器 0000H x 6")
    regs, rtt, err = read_regs(sp, unit, 0x04, 0x0000, 6, tmo)
    if err:
        print("  FC04 0000H x6 失败: %s (rtt=%.2fms)" % (err, rtt))
        return False
    print("  FC04 0000H~0005H = %s  (rtt=%.2f ms)" %
          ([hex(r) for r in regs], rtt))
    t_u16 = decode_u16_temp(regs[0])
    h_u16 = decode_u16_hum(regs[1])
    print("    [0000H] uint16 温度 = %d -> %.1f degC" % (regs[0], t_u16))
    print("    [0001H] uint16 湿度 = %d -> %.1f %%RH" % (regs[1], h_u16))
    if len(regs) >= 6:
        for name, a, b in (("0002H~0003H", 2, 4), ("0004H~0005H", 4, 6)):
            be = f32(regs[a:b])
            le = f32_le(regs[a:b])
            print("    [%s] float32 BE=%.4f  LE(字交换)=%.4f  原始=%s"
                  % (name, be, le, [hex(x) for x in regs[a:b]]))
    return True


def probe_holding(sp, unit, tmo):
    sect("保持寄存器：站号/波特率/校验 + 全表扫描")
    named = [(0x000A, "站号 address"), (0x000B, "波特率 baud"),
             (0x003D, "校验 parity")]
    for addr, desc in named:
        regs, rtt, err = read_regs(sp, unit, 0x03, addr, 1, tmo)
        if err:
            print("  FC03 %04XH (%s) 失败: %s" % (addr, desc, err))
        else:
            print("  FC03 %04XH (%s) = %d (0x%04X)  rtt=%.2fms"
                  % (addr, desc, regs[0], regs[0], rtt))

    print("\n  -- FC03 扫描 0000H~004FH（只列非 0）--")
    nonzero = []
    for base in range(0x0000, 0x0050, 8):
        regs, rtt, err = read_regs(sp, unit, 0x03, base, 8, tmo)
        if err:
            print("    %04XH x8 失败: %s" % (base, err))
            continue
        for i, v in enumerate(regs):
            if v:
                nonzero.append((base + i, v))
                print("    %04XH = %d (0x%04X)" % (base + i, v, v))
    if not nonzero:
        print("    全 0")


def probe_input_scan(sp, unit, tmo):
    sect("输入寄存器：FC04 扫描 0000H~0017H（找有效通道边界）")
    for base in range(0x0000, 0x0018, 8):
        regs, rtt, err = read_regs(sp, unit, 0x04, base, 8, tmo)
        if err:
            print("  %04XH x8 失败: %s" % (base, err))
            continue
        print("  %04XH~%04XH = %s" % (base, base + 7,
                                      " ".join("%5d" % v for v in regs)))


def probe_fc_support(sp, unit, tmo):
    sect("功能码支持矩阵（手册声称 03H/04H/06H/10H）")
    for fc, label in ((0x03, "读保持"), (0x04, "读输入"),
                      (0x01, "读线圈"), (0x02, "读离散输入")):
        regs, rtt, err = read_regs(sp, unit, fc, 0x0000, 2, tmo)
        print("  FC%02X %-10s -> %s" % (fc, label,
                                       "OK 原始=%r" % regs if not err else err))
    # FC06 写回同值（无副作用）
    regs, _, err = read_regs(sp, unit, 0x03, 0x000A, 1, tmo)
    if not err:
        raw, err = write_reg(sp, unit, 0x000A, regs[0])
        print("  FC06 写站号寄存器 000AH=原值(%d) -> %s"
              % (regs[0], err or "OK " + (raw.hex() if raw else "")))
    else:
        print("  FC06 测试跳过（读站号寄存器失败）")


def probe_stability(sp, unit, tmo, n):
    sect("稳定性/噪声：连续 %d 次 FC04 采样" % n)
    temps, hums, rtts = [], [], []
    fails = 0
    for _ in range(n):
        regs, rtt, err = read_regs(sp, unit, 0x04, 0x0000, 6, tmo)
        if err:
            fails += 1
            continue
        temps.append(regs[0])
        hums.append(regs[1])
        rtts.append(rtt)
    if not temps:
        print("  全部失败")
        return
    rtts.sort()

    def stat(vals):
        return (min(vals), max(vals),
                sum(vals) / len(vals),
                len(set(vals)))

    tmin, tmax, tavg, tdist = stat(temps)
    hmin, hmax, havg, hdist = stat(hums)
    print("  成功 %d/%d，失败 %d" % (len(temps), n, fails))
    print("  温度原始 %d~%d 均值 %.1f 不同值 %d 个 -> %.1f~%.1f degC"
          % (tmin, tmax, tavg, tdist,
             decode_u16_temp(tmin), decode_u16_temp(tmax)))
    print("  湿度原始 %d~%d 均值 %.1f 不同值 %d 个 -> %.1f~%.1f %%RH"
          % (hmin, hmax, havg, hdist,
             decode_u16_hum(hmin), decode_u16_hum(hmax)))
    print("  RTT min %.2f / p50 %.2f / p95 %.2f / max %.2f ms"
          % (rtts[0], rtts[len(rtts) // 2], rtts[int(len(rtts) * 0.95)],
             rtts[-1]))


def probe_unit_scan(sp, tmo, lo=1, hi=32):
    sect("站号扫描 %d~%d（找实际应答的从站）" % (lo, hi))
    seen = []
    for u in range(lo, hi + 1):
        regs, rtt, err = read_regs(sp, u, 0x04, 0x0000, 2, tmo)
        if not err:
            seen.append(u)
            print("  unit=%-3d OK 原始=%s (%.0f ms)" % (u, regs, rtt))
    if not seen:
        print("  1~%d 无任何应答" % hi)
    return seen


def probe_baud_scan(port, unit, tmo):
    sect("波特率扫描（找设备实际波特率）")
    import serial
    for baud in (115200, 9600, 19200, 38400, 57600, 4800, 14400, 56000):
        try:
            sp = serial.Serial(port, baud, bytesize=8, parity="N",
                               stopbits=1, timeout=0.05, write_timeout=1.0)
        except Exception as exc:                       # noqa: BLE001
            print("  %6d 打开失败: %s" % (baud, exc))
            continue
        try:
            time.sleep(0.15)
            regs, rtt, err = read_regs(sp, unit, 0x04, 0x0000, 2, tmo)
            print("  %6d 8N1 -> %s" % (baud, regs if not err else err))
        finally:
            sp.close()


def probe_watch(sp, unit, tmo, secs, interval):
    sect("长时基观测 %ds（间隔 %.2fs，Ctrl-C 结束）" % (secs, interval))
    t_end = time.time() + secs
    n = 0
    t0 = time.time()
    try:
        while time.time() < t_end:
            regs, rtt, err = read_regs(sp, unit, 0x04, 0x0000, 6, tmo)
            if err:
                print("  %7.3f  %s" % (time.time() - t0, err))
            else:
                print("  %7.3f  T=%6d(%.1fC) H=%5d(%.1f%%) raw=%s"
                      % (time.time() - t0, regs[0], decode_u16_temp(regs[0]),
                         regs[1], decode_u16_hum(regs[1]),
                         [hex(r) for r in regs]))
            n += 1
            time.sleep(interval)
    except KeyboardInterrupt:
        pass
    print("  共 %d 次" % n)


# ---------------------------------------------------------------- main

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="COM17")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--unit", type=int, default=4)
    ap.add_argument("--timeout", type=float, default=0.5)
    ap.add_argument("--samples", type=int, default=50)
    ap.add_argument("--scan-units", action="store_true", help="扫描站号 1~32")
    ap.add_argument("--scan-baud", action="store_true", help="扫描波特率")
    ap.add_argument("--watch", type=int, default=0, help="长时基观测秒数")
    ap.add_argument("--interval", type=float, default=0.5)
    ap.add_argument("--no-write", action="store_true", help="跳过 FC06 写测试")
    args = ap.parse_args()

    try:
        import serial
    except ImportError:
        print("缺少 pyserial：pip install pyserial", file=sys.stderr)
        return 2

    print("温湿度模块探测  %s @%d 8N1  unit=%d  timeout=%.2fs"
          % (args.port, args.baud, args.unit, args.timeout))

    if args.scan_baud:
        probe_baud_scan(args.port, args.unit, args.timeout)
        return 0

    try:
        sp = serial.Serial(args.port, args.baud, bytesize=8, parity="N",
                           stopbits=1, timeout=0.05, write_timeout=1.0)
    except Exception as exc:                            # noqa: BLE001
        print("打开 %s 失败: %s" % (args.port, exc), file=sys.stderr)
        return 3

    try:
        time.sleep(0.1)
        alive = probe_identity(sp, args.unit, args.timeout)
        if not alive:
            print("\n  unit=%d 无应答，进入站号扫描" % args.unit)
            if not probe_unit_scan(sp, min(args.timeout, 0.3)):
                print("  站号扫描无果；建议 --scan-baud")
        else:
            probe_input_scan(sp, args.unit, args.timeout)
            probe_holding(sp, args.unit, args.timeout)
            if not args.no_write:
                probe_fc_support(sp, args.unit, args.timeout)
            probe_stability(sp, args.unit, args.timeout, args.samples)
            if args.scan_units:
                probe_unit_scan(sp, min(args.timeout, 0.3))
            if args.watch:
                probe_watch(sp, args.unit, args.timeout,
                            args.watch, args.interval)
    finally:
        sp.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
