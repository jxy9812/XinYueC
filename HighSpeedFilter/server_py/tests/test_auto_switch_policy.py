# -*- coding: utf-8 -*-
"""换相策略回归（2026-11-14 用户定案）。

[测什么] 自动模式**换相**（组合模式循环 产水→反洗→顺洗）时，阀门切换要
时间（真机行程 ~9s），这期间泵怎么办有三种诉求，**逐泵**可配：

    stopRestart —— **保持之前的逻辑**：换相先停泵，阀到位后（泵相）再按
                  新模式配置启动（缺省，既有行为）；
    carry      —— **保持上一模式的状态**：换相途中该泵**不动**（运行中
                  就继续运行、停了就继续停）；
    direct     —— **直接切到当前模式的状态**：换相**立即**按新模式的
                  目标启停该泵（与阀门切换并行）。

逐条锁：
    ① 缺省＝先停再启（既有行为不变）；
    ② carry：换相**不停**该泵；且新模式**不选**它时，泵相必须把它停掉
       （否则它会一直转）；
    ③ direct：换相**立即**按新模式启停（新的开 ⇒ 立刻起；新的不开 ⇒
       立刻停）；
    ④ 两个泵各自独立（搅拌泵 carry + 进水泵 stopRestart 这种组合）；
    ⑤ 配置层：非法值在 SET 侧被拒（fail-closed）、落盘往返一致。

[怎么跑]
    python -I tests/test_auto_switch_policy.py      # 独立跑
    python -I tests/run_all.py                      # 全量（含本段）
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

from hsfsrv import autocontrol      # noqa: E402
from hsfsrv import config as cfgmod  # noqa: E402
from hsfsrv import proto            # noqa: E402

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
# 夹具：假泵组 + 假变频器（记录动作）
# ============================================================================

class FakePumpBank(object):
    """假进水泵组：记录 set_pump 调用。

    [items 字段不能少] 控制器的 `_stop_pumps()` 按 `len(bank.items)` 逐台
    停 ⇒ 夹具缺 items 会抛 AttributeError，把后面的动作一起吞掉（实测：
    停泵异常 ⇒ 变频器的 STOP 根本没发出去，断言看起来像"策略没生效"）。
    """

    def __init__(self):
        self.calls = []
        self.items = [0]          # 1 台进水泵（与现场口径一致）

    async def set_pump(self, index, on):
        self.calls.append((index, bool(on)))
        return True, ""


class FakeVfd(object):
    """假变频器：记录 apply_control（op/rpm/reverse）。"""

    def __init__(self):
        self.calls = []

    async def apply_control(self, op, reverse=False, hz=0.0, rpm=0.0,
                            value=0.0, index=0):
        self.calls.append((op, bool(reverse), float(rpm)))
        return True, ""


class FakeApp(object):
    """最小 app 夹具（自动模式配置 + 泵组 + 变频器）。"""

    def __init__(self, modes):
        self.digital = []
        self.valve_bank = None
        self.analog = None
        self.temphum = None
        self.button_station = None
        self.pump_bank = FakePumpBank()
        self.vfd = FakeVfd()
        self.cfg = type("C", (), {"auto": modes})()


def _mk(modes):
    app = FakeApp(modes)
    ctx = autocontrol.AutoContext(app, log=lambda m: None)
    return autocontrol.AutoController(ctx, log=lambda m: None)


def _mode(devices=("valve1",), stir=None, inlet=None):
    """造一个模式配置（换相策略键直接用协议键名）。"""
    m = {"devices": list(devices)}
    if stir is not None:
        m[proto.AUTO_MODE_STIR_SWITCH] = stir
    if inlet is not None:
        m[proto.AUTO_MODE_INLET_SWITCH] = inlet
    return m


async def _switch(ctl, new_mode):
    """执行一次换相，并**等**换相里投递的异步动作落定。"""
    ctl._apply_switch_policies(new_mode)
    # [等投递的任务] _apply_switch_policies 把动作投到事件循环上 ⇒ 等一拍。
    for _ in range(50):
        await asyncio.sleep(0.01)
        if not ctl._phase_tasks:
            break


# ============================================================================
# 用例
# ============================================================================

async def test_default_is_stop_restart():
    """① 缺省＝先停再启（既有行为不变：换相把两个泵都停掉）。"""
    modes = {"produce": _mode(["valve1", "stirPump", "inletPump"]),
             "backwash": _mode(["valve2", "stirPump", "inletPump"])}
    ctl = _mk(modes)
    await _switch(ctl, "backwash")
    check("① 缺省：换相停进水泵",
          ctl.ctx.pump_bank().calls and ctl.ctx.pump_bank().calls[-1][1] is False,
          "%s" % (ctl.ctx.pump_bank().calls,))
    check("① 缺省：换相停搅拌泵（变频器 STOP）",
          any(c[0] == proto.OP_STOP for c in ctl.ctx.vfd_snapshot().calls),
          "%s" % (ctl.ctx.vfd_snapshot().calls,))
    check("① 缺省：策略缺键时读成 stopRestart",
          ctl._switch_policy("backwash", proto.AUTO_MODE_STIR_SWITCH)
          == proto.AUTO_SWITCH_POLICY_DEF,
          ctl._switch_policy("backwash", proto.AUTO_MODE_STIR_SWITCH))


async def test_carry_keeps_pump_running():
    """② carry：换相**不停**该泵；新模式不选它时泵相必须停掉。"""
    modes = {"produce": _mode(["valve1", "stirPump"]),
             "backwash": _mode(["valve2"], stir=proto.AUTO_SWITCH_POLICY_CARRY)}
    ctl = _mk(modes)
    await _switch(ctl, "backwash")
    check("② carry：换相**不停**搅拌泵（没有 STOP 动作）",
          not any(c[0] == proto.OP_STOP for c in ctl.ctx.vfd_snapshot().calls),
          "%s" % (ctl.ctx.vfd_snapshot().calls,))
    check("② carry 且新模式不选它 ⇒ 记进待停集合（泵相收尾要停）",
          "stirPump" in ctl._carry_pending_stop,
          "%s" % (ctl._carry_pending_stop,))
    # 泵相收尾：跑到泵相 ⇒ 应下发一次 STOP（把还在转的搅拌泵停掉）。
    ctl.phase = ctl.PHASE_PUMPS
    ctl._pump_tasks = None
    ctl._step_pumps(ctl.ctx.now_ms())
    await asyncio.sleep(0.05)
    check("② carry 收尾：泵相把仍在转的搅拌泵停掉",
          any(c[0] == proto.OP_STOP for c in ctl.ctx.vfd_snapshot().calls),
          "%s" % (ctl.ctx.vfd_snapshot().calls,))
    # [2026-11-16 审计口径] 待停集合在**验收成功**时清（原来建表即清 ⇒
    # 收尾停泵一失败就被永久丢弃，新模式没选的那台泵一直转）。验收发生
    # 在下一拍 ⇒ 真实运行是逐拍驱动，这里同样再驱动一拍后断言。
    ctl._step_pumps(ctl.ctx.now_ms())
    await asyncio.sleep(0.05)
    check("② carry 收尾后待停集合清空",
          not ctl._carry_pending_stop, "%s" % (ctl._carry_pending_stop,))


async def test_direct_switches_to_new_state():
    """③ direct：换相立即按新模式启停（新的开⇒立刻起；新的不开⇒立刻停）。"""
    # ③a 新模式要搅拌泵 ⇒ 换相就启动它（带新模式的转速/方向）
    modes = {"produce": _mode(["valve1"]),
             "backwash": _mode(["valve2", "stirPump"],
                               stir=proto.AUTO_SWITCH_POLICY_DIRECT)}
    modes["backwash"]["stir_rpm"] = 720
    modes["backwash"]["stir_reverse"] = True
    ctl = _mk(modes)
    await _switch(ctl, "backwash")
    calls = ctl.ctx.vfd_snapshot().calls
    check("③a direct：换相立即启动搅拌泵（新模式要它开）",
          any(c[0] == proto.OP_START for c in calls), "%s" % (calls,))
    check("③a direct：启动时带上新模式的转速与方向",
          any(c[0] == proto.OP_START and c[1] is True and c[2] == 720.0
              for c in calls), "%s" % (calls,))
    # ③b 新模式不要搅拌泵 ⇒ 换相立即停掉
    modes2 = {"produce": _mode(["valve1", "stirPump"]),
              "backwash": _mode(["valve2"],
                                stir=proto.AUTO_SWITCH_POLICY_DIRECT)}
    ctl2 = _mk(modes2)
    await _switch(ctl2, "backwash")
    calls2 = ctl2.ctx.vfd_snapshot().calls
    check("③b direct：新模式不要它 ⇒ 换相立即停",
          any(c[0] == proto.OP_STOP for c in calls2), "%s" % (calls2,))


async def test_two_pumps_independent():
    """④ 两个泵各自独立（搅拌泵 carry + 进水泵 stopRestart）。"""
    modes = {"produce": _mode(["valve1", "stirPump", "inletPump"]),
             "backwash": _mode(["valve2", "stirPump"],
                               stir=proto.AUTO_SWITCH_POLICY_CARRY,
                               inlet=proto.AUTO_SWITCH_POLICY_STOP_RESTART)}
    ctl = _mk(modes)
    await _switch(ctl, "backwash")
    check("④ 搅拌泵 carry（没停）",
          not any(c[0] == proto.OP_STOP for c in ctl.ctx.vfd_snapshot().calls),
          "%s" % (ctl.ctx.vfd_snapshot().calls,))
    check("④ 进水泵 stopRestart（停了）",
          ctl.ctx.pump_bank().calls
          and ctl.ctx.pump_bank().calls[-1][1] is False,
          "%s" % (ctl.ctx.pump_bank().calls,))


def test_config_layer():
    """⑤ 配置层：非法值 fail-closed、落盘往返一致。"""
    cfg = cfgmod.Config()
    cfg.resolve_paths(os.path.join(os.environ.get("TEMP", "/tmp"),
                                   "hsf_switch_cfg"))
    # 缺省＝先停再启
    check("⑤ 缺省配置：两个泵都是先停再启",
          cfg.auto["produce"][proto.AUTO_MODE_STIR_SWITCH]
          == proto.AUTO_SWITCH_POLICY_DEF
          and cfg.auto["produce"][proto.AUTO_MODE_INLET_SWITCH]
          == proto.AUTO_SWITCH_POLICY_DEF,
          "%s" % (cfg.auto["produce"],))
    # 落盘 → 读回 一致
    cfg.auto["produce"][proto.AUTO_MODE_STIR_SWITCH] = \
        proto.AUTO_SWITCH_POLICY_CARRY
    cfg.auto["produce"][proto.AUTO_MODE_INLET_SWITCH] = \
        proto.AUTO_SWITCH_POLICY_DIRECT
    cfg.save()
    cfg2 = cfgmod.Config()
    cfg2.resolve_paths(cfg.base_dir)
    cfg2.load()
    check("⑤ 落盘往返：换相策略一字不差",
          cfg2.auto["produce"][proto.AUTO_MODE_STIR_SWITCH]
          == proto.AUTO_SWITCH_POLICY_CARRY
          and cfg2.auto["produce"][proto.AUTO_MODE_INLET_SWITCH]
          == proto.AUTO_SWITCH_POLICY_DIRECT,
          "%s" % (cfg2.auto["produce"],))


def main():
    async def _all():
        await test_default_is_stop_restart()
        await test_carry_keeps_pump_running()
        await test_direct_switches_to_new_state()
        await test_two_pumps_independent()
    asyncio.run(_all())
    test_config_layer()
    print("\n汇总：PASS %d / FAIL %d" % (PASS, FAIL))
    return 0 if FAIL == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
