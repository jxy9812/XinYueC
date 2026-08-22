/******************************************************************************
 * @file       XWindowSystemInterface.c
 * @brief      窗口系统事件注入接口实现（对标 Qt 6.8 QWindowSystemInterface）。
 * @details    平台后端调用 handle* 注入函数时，构造携带负载的具体事件
 *             （XResizeEvent / XExposeEvent / XPaintEvent / XFocusEvent /
 *             XCloseEvent / XShowEvent / XHideEvent / XKeyEvent /
 *             XMouseEvent / XWheelEvent / XEnterEvent），并经
 *             XGuiApplication_sendSpontaneousEvent 以自发事件语义同步
 *             投递，XWindow_event_base 按事件类型路由到对应窗口事件槽。
 *             事件投递后立即释放，符合 XEvent 的事件所有权约定。
 *             本文件不引用任何平台 API，嵌入式可用。
 * @note       模块总开关 XWINDOWSYSTEMINTERFACE_ON 定义于 XGuiConfig.h；
 *             依赖 XGUIAPPLICATION_ON / XWINDOW_ON / XWINDOWEVENT_ON。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XWindowSystemInterface.h"
#include "XMemory.h"
#include "XPrintf.h"
#include <string.h>

#if XWINDOWSYSTEMINTERFACE_ON && XGUIAPPLICATION_ON && XWINDOW_ON && XWINDOWEVENT_ON

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

void XWindowSystemInterface_handleFocusWindowChanged(XWindow* window, XFocusReason reason)
{
    XFocusEvent* event;
    if (!window) return;
    event = XFocusEvent_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                  XEVENT_TYPE_FOCUS_IN, reason);
    if (!event) return;
    XGuiApplication_sendSpontaneousEvent((XObject*)window, (XEvent*)event);
    XEvent_delete_base((XEvent*)event);
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
    XKeyEvent* event;
    if (!window) return false;
    event = XKeyEvent_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, type, key, modifiers);
    if (!event) return false;
    XKeyEvent_setAutoRepeat(event, autoRepeat);
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
    XMouseEvent* event;
    if (!window) return false;
    event = XMouseEvent_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, type, button,
                                  modifiers, position);
    if (!event) return false;
    XMouseEvent_setButtons(event, buttons);
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
