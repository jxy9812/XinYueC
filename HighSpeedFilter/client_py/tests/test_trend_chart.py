# -*- coding: utf-8 -*-
"""监视页实时趋势图回归锁（纯离屏，不连服务端）。

    python tests/test_trend_chart.py

任务口径（对齐 C 端 HsfMonitorVfd.c 区 5 / HsfMonitorFlowMeter.c 趋势图）：
  A. 60 点滚动窗：新遥测逐拍追加、超窗移除最旧；跟随态窗口右缘贴最新点
  B. 多序列：VFD 3 序列各自独立滚动，互不挤点
  C. 滚轮缩放 X 轴：指针锚点（锚点处时刻不变）、上/下限钳制
  D. 视口接管停写：拖动/缩放后窗口不再自动右移，**追点继续**
  E. 跟随恢复：「跟随」恢复右移；右缘推回实时点（容差）自动交还
  F. 真事件路径：左键拖动 / 双击 / 悬停（离屏平台 QTest.mouseMove 到不了
     mouseMoveEvent，须显式构造 QMouseEvent 投递——见 test_chart_interaction
     的同款注释）
  G. 重绘纪律：追点/缩放/平移不增删控件、不改控件几何（update() 重绘，
     不整页重排）
  H. 页面接入：VfdPage / WaterPage 建了趋势图、refresh() 真的逐拍喂点、
     同拍判重、无数据不编点

[已知环境限制] 板/本机可能没有 PyQt5：本文件与仓库其它 UI 测试一样是
可执行脚本；没有 PyQt5 时由语法门（ast.parse）兜底（见交付说明）。
"""

import os
import sys

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")

HERE = os.path.dirname(os.path.abspath(__file__))
CLIENT_ROOT = os.path.dirname(HERE)
sys.path.insert(0, CLIENT_ROOT)

from PyQt5.QtCore import QPoint, QPointF, Qt                       # noqa: E402
from PyQt5.QtWidgets import QApplication, QPushButton              # noqa: E402

PASS = 0
FAIL = 0

#: 固定基准时刻（毫秒），测试不依赖当前时间（同 test_chart_interaction）。
BASE = 1793927000000
STEP = 500                     # 拍距 500ms


def check(name, ok, detail=""):
    global PASS, FAIL
    if ok:
        PASS += 1
        print("  [PASS] %s %s" % (name, detail))
    else:
        FAIL += 1
        print("  [FAIL] %s %s" % (name, detail))


def app_instance():
    app = QApplication.instance() or QApplication(sys.argv[:1])
    from hsfsrv_client import theme
    theme.install(app)
    return app


def make_chart(series=None):
    """建一个离屏可绘的趋势图（600x300，show 后几何才真实）。"""
    from hsfsrv_client.pages_monitor import TrendChart
    if series is None:
        series = (("out", "输出频率 Hz"), ("set", "给定频率 Hz"),
                  ("cur", "输出电流 A"))
    chart = TrendChart(series)
    chart.resize(600, 300)
    chart.show()
    return chart


def feed(chart, key, count, t0=BASE, step=STEP, v0=10.0, dv=0.1):
    """合成序列逐拍喂入；返回最后一个点的 (t, v)。"""
    last = (t0, v0)
    for i in range(count):
        last = (t0 + i * step, v0 + i * dv)
        chart.append(key, last[0], last[1])
    return last


def send_mouse(widget, kind, pos, buttons=Qt.NoButton):
    """显式投递 QMouseEvent（离屏下 QTest.mouseMove 不可靠，见文件头）。"""
    from PyQt5.QtCore import QEvent
    from PyQt5.QtGui import QMouseEvent
    from PyQt5.QtWidgets import QApplication as _QApp
    etype = {"press": QEvent.MouseButtonPress,
             "move": QEvent.MouseMove,
             "release": QEvent.MouseButtonRelease,
             "dclick": QEvent.MouseButtonDblClick}[kind]
    button = Qt.LeftButton if kind != "move" else Qt.NoButton
    event = QMouseEvent(etype, QPointF(pos), button,
                        buttons if kind == "move" else Qt.LeftButton,
                        Qt.NoModifier)
    _QApp.sendEvent(widget, event)


# ============================================================================
#              A. 60 点滚动窗 + 跟随态窗口
# ============================================================================

def part_a_window(app):
    print("== A. 60 点滚动窗 ==")
    chart = make_chart((("a", "A"),))
    check("A0 初始无窗口（还没数据）", chart.window_ms() is None
          and chart.is_following())

    # 数据未满窗：左缘钳在时间轴 0 点、窗宽不塌（对齐 C 端 vfdTrendFollowWindow）
    feed(chart, "a", 3)
    lo, hi = chart.window_ms()
    check("A1 未满窗时左缘钳到首点（不空滚）",
          lo == BASE and abs(hi - (BASE + chart.WIN_DEF_MS)) < 1e-6,
          "窗口 %s" % (chart.window_ms(),))

    # 喂满 70 点 → 只留最近 60 个（共 73 拍，最旧 13 拍滑出）
    feed(chart, "a", 70, t0=BASE + 3 * STEP)
    pts = chart.points("a")
    check("A2 窗内点数封顶 60", len(pts) == 60, "实际 %d" % len(pts))
    check("A3 最旧的点已滑出（首点 = 第 14 拍）",
          pts[0][0] == BASE + 13 * STEP, "首点 %d" % pts[0][0])
    check("A4 最新点在窗尾", pts[-1][0] == BASE + 72 * STEP,
          "尾点 %d" % pts[-1][0])

    # 跟随态：数据跨度超过窗宽后，右缘贴最新追点、左缘 = 右缘-窗宽
    lo, hi = chart.window_ms()
    newest = pts[-1][0]
    check("A5 跟随态右缘贴最新点", abs(hi - newest) < 1e-6,
          "hi=%s newest=%d" % (hi, newest))
    check("A6 跟随态窗宽 = WIN_DEF",
          abs((hi - lo) - chart.WIN_DEF_MS) < 1e-6,
          "跨度 %.1f" % (hi - lo))

    # 继续逐拍追加：窗口跟着右移（跟随），点数恒 60
    last = feed(chart, "a", 5, t0=pts[-1][0] + STEP)
    pts2 = chart.points("a")
    check("A7 追点后窗口右移", chart.window_ms()[1] == last[0],
          "右缘 %s 最新 %d" % (chart.window_ms()[1], last[0]))
    check("A8 滚动窗继续滑出（仍 60 点）", len(pts2) == 60,
          "实际 %d" % len(pts2))
    check("A9 仍是跟随态", chart.is_following())
    chart.close()


# ============================================================================
#                        B. 多序列
# ============================================================================

def part_b_series(app):
    print("== B. 多序列（VFD 3 序列）==")
    chart = make_chart()                       # out / set / cur 三序列
    feed(chart, "out", 70, v0=24.0)
    feed(chart, "set", 70, v0=25.0)
    feed(chart, "cur", 70, v0=3.0, dv=0.01)
    check("B1 三序列各自封顶 60 点",
          all(len(chart.points(k)) == 60 for k in ("out", "set", "cur")),
          str([len(chart.points(k)) for k in ("out", "set", "cur")]))
    # 一序列继续追点，不影响别的序列已保留的点
    out_first = chart.points("out")[0]
    feed(chart, "cur", 5, t0=BASE + 70 * STEP, v0=3.0, dv=0.01)
    check("B2 他序列追点不动本序列既有点",
          chart.points("out")[0] == out_first and len(chart.points("cur")) == 60)

    # 悬停能按序列分别命中（同一时刻三条序列重横坐标，纵向最近者优先）。
    # [取点口径] 窗宽 WIN_DEF(12s) 只盖最近 WIN_DEF/STEP(=24) 个点，
    # 取倒数第 5 个点保证在视口内（索引 30 在 500ms 拍距下已滑出窗口）。
    app.processEvents()
    t_mid = chart.points("out")[55][0]
    px = chart._x_at(t_mid)
    hit = chart.hover_at(QPoint(int(px), 150))
    check("B3 悬停命中且带序列名",
          hit is not None and hit[1] in ("输出频率 Hz", "给定频率 Hz",
                                         "输出电流 A"),
          str(hit))
    text = chart.hover_text(QPoint(int(px), 150))
    check("B4 悬停文本含数值与时刻",
          ("%.2f" % hit[3]) in text and "@" in text, text)
    chart.close()


# ============================================================================
#                  C. 滚轮缩放（锚点 / 上限 / 下限）
# ============================================================================

def _wheel(chart, app, pos, delta):
    from PyQt5.QtGui import QWheelEvent
    ev = QWheelEvent(QPointF(pos), QPointF(chart.mapToGlobal(pos)),
                     QPoint(0, 0), QPoint(0, delta),
                     Qt.NoButton, Qt.NoModifier, Qt.NoScrollPhase, False)
    chart.wheelEvent(ev)
    app.processEvents()


def part_c_zoom(app):
    print("== C. 滚轮缩放 X 轴 ==")
    chart = make_chart((("a", "A"),))
    feed(chart, "a", 70)
    l, t, r, b = chart.plot_rect()
    center = QPoint((l + r) // 2, (t + b) // 2)

    span0 = chart.window_ms()[1] - chart.window_ms()[0]
    anchor = chart.window_ms()[0] + span0 * 0.5
    _wheel(chart, app, center, 120)            # 上滚一格 = 放大
    lo, hi = chart.window_ms()
    check("C1 滚轮上滚放大（窗变窄）且接管视口",
          abs((hi - lo) - span0 * 0.8) < 1e-6 and not chart.is_following(),
          "跨度 %.0f→%.0f following=%s" % (span0, hi - lo,
                                           chart.is_following()))
    new_frac = (anchor - lo) / (hi - lo)
    check("C2 锚点处时刻不变（指针锚点缩放）", abs(new_frac - 0.5) < 1e-6,
          "锚点位置 %.6f" % new_frac)

    _wheel(chart, app, center, -120)           # 下滚一格 = 缩小
    lo2, hi2 = chart.window_ms()
    check("C3 滚轮下滚缩小（回到原窗宽）",
          abs((hi2 - lo2) - span0) < 1e-6, "跨度 %.0f" % (hi2 - lo2))

    for _ in range(60):                        # 一直放大 → 钳在 WIN_MIN
        chart.zoom_at(0.5, 0.5)
    lo3, hi3 = chart.window_ms()
    check("C4 放大有下限（WIN_MIN_MS）",
          abs((hi3 - lo3) - chart.WIN_MIN_MS) < 1e-6,
          "跨度 %.0f" % (hi3 - lo3))
    for _ in range(80):                        # 一直缩小 → 钳在 WIN_MAX
        chart.zoom_at(0.5, 2.0)
    lo4, hi4 = chart.window_ms()
    check("C5 缩小有上限（WIN_MAX_MS）",
          abs((hi4 - lo4) - chart.WIN_MAX_MS) < 1e-6,
          "跨度 %.0f" % (hi4 - lo4))
    chart.close()


# ============================================================================
#                D. 视口接管停写（追点继续、窗口不动）
# ============================================================================

def part_d_takeover(app):
    print("== D. 视口接管停写 ==")
    chart = make_chart((("a", "A"),))
    feed(chart, "a", 70)
    lo0, hi0 = chart.window_ms()

    # 缩放接管：窗口停写，追点继续
    chart.zoom_at(0.5, 0.5)
    check("D1 缩放后视口归用户（is_following False）",
          not chart.is_following())
    lo1, hi1 = chart.window_ms()
    feed(chart, "a", 4, t0=hi0 + STEP)
    check("D2 接管后追点窗口不动（停写）",
          chart.window_ms() == (lo1, hi1),
          "%s → %s" % ((lo1, hi1), chart.window_ms()))
    check("D3 接管后追点继续入窗（数据不停）",
          len(chart.points("a")) == 60
          and chart.points("a")[-1][0] == hi0 + 4 * STEP,
          "尾点 %d" % chart.points("a")[-1][0])

    # 平移接管：拖后窗口只动"拖的那一下"，随后的追点不再推窗
    chart.follow()
    lo2, hi2 = chart.window_ms()
    l, t, r, b = chart.plot_rect()
    plot_w = max(1, r - l)
    span = hi2 - lo2
    moved = chart.pan_pixels(80, plot_w)       # 右拖 = 看更早
    lo3, hi3 = chart.window_ms()
    check("D4 右拖平移 → 窗口左移（看更早）",
          moved and hi3 < hi2 - 1e-6, "%.0f → %.0f" % (hi2, hi3))
    check("D5 平移不改变窗宽", abs((hi3 - lo3) - span) < 1e-6,
          "跨度 %.0f" % (hi3 - lo3))
    check("D6 平移接管（停写）", not chart.is_following())
    feed(chart, "a", 3, t0=hi2 + STEP)
    check("D7 平移接管后追点窗口不动",
          chart.window_ms() == (lo3, hi3),
          "%s → %s" % ((lo3, hi3), chart.window_ms()))

    # 拖回最右（右缘推回实时点，容差 CATCHUP_MS）→ 自动交还跟随
    # [方向] 负 dx = 窗口右移（追向最新点）；略拖过头靠右缘钳位停在最新点。
    newest = chart.points("a")[-1][0]
    dx = (chart.window_ms()[1] - newest) / span * plot_w
    chart.pan_pixels(dx - 1.0, plot_w)
    check("D8 右缘推回实时点 → 自动恢复跟随", chart.is_following(),
          "窗口 %s 最新 %d" % (chart.window_ms(), newest))
    chart.close()


# ============================================================================
#                        E. 跟随恢复
# ============================================================================

def part_e_follow(app):
    print("== E. 跟随恢复 ==")
    chart = make_chart((("a", "A"),))
    feed(chart, "a", 70)
    newest = chart.points("a")[-1][0]

    chart.zoom_at(0.5, 0.5)                    # 接管
    chart.follow()                             # 「跟随」按钮同款路径
    lo, hi = chart.window_ms()
    check("E1 follow() 恢复跟随且右缘贴最新",
          chart.is_following() and abs(hi - newest) < 1e-6,
          "following=%s hi=%s newest=%d" % (chart.is_following(), hi, newest))
    check("E2 follow() 窗宽回默认",
          abs((hi - lo) - chart.WIN_DEF_MS) < 1e-6, "跨度 %.0f" % (hi - lo))

    last = feed(chart, "a", 3, t0=newest + STEP)
    check("E3 恢复后追点窗口重新右移", chart.window_ms()[1] == last[0],
          "右缘 %s" % (chart.window_ms()[1],))

    # 双击恢复（触摸屏口径）
    chart.zoom_at(0.5, 0.5)
    check("E4 双击前已接管", not chart.is_following())
    l, t, r, b = chart.plot_rect()
    send_mouse(chart, "dclick", QPoint((l + r) // 2, (t + b) // 2))
    app.processEvents()
    check("E5 双击恢复跟随", chart.is_following())
    chart.close()


# ============================================================================
#                     F. 真事件路径（拖动 / 悬停）
# ============================================================================

def part_f_events(app):
    print("== F. 真事件：左键拖动 / 悬停 ==")
    chart = make_chart((("a", "A"),))
    feed(chart, "a", 70)
    app.processEvents()
    l, t, r, b = chart.plot_rect()
    center = QPoint((l + r) // 2, (t + b) // 2)

    lo0, hi0 = chart.window_ms()
    send_mouse(chart, "press", center, Qt.LeftButton)
    send_mouse(chart, "move", QPoint(center.x() + 120, center.y()),
               Qt.LeftButton)
    send_mouse(chart, "release", QPoint(center.x() + 120, center.y()))
    app.processEvents()
    lo1, hi1 = chart.window_ms()
    check("F1 左键拖动接管视口", not chart.is_following())
    check("F2 拖动平移了窗口（右拖看更早）", hi1 < hi0 - 1e-6,
          "%.0f → %.0f" % (hi0, hi1))
    check("F3 拖动不改变窗宽",
          abs((hi1 - lo1) - (hi0 - lo0)) < 1e-6, "跨度 %.0f" % (hi1 - lo1))

    # 悬停：move 到某个点附近 → hover_at 命中该点
    app.processEvents()
    pts = chart.points("a")
    t_mid = pts[-8][0]                         # 窗内肯定可见的靠后点
    px = chart._x_at(t_mid)
    vmin, vmax = chart._visible_scale(pts)
    py = b - (b - t) * (pts[-8][1] - vmin) / (vmax - vmin)
    hit = chart.hover_at(QPoint(int(px), int(py)))
    check("F4 悬停命中最近点", hit is not None and hit[2] == t_mid,
          str(hit))
    text = chart.hover_text(QPoint(int(px), int(py)))
    check("F5 悬停文本 = 名称 数值 @ 时刻",
          text.startswith("A ") and ("%.2f" % pts[-8][1]) in text
          and "@" in text, text)
    # [纵向不设限是设计口径] 悬停按横向吸附（触摸屏不用瞄准），绘图区
    # 纵向都算命中区；只有点击带外（上下各 16px 之外）才不命中。
    check("F6 悬停带外不命中", chart.hover_at(QPoint(center.x(), t - 20))
          is None)
    chart.close()


# ============================================================================
#              G. 重绘纪律（update()，不整页重排）
# ============================================================================

def part_g_repaint(app):
    print("== G. 重绘纪律 ==")
    chart = make_chart((("a", "A"),))
    chart.resize(560, 280)
    app.processEvents()
    children0 = len(chart.children())
    size0 = (chart.width(), chart.height())
    feed(chart, "a", 200)                      # 大量追点
    chart.zoom_at(0.5, 0.5)
    chart.pan_pixels(60, 500)
    app.processEvents()
    check("G1 追点/缩放/平移不增删控件",
          len(chart.children()) == children0,
          "%d → %d" % (children0, len(chart.children())))
    check("G2 控件几何不变（update() 重绘，不触发布局）",
          (chart.width(), chart.height()) == size0,
          "%s → %s" % (size0, (chart.width(), chart.height())))
    check("G3 追 200 点后窗内仍 60 点（deque 滚动窗）",
          len(chart.points("a")) == 60, "实际 %d" % len(chart.points("a")))
    chart.close()


# ============================================================================
#                H. 页面接入（VfdPage / WaterPage）
# ============================================================================

class _Vfd(object):
    """变频器遥测桩：updated_ms 可按拍推进（趋势追点判重靠它）。"""

    run_state = 3
    running = True
    faulted = False
    updated_ms = BASE
    set_freq_hz = 25.0
    output_freq_hz = 24.8
    output_current_a = 3.2
    output_power_kw = 1.1
    torque_percent = 62.0
    speed_rpm = 714.0
    bus_voltage_v = 540.0
    output_voltage_v = 220.0

    def is_fresh(self):
        return True

    def has(self, _key):
        return True

    def run_state_text(self):
        return "运行中"

    def fault_text(self):
        return "无故障"


class _Ctx(object):
    def __init__(self, model):
        self.model = model

    def control_blocked_reason(self):
        return ""

    def set_status(self, _msg):
        pass


def part_h_pages(app):
    print("== H. 页面接入 ==")
    from hsfsrv_client import pages_monitor as pm
    from hsfsrv_client.model import (_now_ms, FlowMeterState, PumpState,
                                     TurbidityState)
    from types import SimpleNamespace

    # ---- VfdPage：三序列逐拍喂点 + 同拍判重 + 无数据不追 ----
    vfd = _Vfd()
    page = pm.VfdPage(_Ctx(SimpleNamespace(vfd=vfd)))
    page.resize(1180, 760)
    page.show()
    app.processEvents()
    check("H1 VfdPage 建了趋势图且 3 条序列",
          isinstance(page._trend, pm.TrendChart)
          and [k for k, _n in pm.VfdPage.TREND_SERIES] == ["out", "set", "cur"],
          str(pm.VfdPage.TREND_SERIES))
    follow_btn = page.findChild(QPushButton, "btnTrendFollow")
    check("H2 页上有「跟随」按钮（btnTrendFollow）", follow_btn is not None)

    for i in range(3):                         # 三个不同的遥测拍
        vfd.updated_ms = BASE + i * 1000
        vfd.output_freq_hz = 24.0 + i
        page.refresh()
    app.processEvents()
    pts = page._trend.points("out")
    check("H3 refresh() 逐拍追点（3 拍 3 点）", len(pts) == 3,
          "实际 %d" % len(pts))
    check("H4 追的是输出频率值", abs(pts[-1][1] - 26.0) < 1e-9,
          str(pts[-1]))
    page.refresh()                             # 同一拍再刷：判重不重复追
    check("H5 同拍重复 refresh 不重复追点",
          len(page._trend.points("out")) == 3,
          "实际 %d" % len(page._trend.points("out")))

    vfd.updated_ms = BASE + 3 * 1000           # 新拍待刷新（下面 refresh 消费）
    page.refresh()
    page._trend.zoom_at(0.5, 0.5)              # 用户滚轮缩放 → 接管
    check("H6 缩放后视口接管（停写）", not page._trend.is_following())
    follow_btn.click()                         # 「跟随」按钮恢复（页级路径）
    check("H7 点「跟随」后恢复", page._trend.is_following())

    class _Stale(_Vfd):
        def is_fresh(self):
            return False

    vfd.__class__ = _Stale                     # 数据过期：不追点（不编平线）
    vfd.updated_ms = BASE + 9 * 1000
    page.refresh()
    check("H8 数据过期不追点", len(page._trend.points("out")) == 4,
          "实际 %d" % len(page._trend.points("out")))
    page.close()

    # ---- WaterPage：流量 / 浊度两路独立判重，离线浊度不编点 ----
    flow = FlowMeterState()
    turb = TurbidityState()
    pump = PumpState()
    page = pm.WaterPage(_Ctx(SimpleNamespace(flowmeter=flow, turbidity=turb,
                                             pump=pump)))
    page.resize(1180, 760)
    page.show()
    app.processEvents()
    check("H9 WaterPage 建了趋势图且 3 条序列",
          isinstance(page._trend, pm.TrendChart)
          and [k for k, _n in pm.WaterPage.TREND_SERIES]
          == ["flow", "turb1", "turb2"],
          str(pm.WaterPage.TREND_SERIES))

    now = _now_ms()                # 模型层单调毫秒钟；updated_ms 要贴近当前
                                   # （is_fresh 判据 now-updated < staleMs）
    flow.apply({"flow": 12.4, "valid": True})
    flow.updated_ms = now
    turb.apply({"count": 2, "v0_name": "原水", "v0_online": True,
                "v0_ntu": 1240,                 # 0.01 NTU 量化 → 12.40
                "v1_online": False, "v1_ntu": 0})
    turb.updated_ms = now
    pump.apply({"count": 1, "v0_name": "进水泵", "v0_on": False,
                "v0_ready": True})
    pump.updated_ms = now
    page.refresh()
    app.processEvents()
    check("H10 流量已追点", len(page._trend.points("flow")) == 1,
          "实际 %d" % len(page._trend.points("flow")))
    check("H11 浊度1 已追点（0.01 量化换回 12.40）",
          page._trend.points("turb1")
          and abs(page._trend.points("turb1")[0][1] - 12.4) < 1e-9,
          str(page._trend.points("turb1")))
    check("H12 浊度仪2 离线不编点", len(page._trend.points("turb2")) == 0,
          "实际 %d" % len(page._trend.points("turb2")))

    # 下一拍：只有流量计推进 → 流量追点、浊度判重不追（两路独立）
    flow.apply({"flow": 12.9, "valid": True})
    flow.updated_ms = now + 1000
    page.refresh()
    check("H13 流量第二拍追点", len(page._trend.points("flow")) == 2,
          "实际 %d" % len(page._trend.points("flow")))
    check("H14 浊度同拍判重不重复追",
          len(page._trend.points("turb1")) == 1,
          "实际 %d" % len(page._trend.points("turb1")))
    page.close()


def main():
    app = app_instance()
    part_a_window(app)
    part_b_series(app)
    part_c_zoom(app)
    part_d_takeover(app)
    part_e_follow(app)
    part_f_events(app)
    part_g_repaint(app)
    part_h_pages(app)
    print("\n==== %d passed, %d failed ====" % (PASS, FAIL))
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
