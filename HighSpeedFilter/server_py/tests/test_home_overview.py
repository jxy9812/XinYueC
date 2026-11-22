# -*- coding: utf-8 -*-
"""主页概览数据面：状态快照带 lastAlarm + 自动阶段计时字段齐全（2026-11-19）。

[测什么]
    1. **状态快照带 lastAlarm**：0x0105（CMD_RUN_MODE）应答里必须有
       `lastAlarm` 键；值 = {kind, dev, text, atMs}（无报警时为 **null**）。
    2. **最近一条**：往 alarms 表插两条后，快照里的 lastAlarm 必须是
       **最近那条**（ts 最大），不是最早那条。
    3. **无报警 ⇒ null**：库是空的时候 lastAlarm 必须是 null（**不是**
       空文档 —— 空文档与"没这个键"分不开，客户端要靠 null 说"没有"）。
    4. **自动阶段计时字段齐全**：快照必须带 autoStageElapsedMs /
       autoStageTotalMs / autoRunMs（主页「阶段信息」行与累计时长要用）。

[为什么要有这条锁] 主页新加了两块内容（阶段信息行 + 最近报警卡）⇒ 数据
    面（服务端状态快照）必须**真的**带上这些键；漏一个就是界面恒显
    「--」/「无报警」而没人发现。

[怎么跑]
    python3 -I tests/test_home_overview.py
"""

import asyncio
import os
import shutil
import sys
import threading

for _stream in (sys.stdout, sys.stderr):
    try:
        _stream.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError, OSError):
        pass

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.dirname(HERE))

from hsfsrv import auth as auth_mod          # noqa: E402
from hsfsrv import bson, config as cfgmod    # noqa: E402
from hsfsrv import proto, xlink              # noqa: E402
from fake_devices import build_fake_app      # noqa: E402
from test_vfd_ops import Client              # noqa: E402  复用协议客户端


async def _spin_server(basedir, log, **cfg_overrides):
    """起一份假设备服务端（临时目录 + 临时端口；本进程内真协议栈）。"""
    cfg = cfgmod.Config()
    cfg.resolve_paths(basedir)
    cfg.config_path = os.path.join(basedir, "cfg", "x.json")
    cfg.users_path = os.path.join(basedir, "cfg", "hsf_users.json")
    os.makedirs(os.path.join(basedir, "cfg"), exist_ok=True)
    cfg.port = 0
    cfg.enable_serial = False
    # [落盘要开] 报警历史存在 alarms 表 ⇒ 服务端必须开着落盘。
    cfg.storage.enabled = True
    cfg.storage.path = os.path.join(basedir, "hsf_data.db")
    for key, val in cfg_overrides.items():
        setattr(cfg, key, val)
    users = auth_mod.UserStore(cfg.users_path)
    users.create_defaults()
    business = build_fake_app(cfg, log=log)
    server = xlink.Server(cfg.port, users, log=log, require_auth=True)
    business.attach(server)
    await server.start()
    business.storage_apply()
    return business, server


def _query_mode(c, server):
    """客户端问一次运行模式（0x0105）⇒ 应答文档。"""
    r = c.request(proto.CMD_RUN_MODE, bson.encode({}))
    assert r is not None, "0x0105 无应答"
    doc = r[1]
    assert doc.get("ok") is True, "0x0105 应受理：%r" % doc
    return doc


async def _async_case(basedir, log):
    business, server = await _spin_server(basedir, log)
    store = business.store
    errors = []

    def scenario():
        try:
            c = Client("127.0.0.1", server.port)
            c.hello()
            ok, _role = c.login("root", "root")
            assert ok, "root 登录失败"

            # ---- ① 空库 ⇒ lastAlarm = null ----
            doc = _query_mode(c, server)
            assert proto.KEY_LAST_ALARM in doc, \
                "0x0105 应带 %s 键：%r" % (proto.KEY_LAST_ALARM, sorted(doc))
            assert doc[proto.KEY_LAST_ALARM] is None, \
                "空库时 lastAlarm 应为 null（不是空文档）：%r" % (
                    doc[proto.KEY_LAST_ALARM],)

            # ---- ② 插两条 ⇒ 快照给**最近**那条 ----
            store.insert_alarm(proto.ALARM_KIND_AUTO, "自动运行",
                               "较早的一条：阀2 动作超时")
            store.insert_alarm(proto.ALARM_KIND_HEALTH, "温湿度",
                               proto.HEALTH_TEXT[proto.HEALTH_STALE],
                               dev=proto.KIND_TEMPHUM)
            doc = _query_mode(c, server)
            la = doc.get(proto.KEY_LAST_ALARM)
            assert isinstance(la, dict), \
                "有报警时 lastAlarm 应是文档：%r" % (la,)
            assert la.get(proto.LAST_ALARM_KEY_TEXT) == \
                proto.HEALTH_TEXT[proto.HEALTH_STALE], \
                "应取**最近**那条（数据过期）：%r" % (la,)
            assert la.get(proto.LAST_ALARM_KEY_KIND) == proto.ALARM_KIND_HEALTH, \
                "kind 应是来源类别 health：%r" % (la,)
            assert la.get(proto.LAST_ALARM_KEY_DEV) == proto.KIND_TEMPHUM, \
                "dev 应是设备类别：%r" % (la,)
            assert int(la.get(proto.LAST_ALARM_KEY_AT_MS) or 0) > 0, \
                "atMs 应是 epoch 毫秒：%r" % (la,)

            # ---- ③ 自动阶段计时字段齐全（主页阶段信息行要用）----
            for key in (proto.KEY_AUTO_STAGE_ELAPSED_MS,
                        proto.KEY_AUTO_STAGE_TOTAL_MS,
                        proto.KEY_AUTO_RUN_MS,
                        proto.KEY_AUTO_PHASE,
                        proto.KEY_AUTO_MODE):
                assert key in doc, "0x0105 缺 %s：%r" % (key, sorted(doc))
        except Exception as exc:                    # noqa: BLE001
            errors.append(exc)

    thread = threading.Thread(target=scenario, name="home-overview-client")
    thread.start()
    try:
        while thread.is_alive():
            await asyncio.sleep(0.05)
    finally:
        thread.join()
        await server.stop()
    if errors:
        raise errors[0]


def main():
    basedir = os.path.join(os.path.dirname(HERE), "_run", "home_overview")
    shutil.rmtree(basedir, ignore_errors=True)
    os.makedirs(basedir, exist_ok=True)
    log = []
    try:
        asyncio.run(_async_case(basedir, log))
        print("[PASS] 状态快照带 lastAlarm（空=null / 最近一条）+ "
              "自动阶段计时字段齐全")
    except AssertionError as exc:
        print("[FAIL] %s" % exc)
        return 1
    except Exception as exc:                        # noqa: BLE001
        print("[ERROR] %s: %s" % (type(exc).__name__, exc))
        return 2
    finally:
        shutil.rmtree(basedir, ignore_errors=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
