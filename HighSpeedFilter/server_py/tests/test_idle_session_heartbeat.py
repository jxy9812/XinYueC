# -*- coding: utf-8 -*-
"""回归锁：只订事件流的空闲客户端不得被「静默超时看门狗」误杀。

[2026-11-19 实锅] 服务端对客户端心跳「不必回」+ 自己的主动心跳
「静默达 1s 间隔才发」两条合在一起的竞态：空闲客户端每 1s 发心跳刷新了
服务端的 last_receive ⇒ 服务端主动心跳几乎永远凑不满间隔 ⇒ 只剩 tick
相位偶然落在客户端两拍之间的窄窗才发 ⇒ 纯订阅**事件流**主题（如
0x0001000B 报警事件，没有周期推送兜底）的空闲客户端可能 6s 收不到任何
下行帧 ⇒ 被客户端自己的「静默超时看门狗」（6s 无下行即断链重连）反复
误杀。E2E 实测：tap 每 ~11-14s 断链重连一次，订阅空窗正好吞掉阀超时
告警帧（端到端表现：I4 偶发收不到 kind=5）。

[修法] 服务端收到 SESSION_HEARTBEAT 就回一帧（此前是「不必回」）⇒
空闲会话的下行节拍由「客户端发一帧 → 服务端回一帧」驱动，恒 < 6s。

[怎么跑]
    python -I tests/test_idle_session_heartbeat.py    # 独立跑
    pytest tests/test_idle_session_heartbeat.py -q    # pytest 收集
"""

import asyncio
import os
import shutil
import sys
import tempfile
import threading
import time

for _stream in (sys.stdout, sys.stderr):
    try:
        _stream.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError, OSError):
        pass

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.dirname(HERE))

from hsfsrv import auth as auth_mod            # noqa: E402
from hsfsrv import bson, config as cfgmod      # noqa: E402
from hsfsrv import proto                       # noqa: E402
from hsfsrv import xlink                       # noqa: E402
from hsfsrv import app as app_mod              # noqa: E402

#: 拿 client_py 的 XLinkClient 当被试客户端（与桌面同一份协议栈）。
CLIENT_ROOT = os.path.join(os.path.dirname(HERE), os.pardir, "client_py")
CLIENT_ROOT = os.path.abspath(CLIENT_ROOT)
if CLIENT_ROOT not in sys.path:
    sys.path.insert(0, CLIENT_ROOT)
from hsfsrv_client.xlink import XLinkClient    # noqa: E402

PASS = 0
FAIL = 0


def check(name, ok, detail=""):
    global PASS, FAIL
    if ok:
        PASS += 1
        print("  [PASS] %s %s" % (name, detail))
    else:
        FAIL += 1
        print("  [FAIL] %s %s" % (name, detail))


#: 空闲观察窗（秒）。旧代码的断链节奏是 ~6-14s 一轮 ⇒ 16s 内必然至少断
#: 一次；修复后应一个断链都没有。
IDLE_S = 16.0


async def _spin(basedir):
    """起一份最小 sim 服务端（真 XLink 栈；与 test_sim_full_flow 同款）。"""
    cfg = cfgmod.Config()
    cfg.resolve_paths(basedir)
    cfg.config_path = os.path.join(basedir, "cfg", "x.json")
    cfg.users_path = os.path.join(basedir, "cfg", "hsf_users.json")
    os.makedirs(os.path.join(basedir, "cfg"), exist_ok=True)
    cfg.port = 0
    cfg.sim_enabled = True
    cfg.storage.enabled = False
    users = auth_mod.UserStore(cfg.users_path)
    users.create_defaults()
    business = app_mod.App(cfg, log=lambda m: None)
    business.build_devices()
    server = xlink.Server(cfg.port, users, log=lambda m: None,
                          require_auth=True)
    business.attach(server)
    await server.start()
    await business.start_polling()
    await asyncio.sleep(0.3)
    return business, server


def test_idle_event_subscriber_survives():
    base = tempfile.mkdtemp(prefix="hsf_idle_sess_")

    async def _case():
        business, server = await _spin(base)
        drops = []
        states = []

        def scenario():
            c = XLinkClient("127.0.0.1", server.port, "root", "root",
                            log=lambda m: drops.append(m))
            c.on_state_changed = lambda st: states.append(str(st))
            ok, err = c.start(timeout=15.0)
            if not ok:
                drops.append("登录失败：%s" % err)
                return
            fut = c.call_soon(c.subscribe(proto.TOPIC_AUTO_ALARM))
            if fut is not None:
                try:
                    fut.result(timeout=5.0)
                except Exception:                   # noqa: BLE001
                    pass
            time.sleep(IDLE_S)
            alive = bool(c._alive()) if hasattr(c, "_alive") else True
            c.close()
            drops.append("__alive=%s" % alive)

        th = threading.Thread(target=scenario, name="idle-tap")
        th.start()
        try:
            while th.is_alive():
                await asyncio.sleep(0.05)
        finally:
            th.join()
            await server.stop()
        return drops, states

    base = None
    try:
        base = tempfile.mkdtemp(prefix="hsf_idle_sess_case_")
        drops, states = asyncio.run(_case())
        reconnects = sum(1 for st in states if st == "connecting") - 1
        check("A1 空闲 16s（只订报警事件流）不断链", reconnects <= 0,
              "重连=%d 状态=%s" % (reconnects, states))
        check("A2 无「静默超时 → 断链」日志",
              not any("静默超时" in ln for ln in drops),
              "日志=%s" % [ln for ln in drops if "静默" in ln][:2])
        check("A3 会话仍存活（_alive=True）",
              any(ln.startswith("__alive=True") for ln in drops),
              "尾=%s" % drops[-1:])
    finally:
        if base:
            shutil.rmtree(base, ignore_errors=True)


def main():
    test_idle_event_subscriber_survives()
    print("\n===== 空闲会话心跳回归：%d failed（PASS %d / FAIL %d）====="
          % (FAIL, PASS, FAIL))
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
