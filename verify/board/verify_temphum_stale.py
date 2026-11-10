#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""温湿度「数据过期」复检（客户端侧，**零 Qt 依赖**）。

现场症状（2026-11-10）：把温湿度采集周期调大（10s / 1 分钟）后，主界面
温湿度在「有值 ↔ --」之间来回抖，报警页同步刷「数据过期」。

根因：服务端的质量判定阈值（stale）写死 5000ms，**短于采集周期** ⇒
两拍之间值必被判陈旧（服务端把 tempValid/humValid 报 false，客户端显示
--）。修复口径（HighSpeedFilter/server/HsfServerIo.c 的 hsfIoStaleMs）：

    有效阈值 = max(设备基线 stale, 采集周期 × 3)

并随周期改动实时生效（装配期按 INI 算一次，运行期改周期再推一次）。

本脚本连板端 HsfServer、订阅温湿度主题连盯 N 秒，判据三条：

    ① 收到的温湿度帧数 ≥ --min-frames；
    ② **一旦出现过有效值，之后不得再出现无效帧**（界面不再闪 --）；
    ③ 帧里回读的 staleMs ≥ --poll-ms（阈值确实跟着周期走了）。

用法（Windows Git Bash / 任意有 python3 的机器）:

    python verify/board/verify_temphum_stale.py --host 192.168.1.249 --port 6100

退出码: 0 = 通过；1 = 不通过（并打印失败原因）；2 = 连不上/环境错。

注意（登录闸）: 服务端缺省策略是「同账并发关」⇒ 同一个账号已有在线会话
时再登录会被拒（REJECTED）。脚本按「退避重试」处理（连接被拒就等几秒再试，
覆盖服务端旧会话的静默回收窗口）。
"""

import argparse
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
CLIENT_PY = os.path.join(REPO, "HighSpeedFilter", "client_py")
if CLIENT_PY not in sys.path:
    sys.path.insert(0, CLIENT_PY)

try:
    from hsfsrv_client import bson, proto
    from hsfsrv_client.xlink import XLinkClient
except ImportError as exc:                       # 环境缺失要说清缺什么
    sys.stderr.write("导入失败: %s\n（client_py 路径: %s）\n" % (exc, CLIENT_PY))
    sys.exit(2)


def parse_args():
    ap = argparse.ArgumentParser(description="温湿度数据过期复检")
    ap.add_argument("--host", default="192.168.1.249")
    ap.add_argument("--port", type=int, default=6100)
    ap.add_argument("--user", default="root")
    ap.add_argument("--password", default="root")
    ap.add_argument("--seconds", type=float, default=40.0,
                    help="采样时长（秒）；须 > 2×采集周期才见得到「抖」")
    ap.add_argument("--poll-ms", type=int, default=10000,
                    help="期望的温湿度采集周期（毫秒），用于核对 staleMs ≥ 周期")
    ap.add_argument("--min-frames", type=int, default=10)
    ap.add_argument("--interval", type=float, default=0.2,
                    help="采样间隔（秒）")
    ap.add_argument("--retry", type=int, default=10,
                    help="登录被拒时的重试次数（同账并发闸）")
    ap.add_argument("--verbose", action="store_true")
    return ap.parse_args()


def connect_with_retry(host, port, user, password, retries, verbose):
    """连 + 登录；被拒则退避重试（服务端同账并发闸 / 旧会话回收窗口）。"""
    last = ""
    for i in range(retries):
        client = XLinkClient(host, port, user, password,
                             log=(lambda m: print("[link]", m)) if verbose else None)
        ok, err = client.start(timeout=8.0)
        if ok:
            return client, ""
        last = str(err)
        try:
            client.close()
        except Exception:
            pass
        print("[verify] 登录失败(%d/%d): %s" % (i + 1, retries, last))
        time.sleep(3.0)
    return None, last


def main():
    args = parse_args()
    frames = []

    def on_publish(topic, payload, data_type, *_):
        if topic != proto.TOPIC_TEMPHUM_STATUS:
            return
        try:
            doc = bson.decode(payload)
        except Exception as exc:                  # 解不开的帧不算数
            print("[verify] 解帧失败: %s" % exc)
            return
        frames.append((time.time(), doc))

    client, err = connect_with_retry(args.host, args.port, args.user,
                                     args.password, args.retry, args.verbose)
    if client is None:
        sys.stderr.write("连不上 %s:%d（%s）\n" % (args.host, args.port, err))
        return 2

    try:
        client.on_publish = on_publish
        client.subscribe_async(proto.TOPIC_TEMPHUM_STATUS)
        deadline = time.time() + args.seconds
        while time.time() < deadline:
            time.sleep(args.interval)
    finally:
        try:
            client.auto_reconnect = False
            client.close()
        except Exception:
            pass

    # ---- 判据 ----
    total = len(frames)
    stale_max = 0
    valid_flags = []
    print("=" * 62)
    print("温湿度帧统计（共 %d 帧，采样 %.0fs）" % (total, args.seconds))
    print("=" * 62)
    for i, (ts, doc) in enumerate(frames):
        tv = bool(doc.get("tempValid", False))
        hv = bool(doc.get("humValid", False))
        valid_flags.append(tv or hv)
        stale = int(doc.get("staleMs", 0) or 0)
        stale_max = max(stale_max, stale)
        temp = doc.get("temp")
        hum = doc.get("hum")
        if args.verbose or i < 5 or not (tv or hv):
            print("  #%-3d %-6s temp=%-7s hum=%-7s valid=%-5s staleMs=%d"
                  % (i, time.strftime("%H:%M:%S", time.localtime(ts)),
                     ("%.1f℃" % (temp / 10.0)) if temp is not None else "--",
                     ("%.1f%%" % (hum / 10.0)) if hum is not None else "--",
                     "T" if tv else ("H" if hv else "-"), stale))

    first_valid = -1
    for i, v in enumerate(valid_flags):
        if v:
            first_valid = i
            break
    invalid_after = [i for i, v in enumerate(valid_flags) if first_valid >= 0 and not v]
    n_valid = sum(1 for v in valid_flags if v)

    print("-" * 62)
    print("有效帧 %d / 无效帧 %d，首个有效帧序号 %s，无效帧序号 %s"
          % (n_valid, total - n_valid, first_valid, invalid_after[:20]))
    print("回读 staleMs 最大值 = %d（期望 ≥ 采集周期 %d）"
          % (stale_max, args.poll_ms))

    fails = []
    if total < args.min_frames:
        fails.append("帧数不足：%d < %d（服务端没在推温湿度？）" % (total, args.min_frames))
    if n_valid == 0:
        fails.append("一帧有效值都没有（温湿度全程无数据）")
    if invalid_after:
        fails.append("有效值之后又出现 %d 个无效帧 ⇒ 界面仍会闪 --（数据过期未修复）"
                     % len(invalid_after))
    if stale_max < args.poll_ms:
        fails.append("staleMs=%d < 采集周期 %d ⇒ 阈值没跟着周期走（修复未生效）"
                     % (stale_max, args.poll_ms))

    print("=" * 62)
    if fails:
        print("判定：不通过")
        for f in fails:
            print("  ✗ %s" % f)
        return 1
    print("判定：通过 —— 温湿度全程有值、无「过期闪 --」，阈值随周期联动")
    return 0


if __name__ == "__main__":
    sys.exit(main())
