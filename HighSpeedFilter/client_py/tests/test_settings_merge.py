# -*- coding: utf-8 -*-
"""设置三页合并（2026-11-13 用户定案）的功能回归：**逐项「存在 + 生效」**。

    python tests/test_settings_merge.py            # 独立跑（自带汇总 + 退出码）

[背景] 用户口径：「本地设置页 + 悬浮窗设置页 + 窗口设置页 三页合并成一个
界面，三页内容全部搬进合并页，一个功能项不许丢」⇒ 合并后是 **16 页
（0..15）**，「本地设置」保留页号 13，页号 16/17 从注册表与导航树删除。

[锁什么] 用户口径「合并后要测试功能是否正常」——**不许只看控件在不在，
每个项都要验「点了真生效」**。本文件逐项断言：
  A 注册表/导航树：16 页、无 16/17 残骸；
  B 合并页结构：三个分区齐全 + 原三页的**每一个**控件都在（不许丢项）；
  C 「界面设置」区逐项生效（缩放改根字号 / 强制紧凑 / 全屏 / 刷新周期 /
    趋势刷新间隔 / 历史·日志每页条数）；
  D 「悬浮窗」区逐项生效（标题栏/状态栏/导航/时钟/温湿度真被藏起来，且
    落盘后重进页 + 启动还原仍保持）；
  E 「窗口与屏幕」区逐项生效（窗口尺寸记忆「改→退出→重开仍记住」/ 屏幕
    休眠区的策略往返与动作终态 / 平台不支持时置灰可解释）；
  F 邻页未受影响（远端设置 14 / 网络设置 15 页号与内容照旧）。

[隔离] 本文件会改本机设置（缩放/紧凑/全屏/显隐/尺寸/电源策略）⇒ 一律走
**临时 INI**（替换 `settings._store`），跑完还原函数引用；绝不碰用户真实
设置（Windows 注册表 / 板端 ~/.config）。

[熄屏说明] 「立即熄屏」在真桥上会**关掉操作者的显示器** ⇒ 动作终态用
**替身桥**验（与总回归 §22C 同款纪律）；真桥只做**策略写入-回读往返**
（不熄屏）。
"""

import os
import shutil
import sys

# [输出编码 2026-11-14] 中文 Windows 默认代码页 cp936 ⇒ stdout 被重定向
# 到文件时用 GBK ⇒ 打印「⇒」(U+21D2，**GBK 编不了**) 直接
# UnicodeEncodeError ⇒ **整套测试在打印断言时被打断**（实测退出码仍是 0，
# 看着像"跑完了"）。强制 UTF-8 + errors="replace"（写不出去也不崩）。
for _stream in (sys.stdout, sys.stderr):
    try:
        _stream.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError, OSError):
        pass
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
if ROOT not in sys.path:
    sys.path.insert(0, ROOT)

# ---------------------------------------------------------------------------
#                          自带断言工具（独立跑时用）
# ---------------------------------------------------------------------------
_PASS = 0
_FAIL = 0
_SKIP = 0


def _check(name, ok, detail=""):
    """纯上报（计数在 run() 的包装里做，避免重复计数）。"""
    if ok:
        print("  [PASS] %s%s" % (name, ("  — " + str(detail)) if detail else ""))
    else:
        print("  [FAIL] %s%s" % (name, ("  — " + str(detail)) if detail else ""))


def _section(title):
    print("\n== %s ==" % title)


def _skip(name, why):
    print("  [SKIP] %s  — %s" % (name, why))


# ---------------------------------------------------------------------------
#                            替身桥（电源能力）
# ---------------------------------------------------------------------------
class _StubBridge(object):
    """电源能力替身：按需返回成功/失败，驱动页面的动作终态分支。

    [字段] caps=电源能力位（1=仅熄屏控制；2=超时委托；4=系统挂起），
    policy=策略回读值，fail_*=对应动作返回失败。
    """

    def __init__(self, caps=1, policy=None):
        self.caps = caps
        self.policy = policy if policy is not None else {
            "screenOffMs": 0, "systemSuspendMs": 0, "wakeInputs": 0xFF}
        self.fail_screen_off = False
        self.fail_screen_on = False
        self.fail_policy = False
        self.policy_calls = []
        self.screen_off_calls = []
        self.error = ""

    def power_capabilities(self):
        return self.caps

    def power_is_screen_off(self):
        return bool(self.screen_off_calls) and self.screen_off_calls[-1]

    def power_set_screen_off(self, off):
        self.screen_off_calls.append(bool(off))
        if off and self.fail_screen_off:
            self.error = "平台拒绝熄屏"
            return False
        if (not off) and self.fail_screen_on:
            self.error = "平台拒绝唤醒"
            return False
        return True

    def power_get_policy(self):
        return dict(self.policy)

    def power_set_policy(self, screen_off_ms, system_suspend_ms, wake_inputs):
        if self.fail_policy:
            self.error = "平台拒绝设置策略"
            return False
        self.policy_calls.append((screen_off_ms, system_suspend_ms,
                                  wake_inputs))
        self.policy = {"screenOffMs": screen_off_ms,
                       "systemSuspendMs": system_suspend_ms,
                       "wakeInputs": wake_inputs}
        return True

    def power_request_permission(self):
        return True

    def last_error(self):
        return self.error


def _patch_bridge(stub, available=True):
    """把 `hsf_native` 的桥替身装上（返回还原函数）。"""
    from hsfsrv_client import hsf_native
    old_bridge = hsf_native.bridge
    old_available = hsf_native.available
    hsf_native.bridge = lambda: stub
    hsf_native.available = lambda: available

    def _restore():
        hsf_native.bridge = old_bridge
        hsf_native.available = old_available
    return _restore


# ---------------------------------------------------------------------------
#                                 主流程
# ---------------------------------------------------------------------------
def run(check=None, section=None, skip=None):
    """跑全部断言。

    :param check: 断言函数（不传则用本文件自带的）。
    :param section: 小节标题函数。
    :param skip: 跳过函数。
    :return: (passed, failed, skipped)
    """
    global _PASS, _FAIL, _SKIP
    _PASS = _FAIL = _SKIP = 0
    # [计数与上报分离] 外部（总回归）可以注入自己的 check/section/skip；
    # 本文件仍**自己**计数 ⇒ 返回值永远是本段的真实计数。
    inner_check = check or _check
    inner_section = section or _section
    inner_skip = skip or _skip

    def check(name, ok, detail=""):                 # noqa: F811
        global _PASS, _FAIL
        if ok:
            _PASS += 1
        else:
            _FAIL += 1
        inner_check(name, ok, detail)

    def section(title):                              # noqa: F811
        inner_section(title)

    def skip(name, why):                            # noqa: F811
        global _SKIP
        _SKIP += 1
        inner_skip(name, why)

    # [离屏] 必须在建 QApplication **之前**设好平台（Qt 只认启动时的值）。
    os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
    from PyQt5.QtWidgets import QApplication
    app = QApplication.instance()
    if app is None:
        app = QApplication(sys.argv[:1])

    from hsfsrv_client import pages_registry as reg
    from hsfsrv_client import settings as S
    from hsfsrv_client.bridge import LinkBridge
    from hsfsrv_client.main_window import MainWindow
    from hsfsrv_client.page_base import PageContext
    from hsfsrv_client.pages_other import (LocalSettingsPage,
                                           RemoteSettingsPage, NetworkPage,
                                           apply_overlay)

    # ---- 本机设置隔离（临时 INI；跑完还原）----
    tmpdir = tempfile.mkdtemp(prefix="hsf_settings_merge_")
    tmp_ini = os.path.join(tmpdir, "client.ini")
    from PyQt5.QtCore import QSettings as _QS
    real_store = S._store
    S._store = lambda: _QS(tmp_ini, _QS.IniFormat)

    statuses = []

    def _ctx():
        return PageContext(LinkBridge(), lambda i: None, statuses.append)

    try:
        _run_all(check, section, skip, app, reg, S, LocalSettingsPage,
                 RemoteSettingsPage, NetworkPage, apply_overlay, _ctx,
                 statuses, MainWindow, LinkBridge)
    finally:
        S._store = real_store
        shutil.rmtree(tmpdir, ignore_errors=True)
    return _PASS, _FAIL, _SKIP


def _run_all(check, section, skip, app, reg, S, LocalSettingsPage,
             RemoteSettingsPage, NetworkPage, apply_overlay, make_ctx,
             statuses, MainWindow, LinkBridge):
    # ==================================================================
    # A. 注册表 / 导航树：17 页（16 + 仅 Python 的 Web 设置页）
    # ==================================================================
    section("A. 注册表与导航树（合并后的页数与页号）")
    # [2026-11-16 用户定案] 网页服务从本地设置拆出「Web 设置」页（页号 16，
    # **仅 Python 客户端**、追加在末尾不重排）⇒ 17 页 / 16 个导航节点。
    # [2026-11-19 相机页] 设备组新增「相机」页（页号 17，两端都有）⇒ 18 页 /
    # 17 个导航节点。页号契约：只追加不重排。
    check("A1 页总数 = 18（16 + Web 设置(16) + 相机(17)）",
          reg.PAGE_COUNT == 18, "count=%d" % reg.PAGE_COUNT)
    check("A2 页名数量 = 页总数", len(reg.PAGE_NAMES) == reg.PAGE_COUNT)
    check("A3 页名里没有「悬浮窗设置」「窗口设置」",
          "悬浮窗设置" not in reg.PAGE_NAMES
          and "窗口设置" not in reg.PAGE_NAMES)
    check("A4 导航树 17 节点且无越界页号",
          len(reg.NAV_TREE) == 17
          and all(node[1] <= reg.PAGE_MONITOR_CAMERA for node in reg.NAV_TREE),
          "nodes=%d" % len(reg.NAV_TREE))
    check("A5 设置分组子菜单 = 本地/远端/网络/Web 四项",
          [e[1] for e in reg.SUBMENU_ENTRIES[reg.PAGE_SETTINGS_MENU]]
          == [reg.PAGE_SETTINGS_LOCAL, reg.PAGE_SETTINGS_REMOTE,
              reg.PAGE_SETTINGS_NETWORK, reg.PAGE_SETTINGS_WEB])
    check("A6 旧页号常量已从注册表删除",
          not hasattr(reg, "PAGE_SETTINGS_OVERLAY")
          and not hasattr(reg, "PAGE_SETTINGS_WINDOW"))
    check("A7 本地设置仍是页号 13（页号契约不变）",
          reg.PAGE_SETTINGS_LOCAL == 13)

    # ==================================================================
    # B. 合并页结构：三分区齐全 + 原三页每一项都在
    # ==================================================================
    section("B. 合并页结构（三分区齐全 + 一个功能项不许丢）")
    ctx = make_ctx()
    win = MainWindow(LinkBridge())
    win.resize(1280, 720)
    win.show()
    app.processEvents()
    win.goto_page(reg.PAGE_SETTINGS_LOCAL)
    app.processEvents()
    page = win.pages[reg.PAGE_SETTINGS_LOCAL]
    check("B1 合并页挂在页号 13 上",
          isinstance(page, LocalSettingsPage)
          and win.current == reg.PAGE_SETTINGS_LOCAL)

    from PyQt5.QtWidgets import QLabel
    titles = [lb.text() for lb in page.findChildren(QLabel)]
    for want in ("界面设置", "悬浮窗", "窗口与屏幕"):
        check("B2 分区标题「%s」在页面上" % want, want in titles,
              "标题集合=%s" % sorted(set(titles))[:8])

    iface_items = ("compact_check", "fullscreen_check", "scale_combo",
                   "refresh_spin", "trend_combo", "hist_page_combo",
                   "log_page_combo")
    overlay_items = ("show_title", "show_status", "show_nav", "show_clock",
                     "show_th")
    window_items = ("size_combo", "size_apply_btn", "power_state",
                    "power_blank_combo", "power_suspend_combo",
                    "power_wake_mouse", "power_wake_keyboard",
                    "power_wake_touch", "power_off_btn", "power_on_btn",
                    "power_save_btn", "power_perm_btn")
    missing = [n for n in iface_items + overlay_items + window_items
               if not hasattr(page, n)]
    check("B3 原三页的全部控件都在合并页上（无丢项）",
          not missing, "缺=%s" % missing)

    # ---- 原两页的类已删除（不许留残骸页）----
    from hsfsrv_client import pages_other as PO
    check("B4 旧页面类 OverlayPage/WindowPage 已删除",
          not hasattr(PO, "OverlayPage") and not hasattr(PO, "WindowPage"))
    check("B5 主窗建页表里没有 16/17 的分支",
          "PAGE_SETTINGS_OVERLAY" not in
          open(os.path.join(ROOT, "hsfsrv_client", "main_window.py"),
               encoding="utf-8").read())
    # [初值就位后再接线] 建页本身**不许写本机设置**（否则用户一进设置页，
    # 存档就被默认值覆盖 —— 2026-11-10 实锅）。
    check("B6 建页不写本机设置（初值就位后再接线）",
          S.get_int(S.K_UI_SCALE_PCT) == 0
          and S.get_bool(S.K_FORCE_COMPACT) is False
          and S.get_int(S.K_HIST_PAGE_SIZE) == 0
          and S.get_int(S.K_LOG_PAGE_SIZE) == 0
          and S.get_int(S.K_REFRESH_MS) == 0
          and S.get_int(S.K_TREND_MS) == 0,
          "scale=%s compact=%s hist=%s log=%s refresh=%s trend=%s"
          % (S.get_int(S.K_UI_SCALE_PCT), S.get_bool(S.K_FORCE_COMPACT),
             S.get_int(S.K_HIST_PAGE_SIZE), S.get_int(S.K_LOG_PAGE_SIZE),
             S.get_int(S.K_REFRESH_MS), S.get_int(S.K_TREND_MS)))

    # ==================================================================
    # C. 「界面设置」区：逐项生效
    # ==================================================================
    section("C. 界面设置区：逐项「存在 + 生效」")

    # ---- C1 界面缩放：改档 → 落盘 → 根字号实测变化 ----
    from hsfsrv_client import theme
    old_font = app.font()
    old_scale = theme.ui_scale()
    try:
        idx150 = page.scale_combo.findData(150)
        idx100 = page.scale_combo.findData(100)
        check("C1a 缩放下拉含 100%/150% 两档", idx150 >= 0 and idx100 >= 0,
              "100%% idx=%d / 150%% idx=%d" % (idx100, idx150))
        # [先拨到另一档] 初始档位可能正好是 150%（theme 缺省 1.5）⇒ 直接设
        # 同一个档位不会发信号，"改了没落盘"就成了假红。
        page.scale_combo.setCurrentIndex(idx100)
        page.scale_combo.setCurrentIndex(idx150)
        check("C1b 缩放档位落盘",
              S.get_int(S.K_UI_SCALE_PCT) == 150,
              "pct=%d" % S.get_int(S.K_UI_SCALE_PCT))
        # [启动路径] 比例 → theme → 全局字体（根字号）。真跑一遍启动同款逻辑，
        # 实测根字号（app 字体 pt）随档位变化。
        def _root_pt(pct):
            theme.set_ui_scale(pct / 100.0)
            theme.apply_font(app, size=int(round(10 * theme.ui_scale())))
            return app.font().pointSize()

        pt_saved = _root_pt(S.get_int(S.K_UI_SCALE_PCT))
        pt_base = _root_pt(100)
        check("C1c 根字号随缩放档位实测变化（150% 比 100% 大）",
              pt_saved > pt_base, "150%%=%s pt / 100%%=%s pt"
              % (pt_saved, pt_base))
    finally:
        theme.set_ui_scale(old_scale)
        app.setFont(old_font)

    # ---- C2 强制紧凑 ----
    before = win._compact
    page.compact_check.setChecked(not page.compact_check.isChecked())
    app.processEvents()
    forced = win._compact
    nav_hidden = not win.nav.isVisible()
    persisted = S.get_bool(S.K_FORCE_COMPACT)      # [还原前先取值]
    page.compact_check.setChecked(before)
    app.processEvents()
    check("C2 强制紧凑生效（导航收起 + 落盘）",
          forced is True and nav_hidden and persisted is True
          and win._compact is before,
          "compact=%s nav隐藏=%s 落盘=%s"
          % (forced, nav_hidden, persisted))

    # ---- C3 全屏 ----
    page.fullscreen_check.setChecked(True)
    app.processEvents()
    fs_on = win.isFullScreen()
    page.fullscreen_check.setChecked(False)
    app.processEvents()
    check("C3 全屏开关生效（开→全屏 / 关→还原）",
          fs_on and not win.isFullScreen()
          and S.get_bool(S.K_FULLSCREEN) is False,
          "全屏时=%s 关后=%s" % (fs_on, win.isFullScreen()))

    # ---- C4 界面刷新周期 → 主窗定时器 ----
    page.refresh_spin.setValue(700)
    app.processEvents()
    check("C4 刷新周期驱动主窗定时器 + 落盘",
          win._refresh_timer.interval() == 700
          and S.get_int(S.K_REFRESH_MS) == 700,
          "interval=%d 落盘=%d"
          % (win._refresh_timer.interval(), S.get_int(S.K_REFRESH_MS)))

    # ---- C5 趋势刷新间隔 → 落盘 + 新建趋势图真按此节拍 ----
    from hsfsrv_client.pages_monitor import TrendChart, trend_sample_ms_load
    tidx = page.trend_combo.findData(1000)
    page.trend_combo.setCurrentIndex(tidx)
    app.processEvents()
    trend_ok = (S.get_int(S.K_TREND_MS) == 1000
                and trend_sample_ms_load() == 1000)
    check("C5a 趋势刷新间隔落盘 + 与趋势卡同一键", trend_ok,
          "落盘=%d 读回=%d" % (S.get_int(S.K_TREND_MS), trend_sample_ms_load()))
    from hsfsrv_client.pages_monitor import VfdPage
    vfd = VfdPage(make_ctx())
    check("C5b 趋势刷新间隔真生效（新页趋势图按此节拍）",
          getattr(vfd._trend, "sample_ms", None) == 1000,
          "sample_ms=%s" % getattr(vfd._trend, "sample_ms", None))
    check("C5c 趋势档位与趋势图档位同源",
          [page.trend_combo.itemData(i)
           for i in range(page.trend_combo.count())]
          == list(TrendChart.SAMPLE_CHOICES))

    # ---- C6/C7 每页条数 → 落盘 + 两页分页真按此值 ----
    from hsfsrv_client.pages_other import HistoryPage, LogPage
    hidx = page.hist_page_combo.findData(50)
    page.hist_page_combo.setCurrentIndex(hidx)
    lidx = page.log_page_combo.findData(10)
    page.log_page_combo.setCurrentIndex(lidx)
    app.processEvents()
    hp = HistoryPage(make_ctx())
    lp = LogPage(make_ctx())
    check("C6 历史每页条数落盘且驱动历史页分页",
          S.get_int(S.K_HIST_PAGE_SIZE) == 50 and hp._page_size == 50,
          "落盘=%d 页大小=%d" % (S.get_int(S.K_HIST_PAGE_SIZE), hp._page_size))
    check("C7 日志每页条数落盘且驱动日志页分页",
          S.get_int(S.K_LOG_PAGE_SIZE) == 10 and lp._page_size == 10,
          "落盘=%d 页大小=%d" % (S.get_int(S.K_LOG_PAGE_SIZE), lp._page_size))

    # ==================================================================
    # D. 「悬浮窗」区：逐项生效（真藏起来 + 落盘后重进页/启动还原仍保持）
    # ==================================================================
    section("D. 悬浮窗区：显隐开关真把元素藏起来且刷新后保持")

    def _toggle(pg, name, want):
        """把开关拨到 want 并返回「元素是否真的藏了/露了」。"""
        box = getattr(pg, name)
        if box.isChecked() != want:
            box.setChecked(want)
        app.processEvents()

    # 标题栏
    _toggle(page, "show_title", False)
    title_hidden = not win.title_bar.isVisible()
    _toggle(page, "show_title", True)
    title_shown = win.title_bar.isVisible()
    check("D1 标题栏显隐生效（藏→露）",
          title_hidden and title_shown,
          "藏=%s 露=%s" % (title_hidden, title_shown))

    # 状态栏
    _toggle(page, "show_status", False)
    status_hidden = not win.status_bar.isVisible()
    _toggle(page, "show_status", True)
    check("D2 状态栏显隐生效（藏→露）",
          status_hidden and win.status_bar.isVisible(),
          "藏=%s 露=%s" % (status_hidden, win.status_bar.isVisible()))

    # 导航栏
    _toggle(page, "show_nav", False)
    nav_hidden2 = not win.nav.isVisible()
    _toggle(page, "show_nav", True)
    check("D3 导航栏显隐生效（藏→露）",
          nav_hidden2 and win.nav.isVisible(),
          "藏=%s 露=%s" % (nav_hidden2, win.nav.isVisible()))

    # 标题栏内的时钟 / 温湿度
    _toggle(page, "show_clock", False)
    clock_hidden = not win.title_bar._clock_box.isVisible()
    _toggle(page, "show_clock", True)
    _toggle(page, "show_th", False)
    th_hidden = not win.title_bar._th_box.isVisible()
    _toggle(page, "show_th", True)
    check("D4 标题栏时钟显隐生效（藏→露）",
          clock_hidden and win.title_bar._clock_box.isVisible(),
          "藏=%s 露=%s" % (clock_hidden,
                          win.title_bar._clock_box.isVisible()))
    check("D5 标题栏温湿度显隐生效（藏→露）",
          th_hidden and win.title_bar._th_box.isVisible(),
          "藏=%s 露=%s" % (th_hidden, win.title_bar._th_box.isVisible()))

    # ---- 落盘后「重进页」与「启动还原」都保持 ----
    page.show_nav.setChecked(False)
    page.show_title.setChecked(False)
    app.processEvents()
    check("D6 显隐开关落盘",
          S.get_bool(S.K_SHOW_NAV) is False
          and S.get_bool(S.K_SHOW_TITLE) is False)
    page2 = LocalSettingsPage(make_ctx())          # 重进页（重建实例）
    check("D7 重进页仍按存档开局（导航/标题栏仍未勾选）",
          page2.show_nav.isChecked() is False
          and page2.show_title.isChecked() is False,
          "nav=%s title=%s" % (page2.show_nav.isChecked(),
                               page2.show_title.isChecked()))
    apply_overlay(win, S)                          # 启动还原同一套判定
    app.processEvents()
    check("D8 启动还原后元素仍保持隐藏",
          not win.nav.isVisible() and not win.title_bar.isVisible(),
          "nav可见=%s title可见=%s" % (win.nav.isVisible(),
                                       win.title_bar.isVisible()))
    # 还原（后续用例要在全显状态下跑）
    page.show_nav.setChecked(True)
    page.show_title.setChecked(True)
    app.processEvents()
    apply_overlay(win, S)
    app.processEvents()

    # ==================================================================
    # E. 「窗口与屏幕」区：逐项生效
    # ==================================================================
    section("E. 窗口与屏幕区：尺寸记忆 + 屏幕休眠")

    # ---- E1/E2 窗口尺寸记忆（改 → 退出 → 重开仍记住）----
    # [为什么不用 findData] 下拉的数据是 Python 元组 (w, h) ⇒ QVariant 里
    # 不是可比较的值，findData 匹配不到 ⇒ 按 itemData 逐项比。
    widx = -1
    for _i in range(page.size_combo.count()):
        if tuple(page.size_combo.itemData(_i) or ()) == (1024, 600):
            widx = _i
            break
    check("E1a 尺寸下拉含 1024×600 档", widx >= 0, "idx=%d" % widx)
    page.size_combo.setCurrentIndex(widx)
    page.size_apply_btn.click()
    app.processEvents()
    got = (win.width(), win.height())
    # [高度口径] 窗口有最小尺寸约束（离屏下最小高 743px）⇒ 高度只会≥请求
    # 值；宽度是精确判据，存档值是精确判据。
    check("E1b 尺寸「应用」生效（窗口真被改 + 落盘）",
          got[0] == 1024 and got[1] >= 600
          and S.get_str(S.K_WIN_SIZE) == "1024x600",
          "size=%s（最小高=%s）落盘=%r"
          % (got, win.minimumSize().height(), S.get_str(S.K_WIN_SIZE)))
    win.close()                                   # 退出
    win_old = win                                 # 保留引用：避免 C++ 对象被
    del win                                       # 回收后页面包装变悬垂
    win2 = MainWindow(LinkBridge())
    win2.show()
    app.processEvents()
    saved_w, saved_h = S.win_size()
    win2.resize(saved_w, saved_h)                 # 镜像 __main__ 启动还原
    app.processEvents()
    check("E2 窗口尺寸记忆：退出→重开仍记住",
          (saved_w, saved_h) == (1024, 600)
          and win2.width() == 1024 and win2.height() >= 600,
          "读回=%s 新窗=%s" % ((saved_w, saved_h),
                              (win2.width(), win2.height())))

    # ---- E3 屏幕休眠区（替身桥：能力位=1）----
    from hsfsrv_client import hsf_native
    stub = _StubBridge(caps=1)
    restore = _patch_bridge(stub)
    try:
        page3 = LocalSettingsPage(make_ctx())
        page3.on_enter()
        app.processEvents()
        check("E3a 屏幕休眠区控件齐全且能力位=1 ⇒ 熄屏/唤醒可用",
              page3.power_off_btn.isEnabled()
              and page3.power_on_btn.isEnabled()
              and not page3.power_suspend_combo.isEnabled(),
              "熄屏可用=%s 挂起置灰=%s"
              % (page3.power_off_btn.isEnabled(),
                 not page3.power_suspend_combo.isEnabled()))
        bidx = page3.power_blank_combo.findData(5 * 60 * 1000)
        page3.power_blank_combo.setCurrentIndex(bidx)
        page3.power_wake_mouse.setChecked(True)
        page3.power_wake_keyboard.setChecked(False)
        page3.power_wake_touch.setChecked(False)
        statuses[:] = []
        page3._power_save()
        app.processEvents()
        check("E3b 保存策略真下发（值与勾选一致）",
              len(stub.policy_calls) == 1
              and stub.policy_calls[0][0] == 5 * 60 * 1000
              and stub.policy_calls[0][2] == hsf_native.INPUT_MOUSE,
              "calls=%s" % (stub.policy_calls,))
        check("E3c 保存后回读回显（以平台回读值为准）",
              page3.power_blank_combo.currentData() == 5 * 60 * 1000
              and page3.power_wake_mouse.isChecked()
              and not page3.power_wake_keyboard.isChecked(),
              "blank=%s mouse=%s"
              % (page3.power_blank_combo.currentData(),
                 page3.power_wake_mouse.isChecked()))
        check("E3d 保存成功是终态（主窗状态栏有话）",
              any("已保存" in s for s in statuses),
              str(statuses[-1:]))
        check("E3e 电源策略同时落盘（重启后能重新下发）",
              S.get_int(S.K_POWER_SCREEN_OFF_MS) == 5 * 60 * 1000
              and S.get_int(S.K_POWER_WAKE_INPUTS) == hsf_native.INPUT_MOUSE,
              "off=%s wake=%s" % (S.get_int(S.K_POWER_SCREEN_OFF_MS),
                                  S.get_int(S.K_POWER_WAKE_INPUTS)))
        # 熄屏/唤醒动作终态（替身桥：不会真关操作者的显示器）
        statuses[:] = []
        page3._power_screen_off()
        check("E3f 熄屏动作是终态（成功：状态行报已熄屏）",
              stub.screen_off_calls[-1] is True
              and any("已熄屏" in s for s in statuses),
              str(statuses[-1:]))
        stub.fail_screen_off = True
        statuses[:] = []
        page3._power_screen_off()
        check("E3g 熄屏失败是终态（状态行报错，不停在「正在…」）",
              any("熄屏失败" in s for s in statuses), str(statuses[-1:]))
        stub.fail_screen_off = False
    finally:
        restore()

    # ---- E4 平台不支持（能力位=0）⇒ 整区置灰 + 可解释文案 ----
    stub0 = _StubBridge(caps=0)
    restore0 = _patch_bridge(stub0)
    try:
        page4 = LocalSettingsPage(make_ctx())
        page4.on_enter()
        app.processEvents()
        check("E4 能力位=0 ⇒ 屏幕休眠区置灰且说明本平台不支持",
              not page4.power_off_btn.isEnabled()
              and not page4.power_save_btn.isEnabled()
              and "不支持" in page4.power_state.text(),
              page4.power_state.text())
    finally:
        restore0()

    # ---- E5 缺动态库 ⇒ 置灰 + 说明怎么编 ----
    stub5 = _StubBridge(caps=1)
    restore5 = _patch_bridge(stub5, available=False)
    try:
        page5 = LocalSettingsPage(make_ctx())
        page5.on_enter()
        app.processEvents()
        check("E5 缺动态库 ⇒ 屏幕休眠区置灰且给出编译口径",
              not page5.power_off_btn.isEnabled()
              and "动态库" in page5.power_state.text()
              and "build_hsf_bridge" in page5.power_state.text(),
              page5.power_state.text())
    finally:
        restore5()

    # ---- E6 真桥（若在位）：策略写入后回读一致（**不熄屏**，会关显示器）----
    if hsf_native.available():
        try:
            real = hsf_native.HsbBridge()
            caps = real.power_capabilities()
            before = real.power_get_policy() or {}
            ok = real.power_set_policy(int(before.get("screenOffMs") or 0),
                                       int(before.get("systemSuspendMs") or 0),
                                       int(before.get("wakeInputs") or 0xFF))
            after = real.power_get_policy() or {}
            check("E6 真桥策略写入后回读一致（能力位=%s）" % caps,
                  ok and after.get("screenOffMs") == before.get("screenOffMs")
                  and after.get("systemSuspendMs")
                  == before.get("systemSuspendMs"),
                  "before=%s after=%s" % (before, after))
        except Exception as exc:                   # noqa: BLE001
            check("E6 真桥策略往返", False,
                  "%s: %s" % (type(exc).__name__, exc))
    else:
        skip("E6 真桥策略往返（缺 hsb_bridge 动态库）",
             "未编译：%s" % hsf_native.library_path())

    # ==================================================================
    # F. 邻页未受影响
    # ==================================================================
    section("F. 邻页未受影响（远端设置 14 / 网络设置 15）")
    check("F1 页号未重排（本地 13 / 远端 14 / 网络 15）",
          (reg.PAGE_SETTINGS_LOCAL, reg.PAGE_SETTINGS_REMOTE,
           reg.PAGE_SETTINGS_NETWORK) == (13, 14, 15))
    rp = RemoteSettingsPage(make_ctx())
    check("F2 远端设置页照旧可构建（有运行模式区）",
          hasattr(rp, "_mode_label") and rp._mode_label is not None,
          "mode=%s" % getattr(rp._mode_label, "text", lambda: "?")())
    np_ = NetworkPage(make_ctx())
    check("F3 网络设置页照旧可构建（有 WiFi 区）",
          all(hasattr(np_, n) for n in ("wifi_adapter_combo", "wifi_scan_btn",
                                        "wifi_connect_btn")))
    check("F4 合并页没有吞掉邻页的页号（13/14/15 各建一页）",
          reg.PAGE_SETTINGS_REMOTE != reg.PAGE_SETTINGS_LOCAL
          and reg.PAGE_SETTINGS_NETWORK != reg.PAGE_SETTINGS_LOCAL)


def main():
    os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
    print("===== 设置三页合并回归（16 页 / 本地设置=13）=====")
    passed, failed, skipped = run()
    print("\n===== 汇总：PASS %d / FAIL %d / SKIP %d ====="
          % (passed, failed, skipped))
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
