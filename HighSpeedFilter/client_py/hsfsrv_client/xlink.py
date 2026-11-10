# -*- coding: utf-8 -*-
"""XLink 客户端：与服务端对称的会话层（握手/认证/请求/订阅/推送）。

与服务端 hsfsrv/xlink.py 的 Session 是**镜像关系**：
    服务端 Session  ←→  客户端 Session
      收 Hello      ←→    发 Hello
      发 Challenge  ←→    收 Challenge 并派生应答
      主动 tick 推   ←→    收 Publish

[为什么客户端也要实现完整会话层] 不能直接复用服务端的 Server 类：
服务端做的是「接受连接、多会话、扇出推送」，客户端做的是「主动建链、
单会话、请求-应答配对」。两者状态机不同，混用会让两边的超时/重连语义
拧在一起。

[线程模型] 单线程：Qt 主线程。网络收发走 asyncio 事件循环，跑在
**独立线程**里，通过 Qt 信号（跨线程队列连接）把数据送回 UI。
不这么做的话，asyncio 的阻塞点会卡住 Qt 事件循环（界面卡死），
反之 Qt 的重绘会拖慢网络超时判定。
"""

import asyncio
import hashlib
import os
import hmac
import socket
import struct
import threading
import time

from . import ports
from . import proto


#: 客户端缺省连接端口 —— **从 ports.py 统一取**，不要在这里另写数字。
#: 改端口只需改 `hsfsrv_client/ports.py` 里的 PY_SERVER_PORT。
DEFAULT_PORT = ports.PY_SERVER_PORT

#: 登录结果码 → 用户可读文案（登录窗/状态栏用）。
#: [为什么要映射] 服务端线文本为了反枚举保持通用（"authentication rejected"），
#: 直接显示给现场用户等于没信息；这里按**结果码**给出准确原因，尤其把
#: 「账户已在线」从「口令错」里分出来（2026-11-09 根修：照着"口令错"重输
#: 只会把服务端账户失败计数推向锁定）。
AUTH_STATUS_TEXT = {
    proto.AUTHSTATUS_BAD_USER: "用户不存在",
    proto.AUTHSTATUS_BAD_PASSWORD: "口令错误",
    proto.AUTHSTATUS_LOCKED: "账户已锁定（失败次数过多）",
    proto.AUTHSTATUS_REJECTED: "服务端拒绝（该账户可能已在别处登录，"
                               "或会话数已达上限）",
    proto.AUTHSTATUS_NO_CREDENTIAL: "本端未配置登录凭据",
    proto.AUTHSTATUS_TIMEOUT: "认证超时（服务器不可达或网络异常）",
    proto.AUTHSTATUS_PROTOCOL: "认证帧畸形",
}


class SessionState(object):
    DISCONNECTED = "disconnected"
    CONNECTING = "connecting"
    CONNECTED = "connected"          # 已建链，等握手
    AUTHENTICATING = "authenticating"
    READY = "ready"


class RequestTimeout(Exception):
    """请求未在期限内拿到应答。"""


class XLinkClient(object):
    """XLink 客户端（单会话）。

    [回调都在网络线程上执行] 调用方（Qt UI）必须把回调里做的事
    投递到 UI 线程（本项目用 Qt 信号自动完成）。**不要在回调里直接改
    控件** —— 那会从非 UI 线程触碰 QWidget，Qt 会崩或行为不确定。
    """

    def __init__(self, host, port, user, password, log=None):
        self.host = host
        self.port = port
        self.user = user
        self.password = password
        self._log = log or (lambda msg: None)

        # ---- 状态 ----
        self.state = SessionState.DISCONNECTED
        self.auth_role = proto.ROLE_ANONYMOUS
        self.auth_error = ""
        self.last_receive_ms = 0
        self.last_send_ms = 0
        self._closed = False
        #: 本连接是否曾经登录成功过（READY 过一次）。决定认证失败是
        #: 「永久」（首登失败，留在登录窗）还是「瞬时」（重连被策略拒绝，
        #: 退避重试）。2026-11-09 根修，见 _on_auth_result。
        self._ever_ready = False
        #: 连续「策略性拒绝」次数（AUTHSTATUS_REJECTED），达
        #: XLINK_AUTH_REJECT_RETRY_LIMIT 才收手；成功即清零。
        self._auth_rejects = 0

        # ---- 协议 ----
        self.local_capability = proto.CAP_AUTH | proto.CAP_REQUEST_TIMEOUT
        self.remote_capability = 0
        self.subscriptions = set()
        self._request_id = 0
        self._pending = {}                  # requestId -> Future
        self._challenge = None              # (nonce, digest)
        self._auth_future = None

        # ---- 线程/循环 ----
        self._loop = None
        self._thread = None
        self._reader = None
        self._writer = None
        self._ready_event = threading.Event()

        # ---- 回调（均在网络线程） ----
        self.on_state_changed = None        # (state: str) -> None
        self.on_publish = None              # (topic_id: int, payload: bytes,
                                            #  data_type: int) -> None
        self.on_auth_result = None          # (ok: bool, role: int, err: str) -> None

        # ---- 重连 ----
        self.auto_reconnect = True
        self.reconnect_interval = 2.0
        self._reconnect_task = None

    # ==================================================================
    #                      线程与生命周期
    # ==================================================================
    def start(self, timeout=8.0):
        """启动网络线程并阻塞等待「登录完成」（或失败/超时）。

        :returns: (ok: bool, err: str)
        """
        # 一次显式登录 = 新会话意图：瞬时拒绝计数与「曾就绪」重新起算，
        # 保证首登被拒仍走永久失败（留在登录窗），不被上一轮的计数污染。
        self._auth_rejects = 0
        self._ever_ready = False
        self._thread = threading.Thread(target=self._thread_main,
                                        name="XLinkClient", daemon=True)
        self._thread.start()
        if not self._ready_event.wait(timeout):
            # [超时竞态的兜底] 唤醒发生在 wait 到期之后的极窄窗口里时，
            # auth_error 已写入而 event 刚 set：此时仍应报真实原因，
            # 不要退回笼统的「连接超时」。auth_error 为空（纯网络失败/
            # 仍在自动重连）时维持原口径。
            if self.auth_error:
                return False, self.auth_error
            return False, "连接超时"
        self._ready_event.wait(0.01)
        if self.state != SessionState.READY:
            return False, self.auth_error or "未就绪"
        return True, ""

    def _thread_main(self):
        """网络线程入口：建一个独立事件循环，跑到底。"""
        self._loop = asyncio.new_event_loop()
        asyncio.set_event_loop(self._loop)
        try:
            self._loop.run_until_complete(self._run())
        except Exception as exc:                        # noqa: BLE001
            self._log("客户端网络线程异常：%s: %s" % (type(exc).__name__, exc))
        finally:
            try:
                self._loop.run_until_complete(self._loop.shutdown_asyncgens())
            except Exception:                           # noqa: BLE001
                pass
            self._loop.close()
            self._set_state(SessionState.DISCONNECTED)

    async def _run(self):
        """主循环：连 → 握手 → 认证 → 收发；断了按策略重连。"""
        while not self._closed:
            try:
                await self._connect_once()
            except Exception as exc:                    # noqa: BLE001
                if not self._closed:
                    self._log("连接失败：%s: %s" % (type(exc).__name__, exc))
            if self._closed or not self.auto_reconnect:
                break
            await asyncio.sleep(self.reconnect_interval)
            if not self.state == SessionState.READY:
                self._set_state(SessionState.CONNECTING)

    async def _connect_once(self):
        self._set_state(SessionState.CONNECTING)
        self._reader, self._writer = await asyncio.wait_for(
            asyncio.open_connection(self.host, self.port), timeout=5.0)
        try:
            sock = self._writer.get_extra_info("socket")
            if sock is not None:
                sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        except OSError:
            pass
        self._set_state(SessionState.CONNECTED)
        self.last_receive_ms = self._now_ms()
        self.last_send_ms = self.last_receive_ms

        # 主动发 Hello（服务端会回 HelloAck 并进入认证等待）。
        await self._send_session(proto.SESSION_HELLO,
                                 proto.encode_hello(self.local_capability))
        await self._pump()

    def close(self, join_timeout=3.0):
        """主动断开（幂等）。

        :param join_timeout: 等待网络线程收尾的秒数；>0 时同步 join——
            **close 返回后保证不再有任何回调/信号从本类发出**（退出竞态
            根修 2026-11-09：此前 close 是异投递即返回，解释器终结阶段
            网络线程仍可能在发状态信号，而主线程的 Qt 对象已析构 →
            pyqtBoundSignal_emit 段错误，见台账 §六）。
        """
        self._closed = True
        if self._loop is not None and not self._loop.is_closed():
            try:
                self._loop.call_soon_threadsafe(self._cancel_all)
            except RuntimeError:
                pass
        if join_timeout and self._thread is not None and self._thread.is_alive():
            self._thread.join(timeout=join_timeout)

    def _cancel_all(self):
        for fut in list(self._pending.values()):
            if not fut.done():
                fut.set_exception(RequestTimeout("连接已关闭"))
        self._pending.clear()

    def _set_state(self, state):
        if self.state == state:
            return
        if self._closed:
            # [退出竞态闸 2026-11-09] close() 之后重连迭代可能还剩最后一拍
            # （await sleep 的重连间隔未到期）：一切状态迁移静默——此时解释
            # 器可能正在终结，on_state_changed(→bridge._on_state→Qt emit)
            # 的接收端已析构，emit 即段错误（faulthandler 全栈实证：
            # _run→_connect_once→_teardown→_set_state）。
            self.state = state
            return
        self.state = state
        self._log("状态 → %s" % state)
        if self.on_state_changed:
            try:
                self.on_state_changed(state)
            except Exception as exc:                    # noqa: BLE001
                self._log("on_state_changed 异常：%s" % exc)
        if state == SessionState.READY:
            self._ready_event.set()
        elif state == SessionState.DISCONNECTED and self.auth_error:
            # [认证失败终局唤醒] 失败路径均「先写 auth_error，后 _teardown()
            # → _set_state(DISCONNECTED)」：原先只有 READY 会 set，登录失败
            # 只能靠 start() 的 8s 超时兜底、还被 :113 误报成「连接超时」。
            # 这里 set 让 start() 立即返回，走既有 :115-116 把 auth_error
            # 作为真实文案交回。带 auth_error 的条件使普通网络失败（空）
            # 仍保持原来的等待/超时口径，行为面收敛到认证失败一类。
            self._log("认证失败终局：唤醒登录等待（%s）" % self.auth_error)
            self._ready_event.set()

    @staticmethod
    def _now_ms():
        return int(time.monotonic() * 1000)

    # ==================================================================
    #                          收发
    # ==================================================================
    async def _send_frame(self, msg_class, msg_code, data_type,
                          payload=b"", request_id=0):
        if self._writer is None or self._writer.is_closing():
            return False
        try:
            head = proto.encode_header(len(payload), msg_class, msg_code,
                                       request_id, data_type)
            self._writer.write(head + payload)
            await self._writer.drain()
            self.last_send_ms = self._now_ms()
            return True
        except (ConnectionError, OSError, RuntimeError):
            return False

    async def _send_session(self, code, payload=b""):
        return await self._send_frame(proto.MSGCLASS_SESSION, code,
                                      proto.DATATYPE_RAW, payload)

    async def _pump(self):
        """收包主循环 + 心跳巡检。

        [不要用 wait_for 包 readexactly — 实锅]
        `asyncio.wait_for(readerexactly(16), timeout=0.5)` 超时时会取消
        readexactly，**它已经读到的部分字节随之丢失**。半帧被吃掉后，
        之后每一帧都按错位的字节切片 → 帧头全非法 → 一条数据也收不到 →
        6 秒静默超时误判断链（板上实测：登录 6 秒后必掉线）。
        正确做法：读**不带超时**，链路是否还活着交给 _tick_loop 判静默
        （那条路径会在超时时主动 teardown，使这里的读抛异常）。
        """
        tick_task = asyncio.ensure_future(self._tick_loop())
        try:
            while not self._closed:
                head = await self._reader.readexactly(proto.XLINK_HEADER_SIZE)
                hdr = proto.decode_header(head)
                if hdr is None:
                    self._log("帧头非法 → 断链")
                    break
                plen, mclass, mcode, rid, dtype = hdr
                payload = b""
                if plen:
                    payload = await self._reader.readexactly(plen)
                self.last_receive_ms = self._now_ms()
                await self._on_frame(mclass, mcode, rid, dtype, payload)
        except (asyncio.IncompleteReadError, ConnectionError, OSError):
            pass
        finally:
            tick_task.cancel()
            try:
                await tick_task
            except (asyncio.CancelledError, Exception):  # noqa: BLE001
                pass
            await self._teardown()

    async def _teardown(self):
        if self._writer is not None:
            try:
                self._writer.close()
            except (ConnectionError, OSError, RuntimeError):
                pass
        self._reader = None
        self._writer = None
        # [订阅去重集合必须随连接作废] 服务端的订阅登记在**每条会话**上；
        # 断线后旧会话的订阅已不存在，若本集合还留着旧主题，重连后
        # subscribe() 的去重分支会吞掉补订帧 ⇒ 新会话在服务端零订阅，
        # 界面永远收不到推送（表现为重连后数据全部陈旧，直到切页且主题
        # 集合恰好变化）。与 bridge._subscribed 在 READY 的清空配套。
        self.subscriptions = set()
        self._set_state(SessionState.DISCONNECTED)
        self._cancel_all()

    async def _tick_loop(self):
        """心跳：静默达间隔就发一帧；静默超时判链路断开。

        [为什么客户端也要判超时] 服务端可能已经死了但 TCP 还没报错
        （半开连接）。不判超时的话界面会一直显示旧数据、用户以为
        「数据没变」——比直接报断开更危险。
        """
        try:
            while True:
                await asyncio.sleep(proto.XLINK_HEARTBEAT_TICK_MS / 1000.0)
                now = self._now_ms()
                # [只看 last_send_ms，不看 last_receive_ms —— 实锅]
                # 早先用 max(send, recv) 判「静默」：订阅了 100ms 一拍的遥测后
                # recv 每 100ms 刷新一次，条件**永不成立** ⇒ 一帧心跳都不发。
                # 服务端那边只看「收没收到东西」，于是 6 秒判静默超时断链
                # （板上实测：登录后必掉线，且服务端日志明确写 6xxx ms 无帧）。
                # 心跳是**我方主动**向对端证明存活，只该看自己的发送时间。
                if (self.state == SessionState.READY
                        and now - self.last_send_ms
                        >= proto.XLINK_HEARTBEAT_INTERVAL_MS):
                    await self._send_session(proto.SESSION_HEARTBEAT)
                if (self.state in (SessionState.READY,
                                   SessionState.AUTHENTICATING)
                        and now - self.last_receive_ms
                        > proto.XLINK_HEARTBEAT_TIMEOUT_MS):
                    self._log("静默超时 → 断链")
                    await self._teardown()
                    return
        except asyncio.CancelledError:
            raise

    # ==================================================================
    #                          帧分派
    # ==================================================================
    async def _on_frame(self, mclass, mcode, rid, dtype, payload):
        if mclass == proto.MSGCLASS_SESSION:
            await self._on_session(mcode, payload)
            return
        if mclass == proto.MSGCLASS_RESPONSE:
            fut = self._pending.pop(rid, None)
            if os.environ.get("HSF_CLIENT_DEBUG") == "1":
                import sys as _sys
                print("[dbg %s] << resp code=0x%04X rid=%d %s"
                      % (time.strftime("%H:%M:%S"), mcode, rid,
                         "命中" if fut is not None else "**无匹配请求**"),
                      file=_sys.stderr, flush=True)
            if fut is not None and not fut.done():
                fut.set_result((mcode, dtype, payload))
            return
        if mclass == proto.MSGCLASS_PUBLISH:
            if len(payload) < 4:
                return
            topic = struct.unpack("<I", payload[:4])[0]
            body = payload[4:]
            if self.on_publish:
                try:
                    self.on_publish(topic, body, dtype)
                except Exception as exc:                # noqa: BLE001
                    self._log("on_publish 异常：%s" % exc)
            return
        # Subscribe/Unsubscribe/File：客户端不消费，忽略。

    async def _on_session(self, code, payload):
        if code in (proto.SESSION_HELLO, proto.SESSION_HELLO_ACK):
            hello = proto.decode_hello(payload)
            if hello is None:
                return
            version, capability = hello
            if (version >> 8) != (proto.XLINK_VERSION >> 8):
                self._log("服务端协议主版本不符（0x%04X）" % version)
                self.auth_error = "协议版本不兼容"
                await self._teardown()
                return
            self.remote_capability = capability & self.local_capability
            if code == proto.SESSION_HELLO:
                await self._send_session(
                    proto.SESSION_HELLO_ACK,
                    proto.encode_hello(self.local_capability))
            # 握手完成 → 若服务端要求认证就发登录请求。
            if (self.state == SessionState.CONNECTED
                    and (capability & proto.CAP_AUTH)):
                await self._start_auth()
        elif code == proto.SESSION_AUTH_CHALLENGE:
            await self._on_challenge(payload)
        elif code == proto.SESSION_AUTH_RESULT:
            await self._on_auth_result(payload)
        elif code == proto.SESSION_HEARTBEAT:
            return
        elif code == proto.SESSION_GOODBYE:
            reason = payload[0] if payload else 0
            self._log("服务端断开（原因 %d）" % reason)
            if reason == proto.GOODBYE_AUTH_FAILED:
                self.auth_error = "认证失败"
            await self._teardown()

    # ---- 认证 ----
    async def _start_auth(self):
        if not self.user:
            self.auth_error = "未提供用户名"
            await self._teardown()
            return
        self._set_state(SessionState.AUTHENTICATING)
        name = self.user.encode("utf-8")
        await self._send_session(proto.SESSION_AUTH_REQUEST,
                                 bytes([len(name)]) + name)

    async def _on_challenge(self, payload):
        """收到挑战：派生 H = HMAC^N(口令, 盐)，回 HMAC(H, nonce)。"""
        if len(payload) < 8:
            self.auth_error = "挑战载荷畸形"
            await self._teardown()
            return
        method = payload[0]
        salt_len = payload[1]
        iterations = struct.unpack("<I", payload[4:8])[0]
        salt = payload[8:8 + salt_len]
        nonce = payload[8 + salt_len:8 + salt_len + proto.XLINK_AUTH_NONCE_BYTES]
        if (salt_len != proto.XLINK_AUTH_SALT_BYTES
                or len(nonce) != proto.XLINK_AUTH_NONCE_BYTES
                or iterations < 1):
            self.auth_error = "挑战参数不合法"
            await self._teardown()
            return
        # 派生放在线程池外会卡住会话（10000 轮 HMAC 约几十毫秒）：
        # 直接同步算，因为此刻本来就在等登录结果，早几十毫秒晚几十毫秒无差。
        key = self.password.encode("utf-8")
        digest = hmac.new(key, salt, hashlib.sha256).digest()
        for _ in range(iterations - 1):
            digest = hmac.new(key, digest, hashlib.sha256).digest()
        self._challenge = (nonce, digest)
        response = hmac.new(digest, nonce, hashlib.sha256).digest()
        await self._send_session(
            proto.SESSION_AUTH_RESPONSE,
            bytes([method, 0]) + struct.pack("<H", len(response)) + response)

    async def _on_auth_result(self, payload):
        if len(payload) < 4:
            self.auth_error = "认证结果畸形"
            await self._teardown()
            return
        status = payload[0]
        role = payload[1]
        text_len = struct.unpack("<H", payload[2:4])[0]
        text = payload[4:4 + text_len].decode("utf-8", "replace")
        if status != proto.AUTHSTATUS_OK:
            self.auth_role = proto.ROLE_ANONYMOUS
            # 线文本保持通用（反枚举），界面按**结果码**给准确原因。
            self.auth_error = (AUTH_STATUS_TEXT.get(status)
                               or text or "认证被拒（%d）" % status)
            # [永久 vs 瞬时 —— 2026-11-09 根修]
            # 只有**账户本身**的失败才永久收手：用户不存在 / 口令错 /
            # 账户锁定 / 无凭据。策略性拒绝（REJECTED：同账并发、会话数
            # 上限）与链路噪声（TIMEOUT/PROTOCOL）是**瞬时**的：客户端断线
            # 重连时旧会话还没被服务端静默回收（~6s）就会撞上，若按永久
            # 处理则网络线程整个退出、界面永久全 `--`（实锅：模拟模式下
            # 客户端偶发无数据，换个时间重登又好了）。
            # 瞬时失败保持 auto_reconnect=True 走退避重试，超过
            # XLINK_AUTH_REJECT_RETRY_LIMIT 次才终局；首登（从未 READY）
            # 仍是永久失败：登录窗要原地显示"账户已在线/被拒绝"，
            # 不在后台无限重连。
            transient = status in (proto.AUTHSTATUS_REJECTED,
                                   proto.AUTHSTATUS_TIMEOUT,
                                   proto.AUTHSTATUS_PROTOCOL)
            retry = bool(transient and self._ever_ready
                         and self.auto_reconnect)
            if retry:
                self._auth_rejects += 1
                if self._auth_rejects > proto.XLINK_AUTH_REJECT_RETRY_LIMIT:
                    retry = False
            if not retry:
                self.auto_reconnect = False
            if retry:
                self._log("登录被拒（%s）：账户可能已在线，"
                          "%.0fs 后自动重试（%d/%d）"
                          % (self.auth_error, self.reconnect_interval,
                             self._auth_rejects,
                             proto.XLINK_AUTH_REJECT_RETRY_LIMIT))
            else:
                self._log("登录失败：%s" % self.auth_error)
            if self.on_auth_result:
                self.on_auth_result(False, 0, self.auth_error)
            await self._teardown()
            return
        self.auth_role = role
        self.auth_error = ""
        self._challenge = None
        self._ever_ready = True
        self._auth_rejects = 0
        self._set_state(SessionState.READY)
        self._log("登录成功（%s，角色 %d）" % (self.user, role))
        if self.on_auth_result:
            self.on_auth_result(True, role, "")

    # ==================================================================
    #                        业务 API（网络线程）
    # ==================================================================
    async def request(self, msg_code, payload=b"",
                      data_type=proto.DATATYPE_BSON, timeout=3.0):
        """发请求并等应答，返回 (msgCode, dataType, payload)。

        :raises RequestTimeout: 超时或链路已断。
        """
        if self.state != SessionState.READY:
            raise RequestTimeout("未连接")
        self._request_id = (self._request_id + 1) & 0xFFFFFFFF
        rid = self._request_id
        fut = self._loop.create_future()
        self._pending[rid] = fut
        ok = await self._send_frame(proto.MSGCLASS_REQUEST, msg_code,
                                    data_type, payload, rid)
        if not ok:
            self._pending.pop(rid, None)
            raise RequestTimeout("发送失败")
        try:
            return await asyncio.wait_for(fut, timeout=timeout)
        except asyncio.TimeoutError:
            self._pending.pop(rid, None)
            raise RequestTimeout("请求超时（cmd=0x%04X）" % msg_code)

    async def subscribe(self, topic_id):
        if topic_id in self.subscriptions:
            return True
        ok = await self._send_frame(proto.MSGCLASS_SUBSCRIBE,
                                    topic_id & 0xFFFF, proto.DATATYPE_RAW,
                                    proto.pack_topic(topic_id))
        if ok:
            self.subscriptions.add(topic_id)
        return ok

    async def unsubscribe(self, topic_id):
        if topic_id not in self.subscriptions:
            return True
        ok = await self._send_frame(proto.MSGCLASS_UNSUBSCRIBE,
                                    topic_id & 0xFFFF, proto.DATATYPE_RAW,
                                    proto.pack_topic(topic_id))
        if ok:
            self.subscriptions.discard(topic_id)
        return ok

    # ==================================================================
    #                    同步包装（给 Qt 线程用）
    # ==================================================================
    def call_soon(self, coro):
        """把协程投递到网络线程（从 UI 线程调用）。"""
        if self._loop is None or self._loop.is_closed():
            return None
        try:
            return asyncio.run_coroutine_threadsafe(coro, self._loop)
        except RuntimeError:
            return None

    def request_async(self, msg_code, payload=b"",
                      data_type=proto.DATATYPE_BSON, timeout=3.0,
                      on_done=None, on_error=None):
        """异步请求：结果经回调返回（**回调在网络线程**，需自行投递到 UI）。

        :param on_done: (msgCode, dataType, payload) -> None
        :param on_error: (err: str) -> None
        """
        async def _do():
            try:
                result = await self.request(msg_code, payload, data_type,
                                            timeout)
                if on_done:
                    on_done(*result)
            except Exception as exc:                    # noqa: BLE001
                if on_error:
                    on_error(str(exc))
        return self.call_soon(_do())

    def subscribe_async(self, topic_id):
        return self.call_soon(self.subscribe(topic_id))

    def unsubscribe_async(self, topic_id):
        return self.call_soon(self.unsubscribe(topic_id))
