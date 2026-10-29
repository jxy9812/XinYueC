#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""战役 1028 端到端联调多模式虚拟从站（xprotocol_campaign_test.c 配套）。

拓扑（单进程全托管，驱动脚本 mbtest_campaign_run.sh 先于 C 测试启动）：
  bus1: pty 虚拟 485 总线，unit 1..8（S1/S2/S3/S5/S6/S7/S8/S9/S10 用）
  bus2: pty 虚拟 485 总线，unit 1..8（S4 发送失败专用，支持关 master）
  bus3: pty 虚拟 485 总线，unit 1..8（S4 恢复段专用新链路）
  tcp : 127.0.0.1:<port> Modbus/TCP，unit 1（S10 连接抖动用）

pty 端路径写 $BASE/pty{1,2,3}_path；全部就绪后写 $BASE/ready（驱动预检依据）。

帧日志（S5/S6 断言依据）：$BASE/frame_bus{1,2,3}.log / frame_tcp.log，
行缓冲逐行落盘，每行格式：
  SEQ=<序号> TS=<秒%08.3f> RX addr=<站号> fc=<功能码> hex=<帧16进制>
  SEQ=<序号> TS=<秒%08.3f> TX addr=<站号> fc=<功能码> hex=<帧16进制>
C 端以「文件字节偏移切片 + 行内容匹配」做时序断言（跨进程时钟不比对）。

控制接口（文件触摸式，$BASE 下，轮询周期 20ms）：
  bus<i>_mute_u<k>      存在期间 unit k 静默不应答（S2/S5/S8）
  bus<i>_badcrc_u<k>    存在期间 unit k 应答帧 CRC 翻坏（S3/S10）
  bus<i>_swalloww_u<k>  存在期间 unit k 吞掉写请求（FC5/6/16 不应答，S7）
  bus2_closem           存在时关闭 bus2 的 pty master fd（S4 发送失败注入）
广播（addr=0）支持：FC5/6/16 广播帧应用到本总线全部 unit，不回帧（真 485 语义）；
站号不在 1..8 保持静默。

寄存器模型（每 unit 独立）：HR64 初值 i*3+7；IR32 初值 i*3+7；
Coils16/DI16 初值 0；DI 位图提供 (unit+i)&1 模式便于肉眼核对。
"""

import argparse
import os
import select
import struct
import sys
import threading
import time
import tty

HR_N = 64
IR_N = 32
BIT_N = 16


def log(fmt, *args):
    print("[campaign-slave %08.3f] %s"
          % (time.time() % 100, fmt % args if args else fmt), flush=True)


def crc16(data):
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return struct.pack("<H", crc)


class Unit:
    """单站寄存器模型（四类区，独立实例）。"""

    def __init__(self, unit_id):
        self.unit = unit_id
        self.hr = [(i * 3 + 7) & 0xFFFF for i in range(HR_N)]
        self.ir = [(i * 3 + 7) & 0xFFFF for i in range(IR_N)]
        self.coils = [0] * BIT_N
        self.di = [(unit_id + i) & 1 for i in range(BIT_N)]
        self.lock = threading.Lock()

    def respond(self, pdu):
        """pdu=请求 PDU（fc 起）。返回应答 PDU；None=拒绝/不归本站语义。"""
        fc = pdu[0]
        with self.lock:
            if fc in (1, 2, 3, 4):
                start, count = struct.unpack(">HH", pdu[1:5])
                if fc in (1, 2):
                    vals = self.coils if fc == 1 else self.di
                    nbytes = (count + 7) // 8
                    bytearr = bytearray(nbytes)
                    for i, v in enumerate(vals[start:start + count]):
                        if v:
                            bytearr[i // 8] |= 1 << (i % 8)
                    return bytes([fc, nbytes]) + bytes(bytearr)
                vals = self.hr if fc == 3 else self.ir
                out = vals[start:start + count]
                if len(out) != count:
                    return bytes([fc | 0x80, 0x02])   # Illegal data address
                return bytes([fc, count * 2]) + struct.pack(">%dH" % len(out), *out)
            if fc == 5:
                start, val = struct.unpack(">HH", pdu[1:5])
                if start >= len(self.coils):
                    return bytes([fc | 0x80, 0x02])
                self.coils[start] = 1 if val else 0
                return pdu[:5]      # FC05 回显=fc+addr+val 共 5 字节（帧长 8）
            if fc == 6:
                start, val = struct.unpack(">HH", pdu[1:5])
                if start >= len(self.hr):
                    return bytes([fc | 0x80, 0x02])
                self.hr[start] = val
                return pdu[:5]      # FC06 回显=fc+addr+val 共 5 字节（帧长 8）
            if fc == 16:
                start, qty = struct.unpack(">HH", pdu[1:5])
                vals = struct.unpack(">%dH" % qty, pdu[6:6 + qty * 2])
                for i, v in enumerate(vals):
                    if start + i < len(self.hr):
                        self.hr[start + i] = v
                return pdu[:5]
        return bytes([fc | 0x80, 0x01])   # Illegal function


class Bus:
    """一条 pty 虚拟 485 总线：8 站 + 帧日志 + 文件控制 + 广播 + 关 master。"""

    def __init__(self, idx, base):
        self.idx = idx
        self.base = base
        self.units = {u: Unit(u) for u in range(1, 9)}
        self.seq = 0
        self.master = None

    # ---- 控制文件 ----
    def _ctrl(self, kind, unit):
        return os.path.exists("%s/bus%d_%s_u%d" % (self.base, self.idx, kind, unit))

    def muted(self, unit):
        return self._ctrl("mute", unit)

    def badcrc(self, unit):
        return self._ctrl("badcrc", unit)

    def swalloww(self, unit):
        return self._ctrl("swalloww", unit)

    # ---- 帧日志 ----
    def flog(self, direction, addr, fc, frame):
        self.seq += 1
        try:
            with open("%s/frame_bus%d.log" % (self.base, self.idx), "a",
                      buffering=1) as f:
                f.write("SEQ=%04d TS=%08.3f %s addr=%d fc=%d hex=%s\n"
                        % (self.seq, time.time() % 100, direction, addr, fc,
                           frame.hex()))
        except OSError:
            pass

    # ---- 主循环 ----
    def run(self):
        master, slave = os.openpty()
        tty.setraw(master)
        name = os.ttyname(slave)
        with open("%s/pty%d_path" % (self.base, self.idx), "w") as f:
            f.write(name)
        log("bus%d PTY %s" % (self.idx, name))
        self.master = master
        buf = b""
        while True:
            # S4 注入：closem → 关 master fd（此后 C 端写 slave 得 EIO）
            if os.path.exists("%s/bus%d_closem" % (self.base, self.idx)):
                if self.master is not None:
                    try:
                        os.close(self.master)
                    except OSError:
                        pass
                    self.master = None
                    log("bus%d MASTER CLOSED (closem)" % self.idx)
                    with open("%s/bus%d_closed" % (self.base, self.idx), "w") as f:
                        f.write("1")
                time.sleep(0.05)
                continue
            if self.master is None:
                time.sleep(0.05)
                continue
            try:
                r, _, _ = select.select([self.master], [], [], 0.02)
            except (OSError, ValueError):
                self.master = None
                continue
            if not r:
                continue
            try:
                data = os.read(self.master, 512)
            except OSError:
                self.master = None
                continue
            if not data:
                self.master = None
                continue
            buf += data
            buf = self._drain(buf)

    def _drain(self, buf):
        """按 RTU 帧长切帧：FC1/2/3/4/5/6 定长 8，FC16 变长，其余逐字节重同步。"""
        while len(buf) >= 4:
            fc = buf[1]
            if fc == 16:
                if len(buf) < 7:
                    break
                need = 9 + buf[6]
            elif fc in (1, 2, 3, 4, 5, 6, 15):
                need = 8
            else:
                buf = buf[1:]      # 未知 fc：丢弃一字节重同步
                continue
            if len(buf) < need:
                break
            frame, buf = buf[:need], buf[need:]
            self._serve(frame)
        return buf

    def _serve(self, frame):
        addr = frame[0]
        fc = frame[1]
        self.flog("RX", addr, fc, frame)
        if addr == 0:
            # 广播：写类应用到全部站，不回帧（真 485 语义）
            if fc in (5, 6, 16):
                for unit in self.units.values():
                    unit.respond(frame[1:-2])
                log("bus%d BROADCAST fc=%d applied to all" % (self.idx, fc))
            return
        unit = self.units.get(addr)
        if unit is None:
            return                       # 站号不匹配 → 静默
        if self.muted(addr):
            log("bus%d u%d MUTED (drop fc=%d)" % (self.idx, addr, fc))
            return
        if fc in (5, 6, 15, 16) and self.swalloww(addr):
            log("bus%d u%d SWALLOWED WRITE fc=%d" % (self.idx, addr, fc))
            return
        resp_pdu = unit.respond(frame[1:-2])
        resp = bytes([addr]) + resp_pdu
        crc = crc16(resp)
        if self.badcrc(addr):
            crc = bytes([crc[0] ^ 0xFF, crc[1] ^ 0x55])
            log("bus%d u%d BAD-CRC reply fc=%d" % (self.idx, addr, fc))
        self.flog("TX", addr, fc, resp + crc)
        try:
            os.write(self.master, resp + crc)
        except OSError:
            self.master = None


def tcp_slave(port, base):
    """Modbus/TCP 单 unit1 从站（S10 连接抖动）；帧日志同格式落 frame_tcp.log。"""
    import socket
    store = Unit(1)
    seq = [0]

    def flog(direction, uid, pdu):
        seq[0] += 1
        try:
            with open("%s/frame_tcp.log" % base, "a", buffering=1) as f:
                f.write("SEQ=%04d TS=%08.3f %s addr=%d fc=%d hex=%s\n"
                        % (seq[0], time.time() % 100, direction, uid, pdu[0],
                           pdu.hex()))
        except OSError:
            pass

    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    bound = False
    for _ in range(50):
        try:
            srv.bind(("127.0.0.1", port))
            bound = True
            break
        except OSError as e:
            log("tcp BIND RETRY %d (%s)" % (port, e))
            time.sleep(0.1)
    if not bound:
        log("tcp BIND FAILED port=%d" % port)
        return
    srv.listen(8)
    log("tcp LISTEN 127.0.0.1:%d" % port)

    def client(conn):
        buf = b""
        try:
            while True:
                data = conn.recv(1024)
                if not data:
                    break
                buf += data
                while len(buf) >= 7:
                    tid, _pid, length, uid = struct.unpack(">HHHB", buf[:7])
                    if len(buf) < 6 + length:
                        break
                    pdu = buf[7:6 + length]
                    buf = buf[6 + length:]
                    flog("RX", uid, pdu)
                    resp_pdu = store.respond(pdu)
                    resp = struct.pack(">HHHB", tid, 0, len(resp_pdu) + 1, uid) \
                        + resp_pdu
                    flog("TX", uid, resp_pdu)
                    conn.sendall(resp)
        except OSError:
            pass
        finally:
            try:
                conn.close()
            except OSError:
                pass

    while True:
        try:
            conn, _ = srv.accept()
            threading.Thread(target=client, args=(conn,), daemon=True).start()
        except OSError:
            time.sleep(0.05)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--base", default="/tmp/mbsoak/campaign1028")
    ap.add_argument("--tcp-port", type=int, default=15431)
    args = ap.parse_args()
    base = args.base
    os.makedirs(base, exist_ok=True)
    threads = [threading.Thread(target=Bus(i, base).run, daemon=True)
               for i in (1, 2, 3)]
    for t in threads:
        t.start()
    threading.Thread(target=tcp_slave, args=(args.tcp_port, base),
                     daemon=True).start()
    # 就绪标记：pty 路径文件由各总线线程写出，三条齐了才挂 ready
    for i in (1, 2, 3):
        for _ in range(250):
            if os.path.exists("%s/pty%d_path" % (base, i)):
                break
            time.sleep(0.02)
        if not os.path.exists("%s/pty%d_path" % (base, i)):
            log("FATAL: pty%d path never appeared" % i)
            sys.exit(1)
    with open("%s/ready" % base, "w") as f:
        f.write("ok")
    log("ALL READY base=%s tcp=%d" % (base, args.tcp_port))
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
