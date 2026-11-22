# -*- coding: utf-8 -*-
"""把网页服务器**内嵌**到别的 Python 进程里（后台线程跑）。

[⚠ 遗留：产品路径已不用（2026-11-17）]
    用户定案「**不要内嵌**，客户端拉起也是独立的进程」⇒ 桌面客户端
    （Python/C）拉起网页端一律走**独立子进程**：
      · 启动方式的**单一事实源** = 本包 :mod:`hsfweb.launcher`
        （命令行 / 日志 / pid 记账 / 残留认领都在那儿）；
      · 本模块的内嵌线程形态**不再是任何产品路径**，仅保留给
        「测试/嵌入式场景」使用（例：客户端泄漏门用它验网页端自身的
        资源回收）。
    新写代码请用 :func:`hsfweb.launcher.start_process`。

为什么需要
==========

网页端原本只是个独立进程（``python -m hsfweb``）。桌面客户端要「顺带把网页
也带起来」，若用子进程就有两个麻烦：

1. **孤儿进程**：客户端被强杀（任务管理器/终端 Ctrl+C 强杀）时子进程活下来，
   继续占着网页端口 ⇒ 下次启动报「端口被占用」（实测踩过）。
2. **找不到解释器**：子进程要另外找 python（板端/裁剪版环境里未必有）。

内嵌（**同进程 + 后台线程**）把两件事都消掉：进程一死线程跟着死，端口自然
释放；解释器就是客户端自己那个，不用找。

    from hsfweb import embed
    handle = embed.start_in_thread(server="127.0.0.1", port=6100,
                                   web_port=8085)
    ...
    handle.stop()          # 停服（幂等）

与 ``python -m hsfweb`` 的差别只有「跑在哪」——装配顺序、协议、页面全部同款
（同一份 :func:`hsfweb.__main__.amain` 的装配逻辑），不存在两套实现。
"""

import asyncio
import os
import threading

from .api import Router
from .auth import SessionStore
from .hub import WebHub
from .httpd import HttpServer
from .store import RunLog

__all__ = ["WebHandle", "start_in_thread"]

#: [遗留标记] 便于检索"还有谁在用内嵌形态"。
LEGACY_EMBED = True


class WebHandle(object):
    """内嵌网页服务器的句柄（线程 + 停止闸 + 实际监听端口）。"""

    def __init__(self):
        self.port = 0          #: 实际监听端口（0 = 还没起来）
        self.error = ""        #: 失败原因（空 = 没失败）
        self.log = None        #: RunLog（供上层翻日志）
        self._thread = None
        self._loop = None
        self._stop = None
        self._ready = threading.Event()

    # -- 状态 ----------------------------------------------------------
    @property
    def alive(self):
        """线程是否还活着（网页是否在跑）。"""
        return bool(self._thread) and self._thread.is_alive()

    @property
    def ready(self):
        """是否已完成启动（起完/起失败都置位）。"""
        return self._ready.is_set()

    def wait_ready(self, timeout=10.0):
        """等启动完成（成功或失败都返回）。"""
        return self._ready.wait(timeout)

    def url(self, host="127.0.0.1"):
        """访问地址（未起则给空串）。"""
        return ("http://%s:%d" % (host, self.port)) if self.port else ""

    # -- 控制 ----------------------------------------------------------
    def stop(self, timeout=5.0):
        """停服（幂等；线程自己会收尾）。"""
        loop = self._loop
        stop = self._stop
        if loop is None or stop is None:
            return
        try:
            loop.call_soon_threadsafe(stop.set)
        except RuntimeError:
            # 循环已关（线程自己退出了）⇒ 忽略。
            pass
        thread = self._thread
        if thread is not None and thread.is_alive():
            thread.join(timeout)


def start_in_thread(server="127.0.0.1", port=6100, web_port=8085,
                    user="root", password="root", bind="0.0.0.0",
                    data_dir=None, log=None):
    """在后台线程里起网页服务器。

    :param server: 上游 XLink 服务端地址。
    :param port: 上游端口（C 端 6100 / Python 端 6200）。
    :param web_port: 网页监听端口。
    :param bind: 网页监听地址（缺省 0.0.0.0 = 局域网可访问）。
    :param data_dir: 告警边沿落盘目录（None = 不落盘）。
    :param log: 复用外部 RunLog（None = 自建一个）。
    :returns: :class:`WebHandle`。
    :note  线程是 **daemon**：主进程退出时它跟着走 ⇒ 网页端口必然释放，
           不会留下孤儿进程占端口。
    """
    handle = WebHandle()

    def _worker():
        try:
            asyncio.run(_serve(handle, server, port, web_port, user, password,
                               bind, data_dir, log))
        except Exception as exc:                        # noqa: BLE001
            handle.error = "%s: %s" % (type(exc).__name__, exc)
        finally:
            handle._ready.set()

    thread = threading.Thread(target=_worker, name="hsf-web", daemon=True)
    handle._thread = thread
    thread.start()
    return handle


async def _serve(handle, server, port, web_port, user, password, bind,
                 data_dir, log):
    """线程里的事件循环主体（装配顺序与 python -m hsfweb 同款）。"""
    run_log = log or RunLog(log_dir=data_dir, echo=True)
    handle.log = run_log
    sessions = SessionStore()
    hub = WebHub(server, port, user, password,
                 data_dir=data_dir or None, log=run_log)
    static_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                              "static")
    router = Router(hub, sessions, static_dir, run_log,
                     upstream_host=server, upstream_port=port)
    http = HttpServer(router, static_dir, run_log, host=bind, port=web_port)

    await hub.start()
    actual = await http.start()
    handle.port = actual
    handle._loop = asyncio.get_running_loop()
    handle._stop = asyncio.Event()
    if actual != web_port:
        run_log.info("网页端口 %d 被占，改听 %d" % (web_port, actual))
    run_log.info("网页服务器就绪：http://%s:%d/（上游 %s:%d）"
                 % (bind if bind != "0.0.0.0" else "127.0.0.1", actual,
                    server, port))
    # [就绪信号 2026-11-14 根修] `_ready` 必须在**端口绑上**时置位，不是等
    # 线程退出才置：`_serve` 之后要 `await handle._stop.wait()`（常驻）⇒
    # 若只在退出的 finally 里置位，`wait_ready()` 每次都要**白等满超时**
    # （实测客户端每次启网页都卡 10 秒；网页其实早就起来了）。
    handle._ready.set()
    try:
        await handle._stop.wait()
    finally:
        await http.close()
        await hub.close()
        run_log.info("网页服务器退出")
        # 退出时也置位（幂等）⇒ 启动失败路径不会吊死调用方。
        handle._ready.set()
