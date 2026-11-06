# -*- coding: utf-8 -*-
"""搅拌泵页「拖动调速」滑块的回归锁（纯离屏，不连服务端）。

    python tests/test_vfd_slider.py

[为什么要有这条滑块] 现场是触摸屏、没有外接键盘：点 QSpinBox 的小箭头
一下一下点太慢，点数字区又会弹软键盘占掉半屏。用户 2026-11-06 要求
"按住滑动调速"。

本测试锁四件事（都是**行为契约**，不是实现细节）：
  A. 拖动过程中**不下发**（逐格下发会在总线上刷一串 FC06，泵/阀跟着抖）
  B. 手停下来（去抖窗口到点）只下发**一条**
  C. 松手立即下发一条，且不再叠加去抖那一条
  D. 滑块量程/取值跟随调速口径（Hz 0~50 ↔ rpm 0~1440）
"""

import os
import sys

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, ROOT)

from PyQt5.QtCore import QEventLoop, QTimer                # noqa: E402
from PyQt5.QtWidgets import QApplication                   # noqa: E402

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


class _Vfd(object):
    """变频器状态桩：运行中、固定设定频率。"""

    running = True
    faulted = False
    set_freq_hz = 25.0
    output_freq_hz = 24.8
    speed_rpm = 714.0
    bus_voltage_v = 540.0
    output_voltage_v = 220.0
    output_current_a = 3.2
    output_power_kw = 1.1
    torque_percent = 62.0

    def is_fresh(self):
        return True

    def has(self, _key):
        return True

    def run_state_text(self):
        return "运行中"

    def fault_text(self):
        return "无故障"


class _Bridge(object):
    def __init__(self):
        self.sent = []

    def send_vfd_control(self, doc, on_done=None, **_kw):
        self.sent.append(dict(doc))


class _Ctx(object):
    def __init__(self):
        self.model = type("M", (), {"vfd": _Vfd()})()
        self.bridge = _Bridge()

    def control_blocked_reason(self):
        return ""

    def set_status(self, _msg):
        pass

    def status_message(self, _msg):
        pass

    def request_snapshot(self, *_a, **_k):
        pass


def wait(ms):
    loop = QEventLoop()
    QTimer.singleShot(ms, loop.quit)
    loop.exec_()


def main():
    app = QApplication.instance() or QApplication(sys.argv[:1])
    from hsfsrv_client import pages_monitor as pm

    page = pm.VfdPage(_Ctx())
    page.resize(1180, 760)
    page.show()
    app.processEvents()
    app.processEvents()

    sent = page.ctx.bridge.sent
    delay = pm.VfdPage.SLIDER_SEND_DELAY_MS

    print("\n=== A. 拖动过程中不下发 ===")
    n0 = len(sent)
    for v in (28, 30, 32, 33):
        page.freq_slider.setValue(v)
        app.processEvents()
    check("A1 拖动中下发 0 条", len(sent) == n0, "实际 %d" % (len(sent) - n0))
    check("A2 数字框跟着滑块走", page.freq_spin.value() == 33,
          "spin=%d slider=%d" % (page.freq_spin.value(), page.freq_slider.value()))
    check("A3 读数标签实时更新", "33" in page._slider_value.text(),
          page._slider_value.text())

    print("\n=== B. 停手后只下发一条 ===")
    wait(delay + 120)
    got = len(sent) - n0
    check("B1 停手后下发 1 条", got == 1, "实际 %d" % got)
    # [2026-11-06 口径变更] 缺省调速方式改为**转速**，故下发 op=3
    # (OP_SET_SPEED) 并带 rpm；hz 是按额定点换算的伴随值。
    check("B2 下发的是最后停留的转速值",
          bool(sent) and abs(float(sent[-1].get("rpm", -1)) - 33.0) < 0.01,
          sent[-1] if sent else None)

    print("\n=== C. 松手立即下发（不叠加去抖那条）===")
    n1 = len(sent)
    page.freq_slider.setValue(35)
    app.processEvents()
    page.freq_slider.sliderReleased.emit()
    app.processEvents()
    check("C1 松手立即下发 1 条", len(sent) - n1 == 1,
          "实际 %d" % (len(sent) - n1))
    # 去抖定时器已被 stop，不该再补一条
    wait(delay + 120)
    check("C2 去抖不再补发", len(sent) - n1 == 1,
          "实际 %d" % (len(sent) - n1))

    print("\n=== D. 量程跟随调速口径（缺省＝转速）===")
    check("D1 缺省是转速调速",
          page.speed_mode.currentData() == pm.VfdPage.SPEED_MODE_RPM,
          "index=%d data=%s" % (page.speed_mode.currentIndex(),
                                page.speed_mode.currentData()))
    check("D2 缺省量程 0~1440 rpm",
          (page.freq_slider.minimum(), page.freq_slider.maximum()) == (0, 1440),
          (page.freq_slider.minimum(), page.freq_slider.maximum()))
    check("D3 缺省后缀是 rpm", "rpm" in page.freq_spin.suffix(),
          page.freq_spin.suffix())
    page.speed_mode.setCurrentIndex(0)      # 频率模式
    app.processEvents()
    check("D4 频率模式量程 0~50",
          (page.freq_slider.minimum(), page.freq_slider.maximum()) == (0, 50),
          (page.freq_slider.minimum(), page.freq_slider.maximum()))
    check("D5 频率模式后缀是 Hz", "Hz" in page.freq_spin.suffix(),
          page.freq_spin.suffix())
    page.speed_mode.setCurrentIndex(1)      # 切回转速
    app.processEvents()
    check("D6 切回转速量程复原",
          (page.freq_slider.minimum(), page.freq_slider.maximum()) == (0, 1440))


    print("\n=== E. 遥测回写不打断用户拖动 ===")
    page.freq_slider.setValue(40)
    app.processEvents()
    page.refresh()                          # 模拟 200ms 刷新（setFreqHz=25）
    app.processEvents()
    check("E1 去抖窗口内不被遥测覆盖", page.freq_slider.value() == 40,
          "slider=%d" % page.freq_slider.value())
    wait(delay + 120)
    page.refresh()                          # 去抖结束后才对齐实际值
    app.processEvents()
    # 当前是转速模式 ⇒ 遥测 25Hz 按额定点换算成 720rpm（50Hz↔1440rpm）
    check("E2 停手后对齐变频器实际设定（换算成转速）",
          page.freq_slider.value() == 720,
          "slider=%d（遥测 setFreqHz=25 应换算为 720rpm）"
          % page.freq_slider.value())

    print("\n=== F. 停机时也能拖动（预置给定）===")
    # [用户 2026-11-06]「不启动的时候也要能拖动调速」
    _Vfd.running = False                    # 让页面判定为停机
    page.refresh()
    app.processEvents()
    check("F1 停机时滑块仍可用", page.freq_slider.isEnabled())
    n2 = len(sent)
    page.freq_slider.setValue(900)
    app.processEvents()
    wait(delay + 120)
    check("F2 停机拖动不发在线给定", len(sent) == n2,
          "实际多发了 %d 条" % (len(sent) - n2))
    check("F3 预置值保留在滑块上", page.freq_slider.value() == 900,
          "slider=%d" % page.freq_slider.value())
    check("F4 预置标记已置位", page._preset_dirty is True)
    check("F5 提示说明是预置",
          "预置" in page._ctrl_hint.text(), page._ctrl_hint.text())

    # 启动时预置值随命令带下去
    n3 = len(sent)
    page._on_run_stop()
    app.processEvents()
    start_msgs = [d for d in sent[n3:] if d.get("op") == 4]   # OP_START
    check("F6 启动命令携带预置给定",
          bool(start_msgs) and abs(float(start_msgs[-1].get("rpm", -1)) - 900.0) < 0.01,
          start_msgs[-1] if start_msgs else None)
    check("F7 启动后预置标记清除", page._preset_dirty is False)
    _Vfd.running = True                     # 还原，避免影响后续

    print("\n==== %d passed, %d failed ====" % (PASS, FAIL))
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
