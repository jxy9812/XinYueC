# -*- coding: utf-8 -*-
"""应用内软键盘：触摸屏无键盘设备的输入方案（拼音 / 字母 / 数字三类键盘）。

[为什么自绘] PyQt5 未打包 QtVirtualKeyboard 模块、现场无外网装不了；板端
（Linux）也没有 Windows 的 TabTip。自绘键盘在桌面 Windows 与板端 X11 表现
完全一致，且可以离线做逻辑测试。

[三类键盘 + 同一套焦点驱动] 键盘类型**由调用方声明**，本模块只负责按声明
装配布局；自动弹出/收起/换目标全部走同一条焦点事件链（没有第二套机制）：

    kind="pinyin"   拼音全键（候选带 + 中/EN）；缺省类型
    kind="alpha"    字母键盘（ABC/abc、1#、符号），纯 ASCII
    kind="numeric"  数字键盘（Digits / Number 两种排布），数字专用
    kind="none"     明确不要键盘（控件自带浮层，如日期选择器）

声明通道优先级（高 → 低），与 C 端 XWidget_inputMethodHints 同口径：

    1. Qt 属性 HSF_KEYBOARD_KIND（set_keyboard_kind）：显式覆盖，页面里
       「这个框就是要数字盘」这类意图用它，不依赖 hints 推断。
    2. QWidget.inputMethodHints()（setInputMethodHints）：Qt 6.8 标准通道，
       也是最自然的通道——QSpinBox 自带 ImhDigitsOnly，密码框自带
       ImhHiddenText，页面**什么都不用写**就自动对。
    3. 缺省：有焦点即弹拼音键盘（中文可输入；EN 态就是字母键盘）。

[拼音] 引擎与数据见 pinyin.py：候选与 C 端（XPinyinEngine + XPinyinTable +
phrases_zh）逐字一致。软键盘只做「按什么键 → 喂引擎 → 画候选带」的接线：
    · 中/EN 键切中英文；中文态字母进组串（不写编辑框），候选带上屏；
    · 空格 = 提交首候选（无候选则原字母串），回车/确定 = 提交原串；
    · 退格组串非空时删字母，空了才删编辑框（与 C 端 xkb 口径一致）；
    · 数字键在组串中选候选（跨页时按当前页内位序）。
[焦点] 面板是顶层 Tool 窗口（WindowDoesNotAcceptFocus，永不抢激活）：按键
直接对目标控件 insert/backspace，不注入系统事件队列——既避开「面板一弹就
把焦点抢走」，又能在离屏环境完整回归。
[开关] HSF_TOUCH 环境变量：'1' 强制开 / '0' 强制关 / 未设 → 自动探测
（Windows 查触摸数字化器，非 Windows 默认开——板端就是触摸屏）。
"""

import os
import sys

from PyQt5.QtCore import QEvent, Qt, QTimer
from PyQt5.QtGui import QColor
from PyQt5.QtWidgets import (QAbstractSpinBox, QApplication, QComboBox,
                             QHBoxLayout, QLabel, QLineEdit, QPushButton,
                             QVBoxLayout, QWidget)

from . import pinyin as pinyin_mod

#: 软键盘目标输入控件的排除标记：设了此 Qt 属性的输入控件不弹键盘
#: （如 TimePickerEdit——它单击弹的是自己的日期选择浮层）。
NO_SOFT_KBD = "_hsf_no_soft_kbd"

#: 键盘类型显式声明属性（优先级高于 inputMethodHints，见模块头注）。
KEYBOARD_KIND_PROP = "_hsf_keyboard_kind"

#: 键盘类型取值。
KIND_PINYIN = "pinyin"
KIND_ALPHA = "alpha"
KIND_NUMERIC = "numeric"
KIND_NONE = "none"
KEYBOARD_KINDS = (KIND_PINYIN, KIND_ALPHA, KIND_NUMERIC, KIND_NONE)


def _is_x11():
    """当前是否跑在 X11（含板端 Xorg）。

    [为什么要判平台] X11BypassWindowManagerHint 只在 X11 有意义；Windows
    上它是空操作，但显式判一下能让意图清楚，也避免将来在 Wayland 上
    误用（Wayland 没有 WM 装饰这回事，该另想办法）。
    """
    if sys.platform.startswith("linux"):
        return bool(os.environ.get("DISPLAY")) and \
            os.environ.get("QT_QPA_PLATFORM", "xcb").startswith("xcb")
    return False


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
#                   键盘类型声明（调用方 → 本模块）
# ============================================================================

def set_keyboard_kind(widget, kind):
    """调用方显式声明控件要哪种键盘（覆盖 inputMethodHints 推断）。

        set_keyboard_kind(port_edit, KEYBOARD_KIND_NUMERIC)

    :param widget: 输入控件（QLineEdit / QSpinBox / 可编辑 QComboBox）。
    :param kind:   KEYBOARD_KINDS 之一；``None`` 表示清除显式声明。
    """
    if kind is not None and kind not in KEYBOARD_KINDS:
        raise ValueError("未知键盘类型：%r（可选 %r）" % (kind, KEYBOARD_KINDS))
    if kind is None:
        widget.setProperty(KEYBOARD_KIND_PROP, None)
    else:
        widget.setProperty(KEYBOARD_KIND_PROP, kind)
    return widget


def _numeric_layout_of_hints(hints):
    """数字键盘的排布：DigitsOnly → 3x4 纯数字盘；其余 → 带符号的 Number。"""
    if hints & int(Qt.ImhDigitsOnly):
        return "digits"
    return "number"


def keyboard_kind_of(widget):
    """按声明通道解析控件需要的键盘类型。

    :returns: ``(kind, layout)``；layout 为 ``None`` 或数字盘排布名。
    """
    explicit = widget.property(KEYBOARD_KIND_PROP)
    hints = int(widget.inputMethodHints() or 0)
    if explicit in KEYBOARD_KINDS:
        layout = _numeric_layout_of_hints(hints) if explicit == KIND_NUMERIC \
            else None
        return explicit, layout
    # ---- 与 C 端 xkb_applyFocusHints 的优先级链同序 ----
    if hints & int(Qt.ImhDialableCharactersOnly):
        return KIND_NUMERIC, "dialpad"
    if hints & int(Qt.ImhFormattedNumbersOnly):
        return KIND_NUMERIC, "number"
    if hints & int(Qt.ImhDigitsOnly):
        return KIND_NUMERIC, "digits"
    if hints & int(Qt.ImhPreferNumbers):
        return KIND_NUMERIC, "number"
    if hints & (int(Qt.ImhLatinOnly) | int(Qt.ImhEmailCharactersOnly)
                | int(Qt.ImhUrlCharactersOnly) | int(Qt.ImhHiddenText)):
        return KIND_ALPHA, None
    return KIND_PINYIN, None


# ============================================================================
#                    键位表（C 端布局语义 + 手机键盘几何）
# ============================================================================
# 条目 = (标签, 动作, 宽度单位)。动作：
#   "t"   插入标签字符            "pl" 拼音/字母键（中文态进组串）
#   "digit" 数字键（组串中选候选） "up"/"low"  切大写/小写布局
#   "num" 切数字布局/回文本        "back" 退格    "nl"/"ok" 换行/确认
#   "dismiss" 收起                 "left"/"right" 移动光标  "sign" 插入负号
#   "space" 空格（组串中=提交首候选） "ime" 中/EN 切换
#   "symbol" 切符号布局            "pinyin" 回拼音字母布局
#   "shift" 拼音键盘的大小写切换
# [单位口径] 1 单位 = 半键宽：字母键 2、功能键 3（=1.5 键）、行 2 的
# 两侧留白 1（=半键）。这样手机键盘的标准几何自然成立：
#     行1 10×2 = 20        行2 1+9×2+1 = 20（左右各缩半键）
#     行3 3+7×2+3 = 20     行4 功能行按 20~24 收口
# 所有行都按本行单位总数归一化，于是同一列在不同行的键宽一致（手机手感）。
# 布局语义仍是 C 端的四模式（TextLower/TextUpper/Special/Number + 拼音页），
# 只是把「满宽扁条」换成手机键盘的比例。
LBL_UPPER, LBL_LOWER = "ABC", "abc"
LBL_BACKSPACE, LBL_NEWLINE = "退格", "换行"
LBL_DISMISS, LBL_LEFT, LBL_RIGHT, LBL_OK = "收起", "←", "→", "确定"
LBL_1NUM, LBL_SIGN = "1#", "+/-"
LBL_IME, LBL_SYMBOL, LBL_NUMBERS = "中/EN", "符", "123"
LBL_PINYIN = "拼音"

#: 手机键盘字母区（3 行：10 / 9 / Shift+7+退格）。
_PINYIN_LETTER_ROWS = [
    "qwertyuiop",
    "asdfghjkl",
    "zxcvbnm",
]


def _letter_rows(upper=False, shift_label=None, swap_label=None):
    """手机键盘字母区 4 行（10 / 9 / 功能+7+退格 / 功能行）。

    各行单位总数统一 20，于是同列键在所有行等宽（手机键盘的手感）：
        行1 10×2
        行2 1 + 9×2 + 1      （两侧各半键留白）
        行3 3 + 7×2 + 3      （两侧各 1.5 键功能键）
        行4 3+3+2+6+2+... → 见下（含空格共 20 单位）

    :param upper: 是否大写键帽。
    :param shift_label: 行 3 左侧键标签（拼音键盘给大小写切换，字母键盘
                        给 ABC/abc 切换）；给 None 表示不要该键。
    :param swap_label: 行 4 左侧「数字/符号」两键的标签对；给 None 用
                       手机缺省 (123, 符)。
    """
    rows = []
    rows.append([(ch.upper() if upper else ch, "pl", 2)
                 for ch in _PINYIN_LETTER_ROWS[0]])
    # 行 2：两侧各留半键（单位 1），9 个字母居中——手机键盘的标志性缩进。
    row2 = [(None, "spacer", 1)]
    row2 += [(ch.upper() if upper else ch, "pl", 2)
             for ch in _PINYIN_LETTER_ROWS[1]]
    row2.append((None, "spacer", 1))
    rows.append(row2)
    # 行 3：功能键（1.5 键）+ 7 字母 + 退格（1.5 键）。
    row3 = [(shift_label, "shift" if shift_label else "spacer", 3)
            if shift_label else (None, "spacer", 3)]
    row3 += [(ch.upper() if upper else ch, "pl", 2)
             for ch in _PINYIN_LETTER_ROWS[2]]
    row3.append((LBL_BACKSPACE, "back", 3))
    rows.append(row3)
    # 行 4（单位合计 20）：123 / 符 / , / 空格 / 。 / 中EN / 收起 / 确定。
    num_label, sym_label = swap_label or (LBL_NUMBERS, LBL_SYMBOL)
    rows.append([
        (num_label, "num", 3),
        (sym_label, "symbol", 2),
        (",", "t", 2),
        (" ", "space", 4),
        ("。", "t", 2),
        (LBL_IME, "ime", 2),
        (LBL_DISMISS, "dismiss", 2),
        (LBL_OK, "ok", 3),
    ])
    return rows


def _pinyin_rows(upper=False):
    """拼音全键布局：手机键盘几何 + C 端 PinyinFull 语义（4 行）。"""
    rows = _letter_rows(upper=upper, shift_label=(LBL_LOWER if upper
                                                  else LBL_UPPER))
    return rows


KB_MAP_TEXT_LOWER = _letter_rows(upper=False, shift_label=LBL_UPPER)
KB_MAP_TEXT_UPPER = _letter_rows(upper=True, shift_label=LBL_LOWER)

#: 符号盘（手机口径）：数字行 / 符号两行 / 功能行，各 20 单位。
KB_MAP_SPECIAL = [
    [("1", "t", 2), ("2", "t", 2), ("3", "t", 2), ("4", "t", 2),
     ("5", "t", 2), ("6", "t", 2), ("7", "t", 2), ("8", "t", 2),
     ("9", "t", 2), ("0", "t", 2)],
    [(LBL_LOWER, "low", 3),
     ("+", "t", 2), ("&", "t", 2), ("/", "t", 2), ("*", "t", 2),
     ("=", "t", 2), ("%", "t", 2), ("!", "t", 2),
     (LBL_BACKSPACE, "back", 3)],
    [("\\", "t", 2), ("@", "t", 2), ("$", "t", 2), ("(", "t", 2),
     (")", "t", 2), ("<", "t", 2), (">", "t", 2), ("#", "t", 2),
     ("\"", "t", 2), ("'", "t", 2)],
    [(LBL_NUMBERS, "num", 3), (LBL_PINYIN, "pinyin", 2), (",", "t", 2),
     (" ", "space", 5), ("；", "t", 2), (LBL_DISMISS, "dismiss", 2),
     (LBL_OK, "ok", 4)],
]

#: 数字盘（带符号，手机数字键盘口径：3 列数字 + 底部操作行）。
KB_MAP_NUMBER = [
    [("1", "digit", 4), ("2", "digit", 4), ("3", "digit", 4)],
    [("4", "digit", 4), ("5", "digit", 4), ("6", "digit", 4)],
    [("7", "digit", 4), ("8", "digit", 4), ("9", "digit", 4)],
    [(LBL_SIGN, "sign", 4), ("0", "digit", 4), (".", "t", 4)],
    [(LBL_BACKSPACE, "back", 4), ("收起", "dismiss", 4),
     (LBL_OK, "ok", 4)],
]

#: 纯数字盘（hints DigitsOnly 的映射目标，手机数字键盘口径：3 列数字 +
#: 底部一行操作键）。
KB_MAP_DIGITS = [
    [("1", "digit", 4), ("2", "digit", 4), ("3", "digit", 4)],
    [("4", "digit", 4), ("5", "digit", 4), ("6", "digit", 4)],
    [("7", "digit", 4), ("8", "digit", 4), ("9", "digit", 4)],
    [(".", "t", 4), ("0", "digit", 4), (LBL_BACKSPACE, "back", 4)],
    [(LBL_DISMISS, "dismiss", 6), (LBL_OK, "ok", 6)],
]

#: 拼音键盘的数字页（带「拼音」返回键：触摸屏没有物理 Esc，必须有可见回路）。
KB_MAP_PINYIN_NUMBER = [
    [("1", "digit", 4), ("2", "digit", 4), ("3", "digit", 4)],
    [("4", "digit", 4), ("5", "digit", 4), ("6", "digit", 4)],
    [("7", "digit", 4), ("8", "digit", 4), ("9", "digit", 4)],
    [(LBL_SIGN, "sign", 4), ("0", "digit", 4), (".", "t", 4)],
    [(LBL_BACKSPACE, "back", 4), (LBL_PINYIN, "pinyin", 4),
     (LBL_OK, "ok", 4)],
]

#: 字母键盘四模式（模式名 = C XKeyboardMode 枚举值字符串）。
KB_MAPS = {
    "TextLower": KB_MAP_TEXT_LOWER,
    "TextUpper": KB_MAP_TEXT_UPPER,
    "Special": KB_MAP_SPECIAL,
    "Number": KB_MAP_NUMBER,
}


# ============================================================================
#                        输入目标适配（控件族 → 插入/删除）
# ============================================================================

def _text_editor_of(widget):
    """取控件真正的文本编辑器（QAbstractSpinBox/QComboBox 内部那个）。"""
    if isinstance(widget, QLineEdit):
        return widget
    if isinstance(widget, QAbstractSpinBox):
        return widget.lineEdit()
    if isinstance(widget, QComboBox) and widget.isEditable():
        return widget.lineEdit()
    return None


def is_text_input(widget):
    """控件是否接受文本输入（软键盘的服务对象）。"""
    return _text_editor_of(widget) is not None


class _InputTarget(object):
    """输入目标适配器。

    [为什么要有这层] QLineEdit 的 insert/backspace 直接生效；QSpinBox 必须
    改它的内部 lineEdit 再 interpretText()，否则文本变了而 value 不变（下
    发频率时读到旧值——实锅）。焦点事件到达的是 spinbox 本身，插入却要找
    内部编辑器，这一层把差异收敛掉。
    """

    def __init__(self, widget):
        self.widget = widget
        self.editor = _text_editor_of(widget)

    # ---- 生命周期 ----
    def alive(self):
        """目标是否还能写（控件没被销毁、可见且可用）。"""
        try:
            return bool(self.widget.isVisible() and self.widget.isEnabled()
                        and self.editor is not None)
        except RuntimeError:                # C++ 对象已销毁
            return False

    # ---- 写入 ----
    def insert(self, text):
        if not self.alive():
            return False
        self.editor.insert(text)
        self._sync()
        return True

    def select_all(self):
        """全选当前内容（键盘弹出时调用，让用户直接覆盖重输）。

        [为什么弹出时就全选] 触摸屏调数值时，操作员想的是"改成 35"，
        而不是"先把 25 删掉再打 35"。全选后第一个字符就顶掉原值
        （`QLineEdit.insert()` 有选区时是替换），是最少动作的输入方式；
        若想微调（如 25→255）也能一键退格只删末位 —— 注意退格在**有选区**
        时会清空整个选区，那正是"全选重输"的预期语义。

        [为什么不影响普通文本框] 全选对 QLineEdit 同样友好：直接打新词
        就替换旧词。历史筛选里的关键字框就是这个体验，符合触摸屏习惯。
        """
        if not self.alive():
            return False
        try:
            self.editor.selectAll()
            return True
        except (RuntimeError, AttributeError):
            return False

    def backspace(self):
        if not self.alive():
            return False
        self.editor.backspace()
        self._sync()
        return True

    def move_cursor(self, forward):
        if not self.alive():
            return False
        if forward:
            self.editor.cursorForward(False)
        else:
            self.editor.cursorBackward(False)
        return True

    def _sync(self):
        """spinbox 族：不做每键强解析。

        [实锅] 每键 `interpretText()` 会把中间态（"6"，低于量程下限）立刻
        clamp 成下限、文本被改写，最终落值变成 2000 而不是 600。QSpinBox
        的 keyboardTracking 默认开着，文本合法时会自己同步 value；中间态
        就该保持原样（用户还在打字）。强制解析留到 commit()。
        """
        return

    def commit(self):
        """确认/收起/失焦：把文本最终解析成数值（等价敲回车）。"""
        if self.adapter_alive() and isinstance(self.widget, QAbstractSpinBox):
            self.widget.interpretText()

    def adapter_alive(self):
        try:
            return self.widget is not None and self.editor is not None
        except RuntimeError:
            return False

    def text(self):
        return self.editor.text() if self.editor is not None else ""


# ============================================================================
#                              软键盘面板
# ============================================================================

class SoftKeyboard(QWidget):
    """底部停靠的软键盘面板（顶层 Tool 窗口，浮在主窗/登录框之上）。

    [层级] 顶层独立窗口而非主窗子控件：登录框是 WindowModal/ApplicationModal，
    独立工具窗口不被它挡住——否则登录时键盘点不动（登录恰恰是最需要键盘
    的场景）。
    [不抢焦点] WindowDoesNotAcceptFocus：面板 show 不夺走窗口激活，正在输入
    的框保持系统焦点；按键输入走 _target 引用。
    [三类键盘] 见模块头注：kind 由目标控件的声明通道决定，切目标即可能出现
    「拼音 → 数字」这种布局切换。
    """

    KEY_H = 58                    # 键高（手机键盘比例：>=48px 触摸目标）
    KEY_H_COMPACT = 50            # 矮屏档键高
    BAND_H = 50                   # 候选带标称高（chip 内高基准；实际高由键高决定）
    KEY_GAP = 7                   # 键间距（手机键盘约 6~8px）
    KEY_RADIUS = 9                # 键圆角
    KEY_FONT = 30                 # 字母/数字键字号（23*1.3，触摸屏手感）
    FUNC_FONT = 21                # 功能键字号（16*1.3，标签较长故小一号）
    MAX_BLOCK_W = 1060            # 字母/符号键区最大宽度（超出则居中）
    MAX_BLOCK_W_NUMERIC = 1320    # 数字盘最大宽度（键少，铺得更开才像手机）
    MAX_KEY_W = 88                # 单键最大宽度（宽屏不把键拉成长条）
    MAX_KEY_W_NUMERIC = 150       # 数字键最大宽度（手机数字键就是又大又方）

    #: 【槽位口径】面板高度按「槽」算：**候选带也算一个槽**（它的高度
    #: 恒等于键高），于是"带 + 4 行键"和"5 行键"的内容高完全相同。
    #: 所有键盘都按 SLOTS 个槽钉死内容高，行数少的布局把键拉高填满 ——
    #: 这样切换键盘时**边框尺寸纹丝不动**（用户 2026-11-06 明确要求），
    #: 同时任何布局都不会在底部留下死白。
    SLOTS = 5

    def __init__(self, parent=None):
        # [WindowDoesNotAcceptFocus 必须加] 面板 show 后若抢走窗口激活，
        # 正在输入的框丢焦点、面板自己的按键成了全局焦点——收起逻辑也被
        # 它骗过（首轮实测：面板弹出即自动隐藏）。屏上键盘的标准行为就是
        # **永不抢激活**：按键输入走 _target 引用，不依赖系统焦点。
        #
        # [X11 必须绕过窗口管理器] 板端实测（2026-11-06，xfwm4）：
        #   · Qt.Tool 窗口被 xfwm4 补了一条 **15px 系统标题栏** ——
        #     Qt 自己的 geometry 是 (0,460,1280,340)，X 上实际是
        #     1280x355+0+445，多出的 15px 就是它，视觉上正是"下方一
        #     大块空白"；键盘是整块自绘底板，绝不能有系统装饰。
        #   · 更严重的是这张 Tool 窗口一 map，xfwm4 会重新排层，把
        #     xfce4 面板抬到全屏窗之上 —— 于是**系统的顶栏盖住了软件
        #     自己的标题栏**（用户 2026-11-06 报的原话）。
        # 加 X11BypassWindowManagerHint 后窗口成为 override-redirect：
        # 无装饰、不参与 WM 排层，全屏窗保持覆盖状态栏，实测两个现象
        # 同时消失（三态对照图 pb_none/normal/bypass）。
        flags = (Qt.Tool | Qt.FramelessWindowHint
                 | Qt.WindowStaysOnTopHint
                 | Qt.WindowDoesNotAcceptFocus)
        if _is_x11():
            flags |= Qt.X11BypassWindowManagerHint
        QWidget.__init__(self, None, flags)
        self.setWindowTitle("软键盘")
        self._kind = KIND_PINYIN
        self._numeric_layout = "digits"
        self._mode = "TextLower"        # alpha 键：TextLower/TextUpper/Special/Number
        self._pinyin_page = "letters"   # pinyin 键：letters/numbers/special
        self._pinyin_upper = False      # 拼音键的 EN 大写态（中文态恒小写）
        self._target = None             # 当前目标控件
        self._adapter = None            # 目标适配器
        self._main = None               # 主窗引用（停靠位置跟随）
        self._engine = pinyin_mod.PinyinEngine()

        root = QVBoxLayout(self)
        root.setContentsMargins(6, 6, 6, 6)
        root.setSpacing(self.KEY_GAP)
        # [手机键盘底色] 深一档的灰蓝底 + 白/浅灰键，键与底有明暗层次；
        # 用 ID 选择器只命中面板自身，否则会级联把键面也刷成底色。
        self.setObjectName("HsfSoftKeyboard")
        self.setStyleSheet(
            "#HsfSoftKeyboard { background-color: #D3DAE3; }")
        # [底色必须显式填充] 无边框顶层窗口默认不画背景，露出下面的桌面；
        # 手机键盘是整块不透明底板，键与键之间要有底色（否则键悬空）。
        self.setAutoFillBackground(True)
        palette = self.palette()
        palette.setColor(self.backgroundRole(), QColor("#D3DAE3"))
        self.setPalette(palette)

        # ---- 候选带（仅拼音键盘可见；常驻占位避免布局跳动）----
        # [高度跟随键高] 候选带是「一个槽」：高等于键高（见 SLOTS）。
        # 硬钉 BAND_H 会让带+4行键的内容高与 5 行键不一致，面板尺寸
        # 随键盘类型变化。初值给 BAND_H，reflow 会立刻按槽位对齐。
        self._band = QWidget(self)
        self._band.setFixedHeight(self.BAND_H)
        self._band.setStyleSheet(
            "QWidget { background-color: #F7F9FC; border: 1px solid #C6CEDA;"
            " border-radius: 8px; }")
        self._band_box = QHBoxLayout(self._band)
        self._band_box.setContentsMargins(10, 4, 10, 4)
        self._band_box.setSpacing(8)
        root.addWidget(self._band)
        self._band.hide()

        # ---- 键区 ----
        self._keys_host = QWidget(self)
        self._rows_box = QVBoxLayout(self._keys_host)
        self._rows_box.setContentsMargins(2, 2, 2, 2)
        self._rows_box.setSpacing(self.KEY_GAP)
        root.addWidget(self._keys_host)
        self._render_page()

    # ------------------------------------------------------------------
    #                          界面构建
    # ------------------------------------------------------------------
    #: 功能键/字符键的动作分类（决定配色：功能键浅灰、字符键白、强调蓝）。
    _FUNC_ACTIONS = frozenset((
        "back", "ok", "nl", "dismiss", "symbol", "num", "shift", "up",
        "low", "ime", "pinyin", "page_prev", "page_next",
    ))

    def _key_style(self, btn, accent=False, func=False, compact=False,
                   key_h=None):
        """给按键上样式并钉死高度。

        [为什么高度必须在这里钉死、而不是只靠 setFixedSize]
        `setFixedSize` 设的是 minimum/maximum，但 **QSS 的 min-height /
        padding 会参与 sizeHint 计算**：当行布局的可用高度小于按钮的
        sizeHint 时，Qt 仍会按 `minimumHeight` 压 —— 实测板端第 1/3/4
        行被压成 32/35px（期望 58），底部因此空出一大块（用户报告的
        "按键下面一大块空白"，2026-11-06 板端截图红框）。
        这里显式用传入的实际键高（= `_key_h_for_slots()`）设 min/max，
        与 setFixedSize 同源，不给布局留压缩空间。
        """
        h = int(key_h) if key_h else (self.KEY_H_COMPACT if compact
                                      else self.KEY_H)
        # [QSS 高度要扣掉边框] QSS 的 min-height/max-height 是**内容区**
        # 尺寸，`border: 1px` 会额外加 2px；直接写 h 会让实际键高变成
        # h+2（实测 58 设进去、量出来 60）。这里扣掉边框，保证量到的
        # 就是设计值 —— 否则每行多 2px，面板总高与 reflow() 的算术对不上。
        inner_h = max(1, h - 2)
        btn.setMinimumHeight(h)
        btn.setMaximumHeight(h)
        btn.setCursor(Qt.PointingHandCursor)
        btn.setFocusPolicy(Qt.NoFocus)     # 按键永不参与焦点（也不抢）
        font_size = self.FUNC_FONT if func else self.KEY_FONT
        if accent:
            bg, fg, border = "#2E7CE8", "#FFFFFF", "#1A66D0"
            pressed = "#1A66D0"
        elif func:
            bg, fg, border = "#C9D2DC", "#243747", "#B3BECB"
            pressed = "#AEBAC7"
        else:
            bg, fg, border = "#FFFFFF", "#1A1A1A", "#C6CEDA"
            pressed = "#8FB2DE"
        # [手机键盘观感] 大圆角 + 无描边重感 + 按下变深；字符键白、功能键
        # 浅灰（iOS/Android 都是这个分层），强调键用品牌蓝。
        #
        # [必须写 max-height/min-height] 只给 padding/font 的话 QSS 会算出
        # 自己的 sizeHint 高度，与 setFixedSize 打架（见本函数头注）。
        btn.setStyleSheet(
            "QPushButton { background-color: %s; color: %s;"
            " border: 1px solid %s; border-radius: %dpx;"
            " padding: 0; min-height: %dpx; max-height: %dpx;"
            " font-size: %dpx; font-weight: 500; }"
            "QPushButton:pressed { background-color: %s; }"
            % (bg, fg, border, self.KEY_RADIUS, inner_h, inner_h,
               font_size, pressed))
        return btn

    def _current_rows(self):
        """当前 kind/state 下的键位行（(标签, 动作, 宽度单位) 的二维表）。"""
        if self._kind == KIND_PINYIN:
            if self._pinyin_page == "numbers":
                return KB_MAP_PINYIN_NUMBER
            if self._pinyin_page == "special":
                return KB_MAP_SPECIAL
            return _pinyin_rows(self._pinyin_upper
                                and not self._engine.is_chinese())
        if self._kind == KIND_NUMERIC:
            if self._numeric_layout == "number":
                return KB_MAP_NUMBER
            if self._numeric_layout == "dialpad":
                return KB_MAP_NUMBER          # 拨号盘：Number 已含 +/-
            return KB_MAP_DIGITS
        return KB_MAPS[self._mode]

    def _clear_rows(self):
        """清空键区（行外框是 QWidget，里面再套两层布局）。

        [为什么行外框是 QWidget] 见 `_render_page`：QHBoxLayout 钉不住
        高度（没有 setFixedHeight），必须包一层 setFixedHeight 的控件。
        于是这里的拆解要按「布局项 → 控件 → 其布局 → 键」走。

        [必须立刻 setParent(None) + deleteLater] 只 takeAt 掉布局不清父对象，
        旧按键仍是 _keys_host 的子控件、会继续画在同一个位置上——重建后
        看起来「多出一整套键」（实测：73 个按钮，左上一个游离的「确定」）。
        """
        while self._rows_box.count():
            item = self._rows_box.takeAt(0)
            host = item.widget()               # 行外框（QWidget）
            if host is not None:
                lay = host.layout()
                if lay is not None:
                    while lay.count():
                        sub = lay.takeAt(0)
                        inner = sub.layout()
                        if inner is not None:
                            while inner.count():
                                cell = inner.takeAt(0)
                                sw = cell.widget()
                                if sw is not None:
                                    sw.setParent(None)
                                    sw.hide()
                                    sw.deleteLater()
                            continue
                        sw = sub.widget()
                        if sw is not None:
                            sw.setParent(None)
                            sw.hide()
                            sw.deleteLater()
                host.setParent(None)
                host.hide()
                host.deleteLater()
                continue
            # 兼容旧形态：直接是布局项（stretch 之类）
            lay = item.layout()
            if lay is None:
                continue
            while lay.count():
                sub = lay.takeAt(0)
                inner = sub.layout()
                if inner is not None:
                    while inner.count():
                        cell = inner.takeAt(0)
                        sw = cell.widget()
                        if sw is not None:
                            sw.setParent(None)
                            sw.hide()
                            sw.deleteLater()
                    continue
                sw = sub.widget()
                if sw is not None:
                    sw.setParent(None)
                    sw.hide()
                    sw.deleteLater()

    def _block_width(self):
        """键区宽度：手机键盘不铺满桌面，超宽就限宽居中。

        数字盘走单独的上限（键少、要更大更好按），字母/符号盘用通用上限。
        """
        avail = self.width() - 2 * self.KEY_GAP
        widest_row = max(
            sum(units for (_lbl, _act, units) in row) or 1
            for row in self._current_rows())
        if self._kind == KIND_NUMERIC:
            cap = min(self.MAX_BLOCK_W_NUMERIC,
                      widest_row * self.MAX_KEY_W_NUMERIC)
        else:
            cap = min(self.MAX_BLOCK_W, widest_row * self.MAX_KEY_W)
        return max(240, min(avail, cap))

    def _render_page(self):
        """按当前布局重建按键区（手机键盘几何：等宽键 + 居中键区）。

        [键高来自槽位] 不再用固定的 KEY_H：由 `_key_h_for_slots()` 按
        SLOTS 反算，行数少的布局（如数字盘 5 行 vs 拼音带+4 行）拿到的
        键高相同，内容总高因此一致 ⇒ 面板尺寸恒定、底部无死白。
        """
        self._clear_rows()

        compact = self._is_compact()
        key_h = self._key_h_for_slots()
        block_w = self._block_width()
        rows_spec = self._current_rows()
        # [统一归一化基准] 所有行按**最宽行**的单位总数算单键宽——手机
        # 键盘上 1 单位在每一行都是同一像素宽，于是短行（如行 2 只有 18
        # 单位）自然两侧留白、长行铺满，不会出现「某行整体偏窄」。
        ref_units = max(
            (sum(u for (_l, _a, u) in row) or 1) for row in rows_spec) or 1
        unit_w = block_w / float(ref_units)
        for row_spec in rows_spec:
            # 行外框：**包一层 QWidget** 才钉得住高度。
            #
            # [为什么不能只用 QHBoxLayout] QLayout 是二维容器，没有
            # setFixedHeight/setMaximumHeight（只有 setSizeConstraint）。
            # 不包控件时它的 sizeHint 高度按子项**最小**要求算，整行会被
            # 压扁 —— 实测板端第 1/3/4 行压成 32/35px（期望 58），底部
            # 因此空出一大块（用户 2026-11-06 报告的红框区域）。
            # 包一层 setFixedHeight(key_h) 的 QWidget 后，行高由外层控件
            # 决定，布局只能在其中水平排布。
            row_host = QWidget(self._keys_host)
            row_host.setFixedHeight(key_h)
            row_host.setStyleSheet("background: transparent;")
            outer = QHBoxLayout(row_host)
            outer.setContentsMargins(0, 0, 0, 0)
            outer.addStretch(1)
            row = QHBoxLayout()
            row.setSpacing(self.KEY_GAP)
            row.setContentsMargins(0, 0, 0, 0)
            for label, action, units in row_spec:
                key_w = max(26, int(round(unit_w * units)))
                if action == "spacer":
                    # 单位留白（手机上第 2 行两侧各半键）。
                    gap = QWidget()
                    gap.setFixedSize(key_w, key_h)
                    gap.setStyleSheet("background: transparent;")
                    row.addWidget(gap)
                    continue
                func = action in self._FUNC_ACTIONS
                btn = self._key_style(QPushButton(label or ""), func=func,
                                     compact=compact, key_h=key_h)
                btn.setFixedSize(key_w, key_h)
                btn.clicked.connect(
                    lambda _c=False, a=action, lb=label: self._act(a, lb))
                row.addWidget(btn)
            outer.addLayout(row, 0)
            outer.addStretch(1)
            self._rows_box.addWidget(row_host)
        # [不要尾部 stretch] 旧实现这里 addStretch(1) 把面板多出来的高度
        # 全沉到底部 —— 那就是用户看到的"按键下面一大块空白"（实测拼音
        # 盘 16px + 底边距 6px）。现在面板高正好等于内容高（见 reflow），
        # 没有余量需要吸收；再加 stretch 反而会在浮点误差下挤出一条缝。
        self._rows_box.addStretch(0)

        # 拼音键盘才显示候选带（其它品类不需要这一行）。
        self._band.setVisible(self._kind == KIND_PINYIN)
        self.reflow()
        self._update_band()

    def _is_compact(self):
        """矮屏档（如 800x480 工控屏）：键高与字号收一档，保证 5 槽装得下。

        [窗口还没布局时不许判成 compact] 主窗刚构造、尚未 show/布局时
        `height()` 是 0 或几十像素（实测板端探针里是 52），此时按"欠高清"
        判成 compact 会让键高落到 50 档，而 reflow() 又按当时算出的高度
        钉面板高 —— 一旦主窗随后变大，键高与面板高就对不上（真机上表现
        为按键被压扁、底部留白）。故高度不可信（< 300）时按**非紧凑**处理，
        等布局完成后的 reflow 自然会用真实高度。
        """
        main = self._main
        if main is None:
            return False                   # 没主窗 ⇒ 按大屏缺省（58 档）
        height = main.height()
        if height < 300:
            return False                   # 尚未布局，按非紧凑
        return height < 700

    def _slot_band(self):
        """本键盘是否占一个候选带槽（只有拼音盘有）。"""
        return self._kind == KIND_PINYIN

    def _key_h_for_slots(self):
        """按「固定槽数」反算键高：内容高恒定，与当前键盘的行数无关。

        [为什么不是"按行数算高"] 原实现 height = 带高 + 行数*键高 + 间距，
        于是拼音盘（带 + 4 行 = 5 槽）高 340、数字盘（5 行）高 355 ——
        每换一次键盘类型，面板边框就跟着收缩/长高一次。用户要求边框
        **不自己收缩**，且键下不许留白，所以反过来：槽数固定为 SLOTS，
        键高由它均分。这样：
            · 面板高恒定 ⇒ 切换键盘边框不动；
            · 内容总高恰好等于面板 ⇒ 底部没有死白。
        """
        base = self.KEY_H_COMPACT if self._is_compact() else self.KEY_H
        gap = self.KEY_GAP
        rows = len(self._current_rows())
        # 可用槽数下限保护：万一将来出现比 SLOTS 更多的布局，按行数走，
        # 宁可面板变高也不能把键压扁（压扁会击穿 48px 触摸目标下限）。
        slots = max(self.SLOTS, rows + (1 if self._slot_band() else 0))
        # 内容高 = slots*key + (slots-1)*gap  ⇒ 基准高度由 base 决定
        # （base 是"每槽"的标称高，等价于原 KEY_H 的视觉密度）。
        total = slots * base + (slots - 1) * gap
        avail = total - (slots - 1) * gap
        return max(self.KEY_H_COMPACT, int(round(float(avail) / slots)))

    def reflow(self):
        """钉死面板高度（含候选带）并按槽位重算键区几何。

        [为什么必须显式钉高] 面板是 frameless 顶层窗口，Qt 不会因内部
        布局变化就调整大小——首次 show 沿用 QWidget 缺省 640x480，或沿用
        上一次（行数不同）的高度，最后一行被裁在窗外（联调实测：拼音
        键盘功能行「符/123/…/确定」看不见）。

        [高度只跟 SLOTS 走，不跟行数走] 见 `_key_h_for_slots` 的说明：
        所有键盘共用同一套槽位 ⇒ 面板尺寸恒定（切换不跳），且末槽就是
        最后一行键，底部不留死白。
        """
        root = self.layout()
        margins = root.contentsMargins()
        key_h = self._key_h_for_slots()
        # 候选带与键同高（一个槽），拼音盘才吃这个槽。
        self._band.setFixedHeight(key_h)
        # [高度必须与实际布局同构，否则末行被压 1px] 真实几何是：
        #   root 边距 + [候选带 + root spacing] + 键区容器边距
        #   + SLOTS*键高 + (SLOTS-1)*键间距
        # 其中 root spacing（带与键区之间那一道）正好替代了"带作为槽"该
        # 占的那道间距，故两种形态的间距总数都是 SLOTS-1。初版漏了容器
        # 边距和 root spacing，实测末行键高被压成 57（应为 58）。
        host_margins = self._rows_box.contentsMargins()
        content = (self.SLOTS * key_h + (self.SLOTS - 1) * self.KEY_GAP
                   + host_margins.top() + host_margins.bottom())
        height = margins.top() + margins.bottom() + content
        self.setFixedHeight(height)
        if self._main is not None and self._main.isVisible():
            handle = self._main.windowHandle()
            width = (handle.frameGeometry().width() if handle is not None
                     else self._main.width())
            self.setFixedWidth(max(360, width))
        self.reposition()

    # ------------------------------------------------------------------
    #                          候选带
    # ------------------------------------------------------------------
    def _clear_band(self):
        while self._band_box.count():
            item = self._band_box.takeAt(0)
            w = item.widget()
            if w is not None:
                w.setParent(None)
                w.deleteLater()

    def _update_band(self):
        """按引擎状态刷新候选带：中/EN + 组串 + 候选 chip + 翻页。"""
        if self._kind != KIND_PINYIN:
            return
        self._clear_band()
        composing = self._engine.composing
        chinese = self._engine.is_chinese()

        mode_btn = self._key_style(
            QPushButton("中" if chinese else "EN"), accent=chinese,
            func=not chinese)
        mode_btn.setObjectName("hsf_kb_mode_chip")
        mode_btn.setFixedWidth(52)
        mode_btn.setToolTip("切换中文/英文输入")
        mode_btn.clicked.connect(lambda: self._act("ime", LBL_IME))
        self._band_box.addWidget(mode_btn)

        if composing:
            text = self._engine.composing_text()
            label = QLabel(text)
            label.setStyleSheet(
                "QLabel { background: transparent; border: none; color: #1A66D0;"
                " font-size: 25px; font-weight: bold; }")
            label.setMinimumWidth(44)
            self._band_box.addWidget(label)
            line = QLabel("|")
            line.setStyleSheet(
                "QLabel { background: transparent; border: none;"
                " color: #B8C0CB; font-size: 22px; }")
            self._band_box.addWidget(line)

            for i, word in enumerate(self._engine.candidates()):
                chip = self._key_style(QPushButton(word), accent=(i == 0))
                chip.setMinimumWidth(56)
                # 带内上下各 4px 边距 ⇒ 芯片高 = 带高 - 8，带高又等于键高。
                chip.setFixedHeight(max(28, self._band.height() - 8))
                chip.clicked.connect(
                    lambda _c=False, idx=self._engine.page_index() * 9 + i:
                    self._commit_candidate(idx))
                self._band_box.addWidget(chip)
            self._band_box.addStretch(1)

            pages = self._engine.page_count()
            if pages > 1:
                prev = self._key_style(QPushButton("‹"), func=True)
                prev.setFixedWidth(40)
                prev.clicked.connect(lambda: self._act("page_prev", ""))
                self._band_box.addWidget(prev)
                info = QLabel("%d/%d" % (self._engine.page_index() + 1, pages))
                info.setStyleSheet(
                    "QLabel { background: transparent; border: none;"
                    " color: #5A6572; font-size: 20px; }")
                self._band_box.addWidget(info)
                nxt = self._key_style(QPushButton("›"), func=True)
                nxt.setFixedWidth(40)
                nxt.clicked.connect(lambda: self._act("page_next", ""))
                self._band_box.addWidget(nxt)
            if self._engine.candidate_count() == 0:
                hint = QLabel("无候选：空格/回车提交原字母")
                hint.setStyleSheet(
                    "QLabel { background: transparent; border: none;"
                    " color: #8B98A5; font-size: 18px; }")
                self._band_box.addStretch(1)
                self._band_box.addWidget(hint)
        else:
            hint = QLabel("拼音输入" if chinese else "英文输入")
            hint.setStyleSheet(
                "QLabel { background: transparent; border: none;"
                " color: #8B98A5; font-size: 17px; }")
            self._band_box.addWidget(hint)
            self._band_box.addStretch(1)

    def _commit_candidate(self, index):
        if self._engine.feed_candidate(index) == pinyin_mod.FEED_COMMITTED:
            self._write(self._engine.commit_string())
        self._update_band()

    # ------------------------------------------------------------------
    #                          按键动作
    # ------------------------------------------------------------------
    def _live_target(self):
        """目标输入控件（还活着且可写才有效）。"""
        if self._adapter is not None and self._adapter.alive():
            return self._target
        return None

    def _write(self, text):
        """把文本写进目标控件（commit / 直接插入的唯一出口）。"""
        if self._adapter is not None and self._adapter.insert(text):
            return True
        return False

    def _type_letter(self, ch):
        """字母键：中文态喂拼音引擎，英文态直写。"""
        if self._kind == KIND_PINYIN:
            feed = self._engine.feed_letter(ch.lower())
            if feed == pinyin_mod.FEED_CONSUMED:
                self._update_band()
                return
        self._write(ch)

    def _type_digit(self, ch):
        """数字键：拼音组串中=选候选（越界吞掉），否则直写。"""
        if self._kind == KIND_PINYIN and self._engine.composing:
            feed = self._engine.feed_digit(int(ch))
            if feed == pinyin_mod.FEED_COMMITTED:
                self._write(self._engine.commit_string())
                self._update_band()
                return
            if feed == pinyin_mod.FEED_CONSUMED:
                return
        self._write(ch)

    def _act(self, action, label=""):
        target = self._adapter
        if action == "t":
            self._write(label)
            return
        if action == "pl":
            self._type_letter(label)
            return
        if action == "digit":
            self._type_digit(label)
            return
        if action == "back":
            self._do_backspace()
            return
        if action == "space":
            self._do_space()
            return
        if action in ("nl", "ok"):
            self._do_confirm()
            return
        if action == "sign":
            self._write("-")
            return
        if action == "left":
            if target is not None:
                target.move_cursor(False)
            return
        if action == "right":
            if target is not None:
                target.move_cursor(True)
            return
        if action == "dismiss":
            self.hide()
            return
        # ---- 布局切换类 ----
        if action == "ime":
            self._engine.set_chinese(not self._engine.is_chinese())
            self._render_page()
            return
        if self._kind == KIND_PINYIN:
            # 拼音键盘：符/123 各自进退（数字页自带「拼音」返回键）。
            if action == "symbol":
                self._pinyin_page = ("letters" if self._pinyin_page == "special"
                                     else "special")
            elif action == "num":
                self._pinyin_page = ("letters" if self._pinyin_page == "numbers"
                                     else "numbers")
            elif action == "shift":
                # 中文态组串恒小写，大写只在英文态有意义。
                self._pinyin_upper = not self._pinyin_upper
            elif action in ("up", "low", "pinyin"):
                self._pinyin_page = "letters"
                self._pinyin_upper = (action == "up")
        else:
            # 字母键盘与 C 的 TextLower/Upper/Special/Number 槽位一一对应：
            # 「符」进 Special（C 的 Special 行 1 就是数字，兼具符号盘），
            # Special 的「abc」回小写——与 XVirtualKeyboard_setMode 口径
            # 完全一致。
            if action == "shift":
                # 字母键盘的 Shift：小写 ↔ 大写锁（C 的 shift 三态在本面板
                # 收敛成两态：点一次大写到锁定，再点回小写）。
                self._mode = ("TextLower" if self._mode == "TextUpper"
                              else "TextUpper")
            elif action == "symbol":
                self._mode = "Special"
            elif action == "up":
                self._mode = "TextUpper"
            elif action == "low":
                self._mode = "TextLower"
            elif action == "num":
                self._mode = "Number"
        if action == "page_prev":
            self._engine.page_prev()
            self._update_band()
            return
        if action == "page_next":
            self._engine.page_next()
            self._update_band()
            return
        self._render_page()                    # 布局切换：重画

    def _do_backspace(self):
        """退格：拼音组串非空先删字母，空了才删编辑框。"""
        if self._kind == KIND_PINYIN:
            feed = self._engine.feed_backspace()
            if feed == pinyin_mod.FEED_CONSUMED:
                self._update_band()
                return
        if self._adapter is not None:
            self._adapter.backspace()

    def _do_space(self):
        """空格：拼音组串中=提交首候选（无候选=原字母串）。"""
        if self._kind == KIND_PINYIN and self._engine.composing:
            feed = self._engine.feed_commit_first()
            if feed == pinyin_mod.FEED_COMMITTED:
                self._write(self._engine.commit_string())
                self._update_band()
                return
        self._write(" ")

    def _do_confirm(self):
        """确定/换行：组串有内容则提交原字母串，然后提交数值并收起面板。"""
        if self._kind == KIND_PINYIN and self._engine.composing:
            if self._engine.feed_commit_raw() == pinyin_mod.FEED_COMMITTED:
                self._write(self._engine.commit_string())
        # [数值框] 确定 = 敲回车：把文本最终解析成 value（每键强解析会把
        # 中间态 clamp 掉，见 _InputTarget._sync）。
        if self._adapter is not None:
            self._adapter.commit()
        # [口径] 客户端全部输入控件是单行：确定/换行统一收起面板
        # （多行控件接入时在此按控件类型分流）。
        self.hide()

    # ------------------------------------------------------------------
    #                    目标控件与显示/收起
    # ------------------------------------------------------------------
    def set_main_window(self, window):
        self._main = window
        self.reposition()

    def reposition(self):
        """停靠到主窗底部（同屏同宽；顶层窗口用全局坐标）。"""
        if self._main is None or not self._main.isVisible():
            return
        handle = self._main.windowHandle()
        if handle is not None:
            frame = handle.frameGeometry()
        else:                               # 离屏/未显示：退回控件几何
            geo = self._main.geometry()
            frame = geo
            frame.moveTo(self._main.mapToGlobal(geo.topLeft()))
        self.setFixedWidth(max(320, frame.width()))
        self.move(frame.x(), frame.y() + frame.height() - self.height())

    def bind(self, widget):
        """把键盘绑定到某个输入控件（决定 kind/布局并弹出）。"""
        # [先存旧目标] 下面 `self._target = widget` 会覆盖，全选判据要用旧值
        # 判断"是不是换了输入框"。
        prev_target = self._target
        was_hidden = not self.isVisible()
        # 换目标时先提交上一个数值框的未解析文本（见 _InputTarget._sync）。
        if self._adapter is not None and self._target is not widget:
            self._adapter.commit()
        self._target = widget
        self._adapter = _InputTarget(widget)
        kind, layout = keyboard_kind_of(widget)
        changed = (kind != self._kind) or (layout
                                           and layout != self._numeric_layout)
        self._kind = kind
        if layout:
            self._numeric_layout = layout
        # 换目标/换类型时清组串：候选属于上一个框，带过去会误上屏。
        self._engine.reset()
        self._pinyin_page = "letters"
        self._pinyin_upper = False
        if kind == KIND_ALPHA:
            self._mode = "TextLower"
        self._render_page()
        if not self.isVisible() or changed:
            self.reposition()
        self.show()
        self.raise_()
        # [弹出即全选] 用户 2026-11-06 口径：「变频器界面输入框弹出键盘后
        # 默认选中全部，这样可以***直接重新完整输入一个值***」。放在 show
        # 之后是为了不干扰 Qt 的焦点处理（先让目标控件拿到焦点，再全选）。
        #
        # [判据] 只在"面板原本不可见"或"换了目标/键盘类型"时全选：
        # 同一目标上连续按键（组串、翻页、切中英）会走 reflow 但**不该**
        # 再次全选 —— 否则每按一个字母都把已有内容全选，输入变成覆盖。
        if was_hidden or changed or widget is not prev_target:
            self._adapter.select_all()

    # ------------------------------------------------------------------
    #                    应用级焦点事件过滤（安装入口）
    # ------------------------------------------------------------------
    def attach(self, app, main_window):
        """接管应用焦点事件：输入类控件获得焦点 → 自动弹出键盘。"""
        self.set_main_window(main_window)
        app.installEventFilter(self)
        # [装配瞬间已在焦点的输入框] 安装过滤器时若已有输入框持有焦点，
        # FocusIn 早已发生过、不会重放——不在这里补一次，面板就得等用户
        # 再点一下才出来（离屏测试与「登录窗预先聚焦用户名」都是这个形态）。
        QTimer.singleShot(0, self._review_focus)
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
        if event.type() == QEvent.MouseButtonPress and isinstance(obj, QWidget):
            # [按下位置驱动] 与 C 端 notifyPress 同口径：按到哪里就绑哪里。
            #
            # 为什么不能只等 FocusIn：面板是 WindowStaysOnTop 的独立顶层
            # 窗口，它 show 之后宿主窗的激活态/焦点可能被清空；此时点另一
            # 个输入框，Qt 未必把焦点转过去（联调实测：点了第二个框，面板
            # 还绑在第一个框上，键盘类型也不切）。按下事件必然到达（点击
            # 总有落点），在这里显式 setFocus + 重绑，焦点链路就与点击位
            # 置一致了。
            if self._is_own_widget(obj):
                return False
            pressed = _resolve_input_widget(obj)
            if pressed is None:
                # [事件冒泡] 输入框不处理鼠标按下时，Qt 会把同一个事件再
                # 送给父控件——应用级过滤器于是又看到一次「落在非输入区」
                # 的按下（实测：点 spinbox 弹出后立刻被这一拍关掉）。判据
                # 用全局落点：还在当前目标矩形内，就说明是同一击的冒泡。
                if self.isVisible() and not self._hit_current_target(event):
                    self._close()
                return False
            if pressed.property(NO_SOFT_KBD) or \
                    pressed.property(KEYBOARD_KIND_PROP) == KIND_NONE:
                if self.isVisible():
                    self._close()       # 排除标记的控件：收起，别叠罗汉
                return False
            if pressed is not self._target or not self.isVisible():
                # 上一个数值框可能还有未解析的文本：换目标前先提交。
                if self._adapter is not None and pressed is not self._target:
                    self._adapter.commit()
                pressed.setFocus(Qt.MouseFocusReason)
                self.bind(pressed)
            return False
        if event.type() == QEvent.FocusIn and isinstance(obj, QWidget):
            widget = _resolve_input_widget(obj)
            if widget is None:
                if not self._is_own_widget(obj):
                    QTimer.singleShot(0, self._review_focus)
                return False
            if widget.property(NO_SOFT_KBD) or \
                    widget.property(KEYBOARD_KIND_PROP) == KIND_NONE:
                # 明确排除的控件（如日期浮层）：聚焦它时把软键盘收起，
                # 否则日期浮层和软键盘叠罗汉（实锅）。
                QTimer.singleShot(0, self._review_focus)
                return False
            if widget is self._target:
                # 同一目标的第二次 FocusIn（spinbox 内部 lineEdit 转发等）：
                # 只需确保面板还在，不重置组串。
                if not self.isVisible():
                    self.bind(widget)
                return False
            self.bind(widget)
        elif event.type() == QEvent.FocusOut and obj is self._target:
            QTimer.singleShot(0, self._review_focus)
        return False

    def _review_focus(self):
        """焦点落定后复查：焦点明确跑到别的控件上才收起。

        [为什么不「焦点为空就收起」] 面板自己是 Tool 窗口，show/raise 在
        某些平台（含 Qt offscreen 插件）会把窗口级焦点置空；真机触摸点击
        非可聚焦区域也会置空。这时收起键盘等于「一弹就收」——上批次真机
        前就在离屏测试里踩到过。焦点为空一律**不收起**（保持可见），真正
        的收起交给人：点面板外（按下位置驱动）或点「收起」键。
        """
        fw = QApplication.focusWidget()
        widget = _resolve_input_widget(fw)
        if widget is not None and not widget.property(NO_SOFT_KBD) and \
                widget.property(KEYBOARD_KIND_PROP) != KIND_NONE:
            if widget is not self._target:
                self.bind(widget)
            return
        if fw is None or self._is_own_widget(fw):
            return                          # 焦点为空/在面板自身：保持
        self._close()

    def _close(self):
        """收起面板并清目标/组串。"""
        # [数值框] 收起前把文本最终解析成 value（等价失焦提交）——不然
        # 用户点了收起，数值框里是新数字但 value 还是旧值。
        if self._adapter is not None:
            self._adapter.commit()
        self._target = None
        self._adapter = None
        self._engine.reset()
        self.hide()

    def _hit_current_target(self, event):
        """这次按下是否落在当前目标（或其内部子控件）的矩形里。

        [为什么用坐标而不是对象] 鼠标按下事件未被输入框消费时会被 Qt
        冒泡给父容器，过滤器会再收到一次「落点是非输入控件」——用对象判
        断会把这一击误当成「点了别处」。坐标判据对冒泡免疫。
        """
        if self._target is None:
            return False
        try:
            global_pos = event.globalPos()
            top = self._target.window()
            local = top.mapFromGlobal(global_pos)
            rect = self._target.rect()
            target_topleft = self._target.mapTo(top, rect.topLeft())
            target_rect = rect.translated(target_topleft)
            return target_rect.contains(local)
        except (AttributeError, RuntimeError):
            return False

    def _is_descendant(self, widget, ancestor):
        """widget 是否在 ancestor 之内（spinbox 的 lineEdit 等内部子控件）。"""
        walker = widget
        while walker is not None:
            if walker is ancestor:
                return True
            walker = walker.parentWidget()
        return False

    def _is_own_widget(self, widget):
        """widget 是否在软键盘面板自身之内（自己的按键不算"焦点离开"）。"""
        walker = widget
        while walker is not None:
            if walker is self:
                return True
            walker = walker.parentWidget()
        return False


def _resolve_input_widget(widget):
    """焦点对象 → 真正接受文本输入的控件（向上解析 spinbox 内部编辑器）。"""
    walker = widget
    while walker is not None:
        if is_text_input(walker):
            return walker
        walker = walker.parentWidget()
    return None


def attach_soft_keyboard(main_window):
    """主窗装配入口：按触摸环境开关建软键盘；关闭时返回 None。"""
    if not touch_default_enabled():
        return None
    kb = SoftKeyboard()
    kb.attach(QApplication.instance(), main_window)
    return kb
