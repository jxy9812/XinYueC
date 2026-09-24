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
 * @brief      注入窗口状态变化（对标 QWindowSystemInterface::
 *             handleWindowStateChanged）。
 * @details    平台后端收到窗口管理器的状态实测结果（WM 图标化、EWMH
 *             _NET_WM_STATE 回送、Win32 WM_SIZE 状态分支等）时调用：
 *             经 XWindow_reportWindowStateChanged 持久化状态位并发射
 *             XWindow 的 windowStateChanged 信号、联动可见性枚举；
 *             与 XWindow_setWindowStates 的主动请求路径不同，本入口
 *             不回写平台层（避免注入回环）。
 * @param      window   目标窗口；可为 NULL（no-op）。
 * @param      newState 平台上报的状态组合（可含 WindowActive 位）。
 */
void XWindowSystemInterface_handleWindowStateChanged(XWindow* window,
                                                     XWindowState newState);

/**
 * @brief      注入屏幕接入（对标 QWindowSystemInterface::handleScreenAdded）。
 * @details    平台后端枚举到新屏幕时调用：登记到 XScreen 注册表并发射
 *             XGuiApplication 的 screenAdded 信号。屏幕所有权归平台层。
 * @param      screen 新屏幕；可为 NULL（no-op）。
 */
void XWindowSystemInterface_handleScreenAdded(XScreen* screen);

/**
 * @brief      注入屏幕移除（对标 QWindowSystemInterface::handleScreenRemoved）。
 * @details    平台后端屏幕热拔时调用：从 XScreen 注册表注销并发射
 *             screenRemoved 信号；屏幕对象仍由平台层释放。
 * @param      screen 被移除屏幕；可为 NULL（no-op）。
 */
void XWindowSystemInterface_handleScreenRemoved(XScreen* screen);

/**
 * @brief      注入屏幕几何变化（对标 QWindowSystemInterface::
 *             handleScreenGeometryChange）。
 * @details    平台后端收到 RandR 几何变更时调用：把新几何同步进 XScreen
 *             （内部按变化发射 geometryChanged/availableGeometryChanged/
 *             virtualGeometryChanged/physicalDotsPerInchChanged 等）。
 * @param      screen 目标屏幕；可为 NULL（no-op）。
 * @param      geometry 新几何；可为 NULL 表示不更新几何。
 * @param      availableGeometry 新可用几何；可为 NULL 表示跟随 geometry
 *             （与 QPlatformScreen 缺省语义一致）。
 */
void XWindowSystemInterface_handleScreenGeometryChange(XScreen* screen,
                                                       const XRect* geometry,
                                                       const XRect* availableGeometry);

/**
 * @brief      注入屏幕逻辑 DPI 变化（对标 QWindowSystemInterface::
 *             handleScreenLogicalDotsPerInchChange）。
 * @details    平台后端逻辑 DPI（如 Xft.dpi 资源）变化时调用；同时更新
 *             水平/垂直逻辑 DPI，值变化时发射 logicalDotsPerInchChanged。
 * @param      screen 目标屏幕；可为 NULL（no-op）。
 * @param      dpi 新逻辑 DPI（水平与垂直同值）。
 */
void XWindowSystemInterface_handleScreenLogicalDotsPerInchChange(XScreen* screen,
                                                                 float dpi);

/**
 * @brief      注入平台主题变化（对标 QWindowSystemInterface::
 *             handleThemeChanged）。
 * @details    平台主题（深/浅色方案等）变化时调用。XGui 的主题落点是
 *             XStyleHints 的颜色方案（XGui 未建独立 Theme 状态，styleHints
 *             亦无 themeChanged 信号，对齐其 colorScheme/
 *             colorSchemeChanged，对标 Qt 6.5+ 深浅色通道）：转发
 *             XStyleHints_setColorScheme，值变化时由其内部发射
 *             colorSchemeChanged。styleHints 单例不可用（XSTYLEHINTS_ON=0
 *             或无应用实例）时 no-op。
 * @param      theme 新主题对应的颜色方案（XStyleHintsColorScheme）。
 */
void XWindowSystemInterface_handleThemeChanged(XStyleHintsColorScheme theme);

/**
 * @brief      注入平台区域设置变化（对标 QWindowSystemInterface::
 *             handleLocaleChange）。
 * @details    系统区域设置变化时调用；localeUtf8 沿用 XGui 全框架的
 *             BCP 47/POSIX 名称字符串约定（XGui 未建 XLocale 类型）。
 *             转发 XGuiApplication_setPlatformLocaleUtf8：落位平台区域
 *             设置并在请求方向为 Auto 时重解析有效布局方向（值变化时
 *             发射 layoutDirectionChanged）。NULL/空串表示清除注入态。
 * @param      localeUtf8 新区域设置名（UTF-8）；可为 NULL。
 */
void XWindowSystemInterface_handleLocaleChange(const char* localeUtf8);

/**
 * @brief      注入应用状态变化（对标 QWindowSystemInterface::
 *             handleApplicationStateChanged）。
 * @details    平台应用生命周期状态（活动/隐藏/挂起，如移动端前后台切换、
 *             桌面会话挂起）变化时调用：转发 XGuiApplication_setApplicationState
 *             （值变化时内部发射 applicationStateChanged）。
 * @param      state 新应用状态（XGuiApplicationState 位组合）。
 */
void XWindowSystemInterface_handleApplicationStateChanged(
        XGuiApplicationState state);

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
 * @brief      注入键盘事件（完整负载版，对标 handleKeyEvent 带
 *             nativeScanCode/timestamp 的平台通道）。
 * @details    与 handleKeyEvent 相同，另携带平台扫描码与事件时间戳
 *             （X11 为 xkey.keycode 与 xkey.time 毫秒值；Win32 为
 *             lParam 硬件扫描码与消息时间）。未知时传 0（事件字段
 *             归零，向后兼容既有调用方）。
 * @param      nativeScanCode 平台扫描码；未知传 0。
 * @param      timestamp      事件时间戳（毫秒）；未知传 0。
 * @return     true 已派发；false 参数无效或分配失败。
 */
bool XWindowSystemInterface_handleKeyEvent_ex(XWindow* window, XEventType type,
                                              int key, XKeyboardModifiers modifiers,
                                              bool autoRepeat,
                                              uint32_t nativeScanCode,
                                              uint32_t timestamp);

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
 * @brief      注入鼠标按键/移动事件（完整负载版，对标 handleMouseEvent 带
 *             globalPosition/timestamp 的平台通道）。
 * @details    与 handleMouseEvent 相同，另携带屏幕全局坐标与事件时间戳
 *             （X11 为 xbutton/xmotion 的 x_root/y_root 与 time 毫秒值）。
 *             未知时传 NULL/0（事件字段归零，向后兼容既有调用方）。
 * @param      globalPosition 屏幕全局坐标；可为 NULL（按 (0,0)）。
 * @param      timestamp      事件时间戳（毫秒）；未知传 0。
 * @return     true 已派发；false 参数无效或分配失败。
 */
bool XWindowSystemInterface_handleMouseEvent_ex(XWindow* window, XEventType type,
                                                XMouseButton button,
                                                XMouseButton buttons,
                                                XKeyboardModifiers modifiers,
                                                XPoint position,
                                                const XPoint* globalPosition,
                                                uint32_t timestamp);

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
 * @brief      注入触摸事件（对标 QWindowSystemInterface::handleTouchEvent）。
 * @details    嵌入式触摸屏驱动 / X11 XI2（后续接入，见下注）在触摸按下/
 *             移动/抬起时调用；构造 XTouchEvent（携带主点局部坐标、屏幕
 *             坐标与触点数量）并自发投递。事件经 XWidgetWindow 桥接窗口
 *             命中派发到控件 touchEvent 虚槽（对标 QWidgetWindow::
 *             handleTouchEvent 形态）。
 *             多点语义按 XTouchEvent 现有最小负载设计：只承载主点
 *             （首个触点）坐标 + 触点计数，完整触点列表为已知偏差
 *             （XWindowEvent.h Task 2.20），不新造字段。
 *             Qt 语义：TouchBegin 被命中控件接受后该触点被隐式抓取，
 *             后续 UPDATE/END 直达抓取控件；TOUCH_END/TOUCH_CANCEL 后
 *             抓取清理（对标 QGuiApplicationPrivate::processTouchEvent
 *             的触点 grab 生命周期）。touch→mouse 仿真已接（默认开，
 *             XWidget_setTouchMouseSynthesisEnabled 可关；应用属性
 *             AA_SynthesizeMouseForUnhandledTouchEvents 经
 *             XGuiApplication_setAttribute 同步该开关）。
 * @note       X11 XI2 触摸合成（XIGetSelectedEvents → XIDeviceEvent 翻译）
 *             与嵌入式 tslib/evdev 注入不在本批（无硬件验证手段）；本
 *             函数即两者后续的统一注入点，可由 /tmp 探针直接合成验证。
 * @param      window         目标窗口；不可为 NULL。
 * @param      type           事件类型：XEVENT_TYPE_TOUCH_BEGIN /
 *                            TOUCH_UPDATE / TOUCH_END / TOUCH_CANCEL。
 * @param      position       主点（首个触点）窗口局部坐标。
 * @param      globalPosition 主点屏幕坐标；可为 NULL（按 0,0）。
 * @param      pointCount     触点数量（<1 按 1 处理，主点必存在）。
 * @return     true 已派发；false 参数无效或分配失败。
 */
bool XWindowSystemInterface_handleTouchEvent(XWindow* window, XEventType type,
                                             XPoint position,
                                             const XPoint* globalPosition,
                                             int pointCount);

/**
 * @brief      注入触摸事件（完整负载版，携带事件时间戳）。
 * @details    与 handleTouchEvent 相同，另携带平台触摸时间戳（X11 XI2
 *             XIDeviceEvent 的 time 毫秒值）。未知时传 0。时间戳经
 *             XWindowSystemInterface_touchTimestamp 供 touch→mouse 合成器
 *             透传到合成鼠标事件（对标 Qt 合成 QMouseEvent 继承触摸
 *             timestamp 语义）。XTouchEvent 负载结构本批未扩展（字段
 *             归属 XWindowEvent.h），时间戳以同步派发作用域的通道承载：
 *             注入为同步自发投递，合成器在投递期间读取即得本序列时间。
 * @param      timestamp 事件时间戳（毫秒）；未知传 0。
 * @return     true 已派发；false 参数无效或分配失败。
 */
bool XWindowSystemInterface_handleTouchEvent_ex(XWindow* window, XEventType type,
                                                XPoint position,
                                                const XPoint* globalPosition,
                                                int pointCount,
                                                uint32_t timestamp);

/**
 * @brief      注入多点触摸事件（方案 B 多点；对标 QWindowSystemInterface::
 *             handleTouchEvent 的 QEventPoint 列表形态）。
 * @details    与 handleTouchEvent_ex 同层：同步自发投递，timestamp 经静态
 *             通道透传给 touch→mouse 合成器。触点列表深拷贝进事件；主点
 *             字段自动同步为 points[0]。
 * @param      window 目标窗口；不可为 NULL。
 * @param      type 触摸事件类型（BEGIN/UPDATE/END/CANCEL）。
 * @param      points 触点数组（借用；不可为 NULL）。
 * @param      count 触点数量（>=1）。
 * @param      timestamp 平台触摸时间戳（毫秒）。
 * @return     true 已投递；false 参数非法或分配失败。
 */
bool XWindowSystemInterface_handleTouchPoints_ex(XWindow* window,
                                                 XEventType type,
                                                 const XTouchPoint* points,
                                                 int count,
                                                 uint32_t timestamp);

/**
 * @brief      返回当前同步派发中的触摸事件时间戳（毫秒）。
 * @details    供 XWidget.c 的 touch→mouse 合成器在触摸事件同步投递期间
 *             读取并透传到合成鼠标事件；仅在 handleTouchEvent(_ex) 同步
 *             派发栈内有定义，其他时刻返回 0（合成事件按未知时间处理）。
 * @return     当前触摸时间戳；无同步触摸派发时为 0。
 */
uint32_t XWindowSystemInterface_touchTimestamp(void);

/**
 * @brief      注入数位板事件（对标 QWindowSystemInterface::handleTabletEvent）。
 * @details    平台数位板驱动（XI2/XlsInput，后续接入）在笔按下/移动/抬起
 *             时调用；构造 XTabletEvent（携带局部坐标、屏幕坐标、压力与
 *             指针类型）并自发投递，经 XWidgetWindow 桥接窗口按鼠标一致
 *             的命中路径派发到控件 tabletEvent 虚槽（对标 QWidgetWindow::
 *             handleTabletEvent 的 childAt 命中形态；tablet 按压不参与
 *             触摸抓取）。
 * @note       X11 XI2 数位板合成不在本批（无硬件验证手段）；本函数即
 *             后续注入点。
 * @param      window         目标窗口；不可为 NULL。
 * @param      type           事件类型：XEVENT_TYPE_TABLET_PRESS /
 *                            TABLET_RELEASE / TABLET_MOVE。
 * @param      position       窗口局部坐标。
 * @param      globalPosition 屏幕坐标；可为 NULL（按 0,0）。
 * @param      pressure       压力（0.0~1.0）。
 * @param      pointerType    指针类型（XTabletPointerType）。
 * @return     true 已派发；false 参数无效或分配失败。
 */
bool XWindowSystemInterface_handleTabletEvent(XWindow* window, XEventType type,
                                              XPoint position,
                                              const XPoint* globalPosition,
                                              float pressure, int pointerType);

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
