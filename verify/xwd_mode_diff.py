#!/usr/bin/env python3
"""定位「模式差异」行：跨模式不同的像素行里，剔除同模式内的时变噪声行。
用法: xwd_mode_diff.py base1.xwd base2.xwd fast1.xwd fast2.xwd
"""
import sys


def load(path):
    with open(path, 'rb') as f:
        data = f.read()
    import struct
    # XWD 固定头 = 25 个大端 u32：word[0]=header_size, [4]=width,
    # [5]=height, [12]=bytes_per_line, [19]=ncolors。
    hs = struct.unpack_from('>I', data, 0)[0]
    w = struct.unpack_from('>I', data, 4 * 4)[0]
    h = struct.unpack_from('>I', data, 5 * 4)[0]
    bpl = struct.unpack_from('>I', data, 12 * 4)[0]
    ncolors = struct.unpack_from('>I', data, 19 * 4)[0]
    off = hs + ncolors * 12
    return data, off, w, h, bpl


def rowset(paths):
    """返回 {y: set(x)}，某行在该路径与其他路径比较中不同的 x 集合。"""
    imgs = [load(p) for p in paths]
    base_data, off0, w, h, bpl = imgs[0]
    out = {}
    for y in range(h):
        base = off0 + y * bpl
        diff = set()
        for data, off, _, _, b in imgs:
            row = off + y * b
            for x in range(w):
                p = row + x * 4
                q = base + x * 4
                if data[p:p + 4] != base_data[q:q + 4]:
                    diff.add(x)
        if diff:
            out[y] = diff
    return out


def main():
    base1, base2, fast1, fast2 = sys.argv[1:5]
    data_a, off0, _w, _h, bpl = load(base1)
    data_b, ob, _w2, _h2, bb = load(fast1)
    intra_base = rowset([base1, base2])
    intra_fast = rowset([fast1, fast2])
    cross = rowset([base1, fast1])

    noise_rows = set(intra_base) | set(intra_fast)
    mode_rows = {y: xs for y, xs in cross.items() if y not in noise_rows}
    print("cross rows=%d  intra-noise rows=%d  mode-only rows=%d"
          % (len(cross), len(noise_rows), len(mode_rows)))
    if not mode_rows:
        return
    ys = sorted(mode_rows)
    # 聚类连续行
    groups = []
    start = prev = ys[0]
    for y in ys[1:]:
        if y - prev > 3:
            groups.append((start, prev))
            start = y
        prev = y
    groups.append((start, prev))
    total = sum(len(mode_rows[y]) for y in ys)
    print("mode-only differing pixels=%d" % total)
    # 模式差异像素的最大通道差（验收口径用：排除同模式内的时变噪声）
    dmax = 0
    hist = {}
    for y in ys:
        for x in mode_rows[y]:
            pa = off0 + y * bpl + x * 4
            pb = ob + y * bb + x * 4
            d = max(abs(data_a[pa + i] - data_b[pb + i]) for i in range(3))
            hist[d] = hist.get(d, 0) + 1
            if d > dmax:
                dmax = d
    top = sorted(hist.items(), key=lambda kv: -kv[1])[:6]
    print("mode-only 最大通道差=%d  差值直方图: %s" % (
        dmax, ", ".join("%d:%d" % (d, c) for d, c in top)))
    # 结构性差异（差值 >= 8）逐点列出：±1 舍入之外的残余需逐点归因
    strong = []
    for y in ys:
        for x in mode_rows[y]:
            pa = off0 + y * bpl + x * 4
            pb = ob + y * bb + x * 4
            d = max(abs(data_a[pa + i] - data_b[pb + i]) for i in range(3))
            if d >= 8:
                strong.append((x, y, d))
    if strong:
        print("结构性差异像素（差值>=8）共 %d 个:" % len(strong))
        for x, y, d in strong[:40]:
            pa = off0 + y * bpl + x * 4
            pb = ob + y * bb + x * 4
            print("   x=%4d y=%4d d=%3d  A=(%3d,%3d,%3d) B=(%3d,%3d,%3d)" % (
                x, y, d,
                data_a[pa + 2], data_a[pa + 1], data_a[pa],
                data_b[pb + 2], data_b[pb + 1], data_b[pb]))
    for a, b in groups:
        n = sum(len(mode_rows[y]) for y in range(a, b + 1) if y in mode_rows)
        xs = [x for y in range(a, b + 1) if y in mode_rows for x in mode_rows[y]]
        print("  y %4d..%-4d  px=%4d  x=%d..%d" % (a, b, n, min(xs), max(xs)))


main()
