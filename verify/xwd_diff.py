#!/usr/bin/env python3
"""两份 XWD 截图的像素级对比（纯标准库）。

用法: python3 xwd_diff.py <a.xwd> <b.xwd> [跳过的矩形 x,y,w,h ...]

跳过区用于排除必然变化的动态元素（时钟、FPS 浮层等）。
输出：逐区差异像素数 / 总差异像素数 / 最大单像素通道差。
"""
import struct
import sys


def load(path):
    with open(path, "rb") as fh:
        data = fh.read()
    header_size = struct.unpack_from(">I", data, 0)[0]
    width = struct.unpack_from(">I", data, 4 * 4)[0]
    height = struct.unpack_from(">I", data, 5 * 4)[0]
    bpl = struct.unpack_from(">I", data, 12 * 4)[0]
    ncolors = struct.unpack_from(">I", data, 19 * 4)[0]
    return data, header_size + ncolors * 12, bpl, width, height


def main():
    a_path, b_path = sys.argv[1], sys.argv[2]
    skips = []
    for arg in sys.argv[3:]:
        skips.append(tuple(int(v) for v in arg.split(",")))

    a, a_off, a_bpl, w, h = load(a_path)
    b, b_off, b_bpl, _, _ = load(b_path)
    if (w, h) != (struct.unpack_from(">I", b, 4 * 4)[0],
                  struct.unpack_from(">I", b, 5 * 4)[0]):
        raise SystemExit("尺寸不一致，无法对比")

    def skipped(x, y):
        for sx, sy, sw, sh in skips:
            if sx <= x < sx + sw and sy <= y < sy + sh:
                return True
        return False

    diff_px = 0
    total_px = 0
    max_delta = 0
    rows_with_diff = []
    for y in range(h):
        row_diff = 0
        base_a = a_off + y * a_bpl
        base_b = b_off + y * b_bpl
        for x in range(w):
            if skipped(x, y):
                continue
            total_px += 1
            ia = base_a + x * 4
            ib = base_b + x * 4
            d = max(abs(a[ia] - b[ib]),
                    abs(a[ia + 1] - b[ib + 1]),
                    abs(a[ia + 2] - b[ib + 2]))
            if d:
                diff_px += 1
                row_diff += 1
                if d > max_delta:
                    max_delta = d
        if row_diff:
            rows_with_diff.append((y, row_diff))

    print("尺寸            : %dx%d" % (w, h))
    print("跳过区          : %s" % (skips if skips else "无"))
    print("参与对比像素    : %d" % total_px)
    print("差异像素        : %d (%.4f%%)" % (diff_px, 100.0 * diff_px / max(1, total_px)))
    print("最大单像素差    : %d" % max_delta)
    if rows_with_diff:
        print("有差异的行数    : %d" % len(rows_with_diff))
        for y, n in rows_with_diff[:12]:
            print("   y=%4d 差异 %d px" % (y, n))
    else:
        print("有差异的行数    : 0（画面逐像素一致）")


if __name__ == "__main__":
    main()
