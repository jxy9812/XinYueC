#!/usr/bin/env python
"""XWD 区域像素并排数值取证：打印两图同一区域的灰度剖面与逐像素差。
用法: xwd_region_dump.py a.xwd b.xwd <x> <y> <w> <h> [maxcols]
"""
import struct
import sys


def load(path):
    with open(path, "rb") as fh:
        data = fh.read()
    hs = struct.unpack_from(">I", data, 0)[0]
    w = struct.unpack_from(">I", data, 4 * 4)[0]
    h = struct.unpack_from(">I", data, 5 * 4)[0]
    bpl = struct.unpack_from(">I", data, 12 * 4)[0]
    ncolors = struct.unpack_from(">I", data, 19 * 4)[0]
    off = hs + ncolors * 12
    return data, off, w, h, bpl


def gray(data, off, bpl, x, y):
    p = off + y * bpl + x * 4
    b, g, r, _a = data[p], data[p + 1], data[p + 2], data[p + 3]
    return (r * 30 + g * 59 + b * 11) // 100


def main():
    pa, pb = sys.argv[1], sys.argv[2]
    x0, y0, cw, ch = (int(v) for v in sys.argv[3:7])
    maxcols = int(sys.argv[7]) if len(sys.argv) > 7 else 40
    da, oa, W, H, ba = load(pa)
    db, ob, _, _, bb = load(pb)
    print("=== 区域 (%d,%d) %dx%d —— 上：A=%s 下：B=%s ===" %
          (x0, y0, cw, ch, pa, pb))
    diffs = []
    for y in range(y0, min(y0 + ch, H)):
        ra = "".join("%3d" % gray(da, oa, ba, x, y)
                     for x in range(x0, min(x0 + maxcols, W)))
        rb = "".join("%3d" % gray(db, ob, bb, x, y)
                     for x in range(x0, min(x0 + maxcols, W)))
        dmax = 0
        row = []
        for x in range(x0, min(x0 + cw, W)):
            ga = gray(da, oa, ba, x, y)
            gb = gray(db, ob, bb, x, y)
            d = abs(ga - gb)
            row.append(d)
            if d > dmax:
                dmax = d
            diffs.append(d)
        print("y=%3d A:%s" % (y, ra))
        print("     B:%s   maxdiff=%d" % (rb, dmax))
    if diffs:
        hist = {}
        for d in diffs:
            hist[d] = hist.get(d, 0) + 1
        top = sorted(hist.items(), key=lambda kv: -kv[1])[:10]
        print("差值直方图（差值:像素数）:", ", ".join(
            "%d:%d" % (d, c) for d, c in top))
        print("最大差=%d  非零差像素=%d/%d" %
              (max(diffs), sum(1 for d in diffs if d), len(diffs)))


main()
