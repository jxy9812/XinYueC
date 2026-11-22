# -*- coding: utf-8 -*-
"""数据三页（历史数据 / 日志 / 报警）**共用的工具条**与两个通用输入控件。

[为什么抽出来]
    三页的工具条此前是**各写一份**的：历史页三行全套（筛选行 + 分页行 +
    视图行），日志页缺「列多选」，报警页缺「关键字 / 分页 / 滚动锁定」且
    「列」还是单选 ⇒ 三页长得不一样，用户每页要重新学一遍，改一处要改
    三处（漏一处就是"两页不一致"的现场）。
    现在一份实现、三页按**能力开关**裁剪控件，形态统一。

[三行结构（照历史页逐字同款）]
    ① 筛选行：[页面特有下拉] 起始 结束 列(多选) 关键字 筛选 清除筛选
    ② 分页行：每页 上一页 第N/M页 下一页 总数统计 计数 刷新
    ③ 视图行：折线列(多选) 表格视图 图表视图 [页面附加控件] 滚动锁定 导出数据

[按页裁剪 = 不许留死控件]
    没有的能力**就不出现控件**（项目铁律：界面不许留占位/选了不兑现的
    选项）：
      · 历史页 = 全套；
      · 日志页 = 无折线列/双视图/滚动锁定（没有图表、也没有自动刷新 ⇒
        "滚动锁定"是死控件）；类别下拉走「页面特有下拉」槽；
      · 报警页 = 无折线列/双视图/刷新（推送来的数据没有"重查"动作），
        有客户端内部分页 + 滚动锁定；类型下拉走「页面特有下拉」槽。

[页面怎么用]
    控件只负责**长控件 + 发信号**，不持有业务状态（分页/筛选/图表数据
    仍在页面里）⇒ 页面侧把控件属性**别名**成自己原有的属性名
    （`self._f_text = tb.keyword_edit`），既有页面逻辑与既有回归锁
    一行都不用改。

[依赖方向]
    TimePickerEdit / MultiCheckCombo 从 pages_other.py 搬到这里（它们是
    通用控件，页面模块不该持有它们）；pages_other.py 再从本模块 re-export
    ⇒ 旧导入路径 `from hsfsrv_client.pages_other import MultiCheckCombo`
    仍然可用（测试里就是这么 import 的）。
"""
from PyQt5.QtCore import QDateTime, QPoint, QPointF, QRect, Qt, pyqtSignal
from PyQt5.QtGui import (QColor, QFont, QPainter, QPen,
                         QPolygonF, QStandardItem, QStandardItemModel)
from PyQt5.QtWidgets import (QCheckBox, QComboBox, QDateTimeEdit, QFrame,
                             QHBoxLayout, QLabel, QLineEdit, QPushButton,
                             QScrollArea, QVBoxLayout, QWidget)

from . import theme
from .page_base import fit_text_width, label_group, wrap_row

#: 时间输入框的**最宽候选**之一：历史页入页会把服务端最前一条的完整时刻
#: 回填进「起始」框（"2026-11-11 15:04:48"）⇒ 框必须装得下它。
_TIME_STAMP_SAMPLE = "2026-11-11 15:04:48"

#: 时间输入框左右留白（QLineEdit 的 QSS：padding 5px 8px + border 1px
#: ⇒ 左右共 8×2 + 1×2 = 18px；与 theme 的输入框样式同源）。
_TIME_EDIT_PAD_H = 18


def _fit_time_edit_width(edit):
    """按**当前字体实测**给时间输入框宽度，不写死像素（尺寸铁律）。

    [为什么] 历史页入页会把「服务端最前一条」的完整时刻
    （``2026-11-11 15:04:48``）回填进「起始」框；1.5× 界面缩放下这串
    文字约 330px，原 ``setMaximumWidth(120)`` 把它压成 ``1 15:04:48``
    ——看着像数据坏了。字号/缩放会变 ⇒ 宽度必须由 QFontMetrics 量出来，
    不能拍脑袋给常数。

    [为什么按「最宽候选」量] 框里可能出现的文字有**两种**：①回填的完整
    时刻串；②占位符提示。原来只按①量 ⇒ ②比①长时占位符被省略号截断
    （实测「yyyy-MM-dd HH:MM 或 HH:MM」要 408px、框只有 339px ⇒ 用户
    看到的是「yyyy-MM-dd HH:MM 或 HH:…」）。⇒ 两个都量，取最宽的。

    [为什么只设最小宽] 不用 setFixedWidth/setMaximumWidth 把宽度钉死
    （尺寸铁律：宽度按字体实测，但**不钉死**）——最小宽保证「不裁字」，
    多出来的空间交给布局；工具条的行是会换行的 ``wrap_row`` ⇒ 加宽
    只会让这一行折行，不会把别的控件挤出界面。
    """
    w = fit_text_width(edit, (_TIME_STAMP_SAMPLE, edit.placeholderText()),
                       extra=_TIME_EDIT_PAD_H)
    edit.setMinimumWidth(max(140, w))


# ============================================================================
#                    可复用输入控件（时间选择 / 多选下拉）
# ============================================================================

class TimePickerEdit(QLineEdit):
    """日期时间输入框：**单击弹带日历的选择浮层**，也允许手输。

    接受三种口径（历史页筛选两侧通用，归一见 _norm_stamp）：
        HH:MM[:SS]              （当天）
        MM-dd HH:MM[:SS]        （当年，如 "11-05 23:09"）
        yyyy-MM-dd [HH:MM[:SS]]
    """

    timePicked = pyqtSignal(str)          # 归一化 "yyyy-MM-dd HH:MM:SS"

    def __init__(self, placeholder="", parent=None):
        QLineEdit.__init__(self, parent)
        self.setPlaceholderText(placeholder)
        # [格式说明挂 tooltip] 占位符只放**最常见**的一种写法——框宽有限，
        # 把三种格式都塞进占位符会被省略号截断（实测「yyyy-MM-dd HH:MM 或
        # HH:MM」要 408px、框只有 339px ⇒ 用户看到「yyyy-MM-dd HH:MM 或
        # HH:…」）；完整格式说明挂 tooltip ⇒ 信息照样可达。
        self.setToolTip("可填：yyyy-MM-dd HH:MM[:SS]、MM-dd HH:MM[:SS] 或 "
                        "HH:MM[:SS]（当天）；也可点框弹日历选日期")
        # [排除软键盘] 本控件单击弹的是自己的日期选择浮层——软键盘再弹
        # 一层就叠罗汉了（softkeyboard.NO_SOFT_KBD 排除标记）。
        from .softkeyboard import NO_SOFT_KBD
        self.setProperty(NO_SOFT_KBD, True)

    def mousePressEvent(self, event):
        QLineEdit.mousePressEvent(self, event)
        self.show_picker()

    def show_picker(self):
        popup = QFrame(self, Qt.Popup)
        popup.setStyleSheet("QFrame { background: #FFFFFF; }")
        lay = QVBoxLayout(popup)
        lay.setContentsMargins(12, 12, 12, 12)
        lay.setSpacing(8)
        hint = QLabel("日期时间（点日历图标选日期）")
        hint.setStyleSheet("QLabel { color: palette(mid); }")
        lay.addWidget(hint)
        edit = QDateTimeEdit(QDateTime.currentDateTime())
        edit.setCalendarPopup(True)           # 内置日历下拉
        edit.setDisplayFormat("yyyy-MM-dd HH:mm:ss")
        lay.addWidget(edit)
        row = QHBoxLayout()
        btn_ok = QPushButton("确定")
        btn_clear = QPushButton("清空")
        row.addWidget(btn_ok)
        row.addWidget(btn_clear)
        lay.addLayout(row)

        def _commit():
            self._set_from_picker(edit.dateTime())
            popup.close()
            # [deleteLater 2026-11-16 审计] 弹层是每次 new 的 `QFrame(self,
            # Qt.Popup)`、close 只藏不析构 ⇒ 频繁点时间框，浮层控件（含
            # QDateTimeEdit）作为子对象持续累积（MultiCheckCombo 的
            # _close_popup 有 deleteLater，这里漏了）。
            popup.deleteLater()

        def _clear():
            self.clear()
            popup.close()
            popup.deleteLater()

        btn_ok.clicked.connect(_commit)
        btn_clear.clicked.connect(_clear)
        popup.move(self.mapToGlobal(QPoint(0, self.height())))
        popup.show()

    def _set_from_picker(self, qdatetime):
        """浮层确定 → 写入文本（独立成方法：无头测试直接调这里）。"""
        text = qdatetime.toString("yyyy-MM-dd HH:mm:ss")
        self.setText(text)
        self.timePicked.emit(text)


class CheckRow(QWidget):
    """浮层里的一行：**方框 + 对勾 + 文字**（指示器自绘，不用 QCheckBox）。

    [为什么不用 QCheckBox —— 用户实锅 2026-11-17]
        QSS 一旦给 ``::indicator`` 设了 border/background，Qt 的样式引擎就
        **不再画原生指示器**（连对勾一起没了）⇒ 勾选态只剩一个"空方框"，
        用户看着像"压根没选上"（远端设置 → 自动模式配置 → 启动设备多选：
        "在选中的时候没有勾，导致看起来一直以为没选上"）。
        而"给 QCheckBox 补一张对勾图片"又与本项目**不引入图片资源**的口径
        冲突（板端字体/图片都可能缺）⇒ 指示器改**自绘**：方框与对勾折线
        全部 QPainter 画出来 ⇒ 任何主题、任何板端都一致。

    [行为口径不变]
        · 点一行 = 翻转勾选（与原 QCheckBox 同款；点项不收浮层）；
        · 对外 API 与 QCheckBox 同名（``isChecked`` / ``setChecked`` /
          ``toggled`` 信号 / ``text``）⇒ 既有调用方与回归锁不用改。

    [尺寸]
        行高 34px（与 MultiCheckCombo.ITEM_H 同口径，触摸屏够点）；
        方框 17×17（与主题 QSS 的 ``::indicator`` 尺寸一致），
        左边距 8px，文字距方框 8px。
    """

    #: 勾选态变化时发（与 QCheckBox.toggled 同参）。
    toggled = pyqtSignal(bool)

    #: 方框边长（px）。
    BOX = 17
    #: 方框左边距（px）。
    BOX_X = 8
    #: 方框与文字的间距（px）。
    GAP = 8
    #: 行高（px）。
    ROW_H = 34
    #: 文字字号（px；沿用原浮层 QSS 的 font-size: 22px）。
    FONT_PX = 22

    def __init__(self, label, checked=False, parent=None):
        QWidget.__init__(self, parent)
        self._label = str(label)
        self._checked = bool(checked)
        self._hover = False
        self.setMouseTracking(True)
        self.setAttribute(Qt.WA_Hover, True)
        self.setMinimumHeight(self.ROW_H)
        self.setCursor(Qt.PointingHandCursor)
        f = QFont(self.font())
        f.setPixelSize(self.FONT_PX)
        self.setFont(f)

    # ---- 对外 API（与 QCheckBox 同名）----
    def text(self):
        return self._label

    def isChecked(self):
        return self._checked

    def setChecked(self, on):
        """设勾选态（同值不重复发信号，与 QCheckBox 一致）。"""
        on = bool(on)
        if on == self._checked:
            return
        self._checked = on
        self.update()
        self.toggled.emit(on)

    # ---- 交互 ----
    def mousePressEvent(self, event):
        if event.button() == Qt.LeftButton:
            self.setChecked(not self._checked)
        event.accept()

    def enterEvent(self, event):
        self._hover = True
        self.update()
        QWidget.enterEvent(self, event)

    def leaveEvent(self, event):
        self._hover = False
        self.update()
        QWidget.leaveEvent(self, event)

    # ---- 绘制 ----
    def _colors(self):
        """(框底, 框线, 对勾, 文字) —— 取当前主题的令牌。"""
        tok = (theme.current_theme() or {}).get("tokens") or {}
        return (tok.get("white", theme.CLR_WHITE),
                tok.get("accent_line", theme.CLR_ACCENT_LINE),
                tok.get("accent_fill", theme.CLR_ACCENT_FILL),
                tok.get("text", "#1A1A1A"))

    def paintEvent(self, event):
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing, True)
        bg, accent_line, accent_fill, text_clr = self._colors()
        tok = (theme.current_theme() or {}).get("tokens") or {}
        border_clr = accent_fill if self._hover else \
            tok.get("border", theme.CLR_BORDER)
        # 背景铺一层（浮层底色）——否则重绘时旧像素会残留。
        p.fillRect(self.rect(), QColor(bg))

        top = (self.height() - self.BOX) // 2
        box = QRect(self.BOX_X, top, self.BOX, self.BOX)

        # 方框（选中时用强调色描边）
        p.setBrush(QColor(bg))
        p.setPen(QPen(QColor(accent_line if self._checked else border_clr), 1))
        p.drawRoundedRect(box, 3, 3)

        # 对勾（自绘折线，不依赖图片/字体）
        if self._checked:
            pen = QPen(QColor(accent_line), 2)
            pen.setCapStyle(Qt.RoundCap)
            pen.setJoinStyle(Qt.RoundJoin)
            p.setPen(pen)
            x, y = box.left(), box.top()
            path = [(x + 4.0, y + 8.5), (x + 7.5, y + 12.0), (x + 13.0, y + 5.0)]
            p.drawPolyline(QPolygonF([QPointF(px, py) for px, py in path]))

        # 文字
        p.setPen(QPen(QColor(text_clr)))
        text_rect = QRect(box.right() + self.GAP + 1, 0,
                          max(0, self.width() - box.right() - self.GAP - 2),
                          self.height())
        p.drawText(text_rect, Qt.AlignVCenter | Qt.AlignLeft, self._label)


class MultiCheckCombo(QPushButton):
    """多选下拉：按钮 + 弹出勾选列表，可勾任意多个；**点项不收起，点外面才收**。

    [两种用法口径]
      · **列显示**（历史页/日志页/报警页）—— 勾中的列才显示，**默认全部
        勾选**；至少要留一列（全不勾等于"把表清空"，没有意义且让人以为
        界面坏了）。
      · **筛选列**（旧用法）—— 空选 = 不过滤（全部列）。
    两种口径差在"空选"的含义上，故由构造参数 `initial` / `min_checked`
    显式声明，不在控件里猜。

    [为什么不用 QComboBox] 原来是 editable QComboBox + view().pressed 自己
    翻转勾选，实测**两个硬伤**（用户反馈："可以取消列但是勾不回去了，
    而且单击一次多选框就消失了"）：
      1. 点列表项会走 QComboBox 的"选中"路径 → `currentIndex` 变化、
         lineEdit 被覆盖成该项文本、popup 自动收起。再点开时点的还是
         "当前项"，于是**取消掉的项怎么也勾不回来**（勾选状态在跑，但
         按钮文字显示的是一个单项名，看着像没生效）。
      2. QComboBox 的 popup 是"选中即关闭"语义，与"连点多选"根本冲突
         —— 每点一下都要重新点开。
    QComboBox 没有"不选中、纯当复选框列表"的开关，所以改成
    `QPushButton + QFrame(Qt.Popup)`：Popup 窗口的关闭条件由我们控制
    （点浮层外自动收，正是用户要求的口径），列表项是纯 QCheckBox 行，
    点击只改勾选、不触发任何"选中"逻辑。

    [下拉箭头的实现] 不用 QComboBox 就没有内建箭头，故在按钮文本尾部
    补一个"▾"（Unicode，板端字体有），右侧留白靠 QSS padding。
    """

    selectionChanged = pyqtSignal()

    class _ReadOnlyText(object):
        """`lineEdit()` 的兼容返回：只暴露 `text()`（汇总文本，只读）。"""

        __slots__ = ("_owner",)

        def __init__(self, owner):
            self._owner = owner

        def text(self):
            return self._owner._summary_text()

    #: 浮层里每行的高度（触摸屏口径：够手指点，又不至于列表太长）。
    ITEM_H = 34
    #: 浮层最大高度（超出后列表内滚动）—— 15 列时约 510px，矮屏要能滚。
    POPUP_MAX_H = 420

    def __init__(self, items, all_text="全部列", parent=None,
                 initial="none", min_checked=0, empty_text=None):
        """:param initial: "none"=全不勾 / "all"=全勾（列显示用后者）
        :param min_checked: 至少保留几个勾选（0 = 允许全不勾）
        :param empty_text: 全不勾时的按钮文字（None = 沿用 all_text）。

        [为什么要 empty_text] min_checked=0 时空选与全选的汇总文本**都**
        是 all_text（旧逻辑共用一个兜底），两种状态在按钮上无法区分——
        自动模式配置的「启动设备」需要"未选择 / 全部设备 / 具名列表"三种
        可辨状态，故给空选一个独立文案；不传时行为与旧版逐字一致。
        """
        QPushButton.__init__(self, parent)
        self._all_text = all_text
        self._empty_text = empty_text
        self._min_checked = max(0, int(min_checked))
        self._labels = [str(t) for t in items]
        self._checked = [initial == "all"] * len(self._labels)
        self._text_view = MultiCheckCombo._ReadOnlyText(self)
        self._syncing = False
        #: 保留 `_model` 兼容旧调用方/测试（QStandardItemModel，勾选态同步）。
        self._model = QStandardItemModel(self)
        for text in self._labels:
            item = QStandardItem(text)
            item.setCheckable(True)
            item.setCheckState(Qt.Checked if initial == "all" else Qt.Unchecked)
            self._model.appendRow(item)
        self._model.itemChanged.connect(self._on_model_item_changed)
        self._popup = None
        self.setCursor(Qt.PointingHandCursor)
        self.clicked.connect(self._toggle_popup)
        self._sync_text()

    # ---- 兼容 QComboBox 时代的取文本入口 ----
    def lineEdit(self):
        """兼容旧调用（读汇总文本）：返回只读文本视图（`.text()`）。

        [为什么不返回 self] 返回按钮自身的话 `lineEdit().setText(...)`
        会直接改按钮显示、绕过 `_checked` 状态 —— 汇总文本必须由勾选态
        派生，不能被外部写进来（否则界面显示与实际筛选条件不一致）。
        """
        return self._text_view

    def text(self):
        return QPushButton.text(self)

    # ---- 勾选状态 ----

    def _on_model_item_changed(self, _item):
        """外部直接改 `_model` 勾选态时同步内部状态（测试/将来代码用）。"""
        if getattr(self, "_syncing", False):
            return
        self._checked = [self._model.item(i).checkState() == Qt.Checked
                         for i in range(self._model.rowCount())]
        self._sync_text()
        self._refresh_popup_checks()

    def _set_checked(self, index, checked):
        """改单项勾选（受 min_checked 约束）。返回是否真的改了。"""
        if 0 <= index < len(self._checked):
            if not checked and self._min_checked:
                # [至少留一列] 不允许把最后一列也取消：表格会变成空表头，
                # 用户看着像界面坏了。直接忽略这次点击（勾选态不变）。
                if sum(self._checked) <= self._min_checked:
                    return False
            if self._checked[index] == checked:
                return False
            self._checked[index] = checked
        else:
            return False
        self._syncing = True
        try:
            self._model.item(index).setCheckState(
                Qt.Checked if checked else Qt.Unchecked)
        finally:
            self._syncing = False
        self._sync_text()
        return True

    def _toggle_item(self, index):
        """兼容旧调用（测试直接点某项）：翻转单项勾选。"""
        idx = index.row() if hasattr(index, "row") else int(index)
        if 0 <= idx < len(self._checked):
            self._set_checked(idx, not self._checked[idx])

    def checked_indexes(self):
        """当前勾中的选项下标列表（0 基；空列表 = 未勾任何项）。"""
        return [i for i, on in enumerate(self._checked) if on]

    def set_checked_indexes(self, indexes):
        """整体重设勾选（给定下标集合，其余全清；回显服务端配置用）。

        [与 _set_checked 的分工] 后者是单项翻转（受 min_checked 约束）；
        这里是"整体回显"语义——服务端下发的配置就是事实，不再叠加
        min_checked 的保护性拦截。
        """
        want = set(int(i) for i in indexes)
        self._checked = [i in want for i in range(len(self._labels))]
        self._push_model()
        self._sync_text()

    def clear_all(self):
        """全部取消。**受 min_checked 约束**：留不下就保留开头几项。"""
        keep = self._min_checked
        self._checked = [i < keep for i in range(len(self._labels))]
        self._push_model()
        self._sync_text()

    def check_all(self):
        """全部勾选（恢复默认视图）。"""
        self._checked = [True] * len(self._labels)
        self._push_model()
        self._sync_text()

    def _push_model(self):
        self._syncing = True
        try:
            for i, on in enumerate(self._checked):
                self._model.item(i).setCheckState(
                    Qt.Checked if on else Qt.Unchecked)
        finally:
            self._syncing = False

    def _summary_text(self):
        names = [t for t, on in zip(self._labels, self._checked) if on]
        if names and len(names) == len(self._labels):
            return ("%s（全部）" % self._all_text if self._min_checked
                    else self._all_text)
        if names:
            return "、".join(names)
        return self._empty_text or self._all_text

    def _sync_text(self):
        self.setText(self._summary_text() + "  ▾")
        self.selectionChanged.emit()

    # ---- 弹出层 ----

    def _toggle_popup(self):
        if self._popup is not None and self._popup.isVisible():
            self._close_popup()
        else:
            self._open_popup()

    def _close_popup(self):
        if self._popup is not None:
            self._popup.hide()
            self._popup.deleteLater()
            self._popup = None
        self.setDown(False)

    def _open_popup(self):
        # [Qt.Popup 的关闭语义正是用户要的] 点浮层外的任何地方 → Qt 自己
        # 关闭这个 popup（等价"失去焦点/点外面才收"）；点浮层内部**不会**
        # 关，所以可以连续勾多个。不需要自己装事件过滤器。
        popup = QFrame(self, Qt.Popup)
        popup.setObjectName("HsfMultiPopup")
        # [底色取主题令牌] CheckRow 自绘时按同一底色铺底 ⇒ 行与浮层无缝。
        _tok = (theme.current_theme() or {}).get("tokens") or {}
        popup.setStyleSheet(
            "QFrame#HsfMultiPopup { background: %s;"
            " border: 1px solid %s; }"
            % (_tok.get("white", theme.CLR_WHITE),
               _tok.get("border", theme.CLR_BORDER)))
        lay = QVBoxLayout(popup)
        lay.setContentsMargins(6, 6, 6, 6)
        lay.setSpacing(0)

        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setFrameShape(QFrame.NoFrame)
        scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarAlwaysOff)
        inner = QWidget()
        inner.setStyleSheet("QWidget { background: transparent; }")
        box = QVBoxLayout(inner)
        box.setContentsMargins(0, 0, 0, 0)
        box.setSpacing(0)
        self._checks = []
        for i, label in enumerate(self._labels):
            # [指示器自绘 2026-11-17] 用 CheckRow 而不是 QCheckBox：QSS 给
            # ::indicator 设了边框/底色后 Qt 不再画原生指示器 ⇒ 勾选态只剩
            # 空方框，用户看着像"没选上"（用户实报）。CheckRow 自绘方框+对勾。
            cb = CheckRow(label, self._checked[i])
            # 点击只改勾选，**不收 popup、不动任何"当前项"**
            cb.toggled.connect(
                lambda on, idx=i: self._on_check_toggled(idx, on))
            box.addWidget(cb)
            self._checks.append(cb)
        box.addStretch(1)
        scroll.setWidget(inner)
        lay.addWidget(scroll)

        # 高度：按项数算，超上限就滚动（15 列 × 34 ≈ 510 → 压到 420 滚动）
        height = min(self.POPUP_MAX_H,
                     self.ITEM_H * len(self._labels) + 14)
        width = max(self.width(), 200)
        popup.resize(width, height)
        popup.move(self.mapToGlobal(QPoint(0, self.height() + 2)))
        popup.show()
        self._popup = popup
        self.setDown(True)
        # [析构期要解绑] popup 的 destroyed 可能在按钮自身被 C++ 销毁之后
        # 才发出（父对象链：popup 是按钮的子对象），那时回调里碰 self 会
        # RuntimeError: wrapped C/C++ object has been deleted（实测）。
        # 连接挂到按钮自己的 destroyed 上先断开，或让回调判一次存活。
        popup.destroyed.connect(self._on_popup_gone)

    def _on_popup_gone(self, *_args):
        """popup 被 Qt 关掉（点外面/Esc）：清引用并复位按钮的按下态。"""
        self._popup = None
        try:
            self.setDown(False)
        except RuntimeError:
            # 按钮本身已析构（关窗/切页销毁），无需复位界面状态。
            pass

    def _on_check_toggled(self, index, checked):
        # 至少留一列的约束在 _set_checked 里；被拒时把勾选框回弹。
        if not self._set_checked(index, checked):
            self._syncing = True
            try:
                if hasattr(self, "_checks") and index < len(self._checks):
                    self._checks[index].setChecked(self._checked[index])
            finally:
                self._syncing = False

    def _refresh_popup_checks(self):
        """外部改勾选（clear_all/check_all/_model）时同步浮层里的勾选框。"""
        if self._popup is None or not hasattr(self, "_checks"):
            return
        self._syncing = True
        try:
            for i, cb in enumerate(self._checks):
                if i < len(self._checked):
                    cb.setChecked(self._checked[i])
        finally:
            self._syncing = False

    def hideEvent(self, event):
        self._close_popup()
        QPushButton.hideEvent(self, event)


# ============================================================================
#                          数据三页共用的工具条
# ============================================================================

class _ToolbarHandle(QWidget):
    """工具条顶部的**折叠把手**（一条分隔线 + 可点的「收起/展开」）。

    [为什么是它 2026-11-19 用户口径「表格的上面与这些下面中间是不是可以
    增加一个分割条，可以折叠红框中的功能区，这样浏览数据是不是更方便」]
    数据三页的工具条有 3~4 行（时间窗 / 列+关键字 / 分页 / 视图+导出），
    在 1280×800 @150% 的板端上吃掉约 1/5 屏 ⇒ 折叠后表格大一屏。
    [触摸口径] 整条把手都是点击区（高 26px、字号 15px）⇒ 手指按得中；
    折叠态显示「▸ 展开筛选」、展开态显示「▾ 收起筛选」。
    """

    def __init__(self, on_toggle, parent=None):
        QWidget.__init__(self, parent)
        self._on_toggle = on_toggle
        self.setObjectName("HsfToolbarHandle")
        self.setFixedHeight(26)
        lay = QHBoxLayout(self)
        lay.setContentsMargins(0, 0, 0, 0)
        lay.setSpacing(6)
        self.btn = QPushButton("▾ 收起筛选")
        self.btn.setFlat(True)
        self.btn.setCursor(Qt.PointingHandCursor)
        self.btn.setStyleSheet(
            "QPushButton { background: transparent; border: none;"
            " color: %s; font-size: 15px; padding: 2px 6px;"
            " text-align: left; }"
            "QPushButton:hover { background-color: %s; border-radius: 6px; }"
            % (theme._tok("text_dim"), theme._tok("hover_bg")))
        self.btn.clicked.connect(self._clicked)
        lay.addWidget(self.btn)
        lay.addStretch(1)
        # 顶部分隔线（把手与筛选区之间的一条淡线）
        self.setStyleSheet(
            "QWidget#HsfToolbarHandle { border-top: 1px solid %s; }"
            % theme._tok("border_soft"))

    def _clicked(self):
        self._on_toggle()

    def set_collapsed(self, collapsed):
        """折叠态文案切换（▸ 展开 / ▾ 收起）。"""
        self.btn.setText("▸ 展开筛选" if collapsed else "▾ 收起筛选")

class DataPageToolbar(QWidget):
    """数据三页共用的工具条（三行；按页裁剪控件，形态与历史页一致）。

    [职责边界] 控件只做两件事：**长控件** + **发信号**。分页/筛选/数据的
    业务状态仍在页面里（控件不持有、不推断）⇒ 页面把控件属性别名成自己
    原有的属性名即可，既有页面逻辑与既有回归锁一行都不用改。

    [信号]
        filterRequested / clearRequested          筛选行
        pageSizeChanged(int) / prevRequested / nextRequested / refreshRequested
                                                  分页行
        exportRequested / viewChanged(int) / columnsChanged /
        chartColumnsChanged / freezeToggled(bool) / extraComboChanged(int)
                                                  视图行 + 页面特有下拉
    """

    filterRequested = pyqtSignal()
    clearRequested = pyqtSignal()
    pageSizeChanged = pyqtSignal(int)
    prevRequested = pyqtSignal()
    nextRequested = pyqtSignal()
    refreshRequested = pyqtSignal()
    exportRequested = pyqtSignal()
    viewChanged = pyqtSignal(int)
    columnsChanged = pyqtSignal()
    chartColumnsChanged = pyqtSignal()
    freezeToggled = pyqtSignal(bool)
    extraComboChanged = pyqtSignal(int)    #: 折叠态变化（bool）：页面据此让表格吸收腾出的空间
    collapsedChanged = pyqtSignal(bool)

    def __init__(self, parent=None, columns=None, chart_columns=None,
                 page_sizes=(10, 20, 50), extra_combo=None, paging=True,
                 refresh=True, chart_view=False, freeze=False, export=True,
                 export_text="导出数据", keyword_placeholder="包含…（空=不限）",
                 state_key="", fold_btn_host=None):
        """构造（三行；**不显示**的能力就不长控件）。

        :param columns: 「列」多选的列名（None = 不显示「列」）。
        :param chart_columns: 「折线列」多选的列名（None = 不显示）。
        :param page_sizes: 「每页」下拉的档位。
        :param extra_combo: ``(标签, [项…])`` —— 页面特有下拉（日志页的
            「类别」、报警页的「类型」）；None = 不显示。
        :param paging: 是否显示分页行。
        :param refresh: 分页行是否带「刷新」钮（报警页数据是健康推送来的、
            没有"重查"动作 ⇒ 传 False 隐藏，别留一个点了没反应的死按钮）。
        :param chart_view: 是否显示「表格视图/图表视图」切换。
        :param freeze: 是否显示「滚动锁定」。
        :param export: 是否显示「导出数据」。
        :param fold_btn_host: 折叠把手的**按钮宿主**（页头标题行的右侧
            QLayout，2026-11-19 用户口径「放第一行标题栏右边，白白占了一
            行」⇒ 把手按钮挂页头、不再独占一行；None = 旧形态独立一行）。
        """
        QWidget.__init__(self, parent)
        self._lay = QVBoxLayout(self)
        self._lay.setContentsMargins(0, 0, 0, 0)
        self._lay.setSpacing(6)
        # ---- 折叠把手（顶行）：页头与筛选区之间的**分隔条** ----
        # [2026-11-19 用户口径「表格的上面与这些下面中间是不是可以增加一个
        #  分割条，可以折叠红框中的功能区，这样浏览数据是不是更方便」]
        # 工具条本身有 3~4 行（时间窗/列+关键字/分页/视图+导出），在 1280×
        # 800 @150% 的板端上吃掉约 1/5 屏 ⇒ 折叠后表格大一屏。
        # [为什么放在工具条**内部顶部**] 三个数据页的页面结构不用改（工具条
        # 就在页头下面）⇒ 一处实现三页通吃。
        # [2026-11-19 用户口径「放第一行标题栏右边，白白占了一行」] 提供
        # `fold_btn_host`（页头标题行右侧 QLayout）⇒ 把手**按钮**挂到那里
        # （btn 被 QLayout addWidget 自动 reparent），handle 容器不再加入
        # 本工具条 ⇒ 独立行消失；set_collapsed 等接口照旧（btn 引用仍在）。
        self._state_key = state_key
        self._row_widgets = []       # 折叠时要收起的行容器（QWidget）
        self._handle = _ToolbarHandle(self._on_toggle_collapsed)
        if fold_btn_host is not None:
            self._handle.setVisible(False)      # 容器只当 btn 的逻辑父级
            fold_btn_host.addWidget(self._handle.btn)
            self._handle.btn.setStyleSheet(
                self._handle.btn.styleSheet().replace("text-align: left;",
                                                      "text-align: center;"))
        else:
            self._lay.addWidget(self._handle)        # 恢复上次的折叠状态（缺省展开）
        self._collapsed = False
        if state_key:
            from . import settings as _settings
            self._collapsed = _settings.data_toolbar_collapsed(state_key)
        self._handle.set_collapsed(self._collapsed)

        # ---- ① 筛选块（两行独立布局，不互相挤宽度）----
        # [为什么要两行] 筛选项有 类别 / 起始 / 结束 / 列 / 关键字 / 筛选 /
        # 清除筛选 共 7 组，1.5× 界面缩放下时间框按字体实测约 340px×2，
        # 全塞进一条 QHBoxLayout 会把「列」「关键字」挤成省略号（现场实锅）。
        # 拆成两条独立 QHBoxLayout：时间窗一行、列+关键字一行，各自分配
        # 宽度、互不撑压 ⇒ 宽屏/窄屏都不裁字（对齐「尺寸交布局、别写死」
        # 铁律）。分页行 / 视图行仍各一行，三页形态一致。
        fbar1 = QHBoxLayout()
        fbar1.setSpacing(8)
        # [会换行 2026-11-14] 四行筛选/分页/视图工具条统一改成**可换行**的
        # 行容器：宽窗一行、窄窗自动折行 ⇒ 既不横向滚动、也不裁字（此前窄窗
        # 下这一页会出**横向滚动条**，右边的控件被裁 ⇒ 用户看到「同一行却
        # 实际不在一行」）。标签与控件**成组**（组内不拆散）。
        items1 = []
        self.extra_combo = None
        if extra_combo:
            label, items = extra_combo[0], list(extra_combo[1])
            self.extra_combo = QComboBox()
            for item in items:
                self.extra_combo.addItem(item)
            self.extra_combo.setMaximumWidth(110)
            self.extra_combo.currentIndexChanged.connect(
                self.extraComboChanged)
            items1.append(label_group(label, self.extra_combo))
        # [占位符只放最常见写法] 完整的三种格式说明在 TimePickerEdit 的
        # tooltip 里（框宽有限，占位符写长了会被省略号截断）。
        self.start_edit = TimePickerEdit("yyyy-MM-dd HH:MM")
        _fit_time_edit_width(self.start_edit)
        items1.append(label_group("起始", self.start_edit))
        self.end_edit = TimePickerEdit("yyyy-MM-dd HH:MM")
        _fit_time_edit_width(self.end_edit)
        items1.append(label_group("结束", self.end_edit))
        self.btn_filter = QPushButton("筛选")
        self.btn_filter.clicked.connect(self.filterRequested)
        items1.append(self.btn_filter)
        self.btn_clear = QPushButton("清除筛选")
        self.btn_clear.clicked.connect(self.clearRequested)
        items1.append(self.btn_clear)
        _row1 = wrap_row(items1)
        self._row_widgets.append(_row1)
        fbar1.addWidget(_row1, 1)
        self._lay.addLayout(fbar1)

        fbar2 = QHBoxLayout()
        fbar2.setSpacing(8)
        items2 = []
        # [「列」= 显示哪些列，不是筛选哪些列] 用户口径（截图反馈）：
        # 「列多选框你要测试，只显示勾选的列，默认是全部勾选」。
        self.column_combo = None
        if columns:
            self.column_combo = MultiCheckCombo(columns, all_text="全部列",
                                                initial="all", min_checked=1)
            # [用最小宽而不是 setMaximumWidth] 它现在是按钮（不是下拉框），
            # 文字是"时间、模式、输出频率(Hz)…"这类汇总；给了最大宽会把
            # 文字用省略号截掉、看不出勾了哪些列。
            self.column_combo.setMinimumWidth(150)
            self.column_combo.setMaximumWidth(300)
            self.column_combo.setToolTip("勾选的列才显示（默认全部勾选，"
                                         "至少保留一列）")
            self.column_combo.selectionChanged.connect(self.columnsChanged)
            items2.append(label_group("列", self.column_combo))
        self.keyword_edit = QLineEdit()
        self.keyword_edit.setPlaceholderText(keyword_placeholder)
        # [键盘类型声明] 关键字筛选是业务文本（可能中文），要拼音全键
        # （显式声明一次，让"这个框要什么键盘"在页面代码里可见）。
        from .softkeyboard import KIND_PINYIN, set_keyboard_kind
        set_keyboard_kind(self.keyword_edit, KIND_PINYIN)
        items2.append(label_group("关键字", self.keyword_edit))
        _row2 = wrap_row(items2)
        self._row_widgets.append(_row2)
        fbar2.addWidget(_row2, 1)
        self._lay.addLayout(fbar2)

        # ---- ② 分页行 ----
        self.page_size_box = None
        self.btn_prev = None
        self.page_label = None
        self.btn_next = None
        self.total_label = None
        self.count_label = None
        self.btn_reload = None
        if paging:
            # [分页控件在表上方] 板端竖屏时表格占满剩余高度，翻页钮放表
            # 下方会被挤出屏幕（历史页同款布局理由）。
            pbar = QHBoxLayout()
            pbar.setSpacing(8)
            items_p = []
            self.page_size_box = QComboBox()
            for n in page_sizes:
                self.page_size_box.addItem("%d 条" % n, n)
            # [下拉不裁字] 档位文字按**字体实测**给底宽（原来只给最大宽
            # 96px，窄窗时布局把它挤到 39px ⇒ Qt 用省略号截成「20…」，
            # 看不出是几档）。留白 = QSS 左右 padding(8×2) + 边框(1×2)
            # + 下拉箭头(~16) ⇒ 文字区刚好装得下最宽的那一档。
            self.page_size_box.setMinimumWidth(
                fit_text_width(self.page_size_box,
                               ["%d 条" % n for n in page_sizes]) + 34)
            self.page_size_box.currentIndexChanged.connect(
                self._sync_page_size_tip)
            self._sync_page_size_tip()
            self.page_size_box.currentIndexChanged.connect(
                self.pageSizeChanged)
            items_p.append(label_group("每页", self.page_size_box))
            self.btn_prev = QPushButton("上一页")
            self.btn_prev.clicked.connect(self.prevRequested)
            items_p.append(self.btn_prev)
            self.page_label = QLabel("")
            self.page_label.setStyleSheet(theme.caption_label_qss())
            items_p.append(self.page_label)
            self.btn_next = QPushButton("下一页")
            self.btn_next.clicked.connect(self.nextRequested)
            items_p.append(self.btn_next)
            self.total_label = QLabel("")
            self.total_label.setStyleSheet(theme.caption_label_qss())
            items_p.append(self.total_label)
            # [计数标签放分页行] 筛选行控件已满（1400 宽时"本页显示 20 行"
            # 会被挤到窗口外），跟页码/总数一起看。
            self.count_label = QLabel("")
            self.count_label.setStyleSheet(theme.caption_label_qss())
            items_p.append(self.count_label)
            if refresh:
                self.btn_reload = QPushButton("刷新")
                self.btn_reload.clicked.connect(self.refreshRequested)
                items_p.append(self.btn_reload)
            _rowp = wrap_row(items_p)
            self._row_widgets.append(_rowp)
            pbar.addWidget(_rowp, 1)
            self._lay.addLayout(pbar)

        # ---- ③ 视图行 ----
        bar = QHBoxLayout()
        bar.setSpacing(8)
        items_v = []
        self.chart_column_combo = None
        if chart_columns:
            # [折线列也做多选] 用户要求：「折线列的下拉也要做类似的设计
            # 可以多选」。与「列」显示开关同一个控件（MultiCheckCombo），
            # 交互一致（点项不收起、点外面才收），只是这里 initial="none"
            # + 至少 1 列 ——没有勾任何列就没有线可画。
            self.chart_column_combo = MultiCheckCombo(
                list(chart_columns), all_text="选择列…", initial="none",
                min_checked=0)
            self.chart_column_combo.setMinimumWidth(150)
            self.chart_column_combo.setMaximumWidth(300)
            self.chart_column_combo.setToolTip("可多选：勾中的列各画一条线"
                                               "（至少选一列）")
            self.chart_column_combo.selectionChanged.connect(
                self.chartColumnsChanged)
            items_v.append(label_group("折线列", self.chart_column_combo))
        self.btn_table_view = None
        self.btn_chart_view = None
        if chart_view:
            self.btn_table_view = QPushButton("表格视图")
            self.btn_table_view.setCheckable(True)
            self.btn_table_view.setChecked(True)
            self.btn_table_view.clicked.connect(lambda: self.viewChanged.emit(0))
            items_v.append(self.btn_table_view)
            self.btn_chart_view = QPushButton("图表视图")
            self.btn_chart_view.setCheckable(True)
            self.btn_chart_view.clicked.connect(lambda: self.viewChanged.emit(1))
            items_v.append(self.btn_chart_view)
        #: 页面把自己的附加控件塞进这里（日志页的「客户端运行日志」开关）。
        self.extra_slot = QHBoxLayout()
        self.extra_slot.setSpacing(8)
        extra_box = QWidget()
        extra_box.setLayout(self.extra_slot)
        items_v.append(extra_box)
        self.freeze_box = None
        if freeze:
            self.freeze_box = QCheckBox("滚动锁定（停止刷表，后台照常采样）")
            self.freeze_box.toggled.connect(self.freezeToggled)
            items_v.append(self.freeze_box)
        self.btn_export = None
        if export:
            self.btn_export = QPushButton(export_text)
            self.btn_export.clicked.connect(self.exportRequested)
            items_v.append(self.btn_export)
        _rowv = wrap_row(items_v)
        self._row_widgets.append(_rowv)
        bar.addWidget(_rowv, 1)
        self._lay.addLayout(bar)

    # ---- 页面侧的小工具 ----

    def _sync_page_size_tip(self, *_args):
        """「每页」下拉的 tooltip 带上**当前档位**（信息可达）。

        [为什么] 窄窗时下拉可能被挤窄（Qt 会把文字省略号截断）⇒ 光看
        框里分不清是几档；tooltip 里写明当前值 ⇒ 随时可查。
        """
        if self.page_size_box is None:
            return
        self.page_size_box.setToolTip(
            "每页显示条数（当前 %s）" % (self.page_size_box.currentText(),))

    # ---- 折叠（2026-11-19 用户口径）----
    def _on_toggle_collapsed(self):
        """点把手 ⇒ 折叠/展开工具条的行（表格因此大一屏）。"""
        self.set_collapsed(not self._collapsed)

    def set_collapsed(self, collapsed):
        """折叠/展开（并记住：折叠是长期偏好，落盘本机设置）。"""
        collapsed = bool(collapsed)
        self._collapsed = collapsed
        for w in self._row_widgets:
            w.setVisible(not collapsed)
        self._handle.set_collapsed(collapsed)
        self.collapsedChanged.emit(collapsed)
        if self._state_key:
            from . import settings as _settings
            _settings.set_data_toolbar_collapsed(self._state_key, collapsed)

    def is_collapsed(self):
        return bool(getattr(self, "_collapsed", False))

    def add_extra_widget(self, widget):
        """把页面自己的控件放进视图行（在"导出"之前）。"""
        self.extra_slot.addWidget(widget)

    def set_page_size(self, size):
        """设置「每页」下拉的档位（按值匹配；找不到就不动）。"""
        if self.page_size_box is None:
            return
        idx = self.page_size_box.findData(int(size))
        if idx >= 0:
            self.page_size_box.setCurrentIndex(idx)

    def page_size(self):
        """当前「每页」档位（没有分页行时返回 0）。"""
        if self.page_size_box is None:
            return 0
        data = self.page_size_box.currentData()
        return int(data) if isinstance(data, int) else 0

    def set_view_mode(self, mode):
        """同步「表格视图/图表视图」按钮的勾选态。"""
        if self.btn_table_view is not None:
            self.btn_table_view.setChecked(mode == 0)
        if self.btn_chart_view is not None:
            self.btn_chart_view.setChecked(mode == 1)
