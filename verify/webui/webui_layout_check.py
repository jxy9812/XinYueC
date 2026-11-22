# -*- coding: utf-8 -*-
"""网页端**布局/排版**自动化几何检查（纯标准库；Edge headless + CDP）。

用法
====

    python verify/webui/webui_layout_check.py                 # 自动解析端口（缺省口径）
    python verify/webui/webui_layout_check.py --own           # 强制自起 C sim + hsfweb
    python verify/webui/webui_layout_check.py --viewport 390x844 --pages 9,10,11
    python verify/webui/webui_layout_check.py                 # 触摸档默认把截图存 out/webui_shots
    python verify/webui/webui_layout_check.py --shots ""      # 关闭截图

[为什么要有这个脚本]
网页端此前**完全没有运行时几何断言**：`webui_check.py` 只有 4 条「源码形状」
静态锁（列宽函数必须存在），真正「历史表叠列」是靠人工在浏览器里量
`td.scrollWidth - td.clientWidth` 发现的；移动端只有 2 条着陆页断言。
本脚本把「移动端显示正常 / 没有文字被裁剪遮挡」变成**可重复的门禁**。

[覆盖的断言]
    A 页面级横向溢出   · 文档不横向滚动；内容区（#page-host）不横向滚动；
                        顶层盒不超出视口；滚动容器自身不超出视口
    B 元素级文字硬裁   · 承载文字的元素 scrollWidth > clientWidth 且**不是**省略号设计
    C 表格列不叠列     · 逐单元格内容不溢出到隔壁列 + 逐列几何不重叠（页 9/10/11 重点）
    D 抽屉导航         · ≤860px：☰ 可见 → 点开抽屉 → 点导航项自动收起且 hash 切换
                       · 861~1023 夹缝档：导航**不是**抽屉形态（position != fixed）
    E 竖屏内容不丢     · body 高度非 0；内容块数量与桌面档一致；.card 数量不塌

[已修的两条首跑红 · 2026-11-20 实测 → 2026-11-21 修]
    ① 页 10（日志）窄屏（≤480px）：`table-layout:fixed` 的「时间 21rem +
       类别 16.8571rem」两列已占满容器宽，**未指定宽度的「事件」列被压成
       0px** ⇒ 手机上日志正文整列不可见（截图：表格只有时间列）。
    ② 页 14（远端设置）「数据存储」卡：`.usage-label` 写的是**库文件绝对
       路径**，CSS 是 `white-space:pre-line` 且路径无换行点 ⇒ 路径超过约
       45 个字符就顶出卡片与视口（内容区被迫横向滚动）。
    ⇒ 两条都已修（详见 docs 三端闭环 §2.3），本脚本现在应**全绿**；
       再红就是新缺陷，不是历史债。

[视口档位]
    360x780 / 375x667（手机竖）/ 390x844 / 480x900 / 667x375（手机横）
    / 768x1024（平板：抽屉档 + 两列网格）/ 860x900（抽屉边界）
    / 900x900（861~1023 夹缝档：抽屉刚失效）/ 1280x800（桌面，**先跑**做内容基准）
    视口用 `Emulation.setDeviceMetricsOverride` 设（**不用** `--window-size`：
    headless 下窗口内高会少一截 ⇒ 断言 innerWidth 必须等于设定值）。
    触摸档（≤860）默认把截图写到 out/webui_shots（已 gitignore）。

[F 触达目标]（2026-11-22 补）
    ≤860 档逐页断言**可点元素有效命中区 ≥44px**：命中区 = 元素自身 rect 与
    「包裹它的 label」rect 的较大者（label.control === 元素 才算）。触摸下
    小方框很难点中，这条把「44px 触达」变成机械门（此前只有 CSS 约定）。

[端口纪律]（照抄 webui_check.py 的口径，不抢别人的端口）
    网页端口：`HSF_WEB_PORT` 环境变量 → 在跑的 hsfweb（按进程命令行认）
              → 候选端口探活 `/api/ping`（认 server=hsf-webui）
              → 都不行才**自起** `python -m hsfweb --web-port <空闲端口>`。
    上游端口：优先复用在跑的 C sim（`bin/HsfServer.exe --sim`）或 py sim(6200)，
              都没有就**自起**一个 C sim（**临时程序根** ⇒ 不碰共享配置/共享库）。
    **收尾只杀自己起的进程**，常驻服务一律原样保留。

[依赖] 纯标准库 + 同目录 `cdp_client.py`（极简 CDP 客户端，自带 WebSocket）。
        浏览器：Edge 或 Chrome（找不到 ⇒ 退出码 2，不假装通过）。

退出码：0=全过；1=有失败；2=环境起不来（没浏览器 / 没服务端）。
"""

import argparse
import http.client
import json
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import time
import urllib.parse

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", ".."))
WEBUI_DIR = os.path.join(REPO, "HighSpeedFilter", "webui_py")
BIN_DIR = os.path.join(REPO, "bin")
SIM_EXE = os.path.join(BIN_DIR, "HsfServer.exe")

HOST = "127.0.0.1"

#: 网页端页数（后端 api.py 的 PAGE_COUNT）。页 16 是**保留槽位**（桌面端的
#: 「Web 设置」页，网页端不实现）⇒ 跳过；页 17 是相机页 ⇒ **要覆盖**。
PAGE_COUNT = 18
RESERVED_PAGES = (16,)

#: 视口档位：(宽, 高, 标签, 是否手机形态)。
#: [为什么加 900x900] 题面要求覆盖「861~1023 夹缝档」——抽屉（≤860px）
#: 刚失效、窄屏媒体查询（≤1023px 的两列网格）还没生效的那一段，
#: 是抽屉导航最容易漏改的区间。
VIEWPORTS = [
    (1280, 800, "桌面 1280x800", False),
    (900, 900, "夹缝 900x900（861~1023）", False),
    (860, 900, "抽屉边界 860x900（≤860）", True),
    (480, 900, "窄手机 480x900", True),
    (390, 844, "手机 390x844", True),
    (360, 780, "小手机 360x780", True),
    # [2026-11-22 补档] 覆盖用户明确点名的三种设备形态：375 竖屏（极窄
    # ≤520 兜底档）、667 横屏（≤700 窄屏档，此前从未测过横屏）、768 平板
    # （≤860 抽屉档但 >700 两列网格）。宽都 ≤1023，不影响排序与基准。
    (375, 667, "手机竖 375x667", True),
    (667, 375, "手机横 667x375", True),
    (768, 1024, "平板 768x1024", True),
]

#: 抽屉档上限（与 util.js 的 HSF.DRAWER_MAX_W / theme.css 媒体查询同值）。
DRAWER_MAX_W = 860
#: 夹缝档上限（theme.css 的 ≤1023px 网格退化段）。
NARROW_GRID_MAX_W = 1023

#: Edge/Chrome 候选路径。
EDGE_CANDIDATES = [
    r"C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe",
    r"C:/Program Files/Microsoft/Edge/Application/msedge.exe",
    r"C:/Program Files/Google/Chrome/Application/chrome.exe",
    r"C:/Program Files (x86)/Google/Chrome/Application/chrome.exe",
]

#: 网页端口候选（探活 /api/ping）。
WEB_PORT_CANDIDATES = (8080, 8085, 8081, 8086, 8087, 8090)

sys.path.insert(0, HERE)
import cdp_client                                          # noqa: E402


# ============================================================================
#                          小工具（HTTP / 端口 / 进程）
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
        return resp.status, resp.read()
    finally:
        conn.close()


def http_json(port, method, path, body=None, token=None, timeout=15.0):
    try:
        status, raw = http_req(port, method, path, body, token, timeout)
    except (OSError, http.client.HTTPException):
        return 0, None
    try:
        return status, json.loads(raw.decode("utf-8"))
    except (ValueError, UnicodeDecodeError):
        return status, None


def tcp_open(host, port, timeout=1.5):
    try:
        with socket.create_connection((host, port), timeout=timeout):
            return True
    except OSError:
        return False


def port_busy(port):
    """端口上有没有服务在听（**连接**判定，不用 bind）。

    [为什么不用 bind] Windows 上 127.0.0.1:8080 与 0.0.0.0:8080 可以共存
    （后绑定者截走流量）⇒ bind 成功不代表端口空闲 ⇒ 抢端口＝打断别的阶段
    的服务（本机 8080 上跑着别人的常驻程序）。
    """
    return tcp_open(HOST, port, timeout=0.5)


def free_port(start):
    """找一个空闲端口（**连接**判定；避开常用段）。"""
    port = start
    while port < start + 320:
        if not port_busy(port):
            return port
        port += 1
    raise RuntimeError("找不到空闲端口（从 %d 起扫了 320 个）" % start)


def wait_tcp(port, timeout):
    deadline = time.time() + timeout
    while time.time() < deadline:
        if tcp_open(HOST, port, 0.8):
            return True
        time.sleep(0.4)
    return False


def _powershell_procs(pattern):
    """列出命令行匹配 pattern（正则）的进程：[(pid, name, cmdline)]。"""
    script = (
        "[Console]::OutputEncoding=[System.Text.Encoding]::UTF8; "
        "Get-CimInstance Win32_Process | "
        "Where-Object { $_.CommandLine -and $_.CommandLine -match '%s' } | "
        "ForEach-Object { $_.ProcessId.ToString() + '|' + $_.Name + '|' + "
        "$_.CommandLine }"
    ) % pattern
    try:
        out = subprocess.run(["powershell", "-NoProfile", "-Command", script],
                             capture_output=True).stdout or b""
    except OSError:
        return []
    rows = []
    for line in out.decode("utf-8", "replace").splitlines():
        line = line.strip()
        if line.count("|") < 2:
            continue
        pid, _, rest = line.partition("|")
        name, _, cmd = rest.partition("|")
        if pid.strip().isdigit():
            rows.append((int(pid.strip()), name.strip(), cmd.strip()))
    return rows


def _is_shell_wrapper(cmd):
    """命令行是不是 shell/包装进程（bash -c / cmd / powershell / wmic）。

    [为什么要排除] 这些进程的命令行里**含有**我们要找的字符串（历史命令
    整条都在里面）⇒ 不排除就会把「上一阶段起过的进程」误认成在跑的实例。
    """
    low = cmd.lower()
    return any(k in low for k in ("bash.exe", "bash -c", "cmd.exe",
                                  "powershell", "snapshot-bash", "wmic"))


def running_hsfweb():
    """在跑的 hsfweb 进程：[(pid, cmdline)]。"""
    out = []
    for pid, name, cmd in _powershell_procs(r"-m\s+hsfweb"):
        if not name.lower().startswith("python"):
            continue
        if _is_shell_wrapper(cmd):
            continue
        out.append((pid, cmd))
    return out


def arg_value(cmd, name, default=None):
    """从命令行里取 `--name value` 或 `--name=value`。"""
    parts = cmd.split()
    for i, part in enumerate(parts):
        if part == name and i + 1 < len(parts):
            return parts[i + 1]
        if part.startswith(name + "="):
            return part.split("=", 1)[1]
    return default


def running_c_sim():
    """找**在跑的** C 版 sim（bin/HsfServer.exe --sim）⇒ (端口, 说明)。"""
    for pid, name, cmd in _powershell_procs(r"HsfServer\.exe"):
        low = cmd.lower()
        if "hsfserver.exe" not in low or "--sim" not in low:
            continue
        if _is_shell_wrapper(cmd):
            continue
        port = _listening_port_of_pid(pid)
        if port:
            return port, "pid=%d %s" % (pid, cmd[:100])
    return None, ""


def _listening_port_of_pid(pid):
    """这个 pid 在监听哪个 TCP 端口（None = 不是监听者 / 查不到）。"""
    if os.name == "nt":
        try:
            out = subprocess.run(["netstat", "-ano", "-p", "TCP"],
                                 capture_output=True).stdout or b""
        except OSError:
            return None
        for line in out.decode("utf-8", "replace").splitlines():
            parts = line.split()
            if len(parts) >= 5 and parts[0] == "TCP" \
                    and parts[3] == "LISTENING" and parts[4] == str(pid):
                try:
                    return int(parts[1].rsplit(":", 1)[-1])
                except ValueError:
                    return None
        return None
    try:
        out = subprocess.run(["ss", "-ltnp"], capture_output=True).stdout or b""
    except OSError:
        return None
    for line in out.decode("utf-8", "replace").splitlines():
        if ("pid=%d," % pid) in line or ("pid=%d)" % pid) in line:
            for token in line.split():
                if ":" in token and token.split(":")[-1].isdigit():
                    return int(token.split(":")[-1])
    return None


def hsfweb_ping(port, timeout=2.5):
    """这个端口上是不是 hsfweb（探 /api/ping 的 server 标记）。

    [为什么不能只看 HTTP 200] 端口被别的应用占着时它也可能回 200（只是
    不是 hsfweb 的应答）⇒ 认服务器要看 /api/ping 的 server 字段。
    """
    status, doc = http_json(port, "GET", "/api/ping", timeout=timeout)
    return bool(status == 200 and isinstance(doc, dict)
                and doc.get("ok") and doc.get("server") == "hsf-webui")


def web_login(port, user, password, attempts=6):
    """登录拿 token（瞬时拒绝按 8 秒间隔重试）。

    [为什么重试] 服务端有「同账并发 DenyNew」策略：上一轮的会话还没被
    静默回收时，第二次同账号登录会被瞬时拒（口令没错也拒）⇒ 重试即可。
    """
    last = ""
    for i in range(attempts):
        status, doc = http_json(port, "POST", "/api/login",
                                {"user": user, "password": password})
        if status == 200 and isinstance(doc, dict) and doc.get("token"):
            return doc["token"]
        last = "status=%s err=%s" % (status, (doc or {}).get("err")
                                     if isinstance(doc, dict) else doc)
        if i + 1 < attempts:
            time.sleep(8.0)
    raise RuntimeError("网页登录失败：%s" % last)


# ============================================================================
#                          进程自管（**只清理自己起的**）
# ============================================================================

class Owned(object):
    """本脚本自己起的进程 + 临时目录（收尾统一清理）。"""

    def __init__(self):
        self.procs = []              # [(label, proc, [out, err])]
        self.dirs = []               # 临时目录
        self.edge_profiles = []      # Edge 临时 profile

    def add(self, label, proc, outs):
        self.procs.append((label, proc, outs))

    def tail(self, prefix, limit=25):
        """取某个自己起的进程（标签前缀）的 stderr 尾巴（诊断用）。"""
        for lab, _proc, outs in self.procs:
            if lab.startswith(prefix) and len(outs) > 1:
                return _dump_tail(outs[1], limit)
        return ""

    def cleanup(self):
        for label, proc, outs in self.procs:
            if proc.poll() is not None:
                continue
            if os.name == "nt":
                subprocess.run(["taskkill", "/F", "/T", "/PID", str(proc.pid)],
                               capture_output=True,
                               env=dict(os.environ, MSYS_NO_PATHCONV="1"))
            else:
                proc.terminate()
            try:
                proc.wait(timeout=10)
            except Exception:                        # noqa: BLE001
                pass
            print("  · 收尾：已停 %s（pid=%d）" % (label, proc.pid))
        self.procs = []
        for profile in self.edge_profiles:
            _kill_edge_by_profile(profile)
            for _ in range(6):
                shutil.rmtree(profile, ignore_errors=True)
                if not os.path.isdir(profile):
                    break
                time.sleep(0.4)
        self.edge_profiles = []
        for path in self.dirs:
            shutil.rmtree(path, ignore_errors=True)
        self.dirs = []


def _spawn(label, argv, cwd):
    out = tempfile.TemporaryFile()
    err = tempfile.TemporaryFile()
    proc = subprocess.Popen(argv, cwd=cwd, stdout=out, stderr=err)
    print("  · 起 %s：pid=%d %s" % (label, proc.pid, " ".join(argv[:5])))
    return proc, out, err


def _dump_tail(handle, limit=30):
    try:
        handle.seek(0)
        data = handle.read()[-3000:]
        return "\n".join(data.decode("utf-8", "replace").splitlines()[-limit:])
    except Exception as exc:                          # noqa: BLE001
        return "(读输出失败：%s)" % exc


def _kill_edge_by_profile(profile):
    """杀占用这个临时 profile 的 Edge 进程（headless 是分离出去的子进程）。"""
    script = (
        "[Console]::OutputEncoding=[System.Text.Encoding]::UTF8; "
        "Get-CimInstance Win32_Process | "
        "Where-Object { $_.Name -eq 'msedge.exe' -and $_.CommandLine -and "
        "$_.CommandLine.Contains('%s') } | "
        "ForEach-Object { $_.ProcessId.ToString() }"
    ) % profile
    try:
        out = subprocess.run(["powershell", "-NoProfile", "-Command", script],
                             capture_output=True).stdout or b""
    except OSError:
        return
    for line in out.decode("utf-8", "replace").splitlines():
        line = line.strip()
        if line.isdigit():
            subprocess.run(["taskkill", "/F", "/T", "/PID", line],
                           capture_output=True,
                           env=dict(os.environ, MSYS_NO_PATHCONV="1"))


# ============================================================================
#                          环境解析（上游 + 网页端口）
# ============================================================================

def start_c_sim(port, owned):
    """自起一个 C 端 sim：exe + 配置**复制到临时程序根**再跑。

    [为什么复制程序根] C 服务端的配置/账户库/历史库都落在 <程序根>/cfg/；
    直接在 bin/ 起会与常驻 C sim 抢同一个 SQLite 文件（C 端对库文件是
    **独占**锁 ⇒ 撞上就是「打开库失败 ⇒ 落盘关闭 ⇒ 历史/日志页全空」）。
    复制到临时目录 ⇒ 不碰共享配置与共享数据文件，收尾直接删目录。
    """
    tmp = tempfile.mkdtemp(prefix="weblayout_csim_%d_" % port)
    owned.dirs.append(tmp)
    os.makedirs(os.path.join(tmp, "cfg"), exist_ok=True)
    shutil.copy2(SIM_EXE, os.path.join(tmp, "HsfServer.exe"))
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
    for name in ("hsf_users.json", "hsf_login_policy.json"):
        src = os.path.join(BIN_DIR, "cfg", name)
        if os.path.isfile(src):
            shutil.copy2(src, os.path.join(tmp, "cfg", name))
    proc, out_h, err_h = _spawn("c-sim(%d)" % port,
                                 [os.path.join(tmp, "HsfServer.exe"),
                                  str(port), "--sim"], tmp)
    owned.add("c-sim(%d)" % port, proc, [out_h, err_h])
    if not wait_tcp(port, 40.0):
        raise RuntimeError("C sim 起不来（TCP %d）：%s"
                           % (port, _dump_tail(err_h)))
    return port


def resolve_upstream(args, owned):
    """决定上游（XLink 服务端）端口。

    优先级：显式 --upstream → 在跑的 C sim → 在跑的 py sim(6200) → 自起 C sim。
    """
    if args.upstream:
        port = int(args.upstream)
        if not tcp_open(HOST, port):
            raise RuntimeError("指定的上游 %d 没人在听" % port)
        print("  · 上游：按 --upstream 用 %d" % port)
        return port
    port, why = running_c_sim()
    if port:
        print("  · 上游：复用在跑的 C sim %d（%s）" % (port, why))
        return port
    if tcp_open(HOST, 6200):
        print("  · 上游：复用在跑的 py sim 6200")
        return 6200
    port = free_port(6100)
    print("  · 上游：自起 C sim（临时程序根）于 %d" % port)
    start_c_sim(port, owned)
    return port


def resolve_web_port(args, upstream, owned, force_own=False):
    """决定网页端口（**不抢**别人的端口）。

    顺序：`--web-port` → `HSF_WEB_PORT` 环境变量 → 在跑的 hsfweb（按进程
    命令行认，优先指向本上游的）→ 候选端口探活 /api/ping → 自起一个。

    [force_own] `--own` 模式：**不复用**任何常驻 hsfweb，一律自起。
    [为什么] 复用别人的实例有两个风险：① 它的上游可能与我们自起的 C sim
    不是同一个 ⇒ 数据页（历史/日志/报警）取的是别的装置的数据，叠列判据
    变成空转；② 它的会话/登录策略是别人的（并发上限、限流），我们的登录
    可能被瞬时拒。⇒ 自起一套才是可重复的门禁。
    """
    if args.web_port:
        port = int(args.web_port)
        if not hsfweb_ping(port):
            raise RuntimeError("指定 --web-port %d 上不是 hsfweb" % port)
        print("  · 网页：按 --web-port 用 %d" % port)
        return port

    if force_own:
        port = free_port(8080)
        print("  · 网页：--own ⇒ 自起 hsfweb 于 %d（上游 %d）" % (port, upstream))
        return _spawn_webui(port, upstream, owned)

    env_port = os.environ.get("HSF_WEB_PORT")
    if env_port:
        try:
            port = int(env_port)
        except ValueError:
            port = 0
        if port and hsfweb_ping(port):
            print("  · 网页：按 HSF_WEB_PORT 用 %d" % port)
            return port
        if port:
            print("  · 网页：HSF_WEB_PORT=%d 上不是 hsfweb（忽略）" % port)

    # 在跑的 hsfweb：优先「上游就是本上游」的那台（数据一致）。
    same, other = None, None
    for pid, cmd in running_hsfweb():
        web = arg_value(cmd, "--web-port")
        up = arg_value(cmd, "--port", "6100")
        try:
            web = int(web)
        except (TypeError, ValueError):
            continue
        if not hsfweb_ping(web):
            continue
        if str(up) == str(upstream):
            same = (web, cmd)
            break
        if other is None:
            other = (web, cmd)
    for cand in (same, other):
        if cand:
            print("  · 网页：复用在跑的 hsfweb %d（%s）" % (cand[0], cand[1][:80]))
            return cand[0]

    for port in WEB_PORT_CANDIDATES:
        if hsfweb_ping(port):
            print("  · 网页：候选端口 %d 上就是 hsfweb ⇒ 复用" % port)
            return port

    port = free_port(8080)
    print("  · 网页：自起 hsfweb 于 %d（上游 %d）" % (port, upstream))
    return _spawn_webui(port, upstream, owned)


def _spawn_webui(port, upstream, owned):
    """自起一个 hsfweb（等 /api/ping 认出 server=hsf-webui 才算起来）。"""
    argv = [sys.executable, "-m", "hsfweb", "--server", HOST,
            "--port", str(upstream), "--bind", HOST, "--web-port", str(port)]
    proc, out_h, err_h = _spawn("webui(%d→%d)" % (port, upstream),
                                 argv, WEBUI_DIR)
    owned.add("webui(%d)" % port, proc, [out_h, err_h])
    deadline = time.time() + 30.0
    while time.time() < deadline:
        if hsfweb_ping(port):
            return port
        time.sleep(0.4)
    raise RuntimeError("hsfweb 起不来（%d）：%s" % (port, _dump_tail(err_h)))


# ============================================================================
#                          浏览器（Edge headless + CDP）
# ============================================================================

def find_edge():
    for path in EDGE_CANDIDATES:
        if os.path.isfile(path):
            return path
    return None


class Browser(object):
    """一个 Edge headless 实例 + 一条 CDP 连接（一个文档，逐页换 hash）。"""

    def __init__(self, edge, owned):
        self.edge = edge
        self.owned = owned
        self.proc = None
        self.cdp = None
        self.profile = None

    def start(self, url):
        self.profile = tempfile.mkdtemp(prefix="weblayout_edge_")
        self.owned.edge_profiles.append(self.profile)
        port = free_port(9500)
        cmd = [self.edge,
               "--headless=new",
               "--disable-gpu",
               "--no-first-run",
               "--no-default-browser-check",
               "--disable-features=Translate,MediaRouter",
               "--user-data-dir=" + self.profile,
               "--remote-debugging-port=%d" % port,
               "--window-size=1400,900",
               url]
        self.proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL,
                                     stderr=subprocess.DEVNULL)
        ws_url = cdp_client.wait_for_target(port, timeout_s=60.0)
        self.cdp = cdp_client.Cdp(ws_url, timeout=60.0)
        self.cdp.call("Page.enable")
        self.cdp.call("Runtime.enable")
        return self.cdp

    def stop(self):
        if self.cdp is not None:
            try:
                self.cdp.close()
            except Exception:                        # noqa: BLE001
                pass
            self.cdp = None
        if self.proc is not None and self.proc.poll() is None:
            if os.name == "nt":
                subprocess.run(["taskkill", "/F", "/T", "/PID",
                                str(self.proc.pid)],
                               capture_output=True,
                               env=dict(os.environ, MSYS_NO_PATHCONV="1"))
            else:
                self.proc.terminate()
            try:
                self.proc.wait(timeout=10)
            except Exception:                        # noqa: BLE001
                pass
        self.proc = None


# ============================================================================
#                          页面内采集（JS，跑在页面上下文里）
# ============================================================================
#
# [为什么采集与判定分开] 采集器只把**几何原始数据**带回 Python，判定逻辑
# 放在 Python 侧 ⇒ 失败信息能带页号/选择器/文本/几何四要素，好读。

JS_COLLECT = r"""
(function () {
  var vw = window.innerWidth, vh = window.innerHeight;
  var doc = document.documentElement;
  var body = document.body;
  var out = {
    innerWidth: vw, innerHeight: vh,
    hash: location.hash,
    docScrollWidth: doc.scrollWidth,
    docScrollHeight: doc.scrollHeight,
    bodyScrollWidth: body ? body.scrollWidth : 0,
    bodyScrollHeight: body ? body.scrollHeight : 0,
    hostChildren: 0, cardCount: 0, contentBlocks: 0,
    topOverflow: [],      /* 顶层盒超出视口右缘 */
    scrollOverflow: [],   /* 滚动容器自身超出视口右缘 */
    clipped: [],          /* 硬裁（内容溢出且不是省略号设计） */
    ellipsis: [],         /* 省略号截断（放行，但记录下来给报告） */
    tables: [],           /* 逐表格的单元格溢出/几何重叠 */
    navPanel: {}, scrim: "", drawerBtn: {}, todo: 0,
  };

  var _cache = new WeakMap();
  function cs(el) {
    var v = _cache.get(el);
    if (!v) { v = getComputedStyle(el); _cache.set(el, v); }
    return v;
  }
  function visible(el) {
    if (!el || el.nodeType !== 1) return false;
    if (el.namespaceURI === "http://www.w3.org/2000/svg") return false;
    var r = el.getBoundingClientRect();
    if (r.width <= 0 || r.height <= 0) return false;
    var c = cs(el);
    if (c.display === "none" || c.visibility === "hidden") return false;
    if (el.offsetParent === null && c.position !== "fixed") return false;
    return true;
  }
  function selOf(el) {
    var s = el.tagName.toLowerCase();
    if (el.id) s += "#" + el.id;
    var c = (el.getAttribute("class") || "").trim().split(/\s+/)
             .filter(Boolean).slice(0, 3);
    if (c.length) s += "." + c.join(".");
    return s;
  }
  function textOf(el) {
    var t = (el.textContent || "").replace(/\s+/g, " ").trim();
    return t.length > 40 ? t.slice(0, 40) + "…" : t;
  }
  function directText(el) {
    for (var i = 0; i < el.childNodes.length; i++) {
      var n = el.childNodes[i];
      if (n.nodeType === 3 && n.nodeValue && n.nodeValue.trim() !== "") {
        return true;
      }
    }
    return false;
  }
  function isScroller(el) {
    var ox = cs(el).overflowX;
    return ox === "auto" || ox === "scroll";
  }
  function isClipper(el) {
    var ox = cs(el).overflowX;
    return ox === "auto" || ox === "scroll" || ox === "hidden" || ox === "clip";
  }
  /* 最近的裁剪祖先（含 hidden/clip；滚动容器也算——里面的东西由它兜着） */
  function clipAncestor(el) {
    var p = el.parentElement;
    while (p && p !== body && p !== doc) {
      if (isClipper(p)) return p;
      p = p.parentElement;
    }
    return null;
  }
  /* 祖先里有没有已经被横向滚动的容器（有 ⇒ 本元素的 rect 是滚后坐标） */
  function scrolledAncestor(el) {
    var p = el.parentElement;
    while (p && p !== body && p !== doc) {
      if (isScroller(p) && p.scrollLeft > 0) return p;
      p = p.parentElement;
    }
    return null;
  }

  var all = body ? body.querySelectorAll("*") : [];

  /* ---- A1/A2：顶层盒（没有裁剪祖先的）不得超出视口右缘 ---- */
  for (var i = 0; i < all.length; i++) {
    var el = all[i];
    if (!visible(el)) continue;
    if (clipAncestor(el)) continue;          /* 在滚动/裁剪容器里 ⇒ 另说 */
    var r = el.getBoundingClientRect();
    if (r.right > vw + 1) {
      out.topOverflow.push({sel: selOf(el), right: Math.round(r.right),
                            width: Math.round(r.width),
                            left: Math.round(r.left),
                            text: textOf(el)});
    }
  }

  /* ---- A3：滚动容器自身不得超出视口右缘（内部横向滚动是设计） ---- */
  for (var i2 = 0; i2 < all.length; i2++) {
    var e2 = all[i2];
    if (!visible(e2) || !isScroller(e2)) continue;
    if (scrolledAncestor(e2)) continue;      /* 祖先已横向滚动 ⇒ 坐标失真 */
    var r2 = e2.getBoundingClientRect();
    if (r2.right > vw + 1) {
      out.scrollOverflow.push({sel: selOf(e2), right: Math.round(r2.right),
                               width: Math.round(r2.width),
                               scrollWidth: e2.scrollWidth,
                               clientWidth: e2.clientWidth,
                               text: textOf(e2)});
    }
  }

  /* ---- B：元素级文字硬裁 ----
   * [口径] 只看「承载文字」的元素：有直接文本节点的，或本身就是内容容器
   * （.card/.stat/.form-row）。表格内部元素交给 C 段（表格允许横向滚动，
   * 用 B 的口径会把整张表判红）。input/textarea/select 本来就会滚动。 */
  var TEXT_SEL = "td, th, .card, .stat, .dot-name, .valve-name, .menu-name, " +
                 ".form-row, label, span, button, a, div, p, li, h1, h2, h3, " +
                 "strong, em, td";
  var cands = body ? body.querySelectorAll(TEXT_SEL) : [];
  for (var j = 0; j < cands.length; j++) {
    var c = cands[j];
    if (!visible(c)) continue;
    var tag = c.tagName.toLowerCase();
    if (tag === "input" || tag === "textarea" || tag === "select") continue;
    if (c.closest("table")) continue;          /* 表格内部 → C 段 */
    var ccs = cs(c);
    var isContainer = c.matches(".card, .stat, .form-row");
    if (!isContainer && !directText(c)) continue;
    var sw = c.scrollWidth, cw = c.clientWidth;
    if (sw - cw <= 1) continue;
    var txt = textOf(c);
    var ell = (ccs.textOverflow === "ellipsis") &&
              (ccs.overflowX === "hidden" || ccs.overflowX === "clip");
    var words = txt.split(/\s+/).filter(Boolean);
    var shortOne = words.length <= 1 && txt.replace(/\s/g, "").length <= 4;
    var rec = {sel: selOf(c), text: txt, scrollWidth: sw, clientWidth: cw,
               overflowX: ccs.overflowX, textOverflow: ccs.textOverflow,
               whiteSpace: ccs.whiteSpace, tag: tag};
    if (ell && !(ccs.whiteSpace === "nowrap" && shortOne)) {
      out.ellipsis.push(rec);                   /* 省略号设计 ⇒ 放行 */
    } else {
      out.clipped.push(rec);                   /* 硬裁 ⇒ FAIL */
    }
  }

  /* ---- C：表格逐单元格溢出 + 逐列几何重叠 ---- */
  var tbls = body ? body.querySelectorAll("#page-host table") : [];
  for (var t = 0; t < tbls.length; t++) {
    var tbl = tbls[t];
    var info = {cls: (tbl.getAttribute("class") || ""), rows: 0,
                overCells: [], ellipsisCells: [], overlaps: [], colWidths: []};
    var trs = tbl.querySelectorAll("tr");
    info.rows = trs.length;
    for (var ri = 0; ri < trs.length; ri++) {
      var cells = trs[ri].children;
      var rects = [];
      for (var ci = 0; ci < cells.length; ci++) {
        var cell = cells[ci];
        var ccs2 = cs(cell);
        var cr = cell.getBoundingClientRect();
        var rec2 = {col: ci, tag: cell.tagName.toLowerCase(),
                    scrollWidth: cell.scrollWidth, clientWidth: cell.clientWidth,
                    overflowX: ccs2.overflowX, textOverflow: ccs2.textOverflow,
                    whiteSpace: ccs2.whiteSpace, text: textOf(cell),
                    left: Math.round(cr.left), right: Math.round(cr.right),
                    width: Math.round(cr.width)};
        rects.push(rec2);
        if (rec2.scrollWidth - rec2.clientWidth > 1) {
          var isEll = (ccs2.overflowX === "hidden" || ccs2.overflowX === "clip") &&
                      (ccs2.textOverflow === "ellipsis");
          if (isEll) info.ellipsisCells.push(rec2);
          else info.overCells.push(rec2);
        }
      }
      for (var k = 0; k + 1 < rects.length; k++) {
        if (rects[k].right > rects[k + 1].left + 1) {
          info.overlaps.push({a: rects[k].col, b: rects[k + 1].col,
                              aRight: rects[k].right, bLeft: rects[k + 1].left,
                              aText: rects[k].text, bText: rects[k + 1].text});
        }
      }
      if (ri === 0) {
        info.colWidths = rects.map(function (x) { return x.width; });
        info.headTexts = rects.map(function (x) { return x.text; });
      }
    }
    out.tables.push(info);
  }

  /* ---- 页面级计数（E 段用） ---- */
  var host = document.getElementById("page-host");
  out.hostChildren = host ? host.childElementCount : -1;
  out.hostScrollWidth = host ? host.scrollWidth : 0;
  out.hostClientWidth = host ? host.clientWidth : 0;
  out.cardCount = body ? body.querySelectorAll(".card").length : 0;
  out.contentBlocks = body ? body.querySelectorAll(
      "#page-host .card, #page-host .menu-entry, #page-host .dt-table-wrap, " +
      "#page-host .analog-table-wrap, #page-host table, " +
      "#page-host .camera-stage, #page-host .home-cards, " +
      "#page-host .valve-grid, #page-host .stat-grid").length : 0;
  out.todo = body ? body.querySelectorAll(".todo-page").length : 0;
  var shell = document.getElementById("app-shell");
  out.shellClass = shell ? shell.className : "";
  var lview = document.getElementById("login-view");
  out.loginClass = lview ? lview.className : "";

  /* ---- 抽屉 / 导航面板形态（D 段用） ---- */
  var np = document.getElementById("nav-panel");
  if (np) {
    var ncs = cs(np);
    var nr = np.getBoundingClientRect();
    out.navPanel = {position: ncs.position, transform: ncs.transform,
                    display: ncs.display, left: Math.round(nr.left),
                    right: Math.round(nr.right), width: Math.round(nr.width),
                    navOpen: body.classList.contains("nav-open")};
  }
  var scrim = document.getElementById("nav-scrim");
  out.scrim = scrim ? cs(scrim).display : "";
  var mb = document.querySelector(".tb-menu-btn");
  if (mb) {
    var mr = mb.getBoundingClientRect();
    var mcs = cs(mb);
    out.drawerBtn = {text: mb.textContent, display: mcs.display,
                      visibility: mcs.visibility, width: Math.round(mr.width),
                      height: Math.round(mr.height),
                      visible: mr.width > 0 && mr.height > 0 &&
                               mcs.display !== "none" &&
                               mcs.visibility !== "hidden"};
  }

  /* ---- F 段：触达目标 ≥44px（触摸可点面积）----
   * [口径] 取**有效命中区**：元素自身 rect 与最近 label 祖先 rect 的较大者
   * （label 包裹的复选框/单选，点文字也能切换 ⇒ 命中区是整行，不是小方框）。
   * 只采样真正的交互元素；span/div 之类不算。 */
  out.smallTargets = [];
  var hitSel = "button, select, input[type=checkbox], input[type=radio],"
             + " input[type=range], a.btn, [role=button], .btn, .float-close,"
             + " .tb-menu-btn, .mcc-item, .menu-item, .nav-item";
  var hits = body ? body.querySelectorAll(hitSel) : [];
  for (var hi = 0; hi < hits.length; hi++) {
    var h = hits[hi];
    if (!visible(h)) continue;
    var hr = h.getBoundingClientRect();
    var hw = hr.width, hh = hr.height;
    /* [只认"包着自身控件"的 label] label.control === h 才用 label 的 rect
     * 放大命中区（防止将来"大 label 包别人的控件"把 22px 控件洗成 44px）。 */
    var lab = h.closest ? h.closest("label") : null;
    if (lab && lab.control === h) {
      var lr = lab.getBoundingClientRect();
      hw = Math.max(hw, lr.width);
      hh = Math.max(hh, lr.height);
    }
    if (hw > 0 && hh > 0 && (hw < 44 || hh < 44)) {
      /* 父链（两级）便于定位：同一个 input.check 可能在不同卡里 */
      var chain = [], p = h.parentElement, pi = 0;
      while (p && p !== body && pi < 2) {
        chain.push(selOf(p));
        p = p.parentElement; pi++;
      }
      out.smallTargets.push({sel: selOf(h), w: Math.round(hw),
                             h: Math.round(hh),
                             path: chain.join(" < ")});
    }
  }
  return out;
})()
"""


JS_DRAWER_CLICK = r"""
(function () {
  var b = document.querySelector(".tb-menu-btn");
  if (!b) return {ok: false, why: "找不到 .tb-menu-btn"};
  b.click();
  return {ok: true};
})()
"""

JS_DRAWER_STATE = r"""
(function () {
  var np = document.getElementById("nav-panel");
  var scrim = document.getElementById("nav-scrim");
  var r = np ? np.getBoundingClientRect() : null;
  var c = np ? getComputedStyle(np) : null;
  return {
    navOpen: document.body.classList.contains("nav-open"),
    position: c ? c.position : "",
    transform: c ? c.transform : "",
    display: c ? c.display : "",
    left: r ? Math.round(r.left) : 0,
    right: r ? Math.round(r.right) : 0,
    width: r ? Math.round(r.width) : 0,
    scrim: scrim ? getComputedStyle(scrim).display : "",
  };
})()
"""

JS_NAV_CLICK = r"""
(function (caption) {
  var items = document.querySelectorAll("#nav-panel .nav-item");
  for (var i = 0; i < items.length; i++) {
    if ((items[i].textContent || "").trim() === caption) {
      items[i].click();
      return {ok: true, hash: location.hash};
    }
  }
  return {ok: false, count: items.length};
})
"""

JS_HASH = "location.hash"


# ============================================================================
#                          检查结果收集
# ============================================================================

class Result(object):
    """一条断言结果。"""

    def __init__(self, viewport, page, label, ok, detail="", severity="FAIL"):
        self.viewport = viewport
        self.page = page
        self.label = label
        self.ok = ok
        self.detail = detail
        self.severity = severity

    def format(self):
        where = "页%s" % self.page if self.page is not None else "全局"
        head = "  %s %s · %s" % (self.severity, where, self.label)
        return head + ("\n       " + self.detail if self.detail else "")


class Collector(object):
    """按 (视口, 页) 收集断言结果。"""

    def __init__(self):
        self.items = []

    def add(self, viewport, page, label, ok, detail="", severity="FAIL"):
        self.items.append(Result(viewport, page, label, bool(ok), detail,
                                 severity))

    def ok_count(self, viewport=None, severity=None):
        n = 0
        for it in self.items:
            if viewport is not None and it.viewport != viewport:
                continue
            if severity is not None and it.severity != severity:
                continue
            if it.ok:
                n += 1
        return n

    def fail_count(self, viewport=None, severity=None):
        n = 0
        for it in self.items:
            if viewport is not None and it.viewport != viewport:
                continue
            if severity is not None and it.severity != severity:
                continue
            if not it.ok:
                n += 1
        return n

    def failures(self, viewport=None):
        return [it for it in self.items
                if not it.ok and (viewport is None or it.viewport == viewport)]


# ============================================================================
#                          逐页判定
# ============================================================================

def judge_page(res, viewport_tag, page, col, reference):
    """把一页的采集数据判成一组断言。"""
    # ---- 视口本身 ----
    col.add(viewport_tag, page, "视口宽度等于设定值",
            res.get("innerWidth") == res.get("expectWidth"),
            "innerWidth=%s 设定=%s" % (res.get("innerWidth"),
                                       res.get("expectWidth")))
    # [页面确实切过去了] 判据要在**目标页**上取 ⇒ 先确认 hash 对得上，
    # 否则量的是上一页的 DOM（切页失败会被误判成「这页布局没问题」）。
    col.add(viewport_tag, page, "已切到目标页（hash 一致）",
            str(res.get("hash") or "") == "#/page/%d" % page,
            "hash=%s 期望=#/page/%s" % (res.get("hash"), page))

    # ---- A 页面级横向溢出 ----
    iw = res.get("innerWidth") or 0
    col.add(viewport_tag, page, "文档不横向滚动（scrollWidth ≤ 视口）",
            res.get("docScrollWidth", 0) <= iw + 1,
            "documentElement.scrollWidth=%s > innerWidth=%s"
            % (res.get("docScrollWidth"), iw))
    if res.get("bodyScrollWidth", 0) > iw + 1:
        col.add(viewport_tag, page, "body 不横向滚动", False,
                "body.scrollWidth=%s > innerWidth=%s"
                % (res.get("bodyScrollWidth"), iw))
    else:
        col.add(viewport_tag, page, "body 不横向滚动", True)

    tops = res.get("topOverflow") or []
    col.add(viewport_tag, page, "顶层盒不超出视口右缘", not tops,
            "；".join("%s right=%s（视口 %s）" % (t["sel"], t["right"], iw)
                      for t in tops[:6]))
    scrolls = res.get("scrollOverflow") or []
    col.add(viewport_tag, page, "滚动容器自身不超出视口右缘", not scrolls,
            "；".join("%s right=%s scrollWidth=%s clientWidth=%s"
                      % (t["sel"], t["right"], t.get("scrollWidth"),
                         t.get("clientWidth")) for t in scrolls[:6]))
    # [内容区不横向滚动] theme.css 的既定设计是「**只有**表格容器内部允许
    # 横向滚动」⇒ 内容区（#page-host，overflow:auto）自己不该出现横向滚动
    # 条。它一旦横向滚动 ⇒ 必有盒子/文字超出了内容区（多半是被卡片裁掉的
    # 长路径之类），用户只能左右拖页面才看得到 ⇒ 判失败。
    col.add(viewport_tag, page,
            "内容区不横向滚动（横向滚动只允许发生在表格容器内）",
            (res.get("hostScrollWidth") or 0) <= (res.get("hostClientWidth") or 0) + 1,
            "page-host.scrollWidth=%s clientWidth=%s"
            % (res.get("hostScrollWidth"), res.get("hostClientWidth")))

    # ---- F 触达目标（只对 ≤860 档判：44px 规则就活在 ≤860 媒体里）----
    if iw and iw <= DRAWER_MAX_W:
        small = res.get("smallTargets") or []
        col.add(viewport_tag, page, "触达目标 ≥44px（有效命中区含 label 包裹）",
                not small,
                "；".join("%s %dx%d（%s）" % (t["sel"], t["w"], t["h"],
                                            t.get("path") or "-")
                          for t in small[:6]))

    # ---- B 元素级文字硬裁 ----
    clipped = res.get("clipped") or []
    col.add(viewport_tag, page, "无文字硬裁（内容溢出且非省略号设计）",
            not clipped,
            "；".join("%s「%s」scrollWidth=%s clientWidth=%s overflowX=%s"
                      % (c["sel"], c["text"], c["scrollWidth"],
                         c["clientWidth"], c["overflowX"])
                      for c in clipped[:6]))
    ell = res.get("ellipsis") or []
    if ell:
        col.add(viewport_tag, page, "省略号截断（放行，仅记录）", True,
                "%d 处：%s" % (len(ell), "；".join(
                    "%s「%s」%s/%s" % (c["sel"], c["text"], c["scrollWidth"],
                                      c["clientWidth"]) for c in ell[:4])),
                severity="WARN")

    # ---- C 表格列不叠列 ----
    tables = res.get("tables") or []
    over_all, ell_all, ov_all = [], [], []
    for t in tables:
        over_all.extend(t.get("overCells") or [])
        ell_all.extend(t.get("ellipsisCells") or [])
        ov_all.extend(t.get("overlaps") or [])
    col.add(viewport_tag, page, "表格单元格内容不溢出到隔壁列", not over_all,
            "；".join("列%s「%s」scrollWidth=%s clientWidth=%s overflowX=%s"
                      % (c["col"], c["text"], c["scrollWidth"],
                         c["clientWidth"], c["overflowX"])
                      for c in over_all[:6]))
    col.add(viewport_tag, page, "表格相邻列几何不重叠", not ov_all,
            "；".join("列%s右缘%s > 列%s左缘%s（%s | %s）"
                      % (o["a"], o["aRight"], o["b"], o["bLeft"],
                         o["aText"], o["bText"]) for o in ov_all[:6]))
    if ell_all:
        col.add(viewport_tag, page, "表格单元格省略号截断（放行，仅记录）", True,
                "%d 处：%s" % (len(ell_all), "；".join(
                    "列%s「%s」%s/%s" % (c["col"], c["text"], c["scrollWidth"],
                                        c["clientWidth"])
                    for c in ell_all[:4])), severity="WARN")

    # ---- E 竖屏内容不丢 ----
    if res.get("expectMobile"):
        col.add(viewport_tag, page, "页面有内容高度（body.scrollHeight>0）",
                (res.get("bodyScrollHeight") or 0) > 0,
                "body.scrollHeight=%s" % res.get("bodyScrollHeight"))
        base = reference.get(page)
        if base is not None:
            col.add(viewport_tag, page, "内容块数量与桌面档一致（不丢内容）",
                    (res.get("contentBlocks") or 0) >= base["contentBlocks"],
                    "移动 contentBlocks=%s 桌面基准=%s（host 子节点=%s "
                    "shell=%s login=%s）"
                    % (res.get("contentBlocks"), base["contentBlocks"],
                       res.get("hostChildren"), res.get("shellClass"),
                       res.get("loginClass")))
            if base["cardCount"] > 0:
                col.add(viewport_tag, page, "关键卡片（.card）数量 > 0",
                        (res.get("cardCount") or 0) > 0,
                        "cardCount=%s 桌面基准=%s"
                        % (res.get("cardCount"), base["cardCount"]))

    # ---- 页面挂载失败（router 兜底页）----
    if res.get("todo"):
        col.add(viewport_tag, page, "页面挂载成功（无「页面加载出错」兜底）",
                False, "页面上出现 .todo-page（router 兜底）")


def judge_drawer(res, viewport_tag, col):
    """D 段：抽屉导航（≤860px）与夹缝档形态（861~1023px）。"""
    width = res.get("innerWidth") or 0
    if width <= DRAWER_MAX_W:
        btn = res.get("drawerBtn") or {}
        col.add(viewport_tag, None, "☰ 按钮可见", bool(btn.get("visible")),
                "btn=%s display=%s visibility=%s %sx%s"
                % (btn.get("text"), btn.get("display"), btn.get("visibility"),
                   btn.get("width"), btn.get("height")))
        st = res.get("drawerAfterClick") or {}
        left = st.get("left")
        opened = bool(st.get("navOpen")) and isinstance(left, (int, float)) \
            and left >= -1
        col.add(viewport_tag, None, "点 ☰ 后抽屉展开（面板滑入屏内）", opened,
                "nav-open=%s position=%s transform=%s left=%s right=%s "
                "遮罩=%s" % (st.get("navOpen"), st.get("position"),
                             st.get("transform"), st.get("left"),
                             st.get("right"), st.get("scrim")))
        nav = res.get("navAfterClick") or {}
        closed = not nav.get("navOpen")
        col.add(viewport_tag, None, "点导航项后抽屉自动收起", closed,
                "nav-open=%s hash=%s" % (nav.get("navOpen"), nav.get("hash")))
        col.add(viewport_tag, None, "点导航项后 hash 切到对应页",
                str(nav.get("hash") or "") == "#/page/%s" % nav.get("wantPage"),
                "hash=%s 期望=#/page/%s" % (nav.get("hash"), nav.get("wantPage")))
    else:
        np = res.get("navPanel") or {}
        col.add(viewport_tag, None,
                "夹缝档（%dpx）导航不是抽屉形态（position != fixed）" % width,
                np.get("position") != "fixed",
                "nav-panel position=%s transform=%s left=%s width=%s"
                % (np.get("position"), np.get("transform"), np.get("left"),
                   np.get("width")))
        col.add(viewport_tag, None, "夹缝档导航面板在版内可见",
                (np.get("width") or 0) > 0 and (np.get("right") or 0) > 0,
                "nav-panel width=%s left=%s right=%s"
                % (np.get("width"), np.get("left"), np.get("right")))


# ============================================================================
#                          主流程
# ============================================================================

def goto_page(cdp, page, timeout=12.0):
    """跳到某页并等它稳定（内容不再变化）。"""
    cdp.evaluate("location.hash = '#/page/%d'" % page)
    deadline = time.time() + timeout
    last, stable = None, 0
    while time.time() < deadline:
        n = cdp.evaluate(
            "(function(){var h=document.getElementById('page-host');"
            "return h?h.innerHTML.length:-1;})()")
        if isinstance(n, (int, float)) and n > 0 and n == last:
            stable += 1
            if stable >= 3:
                break
        else:
            stable = 0
        last = n
        time.sleep(0.3)
    # 数据页再等一手：表格要真出行，否则叠列判据是空转。
    if page in (9, 10, 11):
        deadline = time.time() + 5.0
        while time.time() < deadline:
            rows = cdp.evaluate(
                "(function(){var t=document.querySelector('#page-host table');"
                "return t?t.querySelectorAll('tbody tr').length:0;})()")
            if isinstance(rows, (int, float)) and rows > 0:
                break
            time.sleep(0.4)


def collect(cdp, expect_width):
    res = cdp.evaluate(JS_COLLECT)
    if not isinstance(res, dict):
        raise RuntimeError("页面采集失败（Runtime.evaluate 没给对象）")
    res["expectWidth"] = expect_width
    return res


def run_viewport(cdp, col, width, height, tag, mobile, pages, reference,
                 is_reference=False):
    """跑一个视口档位（逐页）。"""
    cdp.call("Emulation.setDeviceMetricsOverride",
             {"width": width, "height": height, "deviceScaleFactor": 1,
              "mobile": bool(mobile)})
    time.sleep(0.4)
    got = cdp.evaluate("window.innerWidth")
    col.add(tag, None, "视口覆写生效（Emulation.setDeviceMetricsOverride）",
            got == width, "window.innerWidth=%s 设定=%s" % (got, width))

    for page in pages:
        goto_page(cdp, page)
        res = collect(cdp, width)
        res["expectMobile"] = bool(mobile)
        judge_page(res, tag, page, col, reference)
        # [内容基准只取最宽档] E 段比的是「移动端内容块不该比桌面少」⇒ 基准
        # 必须来自**桌面档**；夹缝档（900）也是宽屏但布局已退化，不做基准。
        if is_reference and reference is not None:
            reference[page] = {"contentBlocks": res.get("contentBlocks", 0),
                               "cardCount": res.get("cardCount", 0)}

    # ---- D 段：抽屉 / 夹缝档 ----
    if width <= DRAWER_MAX_W:
        cdp.evaluate(JS_DRAWER_CLICK)
        time.sleep(0.45)          # 等 0.18s 的滑出动画走完
        res = collect(cdp, width)
        res["drawerAfterClick"] = res.get("navPanel") or {}
        res["drawerAfterClick"]["navOpen"] = \
            bool((res.get("navPanel") or {}).get("navOpen"))
        res["drawerAfterClick"]["scrim"] = res.get("scrim") or ""
        # 点一个导航项（"数据" 组的孩子「历史数据」= 页 9）
        clicked = cdp.evaluate("(%s)(%s)" % (JS_NAV_CLICK,
                                            json.dumps("历史数据")))
        time.sleep(0.5)
        st = cdp.evaluate(JS_DRAWER_STATE)
        st = st if isinstance(st, dict) else {}
        st["hash"] = cdp.evaluate(JS_HASH)
        st["wantPage"] = 9
        res["navAfterClick"] = st
        res["expectWidth"] = width
        res["innerWidth"] = res.get("innerWidth")
        judge_drawer(res, tag, col)
        if not (clicked or {}).get("ok"):
            col.add(tag, None, "导航树里有可点的导航项", False,
                    "找不到文本为「历史数据」的导航项")
    else:
        res = collect(cdp, width)
        judge_drawer(res, tag, col)


def _save_shot(cdp, out_dir, name):
    """存一张截图（失败只提示，不影响断言）。"""
    try:
        png = cdp.screenshot_png()
        with open(os.path.join(out_dir, name), "wb") as fh:
            fh.write(png)
    except Exception as exc:                        # noqa: BLE001
        print("  · 截图失败（%s）：%r" % (name, exc))


#: 截图前把内容滚进视口的选择器（表格页滚到表、卡片页滚到卡）。
JS_SCROLL_INTO_VIEW = r"""
(function () {
  var host = document.getElementById("page-host");
  if (!host) return false;
  var el = null;
  var cards = host.querySelectorAll(".card");
  for (var i = 0; i < cards.length; i++) {
    if ((cards[i].textContent || "").indexOf("数据存储") === 0) {
      el = cards[i]; break;
    }
  }
  if (!el) {
    el = host.querySelector("table") || host.querySelector(".card") ||
         host.firstElementChild;
  }
  if (el) el.scrollIntoView({block: "start"});
  var hr = host.getBoundingClientRect();
  var er = el ? el.getBoundingClientRect() : null;
  if (er) host.scrollTop += (er.top - hr.top);
  var wrap = host.querySelector(".dt-table-wrap, .analog-table-wrap");
  if (wrap) wrap.scrollLeft = 0;   /* 表格横向滚到最左（自然位置） */
  return true;
})()
"""


def _shot_page(cdp, out_dir, width, height, page):
    """截某页（先滚到内容，再截图）。"""
    goto_page(cdp, page)
    cdp.evaluate(JS_SCROLL_INTO_VIEW)
    time.sleep(0.4)
    _save_shot(cdp, out_dir,
               "viewport_%dx%d_page%02d.png" % (width, height, page))


def main(argv):
    ap = argparse.ArgumentParser(
        prog="webui_layout_check.py",
        description="网页端布局/排版自动化几何检查（纯标准库 + CDP）")
    ap.add_argument("--web-port", type=int, default=0,
                    help="网页端口覆盖（缺省：环境变量 → 在跑的 hsfweb → "
                         "候选端口 → 自起）")
    ap.add_argument("--upstream", type=int, default=0,
                    help="上游（XLink 服务端）端口覆盖（缺省：复用/自起）")
    ap.add_argument("--own", action="store_true",
                    help="强制自起一套（上游 + hsfweb），不复用常驻")
    ap.add_argument("--viewport", default="",
                    help="只跑某一档视口，如 390x844（可重复用逗号分隔）")
    ap.add_argument("--pages", default="",
                    help="只跑某几页，如 9,10,11（缺省：除保留槽位外全跑）")
    ap.add_argument("--shots", default=None,
                    help="视口截图目录；缺省 out/webui_shots（已 gitignore，"
                         "只截触摸档）；显式传目录则每档都截；传空串关闭")
    ap.add_argument("--shot-pages", default="",
                    help="--shots 时额外逐页截图的页号（如 9,10,11,14）")
    ap.add_argument("--user", default="root", help="网页登录账号（缺省 root）")
    ap.add_argument("--password", default="root", help="网页登录口令（缺省 root）")
    args = ap.parse_args(argv)

    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")

    # ---- 浏览器 ----
    edge = find_edge()
    if not edge:
        print("[weblayout] 找不到 Edge/Chrome（候选：%s）"
              % "、".join(EDGE_CANDIDATES))
        print("[weblayout] 装一个 Edge/Chrome 后重跑；退出码 2")
        return 2
    print("[weblayout] 浏览器：%s" % edge)

    viewports = VIEWPORTS
    if args.viewport:
        want = set()
        for piece in args.viewport.split(","):
            piece = piece.strip().lower()
            if not piece:
                continue
            if "x" in piece:
                w, _, h = piece.partition("x")
                want.add((int(w), int(h)))
        viewports = [v for v in VIEWPORTS if (v[0], v[1]) in want]
        if not viewports:
            print("[weblayout] --viewport 没匹配到任何档位：%s" % args.viewport)
            return 2
    pages = [p for p in range(PAGE_COUNT) if p not in RESERVED_PAGES]
    if args.pages:
        pages = [int(x) for x in args.pages.split(",") if x.strip() != ""]
    shot_pages = []
    if args.shot_pages:
        shot_pages = [int(x) for x in args.shot_pages.split(",")
                      if x.strip() != ""]

    owned = Owned()
    col = Collector()
    t0 = time.time()
    browser = None
    web_log = ""
    sim_log = ""
    try:
        # ---- 环境 ----
        if args.own:
            upstream = 0
            if not args.upstream:
                port = free_port(6100)
                print("  · 上游：--own ⇒ 自起 C sim（临时程序根）于 %d" % port)
                upstream = start_c_sim(port, owned)
            else:
                upstream = int(args.upstream)
            web_port = resolve_web_port(args, upstream, owned, force_own=True)
        else:
            upstream = resolve_upstream(args, owned)
            web_port = resolve_web_port(args, upstream, owned)

        print("  · 探活：上游 %d / 网页 %d" % (upstream, web_port))
        if not hsfweb_ping(web_port):
            print("[weblayout] 网页 %d 上不是 hsfweb（/api/ping 认不出）" % web_port)
            return 2

        token = web_login(web_port, args.user, args.password)
        print("  · 登录成功（%s）" % args.user)

        # ---- 浏览器 ----
        url = "http://%s:%d/?authtoken=%s#/page/0" % (
            HOST, web_port, urllib.parse.quote(token))
        browser = Browser(edge, owned)
        cdp = browser.start(url)
        time.sleep(2.0)             # 直登 + 壳装配 + 首帧快照

        # 桌面档先跑（做 E 段的「内容基准」）
        reference = {}
        order = sorted(viewports,
                       key=lambda v: 0 if v[0] > NARROW_GRID_MAX_W else 1)
        widest = max(v[0] for v in viewports)
        for width, height, tag, mobile in order:
            print("[weblayout] 视口 %dx%d（%s）…" % (width, height, tag))
            run_viewport(cdp, col, width, height, tag, mobile, pages, reference,
                         is_reference=(width == widest))
            # [2026-11-22 截图证据] 缺省目录只截触摸档（≤860，含 768 平板）；
            # **显式传 --shots DIR** 则每档都截（保持旧语义，桌面档也留证据）；
            # 传空串关闭。整块 try：截图失败/磁盘满不能把几何门打挂。
            if args.shots is None:
                shots_dir = "out/webui_shots"
                only_touch = True
            else:
                shots_dir = args.shots
                only_touch = False
            if shots_dir and (not only_touch or width <= DRAWER_MAX_W
                              or shot_pages):
                try:
                    os.makedirs(shots_dir, exist_ok=True)
                    _save_shot(cdp, shots_dir,
                               "viewport_%dx%d.png" % (width, height))
                    for sp in shot_pages:
                        _shot_page(cdp, shots_dir, width, height, sp)
                except Exception as exc:            # noqa: BLE001
                    print("  · 截图跳过（%s）" % exc)
    except Exception as exc:                        # noqa: BLE001
        print("[weblayout] 环境/执行异常：%s: %s"
              % (type(exc).__name__, exc))
        return 2
    finally:
        if browser is not None:
            browser.stop()
        web_log = owned.tail("webui(")
        sim_log = owned.tail("c-sim(")
        owned.cleanup()

    # ---- 报告 ----
    print("")
    print("=" * 72)
    for width, height, tag, _m in order:
        p = col.ok_count(tag, "FAIL")
        f = col.fail_count(tag, "FAIL")
        w = col.fail_count(tag, "WARN")
        print("[%s] 通过 %d 项 / 失败 %d 项%s"
              % (tag, p, f, ("（另有 %d 条省略号截断提示）" % w) if w else ""))
        for it in col.failures(tag):
            if it.severity != "FAIL":
                continue
            print(it.format())
    print("-" * 72)
    print("合计（用时 %.0fs）：通过 %d 项 / 失败 %d 项 / 提示 %d 项"
          % (time.time() - t0, col.ok_count(severity="FAIL"),
             col.fail_count(severity="FAIL"), col.fail_count(severity="WARN")))

    warns = [it for it in col.items
             if not it.ok and it.severity == "WARN"]
    if warns:
        print("")
        print("提示（省略号截断 = 设计如此，不判失败）：")
        for it in warns[:40]:
            print(it.format())
        if len(warns) > 40:
            print("  …（另有 %d 条同类提示）" % (len(warns) - 40))

    if col.fail_count(severity="FAIL") and (web_log or sim_log):
        print("")
        print("自起进程的日志尾巴（定位用）：")
        if web_log:
            print("  [webui]\n    " + web_log.replace("\n", "\n    "))
        if sim_log:
            print("  [c-sim]\n    " + sim_log.replace("\n", "\n    "))

    return 0 if col.fail_count(severity="FAIL") == 0 else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
