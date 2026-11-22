# -*- coding: utf-8 -*-
"""Py 客户端 16 页参考截图（verify/webui/ref_sweep.py）。

用法
====

    python verify/webui/ref_sweep.py          # 编排器：探活/停 webui/起子进程/拉回 webui
    D:/anaconda3/python.exe verify/webui/ref_sweep.py --qt   # 截图子进程（编排器自动调）

职责与口径（与 web_sweep_edge.py 配对，作为「界面对齐」的参考基准）
====================================================================

0. **[2026-11-13「三页合并」后的页数]** 参考客户端现在只有 **16 页**
   （client_py/hsfsrv_client/pages_registry.py:38-42 PAGE_COUNT=16；原
   「悬浮窗设置(16) / 窗口设置(17)」两页的内容并入「本地设置(13)」，页号
   16/17 从注册表与导航树删除）⇒ 本脚本与网页端都按 **16 页**扫。
   [旧基线作废] 合并**之前**拍的 18 张参考图（shots/ref/p00..p17.png）
   不能再当基准：导航树还列着两个已注销的页、且「本地设置」页缺合并后
   新增的卡 ⇒ p13/p16/p17 的比对无效（会误判成「网页多了/少了东西」）。
   合并后的基线必须用本脚本重拍。
1. **真窗口，禁 offscreen**：本机 QT_QPA_PLATFORM=offscreen 无字体、整页
   空白 ⇒ 子进程环境里显式钉 ``QT_QPA_PLATFORM=windows``，绝不允许 offscreen。
2. **必须用 D:/anaconda3/python.exe 跑 Qt**（PyQt5 装在 anaconda；PATH 的
   python 是纯 3.12，没有 PyQt5）。编排器用任意 python 都能跑——它只做
   stdlib 编排，Qt 子进程由它显式用 anaconda 解释器拉起。
3. **单进程单会话扫全部 16 页**：绝不能每页起一个进程——服务端同账会话
   互斥，第二个连接会被拒；且每页一进程每页都要重新登录+首帧推送，时序
   不可复现。这里一个 MainWindow 从 p00 走到 p15。
4. **root 会话互斥**：webui（python -m hsfweb）的上游主链路也用 root 登录
   ⇒ ref_sweep 开始前**必须先停 webui**，跑完等 8 秒再把它拉回
   （8 秒 = C 服务端把旧 root 会话按静默超时回收的窗口，立即拉回会被
   「该账户已在线」瞬时拒绝）。
   **只停 hsfweb 自己**：认进程 = 进程名 python*.exe + 命令行含
   `-m hsfweb` + **正在监听目标网页端口**——本机 8080 上跑着别人的常驻
   程序 F:\\直滤\\read_analog_gui.py（按端口杀会误杀用户的程序），别的阶段
   还并跑着 8086/8094/8097/8098/8100 等隔离实例（也不能碰）。
   **网页端口运行期解析**：环境变量 HSF_WEB_PORT → 在跑的 hsfweb（优先
   「上游 = 6100」那个，同上游多个取最小端口 ⇒ 确定）→ 候选端口探活
   /api/ping（自报 server=hsf-webui）；拉回时显式 `--web-port <原端口>`
   （缺省 8080 被占时 hsfweb 会改听随机端口 ⇒ 常驻服务失联）。
5. **每页等够事件循环再 grab**：win.goto_page(i) 后用 QEventLoop +
   QTimer.singleShot(≥1200ms) 跑完 QSS polish / 主题安装 / 首帧推送，
   再 win.grab() 落盘——截图不是"切页那一瞬"，是"数据到了那一拍"。
6. **收尾按 __main__._teardown 同款顺序拆**：先断桥（闸网络线程 emit）
   → 关窗 → 泵事件消化 deleteLater。唯一差别：不写 K_WIN_SIZE/K_FULLSCREEN
   到用户 INI（截图工具不该改用户本机设置；本例设置也重定向到了临时 INI）。
7. **退出码表成败**：0 = 16 张齐全且非空白；1 = 有缺图/空白图；2 = C sim
   前置失败；3 = 登录失败；4 = 找不到 anaconda python；5 = webui 收尾拉回失败。

输出：verify/webui/shots/ref/p00.png .. p15.png（1280x800）。
空白判据：文件 <15KB，或 QImage 网格取样全同色（QSS 没生效/整页空白的指纹）。
"""

import json
import os
import re
import subprocess
import sys
import time
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(os.path.dirname(HERE))          # F:/Code/CMake/XinYueC
CLIENT_PY_DIR = os.path.join(REPO_ROOT, "HighSpeedFilter", "client_py")
BIN_DIR = os.path.join(REPO_ROOT, "bin")
WEBUI_DIR = os.path.join(REPO_ROOT, "HighSpeedFilter", "webui_py")

#: anaconda 解释器（PyQt5 所在）。
ANACONDA_PY = r"D:/anaconda3/python.exe"
HOST, C_PORT = "127.0.0.1", 6100
#: 网页端口**运行期解析**（不写死一个数）——见 resolve_web_port()。
#: 候选顺序：任务书缺省 8080 在前、8085 在后。2026-11-13 实测本机 8080 上
#: 跑着**别人的**常驻程序（F:\直滤\read_analog_gui.py），hsfweb 实际听
#: 8085 ⇒ 写死端口迟早打错人，故一律运行期解析。
WEB_PORT_CANDIDATES = (8080, 8085)
#: [2026-11-13 三页合并后] 参考客户端只有 16 页（原「悬浮窗设置(16) /
#: 窗口设置(17)」并入「本地设置(13)」）⇒ 与网页端同为 16 页。
PAGE_COUNT = 16
#: webui 拉回前的等待（C 服务端回收旧 root 会话的静默超时窗口）。
WEBUI_RESTART_DELAY_S = 8
SHOT_DIR = os.path.join(HERE, "shots", "ref")


# ============================================================================
#                          stdlib 探活 / 进程编排
# ============================================================================

def tcp_open(host, port, timeout=1.5):
    """TCP 探活：能连 = 有服务在听。"""
    import socket
    try:
        sock = socket.create_connection((host, port), timeout=timeout)
        sock.close()
        return True
    except OSError:
        return False


def wait_tcp(host, port, timeout_s, what):
    """等服务端口开：起进程后轮询，超时报败。"""
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        if tcp_open(host, port):
            return True
        time.sleep(0.5)
    print("[ref_sweep] 等待 %s (%s:%d) 超时" % (what, host, port),
          file=sys.stderr)
    return False


def ensure_c_sim():
    """C 模拟服务端（bin/HsfServer.exe --sim，听 6100）：在跑直接用，不在才起。"""
    if tcp_open(HOST, C_PORT):
        print("[ref_sweep] C sim 已在跑（127.0.0.1:%d），直接复用" % C_PORT, flush=True)
        return True
    print("[ref_sweep] C sim 不在，后台拉起 bin/HsfServer.exe --sim …", flush=True)
    exe = os.path.join(BIN_DIR, "HsfServer.exe")
    if not os.path.isfile(exe):
        print("[ref_sweep] 找不到 %s" % exe, file=sys.stderr)
        return False
    # cwd 必须在 bin/（服务端按相对路径找 cfg/）；stdout 丢弃防管道堵死。
    with open(os.devnull, "wb") as devnull:
        subprocess.Popen([exe, "--sim"], cwd=BIN_DIR,
                         stdout=devnull, stderr=devnull)
    if not wait_tcp(HOST, C_PORT, 40, "C sim"):
        return False
    # 端口开了再给 sim 一拍把虚拟设备/历史库装好。
    time.sleep(3)
    print("[ref_sweep] C sim 已就绪", flush=True)
    return True


def _run_out(cmd):
    """跑一条 Windows 命令拿 stdout 文本。

    [不用 text=True] netstat/tasklist 输出是系统码页（本机 GBK），text=True
    按 utf-8 解码会 UnicodeDecodeError 且 stdout 变 None（2026-11-13 实测）。
    提取 PID/映像名只需要 ASCII 数字，errors="replace" 足够。
    """
    out = subprocess.run(cmd, capture_output=True).stdout
    return (out or b"").decode("utf-8", "replace")


# ============================================================================
#            hsfweb 定位：按**命令行 + 进程名**认进程 + 运行期解析网页端口
# ============================================================================
#
# [为什么必须按命令行认人，而不是「监听 8080 且映像名含 python」]
# 2026-11-13 实测本机：8080 上跑着 F:\直滤\read_analog_gui.py（另一个
# 常驻程序，映像名也是 python.exe），hsfweb 实际听 8085。按端口+映像名杀
# ⇒ 会把用户的程序误杀。hsfweb 的命令行必带 `-m hsfweb`，据此认人准又安全。
#
# [本机同时跑着好几个 hsfweb] 2026-11-13 实测：8085(上游 6100)、8086
# (上游 6110)、8094/8097/8098 等实例并存（别的阶段的隔离环境）⇒ **只停
# 目标端口那一个**，别把人家的隔离环境一起端了。

def _powershell_procs(pattern):
    """列出命令行匹配 pattern 的进程：返回 [(pid, name, cmdline)]。

    [输出编码] 强制 [Console]::OutputEncoding=UTF8 ⇒ stdout 是 UTF-8，
    不受本机 GBK 代码页影响。
    """
    script = (
        "[Console]::OutputEncoding=[System.Text.Encoding]::UTF8; "
        "Get-CimInstance Win32_Process | "
        "Where-Object { $_.CommandLine -and $_.CommandLine -match '%s' } | "
        "ForEach-Object { $_.ProcessId.ToString() + '|' + $_.Name + '|' + "
        "$_.CommandLine }"
    ) % pattern
    out = subprocess.run(["powershell", "-NoProfile", "-Command", script],
                         capture_output=True).stdout or b""
    text = out.decode("utf-8", "replace")
    rows = []
    for line in text.splitlines():
        line = line.strip()
        if line.count("|") < 2:
            continue
        pid, _, rest = line.partition("|")
        name, _, cmd = rest.partition("|")
        if pid.strip().isdigit():
            rows.append((int(pid.strip()), name.strip(), cmd.strip()))
    return rows


def hsfweb_procs():
    """在跑的 hsfweb **python** 进程：[(pid, cmdline)]。

    [必须同时看进程名] 只按命令行含 'hsfweb' 会命中两类噪声：
    ① 启动它的 bash 包装进程（bash -c "... python -m hsfweb ..."）；
    ② 本枚举命令自己的 powershell（命令行里就写着匹配串）。
    ⇒ 进程名 python*.exe + 命令行含 `-m hsfweb` 才是真的 webui。
    """
    out = []
    for pid, name, cmd in _powershell_procs(r"-m\s+hsfweb"):
        if not name.lower().startswith("python"):
            continue
        out.append((pid, cmd))
    return out


def pids_listening_on(port):
    """netstat 找出 LISTEN 在 port 上的 PID 集合（Windows netstat -ano）。"""
    out = _run_out(["netstat", "-ano", "-p", "tcp"])
    pids = set()
    for line in out.splitlines():
        parts = line.split()
        # TCP    0.0.0.0:8080    0.0.0.0:0    LISTENING    12345
        if len(parts) >= 5 and parts[3].upper() == "LISTENING" \
                and parts[1].endswith(":%d" % port):
            pids.add(parts[4])
    return pids


def web_port_of_cmdline(cmdline):
    """从命令行里取 --web-port N（取不到回 None）。"""
    m = re.search(r"--web-port[= ](\d+)", cmdline)
    return int(m.group(1)) if m else None


def upstream_port_of_cmdline(cmdline):
    """从命令行里取 --port N（**上游** XLink 端口；取不到回 None）。

    [为什么要区分] hsfweb 有两个端口：--web-port 是网页端口，--port 是
    上游服务端端口。本机同时跑着好几个 hsfweb（8085→上游 6100、8086→上游
    6110…），只有「上游 = 6100」那个才是本任务要截的网页服务。
    """
    m = re.search(r"(?:^|\s)--port[= ](\d+)", cmdline)
    return int(m.group(1)) if m else None


def http_get_json(url, timeout=5.0):
    """GET 一个 JSON（失败回 None，不抛）。"""
    try:
        with urllib.request.urlopen(url, timeout=timeout) as resp:
            return json.loads(resp.read().decode("utf-8"))
    except Exception:                                    # noqa: BLE001
        return None


def is_hsfweb_port(port):
    """该端口上是不是 hsfweb：GET /api/ping 自报 server=hsf-webui。"""
    doc = http_get_json("http://%s:%d/api/ping" % (HOST, port))
    return bool(doc) and doc.get("server") == "hsf-webui"


def port_is_free(port, host="0.0.0.0"):
    """端口能不能绑（能不能拿来起 webui）。"""
    import socket
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        sock.bind((host, port))
        return True
    except OSError:
        return False
    finally:
        sock.close()


def resolve_web_port(explicit=None):
    """解析网页端口：显式参数 → 环境变量 HSF_WEB_PORT → 在跑的 hsfweb
    的 --web-port → 候选端口探活（/api/ping 自报 hsf-webui）。都没有回 None。

    [为什么不能写死一个端口] 本机 8080 被别人的程序占着、hsfweb 实际听
    8085；网页端口还会随会话主人的部署指令变（--web-port）。写死迟早打错。
    """
    if explicit:
        return int(explicit)
    raw = os.environ.get("HSF_WEB_PORT", "").strip()
    if raw.isdigit():
        return int(raw)
    live = []            # 所有活着的 hsfweb 网页端口
    same_up = []         # 其中「上游 = 本任务的 C sim 端口」的那些
    for _pid, cmd in hsfweb_procs():
        port = web_port_of_cmdline(cmd)
        if not port or not is_hsfweb_port(port):
            continue
        live.append(port)
        if upstream_port_of_cmdline(cmd) == C_PORT:
            same_up.append(port)
    # ① 任务书缺省的 8080 上确实是 hsfweb ⇒ 就用它（口径与任务书一致）。
    if WEB_PORT_CANDIDATES[0] in live:
        return WEB_PORT_CANDIDATES[0]
    # ② 上游 = 本任务的 C sim 的那些（取**最小**端口 ⇒ 结果确定，不受
    #    进程枚举顺序影响；本机并存 8085/8094/8100 三个同上游实例）。
    if same_up:
        return min(same_up)
    # ③ 任何活着的 hsfweb（同样取最小端口，保证确定性）。
    if live:
        return min(live)
    return None


def stop_webui(port):
    """停**指定端口上那个**网页服务器（root 会话独占）。

    只杀「监听 port 且进程名 python 且命令行含 -m hsfweb」的那一个：
    本机 8080 上是别人的常驻程序（read_analog_gui.py），别处还有别的
    阶段的隔离 webui（8086/8094/8097/8098），都不能碰。
    """
    pids = pids_listening_on(port)
    if not pids:
        print("[ref_sweep] 端口 %d 上没有监听者（无需停）" % port, flush=True)
        return
    targets = [(pid, cmd) for pid, cmd in hsfweb_procs()
               if str(pid) in pids]
    if not targets:
        print("[ref_sweep] 端口 %d 的监听者不是 hsfweb（不碰它）" % port,
              file=sys.stderr)
        for pid in pids:
            print("[ref_sweep]   监听者 pid=%s（命令行不是 -m hsfweb）" % pid,
                  file=sys.stderr)
        return
    for pid, cmd in targets:
        print("[ref_sweep] 停 webui：pid=%d cmd=%s" % (pid, cmd[:140]),
              flush=True)
        subprocess.run(["taskkill", "/F", "/T", "/PID", str(pid)],
                       capture_output=True,
                       env=dict(os.environ, MSYS_NO_PATHCONV="1"))
    # 等端口真关（taskkill 是异步通知，立即探活可能还通）。
    deadline = time.time() + 15
    while time.time() < deadline and pids_listening_on(port):
        time.sleep(0.4)
    print("[ref_sweep] 端口 %d 上的 webui 已停" % port, flush=True)
    # 旧 root 会话要等服务端按静默超时回收，稍候再让 Py 客户端去登录。
    time.sleep(2)


def start_webui(preferred_port=None):
    """把网页服务器拉回常驻（python -m hsfweb --web-port <port>）。

    [为什么必须显式 --web-port] hsfweb 缺省听 8080；8080 被占时它会
    「端口被占，改听随机端口」（hsfweb/__main__.py 的端口回退日志）⇒ 拉回
    的服务落在随机端口上，后续 web 截图脚本再也找不到它 ⇒ 常驻服务失联。

    [为什么要先问"活了吗"] 本机有别的阶段在并行管 webui 实例（实测同时
    跑着 8085/8086/8090/8094/8097/8098/8100 好几个）：刚停掉 8085 那个，
    别的阶段可能几秒内就把它拉回来了 ⇒ 此时**不要重复起**，更不要因为
    "端口被占"就报失败（2026-11-13 实跑撞到：拉回时报「候选端口全被占」，
    其实端口上早就是活的 hsfweb）。
    """
    port = preferred_port or resolve_web_port()
    if not port:
        port = WEB_PORT_CANDIDATES[0]
    if is_hsfweb_port(port):
        print("[ref_sweep] 端口 %d 上已有活的 hsfweb（不重复起）" % port,
              flush=True)
        return True
    if not port_is_free(port):
        for cand in WEB_PORT_CANDIDATES:
            if port_is_free(cand):
                port = cand
                break
        else:
            print("[ref_sweep] 候选网页端口 %s 全被占（且不是 hsfweb），"
                  "webui 拉回失败" % (WEB_PORT_CANDIDATES,), file=sys.stderr)
            return False
    python = os.environ.get("PYTHON", "") or sys.executable
    print("[ref_sweep] 拉回 webui（%s -m hsfweb --web-port %d, cwd=%s）…"
          % (python, port, WEBUI_DIR), flush=True)
    log_path = os.path.join(HERE, "webui_restart.log")
    # stdout/stderr 落日志：hsfweb 端口被占时会改听随机端口并把真端口打进
    # 日志（"网页端口 N 被占，改听 M" / "网页服务器就绪：…:M/"）——只看
    # 退出码看不到它挪去了哪。
    log = open(log_path, "ab")
    try:
        log.flush()
        log_mark = os.path.getsize(log_path)   # 只解析本次追加的尾巴
        subprocess.Popen([python, "-m", "hsfweb",
                          "--server", HOST, "--port", str(C_PORT),
                          "--web-port", str(port)],
                         cwd=WEBUI_DIR, stdout=log, stderr=log)
    finally:
        log.close()
    deadline = time.time() + 30
    while time.time() < deadline:
        if is_hsfweb_port(port):
            print("[ref_sweep] webui 已拉回并就绪（http://%s:%d/）"
                  % (HOST, port), flush=True)
            return True
        time.sleep(0.5)
    moved = _webui_port_from_log(log_path, log_mark)
    if moved:
        print("[ref_sweep] webui 改听了 %d（日志 %s）" % (moved, log_path),
              flush=True)
        return True
    print("[ref_sweep] webui 拉回失败（端口 %d 无应答，日志 %s）"
          % (port, log_path), file=sys.stderr)
    return False


def _webui_port_from_log(log_path, mark=0):
    """从 webui 日志里读出它实际监听的端口（被占改听时用）。

    mark = 本次启动前日志的长度 ⇒ 只看新追加的尾巴，不被**上一轮**的
    旧记录误导。
    """
    try:
        with open(log_path, "rb") as fh:
            fh.seek(mark)
            text = fh.read().decode("utf-8", "replace")
    except OSError:
        return None
    hits = re.findall(r"改听\s+(\d+)", text)
    if hits:
        return int(hits[-1])
    hits = re.findall(r"就绪：http://[0-9.]+:(\d+)/", text)
    if hits:
        return int(hits[-1])
    return None


def clear_old_shots():
    """清掉上次跑留下的 pNN.png——陈旧图会让「缺图」判据假绿。"""
    if not os.path.isdir(SHOT_DIR):
        os.makedirs(SHOT_DIR, exist_ok=True)
        return
    removed = 0
    for name in os.listdir(SHOT_DIR):
        if name.startswith("p") and name.endswith(".png"):
            try:
                os.remove(os.path.join(SHOT_DIR, name))
                removed += 1
            except OSError:
                pass
    if removed:
        print("[ref_sweep] 清掉旧图 %d 张" % removed, flush=True)


def check_outputs():
    """编排器侧复检：16 张齐全且每张 ≥15KB（空白/截断的第一道闸）。"""
    missing = []
    for i in range(PAGE_COUNT):
        path = os.path.join(SHOT_DIR, "p%02d.png" % i)
        if not os.path.isfile(path) or os.path.getsize(path) < 15 * 1024:
            missing.append("p%02d" % i)
    return missing


# ============================================================================
#                          编排器入口
# ============================================================================

def orchestrate():
    if not os.path.isfile(ANACONDA_PY):
        print("[ref_sweep] 找不到 %s（PyQt5 只装在 anaconda）" % ANACONDA_PY,
              file=sys.stderr)
        return 4
    if not ensure_c_sim():
        return 2
    # 先记住 webui 现在听哪个端口——停完再原端口拉回（别把常驻服务挪走）。
    web_port = resolve_web_port()
    print("[ref_sweep] 网页端口解析：%s" % (web_port if web_port else "未找到"),
          flush=True)
    if web_port:
        stop_webui(web_port)   # root 会话互斥：webui 的上游主链路也用 root
    else:
        print("[ref_sweep] 没有在跑的 webui，跳过停止", flush=True)
    clear_old_shots()
    code = 0
    try:
        print("[ref_sweep] 起 Qt 截图子进程（%s）…" % ANACONDA_PY, flush=True)
        proc = subprocess.run([ANACONDA_PY, os.path.abspath(__file__), "--qt"])
        code = proc.returncode
    finally:
        # 收尾留常驻服务在跑：先等 8 秒（C 服务端回收 Py 客户端旧 root
        # 会话的静默超时窗口，立即拉回会被「该账户已在线」瞬时拒绝）。
        print("[ref_sweep] 等 %d 秒让 C 服务端回收旧 root 会话…"
              % WEBUI_RESTART_DELAY_S)
        time.sleep(WEBUI_RESTART_DELAY_S)
        if not start_webui(web_port):
            print("[ref_sweep] webui 拉回失败（后续 web 截图须手动起）",
                  file=sys.stderr)
            code = code or 5
    if code == 0:
        missing = check_outputs()
        if missing:
            print("[ref_sweep] 复检缺图/过小：%s" % ", ".join(missing),
                  file=sys.stderr)
            code = 1
    print("[ref_sweep] 完成，退出码 %d（输出 %s）" % (code, SHOT_DIR), flush=True)
    return code


# ============================================================================
#                     Qt 截图子进程（--qt，须 anaconda python）
# ============================================================================

def pump_events(app, ms):
    """嵌套事件循环跑满 ms：QSS polish / 订阅推送 / 定时器全走完。"""
    from PyQt5.QtCore import QEventLoop, QTimer
    loop = QEventLoop()
    QTimer.singleShot(ms, loop.quit)
    loop.exec_()


def looks_blank(pix):
    """空白指纹：1280x800 网格取样全同色（QSS 没挂上/整页无内容的判据）。"""
    img = pix.toImage()
    w, h = img.width(), img.height()
    if w <= 0 or h <= 0:
        return True
    colors = set()
    for gx in range(8):
        for gy in range(8):
            colors.add(img.pixel(gx * (w - 1) // 7, gy * (h - 1) // 7))
    return len(colors) <= 1


def run_qt_sweep():
    """真窗口扫 16 页（单进程单会话）。必须 D:/anaconda3/python.exe 跑。"""
    # ---- 环境闸：offscreen 在本机无字体整页空白，一律钉回真窗口 ----
    if os.environ.get("QT_QPA_PLATFORM", "").lower() == "offscreen":
        print("[ref_sweep][qt] 拒绝 QT_QPA_PLATFORM=offscreen（本机无字体，"
              "整页空白），改回 windows", file=sys.stderr)
    os.environ["QT_QPA_PLATFORM"] = "windows"
    # 界面缩放/本机设置一律走缺省：设置重定向到临时 INI（不动用户 INI，
    # 也不读用户存过的 winSize/uiScalePct——参考截图必须可复现）。
    import tempfile
    tmp_dir = tempfile.mkdtemp(prefix="ref_sweep_cfg_")
    os.environ["HSF_CLIENT_SETTINGS_INI"] = os.path.join(tmp_dir,
                                                         "sweep_settings.ini")
    os.environ.pop("HSF_UI_SCALE", None)     # 缩放固定走缺省（1.5）
    sys.path.insert(0, CLIENT_PY_DIR)

    import time
    from PyQt5.QtCore import Qt, QTimer
    from PyQt5.QtWidgets import QApplication

    QApplication.setAttribute(Qt.AA_EnableHighDpiScaling, False)
    app = QApplication(sys.argv[:1])
    app.setApplicationName("HsfClientPy")

    # 与 __main__.main() 同款主题装配：install（QSS）→ apply_font（全局字号）。
    from hsfsrv_client import theme
    theme.install(app)
    family = theme.apply_font(app, size=int(round(10 * theme.ui_scale())))
    print("[ref_sweep][qt] 字体：%s（缩放 %.2fx）" % (family or "(系统缺省)",
                                                     theme.ui_scale()),
          file=sys.stderr)

    from hsfsrv_client import settings as ui_settings
    from hsfsrv_client.bridge import LinkBridge
    from hsfsrv_client.main_window import MainWindow
    from hsfsrv_client.pages_other import apply_overlay

    bridge = LinkBridge()
    win = MainWindow(bridge)
    win.resize(1280, 800)
    win.setMinimumSize(640, 420)
    win.show()
    win.resize(1280, 800)        # show 后再钉一次（__main__ 同款）
    apply_overlay(win, ui_settings)

    # ---- 登录（重试：C 服务端可能在回收旧 root 会话的窗口期瞬时拒绝）----
    ok, err = False, ""
    for attempt in range(5):
        ok, err = bridge.connect_to(HOST, C_PORT, "root", "root")
        if ok:
            break
        print("[ref_sweep][qt] 登录失败（第 %d 次）：%s" % (attempt + 1, err),
              file=sys.stderr)
        pump_events(app, 3000)
    if not ok:
        print("[ref_sweep][qt] 登录最终失败：%s" % err, file=sys.stderr)
        _qt_teardown(app, win, bridge, tmp_dir)
        return 3

    win.status_bar.set_connection("已连接", ok=True)
    win._sync_account()
    pump_events(app, 2000)       # 首帧推送 + 首页订阅到位
    print("[ref_sweep][qt] 已登录 root，开始扫 %d 页 → %s" % (PAGE_COUNT,
                                                             SHOT_DIR))

    failures = []
    for i in range(PAGE_COUNT):
        win.goto_page(i)
        pump_events(app, 1200)   # ≥1200ms：QSS polish + 页面订阅首帧推送
        path = os.path.join(SHOT_DIR, "p%02d.png" % i)
        pix = win.grab()
        saved = pix.save(path)
        size = os.path.getsize(path) if saved and os.path.isfile(path) else 0
        blank = (not saved) or size < 15 * 1024 or looks_blank(pix)
        tag = "p%02d" % i
        if blank:
            failures.append(tag)
            print("[ref_sweep][qt] %s 空白/未落盘（%s bytes）" % (tag, size),
                  file=sys.stderr)
        else:
            print("[ref_sweep][qt] %s ok %dx%d %d bytes"
                  % (tag, pix.width(), pix.height(), size))
        pump_events(app, 200)    # 让切页的定时器/推送消化掉再走下一页

    _qt_teardown(app, win, bridge, tmp_dir)
    if failures:
        print("[ref_sweep][qt] 空白/缺图：%s" % ", ".join(failures),
              file=sys.stderr)
        return 1
    return 0


def _qt_teardown(app, win, bridge, tmp_dir):
    """__main__._teardown 同款顺序：断桥 → 关窗 → 泵事件。

    差别：不写 winSize/fullscreen 到 INI（截图工具不改用户本机设置；
    本例 INI 本就是重定向的临时文件）。临时 INI 目录顺手清掉。
    """
    from PyQt5.QtCore import QTimer
    try:
        bridge.disconnect_from()
    except Exception:                                    # noqa: BLE001
        pass
    win.close()
    QTimer.singleShot(0, app.quit)
    for _ in range(50):
        app.processEvents()
        time.sleep(0.02)
    import shutil
    shutil.rmtree(tmp_dir, ignore_errors=True)


def main(argv):
    if "--qt" in argv:
        return run_qt_sweep()
    return orchestrate()


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
