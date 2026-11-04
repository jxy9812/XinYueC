#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""第五轮（收尾）：可写寄存器映射 / 帧内字节超时精测 / FC10 复验 / 长稳。

只做「写回原值」的 FC06，不改变设备任何配置。
"""
import collections
import struct
import sys
import time

sys.path.insert(0, __import__("os").path.dirname(__import__("os").path.dirname(__import__("os").path.abspath(__file__))))
from probe_temphum import (build, crc16, decode_u16_temp, decode_u16_hum,
                           f32, read_regs, recv_frame, transact)

PORT, UNIT = "COM17", 4


class Port:
    def __init__(self, baud=115200):
        self.baud, self.sp = baud, None

    def __enter__(self):
        import serial
        last = None
        for i in range(20):
            try:
                self.sp = serial.Serial(PORT, self.baud, bytesize=8,
                                        parity="N", stopbits=1,
                                        timeout=0.05, write_timeout=1.0)
                time.sleep(0.06)
                self.sp.reset_input_buffer()
                return self.sp
            except Exception as exc:                   # noqa: BLE001
                last = exc
                time.sleep(0.5 * (1 + i % 4))
        raise last

    def __exit__(self, *e):
        if self.sp:
            try:
                self.sp.close()
            except Exception:                          # noqa: BLE001
                pass
        return False


def sect(t):
    print("\n=== %s %s" % (t, "=" * max(0, 58 - len(t))), flush=True)


def fc06(sp, addr, value, tmo=0.35):
    body = struct.pack(">BBHH", UNIT, 0x06, addr, value & 0xFFFF)
    frame = body + struct.pack("<H", crc16(body))
    sp.reset_input_buffer()
    sp.write(frame)
    sp.flush()
    raw = recv_frame(sp, 8, tmo)
    if not raw:
        return None, "timeout"
    if len(raw) >= 8 and crc16(raw[:-2]) == struct.unpack("<H", raw[-2:])[0]:
        return raw, None
    return raw, "坏帧 %s" % raw.hex()


def main():
    sect("1. 可写寄存器映射（FC06 写回原值，不改变配置）")
    with Port() as sp:
        for addr in range(0x0000, 0x0014):
            regs, _, e = read_regs(sp, UNIT, 0x03, addr, 1, 0.35)
            if e:
                print("  %04XH 读失败 %s" % (addr, e))
                continue
            raw, werr = fc06(sp, addr, regs[0])
            print("  %04XH 值=%-6d FC06 -> %s"
                  % (addr, regs[0], "ACK" if not werr else werr))

    sect("2. 帧内字节间隔容限精测（无应答=帧被判超时丢弃）")
    frame = build(UNIT, 0x04, 0x0000, 2)
    with Port() as sp:
        for delay_ms in (0, 0.5, 1.0, 1.2, 1.5, 1.8, 2.0, 2.5, 3.0, 5.0):
            ok = 0
            for _ in range(15):
                sp.reset_input_buffer()
                for b in frame:
                    sp.write(bytes([b]))
                    sp.flush()
                    if delay_ms:
                        time.sleep(delay_ms / 1000.0)
                raw = recv_frame(sp, 9, 0.25)
                if raw and len(raw) >= 5 and \
                        crc16(raw[:-2]) == struct.unpack("<H", raw[-2:])[0]:
                    ok += 1
                time.sleep(0.02)
            print("  字节间 %4.1f ms -> 应答 %2d/15" % (delay_ms, ok))

    sect("3. 帧间静默要求（背靠背，0 / 0.3 / 1 / 2 / 4 ms）")
    with Port() as sp:
        for gap_ms in (0, 0.3, 1.0, 2.0, 4.0):
            ok = fail = 0
            for _ in range(50):
                if gap_ms:
                    time.sleep(gap_ms / 1000.0)
                r, rtt, e = read_regs(sp, UNIT, 0x04, 0x0000, 2, 0.2)
                ok, fail = (ok + 1, fail) if not e else (ok, fail + 1)
            print("  帧间 %4.1f ms -> OK %2d/50 失败 %d" % (gap_ms, ok, fail))

    sect("4. FC10 写多寄存器复验（写回原值 000AH）")
    with Port() as sp:
        regs, _, e = read_regs(sp, UNIT, 0x03, 0x000A, 1, 0.35)
        if e:
            print("  读站号失败 %s" % e)
        else:
            for qty in (1, 2):
                body = struct.pack(">BBHHB", UNIT, 0x10, 0x000A, qty,
                                   qty * 2) + \
                    struct.pack(">%dH" % qty, *([regs[0]] * qty))
                frame = body + struct.pack("<H", crc16(body))
                sp.reset_input_buffer()
                sp.write(frame)
                sp.flush()
                raw = recv_frame(sp, 8, 0.35)
                print("  FC10 qty=%d -> %s"
                      % (qty, ("ACK " + raw.hex()) if raw and len(raw) >= 8
                         else ("无应答" if not raw else "坏帧 " + raw.hex())))
                time.sleep(0.1)
        again, _, e = read_regs(sp, UNIT, 0x03, 0x000A, 2, 0.35)
        print("  复核 000AH~000BH = %s（应仍为 [4, 7]）" % again)

    sect("5. 长稳 60 s（1 Hz，看温湿度漂移与丢帧）")
    with Port() as sp:
        t0 = time.time()
        n = fail = 0
        traw, hraw, rtts = [], [], []
        while time.time() - t0 < 60:
            r, rtt, e = read_regs(sp, UNIT, 0x04, 0x0000, 6, 0.4)
            n += 1
            if e:
                fail += 1
            else:
                traw.append(r[0]); hraw.append(r[1]); rtts.append(rtt)
            time.sleep(1.0)
        if traw:
            s = sorted(rtts)
            print("  %d 次，失败 %d" % (n, fail))
            print("  温度 %d~%d (%.1f~%.1fdegC)  湿度 %d~%d (%.1f~%.1f%%RH)"
                  % (min(traw), max(traw), decode_u16_temp(min(traw)),
                     decode_u16_temp(max(traw)), min(hraw), max(hraw),
                     decode_u16_hum(min(hraw)), decode_u16_hum(max(hraw))))
            print("  RTT p50 %.2f p95 %.2f max %.2f ms"
                  % (s[len(s) // 2], s[int(len(s) * .95)], s[-1]))

    sect("6. float32 vs u16 关系（30 帧）")
    with Port() as sp:
        rows = []
        for _ in range(30):
            r, _, e = read_regs(sp, UNIT, 0x04, 0x0000, 6, 0.35)
            if e:
                continue
            rows.append((r[0], f32(r[2:4]), r[1], f32(r[4:6])))
            time.sleep(0.03)
        if rows:
            dt = [abs(tf - tu * 0.1) for tu, tf, _, _ in rows]
            dh = [abs(hf - hu * 0.1) for _, _, hu, hf in rows]
            print("  温度 |float - u16*0.1| 最大 %.4f  温度 float 精度示例 %.4f"
                  % (max(dt), rows[0][1]))
            print("  湿度 |float - u16*0.1| 最大 %.4f  湿度 float 精度示例 %.4f"
                  % (max(dh), rows[0][3]))
            print("  结论: %s" % ("u16 = trunc(float,1位) 一致"
                                  if max(dt) < 0.1 and max(dh) < 0.1
                                  else "两者不一致，需复核"))


if __name__ == "__main__":
    main()
