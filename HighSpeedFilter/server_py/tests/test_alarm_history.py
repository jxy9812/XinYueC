# -*- coding: utf-8 -*-
"""报警历史：服务端记健康边沿 + 0x011A 取数（2026-11-17 新增）。

[测什么]
    1. **健康边沿入库**：设备由正常转异常（数据过期/离线/故障）、异常转正常
       各记一条 alarms 行（kind=health、source=设备名、text=健康档中文、
       dev=设备类别）—— 与网页端口径一致（网页端 hub.feed_health 的
       「进入异常记一条 / 恢复记一条」）。
    2. **0x011A 取数**：客户端按 0x011A 取回这些行（键集与行内键逐字对齐
       proto.py 的 ALARM_KEY_*），total/count/offset/limit 齐备。

[为什么要有这条命令] 报警页此前是数据三页里**唯一不向服务端取数**的一页：
客户端与网页端各自在本地进程内存边沿 ⇒ 客户端一重启历史全丢（用户实报
「web 端报警有数据，客户端报警视图没数据」）⇒ 边沿改由服务端记 + 0x011A 取。

[怎么跑]
    python3 -I tests/test_alarm_history.py          # 独立跑
    pytest tests/test_alarm_history.py -q           # pytest 收集
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
    # [落盘要开] 报警历史存在 alarms 表 ⇒ 服务端必须开着落盘（否则 store
    # 没库，写入无处可去）。
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
    # [落盘要真开] 报警历史在 alarms 表 ⇒ 这里把库打开（真实 main 走
    # start_storage()，测试夹具不跑那一段 ⇒ 手动开）。
    business.storage_apply()
    return business, server


def _entry(name, kind, health, text=""):
    """造一条健康条目（与服务端 HealthEntry 同形：name/kind/side/health/…）。"""
    from hsfsrv.app import HealthEntry
    return HealthEntry(name, kind, 0, health, 0, health != proto.HEALTH_OFFLINE,
                       0, text, "COM17")


async def _case_health_edge_store(basedir, log):
    """健康边沿入 alarms 表（异常一条 + 恢复一条 + 同段异常不重复）。"""
    business, server = await _spin_server(basedir, log)
    try:
        store = business.store
        # ---- ① 正常 → 数据过期：记一条 ----
        business._health_prev = {"温湿度": proto.HEALTH_OK}
        business._record_health_edges(
            [_entry("温湿度", proto.KIND_TEMPHUM, proto.HEALTH_STALE)],
            business._now_ms())
        rows = store.query_alarms_page(limit=50)[0]
        assert len(rows) == 1, "应记 1 条边沿，实得 %d：%r" % (len(rows), rows)
        ts, kind, source, text, dev = rows[0]
        assert kind == proto.ALARM_KIND_HEALTH, "kind 应为 health：%r" % kind
        assert source == "温湿度", "source 应是设备名：%r" % source
        assert text == proto.HEALTH_TEXT[proto.HEALTH_STALE], \
            "text 应是健康档中文「数据过期」：%r" % text
        assert dev == proto.KIND_TEMPHUM, "dev 应是设备类别：%r" % dev

        # ---- ② 同一段异常（过期 → 离线）：**不重复**记 ----
        business._record_health_edges(
            [_entry("温湿度", proto.KIND_TEMPHUM, proto.HEALTH_OFFLINE)],
            business._now_ms())
        rows = store.query_alarms_page(limit=50)[0]
        assert len(rows) == 1, "同一段异常不应重复记：%r" % (rows,)

        # ---- ③ 异常 → 正常：记一条「已恢复正常」----
        business._record_health_edges(
            [_entry("温湿度", proto.KIND_TEMPHUM, proto.HEALTH_OK)],
            business._now_ms())
        rows = store.query_alarms_page(limit=50)[0]
        assert len(rows) == 2, "恢复应再记 1 条：%r" % (rows,)
        newest = rows[-1]
        assert newest[3] == proto.ALARM_TEXT_RECOVERED, \
            "恢复行文案应为「已恢复正常」：%r" % (newest,)
    finally:
        await server.stop()


async def _case_alarm_page(basedir, log):
    """0x011A 取数：键集/行内键/total 齐备。"""
    business, server = await _spin_server(basedir, log)
    store = business.store
    store.insert_alarm(proto.ALARM_KIND_HEALTH, "温湿度",
                       proto.HEALTH_TEXT[proto.HEALTH_STALE],
                       dev=proto.KIND_TEMPHUM)
    store.insert_alarm(proto.ALARM_KIND_AUTO, "自动运行",
                       "现场启动按钮被拒 —— 当前是手动模式")
    errors = []

    def scenario():
        # [为什么客户端放子线程] 客户端是阻塞式 socket 读；服务端协程与之
        # 同在一个事件循环里，主线程一旦阻塞在读上，服务端就饿死（同
        # test_auto_alarm_publish 的口径）。
        try:
            c = Client("127.0.0.1", server.port)
            c.hello()
            ok, _role = c.login("root", "root")
            assert ok, "root 登录失败"
            r = c.request(proto.CMD_ALARM_PAGE,
                          bson.encode({"offset": 0, "limit": 20}))
            assert r is not None, "0x011A 无应答"
            doc = r[1]
            assert doc.get("ok") is True, "0x011A 应受理：%r" % doc
            assert doc.get("total") == 2, "total 应为 2：%r" % doc
            assert doc.get(proto.LOG_KEY_COUNT) == 2, "count 应为 2：%r" % doc
            rows = doc.get(proto.HIST_KEY_ROWS) or []
            assert len(rows) == 2, "rows 应有 2 行：%r" % (rows,)
            for key in (proto.LOG_KEY_TS, proto.ALARM_KEY_KIND,
                        proto.ALARM_KEY_SOURCE, proto.ALARM_KEY_TEXT,
                        proto.ALARM_KEY_DEV):
                assert key in rows[0], "行内缺键 %s：%r" % (key, rows[0])
            kinds = sorted(x.get(proto.ALARM_KEY_KIND) for x in rows)
            assert kinds == ["auto", "health"], "两类来源都应取到：%r" % kinds

            # ---- 关键字过滤（text/source/kind 三列 LIKE）----
            r = c.request(proto.CMD_ALARM_PAGE,
                          bson.encode({"offset": 0, "limit": 20,
                                       "kw": "温湿度"}))
            doc = r[1]
            assert doc.get("ok") is True, "kw 查询应受理：%r" % doc
            assert doc.get("total") == 1,                 "kw=温湿度 应只命中 1 行：%r" % doc
        except Exception as exc:                    # noqa: BLE001
            errors.append(exc)

    thread = threading.Thread(target=scenario, name="alarm-page-client")
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
    basedir = os.path.join(os.path.dirname(HERE), "_run", "alarm_history")
    shutil.rmtree(basedir, ignore_errors=True)
    os.makedirs(basedir, exist_ok=True)
    log = []
    try:
        asyncio.run(_case_health_edge_store(basedir, log))
        print("[PASS] 健康边沿入库（异常/恢复各一条、同段异常不重复）")
        # [两个用例各用各的库] 落盘库按目录 ⇒ 第二个用例另起一个目录，
        # 否则第一个用例的行会把 total 顶成 4。
        base2 = os.path.join(basedir, "case2")
        os.makedirs(base2, exist_ok=True)
        asyncio.run(_case_alarm_page(base2, log))
        print("[PASS] 0x011A 报警历史取数（键集/total/count/关键字过滤）")
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
