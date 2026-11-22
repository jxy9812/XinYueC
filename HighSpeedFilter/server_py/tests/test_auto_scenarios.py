# -*- coding: utf-8 -*-
"""自动模式**全场景**实测（2026-11-14 用户口径「后面自动模式的各种情况都要
实际测试下，保证无 bug」）。

[覆盖矩阵]
==========
**第一段（单元级 · 快）**：换相策略的**完整动作矩阵**
    3 策略 × 2 设备（搅拌泵/进水泵）× 2 方向（新模式要它 / 不要它）= 12 组
    ⇒ 每组都断言「换相窗口内该泵到底发生了什么」：
        stopRestart ⇒ 立刻停（不管新模式要不要它）
        carry      ⇒ 立刻**不动**（保持上一模式的状态）；
                      新模式不要它 ⇒ 记进待停集合（泵相收尾必须停）
        direct     ⇒ 立刻按**新模式**的目标启停

**第二段（端到端 · 真 sim + 真协议）**：策略在**真实换相**里的可观测行为
    两组配置各跑 3 个策略 = 6 组：
    (A) 产水带泵 → 反洗不带泵：区分「carry（泵继续转）」与「停（stopRestart/
        direct）」
    (B) 产水不带泵 → 反洗带泵：区分「direct（换相窗口内泵就起来）」与
        「stopRestart（要等阀到位进泵相才起）」
    观测口径：**采样 (子模式, 相位, 变频器在转?, 进水泵 DO)**，取
    「切到反洗且还在阀相」的那组样本（即换相窗口内）。

[怎么跑]
    python -I tests/test_auto_scenarios.py       # 独立跑
    python -I tests/run_all.py                   # 全量（含本段）
"""

import asyncio
import os
import shutil
import sys
import tempfile
import threading
import time

# [输出编码 2026-11-14] 中文 Windows 默认代码页 cp936 ⇒ stdout 被重定向
# 到文件时用 GBK ⇒ 打印「⇒」(U+21D2，**GBK 编不了**) 直接
# UnicodeEncodeError ⇒ **整套测试在打印断言时被打断**（实测退出码仍是 0，
# 看着像"跑完了"）。强制 UTF-8 + errors="replace"（写不出去也不崩）。
for _stream in (sys.stdout, sys.stderr):
    try:
        _stream.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError, OSError):
        pass

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.dirname(HERE))

from hsfsrv import app as app_mod              # noqa: E402
from hsfsrv import auth as auth_mod            # noqa: E402
from hsfsrv import autocontrol                 # noqa: E402
from hsfsrv import bson                        # noqa: E402
from hsfsrv import config as cfgmod            # noqa: E402
from hsfsrv import proto, xlink                # noqa: E402
from test_vfd_ops import Client                # noqa: E402

PASS = 0
FAIL = 0

#: [2026-11-18 配置化] 参与循环的模式列表（**由配置决定**；缺省＝
#: 产水→反洗→顺洗，与旧硬编码链一致）。断言里的"下一个阶段"一律从这份
#: 列表算（`CYCLE_NEXT`），不写死「反洗」⇒ 以后改配置不用改测试。
CYCLE_STAGES = list(proto.AUTO_CYCLE_MODES_DEF)
CYCLE_FIRST = CYCLE_STAGES[0]
CYCLE_NEXT = CYCLE_STAGES[(CYCLE_STAGES.index(CYCLE_FIRST) + 1)
                           % len(CYCLE_STAGES)]


def check(name, ok, detail=""):
    global PASS, FAIL
    if ok:
        PASS += 1
        print("  [PASS] %s %s" % (name, detail))
    else:
        FAIL += 1
        print("  [FAIL] %s %s" % (name, detail))


# ============================================================================
# 第一段：策略动作矩阵（单元级）
# ============================================================================

class _FakePumpBank(object):
    def __init__(self):
        self.calls = []
        self.items = [0]

    async def set_pump(self, index, on):
        self.calls.append((index, bool(on)))
        return True, ""


class _FakeVfd(object):
    def __init__(self):
        self.calls = []

    async def apply_control(self, op, reverse=False, hz=0.0, rpm=0.0,
                            value=0.0, index=0):
        self.calls.append((op, bool(reverse), float(rpm)))
        return True, ""


class _FakeApp(object):
    def __init__(self, modes):
        self.digital = []
        self.valve_bank = None
        self.analog = None
        self.temphum = None
        self.button_station = None
        self.pump_bank = _FakePumpBank()
        self.vfd = _FakeVfd()
        self.cfg = type("C", (), {"auto": modes})()


def _mk_ctl(modes):
    app = _FakeApp(modes)
    ctx = autocontrol.AutoContext(app, log=lambda m: None)
    return autocontrol.AutoController(ctx, log=lambda m: None)


async def _switch_and_settle(ctl, new_mode):
    ctl._apply_switch_policies(new_mode)
    for _ in range(50):
        await asyncio.sleep(0.01)
        if not ctl._phase_tasks:
            break


async def test_policy_matrix():
    """第一段：3 策略 × 2 设备 × 2 方向 = 12 组动作矩阵。"""
    print("\n=== 第一段：换相策略动作矩阵（12 组）===")
    for dev_id, key in (("stirPump", proto.AUTO_MODE_STIR_SWITCH),
                         ("inletPump", proto.AUTO_MODE_INLET_SWITCH)):
        for policy in (proto.AUTO_SWITCH_POLICY_STOP_RESTART,
                       proto.AUTO_SWITCH_POLICY_CARRY,
                       proto.AUTO_SWITCH_POLICY_DIRECT):
            for want in (False, True):
                label = "%s/%s/%s" % (dev_id, policy,
                                       "新模式要" if want else "新模式不要")
                devs = ["valve1"] + ([dev_id] if want else [])
                modes = {"produce": {"devices": ["valve1", dev_id]},
                         "backwash": {"devices": devs, key: policy}}
                ctl = _mk_ctl(modes)
                await _switch_and_settle(ctl, "backwash")
                vfd_calls = ctl.ctx.vfd_snapshot().calls
                pump_calls = ctl.ctx.pump_bank().calls
                if dev_id == "stirPump":
                    stopped = any(c[0] == proto.OP_STOP for c in vfd_calls)
                    started = any(c[0] == proto.OP_START for c in vfd_calls)
                    acted = stopped or started
                else:
                    stopped = any(c[1] is False for c in pump_calls)
                    started = any(c[1] is True for c in pump_calls)
                    acted = bool(pump_calls)
                if policy == proto.AUTO_SWITCH_POLICY_STOP_RESTART:
                    ok = acted and stopped and not started
                    detail = "应当场停（vfd=%s pump=%s）" % (vfd_calls, pump_calls)
                elif policy == proto.AUTO_SWITCH_POLICY_CARRY:
                    ok = (not acted) if want else True
                    if not want:
                        ok = (not acted) and dev_id in ctl._carry_pending_stop
                    detail = ("应当场不动%s（待停集合=%s）"
                              % ("（新模式要它）" if want else "", 
                                 ctl._carry_pending_stop))
                else:   # direct
                    ok = acted and (started if want else stopped)
                    detail = "应当场按新模式%s（vfd=%s pump=%s）" % (
                        "启动" if want else "停", vfd_calls, pump_calls)
                check("矩阵 %s" % label, ok, detail)


# ============================================================================
# 第二段：端到端（真 sim + 真协议）
# ============================================================================

async def _spin(basedir, log, move_ms=250, valve_timeout_ms=800):
    """起一份真实 sim 服务端（虚拟设备 + 真协议栈）。"""
    cfg = cfgmod.Config()
    cfg.resolve_paths(basedir)
    cfg.config_path = os.path.join(basedir, "cfg", "x.json")
    cfg.users_path = os.path.join(basedir, "cfg", "hsf_users.json")
    os.makedirs(os.path.join(basedir, "cfg"), exist_ok=True)
    cfg.port = 0
    cfg.sim_enabled = True
    cfg.sim_valve_move_ms = move_ms
    cfg.valve_move_timeout_ms = valve_timeout_ms
    cfg.button_debounce_ms = 1
    cfg.storage.enabled = False
    users = auth_mod.UserStore(cfg.users_path)
    users.create_defaults()
    business = app_mod.App(cfg, log=log.append)
    business.build_devices()
    server = xlink.Server(cfg.port, users, log=log.append, require_auth=True)
    business.attach(server)
    await server.start()
    await business.start_polling()
    await asyncio.sleep(0.3)
    return cfg, business, server


def _vfd_running(business):
    try:
        return bool(business.vfd.is_running())
    except Exception:                               # noqa: BLE001
        return False


def _inlet_do(business):
    """进水泵 DO6 的当前值（1=开）。"""
    try:
        return int(business.digital[0].do[6])
    except Exception:                               # noqa: BLE001
        return -1


def _case_switch_window(business, server, log, tag, policy, produce_devs,
                         backwash_devs, expect):
    """跑一次组合循环换相，采样**换相窗口内**的泵状态并断言。

    :param expect: dict，键 = 'stir_running'/'inlet_on'（期望值：True/False
        或 None=不校验）；另可给 'stir_in_valve_phase'（direct 的判据）。
    """
    c = Client("127.0.0.1", server.port)
    c.hello()
    ok, _role = c.login("root", "root")
    assert ok, "root 登录失败"

    def cfg_auto():
        # [阈值取当前压差的 0.9 倍 + 「小于才切」] ⇒ 现在不切（压差高于
        # 阈值）；等产水跑稳后**注入**一个低压差（模拟量通道 0 的 mV 拉低）
        # ⇒ 压差低于阈值 ⇒ **受控地**在那一刻换相 ⇒ 换相窗口可干净观测。
        # [为什么不用固定阈值] 固定阈值要么一开始就切（产水还没跑稳、泵还在
        # 爬坡 ⇒ 观测到的是竞态），要么永远不切。
        p0 = business.auto_ctx.press_mpa() or 0.5
        thr = round(max(0.01, p0 * 0.9), 3)
        doc = {"section": proto.CFG_SECTION_AUTO,
               "modes": {
                   "produce": {"devices": list(produce_devs), "stirRpm": 720},
                   "backwash": {"devices": list(backwash_devs), "stirRpm": 720,
                                proto.AUTO_MODE_STIR_SWITCH: policy,
                                proto.AUTO_MODE_INLET_SWITCH: policy},
                   "rinse": {"devices": ["valve3"], "stirRpm": 720}},
               "cycle": {"enabled": True, "pressThreshold": thr,
                         "pressCompare": proto.AUTO_PRESS_COMPARE_LT,
                         "timerFallback": False, "timerMinutes": 30,
                         "backwashMinutes": 10, "rinseMinutes": 10,
                         # [2026-11-18 配置化] 参与循环的模式与顺序**显式
                         # 下发**（期望值也从这份列表算，不写死三段链）。
                         proto.CFG_AUTO_CYCLE_MODES: list(CYCLE_STAGES)}}
        r = c.request(proto.CMD_CONFIG_SET, bson.encode(doc))
        assert r and r[1].get("ok") is True, "auto SET 失败：%r" % (
            r[1] if r else None)
        return thr

    def q():
        r = c.request(proto.CMD_RUN_MODE, b"")
        return r[1] if r else {}

    cfg_auto()
    # [诊断] GET 回读：服务端到底把策略存进去没有（存的是"缺省"⇒ 是配置
    # 链路的问题；存对了但日志说 stop ⇒ 是控制器读取的问题）。
    _g = c.request(proto.CMD_CONFIG_GET, b"")
    _auto = (_g[1].get(proto.CFG_SECTION_AUTO) or {}) if _g else {}
    _m = (_auto.get("modes") or {}).get("backwash") or {}
    check("%s：配置回读：策略键已存进服务端" % tag,
          _m.get(proto.AUTO_MODE_STIR_SWITCH) == policy
          and _m.get(proto.AUTO_MODE_INLET_SWITCH) == policy,
          "stir=%s inlet=%s" % (_m.get(proto.AUTO_MODE_STIR_SWITCH),
                                _m.get(proto.AUTO_MODE_INLET_SWITCH)))
    # [诊断] 控制器**自己眼里的**配置（同一进程 ⇒ 直接读它的 ctx）。
    _ctrl_cfg = business.auto_ctx.auto_mode_config("backwash")
    check("%s：控制器读到的策略＝配置里的策略" % tag,
          _ctrl_cfg.get(proto.AUTO_MODE_STIR_SWITCH) == policy,
          "控制器看到=%s（cfg.auto 键=%s）"
          % (_ctrl_cfg.get(proto.AUTO_MODE_STIR_SWITCH),
             sorted(_ctrl_cfg.keys())))
    r = c.request(proto.CMD_RUN_MODE, bson.encode({"mode": 1}))
    assert r and r[1].get("ok") is True, "切自动模式失败"
    r = c.request(proto.CMD_AUTO_CONTROL,
                  bson.encode({"on": True,
                               proto.KEY_AUTO_MODE: proto.AUTO_MODE_AUTO}))
    assert r and r[1].get("ok") is True, "启动组合模式失败：%r" % (
        r[1] if r else None)

    # 采样：等主运行阶段跑稳（运行相 + 该有的泵已起来），再注入低压差触发换相。
    want_pumps_up = ("stirPump" in produce_devs) or ("inletPump" in produce_devs)
    ready = False
    t0 = time.time()
    while time.time() - t0 < 25.0:
        st = q()
        if st.get(proto.KEY_AUTO_PHASE) == "running":
            if not want_pumps_up or (_vfd_running(business)
                                     and _inlet_do(business) == 1):
                ready = True
                break
        time.sleep(0.02)
    check("%s：主运行阶段[%s]跑到运行相（泵已起来）" % (tag, CYCLE_FIRST),
          ready, "")
    if not ready:
        c.request(proto.CMD_AUTO_CONTROL, bson.encode({"on": False}))
        c.close()
        return
    # [受控触发] 把模拟量通道 0 的 mV 拉低 ⇒ 压差低于阈值 ⇒ 换相。
    _analog_model = getattr(business.analog._master, "model", None)
    _mv_before = list(getattr(_analog_model, "mv", []) or [])
    if _analog_model is not None and _mv_before:
        _analog_model.mv[0] = 1000
    log_mark = len(log)
    # 采样：找「子模式=参与列表的下一个[%s] 且 相位=valves」的换相窗口样本。
    window = None
    deadline = time.time() + 25.0
    samples = []
    while time.time() < deadline:
        st = q()
        phase = st.get(proto.KEY_AUTO_PHASE)
        sub = st.get(proto.KEY_AUTO_MODE)
        vfd = _vfd_running(business)
        inlet = _inlet_do(business)
        samples.append((sub, phase, vfd, inlet))
        if sub == CYCLE_NEXT and phase == "valves":
            window = (vfd, inlet)
            break
        time.sleep(0.01)
    c.request(proto.CMD_AUTO_CONTROL, bson.encode({"on": False}))
    c.close()
    if _analog_model is not None and _mv_before:
        _analog_model.mv = _mv_before
    check("%s：观察到换相窗口（子模式=%s且仍在阀相）" % (tag, CYCLE_NEXT),
          window is not None,
          "采样尾=%s" % (samples[-3:],))
    # [诊断] 服务端日志里有「换相策略 搅拌泵=… 进水泵=…」一行 ⇒ 直接看
    # 服务端**本次换相**读到的策略（只看本次之后的行——多轮共用一个服务端
    # ⇒ 取最后一行会拿到别的用例的）。
    policy_lines = [ln for ln in log[log_mark:] if "换相策略" in ln]
    if expect.get("log_stir"):
        check("%s：服务端按策略%s处置搅拌泵" % (tag, expect["log_stir"]),
              any(("搅拌泵=%s" % expect["log_stir"]) in ln
                  for ln in policy_lines),
              "%s" % (policy_lines[-1:] or "无日志"))
    if window is None:
        return
    vfd, inlet = window
    if expect.get("stir_running") is not None:
        check("%s：换相窗口内搅拌泵 %s" % (
            tag, "在转" if expect["stir_running"] else "已停"),
            vfd is expect["stir_running"], "vfd_running=%s" % vfd)
    if expect.get("inlet_on") is not None:
        check("%s：换相窗口内进水泵 %s" % (
            tag, "开" if expect["inlet_on"] else "关"),
            bool(inlet) is expect["inlet_on"], "do6=%s" % inlet)


async def _case_e2e(base, log):
    """第二段：端到端换相行为（6 组）。

    [每组一个全新服务端] 共用一个服务端会让上一组的残留状态污染下一组
    （实测：上一组停机后变频器仍在降速 ⇒ 下一组「启动」被设备级单飞闸
    拒成"设备忙" ⇒ 产水永远进不了运行相 ⇒ 观测到的是**假红**）。
    ⇒ 每组都新起一份 sim（起停 ~1s，6 组共 ~10s）。
    """
    print("\n=== 第二段：端到端换相行为（真 sim + 真协议）===")
    cases = (
        # (A) 产水带泵 → 反洗不带泵：区分「carry（泵继续转）」与「停」。
        ("A/stopRestart", proto.AUTO_SWITCH_POLICY_STOP_RESTART,
         ["valve1", "stirPump", "inletPump"], ["valve2"],
         {"stir_running": False, "inlet_on": False,
          "log_stir": "stopRestart"}),
        ("A/carry", proto.AUTO_SWITCH_POLICY_CARRY,
         ["valve1", "stirPump", "inletPump"], ["valve2"],
         {"stir_running": True, "inlet_on": True, "log_stir": "carry"}),
        ("A/direct", proto.AUTO_SWITCH_POLICY_DIRECT,
         ["valve1", "stirPump", "inletPump"], ["valve2"],
         {"stir_running": False, "inlet_on": False, "log_stir": "direct"}),
        # (B) 产水不带泵 → 反洗带泵：区分「direct（换相窗口内泵就起来）」
        # 与「stopRestart（要等阀到位进泵相才起）」。
        ("B/stopRestart", proto.AUTO_SWITCH_POLICY_STOP_RESTART,
         ["valve1"], ["valve2", "stirPump", "inletPump"],
         {"stir_running": False, "inlet_on": False,
          "log_stir": "stopRestart"}),
        ("B/direct", proto.AUTO_SWITCH_POLICY_DIRECT,
         ["valve1"], ["valve2", "stirPump", "inletPump"],
         {"stir_running": True, "inlet_on": True, "log_stir": "direct"}),
        ("B/carry", proto.AUTO_SWITCH_POLICY_CARRY,
         ["valve1"], ["valve2", "stirPump", "inletPump"],
         {"stir_running": False, "inlet_on": False, "log_stir": "carry"}),
    )
    for tag, policy, prod_devs, back_devs, expect in cases:
        cfg, business, server = await _spin(base, log)
        errors = []

        def scenario(tag=tag, policy=policy, prod_devs=prod_devs,
                     back_devs=back_devs, expect=expect, business=business,
                     server=server):
            try:
                _case_switch_window(business, server, log, tag, policy,
                                    prod_devs, back_devs, expect)
            except Exception as exc:                # noqa: BLE001
                import traceback
                errors.append("%s\n%s" % (exc, traceback.format_exc()))

        th = threading.Thread(target=scenario, name="e2e-%s" % tag)
        th.start()
        while th.is_alive():
            await asyncio.sleep(0.05)
        th.join(120)
        for err in errors:
            check("端到端 %s" % tag, False, err)
        await server.stop()


def main():
    base = tempfile.mkdtemp(prefix="hsf_auto_scn_")
    log = []
    try:
        asyncio.run(test_policy_matrix())
        asyncio.run(_case_e2e(base, log))
    finally:
        shutil.rmtree(base, ignore_errors=True)
    print("\n汇总：PASS %d / FAIL %d" % (PASS, FAIL))
    return 0 if FAIL == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
