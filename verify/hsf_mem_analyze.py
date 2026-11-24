#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""分析 hsf_mem_watch.csv：判断 VmData 增长是持续泄漏还是有界累积。

判据：
  - 分段速率递减 ⇒ 趋向平台（有界缓存，如 SQLite 页缓存 / 趋势图窗口）
  - 分段速率恒定且 RSS 同步线性 ⇒ 疑似真泄漏
  - 线程/fd 单调增长 ⇒ 明确泄漏（本数据里都恒定）
"""
import csv
import sys
from collections import defaultdict

path = sys.argv[1] if len(sys.argv) > 1 else "verify/hsf_mem_watch_data.csv"
rows = defaultdict(list)
with open(path, newline="", encoding="utf-8") as f:
    for r in csv.DictReader(f):
        if r["name"] in ("client", "server", "web"):
            rows[r["name"]].append(r)

for name in ("client", "server", "web"):
    rs = sorted(rows[name], key=lambda x: int(x["ts"]))
    if len(rs) < 4:
        print(f"{name}: too few samples")
        continue
    t0 = int(rs[0]["ts"])
    vd = [int(r["vmdata_kb"]) for r in rs]
    rss = [int(r["vmrss_kb"]) for r in rs]
    ts = [(int(r["ts"]) - t0) / 60.0 for r in rs]  # minutes
    th = sorted(set(int(r["threads"]) for r in rs))
    fd = [int(r["fds"]) for r in rs]

    deltas = [vd[i + 1] - vd[i] for i in range(len(vd) - 1)]
    mono = all(d >= 0 for d in deltas)
    down = sum(1 for d in deltas if d < 0)

    # 4 段平均速率 KB/min
    seg = 4
    n = len(vd)
    step = max(1, (n - 1) // seg)
    rates = []
    for s in range(0, n - 1, step):
        e = min(s + step, n - 1)
        dt = ts[e] - ts[s]
        dv = vd[e] - vd[s]
        rates.append((round(ts[s], 1), round(ts[e], 1),
                      round(dv / dt, 1) if dt > 0 else 0.0, dv))

    print(f"=== {name} ===")
    print(f"  samples={n}  span={ts[-1]:.1f}min")
    print(f"  VmData: {vd[0]} -> {vd[-1]} (Δ{vd[-1]-vd[0]:+d} KB)")
    print(f"  RSS:    {rss[0]} -> {rss[-1]} (Δ{rss[-1]-rss[0]:+d} KB)")
    print(f"  threads={th}  fd={min(fd)}..{max(fd)}  monotonic_nondec={mono}  down_steps={down}")
    print(f"  delta sequence: {deltas}")
    print(f"  segment rates (start_min,end_min,KB/min,KB): {rates}")
