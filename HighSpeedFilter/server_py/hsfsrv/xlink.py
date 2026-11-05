# -*- coding: utf-8 -*-
"""XLink 服务端会话层：握手 / 认证 / 订阅 / 发布（对齐 XLinkServer+XLinkSession）。

[C 侧实现的对应关系]
    XLinkServer        —— 监听、接受连接、会话表、周期 tick
    XLinkSession       —— 单连接状态机（Connected→Authenticating→Ready）
    本模块把两者合成一个 asyncio 服务端：每条 TCP 连接一个 Session 对象，
    「周期 tick」由上层 app 的 asyncio 任务驱动（不用线程、不用锁）。

[状态机] 严格照 C 侧的准入顺序，因为客户端的行为依赖它：
    Connected      —— 已建链，等 Hello/HelloAck 定能力交集
    Authenticating —— 服务端要求认证且对端宣告了 CAP_AUTH，等 AuthRequest
    Ready          —— 可服务（认证通过，或显式允许匿名）
  任何一帧的读取都以「状态是否仍然有效」为前提，Goodbye/协议违例就地断链。

[为什么必须校验主版本] 帧头宽度只在主版本升级时变（1.x=12 字节、2.x=16 字节）。
主版本不一致时对方会按错误宽度切帧，从此整条字节流错位——它不报错，只会把
垃圾当帧头继续解析，表现为「连着但业务全乱」。故直接断链（与 C 侧同）。
"""

import asyncio
import os
import socket
import struct
import time

from . import auth as auth_mod
from . import proto


class SessionState(object):
    CONNECTED = "connected"
    AUTHENTICATING = "authenticating"
    READY = "ready"
    CLOSED = "closed"


class Session(object):
    """单条 TCP 连接上的 XLink 会话。

    所有方法都在同一个 asyncio 事件循环里跑，故**无锁**：没有任何 await 会
    在修改自身状态的中途切走（写 socket 用 drain，但状态已在写之前定好）。
    """

    def __init__(self, reader, writer, server):
        self.reader = reader
        self.writer = writer
        self.server = server
        self.state = SessionState.CONNECTED
        self.peer = "%s:%d" % writer.get_extra_info("peername")[:2]
        self.local_capability = proto.CAP_AUTH        # 本端能力（Auth 恒置）
        self.peer_capability = 0                      # 对端声明（原始值）
        self.remote_capability = 0                    # 能力交集
        self.subscriptions = set()
        self.request_id = 0
        self.auth_failures = 0
        self.auth_user = None
        self.auth_role = proto.ROLE_ANONYMOUS
        self.auth_passed = False
        # 认证挑战上下文（一次性，比对完即作废）
        self._challenge_nonce = None
        self._challenge_user = None
        self._challenge_digest = None
        # 心跳计时（单调毫秒）
        self.last_receive_ms = self._now_ms()
        self.last_send_ms = self.last_receive_ms
        self.auth_start_ms = 0
        self.connected_ms = self.last_receive_ms
        self.closed = False

    # ---- 基础 ----
    @staticmethod
    def _now_ms():
        return int(time.monotonic() * 1000)

    def is_ready(self):
        return self.state == SessionState.READY

    def role(self):
        return self.auth_role

    # ==================================================================
    #                          发送原语
    # ==================================================================
    async def _send_frame(self, msg_class, msg_code, data_type,
                          payload=b"", request_id=0):
        """组帧并写出（一次 write，避免帧内被别的协程插入）。"""
        if self.closed:
            return False
        try:
            head = proto.encode_header(len(payload), msg_class, msg_code,
                                       request_id, data_type)
            self.writer.write(head + payload)
            await self.writer.drain()
            self.last_send_ms = self._now_ms()
            return True
        except (ConnectionError, OSError, RuntimeError):
            # 对端已断：就地标记，由 run() 的 finally 统一收尸（不在这里
            # 关闭 writer，否则可能与 pump 的收包路径并发 close）。
            self.closed = True
            return False

    async def send_session(self, code, payload=b""):
        """会话帧（Hello/Auth*/Heartbeat/Goodbye），dataType 恒 Raw。"""
        return await self._send_frame(proto.MSGCLASS_SESSION, code,
                                      proto.DATATYPE_RAW, payload)

    async def send_response(self, request_id, msg_code, payload,
                            data_type=proto.DATATYPE_BSON):
        """应答帧：原样回带对端 requestId 与相同 msgCode（关联口径）。"""
        return await self._send_frame(proto.MSGCLASS_RESPONSE, msg_code,
                                      data_type, payload, request_id)

    async def send_publish(self, topic_id, payload,
                           data_type=proto.DATATYPE_BSON):
        """推送帧：载荷前缀是完整 u32 topicId（msgCode 只装低 16 位）。"""
        return await self._send_frame(proto.MSGCLASS_PUBLISH,
                                      topic_id & 0xFFFF, data_type,
                                      proto.pack_topic(topic_id) + payload)

    async def send_goodbye(self, reason):
        await self.send_session(proto.SESSION_GOODBYE, bytes([reason]))

    # ==================================================================
    #                          收包主循环
    # ==================================================================
    async def run(self):
        """读到断链为止；异常一律转成「干净关闭 + 统计」。"""
        try:
            while not self.closed:
                head = await self._read_exact(proto.XLINK_HEADER_SIZE)
                if head is None:
                    break
                hdr = proto.decode_header(head)
                if hdr is None:
                    # 协议违例：断链前告知原因，不继续按垃圾当帧头读。
                    self.server.log("会话 %s 帧头非法 → 断链" % self.peer)
                    await self.send_goodbye(proto.GOODBYE_PROTOCOL)
                    break
                plen, mclass, mcode, rid, dtype = hdr
                payload = b""
                if plen:
                    payload = await self._read_exact(plen)
                    if payload is None:
                        break
                self.last_receive_ms = self._now_ms()
                await self._on_frame(mclass, mcode, rid, dtype, payload)
        except (ConnectionError, OSError, asyncio.IncompleteReadError):
            pass
        except asyncio.CancelledError:
            raise
        except Exception as exc:                       # noqa: BLE001
            # 兜底：单个会话的任何异常不得拖垮服务端。
            self.server.log("会话 %s 异常终止：%s: %s"
                            % (self.peer, type(exc).__name__, exc))
        finally:
            await self.close()

    async def _read_exact(self, n):
        """读满 n 字节；对端关闭返回 None。"""
        try:
            return await self.reader.readexactly(n)
        except (asyncio.IncompleteReadError, ConnectionError, OSError):
            return None

    async def _on_frame(self, mclass, mcode, rid, dtype, payload):
        """帧分派：会话帧在本层消化，业务帧交给 app 的分发回调。"""
        if mclass == proto.MSGCLASS_SESSION:
            await self._on_session_frame(mcode, payload)
            return
        if mclass == proto.MSGCLASS_SUBSCRIBE:
            await self._on_subscribe(payload)
            return
        if mclass == proto.MSGCLASS_UNSUBSCRIBE:
            await self._on_unsubscribe(payload)
            return
        if mclass == proto.MSGCLASS_PUBLISH:
            # 服务端不消费客户端推送（本协议无此方向业务）；礼貌忽略。
            return
        if self.state != SessionState.READY:
            # 未就绪时的业务帧：回否定应答让对端立刻知道，而不是空等到超时。
            if mclass == proto.MSGCLASS_REQUEST:
                await self._reply_error(rid, mcode, "尚未登录")
            return
        if mclass == proto.MSGCLASS_REQUEST:
            await self.server.dispatch(self, rid, mcode, dtype, payload)
            return
        if mclass == proto.MSGCLASS_RESPONSE:
            # 服务端不发起 Request，收到应答帧属异常但无害：忽略。
            return
        # File(6) 类本实现不支持（未宣告 CAP_FILE_TRANSFER）；落到这里即忽略。

    # ==================================================================
    #                        会话帧处理
    # ==================================================================
    async def _on_session_frame(self, code, payload):
        if code in (proto.SESSION_HELLO, proto.SESSION_HELLO_ACK):
            await self._on_hello(code, payload)
        elif code == proto.SESSION_HEARTBEAT:
            # 仅用于刷新静默计时（上面 _on_frame 已做）；不必回。
            return
        elif code == proto.SESSION_GOODBYE:
            reason = payload[0] if payload else proto.GOODBYE_NORMAL
            self.server.log("会话 %s 对端主动断开（原因 %d）" % (self.peer, reason))
            self.closed = True
        elif code == proto.SESSION_AUTH_REQUEST:
            await self._on_auth_request(payload)
        elif code == proto.SESSION_AUTH_RESPONSE:
            await self._on_auth_response(payload)
        # AuthChallenge/AuthResult 是服务端→客户端方向；对端发来即忽略。

    async def _on_hello(self, code, payload):
        """握手：记能力交集；对端发 Hello 则回 HelloAck；然后决定认证分流。"""
        hello = proto.decode_hello(payload)
        if hello is None:
            # [兼容性] Hello 载荷是可选的（C 侧 XLinkCodec_decodeHello 解不出
            # 就静默跳过，不断链）。早期客户端/探针会发空载荷 Hello，
            # 直接断链会让它们连不上——故这里同样只记日志、不处理。
            self.server.log("会话 %s Hello 载荷不可解（%d 字节），按无声握手忽略"
                            % (self.peer, len(payload)))
            return
        version, capability = hello
        # 主版本必须一致（帧头宽度变了），次版本不校验（属兼容扩展）。
        if (version >> 8) != (proto.XLINK_VERSION >> 8):
            self.server.log("会话 %s 协议主版本不符（0x%04X）→ 断链"
                            % (self.peer, version))
            await self.send_goodbye(proto.GOODBYE_PROTOCOL)
            self.closed = True
            return

        self.peer_capability = capability
        self.remote_capability = capability & self.local_capability

        if code == proto.SESSION_HELLO:
            # 对称握手：对端先发 Hello，本端回 HelloAck（携带自身能力）。
            await self.send_session(proto.SESSION_HELLO_ACK,
                                    proto.encode_hello(self.local_capability))

        # 握手只做一次：双方自动握手会各收到 Hello 与 HelloAck 两帧，第二次
        # 进入时状态已非 Connected，必须整体跳过——否则认证态被覆盖成 Ready，
        # 认证门控失效（C 侧注释同款警告）。
        if self.state != SessionState.CONNECTED:
            return

        if self.server.require_auth:
            if self.remote_capability & proto.CAP_AUTH:
                # 对端支持登录：进等待态，等它发 AuthRequest。
                self.state = SessionState.AUTHENTICATING
                self.auth_start_ms = self._now_ms()
            else:
                # 本端要求认证但对端不宣告 Auth（老对端/恶意降级）：默认拒绝，
                # 绝不回落匿名——否则去掉一个能力位就绕过登录。
                self.server.log("会话 %s 不支持认证 → 拒绝" % self.peer)
                await self.send_goodbye(proto.GOODBYE_AUTH_REQUIRED)
                self.closed = True
        else:
            # 未启用认证：匿名链路直接可服务。
            self.auth_passed = True
            self.auth_role = proto.ROLE_ANONYMOUS
            self.state = SessionState.READY
            await self.server.on_ready(self)

    # ---- 认证 ----
    async def _on_auth_request(self, payload):
        """服务端收 AuthRequest：查表 → 下发挑战（含本账户的盐与迭代数）。"""
        if not self.server.require_auth:
            # 未要求认证却收到登录请求：按协议错误拒绝（不泄露账户信息）。
            await self._auth_reject(proto.AUTHSTATUS_PROTOCOL, "authentication rejected")
            return
        if self.state not in (SessionState.AUTHENTICATING,
                              SessionState.CONNECTED):
            return
        name = auth_mod.decode_request(payload)
        if name is None:
            await self._auth_reject(proto.AUTHSTATUS_PROTOCOL, "malformed request")
            return

        # [root 单会话策略] 防并行修改：同一账户只允许一个在线会话。
        # 拒绝走 BadUser 口径（客户端看到干净的 authFailed，登录窗留在原地）。
        if self.server.server_policy_blocks(name, self):
            self.server.log("会话 %s 重复登录被拒（%s 已有在线会话）"
                            % (self.peer, name))
            await self._auth_reject(proto.AUTHSTATUS_BAD_USER, "unknown user")
            return

        rec = self.server.users.find(name)
        if rec is None or rec.locked or not rec.digest:
            # 用户不存在/锁定/无口令：三种情况对外一律 BadUser，避免枚举账户。
            status = (proto.AUTHSTATUS_LOCKED if (rec is not None and rec.locked)
                      else proto.AUTHSTATUS_BAD_USER)
            await self._auth_reject(status, "unknown user" if status ==
                                    proto.AUTHSTATUS_BAD_USER else "account locked")
            return

        nonce = auth_mod.random_bytes(proto.XLINK_AUTH_NONCE_BYTES)
        self._challenge_nonce = nonce
        self._challenge_user = rec.name
        self._challenge_digest = rec.digest
        challenge = auth_mod.encode_challenge(
            proto.AUTH_METHOD_ITERATED_HMAC, rec.iterations, rec.salt, nonce)
        self.state = SessionState.AUTHENTICATING
        if not self.auth_start_ms:
            self.auth_start_ms = self._now_ms()
        await self.send_session(proto.SESSION_AUTH_CHALLENGE, challenge)

    async def _on_auth_response(self, payload):
        """服务端收 AuthResponse：比对 HMAC(H, nonce)，通过则下发服务端证明。"""
        if self._challenge_nonce is None or self._challenge_digest is None:
            await self._auth_reject(proto.AUTHSTATUS_PROTOCOL, "unexpected response")
            return

        decoded = auth_mod.decode_response(payload)
        nonce = self._challenge_nonce
        digest = self._challenge_digest
        user = self._challenge_user
        # 挑战一次性：无论成败都作废，防重放同一应答。
        self._challenge_nonce = None
        self._challenge_digest = None
        self._challenge_user = None

        if decoded is None or decoded[0] != proto.AUTH_METHOD_ITERATED_HMAC:
            await self._auth_reject(proto.AUTHSTATUS_PROTOCOL, "malformed response")
            return
        expected = auth_mod.compute_response(digest, nonce)
        if not auth_mod.constant_time_equal(decoded[1], expected):
            await self._auth_reject(proto.AUTHSTATUS_BAD_PASSWORD, "bad password")
            return

        rec = self.server.users.find(user)
        role = rec.role if rec is not None else proto.ROLE_USER
        proof = auth_mod.compute_server_proof(digest, nonce)
        await self.send_session(
            proto.SESSION_AUTH_RESULT,
            auth_mod.encode_result(proto.AUTHSTATUS_OK, role, "", proof))

        self.auth_passed = True
        self.auth_role = role
        self.auth_user = user
        self.auth_failures = 0
        self.state = SessionState.READY
        self.server.log("会话 %s 登录成功（%s，角色 %d）"
                        % (self.peer, user, role))
        await self.server.on_ready(self)

    async def _auth_reject(self, status, text):
        """认证失败：下发失败结果，累计失败次数，达上限即断链。"""
        await self.send_session(
            proto.SESSION_AUTH_RESULT,
            auth_mod.encode_result(status, 0, text, None))
        self.auth_failures += 1
        self.server.log("会话 %s 登录失败（状态 %d，累计 %d）"
                        % (self.peer, status, self.auth_failures))
        if self.auth_failures >= proto.XLINK_AUTH_FAILURE_LIMIT:
            await self.send_goodbye(proto.GOODBYE_AUTH_FAILED)
            self.closed = True
        else:
            # 未达上限：允许对端换用户/换口令重试。
            self.state = SessionState.AUTHENTICATING

    async def _reply_error(self, request_id, msg_code, text):
        from . import bson
        await self.send_response(request_id, msg_code,
                                 bson.encode({proto.KEY_OK: False,
                                              proto.KEY_ERR: text}))

    # ==================================================================
    #                        订阅 / 退订
    # ==================================================================
    async def _on_subscribe(self, payload):
        """订阅登记 + 「订阅即补发一拍」。

        [为什么必须补发] 客户端切到某页才订阅（隐藏即退订）。若只等周期推送，
        首帧要等一个推送周期 → 页面刚打开显示"--"，观感「没数据」。
        补发用**该会话定向发送**，只影响本次订阅者；不刷新推送门控，
        故不打乱周期节拍。
        """
        topic = self._topic_from_payload(payload)
        if topic is None:
            return
        if not self.is_ready():
            await self._reply_error(0, topic & 0xFFFF, "尚未登录")
            return
        self.subscriptions.add(topic)
        self.server.log("会话 %s 订阅主题 0x%08X" % (self.peer, topic))
        await self.server.publish_topic_now(self, topic)

    async def _on_unsubscribe(self, payload):
        topic = self._topic_from_payload(payload)
        if topic is None:
            return
        self.subscriptions.discard(topic)
        self.server.log("会话 %s 退订主题 0x%08X" % (self.peer, topic))

    @staticmethod
    def _topic_from_payload(payload):
        """从载荷首 4 字节取完整 topicId。

        [实锅] msgCode 只有 16 位，装不下 0x0001NNNN 的业务主题号——用它
        补发会把主题号截成 0x00NN 发出去，客户端按完整号匹配则整帧丢弃
        （表现为「发了但收不到」）。故一律以载荷里的 u32 为准。
        """
        if len(payload) < 4:
            return None
        topic = struct.unpack("<I", payload[:4])[0]
        return topic if topic >= proto.XLINK_TOPIC_USER_BASE else None

    # ==================================================================
    #                          心跳与超时
    # ==================================================================
    async def tick(self, now_ms):
        """周期巡检：认证超时判线、静默超时判线、主动心跳。

        返回 True 表示会话仍存活；False 表示已判死（调用方负责清理）。
        """
        if self.closed:
            return False
        # 认证超时：进 Authenticating 后迟迟不发 AuthRequest/Response。
        if (self.state == SessionState.AUTHENTICATING
                and self.auth_start_ms
                and now_ms - self.auth_start_ms > proto.XLINK_AUTH_TIMEOUT_MS):
            self.server.log("会话 %s 认证超时 → 断链" % self.peer)
            await self.send_goodbye(proto.GOODBYE_TIMEOUT)
            return False
        # 握手超时：建链后迟迟不握手（防半开连接占资源）。
        if (self.state == SessionState.CONNECTED
                and now_ms - self.connected_ms > proto.XLINK_HANDSHAKE_TIMEOUT_MS):
            self.server.log("会话 %s 握手超时 → 断链" % self.peer)
            await self.send_goodbye(proto.GOODBYE_TIMEOUT)
            return False
        # 静默超时：对端（含心跳）完全无声超过阈值即判链路断开。
        if now_ms - self.last_receive_ms > proto.XLINK_HEARTBEAT_TIMEOUT_MS:
            self.server.log("会话 %s 静默超时（%dms）→ 断链"
                            % (self.peer, now_ms - self.last_receive_ms))
            return False
        # 主动心跳：静默达间隔即发一帧，供对端刷新其静默计时。
        if self.state == SessionState.READY:
            base = max(self.last_send_ms, self.last_receive_ms)
            if now_ms - base >= proto.XLINK_HEARTBEAT_INTERVAL_MS:
                await self.send_session(proto.SESSION_HEARTBEAT)
        return True

    async def close(self):
        """幂等关闭（不发 Goodbye：正常关闭路径由调用方显式发）。"""
        if self.closed and self.writer.is_closing():
            return
        self.closed = True
        self.state = SessionState.CLOSED
        try:
            self.writer.close()
        except (ConnectionError, OSError, RuntimeError):
            pass


class Server(object):
    """XLink 服务端：监听 + 会话表 + 周期 tick + 发布扇出。

    与 C 侧 XLinkServer 的对应：
        setDispatchCallback  → self.dispatch（子类/回调实现）
        setNotifyCallback    → on_session_added / on_session_removed
        setTickCallback      → self.on_tick
        publish_ex           → publish()
    """

    def __init__(self, port, users, log=None, require_auth=True):
        self.port = port
        self.users = users
        self.require_auth = require_auth
        self.sessions = []
        self._log = log or (lambda msg: None)
        self._server = None
        self._tasks = []
        # 回调（由 app 装配）
        self.dispatch = None
        self.on_tick = None
        self.on_ready = None
        self.on_session_added = None
        self.on_session_removed = None
        #: 订阅/首连补发回调：(session, topic_id) → (payload, dataType) 或 None。
        self.on_publish_now = None
        # 会话策略：返回 True 即拒绝本次登录（root 单会话等）
        self.policy_hook = None

    def log(self, msg):
        self._log(msg)

    def server_policy_blocks(self, user_name, session):
        """账户级登录策略钩子（默认不拦）。"""
        if self.policy_hook is None:
            return False
        return self.policy_hook(user_name, session)

    def session_count(self):
        return len(self.sessions)

    # ---- 生命周期 ----
    async def start(self):
        # [⚠ 平台差异：reuse_address 在 Windows 上会吞掉端口冲突]
        #
        # Linux：SO_REUSEADDR 允许绑定 TIME_WAIT 里的端口（重启服务端不必
        #        等 60s，是**正当需求**），但**不允许**两个 socket 同时 LISTEN。
        # Windows：同一位的语义更宽 —— 允许多个 socket **同时监听同一端口**。
        #
        # 实锅 2026-11-05：快捷方式启动时旧实例还在跑，第二个实例**静默绑定
        # 成功**，`netstat` 出现两条 0.0.0.0:6200 LISTENING，客户端连到哪
        # 一个是随机的。两个实例各自轮询同一批虚拟设备、各自维护推送节拍，
        # 现象会是"数据跳变/状态重置"，极难看出是双实例。
        #
        # 故 Windows 上关掉它（宁可重启时偶尔等 TIME_WAIT，也不能静默双开）。
        reuse = (os.name != "nt")
        try:
            self._server = await asyncio.start_server(
                self._handle_client, host="0.0.0.0", port=self.port,
                reuse_address=reuse)
        except OSError as exc:
            # 端口被占用是最常见的启动失败，必须给出能直接照做的提示，
            # 而不是抛一个 EADDRINUSE 栈让用户去猜。
            raise OSError(
                "端口 %d 已被占用，无法启动（另一个服务端实例在跑？）。\n"
                "  · 查占用：netstat -ano | findstr :%d\n"
                "  · 或者换端口：--port 6201\n"
                "原始错误：%s" % (self.port, self.port, exc)) from exc
        sock = self._server.sockets[0] if self._server.sockets else None
        if sock is not None:
            self.port = sock.getsockname()[1]
        self._tasks.append(asyncio.ensure_future(self._tick_loop()))
        return True

    async def _handle_client(self, reader, writer):
        """每条新连接：建会话、登记、跑会话循环，退出时注销。"""
        # TCP_NODELAY：XLink 是小帧交互（遥测 100ms 一拍），Nagle 会把小帧
        # 攒到 40ms 级才发，直接把「数值连续变化」的观感打坏。
        try:
            sock = writer.get_extra_info("socket")
            if sock is not None:
                sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        except OSError:
            pass
        session = Session(reader, writer, self)
        self.sessions.append(session)
        self.log("会话接入 %s（当前 %d 路）" % (session.peer, len(self.sessions)))
        try:
            if self.on_session_added:
                await self._maybe_await(self.on_session_added(session))
            await session.run()
        finally:
            if session in self.sessions:
                self.sessions.remove(session)
            self.log("会话断开 %s（当前 %d 路）" % (session.peer, len(self.sessions)))
            if self.on_session_removed:
                await self._maybe_await(self.on_session_removed(session))

    @staticmethod
    async def _maybe_await(value):
        """回调可同步可协程：统一处理并**原样返回结果**。

        [必须返回值] 早先这里只 await 不 return，协程回调的结果被静默丢弃，
        调用方拿到 None 再解包就抛 TypeError（订阅补发直接把会话打崩）。
        """
        if asyncio.iscoroutine(value):
            return await value
        return value

    async def _tick_loop(self):
        """周期 tick：会话巡检（心跳/超时）+ 业务推送。

        tick 周期取 XLINK_HEARTBEAT_TICK_MS（250ms）与业务推送周期的较小者，
        保证两者都能按各自节拍触发。
        """
        period_ms = proto.XLINK_HEARTBEAT_TICK_MS
        if self.on_tick is not None:
            period_ms = min(period_ms, self.on_tick.interval_ms)
        period = period_ms / 1000.0
        try:
            while True:
                await asyncio.sleep(period)
                now = Session._now_ms()
                for session in list(self.sessions):
                    try:
                        alive = await session.tick(now)
                    except Exception as exc:                # noqa: BLE001
                        self.log("会话 tick 异常：%s" % exc)
                        alive = False
                    if not alive:
                        await session.close()
                        if session in self.sessions:
                            self.sessions.remove(session)
                if self.on_tick is not None:
                    try:
                        await self._maybe_await(self.on_tick(now))
                    except Exception as exc:                # noqa: BLE001
                        self.log("业务 tick 异常：%s: %s"
                                 % (type(exc).__name__, exc))
        except asyncio.CancelledError:
            raise

    async def stop(self):
        """关闭全部会话并停监听（先发 Goodbye 让对端快速感知）。"""
        for task in self._tasks:
            task.cancel()
        self._tasks = []
        for session in list(self.sessions):
            try:
                await session.send_goodbye(proto.GOODBYE_SERVER_SHUTDOWN)
            except Exception:                                # noqa: BLE001
                pass
            await session.close()
        self.sessions = []
        if self._server is not None:
            self._server.close()
            try:
                await self._server.wait_closed()
            except Exception:                                # noqa: BLE001
                pass
            self._server = None

    # ---- 发布 ----
    async def publish(self, topic_id, payload,
                      data_type=proto.DATATYPE_BSON):
        """向**订阅了该主题**的全部会话投递。

        无人订阅时静默返回 0（不视为错误）——与 C 侧 publish_ex 同口径。
        """
        count = 0
        for session in list(self.sessions):
            if not session.is_ready():
                continue
            if topic_id not in session.subscriptions:
                continue
            if await session.send_publish(topic_id, payload, data_type):
                count += 1
        return count

    async def publish_topic_now(self, session, topic_id):
        """向**单个会话**立即发一拍指定主题（订阅补发 / 首连补发用）。"""
        if self.on_publish_now is None:
            return False
        payload, data_type = await self._maybe_await(
            self.on_publish_now(session, topic_id))
        if payload is None:
            return False
        return await session.send_publish(topic_id, payload, data_type)
