#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""多客户端连接/断开反复压测（联调 + 泄漏回归）。

用法（在仓库根执行）::

    python HighSpeedFilter/client_py/tests/test_stress_reconnect.py \
        --port 6100 --clients 8 --cycles 1250

每个 worker 是**独立进程**：登录派生是 HMAC-SHA256 × 10000 轮的
GIL 绑定计算，线程并发反而串行；多进程才能形成真正并发的多客户端。

单轮动作（完整会话生命周期）::

    连接 → Hello → 登录 → 订阅全部推送主题 → 拉一帧设备健康快照 → 主动断开

判定：
- 任一轮登录/订阅/请求失败即记失败（退出码非 0）；
- 服务端堆计数用 `HSF_HEAP_TRACE=1` 观察：稳态轮次之间必须回落
  （AGENTS 口径：看堆计数不看工作集）；
- 压测前/后自动经远端配置（CMD_CONFIG_GET/SET 的 login 节）**打开再
  恢复**同账并发——与客户端「远端设置 → 登录策略（账号）」同一条链路，
  不再需要手改服务端 hsf_login_policy.json。
"""
from __future__ import print_function

import argparse
import multiprocessing
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))

from hsfsrv_client import proto                      # noqa: E402
from hsfsrv_client.xlink import XLinkClient          # noqa: E402

#: 单轮要订阅的主题（覆盖全部推送类别，检验每会话订阅账本的清理）。
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


def stress_worker(spec):
    """单 worker：循环 连接→登录→订阅→请求→断开。"""
    host, port, user, password, cycles, idx = spec
    ok = 0
    fail = 0
    first_err = ""
    for _ in range(cycles):
        cli = XLinkClient(host, port, user, password)
        cli.auto_reconnect = False
        good, err = cli.start(timeout=8.0)
        if good:
            for topic in TOPICS:
                fut = cli.call_soon(cli.subscribe(topic))
                if fut is None:
                    good, err = False, "订阅投递失败"
                    break
                try:
                    if not fut.result(timeout=3.0):
                        good, err = False, "订阅应答失败"
                        break
                except Exception as exc:                # noqa: BLE001
                    good, err = False, "订阅异常:%r" % (exc,)
                    break
        if good:
            fut = cli.call_soon(cli.request(proto.CMD_DEVICE_HEALTH, b"",
                                            proto.DATATYPE_BSON, 3.0))
            if fut is None:
                good, err = False, "请求投递失败"
            else:
                try:
                    fut.result(timeout=4.0)
                except Exception as exc:                # noqa: BLE001
                    good, err = False, "请求异常:%r" % (exc,)
        cli.close()
        try:
            if cli._thread is not None:
                cli._thread.join(timeout=5.0)
        except Exception:                              # noqa: BLE001
            pass
        if good:
            ok += 1
        else:
            fail += 1
            if not first_err:
                first_err = err or "未知错误"
    return idx, ok, fail, first_err


def _config_login(host, port, user, password, values=None):
    """读/写服务端 login 节（远端配置通道，与设置页同链路）。

    values=None ⇒ 只读并返回当前值；否则写入并返回服务端回显。
    """
    cli = XLinkClient(host, port, user, password)
    cli.auto_reconnect = False
    ok, err = cli.start(timeout=8.0)
    if not ok:
        raise RuntimeError("登录失败：%s" % (err or "未知"))
    try:
        if values is None:
            fut = cli.call_soon(cli.request(proto.CMD_CONFIG_GET, b"",
                                            proto.DATATYPE_BSON, 3.0))
            code, dtype, data = fut.result(timeout=5.0)
            from hsfsrv_client import bson as cbson
            doc = cbson.decode(data) if dtype == proto.DATATYPE_BSON else {}
            return dict(doc.get("login") or {})
        from hsfsrv_client import bson as cbson
        doc = {"section": proto.CFG_SECTION_LOGIN}
        doc.update(values)
        payload = cbson.encode(doc)
        fut = cli.call_soon(cli.request(proto.CMD_CONFIG_SET, payload,
                                        proto.DATATYPE_BSON, 3.0))
        code, dtype, data = fut.result(timeout=5.0)
        if dtype != proto.DATATYPE_BSON:
            raise RuntimeError("配置写应答类型异常")
        reply = cbson.decode(data)
        if not reply.get("ok"):
            raise RuntimeError("配置写失败：%s" % reply.get("err", "未知"))
        return dict(reply.get("login") or values)
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
    ap.add_argument("--clients", type=int, default=8,
                    help="并发客户端进程数")
    ap.add_argument("--cycles", type=int, default=1250,
                    help="每个客户端的连接/断开轮数")
    ap.add_argument("--user", default="root")
    ap.add_argument("--password", default="root")
    ap.add_argument("--no-policy", action="store_true",
                    help="不改服务端登录策略（同账并发需事先放开）")
    args = ap.parse_args()

    total = args.clients * args.cycles
    print("压测：%s:%d，%d 客户端 × %d 轮 = %d 次连接/断开"
          % (args.host, args.port, args.clients, args.cycles, total))

    saved_policy = None
    if not args.no_policy:
        try:
            saved_policy = _config_login(args.host, args.port,
                                         args.user, args.password)
            _config_login(args.host, args.port, args.user, args.password, {
                proto.CFGK_LOGIN_ALLOW_CONCUR: True,
                proto.CFGK_LOGIN_MAX_SESSIONS: 0,
            })
            print("已临时放开同账并发（压测结束恢复）")
        except Exception as exc:                       # noqa: BLE001
            print("警告：登录策略配置失败（%r），继续按当前策略压测" % (exc,))
            saved_policy = None
        # 上一会话按 DenyNew 静默回收需要 ~8s。
        time.sleep(8.5)

    t0 = time.time()
    specs = [(args.host, args.port, args.user, args.password,
              args.cycles, i) for i in range(args.clients)]
    try:
        with multiprocessing.Pool(args.clients) as pool:
            results = pool.map(stress_worker, specs)
    finally:
        if saved_policy is not None:
            try:
                _config_login(args.host, args.port, args.user, args.password, {
                    proto.CFGK_LOGIN_ALLOW_ANON:
                        bool(saved_policy.get(proto.CFGK_LOGIN_ALLOW_ANON,
                                              False)),
                    proto.CFGK_LOGIN_ALLOW_CONCUR:
                        bool(saved_policy.get(proto.CFGK_LOGIN_ALLOW_CONCUR,
                                              False)),
                    proto.CFGK_LOGIN_MAX_SESSIONS:
                        int(saved_policy.get(proto.CFGK_LOGIN_MAX_SESSIONS, 8)),
                })
                print("已恢复原登录策略")
            except Exception as exc:                   # noqa: BLE001
                print("警告：登录策略恢复失败（%r）" % (exc,))
    elapsed = time.time() - t0

    ok = sum(r[1] for r in results)
    fail = sum(r[2] for r in results)
    print("完成：成功 %d / 失败 %d，用时 %.1fs（%.1f 次/秒）"
          % (ok, fail, elapsed, (ok + fail) / elapsed if elapsed > 0 else 0))
    for idx, wok, wfail, werr in results:
        if werr:
            print("  worker %d：失败 %d 次，首个错误：%s"
                  % (idx, wfail, werr))
    return 0 if fail == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
