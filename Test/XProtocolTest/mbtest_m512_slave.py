#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""511M/512M/513M 变频器 Modbus RTU 从站模拟器（XVfdM512 闭环测试用）。

依据《511M/512M/513M 高性能矢量变频器使用说明书》附录1 建模：
  · 标准 Modbus-RTU，仅 FC03/FC06
  · 特殊地址：2000H 命令字（1正转 2反转 3正点动 4反点动 5自由停机 6减速停机
    7故障复位）、3000H 运行状态（1正转 2反转 3停机）、8000H 故障码、
    1000H 通讯设定值（±10000=±100%）、2001H 数字输出强制位图、
    1F00H 密码校验、1F01H 参数初始化
  · 快捷遥测块 1001H..1006H（运行频率0.01Hz/母线电压0.1V/输出电压0.1V/
    输出电流0.01A/输出功率0.1kW/输出转矩0.1%），运行频率跟随设定值
  · P 组功能码 EEPROM 地址 F0xxH（P0.10 最大频率 0.01Hz、P0.17/18 加减速
    0.1s、P8.00 点动频率、PC.00~ 多段指令、Pd.02 本机地址）
  · 多站号：unit 1 与 unit 2 独立状态机（混合总线隔离测试）
  · 静默控制：mute 文件内容=站号 → 该站不应答（测从站离线/退避/恢复）

传输：os.openpty() 虚拟串口，从端路径写入 $BASE/pty_path。
控制接口（文件触摸式，操作后自删）：
  $BASE/fault_u1 / fault_u2   内容=整数（8000H 故障值，0=清除）
  $BASE/mute                  内容=站号 → 该站静默；删除文件=恢复
"""

import argparse
import os
import struct
import time

BASE = os.environ.get("M512_BASE", "/tmp/mbsoak/m512")
UNITS = (1, 2)

# ---- 特殊地址（附录1） ----
A_CMD = 0x2000          # 控制命令字
A_STATE = 0x3000        # 运行状态
A_FAULT = 0x8000        # 故障码
A_FREQSET = 0x1000      # 通讯设定值（±10000=±100%）
A_DO = 0x2001           # 数字输出强制位图
A_PWD = 0x1F00          # 密码校验
A_INIT = 0x1F01         # 参数初始化
A_TELE = 0x1001         # 快捷遥测块起点
A_TELE_N = 6            # 1001..1006

# ---- P 组功能码（EEPROM 编码 F0xxH） ----
P_MAXFREQ = 0xF00A      # P0.10 最大频率（0.01Hz，出厂 5000=50.00Hz）
P_ACCEL = 0xF011        # P0.17 加速时间1（0.1s）
P_DECEL = 0xF012        # P0.18 减速时间1
P_JOG = 0xF800          # P8.00 点动运行频率
P_PRESET0 = 0xFC00      # PC.00 多段指令0
P_LOCAL = 0xFD02        # Pd.02 本机地址


def log(fmt, *args):
    print("[m512 %08.3f] %s" % (time.time() % 100, fmt % args if args else fmt), flush=True)


def crc16(data):
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return struct.pack("<H", crc)


class M512:
    """单站寄存器模型（511M/512M/513M 族语义）。"""

    def __init__(self, unit_id):
        self.unit = unit_id
        self.state = 3              # 3=停机（3000H 口径）
        self.jog_active = False
        self.fault = 0              # 8000H 故障值
        self.setpoint = 0           # 1000H 通讯设定值（±10000）
        self.do_bits = 0
        self.max_freq = 5000        # P0.10 原始码（0.01Hz）= 50.00Hz
        self.params = {
            P_MAXFREQ: 5000,
            P_ACCEL: 100,           # 10.0s
            P_DECEL: 100,
            P_JOG: 200,             # 2.00Hz
            P_PRESET0: 0,
            P_LOCAL: unit_id,
        }
        self.cmd = 0
        self.init_cmd = 0

    # ---------- 读 ----------
    def read_reg(self, addr):
        if addr == A_CMD:
            return self.cmd & 0xFFFF
        if addr == A_STATE:
            if self.fault:
                return 1 if self.setpoint >= 0 else 2
            return self.state & 0xFFFF
        if addr == A_FAULT:
            return self.fault & 0xFFFF
        if addr == A_FREQSET:
            return self.setpoint & 0xFFFF
        if addr == A_DO:
            return self.do_bits & 0xFFFF
        if addr == A_PWD:
            return 0x8888           # 密码校验通过标记
        if addr == A_INIT:
            return self.init_cmd & 0xFFFF
        if A_TELE <= addr < A_TELE + A_TELE_N:
            off = addr - A_TELE
            if off == 0:
                # 运行频率跟随设定值（% × 最大频率）
                hz = self.setpoint / 10000.0 * (self.max_freq / 100.0)
                return int(round(hz * 100)) & 0xFFFF        # 0.01Hz
            if off == 1:
                return 5400            # 母线电压 540.0V（0.1V）
            if off == 2:
                return int(round(abs(self.setpoint) / 10000.0 * 3800))  # 输出电压 0.1V
            if off == 3:
                return 100             # 输出电流 1.00A（0.01A）
            if off == 4:
                return 75              # 输出功率 7.5kW（0.1kW）
            if off == 5:
                return 690             # 输出转矩 69.0%（0.1%）
            return 0
        if addr in self.params:
            return self.params[addr] & 0xFFFF
        return None

    # ---------- 写 ----------
    def write_reg(self, addr, value):
        if addr == A_CMD:
            self.cmd = value
            self.apply_cmd(value)
            log("u%d CMD <- %d" % (self.unit, value))
            return True
        if addr == A_FREQSET:
            self.setpoint = struct.unpack(">h", struct.pack(">H", value))[0]
            log("u%d FREQSET <- %d (%.2f%%)" % (self.unit, self.setpoint,
                                                self.setpoint / 100.0))
            return True
        if addr == A_DO:
            self.do_bits = value
            return True
        if addr == A_PWD:
            log("u%d PWD <- 0x%04X (8888=pass)" % (self.unit, value))
            return True
        if addr == A_INIT:
            self.init_cmd = value
            log("u%d INIT <- %d" % (self.unit, value))
            return True
        if addr in (P_MAXFREQ, P_ACCEL, P_DECEL, P_JOG, P_LOCAL) or \
           P_PRESET0 <= addr < P_PRESET0 + 16:
            self.params[addr] = value
            log("u%d PARAM 0x%04X <- %d" % (self.unit, addr, value))
            return True
        return False

    def apply_cmd(self, cmd):
        self.jog_active = cmd in (3, 4)
        if cmd == 1 or cmd == 3:
            self.state = 1
        elif cmd == 2 or cmd == 4:
            self.state = 2
        elif cmd in (5, 6):
            self.state = 3
        elif cmd == 7:
            self.fault = 0
            self.state = 3


def read_control(units, muted):
    """文件控制：故障注入（按站）/ 静默（按站）。"""
    for d in units.values():
        p = "%s/fault_u%d" % (BASE, d.unit)
        try:
            with open(p) as f:
                val = int(f.read().strip(), 0)
            os.remove(p)
            d.fault = val & 0xFFFF
            if val:
                d.state = 3
            log("u%d FAULT INJECT %d" % (d.unit, val))
        except (OSError, ValueError):
            pass
    mp = BASE + "/mute"
    try:
        with open(mp) as f:
            muted["unit"] = int(f.read().strip())
        muted["until_checked"] = False
    except (OSError, ValueError):
        muted["unit"] = 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--unit", type=int, default=0,
                    help="只模拟指定站号（0=双站自建 pty 模式）")
    ap.add_argument("--pty", default="",
                    help="使用外部 pty（虚拟 485 总线 hub 分配的从站端）")
    args = ap.parse_args()
    os.makedirs(BASE, exist_ok=True)
    import tty
    if args.pty:
        # 虚拟总线模式：独占 hub 分配的 pty，只模拟本站
        name = args.pty
        master = os.open(name, os.O_RDWR | os.O_NOCTTY)
        tty.setraw(master)
        units = {args.unit: M512(args.unit)}
        log("BUS SLAVE unit=%d pty=%s", args.unit, name)
    else:
        master, slave = os.openpty()
        tty.setraw(master)
        name = os.ttyname(slave)
        with open(BASE + "/pty_path", "w") as f:
            f.write(name)
        units = {u: M512(u) for u in UNITS}
        log("PTY SLAVE %s (raw), units=%s" % (name, UNITS))
    muted = {"unit": 0}
    buf = b""
    # 请求到达顺序日志（供 C 端断言优先级插队顺序）：每行 "u<unit> fc<fc>"
    order_path = BASE + "/req_order.log"
    order = open(order_path, "a", buffering=1)
    while True:
        read_control(units, muted)
        try:
            data = os.read(master, 256)
        except OSError:
            time.sleep(0.01)
            continue
        if not data:
            continue
        read_control(units, muted)
        buf += data
        while len(buf) >= 4:
            unit = buf[0]
            fc = buf[1]
            if fc in (3, 6):
                need = 8
            elif fc == 16:
                if len(buf) < 7:
                    break
                need = 9 + buf[6]
            else:
                need = 8
            if len(buf) < need:
                break
            frame = buf[:need]
            buf = buf[need:]
            try:
                order.write("u%d fc%d\n" % (unit, fc))
            except OSError:
                pass
            if unit not in units:
                continue   # 地址不匹配 → 保持静默（真实 485 语义）
            if muted["unit"] == unit:
                log("u%d MUTED (drop fc=%d)" % (unit, fc))
                continue
            pdu = frame[1:-2]
            resp_pdu = units[unit].respond(pdu)
            resp = bytes([unit]) + resp_pdu + crc16(bytes([unit]) + resp_pdu)
            try:
                os.write(master, resp)
            except OSError:
                pass


# 挂 respond 方法（与 atv61 模拟器同款帧处理）
def _respond(self, pdu):
    fc = pdu[0]
    if fc == 3:
        start, count = struct.unpack(">HH", pdu[1:5])
        vals = []
        for i in range(count):
            v = self.read_reg(start + i)
            if v is None:
                return bytes([fc | 0x80, 0x02])   # Illegal data address
            vals.append(v & 0xFFFF)
        return bytes([fc, count * 2]) + struct.pack(">%dH" % len(vals), *vals)
    if fc == 6:
        start, val = struct.unpack(">HH", pdu[1:5])
        if not self.write_reg(start, val):
            return bytes([fc | 0x80, 0x02])
        return pdu[:6]          # FC06 应答回显
    return bytes([fc | 0x80, 0x01])   # Illegal function


M512.respond = _respond

if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass
