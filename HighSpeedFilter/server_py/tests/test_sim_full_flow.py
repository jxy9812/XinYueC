# -*- coding: utf-8 -*-
"""控制全流程模拟测试（2026-11-12 用户定案「做模拟测试吗，全部控制模式
全部流程，期间还要有急停(急停后要显示复位才可以在启动)、停止 等各种
因素」）。

[测什么] **真实全栈模拟器**（`cfg.sim_enabled=True` + `build_devices()`
虚拟设备 + 真 XLink 协议 + 真客户端）跑完整流程，逐个因素都覆盖：

    A. 单模式（产水）：手动模式启动被拒 → 切自动 → 启动 → 阀相到位 →
       泵相 → 运行 → 停止全停。
    B. **急停**：运行中按急停 ⇒ 控制器停 + 设备全停 + **锁存** + 报警帧；
       **急停后启动必须被拒**（提示要复位）⇒ 复位后才可再启动。
    C. **报警锁存**（阀超时）：卡阀启动 ⇒ 报警 + 停止 + 锁存 ⇒ 未复位启动
       被拒 ⇒ 复位后可启动。
    D. **组合模式（自动循环）**：产水 →（压差）反洗；停止后**记住阶段** ⇒
       再启动从那一步继续；复位 ⇒ 下次从头。
    E. **权限**：非管理员不能启动/停止/复位（权限闸在服务端）。
    F. **模式切换**：切回手动 ⇒ 全停；切自动**不**自动启动控制器。

[怎么跑]
    python -I tests/test_sim_full_flow.py          # 独立跑
    pytest tests/test_sim_full_flow.py -q          # pytest 收集
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
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.dirname(HERE))

from hsfsrv import auth as auth_mod      # noqa: E402
from hsfsrv import bson, config as cfgmod  # noqa: E402
from hsfsrv import proto                 # noqa: E402
from hsfsrv import xlink                 # noqa: E402
from hsfsrv import app as app_mod        # noqa: E402
from test_vfd_ops import Client          # noqa: E402

PASS = 0
FAIL = 0
TRACE = []


def check(name, ok, detail=""):
    global PASS, FAIL
    if ok:
        PASS += 1
        print("  [PASS] %s %s" % (name, detail))
    else:
        FAIL += 1
        print("  [FAIL] %s %s" % (name, detail))


def step(msg):
    """流程节点（打印出来便于人读过程）。"""
    TRACE.append(msg)
    print("    · %s" % msg)


# ============================================================================
#                        服务端装配（真实全栈 sim）
# ============================================================================

async def _spin(basedir, log, move_ms=250, timeout_ms=800,
                estop_normal=True):
    """起一份真实 sim 服务端（虚拟设备 + 真协议栈）。

    :param move_ms: 阀行程（ms）；调大可造"卡阀"（到位慢于超时）。
    :param timeout_ms: 阀动作超时（ms）。
    """
    cfg = cfgmod.Config()
    cfg.resolve_paths(basedir)
    cfg.config_path = os.path.join(basedir, "cfg", "x.json")
    cfg.users_path = os.path.join(basedir, "cfg", "hsf_users.json")
    os.makedirs(os.path.join(basedir, "cfg"), exist_ok=True)
    cfg.port = 0
    cfg.sim_enabled = True
    cfg.sim_valve_move_ms = move_ms
    cfg.valve_move_timeout_ms = timeout_ms
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
    # 急停回路初始通（常闭未按下）
    _set_estop(business, estop_normal)
    await asyncio.sleep(0.3)
    return cfg, business, server


def _digital_model(business):
    """取虚拟数字量模型（急停/行程模拟的入口）。

    [路径] DigitalIo._master 是 VirtualMaster，其 .model 才是
    VirtualDigital（模型对象）；master._model 不存在。
    """
    master = business.digital[0]._master
    return getattr(master, "model", None)


def _set_estop(business, normal):
    """模拟急停回路（normal=False = 急停被按下）。"""
    _digital_model(business).set_estop(bool(normal))


async def _main_flow(base, log, **kw):
    """在服务端 loop 里跑主流程（客户端在子线程，避免阻塞 tick）。"""
    cfg, business, server = await _spin(base, log, **kw)
    errors = []

    def scenario():
        try:
            _client_scenarios(business, server, errors)
        except Exception as exc:                    # noqa: BLE001
            import traceback
            errors.append("%s\n%s" % (exc, traceback.format_exc()))

    thread = threading.Thread(target=scenario, name="flow-client")
    thread.start()
    try:
        while thread.is_alive():
            await asyncio.sleep(0.05)
    finally:
        thread.join()
        await server.stop()
    if errors:
        raise RuntimeError(errors[0])


def _client_scenarios(business, server, errors):
    """客户端侧的全部场景（顺序执行，每个场景自成一段）。"""
    c = Client("127.0.0.1", server.port)
    c.hello()
    ok, _role = c.login("root", "root")
    assert ok, "root 登录失败"

    def q():
        """查一次状态（兼保活：裸 socket 客户端没心跳，6s 静默会被断）。"""
        r = c.request(proto.CMD_RUN_MODE, b"")
        return r[1] if r else {}

    def cfg_auto(modes=None, cycle=None):
        doc = {"section": proto.CFG_SECTION_AUTO}
        if modes:
            doc["modes"] = modes
        if cycle:
            doc["cycle"] = cycle
        r = c.request(proto.CMD_CONFIG_SET, bson.encode(doc))
        assert r and r[1].get("ok") is True, "配置失败：%r" % (r[1] if r else None)
        return r[1]

    def start(mode=None, wait_running=False, timeout=20.0):
        doc = {"on": True}
        if mode:
            doc[proto.KEY_AUTO_MODE] = mode
        r = c.request(proto.CMD_AUTO_CONTROL, bson.encode(doc))
        assert r is not None, "启动无应答"
        if wait_running:
            # [等的是**运行相**不是 autoRunning] autoRunning = 控制器在跑
            #（启动那一刻就是 True）；设备真正跑起来要看 autoPhase == running
            # （阀相→泵相→运行相）。只等 autoRunning 会读到"还在阀相"的
            # 快照 ⇒ 断言泵已启动时偶发红（竞态）。
            t0 = time.time()
            while time.time() - t0 < timeout:
                st = q()
                if (st.get(proto.KEY_AUTO_RUNNING)
                        and st.get(proto.KEY_AUTO_PHASE) == "running"):
                    return st
                time.sleep(0.2)
        return r[1]

    def stop():
        r = c.request(proto.CMD_AUTO_CONTROL, bson.encode({"on": False}))
        return r[1] if r else {}

    def reset():
        r = c.request(proto.CMD_AUTO_CONTROL,
                      bson.encode({"op": proto.AUTO_OP_RESET}))
        return r[1] if r else {}

    def vfd():
        return business.vfd

    def pump_digital():
        """在线程侧拿数字量缓存（DO 位图）。"""
        dev = business.digital[0]
        return list(dev.do)

    # ---------------- 配置三个模式（循环前置）----------------
    cfg_auto(modes={
        "produce": {"devices": ["valve1", "stirPump", "inletPump"],
                    "stirRpm": 720, "stirReverse": False},
        "backwash": {"devices": ["valve2", "valve3", "stirPump"],
                     "stirRpm": 720, "stirReverse": True},
        "rinse": {"devices": ["valve4", "inletPump"],
                  "stirRpm": 720, "stirReverse": False}})
    step("三个模式配置完成")

    # ================= A. 单模式：产水 全流程 =================
    step("A1 手动模式下启动 ⇒ 应被拒")
    r = c.request(proto.CMD_AUTO_CONTROL,
                  bson.encode({"on": True, proto.KEY_AUTO_MODE: "produce"}))
    assert r and r[1].get("ok") is False, "手动模式启动应被拒"
    assert "手动" in str(r[1].get("err", "")), r[1]

    step("A2 切到自动 ⇒ 控制器不应自动启动")
    r = c.request(proto.CMD_RUN_MODE, bson.encode({"mode": 1}))
    assert r and r[1].get("ok") is True
    st = q()
    check("A2a 切自动不自动启动控制器",
          st.get(proto.KEY_AUTO_RUNNING) is False, "%r" % st)

    step("A3 启动产水模式 ⇒ 阀相 → 到位 → 泵相 → 运行")
    st = start(mode="produce", wait_running=True)
    check("A3a 产水模式跑到运行相",
          st.get(proto.KEY_AUTO_RUNNING) is True, "%r" % st)
    check("A3b 搅拌泵已运行",
          bool(vfd().is_running()), "running=%s" % vfd().is_running())
    check("A3c 进水泵 DO6 已开",
          pump_digital()[6] == 1, "%s" % pump_digital())
    check("A3d 阀1 的 DO 已开",
          pump_digital()[0] == 1, "%s" % pump_digital())

    step("A4 停止 ⇒ 全停（阀关、泵停、变频器停）")
    stop()
    time.sleep(1.5)
    check("A4a 停止后变频器停", not vfd().is_running())
    check("A4b 停止后进水泵关", pump_digital()[6] == 0,
          "%s" % pump_digital())

    # ================= B. 急停 =================
    step("B1 重新启动产水模式")
    start(mode="produce", wait_running=True)
    step("B2 运行中『按下急停』（急停回路断开）")
    _set_estop(business, False)
    # 等服务端采到 DI12=0（debounce 1ms + 轮询 200ms）
    t0 = time.time()
    estoped = False
    while time.time() - t0 < 8.0:
        st = q()
        if st.get(proto.KEY_AUTO_PHASE) == "estop":
            estoped = True
            break
        time.sleep(0.2)
    check("B2a 急停后控制器停 + 进入急停态", estoped, "%r" % (q(),))
    time.sleep(1.0)
    check("B2b 急停后设备全停（变频器停、泵关）",
          not vfd().is_running() and pump_digital()[6] == 0,
          "vfd=%s do=%s" % (vfd().is_running(), pump_digital()))

    step("B3 急停后按启动 ⇒ 必须被拒（要复位）")
    r = c.request(proto.CMD_AUTO_CONTROL,
                  bson.encode({"on": True, proto.KEY_AUTO_MODE: "produce"}))
    assert r and r[1].get("ok") is False, "急停未复位不该能启动"
    check("B3a 急停未复位启动被拒且提示复位",
          "复位" in str(r[1].get("err", "")), r[1])

    step("B4 复位急停 ⇒ 松开急停回路 ⇒ 再启动")
    _set_estop(business, True)          # 松开急停（现场复位后回路恢复）
    # [必须等一拍让 DI 缓存刷新] 按钮站读的是**数字量模块的缓存**，虚拟
    # 模型改了也要等下一拍 poll 才反映上来。急着复位的话，服务端这一拍
    # 仍看到"急停还按着" ⇒ _check_estop 立刻重新锁存（这本身是对的：急停
    # 没松开就不该能启动）。
    time.sleep(1.2)
    reset()
    time.sleep(0.5)
    st = q()
    check("B4a 复位后不在急停态",
          st.get(proto.KEY_AUTO_PHASE) != "estop", "%r" % st)
    r = c.request(proto.CMD_AUTO_CONTROL,
                  bson.encode({"on": True, proto.KEY_AUTO_MODE: "produce"}))
    check("B4b 复位后可以启动", r and r[1].get("ok") is True, r[1] if r else None)
    stop()
    time.sleep(1.0)

    # ================= C. 报警锁存（阀超时）=================
    step("C1 制造卡阀（阀2 行程 3s > 超时 0.8s）⇒ 启动应报警并锁存")
    # 把模拟行程调大（阀到位慢于超时 ⇒ 阀层判超时）
    model = _digital_model(business)
    old_move = model.move_ms
    model.move_ms = 3000.0
    r = c.request(proto.CMD_AUTO_CONTROL,
                  bson.encode({"on": True, proto.KEY_AUTO_MODE: "produce"}))
    assert r and r[1].get("ok") is True, "应能启动：%r" % (r[1] if r else None)
    t0 = time.time()
    faulted = False
    while time.time() - t0 < 12.0:
        st = q()
        if st.get(proto.KEY_AUTO_FAULT):
            faulted = True
            break
        time.sleep(0.2)
    check("C1a 阀超时 ⇒ 报警锁存（autoFault）", faulted, "%r" % (q(),))
    model.move_ms = old_move

    step("C2 报警未复位 ⇒ 启动被拒")
    r = c.request(proto.CMD_AUTO_CONTROL,
                  bson.encode({"on": True, proto.KEY_AUTO_MODE: "produce"}))
    assert r and r[1].get("ok") is False, "报警未复位不该能启动"
    check("C2a 报警未复位启动被拒且提示复位",
          "复位" in str(r[1].get("err", "")), r[1])

    step("C3 复位 ⇒ 可再启动")
    reset()
    st = q()
    check("C3a 复位后报警锁存解除",
          st.get(proto.KEY_AUTO_FAULT) is False, "%r" % st)
    r = c.request(proto.CMD_AUTO_CONTROL,
                  bson.encode({"on": True, proto.KEY_AUTO_MODE: "produce"}))
    check("C3b 复位后可以启动", r and r[1].get("ok") is True, r[1] if r else None)
    stop()
    time.sleep(1.0)

    # ================= D. 组合模式（自动循环）+ 阶段记忆 =================
    # [2026-11-18 配置化] 参与循环的模式与顺序由**配置**决定 ⇒ 这里的
    # 期望值一律**从配置列表算**（不再写死「产水→反洗」），并显式下发
    # cycleModes（把"参与哪些模式"也纳入这条链路的覆盖面）。
    stages = list(proto.AUTO_CYCLE_MODES_DEF)
    first_stage = stages[0]
    next_stage = stages[(stages.index(first_stage) + 1) % len(stages)]
    step("D1 配置循环（压差 0.4 大于切；兜底关；反洗/顺洗 30 分钟；"
         "参与列表＝%s）" % "→".join(stages))
    cfg_auto(cycle={"enabled": True, "pressThreshold": 0.4,
                     "pressCompare": "gt", "timerFallback": False,
                     "timerMinutes": 30, "backwashMinutes": 30,
                     "rinseMinutes": 30,
                     proto.CFG_AUTO_CYCLE_MODES: stages})
    step("D2 启动组合模式 ⇒ 应从参与列表的第一个[%s]开始" % first_stage)
    r = c.request(proto.CMD_AUTO_CONTROL,
                  bson.encode({"on": True, proto.KEY_AUTO_MODE: proto.AUTO_MODE_AUTO}))
    assert r and r[1].get("ok") is True, r[1] if r else None
    check("D2a 组合模式起始阶段 = 参与列表第一个[%s]" % first_stage,
          r[1].get(proto.KEY_AUTO_MODE) == first_stage, r[1])
    step("D3 等压差触发切到下一个阶段")
    t0 = time.time()
    stage = first_stage
    while time.time() - t0 < 25.0:
        st = q()
        stage = st.get(proto.KEY_AUTO_MODE)
        if stage != first_stage:
            break
        time.sleep(0.3)
    check("D3a 压差达标后切到下一个阶段[%s]" % next_stage,
          stage == next_stage, stage)
    step("D4 停止（记住阶段）⇒ 再启动应从记住的阶段继续")
    stop()
    time.sleep(0.5)
    st = q()
    check("D4a 停止后服务端仍报着记住的阶段（%s）" % next_stage,
          st.get(proto.KEY_AUTO_MODE) == next_stage, "%r" % st)
    r = c.request(proto.CMD_AUTO_CONTROL,
                  bson.encode({"on": True, proto.KEY_AUTO_MODE: proto.AUTO_MODE_AUTO}))
    check("D4b 再启动从记住的阶段继续（%s）" % next_stage,
          r and r[1].get(proto.KEY_AUTO_MODE) == next_stage,
          r[1] if r else None)
    step("D5 复位 ⇒ 下次从头（参与列表第一个）")
    stop()
    time.sleep(0.5)
    reset()
    r = c.request(proto.CMD_AUTO_CONTROL,
                  bson.encode({"on": True, proto.KEY_AUTO_MODE: proto.AUTO_MODE_AUTO}))
    check("D5a 复位后再启动从头开始（%s）" % first_stage,
          r and r[1].get(proto.KEY_AUTO_MODE) == first_stage,
          r[1] if r else None)
    stop()
    time.sleep(1.0)

    # ================= E. 权限（非管理员不能控制）=================
    step("E1 普通用户（admin=只读）登录 ⇒ 启动/停止/复位都应被拒")
    c2 = Client("127.0.0.1", server.port)
    c2.hello()
    ok2, _r2 = c2.login("admin", "admin")
    if not ok2:
        step("E1a 跳过：admin 账户不存在")
    else:
        r = c2.request(proto.CMD_AUTO_CONTROL,
                       bson.encode({"on": True,
                                    proto.KEY_AUTO_MODE: "produce"}))
        check("E1b 普通用户启动被拒",
              r is not None and r[1].get("ok") is False,
              r[1] if r else None)
        r = c2.request(proto.CMD_AUTO_CONTROL,
                       bson.encode({"op": proto.AUTO_OP_RESET}))
        check("E1c 普通用户复位被拒",
              r is not None and r[1].get("ok") is False,
              r[1] if r else None)
    c2.close()

    # ================= F. 模式切换 =================
    step("F1 运行中切回手动 ⇒ 应全停且控制器停")
    start(mode="produce", wait_running=True)
    r = c.request(proto.CMD_RUN_MODE, bson.encode({"mode": 0}))
    assert r and r[1].get("ok") is True
    time.sleep(1.2)
    st = q()
    check("F1a 切回手动后控制器停",
          st.get(proto.KEY_AUTO_RUNNING) is False, "%r" % st)
    check("F1b 切回手动后设备全停",
          not vfd().is_running() and pump_digital()[6] == 0,
          "vfd=%s do=%s" % (vfd().is_running(), pump_digital()))

    c.close()


# ============================================================================
#                          各场景入口（pytest 友好）
# ============================================================================

def test_valve1_only_cycle():
    """**只配阀 1**（现场现状口径：板上只有 1 号阀接线）⇒ 未选阀（阀2~6）
    发关命令并**等待关到位**（sim 全阀反馈齐全 ⇒ 关位立即达标）→ 泵相 →
    运行。锁定用户口径「未选的阀是打到关闭状态是要等待的」（2026-11-13）；
    板上未接线的阀给不出关到位 ⇒ 超时报警=保护行为（用本用例在桌面 sim
    验证**完整逻辑**）。"""
    base = tempfile.mkdtemp(prefix="hsf_v1only_")
    try:
        asyncio.run(_case_valve1_only(base))
    finally:
        shutil.rmtree(base, ignore_errors=True)


async def _case_valve1_only(base):
    cfg, business, server = await _spin(base, [])
    errors = []

    def scenario():
        try:
            import time as _time
            c = Client("127.0.0.1", server.port)
            c.hello()
            c.login("root", "root")
            # 切自动 + 只配阀 1（+搅拌泵/进水泵）——阀 2~6 **不在** devices。
            r = c.request(proto.CMD_RUN_MODE, bson.encode({"mode": 1}))
            assert r and r[1].get("ok") is True, \
                "切自动应受理：%r" % (r[1] if r else None)
            r = c.request(proto.CMD_CONFIG_SET, bson.encode({
                "section": proto.CFG_SECTION_AUTO,
                "modes": {"produce": {"devices": ["valve1", "stirPump",
                                                  "inletPump"],
                                      "stirRpm": 720}}}))
            assert r and r[1].get("ok") is True, "只配阀 1 的配置应受理"

            # 启动：未选阀 2~6 发关命令并**等待关到位**（sim 反馈齐全，
            # 关位立即达标）→ 阀相通过 → 泵相 → 运行。
            r = c.request(proto.CMD_AUTO_CONTROL, bson.encode(
                {"on": True, proto.KEY_AUTO_MODE: "produce"}))
            assert r and r[1].get("ok") is True, "启动应受理"
            t0 = _time.time()
            running = False
            st = {}
            while _time.time() - t0 < 15.0:
                r = c.request(proto.CMD_RUN_MODE, b"")
                st = r[1]
                if st.get(proto.KEY_AUTO_PHASE) == "running":
                    running = True
                    break
                _time.sleep(0.3)
            check("G1 只配阀 1 启动进运行（未选阀关到位等待通过）", running,
                  "%r" % st)

            # 未选阀保持关闭（DO1~5=0）、选中阀 1 打开（DO0=1）。
            do = list(business.digital[0].do)
            check("G2 选中阀 1 DO0 已开", do[0] == 1, str(do[:6]))
            check("G3 未选阀 2~6 DO1~5 保持关", all(do[i] == 0
                                                    for i in range(1, 6)),
                  str(do[:6]))
            # 停止收尾（全停）。
            r = c.request(proto.CMD_AUTO_CONTROL, bson.encode({"on": False}))
            assert r and r[1].get("ok") is True, "停止应受理"
        except Exception as exc:                        # noqa: BLE001
            import traceback
            errors.append("%s\n%s" % (exc, traceback.format_exc()))

    thread = threading.Thread(target=scenario, name="v1only-client")
    thread.start()
    try:
        while thread.is_alive():
            await asyncio.sleep(0.05)
    finally:
        thread.join()
        await server.stop()
    if errors:
        raise RuntimeError(errors[0])


def test_full_flow_all_modes():
    base = tempfile.mkdtemp(prefix="hsf_fullflow_")
    try:
        asyncio.run(_main_flow(base, []))
    finally:
        shutil.rmtree(base, ignore_errors=True)


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
    print("\n===== 控制全流程模拟: %d failed（PASS %d / FAIL %d）====="
          % (len(failed), PASS, FAIL))
    return 1 if failed or FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
