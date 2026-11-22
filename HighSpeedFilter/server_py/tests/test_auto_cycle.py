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


def _mk(press=None, cycle=None, modes=None):
    app = _FakeApp(press=press, cycle=cycle, modes=modes)
    ctx = autocontrol.AutoContext(app, log=lambda m: None)
    # [压差读数] 夹具直接给 MPa（走 ctx.press_mpa 的口径）。
    ctx.press_mpa = lambda: app._press
    ctl = autocontrol.AutoController(ctx, log=lambda m: None)
    return ctl


def _enter_cycle(ctl, stage):
    """把控制器放到「组合模式 + 指定子阶段 + running」的状态。"""
    ctl.mode = proto.AUTO_MODE_AUTO
    ctl._cycle_root = proto.AUTO_MODE_AUTO
    ctl._cycle_cfg = ctl._cycle_config()
    ctl.mode = stage
    ctl._cycle_stage_at = ctl.ctx.now_ms()
    ctl._running = True
    ctl.phase = ctl.PHASE_RUNNING


def _next_of(stages, mode):
    """配置列表里的"下一个"（最后一个的下一个＝第一个 ⇒ 闭环）。"""
    idx = list(stages).index(mode)
    return list(stages)[(idx + 1) % len(stages)]


def _elapsed(ctl, minutes):
    """把当前阶段的起点往回拨 minutes 分钟（模拟"已跑满"）。"""
    ctl._cycle_stage_at = ctl.ctx.now_ms() - int(minutes * 60000)


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
      · 非组合模式（单模式启动）/ 未运行 ⇒ (-1, 0)，客户端显示「剩余 --」。
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
    # 单模式启动（非组合循环）⇒ 没有阶段定时
    ctl = _mk(press=0.1)
    ctl.mode = stages[1]
    ctl._running = True
    ctl.phase = ctl.PHASE_RUNNING
    check("单模式启动 ⇒ (-1, 0)（剩余不可知）",
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
    """⑥d 空列表 ⇒ 启动闸拒绝并说明（不让它静默跑成别的链）。"""
    ctl = _mk(cycle=_cycle_cfg(stages=[]))
    err = ctl.mode_config_error(proto.AUTO_MODE_AUTO)
    check("空列表 ⇒ 启动闸拒绝并指路远端设置",
          err != "" and "未配置参与循环的模式" in err, err)
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
        cfg.auto_cycle["cycle_modes"] = ["rinse", "produce", "drain"]
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
              list(c.get("cycle_modes") or []) == ["rinse", "produce", "drain"],
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
