#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""verify/board/session_churn.py —— 单进程反复「连接→登录→订阅→断开」。

用途：在**板端服务端**上制造大量会话生命周期，用来判别「服务端堆随
会话数线性上涨」的按会话泄漏（AGENTS.md 判据：看堆计数不看工作集；
「第二轮不再涨」= 堆 arena 平台期）。

与 `client_py/tests/test_stress_reconnect.py` 的区别：那个用 multiprocessing
开多个 worker 进程（Windows 上 spawn 在部分环境里 DuplicateHandle 失败），
本脚本**单进程串行**跑，方便在本机直接跑，且不依赖 PyQt。

用法::

    python verify/board/session_churn.py --host 192.168.1.249 --port 6100 \
        --user root --password root --cycles 1500
"""
from __future__ import print_function

import argparse
import os
import sys
import time

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(REPO, "HighSpeedFilter", "client_py"))

from hsfsrv_client import proto                      # noqa: E402
from hsfsrv_client.xlink import XLinkClient          # noqa: E402

TOPICS = (
    proto.TOPIC_VFD_STATUS,
    proto.TOPIC_AI_STATUS,
    proto.TOPIC_TEMPHUM_STATUS,
    proto.TOPIC_FLOWMETER_STATUS,
    proto.TOPIC_DEVICE_HEALTH,
    proto.TOPIC_DIGITAL_STATUS,
    proto.TOPIC_VALVE_STATUS,
    proto.TOPIC_PUMP_STATUS,
    getattr(proto, "TOPIC_TURBIDITY_STATUS",
            getattr(proto, "XLINK_TOPIC_USER_BASE", 0x00010000) + 9),
)


def one_session(host, port, user, password):
    """跑一轮完整会话；返回 (ok, err)。"""
    cli = XLinkClient(host, port, user, password)
    cli.auto_reconnect = False
    good, err = cli.start(timeout=8.0)
    try:
        if good:
            for topic in TOPICS:
                fut = cli.call_soon(cli.subscribe(topic))
                if fut is None:
                    return False, "订阅投递失败"
                try:
                    if not fut.result(timeout=3.0):
                        return False, "订阅应答失败"
                except Exception as exc:                # noqa: BLE001
                    return False, "订阅异常:%r" % (exc,)
            fut = cli.call_soon(cli.request(proto.CMD_DEVICE_HEALTH, b"",
                                            proto.DATATYPE_BSON, 3.0))
            if fut is None:
                return False, "请求投递失败"
            try:
                fut.result(timeout=4.0)
            except Exception as exc:                    # noqa: BLE001
                return False, "请求异常:%r" % (exc,)
        return good, (err or "")
    finally:
        cli.close()
        try:
            if cli._thread is not None:
                cli._thread.join(timeout=5.0)
        except Exception:                              # noqa: BLE001
            pass


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=6100)
    ap.add_argument("--user", default="root")
    ap.add_argument("--password", default="root")
    ap.add_argument("--cycles", type=int, default=1000)
    args = ap.parse_args()

    ok = 0
    fail = 0
    first_err = ""
    t0 = time.time()
    for i in range(args.cycles):
        good, err = one_session(args.host, args.port, args.user, args.password)
        if good:
            ok += 1
        else:
            fail += 1
            if not first_err:
                first_err = err or "未知错误"
        if (i + 1) % 100 == 0:
            el = time.time() - t0
            print("[churn] %d/%d ok=%d fail=%d %.1fs (%.1f 会话/秒) last_err=%s"
                  % (i + 1, args.cycles, ok, fail, el,
                     (i + 1) / max(el, 0.001), first_err), flush=True)
    print("[churn] 完成 ok=%d fail=%d first_err=%s" % (ok, fail, first_err))
    return 0 if fail == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
