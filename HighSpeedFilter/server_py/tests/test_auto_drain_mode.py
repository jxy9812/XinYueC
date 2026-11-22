# -*- coding: utf-8 -*-
"""服务端「放水模式」配置面回归（2026-11-18 进正式门禁）。

[用户口径] 「自动模式中再增加一个放水模式，一样在远端设置页添加可自定义，
直接拷贝之前那三模式，改个名字即可；放水模式**不进自动模式**」
⇒ 「能配」与「能跑」必须分成两个集合：
  · `AUTO_MODES`（四模式，含 drain）= **能跑 + 能选 + 能配**（首页下拉 /
    op=5 选模式 / 远端设置的配置块）；
  · `AUTO_CYCLE_MODE_POOL`（四模式）= **可被配置进组合循环**的模式池。
[2026-11-18 再订正] 组合循环的**参与模式与顺序改为可配**（auto.cycle 的
cycleModes）⇒ 「放水不进循环」不再是硬编码禁令，只是**缺省列表**里没有
它（缺省＝产水/反洗/顺洗，与旧硬编码链一致）。
服务端必须**接受并落盘** drain（否则用户配了没保存）。

[测什么]
  1. auto 配置节能接受含 drain 的 modes（四模式同批下发）并**落盘**；
  2. GET 快照能读回四个模式（放水的值原样回显）；
  3. 重新从磁盘加载配置 ⇒ 放水仍在（配了不丢）；
  4. **fail-closed**：不认识/非法的模式键仍被拒（不被静默丢弃）；
  5. 放水**可选/可启动**（2026-11-18 订正：「放水模式不进自动模式」=
     **不进组合循环**，它本身是普通模式）：op=5 被受理、op=1 过模式闸；
     而**缺省参与循环列表仍是三个（不含放水）**，但**可以配成含放水**。

[怎么跑] cd HighSpeedFilter/server_py && python tests/test_auto_drain_mode.py
[为什么客户端跑在别的线程] 服务端是 asyncio（本线程的事件循环），而协议
客户端是阻塞 socket ⇒ 客户端必须跑在**另一个线程**，否则事件循环被阻塞、
服务端根本不回包（登录报 "no challenge"）。
"""
import asyncio
import json
import os
import shutil
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
SRV = os.path.dirname(HERE)            # server_py/
sys.path.insert(0, HERE)
sys.path.insert(0, SRV)

from hsfsrv import auth as auth_mod          # noqa: E402
from hsfsrv import bson, config as cfgmod    # noqa: E402
from hsfsrv import proto, xlink              # noqa: E402
from fake_devices import build_fake_app      # noqa: E402
from test_vfd_ops import Client              # noqa: E402

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


async def _spin_server(basedir, log):
    cfg = cfgmod.Config()
    cfg.resolve_paths(basedir)
    cfg.config_path = os.path.join(basedir, "cfg", "x.json")
    cfg.users_path = os.path.join(basedir, "cfg", "hsf_users.json")
    os.makedirs(os.path.join(basedir, "cfg"), exist_ok=True)
    cfg.port = 0
    cfg.enable_serial = False
    cfg.valve_move_timeout_ms = 600
    cfg.button_debounce_ms = 1
    users = auth_mod.UserStore(cfg.users_path)
    users.create_defaults()
    business = build_fake_app(cfg, log=log)

    from hsfsrv import buttons as buttons_mod
    from hsfsrv import pumps as pumps_mod
    from hsfsrv import valves as valves_mod
    business.valve_bank = valves_mod.ValveBank(
        business.digital, cfg.valve_items, lambda _m: None,
        move_timeout_ms=cfg.valve_move_timeout_ms)
    business.pump_bank = pumps_mod.PumpBank(
        business.digital, cfg.pump_items, lambda _m: None)
    business.button_station = buttons_mod.ButtonStation(
        business.auto_ctx, cfg.button_items,
        debounce_ms=cfg.button_debounce_ms, log=lambda _m: None)
    business.button_station.on_state_change = business._on_button_state
    business.button_station.set_running(False)

    d0 = business.digital[0]
    for n in range(6):
        d0.di[2 * n] = 0
        d0.di[2 * n + 1] = 1
    d0.di[cfg.button_items["estop_di"]] = 1
    d0.di[cfg.button_items.get("stop_di", 14)] = 1

    server = xlink.Server(cfg.port, users, log=log, require_auth=True)
    business.attach(server)
    await server.start()
    return business, server


def _client_scenario(business, server, errors):
    """客户端线程：真 XLink 协议走 auto 配置节。"""
    try:
        c = Client("127.0.0.1", server.port)
        c.hello()
        ok, info = c.login("root", "root")
        if not ok:
            errors.append("root 登录失败：%r" % (info,))
            return
        _run_checks(business, c)
    except Exception as exc:                        # noqa: BLE001
        import traceback
        errors.append("%s\n%s" % (exc, traceback.format_exc()))


def _run_checks(business, c):
    # ---- ① 四模式同批下发（含放水）----
    modes = {
        "produce": {"devices": ["valve1", "stirPump"], "stirRpm": 720,
                    "stirReverse": True},
        "backwash": {"devices": ["valve2"], "stirRpm": 900,
                     "stirReverse": False},
        "rinse": {"devices": ["valve3"], "stirRpm": 600},
        "drain": {"devices": ["valve4", "inletPump"], "stirRpm": 500,
                  "stirReverse": False,
                  proto.AUTO_MODE_RESET_VALVES_ON_STOP: False,
                  proto.AUTO_MODE_RESET_VALVES_ON_ESTOP: True,
                  proto.AUTO_MODE_STIR_SWITCH: "carry",
                  proto.AUTO_MODE_INLET_SWITCH: "direct"},
    }
    r = c.request(proto.CMD_CONFIG_SET, bson.encode({
        "section": proto.CFG_SECTION_AUTO, "modes": modes}))
    assert r and r[1].get("ok") is True, "auto SET 应受理：%r" % (r[1],)
    got_modes = (r[1].get(proto.CFG_SECTION_AUTO) or {}).get("modes", {})
    check("① SET 受理四模式（含放水）", True)
    check("①b 应答回显四个模式",
          sorted(got_modes) == ["backwash", "drain", "produce", "rinse"],
          "%s" % sorted(got_modes))
    d = got_modes.get("drain", {})
    check("①c 放水的设备/转速原样回显",
          d.get("devices") == ["valve4", "inletPump"] and d.get("stirRpm") == 500,
          "%r" % d)
    check("①d 放水的阀复位/换相策略原样回显",
          d.get(proto.AUTO_MODE_RESET_VALVES_ON_STOP) is False
          and d.get(proto.AUTO_MODE_RESET_VALVES_ON_ESTOP) is True
          and d.get(proto.AUTO_MODE_STIR_SWITCH) == "carry"
          and d.get(proto.AUTO_MODE_INLET_SWITCH) == "direct", "%r" % d)

    # ---- ② GET 快照四个模式 ----
    r = c.request(proto.CMD_CONFIG_GET, b"")
    g = (r[1].get(proto.CFG_SECTION_AUTO) or {}).get("modes", {})
    check("② GET 快照含四个模式",
          sorted(g) == ["backwash", "drain", "produce", "rinse"],
          "%s" % sorted(g))

    # ---- ③ 落盘 + 重新加载 ----
    with open(business.cfg.config_path, encoding="utf-8") as fh:
        doc = json.load(fh)
    file_modes = (doc.get("auto") or {}).get("modes") or {}
    check("③ 落盘文件含 drain",
          "drain" in file_modes
          and file_modes["drain"].get("devices") == ["valve4", "inletPump"]
          and file_modes["drain"].get("stir_rpm") == 500,
          "%r" % file_modes.get("drain"))
    cfg2 = cfgmod.Config()
    cfg2.resolve_paths(business.cfg.base_dir)
    cfg2.config_path = business.cfg.config_path
    cfg2.load()
    check("③b 重新从磁盘加载：放水仍在且值一致",
          cfg2.auto.get("drain", {}).get("devices") == ["valve4", "inletPump"]
          and cfg2.auto.get("drain", {}).get("stir_rpm") == 500
          and cfg2.auto.get("drain", {}).get(
              proto.AUTO_MODE_STIR_SWITCH) == "carry",
          "%r" % cfg2.auto.get("drain"))
    check("③c 重新加载后仍是四个模式",
          sorted(cfg2.auto) == ["backwash", "drain", "produce", "rinse"],
          "%s" % sorted(cfg2.auto))

    # ---- ④ fail-closed：非法模式键 / 非法策略值仍被拒 ----
    r = c.request(proto.CMD_CONFIG_SET, bson.encode({
        "section": proto.CFG_SECTION_AUTO,
        "modes": {"nosuchmode": {"devices": ["valve1"]}}}))
    check("④ 未知模式键被拒（不被静默丢弃）",
          r and r[1].get("ok") is False
          and "未知自动模式" in str(r[1].get("err", "")), "%r" % (r[1],))
    r = c.request(proto.CMD_CONFIG_SET, bson.encode({
        "section": proto.CFG_SECTION_AUTO,
        "modes": {"drain": {"devices": ["valve1"],
                             proto.AUTO_MODE_STIR_SWITCH: "nonsense"}}}))
    check("④b 放水的非法换相策略被拒（fail-closed）",
          r and r[1].get("ok") is False, "%r" % (r[1],))
    r = c.request(proto.CMD_CONFIG_GET, b"")
    g = (r[1].get(proto.CFG_SECTION_AUTO) or {}).get("modes", {})
    check("④c 两次拒绝后放水配置未被改坏",
          g.get("drain", {}).get("devices") == ["valve4", "inletPump"]
          and g.get("drain", {}).get(proto.AUTO_MODE_STIR_SWITCH) == "carry",
          "%r" % g.get("drain"))

    # ---- ⑤ 放水：可选/可启动，但**不进组合循环**（2026-11-18 订正）----
    c.request(proto.CMD_RUN_MODE, bson.encode({"mode": business.MODE_AUTO}))
    r = c.request(proto.CMD_AUTO_CONTROL, bson.encode(
        {"op": proto.AUTO_OP_SELECT, proto.KEY_AUTO_MODE: "drain"}))
    check("⑤ op=5 选模式「放水」被受理（它是普通的可启动模式）",
          bool(r) and r[1].get("ok") is True, "%r" % (r[1],))
    r = c.request(proto.CMD_AUTO_CONTROL, bson.encode(
        {"op": proto.AUTO_OP_START, proto.KEY_AUTO_MODE: "drain"}))
    # 启动后按现场条件可能另有原因被拒（如急停）⇒ 这里只断言**模式闸过了**
    # （错误里不再出现「未知自动模式 id」）。
    _err = str((r[1] or {}).get("err", "")) if r else ""
    check("⑤b op=1 按「放水」启动：模式闸已过（不再报未知模式）",
          bool(r) and "未知自动模式" not in _err, "%r" % (r[1],))
    why = business.auto_controller.mode_config_error("drain")
    check("⑤c mode_config_error(放水) 为空（配了设备 ⇒ 可跑）",
          why == "", "%r" % why)
    why = business.auto_controller.mode_config_error("produce")
    check("⑤d mode_config_error(产水) 仍按同口径判（配齐 ⇒ 可跑）",
          why == "", "%r" % why)
    business.cfg.auto["rinse"]["devices"] = []
    why = business.auto_controller.mode_config_error("rinse")
    check("⑤e mode_config_error(顺洗) 缺设备仍报「未配置设备」",
          why.startswith("模式[rinse]未配置设备"), "%r" % why)
    # ⑤f **组合循环的参与模式可配**（2026-11-18 配置化）：缺省列表＝
    # 产水/反洗/顺洗（与旧硬编码链一致）；放水**可以**被配置进循环
    # （旧的「循环里没有放水」只是缺省，不再是禁令）。
    check("⑤f 缺省参与循环的模式列表 = 产水/反洗/顺洗（不含放水）",
          list(proto.AUTO_CYCLE_MODES_DEF) == ["produce", "backwash", "rinse"]
          and "drain" not in proto.AUTO_CYCLE_MODES_DEF,
          "%s" % (list(proto.AUTO_CYCLE_MODES_DEF),))
    check("⑤f-2 模式池含放水（可配置进循环）",
          "drain" in proto.AUTO_CYCLE_MODE_POOL,
          "%s" % (list(proto.AUTO_CYCLE_MODE_POOL),))
    # 实际下发一份「含放水」的参与列表 ⇒ 服务端接受并回显（保序）。
    r = c.request(proto.CMD_CONFIG_SET, bson.encode(
        {"section": proto.CFG_SECTION_AUTO,
         "cycle": {proto.CFG_AUTO_CYCLE_MODES:
                   ["produce", "drain", "backwash"]}}))
    check("⑤f-3 含放水的参与列表被接受",
          bool(r) and r[1].get("ok") is True, "%r" % (r[1],))
    _gc = c.request(proto.CMD_CONFIG_GET, b"")
    _cyc = ((_gc[1].get(proto.CFG_SECTION_AUTO) or {})
             .get(proto.CFG_AUTO_CYCLE) or {}) if _gc else {}
    check("⑤f-4 GET 回显参与列表（保序）",
          list(_cyc.get(proto.CFG_AUTO_CYCLE_MODES) or [])
          == ["produce", "drain", "backwash"],
          "%r" % (_cyc.get(proto.CFG_AUTO_CYCLE_MODES),))
    check("⑤g AUTO_MODES 含放水（它在下拉里、能选能跑）",
          "drain" in [m for m, _n in proto.AUTO_MODES],
          "%s" % ([m for m, _n in proto.AUTO_MODES],))
    try:
        business.auto_controller.stop()
    except Exception:                                # noqa: BLE001
        pass
    c.close()


async def _feed_while(business, thread):
    """客户端线程跑期间，替 start_polling 喂假设备（与 tests 同款）。"""
    while thread.is_alive():
        now = int(time.monotonic() * 1000)
        for dev in business.digital:
            try:
                await dev.poll(now)
            except Exception:                       # noqa: BLE001
                pass
        try:
            await business.vfd.poll(now)
        except Exception:                           # noqa: BLE001
            pass
        await asyncio.sleep(0.05)


async def _case(basedir, log):
    business, server = await _spin_server(basedir, log)
    errors = []
    thread = threading.Thread(target=_client_scenario,
                              args=(business, server, errors),
                              name="drain-client")
    thread.start()
    try:
        await _feed_while(business, thread)
    finally:
        thread.join()
        await server.stop()
    if errors:
        print("  [ERROR] %s" % errors[0])
        globals()["FAIL"] += 1


def main():
    base = tempfile.mkdtemp(prefix="hsf_drain_")
    log = []
    try:
        asyncio.run(_case(base, log))
    finally:
        shutil.rmtree(base, ignore_errors=True)
    print("\n===== 放水模式（服务端配置面）：%d failed"
          "（check PASS %d / FAIL %d）=====" % (FAIL, PASS, FAIL))
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
