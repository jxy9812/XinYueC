#!/usr/bin/env python3
"""[2026-10-31] 扫雷崩溃偏移 → 函数名解析器。

用法: python tools/xgui_sweep_symbolize.py <map 文件> <偏移1> [偏移2 ...]
      或: python tools/xgui_sweep_symbolize.py <map 文件> --from-log <日志>

输入偏移为「模块内偏移」（探针崩溃处理器打印的 +0xXXXX，减去模块基址后
的 RVA）。map 的 VA 列已是绝对地址，减去镜像基址即 RVA。

产出：每个偏移落在哪个函数（最近的前一个符号），以及函数内偏移。
"""
import re
import sys

IMAGE_BASE = 0x140000000
LINE = re.compile(
    r'^\s+[0-9A-Fa-f]{4}:[0-9A-Fa-f]{8}\s+(\S+)\s+([0-9A-Fa-f]{16})\s+(\S+)\s+(\S+)\s*$'
)


def load_symbols(map_path):
    """读 map，返回按 VA 排序的 [(va, name)] 列表（剔除 '?' 与绝对符号）。"""
    syms = []
    with open(map_path, encoding='utf-8', errors='ignore') as fh:
        for line in fh:
            m = LINE.match(line)
            if not m:
                continue
            name = m.group(1)
            if name.startswith('?') or name.startswith('<') or name.startswith('$unwind'):
                continue
            try:
                va = int(m.group(2), 16)
            except ValueError:
                continue
            syms.append((va, name))
    syms = [(va - IMAGE_BASE, name) for va, name in syms]
    syms.sort()
    return syms


def resolve(syms, rva):
    """RVA → (函数名, 函数内偏移)。取 VA <= rva 的最近符号。"""
    lo, hi = 0, len(syms) - 1
    best = None
    while lo <= hi:
        mid = (lo + hi) // 2
        if syms[mid][0] <= rva:
            best = syms[mid]
            lo = mid + 1
        else:
            hi = mid - 1
    if not best:
        return None, 0
    return best[1], rva - best[0]


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 1
    map_path = sys.argv[1]
    syms = load_symbols(map_path)
    if not syms:
        print('map 中未找到符号: %s' % map_path)
        return 1
    for arg in sys.argv[2:]:
        if arg == '--from-log':
            continue
        try:
            off = int(arg, 16)
        except ValueError:
            print('跳过非法偏移: %s' % arg)
            continue
        name, delta = resolve(syms, off)
        print('+0x%05X -> %s+0x%X' % (off, name if name else '?', delta))
    return 0


if __name__ == '__main__':
    sys.exit(main())
