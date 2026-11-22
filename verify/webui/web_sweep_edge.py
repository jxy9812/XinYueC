# -*- coding: utf-8 -*-
"""网页 16 页截图（verify/webui/web_sweep_edge.py，Edge headless）。

用法
====

    python verify/webui/web_sweep_edge.py     # 纯标准库，PATH 的 python 即可

职责与口径（与 ref_sweep.py 配对，截图与 Py 客户端参考图逐页对照）
====================================================================

1. **自管前置**：C sim（6100）与 webui 探活——在跑直接用，不在才
   后台拉起并等就绪。**绝不停 webui**（它就是被截图的对象）。
   **网页端口运行期解析**（不写死）：环境变量 HSF_WEB_PORT → 在跑的
   hsfweb（优先「上游 = 6100」那个）→ 候选端口探活（/api/ping 自报
   server=hsf-webui）。2026-11-13 实测本机 8080 被别人的常驻程序占着
   （F:\\直滤\\read_analog_gui.py）、hsfweb 实际听 8085 ⇒ 写死端口迟早
   打错服务；自己起 webui 时**必须显式 --web-port**（缺省 8080 被占时
   hsfweb 会改听随机端口 ⇒ 服务落在随机端口上，脚本自己都找不到它）。
2. **登录拿 token**：POST /api/login（root/root）→ JSON 的 ``token`` 字段
   （真到服务端挑战应答认证，凭据错会 401）。重试 5 次——C 服务端在回收
   旧会话的窗口期会瞬时拒绝。
3. **同会话逐页翻页截图**（2026-11-13 第 4 轮起，主路径）：
       一个 Edge 实例（--remote-debugging-port）+ **一个文档**，逐页只换
       hash（router.js 的 hashchange ⇒ 不重载文档）⇒ 状态栏消息跨页保留
       （与参考端 Py 客户端「单进程顺序走页」同口径：p10~p15 带着 p09 遗留
       的「已载入服务端历史：…」）。截图走 DevTools 的
       ``Page.captureScreenshot``（视口用 Emulation 钉死 1280x800）。
       CDP 客户端见同目录 cdp_client.py（纯标准库）。
   [为什么不能「每页 spawn 一次 Edge」] 每页新开 = 新文档 = 状态栏消息
   被重置 ⇒ p10~p15 恒显「登录成功（root，角色 2）」，与参考端差一句。
   [为什么不能「同一 profile 多进程」] Edge 共用 profile 时第二发不落盘
   （本文件下方「实操坑」第 4 条）⇒ 只能一个实例 + 协议。
   **回退路径**：CDP 采集失败时自动退回「每页一个进程」的旧口径
   （``shoot_page``），截图照出，只是状态栏文案与参考端差一句。
4. **判据**：缺图、文件 <15KB、或**不同页逐字节相同**（= authtoken 直登
   没完成拍到的登录遮罩，2026-11-13 实测指纹）都算败；16 张齐 = 退出码 0。
   另有启动器坑：msedge.exe 本体 ~0.2s 就退、真浏览器分离后落盘 ⇒
   启动器退出后要**轮询等文件出现**（等 45s），各页因此天然串行
   ——18 发并发挤一起时后页完不成直登、整轮翻车（实测）。
5. **不动常驻服务**：跑完 C sim 与 webui 原样在跑（webui 登录会话随
   6s/2h 策略自过期，无需清理）。

输出：verify/webui/shots/web/p00.png .. p15.png（1280x800）。
"""

import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(os.path.dirname(HERE))          # F:/Code/CMake/XinYueC
BIN_DIR = os.path.join(REPO_ROOT, "bin")
WEBUI_DIR = os.path.join(REPO_ROOT, "HighSpeedFilter", "webui_py")

HOST, C_PORT = "127.0.0.1", 6100
#: 网页端口**运行期解析**（不写死一个数）——见 resolve_web_port()。
#: 候选顺序：任务书缺省 8080 在前、8085 在后。2026-11-13 实测本机 8080 上
#: 跑着**别人的**常驻程序（F:\直滤\read_analog_gui.py），hsfweb 实际听
#: 8085 ⇒ 写死端口迟早打错人/打错服务。
WEB_PORT_CANDIDATES = (8080, 8085)
#: 运行期解析出来的网页端口（main() 里赋值，之后所有 URL 都用它）。
WEB_PORT = None
#: 页数（2026-11-13 三页合并后 18 → 16：原「悬浮窗设置(16)」「窗口设置(17)」
#: 已并入页 13「本地设置」，两个页号从导航树删除）。
PAGE_COUNT = 16
SHOT_DIR = os.path.join(HERE, "shots", "web")
#: 小于 15KB 的 1280x800 截图 = 白板/黑板（正常页面远大于此）。
BLANK_BYTES = 15 * 1024
#: Edge 启动器进程的等待上限（正常 ~0.2s 就退，留大余量）。
EDGE_TIMEOUT_S = 30
#: 启动器退出后等分离浏览器落盘 PNG 的上限（virtual-time-budget 8s + 余量）。
SHOT_WAIT_S = 45

_EDGE_CANDIDATES = [
    r"C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe",
    r"C:/Program Files/Microsoft/Edge/Application/msedge.exe",
]

#: 本轮建过的临时 Edge profile 目录（收尾统一兜底清理）。
_TEMP_PROFILES = []


def find_edge():
    for path in _EDGE_CANDIDATES:
        if os.path.isfile(path):
            return path
    return None


def tcp_open(host, port, timeout=1.5):
    import socket
    try:
        sock = socket.create_connection((host, port), timeout=timeout)
        sock.close()
        return True
    except OSError:
        return False


def wait_tcp(host, port, timeout_s, what):
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        if tcp_open(host, port):
            return True
        time.sleep(0.5)
    print("[web_sweep] 等待 %s (%s:%d) 超时" % (what, host, port),
          file=sys.stderr)
    return False


# ============================================================================
#            hsfweb 定位：按**命令行 + 进程名**认进程 + 运行期解析网页端口
# ============================================================================
#
# [为什么不能写死端口] 2026-11-13 实测本机：8080 被别人的常驻程序占着
# （F:\直滤\read_analog_gui.py），hsfweb 实际听 8085；且本机同时跑着好几
# 个 hsfweb（8085→上游 6100、8086→上游 6110、8094/8097/8098/8100…，是别的
# 阶段的隔离环境）。写死端口 = 迟早打到别人的服务或找不到本任务的。
#
# [认进程要同时看进程名] 只按命令行含 'hsfweb' 会命中启动它的 bash 包装
# 进程和本枚举命令自己的 powershell ⇒ 认进程名 python*.exe + 命令行含
# `-m hsfweb` 才是真的 webui。

def _powershell_procs(pattern):
    """列出命令行匹配 pattern（正则）的进程：返回 [(pid, name, cmdline)]。"""
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


def _edge_pids_for_profile(profile):
    """正在占用这个临时 profile 的 Edge 进程 PID 列表。

    [为什么要杀] headless 浏览器是**分离出去的子进程**，启动器退出后它
    还在跑、写完 PNG 才慢慢退出 ⇒ 直接 rmtree 会因文件被占用而失败
    （ignore_errors=True 会把失败吞掉 ⇒ 每页留 13MB 临时目录，16 页
    ≈ 208MB 垃圾）。先按 profile 路径认出这些进程（用户自己的 Edge 用
    默认 profile，不含我们的临时路径，不会被误杀）再杀整树。
    """
    script = (
        "[Console]::OutputEncoding=[System.Text.Encoding]::UTF8; "
        "Get-CimInstance Win32_Process | "
        "Where-Object { $_.Name -eq 'msedge.exe' -and $_.CommandLine -and "
        "$_.CommandLine.Contains('%s') } | "
        "ForEach-Object { $_.ProcessId.ToString() }"
    ) % profile
    out = subprocess.run(["powershell", "-NoProfile", "-Command", script],
                         capture_output=True).stdout or b""
    pids = []
    for line in (out or b"").decode("utf-8", "replace").splitlines():
        line = line.strip()
        if line.isdigit():
            pids.append(int(line))
    return pids


def cleanup_profile(profile):
    """删掉一页的临时 Edge profile：先杀占用它的 Edge 进程，再 rmtree 重试。"""
    for pid in _edge_pids_for_profile(profile):
        subprocess.run(["taskkill", "/F", "/T", "/PID", str(pid)],
                       capture_output=True,
                       env=dict(os.environ, MSYS_NO_PATHCONV="1"))
    for _ in range(6):
        shutil.rmtree(profile, ignore_errors=True)
        if not os.path.isdir(profile):
            return True
        time.sleep(0.5)
    return not os.path.isdir(profile)


def hsfweb_procs():
    """在跑的 hsfweb **python** 进程：[(pid, cmdline)]。"""
    out = []
    for pid, name, cmd in _powershell_procs(r"-m\s+hsfweb"):
        if not name.lower().startswith("python"):
            continue
        out.append((pid, cmd))
    return out


def web_port_of_cmdline(cmdline):
    """命令行里的 --web-port N（网页端口）。"""
    m = re.search(r"--web-port[= ](\d+)", cmdline)
    return int(m.group(1)) if m else None


def upstream_port_of_cmdline(cmdline):
    """命令行里的 --port N（**上游** XLink 端口）。

    [为什么要区分] hsfweb 有两个端口：--web-port 网页端口、--port 上游
    服务端端口。本机并存的几个 webui 里，只有「上游 = 6100」那个是本任务
    要截的那个。
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
    """解析网页端口：显式 → 环境变量 HSF_WEB_PORT → 在跑的 hsfweb（优先
    「上游 = 6100」那个）→ 候选端口探活。都没有回 None。"""
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
    # ② 上游 = 本任务的 C sim 的那些（取**最小**端口 ⇒ 结果确定）。
    if same_up:
        return min(same_up)
    # ③ 任何活着的 hsfweb（同样取最小端口）。
    if live:
        return min(live)
    return None


def ensure_c_sim():
    """C 模拟服务端探活：在跑直接用，不在才后台拉起（cwd 必须在 bin/）。"""
    if tcp_open(HOST, C_PORT):
        print("[web_sweep] C sim 已在跑（127.0.0.1:%d），直接复用" % C_PORT)
        return True
    print("[web_sweep] C sim 不在，后台拉起 bin/HsfServer.exe --sim …")
    exe = os.path.join(BIN_DIR, "HsfServer.exe")
    if not os.path.isfile(exe):
        print("[web_sweep] 找不到 %s" % exe, file=sys.stderr)
        return False
    with open(os.devnull, "wb") as devnull:
        subprocess.Popen([exe, "--sim"], cwd=BIN_DIR,
                         stdout=devnull, stderr=devnull)
    if not wait_tcp(HOST, C_PORT, 40, "C sim"):
        return False
    time.sleep(3)
    print("[web_sweep] C sim 已就绪")
    return True


def ensure_webui():
    """网页服务器探活：在跑直接用，不在才后台拉起 python -m hsfweb。

    [必须显式 --web-port] hsfweb 缺省听 8080；8080 被占时它会「端口被占，
    改听随机端口」（hsfweb/__main__.py 的端口回退日志）⇒ 拉起来的服务落在
    随机端口上，截图脚本自己都找不到它。
    """
    global WEB_PORT
    WEB_PORT = resolve_web_port()
    if WEB_PORT and is_hsfweb_port(WEB_PORT):
        print("[web_sweep] webui 已在跑（127.0.0.1:%d），直接复用" % WEB_PORT)
        return True
    port = WEB_PORT or WEB_PORT_CANDIDATES[0]
    if not port_is_free(port):
        for cand in WEB_PORT_CANDIDATES:
            if port_is_free(cand):
                port = cand
                break
        else:
            print("[web_sweep] 候选网页端口 %s 全被占，webui 起不来"
                  % (WEB_PORT_CANDIDATES,), file=sys.stderr)
            return False
    WEB_PORT = port
    python = os.environ.get("PYTHON", "") or sys.executable
    print("[web_sweep] webui 不在，后台拉起 %s -m hsfweb --web-port %d"
          "（cwd=%s）…" % (python, port, WEBUI_DIR))
    log_path = os.path.join(HERE, "webui_start.log")
    # stdout/stderr 落日志：端口被占时 hsfweb 会改听随机端口并把真端口
    # 打进日志（"网页端口 N 被占，改听 M"），只看退出码看不到它挪去哪。
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
            print("[web_sweep] webui 已就绪（http://127.0.0.1:%d）" % port)
            return True
        time.sleep(0.5)
    moved = _webui_port_from_log(log_path, log_mark)
    if moved:
        WEB_PORT = moved
        print("[web_sweep] webui 改听了 %d（日志 %s）" % (moved, log_path))
        return True
    print("[web_sweep] webui 起不来（端口 %d 无应答，日志 %s）"
          % (port, log_path), file=sys.stderr)
    return False


def _webui_port_from_log(log_path, mark=0):
    """从 webui 日志里读出它实际监听的端口（被占改听时用）。

    mark = 本次启动前日志的长度 ⇒ 只看新追加的尾巴，不被上一轮旧记录误导。
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


def web_login():
    """POST /api/login（root/root）→ token。重试扛会话回收窗口期。"""
    url = "http://%s:%d/api/login" % (HOST, WEB_PORT)
    payload = json.dumps({"user": "root", "password": "root"}).encode("utf-8")
    last_err = ""
    for attempt in range(5):
        try:
            req = urllib.request.Request(
                url, data=payload, method="POST",
                headers={"Content-Type": "application/json"})
            with urllib.request.urlopen(req, timeout=15) as resp:
                doc = json.loads(resp.read().decode("utf-8"))
            token = doc.get("token", "")
            if token:
                return token
            last_err = "应答无 token：%r" % (doc.get("error"),)
        except Exception as exc:                         # noqa: BLE001
            last_err = str(exc)
        print("[web_sweep] 登录失败（第 %d 次）：%s" % (attempt + 1, last_err),
              file=sys.stderr)
        time.sleep(2)
    print("[web_sweep] 登录最终失败：%s" % last_err, file=sys.stderr)
    return ""


def web_logout(token):
    """礼貌登出（释放 web 会话；上游主链路按 webui 口径不随登出停）。"""
    try:
        req = urllib.request.Request(
            "http://%s:%d/api/logout" % (HOST, WEB_PORT), data=b"",
            method="POST", headers={"Authorization": "Bearer " + token})
        urllib.request.urlopen(req, timeout=5).read()
    except Exception:                                    # noqa: BLE001
        pass


def upstream_ready(token, timeout_s=30.0):
    """上游 XLink + 数据面就绪门：GET /api/state 等 link.ready、mode.known，
    且温湿度/流量/浊度首帧为非零活值、/api/config 可读。

    [为什么要有这道门 2026-11-13 第 2 轮对照] webui 对上游是惰性连接
    （首个浏览器登录后才连），登录后**立刻**拍 p00 会拍到「整页 --/未连接」
    的空数据帧——根因在拍摄时序不在页面（同批 p01~p12 顶栏都已有真值）。
    [第 2 轮追加] 只等 link.ready+mode.known 还不够：sim 重启后的热身窗/
    僵尸会话残局会让「压差/健康有数但流量/浊度/温湿度 0 或 --」（README
    指纹判据），拍出来整批废图。故再加数据面活值门 + config 门（页 14 自动
    配置回显依赖 configGet 成功）：
      · temphum.tempC 是数值（temphum/health 是 GLOBAL_TOPICS，登录即推，
        hub.py:87-92）；
      · flowmeter/turbidity 是**页面级订阅**（无页面打开时上游不推）⇒ 探针
        直接 POST /api/flowmeter/snapshot、/api/turbidity/snapshot——两个
        handler 会把应答落进 webui 模型（api.py _flowmeter_snapshot/
        _turbidity_snapshot 的 model.apply），随后 GET /api/state 即可读到
        fresh 活值（flow>0、浊度1 ntu>0），顺带证明上游 CMD 快照通路通；
      · GET /api/config ok=True（session 正常、上游 CMD_CONFIG_GET 通）。
    响应形状见 webui api.py _state_json 的 snap["link"]/snap["mode"] 与
    model.py TempHumState/FlowMeterState/TurbidityState.to_json。
    """
    url = "http://%s:%d/api/state" % (HOST, WEB_PORT)
    cfg_url = "http://%s:%d/api/config" % (HOST, WEB_PORT)
    fm_url = "http://%s:%d/api/flowmeter/snapshot" % (HOST, WEB_PORT)
    tb_url = "http://%s:%d/api/turbidity/snapshot" % (HOST, WEB_PORT)

    def _authed(url, method="GET"):
        return urllib.request.Request(
            url, headers={"Authorization": "Bearer " + token},
            data=b"" if method == "POST" else None, method=method)

    def _get_json(req):
        with urllib.request.urlopen(req, timeout=8) as resp:
            return json.loads(resp.read().decode("utf-8"))

    def _num(v):
        return isinstance(v, (int, float)) and not isinstance(v, bool)

    deadline = time.time() + timeout_s
    last = ""
    while time.time() < deadline:
        try:
            doc = _get_json(_authed(url))
            link = doc.get("link") or {}
            mode = doc.get("mode") or {}
            th = doc.get("temphum") or {}
            basic_ok = (link.get("ready") is True
                        and bool(mode.get("known"))
                        and _num(th.get("tempC")))
            values_ok = False
            cfg_ok = False
            if basic_ok:
                # 探针主动落模：流量/浊度快照（无页面订阅时的数据面探针）。
                _get_json(_authed(fm_url, "POST"))
                _get_json(_authed(tb_url, "POST"))
                doc = _get_json(_authed(url))
                fm = doc.get("flowmeter") or {}
                tb = doc.get("turbidity") or {}
                items = tb.get("items") or []
                turb1 = (items[0] or {}).get("ntu") if items else None
                values_ok = (fm.get("fresh") is True and _num(fm.get("flow"))
                             and fm.get("flow") > 0
                             and tb.get("fresh") is True
                             and _num(turb1) and turb1 > 0)
            if values_ok:
                cfg_ok = _get_json(_authed(cfg_url)).get("ok") is True
            if basic_ok and values_ok and cfg_ok:
                print("[web_sweep] 上游已就绪（ready/known + 温湿度/流量/浊度"
                      "活值 + config 可读）")
                return True
            last = ("link=%r mode=%r tempC=%r flow=%r/%r turb1=%r cfg=%r"
                    % (link.get("ready"), mode.get("known"), th.get("tempC"),
                       (doc.get("flowmeter") or {}).get("fresh"),
                       (doc.get("flowmeter") or {}).get("flow"),
                       (((doc.get("turbidity") or {}).get("items") or [{}])[0]
                        .get("ntu")),
                       cfg_ok))
        except Exception as exc:                         # noqa: BLE001
            last = str(exc)
        time.sleep(0.5)
    print("[web_sweep] 上游就绪门超时(%ds)：%s" % (int(timeout_s), last),
          file=sys.stderr)
    return False


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
        print("[web_sweep] 清掉旧图 %d 张" % removed)


def shoot_page(edge, token, page_index):
    """spawn 一发 Edge headless 截第 page_index 页；返回 (ok, bytes, note)。

    [启动器先退、截图后到 2026-11-13 实测] msedge.exe 本体是个启动器壳：
    ~180ms 就 exit 0，真 headless 浏览器是分离出去的子进程，PNG 落盘在
    启动器退出**之后**（实测 /api/ping 页 t+1.3s）。所以「进程退出就查
    文件」必然假败 ⇒ 退出后要**轮询等文件出现**；这也顺带把各页串行化
    （上一页落盘才拍下一页——18 发并发挤一起时，后页在 8s 预算内完不成
    直登，拍出来是登录遮罩、甚至不落盘，全部翻车实测）。
    """
    tag = "p%02d" % page_index
    out_png = os.path.join(SHOT_DIR, tag + ".png")
    profile = tempfile.mkdtemp(prefix="edge_shot_%s_" % tag)
    _TEMP_PROFILES.append(profile)
    url = ("http://%s:%d/?authtoken=%s#/page/%d"
           % (HOST, WEB_PORT, token, page_index))
    cmd = [
        edge,
        "--headless=new",
        "--disable-gpu",
        "--no-first-run",
        # [不要再传 --hide-scrollbars] 2026-11-13 对照实锤：它把网页版的
        # 竖/横滚动条全藏了 ⇒ 参照图（桌面端）明明有滚动条、网页版一张都
        # 没有，看起来像「少了一列/少了数据」，实为采集参数造成的假差异
        # （用同一份代码去掉该参数重拍，滚动条立刻出现）。CSS 侧的滚动条
        # 样式（theme.css ::-webkit-scrollbar 宽 10px/高 12px）已与桌面
        # 端一致，不要改样式来迁就采集参数。
        "--user-data-dir=" + profile,
        "--screenshot=" + out_png,
        "--window-size=1280,800",
        "--virtual-time-budget=8000",
        url,
    ]
    try:
        proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL,
                                stderr=subprocess.DEVNULL)
        try:
            proc.wait(timeout=EDGE_TIMEOUT_S)
        except subprocess.TimeoutExpired:
            # 杀整树（Edge 主进程会拉多个子进程，只杀主进程会留下残骸
            # 占着 profile 目录 ⇒ rmtree 失败 + 进程泄漏）。
            subprocess.run(["taskkill", "/F", "/T", "/PID", str(proc.pid)],
                           capture_output=True,
                           env=dict(os.environ, MSYS_NO_PATHCONV="1"))
            proc.wait()
            return False, 0, "超时(%ds)" % EDGE_TIMEOUT_S
        # 启动器已退（正常 ~0.2s）：轮询等分离的浏览器把 PNG 写出来。
        deadline = time.time() + SHOT_WAIT_S
        while time.time() < deadline and not os.path.isfile(out_png):
            time.sleep(0.3)
        if not os.path.isfile(out_png):
            return False, 0, "未落盘(启动器已退，等 %ds 无文件)" % SHOT_WAIT_S
        time.sleep(1.0)          # 落盘后留一拍：分离浏览器退出 + 写完整
        # [取大小要容忍瞬时消失 2026-11-13] 实跑抓到 isfile→getsize 之间
        # 文件被并发清扫删掉的竞态（WinError 3 路径不存在，整轮崩）——
        # 短重试，仍无就判本页失败（不炸整轮）。
        size = -1
        for _ in range(3):
            try:
                size = os.path.getsize(out_png)
                break
            except OSError:
                time.sleep(0.5)
        if size < 0:
            return False, 0, "落盘后消失(并发清扫竞态)"
        if size < BLANK_BYTES:
            return False, size, "空白(%d bytes <%dKB)" % (size, BLANK_BYTES // 1024)
        return True, size, ""
    finally:
        # 临时 profile 必须删干净：headless 浏览器是分离进程，写完 PNG 还在
        # 跑 ⇒ 直接 rmtree 会被占用文件顶掉（每页留 13MB）。见
        # cleanup_profile()。
        cleanup_profile(profile)


def shoot_pages_session(edge, token, page_indices, settle_s=4.0):
    """**同一会话**逐页翻页截图（DevTools 协议；一次浏览器实例、一个文档）。

    [为什么必须同会话] 每页新开一页 = 新文档 = 前端状态栏的消息被重置 ⇒
    截图里 p10/p11/p12 的状态栏恒是「登录成功（root，角色 2）」，而参考端
    （Py 客户端单进程顺序走页）带着 p09 遗留的「已载入服务端历史：…」。
    同会话里换 **hash** 翻页（router.js 的 hashchange）不重载文档 ⇒ 状态栏
    消息跨页保留，两端口径一致（2026-11-13 第 4 轮对照要求）。

    [实现] Edge 起一个 --remote-debugging-port 实例 → CDP 连上页面 target
    → 逐页 Runtime.evaluate 改 hash → 等页面稳定 → Page.captureScreenshot
    落盘。CDP 客户端见同目录 cdp_client.py（纯标准库）。

    :return: [(page_index, ok, size, note), ...]
    """
    import cdp_client

    profile = tempfile.mkdtemp(prefix="edge_sweep_sess_")
    _TEMP_PROFILES.append(profile)
    port = _free_debug_port()
    url0 = ("http://%s:%d/?authtoken=%s#/page/%d"
            % (HOST, WEB_PORT, token, page_indices[0]))
    cmd = [
        edge,
        "--headless=new",
        "--disable-gpu",
        "--no-first-run",
        "--user-data-dir=" + profile,
        "--remote-debugging-port=%d" % port,
        "--window-size=1280,800",
        url0,
    ]
    proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL)
    results = []
    cdp = None
    try:
        ws_url = cdp_client.wait_for_target(port, timeout_s=45.0)
        cdp = cdp_client.Cdp(ws_url, timeout=60.0)
        cdp.call("Page.enable")
        cdp.call("Runtime.enable")
        # [视口钉死 1280x800] headless=new 的窗口内高比 --window-size 少
        # 一截（实测截图 1256x708）⇒ 用 Emulation 覆写视口，保证与参考图
        # 同尺寸（1280x800），否则逐像素对照的坐标全错位。
        cdp.call("Emulation.setDeviceMetricsOverride",
                 {"width": 1280, "height": 800, "deviceScaleFactor": 1,
                  "mobile": False})
        # 首屏：等直登 + 首页挂载完成。
        _wait_page_ready(cdp, settle_s + 2.0)
        for idx in page_indices:
            if idx != page_indices[0]:
                cdp.evaluate("location.hash = '#/page/%d'" % idx)
                _wait_page_ready(cdp, settle_s)
            out_png = os.path.join(SHOT_DIR, "p%02d.png" % idx)
            try:
                data = cdp.screenshot_png()
            except Exception as exc:                # noqa: BLE001
                results.append((idx, False, 0, "截图失败：%s" % exc))
                continue
            with open(out_png, "wb") as fh:
                fh.write(data)
            size = len(data)
            if size < BLANK_BYTES:
                results.append((idx, False, size,
                                "空白(%d bytes <%dKB)"
                                % (size, BLANK_BYTES // 1024)))
            else:
                results.append((idx, True, size, ""))
    except Exception as exc:                        # noqa: BLE001
        note = "同会话采集失败：%s" % exc
        results = [(i, False, 0, note) for i in page_indices]
    finally:
        if cdp is not None:
            try:
                cdp.close()
            except Exception:                       # noqa: BLE001
                pass
        try:
            proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            subprocess.run(["taskkill", "/F", "/T", "/PID", str(proc.pid)],
                           capture_output=True,
                           env=dict(os.environ, MSYS_NO_PATHCONV="1"))
        cleanup_profile(profile)
    return results


def _wait_page_ready(cdp, timeout_s):
    """等页面内容稳定（页容器非空 + 一小段静默期）。"""
    deadline = time.time() + timeout_s
    last_len = -1
    stable = 0
    while time.time() < deadline:
        n = cdp.evaluate(
            "(document.getElementById('page-host')||{}).childElementCount")
        n = int(n) if isinstance(n, (int, float)) else -1
        if n > 0 and n == last_len:
            stable += 1
            if stable >= 2:
                return True
        else:
            stable = 0
        last_len = n
        time.sleep(0.4)
    return False


def _free_debug_port():
    """找一个空闲的本地端口给 DevTools 用。"""
    import socket as _socket
    s = _socket.socket()
    try:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]
    finally:
        s.close()


def main():
    edge = find_edge()
    if edge is None:
        print("[web_sweep] 找不到 msedge.exe（%s）" % _EDGE_CANDIDATES,
              file=sys.stderr)
        return 4
    if not ensure_c_sim():
        return 2
    if not ensure_webui():
        return 2

    token = web_login()
    if not token:
        return 3
    if not upstream_ready(token):
        # 换个新会话再试一轮（webui 首连可能正撞上 C sim 会话回收窗口）。
        web_logout(token)
        token = web_login()
        if not token or not upstream_ready(token):
            return 5

    clear_old_shots()
    print("[web_sweep] 已登录（token 前 8 位 %s…），扫 %d 页 → %s"
          % (token[:8], PAGE_COUNT, SHOT_DIR))

    failures = []
    seen_md5 = {}            # md5 → page tag（跨页同字节 = 登录遮罩指纹）

    def check_shot(tag, ok, size, note):
        """落盘后的一致性检查（跨页同字节 = 登录遮罩指纹）。"""
        if not ok:
            failures.append("%s(%s)" % (tag, note))
            print("[web_sweep] %s 失败：%s" % (tag, note), file=sys.stderr)
            return
        md5 = hashlib.md5(
            open(os.path.join(SHOT_DIR, tag + ".png"), "rb").read()).hexdigest()
        dup = seen_md5.get(md5)
        seen_md5[md5] = tag
        if dup is not None:
            failures.append("%s(与 %s 逐字节相同——不同页面不可能同图，"
                            "这是 authtoken 直登没完成拍到的登录遮罩)"
                            % (tag, dup))
            print("[web_sweep] %s 失败：与 %s 逐字节相同（登录遮罩）"
                  % (tag, dup), file=sys.stderr)
            return
        print("[web_sweep] %s ok %d bytes" % (tag, size))

    # [同会话优先] 一次浏览器实例、一个文档，逐页换 hash ⇒ 状态栏消息跨页
    # 保留（与参考端 Py 客户端「单进程顺序走页」同口径，2026-11-13 第 4 轮
    # 对照要求）。CDP 采集失败时回退到「每页一个进程」的旧口径（截图能出，
    # 只是状态栏文案与参考端差）。
    results = shoot_pages_session(edge, token, list(range(PAGE_COUNT)))
    if results and all(ok for _i, ok, _s, _n in results):
        for idx, ok, size, note in results:
            check_shot("p%02d" % idx, ok, size, note)
    else:
        why = "; ".join("%s:%s" % ("p%02d" % i, n) for i, ok, s, n in results
                         if not ok) or "未知"
        print("[web_sweep] 同会话采集不可用（%s）——回退每页一进程口径"
              % why, file=sys.stderr)
        for i in range(PAGE_COUNT):
            ok, size, note = shoot_page(edge, token, i)
            check_shot("p%02d" % i, ok, size, note)

    web_logout(token)
    # 收尾兜底：逐页清理被占用而失败的临时 profile，这里再清一次。
    left = [d for d in _TEMP_PROFILES if os.path.isdir(d)]
    for d in left:
        cleanup_profile(d)
    still = [d for d in _TEMP_PROFILES if os.path.isdir(d)]
    if still:
        print("[web_sweep] 临时 profile 残留 %d 个（%s）"
              % (len(still), ", ".join(os.path.basename(d)
                                       for d in still[:3])),
              file=sys.stderr)
    if failures:
        print("[web_sweep] 缺图/空白：%s" % ", ".join(failures), file=sys.stderr)
        return 1
    print("[web_sweep] 完成：16 张齐全 → %s" % SHOT_DIR)
    return 0


if __name__ == "__main__":
    sys.exit(main())
