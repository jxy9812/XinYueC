#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Modbus TCP 从站测试替身（XModbus 断线重连战役 2026-10-25）。

用法:
    python3 mbtest_slave.py [port] [--rst]

  port    监听端口（默认 15020）
  --rst   关闭时对活动连接置 SO_LINGER(0) 再 close —— 模拟 RST 硬断
          （默认走内核 FIN 优雅关闭，对应「从站进程重启」场景）

行为:
  - 支持 FC1/2/3/4 读功能码，返回确定性数据（DI/DO 交替、寄存器递增）；
  - 支持多客户端并发连接（远程 IO 类设备允许 8 会话）；
  - 全生命周期打时间戳日志：监听启动、客户端接入/断开、每笔请求；
  - stdout 行缓冲 + 显式 flush，供驱动脚本按行追踪。
"""

import socket
import struct
import sys
import threading
import time

PORT = 15020
RST_MODE = False

def log(fmt, *args):
    line = "[%s] %s" % (time.strftime("%H:%M:%S") + (".%03d" % (time.time() % 1 * 1000)), fmt % args if args else fmt)
    print(line, flush=True)

def handle_client(conn, addr):
    conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    log("CLIENT CONNECT %s:%d" % addr[:2])
    try:
        buf = b""
        while True:
            data = conn.recv(1024)
            if not data:
                log("CLIENT EOF %s:%d" % addr[:2])
                break
            buf += data
            while len(buf) >= 7:
                tid, pid, length, uid = struct.unpack(">HHHB", buf[:7])
                if len(buf) < 6 + length:
                    break
                pdu = buf[7:6 + length]
                buf = buf[6 + length:]
                fc = pdu[0]
                if fc in (1, 2, 3, 4):
                    # PDU 布局: fc(1) + 起始地址(2) + 数量(2)
                    count = struct.unpack(">H", pdu[3:5])[0]
                    nbytes = (count + 7) // 8 if fc in (1, 2) else count * 2
                    payload = bytes((nbytes,)) + bytes(
                        (i * 0x11 + tid) & 0xFF for i in range(nbytes))
                    # MBAP length = UnitId(1) + FC(1) + payload
                    resp = struct.pack(">HHHB", tid, 0, len(payload) + 2, uid) + bytes((fc,)) + payload
                elif fc == 5 or fc == 6 or fc == 15 or fc == 16:
                    # 写功能码：原样回显请求前 4 字节（地址/值）
                    resp = struct.pack(">HHHB", tid, 0, 6, uid) + pdu[:5]
                else:
                    resp = struct.pack(">HHHB", tid, 0, 3, uid) + bytes((fc | 0x80, 1))
                conn.sendall(resp)
                log("REQ tid=%d fc=%d uid=%d -> %d bytes" % (tid, fc, uid, len(resp)))
    except (ConnectionResetError, BrokenPipeError, OSError) as e:
        log("CLIENT ERROR %s:%d %s" % (addr[:2] + (e,)))
    finally:
        if RST_MODE:
            try:
                conn.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER,
                                struct.pack("ii", 1, 0))
            except OSError:
                pass
        try:
            conn.close()
        except OSError:
            pass
        log("CLIENT CLOSE %s:%d" % addr[:2])

def main():
    global PORT, RST_MODE
    args = sys.argv[1:]
    if "--rst" in args:
        RST_MODE = True
        args.remove("--rst")
    if args:
        PORT = int(args[0])

    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("0.0.0.0", PORT))
    srv.listen(8)
    log("SLAVE LISTEN 0.0.0.0:%d rst=%s" % (PORT, RST_MODE))

    try:
        while True:
            conn, addr = srv.accept()
            threading.Thread(target=handle_client, args=(conn, addr), daemon=True).start()
    except KeyboardInterrupt:
        pass
    finally:
        log("SLAVE EXIT")

if __name__ == "__main__":
    main()
