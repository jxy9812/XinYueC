# -*- coding: utf-8 -*-
"""最小 Modbus-TCP 从站（验证 .13 聚合用）。

只实现本用例需要的功能码：
  FC01 读线圈     -> 16 个 DO（bit0..3 + bit15 置位，便于区分来自哪台）
  FC02 读离散输入 -> 16 个 DI（bit0..7 置位）
  FC05 写单线圈   -> 记录，回显
其余回 EXC01。与现场模块一致：**单次最多 16 点**，超了回 EXC03。

用法： python -I verify/digital_io/mb_tcp_slave.py [port]
"""
import socket
import struct
import sys
import threading

DI_BITS = 0x00FF      # bit0..7 通
DO_BITS = 0x800F      # bit0..3 + bit15 通（bit15 是"这台"的标志位）


class Slave(object):
    def __init__(self, port):
        self.port = port
        self.di = DI_BITS
        self.do = DO_BITS
        self.lock = threading.Lock()

    def handle(self, conn):
        conn.settimeout(10)
        buf = b""
        try:
            while True:
                while len(buf) < 7:
                    k = conn.recv(4096)
                    if not k:
                        return
                    buf += k
                tid, proto, length, unit = struct.unpack(">HHHB", buf[:7])
                while len(buf) < 6 + length:
                    k = conn.recv(4096)
                    if not k:
                        return
                    buf += k
                pdu = buf[7:6 + length]
                buf = buf[6 + length:]
                resp = self.pdu(pdu, unit)
                if resp is None:
                    continue
                out = struct.pack(">HHHB", tid, 0, len(resp) + 1, unit) + resp
                conn.sendall(out)
        except (socket.timeout, ConnectionResetError, OSError):
            return
        finally:
            try:
                conn.close()
            except Exception:
                pass

    def pdu(self, pdu, unit):
        if len(pdu) < 2:
            return None
        fc = pdu[0]
        if fc in (1, 2):
            if len(pdu) < 5:
                return bytes([fc | 0x80, 0x03])
            addr, qty = struct.unpack(">HH", pdu[1:5])
            if qty < 1 or qty > 16 or addr + qty > 16:
                return bytes([fc | 0x80, 0x03])   # 与现场一致：超 16 点 EXC03
            with self.lock:
                src = self.do if fc == 1 else self.di
            bits = [(src >> (addr + i)) & 1 for i in range(qty)]
            nbytes = (qty + 7) // 8
            out = bytearray([fc, nbytes])
            for b in range(nbytes):
                v = 0
                for i in range(8):
                    idx = b * 8 + i
                    if idx < qty and bits[idx]:
                        v |= 1 << i
                out.append(v)
            return bytes(out)
        if fc == 5:
            if len(pdu) < 5:
                return bytes([fc | 0x80, 0x03])
            addr, val = struct.unpack(">HH", pdu[1:5])
            if addr >= 16 or val not in (0xFF00, 0x0000):
                return bytes([fc | 0x80, 0x03])
            with self.lock:
                if val == 0xFF00:
                    self.do |= (1 << addr)
                else:
                    self.do &= ~(1 << addr)
            return pdu[:5]
        return bytes([fc | 0x80, 0x01])


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 502
    s = Slave(port)
    srv = socket.socket()
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("0.0.0.0", port))
    srv.listen(8)
    print("slave listening on %d  DI=0x%04X DO=0x%04X" % (port, DI_BITS, DO_BITS))
    sys.stdout.flush()
    while True:
        conn, _ = srv.accept()
        threading.Thread(target=s.handle, args=(conn,), daemon=True).start()


if __name__ == "__main__":
    main()
