#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""寄存器可读性映射 / 块读边界 / 异常码行为（温湿度模块专项）。
"""
import struct
import sys
import time

sys.path.insert(0, __import__("os").path.dirname(__import__("os").path.dirname(__import__("os").path.abspath(__file__))))
from probe_temphum import (build, crc16, decode_u16_temp, decode_u16_hum,
                           f32, read_regs, write_reg, recv_frame, transact)

PORT, BAUD, UNIT, TMO = "COM17", 115200, 4, 0.4


def main():
    import serial
    sp = serial.Serial(PORT, BAUD, bytesize=8, parity="N", stopbits=1,
                       timeout=0.05, write_timeout=1.0)
    time.sleep(0.1)
    try:
        print("=== A. 单寄存器可读性映射 FC03 / FC04  0000H~0060H ===")
        print("addr   FC03          FC04")
        for addr in range(0x0000, 0x0061):
            out = []
            for fc in (0x03, 0x04):
                regs, rtt, err = read_regs(sp, UNIT, fc, addr, 1, TMO)
                if err == "timeout":
                    out.append("--")
                elif err:
                    out.append(err.split()[0])
                else:
                    out.append("%5d" % regs[0])
            print("%04XH  %-12s  %s" % (addr, out[0], out[1]))

        print("\n=== B. 块读长度边界（起始 0x0000）===")
        for n in (1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 16):
            regs, rtt, err = read_regs(sp, UNIT, 0x04, 0x0000, n, TMO)
            print("  FC04 0000H count=%-3d -> %s" %
                  (n, "OK %s (%.1fms)" % (regs, rtt) if not err else err))

        print("\n=== C. 跨空隙块读（0x0005 起）===")
        for start, n in ((0x0005, 1), (0x0005, 2), (0x0006, 1), (0x0007, 1),
                         (0x0009, 3), (0x0009, 4), (0x000A, 2),
                         (0x000B, 2), (0x003C, 2), (0x003D, 1)):
            regs, rtt, err = read_regs(sp, UNIT, 0x03, start, n, TMO)
            print("  FC03 %04XH count=%-2d -> %s" %
                  (start, n, "OK %s" % regs if not err else err))

        print("\n=== D. 越界 / 非法请求行为 ===")
        cases = [("FC03 无此地址 0x0100", 0x03, 0x0100, 1),
                 ("FC03 0x0063", 0x03, 0x0063, 1),
                 ("FC04 0x0100", 0x04, 0x0100, 1),
                 ("FC04 数量=0", 0x04, 0x0000, 0),
                 ("FC04 数量=126", 0x04, 0x0000, 126),
                 ("FC04 数量=125", 0x04, 0x0000, 125)]
        for label, fc, addr, n in cases:
            raw, rtt, err = transact(sp, UNIT, fc, addr, n, TMO)
            detail = err or "OK raw=%s" % (raw.hex() if raw else "")
            if raw and len(raw) > 3 and raw[1] & 0x80 and len(raw) >= 3:
                detail = "异常码 0x%02X (%s)" % (raw[2], err)
            print("  %-22s -> %s" % (label, detail))

        print("\n=== E. 非法功能码 ===")
        for fc in (0x01, 0x02, 0x05, 0x0F, 0x10, 0x11, 0x63):
            raw, rtt, err = transact(sp, UNIT, fc, 0x0000, 2, TMO)
            print("  FC%02X -> %s" % (fc, err or "OK raw=%s" % (raw.hex() if raw else "")))

        print("\n=== F. FC10 写多寄存器（写回原值，无副作用）===")
        regs, _, err = read_regs(sp, UNIT, 0x03, 0x000A, 1, TMO)
        if err:
            print("  读站号失败，跳过: %s" % err)
        else:
            body = struct.pack(">BBHHB", UNIT, 0x10, 0x000A, 1, 2) + \
                struct.pack(">H", regs[0])
            frame = body + struct.pack("<H", crc16(body))
            sp.reset_input_buffer()
            sp.write(frame)
            sp.flush()
            raw = recv_frame(sp, 8, TMO)
            ok = raw and len(raw) >= 8 and \
                crc16(raw[:-2]) == struct.unpack("<H", raw[-2:])[0]
            print("  FC10 000AH=%d -> %s" % (regs[0],
                                             "OK " + raw.hex() if ok else
                                             "失败 %r" % raw))
            regs2, _, _ = read_regs(sp, UNIT, 0x03, 0x000A, 1, TMO)
            print("  复核站号 = %s（应为 %d）" % (regs2, regs[0]))

        print("\n=== G. 错站号 / 广播 ===")
        for u in (3, 5, 0, 255):
            raw, rtt, err = transact(sp, u, 0x04, 0x0000, 2, 0.3)
            print("  unit=%-3d -> %s" % (u, err or "OK raw=%s" % (raw.hex() if raw else "")))

        print("\n=== H. 高频轮询（背靠背 200 次，不留间隔）===")
        rtts, fails = [], 0
        t0 = time.perf_counter()
        for _ in range(200):
            regs, rtt, err = read_regs(sp, UNIT, 0x04, 0x0000, 6, TMO)
            if err:
                fails += 1
            else:
                rtts.append(rtt)
        dur = time.perf_counter() - t0
        s = sorted(rtts)
        print("  成功 %d/200 失败 %d  用时 %.2fs  吞吐 %.1f 帧/秒"
              % (len(rtts), fails, dur, len(rtts) / dur))
        if s:
            print("  RTT min %.2f p50 %.2f p95 %.2f max %.2f ms"
                  % (s[0], s[len(s) // 2], s[int(len(s) * .95)], s[-1]))

        print("\n=== I. 半帧 / 尾部垃圾 / 双帧 ===")
        sp.reset_input_buffer()
        sp.write(b"\x01")            # 半帧
        sp.flush()
        time.sleep(0.12)
        regs, rtt, err = read_regs(sp, UNIT, 0x04, 0x0000, 2, TMO)
        print("  半帧残渣后正常帧 -> %s" % (err or "OK %s" % regs))

        sp.reset_input_buffer()
        f = build(UNIT, 0x04, 0x0000, 2)
        sp.write(f + b"\x00\x00")
        sp.flush()
        raw = recv_frame(sp, 9, TMO)
        print("  帧尾加 2 字节垃圾 -> %s" % ("有应答 %s" % raw.hex() if raw else "无应答"))

        sp.reset_input_buffer()
        sp.write(f + f)
        sp.flush()
        time.sleep(0.15)
        got = sp.read(256)
        print("  同包双帧 -> %d 字节 %s" % (len(got), got.hex()[:80]))

        print("\n=== J. 响应帧间隔稳定性（1000 次 FC04 x2）===")
        rtts = []
        for _ in range(1000):
            regs, rtt, err = transact(sp, UNIT, 0x04, 0x0000, 2, TMO)
            if not err:
                rtts.append(rtt)
        # 重新读取一次走正常 read_regs 避免污染
        s = sorted(rtts)
        print("  n=%d  用时合计 %.2fs" % (len(s), sum(s) / 1000))
        print("  min %.2f p50 %.2f p95 %.2f p99 %.2f max %.2f ms"
              % (s[0], s[len(s) // 2], s[int(len(s) * .95)],
                 s[int(len(s) * .99)], s[-1]))
    finally:
        sp.close()


if __name__ == "__main__":
    main()
