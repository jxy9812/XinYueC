# -*- coding: utf-8 -*-
"""数字量（16DI/16DO）与模拟量/温湿度的端到端测试。

C 侧服务端没有数字量模块，本组验证 Python 版新增的协议面：
    快照 CMD_DIGITAL_SNAPSHOT  载荷 {"dev":int}
    控制 CMD_DIGITAL_CONTROL   载荷 {"dev","do","on"} 或 {"dev","mask"}
    推送 TOPIC_DIGITAL_STATUS

用法：
    python tests/test_io_modules.py [--port 6199]
需要先起 tests/harness.py。
"""

import argparse
import os
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from hsfsrv import bson, proto                     # noqa: E402
from test_vfd_ops import Client, login_as, check, section, FAILED  # noqa: E402
import test_vfd_ops                                 # noqa: E402


def case_digital_snapshot(host, port):
    section("I. 数字量快照")
    c, _ = login_as(host, port, "root", "root")

    for index in (0, 1):
        r = c.request(proto.CMD_DIGITAL_SNAPSHOT,
                      bson.encode({proto.KEY_DEV: index}))
        check("模块 %d 快照有应答" % index, r is not None)
        if not r:
            continue
        doc = r[1]
        check("模块 %d dev 字段正确" % index, doc.get("dev") == index,
              "dev=%s" % doc.get("dev"))
        check("模块 %d count=16" % index, doc.get("count") == 16,
              "count=%s" % doc.get("count"))
        check("模块 %d 含 di 位图" % index, isinstance(doc.get("di"), int),
              "di=0x%04X" % (doc.get("di") or 0))
        check("模块 %d 含 do 位图" % index, isinstance(doc.get("do"), int),
              "do=0x%04X" % (doc.get("do") or 0))
        check("模块 %d valid=true" % index, doc.get("valid") is True)

    # DI 内容：假设备前 4 路 = 1,0,1,0 → 位图 0b0101 = 0x0005
    r = c.request(proto.CMD_DIGITAL_SNAPSHOT, bson.encode({proto.KEY_DEV: 0}))
    if r:
        check("DI 位图内容正确（0b0101）", r[1].get("di") == 0b0101,
              "di=0x%04X" % (r[1].get("di") or 0))

    # 无 dev 字段 → 缺省模块 0
    r = c.request(proto.CMD_DIGITAL_SNAPSHOT, b"")
    check("缺省 dev=0 可取快照", r is not None and r[1].get("dev") == 0)

    # 越界模块号
    r = c.request(proto.CMD_DIGITAL_SNAPSHOT,
                  bson.encode({proto.KEY_DEV: 99}))
    check("越界模块号被拒", r is not None and r[1].get("ok") is False,
          "err=%r" % ((r[1].get("err") if r else "") or "")[:40])

    c.close()
    time.sleep(0.8)


def case_digital_control(host, port):
    section("J. 数字量 DO 控制")
    c, _ = login_as(host, port, "root", "root")

    def read(dev):
        r = c.request(proto.CMD_DIGITAL_SNAPSHOT, bson.encode({proto.KEY_DEV: dev}))
        return r[1].get("do", 0) if r else -1

    base = read(0)
    check("初始 DO 可读", base >= 0, "do=0x%04X" % (base or 0))

    # ---- 单点置位 ----
    r = c.request(proto.CMD_DIGITAL_CONTROL,
                  bson.encode({proto.KEY_DEV: 0, proto.KEY_DO: 3,
                               proto.KEY_ON: True}))
    check("单点 DO3 置位受理", bool(r) and r[1].get("ok") is True,
          "err=%r" % ((r[1].get("err") if r else "") or "")[:40])
    after = read(0)
    check("DO3 置位后读回一致", bool(after & (1 << 3)),
          "do=0x%04X" % after)

    # ---- 单点复位 ----
    r = c.request(proto.CMD_DIGITAL_CONTROL,
                  bson.encode({proto.KEY_DEV: 0, proto.KEY_DO: 3,
                               proto.KEY_ON: False}))
    check("单点 DO3 复位受理", bool(r) and r[1].get("ok") is True)
    after = read(0)
    check("DO3 复位后读回一致", not (after & (1 << 3)), "do=0x%04X" % after)

    # ---- 整组写（位图）----
    r = c.request(proto.CMD_DIGITAL_CONTROL,
                  bson.encode({proto.KEY_DEV: 0, proto.KEY_MASK: 0x00A5}))
    check("整组写位图受理", bool(r) and r[1].get("ok") is True,
          "err=%r" % ((r[1].get("err") if r else "") or "")[:40])
    after = read(0)
    check("整组写读回一致", after == 0x00A5, "do=0x%04X" % after)

    # ---- 整组清零 ----
    r = c.request(proto.CMD_DIGITAL_CONTROL,
                  bson.encode({proto.KEY_DEV: 0, proto.KEY_MASK: 0}))
    check("整组清零受理", bool(r) and r[1].get("ok") is True)
    check("整组清零读回 0", read(0) == 0, "do=0x%04X" % read(0))

    # ---- 两台模块互不影响 ----
    c.request(proto.CMD_DIGITAL_CONTROL,
              bson.encode({proto.KEY_DEV: 0, proto.KEY_MASK: 0x0F0F}))
    check("模块0 写入不影响模块1", read(1) == 0, "do1=0x%04X" % read(1))

    # ---- 边界 ----
    r = c.request(proto.CMD_DIGITAL_CONTROL,
                  bson.encode({proto.KEY_DEV: 0, proto.KEY_DO: 16,
                               proto.KEY_ON: True}))
    check("DO 通道越界被拒", bool(r) and r[1].get("ok") is False,
          "err=%r" % ((r[1].get("err") if r else "") or "")[:40])
    r = c.request(proto.CMD_DIGITAL_CONTROL,
                  bson.encode({proto.KEY_DEV: 0, proto.KEY_DO: -1,
                               proto.KEY_ON: True}))
    check("DO 负通道被拒", bool(r) and r[1].get("ok") is False)
    r = c.request(proto.CMD_DIGITAL_CONTROL, bson.encode({proto.KEY_DEV: 0}))
    check("缺 do/mask 被拒", bool(r) and r[1].get("ok") is False,
          "err=%r" % ((r[1].get("err") if r else "") or "")[:40])
    r = c.request(proto.CMD_DIGITAL_CONTROL,
                  bson.encode({proto.KEY_DEV: 99, proto.KEY_MASK: 1}))
    check("模块号越界被拒", bool(r) and r[1].get("ok") is False)

    c.request(proto.CMD_DIGITAL_CONTROL,
              bson.encode({proto.KEY_DEV: 0, proto.KEY_MASK: 0}))
    c.close()
    time.sleep(0.8)


def case_digital_gates(host, port):
    section("K. 数字量闸门（与变频器同口径）")
    # ---- admin 只读 ----
    c, role = login_as(host, port, "admin", "admin")
    r = c.request(proto.CMD_DIGITAL_CONTROL,
                  bson.encode({proto.KEY_DEV: 0, proto.KEY_DO: 0,
                               proto.KEY_ON: True}))
    check("admin 写 DO 被拒", bool(r) and r[1].get("ok") is False,
          "err=%r" % ((r[1].get("err") if r else "") or "")[:40])
    check("拒绝原因含'权限不足'",
          "权限不足" in ((r[1].get("err") if r else "") or ""))
    # 只读快照仍可
    r = c.request(proto.CMD_DIGITAL_SNAPSHOT, bson.encode({proto.KEY_DEV: 0}))
    check("admin 可读数字量快照", r is not None)
    c.close()
    time.sleep(1.0)

    # ---- 自动模式 ----
    c, _ = login_as(host, port, "root", "root")
    check("切自动模式", c.set_mode(1))
    r = c.request(proto.CMD_DIGITAL_CONTROL,
                  bson.encode({proto.KEY_DEV: 0, proto.KEY_DO: 0,
                               proto.KEY_ON: True}))
    check("自动模式拒绝写 DO", bool(r) and r[1].get("ok") is False,
          "err=%r" % ((r[1].get("err") if r else "") or "")[:40])
    check("拒绝原因含'自动运行中'",
          "自动运行中" in ((r[1].get("err") if r else "") or ""))
    r = c.request(proto.CMD_DIGITAL_SNAPSHOT, bson.encode({proto.KEY_DEV: 0}))
    check("自动模式仍可读快照", r is not None)
    check("恢复手动", c.set_mode(0))
    c.close()
    time.sleep(0.8)


def case_digital_publish(host, port):
    section("L. 数字量遥测推送")
    c, _ = login_as(host, port, "root", "root")
    c._send(proto.MSGCLASS_SUBSCRIBE, proto.TOPIC_DIGITAL_STATUS & 0xFFFF,
            proto.DATATYPE_RAW, proto.pack_topic(proto.TOPIC_DIGITAL_STATUS))
    c.pump(2.0)
    frames = [d for (t, d, _ts) in c.publishes
              if t == proto.TOPIC_DIGITAL_STATUS]
    check("收到数字量遥测", len(frames) > 0, "(共 %d 帧)" % len(frames))
    if frames:
        devs = set(d.get("dev") for d in frames)
        check("两个模块都有推送", devs == {0, 1}, "devs=%s" % sorted(devs))
        check("推送含 di 位图", all(isinstance(d.get("di"), int)
                                    for d in frames))
        check("推送含 do 位图", all(isinstance(d.get("do"), int)
                                    for d in frames))
    c.close()
    time.sleep(0.8)


def case_analog_and_temphum(host, port):
    section("M. 模拟量与温湿度")
    c, _ = login_as(host, port, "root", "root")

    r = c.request(proto.CMD_AI_SNAPSHOT, b"")
    check("模拟量快照有应答", r is not None)
    if r:
        doc = r[1]
        check("模拟量 n=8", doc.get("n") == 8, "n=%s" % doc.get("n"))
        check("模拟量 v 位图全有效", doc.get("v") == 0xFF,
              "v=0x%02X" % (doc.get("v") or 0))
        # 假设备 CH1=995mV → u0 = 3996 µA
        check("CH1 换算为 3996µA", doc.get("u0") == 3996, "u0=%s" % doc.get("u0"))
        check("CH2 满量程 4975mV", doc.get("r1") == 4975, "r1=%s" % doc.get("r1"))

    r = c.request(proto.CMD_TEMPHUM_SNAPSHOT, b"")
    check("温湿度快照有应答", r is not None)
    if r:
        doc = r[1]
        check("温度 21.7（×0.1）", doc.get("temp") == 217, "temp=%s" % doc.get("temp"))
        check("湿度 64.8（×0.1）", doc.get("hum") == 648, "hum=%s" % doc.get("hum"))
        check("tempValid=true", doc.get("tempValid") is True)
        check("含 staleMs", isinstance(doc.get("staleMs"), int))

    c.close()
    time.sleep(0.8)


def case_health(host, port):
    section("N. 设备健康快照")
    c, _ = login_as(host, port, "root", "root")
    r = c.request(proto.CMD_DEVICE_HEALTH, b"")
    check("健康快照有应答", r is not None)
    if r:
        doc = r[1]
        n = doc.get("devCount", 0)
        check("健康条目数 > 0", n > 0, "devCount=%d" % n)
        kinds = set()
        for i in range(n):
            kinds.add(doc.get("d%d_kind" % i))
            check("条目%d 名字非空" % i, bool(doc.get("d%d_name" % i)))
            check("条目%d 有 health" % i, "d%d_health" % i in doc)
        # 假设备含：变频器(0) 温湿度(1) 模拟量(3) 数字量×2(5)
        check("含变频器条目", proto.KIND_VFD in kinds, "kinds=%s" % sorted(kinds))
        check("含温湿度条目", proto.KIND_TEMPHUM in kinds)
        check("含模拟量条目", proto.KIND_ANALOG_IO in kinds)
        check("含数字量条目", proto.KIND_DIGITAL_IO in kinds)
        check("含 seq 字段", isinstance(doc.get("seq"), int))
    c.close()
    time.sleep(0.8)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=6199)
    args = ap.parse_args()
    print("IO 模块测试  %s:%d" % (args.host, args.port))
    t0 = time.time()
    try:
        case_digital_snapshot(args.host, args.port)
        case_digital_control(args.host, args.port)
        case_digital_gates(args.host, args.port)
        case_digital_publish(args.host, args.port)
        case_analog_and_temphum(args.host, args.port)
        case_health(args.host, args.port)
    except Exception as exc:                        # noqa: BLE001
        print("\n[ERROR] %s: %s" % (type(exc).__name__, exc))
        FAILED.append("exception")

    dt = time.time() - t0
    print("\n===== %d passed, %d failed  (%.1fs) ====="
          % (test_vfd_ops.PASS, test_vfd_ops.FAIL, dt))
    if FAILED:
        print("失败项: " + ", ".join(FAILED))
    return 0 if test_vfd_ops.FAIL == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
