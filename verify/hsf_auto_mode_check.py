#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""HSF「自动模式三模式序列」C 服务端协议面端到端复检（无人值守，退出码表成败）。

[背景] 2026-11-11 用户定案的三模式自动序列（产水/反洗/顺洗）已在
  server_py/hsfsrv/autocontrol.py（Python 事实源）与
  HighSpeedFilter/server/HsfServerAutoMode.c（C 端镜像）分别落地。
  Python 侧锁：tests/test_auto_modes.py + tests/test_auto_alarm_publish.py；
  C 端逻辑层锁：Test/XProtocolTest/hsf_regression_test.c §S。
  本脚本补的是**协议面**的端到端复检——起 bin/HsfServer.exe --sim 的隔离
  实例（Python test_auto_modes 的 C 端等价物），纯协议（client_py 的
  XLinkClient，无 Qt）跑三个场景：

  S1 产水全循环（现场现状口径：只有阀1 接线、阀2~6 未接线，模式 devices
     只配 [valve1, inletPump, stirPump]——现场在远端设置里会做的配置）：
     auto 配置 SET/GET 往返一致；手动模式启动拒（含「手动」）；
     切自动 → 带模式启动受理（应答 autoMode=produce / autoPhase=valves）；
     选中阀 DO0=1、未选阀 DO1..5=0（数字量快照 0x0108 位图；列表外阀的
     目标=关=开机稳态，不发令也保持 0）；模拟行程到位 → running；
     搅拌泵 720rpm 反转的折算给定（setFreqHz≈25Hz）+ 进水泵 DO6=1
     （变频器快照 0x0102）；停止 = 全停（泵关/阀关/变频器减速停）；
     停止后 autoMode 粘性保留。搅拌泵能启动即「命令源预置」的**按需断言**
     ——服务端 --sim 装配（HsfServerIo.c，与 §S 夹具同一套）已把两侧
     P0.02 切通讯；若装配丢该步，Start 被闸（「命令源未切到通讯」）、
     相位停在 pumps，S1 在⑥即红。
  S2 未接线阀保护（卡阀夹具 --sim-valve-move -1 正是未接线阀行为：DO 下发
     无物理动作、DI 到位反馈永不到达）：反洗（阀2+进水泵，对齐现场
     「误选了没接线的阀2」）启动 → 阀 2 永远不到位 ⇒ 收 kind=5 告警帧
     （文字点名「阀2」+「超时」）；泵绝不被启动（DO6=0）；autoFault=true；
     未复位再启动拒（含「复位」）；复位后可再启动。
  S3 急停复位 + 闸门面：「自动」组合模式拒（循环配置校验：三子模式缺
     设备即拒，2026-11-12 循环落地后口径）；未配置模式
     拒（含「未配置」）；运行中急停 ⇒ 停泵/变频器 + **阀保持现状**
     （resetValvesOnEstop 缺省 false，2026-11-12 逐模式阀复位口径）+
     autoPhase=estop + autoFault（急停锁存走 autoPhase/estop 呈现）⇒
     锁存拒启动（含「复位」）；复位后可再启动。
  T8 包络面（随场景断言）：0x0105 与 0x0115 的每一条应答（成功与拒绝）
     恒带全量键 mode/auto/autoRunning/autoMode/autoPhase/autoFault——
     「拒绝也回全量键」（2026-11-11 定案）的线面锁。

[为什么阀超时场景用真 20s 产品缺省] C 端阀组动作超时是编译期宏
  （HSF_VALVE_MOVE_TIMEOUT_MS=20000），进程级 CLI 只能改**模拟行程**不能
  改判超时口径 ⇒ 卡阀告警必然在 ~20s 后到（含阀层判故障）。这是本脚本
  比 Python 版（valve_move_timeout_ms=400 可配）慢的那一段，属产品口径
  而非脚本缺陷；S1/S3 用 --sim-valve-move 600 快进行程保总时长可控。

跑法（先编译 bin/HsfServer.exe）：
    py -3 verify/hsf_auto_mode_check.py
    （本脚本纯协议零 Qt，任意 Python3 均可——勿用 D:/anaconda3/python.exe：
    那是 test_client.py 这类 Qt 用例的口径，无 anaconda 的机器上该解释器
    不存在，cmd 直接报「系统找不到指定的路径」连脚本都进不去。）
退出码 0=全过，1=有失败。脚本自起自停服务端（隔离临时目录，不碰现场
bin/ 的任何文件），结束必杀进程。
"""
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import time

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CLIENT_ROOT = os.path.join(REPO_ROOT, "HighSpeedFilter", "client_py")
if CLIENT_ROOT not in sys.path:
    sys.path.insert(0, CLIENT_ROOT)

from hsfsrv_client import bson, proto            # noqa: E402
from hsfsrv_client.xlink import XLinkClient      # noqa: E402

#: 自动运行告警主题（client_py proto 未登记；值 = common/XhsfLinkProto.h 的
#: HSF_TOPIC_AUTO_ALARM，与 server_py/hsfsrv/proto.py 同值，勿改动）。
TOPIC_AUTO_ALARM = 0x0001000B

#: 落盘采样周期经远端配置下调到 1s/行（与 verify/hsf_export_filter_check.py
#: 同口径；本脚本的断言不依赖历史行数，下调只为顺带验配置通道）。
SIM_SAMPLE_MS = 1000
#: 阀快速行程（毫秒）：S1/S3 实例的 --sim-valve-move（真实阀 ≈9s 的快进）。
FAST_MOVE_MS = 600
#: 等相位推进的通用超时（秒；阀相 600ms + 泵相 + 推送节拍）。
PHASE_TIMEOUT_S = 15.0
#: 卡阀告警预算（秒）：阀层 20s 产品缺省判故障 + 序列宽限 + 推送节拍。
VALVE_TIMEOUT_BUDGET_S = 40.0
#: 变频器减速停预算（秒；25Hz ÷ 5Hz/s = 5s，留余量）。
VFD_STOP_BUDGET_S = 12.0

_G_FAILS = []
_G_CHECKS = [0]


def check(name, ok, detail=""):
    _G_CHECKS[0] += 1
    tag = "[PASS]" if ok else "[FAIL]"
    line = "%s %s" % (tag, name)
    if detail:
        line += "  (%s)" % detail
    print(line)
    if not ok:
        _G_FAILS.append(name)


def free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


class IsolatedServer(object):
    """bin/HsfServer.exe 的隔离实例（复制到临时目录起 --sim，绝不碰现场）。"""

    def __init__(self, name, extra_args=()):
        self.name = name
        self.extra_args = list(extra_args)
        self.tmp = None
        self.proc = None
        self.port = 0
        self.log_path = None

    def start(self):
        exe = os.path.join(REPO_ROOT, "bin", "HsfServer.exe")
        if not os.path.isfile(exe):
            raise RuntimeError("找不到 %s（先编译 HsfServer）" % exe)
        if not self.tmp:
            self.tmp = tempfile.mkdtemp(prefix="hsf_automode_%s_" % self.name)
            shutil.copy2(exe, os.path.join(self.tmp, "HsfServer.exe"))
        self.port = free_port()
        self.log_path = os.path.join(self.tmp, "HsfServer.out")
        out = open(self.log_path, "wb")   # 管道会写满卡死服务端，落文件。
        self.proc = subprocess.Popen(
            [os.path.join(self.tmp, "HsfServer.exe"), "--sim",
             str(self.port)] + self.extra_args,
            cwd=self.tmp, stdout=out, stderr=subprocess.STDOUT)
        deadline = time.time() + 30
        while time.time() < deadline:
            if self.proc.poll() is not None:
                out.close()
                raise RuntimeError("服务端%s秒退：%s" % (self.name, self.tail()))
            try:
                sock = socket.create_connection(("127.0.0.1", self.port), 0.3)
                sock.close()
                return self.port
            except OSError:
                time.sleep(0.2)
        out.close()
        raise RuntimeError("服务端%s 30s 未监听端口 %d" % (self.name, self.port))

    def tail(self, n=600):
        try:
            with open(self.log_path, "rb") as fh:
                return fh.read()[-n:].decode("utf-8", "replace")
        except OSError:
            return ""

    def stop(self):
        if self.proc is not None and self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=6)
            except Exception:                          # noqa: BLE001
                self.proc.kill()
        self.proc = None


class ProtoClient(object):
    """纯协议客户端（client_py 的 XLinkClient；无 Qt 依赖）。

    [为什么不用 LinkBridge] 桥把 publish 收进 model（页面消费），拿不到
    原始告警帧；本脚本要按帧断言 0x0001000B 的 kind/text，直接用
    XLinkClient 的 on_publish 回调（网络线程，append 到列表靠 GIL 保护）。
    """

    def __init__(self, port):
        self.port = port
        self.client = None
        self.publishes = []               # [(topic, doc|None), ...]

    def connect(self):
        c = XLinkClient("127.0.0.1", self.port, "root", "root",
                        log=lambda m: None)

        def on_publish(topic, body, dtype):
            doc = None
            if dtype == proto.DATATYPE_BSON:
                try:
                    doc = bson.decode(body)
                except ValueError:
                    doc = None
            self.publishes.append((topic, doc))

        c.on_publish = on_publish
        ok, err = c.start(timeout=10.0)
        if not ok:
            raise RuntimeError("登录失败（root/root@%d）：%s" % (self.port, err))
        self.client = c
        return self

    def close(self):
        if self.client is not None:
            self.client.close()
            self.client = None

    def request(self, cmd, doc=None, timeout=8.0):
        """同步请求（call_soon 投递 + future 等待；桥的 request_sync 同款）。"""
        payload = bson.encode(doc) if doc is not None else b""
        fut = self.client.call_soon(self.client.request(
            cmd, payload, proto.DATATYPE_BSON, timeout))
        if fut is None:
            return None
        try:
            _code, dtype, data = fut.result(timeout=timeout + 2.0)
        except Exception:                          # noqa: BLE001
            return None
        try:
            return bson.decode(data) if dtype == proto.DATATYPE_BSON else {}
        except ValueError:
            return None

    def subscribe(self, topic):
        fut = self.client.call_soon(self.client.subscribe(topic))
        if fut is None:
            return False
        try:
            return bool(fut.result(timeout=5.0))
        except Exception:                          # noqa: BLE001
            return False

    def alarm_frames(self, clear=False):
        frames = [doc for (t, doc) in self.publishes
                  if t == TOPIC_AUTO_ALARM and doc is not None]
        if clear:
            del self.publishes[:]
        return frames


# ---- 协议面小工具 -----------------------------------------------------------

def check_envelope(doc, tag):
    """T8 线面锁：0x0105/0x0115 应答（成功与拒绝都算）恒带全量状态键。"""
    keys = ("mode", "auto", proto.KEY_AUTO_RUNNING, proto.KEY_AUTO_MODE,
            proto.KEY_AUTO_PHASE, proto.KEY_AUTO_FAULT)
    miss = [k for k in keys if not isinstance(doc, dict) or k not in doc]
    check("%s 包络键全量（mode/auto/autoRunning/autoMode/autoPhase/autoFault）"
          % tag, not miss, "缺=%s" % (miss or "无"))


def set_run_mode(cli, mode, tag):
    doc = cli.request(proto.CMD_RUN_MODE, {"mode": mode})
    check_envelope(doc, tag)
    return bool(doc and doc.get(proto.KEY_OK))


def auto_control(cli, payload, tag):
    doc = cli.request(proto.CMD_AUTO_CONTROL, payload, timeout=10.0)
    check_envelope(doc, tag)
    return doc or {}


def run_mode_state(cli, tag=""):
    doc = cli.request(proto.CMD_RUN_MODE, None)
    if tag:
        check_envelope(doc, tag)
    return doc or {}


def digital_snapshot(cli, dev=0):
    doc = cli.request(proto.CMD_DIGITAL_SNAPSHOT, {"dev": dev})
    return doc or {}


def do_mask(cli, dev=0):
    doc = digital_snapshot(cli, dev)
    if not doc or not doc.get(proto.KEY_VALID):
        return None
    return int(doc.get(proto.KEY_DO) or 0)


def vfd_snapshot(cli, side=0):
    doc = cli.request(proto.CMD_VFD_SNAPSHOT, {"side": side})
    return doc or {}


def wait_until(fn, timeout_s, step=0.3):
    deadline = time.time() + timeout_s
    last = None
    while time.time() < deadline:
        last = fn()
        if last:
            return last
        time.sleep(step)
    return fn()


# ---- 场景一：产水全循环（S1） -----------------------------------------------

def case_produce_full_cycle(cli):
    """S1：配置往返 / 手动拒 / 阀相 DO 面 / running / 泵面 / 停止全停 / 粘性。"""
    # ---- ① auto 配置 SET/GET 往返一致（现场现状：只配已接线的阀1）----
    # [devices 按集合比较 2026-11-12] C 端回显按设备表规范序（valve1..6/
    # stirPump/inletPump 的下标序）规范化——功能行为只吃掩码、与顺序无关，
    # 两端真实 UI 客户端发的就是规范序（MultiCheckCombo 固定 8 项序）；
    # Python 端保留输入序。此处按集合+逐字段断言，不锁顺序。
    want_devs = ["valve1", "inletPump", "stirPump"]
    cfg = cli.request(proto.CMD_CONFIG_SET, {
        "section": proto.CFG_SECTION_AUTO,
        "modes": {"produce": {"devices": want_devs,
                              "stirRpm": 720, "stirReverse": True}}})
    got = ((cfg or {}).get(proto.CFG_SECTION_AUTO) or {}).get(
        "modes", {}).get("produce", {})
    check("S1 配置 SET 受理且回显 devices/rpm/reverse（现场现状配置）",
          bool(cfg and cfg.get(proto.KEY_OK))
          and sorted(got.get("devices") or []) == sorted(want_devs)
          and got.get("stirRpm") == 720 and got.get("stirReverse") is True
          # [2026-11-12] 逐模式阀复位两键随回显下发（缺省 true/false）。
          and got.get("resetValvesOnStop") is True
          and got.get("resetValvesOnEstop") is False,
          "回显=%s" % (got,))
    allcfg = cli.request(proto.CMD_CONFIG_GET, None)
    auto_doc = ((allcfg or {}).get(proto.CFG_SECTION_AUTO) or {}).get(
        "modes", {})
    check("S1 配置 GET 与 SET 一致",
          sorted(auto_doc.get("produce", {}).get("devices") or []) ==
          sorted(want_devs)
          and auto_doc.get("produce", {}).get("stirRpm") == 720,
          "auto 节=%s" % (auto_doc,))
    check("S1 未配置模式 = 空清单（缺省口径）",
          auto_doc.get("backwash", {}).get("devices") == [],
          "backwash=%s" % (auto_doc.get("backwash"),))

    # ---- ② 落盘采样周期下调（顺带验远端配置通道；1s/行）----
    st = cli.request(proto.CMD_CONFIG_SET, {
        "section": "storage", "enabled": True, "sampleMs": SIM_SAMPLE_MS})
    check("S1 远端配置：落盘采样周期下调生效（root 可写）",
          bool(st and st.get(proto.KEY_OK)), "应答=%s" % (st or {}).get("err"))

    # ---- ③ 手动模式启动被拒（含「手动」）----
    check("S1 切手动受理", set_run_mode(cli, 0, "S1 切手动"))
    r = auto_control(cli, {"op": 1, proto.KEY_AUTO_MODE: "produce"},
                     "S1 手动启动拒")
    check("S1 手动模式启动被拒且原因说手动",
          not r.get(proto.KEY_OK) and "手动" in str(r.get(proto.KEY_ERR, "")),
          "err=%s" % r.get(proto.KEY_ERR))

    # ---- ④ 切自动 → 带模式启动受理：autoMode=produce / autoPhase=valves ----
    check("S1 切自动受理", set_run_mode(cli, 1, "S1 切自动"))
    r = auto_control(cli, {"op": 1, proto.KEY_AUTO_MODE: "produce"},
                     "S1 启动")
    check("S1 启动受理且回显 autoMode=produce / autoPhase=valves",
          r.get(proto.KEY_OK) and r.get(proto.KEY_AUTO_MODE) == "produce"
          and r.get(proto.KEY_AUTO_PHASE) == "valves",
          "应答=%s" % r)

    # ---- ⑤ 阀相 DO 面：选中阀 DO0=1、未选阀 DO1..5=0（现场现状：阀2~6
    #      未接线也不在 devices 里 ⇒ 目标=关=开机稳态，不发令也保持 0）----
    def valves_aimed():
        m = do_mask(cli)
        if m is None:
            return False
        return (m & 0x1) == 0x1 and (m & 0x3E) == 0

    check("S1 选中阀 DO0=1、未选阀 DO1..5=0（数字量快照位图）",
          wait_until(valves_aimed, PHASE_TIMEOUT_S),
          "do=0x%X" % (do_mask(cli) or 0))

    # ---- ⑥ 模拟到位 → running ----
    state = wait_until(
        lambda: run_mode_state(cli).get(proto.KEY_AUTO_PHASE) == "running",
        PHASE_TIMEOUT_S)
    check("S1 阀全部到位后推进到 running", bool(state),
          "phase=%s" % run_mode_state(cli).get(proto.KEY_AUTO_PHASE))

    # ---- ⑦ 泵按配置启动：搅拌泵 720rpm 反转折算给定 + 进水泵 DO6=1 ----
    m = do_mask(cli)
    check("S1 进水泵（DO6）已启动", m is not None and (m & (1 << 6)) != 0,
          "do=0x%X" % (m or 0))
    # [等待式断言 2026-11-12] 命令字经 Hub 排队、运行位/方向经站轮询回读
    # ——受理 ≠ 已置（C §S1 同款实锅：定点读在满载门禁机上偶发双红）。
    # 模型在写命令那一刻同时落运行与方向 ⇒ 两态同拍回读，一并等待。
    def stir_running_reverse():
        v = vfd_snapshot(cli)
        return bool(v.get("running")) and bool(v.get("reverse"))

    check("S1 搅拌泵已启动且方向 = 配置的反转",
          bool(wait_until(stir_running_reverse, 6.0)),
          "running=%s reverse=%s"
          % (vfd_snapshot(cli).get("running"),
             vfd_snapshot(cli).get("reverse")))
    vfd = vfd_snapshot(cli)
    check("S1 给定 = 720rpm 折算（额定 1440rpm/50Hz 口径 ⇒ 25Hz）",
          abs(float(vfd.get("setFreqHz") or 0.0) - 25.0) < 0.05,
          "setFreqHz=%s" % vfd.get("setFreqHz"))

    # ---- ⑧ 停止 = 全停（泵关 / 阀关 / 变频器减速停）----
    r = auto_control(cli, {"op": 2}, "S1 停止")
    check("S1 停止受理", bool(r.get(proto.KEY_OK)), "err=%s" % r.get("err"))

    def all_off():
        m = do_mask(cli)
        v = vfd_snapshot(cli)
        return (m is not None and (m & 0x7F) == 0
                and not bool(v.get("running")))

    check("S1 停止后泵/阀输出清零且变频器减速停到位", bool(wait_until(
        all_off, VFD_STOP_BUDGET_S)),
        "do=0x%X running=%s" % (do_mask(cli) or 0,
                                vfd_snapshot(cli).get("running")))
    state = run_mode_state(cli, "S1 停止后查询")
    check("S1 停止后 autoRunning=false 且模式粘性保留 produce",
          state.get(proto.KEY_AUTO_RUNNING) is False
          and state.get(proto.KEY_AUTO_MODE) == "produce",
          "autoRunning=%s autoMode=%s" % (state.get(proto.KEY_AUTO_RUNNING),
                                          state.get(proto.KEY_AUTO_MODE)))


# ---- 场景一b：切模式停控制器 + devices 整表替换（S4，快行程实例） ----------

def case_mode_switch_and_devices(cli):
    """S4：切模式停控制器双向（幂等重放不杀序列）+ devices 整表替换。

    [2026-11-13·对齐 Python apply_mode_to_devices] 原先 C 端停控制器条件
    `autoMode && running()` 与 Python 口径正好取反：切回手动不停控制器
    （序列在手动模式继续驱动设备）＋ 0x0105 幂等重发 mode=1 反而杀掉运行
    中的序列。现改「新模式为手动一律停；自动仅真切换停」，本场景端到端锁
    双向（HsfServerApp.c 不在测试宿主编译面内 ⇒ §S 链不到，锁在这里）。
    [devices 整表替换] Python app.py 是 `target["devices"]=clean` 整表覆盖
    ⇒ 子集=只剩子集、空数组=清空；原 C 端「与现值合并只置 true」⇒ 取消
    勾选/清空被静默忽略，已对齐（§S12 逻辑层锁 + 此处协议面锁）。
    """
    # ---- ① 切自动并启动（复用 S1 的 produce 配置：阀1 快行程 600ms）----
    check("S4 切自动受理", set_run_mode(cli, 1, "S4 切自动"))
    r = auto_control(cli, {"op": 1, proto.KEY_AUTO_MODE: "produce"}, "S4 启动")
    check("S4 启动受理", bool(r.get(proto.KEY_OK)), "err=%s" % r.get("err"))
    check("S4 运行相推进", bool(wait_until(
        lambda: run_mode_state(cli).get(proto.KEY_AUTO_PHASE) == "running",
        PHASE_TIMEOUT_S)),
        "phase=%s" % run_mode_state(cli).get(proto.KEY_AUTO_PHASE))

    # ---- ② 幂等重放 0x0105 mode=1：序列必须继续跑（autoRunning 仍 true；
    #         旧口径在这里杀掉生产序列）----
    r = cli.request(proto.CMD_RUN_MODE, {"mode": 1})
    check_envelope(r, "S4 幂等重放")
    state = run_mode_state(cli, "S4 幂等重放后查询")
    check("S4 幂等重发 mode=1 不杀运行中的序列（autoRunning 仍 true）",
          bool(r.get(proto.KEY_OK))
          and state.get(proto.KEY_AUTO_RUNNING) is True,
          "ok=%s autoRunning=%s" % (r.get(proto.KEY_OK),
                                    state.get(proto.KEY_AUTO_RUNNING)))

    # ---- ③ 切回手动：控制器必须被停（autoRunning=false；旧口径不停 ⇒
    #         手动模式下设备被序列偷偷驱动）----
    check("S4 切手动受理", set_run_mode(cli, 0, "S4 切手动"))
    state = run_mode_state(cli, "S4 切手动后查询")
    check("S4 切回手动后控制器已停（autoRunning=false）",
          state.get(proto.KEY_AUTO_RUNNING) is False,
          "autoRunning=%s phase=%s" % (state.get(proto.KEY_AUTO_RUNNING),
                                       state.get(proto.KEY_AUTO_PHASE)))

    # ---- ④ devices 整表替换：子集 SET ⇒ 只剩子集；空数组 SET ⇒ 清空 ----
    cfg = cli.request(proto.CMD_CONFIG_SET, {
        "section": proto.CFG_SECTION_AUTO,
        "modes": {"produce": {"devices": ["valve1"]}}})
    got = ((cfg or {}).get(proto.CFG_SECTION_AUTO) or {}).get(
        "modes", {}).get("produce", {})
    check("S4 devices 子集 SET = 整表替换（只剩 valve1）",
          bool(cfg and cfg.get(proto.KEY_OK))
          and sorted(got.get("devices") or []) == ["valve1"],
          "devices=%s" % (got.get("devices"),))
    cfg = cli.request(proto.CMD_CONFIG_SET, {
        "section": proto.CFG_SECTION_AUTO,
        "modes": {"produce": {"devices": []}}})
    got = ((cfg or {}).get(proto.CFG_SECTION_AUTO) or {}).get(
        "modes", {}).get("produce", {})
    check("S4 devices 空数组 SET = 清空（取消勾选不再被静默忽略）",
          bool(cfg and cfg.get(proto.KEY_OK)) and got.get("devices") == [],
          "devices=%s" % (got.get("devices"),))

    # ---- ⑤ 收尾：恢复现场现状配置（后续场景各自再 SET，保持脚本自洽）----
    cli.request(proto.CMD_CONFIG_SET, {
        "section": proto.CFG_SECTION_AUTO,
        "modes": {"produce": {"devices": ["valve1", "inletPump", "stirPump"],
                              "stirRpm": 720, "stirReverse": True}}})


# ---- 场景二：阀超时（S2，卡阀实例） -----------------------------------------

def case_valve_timeout(cli):
    """S2：未接线阀保护 ⇒ kind=5 告警 / 泵不启动 / autoFault / 未复位拒 / 复位再启。

    [现场现状口径] 电动球阀只有阀1 接线，阀2~6 未接线（DO 下发无物理动作、
    DI 到位反馈永不到达）。卡阀夹具（--sim-valve-move -1）正是这个行为：
    命令受理、行程永不完成。选中阀2 = 现场误配场景 ⇒ 序列必须在阀2 超时处
    截止（保护，不是 bug）：kind=5 告警点名阀2、泵不被启动、报警锁存，
    复位后才能再启动。
    """
    cli.subscribe(TOPIC_AUTO_ALARM)
    cfg = cli.request(proto.CMD_CONFIG_SET, {
        "section": proto.CFG_SECTION_AUTO,
        "modes": {"backwash": {"devices": ["valve2", "inletPump"],
                               "stirRpm": 1440}}})
    check("S2 反洗配置 SET 受理（现场现状：误选未接线阀2）",
          bool(cfg and cfg.get(proto.KEY_OK)),
          "err=%s" % (cfg or {}).get("err"))
    check("S2 订阅告警主题 0x0001000B",
          bool(cli.client and TOPIC_AUTO_ALARM in cli.client.subscriptions))
    check("S2 切自动受理", set_run_mode(cli, 1, "S2 切自动"))
    del cli.publishes[:]
    r = auto_control(cli, {"op": 1, proto.KEY_AUTO_MODE: "backwash"}, "S2 启动")
    check("S2 启动受理（进阀相）",
          r.get(proto.KEY_OK) and r.get(proto.KEY_AUTO_PHASE) == "valves",
          "应答=%s" % r)

    # 阀 2 收命令但永远不到位（未接线：--sim-valve-move -1 同行为）⇒
    # 阀层 20s 判故障 ⇒ 序列截止判定 kind=5 告警 + 报警锁存 + 停自动模式。
    frames = wait_until(
        lambda: [f for f in cli.alarm_frames()
                 if f.get("kind") == proto.AUTO_ALARM_VALVE_TIMEOUT],
        VALVE_TIMEOUT_BUDGET_S, step=0.5)
    check("S2 收到 kind=5 阀超时告警帧", bool(frames),
          "全部告警帧=%s" % cli.alarm_frames())
    if frames:
        text = str(frames[0].get("text", ""))
        check("S2 告警文字点名阀2 +「超时」",
              "阀2" in text and "超时" in text, "text=%s" % text)

    state = run_mode_state(cli, "S2 告警后查询")
    check("S2 停止自动模式 + autoFault=true",
          state.get(proto.KEY_AUTO_RUNNING) is False
          and state.get(proto.KEY_AUTO_FAULT) is True,
          "autoRunning=%s autoFault=%s" % (state.get(proto.KEY_AUTO_RUNNING),
                                           state.get(proto.KEY_AUTO_FAULT)))
    m = do_mask(cli)
    check("S2 泵绝不被启动（阀没到位就不许启动泵：DO6=0）",
          m is not None and (m & (1 << 6)) == 0, "do=0x%X" % (m or 0))

    r = auto_control(cli, {"op": 1, proto.KEY_AUTO_MODE: "backwash"},
                     "S2 未复位再启动")
    check("S2 报警未复位再启动被拒且原因提示复位",
          not r.get(proto.KEY_OK) and "复位" in str(r.get(proto.KEY_ERR, "")),
          "err=%s" % r.get(proto.KEY_ERR))

    r = auto_control(cli, {"op": 4}, "S2 复位")
    check("S2 复位受理且 autoFault 回落 false",
          bool(r.get(proto.KEY_OK)) and r.get(proto.KEY_AUTO_FAULT) is False,
          "应答=%s" % r)
    r = auto_control(cli, {"op": 1, proto.KEY_AUTO_MODE: "backwash"}, "S2 再启动")
    check("S2 复位后允许再启动", bool(r.get(proto.KEY_OK)),
          "err=%s" % r.get(proto.KEY_ERR))
    auto_control(cli, {"op": 2}, "S2 收尾停止")


# ---- 场景三：急停复位 + 闸门面（S3，复用快行程实例） ------------------------

def case_estop_and_gates(cli):
    """S3：组合/未配置拒 / 急停全停 + estop 相位 / 锁存拒 / 复位解锁。"""
    cfg = cli.request(proto.CMD_CONFIG_SET, {
        "section": proto.CFG_SECTION_AUTO,
        "modes": {"produce": {"devices": ["valve1", "stirPump"],
                              "stirRpm": 1440}}})
    check("S3 产水配置 SET 受理（阀1 + 搅拌泵）",
          bool(cfg and cfg.get(proto.KEY_OK)), "err=%s" % (cfg or {}).get("err"))
    check("S3 切自动受理（S1 收尾已在自动，显式重发兜底）",
          set_run_mode(cli, 1, "S3 切自动"))

    r = auto_control(cli, {"op": 1, proto.KEY_AUTO_MODE: proto.AUTO_MODE_AUTO},
                     "S3 组合模式")
    # [2026-11-12 断言更新] 组合模式「自动」已随循环模式落地（C 端
    # HsfServerAutoMode amCycleTick + Python _cycle_tick）：未配三子模式
    # 设备时拒因是循环配置校验（「需要三个子模式都配设备」），不再是
    # 旧口径的「暂未实现」。
    check("S3 「自动」组合模式拒且按循环配置校验（缺子模式设备）",
          not r.get(proto.KEY_OK)
          and "子模式" in str(r.get(proto.KEY_ERR, "")),
          "err=%s" % r.get(proto.KEY_ERR))
    r = auto_control(cli, {"op": 1, proto.KEY_AUTO_MODE: "rinse"}, "S3 未配置")
    check("S3 未配置模式拒且说明去哪配",
          not r.get(proto.KEY_OK) and "未配置" in str(r.get(proto.KEY_ERR, "")),
          "err=%s" % r.get(proto.KEY_ERR))

    r = auto_control(cli, {"op": 1, proto.KEY_AUTO_MODE: "produce"}, "S3 启动")
    check("S3 启动受理", bool(r.get(proto.KEY_OK)), "err=%s" % r.get("err"))
    check("S3 运行相推进", bool(wait_until(
        lambda: run_mode_state(cli).get(proto.KEY_AUTO_PHASE) == "running",
        PHASE_TIMEOUT_S)),
        "phase=%s" % run_mode_state(cli).get(proto.KEY_AUTO_PHASE))

    # ---- 运行中急停：全停 + 相位 estop；复位前不能启动 ----
    r = auto_control(cli, {"op": 3}, "S3 急停")
    state = run_mode_state(cli, "S3 急停后查询")
    check("S3 急停受理且相位 = estop / autoRunning=false",
          bool(r.get(proto.KEY_OK))
          and state.get(proto.KEY_AUTO_PHASE) == "estop"
          and state.get(proto.KEY_AUTO_RUNNING) is False,
          "ok=%s phase=%s autoRunning=%s" % (r.get(proto.KEY_OK),
                                             state.get(proto.KEY_AUTO_PHASE),
                                             state.get(proto.KEY_AUTO_RUNNING)))
    m = do_mask(cli)
    # [2026-11-12 断言更新·逐模式阀复位] 急停缺省 resetValvesOnEstop=false
    # ⇒ **阀保持现状**（阀1 已开保持开、未选阀保持关），只有泵/变频器必停
    # （DO6=0）——旧口径「急停也关阀」已被用户定案取代（AGENTS.md ⑦；
    # C §S3 同款断言「DO0 仍在」）。
    check("S3 急停：阀保持现状（阀1 DO0 保持开、阀2..6 保持关）、泵停（DO6=0）",
          m is not None and (m & 0x01) == 0x01 and (m & 0x3E) == 0
          and (m & 0x40) == 0, "do=0x%X" % (m or 0))
    r = auto_control(cli, {"op": 1, proto.KEY_AUTO_MODE: "produce"},
                     "S3 锁存启动")
    check("S3 急停锁存拒启动（含「复位」）",
          not r.get(proto.KEY_OK) and "复位" in str(r.get(proto.KEY_ERR, "")),
          "err=%s" % r.get(proto.KEY_ERR))
    auto_control(cli, {"op": 4}, "S3 复位")
    r = auto_control(cli, {"op": 1, proto.KEY_AUTO_MODE: "produce"}, "S3 再启动")
    check("S3 复位后可再启动", bool(r.get(proto.KEY_OK)),
          "err=%s" % r.get(proto.KEY_ERR))
    auto_control(cli, {"op": 2}, "S3 收尾停止")


def main():
    server_fast = IsolatedServer("fast", ["--sim-valve-move", str(FAST_MOVE_MS)])
    server_stuck = IsolatedServer("stuck", ["--sim-valve-move", "-1"])
    cli = None
    try:
        port = server_fast.start()
        print("== 隔离服务端 A（阀行程 %dms）已起：127.0.0.1:%d（tmp=%s）"
              % (FAST_MOVE_MS, port, server_fast.tmp))
        cli = ProtoClient(port).connect()
        case_produce_full_cycle(cli)
        case_mode_switch_and_devices(cli)
        case_estop_and_gates(cli)
        cli.close()
        cli = None

        server_fast.stop()
        port = server_stuck.start()
        print("== 隔离服务端 B（阀卡死）已起：127.0.0.1:%d（tmp=%s）"
              % (port, server_stuck.tmp))
        cli = ProtoClient(port).connect()
        case_valve_timeout(cli)
        cli.close()
        cli = None
        return 0
    except Exception as exc:                           # noqa: BLE001
        print("[FAIL] 异常：%s: %s" % (type(exc).__name__, exc))
        print(server_fast.tail())
        print(server_stuck.tail())
        return 1
    finally:
        if cli is not None:
            try:
                cli.close()
            except Exception:                          # noqa: BLE001
                pass
        server_fast.stop()
        server_stuck.stop()
        shutil.rmtree(server_fast.tmp, ignore_errors=True)
        shutil.rmtree(server_stuck.tmp, ignore_errors=True)
        print("== 收尾：两个隔离服务端已停，临时目录已删（检查 %d 项，失败 %d 项）"
              % (_G_CHECKS[0], len(_G_FAILS)))
        for name in _G_FAILS:
            print("   失败项：%s" % name)


if __name__ == "__main__":
    sys.exit(main())
