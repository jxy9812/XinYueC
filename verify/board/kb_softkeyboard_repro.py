# -*- coding: utf-8 -*-
"""板端软键盘取证：**验证「配置回显重排循环行 ⇒ 行被隐藏 ⇒ 焦点丢失 ⇒ 键盘自灭」**。

机制（从代码读出来的猜想）：
    ① 服务端配置回显到货 → `RemoteSettingsPage._populate` → `_populate_cycle`
    ② `_populate_cycle` 末尾无条件 `self._cycle_render_order()`
    ③ `_cycle_render_order` 里：
           layout = self._cycle_list_layout
           while layout.count():
               item = layout.takeAt(0)
               w = item.widget()
               if w is not None:
                   w.setParent(None)      # ← 摘下来！Qt 规定 setParent 会隐藏控件
    ④ 行控件里装着用户正在点的「定时 N 分钟」微调框 ⇒ 行被隐藏 ⇒
       **获得焦点的微调框被隐藏 ⇒ Qt 把焦点挪走**
    ⑤ 软键盘的 `eventFilter` 收到 FocusOut(target) → `_review_focus` →
       焦点已不在输入框 ⇒ `_close()` ⇒ **键盘当场消失**
    ⑥ 而「模式卡·搅拌电机转速」微调框**不在**被重挂的行里 ⇒ 不受影响 ⇒ 正常

v9 = 真实页面 + 真实触摸点中微调框（键盘弹出）→ 立刻喂一份配置回显
     → 看键盘是不是当场自灭。
"""

import os
import subprocess
import sys
import time
import traceback

sys.path.insert(0, "/root/Desktop/XinYueC/hsf_client_py")

from PyQt5.QtCore import QEvent, QObject, QPoint, QTimer                # noqa: E402
from PyQt5.QtWidgets import (QApplication, QLabel, QVBoxLayout,         # noqa: E402
                             QWidget)

from hsfsrv_client import proto                                        # noqa: E402
from hsfsrv_client.pages_other import RemoteSettingsPage               # noqa: E402
from hsfsrv_client.page_base import PageContext                        # noqa: E402
from hsfsrv_client import softkeyboard as sk                            # noqa: E402

OUT = open("/tmp/kb_softkeyboard_repro.log", "w", encoding="utf-8", buffering=1)


def log(*a):
    OUT.write(" ".join(str(x) for x in a) + "\n")
    OUT.flush()


class _Model(object):
    def is_auto(self):
        return False

    auto_running = False


class _Bridge(object):
    def __init__(self):
        self.model = _Model()

    def can(self, perm):
        return True

    def is_connected(self):
        return False

    def __getattr__(self, name):
        def _noop(*a, **k):
            return None
        return _noop


_orig_hide = sk.SoftKeyboard.hide
_orig_close = sk.SoftKeyboard._close


def p_hide(self):
    log(">>> SoftKeyboard.hide()   <<<<<< 面板被收起")
    try:
        traceback.print_stack(file=OUT)
    except Exception:                                                 # noqa: BLE001
        pass
    return _orig_hide(self)


def p_close(self):
    log(">>> SoftKeyboard._close()  <<<<<< 面板被关掉")
    try:
        traceback.print_stack(file=OUT)
    except Exception:                                                 # noqa: BLE001
        pass
    return _orig_close(self)


sk.SoftKeyboard.hide = p_hide
sk.SoftKeyboard._close = p_close

WATCH = {2: 1, 3: 1, 5: 1, 8: 1, 9: 1, 10: 1, 11: 1, 17: 1, 18: 1,
         194: 1, 195: 1, 196: 1}


class Tracer(QObject):
    def __init__(self, kb):
        super().__init__()
        self.kb = kb
        self.on = False

    def eventFilter(self, obj, ev):
        t = ev.type()
        if self.on and t in WATCH:
            try:
                cls = type(obj).__name__
                txt = ""
                if hasattr(obj, "text"):
                    s = obj.text()
                    txt = "[%s]" % s[:8] if isinstance(s, str) else ""
                g = ""
                if t in (2, 3, 5):
                    try:
                        gp = ev.globalPos()
                        g = " @(%d,%d)" % (gp.x(), gp.y())
                    except Exception:                                 # noqa: BLE001
                        g = " @(?)"
                log("   EV %-3d %-16s%-10s%s kbVis=%s"
                    % (t, cls, txt, g, self.kb.isVisible()))
            except Exception:                                         # noqa: BLE001
                pass
        return False


def tap(x, y, hold=150):
    subprocess.run(["python3", "/tmp/tap_inject.py", str(x), str(y),
                    str(hold)], capture_output=True, text=True)


def main():
    app = QApplication(sys.argv)
    win = QWidget()
    win.setWindowTitle("kb_repro9")
    win.showFullScreen()
    win.raise_()
    win.activateWindow()

    lay = QVBoxLayout(win)
    ctx = PageContext(_Bridge(), lambda *a: None, lambda *a: None)
    page = RemoteSettingsPage(ctx)
    lay.addWidget(page)

    kb = sk.attach_soft_keyboard(win)
    log("kb attached:", kb is not None)
    tracer = Tracer(kb)
    app.installEventFilter(tracer)

    sb = page.scroll.verticalScrollBar()

    def pump(n=40, dt=0.02):
        for _ in range(n):
            app.processEvents()
            time.sleep(dt)

    def scroll_to(w, want_y):
        for _ in range(12):
            app.processEvents()
            gy = w.mapToGlobal(QPoint(0, 0)).y()
            if abs(gy - want_y) <= 3:
                break
            sb.setValue(int(sb.value() + (gy - want_y)))
        app.processEvents()

    def target_spin():
        for mid, row in page._cycle_rows.items():
            if row["cond"].isVisible() and row["timer_min"].isEnabled():
                return mid, row["timer_min"]
        return None, None

    def step():
        mid, w = target_spin()
        if w is None:
            log("no target")
            return finish()
        scroll_to(w, 270)              # 面板区之外的位置（先排除几何因素）
        gp = w.mapToGlobal(QPoint(int(w.width() * 0.4), w.height() // 2))
        log("target=%s click=(%d,%d) panel=%s" % (mid, gp.x(), gp.y(),
                                                  kb.geometry().getRect()))
        tracer.on = True
        tap(gp.x(), gp.y(), 150)
        pump(35)
        fw = QApplication.focusWidget()
        log("after touch: kbVis=%s focus=%s target=%s"
            % (kb.isVisible(),
               type(fw).__name__ if fw else "None",
               type(kb._target).__name__ if kb._target else "None"))
        # ---- 关键动作：服务端配置回显到货（_populate → _populate_cycle）----
        log(">>> feed config echo (_populate_cycle) <<<")
        cycle_doc = {
            proto.CFG_AUTO_CYCLE_ENABLED: True,
            proto.CFG_AUTO_CYCLE_MODES: ["produce", "backwash", "rinse"],
            proto.CFG_AUTO_CYCLE_STAGE_CFG: {
                "produce": {proto.CFG_AUTO_STAGE_PRESS_ENABLED: True,
                            proto.CFG_AUTO_STAGE_PRESS_THRESHOLD: 0.3,
                            proto.CFG_AUTO_STAGE_PRESS_COMPARE: "gt",
                            proto.CFG_AUTO_STAGE_TIMER_ENABLED: False,
                            proto.CFG_AUTO_STAGE_TIMER_MINUTES: 10},
                "backwash": {proto.CFG_AUTO_STAGE_PRESS_ENABLED: False,
                             proto.CFG_AUTO_STAGE_PRESS_THRESHOLD: 0.9,
                             proto.CFG_AUTO_STAGE_PRESS_COMPARE: "gt",
                             proto.CFG_AUTO_STAGE_TIMER_ENABLED: True,
                             proto.CFG_AUTO_STAGE_TIMER_MINUTES: 10},
                "rinse": {proto.CFG_AUTO_STAGE_PRESS_ENABLED: False,
                          proto.CFG_AUTO_STAGE_PRESS_THRESHOLD: 0.5,
                          proto.CFG_AUTO_STAGE_PRESS_COMPARE: "gt",
                          proto.CFG_AUTO_STAGE_TIMER_ENABLED: True,
                          proto.CFG_AUTO_STAGE_TIMER_MINUTES: 10},
            },
        }
        page._populate_cycle(cycle_doc)
        pump(35)
        fw = QApplication.focusWidget()
        log("after config echo: kbVis=%s focus=%s target=%s"
            % (kb.isVisible(),
               type(fw).__name__ if fw else "None",
               type(kb._target).__name__ if kb._target else "None"))
        tracer.on = False
        finish()

    def finish():
        log("=== DONE ===")
        OUT.flush()
        OUT.close()
        os._exit(0)

    QTimer.singleShot(2500, step)
    app.exec_()


if __name__ == "__main__":
    main()
