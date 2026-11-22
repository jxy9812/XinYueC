# -*- coding: utf-8 -*-
"""2026-11-15 全量审计整改的回归锁（Python 服务端侧）。

[背景] 这一轮把 Python 服务端/客户端 + C 服务端/客户端都过了一遍，找出并
       修掉了一批"平时跑得好、遇到边界就出事"的缺陷。本文件锁住其中**服务
       端侧**的几条，其余在各自的文件里：

    A. **换相策略 carry 的收尾停泵**（autocontrol._stop_pumps_agg）
       ——原来把「返回列表」的 _stop_pumps() 塞进按单结果验收的泵相任务表
       ⇒ 解包炸 ValueError ⇒ **换相收尾停泵一失败就把整个模式中止**，
       而且报的错驴唇不对马嘴。
    B. **配置读文件的容错**（config.load）
       ——手改配置把某个值写成字符串/对象/数组时原来直接抛 ⇒ **服务端起不
       来**（配置笔误变现场事故）。
    C. **落盘写库的"连接没了"路径**（store._write）
       ——rotate 失败把连接置空后，写库抛 AttributeError（不在 except
       sqlite3.Error 内）⇒ 从日志调用点冒泡 ⇒ 打断正在处理异常的代码。
    D. **手动模式"现场在动"的判定**（autocontrol.manual_running）
       ——变频器掉线后最后一帧遥测还留着（输出频率 25Hz、状态字"运行中"）
       ⇒ 启动灯被永久钉亮（现场什么设备都没在跑，灯却亮着）。
    E. **配置 SET 严格布尔**（app.strict_bool）
       ——`bool("false")` 是 True ⇒ "只报警不停机"被当成"直接停机"。

[怎么跑]
    python3 -I tests/test_audit_hardening.py
"""

import asyncio
import json
import os
import shutil
import sys
import tempfile

# [输出编码 2026-11-14] 中文 Windows 默认代码页 cp936 ⇒ stdout 被重定向
# 到文件时用 GBK ⇒ 打印「⇒」(U+21D2，**GBK 编不了**) 直接
# UnicodeEncodeError ⇒ **整套测试在打印断言时被打断**（退出码仍是 0）。
for _stream in (sys.stdout, sys.stderr):
    try:
        _stream.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError, OSError):
        pass

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.dirname(HERE))

from hsfsrv import autocontrol                # noqa: E402
from hsfsrv import config as cfgmod           # noqa: E402
from hsfsrv import proto                      # noqa: E402
from hsfsrv import store as store_mod         # noqa: E402

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
#                    A 换相 carry 收尾：停泵结果必须汇总成单结果
# ============================================================================

class _FakePumpItem(object):
    def __init__(self, on=False):
        self.on = on


class _FakePumpBank(object):
    """假泵组：set_pump 可控成功/失败。"""

    def __init__(self, ok=True):
        self.items = [_FakePumpItem(False), _FakePumpItem(False)]
        self.ok = ok
        self.calls = []

    async def set_pump(self, index, on):
        self.calls.append((index, on))
        if not self.ok:
            return False, "写入失败"
        self.items[index].on = bool(on)
        return True, ""


def _ctx_with_bank(bank):
    """造一个只带泵组的最小 AutoContext。"""
    ctx = autocontrol.AutoContext.__new__(autocontrol.AutoContext)

    class _App(object):
        pump_bank = None
        cfg = None

    app = _App()
    app.pump_bank = bank
    ctx._app = app
    ctx._log = lambda m: None
    return ctx


async def test_carry_stop_pumps():
    print("=== A 换相 carry 收尾停泵（结果汇总）===")
    # ① 全部成功 ⇒ 单结果 (True, "")。
    bank = _FakePumpBank(ok=True)
    ctl = autocontrol.AutoController(_ctx_with_bank(bank), log=lambda m: None)
    ok, err = await ctl._stop_pumps_agg()
    check("A1 全部停成功 ⇒ (True, 空)",
          ok is True and not err, "ok=%r err=%r" % (ok, err))
    check("A1b 两台泵都停了",
          len(bank.calls) == 2 and all(not c for _i, c in bank.calls),
          "calls=%r" % (bank.calls,))

    # ② 有一台失败 ⇒ 单结果 (False, 带原因) —— **不是**列表。
    # [为什么] 泵相任务表按单结果验收（ok, err = t.result()）⇒ 塞列表进去
    # 会炸 ValueError ⇒ 被当成"泵启动失败" ⇒ 整个模式中止。
    bank = _FakePumpBank(ok=False)
    ctl = autocontrol.AutoController(_ctx_with_bank(bank), log=lambda m: None)
    try:
        ok, err = await ctl._stop_pumps_agg()
        check("A2 停泵失败 ⇒ 汇总成 (False, 原因)",
              ok is False and "进水泵" in str(err),
              "ok=%r err=%r" % (ok, err))
    except (TypeError, ValueError) as exc:
        check("A2 停泵失败 ⇒ 汇总成 (False, 原因)", False,
              "解包炸了：%s: %s" % (type(exc).__name__, exc))

    # ③ 未装配泵组 ⇒ (True, "")（不是异常）。
    ctl = autocontrol.AutoController(_ctx_with_bank(None), log=lambda m: None)
    ok, err = await ctl._stop_pumps_agg()
    check("A3 未装配泵组 ⇒ (True, 空)", ok is True and not err,
          "ok=%r err=%r" % (ok, err))


# ============================================================================
#                       B 配置读文件容错（笔误不该让服务起不来）
# ============================================================================

def test_config_load_tolerance():
    print("=== B 配置读文件容错 ===")
    basedir = tempfile.mkdtemp(prefix="hsf_cfg_tol_")
    try:
        cfgdir = os.path.join(basedir, "cfg")
        os.makedirs(cfgdir, exist_ok=True)
        path = os.path.join(cfgdir, "hsf_server_py.json")
        # 一份"什么都写错了"的配置：节不是对象、值类型不对、字符串当数字。
        with open(path, "w", encoding="utf-8") as fh:
            json.dump({
                "server": "not-an-object",
                "serial": ["not", "an", "object"],
                "analog": {"port": "COM-x", "poll_ms": "abc"},
                "valves": {"items": "nope"},
                "buttons": {"debounce_ms": "x"},
                "stale": {"vfd_ms": "y", "flowmeter_ms": None},
                "storage": {"sample_ms": "zz", "max_mb": "no"},
                "protect": {"inlet_noflow_timeout_ms": "abc",
                            "inlet_noflow_stop": "false",
                            "press_high_limit_mpa": "x"},
                "auto": "not-an-object",
                "login": {"allow_anonymous": "false", "max_sessions": "x"},
                "runtime": {"auto_run_ms": "x"},
            }, fh)

        cfg = cfgmod.Config()
        cfg.resolve_paths(basedir)
        cfg.config_path = path
        try:
            cfg.load()
            ok = True
            err = ""
        except Exception as exc:                      # noqa: BLE001
            ok = False
            err = "%s: %s" % (type(exc).__name__, exc)
        check("B1 写坏类型的配置**不会让服务起不来**", ok, err)

        if ok:
            check("B2 非法数值回缺省（不采信错误类型）",
                  cfg.serial.vfd_poll_ms == 100
                  and cfg.analog.poll_ms == 200,
                  "vfd=%r analog=%r" % (cfg.serial.vfd_poll_ms,
                                        cfg.analog.poll_ms))
            # [字符串 "false" 必须按 false 理解] 原来 bool("false") == True
            # ⇒ "只报警不停机"被读成"停机"（现场误停线）。
            check("B3 字符串 \"false\" 按 false 读（不是 True）",
                  cfg.protect_inlet_noflow_stop is False,
                  "stop=%r" % (cfg.protect_inlet_noflow_stop,))
            check("B4 字符串 \"false\" 的登录策略同理",
                  cfg.login_allow_anonymous is False,
                  "anon=%r" % (cfg.login_allow_anonymous,))
            check("B5 压差上限非法值回缺省 0.8",
                  abs(cfg.protect_press_high_limit_mpa
                      - proto.PROTECT_PRESS_HIGH_LIMIT_MPA_DEF) < 1e-9,
                  "limit=%r" % (cfg.protect_press_high_limit_mpa,))

        # ---- B6 陈旧阈值必须**存得下**（原来读得到、存不下 ⇒ 任何一次
        #      save() 都会把现场手改的阈值抹掉）----
        cfg2 = cfgmod.Config()
        cfg2.resolve_paths(basedir)
        cfg2.config_path = path
        cfg2.load()
        cfg2.flowmeter_stale_ms = 12345
        cfg2.turbidity_stale_ms = 23456
        cfg2.save()
        with open(path, "r", encoding="utf-8") as fh:
            saved = json.load(fh)
        check("B6 流量计/浊度陈旧阈值**写进落盘文件**",
              saved.get("stale", {}).get("flowmeter_ms") == 12345
              and saved.get("stale", {}).get("turbidity_ms") == 23456,
              "stale=%r" % (saved.get("stale"),))
    finally:
        shutil.rmtree(basedir, ignore_errors=True)


# ============================================================================
#                      C 落盘写库：连接没了也不能抛异常
# ============================================================================

def test_store_write_without_connection():
    print("=== C 落盘写库（连接没了）===")
    st = store_mod.Store.__new__(store_mod.Store)
    st._conn = None
    st._open_path = None
    st._pause_reason = ""
    st._max_bytes = 0
    st._before_write = lambda: True
    try:
        ok = st._write("INSERT INTO logs(ts, text) VALUES(?, ?)",
                       (1, "x"))
        check("C1 连接为空 ⇒ 安静返回 False（不抛 AttributeError）",
              ok is False, "ret=%r" % (ok,))
    except Exception as exc:                          # noqa: BLE001
        check("C1 连接为空 ⇒ 安静返回 False（不抛 AttributeError）", False,
              "%s: %s" % (type(exc).__name__, exc))


# ============================================================================
#              D 手动模式"现场在动"：遥测过期/掉线不算还在跑
# ============================================================================

class _FakeVfd(object):
    """假变频器：可控制"运行中/输出频率/健康"。"""

    def __init__(self):
        self.running = True
        self.hz = 25.0
        self.health_result = (proto.HEALTH_OK, 0, True, "")

    def is_running(self):
        return self.running

    def output_freq_hz(self):
        return self.hz

    def health(self, now_ms, stale_ms):
        return self.health_result


def test_manual_running_validity():
    print("=== D 手动模式「现场在动」的判定 ===")
    vfd = _FakeVfd()
    ctx = autocontrol.AutoContext.__new__(autocontrol.AutoContext)

    class _App(object):
        vfd = None
        cfg = None

        def _now_ms(self):
            return 100000

    app = _App()
    app.vfd = vfd

    class _Cfg(object):
        vfd_stale_ms = 5000

    app.cfg = _Cfg()
    ctx._app = app
    ctx._log = lambda m: None

    # ① 遥测新鲜且在跑 ⇒ 在动。
    vfd.health_result = (proto.HEALTH_OK, 0, True, "")
    check("D1 遥测新鲜且在运行 ⇒ 在动", ctx.manual_running() is True)

    # ② 遥测**过期**（掉线后最后一帧还留着）⇒ 不算在动。
    # [为什么] 现场什么设备都没在跑，启动灯却永久亮着 ⇒ 操作员误判。
    vfd.health_result = (proto.HEALTH_STALE, 0, True, "无有效遥测")
    check("D2 遥测过期（陈旧快照）⇒ 不算在动",
          ctx.manual_running() is False)

    # ③ 从站失联 ⇒ 不算在动。
    vfd.health_result = (proto.HEALTH_OFFLINE, 0, False, "从站失联")
    check("D3 从站失联 ⇒ 不算在动", ctx.manual_running() is False)

    # ④ 判不出来（没装配 cfg）⇒ 放行（宁可误亮不可误灭）。
    app.cfg = None
    check("D4 判不出来时放行（沿用旧口径）",
          ctx.manual_running() is True)


# ============================================================================
#                     E 配置 SET 的严格布尔（安全语义）
# ============================================================================

def test_strict_bool():
    print("=== E 配置 SET 严格布尔 ===")
    from hsfsrv.app import strict_bool
    check("E1 True/False 原样通过",
          strict_bool(True, "k") is True and strict_bool(False, "k") is False)
    check("E2 0/1 当布尔（无歧义）",
          strict_bool(0, "k") is False and strict_bool(1, "k") is True)
    for bad in ("false", "true", "0", "yes", None, 2, 1.5, []):
        try:
            strict_bool(bad, "k")
            check("E3 非法布尔 %r 被拒" % (bad,), False, "未抛异常")
        except ValueError:
            check("E3 非法布尔 %r 被拒" % (bad,), True)


# ============================================================================
#                          F 自动模式（配置面）严格布尔
# ============================================================================

def test_auto_config_strict_bool():
    print("=== F 自动模式配置的布尔键 ===")
    from hsfsrv.app import strict_bool
    # stirReverse / resetValvesOnStop / resetValvesOnEstop 都走 strict_bool
    # ⇒ "false" 字符串不会再被当成 True（方向反了 / 阀复位行为反了）。
    for key in (proto.AUTO_MODE_RESET_VALVES_ON_STOP,
                proto.AUTO_MODE_RESET_VALVES_ON_ESTOP):
        try:
            strict_bool("false", key)
            check("F1 %s 的字符串布尔被拒" % key, False, "未抛异常")
        except ValueError:
            check("F1 %s 的字符串布尔被拒" % key, True)


# ============================================================================
#          G 服务端 tick 的分步护栏：同步步骤不许被 await（2026-11-15 实锅）
# ============================================================================

async def test_tick_step_guards():
    """服务端 tick 的每一步都要能跑，且**不许**抛异常。

    [实锅] 本轮给 tick 加"分步护栏"（一步坏只坏它自己）时，把**同步**的
    `_jog_watchdog` 也当协程 await 了 ⇒ 每拍抛
    "TypeError: object NoneType can't be used in 'await' expression"
    ⇒ **从这一步起整拍全废**（点动看门狗从没跑过），而日志只有一句
    "自动控制异常" ⇒ 极难定位。
    ⇒ 护栏用 `inspect.isawaitable` 判；本用例锁住"tick 不抛 + 看门狗真的
    跑了"。
    """
    print("=== G 服务端 tick 分步护栏 ===")
    import asyncio as _asyncio
    from hsfsrv import config as cfgmod
    from fake_devices import build_fake_app

    logs = []
    basedir = tempfile.mkdtemp(prefix="hsf_tick_guard_")
    try:
        cfg = cfgmod.Config()
        cfg.resolve_paths(basedir)
        cfg.config_path = os.path.join(basedir, "cfg", "x.json")
        os.makedirs(os.path.join(basedir, "cfg"), exist_ok=True)
        cfg.port = 0
        cfg.enable_serial = False
        business = build_fake_app(cfg, log=logs.append)

        # 假服务端（**照抄真形状**：真 xlink.Server.publish 是 async ⇒
        # 假件也必须是 async；假件写成同步会自己炸出 "await None"）。
        class _SyncServer(object):
            def __init__(self):
                self.published = []

            async def publish(self, topic, payload, **kw):
                self.published.append((topic, payload))
                return 1

        business.server = _SyncServer()

        # ① tick 不抛异常。
        try:
            await business(1000)
            ok = True
            err = ""
        except Exception as exc:                      # noqa: BLE001
            ok = False
            err = "%s: %s" % (type(exc).__name__, exc)
        check("G1 服务端 tick 不抛异常（分步护栏吃得下同步步骤）", ok, err)

        # ② 日志里没有"自动控制异常"（护栏本身没炸）。
        bad = [m for m in logs if "异常" in str(m)]
        check("G2 tick 日志无「异常」条目",
              not bad, "%r" % (bad[:2],))

        # ③ 同步步骤**真的跑了**（点动看门狗：它会在超时后停机 ⇒ 这里只
        # 验"跑过"，用它的副作用：日志/状态变化）。
        # [怎么验] 看门狗内部会调 auto_controller 的操作 ⇒ 这里直接查
        # "看门狗被调用过"：给它一个假的 jog 对象，调用后看计数。
        calls = []
        business._jog_watchdog = lambda now_ms: calls.append(now_ms)
        await business(2000)
        check("G3 同步步骤（点动看门狗）确实被调用了",
              len(calls) >= 1, "calls=%r" % (calls,))
    finally:
        shutil.rmtree(basedir, ignore_errors=True)


# ============================================================================
#      H 第二轮审计（2026-11-16）：运行中 op=5 / 历史热身门 / 急停 None 持续
# ============================================================================

async def test_round2_hardening():
    """第二轮全量审计的回归锁（服务端侧的高优先级三条）。

    H1. **运行中 op=5 拒绝**：Python 控制器的 mode 同时是组合循环的当前
        子阶段 ⇒ 运行中被远程改写会打断生产（产水跑到一半被按"顺洗时限"
        判到期 ⇒ 换相、停泵）。⇒ app 的 op=5 在 ctl.running 时明确拒绝。
    H2. **历史热身门只拦"该出数的设备"**：停用浊度2 / 关掉数字量 /
        模拟量未标定 ⇒ 对应字段永远 None ⇒ 原来整表一行不写（静默）。
        ⇒ 停用/未装配/未标定的组不参与判定。
    H3. **急停"状态未知"要持续才锁存**：单拍读不到（总线抖动）不算急停；
        连续 ESTOP_NONE_LATCH_TICKS 拍仍无数据才锁存。False（回路确实
        不通）仍然立即锁存（安全侧）。
    """
    print("=== H 第二轮审计整改 ===")
    import asyncio as _asyncio
    from hsfsrv import bson
    from hsfsrv import config as cfgmod
    from fake_devices import build_fake_app

    # ---- H1 运行中 op=5 拒绝（handler 级闸）----
    basedir = tempfile.mkdtemp(prefix="hsf_round2_")
    try:
        cfg = cfgmod.Config()
        cfg.resolve_paths(basedir)
        cfg.config_path = os.path.join(basedir, "cfg", "x.json")
        os.makedirs(os.path.join(basedir, "cfg"), exist_ok=True)
        cfg.port = 0
        cfg.enable_serial = False
        business = build_fake_app(cfg, log=lambda m: None)

        class _Server(object):
            async def publish(self, topic, payload, **kw):
                return 1

        business.server = _Server()
        ctl = business.auto_controller
        # 挂最小 auto 配置并强制"运行中"。
        cfg.auto = {"produce": {"devices": ["valve1"], "stir_rpm": 1440,
                                "stir_reverse": False}}
        ctl._running = True

        replies = []

        class _Sess(object):
            pass

        async def _reply(session, rid, code, ok, err=None, extra=None):
            replies.append((ok, err))

        business._reply = _reply
        # [权限闸放行] handler 第一闸是 AccountAdmin（session 无角色 ⇒ 拒）；
        # 本用例只验"运行中 op=5"这一条，权限闸用假会话角色绕过。
        import hsfsrv.permissions as _perm
        _orig_can = _perm.can
        _perm.can = lambda session, perm: True
        payload = bson.encode({"op": proto.AUTO_OP_SELECT,
                               proto.KEY_AUTO_MODE: "backwash"})
        try:
            await business._h_auto_control(_Sess(), 1,
                                           proto.CMD_AUTO_CONTROL, payload)
        finally:
            _perm.can = _orig_can
        # [2026-11-18 契约变更] 运行中 op=5 由「拒绝」改为「受理，但只记
        # **选择**」：Python 端已把粘性（selected_mode）与运行中模式/
        # 循环子阶段（mode）分账 ⇒ 运行中改选不再改写正在跑的工序。
        # C 端同口径放开（HsfServerApp.c / HsfServerAutoMode.c）。
        check("H1 运行中 op=5 被受理（只记选择、不动运行中模式）",
              bool(replies) and replies[0][0] is True
              and ctl.mode != "backwash"
              and getattr(ctl, "selected_mode", "") == "backwash",
              "replies=%r mode=%r sel=%r"
              % (replies, ctl.mode, getattr(ctl, "selected_mode", "")))

        # 停止态的 op=5 照常受理（粘性语义不变）。
        ctl._running = False
        replies.clear()
        _perm.can = lambda session, perm: True
        try:
            await business._h_auto_control(_Sess(), 2,
                                           proto.CMD_AUTO_CONTROL, payload)
        finally:
            _perm.can = _orig_can
        check("H1b 停止态 op=5 照常受理（粘性=反洗）",
              bool(replies) and replies[0][0] is True
              and ctl.mode == "backwash",
              "replies=%r mode=%r" % (replies, ctl.mode))

        # ---- H2 历史热身门：停用数字量 ⇒ 该组不拦门 ----
        # [只测 digital 一组] 其余组先全部预热（等价"已见过有效值"），
        # 免得 FakeVfd 的组也被本用例的最小 row 卡住（那是热身门的正常
        # 瞬态行为，别混进来）。
        business._hist_warm = {"vfd", "temphum", "turbidity", "flow",
                               "press"}
        cfg.enable_digital = False
        business.digital = []
        row = {"ts": 1, "di": None, "do": None}
        check("H2 停用数字量：行不因 di/do 空洞被拦",
              business._history_row_ready(row) is True)
        # 启用**且有模块**但还没值 ⇒ 仍拦（启动瞬态口径不变）；启用了但
        # 没装配任何模块 ⇒ 同样不拦（永远不会有值，拦了=永远不落盘）。
        cfg.enable_digital = True
        business.digital = []
        business._hist_warm.discard("digital")   # 上一拍把它记成"已满足"了
        check("H2b 启用但无模块：不拦（该组永远不会有值）",
              business._history_row_ready(row) is True)
        business.digital = [object()]
        business._hist_warm.discard("digital")
        check("H2c 启用且有模块但还没值：仍拦（启动瞬态口径不变）",
              business._history_row_ready(row) is False)
    finally:
        shutil.rmtree(basedir, ignore_errors=True)

    # ---- H3 急停 None 持续才锁存（纯状态机，直接驱动）----
    ctl = autocontrol.AutoController.__new__(autocontrol.AutoController)
    ctl._estop_latched = False
    ctl._estop_ever_ok = True
    ctl._estop_none_streak = 0
    ctl._estop_boot_warned = True
    ctl._fault_latched = False
    ctl.phase = "running"
    ctl._running = True
    ctl._log = lambda m: None
    estop_reads = [None] * 99        # ctx.estop_ok() 恒 None
    ctx = autocontrol.AutoContext.__new__(autocontrol.AutoContext)
    ctx.estop_ok = lambda: estop_reads[0]
    ctx.estop_configured = lambda: True
    ctl.ctx = ctx
    estop_calls = []
    # [假 e_stop 也要置锁存位] 真正的 e_stop() 会置 _estop_latched；替换
    # 成探针后要自己置，否则断言锁存状态读不到。
    def _fake_estop(reason=""):
        estop_calls.append(reason)
        ctl._estop_latched = True
    ctl.e_stop = _fake_estop

    for i in range(autocontrol.AutoController.ESTOP_NONE_LATCH_TICKS - 1):
        ctl._check_estop(True)
    check("H3 None 未到持续拍数：不锁存",
          not ctl.estop_latched and not estop_calls,
          "streak=%d" % (ctl._estop_none_streak,))
    ctl._check_estop(True)           # 第 N 拍：持续够 ⇒ 锁存
    check("H3b None 持续到拍数：按「状态未知持续」锁存",
          ctl.estop_latched and bool(estop_calls)
          and "未知" in estop_calls[0],
          "calls=%r" % (estop_calls,))
    # 恢复正常后计数清零（下一次抖动重新起算）。
    estop_reads[0] = True
    ctl._estop_latched = False
    ctl._check_estop(True)
    check("H3c 恢复正常：计数清零",
          ctl._estop_none_streak == 0)
    # False（回路确实不通）仍然立即锁存（安全侧）。
    estop_reads[0] = False
    ctl._check_estop(True)
    check("H3d False（回路不通）仍立即锁存",
          ctl.estop_latched and bool(estop_calls)
          and "按下或断线" in estop_calls[-1],
          "calls=%r" % (estop_calls,))


# ============================================================================
#   I 数据三页（2026-11-17）：落盘未启用明确报错 + 分页应答超帧上限兜底
# ============================================================================

class _ReplySink(object):
    """记录 send_response 的假会话。"""

    def __init__(self):
        self.frames = []

    async def send_response(self, request_id, msg_code, payload):
        self.frames.append((request_id, msg_code, payload))


async def test_data_pages_guards():
    """I. 分页命令的两道服务端护栏。

    I1. **落盘未启用时明确报错**：原来 0x0119 直接回空表 ⇒ 客户端日志页
        显示「共 0 条」（像"日志真的没有"），而不是"没开落盘"。历史页早就
        有这道闸，两页口径统一。
    I2. **应答超单帧上限兜底**：客户端对 >16KB 的帧判"帧头非法" ⇒ 整个
        会话断链重连（一翻页就掉线）。服务端现在组帧前量大小，超限改回
        短失败包。当前 19 列×50 行实测 11.9KB（余量 27%），是给文本列变长
        留的保险。
    """
    print("=== I 数据三页：落盘闸 + 超帧兜底 ===")
    import asyncio as _asyncio
    from hsfsrv import bson, config as cfgmod, proto
    from fake_devices import build_fake_app

    basedir = tempfile.mkdtemp(prefix="hsf_pages_")
    try:
        cfg = cfgmod.Config()
        cfg.resolve_paths(basedir)
        cfg.config_path = os.path.join(basedir, "cfg", "x.json")
        os.makedirs(os.path.join(basedir, "cfg"), exist_ok=True)
        cfg.port = 0
        cfg.enable_serial = False
        business = build_fake_app(cfg, log=lambda m: None)

        # ---- I1 落盘未启用：日志页明确报错 ----
        business.store.close()                    # db_path 变 None
        sink = _ReplySink()
        await business._h_log_page(sink, 7, proto.CMD_LOG_PAGE,
                                   bson.encode({"offset": 0, "limit": 20}))
        doc = bson.decode(sink.frames[0][2]) if sink.frames else {}
        check("I1 落盘未启用：日志页回明确错误（不是空表）",
              doc.get("ok") is False
              and "落盘未启用" in str(doc.get("err")),
              "%r" % (doc,))
        # 历史页同口径（既有行为，一起锁住）
        sink.frames.clear()
        await business._h_history_page(sink, 8, proto.CMD_HISTORY_PAGE,
                                       bson.encode({"offset": 0, "limit": 20}))
        doc = bson.decode(sink.frames[0][2]) if sink.frames else {}
        check("I1b 落盘未启用：历史页同样明确错误",
              doc.get("ok") is False and "落盘未启用" in str(doc.get("err")),
              "%r" % (doc,))

        # ---- I2 超帧上限兜底 ----
        # 造一个编码后必然 >16KB 的载荷（60 行 × 19 列 × 200 字节文本）。
        big_rows = [["x" * 200] * 19 for _ in range(60)]
        sink.frames.clear()
        await business._reply_checked(
            sink, 9, proto.CMD_HISTORY_PAGE,
            {proto.HIST_KEY_TOTAL: 60,
             proto.HIST_KEY_ROWS: big_rows}, "本页数据过大（测试）")
        doc = bson.decode(sink.frames[0][2]) if sink.frames else {}
        check("I2 超 16KB：改回短失败包（不发巨帧）",
              doc.get("ok") is False
              and "本页数据过大" in str(doc.get("err"))
              and len(sink.frames[0][2]) < 1024,
              "bytes=%d" % (len(sink.frames[0][2]) if sink.frames else -1))

        # ---- I3 导出模式：不数总数/不算边界（2026-11-17 性能优化）----
        # [为什么] 关键字查询的 COUNT(*) 是一次完整全表扫（20 万行 / 16 个
        # 显示表达式实测 747ms），导出每批都跑一次 ⇒ 66 分钟的绝大部分。
        # 导出不需要 total（终止靠短批）⇒ 跳过 count/bounds/row_count。
        assert business.store.open_for("")      # 真库（空表也行）
        # [导出要 Download 权限] 假会话没有角色 ⇒ 权限闸会挡在 store 之前；
        # 本用例只验"导出模式走快路"，权限闸用打桩放行。
        import hsfsrv.permissions as _perm
        _orig_perm_can = _perm.can
        _perm.can = lambda _s, _p: True
        seen = {}
        orig_q = business.store.query_history_page
        orig_b = business.store.history_bounds
        orig_r = business.store.row_count

        def _spy_q(**kw):
            seen["with_count"] = kw.get("with_count")
            return [], (-1 if kw.get("with_count") is False else 0)

        def _spy_b():
            seen["bounds"] = seen.get("bounds", 0) + 1
            return 0, 0

        def _spy_r(*a, **k):
            seen["row_count"] = seen.get("row_count", 0) + 1
            return 0

        business.store.query_history_page = _spy_q
        business.store.history_bounds = _spy_b
        business.store.row_count = _spy_r
        try:
            sink.frames.clear()
            await business._h_history_page(
                sink, 11, proto.CMD_HISTORY_PAGE,
                bson.encode({proto.HIST_KEY_OFFSET: 0,
                             proto.HIST_KEY_LIMIT: 40,
                             proto.HIST_KEY_EXPORT: True}))
            doc_exp = bson.decode(sink.frames[0][2]) if sink.frames else {}
            exp_seen = dict(seen)
            seen.clear()
            sink.frames.clear()
            await business._h_history_page(
                sink, 12, proto.CMD_HISTORY_PAGE,
                bson.encode({proto.HIST_KEY_OFFSET: 0,
                             proto.HIST_KEY_LIMIT: 40}))
            doc_ui = bson.decode(sink.frames[0][2]) if sink.frames else {}
            ui_seen = dict(seen)
        finally:
            business.store.query_history_page = orig_q
            business.store.history_bounds = orig_b
            business.store.row_count = orig_r
            _perm.can = _orig_perm_can
        check("I3 导出模式：跳过 COUNT（with_count=False）+ total=-1",
              exp_seen.get("with_count") is False
              and doc_exp.get(proto.HIST_KEY_TOTAL) == -1,
              "seen=%r total=%r" % (exp_seen,
                                    doc_exp.get(proto.HIST_KEY_TOTAL)))
        check("I3b 导出模式：bounds/row_count 也不跑（各是一笔开销）",
              not exp_seen.get("bounds") and not exp_seen.get("row_count"),
              "%r" % (exp_seen,))
        check("I3c 界面翻页（未带 export）：照旧数总数、算边界、数库行",
              ui_seen.get("with_count") is True
              and ui_seen.get("bounds") == 1
              and ui_seen.get("row_count") == 1,
              "%r" % (ui_seen,))

        # 正常载荷照常成功（不许把普通页也拦掉）
        sink.frames.clear()
        await business._reply_checked(
            sink, 10, proto.CMD_HISTORY_PAGE,
            {proto.HIST_KEY_TOTAL: 2,
             proto.HIST_KEY_ROWS: [["a"] * 19, ["b"] * 19]}, "本页数据过大")
        doc = bson.decode(sink.frames[0][2]) if sink.frames else {}
        check("I2b 正常载荷：照常成功应答",
              doc.get("ok") is True and len(doc.get("rows") or []) == 2,
              "%r" % (list(doc.keys()),))
    finally:
        shutil.rmtree(basedir, ignore_errors=True)


# ============================================================================
#                                  入口
# ============================================================================

async def _run():
    await test_carry_stop_pumps()
    test_config_load_tolerance()
    test_store_write_without_connection()
    test_manual_running_validity()
    test_strict_bool()
    test_auto_config_strict_bool()
    await test_tick_step_guards()
    await test_round2_hardening()
    await test_data_pages_guards()


def main():
    asyncio.run(_run())
    print("=" * 62)
    print("  汇总：PASS %d / FAIL %d" % (PASS, FAIL))
    return 0 if FAIL == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
