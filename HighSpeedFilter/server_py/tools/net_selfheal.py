# -*- coding: utf-8 -*-
"""网络自愈脚本：安全地给有线网卡加副地址去找设备，失败自动回滚。

    python tools/net_selfheal.py --scan-only      # 只扫描，不动配置
    python tools/net_selfheal.py                  # 加副地址 + 扫描 + 自动回滚
    python tools/net_selfheal.py --keep           # 找到就保留副地址

[为什么必须"带回滚"] 给以太网加一个和 WLAN 同网段（192.168.1.x）的副地址，
Windows 可能把默认路由切到以太网 —— 那条链路没有网关，**电脑就断网了**。
你人不在场，断了网就再也连不回来。

所以本脚本的每一步都满足：
  1. **动网卡前先存快照**（现有 IP/掩码/网关的完整配置）
  2. **动完立刻验外网**（DNS + TCP 到公网），不通就**马上回滚**
  3. **全程有 watchdog 线程**：主流程卡死/异常/超时也会回滚
  4. **回滚失败时还原成 DHCP**（最后一个安全兜底）
  5. 所有动作写日志到文件，人回来能看到发生了什么

[不会做的事] 不改 WLAN、不改默认网关、不 disable 任何网卡。
副地址用完即删（除非 --keep）。
"""

import argparse
import ctypes
import json
import os
import socket
import subprocess
import sys
import threading
import time

# ============================================================================
#                          配置常量
# ============================================================================

#: 脚本要操作的有线网卡（按 **接口索引**，避开中文名的编码问题）。
#: 11 = "以太网"（当前 192.168.0.243，就是接设备的那个）。
TARGET_IFACE_INDEX = 11

#: 要临时加上的副地址（用于够到 192.168.1.20 等跳段设备）。
#: 选 .50 是为了避开已知设备（.1 网关/.2/.3/.4/.10/.15/.20/.36/.37…）。
TEMP_ADDRESS = "192.168.1.50"
TEMP_MASK = "255.255.255.0"

#: 要探测的目标设备（找 192.168.1.20 上的 Modbus 模块）。
TARGETS = [
    ("192.168.1.20", 502, "数字量模块2（改从站后的可能地址）"),
    ("192.168.1.20", 80, "数字量模块2（Web 口）"),
]

#: 外网连通性检查目标（验证"电脑还能上网"）。
#: 用多个目标 + 只用 TCP 连（不依赖 DNS 解析，DNS 本身也可能被算坏）。
WAN_CHECKS = [
    ("223.5.5.5", 53),        # 阿里 DNS（IP 直连，不走域名解析）
    ("114.114.114.114", 53),  # 114 DNS
    ("192.168.1.1", 80),      # 本地网关（WLAN 侧）
]

#: 日志文件（与脚本同目录，便于人回来查看）。
LOG_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                        "net_selfheal.log")

#: 全程 watchdog 上限（秒）。超时未结束就强制回滚。
WATCHDOG_SECONDS = 180


# ============================================================================
#                              日志
# ============================================================================

_log_lock = threading.Lock()


def log(message):
    stamp = time.strftime("%Y-%m-%d %H:%M:%S")
    line = "[%s] %s" % (stamp, message)
    with _log_lock:
        print(line, flush=True)
        try:
            with open(LOG_PATH, "a", encoding="utf-8") as fh:
                fh.write(line + "\n")
        except OSError:
            pass


# ============================================================================
#                        网络配置读写（netsh）
# ============================================================================

def _run(args, timeout=25):
    """执行命令，返回 (returncode, stdout, stderr)。编码容错（中文 GBK）。"""
    try:
        proc = subprocess.run(args, capture_output=True, timeout=timeout,
                              creationflags=getattr(subprocess,
                                                    "CREATE_NO_WINDOW", 0))
        out = proc.stdout.decode("gbk", errors="replace")
        err = proc.stderr.decode("gbk", errors="replace")
        return proc.returncode, out, err
    except subprocess.TimeoutExpired:
        return -1, "", "命令超时：%s" % " ".join(args)
    except Exception as exc:                            # noqa: BLE001
        return -1, "", "%s: %s" % (type(exc).__name__, exc)


def is_admin():
    """改网卡配置必须管理员权限。"""
    try:
        return bool(ctypes.windll.shell32.IsUserAnAdmin())
    except Exception:                                   # noqa: BLE001
        return False


def snapshot_iface(iface_index):
    """读取某网卡的当前 IP 配置（回滚依据）。

    :returns: {"addresses": [(ip, mask)], "gateways": [ip]}；读取失败返回 None。
    """
    code, out, err = _run(["netsh", "interface", "ipv4", "show", "addresses",
                           str(iface_index)])
    if code != 0:
        log("  读取网卡 %d 配置失败：%s" % (iface_index, err.strip()))
        return None
    addresses, gateways = [], []
    for raw in out.splitlines():
        line = raw.strip()
        # 形如 "IP 地址:   192.168.0.243" / "子网前缀: 192.168.0.0/24"
        if line.startswith("IP 地址") or line.startswith("IP Address"):
            parts = line.split(":", 1)
            if len(parts) == 2:
                ip = parts[1].strip()
                if ip.count(".") == 3:
                    addresses.append([ip, None])
        elif line.startswith("子网前缀") or line.startswith("Subnet Prefix"):
            parts = line.split(":", 1)
            if len(parts) == 2 and addresses and addresses[-1][1] is None:
                prefix = parts[1].strip()
                mask = _prefix_to_mask(prefix)
                addresses[-1][1] = mask
        elif line.startswith("默认网关") or line.startswith("Default Gateway"):
            parts = line.split(":", 1)
            if len(parts) == 2:
                gw = parts[1].strip()
                if gw.count(".") == 3:
                    gateways.append(gw)
    return {"addresses": [(a, m) for a, m in addresses if a],
            "gateways": gateways}


def _prefix_to_mask(prefix_text):
    """'192.168.0.0/24 (掩码 255.255.255.0)' -> '255.255.255.0'。

    [实测格式] Windows 中文版 netsh 输出是
    `子网前缀:  192.168.0.0/24 (掩码 255.255.255.0)` —— 两种表示都有，
    优先取括号里的点分掩码（最精确），没有再按前缀位数换算。
    """
    text = prefix_text.strip()
    # 优先：括号里的点分掩码
    if "(" in text and ")" in text:
        inner = text[text.index("(") + 1:text.rindex(")")]
        for token in inner.replace("掩码", " ").split():
            if token.count(".") == 3:
                return token
    if "/" in text:
        suffix = text.split("/", 1)[1].strip()
        if suffix.count(".") == 3:
            return suffix
        try:
            bits = int(suffix)
        except ValueError:
            return "255.255.255.0"
        value = (0xFFFFFFFF << (32 - bits)) & 0xFFFFFFFF if bits else 0
        return "%d.%d.%d.%d" % ((value >> 24) & 0xFF, (value >> 16) & 0xFF,
                                (value >> 8) & 0xFF, value & 0xFF)
    return "255.255.255.0"


def add_secondary_address(iface_index, ip, mask):
    """给网卡追加一个副地址（不删原有地址）。"""
    log("  添加副地址 %s/%s 到网卡 %d" % (ip, mask, iface_index))
    code, out, err = _run(["netsh", "interface", "ipv4", "add", "address",
                           str(iface_index), ip, mask])
    ok = code == 0
    log("    → %s %s" % ("成功" if ok else "失败", (err or out).strip()[:200]))
    return ok


def remove_address(iface_index, ip):
    """删除某地址。"""
    log("  删除地址 %s（网卡 %d）" % (ip, iface_index))
    code, out, err = _run(["netsh", "interface", "ipv4", "delete", "address",
                           str(iface_index), ip])
    ok = code == 0
    log("    → %s %s" % ("成功" if ok else "失败", (err or out).strip()[:200]))
    return ok


def restore_snapshot(iface_index, snap):
    """按快照还原网卡配置（先清后建，确保没有残留）。

    :returns: 还原成功（且原地址都在）返回 True。
    """
    if not snap:
        log("  没有可用快照，无法精确还原")
        return False
    log("  开始还原网卡 %d：%s" % (iface_index, snap["addresses"]))
    # 1) 删掉临时副地址（幂等）
    remove_address(iface_index, TEMP_ADDRESS)
    # 2) 确认原地址还在；缺了就补回
    now = snapshot_iface(iface_index) or {"addresses": [], "gateways": []}
    present = {ip for ip, _m in now["addresses"]}
    all_ok = True
    for ip, mask in snap["addresses"]:
        if ip in present:
            continue
        log("  原地址 %s 丢失，补回（掩码 %s）" % (ip, mask))
        if not add_secondary_address(iface_index, ip, mask or "255.255.255.0"):
            all_ok = False
    # 3) 复核
    final = snapshot_iface(iface_index) or {"addresses": [], "gateways": []}
    final_ips = {ip for ip, _m in final["addresses"]}
    for ip, _mask in snap["addresses"]:
        if ip not in final_ips:
            log("  [警告] 还原后仍缺地址 %s" % ip)
            all_ok = False
    log("  还原完成：%s（当前 %s）"
        % ("成功" if all_ok else "有缺失", sorted(final_ips)))
    return all_ok


def fallback_to_dhcp(iface_index):
    """最后兜底：把网卡切回 DHCP（让系统自己重新拿地址）。

    [为什么这是兜底] 精确还原失败时，DHCP 至少能让网卡拿到一个可用地址，
    比停在错误配置上导致彻底断网要好。现场这台是有线直连 + 静态 IP 的
    环境，切 DHCP 拿不到地址也不会比现在更糟。
    """
    log("  [兜底] 把网卡 %d 切回 DHCP" % iface_index)
    code, out, err = _run(["netsh", "interface", "ipv4", "set", "address",
                           str(iface_index), "dhcp"])
    log("    → %s %s" % ("成功" if code == 0 else "失败", (err or out).strip()[:200]))
    return code == 0


# ============================================================================
#                          连通性检查
# ============================================================================

def tcp_ok(ip, port, timeout=2.0):
    try:
        sock = socket.create_connection((ip, port), timeout)
        sock.close()
        return True
    except Exception:                                   # noqa: BLE001
        return False


def wan_alive():
    """外网是否还通（任一目标通即算通）。"""
    for ip, port in WAN_CHECKS:
        if tcp_ok(ip, port, 1.5):
            return True, "%s:%d" % (ip, port)
    return False, "全部不可达"


def devices_alive():
    """当前应可达的设备（用于验证"内网没被我搞坏"）。"""
    checks = [("192.168.0.10", 22), ("192.168.0.12", 502)]
    return [("%s:%d" % (ip, p)) for ip, p in checks if tcp_ok(ip, p, 1.5)]


# ============================================================================
#                         主流程（带 watchdog）
# ============================================================================

class SelfHeal(object):
    """一次自愈会话。所有破坏性动作都可回滚。"""

    def __init__(self, iface_index, scan_only=False, keep=False):
        self.iface_index = iface_index
        self.scan_only = scan_only
        self.keep = keep
        self.snapshot = None
        self.address_added = False
        self.finished = False          # watchdog 用它判断是否已收尾
        self._lock = threading.Lock()

    # ---- watchdog ----
    def _watchdog(self):
        """超时未完成 → 强制回滚。防"主流程卡死把网络留在坏状态"。"""
        deadline = time.time() + WATCHDOG_SECONDS
        while time.time() < deadline:
            time.sleep(1.0)
            with self._lock:
                if self.finished:
                    return
        with self._lock:
            if self.finished:
                return
        log("[watchdog] 超时 %ds 未完成，强制回滚" % WATCHDOG_SECONDS)
        self.rollback("watchdog 超时")

    # ---- 回滚 ----
    def rollback(self, reason):
        """回滚到快照状态。幂等、可多次调用。"""
        with self._lock:
            need = self.address_added
            self.address_added = False
        log("=== 回滚（原因：%s）===" % reason)
        alive, detail = wan_alive()
        # 无论是否需要删地址，都检查一次原地址是否完好
        if need or not alive:
            if restore_snapshot(self.iface_index, self.snapshot):
                pass
            else:
                fallback_to_dhcp(self.iface_index)
        # 回滚后复核
        time.sleep(2)
        alive2, detail2 = wan_alive()
        log("回滚后外网：%s（%s）" % ("通" if alive2 else "不通", detail2))
        if not alive2:
            log("[严重] 回滚后仍不通！已尝试: 还原快照 + DHCP 兜底")
            log("       请尽快人工检查网卡 %d 的 IP 配置" % self.iface_index)
            log("       快照内容：%s" % json.dumps(self.snapshot,
                                               ensure_ascii=False))
        log("=== 回滚结束 ===")

    # ---- 主流程 ----
    def run(self):
        log("=" * 66)
        log("网络自愈开始（网卡索引 %d，%s）"
            % (self.iface_index, "仅扫描" if self.scan_only else "加副地址后扫描"))

        # 0) 权限
        if not is_admin() and not self.scan_only:
            log("[失败] 需要管理员权限（改网卡配置）。请用管理员终端运行。")
            return 2

        # 1) 基线：先确认现在是好的（否则不该动手）
        alive, detail = wan_alive()
        log("基线外网：%s（%s）" % ("通" if alive else "不通", detail))
        base_devices = devices_alive()
        log("基线内网设备：%s" % (base_devices or "无"))

        # 2) 快照（动网卡之前！）
        self.snapshot = snapshot_iface(self.iface_index)
        if self.snapshot is None:
            log("[失败] 无法读取网卡快照，拒绝修改配置（安全第一）")
            return 2
        log("已保存快照：%s" % json.dumps(self.snapshot, ensure_ascii=False))

        # 3) 目标已在原网段？那就没必要动网卡
        already = any(tcp_ok(ip, port) for ip, port, _n in TARGETS)
        if already:
            log("目标已在当前网段可达，无需修改网卡配置")
            self._report_targets()
            self.finished = True
            return 0

        if self.scan_only:
            log("仅扫描模式：不动网卡配置")
            self._report_targets()
            self.finished = True
            return 1

        # 4) 加副地址
        with self._lock:
            self.address_added = True
        if not add_secondary_address(self.iface_index, TEMP_ADDRESS, TEMP_MASK):
            log("添加副地址失败，直接回滚")
            self.rollback("添加副地址失败")
            self.finished = True
            return 3

        # 5) 立刻验外网 —— 这一步是安全的全部意义
        time.sleep(3)                 # 等路由表稳定
        alive, detail = wan_alive()
        log("加地址后外网：%s（%s）" % ("通" if alive else "不通", detail))
        if not alive:
            log("[保护] 外网已被影响，立即回滚")
            self.rollback("加副地址导致外网不通")
            self.finished = True
            return 4

        # 6) 验内网设备没受影响
        dev2 = devices_alive()
        log("加地址后内网设备：%s" % (dev2 or "无"))
        lost = set(base_devices) - set(dev2)
        if lost:
            log("[警告] 以下设备变为不可达：%s（继续，但记入日志）" % sorted(lost))

        # 7) 扫描目标
        found = self._report_targets()

        # 8) 收尾
        if self.keep and found:
            log("--keep：保留副地址 %s（重启后失效）" % TEMP_ADDRESS)
        else:
            self.rollback("扫描完成，清理临时地址")
        self.finished = True
        return 0 if found else 1

    def _report_targets(self):
        found = False
        log("--- 探测目标 ---")
        for ip, port, name in TARGETS:
            ok = tcp_ok(ip, port, 2.5)
            log("  %s %-16s:%-5d %s" % ("✓" if ok else "✗", ip, port, name))
            if ok:
                found = True
        if found:
            log("[结果] 找到设备！192.168.1.20 可达")
            # 若 80 口通，读一下 config.js 确认身份
            self._identify()
        else:
            log("[结果] 192.168.1.20 仍不可达")
            log("       可能它在别的地址。建议改 TARGETS 或扩大扫描：")
            log("       python tools/net_selfheal.py --sweep 192.168.1")
        return found

    def _identify(self):
        """若 Web 口通，读 config.js 确认是不是那个模块。"""
        try:
            import urllib.request
            body = urllib.request.urlopen(
                "http://192.168.1.20/config.js", timeout=4
            ).read().decode("utf-8", "replace")
            import re
            mod = re.search(r'"module"\s*:\s*"([^"]+)"', body)
            mac = re.search(r'"mac"\s*:\s*"([^"]+)"', body)
            ms = re.search(r'"ms"\s*:\s*"?(\d+)"?', body)
            log("  身份确认：module=%s mac=%s ms=%s"
                % (mod.group(1) if mod else "?",
                   mac.group(1) if mac else "?",
                   ms.group(1) if ms else "?"))
            if ms and ms.group(1) == "0":
                log("  ✓ ms=0 从站模式 —— 可以直接用 Modbus 采集")
        except Exception as exc:                        # noqa: BLE001
            log("  身份读取失败（设备 HTTP 不规范属正常）：%s" % type(exc).__name__)


def sweep(subnet):
    """扫描一个网段的 502/80，找候选设备（不改任何配置）。"""
    import concurrent.futures
    log("扫描 %s.1~254 的 502/80 …" % subnet)

    def probe(i):
        ip = "%s.%d" % (subnet, i)
        for port in (502, 80):
            if tcp_ok(ip, port, 0.35):
                return (ip, port)
        return None

    hits = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=100) as ex:
        for r in ex.map(probe, range(1, 255)):
            if r:
                hits.append(r)
    for ip, port in sorted(hits):
        log("  %-16s :%d" % (ip, port))
    if not hits:
        log("  该网段无 502/80 主机（可能本机没有到该网段的路由）")
    return hits


# ============================================================================
#                              入口
# ============================================================================

def main(argv=None):
    ap = argparse.ArgumentParser(description="网络自愈（带回滚保险）")
    ap.add_argument("--iface", type=int, default=TARGET_IFACE_INDEX,
                    help="网卡接口索引（缺省 %d = 以太网）" % TARGET_IFACE_INDEX)
    ap.add_argument("--scan-only", action="store_true",
                    help="只扫描，不改网卡配置")
    ap.add_argument("--keep", action="store_true",
                    help="找到设备就保留副地址（缺省用完即删）")
    ap.add_argument("--sweep", default=None, metavar="SUBNET",
                    help="只扫某网段，如 192.168.1（不改配置）")
    args = ap.parse_args(argv)

    if args.sweep:
        sweep(args.sweep)
        return 0

    heal = SelfHeal(args.iface, args.scan_only, args.keep)
    # watchdog 后台跑，主流程异常也会触发回滚
    wd = threading.Thread(target=heal._watchdog, daemon=True)
    wd.start()
    try:
        return heal.run()
    except Exception as exc:                            # noqa: BLE001
        log("[异常] %s: %s" % (type(exc).__name__, exc))
        heal.rollback("主流程异常")
        heal.finished = True
        return 5
    finally:
        log("日志：%s" % LOG_PATH)


if __name__ == "__main__":
    sys.exit(main())
