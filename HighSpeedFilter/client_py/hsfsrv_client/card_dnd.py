# -*- coding: utf-8 -*-
"""主界面看板卡片的**长按拖拽排序**（2026-11-23 用户口径「支持下主界面
长按可以拖拽卡片进行自己排序，并本地持久化记录位置」）。

[交互]
=======
* **长按**（600ms，手指/鼠标按住不动）⇒ 目标卡显示虚线高亮框；
* 拖动到另一张卡上 ⇒ 该卡高亮；松手 = **两张卡交换位置**；
* 松手在空白处 ⇒ 取消（不改变顺序）；
* 长按之前手指移动超过 10px ⇒ 视为滚动/点击，不进入拖拽（不影响页面滚动
  与卡片点击）；
* 拖拽结束的 350ms 内**吞掉点击**（卡片是"点开详情/跳页"的按钮，松手会
  被当点击）。

[为什么不显示"跟随手指的卡片快照"2026-11-24 用户口径]
"最近报警卡片有时候点击会弹出报警卡片悬浮窗，这个删掉吧，在触摸屏上不好
用有时候不回关，还会无法切换界面。"
—— 原实现用 **顶层 Qt.ToolTip 窗口**显示卡片截图跟随手指；而卡片点击是
**按下即触发**（`_ClickableCard`），点「最近报警」卡会在按下当场跳报警页
⇒ 首页被隐藏 ⇒ 若长按计时器恰好已启动，快照窗口照样弹出，而 TouchEnd
落在新页面上、首页的过滤器不再收到 ⇒ **快照永不消失**（顶层窗口还盖在
界面上）。⇒ 直接**取消这个悬浮快照**，拖拽反馈只保留页面内的虚线目标框
（不会产生任何顶层窗口），并在跳页/开悬浮详情/离页时显式取消拖拽状态。

[为什么用全局事件过滤器]
=========================
卡片内部的文字/数值块是子控件，按下事件先到子控件 ⇒ 装在卡片上的过滤器
看不到；而触摸在板端又会先被 QAbstractScrollArea 吃掉。装在 QApplication
上的过滤器能同时看到**鼠标与触摸**事件（含 Qt 由触摸合成的鼠标），再按
全局坐标命中卡片 ⇒ 一套状态机覆盖鼠标 + 触摸。

[持久化]
=========
顺序由使用方（HomePage）负责落盘；本模块只回调 `on_swap(a, b)`。
"""

import time

from PyQt5.QtCore import QEvent, QObject, QPoint, QTimer, Qt
from PyQt5.QtWidgets import QApplication, QFrame, QWidget

#: 长按进入拖拽的时长（ms）。太短会和"手指刚按下去想滚动"打架。
LONG_PRESS_MS = 600
#: 长按判定前允许的手抖位移（px）；超过即视为滚动/点击。
MOVE_TOLERANCE_PX = 10
#: 拖拽结束后的点击抑制窗口（ms）。
CLICK_SUPPRESS_MS = 350


class CardDragController(QObject):
    """把一组 `card_id -> QWidget` 变成可长按拖拽交换的看板卡。"""

    def __init__(self, host, cards, on_swap, parent=None):
        """
        :param host: 看板所在的页面控件（ghost/高亮框挂在它上面，坐标也以它为基准）。
        :param cards: {card_id: QWidget}
        :param on_swap: 回调 ``on_swap(a_id, b_id)`` —— 使用方负责交换顺序、
            重排并落盘（本模块不碰布局与存储）。
        """
        QObject.__init__(self, parent)
        self._host = host
        self._cards = dict(cards)
        self._on_swap = on_swap
        self._press_card = None
        self._press_pos = QPoint()
        self._last_pos = QPoint()
        self._press_touch_id = None
        self._mouse_down = False
        self._dragging = False
        self._frame = None
        self._target = None
        self._suppress_until = 0.0
        self._timer = QTimer(self)
        self._timer.setSingleShot(True)
        self._timer.setInterval(LONG_PRESS_MS)
        self._timer.timeout.connect(self._maybe_start_drag)
        # [拖拽兜底收口 2026-11-24] 触摸屏上 TouchEnd 有可能丢（滚动区抢
        # 手势/页面切走）⇒ 状态机会一直停在 `_dragging`。这里加一个上限：
        # 拖拽超过 15s 一律收口（人不会按着一张卡拖 15 秒）。
        self._safety = QTimer(self)
        self._safety.setSingleShot(True)
        self._safety.setInterval(15000)
        self._safety.timeout.connect(self.cancel)

        # [为什么不用 QApplication 全局过滤器 2026-11-23 实锅] 装在
        # QApplication 上的过滤器在 **QApplication 析构期**仍会被遍历/摘除
        # ⇒ 回归里反复建/毁页时 settingsmerge 子进程 exit=0xC0000005
        # （3/3 复现）。改为装在**页面自己拥有的控件**上（卡片 + 全部子控件
        # + 滚动视口）：控件随页面一起销毁，Qt 在各自析构时安全摘除过滤器。
        self._app = QApplication.instance()
        self._watched = []
        self._install_filters()

    # ---- 生命周期 ----
    def _install_filters(self):
        """把过滤器装到**本页拥有的控件**上（卡片/子控件/滚动视口）。"""
        from PyQt5.QtWidgets import QAbstractScrollArea
        targets = [self._host]
        for card in self._cards.values():
            targets.append(card)
            try:
                targets.extend(card.findChildren(QWidget))
            except RuntimeError:
                continue
        # 触摸事件在板端常由滚动区的**视口**接收 ⇒ 视口也要挂。
        w = self._host.parentWidget()
        while w is not None:
            if isinstance(w, QAbstractScrollArea):
                targets.append(w)
                targets.append(w.viewport())
                break
            w = w.parentWidget()
        for widget in targets:
            try:
                widget.installEventFilter(self)
                self._watched.append(widget)
            except RuntimeError:
                continue

    def detach(self, *_args):
        self._timer.stop()
        self._safety.stop()
        self._clear_visuals()
        for widget in list(self._watched):
            try:
                widget.removeEventFilter(self)
            except RuntimeError:
                pass
        self._watched = []
        self._app = None

    # ---- 查询/测试入口 ----
    def is_dragging(self):
        return self._dragging

    def just_dragged(self):
        """刚结束一次拖拽（调用方用于吞掉卡片点击）。"""
        return time.monotonic() < self._suppress_until

    def press_card(self, card_id):
        """测试入口：等价于在卡片中心按下（不移动）。"""
        w = self._cards.get(card_id)
        if w is None:
            return False
        self._begin(card_id, w.mapToGlobal(w.rect().center()), None)
        return True

    def force_long_press(self):
        """测试入口：跳过 600ms 等待，直接进入拖拽。"""
        self._maybe_start_drag()

    def drag_to_global(self, pos):
        """测试入口：把拖拽移到一个全局坐标。"""
        self._move(pos)

    def drop(self):
        """测试入口：在当前位置松手。"""
        self._finish(self._last_pos)

    def cancel(self):
        self._reset_press()
        self._safety.stop()
        self._clear_visuals()

    # ---- 命中 ----
    def _visible(self):
        try:
            return bool(self._host is not None and self._host.isVisible())
        except RuntimeError:                         # 宿主已销毁
            return False

    def _card_of(self, widget):
        """事件目标 widget → 所属卡片 id（含目标本身）。"""
        if widget is None:
            return None
        for cid, card in self._cards.items():
            try:
                if widget is card or card.isAncestorOf(widget):
                    return cid
            except RuntimeError:                     # 控件已销毁
                continue
        return None

    def _card_at_global(self, pos, exclude=None):
        # 顶层窗口的坐标转换：卡片可能被滚动了 ⇒ 用 mapFromGlobal 逐张判。
        for cid, card in self._cards.items():
            if cid == exclude:
                continue
            try:
                if not card.isVisible():
                    continue
                local = card.mapFromGlobal(pos)
                if card.rect().contains(local):
                    return cid
            except RuntimeError:
                continue
        return None

    # ---- 状态机 ----
    def _begin(self, card_id, pos, touch_id):
        self._timer.stop()
        self._press_card = card_id
        self._press_pos = QPoint(pos)
        self._last_pos = QPoint(pos)
        self._press_touch_id = touch_id
        self._timer.start()

    def _reset_press(self):
        self._timer.stop()
        self._press_card = None
        self._press_touch_id = None
        self._mouse_down = False

    def _maybe_start_drag(self):
        if self._press_card is None or self._dragging:
            return
        # [页面已切走/卡片已隐藏 ⇒ 不再起拖拽 2026-11-24] 「最近报警」卡是
        # **按下即跳页**：按下时计时器已启动，600ms 后若首页已隐藏，原来的
        # 实现仍会弹出顶层快照窗口（且收不到 TouchEnd ⇒ 永不关闭）。
        if not self._visible():
            self.cancel()
            return
        card = self._cards.get(self._press_card)
        if card is None:
            self._reset_press()
            return
        try:
            if not card.isVisible():
                self._reset_press()
                return
        except RuntimeError:                         # 控件已销毁
            self._reset_press()
            return
        self._dragging = True
        self._safety.start()
        # 高亮框：目标卡的虚线边框（挂在页面控件上，透明背景）。
        frame = QFrame(self._host)
        frame.setAttribute(Qt.WA_TransparentForMouseEvents, True)
        frame.setStyleSheet(
            "QFrame { border: 2px dashed #1976D2; background: transparent; }")
        frame.hide()
        self._frame = frame
        self._move(self._last_pos)

    def _move(self, pos):
        self._last_pos = QPoint(pos)
        if not self._dragging:
            return
        if not self._visible():
            self.cancel()
            return
        target = self._card_at_global(pos, exclude=self._press_card)
        self._target = target
        if target is None:
            if self._frame is not None:
                self._frame.hide()
            return
        card = self._cards.get(target)
        if card is None or self._frame is None:
            return
        top_left = card.mapTo(self._host, QPoint(0, 0))
        self._frame.setGeometry(
            top_left.x() - 2, top_left.y() - 2,
            card.width() + 4, card.height() + 4)
        self._frame.show()
        self._frame.raise_()

    def _finish(self, pos):
        dragged = self._press_card
        target = self._card_at_global(pos, exclude=dragged)
        self._clear_visuals()
        self._reset_press()
        self._safety.stop()
        if dragged is not None and target is not None and target != dragged:
            self._suppress_until = time.monotonic() + CLICK_SUPPRESS_MS / 1000.0
            try:
                self._on_swap(dragged, target)
            except Exception:                        # noqa: BLE001
                pass

    def _clear_visuals(self):
        self._dragging = False
        self._target = None
        self._safety.stop()
        if self._frame is not None:
            self._frame.hide()
            self._frame.deleteLater()
            self._frame = None

    # ---- 事件 ----
    def eventFilter(self, obj, event):
        # [防御] teardown/控件已删时任何 RuntimeError 都不能冒泡到事件循环。
        try:
            return self._event_filter(obj, event)
        except RuntimeError:
            return False
        except Exception:                            # noqa: BLE001
            return False

    def _event_filter(self, obj, event):
        et = event.type()
        # [离页/触摸被系统取消 ⇒ 立刻收口 2026-11-24] 页面隐藏后收不到
        # 后续 Move/Release/TouchEnd，拖拽状态必须在这里清掉（否则回到首页
        # 还停在 `_dragging`，移动事件被吃掉 = "界面点不动"）。
        # [必须放在可见性守卫之前] Hide 事件到达时 `isVisible()` 已是 False。
        if et == QEvent.Hide and obj is self._host:
            self.cancel()
            return False
        if self._app is None or not self._visible():
            return False
        if et in (QEvent.TouchCancel, QEvent.UngrabMouse):
            if self._press_card is not None or self._dragging:
                self.cancel()
            return False
        if et == QEvent.MouseButtonPress:
            if event.button() == Qt.LeftButton:
                cid = self._card_of(obj)
                if cid is not None:
                    self._mouse_down = True
                    self._begin(cid, event.globalPos(), None)
            return False
        if et == QEvent.MouseMove:
            if self._press_card is None:
                return False
            if not self._dragging:
                if (event.globalPos() - self._press_pos).manhattanLength() \
                        > MOVE_TOLERANCE_PX:
                    self._reset_press()
                return False
            self._move(event.globalPos())
            return True                              # 拖拽期间吃掉移动（别滚页）
        if et == QEvent.MouseButtonRelease:
            if not self._mouse_down:
                return False
            self._mouse_down = False
            if self._dragging:
                self._finish(event.globalPos())
                return True                          # 吃掉松手 ⇒ 不触发卡片点击
            self._reset_press()
            return False
        if et == QEvent.TouchBegin:
            pts = event.touchPoints()
            if not pts:
                return False
            cid = self._card_of(obj)
            if cid is None:
                # 触摸事件目标常是滚动区视口 ⇒ 按触点命中卡片。
                cid = self._card_at_global(pts[0].screenPos().toPoint())
            if cid is not None:
                self._begin(cid, pts[0].screenPos().toPoint(),
                            pts[0].id())
            return False
        if et == QEvent.TouchUpdate:
            if self._press_card is None:
                return False
            pts = [p for p in event.touchPoints()
                   if self._press_touch_id is None or p.id() == self._press_touch_id]
            if not pts:
                return False
            pos = pts[0].screenPos().toPoint()
            if not self._dragging:
                if (pos - self._press_pos).manhattanLength() \
                        > MOVE_TOLERANCE_PX:
                    self._reset_press()
                return False
            self._move(pos)
            return True                              # 吃掉触摸移动 ⇒ 不滚页
        if et == QEvent.TouchEnd:
            if self._press_card is None:
                return False
            if self._dragging:
                self._finish(self._last_pos)
                return True
            self._reset_press()
            return False
        return False
