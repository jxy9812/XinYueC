# -*- coding: utf-8 -*-
"""双端服务端协议面对照回归锁（C HsfServer vs server_py）。

[为什么有这个测试] 网页版（webui）与桌面客户端两端通吃：同一个请求发到
C 服务端与 Python 服务端，应答必须「同命令集、同推送主题、同配置键、
同快照字段名、同错误口径」——单端缺键/多键/类型不同/文案不同都要红。
口径依据：webui_py/PARITY_PLAN.md §4（2026-11-13 逐条核实后执行）。

覆盖：
    0. 协议常量对照（静态）：19 命令号、10 主题号、落盘缺省、日志类别——
       C 头 XhsfLinkProto.h 的宏逐值 vs server_py proto.py。
    1. 命令覆盖面 + 应答形状对照（19 条命令、安全载荷、逐键键集/值类型）。
    2. 错误口径对照（未知命令/权限拒绝/非法 op/非法模式/越界索引/
       未知配置节——err 文案逐字相等）。
    3. 推送主题对照（9 遥测主题各收一帧，键集对照；阀告警合帧单独归形）。
    4. 配置面对照（GET 全键集 + SET 成功应答 = {ok, err:文案, 节回显} 且
       回显与 GET 一致；storage SET 同值无副作用）。

进程自管：先探活 6100/6200（C sim / PY sim），在跑就复用、不在跑才自己
起（收尾只清理自己起的）。可直接 `python tests/test_server_alignment.py`
运行，退出码 0=全绿。

[条件键口径——这不是放水] 两端都有「数据有效才写键」的既定口径（变频器
vm 掩码、浊度逐台 online、温湿度 valid、健康 d{i}_text、数字量通道名），
应答键集天然随装配与数据新鲜度抖动。对照分两层：
    · FIXED 键（两端源码里无条件写入的键）缺一键即红；
    · 出现白名单之外的键即红（未知键 = 单端多发明键）。
白名单本身就是两端 handler 的键表（写死在本文件，改动会被人眼审到）。
"""

import argparse
import os
import re
import socket
import struct
import subprocess
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
import time

HERE = os.path.dirname(os.path.abspath(__file__))
SERVER_PY_ROOT = os.path.dirname(HERE)                     # HighSpeedFilter/server_py
HSF_ROOT = os.path.dirname(SERVER_PY_ROOT)                 # HighSpeedFilter
REPO_ROOT = os.path.dirname(HSF_ROOT)                      # 仓库根
C_HEADER = os.path.join(HSF_ROOT, "common", "XhsfLinkProto.h")
C_SERVER_EXE = os.path.join(REPO_ROOT, "bin", "HsfServer.exe")

sys.path.insert(0, SERVER_PY_ROOT)
sys.path.insert(0, HERE)

import hsfsrv.proto as proto            # noqa: E402
import hsfsrv.bson as bson              # noqa: E402
from test_vfd_ops import Client         # noqa: E402

PASS = 0
FAIL = 0
FAILED = []
NOTES = []


def check(name, ok, detail=""):
    global PASS, FAIL
    if ok:
        PASS += 1
        print("  [PASS] %s %s" % (name, detail))
    else:
        FAIL += 1
        FAILED.append(name)
        print("  [FAIL] %s %s" % (name, detail))


def note(text):
    NOTES.append(text)
    print("  [NOTE] %s" % text)


# ============================================================================
# 0. C 头静态解析（宏 → 值）
# ============================================================================

def parse_header_macros(path):
    """抓 #define NAME 0xVALUE / 整数 / "字符串" 三种形态。"""
    macros = {}
    pat_hex = re.compile(r'^#define\s+(HSF_\w+)\s+(0x[0-9A-Fa-f]+)u?', re.M)
    pat_int = re.compile(r'^#define\s+(HSF_\w+)\s+([0-9]+)\s*$', re.M)
    pat_str = re.compile(r'^#define\s+(HSF_\w+)\s+"([^"]*)"', re.M)
    text = open(path, encoding="utf-8", errors="replace").read()
    for m in pat_hex.finditer(text):
        macros[m.group(1)] = int(m.group(2), 16)
    for m in pat_int.finditer(text):
        macros.setdefault(m.group(1), int(m.group(1) and m.group(2)))
    for m in pat_str.finditer(text):
        macros[m.group(1)] = m.group(2)
    return macros


CMD_NAMES = [
    "VFD_CONTROL", "VFD_SNAPSHOT", "AI_SNAPSHOT", "TEMPHUM_SNAPSHOT",
    "FLOWMETER_SNAPSHOT", "RUN_MODE", "DEVICE_HEALTH", "DIGITAL_SNAPSHOT",
    "DIGITAL_CONTROL", "VALVE_SNAPSHOT", "VALVE_CONTROL",
    "TURBIDITY_SNAPSHOT", "PUMP_SNAPSHOT", "PUMP_CONTROL", "AUTO_CONTROL",
    "CONFIG_GET", "CONFIG_SET", "HISTORY_PAGE", "LOG_PAGE",
    # [2026-11-18 补] 变频器参数读写 0x011B：两端都占了这个号（C 头
    # HSF_CMD_VFD_PARAMS + Python CMD_VFD_PARAMS），但这张表当时**没列**
    # ⇒ 两端各写各的号也不会红。列进来＝把号锁住。
    "VFD_PARAMS",
]
TOPIC_NAMES = [
    "VFD_STATUS", "AI_STATUS", "TEMPHUM_STATUS", "FLOWMETER_STATUS",
    "DEVICE_HEALTH", "DIGITAL_STATUS", "VALVE_STATUS", "TURBIDITY_STATUS",
    "PUMP_STATUS", "AUTO_ALARM",
]
LOG_CATS = ["CONN", "CTRL", "ALARM", "AUTO", "AUTH"]


def check_constants(hsf):
    print("\n=== §0 协议常量对照（C 头宏 vs server_py proto） ===")
    for name in CMD_NAMES:
        c_val = hsf.get("HSF_CMD_" + name)
        p_val = getattr(proto, "CMD_" + name, None)
        check("CMD_%s" % name, c_val is not None and p_val == c_val,
              "C=%s PY=%s" % (hex(c_val or 0), hex(p_val or 0)))
    for name in TOPIC_NAMES:
        c_val = hsf.get("HSF_TOPIC_" + name)
        p_val = getattr(proto, "TOPIC_" + name, None)
        check("TOPIC_%s" % name, c_val is not None and p_val == c_val,
              "C=%s PY=%s" % (hex(c_val or 0), hex(p_val or 0)))
    for c_key, p_key in (("HSF_STORAGE_SAMPLE_MS_DEF", "STORAGE_SAMPLE_MS_DEF"),
                         ("HSF_STORAGE_MAX_MB_DEF", "STORAGE_MAX_MB_DEF"),
                         ("HSF_HISTORY_PAGE_MAX", "HISTORY_PAGE_MAX"),
                         ("HSF_HISTORY_PAGE_DEFAULT", "HISTORY_PAGE_DEFAULT"),
                         ("HSF_LOG_PAGE_MAX", "HISTORY_PAGE_MAX"),
                         ("HSF_LOG_PAGE_DEFAULT", "HISTORY_PAGE_DEFAULT")):
        c_val = hsf.get(c_key)
        p_val = getattr(proto, p_key, None)
        check(c_key, c_val is not None and p_val == c_val,
              "C=%s PY=%s" % (c_val, p_val))
    for suffix in LOG_CATS:
        # [2026-11-13 改实证] C 头没有 HSF_LOG_CAT_* 宏（类别字面在
        # hsfAppEventLog 调用点），静态解析拿不到——改在 §1 里按类别过滤
        # 0x0119 实证对照（见 check_log_categories）。
        p_val = getattr(proto, "LOG_CAT_" + suffix, None)
        check("LOG_CAT_%s 在 PY proto 有定义" % suffix, p_val is not None,
              "PY=%r" % (p_val,))


# ============================================================================
# 应答形状对照
# ============================================================================

def type_class(v):
    """值 → 类型类（bool/num/str/arr/doc/null/bin）。bool 必须先于 num。"""
    if isinstance(v, bool):
        return "bool"
    if isinstance(v, (int, float)):
        return "num"
    if isinstance(v, str):
        return "str"
    if v is None:
        return "null"
    if isinstance(v, (bytes, bytearray)):
        return "bin"
    if isinstance(v, list):
        return "arr"
    if isinstance(v, dict):
        return "doc"
    return type(v).__name__


def stems(keys):
    """d3_text / din12 / v0_ntu → d*_text / din* / v*_ntu（下标归并）。

    [整段数字归并] din12 必须 → din*（旧正则只吞了末位，"din1*" 会让两端
    键形假性不一致——din10~15 与 din1 撞名）。
    """
    out = set()
    for k in keys:
        out.add(re.sub(r"\d+", "*", k))
    return out


#: **类型随数据变**的键（比对时跳过"类型一致"这一项）。
#:
#: [为什么] `lastAlarm`（主页「最近报警」卡，2026-11-19）的值是
#: **无报警时 null / 有报警时文档** ⇒ 两端各连各的库，库里有没有报警行
#: 不必相同 ⇒ 一端 null、另一端文档都是**合法形态**（不是形状差）。
#: ⇒ 这类键只比"键名在不在"，不比"值的类型"。
TYPE_UNSTABLE_KEYS = {"lastAlarm"}


def compare_doc(name, c_doc, p_doc, fixed, conditional=(), index_prefixes=()):
    """键集 + 类型对照。

    fixed        两端都必须有、且类型一致的键（可含通配 d*_text 形态）。
    conditional  允许出现（数据有效才写）的键；两端都出现时类型必须一致。
                 「键集恒等」判定会把条件键剔除后再比——数据新鲜度抖动
                 （如变频器某字段本拍掩码无效）不算形状差。
    index_prefixes 需要按「各自端自己的条目数」展开的前缀（health 的 d、
                 digital 的 din/don）：下标归并成 stems 再比对。
    """
    if not isinstance(c_doc, dict) or not isinstance(p_doc, dict):
        check(name, False, "应答不是对象：C=%r PY=%r"
              % (type_class(c_doc), type_class(p_doc)))
        return
    if index_prefixes:
        ck = stems(set(c_doc))
        pk = stems(set(p_doc))
    else:
        ck, pk = set(c_doc), set(p_doc)
    fixed_n = {k for k in fixed if "*" not in k}
    fixed_star = {k for k in fixed if "*" in k}
    cond_n = set(conditional)

    def star_known(key):
        """星形白名单：'*' 可匹配下标数字或归并后的字面 '*'。"""
        for pattern in fixed_star:
            if re.fullmatch(pattern.replace("*", r"(\d+|\*)"), key):
                return True
        for pattern in cond_n:
            if "*" in pattern and re.fullmatch(
                    pattern.replace("*", r"(\d+|\*)"), key):
                return True
        return False

    def is_cond(key):
        if key in cond_n:
            return True
        return any(re.fullmatch(p.replace("*", r"\d+"), key)
                   for p in cond_n if "*" in p)

    missing_c = fixed_n - ck
    missing_p = fixed_n - pk
    check(name + " 固定键两端齐", not missing_c and not missing_p,
          "C缺%s PY缺%s" % (sorted(missing_c), sorted(missing_p)))
    unknown_c = {k for k in ck
                 if k not in fixed_n and not is_cond(k) and not star_known(k)}
    unknown_p = {k for k in pk
                 if k not in fixed_n and not is_cond(k) and not star_known(k)}
    check(name + " 无未知键", not unknown_c and not unknown_p,
          "C多%s PY多%s" % (sorted(unknown_c), sorted(unknown_p)))
    # 键集恒等（剔条件键）：单端多发明键 / 单端漏键的主判据。
    c_core = {k for k in ck if not is_cond(k)}
    p_core = {k for k in pk if not is_cond(k)}
    check(name + " 键集一致（剔条件键）", c_core == p_core,
          "C独有%s PY独有%s" % (sorted(c_core - p_core)[:8],
                                sorted(p_core - c_core)[:8]))
    # 共有键的类型一致（下标文档只比两端同字面键——台数差不掺和）
    # [类型随数据变的键不比] 见 TYPE_UNSTABLE_KEYS：两端各连各的库，
    # 库里有没有报警行不必相同 ⇒ null 与文档都是合法形态。
    bad_types = []
    common = (set(c_doc) & set(p_doc)) - set(index_prefixes)
    for k in sorted(common):
        if k in TYPE_UNSTABLE_KEYS:
            continue
        tc, tp = type_class(c_doc[k]), type_class(p_doc[k])
        if tc != tp:
            bad_types.append("%s:%s/%s" % (k, tc, tp))
    check(name + " 类型一致", not bad_types, "; ".join(bad_types[:6]))


# ============================================================================
# 进程管理
# ============================================================================

def port_open(port):
    try:
        with socket.create_connection(("127.0.0.1", port), timeout=1.0):
            return True
    except OSError:
        return False


class Sims(object):
    """探活复用 / 自起自清。"""

    def __init__(self, c_port, py_port):
        self.c_port = c_port
        self.py_port = py_port
        self.own = []           # 自己起的 Popen，收尾杀掉

    def ensure(self):
        if not port_open(self.c_port):
            print("[sims] C sim %d 不在跑，自己起 bin/HsfServer.exe --sim"
                  % self.c_port)
            log = open(os.path.join(os.environ.get("TEMP", "."),
                                    "hsf_align_c_%d.log" % os.getpid()), "ab")
            proc = subprocess.Popen(
                [C_SERVER_EXE, "--sim"], cwd=os.path.dirname(C_SERVER_EXE),
                stdout=log, stderr=subprocess.STDOUT,
                creationflags=subprocess.CREATE_NEW_PROCESS_GROUP)
            self.own.append(proc)
        if not port_open(self.py_port):
            print("[sims] PY sim %d 不在跑，自己起 python -m hsfsrv --sim"
                  % self.py_port)
            log = open(os.path.join(os.environ.get("TEMP", "."),
                                    "hsf_align_py_%d.log" % os.getpid()), "ab")
            proc = subprocess.Popen(
                [sys.executable, "-m", "hsfsrv", "--sim",
                 "--port", str(self.py_port)],
                cwd=SERVER_PY_ROOT, stdout=log, stderr=subprocess.STDOUT,
                creationflags=subprocess.CREATE_NEW_PROCESS_GROUP)
            self.own.append(proc)
        for port in (self.c_port, self.py_port):
            deadline = time.time() + 40
            while time.time() < deadline and not port_open(port):
                time.sleep(0.5)
            check("sim 端口 %d 就绪" % port, port_open(port))

    def cleanup(self):
        for proc in self.own:
            if proc.poll() is None:
                proc.terminate()
                try:
                    proc.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    proc.kill()


# ============================================================================
# 登录
# ============================================================================

ACCOUNT_PASSWORD = {"root": "root", "oper": "oper", "admin": "admin"}


def login_with(host, port, accounts, retries=12, gap=1.5):
    """按账户优先级登录；返回 (Client, account) 或 (None, last_err)。

    [retries 为什么要多] 共享 sim 身边可能有别的客户端在循环重连（HsfClient
    断链自动重试），会话上限瞬时应答 "authentication rejected"——按瞬时失败退避重试即可，
    与 AGENTS「连续两次连接间隔 ~8s」同一口径。
    """
    last = None
    for account in accounts:
        for attempt in range(retries):
            client = Client(host, port, timeout=8.0)
            client.hello()
            ok, info = client.login(account, ACCOUNT_PASSWORD[account])
            if ok:
                return client, account
            last = "%s: %s" % (account, info)
            client.close()
            time.sleep(gap)
    return None, last


class KeepAlive(object):
    """裸 Client 没有心跳 ⇒ 服务端 6s 静默判链（两端同口径）。

    对照用例里 topic 采集/逐命令往返可能连续 >6s 无上行帧，低档会话更是
    长时间空闲——必须周期发 SESSION_HEARTBEAT 保活（真实客户端 1s 心跳）。
    """

    def __init__(self, clients):
        self.clients = [c for c in clients if c is not None]
        self.last_ms = time.time()
        self.stop = False

    def tick(self):
        if self.stop:
            return
        now = time.time()
        if now - self.last_ms < 2.0:
            return
        self.last_ms = now
        for client in self.clients:
            try:
                client._send(proto.MSGCLASS_SESSION, proto.SESSION_HEARTBEAT,
                             proto.DATATYPE_RAW, b"")
            except OSError:
                pass


# ============================================================================
# 命令形状对照表（安全载荷——不改任何共享 sim 的可见状态）
# ============================================================================

# (节名, 命令名, 载荷构造或 None, 固定键, 条件键, 是否按条目归并)
def build_shape_cases():
    cases = [
        ("0x0102 VFD 快照", proto.CMD_VFD_SNAPSHOT, None,
         ["vm", "side", "ratedRpm", "ratedHz"],
         ["ts", "runState", "running", "faulted", "reverse", "remote",
          "jogActive", "setFreqHz", "outputFreqHz", "outputVoltageV",
          "outputCurrentA", "busVoltageV", "outputPowerKw", "torquePercent",
          "speedRpm", "faultCode",
          # [motorTempC 2026-11-20] 搅拌泵卡的电机温度（模拟量温度传感器）
          # PY 先行，C 暂不用 ⇒ 条件键。
          "motorTempC"], ()),
        ("0x0103 AI 快照", proto.CMD_AI_SNAPSHOT, None,
         ["full", "n", "v"],
         ["r*", "u*", "an*", "e*", "eu*", "en*", "ew*"], ()),
        ("0x0104 温湿度 side0", proto.CMD_TEMPHUM_SNAPSHOT, {"side": 0},
         ["side", "tempValid", "humValid", "staleMs"], ["temp", "hum"], ()),
        ("0x0104 温湿度 side1", proto.CMD_TEMPHUM_SNAPSHOT, {"side": 1},
         ["side", "tempValid", "humValid", "staleMs"], ["temp", "hum"], ()),
        ("0x0105 模式查询", proto.CMD_RUN_MODE, None,
         ["ok", "mode", "auto", "autoRunning", "autoMode", "autoPhase",
          "autoFault", "autoRunMs", "autoStageElapsedMs",
          "autoStageTotalMs"],
         # [lastAlarm 放条件键表 2026-11-19] 主页「最近报警」卡的数据源：
         #   PY 端 0x0105 已带（值 = 文档/null），**C 端源码也已补**
         #   （common/XhsfLinkProto.h 的 HSF_KEY_LAST_ALARM +
         #   server/HsfServerApp.c 的 hsfAppMakeModeEnvelope），
         #   但**本机没有 MSVC ⇒ bin/HsfServer.exe 仍是改动前的旧版**
         #   ⇒ 眼下只有 PY 侧有这个键 ⇒ 放条件键表（允许单端有）。
         #   ⇒ **C 端重编 HsfServer.exe 之后**：两端都有该键，此时可把
         #      "lastAlarm" 从条件键表移回固定键表（并从
         #      TYPE_UNSTABLE_KEYS 里评估是否保留）。
         # [autoPaused/autoPauseMode 放条件键表 2026-11-19] 「停止＝暂停」
         #   新语义（PY 端先落）：autoPaused（bool，暂停标志）+
         #   autoPauseMode（str，暂停时记住的模式/阶段）。**C 端尚未实现**
         #   ⇒ 眼下只有 PY 侧有 ⇒ 放条件键表（允许单端有）。
         #   ⇒ C 端补齐（common/XhsfLinkProto.h + HsfServerApp.c）后可移回
         #      固定键表。
         ("lastAlarm", "autoPaused", "autoPauseMode"), ()),
        ("0x0106 流量计快照", proto.CMD_FLOWMETER_SNAPSHOT, None,
         ["side", "valid", "staleMs"],
         ["flow", "speed", "forwardTotal", "reverseTotal", "flowUnit",
          "totalUnit", "alarms"], ()),
        ("0x0108 数字量快照", proto.CMD_DIGITAL_SNAPSHOT, {"dev": 0},
         ["full", "dev", "name", "di", "do", "count", "valid", "staleMs"],
         ["din*", "don*"], ("din", "don")),
        ("0x0110 阀快照", proto.CMD_VALVE_SNAPSHOT, None,
         ["count"],
         ["v*_name", "v*_state", "v*_openCmd", "v*_openFb", "v*_closeFb",
          "v*_ready", "v*_valid", "v*_cmdResult"], ()),
        ("0x0112 浊度快照", proto.CMD_TURBIDITY_SNAPSHOT, None,
         ["count", "side", "staleMs", "v0_name", "v0_online",
          "v1_name", "v1_online"],
         ["v0_ntu", "v0_raw", "v0_temp", "v1_ntu", "v1_raw", "v1_temp"], ()),
        ("0x0113 泵快照", proto.CMD_PUMP_SNAPSHOT, None,
         ["count", "staleMs", "v0_name", "v0_on", "v0_ready", "v0_valid"],
         ["v0_text"], ()),
        ("0x0118 历史分页", proto.CMD_HISTORY_PAGE,
         {"offset": 0, "limit": 5},
         ["ok", "total", "offset", "limit", "rows", "cols", "dbRows",
          "latestTs", "earliestTs", "windowStartTs", "windowEndTs"], (), ()),
        ("0x0119 日志分页", proto.CMD_LOG_PAGE, {"offset": 0, "limit": 5},
         ["ok", "total", "count", "offset", "limit", "rows"], (), ()),
        ("0x0116 配置读", proto.CMD_CONFIG_GET, None,
         ["ok", "collect", "storage", "login", "auto", "protect",
          "storageDbPath", "storageStats"],
         # [camera 条件键 2026-11-20] camera 节是 PY 先行（C 服务端暂不
         # 实现相机取流）⇒ PY 顶层多一个 camera 键属已知差异，走条件键
         # （剔出键集恒等、不判未知），对齐 drain 同款口径。
         ("camera",), ()),
    ]
    return cases


def nested_compare(name, c_doc, p_doc):
    """0x0116 应答的嵌套节键集对照。"""
    for section, fixed, cond in (
            ("collect",
             ["vfdEnabled", "temphumEnabled", "turbidityEnabled",
              "flowmeterEnabled", "turbidity1Enabled", "turbidity2Enabled",
              "analogEnabled", "digitalEnabled", "vfdPollMs",
              "temphumPollMs", "turbidityPollMs", "turbidity1PollMs",
              "turbidity2PollMs", "flowmeterPollMs", "analogPollMs",
              "digitalPollMs"],
             # [PY-only 条件键 2026-11-20] 压差计/温度传感器通道与量程
             # （下限~上限 + 4mA 以下有效开关）PY 先行，C 暂不用 ⇒ 条件键
             # （剔键集恒等、不判未知），与 drain 同款。
             ("pressChannel", "pressLowMpa", "pressHighMpa",
              "pressBelow4Valid", "tempChannel", "tempLowC", "tempHighC",
              "tempBelow4Valid",
              # [analogChannels 2026-11-20] GET 便利键（模块路数，客户端
              # 造通道下拉用）；PY-only，C 暂不用。
              "analogChannels")),
            ("storage",
             ["enabled", "path", "sampleMs", "maxMb", "fullPolicy",
              "keepFiles", "historyTrigger"], ()),
            ("login",
             ["loginAllowAnonymous", "loginAllowConcurrent",
              "loginMaxSessions"], ()),
            # [运行保护 2026-11-15] 进水泵空管超时 + 是否停设备；
            # 压差过大保护：启用 / 上限(MPa) / 触发后是否停设备（两端同键）。
            ("protect",
             ["inletNoFlowTimeoutMs", "inletNoFlowStop",
              "pressHighEnabled", "pressHighLimitMpa", "pressHighStop"],
             # [PY-only 条件键 2026-11-20] 电机超温保护三键 PY 先行。
             ("motorOverTempEnabled", "motorOverTempLimitC",
              "motorOverTempStop")),
            ("storageStats",
             ["usedBytes", "usedMb", "contentBytes", "contentMb", "maxMb",
              "maxKb", "policy", "keepFiles", "paused", "pauseReason",
              "droppedRows", "rows", "segments", "enabled", "dbPath"], ())):
        c_sub, p_sub = c_doc.get(section), p_doc.get(section)
        ok = isinstance(c_sub, dict) and isinstance(p_sub, dict)
        # [PY-only 白名单 2026-11-20] 键集比对放宽成 auto.modes 同款：
        # ① 共有子集逐字段全量比对（力度不减）；② C 多出来的键 ⇒ 红；
        # ③ PY 多出来的键 ⇒ 只允许 PY_ONLY 白名单里的（C 端补齐后本条
        # 自动变严）。collect 的通道/量程键与 protect 的电机超温键是
        # PY 端先行（C 端暂不用），同 drain 的口径。
        py_only = {
            "collect": {"pressChannel", "pressLowMpa", "pressHighMpa",
                        "pressBelow4Valid", "tempChannel", "tempLowC",
                        "tempHighC", "tempBelow4Valid", "analogChannels"},
            "protect": {"motorOverTempEnabled", "motorOverTempLimitC",
                        "motorOverTempStop"},
        }.get(section, set())
        _c_only = (set(c_sub or {}) - set(p_sub or {})) if ok else set()
        _py_only_k = (set(p_sub or {}) - set(c_sub or {})) if ok else set()
        check(name + " §" + section + "（键集：C 多红 / PY 多限白名单）",
              ok and not _c_only and _py_only_k <= py_only,
              "" if ok else "子节缺失/非对象")
        if ok:
            compare_doc(name + " §" + section, c_sub, p_sub, fixed, cond)
    # auto 节：modes × 3（+放水）+ cycle × 7（PY 另有 cycleModes/stageMinutes）
    c_auto, p_auto = c_doc.get("auto"), p_doc.get("auto")
    if isinstance(c_auto, dict) and isinstance(p_auto, dict):
        c_modes, p_modes = c_auto.get("modes"), p_auto.get("modes")
        # [2026-11-18 放水模式] 模式集合不再要求两端**完全相等**：PY 有第
        # 四个**可配置**模式 drain（能配不能跑），C 端还没实现 ⇒ 键集比对
        # 改为：① 共有子集逐字段全量比对（下面那段循环，力度不减）；
        # ② C 多出来的键 ⇒ 判红（PY 不该缺 C 已有的）；③ PY 多出来的键
        # ⇒ 只允许 drain（白名单）。C 端补齐后本条自动变严。
        _py_only = set(p_modes or {}) - set(c_modes or {})
        _c_only = set(c_modes or {}) - set(p_modes or {})
        check(name + " §auto.modes（共有子集一致 + PY 多出只允许 drain）",
              isinstance(c_modes, dict) and isinstance(p_modes, dict)
              and not _c_only and _py_only <= {"drain"},
              "C=%s PY=%s" % (sorted(c_modes or {}), sorted(p_modes or {})))
        if isinstance(c_modes, dict) and isinstance(p_modes, dict):
            for mid in sorted(set(c_modes) & set(p_modes)):
                compare_doc(name + " §auto.modes.%s" % mid,
                            c_modes[mid], p_modes[mid],
                            ["devices", "stirRpm", "stirReverse",
                             "resetValvesOnStop", "resetValvesOnEstop",
                             # [换相策略 2026-11-14] 两端同口径的新键（缺省
                             # ＝ stopRestart）。
                             "stirSwitchPolicy", "inletSwitchPolicy"], ())
        # auto.cycle：[2026-11-18 配置化] PY 多了三个键（参与循环的模式列表
        # + 各阶段时长视图 + **各阶段切换条件**）——C 端还没实现 ⇒ 与
        # auto.modes 同款处理：
        #   ① 共有键逐字段全量比对（力度不减）；
        #   ② C 多出来的键 ⇒ 判红（PY 不该缺 C 已有的）；
        #   ③ PY 多出来的键 ⇒ 只允许 {cycleModes, stageMinutes, stageCfg}
        #      （白名单）。C 端补齐后本条自动变严。
        c_cycle, p_cycle = c_auto.get("cycle"), p_auto.get("cycle")
        _py_only_c = set(p_cycle or {}) - set(c_cycle or {})
        _c_only_c = set(c_cycle or {}) - set(p_cycle or {})
        check(name + " §auto.cycle（共有子集一致 + PY 多出只允许 "
                    "cycleModes/stageMinutes/stageCfg）",
              isinstance(c_cycle, dict) and isinstance(p_cycle, dict)
              and not _c_only_c
              and _py_only_c <= {"cycleModes", "stageMinutes", "stageCfg"},
              "C多%s PY多%s" % (sorted(_c_only_c), sorted(_py_only_c)))
        compare_doc(name + " §auto.cycle", c_cycle, p_cycle,
                    ["enabled", "pressThreshold", "pressCompare",
                     "timerFallback", "timerMinutes", "backwashMinutes",
                     "rinseMinutes"],
                    # [条件键] PY 独有（C 端尚未实现）：允许出现、不参与
                    # 键集恒等判定；两端都出现时类型必须一致。
                    ("cycleModes", "stageMinutes", "stageCfg"))


# ============================================================================
# 主题推送
# ============================================================================

TOPICS = [
    ("VFD", proto.TOPIC_VFD_STATUS),
    ("AI", proto.TOPIC_AI_STATUS),
    ("TEMPHUM", proto.TOPIC_TEMPHUM_STATUS),
    ("FLOWMETER", proto.TOPIC_FLOWMETER_STATUS),
    ("HEALTH", proto.TOPIC_DEVICE_HEALTH),
    ("DIGITAL", proto.TOPIC_DIGITAL_STATUS),
    ("VALVE", proto.TOPIC_VALVE_STATUS),
    ("TURBIDITY", proto.TOPIC_TURBIDITY_STATUS),
    ("PUMP", proto.TOPIC_PUMP_STATUS),
    ("AUTO_ALARM", proto.TOPIC_AUTO_ALARM),
]

TOPIC_SHAPES = {
    "VFD": (["vm", "side"],
            ["ts", "runState", "running", "faulted", "reverse", "remote",
             "jogActive", "setFreqHz", "outputFreqHz", "outputVoltageV",
             "outputCurrentA", "busVoltageV", "outputPowerKw",
             "torquePercent", "speedRpm", "faultCode",
             # [motorTempC 2026-11-20] 搅拌泵卡电机温度（PY 先行）。
             "motorTempC"], ()),
    "AI": (["full", "n", "v"],
           ["r*", "u*", "an*", "e*", "eu*", "en*", "ew*"], ()),
    "TEMPHUM": (["side", "tempValid", "humValid", "staleMs"],
                ["temp", "hum"], ()),
    "FLOWMETER": (["side", "valid", "staleMs"],
                  ["flow", "speed", "forwardTotal", "reverseTotal",
                   "flowUnit", "totalUnit", "alarms"], ()),
    "HEALTH": (["devCount", "seq", "ts", "d*_addr", "d*_error", "d*_health",
                "d*_kind", "d*_name", "d*_online", "d*_runState", "d*_side"],
               ["d*_text"], ("d",)),
    "DIGITAL": (["full", "dev", "name", "di", "do", "count", "valid",
                 "staleMs"], ["din*", "don*"], ("din", "don")),
    "VALVE": (["count"],
              ["v*_name", "v*_state", "v*_openCmd", "v*_openFb",
               "v*_closeFb", "v*_ready", "v*_valid", "v*_cmdResult",
               "v*_text"], ()),
    "TURBIDITY": (["count", "side", "staleMs", "v0_name", "v0_online",
                   "v1_name", "v1_online"],
                  ["v0_ntu", "v0_raw", "v0_temp", "v1_ntu", "v1_raw",
                   "v1_temp"], ()),
    "PUMP": (["count", "staleMs", "v0_name", "v0_on", "v0_ready",
              "v0_valid"], ["v0_text"], ()),
    "AUTO_ALARM": (["kind", "atMs", "text"], [], ()),
}


def subscribe(client, topic_id):
    client._send(proto.MSGCLASS_SUBSCRIBE, topic_id & 0xFFFF,
                 proto.DATATYPE_RAW, struct.pack("<I", topic_id))


def collect_topic_frames(client, seconds, keepalive=None):
    deadline = time.time() + seconds
    while time.time() < deadline:
        client.pump(0.2)
        if keepalive:
            keepalive.tick()
    got = {}
    for topic, doc, _t in client.publishes:
        got.setdefault(topic, []).append(doc)
    client.publishes = []
    return got


def check_topics(client_c, client_py, keepalive=None, label_suffix=""):
    print("\n=== §3 推送主题对照%s ===" % label_suffix)
    for client in (client_c, client_py):
        for _name, tid in TOPICS:
            subscribe(client, tid)
    frames_c = collect_topic_frames(client_c, 6.0, keepalive)
    frames_py = collect_topic_frames(client_py, 2.0, keepalive)
    for name, tid in TOPICS:
        if name == "AUTO_ALARM":
            # 事件驱动主题：空闲 sim 无告警属正常（形状锁在
            # test_auto_alarm_publish.py / C 回归 §S7）。这里只锁
            # 「订阅不报错 + 两端协议号一致」（§0 已对照常量）。
            note("AUTO_ALARM 是事件驱动主题，空闲 sim 不推帧（属正常）")
            continue
        c_frames = [d for d in frames_c.get(tid, []) if "ac" not in d]
        p_frames = [d for d in frames_py.get(tid, []) if "ac" not in d]
        check("主题 %s 两端都推" % name,
              bool(c_frames) and bool(p_frames),
              "C=%d帧 PY=%d帧" % (len(frames_c.get(tid, [])),
                                  len(frames_py.get(tid, []))))
        if c_frames and p_frames:
            fixed, cond, indexed = TOPIC_SHAPES[name]
            if name == "HEALTH":
                compare_doc("主题 %s 形状" % name, c_frames[0], p_frames[0],
                            fixed, cond, indexed)
            else:
                compare_doc("主题 %s 形状" % name, c_frames[0], p_frames[0],
                            fixed, cond, indexed)
        # 阀告警合帧（ac 键）：只在两端都出现时对照形状
        c_alarm = [d for d in frames_c.get(tid, []) if "ac" in d]
        p_alarm = [d for d in frames_py.get(tid, []) if "ac" in d]
        if name == "VALVE":
            if c_alarm and p_alarm:
                compare_doc("阀告警合帧形状", c_alarm[0], p_alarm[0],
                            ["ac"], ["a*_vi", "a*_vn", "a*_k", "a*_t",
                                     "a*_at", "a*_to"], ())
            else:
                note("阀告警合帧 %s" % ("两端都无（空闲无告警，属正常）"
                                        if not c_alarm and not p_alarm
                                        else "C=%d PY=%d（事件驱动，本拍不对照）"
                                        % (len(c_alarm), len(p_alarm))))


# ============================================================================
# 主流程
# ============================================================================

def check_log_categories(c_root, p_root, keepalive=None):
    """日志类别字面实证：按类别过滤 0x0119，两端行的 category 必须同字面。

    [为什么实证] C 头没有类别宏（字面在 hsfAppEventLog 调用点），静态
    解析拿不到；conn/auth 在刚登录的 sim 上必有行，其余类别没行只记备注。
    """
    print("\n=== §1b 日志类别字面实证 ===")
    for cat in (proto.LOG_CAT_CONN, proto.LOG_CAT_AUTH, proto.LOG_CAT_CTRL,
                proto.LOG_CAT_ALARM, proto.LOG_CAT_AUTO):
        c_doc = request_doc(c_root, proto.CMD_LOG_PAGE,
                            {"offset": 0, "limit": 50, "category": cat},
                            keepalive=keepalive)
        p_doc = request_doc(p_root, proto.CMD_LOG_PAGE,
                            {"offset": 0, "limit": 50, "category": cat},
                            keepalive=keepalive)
        if not isinstance(c_doc, dict) or not isinstance(p_doc, dict):
            check("日志类别 %s" % cat, False, "单端无应答")
            continue
        c_rows = c_doc.get("rows") or []
        p_rows = p_doc.get("rows") or []
        c_pure = all(r.get("category") == cat for r in c_rows)
        p_pure = all(r.get("category") == cat for r in p_rows)
        check("日志类别 %s 过滤两端同字面" % cat,
              c_doc.get("ok") is True and p_doc.get("ok") is True
              and c_pure and p_pure,
              "C=%d行%s PY=%d行%s"
              % (len(c_rows), "" if c_pure else "(混入别的类别)",
                 len(p_rows), "" if p_pure else "(混入别的类别)"))
        if not c_rows and not p_rows:
            note("类别 %s 两端该库都暂无行（过滤字面仍一致）" % cat)


def request_doc(client, code, payload=None, wait=5.0, keepalive=None):
    if keepalive:
        keepalive.tick()
    data = bson.encode(payload) if isinstance(payload, dict) else b""
    reply = client.request(code, data, wait=wait)
    if reply is None:
        return None
    return reply[1]


def main():
    global PASS, FAIL
    ap = argparse.ArgumentParser()
    ap.add_argument("--c-port", type=int, default=6100)
    ap.add_argument("--py-port", type=int, default=6200)
    args = ap.parse_args()

    print("=== HSF 双端服务端协议面对照 ===")
    hsf = parse_header_macros(C_HEADER)
    check("C 头可解析（宏数>100）", len(hsf) > 100, "%d 宏" % len(hsf))
    check_constants(hsf)

    sims = Sims(args.c_port, args.py_port)
    try:
        sims.ensure()
        # ---- 登录：root 同档优先 ----
        c_root, c_acct = login_with("127.0.0.1", args.c_port, ["root"])
        p_root, p_acct = login_with("127.0.0.1", args.py_port, ["root"])
        check("两端 root 登录", c_root is not None and p_root is not None,
              "C=%s PY=%s" % (c_acct, p_acct))
        if c_root is None:
            c_root, c_acct = login_with("127.0.0.1", args.c_port, ["oper"])
        if p_root is None:
            p_root, p_acct = login_with("127.0.0.1", args.py_port, ["oper"])
        check("两端可用账户一致（同一权限档）",
              c_root is not None and p_root is not None and c_acct == p_acct,
              "C=%s PY=%s" % (c_acct, p_acct))
        if c_root is None or p_root is None:
            print("[FATAL] 至少一端完全登不上——协议面对照无法进行")
            return 2
        admin = c_acct if c_acct != "root" else "root"

        # 低档会话（权限拒绝文案对照用）：admin/oper 至少一档两端都在
        low_order = [a for a in ("admin", "oper") if a != c_acct]
        c_low, c_low_acct = login_with("127.0.0.1", args.c_port,
                                       low_order, retries=4)
        p_low, p_low_acct = login_with("127.0.0.1", args.py_port,
                                       low_order, retries=4)
        check("两端低档账户登录", c_low is not None and p_low is not None
              and c_low_acct == p_low_acct,
              "C=%s PY=%s" % (c_low_acct, p_low_acct))

        # ---- §1 命令覆盖面 + 形状 ----
        keepalive = KeepAlive([c_root, p_root, c_low, p_low])
        print("\n=== §1 命令覆盖面 + 应答形状对照（账户=%s） ===" % c_acct)
        for (label, code, payload, fixed, cond, indexed) in build_shape_cases():
            c_doc = request_doc(c_root, code, payload, keepalive=keepalive)
            p_doc = request_doc(p_root, code, payload, keepalive=keepalive)
            check(label + " 两端都应答",
                  c_doc is not None and p_doc is not None,
                  "C=%s PY=%s" % ("无应答" if c_doc is None else "",
                                  "无应答" if p_doc is None else ""))
            if c_doc is None or p_doc is None:
                continue
            if code == proto.CMD_CONFIG_GET:
                compare_doc(label, c_doc, p_doc,
                            fixed, cond, indexed)
                nested_compare(label, c_doc, p_doc)
            elif code == proto.CMD_HISTORY_PAGE:
                compare_doc(label, c_doc, p_doc, fixed, cond, indexed)
                # 线上列：两端 cols 逐列一致（19 列 water_temp 末尾）
                check(label + " cols 逐列一致",
                      c_doc.get("cols") == p_doc.get("cols"),
                      "C=%s PY=%s" % (c_doc.get("cols"), p_doc.get("cols")))
            elif code == proto.CMD_LOG_PAGE:
                compare_doc(label, c_doc, p_doc, fixed, cond, indexed)
                c_rows = c_doc.get("rows") or []
                p_rows = p_doc.get("rows") or []
                if c_rows and p_rows:
                    compare_doc(label + " 行对象", c_rows[0], p_rows[0],
                                ["ts", "category", "text"], (), ())
                else:
                    note("日志页样本行不足（C=%d PY=%d），行形状以两端 handler "
                         "键表为准" % (len(c_rows), len(p_rows)))
            elif code == proto.CMD_AI_SNAPSHOT:
                compare_doc(label, c_doc, p_doc, fixed, cond, indexed)
                # 通道名键：an{i} 有标定才发——两端至少都给出通道 0 的名字键
                check(label + " an0/e0/eu0/en0 同在",
                      (("an0" in c_doc) == ("an0" in p_doc))
                      and (("e0" in c_doc) == ("e0" in p_doc))
                      and (("en0" in c_doc) == ("en0" in p_doc)),
                      "C an0=%s e0=%s / PY an0=%s e0=%s"
                      % ("an0" in c_doc, "e0" in c_doc,
                         "an0" in p_doc, "e0" in p_doc))
            else:
                compare_doc(label, c_doc, p_doc, fixed, cond, indexed)

        # 日志类别字面（C 头无宏，实证对照）
        check_log_categories(c_root, p_root, keepalive)

        # ---- §2 错误口径 ----
        print("\n=== §2 错误口径对照（文案逐字相等） ===")
        error_cases = [
            ("未知命令 0x0199", 0x0199, None),
            ("变频器非法 op", proto.CMD_VFD_CONTROL, {"op": 999}),
            ("自动控制非法 op", proto.CMD_AUTO_CONTROL, {"op": 99}),
            ("阀越界索引", proto.CMD_VALVE_CONTROL, {"index": 99, "on": True}),
            ("泵越界索引", proto.CMD_PUMP_CONTROL, {"index": 1, "on": True}),
            ("DO 控制越界模块", proto.CMD_DIGITAL_CONTROL,
             {"dev": 99, "do": 0, "on": True}),
            ("配置未知节", proto.CMD_CONFIG_SET, {"section": "__x__"}),
            ("配置缺 section", proto.CMD_CONFIG_SET,
             {"collect": {"vfdPollMs": 200}}),
            # [0x0118 非法 kwCols 不进错误口径 2026-11-13] 两端都是「非数组 →
            # 按全部列」的容错归一（C: toArray 失败=全列；PY: 非列表 → []），
            # 正常应答 ok=true，形状由 §1 覆盖。
        ]
        for (label, code, payload) in error_cases:
            c_doc = request_doc(c_root, code, payload, keepalive=keepalive)
            p_doc = request_doc(p_root, code, payload, keepalive=keepalive)
            if c_doc is None or p_doc is None:
                check(label, False, "单端无应答")
                continue
            c_err = c_doc.get("err", "")
            p_err = p_doc.get("err", "")
            # [camera 节文案归一 2026-11-20] PY 的「未知配置节」提示里
            # 列的节清单含 camera（C 暂无该节）⇒ 文案逐字比对前把
            # PY 侧的 " / camera" 抹掉（这是"节清单长度"差，不是口径差）。
            import re as _re
            p_err_cmp = _re.sub(r"\s*/\s*camera(?=\s*）|$)", "", p_err)
            same_err = (c_err == p_err_cmp)
            both_fail = (c_doc.get("ok") is False and p_doc.get("ok") is False)
            check(label + " ok=false 且文案一致",
                  both_fail and same_err,
                  "C=%r PY=%r" % (c_err, p_err))

        # 权限拒绝文案（低档会话；拒绝发生在动作之前，不改状态）
        if c_low and p_low:
            print("\n=== §2b 权限面对照（账户=%s） ===" % c_low_acct)
            for (label, code, payload) in (
                    ("低档 VFD 控制", proto.CMD_VFD_CONTROL,
                     {"op": 5}),
                    ("低档 DO 控制", proto.CMD_DIGITAL_CONTROL,
                     {"dev": 0, "do": 0, "on": True}),
                    ("低档 模式切换", proto.CMD_RUN_MODE, {"mode": 1}),
                    ("低档 自动控制", proto.CMD_AUTO_CONTROL, {"op": 2}),
                    ("低档 配置写", proto.CMD_CONFIG_SET,
                     {"section": "login"})):
                c_doc = request_doc(c_low, code, payload, keepalive=keepalive)
                p_doc = request_doc(p_low, code, payload, keepalive=keepalive)
                if c_doc is None or p_doc is None:
                    check(label, False, "单端无应答")
                    continue
                check(label + " 拒绝且文案一致",
                      c_doc.get("ok") is False and p_doc.get("ok") is False
                      and c_doc.get("err", "") == p_doc.get("err", ""),
                      "C=%r PY=%r" % (c_doc.get("err"), p_doc.get("err")))
            # [0x0118 导出权限按档判定] admin=普通用户持 Download（两端矩阵同）：
            # 低档是 admin ⇒ 导出应**放行**且两端 ok=true；是 oper（无 Download）⇒
            # 两端都拒且同文案。这同时实证了两端权限矩阵的这一格一致。
            export_payload = {"offset": 0, "limit": 1, "export": True}
            c_doc = request_doc(c_low, proto.CMD_HISTORY_PAGE, export_payload,
                                keepalive=keepalive)
            p_doc = request_doc(p_low, proto.CMD_HISTORY_PAGE, export_payload,
                                keepalive=keepalive)
            low_has_download = (c_low_acct == "admin")
            if low_has_download:
                check("低档 历史导出（admin 持 Download）两端放行",
                      c_doc is not None and p_doc is not None
                      and c_doc.get("ok") is True and p_doc.get("ok") is True,
                      "C=%s PY=%s"
                      % (c_doc.get("ok") if c_doc else "无应答",
                         p_doc.get("ok") if p_doc else "无应答"))
            else:
                check("低档 历史导出（oper 无 Download）两端拒绝且同文案",
                      c_doc is not None and p_doc is not None
                      and c_doc.get("ok") is False and p_doc.get("ok") is False
                      and c_doc.get("err", "") == p_doc.get("err", ""),
                      "C=%r PY=%r"
                      % (c_doc.get("err") if c_doc else None,
                         p_doc.get("err") if p_doc else None))
        else:
            note("低档账户单端不可用，权限拒绝文案未对照（权限矩阵由"
                 " permissions 单测 + C 表锁定）")

        # ---- §3 主题 ----
        check_topics(c_root, p_root, keepalive)

        # ---- §4 配置写面（同值写回，无副作用）----
        print("\n=== §4 配置写面对照（同值写回） ===")
        c_get = request_doc(c_root, proto.CMD_CONFIG_GET, keepalive=keepalive)
        p_get = request_doc(p_root, proto.CMD_CONFIG_GET, keepalive=keepalive)
        if isinstance(c_get, dict) and isinstance(p_get, dict):
            for section, keys in (("login",
                                   ["loginAllowAnonymous",
                                    "loginAllowConcurrent",
                                    "loginMaxSessions"]),
                                  ("collect",
                                   ["vfdEnabled", "temphumEnabled",
                                    "turbidityEnabled", "flowmeterEnabled",
                                    "turbidity1Enabled", "turbidity2Enabled",
                                    "analogEnabled", "digitalEnabled",
                                    "vfdPollMs", "temphumPollMs",
                                    "turbidityPollMs", "turbidity1PollMs",
                                    "turbidity2PollMs", "flowmeterPollMs",
                                    "analogPollMs", "digitalPollMs"])):
                # [各端各写各的值] 同值写回的前提是「各端自己的当前值」——
                # 两端装配事实可能不同（把 C 的值写进 PY 就成了改配置）。
                c_body = dict(c_get.get(section) or {})
                c_body["section"] = section
                p_body = dict(p_get.get(section) or {})
                p_body["section"] = section
                c_set = request_doc(c_root, proto.CMD_CONFIG_SET, c_body,
                                    keepalive=keepalive)
                p_set = request_doc(p_root, proto.CMD_CONFIG_SET, p_body,
                                    keepalive=keepalive)
                ok_keys = (isinstance(c_set, dict) and isinstance(p_set, dict))
                check("§4 SET %s 两端应答" % section, ok_keys,
                      "C=%s PY=%s" % (type_class(c_set), type_class(p_set)))
                if not ok_keys:
                    continue
                check("§4 SET %s ok=true" % section,
                      c_set.get("ok") is True and p_set.get("ok") is True,
                      "C=%s PY=%s" % (c_set.get("ok"), p_set.get("ok")))
                check("§4 SET %s 成功文案一致" % section,
                      c_set.get("err", "") == p_set.get("err", ""),
                      "C=%r PY=%r" % (c_set.get("err"), p_set.get("err")))
                if section in c_set or section in p_set:
                    # [PY-only 条件键 2026-11-20] collect 的通道/量程键
                    # PY 先行 ⇒ 回显对照同样走条件键。
                    _cond = (proto.CFGK_ANALOG_PRESS_CHANNEL,
                             proto.CFGK_ANALOG_PRESS_LOW_MPA,
                             proto.CFGK_ANALOG_PRESS_HIGH_MPA,
                             proto.CFGK_ANALOG_PRESS_BELOW4_VALID,
                             proto.CFGK_ANALOG_TEMP_CHANNEL,
                             proto.CFGK_ANALOG_TEMP_LOW_C,
                             proto.CFGK_ANALOG_TEMP_HIGH_C,
                             proto.CFGK_ANALOG_TEMP_BELOW4_VALID,
                             "analogChannels")                         if section == "collect" else ()
                    compare_doc("§4 SET %s 节回显" % section,
                                c_set.get(section), p_set.get(section),
                                keys or [], _cond)
                # 回显与 GET 一致（写回后应答节 == 写前 GET 节）
                c_echo_same = c_set.get(section) == (c_get.get(section))
                p_echo_same = p_set.get(section) == (p_get.get(section))
                check("§4 SET %s 回显==GET（同值无漂移）" % section,
                      c_echo_same and p_echo_same,
                      "C=%s PY=%s" % (c_echo_same, p_echo_same))
            # storage：GET 相同值写回（enabled/path/sampleMs/maxMb 同值）
            st = dict(c_get.get("storage") or {})
            st["section"] = "storage"
            c_set = request_doc(c_root, proto.CMD_CONFIG_SET, st,
                                keepalive=keepalive)
            p_set = request_doc(p_root, proto.CMD_CONFIG_SET,
                                dict(p_get.get("storage") or {},
                                     section="storage"), keepalive=keepalive)
            check("§4 SET storage 两端 ok=true 且文案一致",
                  isinstance(c_set, dict) and isinstance(p_set, dict)
                  and c_set.get("ok") is True and p_set.get("ok") is True
                  and c_set.get("err", "") == p_set.get("err", ""),
                  "C=%r PY=%r" % (c_set.get("err") if c_set else None,
                                  p_set.get("err") if p_set else None))
            if isinstance(c_set, dict) and isinstance(p_set, dict):
                compare_doc("§4 SET storage 节回显",
                            c_set.get("storage"), p_set.get("storage"),
                            ["enabled", "path", "sampleMs", "maxMb",
                             "fullPolicy", "keepFiles", "historyTrigger"], ())
                compare_doc("§4 SET storage storageStats 回显",
                            c_set.get("storageStats"),
                            p_set.get("storageStats"),
                            ["usedBytes", "usedMb", "contentBytes",
                             "contentMb", "maxMb", "maxKb", "policy",
                             "keepFiles", "paused", "pauseReason",
                             "droppedRows", "rows", "segments", "enabled",
                             "dbPath"], ())

        # ---- 0x0107 设备健康（按条目归并；放在主题之后避免抢帧）----
        c_h = request_doc(c_root, proto.CMD_DEVICE_HEALTH, keepalive=keepalive)
        p_h = request_doc(p_root, proto.CMD_DEVICE_HEALTH, keepalive=keepalive)
        if isinstance(c_h, dict) and isinstance(p_h, dict):
            compare_doc("0x0107 设备健康", c_h, p_h,
                        ["devCount", "seq", "ts", "d*_addr", "d*_error",
                         "d*_health", "d*_kind", "d*_name", "d*_online",
                         "d*_runState", "d*_side"],
                        ["d*_text"], ("d",))
            # 台数可以不同（装配事实），但两端条目的键后缀集合要一致
            c_n = int(c_h.get("devCount", 0))
            p_n = int(p_h.get("devCount", 0))
            check("0x0107 条目数>0", c_n > 0 and p_n > 0,
                  "C=%d PY=%d" % (c_n, p_n))

        keepalive.stop = True
        c_root.close()
        p_root.close()
        if c_low:
            c_low.close()
        if p_low:
            p_low.close()
    finally:
        sims.cleanup()

    print("\n" + "=" * 70)
    print("  汇总：%d 通过 / %d 失败" % (PASS, FAIL))
    if NOTES:
        print("  备注：")
        for n in NOTES:
            print("    - %s" % n)
    if FAILED:
        print("  失败项：")
        for name in FAILED:
            print("    - %s" % name)
    return 0 if FAIL == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
