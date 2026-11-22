# -*- coding: utf-8 -*-
"""相机按 MAC 连接：MAC → 当前 IP 解析（2026-11-20 用户口径）。

[背景] 现场相机是 DHCP 动态 IP，重启/换网段后 IP 会变 ⇒ 按 IP 配置会
"莫名连不上"（其实相机活着，换了地址）。MAC 是不变的 ⇒ 配 MAC，服务端
每次连接前把 MAC 解析成**当时的 IP**。

[解析原理（纯标准库，Windows/Linux 两栖）]
  1. 读本机 ARP 表（Windows `arp -a` / Linux `/proc/net/arp`），
     MAC → IP 直接命中即返回。
  2. 未命中 ⇒ 相机的 IP 是新租的、本机还没跟它说过话、ARP 表里自然没有
     ⇒ 对候选网段（/24）做一轮 **TCP 探测**（连 554/80，连上或被拒都会
     在内核 ARP 表里留下条目），然后**重读 ARP** 再查。
  3. 还查不到 ⇒ 返回 None（采集线程按「MAC 未解析到」报错并按重连配置
     重试）。

[网段候选从哪来] 种子 IP 的 /24：优先**上次解析到的 IP**（相机多半只在小
范围里挪），其次配置里的 ip 字段（此时当"网段提示"用），最后本机默认出口
IP。三选一取不到就不扫（只查表）。

[为什么不做 ARP 广播包] 造 ARP 包要原始套接字 + 管理员权限，Windows 上
还得 Npcap ⇒ 部署负担太重。TCP 探测借内核之手填 ARP 表，普通权限即可。
"""

import os
import re
import socket
import subprocess
import sys
import threading

#: MAC 归一：`AA-BB-CC-DD-EE-FF` / `aabbccddeeff` / 带点变体 → 小写冒号。
_MAC_RE = re.compile(r"^[0-9a-fA-F]{1,2}[:-]?[0-9a-fA-F]{1,2}[:-]?"
                     r"[0-9a-fA-F]{1,2}[:-]?[0-9a-fA-F]{1,2}[:-]?"
                     r"[0-9a-fA-F]{1,2}[:-]?[0-9a-fA-F]{1,2}$")


def normalize_mac(text):
    """任意常见写法 → `aa:bb:cc:dd:ee:ff`；不合法返回 None。"""
    t = str(text or "").strip().lower().replace("：", ":")
    if not t:
        return None
    hexes = re.findall(r"[0-9a-f]{1,2}", t.replace(":", ""))
    if len(hexes) != 6:
        return None
    if sum(len(h) for h in hexes) != 12:
        return None
    return ":".join(hexes)


def _arp_table_windows():
    """`arp -a` → {mac: ip}（动态/静态条目都收；组播排除）。"""
    # [编码 2026-11-20 实锅] 中文 Windows 的 `arp -a` 输出是 **GBK**；
    # `text=True` 按 locale 默认解码 ⇒ reader 线程抛 UnicodeDecodeError
    # ⇒ stdout 为空 ⇒ **MAC 永远解析不到**（本机实测返回 None；板端走
    # /proc/net/arp 不受影响，所以只在 Windows 露头）。
    # ⇒ 拿 bytes 自己按本地代码页解（errors 兜底，解析靠正则不受乱码影响）。
    out = subprocess.run(["arp", "-a"], capture_output=True, timeout=5.0)
    raw = out.stdout or b""
    text = None
    for enc in ("mbcs", "gbk", "utf-8"):
        try:
            text = raw.decode(enc)
            break
        except (LookupError, UnicodeDecodeError):
            continue
    if text is None:
        text = raw.decode("latin-1", "replace")
    table = {}
    for line in text.splitlines():
        m = re.search(r"(\d+\.\d+\.\d+\.\d+)\s+((?:[0-9a-fA-F]{1,2}[-]){5}"
                      r"[0-9a-fA-F]{1,2})", line)
        if not m:
            continue
        ip, mac = m.group(1), m.group(2).replace("-", ":").lower()
        if mac == "ff:ff:ff:ff:ff:ff" or mac.startswith("01:00:5e"):
            continue                        # 广播/组播不可能是相机
        table[mac] = ip
    return table


def _arp_table_linux():
    """/proc/net/arp → {mac: ip}（flags 0x2 = 有映射；0x0 是不完整条目）。"""
    table = {}
    try:
        with open("/proc/net/arp", "r", encoding="utf-8", errors="replace") \
                as fh:
            next(fh)                        # 表头
            for line in fh:
                parts = line.split()
                if len(parts) < 6:
                    continue
                ip, _hw, _fl, mac = parts[0], parts[1], parts[2], parts[3]
                if parts[2] == "0x0":
                    continue                # 未完成的解析
                mac = mac.lower()
                if mac == "ff:ff:ff:ff:ff:ff" or mac.startswith("01:00:5e"):
                    continue
                table[mac] = ip
    except OSError:
        pass
    return table


def arp_table():
    """本机 ARP 表 {mac: ip}（按平台分发；解析失败返回空表）。"""
    if sys.platform == "win32":
        try:
            return _arp_table_windows()
        except Exception:                   # noqa: BLE001 —— 解析失败按空表
            return {}
    return _arp_table_linux()


def local_ip():
    """本机默认出口 IP（不真发包：UDP connect 后看本地端）。"""
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        sock.connect(("10.255.255.255", 1))
        return sock.getsockname()[0]
    except OSError:
        return "127.0.0.1"
    finally:
        sock.close()


def local_ipv4s():
    """本机所有非环回 IPv4（相机可能挂在**任意**一个网段上）。

    [为什么要它 2026-11-20 板端实锅] 板子三网口（eth0=DHCP 10.168.1.100、
    eth1 静态 192.168.0.10、wlan0=192.168.1.249）。相机原先在 WiFi 网段
    192.168.1.181，换了网线后 DHCP 到 **10.168.1.101** —— 而 MAC 解析
    只按配置 IP 的 /24 扫，永远扫不到 ⇒ 「MAC 未解析到 IP ⇒ 暂用配置 IP
    连接」→ `No route to host`（实锅：16:27 日志）。⇒ 解析必须对**本机
    每个网段**都扫一轮。
    """
    ips = []
    try:
        out = subprocess.run(["ip", "-4", "-o", "addr", "show"],
                             capture_output=True, timeout=4.0)
        text = out.stdout.decode("utf-8", "replace")
        for ln in text.splitlines():
            m = re.search(r"inet (\d+\.\d+\.\d+\.\d+)/", ln)
            if m and not m.group(1).startswith("127."):
                ips.append(m.group(1))
    except Exception:                       # noqa: BLE001 —— 无 ip 命令则兜底
        pass
    if not ips:
        try:
            ips.append(local_ip())
        except Exception:                   # noqa: BLE001
            pass
    return ips


def sweep_subnet(seed_ip, ports=(554, 80), timeout=0.35, workers=64):
    """对 seed_ip 的 /24 做一轮 TCP 探测（成败都算）⇒ 借内核填 ARP 表。

    [成败都算] 连上（相机在）与被拒（其它设备）都会先走 ARP 解析 ⇒
    本函数只负责"让网段里的机器在 ARP 表里露脸"，不做服务识别。
    并发线程池 workers 路（254×2 端口 ≈ 数秒），线程守护、不给退出添堵。
    """
    parts = str(seed_ip or "").split(".")
    if len(parts) != 4:
        return 0
    base = ".".join(parts[:3])
    targets = []
    for last in range(1, 255):
        ip = "%s.%d" % (base, last)
        if ip == seed_ip:
            continue
        for port in ports:
            targets.append((ip, port))
    lock = threading.Lock()
    done = [0]

    def _probe():
        while True:
            with lock:
                if not targets or done[0] > 20000:
                    return
                ip, port = targets.pop()
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.settimeout(timeout)
            try:
                sock.connect_ex((ip, port))
            except OSError:
                pass
            finally:
                try:
                    sock.close()
                except OSError:
                    pass
                with lock:
                    done[0] += 1

    threads = [threading.Thread(target=_probe, daemon=True)
               for _ in range(max(1, min(workers, len(targets))))]
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    return done[0]


def resolve_mac_ip(mac, seed_ip=None, sweep=True,
                   arp_fn=arp_table, sweep_fn=sweep_subnet,
                   ipv4s_fn=local_ipv4s, probe_fn=None):
    """MAC → 当前 IP；查不到返回 None。

    :param seed_ip: 网段种子（上次解析到的 IP / 配置 ip）。
    :param sweep: True=表里没有时做网段探测再查（关掉可省数秒）。
    :param ipv4s_fn: 本机 IPv4 枚举（测试注入用）。
    :param probe_fn: 可达性验证 `fn(ip)->bool`（缺省 TCP 554/80 探测）。

    [多网段 2026-11-20] 相机换了网线 ⇒ IP 跳到另一个本机网段
    （192.168.1.181 → 10.168.1.101），只扫配置 IP 的 /24 必然扫不到
    ⇒ 按「seed 优先，其余本机网段依次」逐个扫，命中即返回。
    [可达性验证] ARP 里可能残留旧网段的同 MAC 条目（缓存未过期）⇒ 解析
    到的 IP 先验一次 TCP 可达，不可达则继续找下一个（旧条目不会再骗人）。
    """
    want = normalize_mac(mac)
    if not want:
        return None
    probe = probe_fn or _probe_reachable
    table = arp_fn()
    ip = table.get(want)
    if ip and probe(ip):
        return ip
    if not sweep:
        return ip or None
    seeds = []
    if seed_ip:
        seeds.append(seed_ip)
    for cand in (ipv4s_fn() or []):
        if cand not in seeds:
            seeds.append(cand)
    if not seeds:
        seeds = [local_ip()]
    for seed in seeds:
        try:
            sweep_fn(seed)
        except Exception:                   # noqa: BLE001 —— 单网段失败继续下一个
            continue
        ip = arp_fn().get(want)
        if ip and probe(ip):
            return ip
    return None


def _probe_reachable(ip, ports=(554, 80), timeout=1.5):
    """该 IP 的 RTSP/HTTP 端口是否有应答（连上/被拒都算"有主机"）。"""
    for port in ports:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(timeout)
        try:
            err = sock.connect_ex((ip, port))
            # 0=连上；111/10061=被拒（主机在，端口关）——都算可达
            if err == 0 or err in (111, 61, 10061):
                return True
        except OSError:
            pass
        finally:
            try:
                sock.close()
            except OSError:
                pass
    return False
