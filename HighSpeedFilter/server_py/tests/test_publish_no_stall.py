# -*- coding: utf-8 -*-
"""周期推送不得阻塞事件循环（2026-11-15 用户口径：服务端不能因为某个
客户端读不动，就让自动控制停滞）。

[背景] XLink 的推送路径曾经是「写完 await writer.drain()」，而**周期推送
是在服务端的 tick 里 await 的**（app._publish_telemetry → server.publish
→ session.send_publish）。drain() 的语义是"等发送缓冲降下来"，对端读不动
（TCP 窗口关）时它会**一直等** ⇒ 一个卡死的客户端就能把**整个事件循环的
tick 卡住** ⇒ 自动控制的命令、全部遥测、告警推送一起停摆。

C 端没有这个坑：XLink 用**发送队列 + 队满拒帧**（XLinkSession.c 的
m_txQueue：队满时 publish 返回失败并记一次推送失步，**从不阻塞发布方**）。
本端对齐 C 端：周期推送**只写不等**，缓冲超上限就丢这一帧（遥测是周期
帧，丢一帧下一秒补上）；**应答**与**订阅补发**仍走 drain=True（必须确保
送达，且它们不在 tick 里 await）。

[怎么跑]
    python3 -I tests/test_publish_no_stall.py
"""

import asyncio
import os
import sys

for _stream in (sys.stdout, sys.stderr):
    try:
        _stream.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError, OSError):
        pass

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.dirname(HERE))

from hsfsrv import proto                    # noqa: E402
from hsfsrv import xlink                   # noqa: E402

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


class _WedgedWriter(object):
    """假 writer：write 正常收；**drain 永不返回**（模拟对端不读）。"""

    def __init__(self):
        self.written = []
        self.drain_calls = 0

    def write(self, data):
        self.written.append(data)

    async def drain(self):
        self.drain_calls += 1
        await asyncio.Event().wait()      # 永不返回

    # [形状照抄真 StreamWriter] 缓冲大小在 **transport** 上，StreamWriter
    # 自己没有这个方法（踩过：写成 writer.get_write_buffer_size() ⇒ 每拍
    # 抛 AttributeError ⇒ 整个 tick 停摆）。本假 writer 同样只给 transport。
    @property
    def transport(self):
        return self

    def get_write_buffer_size(self):
        return 0

    def is_closing(self):
        return False

    def close(self):
        pass

    def get_extra_info(self, name):
        return ("127.0.0.1", 12345) if name == "peername" else None


class _Reader(object):
    async def read(self, n):
        await asyncio.Event().wait()      # 永不返回（收包侧不参与本用例）

    def at_eof(self):
        return False


def _make_session(writer):
    sess = xlink.Session(_Reader(), writer, None)
    sess.subscriptions.add(proto.TOPIC_VFD_STATUS)
    sess.auth_passed = True
    sess.auth_user = "root"
    sess.auth_role = 2
    sess.state = xlink.SessionState.READY     # is_ready() 看的是 state
    return sess


async def _case_no_stall():
    print("=== 周期推送不阻塞（drain 不等）===")

    # ① 周期推送（drain=False）⇒ **不碰 drain** ⇒ 立刻返回。
    writer = _WedgedWriter()
    sess = _make_session(writer)
    ok = await asyncio.wait_for(
        sess.send_publish(proto.TOPIC_VFD_STATUS, b"x" * 64, drain=False),
        timeout=1.0)
    check("T1 周期推送（drain=False）立即返回", ok is True)
    check("T1b 周期推送**没有**调用 drain（对端读不动也不会卡）",
          writer.drain_calls == 0, "drain_calls=%d" % writer.drain_calls)
    check("T1c 帧确实写出去了", len(writer.written) == 1,
          "written=%d" % len(writer.written))

    # ② Server.publish 的缺省是 drain=False ⇒ 周期推送走的就是它。
    writer2 = _WedgedWriter()
    sess2 = _make_session(writer2)
    srv = xlink.Server.__new__(xlink.Server)
    srv.sessions = [sess2]
    srv.on_publish_now = None
    count = await asyncio.wait_for(
        srv.publish(proto.TOPIC_VFD_STATUS, b"y" * 64), timeout=1.0)
    check("T2 Server.publish 缺省不等 drain（tick 不被卡）",
          count == 1 and writer2.drain_calls == 0,
          "count=%d drain_calls=%d" % (count, writer2.drain_calls))

    # ③ 应答路径仍走 drain=True（必须确保送达；且不在 tick 里 await）。
    writer3 = _WedgedWriter()
    sess3 = _make_session(writer3)
    task = asyncio.ensure_future(
        sess3.send_response(1, proto.CMD_VFD_SNAPSHOT, b"z" * 16))
    await asyncio.sleep(0.05)
    check("T3 应答路径仍等 drain（确保送达）", writer3.drain_calls == 1,
          "drain_calls=%d" % writer3.drain_calls)
    task.cancel()
    try:
        await task
    except (asyncio.CancelledError, Exception):       # noqa: BLE001
        pass

    # ④ 发送缓冲超上限 ⇒ 丢这一帧（对齐 C 端"队满拒帧"），**不阻塞**。
    class _BigBufWriter(_WedgedWriter):
        def get_write_buffer_size(self):
            return 4 * 1024 * 1024 + 1

    writer4 = _BigBufWriter()
    sess4 = _make_session(writer4)
    ok4 = await asyncio.wait_for(
        sess4.send_publish(proto.TOPIC_VFD_STATUS, b"w" * 8, drain=False),
        timeout=1.0)
    check("T4 缓冲超上限：丢帧返回 False（不阻塞、不涨内存）", ok4 is False)


async def _run():
    await _case_no_stall()


def main():
    asyncio.run(_run())
    print("=" * 62)
    print("  汇总：PASS %d / FAIL %d" % (PASS, FAIL))
    return 0 if FAIL == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
