#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""xs7_crosscheck.py —— XinYueC XS7（C 库）↔ python-snap7 双向交叉验证。

子命令：
  writecheck  --c-log LOG             写路径验证：snap7 读 C 程序写过的地址，与 C 日志 RAW 行逐地址比对
  readprepare --out EXPECT            读路径准备：snap7 向 DB2/M 写一组全新可区分值并落盘期望 JSON
  readverify  --c-log LOG --expect E  读路径验证：以 XS7_PLC_READBACK=1 的 C 日志 RAW 行比对期望 JSON
  peek                                直接读全部映射地址（调试用）

说明：
  - 连接方式与 C 库集成握手一致：rack=0 slot=1（local TSAP 0x0100 / remote TSAP 0x0301），
    set_connection_params + connect（snap7 connect() 内部即 set_connection_params+Cli_Connect）。
  - DB2.DBB20 在 C 库打印 18 字节（STRING16 = 2B 头 + 16 字符），本脚本同跨度读 18B。
  - T/C 区 snap7 语义与 C 库不同且目标 CPU 返回 0x05，仅尽力而为标注，不计入结论。
  - I 区只读，仅做参考比对，不计入读写结论。
"""

import argparse
import json
import re
import struct
import sys
import time

import snap7
from snap7.client import Client
from snap7.types import Areas

PLC_IP = "192.168.1.251"
RACK, SLOT = 0, 1

# snap7 区线码（snap7.types.Areas 枚举成员；1.3 的 read/write_area 要求枚举而非裸 int）
A_PE, A_PA, A_MK, A_DB = Areas.PE, Areas.PA, Areas.MK, Areas.DB
A_TM, A_CT = Areas.TM, Areas.CT

# 地址映射表：addr -> (area, db, byte_offset, size, bit)
# 与 C 库 [XS7][PLC] MAP 一致；DB2.DBB20 按 C 库 RAW 跨度取 20 字节。
ADDR_MAP = {
    "DB2.DBX0.0": (A_DB, 2, 0, 1, 0),
    "DB2.DBB2":   (A_DB, 2, 2, 1, None),
    "DB2.DBW4":   (A_DB, 2, 4, 2, None),
    "DB2.DBD6":   (A_DB, 2, 6, 4, None),
    "DB2.DBD10":  (A_DB, 2, 10, 4, None),
    "DB2.DBB20":  (A_DB, 2, 20, 18, None),   # STRING16：2B 头 + 16 字符（C 读回 RAW 跨度 18B）
    "M0.0":       (A_MK, 0, 0, 1, 0),
    "MW2":        (A_MK, 0, 2, 2, None),
    "Q0.0":       (A_PA, 0, 0, 1, 0),
    "IW0":        (A_PE, 0, 0, 2, None),
    "T1":         (A_TM, 0, 1, 2, None),
    "C1":         (A_CT, 0, 1, 2, None),
    "DB2.DBW0":   (A_DB, 2, 0, 2, None),     # bad_address 用例后的存活读
}
# 参与结论的地址（DB2/M/Q 读写区）；I/T/C 仅参考
CONCLUSIVE = ["DB2.DBX0.0", "DB2.DBB2", "DB2.DBW4", "DB2.DBD6",
              "DB2.DBD10", "DB2.DBB20", "M0.0", "MW2", "Q0.0"]
# C 库已判 SKIP-UNSUPPORTED（写被 ACK 但读回始终为 PLC 程序值）的地址
KNOWN_PLC_OWNED = {"Q0.0"}   # DB2 为测试专用干净块，仅 Q 区可能仍被用户程序驱动

RAW_RE = re.compile(r"^\[XS7\]\[PLC\] RAW (\S+) ((?:[0-9A-Fa-f]{2}[ \t]*)+)$", re.M)
# C 日志函数级 SKIP 判定行（含 SKIP-UNSUPPORTED 标记）→ 测试函数名；[^\n]*$ 吞到行尾，
# 使 m.group(0) 包含 SKIP-UNSUPPORTED 之后的完整原因（「占用/覆写」判定在其后）
SKIP_VERDICT_RE = re.compile(r"^\[XS7\]\[PLC\] (\S+) SKIP （[^\n]*SKIP-UNSUPPORTED[^\n]*$", re.M)
# 测试函数 → 覆盖的结论地址；m_q_area 的 SKIP 仅因 Q0.0
# （同一日志中 M0.0/MW2 各自「读回比对一致」，SKIP 原因只在 Q 区）
SKIP_FUNC_ADDR = {
    "db_readwrite_bit":    ["DB2.DBX0.0"],
    "db_readwrite_byte":   ["DB2.DBB2"],
    "db_readwrite_word":   ["DB2.DBW4"],
    "db_readwrite_dword":  ["DB2.DBD6"],
    "db_readwrite_real":   ["DB2.DBD10"],
    "db_readwrite_string": ["DB2.DBB20"],
    "m_q_area":            ["Q0.0"],
    "i_area_readonly":     ["IW0"],
    "t_c_access":          ["T1", "C1"],
}


def parse_skip_unsupported(text):
    """从 C 日志解析被标 SKIP-UNSUPPORTED 且原因为「占用/覆写」的地址集（不计入结论）。"""
    out = set()
    for m in SKIP_VERDICT_RE.finditer(text):
        if "占用/覆写" in m.group(0):
            out.update(SKIP_FUNC_ADDR.get(m.group(1), []))
    return out

# 读路径写入的全新可区分模式（与 C 库图案、PLC 现值均不同）
NEW_PATTERN = {
    "DB2.DBX0.0": ("bit", 1),
    "DB2.DBB2":   ("bytes", bytes([0xC3])),
    "DB2.DBW4":   ("bytes", bytes([0x7E, 0x5B])),
    "DB2.DBD6":   ("bytes", bytes([0xA5, 0x5A, 0x3C, 0xC3])),
    "DB2.DBD10":  ("bytes", struct.pack(">f", -98.6)),
    "DB2.DBB20":  ("bytes", bytes([16, 10]) + b"SNAP7_XCHK" + bytes(8)),  # STRING 头 10 0A + 10 字符
    "M0.0":       ("bit", 1),
    "MW2":        ("bytes", bytes([0x51, 0xCA])),
}


def hx(data):
    return " ".join("%02X" % b for b in bytearray(data))


def snap7_read(c, addr):
    """按映射读地址，返回与 C RAW 同跨度的字节串（位地址归一为 00/01 单字节）。"""
    area, db, off, size, bit = ADDR_MAP[addr]
    if bit is None:
        return bytes(c.read_area(area, db, off, size))
    raw = bytes(c.read_area(area, db, off, 1))
    return bytes([(raw[0] >> bit) & 0x01])


def snap7_write(c, addr, value_bytes):
    """按映射写地址；位地址用读-改-写整字节（python-snap7 1.3 未暴露位字长写）。"""
    area, db, off, size, bit = ADDR_MAP[addr]
    if bit is None:
        c.write_area(area, db, off, bytearray(value_bytes))
        return
    cur = bytearray(c.read_area(area, db, off, 1))
    if value_bytes[0] & 0x01:
        cur[0] |= (1 << bit)
    else:
        cur[0] &= ~(1 << bit) & 0xFF
    c.write_area(area, db, off, cur)


def connect():
    c = Client()
    # local TSAP = 0x01<<8|rack，remote TSAP = 0x03<<8|slot（与 C 库 rack=0 slot=1 同编码）
    c.set_connection_params(PLC_IP, (0x01 << 8) | RACK, (0x03 << 8) | SLOT)
    c.connect(PLC_IP, RACK, SLOT)
    if not c.get_connected():
        raise RuntimeError("snap7 connect 失败")
    print("[snap7] connected %s rack=%d slot=%d (python-snap7 %s)"
          % (PLC_IP, RACK, SLOT, snap7.__version__))
    return c


def parse_raw_lines(log_path):
    text = open(log_path, "r", encoding="utf-8", errors="replace").read()
    out = {}
    for m in RAW_RE.finditer(text):
        addr = m.group(1)
        data = bytes.fromhex(m.group(2).replace("\t", " ").strip())
        out[addr] = data          # 同地址多次出现取最后一次（取证读会重复打印）
    return out


def cmd_writecheck(args):
    c = connect()
    text = open(args.c_log, "r", encoding="utf-8", errors="replace").read()
    raw = parse_raw_lines(args.c_log)
    skip_excluded = parse_skip_unsupported(text)
    if skip_excluded != KNOWN_PLC_OWNED:
        print("[writecheck] 注意: 日志解析的 SKIP-UNSUPPORTED 覆写区 %s 与静态清单 %s 不一致"
              % (sorted(skip_excluded), sorted(KNOWN_PLC_OWNED)))
    print("[writecheck] C 日志 RAW 地址数=%d SKIP-UNSUPPORTED 覆写区(不计入结论)=%s"
          % (len(raw), sorted(skip_excluded)))
    ok = bad = skip = 0
    for addr in [a for a in ADDR_MAP if a in raw]:
        if addr not in CONCLUSIVE:
            kind = "INFO"
        elif addr in skip_excluded:
            kind = "SKIPX"
        else:
            kind = "CHECK"
        try:
            r1 = snap7_read(c, addr)
            time.sleep(0.3)
            r2 = snap7_read(c, addr)
        except Exception as e:
            print("  [%s] %-14s snap7 读失败: %s" % (kind, addr, e))
            if kind == "CHECK":
                bad += 1
            continue
        stable = "稳定" if r1 == r2 else "漂移(两次读不同!)"
        match = (r1 == raw[addr])
        verdict = "MATCH" if match else "MISMATCH"
        note = ""
        if kind == "SKIPX":
            note = "C 库已判 SKIP-UNSUPPORTED 覆写区（PLC 用户程序占用，不计入结论）"
        elif kind == "CHECK" and not match and addr in KNOWN_PLC_OWNED:
            note = "^ 不一致且未见于日志 SKIP 判定——按真差异计入结论"
        print("  [%s] %-14s RAW=%-62s snap7=%-62s %s %s %s"
              % (kind, addr, hx(raw[addr]), hx(r1), verdict, stable, note))
        if kind == "CHECK":
            if match:
                ok += 1
            else:
                bad += 1
        elif kind == "SKIPX":
            skip += 1
    c.disconnect()
    c.destroy()
    print("[writecheck] 结论: 一致=%d 不一致=%d SKIP-UNSUPPORTED 覆写区排除=%d（不计入）"
          % (ok, bad, skip))
    return 0 if bad == 0 else 2


def cmd_readprepare(args):
    c = connect()
    expect = {}
    print("[readprepare] 写入全新可区分模式：")
    for addr, (kind, val) in NEW_PATTERN.items():
        vb = bytes([val]) if kind == "bit" else val
        snap7_write(c, addr, vb)
        back = snap7_read(c, addr)
        expect[addr] = hx(back)
        mark = "" if back == vb else "  <-- 注意: snap7 立即回读 != 写入值 (%s)" % hx(vb)
        print("  写 %-14s = %-62s 回读=%s%s" % (addr, hx(vb), hx(back), mark))
    c.disconnect()
    c.destroy()
    with open(args.out, "w", encoding="utf-8") as f:
        json.dump(expect, f, indent=2, ensure_ascii=False)
    print("[readprepare] 期望值已存 %s（以 snap7 回读为期望基线）" % args.out)
    print("下一步: XS7_PLC_READBACK=1 %s --test xs7-plc > c_run2.log 后执行 "
          "readverify --c-log c_run2.log --expect %s" % (args.exe, args.out))
    return 0


def cmd_readverify(args):
    expect = json.load(open(args.expect, "r", encoding="utf-8"))
    raw = parse_raw_lines(args.c_log)
    print("[readverify] 期望地址数=%d C RAW 地址数=%d（双基线：written=本脚本写入值, reread=snap7 写后即时回读）"
          % (len(expect), len(raw)))
    ok = bad = 0
    for addr, reread in expect.items():
        got = raw.get(addr)
        if got is None:
            print("  [CHECK] %-14s C 日志无 RAW 行 — MISMATCH(缺失)" % addr)
            bad += 1
            continue
        kind, val = NEW_PATTERN.get(addr, ("bytes", None))
        written = bytes([val]) if kind == "bit" else (val if val is not None else bytes.fromhex(reread))
        m_written = (got.hex(" ").upper() == hx(written))
        m_reread = (got.hex(" ").upper() == reread.upper())
        tag = "MATCH" if (m_written or m_reread) else "MISMATCH"
        note = ""
        if m_written and m_reread:
            note = "与写入值一致"
        elif m_reread:
            note = "!= 写入值，但 == snap7 即时回读（CPU 程序即时覆写，两实现共同观测）"
        elif m_written:
            note = "写入值保持，但与 snap7 即时回读不同（时序漂移）"
        print("  [CHECK] %-14s 写入=%-59s snap7回读=%-59s C读=%-59s %s %s"
              % (addr, hx(written), reread, hx(got), tag, note))
        if m_written or m_reread:
            ok += 1
        else:
            bad += 1
    print("[readverify] 结论: 一致=%d 不一致=%d" % (ok, bad))
    return 0 if bad == 0 else 2


def cmd_peek(args):
    c = connect()
    for addr in ADDR_MAP:
        try:
            r1 = snap7_read(c, addr)
            time.sleep(0.2)
            r2 = snap7_read(c, addr)
            print("  %-14s = %-62s %s" % (addr, hx(r1), "稳定" if r1 == r2 else "漂移! " + hx(r2)))
        except Exception as e:
            print("  %-14s 读失败: %s" % (addr, e))
    c.disconnect()
    c.destroy()
    return 0


def main(argv):
    p = argparse.ArgumentParser(description="XS7 C 库 ↔ python-snap7 交叉验证")
    sub = p.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("writecheck"); s.add_argument("--c-log", required=True); s.set_defaults(fn=cmd_writecheck)
    s = sub.add_parser("readprepare"); s.add_argument("--out", required=True)
    s.add_argument("--exe", default="XinYueC_Static.exe"); s.set_defaults(fn=cmd_readprepare)
    s = sub.add_parser("readverify"); s.add_argument("--c-log", required=True)
    s.add_argument("--expect", required=True); s.set_defaults(fn=cmd_readverify)
    s = sub.add_parser("peek"); s.set_defaults(fn=cmd_peek)
    args = p.parse_args(argv)
    return args.fn(args)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
