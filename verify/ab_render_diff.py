#!/usr/bin/env python3
"""渲染模式 A/B 像素比对（DIRECT vs FULL，桌面 x64 门禁用）。

用法:
    python3 verify/ab_render_diff.py <截图目录> [页号...]

截图目录内应有两组 PNG：``DIRECT_p<N>.png`` 与 ``FULL_p<N>.png``
（由 demo 的 ``--screenshot <file> --page <N>`` 出图）。

**为什么需要跳过两个矩形**：顶栏时钟的秒位与右下角 FPS/内存 HUD 悬浮层是
**实时读数**，两次运行必然不同，不是渲染差异。排除这两块后比对的是**静态
画面**——那才是「渲染模式是否等价」的有效判据。

退出码：静态画面完全一致 ⇒ 0；存在静态差异 ⇒ 1。

依据：docs/485轮询性能排查与修复_20261029.md §17.2（DIRECT 落地复验）。
"""
import os
import sys

try:
    from PIL import Image
except ImportError:
    raise SystemExit("需要 Pillow：pip install pillow")

# 动态读数区：(x, y, w, h)——顶栏时钟秒位、FPS/内存 HUD 悬浮层。
# 口径按实测差异簇收紧/放宽：顶栏时钟实际占 x≈880~960（秒位在 912~944），
# HUD 浮层占 x≈870~1000 / y≈630~678。
SKIP = [(870, 26, 1000, 66), (870, 626, 1000, 682)]

# 页内自带动态读数（页内时钟控件）：页号 → 额外跳过矩形。
PAGE_SKIP = {
    3: [(610, 258, 700, 300)],
}


def skipped(x, y, page=None):
    for sx, sy, sw, sh in SKIP:
        if sx <= x < sx + sw and sy <= y < sy + sh:
            return True
    for sx, sy, sw, sh in PAGE_SKIP.get(page, ()):
        if sx <= x < sx + sw and sy <= y < sy + sh:
            return True
    return False


def compare(a_path, b_path, page=None):
    a = Image.open(a_path).convert('RGB')
    b = Image.open(b_path).convert('RGB')
    if a.size != b.size:
        return None, '尺寸不一致 %s vs %s' % (a.size, b.size)
    pa, pb = a.load(), b.load()
    w, h = a.size
    diff = maxd = tot = 0
    xs, ys = [], []
    for y in range(h):
        for x in range(w):
            if skipped(x, y, page):
                continue
            tot += 1
            A, B = pa[x, y], pb[x, y]
            if A != B:
                diff += 1
                xs.append(x)
                ys.append(y)
                maxd = max(maxd, max(abs(A[0] - B[0]), abs(A[1] - B[1]),
                                     abs(A[2] - B[2])))
    bbox = ('x %d..%d y %d..%d' % (min(xs), max(xs), min(ys), max(ys))
            if diff else '-')
    return (diff, tot, maxd, bbox), None


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    root = sys.argv[1]
    args = sys.argv[2:]
    # 前缀可覆盖（默认 DIRECT/FULL）：同一工具也用于「两套构建的静态画面
    # 逐像素比对」，例如逐矩形上屏（RECT）vs 外接矩形上屏（BBOX）。
    prefix_a = 'DIRECT'
    prefix_b = 'FULL'
    pages = []
    i = 0
    while i < len(args):
        if args[i] == '--a-prefix':
            prefix_a = args[i + 1]
            i += 2
        elif args[i] == '--b-prefix':
            prefix_b = args[i + 1]
            i += 2
        else:
            pages.append(int(args[i]))
            i += 1
    pages = pages or [1, 2, 3, 4, 5]
    allok = True
    for p in pages:
        a_path = os.path.join(root, '%s_p%d.png' % (prefix_a, p))
        b_path = os.path.join(root, '%s_p%d.png' % (prefix_b, p))
        for path in (a_path, b_path):
            if not os.path.exists(path):
                raise SystemExit('缺少截图: %s' % path)
        res, err = compare(a_path, b_path, p)
        if err:
            print('page%-3d %s' % (p, err))
            allok = False
            continue
        diff, tot, maxd, bbox = res
        status = 'IDENTICAL' if diff == 0 else 'DIFFERS'
        if diff:
            allok = False
        print('page%-3d static: %-9s diffpx=%-5d / %d (%.4f%%) maxdelta=%-4d %s'
              % (p, status, diff, tot, 100.0 * diff / max(1, tot), maxd, bbox))
    print()
    print('ALL STATIC CONTENT IDENTICAL' if allok
          else 'STATIC DIFFERENCES REMAIN')
    return 0 if allok else 1


if __name__ == '__main__':
    sys.exit(main())
