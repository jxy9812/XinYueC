# -*- coding: utf-8 -*-
"""应用内软键盘：触摸屏无键盘设备（现场触摸屏电脑/板端触摸屏）的输入方案。

[键位表 1:1 移植自 C 端] Src/XGui/VirtualKeyboard/XVirtualKeyboard.c 的
内置静态映射表（对标 lv_keyboard.c 9.2.2）逐键移植：
    KB_MAP_TEXT_LOWER / TEXT_UPPER / SPECIAL / NUMBER
    ← s_kbMapTextLower / s_kbMapTextUpper / s_kbMapSpecial / s_kbMapNumber
模式名对齐 C 的 XKeyboardMode 枚举（TextLower/TextUpper/Special/Number）。
每行按键带 C 的宽度单位（ctrl 表），行内按单位归一化铺满行宽（同 C 的
xkb_rebuildLayout 口径）。

[为什么不用 QtVirtualKeyboard] PyQt5 未打包该模块、现场无外网装不了；
[为什么不用 Windows TabTip 做主键盘] 尺寸/弹出时机受系统控制，且板端
（Linux）没有。自绘键盘在桌面 Windows 与板端 X11 表现完全一致。

[中文输入] 本键盘只做 ASCII（字母/数字/符号）。C 框架的拼音输入法
（XPinyinEngine/XPinyinPhrase/XVirtualKeyboardPinyinInputMethod，含词典）
移植是后续专项；Windows 触摸屏过渡期点「中」键唤出系统触摸键盘
（TabTip，带拼音/手写），板端挂系统级输入法。数据采集由服务端完成，
客户端输入只出现在登录/筛选/路径——ASCII 覆盖绝大多数场景。

[弹出时机] 自动：任意 QLineEdit 获得焦点 → 底部弹出；焦点离开输入类
    控件 → 收起。TimePickerEdit 除外（单击弹自己的日期选择器，
    见 NO_SOFT_KBD 排除标记）。
[开关] HSF_TOUCH 环境变量：'1' 强制开 / '0' 强制关 / 未设 → 自动探测
    （Windows 查触摸数字化器，非 Windows 默认开——板端就是触摸屏）。
"""

import os
import sys

from PyQt5.QtCore import QEvent, Qt, QTimer
from PyQt5.QtWidgets import (QApplication, QHBoxLayout, QLineEdit,
                             QPushButton, QVBoxLayout, QWidget)

#: Windows 系统触摸键盘（带中文输入法）。
_TABTIP = r"C:\Program Files\Common Files\microsoft shared\ink\TabTip.exe"

#: 软键盘目标输入控件的排除标记：设了此 Qt 属性的输入控件不弹键盘
#: （如 TimePickerEdit——它单击弹的是自己的日期选择浮层）。
NO_SOFT_KBD = "_hsf_no_soft_kbd"


def touch_default_enabled():
    """触摸环境自动探测：无 HSF_TOUCH 环境变量时的缺省开关。"""
    env = os.environ.get("HSF_TOUCH")
    if env == "1":
        return True
    if env == "0":
        return False
    if sys.platform == "win32":
        try:
            import ctypes
            # SM_MAXIMUMTOUCHES = 99：>0 表示有触摸数字化器（触摸屏）。
            return ctypes.windll.user32.GetSystemMetrics(99) > 0
        except Exception:                               # noqa: BLE001
            return False
    return True                                         # 板端 Linux 触摸屏


# ============================================================================
#                    键位表（1:1 移植 XVirtualKeyboard.c 内置映射表）
# ============================================================================
# 条目 = (标签, 动作, 宽度单位)。动作：
#   ("t", 任意)  插入标签字符          "up"  切大写布局（ABC）
#   "num"        切数字布局/回文本（1#）  "low" 切小写布局（abc）
#   "back"       退格                  "nl"  换行（单行控件等价确认）
#   "dismiss"    收起面板               "left"/"right"  移动光标
#   "ok"         确认（收起面板）       "sign" 插入负号
# 宽度单位同 C 的 ctrl 表：行内归一化铺满行宽。
LBL_UPPER, LBL_LOWER = "ABC", "abc"
LBL_BACKSPACE, LBL_NEWLINE = "退格", "换行"
LBL_DISMISS, LBL_LEFT, LBL_RIGHT, LBL_OK = "收起", "←", "→", "确定"
LBL_1NUM, LBL_SIGN = "1#", "+/-"

KB_MAP_TEXT_LOWER = [
    [("1#", "num", 5),
     ("q", "t", 4), ("w", "t", 4), ("e", "t", 4), ("r", "t", 4),
     ("t", "t", 4), ("y", "t", 4), ("u", "t", 4), ("i", "t", 4),
     ("o", "t", 4), ("p", "t", 4),
     (LBL_BACKSPACE, "back", 7)],
    [(LBL_UPPER, "up", 6),
     ("a", "t", 3), ("s", "t", 3), ("d", "t", 3), ("f", "t", 3),
     ("g", "t", 3), ("h", "t", 3), ("j", "t", 3), ("k", "t", 3),
     ("l", "t", 3),
     (LBL_NEWLINE, "nl", 7)],
    [("_", "t", 1), ("-", "t", 1),
     ("z", "t", 1), ("x", "t", 1), ("c", "t", 1), ("v", "t", 1),
     ("b", "t", 1), ("n", "t", 1), ("m", "t", 1),
     (".", "t", 1), (",", "t", 1), (":", "t", 1)],
    [(LBL_DISMISS, "dismiss", 2), (LBL_LEFT, "left", 2),
     (" ", "t", 6), (LBL_RIGHT, "right", 2), (LBL_OK, "ok", 2)],
]

KB_MAP_TEXT_UPPER = [
    [("1#", "num", 5),
     ("Q", "t", 4), ("W", "t", 4), ("E", "t", 4), ("R", "t", 4),
     ("T", "t", 4), ("Y", "t", 4), ("U", "t", 4), ("I", "t", 4),
     ("O", "t", 4), ("P", "t", 4),
     (LBL_BACKSPACE, "back", 7)],
    [(LBL_LOWER, "low", 6),
     ("A", "t", 3), ("S", "t", 3), ("D", "t", 3), ("F", "t", 3),
     ("G", "t", 3), ("H", "t", 3), ("J", "t", 3), ("K", "t", 3),
     ("L", "t", 3),
     (LBL_NEWLINE, "nl", 7)],
    [("_", "t", 1), ("-", "t", 1),
     ("Z", "t", 1), ("X", "t", 1), ("C", "t", 1), ("V", "t", 1),
     ("B", "t", 1), ("N", "t", 1), ("M", "t", 1),
     (".", "t", 1), (",", "t", 1), (":", "t", 1)],
    [(LBL_DISMISS, "dismiss", 2), (LBL_LEFT, "left", 2),
     (" ", "t", 6), (LBL_RIGHT, "right", 2), (LBL_OK, "ok", 2)],
]

KB_MAP_SPECIAL = [
    [("1", "t", 1), ("2", "t", 1), ("3", "t", 1), ("4", "t", 1),
     ("5", "t", 1), ("6", "t", 1), ("7", "t", 1), ("8", "t", 1),
     ("9", "t", 1), ("0", "t", 1),
     (LBL_BACKSPACE, "back", 2)],
    [(LBL_LOWER, "low", 2),
     ("+", "t", 1), ("&", "t", 1), ("/", "t", 1), ("*", "t", 1),
     ("=", "t", 1), ("%", "t", 1), ("!", "t", 1), ("?", "t", 1),
     ("#", "t", 1), ("<", "t", 1), (">", "t", 1)],
    [("\\", "t", 1), ("@", "t", 1), ("$", "t", 1), ("(", "t", 1),
     (")", "t", 1), ("{", "t", 1), ("}", "t", 1), ("[", "t", 1),
     ("]", "t", 1), (";", "t", 1), ('"', "t", 1), ("'", "t", 1)],
    [(LBL_DISMISS, "dismiss", 2), (LBL_LEFT, "left", 2),
     (" ", "t", 6), (LBL_RIGHT, "right", 2), (LBL_OK, "ok", 2)],
]

KB_MAP_NUMBER = [
    [("1", "t", 1), ("2", "t", 1), ("3", "t", 1), (LBL_DISMISS, "dismiss", 2)],
    [("4", "t", 1), ("5", "t", 1), ("6", "t", 1), (LBL_OK, "ok", 2)],
    [("7", "t", 1), ("8", "t", 1), ("9", "t", 1), (LBL_BACKSPACE, "back", 2)],
    [(LBL_SIGN, "sign", 1), ("0", "t", 1), (".", "t", 1),
     (LBL_LEFT, "left", 1), (LBL_RIGHT, "right", 1)],
]

#: 模式名 → 键位表（模式名 = C XKeyboardMode 枚举值字符串）。
KB_MAPS = {
    "TextLower": KB_MAP_TEXT_LOWER,
    "TextUpper": KB_MAP_TEXT_UPPER,
    "Special": KB_MAP_SPECIAL,
    "Number": KB_MAP_NUMBER,
}


class SoftKeyboard(QWidget):
    """底部停靠的软键盘面板（顶层 Tool 窗口，浮在主窗/登录框之上）。

    [层级] 顶层独立窗口而非主窗子控件：登录框是 WindowModal，独立
    工具窗口不被它挡住——否则登录时键盘点不动（登录恰恰是最需要
    键盘的场景）。
    [不抢焦点] WindowDoesNotAcceptFocus：面板 show 不夺走窗口激活，
    正在输入的框保持系统焦点；按键输入走 _target 引用（首轮实测：
    面板抢激活后自己的按键成了全局焦点，收起逻辑全被它骗过）。
    [目标控件] 应用内焦点落在 QLineEdit 上时记为目标；按键直接对
    目标 insert/backspace——不注入系统事件队列，逻辑可离线测试。
    """

    KEY_H = 46                    # 键高（触摸目标 ≥46px 是触屏惯例）

    def __init__(self, parent=None):
        # [WindowDoesNotAcceptFocus 必须加] 面板 show 后若抢走窗口激活，
        # 正在输入的框丢焦点、面板自己的按键成了全局焦点——收起逻辑也被
        # 它骗过（首轮实测：面板弹出即自动隐藏）。屏上键盘的标准行为就是
        # **永不抢激活**：按键输入走 _target 引用，不依赖系统焦点。
        QWidget.__init__(self, None, Qt.Tool | Qt.FramelessWindowHint
                         | Qt.WindowStaysOnTopHint
                         | Qt.WindowDoesNotAcceptFocus)
        self.setWindowTitle("软键盘")
        self._mode = "TextLower"
        self._target = None            # 当前焦点输入控件
        self._main = None              # 主窗引用（停靠位置跟随）

        self._rows_box = QVBoxLayout()
        self._rows_box.setContentsMargins(6, 6, 6, 6)
        self._rows_box.setSpacing(5)
        self._render_page()
        root = QVBoxLayout(self)
        root.setContentsMargins(4, 4, 4, 4)
        root.setSpacing(4)
        self.setStyleSheet("QWidget { background-color: #DDE3EA; }")

    # ------------------------------------------------------------------
    #                          界面构建
    # ------------------------------------------------------------------
    def _key_style(self, btn):
        btn.setMinimumHeight(46)
        btn.setCursor(Qt.PointingHandCursor)
        btn.setStyleSheet(
            "QPushButton { background-color: #FFFFFF;"
            " border: 1px solid #B8C0CB; border-radius: 6px;"
            " padding: 0; font-size: 16px; color: #1A1A1A; }"
            "QPushButton:pressed { background-color: #7FA6D9; }")
        return btn

    def _render_page(self):
        """按当前模式重建按键区（行内按 C 的宽度单位归一化铺满）。"""
        while self._rows_box.count():
            item = self._rows_box.takeAt(0)
            lay = item.layout()
            if lay is not None:
                while lay.count():
                    sub = lay.takeAt(0)
                    sw = sub.widget()
                    if sw is not None:
                        sw.setParent(None)
                        sw.deleteLater()

        for row_spec in KB_MAPS[self._mode]:
            row = QHBoxLayout()
            row.setSpacing(5)
            for label, action, units in row_spec:
                btn = self._key_style(QPushButton(label))
                btn.clicked.connect(
                    lambda _c=False, a=action, lb=label: self._act(a, lb))
                row.addWidget(btn, units)      # 单位 = 拉伸权重（归一化）
            self._rows_box.addLayout(row)

        # ---- 附加行：Windows 系统中文键盘（过渡期，见模块头注）----
        if sys.platform == "win32" and os.path.isfile(_TABTIP):
            extra = QHBoxLayout()
            extra.setSpacing(5)
            tip = self._key_style(QPushButton("中（系统拼音）"))
            tip.clicked.connect(self._open_tabtip)
            extra.addWidget(tip, 2)
            self._rows_box.addLayout(extra)

    # ------------------------------------------------------------------
    #                          按键动作
    # ------------------------------------------------------------------
    def _act(self, action, label):
        if action == "t":
            target = self._live_target()
            if target is not None:
                target.insert(label)
            return
        if action == "back":
            target = self._live_target()
            if target is not None:
                target.backspace()
            return
        if action == "up":
            self._mode = "TextUpper"
        elif action == "low":
            self._mode = "TextLower"
        elif action == "num":
            self._mode = "TextLower" if self._mode == "Number" else "Number"
        elif action == "nl" or action == "ok":
            # [口径] 客户端全部输入控件是单行 QLineEdit：换行/确认统一
            # 收起面板（多行控件接入时在此按控件类型分流）。
            self.hide()
            return
        elif action == "dismiss":
            self.hide()
            return
        elif action == "left":
            target = self._live_target()
            if target is not None:
                target.cursorBackward(False)
            return
        elif action == "right":
            target = self._live_target()
            if target is not None:
                target.cursorForward(False)
            return
        elif action == "sign":
            target = self._live_target()
            if target is not None:
                target.insert("-")
            return
        self._render_page()                    # up/low/num：切布局

    def _open_tabtip(self):
        """唤出 Windows 系统触摸键盘（带中文输入法）；本面板让位。"""
        if os.path.isfile(_TABTIP):
            os.startfile(_TABTIP)
        self.hide()

    # ------------------------------------------------------------------
    #                    目标控件与显示/收起
    # ------------------------------------------------------------------
    def _live_target(self):
        """目标输入控件（还活着且是 QLineEdit 才有效）。"""
        if isinstance(self._target, QLineEdit) and self._target.isVisible():
            return self._target
        return None

    def set_main_window(self, window):
        self._main = window
        self.reposition()

    def reposition(self):
        """停靠到主窗底部（同屏同宽；顶层窗口用全局坐标）。"""
        if self._main is None or not self._main.isVisible():
            return
        frame = self._main.windowHandle().frameGeometry()
        self.setFixedWidth(frame.width())
        self.move(frame.x(), frame.y() + frame.height() - self.height())

    # ------------------------------------------------------------------
    #                    应用级焦点事件过滤（安装入口）
    # ------------------------------------------------------------------
    def attach(self, app, main_window):
        """接管应用焦点事件：输入类控件获得焦点 → 自动弹出键盘。"""
        self.set_main_window(main_window)
        app.installEventFilter(self)
        # 布局/窗口尺寸变化的滞后重排（覆盖主窗 resize、切页等场景）。
        timer = QTimer(self)
        timer.setInterval(400)
        timer.timeout.connect(self.reposition)
        timer.start()
        return self

    def eventFilter(self, obj, event):
        if event.type() == QEvent.Resize and obj is self._main:
            self.reposition()
            return False
        if event.type() == QEvent.FocusIn and isinstance(obj, QWidget):
            if obj.property(NO_SOFT_KBD):
                # 明确排除的控件（如日期浮层）：聚焦它时把软键盘收起，
                # 否则日期浮层和软键盘叠罗汉（实锅）。
                QTimer.singleShot(0, self._review_focus)
                return False
            if isinstance(obj, QLineEdit):
                self._target = obj
                self.reposition()
                self.show()
                self.raise_()
            elif not self._is_own_widget(obj):
                # 非输入框的 FocusIn：不能直接隐藏——面板自己的按键抢到
                # 焦点时会触发本分支把面板秒藏（首轮实测）。记 0ms 复查：
                # 那时焦点已落定，真离开输入才收起。
                QTimer.singleShot(0, self._review_focus)
        elif event.type() == QEvent.FocusOut and obj is self._target:
            QTimer.singleShot(0, self._review_focus)
        return False

    def _review_focus(self):
        """焦点落定后复查：还指着某个输入框/还在键盘自身 → 保持；否则收起。"""
        fw = QApplication.focusWidget()
        if isinstance(fw, QLineEdit) and not fw.property(NO_SOFT_KBD):
            self._target = fw
            return
        if self._is_own_widget(fw):
            return
        self._target = None
        self.hide()

    def _is_own_widget(self, widget):
        """widget 是否在软键盘面板自身之内（自己的按键不算"焦点离开"）。"""
        walker = widget
        while walker is not None:
            if walker is self:
                return True
            walker = walker.parentWidget()
        return False


def attach_soft_keyboard(main_window):
    """主窗装配入口：按触摸环境开关建软键盘；关闭时返回 None。"""
    if not touch_default_enabled():
        return None
    kb = SoftKeyboard()
    kb.attach(QApplication.instance(), main_window)
    return kb
