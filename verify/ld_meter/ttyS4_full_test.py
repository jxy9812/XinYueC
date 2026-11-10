#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ttyS4 三台设备统一实测（板端专用，**全程只读，不发 FC06 写帧**）。

覆盖现场三台设备：
  站1  在线浊度监测仪 WZT-701A          上海仪电科学仪器
  站2  水质浊度智能控制器 ZHEYI-5100ZDY  浙江浙仪自控系统工程
  站5  LD 电磁流量计                     上自仪

收帧口径 = **按长度收满**（先读 3 字节头拿 byteCount，再补足），
不靠静默猜帧尾 —— 这是 2026-11-09 实测确认的唯一可靠方式（9600 下内核
会把 49 字节应答拆成 32 + 17 两批，批间隔 ~20.6ms，任何静默阈值都断）。

用法（板端，必须先停 hsfserverpy 独占 ttyS4）：
    systemctl stop hsfserverpy
    PYTHONPATH=/root/pyserv python3 ttyS4_full_test.py --section all
    systemctl start hsfserverpy
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


# ---------------------------------------------------------------- Modbus 基础

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
    return " ".join(f"{x:02X}" for x in b) if b else "(无)"


def crc_ok(frame):
    if len(frame) < 4:
        return False
    c = crc16(frame[:-2])
    return frame[-2] == (c & 0xFF) and frame[-1] == ((c >> 8) & 0xFF)


def read_exact(port, want, timeout):
    buf = bytearray()
    t0 = time.time()
    while len(buf) < want and (time.time() - t0) < timeout:
        chunk = port.read(want - len(buf))
        if chunk:
            buf += chunk
    return bytes(buf)


def transact(port, unit, fc, addr, count, timeout=0.5, gap=0.02):
    """发一帧按长度收满。返回 (frame, elapsed_ms)。"""
    req = build(unit, fc, addr, count)
    port.reset_input_buffer()
    t0 = time.time()
    port.write(req)
    port.flush()

    hdr = read_exact(port, 3, timeout)
    if len(hdr) < 3:
        return hdr, (time.time() - t0) * 1000.0
    if hdr[1] & 0x80:
        rest = read_exact(port, 2, timeout)
    else:
        rest = read_exact(port, hdr[2] + 2, timeout)
    return hdr + rest, (time.time() - t0) * 1000.0


def decode_all(data):
    """把一段寄存器字节按多种常见口径解一遍，供人工比对。"""
    out = []
    if len(data) >= 2:
        u = struct.unpack(">H", data[:2])[0]
        out.append(f"u16={u}(0x{u:04X}) i16={struct.unpack('>h', data[:2])[0]}")
    if len(data) >= 4:
        hi, lo = struct.unpack(">2H", data[:4])
        cdab = (lo << 16) | hi          # 低字在前
        abcd = (hi << 16) | lo          # 高字在前
        f_cdab = struct.unpack(">f", struct.pack(">I", cdab))[0]
        f_abcd = struct.unpack(">f", struct.pack(">I", abcd))[0]
        out.append(f"CDAB_f32={f_cdab:.6g} ABCD_f32={f_abcd:.6g}")
        out.append(f"CDAB_u32={cdab}(/{100} {cdab/100.0:.4g}) ABCD_u32={abcd}")
    return "  ".join(out)


# ---------------------------------------------------------------- 输出工具

def hdr(title):
    print("\n" + "=" * 78)
    print(f"### {title}")
    print("=" * 78)


def show(tag, req_desc, frame, dt):
    ok = crc_ok(frame)
    print(f"  {tag:<26} REQ={req_desc:<22} RSP[{len(frame):>3}]={hexs(frame):<60} "
          f"crc={'OK' if ok else 'BAD'} t={dt:.0f}ms")


# ---------------------------------------------------------------- 各段测试

def sec_env(port, baud):
    hdr("A. 测试环境")
    print(f"  端口      : {port}")
    print(f"  串口参数  : {baud} 8N1 (无校验, 1 停止位)")
    print(f"  时间      : {time.strftime('%Y-%m-%d %H:%M:%S')}")
    print(f"  收帧口径  : 按长度收满（3 字节头 + byteCount 数据 + CRC2）")
    print(f"  写操作    : 无（全程只读 FC03/FC04）")


def sec_presence(port, units):
    """普查在线站号：对每个站号试 0x0000(浊度) 与 0x1010(流量计) 两个地址。"""
    hdr("B. 在线站号普查（FC03 读 2 寄存器）")
    print(f"  {'站号':<5}{'0x0000×2':<34}{'0x1010×2':<34}判定")
    online = []
    for unit in units:
        f1, t1 = transact(port, unit, 0x03, 0x0000, 2)
        f2, t2 = transact(port, unit, 0x03, 0x1010, 2)
        d1 = f"{len(f1)}B {'CRC-OK' if crc_ok(f1) else 'bad'}" if f1 else "无应答"
        d2 = f"{len(f2)}B {'CRC-OK' if crc_ok(f2) else 'bad'}" if f2 else "无应答"
        verdict = ""
        if f1 and crc_ok(f1) and not (f1[1] & 0x80):
            verdict = "浊度类设备在线"
            online.append((unit, "turb"))
        if f2 and crc_ok(f2) and not (f2[1] & 0x80):
            verdict = (verdict + " / 流量计在线").strip(" /")
            online.append((unit, "flow"))
        print(f"  {unit:<5}{d1:<34}{d2:<34}{verdict}")
        time.sleep(0.02)
    print(f"\n  >>> 在线：{online if online else '无'}")
    return online


def sec_turbidity(port, unit, name, scan_end, reads=10):
    """浊度仪通用测试：浊度帧、FC 能力、寄存器扫描、稳定性。"""
    hdr(f"C. 浊度仪 站{unit} —— {name}")

    print("\n-- C.1 浊度值帧（0x0000 起 2 寄存器） --")
    samples = []
    for fc in (0x03, 0x04):
        for i in range(3):
            f, dt = transact(port, unit, fc, 0x0000, 2)
            show(f"fc{fc:02X} #{i+1}", f"{unit:02X} {fc:02X} 00 00 00 02", f, dt)
            if len(f) >= 9 and crc_ok(f) and not (f[1] & 0x80):
                lo, hi = struct.unpack(">2H", f[3:7])
                cdab = (hi << 16) | lo
                val = struct.unpack(">f", struct.pack(">I", cdab))[0]
                print(f"      低字=0x{lo:04X} 高字=0x{hi:04X} → CDAB float32 = {val:.6g}")
                samples.append(val)
            time.sleep(0.05)

    print("\n-- C.2 功能码能力 --")
    for fc in (0x03, 0x04):
        f, dt = transact(port, unit, fc, 0x0000, 2)
        state = "支持" if (f and crc_ok(f) and not (f[1] & 0x80)) else (
            "异常应答" if (f and (f[1] & 0x80)) else "无应答")
        print(f"  FC{fc:02X} 读 0x0000×2 : {state:<10} RSP[{len(f)}]={hexs(f)}")

    print("\n-- C.3 连读能力（FC03 0x0000 起 N 个） --")
    for n in (1, 2, 4, 8, 16, 32):
        f, dt = transact(port, unit, 0x03, 0x0000, n)
        if f and crc_ok(f) and not (f[1] & 0x80):
            print(f"  N={n:<3} → 回 {f[2]} 字节数据 ({f[2]//2} 个寄存器) t={dt:.0f}ms")
        else:
            print(f"  N={n:<3} → 失败 RSP[{len(f)}]={hexs(f)}")
        time.sleep(0.03)

    print(f"\n-- C.4 寄存器扫描 0x0000~0x{scan_end-1:04X}（逐点 FC03×1） --")
    nonzero = {}
    for addr in range(0, scan_end):
        f, dt = transact(port, unit, 0x03, addr, 1)
        if not f:
            print(f"  0x{addr:04X} → 无应答")
            continue
        if f[1] & 0x80:
            print(f"  0x{addr:04X} → 异常码 0x{f[2]:02X}")
            continue
        if not crc_ok(f):
            print(f"  0x{addr:04X} → CRC 错 {hexs(f)}")
            continue
        if f[2] >= 2:
            val = struct.unpack(">H", f[3:5])[0]
            if val != 0:
                nonzero[addr] = val
                print(f"  0x{addr:04X} = 0x{val:04X} ({val})")
        time.sleep(0.015)
    print(f"  >>> 非零寄存器 {len(nonzero)} 个："
          f"{ {hex(a): v for a, v in sorted(nonzero.items())} }")

    print(f"\n-- C.5 稳定性（浊度帧 {reads} 连读） --")
    vals, lens, okc = [], {}, 0
    for i in range(reads):
        f, dt = transact(port, unit, 0x03, 0x0000, 2)
        lens[len(f)] = lens.get(len(f), 0) + 1
        if len(f) >= 9 and crc_ok(f):
            okc += 1
            lo, hi = struct.unpack(">2H", f[3:7])
            vals.append(struct.unpack(">f", struct.pack(">I", (hi << 16) | lo))[0])
        time.sleep(0.05)
    print(f"  完整帧 {okc}/{reads}  长度分布 {lens}")
    if vals:
        print(f"  NTU 取值: {', '.join(f'{v:.6g}' for v in vals)}")
        print(f"  最小={min(vals):.6g} 最大={max(vals):.6g} "
              f"恒定={'是' if max(vals) == min(vals) else '否'}")
    return nonzero


def sec_flowmeter(port, unit=5, reads=10):
    """LD 电磁流量计：变量区一帧、参数区单点、稳定性。"""
    hdr(f"D. LD 电磁流量计 站{unit} —— 上自仪 L_mag B 系列")

    print("\n-- D.1 变量区一帧（FC04 0x1010 × 22） --")
    ok_frames = []
    for fc in (0x04, 0x03):
        f, dt = transact(port, unit, fc, 0x1010, 22)
        show(f"fc{fc:02X} 变量区", f"{unit:02X} {fc:02X} 10 10 00 16", f, dt)
        if len(f) >= 49 and crc_ok(f):
            ok_frames.append((fc, f))
        time.sleep(0.05)

    for fc, f in ok_frames:
        regs = list(struct.unpack(">22H", f[3:47]))

        def f32(hi, lo):
            return struct.unpack(">f", struct.pack(">I", ((hi & 0xFFFF) << 16) | (lo & 0xFFFF)))[0]

        def u32(hi, lo):
            return ((hi & 0xFFFF) << 16) | (lo & 0xFFFF)

        print(f"\n  解析（FC{fc:02X}，大端无字交换）：")
        print(f"    瞬时流量   = {f32(regs[0], regs[1]):.4f}")
        print(f"    瞬时流速   = {f32(regs[2], regs[3]):.4f}")
        print(f"    流量百分比 = {f32(regs[4], regs[5]):.2f} %")
        print(f"    流体电导比 = {f32(regs[6], regs[7]):.1f}")
        print(f"    正向累积   = {u32(regs[8], regs[9])} + {f32(regs[10], regs[11]):.4f}"
              f" = {u32(regs[8], regs[9]) + f32(regs[10], regs[11])}")
        print(f"    反向累积   = {u32(regs[12], regs[13])} + {f32(regs[14], regs[15]):.4f}")
        print(f"    流量单位码 = {regs[16]}   累积单位码 = {regs[17]}")
        print(f"    报警: 上限={bool(regs[18])} 下限={bool(regs[19])} "
              f"空管={bool(regs[20])} 系统={bool(regs[21])}")
        print(f"    原始 22 寄存器: {[f'{r:04X}' for r in regs]}")

    print("\n-- D.2 参数区（逐点单读，设备不支持连读） --")
    params = [(0x0000, "语言"), (0x0001, "通讯地址"), (0x0002, "通讯速度"),
              (0x0003, "管道口径"), (0x0006, "流量单位"), (0x0007, "阻尼时间"),
              (0x000A, "流量积算单位"), (0x000C, "电流输出类型"),
              (0x0012, "频率输出范围"), (0x001F, "出厂标定系数"),
              (0x0026, "励磁方式"), (0x002F, "通讯校验模式")]
    for addr, tag in params:
        f, dt = transact(port, unit, 0x03, addr, 1)
        if f and crc_ok(f) and not (f[1] & 0x80) and f[2] >= 2:
            val = struct.unpack(">H", f[3:5])[0]
            print(f"  0x{addr:04X} {tag:<12} = {val:<6} (0x{val:04X})")
        else:
            print(f"  0x{addr:04X} {tag:<12} → 失败 RSP={hexs(f)}")
        time.sleep(0.02)

    print("\n-- D.3 参数区连读能力（证明只能单点） --")
    for n in (1, 2, 4, 8):
        f, dt = transact(port, unit, 0x03, 0x0000, n)
        got = f[2] // 2 if (f and len(f) > 2 and not (f[1] & 0x80)) else 0
        print(f"  请求 {n} 个 → 实测返回 {got} 个  (RSP[{len(f)}])")

    print(f"\n-- D.4 稳定性（变量区 {reads} 连读） --")
    lens, okc, flows = {}, 0, []
    for i in range(reads):
        f, dt = transact(port, unit, 0x04, 0x1010, 22)
        lens[len(f)] = lens.get(len(f), 0) + 1
        if len(f) >= 49 and crc_ok(f):
            okc += 1
            regs = struct.unpack(">22H", f[3:47])
            flows.append(struct.unpack(">f", struct.pack(">I", (regs[0] << 16) | regs[1]))[0])
        time.sleep(0.05)
    print(f"  完整帧 {okc}/{reads}(49B)  长度分布 {lens}")
    if flows:
        print(f"  瞬时流量: {', '.join(f'{v:.4f}' for v in flows[:5])}"
              f"{' …' if len(flows) > 5 else ''}  范围 {min(flows):.4f}~{max(flows):.4f}")


def sec_cross(port, turb_units):
    """三台混跑：验证同一总线交替访问不互相干扰。"""
    hdr("E. 三台混跑（交替访问，验证无互相冲帧）")
    plan = [(u, 0x03, 0x0000, 2, f"浊度站{u}") for u in turb_units]
    plan.append((5, 0x04, 0x1010, 22, "流量计站5"))
    ok, total = 0, 0
    for rnd in range(3):
        print(f"\n  第 {rnd+1} 轮：")
        for unit, fc, addr, count, tag in plan:
            need = 5 + count * 2
            f, dt = transact(port, unit, fc, addr, count)
            total += 1
            good = len(f) >= need and crc_ok(f)
            if good:
                ok += 1
            val = ""
            if good and count == 2:
                lo, hi = struct.unpack(">2H", f[3:7])
                val = f"NTU={struct.unpack('>f', struct.pack('>I', (hi<<16)|lo))[0]:.6g}"
            elif good:
                regs = struct.unpack(">22H", f[3:47])
                val = f"流量={struct.unpack('>f', struct.pack('>I', (regs[0]<<16)|regs[1]))[0]:.4f}"
            print(f"    {tag:<10} {len(f):>3}B crc={'OK' if crc_ok(f) else 'BAD'} "
                  f"t={dt:>4.0f}ms {val}")
            time.sleep(0.03)
    print(f"\n  >>> 混跑成功 {ok}/{total}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="/dev/ttyS4")
    ap.add_argument("--baud", type=int, default=9600)
    ap.add_argument("--section", default="all",
                    choices=["all", "env", "presence", "turb1", "turb2", "flow", "cross"])
    ap.add_argument("--presence-units", default="1-20")
    ap.add_argument("--scan-end", type=lambda s: int(s, 0), default=0x40)
    args = ap.parse_args()

    units = []
    for part in args.presence_units.split(","):
        part = part.strip()
        if "-" in part:
            a, b = part.split("-")
            units.extend(range(int(a), int(b) + 1))
        elif part:
            units.append(int(part))

    port = serial.Serial(port=args.port, baudrate=args.baud,
                         bytesize=serial.EIGHTBITS, parity=serial.PARITY_NONE,
                         stopbits=serial.STOPBITS_ONE, timeout=0.02,
                         write_timeout=1.0)
    try:
        port.reset_input_buffer()
        time.sleep(0.2)
        port.reset_input_buffer()

        if args.section in ("all", "env"):
            sec_env(args.port, args.baud)
        if args.section in ("all", "presence"):
            sec_presence(port, units)
        if args.section in ("all", "turb1"):
            sec_turbidity(port, 1, "WZT-701A 在线浊度监测仪（上海仪电科学）",
                          args.scan_end)
        if args.section in ("all", "turb2"):
            sec_turbidity(port, 2, "ZHEYI-5100ZDY 水质浊度智能控制器（浙江浙仪自控）",
                          args.scan_end)
        if args.section in ("all", "flow"):
            sec_flowmeter(port, 5)
        if args.section in ("all", "cross"):
            sec_cross(port, [1, 2])
    finally:
        port.close()
        print("\n=== 完成（串口已释放） ===")


if __name__ == "__main__":
    main()
