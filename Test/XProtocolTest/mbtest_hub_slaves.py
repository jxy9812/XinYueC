#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""XModbusHub 闭环测试从站拓扑（2026-10-26 聚合设备战役）。

三个从站，验证「用户不管底层是 RTU 还是 TCP」：
  A: TCP 127.0.0.1:P1  unit1   DI8 + IR16 + Coils8 + HR10
  B: TCP 127.0.0.1:P2  unit1   DI8 + IR16
  C: RTU pty slave     unit5   DI8 + IR16 + Coils8 + HR10（os.openpty 虚拟串口）

控制接口（文件触摸式，由测试程序/驱动脚本操作，每次生效后自删）：
  BASE/di_A|di_B|di_C      内容=整数 → 覆盖 DI 位图（值变化轮询测试）
  BASE/pauseA              存在期间 A 静默吞请求（不回帧）→ 写重发测试
  BASE/downA               出现时 A 关监听+断全部连接（模拟从站宕机）；
                           移除后自动恢复监听 → 断线重连测试
DI 初始位图：A=0xA5 B=0x5A C=0xC3；IR 初始值：i*3+7。日志毫秒时间戳行缓冲。
"""

import os
import struct
import sys
import threading
import time

BASE = "/tmp/mbsoak/hub"

def log(tag, fmt, *args):
    t = time.time()
    print("[%s %08.3f] %s" % (tag, t % 100, fmt % args if args else fmt), flush=True)

def crc16(data):
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return struct.pack("<H", crc)

class ModbusStore:
    """单从站寄存器模型。

    [2026-10-29 缓存层战役] 数组放大到 di/coils=32、ir/hr=64：缓存层回归要验
    「相邻块按 gap 合并成少帧」（远端需留洞，如 remote 0..7 + 16..23），
    8/10 点旧容量放不下洞后的高段。初值口径不变（ir/hr 初值 i*3+7），旧用例
    断言（A DI=0xA5 / IR v0=7）不受影响。
    """

    def __init__(self, tag, di_init=0, di_n=32, ir_n=64, coils_n=32, hr_n=64):
        self.tag = tag
        self.di = [(di_init >> i) & 1 for i in range(di_n)]
        self.ir = [i * 3 + 7 for i in range(ir_n)]
        self.coils = [0] * coils_n
        self.hr = [0] * hr_n
        self.lock = threading.Lock()

    def frame_log(self, fc, start, count):
        """帧日志：[2026-10-29] 缓存层帧数断言依据（每请求一行，行缓冲）。"""
        try:
            with open(BASE + "/frames_" + self.tag + ".log", "a") as f:
                f.write("fc=%d start=%d count=%d\n" % (fc, start, count))
        except OSError:
            pass

    def respond(self, addr, pdu):
        """pdu=请求 PDU（fc 起）。返回应答 PDU；None=静默吞（pause）。"""
        if os.path.exists(BASE + "/pause" + self.tag):
            return None
        fc = pdu[0]
        with self.lock:
            if fc in (1, 2, 3, 4):
                start, count = struct.unpack(">HH", pdu[1:5])
                self.frame_log(fc, start, count)
                if fc in (1, 2):
                    vals = self.coils if fc == 1 else self.di
                    nbytes = (count + 7) // 8
                    bits = vals[start:start + count]
                    bytearr = bytearray(nbytes)
                    for i, v in enumerate(bits):
                        if v:
                            bytearr[i // 8] |= 1 << (i % 8)
                    return bytes([fc, nbytes]) + bytes(bytearr)
                vals = self.hr if fc == 3 else self.ir
                out = vals[start:start + count]
                return bytes([fc, count * 2]) + struct.pack(">%dH" % len(out), *out)
            if fc == 5:
                start, val = struct.unpack(">HH", pdu[1:5])
                self.frame_log(fc, start, 1)
                if start < len(self.coils):
                    self.coils[start] = 1 if val else 0
                log(self.tag, "WRITE COIL[%d]=%d" % (start, 1 if val else 0))
                return pdu[:4]
            if fc == 6:
                start, val = struct.unpack(">HH", pdu[1:5])
                self.frame_log(fc, start, 1)
                if start < len(self.hr):
                    self.hr[start] = val
                log(self.tag, "WRITE HR[%d]=%d" % (start, val))
                return pdu[:4]
            if fc == 15:
                # [2026-10-29 缓存层战役] FC15 写多线圈：缓存层把相邻脏点
                # 合并成一帧下发，从站必须按位解包（旧脚本只认 FC5/6/16）。
                start, qty, nbytes = struct.unpack(">HHB", pdu[1:6])
                self.frame_log(fc, start, qty)
                data = pdu[6:6 + nbytes]
                for i in range(qty):
                    bit = (data[i // 8] >> (i % 8)) & 1 if i // 8 < len(data) else 0
                    if start + i < len(self.coils):
                        self.coils[start + i] = bit
                log(self.tag, "WRITE COILS[%d..%d]" % (start, start + qty - 1))
                return pdu[:5]
            if fc == 16:
                start, qty = struct.unpack(">HH", pdu[1:5])
                self.frame_log(fc, start, qty)
                vals = struct.unpack(">%dH" % qty, pdu[6:6 + qty * 2])
                for i, v in enumerate(vals):
                    if start + i < len(self.hr):
                        self.hr[start + i] = v
                log(self.tag, "WRITE HR[%d..]=%s" % (start, list(vals)))
                return pdu[:5]
        return bytes([fc | 0x80, 1])

    def apply_control(self):
        """文件控制：改 DI。"""
        p = BASE + "/di_" + self.tag
        try:
            with open(p) as f:
                val = int(f.read().strip(), 0)
            os.remove(p)
            with self.lock:
                for i in range(len(self.di)):
                    self.di[i] = (val >> i) & 1
            log(self.tag, "DI SET 0x%X" % val)
        except (OSError, ValueError):
            pass

def tcp_slave(port, store):
    import socket
    tag = store.tag
    conns = []
    srv = None
    down_prev = False
    while True:
        down = os.path.exists(BASE + "/down" + tag)
        if down and not down_prev:
            log(tag, "DOWN (close listener+conns)")
            for c in conns:
                try: c.close()
                except OSError: pass
            conns.clear()
            if srv:
                try: srv.close()
                except OSError: pass
                srv = None
            down_prev = True
        if not down and down_prev:
            log(tag, "UP (re-listen)")
            down_prev = False
        if down:
            store.apply_control()
            time.sleep(0.05)
            continue
        if srv is None:
            srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            # [2026-10-26 加固] 前轮崩溃残留监听会让 bind 失败、线程静默死亡、
            # 主站重连风暴。重试 3s；仍失败打日志退出（驱动预检兜底）。
            bound = False
            for _ in range(30):
                try:
                    srv.bind(("127.0.0.1", port))
                    bound = True
                    break
                except OSError as e:
                    log(tag, "BIND RETRY %s (%s)" % (port, e))
                    time.sleep(0.1)
            if not bound:
                log(tag, "BIND FAILED port=%d, thread exit" % port)
                return
            srv.listen(4)
            log(tag, "LISTEN 127.0.0.1:%d" % port)

        def client(conn):
            buf = b""
            try:
                while True:
                    data = conn.recv(1024)
                    if not data:
                        log(tag, "CLIENT EOF")
                        break
                    buf += data
                    while len(buf) >= 7:
                        tid, pid, length, uid = struct.unpack(">HHHB", buf[:7])
                        if len(buf) < 6 + length:
                            break
                        pdu = buf[7:6 + length]
                        buf = buf[6 + length:]
                        log(tag, "REQ tid=%d fc=%d" % (tid, pdu[0]))
                        resp_pdu = store.respond(uid, pdu)
                        if resp_pdu is None:
                            log(tag, "(paused, no reply)")
                            continue
                        resp = struct.pack(">HHHB", tid, 0, len(resp_pdu) + 1, uid) + resp_pdu
                        conn.sendall(resp)
            except OSError as e:
                log(tag, "CONN ERR %s" % e)
            finally:
                try:
                    conn.close()
                except OSError:
                    pass

        srv.settimeout(0.1)
        try:
            conn, _ = srv.accept()
            conns.append(conn)
            threading.Thread(target=client, args=(conn,), daemon=True).start()
        except socket.timeout:
            pass
        except OSError as e:
            log(tag, "ACCEPT ERR %s" % e)
            time.sleep(0.05)
        store.apply_control()

def rtu_slave(store):
    import tty
    master, slave = os.openpty()
    # [2026-10-26 行规程] pty 缺省 canonical+echo 会吞二进制帧（无换行不交付、
    # 自发回显），框架侧收不到应答 → 超时重发。master 设 raw 交付原始字节。
    tty.setraw(master)
    name = os.ttyname(slave)
    with open(BASE + "/rtu_pty_path", "w") as f:
        f.write(name)
    log(store.tag, "PTY SLAVE %s (raw)" % name)
    buf = b""
    while True:
        store.apply_control()
        try:
            data = os.read(master, 256)
        except OSError:
            time.sleep(0.01)
            continue
        buf += data
        while buf:
            if len(buf) < 2:
                break
            fc = buf[1]
            if fc in (1, 2, 3, 4, 5, 6):
                need = 8
            elif fc in (15, 16):
                if len(buf) < 7:
                    break
                need = 9 + buf[6]
            else:
                buf = buf[1:]
                continue
            if len(buf) < need:
                break
            frame, buf = buf[:need], buf[need:]
            if crc16(frame[:-2]) != frame[-2:]:
                log(store.tag, "CRC BAD %s" % frame.hex())
                continue
            addr = frame[0]
            log(store.tag, "REQ addr=%d fc=%d" % (addr, fc))
            resp_pdu = store.respond(addr, frame[1:-2])
            if resp_pdu is None:
                log(store.tag, "(paused, no reply)")
                continue
            resp = bytes([addr]) + resp_pdu
            resp += crc16(resp)
            os.write(master, resp)

def main():
    os.makedirs(BASE, exist_ok=True)
    # [2026-10-29] 帧日志跨轮清理：帧数断言按「本轮增量」读，不混上轮残留。
    for tag in ("A", "B", "C"):
        try:
            os.remove(BASE + "/frames_" + tag + ".log")
        except OSError:
            pass
    p1 = int(sys.argv[1]) if len(sys.argv) > 1 else 15331
    p2 = int(sys.argv[2]) if len(sys.argv) > 2 else 15332

    a = ModbusStore("A", di_init=0xA5)
    b = ModbusStore("B", di_init=0x5A)
    c = ModbusStore("C", di_init=0xC3)

    threading.Thread(target=tcp_slave, args=(p1, a), daemon=True).start()
    threading.Thread(target=tcp_slave, args=(p2, b), daemon=True).start()
    rtu_slave(c)  # RTU 主循环占用本线程，常驻

if __name__ == "__main__":
    main()
