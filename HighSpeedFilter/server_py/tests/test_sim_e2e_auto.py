# -*- coding: utf-8 -*-
"""三模式自动序列 **模拟器端到端**（2026-11-11 用户口径「桌面用模拟器的
方式测试完整，无泄漏，无崩溃」）。

[与 test_auto_modes.py 的分工] 那边用假设备 + 手装阀/泵组，锁**序列判定
逻辑**；本文件起**真实全栈**：`cfg.sim_enabled=True` → build_devices()
（VirtualMaster/VirtualVfd/VirtualDigital 全真装配，阀有 10s 级行程模拟，
可配 `sim_valve_move_ms`）+ start_polling + 真协议客户端走 socket ——
也就是桌面 `python -m hsfsrv --sim` 的同一条路。

[测什么]
    ① 卡阀场景（sim_valve_move_ms > 阀超时 ⇒ 永远不到位）：启动 → 收
       AUTO_ALARM_VALVE_TIMEOUT(5) 帧 → autoFault 锁存 → 复位解锁；
    ② 产水模式完整循环 ×N：配置 SET/GET → 切自动 → 启动（阀相：行程
       模拟走到位）→ 泵相（变频器 720rpm 反转 + 进水泵 DO6）→ 运行 →
       停止全停；循环期间无非预期告警帧；
    ③ 无泄漏/无崩溃：进程存活、asyncio 任务数不增长、进程 RSS 增长有界
       （暖机一轮后取基线，比较尾轮）。

[怎么跑]
    python -I tests/test_sim_e2e_auto.py          # 独立跑
    pytest tests/test_sim_e2e_auto.py -q          # pytest 收集
"""

import asyncio
import ctypes
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

from hsfsrv import auth as auth_mod          # noqa: E402
from hsfsrv import bson, config as cfgmod    # noqa: E402
from hsfsrv import proto, xlink              # noqa: E402
from hsfsrv import app as app_mod            # noqa: E402
from test_vfd_ops import Client              # noqa: E402

PASS = 0
FAIL = 0
CYCLES = 4                     # 完整启停循环轮数（泄漏/崩溃观察窗）
RSS_GROWTH_LIMIT_MB = 12.0     # 循环间 RSS 增长上限（暖机基线之后）


def check(name, ok, detail=""):
    global PASS, FAIL
    if ok:
        PASS += 1
        print("  [PASS] %s %s" % (name, detail))
    else:
        FAIL += 1
        print("  [FAIL] %s %s" % (name, detail))


def _rss_mb():
    """本进程 RSS（MB；Windows psapi / Linux /proc，读不到返回 None）。"""
    try:
        if os.name == "nt":
            class _PMC(ctypes.Structure):
                _fields_ = [("cb", ctypes.c_ulong),
                            ("PageFaultCount", ctypes.c_ulong),
                            ("PeakWorkingSetSize", ctypes.c_size_t),
                            ("WorkingSetSize", ctypes.c_size_t),
                            ("QuotaPeakPagedPoolUsage", ctypes.c_size_t),
                            ("QuotaPagedPoolUsage", ctypes.c_size_t),
                            ("QuotaPeakNonPagedPoolUsage", ctypes.c_size_t),
                            ("QuotaNonPagedPoolUsage", ctypes.c_size_t),
                            ("PagefileUsage", ctypes.c_size_t),
                            ("PeakPagefileUsage", ctypes.c_size_t)]

            pmc = _PMC()
            pmc.cb = ctypes.sizeof(_PMC)
            # [句柄必须是 void_p] GetCurrentProcess 缺省 restype=c_int，
            # 伪句柄 -1 被当 int 传 → GetProcessMemoryInfo 报句柄无效（实测）。
            k32 = ctypes.windll.kernel32
            k32.GetCurrentProcess.restype = ctypes.c_void_p
            handle = k32.GetCurrentProcess()
            k32.K32GetProcessMemoryInfo.restype = ctypes.c_int
            k32.K32GetProcessMemoryInfo.argtypes = [
                ctypes.c_void_p, ctypes.POINTER(_PMC), ctypes.c_ulong]
            if k32.K32GetProcessMemoryInfo(handle, ctypes.byref(pmc), pmc.cb):
                return pmc.WorkingSetSize / (1024.0 * 1024.0)
            return None
        with open("/proc/self/status", "r", encoding="utf-8") as fh:
            for line in fh:
                if line.startswith("VmRSS:"):
                    return int(line.split()[1]) / 1024.0
        return None
    except Exception:                                   # noqa: BLE001
        return None


async def _spin(basedir, log, move_ms=250, valve_timeout_ms=800):
    """起一份**真实装配**的 sim 服务端（虚拟阀行程可配）。"""
    cfg = cfgmod.Config()
    cfg.resolve_paths(basedir)
    cfg.config_path = os.path.join(basedir, "cfg", "x.json")
    cfg.users_path = os.path.join(basedir, "cfg", "hsf_users.json")
    os.makedirs(os.path.join(basedir, "cfg"), exist_ok=True)
    cfg.port = 0
    cfg.sim_enabled = True                 # 虚拟设备（真实业务层全跑）
    cfg.sim_valve_move_ms = move_ms        # 阀行程模拟时长
    cfg.valve_move_timeout_ms = valve_timeout_ms
    cfg.button_debounce_ms = 1
    cfg.storage.enabled = False            # e2e 不落盘（存储有独立段落）
    users = auth_mod.UserStore(cfg.users_path)
    users.create_defaults()
    business = app_mod.App(cfg, log=log)
    business.build_devices()               # Virtual* 全真装配（同 __main__）
    server = xlink.Server(cfg.port, users, log=log, require_auth=True)
    business.attach(server)
    await server.start()
    await business.start_polling()
    return cfg, business, server


def _subscribe(client, topic):
    client._send(proto.MSGCLASS_SUBSCRIBE, topic & 0xFFFF,
                 proto.DATATYPE_RAW, proto.pack_topic(topic))


def _topic_docs(client, topic):
    return [doc for (t, doc, _ts) in client.publishes if t == topic]


def _wait_phase(client, phase, timeout_s=10.0):
    """轮询 RUN_MODE 直到指定相位（客户端 pump 兼收推送帧）。"""
    deadline = time.time() + timeout_s
    state = {}
    while time.time() < deadline:
        state = _query_state(client)
        if state.get(proto.KEY_AUTO_PHASE) == phase:
            return state
        time.sleep(0.05)
    return state


def _query_state(client):
    r = client.request(proto.CMD_RUN_MODE, b"")
    return r[1] if r else {}


def _alarm_frames(client):
    return [doc for (t, doc, _ts) in client.publishes
            if t == proto.TOPIC_AUTO_ALARM]


# ---------------------------------------------------------------------------
#      场景一：卡阀（sim 行程 3s > 阀超时 0.8s）⇒ 超时告警 + 锁存 + 复位
# ---------------------------------------------------------------------------

def test_sim_valve_timeout_alarm():
    base = tempfile.mkdtemp(prefix="hsf_sime2e_to_")
    try:
        asyncio.run(_case_timeout(base))
    finally:
        shutil.rmtree(base, ignore_errors=True)


async def _case_timeout(basedir):
    log = []
    cfg, business, server = await _spin(basedir, log.append, move_ms=3000,
                                        valve_timeout_ms=800)
    errors = []

    def scenario():
        try:
            c = Client("127.0.0.1", server.port)
            c.hello()
            assert c.login("root", "root")[0]
            _subscribe(c, proto.TOPIC_AUTO_ALARM)
            c.request(proto.CMD_CONFIG_SET, bson.encode({
                "section": proto.CFG_SECTION_AUTO,
                "modes": {"produce": {"devices": ["valve1", "valve2"],
                                      "stirRpm": 1440}}}))
            assert _query_state(c)  # 连通性
            r = c.request(proto.CMD_RUN_MODE, bson.encode({"mode": 1}))
            assert r[1].get("ok") is True
            r = c.request(proto.CMD_AUTO_CONTROL, bson.encode(
                {"on": True, proto.KEY_AUTO_MODE: "produce"}))
            assert r and r[1].get("ok") is True, "启动应受理：%r" % (r[1],)
            # 阀行程 3s > 阀超时 0.8s ⇒ 到不了位 ⇒ 序列超时告警（+宽限 2s）
            deadline = time.time() + 8.0
            frames = []
            while time.time() < deadline:
                frames = [f for f in _alarm_frames(c)
                          if f.get("kind") == proto.AUTO_ALARM_VALVE_TIMEOUT]
                if frames:
                    break
                c.pump(0.05)
            assert frames, "应收到阀超时告警帧：%r" % (_alarm_frames(c),)
            st = _query_state(c)
            check("sim 卡阀：autoFault 锁存",
                  st.get(proto.KEY_AUTO_FAULT) is True, "%r" % st)
            check("sim 卡阀：控制器已停",
                  st.get(proto.KEY_AUTO_RUNNING) is False)
            # 泵绝不能启动（阀相未过）
            check("sim 卡阀：进水泵未启动",
                  not bool(getattr(business.pump_bank.items[0], "on", True)))
            # 复位解锁
            c.request(proto.CMD_AUTO_CONTROL,
                      bson.encode({"op": proto.AUTO_OP_RESET}))
            check("sim 卡阀：复位后解锁",
                  _query_state(c).get(proto.KEY_AUTO_FAULT) is False)
            c.close()
        except Exception as exc:                    # noqa: BLE001
            import traceback
            errors.append("%s\n%s" % (exc, traceback.format_exc()))

    thread = threading.Thread(target=scenario, name="sim-e2e")
    thread.start()
    try:
        while thread.is_alive():
            await asyncio.sleep(0.05)
    finally:
        thread.join()
        await server.stop()
    if errors:
        raise RuntimeError(errors[0])


# ---------------------------------------------------------------------------
#   场景二：产水完整循环 ×N（配置往返/阀相/泵相/停止）+ 泄漏崩溃观察
# ---------------------------------------------------------------------------

def test_sim_produce_cycles_no_leak_no_crash():
    base = tempfile.mkdtemp(prefix="hsf_sime2e_cyc_")
    try:
        asyncio.run(_case_cycles(base))
    finally:
        shutil.rmtree(base, ignore_errors=True)


async def _case_cycles(basedir):
    log = []
    cfg, business, server = await _spin(basedir, log.append, move_ms=250,
                                        valve_timeout_ms=800)
    errors = []
    rss_samples = []
    task_samples = []

    def scenario():
        try:
            c = Client("127.0.0.1", server.port)
            c.hello()
            assert c.login("root", "root")[0]
            _subscribe(c, proto.TOPIC_AUTO_ALARM)
            _subscribe(c, proto.TOPIC_VFD_STATUS)
            _subscribe(c, proto.TOPIC_PUMP_STATUS)

            # ---- 配置 SET/GET 往返 ----
            devices_sel = ["valve1", "valve2", "stirPump", "inletPump"]
            r = c.request(proto.CMD_CONFIG_SET, bson.encode({
                "section": proto.CFG_SECTION_AUTO,
                "modes": {"produce": {"devices": devices_sel,
                                      "stirRpm": 720,
                                      "stirReverse": True}}}))
            assert r and r[1].get("ok") is True, "auto SET 失败：%r" % (r[1],)
            r = c.request(proto.CMD_CONFIG_GET, b"")
            got = (r[1].get(proto.CFG_SECTION_AUTO) or {}).get(
                "modes", {}).get("produce", {})
            check("sim 循环：配置 SET/GET 往返一致",
                  got.get("devices") == devices_sel
                  and got.get("stirRpm") == 720
                  and got.get("stirReverse") is True, "%r" % (got,))

            assert c.request(proto.CMD_RUN_MODE,
                             bson.encode({"mode": 1}))[1].get("ok") is True

            for cycle in range(1, CYCLES + 1):
                c.publishes.clear()
                r = c.request(proto.CMD_AUTO_CONTROL, bson.encode(
                    {"on": True, proto.KEY_AUTO_MODE: "produce"}))
                assert r and r[1].get("ok") is True, \
                    "第%d轮启动失败：%r" % (cycle, r[1])
                st = _wait_phase(c, autocontrol_phase_running())
                check("sim 循环%d：到达运行相" % cycle,
                      st.get(proto.KEY_AUTO_PHASE)
                      == autocontrol_phase_running(), "%r" % st)
                # 变频器：720rpm 反转 = 25Hz 反转（经真实虚拟从站回读；
                # 斜坡爬升期算 ACCELERATING，等频率稳到 25Hz 附近再断言）
                vfd = business.vfd
                deadline = time.time() + 8.0
                while time.time() < deadline:
                    if vfd.is_running() and abs(
                            vfd.output_freq_hz() - 25.0) < 1.5:
                        break
                    c.pump(0.05)
                check("sim 循环%d：搅拌泵按 720rpm 反转运行" % cycle,
                      vfd.is_running()
                      and vfd.run_state in (proto.RUN_RUNNING_REVERSE,
                                            proto.RUN_ACCELERATING,
                                            proto.RUN_CONSTANT_SPEED)
                      and vfd._cmd_reverse is True
                      and abs(vfd.output_freq_hz() - 25.0) < 2.0,
                      "running=%s state=%s hz=%s rev=%s" % (
                          vfd.is_running(), vfd.run_state,
                          vfd.output_freq_hz(), vfd._cmd_reverse))
                # 进水泵：DO6 写真（虚拟从站 DO 位 + 泵组回读）
                dev = business.digital[0]
                check("sim 循环%d：进水泵（DO6）已开" % cycle,
                      bool(business.pump_bank.items[0].on)
                      and bool(dev.do[6]),
                      "on=%s do=%s" % (business.pump_bank.items[0].on,
                                       dev.do))
                # 非预期告警（阀 250ms 行程 << 0.8s 超时，不应有超时告警）
                bad = [f for f in _alarm_frames(c)
                       if f.get("kind") in (proto.AUTO_ALARM_VALVE_TIMEOUT,
                                            proto.AUTO_ALARM_ACTION_FAILED)]
                check("sim 循环%d：无非预期告警" % cycle, not bad, "%r" % (bad,))

                # ---- 停止：全停 ----
                r = c.request(proto.CMD_AUTO_CONTROL, bson.encode(
                    {"on": False}))
                assert r and r[1].get("ok") is True
                deadline = time.time() + 5.0
                while time.time() < deadline:
                    if (not business.vfd.is_running()
                            and not business.pump_bank.items[0].on):
                        break
                    c.pump(0.05)
                check("sim 循环%d：停止后泵/阀全停" % cycle,
                      not business.vfd.is_running()
                      and not business.pump_bank.items[0].on,
                      "vfd=%s pump=%s" % (business.vfd.is_running(),
                                          business.pump_bank.items[0].on))
                time.sleep(0.3)
                rss_samples.append(_rss_mb())
                task_samples.append(_task_count())
            c.close()
        except Exception as exc:                    # noqa: BLE001
            import traceback
            errors.append("%s\n%s" % (exc, traceback.format_exc()))

    thread = threading.Thread(target=scenario, name="sim-e2e")
    thread.start()
    try:
        tasks_base = _task_count()
        rss_samples.insert(0, _rss_mb())            # 暖机前基线
        while thread.is_alive():
            await asyncio.sleep(0.05)
        await asyncio.sleep(0.3)                    # 收尾让任务清完
        task_samples.append(_task_count())
    finally:
        thread.join()
        await server.stop()
    if errors:
        raise RuntimeError(errors[0])

    # ---- 无泄漏/无崩溃判定（本进程 = 服务端进程）----
    check("sim 端到端：进程存活（无崩溃）", True)
    valid = [v for v in rss_samples if v is not None]
    if len(valid) >= 3:
        growth = valid[-1] - valid[0]               # 启动基线 → 尾轮
        check("sim 端到端：RSS 增长有界（%.1f -> %.1f MB，+%.1f）"
              % (valid[0], valid[-1], growth),
              growth < RSS_GROWTH_LIMIT_MB, "%.1f MB" % growth)
    else:
        check("sim 端到端：RSS 读数不可用（跳过）", True)
    if task_samples[0] >= 0 and task_samples[-1] >= 0:
        check("sim 端到端：asyncio 任务数不增长（%d -> %d）"
              % (task_samples[0], task_samples[-1]),
              task_samples[-1] <= task_samples[0] + 2,
              "%s" % (task_samples,))


def test_sim_cycle_stage_timing():
    """组合模式：RUN_MODE 应答带**阶段计时两键**（主界面剩余时间表盘）。

    [为什么要端到端] 单元测试只验 stage_timing() 的算数；这里验"配置节 →
    循环启动 → 快照键真正上链路"这条链（客户端表盘全靠它）。
    """
    base = tempfile.mkdtemp(prefix="hsf_sime2e_stage_")
    try:
        asyncio.run(_case_stage_timing(base))
    finally:
        shutil.rmtree(base, ignore_errors=True)


async def _case_stage_timing(basedir):
    log = []
    cfg, business, server = await _spin(basedir, log.append, move_ms=200,
                                        valve_timeout_ms=3000)
    errors = []

    def scenario():
        try:
            c = Client("127.0.0.1", server.port)
            c.hello()
            assert c.login("root", "root")[0]
            # 三子模式各配一路阀 + 循环启用 + 产水定时兜底 30 分钟
            r = c.request(proto.CMD_CONFIG_SET, bson.encode({
                "section": proto.CFG_SECTION_AUTO,
                "modes": {"produce": {"devices": ["valve1"]},
                          "backwash": {"devices": ["valve2"]},
                          "rinse": {"devices": ["valve3"]}},
                proto.CFG_AUTO_CYCLE: {
                    proto.CFG_AUTO_CYCLE_ENABLED: True,
                    proto.CFG_AUTO_CYCLE_TIMER_FALLBACK: True,
                    proto.CFG_AUTO_CYCLE_TIMER_MINUTES: 30,
                    proto.CFG_AUTO_CYCLE_BACKWASH_MINUTES: 10,
                    proto.CFG_AUTO_CYCLE_RINSE_MINUTES: 10}}))
            assert r and r[1].get("ok") is True, "auto SET 失败：%r" % (r[1],)
            assert c.request(proto.CMD_RUN_MODE,
                             bson.encode({"mode": 1}))[1].get("ok") is True
            r = c.request(proto.CMD_AUTO_CONTROL, bson.encode(
                {"on": True, proto.KEY_AUTO_MODE: proto.AUTO_MODE_AUTO}))
            assert r and r[1].get("ok") is True, "启动应受理：%r" % (r[1],)
            st = _wait_phase(c, autocontrol_phase_running(), timeout_s=15)
            check("组合模式：到达运行相（子阶段=产水）",
                  st.get(proto.KEY_AUTO_PHASE) == autocontrol_phase_running()
                  and st.get(proto.KEY_AUTO_MODE) == "produce", "%r" % st)
            total = int(st.get(proto.KEY_AUTO_STAGE_TOTAL_MS, -1))
            elapsed = int(st.get(proto.KEY_AUTO_STAGE_ELAPSED_MS, -1))
            check("产水 + 定时兜底开 ⇒ 阶段总时长 30 分钟",
                  total == 30 * 60000, "%r" % st)
            check("阶段已运行时长在 [0, 总时长) 内",
                  0 <= elapsed < total, "elapsed=%s total=%s"
                  % (elapsed, total))
            # 等 0.8s 再看：计时必须真的在走（不是常量）
            deadline = time.time() + 1.5
            while time.time() < deadline:
                c.pump(0.05)
            st2 = _query_state(c)
            elapsed2 = int(st2.get(proto.KEY_AUTO_STAGE_ELAPSED_MS, -1))
            check("阶段计时随时间增长", elapsed2 >= elapsed + 500,
                  "%s -> %s" % (elapsed, elapsed2))
            # 停止 ⇒ **暂停**（2026-11-19 用户口径：停止保留状态，按启动
            # 续跑）⇒ 阶段计时仍报"记住的阶段与剩余时间"（冻结在停止那刻），
            # 而不是归不可知（-1/0）—— 界面据此显示「暂停中（还剩 X）」。
            r = c.request(proto.CMD_AUTO_CONTROL, bson.encode({"on": False}))
            assert r and r[1].get("ok") is True, "停止应受理：%r" % (r[1],)
            st3 = _query_state(c)
            check("停止后阶段计时仍报记住的阶段与剩余时间（暂停态）",
                  int(st3.get(proto.KEY_AUTO_STAGE_ELAPSED_MS, -1)) >= 0
                  and int(st3.get(proto.KEY_AUTO_STAGE_TOTAL_MS, 0))
                  == 30 * 60000,
                  "%r" % st3)
            check("停止后相位=paused 且带 autoPaused/autoPauseMode",
                  st3.get(proto.KEY_AUTO_PHASE) == "paused"
                  and st3.get(proto.KEY_AUTO_PAUSED) is True
                  and st3.get(proto.KEY_AUTO_PAUSE_MODE) == "produce",
                  "%r" % st3)
            c.close()
        except Exception as exc:                    # noqa: BLE001
            import traceback
            errors.append("%s\n%s" % (exc, traceback.format_exc()))

    thread = threading.Thread(target=scenario, name="sim-e2e-stage")
    thread.start()
    try:
        while thread.is_alive():
            await asyncio.sleep(0.05)
    finally:
        thread.join()
        await server.stop()
    if errors:
        raise RuntimeError(errors[0])


def autocontrol_phase_running():
    """运行相字符串（引到一处，避免测试里散落字面量）。"""
    from hsfsrv import autocontrol
    return autocontrol.AutoController.PHASE_RUNNING


def _task_count():
    try:
        return len(asyncio.all_tasks())
    except Exception:                               # noqa: BLE001
        return -1


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
    print("\n===== 模拟器端到端: %d failed（check PASS %d / FAIL %d）====="
          % (len(failed), PASS, FAIL))
    return 1 if failed or FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
