/**
 * @file       XMenu.c
 * @brief      XMenu 弹出菜单实现（对标 Qt 6.8 QMenu）。
 * @details    对齐 QMenu 的动作容器、属性、弹出与信号语义：
 *             - 动作容器：addAction/addMenu/addSeparator/clear/isEmpty、
 *               actions()/actionAt()/menuAction()；
 *             - 属性：title、defaultAction、activeAction、separators-
 *               Collapsible、toolTipsVisible、tearOffEnabled；
 *             - 弹出：popup() 以顶层窗口显示并激活，点击条目触发动作后
 *               关闭；exec() 阻塞事件循环直至关闭并返回被选动作；
 *             - 信号：aboutToShow/aboutToHide/triggered(XAction*)/
 *               hovered(XAction*)。
 *             绘制使用固定调色（无 XPalette 主题依赖），键盘支持
 *             Up/Down/Enter/Escape。本实现只使用 XinYueC 的 XWidget/
 *             XAction/XString/XPainter 抽象层，不依赖任何平台 API。
 */
#include "XMenu.h"
#include "XWidget_Protected.h"
#include "XWindow.h"
#include "XMemory.h"
#include "XVector.h"
#include "XPainter.h"
#include "XFont.h"
#include "XCoreApplication.h"
#include "XVarList.h"

#include <string.h>

#if XWIDGET_ON && XMENU_ON

/* ==================== 信号发射辅助 ==================== */

static void xmenu_emitVoid(XMenu* self, size_t signal)
{
    XVarList* args = XVarList_create(0);

    if (!args)
        return;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

static void xmenu_emitAction(XMenu* self, size_t signal, XAction* action)
{
    XVarList* args = XVarList_Create(XVar(XAction*, action));

    if (!args)
        return;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

/* ==================== 动作管理 ==================== */

/* 动作 triggered 转发槽：动作被触发时转发菜单 triggered(action) 信号。 */
static void xmenu_actionTriggeredSlot(XObject* receiver, XVarList* args)
{
    XMenu* self = (XMenu*)receiver;
    XObject* sender = XObject_sender(receiver);

    (void)args;
    if (self && sender)
        xmenu_emitAction(self, (size_t)XMenu_triggered_signal,
                         (XAction*)sender);
}

/* 动作销毁槽：动作被外部释放时从菜单列表移除，避免悬挂指针。 */
static void xmenu_actionDestroyedSlot(XObject* receiver, XVarList* args)
{
    XMenu* self = (XMenu*)receiver;
    XObject* sender = XObject_sender(receiver);
    int64_t count;
    int64_t index;

    (void)args;
    if (!self || !sender || !self->m_actions)
        return;
    count = (int64_t)XVector_size_base((const XContainer*)self->m_actions);
    for (index = 0; index < count; ++index) {
        XAction** item = (XAction**)XVector_at_base(
            (XContainer*)self->m_actions, index);
        if (item && *item == (XAction*)sender) {
            XVector_remove_base((XContainer*)self->m_actions, index, 1);
            break;
        }
    }
}

static XAction* xmenu_addActionInternal(XMenu* self, XAction* action)
{
    if (!self || !action || !self->m_actions)
        return NULL;

    XObject_connect_1((XObject*)action, XSignal(XAction_triggered_signal),
                      (XObject*)self, xmenu_actionTriggeredSlot,
                      XConnectionType_Direct);
    XObject_connect_1((XObject*)action, XSignal(XObject_destroyed_signal),
                      (XObject*)self, xmenu_actionDestroyedSlot,
                      XConnectionType_Direct);
    XVector_push_back_1_base(self->m_actions, &action);
    XWidget_updateGeometry((XWidget*)self);
    XWidget_update((XWidget*)self);
    return action;
}

/* ==================== 动作容器（对标 QMenu） ==================== */

XAction* XMenu_addAction(XMenu* self, const XString* text)
{
    XAction* action;

    if (!self)
        return NULL;
    action = XAction_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL, NULL);
    if (!action)
        return NULL;
    if (text)
        XAction_setText(action, text);
    return xmenu_addActionInternal(self, action);
}

XAction* XMenu_addAction_2(XMenu* self, const char* utf8)
{
    XAction* action;

    if (!self)
        return NULL;
    action = XAction_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL, utf8);
    if (!action)
        return NULL;
    return xmenu_addActionInternal(self, action);
}

XAction* XMenu_addSeparator(XMenu* self)
{
    XAction* action;

    if (!self)
        return NULL;
    action = XAction_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL, NULL);
    if (!action)
        return NULL;
    XAction_setSeparator(action, true);
    return xmenu_addActionInternal(self, action);
}

bool XMenu_addMenu(XMenu* self, XMenu* menu)
{
    XAction* action;
    XString* title;

    if (!self)
        return false;
    if (!menu)
        return XMenu_addSeparator(self) != NULL;
    if (menu->m_parentMenu == self)
        return true;

    /* 子菜单登记为本菜单的子控件，父菜单释放时级联释放。 */
    XWidget_setParent((XWidget*)menu, (XWidget*)self, 0);
    menu->m_parentMenu = self;

    action = XAction_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL, NULL);
    if (!action)
        return false;
    title = XMenu_title(menu);
    if (title) {
        XAction_setText(action, title);
        XString_delete_base((XClass*)title);
    }
    XAction_setMenu(action, menu);
    return xmenu_addActionInternal(self, action) != NULL;
}

XMenu* XMenu_addMenu_2(XMenu* self, const char* utf8Title)
{
    XMenu* child;

    if (!self)
        return NULL;
    child = XMenu_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (XWidget*)self,
                            utf8Title);
    if (!child)
        return NULL;
    if (!XMenu_addMenu(self, child)) {
        XMenu_delete_base(child);
        return NULL;
    }
    return child;
}

void XMenu_clear(XMenu* self)
{
    XAction* action;

    if (!self || !self->m_actions)
        return;
    while (XVector_size_base((XContainer*)self->m_actions) > 0) {
        action = *(XAction**)XVector_at_base((XContainer*)self->m_actions,
                                             0);
        if (action)
            XAction_delete_base(action);
        /* 销毁回调会把元素移出列表；未连接时下面显式清空兜底。 */
        if (XVector_size_base((XContainer*)self->m_actions) > 0)
            XVector_remove_base((XContainer*)self->m_actions, 0, 1);
    }
    self->m_defaultAction = NULL;
    self->m_activeAction = NULL;
    XWidget_updateGeometry((XWidget*)self);
    XWidget_update((XWidget*)self);
}

bool XMenu_isEmpty(const XMenu* self)
{
    if (!self || !self->m_actions)
        return true;
    return XVector_size_base((const XContainer*)self->m_actions) == 0;
}

const XVector* XMenu_actions(const XMenu* self)
{
    return self ? self->m_actions : NULL;
}

XAction* XMenu_actionAt(const XMenu* self, const XPoint* pos)
{
    XRect rect;
    int index;
    int64_t count;

    if (!self || !pos)
        return NULL;
    rect = XWidget_rect((XWidget*)self);
    if (pos->y < rect.y)
        return NULL;
    index = (pos->y - rect.y) /
            (self->m_actionHeight > 0 ? self->m_actionHeight : 1);
    count = self->m_actions
                ? (int64_t)XVector_size_base(
                      (const XContainer*)self->m_actions)
                : 0;
    if (index < 0 || index >= count)
        return NULL;
    return *(XAction**)XVector_at_base((XContainer*)self->m_actions, index);
}

XAction* XMenu_menuAction(XMenu* self)
{
    XString* title;

    if (!self)
        return NULL;
    if (!self->m_menuAction) {
        self->m_menuAction =
            XAction_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL, NULL);
        if (!self->m_menuAction)
            return NULL;
        title = XMenu_title(self);
        if (title) {
            XAction_setText(self->m_menuAction, title);
            XString_delete_base((XClass*)title);
        }
    }
    return self->m_menuAction;
}

/* ==================== 属性（对标 QMenu） ==================== */

XString* XMenu_title(const XMenu* self)
{
    if (!self || !self->m_title)
        return NULL;
    return XString_create_copy(self->m_title);
}

const XString* XMenu_title_const(const XMenu* self)
{
    return self ? self->m_title : NULL;
}

void XMenu_setTitle(XMenu* self, const XString* title)
{
    XString* copy;

    if (!self)
        return;
    if (self->m_title && title &&
        XString_equals(self->m_title, title, XChar_CaseSensitive)) {
        return;
    }
    copy = title ? XString_create_copy(title) : XString_create();
    if (!copy)
        return;
    if (self->m_title)
        XString_delete_base((XClass*)self->m_title);
    self->m_title = copy;
    if (self->m_menuAction)
        XAction_setText(self->m_menuAction, self->m_title);
    XWidget_update((XWidget*)self);
}

void XMenu_setTitle_2(XMenu* self, const char* utf8)
{
    XString* text;

    if (!self)
        return;
    text = XString_create_utf8(utf8 ? utf8 : "");
    if (!text)
        return;
    XMenu_setTitle(self, text);
    XString_delete_base((XClass*)text);
}

XAction* XMenu_defaultAction(const XMenu* self)
{
    return self ? self->m_defaultAction : NULL;
}

void XMenu_setDefaultAction(XMenu* self, XAction* action)
{
    if (self)
        self->m_defaultAction = action;
}

XAction* XMenu_activeAction(const XMenu* self)
{
    return self ? self->m_activeAction : NULL;
}

void XMenu_setActiveAction(XMenu* self, XAction* action)
{
    if (!self || self->m_activeAction == action)
        return;
    self->m_activeAction = action;
    XWidget_update((XWidget*)self);
}

bool XMenu_separatorsCollapsible(const XMenu* self)
{
    return self ? self->m_separatorsCollapsible : false;
}

void XMenu_setSeparatorsCollapsible(XMenu* self, bool collapse)
{
    if (!self || self->m_separatorsCollapsible == collapse)
        return;
    self->m_separatorsCollapsible = collapse;
    XWidget_update((XWidget*)self);
}

bool XMenu_toolTipsVisible(const XMenu* self)
{
    return self ? self->m_toolTipsVisible : false;
}

void XMenu_setToolTipsVisible(XMenu* self, bool visible)
{
    if (self)
        self->m_toolTipsVisible = visible;
}

bool XMenu_tearOffEnabled(const XMenu* self)
{
    return self ? self->m_tearOffEnabled : false;
}

void XMenu_setTearOffEnabled(XMenu* self, bool enable)
{
    if (self)
        self->m_tearOffEnabled = enable;
}

/* ==================== 弹出（对标 QMenu） ==================== */

static void xmenu_close(XMenu* self)
{
    if (!self || !self->m_popupActive)
        return;
    self->m_popupActive = false;
    if (self->m_grabTimer != XTIMER_INVALID_ID) {
        XObject_killTimer((XObject*)self, self->m_grabTimer);
        self->m_grabTimer = XTIMER_INVALID_ID;
    }
    /* 释放模态鼠标抓取：公共层直投 + 平台 XUngrabPointer。 */
    XWidget_releaseMouse((XWidget*)self);
    {
        XWindow* handle = XWidget_windowHandle((XWidget*)self);
        if (handle)
            XWindow_setMouseGrabEnabled(handle, false);
    }
    xmenu_emitVoid(self, (size_t)XMenu_aboutToHide_signal);
    XWidget_hide((XWidget*)self);
}

/* 移动高亮到下一个/上一个可选条目；wrap 循环。 */
static void xmenu_moveActive(XMenu* self, int direction)
{
    int64_t count;
    int64_t index;
    int64_t next;
    int steps;

    if (!self || !self->m_actions)
        return;
    count = (int64_t)XVector_size_base(
        (const XContainer*)self->m_actions);
    if (count <= 0)
        return;
    index = -1;
    if (self->m_activeAction) {
        for (index = 0; index < count; ++index) {
            XAction** item = (XAction**)XVector_at_base(
                (XContainer*)self->m_actions, index);
            if (item && *item == self->m_activeAction)
                break;
        }
        if (index >= count)
            index = -1;
    }
    for (steps = 0; steps < (int)count; ++steps) {
        XAction** item;

        next = index + direction;
        if (next < 0)
            next = count - 1;
        else if (next >= count)
            next = 0;
        index = next;
        item = (XAction**)XVector_at_base((XContainer*)self->m_actions,
                                          index);
        if (item && *item && XAction_isEnabled(*item) &&
            !XAction_isSeparator(*item)) {
            XMenu_setActiveAction(self, *item);
            xmenu_emitAction(self, (size_t)XMenu_hovered_signal, *item);
            return;
        }
    }
}

void XMenu_popup(XMenu* self, const XPoint* pos)
{
    XSize hint;
    XRect rect;
    XPoint p;

    if (!self || self->m_popupActive)
        return;
    hint = XMenu_sizeHint(self);
    p.x = pos ? pos->x : 0;
    p.y = pos ? pos->y : 0;
    XRect_init(&rect, p.x, p.y,
               hint.width > 0 ? hint.width : 120,
               hint.height > 0 ? hint.height : 20);
    xmenu_emitVoid(self, (size_t)XMenu_aboutToShow_signal);
    XWidget_setGeometryRect((XWidget*)self, &rect);
    XWidget_show((XWidget*)self);
    XWidget_raise((XWidget*)self);
    /* 立即建立后备存储并完成首帧绘制上屏：菜单作为独立顶层窗口没有
     * 宿主帧泵，若不主动 flush，backingStore 一直为空、paintDevice 返回
     * NULL，弹出后内容空白。 */
    XWidget_flushBackingStore((XWidget*)self, NULL);
    /* 模态鼠标抓取（对标 QMenu 弹窗）：公共层立即设置直投目标；平台层
     * XGrabPointer 需要窗口已完成映射，因此延迟到 1ms 定时器（事件循环
     * 处理时窗口已映射）再执行，避免对未映射窗口抓取失败。 */
    XWidget_grabMouse((XWidget*)self);
    if (self->m_grabTimer == XTIMER_INVALID_ID) {
        self->m_grabTimer = XObject_startTimer_ms(
            (XObject*)self, 1u, XTimerType_PreciseTimer);
    }
    /* 注意：不在 show() 后立即 activateWindow/setFocus。X11 的
     * XSetInputFocus 要求窗口已完成映射，而 show() 的映射是异步的，
     * 立即激活会对未映射窗口触发 BadMatch 崩溃；焦点交由平台在用户
     * 与菜单窗口实际交互时自然授予。 */
    self->m_popupActive = true;
}

XAction* XMenu_exec(XMenu* self)
{
    if (!self)
        return NULL;
    self->m_execResult = NULL;
    XMenu_popup(self, NULL);
    while (self->m_popupActive)
        XCoreApplication_processEvents(XEventLoop_AllEvents);
    return self->m_execResult;
}

/* ==================== 尺寸（对标 QMenu::sizeHint） ==================== */

XSize XMenu_sizeHint(const XMenu* self)
{
    XSize out;
    XFont font;
    int64_t count;
    int64_t i;
    int maxWidth;

    XSize_init(&out, 0, 0);
    if (!self)
        return out;
    count = self->m_actions
                ? (int64_t)XVector_size_base(
                      (const XContainer*)self->m_actions)
                : 0;
    font = XWidget_font((XWidget*)self);
    maxWidth = 0;
    for (i = 0; i < count; ++i) {
        XAction** item = (XAction**)XVector_at_base(
            (XContainer*)self->m_actions, i);
        if (item && *item && !XAction_isSeparator(*item)) {
            const char* text = XString_toUtf8(
                XAction_text_const(*item) ? XAction_text_const(*item)
                                          : NULL);
            int width;

            if (!text)
                text = "";
            width = XPainter_textWidth(&font, text);
            if (XAction_menu(*item))
                width += 16; /* 子菜单箭头预留。 */
            if (width > maxWidth)
                maxWidth = width;
        }
    }
    XFont_deinit_base(&font);
    out.width = maxWidth + 24;
    out.height = (int)count * (self->m_actionHeight > 0
                                   ? self->m_actionHeight
                                   : 20);
    return out;
}

/* ==================== 虚槽实现（绘制与交互） ==================== */

void XMenu_drawContents(XMenu* self, XPainter* painter)
{
    XRect rect;
    XFont font;
    int64_t count;
    int64_t i;

    if (!self || !painter)
        return;
    rect = XWidget_rect((XWidget*)self);
    XPainter_fillRect(painter, &rect, 0xFFF0F0F0u);
    font = XWidget_font((XWidget*)self);
    XPainter_setFont(painter, &font);
    count = self->m_actions
                ? (int64_t)XVector_size_base(
                      (const XContainer*)self->m_actions)
                : 0;
    for (i = 0; i < count; ++i) {
        XAction** item = (XAction**)XVector_at_base(
            (XContainer*)self->m_actions, i);
        XAction* action = item ? *item : NULL;
        XRect cell;
        int y;

        if (!action)
            continue;
        y = rect.y + (int)i * self->m_actionHeight;
        XRect_init(&cell, rect.x, y, rect.width, self->m_actionHeight);
        if (XAction_isSeparator(action)) {
            XRect line;
            XRect_init(&line, rect.x + 6, y + self->m_actionHeight / 2,
                       rect.width - 12, 1);
            XPainter_fillRect(painter, &line, 0xFFA0A0A0u);
            continue;
        }
        if (action == self->m_activeAction)
            XPainter_fillRect(painter, &cell, 0xFFB0C4DEu);
        {
            const char* text = XString_toUtf8(
                XAction_text_const(action) ? XAction_text_const(action)
                                           : NULL);
            int ascent;
            uint32_t color;

            if (!text)
                text = "";
            ascent = XPainter_textAscent(&font);
            color = XAction_isEnabled(action) ? 0xFF000000u : 0xFF808080u;
            XPainter_drawText(painter, rect.x + 6,
                              y + (self->m_actionHeight -
                                   XPainter_textHeight(&font)) /
                                      2 +
                                  ascent,
                              text, color);
        }
        if (XAction_menu(action)) {
            int arrowX = rect.x + rect.width - 14;
            int arrowY = y + (self->m_actionHeight / 2) - 2;
            XRect tri;
            XRect_init(&tri, arrowX + 2, arrowY, 1, 1);
            XPainter_fillRect(painter, &tri, 0xFF606060u);
            XRect_init(&tri, arrowX + 1, arrowY + 1, 3, 1);
            XPainter_fillRect(painter, &tri, 0xFF606060u);
            XRect_init(&tri, arrowX, arrowY + 2, 5, 1);
            XPainter_fillRect(painter, &tri, 0xFF606060u);
            XRect_init(&tri, arrowX + 1, arrowY + 3, 3, 1);
            XPainter_fillRect(painter, &tri, 0xFF606060u);
            XRect_init(&tri, arrowX + 2, arrowY + 4, 1, 1);
            XPainter_fillRect(painter, &tri, 0xFF606060u);
        }
    }
    XFont_deinit_base(&font);
}

static void VXMenu_paintEvent(XWidget* self, XEvent* event)
{
#if !XWINDOWEVENT_ON
    (void)self;
    (void)event;
    return;
#else
    XMenu* menu = (XMenu*)self;
    XImage* image;
    XPoint offset;
    XPainter painter;

    if (!menu || !event || XEvent_type(event) != XEVENT_TYPE_PAINT)
        return;
    image = XWidget_paintDevice(self);
    if (!image)
        return;
    XPainter_init(&painter, NULL);
    if (!XPainter_begin_image(&painter, image)) {
        XPainter_deinit(&painter);
        return;
    }
    offset = XWidget_paintOffset(self);
    if (offset.x != 0 || offset.y != 0)
        XPainter_translate(&painter, (float)offset.x, (float)offset.y);
    XMenu_drawContents(menu, &painter);
    XPainter_end(&painter);
    XPainter_deinit(&painter);
#endif /* XWINDOWEVENT_ON */
}

static void VXMenu_mousePressEvent(XWidget* self, XEvent* event)
{
    XMenu* menu = (XMenu*)self;
    XMouseEvent* mouseEvent;
    XPoint pos;
    XAction* action;

    if (!menu || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_PRESS) {
        return;
    }
    mouseEvent = (XMouseEvent*)event;
    /* 菜单外点击（任意按键，含左/中/右键）一律关闭并释放抓取；只有
     * 落在菜单条目上时才保留，等待 release 触发动作。 */
    pos = XMouseEvent_position(mouseEvent);
    action = XMenu_actionAt(menu, &pos);
    if (!action) {
        menu->m_execResult = NULL;
        xmenu_close(menu);
        XEvent_accept(event);
        return;
    }
    XEvent_accept(event);
}

static void VXMenu_mouseReleaseEvent(XWidget* self, XEvent* event)
{
    XMenu* menu = (XMenu*)self;
    XMouseEvent* mouseEvent;
    XPoint pos;
    XAction* action;

    if (!menu || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_RELEASE) {
        return;
    }
    mouseEvent = (XMouseEvent*)event;
    if (XMouseEvent_button(mouseEvent) != XMouseButton_LeftButton) {
        XEvent_ignore(event);
        return;
    }
    pos = XMouseEvent_position(mouseEvent);
    action = XMenu_actionAt(menu, &pos);
    if (!action || XAction_isSeparator(action) || !XAction_isEnabled(action)) {
        XEvent_ignore(event);
        return;
    }
    menu->m_execResult = action;
    xmenu_close(menu);
    XAction_trigger(action);
    XEvent_accept(event);
}

static void VXMenu_mouseMoveEvent(XWidget* self, XEvent* event)
{
    XMenu* menu = (XMenu*)self;
    XMouseEvent* mouseEvent;
    XPoint pos;
    XAction* action;

    if (!menu || !event || XEvent_type(event) != XEVENT_TYPE_MOUSE_MOVE)
        return;
    mouseEvent = (XMouseEvent*)event;
    pos = XMouseEvent_position(mouseEvent);
    action = XMenu_actionAt(menu, &pos);
    if (action && action != menu->m_activeAction &&
        !XAction_isSeparator(action)) {
        XMenu_setActiveAction(menu, action);
        xmenu_emitAction(menu, (size_t)XMenu_hovered_signal, action);
    }
}

static void VXMenu_keyPressEvent(XWidget* self, XEvent* event)
{
    XMenu* menu = (XMenu*)self;
    int key;

    if (!menu || !event || XEvent_type(event) != XEVENT_TYPE_KEY_PRESS)
        return;
    key = XKeyEvent_key((XKeyEvent*)event);
    if (key == XKey_Up) {
        xmenu_moveActive(menu, -1);
        XEvent_accept(event);
    } else if (key == XKey_Down) {
        xmenu_moveActive(menu, 1);
        XEvent_accept(event);
    } else if (key == XKey_Return || key == XKey_Enter) {
        XAction* action = menu->m_activeAction;

        if (action && XAction_isEnabled(action) &&
            !XAction_isSeparator(action)) {
            menu->m_execResult = action;
            xmenu_close(menu);
            XAction_trigger(action);
        }
        XEvent_accept(event);
    } else if (key == XKey_Escape) {
        menu->m_execResult = NULL;
        xmenu_close(menu);
        XEvent_accept(event);
    } else {
        XEvent_ignore(event);
    }
}

static void VXMenu_leaveEvent(XWidget* self, XEvent* event)
{
    XMenu* menu = (XMenu*)self;

    if (menu && event) {
        XMenu_setActiveAction(menu, NULL);
        XEvent_accept(event);
    }
}

/* 外部 hide（平台关闭/失焦收起）时结束弹出状态，避免 exec 卡死。 */
static void VXMenu_hideEvent(XWidget* self, XEvent* event)
{
    XMenu* menu = (XMenu*)self;

    if (menu && event) {
        if (menu->m_popupActive)
            xmenu_close(menu);
        XEvent_accept(event);
    }
}

/* 弹出后的延迟抓取定时器：窗口映射完成后执行平台 XGrabPointer，
 * 使点击菜单外部的按键都送达本窗口（模态关闭）。 */
static void VXMenu_timerEvent(XObject* object, XTimerEvent* event)
{
    XMenu* self = (XMenu*)object;
    XTimerId id;
    XWindow* handle;

    if (self && event &&
        XTimerEvent_timerId(event) == self->m_grabTimer) {
        id = self->m_grabTimer;
        self->m_grabTimer = XTIMER_INVALID_ID;
        XObject_killTimer((XObject*)self, id);
        handle = XWidget_windowHandle((XWidget*)self);
        if (handle)
            XWindow_setMouseGrabEnabled(handle, true);
        XEvent_accept((XEvent*)event);
        return;
    }
    XClass_Parent(XObject, EXObject_TimerEvent,
                  void(*)(XObject*, XTimerEvent*))(object, event);
}

/* ==================== 生命周期 ==================== */

static void VXMenu_copy(XMenu* self, const XMenu* other)
{
    int64_t count;
    int64_t i;

    if (!self || !other || self == other)
        return;
    if (XClassIsVtableNull(self))
        XMenu_init(self, NULL);

    XMenu_clear(self);
    if (self->m_title) {
        XString_delete_base((XClass*)self->m_title);
        self->m_title = NULL;
    }
    self->m_title = other->m_title ? XString_create_copy(other->m_title)
                                   : NULL;
    self->m_separatorsCollapsible = other->m_separatorsCollapsible;
    self->m_toolTipsVisible = other->m_toolTipsVisible;
    self->m_tearOffEnabled = other->m_tearOffEnabled;
    self->m_actionHeight = other->m_actionHeight;
    count = other->m_actions
                ? (int64_t)XVector_size_base(
                      (const XContainer*)other->m_actions)
                : 0;
    for (i = 0; i < count; ++i) {
        XAction** item = (XAction**)XVector_at_base(
            (XContainer*)other->m_actions, i);
        XAction* src = item ? *item : NULL;
        XAction* dst;

        if (!src)
            continue;
        if (XAction_isSeparator(src)) {
            dst = XMenu_addSeparator(self);
        } else if (XAction_menu(src)) {
            XMenu* sub = XAction_menu(src);
            XMenu* subCopy;

            subCopy = XMenu_create_copy(sub);
            if (subCopy)
                XMenu_addMenu(self, subCopy);
            continue;
        } else {
            dst = XMenu_addAction(self, XAction_text_const(src));
        }
        (void)dst;
    }
    self->m_defaultAction = NULL;
    self->m_activeAction = NULL;
}

static void VXMenu_move(XMenu* self, XMenu* other)
{
    if (!self || !other || self == other)
        return;
    if (XClassIsVtableNull(self))
        XMenu_init(self, NULL);

    XMenu_clear(self);
    if (self->m_title) {
        XString_delete_base((XClass*)self->m_title);
        self->m_title = NULL;
    }
    self->m_title = other->m_title;
    other->m_title = NULL;
    self->m_actions = other->m_actions;
    other->m_actions = XVector_create(sizeof(XAction*));
    self->m_menuAction = other->m_menuAction;
    other->m_menuAction = NULL;
    self->m_parentMenu = other->m_parentMenu;
    other->m_parentMenu = NULL;
    self->m_defaultAction = other->m_defaultAction;
    other->m_defaultAction = NULL;
    self->m_activeAction = other->m_activeAction;
    other->m_activeAction = NULL;
    self->m_popupActive = false;
    other->m_popupActive = false;
    self->m_separatorsCollapsible = other->m_separatorsCollapsible;
    other->m_separatorsCollapsible = true;
    self->m_toolTipsVisible = other->m_toolTipsVisible;
    other->m_toolTipsVisible = false;
    self->m_tearOffEnabled = other->m_tearOffEnabled;
    other->m_tearOffEnabled = false;
    self->m_actionHeight = other->m_actionHeight;
    other->m_actionHeight = 20;
}

static void VXMenu_deinit(XMenu* self)
{
    if (!self)
        return;

    self->m_defaultAction = NULL;
    self->m_activeAction = NULL;
    self->m_execResult = NULL;
    if (self->m_actions) {
        while (XVector_size_base((XContainer*)self->m_actions) > 0) {
            XAction** item = (XAction**)XVector_at_base(
                (XContainer*)self->m_actions, 0);
            if (item && *item)
                XAction_delete_base(*item);
            if (XVector_size_base((XContainer*)self->m_actions) > 0)
                XVector_remove_base((XContainer*)self->m_actions, 0, 1);
        }
        XVector_delete_base(self->m_actions);
        self->m_actions = NULL;
    }
    if (self->m_menuAction) {
        XAction_delete_base(self->m_menuAction);
        self->m_menuAction = NULL;
    }
    if (self->m_title) {
        XString_delete_base((XClass*)self->m_title);
        self->m_title = NULL;
    }
    XClass_Deinit_Parent(XWidget, (XWidget*)self);
}

XVtable* XMenu_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XMenu)
    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VXMenu_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MousePressEvent,
                             VXMenu_mousePressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseReleaseEvent,
                             VXMenu_mouseReleaseEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseMoveEvent, VXMenu_mouseMoveEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_KeyPressEvent, VXMenu_keyPressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_LeaveEvent, VXMenu_leaveEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_HideEvent, VXMenu_hideEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_TimerEvent, VXMenu_timerEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXMenu_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXMenu_move);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXMenu_deinit);
    return XVTABLE_DEFAULT;
}

void XMenu_init(XMenu* self, XWidget* parent)
{
    XMenu_init_2(self, parent, NULL);
}

void XMenu_init_2(XMenu* self, XWidget* parent, const char* utf8Title)
{
    if (!self)
        return;
    memset(self, 0, sizeof(XMenu));
    /* 菜单使用 Popup 窗口类型（对标 Qt::Popup）：无边框、无标题栏、
     * 无关闭按钮，弹出时覆盖式显示；X11 平台据此设置 override-redirect。 */
    XWidget_init(&self->m_base, parent, (XWidgetFlags)XWindowType_Popup);
    XClassSetVtable(self, XMenu);
    self->m_actions = XVector_create(sizeof(XAction*));
    self->m_actionHeight = 20;
    self->m_separatorsCollapsible = true;
    self->m_grabTimer = XTIMER_INVALID_ID;
    if (utf8Title && utf8Title[0])
        XMenu_setTitle_2(self, utf8Title);
}

XMenu* XMenu_create(void)
{
    return XMenu_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL, NULL);
}

XMenu* XMenu_create_ex(XMemoryType memory, XWidget* parent,
                       const char* utf8Title)
{
    XMenu* self = (XMenu*)XMemory_malloc(sizeof(XMenu), memory);

    if (!self)
        return NULL;
    memset(self, 0, sizeof(XMenu));
    XMenu_init_2(self, parent, utf8Title);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

XMenu* XMenu_create_copy(const XMenu* other)
{
    XMenu* self;

    if (!other)
        return NULL;
    self = XMenu_create();
    if (!self)
        return NULL;
    XCopy(self, other);
    return self;
}

XMenu* XMenu_create_move(XMenu* other)
{
    XMenu* self;

    if (!other)
        return NULL;
    self = XMenu_create();
    if (!self)
        return NULL;
    XMove(self, other);
    return self;
}

/* ==================== 公开信号 ==================== */

void* XMenu_aboutToShow_signal(XMenu* self)
{
    if (!self)
        return (void*)(size_t)XMenu_aboutToShow_signal;
    xmenu_emitVoid(self, (size_t)XMenu_aboutToShow_signal);
    return (void*)(size_t)XMenu_aboutToShow_signal;
}

void* XMenu_aboutToHide_signal(XMenu* self)
{
    if (!self)
        return (void*)(size_t)XMenu_aboutToHide_signal;
    xmenu_emitVoid(self, (size_t)XMenu_aboutToHide_signal);
    return (void*)(size_t)XMenu_aboutToHide_signal;
}

void* XMenu_triggered_signal(XMenu* self, XAction* action)
{
    if (!self)
        return (void*)(size_t)XMenu_triggered_signal;
    xmenu_emitAction(self, (size_t)XMenu_triggered_signal, action);
    return (void*)(size_t)XMenu_triggered_signal;
}

void* XMenu_hovered_signal(XMenu* self, XAction* action)
{
    if (!self)
        return (void*)(size_t)XMenu_hovered_signal;
    xmenu_emitAction(self, (size_t)XMenu_hovered_signal, action);
    return (void*)(size_t)XMenu_hovered_signal;
}

#endif /* XWIDGET_ON && XMENU_ON */
