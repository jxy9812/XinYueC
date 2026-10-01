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

/* ==================== 平台桥接内部状态（仅供内部实现，不对外） ==================== */

/**
 * @brief      推送窗口设备像素比（框架内部接口，仅供子类与内部实现）。
 * @details    闭环 XWindow_setScreen 声明的「屏幕归属决定 dpr」语义的
 *             运行期补线（DPI 定版）：屏幕 dpr/logicalDpi 变化经
 *             XGuiApplication 的 screen 信号回调遍历 allWindows 时调用
 *             （对标 Qt QWindowPrivate::setScreen 更新
 *             QHighDpiDirty / QWindow::devicePixelRatio 随屏联动）。仅
 *             更新快照值：等值短路（零扰动），变化时也不发信号——窗口
 *             级 dpr 无对应 Qt 信号，重绘由调用方随后续 XWindow_
 *             requestUpdate 统一触发。不走公开 API 是刻意收窄：外部
 *             调用方设置 dpr 无合法语义（Qt 中该属性只由窗口系统写）。
 * @param      self 目标窗口；NULL 不执行任何操作。
 * @param      ratio 新设备像素比（来自 XScreen_devicePixelRatio）。
 * @return     无。
 */
void XWindow_setDevicePixelRatio_internal(XWindow* self, float ratio);

/**
 * @brief      设置「CSD 激活时抑制原生 WM 装饰」标记（框架内部接口）。
 * @details    仅供框架内部实现（标题栏自绘激活/停用路径）调用，不对外
 *             公开：桌面会话下框架接管标题栏时置位，平台原生窗口后端
 *             （Drive）在组装 _MOTIF_WM_HINTS 等装饰提示时读取本标记，
 *             按无边框语义抑制 WM 装饰，避免原生标题栏与框架标题栏双
 *             重出现。只更新快照位，不触发任何平台调用；置位后对已建
 *             原生窗的即时生效由调用方随既有 setWindowFlags 重写路径
 *             完成。无 WM 环境（fbdev 直写）本标记无效果。
 * @param      self 目标窗口；NULL 不执行任何操作。
 * @param      suppressed true 置位抑制原生装饰；false 清除（恢复交 WM）。
 * @return     无。
 */
void XWindow_setCsdFrameSuppressed(XWindow* self, bool suppressed);

/**
 * @brief      查询窗口是否要求抑制原生 WM 装饰（CSD 激活，框架内部接口）。
 * @details    仅供平台层（原生窗口后端）与框架内部实现读取，不对外公开。
 * @param      self 目标窗口；可为 NULL。
 * @return     需要抑制返回 true；入参非法（调用者没有提供对象）或未
 *             置位返回 false。
 */
bool XWindow_isCsdFrameSuppressed(const XWindow* self);

#ifdef __cplusplus
}
#endif
#endif /* XWINDOW_ON */

#ifdef __cplusplus
}
#endif
#endif /* XWINDOW_PROTECTED_H */
