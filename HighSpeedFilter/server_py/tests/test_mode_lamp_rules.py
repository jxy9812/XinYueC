# -*- coding: utf-8 -*-
"""模式规则回归：手动不写灯 + 切模式全停（2026-11-10 用户口径）。

[测什么] 三条被用户明确要求的运行逻辑，逐条锁死：

    1. **手动模式不跑自动程序 => 按钮组灯归人工**：手动模式下 poll()
       一个 DO 都不许写。旧实现每拍写灯，人工点的灯被自动逻辑覆盖回去
       （现场原话「手动模式下无法单独控制按钮灯」）。自动模式才让灯跟随
       自动运行态。
    2. **切换模式 => 关闭所有设备**（手动<->自动**双向**）：切到自动也要全停
       ——自动模式拒掉全部远程控制、控制器又要等人按"启动"才跑，设备若还
       转着，现场既停不掉也看不到谁在驱动它。
    3. **只有按了启动按钮后设备才允许工作**：切自动**不**启动控制器
       （模式开关不是启动按钮），自动运行位保持"已停止"。

[怎么跑]
    python3 -I tests/test_mode_lamp_rules.py          # 独立跑
    pytest tests/test_mode_lamp_rules.py -q           # pytest 收集

服务端在本进程内起（假设备 + 真业务层）。
"""

import asyncio
import os
import shutil
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.dirname(HERE))

from hsfsrv import auth as auth_mod          # noqa: E402
from hsfsrv import buttons as buttons_mod    # noqa: E402
from hsfsrv import config as cfgmod          # noqa: E402
from hsfsrv import devices as devices_mod    # noqa: E402
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


async def test_lamp_writing_by_mode():
    """① 手动模式不写灯 / 自动模式写灯。"""
    print("=== A 灯的归属：手动=人工，自动=跟随 ===")
    # 急停常闭且正常（通=1）；启动/复位常开未按=0；停止常闭未按=1
    ctx = FakeCtx(di={12: True, 13: False, 14: True, 15: False})
    st = buttons_mod.ButtonStation(ctx, _station_items())

    # ---- 手动模式：一拍过去，一个 DO 都不许写 ----
    st.poll(1000, auto_mode=False)
    await _settle()
    check("手动模式：poll 后没有任何 DO 写", not ctx.writes,
          "writes=%s" % (ctx.writes,))
    # 采集与状态推导照做（界面要显示触点/灯态）
    check("手动模式：仍采集到急停正常", st.estop_ok is True,
          "estop_ok=%s" % (st.estop_ok,))

    # ---- 自动模式：灯跟随运行态（未运行 => 停止灯亮）----
    ctx2 = FakeCtx(di={12: True, 13: False, 14: True, 15: False})
    st2 = buttons_mod.ButtonStation(ctx2, _station_items())
    st2.poll(1000, auto_mode=True)
    await _settle()
    wrote = dict((ch, on) for ch, on, _m in ctx2.writes)
    check("自动模式：写灯（停止灯亮、启动灯灭）",
          wrote.get(8) is True and wrote.get(7) is False,
          "writes=%s" % (ctx2.writes,))

    # ---- 自动模式 + 运行中 => 启动灯亮 ----
    ctx3 = FakeCtx(di={12: True, 13: False, 14: True, 15: False})
    st3 = buttons_mod.ButtonStation(ctx3, _station_items())
    st3.set_running(True)
    st3.poll(2000, auto_mode=True)
    await _settle()
    wrote = dict((ch, on) for ch, on, _m in ctx3.writes)
    check("自动模式运行中：启动灯亮、停止灯灭",
          wrote.get(7) is True and wrote.get(8) is False,
          "writes=%s" % (ctx3.writes,))


async def test_mode_switch_stops_devices():
    """②③ 切模式 => 全停；切自动不启动控制器。"""
    print("=== B 切模式 => 关闭所有设备（双向）===")
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
        check("切到自动：命令受理", ok)
        check("切到自动：变频器收到停机命令",
              any(c in stop_cmds for c in business.vfd.commands),
              "commands=%s" % (business.vfd.commands,))
        check("切到自动：控制器**未**启动（要人按启动）",
              not business.is_auto_running(),
              "autoRunning=%s" % (business.is_auto_running(),))
        check("切到自动：日志写明「设备全停，等待人工启动自动运行」",
              any("切到自动模式：设备全停" in line for line in log),
              "log=%s" % (log[-3:],))

        # ---- 自动 -> 手动：同样全停 ----
        business.vfd.commands.clear()
        log.clear()
        ok = business.set_run_mode(business.MODE_MANUAL)
        await _settle()
        check("切回手动：命令受理", ok)
        check("切回手动：变频器收到停机命令",
              any(c in stop_cmds for c in business.vfd.commands),
              "commands=%s" % (business.vfd.commands,))
        check("切回手动：控制器已停",
              not business.is_auto_running(),
              "autoRunning=%s" % (business.is_auto_running(),))

        # ---- 幂等重放：不许把人家手动开的设备停掉 ----
        business.vfd.commands.clear()
        business.set_run_mode(business.MODE_MANUAL)
        await _settle()
        check("同一模式重放：不发停机命令（手动模式下设备归人）",
              not business.vfd.commands,
              "commands=%s" % (business.vfd.commands,))
    finally:
        shutil.rmtree(basedir, ignore_errors=True)


async def _run():
    await test_lamp_writing_by_mode()
    await test_mode_switch_stops_devices()


def main():
    asyncio.run(_run())
    print("=" * 62)
    print("  汇总：PASS %d / FAIL %d" % (PASS, FAIL))
    return 0 if FAIL == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
