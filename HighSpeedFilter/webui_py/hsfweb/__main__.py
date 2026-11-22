# -*- coding: utf-8 -*-
"""网页服务器入口：``python -m hsfweb --server 127.0.0.1 --port 6100``。

用法
====

    cd HighSpeedFilter/webui_py
    python -m hsfweb                          # 连 C 版服务端 127.0.0.1:6100，听 8080
    python -m hsfweb --port 6200              # 连 Python 版服务端（两端都要能连）
    python -m hsfweb --bind 127.0.0.1 --web-port 8081 --static ./static

参数
====

    --server HOST    上游 XLink 服务端地址（缺省 127.0.0.1）
    --port N         上游端口：6100 = C 端（bin/HsfServer.exe --sim），
                     6200 = Python 端（server_py）。**--port 指上游**，
                     网页端口用 --web-port，别混。
    --bind HOST      网页监听地址（缺省 0.0.0.0 = 局域网可访问）
    --web-port N     网页监听端口（缺省 8080）
    --user/--password 上游主链路登录账号（缺省 root/root）。
                     与浏览器身份无关：浏览器每次登录都用**自己的**凭据
                     走一次性探测连接到服务端真认证（挑战应答）；
                     主链路只是网页服务器的"数据面"身份。
    --static DIR     静态文件目录（缺省包内 static/）
    --data-dir DIR   告警边沿记录的落盘目录（缺省不落盘，仅内存）

启动顺序：RunLog → SessionStore → WebHub → HttpServer。
[上游惰性连接] 启动时**不连**上游；首个浏览器登录成功后才连
（见 hub.ensure_upstream）。上游不在/没连时网页照常起——页面显示
「未连接服务端」，控制端点回 503，不崩不退出。
"""

import argparse
import asyncio
import os
import sys

from . import ports
from . import proto
from .api import Router
from .auth import SessionStore
from .hub import WebHub
from .httpd import HttpServer
from .store import RunLog


def parse_args(argv):
    parser = argparse.ArgumentParser(
        prog="python -m hsfweb",
        description="高速过滤器网页服务器（纯标准库；上游 = XLink 服务端）")
    parser.add_argument("--server", default=ports.LOOPBACK_HOST,
                        help="上游 XLink 服务端地址（缺省 127.0.0.1）")
    parser.add_argument("--port", type=int, default=ports.C_SERVER_PORT,
                        help="上游端口：6100=C 端 / 6200=Python 端"
                             "（缺省 %d）" % ports.C_SERVER_PORT)
    parser.add_argument("--bind", default=ports.default_web_bind(),
                        help="网页监听地址（缺省 %(default)s）")
    parser.add_argument("--web-port", type=int,
                        default=ports.web_port_from_env()
                        or ports.default_web_port(),
                        help="网页监听端口（缺省 %(default)s）")
    parser.add_argument("--user", default=proto.DEFAULT_ROOT_NAME,
                        help="上游主链路登录账号（缺省 root）")
    parser.add_argument("--password", default=proto.DEFAULT_ROOT_PASS,
                        help="上游主链路登录口令（缺省 root）")
    parser.add_argument("--static", default="",
                        help="静态文件目录（缺省包内 static/）")
    parser.add_argument("--data-dir", default="",
                        help="告警边沿落盘目录（缺省不落盘）")
    return parser.parse_args(argv)


async def amain(args):
    """装配并常驻（Ctrl+C / cancel 退出）。"""
    data_dir = args.data_dir or None
    log = RunLog(log_dir=data_dir, echo=True)
    log.info("网页服务器启动：web=%s:%d 上游=%s:%d（%s，惰性连接）"
             % (args.bind, args.web_port, args.server, args.port, args.user))

    sessions = SessionStore()
    hub = WebHub(args.server, args.port, args.user, args.password,
                 data_dir=data_dir or None, log=log)
    static_dir = args.static or os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "static")
    router = Router(hub, sessions, static_dir, log,
                    upstream_host=args.server, upstream_port=args.port)
    server = HttpServer(router, static_dir, log,
                        host=args.bind, port=args.web_port)

    await hub.start()
    port = await server.start()
    if port != args.web_port:
        log.info("网页端口 %d 被占，改听 %d" % (args.web_port, port))
    # 上游此刻未连（惰性）：照常起 web，页面显示「未连接服务端」。
    print("HSF 网页服务器就绪：http://%s:%d/  （上游 %s:%d 将在首个浏览器"
          "登录后连接）" % (args.bind if args.bind != "0.0.0.0"
                           else "127.0.0.1", port, args.server, args.port),
          flush=True)

    try:
        while True:
            await asyncio.sleep(3600)
    finally:
        await server.close()
        await hub.close()
        log.info("网页服务器退出")


def main(argv=None):
    args = parse_args(sys.argv[1:] if argv is None else argv)
    try:
        asyncio.run(amain(args))
    except KeyboardInterrupt:
        print("\n[hsfweb] Ctrl+C，退出", flush=True)


if __name__ == "__main__":
    main()
