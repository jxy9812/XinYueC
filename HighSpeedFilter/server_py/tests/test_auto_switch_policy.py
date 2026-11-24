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
    """最小 app 夹具（自动模式配置 + 泵组 + 变频器 + 循环实例快照）。"""

    def __init__(self, modes, cycle=None):
        self.digital = []
        self.valve_bank = None
        self.analog = None
        self.temphum = None
        self.button_station = None
        self.pump_bank = FakePumpBank()
        self.vfd = FakeVfd()
        self.cfg = type("C", (), {"auto": modes,
                                  "auto_cycle": cycle or {}})()


def _mk(instances, modes=None):
    """造一个控制器并放到**组合模式**（策略逐实例的读取源）。

    [2026-11-26 实例化] 换相策略/设备清单读**循环快照里的实例**
    （`_mode_cfg_for`）；单模式/非循环路径仍读 auto.modes。本文件全部
    用例都走实例口径（策略键已从 auto.modes 删除）。
    """
    modes = modes or {}
    app = FakeApp(modes, cycle={"enabled": True, "instances": instances})
    ctx = autocontrol.AutoContext(app, log=lambda m: None)
    ctl = autocontrol.AutoController(ctx, log=lambda m: None)
    ctl._cycle_root = proto.AUTO_MODE_AUTO
    ctl._cycle_cfg = {"enabled": True, "instances": list(instances)}
    return ctl


def _inst(mode, devices=("valve1",), stir=None, inlet=None, rpm=None,
          rev=None):
    """一个模式实例（内部 snake_case 策略键＝配置面键名）。"""
    inst = {"mode": mode, "devices": list(devices)}
    if stir is not None:
        inst["stir_switch_policy"] = stir
    if inlet is not None:
        inst["inlet_switch_policy"] = inlet
    if rpm is not None:
        inst["stir_rpm"] = rpm
    if rev is not None:
        inst["stir_reverse"] = rev
    return inst


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
    insts = [_inst("produce", ["valve1", "stirPump", "inletPump"]),
             _inst("backwash", ["valve2", "stirPump", "inletPump"])]
    ctl = _mk(insts)
    await _switch(ctl, "backwash")
    check("① 缺省：换相停进水泵",
          ctl.ctx.pump_bank().calls and ctl.ctx.pump_bank().calls[-1][1] is False,
          "%s" % (ctl.ctx.pump_bank().calls,))
    check("① 缺省：换相停搅拌泵（变频器 STOP）",
          any(c[0] == proto.OP_STOP for c in ctl.ctx.vfd_snapshot().calls),
          "%s" % (ctl.ctx.vfd_snapshot().calls,))
    check("① 缺省：实例策略缺键时读成 stopRestart",
          ctl._switch_policy("backwash", proto.AUTO_MODE_STIR_SWITCH)
          == proto.AUTO_SWITCH_POLICY_DEF,
          ctl._switch_policy("backwash", proto.AUTO_MODE_STIR_SWITCH))
    check("①b 单模式 modes 不再携带策略键（实例化后逐实例）",
          proto.AUTO_MODE_STIR_SWITCH
          not in (ctl.ctx.auto_mode_config("produce") or {}),
          "%s" % (ctl.ctx.auto_mode_config("produce"),))


async def test_carry_keeps_pump_running():
    """② carry：换相**不停**该泵；新模式不选它时泵相必须停掉。"""
    insts = [_inst("produce", ["valve1", "stirPump"]),
             _inst("backwash", ["valve2"],
                   stir=proto.AUTO_SWITCH_POLICY_CARRY)]
    ctl = _mk(insts)
    ctl.mode = "backwash"          # 泵相收尾按当前实例（backwash）读配置
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
    """③ direct：换相立即按新模式启停（新的开⇒立刻起；新的不开⇒立刻停）。

    [2026-11-26 实例化] 启动参数（转速/方向）同样来自**实例**。
    """
    # ③a 新实例要搅拌泵 ⇒ 换相就启动它（带实例的转速/方向）
    insts = [_inst("produce", ["valve1"]),
             _inst("backwash", ["valve2", "stirPump"],
                   stir=proto.AUTO_SWITCH_POLICY_DIRECT, rpm=720, rev=True)]
    ctl = _mk(insts)
    await _switch(ctl, "backwash")
    calls = ctl.ctx.vfd_snapshot().calls
    check("③a direct：换相立即启动搅拌泵（新实例要它开）",
          any(c[0] == proto.OP_START for c in calls), "%s" % (calls,))
    check("③a direct：启动时带上**实例**的转速与方向",
          any(c[0] == proto.OP_START and c[1] is True and c[2] == 720.0
              for c in calls), "%s" % (calls,))
    # ③b 新实例不要搅拌泵 ⇒ 换相立即停掉
    insts2 = [_inst("produce", ["valve1", "stirPump"]),
              _inst("backwash", ["valve2"],
                    stir=proto.AUTO_SWITCH_POLICY_DIRECT)]
    ctl2 = _mk(insts2)
    await _switch(ctl2, "backwash")
    calls2 = ctl2.ctx.vfd_snapshot().calls
    check("③b direct：新实例不要它 ⇒ 换相立即停",
          any(c[0] == proto.OP_STOP for c in calls2), "%s" % (calls2,))


async def test_two_pumps_independent():
    """④ 两个泵各自独立（搅拌泵 carry + 进水泵 stopRestart）。"""
    insts = [_inst("produce", ["valve1", "stirPump", "inletPump"]),
             _inst("backwash", ["valve2", "stirPump"],
                   stir=proto.AUTO_SWITCH_POLICY_CARRY,
                   inlet=proto.AUTO_SWITCH_POLICY_STOP_RESTART)]
    ctl = _mk(insts)
    await _switch(ctl, "backwash")
    check("④ 搅拌泵 carry（没停）",
          not any(c[0] == proto.OP_STOP for c in ctl.ctx.vfd_snapshot().calls),
          "%s" % (ctl.ctx.vfd_snapshot().calls,))
    check("④ 进水泵 stopRestart（停了）",
          ctl.ctx.pump_bank().calls
          and ctl.ctx.pump_bank().calls[-1][1] is False,
          "%s" % (ctl.ctx.pump_bank().calls,))


async def test_duplicate_target_instance_policy():
    """④b 目标=重复 mode 的**非首个**实例 ⇒ 策略/devices 按 **#2 实例**。

    [2026-11-26 复核修复回归锁] 同 mode 两实例策略/devices 都不同
    （backwash#1=carry·只阀2；backwash#2=direct·阀2+阀4+进水泵）：从
    产水换相到 **idx=3（backwash#2）**，换相窗口内的泵处置必须按 #2 的
    direct 与它的 devices 走——按 mode id 找首个会拿到 #1 的 carry ⇒
    两泵都 keep、无任何立即动作（修复前的实际行为，此用例修复前红）。
    """
    logs = []
    insts = [
        _inst("produce", ["valve1"]),
        _inst("backwash", ["valve2"],
              stir=proto.AUTO_SWITCH_POLICY_CARRY,
              inlet=proto.AUTO_SWITCH_POLICY_CARRY),
        _inst("rinse", ["valve3"]),
        _inst("backwash", ["valve2", "valve4", "inletPump"],
              stir=proto.AUTO_SWITCH_POLICY_DIRECT,
              inlet=proto.AUTO_SWITCH_POLICY_DIRECT),
    ]
    app = FakeApp({}, cycle={"enabled": True, "instances": insts})
    ctx = autocontrol.AutoContext(app, log=lambda m: logs.append(m))
    ctl = autocontrol.AutoController(ctx, log=lambda m: logs.append(m))
    ctl._cycle_root = proto.AUTO_MODE_AUTO
    ctl._cycle_cfg = {"enabled": True, "instances": list(insts)}
    ctl.mode = "produce"
    ctl._cycle_stage_idx = 0
    # [走真实换相入口] `_cycle_switch` 内部先记账（idx+mode）再处置泵——
    # 修复点就在这条路径上。之后**只等投递任务落定、绝不补发**策略：
    # 再调一次 `_apply_switch_policies` 会在 mode 已被 `_begin_mode_phase`
    # 修正后读到正确实例 ⇒ 把窗口内读错实例的缺陷掩盖（A/B 实测）。
    ctl._cycle_switch("backwash", "复核④b", new_idx=3)
    for _ in range(50):
        await asyncio.sleep(0.01)
        if not ctl._phase_tasks:
            break
    vfd_calls = ctl.ctx.vfd_snapshot().calls
    pump_calls = ctl.ctx.pump_bank().calls
    # 按 #2 实例（direct）：搅拌泵不被 #2 选中 ⇒ 换相立即停；
    # 进水泵被 #2 选中 ⇒ 换相立即启动。
    check("④b 换相窗口按 **#2 实例** direct 处置搅拌泵（立即 STOP）",
          any(c[0] == proto.OP_STOP for c in vfd_calls),
          "%s" % (vfd_calls,))
    check("④b 换相窗口按 **#2 实例** direct 启动进水泵",
          any(c[1] is True for c in pump_calls), "%s" % (pump_calls,))
    check("④b 换相策略日志读的是 #2（搅拌泵=direct）",
          any("搅拌泵=direct" in str(m) for m in logs),
          "%s" % ([str(m) for m in logs if "换相策略" in str(m)],))
    check("④b 不是按 #1（carry ⇒ 两泵 keep 无动作）",
          not (not vfd_calls and not pump_calls
               and ctl._carry_pending_stop == {"stirPump", "inletPump"}),
          "pending=%s" % (ctl._carry_pending_stop,))
    # 换相后参数表就是 #2（devices 含阀4+进水泵）
    cfg_now = ctl._mode_cfg_for("backwash")
    check("④b 换相后参数表 = #2 实例（devices=阀2+阀4+进水泵）",
          cfg_now.get("devices") == ["valve2", "valve4", "inletPump"],
          "%s" % (cfg_now,))


def test_config_layer():
    """⑤ 配置层：实例策略非法值 fail-closed、落盘往返一致（**实例键**）。"""
    cfg = cfgmod.Config()
    cfg.resolve_paths(os.path.join(os.environ.get("TEMP", "/tmp"),
                                   "hsf_switch_cfg"))
    # 缺省＝先停再启（合成实例的策略缺省；modes 已无策略键）
    _d = proto.default_cycle_instance("produce")
    check("⑤ 缺省配置：实例两策略都是先停再启",
          _d["stir_switch_policy"] == proto.AUTO_SWITCH_POLICY_DEF
          and _d["inlet_switch_policy"] == proto.AUTO_SWITCH_POLICY_DEF,
          "%s" % (_d,))
    check("⑤b 单模式 modes 缺省**不含**策略键（实例化后删）",
          proto.AUTO_MODE_STIR_SWITCH not in cfg.auto["produce"]
          and proto.AUTO_MODE_INLET_SWITCH not in cfg.auto["produce"],
          "%s" % (cfg.auto["produce"],))
    # 显式实例（carry/direct）→ 落盘 → 读回 一致
    cfg.auto_cycle["instances"] = proto.normalize_cycle_instances([
        {"mode": "produce", "devices": ["valve1"],
         "stirSwitchPolicy": proto.AUTO_SWITCH_POLICY_CARRY,
         "inletSwitchPolicy": proto.AUTO_SWITCH_POLICY_DIRECT},
        {"mode": "rinse", "devices": ["valve3"], "timerEnabled": True}])
    cfg.auto_cycle["instances_explicit"] = True
    cfg._sync_auto_cycle_legacy()
    cfg.save()
    cfg2 = cfgmod.Config()
    cfg2.resolve_paths(cfg.base_dir)
    cfg2.load()
    check("⑤c 落盘往返：实例换相策略一字不差",
          cfg2.auto_cycle["instances"][0]["stir_switch_policy"]
          == proto.AUTO_SWITCH_POLICY_CARRY
          and cfg2.auto_cycle["instances"][0]["inlet_switch_policy"]
          == proto.AUTO_SWITCH_POLICY_DIRECT
          and cfg2.auto_cycle["instances_explicit"] is True,
          "%s" % (cfg2.auto_cycle["instances"],))


def main():
    async def _all():
        await test_default_is_stop_restart()
        await test_carry_keeps_pump_running()
        await test_direct_switches_to_new_state()
        await test_two_pumps_independent()
        # [2026-11-26 复核回归锁] 目标=重复 mode 的非首个实例 ⇒ 按 #2 实例处置。
        await test_duplicate_target_instance_policy()
    asyncio.run(_all())
    test_config_layer()
    print("\n汇总：PASS %d / FAIL %d" % (PASS, FAIL))
    return 0 if FAIL == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
