# -*- coding: utf-8 -*-
"""S3 端到端：login 策略三键落地（2026-11-09）。

[测什么] 对齐 C 侧 XLoginPolicy_admitSession 的闸序（总数上限 → 匿名 →
同账并发），以及「拒绝路径回 AUTHSTATUS 拒绝码」（BAD_USER，与 C 侧
XLinkSessionAuthLookup_BadUser 同口径，客户端看到干净的 authFailed）：

    1. loginMaxSessions=2：第三条连接的登录被拒（状态码 AUTHSTATUS_BAD_USER）；
       前两条不受影响；
    2. loginAllowAnonymous=false（缺省）：未宣告认证的登录请求被拒
       （跳过 Hello 直接发 AuthRequest 的"想匿名进来"路径）；
       正常宣告 CAP_AUTH 的登录不受影响；
    3. 单元矩阵：三键逐键判定 + 边界（0=不限、恰好等于上限）。

[怎么跑]
    python3 -I tests/test_login_policy_enforce.py
    pytest tests/test_login_policy_enforce.py -q
"""

import asyncio
import hashlib
import hmac
import os
import shutil
import socket
import struct
import sys
import tempfile
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.dirname(HERE))

from hsfsrv import auth as auth_mod          # noqa: E402
from hsfsrv import bson, config as cfgmod    # noqa: E402
from hsfsrv import proto, xlink              # noqa: E402
from fake_devices import build_fake_app      # noqa: E402
from test_vfd_ops import Client              # noqa: E402  复用协议客户端


async def _spin_server(basedir, log, **cfg_overrides):
    """起一份假设备服务端（与 test_auto_alarm_publish 同款；独立副本防耦合）。"""
    cfg = cfgmod.Config()
    cfg.resolve_paths(basedir)
    cfg.config_path = os.path.join(basedir, "cfg", "x.json")
    cfg.users_path = os.path.join(basedir, "cfg", "hsf_users.json")
    os.makedirs(os.path.join(basedir, "cfg"), exist_ok=True)
    cfg.port = 0
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


def _wait_challenge_or_result(c, seconds=3.0):
    """等 AuthChallenge；期间若先到 AuthResult（策略拒绝不走挑战）先记下。

    [为什么不用 Client._wait_session] 它对非目标帧一律丢弃 —— 策略拒绝
    的 AuthResult 会在等挑战的 3s 里被吃掉，之后再也等不到（实测踩坑）。
    """
    deadline = time.time() + seconds
    result = None
    while time.time() < deadline:
        f = c._read_frame()
        if f:
            mclass, mcode, _rid, _dt, payload = f
            if (mclass == proto.MSGCLASS_SESSION
                    and mcode == proto.SESSION_AUTH_CHALLENGE):
                return "challenge", payload
            if (mclass == proto.MSGCLASS_SESSION
                    and mcode == proto.SESSION_AUTH_RESULT
                    and result is None):
                result = payload        # 先记下，挑战若随后到仍优先
        else:
            time.sleep(0.01)
    return "result", result


def login_status(c, user, password):
    """登录并返回 (ok, 状态码)。与 Client.login 同款交换，多暴露状态码。"""
    name = user.encode("utf-8")
    c._send(proto.MSGCLASS_SESSION, proto.SESSION_AUTH_REQUEST,
            proto.DATATYPE_RAW, bytes([len(name)]) + name)
    kind, ch = _wait_challenge_or_result(c)
    if kind == "result" or not ch or len(ch) < 8:
        # 没有挑战就被拒：直接收 AuthResult（策略拒绝不走挑战）。
        res = ch if (kind == "result" and ch) else None
        if res is None:
            res = c._wait_session(proto.SESSION_AUTH_RESULT)
        if res and len(res) >= 4:
            text_len = struct.unpack("<H", res[2:4])[0]
            text = res[4:4 + text_len].decode("utf-8", "replace")
            return False, res[0], text
        return False, None, "no reply"
    method = ch[0]
    salt_len = ch[1]
    iterations = struct.unpack("<I", ch[4:8])[0]
    salt = ch[8:8 + salt_len]
    nonce = ch[8 + salt_len:8 + salt_len + proto.XLINK_AUTH_NONCE_BYTES]
    digest = hmac.new(password.encode("utf-8"), salt,
                      hashlib.sha256).digest()
    for _ in range(iterations - 1):
        digest = hmac.new(password.encode("utf-8"), digest,
                          hashlib.sha256).digest()
    resp = hmac.new(digest, nonce, hashlib.sha256).digest()
    c._send(proto.MSGCLASS_SESSION, proto.SESSION_AUTH_RESPONSE,
            proto.DATATYPE_RAW,
            bytes([method, 0]) + struct.pack("<H", len(resp)) + resp)
    res = c._wait_session(proto.SESSION_AUTH_RESULT)
    if not res or len(res) < 4:
        return False, None, "no auth result"
    text_len = struct.unpack("<H", res[2:4])[0]
    text = res[4:4 + text_len].decode("utf-8", "replace")
    if res[0] != proto.AUTHSTATUS_OK:
        return False, res[0], text
    return True, res[0], text


def login_raw_request(c, user):
    """跳过 Hello 直接发 AuthRequest（模拟"未宣告认证就想进来"的老对端）。"""
    name = user.encode("utf-8")
    c._send(proto.MSGCLASS_SESSION, proto.SESSION_AUTH_REQUEST,
            proto.DATATYPE_RAW, bytes([len(name)]) + name)


# ============================================================================
#                端到端：会话总数上限（maxSessions 第三连接被拒）
# ============================================================================

async def _case_max_sessions(basedir, log, max_sessions):
    business, server = await _spin_server(
        basedir, log,
        login_max_sessions=max_sessions,
        login_allow_concurrent=True,     # 隔离闸3，专测闸1（总数上限）
    )
    errors = []

    def scenario():
        try:
            # 前（max-1）条连接：正常登录。
            cs = []
            for i in range(max_sessions - 1):
                c = Client("127.0.0.1", server.port)
                c.hello()
                ok, status, _t = login_status(c, "root", "root")
                assert ok, "第 %d 条连接应登录成功（status=%s）" % (
                    i + 1, status)
                cs.append(c)

            # 第 max 条连接：连接建立即入会话表（xlink.Server._handle_client
            # accept 即 append，与 C 侧 XLinkServer_sessionCount 的"当前
            # 总数"口径一致，含连接中未认证的会话）⇒ 总数已到上限 ⇒ 登录
            # 被拒，回 AUTHSTATUS_BAD_USER。
            cx = Client("127.0.0.1", server.port)
            cx.hello()
            ok, status, text = login_status(cx, "root", "root")
            assert not ok, "第 %d 条连接应被拒（got ok=%s）" % (
                max_sessions, ok)
            assert status == proto.AUTHSTATUS_BAD_USER, \
                "拒绝码应为 BAD_USER(1)，实得 %s（%s）" % (status, text)

            # 存量会话不受影响（策略不追溯，已准入的不中途掐断）。
            r = cs[0].request(proto.CMD_DEVICE_HEALTH, b"")
            assert r is not None, "已登录会话不受新策略影响"
            for c in cs:
                c.close()
            cx.close()
        except Exception as exc:                    # noqa: BLE001
            errors.append(exc)

    thread = threading.Thread(target=scenario, name="policy-client")
    thread.start()
    try:
        while thread.is_alive():
            await asyncio.sleep(0.05)
    finally:
        thread.join()
        await server.stop()
    if errors:
        raise errors[0]


def test_max_sessions_2_rejects_third_connection():
    """maxSessions=3：前两条登录成功，第三条连接被拒（BAD_USER）。"""
    base = tempfile.mkdtemp(prefix="hsf_login_")
    try:
        asyncio.run(_case_max_sessions(base, lambda m: None, 3))
    finally:
        shutil.rmtree(base, ignore_errors=True)


def test_max_sessions_2_boundary_rejects_second_connection():
    """maxSessions=2 边界：第二条连接的登录即被拒。

    [为什么不是"第三条"] 总数口径与 C 一致：**含连接中未认证的会话**
    （accept 即入表）。上限 2 ⇒ 第 1 条登录时总数 1、放行；第 2 条连接
    一建立总数就到 2，其登录请求被拒 —— 与 C 侧 XLoginPolicy_admitSession
    的 activeSessions >= maxSessions 同一条边界。
    """
    base = tempfile.mkdtemp(prefix="hsf_login_b_")
    try:
        asyncio.run(_case_max_sessions(base, lambda m: None, 2))
    finally:
        shutil.rmtree(base, ignore_errors=True)


# ============================================================================
#                端到端：匿名未开放 → 未宣告认证的登录被拒
# ============================================================================

def raw_auth_reject_status(c, user):
    """跳过 Hello 直接发 AuthRequest，返回收到的 AuthResult 状态码。"""
    login_raw_request(c, user)
    res = c._wait_session(proto.SESSION_AUTH_RESULT)
    if not res or len(res) < 4:
        return None, "no reply"
    text_len = struct.unpack("<H", res[2:4])[0]
    text = res[4:4 + text_len].decode("utf-8", "replace")
    return res[0], text


async def _case_anonymous_gate(basedir, log):
    # 缺省即 login_allow_anonymous=False；上限给大，隔离闸1。
    business, server = await _spin_server(
        basedir, log, login_max_sessions=8)
    errors = []

    def scenario():
        try:
            # ---- 未宣告认证（跳过 Hello 直接发 AuthRequest）→ 拒 ----
            c1 = Client("127.0.0.1", server.port)
            status, text = raw_auth_reject_status(c1, "root")
            assert status == proto.AUTHSTATUS_BAD_USER, \
                "未宣告认证的登录应被拒为 BAD_USER(1)，实得 %s（%s）" % (
                    status, text)
            # ---- 重试也进不来（不能靠"再发一次"绕过匿名闸）----
            status, text = raw_auth_reject_status(c1, "root")
            assert status == proto.AUTHSTATUS_BAD_USER, \
                "重试不应绕过匿名闸，实得 %s（%s）" % (status, text)
            c1.close()

            # ---- 正常宣告 CAP_AUTH 的登录不受影响 ----
            c2 = Client("127.0.0.1", server.port)
            c2.hello()
            ok, status, _t = login_status(c2, "root", "root")
            assert ok, "宣告 CAP_AUTH 的正常登录应放行（status=%s）" % status
            c2.close()
        except Exception as exc:                    # noqa: BLE001
            errors.append(exc)

    thread = threading.Thread(target=scenario, name="anon-client")
    thread.start()
    try:
        while thread.is_alive():
            await asyncio.sleep(0.05)
    finally:
        thread.join()
        await server.stop()
    if errors:
        raise errors[0]


def test_anonymous_gate_rejects_undeclared_auth_login():
    base = tempfile.mkdtemp(prefix="hsf_anon_")
    try:
        asyncio.run(_case_anonymous_gate(base, lambda m: None))
    finally:
        shutil.rmtree(base, ignore_errors=True)


# ============================================================================
#                     单元矩阵：policy_blocks_login 三键
# ============================================================================

class _StubSession(object):
    """策略闸用的最小会话桩（xlink.Session 的判定面子集）。"""

    def __init__(self, declared=True, user=None, ready=True):
        self.peer_capability = proto.CAP_AUTH if declared else 0
        self.state = (xlink.SessionState.AUTHENTICATING if declared
                      else xlink.SessionState.CONNECTED)
        self.auth_user = user
        self._ready = ready

    def auth_declared(self):
        if self.peer_capability & proto.CAP_AUTH:
            return True
        return (self.state == xlink.SessionState.AUTHENTICATING
                or bool(self.auth_user))

    def is_ready(self):
        return self._ready


class _StubServer(object):
    def __init__(self, count):
        self._count = count
        self.sessions = []

    def session_count(self):
        return self._count


def _app_with(cfg_kwargs):
    from hsfsrv.app import App

    cfg = cfgmod.Config()
    for key, val in cfg_kwargs.items():
        setattr(cfg, key, val)

    class _A(App):
        def __init__(self, c):     # 绕开设备装配，只要 cfg/策略闸
            App.__init__(self, c, log=lambda m: None)

    app = _A(cfg)
    return app


def test_gate1_total_session_cap():
    app = _app_with({"login_max_sessions": 2,
                     "login_allow_concurrent": True})
    app.server = _StubServer(2)
    assert app.policy_blocks_login("op", _StubSession()) is True, \
        "达到上限应拒"
    app.server = _StubServer(1)
    assert app.policy_blocks_login("op", _StubSession()) is False, \
        "未达上限应放"
    app.cfg.login_max_sessions = 0
    app.server = _StubServer(999)
    assert app.policy_blocks_login("op", _StubSession()) is False, \
        "0=不限"


def test_gate2_anonymous_closed_blocks_undeclared():
    app = _app_with({"login_allow_anonymous": False,
                     "login_allow_concurrent": True,
                     "login_max_sessions": 8})
    app.server = _StubServer(1)
    assert app.policy_blocks_login("root", _StubSession(declared=False)) \
        is True, "匿名未开放 + 未宣告认证 ⇒ 拒"
    assert app.policy_blocks_login("root", _StubSession(declared=True)) \
        is False, "宣告了认证 ⇒ 放（落到闸3：非同账并发情形）"


def test_gate3_concurrent_unchanged():
    app = _app_with({"login_allow_anonymous": True,
                     "login_allow_concurrent": False,
                     "login_max_sessions": 8})
    srv = _StubServer(2)
    srv.sessions = [_StubSession(declared=True, user="root", ready=True)]
    app.server = srv
    me = _StubSession(declared=True)
    srv.sessions.append(me)
    assert app.policy_blocks_login("root", me) is True, \
        "root 已在线（同账并发关）⇒ 拒（现状维持）"
    app.cfg.login_allow_concurrent = True
    assert app.policy_blocks_login("root", me) is False, \
        "同账并发开 ⇒ 放"


def main():
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
    print("\n===== 登录策略落地: %d failed =====" % len(failed))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
