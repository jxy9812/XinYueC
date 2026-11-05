# -*- coding: utf-8 -*-
"""网页会话与登录：浏览器怎么拿到一个"能操作"的身份。

设计抉择：**凭据门禁放在哪一层？**
===================================

三条路可选，逐条说明为什么选第三条：

  A) 网页服务器自己存一份账户库（复制服务端 hsf_users.json）
     → 两份真相。服务端改口令、加账户、锁账户，网页版全不知道。
       现场表现：服务端说"口令错"，网页版还能登进去（或反过来）。
       而且网页版的账户库要单独维护、单独备份 —— 运维负担翻倍。

  B) 网页服务器不认证，直接放行（内含"反正是内网"）
     → 板子所在网段常有临时接入的调试电脑。不认证等于把变频器启停
       和 6 路阀开关公开给整个网段。不可接受。

  C) **浏览器 → 网页服务器 → 上游 XLink 认证**（本实现）
     → 账户库只有服务端一份；口令只在浏览器与网页服务器之间过一次
       （同机/内网明文 TCP，与 XLink 本身同一信任级别），网页服务器
       再用它向服务端做挑战应答，**从不保存口令**。
       好处：账户/锁定/角色变化天然同步；root 单会话策略也由服务端统一裁决。

    [为什么网页凭据不缓存] 每次浏览器登录都真去服务端认证一次。
    代价是几十毫秒（10000 轮 HMAC），换来的是"账户被锁/降权立刻生效"。
    若缓存凭据，运维在服务端把某人降成只读之后，那人还能继续控制阀门。

[会话怎么维持]
  登录成功 → 服务端下发一个随机 token（本进程内存表），浏览器把它放在
  `Authorization: Bearer <token>` 或 Cookie 里。token 不落盘，
  网页服务器重启即全体失效（现场表现：重启后要重新登录，可接受）。

[为什么不复用上游的登录态] 上游只有一条连接、一个角色（见 xlink.py）。
若浏览器 A 用 root 登录、B 用 admin 登录，上游连接只能有一个角色。
故网页侧**必须**自己维护"每个浏览器是谁"，再把控制命令按该身份做闸门
预检 —— 但**服务端的闸门仍然生效**（它看到的永远是网页服务器那一个身份），
所以网页侧的门禁是**下限**而不是替代。
"""

import asyncio
import secrets
import threading
import time

from . import proto
from .xlink import AuthRejected, RequestTimeout, SessionState, XLinkUpstream


def _now_ms():
    return int(time.time() * 1000)


class WebSession(object):
    """一个浏览器会话。"""

    __slots__ = ("token", "user", "role", "created_ms", "last_seen_ms",
                 "remote", "password_hint")

    def __init__(self, token, user, role, remote=""):
        self.token = token
        self.user = user
        self.role = int(role)
        self.created_ms = _now_ms()
        self.last_seen_ms = self.created_ms
        self.remote = remote
        #: 只用于诊断（"口令错的是哪个账户"），**不存口令本身**。
        self.password_hint = ""

    def is_admin(self):
        return self.role >= proto.ROLE_ADMIN

    def touch(self):
        self.last_seen_ms = _now_ms()

    def to_json(self):
        return {
            "user": self.user,
            "role": self.role,
            "roleText": role_text(self.role),
            "isAdmin": self.is_admin(),
            "createdMs": self.created_ms,
            "lastSeenMs": self.last_seen_ms,
            "remote": self.remote,
        }


def role_text(role):
    return {proto.ROLE_ANONYMOUS: "匿名",
            proto.ROLE_USER: "只读",
            proto.ROLE_ADMIN: "管理员"}.get(int(role), "未知")


class SessionStore(object):
    """网页会话表（内存，进程内有效）。

    [为什么用随机 token 而不是签名 Cookie] 签名 Cookie 需要一个长期密钥，
    密钥要么写死（等于没有）、要么落盘（多一个需要保护的秘密）。
    随机 token + 内存表：不落盘、不可伪造、重启即失效，对单进程足够。
    """

    #: 会话有效期（12 小时；现场一个班次够用）。
    TTL_MS = 12 * 3600 * 1000
    #: 最多同时在线（板子内存有限，防止被刷）。
    MAX_SESSIONS = 32

    def __init__(self):
        self._by_token = {}
        self._lock = threading.Lock()

    def create(self, user, role, remote=""):
        token = secrets.token_urlsafe(32)
        with self._lock:
            # 清掉过期会话（顺手防止无限增长）。
            self._purge_locked()
            if len(self._by_token) >= self.MAX_SESSIONS:
                # 淘汰最久没活动的（而不是拒绝新登录：现场换个人来用很正常）。
                oldest = min(self._by_token.values(),
                             key=lambda s: s.last_seen_ms)
                self._by_token.pop(oldest.token, None)
            session = WebSession(token, user, role, remote)
            self._by_token[token] = session
        return session

    def get(self, token):
        if not token:
            return None
        with self._lock:
            session = self._by_token.get(token)
            if session is None:
                return None
            if _now_ms() - session.created_ms > self.TTL_MS:
                self._by_token.pop(token, None)
                return None
            session.touch()
            return session

    def drop(self, token):
        with self._lock:
            return self._by_token.pop(token, None) is not None

    def drop_user(self, user):
        """踢掉某个用户的全部会话（管理员操作/账户被锁）。"""
        with self._lock:
            gone = [t for t, s in self._by_token.items() if s.user == user]
            for token in gone:
                self._by_token.pop(token, None)
        return gone

    def _purge_locked(self):
        now = _now_ms()
        for token in [t for t, s in self._by_token.items()
                      if now - s.created_ms > self.TTL_MS]:
            self._by_token.pop(token, None)

    def count(self):
        with self._lock:
            return len(self._by_token)

    def list(self):
        with self._lock:
            return [s.to_json() for s in self._by_token.values()]


class LoginError(Exception):
    """登录失败（带用户可读原因）。"""


async def verify_with_upstream(host, port, user, password, log, timeout=10.0):
    """用一次性连接向服务端验证凭据，返回角色。

    [为什么用一次性连接而不是复用主链路] 主链路的身份是网页服务器自己
    （通常用 root），浏览器 A 用 admin 登录不该改变主链路的身份 ——
    否则 B 的 root 权限会被 A 的 admin 登录"降级"，两人互相踩。

    [为什么不复用客户端 XLinkUpstream] 复用会让"登录"与"链路状态"耦合：
    一次登录失败（口令错）会把主链路也带进"停止重连"状态。
    这里起一条独立短连接，认完就关，失败只影响本次登录。

    :raises LoginError: 服务端拒绝或连不上
    """
    if not user:
        raise LoginError("请填写用户名")
    if not password:
        raise LoginError("请填写口令")
    loop = asyncio.get_event_loop()

    probe = XLinkUpstream(host, port, user, password, log=log)
    probe.auto_reconnect = False
    probe._loop = loop
    ready = loop.create_future()

    def _on_state(state, detail):
        if state == SessionState.READY:
            if not ready.done():
                ready.set_result(int(probe.auth_role))
        elif state == SessionState.DISCONNECTED:
            if not ready.done():
                ready.set_exception(
                    LoginError(probe.auth_error or detail or "登录失败"))

    probe.on_state_changed = _on_state
    probe.on_ready_changed = None
    probe.on_publish = None
    task = loop.create_task(probe._connect_once())
    try:
        role = await asyncio.wait_for(ready, timeout)
        return int(role)
    except asyncio.TimeoutError:
        raise LoginError("登录超时（服务端无应答）")
    except AuthRejected as exc:
        raise LoginError(str(exc))
    except RequestTimeout as exc:
        raise LoginError(str(exc))
    except (ConnectionError, OSError) as exc:
        raise LoginError("连不上服务端 %s:%d（%s）" % (host, port, exc))
    finally:
        task.cancel()
        try:
            await task
        except (asyncio.CancelledError, Exception):         # noqa: BLE001
            pass
        try:
            await probe.close()
        except Exception:                                   # noqa: BLE001
            pass
        probe.on_state_changed = None


def check_control_gate(session, model):
    """控制类操作的闸门（与 Qt 客户端同一口径）。

    :returns: 空串 = 允许；否则是拒绝原因（直接给用户看）
    """
    if session is None:
        return "未登录"
    if not model.run_mode_known:
        return "运行模式未知（未连上服务端）"
    if model.is_auto():
        return "自动运行中：远程控制已被禁用（现场切换到手动后可用）"
    if session.role < proto.ROLE_ADMIN:
        return "权限不足：控制需要 root 账户"
    return ""
