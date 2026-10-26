#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ATV61 变频器 Modbus RTU 从站模拟器（闭环测试用）。

模拟施耐德 ATV61 内置 Modbus（RJ45/RS485 两线制）的寄存器模型：
  · 逻辑地址体系（手册 W 号 = Modbus PDU 地址，不做 -1 偏移）
  · Drivecom/CiA402 命令字状态机：写 CMD 8501 推进 ETA 3201 的运行状态
  · 频率给定字 8521(LFR1) / 8502(LFR) / 转速给定 8602(LFRD)
  · 状态块 3201..3209（ETA RFR FRH LCR OTR ETI ULN UOP THD）
  · 故障码 7121(LFT)、电机热状态 9630(THR)

传输：os.openpty() 虚拟串口（master 侧设 raw，交付原始字节），把从端路径写入
      $BASE/pty_path，C 测试程序从该文件取路径打开。

控制接口（文件触摸式，操作后自删）：
  $BASE/fault      内容=整数 → 注入故障（写进 LFT，并把 ETA bit3 置 1）
  $BASE/clear      存在 → 清除故障
  $BASE/cmdlog     从站打印收到的写请求（调试用）
"""

import os
import struct
import sys
import time

BASE = os.environ.get("ATV61_BASE", "/tmp/mbsoak/atv61")

# ---- ATV61 逻辑地址 ----
A_CMD = 8501
A_LFR = 8502
A_ETA = 3201
A_RFR = 3202
A_FRH = 3203
A_LCR = 3204
A_OTR = 3205
A_ETI = 3206
A_ULN = 3207
A_UOP = 3208
A_THD = 3209
A_OPR = 3211
A_LFT = 7121
A_THR = 9630
A_LFRD = 8602
A_LFR1 = 8521

# ---- Drivecom 状态 ----
ST_DISABLED = 2
ST_READY = 3
ST_SWITCHED = 4
ST_RUNNING = 5

def log(fmt, *args):
    print("[atv61 %08.3f] %s" % (time.time() % 100, fmt % args if args else fmt), flush=True)

def crc16(data):
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return struct.pack("<H", crc)

class Atv61:
    def __init__(self, unit_id=1):
        self.unit = unit_id
        self.state = ST_DISABLED
        self.cmd = 0
        self.ref_hz = 0.0          # 频率给定（Hz）
        self.ref_rpm = 0           # 转速给定（rpm）
        self.fault = 0
        self.motor_thermal = 30
        self.drive_thermal = 25
        self.reverse = False

    # ---------- 寄存器视图 ----------
    def read_reg(self, addr):
        if addr == A_CMD:
            return self.cmd
        if addr in (A_LFR, A_LFR1):
            return int(round(self.ref_hz * 10)) & 0xFFFF
        if addr == A_LFRD:
            return self.ref_rpm & 0xFFFF
        if addr == A_ETA:
            return self.eta()
        if addr == A_RFR:
            # 输出频率跟随给定（简化：一阶跟随）
            return int(round(self.ref_hz * 10)) & 0xFFFF
        if addr == A_FRH:
            return int(round(self.ref_hz * 10)) & 0xFFFF
        if addr == A_LCR:
            return 0x0064            # 电机电流 100
        if addr == A_OTR:
            return 0x0045            # 转矩 6.9%
        if addr == A_ETI:
            return 0x0000
        if addr == A_ULN:
            return 0x00F0            # 电网电压 24.0 V
        if addr == A_UOP:
            return 0x00F0
        if addr == A_THD:
            return self.drive_thermal
        if addr == A_OPR:
            return 50
        if addr == A_LFT:
            return self.fault
        if addr == A_THR:
            return self.motor_thermal
        return None

    def eta(self):
        eta = 0
        if self.state == ST_DISABLED:
            eta |= 0x0040          # Switch on disabled
        elif self.state == ST_READY:
            eta |= 0x0021          # Ready to switch on
        elif self.state == ST_SWITCHED:
            eta |= 0x0023          # Switched on
        elif self.state == ST_RUNNING:
            eta |= 0x0027          # Operation enabled
        eta |= 0x0010              # Voltage enabled
        if self.state != ST_DISABLED:
            eta |= 0x0020          # Quick stop 未激活
        if self.fault:
            eta |= 0x0008          # Fault
        if self.state == ST_RUNNING:
            eta |= 0x0200          # Remote（命令来自总线）
            eta |= 0x0400          # Target reached
        if self.reverse:
            eta |= 0x4000
        return eta & 0xFFFF

    # ---------- 写寄存器 ----------
    def write_reg(self, addr, value):
        if addr == A_CMD:
            self.cmd = value
            self.apply_cmd(value)
            log("CMD <- 0x%04X" % value)
            return True
        if addr in (A_LFR, A_LFR1):
            self.ref_hz = struct.unpack(">h", struct.pack(">H", value))[0] / 10.0
            log("REF <- %.1f Hz (addr %d)" % (self.ref_hz, addr))
            return True
        if addr == A_LFRD:
            self.ref_rpm = struct.unpack(">h", struct.pack(">H", value))[0]
            log("REFRPM <- %d rpm" % self.ref_rpm)
            return True
        return False

    def apply_cmd(self, cmd):
        # CiA402 状态机（简化）
        bit3 = (cmd >> 3) & 1
        bit1 = (cmd >> 1) & 1
        bit0 = cmd & 1
        bit2 = (cmd >> 2) & 1
        bit7 = (cmd >> 7) & 1
        self.reverse = bool((cmd >> 11) & 1)
        if self.fault and not bit7:
            return
        if bit7:                       # Fault reset 上升沿
            self.fault = 0
            self.state = ST_DISABLED
            return
        if not bit1 and not bit0:     # Disable voltage → 状态 2
            self.state = ST_DISABLED
        elif not bit3:                # Shutdown / Switch on / Disable operation
            if bit0:
                self.state = ST_SWITCHED if bit1 else ST_READY
            else:
                self.state = ST_READY
        else:                         # Enable operation
            self.state = ST_RUNNING

    # ---------- Modbus 应答 ----------
    def respond(self, pdu):
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
            self.write_reg(start, val)
            return pdu[:6]          # FC06 应答回显完整 6 字 PDU
        if fc == 16:
            start, qty = struct.unpack(">HH", pdu[1:5])
            vals = struct.unpack(">%dH" % qty, pdu[6:6 + qty * 2])
            for i, v in enumerate(vals):
                self.write_reg(start + i, v)
            return pdu[:5]          # FC16 应答 = fc + 起始地址 + 数量
        return bytes([fc | 0x80, 0x01])   # Illegal function

    def apply_control(self):
        """文件控制：注入故障 / 清除故障。"""
        p = BASE + "/fault"
        try:
            with open(p) as f:
                val = int(f.read().strip(), 0)
            os.remove(p)
            self.fault = val
            if val:
                self.state = ST_DISABLED
            log("FAULT INJECT %d (%s)" % (val, "OCF" if val == 9 else "code"))
        except (OSError, ValueError):
            pass
        if os.path.exists(BASE + "/clear"):
            os.remove(BASE + "/clear")
            self.fault = 0
            log("FAULT CLEAR")


def main():
    os.makedirs(BASE, exist_ok=True)
    import tty
    master, slave = os.openpty()
    tty.setraw(master)
    name = os.ttyname(slave)
    with open(BASE + "/pty_path", "w") as f:
        f.write(name)
    log("PTY SLAVE %s (raw), unit=1" % name)

    drive = Atv61(1)
    buf = b""
    while True:
        drive.apply_control()
        try:
            data = os.read(master, 256)
        except OSError:
            time.sleep(0.01)
            continue
        if not data:
            continue
        # 请求到达后再取一次控制文件：保证「注入故障/清故障」对本次请求
        # 立即生效（否则注入落在下一次请求，与读状态分两拍，出现
        # 「ETA 无故障位但 LFT 已有码」的错拍现象）。
        drive.apply_control()
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
            if unit != drive.unit:
                continue
            pdu = frame[1:-2]
            resp_pdu = drive.respond(pdu)
            resp = bytes([unit]) + resp_pdu + crc16(bytes([unit]) + resp_pdu)
            log("REQ fc=%d -> resp %d bytes" % (fc, len(resp)))
            try:
                os.write(master, resp)
            except OSError:
                pass


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass
