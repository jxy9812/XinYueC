/******************************************************************************
 * @file       XWindowSystemInterface.h
 * @brief      窗口系统事件注入接口（对标 Qt 6.8 QWindowSystemInterface）。
 * @details    平台层唯一允许调用的事件注入入口：Windows/Linux 平台后端
 *             把原生事件翻译为 XEvent 派生事件（XResizeEvent /
 *             XExposeEvent / XPaintEvent / XFocusEvent / XCloseEvent /
 *             XShowEvent / XHideEvent），通过本接口的 handle* 注入函数
 *             统一进入 XGuiApplication（即 XCoreApplication）事件系统，
 *             最终由 XWindow_event_base 路由到窗口事件槽，形成「平台注入
 *             → 事件分发 → 窗口重绘（XBackingStore）→ 上屏」闭环。
 *             API 命名与语义对齐 Qt 6.8 qwindowsysteminterface.h：
 *             - handleGeometryChange：持久化新几何并投递 Resize 事件；
 *             - handleExposeEvent / handlePaintEvent：投递暴露/重绘事件；
 *             - handleFocusWindowChanged：焦点窗口变化（新窗口收 FocusIn）；
 *             - handleCloseEvent：投递关闭事件并返回窗口是否接受；
 *             - flushWindowSystemEvents：冲刷排队事件。
 *             额外提供 handleShowEvent / handleHideEvent（项目扩展，Qt
 *             WSI 中 show/hide 由 QWindow::setVisible 内部产生）。
 *             注入函数默认同步自发投递（sendSpontaneousEvent），调用返回
 *             时事件已被窗口处理。本模块只依赖 XGuiApplication /
 *             XWindowEvent / XWindow 的公共 API，不引用任何平台 API；
 *             各平台后端在 Drive 目录下调用本接口时保持平台实现风格。
 * @note       模块总开关 XWINDOWSYSTEMINTERFACE_ON 定义于 XGuiConfig.h；
 *             同时受 XGUIAPPLICATION_ON / XWINDOW_ON / XWINDOWEVENT_ON
 *             约束，任一关闭时公共 API 即被裁剪。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XWINDOWSYSTEMINTERFACE_H
#define XWINDOWSYSTEMINTERFACE_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include "XGuiConfig.h"
#include "XGeometry.h"
#include "XEvent.h"
#include "XWindow.h"
#include "XWindowEvent.h"
#include "XGuiApplication.h"
#if XWINDOWSYSTEMINTERFACE_ON && XGUIAPPLICATION_ON && XWINDOW_ON && XWINDOWEVENT_ON

/**
 * @brief      注入几何变化（对标 QWindowSystemInterface::handleGeometryChange）。
 * @details    平台窗口位置/尺寸变化时调用：先把新几何持久化到 XWindow
 *             （XWindow_setGeometry_rect），再以自发事件投递
 *             XResizeEvent（oldSize 为变化前尺寸），窗口 resize 槽中
 *             XWindow_size() 已可读到新尺寸。
 * @param      window  目标窗口；不可为 NULL。
 * @param      rect    新几何（相对父窗口，本地坐标）；不可为 NULL。
 */
void XWindowSystemInterface_handleGeometryChange(XWindow* window, const XRect* rect);

/**
 * @brief      注入暴露事件（对标 QWindowSystemInterface::handleExposeEvent）。
 * @details    平台窗口首次显示或被遮挡恢复/失效时调用。region 为窗口
 *             本地坐标的暴露区域；空区域表示窗口被完全遮挡。事件携带
 *             区域由窗口 expose 槽（应用可在子类重载）驱动重绘到
 *             XBackingStore 并 flush。
 * @param      window 目标窗口；不可为 NULL。
 * @param      region 暴露区域；可为 NULL。内部深拷贝。
 * @return     true 已派发且窗口处理；false 参数无效或分配失败。
 */
bool XWindowSystemInterface_handleExposeEvent(XWindow* window, const XRegion* region);

/**
 * @brief      注入重绘事件（对标 QWindowSystemInterface::handlePaintEvent）。
 * @details    需要立即重绘窗口部分/全部区域时调用（区别于 handleExposeEvent
 *             的暴露通知）。事件携带区域由窗口 paint 槽（应用可在子类
 *             重载）在 XBackingStore 上绘制并 flush。
 * @param      window 目标窗口；不可为 NULL。
 * @param      region 绘制区域；可为 NULL。内部深拷贝。
 * @return     true 已派发且窗口处理；false 参数无效或分配失败。
 */
bool XWindowSystemInterface_handlePaintEvent(XWindow* window, const XRegion* region);

/**
 * @brief      注入焦点窗口变化（对标 QWindowSystemInterface::handleFocusWindowChanged）。
 * @details    平台键盘焦点从旧窗口转移到 window 时调用；window 收到
 *             XFocusEvent(FOCUS_IN, reason)。失焦通知按 Qt 语义由
 *             QGuiApplication 内部对旧窗口补发 FocusOut；本接口同步实
 *             现中请平台在切换前自行向旧窗口投递
 *             XFocusEvent(XEVENT_TYPE_FOCUS_OUT, reason)（可用
 *             XGuiApplication_sendSpontaneousEvent）。
 * @param      window 新焦点窗口；不可为 NULL。
 * @param      reason 焦点变化原因。
 */
void XWindowSystemInterface_handleFocusWindowChanged(XWindow* window, XFocusReason reason);

/**
 * @brief      注入关闭事件（对标 QWindowSystemInterface::handleCloseEvent）。
 * @details    平台请求关闭窗口（关闭按钮/系统关机）时调用。事件经
 *             XWindow 的 close 槽（应用可重载）决定是否接受；接受时
 *             返回 true，平台应继续销毁窗口，否则取消关闭。
 * @param      window 目标窗口；不可为 NULL。
 * @return     true 窗口接受关闭；false 拒绝关闭或参数无效。
 */
bool XWindowSystemInterface_handleCloseEvent(XWindow* window);

/**
 * @brief      注入显示事件（项目扩展；Qt WSI 无此函数）。
 * @details    平台窗口首次映射到屏幕时调用，事件通知窗口已可见。
 * @param      window 目标窗口；不可为 NULL。
 * @return     true 已派发；false 参数无效或分配失败。
 */
bool XWindowSystemInterface_handleShowEvent(XWindow* window);

/**
 * @brief      注入隐藏事件（项目扩展；Qt WSI 无此函数）。
 * @details    平台窗口从屏幕取消映射时调用，事件通知窗口已不可见。
 * @param      window 目标窗口；不可为 NULL。
 * @return     true 已派发；false 参数无效或分配失败。
 */
bool XWindowSystemInterface_handleHideEvent(XWindow* window);

/**
 * @brief      注入键盘事件（对标 QWindowSystemInterface::handleKeyEvent）。
 * @details    平台键盘按下/释放时调用；构造 XKeyEvent（携带按平台无关的
 *             按键码 XKey、修饰键、自动重复标志）并自发投递，由
 *             XWindow keyPressEvent/keyReleaseEvent 槽处理。
 * @param      window     目标窗口；不可为 NULL。
 * @param      type       事件类型；必须为 XEVENT_TYPE_KEY_PRESS 或
 *                        XEVENT_TYPE_KEY_RELEASE。
 * @param      key        与平台无关的按键码（XKey 枚举或 ASCII 码位）。
 * @param      modifiers  事件发生时按下的修饰键位掩码。
 * @param      autoRepeat 是否为按住键位不放产生的系统重复事件。
 * @return     true 已派发；false 参数无效或分配失败。
 */
bool XWindowSystemInterface_handleKeyEvent(XWindow* window, XEventType type,
                                           int key, XKeyboardModifiers modifiers,
                                           bool autoRepeat);

/**
 * @brief      注入系统输入法组合/提交事件（对标 handleInputMethodEvent）。
 * @details    preeditUtf8 是仍处于组合态的文本，commitUtf8 是已确认提交的
 *             UTF-8 文本；两者可独立为空。该事件不替代 KEY_PRESS，而是
 *             作为真实 IME 的文本通道交给窗口或焦点控件。
 * @param      window 目标窗口；不可为 NULL。
 * @param      preeditUtf8 组合文本；可为 NULL。
 * @param      commitUtf8 已提交文本；可为 NULL。
 * @param      replacementStart 相对当前光标的替换起始。
 * @param      replacementLength 替换长度。
 * @param      cursorPosition preedit 内光标位置；-1 表示未知。
 * @param      anchorPosition preedit 内锚点位置；-1 表示未知。
 */
bool XWindowSystemInterface_handleInputMethodEvent(
        XWindow* window, const char* preeditUtf8, const char* commitUtf8,
        int replacementStart, int replacementLength,
        int cursorPosition, int anchorPosition);

/**
 * @brief      注入平台拖放事件。
 * @details    type 必须为 DRAG_ENTER/DRAG_MOVE/DRAG_LEAVE/DROP；MIME 和数据
 *             均为 UTF-8。返回值对应事件 accept 状态，供 XDND/Windows
 *             后端向源端回复接收与否。
 */
bool XWindowSystemInterface_handleDropEvent(
        XWindow* window, XEventType type, XPoint position,
        const XPoint* globalPosition, const char* mimeTypeUtf8,
        const char* dataUtf8);

/**
 * @brief      注入鼠标按键/移动事件（对标 QWindowSystemInterface::handleMouseEvent）。
 * @details    平台鼠标按下/释放/移动时调用；构造 XMouseEvent（携带触发放
 *             键 button、当前按下按键集合 buttons、修饰键与局部坐标）并
 *             自发投递。双击由平台识别后以
 *             XEVENT_TYPE_MOUSE_BUTTON_DBL_CLICK 类型注入。
 * @param      window    目标窗口；不可为 NULL。
 * @param      type      事件类型：XEVENT_TYPE_MOUSE_BUTTON_PRESS /
 *                       MOUSE_BUTTON_RELEASE / MOUSE_BUTTON_DBL_CLICK /
 *                       MOUSE_MOVE。
 * @param      button    触发该事件的按键；移动事件传 NoButton。
 * @param      buttons   事件发生时处于按下状态的按键位掩码。
 * @param      modifiers 事件发生时按下的修饰键。
 * @param      position  窗口局部坐标。
 * @return     true 已派发；false 参数无效或分配失败。
 */
bool XWindowSystemInterface_handleMouseEvent(XWindow* window, XEventType type,
                                             XMouseButton button,
                                             XMouseButton buttons,
                                             XKeyboardModifiers modifiers,
                                             XPoint position);

/**
 * @brief      注入滚轮事件（对标 QWindowSystemInterface::handleWheelEvent）。
 * @details    平台滚轮滚动时调用；构造 XWheelEvent（携带局部坐标、角度增量
 *             angleDelta、按下按键与修饰键）并自发投递。角度增量遵循 Qt
 *             约定：普通刻度滚轮每次 ±120，向上/向右为正（垂直在 y 轴、
 *             水平在 x 轴）。
 * @param      window     目标窗口；不可为 NULL。
 * @param      buttons    事件发生时按下的鼠标按键位掩码。
 * @param      modifiers  事件发生时按下的修饰键。
 * @param      position   窗口局部坐标。
 * @param      angleDelta 角度增量；可为 NULL（按 0,0）。
 * @return     true 已派发；false 参数无效或分配失败。
 */
bool XWindowSystemInterface_handleWheelEvent(XWindow* window,
                                             XMouseButton buttons,
                                             XKeyboardModifiers modifiers,
                                             XPoint position,
                                             const XPoint* angleDelta);

/**
 * @brief      注入指针进入事件（对标 QWindowSystemInterface::handleEnterEvent）。
 * @details    平台指针首次进入窗口客户区时调用；构造 XEnterEvent（携带局部
 *             坐标与屏幕全局坐标）并自发投递，由 XWindow enterEvent 槽处理。
 * @param      window         目标窗口；不可为 NULL。
 * @param      position       窗口局部坐标。
 * @param      globalPosition 屏幕全局坐标；可为 NULL（按 0,0）。
 */
void XWindowSystemInterface_handleEnterEvent(XWindow* window,
                                             XPoint position,
                                             const XPoint* globalPosition);

/**
 * @brief      注入指针离开事件（对标 QWindowSystemInterface::handleLeaveEvent）。
 * @details    平台指针离开窗口客户区时调用；投递无负载的
 *             XEVENT_TYPE_LEAVE 普通事件，由 XWindow leaveEvent 槽处理。
 * @param      window 目标窗口；不可为 NULL。
 */
void XWindowSystemInterface_handleLeaveEvent(XWindow* window);

/**
 * @brief      冲刷窗口系统事件队列（对标 QWindowSystemInterface::flushWindowSystemEvents）。
 * @details    处理此前经 XGuiApplication_postEvent 排队的事件；同步注入
 *             系列不排队，本函数主要供平台在无主事件循环的运行间隙冲刷。
 * @param      flags 事件处理标志（XEventLoopProcessEventsFlags）。
 */
void XWindowSystemInterface_flushWindowSystemEvents(XEventLoopProcessEventsFlags flags);

#endif /* XWINDOWSYSTEMINTERFACE_ON && XGUIAPPLICATION_ON && XWINDOW_ON && XWINDOWEVENT_ON */

#ifdef __cplusplus
}
#endif
#endif /* XWINDOWSYSTEMINTERFACE_H */
