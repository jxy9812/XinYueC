# -*- coding: utf-8 -*-
"""三模式自动序列回归（2026-11-11 用户口径）。

[测什么] 远端设置配置的三个自动模式（产水/反洗/顺洗）在服务端的完整
执行序列，逐条锁死用户要求：

    1. **启动 = 先动阀**：开设置中选中的阀、关没选中的阀 → 等全部到位；
    2. **到位后按配置启动泵**：搅拌泵（配置的转速/方向）+ 进水泵，
       统一由启动按钮一次触发；
    3. **阀超时 ⇒ 报警 + 停止自动模式**（AUTO_ALARM_VALVE_TIMEOUT 上
       TOPIC_AUTO_ALARM + 落库），且**报警未复位不能再启动**；
    4. **急停闸**：急停按下不能启动；运行中急停 ⇒ 停全部设备 + 锁存，
       必须按复位才能再启动；
    5. **手动模式不能启动**；「自动」组合模式启动被明确拒（暂未实现）；
    6. **auto 配置节**：CMD_CONFIG_SET/GET 往返一致并落盘；
    7. 现场启动按钮用**上次选中的模式**（粘性）；从未选过则拒绝。

[怎么跑]
    python -I tests/test_auto_modes.py          # 独立跑
    pytest tests/test_auto_modes.py -q          # pytest 收集

服务端在本进程内起（假设备 + 真 XLink 协议栈），客户端走真 socket。
"""

import asyncio
import os
import shutil
import sys

# [输出编码 2026-11-14] 中文 Windows 默认代码页 cp936 ⇒ stdout 被重定向
# 到文件时用 GBK ⇒ 打印「⇒」(U+21D2，**GBK 编不了**) 直接
# UnicodeEncodeError ⇒ **整套测试在打印断言时被打断**（实测退出码仍是 0，
# 看着像"跑完了"）。强制 UTF-8 + errors="replace"（写不出去也不崩）。
for _stream in (sys.stdout, sys.stderr):
    try:
        _stream.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError, OSError):
        pass
import tempfile
import threading

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.dirname(HERE))

from hsfsrv import auth as auth_mod          # noqa: E402
from hsfsrv import autocontrol               # noqa: E402
from hsfsrv import bson, config as cfgmod    # noqa: E402
from hsfsrv import proto, xlink              # noqa: E402
from fake_devices import build_fake_app      # noqa: E402
from test_vfd_ops import Client              # noqa: E402  复用协议客户端

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


# ============================================================================
#                       装配：假设备 + 真 阀/泵/按钮组
# ============================================================================

async def _spin_server(basedir, log, valve_move_timeout_ms=600):
    """起一份假设备服务端，并补装 阀组/泵组/按钮组（build_fake_app 不建）。

    [阀超时缺省 600ms] 真机口径 20s；测试要快，塞短超时（阀层判故障与
    自动序列的截止判定共用这一个配置值）。
    """
    cfg = cfgmod.Config()
    cfg.resolve_paths(basedir)
    cfg.config_path = os.path.join(basedir, "cfg", "x.json")
    cfg.users_path = os.path.join(basedir, "cfg", "hsf_users.json")
    os.makedirs(os.path.join(basedir, "cfg"), exist_ok=True)
    cfg.port = 0                       # 临时端口，避免撞上在跑的实例
    cfg.enable_serial = False
    cfg.valve_move_timeout_ms = valve_move_timeout_ms
    cfg.button_debounce_ms = 1
    users = auth_mod.UserStore(cfg.users_path)
    users.create_defaults()
    business = build_fake_app(cfg, log=log)

    # ---- 补装三件真 Bank（与 app.build_devices 同款，只是不建真设备）----
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

    # ---- 初始 DI：6 阀全关到位 + 急停回路通（常闭=1）+ 停止常闭=1 ----
    d0 = business.digital[0]
    for n in range(6):
        d0.di[2 * n] = 0
        d0.di[2 * n + 1] = 1
    d0.di[cfg.button_items["estop_di"]] = 1
    d0.di[cfg.button_items.get("stop_di", 14)] = 1

    server = xlink.Server(cfg.port, users, log=log, require_auth=True)
    business.attach(server)
    await server.start()
    return business, server, d0


def _set_valve_di(d0, index, open_fb):
    """把第 index 路（0 基）阀的到位 DI 拨到目标位（模拟阀体走到位）。"""
    d0.di[2 * index] = 1 if open_fb else 0
    d0.di[2 * index + 1] = 0 if open_fb else 1


def _subscribe(client, topic):
    client._send(proto.MSGCLASS_SUBSCRIBE, topic & 0xFFFF,
                 proto.DATATYPE_RAW, proto.pack_topic(topic))


def _alarm_frames(client):
    return [doc for (t, doc, _ts) in client.publishes
            if t == proto.TOPIC_AUTO_ALARM]


def _set_mode(client, mode):
    return client.request(proto.CMD_RUN_MODE, bson.encode({"mode": mode}))


def _auto_state(client):
    """查询运行模式（应答带 autoMode/autoPhase/autoFault 三键）。"""
    r = client.request(proto.CMD_RUN_MODE, b"")
    return r[1] if r else {}


# ============================================================================
#                      场景一：产水模式全程（配置往返/阀相/泵相/停止）
# ============================================================================

def test_produce_mode_full_cycle():
    base = tempfile.mkdtemp(prefix="hsf_automode_")
    log = []
    try:
        asyncio.run(_case_produce(base, log))
    finally:
        shutil.rmtree(base, ignore_errors=True)


async def _case_produce(basedir, log):
    business, server, d0 = await _spin_server(basedir, log)
    errors = []

    def scenario():
        try:
            c = Client("127.0.0.1", server.port)
            c.hello()
            ok, _role = c.login("root", "root")
            assert ok, "root 登录失败"

            # ---- ① auto 配置节 SET/GET 往返 + 落盘 ----
            r = c.request(proto.CMD_CONFIG_SET, bson.encode({
                "section": proto.CFG_SECTION_AUTO,
                "modes": {"produce": {"devices": ["valve1", "valve2",
                                                  "stirPump", "inletPump"],
                                      "stirRpm": 720, "stirReverse": True},
                          }}))
            assert r and r[1].get("ok") is True, "auto SET 应受理：%r" % (
                r[1] if r else None)
            got = (r[1].get(proto.CFG_SECTION_AUTO) or {}).get("modes",
                                                               {}).get(
                "produce", {})
            assert got.get("devices") == ["valve1", "valve2", "stirPump",
                                          "inletPump"], \
                "SET 应答回显 devices：%r" % got
            assert got.get("stirRpm") == 720 and got.get("stirReverse") is True
            r = c.request(proto.CMD_CONFIG_GET, b"")
            auto_doc = (r[1].get(proto.CFG_SECTION_AUTO) or {}).get(
                "modes", {})
            assert auto_doc.get("produce", {}).get("devices") == \
                ["valve1", "valve2", "stirPump", "inletPump"], \
                "GET 应与 SET 一致：%r" % (auto_doc,)
            assert auto_doc.get("backwash", {}).get("devices") == [], \
                "未配置模式 = 空清单（缺省）：%r" % (auto_doc,)
            assert business.cfg.auto["produce"]["stir_rpm"] == 720
            assert business.cfg.auto["produce"]["stir_reverse"] is True

            # ---- ② 手动模式启动被拒 ----
            _set_mode(c, business.MODE_MANUAL)
            r = c.request(proto.CMD_AUTO_CONTROL, bson.encode(
                {"on": True, proto.KEY_AUTO_MODE: "produce"}))
            assert r and r[1].get("ok") is False, "手动模式启动应被拒"
            assert "手动" in str(r[1].get("err", "")), "拒绝原因应说手动：%r" % (
                r[1],)

            # ---- ③ 切自动 → 启动产水模式 ----
            assert _set_mode(c, business.MODE_AUTO)[1].get("ok") is True
            r = c.request(proto.CMD_AUTO_CONTROL, bson.encode(
                {"on": True, proto.KEY_AUTO_MODE: "produce"}))
            assert r and r[1].get("ok") is True, "启动应受理：%r" % (r[1],)
            assert r[1].get(proto.KEY_AUTO_MODE) == "produce"
            assert r[1].get(proto.KEY_AUTO_PHASE) == \
                autocontrol.AutoController.PHASE_VALVES, "启动即进阀相：%r" % (
                    r[1],)

            # ---- ④ 阀相：选中阀开（DO0/DO1=1）、未选阀关（DO2..5=0）----
            import time as _time
            deadline = _time.time() + 6.0
            while _time.time() < deadline:
                if d0.do[0] == 1 and d0.do[1] == 1:
                    break
                c.pump(0.05)
            assert d0.do[0] == 1 and d0.do[1] == 1, \
                "选中阀应下发开命令：do=%s" % (d0.do[:6],)
            assert all(d0.do[i] == 0 for i in (2, 3, 4, 5)), \
                "未选阀应下发关命令：do=%s" % (d0.do[:6],)

            # ---- ⑤ 模拟到位 → 泵相 → 运行相 ----
            for idx in (0, 1):
                _set_valve_di(d0, idx, True)      # 选中阀开到位
            for idx in (2, 3, 4, 5):
                _set_valve_di(d0, idx, False)     # 未选阀关到位（原已关）
            deadline = _time.time() + 8.0
            state = {}
            while _time.time() < deadline:
                state = _auto_state(c)
                if state.get(proto.KEY_AUTO_PHASE) == \
                        autocontrol.AutoController.PHASE_RUNNING:
                    break
                _time.sleep(0.1)
            assert state.get(proto.KEY_AUTO_PHASE) == \
                autocontrol.AutoController.PHASE_RUNNING, \
                "应推进到运行相：%r" % state
            assert state.get(proto.KEY_AUTO_MODE) == "produce"

            # ---- ⑥ 泵按配置启动：搅拌泵 720rpm 反转 + 进水泵 DO6=1 ----
            assert business.vfd._running is True, "搅拌泵应已启动"
            assert business.vfd._reverse is True, "方向应为配置的反转"
            want_pct = int(round(720.0 / 1440.0 * 50.0
                                 / business.vfd.max_freq_hz * 10000.0))
            assert business.vfd.freq_set_pct == max(-10000, min(
                10000, want_pct)), "给定应按 720rpm 折算（%d）：%d" % (
                want_pct, business.vfd.freq_set_pct)
            assert d0.do[6] == 1, "进水泵（DO6）应已启动：%s" % (d0.do,)

            # ---- ⑦ 停止：全停（变频器停 + 泵关 + 阀关）----
            r = c.request(proto.CMD_AUTO_CONTROL,
                          bson.encode({"on": False}))
            assert r and r[1].get("ok") is True
            deadline = _time.time() + 5.0
            while _time.time() < deadline:
                if d0.do[6] == 0 and business.vfd._running is False:
                    break
                _time.sleep(0.05)
            assert business.vfd._running is False, "停止后搅拌泵应停"
            assert d0.do[6] == 0, "停止后进水泵应关"
            assert any(code == business.vfd.CMD_DECEL_STOP
                       for code in business.vfd.commands), \
                "停止应走减速停机：%s" % (business.vfd.commands,)
            st = _auto_state(c)
            assert st.get(proto.KEY_AUTO_RUNNING) is False
            # 模式粘性保留（现场启动按钮/再次启动仍指向上次的产水模式）
            assert st.get(proto.KEY_AUTO_MODE) == "produce"
            c.close()
        except Exception as exc:                    # noqa: BLE001
            import traceback
            errors.append("%s\n%s" % (exc, traceback.format_exc()))

    thread = threading.Thread(target=scenario, name="auto-client")
    thread.start()
    try:
        await _feed_while(business, thread)
    finally:
        thread.join()
        await server.stop()
    if errors:
        raise RuntimeError(errors[0])


async def _feed_while(business, thread):
    """客户端线程跑期间，**替 start_polling 喂假设备**。

    [为什么需要] build_fake_app 场景不起 start_polling（那要真端口/连接）；
    而阀反馈的新鲜度闸认 `module.last_ok_ms`（设备 poll 才刷新）。不喂的
    话 6 路阀全部「数据不新鲜」，反馈永远进不来——实锅复现：不加这个
    feeder，阀相直接超时报警（阀状态从未离开构造缺省）。假设备 poll 与
    真实采集循环同接口，这里就是把它按拍驱动起来。
    """
    import time as _time
    while thread.is_alive():
        now = int(_time.monotonic() * 1000)
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


# ============================================================================
#            场景二：阀超时 ⇒ 报警 + 锁存；复位后才能再启动
# ============================================================================

def test_valve_timeout_alarm_and_fault_latch():
    base = tempfile.mkdtemp(prefix="hsf_automode_to_")
    log = []
    try:
        asyncio.run(_case_timeout(base, log))
    finally:
        shutil.rmtree(base, ignore_errors=True)


async def _case_timeout(basedir, log):
    business, server, d0 = await _spin_server(
        basedir, log, valve_move_timeout_ms=400)
    errors = []

    def scenario():
        try:
            import time as _time
            c = Client("127.0.0.1", server.port)
            c.hello()
            c.login("root", "root")
            # 配置反洗模式：阀3 + 进水泵（阀卡死时泵不该被启动）
            r = c.request(proto.CMD_CONFIG_SET, bson.encode({
                "section": proto.CFG_SECTION_AUTO,
                "modes": {"backwash": {"devices": ["valve3", "inletPump"],
                                       "stirRpm": 1440}}}))
            assert r and r[1].get("ok") is True
            _subscribe(c, proto.TOPIC_AUTO_ALARM)

            assert _set_mode(c, business.MODE_AUTO)[1].get("ok") is True
            r = c.request(proto.CMD_AUTO_CONTROL, bson.encode(
                {"on": True, proto.KEY_AUTO_MODE: "backwash"}))
            assert r and r[1].get("ok") is True

            # 阀 3 收到开命令但**永远不到位**（不给反馈）⇒ 阀层超时判故障
            # ⇒ 序列截止判定报警并停自动模式。
            deadline = _time.time() + 6.0
            frames = []
            while _time.time() < deadline:
                frames = [f for f in _alarm_frames(c)
                          if f.get("kind") == proto.AUTO_ALARM_VALVE_TIMEOUT]
                if frames:
                    break
                c.pump(0.05)
            assert frames, "应收到阀超时告警帧：%r" % (_alarm_frames(c),)
            text = str(frames[0].get("text", ""))
            assert "阀3" in text and "超时" in text, "告警文字应点名阀3：%r" % text

            # 停止自动模式 + 报警锁存：泵绝不能被启动，相位回 idle
            st = _auto_state(c)
            assert st.get(proto.KEY_AUTO_RUNNING) is False
            assert st.get(proto.KEY_AUTO_FAULT) is True
            assert d0.do[6] == 0, "阀没到位就不许启动泵"

            # 报警未复位 ⇒ 再启动被拒（用户口径「有报警没复位不能启动」）
            r = c.request(proto.CMD_AUTO_CONTROL, bson.encode(
                {"on": True, proto.KEY_AUTO_MODE: "backwash"}))
            assert r and r[1].get("ok") is False, "报警未复位应拒绝启动"
            assert "复位" in str(r[1].get("err", "")), "拒绝原因应提示复位：%r" % (
                r[1],)

            # 复位 ⇒ 解锁，可再启动（阀仍卡着会再次超时，但这轮只验解锁）
            r = c.request(proto.CMD_AUTO_CONTROL,
                          bson.encode({"op": proto.AUTO_OP_RESET}))
            assert r and r[1].get("ok") is True
            assert _auto_state(c).get(proto.KEY_AUTO_FAULT) is False
            r = c.request(proto.CMD_AUTO_CONTROL, bson.encode(
                {"on": True, proto.KEY_AUTO_MODE: "backwash"}))
            assert r and r[1].get("ok") is True, "复位后应允许再启动"
            c.close()
        except Exception as exc:                    # noqa: BLE001
            import traceback
            errors.append("%s\n%s" % (exc, traceback.format_exc()))

    thread = threading.Thread(target=scenario, name="auto-client")
    thread.start()
    try:
        await _feed_while(business, thread)
    finally:
        thread.join()
        await server.stop()
    if errors:
        raise RuntimeError(errors[0])


async def _feed_while(business, thread):
    """客户端线程跑期间，**替 start_polling 喂假设备**。

    [为什么需要] build_fake_app 场景不起 start_polling（那要真端口/连接）；
    而阀反馈的新鲜度闸认 `module.last_ok_ms`（设备 poll 才刷新）。不喂的
    话 6 路阀全部「数据不新鲜」，反馈永远进不来——实锅复现：不加这个
    feeder，阀相直接超时报警（阀状态从未离开构造缺省）。假设备 poll 与
    真实采集循环同接口，这里就是把它按拍驱动起来。
    """
    import time as _time
    while thread.is_alive():
        now = int(_time.monotonic() * 1000)
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


# ============================================================================
#     场景三：闸门面（组合模式未实现 / 空配置 / 急停闸与锁存 / 现场按钮粘性）
# ============================================================================

def test_gates_and_sticky_mode():
    base = tempfile.mkdtemp(prefix="hsf_automode_gate_")
    log = []
    try:
        asyncio.run(_case_gates(base, log))
    finally:
        shutil.rmtree(base, ignore_errors=True)


async def _case_gates(basedir, log):
    business, server, d0 = await _spin_server(basedir, log)
    errors = []

    def scenario():
        try:
            import time as _time
            c = Client("127.0.0.1", server.port)
            c.hello()
            c.login("root", "root")
            # 只配产水（阀1 + 搅拌泵）
            r = c.request(proto.CMD_CONFIG_SET, bson.encode({
                "section": proto.CFG_SECTION_AUTO,
                "modes": {"produce": {"devices": ["valve1", "stirPump"],
                                      "stirRpm": 1440}}}))
            assert r and r[1].get("ok") is True
            assert _set_mode(c, business.MODE_AUTO)[1].get("ok") is True

            # ---- 「自动」组合模式（循环）：启动被明确拒（子模式缺设备）----
            # [2026-11-12 组合模式已实现] 拒绝原因不再是"暂未实现"，而是
            # **具体的循环配置原因**（这里：反洗/顺洗两个子模式没配设备）。
            r = c.request(proto.CMD_AUTO_CONTROL, bson.encode(
                {"on": True, proto.KEY_AUTO_MODE: proto.AUTO_MODE_AUTO}))
            assert r and r[1].get("ok") is False, "组合模式应被拒"
            _err = str(r[1].get("err", ""))
            assert ("未实现" not in _err and "循环" in _err), \
                "应说明循环配置原因：%r" % (r[1],)

            # ---- 未配置模式：拒绝且说明去哪配 ----
            r = c.request(proto.CMD_AUTO_CONTROL, bson.encode(
                {"on": True, proto.KEY_AUTO_MODE: "rinse"}))
            assert r and r[1].get("ok") is False
            assert "未配置" in str(r[1].get("err", "")), "%r" % (r[1],)

            # ---- 启动产水（无泵配置中 stirPump 有 ⇒ 等泵相）→ 运行 ----
            r = c.request(proto.CMD_AUTO_CONTROL, bson.encode(
                {"on": True, proto.KEY_AUTO_MODE: "produce"}))
            assert r and r[1].get("ok") is True
            _set_valve_di(d0, 0, True)
            for idx in (1, 2, 3, 4, 5):
                _set_valve_di(d0, idx, False)
            deadline = _time.time() + 8.0
            while _time.time() < deadline:
                if _auto_state(c).get(proto.KEY_AUTO_PHASE) == \
                        autocontrol.AutoController.PHASE_RUNNING:
                    break
                _time.sleep(0.1)
            assert business.vfd._running is True, "搅拌泵应已启动"

            # ---- 运行中急停：全停 + 锁存；复位前不能启动 ----
            r = c.request(proto.CMD_AUTO_CONTROL,
                          bson.encode({"op": proto.AUTO_OP_ESTOP}))
            assert r and r[1].get("ok") is True
            deadline = _time.time() + 5.0
            while _time.time() < deadline:
                if business.vfd._running is False:
                    break
                _time.sleep(0.05)
            assert business.vfd._running is False, "急停应停搅拌泵"
            st = _auto_state(c)
            assert st.get(proto.KEY_AUTO_RUNNING) is False
            assert st.get(proto.KEY_AUTO_PHASE) == \
                autocontrol.AutoController.PHASE_ESTOP
            r = c.request(proto.CMD_AUTO_CONTROL, bson.encode(
                {"on": True, proto.KEY_AUTO_MODE: "produce"}))
            assert r and r[1].get("ok") is False, "急停锁存应拒绝启动"
            # 复位 → 解锁（急停锁存与报警锁存一把钥匙）
            c.request(proto.CMD_AUTO_CONTROL,
                      bson.encode({"op": proto.AUTO_OP_RESET}))
            r = c.request(proto.CMD_AUTO_CONTROL, bson.encode(
                {"on": True, proto.KEY_AUTO_MODE: "produce"}))
            assert r and r[1].get("ok") is True, "复位后应可再启动：%r" % (
                r[1],)
            c.request(proto.CMD_AUTO_CONTROL, bson.encode({"on": False}))
            c.close()

            # ---- 现场启动按钮：用粘性模式；从未选过 ⇒ 拒绝 ----
            # [start_edge 直接置位] 真实沿来自按钮站 DI 扫描（debounce 后
            # 置 start_edge）；单元层直接置同一个沿标志，只验 _handle_buttons
            # 的判定逻辑（沿的产生是按钮站自己的已锁语义）。
            ctl = business.auto_controller
            ctl.stop(ctl.ctx.now_ms(), reason="准备现场按钮用例")
            ctl.mode = ""
            # [粘性分账 2026-11-18] 「从未选过」要同时清**用户选中的模式**
            # （selected_mode）：现场启动按钮用的是它（= C 端 s_mode）。
            ctl.selected_mode = ""
            ctl._estop_latched = False
            ctl._fault_latched = False
            business.button_station.start_edge = True
            ctl._handle_buttons(ctl.ctx.now_ms(), auto_mode=True)
            assert not ctl.running, "未选模式时现场启动应被拒"

            ctl.mode = "produce"
            ctl.selected_mode = "produce"
            business.button_station.start_edge = True
            ctl._handle_buttons(ctl.ctx.now_ms(), auto_mode=True)
            assert ctl.running, "现场启动按钮应按粘性模式受理"
            assert ctl.phase == ctl.PHASE_VALVES

            # ---- 阀复位开关（2026-11-12）：停止/急停按模式配置 ----
            # [全部走**协议**触发] 全停是**异步投递**（stop_all_devices 用
            # get_event_loop 投递到服务端 loop）⇒ 必须从服务端 loop 内调用
            # 才投得出去；客户端线程直接调 ctl.stop() 会投递失败（假动作）。
            # 所以这里走真实入口：0x0115 的 op=2/op=3 与 0x0105 切模式。
            # [判据用**阀状态**而不是日志] 阀已到位时 set_valve 幂等返回
            # 「已关到位，忽略重复命令」⇒ 日志里根本没有"命令关"；状态才
            # 是事实（1=关到位 2=开到位）。
            # [上面的用例已把 c 关掉了] 这里重开一条连接再发协议。
            c = Client("127.0.0.1", server.port)
            c.hello()
            c.login("root", "root")

            def _valve_do(idx):
                """阀的**输出**位（DO0..5）。

                [为什么看 DO 而不是阀状态] 假数字量模块的 DI 是静态的
                （写 DO 不会改反馈）⇒ 阀永远到不了位，状态恒为"动作中"；
                真正反映"有没有下发关阀命令"的是 **DO 输出位**。真机上有
                反馈时两者一致（DO 关 + 反馈到关位）。
                """
                return bool(business.digital[0].do[idx])

            def _set_valve(idx, on):
                """在**本线程**跑一次阀命令（set_valve 是 async；假设备的
                写 DO 不碰真 IO ⇒ 独立 loop 里跑安全，不与服务端 loop 抢）。
                """
                loop = asyncio.new_event_loop()
                try:
                    loop.run_until_complete(
                        business.valve_bank.set_valve(idx, on))
                finally:
                    loop.close()

            def _stop_auto():
                return c.request(proto.CMD_AUTO_CONTROL,
                                 bson.encode({"op": proto.AUTO_OP_STOP}))

            def _estop_auto():
                return c.request(proto.CMD_AUTO_CONTROL,
                                 bson.encode({"op": proto.AUTO_OP_ESTOP}))

            # ① 停止：不复位 ⇒ 阀保持现状（先把它开到开位）
            _set_valve(0, True)
            _time.sleep(0.1)
            business.cfg.auto["produce"][
                proto.AUTO_MODE_RESET_VALVES_ON_STOP] = False
            ctl.mode = "produce"
            _stop_auto()
            _time.sleep(0.3)
            check("停止：模式配置为不复位 ⇒ 阀保持现状（仍开）",
                  _valve_do(0) is True,
                  "do=%s" % _valve_do(0))
            # ② 停止：勾选 ⇒ 阀关回
            business.cfg.auto["produce"][
                proto.AUTO_MODE_RESET_VALVES_ON_STOP] = True
            ctl.mode = "produce"
            _stop_auto()
            _time.sleep(0.3)
            check("停止：勾选配置 ⇒ 阀关回原位",
                  _valve_do(0) is False,
                  "do=%s" % _valve_do(0))
            # ③ 急停：缺省不复位（阀保持现状），勾选才关阀。
            _set_valve(0, True)
            _time.sleep(0.1)
            business.cfg.auto["produce"][
                proto.AUTO_MODE_RESET_VALVES_ON_ESTOP] = False
            ctl.mode = "produce"
            _estop_auto()
            _time.sleep(0.3)
            check("急停：缺省配置 ⇒ 阀保持现状（仍开）",
                  ctl.phase == ctl.PHASE_ESTOP
                  and ctl._estop_latched is True
                  and _valve_do(0) is True,
                  "phase=%s do=%s" % (ctl.phase, _valve_do(0)))
            c.request(proto.CMD_AUTO_CONTROL,
                      bson.encode({"op": proto.AUTO_OP_RESET}))
            business.cfg.auto["produce"][
                proto.AUTO_MODE_RESET_VALVES_ON_ESTOP] = True
            _estop_auto()
            _time.sleep(0.3)
            check("急停：勾选配置 ⇒ 阀关回原位",
                  ctl.phase == ctl.PHASE_ESTOP
                  and _valve_do(0) is False,
                  "phase=%s do=%s" % (ctl.phase, _valve_do(0)))
            # ④ 切模式的全停**不受**这两个开关影响（既有安全契约）。
            _set_valve(0, True)
            _time.sleep(0.1)
            business.cfg.auto["produce"][
                proto.AUTO_MODE_RESET_VALVES_ON_STOP] = False
            business.cfg.auto["produce"][
                proto.AUTO_MODE_RESET_VALVES_ON_ESTOP] = False
            assert _set_mode(c, business.MODE_MANUAL)[1].get("ok") is True
            _time.sleep(0.3)
            check("切回手动：仍全停（含阀），不受模式配置影响",
                  _valve_do(0) is False,
                  "do=%s" % _valve_do(0))
            # ---- [2026-11-18 板端实锅] 物理启动 ≡ 屏幕启动 ----
            # （放在场景末尾：本段会启停控制器、扰动阀状态 ⇒ 放中间会带崩
            #   后面「阀复位开关」那几条用例。）            ctl.stop(ctl.ctx.now_ms(), reason="准备现场按钮用例")
            ctl._estop_latched = False
            ctl._fault_latched = False
            # ① 运行中改选：被受理，且**运行中的模式不被改写**
            ctl._running = True
            ctl.mode = "produce"
            ctl.selected_mode = "produce"
            # [连接] 复用本段之前已登录的 c（root 账户**单会话**：再开一条
            # 连接登录会被拒 ⇒ 「尚未登录」）。
            r = c.request(proto.CMD_AUTO_CONTROL, bson.encode(
                {"op": proto.AUTO_OP_SELECT,
                 proto.KEY_AUTO_MODE: "rinse"}))
            assert r and r[1].get("ok") is True, \
                "运行中改选应被受理（只记选择）：%r" % (r[1],)
            assert ctl.mode == "produce", \
                "运行中的模式不应被改选影响：%s" % ctl.mode
            assert ctl.selected_mode == "rinse", \
                "粘性应记为新选择：%s" % ctl.selected_mode
            ctl._running = False
            # ② 现场启动按钮按**用户选中的模式**跑（不是运行中遗留的旧模式）
            #    [反证] 旧口径取 self.mode（此处 = "rinse"）⇒ 该模式未配置
            #    ⇒ 会被「未配置」拒掉 ⇒ 下面两条断言必红。
            ctl.mode = "rinse"            # 运行中遗留的旧模式（未配置）
            ctl.selected_mode = "produce"  # 用户当前选中的（已配置）
            business.button_station.start_edge = True
            ctl._handle_buttons(ctl.ctx.now_ms(), auto_mode=True)
            assert ctl.running, "物理启动应按用户选中的模式受理"
            assert ctl.mode == "produce", \
                "物理启动应按用户选中的模式跑：%s" % ctl.mode
            ctl.stop(ctl.ctx.now_ms(), reason="现场按钮用例收尾")


        except Exception as exc:                    # noqa: BLE001
            import traceback
            errors.append("%s\n%s" % (exc, traceback.format_exc()))

    thread = threading.Thread(target=scenario, name="auto-client")
    thread.start()
    try:
        await _feed_while(business, thread)
    finally:
        thread.join()
        await server.stop()
    if errors:
        raise RuntimeError(errors[0])


async def _feed_while(business, thread):
    """客户端线程跑期间，**替 start_polling 喂假设备**。

    [为什么需要] build_fake_app 场景不起 start_polling（那要真端口/连接）；
    而阀反馈的新鲜度闸认 `module.last_ok_ms`（设备 poll 才刷新）。不喂的
    话 6 路阀全部「数据不新鲜」，反馈永远进不来——实锅复现：不加这个
    feeder，阀相直接超时报警（阀状态从未离开构造缺省）。假设备 poll 与
    真实采集循环同接口，这里就是把它按拍驱动起来。
    """
    import time as _time
    while thread.is_alive():
        now = int(_time.monotonic() * 1000)
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


# ============================================================================
#                        单元级：配置校验（不起服务端）
# ============================================================================

def test_mode_config_error_variants():
    """mode_config_error：空配置/组合模式/未选模式/设备缺失（两个入口同源）。"""
    import time as _time

    class _FakeValve(object):
        def __init__(self, ready):
            self.ready = ready

    class _FakeBank(object):
        def __init__(self, ready_flags):
            self.items = [_FakeValve(r) for r in ready_flags]

    class _FakeApp(object):
        digital = []
        valve_bank = _FakeBank([True] * 6)
        analog = None
        vfd = object()
        temphum = None

        def __init__(self, auto_cfg, vfd=object()):
            self.cfg = type("C", (), {"auto": auto_cfg})()
            self.vfd = vfd

    auto_cfg = {"produce": {"devices": [], "stir_rpm": 1440,
                            "stir_reverse": False},
                "backwash": {"devices": ["valve2", "stirPump"],
                             "stir_rpm": 1440, "stir_reverse": False},
                "rinse": {"devices": ["stirPump"], "stir_rpm": 1440,
                          "stir_reverse": False}}
    app = _FakeApp(auto_cfg)
    ctx = autocontrol.AutoContext(app, log=lambda m: None)
    ctl = autocontrol.AutoController(ctx, log=lambda m: None)

    check("空配置 -> 提示去远端设置配",
          "未配置" in ctl.mode_config_error("produce"), "")
    check("配置齐 -> 可跑", ctl.mode_config_error("backwash") == "", "")
    # [组合模式 2026-11-12 已实现] 不再是"暂未实现"；现在要么可跑、要么
    # 给出**具体的循环配置原因**（循环未启用 / 三个子模式缺设备）。
    _cycle_err = ctl.mode_config_error(proto.AUTO_MODE_AUTO)
    check("组合模式 -> 不再报「未实现」，给具体循环配置原因",
          "未实现" not in _cycle_err
          and (_cycle_err == "" or "循环" in _cycle_err), _cycle_err)
    check("未知 id -> 拒绝", ctl.mode_config_error("ghost") != "", "")
    check("未选模式 -> 拒绝", ctl.mode_config_error("") != "", "")
    # 未配置点位的选中阀 -> 拒绝
    app.valve_bank.items[1].ready = False
    check("选中阀未配点位 -> 拒绝并点名",
          "阀2" in ctl.mode_config_error("backwash"), "")
    # 选中搅拌泵但变频器未装配 -> 拒绝
    app2 = _FakeApp(auto_cfg, vfd=None)
    ctl2 = autocontrol.AutoController(autocontrol.AutoContext(
        app2, log=lambda m: None), log=lambda m: None)
    check("搅拌泵无变频器 -> 拒绝",
          "变频器" in ctl2.mode_config_error("rinse"), "")


def test_run_time_accumulates_only_while_running():
    """累计自动运行时长：只在 running 期间累加，停止后冻结并保留。

    [口径] 服务端是唯一事实源（客户端断线/重启会归零）。多次运行是
    **累计**（用户口径「自动运行时间」）。
    """
    import time as _time

    class _FakeApp(object):
        digital = []
        valve_bank = None
        analog = None
        vfd = None
        temphum = None
        cfg = type("C", (), {"auto": {}})()

    ctx = autocontrol.AutoContext(_FakeApp(), log=lambda m: None)
    ctl = autocontrol.AutoController(ctx, log=lambda m: None)

    check("未启动时累计为 0", ctl.run_ms() == 0, "%s" % ctl.run_ms())
    # 起跑（用可控时钟：直接改 _run_start_ms，避免真等）
    ctl._running = True
    ctl._run_start_ms = ctl.ctx.now_ms() - 5000      # 已经跑了 5 秒
    running_ms = ctl.run_ms()
    check("运行中：累计按当前时刻算（≥5s）", running_ms >= 5000,
          "%s" % running_ms)
    # 停止 ⇒ 结算并冻结
    ctl._running = False
    ctl._settle_run_time()
    frozen = ctl.run_ms()
    _time.sleep(0.05)
    check("停止后：冻结不再增长", ctl.run_ms() == frozen,
          "%s vs %s" % (frozen, ctl.run_ms()))
    # 再启动 ⇒ 继续累加（累计而非单次）
    ctl._running = True
    ctl._run_start_ms = ctl.ctx.now_ms() - 3000
    check("再次运行：累计叠加（≥8s）", ctl.run_ms() >= frozen + 3000,
          "%s vs %s" % (ctl.run_ms(), frozen))
    ctl._running = False
    ctl._settle_run_time()


def test_precondition_order_fault_before_estop():
    """报警锁存与急停锁存都在时，先报报警（配置→安全层层可读）。"""
    import time as _time

    class _FakeApp(object):
        digital = []
        valve_bank = None
        analog = None
        vfd = None
        temphum = None
        cfg = type("C", (), {"auto": {}})()

    ctx = autocontrol.AutoContext(_FakeApp(), log=lambda m: None)
    ctl = autocontrol.AutoController(ctx, log=lambda m: None)
    ctl._fault_latched = True
    ctl._estop_latched = True
    ok, why = ctl.start_preconditions(True)
    check("双锁存时先报报警", (not ok) and "报警" in why, why)
    ctl._fault_latched = False
    ok, why = ctl.start_preconditions(True)
    check("只剩急停锁存时报急停", (not ok) and "急停" in why, why)


def main():
    """独立跑入口（与 pytest 收集等价；退出码 0=全过）。"""
    failed = []
    for name, fn in sorted(globals().items()):
        if name.startswith("test_") and callable(fn):
            try:
                fn()
                print("  [PASS] %s" % name)
            except AssertionError as exc:
                failed.append(name)
                print("  [FAIL] %s：%s" % (name, exc))
            except Exception as exc:                    # noqa: BLE001
                failed.append(name)
                print("  [ERROR] %s：%s: %s" % (name, type(exc).__name__, exc))
    print("\n===== 三模式自动序列: %d failed（check PASS %d / FAIL %d）====="
          % (len(failed), PASS, FAIL))
    return 1 if failed or FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
