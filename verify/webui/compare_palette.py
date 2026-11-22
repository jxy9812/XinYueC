# -*- coding: utf-8 -*-
"""两端逐页**配色直方图**对比（客观，不靠肉眼）。

[用法] python verify/webui/compare_palette.py [页号...]
[前提] 先跑 verify/webui/ref_sweep.py（桌面端 16 页）与
        verify/webui/web_sweep_edge.py（网页端 16 页）。

[为什么] 50% 缩略图上"黑字"和"蓝字"分不清（抗锯齿 + 缩放混色）⇒ 之前
误判过两次。改为：把每页颜色**量化**（每通道 16 级）后取前 12 大色，比较
两端集合差 ⇒ 只看**差集**，差异一目了然。

用法：python .zcode/tmp/compare_palette.py [页号...]
"""
import os
import sys
from collections import Counter

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SHOTS = os.path.join(ROOT, "verify", "webui", "shots")

PAGES = {0: "主界面", 1: "主菜单", 2: "设备", 3: "数字量", 4: "模拟量",
         5: "搅拌泵", 6: "水质", 7: "电动球阀", 8: "数据", 9: "历史数据",
         10: "日志", 11: "报警", 12: "设置", 13: "本地设置", 14: "远端设置",
         15: "网络设置", 17: "相机", 18: "流量"}


def quant(c, step=16):
    return tuple((v // step) * step for v in c)


def palette(path, top=12):
    img = Image.open(path).convert("RGB")
    w, h = img.size
    counts = Counter()
    for y in range(0, h, 2):
        for x in range(0, w, 2):
            counts[quant(img.getpixel((x, y)))] += 1
    total = sum(counts.values())
    out = []
    for c, n in counts.most_common(top):
        out.append((c, 100.0 * n / total))
    return out, total


def main():
    pages = [int(a) for a in sys.argv[1:]] or list(PAGES)
    for i in pages:
        ref = os.path.join(SHOTS, "ref", "p%02d.png" % i)
        web = os.path.join(SHOTS, "web", "p%02d.png" % i)
        if not (os.path.exists(ref) and os.path.exists(web)):
            continue
        pr, _ = palette(ref)
        pw, _ = palette(web)
        rmap = {c: p for c, p in pr}
        wmap = {c: p for c, p in pw}
        only_r = [(c, rmap[c], wmap.get(c, 0.0)) for c in rmap
                  if c not in wmap and rmap[c] >= 1.0]
        only_w = [(c, rmap.get(c, 0.0), wmap[c]) for c in wmap
                  if c not in rmap and wmap[c] >= 1.0]
        print("=" * 66)
        print("### p%02d %s" % (i, PAGES.get(i, "")))
        if only_r:
            print("  仅桌面端有: %s"
                  % ", ".join("#%02X%02X%02X 桌面%.1f%%/网页%.1f%%"
                              % (c[0], c[1], c[2], a, b) for c, a, b in only_r))
        if only_w:
            print("  仅网页端有: %s"
                  % ", ".join("#%02X%02X%02X 桌面%.1f%%/网页%.1f%%"
                              % (c[0], c[1], c[2], a, b) for c, a, b in only_w))
        if not only_r and not only_w:
            print("  配色集合一致 ✓")


if __name__ == "__main__":
    main()
