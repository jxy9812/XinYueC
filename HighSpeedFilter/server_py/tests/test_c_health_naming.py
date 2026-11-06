# -*- coding: utf-8 -*-
"""C 版服务端健康表「浊度仪数量 / 命名」源码级回归锁。

    python server_py/tests/test_c_health_naming.py

[为什么是源码级而不是抓包] C 版服务端本机没有 485 设备（COM17/COM18 都
打开失败 → 整路跳过），两条总线都不 ready 时健康表里连设备条目都不生成，
没法在本机用真链路验证"恰好 2 台浊度仪"。但用户反馈的 bug 是**代码里
对每侧都硬循环 HSF_TURBIDITY_COUNT 次**造成的：
  「浊度仪怎么多了两个我总共就两个」
  「去掉什么ab侧我总共就两个都是一个485总线与流量计都是一个485总线」
  「变频器与温湿度计是另一条485总线」

所以这里静态检查 hsfHealthCollect 的结构，把三条事实钉住：
  1. 设备名不带 -A/-B（侧标只属于总线）
  2. 浊度仪循环受"该侧是否挂浊度仪"约束（A 侧不挂 ⇒ 不出占位条目）
  3. 配置里 A=0 / B=1，且总线拓扑注释与现场接法一致
真机连上 485 后的运行时验证仍需在现场做（Python 侧另有无头测试）。
"""

import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
# tests/ → server_py/ → HighSpeedFilter/ → XinYueC/
HSF_ROOT = os.path.dirname(os.path.dirname(HERE))
APP_C = os.path.join(HSF_ROOT, "server", "HsfServerApp.c")
CFG_H = os.path.join(HSF_ROOT, "server", "HsfServerConfig.h")

PASS = 0
FAIL = 0


def check(name, ok, detail=""):
    global PASS, FAIL
    if ok:
        PASS += 1
        print("  [PASS] %s %s" % (name, detail))
    else:
        FAIL += 1
        print("  [FAIL] %s %s" % (name, detail))


def read(path):
    with open(path, encoding="utf-8", errors="replace") as fh:
        return fh.read()


def health_block(src):
    """截出 hsfHealthCollect 函数体（到下一个顶层函数为止）。"""
    start = src.find("static void hsfHealthCollect")
    if start < 0:
        return ""
    # 找函数开括号后的配对收括号
    brace = src.find("{", start)
    depth = 0
    for i in range(brace, len(src)):
        if src[i] == "{":
            depth += 1
        elif src[i] == "}":
            depth -= 1
            if depth == 0:
                return src[start:i + 1]
    return src[start:]


def main():
    print("== C 版健康表：浊度仪数量 / 命名 ==")
    app = read(APP_C)
    cfg = read(CFG_H)
    block = health_block(app)
    check("A0 找到 hsfHealthCollect 函数体", bool(block),
          "%d 字符" % len(block))

    # ---- A. 设备名不带侧标 ----
    # 允许 "485-%s 总线" 带侧标（那是两条真实物理总线）；
    # 设备类（变频器/温湿度/流量计/浊度仪）不许出现 -%s 或 -A/-B。
    device_with_side = re.findall(
        r'snprintf\(name[^;]*"(变频器|温湿度|流量计|浊度仪\d*)-%s'
        r'|snprintf\(name[^;]*"(变频器|温湿度|流量计|浊度仪\d*)-[AB]"',
        block)
    check("A1 设备名不再拼 -A/-B 侧标", not device_with_side,
          str(device_with_side[:3]))
    # 总线名仍带侧标（排查接线要区分两条总线）
    check("A2 总线名仍保留 485-A/B 侧标",
          'snprintf(name, sizeof(name), "485-%s 总线", sideName)' in block)

    # 设备名是纯名字
    for name in ("变频器", "温湿度", "流量计"):
        check("A3 %s 条目用纯设备名" % name,
              'hsfHealthAdd(st, "%s"' % name in block)
    check("A4 浊度仪名字 = 浊度仪%d（无侧标）",
          'snprintf(name, sizeof(name), "浊度仪%d", t + 1)' in block)

    # ---- B. 只在该侧挂了浊度仪时才循环 ----
    check("B1 循环受 sideHasTurbidity 约束",
          "sideHasTurbidity && t < HSF_TURBIDITY_COUNT" in block,
          "for 条件里必须有该侧开关")
    check("B2 开关按侧取 A/B 配置宏",
          "HSF_BUS_A_HAS_TURBIDITY" in block
          and "HSF_BUS_B_HAS_TURBIDITY" in block)
    # 不应再有"未装配占位"的兜底（该侧不挂就整段不出条目）
    check("B3 没有悬空 goto / 无条件的每侧循环",
          "goto skipTurbidity" not in block
          and "for (t = 0; t < HSF_TURBIDITY_COUNT; ++t)" not in block)

    # ---- C. 配置与现场接法一致 ----
    def macro(name):
        m = re.search(r"#define\s+%s\s+(\d+)" % name, cfg)
        return int(m.group(1)) if m else None

    check("C1 A 侧不挂浊度仪", macro("HSF_BUS_A_HAS_TURBIDITY") == 0,
          str(macro("HSF_BUS_A_HAS_TURBIDITY")))
    check("C2 B 侧挂浊度仪（2 台）", macro("HSF_BUS_B_HAS_TURBIDITY") == 1,
          str(macro("HSF_BUS_B_HAS_TURBIDITY")))
    check("C3 全系统浊度仪台数 = 2",
          macro("HSF_TURBIDITY_COUNT") == 2,
          str(macro("HSF_TURBIDITY_COUNT")))
    check("C4 A 总线 = 变频器 + 温湿度",
          macro("HSF_BUS_A_HAS_VFD") == 1
          and macro("HSF_BUS_A_HAS_TEMPHUM") == 1
          and macro("HSF_BUS_A_HAS_FLOWMETER") == 0)
    check("C5 B 总线 = 流量计 + 浊度仪",
          macro("HSF_BUS_B_HAS_FLOWMETER") == 1
          and macro("HSF_BUS_B_HAS_VFD") == 0
          and macro("HSF_BUS_B_HAS_TEMPHUM") == 0)
    print("==== %d passed, %d failed ====" % (PASS, FAIL))
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
