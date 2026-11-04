#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""第五轮补充段：帧间静默 / FC10 / 写空寄存器是否真生效 / 短长稳 / float 与 u16 关系。"""
import struct
import sys
import time

sys.path.insert(0, __import__("os").path.dirname(__import__("os").path.dirname(__import__("os").path.abspath(__file__))))
from probe_temphum import (build, crc16, f32, read_regs, recv_frame)

PORT, UNIT = "COM17", 4


class Port:
    def __enter__(self):
        import serial
        last = None
        for i in range(24):
            try:
                self.sp = serial.Serial(PORT, 115200, bytesize=8, parity="N",
                                        stopbits=1, timeout=0.05,
                                        write_timeout=1.0)
                time.sleep(0.06)
                self.sp.reset_input_buffer()
                return self.sp
            except Exception as exc:                   # noqa: BLE001
                last = exc
                time.sleep(0.4 * (1 + i % 5))
        raise last

    def __exit__(self, *e):
        try:
            self.sp.close()
        except Exception:                              # noqa: BLE001
            pass
        return False


def fc06(sp, addr, value, tmo=0.35):
    body = struct.pack(">BBHH", UNIT, 0x06, addr, value & 0xFFFF)
    sp.reset_input_buffer()
    sp.write(body + struct.pack("<H", crc16(body)))
    sp.flush()
    raw = recv_frame(sp, 8, tmo)
    return raw, (None if raw and len(raw) >= 8 else "无应答/短帧")


def sect(t):
    print("\n=== %s %s" % (t, "=" * max(0, 58 - len(t))), flush=True)


def main():
    sect("3. 帧间静默要求（背靠背 0 / 0.3 / 1 / 2 / 4 ms）")
    with Port() as sp:
        for gap in (0, 0.3, 1.0, 2.0, 4.0):
            ok = fail = 0
            for _ in range(50):
                if gap:
                    time.sleep(gap / 1000.0)
                r, rtt, e = read_regs(sp, UNIT, 0x04, 0x0000, 2, 0.2)
                ok, fail = (ok + 1, fail) if not e else (ok, fail + 1)
            print("  帧间 %4.1f ms -> OK %2d/50 失败 %d" % (gap, ok, fail))
        print("  （Python sleep 粒度约 1~15ms，0/0.3ms 即真正背靠背）")

    sect("4. FC10 写多寄存器复验（写回原值 000AH）")
    with Port() as sp:
        regs, _, e = read_regs(sp, UNIT, 0x03, 0x000A, 1, 0.35)
        if e:
            print("  读站号失败 %s" % e)
        else:
            body = struct.pack(">BBHHB", UNIT, 0x10, 0x000A, 1, 2) + \
                struct.pack(">H", regs[0])
            sp.reset_input_buffer()
            sp.write(body + struct.pack("<H", crc16(body)))
            sp.flush()
            raw = recv_frame(sp, 8, 0.35)
            print("  FC10 qty=1 -> %s" % ("无应答" if not raw else raw.hex()))
        again, _, e = read_regs(sp, UNIT, 0x03, 0x000A, 2, 0.35)
        print("  复核 000AH~000BH = %s（应仍为 [4, 7]）" % again)

    sect("5. FC06 写「空寄存器」是否真生效（0006H，非配置区，可安全复位）")
    with Port() as sp:
        r0, _, e = read_regs(sp, UNIT, 0x03, 0x0006, 1, 0.35)
        raw, werr = fc06(sp, 0x0006, 1234)
        r1, _, e1 = read_regs(sp, UNIT, 0x03, 0x0006, 1, 0.35)
        raw2, _ = fc06(sp, 0x0006, 0)
        r2, _, e2 = read_regs(sp, UNIT, 0x03, 0x0006, 1, 0.35)
        print("  写前=%s  写 1234 %s  读回=%s  写回 0  读回=%s"
              % (r0, "ACK" if not werr else werr, r1, r2))
        print("  判定: %s" % ("写被静默忽略（只 ACK 不存）"
                              if r1 and r0 and r1[0] == r0[0] == 0
                              else "写入生效（值=%s）" % (r1[0] if r1 else "?")))

    sect("6. 长稳 30 s（1 Hz）")
    with Port() as sp:
        t0 = time.time(); n = fail = 0
        traw, hraw, rtts = [], [], []
        while time.time() - t0 < 30:
            r, rtt, e = read_regs(sp, UNIT, 0x04, 0x0000, 6, 0.4)
            n += 1
            if e:
                fail += 1
            else:
                traw.append(r[0]); hraw.append(r[1]); rtts.append(rtt)
            time.sleep(1.0)
        s = sorted(rtts)
        print("  %d 次，失败 %d" % (n, fail))
        print("  温度 %d~%d (%.1f~%.1f degC)   湿度 %d~%d (%.1f~%.1f %%RH)"
              % (min(traw), max(traw), min(traw) * .1, max(traw) * .1,
                 min(hraw), max(hraw),
                 min(hraw) * .1, max(hraw) * .1))
        print("  RTT p50 %.2f  p95 %.2f  max %.2f ms"
              % (s[len(s) // 2], s[int(len(s) * .95)], s[-1]))

    sect("7. float32 与 u16 口径关系（30 帧）")
    with Port() as sp:
        rows = []
        for _ in range(30):
            r, _, e = read_regs(sp, UNIT, 0x04, 0x0000, 6, 0.35)
            if not e:
                rows.append((r[0], f32(r[2:4]), r[1], f32(r[4:6])))
            time.sleep(0.03)
        dt = max(abs(tf - tu * 0.1) for tu, tf, _, _ in rows)
        dh = max(abs(hf - hu * 0.1) for _, _, hu, hf in rows)
        print("  温度 |float - u16/10| 最大 %.4f degC" % dt)
        print("  湿度 |float - u16/10| 最大 %.4f %%RH" % dh)
        print("  样例: u16 温度 %d(%.1f) / float %.4f ; u16 湿度 %d(%.1f) / float %.4f"
              % (rows[0][0], rows[0][0] * .1, rows[0][1],
                 rows[0][2], rows[0][2] * .1, rows[0][3]))
        print("  判定: %s" % ("float 与 u16 同源，u16 为 float 截断到 0.1"
                              if dt < 0.1 and dh < 0.1 else "不一致"))


if __name__ == "__main__":
    main()
