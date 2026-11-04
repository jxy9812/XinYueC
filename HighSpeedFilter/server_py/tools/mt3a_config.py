# -*- coding: utf-8 -*-
"""MT3A-IO1632 模块配置工具（读 / 备份 / 改）。

    python tools/mt3a_config.py --list
        列出网段内所有模块的当前配置（ms/点数/IP）

    python tools/mt3a_config.py --backup 192.168.0.12
        备份某模块的完整配置到文件（改之前必做）

    python tools/mt3a_config.py --set 192.168.0.12 --ms 0
        只改工作模式（主站↔从站），**其余字段按备份原样回填**

    python tools/mt3a_config.py --restore 192.168.0.12 --from backup.json
        按备份完整还原

================================================================================
                        ⚠ 最重要的实测教训（务必先读）
================================================================================

**`POST /config_ch.cgi` 会清空 I/O 点数！**

这是本项目最坑的一个坑，实测过程：

    改 ms=1 → 0（切从站）
    提交成功（固件返回倒计时页 = "已接受"）
    重启后：ms=0 确实生效了，但
           sysDoNu 从 32 → 0
           sysDiNu 从 32 → 0

**I/O 点数被清零后，模块在 Modbus 上"可连但完全不应答"**
（所有读请求返回 0 字节，看似设备坏了，其实是没配任何点位）。

原因：`config_ch.cgi` 只接受**网络类**字段（ms/slaId/lip00/gw/sub/rip/rport…），
`sysDoNu`/`sysDiNu`/`es*` 这些 I/O 字段**不属于它的表单管辖**，提交时被固件
按"未提供"处理 → 归零。真正的 I/O 配置页是 `/io_param_ch.cgi`（该页面
在部分固件上返回空，无法脚本化）。

**所以本工具的做法**：改任何字段前先 `--backup` 拿到全部字段，
提交时把**所有字段原样带上**（包括 sys*/es*），只有你要改的那个不同。
这样即使固件不清空，也不会因为"漏传"而丢配置。

**改完必须复核**：本工具会自动重读并比对，点数掉了会明确报警。

================================================================================
                              模块的角色模型
================================================================================

MT3A-IO1632 有两种角色，由 `ms` 字段决定：

    ms = 1  主站模式
            · 主动向 rip:rport 推送数据（TCP 客户端行为）
            · **不响应任何 Modbus 从站请求**
            · 现象：502 端口 TCP 能连，但连接立刻被关 或 零应答
            · 可用于"聚合"：把多个从站的点位映射到自己的地址空间
              （本例：.12 主站聚合了 .13 的 16 点，对外是 32DI/32DO）

    ms = 0  从站模式
            · 标准 Modbus TCP 从站，响应 FC01/02/03/04/05/06
            · IP 由 slaId 决定：slaId = N → 取 lip0N
            · **前提是 sysDoNu/sysDiNu 已配好点位**，否则同样零应答

**现场配置（2026-11-04 实测）**：

    .12  ms=1  sysDoNu=32  sysDiNu=32   ← 聚合者，rip=192.168.0.9:8080
          esDoNu1=16 esDiNu1=16         ← 条目1 映射 .13 的 16 点
    .13  ms=1  sysDoNu=16  sysDiNu=16   ← 被聚合者，rip=192.168.0.243:8080

两者都是主站（都往外推数据），所以**都不是好的 Modbus 从站**。
要采集就得把**聚合者 .12** 改成 ms=0 —— 但必须先解决点数被清空的问题
（见上）。
"""

import argparse
import json
import os
import re
import socket
import sys
import time
import urllib.parse

HERE = os.path.dirname(os.path.abspath(__file__))

#: 模块 Web 账密（现场统一）。
WEB_USER = "amx666"
WEB_PASS = "amx666"

#: 网络类字段（config_ch.cgi 真正管辖的）。
NETWORK_FIELDS = ("ms", "slaId", "slaNum", "gw", "sub",
                  "rip", "rport", "mport", "lip00")

#: I/O 类字段前缀 —— 提交时必须原样带上，否则会被固件清零。
IO_FIELD_PREFIXES = ("sys", "es")


# ============================================================================
#                            HTTP（该固件不规范）
# ============================================================================

def http_request(host, path, body=None, timeout=10.0):
    """裸 socket HTTP：该固件 POST 响应无状态行，urllib 会抛 BadStatusLine。"""
    method = "POST" if body is not None else "GET"
    headers = ["%s %s HTTP/1.0" % (method, path),
               "Host: %s" % host, "Connection: close"]
    payload = b""
    if body is not None:
        payload = body.encode("utf-8") if isinstance(body, str) else body
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
            if b"</html>" in data:
                break
        return b"".join(chunks)
    finally:
        sock.close()


def read_config(host, timeout=6.0):
    """读 config.js → dict。该固件 JSON 不规范（尾逗号等），用正则逐字段提取。"""
    raw = http_request(host, "/config.js", timeout=timeout)
    text = raw.decode("utf-8", "replace")
    pairs = re.findall(r'"([A-Za-z0-9_]+)"\s*:\s*"?([^,"}]*)"?', text)
    return {k: v.strip() for k, v in pairs}


def http_ok(host, timeout=2.0):
    try:
        socket.create_connection((host, 80), timeout).close()
        return True
    except Exception:                                   # noqa: BLE001
        return False


def modbus_ok(host, timeout=2.0):
    try:
        socket.create_connection((host, 502), timeout).close()
        return True
    except Exception:                                   # noqa: BLE001
        return False


# ============================================================================
#                          读 / 备份 / 写
# ============================================================================

def describe(cfg):
    """一行摘要（人读）。"""
    ms = cfg.get("ms", "?")
    role = "主站" if ms == "1" else ("从站" if ms == "0" else "?")
    io_parts = []
    for key, label in (("sysDoNu", "DO"), ("sysDiNu", "DI"),
                       ("sysRoNu", "RO"), ("sysRwNu", "RW")):
        v = cfg.get(key, "0")
        if v not in ("", "0"):
            io_parts.append("%s=%s" % (label, v))
    io_text = " ".join(io_parts) if io_parts else "**无点位**"
    return ("ms=%s(%s) %-28s lip00=%-14s ripple=%s:%s"
            % (ms, role, io_text, cfg.get("lip00", "?"),
               cfg.get("rip", "?"), cfg.get("rport", "?")))


def list_modules(subnets=("192.168.0",), port_timeout=0.4):
    """扫网段找模块（认 80 口 + module 字段）。"""
    import concurrent.futures

    def probe(ip):
        if not http_ok(ip, port_timeout):
            return None
        try:
            cfg = read_config(ip, timeout=3.0)
        except Exception:                               # noqa: BLE001
            return None
        if cfg.get("module", "").startswith("MT3A"):
            return (ip, cfg)
        return None

    ips = []
    for subnet in subnets:
        ips.extend("%s.%d" % (subnet, i) for i in range(1, 255))
    found = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=100) as ex:
        for r in ex.map(probe, ips):
            if r:
                found.append(r)
    return sorted(found)


def backup(host, out_dir=None):
    """备份完整配置到 JSON。返回文件路径。"""
    cfg = read_config(host)
    if not cfg:
        raise RuntimeError("读不到 %s 的配置" % host)
    out_dir = out_dir or os.path.join(HERE, "module_backups")
    os.makedirs(out_dir, exist_ok=True)
    stamp = time.strftime("%Y%m%d-%H%M%S")
    mac = cfg.get("mac", "unknown").replace(":", "")
    path = os.path.join(out_dir, "mt3a_%s_%s.json" % (mac, stamp))
    with open(path, "w", encoding="utf-8") as fh:
        json.dump({"host": host, "saved_at": stamp, "config": cfg},
                  fh, ensure_ascii=False, indent=2)
    return path


def apply_config(host, cfg, changes, wait_seconds=14):
    """提交配置：**全部字段原样带上**，只覆盖 changes 指定的。

    [为什么要全量带] 见文件头 —— 漏传 I/O 字段会被固件清零。
    :returns: (ok, before, after) —— after 是重启后重读的结果。
    """
    before = dict(cfg)
    login_body = urllib.parse.urlencode({"user": WEB_USER, "pwd": WEB_PASS})
    http_request(host, "/login_ch.cgi", login_body)
    time.sleep(0.4)

    form = dict(before)
    form.update(changes)
    # 账密三件套（固件要求 5~9 位且必填）
    form["name"] = WEB_USER
    form["pwd"] = WEB_PASS
    form["pwd2"] = WEB_PASS
    form = {k: v for k, v in form.items() if v != ""}

    resp = http_request(host, "/config_ch.cgi",
                        urllib.parse.urlencode(form), timeout=20.0)
    text = resp.decode("utf-8", "replace")
    accepted = ("倒计时" in text or "Configuration" in text
                or "j=8" in text or "j = 8" in text)
    print("  提交 %d 字段 / 响应 %d 字节 / %s"
          % (len(form), len(resp), "已接受" if accepted else "响应异常"))

    print("  等 %d 秒软重启…" % wait_seconds)
    time.sleep(wait_seconds)
    try:
        after = read_config(host)
    except Exception as exc:                            # noqa: BLE001
        print("  重读失败（可能还在重启）：%s" % type(exc).__name__)
        return accepted, before, None
    return accepted, before, after


def diff_io(before, after):
    """比对 I/O 字段，返回丢失的 (字段, 旧值, 新值) 列表。"""
    if not after:
        return []
    lost = []
    for key, old in before.items():
        if not key.startswith(IO_FIELD_PREFIXES):
            continue
        new = after.get(key, "")
        if old not in ("", "0") and new in ("", "0"):
            lost.append((key, old, new))
    return lost


# ============================================================================
#                              入口
# ============================================================================

def main(argv=None):
    ap = argparse.ArgumentParser(
        description="MT3A-IO1632 配置工具（读/备份/改，带 I/O 点数保护）")
    ap.add_argument("--list", action="store_true", help="列出网段内所有模块")
    ap.add_argument("--subnets", default="192.168.0",
                    help="扫描网段，逗号分隔（缺省 192.168.0）")
    ap.add_argument("--show", default=None, metavar="HOST", help="显示某模块配置")
    ap.add_argument("--backup", default=None, metavar="HOST", help="备份配置")
    ap.add_argument("--set", default=None, metavar="HOST",
                    help="改配置（配合 --ms/--lip00 等）")
    ap.add_argument("--restore", default=None, metavar="HOST",
                    help="按 --from 备份文件还原")
    ap.add_argument("--from", dest="from_file", default=None,
                    help="备份文件路径（--restore 用）")
    ap.add_argument("--ms", default=None, help="工作模式：1=主站 0=从站")
    ap.add_argument("--lip00", default=None, help="本机 IP")
    ap.add_argument("--sla-id", default=None, help="从站号（决定用 lip0N）")
    ap.add_argument("--sys-do", default=None, help="DO 点数（如 32）")
    ap.add_argument("--sys-di", default=None, help="DI 点数（如 32）")
    ap.add_argument("--dry-run", action="store_true", help="只显示将要改什么")
    args = ap.parse_args(argv)

    # ---- 列出 ----
    if args.list:
        subnets = [s.strip() for s in args.subnets.split(",") if s.strip()]
        found = list_modules(subnets)
        if not found:
            print("未找到 MT3A 模块")
            return 1
        print("找到 %d 台模块：" % len(found))
        for ip, cfg in found:
            print("  %-15s %s" % (ip, describe(cfg)))
            print("  %-15s Modbus502=%s Web80=%s"
                  % ("", "可连" if modbus_ok(ip) else "不可连",
                     "可连" if http_ok(ip) else "不可连"))
        return 0

    # ---- 显示 ----
    if args.show:
        cfg = read_config(args.show)
        print("%s：" % args.show)
        for key in sorted(cfg):
            if key in ("name", "pwd"):
                continue
            print("  %-12s = %s" % (key, cfg[key]))
        print()
        print("摘要：" + describe(cfg))
        return 0

    # ---- 备份 ----
    if args.backup:
        path = backup(args.backup)
        print("已备份 %s → %s" % (args.backup, path))
        return 0

    # ---- 还原 ----
    if args.restore:
        if not args.from_file:
            print("--restore 需要 --from <备份文件>", file=sys.stderr)
            return 2
        with open(args.from_file, encoding="utf-8") as fh:
            data = json.load(fh)
        target = data["config"]
        cfg = read_config(args.restore)
        ok, before, after = apply_config(args.restore, cfg, target)
        _report(ok, before, after, target)
        return 0 if ok else 1

    # ---- 改 ----
    if args.set:
        changes = {}
        if args.ms is not None:
            if args.ms not in ("0", "1"):
                print("--ms 只能是 0 或 1", file=sys.stderr)
                return 2
            changes["ms"] = args.ms
        if args.lip00:
            changes["lip00"] = args.lip00
        if args.sla_id is not None:
            changes["slaId"] = args.sla_id
        if args.sys_do is not None:
            changes["sysDoNu"] = args.sys_do
        if args.sys_di is not None:
            changes["sysDiNu"] = args.sys_di
        if not changes:
            print("没有指定要改的字段（--ms/--lip00/--sla-id/--sys-do/--sys-di）",
                  file=sys.stderr)
            return 2

        cfg = read_config(args.set)
        print("当前：" + describe(cfg))
        print("将改：%s" % changes)
        if args.dry_run:
            print("[dry-run] 不提交")
            return 0

        # 自动备份（改之前）
        path = backup(args.set)
        print("已自动备份 → %s" % path)

        ok, before, after = apply_config(args.set, cfg, changes)
        _report(ok, before, after, changes)
        return 0 if ok else 1

    ap.print_help()
    return 0


def _report(ok, before, after, changes):
    """统一的提交后报告（含 I/O 点数丢失报警）。"""
    if not ok:
        print("!! 提交未被接受")
        return
    if after is None:
        print("!! 重启后读不到配置，请稍后手工 --show")
        return
    print()
    print("改前：" + describe(before))
    print("改后：" + describe(after))
    print()
    # 变更是否生效
    for key, want in changes.items():
        got = after.get(key, "")
        mark = "✓" if got == want else "✗"
        print("  %s %s: 期望 %r 实际 %r" % (mark, key, want, got))
    # I/O 点数丢失检查（本工具的立身之本）
    lost = diff_io(before, after)
    if lost:
        print()
        print("!! [严重] I/O 点数被固件清空（config_ch.cgi 不管辖这些字段）：")
        for key, old, new in lost:
            print("     %s: %s → %s" % (key, old, new))
        print("   后果：模块在 Modbus 上「可连但零应答」")
        print("   处理：用浏览器打开 http://%s 的 I/O 配置页手工补回点数"
              % (after.get("lip00") or "模块"))
        print("   备份文件里有原值，可参照：")
    else:
        print()
        print("  ✓ I/O 点数未受影响")


if __name__ == "__main__":
    sys.exit(main())
