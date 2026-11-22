# -*- coding: utf-8 -*-
"""运行保护（protect 节）+ 流量计告警入库（2026-11-15 用户定案）。

[测什么]
    A. **配置面**：protect 节的键、缺省、非法值 fail-closed、越界夹取、
       落盘往返（对齐 C 端 HsfServerApp.c 的 protect 分支）。
    B. **保护判定**：进水泵开着 + 流量计仍报空管，持续超时限 ⇒ 动作
       （停设备 / 只报警）；关闭保护、流量计读不到、泵没开 ⇒ 不动作。
    C. **流量计告警**：报警位**边沿** ⇒ 推一条告警（TOPIC_AUTO_ALARM）
       + 落 alarms 表；位不变不重复推；掉线不算"已恢复"。

[怎么跑]
    python3 -I tests/test_protect.py          # 独立跑
    pytest tests/test_protect.py -q           # pytest 收集

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

from hsfsrv import bson, config as cfgmod    # noqa: E402
from hsfsrv import proto                     # noqa: E402
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


class _FakeServer(object):
    """只记发布帧的假服务端（保护/告警都走 publish）。"""

    def __init__(self):
        self.frames = []

    async def publish(self, topic, payload):
        try:
            doc = bson.decode(payload)
        except Exception:                               # noqa: BLE001
            doc = {"_raw": payload}
        self.frames.append((topic, doc))

    def alarm_frames(self):
        return [d for (t, d) in self.frames
                if t == proto.TOPIC_AUTO_ALARM]


class _FakeFlowMeter(object):
    """假流量计：只提供保护/告警判定要用的那几个字段。"""

    def __init__(self):
        self.online = True
        self.last_ok_ms = 0
        self.alarm_upper = False
        self.alarm_lower = False
        self.alarm_empty = False
        self.alarm_system = False

    def alarm_mask(self):
        mask = 0
        if self.alarm_upper:
            mask |= 1
        if self.alarm_lower:
            mask |= 2
        if self.alarm_empty:
            mask |= 4
        if self.alarm_system:
            mask |= 8
        return mask


def _rig(basedir):
    """假设备业务层 + 阀组/泵组 + 假流量计 + 假服务端。"""
    from hsfsrv import pumps as pumps_mod
    from hsfsrv import valves as valves_mod

    cfg = cfgmod.Config()
    cfg.resolve_paths(basedir)
    cfg.config_path = os.path.join(basedir, "cfg", "x.json")
    cfg.users_path = os.path.join(basedir, "cfg", "hsf_users.json")
    os.makedirs(os.path.join(basedir, "cfg"), exist_ok=True)
    cfg.port = 0
    cfg.enable_serial = False
    business = build_fake_app(cfg, log=lambda m: None)
    business.server = _FakeServer()
    business.flowmeter = _FakeFlowMeter()
    business.pump_bank = pumps_mod.PumpBank(business.digital, cfg.pump_items,
                                            lambda _m: None)
    business.valve_bank = valves_mod.ValveBank(business.digital,
                                                cfg.valve_items,
                                                lambda _m: None)
    business.store.insert_alarm = lambda *a, **k: True
    return business, cfg


async def _pump_on(business, now_ms, on=True):
    """把进水泵开/关，并刷新泵组与数字量模块的缓存。"""
    dev = business.digital[0]
    await dev.poll(now_ms)
    await business.pump_bank.set_pump(0, on)
    business.pump_bank.poll(now_ms, business.cfg.pump_stale_ms)


async def _tick(business, now_ms):
    """推进一拍保护判定（同时把流量计数据刷新成"新鲜"）。

    [为什么要刷新 last_ok_ms] 保护判据要求流量计**在线且数据新鲜** ——
    假流量计不会自己刷新 ⇒ 每拍手动推进一次（模拟刚读到一帧）。
    """
    business.flowmeter.last_ok_ms = now_ms
    await business._protect_step(now_ms)
    # [让出事件循环] 全停是**异步投递**（loop.create_task）⇒ 动作要等
    # 事件循环转一圈才落地 ⇒ 这里主动让出几轮。
    await asyncio.sleep(0.05)


# ============================================================================
#                              A 配置面
# ============================================================================

async def test_protect_config():
    """A1~A6：缺省 / 逐键写 / 非法键拒 / 非法值拒 / 越界夹取 / 落盘往返。"""
    print("=== A 运行保护配置面 ===")
    basedir = tempfile.mkdtemp(prefix="hsf_protect_")
    try:
        business, cfg = _rig(basedir)

        # A1 缺省：**关闭该保护**（0ms）+ 超时则停设备。
        # [为什么缺省关闭] 现场实测：板端流量计在有流量（3.6 m³/h）时也一直
        # 挂着"空管"位 ⇒ 缺省启用 + 时限偏短 ⇒ 每次开泵一分钟就停线。
        doc = business._config_protect_doc()
        check("A1 缺省：保护关闭（0ms）+ 动作=停止设备",
              doc.get(proto.CFGK_PROTECT_INLET_NOFLOW_TIMEOUT_MS) == 0
              and doc.get(proto.CFGK_PROTECT_INLET_NOFLOW_STOP) is True,
              "%r" % (doc,))

        # A2 逐键写（协议键名 → cfg）。
        business._cfg_set_protect_key(
            proto.CFGK_PROTECT_INLET_NOFLOW_TIMEOUT_MS, 1500)
        business._cfg_set_protect_key(
            proto.CFGK_PROTECT_INLET_NOFLOW_STOP, False)
        doc = business._config_protect_doc()
        check("A2 逐键写生效（1500ms + 只报警）",
              doc.get(proto.CFGK_PROTECT_INLET_NOFLOW_TIMEOUT_MS) == 1500
              and doc.get(proto.CFGK_PROTECT_INLET_NOFLOW_STOP) is False,
              "%r" % (doc,))

        # A3 未知键 → ValueError（fail-closed：不能静默接受）。
        try:
            business._cfg_set_protect_key("nope", 1)
            check("A3 未知键被拒", False, "未抛异常")
        except ValueError:
            check("A3 未知键被拒", True)

        # A4 非数字超时 → ValueError。
        try:
            business._cfg_set_protect_key(
                proto.CFGK_PROTECT_INLET_NOFLOW_TIMEOUT_MS, "abc")
            check("A4 非数字超时被拒", False, "未抛异常")
        except ValueError:
            check("A4 非数字超时被拒", True)

        # A5 越界夹取（负数 → 0；超上限 → 上限）。
        business._cfg_set_protect_key(
            proto.CFGK_PROTECT_INLET_NOFLOW_TIMEOUT_MS, -5)
        check("A5a 负数夹到 0（=关闭保护）",
              business._config_protect_doc().get(
                  proto.CFGK_PROTECT_INLET_NOFLOW_TIMEOUT_MS) == 0)
        business._cfg_set_protect_key(
            proto.CFGK_PROTECT_INLET_NOFLOW_TIMEOUT_MS, 999999999)
        check("A5b 超上限夹到 3600000",
              business._config_protect_doc().get(
                  proto.CFGK_PROTECT_INLET_NOFLOW_TIMEOUT_MS) == 3600000)

        # A6 落盘往返：save → 新 Config.load → 值一致。
        business._cfg_set_protect_key(
            proto.CFGK_PROTECT_INLET_NOFLOW_TIMEOUT_MS, 45000)
        business._cfg_set_protect_key(
            proto.CFGK_PROTECT_INLET_NOFLOW_STOP, True)
        cfg.save()
        cfg2 = cfgmod.Config()
        cfg2.resolve_paths(basedir)
        cfg2.config_path = cfg.config_path
        cfg2.load()
        check("A6 落盘往返（超时/开关都一致）",
              cfg2.protect_inlet_noflow_timeout_ms == 45000
              and cfg2.protect_inlet_noflow_stop is True,
              "timeout=%r stop=%r" % (cfg2.protect_inlet_noflow_timeout_ms,
                                     cfg2.protect_inlet_noflow_stop))

        # ---- A7 严格布尔（2026-11-15 审计）----
        # [为什么] `bool("false")` 是 True ⇒ 载荷里写成字符串 "false"
        # （客户端拼错/老版本客户端发字符串）时，"只报警不停机"会被当成
        # "**直接停机**"⇒ 现场误停线。布尔开关全是安全语义 ⇒ 宁可报错。
        for key in (proto.CFGK_PROTECT_INLET_NOFLOW_STOP,
                    proto.CFGK_PROTECT_PRESS_HIGH_ENABLED,
                    proto.CFGK_PROTECT_PRESS_HIGH_STOP):
            try:
                business._cfg_set_protect_key(key, "false")
                check("A7 字符串布尔被拒（%s）" % key, False, "未抛异常")
            except ValueError:
                check("A7 字符串布尔被拒（%s）" % key, True)

        # ---- A8 整包失败即回滚（不留半截配置）----
        before = business._config_protect_doc()
        try:
            business._cfg_apply_batch(
                [proto.CFGK_PROTECT_INLET_NOFLOW_TIMEOUT_MS, "nope"],
                {proto.CFGK_PROTECT_INLET_NOFLOW_TIMEOUT_MS: 9999,
                 "nope": 1},
                business._cfg_set_protect_key)
            check("A8 整包失败被拒", False, "未抛异常")
        except ValueError:
            check("A8 整包失败被拒", True)
        after = business._config_protect_doc()
        check("A8b 整包失败：合法键**也回滚**（不留半截配置）",
              after.get(proto.CFGK_PROTECT_INLET_NOFLOW_TIMEOUT_MS)
              == before.get(proto.CFGK_PROTECT_INLET_NOFLOW_TIMEOUT_MS),
              "before=%r after=%r" % (before, after))

        # ---- A9 压差上限 0 = 合法值（不被缺省 0.8 顶掉）----
        # [为什么] 上限 0 的语义是"不限/禁用上限"（服务端判 limit<=0 不动
        # 作）；`float(v or DEF)` 这种写法会把 **0 变成 0.8** ⇒ 界面显示
        # 0.8、服务端却是 0 ⇒ 一点「应用」就把配置改了。
        business._cfg_set_protect_key(
            proto.CFGK_PROTECT_PRESS_HIGH_LIMIT_MPA, 0.0)
        check("A9a SET 压差上限 0 保持 0",
              business._config_protect_doc().get(
                  proto.CFGK_PROTECT_PRESS_HIGH_LIMIT_MPA) == 0.0,
              "%r" % (business._config_protect_doc(),))
        cfg.save()
        cfg3 = cfgmod.Config()
        cfg3.resolve_paths(basedir)
        cfg3.config_path = cfg.config_path
        cfg3.load()
        check("A9b 落盘往返：压差上限 0 仍是 0（不被缺省顶掉）",
              cfg3.protect_press_high_limit_mpa == 0.0,
              "limit=%r" % (cfg3.protect_press_high_limit_mpa,))

        # ---- A10 告警入库的 kind/source 按设备类别（2026-11-15 审计）----
        # [原来] 只有"是不是流量计"一个分支，其余一律落 "device/变频器"
        # ⇒ 压差过高（dev=模拟量）进库后来源写着"变频器"（张冠李戴）。
        check("A10a 压差过大 → (analog, 模拟量)",
              business._alarm_db_kind_source(proto.KIND_ANALOG_IO)
              == ("analog", "模拟量"),
              "%r" % (business._alarm_db_kind_source(proto.KIND_ANALOG_IO),))
        check("A10b 流量计 → (flowmeter, 流量计)",
              business._alarm_db_kind_source(proto.KIND_FLOWMETER)
              == ("flowmeter", "流量计"))
        check("A10c 变频器 → (device, 变频器)",
              business._alarm_db_kind_source(proto.KIND_VFD)
              == ("device", "变频器"))
        check("A10d 无设备类别 → (auto, 自动运行)",
              business._alarm_db_kind_source(None) == ("auto", "自动运行"))
    finally:
        shutil.rmtree(basedir, ignore_errors=True)


# ============================================================================
#                              B 保护判定
# ============================================================================

async def test_protect_action():
    """B1~B7：关闭/未到时限/到时限/只报警/读不到/泵没开/条件消失。"""
    print("=== B 运行保护判定 ===")
    basedir = tempfile.mkdtemp(prefix="hsf_protect_")
    try:
        business, cfg = _rig(basedir)
        fm = business.flowmeter
        fm.alarm_empty = True
        fm.last_ok_ms = 1000

        # ---- B1 关闭保护（timeout=0）⇒ 什么都不做 ----
        business._cfg_set_protect_key(
            proto.CFGK_PROTECT_INLET_NOFLOW_TIMEOUT_MS, 0)
        business._cfg_set_protect_key(
            proto.CFGK_PROTECT_INLET_NOFLOW_STOP, True)
        await _pump_on(business, 1000, True)
        await _tick(business, 2000)
        await _tick(business, 5000)
        check("B1 关闭保护（0ms）：不报保护告警",
              business.server.alarm_frames() == [],
              "%r" % (business.server.alarm_frames(),))
        check("B1b 关闭保护：泵仍开着（没动设备）",
              business.pump_bank.items[0].on is True)

        # ---- B2 到时限 ⇒ 动作（告警 + 停设备）----
        business._cfg_set_protect_key(
            proto.CFGK_PROTECT_INLET_NOFLOW_TIMEOUT_MS, 1000)
        business.server.frames = []
        await _tick(business, 6000)                 # 计时起点
        await _tick(business, 6500)                 # 未到时限
        check("B2 未到时限：不动作",
              business.server.alarm_frames() == []
              and business.pump_bank.items[0].on is True,
              "%r" % (business.server.alarm_frames(),))
        await _tick(business, 7200)                 # 超时 ⇒ 动作
        frames = business.server.alarm_frames()
        check("B2 到时限：推一条保护告警（kind=流量计）",
              len(frames) == 1
              and frames[0].get("kind") == proto.AUTO_ALARM_FLOWMETER
              and "空管" in str(frames[0].get("text", "")),
              "%r" % (frames,))
        check("B2b 到时限：告警帧带设备类别（dev=流量计）",
              len(frames) == 1
              and frames[0].get(proto.KEY_AUTOALARM_DEV) == proto.KIND_FLOWMETER,
              "%r" % (frames,))
        check("B2c 到时限：进水泵被停掉",
              business.pump_bank.items[0].on is False,
              "on=%r" % (business.pump_bank.items[0].on,))

        # ---- B3 只报警模式（stop=false）⇒ 不停设备 ----
        # [先解掉 B2 留下的锁存] 保护动作会锁存（进报警表 ⇒ 需人工复位），
        # 锁存未解时保护**不重复动作**（否则报警表被刷爆）⇒ 这里模拟"人已复位"。
        business.auto_controller.clear_fault("测试：模拟人工复位")
        business._cfg_set_protect_key(
            proto.CFGK_PROTECT_INLET_NOFLOW_TIMEOUT_MS, 1000)
        business._cfg_set_protect_key(
            proto.CFGK_PROTECT_INLET_NOFLOW_STOP, False)
        await _pump_on(business, 8000, True)
        business.server.frames = []
        await _tick(business, 8000)
        await _tick(business, 9500)
        frames = business.server.alarm_frames()
        check("B3 只报警：到时限推一条告警",
              len(frames) == 1
              and frames[0].get("kind") == proto.AUTO_ALARM_FLOWMETER,
              "%r" % (frames,))
        check("B3b 只报警：设备**不停**（泵仍开）",
              business.pump_bank.items[0].on is True,
              "on=%r" % (business.pump_bank.items[0].on,))
        check("B3c 只报警：文案说明未停设备",
              "未停设备" in str(frames[0].get("text", "") if frames else ""),
              "%r" % (frames,))

        # ---- B4 流量计离线 ⇒ 不判定（读不到 ≠ 空管）----
        business._cfg_set_protect_key(
            proto.CFGK_PROTECT_INLET_NOFLOW_TIMEOUT_MS, 1000)
        business._cfg_set_protect_key(
            proto.CFGK_PROTECT_INLET_NOFLOW_STOP, True)
        await _pump_on(business, 10000, True)
        fm.online = False
        business.server.frames = []
        await business._protect_step(10000)         # [离线：不刷新 last_ok_ms]
        await business._protect_step(12000)
        check("B4 流量计离线：不判定、不动作",
              business.server.alarm_frames() == []
              and business.pump_bank.items[0].on is True,
              "%r" % (business.server.alarm_frames(),))

        # ---- B5 流量计数据陈旧 ⇒ 不判定 ----
        fm.online = True
        fm.last_ok_ms = 1        # 远早于 now（陈旧）
        business.server.frames = []
        await business._protect_step(13000)         # [陈旧：不刷新 last_ok_ms]
        await business._protect_step(15000)
        check("B5 流量计数据陈旧：不判定、不动作",
              business.server.alarm_frames() == []
              and business.pump_bank.items[0].on is True,
              "%r" % (business.server.alarm_frames(),))

        # ---- B6 泵没开 ⇒ 不动作（只有空管不算）----
        fm.last_ok_ms = 20000
        await _pump_on(business, 20000, False)
        business.server.frames = []
        await _tick(business, 20000)
        await _tick(business, 22000)
        check("B6 泵没开：不动作",
              business.server.alarm_frames() == [],
              "%r" % (business.server.alarm_frames(),))

        # ---- B7 空管消失 ⇒ 计时清零（重新计时，不是累计）----
        await _pump_on(business, 23000, True)
        fm.alarm_empty = True
        business.server.frames = []
        await _tick(business, 23000)             # 计时起点
        fm.alarm_empty = False                    # 中途恢复
        await _tick(business, 24000)
        fm.alarm_empty = True
        await _tick(business, 25000)
        await _tick(business, 25900)             # 距"重新成立"不到时限
        check("B7 空管消失后重新计时（不累计到旧起点）",
              business.server.alarm_frames() == [],
              "%r" % (business.server.alarm_frames(),))
    finally:
        shutil.rmtree(basedir, ignore_errors=True)


# ============================================================================
#                          C 流量计告警边沿
# ============================================================================

async def test_flowmeter_alarm_edge():
    """C1~C4：出现/不变/消失/掉线。"""
    print("=== C 流量计告警边沿 ===")
    basedir = tempfile.mkdtemp(prefix="hsf_protect_")
    try:
        business, cfg = _rig(basedir)
        fm = business.flowmeter
        fm.last_ok_ms = 1000
        store_calls = []
        # [dev=None 2026-11-19] 产品侧 _publish_alarm_event 现在带
        # dev=dev_kind 落库（报警表"类型"列口径对齐）⇒ 桩必须收下这个
        # 关键字，否则 TypeError 被产品代码的 except Exception 吞掉 ⇒
        # 告警"没落表"（C1c 假红的根因）。
        business.store.insert_alarm = \
            lambda kind, source, text, now_ms=None, dev=None: \
            store_calls.append((kind, source, text)) or True

        # C0 首帧只记基线（启动时流量计可能已经挂着空管 ⇒ 不报）。
        fm.alarm_empty = True
        await business._flowmeter_alarm_step(900)
        check("C0 首帧只记基线：启动时不报（不制造噪音）",
              business.server.alarm_frames() == [],
              "%r" % (business.server.alarm_frames(),))

        # C1 报警位**变化**（空管 → 正常 → 再空管）：各推一帧。
        fm.alarm_empty = False
        await business._flowmeter_alarm_step(950)
        fm.alarm_empty = True
        await business._flowmeter_alarm_step(1000)
        frames = business.server.alarm_frames()
        check("C1 报警位变化：每变一次推一帧（kind=流量计 + dev=流量计）",
              len(frames) == 2
              and all(f.get("kind") == proto.AUTO_ALARM_FLOWMETER
                      and f.get(proto.KEY_AUTOALARM_DEV) == proto.KIND_FLOWMETER
                      for f in frames),
              "%r" % (frames,))
        check("C1b 报警位出现：文案带「空管」",
              frames and "空管" in str(frames[-1].get("text", "")),
              "%r" % (frames,))
        check("C1c 报警位变化：落 alarms 表（flowmeter/流量计）",
              ("flowmeter", "流量计") in [(k, s) for (k, s, _t) in store_calls],
              "%r" % (store_calls,))

        # C2 位不变 ⇒ 不再推（报警位是状态，不是每拍事件）。
        business.server.frames = []
        await business._flowmeter_alarm_step(1200)
        await business._flowmeter_alarm_step(1400)
        check("C2 报警位不变：不重复推",
              business.server.alarm_frames() == [],
              "%r" % (business.server.alarm_frames(),))

        # C3 报警位消失 ⇒ 推一条「已恢复」。
        fm.alarm_empty = False
        await business._flowmeter_alarm_step(1600)
        frames = business.server.alarm_frames()
        check("C3 报警位消失：推一条「已恢复」",
              len(frames) == 1 and "恢复" in str(frames[0].get("text", "")),
              "%r" % (frames,))

        # C4 流量计掉线 ⇒ 不推（掉线不是「恢复」）。
        business.server.frames = []
        fm.online = False
        await business._flowmeter_alarm_step(1800)
        check("C4 流量计掉线：不推帧（不把掉线当恢复）",
              business.server.alarm_frames() == [],
              "%r" % (business.server.alarm_frames(),))
    finally:
        shutil.rmtree(basedir, ignore_errors=True)


async def test_fault_latch():
    """D1~D8：设备故障 ⇒ 报警（进报警历史表）+ 锁存 + 自动停机 + 需手动复位。"""
    print("=== D 设备故障：报警 + 锁存 + 自动停机 + 手动复位 ===")
    basedir = tempfile.mkdtemp(prefix="hsf_fault_")
    try:
        business, cfg = _rig(basedir)
        ctl = business.auto_controller

        # ---- D1 变频器故障（边沿）⇒ 报警 + 锁存 + 停设备 ----
        business.vfd.faulted = True
        business.vfd.fault_code = 3
        await business._watch_vfd_fault(1000)
        frames = business.server.alarm_frames()
        check("D1 变频器故障：推一条告警（kind=设备故障 + dev=变频器）",
              len(frames) == 1
              and frames[0].get("kind") == proto.AUTO_ALARM_DEVICE_FAULT
              and frames[0].get(proto.KEY_AUTOALARM_DEV) == proto.KIND_VFD,
              "%r" % (frames,))
        check("D1b 变频器故障：报警锁存已置位",
              ctl.fault_latched is True,
              "latched=%r" % (ctl.fault_latched,))
        check("D1c 变频器故障：文案带「已停止设备」",
              frames and "已停止设备" in str(frames[0].get("text", "")),
              "%r" % (frames,))
        await asyncio.sleep(0.05)
        check("D1d 变频器故障：搅拌泵已停（自动停机）",
              business.vfd._running is False,
              "running=%r" % (business.vfd._running,))

        # ---- D2 锁存未解 ⇒ 手动模式的远程控制被拒（闸门文案）----
        check("D2 锁存未解：手动模式控制被拒（闸门文案）",
              business._fault_gate_text() is not None
              and "未复位" in business._fault_gate_text(),
              "%r" % (business._fault_gate_text(),))

        # ---- D3 故障复位（人工）⇒ 解锁 ----
        ctl.clear_fault("设备故障复位")
        check("D3 故障复位后：报警锁存解除",
              ctl.fault_latched is False,
              "latched=%r" % (ctl.fault_latched,))
        check("D3b 故障复位后：手动模式控制放行",
              business._fault_gate_text() is None,
              "%r" % (business._fault_gate_text(),))

        # ---- D3c 复位（op=4 口径）⇒ 顺带给变频器发故障复位 ----
        # [2026-11-17 用户口径] 变频器过载等故障后，只清服务端锁存不够：
        # 变频器自己锁在故障里，不发复位它不让再启动 ⇒ 复位时变频器正故障
        # 就要把故障复位命令写下去（0x2000=7）。FakeVfd 收到命令 7 即清
        # 驱动器故障（真机口径）。[重新锁存用 raise_fault 直注] watch 是
        # 边沿检测（D1 已见过故障 ⇒ 再置 faulted 不产生新边沿）。
        business.vfd.faulted = True
        business.vfd.fault_code = 3
        ctl.raise_fault(proto.AUTO_ALARM_DEVICE_FAULT, "测试故障2", 1050,
                        stop_devices=False, enqueue_alarm=False)
        check("D3c-0 前置：故障已重新锁存",
              ctl.fault_latched is True,
              "latched=%r" % (ctl.fault_latched,))
        _n_before = len(business.vfd.commands)
        _ok, _note = await business._auto_reset_and_vfd()
        check("D3c 复位：变频器故障复位已下发（附注）",
              _ok and "已下发" in _note, "ok=%s note=%r" % (_ok, _note))
        check("D3c-1 故障复位命令已写到变频器（0x2000=7）",
              7 in business.vfd.commands[_n_before:],
              "commands=%r" % (business.vfd.commands[_n_before:],))
        check("D3c-2 变频器故障已清（命令 7 生效）",
              business.vfd.fault_code == 0
              and business.vfd.faulted is False,
              "code=%r faulted=%r" % (business.vfd.fault_code,
                                      business.vfd.faulted))
        check("D3c-3 报警锁存已解除",
              ctl.fault_latched is False,
              "latched=%r" % (ctl.fault_latched,))

        # ---- D3d 变频器无故障时复位 ⇒ 不发变频器命令（附注为空）----
        _n_before2 = len(business.vfd.commands)
        _ok2, _note2 = await business._auto_reset_and_vfd()
        check("D3d 变频器无故障：复位不发变频器命令",
              _ok2 and _note2 == "" and business.vfd.commands[_n_before2:] == [],
              "ok=%s note=%r commands=%r"
              % (_ok2, _note2, business.vfd.commands[_n_before2:],))

        # ---- D3e 自动运行中复位 ⇒ 明确拒绝（不架空序列）----
        # [2026-11-18 联调深检] 运行中按复位会把 phase 打回 idle 但设备
        # 保持当前状态无人管理 ⇒ reset() 运行中守卫 + op=4 明确拒。
        ctl._running = True
        try:
            _ok3, _note3 = await business._auto_reset_and_vfd()
            check("D3e 运行中复位被拒（ok=False + 文案）",
                  _ok3 is False and "运行中不可复位" in _note3,
                  "ok=%s note=%r" % (_ok3, _note3))
            check("D3e-1 无变频器命令（拒绝路径不发总线写）",
                  business.vfd.commands[_n_before2:] == [],
                  "commands=%r" % (business.vfd.commands[_n_before2:],))
        finally:
            ctl._running = False

        # ---- D4 急停锁存不被故障复位清掉 ----
        ctl._estop_latched = True
        ctl._fault_latched = True
        ctl.clear_fault("设备故障复位")
        check("D4 故障复位**不清**急停锁存",
              ctl.estop_latched is True,
              "estop_latched=%r" % (ctl.estop_latched,))
        ctl._estop_latched = False

        # ---- D5 故障消失**不解锁**（必须人工复位）----
        # （D3 已把锁存解掉了 ⇒ 这里重新置一次锁存，再让故障消失）
        ctl.raise_fault(proto.AUTO_ALARM_DEVICE_FAULT, "测试故障", 1150,
                        stop_devices=False, enqueue_alarm=False)
        business.vfd.faulted = False
        await business._watch_vfd_fault(1200)
        check("D5 故障消失：锁存仍在（必须人工复位）",
              ctl.fault_latched is True,
              "latched=%r" % (ctl.fault_latched,))
        ctl.clear_fault("测试收尾")

        # ---- D6 流量计的工况告警**不锁存**（现场空管位常置位 ⇒ 锁存会
        #      让线永远起不来；停机由「运行保护」的时限负责）----
        fm = business.flowmeter
        fm.alarm_empty = True
        await business._flowmeter_alarm_step(1300)
        check("D6 流量计工况告警：只记录，不锁存",
              ctl.fault_latched is False
              and len(business.server.alarm_frames()) >= 1,
              "latched=%r frames=%d" % (ctl.fault_latched,
                                        len(business.server.alarm_frames())))

        # ---- D8 数字量页的原始 DO 写也是"启动设备"的途径 ⇒ 闸门要覆盖 ----
        chans = business._device_do_channels(0)
        check("D8 设备 DO 通道清单（6 路阀 + 进水泵）",
              set(chans) == set(range(0, 7)),
              "chans=%r" % (sorted(chans),))

        # ---- D7 保护动作 ⇒ 锁存（进了报警表 ⇒ 需人工复位）----
        business._cfg_set_protect_key(
            proto.CFGK_PROTECT_INLET_NOFLOW_TIMEOUT_MS, 500)
        business._cfg_set_protect_key(
            proto.CFGK_PROTECT_INLET_NOFLOW_STOP, True)
        ctl.clear_fault("测试收尾")
        fm.last_ok_ms = 1400
        await _pump_on(business, 1400, True)
        await _tick(business, 1500)          # 计时起点
        await _tick(business, 2200)          # 超时 ⇒ 动作
        check("D7 保护动作：报警锁存已置位（需人工复位）",
              ctl.fault_latched is True,
              "latched=%r" % (ctl.fault_latched,))
    finally:
        shutil.rmtree(basedir, ignore_errors=True)


async def test_press_high_protect():
    """E1~E8：压差过大保护（开关 + 阈值 + 去抖 + 停机 + 锁存）。"""
    print("=== E 压差过大保护 ===")
    basedir = tempfile.mkdtemp(prefix="hsf_press_")
    try:
        business, cfg = _rig(basedir)
        ctl = business.auto_controller
        # [压差取数打桩] 真实取数走模拟量通道 0 + 工程量标定；这里直接桩掉
        # ctx.press_mpa()（只验判定与动作，不验换算链）。
        state = {"mpa": 0.9}

        def _press():
            return state["mpa"]

        business.auto_ctx.press_mpa = _press

        # ---- E1 缺省：保护不启用 ----
        doc = business._config_protect_doc()
        check("E1 缺省：压差保护不启用（阈值 0.8 MPa）",
              doc.get(proto.CFGK_PROTECT_PRESS_HIGH_ENABLED) is False
              and abs(float(doc.get(
                  proto.CFGK_PROTECT_PRESS_HIGH_LIMIT_MPA)) - 0.8) < 1e-6,
              "%r" % (doc,))

        # ---- E2 未启用 ⇒ 不动作（哪怕压差很高）----
        business._cfg_set_protect_key(
            proto.CFGK_PROTECT_PRESS_HIGH_LIMIT_MPA, 0.5)
        business.server.frames = []
        await business._protect_press_step(1000)
        await business._protect_press_step(5000)
        check("E2 未启用：压差再高也不动作",
              business.server.alarm_frames() == []
              and ctl.fault_latched is False,
              "%r" % (business.server.alarm_frames(),))

        # ---- E3 启用 + 超上限：未到去抖时长不动作；到时动作 ----
        business._cfg_set_protect_key(
            proto.CFGK_PROTECT_PRESS_HIGH_ENABLED, True)
        business._cfg_set_protect_key(
            proto.CFGK_PROTECT_PRESS_HIGH_LIMIT_MPA, 0.5)
        business._cfg_set_protect_key(
            proto.CFGK_PROTECT_PRESS_HIGH_STOP, True)
        business.server.frames = []
        await business._protect_press_step(6000)          # 计时起点
        await business._protect_press_step(6500)          # 未到 2s 去抖
        check("E3 未到去抖时长：不动作",
              business.server.alarm_frames() == [],
              "%r" % (business.server.alarm_frames(),))
        await business._protect_press_step(8500)          # 超 2s ⇒ 动作
        frames = business.server.alarm_frames()
        check("E3 到去抖时长：推一条告警（kind=压差过高 + dev=模拟量）",
              len(frames) == 1
              and frames[0].get("kind") == proto.AUTO_ALARM_PRESS_HIGH
              and frames[0].get(proto.KEY_AUTOALARM_DEV) == proto.KIND_ANALOG_IO,
              "%r" % (frames,))
        check("E3b 告警文案带实测值与上限",
              frames and "0.900" in str(frames[0].get("text", ""))
              and "0.500" in str(frames[0].get("text", "")),
              "%r" % (frames,))
        check("E3c 压差过大：报警锁存已置位（需人工复位）",
              ctl.fault_latched is True,
              "latched=%r" % (ctl.fault_latched,))
        # [锁存已在 ⇒ 不重复动作] 压差还高着时每 2 秒报一次会把报警表刷爆
        # （实锅：一次超压 ⇒ 连续两条告警 + 两次全停）⇒ 锁存未解就不重复。
        business.server.frames = []
        await business._protect_press_step(9000)
        await business._protect_press_step(9500)
        check("E3d 锁存未解：不重复报警（等人工复位）",
              business.server.alarm_frames() == [],
              "%r" % (business.server.alarm_frames(),))
        ctl.clear_fault("测试收尾")

        # ---- E4 压差在上限之下 ⇒ 不动作 ----
        state["mpa"] = 0.3
        business.server.frames = []
        await business._protect_press_step(9000)
        await business._protect_press_step(12000)
        check("E4 压差未超上限：不动作",
              business.server.alarm_frames() == []
              and ctl.fault_latched is False,
              "%r" % (business.server.alarm_frames(),))

        # ---- E5 压差读不到（None）⇒ 不判定（读不到 ≠ 压差过大）----
        state["mpa"] = None
        await business._protect_press_step(13000)
        await business._protect_press_step(16000)
        check("E5 压差读不到：不判定、不动作",
              business.server.alarm_frames() == []
              and ctl.fault_latched is False,
              "%r" % (business.server.alarm_frames(),))

        # ---- E6 只报警模式（stop=false）⇒ 报警但不停设备 ----
        state["mpa"] = 0.9
        business._cfg_set_protect_key(
            proto.CFGK_PROTECT_PRESS_HIGH_STOP, False)
        business.server.frames = []
        await business._protect_press_step(17000)         # 计时起点
        await business._protect_press_step(20000)         # 超 2s ⇒ 动作
        frames = business.server.alarm_frames()
        check("E6 只报警：出告警但不写「已停止设备」",
              len(frames) == 1 and "未停设备" in str(frames[0].get("text", "")),
              "%r" % (frames,))
        check("E6b 只报警：仍锁存（进报警表 ⇒ 需人工复位）",
              ctl.fault_latched is True,
              "latched=%r" % (ctl.fault_latched,))
        ctl.clear_fault("测试收尾")

        # ---- E7 配置落盘往返 ----
        business._cfg_set_protect_key(
            proto.CFGK_PROTECT_PRESS_HIGH_ENABLED, True)
        business._cfg_set_protect_key(
            proto.CFGK_PROTECT_PRESS_HIGH_LIMIT_MPA, 1.25)
        business._cfg_set_protect_key(
            proto.CFGK_PROTECT_PRESS_HIGH_STOP, False)
        cfg.save()
        cfg2 = cfgmod.Config()
        cfg2.resolve_paths(basedir)
        cfg2.config_path = cfg.config_path
        cfg2.load()
        check("E7 落盘往返（启用/上限/开关都一致）",
              cfg2.protect_press_high_enabled is True
              and abs(cfg2.protect_press_high_limit_mpa - 1.25) < 1e-6
              and cfg2.protect_press_high_stop is False,
              "enabled=%r limit=%r stop=%r"
              % (cfg2.protect_press_high_enabled,
                 cfg2.protect_press_high_limit_mpa,
                 cfg2.protect_press_high_stop))
    finally:
        shutil.rmtree(basedir, ignore_errors=True)


async def _run():
    await test_protect_config()
    await test_protect_action()
    await test_flowmeter_alarm_edge()
    await test_fault_latch()
    await test_press_high_protect()
    await test_motor_over_temp_protect()
    await test_analog_channel_config()

async def test_motor_over_temp_protect():
    """F1~F8：电机超温保护（开关 + 阈值 + 去抖 + 停机 + 锁存 + 通道可配）。"""
    print("=== F 电机超温保护 ===")
    basedir = tempfile.mkdtemp(prefix="hsf_motortemp_")
    try:
        business, cfg = _rig(basedir)
        ctl = business.auto_controller
        # [温度取数打桩] 真实取数走模拟量通道（可配，缺省 CH1）+ 4~20mA
        # 线性换算；这里桩掉 ma()（只验判定与动作，不验换算链——换算链在
        # G 段用真 AnalogChannelScale 验）。
        state = {"ma": 4.0}

        def _ma(_ch):
            return state["ma"]

        business.analog.ma = _ma

        # ---- F1 缺省：保护**不启用**（阈值 75 ℃，缺省停设备）----
        doc = business._config_protect_doc()
        check("F1 缺省：超温保护不启用（阈值 75 ℃ / 停设备=True）",
              doc.get(proto.CFGK_PROTECT_MOTOR_OVER_TEMP_ENABLED) is False
              and abs(float(doc.get(
                  proto.CFGK_PROTECT_MOTOR_OVER_TEMP_LIMIT_C)) - 75.0) < 1e-6
              and doc.get(proto.CFGK_PROTECT_MOTOR_OVER_TEMP_STOP) is True,
              "%r" % (doc,))

        # ---- F2 未启用 ⇒ 不动作（哪怕温度爆表）----
        state["ma"] = 20.0                       # 满量程 ⇒ 100 ℃
        business.server.frames = []
        await business._protect_motor_over_temp_step(1000)
        await business._protect_motor_over_temp_step(5000)
        check("F2 未启用：温度再高也不动作",
              business.server.alarm_frames() == []
              and ctl.fault_latched is False,
              "%r" % (business.server.alarm_frames(),))

        # ---- F3 启用 + 超阈值：未到去抖不动作；到时动作 ----
        business._cfg_set_protect_key(
            proto.CFGK_PROTECT_MOTOR_OVER_TEMP_ENABLED, True)
        business._cfg_set_protect_key(
            proto.CFGK_PROTECT_MOTOR_OVER_TEMP_LIMIT_C, 75.0)
        business.server.frames = []
        await business._protect_motor_over_temp_step(6000)   # 计时起点
        await business._protect_motor_over_temp_step(6500)   # 未到 2s 去抖
        check("F3 未到去抖时长：不动作",
              business.server.alarm_frames() == [],
              "%r" % (business.server.alarm_frames(),))
        await business._protect_motor_over_temp_step(8500)   # 超 2s ⇒ 动作
        frames = business.server.alarm_frames()
        check("F3 到去抖时长：推一条告警（kind=电机超温 + dev=模拟量）",
              len(frames) == 1
              and frames[0].get("kind")
              == proto.PROTECT_ALARM_KIND_MOTOR_OVER_TEMP
              and frames[0].get(proto.KEY_AUTOALARM_DEV)
              == proto.KIND_ANALOG_IO,
              "%r" % (frames,))
        check("F3b 文案带实测温度与阈值（100.0 / 75.0）",
              frames and "100.0" in str(frames[0].get("text", ""))
              and "75.0" in str(frames[0].get("text", "")),
              "%r" % (frames,))
        check("F3c 超温：报警锁存已置位（需人工复位）",
              ctl.fault_latched is True,
              "latched=%r" % (ctl.fault_latched,))
        # [锁存未解 ⇒ 不重复动作] 同压差保护口径（实锅：一条超压连续两报）。
        business.server.frames = []
        await business._protect_motor_over_temp_step(9000)
        await business._protect_motor_over_temp_step(9500)
        check("F3d 锁存未解：不重复报警（等人工复位）",
              business.server.alarm_frames() == [],
              "%r" % (business.server.alarm_frames(),))
        ctl.clear_fault("测试收尾")

        # ---- F4 温度在阈值之下 ⇒ 不动作 ----
        state["ma"] = 12.0                       # (12-4)/16*100 = 50 ℃
        business.server.frames = []
        await business._protect_motor_over_temp_step(9000)
        await business._protect_motor_over_temp_step(12000)
        check("F4 温度未超阈值：不动作",
              business.server.alarm_frames() == []
              and ctl.fault_latched is False,
              "%r" % (business.server.alarm_frames(),))

        # ---- F5 温度读不到（None）⇒ 不判定（读不到 ≠ 超温）----
        state["ma"] = None
        await business._protect_motor_over_temp_step(13000)
        await business._protect_motor_over_temp_step(16000)
        check("F5 温度读不到：不判定、不动作",
              business.server.alarm_frames() == []
              and ctl.fault_latched is False,
              "%r" % (business.server.alarm_frames(),))

        # ---- F6 只报警模式（stop=false）⇒ 报警但不停设备 ----
        state["ma"] = 20.0
        business._cfg_set_protect_key(
            proto.CFGK_PROTECT_MOTOR_OVER_TEMP_STOP, False)
        business.server.frames = []
        await business._protect_motor_over_temp_step(17000)  # 计时起点
        await business._protect_motor_over_temp_step(20000)  # 超 2s ⇒ 动作
        frames = business.server.alarm_frames()
        check("F6 只报警：出告警但文案写「未停设备」",
              len(frames) == 1
              and "未停设备" in str(frames[0].get("text", "")),
              "%r" % (frames,))
        check("F6b 只报警：仍锁存（进报警表 ⇒ 需人工复位）",
              ctl.fault_latched is True,
              "latched=%r" % (ctl.fault_latched,))
        ctl.clear_fault("测试收尾")

        # ---- F7 通道可配：换到 CH3 后判定跟着读 CH3 ----
        business._cfg_set_collect_key(proto.CFGK_ANALOG_TEMP_CHANNEL, 3)
        state["ma"] = 4.0
        seen = []

        def _ma2(ch):
            seen.append(ch)
            return state["ma"]

        business.analog.ma = _ma2
        await business._protect_motor_over_temp_step(21000)
        check("F7 通道改 3：判定读 CH3（不再读 CH1）",
              seen and seen[0] == 3,
              "seen=%r" % (seen,))
        # 把通道 3 顶到超温 ⇒ 仍能触发（通道本身生效）。
        # [清 frames] F6 那条告警（atMs 20000）还留在列表里 —— 只数本次。
        state["ma"] = 20.0
        business.server.frames = []
        await business._protect_motor_over_temp_step(21500)  # 计时起点
        await business._protect_motor_over_temp_step(24000)  # 超 2s ⇒ 动作
        check("F7b 通道 3 超温：照常动作",
              len(business.server.alarm_frames()) == 1,
              "%r" % (business.server.alarm_frames(),))
        ctl.clear_fault("测试收尾")

        # ---- F8 配置落盘往返 ----
        business._cfg_set_protect_key(
            proto.CFGK_PROTECT_MOTOR_OVER_TEMP_ENABLED, True)
        business._cfg_set_protect_key(
            proto.CFGK_PROTECT_MOTOR_OVER_TEMP_LIMIT_C, 80.0)
        business._cfg_set_protect_key(
            proto.CFGK_PROTECT_MOTOR_OVER_TEMP_STOP, False)
        cfg.save()
        cfg2 = cfgmod.Config()
        cfg2.resolve_paths(basedir)
        cfg2.config_path = cfg.config_path
        cfg2.load()
        check("F8 落盘往返（启用/阈值/开关都一致）",
              cfg2.protect_motor_over_temp_enabled is True
              and abs(cfg2.protect_motor_over_temp_limit_c - 80.0) < 1e-6
              and cfg2.protect_motor_over_temp_stop is False,
              "enabled=%r limit=%r stop=%r"
              % (cfg2.protect_motor_over_temp_enabled,
                 cfg2.protect_motor_over_temp_limit_c,
                 cfg2.protect_motor_over_temp_stop))
    finally:
        shutil.rmtree(basedir, ignore_errors=True)


async def test_analog_channel_config():
    """G1~G10：模拟量通道/量程（下限~上限）远端可配 + 「4mA 以下有效」。

    [二改 2026-11-20] 用户口径「温度只要填上限和下限即可，范围自己算；
    压差计上限下限也要能设；增加 0-4 有效选项适配更多传感器」：
      · 量程 = 下限~上限：4mA→下限、20mA→上限（AnalogChannelScale.lowValue
        两点定标），「温度满量程」键废弃；
      · 「4mA 以下有效」逐传感器开关：开=0~4 段线性延续（压差计的负压
        特殊处理），关=欠量程钳到下限；压差计缺省开、温度缺省关。
    """
    print("=== G 模拟量通道与量程配置 ===")
    basedir = tempfile.mkdtemp(prefix="hsf_analogch_")
    try:
        business, cfg = _rig(basedir)

        # ---- G1 GET 缺省：压差 CH0 0~1MPa（4mA 以下有效）/ 温度 CH1
        #      0~100℃（4mA 以下无效）----
        doc = business._config_collect_doc()
        check("G1 GET 缺省：压差 CH0 0~1（below4=开）/ 温度 CH1 0~100（关）",
              doc.get(proto.CFGK_ANALOG_PRESS_CHANNEL) == 0
              and abs(float(doc.get(
                  proto.CFGK_ANALOG_PRESS_LOW_MPA))) < 1e-6
              and abs(float(doc.get(
                  proto.CFGK_ANALOG_PRESS_HIGH_MPA)) - 1.0) < 1e-6
              and doc.get(proto.CFGK_ANALOG_PRESS_BELOW4_VALID) is True
              and doc.get(proto.CFGK_ANALOG_TEMP_CHANNEL) == 1
              and abs(float(doc.get(
                  proto.CFGK_ANALOG_TEMP_LOW_C))) < 1e-6
              and abs(float(doc.get(
                  proto.CFGK_ANALOG_TEMP_HIGH_C)) - 100.0) < 1e-6
              and doc.get(proto.CFGK_ANALOG_TEMP_BELOW4_VALID) is False,
              "%r" % (doc,))

        # ---- G2 换算表：压差 CH0（lowValue 0 / below 开）CH1 温度（关）----
        check("G2 换算表：压差 lowValue=0/below 开；温度 lowValue=0/below 关",
              cfg.analog.scales[0].low_value == 0.0
              and cfg.analog.scales[0].below_valid is True
              and cfg.analog.scales[1].name == "温度传感器"
              and cfg.analog.scales[1].low_value == 0.0
              and cfg.analog.scales[1].below_valid is False,
              "press=%r temp=%r" % (cfg.analog.scales[0].to_doc(),
                                    cfg.analog.scales[1].to_doc()))

        # ---- G3 改通道 ⇒ 换算表跟着挪（CH1 撤、CH3 挂）----
        changed, restart = business._cfg_set_collect_key(
            proto.CFGK_ANALOG_TEMP_CHANNEL, 3)
        check("G3 改通道：changed=True 且无需重启轮询",
              changed is True and restart is False,
              "changed=%r restart=%r" % (changed, restart))
        check("G3b 换算表跟随：CH3=温度传感器、CH1 已撤",
              cfg.analog.scales.get(3) is not None
              and cfg.analog.scales[3].name == "温度传感器"
              and cfg.analog.scales.get(1) is None,
              "scales=%r" % (sorted(cfg.analog.scales),))
        business._cfg_set_collect_key(proto.CFGK_ANALOG_TEMP_CHANNEL, 1)

        # ---- G4 压差量程 -0.1~0.6：换算表两点定标（8mA ⇒ 0.075）----
        business._cfg_set_collect_key(proto.CFGK_ANALOG_PRESS_LOW_MPA, -0.1)
        business._cfg_set_collect_key(proto.CFGK_ANALOG_PRESS_HIGH_MPA, 0.6)
        v, _n = cfg.analog.scales[0].engineering(8.0)
        check("G4 压差量程 -0.1~0.6：8mA ⇒ 0.075 MPa",
              abs(v - 0.075) < 1e-9,
              "v=%r" % (v,))

        # ---- G5 压差 4mA 以下（below 开）⇒ 线性延续负压 ----
        v, n = cfg.analog.scales[0].engineering(3.0)
        check("G5 压差 3mA（below 开）⇒ -0.14375 + 标注「负压/线性延续」",
              abs(v - (-0.14375)) < 1e-9 and "负压" in n,
              "v=%r note=%r" % (v, n))
        # ---- G5b 温度 4mA 以下（below 缺省关）⇒ 欠量程钳到下限 ----
        v, n = cfg.analog.scales[1].engineering(3.0)
        check("G5b 温度 3mA（below 关）⇒ 0.0（钳下限）+ 标注「欠量程」",
              v == 0.0 and "欠量程" in n,
              "v=%r note=%r" % (v, n))
        # ---- G5c 打开温度 0~4 段有效 ⇒ 线性延续 ----
        business._cfg_set_collect_key(
            proto.CFGK_ANALOG_TEMP_BELOW4_VALID, True)
        v, n = cfg.analog.scales[1].engineering(3.0)
        check("G5c 温度 below 开：3mA ⇒ -6.25（线性）+ 标注",
                      abs(v - (-6.25)) < 1e-9 and "线性延续" in n,
              "v=%r note=%r" % (v, n))
        business._cfg_set_collect_key(
            proto.CFGK_ANALOG_TEMP_BELOW4_VALID, False)

        # ---- G6 温度量程 -10~120：temp_c 两点定标（8mA ⇒ 22.5℃）----
        business._cfg_set_collect_key(proto.CFGK_ANALOG_TEMP_LOW_C, -10.0)
        business._cfg_set_collect_key(proto.CFGK_ANALOG_TEMP_HIGH_C, 120.0)
        tc = cfg.analog.temp_c(lambda _ch: 8.0)
        check("G6 温度量程 -10~120：8mA ⇒ 22.5 ℃",
              tc is not None and abs(tc - 22.5) < 1e-9,
              "temp_c=%r" % (tc,))

        # ---- G7 越界/非法值明确拒（fail-closed）+ 下限≥上限交叉拒 ----
        rejects = []
        for key, bad in ((proto.CFGK_ANALOG_TEMP_CHANNEL, -1),
                         (proto.CFGK_ANALOG_TEMP_CHANNEL, 99),
                         (proto.CFGK_ANALOG_PRESS_CHANNEL, -5),
                         (proto.CFGK_ANALOG_PRESS_CHANNEL, 99),
                         (proto.CFGK_ANALOG_PRESS_LOW_MPA, -99.0),
                         (proto.CFGK_ANALOG_PRESS_HIGH_MPA, 0.0),
                         (proto.CFGK_ANALOG_PRESS_HIGH_MPA, 999.0),
                         (proto.CFGK_ANALOG_TEMP_LOW_C, -999.0),
                         (proto.CFGK_ANALOG_TEMP_HIGH_C, 9999.0),
                         (proto.CFGK_ANALOG_TEMP_CHANNEL, "x"),
                         (proto.CFGK_ANALOG_PRESS_BELOW4_VALID, "x"),
                         (proto.CFGK_ANALOG_TEMP_BELOW4_VALID, 1)):
            try:
                business._cfg_set_collect_key(key, bad)
            except ValueError:
                rejects.append(key)
        check("G7 越界/非法值 12 连拒（ValueError）", len(rejects) == 12,
              "rejects=%r" % (rejects,))
        # 交叉：温度下限 ≥ 当前上限 ⇒ 拒
        try:
            business._cfg_set_collect_key(proto.CFGK_ANALOG_TEMP_LOW_C, 999.0)
            cross_low_ok = False
        except ValueError:
            cross_low_ok = True
        try:
            business._cfg_set_collect_key(
                proto.CFGK_ANALOG_TEMP_HIGH_C, -99.0)
            cross_high_ok = False
        except ValueError:
            cross_high_ok = True
        check("G7b 交叉校验：下限≥上限 / 上限≤下限 都拒",
              cross_low_ok and cross_high_ok,
              "low_ok=%r high_ok=%r" % (cross_low_ok, cross_high_ok))

        # ---- G8 温度通道 = 压差通道：压差表保留（温度不占压差通道）----
        business._cfg_set_collect_key(proto.CFGK_ANALOG_TEMP_CHANNEL,
                                      cfg.analog.press_channel)
        check("G8 通道撞车：压差换算表保留在 CH0",
              cfg.analog.scales.get(0) is not None
              and cfg.analog.scales[0].name == "压差计",
              "names=%r"
              % ({k: v.name for k, v in cfg.analog.scales.items()},))
        business._cfg_set_collect_key(proto.CFGK_ANALOG_TEMP_CHANNEL, 1)

        # ---- G9 落盘往返（通道/量程/below4 全一致 + 重载后换算表重建）----
        business._cfg_set_collect_key(proto.CFGK_ANALOG_TEMP_CHANNEL, 2)
        business._cfg_set_collect_key(proto.CFGK_ANALOG_TEMP_LOW_C, -5.0)
        business._cfg_set_collect_key(proto.CFGK_ANALOG_TEMP_HIGH_C, 150.0)
        business._cfg_set_collect_key(proto.CFGK_ANALOG_TEMP_BELOW4_VALID,
                                      True)
        business._cfg_set_collect_key(proto.CFGK_ANALOG_PRESS_LOW_MPA, 0.0)
        business._cfg_set_collect_key(
            proto.CFGK_ANALOG_PRESS_HIGH_MPA, 1.6)
        cfg.save()
        cfg2 = cfgmod.Config()
        cfg2.resolve_paths(basedir)
        cfg2.config_path = cfg.config_path
        cfg2.load()
        sc = cfg2.analog.scales.get(2)
        check("G9 落盘往返：CH2 / -5~150 ℃（below 开）/ 压差 0~1.6 / 重建",
              cfg2.analog.temp_channel == 2
              and abs(cfg2.analog.temp_low_c + 5.0) < 1e-6
              and abs(cfg2.analog.temp_high_c - 150.0) < 1e-6
              and cfg2.analog.temp_below4_valid is True
              and abs(cfg2.analog.press_high_mpa - 1.6) < 1e-6
              and cfg2.analog.press_below4_valid is True
              and sc is not None and sc.name == "温度传感器"
              and sc.low_value == -5.0 and sc.below_valid is True,
              "ch=%r scales=%r" % (cfg2.analog.temp_channel,
                                   sorted(cfg2.analog.scales)))

        # ---- G10 GET 文档键齐（7 键）----
        doc = business._config_collect_doc()
        keys = (proto.CFGK_ANALOG_PRESS_CHANNEL,
                proto.CFGK_ANALOG_PRESS_LOW_MPA,
                proto.CFGK_ANALOG_PRESS_HIGH_MPA,
                proto.CFGK_ANALOG_PRESS_BELOW4_VALID,
                proto.CFGK_ANALOG_TEMP_CHANNEL,
                proto.CFGK_ANALOG_TEMP_LOW_C,
                proto.CFGK_ANALOG_TEMP_HIGH_C,
                proto.CFGK_ANALOG_TEMP_BELOW4_VALID)
        check("G10 GET 文档模拟量 8 键齐",
              all(k in doc for k in keys),
              "missing=%r" % ([k for k in keys if k not in doc],))
    finally:
        shutil.rmtree(basedir, ignore_errors=True)


def main():
    asyncio.run(_run())
    print("=" * 62)
    print("  汇总：PASS %d / FAIL %d" % (PASS, FAIL))
    return 0 if FAIL == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
