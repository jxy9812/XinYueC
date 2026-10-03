#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""xs7_probe.py —— DB2 长度边界探测 + CPU 状态/程序覆写探测（联调用，只读为主）。

背景：get_block_info('DB',1) 已被目标 CPU 拒绝（见 xs7_diffdiag.py blocks 实验），
改用逐偏移 read_area 探测边界定性 DB2 实际长度。

子命令：
  db1     逐偏移读 DB2 探测可读边界（指数上界 + 二分精确到字节），只读无副作用
  state   CPU 状态（get_cpu_state）+ 覆写探测：向 DB2.DBB2/DB2.DBW4/MW2 写新图案，
          间隔两次回读观察是否被程序改回（RUN 的判据），测完按原值还原

连接方式与 xs7_crosscheck.py 一致：local TSAP = 0x01<<8|rack，remote TSAP = 0x03<<8|slot。
"""

import argparse
import sys
import time

import snap7
from snap7.client import Client
from snap7.types import Areas

PLC_IP = "192.168.1.251"
RACK, SLOT = 0, 1

A_DB, A_MK = Areas.DB, Areas.MK

# 覆写探测目标：(显示名, area, db, offset, size, 写入图案)
# DB2.DBB2 / DB2.DBW4 是 xs7_crosscheck.KNOWN_PLC_OWNED 中的程序覆写区，为可靠探针
OVERWRITE_PROBES = [
    ("DB2.DBB2", A_DB, 2, 2, 1, b"\xC7"),
    ("DB2.DBW4", A_DB, 2, 4, 2, b"\x7D\x5A"),
    ("MW2",      A_MK, 0, 2, 2, b"\x63\xA9"),
]


def hx(data):
    return " ".join("%02X" % b for b in bytearray(data))


def connect():
    c = Client()
    c.set_connection_params(PLC_IP, (0x01 << 8) | RACK, (0x03 << 8) | SLOT)
    c.connect(PLC_IP, RACK, SLOT)
    if not c.get_connected():
        raise RuntimeError("snap7 connect 失败")
    print("[snap7] connected %s rack=%d slot=%d (python-snap7 %s)"
          % (PLC_IP, RACK, SLOT, snap7.__version__))
    return c


def try_read(c, off, size=1):
    """读 DB2[off:off+size]，成功返回 bytes，失败返回 None。"""
    try:
        return bytes(c.read_area(A_DB, 2, off, size))
    except Exception as e:
        print("    (DBB%d+%dB 读异常: %s)" % (off, size, e))
        return None


def cmd_db1(_args):
    c = connect()
    base = try_read(c, 0, 8)
    if base is None:
        print("[db1] 结论: DB2 低偏移不可读——连接或块异常，无法探测")
        c.disconnect(); c.destroy()
        return 2
    print("[db1] 基线 DB2.DBB0 8B: %s" % hx(base))

    # 指数上界探测
    bad = None
    good = 0
    for off in [96, 128, 256, 512, 768, 1024, 1200, 1600, 2048, 4096, 8192, 16384]:
        r = try_read(c, off, 1)
        print("[db1] 探测 DBB%5d 1B: %s" % (off, ("OK  " + hx(r)) if r is not None else "FAIL"))
        if r is None:
            bad = off
            break
        good = off

    if bad is None:
        length = 16385
        print("[db1] 到 DBB16384 仍可读，超出常规范围，按 >=16385 记录")
    else:
        lo, hi = good, bad          # lo: 最大已知可读偏移; hi: 最小已知不可读偏移
        while hi - lo > 1:
            mid = (lo + hi) // 2
            r = try_read(c, mid, 1)
            print("[db1] 二分 DBB%5d 1B: %s" % (mid, "OK" if r is not None else "FAIL"))
            if r is not None:
                lo = mid
            else:
                hi = mid
        length = lo + 1

    print("[db1] 探测结论: DB2 长度 = %d 字节（最大可读偏移 DBB%d，DBB%d 起不可读）"
          % (length, length - 1, length))
    if length >= 1200:
        r = try_read(c, 0, 1200)
        print("[db1] 整段读 DB2[0:1200] 1200B: %s —— 满足 large_fragmentation 的 1024B 分片需求"
              % ("OK %d B" % len(r) if r is not None else "FAIL"))
        verdict = 0
    else:
        print("[db1] DB2 不足 1200 字节 —— large_fragmentation（DB2.DBB100+1024）无片可分")
        verdict = 1
    c.disconnect(); c.destroy()
    return verdict


def cmd_state(_args):
    c = connect()
    try:
        st = c.get_cpu_state()
        print("[state] get_cpu_state(): %s" % st)
    except Exception as e:
        st = None
        print("[state] get_cpu_state() 异常: %r（改用覆写行为判据）" % e)

    saved, r0, r1, r2 = {}, {}, {}, {}
    for name, area, db, off, size, _pat in OVERWRITE_PROBES:
        saved[name] = bytes(c.read_area(area, db, off, size))
    print("[state] 探针原值: %s" % {k: hx(v) for k, v in saved.items()})

    for name, area, db, off, size, pat in OVERWRITE_PROBES:
        c.write_area(area, db, off, bytearray(pat))
    for name, area, db, off, size, _pat in OVERWRITE_PROBES:
        r0[name] = bytes(c.read_area(area, db, off, size))
    print("[state] 写后即时回读: %s" % {k: hx(v) for k, v in r0.items()})

    time.sleep(4.0)
    for name, area, db, off, size, _pat in OVERWRITE_PROBES:
        r1[name] = bytes(c.read_area(area, db, off, size))
    time.sleep(4.0)
    for name, area, db, off, size, _pat in OVERWRITE_PROBES:
        r2[name] = bytes(c.read_area(area, db, off, size))
    print("[state] 4s 后回读:   %s" % {k: hx(v) for k, v in r1.items()})
    print("[state] 8s 后回读:   %s" % {k: hx(v) for k, v in r2.items()})

    # 还原探针原值（不动 PLC 里不属于测试的东西）
    for name, area, db, off, size, _pat in OVERWRITE_PROBES:
        c.write_area(area, db, off, bytearray(saved[name]))
    back = {name: bytes(c.read_area(area, db, off, size))
            for name, area, db, off, size, _pat in OVERWRITE_PROBES}
    print("[state] 已还原原值: %s" % {k: hx(v) for k, v in back.items()})
    c.disconnect(); c.destroy()

    overwritten = [n for n in r2 if r2[n] != OVERWRITE_PROBES[[p[0] for p in OVERWRITE_PROBES].index(n)][5]
                   or r1[n] != r2[n] or r0[n] != r1[n]]
    stable = [n for n in r2 if n not in overwritten]
    print("[state] 判定: 被程序改写 %s / 保持写入值 %s" % (overwritten, stable))
    if st == "S7CpuStatusRun":
        print("[state] 结论: CPU 在 RUN（get_cpu_state 权威 + 覆写证据 %s）" % overwritten)
        return 1
    if st == "S7CpuStatusStop":
        print("[state] 结论: CPU 在 STOP")
        return 0
    if overwritten:
        print("[state] 结论: 状态查询不可用，但覆写证据 %s 存在 —— 程序在跑（RUN）" % overwritten)
        return 1
    print("[state] 结论: 状态查询不可用，写读回一致且 8s 无覆写 —— 疑似 STOP（无程序覆写）")
    return 0


def main(argv):
    p = argparse.ArgumentParser(description="DB2 边界 + CPU 状态探测")
    sub = p.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("db1"); s.set_defaults(fn=cmd_db1)
    s = sub.add_parser("state"); s.set_defaults(fn=cmd_state)
    args = p.parse_args(argv)
    return args.fn(args)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
