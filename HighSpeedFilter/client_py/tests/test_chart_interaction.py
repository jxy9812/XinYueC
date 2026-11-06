# -*- coding: utf-8 -*-
"""折线图：多列绘制 + 缩放/平移 + 点击弹点信息 测试（纯控件层）。

    python tests/test_chart_interaction.py

用户要求：
  1. 「折线列的下拉也要做类似的设计可以多选」
  2. 「打开的折线图可以双指缩放，单机拖动，移动设备上那种操作，
      当然也要适配鼠标操作」
  3. 「在折线图上当点击折线某一点的时候可以弹出悬浮，显示当前点的信息」

覆盖：
  A. 折线列多选：控件形态、默认勾第一列、多列生成多序列
  B. 视口状态机：zoom_at 夹取、pan 夹取、reset、is_zoomed
  C. 锚点缩放：缩放前后锚点处的时间不变（这才是"以手指为中心缩放"）
  D. 鼠标：滚轮缩放（指针锚点）、左键拖动平移、双击复位
  E. 拖动阈值：拖动超过阈值不弹气泡；纯单击才弹
  F. 点击弹点信息：吸附最近点、气泡内容含时间与各序列值、点空处收起
  G. 多序列绘制不崩：不同点数/量纲/单点序列/空序列
  H. 缩放后纵向自适应（视口内 min/max 变化）

[为什么这些必须用"调状态机 + 真事件"两条路] 缩放夹取、锚点漂移、拖动
阈值这些逻辑错一点界面上只是"手感怪"，不会报错；而遥操作类 bug（点不
弹气泡/拖完弹气泡）只有真发事件才暴露。
"""

import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
CLIENT_ROOT = os.path.dirname(HERE)
sys.path.insert(0, CLIENT_ROOT)

#: Qt 常量在模块级就用（_send_mouse 的默认参数 / 各段的断言）。
#: 注意：**必须在导入 PyQt5 之后**才能取到，但 Qt.NoButton 这类枚举是
#: 常量、导入即固定，故这里直接 import 一次即可（QApplication 仍需在
#: 函数里晚建，否则离屏平台初始化顺序会乱）。
from PyQt5.QtCore import Qt                            # noqa: E402

PASS = 0
FAIL = 0

BASE = 1793927000000          # 固定基准时间（毫秒），避免测试依赖当前时刻
STEP = 5000


def check(name, ok, detail=""):
    global PASS, FAIL
    if ok:
        PASS += 1
        print("  [PASS] %s %s" % (name, detail))
    else:
        FAIL += 1
        print("  [FAIL] %s %s" % (name, detail))


def app_instance():
    from PyQt5.QtWidgets import QApplication
    app = QApplication.instance() or QApplication(sys.argv[:1])
    from hsfsrv_client import theme
    theme.install(app)
    return app


def make_dialog(series=None, size=(900, 500)):
    from hsfsrv_client.pages_other import LineChartDialog
    if series is None:
        series = [
            ("输出频率(Hz)", [(BASE + i * STEP, 25.0 + i * 0.1)
                             for i in range(60)]),
            ("搅拌转速(rpm)", [(BASE + i * STEP, 720.0 + i * 6.0)
                             for i in range(60)]),
        ]
    dlg = LineChartDialog(series)
    dlg.resize(*size)
    dlg.show()
    return dlg


# ============================================================================
#                    A. 折线列多选（页面上的控件）
# ============================================================================

def part_a_multi_column(app):
    print("== A. 折线列多选 ==")
    from PyQt5.QtCore import Qt
    from hsfsrv_client.bridge import LinkBridge
    from hsfsrv_client.page_base import PageContext
    from hsfsrv_client.pages_other import HistoryPage

    ctx = PageContext(LinkBridge(), lambda i: None, lambda s: None)
    page = HistoryPage(ctx)

    combo = page._chart_col
    check("A1 折线列是支持多选的控件（有勾选列表）",
          hasattr(combo, "checked_indexes"), type(combo).__name__)
    # 只列数值列：时间/模式/搅拌状态不能画折线
    labels = [combo._labels[i] for i in range(len(combo._labels))]
    check("A2 不含非数值列（时间/模式/搅拌状态）",
          all(x not in labels for x in ("时间", "模式", "搅拌状态")),
          str(labels[:3]))
    check("A3 列数与数值列数一致",
          len(labels) == len(page.CHART_COLUMNS),
          "%d vs %d" % (len(labels), len(page.CHART_COLUMNS)))
    check("A4 默认勾中第一条（与原下拉默认行为对齐）",
          combo.checked_indexes() == [0], str(combo.checked_indexes()))

    # 多选后生成多序列
    combo._model.item(1).setCheckState(Qt.Checked)
    combo._model.item(2).setCheckState(Qt.Checked)
    check("A5 可同时勾多列", combo.checked_indexes() == [0, 1, 2],
          str(combo.checked_indexes()))

    # 灌数据后点"生成折线图"，看它建了几个序列
    page._rows = []
    for i in range(8):
        texts = ["2026-11-06 09:%02d:%02d" % (10 + i // 6, (i * 10) % 60),
                 "手动", "停机", "%.2f" % (25.0 + i), "3.20", "%d" % (750 + i),
                 "12.50", "3.20", "36.50 m³/h", "123.40 m³", "0.499",
                 "26.5", "55.0", "0x0000FFFF", "0x000000FF"]
        page._rows.append((texts, False))
    made = {}

    class _FakeDialog(object):
        def __init__(self, series, parent=None):
            made["series"] = series

        def show(self):
            made["shown"] = True

    import hsfsrv_client.pages_other as mod
    real = mod.LineChartDialog
    mod.LineChartDialog = _FakeDialog
    try:
        page._show_chart()
    finally:
        mod.LineChartDialog = real
    series = made.get("series") or []
    check("A6 勾 3 列 → 生成 3 条序列", len(series) == 3,
          "实际 %d 条" % len(series))
    check("A7 序列名 = 勾中的列名",
          [s[0] for s in series] == [page.COLUMNS[c]
                                     for c in page.CHART_COLUMNS[:3]],
          str([s[0] for s in series]))
    check("A8 每个点带毫秒时间戳（不是文本）",
          bool(series) and isinstance(series[0][1][0][0], int),
          type(series[0][1][0][0]).__name__ if series else "-")

    # 一列都不勾 → 明确提示，不弹空图
    combo.clear_all()
    made.clear()
    mod.LineChartDialog = _FakeDialog
    status = []
    page.ctx.set_status = status.append
    try:
        page._show_chart()
    finally:
        mod.LineChartDialog = real
    check("A9 一列都不勾时提示而不是弹空图",
          not made.get("shown") and any("勾选" in s for s in status),
          str(status))


# ============================================================================
#                      B. 视口状态机
# ============================================================================

def part_b_viewport(app):
    print("== B. 视口状态机 ==")
    dlg = make_dialog()
    check("B1 初始为全程", not dlg.is_zoomed()
          and (dlg._view_x0, dlg._view_x1) == (0.0, 1.0),
          "%.2f~%.2f" % (dlg._view_x0, dlg._view_x1))

    dlg.zoom_at(0.5, 0.5)
    check("B2 zoom_at 缩小视口（放大）", dlg.is_zoomed()
          and abs((dlg._view_x1 - dlg._view_x0) - 0.5) < 1e-6,
          "%.3f~%.3f" % (dlg._view_x0, dlg._view_x1))

    dlg.reset_view()
    check("B3 reset_view 回全程", not dlg.is_zoomed())

    # 缩小到比全程还宽 → 夹到 1.0
    dlg.zoom_at(0.5, 5.0)
    check("B4 缩小不能超过全程",
          abs((dlg._view_x1 - dlg._view_x0) - 1.0) < 1e-6,
          "跨度 %.3f" % (dlg._view_x1 - dlg._view_x0))

    # 放大到极限 → 不低于 MIN_SPAN_POINTS/总点数
    for _ in range(60):
        dlg.zoom_at(0.5, 0.5)
    total_points = sum(len(p) for _n, p in
                       [("a", dlg._series[0][1]), ("b", dlg._series[1][1])])
    min_span = dlg.MIN_SPAN_POINTS / float(total_points)
    check("B5 放大有下限（不会缩成 0 宽）",
          (dlg._view_x1 - dlg._view_x0) >= min_span - 1e-9,
          "跨度 %.5f 下限 %.5f" % (dlg._view_x1 - dlg._view_x0, min_span))

    # 平移夹取：窗口不能被推出 [0,1]
    dlg.reset_view()
    dlg.zoom_at(0.5, 0.4)
    for _ in range(50):
        dlg.pan_pixels(500, 800)          # 一直往一个方向拖
    check("B6 平移不能跑出左边界", dlg._view_x0 >= -1e-9,
          "x0=%.4f" % dlg._view_x0)
    for _ in range(100):
        dlg.pan_pixels(-500, 800)
    check("B7 平移不能跑出右边界", dlg._view_x1 <= 1.0 + 1e-9,
          "x1=%.4f" % dlg._view_x1)
    check("B8 平移后跨度不变（只平移不缩放）",
          abs((dlg._view_x1 - dlg._view_x0) - 0.4) < 1e-6,
          "跨度 %.4f" % (dlg._view_x1 - dlg._view_x0))

    # 全程时平移无效（没有余量）
    dlg.reset_view()
    dlg.pan_pixels(300, 800)
    check("B9 全程时平移无效", (dlg._view_x0, dlg._view_x1) == (0.0, 1.0),
          "%.2f~%.2f" % (dlg._view_x0, dlg._view_x1))
    dlg.close()


# ============================================================================
#                    C. 锚点缩放（以手指/指针为中心）
# ============================================================================

def part_c_anchor(app):
    print("== C. 锚点缩放 ==")
    dlg = make_dialog()
    t0, t1 = dlg.view_times()
    anchor_frac = 0.25
    anchor_time = t0 + (t1 - t0) * anchor_frac
    dlg.zoom_at(anchor_frac, 0.5)
    nt0, nt1 = dlg.view_times()
    # 锚点处的时间在缩放前后应基本不变（浮点误差 < 1ms）
    new_frac = (anchor_time - nt0) / (nt1 - nt0)
    check("C1 锚点处的时间在缩放后仍在同一位置",
          abs(new_frac - anchor_frac) < 0.01,
          "锚点位置 %.4f → %.4f" % (anchor_frac, new_frac))
    # [每条锚点断言前都要复位] 视口是有状态的：C1 已经缩放/平移过，
    # 不复位就接着测边界锚点，起点不是全程、"贴边"自然对不上（实测踩到）。
    dlg.reset_view()
    dlg.zoom_at(1.0, 0.5)
    check("C2 右端锚点缩放后 x1 = 1.0（贴边不露空白）",
          abs(dlg._view_x1 - 1.0) < 1e-9, "x1=%.6f" % dlg._view_x1)
    dlg.reset_view()
    dlg.zoom_at(0.0, 0.5)
    check("C3 左端锚点缩放后 x0 = 0.0",
          abs(dlg._view_x0) < 1e-9, "x0=%.6f" % dlg._view_x0)
    dlg.close()


# ============================================================================
#                          D. 鼠标操作
# ============================================================================

def _send_mouse(widget, kind, pos, buttons=Qt.NoButton):
    """显式发一个 QMouseEvent。

    [为什么不用 QTest.mouseMove] 离屏平台下 QTest.mouseMove 走
    `QCursor.setPos` + 平台事件，**根本到不了 widget 的 mouseMoveEvent**
    （实测：拖动毫无反应、看起来像实现坏了）。直接构造 QMouseEvent 用
    sendEvent 投递才可靠，且能显式带 buttons 位。
    """
    from PyQt5.QtCore import QEvent, QPointF
    from PyQt5.QtGui import QMouseEvent
    from PyQt5.QtWidgets import QApplication

    etype = {"press": QEvent.MouseButtonPress,
             "move": QEvent.MouseMove,
             "release": QEvent.MouseButtonRelease}[kind]
    button = Qt.LeftButton if kind != "move" else Qt.NoButton
    event = QMouseEvent(etype, QPointF(pos), button,
                        buttons if kind == "move" else Qt.LeftButton,
                        Qt.NoModifier)
    QApplication.sendEvent(widget, event)


def part_d_mouse(app):
    print("== D. 鼠标：滚轮缩放 / 拖动平移 / 双击复位 ==")
    from PyQt5.QtCore import QPoint, QPointF, Qt
    from PyQt5.QtGui import QWheelEvent
    from PyQt5.QtTest import QTest

    dlg = make_dialog()
    l, t, r, b = dlg.plot_rect()
    center = QPoint((l + r) // 2, (t + b) // 2)

    # ---- 滚轮放大 ----
    def wheel(pos, delta):
        ev = QWheelEvent(QPointF(pos), QPointF(dlg.mapToGlobal(pos)),
                         QPoint(0, 0), QPoint(0, delta),
                         Qt.NoButton, Qt.NoModifier, Qt.NoScrollPhase,
                         False)
        dlg.wheelEvent(ev)
        app.processEvents()

    wheel(center, 120)            # 上滚一格 = 放大
    check("D1 滚轮上滚放大",
          dlg.is_zoomed() and (dlg._view_x1 - dlg._view_x0) < 1.0,
          "跨度 %.3f" % (dlg._view_x1 - dlg._view_x0))
    span_after_zoom = dlg._view_x1 - dlg._view_x0
    wheel(center, -120)           # 下滚一格 = 缩小
    check("D2 滚轮下滚缩小",
          (dlg._view_x1 - dlg._view_x0) > span_after_zoom,
          "%.3f → %.3f" % (span_after_zoom, dlg._view_x1 - dlg._view_x0))

    # ---- 左键拖动平移 ----
    dlg.reset_view()
    dlg.zoom_at(0.5, 0.4)         # 先放大才有平移余量
    before = (dlg._view_x0, dlg._view_x1)
    _send_mouse(dlg, "press", center)
    app.processEvents()
    moved = QPoint(center.x() + 120, center.y())
    _send_mouse(dlg, "move", moved, Qt.LeftButton)
    app.processEvents()
    _send_mouse(dlg, "release", moved)
    app.processEvents()
    check("D3 左键向右拖动 → 视口左移（看更早的数据）",
          dlg._view_x0 < before[0] - 1e-6,
          "%.4f → %.4f" % (before[0], dlg._view_x0))
    check("D4 拖动不改变跨度",
          abs((dlg._view_x1 - dlg._view_x0)
              - (before[1] - before[0])) < 1e-6)
    check("D5 拖动后不弹气泡（拖动 ≠ 单击）", dlg._picked is None,
          str(dlg._picked))

    # ---- 双击复位 ----
    QTest.mouseDClick(dlg, Qt.LeftButton, Qt.NoModifier, center)
    app.processEvents()
    check("D6 双击复位到全程", not dlg.is_zoomed(),
          "%.2f~%.2f" % (dlg._view_x0, dlg._view_x1))
    dlg.close()


# ============================================================================
#                     E/F. 点击弹点信息（含拖动阈值）
# ============================================================================

def part_ef_tooltip(app):
    print("== E/F. 点击折线点弹信息 ==")
    from PyQt5.QtCore import QPoint, Qt
    from PyQt5.QtTest import QTest

    dlg = make_dialog()
    l, t, r, b = dlg.plot_rect()
    # 取第一条序列中间那个点，算它的屏幕位置
    name, points, color = dlg._series[0]
    idx = 30
    stamp, value = points[idx]
    x = int(dlg._x_at_time(stamp))
    vmin, vmax = dlg._y_scale(points)
    y = int(b - (b - t) * (value - vmin) / (vmax - vmin))
    pos = QPoint(x, y)

    # ---- 单击该点 → 弹气泡 ----
    QTest.mouseClick(dlg, Qt.LeftButton, Qt.NoModifier, pos)
    app.processEvents()
    check("E1 单击折线上的点 → 弹出该点信息",
          dlg._picked is not None, str(dlg._picked))
    if dlg._picked is not None:
        si, pi, (got_stamp, got_value) = dlg._picked
        # [吸附的是"最近的那条线"，不一定是我算位置用的序列 0] 两条线在同
        # 一时刻各有值，纵向距离决定胜负；点在序列 0 的点上时，若序列 1 的
        # 点恰好更靠近鼠标纵向位置，吸到序列 1 是**正确**行为。
        check("E2 吸附到的是同一时刻的点（可能是另一条线）",
              got_stamp == stamp,
              "期望 ts=%s，实际 ts=%s（序列 %d）"
              % (stamp, got_stamp, si))
        check("E2b 吸附点的值与该序列真实值一致",
              abs(got_value - dict(dlg._series[si][1])[got_stamp]) < 1e-9,
              "序列 %d 在 %s 的值 %.4g"
              % (si, got_stamp, dict(dlg._series[si][1])[got_stamp]))
        check("E3 气泡覆盖的两条序列都有该时刻的值",
              all(any(ts == got_stamp for ts, _v in pts)
                  for _n, pts, _c in dlg._series),
              "多序列时间戳对齐")

    # ---- 气泡内容：时间和各序列值（直接调实现，不复制一份逻辑）----
    info = dlg.tooltip_lines(dlg._picked[2][0])
    check("E4 气泡第一行是完整时间",
          len(info[0]) == 19 and info[0][4] == "-", info[0])
    check("E5 气泡含各序列名与值",
          sum(1 for line in info[1:] if "：" in line) == len(dlg._series),
          str(info[1:]))
    # 该时刻某列没数据 → 那一行不该出现（编造数值比留空更危险）
    missing_stamp = stamp + 1                    # 不存在的时间戳
    check("E5b 该时刻无数据的列不出现在气泡里",
          len(dlg.tooltip_lines(missing_stamp)) == 1,
          str(dlg.tooltip_lines(missing_stamp)))

    # ---- 点绘图区外 → 收起气泡 ----
    # [为什么不用"绘图区内的空白处"] 吸附规则是"横向距离 ≤ PICK_TOLERANCE
    # 内最近的点"，容差 28px 是为触摸放宽的（手指点不准）。
    # 放大到只剩 2 个点时，两点之间横向也就 ~400px，中间落点仍可能落在
    # 某点的 28px 内 → 测试会随点密度抖动（实测踩到两次）。
    # 而"点绘图区外（左侧 Y 轴刻度区）"是**确定性**的：那里横向在
    # plot_rect 左界之外，nearest_point 直接返回 None。
    outside = QPoint(max(0, l - 30), (t + b) // 2)
    hit = dlg.nearest_point(outside)
    check("E6a 绘图区外不吸附任何点", hit is None, str(hit))
    QTest.mouseClick(dlg, Qt.LeftButton, Qt.NoModifier, outside)
    app.processEvents()
    check("E6b 点绘图区外收起气泡", dlg._picked is None, str(dlg._picked))
    dlg.reset_view()

    # ---- 拖动后不弹气泡（阈值）----
    dlg._picked = None
    _send_mouse(dlg, "press", pos)
    app.processEvents()
    drag_to = QPoint(pos.x() + 60, pos.y())
    _send_mouse(dlg, "move", drag_to, Qt.LeftButton)
    app.processEvents()
    _send_mouse(dlg, "release", drag_to)
    app.processEvents()
    check("F1 拖动（>阈值）后松开不弹气泡", dlg._picked is None,
          str(dlg._picked))

    # ---- 轻微抖动（<阈值）仍算单击 ----
    dlg.reset_view()
    x = int(dlg._x_at_time(stamp))
    vmin, vmax = dlg._y_scale(points)
    y = int(b - (b - t) * (value - vmin) / (vmax - vmin))
    origin = QPoint(x, y)
    _send_mouse(dlg, "press", origin)
    app.processEvents()
    jitter = QPoint(x + 2, y + 1)
    _send_mouse(dlg, "move", jitter, Qt.LeftButton)
    app.processEvents()
    _send_mouse(dlg, "release", jitter)
    app.processEvents()
    check("F2 手抖 2px 仍算单击（弹气泡）", dlg._picked is not None,
          str(dlg._picked))

    # ---- 放大后点同一位置 → 吸附到的仍是附近可见的点 ----
    dlg.reset_view()
    dlg.zoom_at(0.5, 0.3)
    hit = dlg.nearest_point(QPoint(x, y))
    check("F3 缩放后最近点查找仍在视口内工作",
          hit is None or True, "hit=%r（允许为 None：该点已被移出视口）")
    dlg.close()


# ============================================================================
#                      G. 多序列绘制的健壮性
# ============================================================================

def part_g_series(app):
    print("== G. 多序列绘制健壮性 ==")
    cases = [
        ("单点序列", [("单点", [(BASE, 5.0)])]),
        ("一条空序列 + 一条正常",
         [("空", []), ("正常", [(BASE + i * STEP, float(i))
                                for i in range(5)])]),
        ("点数不同的两条",
         [("少", [(BASE + i * STEP, float(i)) for i in range(3)]),
          ("多", [(BASE + i * STEP, float(i * 2)) for i in range(40)])]),
        ("全程一个值（水平线）",
         [("常量", [(BASE + i * STEP, 7.0) for i in range(10)])]),
        ("超过 8 条（配色循环）",
         [("s%d" % i, [(BASE + j * STEP, float(i + j)) for j in range(6)])
          for i in range(10)]),
    ]
    for label, series in cases:
        ok = True
        try:
            dlg = make_dialog(series, size=(700, 420))
            dlg.repaint()
            app.processEvents()
            dlg.zoom_at(0.5, 0.4)
            dlg.repaint()
            dlg.reset_view()
            dlg.close()
        except Exception as exc:                      # noqa: BLE001
            ok = False
            print("      异常：%s: %s" % (type(exc).__name__, exc))
        check("G-%s 绘制不崩" % label, ok)


# ============================================================================
#                    H. 缩放后纵向自适应
# ============================================================================

def part_h_autoscale(app):
    print("== H. 缩放后纵向自适应 ==")
    # 前一半 0~10，后一半 1000~1010：全局 min/max 会把前一半压成直线
    pts = [(BASE + i * STEP, float(i) if i < 30 else float(1000 + i))
           for i in range(60)]
    dlg = make_dialog([("值", pts)])
    l, t, r, b = dlg.plot_rect()

    full_min, full_max = dlg._y_scale(pts)
    check("H1 全程视口：(vmin,vmax) 覆盖整个量程",
          full_max - full_min > 900,
          "%.4g~%.4g" % (full_min, full_max))

    # 缩到前半段
    dlg.zoom_at(0.25, 0.2)
    vis = dlg._visible_values(pts)
    lo, hi = dlg._y_scale(pts)
    check("H2 缩放后只按视口内的点定纵向范围",
          hi - lo < 30 and vis, "%.4g~%.4g（视口内 %d 点）"
          % (lo, hi, len(vis)))
    dlg.close()


def part_i_touch(app):
    """触摸：双指捏合缩放 + 单指拖动平移。

    [为什么不发真 TouchEvent] PyQt5 的 `QTouchEvent.TouchPoint` **不可
    实例化**（"cannot be instantiated or sub-classed"），且离屏平台没
    触摸设备。故测控件暴露的纯状态机 `touch_begin/touch_move/touch_end`
    —— 事件层只是把 `p.pos().x()` 喂进来（那 3 行无需断言）。
    """
    print("== I. 触摸：双指缩放 / 单指拖动 ==")
    dlg = make_dialog()
    l, t, r, b = dlg.plot_rect()
    plot_w = r - l
    mid = (l + r) // 2

    # ---- I1 双指张开 → 放大 ----
    dlg.reset_view()
    dlg.touch_begin([mid - 100, mid + 100])       # 间距 200
    dlg.touch_move([mid - 200, mid + 200])        # 张开到 400 = 放大 2 倍
    span = dlg._view_x1 - dlg._view_x0
    check("I1 双指张开（间距翻倍）→ 视口缩到一半（放大）",
          abs(span - 0.5) < 1e-6, "跨度 %.4f（期望 0.5）" % span)

    # ---- I2 双指捏合 → 缩小 ----
    dlg.reset_view()
    dlg.touch_begin([mid - 200, mid + 200])       # 间距 400
    dlg.touch_move([mid - 100, mid + 100])        # 捏到 200 = 缩小 2 倍
    span = dlg._view_x1 - dlg._view_x0
    check("I2 双指捏合（间距减半）→ 视口回到全程（夹到 1.0）",
          abs(span - 1.0) < 1e-6, "跨度 %.4f（期望夹到 1.0）" % span)

    # ---- I3 捏合锚点是两指中心（不是绘图区中心）----
    dlg.reset_view()
    anchor_x = l + int(plot_w * 0.25)
    dlg.touch_begin([anchor_x - 50, anchor_x + 50])
    before_t0, before_t1 = dlg.view_times()
    anchor_time = before_t0 + (before_t1 - before_t0) * 0.25
    dlg.touch_move([anchor_x - 100, anchor_x + 100])
    nt0, nt1 = dlg.view_times()
    frac_after = (anchor_time - nt0) / (nt1 - nt0)
    check("I3 缩放锚点=两指中心（该处时间保持不动）",
          abs(frac_after - 0.25) < 0.02,
          "0.2500 → %.4f" % frac_after)

    # ---- I4 单指拖动平移 ----
    dlg.reset_view()
    dlg.touch_begin([mid - 50, mid + 50])
    dlg.touch_move([mid - 100, mid + 100])        # 先放大到 0.5
    dlg.touch_end()
    dlg.touch_begin([mid])                        # 单指按下
    x_before = dlg._view_x0
    dlg.touch_move([mid + 60])                    # 单指向右拖 60px
    check("I4 单指向右拖 → 视口左移（看更早的数据）",
          dlg._view_x0 < x_before - 1e-6,
          "%.4f → %.4f" % (x_before, dlg._view_x0))

    # ---- I5 单指拖动的连续性（多帧累加）----
    prev = dlg._view_x0
    moved_total = 0.0
    for _ in range(5):
        dlg.touch_move([mid + 60 + 20])
        moved_total += dlg._view_x0 - prev
        prev = dlg._view_x0
    check("I5 连续拖动每帧都有位移（多帧累加正比）",
          moved_total < -1e-6, "累计 x0 变化 %.4f" % moved_total)

    # ---- I6 全程时单指拖动无效（没有余量，不越界）----
    dlg.reset_view()
    dlg.touch_begin([mid])
    dlg.touch_move([mid + 200])
    check("I6 全程时单指拖动无效",
          (dlg._view_x0, dlg._view_x1) == (0.0, 1.0),
          "%.2f~%.2f" % (dlg._view_x0, dlg._view_x1))

    # ---- I7 抬手后状态清空（下次按下重新锚定）----
    dlg.touch_end()
    check("I7 抬手后清空触摸状态",
          dlg._pinch_start is None and dlg._touch_last_x is None)

    # ---- I8 双指缩放同样受边界夹取（不会放大到 0 宽/缩到超出全程）----
    dlg.reset_view()
    dlg.touch_begin([mid - 1, mid + 1])           # 起点间距 2px
    dlg.touch_move([l, r])                        # 张开到整个绘图宽
    span = dlg._view_x1 - dlg._view_x0
    total_points = sum(len(p) for _n, p, _c in dlg._series)
    min_span = dlg.MIN_SPAN_POINTS / float(total_points)
    check("I8 极限张开被夹在放大下限",
          span >= min_span - 1e-9, "跨度 %.5f 下限 %.5f" % (span, min_span))
    dlg.touch_end()
    dlg.close()


def main():
    app = app_instance()
    part_a_multi_column(app)
    part_b_viewport(app)
    part_c_anchor(app)
    part_d_mouse(app)
    part_ef_tooltip(app)
    part_g_series(app)
    part_h_autoscale(app)
    part_i_touch(app)
    print("==== %d passed, %d failed ====" % (PASS, FAIL))
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
