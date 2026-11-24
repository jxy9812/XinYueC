# -*- coding: utf-8 -*-
"""自动循环（组合模式「自动」）**逐参数执行矩阵**（2026-11-27）。

[为什么要这一份]
==============
`test_auto_cycle.py` 锁的是**配置层**（instances 全键/派生视图/fail-closed）
与**单元级**触发逻辑；`test_auto_scenarios.py` 锁的是**换相策略**（泵处置）。
两者都不回答一个问题：**实例上的每个参数真的驱动了实际动作吗？**
（配置存进去、GET 读得回，但运行期读的是另一个字段、或压根没被消费 ——
这类"存/读一致但跑起来不生效"的缺陷，只有真跑一遍才看得见。）

故本文件是**执行矩阵**：每个实例参数逐项改成不同值 → 起一份真 sim →
启动组合循环 → 直接观测**设备动作**（数字量 DO 位 / 变频器寄存器与输出
频率 / 阀状态 / 实例子阶段），而不是只看配置回显。

[覆盖矩阵]（每条都断言**实际动作**）
==================================
    ① devices 设备组合：仅阀1 / 阀2+阀4 / 阀1+搅拌泵 / 仅进水泵
       ⇒ 阀相 DO 位（阀 i → DO i-1）、未选阀保持关、进水泵 DO6、搅拌泵启停
    ② stirRpm：300 / 600 / 1440 ⇒ 变频器设定频率（rpm/1440×50 ±0.2）
       与实际爬坡（输出频率从 0 向目标单调上升，能看到中间样本）
    ③ stirReverse：False / True ⇒ 变频器快照 reverse 键 + runState
       （正转/反转运行）+ 虚拟模型物理方向
    ④ stirAccelRpmS：0 / 30 / 300 ⇒ 0xF011 满量程加速时间原始码
       （rated/accel：48.0s→480、4.8s→48；0=**不写**、保持现场原值）
    ⑤ 四触发逐项（改完重启循环）：
       · 压差 pressEnabled+pressThreshold+pressCompare（gt / lt 两向）
       · 进水压力 press1Enabled+press1Threshold+press1Compare
       · 出水压力 press2Enabled+press2Threshold+press2Compare
       · 定时 timerEnabled+timerMinutes（假时钟：把阶段起点往回拨 1 分钟）
       每条都先做**负向控制**（条件未满足的窗口内不换相），再注入条件 ⇒
       换到下一实例 + 快照 autoStageTitle 变成目标实例标题 + 换相日志点名
       该触发（证明是**这个参数**触发的，不是别的原因）。
    ⑥ resetValvesOnStop：True / False ⇒ 远程停止后阀 DO 清零 / 保持
       （进水泵与变频器两种情况都必须停 —— 复位开关只影响阀）
    ⑦ resetValvesOnEstop：True / False ⇒ 急停后同上
    ⑧ stirMaxRpm 钳位：stirMaxRpm=800 + 实例 stirRpm=1440 ⇒ GET 回显被夹到
       800，且变频器给定 ≤ 800rpm 对应频率
    ⑨ 实例顺序 / 重复 mode / 序号标题：[产水, 顺洗, 产水] ⇒ GET 顺序不变、
       autoStageTitle 是「产水模式#2」（与服务端 cycle_instance_titles 同源）、
       循环严格按列表顺序推进（含末位回绕首位）

[隔离纪律]
========
每个子用例**自己起一份全新 sim**（随机端口 + 独立临时程序根 + 短阀行程
250ms）：上一组的残留（变频器还在降速 / 阀还在动）会把下一组的"启动"拒成
设备忙/阀相超时 —— 这是既有实锅，本文件不再共用服务端。

[怎么跑]
    python -I tests/test_auto_param_matrix.py      # 独立跑
    python -I tests/run_all.py                     # 全量（含本段）
"""

import asyncio
import os
import shutil
import sys
import tempfile
import threading
import time
import traceback

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

from hsfsrv import app as app_mod              # noqa: E402
from hsfsrv import auth as auth_mod            # noqa: E402
from hsfsrv import bson                        # noqa: E402
from hsfsrv import config as cfgmod            # noqa: E402
from hsfsrv import proto, xlink                # noqa: E402
from test_vfd_ops import Client                # noqa: E402

PASS = 0
FAIL = 0

#: 额定点（与 cfg.serial.vfd_rated_rpm/hz 缺省一致）：rpm → Hz 的唯一换算。
RATED_RPM = 1440.0
RATED_HZ = 50.0

#: 模拟量采样电阻（mV → mA；见 devices.AnalogInput.SHUNT_OHM）。
SHUNT_OHM = 249.0
#: 压力类通道缺省量程：0~1 MPa ↔ 4~20 mA（cfg.analog.press*_high_mpa=1.0）。
PRESS_FS_MPA = 1.0


def check(name, ok, detail=""):
    global PASS, FAIL
    if ok:
        PASS += 1
        print("  [PASS] %s %s" % (name, detail))
    else:
        FAIL += 1
        print("  [FAIL] %s %s" % (name, detail))


# ============================================================================
# 夹具：隔离 sim
# ============================================================================

async def _spin(base, log, tag):
    """起一份真 sim（虚拟设备 + 真 XLink + 真业务层）。

    [隔离] 每个子用例一个**独立临时程序根**（配置/账户/库三样都不与别的
    实例共用；库关掉不落盘）。端口用 0 ⇒ 系统分配，绝不撞常驻 6100/6200。
    """
    basedir = tempfile.mkdtemp(prefix="hsf_pm_%s_" % tag, dir=base)
    cfg = cfgmod.Config()
    cfg.resolve_paths(basedir)
    os.makedirs(os.path.join(basedir, "cfg"), exist_ok=True)
    cfg.port = 0
    cfg.sim_enabled = True
    # [短阀行程] 真机满开满关 ~10s；执行矩阵只关心"到位后 DO/泵是什么状态"
    # ⇒ 行程压到 250ms（阀相 ~0.4s 收口），单用例才跑得完。
    cfg.sim_valve_move_ms = 250
    # 阀动作超时（控制器阀相截止 + 阀层超时判定同一个配置值）：2000ms 对
    # 250ms 行程留足两拍余量，又不至于真卡阀时把用例拖到 20s。
    cfg.valve_move_timeout_ms = 2000
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
    await asyncio.sleep(0.3)
    return basedir, cfg, business, server


def _devices_ready(business):
    """设备是否已"可跑"：三样在线 + 6 路阀位已知。

    [为什么阀位也要判] 阀相要等"全部到位"：阀位 Unknown 既不是 Fault 也不是
    目标位 ⇒ 状态机会一直等到阀相截止 → 报阀超时中止（假红）。首拍数字量
    +阀轮询跑过之后阀位才是 Closed。
    """
    try:
        if business.vfd is None or not getattr(business.vfd, "online", False):
            return False
        if business.analog is None or not getattr(business.analog,
                                                  "online", False):
            return False
        if not business.digital or not all(
                getattr(d, "online", False) for d in business.digital):
            return False
        bank = business.valve_bank
        if bank is None:
            return False
        known = (proto.HsfValveState.Open, proto.HsfValveState.Closed)
        for item in bank.items:
            if int(getattr(item, "state", proto.HsfValveState.Unknown)) \
                    not in known:
                return False
        return True
    except Exception:                               # noqa: BLE001
        return False


async def _wait_ready(business, timeout=8.0):
    """等设备就绪（在事件循环里等，不占用会话 ⇒ 不会触发静默判线）。"""
    deadline = time.time() + timeout
    while time.time() < deadline:
        if _devices_ready(business):
            return True
        await asyncio.sleep(0.05)
    return False


async def _run_case(base, log, tag, scenario, timeout=90.0):
    """起一份隔离 sim → 在线程里跑阻塞式场景 → 收尾（停轮询/停服务端）。

    [为什么场景跑在线程里] 本文件复用的 `Client` 是**阻塞 socket**；若在
    asyncio 主线程里 hello/login/request，会把同线程的事件循环堵死 ⇒ 服务端
    连 HELLO 挑战都发不出来（实测 login 恒 "no challenge"）。
    """
    basedir, cfg, business, server = await _spin(base, log, tag)
    ok_ready = await _wait_ready(business)
    check("%s：设备就绪（变频器/模拟量/数字量在线 + 阀位已知）" % tag,
          ok_ready, "" if ok_ready else "%.0fms 内未就绪" % 8000)
    errors = []
    if ok_ready:
        def _run():
            # [Client 构造也要在 try 里] 连接被拒（服务端还没 listen 完）会
            # 抛在构造处；放在 try 外的话线程静默死掉、errors 为空 ⇒ 整个
            # 用例"看着跑完"却一条断言都没跑（假绿）。
            c = None
            try:
                c = Client("127.0.0.1", server.port)
                c.hello()
                _login_root(c, tag)
                scenario(c, business, cfg, log)
            except Exception:                       # noqa: BLE001
                errors.append(traceback.format_exc())
            finally:
                if c is not None:
                    try:
                        # 收尾：停控制器 + 全停设备（幂等；断言之后才做）。
                        _stop_cycle(c, wait=1.5)
                    except Exception:               # noqa: BLE001
                        pass
                    c.close()

        th = threading.Thread(target=_run, name="pm-%s" % tag)
        # [daemon] 万一某个等待被拖住，也不能让挂死的线程把整个测试进程吊在
        # 退出处（run_all 是按子进程超时判失败的）。
        th.daemon = True
        th.start()
        t0 = time.time()
        while th.is_alive() and (time.time() - t0) < timeout:
            await asyncio.sleep(0.05)
        if th.is_alive():
            errors.append("场景超时（%.0fs 未结束）" % timeout)
        th.join(5.0)
        for err in errors:
            check("%s 场景异常" % tag, False, err)
        # 让 finally 里的"停止"任务落定，再拆轮询（否则可能在拆设备的
        # 同时还有一笔停机动作在途）。
        await asyncio.sleep(0.15)
    try:
        await business.stop_polling()
    except Exception:                               # noqa: BLE001
        pass
    await server.stop()


def _login_root(c, tag=""):
    """登录 root（HELLO 挑战偶发晚到 ⇒ 重试，避免测试假红）。"""
    last = None
    for _ in range(30):
        c.hello()
        ok, role = c.login("root", "root")
        if ok:
            return role
        last = role
        time.sleep(0.1)
    raise AssertionError("%s：root 登录失败（%r）" % (tag, last))


# ============================================================================
# 协议小工具（配置 / 启停 / 快照）
# ============================================================================

def _inst(mode, devices, press=None, press1=None, press2=None, timer=None,
          rpm=None, reverse=None, accel=None, reset_stop=None,
          reset_estop=None):
    """构造一个**循环实例**（协议面 camelCase 键，与 SET 载荷同形）。

    :param press/press1/press2: (阈值 MPa, "gt"/"lt")；None ⇒ 该触发关闭。
    :param timer: 定时分钟数；None ⇒ 定时触发关闭。
    """
    inst = {"mode": mode,
            "devices": list(devices),
            "pressEnabled": False,
            "press1Enabled": False,
            "press2Enabled": False,
            "timerEnabled": False}
    if press is not None:
        inst["pressEnabled"] = True
        inst["pressThreshold"] = float(press[0])
        inst["pressCompare"] = str(press[1])
    if press1 is not None:
        inst["press1Enabled"] = True
        inst["press1Threshold"] = float(press1[0])
        inst["press1Compare"] = str(press1[1])
    if press2 is not None:
        inst["press2Enabled"] = True
        inst["press2Threshold"] = float(press2[0])
        inst["press2Compare"] = str(press2[1])
    if timer is not None:
        inst["timerEnabled"] = True
        inst["timerMinutes"] = int(timer)
    if rpm is not None:
        inst["stirRpm"] = int(rpm)
    if reverse is not None:
        inst["stirReverse"] = bool(reverse)
    if accel is not None:
        inst["stirAccelRpmS"] = int(accel)
    if reset_stop is not None:
        inst["resetValvesOnStop"] = bool(reset_stop)
    if reset_estop is not None:
        inst["resetValvesOnEstop"] = bool(reset_estop)
    return inst


def _set_cycle(c, instances, tag, extra=None):
    """SET auto 节的循环实例列表；返回应答里的 auto 文档。"""
    doc = {"section": proto.CFG_SECTION_AUTO,
           proto.CFG_AUTO_CYCLE: {
               "enabled": True,
               proto.CFG_AUTO_CYCLE_INSTANCES: instances}}
    if extra:
        doc.update(extra)
    r = c.request(proto.CMD_CONFIG_SET, bson.encode(doc), wait=5.0)
    assert r is not None, "%s：auto SET 无应答" % tag
    body = r[1]
    assert body.get("ok") is True, \
        "%s：auto SET 失败：%r" % (tag, body.get("err"))
    return body.get(proto.CFG_SECTION_AUTO) or {}


def _get_instances(c):
    """GET 配置里的循环实例列表（协议面 camelCase；顺序＝配置顺序）。"""
    r = c.request(proto.CMD_CONFIG_GET, b"", wait=5.0)
    assert r is not None, "config GET 无应答"
    auto = (r[1].get(proto.CFG_SECTION_AUTO) or {}) if r else {}
    cycle = auto.get(proto.CFG_AUTO_CYCLE) or {}
    return list(cycle.get(proto.CFG_AUTO_CYCLE_INSTANCES) or [])


def _state(c):
    """查运行状态快照（autoMode/autoPhase/autoStageIndex/autoStageTitle…）。"""
    r = c.request(proto.CMD_RUN_MODE, b"", wait=3.0)
    return (r[1] if r else {}) or {}


def _enter_auto(c, tag):
    """切到自动模式（只是授权，不启动控制器）。"""
    r = c.request(proto.CMD_RUN_MODE, bson.encode({"mode": 1}), wait=5.0)
    assert r is not None and r[1].get("ok") is True, \
        "%s：切自动模式失败：%r" % (tag, r[1] if r else None)
    # [切模式会异步全停设备] 那份"关阀"任务若晚于本次启动的阀相命令落地，
    # 会把刚开的阀又关回去 ⇒ 阀相等到超时（假红）⇒ 等它落定再启动。
    time.sleep(0.35)


def _start_cycle(c, tag):
    """启动组合循环（载荷带 mode=auto，与客户端主界面同一条）。"""
    r = c.request(proto.CMD_AUTO_CONTROL,
                  bson.encode({"on": True,
                               proto.KEY_AUTO_MODE: proto.AUTO_MODE_AUTO}),
                  wait=5.0)
    assert r is not None and r[1].get("ok") is True, \
        "%s：启动组合循环失败：%r" % (tag, r[1] if r else None)


def _stop_cycle(c, wait=3.0):
    """远程停止自动运行（op=2；停控制器 + 全停设备）。"""
    return c.request(proto.CMD_AUTO_CONTROL,
                     bson.encode({"op": proto.AUTO_OP_STOP}), wait=wait)


def _wait_stage(c, idx=None, phase="running", timeout=20.0):
    """等快照到达「实例下标 idx + 相位 phase」；超时返回 None。"""
    deadline = time.time() + timeout
    st = {}
    while time.time() < deadline:
        st = _state(c)
        cur = int(st.get(proto.KEY_AUTO_STAGE_INDEX, -99))
        if st.get(proto.KEY_AUTO_PHASE) == phase and \
                (idx is None or cur == int(idx)):
            return st
        time.sleep(0.04)
    return None


def _no_switch(c, idx, window=1.0):
    """**负向窗口**：idx 在 window 内不变（触发条件未满足就不许换相）。

    :return: (ok, 最后一次快照)
    """
    deadline = time.time() + window
    st = {}
    while time.time() < deadline:
        st = _state(c)
        cur = int(st.get(proto.KEY_AUTO_STAGE_INDEX, -99))
        if cur != int(idx):
            return False, st
        time.sleep(0.05)
    return True, st


def _wait_switch(c, from_idx, timeout=8.0):
    """等实例下标离开 from_idx（换相已发生）；返回 (快照, 新下标)。"""
    deadline = time.time() + timeout
    while time.time() < deadline:
        st = _state(c)
        cur = int(st.get(proto.KEY_AUTO_STAGE_INDEX, -99))
        if cur >= 0 and cur != int(from_idx):
            return st, cur
        time.sleep(0.03)
    return None, None


def _shift_stage_clock(business, ms):
    """**假时钟**：把当前阶段计时起点往回拨 ms（模拟"定时已到期"）。

    [为什么不真等] timerMinutes 最小 1 分钟 ⇒ 真等 4 个触发用例要 4 分钟。
    阶段计时是控制器内部状态（`_cycle_stage_at`），运行中的进程就在本进程
    里 ⇒ 直接往回拨等价于"已经跑了 ms 毫秒"（与 test_auto_cycle 的假时钟
    同款口径）。
    """
    ctl = business.auto_controller
    ctl._cycle_stage_at = int(ctl._cycle_stage_at) - int(ms)


def _log_lines(log, mark, needle):
    """取 mark 之后含 needle 的日志行（只看**本次**用例产生的行）。"""
    return [str(ln) for ln in log[mark:] if needle in str(ln)]


# ============================================================================
# 设备观测小工具（DO 位 / 阀状态 / 变频器寄存器与频率）
# ============================================================================

def _do(business, ch):
    """数字量 DO 通道当前值（0/1；不可用 -1）。"""
    try:
        return int(business.digital[0].do[int(ch)])
    except Exception:                               # noqa: BLE001
        return -1


def _do_str(business):
    """DO0~DO7 位串（失败详情用；DO0..5=阀1..6、DO6=进水泵、DO7=启动灯）。"""
    try:
        return "".join(str(int(v)) for v in
                       list(business.digital[0].do[:8]))
    except Exception:                               # noqa: BLE001
        return "?"


def _wait_do(business, ch, want, timeout=4.0):
    """等某路 DO 变成 want；返回是否达成。"""
    deadline = time.time() + timeout
    while time.time() < deadline:
        if _do(business, ch) == (1 if want else 0):
            return True
        time.sleep(0.03)
    return False


def _valve_state(business, index):
    """第 index 路（0 基）阀状态（HsfValveState；不可用 -1）。"""
    try:
        return int(business.valve_bank.items[int(index)].state)
    except Exception:                               # noqa: BLE001
        return -1


def _vfd_model(business, cfg):
    """虚拟变频器的寄存器模型（sim 下的"真机"）。

    [与 test_auto_scenarios 同款] 参数寄存器（0xF011）没有走 0x011B 的读
    通路（那里只读 P1 铭牌组 0xF100~0xF105）⇒ 直接读虚拟模型/寄存器表。
    """
    try:
        vfd = business.vfd
        if vfd is None:
            return None
        if getattr(vfd, "_master", None) is None:
            vfd._master_get()
        return vfd._master.models[cfg.vfd_unit_id]
    except Exception:                               # noqa: BLE001
        return None


def _vfd_reg(business, cfg, addr):
    """读虚拟变频器一个寄存器（FC03 同一条表；失败 None）。"""
    model = _vfd_model(business, cfg)
    if model is None:
        return None
    try:
        return int(model.read(3, int(addr), 1)[0])
    except Exception:                               # noqa: BLE001
        return None


def _vfd_out_hz(business):
    """变频器实际输出频率（Hz；遥测缓存，100ms 刷新）。"""
    try:
        return float(business.vfd.output_freq_hz() or 0.0)
    except Exception:                               # noqa: BLE001
        return 0.0


def _vfd_set_hz(business):
    """变频器**设定**频率（0x1000 百分比回读 × 最大频率；Hz）。"""
    try:
        val = business.vfd.set_freq_hz()
        return None if val is None else float(val)
    except Exception:                               # noqa: BLE001
        return None


def _vfd_running(business):
    """变频器是否在运行（以遥测状态字为准）。"""
    try:
        return bool(business.vfd.is_running())
    except Exception:                               # noqa: BLE001
        return False


def _wait_vfd_running(business, want, timeout=4.0):
    """等变频器进入/退出运行态。"""
    deadline = time.time() + timeout
    while time.time() < deadline:
        if _vfd_running(business) is bool(want):
            return True
        time.sleep(0.03)
    return False


def _sample_ramp(c, business, target_hz, timeout=20.0):
    """采样输出频率直到到速（≥98% 目标）或超时；返回非零样本列表。

    [保活] 裸 socket 客户端**没有心跳** ⇒ 空闲 6s 会被服务端按静默判线断开
    ⇒ 长斜坡（1440rpm/240rpm·s 要 6s）期间每 ~1s 发一条状态查询兼保活。
    """
    samples = []
    deadline = time.time() + timeout
    last_keepalive = time.time()
    while time.time() < deadline:
        hz = _vfd_out_hz(business)
        if hz > 0.05:
            samples.append(hz)
            if hz >= target_hz * 0.98:
                break
        if time.time() - last_keepalive > 1.0:
            _state(c)
            last_keepalive = time.time()
        time.sleep(0.03)
    return samples


def _wait_set_hz(business, want_hz, tol=0.2, timeout=4.0):
    """等**设定频率回读**跟上目标（返回最后一次读数）。

    [为什么要等] `set_freq_hz()` 读的是 `raw[0x1000]` —— 那是**轮询缓存**，
    服务端写入后要等下一拍 VFD 采集（100ms）才刷新。启动命令刚落地就读会
    拿到上一笔给定（缺省 25Hz）⇒ 把"缓存没跟上"当"参数没生效"（假红）。
    """
    deadline = time.time() + timeout
    got = None
    while time.time() < deadline:
        got = _vfd_set_hz(business)
        if got is not None and abs(got - want_hz) <= tol:
            return got
        time.sleep(0.04)
    return got


def _wait_at_speed(business, target_hz, tol=0.10, timeout=6.0):
    """等输出频率进入**恒速死区**（|目标-输出| < 0.15Hz）。

    [为什么不能只等 98%] 变频器的"恒速正转/反转"判据是差值 < 0.15Hz
    （devices.VfdM512.PHASE_DEADBAND_HZ）；20.83Hz 的 98% 还差 0.42Hz ⇒
    那一刻 runState 仍是"加速中"，断言恒速会偶发假红。
    """
    deadline = time.time() + timeout
    while time.time() < deadline:
        if abs(_vfd_out_hz(business) - target_hz) <= tol:
            return True
        time.sleep(0.03)
    return False


def _mv_for_mpa(mpa):
    """MPa → 模拟量毫伏（0~1MPa ↔ 4~20mA，采样电阻 249Ω）。"""
    ma = 4.0 + (float(mpa) / PRESS_FS_MPA) * 16.0
    return max(0, int(round(ma * SHUNT_OHM)))


def _analog_model(business):
    """虚拟模拟量模型（mV 数组；sim 下的"传感器"）。"""
    try:
        return business.analog._master.model
    except Exception:                               # noqa: BLE001
        return None


def _set_press(business, channel, mpa):
    """把某一路模拟量通道的读数改成 mpa（MPa）。"""
    model = _analog_model(business)
    assert model is not None, "模拟量模型不可用（sim 未装配）"
    model.mv[int(channel)] = _mv_for_mpa(mpa)


# ============================================================================
# ① devices 设备组合
# ============================================================================

#: (标签, 设备清单)。覆盖：单阀 / 双阀 / 阀+搅拌 / 只有进水泵。
DEVICE_VARIANTS = (
    ("仅阀1", ["valve1"]),
    ("阀2+阀4", ["valve2", "valve4"]),
    ("阀1+搅拌泵", ["valve1", "stirPump"]),
    ("仅进水泵", ["inletPump"]),
)


def _scenario_devices(c, business, cfg, log, tag, devs):
    """设备组合 → 阀相 DO 位 / 未选阀保持关 / 泵启停。"""
    rpm = 720
    _set_cycle(c, [
        _inst("produce", devs, timer=30, rpm=rpm, accel=1440),
        _inst("rinse", ["valve6"], timer=30),
    ], tag)
    _enter_auto(c, tag)
    _start_cycle(c, tag)
    st = _wait_stage(c, 0, "running", 20.0)
    check("%s：进入运行相（实例0=产水）" % tag, st is not None,
          "phase=%s idx=%s" % ((st or {}).get(proto.KEY_AUTO_PHASE),
                               (st or {}).get(proto.KEY_AUTO_STAGE_INDEX)))
    if st is None:
        return
    # ---- 阀 DO 位：阀 i 的 DO = i-1（开=1/关=0）----
    want = {i: (1 if ("valve%d" % i) in devs else 0) for i in range(1, 7)}
    bad = [i for i in range(1, 7) if _do(business, i - 1) != want[i]]
    check("%s：阀相 DO 位符合设备组合（开%s）"
          % (tag, "、".join("阀%d" % i for i in range(1, 7)
                            if want[i] == 1) or "无"),
          not bad, "失配阀=%s do=%s" % (bad, _do_str(business)))
    # ---- 未选阀保持关（不仅是 DO=0，阀状态也必须是"已关"）----
    unsel = [i for i in range(1, 7) if want[i] == 0]
    unsel_bad = [i for i in unsel
                 if _valve_state(business, i - 1) != proto.HsfValveState.Closed]
    check("%s：未选阀保持关（%d 路）" % (tag, len(unsel)), not unsel_bad,
          "失配=%s 状态=%s" % (unsel_bad,
                               [_valve_state(business, i - 1)
                                for i in unsel]))
    # ---- 进水泵 DO6 ----
    want_inlet = 1 if "inletPump" in devs else 0
    check("%s：进水泵 DO6=%d" % (tag, want_inlet),
          _do(business, 6) == want_inlet,
          "do6=%d do=%s" % (_do(business, 6), _do_str(business)))
    # ---- 搅拌泵（变频器）----
    want_stir = "stirPump" in devs
    if want_stir:
        up = _wait_vfd_running(business, True, 4.0)
        check("%s：搅拌泵已启动" % tag, up,
              "running=%s setHz=%s" % (_vfd_running(business),
                                       _vfd_set_hz(business)))
        want_hz = rpm / RATED_RPM * RATED_HZ
        # [等回读] 设定频率读的是轮询缓存（见 _wait_set_hz 的说明）。
        got = _wait_set_hz(business, want_hz)
        check("%s：搅拌给定 ≈ %.0frpm（%.2fHz，±0.2）"
              % (tag, rpm, want_hz),
              got is not None and abs(got - want_hz) <= 0.2,
              "setFreqHz=%r" % (got,))
    else:
        time.sleep(0.4)                 # 给"不该启动"留一个可观测窗口
        check("%s：搅拌泵未启动" % tag,
              (not _vfd_running(business)) and _vfd_out_hz(business) <= 0.05,
              "running=%s outHz=%s" % (_vfd_running(business),
                                       _vfd_out_hz(business)))


async def _case_devices(base, log):
    print("\n=== ① devices 设备组合（阀 DO 位 / 未选阀 / 泵启停）===")
    # [目录名用 ASCII] tag 是给人看的（中文），临时程序根的前缀保持 ASCII，
    # 免得某些只吃 ANSI 路径的工具链在中文目录上出怪问题。
    for idx, (tag, devs) in enumerate(DEVICE_VARIANTS):
        await _run_case(
            base, log, "dev%d" % (idx + 1),
            lambda c, b, cfg, lg, tag=tag, devs=devs:
                _scenario_devices(c, b, cfg, lg, tag, devs))


# ============================================================================
# ② stirRpm 搅拌转速
# ============================================================================

def _scenario_stir_rpm(c, business, cfg, log, tag, rpm):
    """stirRpm → 变频器设定频率 + 实际爬坡方向。"""
    # accel=240rpm/s（满量程 6s）：300rpm 1.25s、600rpm 2.5s、1440rpm 6s
    # ⇒ 既能在采样里看到"中间转速"，又不会把单用例拖太久。
    _set_cycle(c, [
        _inst("produce", ["valve1", "stirPump"], timer=30, rpm=rpm,
              accel=240),
        _inst("rinse", ["valve6"], timer=30),
    ], tag)
    _enter_auto(c, tag)
    _start_cycle(c, tag)
    st = _wait_stage(c, 0, "running", 20.0)
    check("%s：进入运行相" % tag, st is not None, "")
    if st is None:
        return
    want_hz = rpm / RATED_RPM * RATED_HZ
    # ---- 设定频率（0x1000 回读×满量程；等轮询缓存跟上）----
    got = _wait_set_hz(business, want_hz)
    check("%s：设定频率 ≈ %.0frpm（%.2fHz，±0.2）" % (tag, rpm, want_hz),
          got is not None and abs(got - want_hz) <= 0.2,
          "setFreqHz=%r want=%.3f" % (got, want_hz))
    # ---- 实际输出爬坡：到速 + 单调向上 + 看得到中间样本 ----
    samples = _sample_ramp(c, business, want_hz, timeout=20.0)
    reached = bool(samples) and samples[-1] >= want_hz * 0.98
    check("%s：输出频率爬到目标（%.2fHz）" % (tag, want_hz), reached,
          "samples=%d 尾=%s" % (len(samples), samples[-3:]))
    mono = all(b >= a - 0.1 for a, b in zip(samples, samples[1:]))
    mid = bool(samples) and min(samples) < want_hz * 0.9
    check("%s：爬坡方向为正且有中间转速样本（不是瞬跳）" % tag,
          mono and mid,
          "首=%s 最小=%.2f 单调=%s" % (samples[:2],
                                       min(samples) if samples else -1.0,
                                       mono))


async def _case_stir_rpm(base, log):
    print("\n=== ② stirRpm 搅拌转速（300/600/1440 → 设定频率 + 爬坡）===")
    for rpm in (300, 600, 1440):
        await _run_case(
            base, log, "rpm%d" % rpm,
            lambda c, b, cfg, lg, rpm=rpm: _scenario_stir_rpm(
                c, b, cfg, lg, "stirRpm=%d" % rpm, rpm))


# ============================================================================
# ③ stirReverse 搅拌方向
# ============================================================================

def _scenario_stir_reverse(c, business, cfg, log, tag, reverse):
    """stirReverse → 变频器快照 reverse 键 + runState（正转/反转运行）。"""
    # accel=1440（满量程 1s）：600rpm 0.42s 到速，快照时已是"恒速运行"。
    _set_cycle(c, [
        _inst("produce", ["valve1", "stirPump"], timer=30, rpm=600,
              reverse=reverse, accel=1440),
        _inst("rinse", ["valve6"], timer=30),
    ], tag)
    _enter_auto(c, tag)
    _start_cycle(c, tag)
    st = _wait_stage(c, 0, "running", 20.0)
    check("%s：进入运行相" % tag, st is not None, "")
    if st is None:
        return
    want_hz = 600.0 / RATED_RPM * RATED_HZ
    samples = _sample_ramp(c, business, want_hz, timeout=15.0)
    check("%s：到速（%.2fHz）" % (tag, want_hz),
          bool(samples) and samples[-1] >= want_hz * 0.98,
          "尾=%s" % (samples[-2:],))
    # [等进恒速死区再看 runState] 见 _wait_at_speed 的说明（"加速中"≠恒速）。
    at_speed = _wait_at_speed(business, want_hz)
    check("%s：进入恒速（差值 < 0.15Hz，runState 才判正/反转运行）" % tag,
          at_speed, "outHz=%s want=%.2f" % (_vfd_out_hz(business), want_hz))
    # 等一拍遥测刷新，再取变频器快照（字段来自状态字解码）。
    time.sleep(0.25)
    snap = c.snapshot()
    check("%s：变频器快照 reverse=%s" % (tag, bool(reverse)),
          snap.get("reverse") is bool(reverse),
          "snapshot reverse=%r vm=%r" % (snap.get("reverse"),
                                         snap.get(proto.KEY_VALID_MASK)))
    want_state = (proto.RUN_RUNNING_REVERSE if reverse
                  else proto.RUN_RUNNING_FORWARD)
    check("%s：快照 runState=%s（%s）"
          % (tag, want_state, "反转运行" if reverse else "正转运行"),
          snap.get("runState") == want_state,
          "runState=%r" % (snap.get("runState"),))
    model = _vfd_model(business, cfg)
    check("%s：虚拟变频器物理方向=%s" % (tag, bool(reverse)),
          model is not None and bool(getattr(model, "reverse", None))
          is bool(reverse),
          "model.reverse=%r" % (getattr(model, "reverse", None),))


async def _case_stir_reverse(base, log):
    print("\n=== ③ stirReverse 搅拌方向（False/True → reverse + runState）===")
    for reverse in (False, True):
        await _run_case(
            base, log, "rev%d" % (1 if reverse else 0),
            lambda c, b, cfg, lg, reverse=reverse:
                _scenario_stir_reverse(
                    c, b, cfg, lg,
                    "stirReverse=%s" % ("True" if reverse else "False"),
                    reverse))


# ============================================================================
# ④ stirAccelRpmS 搅拌加速度
# ============================================================================

def _scenario_accel(c, business, cfg, log, tag, accel, preset_raw):
    """stirAccelRpmS → 0xF011 满量程加速时间（0.1s 定点原始码）。

    :param preset_raw: 启动**之前**预置的 0xF011 原始码（模拟现场已配的
        加速时间）；accel=0 时必须**保持原值**（证明"不写"而不是"写缺省"）。
    """
    at_addr = 0xF011
    model = _vfd_model(business, cfg)
    assert model is not None, "%s：虚拟变频器模型不可用" % tag
    if preset_raw is not None:
        # 现场已配 20.0s 满量程（=200）——若 accel=0 时服务端仍写参数，回读
        # 会变成 100（缺省 10s），本用例就能抓到。
        model.write_single(6, at_addr, int(preset_raw))
        check("%s：预置 0xF011=%d（%.1fs）" % (tag, preset_raw,
                                              preset_raw / 10.0),
              model.read(3, at_addr, 1)[0] == preset_raw,
              "raw=%r" % (model.read(3, at_addr, 1)[0],))
    _set_cycle(c, [
        _inst("produce", ["valve1", "stirPump"], timer=30, rpm=600,
              accel=accel),
        _inst("rinse", ["valve6"], timer=30),
    ], tag)
    _enter_auto(c, tag)
    _start_cycle(c, tag)
    st = _wait_stage(c, 0, "running", 20.0)
    check("%s：进入运行相（泵相已下发启动参数）" % tag, st is not None, "")
    if st is None:
        return
    raw = _vfd_reg(business, cfg, at_addr)
    if accel > 0:
        want_raw = int(round(RATED_RPM / float(accel) * 10.0))
        check("%s：0xF011 满量程加速时间 = %.1fs（raw=%d，额定/加速度）"
              % (tag, want_raw / 10.0, want_raw),
              raw == want_raw, "raw=%r want=%d" % (raw, want_raw))
    else:
        check("%s：0xF011 保持现场原值 %s（accel=0 ⇒ 不写）"
              % (tag, preset_raw),
              raw == preset_raw, "raw=%r want=%r" % (raw, preset_raw))
    check("%s：启动仍下发（输出开始上升）" % tag,
          _wait_out_rising(business, 3.0),
          "outHz=%s" % (_vfd_out_hz(business),))


def _wait_out_rising(business, timeout=3.0):
    """等输出频率离开 0（证明 START 真的下发了）。

    [阈值取 0.05Hz] 加速度 30rpm/s 时满量程 48s（1.04Hz/s）⇒ 一拍只有
    0.104Hz；阈值取 0.1 就贴着量化边界了（会偶发假红）。
    """
    deadline = time.time() + timeout
    while time.time() < deadline:
        if _vfd_out_hz(business) > 0.05:
            return True
        time.sleep(0.03)
    return False


async def _case_accel(base, log):
    print("\n=== ④ stirAccelRpmS 加速度（0/30/300 → 0xF011）===")
    # (accel, 启动前预置的原始码)。0 ⇒ 预置 200（20.0s）证明"不写"。
    for accel, preset in ((0, 200), (30, None), (300, None)):
        await _run_case(
            base, log, "acc%d" % accel,
            lambda c, b, cfg, lg, accel=accel, preset=preset:
                _scenario_accel(c, b, cfg, lg, "stirAccelRpmS=%d" % accel,
                                accel, preset))


# ============================================================================
# ⑤ 四触发逐项（压差 / 进水压力 / 出水压力 / 定时）
# ============================================================================

def _run_trigger(c, business, cfg, log, tag, inst0, inject, needle,
                 target_title):
    """触发用例骨架：负向窗口（不换相）→ 注入条件 → 换到实例1。

    :param inst0: 源实例（只开被测的那一个触发）。
    :param inject: 注入触发条件（改模拟量 mV / 拨阶段时钟）。
    :param needle: 换相日志里应出现的触发名（压差/进水压力/出水压力/定时）。
    :param target_title: 目标实例标题（cyc 顺序：rinse 是实例 1）。
    """
    _set_cycle(c, [inst0, _inst("rinse", ["valve6"], timer=30)], tag)
    _enter_auto(c, tag)
    _start_cycle(c, tag)
    st = _wait_stage(c, 0, "running", 20.0)
    check("%s：进入运行相（实例0）" % tag, st is not None,
          "phase=%s idx=%s" % ((st or {}).get(proto.KEY_AUTO_PHASE),
                               (st or {}).get(proto.KEY_AUTO_STAGE_INDEX)))
    if st is None:
        return
    # ---- 负向控制：条件未满足 ⇒ 1s 窗口内不换相 ----
    ok, st2 = _no_switch(c, 0, 1.0)
    check("%s：条件未满足时不换相（1s 窗口）" % tag, ok,
          "phase=%s idx=%s" % (st2.get(proto.KEY_AUTO_PHASE),
                               st2.get(proto.KEY_AUTO_STAGE_INDEX)))
    # ---- 注入条件 ⇒ 必须换到实例 1 ----
    mark = len(log)
    inject()
    st3, new_idx = _wait_switch(c, 0, 8.0)
    check("%s：条件满足后换到下一实例（idx=1）" % tag, new_idx == 1,
          "idx=%r state=%s" % (new_idx, st3))
    if st3 is not None:
        check("%s：快照 autoStageTitle=%s" % (tag, target_title),
              st3.get(proto.KEY_AUTO_STAGE_TITLE) == target_title,
              "title=%r mode=%r" % (st3.get(proto.KEY_AUTO_STAGE_TITLE),
                                    st3.get(proto.KEY_AUTO_MODE)))
    lines = _log_lines(log, mark, "自动循环：")
    check("%s：换相日志点名触发原因（%s）" % (tag, needle),
          any(needle in ln for ln in lines),
          "%s" % (lines[-2:] or "无换相日志"))


def _scenario_press(c, business, cfg, log, tag, compare):
    """压差触发（gt / lt 两向）。"""
    thr = 0.6 if compare == proto.AUTO_PRESS_COMPARE_GT else 0.4
    # 缺省压差 ≈ 0.499MPa（2985mV）：gt 0.6 ⇒ 不满足；lt 0.4 ⇒ 不满足。
    inst0 = _inst("produce", ["valve1"], press=(thr, compare), timer=None)
    ch = int(cfg.analog.press_channel)
    target = 0.9 if compare == proto.AUTO_PRESS_COMPARE_GT else 0.10
    _run_trigger(c, business, cfg, log, tag, inst0,
                 lambda: _set_press(business, ch, target),
                 "压差", "顺洗模式")


def _scenario_press1(c, business, cfg, log, tag):
    """进水压力触发（gt）。缺省 CH2 ≈ 0.252MPa ⇒ 阈值 0.4 不满足。"""
    inst0 = _inst("produce", ["valve1"], press1=(0.4,
                                                 proto.AUTO_PRESS_COMPARE_GT))
    ch = int(cfg.analog.press1_channel)
    _run_trigger(c, business, cfg, log, tag, inst0,
                 lambda: _set_press(business, ch, 0.6),
                 proto.PRESS1_LABEL, "顺洗模式")


def _scenario_press2(c, business, cfg, log, tag):
    """出水压力触发（lt）。缺省 CH3 ≈ 0.503MPa ⇒ 阈值 0.4 不满足。"""
    inst0 = _inst("produce", ["valve1"], press2=(0.4,
                                                 proto.AUTO_PRESS_COMPARE_LT))
    ch = int(cfg.analog.press2_channel)
    _run_trigger(c, business, cfg, log, tag, inst0,
                 lambda: _set_press(business, ch, 0.10),
                 proto.PRESS2_LABEL, "顺洗模式")


def _scenario_timer(c, business, cfg, log, tag):
    """定时触发（timerMinutes=1；假时钟拨满）。"""
    inst0 = _inst("produce", ["valve1"], timer=1)
    _run_trigger(c, business, cfg, log, tag, inst0,
                 lambda: _shift_stage_clock(business, 61 * 1000),
                 "定时", "顺洗模式")


async def _case_triggers(base, log):
    print("\n=== ⑤ 四触发逐项（负向窗口 + 注入后换相）===")
    # 压差两向
    for compare in (proto.AUTO_PRESS_COMPARE_GT, proto.AUTO_PRESS_COMPARE_LT):
        await _run_case(
            base, log, "trigpress%s" % compare,
            lambda c, b, cfg, lg, compare=compare: _scenario_press(
                c, b, cfg, lg, "压差(%s)" % compare, compare))
    # 进水压力（gt）
    await _run_case(base, log, "trigpress1",
                    lambda c, b, cfg, lg: _scenario_press1(
                        c, b, cfg, lg, "进水压力(gt)"))
    # 出水压力（lt）
    await _run_case(base, log, "trigpress2",
                    lambda c, b, cfg, lg: _scenario_press2(
                        c, b, cfg, lg, "出水压力(lt)"))
    # 定时（假时钟 1 分钟）
    await _run_case(base, log, "trigtimer",
                    lambda c, b, cfg, lg: _scenario_timer(
                        c, b, cfg, lg, "定时(1分钟)"))


# ============================================================================
# ⑥⑦ 停止/急停时的阀复位开关
# ============================================================================

def _scenario_reset(c, business, cfg, log, tag, value, stop_op):
    """resetValvesOnStop / resetValvesOnEstop → 停止/急停后阀 DO 是否清零。

    [两种动作都必须停动力] 复位开关只影响**阀**：进水泵与变频器无论开关
    都要停（否则"不复位阀"会被误读成"什么都不停"）。
    """
    is_stop = (stop_op == proto.AUTO_OP_STOP)
    kw = {"reset_stop": value} if is_stop else {"reset_estop": value}
    # [两路阀都开] "全清零"要能在**两路**上同时看出来（只有一路时其余 5 路
    # 本来就是 0，判不出"有没有真去关"）；阀1=DO0、阀2=DO1。
    _set_cycle(c, [
        _inst("produce", ["valve1", "valve2", "stirPump", "inletPump"],
              timer=30, rpm=720, accel=1440, **kw),
        _inst("rinse", ["valve6"], timer=30),
    ], tag)
    _enter_auto(c, tag)
    _start_cycle(c, tag)
    st = _wait_stage(c, 0, "running", 20.0)
    check("%s：进入运行相" % tag, st is not None, "")
    if st is None:
        return
    check("%s：前置：阀1/阀2 已开（DO0=DO1=1）" % tag,
          _wait_do(business, 0, True, 4.0)
          and _wait_do(business, 1, True, 3.0),
          "do=%s" % _do_str(business))
    check("%s：前置：进水泵开（DO6=1）" % tag, _wait_do(business, 6, True, 3.0),
          "do=%s" % _do_str(business))
    check("%s：前置：搅拌泵在转" % tag, _wait_vfd_running(business, True, 4.0),
          "outHz=%s" % (_vfd_out_hz(business),))
    # ---- 下发停止 / 急停 ----
    r = c.request(proto.CMD_AUTO_CONTROL,
                  bson.encode({"op": stop_op}), wait=5.0)
    check("%s：%s命令被受理" % (tag, "停止" if is_stop else "急停"),
          r is not None and r[1].get("ok") is True,
          "%r" % (r[1] if r else None))
    # ---- 阀：按配置 ----
    if value:
        closed = (_wait_do(business, 0, False, 5.0)
                  and _wait_do(business, 1, False, 3.0))
        check("%s：%s后阀1/阀2 DO 全清 0（复位关回）"
              % (tag, "停止" if is_stop else "急停"), closed,
              "do=%s" % _do_str(business))
    else:
        # 给"本该发的关阀命令"一个可观测窗口（若误发了，DO 会在此窗口内变 0）
        time.sleep(1.0)
        check("%s：%s后阀1/阀2 DO 保持 1（配置为不复位）"
              % (tag, "停止" if is_stop else "急停"),
              _do(business, 0) == 1 and _do(business, 1) == 1,
              "do=%s" % _do_str(business))
    # ---- 动力必须停（与阀开关无关）----
    check("%s：进水泵 DO6 清 0" % tag, _wait_do(business, 6, False, 4.0),
          "do=%s" % _do_str(business))
    check("%s：变频器停（不再运行）" % tag,
          _wait_vfd_running(business, False, 5.0),
          "outHz=%s" % (_vfd_out_hz(business),))
    if not is_stop:
        check("%s：快照相位=estop（急停已锁存）" % tag,
              _state(c).get(proto.KEY_AUTO_PHASE) == "estop",
              "phase=%r" % (_state(c).get(proto.KEY_AUTO_PHASE),))
        # [急停会锁存] 复位（op=4）解除锁存并回停止态 —— 现场要按了复位才能
        # 再次启动。本用例每个子场景都是全新服务端（不存在"上一组残留锁存"
        # 需要先复位），这里只是把"复位可解锁"这条口径顺手走通。
        r2 = c.request(proto.CMD_AUTO_CONTROL,
                       bson.encode({"op": proto.AUTO_OP_RESET}), wait=5.0)
        check("%s：复位解除急停锁存（相位回停止态）" % tag,
              r2 is not None and r2[1].get("ok") is True
              and _state(c).get(proto.KEY_AUTO_PHASE) != "estop",
              "%r phase=%r" % (r2[1] if r2 else None,
                               _state(c).get(proto.KEY_AUTO_PHASE)))


async def _case_reset_stop(base, log):
    print("\n=== ⑥ resetValvesOnStop（True→关阀 / False→保持）===")
    for value in (True, False):
        await _run_case(
            base, log, "stopreset%d" % (1 if value else 0),
            lambda c, b, cfg, lg, value=value: _scenario_reset(
                c, b, cfg, lg,
                "resetValvesOnStop=%s" % ("True" if value else "False"),
                value, proto.AUTO_OP_STOP))


async def _case_reset_estop(base, log):
    print("\n=== ⑦ resetValvesOnEstop（True→关阀 / False→保持）===")
    for value in (True, False):
        await _run_case(
            base, log, "estopreset%d" % (1 if value else 0),
            lambda c, b, cfg, lg, value=value: _scenario_reset(
                c, b, cfg, lg,
                "resetValvesOnEstop=%s" % ("True" if value else "False"),
                value, proto.AUTO_OP_ESTOP))


# ============================================================================
# ⑧ stirMaxRpm 钳位
# ============================================================================

def _scenario_stir_max(c, business, cfg, log, tag, max_rpm, inst_rpm):
    """stirMaxRpm=800 + 实例 stirRpm=1440 ⇒ 保存即夹到 800，给定也 ≤800。"""
    # [同包顺序] 服务端先写 stirMaxRpm 再写 instances，后者按**新上限**夹取
    # （见 app._h_config_set 的「限速收口」注释）⇒ 同一个 SET 里发两项最能
    # 验证这条链路。
    _set_cycle(c, [
        _inst("produce", ["valve1", "stirPump"], timer=30, rpm=inst_rpm,
              accel=1440),
        _inst("rinse", ["valve6"], timer=30),
    ], tag, extra={proto.AUTO_STIR_MAX_RPM: int(max_rpm)})
    got = _get_instances(c)
    echo = int((got[0] if got else {}).get(proto.CYCLE_INST_STIR_RPM, -1))
    check("%s：GET 回显实例 stirRpm 被夹到 %d（配置 %d）"
          % (tag, max_rpm, inst_rpm), echo == int(max_rpm),
          "instances[0].stirRpm=%r" % (echo,))
    _enter_auto(c, tag)
    _start_cycle(c, tag)
    st = _wait_stage(c, 0, "running", 20.0)
    check("%s：进入运行相" % tag, st is not None, "")
    if st is None:
        return
    cap_hz = float(max_rpm) / RATED_RPM * RATED_HZ
    # 等轮询缓存跟上（设定频率读的是缓存，见 _wait_set_hz 的说明）。
    got_hz = _wait_set_hz(business, cap_hz)
    check("%s：变频器给定 ≤ %drpm（%.2fHz）" % (tag, max_rpm, cap_hz),
          got_hz is not None and got_hz <= cap_hz + 0.2,
          "setFreqHz=%r cap=%.3f" % (got_hz, cap_hz))
    check("%s：给定确实按上限（≈%.2fHz，不是 1440rpm 的 50Hz）"
          % (tag, cap_hz),
          got_hz is not None and abs(got_hz - cap_hz) <= 0.2,
          "setFreqHz=%r" % (got_hz,))


async def _case_stir_max(base, log):
    print("\n=== ⑧ stirMaxRpm 钳位（800 上限 vs 实例 1440）===")
    await _run_case(base, log, "stirmax",
                    lambda c, b, cfg, lg: _scenario_stir_max(
                        c, b, cfg, lg, "stirMaxRpm", 800, 1440))


# ============================================================================
# ⑨ 实例顺序 / 重复 mode / 序号标题
# ============================================================================

def _scenario_order(c, business, cfg, log, tag):
    """[产水, 顺洗, 产水]：GET 顺序不变、标题带 #N、按列表顺序推进 + 回绕。"""
    insts = [
        # 设备刻意不同（阀1/阀2/阀4）：顺序推进时阀目标也跟着换 ⇒ 顺带锁
        # "每个实例各带自己的 devices"。
        _inst("produce", ["valve1"], timer=1, rpm=720),
        _inst("rinse", ["valve2"], timer=1),
        _inst("produce", ["valve4"], timer=1),
    ]
    _set_cycle(c, insts, tag)
    got = _get_instances(c)
    modes = [i.get("mode") for i in got]
    check("%s：GET 实例顺序不变（produce→rinse→produce）" % tag,
          modes == ["produce", "rinse", "produce"], "modes=%s" % (modes,))
    # [2026-11-27 标题口径] 服务端状态行用**展开计划**的 `_title`
    # （「产水模式1」数字后缀，不含旧日志的 `#`）⇒ 期望同样走 expand。
    want_insts = [proto.default_cycle_instance(m)
                  for m in ("produce", "rinse", "produce")]
    want_titles = proto.cycle_instance_titles(
        proto.expand_cycle_plan(want_insts))
    check("%s：实例标题按同模式序号计数（服务端 cycle_instance_titles 同源）"
          % tag, want_titles == ["产水模式1", "顺洗模式", "产水模式2"],
          "%s" % (want_titles,))
    _enter_auto(c, tag)
    _start_cycle(c, tag)
    st = _wait_stage(c, 0, "running", 20.0)
    check("%s：起始实例=0 且标题=%s" % (tag, want_titles[0]),
          st is not None
          and st.get(proto.KEY_AUTO_STAGE_TITLE) == want_titles[0],
          "title=%r phase=%r" % ((st or {}).get(proto.KEY_AUTO_STAGE_TITLE),
                                 (st or {}).get(proto.KEY_AUTO_PHASE)))
    if st is None:
        return
    # 逐个推进：定时 1 分钟 + 假时钟拨满。
    cur = 0
    for want_idx in (1, 2, 0):          # 末位回绕首位 ⇒ 2 → 0
        _shift_stage_clock(business, 61 * 1000)
        st2, idx = _wait_switch(c, cur, 8.0)
        check("%s：按列表顺序推进到实例 %d" % (tag, want_idx), idx == want_idx,
              "idx=%r state=%s" % (idx, st2))
        if idx != want_idx:
            return
        check("%s：实例 %d 标题=%s" % (tag, want_idx, want_titles[want_idx]),
              st2.get(proto.KEY_AUTO_STAGE_TITLE) == want_titles[want_idx],
              "title=%r mode=%r" % (st2.get(proto.KEY_AUTO_STAGE_TITLE),
                                    st2.get(proto.KEY_AUTO_MODE)))
        # 换相后要走完阀相/泵相才能再次计时换相（下一轮 _cycle_tick 只在
        # running 相跑）；阀 250ms 行程 + 两拍轮询 ⇒ 给 12s 余量。
        st3 = _wait_stage(c, want_idx, "running", 15.0)
        check("%s：实例 %d 进入运行相" % (tag, want_idx), st3 is not None,
              "phase=%r" % ((st3 or {}).get(proto.KEY_AUTO_PHASE),))
        if st3 is None:
            return
        # 阀目标按**该实例**的 devices 定型（三个实例各一路：阀1/阀2/阀4）。
        # [逐阀核对 2026-11-27] 不只查目标阀开、也查**未选阀关回**——换相
        # 若不关旧阀，上一实例的阀会残留（"设备组合"段只覆盖首实例，
        # 换相路径要单独锁）。
        want_devs = (["valve1"], ["valve2"], ["valve4"])[want_idx]
        bad = []
        for vi in range(1, 7):
            want = 1 if ("valve%d" % vi) in want_devs else 0
            got = _do(business, vi - 1)
            if got != want:
                bad.append("阀%d=%s(期望%d)" % (vi, got, want))
        check("%s：实例 %d 的 devices 驱动阀目标（阀%s 开、余阀关）"
              % (tag, want_idx, "/".join(want_devs)),
              not bad, "do=%s 失配=%s" % (_do_str(business), bad))
        cur = want_idx


async def _case_order(base, log):
    print("\n=== ⑨ 实例顺序 / 重复 mode / 序号标题 ===")
    await _run_case(base, log, "order",
                    lambda c, b, cfg, lg: _scenario_order(c, b, cfg, lg,
                                                          "实例顺序"))


# ============================================================================
# main
# ============================================================================

def main():
    base = tempfile.mkdtemp(prefix="hsf_auto_pm_")
    log = []
    try:
        asyncio.run(_case_devices(base, log))
        asyncio.run(_case_stir_rpm(base, log))
        asyncio.run(_case_stir_reverse(base, log))
        asyncio.run(_case_accel(base, log))
        asyncio.run(_case_triggers(base, log))
        asyncio.run(_case_reset_stop(base, log))
        asyncio.run(_case_reset_estop(base, log))
        asyncio.run(_case_stir_max(base, log))
        asyncio.run(_case_order(base, log))
    finally:
        shutil.rmtree(base, ignore_errors=True)
    print("\n汇总：PASS %d / FAIL %d" % (PASS, FAIL))
    return 0 if FAIL == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
