# -*- coding: utf-8 -*-
"""网页端自己的**独立进程启动器**（启动方式的**单一事实源**）。

[为什么要有这一层] 谁来拼 ``python -m hsfweb …`` 这条命令行、谁管 pid 与
日志文件，曾经**各写一份**：桌面客户端自己拼一套、C 端 ``HsfWebServer.c``
自己拼一套 ⇒ 参数一改就漂（端口默认值、``--bind``、日志路径对不上）。
⇒ 收口到这里：**网页端自己知道怎么把自己起起来**，调用方只给参数。

[口径] 网页端**只有独立进程一种形态**（2026-11-17 用户定案「不要内嵌，
客户端拉起也是独立的进程」）：
  · 客户端拉起它 ⇒ 走本模块 :func:`start_process`；
  · 它自己独立跑 ⇒ 走 ``python -m hsfweb``（同一份装配逻辑，见
    :func:`hsfweb.__main__.amain`）；
  · 内嵌线程形态（:mod:`hsfweb.embed`）**降级为遗留**，不再是任何产品路径。

[为什么 pid 要记账] 独立进程形态下父进程被强杀 ⇒ 子进程变孤儿、占着端口
⇒ 下次"拉不起来"（端口被占）。⇒ 把 pid 落到 pid 文件 ⇒ 下次能**认领并
清理**上次残留（只认领自己记着的那个，不动别人的）。

[跨平台探活的坑] **Windows 上 ``os.kill(pid, 0)`` 会直接终止该进程**
（Python 把它映射成 TerminateProcess，signal 值被忽略）⇒ 拿它"探活"
等于**误杀**（pid 可能已被系统复用给别的进程）⇒ Windows 走 ctypes 的
``OpenProcess`` + ``GetExitCodeProcess``，POSIX 才用 ``os.kill(pid, 0)``。
"""

import json
import os
import subprocess
import sys

__all__ = [
    "DEFAULT_WEB_PORT", "DEFAULT_BIND", "LOG_NAME", "ERR_NAME",
    "PID_FILE_NAME", "build_argv", "start_process", "stop_process",
    "pid_alive", "pid_file_path", "write_pid", "clear_pid", "read_pid",
    "stale_pid", "stop_stale",
]

#: 网页端口缺省（**不是 8080**：本机 8080 常被别的常驻程序占着）。
DEFAULT_WEB_PORT = 8085
#: 监听地址缺省（0.0.0.0 = 局域网可访问；网页端自己的缺省）。
DEFAULT_BIND = "0.0.0.0"
#: 独立进程的日志文件名（stdout / stderr **两个文件**）。
LOG_NAME = "hsf_web.log"
ERR_NAME = "hsf_web_err.log"
#: pid 记账文件名。
PID_FILE_NAME = "hsf_web.pid"


# ==================================================================
#                       命令行（唯一事实源）
# ==================================================================

def build_argv(server="127.0.0.1", port=6100, web_port=DEFAULT_WEB_PORT,
               bind=DEFAULT_BIND, user="root", password="root",
               static="", data_dir="", python=None):
    """拼出独立进程的命令行（**唯一事实源**：参数与 ``__main__`` 的 argparse
    一一对应；改参数只改这里 + argparse）。

    :returns: list[str]（可直接交给 :func:`subprocess.Popen`）。
    """
    exe = python or sys.executable
    argv = [exe, "-u", "-m", "hsfweb",
            "--server", str(server),
            "--port", str(int(port)),
            "--web-port", str(int(web_port)),
            "--bind", str(bind),
            "--user", str(user),
            "--password", str(password)]
    if static:
        argv += ["--static", str(static)]
    if data_dir:
        argv += ["--data-dir", str(data_dir)]
    return argv


# ==================================================================
#                       起 / 停
# ==================================================================

def _open_logs(log_dir):
    """开日志文件（stdout / stderr **两个文件**）。"""
    if log_dir:
        try:
            os.makedirs(log_dir, exist_ok=True)
        except OSError:
            pass
    out_path = os.path.join(log_dir, LOG_NAME) if log_dir else LOG_NAME
    err_path = os.path.join(log_dir, ERR_NAME) if log_dir else ERR_NAME
    out_fh = open(out_path, "ab", buffering=0)
    err_fh = open(err_path, "ab", buffering=0)
    return out_fh, err_fh, out_path, err_path


def start_process(server="127.0.0.1", port=6100, web_port=DEFAULT_WEB_PORT,
                  bind=DEFAULT_BIND, user="root", password="root",
                  static="", data_dir="", log_dir=None, python=None,
                  package_dir=None, env=None):
    """以**独立子进程**起网页端。

    :param package_dir: 网页端包目录（``hsfweb`` 的上一级）—— 子进程的
        cwd 与 PYTHONPATH 都按它来（保证 ``-m hsfweb`` 能 import 到）。
    :param log_dir: 日志目录（stdout/stderr 两个文件）；None = 当前目录。
    :returns: (proc, log_paths) —— proc 是 :class:`subprocess.Popen`。
    """
    cwd = package_dir or os.path.dirname(os.path.dirname(
        os.path.abspath(__file__)))
    child_env = dict(os.environ if env is None else env)
    child_env["PYTHONPATH"] = (cwd + os.pathsep
                               + child_env.get("PYTHONPATH", "")
                               ).strip(os.pathsep)
    out_fh, err_fh, out_path, err_path = _open_logs(log_dir)
    argv = build_argv(server=server, port=port, web_port=web_port,
                      bind=bind, user=user, password=password,
                      static=static, data_dir=data_dir, python=python)
    try:
        proc = subprocess.Popen(
            argv, cwd=cwd, env=child_env,
            stdout=out_fh, stderr=err_fh, stdin=subprocess.DEVNULL)
    except Exception:
        try:
            out_fh.close()
            err_fh.close()
        except OSError:
            pass
        raise
    return proc, (out_path, err_path)


def stop_process(proc, timeout=5.0):
    """停掉子进程（先 terminate 再 kill；幂等）。"""
    if proc is None:
        return
    try:
        if proc.poll() is None:
            proc.terminate()
            proc.wait(timeout=timeout)
    except Exception:                                   # noqa: BLE001
        try:
            proc.kill()
        except Exception:                               # noqa: BLE001
            pass


# ==================================================================
#                     pid 记账 / 残留认领
# ==================================================================

def pid_file_path(log_dir=None):
    """pid 记账文件路径（``log_dir`` 缺省 = 网页端包的同级 cfg 目录）。"""
    if log_dir:
        return os.path.join(log_dir, PID_FILE_NAME)
    here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    return os.path.join(here, "cfg", PID_FILE_NAME)


def write_pid(pid, log_dir=None, parent=None):
    """记下刚拉起的子进程 pid 与**所属客户端 pid**（供下次认领残留）。

    [为什么记 parent] 孤儿判定需要知道"属主还活着吗"：属主活着的网页端
    是那个客户端的**合法网页服务**（多客户端各有一个）⇒ 别的客户端启动时
    **不能**拿它当残留杀掉；属主死了才是真孤儿。
    """
    path = pid_file_path(log_dir)
    try:
        d = os.path.dirname(path)
        if d and not os.path.isdir(d):
            os.makedirs(d, exist_ok=True)
        rec = {"pid": int(pid),
               "parent": int(parent) if parent else 0}
        with open(path, "w", encoding="utf-8") as fh:
            json.dump(rec, fh)
    except OSError:
        pass


def clear_pid(log_dir=None):
    """清掉 pid 记账（网页端已停）。"""
    try:
        os.remove(pid_file_path(log_dir))
    except OSError:
        pass


def read_record(log_dir=None):
    """读 pid 记账（dict：pid/parent；旧格式纯数字 ⇒ parent=0；坏/无 ⇒ 空）。"""
    path = pid_file_path(log_dir)
    try:
        with open(path, "r", encoding="utf-8") as fh:
            raw = (fh.read() or "").strip()
    except OSError:
        return {}
    if not raw:
        return {}
    try:
        rec = json.loads(raw)
        if isinstance(rec, dict):
            return {"pid": int(rec.get("pid") or 0),
                    "parent": int(rec.get("parent") or 0)}
    except (ValueError, TypeError, AttributeError):
        pass
    try:
        return {"pid": int(raw), "parent": 0}
    except (ValueError, TypeError):
        return {}


def read_pid(log_dir=None):
    """读 pid 记账（读不到/坏了 ⇒ 0）。"""
    return read_record(log_dir).get("pid", 0)


def pid_alive(pid):
    """进程是否还活着（**跨平台安全的探活**；见模块头「跨平台探活的坑」）。"""
    try:
        pid = int(pid)
    except (TypeError, ValueError):
        return False
    if pid <= 0:
        return False
    if os.name == "nt":
        try:
            import ctypes
            kernel32 = ctypes.windll.kernel32
            handle = kernel32.OpenProcess(0x1000 | 0x00100000, False, pid)
            if not handle:
                return False
            try:
                code = ctypes.c_ulong()
                if not kernel32.GetExitCodeProcess(handle,
                                                  ctypes.byref(code)):
                    return False
                return code.value == 259              # STILL_ACTIVE
            finally:
                kernel32.CloseHandle(handle)
        except Exception:                               # noqa: BLE001
            return False
    try:
        os.kill(pid, 0)
        return True
    except OSError:
        return False
    except Exception:                                   # noqa: BLE001
        return True


def stale_pid(log_dir=None):
    """上次残留的网页端 pid（进程还活着才返回；否则 0）。"""
    pid = read_pid(log_dir)
    return pid if pid_alive(pid) else 0


def _proc_cmdline(pid):
    """进程命令行（Windows wmic / POSIX /proc）；取不到返回空串。"""
    if os.name == "nt":
        try:
            out = subprocess.run(
                ["wmic", "process", "where", "ProcessId=%d" % int(pid),
                 "get", "CommandLine", "/format:csv"],
                capture_output=True, timeout=20
            ).stdout.decode("utf-8", "replace")
        except Exception:                       # noqa: BLE001
            return ""
        for line in out.splitlines():
            if "hsfweb" in line:
                return line
        return ""
    try:
        with open("/proc/%d/cmdline" % int(pid), "rb") as fh:
            return fh.read().decode("utf-8", "replace").replace("\x00", " ")
    except OSError:
        return ""


def stop_stale(log_dir=None):
    """认领并停掉**上次残留**的网页端子进程（三重闸，2026-11-18 联调深检）。

    [为什么不能盲杀] pid 文件里的 pid 可能是：① 系统复用给**无关进程**的
    （杀=误伤）；② 另一个**还活着**的客户端的合法网页服务（杀=拆它台）；
    ③ 真孤儿（属主客户端已死）。⇒ 三重闸全过才杀：
      1) pid 还活着（pid_alive，Windows 走 ctypes——os.kill(pid,0) 是误杀）；
      2) 命令行含 hsfweb（身份校验；防 pid 复用误杀无关进程）；
      3) 属主客户端（记录里的 parent）**已死**（孤儿判定——属主活着说明
         那是它的合法网页服务，不动、不清记录）。
    [有意留后台的不算残留] webStopWithClient=False 时客户端正常退出会清
    pid 记录（退出代码里处理）；只有客户端**异常死亡**才留下记录 ⇒
    「有记录 + 属主死」= 真残留。
    :returns: True=确实停掉了一个残留进程。
    """
    rec = read_record(log_dir)
    pid = int(rec.get("pid") or 0)
    if not pid or not pid_alive(pid):
        clear_pid(log_dir)
        return False
    if "hsfweb" not in _proc_cmdline(pid):
        # 身份不符（pid 复用给别的进程）⇒ 只清记录，不动那个进程。
        clear_pid(log_dir)
        return False
    parent = int(rec.get("parent") or 0)
    if parent and pid_alive(parent):
        # 属主客户端还活着 ⇒ 合法网页服务，不是残留。
        return False
    try:
        os.kill(pid, 15)                   # SIGTERM（Windows=TerminateProcess）
    except Exception:                       # noqa: BLE001
        pass
    clear_pid(log_dir)
    return True
