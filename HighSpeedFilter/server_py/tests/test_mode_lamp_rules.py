# -*- coding: utf-8 -*-
"""模式规则 + 按钮组灯的回归（2026-11-15 用户口径：手动模式灯跟随设备在动）。

[测什么]
    A. **灯的归属按模式分**（2026-11-15 用户定案，取代 2026-11-10 的
       「手动模式一个 DO 都不写」）：
         · **自动模式**：启动灯 = 自动运行中（急停回路异常按停着）；
         · **手动模式**：启动灯 = **现场还有设备在动**（搅拌泵 / 进水泵 /
           任意一路电动球阀）——该事实由 app 层算好后经
           `set_manual_running()` 推进给按钮组。
       两种模式下停止灯与启动灯**严格互斥**、复位灯是按键点动镜像。
       [为什么改这条口径] 旧口径下手动模式的灯停在旧状态：现场什么设备
       都没在跑，启动灯却亮着（用户 2026-11-15 现场截图）⇒ 操作员误判。
       旧口径的初衷（人工点的灯不被自动逻辑覆盖）在手动模式已无对象 ——
       手动模式下的启动灯现在表达的是**设备在不在动**这个客观事实。
    B. **手动模式「在动」判定**：三条判据各锁一条。
    C. **手动模式：现场停止按钮 ⇒ 泵停 + 球阀全关 + 搅拌泵停**
       （用户口径「按下停止也能全部关闭」「按下停止也关闭全部球阀」）。
    D. **手动模式：急停 ⇒ 同上**（用户口径「急停也有效果」）。
    E. **切换模式 ⇒ 关闭所有设备**（手动<->自动**双向**）：切到自动也要全停。
    F. **只有按了启动按钮后设备才允许工作**：切自动**不**启动控制器。

[怎么跑]
    python3 -I tests/test_mode_lamp_rules.py          # 独立跑
    pytest tests/test_mode_lamp_rules.py -q           # pytest 收集

服务端在本进程内起（假设备 + 真业务层）。
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

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.dirname(HERE))

from hsfsrv import auth as auth_mod          # noqa: E402
from hsfsrv import autocontrol as acmod     # noqa: E402
from hsfsrv import buttons as buttons_mod    # noqa: E402
from hsfsrv import config as cfgmod          # noqa: E402
from hsfsrv import devices as devices_mod    # noqa: E402
from hsfsrv import proto                    # noqa: E402
from fake_devices import build_fake_app      # noqa: E402


PASS = 0
FAIL = 0


def check(name, ok, detail=""):
    """记一条断言结果（口径与其它 tests 一致：PASS/FAIL 计数 + 明细）。"""
    global PASS, FAIL
    if ok:
        PASS += 1
        print("  [PASS] %s %s" % (name, detail))
    else:
        FAIL += 1
        print("  [FAIL] %s %s" % (name, detail))


class FakeCtx(object):
    """只实现按钮站用到的三个接口，并记录每次写 DO。"""

    def __init__(self, di=None, do=None):
        self.di = dict(di or {})
        self.do = dict(do or {})
        self.writes = []          # [(channel, on, module)]

    def read_di(self, channel, module=0):
        return self.di.get(channel)

    def read_do(self, channel, module=0):
        return self.do.get(channel)

    async def write_do(self, channel, on, module=0):
        self.writes.append((channel, bool(on), module))
        self.do[channel] = bool(on)
        return True, ""


def _station_items():
    return {
        buttons_mod.KEY_ESTOP: 12,
        buttons_mod.KEY_START: 13,
        buttons_mod.KEY_STOP: 14,
        buttons_mod.KEY_RESET: 15,
        buttons_mod.KEY_START_LAMP: 7,
        buttons_mod.KEY_STOP_LAMP: 8,
        buttons_mod.KEY_RESET_LAMP: 9,
        buttons_mod.KEY_MODULE: 0,
    }


async def _settle():
    """让 ensure_future 投递出去的写任务跑完（灯写/停机都是异步投递）。"""
    for _ in range(5):
        await asyncio.sleep(0)


# ============================================================================
#                     A 灯的归属（按模式分）
# ============================================================================

async def test_lamp_writing_by_mode():
    """A1~A5：自动=跟随运行态；手动=跟随设备在动；两模式停止灯互斥。"""
    print("=== A 灯的归属：自动=运行态，手动=设备在动 ===")
    # 急停常闭且正常（通=1）；启动/复位常开未按=0；停止常闭未按=1
    di = {12: True, 13: False, 14: True, 15: False}

    # ---- A1 自动模式：未运行 ⇒ 停止灯亮、启动灯灭 ----
    ctx = FakeCtx(di=di)
    st = buttons_mod.ButtonStation(ctx, _station_items())
    st.poll(1000, auto_mode=True)
    await _settle()
    wrote = dict((ch, on) for ch, on, _m in ctx.writes)
    check("A1 自动模式未运行：写灯（停止灯亮、启动灯灭）",
          wrote.get(8) is True and wrote.get(7) is False,
          "writes=%s" % (ctx.writes,))

    # ---- A2 自动模式：运行中 ⇒ 启动灯亮、停止灯灭 ----
    ctx2 = FakeCtx(di=di)
    st2 = buttons_mod.ButtonStation(ctx2, _station_items())
    st2.set_running(True)
    st2.poll(2000, auto_mode=True)
    await _settle()
    wrote = dict((ch, on) for ch, on, _m in ctx2.writes)
    check("A2 自动模式运行中：启动灯亮、停止灯灭",
          wrote.get(7) is True and wrote.get(8) is False,
          "writes=%s" % (ctx2.writes,))

    # ---- A3 手动模式：设备都没在动 ⇒ 启动灯灭、停止灯亮 ----
    ctx3 = FakeCtx(di=di)
    st3 = buttons_mod.ButtonStation(ctx3, _station_items())
    st3.poll(3000, auto_mode=False)
    await _settle()
    wrote = dict((ch, on) for ch, on, _m in ctx3.writes)
    check("A3 手动模式无设备在动：启动灯灭、停止灯亮",
          wrote.get(7) is False and wrote.get(8) is True,
          "writes=%s" % (ctx3.writes,))

    # ---- A4 手动模式：有设备在动 ⇒ 启动灯亮、停止灯灭 ----
    ctx4 = FakeCtx(di=di)
    st4 = buttons_mod.ButtonStation(ctx4, _station_items())
    st4.set_manual_running(True)
    st4.poll(4000, auto_mode=False)
    await _settle()
    wrote = dict((ch, on) for ch, on, _m in ctx4.writes)
    check("A4 手动模式有设备在动：启动灯亮、停止灯灭",
          wrote.get(7) is True and wrote.get(8) is False,
          "writes=%s" % (ctx4.writes,))

    # ---- A5 手动模式：安全件照采（急停仍要判）----
    check("A5 手动模式：仍采集到急停正常", st4.estop_ok is True,
          "estop_ok=%s" % (st4.estop_ok,))

    # ---- A6 手动模式：外部把灯打灭 ⇒ 下一拍按回读补写（自愈）----
    ctx4.do[8] = False
    st4.set_manual_running(False)
    st4.poll(5000, auto_mode=False)
    await _settle()
    wrote = dict((ch, on) for ch, on, _m in ctx4.writes)
    check("A6 手动模式：外部清零后自愈（停止灯复亮）",
          wrote.get(8) is True, "writes=%s" % (ctx4.writes,))

    # ---- A7 需要复位（报警锁存 ∨ 急停锁存）⇒ **复位灯闪烁** ----
    # [2026-11-24 用户口径] 不再是常亮；半周期内必须看到亮/灭都出现。
    ctx7 = FakeCtx(di=di)
    st7 = buttons_mod.ButtonStation(ctx7, _station_items())
    st7.set_need_reset(True)
    st7.poll(6000, auto_mode=True)
    await _settle()
    check("A7a 需要复位：复位灯进入亮段",
          ctx7.do.get(9) is True, "do=%s" % (ctx7.do,))
    st7.poll(6500, auto_mode=True)      # 默认 500/500 ⇒ 半周期处灭
    await _settle()
    check("A7b 需要复位：复位灯进入灭段（闪烁，不是常亮）",
          ctx7.do.get(9) is False, "do=%s" % (ctx7.do,))
    st7.poll(7000, auto_mode=True)      # 下一周期亮段
    await _settle()
    check("A7c 需要复位：复位灯回到亮段",
          ctx7.do.get(9) is True, "do=%s" % (ctx7.do,))

    # ---- A8 没有锁存 ⇒ 复位灯仍是**点动镜像**（按住亮、松手灭）----
    ctx8 = FakeCtx(di=di)          # DI15=False ⇒ 复位键没按
    st8 = buttons_mod.ButtonStation(ctx8, _station_items())
    st8.set_need_reset(False)
    st8.poll(7000, auto_mode=True)
    await _settle()
    wrote8 = dict((ch, on) for ch, on, _m in ctx8.writes)
    check("A8 无锁存且未按复位：复位灯灭",
          wrote8.get(9) is False, "writes=%s" % (ctx8.writes,))
    di_pressed = dict(di)          # 按住复位键（DI15=1）
    di_pressed[15] = True
    ctx8b = FakeCtx(di=di_pressed)
    st8b = buttons_mod.ButtonStation(ctx8b, _station_items())
    st8b.set_need_reset(False)
    st8b.poll(7100, auto_mode=True)
    await _settle()
    wrote8b = dict((ch, on) for ch, on, _m in ctx8b.writes)
    check("A8b 无锁存但按住复位键：复位灯点动亮",
          wrote8b.get(9) is True, "writes=%s" % (ctx8b.writes,))


# ============================================================================
#                   B 手动模式「在动」判定（AutoContext）
# ============================================================================

class _FakeVfd(object):
    def __init__(self, running=False, hz=0.0):
        self._running = running
        self._hz = hz

    def is_running(self):
        return self._running

    def output_freq_hz(self):
        return self._hz


class _FakeBank(object):
    def __init__(self, items):
        self.items = items


class _FakeValveItem(object):
    def __init__(self, open_fb=False, open_cmd=False, close_fb=False):
        self.open_fb = open_fb
        self.open_cmd = open_cmd
        self.close_fb = close_fb


class _FakePumpItem(object):
    def __init__(self, on=False):
        self.on = on


def _ctx_with(vfd=None, pumps=None, valves=None):
    """造一个只带设备访问口的 AutoContext（判据不需要别的）。"""
    ctx = acmod.AutoContext.__new__(acmod.AutoContext)

    class _App(object):
        pass

    app = _App()
    app.vfd = vfd
    app.pump_bank = pumps
    app.valve_bank = valves
    ctx._app = app
    ctx._log = lambda m: None
    ctx._throttle = {}
    ctx.last_error = ""
    ctx.action_count = 0
    return ctx


async def test_manual_running_predicate():
    """B1~B5：搅拌泵 / 进水泵 / 球阀 三条判据 + 全不动的反证。"""
    print("=== B 手动模式「在动」判定 ===")

    ctx = _ctx_with(vfd=_FakeVfd(running=True),
                     pumps=_FakeBank([_FakePumpItem(False)]),
                     valves=_FakeBank([_FakeValveItem()]))
    check("B1 搅拌泵在转 ⇒ 在动", ctx.manual_running() is True)

    ctx = _ctx_with(vfd=_FakeVfd(running=False, hz=12.5),
                     pumps=_FakeBank([_FakePumpItem(False)]),
                     valves=_FakeBank([_FakeValveItem()]))
    check("B2 停机斜坡（输出频率 > 0）⇒ 在动", ctx.manual_running() is True)

    ctx = _ctx_with(vfd=_FakeVfd(running=False, hz=0.0),
                     pumps=_FakeBank([_FakePumpItem(True)]),
                     valves=_FakeBank([_FakeValveItem()]))
    check("B3 进水泵在开 ⇒ 在动", ctx.manual_running() is True)

    ctx = _ctx_with(vfd=_FakeVfd(running=False, hz=0.0),
                     pumps=_FakeBank([_FakePumpItem(False)]),
                     valves=_FakeBank([_FakeValveItem(open_cmd=True)]))
    check("B4 球阀已下开命令 ⇒ 在动", ctx.manual_running() is True)

    ctx = _ctx_with(vfd=_FakeVfd(running=False, hz=0.0),
                     pumps=_FakeBank([_FakePumpItem(False)]),
                     valves=_FakeBank([_FakeValveItem(open_fb=True)]))
    check("B5 球阀开到位反馈 ⇒ 在动", ctx.manual_running() is True)

    ctx = _ctx_with(vfd=_FakeVfd(running=False, hz=0.0),
                     pumps=_FakeBank([_FakePumpItem(False)]),
                     valves=_FakeBank([_FakeValveItem()]))
    check("B6 全都不动 ⇒ 不在动", ctx.manual_running() is False)

    # 没装配任何设备 ⇒ 不在动（不能因为读不到就报"在动"）
    ctx = _ctx_with()
    check("B7 未装配设备 ⇒ 不在动", ctx.manual_running() is False)


# ============================================================================
#        C/D 手动模式：现场停止按钮 / 急停 ⇒ 设备与球阀全部停下来
# ============================================================================

def _rig_manual(cfg, log):
    """假设备 + 真阀组/泵组/按钮组（build_fake_app 不建这三件）。"""
    from hsfsrv import buttons as buttons_mod
    from hsfsrv import pumps as pumps_mod
    from hsfsrv import valves as valves_mod

    business = build_fake_app(cfg, log=log)
    business.valve_bank = valves_mod.ValveBank(
        business.digital, cfg.valve_items, lambda _m: None,
        move_timeout_ms=cfg.valve_move_timeout_ms)
    business.pump_bank = pumps_mod.PumpBank(
        business.digital, cfg.pump_items, lambda _m: None)
    business.button_station = buttons_mod.ButtonStation(
        business.auto_ctx, cfg.button_items,
        debounce_ms=cfg.button_debounce_ms, log=lambda _m: None)
    business.button_station.set_running(False)
    return business


async def _case_manual_stop_estop(basedir, log):
    cfg = cfgmod.Config()
    cfg.resolve_paths(basedir)
    cfg.config_path = os.path.join(basedir, "cfg", "x.json")
    cfg.users_path = os.path.join(basedir, "cfg", "hsf_users.json")
    os.makedirs(os.path.join(basedir, "cfg"), exist_ok=True)
    cfg.port = 0
    cfg.enable_serial = False
    cfg.button_debounce_ms = 0        # 测试要快：不去抖
    cfg.valve_move_timeout_ms = 600

    business = _rig_manual(cfg, log)
    ctl = business.auto_controller
    d0 = business.digital[0]

    # ---- 初始现场：6 阀全关到位 + 急停回路通 + 停止未按（常闭=1）----
    for n in range(6):
        d0.di[2 * n] = 0
        d0.di[2 * n + 1] = 1
    d0.di[cfg.button_items["estop_di"]] = 1
    d0.di[cfg.button_items["stop_di"]] = 1

    # ---- 把现场开起来：阀1 开 + 进水泵 开 + 搅拌泵转 ----
    await business.valve_bank.set_valve(0, True)
    await business.pump_bank.set_pump(0, True)
    await business.vfd.poll(1000)
    business.valve_bank.poll(1000)
    business.pump_bank.poll(1000, cfg.pump_stale_ms)
    check("C0 现场已开起来（阀1 开 + 进水泵开 + 搅拌泵转）",
          bool(d0.do[0]) and bool(d0.do[6])
          and business.vfd.is_running() is True,
          "do=%s running=%s" % (d0.do, business.vfd.is_running()))

    # ---- 手动模式下「在动」⇒ 启动灯亮（端到端：app 汇总 → 按钮组）----
    business.button_station.set_manual_running(business._manual_running())
    business.button_station.poll(1100, auto_mode=False)
    await _settle()
    check("C1 手动模式有设备在动 ⇒ 启动灯亮、停止灯灭",
          bool(d0.do[7]) and not bool(d0.do[8]),
          "do7=%s do8=%s" % (d0.do[7], d0.do[8]))

    # ---- C2 现场按停止（常闭 ⇒ 1→0 下降沿）⇒ 泵停 + 阀全关 + 搅拌泵停 ----
    d0.di[cfg.button_items["stop_di"]] = 0
    business.button_station.poll(1200, auto_mode=False)
    ctl.poll_idle(1200, auto_mode=False)      # 受理停止沿（手动模式）
    await asyncio.sleep(0.1)                  # 全停是异步投递
    check("C2 手动模式按停止：阀全关",
          all(not bool(d0.do[i]) for i in range(6)),
          "do=%s" % (d0.do[:6],))
    check("C3 手动模式按停止：进水泵停", not bool(d0.do[6]),
          "do6=%s" % (d0.do[6],))
    check("C4 手动模式按停止：搅拌泵收到停机命令并停住",
          business.vfd._running is False
          and any(c in (devices_mod.VfdM512.CMD_DECEL_STOP,
                        devices_mod.VfdM512.CMD_FREE_STOP)
                  for c in business.vfd.commands),
          "running=%s commands=%s" % (business.vfd._running,
                                      business.vfd.commands))
    d0.di[cfg.button_items["stop_di"]] = 1    # 松开

    # ---- D 手动模式急停 ⇒ 同样把设备与球阀全停下来 ----
    # 先重新把现场开起来
    await business.valve_bank.set_valve(0, True)
    await business.pump_bank.set_pump(0, True)
    await business.vfd.apply_control(proto.OP_START)
    await business.vfd.poll(2000)
    business.vfd.commands.clear()
    check("D0 急停前：阀1 开 + 进水泵开 + 搅拌泵转",
          bool(d0.do[0]) and bool(d0.do[6])
          and business.vfd.is_running() is True,
          "do=%s running=%s" % (d0.do, business.vfd.is_running()))

    # 急停回路断开（常闭 ⇒ 0）⇒ 锁存 + 全停
    d0.di[cfg.button_items["estop_di"]] = 0
    business.button_station.poll(2100, auto_mode=False)
    ctl.poll_idle(2100, auto_mode=False)
    await asyncio.sleep(0.1)
    check("D1 手动模式急停：阀全关",
          all(not bool(d0.do[i]) for i in range(6)),
          "do=%s" % (d0.do[:6],))
    check("D2 手动模式急停：进水泵停", not bool(d0.do[6]),
          "do6=%s" % (d0.do[6],))
    check("D3 手动模式急停：搅拌泵收到停机命令并停住",
          business.vfd._running is False
          and any(c in (devices_mod.VfdM512.CMD_DECEL_STOP,
                        devices_mod.VfdM512.CMD_FREE_STOP)
                  for c in business.vfd.commands),
          "running=%s commands=%s" % (business.vfd._running,
                                      business.vfd.commands))
    check("D4 手动模式急停：已锁存（复位前不能启动）",
          ctl.estop_latched is True, "latched=%s" % (ctl.estop_latched,))


# ============================================================================
#                    E/F 切模式全停 + 切自动不启动
# ============================================================================

async def test_mode_switch_stops_devices():
    """E/F 切模式 ⇒ 全停；切自动不启动控制器。"""
    print("=== E/F 切模式 => 关闭所有设备（双向）===")
    basedir = tempfile.mkdtemp(prefix="hsf_mode_")
    log = []
    try:
        cfg = cfgmod.Config()
        cfg.resolve_paths(basedir)
        cfg.config_path = os.path.join(basedir, "cfg", "x.json")
        cfg.users_path = os.path.join(basedir, "cfg", "hsf_users.json")
        os.makedirs(os.path.join(basedir, "cfg"), exist_ok=True)
        cfg.port = 0
        cfg.enable_serial = False
        users = auth_mod.UserStore(cfg.users_path)
        users.create_defaults()
        business = build_fake_app(cfg, log=log.append)

        stop_cmds = (devices_mod.VfdM512.CMD_DECEL_STOP,
                     devices_mod.VfdM512.CMD_FREE_STOP)

        # ---- 手动 -> 自动：全停 + 控制器不启动 ----
        business.vfd.commands.clear()
        ok = business.set_run_mode(business.MODE_AUTO)
        await _settle()
        check("E1 切到自动：命令受理", ok)
        check("E2 切到自动：变频器收到停机命令",
              any(c in stop_cmds for c in business.vfd.commands),
              "commands=%s" % (business.vfd.commands,))
        check("E3 切到自动：控制器**未**启动（要人按启动）",
              not business.is_auto_running(),
              "autoRunning=%s" % (business.is_auto_running(),))
        check("E4 切到自动：日志写明「设备全停，等待人工启动自动运行」",
              any("切到自动模式：设备全停" in line for line in log),
              "log=%s" % (log[-3:],))

        # ---- 自动 -> 手动：同样全停 ----
        business.vfd.commands.clear()
        log.clear()
        ok = business.set_run_mode(business.MODE_MANUAL)
        await _settle()
        check("E5 切回手动：命令受理", ok)
        check("E6 切回手动：变频器收到停机命令",
              any(c in stop_cmds for c in business.vfd.commands),
              "commands=%s" % (business.vfd.commands,))
        check("E7 切回手动：控制器已停",
              not business.is_auto_running(),
              "autoRunning=%s" % (business.is_auto_running(),))

        # ---- 幂等重放：不许把人家手动开的设备停掉 ----
        business.vfd.commands.clear()
        business.set_run_mode(business.MODE_MANUAL)
        await _settle()
        check("E8 同一模式重放：不发停机命令（手动模式下设备归人）",
              not business.vfd.commands,
              "commands=%s" % (business.vfd.commands,))
    finally:
        shutil.rmtree(basedir, ignore_errors=True)


async def _run():
    await test_lamp_writing_by_mode()
    await test_manual_running_predicate()
    basedir = tempfile.mkdtemp(prefix="hsf_manual_")
    log = []
    try:
        await _case_manual_stop_estop(basedir, log)
    finally:
        shutil.rmtree(basedir, ignore_errors=True)
    await test_mode_switch_stops_devices()


def main():
    asyncio.run(_run())
    print("=" * 62)
    print("  汇总：PASS %d / FAIL %d" % (PASS, FAIL))
    return 0 if FAIL == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
