# -*- coding: utf-8 -*-
"""XLink 登录认证：迭代 HMAC-SHA256 挑战应答（对齐 XLinkAuth.c）。

[为什么不存明文口令] 账户库只存「盐 + 迭代摘要」：H = HMAC^N(口令, 盐)。
登录时服务端下发一次性 nonce，客户端回 HMAC(H, nonce)，服务端比对——
口令与摘要都不上网，摘要也不随每次登录变化（防重放靠 nonce）。

[为什么迭代 10000 次] 让离线爆破单条口令的成本 ×10000。迭代次数写进
挑战帧，客户端照办，故将来提高迭代数不需要改客户端。

[服务端证明] 认证通过时服务端还要回 HMAC(H, nonce||0x01)：客户端据此确认
对面确实持有 H（不是中间人对着口令瞎猜）。域分离字节 0x01 防止与
客户端的 HMAC(H, nonce) 撞用途。

本模块**不打印任何口令/摘要/nonce 内容**（红线，与 C 侧一致）。
"""

import hashlib
import hmac
import os
import struct

from . import proto


def derive_hash(password, salt, iterations):
    """H = 迭代 HMAC-SHA256(口令, 盐)。

    首轮 key=口令、msg=盐；其后 key=口令、msg=上一轮摘要，共 iterations 轮。
    与 C 侧 XLinkAuth_deriveHash 完全同式（两边算出的摘要必须逐字节相同，
    否则服务端下发的账户库在 C 客户端上无法登录）。
    """
    if iterations < 1:
        raise ValueError("迭代次数必须 >= 1")
    key = password.encode("utf-8")
    if not key:
        raise ValueError("口令不得为空")
    digest = hmac.new(key, salt, hashlib.sha256).digest()
    for _ in range(iterations - 1):
        digest = hmac.new(key, digest, hashlib.sha256).digest()
    return digest


def compute_response(pased_hash, nonce):
    """客户端应答：HMAC(H, nonce)。"""
    return hmac.new(pased_hash, nonce, hashlib.sha256).digest()


def compute_server_proof(pased_hash, nonce):
    """服务端证明：HMAC(H, nonce || 0x01)。"""
    return hmac.new(pased_hash, nonce + bytes([proto.AUTH_PROOF_DOMAIN]),
                    hashlib.sha256).digest()


def constant_time_equal(a, b):
    """定长摘要比较（避免按字节提前返回泄漏前缀匹配长度）。"""
    return hmac.compare_digest(bytes(a), bytes(b))


def random_bytes(n):
    """密码学随机字节（盐 / nonce）。"""
    return os.urandom(n)


# ============================================================================
#                          载荷编解码（逐字段小端）
# ============================================================================

def encode_request(name):
    """AuthRequest：[u8 nameLen][name...]（UTF-8 非终止符）。"""
    raw = name.encode("utf-8")
    if not raw or len(raw) >= proto.XLINK_AUTH_NAME_BYTES:
        raise ValueError("用户名长度须在 1..%d" % (proto.XLINK_AUTH_NAME_BYTES - 1))
    return bytes([len(raw)]) + raw


def decode_request(payload):
    """解 AuthRequest，返回用户名 str；畸形返回 None。"""
    if not payload:
        return None
    n = payload[0]
    if n == 0 or n > proto.XLINK_AUTH_NAME_BYTES or len(payload) < 1 + n:
        return None
    return payload[1:1 + n].decode("utf-8", "replace")


def encode_challenge(method, iterations, salt, nonce):
    """AuthChallenge：[u8 method][u8 saltLen][u16 reserved][u32 iterations]
    [salt][nonce 32]。"""
    if len(salt) != proto.XLINK_AUTH_SALT_BYTES:
        raise ValueError("盐长度必须 %d" % proto.XLINK_AUTH_SALT_BYTES)
    if len(nonce) != proto.XLINK_AUTH_NONCE_BYTES:
        raise ValueError("nonce 长度必须 %d" % proto.XLINK_AUTH_NONCE_BYTES)
    if iterations < 1:
        raise ValueError("迭代次数必须 >= 1")
    return (bytes([method, proto.XLINK_AUTH_SALT_BYTES, 0, 0])
            + struct.pack("<I", iterations) + salt + nonce)


def decode_response(payload):
    """解 AuthResponse：[u8 method][u8 reserved][u16 len][response 32]。

    返回 (method, response)；长度字段与固定摘要长度不符即判畸形（C 侧同口径，
    不接受变长摘要——否则对端可用短摘要降级强度）。
    """
    if len(payload) < 4:
        return None
    method = payload[0]
    n = struct.unpack("<H", payload[2:4])[0]
    if n != proto.XLINK_AUTH_NONCE_BYTES or len(payload) < 4 + n:
        return None
    return method, payload[4:4 + n]


def encode_result(status, role, text, server_proof):
    """AuthResult：[u8 status][u8 role][u16 textLen][text][proof 32]。

    失败时 proof 传全 0（C 侧解码要求字段存在，但只在 status==Ok 时采信）。
    """
    raw = text.encode("utf-8")[:proto.XLINK_AUTH_TEXT_BYTES]
    proof = bytes(server_proof) if server_proof else bytes(proto.XLINK_AUTH_NONCE_BYTES)
    if len(proof) != proto.XLINK_AUTH_NONCE_BYTES:
        proof = bytes(proto.XLINK_AUTH_NONCE_BYTES)
    return (bytes([status, role]) + struct.pack("<H", len(raw)) + raw + proof)


# ============================================================================
#                            账户库
# ============================================================================

class UserRecord(object):
    """单条账户：盐 + 迭代摘要 + 角色（**不含明文口令**）。"""

    __slots__ = ("name", "salt", "digest", "iterations", "role", "locked",
                 "flags")

    def __init__(self, name, salt, digest, iterations, role, locked=False):
        self.name = name
        self.salt = salt
        self.digest = digest
        self.iterations = iterations
        self.role = role
        self.locked = locked
        self.flags = 0

    def to_json(self):
        """落盘形态：与 C 侧 XLinkUserStore 的键表逐字一致（账户库可互通）。"""
        return {
            "name": self.name,
            "role": self.role,
            "iterations": self.iterations,
            "locked": bool(self.locked),
            "salt": self.salt.hex(),
            "hash": self.digest.hex(),
        }

    @classmethod
    def from_json(cls, obj):
        """从 C 侧账户库条目恢复；字段不全或类型不符返回 None（跳过该条）。"""
        try:
            name = obj["name"]
            salt = bytes.fromhex(obj["salt"])
            digest = bytes.fromhex(obj["hash"])
            iterations = int(obj.get("iterations", proto.XLINK_AUTH_HASH_ITERATIONS))
            role = int(obj.get("role", proto.ROLE_USER))
            locked = bool(obj.get("locked", False))
        except (KeyError, TypeError, ValueError):
            return None
        if not isinstance(name, str) or not name:
            return None
        if len(salt) != proto.XLINK_AUTH_SALT_BYTES:
            return None
        if len(digest) != proto.XLINK_AUTH_NONCE_BYTES:
            return None
        if iterations < 1:
            return None
        if role not in (proto.ROLE_ANONYMOUS, proto.ROLE_USER,
                        proto.ROLE_ADMIN, proto.ROLE_OPERATOR):
            return None
        return cls(name, salt, digest, iterations, role, locked)


class UserStore(object):
    """账户库：内存字典 + JSON 落盘（对齐 XLinkUserStore 的 {version,users[]}）。

    口令只在 add_user/create_defaults 这一瞬间以明文存在，随即被派生摘要替换，
    明文不落盘、不进日志。
    """

    VERSION = 1

    def __init__(self, path=None, log=None):
        self.path = path
        self.users = {}
        self._log = log or (lambda msg: None)

    # ---- 查询 ----
    def __len__(self):
        return len(self.users)

    def names(self):
        return list(self.users.keys())

    def find(self, name):
        """按用户名精确查（大小写敏感，与 C 侧 XString_equals_utf8 同口径）。"""
        return self.users.get(name)

    # ---- 增改 ----
    def add_user(self, name, password, role, iterations=None):
        """新增或更新账户（已存在则改口令+角色，并解锁）。"""
        iterations = iterations or proto.XLINK_AUTH_HASH_ITERATIONS
        salt = random_bytes(proto.XLINK_AUTH_SALT_BYTES)
        digest = derive_hash(password, salt, iterations)
        self.users[name] = UserRecord(name, salt, digest, iterations, role)
        return self.users[name]

    def create_defaults(self):
        """首次启动：admin=只读(User)、root=管理员(可控制)。"""
        self.add_user(proto.DEFAULT_USER_NAME, proto.DEFAULT_USER_PASS,
                      proto.ROLE_USER)
        self.add_user(proto.DEFAULT_ROOT_NAME, proto.DEFAULT_ROOT_PASS,
                      proto.ROLE_ADMIN)

    # ---- 持久化 ----
    def load(self):
        """读 JSON；文件不存在视为空库（返回 True，调用方随后建缺省账户）。

        解析失败**不**清空已有内存态：返回 False 让上层决定是否继续——
        静默吞掉会让「账户库损坏」伪装成「全都能登录」。
        """
        if not self.path:
            return False
        import json
        try:
            with open(self.path, "r", encoding="utf-8") as fh:
                doc = json.load(fh)
        except FileNotFoundError:
            return True
        except (OSError, ValueError) as exc:
            self._log("账户库读取失败（%s）：%s" % (self.path, exc))
            return False
        users = doc.get("users") if isinstance(doc, dict) else None
        if not isinstance(users, list):
            self._log("账户库格式非法（缺 users 数组）：%s" % self.path)
            return False
        loaded = 0
        for item in users:
            if not isinstance(item, dict):
                continue
            rec = UserRecord.from_json(item)
            if rec is None:
                self._log("账户条目非法，已跳过：%r" % (item.get("name"),))
                continue
            self.users[rec.name] = rec
            loaded += 1
        self._log("账户库已加载（%s，%d 账户）" % (self.path, loaded))
        return True

    def save(self):
        """原子落盘（先写临时文件再 rename，避免掉电留下半截 JSON）。"""
        if not self.path:
            return False
        import json
        import os
        doc = {
            "version": self.VERSION,
            "users": [rec.to_json() for rec in self.users.values()],
        }
        # 目录不存在就先建：首次运行常见（cfg/ 还没落过任何文件），
        # 不建会以 ENOENT 失败并让"缺省账户"永远落不了盘。
        parent = os.path.dirname(os.path.abspath(self.path))
        if parent and not os.path.isdir(parent):
            try:
                os.makedirs(parent)
            except OSError as exc:
                self._log("账户库目录创建失败（%s）：%s" % (parent, exc))
                return False
        tmp = self.path + ".tmp"
        try:
            with open(tmp, "w", encoding="utf-8") as fh:
                json.dump(doc, fh, ensure_ascii=False, indent=1)
                fh.flush()
                os.fsync(fh.fileno())
            os.replace(tmp, self.path)
        except OSError as exc:
            self._log("账户库落盘失败（%s）：%s" % (self.path, exc))
            try:
                os.unlink(tmp)
            except OSError:
                pass
            return False
        return True
