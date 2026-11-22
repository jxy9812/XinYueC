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

from . import permissions as perms_mod
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
        """是否持有账户/配置管理位（PERM_ACCOUNT_ADMIN）。

        [为什么按位判而不是 role >= ROLE_ADMIN] 角色值不单调
        （Operator=3 是追加值）；`role >= ROLE_ADMIN` 会把操作者误当管理员。
        """
        return self.can(proto.PERM_ACCOUNT_ADMIN)

    def can(self, perm):
        """本会话是否持有 perm 的全部位（web 网关预检；服务端仍终审）。"""
        return perms_mod.can_role(int(self.role or 0), int(perm))

    def touch(self):
        self.last_seen_ms = _now_ms()

    def to_json(self):
        return {
            "user": self.user,
            "role": self.role,
            "roleText": role_text(self.role),
            "isAdmin": self.is_admin(),
            "perms": perms_mod.perms_of(int(self.role or 0)),
            "createdMs": self.created_ms,
            "lastSeenMs": self.last_seen_ms,
            "remote": self.remote,
        }


def role_text(role):
    """角色中文名（四档；与 permissions.ROLE_NAMES 同一事实源）。"""
    return perms_mod.role_name(role)


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


class LoginThrottle(object):
    """登录限流：IP 令牌桶 + 账号失败退避。

    [为什么必须有] `/api/login` 每来一次就要做一次口令派生（10000 轮
    HMAC；已挪到线程，但 CPU 照样被占）⇒ 登录洪泛能把网页端打停：
    SSE 心跳停发 ⇒ 上游按「静默 6s」判线断链 ⇒ 现场看起来「整个控制台
    死了」（而且**不需要任何凭据**就能触发）。⇒ 洪泛必须挡在密码学之前。

    [两把闸的分工]
      · **IP 令牌桶**：挡"一个来源刷很多账号"（口令喷洒）。按**来源 IP**
        计数（不含端口——同一 NAT 出口的多个操作员算一个来源，桶容量
        留了余量，正常使用不会误伤）。
      · **账号退避**：挡"很多来源刷同一个账号"（撞库/在线爆破）。失败
        次数越多 ⇒ 暂停越久（指数退避）。

    [为什么失败只退避账号、不退避 IP] 现场所有操作员可能共用一个出口
    IP ⇒ 一人输错口令若把整个 IP 退避，会把全厂的人一起挡在门外。
    ⇒ IP 只做速率桶，失败退避只落在**账号**上。
    """

    #: IP 令牌桶容量（突发次数）。
    #:
    #: [为什么给到 16] 现场所有操作员可能共用一个出口 IP（NAT）⇒ 容量太小
    #: 会把"几个操作员轮流登录"也当成洪泛。16 次突发 + 之后 2 秒一个，
    #: 既挡得住持续洪泛，又不误伤正常使用。
    CAPACITY = 16
    #: IP 令牌补充速率（个/秒）—— 2 秒补一个。
    REFILL_PER_S = 0.5
    #: 账号失败退避：基数（秒）/ 倍率 / 上限（秒）。
    BACKOFF_BASE_S = 2.0
    BACKOFF_MULT = 2.0
    BACKOFF_MAX_S = 60.0
    #: 键表上限（防键表无界增长；超限即丢掉最老的失败记录）。
    MAX_KEYS = 256

    def __init__(self):
        self._buckets = {}        # ip → [tokens, last_refill_ts]
        self._fails = {}          # user → (失败次数, 最近失败时刻)
        self._lock = threading.Lock()

    # ---- 键 ----
    @staticmethod
    def _ip_key(ip):
        return "ip:%s" % (ip or "?")

    @staticmethod
    def _user_key(user):
        return "user:%s" % (user or "?")

    def _refill(self, key, now):
        entry = self._buckets.get(key)
        if entry is None:
            entry = [float(self.CAPACITY), now]
            self._buckets[key] = entry
            return entry
        elapsed = max(0.0, now - entry[1])
        entry[0] = min(self.CAPACITY, entry[0] + elapsed * self.REFILL_PER_S)
        entry[1] = now
        return entry

    def _consume(self, key, now):
        """取一个令牌；没有 ⇒ False（被限流）。"""
        entry = self._refill(key, now)
        if entry[0] < 1.0:
            return False
        entry[0] -= 1.0
        return True

    def _backoff_left(self, user, now):
        """该账号的失败退避剩余秒数（0 = 不在退避中）。

        [第一次失败不延迟] 操作员手滑输错一次是常事 ⇒ 立刻让他重试；
        从第二次失败起才退避（2s → 4s → 8s …，上限 60s）。
        """
        info = self._fails.get(self._user_key(user))
        if not info:
            return 0.0
        count, last = info
        if count <= 1:
            return 0.0
        delay = min(self.BACKOFF_MAX_S,
                    self.BACKOFF_BASE_S * (self.BACKOFF_MULT ** (count - 2)))
        left = delay - (now - last)
        return left if left > 0 else 0.0

    def _trim(self, now):
        """键表有界：桶条目按"用得少"清、失败记录按"最老"清。"""
        if len(self._buckets) > self.MAX_KEYS:
            # 桶条目按最后一次补充时间排序（最久没用的先走）。
            old = sorted(self._buckets.items(), key=lambda kv: kv[1][1])
            for key, _v in old[:len(self._buckets) - self.MAX_KEYS]:
                self._buckets.pop(key, None)
        if len(self._fails) > self.MAX_KEYS:
            old = sorted(self._fails.items(), key=lambda kv: kv[1][1])
            for key, _v in old[:len(self._fails) - self.MAX_KEYS]:
                self._fails.pop(key, None)

    def check(self, ip, user):
        """登录前的闸（放行则返回 None；被限流则抛 LoginError）。

        :raises LoginError: 命中失败退避或 IP 令牌耗尽
        """
        now = time.monotonic()
        with self._lock:
            left = self._backoff_left(user, now)
            if left > 0:
                raise LoginError("该账户登录失败次数过多，请 %.0f 秒后重试"
                                 % (left + 0.999))
            if not self._consume(self._ip_key(ip), now):
                raise LoginError("登录尝试过于频繁，请稍后重试")
            self._trim(now)
        return None

    def on_failure(self, ip, user):
        """记一次失败（账号退避 +1 级）。"""
        now = time.monotonic()
        with self._lock:
            key = self._user_key(user)
            count, _last = self._fails.get(key, (0, now))
            self._fails[key] = (count + 1, now)
            self._trim(now)

    def on_success(self, ip, user):
        """登录成功 ⇒ 该账号的失败计数清零（IP 令牌不回补）。"""
        with self._lock:
            self._fails.pop(self._user_key(user), None)


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
    """设备控制类操作的闸门（与 Qt 客户端 page_base.control_blocked_reason
    同口径；只门 0x0101/0x0109/0x0111/0x0114 这类"动输出"的命令——
    0x0105/0x0115/0x0117 走各自的权限位，不套本闸）。

    :returns: 空串 = 允许；否则是拒绝原因（直接给用户看）
    """
    if session is None:
        return "未登录"
    if not session.can(proto.PERM_DEVICE_CONTROL):
        return perms_mod.denied_text(proto.PERM_DEVICE_CONTROL)
    if not model.run_mode_known:
        return "运行模式未知（未连上服务端）"
    if model.is_auto():
        # [措辞分两种 2026-11-12] 自动**模式**已切 ≠ 自动**运行**中；
        # 旧文案一律说"自动运行中" ⇒ 控制器没跑时也这么说，操作员会
        # 以为设备正被自动逻辑驱动。顺带点明"输出点由自动逻辑驱动"。
        if model.auto_running:
            return "自动运行中：远程控制已被禁用（输出点由自动逻辑驱动）"
        return "自动模式：远程控制已被禁用（输出点由自动逻辑驱动）"
    return ""
