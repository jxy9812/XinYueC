# -*- coding: utf-8 -*-
"""四档角色权限矩阵（端到端）：匿名 / 普通用户 / 操作者 / 管理员。

[测什么] 用户要求的口径：
    匿名     —— 只能查看
    普通用户 —— 查看 + 下载数据
    操作者   —— 普通 + 操作控制设备
    管理员   —— 全部

本用例起一个**独立临时配置**的服务端（--sim，临时 cfg 目录），用四个账户
分别登录，逐项验证**服务端**行为（不是只验证界面）：查看放行、下载/控制/
改配置的拒绝与放行，以及越权提示原文。

[为什么必须走真服务端] 权限不能只靠客户端灰按钮：本用例直接用协议帧
（绕过任何界面）请求，服务端若没有真闸就会在这里露馅。

运行：python tests/test_permissions.py [--port 6211]
"""

import json
import os
import shutil
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.dirname(HERE))

from hsfsrv import auth as auth_mod          # noqa: E402
from hsfsrv import bson, proto               # noqa: E402
from test_vfd_ops import Client              # noqa: E402  复用协议客户端

FAILED = []
PASSED = []


def check(desc, cond, detail=""):
    if cond:
        PASSED.append(desc)
        print("  [PASS] %s %s" % (desc, detail))
    else:
        FAILED.append(desc)
        print("  [FAIL] %s %s" % (desc, detail))


def section(title):
    print("\n=== %s ===" % title)


def make_users(path):
    """写四档账户（合法摘要；口令与用户名同）。"""
    users = []
    for name, role in (("guest", proto.ROLE_ANONYMOUS),
                       ("user", proto.ROLE_USER),
                       ("op", proto.ROLE_OPERATOR),
                       ("root", proto.ROLE_ADMIN)):
        salt = bytes(range(16))
        digest = auth_mod.derive_hash(name, salt, 1000)
        users.append({"name": name, "role": role, "iterations": 1000,
                      "locked": False, "salt": salt.hex(),
                      "hash": digest.hex()})
    with open(path, "w", encoding="utf-8") as fh:
        json.dump({"version": 1, "users": users}, fh)


def start_server(base_dir, port):
    """在临时目录起服务端（**users 在 ``<base>/cfg/hsf_users.json``**）。

    [路径口径] 服务端以 ``--config`` 文件所在目录为 base_dir，users 路径固定
    为 ``<base_dir>/cfg/hsf_users.json``。所以配置文件放在 base 根、账户库
    放在 base/cfg 下，四档账户才会被真正加载。
    """
    cfg_dir = os.path.join(base_dir, "cfg")
    os.makedirs(cfg_dir, exist_ok=True)
    cfg_path = os.path.join(base_dir, "hsf_server_py.json")
    with open(cfg_path, "w", encoding="utf-8") as fh:
        json.dump({"port": port, "serial": {"enabled": False},
                   "storage": {"enabled": True, "sampleMs": 1000}}, fh)
    make_users(os.path.join(cfg_dir, "hsf_users.json"))
    log = open(os.path.join(base_dir, "server.log"), "w", encoding="utf-8")
    env = dict(os.environ)
    pkg_root = os.path.dirname(HERE)
    env["PYTHONPATH"] = pkg_root + os.pathsep + env.get("PYTHONPATH", "")
    proc = subprocess.Popen(
        [sys.executable, "-m", "hsfsrv", "--sim", "--port", str(port),
         "--config", cfg_path],
        cwd=base_dir, env=env, stdout=log, stderr=subprocess.STDOUT)
    return proc, log


def login(host, port, user):
    last = None
    for _ in range(10):
        c = Client(host, port)
        try:
            c.hello()
            ok, info = c.login(user, user)
        except Exception as exc:                  # noqa: BLE001
            last = str(exc)
            c.close()
            time.sleep(0.4)
            continue
        if ok:
            return c, info
        last = info
        c.close()
        time.sleep(0.4)
    raise RuntimeError("登录失败：%s" % (last,))


def err_of(reply):
    return (reply[1].get("err") if reply else "") or ""


def main():
    port = 6211
    if "--port" in sys.argv:
        port = int(sys.argv[sys.argv.index("--port") + 1])
    base = tempfile.mkdtemp(prefix="hsf_perm_")
    proc, log = start_server(base, port)
    host = "127.0.0.1"
    try:
        time.sleep(4.0)
        # ---------------- 匿名（role 0） ----------------
        section("1. 匿名：只能查看")
        c, role = login(host, port, "guest")
        check("匿名登录角色", role == proto.ROLE_ANONYMOUS, "role=%s" % role)
        r = c.request(proto.CMD_VFD_SNAPSHOT, b"")
        check("匿名可拉快照", bool(r) and r[1].get("ok", True) is not False)
        r = c.request(proto.CMD_HISTORY_PAGE,
                      bson.encode({"offset": 0, "limit": 5, "export": True}))
        check("匿名导出被拒", bool(r) and r[1].get("ok") is False,
              "err=%r" % err_of(r)[:40])
        check("匿名导出提示含'权限不足'", "权限不足" in err_of(r))
        r = c.request(proto.CMD_DIGITAL_CONTROL,
                      bson.encode({"dev": 0, "do": 0, "on": True}))
        check("匿名控制被拒", bool(r) and r[1].get("ok") is False,
              "err=%r" % err_of(r)[:40])
        r = c.request(proto.CMD_CONFIG_SET,
                      bson.encode({"section": "collect", "vfdPollMs": 500}))
        check("匿名改配置被拒", bool(r) and r[1].get("ok") is False)
        c.close()
        time.sleep(1.0)

        # ---------------- 普通用户（role 1） ----------------
        section("2. 普通用户：查看 + 下载")
        c, role = login(host, port, "user")
        check("普通用户登录角色", role == proto.ROLE_USER, "role=%s" % role)
        r = c.request(proto.CMD_HISTORY_PAGE,
                      bson.encode({"offset": 0, "limit": 5, "export": True}))
        check("普通用户导出受理", bool(r) and r[1].get("ok") is True,
              "err=%r" % err_of(r)[:40])
        r = c.request(proto.CMD_DIGITAL_CONTROL,
                      bson.encode({"dev": 0, "do": 0, "on": True}))
        check("普通用户控制被拒", bool(r) and r[1].get("ok") is False,
              "err=%r" % err_of(r)[:40])
        check("普通用户控制提示含'权限不足'", "权限不足" in err_of(r))
        c.close()
        time.sleep(1.0)

        # ---------------- 操作者（role 3） ----------------
        section("3. 操作者：+ 设备控制")
        c, role = login(host, port, "op")
        check("操作者登录角色", role == proto.ROLE_OPERATOR, "role=%s" % role)
        r = c.request(proto.CMD_DIGITAL_CONTROL,
                      bson.encode({"dev": 0, "do": 1, "on": True}))
        check("操作者可控制设备", bool(r) and r[1].get("ok") is True,
              "err=%r" % err_of(r)[:40])
        r = c.request(proto.CMD_RUN_MODE, bson.encode({"mode": 1}))
        check("操作者切模式被拒", bool(r) and r[1].get("ok") is False,
              "err=%r" % err_of(r)[:40])
        r = c.request(proto.CMD_CONFIG_SET,
                      bson.encode({"section": "collect", "vfdPollMs": 500}))
        check("操作者改配置被拒", bool(r) and r[1].get("ok") is False,
              "err=%r" % err_of(r)[:40])
        check("操作者改配置提示含'权限不足'", "权限不足" in err_of(r))
        c.close()
        time.sleep(1.0)

        # ---------------- 管理员（role 2） ----------------
        section("4. 管理员：全部")
        c, role = login(host, port, "root")
        check("管理员登录角色", role == proto.ROLE_ADMIN, "role=%s" % role)
        r = c.request(proto.CMD_DIGITAL_CONTROL,
                      bson.encode({"dev": 0, "do": 1, "on": True}))
        check("管理员可控制设备", bool(r) and r[1].get("ok") is True,
              "err=%r" % err_of(r)[:40])
        r = c.request(proto.CMD_CONFIG_SET,
                      bson.encode({"section": "collect", "vfdPollMs": 500}))
        check("管理员可改配置", bool(r) and r[1].get("ok") is True,
              "err=%r" % err_of(r)[:40])
        r = c.request(proto.CMD_RUN_MODE, bson.encode({"mode": 1}))
        check("管理员可切模式", bool(r) and r[1].get("ok") is True,
              "err=%r" % err_of(r)[:40])
        c.request(proto.CMD_RUN_MODE, bson.encode({"mode": 0}))
        c.close()
    finally:
        try:
            proc.terminate()
            proc.wait(timeout=8)
        except Exception:                          # noqa: BLE001
            proc.kill()
        log.close()
        shutil.rmtree(base, ignore_errors=True)

    print("\n===== 权限矩阵: %d passed, %d failed =====" %
          (len(PASSED), len(FAILED)))
    if FAILED:
        print("失败项: %s" % ", ".join(FAILED))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
