#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""浊度仪1「偶发数据过期」复检（客户端侧，**零 Qt 依赖**）。

现场症状（2026-11-10 板端 ttyS4）：浊度仪1 偶发整段「数据过期」（约 5 个
episode / 10 分钟，最大间隙 5.7~6.8s），浊度仪2 同总线同节拍却全程正常。

根因（框架层）：XModbusHub 的「发帧前静默闸饥饿逃逸」对**从未成功发过帧**
的从站是死代码——簿记条目只在发送成功后创建，而路 B 读组序是区优先
（IR 流量计恒排第一），开局唯一一次放行被流量计吃掉 ⇒ 要求 200ms 静默的
站 1 冷启动被无声饿死（实测 150.4s 才首帧）；稳态则靠 2s 一次的逃逸轮询，
逃逸帧闯进流量计长帧后的恢复死区时再叠加 700ms 在途超时 ⇒ 回填间隙尾部
6.8s，越过 5000ms 陈旧阈值 ⇒ 健康报 Stale ⇒ 客户端「数据过期」。

修复（2026-11-11）：逃逸簿记惰性建（被静默闸挡住即建条目，冷启动有界
首帧）+ 逃逸帧半静默下限（避开恢复死区）+ 应用层浊度陈旧阈值按
「轮转 + 静默」口径抬档（HsfServerIo.c hsfIoTurbRoundStaleMs）。

本脚本连板端 HsfServer、订阅浊度主题与健康主题连盯 N 秒，判据三条：

    ① 收到的浊度帧数 ≥ --min-frames；
    ② **一旦出现过有效浊度（浊度仪1），之后不得再出现无效帧**
       （v0_online=false 或 v0_ntu 缺失 ⇒ 界面闪「数据过期」）；
    ③ 健康帧里「浊度仪1」不得出现 health=2（HsfDeviceHealth_Stale）。

用法（Windows Git Bash / 任意有 python3 的机器）:

    python verify/board/verify_turbidity_stale.py --host 192.168.1.249 --port 6100
    python verify/board/verify_turbidity_stale.py --seconds 300   # 更长采样

退出码: 0 = 通过；1 = 不通过（并打印失败原因）；2 = 连不上/环境错。

注意（登录闸）: 服务端缺省策略是「同账并发关」⇒ 同一个账号已有在线会话
时再登录会被拒（REJECTED）。脚本按「退避重试」处理（连接被拒就等几秒再试，
覆盖服务端旧会话的静默回收窗口）。跑无人值守时确保没有别的客户端占着账号。

注意（冷启动）: 服务端刚重启的最初 ~3s 内站 1 可能尚未完成首次回填
（修复后有界 ≤ 逃逸时限+静默期 ≈ 2.2s）⇒ 订阅补发的第一份健康快照可能
短暂 Stale。判据 ② 对「首个有效帧之前」的无效帧天然容忍；判据 ③ 若在
服务端启动后立刻跑仍可能首拍命中，稍等几秒再跑即可。
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

HEALTH_STALE = 2                                 # HsfDeviceHealth_Stale


def parse_args():
    ap = argparse.ArgumentParser(description="浊度仪1 数据过期复检")
    ap.add_argument("--host", default="192.168.1.249")
    ap.add_argument("--port", type=int, default=6100)
    ap.add_argument("--user", default="root")
    ap.add_argument("--password", default="root")
    ap.add_argument("--seconds", type=float, default=300.0,
                    help="采样时长（秒）；缺省 300——症状约 5 次/10 分钟的"
                         "偶发间隙，120s 短窗全绿只证明『这 120s 没犯』"
                         "（2026-11-11 复审定案：缺省即取 ≥300 不设防）")
    ap.add_argument("--min-frames", type=int, default=40,
                    help="浊度帧数下限（推送周期 500ms ⇒ 120s 应收 ~240 帧）")
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


def turb_valid(doc):
    """浊度仪1 这一帧是否有效：online=True 且 ntu 键在场。"""
    if not doc.get("v0_online", False):
        return False
    return "v0_ntu" in doc


def main():
    args = parse_args()
    turb_frames = []        # (ts, doc)
    health_frames = []      # (ts, doc)

    def on_publish(topic, payload, data_type, *_):
        if topic not in (proto.TOPIC_TURBIDITY_STATUS, proto.TOPIC_DEVICE_HEALTH):
            return
        try:
            doc = bson.decode(payload)
        except Exception as exc:                  # 解不开的帧不算数
            print("[verify] 解帧失败: %s" % exc)
            return
        if topic == proto.TOPIC_TURBIDITY_STATUS:
            turb_frames.append((time.time(), doc))
        else:
            health_frames.append((time.time(), doc))

    client, err = connect_with_retry(args.host, args.port, args.user,
                                     args.password, args.retry, args.verbose)
    if client is None:
        sys.stderr.write("连不上 %s:%d（%s）\n" % (args.host, args.port, err))
        return 2

    try:
        client.on_publish = on_publish
        client.subscribe_async(proto.TOPIC_TURBIDITY_STATUS)
        client.subscribe_async(proto.TOPIC_DEVICE_HEALTH)
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
    total = len(turb_frames)
    valid_flags = [turb_valid(doc) for _, doc in turb_frames]
    n_valid = sum(1 for v in valid_flags if v)

    first_valid = -1
    for i, v in enumerate(valid_flags):
        if v:
            first_valid = i
            break
    invalid_after = [i for i, v in enumerate(valid_flags)
                     if first_valid >= 0 and not v]
    # 有效段统计（供与历史 episode 对照）：有效帧之间的最大时间间隙。
    max_valid_gap = 0.0
    prev_valid_ts = None
    for ts, doc in turb_frames:
        if turb_valid(doc):
            if prev_valid_ts is not None:
                max_valid_gap = max(max_valid_gap, ts - prev_valid_ts)
            prev_valid_ts = ts

    # 健康帧：浊度仪1 的 health 值（健康帧是变化沿才推，收到的都算数）。
    turb1_bad_health = []                       # (ts, health, seq)
    for ts, doc in health_frames:
        n = int(doc.get("devCount", 0) or 0)
        for i in range(n):
            if doc.get("d%d_name" % i) == "浊度仪1":
                h = int(doc.get("d%d_health" % i, -1) or -1)
                if h == HEALTH_STALE:
                    turb1_bad_health.append(
                        (ts, h, int(doc.get("seq", 0) or 0)))

    stale_ms_min = None
    for _, doc in turb_frames:
        v = doc.get("staleMs")
        if v is not None:
            v = int(v)
            stale_ms_min = v if stale_ms_min is None else min(stale_ms_min, v)

    print("=" * 62)
    print("浊度帧统计（共 %d 帧，有效 %d，采样 %.0fs）"
          % (total, n_valid, args.seconds))
    print("=" * 62)
    for i, (ts, doc) in enumerate(turb_frames):
        if args.verbose or i < 5 or not valid_flags[i]:
            ntu = doc.get("v0_ntu")
            ntu_s = ("%.2f" % (ntu / 100.0)) if ntu is not None else "--"
            on = bool(doc.get("v0_online", False))
            print("  #%-3d %-6s 浊度仪1 online=%-5s ntu=%-8s staleMs=%s"
                  % (i, time.strftime("%H:%M:%S", time.localtime(ts)),
                     on, ntu_s, doc.get("staleMs")))
    if health_frames:
        print("健康帧 %d 份；「浊度仪1」health=2 出现 %d 次"
              % (len(health_frames), len(turb1_bad_health)))

    print("-" * 62)
    print("首个有效帧序号 %s；其后再现无效帧 %s"
          % (first_valid, invalid_after[:20]))
    print("有效帧最大间隔 %.2f s；回读 staleMs 最小值 %s"
          % (max_valid_gap,
             ("=%d" % stale_ms_min) if stale_ms_min is not None else "缺"))

    fails = []
    if total < args.min_frames:
        fails.append("帧数不足：%d < %d（服务端没在推浊度？）"
                     % (total, args.min_frames))
    if n_valid == 0:
        fails.append("一帧有效浊度都没有（浊度仪1 全程无数据）")
    if invalid_after:
        fails.append("首个有效帧之后又出现 %d 个无效帧 ⇒ 界面仍会闪「数据过期」"
                     "（冷启动饿死/逃逸超时尾巴未修复）" % len(invalid_after))
    if turb1_bad_health:
        for ts, h, seq in turb1_bad_health[:10]:
            print("  ✗ 健康帧 %s seq=%d health=%d"
                  % (time.strftime("%H:%M:%S", time.localtime(ts)), seq, h))
        fails.append("「浊度仪1」健康出现 %d 次 health=2（Stale）"
                     % len(turb1_bad_health))

    print("=" * 62)
    if fails:
        print("判定：不通过")
        for f in fails:
            print("  ✗ %s" % f)
        return 1
    print("判定：通过 —— 浊度仪1 全程有值、无「过期闪断」，健康无 Stale")
    return 0


if __name__ == "__main__":
    sys.exit(main())
