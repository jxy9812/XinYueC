# -*- coding: utf-8 -*-
"""XLink 上游链路：**一条连接**服务多个浏览器会话。

与 client_py 的 `hsfsrv_client/xlink.py` 的关系
================================================

那一份是「一个 Qt 客户端 = 一条 XLink 连接」，每个进程自己握手认证、
自己订阅、自己发请求。网页服务器不能照抄，原因有三条，每条都是硬约束：

1) **服务端的 root 单会话策略**
   `hsfsrv/app.py: policy_blocks_login` —— 一个 root 只允许一个在线会话，
   第二个 root 登录会被直接拒。若每个浏览器标签页各建一条 XLink 连接、
   都用 root 登录，第二个标签页必然登不上（现象：第一个窗口正常，
   第二个窗口一直提示"该账户已在别处登录"）。

2) **485 总线是排他资源**
   同一主题被 N 条连接各订阅一次，服务端就要推 N 份；更要命的是
   快照命令（CMD_VFD_SNAPSHOT / CMD_DIGITAL_SNAPSHOT）会触发**现读设备**，
   N 个浏览器同时点"请求快照"就是 N 倍并发读同一条 485 —— 主站撞拍、
   应答丢失、界面表现为"偶尔读不到"。这个坑在服务端文档 03 里记过。

3) **页面切换的订阅语义会互相踩**
   Qt 客户端切页时把订阅集合**收敛**成本页需要的主题（`set_topics`）。
   多连接时每个浏览器各收敛各的：A 在数字量页想订数字量、
   B 在变频器页想退订数字量 —— 服务端只能按"该主题是否还有人要"合并。
   这条合并逻辑必须在一个进程里做，否则要么漏订（界面卡在 --），
   要么退订早了（另一端的页面突然没数据）。

所以本模块的设计是：**一条上游连接 + 多路复用**。

    ┌── 浏览器 A ─┐
    ├── 浏览器 B ─┼─ SSE ─→ WebHub ─→ 一条 XLink ─→ 服务端
    └── 浏览器 C ─┘

    · 上行请求：`request()` 带 requestId 配对，任何调用方都能并发用
    · 下行推送：一次收到，扇出给所有 SSE 订阅者
    · 订阅集合：由 WebHub 按「所有在线浏览器的并集」收敛，只增删变化项

[与 client_py 那份的代码差异]
  · 去掉 Qt 相关的一切（信号、`call_soon_threadsafe`）—— 本进程只有
    asyncio 一个事件循环，无跨线程问题
  · `start()` 不再阻塞等待，改为后台任务；重连由内部循环做
  · 增加 `on_publish` 的多订阅者扇出与连接状态事件回调

[为什么"口令错不重连"] 与客户端同口径：口令错属永久失败，无限重连会把
服务端账户的失败计数打满（`XLINK_AUTH_FAILURE_LIMIT = 3` 即锁账户），
现场表现为"输错一次口令后账户被锁"。故认证被拒后**停止自动重连**，
把原因报给上层（网页版据此在界面上提示）。
"""

import asyncio
import hashlib
import hmac
import socket
import struct
import time

from . import ports
from . import proto


#: 缺省连接端口 —— 从 ports.py 统一取，不要在这里另写数字。
DEFAULT_PORT = ports.PY_SERVER_PORT


# ============================================================================
#                      口令派生（H = HMAC^N(口令, 盐)）
# ============================================================================
#
# [为什么挪出事件循环] 10000 轮 HMAC-SHA256 是**纯 CPU**的几十毫秒级活，
# 原来在事件循环里同步跑 ⇒ 一次登录洪泛就能把整个循环占满 ⇒ SSE 心跳
# 停发 ⇒ 上游按「静默 6s」判线断链 ⇒ 现场看起来「整个控制台死了」。
# ⇒ 派生放到线程里跑（事件循环只 await 结果）。
#
# [为什么还能缓存] 服务端下发的盐是**账户存储的盐**（命中账户时固定，
# 见 XLinkSession.c 的 "挑战必须携带账户存储的盐"）⇒ 同一账户同一口令
# 的 H 可以复用 ⇒ 第二次登录不必再跑 10000 轮。
#
# [键里为什么带口令指纹] 盐不变而口令被管理员改过时，只按 (user, salt)
# 命中会拿到**旧 H** ⇒ 登录永远失败（且难查）。⇒ 键里再带口令的
# sha256 指纹（一次 32 字节摘要，代价可忽略）。
#
# [为什么放模块级] 每次网页登录都会新建一个一次性探测连接（XLinkUpstream
# 实例），实例级缓存命中不了。
#
# [安全口径] 缓存里存的是**派生摘要 H**，不是口令；但持有 (salt, H) 就能
# 自算 HMAC(H, nonce) 过认证 ⇒ 它的敏感度≈口令。⇒ 本缓存与上游连接对象
# 里本来就明文驻留的口令**同一威胁面**（能读进程内存的人本来就拿到口令）
# ⇒ 不额外扩大风险面；进程重启即清空。

#: 派生密钥缓存：(user, salt, iterations, 口令指纹) → H。
_DERIVE_CACHE = {}
#: 缓存条数上限（防"不存在的账户"路径的随机盐把缓存撑爆；超限即整体清空）。
_DERIVE_CACHE_MAX = 64


def derive_hash_cached(user, password, salt, iterations):
    """派生 H = HMAC^N(口令, 盐)（**同步**，调用方负责放线程里跑）。"""
    pw = password.encode("utf-8")
    key = (user, bytes(salt), int(iterations),
           hashlib.sha256(pw).digest())
    hit = _DERIVE_CACHE.get(key)
    if hit is not None:
        return hit
    digest = hmac.new(pw, salt, hashlib.sha256).digest()
    for _ in range(int(iterations) - 1):
        digest = hmac.new(pw, digest, hashlib.sha256).digest()
    if len(_DERIVE_CACHE) >= _DERIVE_CACHE_MAX:
        _DERIVE_CACHE.clear()
    _DERIVE_CACHE[key] = digest
    return digest


async def run_in_thread(func, *args):
    """把阻塞函数丢到线程池跑（兼容没有 `asyncio.to_thread` 的老 Python）。"""
    loop = asyncio.get_running_loop()
    return await loop.run_in_executor(None, func, *args)


class SessionState(object):
    DISCONNECTED = "disconnected"
    CONNECTING = "connecting"
    CONNECTED = "connected"          # 已建链，等握手
    AUTHENTICATING = "authenticating"
    READY = "ready"


class RequestTimeout(Exception):
    """请求未在期限内拿到应答（或链路已断）。"""


class AuthRejected(Exception):
    """认证被服务端**永久**拒绝（口令错/账户锁/无此账户）。

    [为什么要单独一个异常类型] 它与"网络断了"的处理完全不同：
      · 网络断 → 无限重连（现场拔网线很常见）
      · 永久拒 → **立即停止重连**，否则会把失败计数打满锁账户
    """


class AuthTransient(Exception):
    """认证被**瞬时**拒绝（策略闸：同账并发「该账户已在线」/会话数达上限）。

    [为什么与 AuthRejected 分开] 凭据可能是对的，旧会话被服务端按静默
    超时回收后重试即通过——客户端同口径（client_py/xlink.py）：瞬时拒绝
    退避重试，达 `proto.XLINK_AUTH_REJECT_RETRY_LIMIT`（30 次）才终局。
    webui 主链路在首个浏览器登录后立刻起连，若同账并发关闭且主链路账号
    与刚认证完的探测连接同账，就正好踩这个窗口。
    """


class XLinkUpstream(object):
    """单条上游 XLink 连接（握手 / 认证 / 请求 / 订阅 / 推送）。

    [所有回调都在事件循环里同步执行] 调用方（WebHub）在回调里扇出给
    SSE 连接即可；**不要**在回调里 await（本类不等待回调完成）。
    """

    def __init__(self, host, port, user, password, log=None,
                 reconnect_interval=2.0):
        self.host = host
        self.port = int(port)
        self.user = user
        self.password = password
        self._log = log or (lambda msg: None)

        # ---- 状态 ----
        self.state = SessionState.DISCONNECTED
        self.auth_role = proto.ROLE_ANONYMOUS
        self.auth_error = ""
        self.last_receive_ms = 0
        self.last_send_ms = 0
        self.connected_since_ms = 0
        self.reconnect_count = 0
        self._closed = False

        # ---- 协议 ----
        self.local_capability = proto.CAP_AUTH | proto.CAP_REQUEST_TIMEOUT
        self.remote_capability = 0
        #: 当前**已向服务端生效**的订阅集合（重连后由 WebHub 重新下发）。
        self.subscriptions = set()
        self._request_id = 0
        self._pending = {}                  # requestId -> Future

        # ---- 事件循环/传输 ----
        self._loop = None
        self._reader = None
        self._writer = None
        self._task = None

        # ---- 回调（同步，事件循环内） ----
        #: (state: str, detail: str) -> None
        self.on_state_changed = None
        #: (topic_id: int, payload: bytes, data_type: int) -> None
        self.on_publish = None
        #: (ready: bool) -> None —— 链路"没就绪↔就绪"翻转时调
        self.on_ready_changed = None

        # ---- 重连 ----
        self.auto_reconnect = True
        self.reconnect_interval = float(reconnect_interval)
        #: 认证被**永久**拒后置 True，不再自动重连（见模块文档）。
        self.auth_blocked = False
        #: 瞬时拒绝（REJECTED）连续计数；达上限转终局（同客户端口径）。
        self._auth_reject_count = 0

    # ==================================================================
    #                          生命周期
    # ==================================================================
    def start(self):
        """在当前事件循环里起后台任务。**不阻塞**（区别于客户端那份）。

        [惰性启动] 本方法可以在任意时刻调用（包括链路已经 close 之后
        重新开链——先把 `_closed` 翻回来）。网页服务器启动时**不**自动起链，
        首个浏览器登录成功后才由 WebHub.ensure_upstream() 调它，
        避免常驻空占一个上游会话（root 单会话策略下会把桌面客户端挡在外面）。
        """
        try:
            self._loop = asyncio.get_running_loop()
        except RuntimeError:
            self._loop = asyncio.get_event_loop()
        if self._closed:
            # close() 之后允许重新起步（惰性启动/重开链共用这一条路）。
            self._closed = False
            self.auth_blocked = False
            self._auth_reject_count = 0
        if self._task is None or self._task.done():
            self._task = self._loop.create_task(self._run())
        return self._task

    async def close(self):
        """主动断开（幂等）。"""
        self._closed = True
        await self._teardown()
        if self._task is not None and not self._task.done():
            self._task.cancel()
            try:
                await self._task
            except (asyncio.CancelledError, Exception):     # noqa: BLE001
                pass

    async def _run(self):
        """主循环：连 → 握手 → 认证 → 收发；断了按策略重连。"""
        while not self._closed:
            try:
                await self._connect_once()
            except asyncio.CancelledError:
                raise
            except AuthRejected as exc:
                # 永久失败（口令错/账户锁）：不重连，等改配置/凭据后重开链。
                self.auth_blocked = True
                self.auth_error = str(exc)
                self._set_state(SessionState.DISCONNECTED, str(exc))
                self._log("认证被拒，停止自动重连：%s" % exc)
                return
            except AuthTransient as exc:
                # 瞬时失败（策略闸）：退避重试；达上限才终局（同客户端口径）。
                self._auth_reject_count += 1
                self.auth_error = str(exc)
                self._set_state(SessionState.DISCONNECTED, str(exc))
                if self._auth_reject_count >= proto.XLINK_AUTH_REJECT_RETRY_LIMIT:
                    self.auth_blocked = True
                    self._log("瞬时拒绝连续 %d 次，停止重连：%s"
                              % (self._auth_reject_count, exc))
                    return
                self._log("认证被暂时拒绝（第 %d/%d 次）：%s"
                          % (self._auth_reject_count,
                             proto.XLINK_AUTH_REJECT_RETRY_LIMIT, exc))
            except Exception as exc:                        # noqa: BLE001
                if not self._closed:
                    self._log("上游连接失败：%s: %s"
                              % (type(exc).__name__, exc))
            if self._closed:
                return
            if not self.auto_reconnect:
                return
            self.reconnect_count += 1
            self._set_state(SessionState.CONNECTING,
                            "第 %d 次重连，%0.1fs 后"
                            % (self.reconnect_count, self.reconnect_interval))
            try:
                await asyncio.sleep(self.reconnect_interval)
            except asyncio.CancelledError:
                raise

    async def _connect_once(self):
        self._set_state(SessionState.CONNECTING,
                        "%s:%d" % (self.host, self.port))
        self._reader, self._writer = await asyncio.wait_for(
            asyncio.open_connection(self.host, self.port), timeout=5.0)
        try:
            sock = self._writer.get_extra_info("socket")
            if sock is not None:
                # [关 Nagle] 遥测帧都是几十字节的小包；开着 Nagle 会攒包，
                # 服务端那边看起来就是"推送间隔忽长忽短"。
                sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        except OSError:
            pass
        self._set_state(SessionState.CONNECTED,
                        "已建链 %s:%d" % (self.host, self.port))
        self.last_receive_ms = self._now_ms()
        self.last_send_ms = self.last_receive_ms

        # 主动发 Hello（服务端会回 HelloAck 并进入认证等待）。
        await self._send_session(proto.SESSION_HELLO,
                                 proto.encode_hello(self.local_capability))
        await self._pump()

    def _set_state(self, state, detail=""):
        changed = self.state != state
        self.state = state
        if changed:
            self._log("上游状态 → %s%s"
                      % (state, ("（%s）" % detail) if detail else ""))
        if self.on_state_changed:
            try:
                self.on_state_changed(state, detail)
            except Exception as exc:                        # noqa: BLE001
                self._log("on_state_changed 异常：%s" % exc)
        if changed and state == SessionState.READY:
            self.connected_since_ms = self._now_ms()
            self._notify_ready(True)
        elif changed and state == SessionState.DISCONNECTED:
            self.connected_since_ms = 0
            self._notify_ready(False)

    def _notify_ready(self, ready):
        if self.on_ready_changed:
            try:
                self.on_ready_changed(ready)
            except Exception as exc:                        # noqa: BLE001
                self._log("on_ready_changed 异常：%s" % exc)

    @staticmethod
    def _now_ms():
        return int(time.monotonic() * 1000)

    def is_ready(self):
        return self.state == SessionState.READY

    def task_running(self):
        """后台主循环任务是否在跑（惰性启动的判据）。"""
        return self._task is not None and not self._task.done()

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

        [不要用 wait_for 包 readexactly —— 与客户端同一实锅]
        `asyncio.wait_for(readexactly(16), timeout=0.5)` 超时时会取消
        readexactly，**它已经读到的部分字节随之丢失**。半帧被吃掉后，
        之后每一帧都按错位的字节切片 → 帧头全非法 → 一条数据也收不到 →
        静默超时误判断链。正确做法：读**不带超时**，链路存活交给
        `_tick_loop` 判静默（那条路径会在超时时主动 teardown）。
        """
        tick_task = asyncio.ensure_future(self._tick_loop())
        try:
            while not self._closed:
                head = await self._reader.readexactly(proto.XLINK_HEADER_SIZE)
                hdr = proto.decode_header(head)
                if hdr is None:
                    self._log("上游帧头非法 → 断链")
                    break
                plen, mclass, mcode, rid, dtype = hdr
                payload = b""
                if plen:
                    payload = await self._reader.readexactly(plen)
                self.last_receive_ms = self._now_ms()
                await self._on_frame(mclass, mcode, rid, dtype, payload)
        except (asyncio.IncompleteReadError, ConnectionError, OSError):
            pass
        except asyncio.CancelledError:
            raise
        finally:
            tick_task.cancel()
            try:
                await tick_task
            except (asyncio.CancelledError, Exception):     # noqa: BLE001
                pass
            await self._teardown()

    async def _teardown(self):
        writer, self._writer = self._writer, None
        self._reader = None
        # 订阅表随旧会话作废：重连后必须重订（服务端不记跨会话订阅）。
        self.subscriptions.clear()
        if writer is not None:
            try:
                writer.close()
            except (ConnectionError, OSError, RuntimeError):
                pass
        self._fail_all_pending("连接已断开")
        self._set_state(SessionState.DISCONNECTED, "连接已断开")

    def _fail_all_pending(self, reason):
        for fut in list(self._pending.values()):
            if not fut.done():
                fut.set_exception(RequestTimeout(reason))
        self._pending.clear()

    async def _tick_loop(self):
        """心跳：静默达间隔就发一帧；静默超时判链路断开。

        [只看 last_send_ms 判"该发心跳" —— 实锅]
        早先用 max(send, recv) 判静默：订阅了 100ms 一拍的遥测后 recv
        每 100ms 刷新一次，条件**永不成立** ⇒ 一帧心跳都不发；服务端
        只看"收没收到东西"，于是 6 秒判静默超时断链。
        心跳是**我方主动**向对端证明存活，只该看自己的发送时间。
        """
        try:
            while True:
                await asyncio.sleep(proto.XLINK_HEARTBEAT_TICK_MS / 1000.0)
                now = self._now_ms()
                if (self.state == SessionState.READY
                        and now - self.last_send_ms
                        >= proto.XLINK_HEARTBEAT_INTERVAL_MS):
                    await self._send_session(proto.SESSION_HEARTBEAT)
                if (self.state in (SessionState.READY,
                                   SessionState.AUTHENTICATING)
                        and now - self.last_receive_ms
                        > proto.XLINK_HEARTBEAT_TIMEOUT_MS):
                    self._log("上游静默超时 → 断链")
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
                except Exception as exc:                    # noqa: BLE001
                    self._log("on_publish 异常：%s" % exc)
            return
        # Subscribe/Unsubscribe/File：本端不消费，忽略。

    async def _on_session(self, code, payload):
        if code in (proto.SESSION_HELLO, proto.SESSION_HELLO_ACK):
            hello = proto.decode_hello(payload)
            if hello is None:
                return
            version, capability = hello
            if (version >> 8) != (proto.XLINK_VERSION >> 8):
                self._log("服务端协议主版本不符（0x%04X）" % version)
                raise AuthRejected("协议版本不兼容（服务端 0x%04X）" % version)
            self.remote_capability = capability & self.local_capability
            if code == proto.SESSION_HELLO:
                await self._send_session(
                    proto.SESSION_HELLO_ACK,
                    proto.encode_hello(self.local_capability))
            if (self.state == SessionState.CONNECTED
                    and (capability & proto.CAP_AUTH)):
                await self._start_auth()
            return
        if code == proto.SESSION_AUTH_CHALLENGE:
            await self._on_challenge(payload)
            return
        if code == proto.SESSION_AUTH_RESULT:
            await self._on_auth_result(payload)
            return
        if code == proto.SESSION_HEARTBEAT:
            return
        if code == proto.SESSION_GOODBYE:
            reason = payload[0] if payload else 0
            text = GOODBYE_TEXT.get(reason, "原因 %d" % reason)
            self._log("服务端断开：%s" % text)
            if reason in (proto.GOODBYE_AUTH_FAILED,
                          proto.GOODBYE_AUTH_REQUIRED):
                self.auth_error = text
                raise AuthRejected(text)
            return

    # ---- 认证 ----
    async def _start_auth(self):
        if not self.user:
            raise AuthRejected("未配置用户名")
        self._set_state(SessionState.AUTHENTICATING, self.user)
        name = self.user.encode("utf-8")
        await self._send_session(proto.SESSION_AUTH_REQUEST,
                                 bytes([len(name)]) + name)

    async def _on_challenge(self, payload):
        """收到挑战：派生 H = HMAC^N(口令, 盐)，回 HMAC(H, nonce)。"""
        if len(payload) < 8:
            raise AuthRejected("挑战载荷畸形")
        method = payload[0]
        salt_len = payload[1]
        iterations = struct.unpack("<I", payload[4:8])[0]
        salt = payload[8:8 + salt_len]
        nonce = payload[8 + salt_len:8 + salt_len + proto.XLINK_AUTH_NONCE_BYTES]
        if (salt_len != proto.XLINK_AUTH_SALT_BYTES
                or len(nonce) != proto.XLINK_AUTH_NONCE_BYTES
                or iterations < 1):
            raise AuthRejected("挑战参数不合法")
        # [2026-11-18 挪出事件循环 + 带缓存] 10000 轮 HMAC 是几十毫秒的
        # 纯 CPU 活；同步算会把整个事件循环堵住（登录洪泛 ⇒ SSE 心跳停发
        # ⇒ 上游判静默断链 ⇒ 看着像"整个控制台死了"）。⇒ 丢线程里算。
        digest = await run_in_thread(derive_hash_cached, self.user,
                                     self.password, salt, iterations)
        response = hmac.new(digest, nonce, hashlib.sha256).digest()
        await self._send_session(
            proto.SESSION_AUTH_RESPONSE,
            bytes([method, 0]) + struct.pack("<H", len(response)) + response)

    async def _on_auth_result(self, payload):
        if len(payload) < 4:
            raise AuthRejected("认证结果畸形")
        status = payload[0]
        role = payload[1]
        text_len = struct.unpack("<H", payload[2:4])[0]
        text = payload[4:4 + text_len].decode("utf-8", "replace")
        if status != proto.AUTHSTATUS_OK:
            self.auth_role = proto.ROLE_ANONYMOUS
            self.auth_error = text or ("认证被拒（%d）" % status)
            # [永久/瞬时分类对齐客户端] 永久（BAD_USER/BAD_PASSWORD/LOCKED/
            # NO_CREDENTIAL）→ AuthRejected 停止重连（重试同一凭据永远不会
            # 成功，还会把服务端失败计数打满锁账户）；瞬时（REJECTED——
            # 同账并发/会话上限这类策略闸，TIMEOUT/PROTOCOL——链路噪声）
            # → AuthTransient 退避重试。
            if status in (proto.AUTHSTATUS_REJECTED,
                          proto.AUTHSTATUS_TIMEOUT,
                          proto.AUTHSTATUS_PROTOCOL):
                raise AuthTransient(self.auth_error)
            raise AuthRejected(self.auth_error)
        self.auth_role = role
        self.auth_error = ""
        self._auth_reject_count = 0
        # 重连成功后订阅表是空的；WebHub 的 on_ready_changed 会重新下发。
        self._set_state(SessionState.READY, "角色 %d" % role)
        self._log("上游登录成功（%s，角色 %d）" % (self.user, role))

    # ==================================================================
    #                        业务 API（供 WebHub 调）
    # ==================================================================
    async def request(self, msg_code, payload=b"",
                      data_type=proto.DATATYPE_BSON, timeout=3.0):
        """发请求并等应答，返回 (msgCode, dataType, payload)。

        :raises RequestTimeout: 超时或链路已断。
        """
        if self.state != SessionState.READY:
            raise RequestTimeout("未连接服务端")
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
        """订阅（幂等）。已订的不会重发。"""
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

    async def set_topics(self, topics):
        """把订阅集合收敛到恰好 `topics`（一次调用完成增删）。

        [为什么用收敛而不是订阅/退订分别调] 浏览器切页时两件事同时发生
        （离开 A 页退订、进入 B 页订阅）。分开调会出现中间态"先退后订"，
        这段时间里推送空档；收敛式调用只发差集，中间态不存在。
        """
        topics = set(topics)
        drop = self.subscriptions - topics
        add = topics - self.subscriptions
        for topic in drop:
            await self.unsubscribe(topic)
        for topic in add:
            await self.subscribe(topic)
        return True

    # ==================================================================
    #                          诊断信息
    # ==================================================================
    def status(self):
        """给 /api/status 用的链路快照。"""
        now = self._now_ms()
        return {
            "state": self.state,
            "ready": self.is_ready(),
            "host": self.host,
            "port": self.port,
            "user": self.user,
            "role": int(self.auth_role),
            "error": self.auth_error,
            "authBlocked": bool(self.auth_blocked),
            "reconnectCount": self.reconnect_count,
            "subscriptions": sorted(int(t) for t in self.subscriptions),
            "silentMs": (now - self.last_receive_ms
                         if self.last_receive_ms else -1),
            "readyForMs": (now - self.connected_since_ms
                           if self.connected_since_ms else 0),
        }


#: Goodbye 原因码 → 中文（诊断/界面提示用）。
GOODBYE_TEXT = {
    proto.GOODBYE_NORMAL: "正常关闭",
    proto.GOODBYE_PROTOCOL: "协议违例",
    proto.GOODBYE_AUTH_FAILED: "认证失败",
    proto.GOODBYE_AUTH_REQUIRED: "需要登录",
    proto.GOODBYE_TIMEOUT: "静默超时",
    proto.GOODBYE_SERVER_SHUTDOWN: "服务端关闭",
}
