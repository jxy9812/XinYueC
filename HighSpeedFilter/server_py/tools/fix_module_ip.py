# -*- coding: utf-8 -*-
"""把 MT3A-IO1632 模块的 IP 改回指定网段（带回滚保险）。

用途（本次现场）：`.13` 被误改成从站模式后跳到 `192.168.1.20`，
需要把它改回 `192.168.0.13`。

    python tools/fix_module_ip.py --dry-run      # 只读现状，不改
    python tools/fix_module_ip.py                # 执行：临时跨段 → 改 IP → 还原

[为什么要临时跨网段]
模块现在在 192.168.1.20，本机有线网卡是 192.168.0.243/24。跨网段访问需要
给有线网卡临时加一个 192.168.1.x 副地址。**这一步有断网风险**（Windows 可能
把默认路由切到没有网关的有线口），所以：

  * 动网卡前存快照；加完立即验外网；**不通就立刻回滚**
  * 全程 watchdog 线程兜底
  * 改完 IP 后无论如何都删掉临时副地址

[怎么改模块 IP]
模块的 Web 配置：POST /login_ch.cgi 登录 → POST /config_ch.cgi 提交表单。
该固件 HTTP 不规范（POST 无状态行），故用裸 socket 收发、手工解析。

**关键**：改 `slaId`（从站号）而不只是改 `lip00`。
从站模式下 IP 由 `slaId` 决定：`slaId = N` → 取 `lip0N`。
所以要让它在 192.168.0.13，得让 `slaId = 0` 且 `lip00 = 192.168.0.13`。
"""

import argparse
import json
import socket
import subprocess
import sys
import threading
import time
import urllib.parse

HERE = __import__("os").path.dirname(__import__("os").path.abspath(__file__))
sys.path.insert(0, HERE)

# 复用自愈脚本的网络原语（快照/加地址/回滚/外网检查）
from net_selfheal import (  # noqa: E402
    TARGET_IFACE_INDEX, log, is_admin, snapshot_iface, add_secondary_address,
    remove_address, restore_snapshot, fallback_to_dhcp, wan_alive, tcp_ok,
)

#: 模块当前地址（跨段访问时用）。
MODULE_CURRENT_IP = "192.168.1.20"
#: 目标地址。
MODULE_TARGET_IP = "192.168.0.13"
MODULE_TARGET_MASK = "255.255.255.0"
MODULE_TARGET_GW = "192.168.0.1"
#: 从站号：决定用 lip0N 里哪一项。要让 IP=192.168.0.13 ⇒ slaId=0。
MODULE_TARGET_SLA_ID = 0

#: 临时副地址（跨段用）。
TEMP_ADDRESS = "192.168.1.50"
TEMP_MASK = "255.255.255.0"

#: 模块 Web 账密（与现场一致）。
WEB_USER = "amx666"
WEB_PASS = "amx666"

#: watchdog 上限。
WATCHDOG_SECONDS = 240


# ============================================================================
#                        裸 socket HTTP（该固件不规范）
# ============================================================================

def http_request(host, path, body=None, timeout=8.0, method=None):
    """发一个 HTTP 请求并返回响应体（bytes）。

    [为什么不用 urllib] 该固件 POST 响应**没有状态行**，urllib 会抛
    BadStatusLine；连接也常被立刻关闭。裸 socket 手工收发最稳。
    """
    if method is None:
        method = "POST" if body is not None else "GET"
    headers = ["%s %s HTTP/1.0" % (method, path),
               "Host: %s" % host,
               "Connection: close",
               "User-Agent: Mozilla/5.0"]
    payload = b""
    if body is not None:
        payload = body if isinstance(body, bytes) else body.encode("utf-8")
        headers.append("Content-Type: application/x-www-form-urlencoded")
        headers.append("Content-Length: %d" % len(payload))
    request = ("\r\n".join(headers) + "\r\n\r\n").encode("ascii") + payload

    sock = socket.create_connection((host, 80), timeout)
    try:
        sock.settimeout(timeout)
        sock.sendall(request)
        chunks = []
        while True:
            try:
                data = sock.recv(8192)
            except socket.timeout:
                break
            if not data:
                break
            chunks.append(data)
            # 该固件不总是发 Content-Length；收到 </html> 或足够长就停
            if b"</html>" in data or sum(len(c) for c in chunks) > 65536:
                break
        return b"".join(chunks)
    finally:
        sock.close()


def read_config(host):
    """读 config.js 并解析成 dict（该固件 JSON 不规范，用正则兜）。"""
    import re
    raw = http_request(host, "/config.js")
    text = raw.decode("utf-8", "replace")
    pairs = re.findall(r'"([A-Za-z0-9_]+)"\s*:\s*"?([^,"}]*)"?', text)
    return {k: v.strip() for k, v in pairs}


# ============================================================================
#                        改模块 IP
# ============================================================================

def set_module_ip(host, new_ip, mask, gateway, sla_id):
    """通过 Web 界面把模块 IP 改到 new_ip。

    :returns: (ok: bool, detail: str)
    """
    log("登录模块 %s …" % host)
    login_body = urllib.parse.urlencode({"user": WEB_USER, "pwd": WEB_PASS})
    resp = http_request(host, "/login_ch.cgi", login_body)
    log("  登录响应 %d 字节" % len(resp))
    if not resp:
        return False, "登录无响应"

    # 提交完整配置：以现有配置为基底，只改 IP 相关字段
    cfg = read_config(host)
    if not cfg:
        return False, "读不到 config.js"

    log("  当前: ms=%s slaId=%s lip00=%s"
        % (cfg.get("ms", "?"), cfg.get("slaId", "?"), cfg.get("lip00", "?")))

    form = dict(cfg)
    form["lip00"] = new_ip
    form["slaId"] = str(sla_id)
    form["gw"] = gateway
    form["sub"] = mask
    # 账密三件套必须带上且 5~9 位（固件校验）
    form["name"] = WEB_USER
    form["pwd"] = WEB_PASS
    form["pwd2"] = WEB_PASS
    # 去掉可能让固件困惑的空字段
    form = {k: v for k, v in form.items() if v != ""}

    log("  提交新配置：lip00=%s slaId=%s gw=%s" % (new_ip, sla_id, gateway))
    body = urllib.parse.urlencode(form)
    resp = http_request(host, "/config_ch.cgi", body, timeout=15.0)
    text = resp.decode("utf-8", "replace") if resp else ""
    log("  提交响应 %d 字节%s" % (len(resp),
                                 "（含倒计时页，通常表示成功）"
                                 if "倒计时" in text or "Configuration" in text
                                 else ""))
    if not resp:
        return False, "提交无响应"
    return True, text[:200]


# ============================================================================
#                         主流程（带回滚）
# ============================================================================

class Fixer(object):
    def __init__(self, iface_index, dry_run=False):
        self.iface_index = iface_index
        self.dry_run = dry_run
        self.snapshot = None
        self.address_added = False
        self.finished = False
        self._lock = threading.Lock()

    def _watchdog(self):
        deadline = time.time() + WATCHDOG_SECONDS
        while time.time() < deadline:
            time.sleep(1.0)
            with self._lock:
                if self.finished:
                    return
        with self._lock:
            if self.finished:
                return
        log("[watchdog] 超时，强制清理")
        self.cleanup("watchdog 超时")

    def cleanup(self, reason):
        """删掉临时副地址 + 验外网（幂等）。"""
        with self._lock:
            need = self.address_added
            self.address_added = False
        log("=== 清理（%s）===" % reason)
        if need:
            remove_address(self.iface_index, TEMP_ADDRESS)
        alive, detail = wan_alive()
        log("外网：%s（%s）" % ("通" if alive else "不通", detail))
        if not alive:
            log("[保护] 外网不通，还原快照")
            if not restore_snapshot(self.iface_index, self.snapshot):
                fallback_to_dhcp(self.iface_index)
            time.sleep(2)
            alive2, detail2 = wan_alive()
            log("还原后外网：%s（%s）" % ("通" if alive2 else "不通", detail2))
        log("=== 清理结束 ===")

    def run(self):
        log("=" * 66)
        log("把模块 IP 改回 %s（当前 %s）"
            % (MODULE_TARGET_IP, MODULE_CURRENT_IP))

        if not is_admin() and not self.dry_run:
            log("[失败] 需要管理员权限")
            return 2

        # 1) 基线
        alive, detail = wan_alive()
        log("基线外网：%s（%s）" % ("通" if alive else "不通", detail))
        self.snapshot = snapshot_iface(self.iface_index)
        log("已保存快照：%s" % json.dumps(self.snapshot, ensure_ascii=False))
        if self.snapshot is None:
            log("[失败] 读不到网卡快照，拒绝改动")
            return 2

        # 2) 模块是否已在原网段？
        if tcp_ok(MODULE_CURRENT_IP, 80, 1.5):
            log("模块在当前网段可达，无需跨段")
        else:
            log("模块不在当前网段，需要临时加副地址 %s" % TEMP_ADDRESS)
            if self.dry_run:
                log("[dry-run] 到此为止（不修改任何配置）")
                self.finished = True
                return 0
            with self._lock:
                self.address_added = True
            if not add_secondary_address(self.iface_index, TEMP_ADDRESS,
                                         TEMP_MASK):
                self.cleanup("加副地址失败")
                self.finished = True
                return 3
            time.sleep(3)
            alive, detail = wan_alive()
            log("加地址后外网：%s（%s）" % ("通" if alive else "不通", detail))
            if not alive:
                log("[保护] 外网被影响，立即回滚")
                self.cleanup("加副地址导致断网")
                self.finished = True
                return 4

        # 3) 确认模块可达
        if not tcp_ok(MODULE_CURRENT_IP, 80, 3.0):
            log("[失败] 模块 %s:80 不可达，放弃" % MODULE_CURRENT_IP)
            self.cleanup("模块不可达")
            self.finished = True
            return 5
        log("模块 Web 可达")

        if self.dry_run:
            cfg = read_config(MODULE_CURRENT_IP)
            log("[dry-run] 当前配置：ms=%s slaId=%s lip00=%s gw=%s"
                % (cfg.get("ms", "?"), cfg.get("slaId", "?"),
                   cfg.get("lip00", "?"), cfg.get("gw", "?")))
            self.cleanup("dry-run 结束")
            self.finished = True
            return 0

        # 4) 改 IP
        log("--- 开始修改模块 IP ---")
        ok, detail = set_module_ip(MODULE_CURRENT_IP, MODULE_TARGET_IP,
                                   MODULE_TARGET_MASK, MODULE_TARGET_GW,
                                   MODULE_TARGET_SLA_ID)
        log("修改结果：%s %s" % ("成功" if ok else "失败", detail))
        if not ok:
            self.cleanup("修改失败")
            self.finished = True
            return 6

        # 5) 等它软重启
        log("等待模块软重启（8 秒）…")
        time.sleep(8)

        # 6) 清理临时地址（必须！否则本机多一个跨段地址）
        self.cleanup("改完，清理临时地址")

        # 7) 验证新地址
        log("--- 验证 %s ---" % MODULE_TARGET_IP)
        for attempt in range(6):
            if tcp_ok(MODULE_TARGET_IP, 80, 2.0):
                log("  ✓ %s:80 可达（第 %d 次尝试）"
                    % (MODULE_TARGET_IP, attempt + 1))
                cfg = read_config(MODULE_TARGET_IP)
                log("  确认：lip00=%s slaId=%s ms=%s mac=%s"
                    % (cfg.get("lip00", "?"), cfg.get("slaId", "?"),
                       cfg.get("ms", "?"), cfg.get("mac", "?")))
                if tcp_ok(MODULE_TARGET_IP, 502, 2.0):
                    log("  ✓ %s:502（Modbus）可达 —— 可以采集了"
                        % MODULE_TARGET_IP)
                self.finished = True
                return 0
            log("  第 %d 次未通，等 3 秒…" % (attempt + 1))
            time.sleep(3)

        log("[警告] %s 仍不可达。模块可能没接受新 IP，或落到了其它地址。"
            % MODULE_TARGET_IP)
        log("       建议跑：python tools/net_selfheal.py --sweep 192.168.0")
        self.finished = True
        return 7


def main(argv=None):
    ap = argparse.ArgumentParser(description="把模块 IP 改回 192.168.0.13")
    ap.add_argument("--iface", type=int, default=TARGET_IFACE_INDEX)
    ap.add_argument("--dry-run", action="store_true",
                    help="只读现状，不修改任何配置")
    args = ap.parse_args(argv)

    fixer = Fixer(args.iface, args.dry_run)
    wd = threading.Thread(target=fixer._watchdog, daemon=True)
    wd.start()
    try:
        return fixer.run()
    except Exception as exc:                            # noqa: BLE001
        log("[异常] %s: %s" % (type(exc).__name__, exc))
        fixer.cleanup("主流程异常")
        fixer.finished = True
        return 8


if __name__ == "__main__":
    sys.exit(main())
