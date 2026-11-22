#!/usr/bin/env python3
# verify/board/leak_analyze.py —— 板端 HSF 堆计数趋势分析（泄漏判读）。
#
# 用法: python leak_analyze.py <服务端heap日志> [板端客户端heap日志] [采样CSV]
#
# 判读口径（AGENTS.md「泄漏结论必须用堆计数」）：
#   ① 堆计数按**会话数**分组看趋势：固定会话数下堆随时间线性上涨 = 泄漏；
#   ② 「第二轮不再涨 / 回落」= 堆 arena 平台期，不是泄漏；
#   ③ 服务端每会话增量（Δheap/Δsessions）应恒定：逐会话涨 = 会话泄漏；
#   ④ 采样 CSV 的 VmData/fds/threads：fds 单调涨 = 套接字/会话没回收。
import re
import sys


def load_heap(path, key):
    rows = []
    if not path:
        return rows
    pat = re.compile(r"\[heap\]\s+sys=(\d+)\s+%s=(\d+)\s+t=(\d+)" % key)
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            m = pat.search(line)
            if m:
                rows.append((int(m.group(3)), int(m.group(1)), int(m.group(2))))
    rows.sort()
    return rows


def slope(xs, ys):
    n = len(xs)
    if n < 3:
        return None
    mx = sum(xs) / n
    my = sum(ys) / n
    num = sum((x - mx) * (y - my) for x, y in zip(xs, ys))
    den = sum((x - mx) ** 2 for x in xs)
    if den == 0:
        return None
    return num / den


def report(title, rows, sess_key=True):
    if not rows:
        print("== %s: 无数据 ==" % title)
        return
    t0 = rows[0][0]
    print("== %s: %d 条采样, 跨度 %.1f 分钟 ==" %
          (title, len(rows), (rows[-1][0] - t0) / 60000.0))
    # 总体
    s = slope([r[0] for r in rows], [r[1] for r in rows])
    if s is not None:
        print("   全程斜率: %+.1f B/s (%+.1f KB/min)" % (s, s * 60 / 1024.0))
    # 分组：固定会话数看斜率
    groups = {}
    for t, h, sess in rows:
        groups.setdefault(sess, []).append((t, h))
    for sess in sorted(groups):
        g = groups[sess]
        if len(g) < 4:
            continue
        s = slope([x[0] for x in g], [x[1] for x in g])
        if s is None:
            continue
        print("   sessions=%d: n=%3d 首=%9d 末=%9d Δ=%+8d 斜率=%+8.1f B/s (%+6.1f KB/min)"
              % (sess, len(g), g[0][1], g[-1][1], g[-1][1] - g[0][1], s, s * 60 / 1024.0))
    # 每会话增量（相邻会话组的堆差）
    if sess_key:
        base = {k: (v[0][1] + v[-1][1]) / 2.0 for k, v in groups.items() if v}
        ks = sorted(base)
        print("   各会话数均值堆:", ", ".join("s%d=%.0f" % (k, base[k]) for k in ks))
        for a, b in zip(ks, ks[1:]):
            print("   每会话增量 s%d->s%d: %+.0f B" % (a, b, base[b] - base[a]))


def report_csv(path):
    if not path:
        return
    print("== 采样 CSV: %s ==" % path)
    rows = {}
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            p = line.strip().split(",")
            if len(p) != 8 or p[0] == "ts":
                continue
            rows.setdefault((p[1], p[2]), []).append(
                (int(p[0]), int(p[3]), int(p[4]), int(p[5]), int(p[6]), int(p[7])))
    for (tag, pid), g in rows.items():
        g.sort()
        print("   %s pid=%s: n=%d  VmData %d->%d kB (Δ%+d)  VmRSS %d->%d kB (Δ%+d)  "
              "VmSize %d->%d kB  fds %d->%d  threads %d->%d  跨度 %.1f 分钟"
              % (tag, pid, len(g), g[0][2], g[-1][2], g[-1][2] - g[0][2],
                 g[0][1], g[-1][1], g[-1][1] - g[0][1],
                 g[0][3], g[-1][3], g[0][5], g[-1][5], g[0][4], g[-1][4],
                 (g[-1][0] - g[0][0]) / 60.0))
        sd = slope([x[0] for x in g], [x[2] for x in g])
        if sd is not None:
            print("      VmData 斜率 %+.2f kB/min" % (sd * 60 / 1024.0))


def main():
    srv = sys.argv[1] if len(sys.argv) > 1 else None
    cli = sys.argv[2] if len(sys.argv) > 2 else None
    csv = sys.argv[3] if len(sys.argv) > 3 else None
    report("板端服务端 [heap]", load_heap(srv, "sessions"))
    report("板端客户端 [heap]", load_heap(cli, "link"), sess_key=False)
    report_csv(csv)


if __name__ == "__main__":
    main()
