# -*- coding: utf-8 -*-
"""自动循环模式（组合模式「自动」）回归（2026-11-12 用户定案；
2026-11-18 升级为「参与模式 + 顺序 + 每阶段各自的切换条件」可配）。

[测什么] 循环的**参与模式列表、顺序、以及每个阶段各自的切换条件**都由
配置决定（auto.cycle 子节的 cycleModes / stageCfg）：

    ① 压差触发：启用 + 阈值 + 方向（大于/小于）；压差读不到时**不切**
       （不误判）；
    ② 定时触发：启用 + 分钟数；本阶段已耗时 ≥ 分钟 ⇒ 切；
    ③ 两者**先到先切**（互不排斥）；某个阶段两个都没开 ⇒ 配置非法 ⇒
       启动闸明确拒绝（fail-closed）；
    ④ 换相 = 停泵 + 按新模式重建阀目标（回阀相），累计运行时间不中断；
    ⑤ 配置校验：循环须启用 + 参与循环的模式都配设备（+ 设备级校验）；
    ⑥ 两元素列表闭环 / 顺序被改后严格按序推进 / 列表不含产水时按配置走 /
       空列表被拒 / 未知与重复模式 SET 被拒 / stageCfg 缺阶段配置被拒；
    ⑦ 阶段记忆：记忆的阶段不在新列表 ⇒ 兜底到**首元素**；
    ⑧ cycle_modes / stage_cfg 落盘往返**保序**；
    ⑨ **缺省配置的行为与改动前逐字一致**（老配置零改动仍按原样跑）。

[表驱动] 断言里的"下一个"一律**从配置列表算**（`stages[(i+1) % n]`），
不写死三段链 ⇒ 以后改配置不用改测试。

[怎么跑]
    python -I tests/test_auto_cycle.py          # 独立跑
    pytest tests/test_auto_cycle.py -q          # pytest 收集
"""

import asyncio
import os
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

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.dirname(HERE))

from hsfsrv import app as app_mod        # noqa: E402
from hsfsrv import autocontrol           # noqa: E402
from hsfsrv import config as cfgmod      # noqa: E402
from hsfsrv import proto                 # noqa: E402

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


# ---------------------------------------------------------------------------
# 夹具
# ---------------------------------------------------------------------------

#: 缺省参与列表（与旧硬编码链一致）。
DEF_STAGES = list(proto.AUTO_CYCLE_MODES_DEF)


def _stage(press_enabled=False, press_threshold=0.5, press_compare="gt",
           timer_enabled=True, timer_minutes=10):
    """一个阶段的切换条件（内部 snake_case 键＝配置面键名）。"""
    return {"press_enabled": bool(press_enabled),
            "press_threshold": float(press_threshold),
            "press_compare": str(press_compare),
            "timer_enabled": bool(timer_enabled),
            "timer_minutes": int(timer_minutes)}


def _default_stage_cfg(stages=None):
    """缺省切换条件（与产品规格逐字一致）：
    首元素＝压差触发（阈值 0.5 / gt）+ 定时**关**（30 分钟）；
    其余＝压差关 + 定时开（10 分钟）。
    """
    stages = list(stages or DEF_STAGES)
    out = {}
    for idx, mid in enumerate(stages):
        if idx == 0:
            out[mid] = _stage(True, 0.5, "gt", False, 30)
        else:
            out[mid] = _stage(False, 0.5, "gt", True, 10)
    return out


def _cycle_cfg(stages=None, stage_cfg=None, **kw):
    """造一份 auto_cycle 配置（缺省＝缺省值）。

    [为什么显式带 stage_cfg] 每阶段的切换条件现在**逐阶段可配** ⇒ 夹具
    必须能精确指定某个阶段的压差/定时条件（用例的前提）。
    """
    stages = list(stages if stages is not None else DEF_STAGES)
    cfg = {
        "enabled": True,
        "press_threshold": 0.5,
        "press_compare": proto.AUTO_PRESS_COMPARE_GT,
        "timer_fallback": False,
        "timer_minutes": 30,
        "backwash_minutes": 10,
        "rinse_minutes": 10,
        "cycle_modes": list(stages),
        "stage_cfg": (dict(stage_cfg) if stage_cfg is not None
                      else _default_stage_cfg(stages)),
    }
    cfg.update(kw)
    return cfg


def _modes_cfg(stages):
    """给列表里的每个模式配一台阀（放水也配 ⇒ 能进循环）。"""
    out = {}
    for i, mid in enumerate(stages):
        out[mid] = {"devices": ["valve%d" % (i + 1)]}
    return out


class _FakeApp(object):
    """最小 app 夹具：模拟量（压差）+ 模式设备配置 + 循环配置。"""

    def __init__(self, press=None, cycle=None, modes=None):
        self.digital = []
        self.valve_bank = None
        self.analog = None
        self.vfd = None
        self.temphum = None
        self.pump_bank = None
        self.button_station = None
        self._press = press
        modes = modes or {"produce": {"devices": ["valve1"]},
                          "backwash": {"devices": ["valve2"]},
                          "rinse": {"devices": ["valve3"]}}
        self.cfg = type("C", (), {
            "auto": modes,
            "auto_cycle": cycle if cycle is not None else _cycle_cfg(),
        })()


def _mk(press=None, cycle=None, modes=None, logs=None):
    app = _FakeApp(press=press, cycle=cycle, modes=modes)
    ctx = autocontrol.AutoContext(app, log=lambda m: None)
    # [压差读数] 夹具直接给 MPa（走 ctx.press_mpa 的口径）。
    ctx.press_mpa = lambda: app._press
    # [logs 可选] 传入 list 时控制器日志收进它（四触发「取靠前者」的
    # 顺序断言靠**切相理由文本**区分——只看 idx 分不出哪个触发先命中）。
    if logs is not None:
        ctl = autocontrol.AutoController(ctx, log=lambda m: logs.append(m))
    else:
        ctl = autocontrol.AutoController(ctx, log=lambda m: None)
    return ctl


def _enter_cycle(ctl, stage):
    """把控制器放到「组合模式 + 指定子阶段 + running」的状态。"""
    ctl.mode = proto.AUTO_MODE_AUTO
    ctl._cycle_root = proto.AUTO_MODE_AUTO
    ctl._cycle_cfg = ctl._cycle_config()
    ctl.mode = stage
    now = ctl.ctx.now_ms()
    ctl._cycle_stage_at = now
    # [单模式/无界阶段报"本次运行已用" 2026-11-23] 真实 start() 会同时置
    # `_run_start_ms`；夹具直接改状态时也要同款（否则 stage_timing 按
    # `_run_start_ms=0` 判"未真正起跑"⇒ (-1,0)，测不到无界阶段的已用）。
    ctl._run_start_ms = now
    ctl._running = True
    ctl.phase = ctl.PHASE_RUNNING


def _next_of(stages, mode):
    """配置列表里的"下一个"（最后一个的下一个＝第一个 ⇒ 闭环）。"""
    idx = list(stages).index(mode)
    return list(stages)[(idx + 1) % len(stages)]


def _elapsed(ctl, minutes):
    """把当前阶段的起点往回拨 minutes 分钟（模拟"已跑满"）。

    [无界阶段 2026-11-23] 组合循环没有定时的阶段改报"本次运行已用"
    （`_run_start_ms` 起算）⇒ 起点要同步往回拨，否则 `_elapsed()` 对
    无界阶段无效（表盘仍显示刚起步）。
    """
    delta = int(minutes * 60000)
    ctl._cycle_stage_at = ctl.ctx.now_ms() - delta
    if ctl._run_start_ms:
        ctl._run_start_ms -= delta


def _file_auto_cycle(tmp):
    """读**落盘文件**里的 auto.cycle 节（验证"写盘"而不是"内存态"）。"""
    import json
    with open(os.path.join(tmp, "x.json"), encoding="utf-8") as fh:
        doc = json.load(fh)
    return ((doc.get(proto.CFG_SECTION_AUTO) or {})
            .get(proto.CFG_AUTO_CYCLE) or {})


# ---------------------------------------------------------------------------
# ① 压差触发（表驱动：切换目标＝列表里的下一个）
# ---------------------------------------------------------------------------

def test_cycle_press_switch_to_next_stage():
    """① 压差达标 ⇒ 当前阶段切到**列表里的下一个**（两个方向各测一次）。"""
    stages = DEF_STAGES
    nxt = _next_of(stages, stages[0])
    # 大于：压差 0.8 > 阈值 0.5 ⇒ 切
    ctl = _mk(press=0.8)
    _enter_cycle(ctl, stages[0])
    ctl._cycle_tick(ctl.ctx.now_ms())
    check("压差 0.8 > 阈值 0.5 ⇒ 切到下一个阶段（%s）" % nxt,
          ctl.mode == nxt, ctl.mode)
    # 小于：压差 0.2 < 阈值 0.5 ⇒ 切（该阶段的 pressCompare=lt）
    sc = _default_stage_cfg(stages)
    sc[stages[0]] = _stage(True, 0.5, proto.AUTO_PRESS_COMPARE_LT, False, 30)
    ctl = _mk(press=0.2, cycle=_cycle_cfg(stages=stages, stage_cfg=sc))
    _enter_cycle(ctl, stages[0])
    ctl._cycle_tick(ctl.ctx.now_ms())
    check("压差 0.2 < 阈值 0.5 ⇒ 切到下一个阶段（%s）" % nxt,
          ctl.mode == nxt, ctl.mode)
    # 小于方向 + 压差偏大 ⇒ 不切
    ctl = _mk(press=0.8, cycle=_cycle_cfg(stages=stages, stage_cfg=sc))
    _enter_cycle(ctl, stages[0])
    ctl._cycle_tick(ctl.ctx.now_ms())
    check("小于方向 + 压差 0.8 ⇒ 不切（仍 %s）" % stages[0],
          ctl.mode == stages[0], ctl.mode)
    # 压差读不到（None）⇒ 不切（不按缺数据误判）
    ctl = _mk(press=None)
    _enter_cycle(ctl, stages[0])
    ctl._cycle_tick(ctl.ctx.now_ms())
    check("压差读不到 ⇒ 不切（不误判）", ctl.mode == stages[0], ctl.mode)


def test_stage_press_disabled_never_switches():
    """①b 该阶段**没启用**压差触发 ⇒ 压差再高也不切（逐阶段可配）。"""
    stages = DEF_STAGES
    sc = _default_stage_cfg(stages)
    sc[stages[0]] = _stage(False, 0.5, "gt", True, 30)   # 只留定时
    ctl = _mk(press=9.9, cycle=_cycle_cfg(stages=stages, stage_cfg=sc))
    _enter_cycle(ctl, stages[0])
    ctl._cycle_tick(ctl.ctx.now_ms())
    check("该阶段没启用压差触发 ⇒ 压差 9.9 也不切",
          ctl.mode == stages[0], ctl.mode)


# ---------------------------------------------------------------------------
# ② 定时触发（逐阶段；缺省：首元素关、其余 10 分钟）
# ---------------------------------------------------------------------------

def test_cycle_timer_switch():
    """② 定时触发：启用后到点切下一个；没启用/未到期都不切。"""
    stages = DEF_STAGES
    nxt = _next_of(stages, stages[1])
    # 反洗（缺省 10 分钟）到期 ⇒ 切
    ctl = _mk(press=0.1)
    _enter_cycle(ctl, stages[1])
    _elapsed(ctl, 11)
    ctl._cycle_tick(ctl.ctx.now_ms())
    check("%s 11 分钟到期 ⇒ 切到 %s" % (stages[1], nxt),
          ctl.mode == nxt, ctl.mode)
    # 未到期 ⇒ 不切
    ctl = _mk(press=0.1)
    _enter_cycle(ctl, stages[1])
    _elapsed(ctl, 9)
    ctl._cycle_tick(ctl.ctx.now_ms())
    check("%s 未到期 ⇒ 不切" % stages[1], ctl.mode == stages[1], ctl.mode)
    # 首阶段缺省**定时关** ⇒ 到点也不切
    ctl = _mk(press=0.1)
    _enter_cycle(ctl, stages[0])
    _elapsed(ctl, 99)
    ctl._cycle_tick(ctl.ctx.now_ms())
    check("首阶段定时缺省关 ⇒ 到期不切（等压差）",
          ctl.mode == stages[0], ctl.mode)
    # 首阶段开了定时 ⇒ 到期切
    sc = _default_stage_cfg(stages)
    sc[stages[0]] = _stage(True, 0.5, "gt", True, 30)
    ctl = _mk(press=0.1, cycle=_cycle_cfg(stages=stages, stage_cfg=sc))
    _enter_cycle(ctl, stages[0])
    _elapsed(ctl, 31)
    ctl._cycle_tick(ctl.ctx.now_ms())
    check("首阶段定时开 ⇒ 31 分钟到期切到 %s" % _next_of(stages, stages[0]),
          ctl.mode == _next_of(stages, stages[0]), ctl.mode)


def test_stage_only_timer_configured():
    """②b 某阶段**只配定时**（压差关）⇒ 只按定时切。"""
    stages = DEF_STAGES
    nxt = _next_of(stages, stages[1])
    sc = _default_stage_cfg(stages)
    sc[stages[1]] = _stage(False, 0.5, "gt", True, 3)   # 只定时 3 分钟
    ctl = _mk(press=9.9, cycle=_cycle_cfg(stages=stages, stage_cfg=sc))
    _enter_cycle(ctl, stages[1])
    _elapsed(ctl, 4)
    ctl._cycle_tick(ctl.ctx.now_ms())
    check("只配定时的阶段：到期切到 %s" % nxt, ctl.mode == nxt, ctl.mode)
    # 未到期不切
    ctl = _mk(press=9.9, cycle=_cycle_cfg(stages=stages, stage_cfg=sc))
    _enter_cycle(ctl, stages[1])
    _elapsed(ctl, 2)
    ctl._cycle_tick(ctl.ctx.now_ms())
    check("只配定时的阶段：未到期不切", ctl.mode == stages[1], ctl.mode)


def test_stage_only_press_configured():
    """②c 某阶段**只配压差**（定时关）⇒ 只按压差切（定时到点也不切）。"""
    stages = DEF_STAGES
    nxt = _next_of(stages, stages[1])
    sc = _default_stage_cfg(stages)
    sc[stages[1]] = _stage(True, 0.4, "gt", False, 10)  # 只压差
    ctl = _mk(press=0.8, cycle=_cycle_cfg(stages=stages, stage_cfg=sc))
    _enter_cycle(ctl, stages[1])
    ctl._cycle_tick(ctl.ctx.now_ms())
    check("只配压差的阶段：压差达标 ⇒ 切到 %s" % nxt,
          ctl.mode == nxt, ctl.mode)
    # 定时到点但压差没达标 ⇒ 不切（该阶段没开定时）
    ctl = _mk(press=0.1, cycle=_cycle_cfg(stages=stages, stage_cfg=sc))
    _enter_cycle(ctl, stages[1])
    _elapsed(ctl, 99)
    ctl._cycle_tick(ctl.ctx.now_ms())
    check("只配压差的阶段：定时到点也不切（没开定时）",
          ctl.mode == stages[1], ctl.mode)


def test_stage_both_triggers_first_wins():
    """②d 某阶段**两个都配** ⇒ 先到先切（压差先到 / 定时先到各测一次）。"""
    stages = DEF_STAGES
    nxt = _next_of(stages, stages[1])
    sc = _default_stage_cfg(stages)
    sc[stages[1]] = _stage(True, 0.5, "gt", True, 5)   # 压差 + 定时 5 分钟
    # 压差先到（时间没到）
    ctl = _mk(press=0.8, cycle=_cycle_cfg(stages=stages, stage_cfg=sc))
    _enter_cycle(ctl, stages[1])
    _elapsed(ctl, 1)
    ctl._cycle_tick(ctl.ctx.now_ms())
    check("两个都配：压差先到 ⇒ 切到 %s" % nxt, ctl.mode == nxt, ctl.mode)
    # 定时先到（压差不达标）
    ctl = _mk(press=0.1, cycle=_cycle_cfg(stages=stages, stage_cfg=sc))
    _enter_cycle(ctl, stages[1])
    _elapsed(ctl, 6)
    ctl._cycle_tick(ctl.ctx.now_ms())
    check("两个都配：定时先到 ⇒ 切到 %s" % nxt, ctl.mode == nxt, ctl.mode)


# ---------------------------------------------------------------------------
# ③ 表盘口径（stage_timing 与换相判定同一处）
# ---------------------------------------------------------------------------

def test_stage_timing_for_dial():
    """③ 主界面剩余时间表盘的抠数：stage_timing() = (已运行, 总时长)。

    口径（客户端只显示，服务端算）：
      · 该阶段**启用了定时** ⇒ 总时长 = 它的 timerMinutes；
      · 只按压差切（没启用定时）⇒ 总时长 0（不编造倒计时）；
      · 单模式**运行中** ⇒ 报本次运行已用、总时长 0（2026-11-23 用户
        口径，完整用例见 tests/test_auto_modes.py）；未运行 / 未真正起跑
        （`_run_start_ms=0`）⇒ (-1, 0)（不编造）。
    """
    stages = DEF_STAGES
    # 中间阶段 10 分钟，已跑 2 分钟
    mid = stages[1]
    ctl = _mk(press=0.1)
    _enter_cycle(ctl, mid)
    _elapsed(ctl, 2)
    elapsed, total = ctl.stage_timing()
    check("%s 2/10 分钟 ⇒ (120000, 600000)" % mid,
          total == 600000 and 120000 <= elapsed <= 121000,
          "(%s, %s)" % (elapsed, total))
    # 末阶段 10 分钟，刚起步
    last = stages[-1]
    ctl = _mk(press=0.1)
    _enter_cycle(ctl, last)
    elapsed, total = ctl.stage_timing()
    check("%s 刚起步 ⇒ 总时长 10 分钟、已运行≈0" % last,
          total == 600000 and 0 <= elapsed <= 1000,
          "(%s, %s)" % (elapsed, total))
    # 首阶段（缺省定时关）⇒ 无总时长（不编造倒计时）
    first = stages[0]
    ctl = _mk(press=0.1)
    _enter_cycle(ctl, first)
    _elapsed(ctl, 0.1)
    elapsed, total = ctl.stage_timing()
    check("首阶段（定时缺省关）⇒ 总时长 0（不编造倒计时）",
          total == 0 and elapsed >= 5000, "(%s, %s)" % (elapsed, total))
    # 首阶段开了定时 ⇒ 总时长 = 它的 timerMinutes
    sc = _default_stage_cfg(stages)
    sc[first] = _stage(True, 0.5, "gt", True, 30)
    ctl = _mk(press=0.1, cycle=_cycle_cfg(stages=stages, stage_cfg=sc))
    _enter_cycle(ctl, first)
    _elapsed(ctl, 1)
    elapsed, total = ctl.stage_timing()
    check("首阶段（定时开）⇒ 总时长 30 分钟",
          total == 30 * 60000 and 59000 <= elapsed <= 61000,
          "(%s, %s)" % (elapsed, total))
    # 单模式 running 但未真正起跑（`_run_start_ms=0`，没经过 start()）⇒
    # 不编造"本次已用"（真实运行中的单模式用例见 test_auto_modes.py）。
    ctl = _mk(press=0.1)
    ctl.mode = stages[1]
    ctl._running = True
    ctl.phase = ctl.PHASE_RUNNING
    check("单模式 running 但 _run_start_ms=0 ⇒ (-1, 0)（不编造）",
          ctl.stage_timing() == (-1, 0), "%s" % (ctl.stage_timing(),))
    # 未运行 ⇒ (-1, 0)（停止后不显示倒计时）
    ctl = _mk(press=0.1)
    _enter_cycle(ctl, stages[1])
    ctl._running = False
    check("未运行 ⇒ (-1, 0)", ctl.stage_timing() == (-1, 0),
          "%s" % (ctl.stage_timing(),))


# ---------------------------------------------------------------------------
# ④ 换相动作
# ---------------------------------------------------------------------------

def test_cycle_switch_rebuilds_valves():
    """④ 换相 ⇒ 按新模式重建阀目标（回阀相），累计运行时间不中断。"""
    stages = DEF_STAGES
    ctl = _mk(press=0.8)
    _enter_cycle(ctl, stages[0])
    # 让阀组"就绪"（夹具里 valve_count=0 ⇒ 目标表为空，这里只看相位与
    # 计时是否按换相重置）。
    before_ms = ctl.run_ms()
    ctl._cycle_tick(ctl.ctx.now_ms())
    check("换相后回阀相（重新走阀序列）",
          ctl.phase == ctl.PHASE_VALVES, ctl.phase)
    check("换相不中断累计运行时间", ctl.run_ms() >= before_ms,
          "%s -> %s" % (before_ms, ctl.run_ms()))
    check("换相后子阶段计时重置", ctl._cycle_stage_at > 0)


# ---------------------------------------------------------------------------
# ⑤ 启动闸（配置校验）
# ---------------------------------------------------------------------------

def test_cycle_config_gate():
    """⑤ 组合模式启动闸：循环须启用 + 参与循环的模式都配设备。"""
    stages = DEF_STAGES
    # 循环未启用 ⇒ 拒绝
    ctl = _mk(cycle=_cycle_cfg(stages=stages, enabled=False))
    err = ctl.mode_config_error(proto.AUTO_MODE_AUTO)
    check("循环未启用 ⇒ 组合模式启动被拒（说明清楚）",
          err != "" and "循环" in err, err)
    # 缺一个参与模式的设备 ⇒ 拒绝并点名（**不写死数量词**）
    modes = _modes_cfg(stages)
    modes[stages[-1]] = {"devices": []}
    ctl = _mk(cycle=_cycle_cfg(stages=stages), modes=modes)
    err = ctl.mode_config_error(proto.AUTO_MODE_AUTO)
    check("参与模式缺设备 ⇒ 拒绝并点名（文案不写死数量）",
          err != "" and "配设备" in err and "缺：" in err, err)
    # 齐了 ⇒ 可跑
    ctl = _mk(cycle=_cycle_cfg(stages=stages), modes=_modes_cfg(stages))
    check("参与模式齐 + 循环启用 ⇒ 可跑",
          ctl.mode_config_error(proto.AUTO_MODE_AUTO) == "",
          ctl.mode_config_error(proto.AUTO_MODE_AUTO))
    # [设备级校验 · fail-closed] 配了搅拌泵但变频器未装配 ⇒ 拒
    modes = _modes_cfg(stages)
    modes[stages[0]] = {"devices": ["valve1", "stirPump"]}
    ctl = _mk(cycle=_cycle_cfg(stages=stages), modes=modes)
    err = ctl.mode_config_error(proto.AUTO_MODE_AUTO)
    check("参与模式选了搅拌泵但变频器未装配 ⇒ 拒（设备级校验）",
          err != "" and "变频器" in err, err)


def test_stage_without_any_trigger_rejected():
    """⑤b 某阶段**两个触发都没开** ⇒ 启动闸拒绝并说清楚（fail-closed）。

    [为什么必须拒] 这种阶段永远等不到切换条件 ⇒ 流程卡死在那个阶段 ⇒
    属配置错误，必须在启动时就说清楚，不能"跑起来再说"。
    """
    stages = DEF_STAGES
    sc = _default_stage_cfg(stages)
    sc[stages[1]] = _stage(False, 0.5, "gt", False, 10)   # 两个都关
    ctl = _mk(cycle=_cycle_cfg(stages=stages, stage_cfg=sc),
              modes=_modes_cfg(stages))
    err = ctl.mode_config_error(proto.AUTO_MODE_AUTO)
    check("某阶段两个触发都没开 ⇒ 启动闸拒绝（文案含「切换条件」）",
          err != "" and "切换条件" in err, err)
    # 只关一个也不行（压差关 + 定时关 才是两个都关）；压差开 ⇒ 放行
    sc2 = _default_stage_cfg(stages)
    sc2[stages[1]] = _stage(True, 0.5, "gt", False, 10)
    ctl = _mk(cycle=_cycle_cfg(stages=stages, stage_cfg=sc2),
              modes=_modes_cfg(stages))
    check("只开压差的阶段 ⇒ 可跑",
          ctl.mode_config_error(proto.AUTO_MODE_AUTO) == "",
          ctl.mode_config_error(proto.AUTO_MODE_AUTO))


# ---------------------------------------------------------------------------
# ⑥ 配置化的循环（新用例）
# ---------------------------------------------------------------------------

def test_two_stage_list_closes_loop():
    """⑥a 两元素列表 ⇒ 闭环（A 到 B，B 回到 A）。"""
    stages = ["produce", "rinse"]
    sc = _default_stage_cfg(stages)
    sc["produce"] = _stage(True, 0.5, "gt", False, 30)
    sc["rinse"] = _stage(False, 0.5, "gt", True, 1)
    ctl = _mk(press=0.8, cycle=_cycle_cfg(stages=stages, stage_cfg=sc),
              modes=_modes_cfg(stages))
    # 主运行阶段压差达标 ⇒ 切到第二个
    _enter_cycle(ctl, stages[0])
    ctl._cycle_tick(ctl.ctx.now_ms())
    check("两元素列表：压差达标 ⇒ 切到 %s" % stages[1],
          ctl.mode == stages[1], ctl.mode)
    # 第二个阶段定时到期 ⇒ 回到第一个（闭环）
    _elapsed(ctl, 2)
    ctl._cycle_tick(ctl.ctx.now_ms())
    check("两元素列表：%s 到期 ⇒ 回到 %s（闭环）" % (stages[1], stages[0]),
          ctl.mode == stages[0], ctl.mode)


def test_reordered_list_follows_new_order():
    """⑥b 顺序被改后 ⇒ **严格按新顺序**推进（不再走旧的固定三段链）。"""
    stages = ["rinse", "produce", "backwash"]
    sc = _default_stage_cfg(stages)
    for mid in stages:
        sc[mid] = _stage(True, 0.5, "gt", True, 1)   # 都用定时 1 分钟
    ctl = _mk(press=0.1, cycle=_cycle_cfg(stages=stages, stage_cfg=sc),
              modes=_modes_cfg(stages))
    seq = []
    _enter_cycle(ctl, stages[0])
    for _ in range(4):
        seq.append(ctl.mode)
        _elapsed(ctl, 2)
        ctl._cycle_tick(ctl.ctx.now_ms())
    check("改序后严格按新列表推进：%s" % " → ".join(seq),
          seq[:3] == stages, " → ".join(seq))


def test_first_stage_uses_press_when_no_produce():
    """⑥c 列表**不含产水** ⇒ 首元素按**它自己的配置**走（位置驱动）。"""
    stages = ["rinse", "backwash"]
    sc = _default_stage_cfg(stages)
    sc["rinse"] = _stage(True, 0.5, "gt", False, 30)   # 首元素＝压差触发
    sc["backwash"] = _stage(False, 0.5, "gt", True, 1)
    ctl = _mk(press=0.8, cycle=_cycle_cfg(stages=stages, stage_cfg=sc),
              modes=_modes_cfg(stages))
    _enter_cycle(ctl, stages[0])
    ctl._cycle_tick(ctl.ctx.now_ms())
    check("列表不含产水：首元素[%s]按压差切到 %s"
          % (stages[0], stages[1]), ctl.mode == stages[1], ctl.mode)
    # 反过来：压差不达标 ⇒ 首元素**不切**（它的定时没开）
    ctl = _mk(press=0.1, cycle=_cycle_cfg(stages=stages, stage_cfg=sc),
              modes=_modes_cfg(stages))
    _enter_cycle(ctl, stages[0])
    _elapsed(ctl, 99)
    ctl._cycle_tick(ctl.ctx.now_ms())
    check("列表不含产水：首元素不按时长切（等压差）",
          ctl.mode == stages[0], ctl.mode)


def test_empty_stage_list_rejected():
    """⑥d 空列表 ⇒ 启动闸拒绝并说明（不让它静默跑成别的链）。

    [2026-11-26 实例化] 文案改实例口径（「未配置参与循环的**实例**」）。
    """
    ctl = _mk(cycle=_cycle_cfg(stages=[]))
    err = ctl.mode_config_error(proto.AUTO_MODE_AUTO)
    check("空列表 ⇒ 启动闸拒绝并指路远端设置",
          err != "" and "未配置实例" in err, err)
    # 只有 1 个模式 ⇒ 拒绝（1 个没有循环意义）
    ctl = _mk(cycle=_cycle_cfg(stages=["produce"]),
              modes=_modes_cfg(["produce"]))
    err = ctl.mode_config_error(proto.AUTO_MODE_AUTO)
    check("只有 1 个模式 ⇒ 启动闸拒绝（至少 2 个）",
          err != "" and "至少" in err, err)


def test_set_cycle_modes_fail_closed():
    """⑥e SET 路径 fail-closed：未知模式 / 重复模式 / 空列表 / 类型错 ⇒ 明确报错。"""
    cfg = cfgmod.Config()
    app = app_mod.App.__new__(app_mod.App)     # 只用到 cfg.auto_cycle
    app.cfg = cfg
    set_auto = app._cfg_set_auto_cycle
    base = {proto.CFG_AUTO_CYCLE_ENABLED: True}

    err = set_auto(dict(base, **{proto.CFG_AUTO_CYCLE_MODES: ["produce",
                                                             "ghost"]}))
    check("SET cycleModes 含未知模式 ⇒ 报错",
          err != "" and "未知模式" in err, err)
    err = set_auto(dict(base, **{proto.CFG_AUTO_CYCLE_MODES:
                                 ["produce", "produce"]}))
    check("SET cycleModes 有重复模式 ⇒ 报错",
          err != "" and "重复" in err, err)
    err = set_auto(dict(base, **{proto.CFG_AUTO_CYCLE_MODES: []}))
    check("SET cycleModes 空列表 ⇒ 报错",
          err != "" and "空" in err, err)
    err = set_auto(dict(base, **{proto.CFG_AUTO_CYCLE_MODES: "produce"}))
    check("SET cycleModes 类型错（字符串）⇒ 报错",
          err != "" and "数组" in err, err)
    err = set_auto(dict(base, **{proto.CFG_AUTO_CYCLE_MODES: ["produce", 7]}))
    check("SET cycleModes 元素类型错 ⇒ 报错", err != "", err)
    # stageMinutes（兼容视图）的 fail-closed
    err = set_auto(dict(base, **{proto.CFG_AUTO_CYCLE_STAGE_MINUTES:
                                 {"ghost": 10}}))
    check("SET stageMinutes 含未知模式 ⇒ 报错",
          err != "" and "未知模式" in err, err)
    err = set_auto(dict(base, **{proto.CFG_AUTO_CYCLE_STAGE_MINUTES:
                                 {"produce": "abc"}}))
    check("SET stageMinutes 分钟数非法 ⇒ 报错", err != "", err)
    err = set_auto(dict(base, **{proto.CFG_AUTO_CYCLE_STAGE_MINUTES: [1, 2]}))
    check("SET stageMinutes 类型错（数组）⇒ 报错", err != "", err)
    # 合法的写：保序 + 分钟数夹范围
    err = set_auto({proto.CFG_AUTO_CYCLE_MODES: ["rinse", "produce"],
                    proto.CFG_AUTO_CYCLE_STAGE_MINUTES:
                    {"rinse": 99999, "produce": 3}})
    check("SET 合法值 ⇒ 不报错", err == "", err)
    check("SET 合法值：列表保序",
          cfg.auto_cycle["cycle_modes"] == ["rinse", "produce"],
          "%s" % (cfg.auto_cycle["cycle_modes"],))
    check("SET 合法值：分钟数夹在允许范围内（并折进 stage_cfg）",
          cfg.auto_cycle["stage_cfg"]["rinse"]["timer_minutes"]
          == proto.AUTO_CYCLE_MINUTES_MAX
          and cfg.auto_cycle["stage_cfg"]["produce"]["timer_minutes"] == 3,
          "%s" % (cfg.auto_cycle["stage_cfg"],))


def test_set_stage_cfg_fail_closed():
    """⑥f SET stageCfg 的 fail-closed：类型错 / 未知模式 / 缺阶段配置 /
    某阶段两个触发都没开 ⇒ 一律明确报错。"""
    cfg = cfgmod.Config()
    app = app_mod.App.__new__(app_mod.App)
    app.cfg = cfg
    set_auto = app._cfg_set_auto_cycle
    stages = ["produce", "backwash"]
    good = {
        "produce": {"pressEnabled": True, "pressThreshold": 0.5,
                    "pressCompare": "gt", "timerEnabled": False,
                    "timerMinutes": 30},
        "backwash": {"pressEnabled": False, "pressThreshold": 0.5,
                     "pressCompare": "gt", "timerEnabled": True,
                     "timerMinutes": 10},
    }
    # 类型错
    err = set_auto({proto.CFG_AUTO_CYCLE_MODES: stages,
                    proto.CFG_AUTO_CYCLE_STAGE_CFG: [1, 2]})
    check("SET stageCfg 类型错（数组）⇒ 报错",
          err != "" and "对象" in err, err)
    # 未知模式
    err = set_auto({proto.CFG_AUTO_CYCLE_MODES: stages,
                    proto.CFG_AUTO_CYCLE_STAGE_CFG:
                    dict(good, ghost={"timerEnabled": True,
                                      "timerMinutes": 5})})
    check("SET stageCfg 含未知模式 ⇒ 报错",
          err != "" and "未知模式" in err, err)
    # 缺阶段配置（只给了一个参与阶段）
    err = set_auto({proto.CFG_AUTO_CYCLE_MODES: stages,
                    proto.CFG_AUTO_CYCLE_STAGE_CFG:
                    {"produce": good["produce"]}})
    check("SET stageCfg 缺参与阶段 ⇒ 报错",
          err != "" and "缺少参与循环的阶段配置" in err, err)
    # 该阶段两个触发都没开
    err = set_auto({proto.CFG_AUTO_CYCLE_MODES: stages,
                    proto.CFG_AUTO_CYCLE_STAGE_CFG:
                    {"produce": good["produce"],
                     "backwash": {"pressEnabled": False,
                                  "timerEnabled": False}}})
    check("SET stageCfg 某阶段两个触发都没开 ⇒ 报错",
          err != "" and "切换条件" in err, err)
    # 字段类型错（布尔写成字符串 / 阈值写成非数字 / 比较方向非法）
    err = set_auto({proto.CFG_AUTO_CYCLE_MODES: stages,
                    proto.CFG_AUTO_CYCLE_STAGE_CFG:
                    {"produce": dict(good["produce"], pressEnabled="yes"),
                     "backwash": good["backwash"]}})
    check("SET stageCfg 布尔字段类型错 ⇒ 报错", err != "", err)
    err = set_auto({proto.CFG_AUTO_CYCLE_MODES: stages,
                    proto.CFG_AUTO_CYCLE_STAGE_CFG:
                    {"produce": dict(good["produce"], pressThreshold="高"),
                     "backwash": good["backwash"]}})
    check("SET stageCfg 阈值非数字 ⇒ 报错", err != "", err)
    err = set_auto({proto.CFG_AUTO_CYCLE_MODES: stages,
                    proto.CFG_AUTO_CYCLE_STAGE_CFG:
                    {"produce": dict(good["produce"], pressCompare="xx"),
                     "backwash": good["backwash"]}})
    check("SET stageCfg 比较方向非法 ⇒ 报错", err != "", err)
    # 合法写入 ⇒ 落进 stage_cfg（camelCase → 内部键）
    err = set_auto({proto.CFG_AUTO_CYCLE_MODES: stages,
                    proto.CFG_AUTO_CYCLE_STAGE_CFG: good})
    check("SET stageCfg 合法值 ⇒ 不报错", err == "", err)
    sc = cfg.auto_cycle["stage_cfg"]
    check("SET stageCfg 合法值：落进 stage_cfg（逐阶段）",
          sc["produce"]["press_enabled"] is True
          and sc["produce"]["press_threshold"] == 0.5
          and sc["produce"]["timer_enabled"] is False
          and sc["backwash"]["timer_enabled"] is True
          and sc["backwash"]["timer_minutes"] == 10,
          "%s" % (sc,))
    check("SET stageCfg 后旧键同步（压差/兜底定时＝首元素的值）",
          cfg.auto_cycle["press_threshold"] == 0.5
          and cfg.auto_cycle["timer_fallback"] is False
          and cfg.auto_cycle["backwash_minutes"] == 10,
          "%s" % ({k: cfg.auto_cycle[k] for k in
                   ("press_threshold", "timer_fallback",
                    "backwash_minutes")},))


def test_set_legacy_keys_fold_into_stage_cfg():
    """⑥g 旧键 ⇒ 折进 stage_cfg（老客户端发的旧键照样生效）。"""
    cfg = cfgmod.Config()
    app = app_mod.App.__new__(app_mod.App)
    app.cfg = cfg
    set_auto = app._cfg_set_auto_cycle
    # 老客户端：压差阈值/方向/兜底定时/分钟数 + 反洗顺洗分钟
    err = set_auto({proto.CFG_AUTO_CYCLE_PRESS_THRESHOLD: 0.7,
                    proto.CFG_AUTO_CYCLE_PRESS_COMPARE: "lt",
                    proto.CFG_AUTO_CYCLE_TIMER_FALLBACK: True,
                    proto.CFG_AUTO_CYCLE_TIMER_MINUTES: 45,
                    proto.CFG_AUTO_CYCLE_BACKWASH_MINUTES: 13,
                    proto.CFG_AUTO_CYCLE_RINSE_MINUTES: 11})
    check("SET 旧键 ⇒ 不报错", err == "", err)
    sc = cfg.auto_cycle["stage_cfg"]
    check("旧键压差阈值/方向 ⇒ 落到**首元素**",
          sc["produce"]["press_threshold"] == 0.7
          and sc["produce"]["press_compare"] == "lt",
          "%s" % (sc.get("produce"),))
    check("旧键兜底定时 ⇒ 落到首元素的 timer_enabled",
          sc["produce"]["timer_enabled"] is True
          and sc["produce"]["timer_minutes"] == 45,
          "%s" % (sc.get("produce"),))
    check("旧键反洗/顺洗分钟 ⇒ 落到对应模式的 timerMinutes",
          sc["backwash"]["timer_minutes"] == 13
          and sc["rinse"]["timer_minutes"] == 11,
          "%s" % (sc,))
    check("旧键折进后，内存里的旧键与新配置一致",
          cfg.auto_cycle["press_threshold"] == 0.7
          and cfg.auto_cycle["timer_fallback"] is True
          and cfg.auto_cycle["timer_minutes"] == 45
          and cfg.auto_cycle["backwash_minutes"] == 13
          and cfg.auto_cycle["rinse_minutes"] == 11,
          "%s" % (cfg.auto_cycle,))


def test_resume_stage_not_in_new_list_falls_back():
    """⑥h 记住的阶段**不在新列表** ⇒ 启动时兜底到**首元素**（并记日志）。"""
    old = ["produce", "backwash", "rinse"]
    stages = ["produce", "backwash"]
    logs = []
    app = _FakeApp(press=0.8, cycle=_cycle_cfg(stages=old),
                   modes=_modes_cfg(old))
    ctx = autocontrol.AutoContext(app, log=lambda m: None)
    ctl = autocontrol.AutoController(ctx, log=lambda m: logs.append(m))
    # 先按**旧列表**跑到顺洗并记住
    _enter_cycle(ctl, "rinse")
    ctl.stop(ctl.ctx.now_ms(), stop_devices=True, reason="用例")
    check("停止后记住顺洗", ctl._cycle_resume_mode == "rinse",
          ctl._cycle_resume_mode)
    # 配置改成**不含顺洗**的新列表 ⇒ 再启动应从首元素开始
    app.cfg.auto_cycle = _cycle_cfg(stages=stages)
    ctl.start(ctl.ctx.now_ms(), mode=proto.AUTO_MODE_AUTO)
    check("记忆阶段不在新列表 ⇒ 从首元素[%s]开始" % stages[0],
          ctl.mode == stages[0], ctl.mode)
    check("记忆阶段不在新列表 ⇒ 记一行日志（不静默）",
          any("已不在参与循环" in str(m) for m in logs),
          "%s" % ([str(m) for m in logs],))


def test_state_extra_resume_not_in_list_reports_auto():
    """⑥i 停止态回显：记住的阶段**不在当前列表** ⇒ 回显 "auto"。

    [为什么] 界面显示的阶段必须是"下次真会跑的阶段"——链上不存在的阶段
    显示出来 ⇒ 操作员以为会从反洗继续，实际会从产水开始。
    """
    old = ["produce", "backwash", "rinse"]
    app = _FakeApp(press=0.8, cycle=_cycle_cfg(stages=old),
                   modes=_modes_cfg(old))
    ctx = autocontrol.AutoContext(app, log=lambda m: None)
    ctl = autocontrol.AutoController(ctx, log=lambda m: None)
    _enter_cycle(ctl, "rinse")
    ctl.stop(ctl.ctx.now_ms(), stop_devices=True, reason="用例")
    check("停止后记住顺洗", ctl._cycle_resume_mode == "rinse",
          ctl._cycle_resume_mode)
    biz = app_mod.App.__new__(app_mod.App)
    biz.auto_controller = ctl
    # ① 列表里还有顺洗 ⇒ 回显顺洗（既有口径不变）
    extra = biz._auto_state_extra()
    check("停止态回显记住的阶段（顺洗）",
          extra.get(proto.KEY_AUTO_MODE) == "rinse", "%r" % extra)
    # ② 配置改成不含顺洗 ⇒ 回显 "auto"（不显示链上不存在的阶段）
    app.cfg.auto_cycle = _cycle_cfg(stages=["produce", "backwash"])
    extra = biz._auto_state_extra()
    check("记忆阶段不在列表 ⇒ 回显 auto（不是链外阶段）",
          extra.get(proto.KEY_AUTO_MODE) == proto.AUTO_MODE_AUTO,
          "%r" % extra)


def test_cycle_cfg_persist_round_trip():
    """⑥j cycle_modes / stage_cfg 落盘往返**保序**（且旧键也写）。"""
    import shutil as _shutil
    import tempfile
    tmp = tempfile.mkdtemp(prefix="hsf_cyclecfg_")
    try:
        cfg = cfgmod.Config()
        cfg.config_path = os.path.join(tmp, "x.json")
        cfg.users_path = os.path.join(tmp, "u.json")
        cfg.load()
        cfg.auto_cycle["cycle_modes"] = ["rinse", "produce", "drain",
                                         "backwash"]
        cfg.auto_cycle["stage_cfg"] = {
            "rinse": _stage(True, 0.3, "lt", False, 7),
            "produce": _stage(False, 0.5, "gt", True, 3),
            "drain": _stage(False, 0.5, "gt", True, 5),
            "backwash": _stage(False, 0.5, "gt", True, 11),
        }
        cfg._sync_auto_cycle_legacy()
        cfg.save()
        cfg2 = cfgmod.Config()
        cfg2.config_path = os.path.join(tmp, "x.json")
        cfg2.users_path = os.path.join(tmp, "u.json")
        cfg2.load()
        c = cfg2.auto_cycle or {}
        check("cycle_modes 落盘往返保序",
              list(c.get("cycle_modes") or [])
              == ["rinse", "produce", "drain", "backwash"],
              "%s" % (c.get("cycle_modes"),))
        sc = c.get("stage_cfg") or {}
        check("stage_cfg 落盘往返（逐阶段的压差/定时条件）",
              sc.get("rinse", {}).get("press_threshold") == 0.3
              and sc.get("rinse", {}).get("press_compare") == "lt"
              and sc.get("rinse", {}).get("press_enabled") is True
              and sc.get("produce", {}).get("timer_enabled") is True
              and sc.get("produce", {}).get("timer_minutes") == 3
              and sc.get("drain", {}).get("timer_minutes") == 5,
              "%s" % (sc,))
        check("旧键也写（老读方不受影响）：压差/兜底定时＝首元素的值",
              c.get("press_threshold") == 0.3
              and c.get("press_compare") == "lt"
              and c.get("timer_fallback") is False
              and c.get("timer_minutes") == 7,
              "%s" % ({k: c.get(k) for k in
                       ("press_threshold", "press_compare",
                        "timer_fallback", "timer_minutes")},))
        check("旧键也写：反洗/顺洗分钟＝对应模式的值",
              c.get("backwash_minutes") == 11
              and c.get("rinse_minutes") == 7,
              "backwash=%s rinse=%s" % (c.get("backwash_minutes"),
                                        c.get("rinse_minutes")))
        check("兼容视图 stageMinutes 也写（已上线的旧客户端靠它显示时长）",
              _file_auto_cycle(tmp).get("stage_minutes", {}).get("drain") == 5,
              "%s" % (_file_auto_cycle(tmp).get("stage_minutes"),))
    finally:
        _shutil.rmtree(tmp, ignore_errors=True)


def test_legacy_config_synthesizes_stage_cfg():
    """⑥k 老配置（只有旧键）⇒ 按**原语义**合成 stage_cfg（向后兼容）。

    [为什么要这条] 现场已有的配置文件里只有 pressThreshold/timerFallback/
    backwashMinutes 这些旧键 ⇒ 升级后必须**一字不改**也能按原来的工艺跑
    （产水按压差切、反洗/顺洗各 10 分钟切）。
    """
    cfg = cfgmod.Config()
    cfg._load_auto_cycle({"press_threshold": 0.4, "timer_fallback": True,
                           "timer_minutes": 20, "backwash_minutes": 13,
                           "rinse_minutes": 11})
    sc = cfg.auto_cycle.get("stage_cfg") or {}
    check("老配置合成：产水（首元素）按压差切 + 兜底定时开",
          sc.get("produce", {}).get("press_enabled") is True
          and sc.get("produce", {}).get("press_threshold") == 0.4
          and sc.get("produce", {}).get("timer_enabled") is True
          and sc.get("produce", {}).get("timer_minutes") == 20,
          "%s" % (sc.get("produce"),))
    check("老配置合成：反洗/顺洗按各自的分钟数切",
          sc.get("backwash", {}).get("press_enabled") is False
          and sc.get("backwash", {}).get("timer_enabled") is True
          and sc.get("backwash", {}).get("timer_minutes") == 13
          and sc.get("rinse", {}).get("timer_minutes") == 11,
          "%s" % (sc,))
    # 缺省：老配置没写 ⇒ 回缺省（产水压差 0.5/关定时；反洗顺洗 10 分钟）
    cfg2 = cfgmod.Config()
    sc2 = cfg2.auto_cycle.get("stage_cfg") or {}
    check("缺省合成：产水压差 0.5 + 定时关",
          sc2.get("produce", {}).get("press_threshold") == 0.5
          and sc2.get("produce", {}).get("timer_enabled") is False
          and sc2.get("produce", {}).get("timer_minutes") == 30,
          "%s" % (sc2.get("produce"),))
    check("缺省合成：反洗/顺洗 10 分钟定时",
          sc2.get("backwash", {}).get("timer_minutes") == 10
          and sc2.get("rinse", {}).get("timer_minutes") == 10,
          "%s" % (sc2,))
    # 手改配置写坏 ⇒ 回缺省，不让服务起不来
    cfg3 = cfgmod.Config()
    cfg3._load_auto_cycle({"cycle_modes": "坏数据", "stage_cfg": "坏数据"})
    check("手改配置写坏 ⇒ 宽松回缺省（服务起得来）",
          list(cfg3.auto_cycle["cycle_modes"]) == list(DEF_STAGES)
          and (cfg3.auto_cycle["stage_cfg"].get("backwash") or {}
               ).get("timer_minutes")
          == proto.AUTO_CYCLE_BACKWASH_MINUTES_DEF,
          "%s / %s" % (cfg3.auto_cycle["cycle_modes"],
                       cfg3.auto_cycle["stage_cfg"]))


def test_default_config_matches_spec():
    """⑥l **缺省配置与产品规格逐字一致**（老配置零改动仍按原样跑）。

    [为什么锁这条] 本次把"写死的三段链"换成"可配置"⇒ 必须证明**缺省
    状态下行为与改动前完全一致**（产水按压差 0.5/大于切；反洗、顺洗各
    10 分钟切；参与顺序＝产水→反洗→顺洗）。
    """
    cfg = cfgmod.Config()
    c = cfg.auto_cycle
    check("缺省参与循环的模式列表＝产水→反洗→顺洗",
          list(c["cycle_modes"]) == ["produce", "backwash", "rinse"],
          "%s" % (c["cycle_modes"],))
    sc = c["stage_cfg"]
    check("缺省 produce：压差开 + 0.5 MPa + 大于 + 定时关（30 分钟）",
          sc["produce"]["press_enabled"] is True
          and sc["produce"]["press_threshold"] == 0.5
          and sc["produce"]["press_compare"] == "gt"
          and sc["produce"]["timer_enabled"] is False
          and sc["produce"]["timer_minutes"] == 30,
          "%s" % (sc.get("produce"),))
    check("缺省 backwash：压差关 + 定时开（10 分钟）",
          sc["backwash"]["press_enabled"] is False
          and sc["backwash"]["timer_enabled"] is True
          and sc["backwash"]["timer_minutes"] == 10,
          "%s" % (sc.get("backwash"),))
    check("缺省 rinse：压差关 + 定时开（10 分钟）",
          sc["rinse"]["press_enabled"] is False
          and sc["rinse"]["timer_enabled"] is True
          and sc["rinse"]["timer_minutes"] == 10,
          "%s" % (sc.get("rinse"),))
    check("缺省 drain（若被选进列表）：压差关 + 定时开（10 分钟）",
          sc["drain"]["press_enabled"] is False
          and sc["drain"]["timer_enabled"] is True
          and sc["drain"]["timer_minutes"] == 10,
          "%s" % (sc.get("drain"),))
    check("缺省旧键与新配置一致（压差 0.5 / gt / 兜底关 / 30 / 10 / 10）",
          c["press_threshold"] == 0.5 and c["press_compare"] == "gt"
          and c["timer_fallback"] is False and c["timer_minutes"] == 30
          and c["backwash_minutes"] == 10 and c["rinse_minutes"] == 10,
          "%s" % (c,))
    # 缺省行为：产水按压差切、反洗按 10 分钟切（与改动前一致）
    stages = DEF_STAGES
    ctl = _mk(press=0.8, cycle=_cycle_cfg(stages=stages))
    _enter_cycle(ctl, stages[0])
    ctl._cycle_tick(ctl.ctx.now_ms())
    check("缺省行为：产水压差达标 ⇒ 切到反洗",
          ctl.mode == "backwash", ctl.mode)
    ctl = _mk(press=0.8, cycle=_cycle_cfg(stages=stages))
    _enter_cycle(ctl, "backwash")
    _elapsed(ctl, 10)
    ctl._cycle_tick(ctl.ctx.now_ms())
    check("缺省行为：反洗 10 分钟到期 ⇒ 切到顺洗",
          ctl.mode == "rinse", ctl.mode)
    ctl = _mk(press=0.8, cycle=_cycle_cfg(stages=stages))
    _enter_cycle(ctl, "rinse")
    _elapsed(ctl, 10)
    ctl._cycle_tick(ctl.ctx.now_ms())
    check("缺省行为：顺洗 10 分钟到期 ⇒ 回到产水（闭环）",
          ctl.mode == "produce", ctl.mode)


def test_get_doc_contains_stage_cfg():
    """⑥m GET 回显：总是带 cycleModes + stageCfg（老客户端忽略未知键即可）。"""
    cfg = cfgmod.Config()
    app = app_mod.App.__new__(app_mod.App)
    app.cfg = cfg
    doc = app._config_auto_doc()
    cycle = doc.get(proto.CFG_AUTO_CYCLE) or {}
    check("GET 回显带 cycleModes",
          list(cycle.get(proto.CFG_AUTO_CYCLE_MODES) or [])
          == ["produce", "backwash", "rinse"],
          "%s" % (cycle.get(proto.CFG_AUTO_CYCLE_MODES),))
    sc = cycle.get(proto.CFG_AUTO_CYCLE_STAGE_CFG) or {}
    check("GET 回显带 stageCfg（camelCase 逐阶段）",
          sc.get("produce", {}).get("pressEnabled") is True
          and sc.get("produce", {}).get("pressThreshold") == 0.5
          and sc.get("produce", {}).get("pressCompare") == "gt"
          and sc.get("produce", {}).get("timerEnabled") is False
          and sc.get("backwash", {}).get("timerEnabled") is True
          and sc.get("backwash", {}).get("timerMinutes") == 10,
          "%s" % (sc,))
    check("GET 回显带兼容视图 stageMinutes（已上线的旧客户端靠它）",
          (cycle.get(proto.CFG_AUTO_CYCLE_STAGE_MINUTES) or {})
          .get("backwash") == 10,
          "%s" % (cycle.get(proto.CFG_AUTO_CYCLE_STAGE_MINUTES),))


def test_defensive_no_crash_on_broken_config():
    """⑥n 防御：配置写坏（空列表 / 当前阶段不在列表）⇒ **绝不崩**。

    [为什么] 换相判定在事件循环里跑 ⇒ 一次 IndexError/ValueError 就能把
    整个 tick 打死（界面全停）。宁可不动也不要崩。
    """
    stages = DEF_STAGES
    # ① 空列表
    ctl = _mk(press=0.8, cycle=_cycle_cfg(stages=[]))
    _enter_cycle(ctl, stages[0])
    try:
        ctl._cycle_tick(ctl.ctx.now_ms())
        ok = True
    except Exception as exc:                        # noqa: BLE001
        ok = False
        print("     异常：%r" % (exc,))
    check("空参与列表 ⇒ 换相判定不抛异常", ok, "")
    # ② 当前阶段不在列表里（配置被改过）
    ctl = _mk(press=0.8, cycle=_cycle_cfg(stages=stages))
    _enter_cycle(ctl, "drain")
    try:
        ctl._cycle_tick(ctl.ctx.now_ms())
        ok = True
    except Exception as exc:                        # noqa: BLE001
        ok = False
        print("     异常：%r" % (exc,))
    check("当前阶段不在参与列表 ⇒ 换相判定不抛异常", ok, "")
    check("当前阶段不在参与列表 ⇒ 表盘也不抛异常",
          ctl.stage_timing() == (-1, 0), "%s" % (ctl.stage_timing(),))


# ---------------------------------------------------------------------------
# 既有回归（保持）
# ---------------------------------------------------------------------------

def test_cycle_resume_after_stop():
    """停止/急停后**从记住的阶段继续**，按复位才从头（用户口径 2026-11-12）。"""
    stages = DEF_STAGES
    # ① 跑到反洗阶段 → 停止 → 记忆 = 反洗
    ctl = _mk(press=0.8, cycle=_cycle_cfg(stages=stages))
    _enter_cycle(ctl, stages[1])
    ctl.stop(ctl.ctx.now_ms(), stop_devices=True, reason="用例")
    check("停止后记住当前子阶段（%s）" % stages[1],
          ctl._cycle_resume_mode == stages[1],
          ctl._cycle_resume_mode)
    # ② 再启动组合模式 ⇒ 从反洗继续（不是第一个）
    #    [先 stop 再 start] start() 在 _running 时直接返回 —— 真实流程是
    #    "停止 → 启动"，这里保持同样的顺序。
    ctl.start(ctl.ctx.now_ms(), mode=proto.AUTO_MODE_AUTO)
    check("再启动组合模式 ⇒ 从记住的阶段继续（%s）" % stages[1],
          ctl.mode == stages[1], ctl.mode)
    # ③ 按复位 ⇒ 记忆清空 ⇒ 下次从头（首元素）
    ctl.stop(ctl.ctx.now_ms(), reason="用例2")
    ctl.reset()
    check("复位 ⇒ 循环阶段记忆清空",
          ctl._cycle_resume_mode == "", ctl._cycle_resume_mode)
    ctl.start(ctl.ctx.now_ms(), mode=proto.AUTO_MODE_AUTO)
    check("复位后再启动 ⇒ 从首元素[%s]开始" % stages[0],
          ctl.mode == stages[0], ctl.mode)
    # ④ 急停同样记住
    ctl2 = _mk(press=0.8, cycle=_cycle_cfg(stages=stages))
    _enter_cycle(ctl2, stages[-1])
    ctl2.e_stop("用例急停")
    check("急停后也记住当前子阶段（%s）" % stages[-1],
          ctl2._cycle_resume_mode == stages[-1],
          ctl2._cycle_resume_mode)


def test_sticky_mode_persisted_across_restart():
    """粘性自动模式**持久化**：选模式（op=5 的执行体=赋值+persist）⇒
    新控制器实例（=服务端重启）从持久化恢复——物理启动按钮用「上次选中
    的」（粘性），粘性清空即现场按启动被拒「未选择自动模式」（2026-11-13
    板端实锅：服务端重启后物理键全废，界面下拉显示只是客户端本地缺省）。"""
    import tempfile
    import shutil as _shutil
    cfg = cfgmod.Config()
    tmp = tempfile.mkdtemp(prefix="hsf_stickymode_")
    cfg.config_path = os.path.join(tmp, "x.json")
    cfg.users_path = os.path.join(tmp, "u.json")
    cfg.load()
    app = _FakeApp2(cfg)
    ctl = autocontrol.AutoController(autocontrol.AutoContext(
        app, log=lambda m: None), log=lambda m: None)
    check("新控制器：粘性初始为空（从未选过）", ctl.mode == "",
          repr(ctl.mode))
    # 远程选模式（=op=5 SELECT 的执行体：mode 赋值 + persist）。
    ctl.mode = "backwash"
    ctl.ctx.persist_mode(ctl.mode)
    check("选模式后持久化（runtime 节）",
          cfg._runtime.get("auto_mode") == "backwash",
          repr(cfg._runtime.get("auto_mode")))
    ctl2 = autocontrol.AutoController(autocontrol.AutoContext(
        app, log=lambda m: None), log=lambda m: None)
    check("重启后从持久化恢复（物理启动键可用）", ctl2.mode == "backwash",
          repr(ctl2.mode))
    # 恢复的粘性供**现场启动按钮路径**（start 空 mode=沿用粘性）。
    ok = ctl2.start(ctl2.ctx.now_ms())
    check("现场按钮路径（空 mode 启动）用恢复的粘性成功",
          ok and ctl2._running and ctl2.mode == "backwash",
          "ok=%s mode=%s" % (ok, ctl2.mode))
    ctl2.stop(ctl2.ctx.now_ms(), stop_devices=False)
    _shutil.rmtree(tmp, ignore_errors=True)


def test_run_time_persisted_across_restart():
    """累计自动运行时长**持久化**：结算落盘 ⇒ 新控制器实例（=服务端重启）
    从持久化值恢复（2026-11-12 用户定案「这个时间要服务端持久化」）。"""
    import tempfile
    import shutil as _shutil
    cfg = cfgmod.Config()
    tmp = tempfile.mkdtemp(prefix="hsf_runms_")
    cfg.config_path = os.path.join(tmp, "x.json")
    cfg.users_path = os.path.join(tmp, "u.json")
    cfg.load()
    app = _FakeApp2(cfg)
    ctx = autocontrol.AutoContext(app, log=lambda m: None)
    ctl = autocontrol.AutoController(ctx, log=lambda m: None)
    check("新控制器：累计从持久化值恢复（初始 0）", ctl.run_ms() == 0,
          "%s" % ctl.run_ms())
    ctl._running = True
    ctl._run_start_ms = ctl.ctx.now_ms() - 5000
    ctl._settle_run_time()
    check("结算后持久化（runtime 节）",
          int(cfg._runtime.get("auto_run_ms", 0)) >= 5000,
          "%s" % cfg._runtime.get("auto_run_ms"))
    ctl2 = autocontrol.AutoController(autocontrol.AutoContext(
        app, log=lambda m: None), log=lambda m: None)
    check("重启后从持久化恢复（不归零）", ctl2.run_ms() >= 5000,
          "%s" % ctl2.run_ms())
    _shutil.rmtree(tmp, ignore_errors=True)


class _FakeApp2(object):
    """带**真实 Config** 的最小 app（持久化测试用）。"""

    def __init__(self, cfg):
        self.digital = []
        self.valve_bank = None
        self.analog = None
        self.vfd = None
        self.temphum = None
        self.cfg = cfg


# ---------------------------------------------------------------------------
# ⑦ 实例化族（2026-11-26：auto.cycle = enabled + instances 有序实例列表）
# ---------------------------------------------------------------------------

def _inst(mode, devices=(), press=False, press_thr=0.5, press_cmp="gt",
          press1=False, press1_thr=1.0, press1_cmp="gt",
          press2=False, press2_thr=1.0, press2_cmp="gt",
          timer=False, timer_minutes=10, rpm=None, rev=None, accel=None,
          reset_stop=None, reset_estop=None, stir_sw=None, inlet_sw=None):
    """一个模式实例（协议面 camelCase 键；SET 载荷/文件规整都认）。"""
    inst = {"mode": mode, "devices": list(devices),
            "pressEnabled": bool(press), "pressThreshold": float(press_thr),
            "pressCompare": press_cmp,
            "press1Enabled": bool(press1),
            "press1Threshold": float(press1_thr),
            "press1Compare": press1_cmp,
            "press2Enabled": bool(press2),
            "press2Threshold": float(press2_thr),
            "press2Compare": press2_cmp,
            "timerEnabled": bool(timer),
            "timerMinutes": int(timer_minutes)}
    if rpm is not None:
        inst["stirRpm"] = int(rpm)
    if rev is not None:
        inst["stirReverse"] = bool(rev)
    if accel is not None:
        inst["stirAccelRpmS"] = int(accel)
    if reset_stop is not None:
        inst["resetValvesOnStop"] = bool(reset_stop)
    if reset_estop is not None:
        inst["resetValvesOnEstop"] = bool(reset_estop)
    if stir_sw is not None:
        inst["stirSwitchPolicy"] = stir_sw
    if inlet_sw is not None:
        inst["inletSwitchPolicy"] = inlet_sw
    return inst


def _enter_cycle_inst(ctl, idx):
    """把控制器放到「组合模式 + 指定实例下标 + running」（实例口径）。"""
    ctl.mode = proto.AUTO_MODE_AUTO
    ctl._cycle_root = proto.AUTO_MODE_AUTO
    ctl._cycle_cfg = ctl._cycle_config()
    ctl._cycle_stage_idx = int(idx)
    ctl.mode = str(ctl._cycle_stages()[idx].get("mode"))
    now = ctl.ctx.now_ms()
    ctl._cycle_stage_at = now
    ctl._run_start_ms = now
    ctl._running = True
    ctl.phase = ctl.PHASE_RUNNING


def _cycle_inst_cfg(instances, **kw):
    """造一份**显式 instances** 的 auto_cycle 配置。"""
    cfg = _cycle_cfg()
    cfg["instances"] = proto.normalize_cycle_instances(list(instances))
    cfg["instances_explicit"] = True
    cfg.update(kw)
    return cfg


def _modes_full(stages):
    """给每个模式配一台阀（旧键兜底合成实例时设备的来源）。"""
    return _modes_cfg(stages)


def _new_app(set_auto):
    """fresh Config + 裸 App（只用到 cfg.auto_cycle / _cfg_set_auto_cycle）。"""
    cfg = cfgmod.Config()
    app = app_mod.App.__new__(app_mod.App)
    app.cfg = cfg
    return cfg, app


def test_set_instances_fail_closed():
    """⑦a SET instances 的 fail-closed 逐键（wire ①-⑦）。"""
    cfg, app = _new_app(True)
    set_auto = app._cfg_set_auto_cycle
    key = proto.CFG_AUTO_CYCLE_INSTANCES
    good2 = [_inst("produce", ["valve1"], timer=True, timer_minutes=5),
             _inst("rinse", ["valve3"], timer=True, timer_minutes=5)]
    # ① 类型错 / 元素非对象
    err = set_auto({key: "produce"})
    check("instances 非数组 ⇒ 拒「需要数组」", "需要数组" in err, err)
    err = set_auto({key: ["produce", "rinse"]})
    check("instances 元素非对象 ⇒ 拒「需要对象」", "需要对象" in err, err)
    # ② mode 未知/缺失 ⇒ 拒
    err = set_auto({key: [good2[0], _inst("ghost", timer=True)]})
    check("instances 含未知 mode ⇒ 拒（点名 mode 键）",
          err != "" and proto.CYCLE_INST_MODE in err, err)
    err = set_auto({key: [{"devices": ["valve1"], "timerEnabled": True},
                          good2[1]]})
    check("instances 缺 mode ⇒ 拒", err != "" and "mode" in err, err)
    # ③ 同 mode 可重复（不拒——本口径的核心）。[放行会置显式标志]
    # ⇒ 本用例里所有**拒**的断言排在它前面（下面 ④ 起才是拒），最后统一
    # 用独立 cfg 复验"被拒不改配置"。
    err = set_auto({key: [_inst("produce", ["valve1"], timer=True),
                          _inst("produce", ["valve2"], timer=True,
                                timer_minutes=3)]})
    check("同 mode 重复出现 ⇒ 放行（instance 化核心）", err == "", err)
    cfg_reject = app_mod.App.__new__(app_mod.App)
    cfg_reject.cfg = cfgmod.Config()
    set_reject = cfg_reject._cfg_set_auto_cycle
    # ④ len<2 ⇒ 拒（沿用旧文案口径）
    err = set_auto({key: [good2[0]]})
    check("单实例 ⇒ 拒（至少 2 个）", err != "" and "至少" in err, err)
    err = set_auto({key: []})
    check("空实例列表 ⇒ 拒（不能为空）", err != "" and "不能为空" in err, err)
    # ⑤ MAX 闸
    many = [_inst("produce" if i % 2 == 0 else "rinse", ["valve1"],
                  timer=True) for i in range(proto.AUTO_CYCLE_INSTANCES_MAX + 1)]
    err = set_auto({key: many})
    check("超上限 ⇒ 拒（点名上限 %d）" % proto.AUTO_CYCLE_INSTANCES_MAX,
          err != "" and str(proto.AUTO_CYCLE_INSTANCES_MAX) in err, err)
    # ⑥ 四触发全关 ⇒ 整包拒（文案带实例序号「产水模式2」）
    err = set_auto({key: [good2[0],
                          _inst("produce", ["valve2"],
                                press=False, timer=False)]})
    check("第二实例四触发全关 ⇒ 拒（文案带「产水模式2」）",
          err != "" and "产水模式2" in err and "切换条件" in err, err)
    # 逐键 fail-closed：布尔/数字/枚举/设备清单
    base = [_inst("produce", ["valve1"], timer=True), good2[1]]
    err = set_auto({key: [dict(base[0], pressEnabled="false"), good2[1]]})
    check("pressEnabled 字符串 ⇒ 拒（strict_bool）",
          err != "" and "布尔" in err, err)
    err = set_auto({key: [dict(base[0], pressThreshold="高"), good2[1]]})
    check("pressThreshold 非数字 ⇒ 拒", err != "" and "数字" in err, err)
    err = set_auto({key: [dict(base[0], press1Threshold=True), good2[1]]})
    check("press1Threshold 布尔 ⇒ 拒（不是合法数字）", err != "", err)
    err = set_auto({key: [dict(base[0], pressCompare="xx"), good2[1]]})
    check("pressCompare 非法 ⇒ 拒", err != "" and "pressCompare" in err, err)
    err = set_auto({key: [dict(base[0], press1Compare="ge"), good2[1]]})
    check("press1Compare 非法 ⇒ 拒", err != "" and "press1Compare" in err,
          err)
    err = set_auto({key: [dict(base[0], timerMinutes="abc"), good2[1]]})
    check("timerMinutes 非整数 ⇒ 拒", err != "", err)
    err = set_auto({key: [dict(base[0], stirRpm="abc"), good2[1]]})
    check("stirRpm 非数字 ⇒ 拒", err != "", err)
    err = set_auto({key: [dict(base[0], stirSwitchPolicy="nope"), good2[1]]})
    check("stirSwitchPolicy 非法 ⇒ 拒（fail-closed）",
          err != "" and "stirSwitchPolicy" in err, err)
    err = set_auto({key: [dict(base[0], devices=["ghost"]), good2[1]]})
    check("devices 全未知 ⇒ 拒（有内容但全不认识）",
          err != "" and "设备" in err, err)
    # 非法值写入被拒后配置不被改坏（fail-closed；调用方另有快照回滚）。
    # [用独立 cfg] 前面 ③ 的合法包已把 `cfg` 置显式 ⇒ 这里开一份新的验。
    err = set_reject({key: [good2[0],
                            _inst("produce", ["valve2"],
                                  press=False, timer=False)]})
    check("（独立 cfg）四触发全关 ⇒ 拒", err != "" and "切换条件" in err, err)
    check("非法包被拒后 instances 未被写坏（仍未显式）",
          cfg_reject.cfg.auto_cycle.get("instances_explicit") is False,
          "%s" % (cfg_reject.cfg.auto_cycle.get("instances_explicit"),))
    # 合法包：放行 + 置显式标志 + 同包旧键忽略
    err = set_auto({key: good2,
                    proto.CFG_AUTO_CYCLE_MODES: "垃圾也应忽略",
                    proto.CFG_AUTO_CYCLE_STAGE_CFG: {"produce": {}}})
    check("合法 instances 包（带旧键垃圾）⇒ 不报错（旧键忽略）",
          err == "", err)
    check("合法包置显式标志",
          cfg.auto_cycle.get("instances_explicit") is True,
          "%s" % (cfg.auto_cycle.get("instances_explicit"),))
    check("合法包：实例按序落进 cfg",
          [i["mode"] for i in cfg.auto_cycle["instances"]]
          == ["produce", "rinse"],
          "%s" % ([i["mode"] for i in cfg.auto_cycle["instances"]],))


def test_set_instances_defaults_and_clamps():
    """⑦b 实例逐键缺省与钳位（只写部分键 ⇒ 其余回缺省；越界钳位）。"""
    cfg, app = _new_app(True)
    set_auto = app._cfg_set_auto_cycle
    key = proto.CFG_AUTO_CYCLE_INSTANCES
    err = set_auto({key: [
        {"mode": "produce", "devices": ["valve1"], "timerEnabled": True,
         "timerMinutes": 99999, "pressThreshold": 99.0,
         "press1Threshold": 2.5, "stirRpm": 9999, "stirAccelRpmS": -5},
        _inst("rinse", ["valve3"], timer=True)]})
    check("只写部分键的实例 ⇒ 放行", err == "", err)
    p = cfg.auto_cycle["instances"][0]
    check("timerMinutes 越界钳到 1440", p["timer_minutes"] == 1440,
          "%s" % p["timer_minutes"])
    check("pressThreshold 越界钳到 10.0", p["press_threshold"] == 10.0,
          "%s" % p["press_threshold"])
    check("press1Threshold 缺省 1.0、显式 2.5 收敛",
          p["press1_threshold"] == 2.5,
          "%s" % p["press1_threshold"])
    check("未写的 press2Enabled 回缺省 False", p["press2_enabled"] is False,
          "%s" % p["press2_enabled"])
    check("stirRpm 钳到 1440", p["stir_rpm"] == 1440, "%s" % p["stir_rpm"])
    check("stirAccelRpmS 负值钳到 0", p["stir_accel_rpm_s"] == 0,
          "%s" % p["stir_accel_rpm_s"])
    check("复位两开关缺省（停止 True / 急停 False）",
          p["reset_valves_on_stop"] is True
          and p["reset_valves_on_estop"] is False,
          "%s/%s" % (p["reset_valves_on_stop"], p["reset_valves_on_estop"]))
    check("换相策略缺省 stopRestart",
          p["stir_switch_policy"] == proto.AUTO_SWITCH_POLICY_DEF
          and p["inlet_switch_policy"] == proto.AUTO_SWITCH_POLICY_DEF,
          "%s" % p["stir_switch_policy"])


def test_migration_boundary():
    """⑦c 迁移边界：手写 instances:[]/len<2 ⇒ 未显式（合成可跑）；
    显式后旧键降级派生（手改文件旧键不再直接生效）。"""
    import shutil as _shutil
    import tempfile
    tmp = tempfile.mkdtemp(prefix="hsf_instmig_")
    try:
        # 老配置（只有旧键）→ load → save（未显式，无 instances 键）→ 重启
        # ⇒ 行为不变（往返锁）。
        cfg = cfgmod.Config()
        cfg.config_path = os.path.join(tmp, "x.json")
        cfg.users_path = os.path.join(tmp, "u.json")
        with open(cfg.config_path, "w", encoding="utf-8") as fh:
            import json as _json
            _json.dump({proto.CFG_SECTION_AUTO: {proto.CFG_AUTO_CYCLE: {
                "enabled": True, "press_threshold": 0.6,
                "press_compare": "lt", "timer_fallback": True,
                "timer_minutes": 20, "backwash_minutes": 13,
                "rinse_minutes": 11,
                "cycle_modes": ["produce", "backwash", "rinse"]}}},
                fh, ensure_ascii=False)
        check("老配置 load 成功", cfg.load(), "")
        c = cfg.auto_cycle
        check("老配置合成实例（产水=压差切、反洗/顺洗=定时）",
              [i["mode"] for i in c["instances"]]
              == ["produce", "backwash", "rinse"]
              and c["instances"][0]["press_enabled"] is True
              and abs(c["instances"][0]["press_threshold"] - 0.6) < 1e-9
              and c["instances"][1]["timer_minutes"] == 13
              and c["instances"][2]["timer_minutes"] == 11,
              "%s" % ([(i["mode"], i["press_enabled"], i["timer_minutes"])
                       for i in c["instances"]],))
        check("老配置 load 后**未置显式**", c["instances_explicit"] is False,
              "%s" % c["instances_explicit"])
        cfg.save()
        with open(cfg.config_path, encoding="utf-8") as fh:
            import json as _json
            doc = _json.load(fh)
        cyc = doc[proto.CFG_SECTION_AUTO][proto.CFG_AUTO_CYCLE]
        check("未显式 save **不写** instances 键",
              proto.CFG_AUTO_CYCLE_INSTANCES not in cyc,
              "%s" % sorted(cyc.keys()))
        cfg2 = cfgmod.Config()
        cfg2.config_path = cfg.config_path
        cfg2.users_path = cfg.users_path
        cfg2.load()
        c2 = cfg2.auto_cycle
        check("老配置→load→save→重启：行为不变（合成实例逐值一致）",
              [i["mode"] for i in c2["instances"]]
              == ["produce", "backwash", "rinse"]
              and abs(c2["instances"][0]["press_threshold"] - 0.6) < 1e-9
              and c2["timer_fallback"] is True
              and c2["timer_minutes"] == 20
              and c2["backwash_minutes"] == 13,
              "%s" % (c2["instances"],))
        # 手写 instances:[] / len<2 ⇒ 未显式（不拒、按旧键继续）
        with open(cfg.config_path, encoding="utf-8") as fh:
            doc = _json.load(fh)
        doc[proto.CFG_SECTION_AUTO][proto.CFG_AUTO_CYCLE][
            proto.CFG_AUTO_CYCLE_INSTANCES] = []
        with open(cfg.config_path, "w", encoding="utf-8") as fh:
            _json.dump(doc, fh, ensure_ascii=False)
        cfg3 = cfgmod.Config()
        cfg3.config_path = cfg.config_path
        cfg3.users_path = cfg.users_path
        check("手写 instances:[] ⇒ load 不拒", cfg3.load(), "")
        check("手写 instances:[] ⇒ 未显式（按旧键合成可跑）",
              cfg3.auto_cycle["instances_explicit"] is False
              and len(cfg3.auto_cycle["instances"]) == 3,
              "%s" % (cfg3.auto_cycle["instances_explicit"],))
        # 显式落盘后旧键降级：手改文件里的旧键不再直接生效
        app = app_mod.App.__new__(app_mod.App)
        app.cfg = cfg3
        err = app._cfg_set_auto_cycle({proto.CFG_AUTO_CYCLE_INSTANCES: [
            _inst("produce", ["valve1"], timer=True),
            _inst("rinse", ["valve3"], timer=True)]})
        check("SET instances 成功（置显式）", err == "", err)
        cfg3.save()
        with open(cfg.config_path, encoding="utf-8") as fh:
            doc = _json.load(fh)
        cyc = doc[proto.CFG_SECTION_AUTO][proto.CFG_AUTO_CYCLE]
        check("显式 save 写出 instances 键",
              proto.CFG_AUTO_CYCLE_INSTANCES in cyc,
              "%s" % sorted(cyc.keys()))
        # 手改文件旧键（改阈值 0.9）→ 重启 ⇒ 旧键被忽略（instances 为准）
        cyc["press_threshold"] = 0.9
        cyc["backwash_minutes"] = 42
        with open(cfg.config_path, "w", encoding="utf-8") as fh:
            _json.dump(doc, fh, ensure_ascii=False)
        cfg4 = cfgmod.Config()
        cfg4.config_path = cfg.config_path
        cfg4.users_path = cfg.users_path
        cfg4.load()
        check("显式后手改文件旧键不再直接生效（instances 为唯一事实源）",
              cfg4.auto_cycle["instances_explicit"] is True
              and abs(cfg4.auto_cycle["instances"][0]["press_threshold"]
                      - 0.5) < 1e-9
              and abs(cfg4.auto_cycle["press_threshold"] - 0.5) < 1e-9
              and cfg4.auto_cycle["backwash_minutes"]
              == proto.AUTO_CYCLE_BACKWASH_MINUTES_DEF,
              "press=%s backwash=%s" % (cfg4.auto_cycle["press_threshold"],
                                        cfg4.auto_cycle["backwash_minutes"]))
    finally:
        _shutil.rmtree(tmp, ignore_errors=True)


def test_legacy_write_through():
    """⑦d 旧键写穿：显式 instances 下 SET 旧键 ⇒ 写进对应实例（不重合成）；
    逐实例 press1/press2/策略/转速不被抹掉；cycleModes 忽略。"""
    cfg, app = _new_app(True)
    set_auto = app._cfg_set_auto_cycle
    key = proto.CFG_AUTO_CYCLE_INSTANCES
    # 先显式配置：产水（压差 0.5）→ 产水#2（press1 开、carry 策略、720rpm）
    # → 顺洗
    err = set_auto({key: [
        _inst("produce", ["valve1"], press=True, press_thr=0.5,
              timer=True, timer_minutes=30),
        _inst("produce", ["valve2"], press1=True, press1_thr=1.2,
              stir_sw="carry", rpm=720, rev=True),
        _inst("rinse", ["valve3"], timer=True, timer_minutes=8)]})
    check("显式三实例（含同 mode 重复）⇒ 放行", err == "", err)
    # 旧键写穿：六扁平键 → 首实例；backwashMinutes（缺席）记日志忽略；
    # stageCfg → 同 mode 首实例；stageMinutes → 同 mode 首实例
    err = set_auto({proto.CFG_AUTO_CYCLE_PRESS_THRESHOLD: 0.7,
                    proto.CFG_AUTO_CYCLE_PRESS_COMPARE: "lt",
                    proto.CFG_AUTO_CYCLE_TIMER_FALLBACK: True,
                    proto.CFG_AUTO_CYCLE_TIMER_MINUTES: 45,
                    proto.CFG_AUTO_CYCLE_STAGE_CFG:
                        {"rinse": {"timerEnabled": True,
                                   "timerMinutes": 6}},
                    proto.CFG_AUTO_CYCLE_STAGE_MINUTES: {"rinse": 9},
                    proto.CFG_AUTO_CYCLE_BACKWASH_MINUTES: 21})
    check("显式下 SET 旧键 ⇒ 不报错（写穿）", err == "", err)
    insts = cfg.auto_cycle["instances"]
    check("六扁平键写穿到**首实例**（阈值/方向/兜底/分钟）",
          insts[0]["press_threshold"] == 0.7
          and insts[0]["press_compare"] == "lt"
          and insts[0]["timer_enabled"] is True
          and insts[0]["timer_minutes"] == 45,
          "%s" % (insts[0],))
    check("stageCfg/stageMinutes 写穿到同 mode 首实例"
          "（stageMinutes 不动 enabled 开关）",
          insts[2]["timer_minutes"] == 9
          and insts[2]["timer_enabled"] is True,
          "%s" % (insts[2],))
    check("**逐实例 press1/策略/转速不被旧键写入抹掉**",
          insts[1]["press1_enabled"] is True
          and abs(insts[1]["press1_threshold"] - 1.2) < 1e-9
          and insts[1]["stir_switch_policy"] == "carry"
          and insts[1]["stir_rpm"] == 720
          and insts[1]["stir_reverse"] is True,
          "%s" % (insts[1],))
    check("缺席模式（backwash）的分钟无处写穿 ⇒ 回旧键缺省视图",
          cfg.auto_cycle["backwash_minutes"]
          == proto.AUTO_CYCLE_BACKWASH_MINUTES_DEF,
          "%s" % cfg.auto_cycle["backwash_minutes"])
    # cycleModes ⇒ 忽略不报错（含非法形状也不拒）
    err = set_auto({proto.CFG_AUTO_CYCLE_MODES: ["produce", "produce",
                                                 "produce"]})
    check("显式下 SET cycleModes ⇒ 忽略不报错", err == "", err)
    check("cycleModes 被忽略后实例未变",
          [i["mode"] for i in cfg.auto_cycle["instances"]]
          == ["produce", "produce", "rinse"],
          "%s" % ([i["mode"] for i in cfg.auto_cycle["instances"]],))
    # 带 instances 的同包旧键 ⇒ 一律忽略（instances 优先，不写穿）
    err = set_auto({key: [_inst("produce", ["valve1"], timer=True,
                                timer_minutes=5),
                          _inst("rinse", ["valve3"], timer=True,
                                timer_minutes=6)],
                    proto.CFG_AUTO_CYCLE_PRESS_THRESHOLD: 9.9})
    check("同包 instances+旧键 ⇒ instances 优先（旧键忽略）", err == "", err)
    check("同包旧键没有写进新实例（阈值仍是缺省 0.5）",
          abs(cfg.auto_cycle["instances"][0]["press_threshold"] - 0.5)
          < 1e-9,
          "%s" % cfg.auto_cycle["instances"][0]["press_threshold"])


def test_duplicate_mode_cycle_and_wraparound():
    """⑦e 同 mode 多实例：按序推进 + 末段回绕 + 阶段记忆按下标续跑。"""
    stages_like = None
    instances = [_inst("produce", ["valve1"], timer=True, timer_minutes=3),
                 _inst("backwash", ["valve2"], timer=True, timer_minutes=2),
                 _inst("produce", ["valve3"], timer=True, timer_minutes=4)]
    ctl = _mk(press=0.1, cycle=_cycle_inst_cfg(instances),
              modes={"produce": {"devices": ["valve1"]},
                     "backwash": {"devices": ["valve2"]},
                     "rinse": {"devices": ["valve3"]}})
    ctl.start(ctl.ctx.now_ms(), mode=proto.AUTO_MODE_AUTO)
    check("起始=第一个实例（produce#1，idx0）",
          ctl.mode == "produce" and ctl._cycle_stage_idx == 0,
          "%s/%s" % (ctl.mode, ctl._cycle_stage_idx))
    _elapsed(ctl, 4)
    ctl._cycle_tick(ctl.ctx.now_ms())
    check("定时到期 ⇒ 切到第二个实例（backwash#1，idx1）",
          ctl.mode == "backwash" and ctl._cycle_stage_idx == 1,
          "%s/%s" % (ctl.mode, ctl._cycle_stage_idx))
    _elapsed(ctl, 3)
    ctl._cycle_tick(ctl.ctx.now_ms())
    check("再到期 ⇒ 切到第三个实例（**同 mode 的 produce#2**，idx2）",
          ctl.mode == "produce" and ctl._cycle_stage_idx == 2,
          "%s/%s" % (ctl.mode, ctl._cycle_stage_idx))
    # 停在第 2 个产水实例 ⇒ 停止记住 idx=2（不是 produce 的首个）
    ctl.stop(ctl.ctx.now_ms(), stop_devices=True, reason="用例")
    check("停止 ⇒ 记忆按实例下标（idx=2，不是同 mode 首个）",
          ctl._cycle_resume_idx == 2 and ctl._cycle_resume_mode == "produce",
          "%s/%s" % (ctl._cycle_resume_idx, ctl._cycle_resume_mode))
    check("暂停态带实例下标（pause_info.stageIdx=2）",
          ctl.pause_info().get("stageIdx") == 2,
          "%s" % (ctl.pause_info(),))
    # [暂停表盘指向记忆实例 2026-11-26 复核补锁] 两个产水实例定时不同
    # （3 分钟 / 4 分钟）⇒ 停在第 2 个后 stage_timing 的 total 必须取
    # **记忆实例**（4 分钟）的上限——按 mode id 找首个会错取 3 分钟
    # （评审问题5：stage_timing 暂停分支改按 _pause_stage_idx 的重点）。
    # 此刻 _pause_elapsed_ms 是停止瞬间"本阶段已用"（stop 前 idx2 刚被
    # _cycle_tick 切进来 ⇒ 已用 ≈0），total=240000 是关键区分项。
    _pe, _pt = ctl.stage_timing()
    check("暂停表盘 total 指向**第 2 个产水实例**（4 分钟，≠ 首实例 3 分钟）",
          _pt == 4 * 60000 and _pe >= 0,
          "(elapsed=%s, total=%s)" % (_pe, _pt))
    # 续跑 ⇒ 从第 2 个产水继续（idx=2），不是 idx=0
    ctl.start(ctl.ctx.now_ms(), mode=proto.AUTO_MODE_AUTO)
    check("续跑 ⇒ 从记忆下标继续（produce#2，idx=2）",
          ctl.mode == "produce" and ctl._cycle_stage_idx == 2
          and ctl.phase == ctl.PHASE_VALVES,
          "%s/%s/%s" % (ctl.mode, ctl._cycle_stage_idx, ctl.phase))
    # 末段回绕：idx2 到期 ⇒ 回到 idx0
    _enter_cycle_inst(ctl, 2)
    _elapsed(ctl, 5)
    ctl._cycle_tick(ctl.ctx.now_ms())
    check("末位实例到期 ⇒ 回绕首位（idx=0）",
          ctl._cycle_stage_idx == 0 and ctl.mode == "produce",
          "%s/%s" % (ctl._cycle_stage_idx, ctl.mode))
    # 复位 ⇒ 记忆清空 ⇒ 从头 idx=0
    ctl.stop(ctl.ctx.now_ms(), reason="用例2")
    ctl.reset()
    ctl.start(ctl.ctx.now_ms(), mode=proto.AUTO_MODE_AUTO)
    check("复位后再启动 ⇒ 从头（idx=0）",
          ctl._cycle_stage_idx == 0, "%s" % ctl._cycle_stage_idx)


def test_state_extra_instance_keys():
    """⑦f 协议回显新键锁（autoStageIndex / autoPauseStageIndex）。

    锁（评审问题8）：运行中报当前实例 idx；停止/急停后
    autoPauseStageIndex=记忆 idx；不在循环报 -1；同 mode 两实例运行时
    idx 不同且 autoMode 相同；列表改短越界 ⇒ -1/"auto"。
    """
    instances = [_inst("produce", ["valve1"], timer=True, timer_minutes=3),
                 _inst("produce", ["valve2"], timer=True, timer_minutes=4)]
    app = _FakeApp(press=0.1,
                   cycle=_cycle_inst_cfg(instances),
                   modes={"produce": {"devices": ["valve1"]}})
    ctx = autocontrol.AutoContext(app, log=lambda m: None)
    ctl = autocontrol.AutoController(ctx, log=lambda m: None)
    biz = app_mod.App.__new__(app_mod.App)
    biz.auto_controller = ctl
    # 不在循环 ⇒ -1
    extra = biz._auto_state_extra()
    check("未运行 ⇒ autoStageIndex=-1 / autoPauseStageIndex=-1",
          extra.get(proto.KEY_AUTO_STAGE_INDEX) == -1
          and extra.get(proto.KEY_AUTO_PAUSE_STAGE_INDEX) == -1,
          "%s/%s" % (extra.get(proto.KEY_AUTO_STAGE_INDEX),
                     extra.get(proto.KEY_AUTO_PAUSE_STAGE_INDEX)))
    # 运行第 2 个产水实例 ⇒ idx=1 且 autoMode 相同（都是 produce）
    _enter_cycle_inst(ctl, 1)
    extra = biz._auto_state_extra()
    check("运行第 2 个实例 ⇒ autoStageIndex=1 且 autoMode=produce",
          extra.get(proto.KEY_AUTO_STAGE_INDEX) == 1
          and extra.get(proto.KEY_AUTO_MODE) == "produce",
          "%s/%s" % (extra.get(proto.KEY_AUTO_STAGE_INDEX),
                     extra.get(proto.KEY_AUTO_MODE)))
    ctl.stop(ctl.ctx.now_ms(), stop_devices=True, reason="用例")
    extra = biz._auto_state_extra()
    check("停止后 ⇒ autoPauseMode=produce 且 autoPauseStageIndex=1",
          extra.get(proto.KEY_AUTO_PAUSE_MODE) == "produce"
          and extra.get(proto.KEY_AUTO_PAUSE_STAGE_INDEX) == 1
          and extra.get(proto.KEY_AUTO_STAGE_INDEX) == -1,
          "%s/%s" % (extra.get(proto.KEY_AUTO_PAUSE_MODE),
                     extra.get(proto.KEY_AUTO_PAUSE_STAGE_INDEX)))
    # 列表改短越界 ⇒ autoMode 回 "auto"、autoPauseStageIndex=-1
    app.cfg.auto_cycle = _cycle_inst_cfg([
        _inst("backwash", ["valve2"], timer=True)])
    extra = biz._auto_state_extra()
    check("记忆下标越界 ⇒ autoMode=auto / autoPauseStageIndex=-1",
          extra.get(proto.KEY_AUTO_MODE) == proto.AUTO_MODE_AUTO
          and extra.get(proto.KEY_AUTO_PAUSE_STAGE_INDEX) == -1,
          "%s/%s" % (extra.get(proto.KEY_AUTO_MODE),
                     extra.get(proto.KEY_AUTO_PAUSE_STAGE_INDEX)))


def test_four_triggers_order_and_none():
    """⑦g 四触发机器行为：固定顺序 压差→压力1→压力2→定时；None 不判定；
    lt 方向各一条。

    [顺序断言靠**切相理由文本** 2026-11-26 复核修复] 「同拍多命中取靠
    前者」只看 `_cycle_stage_idx` 分不出哪个触发先命中（谁先命中都切到
    idx1）⇒ 夹具收日志（`_mk(logs=...)`），断言换相行的理由文本：
    「压差 0.8 …」/「压力1 0.9 …」/「定时（… 分钟到期）」——把实现里
    press1/press2 两段对调本用例必红。
    """
    # ① 同拍 压差 + 压力1 + 定时 都命中 ⇒ 取**压差**（最靠前者）
    instances = [_inst("produce", ["valve1"], press=True, press_thr=0.5,
                       press1=True, press1_thr=0.2,
                       timer=True, timer_minutes=1),
                 _inst("rinse", ["valve3"], timer=True)]
    logs1 = []
    ctl = _mk(press=0.8, cycle=_cycle_inst_cfg(instances),
              modes={"produce": {"devices": ["valve1"]},
                     "rinse": {"devices": ["valve3"]}},
              logs=logs1)
    # 压力1/压力2 取数走 app._press_channel_mpa ⇒ 夹具给假值
    ctl.ctx.press_channel_mpa = lambda idx: 0.9
    _enter_cycle_inst(ctl, 0)
    _elapsed(ctl, 2)
    ctl._cycle_tick(ctl.ctx.now_ms())
    switch1 = [str(m) for m in logs1 if "切换到" in str(m)]
    check("压差/压力1/定时同拍 ⇒ 切到 idx1",
          ctl._cycle_stage_idx == 1, "%s" % ctl._cycle_stage_idx)
    check("三触发同拍 ⇒ 理由取**压差**（最靠前者）",
          switch1 and "压差" in switch1[-1]
          and "进水压力" not in switch1[-1] and "定时" not in switch1[-1],
          "%s" % (switch1[-1:] or "无切相日志",))
    # ② press1 与 press2 同拍 ⇒ 取 press1（靠前者）
    instances2 = [_inst("produce", ["valve1"],
                        press1=True, press1_thr=0.2,
                        press2=True, press2_thr=0.3),
                  _inst("rinse", ["valve3"], timer=True)]
    logs2 = []
    ctl2 = _mk(press=None, cycle=_cycle_inst_cfg(instances2),
               modes={"produce": {"devices": ["valve1"]},
                      "rinse": {"devices": ["valve3"]}},
               logs=logs2)
    reads = {1: 0.9, 2: 0.9}
    ctl2.ctx.press_channel_mpa = lambda idx: reads.get(idx)
    _enter_cycle_inst(ctl2, 0)
    ctl2._cycle_tick(ctl2.ctx.now_ms())
    switch2 = [str(m) for m in logs2 if "切换到" in str(m)]
    check("press1/press2 同拍 ⇒ 切到 idx1",
          ctl2._cycle_stage_idx == 1, "%s" % ctl2._cycle_stage_idx)
    check("press1 与 press2 同拍 ⇒ 理由取**压力1**（不是压力2）",
          switch2 and "进水压力" in switch2[-1] and "出水压力" not in switch2[-1],
          "%s" % (switch2[-1:] or "无切相日志",))
    # ③ 压力通道 None ⇒ 不判定不切（timer 没到）
    ctl3 = _mk(press=None, cycle=_cycle_inst_cfg(instances2),
               modes={"produce": {"devices": ["valve1"]},
                      "rinse": {"devices": ["valve3"]}})
    ctl3.ctx.press_channel_mpa = lambda idx: None
    _enter_cycle_inst(ctl3, 0)
    _elapsed(ctl3, 0.5)
    ctl3._cycle_tick(ctl3.ctx.now_ms())
    check("压力通道 None ⇒ 不切（不按缺数据误判）",
          ctl3._cycle_stage_idx == 0, "%s" % ctl3._cycle_stage_idx)
    # ④ 压力1 与定时同拍命中 ⇒ 取**压力1**（定时靠后）
    instances4 = [_inst("produce", ["valve1"],
                        press1=True, press1_thr=0.2,
                        timer=True, timer_minutes=1),
                  _inst("rinse", ["valve3"], timer=True)]
    logs4 = []
    ctl4 = _mk(press=None, cycle=_cycle_inst_cfg(instances4),
               modes={"produce": {"devices": ["valve1"]},
                      "rinse": {"devices": ["valve3"]}},
               logs=logs4)
    ctl4.ctx.press_channel_mpa = lambda idx: 0.9
    _enter_cycle_inst(ctl4, 0)
    _elapsed(ctl4, 2)      # 定时 1 分钟已过 + press1 命中 ⇒ 同拍双命中
    ctl4._cycle_tick(ctl4.ctx.now_ms())
    switch4 = [str(m) for m in logs4 if "切换到" in str(m)]
    check("压力1 与定时同拍 ⇒ 切到 idx1",
          ctl4._cycle_stage_idx == 1, "%s" % ctl4._cycle_stage_idx)
    check("压力1 与定时同拍 ⇒ 理由取**压力1**（不是定时）",
          switch4 and "进水压力" in switch4[-1] and "定时" not in switch4[-1],
          "%s" % (switch4[-1:] or "无切相日志",))
    # ⑤ 只有定时命中（压力都 None）⇒ 取定时
    logs5 = []
    ctl5 = _mk(press=None, cycle=_cycle_inst_cfg(instances4),
               modes={"produce": {"devices": ["valve1"]},
                      "rinse": {"devices": ["valve3"]}},
               logs=logs5)
    ctl5.ctx.press_channel_mpa = lambda idx: None
    _enter_cycle_inst(ctl5, 0)
    _elapsed(ctl5, 2)
    ctl5._cycle_tick(ctl5.ctx.now_ms())
    switch5 = [str(m) for m in logs5 if "切换到" in str(m)]
    check("只有定时命中 ⇒ 理由取**定时**",
          ctl5._cycle_stage_idx == 1 and switch5
          and "定时" in switch5[-1],
          "%s" % (switch5[-1:] or "无切相日志",))
    # ⑥ lt 方向各一条（press1 lt / press2 lt）
    instances6 = [_inst("produce", ["valve1"],
                        press1=True, press1_thr=1.0, press1_cmp="lt"),
                  _inst("rinse", ["valve3"], timer=True)]
    ctl6 = _mk(press=None, cycle=_cycle_inst_cfg(instances6),
               modes={"produce": {"devices": ["valve1"]},
                      "rinse": {"devices": ["valve3"]}})
    ctl6.ctx.press_channel_mpa = lambda idx: 0.4
    _enter_cycle_inst(ctl6, 0)
    ctl6._cycle_tick(ctl6.ctx.now_ms())
    check("press1 lt 方向：0.4 < 1.0 ⇒ 切",
          ctl6._cycle_stage_idx == 1, "%s" % ctl6._cycle_stage_idx)
    instances7 = [_inst("produce", ["valve1"],
                        press2=True, press2_thr=0.3, press2_cmp="lt"),
                  _inst("rinse", ["valve3"], timer=True)]
    ctl7 = _mk(press=None, cycle=_cycle_inst_cfg(instances7),
               modes={"produce": {"devices": ["valve1"]},
                      "rinse": {"devices": ["valve3"]}})
    ctl7.ctx.press_channel_mpa = lambda idx: {2: 0.1}.get(idx)
    _enter_cycle_inst(ctl7, 0)
    ctl7._cycle_tick(ctl7.ctx.now_ms())
    check("press2 lt 方向：0.1 < 0.3 ⇒ 切",
          ctl7._cycle_stage_idx == 1, "%s" % ctl7._cycle_stage_idx)


def test_protect_coexists_with_cycle_trigger():
    """⑦h 与 protect 压力保护共存（评审问题6）：同通道两套都使能、阈值
    相近 ⇒ protect 先动作（2s 去抖 + 停机 + 锁存），循环不再切相。

    [口径] 两套**互相独立、无互斥、无优先级合并**——protect 是保护动作
    （停线+锁存），循环触发是工艺步骤（无去抖）。protect 动作后控制器
    已停 ⇒ 循环 tick 不再跑 ⇒ 事实上 protect 优先。
    """
    instances = [_inst("produce", ["valve1"], press1=True, press1_thr=0.9),
                 _inst("rinse", ["valve3"], timer=True)]
    ctl = _mk(press=None, cycle=_cycle_inst_cfg(instances),
              modes={"produce": {"devices": ["valve1"]},
                     "rinse": {"devices": ["valve3"]}})
    ctl.ctx.press_channel_mpa = lambda idx: 1.5
    _enter_cycle_inst(ctl, 0)
    # 模拟 protect 已动作（raise_fault ⇒ 停止 + 锁存）
    ctl.raise_fault(proto.PROTECT_ALARM_KIND_PRESS1_HIGH,
                    "进水压力过高（保护）", ctl.ctx.now_ms(),
                    stop_devices=True, enqueue_alarm=False)
    check("protect 动作 ⇒ 控制器停（循环随控制器停止而中止）",
          not ctl.running and ctl.fault_latched,
          "running=%s fault=%s" % (ctl.running, ctl.fault_latched))
    idx_before = ctl._cycle_stage_idx
    ctl._cycle_tick(ctl.ctx.now_ms())   # 控制器已停；防御性再调一次
    check("protect 停机后循环不再切相（idx 不变）",
          ctl._cycle_stage_idx == idx_before,
          "%s" % ctl._cycle_stage_idx)
    check("锁存未解 ⇒ 启动被拒（protect 优先于循环切相）",
          ctl.start_preconditions(True)[0] is False,
          "%s" % (ctl.start_preconditions(True),))


def test_stir_max_rpm_clamps_instances():
    """⑦i stirMaxRpm 联动钳位（实例版）：只改上限不带 instances ⇒ 已存
    实例的 stirRpm 全部夹到新上限（GET/运行期/落盘同一份有效值）。"""
    cfg, app = _new_app(True)
    set_auto = app._cfg_set_auto_cycle
    key = proto.CFG_AUTO_CYCLE_INSTANCES
    err = set_auto({key: [_inst("produce", ["valve1"], timer=True,
                                rpm=1200),
                          _inst("rinse", ["valve3"], timer=True,
                                rpm=900)]})
    check("先配实例（1200/900 rpm）⇒ 放行", err == "", err)
    # 只改上限（不带 instances / modes）
    cfg.stir_max_rpm = 600
    # 模拟 _h_config_set 的收口段（同款循环）
    for _m in cfg.auto.values():
        _m["stir_rpm"] = min(int(_m.get("stir_rpm",
                                        proto.AUTO_STIR_RPM_DEF)),
                             int(cfg.stir_max_rpm))
    for _iv in (cfg.auto_cycle.get("instances") or []):
        _iv["stir_rpm"] = min(int(_iv.get("stir_rpm",
                                          proto.AUTO_STIR_RPM_DEF)),
                              int(cfg.stir_max_rpm))
    insts = cfg.auto_cycle["instances"]
    check("只改上限 ⇒ 已存实例转速全部夹到 600",
          insts[0]["stir_rpm"] == 600 and insts[1]["stir_rpm"] == 600,
          "%s/%s" % (insts[0]["stir_rpm"], insts[1]["stir_rpm"]))
    # 同包「stirMaxRpm + instances」：实例按新上限写入（写入口也兜一道）
    app2_cfg, app2 = _new_app(True)
    app2.cfg.stir_max_rpm = 500
    err = app2._cfg_set_auto_cycle({key: [
        _inst("produce", ["valve1"], timer=True, rpm=1200),
        _inst("rinse", ["valve3"], timer=True)]})
    check("同包改上限+实例 ⇒ 实例转速按新上限夹取（≤500）",
          err == "" and app2_cfg.auto_cycle["instances"][0]["stir_rpm"]
          == 500,
          "%s" % app2_cfg.auto_cycle["instances"][0]["stir_rpm"])


def test_compat_view_lossy_and_absent_default():
    """⑦j GET 派生兼容视图：cycleModes 去重保序（有损）+ 缺席模式回合成
    缺省（压差关 + 定时开 10 分钟）。"""
    cfg, app = _new_app(True)
    set_auto = app._cfg_set_auto_cycle
    err = set_auto({proto.CFG_AUTO_CYCLE_INSTANCES: [
        _inst("produce", ["valve1"], timer=True, timer_minutes=5),
        _inst("produce", ["valve2"], timer=True, timer_minutes=6),
        _inst("rinse", ["valve3"], timer=True, timer_minutes=7)]})
    check("三实例（produce×2 + rinse）⇒ 放行", err == "", err)
    doc = app._config_auto_doc()
    cycle = doc.get(proto.CFG_AUTO_CYCLE) or {}
    check("GET 回 instances（camelCase 全键）",
          len(cycle.get(proto.CFG_AUTO_CYCLE_INSTANCES) or []) == 3
          and (cycle[proto.CFG_AUTO_CYCLE_INSTANCES][1]
               .get(proto.CYCLE_INST_STIR_REVERSE) is False)
          and proto.CYCLE_INST_PRESS1_ENABLED
          in cycle[proto.CFG_AUTO_CYCLE_INSTANCES][0],
          "%s" % (cycle.get(proto.CFG_AUTO_CYCLE_INSTANCES),))
    check("GET cycleModes = 实例 mode **去重保序**（有损：produce 只出现"
          "一次）",
          list(cycle.get(proto.CFG_AUTO_CYCLE_MODES) or [])
          == ["produce", "rinse"],
          "%s" % (cycle.get(proto.CFG_AUTO_CYCLE_MODES),))
    check("GET stageCfg：同 mode 首实例的触发",
          (cycle.get(proto.CFG_AUTO_CYCLE_STAGE_CFG) or {})
          .get("produce", {}).get("timerMinutes") == 5,
          "%s" % (cycle.get(proto.CFG_AUTO_CYCLE_STAGE_CFG),))
    check("GET stageCfg：缺席模式（backwash/drain）回合成缺省"
          "（压差关 + 定时开 10 分钟）",
          (cycle.get(proto.CFG_AUTO_CYCLE_STAGE_CFG) or {})
          .get("backwash", {}) == {"pressEnabled": False,
                                   "pressThreshold": 0.5,
                                   "pressCompare": "gt",
                                   "timerEnabled": True,
                                   "timerMinutes": 10},
          "%s" % ((cycle.get(proto.CFG_AUTO_CYCLE_STAGE_CFG) or {})
                  .get("backwash"),))
    check("GET 六扁平键 = 首实例（timerFallback=首实例 timer_enabled）",
          cycle.get(proto.CFG_AUTO_CYCLE_TIMER_MINUTES) == 5
          and cycle.get(proto.CFG_AUTO_CYCLE_TIMER_FALLBACK) is True,
          "%s/%s" % (cycle.get(proto.CFG_AUTO_CYCLE_TIMER_MINUTES),
                     cycle.get(proto.CFG_AUTO_CYCLE_TIMER_FALLBACK)))


def test_explicit_instances_gate_and_start():
    """⑦k 显式实例的启动闸与运行参数（点名文案带实例序号）。"""
    instances = [_inst("produce", ["valve1"], timer=True),
                 _inst("produce", ["valve2"], timer=True)]
    # 第二个产水实例缺设备 ⇒ 拒且点名「产水模式#2」
    bad = proto.normalize_cycle_instances([
        _inst("produce", ["valve1"], timer=True),
        _inst("produce", [], timer=True)])
    ctl = _mk(cycle=_cycle_inst_cfg(bad),
              modes={"produce": {"devices": ["valve1"]}})
    err = ctl.mode_config_error(proto.AUTO_MODE_AUTO)
    check("同 mode 第二实例缺设备 ⇒ 拒（点名「产水模式#2」）",
          err != "" and "产水模式2" in err, err)
    # 齐了 ⇒ 可跑；运行参数按实例（第二个产水的设备=阀2）
    ctl2 = _mk(press=0.1, cycle=_cycle_inst_cfg(instances),
               modes={"produce": {"devices": ["valve1"]}})
    check("实例齐 ⇒ 可跑", ctl2.mode_config_error(proto.AUTO_MODE_AUTO)
          == "", ctl2.mode_config_error(proto.AUTO_MODE_AUTO))
    check("启动成功", ctl2.start(ctl2.ctx.now_ms(),
                                 mode=proto.AUTO_MODE_AUTO), "")
    check("起始=首实例 idx0（设备=阀1——实例自带 devices）",
          ctl2._cycle_stage_idx == 0
          and ctl2._mode_cfg_for("produce").get("devices") == ["valve1"],
          "%s/%s" % (ctl2._cycle_stage_idx,
                     ctl2._mode_cfg_for("produce").get("devices")))
    # 换相到第二个产水 ⇒ 参数改为第二实例（设备=阀2；按**目标实例**）
    ctl2._cycle_switch("produce", "用例", new_idx=1)
    check("换相 ⇒ idx=1 且设备按第二实例（阀2）",
          ctl2._cycle_stage_idx == 1
          and ctl2._mode_cfg_for("produce").get("devices") == ["valve2"],
          "%s/%s" % (ctl2._cycle_stage_idx,
                     ctl2._mode_cfg_for("produce").get("devices")))
    # 停止/急停阀复位按**当前实例**：急停复位开关逐实例
    insts_cfg = _cycle_inst_cfg([
        _inst("produce", ["valve1"], timer=True, reset_estop=True),
        _inst("rinse", ["valve3"], timer=True, reset_estop=False)])
    ctl3 = _mk(cycle=insts_cfg,
               modes={"produce": {"devices": ["valve1"]},
                      "rinse": {"devices": ["valve3"]}})
    _enter_cycle_inst(ctl3, 0)
    check("急停复位开关按当前实例（实例0=True）",
          ctl3._reset_valves_on_estop() is True,
          "%s" % ctl3._reset_valves_on_estop())
    _enter_cycle_inst(ctl3, 1)
    check("急停复位开关按当前实例（实例1=False）",
          ctl3._reset_valves_on_estop() is False,
          "%s" % ctl3._reset_valves_on_estop())


def main():
    failed = []
    for name, fn in sorted(globals().items()):
        if name.startswith("test_") and callable(fn):
            try:
                fn()
                print("  [PASS] %s" % name)
            except AssertionError as exc:
                failed.append(name)
                print("  [FAIL] %s：%s" % (name, exc))
            except Exception as exc:                # noqa: BLE001
                failed.append(name)
                print("  [ERROR] %s：%s: %s" % (name, type(exc).__name__, exc))
    print("\n===== 自动循环模式: %d failed（PASS %d / FAIL %d）====="
          % (len(failed), PASS, FAIL))
    return 1 if failed or FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
