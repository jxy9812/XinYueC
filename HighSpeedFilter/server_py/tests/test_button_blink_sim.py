# -*- coding: utf-8 -*-
"""按钮灯闪烁：**真实 sim 全栈闭环**（2026-11-24 用户口径）。

启动一份真实 sim 服务端（虚拟设备 + 真 XLink + 真客户端），配置
auto.lights 为短毫秒，然后从**数字量模块 DO** 直接观测三盏灯：

    A. 切自动 + 选好可跑的模式 ⇒ 启动灯 DO7 闪；
    B. 启动进入运行相 ⇒ 启动灯常亮；
    C. 停止 ⇒ VFD 减速期间停止灯 DO8 闪；停稳后常亮、启动灯不闪；
    D. 报警锁存 ⇒ 复位灯 DO9 闪；复位后熄灭。
"""
import asyncio
import os
import sys
import tempfile
import threading
import time

for _stream in (sys.stdout, sys.stderr):
    try:
        _stream.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError, OSError):
        pass

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.dirname(HERE))

from hsfsrv import bson, proto  # noqa: E402
from test_vfd_ops import Client  # noqa: E402
from test_sim_full_flow import _spin  # noqa: E402

PASS = 0
FAIL = 0


def check(name, ok, detail=""):
    global PASS, FAIL
    if ok:
        PASS += 1
        print("  [PASS] %s %s" % (name, detail))
    else:
        FAIL += 1
        print("  [FAIL] %s %s" % (name, detail))


def _do_bits(business):
    return list(business.digital[0].do)


def _sample(business, ch, seconds, interval=0.025):
    vals = []
    end = time.time() + seconds
    while time.time() < end:
        vals.append(1 if _do_bits(business)[ch] else 0)
        time.sleep(interval)
    return vals


def _wait(pred, seconds, interval=0.02):
    end = time.time() + seconds
    while time.time() < end:
        if pred():
            return True
        time.sleep(interval)
    return False


def _client_scenario(business, server, errors):
    try:
        c = Client("127.0.0.1", server.port)
        c.hello()
        ok, _role = c.login("root", "root")
        assert ok, "root 登录失败"

        def q():
            r = c.request(proto.CMD_RUN_MODE, b"")
            return r[1] if r else {}

        # 配置：产水模式可跑 + 灯闪烁 150/100ms。
        #
        # [B 段前提：调速泵必须真的转起来] 停止灯闪的判据是
        # `vfd.is_stopping()`（停止命令已下发、输出还在减速）。模式配置里的
        # "devices" 决定自动序列启动哪些设备——只选阀 1 时变频器**从未收到
        # 启动命令**（输出恒 0），停止后自然没有"减速期间"可看（is_stopping
        # 立刻为 False 是**正确**的）。B1 断言的是"减速期间停止灯闪"，所以
        # 这里必须把 stirPump 一并选进产水模式，让 VFD 真的转起来、停止后
        # 走一段减速斜坡。断言本身不变。
        doc = {"section": proto.CFG_SECTION_AUTO,
               "modes": {"produce": {"devices": ["valve1", "stirPump"],
                                     "stirRpm": 720,
                                     "stirReverse": False}},
               "lights": {"startOnMs": 150, "startOffMs": 100,
                          "stopOnMs": 150, "stopOffMs": 100,
                          "resetOnMs": 150, "resetOffMs": 100}}
        r = c.request(proto.CMD_CONFIG_SET, bson.encode(doc))
        assert r and r[1].get("ok") is True, "配置失败 %r" % (
            r[1] if r else None,)

        # ---- 配置面：GET 返回 / 非法值被拒且不生效 ----
        def _lights():
            r = c.request(proto.CMD_CONFIG_GET,
                          bson.encode({"section": proto.CFG_SECTION_AUTO}))
            return (((r[1] if r else {}).get("auto") or {})
                    .get(proto.CFG_AUTO_LIGHTS) or {})

        check("A0a GET 返回 auto.lights 六键，值与 SET 一致",
              _lights() == doc["lights"], str(_lights()))
        bad = {"section": proto.CFG_SECTION_AUTO,
               "lights": {proto.AUTO_LIGHT_START_ON: 10}}
        r = c.request(proto.CMD_CONFIG_SET, bson.encode(bad))
        check("A0b 越界毫秒 SET 被拒（fail-closed）",
              bool(r) and r[1].get("ok") is False
              and ("范围" in str(r[1].get("err")) or "范围" in str(r[1])),
              str(r[1] if r else None))
        check("A0c 被拒后配置未被改动",
              _lights().get(proto.AUTO_LIGHT_START_ON) == 150,
              str(_lights()))

        # 切自动 + 选产水（只选不启动）⇒ 启动灯应闪。
        c.request(proto.CMD_RUN_MODE, bson.encode({"mode": 1}))
        c.request(proto.CMD_AUTO_CONTROL,
                  bson.encode({"op": proto.AUTO_OP_SELECT,
                               proto.KEY_AUTO_MODE: "produce"}))
        _wait(lambda: bool(q().get(proto.KEY_AUTO_RUNNING) is False), 3.0)
        vals = _sample(business, 7, 1.2)
        check("A1 切自动+可启动 ⇒ 启动灯 DO7 闪（有亮有灭）",
              0 in vals and 1 in vals, "DO7=%s" % "".join(map(str, vals)))
        # [2026-11-24 用户追加口径] 启动提示灯闪的同时，停止灯保持常亮
        # （两盏灯不再严格互斥）；停止过程才由 stopping 抢成闪。
        check("A2 启动灯闪烁期间停止灯 DO8 常亮",
              all(v == 1 for v in _sample(business, 8, 0.4)), "")

        # 启动 ⇒ 运行相后启动灯常亮。
        c.request(proto.CMD_AUTO_CONTROL,
                  bson.encode({"on": True,
                               proto.KEY_AUTO_MODE: "produce"}))
        _wait(lambda: q().get(proto.KEY_AUTO_PHASE) == "running", 20.0)
        vals = _sample(business, 7, 0.7)
        check("A3 进入运行相 ⇒ 启动灯 DO7 常亮", all(v == 1 for v in vals),
              "DO7=%s" % "".join(map(str, vals)))

        # 停止 ⇒ 减速期间停止灯闪；停稳后常亮。
        c.request(proto.CMD_AUTO_CONTROL, bson.encode({"on": False}))
        _ok_stop = _wait(lambda: business.vfd is not None
                         and business.vfd.is_stopping(), 5.0)
        vals = _sample(business, 8, 0.9)
        check("B1 减速期间停止灯 DO8 闪（有亮有灭）",
              0 in vals and 1 in vals, "DO8=%s" % "".join(map(str, vals)))
        _wait(lambda: business.vfd is not None
              and not business.vfd.is_stopping(), 20.0)
        time.sleep(0.35)
        vals = _sample(business, 8, 0.6)
        check("B2 完全停稳 ⇒ 停止灯 DO8 常亮", all(v == 1 for v in vals),
              "DO8=%s" % "".join(map(str, vals)))
        # [2026-11-24 用户追加] 自动模式停稳后应重新闪启动灯，提示可再启动。
        vals = _sample(business, 7, 1.2)
        check("B3 完全停稳后启动灯 DO7 闪（提示可再次启动）",
              0 in vals and 1 in vals, "DO7=%s" % "".join(map(str, vals)))

        # 报警锁存 ⇒ 复位灯闪；复位后熄灭。
        business.auto_controller.raise_fault(
            proto.AUTO_ALARM_CUSTOM, "blink-sim-test", stop_devices=False)
        time.sleep(0.35)
        vals = _sample(business, 9, 0.9)
        check("C1 报警锁存 ⇒ 复位灯 DO9 闪（有亮有灭）",
              0 in vals and 1 in vals, "DO9=%s" % "".join(map(str, vals)))
        c.request(proto.CMD_AUTO_CONTROL,
                  bson.encode({"op": proto.AUTO_OP_RESET}))
        time.sleep(0.35)
        vals = _sample(business, 9, 0.6)
        check("C2 复位后复位灯 DO9 熄灭", all(v == 0 for v in vals),
              "DO9=%s" % "".join(map(str, vals)))
    except Exception as exc:                            # noqa: BLE001
        errors.append("%s: %s" % (type(exc).__name__, exc))


async def _run():
    basedir = tempfile.mkdtemp(prefix="hsf_blink_sim_")
    errors = []
    cfg, business, server = await _spin(basedir, [],
                                        move_ms=250, timeout_ms=3000)
    thread = threading.Thread(target=_client_scenario,
                              args=(business, server, errors),
                              name="blink-sim-client")
    thread.start()
    while thread.is_alive():
        await asyncio.sleep(0.05)
    thread.join()
    await server.stop()
    if errors:
        for e in errors:
            check("场景异常", False, e)


def main():
    asyncio.run(_run())
    print("\nBLINK-SIM RESULT PASS %d FAIL %d" % (PASS, FAIL))
    return 0 if FAIL == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
