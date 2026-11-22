# -*- coding: utf-8 -*-
"""自动模式「停止＝暂停 / 启动＝续跑 / 复位＝清零」回归（2026-11-19 用户口径）。

[测什么] 用户口径逐字照做：

    「自动模式下**不管是哪个模式运行**，按下停止后都要**保留停止时候的
      状态**，之后按启动是**继续运行**，而不是从头开始，**特别是定时器
      时间要保留**，除非按过**复位按钮**，屏幕上的或者**实体的复位**
      都可以才会重置状态重置时间」
    「**阀门状态还是按照设置的是否复位来**」（这条口径不动）

逐条落成断言：

    ① **停止＝暂停**：停止后记住运行态（根模式 / 模式 / 阶段 / 阶段已用
       时长）；相位报 "paused"；表盘仍报"记住的阶段与剩余时间"（冻结在
       停止那一刻，暂停期间不继续走）。
    ② **启动＝续跑**：从**同一模式、同一阶段**继续，**剩余时间接着走**
       （先跑 3 秒 → 停止 → 暂停 2 秒 → 启动 ⇒ 已用时间仍 ≈3 秒，既不是
       5 秒（把暂停时间算进去）也不是 0（重新计时））。
    ③ **复位＝清零**：屏幕上的复位（op=4）与**实体复位按钮**（按钮组
       DI15 的沿）都清掉记住的状态与计时 ⇒ 下次启动从头开始。
    ④ **单模式**同样保留（口径「不管是哪个模式运行」）。
    ⑤ **续跑时先等阀到位**：阀未到位 ⇒ **搅拌/进水泵都不启动**；阀到位
       后才按**该模式配置的设备集合**启泵（配了 stirPump 才启搅拌）。
    ⑥ **阀门复位口径不变**：停止时阀是否复位仍按 `resetValvesOnStop`
       （开 ⇒ 阀被关回；关 ⇒ 阀不动）。
    ⑦ 端到端（真服务端 + 真协议）：停止后的状态快照带暂停态；续跑走
       阀相→泵相；实体复位按钮同样清零。

[怎么跑]
    python -I tests/test_auto_pause_resume.py     # 独立跑
    pytest tests/test_auto_pause_resume.py -q     # pytest 收集
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

from hsfsrv import autocontrol               # noqa: E402
from hsfsrv import bson, proto                 # noqa: E402
from test_auto_modes import (                 # noqa: E402  复用真服务端夹具
    _feed_while,
    _set_valve_di,
    _spin_server,
)
from test_vfd_ops import Client                # noqa: E402  复用协议客户端

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
#                     假设备夹具（单元层：相位机可控、无真总线）
# ============================================================================

class _Clock(object):
    """假时钟（毫秒）：让"暂停期间过了 2 秒"这类断言**确定地**复现。"""

    def __init__(self, start=1000000):
        self.now = int(start)

    def advance(self, ms):
        self.now += int(ms)
        return self.now


class _FakeValveItem(object):
    def __init__(self):
        self.ready = True
        self.state = proto.HsfValveState.Closed
        self.open_cmd = False
        self.close_cmd = False


class _FakeValveBank(object):
    """假阀组：命令写进 cmds；`arrive` 为 False 时**停在动作中**（不到位）。"""

    def __init__(self, count=2, arrive=True):
        self.items = [_FakeValveItem() for _ in range(count)]
        self.cmds = []
        self.arrive = bool(arrive)

    async def set_valve(self, index, want_open):
        want_open = bool(want_open)
        self.cmds.append((int(index), want_open))
        item = self.items[int(index)]
        item.open_cmd = want_open
        item.close_cmd = not want_open
        item.state = (proto.HsfValveState.Open if want_open
                      else proto.HsfValveState.Closed) if self.arrive \
            else proto.HsfValveState.Moving
        return True, ""

    def arrive_all(self, want_open=True):
        """模拟阀体走到位（反馈到位）。"""
        for item in self.items:
            item.state = (proto.HsfValveState.Open if want_open
                          else proto.HsfValveState.Closed)

    def arrive_targets(self, targets):
        """按**目标表**逐路到位（未选中的阀目标是"关"）。"""
        for idx, want in dict(targets or {}).items():
            self.items[int(idx)].state = (
                proto.HsfValveState.Open if want
                else proto.HsfValveState.Closed)


class _FakePumpItem(object):
    def __init__(self):
        self.on = False


class _FakePumpBank(object):
    def __init__(self, count=1):
        self.items = [_FakePumpItem() for _ in range(count)]
        self.cmds = []

    async def set_pump(self, index, on):
        self.cmds.append((int(index), bool(on)))
        self.items[int(index)].on = bool(on)
        return True, ""


class _FakeVfd(object):
    def __init__(self):
        self.commands = []
        self.running = False

    async def apply_control(self, op, reverse=False, hz=0.0, rpm=0.0,
                            value=0.0, index=0):
        self.commands.append((op, bool(reverse), hz, rpm))
        if op == proto.OP_START:
            self.running = True
        elif op == proto.OP_STOP:
            self.running = False
        return True, ""


class _FakeButtonStation(object):
    """假按钮组（够 AutoContext 用：沿 + 灯态 + 急停点位）。"""

    def __init__(self):
        self.start_edge = False
        self.stop_edge = False
        self.reset_edge = False
        self.estop_ok = True
        self.state = "idle"
        self.running = False

    def _ch(self, key):
        return None                     # 没配急停回路 ⇒ 不拦启动

    def set_running(self, running):
        self.running = bool(running)

    def snapshot(self):
        return {}


class _FakeCfg(object):
    def __init__(self, modes, cycle=None):
        self.auto = modes
        self.auto_cycle = cycle if cycle is not None else {}
        self.valve_move_timeout_ms = 20000
        self.vfd_stale_ms = 3000

    def auto_run_ms(self, default=0):
        return default

    def auto_mode(self, default=""):
        return default

    def save_auto_run_ms(self, ms):
        return None

    def save_auto_mode(self, mode):
        return None


class _FakeApp(object):
    def __init__(self, modes, cycle=None, arrive=True, valve_count=2):
        self.digital = []
        self.analog = None
        self.temphum = None
        self.button_station = None
        self.vfd = _FakeVfd()
        self.valve_bank = _FakeValveBank(valve_count, arrive=arrive)
        self.pump_bank = _FakePumpBank()
        self.cfg = _FakeCfg(modes, cycle)

    def is_auto(self):
        return True


def _stage(press_enabled=False, press_threshold=0.5, press_compare="gt",
           timer_enabled=True, timer_minutes=10):
    """一个阶段的切换条件（内部 snake_case 键＝配置面键名）。"""
    return {"press_enabled": bool(press_enabled),
            "press_threshold": float(press_threshold),
            "press_compare": str(press_compare),
            "timer_enabled": bool(timer_enabled),
            "timer_minutes": int(timer_minutes)}


def _cycle_cfg(stages, stage_cfg=None):
    """造一份 auto_cycle 配置（与 config.py 的落盘形状一致）。"""
    stages = list(stages)
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
                      else {stages[0]: _stage(True, 0.5, "gt", False, 30)}),
    }
    return cfg


def _mk(app, clock):
    """造一个控制器（时钟换成假时钟 ⇒ 时间断言确定）。"""
    ctx = autocontrol.AutoContext(app, log=lambda m: None)
    ctx.now_ms = lambda: clock.now
    return autocontrol.AutoController(ctx, log=lambda m: None)


async def _tick(ctl, clock, times=1, dt=100):
    """推进 n 拍（每拍 100ms），并让拍内投递的异步任务跑完。"""
    for _ in range(times):
        clock.advance(dt)
        ctl.step(clock.now)
        await asyncio.sleep(0.01)


def _modes(stages, reset_on_stop=True, devices=None):
    """给每个模式配设备（默认：一台阀；产水再加搅拌泵与进水泵）。"""
    out = {}
    for i, mid in enumerate(stages):
        devs = list(devices) if devices else ["valve%d" % (i + 1)]
        out[mid] = {"devices": devs,
                    "stir_rpm": 1440,
                    "stir_reverse": False,
                    proto.AUTO_MODE_RESET_VALVES_ON_STOP: bool(reset_on_stop),
                    proto.AUTO_MODE_RESET_VALVES_ON_ESTOP: False}
    return out


# ============================================================================
# ① 组合循环：停止＝暂停（模式/阶段/计时都保留）
# ============================================================================

async def _case_cycle_stop_keeps_state():
    stages = ["produce", "backwash"]
    sc = {stages[0]: _stage(True, 0.5, "gt", False, 30),   # 定时关
          stages[1]: _stage(False, 0.5, "gt", True, 10)}   # 定时 10 分钟
    app = _FakeApp(_modes(stages), _cycle_cfg(stages, sc))
    clock = _Clock()
    ctl = _mk(app, clock)

    ctl.start(clock.now, mode=proto.AUTO_MODE_AUTO)
    check("启动组合模式 ⇒ 进阀相", ctl.phase == ctl.PHASE_VALVES, ctl.phase)
    await _tick(ctl, clock, 4)
    check("跑到运行相", ctl.phase == ctl.PHASE_RUNNING, ctl.phase)

    # 换到第二个阶段（带 10 分钟定时），并让它"跑了" 3 秒
    ctl._cycle_switch(stages[1], "用例")
    await _tick(ctl, clock, 4)
    check("换相后阶段=%s" % stages[1], ctl.mode == stages[1], ctl.mode)
    clock.advance(3000)
    ran_ms = clock.now - ctl._cycle_stage_at      # 停止瞬间该阶段的已用时长

    ctl.stop(clock.now, stop_devices=True, reason="用例")
    check("停止 ⇒ 进入暂停态", ctl.paused)
    check("停止 ⇒ 相位报 paused", ctl.phase == ctl.PHASE_PAUSED, ctl.phase)
    info = ctl.pause_info()
    check("停止 ⇒ 记住根模式=组合循环",
          info["root"] == proto.AUTO_MODE_AUTO, info)
    check("停止 ⇒ 记住阶段=%s" % stages[1], info["mode"] == stages[1], info)
    elapsed, total = ctl.stage_timing()
    check("停止 ⇒ 阶段已用时长保留（=停止瞬间的 %dms，不是 0）" % ran_ms,
          elapsed == ran_ms and ran_ms >= 3000,
          "%s vs %s" % (elapsed, ran_ms))
    check("停止 ⇒ 阶段总时长保留（600000）", total == 600000, total)

    # 暂停 2 秒 ⇒ 表盘冻结（不继续走）
    clock.advance(2000)
    elapsed2, _total2 = ctl.stage_timing()
    check("暂停期间计时冻结（不继续走表）", elapsed2 == elapsed,
          "%s vs %s" % (elapsed2, elapsed))


# ============================================================================
# ② 启动＝续跑：同一模式/同一阶段，剩余时间接着走
# ============================================================================

async def _case_cycle_resume_continues_timer():
    stages = ["produce", "backwash"]
    sc = {stages[0]: _stage(True, 0.5, "gt", False, 30),
          stages[1]: _stage(False, 0.5, "gt", True, 10)}
    app = _FakeApp(_modes(stages), _cycle_cfg(stages, sc))
    clock = _Clock()
    ctl = _mk(app, clock)

    ctl.start(clock.now, mode=proto.AUTO_MODE_AUTO)
    await _tick(ctl, clock, 4)
    ctl._cycle_switch(stages[1], "用例")
    await _tick(ctl, clock, 4)
    clock.advance(3000)
    ctl.stop(clock.now, stop_devices=True, reason="用例")
    paused_elapsed, _ = ctl.stage_timing()

    # 暂停 2 秒（这段时间**不该**算进阶段用时）
    clock.advance(2000)

    # 续跑：请求组合循环（客户端下拉里的"自动（流量循环）"）
    ctl.start(clock.now, mode=proto.AUTO_MODE_AUTO)
    check("续跑 ⇒ 仍是记住的阶段[%s]" % stages[1],
          ctl.mode == stages[1], ctl.mode)
    check("续跑 ⇒ 相位回阀相（要走一遍阀相等到位）",
          ctl.phase == ctl.PHASE_VALVES, ctl.phase)
    resumed, total = ctl.stage_timing()
    check("续跑 ⇒ 剩余时间接着走（仍是 %dms；不是 +2000 也不是 0）"
          % paused_elapsed,
          abs(resumed - paused_elapsed) <= 50 and paused_elapsed >= 3000,
          "%s（暂停时 %s）" % (resumed, paused_elapsed))
    check("续跑 ⇒ 总时长不变", total == 600000, total)
    check("续跑 ⇒ 暂停态被消费（不再是暂停）", not ctl.paused)

    # 续跑后再跑 10 拍（每拍 100ms）⇒ 用时 = 续跑值 + 1000
    await _tick(ctl, clock, 10)
    later, _ = ctl.stage_timing()
    check("续跑后继续累加（=续跑值 + 10 拍 × 100ms）",
          later == resumed + 1000, "%s vs %s" % (later, resumed + 1000))

    # 客户端把"记住的子阶段"回填进下拉后按启动 ⇒ 仍按组合循环续跑
    ctl.stop(clock.now, stop_devices=True, reason="用例2")
    ctl.start(clock.now, mode=stages[1])
    check("按子阶段启动 ⇒ 仍续跑组合循环的该阶段",
          ctl.mode == stages[1] and ctl._cycle_root == proto.AUTO_MODE_AUTO,
          "%s / %s" % (ctl.mode, ctl._cycle_root))


# ============================================================================
# ③ 复位＝清零（屏幕上的复位 与 实体复位按钮 两条路径）
# ============================================================================

async def _case_reset_clears_pause():
    stages = ["produce", "backwash"]
    sc = {stages[0]: _stage(True, 0.5, "gt", False, 30),
          stages[1]: _stage(False, 0.5, "gt", True, 10)}
    app = _FakeApp(_modes(stages), _cycle_cfg(stages, sc))
    clock = _Clock()
    ctl = _mk(app, clock)

    ctl.start(clock.now, mode=proto.AUTO_MODE_AUTO)
    await _tick(ctl, clock, 4)
    ctl._cycle_switch(stages[1], "用例")
    await _tick(ctl, clock, 4)
    clock.advance(3000)
    ctl.stop(clock.now, stop_devices=True, reason="用例")
    check("停止 ⇒ 有暂停态", ctl.paused)

    # ---- 屏幕上的复位（op=4 走的就是 ctl.reset()）----
    ctl.reset()
    check("复位 ⇒ 暂停态清空", not ctl.paused)
    check("复位 ⇒ 循环阶段记忆清空", ctl._cycle_resume_mode == "",
          ctl._cycle_resume_mode)
    elapsed, total = ctl.stage_timing()
    check("复位 ⇒ 表盘回 (-1, 0)", (elapsed, total) == (-1, 0),
          "%s" % ((elapsed, total),))
    check("复位 ⇒ 相位回 idle", ctl.phase == ctl.PHASE_IDLE, ctl.phase)

    ctl.start(clock.now, mode=proto.AUTO_MODE_AUTO)
    check("复位后再启动 ⇒ 从首阶段[%s]开始" % stages[0],
          ctl.mode == stages[0], ctl.mode)
    fresh, _ = ctl.stage_timing()
    check("复位后再启动 ⇒ 计时从头（≈0）", fresh <= 200, fresh)


async def _case_physical_reset_clears_pause():
    """**实体复位按钮**（按钮组 DI15 的沿）同样清零。"""
    stages = ["produce", "backwash"]
    sc = {stages[0]: _stage(True, 0.5, "gt", False, 30),
          stages[1]: _stage(False, 0.5, "gt", True, 10)}
    app = _FakeApp(_modes(stages), _cycle_cfg(stages, sc))
    clock = _Clock()
    ctl = _mk(app, clock)
    station = _FakeButtonStation()
    app.button_station = station

    ctl.start(clock.now, mode=proto.AUTO_MODE_AUTO)
    await _tick(ctl, clock, 4)
    ctl._cycle_switch(stages[1], "用例")
    await _tick(ctl, clock, 4)
    clock.advance(3000)
    ctl.stop(clock.now, stop_devices=True, reason="用例")
    check("停止 ⇒ 有暂停态", ctl.paused)

    # 实体复位按钮：DI15 的上升沿 ⇒ reset_edge ⇒ reset()
    station.reset_edge = True
    ctl.poll_idle(clock.now, True)
    check("实体复位 ⇒ 暂停态清空", not ctl.paused)
    check("实体复位 ⇒ 阶段记忆清空", ctl._cycle_resume_mode == "",
          ctl._cycle_resume_mode)
    elapsed, total = ctl.stage_timing()
    check("实体复位 ⇒ 表盘回 (-1, 0)", (elapsed, total) == (-1, 0),
          "%s" % ((elapsed, total),))


# ============================================================================
# ④ 单模式同样保留（口径「不管是哪个模式运行」）
# ============================================================================

async def _case_single_mode_pause():
    stages = ["produce", "backwash"]
    app = _FakeApp(_modes(stages))
    clock = _Clock()
    ctl = _mk(app, clock)

    ctl.start(clock.now, mode="produce")
    await _tick(ctl, clock, 4)
    check("单模式 ⇒ 跑到运行相", ctl.phase == ctl.PHASE_RUNNING, ctl.phase)
    ctl.stop(clock.now, stop_devices=True, reason="用例")
    check("单模式停止 ⇒ 暂停态", ctl.paused)
    info = ctl.pause_info()
    check("单模式停止 ⇒ 记住模式 produce",
          info["mode"] == "produce" and info["root"] == "produce", info)
    check("单模式停止 ⇒ 相位报 paused", ctl.phase == ctl.PHASE_PAUSED,
          ctl.phase)
    # 单模式没有阶段定时 ⇒ 表盘仍不可知（不编造倒计时）
    elapsed, total = ctl.stage_timing()
    check("单模式停止 ⇒ 表盘 (-1, 0)（单模式无定时）",
          (elapsed, total) == (-1, 0), "%s" % ((elapsed, total),))

    ctl.start(clock.now, mode="produce")
    check("单模式续跑 ⇒ 同一模式", ctl.mode == "produce", ctl.mode)
    check("单模式续跑 ⇒ 暂停态被消费", not ctl.paused)
    await _tick(ctl, clock, 4)
    check("单模式续跑 ⇒ 相位机照常（阀相→泵相→运行相）",
          ctl.phase == ctl.PHASE_RUNNING, ctl.phase)

    # 换成别的模式 ⇒ 旧的暂停态作废（新的意图 ⇒ 从头开始）
    ctl.stop(clock.now, stop_devices=True, reason="用例2")
    ctl.start(clock.now, mode="backwash")
    check("换了模式 ⇒ 暂停态作废", not ctl.paused)
    check("换了模式 ⇒ 跑新模式", ctl.mode == "backwash", ctl.mode)


# ============================================================================
# ⑤ 续跑时先等阀到位；到位后才按该模式配置启泵
# ============================================================================

async def _case_resume_waits_valve_arrival():
    """阀**不到位** ⇒ 搅拌/进水泵都**不许**启动；到位后才按配置启泵。"""
    stages = ["produce", "backwash"]
    modes = _modes(stages)
    modes["produce"] = {"devices": ["valve1", "stirPump", "inletPump"],
                         "stir_rpm": 1440, "stir_reverse": False,
                         proto.AUTO_MODE_RESET_VALVES_ON_STOP: True,
                         proto.AUTO_MODE_RESET_VALVES_ON_ESTOP: False}
    app = _FakeApp(modes, arrive=False)      # 假阀：命令后**不到位**
    clock = _Clock()
    ctl = _mk(app, clock)

    ctl.start(clock.now, mode="produce")
    await _tick(ctl, clock, 3)
    check("阀未到位 ⇒ 仍在阀相", ctl.phase == ctl.PHASE_VALVES, ctl.phase)
    check("阀未到位 ⇒ 搅拌泵没被启动", not app.vfd.running,
          app.vfd.commands)
    check("阀未到位 ⇒ 进水泵没被启动", not app.pump_bank.items[0].on,
          app.pump_bank.cmds)

    app.valve_bank.arrive_targets(ctl._valve_targets)
    await _tick(ctl, clock, 4)
    check("阀到位 ⇒ 进入泵相/运行相",
          ctl.phase in (ctl.PHASE_PUMPS, ctl.PHASE_RUNNING), ctl.phase)
    check("阀到位后 ⇒ 按配置启动搅拌泵", app.vfd.running, app.vfd.commands)
    check("阀到位后 ⇒ 按配置启动进水泵", app.pump_bank.items[0].on,
          app.pump_bank.cmds)


async def _case_pumps_follow_mode_config():
    """泵启停按**该模式配的设备集合**（配了 stirPump 才启搅拌）。"""
    stages = ["produce", "backwash"]
    modes = _modes(stages)
    modes["produce"] = {"devices": ["valve1", "inletPump"],
                         "stir_rpm": 1440, "stir_reverse": False,
                         proto.AUTO_MODE_RESET_VALVES_ON_STOP: True,
                         proto.AUTO_MODE_RESET_VALVES_ON_ESTOP: False}
    app = _FakeApp(modes)
    clock = _Clock()
    ctl = _mk(app, clock)
    ctl.start(clock.now, mode="produce")
    await _tick(ctl, clock, 4)
    check("跑到运行相", ctl.phase == ctl.PHASE_RUNNING, ctl.phase)
    check("没配 stirPump ⇒ 不启动搅拌泵", not app.vfd.running,
          app.vfd.commands)
    check("配了 inletPump ⇒ 启动进水泵", app.pump_bank.items[0].on,
          app.pump_bank.cmds)


# ============================================================================
# ⑥ 阀门复位口径不变（resetValvesOnStop 开⇒关回；关⇒不动）
# ============================================================================

async def _case_valve_reset_config_unchanged():
    stages = ["produce", "backwash"]
    for reset_on_stop, want_closed in ((True, True), (False, False)):
        modes = _modes(stages, reset_on_stop=reset_on_stop)
        app = _FakeApp(modes)
        clock = _Clock()
        ctl = _mk(app, clock)
        ctl.start(clock.now, mode="produce")
        await _tick(ctl, clock, 4)
        app.valve_bank.cmds.clear()
        ctl.stop(clock.now, stop_devices=True, reason="用例")
        await asyncio.sleep(0.05)          # 让"全停"任务跑完
        closed = [c for c in app.valve_bank.cmds if not c[1]]
        if want_closed:
            check("resetValvesOnStop=True ⇒ 停止时阀被关回",
                  len(closed) >= 1, app.valve_bank.cmds)
        else:
            check("resetValvesOnStop=False ⇒ 停止时阀不动（无关阀命令）",
                  not closed, app.valve_bank.cmds)


# ============================================================================
# ⑦ 端到端（真服务端 + 真协议）：暂停态快照 / 续跑等阀到位 / 实体复位清零
# ============================================================================

def test_e2e_pause_resume():
    base = tempfile.mkdtemp(prefix="hsf_pause_")
    log = []
    try:
        asyncio.run(_case_e2e(base, log))
    finally:
        shutil.rmtree(base, ignore_errors=True)


async def _case_e2e(basedir, log):
    # [阀超时放宽到 3000ms] 真机 20s；这里要留出"阀未到位"的观察窗口
    # （续跑时先等阀到位那段），又不能让它在窗口内误判超时。
    business, server, d0 = await _spin_server(basedir, log,
                                              valve_move_timeout_ms=3000)
    errors = []

    def scenario():
        try:
            c = Client("127.0.0.1", server.port)
            c.hello()
            ok, _role = c.login("root", "root")
            assert ok, "root 登录失败"

            # ---- 配产水模式：阀1 + 搅拌泵 + 进水泵；停止时**复位阀**（开）----
            r = c.request(proto.CMD_CONFIG_SET, bson.encode({
                "section": proto.CFG_SECTION_AUTO,
                "modes": {"produce": {"devices": ["valve1", "stirPump",
                                                  "inletPump"],
                                      "stirRpm": 1440, "stirReverse": False,
                                      proto.AUTO_MODE_RESET_VALVES_ON_STOP:
                                          True}}}))
            assert r and r[1].get("ok") is True, "auto SET 应受理：%r" % (
                r[1] if r else None,)
            assert _set_mode(c, business.MODE_AUTO)[1].get("ok") is True

            # ---- 启动 ⇒ 阀相 ⇒（模拟到位）⇒ 运行相 ----
            r = c.request(proto.CMD_AUTO_CONTROL,
                          bson.encode({"on": True,
                                       proto.KEY_AUTO_MODE: "produce"}))
            assert r and r[1].get("ok") is True, "启动应受理：%r" % (r[1],)
            _wait_do(c, d0, 0, 1, 6.0)            # 阀1 的开命令
            _set_valve_di(d0, 0, True)            # 阀1 开到位
            state = _wait_phase(c, autocontrol.AutoController.PHASE_RUNNING,
                                 8.0)
            check("端到端：启动 ⇒ 跑到运行相",
                  state.get(proto.KEY_AUTO_PHASE) ==
                  autocontrol.AutoController.PHASE_RUNNING,
                  "%r" % (state,))
            check("端到端：搅拌泵已启动", business.vfd._running is True)

            # ---- 停止 ⇒ 暂停态（阀被复位关回）----
            r = c.request(proto.CMD_AUTO_CONTROL, bson.encode({"on": False}))
            assert r and r[1].get("ok") is True, "停止应受理：%r" % (r[1],)
            state = _wait_phase(c, autocontrol.AutoController.PHASE_PAUSED,
                                 5.0)
            check("端到端：停止 ⇒ 相位报 paused",
                  state.get(proto.KEY_AUTO_PHASE) ==
                  autocontrol.AutoController.PHASE_PAUSED, "%r" % (state,))
            check("端到端：停止 ⇒ 快照带 autoPaused=true",
                  state.get(proto.KEY_AUTO_PAUSED) is True, "%r" % (state,))
            check("端到端：停止 ⇒ 快照记住模式 produce",
                  state.get(proto.KEY_AUTO_PAUSE_MODE) == "produce",
                  "%r" % (state,))
            check("端到端：停止 ⇒ 搅拌泵已停", business.vfd._running is False)
            _wait_do(c, d0, 0, 0, 5.0)            # 阀1 被关回（命令）
            _set_valve_di(d0, 0, False)            # 阀1 关到位反馈

            # ---- 续跑 ⇒ 先等阀到位；到位前泵**不许**启动 ----
            r = c.request(proto.CMD_AUTO_CONTROL,
                          bson.encode({"on": True,
                                       proto.KEY_AUTO_MODE: "produce"}))
            assert r and r[1].get("ok") is True, "续跑应受理：%r" % (r[1],)
            state = _wait_phase(c, autocontrol.AutoController.PHASE_VALVES,
                                 5.0)
            check("端到端：续跑 ⇒ 先走阀相（等阀到位）",
                  state.get(proto.KEY_AUTO_PHASE) ==
                  autocontrol.AutoController.PHASE_VALVES, "%r" % (state,))
            check("端到端：阀未到位前搅拌泵不许启动",
                  business.vfd._running is False)
            check("端到端：阀未到位前进水泵不许启动", d0.do[6] == 0,
                  "%s" % (d0.do,))

            # ---- 阀到位 ⇒ 才按配置启泵 ----
            _set_valve_di(d0, 0, True)
            state = _wait_phase(c, autocontrol.AutoController.PHASE_RUNNING,
                                 8.0)
            check("端到端：阀到位 ⇒ 到运行相",
                  state.get(proto.KEY_AUTO_PHASE) ==
                  autocontrol.AutoController.PHASE_RUNNING, "%r" % (state,))
            check("端到端：阀到位后才启动搅拌泵",
                  business.vfd._running is True)
            check("端到端：阀到位后才启动进水泵", d0.do[6] == 1,
                  "%s" % (d0.do,))

            # ---- 实体复位按钮（DI15）⇒ 暂停态清零 ----
            r = c.request(proto.CMD_AUTO_CONTROL, bson.encode({"on": False}))
            assert r and r[1].get("ok") is True
            state = _wait_phase(c, autocontrol.AutoController.PHASE_PAUSED,
                                 5.0)
            check("端到端：再次停止 ⇒ 处于暂停态",
                  state.get(proto.KEY_AUTO_PAUSED) is True, "%r" % (state,))
            reset_di = business.cfg.button_items.get("reset_di", 15)
            d0.di[reset_di] = 1                   # 复位键（常开）上升沿
            state = _wait_phase(c, autocontrol.AutoController.PHASE_IDLE, 5.0)
            check("端到端：实体复位 ⇒ 暂停态清零",
                  state.get(proto.KEY_AUTO_PAUSED) is False, "%r" % (state,))
            check("端到端：实体复位 ⇒ 不再记住模式",
                  state.get(proto.KEY_AUTO_PAUSE_MODE) == "", "%r" % (state,))
            c.close()
        except Exception as exc:                    # noqa: BLE001
            import traceback
            errors.append("%s\n%s" % (exc, traceback.format_exc()))

    thread = threading.Thread(target=scenario, name="pause-client")
    fail_before = FAIL
    thread.start()
    try:
        await _feed_while(business, thread)
    finally:
        thread.join()
        await server.stop()
    if errors:
        raise RuntimeError(errors[0])
    # [端到端用 check() 记断言] ⇒ 失败数涨了就是红 ⇒ 抛出去让门禁看见。
    if FAIL != fail_before:
        raise RuntimeError("端到端断言失败 %d 条" % (FAIL - fail_before))


def _set_mode(client, mode):
    return client.request(proto.CMD_RUN_MODE, bson.encode({"mode": mode}))


def _auto_state(client):
    r = client.request(proto.CMD_RUN_MODE, b"")
    return r[1] if r else {}


def _wait_phase(client, phase, timeout):
    """等相位到目标（期间持续发请求保活：裸 socket 客户端没有心跳）。"""
    import time as _time
    deadline = _time.time() + timeout
    state = {}
    while _time.time() < deadline:
        state = _auto_state(client)
        if state.get(proto.KEY_AUTO_PHASE) == phase:
            return state
        _time.sleep(0.1)
    return state


def _wait_do(client, d0, index, want, timeout):
    """等某路 DO 达到目标值（期间持续发请求保活）。"""
    import time as _time
    deadline = _time.time() + timeout
    while _time.time() < deadline:
        if d0.do[index] == want:
            return True
        client.pump(0.05)
    return d0.do[index] == want


# ============================================================================
#                                  入口
# ============================================================================

def main():
    """独立跑入口（与 pytest 收集等价；退出码 0=全过）。"""
    failed = []
    cases = [
        ("组合循环停止保留状态", _case_cycle_stop_keeps_state),
        ("组合循环续跑接着计时", _case_cycle_resume_continues_timer),
        ("复位清零", _case_reset_clears_pause),
        ("实体复位清零", _case_physical_reset_clears_pause),
        ("单模式同样保留", _case_single_mode_pause),
        ("续跑先等阀到位", _case_resume_waits_valve_arrival),
        ("泵按模式配置启停", _case_pumps_follow_mode_config),
        ("阀门复位口径不变", _case_valve_reset_config_unchanged),
    ]
    for label, fn in cases:
        print("  --- %s" % label)
        try:
            asyncio.run(fn())
        except Exception as exc:                    # noqa: BLE001
            failed.append(label)
            print("  [ERROR] %s：%s: %s" % (label, type(exc).__name__, exc))
    # 端到端（自带真服务端）
    print("  --- 端到端（真服务端 + 真协议）")
    try:
        test_e2e_pause_resume()
    except Exception as exc:                        # noqa: BLE001
        failed.append("端到端")
        print("  [ERROR] 端到端：%s: %s" % (type(exc).__name__, exc))
    print("\n===== 停止＝暂停/启动＝续跑/复位＝清零: %d failed"
          "（check PASS %d / FAIL %d）=====" % (len(failed), PASS, FAIL))
    return 1 if failed or FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
