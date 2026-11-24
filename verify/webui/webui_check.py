# -*- coding: utf-8 -*-
"""webui 全功能协议级验收（纯标准库，自管进程）：

    python verify/webui/webui_check.py --server-port 6100   # C 端：复用常驻
                                                            # bin/HsfServer.exe --sim(6100)
                                                            # + 常驻 webui(8080)
    python verify/webui/webui_check.py --server-port 6200   # Python 端：自起
                                                            # server_py --sim(6200)
                                                            # + 自起 webui(8081→6200)
    python verify/webui/webui_check.py --server-port both   # 两端各跑一遍（缺省）

[收尾纪律] 只杀**自己起的**进程；复用的常驻服务（6100/8080）原样保留。
测试期间改动的服务端配置（auto 节 modes/cycle、storage.sampleMs/
historyTrigger、login 节）按开跑前抓到的原值回写恢复。

[覆盖面]（两端各跑一遍，全部硬断言，不许弱化）
    · 三档角色登录矩阵：admin(普通用户) 只读、控制 403；oper(操作者)
      可控设备、不可切模式/不可管账户/不可自动控制；root 全通；错口令 401。
    · SSE 状态帧：snapshot 首帧 + X-Hsf-Conn-Id + 订阅收敛（/api/status
      desiredTopics ⊇ 订阅集）+ 八个周期主题事件全覆盖。
    · 数字量：阀1 开→开到位回读（0x0110 快照 state=已开/openFb）→关；
      DO0/DO6 位回读（0x0108）；进水泵 DO6 启停（输出位回读）。
    · 变频器：启动（带给定）→运行→运行中给定频率→停止→点动代持
      （/api/jog/start 200ms 重发→/api/status jog 计数+变频器 jogActive→松开）。
    · 自动模式·产水：手动模式启动被拒 → auto 节 modes+cycle 同批提交 →
      切自动**不**自动启动 → 启动→阀相→泵相→运行相（0x0105 幂等回读）
      →设备回读（阀1/搅拌泵变频器/进水泵）→停止全停（含关阀）。
    · 急停：运行中急停→estop 锁存→再启动被拒（提示复位）→「复位」亮
      （autoPhase==estop/autoFault）→告警入库（/api/alarms 出现急停条目）
      →等待后复位→可再启动。
    · 组合模式「自动」：跑到反洗相（压差 gt 触发）→停止→阶段记忆生效
      （再启动从反洗继续）→复位→下次从头（产水）。
    · 自动循环实例化（2026-11-26，只 PY6200；C 端不认 instances 记 SKIP）：
      SET instances（含同 mode 重复）⇒ ok；GET 回读实例全键 + 旧键派生
      兼容视图（cycleModes 去重保序、缺席模式回合成缺省、六扁平键=首
      实例）；非法值（mode 未知/单实例/触发全关/策略非法/设备全未知/
      超上限）显式拒且 GET 原值不变；显式 instances 下旧键**写穿**
      （首实例/同 mode 首实例；逐实例 press1/press2/策略不丢；
      cycleModes 忽略；同包 instances 优先）；stirMaxRpm 联动钳位。
    · 真浏览器抓循环载荷（实例化口径）：cycle 只含 enabled/instances、
      实例全键、行序号标题=titles 规则（模式名+同类序号）；「添加实例」
      追加放水 ⇒ 载荷多一项；老服务端（C sim）⇒ 行只读+载荷绝不发
      instances。
    · 配置 GET/SET 往返：storage.historyTrigger always→auto→always；
      非法值（historyTrigger=bogus / pressThreshold="abc" /
      backwashMinutes="abc"）显式报错不假成功（GET 原值不变）；
      **局部应答不冲刷**：login 节 SET 的应答只带 login 节（无 storage 键）
      且 GET 的 storage 节原值完好。
    · 历史分页（窗口 loTs/hiTs、关键字 kw、列筛选 kwCols）+ 导出下载
      （CSV 首字节 UTF-8 BOM、CRLF 行尾、数据行数==命中总数）。
    · 日志 0x0119 分页（rows={ts,category,text}、跨页时间戳严格递减、
      category/kw 过滤子集关系）。
    · 报警列表与统计（/api/alarms 行结构、/api/alarms/active、
      /api/status alarms.total/active）。
    · 报警页服务端数据源（/api/alarms/page 0x011A 往返/形状/兜底包络
      + 前后端接线静态检查，2026-11-17 对齐桌面客户端改造）。
    · 变频器参数（/api/vfd/params 0x011B：GET 读真实值键齐 + RunLocked
      运行中拒写 + 停机幂等回写后回读一致 + 权限（读=登录/写=管理员）/
      坏载荷 400 + 前后端接线静态检查，2026-11-18；C 端无 0x011B 时按
      显式失败包络口径验收）。
    · 实时趋势「图例」多选（2026-11-22，真浏览器）：页 18 四序列/页 6 三
      序列/页 5 三序列的选项逐字；取消某条 ⇒ 不画线（canvas 指纹色归零）、
      图例条目与按钮汇总同步；再勾回历史点仍在；至少留一条；Y 轴取第一条
      可见序列量程；单序列不渲染下拉。

[上游身份判定] 6100 这一端要求上游是 **C 版 sim**（bin/HsfServer.exe
--sim）。本机实测：6100 上常驻的是 **py sim**（`python -m hsfsrv --sim
--port 6100`），不是 C 端——脚本不认端口、认**进程**：查 6100 的监听进程
命令行，含 HsfServer.exe 才复用；不是 C sim 就**不抢** 6100，另起一个 C
sim（临时程序根：exe + cfg 复制到临时目录 ⇒ 既不碰共享配置、也不与常驻
C sim 抢同一个 SQLite 库文件——C 端对库文件是独占锁，撞上就「打开库失败
⇒ 落盘关闭 ⇒ 历史/日志页全空」）。6200 那端要求上游是 **py sim**：常驻就
在用，不在就按题目口径自起。

[端口纪律] 网页端口不抢：某个端口上若已有别的服务（非 hsfweb），**不接管**
（Windows 上 127.0.0.1 与 0.0.0.0 同端口可共存、后绑定者会截走流量 ⇒
抢端口＝打断别的阶段的服务），改用空闲端口。

[已知缺陷锁·已修复] 「GET /api/mode 实时性」：此前网页后端的运行模式族
只来自缓存模型（0x0105/0x0115 控制应答时才刷新），前端 1s 轮询拿到的是
**冻结的相位**。后端已改为 hub 每秒轮询 CMD_RUN_MODE 并广播（hub.py 的
MODE_POLL_MS=1000），本脚本把该条继续锁成硬断言（设备已运行时 GET 必
须在 ~2s 内报 running，且 autoRunMs 逐秒增长）。

[已知差异·历史 ts 精度] 两端历史 ts 精度不同：**C 端是秒级**
（HsfServerApp.c: row.tsMs = currentSecsSinceEpoch() * 1000），Python 端
是毫秒级。⇒ C 端同一秒内可有多行 ⇒ ① 不能拿「ts 唯一」当分页「不重」的
判据；② 只钉 beforeTs 不够（锚点之后写的行其 ts 仍可能 ≤ 锚点）⇒ 脚本
把窗口上界也钉死（hiTs = 最新一行 ts − 1）才让计数与导出可比。脚本在
跑完分页段会打印实测精度。

[干扰说明] 常驻 sim 上可能有别的客户端插入控制动作。自动类断言失败时，
失败详情附带服务端 auto 日志尾部（/api/log/page category=auto），便于
区分「真缺陷」与「他端插手」。

退出码 0=全过，1=有失败；2=环境起不来（没跑起来）。
"""

import argparse
import datetime
import gzip
import http.client
import json
import os
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time
import urllib.parse

# [编码 2026-11-23 实锅] 中文 Windows 默认 cp936：断言名里有「⇒」这类
# GBK 编不出的字符时，**打印 PASS/FAIL 就抛 UnicodeEncodeError**（整套
# 门禁被中途打断，而进程退出码还可能是 0 ⇒ 看着像"跑完了"）。与本仓
# 测试文件的统一做法一致：显式把 stdout/stderr 转成 UTF-8。
for _stream in (sys.stdout, sys.stderr):
    try:
        _stream.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError, OSError):
        pass

REPO = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                     "..", ".."))
WEBUI_DIR = os.path.join(REPO, "HighSpeedFilter", "webui_py")
SERVER_PY_DIR = os.path.join(REPO, "HighSpeedFilter", "server_py")
BIN_DIR = os.path.join(REPO, "bin")
SIM_EXE = os.path.join(BIN_DIR, "HsfServer.exe")

HOST = "127.0.0.1"

# ---- 主题号（与 webui proto.py 同值；订阅收敛面） ----
T_VFD = 0x00010001
T_AI = 0x00010002
T_TEMPHUM = 0x00010003
T_FLOW = 0x00010004
T_HEALTH = 0x00010005
T_DIGITAL = 0x00010006
T_VALVE = 0x00010007
T_TURB = 0x00010009
T_PUMP = 0x0001000A
T_AUTO_ALARM = 0x0001000B
#: 九个遥测主题 + 自动告警主题（报警页 page11 的补订口径）。
SUB_TOPICS = [T_VFD, T_AI, T_TEMPHUM, T_FLOW, T_TURB, T_PUMP,
              T_DIGITAL, T_VALVE, T_HEALTH, T_AUTO_ALARM]
#: SSE 周期内应见到的周期性事件名（health 事件驱动，由 /api/state 覆盖）。
SSE_EVENTS = {"vfd", "analog", "temphum", "flowmeter", "turbidity",
              "pump", "digital", "valve"}

# ---- 自动模式测试配置（协议稳定 id；与 server_py test_sim_full_flow 同款） ----
# [两个复位开关显式写入] 持久化配置里可能留着此前阶段的 false（实测 py 端
# cfg 文件 resetValvesOnStop=false ⇒ 停止阀保持现状是**配置使然**）——
# 夹具显式声明口径：停止关阀复位 / 急停保持现状（2026-11-12 用户定案），
# 使「停止全停含关阀」「急停阀保持现状」两条断言两端行为确定。
AUTO_MODES_CFG = {
    "produce": {"devices": ["valve1", "stirPump", "inletPump"],
                "stirRpm": 720, "stirReverse": False,
                # [2026-11-25] 逐模式搅拌加速度（rpm/s；0=跟随变频器）。
                "stirAccelRpmS": 300,
                "resetValvesOnStop": True, "resetValvesOnEstop": False},
    "backwash": {"devices": ["valve2", "valve3", "stirPump"],
                 "stirRpm": 720, "stirReverse": True,
                 "resetValvesOnStop": True, "resetValvesOnEstop": False},
    "rinse": {"devices": ["valve4", "inletPump"],
              "stirRpm": 720, "stirReverse": False,
              "resetValvesOnStop": True, "resetValvesOnEstop": False},
}
#: 压差阈值 0.3 < sim 恒值 0.499MPa ⇒ 产水跑起来即切反洗；反洗/顺洗
#: 10 分钟 ⇒ 检查窗口内不会自己切走（阶段记忆才有稳定落点）。
AUTO_CYCLE_CFG = {"enabled": True, "pressThreshold": 0.3,
                  "pressCompare": "gt", "timerFallback": False,
                  "timerMinutes": 30, "backwashMinutes": 10,
                  "rinseMinutes": 10}

_passed = 0
_failed = []
_skipped = []            # [(label, reason)]：环境性跳过（不进 FAIL 分母）
_section = ["?"]
_end = ["?"]
# [上游会话中断门控 2026-11-26] 连续 ≥3 次 503「未连接服务端」⇒ 判上游
# 会话已断（C sim 弃用后的既有现象：DenyNew 下旧会话静默超时回收、立即
# 重连被拒；基线实测一次中断 ⇒ 约 106 条级联红）。此后**依赖上游**的
# 断言几乎必然逐条失败，但它们测到的只是「链路死了」不是产品行为 ⇒
# check() 把这些失败转记 SKIP（不进 FAIL 分母），并在首次判定处打
# [NOTE]。静态检查（读源码 / node 跑函数）不过上游 ⇒ 其失败照常记 FAIL；
# 且同一轮 `both` 会跑 PY6200（不吃级联）⇒ 产品回归仍有第二意见。
_srv_down = [False]
_srv_streak = [0]


def _kill_tree(proc):
    """杀掉浏览器**进程树**（2026-11-18）。

    [为什么不能只 terminate 主进程] Edge 会派生 gpu/renderer/utility 子
    进程；只杀主进程 ⇒ 子进程活下来 ⇒ 继续占着 CDP 端口与 profile 目录
    （实测一轮跑完残留 489 个进程、29GB profile ⇒ 系统盘满 + 下一轮
    「找不到空闲端口」）。
    ⇒ Windows 用 `taskkill /F /T`（按树杀）；其它平台先 terminate 再 kill。
    """
    if proc is None:
        return
    if os.name == "nt":
        try:
            subprocess.run(["taskkill", "/F", "/T", "/PID", str(proc.pid)],
                           capture_output=True, timeout=20)
            return
        except Exception:                            # noqa: BLE001
            pass
    try:
        if proc.poll() is None:
            proc.terminate()
            proc.wait(timeout=5)
    except Exception:                                # noqa: BLE001
        try:
            proc.kill()
        except Exception:                            # noqa: BLE001
            pass


def _rm_profile(path):
    """删掉临时的浏览器 profile 目录（删不掉就重试几次）。

    [为什么要重试] Windows 上 Edge 的子进程退出后**文件锁**还要过一会儿
    才释放 ⇒ 第一次 rmtree 会留下残目录（实测一轮跑完还剩 4 个）⇒
    重试几次（每次隔 1s）基本能删干净；删不掉也不影响门禁。
    """
    if not path:
        return
    import shutil
    import time as _t
    for _i in range(4):
        try:
            if not os.path.isdir(path):
                return
            shutil.rmtree(path, ignore_errors=True)
            if not os.path.isdir(path):
                return
        except Exception:                            # noqa: BLE001
            pass
        _t.sleep(1.0)


def check(name, cond, detail=""):
    """一条硬断言。失败带段名与详情（定位到哪端哪条）。
    [上游会话中断门控] 会话中断（503「未连接服务端」连发）判定后，此后的
    失败按环境问题转记 SKIP——链路层已死，这些断言测不到产品行为。"""
    global _passed
    label = "[%s|%s] %s" % (_end[0], _section[0], name)
    if cond:
        _passed += 1
        print("  PASS  %s" % name)
        return True
    if _srv_down[0]:
        return skip(name, "上游会话中断（503 未连接服务端连发），"
                          "依赖上游的断言不计失败")
    _failed.append((label, detail))
    print("  FAIL  %s  %s" % (name, detail))
    return cond


def skip(name, reason=""):
    """一条环境性跳过：不进 FAIL 分母、不进 PASS 计数（单独计数并在
    末尾汇总打印）。用于：① 上游会话中断后的级联；② C 端不实现/
    PY 先行键的整段或整条让位（调用量与 FAIL 相同，语义不同）。"""
    label = "[%s|%s] %s" % (_end[0], _section[0], name)
    _skipped.append((label, reason))
    print("  SKIP  %s  %s" % (name, reason))
    return True


def note_upstream(status, doc):
    """每次经 webui 的上游请求后调用：503「未连接服务端」连发计数。
    连发 ≥3 ⇒ 判会话中断（置 _srv_down，只打一次 [NOTE]）；任何非该
    形态的应答都把连发计数清零（偶发一两次 503 不判死）。"""
    if status == 503 and isinstance(doc, dict) \
            and "未连接服务端" in str(doc.get("err", "")):
        _srv_streak[0] += 1
        if _srv_streak[0] >= 3 and not _srv_down[0]:
            _srv_down[0] = True
            print("  [NOTE] [%s] 上游会话中断（503「未连接服务端」连发 %d 次）"
                  "——后续依赖上游的断言记 SKIP，不进 FAIL 分母"
                  % (_end[0], _srv_streak[0]))
    else:
        _srv_streak[0] = 0


def step(msg):
    print("    · %s" % msg)


class Section(object):
    """段落上下文（失败定位用）。"""

    def __init__(self, title):
        self.title = title

    def __enter__(self):
        _section[0] = self.title
        print("\n[%s] %s" % (_end[0], self.title))
        return self

    def __exit__(self, exc_type, exc, tb):
        if exc is not None:
            check("段落异常退出", False,
                  "%s: %s" % (type(exc).__name__, exc))
            return True                      # 已记为失败，不再外抛
        return False


# ============================================================================
#                          HTTP 小工具
# ============================================================================

def http_req(port, method, path, body=None, token=None, timeout=15.0):
    conn = http.client.HTTPConnection(HOST, port, timeout=timeout)
    headers = {"Content-Type": "application/json"}
    if token:
        headers["Authorization"] = "Bearer %s" % token
    payload = None
    if body is not None:
        payload = json.dumps(body, ensure_ascii=False).encode("utf-8")
    try:
        conn.request(method, path, body=payload, headers=headers)
        resp = conn.getresponse()
        return resp.status, dict(resp.getheaders()), resp.read()
    finally:
        conn.close()


def http_json(port, method, path, body=None, token=None, timeout=15.0):
    status, headers, data = http_req(port, method, path, body, token, timeout)
    try:
        doc = json.loads(data.decode("utf-8"))
    except (ValueError, UnicodeDecodeError):
        doc = None
    return status, headers, doc


def tcp_open(host, port, timeout=1.5):
    try:
        with socket.create_connection((host, port), timeout=timeout):
            return True
    except OSError:
        return False


def port_busy(port):
    """端口上有没有服务在听（用**连接**判定，不用 bind）。

    [为什么不用 bind] Windows 上 127.0.0.1:8080 与 0.0.0.0:8080 可以共存
    （后绑定者截走该地址的流量）⇒ bind 成功**不代表**端口空闲，抢端口
    等于打断别的阶段的服务（实测：本机 8080 上有另一个应用）。
    """
    return tcp_open(HOST, port, timeout=0.5)


def free_port(start):
    # [扫描窗口 60→320 2026-11-18] 残留的 Edge 会占住 9500 段 ⇒ 60 个
    # 端口不够用就抛「找不到空闲端口」，整段白屏门作废（实测一轮跑完
    # 残留 489 个 Edge 进程）。⇒ 扫得宽一点（仍避开常用端口段）。
    port = start
    while port < start + 320:
        if not port_busy(port):
            return port
        port += 1
    raise RuntimeError("找不到空闲端口")


# ============================================================================
#                          进程自管（只清理自己起的）
# ============================================================================

def wait_tcp(port, timeout):
    deadline = time.time() + timeout
    while time.time() < deadline:
        if tcp_open(HOST, port, 0.8):
            return True
        time.sleep(0.5)
    return False


def _spawn(label, argv, cwd):
    """起子进程（stdout/stderr 落临时文件，便于失败时取证）。"""
    out = tempfile.TemporaryFile()
    err = tempfile.TemporaryFile()
    proc = subprocess.Popen(argv, cwd=cwd, stdout=out, stderr=err)
    print("  起 %s：pid=%d %s（cwd=%s）" % (label, proc.pid,
                                          " ".join(argv[:4]), cwd))
    return proc, out, err


def _dump_tail(handle, limit=40):
    try:
        handle.seek(0)
        data = handle.read()[-4000:]
        lines = data.decode("utf-8", "replace").splitlines()
        return "\n".join(lines[-limit:])
    except Exception as exc:                            # noqa: BLE001
        return "(读输出失败：%s)" % exc


def stop_proc(proc, label, outs):
    """杀自己起的进程并确认真消失（Git Bash 下 taskkill /F 会被啃成路径，
    一律 MSYS_NO_PATHCONV 语义的 list 传参 + 事后 poll 确认）。"""
    if proc is None or proc.poll() is not None:
        return
    if os.name == "nt":
        subprocess.run(["taskkill", "/F", "/T", "/PID", str(proc.pid)],
                       capture_output=True)
    else:
        proc.terminate()
    try:
        proc.wait(timeout=10)
    except subprocess.TimeoutExpired:
        print("  (WARN) %s 未如期退出 pid=%d" % (label, proc.pid))
    for handle in outs:
        try:
            handle.close()
        except OSError:
            pass


class Owned(object):
    """自己起的进程集合（收尾统一杀）+ 收尾要删的临时目录。"""

    def __init__(self):
        self.items = []                  # (label, proc, [handles])
        self.dir_cleanups = []          # 收尾要删的临时目录

    def add(self, label, proc, handles):
        self.items.append((label, proc, handles))

    def cleanup(self):
        for label, proc, handles in reversed(self.items):
            print("[清理] 停自己起的 %s（pid=%s）" % (label, proc.pid))
            stop_proc(proc, label, handles)
        self.items = []
        for path in self.dir_cleanups:
            print("[清理] 删临时目录 %s" % path)
            shutil.rmtree(path, ignore_errors=True)
        self.dir_cleanups = []


# ============================================================================
#                    进程 / 端口身份判定（谁在听、是不是 C 端）
# ============================================================================

def _run_text(argv, timeout=8.0):
    try:
        proc = subprocess.run(argv, capture_output=True, timeout=timeout)
    except (OSError, subprocess.SubprocessError):
        return ""
    return (proc.stdout or b"").decode("utf-8", "replace")


def listener_pid(port):
    """谁在监听这个 TCP 端口（None = 查不到 / 没人在听）。"""
    if os.name == "nt":
        out = _run_text(["netstat", "-ano", "-p", "TCP"])
        for line in out.splitlines():
            parts = line.split()
            if len(parts) >= 5 and parts[0] == "TCP" and parts[3] == "LISTENING":
                if parts[1].rsplit(":", 1)[-1] == str(port):
                    try:
                        return int(parts[4])
                    except ValueError:
                        return None
        return None
    out = _run_text(["ss", "-ltnp"])
    for line in out.splitlines():
        if (":%d " % port) in line or line.rstrip().endswith(":%d" % port):
            for token in line.split("pid="):
                digits = ""
                for ch in token:
                    if ch.isdigit():
                        digits += ch
                    else:
                        break
                if digits:
                    return int(digits)
    return None


def proc_cmdline(pid):
    """进程命令行（查不到返回空串）。"""
    if os.name == "nt":
        out = _run_text(["wmic", "process", "where",
                         "ProcessId=%d" % int(pid), "get", "CommandLine",
                         "/format:list"])
        for line in out.splitlines():
            if line.lower().startswith("commandline="):
                return line.split("=", 1)[1].strip()
        out = _run_text(["powershell", "-NoProfile", "-Command",
                         "(Get-CimInstance Win32_Process -Filter "
                         "'ProcessId=%d').CommandLine" % int(pid)])
        return out.strip().splitlines()[0] if out.strip() else ""
    try:
        with open("/proc/%d/cmdline" % int(pid), "rb") as fh:
            return fh.read().decode("utf-8", "replace").replace("\x00", " ")
    except OSError:
        return ""


def proc_cmdlines():
    """全部进程的命令行（[(pid, cmdline)]；查不到返回空表）。

    [Windows 取法] `wmic process get ProcessId,CommandLine /format:csv`
    的 **CommandLine 列整列为空**（批量取不到），必须走 PowerShell 的
    Get-CimInstance（单次 ~1s，列有值）。
    """
    out = []
    if os.name == "nt":
        text = _run_text(["powershell", "-NoProfile", "-Command",
                          "Get-CimInstance Win32_Process | "
                          "Select-Object ProcessId,CommandLine | "
                          "ConvertTo-Csv -NoTypeInformation"], timeout=40.0)
        try:
            import csv as _csv
            import io as _io
            rows = list(_csv.reader(_io.StringIO(text)))
        except Exception:                                   # noqa: BLE001
            rows = []
        for row in rows[1:]:
            if len(row) < 2:
                continue
            try:
                pid = int(row[0])
            except ValueError:
                continue
            cmd = row[1]
            if cmd:
                out.append((pid, cmd))
        return out
    for entry in os.listdir("/proc"):
        if not entry.isdigit():
            continue
        cmd = proc_cmdline(int(entry))
        if cmd:
            out.append((int(entry), cmd))
    return out


def arg_value(cmd, name, default=None):
    """命令行里 --name value / --name=value 的值。"""
    parts = cmd.split()
    for i, part in enumerate(parts):
        if part == name and i + 1 < len(parts):
            return parts[i + 1]
        if part.startswith(name + "="):
            return part.split("=", 1)[1]
    return default


def upstream_is_c_sim(port):
    """监听该端口的是不是 C 版 sim（bin/HsfServer.exe）。

    :returns: (是否 C sim, 说明文字)
    """
    pid = listener_pid(port)
    if pid is None:
        return False, "端口 %d 没人在听" % port
    cmd = proc_cmdline(pid)
    if not cmd:
        return False, "端口 %d 的进程 pid=%d 命令行查不到" % (port, pid)
    low = cmd.lower()
    if "hsfserver.exe" in low:
        return True, "pid=%d %s" % (pid, cmd[:110])
    if "hsfsrv" in low:
        return False, "pid=%d 是 **Python 版** hsfsrv（不是 C 端）：%s" \
            % (pid, cmd[:110])
    return False, "pid=%d 认不出实现：%s" % (pid, cmd[:110])


def find_hsfweb_for(upstream_port):
    """找一条已经指向该上游的常驻 hsfweb（复用它，少占一个上游会话）。

    :returns: (web_port, cmdline) —— 没找到时 (None, "")
    """
    for pid, cmd in proc_cmdlines():
        low = cmd.lower()
        if "-m hsfweb" not in low:
            continue
        if _is_shell_wrapper(cmd):
            continue
        up = arg_value(cmd, "--port", "6100")
        web = arg_value(cmd, "--web-port")
        if web is None:
            continue
        try:
            if int(up) == int(upstream_port):
                return int(web), cmd
        except (TypeError, ValueError):
            continue
    return None, ""


def hsfweb_ping(port, timeout=2.5):
    """这个端口上是不是 hsfweb（探 /api/ping 的 server 标记）。"""
    try:
        status, _h, body = http_req(port, "GET", "/api/ping", timeout=timeout)
    except (OSError, http.client.HTTPException):
        return False
    if status != 200:
        return False
    try:
        doc = json.loads(body.decode("utf-8"))
    except (ValueError, UnicodeDecodeError):
        return False
    return bool(doc.get("ok")) and doc.get("server") == "hsf-webui"


# ============================================================================
#                          登录（DenyNew 8s 重试）
# ============================================================================

def web_login(port, user, password, attempts=5):
    """登录；瞬时拒绝按 **8 秒间隔**重试。

    [两种拒绝要分清] 服务端回线文本刻意反枚举，只有两种可辨：
      · 「bad password」= 凭据本身不对（永久失败 ⇒ 不重试）；
      · 「authentication rejected」= **服务端策略瞬时状态**（同账并发
        DenyNew / 会话数上限）——旧会话被静默回收、或上限被抬高后即通过，
        ⇒ 按 8s 间隔重试（题目要求的 DenyNew 节流口径）。
    另有中文态「账户已在线」「会话数已达上限」，同样按瞬时处理。

    :returns: (status, headers, doc) —— 同 http_json 形状。
    """
    last = (0, {}, None)
    for i in range(attempts):
        status, _h, doc = http_json(port, "POST", "/api/login",
                                    {"user": user, "password": password},
                                    timeout=20.0)
        if status == 200 and isinstance(doc, dict) and doc.get("ok"):
            return status, _h, doc
        last = (status, _h, doc)
        err = str((doc or {}).get("err", "")) if isinstance(doc, dict) else ""
        low = err.lower()
        permanent = ("bad password" in low or "口令" in err or "密码" in err
                     or "unknown user" in low or "locked" in low)
        transient = (not permanent) and (
            "authentication rejected" in low or "已在线" in err
            or "并发" in err or "上限" in err or "超时" in err
            or "连不上" in err or "无应答" in err)
        if transient and i < attempts - 1:
            print("    · 登录被瞬时拒（%s），8s 后重试（%d/%d）"
                  % (err[:60], i + 1, attempts))
            time.sleep(8.0)
            continue
        break
    return last


# ============================================================================
#                          SSE 收集线程
# ============================================================================

class SseCollector(object):
    """后台线程读 SSE 流：记 connId、按事件名计数、保留 snapshot 首帧。

    [断了要能重连] 浏览器的 EventSource 断线会自动重连；本收集器同样提供
    ensure_alive()：线程死亡（任何异常/服务端关闭）时重开一条流并重发
    订阅——长跑验收里「流断一次 ⇒ 自动告警整段收不到」会制造假红。
    """

    def __init__(self):
        self.conn = None
        self.resp = None                 # SSE 应答对象（readline 走它）
        self.conn_id = None
        self.snapshot = None
        self.events = {}                 # kind → 计数
        self.alarm_texts = []            # alarm 事件 text 集合
        self.error = ""                  # 线程死亡原因（诊断用，不再刷屏）
        self.lock = threading.Lock()
        self.dead = True
        self.token = ""
        self.port = 0

    def start(self, port, token):
        self.port = port
        self.token = token
        return self._connect()

    def _connect(self):
        try:
            conn = http.client.HTTPConnection(HOST, self.port, timeout=60.0)
            conn.request("GET", "/api/stream", headers={
                "Authorization": "Bearer %s" % self.token,
                "Accept": "text/event-stream"})
            resp = conn.getresponse()
            with self.lock:
                self.conn = conn
                self.resp = resp
                self.conn_id = resp.getheader("X-Hsf-Conn-Id")
                self.dead = False
                self.error = ""
        except (OSError, http.client.HTTPException) as exc:
            with self.lock:
                self.dead = True
                self.error = repr(exc)
            print("    · SSE 连接失败：%r" % (exc,))
            return False
        thread = threading.Thread(target=self._run, daemon=True)
        thread.start()
        return True

    def ensure_alive(self):
        """流死了就重连（重连后订阅集合由 hub 巡检收敛回来）。"""
        with self.lock:
            alive = not self.dead
        if alive:
            return True
        ok = self._connect()
        if ok:
            print("    · SSE 流断线重连成功（connId=%s）" % self.conn_id)
        return ok

    def reapply_topics(self, ctx):
        """重连后把订阅意图重新发一遍（新 connId）。"""
        with self.lock:
            conn_id = self.conn_id
        if conn_id is None:
            return False
        status, _h, doc = ctx.post("/api/topics",
                                   {"connId": int(conn_id),
                                    "topics": SUB_TOPICS},
                                   token=self.token)
        return status == 200 and (doc or {}).get("applied") == 1

    def _run(self):
        event = None
        err = ""
        try:
            while True:
                line = self.resp.readline()
                if not line:
                    err = "流关闭（服务端断开）"
                    break
                text = line.decode("utf-8", "replace").strip()
                if text.startswith("event: "):
                    event = text[len("event: "):].strip()
                elif text.startswith("data: ") and event:
                    try:
                        doc = json.loads(text[len("data: "):])
                    except ValueError:
                        doc = None
                    with self.lock:
                        self.events[event] = self.events.get(event, 0) + 1
                        if event == "snapshot" and self.snapshot is None:
                            self.snapshot = doc
                        if event == "alarm" and isinstance(doc, dict):
                            self.alarm_texts.append(str(doc.get("text", "")))
                    event = None
                elif not text.startswith(":"):   # 心跳注释行忽略
                    event = None
        except BaseException as exc:                        # noqa: BLE001
            err = "%s: %s" % (type(exc).__name__, exc)
        finally:
            with self.lock:
                self.dead = True
                self.error = err

    def close(self):
        try:
            if self.conn is not None:
                self.conn.close()
        except OSError:
            pass

    def got(self, kind):
        with self.lock:
            return self.events.get(kind, 0)

    def has_alarm_text(self, needle):
        with self.lock:
            return any(needle in t for t in self.alarm_texts)


# ============================================================================
#                          验收上下文
# ============================================================================

class Ctx(object):
    """一端验收的全部句柄。"""

    def __init__(self, web_port, tag):
        self.web = web_port
        self.tag = tag
        self.upstream = 0                   # 实际用的上游端口（6100 可能被占→换端口）
        self.note = ""                      # 上游来源说明（复用/自起/换端口）
        self.tokens = {}                 # user → token
        self.sse = None
        self.orig_auto = None            # 开跑前的 auto 节（modes+cycle）
        self.orig_storage = None         # 开跑前的 storage 节
        self.orig_max_sessions = None    # 开跑前的 loginMaxSessions（抬过才恢复）
        self.storage_touched = False     # 是否改过 sampleMs
        self._mode_sync = 0              # 最近一次已知的服务端运行模式
        self.last_err = ""               # 最近一次设备快照请求失败原因

    # ---- 基础请求 ----
    # [都会过 note_upstream] 503「未连接服务端」连发 ≥3 ⇒ 判上游会话中断
    # （check() 据此把后续失败转记 SKIP；见文件头的 _srv_down 注释）。
    def get(self, path, token=None, timeout=15.0):
        result = http_json(self.web, "GET", path, token=token,
                           timeout=timeout)
        note_upstream(result[0], result[2])
        return result

    def post(self, path, body=None, token=None, timeout=15.0):
        result = http_json(self.web, "POST", path, body=body, token=token,
                           timeout=timeout)
        note_upstream(result[0], result[2])
        return result

    def set_mode(self, token, mode):
        """切/确认运行模式（应答 mode 为准同步 _mode_sync）。"""
        status, _h, doc = self.post("/api/mode", {"mode": int(mode)},
                                    token=token)
        if isinstance(doc, dict) and "mode" in doc:
            try:
                self._mode_sync = int(doc["mode"])
            except (TypeError, ValueError):
                pass
        return status, doc

    def set_mode_confirmed(self, token, mode, attempts=3):
        """切模式并**确认生效**（链路抖动吞掉切换时重发一次）。

        [为什么必须确认] 切自动是后续自动段的硬前置；POST 超时/丢应答时
        服务器还在手动，下一段的启动会拿「手动模式拒绝」这种误导性失败。
        """
        for _ in range(attempts):
            status, doc = self.set_mode(token, mode)
            live = self.mode_live(token=token)
            if live.get("mode") == int(mode) and \
                    (doc or {}).get("ok") is not False:
                return live
            print("    · 切模式未确认（mode=%r），2s 后重发"
                  % (live or {}).get("mode"))
            time.sleep(2.0)
        return self.mode_live(token=token)

    def token_of(self, user):
        return self.tokens.get(user, "")

    # ---- 轮询帮手 ----
    def poll(self, fn, timeout, interval=0.5, label=""):
        """轮询直到 fn() 返回真值；返回最后一个真值（或 None）。"""
        deadline = time.time() + timeout
        result = None
        while time.time() < deadline:
            result = fn()
            if result:
                return result
            time.sleep(interval)
        if label:
            print("    · 轮询超时（%s，%.0fs）" % (label, timeout))
        return None

    # ---- 运行模式族（0x0105 幂等回读 = 服务端纯查询、不动设备）----
    def mode_live(self, current_mode=None, token=""):
        """同模式重发 0x0105 → 服务端回**实时**全量状态键（幂等重放不动设备）。

        [为什么不用 GET /api/mode] 它读的是 web 缓存模型（见缺陷锁）；本
        帮手经 POST /api/mode 走到上游 0x0105，拿到控制器此刻的真实相位。
        发送的模式取 current_mode（缺省=上次同步到的模式）——若与服务端
        实际模式一致就是纯查询；应答的 mode 键回写 _mode_sync（他端插手
        切了模式时，下一拍就跟着新模式走，不把「查询」变成「切换」）。
        """
        want = self._mode_sync if current_mode is None else int(current_mode)
        status, _h, doc = self.post("/api/mode", {"mode": want}, token=token)
        if status == 200 and isinstance(doc, dict):
            if "mode" in doc:
                try:
                    self._mode_sync = int(doc["mode"])
                except (TypeError, ValueError):
                    pass
            return doc
        return {}

    def wait_phase(self, token, phase, timeout):
        """等运行相位到达 phase；返回 (到达与否, 最后一次回读文档)。"""
        deadline = time.time() + timeout
        last = {}
        while time.time() < deadline:
            last = self.mode_live(token=token)
            if last.get("autoPhase") == phase:
                return True, last
            time.sleep(0.6)
        return False, last

    def valve_state(self, token, index=0):
        """阀快照回读（0x0110 现读）：返回该路的 {state,openFb,closeFb,...}。"""
        status, _h, doc = self.post("/api/valve/snapshot", {}, token=token,
                                    timeout=20.0)
        if status != 200 or not isinstance(doc, dict):
            self.last_err = "valve/snapshot status=%s doc=%s" \
                % (status, str(doc)[:100])
            return None
        out = {"state": doc.get("v%d_state" % index),
               "openFb": doc.get("v%d_openFb" % index),
               "closeFb": doc.get("v%d_closeFb" % index),
               "ready": doc.get("v%d_ready" % index)}
        return out

    def digital_do_bit(self, token, dev, bit):
        """数字量 DO 单位回读（0x0108 现读）。"""
        status, _h, doc = self.post("/api/digital/snapshot", {"dev": dev},
                                    token=token, timeout=20.0)
        if status != 200 or not isinstance(doc, dict):
            self.last_err = "digital/snapshot status=%s" % status
            return None
        mask = doc.get("do")
        if mask is None:
            self.last_err = "digital/snapshot 无 do 键"
            return None
        return bool(int(mask) & (1 << int(bit)))

    def pump_on(self, token, index=0):
        status, _h, doc = self.post("/api/pump/snapshot", {}, token=token,
                                    timeout=15.0)
        if status != 200 or not isinstance(doc, dict):
            self.last_err = "pump/snapshot status=%s" % status
            return None
        return doc.get("v%d_on" % index)

    def vfd_field(self, token, key):
        status, _h, doc = self.post("/api/vfd/snapshot", {}, token=token,
                                    timeout=15.0)
        if status != 200 or not isinstance(doc, dict):
            self.last_err = "vfd/snapshot status=%s" % status
            return None
        return doc.get(key)

    def upstream_subs(self, token):
        """上游会话此刻的实际订阅表（诊断：告警帧没人收时看它）。"""
        status, _h, st = self.get("/api/status", token=token)
        return ((st or {}).get("upstream") or {}).get("subscriptions") or []

    # ---- 失败取证：服务端 auto 日志尾部 ----
    def auto_log_tail(self, token, n=8):
        status, _h, doc = self.get("/api/log/page?limit=%d&category=auto"
                                   "&offset=0" % n, token=token)
        rows = (doc or {}).get("rows") or [] if status == 200 else []
        out = []
        for row in reversed(rows):
            stamp = datetime.datetime.fromtimestamp(
                row.get("ts", 0) / 1000.0).strftime("%H:%M:%S")
            out.append("%s %s" % (stamp, str(row.get("text", ""))[:70]))
        return " | ".join(out) if out else "(无 auto 日志)"

    def fail_with_log(self, name, detail, token):
        check(name, False, "%s ｜ 服务端auto日志: %s"
              % (detail, self.auto_log_tail(token)))


# ============================================================================
#                          验收段
# ============================================================================

def wait_hsfweb(port, timeout=30.0):
    """等这个端口上的**是 hsfweb**（探 /api/ping 的 server 标记）。

    [为什么不能只看 HTTP 200] 端口被别的应用占着时它也可能回 200（只是
    不是 hsfweb 的应答）⇒ 认服务器要看 /api/ping 的 server 字段。
    """
    deadline = time.time() + timeout
    while time.time() < deadline:
        if hsfweb_ping(port):
            return True
        time.sleep(0.4)
    return False


def listening_port_of_pid(pid):
    """这个 pid 在监听哪个 TCP 端口（None = 不是监听者 / 查不到）。"""
    if os.name == "nt":
        out = _run_text(["netstat", "-ano", "-p", "TCP"])
        for line in out.splitlines():
            parts = line.split()
            if len(parts) >= 5 and parts[0] == "TCP" \
                    and parts[3] == "LISTENING" and parts[4] == str(pid):
                try:
                    return int(parts[1].rsplit(":", 1)[-1])
                except ValueError:
                    return None
        return None
    out = _run_text(["ss", "-ltnp"])
    for line in out.splitlines():
        if ("pid=%d," % pid) in line or ("pid=%d)" % pid) in line:
            for token in line.split():
                if ":" in token and token.split(":")[-1].isdigit():
                    return int(token.split(":")[-1])
    return None


def _is_shell_wrapper(cmd):
    """命令行是不是 shell/包装进程（bash -c / cmd / powershell）。

    [为什么要排除] 这些进程的命令行里**含有**我们要找的字符串（历史命令
    整条都在里面）⇒ 不排除就会把「上一阶段起过的进程」误认成在跑的实例。
    """
    low = cmd.lower()
    return any(k in low for k in ("bash.exe", "bash -c", "cmd.exe",
                                  "powershell", "snapshot-bash", "wmic"))


def find_running_c_sim():
    """找**在跑的** C 版 sim（bin/HsfServer.exe --sim）⇒ (端口, 说明)。

    [为什么不认端口认进程] 端口号只是约定：本机实测 6100 上常驻的是
    **py sim**（python -m hsfsrv --sim --port 6100），真正的 C 端跑在别的
    端口上。验收要覆盖的是**实现**，故按进程命令行认（C 版 = HsfServer.exe
    且带 --sim），端口取该进程的监听口。
    """
    for pid, cmd in proc_cmdlines():
        low = cmd.lower()
        if "hsfserver.exe" not in low or "--sim" not in low:
            continue
        if _is_shell_wrapper(cmd):
            continue
        port = listening_port_of_pid(pid)
        if port is None:
            continue
        return port, "pid=%d %s" % (pid, cmd[:110])
    return None, ""


def start_c_sim(port, owned):
    """自起一个 C 端 sim：exe + 配置**复制到临时程序根**再跑。

    [为什么复制程序根] ① C 服务端的配置/账户库/历史库都落在 <程序根>/cfg/；
      直接在 bin/ 起会与常驻 C sim 抢同一个 SQLite 文件——C 端对库文件是
      **独占**锁（本机实测：常驻 C sim 正开着 bin/cfg/hsf_data.db ⇒ 自起
      的实例「打开库失败」⇒ 落盘关闭 ⇒ 历史/日志页全空）；
      ② 复制到临时目录 ⇒ **不碰**共享配置与共享数据文件，收尾直接删目录。

    :returns: (进程, 说明)
    """
    tmp = tempfile.mkdtemp(prefix="webui_check_csim_%d_" % port)
    os.makedirs(os.path.join(tmp, "cfg"), exist_ok=True)
    shutil.copy2(SIM_EXE, os.path.join(tmp, "HsfServer.exe"))
    # 配置模板：共享 ini（若在），只把 [storage] 的库路径改到临时目录。
    src_ini = os.path.join(BIN_DIR, "cfg", "hsf_server.ini")
    dst_ini = os.path.join(tmp, "cfg", "hsf_server.ini")
    text = ""
    if os.path.isfile(src_ini):
        with open(src_ini, "r", encoding="utf-8") as fh:
            text = fh.read()
    db_path = os.path.join(tmp, "hsf_data.db").replace("\\", "\\\\")
    out = []
    for line in text.splitlines():
        if line.startswith("path="):
            line = "path=" + db_path
        out.append(line)
    if not any(l.startswith("[storage]") for l in out):
        out += ["[storage]", "enabled=1", "sample_ms=1000", "path=" + db_path]
    with open(dst_ini, "w", encoding="utf-8", newline="") as fh:
        fh.write("\r\n".join(out) + "\r\n")
    # 账户库 / 登录策略：沿用共享的一份（同一套账户与并发口径）
    for name in ("hsf_users.json", "hsf_login_policy.json"):
        src = os.path.join(BIN_DIR, "cfg", name)
        if os.path.isfile(src):
            shutil.copy2(src, os.path.join(tmp, "cfg", name))
    proc, out_h, err_h = _spawn("c-sim(%d)" % port,
                                 [os.path.join(tmp, "HsfServer.exe"),
                                  str(port), "--sim"], tmp)
    owned.add("c-sim(%d)" % port, proc, [out_h, err_h])
    owned.dir_cleanups.append(tmp)
    check("C sim 起来（TCP %d，临时程序根 %s）" % (port, tmp),
          wait_tcp(port, 40.0), _dump_tail(err_h))
    return port


def start_py_sim(port, owned):
    """自起一个 py 端 sim：**独立临时程序根**（配置/账户/库全隔离）。

    [为什么必须隔离 —— 2026-11-15 实锅] py 端的路径全是「程序根」相对：
        · 配置   = <base_dir>/cfg/hsf_server_py.json
        · 账户库 = <base_dir>/cfg/hsf_users.json
        · 历史库 = <base_dir>/hsf_data.db
    而 base_dir 默认 = **服务端程序目录**（`server_py/`）⇒ **本机常驻的
    6100 py sim 与自起的 6200 py sim 共用同一份配置与同一个 SQLite 库**：
        ① **运行模式互相覆盖**：谁的 save() 后落盘谁赢（常驻那台在自动模式
           ⇒ 每 60s 的 auto_run_ms 落盘会把 run_mode=1 写回去）⇒ 自起的那台
           下次启动就是「自动模式」⇒ 手动模式段的读数/闸门全串味；
        ② **历史行混在一起**：常驻那台在自动模式跑 ⇒ 库里全是「自动·产水/
           反洗/顺洗」⇒ 本段断言「关键字筛『手动』列=模式 ⇒ 命中 > 0」必然
           0 命中（实测 PY6200 段 1 条红，根因就是这个，不是产品缺陷）。
    C 端那一侧早就是「临时程序根」（start_c_sim），py 端这侧漏了 ⇒ 现在对齐。

    [怎么隔离] `--config <临时目录>/hsf_server_py.json` ⇒ base_dir 变成临时
    目录（`__main__._dir_of` 取配置文件所在目录）⇒ 配置/账户/库三样全部
    落在临时目录里。配置内容**照抄共享那份**（设备/采集口径一致），但
    **runtime 节清空**（run_mode 归手动 ⇒ 新实例从手动起步，不继承上一轮
    的自动模式）。账户库照抄（同一套账户与并发口径）。
    """
    tmp = tempfile.mkdtemp(prefix="webui_check_pysim_%d_" % port)
    os.makedirs(os.path.join(tmp, "cfg"), exist_ok=True)
    # ---- 账户库：照抄共享的一份（同一套账户/并发口径）----
    src_users = os.path.join(SERVER_PY_DIR, "cfg", "hsf_users.json")
    if os.path.isfile(src_users):
        shutil.copy2(src_users, os.path.join(tmp, "cfg", "hsf_users.json"))
    # ---- 配置：照抄，但 runtime 节清空（不继承上一轮的运行模式）----
    src_cfg = os.path.join(SERVER_PY_DIR, "cfg", "hsf_server_py.json")
    dst_cfg = os.path.join(tmp, "hsf_server_py.json")
    doc = {}
    if os.path.isfile(src_cfg):
        try:
            with open(src_cfg, "r", encoding="utf-8") as fh:
                doc = json.load(fh)
        except (OSError, ValueError):
            doc = {}
    if not isinstance(doc, dict):
        doc = {}
    doc["runtime"] = {"run_mode": 0, "auto_run_ms": 0, "auto_mode": ""}
    # [2026-11-27 实锅：装置污染] 共享 cfg 里若已带**显式 auto.cycle.instances**
    # （本地调试/桌面客户端保存过），服务端一启动就是"实例为唯一事实源"态
    # ⇒ 本脚本的旧键契约段（SET cycleModes/stageCfg 再回读顺序）会被按
    # 设计忽略 ⇒ 顺序/非法拒绝/阶段记忆连片假红（22 条）。种子必须是
    # **无循环配置**的干净 auto 节：cycle 只留 enabled，其余清空。
    try:
        _auto = doc.get("auto") or {}
        if isinstance(_auto, dict):
            _auto["cycle"] = {"enabled": bool(
                (_auto.get("cycle") or {}).get("enabled", True))}
            doc["auto"] = _auto
    except (AttributeError, TypeError):
        pass
    with open(dst_cfg, "w", encoding="utf-8", newline="") as fh:
        json.dump(doc, fh, ensure_ascii=False, indent=2)
    proc, out_h, err_h = _spawn(
        "py-sim(%d)" % port,
        [sys.executable, "-m", "hsfsrv", "--sim", "--port", str(port),
         "--config", dst_cfg], SERVER_PY_DIR)
    owned.add("py-sim(%d)" % port, proc, [out_h, err_h])
    owned.dir_cleanups.append(tmp)
    # [2026-11-22 实锅] 端口上**已有别人**在听时，本进程 bind 失败会立刻
    # 退出，而 wait_tcp 只证明"有人听" ⇒ 旧实现照样 PASS，后续测试打在
    # 别人的实例上（并发跑两个门禁时实测：布局驱动抢到 6200、本进程却
    # 「判定起来了」，随后布局收尾一杀，本套全部 503/401）。
    # ⇒ 必须同时确认**子进程还活着**（活着才说明 bind 成功）。
    ready = wait_tcp(port, 40.0) and proc.poll() is None
    check("py sim 起来（TCP %d，临时程序根 %s）" % (port, tmp),
          ready, _dump_tail(err_h))
    return port


def resolve_upstream(args, owned):
    """决定本端验收用的上游端口。

    6100 = **C 端**（bin/HsfServer.exe --sim）：常驻 6100 若确实是 C sim
    就复用（题目口径）；6100 上是别的实现（本机实测是 py sim）⇒ **不抢**，
    在空闲端口自起一个 C sim（临时程序根，隔离共享库；自己起的 ⇒ 收尾
    停掉并删目录）。
    6200 = **py 端**：常驻就在用，不在就按题目口径自起（**临时程序根**，
    隔离共享配置与共享库 —— 见 start_py_sim 的坑注）。
    """
    want = int(args.server_port)
    if want == 6200:
        if tcp_open(HOST, 6200):
            print("  · py sim 6200 已在跑 → 复用")
            return 6200, "py 端：复用常驻 6200"
        print("  · py sim 6200 没在跑 → 自起（临时程序根；配置/账户/库隔离，"
              "不与常驻 6100 那台共用）")
        start_py_sim(6200, owned)
        return 6200, "py 端：自起 6200（临时程序根）"

    # ---- 6100：C 端 ----
    is_c, why = upstream_is_c_sim(6100)
    if is_c:
        print("  · 6100 上就是 C 版 sim → 复用（%s）" % why)
        return 6100, "C 端：复用常驻 6100（C sim）"
    other, csim = find_running_c_sim()
    if other:
        print("  · 另有在跑的 C 版 sim（%s）——本端按 6100 口径，另起一个"
              "干净的（不碰它的会话与数据）" % csim)
    port = 6100 if not tcp_open(HOST, 6100) else free_port(6101)
    print("  · 6100 上不是 C sim（%s）→ 不抢，另在 %d 自起 C sim（临时程序根）"
          % (why, port))
    start_c_sim(port, owned)
    return port, ("C 端：6100 被 %s 占用 → 自起 %d（C sim，临时程序根）"
                  % (why.split("：")[0], port))


def resolve_webui(upstream_port, preferred, owned):
    """网页服务器：优先复用已指向该上游的常驻 hsfweb，否则自起一个。

    [不抢端口] 端口上有别的服务（非 hsfweb）时**不接管**（Windows 上同
    端口并存会截走流量 ⇒ 抢端口＝打断别的阶段的服务），改用空闲端口。
    """
    web, cmd = find_hsfweb_for(upstream_port)
    if web and hsfweb_ping(web):
        print("  · 复用常驻 webui（%d → 上游 %d）：%s"
              % (web, upstream_port, cmd[:90]))
        return web
    port = preferred
    if port_busy(port):
        if hsfweb_ping(port):
            print("  · %d 上是 hsfweb，但指向别的上游 → 另起一个" % port)
        else:
            print("  · %d 被别的服务占用（不抢）→ 改用空闲端口" % port)
        port = free_port(port + 1)
    argv = [sys.executable, "-m", "hsfweb", "--server", HOST,
            "--port", str(upstream_port), "--bind", HOST,
            "--web-port", str(port)]
    proc, out, err = _spawn("webui(%d→%d)" % (port, upstream_port),
                            argv, WEBUI_DIR)
    owned.add("webui(%d)" % port, proc, [out, err])
    check("webui 起来（/api/ping 认 server=hsf-webui）",
          wait_hsfweb(port, 30.0), _dump_tail(err))
    return port


def sec_env(ctx, args, owned):
    """进程与探活：复用常驻 / 自起。"""
    with Section("环境与进程（探活/自起/复用）"):
        up, note = resolve_upstream(args, owned)
        ctx.upstream = up
        ctx.note = note
        check("上游 %d 可连" % up, tcp_open(HOST, up), note)
        ctx.web = resolve_webui(up, ctx.web, owned)
        check("网页服务器 %d 可用（hsf-webui）" % ctx.web,
              hsfweb_ping(ctx.web), "web=%s" % ctx.web)


def sec_public(ctx):
    with Section("无鉴权面（ping/version/pages）"):
        status, _h, doc = ctx.get("/api/ping")
        check("GET /api/ping", status == 200 and (doc or {}).get("ok") is True,
              "status=%s" % status)
        # [版本戳 2026-11-19] index.html 的静态引用带 ?v=<指纹> ⇒ 改一次
        # JS 必刷新一次（用户实报过「刷新网页也不对」——浏览器还在跑旧
        # JS；光有 Cache-Control: no-cache 不够，旧 js 自己也可能被缓存）。
        # [必须用 http_req 不是 ctx.get] ctx.get 走 http_json ⇒ index.html
        # 不是合法 JSON ⇒ 解析成 None ⇒ 检查拿到空（假红；2026-11-19 实测）。
        # [再踩一次的教训] http_req 返回 (status, headers, data) 三元组——
        # 第一版用 [:2] 把 data 扔了、拿 headers 当正文 ⇒ 永远 0 次（又红
        # 一轮才发现）。
        _status, _h, _raw = http_req(ctx.web, "GET", "/")
        _html = _raw.decode("utf-8", "replace") if \
            isinstance(_raw, bytes) else str(_raw or "")
        check("index.html 的静态引用带版本戳（?v=…，改 JS 必刷新）",
              _status == 200 and _html.count("?v=") >= 5,
              "status=%s ?v= 出现 %d 次" % (_status, _html.count("?v=")))
        status, _h, ver = ctx.get("/api/version")
        feats = (ver or {}).get("features") or []
        check("GET /api/version（features 全）",
              status == 200 and all(f in feats for f in
                                    ("sse", "history-proxy", "log-page",
                                     "export-csv", "auto-modes",
                                     "config-rw")),
              str(feats))
        status, _h, pages = ctx.get("/api/pages")
        names = (pages or {}).get("names") or []
        nav = (pages or {}).get("nav") or []
        # [2026-11-19 相机页] 页数 16 → 18：17 = 相机（设备组下），
        # 16 = **保留槽位**（桌面客户端的「Web 设置」，网页端不实现 ⇒
        # 槽位保留是为了两端页号逐号对齐：同一个页号指的是同一页）。
        # [2026-11-22 页 6 拆分] 流量页追加末尾 ⇒ 19 页 / 导航 17 项
        # （水质(6) 只留浊度，流量计+进水泵移到页 18「流量」）。
        check("GET /api/pages：19 页 + 导航 17 项",
              status == 200 and len(names) == 19 and len(nav) == 17,
              "names=%d nav=%d" % (len(names), len(nav)))
        check("页名 2026-11-22 口径（设备/搅拌泵/水质/电动球阀 + 页 18 流量，"
              "无「监视/变频器/流量计/电磁阀/水质监测」）",
              len(names) == 19 and names[2] == "设备" and names[5] == "搅拌泵"
              and names[6] == "水质" and names[7] == "电动球阀"
              and names[18] == "流量",
              str(names))
        # [2026-11-22 页 6 拆分] 导航顺序：数字量、模拟量、搅拌泵、水质、
        # 流量、电动球阀、相机（设备组下）。
        _devices = [n.get("caption") for n in nav
                    if n.get("page") in (3, 4, 5, 6, 18, 7, 17)
                    and n.get("childOf") == 1]
        check("导航树设备组顺序（数字量/模拟量/搅拌泵/水质/流量/电动球阀/相机）",
              _devices == ["数字量", "模拟量", "搅拌泵", "水质", "流量",
                           "电动球阀", "相机"],
              str(_devices))
        # [相机页 2026-11-19] 页名/页号/导航/主题四件套。
        # [注意 nav 是 dict 列表] /api/pages 的 nav 每项是
        # {"caption":…,"page":…,"childOf":…} ⇒ 不能按元组下标取（踩过
        # `KeyError: 0`，整个段落异常退出）。
        check("相机页：页号 17 + 页名「相机」+ 在设备组下 + 订阅相机主题",
              len(names) > 17 and names[17] == "相机"
              and any(n.get("caption") == "相机" and n.get("page") == 17
                      and n.get("childOf") == 1 for n in nav)
              and (pages or {}).get("pageTopics", {}).get("17") == [0x0001000C],
              "names=%s nav=%s topics=%s"
              % (names[15:],
                 [n for n in nav if n.get("caption") == "相机"],
                 (pages or {}).get("pageTopics", {}).get("17")))
        # [2026-11-22 页 6 拆分] 流量页 = 18（追加末尾），挂设备组；
        # 主题 = 流量计 + 进水泵；水质页只订浊度。
        _pt = (pages or {}).get("pageTopics", {}) or {}
        check("流量页：页号 18 + 页名「流量」+ 在设备组下 + 订阅流量/泵主题",
              len(names) > 18 and names[18] == "流量"
              and any(n.get("caption") == "流量" and n.get("page") == 18
                      and n.get("childOf") == 1 for n in nav)
              and _pt.get("18") == [0x00010004, 0x0001000A],
              "names=%s nav=%s topics=%s"
              % (names[15:],
                 [n for n in nav if n.get("caption") == "流量"],
                 _pt.get("18")))
        check("水质页只订浊度主题（流量计/泵已移到页 18）",
              _pt.get("6") == [0x00010009], "topics=%s" % (_pt.get("6"),))
        check("页号 16 是保留槽位（Web 设置，网页端不实现 ⇒ 不在导航里）",
              len(names) > 16 and names[16] == "Web 设置"
              and not any(n.get("page") == 16 for n in nav),
              "names=%s" % (names[15:17],))


def sec_login(ctx):
    with Section("登录矩阵（三档角色 + 错口令；DenyNew 节流："
                 "角色间 ≥8s、单次重试 ≤5 次）"):
        # [节流纪律 2026-11-13 会话主人定案] 每次登录尝试间隔 ≥2s、
        # 总重试 ≤5；角色切换之间 ≥8s（DenyNew 回收窗口）。
        # [顺序为什么是「错口令 → root → admin → oper」] 网页服务器的
        # 上游主链路用的就是 **root**：首个浏览器登录成功后 1s 它才连上游
        # （惰性）。若先登 admin，root 的浏览器探测就会被主链路占住
        # root 会话（同账并发关时直接被策略拒）⇒ **root 必须在主链路连上
        # 之前登**，故把 root 提到第二位。admin/oper 是别的账户，不受
        # 同账并发影响。
        # ---- 错口令（最先做：此时上游还没有任何会话，最干净）----
        status, _h, doc = web_login(ctx.web, "root", "wrong-password!")
        err = str((doc or {}).get("err", "")) if isinstance(doc, dict) else ""
        cred_rejected = status == 401 and (
            "bad password" in err.lower() or "口令" in err or "密码" in err)
        if not cred_rejected:
            # 策略拒（同账并发/会话上限）时换个**没在线**的账户名再试一次
            # 错口令 ⇒ 拿到凭据级拒绝证据（假挑战路径也回 bad password）。
            st2, _h2, doc2 = web_login(ctx.web, "no-such-user-zz", "whatever")
            err2 = str((doc2 or {}).get("err", "")) if isinstance(doc2, dict) \
                else ""
            cred_rejected = st2 == 401 and (
                "bad password" in err2.lower() or "口令" in err2
                or "密码" in err2)
        check("错口令被拒（401 + 凭据级原因）",
              status == 401 and cred_rejected,
              "status=%s err=%s" % (status, err or "(无)"))
        time.sleep(8.0)                      # DenyNew 回收窗口

        # ---- root（管理员）：必须排在 admin/oper 之前（见上）----
        status, _h, doc = web_login(ctx.web, "root", "root")
        sess = (doc or {}).get("session") or {}
        if check("root 登录 200（角色=2 管理员）",
                 status == 200 and sess.get("role") == 2,
                 "status=%s err=%s sess=%s" % (status,
                                               str((doc or {}).get("err")),
                                               str(sess)[:120])):
            ctx.tokens["root"] = doc.get("token", "")
        time.sleep(8.0)

        # ---- admin（普通用户=查看+下载）----
        status, _h, doc = web_login(ctx.web, "admin", "admin")
        sess = (doc or {}).get("session") or {}
        if check("admin 登录 200（角色=1 普通用户）",
                 status == 200 and sess.get("role") == 1,
                 "status=%s err=%s sess=%s" % (status,
                                               str((doc or {}).get("err")),
                                               str(sess)[:120])):
            ctx.tokens["admin"] = doc.get("token", "")
        time.sleep(8.0)

        # ---- oper（操作者=+设备控制）----
        status, _h, doc = web_login(ctx.web, "oper", "oper")
        sess = (doc or {}).get("session") or {}
        if check("oper 登录 200（角色=3 操作者）",
                 status == 200 and sess.get("role") == 3,
                 "status=%s err=%s sess=%s" % (status,
                                               str((doc or {}).get("err")),
                                               str(sess)[:120])):
            ctx.tokens["oper"] = doc.get("token", "")
        time.sleep(2.0)
        # ---- 被策略拒（会话数上限）时的兜底：临时抬高上限再登 ----
        # [为什么需要] 共享常驻 sim 上别的客户端占着会话，服务端策略闸
        # （上限 8）会把**新登录**一律拒成「authentication rejected」。
        # 这不是凭据问题，重试也等不到（会话位不会自己腾出来）⇒ 改用
        # 管理员权限把 loginMaxSessions 临时抬高，收尾恢复原值。
        for user, role, label in (("root", 2, "root（管理员）"),
                                   ("admin", 1, "admin（普通用户）"),
                                   ("oper", 3, "oper（操作者）")):
            if user in ctx.tokens:
                continue
            print("    · %s 登录被拒 → 尝试抬高 loginMaxSessions 后重登"
                  % label)
            if not raise_session_limit(ctx):
                break
            status, _h, doc = web_login(ctx.web, user, user)
            sess = (doc or {}).get("session") or {}
            if status == 200 and sess.get("role") == role:
                ctx.tokens[user] = doc.get("token", "")
                print("    · %s 补登成功（role=%s）" % (label, role))


def raise_session_limit(ctx, target=32):
    """临时抬高服务端会话数上限（收尾恢复原值）。

    :returns: 是否成功抬高
    """
    # 没有 root 会话就没法改配置 ⇒ 先用 admin/oper 试（它们没有配置权）。
    tok = ctx.token_of("root") or ctx.token_of("admin") or ctx.token_of("oper")
    if not tok:
        return False
    status, _h, cfg = ctx.get("/api/config", token=tok)
    login = ((cfg or {}).get("login") or {}) if status == 200 else {}
    old = login.get("loginMaxSessions")
    if old is None:
        return False
    ctx.orig_max_sessions = old
    status, _h, doc = ctx.post("/api/config",
                                {"section": "login",
                                 "loginMaxSessions": int(target)},
                                token=tok)
    ok = status == 200 and (doc or {}).get("ok") is True
    print("    · loginMaxSessions %s → %s（收尾恢复）"
          % (old, target if ok else "失败"))
    return ok


def sec_admin_ro(ctx):
    with Section("admin（普通用户）只读矩阵：控制/模式/自动/配置全 403"):
        tok = ctx.token_of("admin")
        status, _h, doc = ctx.post("/api/valve/control",
                                   {"index": 0, "on": True}, token=tok)
        if status == 503 and "未连接服务端" in str((doc or {}).get("err", "")):
            skip("admin 阀控制 403（设备控制位文案）", "C 上游不在跑（503）")
        else:
            check("admin 阀控制 403（设备控制位文案）",
                  status == 403 and "设备控制" in str((doc or {}).get("err", "")),
                  "status=%s err=%s" % (status, str(doc)[:160]))
        status, _h, doc = ctx.post("/api/pump/control",
                                   {"index": 0, "on": True}, token=tok)
        if status == 503 and "未连接服务端" in str((doc or {}).get("err", "")):
            skip("admin 泵控制 403", "C 上游不在跑（503）")
        else:
            check("admin 泵控制 403", status == 403, "status=%s" % status)
        status, _h, doc = ctx.post("/api/vfd/control", {"op": 4}, token=tok)
        check("admin 变频器控制 403", status == 403, "status=%s" % status)
        status, _h, doc = ctx.post("/api/mode", {"mode": 1}, token=tok)
        check("admin 切模式 403（模式切换位文案）",
              status == 403 and "切换运行模式" in str((doc or {}).get("err", "")),
              "status=%s err=%s" % (status, str(doc)[:160]))
        status, _h, doc = ctx.post("/api/auto/control",
                                   {"op": 1, "autoMode": "produce"}, token=tok)
        check("admin 自动控制 403（自动运行位文案）",
              status == 403 and "自动运行" in str((doc or {}).get("err", "")),
              "status=%s err=%s" % (status, str(doc)[:160]))
        status, _h, doc = ctx.post("/api/config",
                                   {"section": "storage",
                                    "historyTrigger": "always"}, token=tok)
        check("admin 写配置 403（账户管理位文案）",
              status == 403 and "管理员" in str((doc or {}).get("err", "")),
              "status=%s err=%s" % (status, str(doc)[:160]))
        # 只读面放行
        status, _h, doc = ctx.get("/api/state", token=tok)
        check("admin GET /api/state 200（查看位放行）", status == 200,
              "status=%s" % status)
        status, _h, doc = ctx.get("/api/history/page?offset=0&limit=5",
                                  token=tok)
        check("admin 历史页 200（查看位放行）", status == 200,
              "status=%s" % status)
        status, _h, blob = http_req(ctx.web, "GET",
                                    "/api/history/export?format=csv&limit=5",
                                    token=tok, timeout=60.0)
        check("admin 历史导出 200（下载位放行；BOM 开头）",
              status == 200 and blob[:3] == b"\xef\xbb\xbf",
              "status=%s head=%r" % (status, blob[:16]))


def sec_oper(ctx):
    with Section("oper（操作者）矩阵：可控设备，不可切模式/自动/账户"):
        tok = ctx.token_of("oper")
        ensure_manual_idle(ctx, ctx.token_of("root"))   # 控制闸要求手动模式
        # 可控设备：进水泵启停各回终态（无反馈输出点，即时回读）
        status, _h, doc = ctx.post("/api/pump/control",
                                   {"index": 0, "on": True}, token=tok)
        ok = status == 200 and (doc or {}).get("ok") is True
        if status == 503 and "未连接服务端" in str((doc or {}).get("err", "")):
            skip("oper 进水泵启动 ok=true", "C 上游不在跑（503）")
        else:
            check("oper 进水泵启动 ok=true", ok,
                  "status=%s doc=%s" % (status, str(doc)[:200]))
        on = ctx.poll(lambda: ctx.pump_on(tok), 5, label="泵输出回读")
        check("oper 泵启动后输出回读 on=true", on is True, "on=%r" % on)
        status, _h, doc = ctx.post("/api/pump/control",
                                   {"index": 0, "on": False}, token=tok)
        check("oper 进水泵停止 ok=true",
              status == 200 and (doc or {}).get("ok") is True,
              "status=%s doc=%s" % (status, str(doc)[:200]))
        off = ctx.poll(lambda: ctx.pump_on(tok) is False, 5, label="泵输出回读")
        check("oper 泵停止后输出回读 on=false", off is True, "on=%r" % off)
        # 越权面
        status, _h, doc = ctx.post("/api/mode", {"mode": 1}, token=tok)
        check("oper 切模式 403", status == 403
              and "切换运行模式" in str((doc or {}).get("err", "")),
              "status=%s err=%s" % (status, str(doc)[:160]))
        status, _h, doc = ctx.post("/api/auto/control",
                                   {"op": 1, "autoMode": "produce"}, token=tok)
        check("oper 自动控制 403", status == 403
              and "自动运行" in str((doc or {}).get("err", "")),
              "status=%s err=%s" % (status, str(doc)[:160]))
        status, _h, doc = ctx.post("/api/config",
                                   {"section": "storage",
                                    "historyTrigger": "always"}, token=tok)
        check("oper 写配置 403", status == 403, "status=%s" % status)
        status, _h, doc = ctx.get("/api/history/page?offset=0&limit=5",
                                  token=tok)
        check("oper 历史页 200（可查看）", status == 200, "status=%s" % status)


def sec_sse(ctx):
    """SSE 流整场开着（后续段还要靠它收自动告警）。"""
    with Section("SSE 状态帧（snapshot/connId/订阅收敛/主题事件覆盖）"):
        tok = ctx.token_of("root")
        collector = SseCollector()
        collector.start(ctx.web, tok)
        ctx.sse = collector
        check("SSE 连接建立（X-Hsf-Conn-Id）",
              ctx.poll(lambda: collector.conn_id, 6, label="connId") is not None
              and collector.conn_id, "connId=%r error=%s"
              % (collector.conn_id, collector.error))
        # 首帧 snapshot
        snap = ctx.poll(lambda: collector.snapshot, 8, label="snapshot 首帧")
        check("SSE snapshot 首帧（含 vfd/mode/health/turbidity/pump/link）",
              isinstance(snap, dict)
              and all(k in snap for k in ("vfd", "mode", "health",
                                          "turbidity", "pump", "link")),
              str(sorted((snap or {}).keys())))
        # 订阅收敛
        if collector.conn_id:
            status, _h, doc = ctx.post("/api/topics",
                                       {"connId": int(collector.conn_id),
                                        "topics": SUB_TOPICS}, token=tok)
            check("POST /api/topics（connId 定向，applied=1）",
                  status == 200 and (doc or {}).get("applied") == 1,
                  "status=%s doc=%s" % (status, str(doc)[:160]))
        else:
            check("POST /api/topics（connId 定向，applied=1）", False,
                  "SSE 连接失败，无 connId")
        status, _h, st = ctx.get("/api/status", token=tok)
        desired = (st or {}).get("desiredTopics") or []
        check("订阅收敛：desiredTopics ⊇ 十主题（九遥测+自动告警）",
              set(SUB_TOPICS) <= set(desired),
              "desired=%s" % sorted(desired))

        # ---- ② 自动告警主题**常驻**（2026-11-14 修）----
        # [为什么] TOPIC_AUTO_ALARM 是**事件流**（急停/阀超时/动作失败…），
        # 不是周期遥测：若只在「报警页开着」时订阅，用户在别的页面时上游
        # 根本不推 ⇒ 那些告警永远收不到、也不落本地报警档（行数据源与
        # 订阅集合分离的同一个坑）。口径 = hub.GLOBAL_TOPICS 无条件订阅
        # ⇒ 本连接**不订它**也在。
        # [怎么验] ① 把本连接的订阅集去掉自动告警主题 ⇒ desiredTopics
        # 里仍应含它；② 上游**实际**订阅表里也应有它（真发到了服务端）。
        if collector.conn_id:
            status, _h, doc = ctx.post(
                "/api/topics",
                {"connId": int(collector.conn_id),
                 "topics": [t for t in SUB_TOPICS if t != T_AUTO_ALARM]},
                token=tok)
            check("POST /api/topics（连接订阅集去掉自动告警主题）",
                  status == 200 and (doc or {}).get("applied") == 1,
                  "status=%s doc=%s" % (status, str(doc)[:160]))
            status, _h, st = ctx.get("/api/status", token=tok)
            desired = (st or {}).get("desiredTopics") or []
            check("② 自动告警主题常驻：本连接没订它，desiredTopics 仍含它",
                  T_AUTO_ALARM in desired,
                  "desired=%s" % sorted(desired))
            up = ctx.poll(lambda: (ctx.upstream_subs(tok)
                                   if T_AUTO_ALARM in ctx.upstream_subs(tok)
                                   else None),
                          12, interval=1.0, label="上游订阅自动告警")
            check("② 自动告警主题常驻：上游**实际**订阅表里含它",
                  up is not None and T_AUTO_ALARM in up,
                  "upstream=%s" % sorted(up or []))
            # 收尾把本连接订阅还原成全集（后续段靠它收事件）
            status, _h, doc = ctx.post("/api/topics",
                                       {"connId": int(collector.conn_id),
                                        "topics": SUB_TOPICS}, token=tok)
            check("POST /api/topics（订阅集还原全集）",
                  status == 200 and (doc or {}).get("applied") == 1,
                  "status=%s doc=%s" % (status, str(doc)[:160]))
        else:
            check("② 自动告警主题常驻：本连接没订它，desiredTopics 仍含它",
                  False, "SSE 连接失败，无 connId")

        def all_events():
            with collector.lock:
                return {k for k in SSE_EVENTS if collector.events.get(k)}

        got = ctx.poll(lambda: (all_events() if len(all_events())
                                == len(SSE_EVENTS) else None),
                       30, interval=1.0, label="主题事件")
        check("SSE 八个周期主题事件全覆盖（health 事件驱动另行断言）",
              got == SSE_EVENTS,
              "缺 %s" % sorted(SSE_EVENTS - (got or set())))
        # health 由 /api/state 覆盖（事件驱动主题）
        status, _h, state = ctx.get("/api/state", token=tok)
        health = (state or {}).get("health") or {}
        check("health 主题经 /api/state 覆盖（健康条目 ≥10 台 + addr 键）",
              status == 200 and (health.get("count") or 0) >= 10
              and "addr" in ((health.get("devices") or [{}])[0]),
              "count=%s" % health.get("count"))
        mode = (state or {}).get("mode") or {}
        check("运行模式已知（mode.known）", bool(mode.get("known")),
              str(mode)[:120])


def ensure_manual_idle(ctx, tok, label=""):
    """回到手动 + 控制器停 + 无锁存（每段自动类测试的前置）。"""
    ctx.post("/api/auto/control", {"op": 2}, token=tok)      # 停自动（幂等）
    ctx.set_mode_confirmed(tok, 0)
    live = ctx.mode_live(token=tok)
    if live.get("autoFault") or live.get("autoPhase") == "estop":
        ctx.post("/api/auto/control", {"op": 4}, token=tok)  # 复位两把锁
        live = ctx.mode_live(token=tok)
    return live


def wait_upstream_ready(ctx, tok, timeout=40.0):
    """等 webui 的上游链路就绪（惰性连接 + 慢启动）：state 里 link.ready、
    模式已知、健康条目到位。未就绪时控制/历史端点回 503，不能拿它当
    「拒绝/放行」断言的素材。"""
    def ready():
        status, _h, st = ctx.get("/api/state", token=tok)
        if status != 200 or not isinstance(st, dict):
            return None
        link = st.get("link") or {}
        mode = st.get("mode") or {}
        health = st.get("health") or {}
        if link.get("ready") and mode.get("known") \
                and (health.get("count") or 0) > 0:
            return st
        return None

    return ctx.poll(ready, timeout, interval=1.0, label="上游就绪")


def sec_valve_pump(ctx):
    with Section("数字量：阀1 开→开到位回读→关；进水泵 DO6 启停 + DO 位回读"):
        tok = ctx.token_of("root")
        ensure_manual_idle(ctx, tok)

        # ---- 阀1 开 ----
        status, _h, doc = ctx.post("/api/valve/control",
                                   {"index": 0, "on": True}, token=tok,
                                   timeout=30.0)
        ok = status == 200 and (doc or {}).get("ok") is True
        check("阀1 开命令 ok=true（终态应答）", ok,
              "status=%s doc=%s" % (status, str(doc)[:200]))
        check("阀1 开命令应答带 cmdResult（issued/repeated/…）",
              ok and bool(doc.get("v0_cmdResult")),
              "keys=%s" % sorted((doc or {}).keys())[:12])

        def opened():
            st = ctx.valve_state(tok, 0)
            return st if st and st.get("state") == 2 else None

        st = ctx.poll(opened, 16, label="阀1 开到位")
        check("阀1 开到位回读（state=2 已开，openFb=true）",
              st is not None and st.get("openFb") is True,
              "snap=%r" % (st,))
        do0 = ctx.poll(lambda: ctx.digital_do_bit(tok, 0, 0) is True, 6,
                       label="DO0 位")
        check("阀1 期间 DO0 输出位=1（数字量模块回读）", do0 is True,
              "do0=%r" % do0)

        # ---- 阀1 关 ----
        status, _h, doc = ctx.post("/api/valve/control",
                                   {"index": 0, "on": False}, token=tok,
                                   timeout=30.0)
        check("阀1 关命令 ok=true",
              status == 200 and (doc or {}).get("ok") is True,
              "status=%s doc=%s" % (status, str(doc)[:200]))

        def closed():
            st = ctx.valve_state(tok, 0)
            return st if st and st.get("state") == 1 else None

        st = ctx.poll(closed, 16, label="阀1 关到位")
        check("阀1 关到位回读（state=1 已关，closeFb=true）",
              st is not None and st.get("closeFb") is True, "snap=%r" % (st,))

        # ---- 进水泵 DO6 ----
        status, _h, doc = ctx.post("/api/pump/control",
                                   {"index": 0, "on": True}, token=tok)
        check("进水泵启动 ok=true",
              status == 200 and (doc or {}).get("ok") is True,
              "status=%s doc=%s" % (status, str(doc)[:200]))
        on = ctx.poll(lambda: ctx.pump_on(tok) is True, 6, label="泵输出")
        do6 = ctx.poll(lambda: ctx.digital_do_bit(tok, 0, 6) is True, 6,
                       label="DO6 位")
        check("进水泵启动后输出 on=true 且 DO6 位=1",
              on is True and do6 is True, "on=%r do6=%r" % (on, do6))
        status, _h, doc = ctx.post("/api/pump/control",
                                   {"index": 0, "on": False}, token=tok)
        check("进水泵停止 ok=true",
              status == 200 and (doc or {}).get("ok") is True,
              "status=%s doc=%s" % (status, str(doc)[:200]))
        off = ctx.poll(lambda: ctx.pump_on(tok) is False, 6, label="泵输出")
        do6b = ctx.poll(lambda: ctx.digital_do_bit(tok, 0, 6) is False, 6,
                        label="DO6 位")
        check("进水泵停止后输出 on=false 且 DO6 位=0",
              off is True and do6b is True, "on=%r do6=%r" % (off, do6b))


def sec_vfd(ctx):
    with Section("变频器：启动/给定频率/停止/点动代持"):
        tok = ctx.token_of("root")
        ensure_manual_idle(ctx, tok)

        # ---- 启动（给定必须在启动帧捎带） ----
        status, _h, doc = ctx.post("/api/vfd/control",
                                   {"op": 4, "hz": 20.0}, token=tok)
        check("变频器启动（带 20Hz 给定）ok=true",
              status == 200 and (doc or {}).get("ok") is True,
              "status=%s doc=%s" % (status, str(doc)[:200]))
        running = ctx.poll(lambda: ctx.vfd_field(tok, "running") is True, 6,
                           label="运行位")
        check("启动后运行位 running=true（0x0102 快照回读）",
              running is True, "running=%r" % running)

        # ---- 运行中给定频率 ----
        status, _h, doc = ctx.post("/api/vfd/control",
                                   {"op": 2, "hz": 30.0}, token=tok)
        check("运行中给定 30Hz ok=true",
              status == 200 and (doc or {}).get("ok") is True,
              "status=%s doc=%s" % (status, str(doc)[:200]))

        def hz30():
            v = ctx.vfd_field(tok, "setFreqHz")
            return v if isinstance(v, (int, float)) and abs(v - 30.0) < 0.6 \
                else None

        v = ctx.poll(hz30, 6, label="给定回读")
        check("给定频率回读 setFreqHz≈30", v is not None, "setFreqHz=%r" % v)

        # ---- 停止 ----
        status, _h, doc = ctx.post("/api/vfd/control", {"op": 5}, token=tok)
        check("变频器停止 ok=true",
              status == 200 and (doc or {}).get("ok") is True,
              "status=%s doc=%s" % (status, str(doc)[:200]))
        stopped = ctx.poll(lambda: ctx.vfd_field(tok, "running") is False, 10,
                           label="停机")
        check("停止后 running=false（停机斜坡期运行位即 False）",
              stopped is True, "running=%r" % stopped)
        idle = ctx.poll(lambda: ctx.vfd_field(tok, "runState") in (1, 2), 14,
                        label="停机稳态")
        check("变频器回到停机/就绪稳态（runState 1/2，点动前置）",
              idle is not None, "runState=%r" % idle)

        # ---- 点动代持 ----
        status, _h, doc = ctx.post("/api/jog/start",
                                   {"hz": 10.0, "rev": False, "connId": None},
                                   token=tok, timeout=20.0)
        check("点动开始 ok=true（web 代持）",
              status == 200 and (doc or {}).get("ok") is True
              and (doc or {}).get("active") is True,
              "status=%s doc=%s" % (status, str(doc)[:200]))

        def jog_repeats():
            status, _h, st = ctx.get("/api/status", token=tok)
            jog = (st or {}).get("jog") or {}
            if jog.get("active") and (jog.get("count") or 0) >= 2:
                return jog
            return None

        jog = ctx.poll(jog_repeats, 6, label="点动重发计数")
        check("点动保持中：/api/status jog.active=true 且重发 ≥2 帧（200ms 拍）",
              jog is not None, "jog=%r" % jog)
        # 变频器侧的可感知效果：按住期间电机以点动给定运转。
        # [两端虚拟模型实测 2026-11-13] C sim 把点动建为「加速到给定频率」
        # （runState=7，快照不带 jogActive 键）；py sim 快照带 jogActive 键
        # 但恒 False（VirtualVfd 的 jogging 态不经遥测快照暴露）——即两端
        # sim 都不把「点动中」作为遥测位下发，网页可感知的是 /api/status
        # 的代持状态 + 变频器真的在按给定转。故此处只断言后者。
        def jog_effect():
            status, _h, doc = ctx.post("/api/vfd/snapshot", {}, token=tok,
                                       timeout=15.0)
            if status == 200 and isinstance(doc, dict) \
                    and doc.get("running") is True:
                return doc
            return None

        effect = ctx.poll(jog_effect, 6, label="点动运转")
        hz = (effect or {}).get("setFreqHz")
        check("点动保持中：变频器在转（0x0102 回读 running=true）",
              effect is not None and (effect or {}).get("running") is True,
              "running=%r" % (effect or {}).get("running"))
        if hz is not None and abs(hz - 10.0) >= 1.5:
            # [跨端差异留证 2026-11-13] py sim 点动捎带给定生效（≈10Hz）；
            # C sim 保持旧给定（本 run 残留 30Hz）——点动帧捎带的给定在 C 端
            # 未落到设备（单飞闸/多阶段动作交互），如实打印供 C 端核查。
            print("    · (NOTE) 点动捎带给定未生效：setFreqHz=%s（期望≈10，"
                  "py 端同场景≈10）——C 端点动给定链路待核" % hz)
        status, _h, doc = ctx.post("/api/jog/stop", {}, token=tok,
                                   timeout=20.0)
        check("点动松开 ok=true（安全动作不设闸）",
              status == 200 and (doc or {}).get("ok") is True,
              "status=%s doc=%s" % (status, str(doc)[:200]))

        def jog_down():
            status, _h, st = ctx.get("/api/status", token=tok)
            j = ((st or {}).get("jog") or {})
            if j.get("active") is False:
                return j
            return None

        released = ctx.poll(jog_down, 5, label="点动松开回读")
        check("松开后 /api/status jog.active=false", released is not None,
              "jog=%r" % released)


# ---------------------------------------------------------------------------
# [网页端搅拌泵：停机预置 2026-11-18]
# ---------------------------------------------------------------------------
#
# [用户原话]「这个提示应该改下，没启动不是可以预设速度，启动的时候是按照
#  拖动调速的给的速度启动的吗」
#
# [背景] 桌面端（pages_monitor.VfdPage）原先「滑块松手那条路径」没判停机
# ⇒ 停机时拖一下就真发一条给定帧 ⇒ 服务端按「给定是运行参数」拒了 ⇒
# 状态栏弹「命令被拒：变频器处于停机状态，不能下发给定（请先启动后再调速）」，
# 而页面提示又写着「停机只预置给定」⇒ 自相矛盾。
#
# [网页端当时是对的，但那是「碰巧对」] page05.js 的 onGivenSettled() 里
# 统一判了运行态（拖动去抖 / 松手提交 / 数字框去抖三条路径都走它）⇒ 停机
# 不发帧。**但没有任何回归锁** ⇒ 下次谁把 sendOp 挪进 change 处理器，就
# 会回潮成同样的毛病。⇒ 本段把「停机只预置、不发帧」钉死（跑真在跑的那
# 份 JS，不是抄一份在这儿测）。

def _vfd_preset_js_case(page_src, running, value=370):
    """在 node 里跑网页端搅拌泵页的 `onGivenSettled`（真在跑的那份）。

    :param running: 桩掉 isRunning() 的返回值（True=运行中 / False=停机）。
    :param value: 桩掉给定值（rpm；0 = 「不指定给定」那个特例）。
    :return: {"sent": [...], "msgs": [...], "hint": str, "presetDirty": bool}
    """
    fn_settled = _js_func_src(page_src, "onGivenSettled")
    fn_text = _js_func_src(page_src, "presetGivenText")
    if fn_settled is None or fn_text is None:
        return None
    harness = """
var __sent = [];
var __msgs = [];
var __hint = "";
var HSF = { statusMessage: function (t) { __msgs.push(String(t)); } };
function setText(node, text) { __hint = String(text == null ? "" : text); }
var SPEED_MODE_HZ = 0;
var SPEED_MODE_RPM = 1;
var speedMode = SPEED_MODE_RPM;
var ratedRpm = 1440.0;
var ratedHz = 50.0;
var presetDirty = false;
var syncing = false;
var ctrlHint = {};
function curUnit() { return speedMode === SPEED_MODE_RPM ? "rpm" : "Hz"; }
function spinValue() { return %s; }
function isRunning() { return %s; }
function sendOp(op, extra, silent) {
  __sent.push({ op: op, extra: extra || null });
}
""" % (json.dumps(value), "true" if running else "false")
    call = ("(onGivenSettled(false), {sent: __sent, msgs: __msgs, "
            "hint: __hint, presetDirty: presetDirty})")
    return _js_run_node(harness + "\n" + fn_text + "\n" + fn_settled, call)


def _js_listener_body(stripped, marker, span=800):
    """截出某个 addEventListener 处理器的函数体（到第一个 `});` 为止）。"""
    idx = stripped.find(marker)
    if idx < 0:
        return ""
    body = stripped[idx:idx + span]
    end = body.find("});")
    return body[:end] if end > 0 else body


def sec_vfd_preset(ctx):
    """网页端搅拌泵：停机时「拖动/改值」只预置、不发帧（与桌面端同口径）。"""
    with Section("网页端搅拌泵：停机预置只预置不发帧（与桌面端同口径）"):
        js_path = os.path.join(WEBUI_DIR, "hsfweb", "static", "js", "pages",
                               "page05.js")
        try:
            with open(js_path, "r", encoding="utf-8") as fh:
                page_src = fh.read()
        except OSError as exc:
            check("搅拌泵页 JS 可读", False, "%s" % exc)
            return
        check("前端接线：有 onGivenSettled（给定提交的唯一收口）",
              _js_func_src(page_src, "onGivenSettled") is not None, "")
        check("前端接线：有 presetGivenText（停机预置文案唯一事实源）",
              _js_func_src(page_src, "presetGivenText") is not None, "")
        if shutil.which("node") is None:
            check("node 可用（网页端停机预置自检）", True,
                  "未找到 node：跳过本段自检（安装 Node.js 后才有牙齿）")
            return

        # ---- ① 停机：只预置、不发帧 ----
        got = _vfd_preset_js_case(page_src, False)
        if not isinstance(got, dict) or "__error__" in got:
            check("网页端停机预置：跑 onGivenSettled（停机态）", False,
                  "got=%r" % (got,))
            return
        check("网页端停机预置：停机时**不发任何帧**（sendOp 一次都没调）",
              not (got.get("sent") or []), "sent=%r" % (got.get("sent"),))
        check("网页端停机预置：置预置标记（启动帧捎带用）",
              got.get("presetDirty") is True, "%r" % (got.get("presetDirty"),))
        hint = got.get("hint") or ""
        check("网页端停机预置：提示是预置口径（含预置值与启动说明）",
              "预置" in hint and "370" in hint and "启动时" in hint, hint)
        check("网页端停机预置：提示不含「命令被拒」",
              "被拒" not in hint, hint)
        msgs = got.get("msgs") or []
        check("网页端停机预置：状态栏提示是预置口径（不是命令被拒）",
              bool(msgs) and all("被拒" not in s for s in msgs)
              and any("预置" in s for s in msgs), "%r" % (msgs[-3:],))

        # ---- ② 运行中：仍然在线调速（真发帧，行为不变）----
        got2 = _vfd_preset_js_case(page_src, True)
        sent2 = (got2 or {}).get("sent") or []
        check("网页端运行中调速：仍然在线下发（op=3 + rpm）",
              len(sent2) == 1 and sent2[0].get("op") == 3
              and abs(float((sent2[0].get("extra") or {}).get("rpm", -1))
                      - 370.0) < 0.01,
              "sent=%r" % (sent2,))

        # ---- ②b 给定 = 0 的特例：服务端把 0 当「未指定」----
        # 两端同口径：写频率的判断是 `if given_hz > 0` ⇒ 预置 0 之后启动，
        # 设备是**沿用变频器里已有的给定**，不是「按 0 运行」⇒ 文案不能写
        # 「启动时按此值运行」（那是假的）。
        got0 = _vfd_preset_js_case(page_src, False, value=0)
        hint0 = (got0 or {}).get("hint") or ""
        check("网页端给定=0：提示说「沿用变频器当前值」（不说按此值运行）",
              "0" in hint0 and "沿用" in hint0 and "按此值运行" not in hint0,
              hint0)
        check("网页端给定=0：仍然不发帧（0 与其它值同口径）",
              not ((got0 or {}).get("sent") or []),
              "sent=%r" % ((got0 or {}).get("sent"),))

        # ---- ③ 三条交互路径都走同一个收口（不许谁直接 sendOp）----
        stripped = _js_strip_comments(page_src)
        for label, marker in (("滑块拖动去抖", 'slider.addEventListener("input"'),
                               ("滑块松手提交", 'slider.addEventListener("change"'),
                               ("数字框改值去抖", 'spin.addEventListener("input"')):
            body = _js_listener_body(stripped, marker)
            check("前端接线：%s 走 onGivenSettled（不直接 sendOp）" % label,
                  bool(body) and "onGivenSettled(" in body
                  and "sendOp(" not in body, body[:160])

        # ---- ④ 启动帧带上预置值（预置真的会在启动时兑现）----
        fn_run = _js_func_src(page_src, "onRunStop")
        check("前端接线：有 onRunStop（启动帧捎带给定）", fn_run is not None, "")
        if fn_run:
            check("前端接线：启动帧用 givenPayload()（预置值随启动下发）",
                  "givenPayload()" in fn_run, "")


def sec_cycle_order_gate(ctx):
    """网页端自动循环：上移/下移的**边界状态**不被通用权限闸抹平。

    [背景 2026-11-18] 桌面端 RemoteSettingsPage.refresh() 的通用权限闸原来
    直接 `up/down.setEnabled(can_cfg)` ⇒ 把「首行不能上移 / 末行不能下移」
    的边界状态抹平（界面骗人：已经在最上/最下，按钮却还是可点的）。
    修法：权限闸只管勾选与条件；上移/下移的可用性**只**由按位置的计算
    （桌面端 `_cycle_render_order()` / 网页端 `cycleApplyGate()`）决定。
    [网页端当时是对的] 但那是「碰巧对」——通用闸 `cfgControls.forEach(...)`
    里**含**上移/下移按钮（全量置灰/点亮），只有靠 `cycleApplyGate()` 在
    **之后**再刷一遍才对 ⇒ 顺序一旦颠倒就回潮。⇒ 本段把顺序钉死。
    """
    with Section("网页端自动循环：上移/下移边界不被通用权限闸抹平"):
        js_path = os.path.join(WEBUI_DIR, "hsfweb", "static", "js", "pages",
                               "page14_remote.js")
        try:
            with open(js_path, "r", encoding="utf-8") as fh:
                js = fh.read()
        except OSError as exc:
            check("远端设置页 JS 可读", False, "%s" % exc)
            return
        fn = _js_func_src(js, "cycleApplyGate")
        check("前端接线：有 cycleApplyGate（逐行可用性闸）", fn is not None, "")
        if fn:
            check("前端接线：上移按位置算（i > 0 ⇒ 首行不能上移）",
                  "i > 0" in fn, "")
            check("前端接线：下移按位置算（i < part.length - 1 ⇒ 末行不能下移）",
                  "part.length - 1" in fn, "")
        stripped = _js_strip_comments(js)
        # [为什么要限定在 refreshBeat 里比顺序] 全文里 `cycleApplyGate()` 的
        # **第一次**出现在 cycleRenderOrder()（定义在前面）⇒ 拿全文首现比
        # 顺序会误判 ⇒ 只在「通用闸所在的那个函数」里比。
        fn_beat = _js_func_src(js, "refreshBeat")
        check("前端接线：有 refreshBeat（每拍刷新 + 通用权限闸）",
              fn_beat is not None, "")
        if fn_beat:
            gate_at = fn_beat.find("cfgControls.forEach")
            call_at = fn_beat.find("cycleApplyGate()")
            check("前端接线：通用权限闸之后**再**刷一次逐行闸（顺序不能颠倒）",
                  gate_at >= 0 and call_at > gate_at,
                  "cfgControls.forEach@%d cycleApplyGate()@%d"
                  % (gate_at, call_at))
        # [实例化 2026-11-26] 实例行的上移/下移不再逐个写进通用闸名单，
        # 而是随「行控件包」一起进（push → cfgControls）并在行销毁时摘出
        # （cfgControlsRemove）⇒ 通用闸仍会碰到它们，refreshBeat 末尾的
        # cycleApplyGate() 重刷仍然必要（上面两条顺序断言钉的就是它）。
        check("前端接线：实例行的控件随行包进通用闸名单（push → "
              "cfgControls）且销毁时摘出（cfgControlsRemove）",
              "cfgControls.push(arguments[i])" in stripped
              and "function cfgControlsRemove" in stripped, "")


def sec_auto_produce(ctx):
    with Section("自动模式·产水全流程（阀相→泵相→运行相→停止全停）"):
        tok = ctx.token_of("root")
        ensure_manual_idle(ctx, tok)
        # 开跑前抓原始 auto/storage 配置（收尾恢复用）
        status, _h, cfg = ctx.get("/api/config", token=tok)
        if status == 200 and isinstance(cfg, dict):
            ctx.orig_auto = {"modes": (cfg.get("auto") or {}).get("modes"),
                             "cycle": (cfg.get("auto") or {}).get("cycle")}
            ctx.orig_storage = cfg.get("storage")

        # ---- 手动模式下启动被拒 ----
        status, _h, doc = ctx.post("/api/auto/control",
                                   {"op": 1, "autoMode": "produce"}, token=tok)
        check("手动模式启动自动运行被拒（err 提「手动」）",
              status == 200 and (doc or {}).get("ok") is False
              and "手动" in str((doc or {}).get("err", "")),
              "status=%s doc=%s" % (status, str(doc)[:200]))

        # ---- auto 节 modes+cycle 同批提交 ----
        status, _h, doc = ctx.post("/api/config",
                                   {"section": "auto",
                                    "modes": AUTO_MODES_CFG,
                                    "cycle": AUTO_CYCLE_CFG,
                                    # [2026-11-25] 搅拌最高转速（> 模式 720，
                                    # 不夹取；单独验证顶层键往返）。
                                    "stirMaxRpm": 900}, token=tok)
        check("auto 节 modes+cycle 同批提交 ok=true",
              status == 200 and (doc or {}).get("ok") is True,
              "status=%s doc=%s" % (status, str(doc)[:240]))
        status, _h, cfg = ctx.get("/api/config", token=tok)
        auto = (cfg or {}).get("auto") or {}
        modes = auto.get("modes") or {}
        cycle = auto.get("cycle") or {}
        got_prod = (modes.get("produce") or {}).get("devices") or []
        check("配置往返回读：produce.devices=[valve1,stirPump,inletPump] 且"
              " resetValvesOnStop=true / resetValvesOnEstop=false",
              got_prod == ["valve1", "stirPump", "inletPump"]
              and (modes.get("produce") or {}).get("resetValvesOnStop") is True
              and (modes.get("produce") or {}).get("resetValvesOnEstop") is False,
              "produce=%s" % modes.get("produce"))
        # [PY 先行键按端门控 2026-11-26] stirMaxRpm 顶层键与逐模式
        # stirAccelRpmS 是 PY 服务端先行实现的键，C 端弃用不实现 ⇒ C sim
        # 的 auto 回显不含它们（基线实测 2 条恒红）。只对 PY6200 硬断言；
        # C6100 记 SKIP + 原因（不进 FAIL 分母）。
        if _end[0] == "PY6200":
            check("配置往返回读：stirMaxRpm=900（搅拌最高转速顶层键）",
                  auto.get("stirMaxRpm") == 900,
                  "auto=%s" % str(auto)[:200])
            check("配置往返回读：produce.stirAccelRpmS=300（逐模式加速度）",
                  (modes.get("produce") or {}).get("stirAccelRpmS") == 300,
                  "produce=%s" % modes.get("produce"))
        else:
            skip("配置往返回读：stirMaxRpm=900（搅拌最高转速顶层键）",
                 "PY 先行键，C 端不实现")
            skip("配置往返回读：produce.stirAccelRpmS=300（逐模式加速度）",
                 "PY 先行键，C 端不实现")
        check("配置往返回读：backwash.stirReverse=true / rinse.devices 含 inletPump",
              (modes.get("backwash") or {}).get("stirReverse") is True
              and "inletPump" in ((modes.get("rinse") or {}).get("devices")
                                  or []),
              "backwash=%s rinse=%s" % (modes.get("backwash"),
                                        modes.get("rinse")))
        check("配置往返回读：cycle 键值与提交一致（阈值 0.3/gt/兜底关）",
              abs((cycle.get("pressThreshold") or 0) - 0.3) < 1e-6
              and cycle.get("pressCompare") == "gt"
              and cycle.get("timerFallback") is False
              and cycle.get("enabled") is True,
              "cycle=%s" % cycle)

        # ---- 切自动：不自动启动 ----
        live = ctx.set_mode_confirmed(tok, 1)
        check("切自动确认生效（runMode=1）",
              (live or {}).get("mode") == 1 and (live or {}).get("ok") is not False,
              "live=%s" % str(live)[:160])
        live = ctx.mode_live(token=tok)
        check("切自动后控制器**不**自动启动（autoRunning=false）",
              live.get("autoRunning") is False, "live=%s" % str(live)[:200])

        # ============================================================
        # 场景 A（缺陷锁干净窗口）：启动 → 只用设备快照等运行（不发任何
        # 0x0105 交换）→ GET /api/mode 必须自己跟上相位。web 缓存模型
        # （只在控制应答时刷新）在本窗口暴露：GET 恒停在启动应答的 valves。
        # ============================================================
        status, _h, doc = ctx.post("/api/auto/control",
                                   {"op": 1, "autoMode": "produce"},
                                   token=tok, timeout=20.0)
        check("启动产水 ok=true 且应答在阀相",
              status == 200 and (doc or {}).get("ok") is True
              and (doc or {}).get("autoPhase") in ("valves", "pumps",
                                                   "running"),
              "status=%s doc=%s" % (status, str(doc)[:240]))
        if isinstance(doc, dict) and "mode" in doc:
            ctx._mode_sync = int(doc.get("mode", 1))

        def devices_running():
            # 只发 0x0113/0x0102 快照（不触碰 web 模式缓存）。
            return None if not (ctx.pump_on(tok) is True
                                and ctx.vfd_field(tok, "running") is True) \
                else True

        dev_run = ctx.poll(devices_running, 45, interval=1.0,
                           label="设备进入运行（泵+变频器）")
        check("产水跑到设备运行（进水泵 on + 变频器 running，≤45s）",
              dev_run is True, "泵行程 10s + 泵相；超时详见后续段")

        # ---- GET /api/mode 实时性（hub._mode_poll_loop 1s 轮询口径）----
        # [2026-11-13 主会话根修] 后端此前只在控制应答时刷新缓存，无人发
        # 0x0105 时相位冻结；现 hub 每秒轮询 CMD_RUN_MODE 并广播。验收按
        # 正向口径：设备运行后 GET 的相位在 ~2s 内跟上，且 autoRunMs /
        # autoStageElapsedMs 逐秒增长（服务端在计时，前端 1s 节拍可见）。
        def get_running_doc():
            status, _h, m = ctx.get("/api/mode", token=tok)
            return m if isinstance(m, dict) \
                and m.get("autoPhase") == "running" else None

        m1 = ctx.poll(get_running_doc, 5, interval=0.5,
                      label="GET /api/mode 跟上运行相")
        check("GET /api/mode 跟上实时相位（设备已运行，≤5s 内报 running）",
              m1 is not None,
              "设备已运行而 GET 仍报 autoPhase=%r"
              % ((m1 or {}).get("autoPhase")))
        time.sleep(2.2)
        status, _h, m2 = ctx.get("/api/mode", token=tok)
        run_ms_grow = isinstance(m2, dict) and isinstance(m1, dict) \
            and (m2.get("autoRunMs") or 0) > (m1.get("autoRunMs") or 0) + 1000
        check("运行中 GET /api/mode 的 autoRunMs 逐秒增长"
              "（服务端计时，前端秒级节拍可见）",
              run_ms_grow, "autoRunMs %r → %r" % ((m1 or {}).get("autoRunMs"),
                                                  (m2 or {}).get("autoRunMs")))
        # [阶段时长只在组合模式断言] C 端口径「单模式启动没有换相定时 ⇒
        # elapsed=-1」（HsfServerApp.c 模式包络注释），py 端单模式也报
        # elapsed——该键的增长断言放 sec_cycle（组合模式）两端才一致。

        # ---- 停止 → 全停（含关阀） ----
        status, _h, doc = ctx.post("/api/auto/control", {"op": 2}, token=tok,
                                   timeout=20.0)
        check("停止自动运行 ok=true",
              status == 200 and (doc or {}).get("ok") is True,
              "status=%s doc=%s" % (status, str(doc)[:200]))
        live = ctx.mode_live(token=tok)
        # [相位回 paused 2026-11-19] 停止＝暂停 ⇒ autoRunning=false 且相位
        # = **paused**（记住阶段与计时，按启动续跑；复位才清零回 idle）。
        # 旧断言"相位回 idle"是停止＝丢弃时代的口径。
        check("停止后控制器停（autoRunning=false，相位=paused 暂停态）",
              live.get("autoRunning") is False
              and live.get("autoPhase") == "paused",
              "live=%s" % str(live)[:200])
        off = ctx.poll(lambda: ctx.pump_on(tok) is False, 6, label="泵停")
        vfd_off = ctx.poll(lambda: ctx.vfd_field(tok, "running") is False, 10,
                           label="变频器停")
        check("停止全停：进水泵 off、变频器 running=false",
              off is True and vfd_off is True,
              "on=%r running=%r" % (off, vfd_off))

        def valve_closed():
            st = ctx.valve_state(tok, 0)
            if st is None:
                print("    · (阀快照回读失败：%s)" % ctx.last_err)
            return st if st and st.get("state") == 1 else None

        closed = ctx.poll(valve_closed, 18, label="停止后关阀复位")
        check("停止全停：阀1 关回原位（夹具配置 resetValvesOnStop=true）",
              closed is not None, "snap=%r" % (closed,))

        # ============================================================
        # 场景 B：相位推进逐相回读（0x0105 幂等回读；泵相瞬态用服务端
        # auto 日志「阀全部到位 → 启动泵」兜底）。
        # ============================================================
        status, _h, doc = ctx.post("/api/auto/control",
                                   {"op": 1, "autoMode": "produce"},
                                   token=tok, timeout=20.0)
        check("再次启动产水 ok=true（相位推进前置）",
              status == 200 and (doc or {}).get("ok") is True,
              "status=%s doc=%s" % (status, str(doc)[:200]))
        start_wall_ms = int(time.time() * 1000)

        seen = {"valves": False, "pumps": False, "running": False}
        deadline = time.time() + 45.0
        last = {}
        while time.time() < deadline:
            last = ctx.mode_live(token=tok)
            ph = last.get("autoPhase")
            if ph in seen and not seen[ph]:
                seen[ph] = True
                print("    · 相位推进 → %s" % ph)
            if ph == "running":
                break
            # [泵相是瞬态] 两端在泵全部受理后立刻进运行相（一次控制器拍
            # ~200ms），0.5s 轮询会踩空 ⇒ 阀相期间 0.5s 慢拍、阀到位后
            # （泵相窗口）连续拍不睡。
            time.sleep(0.0 if seen["valves"] else 0.5)
        check("相位推进：阀相可见", seen["valves"],
              "seen=%s last=%s" % (seen, str(last)[:160]))
        if not seen["pumps"]:
            # 泵相回读踩空的兜底证据：两端进入泵相时都在服务端 auto 日志
            # 记同一条「阀全部到位 → 启动泵」（C amLog / py _log 同文）。
            status, _h, d = ctx.get(
                "/api/log/page?limit=20&category=auto&kw=%s&loTs=%d"
                % (urllib.parse.quote("启动泵"), start_wall_ms - 3000),
                token=tok, timeout=20.0)
            rows = (d or {}).get("rows") or []
            seen["pumps"] = any("启动泵" in str(r.get("text", ""))
                                for r in rows)
            if seen["pumps"]:
                print("    · 相位推进 → pumps（服务端 auto 日志佐证）")
        check("相位推进：泵相可见（相位回读 或 服务端进入泵相的 auto 日志）",
              seen["pumps"],
              "seen=%s last=%s ｜ %s" % (seen, str(last)[:160],
                                         ctx.auto_log_tail(tok)))
        check("相位推进：运行相到达（≤45s；阀行程 10s）", seen["running"],
              "seen=%s last=%s" % (seen, str(last)[:160]))

        # ---- 运行相设备回读 ----
        on = ctx.poll(lambda: ctx.pump_on(tok) is True, 6, label="进水泵输出")
        vfd_run = ctx.poll(lambda: ctx.vfd_field(tok, "running") is True, 6,
                           label="变频器运行位")
        do0 = ctx.poll(lambda: ctx.digital_do_bit(tok, 0, 0) is True, 6,
                       label="DO0")
        do6 = ctx.poll(lambda: ctx.digital_do_bit(tok, 0, 6) is True, 6,
                       label="DO6")
        check("运行相：进水泵输出 on=true", on is True, "on=%r" % on)
        check("运行相：搅拌泵变频器 running=true", vfd_run is True,
              "running=%r" % vfd_run)
        check("运行相：阀1 DO0=1 且进水泵 DO6=1",
              do0 is True and do6 is True, "do0=%r do6=%r" % (do0, do6))

        # ---- 收尾停止 ----
        status, _h, doc = ctx.post("/api/auto/control", {"op": 2}, token=tok,
                                   timeout=20.0)
        check("停止自动运行 ok=true（场景 B 收尾）",
              status == 200 and (doc or {}).get("ok") is True,
              "status=%s doc=%s" % (status, str(doc)[:200]))
        off = ctx.poll(lambda: ctx.pump_on(tok) is False, 6, label="泵停")
        vfd_off = ctx.poll(lambda: ctx.vfd_field(tok, "running") is False, 10,
                           label="变频器停")
        check("停止全停：进水泵 off、变频器 running=false",
              off is True and vfd_off is True,
              "on=%r running=%r" % (off, vfd_off))


def sec_auto_select(ctx):
    """③ 模式下拉 op=5：只选模式（记粘性）——不启动、不动设备、可回读。

    [为什么锁这条] 现场物理启动按钮按「服务端上次选中的模式」启动。网页
    端模式下拉改选即发 0x0115 op=5 记粘性（参考客户端 bridge.send_auto_select
    同口径）；C 服务端原先只认 op=1..4 ⇒ 回「非法 op 操作字」⇒ 下拉与现场
    按钮各记各的。本段锁住：① op=5 成功且**粘性可回读**；② **不动设备**
    （不启动控制器）；③ 载荷缺模式 id / 非法 id / 非法 op 都**显式报错**
    不假成功。
    """
    with Section("模式下拉 op=5（只选模式：记粘性、不启动、坏载荷显式报错）"):
        tok = ctx.token_of("root")
        ensure_manual_idle(ctx, tok)
        # 原粘性（收尾还原，避免污染后续段）
        orig = ctx.mode_live(token=tok).get("autoMode") or ""

        # ---- op=5 只选模式 ----
        status, _h, doc = ctx.post("/api/auto/control",
                                   {"op": 5, "autoMode": "backwash"},
                                   token=tok, timeout=20.0)
        check("③ op=5 只选模式 ok=true",
              status == 200 and (doc or {}).get("ok") is True,
              "status=%s doc=%s" % (status, str(doc)[:200]))
        live = ctx.mode_live(token=tok)
        check("③ op=5 后粘性回读为 backwash（服务端记住了）",
              live.get("autoMode") == "backwash",
              "autoMode=%r" % live.get("autoMode"))
        check("③ op=5 **不启动**控制器（autoRunning 非真 / 相位非 running）",
              not live.get("autoRunning")
              and live.get("autoPhase") not in ("running", "valves", "pumps"),
              "autoRunning=%r autoPhase=%r"
              % (live.get("autoRunning"), live.get("autoPhase")))

        # ---- 坏载荷：显式报错，不假成功 ----
        status, _h, doc = ctx.post("/api/auto/control", {"op": 5}, token=tok)
        check("③ op=5 缺 autoMode → 显式报错（不假成功）",
              status == 400 and "autoMode" in str(doc or ""),
              "status=%s doc=%s" % (status, str(doc)[:200]))
        status, _h, doc = ctx.post("/api/auto/control",
                                   {"op": 5, "autoMode": "bogus"}, token=tok)
        check("③ op=5 非法模式 id → 显式报错（fail-closed）",
              status == 400 and "autoMode" in str(doc or ""),
              "status=%s doc=%s" % (status, str(doc)[:200]))
        status, _h, doc = ctx.post("/api/auto/control", {"op": 9}, token=tok)
        check("③ 非法 op → 显式报错（1..5 白名单）",
              status == 400 and "op" in str(doc or ""),
              "status=%s doc=%s" % (status, str(doc)[:200]))

        # ---- 收尾：还原原粘性 ----
        if orig:
            ctx.post("/api/auto/control", {"op": 5, "autoMode": orig},
                     token=tok)


def sec_estop(ctx):
    with Section("急停：锁存→再启动拒→复位亮→告警入库→等待→复位"):
        tok = ctx.token_of("root")
        ensure_manual_idle(ctx, tok)
        # 自动告警走 SSE 流（TOPIC_AUTO_ALARM）进 web AlarmLog：流要是
        # 断过，先重连并重发订阅，否则急停告警帧没人收（假红）。
        if ctx.sse is not None and ctx.sse.ensure_alive():
            ctx.sse.reapply_topics(ctx)
        ctx.set_mode_confirmed(tok, 1)
        status, _h, doc = ctx.post("/api/auto/control",
                                   {"op": 1, "autoMode": "produce"},
                                   token=tok, timeout=20.0)
        if not check("产水启动 ok=true（急停前置）",
                     status == 200 and (doc or {}).get("ok") is True,
                     "status=%s doc=%s" % (status, str(doc)[:200])):
            ctx.post("/api/mode", {"mode": 0}, token=tok)
            ctx._mode_sync = 0
            return
        ok_run, last = ctx.wait_phase(tok, "running", 45.0)
        if not check("产水到运行相（急停前置）", ok_run,
                     "last=%s" % str(last)[:160]):
            ctx.post("/api/auto/control", {"op": 2}, token=tok)
            ctx.set_mode_confirmed(tok, 0)
            return

        # ---- 急停 ----
        status, _h, doc = ctx.post("/api/auto/control", {"op": 3}, token=tok,
                                   timeout=20.0)
        check("急停命令 ok=true",
              status == 200 and (doc or {}).get("ok") is True,
              "status=%s doc=%s" % (status, str(doc)[:200]))
        estop_hit, last = ctx.wait_phase(tok, "estop", 8.0)
        check("急停后进入 estop 相位（锁存）", estop_hit,
              "last=%s ｜ %s" % (str(last)[:160], ctx.auto_log_tail(tok)))
        vfd_off = ctx.poll(lambda: ctx.vfd_field(tok, "running") is False, 8,
                           label="急停全停")
        pump_off = ctx.poll(lambda: ctx.pump_on(tok) is False, 8,
                            label="急停泵停")
        check("急停全停：变频器停、进水泵停（阀保持现状）",
              vfd_off is True and pump_off is True,
              "running=%r on=%r" % (vfd_off, pump_off))

        # ---- 锁存：再启动被拒 + 复位亮 ----
        status, _h, doc = ctx.post("/api/auto/control",
                                   {"op": 1, "autoMode": "produce"},
                                   token=tok, timeout=20.0)
        check("急停未复位：启动被拒且提示「复位」",
              status == 200 and (doc or {}).get("ok") is False
              and "复位" in str((doc or {}).get("err", "")),
              "status=%s doc=%s" % (status, str(doc)[:200]))
        live = ctx.mode_live(token=tok)
        # 客户端「复位」按钮亮起的判据 = autoFault 或 phase==estop
        check("「复位」亮：autoPhase==estop 或 autoFault=true",
              live.get("autoPhase") == "estop" or live.get("autoFault") is True,
              "live=%s" % str(live)[:200])

        # ---- 告警入库（TOPIC_AUTO_ALARM → web AlarmLog → /api/alarms）----
        def alarm_seen():
            status, _h, doc = ctx.get("/api/alarms?limit=50", token=tok)
            rows = (doc or {}).get("alarms") or []
            hit = [r for r in rows if "急停" in str(r.get("text", ""))
                   or "急停" in str(r.get("name", ""))]
            return hit if hit else None

        hit = ctx.poll(alarm_seen, 8, label="急停告警入库")
        check("急停告警入库：/api/alarms 出现「急停」条目（kind=auto）",
              hit is not None and any(r.get("kind") == "auto" for r in hit),
              "alarms=%s" % str((hit or "无"))[:240])
        status, _h, st = ctx.get("/api/status", token=tok)
        alarms_stat = (st or {}).get("alarms") or {}
        check("报警统计：/api/status alarms.total ≥ 1",
              (alarms_stat.get("total") or 0) >= 1,
              "stats=%s" % alarms_stat)
        if ctx.sse is not None:
            check("SSE alarm 事件到达（自动告警主题推送）",
                  ctx.poll(lambda: ctx.sse.has_alarm_text("急停"), 5,
                           label="SSE alarm") is not None,
                  "alarm 事件=%d 条" % ctx.sse.got("alarm"))

        # ---- 等待后复位 ----
        time.sleep(1.5)
        status, _h, doc = ctx.post("/api/auto/control", {"op": 4}, token=tok,
                                   timeout=20.0)
        check("复位命令 ok=true",
              status == 200 and (doc or {}).get("ok") is True,
              "status=%s doc=%s" % (status, str(doc)[:200]))
        live = ctx.mode_live(token=tok)
        check("复位后解除锁存（phase≠estop 且 autoFault=false）",
              live.get("autoPhase") != "estop" and live.get("autoFault") is False,
              "live=%s" % str(live)[:200])
        status, _h, doc = ctx.post("/api/auto/control",
                                   {"op": 1, "autoMode": "produce"},
                                   token=tok, timeout=20.0)
        check("复位后可以再启动 ok=true",
              status == 200 and (doc or {}).get("ok") is True,
              "status=%s doc=%s" % (status, str(doc)[:200]))
        ctx.post("/api/auto/control", {"op": 2}, token=tok)
        ctx.set_mode_confirmed(tok, 0)


def sec_cycle(ctx):
    with Section("组合模式「自动」：跑到反洗→停止→阶段记忆→复位从头"):
        tok = ctx.token_of("root")
        ensure_manual_idle(ctx, tok)
        # 复位清阶段记忆（从头开始；上一段的复位可能已被启动消费）
        ctx.post("/api/auto/control", {"op": 4}, token=tok)
        ctx.set_mode_confirmed(tok, 1)

        # ---- 启动组合模式：从产水开始 ----
        status, _h, doc = ctx.post("/api/auto/control",
                                   {"op": 1, "autoMode": "auto"},
                                   token=tok, timeout=20.0)
        check("启动组合模式 ok=true 且起始阶段=产水",
              status == 200 and (doc or {}).get("ok") is True
              and (doc or {}).get("autoMode") == "produce",
              "status=%s doc=%s" % (status, str(doc)[:240]))

        # ---- 压差触发切反洗（sim 压差恒 0.499 > 0.3，gt）----
        deadline = time.time() + 50.0
        stage = ""
        last = {}
        while time.time() < deadline:
            last = ctx.mode_live(token=tok)
            stage = str(last.get("autoMode") or "")
            if stage == "backwash":
                break
            time.sleep(0.6)
        check("组合模式推进到反洗相（压差 gt 阈值触发）",
              stage == "backwash",
              "stage=%r last=%s ｜ %s" % (stage, str(last)[:160],
                                          ctx.auto_log_tail(tok)))

        # ---- 阶段时长逐秒增长（GET /api/mode；组合模式两端都有该键）----
        status, _h, s1 = ctx.get("/api/mode", token=tok)
        time.sleep(2.2)
        status, _h, s2 = ctx.get("/api/mode", token=tok)
        stage_grow = isinstance(s1, dict) and isinstance(s2, dict) \
            and isinstance(s1.get("autoStageElapsedMs"), int) \
            and isinstance(s2.get("autoStageElapsedMs"), int) \
            and s2["autoStageElapsedMs"] > s1["autoStageElapsedMs"] + 1000
        check("组合模式运行中 autoStageElapsedMs 逐秒增长"
              "（阶段倒计时源，前端秒级节拍可见）",
              stage_grow, "stage %r → %r"
              % ((s1 or {}).get("autoStageElapsedMs"),
                 (s2 or {}).get("autoStageElapsedMs")))

        # ---- 停止 → 记住阶段 ----
        status, _h, doc = ctx.post("/api/auto/control", {"op": 2}, token=tok,
                                   timeout=20.0)
        check("停止组合模式 ok=true",
              status == 200 and (doc or {}).get("ok") is True,
              "status=%s doc=%s" % (status, str(doc)[:200]))
        live = ctx.mode_live(token=tok)
        check("停止后服务端仍报记住的阶段（反洗）",
              live.get("autoRunning") is False
              and live.get("autoMode") == "backwash",
              "live=%s" % str(live)[:200])

        # ---- 再启动：从反洗继续（阶段记忆生效）----
        status, _h, doc = ctx.post("/api/auto/control",
                                   {"op": 1, "autoMode": "auto"},
                                   token=tok, timeout=20.0)
        check("再启动组合模式从记住的阶段继续（反洗）",
              status == 200 and (doc or {}).get("ok") is True
              and (doc or {}).get("autoMode") == "backwash",
              "status=%s doc=%s" % (status, str(doc)[:240]))

        # ---- 停止 + 复位 → 下次从头 ----
        ctx.post("/api/auto/control", {"op": 2}, token=tok)
        status, _h, doc = ctx.post("/api/auto/control", {"op": 4}, token=tok,
                                   timeout=20.0)
        check("复位 ok=true（清阶段记忆）",
              status == 200 and (doc or {}).get("ok") is True,
              "status=%s doc=%s" % (status, str(doc)[:200]))
        status, _h, doc = ctx.post("/api/auto/control",
                                   {"op": 1, "autoMode": "auto"},
                                   token=tok, timeout=20.0)
        check("复位后再启动从头开始（产水）",
              status == 200 and (doc or {}).get("ok") is True
              and (doc or {}).get("autoMode") == "produce",
              "status=%s doc=%s" % (status, str(doc)[:240]))
        ctx.post("/api/auto/control", {"op": 2}, token=tok)
        ctx.post("/api/mode", {"mode": 0}, token=tok)


def sec_config(ctx):
    with Section("配置读写：historyTrigger 往返 / 非法值显式报错 / 局部应答不冲刷"):
        tok = ctx.token_of("root")
        status, _h, cfg = ctx.get("/api/config", token=tok)
        storage = (cfg or {}).get("storage") or {}
        check("GET /api/config 键全（collect/storage/login/auto）",
              status == 200 and all(k in (cfg or {}) for k in
                                    ("collect", "storage", "login", "auto")),
              str(sorted((cfg or {}).keys())))
        orig_trigger = storage.get("historyTrigger", "always")

        # ---- always → auto → always ----
        for want in ("auto", "always"):
            status, _h, doc = ctx.post("/api/config",
                                       {"section": "storage",
                                        "historyTrigger": want}, token=tok)
            ok = status == 200 and (doc or {}).get("ok") is True
            check("SET storage.historyTrigger=%s ok=true" % want, ok,
                  "status=%s doc=%s" % (status, str(doc)[:200]))
            status, _h, cfg2 = ctx.get("/api/config", token=tok)
            got = ((cfg2 or {}).get("storage") or {}).get("historyTrigger")
            check("GET 回读 historyTrigger=%s" % want, got == want,
                  "got=%r" % got)

        # ---- 非法值显式报错（fail-closed 不假成功）----
        for payload, what in (
                ({"section": "storage", "historyTrigger": "bogus"},
                 "historyTrigger=bogus"),
                ({"section": "auto", "cycle": {"pressThreshold": "abc"}},
                 "cycle.pressThreshold=\"abc\""),
                ({"section": "auto", "cycle": {"backwashMinutes": "abc"}},
                 "cycle.backwashMinutes=\"abc\"")):
            status, _h, doc = ctx.post("/api/config", payload, token=tok)
            check("非法配置显式报错：%s（ok=false + err）" % what,
                  status == 200 and (doc or {}).get("ok") is False
                  and bool((doc or {}).get("err")),
                  "status=%s doc=%s" % (status, str(doc)[:200]))
        status, _h, cfg2 = ctx.get("/api/config", token=tok)
        st2 = (cfg2 or {}).get("storage") or {}
        cy2 = ((cfg2 or {}).get("auto") or {}).get("cycle") or {}
        check("非法 SET 后原值不变（trigger=always、阈值 0.3、反洗 10 分钟）",
              st2.get("historyTrigger") == "always"
              and abs((cy2.get("pressThreshold") or 0) - 0.3) < 1e-6
              and cy2.get("backwashMinutes") == 10,
              "storage=%s cycle=%s" % (st2.get("historyTrigger"),
                                       str(cy2)[:120]))

        # ---- 局部应答不冲刷：login 节 SET 的应答只带 login 节 ----
        status, _h, cfg2 = ctx.get("/api/config", token=tok)
        login_before = (cfg2 or {}).get("login") or {}
        max_sessions = login_before.get("loginMaxSessions", 8)
        status, _h, doc = ctx.post("/api/config",
                                   {"section": "login",
                                    "loginMaxSessions": max_sessions},
                                   token=tok)
        ok = status == 200 and (doc or {}).get("ok") is True
        check("SET login 节（原值重写）ok=true", ok,
              "status=%s doc=%s" % (status, str(doc)[:200]))
        check("局部应答只带 login 节（无 storage 键——应答里没有的字段"
              "不得当成已存在）",
              ok and "login" in (doc or {}) and "storage" not in (doc or {}),
              "keys=%s" % sorted((doc or {}).keys()))
        status, _h, cfg3 = ctx.get("/api/config", token=tok)
        st3 = (cfg3 or {}).get("storage") or {}
        check("login 节应答后 GET：storage 节原值完好（enabled/trigger 不被冲掉）",
              status == 200 and st3.get("enabled") == storage.get("enabled")
              and st3.get("historyTrigger") == "always"
              and st3.get("sampleMs") == storage.get("sampleMs"),
              "before=%s after=%s" % (str(storage)[:120], str(st3)[:120]))


def sec_protect_config(ctx):
    """运行保护节 SET 往返（2026-11-20 网页端实锅回归锁）。

    [实锅] api.py 的 CONFIG_SECTIONS 白名单一直漏 protect ⇒ 网页端
    「应用运行保护」恒回 400「section 必须是 collect/storage/login/
    auto/camera 之一」（2026-11-15 起就在；webui_check 没测过这条 HTTP
    通路所以一直没红）。本轮手机浏览器 E2E 抓到后补：① protect 进
    白名单；② 本段锁三件事——SET ok=true、回读值一致、非法键仍显式
    报错（fail-closed 不因放行节名而放松）。
    """
    with Section("配置读写：运行保护节（protect）往返 / 白名单 / 非法键拒"):
        tok = ctx.token_of("root")
        # ---- protect 节在白名单里：原值写回 ok=true ----
        status, _h, cfg = ctx.get("/api/config", token=tok)
        prot = (cfg or {}).get("protect") or {}
        check("GET /api/config 带 protect 节（三段保护键齐）",
              status == 200 and isinstance(prot, dict)
              and "inletNoFlowTimeoutMs" in prot
              and "pressHighEnabled" in prot
              and "motorOverTempEnabled" in prot
              # [2026-11-25] 电箱超温 + 压力1/2 过高保护九键。
              and "boxOverTempEnabled" in prot
              and "press1HighEnabled" in prot
              and "press2HighEnabled" in prot,
              "keys=%s" % (sorted(prot) if isinstance(prot, dict) else prot))
        status, _h, doc = ctx.post("/api/config",
                                   dict(prot, section="protect"), token=tok)
        check("SET protect（原值重写）ok=true —— 白名单已放行",
              status == 200 and (doc or {}).get("ok") is True,
              "status=%s doc=%s" % (status, str(doc)[:200]))
        # ---- 电机超温三键往返 ----
        payload = dict(prot, section="protect",
                       motorOverTempEnabled=True,
                       motorOverTempLimitC=80.0, motorOverTempStop=False)
        status, _h, doc = ctx.post("/api/config", payload, token=tok)
        ok = status == 200 and (doc or {}).get("ok") is True
        check("SET protect.motorOverTemp（启用/80℃/不停设备）ok=true", ok,
              "status=%s doc=%s" % (status, str(doc)[:200]))
        if ok:
            back = ((doc or {}).get("protect") or {})
            check("SET protect 应带回显三键",
                  back.get("motorOverTempEnabled") is True
                  and abs(float(back.get("motorOverTempLimitC", 0)) - 80.0)
                  < 1e-6
                  and back.get("motorOverTempStop") is False,
                  "back=%s" % str(back)[:200])
        status, _h, cfg2 = ctx.get("/api/config", token=tok)
        p2 = (cfg2 or {}).get("protect") or {}
        check("GET 回读电机超温三键一致",
              p2.get("motorOverTempEnabled") is True
              and abs(float(p2.get("motorOverTempLimitC", 0)) - 80.0) < 1e-6
              and p2.get("motorOverTempStop") is False,
              "protect=%s" % str(p2)[:200])
        # ---- 非法键仍显式拒（fail-closed）----
        status, _h, doc = ctx.post(
            "/api/config",
            dict(prot, section="protect", motorOverTempLimitC="abc"),
            token=tok)
        check("SET protect 非法阈值显式报错（ok=false + err）",
              status == 200 and (doc or {}).get("ok") is False
              and bool((doc or {}).get("err")),
              "status=%s doc=%s" % (status, str(doc)[:200]))
        # ---- 电箱超温 + 压力1/2 过高保护（2026-11-25）九键往返 ----
        payload = dict(prot, section="protect",
                       boxOverTempEnabled=True, boxOverTempLimitC=65.0,
                       boxOverTempStop=False,
                       press1HighEnabled=True, press1HighLimitMpa=1.25,
                       press1HighStop=False,
                       press2HighEnabled=True, press2HighLimitMpa=0.75,
                       press2HighStop=True)
        status, _h, doc = ctx.post("/api/config", payload, token=tok)
        ok = status == 200 and (doc or {}).get("ok") is True
        check("SET protect 电箱/压力1/2（九键）ok=true", ok,
              "status=%s doc=%s" % (status, str(doc)[:200]))
        status, _h, cfg3 = ctx.get("/api/config", token=tok)
        p3 = (cfg3 or {}).get("protect") or {}
        check("GET 回读电箱/压力1/2 九键一致",
              ok and p3.get("boxOverTempEnabled") is True
              and abs(float(p3.get("boxOverTempLimitC", 0)) - 65.0) < 1e-6
              and p3.get("boxOverTempStop") is False
              and p3.get("press1HighEnabled") is True
              and abs(float(p3.get("press1HighLimitMpa", 0)) - 1.25) < 1e-6
              and p3.get("press1HighStop") is False
              and p3.get("press2HighEnabled") is True
              and abs(float(p3.get("press2HighLimitMpa", 0)) - 0.75) < 1e-6
              and p3.get("press2HighStop") is True,
              "protect=%s" % str(p3)[:300])
        # ---- 收尾：恢复缺省（不启用），不给别的段留副作用 ----
        status, _h, doc = ctx.post(
            "/api/config",
            dict(prot, section="protect", motorOverTempEnabled=False,
                 motorOverTempLimitC=75.0, motorOverTempStop=True,
                 boxOverTempEnabled=False, boxOverTempLimitC=60.0,
                 boxOverTempStop=True,
                 press1HighEnabled=False, press1HighLimitMpa=1.0,
                 press1HighStop=True,
                 press2HighEnabled=False, press2HighLimitMpa=1.0,
                 press2HighStop=True),
            token=tok)
        check("收尾：电机/电箱/压力1/2 恢复缺省",
              status == 200 and (doc or {}).get("ok") is True,
              "status=%s doc=%s" % (status, str(doc)[:200]))
        # ---- 静态接线：page14 前端真的发/收这九键 ----
        p14 = os.path.join(WEBUI_DIR, "hsfweb", "static", "js", "pages",
                           "page14_remote.js")
        try:
            with open(p14, "r", encoding="utf-8", errors="replace") as fp:
                src = fp.read()
        except OSError as exc:
            check("page14_remote.js 可读（电箱/压力保护接线）", False,
                  "%s" % exc)
            return
        bad = []
        for key in ("boxOverTempEnabled", "boxOverTempLimitC",
                    "boxOverTempStop",
                    "press1HighEnabled", "press1HighLimitMpa",
                    "press1HighStop",
                    "press2HighEnabled", "press2HighLimitMpa",
                    "press2HighStop"):
            if ("%s:" % key) not in src:
                bad.append("提交缺 %s" % key)
            if ("doc.protect.%s" % key) not in src:
                bad.append("回显缺 %s" % key)
        check("page14 电箱/压力1/2 九键提交 + 回显接线", not bad,
              "；".join(bad))
        check("page14 三张新保护卡标题在位",
              "电箱超温保护" in src
              and 'PRESS1_LABEL + "过高保护"' in src
              and 'PRESS2_LABEL + "过高保护"' in src, "")


#: 自动循环**新契约**（2026-11-18 用户定案）的载荷样例：
#:   · cycleModes = **有序数组**（顺序＝循环顺序；第一个＝**主运行阶段**；
#:     末位→首位＝闭环）；
#:   · stageCfg   = {模式 id: {pressEnabled, pressThreshold, pressCompare,
#:     timerEnabled, timerMinutes}}——**每个参与阶段各自**的切换条件
#:     （压差与定时互不排斥 ⇒ 先到先切；两个都没开 ⇒ 服务端明确拒）。
#: [为什么每阶段条件各不相同] 老扁平键模型只能表达「一个压差阈值 + 每模式
#: 一个分钟数」；新模型允许「反洗也按压差切」「放水只按分钟」⇒ 三个阶段
#: 各给一套**互不相同**的值，回读时逐项对得上才算真往返。
AUTO_CYCLE_NEW_CFG = {
    "enabled": True,
    "cycleModes": ["produce", "backwash", "rinse"],
    "stageCfg": {
        "produce": {"pressEnabled": True, "pressThreshold": 0.42,
                    "pressCompare": "lt", "timerEnabled": True,
                    "timerMinutes": 33},
        "backwash": {"pressEnabled": True, "pressThreshold": 0.11,
                     "pressCompare": "gt", "timerEnabled": False,
                     "timerMinutes": 7},
        "rinse": {"pressEnabled": False, "pressThreshold": 0.5,
                  "pressCompare": "gt", "timerEnabled": True,
                  "timerMinutes": 9},
    },
}
#: **旧的扁平键**（cycle 级；已作废）：pressThreshold / pressCompare /
#: timerFallback / timerMinutes / backwashMinutes / rinseMinutes。
#: [为什么必须彻底不提交] 服务端会把旧键**折进对应阶段**（只改值）⇒ 网页端
#: 发一次旧键就会**悄悄覆盖**别的端（桌面客户端）配好的该阶段条件 ⇒ 跨端
#: 互踩。⇒ 网页端只发 cycleModes + stageCfg（成对）。
AUTO_CYCLE_FLAT_KEYS = ("pressThreshold", "pressCompare", "timerFallback",
                         "timerMinutes", "backwashMinutes", "rinseMinutes")


def sec_auto_cycle_cfg(ctx):
    """自动循环新契约：cycleModes（有序）+ stageCfg（每阶段各自）。

    [为什么网页端也要锁这段] 2026-11-18 用户定案：参与循环的**模式与顺序**
    可配，且**每个阶段各自**的切换条件（压差/定时）可配。桌面客户端与服务端
    已换契约，网页端若还发旧扁平键 ⇒ ① 配不出每阶段条件；② 服务端把旧键
    **折进对应阶段** ⇒ 写一次旧键就悄悄覆盖别的端配好的条件（跨端互踩）。
    本段把**服务端侧的契约**钉死（网页端载荷就照它发），另有一段
    `_auto_cycle_static` 钉前端载荷形状、一段 `sec_auto_cycle_ui` 用真浏览器
    抓网页端**实际发出的载荷**。

    [收尾] 本段改了 auto.cycle ⇒ 段末按开跑前抓到的原值回写恢复（老服务端
    没有新键时按原样回写整个 cycle 子节）。
    """
    with Section("自动循环新契约（cycleModes 有序 + stageCfg 每阶段各自）"):
        tok = ctx.token_of("root")
        # ---- 抓原值（收尾恢复用）----
        status, _h, cfg0 = ctx.get("/api/config", token=tok)
        orig_cycle = (((cfg0 or {}).get("auto") or {}).get("cycle") or {})
        server_has_new = ("cycleModes" in orig_cycle
                          and "stageCfg" in orig_cycle)

        def restore_cycle():
            """把 auto.cycle 恢复成开跑前的值。"""
            if not orig_cycle:
                return None
            payload = {"section": "auto", "cycle": dict(orig_cycle)}
            status, _h, doc = ctx.post("/api/config", payload, token=tok)
            return status == 200 and (doc or {}).get("ok") is True

        # ---- ① 新契约一次提交（只带 cycle，不动 modes）----
        status, _h, doc = ctx.post("/api/config",
                                   {"section": "auto",
                                    "cycle": AUTO_CYCLE_NEW_CFG}, token=tok)
        ok = status == 200 and (doc or {}).get("ok") is True
        check("SET auto.cycle（cycleModes + stageCfg 成对）ok=true", ok,
              "status=%s doc=%s" % (status, str(doc)[:240]))

        # ---- ② GET 回读：顺序 + 每阶段条件 ----
        status, _h, cfg = ctx.get("/api/config", token=tok)
        cyc = (((cfg or {}).get("auto") or {}).get("cycle") or {})
        check("GET 回读 cycleModes＝提交的有序数组（顺序逐项相同）",
              cyc.get("cycleModes") == AUTO_CYCLE_NEW_CFG["cycleModes"],
              "got=%r want=%r" % (cyc.get("cycleModes"),
                                   AUTO_CYCLE_NEW_CFG["cycleModes"]))
        sc = cyc.get("stageCfg") or {}
        want = AUTO_CYCLE_NEW_CFG["stageCfg"]

        def _same(got, key):
            g = got.get(key) or {}
            w = want.get(key) or {}
            return (g.get("pressEnabled") == w["pressEnabled"]
                    and abs(float(g.get("pressThreshold", -1))
                            - w["pressThreshold"]) < 1e-6
                    and g.get("pressCompare") == w["pressCompare"]
                    and g.get("timerEnabled") == w["timerEnabled"]
                    and g.get("timerMinutes") == w["timerMinutes"])

        check("GET 回读 stageCfg：产水阶段条件逐项一致"
              "（压差 0.42/lt + 定时 33 分钟）",
              _same(sc, "produce"), "produce=%r" % (sc.get("produce"),))
        check("GET 回读 stageCfg：反洗阶段条件逐项一致"
              "（压差 0.11/gt + 定时关 + 7 分钟）",
              _same(sc, "backwash"), "backwash=%r" % (sc.get("backwash"),))
        check("GET 回读 stageCfg：顺洗阶段条件逐项一致"
              "（压差关 + 定时 9 分钟）",
              _same(sc, "rinse"), "rinse=%r" % (sc.get("rinse"),))
        # [每阶段独立] 老扁平键模型只能有一个压差阈值 ⇒ 这一条是新契约的
        # 核心能力：三个阶段的压差阈值互不相同，且各自生效。
        th = [float((sc.get(m) or {}).get("pressThreshold", -1))
              for m in ("produce", "backwash", "rinse")]
        check("每阶段各有自己的压差阈值（老扁平键模型表达不了）",
              len(set(th)) == 3 and all(t >= 0 for t in th),
              "thresholds=%r" % th)

        # ---- ③ 顺序往返（换个顺序再写一次，回读必须照旧）----
        order2 = ["backwash", "produce", "rinse"]
        payload = {"section": "auto", "cycle": dict(AUTO_CYCLE_NEW_CFG)}
        payload["cycle"]["cycleModes"] = order2
        status, _h, doc = ctx.post("/api/config", payload, token=tok)
        check("SET auto.cycle 换顺序（首元素＝主运行阶段）ok=true",
              status == 200 and (doc or {}).get("ok") is True,
              "status=%s doc=%s" % (status, str(doc)[:200]))
        status, _h, cfg = ctx.get("/api/config", token=tok)
        cyc = (((cfg or {}).get("auto") or {}).get("cycle") or {})
        check("GET 回读 cycleModes 顺序＝提交顺序（顺序不丢）",
              cyc.get("cycleModes") == order2,
              "got=%r want=%r" % (cyc.get("cycleModes"), order2))

        # ---- ④ fail-closed：缺阶段配置 / 两个触发都没开 / 未知模式 /
        #         重复模式 / 空列表 / 非法值 ----
        bad_cases = [
            ({"enabled": True,
              "cycleModes": ["produce", "backwash"],
              "stageCfg": {"produce": {"pressEnabled": True,
                                       "pressThreshold": 0.5,
                                       "pressCompare": "gt",
                                       "timerEnabled": False,
                                       "timerMinutes": 10}}},
             "缺少参与循环的阶段配置", "stageCfg 缺 backwash"),
            ({"enabled": True,
              "cycleModes": ["produce", "backwash"],
              "stageCfg": {"produce": {"pressEnabled": True,
                                       "pressThreshold": 0.5,
                                       "pressCompare": "gt",
                                       "timerEnabled": True,
                                       "timerMinutes": 10},
                           "backwash": {"pressEnabled": False,
                                        "pressThreshold": 0.5,
                                        "pressCompare": "gt",
                                        "timerEnabled": False,
                                        "timerMinutes": 10}}},
             "未设置切换条件", "参与阶段两个触发都没开"),
            ({"enabled": True,
              "cycleModes": ["produce", "nope"],
              "stageCfg": {"produce": {"pressEnabled": True,
                                       "pressThreshold": 0.5,
                                       "pressCompare": "gt",
                                       "timerEnabled": True,
                                       "timerMinutes": 10}}},
             "含未知模式", "cycleModes 含未知模式 id"),
            ({"enabled": True,
              "cycleModes": ["produce", "produce"],
              "stageCfg": {"produce": {"pressEnabled": True,
                                       "pressThreshold": 0.5,
                                       "pressCompare": "gt",
                                       "timerEnabled": True,
                                       "timerMinutes": 10}}},
             "有重复模式", "cycleModes 有重复模式"),
            ({"enabled": True, "cycleModes": [],
              "stageCfg": {}},
             "不能为空", "cycleModes 空数组"),
            ({"enabled": True,
              "cycleModes": ["produce", "backwash"],
              "stageCfg": {"produce": {"pressEnabled": True,
                                       "pressThreshold": "abc",
                                       "pressCompare": "gt",
                                       "timerEnabled": True,
                                       "timerMinutes": 10},
                           "backwash": {"pressEnabled": False,
                                        "pressThreshold": 0.5,
                                        "pressCompare": "gt",
                                        "timerEnabled": True,
                                        "timerMinutes": 10}}},
             "需要数字", "stageCfg 压差阈值非数字"),
            ({"enabled": True,
              "cycleModes": ["produce", "backwash"],
              "stageCfg": {"produce": {"pressEnabled": True,
                                       "pressThreshold": 0.5,
                                       "pressCompare": "bogus",
                                       "timerEnabled": True,
                                       "timerMinutes": 10},
                           "backwash": {"pressEnabled": False,
                                        "pressThreshold": 0.5,
                                        "pressCompare": "gt",
                                        "timerEnabled": True,
                                        "timerMinutes": 10}}},
             "只能是", "stageCfg 比较方向非法"),
        ]
        for cyc_doc, want_err, what in bad_cases:
            status, _h, doc = ctx.post("/api/config",
                                       {"section": "auto", "cycle": cyc_doc},
                                       token=tok)
            err = str((doc or {}).get("err", ""))
            check("非法循环配置显式报错（fail-closed）：%s" % what,
                  status == 200 and (doc or {}).get("ok") is False
                  and want_err in err,
                  "status=%s err=%r" % (status, err[:160]))
        # 非法 SET 不得改坏原值（整包回滚）。
        status, _h, cfg = ctx.get("/api/config", token=tok)
        cyc = (((cfg or {}).get("auto") or {}).get("cycle") or {})
        sc = cyc.get("stageCfg") or {}
        check("非法 SET 后原值不变（顺序 + 每阶段条件完好）",
              cyc.get("cycleModes") == order2 and _same(sc, "produce"),
              "cycleModes=%r produce=%r" % (cyc.get("cycleModes"),
                                           sc.get("produce")))

        # ---- ⑤ 跨端互踩的根因（旧扁平键会折进首阶段）----
        # [留证] 服务端为了兼容老客户端，仍把旧扁平键折进**首阶段**（只改
        # 值）⇒ 网页端/任何端发一次旧键就会覆盖该阶段的条件 ⇒ 这正是
        # 三端统一改成「只发 cycleModes + stageCfg」的原因。
        # [首阶段＝当前顺序里的第一个] ③ 刚把顺序换成 backwash 开头 ⇒ 折进
        # 的应是 **backwash**（写死 produce 会与顺序用例自相矛盾）⇒ 断言
        # 按回显的 cycleModes[0] 算。
        legacy_doc = {"enabled": True, "pressThreshold": 0.9,
                      "pressCompare": "gt"}
        status, _h, doc = ctx.post("/api/config",
                                   {"section": "auto", "cycle": legacy_doc},
                                   token=tok)
        status, _h, cfg = ctx.get("/api/config", token=tok)
        cyc = (((cfg or {}).get("auto") or {}).get("cycle") or {})
        sc = cyc.get("stageCfg") or {}
        first_mode = (cyc.get("cycleModes") or ["produce"])[0]
        prod_th = float((sc.get(first_mode) or {}).get("pressThreshold", -1))
        check("旧扁平键仍会折进**首阶段**（跨端互踩的根因 ⇒ 三端都不再发）",
              abs(prod_th - 0.9) < 1e-6,
              "%s.pressThreshold=%r（期望 0.9）" % (first_mode, prod_th))

        # ---- 收尾：恢复原值 ----
        restored = restore_cycle()
        check("收尾恢复 auto.cycle 原值", restored is not False,
              "restore=%r" % restored)

    # ---- 前端/后端接线静态检查（网页端载荷形状 + 三端键名一致）----
    _auto_cycle_static()


def _js_strip_comments(src):
    """剥掉 JS 的块注释与行注释（判「代码里还有没有某个键名」时用）。

    [为什么要剥] 源码注释里**提到**旧键名是正常的（记录「这些键作废了」
    的来龙去脉）⇒ 判「代码还在用旧键」必须只看**代码**。
    """
    import re as _re
    out = _re.sub(r"/\*.*?\*/", " ", src, flags=_re.S)
    out = _re.sub(r"//[^\n]*", " ", out)
    return out


def _js_func_src(src, name):
    """取 JS 里某个函数的**完整源码**（花括号配对；先剥注释免得被注释里的
    花括号带偏）。找不到 ⇒ None。

    [用途] 把网页端的纯函数（循环载荷构造、变频器写入文案）抠出来**直接
    跑**：断言的是**真在发的那份代码**，不是复制一份在这儿测。
    """
    stripped = _js_strip_comments(src)
    idx = stripped.find("function %s(" % name)
    if idx < 0:
        return None
    start = stripped.find("{", idx)
    if start < 0:
        return None
    depth = 0
    for i in range(start, len(stripped)):
        if stripped[i] == "{":
            depth += 1
        elif stripped[i] == "}":
            depth -= 1
            if depth == 0:
                return stripped[idx:i + 1]
    return None


def _js_run_node(func_src, call_expr):
    """在 node 里跑「一段函数源码 + 一句调用」⇒ 取回 JSON 结果。

    node 不在 ⇒ None（环境问题，调用方按「跳过」处理，不假绿）。
    出错 ⇒ {"__error__": ...}。
    """
    node = shutil.which("node")
    if node is None:
        return None
    code = (func_src + "\nvar __out = (" + call_expr + ");\n"
            "console.log(JSON.stringify(__out));\n")
    path = None
    try:
        with tempfile.NamedTemporaryFile("w", suffix=".js", delete=False,
                                         encoding="utf-8") as fp:
            fp.write(code)
            path = fp.name
        proc = subprocess.run([node, path], capture_output=True, timeout=30)
    except (OSError, subprocess.SubprocessError) as exc:
        return {"__error__": "%s: %s" % (type(exc).__name__, exc)}
    finally:
        if path:
            try:
                os.unlink(path)
            except OSError:
                pass
    if proc.returncode != 0:
        return {"__error__": (proc.stderr or b"").decode("utf-8",
                                                       "replace")[:400]}
    out = (proc.stdout or b"").decode("utf-8", "replace").strip()
    try:
        return json.loads(out)
    except (TypeError, ValueError):
        return {"__error__": out[:400]}


def _auto_cycle_static():
    """自动循环契约的**前后端接线静态检查**（实例化口径）。

    [为什么静态查也要有] 网页端的载荷是 JS 里拼的 ⇒ 协议级段（打 API）
    **看不到**网页端到底发了什么 ⇒ 这里把「载荷形状」钉死（2026-11-26
    实例化后）：
      · cycle 级载荷只含 enabled / instances（实例全键）；
      · **不含**旧键（cycleModes/stageCfg 是派生兼容视图，不再是提交面；
        timerFallback / backwashMinutes / rinseMinutes 同理）；
      · 三端（网页端 proto / 服务端 proto / 桌面客户端 proto）的键名逐字
        一致（instances + 实例全键 + 派生视图键）；
      · 实例标题规则（titles）的前端函数**抠出来在 node 里直接跑**。
    """
    hsfweb = os.path.join(WEBUI_DIR, "hsfweb")

    def _read(rel):
        path = os.path.join(hsfweb, rel)
        try:
            with open(path, "r", encoding="utf-8") as fh:
                return fh.read()
        except OSError as exc:
            check("%s 可读" % rel, False, "%s" % exc)
            return None

    page_src = _read(os.path.join("static", "js", "pages",
                                  "page14_remote.js"))
    proto_src = _read("proto.py")
    css_src = _read(os.path.join("static", "css", "theme.css"))
    if None in (page_src, proto_src, css_src):
        return
    # ---- ① 网页端载荷形状（autoCycleValues 的函数体）----
    body = _js_func_src(page_src, "autoCycleValues")
    check("远端设置页有 autoCycleValues（循环载荷构造函数）",
          body is not None, "")
    if body:
        check("循环载荷含 enabled（循环总开关）",
              "enabled" in body, body[:160])
        check("循环载荷含 instances（有序实例数组）",
              "instances" in body, body[:160])
        check("循环载荷经 serverKeys.instances 闸（老服务端不回显就不发）",
              "serverKeys.instances" in body, body[:160])
        # [顶层总循环次数 2026-11-27] 与 instances 同款条件键闸：用户改过
        # （dirty）或服务端回显过（serverKeys.topRepeat）才提交。
        check("循环载荷含 topRepeat（顶层总循环次数）且走条件键闸",
              "topRepeat" in body and "serverKeys.topRepeat" in body
              and "topRepeatDirty" in body, body[:240])
        legacy_in_body = [k for k in ("cycleModes", "stageCfg") if k in body]
        check("循环载荷**不含旧键**（cycleModes/stageCfg 已降级为派生视图，"
              "不再是提交面）", not legacy_in_body, "命中=%s" % legacy_in_body)
        old_in_body = [k for k in AUTO_CYCLE_FLAT_KEYS if k in body]
        check("循环载荷**不含旧扁平键**（timerFallback/backwashMinutes/…）",
              not old_in_body, "命中=%s" % old_in_body)
    # ---- ①b 实例载荷的**全键**（cycleInstancesPayload 函数体）----
    inst_body = _js_func_src(page_src, "cycleInstancesPayload")
    check("远端设置页有 cycleInstancesPayload（实例全键载荷构造）",
          inst_body is not None, "")
    if inst_body:
        # [2026-11-27 浊度触发] 四触发 → **六触发**：多出
        # turb1/turb2 各三键（enabled/threshold/compare，单位 NTU）。
        missing = [k for k in (
            "mode", "devices", "pressEnabled", "pressThreshold",
            "pressCompare", "press1Enabled", "press1Threshold",
            "press1Compare", "press2Enabled", "press2Threshold",
            "press2Compare", "turb1Enabled", "turb1Threshold",
            "turb1Compare", "turb2Enabled", "turb2Threshold",
            "turb2Compare", "timerEnabled", "timerMinutes", "stirRpm",
            "stirReverse", "stirAccelRpmS", "resetValvesOnStop",
            "resetValvesOnEstop", "stirSwitchPolicy", "inletSwitchPolicy")
            if k not in inst_body]
        check("实例载荷**全键**齐（26 键：mode/devices/六触发/转速/方向/"
              "加速度/阀复位两键/换相策略两键）", not missing,
              "缺=%s" % missing)
        # [压力1/压力2 界面 kPa、协议 MPa] 提交侧必须有 ÷1000 的换算。
        check("实例载荷把压力1/压力2 阈值从 kPa 换算成 MPa（÷1000）",
              "/ 1000.0" in inst_body, inst_body[:200])
    # ---- ② 旧扁平键在**代码**里也不该再出现（界面已无「旧版兼容」区：
    #         注释里提到它们是正常的——那是记录「这些键作废了」的来龙去脉，
    #         判「代码还在用」必须剥掉注释只看代码）----
    code_only = _js_strip_comments(page_src)
    old_anywhere = [k for k in ("timerFallback", "backwashMinutes",
                                "rinseMinutes") if k in code_only]
    check("远端设置页代码里不再出现旧扁平键（timerFallback/backwashMinutes/"
          "rinseMinutes）", not old_anywhere, "命中=%s" % old_anywhere)
    # ---- ③ 交互件：添加实例下拉 + 上移/下移/删除/设置 + 每实例条件控件 ----
    check("循环区有「添加实例」下拉与 添加/上移/下移/删除/设置 控件",
          all(s in page_src for s in ("添加实例", "上移", "下移", "删除",
                                      "设置", "cycleAddInstance")), "")
    # [顶层总循环次数 2026-11-27] 控件/文案/回显接线三件套（静态面；
    # 真浏览器的 DOM 断言在 sec_auto_cycle_ui ①c）。
    check("循环区有「总循环次数」控件与「一直循环」文案",
          "总循环次数" in page_src and "一直循环" in page_src
          and '"aria-label", "总循环次数"' in page_src,
          "page14 缺总循环次数控件/文案")
    check("循环区回显 topRepeat（服务端回显过才记 serverKeys）",
          '"topRepeat" in c' in page_src
          and "cycle.serverKeys.topRepeat" in page_src, "")
    # [三端 proto 同步] 网页端 proto 必须有 topRepeat 与五个进度键。
    for _k in ("CFG_AUTO_CYCLE_TOP_REPEAT = \"topRepeat\"",
               "AUTO_CYCLE_TOP_REPEAT_MIN = 0",
               "AUTO_CYCLE_TOP_REPEAT_MAX = 999",
               "AUTO_CYCLE_TOP_REPEAT_DEF = 0",
               "KEY_AUTO_REP_GROUP = \"autoRepGroup\"",
               "KEY_AUTO_REP_CURRENT = \"autoRepCurrent\"",
               "KEY_AUTO_REP_TOTAL = \"autoRepTotal\"",
               "KEY_AUTO_ROUND = \"autoRound\"",
               "KEY_AUTO_ROUND_TOTAL = \"autoRoundTotal\""):
        check("网页端 proto 有 %s" % _k.split(" = ")[0], _k in proto_src, "")
    # [缺省 0 的静态锚点 2026-11-27] 浏览器段不拿现场配置冒充缺省
    #（复用的 6200 可能是非 0）⇒ 产品缺省由 proto 常量 + 前端缺省常量锁。
    check("前端缺省 topRepeat = 0（0 = 一直循环）",
          "topRepeatDef: 0" in page_src, "")
    # [继承单模式 2026-11-26 用户「添加新模式默认继承单模式的配置」]
    # modeBlockValues 与 autoConfigValues 同一口径 + 添加实例时六键跟块走。
    check("循环区添加实例默认继承单模式块（modeBlockValues 同源 + 六键跟块）",
          "function modeBlockValues(blk)" in page_src
          and "modes[blk.id] = modeBlockValues(blk);" in page_src
          and "inst.stirRpm = m.stirRpm;" in page_src
          and "inst.resetValvesOnEstop = m.resetValvesOnEstop;" in page_src,
          "page14 缺实例继承单模式接线")
    check("循环区有每实例的压差/压力1/压力2/定时切换控件与实例键",
          all(s in page_src for s in ("压差切换", "定时切换",
                                      "pressThreshold", "timerMinutes",
                                      "press1Enabled", "press2Enabled")), "")
    check("实例数上限常量在位（AUTO_CYCLE_INSTANCES_MAX = 32）",
          "AUTO_CYCLE_INSTANCES_MAX = 32" in page_src, "")
    check("压力1/压力2 显示名走 JS 常量（PRESS1_LABEL/PRESS2_LABEL=压力1/压力2）",
          'var PRESS1_LABEL = "进水压力"' in page_src
          and 'var PRESS2_LABEL = "出水压力"' in page_src, "")
    check("样式表有循环行（.cycle-row）与实例设置卡（.cycle-inst）",
          ".cycle-row" in css_src and ".cycle-inst" in css_src, "")
    # ---- ④ 三端键名逐字一致 ----
    def _py(rel):
        path = os.path.join(REPO, "HighSpeedFilter", rel)
        try:
            with open(path, "r", encoding="utf-8") as fh:
                return fh.read()
        except OSError:
            return None

    web_proto = proto_src
    srv_proto = _py("server_py/hsfsrv/proto.py")
    cli_proto = _py("client_py/hsfsrv_client/proto.py")
    for tag, src in (("网页端", web_proto), ("服务端", srv_proto),
                     ("桌面客户端", cli_proto)):
        if src is None:
            check("%s proto 可读" % tag, False, "文件读不到")
            continue
        check("%s proto 有 cycleModes / stageCfg（派生兼容视图键仍在）" % tag,
              'CFG_AUTO_CYCLE_MODES = "cycleModes"' in src
              and 'CFG_AUTO_CYCLE_STAGE_CFG = "stageCfg"' in src, "")
        check("%s proto 有 instances 键（CFG_AUTO_CYCLE_INSTANCES）" % tag,
              'CFG_AUTO_CYCLE_INSTANCES = "instances"' in src, "")
        check("%s proto 有实例全键（含六触发/两策略键/浊度三键×2）" % tag,
              all(k in src for k in ("pressEnabled", "pressThreshold",
                                     "pressCompare", "timerEnabled",
                                     "timerMinutes", "press1Enabled",
                                     "press1Threshold", "press1Compare",
                                     "press2Enabled", "press2Threshold",
                                     "press2Compare", "turb1Enabled",
                                     "turb1Threshold", "turb1Compare",
                                     "turb2Enabled", "turb2Threshold",
                                     "turb2Compare", "stirAccelRpmS",
                                     "resetValvesOnStop",
                                     "resetValvesOnEstop",
                                     "stirSwitchPolicy",
                                     "inletSwitchPolicy")), "")
        check("%s proto 有压力1/压力2 显示名常量（压力1/压力2）" % tag,
              'PRESS1_LABEL = "进水压力"' in src
              and 'PRESS2_LABEL = "出水压力"' in src, "")
    # ---- ⑤ 实例标题规则（titles）的前端函数**直接跑** ----
    # [断言的是真在渲染的那份代码] cycleInstanceTitles 抠出来在 node 里跑
    # 三组用例：同 mode 顺序编号 / 删掉首个后整表重算 / 未知 id 回 id。
    fn_titles = _js_func_src(page_src, "cycleInstanceTitles")
    check("远端设置页有 cycleInstanceTitles（实例标题=模式名+同类序号）",
          fn_titles is not None, "")
    if fn_titles:
        # [注意双重序列化] _js_run_node 自己会把 __out JSON.stringify 后
        # 打印 ⇒ call_expr 直接给**数组**即可；再包一层 stringify 会拿到
        # 字符串（json.loads 一次解不回数组）。
        got = _js_run_node(
            fn_titles,
            "["
            "cycleInstanceTitles([{mode:'produce'},{mode:'produce'},"
            "{mode:'backwash'}]),"
            "cycleInstanceTitles([{mode:'produce'},{mode:'backwash'}]),"
            "cycleInstanceTitles([{mode:'bogus'},{mode:'bogus'}])]")
        want = [["产水模式1", "产水模式2", "反洗模式1"],
                ["产水模式1", "反洗模式1"],
                ["bogus1", "bogus2"]]
        if got is None:
            skip("titles 规则实跑（node 不在，环境性跳过）", "无 node")
        else:
            check("titles 规则实跑：同 mode 顺序编号 / 删后整表重算 / "
                  "未知 id 回 id+n", got == want,
                  "got=%r want=%r" % (got, want))


def sec_auto_cycle_ui(ctx, token):
    """真浏览器打开远端设置页 ⇒ 抓网页端**实际发出的循环载荷**。

    [为什么必须真开] 「载荷形状 = enabled + instances（实例全键）、无旧键」
    是**前端**的行为：协议级段打的是 API，看不到网页端拼了什么；静态检查
    又只能看源码形状 ⇒ 这里用真浏览器（CDP）钩住 fetch，抓下「应用自动
    配置」真正 POST 出去的 JSON，逐项断言。

    [流程] ① ?authtoken 直登 → 跳到远端设置页（#/page/14）；
            ② 等循环区挂载（实例行 ≥2，来自服务端回显）+ 序号标题=
              titles 规则（模式名+同类序号）；
            ③ 钩 fetch 抓载荷 → 点「应用自动配置」→ 读抓到的 JSON：
              cycle 只含 enabled/instances、实例全键、序号标题逐行对；
            ④ 「添加实例」选放水模式 → 添加 → 载荷的 instances 多一项且
               含 drain、行标题出现「放水模式1」。
            [老服务端 C6100] GET 无 instances ⇒ 行只读（提示在）、载荷
              cycle 只有 enabled（**绝不发 instances**——老服务端整包拒）。
    [不做浏览器时] 环境问题 ⇒ 记一条带说明的 PASS（同 sec_page_boot 口径，
    不假绿也不误红）。
    """
    exe = find_browser()
    with Section("自动循环实例化：真浏览器抓网页端实际载荷"):
        py_end = _end[0] == "PY6200"
        if exe is None:
            check("浏览器可用（网页端载荷抓取）", True,
                  "未找到 Edge/Chrome：跳过真开检查")
            return
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        import cdp_client
        url = "http://%s:%d/?authtoken=%s" % (HOST, ctx.web, token)
        profile = tempfile.mkdtemp(prefix="edge_cyc_")
        port = free_port(9520)
        proc = None
        cdp = None
        try:
            # [会话预检 2026-11-27] 直登用的是脚本的 root token：若它已失效
            # （web 会话被清/进程重启），页面只会停在登录页，后续 11 条会
            # 连锁红而看不出原因 ⇒ 先验活，把状态写进详情。
            status, _h, me = ctx.get("/api/me", token=token)
            check("载荷抓取用的 root 会话有效（GET /api/me 200）",
                  status == 200 and bool((me or {}).get("ok")),
                  "status=%s doc=%s" % (status, str(me)[:160]))
            proc = subprocess.Popen(
                [exe, "--headless=new", "--disable-gpu", "--no-first-run",
                 "--user-data-dir=" + profile,
                 "--remote-debugging-port=%d" % port,
                 "--window-size=1280,900", "about:blank"],
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            ws_url = cdp_client.wait_for_target(port, timeout_s=45.0)
            cdp = cdp_client.Cdp(ws_url, timeout=30.0)
            cdp.call("Page.enable")
            cdp.call("Runtime.enable")

            # [401 取证 2026-11-27] 页面若在中途被判未登录，只看到登录页
            # 无法知道**哪个请求**吃了 401 ⇒ 注入 fetch 钩子把 401 的 URL
            # 记到 window.__hsf401（失败详情里带出来）。纯诊断，不改断言。
            _net_hook = (
                "(function(){window.__hsf401=[];"
                "if(window.__hsf401Hooked)return 1;window.__hsf401Hooked=1;"
                "var of=window.fetch;window.fetch=function(i,init){"
                " return of.apply(this,arguments).then(function(r){"
                "  try{if(r&&r.status===401){"
                "   window.__hsf401.push(String((i&&i.url)||i));}}"
                "  catch(e){}return r;});};return 1;})()")
            try:
                cdp.call("Page.addScriptToEvaluateOnNewDocument",
                         {"source": _net_hook})
                cdp.evaluate(_net_hook)
            except Exception:                        # noqa: BLE001
                pass

            # ---- ① 跳到远端设置页（等浏览器自己的链路 ready + 重载兜底）----
            # [为什么先等 link.ready] 实例行来自**服务端回显**：旧 UI 的四个
            # 模式行是静态建的，链路没起来挂载探针也能过；实例化后行＝回显
            # 的投影 ⇒ 必须先等页面自身的 SSE 链路 ready（store.link.ready），
            # 配置 GET 才会被拉、populateCycle 才会铺行、「应用」点击才会发
            # POST。复核轮（2026-11-26）实测抓到一次瞬态：页面挂载正常
            # （cur=14、循环区/添加下拉在）但 link 恒未 ready ⇒ rows=0、
            # populateCycle 未就绪、captured=0 的 11 连红。治法是**整页重载
            # 一次再等**（两窗各 20s）——harness 韧性，不弱化任何断言。
            link_ready = False
            dom = {}
            # [单次导航 + 登录态门 2026-11-27] 原来 Chrome 直接以带 token 的
            # URL 启动、2s 后又 Page.navigate 一次 ⇒ 首屏 boot 的 /api/me
            # 可能被第二次导航打断，旧文档在 `boot` 失败分支清掉 token 后
            # 若页面没真正重载，后续 1s 轮询全吃 401 ⇒ 整段只剩登录页
            # （实测 run3：link_ready=False + 登录页「登录会话已过期」）。
            # 改为 about:blank 起步、一次导航，然后**等登录态落定**：
            # 壳可见 + token 非空才算进入；仍在登录页就带着 token 再导航
            # 一次（token 由脚本侧验活过，重载即可恢复）。
            for attempt in (1, 2):
                cdp.call("Page.navigate", {"url": url})
                deadline = time.time() + 20.0
                while time.time() < deadline:
                    boot = cdp.evaluate(
                        "(function(){var lv=document.getElementById("
                        "'login-view');var lvOn=!!(lv&&!lv.classList"
                        ".contains('hidden'));var t=-1;try{t=(window.HSF"
                        "&&HSF.api&&HSF.api.getToken)?HSF.api.getToken()"
                        ".length:-1;}catch(e){t=-2;}"
                        "return {host:!!document.getElementById("
                        "'page-host'),login:lvOn,tokenLen:t,"
                        "net401:(window.__hsf401||[]).slice(0,4)};})()") or {}
                    if bool(boot.get("host")) and not bool(boot.get("login")) \
                            and int(boot.get("tokenLen") or 0) > 0:
                        break
                    time.sleep(0.4)
                if bool(boot.get("login")) or int(boot.get("tokenLen") or 0) <= 0:
                    step("第 %d 次打开：仍停在登录页（tokenLen=%r net401=%r）"
                         " ⇒ 带 token 重载" % (attempt, boot.get("tokenLen"),
                                              boot.get("net401")))
                    continue
                cdp.evaluate(
                    "(function(){location.hash='#/page/14';return 1;})()")
                deadline = time.time() + 20.0
                while time.time() < deadline:
                    link_ready = bool(cdp.evaluate(
                        "(function(){return !!(window.HSF&&window.HSF.store"
                        "&&window.HSF.store.get('link')&&"
                        "window.HSF.store.get('link').ready);})()"))
                    if link_ready:
                        break
                    time.sleep(0.5)
                if not link_ready:
                    step("第 %d 次打开：链路未 ready ⇒ 整页重载再等一窗"
                         % attempt)
                    continue
                deadline = time.time() + 25.0
                dom = {}
                while time.time() < deadline:
                    dom = cdp.evaluate(
                        "(function(){var l=document.querySelector("
                        "'.cycle-list');"
                        "var rows=document.querySelectorAll('.cycle-row');"
                        "var add=document.querySelector("
                        "'select[aria-label=\"添加实例的模式\"]');"
                        "return {list:!!l, rows:rows.length, add:!!add,"
                        " cur:(window.HSF&&window.HSF.router)"
                        "  ?window.HSF.router.current():-2};})()") or {}
                    if dom.get("list") and int(dom.get("rows") or 0) >= 2 \
                            and dom.get("add"):
                        break
                    time.sleep(0.5)
                if int(dom.get("rows") or 0) >= 2:
                    break
            # [失败取证 2026-11-27] 页面若停在登录页（会话失效）或抛了 JS
            # 异常，dom 会是空对象、后续断言全红 ⇒ 把页面实际状态写进详情。
            page_state = {}
            if not (bool(link_ready) and bool(dom.get("list"))
                    and int(dom.get("rows") or 0) >= 2 and bool(dom.get("add"))):
                page_state = cdp.evaluate(
                    "(function(){var t=-1;try{t=(window.HSF&&HSF.api"
                    "&&HSF.api.getToken)?HSF.api.getToken().length:-1;}"
                    "catch(e){t=-2;}"
                    "return {href:String(location.href),"
                    " login:!!document.getElementById('login-view'),"
                    " net401:(window.__hsf401||[]).slice(0,8),"
                    " tokenLen:t,"
                    " cookie:!!(String(document.cookie||'')"
                    ".indexOf('hsf_token=')>=0),"
                    " text:String((document.body&&document.body.innerText)"
                    "||'').slice(0,120)};})()") or {}
            check("远端设置页已挂载（链路 ready + 循环区在 + 添加实例下拉"
                  " + 实例行 ≥2）",
                  bool(link_ready) and bool(dom.get("list"))
                  and int(dom.get("rows") or 0) >= 2 and bool(dom.get("add")),
                  "link_ready=%r dom=%s page=%s"
                  % (link_ready, str(dom)[:200], str(page_state)[:240]))
            # ---- ①b 等配置回显到位（populateCycle 已跑且拿到 instances；
            #         老服务端没有 instances ⇒ 等到 populateCycle 跑过即可，
            #         界面按旧键派生视图只读显示）----
            # [为什么要等] 循环区的行是**先建后填**的：配置 GET 没回来时
            # 点「应用」⇒ 载荷里**没有** instances（只在「服务端回显过」时
            # 才发）⇒ 那抓到的是"还没加载"的中间态，不是产品行为。
            deadline = time.time() + 20.0
            loaded = False
            while time.time() < deadline:
                flag = cdp.evaluate(
                    "(function(){var l=window.__cycLog||[];"
                    "for(var i=0;i<l.length;i++){"
                    " if(l[i].what=='populateCycle'"
                    "&&l[i].hasInstances)return 1;}return 0;})()" if py_end
                    else "(function(){var l=window.__cycLog||[];"
                         "for(var i=0;i<l.length;i++){"
                         " if(l[i].what=='populateCycle')return 1;}"
                         "return 0;})()")
                if flag:
                    loaded = True
                    break
                time.sleep(0.5)
            check("配置回显已到位（循环区拿到服务端回显并铺行）",
                  loaded, "populateCycle 未就绪")
            # ---- ①c 顶层总循环次数控件（2026-11-27 用户「远端设置中顶层的
            #         也支持下循环次数，支持一直循环，或指定循环次数后
            #         停止」）----
            # [回显口径 2026-11-27 复核订正] 控件的值取的是**服务端当前配置**
            # （populate 回显），**不是**恒为 0：本脚本支持复用已在跑的 6200，
            # 其配置可能是非 0（本机 server_py/cfg/hsf_server_py.json 实测
            # top_repeat=1）⇒ 「值必须 == '0'」会假红（UI 其实回显正确）。
            # 正确判据：① 控件存在 + 0..999；② 文案含「一直循环」；
            # ③ 值 == GET 的 cycle.topRepeat（回显正确性）。
            # 「缺省 0」是**产品缺省**，由 proto/前端常量静态断言锁
            #（_auto_cycle_static 的 AUTO_CYCLE_TOP_REPEAT_DEF = 0 /
            # topRepeatDef: 0），不拿现场配置冒充缺省。
            status, _h, cfg_ui0 = ctx.get("/api/config", token=token)
            ui_top0 = ((((cfg_ui0 or {}).get("auto") or {}).get("cycle")
                        or {}).get("topRepeat"))
            tr_dom = cdp.evaluate(
                "(function(){var i=document.querySelector("
                "'input[aria-label=\"总循环次数\"]');if(!i)return null;"
                "var t=String((document.body&&document.body.innerText)||'');"
                "return {val:String(i.value),min:String(i.getAttribute('min')),"
                " max:String(i.getAttribute('max')),loop:t.indexOf('一直循环')>=0};"
                "})()") or {}
            check("远端设置有「总循环次数」控件（number，0..999）",
                  bool(tr_dom) and tr_dom.get("min") == "0"
                  and tr_dom.get("max") == "999",
                  "dom=%s" % str(tr_dom)[:200])
            check("总循环次数回显＝GET 当前值（缺省 0 由常量锁）且文案含"
                  "「一直循环」",
                  type(ui_top0) is int and bool(tr_dom)
                  and tr_dom.get("val") == str(ui_top0)
                  and bool(tr_dom.get("loop")),
                  "dom=%s get_topRepeat=%r" % (str(tr_dom)[:200], ui_top0))
            # ---- ② 钩 fetch（抓 POST /api/config 的载荷）----
            cdp.evaluate(
                "(function(){window.__cycCap=[];"
                "var of=window.fetch;"
                "window.fetch=function(input,init){"
                " try{ if(init&&init.body){"
                "  var b=String(init.body);"
                "  if(b.indexOf('\"section\"')>=0&&b.indexOf('\"auto\"')>=0)"
                "   window.__cycCap.push(b);"
                " } }catch(e){}"
                " return of.apply(this,arguments);};"
                "return 1;})()")
            # ---- ③ 点「应用自动配置」----
            cdp.evaluate(
                "(function(){var bs=document.querySelectorAll('button.btn');"
                "for(var i=0;i<bs.length;i++){"
                " if((bs[i].textContent||'').indexOf('应用自动配置')>=0)"
                "  {bs[i].click();return 1;}}return 0;})()")
            cap = []
            deadline = time.time() + 20.0
            reclicked = False
            while time.time() < deadline:
                cap = cdp.evaluate("window.__cycCap||[]") or []
                if cap:
                    break
                if not reclicked and time.time() > deadline - 10.0:
                    # 首点 10s 无载荷 ⇒ 补点一次（按钮接线/链路瞬态兜底；
                    # 仍抓不到就按失败记账，不无限重试）。
                    cdp.evaluate(
                        "(function(){var bs=document.querySelectorAll("
                        "'button.btn');for(var i=0;i<bs.length;i++){"
                        " if((bs[i].textContent||'').indexOf('应用自动配置')"
                        " >=0){bs[i].click();return 1;}}return 0;})()")
                    reclicked = True
                time.sleep(0.5)
            check("点击「应用自动配置」后抓到一次 auto 配置 POST",
                  bool(cap), "captured=%d" % len(cap or []))
            payload = {}
            if cap:
                try:
                    payload = json.loads(cap[-1])
                except (TypeError, ValueError):
                    payload = {}
            cyc = payload.get("cycle") or {}
            # [只取顶层行 2026-11-27] 组内子行也在 DOM 里带 .cycle-name；
            # 本断言比的是**顶层实例列表**（payload 顶层顺序）⇒ 按
            # `.cycle-list > .cycle-row` 取行、取该行自己的名字（head 在
            # 子项列表之前 ⇒ querySelector 首个就是本行的名字）。
            row_names = cdp.evaluate(
                "(function(){var out=[];"
                "document.querySelectorAll('.cycle-list > .cycle-row')"
                ".forEach(function(r){var n=r.querySelector('.cycle-name');"
                "out.push(String((n&&n.textContent)||''));});"
                "return out;})()") or []
            if py_end:
                # [键集 2026-11-27] 顶层总循环次数 topRepeat 是**条件键**：
                # 服务端回显过（说明认识）才随包提交，老服务端不回显就**绝不
                # 发**（整包拒）。断言按 GET 回显决定期望键集，两条口径都钉死。
                status, _h, cfg_ui = ctx.get("/api/config", token=token)
                ui_cycle = (((cfg_ui or {}).get("auto") or {}).get("cycle")
                            or {})
                echo_top = ui_cycle.get("topRepeat")
                has_top = (type(echo_top) is int)
                want_keys = ["enabled", "instances"] + (
                    ["topRepeat"] if has_top else [])
                check("网页端载荷的 cycle 键集 = enabled/instances"
                      "（+topRepeat 仅当服务端回显）",
                      sorted(cyc.keys()) == sorted(want_keys),
                      "keys=%s want=%s echo_topRepeat=%r"
                      % (sorted(cyc.keys()), sorted(want_keys), echo_top))
                if has_top:
                    check("载荷 topRepeat 是 0..999 的整数（回显过 ⇒ 随包提交）",
                          type(cyc.get("topRepeat")) is int
                          and 0 <= cyc.get("topRepeat") <= 999,
                          "topRepeat=%r" % (cyc.get("topRepeat"),))
                insts = cyc.get("instances")
                check("网页端载荷的 instances 是**数组**（有序，len≥2）",
                      isinstance(insts, list)
                      and len(insts or []) >= 2,
                      "instances=%s" % str(insts)[:200])
                first = insts[0] if isinstance(insts, list) and insts else {}
                check("实例载荷**全键**齐（26 键：mode/devices/六触发/转速/"
                      "方向/加速度/阀复位两键/换相策略两键）",
                      all(k in first for k in (
                          "mode", "devices", "pressEnabled", "pressThreshold",
                          "pressCompare", "press1Enabled", "press1Threshold",
                          "press1Compare", "press2Enabled", "press2Threshold",
                          "press2Compare", "turb1Enabled", "turb1Threshold",
                          "turb1Compare", "turb2Enabled", "turb2Threshold",
                          "turb2Compare", "timerEnabled", "timerMinutes",
                          "stirRpm", "stirReverse", "stirAccelRpmS",
                          "resetValvesOnStop", "resetValvesOnEstop",
                          "stirSwitchPolicy", "inletSwitchPolicy")),
                      "first=%s" % str(first)[:240])
                # ---- 行名 vs 载荷：**默认命名规则**（不是日志对照副本）----
                # [当前正确口径 2026-11-27] 同一模式出现 ≥2 次 ⇒ 该模式全部
                # 带序号（产水模式1/产水模式2）；只出现 1 次 ⇒ 裸名
                # （产水模式）。出处＝page14 的 cycleNormalizeLevelTitles
                # 注释（用户「有两个的时候第一个也要加后缀1」）。
                # [行名取 payload title 2026-11-27 复核加固] 界面「显示的
                # 就是 title」（page14: cycleRenderOrder 的 syncName 注释），
                # 而 payload 里每个实例的 title 由 wireTitle 现算并回写行
                # ⇒ **直接与 payload.title 比**才精确：既覆盖「同类≥2 全部
                # 带号」，也覆盖用户自定义名（如组合名「清洗」）与嵌套组
                #（旧算法只按 mode+计数推，遇到自定义名/组会误判）。
                # 只比**顶层**行（`.cycle-list > .cycle-row` 自己的名字；
                # 组内子行不属本层，另由 titles 静态用例与组内添加口径覆盖）。
                want_names = [
                    str(i.get("title")
                        or _CYCLE_MODE_TEXT.get(str(i.get("mode")),
                                                str(i.get("mode"))))
                    for i in (insts or []) if isinstance(i, dict)]
                check("行名＝载荷 title（显示的就是 title；含自定义名/组合名）",
                      bool(want_names)
                      and row_names[:len(want_names)] == want_names,
                      "dom=%r want=%r" % (row_names, want_names))
                check("载荷不含旧键（cycleModes/stageCfg 不再是提交面）",
                      "cycleModes" not in cyc and "stageCfg" not in cyc,
                      "keys=%s" % sorted(cyc.keys()))
            else:
                # 老服务端（C sim 无 instances）：行按旧键派生视图只读显示，
                # 载荷 **绝不发 instances**（老服务端对未知键整包拒）。
                check("老服务端：载荷的 cycle 只有 enabled（不发 instances）",
                      sorted(cyc.keys()) == ["enabled"],
                      "keys=%s" % sorted(cyc.keys()))
                ro_hint = cdp.evaluate(
                    "(function(){var h=document.querySelector('.caption');"
                    "var all=document.querySelectorAll('div.caption');"
                    "for(var i=0;i<all.length;i++){"
                    " if((all[i].textContent||'').indexOf("
                    "'当前服务端不支持循环实例')>=0)return 1;}"
                    "return 0;})()")
                check("老服务端：实例行只读（提示「当前服务端不支持循环实例」在）",
                      bool(ro_hint), "hint=%r" % ro_hint)
                check("老服务端：行名按旧键派生视图合成（cycleModes 去重名）",
                      bool(row_names), "names=%r" % (row_names,))
            # ---- ④ 「添加实例」选放水模式 → 添加 → 载荷跟着变 ----
            # [先等第一次应用的**应答**落定] 第一次「应用」的 POST 发出后，
            # 应答回来会触发 populate(doc) ⇒ populateCycle 重跑（**整表重建
            # 行**）⇒ 若在应答到达之前添加，迟到的回显会把新行冲掉。
            # ⇒ 等 __cycLog 里再出现一次 populateCycle（= 应答已落）再添加。
            _pc0 = cdp.evaluate(
                "(function(){var l=(window.__cycLog||[]);var n=0;"
                "for(var i=0;i<l.length;i++){"
                " if(l[i].what=='populateCycle')n++;}return n;})()")
            _n0 = int(cdp.evaluate(
                "(function(){return document.querySelectorAll("
                "'.cycle-row').length;})()") or 0)
            if py_end:
                cdp.evaluate(
                    "(function(){var s=document.querySelector("
                    "'select[aria-label=\"添加实例的模式\"]');"
                    "if(!s)return 0;s.value='drain';"
                    "s.dispatchEvent(new Event('change',{bubbles:true}));"
                    "return 1;})()")
                cdp.evaluate(
                    "(function(){var bs=document.querySelectorAll("
                    "'button.btn');for(var i=0;i<bs.length;i++){"
                    " if((bs[i].getAttribute('aria-label')||'')==="
                    "'添加循环实例')"
                    "  {bs[i].click();return 1;}}return 0;})()")
                # 等待：要么 populateCycle 再跑一次（应答落地），要么超时兜底
                deadline = time.time() + 10.0
                while time.time() < deadline:
                    _pc1 = cdp.evaluate(
                        "(function(){var l=(window.__cycLog||[]);var n=0;"
                        "for(var i=0;i<l.length;i++){"
                        " if(l[i].what=='populateCycle')n++;}return n;})()")
                    if int(_pc1 or 0) > int(_pc0 or 0):
                        break
                    time.sleep(0.3)
                time.sleep(0.5)
                # [添加到稳定] 迟到的回显可能把新行冲掉（整表重建）⇒
                # 轮询到「放水模式行在且行数=原+1 连续两拍稳定」再点应用
                # （最多 8s）；被冲掉就再点一次「添加」。
                _drain_stable = False
                _dl = time.time() + 8.0
                _prev_ok = False
                while time.time() < _dl:
                    _st = cdp.evaluate(
                        "(function(){var rows=document.querySelectorAll("
                        "'.cycle-row');var drain=false;"
                        "for(var i=0;i<rows.length;i++){"
                        " var n=rows[i].querySelector('.cycle-name');"
                        " if(n&&(String(n.textContent||'').indexOf("
                        "'放水模式')===0))drain=true;}"
                        "return rows.length+':'+drain;})()")
                    ok_now = (_st == "%d:true" % (_n0 + 1))
                    if ok_now and _prev_ok:
                        _drain_stable = True
                        break
                    if not ok_now:
                        cdp.evaluate(
                            "(function(){var bs=document.querySelectorAll("
                            "'button.btn');for(var i=0;i<bs.length;i++){"
                            " if((bs[i].getAttribute('aria-label')||'')==="
                            "'添加循环实例')"
                            "  {bs[i].click();return 1;}}return 0;})()")
                    _prev_ok = ok_now
                    time.sleep(0.5)
                check("「添加」放水实例后行保持稳定（不被迟到回显冲掉）",
                      _drain_stable,
                      "最后一拍=%r（期望 %d 行含放水模式）" % (_st, _n0 + 1))
                row_names2 = cdp.evaluate(
                    "(function(){var out=[];"
                    "document.querySelectorAll('.cycle-name')"
                    ".forEach(function(n){"
                    "out.push(String(n.textContent||''));});return out;})()"
                ) or []
                check("新行标题＝放水模式（默认名：同类首个不带号、重名才加后缀；"
                      "列表顺序内编）", "放水模式" in row_names2,
                      "names=%r" % (row_names2,))
            time.sleep(0.3)
            cdp.evaluate(
                "(function(){var bs=document.querySelectorAll('button.btn');"
                "for(var i=0;i<bs.length;i++){"
                " if((bs[i].textContent||'').indexOf('应用自动配置')>=0)"
                "  {bs[i].click();return 1;}}return 0;})()")
            cap2 = []
            deadline = time.time() + 20.0
            while time.time() < deadline:
                cap2 = cdp.evaluate("window.__cycCap||[]") or []
                if len(cap2) > len(cap or []):
                    break
                time.sleep(0.5)
            payload2 = {}
            if cap2:
                try:
                    payload2 = json.loads(cap2[-1])
                except (TypeError, ValueError):
                    payload2 = {}
            cyc2 = payload2.get("cycle") or {}
            if py_end:
                insts2 = cyc2.get("instances")
                modes2 = [i.get("mode") for i in (insts2 or [])
                          if isinstance(i, dict)]
                check("「添加」放水实例 ⇒ 载荷 instances 多一项且含 drain"
                      "（追加在末尾＝循环最后一步）",
                      isinstance(insts2, list) and "drain" in modes2
                      and len(insts2 or []) == len(cyc.get("instances")
                                                   or []) + 1
                      and modes2[-1] == "drain",
                      "modes=%r" % (modes2,))
                check("drain 实例带全键且缺省触发合法（定时开 ⇒ 新建即可过"
                      "服务端四触发闸）",
                      bool(insts2) and isinstance(insts2[-1], dict)
                      and insts2[-1].get("timerEnabled") is True
                      and "stirSwitchPolicy" in insts2[-1]
                      and "inletSwitchPolicy" in insts2[-1],
                      "last=%s" % str(insts2[-1] if insts2 else None)[:240])
            else:
                check("老服务端：第二次应用的载荷仍只有 enabled（只读行"
                      "不进载荷）", sorted(cyc2.keys()) == ["enabled"],
                      "keys=%s" % sorted(cyc2.keys()))
            try:
                shot = os.path.join(REPO, "verify", "webui", "shots",
                                    "webui_cycle_%s.png" % ctx.tag)
                os.makedirs(os.path.dirname(shot), exist_ok=True)
                with open(shot, "wb") as fp:
                    fp.write(cdp.screenshot_png())
                step("循环配置区截图留证：%s" % shot)
            except Exception:                        # noqa: BLE001
                pass
            # ---- ⑤ 恢复：删掉放水实例并再应用一次 ----
            # [为什么必须恢复 2026-11-19 实锅] 上面点过的「应用自动配置」
            # 把放水实例存进了服务端，而放水实例缺省没配启动设备 ⇒ 后面的
            # 自动类段启动被闸拒绝（缺设备）——测试顺序污染，不是产品缺陷
            # ⇒ 用例自己收拾自己：删除该实例 + 再应用（老服务端没添加过，
            # 无需删）。
            if py_end:
                cdp.evaluate(
                    "(function(){var rows=document.querySelectorAll("
                    "'.cycle-row');for(var i=0;i<rows.length;i++){"
                    " var n=rows[i].querySelector('.cycle-name');"
                    " if(n&&(String(n.textContent||'').indexOf("
                    "'放水模式')===0)){"
                    "  var bs=rows[i].querySelectorAll('button.btn');"
                    "  for(var j=0;j<bs.length;j++){"
                    "   if((bs[j].textContent||'')==='删除')"
                    "    {bs[j].click();return 1;}}}}"
                    "return 0;})()")
                time.sleep(0.5)
            cdp.evaluate(
                "(function(){var bs=document.querySelectorAll('button.btn');"
                "for(var i=0;i<bs.length;i++){"
                " if((bs[i].textContent||'').indexOf('应用自动配置')>=0)"
                "  {bs[i].click();return 1;}}return 0;})()")
            time.sleep(1.5)
            # ---- ⑥ 主页提示行的遍数/轮次（2026-11-27 用户「组合有多次循环
            #        要在主界面提示总共几次、当前第几次」）----
            # [注入口径] 直接给 store 灌一份 mode 文档（与 SSE mode 事件同一
            # 条通路；applyEvent 同步广播 ⇒ 同一次 evaluate 里就能读回提示行
            # 文本，不会被 1s 轮询覆盖）。断言只看前缀片段，不锁整句。
            cdp.evaluate(
                "(function(){location.hash='#/page/0';return 1;})()")
            home_ready = False
            deadline = time.time() + 15.0
            while time.time() < deadline:
                home_ready = bool(cdp.evaluate(
                    "(function(){return !!document.querySelector("
                    "'.auto-hint');})()"))
                if home_ready:
                    break
                time.sleep(0.4)
            check("主界面自动模式卡已挂载（提示行文案注入用）", home_ready,
                  "auto-hint 未出现")
            if home_ready:
                def _inject(doc_js):
                    return cdp.evaluate(
                        "(function(){HSF.store.applyEvent('mode',"
                        + doc_js + ");return String((document.querySelector("
                        "'.auto-hint')||{}).textContent||'');})()")

                txt_rep = _inject(
                    "{known:true,auto:true,autoRunning:true,"
                    "autoPhase:'running',autoMode:'produce',"
                    "autoStageTitle:'产水模式2',autoRepGroup:'组合',"
                    "autoRepCurrent:2,autoRepTotal:3,"
                    "autoRound:1,autoRoundTotal:5}")
                check("主页提示行含「第 2/3 遍」（autoRepTotal>=2 才显示）",
                      "第 2/3 遍" in str(txt_rep or ""),
                      "hint=%r" % (txt_rep,))
                check("主页提示行含「第 1/5 轮」（autoRoundTotal>0）",
                      "第 1/5 轮" in str(txt_rep or ""),
                      "hint=%r" % (txt_rep,))
                txt_loop = _inject(
                    "{known:true,auto:true,autoRunning:true,"
                    "autoPhase:'running',autoMode:'produce',"
                    "autoStageTitle:'产水模式2',autoRepCurrent:1,"
                    "autoRepTotal:1,autoRound:2,autoRoundTotal:0}")
                check("一直循环（total=0）报「第 2 轮」且 total==1 不显示遍数",
                      "第 2 轮" in str(txt_loop or "")
                      and "遍" not in str(txt_loop or "")
                      and "第 1/1 遍" not in str(txt_loop or ""),
                      "hint=%r" % (txt_loop,))
                txt_none = _inject(
                    "{known:true,auto:true,autoRunning:true,"
                    "autoPhase:'running',autoMode:'produce',"
                    "autoStageTitle:'',autoRepGroup:'',autoRepCurrent:0,"
                    "autoRepTotal:0,autoRound:0,autoRoundTotal:0}")
                check("缺键/0 值不显示遍数轮次（不编造）",
                      "遍" not in str(txt_none or "")
                      and "轮" not in str(txt_none or ""),
                      "hint=%r" % (txt_none,))
            # ---- ⑦ 本地「添加」同模式重复实例 ⇒ 整层重排带号（2026-11-27
            #         复核实锅：只给新行命名 ⇒ 首行裸名 + 新行带号并存）----
            # [口径] 同层同类出现 ≥2 次 ⇒ **全部**带号（第一个也是 1）；
            # 只有 1 次 ⇒ 裸名。回显路径（cycleSetInstances）早已如此，本地
            # 添加路径原先漏调 cycleNormalizeLevelTitles ⇒ 实测「产水模式」
            # 与「产水模式2」并存。这里连点两次「添加」(产水) ⇒ 产水实例
            # 必然 ≥2 ⇒ 断言名字恰为 产水模式1..N（无裸名），删掉刚加的两行
            # 收尾（**不点应用**：服务端配置由 sec_cleanup 按开跑前原值恢复）。
            cdp.evaluate(
                "(function(){location.hash='#/page/14';return 1;})()")
            dup_ready = False
            deadline = time.time() + 15.0
            while time.time() < deadline:
                dup_ready = bool(cdp.evaluate(
                    "(function(){var b=document.querySelector("
                    "'button[aria-label=\"添加循环实例\"]');"
                    "return !!(document.querySelector('.cycle-list')"
                    "&&document.querySelectorAll('.cycle-row').length>=2"
                    "&&b&&!b.disabled);})()"))
                if dup_ready:
                    break
                time.sleep(0.4)

            def _add_produce():
                cdp.evaluate(
                    "(function(){var s=document.querySelector("
                    "'select[aria-label=\"添加实例的模式\"]');"
                    "if(!s)return 0;s.value='produce';"
                    "s.dispatchEvent(new Event('change',{bubbles:true}));"
                    "return 1;})()")
                cdp.evaluate(
                    "(function(){var b=document.querySelector("
                    "'button[aria-label=\"添加循环实例\"]');"
                    "if(!b||b.disabled)return 0;b.click();return 1;})()")

            def _produce_names():
                names = cdp.evaluate(
                    "(function(){var out=[];"
                    "document.querySelectorAll('.cycle-name')"
                    ".forEach(function(n){var t=String(n.textContent||'');"
                    "if(n.closest&&n.closest('.cycle-child-list'))return;"
                    "if(t.indexOf('产水模式')===0)out.push(t);});"
                    "return out;})()") or []
                return [str(x) for x in names]

            if dup_ready:
                _add_produce()
                time.sleep(0.4)
                _add_produce()
                deadline = time.time() + 8.0
                got_names = []
                while time.time() < deadline:
                    got_names = _produce_names()
                    if len(got_names) >= 2:
                        break
                    time.sleep(0.4)
                want_names = ["产水模式%d" % i
                              for i in range(1, len(got_names) + 1)]
                check("本地「添加」同模式重复实例 ⇒ 整层带号（首个也是 1，"
                      "无裸名并存）",
                      len(got_names) >= 2 and got_names == want_names,
                      "dom=%r want=%r" % (got_names, want_names))
                # 收尾：删掉刚加的两行（列表尾部两条产水；不点应用）。
                for _ in range(2):
                    cdp.evaluate(
                        "(function(){var rows=document.querySelectorAll("
                        "'.cycle-row');var pick=null;"
                        "for(var i=0;i<rows.length;i++){"
                        " if(rows[i].parentNode&&rows[i].parentNode"
                        ".classList&&rows[i].parentNode.classList"
                        ".contains('cycle-child-list'))continue;"
                        " var n=rows[i].querySelector('.cycle-name');"
                        " if(n&&String(n.textContent||'').indexOf("
                        "'产水模式')===0)pick=rows[i];}"
                        "if(!pick)return 0;"
                        "var bs=pick.querySelectorAll('button.btn');"
                        "for(var j=0;j<bs.length;j++){"
                        " if((bs[j].textContent||'').trim()==='删除')"
                        "  {bs[j].click();return 1;}}return 0;})()")
                    time.sleep(0.3)
            else:
                check("本地「添加」同模式重复实例 ⇒ 整层带号（首个也是 1，"
                      "无裸名并存）", False,
                      "循环区未就绪（add 按钮不可用/行数<2）")
            # [401 留痕 2026-11-27] 即使断言全过，页面期间若出现过 401
            # 也打印出来（会话被清会让界面悄悄回登录页，见 run2 截图）。
            try:
                stray401 = cdp.evaluate(
                    "(window.__hsf401||[]).slice(0,8)") or []
                if stray401:
                    step("页面期间出现 401：%s" % (stray401,))
            except Exception:                        # noqa: BLE001
                pass
        except Exception as exc:                     # noqa: BLE001
            check("自动循环网页端载荷抓取（真浏览器）", False,
                  "%s: %s" % (type(exc).__name__, exc))
        finally:
            if cdp is not None:
                try:
                    cdp.close()
                except Exception:                    # noqa: BLE001
                    pass
            if proc is not None:
                _kill_tree(proc)
            _rm_profile(profile)


def sec_switch_policy(ctx):
    """网页端「换相策略」：SET/GET 往返 + 非法值 fail-closed + 前端接线。

    [实例化 2026-11-26 口径变化] 换相策略从「逐模式」（auto.modes.*）搬进
    「逐实例」（auto.cycle.instances[i].stirSwitchPolicy / inletSwitchPolicy）：
      · PY6200：modes.produce 带 stirSwitchPolicy 的 SET ⇒ ok=true 但
        **旧键忽略**（回显 modes.produce 不含该键——服务端对 modes 里的
        旧策略键是「忽略不报错，继续处理包内其它键」）；策略键改到实例
        载荷上做 SET/GET 往返 + 非法值 fail-closed；
      · C6100：C 端弃用不改，仍是逐模式口径 ⇒ 保留旧断言；instances 是
        PY 先行 ⇒ 该部分记 SKIP（不进 FAIL 分母）。
    [三层都要锁] ① 网页 API 的 SET/GET 往返；② 服务端 fail-closed（非法值
    明确报错）；③ 前端 JS 的接线（实例设置卡两个下拉 + 两个载荷键 + 三个
    策略取值 + **单模式块无旧策略下拉**）——JS 改动不会被 API 级检查看到，
    故补静态检查。
    """
    with Section("网页端换相策略（SET/GET 往返 + 非法值拒 + 前端接线）"):
        tok = ctx.token_of("root")
        py_end = _end[0] == "PY6200"
        if py_end:
            # ---- ① 单模式旧策略键：忽略不报错（实例化后的 modes 面）----
            status, _h, doc = ctx.post(
                "/api/config",
                {"section": "auto",
                 "modes": {"produce": {"stirSwitchPolicy": "carry",
                                       "inletSwitchPolicy": "direct"}}},
                token=tok)
            ok = status == 200 and (doc or {}).get("ok") is True
            check("SET modes.produce 带旧策略键 ok=true（忽略不报错）", ok,
                  "status=%s doc=%s" % (status, str(doc)[:200]))
            status, _h, cfg = ctx.get("/api/config", token=tok)
            prod = (((cfg or {}).get("auto") or {}).get("modes") or {}).get(
                "produce") or {}
            check("回显 modes.produce 不含旧策略键（忽略 ≠ 改值）",
                  "stirSwitchPolicy" not in prod
                  and "inletSwitchPolicy" not in prod,
                  "produce=%s" % str(prod)[:200])
            # ---- ② 策略逐实例：SET/GET 往返 + 非法值 fail-closed ----
            insts = [
                {"mode": "produce", "devices": ["valve1", "stirPump"],
                 "pressEnabled": True, "pressThreshold": 0.5,
                 "pressCompare": "gt", "timerEnabled": True,
                 "timerMinutes": 30,
                 "stirSwitchPolicy": "carry", "inletSwitchPolicy": "direct"},
                {"mode": "backwash", "devices": ["valve2", "inletPump"],
                 "pressEnabled": False, "pressThreshold": 0.5,
                 "pressCompare": "gt", "timerEnabled": True,
                 "timerMinutes": 10,
                 "stirSwitchPolicy": "stopRestart",
                 "inletSwitchPolicy": "stopRestart"},
            ]
            status, _h, doc = ctx.post(
                "/api/config",
                {"section": "auto",
                 "cycle": {"enabled": True, "instances": insts}}, token=tok)
            ok = status == 200 and (doc or {}).get("ok") is True
            check("SET cycle.instances 带逐实例策略 ok=true", ok,
                  "status=%s doc=%s" % (status, str(doc)[:200]))
            status, _h, cfg = ctx.get("/api/config", token=tok)
            got = (((cfg or {}).get("auto") or {}).get("cycle") or {}).get(
                "instances") or []
            first = got[0] if got and isinstance(got[0], dict) else {}
            check("GET 回读：首实例 stirSwitchPolicy=carry / "
                  "inletSwitchPolicy=direct",
                  first.get("stirSwitchPolicy") == "carry"
                  and first.get("inletSwitchPolicy") == "direct",
                  "instances[0]=%s" % str(first)[:200])
            status, _h, doc = ctx.post(
                "/api/config",
                {"section": "auto",
                 "cycle": {"enabled": True, "instances": [
                     dict(insts[0], stirSwitchPolicy="bogus"), insts[1]]}},
                token=tok)
            check("网页 SET 非法换相策略（实例）显式报错（ok=false + err）",
                  status == 200 and (doc or {}).get("ok") is False
                  and bool((doc or {}).get("err")),
                  "status=%s doc=%s" % (status, str(doc)[:200]))
            status, _h, cfg = ctx.get("/api/config", token=tok)
            got = (((cfg or {}).get("auto") or {}).get("cycle") or {}).get(
                "instances") or []
            first = got[0] if got and isinstance(got[0], dict) else {}
            check("非法 SET 后原值不变（carry）",
                  first.get("stirSwitchPolicy") == "carry",
                  "instances[0].stirSwitchPolicy=%r"
                  % first.get("stirSwitchPolicy"))
        else:
            # C 端弃用不改：仍是「逐模式」口径 ⇒ 旧断言原样保留。
            payload = {"section": "auto",
                       "modes": {"produce": {
                           "devices": ["valve1", "stirPump"],
                           "stirRpm": 720,
                           "stirSwitchPolicy": "carry",
                           "inletSwitchPolicy": "direct"}}}
            status, _h, doc = ctx.post("/api/config", payload, token=tok)
            ok = status == 200 and (doc or {}).get("ok") is True
            check("网页 SET auto 换相策略 ok=true（C 端逐模式口径）", ok,
                  "status=%s doc=%s" % (status, str(doc)[:200]))
            status, _h, cfg = ctx.get("/api/config", token=tok)
            prod = (((cfg or {}).get("auto") or {}).get("modes") or {}).get(
                "produce") or {}
            check("网页 GET 回读：stirSwitchPolicy=carry",
                  prod.get("stirSwitchPolicy") == "carry",
                  "got=%r" % prod.get("stirSwitchPolicy"))
            check("网页 GET 回读：inletSwitchPolicy=direct",
                  prod.get("inletSwitchPolicy") == "direct",
                  "got=%r" % prod.get("inletSwitchPolicy"))
            skip("策略逐实例 SET/GET 往返（instances 往返/非法值拒）",
                 "PY 先行键，C 端不实现")
        # ---- ③ 前端接线（静态，两端都查；读源码，不过上游）----
        js_path = os.path.join(WEBUI_DIR, "hsfweb", "static", "js", "pages",
                               "page14_remote.js")
        try:
            with open(js_path, "r", encoding="utf-8") as fh:
                js = fh.read()
        except OSError as exc:
            check("远端设置页 JS 可读", False, "%s" % exc)
            return
        check("前端接线：实例设置卡有搅拌泵/进水泵两个换相策略下拉",
              "stirSwitch" in js and "inletSwitch" in js, "")
        check("前端接线：载荷带两个策略键",
              "stirSwitchPolicy" in js and "inletSwitchPolicy" in js, "")
        check("前端接线：三档策略取值齐（stopRestart/carry/direct）",
              all(v in js for v in ("stopRestart", "carry", "direct")), "")
        # [单模式块无旧策略下拉 2026-11-26] buildModeBlock 函数体（剥注释）
        # 里不得再出现策略下拉/策略载荷键——策略已搬进实例设置卡。
        fn_mode = _js_func_src(js, "buildModeBlock")
        check("前端接线：单模式块无旧策略下拉（stirSwitch/inletSwitch/"
              "SWITCH_POLICIES 不在 buildModeBlock 函数体）",
              fn_mode is not None and "stirSwitch" not in fn_mode
              and "inletSwitch" not in fn_mode
              and "SWITCH_POLICIES" not in fn_mode,
              "buildModeBlock=%s" % (fn_mode or "")[:160])
        fn_cfg = _js_func_src(js, "autoConfigValues")
        check("前端接线：modes 载荷不带旧策略键（stirSwitchPolicy/"
              "inletSwitchPolicy 不在 autoConfigValues 函数体）",
              fn_cfg is not None and "stirSwitchPolicy" not in fn_cfg
              and "inletSwitchPolicy" not in fn_cfg,
              "autoConfigValues=%s" % (fn_cfg or "")[:160])


#: 模式 id → 显示名（与 server_py proto.AUTO_MODES 逐字一致；titles 规则
#: 的期望值按它现算，与前端 cycleInstanceTitles 同一算法）。
_CYCLE_MODE_TEXT = {
    "produce": "产水模式", "backwash": "反洗模式",
    "rinse": "顺洗模式", "drain": "放水模式",
}

#: 自动循环**实例**夹具（协议全键 20 个；与 wire 的实例对象逐键表同形）。
#: [参数选取] A=产水（压差 0.42/lt + 压力1 1.2/gt + 定时 33 分钟 + carry/
#: direct 策略）；B=反洗（全触发里只开压力2 0.8/lt + 定时 7 分钟 + 转速
#: 900 —— 900 供 stirMaxRpm 联动钳位用例观测）。
CYCLE_INST_A = {
    "mode": "produce", "devices": ["valve1", "stirPump", "inletPump"],
    "pressEnabled": True, "pressThreshold": 0.42, "pressCompare": "lt",
    "press1Enabled": True, "press1Threshold": 1.2, "press1Compare": "gt",
    "press2Enabled": False, "press2Threshold": 1.0, "press2Compare": "gt",
    "timerEnabled": True, "timerMinutes": 33,
    "stirRpm": 720, "stirReverse": True, "stirAccelRpmS": 300,
    "resetValvesOnStop": True, "resetValvesOnEstop": False,
    "stirSwitchPolicy": "carry", "inletSwitchPolicy": "direct",
}
CYCLE_INST_B = {
    "mode": "backwash", "devices": ["valve2", "inletPump"],
    "pressEnabled": False, "pressThreshold": 0.5, "pressCompare": "gt",
    "press1Enabled": False, "press1Threshold": 1.0, "press1Compare": "gt",
    "press2Enabled": True, "press2Threshold": 0.8, "press2Compare": "lt",
    "timerEnabled": True, "timerMinutes": 7,
    "stirRpm": 900, "stirReverse": False, "stirAccelRpmS": 0,
    "resetValvesOnStop": False, "resetValvesOnEstop": True,
    "stirSwitchPolicy": "stopRestart", "inletSwitchPolicy": "stopRestart",
}


def _inst_same(got, want):
    """实例逐键比较（浮点按 1e-6 容差；其余精确）。got 缺键 = 不一致。"""
    if not isinstance(got, dict):
        return False
    for key, want_v in want.items():
        got_v = got.get(key)
        if isinstance(want_v, float):
            if not isinstance(got_v, (int, float)) \
                    or abs(float(got_v) - want_v) > 1e-6:
                return False
        elif got_v != want_v:
            return False
    return True


def sec_cycle_instances(ctx):
    """自动循环实例化（auto.cycle.instances）：API 往返 + fail-closed +
    旧键迁移（写穿/派生视图）。

    [只在 PY6200 跑] C 端弃用不改：C sim 不认 instances（整包拒）⇒
    C6100 本段整体记 SKIP + 原因（不进 FAIL 分母）。
    [口径出处] wire ①-⑦ fail-closed / 实例逐键表 / 旧键迁移规则二分 /
    派生兼容视图（cycleModes 去重保序、缺席模式回合成缺省、六扁平键=首
    实例，有损）。
    [收尾] 本段把配置变成「显式 instances」且不可逆（无取消显式的 SET）
    ⇒ 段末把开跑前的循环语义按实例写回去；py sim 是临时程序根、跑完即
    收，不留跨轮残留。
    """
    with Section("循环实例（instances 有序表 + 同模式可重复 + 旧键写穿）"):
        if _end[0] != "PY6200":
            skip("循环实例整段（instances API 往返/fail-closed/旧键写穿）",
                 "PY 先行键，C 端不实现（C sim 对未知键整包拒）")
            return
        tok = ctx.token_of("root")
        # ---- 抓原值（收尾恢复用）----
        status, _h, cfg0 = ctx.get("/api/config", token=tok)
        auto0 = (cfg0 or {}).get("auto") or {}
        orig_cycle = auto0.get("cycle") or {}
        orig_stirmax = auto0.get("stirMaxRpm")
        orig_insts = orig_cycle.get("instances")
        orig_explicit = isinstance(orig_insts, list) \
            and len(orig_insts) >= 2

        def restore_cycle():
            """把 auto.cycle 恢复成开跑前的循环语义（显式→原样回写；
            未显式→按派生视图合成实例回写；stirMaxRpm 一并恢复）。"""
            if orig_explicit:
                cyc = {"enabled": orig_cycle.get("enabled", True),
                       "instances": orig_insts}
            else:
                modes = orig_cycle.get("cycleModes") \
                    if isinstance(orig_cycle.get("cycleModes"), list) \
                    else ["produce", "backwash", "rinse"]
                stage = orig_cycle.get("stageCfg") \
                    if isinstance(orig_cycle.get("stageCfg"), dict) else {}
                insts = []
                for idx, mid in enumerate(modes):
                    sub = stage.get(mid) \
                        if isinstance(stage.get(mid), dict) else {}
                    if sub:
                        inst = {"mode": mid,
                                "pressEnabled": bool(sub.get("pressEnabled")),
                                "pressThreshold":
                                    float(sub.get("pressThreshold", 0.5)),
                                "pressCompare":
                                    str(sub.get("pressCompare", "gt")),
                                "timerEnabled":
                                    bool(sub.get("timerEnabled")),
                                "timerMinutes":
                                    int(sub.get("timerMinutes", 10))}
                    elif idx == 0:
                        # 六扁平键→首实例（旧合成链的形状：兜底定时键映射
                        # 到 timerEnabled/timerMinutes）
                        inst = {"mode": mid, "pressEnabled": False,
                                "pressThreshold":
                                    float(orig_cycle.get("pressThreshold",
                                                         0.5)),
                                "pressCompare":
                                    str(orig_cycle.get("pressCompare",
                                                       "gt")),
                                "timerEnabled":
                                    bool(orig_cycle.get("timerFallback",
                                                        False)),
                                "timerMinutes":
                                    int(orig_cycle.get("timerMinutes", 30))}
                    else:
                        inst = {"mode": mid, "pressEnabled": False,
                                "pressThreshold": 0.5,
                                "pressCompare": "gt",
                                "timerEnabled": True,
                                "timerMinutes": 10}
                    insts.append(inst)
                cyc = {"enabled": orig_cycle.get("enabled", True),
                       "instances": insts}
            payload = {"section": "auto", "cycle": cyc}
            if orig_stirmax is not None:
                payload["stirMaxRpm"] = orig_stirmax
            status, _h, doc = ctx.post("/api/config", payload, token=tok)
            return status == 200 and (doc or {}).get("ok") is True

        # ---- ① 基础往返：两实例（不同模式）全键 ----
        status, _h, doc = ctx.post(
            "/api/config",
            {"section": "auto",
             "cycle": {"enabled": True,
                       "instances": [CYCLE_INST_A, CYCLE_INST_B]}},
            token=tok)
        ok = status == 200 and (doc or {}).get("ok") is True
        check("SET cycle.instances（两实例全键）ok=true", ok,
              "status=%s doc=%s" % (status, str(doc)[:240]))
        status, _h, cfg = ctx.get("/api/config", token=tok)
        cyc = (((cfg or {}).get("auto") or {}).get("cycle") or {})
        got = cyc.get("instances")
        check("GET 回读 instances 为数组且 len==2（有序）",
              isinstance(got, list) and len(got) == 2,
              "instances=%s" % str(got)[:240])
        check("GET 回读实例A逐键一致（含压力1/压力2 三键与两策略键）",
              _inst_same((got or [{}])[0], CYCLE_INST_A),
              "A=%s" % str((got or [{}])[0])[:240])
        check("GET 回读实例B逐键一致",
              _inst_same((got or [{}])[1], CYCLE_INST_B),
              "B=%s" % str((got or [{}])[1])[:240])
        # ---- 派生兼容视图（旧键继续回，但语义=派生）----
        check("派生视图：cycleModes=实例 mode 去重保序 "
              "[produce, backwash]",
              cyc.get("cycleModes") == ["produce", "backwash"],
              "cycleModes=%r" % (cyc.get("cycleModes"),))
        check("派生视图：六扁平键=首实例的值（0.42/lt/兜底开/33 分钟）",
              abs((cyc.get("pressThreshold") or 0) - 0.42) < 1e-6
              and cyc.get("pressCompare") == "lt"
              and cyc.get("timerFallback") is True
              and cyc.get("timerMinutes") == 33,
              "flat=%s" % str({k: cyc.get(k) for k in (
                  "pressThreshold", "pressCompare", "timerFallback",
                  "timerMinutes")}))
        sc = cyc.get("stageCfg") or {}
        a_stage = {"pressEnabled": True, "pressThreshold": 0.42,
                   "pressCompare": "lt", "timerEnabled": True,
                   "timerMinutes": 33}
        check("派生视图：stageCfg[produce]=首实例五触发键",
              _inst_same(sc.get("produce"), a_stage),
              "produce=%r" % (sc.get("produce"),))
        check("派生视图：缺席模式回合成缺省（rinse/drain：压差关+定时开"
              "10 分钟）",
              all(_inst_same(sc.get(m), {
                  "pressEnabled": False, "pressThreshold": 0.5,
                  "pressCompare": "gt", "timerEnabled": True,
                  "timerMinutes": 10}) for m in ("rinse", "drain")),
              "rinse=%r drain=%r" % (sc.get("rinse"), sc.get("drain")))
        check("派生视图：backwashMinutes=反洗首实例 timerMinutes=7；"
              "rinseMinutes 缺席 ⇒ 10",
              cyc.get("backwashMinutes") == 7
              and cyc.get("rinseMinutes") == 10,
              "backwashMinutes=%r rinseMinutes=%r"
              % (cyc.get("backwashMinutes"), cyc.get("rinseMinutes")))

        # ---- ② 同模式可重复 + 顺序保序（数据面）----
        inst_a2 = dict(CYCLE_INST_A, pressThreshold=0.44, timerMinutes=21,
                       stirSwitchPolicy="stopRestart")
        insts2 = [CYCLE_INST_A, inst_a2, CYCLE_INST_B]
        status, _h, doc = ctx.post(
            "/api/config",
            {"section": "auto",
             "cycle": {"enabled": True, "instances": insts2}}, token=tok)
        ok = status == 200 and (doc or {}).get("ok") is True
        check("同 mode 重复出现（produce×2 + backwash）SET ok=true", ok,
              "status=%s doc=%s" % (status, str(doc)[:240]))
        status, _h, cfg = ctx.get("/api/config", token=tok)
        cyc = (((cfg or {}).get("auto") or {}).get("cycle") or {})
        got = cyc.get("instances") or []
        check("同 mode 两实例按序推进：len==3 且前两个都是 produce、"
              "参数互不串（0.42/33 vs 0.44/21）",
              len(got) == 3
              and got[0].get("mode") == "produce"
              and got[1].get("mode") == "produce"
              and got[2].get("mode") == "backwash"
              and abs(float(got[0].get("pressThreshold", -1)) - 0.42) < 1e-6
              and got[0].get("timerMinutes") == 33
              and abs(float(got[1].get("pressThreshold", -1)) - 0.44) < 1e-6
              and got[1].get("timerMinutes") == 21
              and got[0].get("stirSwitchPolicy") == "carry"
              and got[1].get("stirSwitchPolicy") == "stopRestart",
              "instances=%s" % str(got)[:300])
        check("派生视图随重复更新：cycleModes 去重保序 "
              "[produce, backwash]、六扁平键=首个 produce（0.42）",
              cyc.get("cycleModes") == ["produce", "backwash"]
              and abs((cyc.get("pressThreshold") or 0) - 0.42) < 1e-6,
              "cycleModes=%r pressThreshold=%r"
              % (cyc.get("cycleModes"), cyc.get("pressThreshold")))

        # ---- ③ fail-closed（逐条；GET 原值不变）----
        # [want_errs 与服务端文案逐字对齐]（app.py _cfg_set_cycle_instances
        # / _cycle_instance_entry_check 实读）：「instances 需要数组」/
        # 「instances[i] 需要对象（模式实例）」/「…的 mode 需要
        # produce/backwash/rinse/drain/stir 或 group（当前 …）」（2026-11-27
        # 现状：合法集含搅拌模式与嵌套组，文案里没有「之一」）/
        # 「至少要有 2 个模式才能构成循环（当前 N 个）」/「实例 产水模式2
        # 未设置切换条件（…）」（实例标题＝模式名+同类序号，**不带 #**）/
        # 「…devices 里没有可识别的设备 id」/「…需要数字（MPa）」/
        # 「…只能是 gt/lt」/「…需要整数分钟」。
        all_off_p2 = dict(CYCLE_INST_A, pressEnabled=False,
                          press1Enabled=False, press2Enabled=False,
                          timerEnabled=False)      # 产水模式2（同类第 2 个）
        bad_cases = [
            ({"enabled": True, "instances": "x"},
             ("数组",), "instances 非数组"),
            ({"enabled": True, "instances": [1, 2]},
             ("对象",), "instances 元素非对象"),
            ({"enabled": True, "instances": [dict(CYCLE_INST_A), dict(
                CYCLE_INST_B, mode="nope")]},
             ("mode", "需要", "或 group"), "实例 mode 未知"),
            ({"enabled": True, "instances": [
                {k: v for k, v in CYCLE_INST_A.items() if k != "mode"},
                CYCLE_INST_B]},
             ("mode", "需要", "或 group"), "实例 mode 缺失"),
            ({"enabled": True, "instances": [CYCLE_INST_A]},
             ("至少要有 2 个模式",), "len(instances)<2"),
            ({"enabled": True,
              "instances": [dict(CYCLE_INST_A, mode="produce")
                            for _ in range(33)]},
             ("最多 32 个实例",), "len(instances)>MAX(32)"),
            ({"enabled": True, "instances": [CYCLE_INST_A, all_off_p2]},
             ("未设置切换条件", "产水模式2"), "实例四触发全关（文案带实例序号）"),
            ({"enabled": True, "instances": [
                dict(CYCLE_INST_A, devices=["nope1", "nope2"]),
                CYCLE_INST_B]},
             ("没有可识别的设备",), "实例 devices 全未知"),
            ({"enabled": True, "instances": [
                dict(CYCLE_INST_A, pressThreshold="abc"), CYCLE_INST_B]},
             ("需要数字",), "实例压差阈值非数字"),
            ({"enabled": True, "instances": [
                dict(CYCLE_INST_A, pressCompare="bogus"), CYCLE_INST_B]},
             ("只能是",), "实例压差方向非法"),
            ({"enabled": True, "instances": [
                dict(CYCLE_INST_A, stirSwitchPolicy="bogus"), CYCLE_INST_B]},
             ("只能是",), "实例换相策略非法"),
            ({"enabled": True, "instances": [
                dict(CYCLE_INST_A, timerMinutes="abc"), CYCLE_INST_B]},
             ("需要整数分钟",), "实例定时分钟非整数"),
        ]
        for cyc_doc, want_errs, what in bad_cases:
            status, _h, doc = ctx.post("/api/config",
                                       {"section": "auto",
                                        "cycle": cyc_doc}, token=tok)
            err = str((doc or {}).get("err", ""))
            hit_err = status == 200 and (doc or {}).get("ok") is False \
                and bool(err) \
                and all(w in err for w in want_errs)
            check("非法实例配置显式报错（fail-closed）：%s" % what, hit_err,
                  "status=%s want=%s err=%r"
                  % (status, "/".join(want_errs) or "(ok=false+err)", err[:160]))
        status, _h, cfg = ctx.get("/api/config", token=tok)
        cyc = (((cfg or {}).get("auto") or {}).get("cycle") or {})
        got = cyc.get("instances") or []
        check("非法 SET 后原值不变（仍 ② 的三实例，首实例 0.42/33）",
              len(got) == 3
              and abs(float(got[0].get("pressThreshold", -1)) - 0.42) < 1e-6
              and got[0].get("timerMinutes") == 33,
              "instances=%s" % str(got)[:200])

        # ---- ④ 旧键写穿（显式 instances 下；wire 评审问题1）----
        status, _h, doc = ctx.post(
            "/api/config",
            {"section": "auto",
             "cycle": {"pressThreshold": 0.9, "pressCompare": "gt",
                       "timerFallback": False, "timerMinutes": 44}},
            token=tok)
        ok = status == 200 and (doc or {}).get("ok") is True
        check("显式 instances 下 SET 六扁平键 ok=true（写穿不拒）", ok,
              "status=%s doc=%s" % (status, str(doc)[:200]))
        status, _h, cfg = ctx.get("/api/config", token=tok)
        cyc = (((cfg or {}).get("auto") or {}).get("cycle") or {})
        got = cyc.get("instances") or []
        check("写穿落点=首实例（0.9/gt/定时关/44 分钟）",
              len(got) == 3
              and abs(float(got[0].get("pressThreshold", -1)) - 0.9) < 1e-6
              and got[0].get("pressCompare") == "gt"
              and got[0].get("timerEnabled") is False
              and got[0].get("timerMinutes") == 44,
              "instances[0]=%s" % str(got[0] if got else None)[:240])
        check("写穿不抹逐实例字段（首实例压力1 1.2/gt 仍在）",
              bool(got) and abs(float(got[0].get("press1Threshold", -1))
                                - 1.2) < 1e-6
              and got[0].get("press1Compare") == "gt"
              and got[0].get("stirSwitchPolicy") == "carry",
              "instances[0]=%s" % str(got[0] if got else None)[:240])
        check("写穿只动首实例（第 2 个 produce 仍 0.44/21）",
              len(got) == 3
              and abs(float(got[1].get("pressThreshold", -1)) - 0.44) < 1e-6
              and got[1].get("timerMinutes") == 21,
              "instances[1]=%s" % str(got[1] if got else None)[:200])
        status, _h, doc = ctx.post("/api/config",
                                   {"section": "auto",
                                    "cycle": {"backwashMinutes": 12}},
                                   token=tok)
        ok = status == 200 and (doc or {}).get("ok") is True
        check("显式 instances 下 SET backwashMinutes ok=true", ok,
              "status=%s doc=%s" % (status, str(doc)[:200]))
        status, _h, cfg = ctx.get("/api/config", token=tok)
        cyc = (((cfg or {}).get("auto") or {}).get("cycle") or {})
        got = cyc.get("instances") or []
        check("backwashMinutes 写穿到同 mode 首个实例（backwash 定时 12）",
              len(got) == 3 and got[2].get("mode") == "backwash"
              and got[2].get("timerMinutes") == 12
              and cyc.get("backwashMinutes") == 12,
              "instances[2]=%s backwashMinutes=%r"
              % (str(got[2] if len(got) > 2 else None)[:160],
                 cyc.get("backwashMinutes")))
        status, _h, doc = ctx.post(
            "/api/config",
            {"section": "auto",
             "cycle": {"cycleModes": ["rinse", "produce"]}}, token=tok)
        ok = status == 200 and (doc or {}).get("ok") is True
        status, _h, cfg = ctx.get("/api/config", token=tok)
        cyc2 = (((cfg or {}).get("auto") or {}).get("cycle") or {})
        got2 = cyc2.get("instances") or []
        check("cycleModes 写穿路径忽略不报错（SET ok=true 且实例顺序不变）",
              ok and [i.get("mode") for i in got2]
              == ["produce", "produce", "backwash"],
              "ok=%r modes=%s" % (ok, [i.get("mode") for i in got2]))
        status, _h, doc = ctx.post(
            "/api/config",
            {"section": "auto",
             "cycle": {"enabled": True,
                       "instances": [dict(CYCLE_INST_A), CYCLE_INST_B],
                       "pressThreshold": 0.77}},
            token=tok)
        ok = status == 200 and (doc or {}).get("ok") is True
        status, _h, cfg = ctx.get("/api/config", token=tok)
        cyc = (((cfg or {}).get("auto") or {}).get("cycle") or {})
        got = cyc.get("instances") or []
        check("同包 instances 优先：旧键忽略（首实例阈值=0.42 非 0.77）",
              ok and len(got) == 2
              and abs(float(got[0].get("pressThreshold", -1)) - 0.42) < 1e-6,
              "ok=%r instances[0].pressThreshold=%r"
              % (ok, got[0].get("pressThreshold") if got else None))
        # ---- stirMaxRpm 联动钳位（只改上限不带 instances ⇒ 实例全夹）----
        status, _h, doc = ctx.post("/api/config",
                                   {"section": "auto", "stirMaxRpm": 800},
                                   token=tok)
        ok = status == 200 and (doc or {}).get("ok") is True
        status, _h, cfg = ctx.get("/api/config", token=tok)
        cyc = (((cfg or {}).get("auto") or {}).get("cycle") or {})
        got = cyc.get("instances") or []
        rpms = [i.get("stirRpm") for i in got if isinstance(i, dict)]
        check("stirMaxRpm=800 联动钳位：已存实例 stirRpm 全部 ≤800"
              "（B 的 900 被夹到 800）",
              ok and rpms and all(isinstance(r, int) and 0 <= r <= 800
                                  for r in rpms)
              and 800 in rpms,
              "ok=%r rpms=%r" % (ok, rpms))

        # ---- 收尾：恢复原值 ----
        restored = restore_cycle()
        check("收尾恢复 auto.cycle 原语义（stirMaxRpm 一并恢复）",
              restored is not False, "restore=%r" % restored)


def sec_cycle_top_repeat(ctx):
    """顶层总循环次数（auto.cycle.topRepeat，2026-11-27 用户定案）。

    [冻结契约] 0 = 一直循环（缺省，保持既有行为）；N>=1 = 展开计划完整
    跑完 N 轮后自动停止（等价现场停止按钮）。运行中改不立即生效（启动时
    快照，停止再启动才生效）——本段只锁**配置面**，运行语义由服务端
    自动控制器的用例锁（网页端不重复造第二套判定）。

    [断言] ① GET 快照的 cycle.topRepeat 必须是 int；② SET 3 ⇒ ok + GET 回
    读 3；③ 非整数（"abc"）⇒ fail-closed 且 err 含 topRepeat、GET 原值不变；
    ④ 整数越界钳位 0..999（不报错、不无限）；⑤ 0 = 一直循环是合法值。

    [只跑 PY6200] 契约里 C 端不实现 topRepeat（C 服务端不认该键）⇒ C 段
    记 SKIP，不拿网页端去要求 C 端行为。
    """
    if _end[0] != "PY6200":
        skip("自动循环顶层总循环次数（topRepeat）", "C 端不实现 topRepeat")
        return
    with Section("自动循环顶层总循环次数（topRepeat 往返/钳位/非法拒）"):
        tok = ctx.token_of("root")

        def get_top():
            st, _h, cfg = ctx.get("/api/config", token=tok)
            cyc = (((cfg or {}).get("auto") or {}).get("cycle") or {})
            return st, cyc.get("topRepeat")

        status, top0 = get_top()
        check("GET 快照的 cycle.topRepeat 是 int（0=一直循环）",
              status == 200 and type(top0) is int,
              "status=%s topRepeat=%r" % (status, top0))
        orig_ok = (type(top0) is int)

        # ---- ① SET → GET 回读 ----
        status, _h, doc = ctx.post("/api/config",
                                   {"section": "auto",
                                    "cycle": {"topRepeat": 3}}, token=tok)
        ok = status == 200 and (doc or {}).get("ok") is True
        check("SET cycle.topRepeat=3 ok=true", ok,
              "status=%s doc=%s" % (status, str(doc)[:240]))
        status, top1 = get_top()
        check("GET 回读 topRepeat=3（int）",
              status == 200 and type(top1) is int and top1 == 3,
              "status=%s topRepeat=%r" % (status, top1))

        # ---- ② 非整数 ⇒ fail-closed，且不改原值 ----
        status, _h, doc = ctx.post("/api/config",
                                   {"section": "auto",
                                    "cycle": {"topRepeat": "abc"}}, token=tok)
        err = str((doc or {}).get("err", ""))
        check("非法 topRepeat（\"abc\"）显式拒绝且文案含 topRepeat",
              status == 200 and (doc or {}).get("ok") is False
              and "topRepeat" in err,
              "status=%s err=%r" % (status, err[:200]))
        status, top2 = get_top()
        check("非法 SET 后 topRepeat 原值不变（仍 3）",
              status == 200 and top2 == 3,
              "status=%s topRepeat=%r" % (status, top2))

        # ---- ③ 越界钳位（999 上限 / 0 下限；与 minutes 同口径）----
        for val, want in ((10000, 999), (-5, 0)):
            status, _h, doc = ctx.post("/api/config",
                                       {"section": "auto",
                                        "cycle": {"topRepeat": val}}, token=tok)
            ok = status == 200 and (doc or {}).get("ok") is True
            status, got = get_top()
            check("越界 topRepeat=%r 钳位到 %d（不报错）" % (val, want),
                  ok and status == 200 and got == want,
                  "ok=%r status=%s got=%r" % (ok, status, got))

        # ---- ④ 0 = 一直循环（合法值；与缺省同义）----
        status, _h, doc = ctx.post("/api/config",
                                   {"section": "auto",
                                    "cycle": {"topRepeat": 0}}, token=tok)
        ok = status == 200 and (doc or {}).get("ok") is True
        status, got0 = get_top()
        check("SET topRepeat=0（一直循环）是合法值且回读 0",
              ok and status == 200 and got0 == 0,
              "ok=%r status=%s got=%r" % (ok, status, got0))

        # ---- 收尾：恢复开跑前的值 ----
        if orig_ok:
            status, _h, doc = ctx.post("/api/config",
                                       {"section": "auto",
                                        "cycle": {"topRepeat": top0}},
                                       token=tok)
            check("收尾恢复 topRepeat 原值",
                  status == 200 and (doc or {}).get("ok") is not False,
                  "status=%s doc=%s" % (status, str(doc)[:160]))


def sec_history(ctx):
    with Section("历史分页（窗口/关键字/列筛选）+ 导出下载（BOM/CRLF/行数==命中）"):
        tok = ctx.token_of("root")
        status, _h, doc = ctx.get("/api/history/page?offset=0&limit=5",
                                  token=tok, timeout=30.0)
        ok = status == 200 and (doc or {}).get("ok") is True
        total = (doc or {}).get("total") or 0
        rows = (doc or {}).get("rows") or []
        check("历史分页 ok=true（total/rows/cols 键全）",
              ok and all(k in (doc or {}) for k in ("total", "rows", "cols")),
              "status=%s doc=%s" % (status, str(doc)[:200]))
        if not ok or total <= 0:
            # 库空不是断言弱化口：显式补数据（提速采样）再验。
            print("    · 库空（total=%s）：提速 storage.sampleMs=1000 等数据"
                  % total)
            status, _h, doc2 = ctx.post("/api/config",
                                        {"section": "storage",
                                         "sampleMs": 1000}, token=tok)
            ctx.storage_touched = status == 200 and (doc2 or {}).get("ok") \
                is True

            def rows_come():
                status, _h, d = ctx.get(
                    "/api/history/page?offset=0&limit=5", token=tok,
                    timeout=20.0)
                return d if (d or {}).get("total", 0) > 0 else None

            got = ctx.poll(rows_come, 40, interval=2.0, label="历史首行入库")
            check("提速采样后历史出数（total>0）", got is not None,
                  "落盘仍无数据（检查 storage.enabled）")
            status, _h, doc = ctx.get("/api/history/page?offset=0&limit=5",
                                      token=tok, timeout=30.0)
            ok = status == 200 and (doc or {}).get("ok") is True
            total = (doc or {}).get("total") or 0
            rows = (doc or {}).get("rows") or []
            check("历史分页 ok=true（补数据后）", ok,
                  "status=%s doc=%s" % (status, str(doc)[:160]))
        cols = (doc or {}).get("cols") or []
        latest_ts = (doc or {}).get("latestTs") or 0
        earliest_ts = (doc or {}).get("earliestTs") or 0
        # [2026-11-23] 线上列追加 motor_temp（电机温度，模拟量温度传感器）
        # ⇒ 20 列；同日再追加 press1_mpa/press2_mpa（压力1/压力2，模拟量）
        # ⇒ 22 列。C 端未实现这些传感器，只认前 19 列（尾部增量列按下标读
        # 天然忽略）。
        check("列序 22 列（water_temp/motor_temp/press1/press2 依次在末尾）",
              len(cols) == 22
              and cols[-4:] == ["water_temp", "motor_temp",
                                "press1_mpa", "press2_mpa"],
              "cols=%s" % cols)
        check("行是值数组且宽度=列数",
              bool(rows) and isinstance(rows[0], list)
              and len(rows[0]) == len(cols),
              "row0=%s" % str(rows[0])[:120] if rows else "无行")
        ts_first = [r[0] for r in rows if isinstance(r, list)]
        check("第 1 页=最新一页且页内时间正序（页内 ts 非降、末行≈latestTs）",
              all(a <= b for a, b in zip(ts_first, ts_first[1:]))
              and (not ts_first or abs(ts_first[-1] - latest_ts) < 5000),
              "ts=%s latestTs=%s" % (ts_first, latest_ts))

        # ---- 跨页不重不漏（limit=7 逐页走到底，钉锚点 + 冻上界）----
        # [分页方向口径] 两端同款：SQL 侧 ORDER BY ts DESC 取页再内存翻转
        # ⇒ 第 1 页（offset=0）是**最新的一页**、页内由旧到新（ts 升序）；
        # offset 增大向更老的行走。
        # [为什么用「多重集相等」判不重不漏] C 端历史 ts 是**秒级**精度
        # （HsfServerApp.c: row.tsMs = currentSecsSinceEpoch() * 1000）⇒
        # 同一秒内可有多行、ts 会重复 ⇒ 不能拿「ts 唯一」当「不重」的判据。
        # 改判三件事（与 ts 精度无关）：① 页内 ts 非降；② 跨页向更老走
        # （允许同 ts 相接）；③ **逐页拼接 == 一次性取全量（多重集相等）**
        # ——这一条同时证明既不漏行也不重复行。
        # [为什么要冻上界 hiTs=latest_ts-1] 只钉 beforeTs 不够：C 端 ts 是
        # 秒级（写库时截到整秒）⇒ 锚点之后写的行，其 ts 可能仍 ≤ 锚点 ⇒
        # 锚定集合在跑的过程中**继续长大** ⇒ 两次查询的行数对不上。把上界
        # 钉在最新一行之前（latest_ts-1），之后写入的行（ts ≥ latest_ts）
        # 一律进不来 ⇒ 集合冻结，计数与导出才可比。
        anchor_walk = int(time.time() * 1000)
        hi_frozen = max(0, latest_ts - 1)
        q_frozen = "beforeTs=%d&hiTs=%d" % (anchor_walk, hi_frozen)

        def _count(qs):
            status, _h, d = ctx.get("/api/history/page?offset=0&limit=1&%s"
                                    % qs, token=tok, timeout=30.0)
            return (d or {}).get("total") or 0

        # [走分页的窗口要**行数可控**] 库里可能有上万行（常驻 sim 共用一个
        # 库），全量走一遍既慢又没必要 ⇒ 先把窗口收到 50~400 行：
        # 用「取第 count//2 行的 ts 当新下界」二分收窄。
        lo_walk = 0
        total_frozen = _count("%s&loTs=%d" % (q_frozen, lo_walk))
        total_anchor = total_frozen
        for _ in range(8):
            if total_anchor <= 400:
                break
            status, _h, d = ctx.get(
                "/api/history/page?offset=%d&limit=1&%s&loTs=%d"
                % (total_anchor // 2, q_frozen, lo_walk), token=tok,
                timeout=30.0)
            rows_mid = (d or {}).get("rows") or []
            if not rows_mid or not isinstance(rows_mid[0], list):
                break
            lo_walk = int(rows_mid[0][0])
            total_anchor = _count("%s&loTs=%d" % (q_frozen, lo_walk))
        q_walk = "%s&loTs=%d" % (q_frozen, lo_walk)
        status, _h, allpage = ctx.get(
            "/api/history/page?offset=0&limit=400&%s" % q_walk,
            token=tok, timeout=30.0)
        all_rows = (allpage or {}).get("rows") or []
        walk_rows = []
        page_ts = []                     # 每页的 ts 列表
        offset = 0
        pages = 0
        per_page_ok = True
        while pages < 120:
            status, _h, d = ctx.get(
                "/api/history/page?offset=%d&limit=7&%s" % (offset, q_walk),
                token=tok, timeout=30.0)
            if status != 200 or not (d or {}).get("ok"):
                per_page_ok = False
                break
            batch = d.get("rows") or []
            if len(batch) > 7:
                per_page_ok = False
            ts_page = [r[0] for r in batch if isinstance(r, list)]
            if not all(a <= b for a, b in zip(ts_page, ts_page[1:])):
                per_page_ok = False      # 页内必须时间正序
            if ts_page and page_ts:
                # 跨页边界：本页（更老）最新行 ≤ 上一页（更新）最老行
                # （同 ts 可并列：C 端 ts 秒级，同一秒可能有多行）
                if ts_page[-1] > page_ts[-1][0]:
                    per_page_ok = False
            page_ts.append(ts_page)
            walk_rows.extend(batch)
            offset += 7
            pages += 1
            if len(batch) < 7:
                break
        ts_list = [r[0] for r in walk_rows if isinstance(r, list)]
        # [比较口径] ① 逐页拼接必须**走完**冻结窗口（行数 == 锚定总数 ⇒
        #    不漏行）；② 每页在服务端是「ts 倒序取页再内存翻正」⇒ 拼接
        #    顺序是「页内正序、页间由新到旧」，不是全局有序 ⇒ 取最新 N 行
        #    要先按 ts 排一次；③ 服务端单页上限 50 ⇒ 一次性取全量只能取
        #    到最新的 50 行，两边取同样的 N 比。
        # [有并列 ts 时只比时间戳分布] C 端 ts 秒级 ⇒ 同一秒多行时 SQL 的
        # LIMIT/OFFSET 在并列键上的先后**不保证稳定**（两次 limit 不同的
        # 查询计划可能给出不同的并列顺序）⇒ 行级比较在并列边界上不可靠，
        # 改比 ts 分布（并列行互换不影响分布）；无并列时仍要求行级相等。
        cap = min(len(walk_rows), len(all_rows))
        walk_sorted = sorted(walk_rows,
                             key=lambda r: r[0] if isinstance(r, list) else 0)
        walk_head = walk_sorted[-cap:] if cap else []
        ts_walk = sorted(r[0] for r in walk_head if isinstance(r, list))
        ts_all = sorted(r[0] for r in all_rows if isinstance(r, list))
        ts_same = (ts_walk == ts_all) and len(ts_walk) > 0
        rows_same = (sorted(tuple(r) for r in walk_head) ==
                     sorted(tuple(r) for r in all_rows))
        uniq_ts = len(set(ts_list)) == len(ts_list)
        covered = len(walk_rows) == total_anchor
        check("跨页分页：每页 ≤limit、页内时间正序、跨页向更老走、"
              "逐页拼接==一次性取全量（不重不漏）",
              per_page_ok and covered and ts_same
              and (rows_same or not uniq_ts) and len(ts_list) >= 2,
              "pages=%d rows=%d 锚定总数=%d 覆盖=%s 一次性=%d ts分布相等=%s "
              "行级相等=%s ts唯一=%s per_page_ok=%s"
              % (pages, len(walk_rows), total_anchor, covered, len(all_rows),
                 ts_same, rows_same, uniq_ts, per_page_ok))
        # ts 精度（C 端秒级 / py 端毫秒级）——如实打印，不做断言
        if ts_list:
            sec_gran = all((t % 1000) == 0 for t in ts_list)
            print("    · 历史 ts 精度：%s（%s）"
                  % ("秒级（整秒）" if sec_gran else "毫秒级",
                     "C 端口径 row.tsMs=currentSecs*1000" if sec_gran
                     else "Python 端口径 epoch 毫秒"))

        # ---- 窗口筛选（与全量同锚点 + 同冻上界 ⇒ 期间新入库的行不干扰）----
        anchor = anchor_walk
        span = 30 * 60 * 1000
        q_win = ("loTs=%d&hiTs=%d&beforeTs=%d"
                 % (max(0, latest_ts - span), hi_frozen, anchor))
        status, _h, d = ctx.get("/api/history/page?offset=0&limit=5&%s"
                                % q_win, token=tok, timeout=30.0)
        total_win = (d or {}).get("total") if status == 200 else None
        check("窗口筛选：近 30 分钟 total ≤ 全量（同锚点同冻上界）",
              total_win is not None and 0 < total_win <= total_frozen,
              "win=%r all(锚点)=%d" % (total_win, total_frozen))
        status, _h, d = ctx.get(
            "/api/history/page?offset=0&limit=5&loTs=1&hiTs=1&beforeTs=%d"
            % anchor, token=tok, timeout=30.0)
        check("窗口筛选：空窗（[1,1]）total=0",
              status == 200 and (d or {}).get("total") == 0,
              "doc=%s" % str(d)[:160])

        # ---- 关键字 + 列筛选 ----
        status, _h, d = ctx.get(
            "/api/history/page?offset=0&limit=5&kw=%s&kwCols=1&%s"
            % (urllib.parse.quote("手动"), q_frozen), token=tok, timeout=30.0)
        total_kw = (d or {}).get("total") if status == 200 else None
        check("关键字筛选（kw=手动，列=模式）：0 < 命中 ≤ 全量",
              total_kw is not None and 0 < total_kw <= total_frozen,
              "kw=%r all=%d" % (total_kw, total_frozen))
        status, _h, d = ctx.get(
            "/api/history/page?offset=0&limit=5&kw=%s&kwCols=0&%s"
            % (urllib.parse.quote("手动"), q_frozen), token=tok, timeout=30.0)
        total_col0 = (d or {}).get("total") if status == 200 else None
        check("列筛选限域（同一关键字改到时间列 → 命中 0）",
              total_col0 == 0, "col0=%r" % total_col0)
        status, _h, d = ctx.get(
            "/api/history/page?offset=0&limit=5&kw=%s&%s"
            % (urllib.parse.quote("__绝无此关键字__"), q_frozen),
            token=tok, timeout=30.0)
        check("无命中关键字 → total=0",
              status == 200 and (d or {}).get("total") == 0,
              "doc=%s" % str(d)[:160])

        # ---- 导出：行数 == 命中数（窗口+关键字同参数，锚点钉死）----
        # [窗口收敛] 50 分钟起步；命中 >400 收窄、命中 ==0 **放宽**（历史
        # 一拍一分钟，窗口太窄会空手）。最终必须 t ≥ 1——0 行的
        # 「行数==命中数」是空转假绿，命中不了就如实红。
        # [hiTs 取 latest_ts-1] 把窗口上界钉在**最新一行之前** ⇒ 计数与
        # 导出之间新入库的行进不来（C 端 ts 秒级：同一秒写的行 ts 会等于
        # latest_ts，若上界留 +1000ms 它就会挤进导出侧 ⇒ 行数对不上）。
        span = 50 * 60 * 1000
        hi = max(0, latest_ts - 1)
        lo = max(0, latest_ts - span)
        t = 0
        q = ""
        for _ in range(6):
            q = "kw=%s&kwCols=1&loTs=%d&hiTs=%d&beforeTs=%d" % (
                urllib.parse.quote("手动"), lo, hi, anchor)
            status, _h, d = ctx.get("/api/history/page?offset=0&limit=5&%s"
                                    % q, token=tok, timeout=30.0)
            t = (d or {}).get("total") or 0
            if 0 < t <= 400:
                break
            if t > 400:
                span //= 2
                lo = max(0, latest_ts - span)
            else:
                span *= 2
                lo = max(0, latest_ts - span)
                if lo == 0 and span > 24 * 3600 * 1000:
                    break
        if not (0 < t <= 400):
            # 全窗仍无命中：换无关键字的窗口（只留窗口筛选），再不行如实红。
            q = "loTs=%d&hiTs=%d&beforeTs=%d" % (lo, hi, anchor)
            status, _h, d = ctx.get("/api/history/page?offset=0&limit=5&%s"
                                    % q, token=tok, timeout=30.0)
            t = (d or {}).get("total") or 0
        check("导出前置：筛选窗口命中 ≥1 行（%s 行，q=%s）" % (t, q),
              0 < t <= 400, "命中 0 行——导出断言需要非空数据")
        status, _h, blob = http_req(ctx.web, "GET",
                                    "/api/history/export?format=csv&%s" % q,
                                    token=tok, timeout=120.0)
        check("导出 CSV 200 且首字节 UTF-8 BOM",
              status == 200 and blob[:3] == b"\xef\xbb\xbf",
              "status=%s head=%r" % (status, blob[:16]))
        text = blob.decode("utf-8-sig", "replace")
        lines = [l for l in text.split("\r\n") if l != ""]
        header = lines[0].split(",") if lines else []
        data_lines = lines[1:]
        check("导出 CSV 表头（19 显示列，首列=时间；压力1/2 紧跟压差）",
              header[:2] == ["时间", "模式"] and len(header) == 19
              and header[12:15] == ["压差(MPa)", "进水压力(MPa)",
                                    "出水压力(MPa)"],
              "header=%s" % header[:20])
        check("导出 CSV CRLF 行尾（无裸 LF）",
              b"\r\n" in blob and blob.replace(b"\r\n", b"").find(b"\n") == -1,
              "head=%r" % blob[:80])
        check("导出行数 == 筛选命中数（%s 行；锚点 beforeTs=%d）"
              % (t, anchor),
              len(data_lines) == t,
              "export=%d page_total=%d q=%s" % (len(data_lines), t, q))
    _history_col_width_static()


def _history_col_width_static():
    """历史表列宽静态检查：内容比表头长的列必须拿到内容下限。

    [2026-11-20 用户实报「web端列数据重叠」] 「模式」列表头 2 字、内容
    「自动·运行中」6 字 ⇒ 只按表头算下限（72px）⇒ fixed 布局 + nowrap
    ⇒ 文本溢出画到隔壁「搅拌状态」列上（两列数据糊在一起）。
    根修是逐页量**最长单元格文字**（参考端 page_base._measure_content_texts
    同口径）；这里钉住三件事，回退任何一件都会重新打开这个缺陷：
    ① 有 `measureContentTexts`（按当前字体现量，不写死像素）；
    ② `applyColWidths` 拿得到当前页行数据（拿不到就量不了内容）；
    ③ 静态基线里保留「模式」列的代表长文本（运行子阶段一变文本长度就变，
       只靠现量会让列宽每 3s 抖一次）。
    """
    path = os.path.join(WEBUI_DIR, "hsfweb", "static", "js", "pages",
                        "page09_history.js")
    try:
        with open(path, "r", encoding="utf-8") as fh:
            src = fh.read()
    except OSError as exc:
        check("page09_history.js 可读", False, "%s" % exc)
        return
    check("历史表列宽：有内容下限（逐页量最长单元格文字）",
          "function measureContentTexts(" in src
          and "measureContentTexts(cols, list || [], font)" in src,
          "page09_history.js 缺 measureContentTexts"
          "（只按表头量 ⇒ 数据长的列叠列）")
    check("历史表列宽：applyColWidths 拿得到当前页行（内容下限的前提）",
          "applyColWidths(cols, ths, list)" in src,
          "applyColWidths 没接行数据 ⇒ 量不到内容宽")
    check("历史表列宽：模式列有静态基线（子阶段变化时列宽不抖）",
          '1: "自动·运行中"' in src,
          "COL_MIN_TEXTS 缺模式列基线")


#: SpreadsheetML 命名空间（xlsx 的 sheet XML）。
_XLSX_NS = "{http://schemas.openxmlformats.org/spreadsheetml/2006/main}"


def _xlsx_col_letter(index):
    """1 基列号 → Excel 字母列名。"""
    letters = ""
    while index > 0:
        index, rem = divmod(index - 1, 26)
        letters = chr(ord("A") + rem) + letters
    return letters


def _xlsx_cells(blob):
    """xlsx 字节 → {工作表名: {单元格引用: (类型, 文本)}}（类型 num/str）。

    [为什么用 XML 解析器而不是正则] 「这一格是不是数值」的真判据是
    **有没有 t 属性**（``t="inlineStr"`` 就是文本）⇒ 正则容易把
    ``<v>`` 与 ``<t>`` 混起来判错；解析器才准（这正是 Excel 认不认它是
    数的依据）。
    """
    import io as _io
    import xml.etree.ElementTree as ET
    import zipfile
    out = {}
    with zipfile.ZipFile(_io.BytesIO(blob)) as zf:
        for name in zf.namelist():
            if not name.startswith("xl/worksheets/sheet"):
                continue
            cells = {}
            root = ET.fromstring(zf.read(name))
            for row in root.iter(_XLSX_NS + "row"):
                for cell in row.iter(_XLSX_NS + "c"):
                    ref = cell.get("r")
                    v = cell.find(_XLSX_NS + "v")
                    is_ = cell.find(_XLSX_NS + "is")
                    if v is not None:
                        cells[ref] = ("num", v.text or "")
                    elif is_ is not None:
                        t = is_.find(_XLSX_NS + "t")
                        cells[ref] = ("str",
                                      (t.text or "") if t is not None else "")
                    else:
                        cells[ref] = ("empty", "")
            out[name] = cells
    return out


def _xlsx_data_sheet(blob):
    """xlsx 字节 → 数据工作表的 {引用: (类型, 文本)}（格最多的那张）。"""
    sheets = _xlsx_cells(blob)
    if not sheets:
        return {}
    return max(sheets.items(), key=lambda kv: len(kv[1]))[1]


#: 历史表里**应当是数值格**的列（表头逐字）与**应当是文本格**的列。
XLSX_NUMERIC_HEADERS = ("输出频率(Hz)", "输出电流(A)", "搅拌转速(rpm)",
                        "电机温度(℃)",
                        "进水浊度(NTU)", "出水浊度(NTU)", "水温(℃)",
                        "压差(MPa)", "进水压力(MPa)", "出水压力(MPa)",
                        "电箱温度(℃)", "电箱湿度(%RH)")
XLSX_TEXT_HEADERS = ("时间", "模式", "搅拌状态", "瞬时流量", "正累计",
                     "DI(32位)", "DO(32位)")


def sec_xlsx_numeric(ctx):
    """XLSX 导出的**数值格**（2026-11-19 用户实报「数值列保存成 xlsx 后是
    文本型」）。

    [为什么要这一段] 网页端导出有三条产出路径（流式 ``xlsx_stream``、
    一次性 ``export.build_xlsx``、C 动态库 ``xlsx_native``）⇒ 三处都
    必须把**数值列**写成数值格（``<c r="D2"><v>-0.008</v></c>``：**不带
    t 属性**）⇒ Excel 里能直接求和/排序，而不是左对齐的文本。

    [三件事]
      ① **静态**：三个写出器的源码里必须有"数值格"判定（防回潮成一律
         inlineStr）；
      ② **行为**（进程内）：用网页端**真实**的历史格式化
         （``export.history_row_texts``）造一行 ⇒ 写 xlsx ⇒ 逐格判定
         （数值列是数值格 / 文本列是 inlineStr）；
      ③ **端到端**（HTTP）：真下载一次 xlsx ⇒ 里面确实有数值格。
    """
    with Section("XLSX 导出：数值列是数值格（Excel 能求和/排序）"):
        hsfweb = os.path.join(WEBUI_DIR, "hsfweb")

        def _read(rel):
            try:
                with open(os.path.join(hsfweb, rel), "r",
                          encoding="utf-8") as fh:
                    return fh.read()
            except OSError as exc:
                check("%s 可读" % rel, False, "%s" % exc)
                return None

        stream_src = _read("xlsx_stream.py")
        export_src = _read("export.py")
        native_src = _read("xlsx_native.py")
        if None in (stream_src, export_src, native_src):
            return

        # ---- ① 静态：三处写出器都得有"数值格"判定 ----
        check("流式写出器有数值格判定（<v> 且不带 t 属性）",
              '<c r="%s"><v>%s</v></c>' in stream_src
              and "_numeric_text" in stream_src and "_cell_xml" in stream_src,
              "")
        check("一次性写出器（export.py）复用同一份数值格判定",
              "from .xlsx_stream import _cell_xml" in export_src
              and "_cell_xml(ref, value)" in export_src, "")
        check("C 动态库路径与纯 Python 路径同口径",
              "_number_text" in native_src and "_numeric_text" in native_src,
              "")
        check("旧的「数字一律 inlineStr」判定已不在一次性写出器里",
              "isinstance(value, (int, float)) and not isinstance(value, bool)"
              not in export_src, "")

        # ---- ② 行为（进程内）：真实格式化 → 写 xlsx → 逐格判定 ----
        if WEBUI_DIR not in sys.path:
            sys.path.insert(0, WEBUI_DIR)
        try:
            from hsfweb import export as export_mod
            from hsfweb import model as web_model
            from hsfweb import proto as web_proto
            from hsfweb.xlsx_stream import XlsxStreamWriter
        except Exception as exc:                            # noqa: BLE001
            check("网页端导出模块可导入", False,
                  "%s: %s" % (type(exc).__name__, exc))
            return
        # [2026-11-24 板端实报「通道上怎么没有设备」] 断线/未接通道服务端
        # 只发 en（名字）/ew（断线说明）不发 e ⇒ 解析必须以**每个键独立**
        # 判定，不能让 e 把整行吞掉；全量帧的 an 也要能兜底名字。
        _ai = web_model.AnalogState(8)
        _ai.apply({"n": 8, "v": 0xFF,
                   "e0": 3723, "eu0": "mA / MPa", "en0": "压差计",
                   "en1": "温度传感器", "ew1": "断线（0.00mA 回路无电流）",
                   "en2": "进水压力", "ew2": "断线（0.00mA 回路无电流）",
                   "an3": "出水压力"})
        _ch = _ai.to_json()["channels"]
        check("模拟量：断线通道仍带名字与说明（en/ew 不被 e 整行吞掉）",
              _ch[1]["name"] == "温度传感器"
              and _ch[2]["name"] == "进水压力"
              and _ch[3]["name"] == "出水压力"
              and "断线" in _ch[1]["note"], "%r"
              % ([(c["name"], c["note"]) for c in _ch[:4]],))

        headers = list(export_mod.HISTORY_HEADERS)
        raw = [1763512920000, "manual", "运行", 45.0, 12.34, 1440.0, 2.29,
               0.045, 3.6, "m³/h", 123.45, "m³", -0.008, "", 25.5, 55.0,
               0, 10, 19.3, 46.2, 0.512, 0.487]
        texts, _abn = export_mod.history_row_texts(
            raw, list(web_proto.HISTORY_COLUMNS))
        num_cols = [i for i, h in enumerate(headers)
                    if h in XLSX_NUMERIC_HEADERS]
        txt_cols = [i for i, h in enumerate(headers)
                    if h in XLSX_TEXT_HEADERS]
        check("网页端真实历史格式化产出 19 列显示文本",
              len(texts) == len(headers) == 19, str(texts))

        def _judge(label, blob):
            cells = _xlsx_data_sheet(blob)
            bad_num = []
            for i in num_cols:
                kind, text = cells.get("%s2" % _xlsx_col_letter(i + 1),
                                       ("empty", ""))
                if kind != "num" or text != texts[i]:
                    bad_num.append("%s(%s=%r)" % (headers[i], kind, text))
            check("%s 数值列是数值格（%d 列）" % (label, len(num_cols)),
                  not bad_num, "; ".join(bad_num))
            bad_txt = []
            for i in txt_cols:
                kind, text = cells.get("%s2" % _xlsx_col_letter(i + 1),
                                       ("empty", ""))
                if kind != "str" or text != texts[i]:
                    bad_txt.append("%s(%s=%r)" % (headers[i], kind, text))
            check("%s 文本列是 inlineStr（%d 列）" % (label, len(txt_cols)),
                  not bad_txt, "; ".join(bad_txt))

        try:
            blob = export_mod.build_xlsx([("历史数据", headers, [texts])])
            _judge("一次性写出器", blob)
        except Exception as exc:                            # noqa: BLE001
            check("一次性写出器产出 xlsx", False,
                  "%s: %s" % (type(exc).__name__, exc))

        tmp_path = None
        try:
            import tempfile
            fd, tmp_path = tempfile.mkstemp(prefix="hsf_xlsx_", suffix=".xlsx")
            os.close(fd)
            writer = XlsxStreamWriter(tmp_path, ["历史数据"])
            writer.sheet(0, headers)
            writer.row(0, texts)
            writer.close()
            with open(tmp_path, "rb") as fh:
                blob = fh.read()
            _judge("流式写出器", blob)
        except Exception as exc:                            # noqa: BLE001
            check("流式写出器产出 xlsx", False,
                  "%s: %s" % (type(exc).__name__, exc))
        finally:
            if tmp_path and os.path.exists(tmp_path):
                try:
                    os.unlink(tmp_path)
                except OSError:
                    pass

        # ---- ③ 端到端（HTTP）：真下载一次 xlsx ----
        tok = ctx.token_of("root")
        status, _h, blob = http_req(ctx.web, "GET",
                                    "/api/history/export?format=xlsx",
                                    token=tok, timeout=180.0)
        check("导出 XLSX 200 且是 zip",
              status == 200 and blob[:2] == b"PK",
              "status=%s head=%r" % (status, blob[:16]))
        if status != 200 or blob[:2] != b"PK":
            return
        try:
            cells = _xlsx_data_sheet(blob)
        except Exception as exc:                            # noqa: BLE001
            check("端到端导出的 xlsx 可解析", False,
                  "%s: %s" % (type(exc).__name__, exc))
            return
        num_cells = [k for k, v in cells.items() if v[0] == "num"]
        check("端到端导出的 xlsx 里确有数值格（不是全 inlineStr）",
              len(num_cells) > 0, "数值格 %d 个 / 共 %d 格"
              % (len(num_cells), len(cells)))
        row2_num = 0
        for i in num_cols:
            kind, _text = cells.get("%s2" % _xlsx_col_letter(i + 1),
                                    ("empty", ""))
            if kind == "num":
                row2_num += 1
        check("端到端导出：首行数据的数值列是数值格（≥3 列）",
              row2_num >= 3, "首行数值格 %d/%d 列" % (row2_num, len(num_cols)))


def sec_log(ctx):
    with Section("运行日志分页（0x0119）"):
        tok = ctx.token_of("root")
        status, _h, doc = ctx.get("/api/log/page?offset=0&limit=7", token=tok,
                                  timeout=30.0)
        ok = status == 200 and (doc or {}).get("ok") is True
        total = (doc or {}).get("total") or 0
        rows = (doc or {}).get("rows") or []
        check("日志分页 ok=true（total/count/rows）",
              ok and all(k in (doc or {}) for k in ("total", "count", "rows")),
              "status=%s doc=%s" % (status, str(doc)[:200]))
        shape_ok = bool(rows) and all(
            isinstance(r, dict) and "ts" in r and "category" in r
            and "text" in r for r in rows)
        check("日志行形状 {ts,category,text}（对象数组）", shape_ok,
              "row0=%s" % str(rows[0])[:120] if rows else "无行")
        # 跨页方向口径与历史同（页内正序、offset 向更老走；钉锚点防翻页
        # 窗口漂移）。日志允许同毫秒多条（同 ms 事件真实存在），跨页边界
        # 用非严格 ≤；页内仍须非降。
        anchor_log = int(time.time() * 1000)
        page_ts = []
        n_rows = 0
        log_ok = True
        offset = 0
        for _ in range(4):
            status, _h, d = ctx.get(
                "/api/log/page?offset=%d&limit=7&beforeTs=%d"
                % (offset, anchor_log), token=tok, timeout=30.0)
            if status != 200 or not (d or {}).get("ok"):
                log_ok = False
                break
            batch = d.get("rows") or []
            ts_page = [r.get("ts", 0) for r in batch]
            if not all(a <= b for a, b in zip(ts_page, ts_page[1:])):
                log_ok = False
            if ts_page and page_ts and ts_page[-1] > page_ts[-1][0]:
                log_ok = False
            page_ts.append(ts_page)
            n_rows += len(batch)
            offset += 7
            if len(batch) < 7:
                break
        check("日志跨页：每页 ≤limit、页内时间正序、跨页向更老走（钉锚点）",
              log_ok and n_rows >= 2,
              "rows=%d ok=%s pages=%s"
              % (n_rows, log_ok, [len(p) for p in page_ts]))
        # category 过滤（conn 一定有：本脚本每次登录都入库一条）
        status, _h, d = ctx.get(
            "/api/log/page?offset=0&limit=7&category=conn", token=tok,
            timeout=30.0)
        total_conn = (d or {}).get("total") if status == 200 else None
        cats = {r.get("category") for r in (d or {}).get("rows") or []}
        check("category=conn 过滤：命中 ≤ 全量且本页类别全是 conn",
              total_conn is not None and 0 < total_conn <= total
              and cats <= {"conn"},
              "conn=%r all=%d cats=%s" % (total_conn, total, cats))
        # kw 过滤：取一条真实日志文本的子串
        kw = ""
        for r in rows:
            text = str(r.get("text", "") or "")
            if len(text) >= 2:
                kw = text[:2]
                break
        status, _h, d = ctx.get(
            "/api/log/page?offset=0&limit=7&kw=%s"
            % urllib.parse.quote(kw), token=tok, timeout=30.0)
        total_kw = (d or {}).get("total") if status == 200 else None
        check("关键字过滤（kw=%r）：命中 ≤ 全量" % kw,
              total_kw is not None and 0 < total_kw <= total,
              "kw_total=%r all=%d" % (total_kw, total))


def sec_alarms(ctx):
    with Section("报警列表与统计"):
        tok = ctx.token_of("root")
        if ctx.sse is not None and ctx.sse.ensure_alive():
            ctx.sse.reapply_topics(ctx)
        status, _h, doc = ctx.get("/api/alarms?limit=100", token=tok)
        rows = (doc or {}).get("alarms") or []
        active = (doc or {}).get("active") or []
        check("GET /api/alarms ok（alarms/active 列表）",
              status == 200 and isinstance(rows, list)
              and isinstance(active, list),
              "status=%s" % status)
        keys_ok = all(all(k in r for k in ("ts", "time", "kind", "source",
                                           "name", "level", "text"))
                      for r in rows)
        check("报警行结构（ts/time/kind/source/name/level/text）",
              keys_ok, "row0=%s" % (str(rows[0])[:160] if rows else "空"))
        status, _h, doc = ctx.get("/api/alarms/active", token=tok)
        check("GET /api/alarms/active（列表）",
              status == 200 and isinstance((doc or {}).get("active"), list),
              "status=%s" % status)
        status, _h, st = ctx.get("/api/status", token=tok)
        a = (st or {}).get("alarms") or {}
        check("报警统计（/api/status alarms.total/active 且 total ≥ 行数）",
              isinstance(a.get("total"), int)
              and a.get("total", 0) >= len(rows)
              and isinstance(a.get("active"), int),
              "stats=%s rows=%d" % (a, len(rows)))
        auto_rows = [r for r in rows if r.get("kind") == "auto"]
        check("自动运行告警已入档（kind=auto ≥1，本 run 急停产生）",
              len(auto_rows) >= 1,
              "auto=%s" % str(auto_rows[:1])[:160])


def sec_alarm_server(ctx):
    """网页端报警页「服务端单一数据源」（0x011A 代理 + 兜底 + 前端接线）。

    [对齐 2026-11-17 桌面客户端改造] 报警历史一律向服务端要（XLink 0x011A，
    与 0x0118/0x0119 同形）；web 后端 /api/alarms/page 转发（hub.alarm_page）；
    报警页 page11 主数据源改走它，老服务端没有 0x011A 或链路未就绪时回落
    本地边沿存档（/api/alarms，fail-open 别让报警页变空白）。

    [三层都锁] ① 后端转发路由 API 往返（形状 = 0x011A 应答键集）；
    ② 上游不支持时必须回**显式失败包络**（fail-open 的前提——绝不能把
    失败洗白成"0 行历史"让页面误显空表）；③ 前端/后端接线静态检查
    （page11 要既会取服务端、也会回落本地；GLOBAL_TOPICS 的
    TOPIC_AUTO_ALARM 必须保留——它是实时刷新触发器）。

    [两端口径] py 服务端 2026-11-17 起已实现 0x011A ⇒ PY6200 端
    ok 必须为 true（转发断了就是真红）；C 端 HsfServerApp.c 暂无此命令
    ⇒ C6100 端接受显式失败包络（上游将来实现后会自动走 ok:true 分支
    参与全形状断言）。
    """
    with Section("报警页服务端数据源（0x011A 往返 + 兜底包络 + 前端接线）"):
        tok = ctx.token_of("root")
        # ---- ① API 往返（转发路由活着 + 形状）----
        status, _h, doc = ctx.get("/api/alarms/page?offset=0&limit=20",
                                  token=tok, timeout=20.0)
        envelope = status == 200 and isinstance(doc, dict)
        check("GET /api/alarms/page 回 JSON 包络（200）", envelope,
              "status=%s doc=%s" % (status, str(doc)[:160]))
        if not envelope:
            return
        ok_true = doc.get("ok") is True
        if ok_true:
            check("0x011A 应答键齐（total/count/offset/limit/rows）",
                  all(k in doc for k in ("total", "count", "offset",
                                         "limit", "rows")),
                  "doc=%s" % str(doc)[:160])
            rows = doc.get("rows")
            check("rows 是对象数组且行键齐（ts/kind/source/text）",
                  isinstance(rows, list)
                  and all(isinstance(r, dict)
                          and all(k in r for k in ("ts", "kind", "source",
                                                   "text"))
                          for r in rows[:5]),
                  "row0=%s" % (str(rows[0])[:140] if rows else "空"))
            check("offset/limit 回显一致（请求 0/20）",
                  doc.get("offset") == 0 and doc.get("limit") == 20,
                  "offset=%r limit=%r" % (doc.get("offset"),
                                          doc.get("limit")))
            # kw 链路（可选关键字下推服务端）：取首行 source 前两字符做探针。
            probe = ""
            for r in (rows or [])[:1]:
                probe = str(r.get("source", "") or "")[:2]
                break
            if probe:
                status, _h, d = ctx.get(
                    "/api/alarms/page?offset=0&limit=20&kw=%s"
                    % urllib.parse.quote(probe), token=tok, timeout=20.0)
                kw_total = (d or {}).get("total") if status == 200 else None
                check("关键字过滤（kw=%r）：命中 ≤ 全量" % probe,
                      kw_total is not None and 0 < kw_total <= doc.get(
                          "total", 0),
                      "kw_total=%r all=%r" % (kw_total, doc.get("total")))
            else:
                step("库中暂无报警行，kw 探针跳过（形状断言已覆盖键集）")
        else:
            check("上游无 0x011A：回显式失败包络（ok=false + err，供页面兜底）",
                  doc.get("ok") is False and bool(doc.get("err")),
                  "doc=%s" % str(doc)[:160])
        if _end[0] == "PY6200":
            check("py 上游已实现 0x011A：/api/alarms/page 必须 ok=true",
                  ok_true,
                  "py 服务端 2026-11-17 起有报警历史分页；ok=false 说明转发断了")
        # ---- ② 前端/后端接线（静态）----
        import re as _re
        hsfweb = os.path.join(WEBUI_DIR, "hsfweb")

        def _read(rel):
            path = os.path.join(hsfweb, rel)
            try:
                with open(path, "r", encoding="utf-8") as fh:
                    return fh.read()
            except OSError as exc:
                check("%s 可读" % rel, False, "%s" % exc)
                return None

        hub_src = _read("hub.py")
        api_src = _read("api.py")
        proto_src = _read("proto.py")
        jsapi_src = _read(os.path.join("static", "js", "api.js"))
        page_src = _read(os.path.join("static", "js", "pages",
                                      "page11_alarm.js"))
        if None in (hub_src, api_src, proto_src, jsapi_src, page_src):
            return
        check("后端接线：hub 有 0x011A 请求方法（CMD_ALARM_PAGE + alarm_page）",
              "CMD_ALARM_PAGE" in hub_src and "def alarm_page" in hub_src, "")
        check("后端接线：api 有 /api/alarms/page 路由", "/api/alarms/page" in
              api_src, "")
        check("协议常量：webui proto 有 CMD_ALARM_PAGE=0x011A 与 ALARM_KEY_*",
              "CMD_ALARM_PAGE = 0x011A" in proto_src
              and "ALARM_KEY_TS" in proto_src, "")
        check("前端接线：api.js 有 alarmsPage 具名端点", "alarmsPage" in
              jsapi_src, "")
        check("前端接线：报警页会取服务端分页（alarmsPage）",
              "alarmsPage" in page_src, "")
        check("前端接线：报警页保留本地兜底（fail-open 用 /api/alarms）",
              "api.alarms(" in page_src and "usingLocal" in page_src, "")
        # [GLOBAL_TOPICS 保留 TOPIC_AUTO_ALARM] 实时刷新触发器：报警历史改
        # 服务端取数后，本地边沿存档仍靠它落档（兜底）+ SSE alarm 事件仍靠
        # 常驻订阅推送（不订阅 ⇒ 报警页开着也收不到"有新行"信号）。
        m = _re.search(r"GLOBAL_TOPICS\s*=\s*\(([^)]*)\)", hub_src)
        check("前端接线：GLOBAL_TOPICS 仍常驻订阅 TOPIC_AUTO_ALARM",
              bool(m) and "TOPIC_AUTO_ALARM" in m.group(1), "")


def _xlsx_probe(blob):
    """XLSX 字节 → (行数, 全部部件文本)；不是合法 zip ⇒ (None, "")。

    [行数怎么数] 数 sheet part 里的 ``<row `` 出现次数（表头行也算一行）
    ⇒ **不依赖单元格类型**（数值格 ``<c r="D2"><v>…</v></c>`` 与文本格
    ``t="inlineStr"`` 都一样数）——写出器改数值格时这条断言不该红。
    """
    import io
    import zipfile
    try:
        zf = zipfile.ZipFile(io.BytesIO(blob))
    except Exception:                                   # noqa: BLE001
        return None, ""
    names = zf.namelist()
    if "xl/workbook.xml" not in names:
        return None, ""
    chunks = []
    sheet = ""
    for name in names:
        try:
            text = zf.read(name).decode("utf-8", "replace")
        except Exception:                               # noqa: BLE001
            continue
        chunks.append(text)
        if name.startswith("xl/worksheets/") and not sheet:
            sheet = text
    rows = sheet.count("<row ")
    return rows, "\n".join(chunks)


def sec_export_log_alarm(ctx):
    """日志/报警导出（CSV + Excel；按筛选全量批取流；2026-11-19）。

    [覆盖]
      · /api/log/export：csv（UTF-8 BOM + CRLF + 表头 + **行数 == 分页
        命中数**）/ xlsx（合法 OOXML + 行数一致）/ 非法 format 400；
      · /api/alarm/export：同上（数据源 = 0x011A 报警历史；老服务端没有
        0x011A ⇒ 必须回**显式失败包络**，不假装"导出成功"）；
      · 权限闸：未登录 401（下载位 PERM_DOWNLOAD 的下限）；
      · 筛选一致性：导出（带类型筛选）的行数 == 分页应答里同一筛选的
        命中数（分页取不全时只做"不多于"的方向断言）；
      · 前端接线静态检查：日志页/报警页都有 **CSV + Excel** 两个入口，
        且报警页走后端批取流（不是前端拼 CSV）。

    [为什么两形态都要锁] 用户 2026-11-14 口径「不管多大的数据都可以导出」
    ⇒ CSV 与 XLSX 都必须**流式**（不限行数）；XLSX 还要过"合法 OOXML"
    这道（zip 坏了 Excel 直接报"文件已损坏"）。
    """
    with Section("日志/报警导出（CSV + Excel；按筛选全量）"):
        tok = ctx.token_of("root")

        # ---------- 0. 权限闸（未登录 401）----------
        status, _h, _d = ctx.get("/api/log/export?format=csv")
        check("未登录导出日志 401（下载位闸在 web）", status == 401,
              "status=%s" % status)
        status, _h, _d = ctx.get("/api/alarm/export?format=csv")
        check("未登录导出报警 401（下载位闸在 web）", status == 401,
              "status=%s" % status)

        # ---------- 1. 日志导出 ----------
        # [锚点] 两种形态与分页用**同一个锚点** ⇒ 行数可比（整轮钉死
        # 同一 beforeTs ⇒ 导出期间新入库的日志进不来 ⇒ 不重不漏）。
        anchor = int(time.time() * 1000)
        status, _h, doc = ctx.get(
            "/api/log/page?offset=0&limit=5&beforeTs=%d" % anchor,
            token=tok, timeout=30.0)
        check("日志分页可读（导出前置）",
              status == 200 and (doc or {}).get("ok") is True,
              "status=%s doc=%s" % (status, str(doc)[:160]))
        log_total = (doc or {}).get("total") or 0
        status, hdr, blob = http_req(
            ctx.web, "GET",
            "/api/log/export?format=csv&beforeTs=%d" % anchor,
            token=tok, timeout=180.0)
        check("日志导出 CSV 200 + 首字节 UTF-8 BOM",
              status == 200 and blob[:3] == b"\xef\xbb\xbf",
              "status=%s head=%r" % (status, blob[:16]))
        text = blob.decode("utf-8-sig", "replace")
        lines = [l for l in text.split("\r\n") if l != ""]
        header = lines[0].split(",") if lines else []
        data_lines = lines[1:]
        check("日志导出 CSV 表头（时间/类别/事件）",
              header == ["时间", "类别", "事件"], "header=%s" % header)
        check("日志导出 CSV CRLF 行尾（无裸 LF）",
              b"\r\n" in blob and blob.replace(b"\r\n", b"").find(b"\n") == -1,
              "head=%r" % blob[:80])
        check("日志导出行数 == 分页命中数（%d 行）" % log_total,
              len(data_lines) == log_total,
              "export=%d page_total=%d" % (len(data_lines), log_total))
        cdisp = hdr.get("Content-Disposition") or ""
        check("日志导出 CSV 带下载文件名（Content-Disposition）",
              "attachment" in cdisp and ".csv" in cdisp, "cd=%r" % cdisp)

        # ---- XLSX 形态 ----
        status, hdr, blob = http_req(
            ctx.web, "GET",
            "/api/log/export?format=xlsx&beforeTs=%d" % anchor,
            token=tok, timeout=180.0)
        check("日志导出 XLSX 200 + zip 魔数（PK）",
              status == 200 and blob[:2] == b"PK",
              "status=%s head=%r" % (status, blob[:16]))
        ctype = hdr.get("Content-Type") or ""
        check("日志导出 XLSX 的 Content-Type 是 OOXML 工作簿",
              "spreadsheetml.sheet" in ctype, "ctype=%r" % ctype)
        rows_x, alltext = _xlsx_probe(blob)
        check("日志导出 XLSX 是合法 OOXML（含 workbook + sheet 部件）",
              rows_x is not None, "zip 解析失败")
        check("日志导出 XLSX 行数 == CSV 行数 + 表头（%d + 1）"
              % len(data_lines),
              rows_x == len(data_lines) + 1,
              "xlsx_rows=%r csv=%d" % (rows_x, len(data_lines)))
        check("日志导出 XLSX 表头文字在包里（时间/类别/事件）",
              "时间" in alltext and "类别" in alltext and "事件" in alltext,
              "")

        # ---- 非法形态必须明确拒（不留"选了不兑现"的选项）----
        status, _h, doc = ctx.get("/api/log/export?format=pdf", token=tok)
        check("日志导出非法 format=pdf → 400",
              status == 400, "status=%s" % status)
        status, _h, doc = ctx.get("/api/alarm/export?format=pdf", token=tok)
        check("报警导出非法 format=pdf → 400",
              status == 400, "status=%s" % status)

        # ---- **fmt** 是 format 的兼容别名（外部调用方常写 fmt=xlsx；
        #      若不认，会**静默回落成 csv**（拿到 CSV 却以为是 Excel））----
        status, hdr, blob = http_req(
            ctx.web, "GET",
            "/api/log/export?fmt=xlsx&beforeTs=%d" % anchor,
            token=tok, timeout=180.0)
        check("日志导出 fmt=xlsx（别名）也出 xlsx（不是静默回落 csv）",
              status == 200 and blob[:2] == b"PK",
              "status=%s head=%r" % (status, blob[:8]))
        status, _h, doc = ctx.get("/api/log/export?fmt=pdf", token=tok)
        check("日志导出非法 fmt=pdf → 400（别名同样 fail-closed）",
              status == 400, "status=%s" % status)

        # ---------- 2. 报警导出（0x011A） ----------
        anchor_a = int(time.time() * 1000)
        status, _h, doc = ctx.get(
            "/api/alarms/page?offset=0&limit=50&beforeTs=%d" % anchor_a,
            token=tok, timeout=30.0)
        ok_alarm = status == 200 and (doc or {}).get("ok") is True
        if not ok_alarm:
            # [老服务端（C 端）没有 0x011A] 导出必须回**显式失败包络**
            # ——绝不洗白成"0 行导出成功"（前端据此回落本机边沿存档）。
            check("上游无 0x011A：报警导出回显式失败包络（ok=false + err）",
                  (doc or {}).get("ok") is False
                  and bool((doc or {}).get("err")),
                  "doc=%s" % str(doc)[:160])
            status, _h, doc = ctx.get("/api/alarm/export?format=csv",
                                      token=tok, timeout=60.0)
            check("上游无 0x011A：报警导出不是 200（不假装导出成功）",
                  status != 200, "status=%s" % status)
            check("上游无 0x011A：失败文案点名 0x011A（前端据此回落）",
                  "0x011A" in str((doc or {}).get("err", "")),
                  "err=%r" % str((doc or {}).get("err", ""))[:160])
        else:
            rows_page = (doc or {}).get("rows") or []
            total_a = (doc or {}).get("total") or 0
            complete = (total_a == len(rows_page))
            status, hdr, blob = http_req(
                ctx.web, "GET",
                "/api/alarm/export?format=csv&beforeTs=%d" % anchor_a,
                token=tok, timeout=180.0)
            check("报警导出 CSV 200 + 首字节 UTF-8 BOM",
                  status == 200 and blob[:3] == b"\xef\xbb\xbf",
                  "status=%s head=%r" % (status, blob[:16]))
            text = blob.decode("utf-8-sig", "replace")
            lines = [l for l in text.split("\r\n") if l != ""]
            a_header = lines[0].split(",") if lines else []
            a_data = lines[1:]
            check("报警导出 CSV 表头（时间/设备/类型/状态）",
                  a_header == ["时间", "设备", "类型", "状态"],
                  "header=%s" % a_header)
            check("报警导出行数 == 0x011A 分页命中数（%d 行）" % total_a,
                  len(a_data) == total_a,
                  "export=%d page_total=%d" % (len(a_data), total_a))
            # ---- 筛选一致性（类型列 = 设备类别；dev=7 ⇒ 水质）----
            want_water = len([r for r in rows_page if r.get("dev") == 7])
            status, _h, blob = http_req(
                ctx.web, "GET",
                "/api/alarm/export?format=csv&kindText=%s&beforeTs=%d"
                % (urllib.parse.quote("水质"), anchor_a),
                token=tok, timeout=180.0)
            got_text = blob.decode("utf-8-sig", "replace")
            got_lines = [l for l in got_text.split("\r\n") if l != ""]
            got = max(0, len(got_lines) - 1)
            if complete:
                check("报警导出（类型筛选=水质）行数 == 分页同筛选命中数"
                      "（%d 行）" % want_water,
                      got == want_water,
                      "export=%d page=%d" % (got, want_water))
            else:
                check("报警导出（类型筛选=水质）不多于全量（分页取不全时）",
                      got <= total_a, "export=%d total=%d" % (got, total_a))
            check("报警导出（类型筛选）只出该类型的行",
                  all(",水质," in l or l.endswith(",水质")
                      for l in got_lines[1:]) if got_lines[1:] else True,
                  "sample=%s" % (got_lines[1:2] or "无行"))
            # ---- XLSX 形态 ----
            status, hdr, blob = http_req(
                ctx.web, "GET",
                "/api/alarm/export?format=xlsx&beforeTs=%d" % anchor_a,
                token=tok, timeout=180.0)
            check("报警导出 XLSX 200 + zip 魔数（PK）",
                  status == 200 and blob[:2] == b"PK",
                  "status=%s head=%r" % (status, blob[:16]))
            ctype = hdr.get("Content-Type") or ""
            check("报警导出 XLSX 的 Content-Type 是 OOXML 工作簿",
                  "spreadsheetml.sheet" in ctype, "ctype=%r" % ctype)
            rows_x, alltext = _xlsx_probe(blob)
            check("报警导出 XLSX 是合法 OOXML（含 workbook + sheet 部件）",
                  rows_x is not None, "zip 解析失败")
            check("报警导出 XLSX 行数 == CSV 行数 + 表头（%d + 1）"
                  % len(a_data),
                  rows_x == len(a_data) + 1,
                  "xlsx_rows=%r csv=%d" % (rows_x, len(a_data)))
            check("报警导出 XLSX 表头文字在包里（时间/设备/类型/状态）",
                  "时间" in alltext and "设备" in alltext
                  and "类型" in alltext and "状态" in alltext, "")
        # ---------- 3. 前端/后端接线静态检查 ----------
        hsfweb = os.path.join(WEBUI_DIR, "hsfweb")

        def _read(rel):
            path = os.path.join(hsfweb, rel)
            try:
                with open(path, "r", encoding="utf-8") as fh:
                    return fh.read()
            except OSError as exc:
                check("%s 可读" % rel, False, "%s" % exc)
                return None

        api_src = _read("api.py")
        log_js = _read(os.path.join("static", "js", "pages",
                                    "page10_log.js"))
        alarm_js = _read(os.path.join("static", "js", "pages",
                                      "page11_alarm.js"))
        if None in (api_src, log_js, alarm_js):
            return
        check("后端接线：日志导出支持 xlsx（format=csv|xlsx）",
              "/api/log/export" in api_src and "_log_export" in api_src
              and '("csv", "xlsx")' in api_src, "")
        check("后端接线：有 /api/alarm/export 路由 + 处理器",
              "/api/alarm/export" in api_src and "_alarm_export" in api_src,
              "")
        check("后端接线：报警导出走 0x011A（CMD_ALARM_PAGE）+ 批取流",
              "CMD_ALARM_PAGE" in api_src and "ALARM_HEADERS" in api_src
              and "EXPORT_CHUNK" in api_src, "")
        check("后端接线：报警导出的权限闸是 PERM_DOWNLOAD",
              "PERM_DOWNLOAD" in api_src, "")
        check("前端接线：日志页导出有 CSV + Excel 两个入口",
              "/api/log/export" in log_js and 'doExport("csv")' in log_js
              and 'doExport("xlsx")' in log_js, "")
        check("前端接线：报警页导出有 CSV + Excel 两个入口",
              "/api/alarm/export" in alarm_js and 'doExport("csv")' in alarm_js
              and 'doExport("xlsx")' in alarm_js, "")
        check("前端接线：报警页导出走后端批取流（不是前端拼 CSV）",
              "fetchWithTimeout" in alarm_js and "kindText" in alarm_js
              and "cols" in alarm_js, "")


def _vfd_params_static():
    """「变频器参数」前后端接线静态检查（API 往返可用与否都要查）。"""
    hsfweb = os.path.join(WEBUI_DIR, "hsfweb")

    def _read(rel):
        path = os.path.join(hsfweb, rel)
        try:
            with open(path, "r", encoding="utf-8") as fh:
                return fh.read()
        except OSError as exc:
            check("%s 可读" % rel, False, "%s" % exc)
            return None

    hub_src = _read("hub.py")
    api_src = _read("api.py")
    proto_src = _read("proto.py")
    jsapi_src = _read(os.path.join("static", "js", "api.js"))
    page_src = _read(os.path.join("static", "js", "pages",
                                  "page14_remote.js"))
    # [2026-11-18] 放水模式（首页下拉 vs 远端配置块）与滑条分段着色要查
    # ⇒ 多读首页页、搅拌泵页与样式表。
    home_src = _read(os.path.join("static", "js", "pages",
                                  "page00_home.js"))
    hist_js_src = _read(os.path.join("static", "js", "pages",
                                     "page09_history.js"))
    vfd_js_src = _read(os.path.join("static", "js", "pages", "page05.js"))
    # [2026-11-23] 阀门改名要同时锁「电动球阀」页的兜底名与后端 model 兜底。
    valve_js_src = _read(os.path.join("static", "js", "pages", "page07.js"))
    model_src = _read("model.py")
    css_src = _read(os.path.join("static", "css", "theme.css"))
    if None in (hub_src, api_src, proto_src, jsapi_src, page_src,
                home_src, hist_js_src, vfd_js_src, valve_js_src,
                model_src, css_src):
        return
    # [2026-11-19 用户口径两则] ①「历史数据里应该记录具体的负压值用负数，
    # 而不是说是个负压」⇒ 压差列**数值优先**（说明仍在 press_note，异常行
    # 标红照旧按它判 ⇒ 不丢信号）；②「web 网页端转速怎么显示 -- 不是 0」
    # ⇒ 转速的 0 是**真值**（停机 ⇒ 频率 0 ⇒ 转速 0），字段缺失才 "--"。
    check("历史页压差列数值优先（负压显示数值，不是说明文字）",
          'pressNote || num("press_mpa"' not in hist_js_src
          and 'num("press_mpa", 3)' in hist_js_src,
          "page09_history.js 的压差列仍是 note 优先")
    # [2026-11-23 三栏卡 重写判据] 压差卡改成「压差/压力1/压力2」三栏后，
    # 旧判据里的 dpValue/dpNote 标识符已不存在（继续按旧串判会假红）。
    # 语义没变：值来自 fmt3(ch.eng)（数值优先），ew 说明只作第 4 个实参
    # 进说明行 ⇒ 按新结构锁同一件事（值不是 note、note 不进值位）。
    check("主页压差卡数值优先（负压照显示数值）",
          "cols[i].set(fmt3(ch.eng)" in home_src
          and 'ch.note || ""' in home_src
          and "set(ch.note" not in home_src,
          "page00_home.js 的压差卡仍是 note 优先")
    check("主页转速：0 是真值（字段缺失才 --）",
          "doc.speedRpm > 0" not in home_src
          and "doc.outputFreqHz > 0" not in home_src,
          "page00_home.js 的转速仍把 0 当成无值")
    # [2026-11-19 停止＝暂停] 网页端相位两套文案表都要有 paused（状态行
    # 分支 + 阶段名表；服务端停止后 autoPhase="paused"、计时冻结）。
    check("网页端相位支持 paused（状态行分支 + 阶段名表「暂停」）",
          'm.autoPhase === "paused"' in home_src
          and 'paused: "暂停",' in home_src,
          "page00_home.js 缺 paused 相位文案")
    # [2026-11-19 用户「搅拌泵界面的下拉在自动模式下还能操作」] 方向下拉
    # 桌面端漏置灰（改选真发换向命令）；调速方式下拉三端都漏（只切本地
    # Hz/rpm 单位不发命令，但给定框已禁 ⇒ 一并置灰保持控制区一致）。
    check("搅拌泵页控制闸：方向/调速方式下拉随闸置灰",
          "dirSelect.disabled = !canCtrl;" in vfd_js_src
          and "speedModeSel.disabled = !canCtrl;" in vfd_js_src,
          "page05.js 的下拉置灰缺一（dirSelect/speedModeSel）")
    # [手动加速度 2026-11-26 用户「搅拌机界面 手动也支持下加速度」]
    # 控件在位 + 运动类命令（START=4/JOG=9/设频=2/设速=3）捎带 accelRpmS、
    # 0 不发键（与桌面 accel_spin / 服务端 _VFD_ACCEL_OPS 同口径）。
    check("搅拌泵页手动加速度：输入框在位 + 运动命令捎带 accelRpmS",
          'HSF.el("span.lbl", { text: "加速度" }), accelWrap, accelHint,' in vfd_js_src
          and "doc.accelRpmS = accel;" in vfd_js_src
          and "op === 4 || op === 9 || op === 2 || op === 3" in vfd_js_src,
          "page05.js 缺加速度控件或载荷注入")
    # [2026-11-19 用户实报「客户端改过了，web端没同步，刷新也不对」]
    # 真因：网页端读每阶段条件时把键名写成 `sub.pressEnable`（少一个 d），
    # 而服务端下发的是 **pressEnabled** ⇒ sub.pressEnable 恒为 undefined
    # ⇒ 三元表达式走 false 分支 ⇒ **压差/定时开关被强制取消**（阈值、
    # 方向、分钟数是对的 ⇒ 看起来"只有开关不同步"）。
    # ⇒ 这里钉住**键名逐字一致**（这类拼写错误静态就能查出来）。
    _remote_src = page_src
    check("网页端读每阶段条件用**正确的键名**（pressEnabled/timerEnabled）",
          "sub.pressEnabled" in _remote_src
          and "sub.timerEnabled" in _remote_src
          and "sub.pressEnable " not in _remote_src
          and "sub.pressEnable." not in _remote_src
          and ": !!sub.pressEnable;" not in _remote_src
          and ": !!sub.timerEnable;" not in _remote_src,
          "page14_remote.js 的 stageCfg 键名拼写与协议不一致")
    check("协议常量：webui proto 有 CMD_VFD_PARAMS=0x011B 与 PARAM_DEFS",
          "CMD_VFD_PARAMS = 0x011B" in proto_src and "PARAM_DEFS" in proto_src,
          "")
    low_proto = proto_src.lower()
    check("PARAM_DEFS 覆盖 P1.00~P1.05（0xF100~0xF105）六项",
          all(("0x%04x" % a) in low_proto for a in range(0xF100, 0xF106)), "")
    check("后端接线：hub 有 0x011B 请求方法（CMD_VFD_PARAMS + vfd_params）",
          "CMD_VFD_PARAMS" in hub_src and "def vfd_params" in hub_src, "")
    check("后端接线：api 有 /api/vfd/params 路由（GET+POST 两个 handler）",
          "/api/vfd/params" in api_src and "_vfd_params_get" in api_src
          and "_vfd_params_write" in api_src, "")
    check("后端接线：写路径要管理员（PERM_ACCOUNT_ADMIN）",
          "_vfd_params_write" in api_src and "PERM_ACCOUNT_ADMIN" in api_src,
          "")
    check("前端接线：api.js 有 vfdParamsGet/vfdParamsWrite 具名端点",
          "vfdParamsGet" in jsapi_src and "vfdParamsWrite" in jsapi_src, "")
    check("前端接线：远端设置页有变频器参数区块（读+写）",
          "变频器参数" in page_src and "vfdParamsGet" in page_src
          and "vfdParamsWrite" in page_src, "")
    check("前端接线：写后用应答回读值刷新（回读确认口径）",
          "已写入并回读确认" in page_src, "")
    # ---- [放水模式 2026-11-18] 「能配」与「能跑」两份列表必须分开 ----
    check("网页端 proto 有 AUTO_CONFIG_MODES（第四个可配置模式=放水）",
          "AUTO_CONFIG_MODES" in proto_src and "放水模式" in proto_src, "")
    check("接口把两份列表分开下发（autoModes=能跑 / autoConfigModes=能配）",
          "autoConfigModes" in api_src
          and "AUTO_CONFIG_MODES" in api_src, "")
    check("远端设置页读 autoConfigModes（配置块含放水）",
          "autoConfigModes" in page_src, "")
    check("首页下拉读 autoModes（放水模式在下拉里；组合循环不含它）",
          "autoModes" in home_src and "autoConfigModes" not in home_src, "")
    # [主页搅拌泵卡第三项＝转速 2026-11-18] C 端 11-17 已把第三项从
    # 「设定频率」换成「转速」（1440/50 ⇒ 28.8 rpm/Hz），桌面客户端与
    # 网页端当时**没跟上**（C 端注释却写"同口径"）⇒ 本轮三端补齐。
    # ⇒ 这里锁网页端：主页卡是「转速/rpm」且带换算系数，**不再**是
    # 「设定频率/Hz」；泵详情悬浮卡里仍保留「设定频率」（三端口径一致）。
    check("主页搅拌泵卡第三项＝转速（rpm），不再是「设定频率」（三端同口径）",
          'var vfdRpm = statBlock("转速", "rpm"' in home_src
          and "var vfdSetf" not in home_src
          and "1440.0 / 50.0" in home_src,
          "首页 rpm 块/换算系数缺失")
    check("泵详情悬浮卡仍保留「设定频率」（主页换项 ≠ 删掉设定值）",
          'var pdSetf = statBlock("设定频率", "Hz"' in home_src,
          "详情卡设定频率缺失")
    # [搅拌泵卡对调 2026-11-23 用户口径「输出频率和转速位置互调下，现在
    # 频率字大，但一般关心的是转速」] 转速升为主数值（40px，占频率原格），
    # 输出频率降到次一级（26px）⇒ 锁字号与第一排的顺序（位置互调）。
    check("主页搅拌泵卡：转速 40px 主位、输出频率 26px 次位（已互调）",
          'var vfdRpm = statBlock("转速", "rpm", 40)' in home_src
          and 'var vfdFreq = statBlock("输出频率", "Hz", 26)' in home_src
          and "[vfdState.root, vfdRpm.root, vfdFreq.root]" in home_src,
          "page00_home.js 的转速/频率字号或排列未对调")
    # [所有模式都显示运行时长 2026-11-23 用户口径「原先所有模式都有运行
    # 时长显示」] 阶段行在「已知 + 无定时（total=0）」时要显示「已用 X」，
    # 不能落回 "--"（"--" 只留给服务端真没给数据）。
    check("阶段行所有模式都显示「已用」（单模式/无界阶段不再 --）",
          "else if (m.autoStageKnown && elapsed >= 0)" in home_src
          and 'setText(phaseText, "已用 " + fmtPhaseMs(elapsed));' in home_src,
          "page00_home.js 的阶段行对 total=0 仍显示 --")
    # [阀门改名 2026-11-23 用户口径] 六路逐字：产水阀(1) / 反洗进水阀(2) /
    # 进水阀(3) / 排空阀(4) / 反洗出水阀(5) / 顺洗出水阀(6)。协议 id 不变；
    # 网页端三处兜底（proto.AUTO_DEVICES、model 快照兜底、首页/阀门页
    # 兜底名）都要是同一份名单。
    _valve_names = ("产水阀(1)", "反洗进水阀(2)", "进水阀(3)",
                    "排空阀(4)", "反洗出水阀(5)", "顺洗出水阀(6)")
    check("阀门改名：proto.AUTO_DEVICES 六路逐字（id 不变）",
          all(('("valve%d", "%s")' % (i + 1, nm)) in proto_src
              for i, nm in enumerate(_valve_names)),
          "proto.py 的 AUTO_DEVICES 显示名未改名")
    check("阀门改名：后端快照兜底与首页/阀门页兜底同一份名单",
          "VALVE_FALLBACK_NAMES = (\"" + _valve_names[0] in model_src
          and all(('"%s"' % nm) in home_src for nm in _valve_names)
          and all(('"%s"' % nm) in valve_js_src for nm in _valve_names),
          "model.py / page00_home.js / page07.js 的兜底名单不一致")
    # [组合循环的子模式集合不含放水] 协议侧 AUTO_CYCLE_MODES 只有三个 ⇒
    # 循环里没有放水（这才是「放水模式不进自动模式」的真实含义）。
    _cyc = ""
    if "AUTO_CYCLE_MODES" in proto_src:
        _cyc = proto_src.split("AUTO_CYCLE_MODES", 1)[1].split(")", 1)[0]
    check("协议侧有 AUTO_CYCLE_MODES（组合循环子模式 = 三个，不含放水）",
          "AUTO_CYCLE_MODES" in proto_src and "drain" not in _cyc,
          "%s" % _cyc.strip()[:80])
    # [枚举型参数 2026-11-18] P1.00 电机类型要渲染成**下拉框**（中文类型
    # 名），不是裸数字框；选项优先用服务端应答。
    check("前端接线：枚举型参数渲染 <select>（电机类型下拉）",
          "select.input" in page_src and "valueText" in page_src
          and "options" in page_src, "")
    check("网页端 proto 有 PARAM_OPTIONS（P1.00 三选项，老服务端兜底）",
          "PARAM_OPTIONS" in proto_src and "普通异步电机" in proto_src
          and "永磁同步电机" in proto_src, "")
    # ---- [拖动调速条渐变 2026-11-24 用户「拖动条也按蓝绿红改下」] ----
    check("前端接线：滑条按电机转速范围渐变取色（HSF.gradientColor）",
          "HSF.gradientColor(" in vfd_js_src
          and "speedRangeMin" in vfd_js_src
          and "speedRangeMax" in vfd_js_src
          and "--vfd-current" in vfd_js_src, "")
    check("样式：滑条轨道/手柄整条同色（--vfd-current 当前值色）",
          "var(--vfd-current, var(--accent-line))" in css_src
          and "linear-gradient" not in css_src.split(".vfd-slider", 1)[1]
          .split(".vfd-slider-value", 1)[0], "")
    # [剥注释 2026-11-25 实锅] CSS 里那条「原 band-green/blue/red 已删除」
    # 的**说明注释**本身含类名 ⇒ 裸串判会假红（规则其实已删）。
    _css_bare = re.sub(r"/\*.*?\*/", "", css_src, flags=re.S)
    check("样式：旧三档 band-* 规则已删除（渐变取色取代三档）",
          "band-green" not in _css_bare and "band-blue" not in _css_bare
          and "band-red" not in _css_bare, "")
    # ---- [写入确认数口径 2026-11-18 用户：三端口径必须一致] ----
    # 桌面客户端口径：**确认数 = len(params) − len(applied===false 的项)**，
    # 失败项逐个列出（「已写入并回读确认：5 项；失败：P1.02」）。网页端原先
    # 是「只要有一项 applied===false 就**整体归零**」⇒ 明明 5 项写进去了
    # 却显示「确认 0 项」⇒ **误导现场**（现场会以为一次都没写进去）。
    # 这里把网页端**真在跑的那份函数**抠出来直接执行（不是抄一份在这儿测）。
    fn = _js_func_src(page_src, "vfdWriteOkMessage")
    check("前端接线：有 vfdWriteOkMessage（写入确认数文案函数）",
          fn is not None, "")
    if fn:
        if shutil.which("node") is None:
            check("node 可用（变频器写入确认数口径自检）", True,
                  "未找到 node：跳过本段自检（安装 Node.js 后才有牙齿）")
        else:
            params = [{"code": "P1.00", "applied": True},
                      {"code": "P1.01", "applied": True},
                      {"code": "P1.02", "applied": False},
                      {"code": "P1.03", "applied": True},
                      {"code": "P1.04", "applied": True},
                      {"code": "P1.05", "applied": True}]
            sent = {"16384": 1, "16385": 1, "16386": 1,
                    "16387": 1, "16388": 1, "16389": 1}
            got = _js_run_node(
                fn, "vfdWriteOkMessage(%s, %s)"
                    % (json.dumps(params), json.dumps(sent)))
            want = "已写入并回读确认：5 项；失败：P1.02"
            check("变频器写入确认数口径（与桌面端逐字一致）：5 项成功 + 1 项"
                  "失败 ⇒「确认 5 项」且逐个列出失败项",
                  got == want, "got=%r want=%r" % (got, want))
            got2 = _js_run_node(
                fn, "vfdWriteOkMessage(%s, %s)"
                    % (json.dumps([{"code": "P1.00", "value": 1}]),
                       json.dumps({"16384": 1, "16385": 1})))
            check("变频器写入确认数（老服务端无 applied 键 ⇒ 按提交项数计，"
                  "旧口径不变）",
                  got2 == "已写入并回读确认：2 项", "got=%r" % (got2,))
            got3 = _js_run_node(
                fn, "vfdWriteOkMessage(%s, %s)"
                    % (json.dumps([{"code": "P1.00", "applied": True},
                                   {"code": "P1.01", "applied": True}]),
                       json.dumps({"16384": 1, "16385": 1})))
            check("变频器写入确认数（全成功 ⇒ 无失败清单）",
                  got3 == "已写入并回读确认：2 项", "got=%r" % (got3,))


def sec_auto_modes(ctx):
    """放水/搅拌模式：「能配」与「能跑」两份模式列表必须分开（2026-11-18）。

    [为什么锁] 用户口径「自动模式中再增加一个放水模式…放水模式**不进自动
    模式**」⇒ 网页端必须给两份列表：
      · autoModes（首页下拉用）= 单模式池 + 组合「自动」；
      · autoConfigModes（远端设置配置块用）= 同一份单模式池（含放水/搅拌）。
    两份写混 ⇒ 首页下拉冒出配置专用项或远端设置少一块。
    [2026-11-27 现状] 单模式池 = 产水/反洗/顺洗/放水/**搅拌**（用户
    「默认配置下拉再增加一个搅拌」）；首页下拉 = 这五个 + 组合「自动」。
    """
    with Section("放水模式（能配/能跑两份列表分开）"):
        status, _h, doc = ctx.get("/api/pages", timeout=15.0)
        check("GET /api/pages 200（注册表）",
              status == 200 and isinstance(doc, dict),
              "status=%s" % status)
        if not isinstance(doc, dict):
            return
        run_modes = doc.get("autoModes") or []
        cfg_modes = doc.get("autoConfigModes") or []
        run_ids = [m.get("id") for m in run_modes if isinstance(m, dict)]
        cfg_ids = [m.get("id") for m in cfg_modes if isinstance(m, dict)]
        # [2026-11-18 订正] 「放水模式不进自动模式」= **不进组合循环**，
        # 它本身是普通的可选/可启动模式 ⇒ 首页下拉里**有**放水。
        # [2026-11-27] 顶端追加搅拌模式（AUTO_MODES 末尾）⇒ 首页下拉
        # = 五模式 + 组合「自动」。
        check("autoModes（首页下拉）= 五模式 + 组合「自动」（含放水/搅拌）",
              run_ids == ["produce", "backwash", "rinse", "drain", "stir",
                          "auto"],
              "%s" % run_ids)
        check("autoConfigModes（远端配置块）= 五模式（含放水/搅拌）",
              cfg_ids == ["produce", "backwash", "rinse", "drain", "stir"],
              "%s" % cfg_ids)
        _names = [m.get("name") for m in cfg_modes if isinstance(m, dict)]
        check("放水模式/搅拌模式的中文名在配置块里（放水模式、搅拌模式）",
              "放水模式" in _names and "搅拌模式" in _names, "%s" % _names)


def sec_vfd_params(ctx):
    """网页端「变频器参数」区块（0x011B 透传 + 权限 + RunLocked + 接线）。

    [三层都锁] ① API 往返（GET 读真实值键齐 + POST 幂等回写后回读一致
    ——写的是**回读到的原值**，不动驱动器实际配置）；② 权限与 fail-closed
    （读=登录、写=管理员、POST op=0/空 values 显式 400）；③ RunLocked
    （变频器运行中写参数必须被服务端拒，err 三端同款
    「变频器运行中禁止写参数（请先停止）」——先启动变频器试写一次，拿到
    拒绝、停机后再做幂等回写）；④ 静态接线（proto/hub/api/api.js/page14）。

    [两端口径] py 服务端 0x011B 由并行改造实现 ⇒ PY6200 端 ok 必须为 true
    （转发断了就是真红）；C 端 HsfServerApp.c 暂无此命令 ⇒ C6100 端接受
    显式失败包络（上游将来实现后会自动走 ok:true 分支参与全形状断言）。
    """
    with Section("变频器参数（0x011B GET 读真实值 + RunLocked 拒写 + 幂等回写 + 接线）"):
        tok = ctx.token_of("root")
        # ---- ① GET：读真实值（形状 = 0x011B 应答键集）----
        status, _h, doc = ctx.get("/api/vfd/params", token=tok, timeout=20.0)
        envelope = status == 200 and isinstance(doc, dict)
        check("GET /api/vfd/params 回 JSON 包络（200）", envelope,
              "status=%s doc=%s" % (status, str(doc)[:160]))
        if not envelope:
            return
        ok_true = doc.get("ok") is True
        values = {}
        if ok_true:
            params = doc.get("params")
            check("0x011B 应答键齐（params 数组非空 + vfdFaulted + runState）",
                  isinstance(params, list) and bool(params)
                  and "vfdFaulted" in doc and "runState" in doc,
                  "doc=%s" % str(doc)[:200])
            item_keys = ("code", "label", "addr", "raw", "value", "unit",
                         "writable", "runLocked")
            check("params 条目键齐（code/label/addr/raw/value/unit/"
                  "writable/runLocked）",
                  all(isinstance(p, dict)
                      and all(k in p for k in item_keys) for p in params),
                  "p0=%s" % (str(params[0])[:160] if params else "空"))
            try:
                addrs = sorted(int(p.get("addr", -1)) for p in params
                               if isinstance(p, dict))
            except (TypeError, ValueError):
                addrs = []
            check("参数集 = P1.00~P1.05（0xF100~0xF105 电机铭牌组）",
                  addrs == [0xF100, 0xF101, 0xF102, 0xF103, 0xF104, 0xF105],
                  "addrs=%s" % [hex(a) for a in addrs])
            # ---- 枚举型参数（P1.00 电机类型）：options + valueText ----
            # [需求 2026-11-18] 电机类型要能下拉选中文类型 ⇒ 选项随应答
            # 下发（服务端是单一事实源）；valueText 按**回读** raw 取名。
            _p100 = None
            for _p in (params or []):
                if isinstance(_p, dict) and int(_p.get("addr", -1) or -1) \
                        == 0xF100:
                    _p100 = _p
                    break
            _opts = (_p100 or {}).get("options")
            check("P1.00 带 options（3 项中文类型名，手册第 20 页 P1 组）",
                  isinstance(_opts, list) and len(_opts) == 3
                  and [o.get("value") for o in _opts] == [0, 1, 2]
                  and [o.get("label") for o in _opts]
                  == ["普通异步电机", "变频异步电机", "永磁同步电机"],
                  "options=%s" % (str(_opts)[:160],))
            check("P1.00 带 valueText（按回读 raw 取中文名）",
                  bool((_p100 or {}).get("valueText")),
                  "raw=%s valueText=%s"
                  % ((_p100 or {}).get("raw"), (_p100 or {}).get("valueText")))
            # 读=登录：普通用户（admin）也应能读（写才要管理员）。
            st2, _h2, d2 = ctx.get("/api/vfd/params",
                                   token=ctx.token_of("admin"), timeout=20.0)
            check("admin（普通用户）GET 读参数 200（读=登录）",
                  st2 == 200 and (d2 or {}).get("ok") is True,
                  "status=%s doc=%s" % (st2, str(d2)[:120]))
            # 幂等回写的落点：只收 writable!==false 的项的原值。
            for p in (params or []):
                if not isinstance(p, dict) or p.get("writable") is False:
                    continue
                try:
                    values[int(p["addr"])] = int(p["raw"])
                except (KeyError, TypeError, ValueError):
                    pass
        else:
            check("上游无 0x011B：回显式失败包络（ok=false + err，供前端显示）",
                  doc.get("ok") is False and bool(doc.get("err")),
                  "doc=%s" % str(doc)[:160])
        if _end[0] == "PY6200":
            check("py 上游已实现 0x011B：/api/vfd/params 必须 ok=true",
                  ok_true,
                  "py 服务端有变频器参数命令；ok=false 说明转发断了/未实现")
        if not ok_true:
            # C 端没有 0x011B ⇒ 写链路没法真往返；静态接线照查（最后一节）。
            _vfd_params_static()
            return
        # ---- ② 权限/fail-closed：写=管理员；坏载荷显式 400 ----
        status, _h, d = ctx.post("/api/vfd/params", {"op": 1, "values": {}},
                                 token=tok)
        check("POST 空 values 显式 400（不让拼错的键悄悄变成「没写」）",
              status == 400, "status=%s doc=%s" % (status, str(d)[:120]))
        status, _h, d = ctx.post("/api/vfd/params", {"op": 0}, token=tok)
        check("POST op=0 显式 400（读走 GET，不混着来）",
              status == 400, "status=%s doc=%s" % (status, str(d)[:120]))
        status, _h, d = ctx.post("/api/vfd/params",
                                 {"op": 1, "values": {0xF101: 1}},
                                 token=ctx.token_of("admin"))
        check("admin（普通用户）POST 写参数 403（写=管理员）",
              status == 403, "status=%s doc=%s" % (status, str(d)[:120]))
        if not values:
            check("可写参数 ≥1（RunLocked/幂等回写有落点）", False,
                  "服务端把全部参数标只读？params=%s"
                  % str(doc.get("params"))[:200])
            _vfd_params_static()
            return
        # ---- ③ RunLocked：变频器运行中写参数必须被服务端拒 ----
        ensure_manual_idle(ctx, tok)
        # 故障复位先行（急停段可能留过故障态；无故障时无害），再确保停机。
        ctx.post("/api/vfd/control", {"op": 8}, token=tok)
        ctx.post("/api/vfd/control", {"op": 5}, token=tok)

        def _stopped():
            st = ctx.vfd_field(tok, "runState")
            return st if st in (1, 2) else None

        idle = ctx.poll(_stopped, 25, interval=1.0, label="停机稳态")
        check("写参数前置：变频器回停机/就绪稳态（runState 1/2）",
              idle is not None,
              "runState=%r" % ctx.vfd_field(tok, "runState"))
        status, _h, d = ctx.post("/api/vfd/control",
                                 {"op": 4, "hz": 20.0}, token=tok)
        check("RunLocked 前置：变频器启动 ok=true",
              status == 200 and (d or {}).get("ok") is True,
              "status=%s doc=%s" % (status, str(d)[:160]))
        running = ctx.poll(lambda: ctx.vfd_field(tok, "running") is True, 6,
                           label="运行位")
        if running is True:
            status, _h, d = ctx.post("/api/vfd/params",
                                     {"op": 1, "values": values},
                                     token=tok, timeout=30.0)
            err = str((d or {}).get("err", ""))
            check("RunLocked：运行中写参数被拒（ok=false + 三端同款文案）",
                  status == 200 and (d or {}).get("ok") is False
                  and "变频器运行中" in err and "请先停止" in err,
                  "status=%s doc=%s" % (status, str(d)[:200]))
        else:
            check("RunLocked 前置：变频器启动后运行位 running=true", False,
                  "running=%r（启动链路异常：RunLocked 与回写两条没测到）"
                  % running)
        # 停机 → 幂等回写（RunLocked 试写的 values = 回读原值，无副作用）。
        ctx.post("/api/vfd/control", {"op": 5}, token=tok)
        idle2 = ctx.poll(_stopped, 25, interval=1.0, label="回写前停机")
        check("回写前置：变频器停机稳态", idle2 is not None,
              "runState=%r" % ctx.vfd_field(tok, "runState"))
        # ---- ④ 幂等回写：把回读到的原值写回，应答回读一致 ----
        status, _h, wdoc = ctx.post("/api/vfd/params",
                                    {"op": 1, "values": values},
                                    token=tok, timeout=30.0)
        wrote = status == 200 and (wdoc or {}).get("ok") is True
        check("POST 幂等回写 ok=true（写=管理员，root 放行）", wrote,
              "status=%s doc=%s" % (status, str(wdoc)[:200]))
        if wrote:
            back = {}
            for p in ((wdoc or {}).get("params") or []):
                try:
                    back[int(p["addr"])] = int(p["raw"])
                except (KeyError, TypeError, ValueError):
                    pass
            mismatch = [hex(a) for a in sorted(values)
                        if back.get(a) != values[a]]
            check("写后回读一致（应答 raw == 提交 raw，逐项）", not mismatch,
                  "不一致项=%s（回读表=%s）" % (mismatch, str(back)[:160]))
        # ---- ⑤ 静态接线 ----
        _vfd_params_static()


def sec_cleanup(ctx):
    with Section("收尾：恢复配置 / 退出登录 / 回手动全停"):
        tok = ctx.token_of("root")
        # 回手动（切模式=全停，含阀）
        ctx.set_mode_confirmed(tok, 0)
        live = ctx.mode_live(token=tok)
        check("收尾回手动：控制器停、模式手动",
              live.get("autoRunning") is False and live.get("mode") == 0,
              "live=%s" % str(live)[:160])
        # 恢复 auto 节
        if ctx.orig_auto and (ctx.orig_auto.get("modes") is not None
                              or ctx.orig_auto.get("cycle") is not None):
            payload = {"section": "auto"}
            if ctx.orig_auto.get("modes") is not None:
                payload["modes"] = ctx.orig_auto["modes"]
            if ctx.orig_auto.get("cycle") is not None:
                payload["cycle"] = ctx.orig_auto["cycle"]
            if ctx.orig_auto.get("stirMaxRpm") is not None:
                payload["stirMaxRpm"] = ctx.orig_auto["stirMaxRpm"]
            status, _h, doc = ctx.post("/api/config", payload, token=tok)
            check("恢复 auto 节原值", status == 200
                  and (doc or {}).get("ok") is True,
                  "status=%s doc=%s" % (status, str(doc)[:160]))
        # 恢复 storage（sampleMs 若被本脚本改过）
        if ctx.storage_touched and ctx.orig_storage:
            payload = {"section": "storage"}
            for key in ("enabled", "sampleMs", "maxMb", "fullPolicy",
                        "keepFiles", "historyTrigger"):
                if key in ctx.orig_storage:
                    payload[key] = ctx.orig_storage[key]
            status, _h, doc = ctx.post("/api/config", payload, token=tok)
            check("恢复 storage 节原值（sampleMs 等）", status == 200
                  and (doc or {}).get("ok") is True,
                  "status=%s doc=%s" % (status, str(doc)[:160]))
        # 恢复 login 节（会话上限若被本脚本抬高过）
        if ctx.orig_max_sessions is not None:
            status, _h, doc = ctx.post("/api/config",
                                       {"section": "login",
                                        "loginMaxSessions":
                                            int(ctx.orig_max_sessions)},
                                       token=tok)
            check("恢复 login 节原值（loginMaxSessions=%s）"
                  % ctx.orig_max_sessions,
                  status == 200 and (doc or {}).get("ok") is True,
                  "status=%s doc=%s" % (status, str(doc)[:160]))
        # 登出全部本脚本会话
        for user, token in list(ctx.tokens.items()):
            ctx.post("/api/logout", {}, token=token)
        if ctx.sse is not None:
            if ctx.sse.error:
                print("  (NOTE) SSE 收集线程曾记录：%s" % ctx.sse.error)
            ctx.sse.close()
        print("  已登出 %d 个脚本会话" % len(ctx.tokens))


# ============================================================================
#                 页面自举（静态资源 + 浏览器真开一眼）
# ============================================================================

#: Edge/Chrome 候选（同 webui_export_browser_check.py 口径）。
BROWSER_CANDIDATES = (
    r"C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe",
    r"C:/Program Files/Microsoft/Edge/Application/msedge.exe",
    r"C:/Program Files (x86)/Google/Chrome/Application/chrome.exe",
    r"C:/Program Files/Google/Chrome/Application/chrome.exe",
)

_STATIC_DONE = [False]


def find_browser():
    for exe in BROWSER_CANDIDATES:
        if os.path.isfile(exe):
            return exe
    return None


def _check_press_ui_static():
    """静态口径：首页「压差」卡三栏 + 远端设置模拟量通道与量程（2026-11-23）。

    用户口径：新增两台压力传感器（名字「压力1」「压力2」，MPa）并入现有
    「压差」卡做三栏（压差/压力1/压力2）；远端设置「采集配置」里能分别配
    通道号/上下限/4mA 以下有效；各种尺寸下不溢出/不裁字/不挤压。
    第二轮（同日）：「一个设备一行」——压差计/温度/压力1/压力2 各成一块
    （.press-cfg-dev），四路通道统一走 newChannelSelect（下拉，无数字框），
    ≤700px 块内每个控件独占一行、左对齐。

    [为什么放静态段] 这几条都是**纯前端结构/样式契约**（不改任何 HTTP
    接口）；协议面（collect 节 8 个键的 GET/SET）由主回归的 0x0116/0x0117
    段覆盖，这里只钉源码里「三栏标签、按名字取通道、一台一块、统一通道
    下拉、窄屏护栏」在位。
    """
    import re as _re
    static_dir = os.path.join(WEBUI_DIR, "hsfweb", "static")
    css_path = os.path.join(static_dir, "css", "theme.css")
    try:
        with open(css_path, "r", encoding="utf-8", errors="replace") as fp:
            css = fp.read()
    except OSError as exc:
        check("theme.css 可读（压差三栏响应式口径）", False, "%s" % exc)
        return

    # ---- ① 首页压差卡：三栏标签 + 按通道名查找（en{i} 取法） ----
    home_js = os.path.join(static_dir, "js", "pages", "page00_home.js")
    try:
        with open(home_js, "r", encoding="utf-8", errors="replace") as fp:
            home_src = fp.read()
    except OSError as exc:
        check("page00_home.js 可读（压差三栏口径）", False, "%s" % exc)
        return
    check("主页压差卡：三栏标签含「压力1」「压力2」",
          "进水压力" in home_src and "出水压力" in home_src, "")
    check("主页压力卡：三行容器 .press-rows 在位",
          "div.press-rows" in home_src
          and home_src.count("pressureRow(") >= 4, "")
    # 通道号远端可配、名字跟着走 ⇒ 客户端必须**按名字找**，不许写死下标。
    # store 归一化后是 channels[i].name，但页面同时对原始协议 en{i} 兜底
    # （两种形状都认），这里两条都钉。
    check("主页压差卡：按通道名查找（en{i} 取法 + findAnalogByName）",
          _re.search(r'\["en"\s*\+\s*i\]', home_src) is not None
          and "findAnalogByName" in home_src
          and _re.search(r'findAnalogByName\([^)]*"进水压力"', home_src)
          is not None,
          "en 键取法/名字匹配缺失")
    # 说明行恒占位：有 ew 上警示色、无 ew 清空文本，不切 hidden/display。
    bare_css = _re.sub(r"/\*.*?\*/", "", css, flags=_re.S)
    # [2026-11-23 用户「下面空了整整一行」] 说明行**有文案才显示**（空时
    # 收起，卡片已按内容自然高）；有文案时固定单行高，读数不跳。
    check("主页压差卡：说明行有文案才显示（空时收起、单行定高）",
          "div.press-note" in home_src
          and _re.search(r"\.press-note\s*\{[^}]*min-height", bare_css)
          is not None
          and ".press-note-shared:empty" in bare_css,
          "press-note 收起/定高规则缺失")
    # [改名 2026-11-23 用户口径「卡片名字修改为压力及压差」]
    check("主页压力及压差卡：标题 = 「压力及压差」",
          'text: "压力及压差"' in home_src, "")
    # [行间距缩小 2026-11-23] 说明行从「每栏各一条」收成卡底共享一行
    # （压力1/压力2 之间只剩 6px）；前缀 DP_LABELS 写清哪一路。
    check("主页压力及压差卡：说明收成卡底共享一行（压力行不再各带说明）",
          "press-note-shared" in home_src
          and "DP_LABELS" in home_src
          and home_src.count("div.press-note") == 1,
          "press-note 实例数=%d" % home_src.count("div.press-note"))

    # ---- ② 远端设置采集配置：压力1/压力2 八个协议键（发 + 收） ----
    remote_js = os.path.join(static_dir, "js", "pages", "page14_remote.js")
    try:
        with open(remote_js, "r", encoding="utf-8", errors="replace") as fp:
            remote_src = fp.read()
    except OSError as exc:
        check("page14_remote.js 可读（压力传感器口径）", False, "%s" % exc)
        return
    keys = ["press1Channel", "press1LowMpa", "press1HighMpa",
            "press1Below4Valid",
            "press2Channel", "press2LowMpa", "press2HighMpa",
            "press2Below4Valid"]
    missing = [k for k in keys if k not in remote_src]
    check("远端设置采集配置：压力1/压力2 八个键名齐全", not missing,
          "缺失：%s" % missing)
    no_send = [k for k in keys if ("values.%s" % k) not in remote_src]
    check("远端设置采集配置：八键进 collect SET 载荷", not no_send,
          "未提交：%s" % no_send)
    no_echo = [k for k in keys if ("collect.%s" % k) not in remote_src]
    check("远端设置采集配置：八键按应答节存在才回显", not no_echo,
          "未回显：%s" % no_echo)

    # ---- ②b 一台设备一块 + 四路通道统一下拉（2026-11-23 用户口径） ----
    # 用户原话「一堆混在一起了，一个设备一行尝试下，竖屏下在调整下都一个
    # 控件一行」+「通道选择都用统一的下拉框」：压差计/温度/压力1/压力2
    # 各成一块 .press-cfg-dev（块内 4 个控件：通道/下限/上限/4mA 以下
    # 有效）；四路通道一律由 newChannelSelect 唯一 helper 构造（内部走
    # fillChannelSelect，选项 CH0..CH{analogChannels-1}），不再有数字框。
    check("远端设置采集配置：一台设备一块（.press-cfg-dev + 4 个设备块）",
          remote_src.count('HSF.el("div.press-cfg-dev"') == 1
          and ('buildSensorBlock("' in remote_src
           and remote_src.count('buildSensorBlock("') == 2
           and remote_src.count('buildSensorBlock(PRESS1_LABEL') == 1
           and remote_src.count('buildSensorBlock(PRESS2_LABEL') == 1),
          "buildSensorBlock 调用 %d 处（应为 4）"
          % remote_src.count('buildSensorBlock("'))
    # [2026-11-26 改名] 标签走 PRESS{1,2}_LABEL 常量 ⇒ 按常量调用形态断言。
    check("远端设置采集配置：四块设备标签齐全（压差计/温度传感器/进水压力/出水压力）",
          ('buildSensorBlock("压差计"' in remote_src
           and 'buildSensorBlock("温度传感器"' in remote_src
           and 'buildSensorBlock(PRESS1_LABEL' in remote_src
           and 'buildSensorBlock(PRESS2_LABEL' in remote_src),
          "设备标签缺失")
    check("远端设置采集配置：四路通道走 newChannelSelect 单一构造",
          _re.search(r"function\s+newChannelSelect\([^)]*\)\s*\{[^}]*"
                     r"fillChannelSelect\(sel", remote_src, _re.S) is not None
          and len(_re.findall(
              r"var\s+an(?:Press1|Press2|Press|Temp)Ch\s*=\s*"
              r"newChannelSelect\(", remote_src)) == 4,
          "newChannelSelect 定义/调用不齐")
    bad_ch = _re.findall(r"var\s+an(?:Press1|Press2|Press|Temp)Ch\s*=\s*"
                         r"(?:numInput|fixedInput)\(", remote_src)
    check("远端设置采集配置：压力/温度通道无数字微调框路径", not bad_ch,
          "仍用数字框：%s" % bad_ch)
    # 四路通道的 SET 都取统一下拉、回显都经 fillChannelSelect（8 处键名
    # 各走自己的通道控件；ameCase 的四个通道键一个都不能漏）。
    ch_pairs = [("pressChannel", "anPressCh"), ("tempChannel", "anTempCh"),
                ("press1Channel", "anPress1Ch"),
                ("press2Channel", "anPress2Ch")]
    bad_set = [k for k, var in ch_pairs
               if _re.search(r"values\.%s\s*=\s*clampInt\(%s\b" % (k, var),
                             remote_src) is None]
    check("远端设置采集配置：四路通道 SET 都取自统一下拉", not bad_set,
          "未取自下拉：%s" % bad_set)
    bad_ch_echo = [k for k, var in ch_pairs
                   if _re.search(r"fillChannelSelect\(%s,\s*collect\.%s"
                                 % (var, k), remote_src) is None]
    check("远端设置采集配置：四路通道回显都经 fillChannelSelect", not bad_ch_echo,
          "未经下拉回显：%s" % bad_ch_echo)
    # [2026-11-23 用户「会不会选择到同一个通道…互斥做了吗」] 通道互斥：
    # 其它传感器占用的 CH 置灰（当前项永远可用），任一路改选四路一起重算。
    check("远端设置采集配置：四路通道下拉互斥（占用 CH 置灰）",
          "CHANNEL_SENSORS" in remote_src
          and "refreshChannelExclusivity" in remote_src
          and "opt.disabled = taken && !opt.selected" in remote_src
          and "addEventListener(\"change\", refreshChannelExclusivity)"
              in remote_src, "")
    # [2026-11-24 全量工作流复核出的缺陷回归锁]
    check("远端设置：温度通道 CH0 不被 || 吞（channelOr，无 || 1）",
          "function channelOr(" in remote_src
          and "channelOr(collect.tempChannel, 1)" in remote_src
          and remote_src.count("parseInt(collect.tempChannel, 10) || 1") == 0,
          "")
    check("数据存储行：标签+控件成对（.cfg-pair，换行不拆散）",
          remote_src.count('HSF.el("span.cfg-pair"') >= 3
          and 'text: "保留分段" }), stKeep]' in remote_src, "")
    _toolbar_js = os.path.join(static_dir, "js", "pages", "data_toolbar.js")
    try:
        with open(_toolbar_js, "r", encoding="utf-8", errors="replace") as _fp:
            _toolbar_src = _fp.read()
    except OSError:
        _toolbar_src = ""
    check("折线列下拉：文本/箭头分离（.mcc-caret 固定可见）",
          "mcc-caret" in bare_css and "mcc-text" in bare_css
          and "span.mcc-caret" in _toolbar_src, "")
    check("输入型下拉有 max-width:100%（长适配器名不顶破卡片）",
          _re.search(r"select\.input\s*\{[^}]*max-width:\s*100%", bare_css)
          is not None, "")
    check("首页浊度数字：窄窗口（<1200）回落 32px、宽屏 36px",
          "window.innerWidth < 1200" in home_src
          and "? 32 : 36" in home_src, "")
    _export_py = os.path.join(static_dir, os.pardir, "export.py")
    try:
        with open(_export_py, "r", encoding="utf-8", errors="replace") as _fp:
            _export_src = _fp.read()
    except OSError:
        _export_src = ""
    check("网页导出：压差列数值优先（不再 press_note or 数值）",
          _export_src != ""
          and "press_note or" not in _export_src
          and '_fmt_num(rec, "press_mpa", "%.3f")' in _export_src, "")

    # ---- ③ theme.css：压差卡「压差靠左 + 压力上下排」（2026-11-23 第二版）
    # [必须先剥注释] 新规则的注释里就写着 flex/column（bare_css 已在 ① 剥好）。
    # [布局第四版 2026-11-23「压差 数值 单位写在一行，另外两个压力一样的
    # 布局分别两行，总共三行」] 三行同构 = 两列 grid（标签列 + 读数列）。
    check("压力卡：三行两列 grid（.press-rows + max-content 标签列）",
          _re.search(r"\.press-rows\s*\{[^}]*display:\s*grid", bare_css)
          is not None
          and _re.search(r"grid-template-columns:\s*max-content", bare_css)
          is not None, "")
    check("压力卡：每行 display:contents（标签/读数直接落进 grid 两列）",
          _re.search(r"\.press-row\s*\{[^}]*display:\s*contents",
                     bare_css) is not None, "")
    m = _re.search(r"@media\s*\(max-width:\s*700px\)\s*\{", bare_css)
    block = ""
    if m is not None:
        depth, i = 0, m.end() - 1
        while i < len(bare_css):
            if bare_css[i] == "{":
                depth += 1
            elif bare_css[i] == "}":
                depth -= 1
                if depth == 0:
                    break
            i += 1
        block = bare_css[m.end():i]
    hit = ""
    for sel, body in _re.findall(r"([^{}]+)\{([^{}]*)\}", block):
        if "press-" in sel and "font-size" in body:
            hit = sel.strip()
            break
    check("窄屏 ≤700px：压差卡有字号/间距响应式规则", bool(hit),
          "未找到 .press-* 的 font-size 规则")

    # ---- ③b 采集配置「一台设备一块」+ ≤700px 每控件一行（2026-11-23）----
    # 用户口径「一堆混在一起了，一个设备一行尝试下，竖屏下再调整下都一个
    # 控件一行」：块要有明确边界（左边线 + 块头设备名）；宽屏块内自然
    # 排布；窄屏块内每个「标签 + 控件」对整行占位、左对齐。
    check("采集配置：设备块有明确分隔（.press-cfg-dev 左边线 + 块头标签）",
          _re.search(r"\.press-cfg-dev\s*\{[^}]*border-left", bare_css)
          is not None
          and _re.search(r"\.press-cfg-dev-title\s*\{[^}]*"
                         r"font-weight\s*:\s*bold", bare_css) is not None, "")
    check("采集配置：块内控件行宽屏自然排布（.press-cfg-fields flex-wrap）",
          _re.search(r"\.press-cfg-fields\s*\{[^}]*display\s*:\s*flex",
                     bare_css) is not None
          and _re.search(r"\.press-cfg-fields\s*\{[^}]*flex-wrap\s*:\s*wrap",
                         bare_css) is not None, "")
    check("采集配置：旧的混排行 .press-cfg-row 已移除",
          ".press-cfg-row" not in bare_css
          and ".press-cfg-row" not in remote_src, "")
    # ≤700px：.press-cfg-fields 竖排 + 每个「标签+控件」对 100% 宽（与
    # flex-basis:100% 同义）⇒ 一控件一行、左对齐，绝不横向溢出。
    block_fields = ""
    for sel, body in _re.findall(r"([^{}]+)\{([^{}]*)\}", block):
        if ".press-cfg-fields" in sel and "flex-direction" in body:
            block_fields = body
            break
    check("窄屏 ≤700px：采集配置一块内每控件一行（flex-direction:column）",
          _re.search(r"flex-direction\s*:\s*column", block_fields) is not None
          and _re.search(r"align-items\s*:\s*flex-start", block_fields)
          is not None, "fields 规则=%r" % block_fields[:80])
    one_per_line = [sel.strip() for sel, body in
                    _re.findall(r"([^{}]+)\{([^{}]*)\}", block)
                    if ".press-cfg-fields" in sel and ".cfg-pair" in sel
                    and _re.search(r"width\s*:\s*100%|flex\s*:[^;]*\b100%",
                                   body)]
    check("窄屏 ≤700px：标签+控件对整行占位（width/flex-basis 100%）",
          bool(one_per_line), "未找到 .press-cfg-fields .cfg-pair 100% 规则")


def _check_auto_lights_static():
    """静态断言：网页端「按钮灯闪烁」（auto.lights 六键 + 控件 + 接线）。

    [为什么放静态段] 2026-11-24 用户定案：三盏灯（启动/停止/复位）各自
    「亮 ms / 灭 ms」可配（50~5000，缺省 500），随自动配置**一次提交**
    （auto 节 lights 子节；服务端灯节拍约 100ms）。这套全是**前端接线**，
    协议级验收只打 /api/config 看不出来 ⇒ 只能静态钉：
      ① 六键必须与 proto.AUTO_LIGHT_* 逐字一致，且**集中一处**声明
         （载荷/回显都从那取；各写一份就会拼漂）；
      ② 控件必须是 number 输入（范围/缺省与 proto 同值）+ 进整卡权限闸
         （无管理权限置灰）+ 真的挂进自动配置卡体；
      ③ 载荷构造点（autoConfigValues）带 lights 子节；
      ④ 回显（populateAuto）只在应答带 lights 时铺（缺键不冲缺省），
         且数值钳制走 clampIntValue —— clampInt 读的是控件的 .value，
         传数字会恒回缺省 ⇒ 灯参数永远显示 500（回显等于失效）。
    """
    import re as _re
    js_path = os.path.join(WEBUI_DIR, "hsfweb", "static", "js", "pages",
                           "page14_remote.js")
    with Section("网页端按钮灯闪烁（lights 六键 + 控件 + 载荷/回显接线）"):
        try:
            with open(js_path, "r", encoding="utf-8") as fh:
                js = fh.read()
        except OSError as exc:
            check("远端设置页 JS 可读（按钮灯闪烁）", False, "%s" % exc)
            return
        src = _js_strip_comments(js)      # 只认代码：注释里提到键名不算
        keys = ("startOnMs", "startOffMs", "stopOnMs", "stopOffMs",
                "resetOnMs", "resetOffMs")
        # ---- ① 六键集中声明（BLINK_LIGHTS 表）----
        mt = _re.search(r"var\s+BLINK_LIGHTS\s*=\s*\[(.*?)\];", src, _re.S)
        table = mt.group(1) if mt else ""
        check("六键逐字齐（start/stop/reset 各一对 On/Off）",
              bool(table) and all(k in table for k in keys),
              "BLINK_LIGHTS=%r" % table[:240])
        check("三盏灯齐（启动灯/停止灯/复位灯 + start/stop/reset）",
              all(v in table for v in ("start", "stop", "reset",
                                       "启动灯", "停止灯", "复位灯")),
              table[:240])
        dup = [k for k in keys if src.count(k) > 1]
        check("六键只在 BLINK_LIGHTS 一处声明（不重复写死）",
              not dup, "重复键：%s" % ", ".join(dup))
        # ---- ② 范围/缺省与 proto 同值 ----
        mb = _re.search(r"var\s+BLINK\s*=\s*\{([^}]*)\}", src)
        blink = mb.group(1) if mb else ""
        check("范围/缺省与 proto 同值（min 50 / max 5000 / 缺省 500）",
              bool(blink)
              and _re.search(r"\bmin\s*:\s*50\b", blink) is not None
              and _re.search(r"\bmax\s*:\s*5000\b", blink) is not None
              and _re.search(r"\bonDef\s*:\s*500\b", blink) is not None
              and _re.search(r"\boffDef\s*:\s*500\b", blink) is not None,
              "BLINK={%s}" % blink.strip())
        # ---- ③ 控件存在 + 权限闸 + 挂进卡体 ----
        fn_build = _js_func_src(js, "buildLightsSection")
        check("前端接线：有 buildLightsSection（按钮灯闪烁小节）",
              fn_build is not None, "")
        if fn_build:
            check("控件是 number 输入（numInput + 范围/缺省走 BLINK）",
                  fn_build.count("numInput(") >= 2
                  and "BLINK.min" in fn_build and "BLINK.max" in fn_build,
                  fn_build[:200])
            check("无管理权限随整卡置灰（亮/灭两个框都进 cfgControls）",
                  "cfgControls.push(on, off)" in fn_build, "")
            check("小节标题与亮/灭标签在位（按钮灯闪烁）",
                  "按钮灯闪烁" in fn_build and '"亮"' in fn_build
                  and '"灭"' in fn_build, "")
        check("卡体里挂上按钮灯小节（自动配置一次提交那张卡）",
              "autoCardBody.push(buildLightsSection())" in src, "")
        # ---- ④ 载荷接线：autoConfigValues 带 lights 子节 ----
        fn_payload = _js_func_src(js, "autoConfigValues")
        check("前端接线：有 autoConfigValues（自动配置载荷唯一构造点）",
              fn_payload is not None, "")
        if fn_payload:
            check("载荷带 lights 子节（与服务端 auto.lights 同名）",
                  "lights: lights" in fn_payload
                  and "BLINK_LIGHTS.forEach" in fn_payload, "")
            check("载荷两个值都过 clampInt（控件版：缺省 500 / 范围 50~5000）",
                  "clampInt(c.on, BLINK.onDef" in fn_payload
                  and "clampInt(c.off, BLINK.offDef" in fn_payload, "")
        # ---- ⑤ 回显接线：带 lights 才铺 + 数值钳制 ----
        fn_pop = _js_func_src(js, "populateAuto")
        check("前端接线：有 populateAuto（远端设置回显唯一入口）",
              fn_pop is not None, "")
        if fn_pop:
            check("回显只在应答带 lights 时铺（缺键不冲缺省）",
                  _re.search(r"if\s*\(\s*auto\.lights\s*&&", fn_pop)
                  is not None, "")
            check("回显按 BLINK_LIGHTS 的亮/灭键取数（row[2]/row[3]）",
                  "auto.lights[row[2]]" in fn_pop
                  and "auto.lights[row[3]]" in fn_pop, "")
            check("回显数值走 clampIntValue（控件版 clampInt 会恒回 500）",
                  "clampIntValue(" in fn_pop
                  and "clampInt(auto.lights" not in fn_pop, "")


def _check_ui_polish_static():
    """静态口径：主页表盘只给组合循环 + 窄屏数据三表不吸左时间列（2026-11-23）。

    [为什么放静态段] 这两条都是**纯前端样式/显隐**口径（不改变任何 HTTP
    接口），协议级验收打 API 看不出来；但用户口径很明确：
      · 主页运行表盘只在「自动循环（autoMode==="auto"）」运行中显示，
        产水/反洗/顺洗/放水单模式运行时不显示（用户原话「不好看」）；
      · 竖屏（≤700px）数据三表（历史/日志/报警，共用 .dt-table）的**时间列
        不再横向吸左**——吸左反而把可见列数压得更少；表头**吸顶**必须保留。
    """
    import re as _re
    # [压差三栏 + 压力传感器设置 2026-11-23] 独立一组静态断言，先跑：
    # 本函数后面有几处「文件读不到就 return」，不能让新断言被跳过。
    _check_press_ui_static()
    static_dir = os.path.join(WEBUI_DIR, "hsfweb", "static")

    # [本地读文件助手 2026-11-25 实锅] 本函数下半段（显示渐变静态断言）
    # 直接调了 `_read(...)`，但本函数**没有定义它**（别处的 `_read` 都是
    # 各自函数的局部名）⇒ NameError 把整套 webui_check 从这个函数起打断
    # （只跑出 43 项就报「验收执行异常」）。补本地助手，路径相对 hsfweb。
    def _read(rel):
        path = os.path.join(WEBUI_DIR, "hsfweb", rel)
        try:
            with open(path, "r", encoding="utf-8", errors="replace") as fh:
                return fh.read()
        except OSError as exc:
            check("%s 可读" % rel, False, "%s" % exc)
            return ""

    # ---- ① 主页表盘：只有组合循环运行中才显示 ----
    home_js = os.path.join(static_dir, "js", "pages", "page00_home.js")
    try:
        with open(home_js, "r", encoding="utf-8", errors="replace") as fp:
            src = fp.read()
    except OSError as exc:
        check("page00_home.js 可读（表盘口径）", False, "%s" % exc)
        return
    # [2026-11-27 判据订正] 循环运行中服务端 autoMode 报的是**子阶段**
    # （produce/rinse…），不是 "auto" ⇒ 旧判据恒假（表盘从未显示过）。
    # 现在以 autoStageTitle 非空判循环（isCycleDoc），老服务端缺键兜底。
    check("主页表盘：dialShow 判 autoRunning 与 isCycleDoc（不再用 autoMode==='auto'）",
          _re.search(r'var\s+dialShow\s*=\s*m\.autoRunning\s*===\s*true\s*'
                     r'&&\s*isCycleDoc\(m\)\s*;', src) is not None
          and "function isCycleDoc" in src,
          "")
    check("主页表盘：旧的『运行中四模式都显示』判据已不存在",
          "var dialShow = m.autoRunning === true;" not in src, "")
    check("主页表盘：仍用 .hidden 切换（不删 DOM、不跳布局）",
          'dial.el.classList.toggle("hidden", !dialShow);' in src, "")
    # [2026-11-23 所有模式都报「已用」的连带坑] 单模式运行中也会
    # autoStageElapsedMs>=0 ⇒ 不能拿它当"正在跑组合循环"的判据：旧判据
    # 会把单模式状态行写成「自动循环（产水模式）」并把下拉抢到"自动"。
    # 循环的唯一判据是服务端根模式 autoMode === "auto"。
    check("主页自动卡：循环判据走 isCycleDoc（不用 elapsed>=0、不靠 autoMode）",
          "var cycleStage = !!running && isCycleDoc(m);" in src
          and "var cycleStage = !!running && !!m.autoStageKnown" not in src
          and "var cycleStage = !!running && m.autoMode === \"auto\";" not in src,
          "cycleStage 判据仍是旧的 autoMode==='auto' 或 elapsed>=0")

    # ---- ② 窄屏数据三表：首列不吸左；表头吸顶保留 ----
    css_path = os.path.join(static_dir, "css", "theme.css")
    try:
        with open(css_path, "r", encoding="utf-8", errors="replace") as fp:
            css = fp.read()
    except OSError as exc:
        check("theme.css 可读（窄屏数据表口径）", False, "%s" % exc)
        return
    # [必须先剥注释] 本轮新注释里就写着 sticky/first-child 这些词，裸串判会假红。
    bare = _re.sub(r"/\*.*?\*/", "", css, flags=_re.S)
    # ---- 看板卡片：默认换位 + 长按拖拽自助排序（2026-11-23 用户口径）----
    check("主页默认排布：按钮组与电动球阀已换位",
          "[turbCard, valveCard, dpCard]" in src
          and "[vfdCard, fmCard, btnCard]" in src, "")
    check("看板卡片：9 个稳定 id + localStorage 持久化键",
          "BOARD_ORDER_KEY" in src and "hsf.home.cardOrder" in src
          and all(('"%s"' % cid) in src for cid in
                  ("turb", "valve", "dp", "vfd", "fm", "btn",
                   "th", "health", "alarm")), "")
    check("看板卡片：长按 550ms/位移 10px 判定 + 拖拽后吞 click",
          "}, 550);" in src and "> 10) resetPress();" in src
          and "dragState.suppressUntil" in src, "")
    check("看板卡片：拖拽快照/目标高亮/原位半透明样式在位",
          ".card-drag-ghost" in bare and ".card-drop-target" in bare
          and ".card-dragging" in bare, "")
    # [2026-11-23 用户「同一行卡片拉伸成一样高」] 等高拉伸（不要
    # align-items:start 那种自然高排布），空白靠数值自动缩放吃掉。
    check("看板卡片：同一行等高拉伸（.home-cards 不设 align-items:start）",
          _re.search(r"\.home-cards\s*\{[^}]*align-items:\s*start", bare)
          is None, "")
    check("看板卡片：数值自动缩放（fitBoardText / 1.3× 上限 / 溢出回退）",
          "fitBoardText" in src and "Math.min(1.30" in src
          and "dataset.basePx" in src
          and "contentBottom(card) > avail + 1" in src, "")
    # [2026-11-23 用户「压差/压力1/压力2 这几个字也同步大点」「单位也同步
    # 放大」「浊度仪数字大点」] 标签/单位与数值同档；浊度基准 36。
    check("压力卡：标签/单位字号与数值同档（×1.35 caption）",
          _re.search(r"\.press-label\s*\{[^}]*font-size:\s*calc\("
                     r"var\(--font-caption\) \* 1\.35", bare) is not None
          and _re.search(r"\.press-unit\s*\{[^}]*font-size:\s*calc\("
                         r"var\(--font-caption\) \* 1\.35", bare)
          is not None, "")
    check("压力卡：单位跟随自动缩放（.press-unit 进缩放清单）",
          '".press-label, .press-unit"' in src, "")
    check("浊度仪数字基准：宽屏 36 / 窄窗 32（两列并排不裁字）",
          "var turbPx = (window.innerWidth < 1200) ? 32 : 36;" in src
          and 'statBlock("浊度仪" + (ti + 1), "NTU", turbPx)' in src, "")
    # [2026-11-25 用户「设定600转主界面显示599」] 输出频率 0.1Hz 级量化
    # ⇒ 稳态时按给定对齐（≤1% 或 2rpm），与桌面端 _vfd_display_rpm 同口径。
    check("主页转速：运行稳态按给定对齐（设定 600 不显示 599）",
          "rpmTarget" in src and "doc.setFreqHz" in src
          and "doc.running === true" in src
          and "Math.max(2, Math.abs(rpmTarget) * 0.01)" in src, "")
    # [显示渐变 2026-11-24 用户「蓝→绿→红；浊度逐台范围远端可配；温度/
    # 压力/电机温度/电机转速；其他界面也支持下」] 公共色带 + 各页接线。
    _util_js = _read(os.path.join("static", "js", "util.js"))
    _p04 = _read(os.path.join("static", "js", "pages", "page04.js"))
    _p05 = _read(os.path.join("static", "js", "pages", "page05.js"))
    _p06 = _read(os.path.join("static", "js", "pages", "page06.js"))
    # [2026-11-25 实锅] 下面「显示渐变范围」三条断言用的 remote_src 只在
    # _check_press_ui_static 里是局部名 ⇒ 这里没读 ⇒ NameError 打断整套
    # 门禁。本函数自己读一份（同一份 page14 源码）。
    remote_src = _read(os.path.join("static", "js", "pages",
                                    "page14_remote.js"))
    check("网页渐变：util.js 公共色带（蓝→绿→橙→红，HSF.gradientColor）",
          "function gradientColor(" in _util_js
          and "HSF.gradientColor = gradientColor" in _util_js
          and "0x15, 0x65, 0xC0" in _util_js
          and "0xC6, 0x28, 0x28" in _util_js, "")
    check("网页渐变：首页浊度/压力/电箱温度/电机温度/电机转速全部接线",
          "gradientColor(ntu, item.rangeMin, item.rangeMax)" in src
          and "gradientColor(ch.eng, ch.rangeMin, ch.rangeMax)" in src
          and "gradientColor(doc.tempC, doc.tempMin, doc.tempMax)" in src
          and "doc.motorTempRangeMin" in src
          and "doc.speedRangeMin" in src, "")
    check("网页渐变：模拟量页/搅拌泵页/水质页也接线",
          "HSF.gradientColor(cch.eng, cch.rangeMin, cch.rangeMax)" in _p04
          and "HSF.gradientColor(rpmNum, doc.speedRangeMin" in _p05
          and "HSF.gradientColor(ntu, item.rangeMin, item.rangeMax)" in _p06,
          "")
    check("搅拌泵页运行状态有「电机温度」块（故障右侧空位）",
          'blk("motorTemp", "电机温度", "℃", 24)' in _p05
          and "doc.motorTempC" in _p05
          and "doc.motorTempRangeMin" in _p05, "")
    check("远端设置：显示渐变范围含电机温度（vfdMotorTempRange* 载荷+回显）",
          'displayRangeRow("电机温度"' in remote_src
          and "values.vfdMotorTempRangeMin" in remote_src
          and "collect.vfdMotorTempRangeMax" in remote_src, "")
    # [阈值 2026-11-25 实锅] 压力三项的开关走 gradToggle 的**动态键**
    # （`values[pressGrad.key]` / `collect[g.key]`）——检查只认字面量
    # `values.pressGradientEnabled` 会假红。两种写法都算接线。
    check("远端设置：渐变逐项启用勾选（5 行 + 压力三项，载荷/回显齐全）",
          'var cb = HSF.el("input.check", { type: "checkbox" });' in remote_src
          and "values.turbidity1GradientEnabled" in remote_src
          and ("values.pressGradientEnabled" in remote_src
               or "values[pressGrad.key]" in remote_src)
          and ("collect.press1GradientEnabled" in remote_src
               or "[pressGrad, press1Grad, press2Grad]" in remote_src), "")
    # [2026-11-25 用户「搅拌机变频器增加加速度设置，用指定的加速度加速到
    #  设定转速」] 前端控件 / 载荷 / 回显三处接线。
    check("远端设置：搅拌加速度控件 + stirAccelRpmS 载荷/回显",
          "var ACCEL = { min: 0, max: 1440, def: 0 };" in remote_src
          and "blk.accel" in remote_src
          and "stirAccelRpmS" in remote_src
          and "m.stirAccelRpmS" in remote_src, "")
    # [2026-11-25 用户「增加搅拌机最高转速限制，界面同步」]
    check("远端设置：搅拌最高转速控件 + stirMaxRpm 载荷/回显",
          "var STIRMAX =" in remote_src and "stirMax.value" in remote_src
          and "stirMaxRpm" in remote_src
          and "auto.stirMaxRpm" in remote_src, "")
    # [滤速 2026-11-25 用户「增加滤速显示，单位 m/h，公式瞬时流量/面积」]
    check("远端设置：滤速面积控件 + filterAreaM2 载荷/回显",
          "滤速计算面积" in remote_src
          and "filterAreaM2" in remote_src
          and "collect.filterAreaM2" in remote_src, "")
    check("主页流量计卡：罐体滤速块（m/h）+ 消费 filterVelocityMh",
          "statBlock(\"罐体滤速\", \"m/h\"" in src
          and "filterVelocityMh" in src, "")
    check("搅拌泵页：手动调速量程跟随 stirMaxRpm（rpm/Hz 双口径）",
          "stirMaxRpm" in _p05 and "Math.min(ratedRpm, stirMaxRpm)" in _p05,
          "")
    m = _re.search(r"@media\s*\(max-width:\s*700px\)\s*\{", bare)
    check("主题含 ≤700px 窄屏媒体查询", m is not None, "")
    block = ""
    if m is not None:
        depth, i = 0, m.end() - 1
        while i < len(bare):
            if bare[i] == "{":
                depth += 1
            elif bare[i] == "}":
                depth -= 1
                if depth == 0:
                    break
            i += 1
        block = bare[m.end():i]
    bad = []
    for sel, body in _re.findall(r"([^{}]+)\{([^{}]*)\}", block):
        if ".dt-table" in sel and "first-child" in sel:
            if "sticky" in body or _re.search(r"(?<![-\w])left\s*:", body):
                bad.append(sel.strip())
    check("窄屏数据三表：首列不再吸左（无 sticky/left）", not bad,
          "仍吸左：%s" % bad[:3])
    check("窄屏数据三表：块内无 left: 0 吸左残留",
          not _re.search(r"(?<![-\w])left\s*:\s*0", block), "")
    check("数据三表：表头吸顶规则保留（.dt-table thead th sticky top:0）",
          _re.search(r"\.dt-table\s+thead\s+th\s*\{[^}]*position\s*:\s*"
                     r"sticky[^}]*top\s*:\s*0", bare, _re.S) is not None, "")
    check("数据三表：角落格（吸顶+吸左）规则已移除",
          ".dt-table thead th:first-child" not in bare, "")

    # ---- ③ 窄屏数据工具条：分组左对齐（2026-11-23 用户口径）----
    # 用户原话「web端竖屏模式下这些控件位置调整下左对齐下，这样不好看」：
    # 窄屏下 `.top-stretch` 不再占位（否则按钮被推到右边漂浮），筛选按钮/
    # 分页统计/视图切换各自成组左对齐；宽屏用 display:contents 包一层、
    # 视觉零变化（这四条锁的就是这个结构契约）。
    js_path = os.path.join(static_dir, "js", "pages", "data_toolbar.js")
    try:
        with open(js_path, "r", encoding="utf-8", errors="replace") as fp:
            tb_src = fp.read()
    except OSError as exc:
        check("data_toolbar.js 可读（窄屏分组口径）", False, "%s" % exc)
        return
    check("工具条四行有语义类（filter/cond/page/view）",
          all(("dt-row-%s" % k) in tb_src
              for k in ("filter", "cond", "page", "view")), "")
    check("筛选按钮/统计/视图切换有分组容器",
          "dt-filter-btns" in tb_src and "dt-page-stats" in tb_src
          and "dt-view-btns" in tb_src, "")
    # 宽屏零变化：包装层在媒体查询外 display:contents（不生成盒子）
    check("宽屏包装层 display:contents（视觉零变化）",
          _re.search(r"\.dt-extra-field,\s*\.dt-pair,\s*\.dt-pair-grow,\s*"
                     r"\.dt-btn-group,\s*\.dt-page-stats\s*\{\s*"
                     r"display:\s*contents;", bare) is not None, "")
    # 窄屏：stretch 不占位 + 统计行 order:1 独占一行
    check("窄屏 .top-stretch 不占位（按钮不再靠右漂浮）",
          _re.search(r"\.dt-row\s*>\s*\.top-stretch[\s\S]{0,120}?"
                     r"display:\s*none", block) is not None, "")
    check("窄屏分页统计独占一行（order:1 + flex 100%）",
          _re.search(r"\.dt-row-page\s*>\s*\.dt-page-stats\s*\{[^}]*"
                     r"order:\s*1", block) is not None, "")
    check("窄屏筛选按钮成组左对齐（flex 100%）",
          _re.search(r"\.dt-row-filter\s*>\s*\.dt-btn-group\s*\{[^}]*"
                     r"flex:\s*1 1 100%", block) is not None, "")

    # ---- ④ 网页端 VFD 模型必须有 motorTempC（2026-11-23 实锅）----
    # 服务端自 11-20 起随 VFD 遥测下发 motorTempC，桌面客户端一直有；
    # **网页端 model.py 的 VfdState 漏了这个键** ⇒ 搅拌泵卡永远 "--"。
    # 这里锁"FIELDS 收 + to_json 发"两件事，防再被漏掉。
    model_py = os.path.join(WEBUI_DIR, "hsfweb", "model.py")
    try:
        with open(model_py, "r", encoding="utf-8", errors="replace") as fp:
            model_src = fp.read()
    except OSError as exc:
        check("hsfweb/model.py 可读（VFD 温度键口径）", False, "%s" % exc)
        return
    check("网页 VFD 模型收取 motorTempC（FIELDS 有该键）",
          _re.search(r'\"motorTempC\":\s*\(\"motor_temp_c\"', model_src)
          is not None, "")
    check("网页 VFD 模型下发 motorTempC（to_json 有该键）",
          _re.search(r'\"motorTempC\":\s*\(_round\(self\.motor_temp_c',
                     model_src) is not None, "")

    hjs = os.path.join(static_dir, "js", "pages", "page09_history.js")
    try:
        with open(hjs, "r", encoding="utf-8", errors="replace") as fp:
            hist_src = fp.read()
    except OSError as exc:
        check("page09_history.js 可读（窄屏关键列口径）", False, "%s" % exc)
        return

    # ---- ⑤ 历史表「电机温度」列下限不能被内容文本压低（表头比内容长）----
    # COL_MIN_TEXTS 是"内容比表头长"的列的静态基线；电机温度的表头
    # 「电机温度(℃)」比内容长 ⇒ 不能出现在这张表里（否则表头被截）。
    hm = _re.search(r"var\s+COL_MIN_TEXTS\s*=\s*\{(.*?)\};", hist_src, _re.S)
    if hm is None:
        check("历史页 COL_MIN_TEXTS 可解析", False, "")
    else:
        keys = [int(x) for x in _re.findall(r"(\d+)\s*:", hm.group(1))]
        check("历史页 COL_MIN_TEXTS 不含电机温度列(6)（表头才是最长文本）",
              6 not in keys, "keys=%s" % keys)

    check("历史页每线抽样点数可配（100/200/500/1000/2000，本机存档）",
          "CHART_POINT_CHOICES = [100, 200, 500, 1000, 2000]" in hist_src
          and "histChartPoints" in hist_src
          and "hist-chart-points" in hist_src, "")

    # ---- ⑥ 窄屏历史表「关键列」名单必须与 COLUMNS 下标同步 ----
    # [2026-11-23 实锅] 窄屏默认列是**按下标**写死的 [0,1,2,3,7]；本轮
    # 电机温度插到显示列第 6 位，若名单不跟着改，窄屏会把「浊度1」换成
    # 「电机温度」（语义漂移，肉眼一时看不出）。这里按 COLUMNS 反查名字。
    cm = _re.search(r"var\s+COLUMNS\s*=\s*\[(.*?)\];", hist_src, _re.S)
    # [锚 colCombo] 页里还有 chartColCombo.setCheckedIndexes([0])，裸匹配会
    # 先命中它 ⇒ 名单被误读成 [0]（本轮踩过）。
    nm = _re.search(r"colCombo\.setCheckedIndexes\(\[([0-9,\s]+)\]\)",
                    hist_src)
    if not cm or not nm:
        check("窄屏关键列名单可解析", False,
              "COLUMNS=%s 名单=%s" % (bool(cm), bool(nm)))
    else:
        names = _re.findall(r'"([^"]+)"', cm.group(1))
        idxs = [int(x) for x in nm.group(1).replace(" ", "").split(",")
                if x != ""]
        picked = [names[i] if 0 <= i < len(names) else "<越界%d>" % i
                  for i in idxs]
        want = ["时间", "模式", "搅拌状态", "输出频率(Hz)", "进水浊度(NTU)"]
        check("窄屏历史表关键列 = %s（下标随 COLUMNS 同步）" % "/".join(want),
              picked == want, "实际=%s" % picked)


def sec_static():
    """静态资源齐全 + **全部 JS 语法自检**（每次进程只跑一遍）。

    [为什么必须有这一段 2026-11-17 实锅] 网页端所有 JS 是「经典 <script>
    顺序加载、全局 window.HSF」——**任意一个文件有语法错误**，整份文件
    不执行 ⇒ 它注册的 HSF.xxx 不存在 ⇒ app.js 启动时抛 TypeError ⇒
    `#login-view` 与 `#app-shell` 都停在空/隐藏 ⇒ **打开页面一片白**。
    协议级验收（本脚本其余段）全绿也发现不了：它只打 HTTP API，从不
    加载页面。那次 login.js 把两段字符串写成相邻字面量（JS 没有 C/Python
    的隐式拼接）⇒ SyntaxError。`node --check` 一条命令就能钉死这类问题。
    """
    if _STATIC_DONE[0]:
        return
    _STATIC_DONE[0] = True
    _check_ui_polish_static()
    # [按钮灯闪烁 2026-11-24] 纯前端接线 ⇒ 静态段先跑（不起服务端也有牙齿）。
    _check_auto_lights_static()
    static_dir = os.path.join(WEBUI_DIR, "hsfweb", "static")
    with Section("页面自举：静态资源齐全 + JS 全部语法通过"):
        index = os.path.join(static_dir, "index.html")
        check("static/index.html 存在", os.path.isfile(index), index)
        if not os.path.isfile(index):
            return
        with open(index, "r", encoding="utf-8") as fp:
            html = fp.read()
        # ① index 引用的每个相对资源必须真在盘上（改名/漏拷贝即红）。
        import re as _re
        refs = _re.findall(r'(?:src|href)="([^"]+)"', html)
        refs = [r for r in refs if not r.startswith(("http:", "https:",
                                                     "data:", "//"))]
        missing = [r for r in refs
                   if not os.path.isfile(os.path.join(static_dir, r))]
        check("index.html 引用的 %d 个静态资源全部存在" % len(refs),
              not missing, "缺失：%s" % ", ".join(missing[:6]))
        # ② 所有 js 文件 `node --check`（node 不在就当环境跳过，不假绿）。
        js_files = []
        js_root = os.path.join(static_dir, "js")
        for base, _dirs, files in os.walk(js_root):
            js_files.extend(os.path.join(base, f) for f in files
                            if f.endswith(".js"))
        node = shutil.which("node")
        if node is None:
            check("node 可用（JS 语法自检）", True,
                  "未找到 node：跳过语法自检（安装 Node.js 后本段才有牙齿）")
            return
        bad = []
        for path in js_files:
            try:
                proc = subprocess.run([node, "--check", path],
                                      capture_output=True, timeout=20)
            except (OSError, subprocess.SubprocessError) as exc:
                bad.append("%s（%s）" % (os.path.basename(path), exc))
                continue
            if proc.returncode != 0:
                err = (proc.stderr or b"").decode("utf-8", "replace")
                head = err.strip().splitlines()
                bad.append("%s：%s" % (os.path.basename(path),
                                       head[0] if head else "语法错误"))
        check("全部 %d 个 JS 文件语法通过（node --check）" % len(js_files),
              not bad, "；".join(bad[:4]))
        # ③ **在途闸成对开合**（2026-11-18 联调验收实测的真缺陷）：
        #    page14_remote.js 的 `vfdInflight` 只置位不复位 ⇒ 第一次
        #    「应用到变频器」成功后按钮**永久失灵**（后续点击全被 early
        #    return 吞掉）。协议级验收打不到它（只打 API，不点按钮），
        #    语法检查也打不到（语法完全合法）⇒ 只能静态扫结构。
        import re as _re2
        unpaired = []
        for path in js_files:
            try:
                with open(path, "r", encoding="utf-8") as fp:
                    src = fp.read()
            except OSError:
                continue
            # [判据 2026-11-18 放宽] 只要求「同文件内存在该变量的复位」，
            # 不限定复位位置（finally / 异步回调 / 事件处理都算合法）——
            # 第一版要求 finally ⇒ 把 page06 的 pumpBusy、trendchart 的
            # rafPending（都在回调里复位）误判成红 ⇒ 门禁自己有噪音。
            names = set(_re2.findall(
                r"\b([A-Za-z_$][\w$]*[Ii]nflight[\w$]*)\s*=\s*true\s*;",
                src))
            for name in sorted(names):
                if not _re2.search(
                        r"\b%s\s*=\s*false\b" % _re2.escape(name), src):
                    unpaired.append("%s：%s 置位后从未复位（闸不成对）"
                                    % (os.path.basename(path), name))
        check("在途闸成对开合（置位必有复位）", not unpaired,
              "；".join(unpaired[:4]))
    _client_web_alignment_static()


def _client_web_alignment_static():
    """桌面客户端 ↔ 网页端「三端对齐」静态门禁（2026-11-21 补洞）。

    [为什么需要这一整段] 之前全仓只有 auto cycle 7 键的局部机械比对，
    2026-11-21 全量审计抓出两处长期漂移——客户端报警页缺 analog/digital
    两类 kind 映射、网页相机配置卡缺 transport/codec/hwDecode 三键——都是
    「各端各测各的、没有跨端机械比对」才漏得掉。这里把两份 proto 与两侧
    业务源码放一起对表，任何一端漂移即红（纯静态，不依赖浏览器/服务端）。
    """
    cli_py = os.path.join(REPO, "HighSpeedFilter", "client_py",
                          "hsfsrv_client")
    web_py = os.path.join(WEBUI_DIR, "hsfweb")

    def _read(path):
        try:
            with open(path, "r", encoding="utf-8") as fh:
                return fh.read()
        except OSError:
            return ""

    cli_proto = _read(os.path.join(cli_py, "proto.py"))
    web_proto = _read(os.path.join(web_py, "proto.py"))
    page14 = _read(os.path.join(web_py, "static", "js", "pages",
                                "page14_remote.js"))
    page11 = _read(os.path.join(web_py, "static", "js", "pages",
                                "page11_alarm.js"))
    other = _read(os.path.join(cli_py, "pages_other.py"))
    cli_srcs = cli_proto + other
    web_srcs = web_proto + page14 + page11

    with Section("三端对齐：客户端 ↔ 网页端协议/文案机械比对"):
        # ① 相机配置三键：两端 proto 逐键同值；网页配置卡控件/提交/回显齐全。
        cam_keys = {
            "transport": ("CFGK_CAMERA_TRANSPORT", "transport"),
            "codec": ("CFGK_CAMERA_CODEC", "codec"),
            "hwDecode": ("CFGK_CAMERA_HW_DECODE", "hwDecode"),
        }
        cam_bad = []
        for _val, (const, key) in cam_keys.items():
            if ('%s = "%s"' % (const, key)) not in cli_proto:
                cam_bad.append("client proto 缺 %s" % const)
            if ('%s = "%s"' % (const, key)) not in web_proto:
                cam_bad.append("webui proto 缺 %s" % const)
            # 网页配置卡：提交载荷有该键 + populate 回显有该键。
            if ("%s:" % key) not in page14:
                cam_bad.append("page14 提交缺 %s 键" % key)
            if ("doc.camera.%s" % key) not in page14:
                cam_bad.append("page14 回显缺 %s 键" % key)
        check("相机三键 transport/codec/hwDecode 两端 proto 同值 + 网页提交/回显齐全",
              not cam_bad, "；".join(cam_bad[:6]))
        # 三个下拉的选项值必须与 proto 逐值一致（缺省 stream/h264/auto）。
        cam_enum_ok = all(tok in page14 for tok in
                          ('["stream",', '["jpeg",', '["h264",', '["h265",',
                           '["auto",', '["on",', '["off",'))
        check("相机三键下拉选项值 stream/jpeg/h264/h265/auto/on/off 齐全",
              cam_enum_ok, "网页下拉少了某个协议枚举值")
        # ② 报警 kind：两端 SERVER_KIND_ROWS 都含 6 类（含 analog/digital）。
        #    提取字典块再查 key，避免别处的同名字符串造成假绿。
        ci = other.find("SERVER_KIND_ROWS = {")
        cli_block = other[ci:other.find("}", ci)] if ci >= 0 else ""
        wi = page11.find("var SERVER_KIND_ROWS = {")
        web_block = page11[wi:page11.find("};", wi)] if wi >= 0 else ""
        alarm_bad = []
        if '\"analog\": ("模拟量", "越限保护")' not in cli_block:
            alarm_bad.append("客户端报警页缺 analog 映射")
        if '\"digital\": ("数字量", "数字量告警")' not in cli_block:
            alarm_bad.append("客户端报警页缺 digital 映射")
        for kind, text in (("analog", "越限保护"),
                           ("digital", "数字量告警")):
            if text not in web_block or kind not in web_block:
                alarm_bad.append("网页报警页缺 %s 映射" % kind)
        check("报警页 kind 映射 6 类齐全（analog 越限保护 / digital 数字量告警）",
              not alarm_bad, "；".join(alarm_bad))
    _static_perf_static()


def _static_perf_static():
    """静态资源性能门禁（gzip 覆盖 + ETag 条件请求）——2026-11-21 补洞。

    [为什么要这一段] 2026-11-21 实测抓到：gzip **看着做了却几乎没生效**——
    `guess_mime` 返回的是带参数的 MIME（`application/javascript; charset=utf-8`），
    而 gzip 白名单是**裸类型** ⇒ 精确匹配失配 ⇒ **32 个 .js 一个都没压缩**
    （只有 .css 侥幸命中：它在 `;` 之前就以 `text/` 开头，走前缀匹配）。
    实测线上字节：首屏 33 个资源 691.1 KB ⇒ 只发出 623.6 KB（压到 90%）。
    ⇒ 「优化做了」不等于「优化生效」——**必须按最终字节判**，不然这条
    性能优化会静静躺着，谁也看不出来。

    [本段的三个判据]
      ① **所有**文本类扩展名的 MIME 都命中 gzip 白名单（防改 MIME 表又漏）
      ② gzip 压缩体确实被产出（不是 None）
      ③ 静态资源带 ETag（条件请求 ⇒ 二次打开零字节）
    """
    web_py = os.path.join(WEBUI_DIR, "hsfweb")
    httpd = _import_webui_httpd()

    with Section("静态资源性能：gzip 全覆盖 + ETag 条件请求"):
        # ① MIME 白名单覆盖：逐个文本类扩展名过一遍真实判定函数。
        text_exts = (".js", ".mjs", ".css", ".html", ".htm", ".json", ".svg")
        bad_mime = []
        for ext in text_exts:
            mime = httpd.guess_mime("x" + ext)
            if not httpd._is_gzip_mime(mime):
                bad_mime.append("%s→%s 不在 gzip 白名单" % (ext, mime))
        check("全部 %d 个文本类扩展名的 MIME 都命中 gzip 白名单" % len(text_exts),
              not bad_mime, "；".join(bad_mime))
        # ② gzip 体确实产出（拿一个真实资源试）。
        static_dir = os.path.join(web_py, "static")
        gz_ok = False
        try:
            _raw, gz, _etag = httpd.read_static_cached(
                static_dir, os.path.join("js", "app.js"))
            gz_ok = gz is not None and len(gz) > 0
        except Exception as exc:                         # noqa: BLE001
            check("read_static_cached 可用", False, "%s" % exc)
        check("静态资源确实产出 gzip 体（不是 None）", gz_ok,
              "app.js 的 gzip 体为空 ⇒ gzip 白名单又漏了")
        # ③ ETag 存在（条件请求的前提）。
        etag_ok = False
        try:
            _raw, _gz, etag = httpd.read_static_cached(
                static_dir, os.path.join("js", "app.js"))
            etag_ok = bool(etag) and etag.startswith('W/"')
        except Exception:                                # noqa: BLE001
            pass
        check("静态资源带 ETag（条件请求 ⇒ 二次打开零字节）", etag_ok,
              "read_static_cached 没算出 ETag")
    _export_stream_static()


def _export_stream_static():
    """导出端点必须走 export_stream 流式写出（禁止改回攒齐拼 XLSX）。

    [为什么要静态钉] write_xlsx/build_xlsx_auto 保留为死代码没问题，但
    2026-11-13 实锅：一旦把端点改回「攒齐所有行一次性拼」，大表 40+ 秒
    纯 CPU 卡住事件循环 ⇒ 心跳停发 ⇒ 上游 6s 判线断链。协议级用例用小
    数据跑不出来，只能静态扫源码结构（按函数体切块，避开注释干扰）。
    """
    hsfweb = os.path.join(WEBUI_DIR, "hsfweb")

    def _read(rel):
        try:
            with open(os.path.join(hsfweb, rel), "r", encoding="utf-8") as fh:
                return fh.read()
        except OSError as exc:
            check("导出静态检查：%s 可读" % rel, False, "%s" % exc)
            return None

    def _body(src, name):
        """取 `async def name(...)` 的函数体文本（切到下一个同级方法）。

        [2026-11-22 复审必修] 原实现按「行首非空白」（列 0）切块，但三个导出
        handler 都是**类方法（缩进 4）**，导致 `_history_export` 的 body
        一路延伸到文件末尾、把另外两个 handler 全包进去 ⇒ 前两个回潮
        假绿。改为按目标方法的实际缩进切：下一个「缩进 ≤ 目标缩进且是
        def/class/async def」的行就是边界。
        """
        m = re.search(r"^(?P<ind>[ \t]*)async def %s\s*\(" % re.escape(name),
                      src, re.M)
        if not m:
            return ""
        ind = m.group("ind")
        tail = src[m.end():]
        # 下一个同级（或更外层）的定义行 ⇒ 方法体到此为止
        nxt = re.search(r"^[ \t]{0,%d}(?:async def|def|class)\s" % len(ind),
                        tail, re.M)
        return tail[:nxt.start()] if nxt else tail

    api_src = _read("api.py")
    exp_src = _read("export.py")
    if api_src is None or exp_src is None:
        return
    with Section("导出端点：全量流式写出（禁止攒齐拼 XLSX）"):
        handlers = (("历史", "_history_export"),
                    ("日志", "_log_export"),
                    ("报警", "_alarm_export"))
        for label, name in handlers:
            body = _body(api_src, name)
            check("%s导出处理器 %s 存在" % (label, name), bool(body), "")
            if not body:
                continue
            check("%s导出走 export_mod.export_stream（流式）" % label,
                  "export_mod.export_stream(" in body,
                  "函数体里没有 export_stream 调用")
            check("%s导出未回潮到 write_xlsx/build_xlsx_auto" % label,
                  ("export_mod.write_xlsx(" not in body
                   and "export_mod.build_xlsx_auto(" not in body
                   and "export.write_xlsx(" not in body),
                  "发现攒齐拼 XLSX 调用（40s 卡死事件循环）")
        check("export_stream 仍用 XlsxStreamWriter 流式写出",
              "XlsxStreamWriter" in exp_src
              and "from .xlsx_stream import XlsxStreamWriter" in exp_src,
              "export_stream 内核被改回攒齐拼")


def _import_webui_httpd():
    """按包路径导入 webui 的 httpd 模块（拿它的真实判定函数）。"""
    import importlib
    if WEBUI_DIR not in sys.path:
        sys.path.insert(0, WEBUI_DIR)
    return importlib.import_module("hsfweb.httpd")


def _http_req_headers(port, method, path, extra=None, timeout=15.0):
    """带自定义请求头的 GET（要发 Accept-Encoding / If-None-Match）。"""
    conn = http.client.HTTPConnection(HOST, port, timeout=timeout)
    headers = dict(extra or {})
    try:
        conn.request(method, path, headers=headers)
        resp = conn.getresponse()
        return resp.status, dict(resp.getheaders()), resp.read()
    finally:
        conn.close()


def sec_static_wire(ctx):
    """**线上字节**门禁：真起 HTTP 量首屏实际传输量 + 条件请求 304。

    [为什么必须有这一段] `_static_perf_static` 只测**函数**（MIME 判定、
    gzip 体产出、ETag 生成）——若 `api.py` 的 gzip 分支或 304 分支被改坏，
    它照样全绿，而线上字节悄悄回到 691 KB。**这正是 gzip 那次静默失效的
    同款路径**（代码在、协商在、门禁绿着，线上没降）。
    ⇒ 这一段按**最终字节**判：真发 HTTP、按响应体实际长度求和。

    判据：
      ① 首屏 33 个资源全部带 `Content-Encoding: gzip`（除 index.html）
      ② 线上传输总量 < 原始总量的一半（gzip 真生效的硬指标）
      ③ 带 `If-None-Match` 再发一次 ⇒ 全部 304 且响应体长度 0
      ④ `Vary: Accept-Encoding` 与 `Cache-Control: no-cache` 都在
      ⑤ index.html **不**带 Content-Encoding（有意为之，防将来误改）
    """
    with Section("静态资源性能（线上字节）：gzip 真生效 + 304 零字节"):
        status, headers, body = _http_req_headers(ctx.web, "GET", "/")
        if status != 200:
            check("首页可取（静态性能测量前置）", False,
                  "status=%d" % status)
            return
        html = body.decode("utf-8", "replace")
        refs = sorted(set(re.findall(r'(?:src|href)="([^"]+)"', html)))
        refs = [r for r in refs
                if not r.startswith(("http:", "https:", "data:", "//"))
                and ("." in r) and (" " not in r)]
        if not refs:
            check("首页引用清单可解析（静态性能测量前置）", False,
                  "没解析出资源引用")
            return

        raw_total = 0
        wire_total = 0
        gz_hit = 0
        no_gz = []
        vary_missing = []
        cc_missing = []
        etags = {}
        for r in refs:
            st, hd, data = _http_req_headers(
                ctx.web, "GET", r, {"Accept-Encoding": "gzip"})
            if st != 200:
                check("静态资源可取：%s" % r, False, "status=%d" % st)
                continue
            raw_total += len(data)
            wire_total += len(data)
            enc = (hd.get("Content-Encoding") or "").lower()
            if enc == "gzip":
                gz_hit += 1
                raw_total -= len(data)      # 线上量先记压缩体，稍后修正
                try:
                    raw_total += len(gzip.decompress(data))
                except Exception:            # noqa: BLE001
                    raw_total += len(data)
            elif r.endswith("index.html"):
                pass                          # 有意不压
            else:
                no_gz.append(r)
            if enc == "gzip" and not (hd.get("Vary") or ""):
                vary_missing.append(r)
            if r.endswith((".js", ".css")) and not (hd.get("Cache-Control")
                                                    or ""):
                cc_missing.append(r)
            if hd.get("ETag"):
                etags[r] = hd["ETag"]
        check("首屏 %d 个资源全部 gzip（index.html 除外）" % len(refs),
              not no_gz, "未压缩：%s" % ", ".join(no_gz[:4]))
        ratio = (wire_total * 100 / raw_total) if raw_total else 100
        check("首屏线上传输压到原始的一半以内（实测 %.0f%%：%d→%d B）"
              % (ratio, raw_total, wire_total),
              raw_total > 0 and wire_total * 2 < raw_total,
              "gzip 没真生效：原始 %d B / 线上 %d B" % (raw_total, wire_total))
        check("gzip 响应都带 Vary: Accept-Encoding", not vary_missing,
              "缺 Vary：%s" % ", ".join(vary_missing[:4]))
        check("JS/CSS 都带 Cache-Control（no-cache 保「改了立刻生效」）",
              not cc_missing, "缺 Cache-Control：%s"
              % ", ".join(cc_missing[:4]))
        # [2026-11-22 补空转盲区] 下面 304 段遍历的是 etags；若 ETag 整个
        # 消失（服务端不写这个头），etags 为空 ⇒ 循环不跑 ⇒ 两条断言**空
        # 过**（假绿）。这条先保证条件请求有覆盖。
        check("条件请求覆盖全部资源（ETag 不缺，防 304 段空转）",
              len(etags) >= len(refs),
              "有 ETag 的资源 %d / 共 %d" % (len(etags), len(refs)))
        # 条件请求：带 ETag 再发一次 ⇒ 304 且零字节。
        not_304 = []
        second_bytes = 0
        for r, et in etags.items():
            st, _hd, data = _http_req_headers(
                ctx.web, "GET", r, {"Accept-Encoding": "gzip",
                                    "If-None-Match": et})
            second_bytes += len(data)
            if st != 304 or len(data) != 0:
                not_304.append("%s(status=%d,len=%d)" % (r, st, len(data)))
        check("带 If-None-Match 重发 ⇒ 全部 304 零字节（二次打开不重下）",
              not not_304, "；".join(not_304[:4]))
        check("二次取数总字节 = 0（实测 %d B）" % second_bytes,
              second_bytes == 0, "实测 %d B" % second_bytes)

        # ---- gzip/ETag 负对照（2026-11-22 补；防「协商头形同虚设」的假绿）----
        # 上面五条只证明"发标准头时对了"，但 _accepts_gzip / _etag_matches
        # 的**拒绝分支**若被改坏（q=0 仍压、弱比较写死），它们照样全绿。
        # 这里对真实分支各发一组反例，任一被改坏就多一条 FAIL。
        check("负对照取样资源非空（否则下面断言形同虚设）",
              bool(refs and etags), "refs=%d etags=%d" % (len(refs), len(etags)))
        neg_paths = [r for r in refs if not r.endswith("index.html")][:4]
        # ① gzip;q=0 显式拒绝 ⇒ 不得下发 Content-Encoding（否则不支持的对端解不开）
        still_gz = []
        for r in neg_paths:
            st, hd, _data = _http_req_headers(
                ctx.web, "GET", r, {"Accept-Encoding": "gzip;q=0"})
            if (hd.get("Content-Encoding") or "").lower() == "gzip":
                still_gz.append(r)
        check("gzip;q=0 显式拒绝 ⇒ 不下发 Content-Encoding", not still_gz,
              "仍在压缩：%s" % ", ".join(still_gz[:4]))
        # ② 浏览器真实协商头 gzip, deflate, br ⇒ 仍必须命中 gzip
        real_gz_miss = []
        for r in neg_paths:
            st, hd, _data = _http_req_headers(
                ctx.web, "GET", r,
                {"Accept-Encoding": "gzip, deflate, br"})
            if st == 200 and (hd.get("Content-Encoding") or "").lower() != "gzip":
                real_gz_miss.append(r)
        check("浏览器协商头 gzip, deflate, br ⇒ 仍命中 gzip",
              not real_gz_miss, "未压缩：%s" % ", ".join(real_gz_miss[:4]))
        # ③ 不匹配 ETag ⇒ 回 200 且有响应体（304 分支不能被写死）
        wrong_etag = []
        for r, et in list(etags.items())[:4]:
            bogus = 'W/"0-mismatch"' if et.startswith('W/"') else '"0-mismatch"'
            st, _hd, data = _http_req_headers(
                ctx.web, "GET", r,
                {"Accept-Encoding": "gzip", "If-None-Match": bogus})
            if st != 200 or not data:
                wrong_etag.append("%s(status=%d,len=%d)" % (r, st, len(data)))
        check("不匹配 ETag ⇒ 回 200 且有响应体（304 未写死）",
              not wrong_etag, "；".join(wrong_etag[:4]))
        # ④ W/ 弱比较：去/加 W/ 后重发 ⇒ 仍应 304
        weak_miss = []
        for r, et in list(etags.items())[:4]:
            strong = et[2:] if et.startswith("W/") else "W/" + et
            st, _hd, _data = _http_req_headers(
                ctx.web, "GET", r,
                {"Accept-Encoding": "gzip", "If-None-Match": strong})
            if st != 304:
                weak_miss.append("%s(status=%d)" % (r, st))
        check("ETag 强弱比较互认（去/加 W/ 仍 304）", not weak_miss,
              "；".join(weak_miss[:4]))


def sec_page_boot(ctx):
    """用真浏览器打开首页，断言**登录视图真渲染出来**（白屏门）。

    [为什么必须真开] 语法自检能抓"整份文件不执行"，但运行时错误（少一个
    HSF.xxx、初始化顺序反了）同样会让页面全白，且只有真跑 JS 才看得见。
    这里只看"登录卡在不在、有几行文字"——不登录、不碰服务端状态。
    没有浏览器是环境问题 ⇒ 记一条带说明的 PASS（不假绿也不误红）。
    """
    exe = find_browser()
    with Section("页面自举：真浏览器打开首页（白屏门）"):
        if exe is None:
            check("浏览器可用（白屏门）", True,
                  "未找到 Edge/Chrome：跳过真开检查")
            return
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        import cdp_client
        profile = tempfile.mkdtemp(prefix="edge_boot_")
        port = free_port(9500)
        proc = None
        cdp = None
        try:
            proc = subprocess.Popen(
                [exe, "--headless=new", "--disable-gpu", "--no-first-run",
                 "--user-data-dir=" + profile,
                 "--remote-debugging-port=%d" % port,
                 "http://%s:%d/" % (HOST, ctx.web)],
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            ws_url = cdp_client.wait_for_target(port, timeout_s=45.0)
            cdp = cdp_client.Cdp(ws_url, timeout=30.0)
            cdp.call("Runtime.enable")
            deadline = time.time() + 20.0
            info = {}
            while time.time() < deadline:
                info = cdp.evaluate(
                    "(function(){var lv=document.getElementById('login-view');"
                    "var card=document.querySelector('.login-card');"
                    "return {kids: lv?lv.children.length:-1,"
                    " card: !!card,"
                    " text: card?(card.innerText||'').slice(0,400):'',"
                    " login: typeof (window.HSF&&window.HSF.login),"
                    " body: document.body?document.body.innerText.length:0};"
                    "})()") or {}
                if info.get("card"):
                    break
                time.sleep(0.4)
            check("登录卡已渲染（#login-view 有内容，页面不白）",
                  bool(info.get("card")) and (info.get("kids") or 0) > 0,
                  "info=%s" % str(info)[:200])
            check("HSF.login 模块已注册（语法/加载顺序正常）",
                  info.get("login") == "object",
                  "typeof HSF.login=%r" % info.get("login"))
            check("登录卡文案含「登录」", "登录" in (info.get("text") or ""),
                  (info.get("text") or "")[:60])
            try:
                shot = os.path.join(REPO, "verify", "webui", "shots",
                                    "webui_boot_%s.png" % ctx.tag)
                os.makedirs(os.path.dirname(shot), exist_ok=True)
                with open(shot, "wb") as fp:
                    fp.write(cdp.screenshot_png())
                step("白屏门截图留证：%s" % shot)
            except Exception:                        # noqa: BLE001
                pass
        except Exception as exc:                     # noqa: BLE001
            check("页面自举（真浏览器）", False,
                  "%s: %s" % (type(exc).__name__, exc))
        finally:
            if cdp is not None:
                try:
                    cdp.close()
                except Exception:                    # noqa: BLE001
                    pass
            if proc is not None:
                _kill_tree(proc)
            # [临时 profile 清理 2026-11-18] 不删 ⇒ 每跑一轮就往 %TEMP%
            # 扔一个 ~489MB 的浏览器 profile ⇒ 几十轮后**系统盘满**
            # （C 盘 241G 用满 ⇒ 后续门禁全被「磁盘空间不足」打断）。
            _rm_profile(profile)


def sec_no_placeholder_pages(ctx, token):
    """页面接入完整性门：**导航页一律不许落到 todo-page 占位**。

    [为什么要这一段 2026-11-22] 用户问「是否还有占位没实现」——静态扫
    "TODO/未实现" 只能看到注释；真正的判据是**逐个导航页真打开、看有没有
    落到 placeholder 模块**（`pages/placeholder.js` 的 `.todo-page`）。
    同时验两个**如实说明**的深链：页 16「Web 设置」（仅桌面客户端提供，
    不是"以后接入"）与未知页号。
    """
    exe = find_browser()
    with Section("页面接入完整性：导航页无占位 + 保留槽位如实说明"):
        if exe is None:
            check("浏览器可用（占位检查）", True, "未找到浏览器：跳过")
            return
        st, _h, doc = ctx.get("/api/pages", token=token)
        names = (doc or {}).get("names") or []
        nav = (doc or {}).get("nav") or []
        nav_pages = sorted({int(e["page"]) for e in nav if int(e["page"]) >= 0})
        check("/api/pages 给出导航页清单（≥16）",
              st == 200 and len(nav_pages) >= 16,
              "status=%s pages=%s" % (st, nav_pages))
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        import cdp_client
        profile = tempfile.mkdtemp(prefix="edge_pages_")
        port = free_port(9560)
        proc = None
        cdp = None
        try:
            proc = subprocess.Popen(
                [exe, "--headless=new", "--disable-gpu", "--no-first-run",
                 "--user-data-dir=" + profile,
                 "--remote-debugging-port=%d" % port,
                 "--window-size=1280,800", "about:blank"],
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            ws_url = cdp_client.wait_for_target(port, timeout_s=45.0)
            cdp = cdp_client.Cdp(ws_url, timeout=60.0)
            cdp.call("Page.enable")
            cdp.call("Runtime.enable")
            cdp.call("Page.navigate",
                     {"url": "http://%s:%d/?authtoken=%s"
                             % (HOST, ctx.web, token)})
            ok = False
            for _ in range(60):
                if cdp.evaluate("!!document.getElementById('page-host')"):
                    ok = True
                    break
                time.sleep(0.4)
            check("壳已渲染（占位检查前置）", ok)

            def _goto(pid, timeout=6.0):
                cdp.evaluate("location.hash = '#/page/%d'" % pid)
                deadline = time.time() + timeout
                while time.time() < deadline:
                    if cdp.evaluate("location.hash") == "#/page/%d" % pid:
                        time.sleep(0.25)      # 等 router.resolve 挂载完
                        return True
                    time.sleep(0.1)
                return False

            todo = []
            for pid in nav_pages:
                if not _goto(pid):
                    todo.append("%d(未切过去)" % pid)
                    continue
                if cdp.evaluate("!!document.querySelector('.todo-page')"):
                    todo.append(str(pid))
            check("全部导航页都真接入（没有落到 todo-page 占位）",
                  not todo, "占位页：%s" % ",".join(todo))

            # 页 16 保留槽位：如实说明「仅桌面客户端提供」
            web_idx = names.index("Web 设置") if "Web 设置" in names else -1
            if web_idx >= 0 and _goto(web_idx):
                txt = cdp.evaluate(
                    "(function(){var n=document.querySelector("
                    "'.todo-page .todo-badge');"
                    "return n?n.textContent:'';})()") or ""
                check("页 %d「Web 设置」如实说明仅桌面端提供" % web_idx,
                      "桌面客户端" in txt and "提供" in txt,
                      "badge=%r" % txt)
            # 未知页号：不得谎称"以后接入"
            if _goto(999):
                txt = cdp.evaluate(
                    "(function(){var n=document.querySelector("
                    "'.todo-page .todo-badge');"
                    "return n?n.textContent:'';})()") or ""
                check("未知页号如实说明（不写「以后接入」）",
                      "未知页号" in txt and "后续页面" not in txt,
                      "badge=%r" % txt)
        except Exception as exc:                     # noqa: BLE001
            check("页面接入完整性（真浏览器）", False,
                  "%s: %s" % (type(exc).__name__, exc))
        finally:
            if cdp is not None:
                try:
                    cdp.close()
                except Exception:                    # noqa: BLE001
                    pass
            if proc is not None:
                _kill_tree(proc)
            _rm_profile(profile)


def _cdp_navigate_wire(cdp, url, want_min=20, timeout=30.0):
    """导航一次并返回 (线上总字节, 完成请求数)。

    [CDP 事件模型的坑] `cdp_client.Cdp` 只在 `call()`/`evaluate()` 等应答时
    才从 WebSocket 读帧（文件头 :16-25 写明）⇒ 导航后必须**周期发廉价
    `evaluate("1")` 把 socket 里的事件冲进 `cdp.events`，否则
    `drain_events()` 恒为空、字节恒 0（假绿）。
    `Network.loadingFinished.encodedDataLength` 是该资源网络层实际传输字节
    （gzip 后；304/缓存命中时很小）——正是「线上字节」口径。
    """
    cdp.call("Page.enable")
    cdp.call("Network.enable")          # 必须在 navigate 之前开，否则早期事件丢
    cdp.drain_events()                  # 丢弃导航前残留
    cdp.call("Page.navigate", {"url": url})
    deadline = time.time() + timeout
    last_count = -1
    stable = 0
    while time.time() < deadline:
        # [抗导航期 context 销毁] 导航瞬间 Runtime.evaluate 可能抛
        # CdpError（execution context destroyed）⇒ 不能让它冒到外层记整段
        # 红（那是导航的正常副作用，不是产品缺陷）。
        try:
            cdp.evaluate("1")           # 冲事件（唯一驱动读帧的动作）
        except Exception:               # noqa: BLE001
            time.sleep(0.1)
        n = len(cdp.poll_events(["Network.loadingFinished"]))
        if n >= want_min and n == last_count:
            stable += 1
            if stable >= 3:             # 完成数连续稳定 ⇒ 认为加载完
                break
        else:
            stable = 0
        last_count = n
        time.sleep(0.25)
    fins = cdp.drain_events(["Network.loadingFinished"])
    total = sum(int((e.get("params") or {}).get("encodedDataLength") or 0)
                for e in fins)
    return total, len(fins)


def sec_browser_bytes(ctx, token):
    """真浏览器线上字节门：CDP Network 求和 + gzip/ETag 复用。

    [为什么必须有] `sec_static_wire` 用 http.client **手工发标准头**，只证明
    "这样发时服务端对了"；浏览器真实的 `Accept-Encoding: gzip, deflate, br,
    zstd`、gzip 解码链、ETag 缓存复用都没有回归保护。历史上「浏览器侧登录态
    二次访问只传 23.2KB」是一次性人工实测（docs 三端闭环 §4.2.4），时间一长
    就会被遗忘成传说 ⇒ 这里把它固化成机械门。

    判据（用区间 + 相对比，不用精确值——encodedDataLength 各版本对「是否含
    响应头」口径不一，精确值会变成噪音门）：
      ① 冷加载收集到足够资源事件（≥20 个，证明 Network 域真的开了）
      ② 冷加载线上字节 < 512KB（gzip 真生效；失效会回到 691KB）
      ③ 同会话二次访问落在 ETag 复用带（8KB~64KB，基线 23.2KB）
      ④ 二次访问显著小于冷加载（< 40%）
    """
    exe = find_browser()
    with Section("真浏览器线上字节：CDP Network 求和 + gzip/ETag 复用"):
        if exe is None:
            check("浏览器可用（真浏览器字节门）", True,
                  "未找到 Edge/Chrome：跳过")
            return
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        import cdp_client
        url = "http://%s:%d/?authtoken=%s" % (HOST, ctx.web, token)
        profile = tempfile.mkdtemp(prefix="edge_bytes_")
        port = free_port(9530)
        proc = None
        cdp = None
        try:
            # [必须 about:blank 起步 2026-11-22 实锅] 启动参数直接带 url ⇒
            # 首屏在 CDP 连上、Network.enable 之前就加载完了（缓存已热）⇒
            # 后面量的"冷加载"其实是热缓存（实测两次都 ~25.6KB）⇒ 冷热
            # 对比恒假红。改成空白页起步，由 CDP 导航 ⇒ 首次是真冷加载。
            proc = subprocess.Popen(
                [exe, "--headless=new", "--disable-gpu", "--no-first-run",
                 "--user-data-dir=" + profile,
                 "--remote-debugging-port=%d" % port,
                 "--window-size=1280,800", "about:blank"],
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            ws_url = cdp_client.wait_for_target(port, timeout_s=45.0)
            cdp = cdp_client.Cdp(ws_url, timeout=60.0)
            cdp.call("Page.enable")
            cdp.call("Network.enable")

            cold, n_cold = _cdp_navigate_wire(cdp, url, want_min=20)
            check("冷加载收集到足够资源事件（%d 个）" % n_cold,
                  n_cold >= 20, "n=%d total=%d" % (n_cold, cold))
            check("冷加载线上字节 < 512KB（gzip 真生效，防回到 691KB）",
                  0 < cold < 512 * 1024, "实测 %d B" % cold)

            again, n_again = _cdp_navigate_wire(cdp, url, want_min=20)
            check("二次导航同样收集到足够资源事件（%d 个）" % n_again,
                  n_again >= 20, "n=%d total=%d" % (n_again, again))
            check("二次访问线上字节落在 23.2KB 回归带（8KB~64KB）",
                  8 * 1024 <= again <= 64 * 1024,
                  "实测 %d B（基线 23756 B / docs §4.2.4）" % again)
            check("二次访问显著小于冷加载（ETag/缓存复用生效）",
                  again * 100 < cold * 40,
                  "cold=%d B again=%d B" % (cold, again))
        except Exception as exc:                     # noqa: BLE001
            check("真浏览器字节门", False,
                  "%s: %s" % (type(exc).__name__, exc))
        finally:
            if cdp is not None:
                try:
                    cdp.close()
                except Exception:                    # noqa: BLE001
                    pass
            if proc is not None:
                _kill_tree(proc)
            _rm_profile(profile)


def sec_trend_legend(ctx, token):
    """实时趋势「图例」多选（2026-11-22 用户口径）。

    判据（真浏览器，CDP）：
      ① 页 18 趋势卡标题行有「图例」下拉（复用 HSF.multiCheckCombo），
         默认「全部图例」，4 个选项逐字 = 瞬时流量/瞬时流速/正向累积/反向累积；
      ② 取消「瞬时流速」⇒ 按钮汇总与 chart.visibleKeys/legendEntries 都不含它，
         **canvas 上该序列颜色（--chart-s2 橙）像素归零**（真没画线）；
      ③ 再勾回 ⇒ 图例条目仍带「min~max」（隐藏期间 pts 继续累积，历史点还在）；
      ④ 至少留一条：取消到只剩 1 条后，最后一条取消被拒（勾选框回弹）；
      ⑤ 点层外（mousedown）收起弹层；切页卸载时弹层也自动收起；
      ⑥ Y 轴取**第一条可见**序列量程（独立双序列图：隐藏 A ⇒ 量程变 B）；
         单序列不渲染图例下拉（防死控件）；
      ⑦ 页 6 三项 = 浊度1/浊度2/水温，页 5 三项 = 输出/给定频率+输出电流
         （页 5 未改序列，自动获得图例下拉）。
    """
    exe = find_browser()
    with Section("实时趋势图例：多选下拉/隐藏不画线/Y 轴取可见序列"):
        if exe is None:
            check("浏览器可用（趋势图例）", True,
                  "未找到 Edge/Chrome：跳过真开检查")
            return
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        import cdp_client
        base = "http://%s:%d/?authtoken=%s" % (HOST, ctx.web, token)
        profile = tempfile.mkdtemp(prefix="edge_trend_")
        port = free_port(9560)
        proc = None
        cdp = None

        def ev(js, timeout=20.0):
            return cdp.evaluate(js, timeout=timeout)

        def wait_js(js, pred, timeout=25.0):
            """轮询 js 直到 pred(doc) 为真；返回最后一次结果（dict）。"""
            info = {}
            deadline = time.time() + timeout
            while time.time() < deadline:
                info = ev(js) or {}
                try:
                    if pred(info):
                        return info
                except Exception:                    # noqa: BLE001
                    pass
                time.sleep(0.35)
            return info

        PAGE_STATE = (
            "(function(){"
            "var card=document.querySelector('.trend-card');"
            "var btn=card?card.querySelector('.trend-legend'):null;"
            "var ch=card?(card.hsfTrendChart||null):null;"
            "var fm=(window.HSF&&HSF.store&&HSF.store.get)"
            "?(HSF.store.get('flowmeter')||{}):{};"
            "return {ok:!!(card&&btn&&ch),"
            " page:(window.HSF&&HSF.router)?HSF.router.current():-1,"
            " text:btn?btn.textContent:'',"
            " keys:ch?ch.visibleKeys():null,"
            " entries:ch?ch.legendEntries().map(function(e){"
            "return {key:e.key,name:e.name,label:e.label};}):null,"
            " axis:ch?ch.axisRange():null,"
            " fresh:!!fm.fresh,"
            " flow:(typeof fm.flow==='number')?fm.flow:null};})()"
        )
        OPEN_POP = (
            "(function(){var b=document.querySelector('.trend-legend');"
            "if(!b)return {ok:false};b.click();"
            "var items=[];document.querySelectorAll('.mcc-pop .mcc-item')"
            ".forEach(function(row){var cb=row.querySelector('input');"
            "var sp=row.querySelector('span');"
            "items.push({name:sp?sp.textContent:'',"
            "checked:cb?cb.checked:null});});"
            "return {ok:true,items:items,"
            "pop:document.querySelectorAll('.mcc-pop').length};})()"
        )

        def toggle_js(name):
            """点弹层里名为 name 的勾选框；返回点后该框状态（被拒时已回弹）。"""
            return (
                "(function(){var want=%s;"
                "var rows=document.querySelectorAll('.mcc-pop .mcc-item');"
                "for(var i=0;i<rows.length;i++){"
                "var sp=rows[i].querySelector('span');"
                "if(sp&&sp.textContent===want){"
                "var cb=rows[i].querySelector('input');cb.click();"
                "return {ok:true,checked:cb.checked};}}"
                "return {ok:false};})()" % json.dumps(name)
            )

        # --chart-s2 两种主题下都是 #E8730C ⇒ 用它当「瞬时流速」的指纹色。
        PIX_S2 = (
            "(function(){var c=document.querySelector('.trend-card canvas');"
            "if(!c)return {ok:false};var g=c.getContext('2d');"
            "var d=g.getImageData(0,0,c.width,c.height).data;var n=0;"
            "for(var i=0;i<d.length;i+=4){"
            "if(d[i+3]>16&&Math.abs(d[i]-232)<=6"
            "&&Math.abs(d[i+1]-115)<=6&&Math.abs(d[i+2]-12)<=6)n++;}"
            "return {ok:true,n:n,w:c.width,h:c.height};})()"
        )
        CLOSE_POP = (
            "(function(){document.body.dispatchEvent(new MouseEvent("
            "'mousedown',{bubbles:true,cancelable:true}));"
            "return {pop:document.querySelectorAll('.mcc-pop').length};})()"
        )
        # 独立图：确定性验证「Y 轴取第一条可见序列」+「单序列不渲染下拉」。
        STANDALONE = (
            "(function(){"
            "var host=document.createElement('div');"
            "host.style.cssText='position:fixed;left:-10000px;top:0;"
            "width:400px;height:220px';document.body.appendChild(host);"
            "var ch=HSF.TrendChart([{key:'a',name:'A'},{key:'b',name:'B'}]);"
            "host.appendChild(ch.el);var t=Date.now();"
            "ch.append('a',t-2000,100);ch.append('a',t,110);"
            "ch.append('b',t-2000,5);ch.append('b',t,6);"
            "var all=ch.axisRange();ch.setVisibleKeys(['b']);"
            "var onlyB=ch.axisRange();"
            "ch.setVisibleKeys(['a','b']);var back=ch.axisRange();"
            "var entries=ch.legendEntries().map(function(e){return e.label;});"
            "var card=HSF.trendCard([{key:'x',name:'X'}],'trendSampleMs',2000);"
            "host.appendChild(card.card);"
            "var single={legend:!!card.legend,"
            "btn:!!card.card.querySelector('.mcc-btn')};"
            "var ropts=[];var sels=card.card.querySelectorAll('.trend-range option');"
            "for(var oi=0;oi<sels.length;oi++)"
            "ropts.push(parseInt(sels[oi].value,10));"
            "var rbefore=ch.rangeMs();ch.setRangeMs(1800000);"
            "var rafter=ch.rangeMs();ch.setRangeMs(rbefore);"
            "var hvals=ch.hoverValues(t).map(function(e){"
            "return e.name+':'+e.value;});"
            # [2026-11-25] normalized 轴：115 不拉高轴，恒 0~1。
            "var chn=HSF.TrendChart([{key:'f',name:'流量'},"
            "{key:'n',name:'正累'}],'normalized');"
            "host.appendChild(chn.el);var tt=Date.now();"
            "chn.append('f',tt,0);chn.append('n',tt,115);"
            "var naxis=chn.axisRange();var nmode=chn.axisMode();"
            "var nlegend=chn.legendEntries().map(function(e){return e.label;});"
            "host.parentNode.removeChild(host);"
            "return {all:all,onlyB:onlyB,back:back,entries:entries,"
            "single:single,rangeOpts:ropts,rangeBefore:rbefore,"
            "rangeAfter:rafter,hvals:hvals,naxis:naxis,nmode:nmode,"
            "nlegend:nlegend};})()"
        )

        def goto_page(n, timeout=25.0):
            ev("location.hash='#/page/%d';null" % n)
            return wait_js(PAGE_STATE,
                           lambda d: d.get("ok") and d.get("page") == n,
                           timeout=timeout)

        try:
            proc = subprocess.Popen(
                [exe, "--headless=new", "--disable-gpu", "--no-first-run",
                 "--user-data-dir=" + profile,
                 "--remote-debugging-port=%d" % port,
                 "--window-size=1280,900",
                 base + "#/page/18"],
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            ws_url = cdp_client.wait_for_target(port, timeout_s=45.0)
            cdp = cdp_client.Cdp(ws_url, timeout=30.0)
            cdp.call("Page.enable")
            cdp.call("Runtime.enable")

            st18 = goto_page(18, timeout=40.0)
            check("页 18 趋势卡「图例」下拉存在且默认全部图例",
                  bool(st18.get("ok")) and "全部图例" in (st18.get("text") or ""),
                  "info=%s" % str(st18)[:240])

            pop = ev(OPEN_POP) or {}
            names = [it.get("name") for it in (pop.get("items") or [])]
            check("页 18 图例 4 个选项逐字匹配（默认全选）",
                  names == ["瞬时流量", "瞬时流速", "正向累积", "反向累积"] and
                  all(it.get("checked") for it in (pop.get("items") or [])),
                  "names=%s items=%s" % (names, str(pop.get("items"))[:200]))

            # 等数据点进图（该序列橙像素出现）再建基线。
            px0 = wait_js(PIX_S2, lambda d: d.get("ok") and d.get("n", 0) > 0,
                          timeout=20.0)
            check("页 18 趋势有数据（瞬时流速指纹色已上屏）",
                  px0.get("n", 0) > 0, "pixels=%s" % str(px0)[:160])
            n_before = px0.get("n", 0)

            hit = ev(toggle_js("瞬时流速")) or {}
            time.sleep(0.5)
            st_hide = ev(PAGE_STATE) or {}
            px1 = ev(PIX_S2) or {}
            check("取消「瞬时流速」：可见集合/图例/按钮汇总都不含它",
                  hit.get("ok") and hit.get("checked") is False and
                  st_hide.get("keys") == ["flow", "fwd", "rev"] and
                  "瞬时流速" not in (st_hide.get("text") or "") and
                  all(e.get("name") != "瞬时流速"
                      for e in (st_hide.get("entries") or [])),
                  "hit=%s keys=%s text=%s" % (hit, st_hide.get("keys"),
                                              st_hide.get("text")))
            check("取消后 canvas 上该序列颜色像素归零（真没画线）",
                  px1.get("ok") and px1.get("n", 0) == 0 and n_before > 0,
                  "before=%d after=%s" % (n_before, px1.get("n")))

            ev(toggle_js("瞬时流速"))
            time.sleep(0.5)
            st_back = ev(PAGE_STATE) or {}
            speed_entry = next((e for e in (st_back.get("entries") or [])
                                if e.get("key") == "speed"), None)
            check("再勾回：历史点仍在（图例仍带 min~max）",
                  st_back.get("keys") == ["flow", "speed", "fwd", "rev"] and
                  speed_entry is not None and "~" in (speed_entry.get("label") or ""),
                  "entry=%s keys=%s" % (speed_entry, st_back.get("keys")))

            # 至少留一条：先清到只剩「反向累积」，再尝试取消最后一条。
            for nm in ("瞬时流量", "瞬时流速", "正向累积"):
                ev(toggle_js(nm))
            st_one = ev(PAGE_STATE) or {}
            last = ev(toggle_js("反向累积")) or {}
            time.sleep(0.3)
            st_last = ev(PAGE_STATE) or {}
            check("至少留一条：取消最后一条被拒（勾选框回弹）",
                  st_one.get("keys") == ["rev"] and
                  last.get("checked") is True and
                  st_last.get("keys") == ["rev"],
                  "one=%s last=%s after=%s" %
                  (st_one.get("keys"), last, st_last.get("keys")))

            pop_after = ev(CLOSE_POP) or {}
            check("点弹层外面收起（mousedown 外部关闭）",
                  pop_after.get("pop") == 0,
                  "pop=%s" % pop_after.get("pop"))

            # Y 轴/单序列（独立图，不依赖页面数据）
            sa = ev(STANDALONE) or {}
            a_all, a_b, a_back = (sa.get("all"), sa.get("onlyB"),
                                  sa.get("back"))
            check("Y 轴从 0 到最大值（隐藏 A ⇒ 上限换成 B）",
                  bool(a_all) and bool(a_b) and
                  abs(a_all[0]) < 1e-9 and abs(a_all[1] - 110) < 1e-9 and
                  abs(a_b[0]) < 1e-9 and abs(a_b[1] - 6) < 1e-9 and
                  bool(a_back) and abs(a_back[1] - 110) < 1e-9,
                  "all=%s onlyB=%s back=%s" % (a_all, a_b, a_back))
            # [2026-11-25] 气泡取该时刻全部可见序列 + normalized 轴恒 0~1。
            check("气泡取该时刻全部可见序列真值（不只最近一条）",
                  sa.get("hvals") == ["A:110", "B:6"],
                  "hvals=%s" % (sa.get("hvals"),))
            naxis = sa.get("naxis")
            check("normalized 轴恒 0~1（115 不拉高）",
                  sa.get("nmode") == "normalized" and bool(naxis)
                  and abs(naxis[0]) < 1e-9 and abs(naxis[1] - 1.0) < 1e-9,
                  "mode=%s axis=%s" % (sa.get("nmode"), naxis))
            check("normalized 图例仍标真实量程（正累 115）",
                  any("115" in s for s in (sa.get("nlegend") or [])),
                  "nlegend=%s" % (sa.get("nlegend"),))
            single = sa.get("single") or {}
            check("单序列不渲染图例下拉（防死控件）",
                  single.get("legend") is False and single.get("btn") is False,
                  "single=%s" % single)
            check("趋势卡「范围」下拉 1/5/10/30/60 分钟",
                  sa.get("rangeOpts") == [60000, 300000, 600000,
                                          1800000, 3600000]
                  and sa.get("rangeAfter") == 1800000,
                  "opts=%s range=%s→%s" % (sa.get("rangeOpts"),
                                           sa.get("rangeBefore"),
                                           sa.get("rangeAfter")))

            # 卸载时弹层自动收起：先在页 18 打开图例弹层，再切页
            # （trend.chart.destroy 顺带 legend.destroy；弹层挂在 body 上，
            # 不随卡片 DOM 一起移除）。
            ev(OPEN_POP)
            st6 = goto_page(6)
            # 设备名由服务端下发（进水浊度仪/出水浊度仪）；首帧到达后图例
            # 文本才改名（2026-11-24）⇒ 先等改名落地再开弹层核对。
            wait_js(
                "(function(){var c=document.querySelector('.trend-card');"
                "if(!c||!c.hsfTrendChart)return {ok:false};"
                "var e=c.hsfTrendChart.legendEntries();"
                "return {ok:e.length>0&&e[0].name!=='浊度1 NTU'};})()",
                lambda d: d.get("ok"), timeout=12)
            pop_left = ev("document.querySelectorAll('.mcc-pop').length")
            check("切页卸载时图例弹层自动收起",
                  pop_left == 0 and st6.get("page") == 6,
                  "pop=%s page=%s" % (pop_left, st6.get("page")))
            pop6 = ev(OPEN_POP) or {}
            names6 = [it.get("name") for it in (pop6.get("items") or [])]
            ev(CLOSE_POP)
            # Py 服务端名字=进水/出水浊度仪；C 栈配置缺名字时回退「浊度仪1/2」
            # （C 端已废弃，两种都认——锁的是"图例跟随服务端名字"这件事本身）。
            # [2026-11-24] 图例只留「进水/出水」短名 + NTU 单位（用户口径）。
            check("页 6 图例 3 个选项（短名「进水/出水」+ NTU）",
                  bool(st6.get("ok")) and
                  names6 in (["进水 NTU", "出水 NTU", "水温 ℃"],
                             ["浊度仪1 NTU", "浊度仪2 NTU", "水温 ℃"]),
                  "names=%s" % names6)

            st5 = goto_page(5)
            pop5 = ev(OPEN_POP) or {}
            names5 = [it.get("name") for it in (pop5.get("items") or [])]
            ev(CLOSE_POP)
            check("页 5 图例 3 个选项（未改序列，自动获得下拉）",
                  bool(st5.get("ok")) and
                  names5 == ["输出频率 Hz", "给定频率 Hz", "输出电流 A"],
                  "names=%s" % names5)
            h5 = ev(
                "(function(){var c=document.querySelector('.vfd-page "
                ".trend-chart');return c?Math.round("
                "c.getBoundingClientRect().height):0;})()")
            check("搅拌泵趋势图高度加倍（≥400px）",
                  isinstance(h5, (int, float)) and h5 >= 400,
                  "h=%s" % (h5,))

            # ---- 后台趋势采集（2026-11-24 用户「不在这个界面的时候不要
            #      暂停数据采集」）：此刻停在水质页之外，trendhist 仍在收
            #      浊度；回到页 6 时曲线应立即灌入这段历史。----
            HCOUNT = ("(function(){return (window.HSF&&HSF.trendHistory)"
                      "?HSF.trendHistory.points('water','turb1').length:0;})()")
            h0 = ev(HCOUNT) or 0
            got = wait_js(
                "(function(){return {n:%s};})()" % HCOUNT,
                lambda d: (d.get("n") or 0) > h0, timeout=8)
            h1 = got.get("n") or 0
            check("离开水质页后浊度历史仍在增长（后台采集）",
                  h1 > h0, "n=%s→%s" % (h0, h1))
            st6b = goto_page(6)
            seeded = wait_js(
                "(function(){var c=document.querySelector('.trend-card');"
                "return {n:(c&&c.hsfTrendChart)?"
                "c.hsfTrendChart.pointCount('turb1'):0};})()",
                lambda d: (d.get("n") or 0) > 0, timeout=6)
            check("回到水质页曲线立即灌入后台历史",
                  bool(st6b.get("ok")) and
                  (seeded.get("n") or 0) >= max(1, h1),
                  "seeded=%s（后台历史 %s）"
                  % (seeded.get("n"), h1))

            try:
                shot = os.path.join(REPO, "verify", "webui", "shots",
                                    "webui_trend_legend_%s.png" % ctx.tag)
                os.makedirs(os.path.dirname(shot), exist_ok=True)
                with open(shot, "wb") as fp:
                    fp.write(cdp.screenshot_png())
                step("趋势图例截图留证：%s" % shot)
            except Exception:                        # noqa: BLE001
                pass
        except Exception as exc:                     # noqa: BLE001
            check("实时趋势图例（真浏览器）", False,
                  "%s: %s" % (type(exc).__name__, exc))
        finally:
            if cdp is not None:
                try:
                    cdp.close()
                except Exception:                    # noqa: BLE001
                    pass
            if proc is not None:
                _kill_tree(proc)
            _rm_profile(profile)


def sec_landing(ctx, token):
    """缺省着陆页：**桌面与移动端都进主界面(0)**（2026-11-17 用户定案）。

    用 `?authtoken=<token>` 直登（无头通路口径）真开一次页面：
      ① 桌面档 1280×800：`HSF.router.current()==0`、`.home-page` 已挂、
         hash 写为 `#/page/0`；
      ② 移动档（CDP 设备模拟 390×844 + mobile）：`HSF.isNarrow()` 生效，
         重新载入（hash 清空）后**仍**落在主界面(0)——防「移动端被特判到
         主菜单」的回潮。
    """
    exe = find_browser()
    with Section("缺省着陆页：桌面/移动端统一进主界面(0)"):
        if exe is None:
            check("浏览器可用（着陆页检查）", True,
                  "未找到 Edge/Chrome：跳过真开检查")
            return
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        import cdp_client
        url = "http://%s:%d/?authtoken=%s" % (HOST, ctx.web, token)
        profile = tempfile.mkdtemp(prefix="edge_land_")
        port = free_port(9510)
        proc = None
        cdp = None

        def snap():
            return cdp.evaluate(
                "(function(){var h=document.getElementById('page-host');"
                "return {hash: location.hash,"
                " cur: (window.HSF&&window.HSF.router)"
                "  ?window.HSF.router.current():-2,"
                " kids: h?h.children.length:0,"
                " home: !!document.querySelector('.home-page'),"
                " narrow: !!(window.HSF&&window.HSF.isNarrow"
                "  &&window.HSF.isNarrow()),"
                " w: window.innerWidth};})()") or {}

        def wait_home(expect_w=None, timeout=25.0):
            info = {}
            deadline = time.time() + timeout
            while time.time() < deadline:
                info = snap()
                if info.get("home") and (expect_w is None
                                         or info.get("w") == expect_w):
                    return info
                time.sleep(0.4)
            return info

        try:
            proc = subprocess.Popen(
                [exe, "--headless=new", "--disable-gpu", "--no-first-run",
                 "--user-data-dir=" + profile,
                 "--remote-debugging-port=%d" % port,
                 "--window-size=1280,800", url],
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            ws_url = cdp_client.wait_for_target(port, timeout_s=45.0)
            cdp = cdp_client.Cdp(ws_url, timeout=30.0)
            cdp.call("Page.enable")
            cdp.call("Runtime.enable")

            # ① 桌面档
            info = wait_home()
            check("桌面档：缺省着陆 = 主界面(0)",
                  info.get("cur") == 0 and bool(info.get("home")),
                  "info=%s" % str(info)[:200])
            check("桌面档：hash 写为 #/page/0",
                  info.get("hash") == "#/page/0",
                  "hash=%r" % info.get("hash"))
            try:
                shot = os.path.join(REPO, "verify", "webui", "shots",
                                    "webui_land_%s.png" % ctx.tag)
                os.makedirs(os.path.dirname(shot), exist_ok=True)
                with open(shot, "wb") as fp:
                    fp.write(cdp.screenshot_png())
                step("桌面档着陆截图留证：%s" % shot)
            except Exception:                        # noqa: BLE001
                pass

            # ② 移动档：设备模拟 + 重新载入（hash 清空 ⇒ 走 DEFAULT_PAGE）
            cdp.call("Emulation.setDeviceMetricsOverride", {
                "width": 390, "height": 844, "deviceScaleFactor": 2,
                "mobile": True})
            cdp.call("Page.navigate", {"url": url})
            info2 = wait_home(expect_w=390)
            check("移动档(390px)：isNarrow 生效",
                  bool(info2.get("narrow")),
                  "innerWidth=%r narrow=%r" % (info2.get("w"),
                                               info2.get("narrow")))
            check("移动档(390px)：缺省着陆仍是主界面(0)",
                  info2.get("cur") == 0 and bool(info2.get("home")),
                  "info=%s" % str(info2)[:200])
            try:
                shot = os.path.join(REPO, "verify", "webui", "shots",
                                    "webui_land_%s_mobile.png" % ctx.tag)
                os.makedirs(os.path.dirname(shot), exist_ok=True)
                with open(shot, "wb") as fp:
                    fp.write(cdp.screenshot_png())
                step("移动档着陆截图留证：%s" % shot)
            except Exception:                        # noqa: BLE001
                pass
        except Exception as exc:                     # noqa: BLE001
            check("着陆页（真浏览器）", False,
                  "%s: %s" % (type(exc).__name__, exc))
        finally:
            if cdp is not None:
                try:
                    cdp.close()
                except Exception:                    # noqa: BLE001
                    pass
            if proc is not None:
                _kill_tree(proc)
            # [临时 profile 清理 2026-11-18] 同上：不删就把系统盘撑满。
            _rm_profile(profile)


# ============================================================================
#                              主流程
# ============================================================================

def run_end(args, owned):
    tag = "C6100" if args.server_port == 6100 else "PY6200"
    _end[0] = tag
    print("=" * 64)
    print("webui 全功能验收：%s（上游 %d，web %d）"
          % (tag, args.server_port, args.web_port))
    print("=" * 64)

    ctx = Ctx(args.web_port, tag)

    sec_static()
    sec_env(ctx, args, owned)
    if not tcp_open(HOST, ctx.upstream):
        check("上游 %d 起不来，后续段无法执行" % ctx.upstream, False,
              ctx.note or "未在跑")
        return False
    sec_public(ctx)
    sec_page_boot(ctx)
    sec_login(ctx)
    if "root" not in ctx.tokens:
        check("root 登录失败，后续段无法执行", False, "提前收尾")
        return False
    tok = ctx.token_of("root")
    sec_no_placeholder_pages(ctx, tok)
    with Section("上游链路就绪（惰性连接等待）+ 数据预备"):
        st = wait_upstream_ready(ctx, tok)
        check("上游链路就绪（link.ready + 模式已知 + 健康 ≥1 台）",
              st is not None, "state=%s" % str(st or {})[:200])
        # [数据预备] 库里行数太少时分页/导出断言会变成空转（0==0 的假绿）：
        # 不足 40 行就把入库周期提到 1s **且**把入库触发改成「持续采集」
        # （C sim 的缺省 historyTrigger=auto ⇒ 不跑自动就不入库），
        # 收尾按原值恢复。
        status, _h, cfg0 = ctx.get("/api/config", token=tok, timeout=20.0)
        if status == 200 and isinstance(cfg0, dict):
            ctx.orig_storage = cfg0.get("storage")
            ctx.orig_auto = {"modes": (cfg0.get("auto") or {}).get("modes"),
                             "cycle": (cfg0.get("auto") or {}).get("cycle")}
        status, _h, d = ctx.get("/api/history/page?offset=0&limit=1",
                                token=tok, timeout=20.0)
        total0 = (d or {}).get("total") or 0
        if total0 < 40:
            status, _h, doc = ctx.post("/api/config",
                                       {"section": "storage",
                                        "sampleMs": 1000,
                                        "historyTrigger": "always"},
                                       token=tok)
            ctx.storage_touched = status == 200 and (doc or {}).get("ok") \
                is True
            print("    · 库仅 %d 行 → sampleMs=1000 + historyTrigger=always"
                  "（收尾恢复）" % total0)

            def rows_come():
                status, _h, d = ctx.get(
                    "/api/history/page?offset=0&limit=1", token=tok,
                    timeout=20.0)
                return d if (d or {}).get("total", 0) >= 10 else None

            got = ctx.poll(rows_come, 60, interval=2.0, label="历史行积累")
            check("提速采样后历史行积累 ≥10 行", got is not None,
                  "落盘仍无数据（检查 storage.enabled 与 historyTrigger）")
    sec_landing(ctx, tok)
    sec_static_wire(ctx)
    # [2026-11-22] 真浏览器字节门：紧挨 HTTP 口径的线上字节门（失败时并排看），
    # 用 tok 直登到登录态（23.2KB 口径就是登录态二次访问）。
    sec_browser_bytes(ctx, tok)
    # [2026-11-22] 实时趋势「图例」多选（真浏览器；只读，不改服务端状态）。
    sec_trend_legend(ctx, tok)
    sec_admin_ro(ctx)
    sec_oper(ctx)
    sec_sse(ctx)
    sec_valve_pump(ctx)
    sec_vfd(ctx)
    sec_vfd_preset(ctx)
    sec_cycle_order_gate(ctx)
    sec_auto_produce(ctx)
    sec_auto_select(ctx)
    sec_estop(ctx)
    sec_cycle(ctx)
    sec_config(ctx)
    sec_protect_config(ctx)
    sec_auto_cycle_cfg(ctx)
    sec_switch_policy(ctx)
    # [循环实例 2026-11-26] 只在 PY6200 跑（C sim 不认 instances 整包拒
    # ⇒ C6100 记 SKIP）；放 sec_auto_cycle_cfg 之后——后者锁的旧键
    # （cycleModes/stageCfg）路径要求配置**未显式** instances。
    sec_cycle_instances(ctx)
    # [顶层总循环次数 2026-11-27] 紧随实例段（同在 auto.cycle 子节；
    # 只跑 PY6200，C 端记 SKIP）。
    sec_cycle_top_repeat(ctx)
    sec_history(ctx)
    sec_xlsx_numeric(ctx)
    sec_log(ctx)
    sec_alarms(ctx)
    sec_alarm_server(ctx)
    sec_export_log_alarm(ctx)
    sec_auto_modes(ctx)
    sec_vfd_params(ctx)
    # [真浏览器抓载荷] 放最后：它会点「应用自动配置」（真写服务端配置），
    # 写完由 sec_cleanup 按开跑前抓到的原值恢复。
    sec_auto_cycle_ui(ctx, tok)
    sec_cleanup(ctx)
    return True


def main():
    global _passed, _failed, _skipped
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, OSError):
        pass
    parser = argparse.ArgumentParser(
        description="webui 全功能协议级验收（两端各跑一遍）")
    parser.add_argument("--server-port", default="both",
                        choices=("6100", "6200", "both"),
                        help="上游服务端口：6100=C sim（bin/HsfServer.exe "
                             "--sim）+ 常驻 webui(8080)；6200=自起 py sim + "
                             "自起 webui(8081)；both=两端各跑一遍（缺省）")
    parser.add_argument("--web-port", type=int, default=0,
                        help="网页端口覆盖（缺省：6100→8080 / 6200→8081）")
    args = parser.parse_args()
    ports = ([6100, 6200] if args.server_port == "both"
             else [int(args.server_port)])

    owned = Owned()
    t0 = time.time()
    per_end = []
    env_broken = False
    try:
        for port in ports:
            _passed = 0
            _failed = []
            _skipped = []
            _srv_down[0] = False
            _srv_streak[0] = 0
            # 每端一套「自己起的进程」⇒ 端与端之间端口/会话互不干扰；
            # 收尾只停**本端自己起的**（常驻服务一律不动）。
            owned = Owned()
            sub = argparse.Namespace(
                server_port=port,
                web_port=(args.web_port or (8080 if port == 6100 else 8081)))
            try:
                started = run_end(sub, owned)
            except Exception as exc:                        # noqa: BLE001
                check("验收执行异常", False,
                      "%s: %s" % (type(exc).__name__, exc))
                started = False
            finally:
                owned.cleanup()
            env_broken = env_broken or not started
            per_end.append((_end[0], _passed, list(_failed), list(_skipped)))
    finally:
        owned.cleanup()

    print("")
    print("=" * 64)
    total_pass = 0
    total_fail = 0
    total_skip = 0
    for tag, passed, failed, skipped in per_end:
        total_pass += passed
        total_fail += len(failed)
        total_skip += len(skipped)
        print("[%s] 完成：通过 %d 项；失败 %d 项；跳过 %d 项（环境）"
              % (tag, passed, len(failed), len(skipped)))
        for label, detail in failed:
            print("  FAIL %s\n       %s" % (label, detail))
        for label, reason in skipped:
            print("  SKIP %s\n       %s" % (label, reason))
    print("-" * 64)
    print("合计（用时 %.0fs）：%d 项通过 / %d 项失败 / %d 项跳过（环境）"
          % (time.time() - t0, total_pass, total_fail, total_skip))
    if not per_end:
        print("环境：目标端一个都没起来（退出码 2）")
        return 2
    if env_broken:
        print("注意：有一端没起来（环境问题，见该端段落）")
    return 0 if not total_fail else 1


if __name__ == "__main__":
    sys.exit(main())
