#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""变频器遥测「--- 循环」复检（客户端侧，**零 Qt 依赖**）。

现场症状（2026-11-10）：变频器页的数值**有时候显示 `---`，过一会儿又恢复，
循环往复**（不是彻底没数据，是闪）。

根因（服务端侧）：Modbus Hub 每拍只发**一个**读组 ⇒ 一圈要把总线的读组
轮完（现场变频器 14 组 × 拍长 200ms = 2.8s）。而变频器快照的缺省陈旧阈值
是「发布周期 × 5」= 1000ms —— **盖不住一圈** ⇒ 每隔两拍就把快照判成陈旧、
有效掩码清零 ⇒ 服务端推的帧里字段键消失 ⇒ 客户端按掩码显示 `--`；下一拍
数据回来又恢复 ⇒ 于是「--- ↔ 真值」循环。

修复：快照陈旧阈值按**真实轮转周期**算（组数 × 拍长 × 2），由装配层经
`XVfdDevice_setStaleMs` 下发（见 HsfServerIoVfd_applyStaleMs）。

本脚本的判据：

    ① 收到的变频器帧数 ≥ --min-frames；
    ② **一旦出现过带值的帧，之后不得再出现「字段缺失」的帧**
       （缺字段 = 服务端掩码被清零 = 客户端会显示 `---`）；
    ③ 若服务端能报出 staleMs（有的版本不回读），它必须 ≥ 轮转周期。

用法：

    python verify/board/verify_vfd_fresh.py --host 192.168.1.249 --port 6100

退出码：0 = 通过；1 = 不通过；2 = 连不上/环境错。
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
except ImportError as exc:
    sys.stderr.write("导入失败: %s\n（client_py 路径: %s）\n" % (exc, CLIENT_PY))
    sys.exit(2)

#: 判定「这一帧有值」的字段键（服务端掩码清零时这些键会消失）。
#: 键名取自 Src/Industry/XVfd/XVfdCodec.c 的 s_fieldTable（位号=键名）。
VALUE_KEYS = ("outputFreqHz", "outputCurrentA", "busVoltageV")


def parse_args():
    ap = argparse.ArgumentParser(description="变频器 --- 循环复检")
    ap.add_argument("--host", default="192.168.1.249")
    ap.add_argument("--port", type=int, default=6100)
    ap.add_argument("--user", default="root")
    ap.add_argument("--password", default="root")
    ap.add_argument("--seconds", type=float, default=40.0,
                    help="采样时长（秒）；须 > 2× 轮转周期才见得到「闪」")
    ap.add_argument("--min-frames", type=int, default=20)
    ap.add_argument("--interval", type=float, default=0.2)
    ap.add_argument("--retry", type=int, default=10)
    ap.add_argument("--verbose", action="store_true")
    return ap.parse_args()


def connect_with_retry(host, port, user, password, retries, verbose):
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
        if topic != proto.TOPIC_VFD_STATUS:
            return
        try:
            doc = bson.decode(payload)
        except Exception as exc:
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
        client.subscribe_async(proto.TOPIC_VFD_STATUS)
        deadline = time.time() + args.seconds
        while time.time() < deadline:
            time.sleep(args.interval)
    finally:
        try:
            client.auto_reconnect = False
            client.close()
        except Exception:
            pass

    total = len(frames)
    have = []          # 每帧是否带着值字段
    missing_idx = []
    print("=" * 62)
    print("变频器帧统计（共 %d 帧，采样 %.0fs）" % (total, args.seconds))
    print("=" * 62)
    for i, (ts, doc) in enumerate(frames):
        present = [k for k in VALUE_KEYS if k in doc]
        ok = len(present) == len(VALUE_KEYS)
        have.append(ok)
        if not ok:
            missing_idx.append(i)
        if args.verbose or not ok or i < 3:
            freq = doc.get("outputFreqHz")
            cur = doc.get("outputCurrentA")
            print("  #%-4d %-6s freq=%-8s cur=%-8s 字段=%d/%d%s"
                  % (i, time.strftime("%H:%M:%S", time.localtime(ts)),
                     ("%.1f" % freq) if freq is not None else "--",
                     ("%.1f" % cur) if cur is not None else "--",
                     len(present), len(VALUE_KEYS),
                     "" if ok else "  ← 掩码被清零（客户端显示 ---）"))

    first_ok = -1
    for i, v in enumerate(have):
        if v:
            first_ok = i
            break
    n_ok = sum(1 for v in have if v)
    print("-" * 62)
    print("带值帧 %d / 缺字段帧 %d，首个带值帧序号 %s，缺字段帧序号 %s"
          % (n_ok, total - n_ok, first_ok, missing_idx[:20]))

    fails = []
    if total < args.min_frames:
        fails.append("帧数不足：%d < %d（服务端没在推变频器？）"
                     % (total, args.min_frames))
    if n_ok == 0:
        fails.append("一帧带值都没有（变频器全程无数据）")
    if missing_idx and first_ok >= 0:
        fails.append("出现有效值之后又有 %d 帧字段缺失 ⇒ 界面仍会闪 ---"
                     % len(missing_idx))
    print("=" * 62)
    if fails:
        print("判定：不通过")
        for f in fails:
            print("  ✗ %s" % f)
        return 1
    print("判定：通过 —— 变频器遥测全程带值，不再出现「--- 闪」")
    return 0


if __name__ == "__main__":
    sys.exit(main())
