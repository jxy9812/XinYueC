# -*- coding: utf-8 -*-
"""客户端侧的「网页服务」控制：起停 + 端口可配 + **跨进程探测**。

[为什么有这一层]
    网页版（``HighSpeedFilter/webui_py/hsfweb``）原本只能手工起、端口写死
    在命令行里；桌面上「客户端和网页同开」是常态 ⇒ 起停与端口必须**在客户
    端里可配**，不能靠人记命令。

[启动方式的事实源在**网页端那边**（2026-11-17 收口）]
    客户端**不再自己拼** ``python -m hsfweb …`` 这条命令行，也不再自己管
    pid 记账/探活 ⇒ 全部走网页端包里的 :mod:`hsfweb.launcher`（网页端自己
    知道怎么把自己起起来：参数、命令行、日志、pid、残留认领）。
    ⇒ 客户端、C 端、网页端三边对"怎么起网页端"的认知只有一份。

[运行形态（2026-11-17 用户定案「不要内嵌，客户端拉起也是独立的进程」）]
    网页端**一律**以**独立子进程**跑（``python -m hsfweb …``），**不再**用
    同进程线程内嵌：
      · 内嵌形态（原缺省）已按用户口径**去掉** —— 网页端与客户端两套
        生命周期混在一个进程里，网页端想继续服务时做不到；
      · 独立进程 ⇒ 客户端只是网页端的**配置入口**，网页端可以被单独
        拉起/停止，也能在客户端退出后继续在后台服务。
    [关闭策略] 设置项 ``webStopWithClient``（缺省 **开** = 同步关闭）：
      · **开** ⇒ 客户端退出时**终止**那个子进程（网页端随之停，端口释放）；
      · **关** ⇒ 客户端退出时**不终止**它 ⇒ 网页端留在后台继续服务。
    [代价] 客户端被**强杀**（任务管理器/断电）⇒ 子进程变孤儿占着端口 ⇒
      这是独立进程形态的固有代价；设置页的「停止网页服务」是清理入口，
      且下次客户端起来能**探测到**这个残留进程并如实显示来源。

[上游是谁]
    网页端连的是**同一个 XLink 服务端**（不是客户端）：网页端自己拿 root
    主链路连上游、浏览器再各自登录。所以上游地址取客户端当前连的那台。

[找不到网页端包时]
    状态行如实说「找不到网页端包目录」，**不静默失败**（网页端可选，不该
    因为它拖住客户端其余功能）。
"""

import json
import os
import subprocess
import sys
import threading
import urllib.error
import urllib.request

#: 网页端口缺省（**不是 8080**：本机 8080 常被别的常驻程序占着）。
DEFAULT_PORT = 8085
MIN_PORT = 1024
MAX_PORT = 65535

#: 环境变量：指定网页端包目录（部署形态下网页端不在仓库布局时用）。
ENV_WEB_DIR = "HSF_WEB_DIR"

#: 环境变量：覆盖 pid 记账文件路径（回归/多实例各用各的，别互相踩）。
#: [pid 记账本体在网页端 launcher 里；这里只是把同名环境变量透过去，
#:  便于回归段把记账写到临时目录]
ENV_PID_FILE = "HSF_WEB_PID_FILE"

#: 独立进程形态的日志文件名（放客户端 cfg 目录旁，与 C 端同口径：
#: stdout 与 stderr **必须两个文件** —— 框架的文件打开不共享写，同一个
#: 文件开两次必失败 ⇒ 重定向句柄为空 ⇒ 报「Failed to start process」）。
LOG_NAME = "hsf_web.log"
ERR_NAME = "hsf_web_err.log"

#: 模块状态（单例：一个客户端只带一个网页端）。
_lock = threading.Lock()
#: **起停操作的互斥锁**（2026-11-17 实测）：`start`/`stop`/`restart` 整体
#: 串行 ⇒ 并发调用（改端口的去抖 + 用户点按钮）不会交错、不会互杀。
_op_lock = threading.RLock()
_proc = None            # subprocess.Popen 或 None（独立进程形态）
_proc_port = 0          # 独立进程形态占的网页端口（状态行用）
_proc_files = None      # 独立进程形态的 stdout/stderr 句柄（要留着别关）
_last_error = ""        # 最近一次失败原因（空 = 无）
_web_dir = ""           # 已定位的网页端包目录


def find_webui_dir():
    """定位网页端包目录（要能看到 ``hsfweb/__main__.py`` 才认）。

    候选顺序：环境变量 → 客户端同级的 ``webui_py`` → 仓库布局
    ``<repo>/HighSpeedFilter/webui_py``（客户端在 ``HighSpeedFilter/
    client_py/hsfsrv_client`` 里，往上三级就是 ``HighSpeedFilter``）。

    :returns: 目录绝对路径；找不到返回空串。
    """
    env = os.environ.get(ENV_WEB_DIR, "").strip()
    candidates = []
    if env:
        candidates.append(env)
    here = os.path.dirname(os.path.abspath(__file__))       # client_py/hsfsrv_client
    pkg = os.path.dirname(os.path.dirname(here))            # client_py
    hsfs = os.path.dirname(pkg)                             # HighSpeedFilter
    candidates.append(os.path.join(pkg, "webui_py"))        # 与客户端同目录
    candidates.append(os.path.join(hsfs, "webui_py"))        # 仓库布局
    for cand in candidates:
        # [探针改了 2026-11-17] 网页端不再内嵌 ⇒ 认的是**独立进程的入口**
        # （``python -m hsfweb``），不是 embed.py（内嵌用的）。
        probe = os.path.join(cand, "hsfweb", "__main__.py")
        if os.path.isfile(probe):
            return os.path.abspath(cand)
    return ""


# ==================================================================
#                     跨进程探测（谁在跑、是不是自己起的）
# ==================================================================

def probe(port=None, timeout=1.5):
    """探测**某个端口上**有没有网页服务在跑（**跨进程**）。

    [为什么必须跨进程] 网页服务不一定是本客户端起的：
      · 用户手工 ``python -m hsfweb --web-port 8085`` 起的；
      · 另一个客户端实例内嵌的（多开场景）；
      · 上一版客户端没退干净留下的。
    ⇒ 设置页要能回答"到底有没有在跑、是不是我认领的那个"，才能给对动作
      （没跑 ⇒ 可拉起；跑的是别人的 ⇒ 别重复拉起、也别去停它）。

    [探活口径] 网页端有无鉴权的 ``GET /api/ping`` ⇒ 自报
    ``{"ok": true, "server": "hsf-webui"}`` ⇒ 用 ``server`` 字段认人，
    免得把别的 HTTP 服务误认成网页端。

    :param port: 网页端口（None = 缺省 8085）。
    :param timeout: HTTP 超时（秒；探活要快，别让界面等）。
    :returns: dict —— ``alive``(bool) / ``server``(str) / ``url``(str) /
        ``own``(bool：是不是本客户端内嵌的那个) / ``port``(int)。
    """
    try:
        want = int(port) if port else DEFAULT_PORT
    except (TypeError, ValueError):
        want = DEFAULT_PORT
    url = "http://127.0.0.1:%d/api/ping" % want
    result = {"alive": False, "server": "", "url": "", "own": False,
              "port": want}
    try:
        with urllib.request.urlopen(url, timeout=timeout) as resp:
            raw = resp.read()
    except (urllib.error.URLError, OSError, ValueError):
        return result
    try:
        data = json.loads(raw.decode("utf-8", "replace"))
    except (ValueError, AttributeError):
        return result
    if not isinstance(data, dict):
        return result
    server = str(data.get("server") or "")
    if not data.get("ok") or server != "hsf-webui":
        return result
    result["alive"] = True
    result["server"] = server
    result["url"] = "http://127.0.0.1:%d/" % want
    # [own 的口径] 是不是**本客户端拉起**的那个（靠我们自己记的子进程
    # 判，不靠查端口占用进程的 PID ⇒ 跨平台一致、也不需要额外权限）。
    with _lock:
        result["own"] = bool(_proc) and _proc.poll() is None
    return result


def status_text(probe_result=None):
    """状态文案（给设置页状态行显示）。

    :param probe_result: ``probe()`` 的结果（None = 不探测，只看内嵌形态）。

    三种状态（**来源要说清**，否则用户分不清"我的"和"别人的"）：
      · **本客户端启动的独立进程** ⇒ 本客户端拉起的那个子进程；
      · **其它进程** ⇒ 用户手工起的 / 另一个客户端实例 ⇒ 本客户端既不该
        重复拉起、也**不该**去停它；
      · **已停止** ⇒ 可用设置里的「启动网页服务」拉起。
    """
    with _lock:
        proc = _proc
        proc_port = _proc_port
        err = _last_error
    if proc is not None and proc.poll() is None:
        # [独立进程形态] 网页端是**本客户端拉起的子进程** ⇒ 客户端退出后
        # 它继续跑（"不同步关闭"），状态行要说清是谁起的。
        url = ""
        if probe_result:
            url = probe_result.get("url") or ""
        if not url and proc_port:
            url = "http://127.0.0.1:%d/" % proc_port
        return "运行中（本客户端启动的独立进程）· %s" % (url or
                                                    "http://127.0.0.1")
    if probe_result is None:
        return err or "已停止"
    if probe_result.get("alive"):
        if probe_result.get("own"):
            return "运行中（本客户端启动的独立进程）· %s" % (
                probe_result.get("url") or "")
        return "运行中（其它进程）· %s" % (probe_result.get("url") or "")
    return err or "已停止"


def running():
    """**本客户端拉起**的网页端当前是否在跑（独立进程形态）。"""
    with _lock:
        return bool(_proc) and _proc.poll() is None


def own_process_alive():
    """本客户端拉起的网页端子进程是否还活着（页面按钮可用性用）。"""
    return running()


def alive(port=None):
    """该端口上**有没有**网页服务在跑（含其它进程起的）。"""
    return bool(probe(port).get("alive"))


def url():
    """访问地址（本客户端拉起的那个；未起则空串）。"""
    with _lock:
        proc_port = _proc_port
        proc = _proc
    if proc is not None and proc.poll() is None and proc_port:
        return "http://127.0.0.1:%d/" % proc_port
    return ""


# ==================================================================
#                          起 / 停
# ==================================================================

def _launcher():
    """网页端 launcher（启动/pid 的**单一事实源**）。"""
    web_dir = _web_dir or find_webui_dir()
    return _import_launcher(web_dir), web_dir


def _pid_file():
    """pid 记账文件路径（环境变量 > 客户端 cfg 目录）。"""
    env = os.environ.get(ENV_PID_FILE, "").strip()
    if env:
        return env
    here = os.path.dirname(os.path.abspath(__file__))
    pkg = os.path.dirname(here)                     # client_py
    return os.path.join(pkg, "cfg", "hsf_web.pid")


def _write_pid(pid):
    """记下子进程 pid 与**所属客户端 pid**（供下次认领残留；本体在 launcher）。"""
    try:
        launcher, _wd = _launcher()
        launcher.write_pid(int(pid), parent=os.getpid(),
                           log_dir=os.path.dirname(_pid_file()))
    except Exception:                               # noqa: BLE001
        pass


def _clear_pid():
    """清掉 pid 记账（网页端已停；本体在网页端 launcher）。"""
    try:
        launcher, _wd = _launcher()
        launcher.clear_pid(log_dir=os.path.dirname(_pid_file()))
    except Exception:                               # noqa: BLE001
        try:
            os.remove(_pid_file())
        except OSError:
            pass


def pid_alive(pid):
    """进程是否还活着（**跨平台安全的探活**）。

    [为什么不能用 os.kill(pid, 0)] **Windows 上 `os.kill(pid, 0)` 会直接
    终止该进程**（Python 把它映射成 TerminateProcess，signal 值被忽略）
    ⇒ 拿它"探活"等于**误杀**（而 pid 可能已被系统复用给别的进程）。
    ⇒ Windows 走 ctypes 的 OpenProcess + GetExitCodeProcess，POSIX 走
    os.kill(pid, 0)。
    """
    try:
        launcher, _wd = _launcher()
        return bool(launcher.pid_alive(pid))
    except Exception:                               # noqa: BLE001
        return False


def stale_pid():
    """上次残留的网页端 pid（进程还活着才返回；否则 0）。

    [为什么只认领自己记下的] 端口上的网页端**可能是别人的**（用户手工起的）
    ⇒ 只有 pid 文件里记着的那个才是本客户端上次留下的 ⇒ 只动它。
    """
    try:
        launcher, _wd = _launcher()
        return int(launcher.stale_pid(log_dir=os.path.dirname(_pid_file()))
                   or 0)
    except Exception:                               # noqa: BLE001
        return 0


def stop_stale():
    """清理上次残留的网页端子进程（认领 pid 文件里那个）。

    :returns: True=确实停掉了一个残留进程。
    """
    try:
        launcher, _wd = _launcher()
        return bool(launcher.stop_stale(
            log_dir=os.path.dirname(_pid_file())))
    except Exception:                   # noqa: BLE001
        return False


def _log_paths():
    """独立进程形态的日志路径（stdout / stderr **两个文件**）。"""
    here = os.path.dirname(os.path.abspath(__file__))
    pkg = os.path.dirname(here)                     # client_py
    cfg = os.path.join(pkg, "cfg")
    try:
        os.makedirs(cfg, exist_ok=True)
    except OSError:
        pass
    return (os.path.join(cfg, LOG_NAME),
            os.path.join(cfg, ERR_NAME))


def _import_launcher(web_dir):
    """导入网页端的 launcher（**启动方式的单一事实源**）。

    [为什么不是自己拼命令行] 网页端自己知道它的参数名与默认值 ⇒ 命令行、
    日志、pid 记账、残留认领都归它 ⇒ 客户端只给参数。
    """
    if web_dir and web_dir not in sys.path:
        sys.path.insert(0, web_dir)
    from hsfweb import launcher                     # noqa: PLC0415
    return launcher


def _start_detached(web_dir, up_host, up_port, web_port):
    """以**独立子进程**起网页端（客户端退出后它继续跑）。

    :returns: (ok, reason)；reason 是人话原因。
    """
    global _proc, _proc_files, _proc_port
    try:
        launcher = _import_launcher(web_dir)
    except Exception as exc:                       # noqa: BLE001
        return False, "网页端导入失败：%s" % exc
    try:
        proc, _paths = launcher.start_process(
            server=str(up_host), port=int(up_port), web_port=int(web_port),
            package_dir=web_dir, log_dir=os.path.dirname(_log_paths()[0]))
    except Exception as exc:                       # noqa: BLE001
        return False, "网页端启动失败：%s" % exc
    with _lock:
        _proc = proc
        _proc_files = None          # 日志句柄在 start_process 返回后由引用
                                    # 计数关闭（子进程已继承自己的句柄，
                                    # 父进程关不影响子进程写日志）
        _proc_port = int(web_port)
    _write_pid(proc.pid)
    # 等它把端口绑上（起失败进程会退出 ⇒ 不吊死）。
    import time
    deadline = time.time() + 10.0
    while time.time() < deadline:
        if proc.poll() is not None:
            return False, ("网页端启动失败（进程已退出，详见 %s）"
                           % _log_paths()[1])
        if probe(web_port, timeout=0.6).get("alive"):
            return True, ""
        time.sleep(0.2)
    return False, "网页端启动超时（10 秒内没绑上端口 %d）" % web_port


def _stop_detached():
    """停掉独立进程形态的网页端（幂等）。"""
    global _proc, _proc_files, _proc_port
    with _lock:
        proc = _proc
        files = _proc_files
        _proc = None
        _proc_files = None
        _proc_port = 0
    if proc is not None:
        try:
            proc.terminate()
            proc.wait(timeout=5)
        except Exception:                           # noqa: BLE001
            try:
                proc.kill()
            except Exception:                       # noqa: BLE001
                pass
    for fh in (files or ()):
        try:
            fh.close()
        except OSError:
            pass


def start(upstream_host="127.0.0.1", upstream_port=None, port=None):
    """起网页端（**独立子进程**形态；2026-11-17 用户定案「不要内嵌」）。

    :param upstream_host: 上游 XLink 服务端地址。
    :param upstream_port: 上游端口（None = 用 ports.C_SERVER_PORT 缺省；
        **不写死端口字面量**——端口的单一事实源是 ports.py，见
        tests/test_ports.py 的「不得绕过 ports.py 写死端口」纪律）。
    :param port: 网页端口（None = 用设置里的值）。
    :returns: (ok, reason)；失败时 reason 是人话原因。
    """
    global _last_error, _web_dir
    from .ports import C_SERVER_PORT
    with _op_lock:
        return _start_locked(upstream_host, upstream_port, port)


def _start_locked(upstream_host, upstream_port, port):
    """`start()` 的真正实现（**调用方已持 _op_lock**）。"""
    global _last_error, _web_dir
    from .ports import C_SERVER_PORT
    want = int(port) if port else DEFAULT_PORT
    want = max(MIN_PORT, min(MAX_PORT, want))
    up_port = int(upstream_port) if upstream_port else int(C_SERVER_PORT)
    with _lock:
        if _proc is not None and _proc.poll() is None:
            return True, ""
        web_dir = _web_dir or find_webui_dir()
        if not web_dir:
            _last_error = ("找不到网页端包目录（放到 webui_py/ 下，或设 %s）"
                           % ENV_WEB_DIR)
            return False, _last_error
        _web_dir = web_dir
    # [清残留**不在这里**做] 之前 start() 开头会 `stop_stale()` ⇒ 并发的
    # 另一个 start 刚起的进程会被误杀（pid 文件是全局的，分不清是谁的）
    # ⇒ 改成只在**客户端启动时**清一次（见 __main__._start_web_server）。
    # [端口已被别的网页端占着] 别重复拉起（起来必然 bind 失败）⇒ 如实说。
    existing = probe(want, timeout=0.8)
    if existing.get("alive"):
        _last_error = ("端口 %d 上已有网页服务在运行，"
                       "本客户端不再重复拉起" % want)
        return False, _last_error
    ok, reason = _start_detached(web_dir, upstream_host, up_port, want)
    if not ok:
        with _lock:
            _last_error = reason
        return False, reason
    with _lock:
        _last_error = ""
    return True, ""


def stop():
    """停网页端（**只停本客户端拉起的那���子进程**；幂等）。

    [为什么不动别人的] 探测到的"其它进程"网页端不是本客户端拉起来的 ⇒
    客户端退出/点「停止」都不该去杀它（可能是用户手工起的常驻服务）。
    """
    global _last_error
    with _op_lock:
        with _lock:
            _last_error = ""
        _stop_detached()
        _clear_pid()


def restart(upstream_host="127.0.0.1", upstream_port=None, port=None):
    """重启（换上游/换端口后用）。"""
    with _op_lock:
        stop()
        return start(upstream_host, upstream_port, port)
