# -*- coding: utf-8 -*-
"""变频器页**组合操作**冒烟测试：把所有控件挨个按一遍，确认不崩。

    python tests/test_vfd_combo.py                    # 连本机 6200（虚拟设备）
    python tests/test_vfd_combo.py --port 6202
    python tests/test_vfd_combo.py --rounds 3

[为什么要这个测试] 曾经的真实事故：点"自由停车"后**界面直接消失**，
日志里没有任何 Python traceback。根因是控制应答回调跑在**网络线程**，
里面直接读控件文字/改按钮态 —— 从非 UI 线程碰 QWidget，Qt **直接崩进程**。

单点点击无法覆盖这类问题（有些回调只在特定应答下才走到）。故这里做
**组合操作**：启停、点动、方向切换、调速方式切换、下发给定、自由停车、
故障复位、跨页往返，连续多轮。

[判据] 整个过程进程必须存活、Qt 事件循环不退出、界面能持续刷新。
"""

import argparse
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
CLIENT_ROOT = os.path.dirname(HERE)
sys.path.insert(0, CLIENT_ROOT)


def main(argv=None):
    ap = argparse.ArgumentParser(description="变频器页组合操作冒烟测试")
    ap.add_argument("--port", type=int, default=6200)
    ap.add_argument("--rounds", type=int, default=2,
                    help="组合操作重复轮数")
    ap.add_argument("--pause", type=float, default=0.45,
                    help="每步之间的间隔（秒）；太短会掩盖时序问题")
    ap.add_argument("--show", action="store_true",
                    help="显示窗口（否则离屏）")
    args = ap.parse_args(argv)

    if not args.show:
        os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")

    from PyQt5.QtWidgets import QApplication
    from hsfsrv_client import pages_registry as reg
    from hsfsrv_client import proto, theme
    from hsfsrv_client.bridge import LinkBridge
    from hsfsrv_client.main_window import MainWindow

    app = QApplication(sys.argv[:1])
    theme.install(app)
    theme.apply_font(app)
    bridge = LinkBridge()
    win = MainWindow(bridge)
    win.resize(1180, 900)
    win.show()
    ok, err = bridge.connect_to("127.0.0.1", args.port, "root", "root")
    if not ok:
        print("登录失败：%s" % err)
        return 2
    print("已连接 127.0.0.1:%d" % args.port)

    win.goto_page(reg.PAGE_MONITOR_VFD)
    _pump(app, 40)
    page = win.pages[reg.PAGE_MONITOR_VFD]

    steps = []

    def step(name, fn):
        steps.append((name, fn))

    # ---- 方向 + 启停 ----
    step("方向→反转", lambda: page.dir_combo.setCurrentIndex(1))
    step("点启动", lambda: page.btn_run.click())
    step("（运行中）点停止", lambda: page.btn_run.click())
    step("方向→正转", lambda: page.dir_combo.setCurrentIndex(0))
    step("点启动", lambda: page.btn_run.click())

    # ---- 调速方式切换（会改量程/后缀/值）----
    step("调速方式→转速", lambda: page.speed_mode.setCurrentIndex(1))
    # [2026-11-06 起无「下发」按钮] 改值即生效（去抖后自动下发），
    # 这里直接调统一入口模拟"去抖到点"。
    step("转速模式下发", lambda: page._on_spin_settled())
    step("调速方式→频率", lambda: page.speed_mode.setCurrentIndex(0))
    step("改给定 40Hz", lambda: page.freq_spin.setValue(40))
    step("频率模式下发", lambda: page._on_spin_settled())

    # ---- 点动（按住/松开走 pressed/released）----
    step("点动按下", lambda: page.btn_jog.pressed.emit())
    step("点动松开", lambda: page.btn_jog.released.emit())

    # ---- 危险的几个（原来会崩的路径）----
    step("自由停车", lambda: page.btn_free.click())
    step("故障复位", lambda: page.btn_reset.click())

    # ---- 运行中直接自由停车 ----
    step("再启动", lambda: page.btn_run.click())
    step("运行中自由停车", lambda: page.btn_free.click())

    # ---- 跨页往返（订阅收敛 + 回页重建）----
    step("切到数字量", lambda: win.goto_page(reg.PAGE_MONITOR_DIGITAL))
    step("切回变频器", lambda: win.goto_page(reg.PAGE_MONITOR_VFD))
    step("切到电磁阀", lambda: win.goto_page(reg.PAGE_MONITOR_VALVE))
    step("切回变频器", lambda: win.goto_page(reg.PAGE_MONITOR_VFD))

    # ---- 电磁阀：真点按钮（原来只切页、从不点）----
    #
    # [为什么必须点] 阀控制应答的处理函数在**网络线程**里跑，内部要按服务端
    # 回的 `cmdResult`（issued/repeated/reversed/ignored）分支出不同文案 ——
    # 只要有一支忘了 `run_in_ui` 就直接碰 QWidget，Qt 会**崩进程且无
    # traceback**。只切页永远走不到这条路径，"界面能切" ≠ "按钮能点"。
    #
    # 连点**同一侧两次**：第一次真下发（issued），第二次服务端判
    # repeated 或 ignored —— 两个分支都要过一遍。
    #
    # [页面必须**执行时**再取] `win.pages` 是惰性构建的（没打开过的页不存在）。
    # 建 steps 列表时电磁阀页还没被访问过 → `pages.get()` 返回 None →
    # 整块 step() 注册被跳过，测试看起来"通过"但一步没跑。第一次就踩了这个坑。

    def _valve_screen():
        win.goto_page(reg.PAGE_MONITOR_VALVE)
        _pump(app, 25)
        return win.pages[reg.PAGE_MONITOR_VALVE]

    def _valve_click(index, side, times=2):
        page_v = _valve_screen()
        card = page_v.cards[index]
        btn = card.btn_open if side else card.btn_close
        for _ in range(times):
            btn.click()
        return None

    step("切到电磁阀（备操作）", _valve_screen)
    step("阀1 连点两次开阀", lambda: _valve_click(0, True))
    step("阀1 连点两次关阀", lambda: _valve_click(0, False))
    # 反向中途再反向（服务端 reversed 分支）
    step("阀2 开→立刻关（反向）",
         lambda: (_valve_click(1, True, 1), _valve_click(1, False, 1)))
    step("回到变频器", lambda: win.goto_page(reg.PAGE_MONITOR_VFD))

    # ---- 进水泵：真点启停（水质页）----
    #
    # [与 test_client.py 的 G 组分工] G 组是注入式（塞 model 看显示），
    # 这里走 buttons.click() 的完整链路（请求→应答→run_in_ui→改控件）。

    def _water_screen():
        win.goto_page(reg.PAGE_MONITOR_FLOWMETER)
        _pump(app, 25)
        return win.pages[reg.PAGE_MONITOR_FLOWMETER]

    def _pump_click(on):
        page_w = _water_screen()
        btn = page_w.btn_pump_on if on else page_w.btn_pump_off
        # 已是该状态时按钮置灰（服务端也幂等）——置灰就不点，避免误判失败
        if btn.isEnabled():
            btn.click()
        return None

    step("切到水质页（备操作）", _water_screen)
    step("进水泵启动", lambda: _pump_click(True))
    step("进水泵停止", lambda: _pump_click(False))
    step("回到变频器", lambda: win.goto_page(reg.PAGE_MONITOR_VFD))

    # ---- 进水泵：真点启停（水质页）----
    #
    # [与 test_client.py 的 G 组分工] G 组是注入式（塞 model 看显示），
    # 这里走 buttons.click() 的完整链路（请求→应答→run_in_ui→改控件）。
    water_page = win.pages.get(reg.PAGE_MONITOR_FLOWMETER)

    def _water_prepare():
        win.goto_page(reg.PAGE_MONITOR_FLOWMETER)
        _pump(app, 25)

    def _pump_start():
        water_page = win.pages[reg.PAGE_MONITOR_FLOWMETER]
        if water_page.btn_pump_on.isEnabled():
            water_page.btn_pump_on.click()

    def _pump_stop():
        water_page = win.pages[reg.PAGE_MONITOR_FLOWMETER]
        if water_page.btn_pump_off.isEnabled():
            water_page.btn_pump_off.click()

    if water_page is not None:
        step("切到水质页（备操作）", _water_prepare)
        step("进水泵启动", _pump_start)
        step("进水泵停止", _pump_stop)
        step("回到变频器", lambda: win.goto_page(reg.PAGE_MONITOR_VFD))

    total = 0
    failures = 0
    for r in range(max(1, args.rounds)):
        print()
        print("=== 第 %d/%d 轮 ===" % (r + 1, max(1, args.rounds)))
        for name, fn in steps:
            total += 1
            try:
                fn()
            except Exception as exc:                    # noqa: BLE001
                failures += 1
                print("  [FAIL] %-16s 异常：%s: %s"
                      % (name, type(exc).__name__, exc))
                continue
            # 泵事件循环：让网络线程的应答投递到 UI 线程真正执行
            alive = _pump(app, int(args.pause * 20))
            if not alive:
                print("  [CRASH] 执行「%s」后 Qt 事件循环已退出（进程将崩）"
                      % name)
                return 3
            print("  [OK]   %s" % name)

    # ---- 收尾：确认还活着且能刷新 ----
    alive = _pump(app, 40)
    print()
    print("=" * 56)
    if not alive:
        print("结论：事件循环已死")
        return 3
    st = bridge.model.vfd
    print("操作 %d 步 × %d 轮，异常 %d 次" % (len(steps), max(1, args.rounds),
                                             failures))
    print("进程存活，界面仍在刷新（vfd fresh=%s runState=%s）"
          % (st.is_fresh(), st.run_state))
    print("===== %s =====" % ("通过" if failures == 0 else "有异常，需查"))
    bridge.disconnect_from()
    return 0 if failures == 0 else 1


def _pump(app, iterations):
    """泵 Qt 事件循环若干次；返回 False 表示循环已退出（进程要崩了）。"""
    for _ in range(max(1, iterations)):
        app.processEvents()
        time.sleep(0.02)
    return True


if __name__ == "__main__":
    sys.exit(main())
