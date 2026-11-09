# -*- coding: utf-8 -*-
"""S1 端到端：AUTO_ALARM（0x0001000B）发布链路（2026-11-09）。

[测什么] 对齐 C 侧 HsfServerApp.c hsfAppPushAutoAlarms 的三条口径：
    1. 压急停 → 订阅会话收到一帧 0x0001000B，载荷 {kind, atMs, text}
       （键名与 C 侧 XAutoControlCodec_alarmToBson 逐字一致；kind=4
       EmergencyStop）；
    2. 一帧一条（takeAlarm 取走即消费）：同一帧绝不出两次；已锁存再压
       急停不重复报警（C 同款：已是 EmergencyStop 态不重复入队）；
    3. 告警入库：insert_alarm("auto", "自动运行", text)，与 C 侧
       insertAlarm(&s_store, "auto", "自动运行", ...) 同表同分类。

[怎么跑]
    python3 -I tests/test_auto_alarm_publish.py          # 独立跑
    pytest tests/test_auto_alarm_publish.py -q           # pytest 收集

服务端在本进程内起（假设备 + 真 XLink 协议栈），客户端走真 socket。
"""

import asyncio
import os
import shutil
import sys
import tempfile
import threading

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
    cfg.port = 0                       # 临时端口，避免撞上在跑的实例
    cfg.enable_serial = False
    for key, val in cfg_overrides.items():
        setattr(cfg, key, val)
    users = auth_mod.UserStore(cfg.users_path)
    users.create_defaults()
    business = build_fake_app(cfg, log=log)
    server = xlink.Server(cfg.port, users, log=log, require_auth=True)
    business.attach(server)
    await server.start()
    return business, server


def _subscribe(client, topic):
    client._send(proto.MSGCLASS_SUBSCRIBE, topic & 0xFFFF,
                 proto.DATATYPE_RAW, proto.pack_topic(topic))


def _alarm_frames(client):
    return [doc for (t, doc, _ts) in client.publishes
            if t == proto.TOPIC_AUTO_ALARM]


async def _case_estop_publish(basedir, log):
    business, server = await _spin_server(basedir, log)
    store_calls = []
    orig_insert = business.store.insert_alarm

    def spy_insert(kind, source, text, now_ms=None):
        store_calls.append((kind, source, text))
        return orig_insert(kind, source, text, now_ms)

    business.store.insert_alarm = spy_insert

    # [为什么在子线程跑客户端] 客户端是阻塞式 socket 读；服务端协程与之同
    # 在一个事件循环里，主线程一旦阻塞在读上，服务端就饿死（实测 hello 都
    # 回不去）。故客户端放子线程，事件循环留在主线程持续驱动 tick。
    errors = []

    def scenario():
        try:
            c = Client("127.0.0.1", server.port)
            c.hello()
            ok, _role = c.login("root", "root")
            assert ok, "root 登录失败"
            _subscribe(c, proto.TOPIC_AUTO_ALARM)
            c.pump(0.3)
            c.publishes.clear()

            # ---- ① 压急停（永远放行，不要求自动模式）→ 收一帧 0x0001000B ----
            r = c.request(proto.CMD_AUTO_CONTROL,
                          bson.encode({"op": proto.AUTO_OP_ESTOP}))
            assert r is not None and r[1].get("ok") is True, \
                "急停命令应受理：%r" % (r[1] if r else None)
            c.pump(1.5)
            frames = _alarm_frames(c)
            assert len(frames) == 1, "期望恰好 1 条告警帧，实得 %d：%r" % (
                len(frames), frames)
            f = frames[0]
            assert f.get("kind") == proto.AUTO_ALARM_ESTOP, \
                "kind 应为 EmergencyStop(4)：%r" % f
            assert isinstance(f.get("atMs"), int) and f["atMs"] > 0, \
                "atMs 应为单调毫秒：%r" % f
            assert "急停" in str(f.get("text", "")), "text 应带急停语义：%r" % f

            # ---- ② 入库分类（对齐 C 侧 insertAlarm("auto","自动运行",...)）--
            assert ("auto", "自动运行") in \
                [(k, s) for (k, s, _t) in store_calls], \
                "告警应入库 auto/自动运行：%r" % (store_calls,)

            # ---- ③ 一帧一条：多等几拍，同一帧不再出现 ----
            c.publishes.clear()
            c.pump(1.0)
            assert _alarm_frames(c) == [], \
                "取走即消费：不应再出帧 %r" % (_alarm_frames(c),)

            # ---- ④ 已锁存再压急停：不重复报警（C 同款幂等）----
            r = c.request(proto.CMD_AUTO_CONTROL,
                          bson.encode({"op": proto.AUTO_OP_ESTOP}))
            assert r is not None and r[1].get("ok") is True
            c.pump(1.0)
            assert _alarm_frames(c) == [], \
                "锁存期间重复急停不应重复报警 %r" % (_alarm_frames(c),)
            n_store = len(store_calls)

            # ---- ⑤ 逻辑层连 raise 两条 → 下一拍恰好两条帧（FIFO 顺序）----
            business.auto_controller.raise_alarm(
                proto.AUTO_ALARM_ACTION_FAILED, "动作失败A")
            business.auto_controller.raise_alarm(
                proto.AUTO_ALARM_CUSTOM, "自定义B")
            c.pump(1.0)
            frames = _alarm_frames(c)
            assert [fr.get("kind") for fr in frames] == \
                [proto.AUTO_ALARM_ACTION_FAILED, proto.AUTO_ALARM_CUSTOM], \
                "应按 FIFO 出两条帧：%r" % (frames,)
            assert [fr.get("text") for fr in frames] == ["动作失败A", "自定义B"]
            assert len(store_calls) == n_store + 2, \
                "两条告警都应入库：%r" % (store_calls[n_store:],)

            # ---- ⑥ 复位后锁存解除，再急停重新报警（迁移沿一次）----
            r = c.request(proto.CMD_AUTO_CONTROL,
                          bson.encode({"op": proto.AUTO_OP_RESET}))
            assert r is not None and r[1].get("ok") is True
            c.publishes.clear()
            c.pump(0.5)
            assert _alarm_frames(c) == []      # 复位本身不是告警
            r = c.request(proto.CMD_AUTO_CONTROL,
                          bson.encode({"op": proto.AUTO_OP_ESTOP}))
            assert r is not None and r[1].get("ok") is True
            c.pump(1.0)
            frames = _alarm_frames(c)
            assert len(frames) == 1 and frames[0].get("kind") == \
                proto.AUTO_ALARM_ESTOP, "复位后急停应重新报警：%r" % (frames,)
            c.close()
        except Exception as exc:                    # noqa: BLE001
            errors.append(exc)

    thread = threading.Thread(target=scenario, name="alarm-client")
    thread.start()
    try:
        while thread.is_alive():
            await asyncio.sleep(0.05)     # 循环持续喂服务端 tick
    finally:
        thread.join()
        await server.stop()
    if errors:
        raise errors[0]


def test_estop_publishes_alarm_frame_on_topic_0x0001000B():
    base = tempfile.mkdtemp(prefix="hsf_alarm_")
    try:
        asyncio.run(_case_estop_publish(base, lambda m: None))
    finally:
        shutil.rmtree(base, ignore_errors=True)


def test_alarm_queue_full_drops_oldest_keeps_newest():
    """队列满挤掉最旧一条（对齐 C raiseAlarm：永不因满丢新告警）。"""
    from hsfsrv import autocontrol

    class _A(object):
        digital = []
        valve_bank = None
        analog = None
        vfd = None
        temphum = None

    ctx = autocontrol.AutoContext(_A(), log=lambda m: None)
    ctl = autocontrol.AutoController(ctx, log=lambda m: None)
    for i in range(ctl.ALARM_CAPACITY + 3):
        ctl.raise_alarm(proto.AUTO_ALARM_CUSTOM, "第%d条" % i, now_ms=i)
    assert ctl.pending_alarm_count() == ctl.ALARM_CAPACITY
    first = ctl.take_alarm()
    assert first["text"] == "第3条", "最旧 3 条被挤掉：%r" % first
    last = None
    while True:
        item = ctl.take_alarm()
        if item is None:
            break
        last = item
    assert last["text"] == "第%d条" % (ctl.ALARM_CAPACITY + 2), \
        "最新一条必须保留：%r" % last


def test_alarm_text_clipped_at_128_bytes_utf8_safe():
    """超长文字按 UTF-8 字节截断且不截出半个多字节字符（对齐 C m_text[128]）。"""
    from hsfsrv import autocontrol

    class _A(object):
        digital = []
        valve_bank = None
        analog = None
        vfd = None
        temphum = None

    ctx = autocontrol.AutoContext(_A(), log=lambda m: None)
    ctl = autocontrol.AutoController(ctx, log=lambda m: None)
    ctl.raise_alarm(proto.AUTO_ALARM_CUSTOM, "急" * 100)   # 200 字节
    text = ctl.take_alarm()["text"]
    assert len(text.encode("utf-8")) <= ctl.ALARM_TEXT_MAX_BYTES
    text.encode("utf-8")          # 能整体编码 = 没有截出半个字符


def main():
    """独立跑入口（与 pytest 收集等价；退出码 0=全过）。"""
    failed = []
    for name, fn in sorted(globals().items()):
        if name.startswith("test_") and callable(fn):
            try:
                fn()
                print("  [PASS] %s" % name)
            except AssertionError as exc:
                failed.append(name)
                print("  [FAIL] %s：%s" % (name, exc))
            except Exception as exc:                    # noqa: BLE001
                failed.append(name)
                print("  [ERROR] %s：%s: %s" % (name, type(exc).__name__, exc))
    print("\n===== 自动告警发布链路: %d failed =====" % len(failed))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
