# -*- coding: utf-8 -*-
"""界面字号缩放的回归锁（板端触摸屏可读性）。

    python tests/test_ui_scale.py

[背景] 板端是 1280x800 的 7.9 寸屏（338x211mm），12px 字物理高度只有
约 1.6mm —— 站在触摸屏前看着明显偏小（用户 2026-11-06 反馈"字体有点小，
可以尝试字体都加大"）。界面字号原本是分散硬编码的 QSS 像素值（7 个文件
约 50 处），改法是把它们统一按 `theme.ui_scale()` 放大。

本测试锁四件事：
  A. 缩放比例本身：缺省 1.15、可用 HSF_UI_SCALE 覆盖、非法值回落
  B. 换算正确且不小于下限（再小就看不清）
  C. 主题样式表里**不存在漏改的原始字号**（12px 之类应已放大）
  D. 各页面源码里不再残留未缩放的像素字号

[为什么 C/D 要查"残留"] 这次是批量替换，最容易出的错是**漏掉几处** ——
局部字体没放大，界面看起来"有的字大有的字小"。用"不许出现原始小字号"
做断言，比逐个比对像素更快也更稳。
"""

import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, ROOT)

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


#: 设计稿里的原始字号（这些数值不应再出现在已缩放的源码里）
DESIGN_PX = (11, 12, 13, 14, 15, 16, 17, 20, 24, 30)

#: 需要检查的文件（界面渲染相关）
UI_FILES = (
    "hsfsrv_client/theme.py",
    "hsfsrv_client/login.py",
    "hsfsrv_client/widgets.py",
    "hsfsrv_client/page_base.py",
    "hsfsrv_client/pages_monitor.py",
    "hsfsrv_client/pages_other.py",
    "hsfsrv_client/pages_valve.py",
    "hsfsrv_client/softkeyboard.py",
)


def _fresh_theme():
    """重载 theme 模块并返回它。

    [为什么不用 del sys.modules + from import] 那样拿到的仍是**包属性上
    缓存的旧模块对象**（`hsfsrv_client.theme` 属性没被清）；`ui_scale()`
    读的是旧对象的 `_UI_SCALE`，于是断言会"滞后一拍"（实测 A5 读到 1.15、
    A6 读到 1.25）。importlib.reload 才是真正重跑模块顶层代码。
    """
    import importlib
    from hsfsrv_client import theme as _t
    return importlib.reload(_t)


def part_a_scale():
    print("\n=== A. 缩放比例 ===")
    os.environ.pop("HSF_UI_SCALE", None)
    theme = _fresh_theme()

    check("A1 缺省 1.15", abs(theme.ui_scale() - 1.15) < 1e-6,
          theme.ui_scale())
    theme.set_ui_scale(1.3)
    check("A2 set_ui_scale 生效", abs(theme.ui_scale() - 1.3) < 1e-6,
          theme.ui_scale())
    theme.set_ui_scale(0.5)          # 低于下限
    check("A3 低于 1.0 被钳到 1.0", abs(theme.ui_scale() - 1.0) < 1e-6,
          theme.ui_scale())
    theme.set_ui_scale(9.0)          # 高于上限
    check("A4 高于 2.0 被钳到 2.0", abs(theme.ui_scale() - 2.0) < 1e-6,
          theme.ui_scale())

    # 环境变量路径
    # [必须先清缓存] 上面 A2~A4 调过 set_ui_scale，模块级 _UI_SCALE 已被
    # 写成具体值；只删 sys.modules 拿到的**新模块** _UI_SCALE 是 None，
    # 但同一进程里若模块对象被复用就永远读到旧值。用 None 清除语义保证
    # 干净（这正是 set_ui_scale(None) 存在的理由）。
    theme.set_ui_scale(None)
    os.environ["HSF_UI_SCALE"] = "1.25"
    theme = _fresh_theme()          # 重跑模块顶层 → _UI_SCALE 回到 None
    check("A5 HSF_UI_SCALE=1.25 生效", abs(theme.ui_scale() - 1.25) < 1e-6,
          theme.ui_scale())

    # 换一个环境变量值：必须再清一次缓存才读新值
    os.environ["HSF_UI_SCALE"] = "abc"          # 非法
    theme = _fresh_theme()
    check("A6 非法值回落 1.15", abs(theme.ui_scale() - 1.15) < 1e-6,
          theme.ui_scale())

    # 越界值也回落缺省
    os.environ["HSF_UI_SCALE"] = "5.0"
    theme = _fresh_theme()
    check("A7 越界值回落 1.15", abs(theme.ui_scale() - 1.15) < 1e-6,
          theme.ui_scale())

    theme.set_ui_scale(None)
    os.environ.pop("HSF_UI_SCALE", None)
    theme.set_ui_scale(None)                    # 回到缺省


def part_b_convert():
    print("\n=== B. 换算 ===")
    for m in list(sys.modules):
        if m.startswith("hsfsrv_client.theme"):
            del sys.modules[m]
    from hsfsrv_client import theme
    theme.set_ui_scale(1.15)
    check("B1 12px → 14px", theme.font_px(12) == 14, theme.font_px(12))
    check("B2 30px → 34px", theme.font_px(30) == 34, theme.font_px(30))
    check("B3 下限生效（8px 不低于 10）", theme.font_px(8) == 10,
          theme.font_px(8))
    check("B4 返回整数", isinstance(theme.font_px(13), int))
    theme.set_ui_scale(1.0)
    check("B5 比例 1.0 时原样", theme.font_px(12) == 12, theme.font_px(12))
    theme.set_ui_scale(1.15)


def part_c_stylesheet():
    print("\n=== C. 主题样式表无残留小字号 ===")
    for m in list(sys.modules):
        if m.startswith("hsfsrv_client.theme"):
            del sys.modules[m]
    from hsfsrv_client import theme
    theme.set_ui_scale(1.15)
    qss = theme.STYLESHEET
    check("C1 无未替换的占位符", "%(" not in qss)
    found = [int(n) for n in re.findall(r"font-size:\s*(\d+)px", qss)]
    small = [n for n in found if n < 13]
    check("C2 样式表里没有小于 13px 的字号", not small, small or found)


def part_d_sources():
    print("\n=== D. 各页面源码无漏改字号 ===")
    # 每个文件允许的最大原始字号出现次数（0 = 必须全改完）
    offenders = []
    pat = re.compile(r"font-size:\s*(\d+)px")
    for rel in UI_FILES:
        path = os.path.join(ROOT, rel)
        if not os.path.isfile(path):
            offenders.append("%s 文件不存在" % rel)
            continue
        src = open(path, encoding="utf-8").read()
        for m in pat.finditer(src):
            # [跳过注释] 注释里出现的 `font-size: 12px` 只是举例，
            # 不是真实样式（theme.py 的说明段就有）。
            ls = src.rfind("\n", 0, m.start()) + 1
            line_text = src[ls:src.find("\n", m.start())]
            if line_text.lstrip().startswith("#"):
                continue
            px = int(m.group(1))
            # 缩放后 11→13, 12→14, 13→15, 14→16, 15→17, 16→18, 17→20,
            # 20→23, 24→28, 30→34；原始值里 14/16 与缩放结果重叠，
            # 故只把"确定未放大"的原始小值当问题。
            if px in (11, 12, 13):
                line = src[:m.start()].count("\n") + 1
                offenders.append("%s:%d 仍是 %dpx" % (rel, line, px))
    check("D1 无未放大的小字号（11/12/13px）", not offenders,
          "; ".join(offenders[:5]) if offenders else "")

    # 关键页面必须真的变大了：抽查最大字号
    pm_path = os.path.join(ROOT, "hsfsrv_client/pages_monitor.py")
    src = open(pm_path, encoding="utf-8").read()
    big = [int(n) for n in pat.findall(src)]
    check("D2 监视页保留大字号读数（≥30px）", any(n >= 30 for n in big),
          sorted(set(big), reverse=True)[:5])


def main():
    print("界面字号缩放回归")
    part_a_scale()
    part_b_convert()
    part_c_stylesheet()
    part_d_sources()
    print("\n==== %d passed, %d failed ====" % (PASS, FAIL))
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
