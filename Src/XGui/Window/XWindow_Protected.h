/******************************************************************************
 * @file       XWindow_Protected.h
 * @brief      XWindow 基类保护接口（仅供子类与内部实现使用）。
 * @details    本文件集中声明 Qt QWindow 中属于 protected 的事件虚函数
 *             （expose/resize/paint/move/focusIn/focusOut/show/hide/close/
 *             键盘/鼠标/滚轮/触摸/数位板/输入法/拖放/进入/离开）、原生事件
 *             槽以及事件总入口宏；普通用户代码不应直接包含或调用。
 *             XWindow.h 只保留公开 API。
 ******************************************************************************/
#ifndef XWINDOW_PROTECTED_H
#define XWINDOW_PROTECTED_H
#ifdef __cplusplus
extern "C" {
#endif

#include "XWindow.h"

#if XWINDOW_ON

/* ==================== 事件分发（对标 QWindow protected 事件虚函数） ==================== */

/**
 * @brief      窗口事件总入口（对标 QWindow::event；继承自 XObject，宏复用）。
 * @details    虚表内为 EXObject_Event 槽位，XWindow 已重载为默认分发器
 *             （VXWindow_event）：按 XEvent_type 路由到具体事件槽
 *             （expose/resize/paint/move/focusIn/focusOut/show/hide/close/key/
 *             mouse/wheel/touch/tablet）；未识别事件回退调用 XObject 默认
 *             Event 实现。
 * @param      self 目标窗口；可为 NULL。
 * @param      event 待分发事件；可为 NULL。
 * @return     已识别并分发返回 true；否则返回父类处理结果。
 */
#define XWindow_event_base(self, event) \
    XObject_event_base((XObject*)(self), (event))

/** @brief 暴露事件槽（对标 QWindow::exposeEvent）。 @param self 目标窗口。 @param event 暴露事件。 */
void XWindow_exposeEvent_base(XWindow* self, XEvent* event);
/** @brief 调整大小事件槽（对标 QWindow::resizeEvent）。 @param self 目标窗口。 @param event 调整大小事件。 */
void XWindow_resizeEvent_base(XWindow* self, XEvent* event);
/** @brief 绘制事件槽（对标 QWindow::paintEvent）。 @param self 目标窗口。 @param event 绘制事件。 */
void XWindow_paintEvent_base(XWindow* self, XEvent* event);
/** @brief 移动事件槽（对标 QWindow::moveEvent）。 @param self 目标窗口。 @param event 移动事件。 */
void XWindow_moveEvent_base(XWindow* self, XEvent* event);
/** @brief 焦点进入事件槽（对标 QWindow::focusInEvent）。 @param self 目标窗口。 @param event 焦点事件。 */
void XWindow_focusInEvent_base(XWindow* self, XEvent* event);
/** @brief 焦点离开事件槽（对标 QWindow::focusOutEvent）。 @param self 目标窗口。 @param event 焦点事件。 */
void XWindow_focusOutEvent_base(XWindow* self, XEvent* event);
/** @brief 显示事件槽（对标 QWindow::showEvent）。 @param self 目标窗口。 @param event 显示事件。 */
void XWindow_showEvent_base(XWindow* self, XEvent* event);
/** @brief 隐藏事件槽（对标 QWindow::hideEvent）。 @param self 目标窗口。 @param event 隐藏事件。 */
void XWindow_hideEvent_base(XWindow* self, XEvent* event);
/** @brief 关闭事件槽（对标 QWindow::closeEvent）。 @param self 目标窗口。 @param event 关闭事件。 */
void XWindow_closeEvent_base(XWindow* self, XEvent* event);
/** @brief 按键按下事件槽（对标 QWindow::keyPressEvent）。 @param self 目标窗口。 @param event 键盘事件。 */
void XWindow_keyPressEvent_base(XWindow* self, XEvent* event);
/** @brief 按键释放事件槽（对标 QWindow::keyReleaseEvent）。 @param self 目标窗口。 @param event 键盘事件。 */
void XWindow_keyReleaseEvent_base(XWindow* self, XEvent* event);
/** @brief 鼠标按下事件槽（对标 QWindow::mousePressEvent）。 @param self 目标窗口。 @param event 鼠标事件。 */
void XWindow_mousePressEvent_base(XWindow* self, XEvent* event);
/** @brief 鼠标释放事件槽（对标 QWindow::mouseReleaseEvent）。 @param self 目标窗口。 @param event 鼠标事件。 */
void XWindow_mouseReleaseEvent_base(XWindow* self, XEvent* event);
/** @brief 鼠标双击事件槽（对标 QWindow::mouseDoubleClickEvent）。 @param self 目标窗口。 @param event 鼠标事件。 */
void XWindow_mouseDoubleClickEvent_base(XWindow* self, XEvent* event);
/** @brief 鼠标移动事件槽（对标 QWindow::mouseMoveEvent）。 @param self 目标窗口。 @param event 鼠标事件。 */
void XWindow_mouseMoveEvent_base(XWindow* self, XEvent* event);
/** @brief 滚轮事件槽（对标 QWindow::wheelEvent）。 @param self 目标窗口。 @param event 滚轮事件。 */
void XWindow_wheelEvent_base(XWindow* self, XEvent* event);
/** @brief 触摸事件槽（对标 QWindow::touchEvent）。 @param self 目标窗口。 @param event 触摸事件。 */
void XWindow_touchEvent_base(XWindow* self, XEvent* event);
/** @brief 数位板事件槽（对标 QWindow::tabletEvent）。 @param self 目标窗口。 @param event 数位板事件。 */
void XWindow_tabletEvent_base(XWindow* self, XEvent* event);
/** @brief 输入法组合/提交事件槽（对标 QWindow::inputMethodEvent）。 */
void XWindow_inputMethodEvent_base(XWindow* self, XEvent* event);
/** @brief 拖放进入事件槽（对标 QWindow::dragEnterEvent）。 */
void XWindow_dragEnterEvent_base(XWindow* self, XEvent* event);
/** @brief 拖放移动事件槽（对标 QWindow::dragMoveEvent）。 */
void XWindow_dragMoveEvent_base(XWindow* self, XEvent* event);
/** @brief 拖放离开事件槽（对标 QWindow::dragLeaveEvent）。 */
void XWindow_dragLeaveEvent_base(XWindow* self, XEvent* event);
/** @brief 放置事件槽（对标 QWindow::dropEvent）。 */
void XWindow_dropEvent_base(XWindow* self, XEvent* event);

/**
 * @brief      原生事件槽（对标 QWindow::nativeEvent）。
 * @details    事件类型/消息/结果统一承载在 XEvent* 中；无平台后端时调用方
 *             可在子类重载后自行解释。默认实现返回 false（不接受）。
 * @param      self 目标窗口。
 * @param      event 原生事件载体；可为 NULL。
 * @return     true 表示已处理；false 表示未处理。
 */
bool XWindow_nativeEvent_base(XWindow* self, XEvent* event);
/** @brief 指针进入事件槽（对标 QWindow::enterEvent）。 @param self 目标窗口。 @param event 指针进入事件（XEnterEvent）。 */
void XWindow_enterEvent_base(XWindow* self, XEvent* event);
/** @brief 指针离开事件槽（对标 QWindow::leaveEvent）。 @param self 目标窗口。 @param event 指针离开事件（无负载 XEvent）。 */
void XWindow_leaveEvent_base(XWindow* self, XEvent* event);

#ifdef __cplusplus
}
#endif
#endif /* XWINDOW_ON */

#ifdef __cplusplus
}
#endif
#endif /* XWINDOW_PROTECTED_H */
