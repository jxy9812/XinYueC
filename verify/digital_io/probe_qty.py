# -*- coding: utf-8 -*-
"""临时探针：实测 .12 / .13 的 16 点与 32 点读能力（用完即删）。"""
import socket
import struct
import sys


def read(host, fc, addr, qty, port=502, timeout=2.0):
    tid = 1
    try:
        s = socket.create_connection((host, port), timeout=timeout)
    except OSError as exc:
        return "CONNECT-FAIL(%s)" % exc
    try:
        pdu = struct.pack(">BHH", fc, addr, qty)
        s.sendall(struct.pack(">HHHB", tid, 0, len(pdu) + 1, 1) + pdu)
        head = b""
        while len(head) < 7:
            k = s.recv(7 - len(head))
            if not k:
                return "EMPTY"
            head += k
        r_tid, _, length, _ = struct.unpack(">HHHB", head)
        body = b""
        while len(body) < length - 1:
            k = s.recv(length - 1 - len(body))
            if not k:
                return "INCOMPLETE(%d/%d)" % (len(body), length - 1)
            body += k
        if len(body) >= 2 and body[0] & 0x80:
            return "EXC%02d" % body[1]
        nbytes = body[1] if len(body) > 1 else 0
        data = body[2:2 + nbytes]
        val = 0
        for i, b in enumerate(data):
            val |= b << (8 * i)
        return "OK bytes=%d mask=0x%0*X %s" % (
            nbytes, nbytes * 2, val, " ".join("%02X" % b for b in data))
    finally:
        s.close()


def main():
    host = sys.argv[1] if len(sys.argv) > 1 else "192.168.0.12"
    print("=== %s ===" % host)
    for label, fc, addr, qty in (
            ("FC02 DI  16", 2, 0, 16),
            ("FC02 DI  32", 2, 0, 32),
            ("FC02 DI  @16x16", 2, 16, 16),
            ("FC01 DO  16", 1, 0, 16),
            ("FC01 DO  32", 1, 0, 32),
            ("FC01 DO  @16x16", 1, 16, 16)):
        print("  %-16s -> %s" % (label, read(host, fc, addr, qty)))


if __name__ == "__main__":
    main()
