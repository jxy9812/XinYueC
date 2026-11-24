# -*- coding: utf-8 -*-
"""自动循环**顶层循环次数**（2026-11-27 用户「自动循环模式，远端设置中
顶层的也支持下循环次数，支持一直循环，或指定循环次数后停止」）。

[冻结契约]
  ① 配置：`auto.cycle` 新增协议键 `topRepeat`（int，0..999）；0 = 一直循环
     （缺省，保持既有行为）；N>=1 = **展开计划**（含嵌套组重复）完整跑完
     N 轮后自动停止。配置文件落盘键 `top_repeat`（snake_case）。
  ② 取值：SET 非整数 ⇒ **fail-closed 拒**（文案含 topRepeat）；整数越界
     **钳位**到 0..999（与 minutes 同口径）。GET 快照的 cycle.topRepeat
     必须是 int。
  ③ 停止语义（达到 N 轮）：等价现场停止按钮 —— stop_devices=True（按
     resetValvesOnStop 配置关阀）、清暂停记忆（下次启动从头）、**不调
     reset()**（不得清急停/报警锁存）。
  ④ 计数口径：一轮 = 展开计划全部阶段跑完、**从末位回绕到 0** 时算一轮；
     到 N 轮的那个回绕点**直接停**，不再进入新一轮。停止＝暂停（人按停止）
     保留轮次，再启动续跑不清零；reset() 与「轮次完成自动停止」后下次启动
     从第 1 轮开始。
  ⑤ 运行中改 topRepeat **不立即生效**（启动时快照，停止再启动才生效——
     与 instances 同口径）。
  ⑥ 状态键：autoRound（1-based；0=不在循环）/ autoRoundTotal（0=一直循环）。

[测什么]
  A 配置面：缺省 / SET / GET / 落盘往返 / **三条 SET 路径**（带 instances
    / 显式实例写穿 / 旧键合成链）/ 钳位 / 非整数拒 / 配置文件读盘两条路径。
  B 执行面（假时钟：每拍把阶段起点往回拨 61s ⇒ 定时 1 分钟到期即换相）：
    计数、到点停（相位/日志/全停/重启从头）、一直循环、暂停保留、续跑不
    清零、复位清零、快照口径、单模式。
  C 状态键：`_auto_state_extra` 下发 autoRound / autoRoundTotal 的口径。

[怎么跑]
    python -I tests/test_auto_top_repeat.py
"""

import json
import os
import shutil
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))))

# [输出编码 2026-11-14] 中文 Windows 默认代码页 cp936 ⇒ 打印中文断言名时
# 不会因编码打断整套回归（写不出去也不崩）。
if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")

from hsfsrv import app as app_mod          # noqa: E402
from hsfsrv import autocontrol             # noqa: E402
from hsfsrv import config as cfgmod        # noqa: E402
from hsfsrv import proto                   # noqa: E402

PASS = 0
FAIL = 0
FAILED = []


def check(name, cond, detail=""):
    global PASS, FAIL
    if cond:
        PASS += 1
        print("  [PASS] %s" % name)
    else:
        FAIL += 1
        FAILED.append(name)
        print("  [FAIL] %s  %s" % (name, detail))


def _inst(mode, dev, **kw):
    """一个可跑的循环实例：定时 1 分钟（假时钟一拍即到期）、配一台设备。"""
    d = proto.default_cycle_instance(mode)
    d["devices"] = [dev]
    d["timer_enabled"] = True
    d["timer_minutes"] = 1
    d.update(kw)
    return d


def _grp(title, children, repeat=1):
    """一个组合实例（组内 children × repeat 遍）。"""
    d = proto.default_cycle_instance(proto.CYCLE_INST_GROUP)
    d["title"] = title
    d["children"] = list(children)
    d["repeat"] = repeat
    return d


def _new_app():
    """最小 app 夹具（只给 cfg；与 test_auto_nested_group 同款）。"""
    app = app_mod.App.__new__(app_mod.App)
    app.cfg = cfgmod.Config()
    return app


def _file_auto_cycle(path):
    """读**落盘文件**里的 auto.cycle 节（验证写盘形状而不是内存态）。"""
    with open(path, encoding="utf-8") as fh:
        doc = json.load(fh)
    return ((doc.get(proto.CFG_SECTION_AUTO) or {})
            .get(proto.CFG_AUTO_CYCLE) or {})


# ============================================================================
# A. 配置面
# ============================================================================
def part_a_config():
    print("\n=== A. 配置面（缺省 / SET / GET / 落盘 / 三条路径 / 钳位）===")
    app = _new_app()
    cfg = app.cfg

    # A1 缺省
    check("A1 缺省 top_repeat = 0（一直循环，既有行为不变）且协议常量同值",
          cfg.auto_cycle.get("top_repeat") == 0
          and proto.AUTO_CYCLE_TOP_REPEAT_DEF == 0
          and proto.CFG_AUTO_CYCLE_TOP_REPEAT == "topRepeat"
          and proto.AUTO_CYCLE_TOP_REPEAT_MAX == 999,
          "%r / %r" % (cfg.auto_cycle.get("top_repeat"),
                       proto.AUTO_CYCLE_TOP_REPEAT_DEF))

    # A2 SET（旧键合成链路径：不带 instances）
    err = app._cfg_set_auto_cycle({proto.CFG_AUTO_CYCLE_TOP_REPEAT: 3})
    check("A2 SET topRepeat=3 受理（旧键合成链路径）且落成 top_repeat",
          err == "" and cfg.auto_cycle.get("top_repeat") == 3,
          "%r / %r" % (err, cfg.auto_cycle.get("top_repeat")))

    # A3 GET 快照
    cyc = app._config_auto_doc()["cycle"]
    check("A3 GET 快照 cycle.topRepeat=3 且是 int",
          cyc.get(proto.CFG_AUTO_CYCLE_TOP_REPEAT) == 3
          and isinstance(cyc.get(proto.CFG_AUTO_CYCLE_TOP_REPEAT), int),
          "%r (%s)" % (cyc.get(proto.CFG_AUTO_CYCLE_TOP_REPEAT),
                       type(cyc.get(proto.CFG_AUTO_CYCLE_TOP_REPEAT))))

    # A4 落盘往返
    tmp = tempfile.mkdtemp(prefix="hsf_top_repeat_")
    try:
        path = os.path.join(tmp, "x.json")
        cfg.config_path = path
        cfg.users_path = os.path.join(tmp, "u.json")
        cfg.save()
        cfg2 = cfgmod.Config()
        cfg2.config_path = path
        cfg2.users_path = os.path.join(tmp, "u.json")
        cfg2.load()
        check("A4 落盘后新 Config 读回 top_repeat=3",
              cfg2.auto_cycle.get("top_repeat") == 3,
              "%r" % (cfg2.auto_cycle.get("top_repeat"),))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    # A5 非整数拒（fail-closed，文案含键名，值不变）
    before = cfg.auto_cycle.get("top_repeat")
    err = app._cfg_set_auto_cycle({proto.CFG_AUTO_CYCLE_TOP_REPEAT: "两次"})
    check("A5 非整数（字符串）拒且文案含 topRepeat、旧值不变",
          err != "" and proto.CFG_AUTO_CYCLE_TOP_REPEAT in err
          and cfg.auto_cycle.get("top_repeat") == before, err)

    # A6 非 str 形态也拒（None / 数组 —— fail-closed 不静默当 0）
    err_none = app._cfg_set_auto_cycle(
        {proto.CFG_AUTO_CYCLE_TOP_REPEAT: None})
    err_list = app._cfg_set_auto_cycle(
        {proto.CFG_AUTO_CYCLE_TOP_REPEAT: [3]})
    check("A6 None / 数组同样拒（不静默当成 0）且旧值不变",
          err_none != "" and err_list != ""
          and cfg.auto_cycle.get("top_repeat") == before,
          "%r / %r" % (err_none, err_list))

    # A7 / A8 越界钳位（与 minutes 同口径：不报错）
    err_neg = app._cfg_set_auto_cycle({proto.CFG_AUTO_CYCLE_TOP_REPEAT: -5})
    check("A7 负数钳位到 0（受理）",
          err_neg == "" and cfg.auto_cycle.get("top_repeat") == 0,
          "%r / %r" % (err_neg, cfg.auto_cycle.get("top_repeat")))
    err_big = app._cfg_set_auto_cycle({proto.CFG_AUTO_CYCLE_TOP_REPEAT: 5000})
    check("A8 5000 钳位到上限 999（受理）",
          err_big == "" and cfg.auto_cycle.get("top_repeat") == 999,
          "%r / %r" % (err_big, cfg.auto_cycle.get("top_repeat")))

    # A9 带 instances 的同包（实例键优先路径）—— topRepeat 不属于实例键，
    # 必须照常生效。
    err = app._cfg_set_auto_cycle({
        proto.CFG_AUTO_CYCLE_TOP_REPEAT: 2,
        proto.CFG_AUTO_CYCLE_INSTANCES: [
            _inst("produce", "valve1"), _inst("rinse", "valve3")]})
    check("A9 带 instances 同包 topRepeat=2 生效（instances 成为显式事实源）",
          err == "" and cfg.auto_cycle.get("top_repeat") == 2
          and cfg.auto_cycle.get("instances_explicit") is True,
          "%r / %r / %r" % (err, cfg.auto_cycle.get("top_repeat"),
                            cfg.auto_cycle.get("instances_explicit")))

    # A10 显式实例下**不带 instances** 的旧键写穿路径：topRepeat 也生效
    # （这条路径在 _cfg_set_auto_cycle 里是提前 return 的，最容易漏）。
    err = app._cfg_set_auto_cycle({
        proto.CFG_AUTO_CYCLE_TOP_REPEAT: 7,
        proto.CFG_AUTO_CYCLE_PRESS_THRESHOLD: 0.6})
    check("A10 显式实例写穿路径 topRepeat=7 生效（旧键照常写穿）",
          err == "" and cfg.auto_cycle.get("top_repeat") == 7
          and (cfg.auto_cycle.get("instances") or [{}])[0].get(
              "press_threshold") == 0.6,
          "%r / %r / %r" % (err, cfg.auto_cycle.get("top_repeat"),
                            (cfg.auto_cycle.get("instances") or [{}])[0]
                            .get("press_threshold")))

    # A11 写盘形状：落盘文件里是 snake_case 的 top_repeat（不是 topRepeat）
    tmp = tempfile.mkdtemp(prefix="hsf_top_repeat_doc_")
    try:
        path = os.path.join(tmp, "x.json")
        cfg.config_path = path
        cfg.users_path = os.path.join(tmp, "u.json")
        cfg.save()
        doc = _file_auto_cycle(path)
        check("A11 落盘键名是 top_repeat（snake_case，与 cycle 其它键同风格）",
              doc.get("top_repeat") == 7
              and proto.CFG_AUTO_CYCLE_TOP_REPEAT not in doc
              and cfg._auto_cycle_file_doc().get("top_repeat") == 7,
              "%r" % ({k: doc.get(k) for k in ("top_repeat", "topRepeat")},))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    # A12 配置文件读盘：**显式 instances 分支提前 return** 的路径也要读到
    # top_repeat（放在 return 之后就静默丢了）。
    cfgx = cfgmod.Config()
    cfgx._load_auto_cycle({
        "instances": [_inst("produce", "valve1"), _inst("rinse", "valve3")],
        "top_repeat": 4})
    check("A12 配文件读盘（显式 instances 提前 return 路径）top_repeat=4",
          cfgx.auto_cycle.get("top_repeat") == 4
          and cfgx.auto_cycle.get("instances_explicit") is True,
          "%r / %r" % (cfgx.auto_cycle.get("top_repeat"),
                       cfgx.auto_cycle.get("instances_explicit")))

    # A13 配置文件的非法值 ⇒ 回缺省（手改写坏不该让服务起不来）
    cfgy = cfgmod.Config()
    cfgy._load_auto_cycle({"top_repeat": "坏数据"})
    check("A13 配文件非法 top_repeat ⇒ 宽送回缺省 0（服务起得来）",
          cfgy.auto_cycle.get("top_repeat") == 0,
          "%r" % (cfgy.auto_cycle.get("top_repeat"),))

    # A14 [2026-11-27 复核] 顶层**单个组合**也要能落盘→重启：SET/启动闸早
    # 就允许「只有 1 个组合」（组内多模式构成循环），读盘若仍要求 ≥2 ⇒
    # 组合与 topRepeat 重启后一起丢（实测过：reload 后 explicit=False /
    # instances=0）。这里锁 save→新 Config→load 三步。
    tmp = tempfile.mkdtemp(prefix="hsf_top_repeat_grp_")
    try:
        app3 = _new_app()
        err = app3._cfg_set_auto_cycle({
            proto.CFG_AUTO_CYCLE_TOP_REPEAT: 4,
            proto.CFG_AUTO_CYCLE_INSTANCES: [
                _grp("组A", [_inst("produce", "valve1"),
                             _inst("rinse", "valve3")])]})
        path3 = os.path.join(tmp, "x.json")
        app3.cfg.config_path = path3
        app3.cfg.users_path = os.path.join(tmp, "u.json")
        app3.cfg.save()
        cfg3 = cfgmod.Config()
        cfg3.config_path = path3
        cfg3.users_path = os.path.join(tmp, "u.json")
        cfg3.load()
        ins3 = cfg3.auto_cycle.get("instances") or []
        check("A14 顶层单个组合 + topRepeat=4 落盘→重启仍在"
              "（explicit=True/instances=1/top=4）",
              err == "" and cfg3.auto_cycle.get("instances_explicit") is True
              and len(ins3) == 1
              and str(ins3[0].get("mode") or "") == proto.CYCLE_INST_GROUP
              and cfg3.auto_cycle.get("top_repeat") == 4,
              "err=%r explicit=%r n=%d top=%r"
              % (err, cfg3.auto_cycle.get("instances_explicit"), len(ins3),
                 cfg3.auto_cycle.get("top_repeat")))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    # A15 [2026-11-27 复核] 非整数**不许静默截断**：float 3.5 原实现走
    # int(raw) 变 3（契约要求 fail-closed）。值为整数的 3.0 视为整数。
    app4 = _new_app()
    app4._cfg_set_auto_cycle({proto.CFG_AUTO_CYCLE_TOP_REPEAT: 5})
    before4 = app4.cfg.auto_cycle.get("top_repeat")
    err_f = app4._cfg_set_auto_cycle({proto.CFG_AUTO_CYCLE_TOP_REPEAT: 3.5})
    stored_after_f = app4.cfg.auto_cycle.get("top_repeat")
    err_s = app4._cfg_set_auto_cycle({proto.CFG_AUTO_CYCLE_TOP_REPEAT: "3.0"})
    err_ok = app4._cfg_set_auto_cycle({proto.CFG_AUTO_CYCLE_TOP_REPEAT: 3.0})
    check("A15 非整数（3.5 / '3.0'）拒且不改值；3.0（值为整数）受理",
          err_f != "" and proto.CFG_AUTO_CYCLE_TOP_REPEAT in err_f
          and stored_after_f == before4
          and err_s != "" and err_ok == ""
          and app4.cfg.auto_cycle.get("top_repeat") == 3,
          "3.5→%r(存 %r) / '3.0'→%r / 3.0→%r(存 %r)"
          % (err_f, stored_after_f, err_s, err_ok,
             app4.cfg.auto_cycle.get("top_repeat")))


# ============================================================================
# B. 执行面（假时钟：每拍把阶段起点往回拨 61s ⇒ 定时 1 分钟到期即换相）
# ============================================================================
class _FakeCfg(object):
    """AutoContext 会用到的 cfg 面（粘性/累计运行时长落盘 + 模式配置）。"""
    auto = {}
    stir_max_rpm = 1440
    valve_move_timeout_ms = 20000

    def __init__(self, cycle):
        self.auto_cycle = cycle

    def auto_run_ms(self):
        return 0

    def save_auto_run_ms(self, _total):
        pass

    def auto_mode(self):
        return ""

    def save_auto_mode(self, _mode):
        pass


class _FakeApp(object):
    def __init__(self, cycle):
        self.analog = None
        self.digital = []
        self.vfd = None
        self.pump_bank = None
        self.cfg = _FakeCfg(cycle)

    def is_auto(self):
        return True


def _mk(top_repeat):
    """两阶段循环（产水 → 顺洗，各定时 1 分钟）+ 指定顶层循环次数。"""
    cycle = {"enabled": True,
             "instances": [_inst("produce", "valve1"),
                           _inst("rinse", "valve3")]}
    if top_repeat is not None:
        cycle["top_repeat"] = top_repeat
    app = _FakeApp(cycle)
    ctx = autocontrol.AutoContext(app, log=lambda m: None)
    ctx.press_mpa = lambda: None
    logs = []
    ctl = autocontrol.AutoController(ctx, log=lambda m: logs.append(m))
    # [全停投递要运行中的 loop] 夹具里换成记录桩：既避免"无法投递"日志，又能
    # 断言"轮次跑完 = 等价现场停止"（stop_devices=True + reset_valves）。
    stops = []
    ctl.stop_all_devices = (lambda reason="", reset_valves=True:
                           stops.append((reason, reset_valves)))
    ok = ctl.start(mode=proto.AUTO_MODE_AUTO)
    ctl.phase = ctl.PHASE_RUNNING
    return ctl, ctx, app, logs, stops, ok


def _tick(ctl, ctx):
    """推进一拍：阶段起点往回拨 61s（定时 1 分钟到期）⇒ 换相一次。

    [为什么每拍要复位相位] `_cycle_switch` 换相后回到阀相（`_begin_mode_phase`
    置 PHASE_VALVES）；真实运行由阀/泵相推进到 running。夹具直接放回运行相
    （与 test_auto_nested_* 同款）。
    """
    ctl._cycle_stage_at = ctx.now_ms() - 61 * 1000
    ctl._cycle_tick(ctx.now_ms())
    if ctl.running:
        ctl.phase = ctl.PHASE_RUNNING


def part_b_execute():
    print("\n=== B. 执行面（计数 / 到点停 / 暂停保留 / 快照口径）===")

    # ---- B1~B7：topRepeat=2 完整跑两轮 ----
    ctl, ctx, app, logs, stops, ok = _mk(2)
    check("B1 组合循环启动即第 1 轮、总轮数读启动快照（1/2）",
          ok and ctl.cycle_round() == 1
          and ctl.cycle_round_total() == 2,
          "%r / %r / %r" % (ok, ctl.cycle_round(),
                            ctl.cycle_round_total()))

    _tick(ctl, ctx)                     # 产水 → 顺洗（非回绕）
    check("B2 第一次换相（0→1，非回绕）轮次不变（仍第 1 轮）",
          ctl._cycle_stage_idx == 1 and ctl.cycle_round() == 1
          and ctl.running, "%r / %r" % (ctl._cycle_stage_idx,
                                        ctl.cycle_round()))

    _tick(ctl, ctx)                     # 顺洗 → 产水（回绕，第一次）
    check("B3 第 1 次回绕 ⇒ 第 2 轮且仍在跑（不到 2 轮不停）",
          ctl._cycle_stage_idx == 0 and ctl.cycle_round() == 2
          and ctl.running and ctl.phase == ctl.PHASE_RUNNING,
          "%r / %r / %r" % (ctl._cycle_stage_idx, ctl.cycle_round(),
                            ctl.running))

    _tick(ctl, ctx)                     # 产水 → 顺洗（第 2 轮里）
    check("B4 第 2 轮内换相（0→1）轮次仍为 2",
          ctl._cycle_stage_idx == 1 and ctl.cycle_round() == 2,
          "%r / %r" % (ctl._cycle_stage_idx, ctl.cycle_round()))

    _tick(ctl, ctx)                     # 顺洗 → 回绕点 ⇒ 到 2 轮 ⇒ 自动停
    check("B5 第 2 次回绕（到设定轮数）⇒ 自动停止、不再进入第 3 轮",
          not ctl.running and ctl._cycle_stage_idx == -1
          and ctl.cycle_round() == 0,
          "running=%r idx=%r round=%r" % (ctl.running, ctl._cycle_stage_idx,
                                          ctl.cycle_round()))
    check("B6 停止相位不是 paused（轮次跑完≠暂停，不保留续跑记忆）",
          ctl.phase != ctl.PHASE_PAUSED
          and not ctl.pause_info()["paused"]
          and ctl.pause_info()["stageIdx"] == -1,
          "%r / %r" % (ctl.phase, ctl.pause_info()))
    check("B7 日志含「已完成设定循环 2 轮」",
          any("已完成设定循环 2 轮" in m for m in logs),
          [m for m in logs if "已完成设定循环" in m][:2])
    check("B8 等价现场停止：stop_devices=True（按 resetValvesOnStop 关阀）"
          "且 reason 带轮次",
          len(stops) == 1 and stops[0][1] is True
          and "已完成设定循环 2 轮" in stops[0][0],
          "%r" % (stops,))

    # 再次启动：从首实例、第 1 轮开始
    ok2 = ctl.start(mode=proto.AUTO_MODE_AUTO)
    check("B9 自动停止后再启动 ⇒ 从下标 0 / 第 1 轮开始",
          ok2 and ctl._cycle_stage_idx == 0 and ctl.cycle_round() == 1,
          "%r / %r / %r" % (ok2, ctl._cycle_stage_idx, ctl.cycle_round()))

    # ---- B10：topRepeat=0 一直循环（连续 3 次回绕不停）----
    ctl0, ctx0, _app0, logs0, stops0, ok0 = _mk(0)
    for _ in range(6):                  # 6 拍 = 3 次回绕
        _tick(ctl0, ctx0)
    check("B10 topRepeat=0 ⇒ 连续 3 次回绕仍不停（第 4 轮、running）",
          ok0 and ctl0.running and ctl0.cycle_round() == 4
          and ctl0.cycle_round_total() == 0 and not stops0,
          "round=%r running=%r total=%r stops=%r"
          % (ctl0.cycle_round(), ctl0.running, ctl0.cycle_round_total(),
             stops0))

    # ---- B11~B13：人按停止＝暂停 ⇒ 轮次保留、续跑不清零 ----
    ctl1, ctx1, _app1, _logs1, _stops1, _ = _mk(5)
    _tick(ctl1, ctx1)
    _tick(ctl1, ctx1)                   # 回绕一次 ⇒ 第 2 轮
    check("B11 人按停止前：第 2 轮（回绕 1 次）",
          ctl1.cycle_round() == 2, ctl1.cycle_round())
    ctl1.stop(ctx1.now_ms())            # 普通停止（不传 stop_devices）
    check("B12 人按停止＝暂停 ⇒ 轮次保留、相位 paused",
          not ctl1.running and ctl1.paused
          and ctl1.phase == ctl1.PHASE_PAUSED
          and ctl1.cycle_round() == 2,
          "round=%r phase=%r paused=%r" % (ctl1.cycle_round(), ctl1.phase,
                                           ctl1.paused))
    paused_idx = ctl1.pause_info()["stageIdx"]
    ctl1.start(mode=proto.AUTO_MODE_AUTO)
    check("B13 再启动续跑 ⇒ 轮次不清零（仍第 2 轮）、阶段下标不变",
          ctl1.running and ctl1.cycle_round() == 2
          and ctl1._cycle_stage_idx == paused_idx,
          "round=%r idx=%r want=%r" % (ctl1.cycle_round(),
                                       ctl1._cycle_stage_idx, paused_idx))

    # ---- B14~B15：reset() 清零 ----
    ctl1.stop(ctx1.now_ms())
    ctl1.reset()
    check("B14 reset() 后轮次归 0（人工确认现场 ⇒ 从头重来）",
          ctl1.cycle_round() == 0 and not ctl1.paused,
          "%r / %r" % (ctl1.cycle_round(), ctl1.paused))
    ctl1.start(mode=proto.AUTO_MODE_AUTO)
    check("B15 reset() 后再启动 ⇒ 第 1 轮",
          ctl1.cycle_round() == 1, ctl1.cycle_round())

    # ---- B16：运行中改 topRepeat 不立即生效（启动快照口径）----
    ctl2, ctx2, app2, logs2, stops2, _ = _mk(2)
    app2.cfg.auto_cycle["top_repeat"] = 9      # 运行中改配置
    check("B16 运行中改 topRepeat=9 ⇒ 快照仍报 2（不立即生效）",
          ctl2.cycle_round_total() == 2, ctl2.cycle_round_total())
    for _ in range(4):                          # 跑满 2 轮的两拍回绕
        _tick(ctl2, ctx2)
    check("B17 仍按启动快照的 2 轮停（不是按新配置的 9 轮）",
          not ctl2.running and any("已完成设定循环 2 轮" in m for m in logs2)
          and len(stops2) == 1,
          "running=%r stops=%r" % (ctl2.running, stops2))

    # ---- B18：单模式启动轮次 0（不在循环）----
    ctl3, ctx3, _app3, _logs3, _stops3, _ = _mk(2)
    ctl3.stop(ctx3.now_ms())
    ctl3._clear_pause()
    ctl3.reset()
    ok3 = ctl3.start(mode="produce")
    check("B18 单模式启动 ⇒ 轮次 0（不在组合循环里）",
          ok3 and ctl3.cycle_round() == 0,
          "%r / %r" % (ok3, ctl3.cycle_round()))

    # ---- B19：轮次跑完自动停止**不是 reset()**（锁存不得被清）----
    ctl4, ctx4, _app4, _logs4, stops4, _ = _mk(1)
    ctl4._estop_latched = True
    ctl4._fault_latched = True
    for _ in range(2):                  # 两阶段 ⇒ 两拍一次回绕
        _tick(ctl4, ctx4)
    check("B19 轮次跑完自动停止**不清**急停/报警锁存（契约：不调 reset()）",
          not ctl4.running and ctl4.estop_latched and ctl4.fault_latched
          and len(stops4) == 1,
          "running=%r estop=%r fault=%r stops=%r"
          % (ctl4.running, ctl4.estop_latched, ctl4.fault_latched, stops4))

    # ---- B20/B21：顶层单组合的两种展开（2026-11-27 复核）----
    # [口径] 判据是**展开计划**长度（`_cycle_tick` 对 <2 项不换相 ⇒ 放行
    # 会"永不计数/永不自动停"）。展开 1 项 ⇒ 启动闸必须拒；展开 2 项
    # （组内 1 个子模式 repeat=2）⇒ 照常可跑并按 topRepeat 停。
    def _mk_group_cfg(repeat, top=2):
        cfg = cfgmod.Config()
        cfg.auto_cycle.update({
            "enabled": True, "top_repeat": top,
            "instances": [_grp("独组", [_inst("produce", "valve1")],
                               repeat=repeat)],
            "instances_explicit": True})
        fa = _FakeApp(cfg.auto_cycle)
        fa.cfg = cfg                      # 换真 Config（生产同接口）
        cx = autocontrol.AutoContext(fa, log=lambda m: None)
        cx.press_mpa = lambda: None
        lg = []
        c = autocontrol.AutoController(cx, log=lambda m: lg.append(m))
        sp = []
        c.stop_all_devices = (lambda reason="", reset_valves=True:
                             sp.append((reason, reset_valves)))
        return c, cx, lg, sp

    ctl5, _ctx5, _lg5, _sp5 = _mk_group_cfg(repeat=1)
    why5 = ctl5.mode_config_error(proto.AUTO_MODE_AUTO)
    check("B20 单个组合但展开只有 1 项 ⇒ 启动闸拒（与 tick 不换相同口径）",
          why5 != "" and ("只有 1 个" in why5 or "至少" in why5),
          why5)

    ctl6, ctx6, lg6, sp6 = _mk_group_cfg(repeat=2)
    why6 = ctl6.mode_config_error(proto.AUTO_MODE_AUTO)
    ok6 = ctl6.start(mode=proto.AUTO_MODE_AUTO)
    ctl6.phase = ctl6.PHASE_RUNNING
    for _ in range(4):                    # 展开 2 项 ⇒ 两拍一轮 × 2 轮
        ctl6._cycle_stage_at = ctx6.now_ms() - 61 * 1000
        ctl6._cycle_tick(ctx6.now_ms())
        if ctl6.running:
            ctl6.phase = ctl6.PHASE_RUNNING
    check("B21 单个组合 1 子模式 × repeat=2（展开 2 项）⇒ 可跑并按 2 轮停",
          why6 == "" and ok6 and len(ctl6._cycle_stages()) == 2
          and not ctl6.running and len(sp6) == 1
          and any("已完成设定循环 2 轮" in m for m in lg6),
          "why=%r ok=%r stages=%d running=%r stops=%r"
          % (why6, ok6, len(ctl6._cycle_stages()), ctl6.running, sp6))


# ============================================================================
# C. 状态键（app._auto_state_extra）
# ============================================================================
def part_c_state_keys():
    print("\n=== C. 状态键（autoRound / autoRoundTotal）===")
    app = app_mod.App.__new__(app_mod.App)
    app.cfg = cfgmod.Config()
    # `_auto_state_extra` 末尾要"最近报警"（读库）⇒ 夹具里给个空实现。
    app._last_alarm_doc = lambda: None
    ctx = autocontrol.AutoContext(app, log=lambda m: None)
    ctl = autocontrol.AutoController(ctx, log=lambda m: None)
    app.auto_controller = ctl

    doc = app._auto_state_extra()
    check("C1 状态快照**总是**带 autoRound / autoRoundTotal 两键（int，"
          "不在循环时为 0）",
          isinstance(doc.get(proto.KEY_AUTO_ROUND), int)
          and isinstance(doc.get(proto.KEY_AUTO_ROUND_TOTAL), int)
          and doc.get(proto.KEY_AUTO_ROUND) == 0
          and doc.get(proto.KEY_AUTO_ROUND_TOTAL) == 0,
          "%r / %r" % (doc.get(proto.KEY_AUTO_ROUND),
                       doc.get(proto.KEY_AUTO_ROUND_TOTAL)))

    # 运行中：round=2 / total=2（快照口径）
    app.cfg.auto_cycle["top_repeat"] = 2
    ctl.mode = proto.AUTO_MODE_AUTO
    ctl._cycle_cfg = ctl._cycle_config()
    ctl._cycle_round = 2
    ctl._running = True
    ctl._cycle_root = proto.AUTO_MODE_AUTO
    doc = app._auto_state_extra()
    check("C2 运行中口径：autoRound=2 / autoRoundTotal=2",
          doc.get(proto.KEY_AUTO_ROUND) == 2
          and doc.get(proto.KEY_AUTO_ROUND_TOTAL) == 2,
          "%r / %r" % (doc.get(proto.KEY_AUTO_ROUND),
                       doc.get(proto.KEY_AUTO_ROUND_TOTAL)))

    # 不在循环（无运行/无暂停/无记忆）⇒ autoRound 0（不编造）
    ctl._running = False
    ctl._cycle_root = ""
    ctl._paused = False
    ctl._cycle_resume_mode = ""
    doc = app._auto_state_extra()
    check("C3 不在循环 ⇒ autoRound=0（不把历史轮次当当前轮次编造）",
          doc.get(proto.KEY_AUTO_ROUND) == 0,
          doc.get(proto.KEY_AUTO_ROUND))

    # 没有控制器（老装配/桩）⇒ 0/0 且整条状态快照不失败
    app.auto_controller = None
    doc = app._auto_state_extra()
    check("C4 无控制器 ⇒ 两键 0（状态快照不失败、不编造）",
          doc.get(proto.KEY_AUTO_ROUND) == 0
          and doc.get(proto.KEY_AUTO_ROUND_TOTAL) == 0,
          "%r / %r" % (doc.get(proto.KEY_AUTO_ROUND),
                       doc.get(proto.KEY_AUTO_ROUND_TOTAL)))


# ============================================================================
# D. 实例命名（2026-11-27 复核：文案与运行期 `_title` 必须同一算法）
# ============================================================================
def part_d_naming():
    print("\n=== D. 实例命名（cycle_instance_titles 与展开 _title 一致 / "
          "同层唯一）===")
    # D1 原始实例列表（SET/校验文案用）与展开计划（运行期/界面用）同名
    raw = [_inst("produce", "valve1"), _inst("produce", "valve2")]
    want = [p["_title"] for p in proto.expand_cycle_plan(raw)]
    got = proto.cycle_instance_titles(raw)
    check("D1 cycle_instance_titles(原始) 与展开 _title 逐字一致"
          "（同类 ≥2 ⇒ 都是「产水模式1/2」）",
          got == want == ["产水模式1", "产水模式2"],
          "raw=%r plan=%r" % (got, want))

    # D2 自定义名与默认名撞车 ⇒ 同层唯一（默认形态按客户端规则重排号）
    raw2 = [_inst("produce", "valve1", title="产水模式2"),
            _inst("produce", "valve2")]
    t2 = [p["_title"] for p in proto.expand_cycle_plan(raw2)]
    check("D2 自定义「产水模式2」+ 默认产水 ⇒ 不与默认名重复"
          "（默认形态重排为 1/2）",
          t2 == ["产水模式1", "产水模式2"] and len(set(t2)) == 2, t2)

    # D3 两个相同的非默认自定义名 ⇒ 后出现者加数字后缀（同层唯一）
    raw3 = [_inst("produce", "valve1", title="我的产水"),
            _inst("produce", "valve2", title="我的产水")]
    t3 = [p["_title"] for p in proto.expand_cycle_plan(raw3)]
    check("D3 同层两个同名自定义 ⇒ 后出现者追加数字后缀（唯一）",
          t3 == ["我的产水", "我的产水2"] and len(set(t3)) == 2, t3)

    # D4 [别过度去重] 组的 repeat 遍数天然产生同名项（既有口径：组名/遍数
    # 由 autoRep* 表达），同层唯一规则不得跨 repeat 去重。
    t4 = [p["_title"] for p in proto.expand_cycle_plan(
        [_grp("组A", [_inst("rinse", "valve3")], repeat=2)])]
    check("D4 组 repeat=2 ⇒ 展开两项同名（不跨重复遍数去重）",
          t4 == ["组A/顺洗模式", "组A/顺洗模式"], t4)


if __name__ == "__main__":
    print("===== 自动循环顶层循环次数（2026-11-27）=====")
    part_a_config()
    part_b_execute()
    part_c_state_keys()
    part_d_naming()
    print("\n===== 汇总：PASS %d / FAIL %d =====" % (PASS, FAIL))
    if FAILED:
        print("失败项：")
        for n in FAILED:
            print("  - " + n)
        sys.exit(1)
