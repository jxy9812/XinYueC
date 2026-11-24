# -*- coding: utf-8 -*-
"""本机**有线网口**信息与 IPv4 配置（DHCP / 静态）。

[为什么放在 Python 层而不是 hsb_bridge]
========================================
WiFi 走框架 `XNetworkWiFi`（有 C 会话对象，桥接成 hsb_bridge）；而「有线
网口切 DHCP/静态」在 Windows 是 `Set-NetIPInterface`/`New-NetIPAddress`、
在板端 Linux 是 NetworkManager 的 `nmcli con mod` —— 都是**操作系统命令**，
框架没有对应能力。这里用纯标准库 subprocess 封装，两端一份代码：
  · Windows：PowerShell（Get-NetAdapter / Get-NetIPConfiguration /
    Set-NetIPInterface / New-NetIPAddress / Set-DnsClientServerAddress）；
  · Linux（板端 RK3568）：`nmcli`（现场 eth0/eth1 都归 NetworkManager 管，
    配置会持久化到连接档案，重启不丢）。

[纪律]
======
· 只列**有线以太网**（排除回环/WiFi/虚拟网卡）；WiFi 仍由页面上的
  WiFi 卡片负责。
· 所有动作有超时（PowerShell 8s / nmcli 20s），错误原文回传；
  界面必须落到成功/失败/超时三选一的终态。
· 改网口可能瞬断连接（尤其客户端正好通过该网口连服务端）⇒ 页面按钮
  先弹确认，并把这一句写在卡片上。
· 没有管理员/root 权限：**只看不改**（`can_configure()` 返回原因）。
"""

import json
import os
import re
import shutil
import subprocess
import sys

#: 单次系统命令超时（秒）。
INFO_TIMEOUT_S = 8
APPLY_TIMEOUT_S = 25


class NetcfgError(RuntimeError):
    """网口操作失败（消息可直接显示给现场）。"""


# ============================================================================
#                          纯校验/转换（可单测）
# ============================================================================

def ipv4_ok(text):
    """严格 IPv4 点分十进制（四段 0~255，不允许前导零歧义）。"""
    s = str(text or "").strip()
    parts = s.split(".")
    if len(parts) != 4:
        return False
    for p in parts:
        if not p.isdigit() or len(p) > 3:
            return False
        n = int(p)
        if n < 0 or n > 255:
            return False
        if len(p) > 1 and p[0] == "0":
            return False
    return True


def mask_to_prefix(mask):
    """点分掩码 → 前缀长度（必须是连续 1）；非法返回 None。"""
    if not ipv4_ok(mask):
        return None
    bits = 0
    seen_zero = False
    for octet in [int(x) for x in str(mask).split(".")]:
        for bit in range(7, -1, -1):
            one = bool(octet & (1 << bit))
            if one and seen_zero:
                return None            # 1 中间夹 0：非连续掩码
            if one:
                bits += 1
            else:
                seen_zero = True
    if bits < 1 or bits > 32:
        return None
    return bits


def prefix_to_mask(prefix):
    """前缀长度 → 点分掩码（1..32）。"""
    p = int(prefix)
    if p < 1 or p > 32:
        raise ValueError("前缀长度必须在 1~32")
    value = (0xFFFFFFFF << (32 - p)) & 0xFFFFFFFF
    return ".".join(str((value >> s) & 0xFF) for s in (24, 16, 8, 0))


def normalize_dns(items):
    """DNS 列表/字符串 → 去重后的合法 IPv4 列表；非法项抛 ValueError。"""
    if items is None:
        return []
    if isinstance(items, str):
        raw = re.split(r"[\s,;，；]+", items.strip())
    else:
        raw = list(items)
    out = []
    for item in raw:
        s = str(item or "").strip()
        if not s:
            continue
        if not ipv4_ok(s):
            raise ValueError("DNS 不是合法 IPv4：%s" % s)
        if s not in out:
            out.append(s)
    return out


def validate_static(ip, mask, gateway="", dns=None):
    """静态配置整体校验：返回 (prefix, gateway, dns_list)。"""
    if not ipv4_ok(ip):
        raise ValueError("IP 地址不是合法 IPv4：%s" % ip)
    prefix = mask_to_prefix(mask)
    if prefix is None:
        raise ValueError("子网掩码非法（必须是连续 1 的点分掩码）：%s" % mask)
    gw = str(gateway or "").strip()
    if gw and not ipv4_ok(gw):
        raise ValueError("网关不是合法 IPv4：%s" % gw)
    return prefix, gw, normalize_dns(dns)


def speed_to_mbps(text):
    """'1 Gbps' / '100 Mbps' / '1000' → int Mbps；认不出返回 None。"""
    s = str(text or "").strip().lower()
    m = re.search(r"([0-9]+(?:\.[0-9]+)?)\s*(gbps|mbps|kbps|g|m|k)?", s)
    if not m:
        return None
    try:
        val = float(m.group(1))
    except ValueError:
        return None
    unit = (m.group(2) or "").lower()
    if unit.startswith("g"):
        val *= 1000.0
    elif unit.startswith("k"):
        val /= 1000.0
    return int(round(val)) if val > 0 else None


def _terse_fields(line):
    """nmcli -t 的转义拆分：`\\:` 是值里的冒号，`\\\\` 是反斜杠。"""
    fields, buf, esc = [], [], False
    for ch in str(line):
        if esc:
            buf.append(ch)
            esc = False
        elif ch == "\\":
            esc = True
        elif ch == ":":
            fields.append("".join(buf))
            buf = []
        else:
            buf.append(ch)
    fields.append("".join(buf))
    return fields


def parse_windows_ports(text):
    """PowerShell JSON → 端口列表（纯解析，测试用）。"""
    s = str(text or "").strip()
    if not s or s == "null":
        return []
    try:
        doc = json.loads(s)
    except ValueError as exc:
        raise NetcfgError("解析网卡信息失败：%s" % exc)
    if isinstance(doc, dict):
        doc = [doc]
    if not isinstance(doc, list):
        raise NetcfgError("解析网卡信息失败：不是数组")
    ports = []
    for row in doc:
        if not isinstance(row, dict):
            continue
        name = str(row.get("name") or "").strip()
        if not name:
            continue
        prefix = int(row.get("prefix") or 0)
        try:
            mask = prefix_to_mask(prefix) if prefix else ""
        except ValueError:
            mask = ""
        dns = row.get("dns")
        if isinstance(dns, str):
            dns = [dns]
        dhcp = str(row.get("dhcp") or "").lower()
        if dhcp in ("enabled", "true", "1"):
            mode = "dhcp"
        elif dhcp in ("disabled", "false", "0"):
            mode = "static"
        else:
            mode = "unknown"
        ports.append({
            "id": str(row.get("id") or name),
            "name": name,
            "desc": str(row.get("desc") or ""),
            "mac": str(row.get("mac") or ""),
            "link": str(row.get("status") or "").lower() == "up",
            "status": str(row.get("status") or ""),
            "speed_mbps": speed_to_mbps(row.get("speed")),
            "ipv4": str(row.get("ip") or ""),
            "prefix": prefix,
            "mask": mask,
            "gateway": str(row.get("gateway") or ""),
            "dns": [str(x) for x in (dns or []) if str(x).strip()],
            "mode": mode,
        })
    return ports


def parse_nmcli_dev_status(text):
    """`nmcli -t -f DEVICE,TYPE,STATE,CONNECTION dev status` → 有线设备行。"""
    out = []
    for line in str(text or "").splitlines():
        fields = _terse_fields(line)
        if len(fields) < 4:
            continue
        dev, typ, state, con = fields[0], fields[1], fields[2], fields[3]
        if typ != "ethernet" or not dev or dev == "lo":
            continue
        out.append({"device": dev, "state": state, "connection": con})
    return out


def _nmcli_values(text):
    """nmcli -g 输出行 → 去空、反转义（`\\:` → `:`、`\\\\` → `\\`）。"""
    out = []
    for ln in str(text or "").splitlines():
        s = ln.strip()
        if not s:
            continue
        s = s.replace("\\:", ":").replace("\\\\", "\\")
        out.append(s)
    return out


# ============================================================================
#                          权限
# ============================================================================

def can_configure():
    """(bool, 原因)。无权限时页面只读（信息照显示）。"""
    if sys.platform.startswith("win"):
        try:
            import ctypes
            if ctypes.windll.shell32.IsUserAnAdmin():
                return True, ""
        except Exception:                              # noqa: BLE001
            pass
        return False, "当前未以管理员身份运行：只能查看，不能修改网口"
    try:
        if os.geteuid() == 0:
            return True, ""
    except AttributeError:                             # pragma: no cover
        pass
    return False, "当前不是 root：只能查看，不能修改网口"


# ============================================================================
#                          Windows
# ============================================================================

_WIN_INFO_PS = r"""
$ErrorActionPreference = "SilentlyContinue"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$rows = @()
Get-NetAdapter -Physical | Where-Object { $_.PhysicalMediaType -notmatch "802.11" } | ForEach-Object {
  $idx = $_.InterfaceIndex
  $ipa = Get-NetIPAddress -InterfaceIndex $idx -AddressFamily IPv4 | Select-Object -First 1
  $cfg = Get-NetIPConfiguration -InterfaceIndex $idx
  $dns = @((Get-DnsClientServerAddress -InterfaceIndex $idx -AddressFamily IPv4).ServerAddresses)
  $dhcp = (Get-NetIPInterface -InterfaceIndex $idx -AddressFamily IPv4).Dhcp
  $rows += [pscustomobject]@{
    id = "$idx"
    name = $_.Name
    desc = $_.InterfaceDescription
    mac = $_.MacAddress
    status = "$($_.Status)"
    speed = "$($_.LinkSpeed)"
    ip = $(if ($ipa) { $ipa.IPAddress } else { "" })
    prefix = $(if ($ipa) { [int]$ipa.PrefixLength } else { 0 })
    gateway = $(if ($cfg -and $cfg.IPv4DefaultGateway) { $cfg.IPv4DefaultGateway.NextHop } else { "" })
    dns = $dns
    dhcp = "$dhcp"
  }
}
$rows | ConvertTo-Json -Depth 4 -Compress
"""


def _run(cmd, timeout, what):
    env = dict(os.environ)
    # nmcli 的输出（connected/auto/manual）在中文 locale 下会被翻译 ⇒
    # 统一 C locale，保证解析稳定（PowerShell 不受 LANG 影响）。
    env["LC_ALL"] = "C"
    env["LANG"] = "C"
    try:
        proc = subprocess.run(
            cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            timeout=timeout, env=env)
    except subprocess.TimeoutExpired:
        raise NetcfgError("%s超时（%ds）" % (what, timeout))
    except OSError as exc:
        raise NetcfgError("%s失败：%s" % (what, exc))
    out = (proc.stdout or b"").decode("utf-8", "replace")
    err = (proc.stderr or b"").decode("utf-8", "replace").strip()
    if proc.returncode != 0:
        raise NetcfgError("%s失败：%s"
                          % (what, err or ("退出码 %d" % proc.returncode)))
    return out, err


def _win_ps(script, timeout, what):
    return _run(["powershell", "-NoProfile", "-NonInteractive",
                 "-ExecutionPolicy", "Bypass", "-Command", script],
                timeout, what)


def _list_windows():
    out, _ = _win_ps(_WIN_INFO_PS, INFO_TIMEOUT_S, "读取网卡信息")
    return parse_windows_ports(out)


def _apply_windows(port_id, mode, ip="", prefix=0, gateway="", dns=None):
    ok, why = can_configure()
    if not ok:
        raise NetcfgError(why)
    idx = str(port_id)
    if not idx.isdigit():
        raise NetcfgError("网卡标识非法：%s" % idx)
    if mode == "dhcp":
        script = (
            "$ErrorActionPreference='Stop';"
            "Set-NetIPInterface -InterfaceIndex %s -Dhcp Enabled;"
            "Set-DnsClientServerAddress -InterfaceIndex %s "
            "-ResetServerAddresses;"
            "Write-Output OK" % (idx, idx))
    else:
        gw = (' -DefaultGateway "%s"' % gateway) if gateway else ""
        dns_list = "@(" + ",".join('"%s"' % d for d in (dns or [])) + ")"
        script = (
            "$ErrorActionPreference='Stop';"
            "Get-NetIPAddress -InterfaceIndex %s -AddressFamily IPv4 "
            "-ErrorAction SilentlyContinue | Remove-NetIPAddress "
            "-Confirm:$false -ErrorAction SilentlyContinue;"
            "Get-NetRoute -InterfaceIndex %s -AddressFamily IPv4 "
            "-DestinationPrefix '0.0.0.0/0' -ErrorAction SilentlyContinue | "
            "Remove-NetRoute -Confirm:$false -ErrorAction SilentlyContinue;"
            "New-NetIPAddress -InterfaceIndex %s -IPAddress \"%s\" "
            "-PrefixLength %d%s;"
            "Set-DnsClientServerAddress -InterfaceIndex %s "
            "-ServerAddresses %s -ErrorAction SilentlyContinue;"
            "Write-Output OK"
            % (idx, idx, idx, ip, prefix, gw, idx, dns_list))
    try:
        out, _ = _win_ps(script, APPLY_TIMEOUT_S, "应用网口设置")
    except NetcfgError as exc:
        msg = str(exc)
        if "Access is denied" in msg or "拒绝访问" in msg:
            raise NetcfgError("应用失败：需要管理员权限（请以管理员身份"
                              "运行客户端）")
        raise
    if "OK" not in out:
        raise NetcfgError("应用失败：PowerShell 未返回成功标记")
    return "已应用（Windows 网口设置已持久化）"


# ============================================================================
#                          Linux / NetworkManager
# ============================================================================

def _linux_has_nmcli():
    return shutil.which("nmcli") is not None


def _linux(dev, getter):
    out, _ = _run(["nmcli", "-g", getter, "dev", "show", dev],
                  INFO_TIMEOUT_S, "读取 %s 的 %s" % (dev, getter))
    return _nmcli_values(out)


def _linux_con(con, getter):
    out, _ = _run(["nmcli", "-g", getter, "con", "show", con],
                  INFO_TIMEOUT_S, "读取连接 %s 的 %s" % (con, getter))
    return _nmcli_values(out)


def _list_linux():
    if not _linux_has_nmcli():
        return _list_linux_ip_fallback()
    out, _ = _run(["nmcli", "-t", "-f", "DEVICE,TYPE,STATE,CONNECTION",
                   "dev", "status"], INFO_TIMEOUT_S, "读取网口状态")
    ports = []
    for row in parse_nmcli_dev_status(out):
        dev = row["device"]
        con = row["connection"]
        state = row["state"].lower()
        mac = (_linux(dev, "GENERAL.HWADDR") or [""])[0]
        addr = (_linux(dev, "IP4.ADDRESS") or [""])[0]
        if not addr and con:
            addr = (_linux_con(con, "ipv4.addresses") or [""])[0]
        ipv4, prefix = "", 0
        if "/" in addr:
            ip, _, pfx = addr.partition("/")
            ipv4 = ip.strip()
            try:
                prefix = int(pfx.strip())
            except ValueError:
                prefix = 0
        elif addr:
            ipv4 = addr.strip()
        gateway = ((_linux(dev, "IP4.GATEWAY") or [""])[0]
                   or (con and (_linux_con(con, "ipv4.gateway") or [""])[0])
                   or "")
        dns = _linux(dev, "IP4.DNS")
        if not dns and con:
            for item in _linux_con(con, "ipv4.dns"):
                dns.extend(re.split(r"[\s,;]+", item.strip()))
        dns = [d for d in dns if d]
        method = ((_linux_con(con, "ipv4.method") or [""])[0]
                  if con else "")
        if method == "auto":
            mode = "dhcp"
        elif method == "manual":
            mode = "static"
        else:
            mode = "unknown"
        speed = None
        try:
            with open("/sys/class/net/%s/speed" % dev, "r",
                      encoding="ascii", errors="ignore") as fh:
                raw = fh.read().strip()
            if raw and int(raw) > 0:
                speed = int(raw)
        except (OSError, ValueError):
            speed = None
        try:
            mask = prefix_to_mask(prefix) if prefix else ""
        except ValueError:
            mask = ""
        ports.append({
            "id": dev,
            "name": dev,
            "desc": con or "以太网",
            "mac": mac,
            "link": state.startswith("connected"),
            "status": state,
            "speed_mbps": speed,
            "ipv4": ipv4,
            "prefix": prefix,
            "mask": mask,
            "gateway": gateway,
            "dns": dns,
            "mode": mode,
        })
    return ports


def _list_linux_ip_fallback():
    """没有 nmcli 时只读展示 `ip -j addr`（不能改）。"""
    try:
        out, _ = _run(["ip", "-j", "addr", "show"], INFO_TIMEOUT_S,
                      "读取网口信息")
        doc = json.loads(out)
    except (NetcfgError, ValueError):
        return []
    ports = []
    for item in doc if isinstance(doc, list) else []:
        if str(item.get("link_type") or "") != "ether":
            continue
        name = str(item.get("ifname") or "")
        if not name or name == "lo":
            continue
        ipv4, prefix = "", 0
        for a in item.get("addr_info") or []:
            if a.get("family") == "inet":
                ipv4 = str(a.get("local") or "")
                prefix = int(a.get("prefixlen") or 0)
                break
        ports.append({
            "id": name, "name": name, "desc": "以太网（nmcli 不可用）",
            "mac": str(item.get("address") or ""),
            "link": str(item.get("operstate") or "") == "UP",
            "status": str(item.get("operstate") or ""),
            "speed_mbps": None, "ipv4": ipv4, "prefix": prefix,
            "mask": prefix_to_mask(prefix) if prefix else "",
            "gateway": "", "dns": [], "mode": "unknown",
        })
    return ports


def _apply_linux(port_id, mode, ip="", prefix=0, gateway="", dns=None):
    ok, why = can_configure()
    if not ok:
        raise NetcfgError(why)
    dev = str(port_id)
    if not re.match(r"^[A-Za-z0-9_.:-]+$", dev):
        raise NetcfgError("网口名非法：%s" % dev)
    con = ""
    try:
        con = (_linux(dev, "GENERAL.CONNECTION") or [""])[0]
    except NetcfgError:
        con = ""
    if not con:
        con = "hsf-%s" % dev
        _run(["nmcli", "con", "add", "type", "ethernet", "ifname", dev,
              "con-name", con], APPLY_TIMEOUT_S, "创建连接档案")
    if mode == "dhcp":
        _run(["nmcli", "con", "mod", con, "ipv4.method", "auto",
              "ipv4.addresses", "", "ipv4.gateway", "", "ipv4.dns", "",
              "ipv4.ignore-auto-dns", "no"],
             APPLY_TIMEOUT_S, "设置 DHCP")
    else:
        args = ["nmcli", "con", "mod", con, "ipv4.method", "manual",
                "ipv4.addresses", "%s/%d" % (ip, prefix)]
        args += ["ipv4.gateway", gateway or ""]
        args += ["ipv4.dns", " ".join(dns or [])]
        _run(args, APPLY_TIMEOUT_S, "设置静态 IP")
    _run(["nmcli", "con", "up", con], APPLY_TIMEOUT_S, "启用连接")
    return "已应用（NetworkManager 连接「%s」已保存并生效）" % con


# ============================================================================
#                          对外入口
# ============================================================================

def list_ports():
    """枚举有线网口（失败抛 NetcfgError，消息可直接显示）。"""
    if sys.platform.startswith("win"):
        return _list_windows()
    return _list_linux()


def apply_port(port_id, mode, ip="", mask="", gateway="", dns=None):
    """应用网口配置；成功返回文案，失败抛 NetcfgError/ValueError。

    mode = "dhcp" 时忽略静态字段；mode = "static" 时校验 ip/mask/gateway/dns。
    """
    if mode not in ("dhcp", "static"):
        raise ValueError("未知配置方式：%s" % mode)
    if mode == "static":
        prefix, gw, dns_list = validate_static(ip, mask, gateway, dns)
    else:
        prefix, gw, dns_list = 0, "", []
    if sys.platform.startswith("win"):
        return _apply_windows(port_id, mode, ip, prefix, gw, dns_list)
    return _apply_linux(port_id, mode, ip, prefix, gw, dns_list)
