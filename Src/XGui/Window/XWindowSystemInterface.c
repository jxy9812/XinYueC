/******************************************************************************
 * @file       XWindowSystemInterface.c
 * @brief      窗口系统事件注入接口实现（对标 Qt 6.8 QWindowSystemInterface）。
 * @details    平台后端调用 handle* 注入函数时，构造携带负载的具体事件
 *             （XResizeEvent / XExposeEvent / XPaintEvent / XFocusEvent /
 *             XCloseEvent / XShowEvent / XHideEvent / XKeyEvent /
 *             XMouseEvent / XWheelEvent / XEnterEvent / XTouchEvent /
 *             XTabletEvent），并经
 *             XGuiApplication_sendSpontaneousEvent 以自发事件语义同步
 *             投递，XWindow_event_base 按事件类型路由到对应窗口事件槽。
 *             屏幕接入系列（handleScreenAdded/Removed/GeometryChange/
 *             LogicalDotsPerInchChange，对标 QWindowSystemInterface 同名
 *             接口）为同步属性同步入口：直接转发 XScreen 注册表与
 *             XGuiApplication 屏幕信号，不走事件队列。
 *             事件投递后立即释放，符合 XEvent 的事件所有权约定。
 *             本文件不引用任何平台 API，嵌入式可用。
 * @note       模块总开关 XWINDOWSYSTEMINTERFACE_ON 定义于 XGuiConfig.h；
 *             依赖 XGUIAPPLICATION_ON / XWINDOW_ON / XWINDOWEVENT_ON。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XWindowSystemInterface.h"

#include "XAlgorithm.h"
#include "XMemory.h"
#include "XPrintf.h"

#if XWINDOWSYSTEMINTERFACE_ON && XGUIAPPLICATION_ON && XWINDOW_ON && XWINDOWEVENT_ON

/** @brief 当前同步派发中的触摸事件时间戳（毫秒）；仅 handleTouchEvent(_ex)
 *         同步投递栈内有定义，供 touch→mouse 合成器透传（详见
 *         XWindowSystemInterface_touchTimestamp 注释）。 */
static uint32_t g_touchTimestamp = 0;

void XWindowSystemInterface_handleGeometryChange(XWindow* window, const XRect* rect)
{
    XResizeEvent* event;
    XRect old;
    XSize oldSize;
    XSize newSize;
    if (!window || !rect) return;

    /* 1) 持久化新几何：保证 resize 槽内可读到新尺寸（Qt 语义）。 */
    old = XWindow_geometry(window);
    oldSize.width = old.width;
    oldSize.height = old.height;
    XWindow_setGeometry_rect(window, rect);

    /* 2) 投递 Resize 事件（oldSize 为变化前尺寸）。 */
    newSize.width = XWindow_width(window);
    newSize.height = XWindow_height(window);
    event = XResizeEvent_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                   XEVENT_TYPE_RESIZE, &newSize, &oldSize);
    if (!event) return;
    XGuiApplication_sendSpontaneousEvent((XObject*)window, (XEvent*)event);
    XEvent_delete_base((XEvent*)event);
}

bool XWindowSystemInterface_handleExposeEvent(XWindow* window, const XRegion* region)
{
    XExposeEvent* event;
    XRegion payload;
    bool handled;
    bool exposed;
    if (!window) return false;
    event = XExposeEvent_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                   XEVENT_TYPE_EXPOSE, region);
    if (!event) return false;
    /* 对齐 Qt QGuiApplicationPrivate::processExposeEvent：投递前先把
       「是否暴露」同步到窗口（区域非空即已暴露，区域为空即整窗隐藏）。 */
    payload = XExposeEvent_region(event);
    exposed = !XRegion_isEmpty(&payload);
    XRegion_deinit(&payload);
    XWindow_setExposed(window, exposed);
    handled = XGuiApplication_sendSpontaneousEvent((XObject*)window, (XEvent*)event);
    XEvent_delete_base((XEvent*)event);
    return handled;
}

bool XWindowSystemInterface_handlePaintEvent(XWindow* window, const XRegion* region)
{
    XPaintEvent* event;
    bool handled;
    if (!window) return false;
    event = XPaintEvent_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                  XEVENT_TYPE_PAINT, region);
    if (!event) return false;
    handled = XGuiApplication_sendSpontaneousEvent((XObject*)window, (XEvent*)event);
    XEvent_delete_base((XEvent*)event);
    return handled;
}

void XWindowSystemInterface_handleFocusWindowChanged(XWindow* window,
                                                     XFocusReason reason)
{
    XWindow* oldFocused;
    XFocusEvent* event;

    /* 根因修复（复扫 R-34）：此前只向新窗口投递 FocusIn，从不更新
       XGuiApplication 焦点窗口——focusWindow() 脱钩、focusWindowChanged
       不发射、IME setFocusObject 链不触发。对标
       QGuiApplicationPrivate::processFocusWindowChanged 在本入口收口
       完整焦点切换语义：
       1) 非顶层窗口折算到其顶层（Qt: window = window->window()）；
       2) 向旧焦点窗口补发 FocusOut（先失焦后聚焦，与 Qt 同序）；
       3) 向新焦点窗口派发 FocusIn；
       4) 经 XGuiApplication_setFocusWindow 更新焦点窗口（值变化时内部
          发射 focusWindowChanged，并联动 focusObject / 平台输入上下文
          setFocusObject 的 IME 链）。
       新旧窗口的 m_active / activeChanged 联动由 XWindow 焦点事件路由
       （XWindow.c VXWindow_event）在事件派发时统一完成，与本入口解耦。 */

    /* Qt 语义：子窗口获得焦点即其顶层获得焦点（沿普通父链上溯）。 */
    while (window) {
        XWindow* parent = XWindow_parent(window,
                                         XWindowAncestor_ExcludeTransients);
        if (!parent) break;
        window = parent;
    }

    oldFocused = XGuiApplication_focusWindow();
    if (oldFocused == window) return; /* Qt：焦点窗口未变直接返回。 */

    /* 旧窗口补发 FocusOut。仅当旧窗口仍处于激活态时补发：按本头文件
       既有约定，平台后端会在切换前自行向旧窗口投递 FocusOut（X11
       FocusOut 原生事件直投路径），以 isActive 守卫保证同一失焦只
       派发一次；纯 WSI 注入路径（平台未先派发）则由此处补齐 Qt 语义。 */
    if (oldFocused && XWindow_isActive(oldFocused)) {
        event = XFocusEvent_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                      XEVENT_TYPE_FOCUS_OUT, reason);
        if (event) {
            XGuiApplication_sendSpontaneousEvent((XObject*)oldFocused,
                                                 (XEvent*)event);
            XEvent_delete_base((XEvent*)event);
        }
    }

    /* 新窗口 FocusIn（window 为 NULL 表示整体失焦：跳过聚焦、仅走
       下方焦点窗口清空，与 Qt processFocusWindowChanged(NULL) 一致）。 */
    if (window) {
        event = XFocusEvent_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                      XEVENT_TYPE_FOCUS_IN, reason);
        if (!event) return;
        XGuiApplication_sendSpontaneousEvent((XObject*)window,
                                             (XEvent*)event);
        XEvent_delete_base((XEvent*)event);
    }

    /* 更新应用焦点窗口与焦点对象：内部发射 focusWindowChanged /
       focusObjectChanged，并经平台输入上下文 setFocusObject 触发
       IME 聚焦链（此三件事此前全部脱钩）。object 传 NULL 按应用层
       既有语义缺省为窗口自身。 */
    XGuiApplication_setFocusWindow(window, NULL);
}

void XWindowSystemInterface_handleWindowStateChanged(XWindow* window,
                                                     XWindowState newState)
{
    /* 对标 QWindowSystemInterface::handleWindowStateChanged：平台是窗口
       状态的事实来源（WM 实测结果），经 report 通道持久化并发射
       windowStateChanged 信号、联动可见性（内部不回写平台层）。 */
    if (!window) return;
    XWindow_reportWindowStateChanged(window, newState);
}

void XWindowSystemInterface_handleScreenAdded(XScreen* screen)
{
    /* 对标 QWindowSystemInterface::handleScreenAdded：平台层枚举到屏幕后
       统一经本入口登记；登记与 screenAdded 信号由 XGuiApplication 负责。 */
    if (!screen) return;
    XGuiApplication_screenAdded(screen);
}

void XWindowSystemInterface_handleScreenRemoved(XScreen* screen)
{
    if (!screen) return;
    XGuiApplication_screenRemoved(screen);
}

void XWindowSystemInterface_handleScreenGeometryChange(XScreen* screen,
                                                       const XRect* geometry,
                                                       const XRect* availableGeometry)
{
#if XSCREEN_ON
    if (!screen) return;
    /* 更新顺序与 QPlatformScreen::geometryChanged 一致：先 geometry
       （其内部已联动 availableGeometry/virtualGeometry/物理 DPI），再按
       平台提供的独立可用几何覆盖。 */
    if (geometry) XScreen_setGeometry(screen, geometry);
    if (availableGeometry) XScreen_setAvailableGeometry(screen, availableGeometry);
#else
    (void)screen; (void)geometry; (void)availableGeometry;
#endif /* XSCREEN_ON */
}

void XWindowSystemInterface_handleScreenLogicalDotsPerInchChange(XScreen* screen,
                                                                 float dpi)
{
#if XSCREEN_ON
    if (!screen) return;
    /* Qt 的 handleScreenLogicalDotsPerInchChange 用单值同时更新 X/Y。 */
    XScreen_setLogicalDotsPerInch(screen, dpi, dpi);
#else
    (void)screen; (void)dpi;
#endif /* XSCREEN_ON */
}

void XWindowSystemInterface_handleThemeChanged(XStyleHintsColorScheme theme)
{
#if XSTYLEHINTS_ON
    /* 对标 QGuiApplicationPrivate::processThemeChanged：平台主题变化
       进入 styleHints 颜色方案（XGui 的 Theme 落点），值变化时由
       XStyleHints_setColorScheme 内部发射 colorSchemeChanged。 */
    XStyleHints* hints = XGuiApplication_styleHints();
    if (hints) XStyleHints_setColorScheme(hints, theme);
#else
    (void)theme;
#endif /* XSTYLEHINTS_ON */
}

void XWindowSystemInterface_handleLocaleChange(const char* localeUtf8)
{
    /* 对标 QWindowSystemInterface::handleLocaleChange：区域设置落位应用
       注入态（BCP 47 名称字符串），请求方向为 Auto 时按新语言重解析
       有效布局方向（值变化时发射 layoutDirectionChanged）。 */
    XGuiApplication_setPlatformLocaleUtf8(localeUtf8);
}

void XWindowSystemInterface_handleApplicationStateChanged(
        XGuiApplicationState state)
{
    /* 对标 QWindowSystemInterface::handleApplicationStateChanged：转发
       应用状态（值变化时内部发射 applicationStateChanged）。 */
    XGuiApplication_setApplicationState(state);
}

bool XWindowSystemInterface_handleCloseEvent(XWindow* window)
{
    XCloseEvent* event;
    bool handled;
    if (!window) return false;
    event = XCloseEvent_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XEVENT_TYPE_CLOSE);
    if (!event) return false;
    handled = XGuiApplication_sendSpontaneousEvent((XObject*)window, (XEvent*)event);
    if (handled) {
        /* 关闭事件以 accept 状态表达「是否允许关闭」，与 QCloseEvent 一致。 */
        handled = XEvent_isAccepted((const XEvent*)event);
    }
    XEvent_delete_base((XEvent*)event);
    return handled;
}

bool XWindowSystemInterface_handleShowEvent(XWindow* window)
{
    XShowEvent* event;
    bool handled;
    if (!window) return false;
    event = XShowEvent_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XEVENT_TYPE_SHOW);
    if (!event) return false;
    handled = XGuiApplication_sendSpontaneousEvent((XObject*)window, (XEvent*)event);
    XEvent_delete_base((XEvent*)event);
    return handled;
}

bool XWindowSystemInterface_handleHideEvent(XWindow* window)
{
    XHideEvent* event;
    bool handled;
    if (!window) return false;
    event = XHideEvent_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XEVENT_TYPE_HIDE);
    if (!event) return false;
    handled = XGuiApplication_sendSpontaneousEvent((XObject*)window, (XEvent*)event);
    XEvent_delete_base((XEvent*)event);
    return handled;
}

bool XWindowSystemInterface_handleKeyEvent(XWindow* window, XEventType type,
                                             int key, XKeyboardModifiers modifiers,
                                             bool autoRepeat)
{
    /* 既有签名向后兼容：平台未提供扫描码/时间时按 0 委托完整负载版。 */
    return XWindowSystemInterface_handleKeyEvent_ex(window, type, key,
                                                    modifiers, autoRepeat, 0, 0);
}

bool XWindowSystemInterface_handleKeyEvent_ex(XWindow* window, XEventType type,
                                              int key, XKeyboardModifiers modifiers,
                                              bool autoRepeat,
                                              uint32_t nativeScanCode,
                                              uint32_t timestamp)
{
    XKeyEvent* event;
    if (!window) return false;
    event = XKeyEvent_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, type, key, modifiers);
    if (!event) return false;
    XKeyEvent_setAutoRepeat(event, autoRepeat);
    XKeyEvent_setNativeScanCode(event, nativeScanCode);
    XKeyEvent_setTimestamp(event, timestamp);
    XGuiApplication_sendSpontaneousEvent((XObject*)window, (XEvent*)event);
    XEvent_delete_base((XEvent*)event);
    return true;
}

bool XWindowSystemInterface_handleInputMethodEvent(
        XWindow* window, const char* preeditUtf8, const char* commitUtf8,
        int replacementStart, int replacementLength,
        int cursorPosition, int anchorPosition)
{
    XString* preedit;
    XString* commit;
    XInputMethodEvent* event;
    bool handled;
    if (!window) return false;
    preedit = XString_create_utf8(preeditUtf8 ? preeditUtf8 : "");
    commit = XString_create_utf8(commitUtf8 ? commitUtf8 : "");
    if (!preedit || !commit) {
        if (preedit) XString_delete_base((XClass*)preedit);
        if (commit) XString_delete_base((XClass*)commit);
        return false;
    }
    event = XInputMethodEvent_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, preedit,
                                        commit, replacementStart,
                                        replacementLength, cursorPosition,
                                        anchorPosition);
    XString_delete_base((XClass*)preedit);
    XString_delete_base((XClass*)commit);
    if (!event) return false;
    /* 与其它窗口系统接口事件保持一致：经应用自发事件入口投递，
       由 XWidgetWindow 桥接到当前焦点控件（例如 XLineEdit）。
       直接调用窗口槽会绕过 notify/vtable 事件链，导致输入法提交
       只到达窗口而不会到达实际编辑控件。 */
    handled = XGuiApplication_sendSpontaneousEvent((XObject*)window,
                                                    (XEvent*)event);
    XEvent_delete_base((XEvent*)event);
    return handled;
}

bool XWindowSystemInterface_handleDropEvent(
        XWindow* window, XEventType type, XPoint position,
        const XPoint* globalPosition, const char* mimeTypeUtf8,
        const char* dataUtf8)
{
    XString* mimeType;
    XString* data;
    XDropEvent* event;
    bool handled;
    if (!window || (type != XEVENT_TYPE_DRAG_ENTER &&
                    type != XEVENT_TYPE_DRAG_MOVE &&
                    type != XEVENT_TYPE_DRAG_LEAVE && type != XEVENT_TYPE_DROP))
        return false;
    mimeType = XString_create_utf8(mimeTypeUtf8 ? mimeTypeUtf8 : "");
    data = XString_create_utf8(dataUtf8 ? dataUtf8 : "");
    if (!mimeType || !data) {
        if (mimeType) XString_delete_base((XClass*)mimeType);
        if (data) XString_delete_base((XClass*)data);
        return false;
    }
    event = XDropEvent_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, type, &position,
                                 globalPosition, mimeType, data);
    XString_delete_base((XClass*)mimeType);
    XString_delete_base((XClass*)data);
    if (!event) return false;
    handled = XGuiApplication_sendSpontaneousEvent((XObject*)window,
                                                   (XEvent*)event);
    handled = handled && XEvent_isAccepted((XEvent*)event);
    XEvent_delete_base((XEvent*)event);
    return handled;
}

bool XWindowSystemInterface_handleMouseEvent(XWindow* window, XEventType type,
                                             XMouseButton button,
                                             XMouseButton buttons,
                                             XKeyboardModifiers modifiers,
                                             XPoint position)
{
    /* 既有签名向后兼容：平台未提供全局坐标/时间时按零值委托完整负载版。 */
    return XWindowSystemInterface_handleMouseEvent_ex(window, type, button,
                                                      buttons, modifiers,
                                                      position, NULL, 0);
}

bool XWindowSystemInterface_handleMouseEvent_ex(XWindow* window, XEventType type,
                                                XMouseButton button,
                                                XMouseButton buttons,
                                                XKeyboardModifiers modifiers,
                                                XPoint position,
                                                const XPoint* globalPosition,
                                                uint32_t timestamp)
{
    XMouseEvent* event;
    if (!window) return false;
    event = XMouseEvent_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, type, button,
                                  modifiers, position);
    if (!event) return false;
    XMouseEvent_setButtons(event, buttons);
    XMouseEvent_setGlobalPosition(event, globalPosition);
    XMouseEvent_setTimestamp(event, timestamp);
    XGuiApplication_sendSpontaneousEvent((XObject*)window, (XEvent*)event);
    XEvent_delete_base((XEvent*)event);
    return true;
}

bool XWindowSystemInterface_handleWheelEvent(XWindow* window,
                                             XMouseButton buttons,
                                             XKeyboardModifiers modifiers,
                                             XPoint position,
                                             const XPoint* angleDelta)
{
    XWheelEvent* event;
    if (!window) return false;
    event = XWheelEvent_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                  XEVENT_TYPE_WHEEL, &position, NULL,
                                  angleDelta, buttons, modifiers);
    if (!event) return false;
    XGuiApplication_sendSpontaneousEvent((XObject*)window, (XEvent*)event);
    XEvent_delete_base((XEvent*)event);
    return true;
}

bool XWindowSystemInterface_handleTouchEvent(XWindow* window, XEventType type,
                                             XPoint position,
                                             const XPoint* globalPosition,
                                             int pointCount)
{
    /* 既有签名向后兼容：平台未提供时间时按 0 委托完整负载版。 */
    return XWindowSystemInterface_handleTouchEvent_ex(window, type, position,
                                                      globalPosition,
                                                      pointCount, 0);
}

bool XWindowSystemInterface_handleTouchEvent_ex(XWindow* window, XEventType type,
                                                XPoint position,
                                                const XPoint* globalPosition,
                                                int pointCount,
                                                uint32_t timestamp)
{
    XTouchEvent* event;
    /* 对标 QGuiApplicationPrivate::processTouchEvent 的 WSI 入口形态：
       平台后端只负责翻译原生触摸流，合成/命中/派发统一在本入口之后。
       X11 XI2 合成与嵌入式 tslib/evdev 注入不在本批（无硬件验证手段），
       本函数即其后续统一注入点（/tmp 探针可直接合成验证）。 */
    if (!window || (type != XEVENT_TYPE_TOUCH_BEGIN &&
                    type != XEVENT_TYPE_TOUCH_UPDATE &&
                    type != XEVENT_TYPE_TOUCH_END &&
                    type != XEVENT_TYPE_TOUCH_CANCEL))
        return false;
    if (pointCount < 1) pointCount = 1;
    /* 记录本序列时间戳供合成器读取：注入为同步自发投递（调用返回时
       事件已处理完毕），静态值的作用域即"当前同步派发中的触摸事件"，
       合成器在投递栈内读取恰好命中本序列；XTouchEvent 负载结构未扩展
       （字段归属 XWindowEvent.h，不在本批改动面），以通道方式透传。 */
    g_touchTimestamp = timestamp;
    event = XTouchEvent_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, type, &position,
                                  globalPosition, pointCount);
    if (!event) {
        g_touchTimestamp = 0;
        return false;
    }
    XGuiApplication_sendSpontaneousEvent((XObject*)window, (XEvent*)event);
    XEvent_delete_base((XEvent*)event);
    g_touchTimestamp = 0;
    return true;
}

bool XWindowSystemInterface_handleTouchPoints_ex(XWindow* window,
                                                 XEventType type,
                                                 const XTouchPoint* points,
                                                 int count,
                                                 uint32_t timestamp)
{
    XTouchEvent* event;
    if (!window || !points || count < 1 ||
        (type != XEVENT_TYPE_TOUCH_BEGIN &&
         type != XEVENT_TYPE_TOUCH_UPDATE &&
         type != XEVENT_TYPE_TOUCH_END &&
         type != XEVENT_TYPE_TOUCH_CANCEL))
        return false;
    g_touchTimestamp = timestamp;
    /* 先建 1 点事件（旧 init 签名，主点取 points[0]），再注入完整列
       表（深拷贝+主点同步在 setPoints 内完成）。 */
    event = XTouchEvent_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, type,
                                  &points[0].m_position,
                                  &points[0].m_globalPosition, 1);
    if (!event) {
        g_touchTimestamp = 0;
        return false;
    }
    XTouchEvent_setPoints(event, points, count);
    XGuiApplication_sendSpontaneousEvent((XObject*)window, (XEvent*)event);
    XEvent_delete_base((XEvent*)event);
    g_touchTimestamp = 0;
    return true;
}

uint32_t XWindowSystemInterface_touchTimestamp(void)
{
    return g_touchTimestamp;
}

bool XWindowSystemInterface_handleTabletEvent(XWindow* window, XEventType type,
                                              XPoint position,
                                              const XPoint* globalPosition,
                                              float pressure, int pointerType)
{
    XTabletEvent* event;
    /* 对标 QGuiApplicationPrivate::processTabletEvent：平台翻译原生笔事件
       后统一经本入口投递；X11 XI2 合成不在本批（无硬件验证手段）。 */
    if (!window || (type != XEVENT_TYPE_TABLET_PRESS &&
                    type != XEVENT_TYPE_TABLET_RELEASE &&
                    type != XEVENT_TYPE_TABLET_MOVE))
        return false;
    event = XTabletEvent_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, type, &position,
                                   globalPosition, pressure, pointerType);
    if (!event) return false;
    XGuiApplication_sendSpontaneousEvent((XObject*)window, (XEvent*)event);
    XEvent_delete_base((XEvent*)event);
    return true;
}

void XWindowSystemInterface_handleEnterEvent(XWindow* window,
                                             XPoint position,
                                             const XPoint* globalPosition)
{
    XEnterEvent* event;
    if (!window) return;
    event = XEnterEvent_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                  XEVENT_TYPE_ENTER, &position,
                                  globalPosition);
    if (!event) return;
    XGuiApplication_sendSpontaneousEvent((XObject*)window, (XEvent*)event);
    XEvent_delete_base((XEvent*)event);
}

void XWindowSystemInterface_handleLeaveEvent(XWindow* window)
{
    XEvent event;
    if (!window) return;
    XEvent_init(&event, XEVENT_TYPE_LEAVE);
    XGuiApplication_sendSpontaneousEvent((XObject*)window, &event);
}

void XWindowSystemInterface_flushWindowSystemEvents(XEventLoopProcessEventsFlags flags)
{
    XGuiApplication_processEvents(flags);
}

#endif /* XWINDOWSYSTEMINTERFACE_ON && XGUIAPPLICATION_ON && XWINDOW_ON && XWINDOWEVENT_ON */
