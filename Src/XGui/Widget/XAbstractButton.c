/**
 * @file       XAbstractButton.c
 * @brief      XAbstractButton 抽象按钮基类实现。
 * @details    对齐 Qt 6.8 QAbstractButton 的公共状态、激活、自动重复、
 *             自动互斥和输入事件语义；具体外观、尺寸提示和专有属性由
 *             派生控件实现。本实现只使用 XinYueC 的 XWidget、XObject
 *             和 XClass 抽象层，不调用 Win32、POSIX、Qt 或其他平台 API。
 */
#include "XAbstractButton.h"
#include "XAbstractButton_Protected.h"
#include "XWidget_Protected.h"
#include "XMemory.h"
#include "XVarList.h"
#include "XVector.h"

#include <string.h>

#if XWIDGET_ON && XABSTRACTBUTTON_ON

/* ==================== 派生类登记 ==================== */

/*
 * QAbstractButton 的派生类没有 RTTI；登记静态虚表使自动互斥组可以从
 * XObject 子列表中识别所有按钮派生对象。该表随进程存在，只在 GUI
 * 线程的 class_init 路径写入。
 */
static XVector* g_abstractButtonClasses;

static bool abstractbutton_vtableRegistered(const XVtable* vtable)
{
    int64_t count;
    int64_t index;

    if (!vtable)
        return false;
    if (vtable == XAbstractButton_class_init())
        return true;
    if (!g_abstractButtonClasses)
        return false;

    count = (int64_t)XVector_size_base(
        (const XContainer*)g_abstractButtonClasses);
    for (index = 0; index < count; ++index) {
        XVtable** item = (XVtable**)XVector_at_base(
            g_abstractButtonClasses, index);
        if (item && *item == vtable)
            return true;
    }
    return false;
}

void XAbstractButton_registerClass(XVtable* vtable)
{
    int64_t count;
    int64_t index;

    if (!vtable || vtable == XAbstractButton_class_init() ||
        XVtable_size(vtable) < XCLASS_VTABLE_GET_SIZE(XAbstractButton))
        return;

    if (!g_abstractButtonClasses) {
        g_abstractButtonClasses = XVector_Create(XVtable*);
        if (!g_abstractButtonClasses)
            return;
    }

    count = (int64_t)XVector_size_base(
        (const XContainer*)g_abstractButtonClasses);
    for (index = 0; index < count; ++index) {
        XVtable** item = (XVtable**)XVector_at_base(
            g_abstractButtonClasses, index);
        if (item && *item == vtable)
            return;
    }
    XVector_push_back_1_base(g_abstractButtonClasses, &vtable);
}

bool XAbstractButton_isInstance(const XObject* object)
{
    if (!object || !XClassGetVtable(object))
        return false;
    return abstractbutton_vtableRegistered(XClassGetVtable(object));
}

/* ==================== 内部状态与信号工具 ==================== */

static void abstractbutton_stopRepeatTimer(XAbstractButton* self);
static void abstractbutton_startRepeatTimer(XAbstractButton* self,
                                            int interval);
static void abstractbutton_stopAnimateTimer(XAbstractButton* self);
static void abstractbutton_startAnimateTimer(XAbstractButton* self);

static void abstractbutton_emitVoid(XAbstractButton* self, size_t signal)
{
    XVarList* arguments = XVarList_create(0);

    if (!arguments)
        return;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, arguments, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(arguments);
    }
}

static void abstractbutton_emitBool(XAbstractButton* self, size_t signal,
                                    bool value)
{
    XVarList* arguments = XVarList_Create(XVar(bool, value));

    if (!arguments)
        return;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, arguments, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(arguments);
    }
}

static void abstractbutton_stopRepeatTimer(XAbstractButton* self)
{
    if (!self || self->m_repeatTimer == XTIMER_INVALID_ID)
        return;
    XObject_killTimer((XObject*)self, self->m_repeatTimer);
    self->m_repeatTimer = XTIMER_INVALID_ID;
}

static void abstractbutton_startRepeatTimer(XAbstractButton* self,
                                            int interval)
{
    uint64_t intervalMs;

    if (!self || !self->m_autoRepeat || !self->m_down)
        return;
    abstractbutton_stopRepeatTimer(self);
    /*
     * 当前事件调度器拒绝 0ms 定时器；属性本身仍保留调用者传入值，
     * 仅在实际注册时收敛到 1ms。
     */
    intervalMs = interval > 0 ? (uint64_t)interval : 1u;
    self->m_repeatTimer = XObject_startTimer_ms(
        (XObject*)self, intervalMs, XTimerType_PreciseTimer);
}

static void abstractbutton_stopAnimateTimer(XAbstractButton* self)
{
    if (!self || self->m_animateTimer == XTIMER_INVALID_ID)
        return;
    XObject_killTimer((XObject*)self, self->m_animateTimer);
    self->m_animateTimer = XTIMER_INVALID_ID;
}

static void abstractbutton_startAnimateTimer(XAbstractButton* self)
{
    if (!self)
        return;
    abstractbutton_stopAnimateTimer(self);
    self->m_animateTimer = XObject_startTimer_ms(
        (XObject*)self, 100u, XTimerType_PreciseTimer);
}

/*
 * 输入事件使用本函数同步 down、自动重复和信号；公共 setDown() 不能
 * 发射 pressed/released，因此单独保留该内部路径。
 */
static void abstractbutton_setDownFromInput(XAbstractButton* self, bool down)
{
    if (!self || self->m_down == down)
        return;

    self->m_down = down;
    XWidget_repaint((XWidget*)self);
    if (down && self->m_autoRepeat)
        abstractbutton_startRepeatTimer(self, self->m_autoRepeatDelay);
    else if (!down)
        abstractbutton_stopRepeatTimer(self);

    if (down)
        XAbstractButton_pressed_signal(self);
    else
        XAbstractButton_released_signal(self);
}

static void abstractbutton_refresh(XAbstractButton* self)
{
    if (!self)
        return;
    /*
     * 内容（文本/图标/图标尺寸）真正变化后，先让派生按钮通过虚表刷新
     * 自己的内容相关存储（例如 sizeHint 存储位），再通知布局与重绘。
     */
    XAbstractButton_contentChanged_base(self);
    XWidget_updateGeometry((XWidget*)self);
    XWidget_update((XWidget*)self);
}

/* ==================== 自动互斥 ==================== */

static void abstractbutton_uncheckAutoExclusiveSiblings(
    XAbstractButton* self)
{
    XWidget* parent;
    const XVector* children;
    int64_t count;
    int64_t index;

    if (!self || !self->m_autoExclusive || !self->m_checkable)
        return;
    parent = XWidget_parentWidget((const XWidget*)self);
    if (!parent)
        return;
    children = XObject_children((const XObject*)parent);
    if (!children)
        return;

    count = (int64_t)XVector_size_base((const XContainer*)children);
    for (index = 0; index < count; ++index) {
        XObject** entry = (XObject**)XVector_at_base(children, index);
        XObject* object = entry ? *entry : NULL;
        XAbstractButton* sibling;

        if (!object || object == (XObject*)self ||
            !XObject_isWidgetType(object) ||
            !XAbstractButton_isInstance(object))
            continue;
        sibling = (XAbstractButton*)object;
        if (sibling->m_autoExclusive && sibling->m_checkable &&
            sibling->m_checked) {
            XAbstractButton_setChecked(sibling, false);
        }
    }
}

static bool abstractbutton_isOnlyAutoExclusiveMember(
    const XAbstractButton* self)
{
    XWidget* parent;
    const XVector* children;
    bool hasSibling = false;
    int64_t count;
    int64_t index;

    if (!self || !self->m_autoExclusive)
        return false;
    parent = XWidget_parentWidget((const XWidget*)self);
    /* Qt 中没有父控件时不形成 autoExclusive 组。 */
    if (!parent)
        return false;
    children = XObject_children((const XObject*)parent);
    if (!children)
        return false;

    count = (int64_t)XVector_size_base((const XContainer*)children);
    for (index = 0; index < count; ++index) {
        XObject** entry = (XObject**)XVector_at_base(children, index);
        XObject* object = entry ? *entry : NULL;
        XAbstractButton* sibling;

        if (!object || object == (const XObject*)self ||
            !XObject_isWidgetType(object) ||
            !XAbstractButton_isInstance(object))
            continue;
        sibling = (XAbstractButton*)object;
        if (!sibling->m_autoExclusive || !sibling->m_checkable)
            continue;
        hasSibling = true;
        /*
         * 其它同组项已经选中时，self 不是组中唯一选中项，允许取消；
         * 否则保留最后一个已选中项。
         */
        if (sibling->m_checked)
            return false;
    }
    return hasSibling;
}

/* ==================== 激活流程 ==================== */

static void abstractbutton_repeatTimeout(XAbstractButton* self)
{
    if (!self || !self->m_down || !XWidget_isEnabled((XWidget*)self))
        return;

    if (self->m_checkable)
        XAbstractButton_nextCheckState_base(self);
    XAbstractButton_released_signal(self);
    XAbstractButton_clicked_signal(self, self->m_checked);
    if (self->m_down)
        XAbstractButton_pressed_signal(self);
}

static void abstractbutton_clickInternal(XAbstractButton* self,
                                         bool emitPressed)
{
    if (!self || !XWidget_isEnabled((XWidget*)self))
        return;

    if (emitPressed) {
        /*
         * QAbstractButton::click() 即使调用前处于 down 状态也重放一对
         * pressed/released；这里不经 setDown()，避免程序化 click 注册
         * 自动重复定时器。
         */
        self->m_down = true;
        XWidget_repaint((XWidget*)self);
        XAbstractButton_pressed_signal(self);
    }

    abstractbutton_stopRepeatTimer(self);
    self->m_down = false;
    self->m_pressed = false;
    XWidget_repaint((XWidget*)self);
    if (self->m_checkable)
        XAbstractButton_nextCheckState_base(self);
    XAbstractButton_released_signal(self);
    XAbstractButton_clicked_signal(self, self->m_checked);
}

/* ==================== 公开信号 ==================== */

void* XAbstractButton_pressed_signal(XAbstractButton* self)
{
    if (!self)
        return (void*)(size_t)XAbstractButton_pressed_signal;
    abstractbutton_emitVoid(self, (size_t)XAbstractButton_pressed_signal);
    return (void*)(size_t)XAbstractButton_pressed_signal;
}

void* XAbstractButton_released_signal(XAbstractButton* self)
{
    if (!self)
        return (void*)(size_t)XAbstractButton_released_signal;
    abstractbutton_emitVoid(self, (size_t)XAbstractButton_released_signal);
    return (void*)(size_t)XAbstractButton_released_signal;
}

void* XAbstractButton_clicked_signal(XAbstractButton* self, bool checked)
{
    if (!self)
        return (void*)(size_t)XAbstractButton_clicked_signal;
    abstractbutton_emitBool(self, (size_t)XAbstractButton_clicked_signal,
                            checked);
    return (void*)(size_t)XAbstractButton_clicked_signal;
}

void* XAbstractButton_toggled_signal(XAbstractButton* self, bool checked)
{
    if (!self)
        return (void*)(size_t)XAbstractButton_toggled_signal;
    abstractbutton_emitBool(self, (size_t)XAbstractButton_toggled_signal,
                            checked);
    return (void*)(size_t)XAbstractButton_toggled_signal;
}

/* ==================== 公开属性 ==================== */

const XString* XAbstractButton_text(const XAbstractButton* self)
{
    return self ? self->m_text : NULL;
}

void XAbstractButton_setText(XAbstractButton* self, const XString* text)
{
    XString* copy;

    if (!self)
        return;
    if (self->m_text && text &&
        XString_equals(self->m_text, text, XChar_CaseSensitive)) {
        return;
    }

    copy = text ? XString_create_copy(text) : XString_create();
    if (!copy)
        return;
    if (self->m_text)
        XString_delete_base((XClass*)self->m_text);
    self->m_text = copy;
    abstractbutton_refresh(self);
}

void XAbstractButton_setText_2(XAbstractButton* self, const char* utf8)
{
    XString* text;

    if (!self)
        return;
    text = XString_create_utf8(utf8 ? utf8 : "");
    if (!text)
        return;
    XAbstractButton_setText(self, text);
    XString_delete_base((XClass*)text);
}

XIcon XAbstractButton_icon(const XAbstractButton* self)
{
    XIcon icon;

    XIcon_init(&icon);
    if (self)
        XCopy(&icon, &self->m_icon);
    return icon;
}

void XAbstractButton_setIcon(XAbstractButton* self, const XIcon* icon)
{
    if (!self || icon == &self->m_icon)
        return;

    XIcon_deinit_base(&self->m_icon);
    if (icon && !XIcon_isNull(icon))
        XCopy(&self->m_icon, icon);
    abstractbutton_refresh(self);
}

XSize XAbstractButton_iconSize(const XAbstractButton* self)
{
    XSize size;

    XSize_init(&size, 0, 0);
    if (self)
        size = self->m_iconSize;
    return size;
}

void XAbstractButton_setIconSize(XAbstractButton* self, const XSize* size)
{
    XSize value;

    if (!self)
        return;
    XSize_init(&value, 0, 0);
    if (size && size->width > 0 && size->height > 0)
        value = *size;
    if (self->m_iconSize.width == value.width &&
        self->m_iconSize.height == value.height) {
        return;
    }
    self->m_iconSize = value;
    abstractbutton_refresh(self);
}

bool XAbstractButton_isCheckable(const XAbstractButton* self)
{
    return self ? self->m_checkable : false;
}

void XAbstractButton_setCheckable(XAbstractButton* self, bool checkable)
{
    if (!self || self->m_checkable == checkable)
        return;

    self->m_checkable = checkable;
    /* 与现有 QPushButton 语义一致：关闭时静默清空 checked。 */
    if (!checkable && self->m_checked) {
        self->m_checked = false;
        XAbstractButton_checkStateSet_base(self);
    }
    XWidget_update((XWidget*)self);
}

bool XAbstractButton_isChecked(const XAbstractButton* self)
{
    return self ? self->m_checked : false;
}

void XAbstractButton_setChecked(XAbstractButton* self, bool checked)
{
    bool old;

    if (!self || !self->m_checkable)
        return;
    old = self->m_checked;
    if (old == checked)
        return;
    if (!checked && old && self->m_autoExclusive &&
        abstractbutton_isOnlyAutoExclusiveMember(self)) {
        return;
    }

    self->m_checked = checked;
    XAbstractButton_checkStateSet_base(self);
    XWidget_update((XWidget*)self);
    if (checked)
        abstractbutton_uncheckAutoExclusiveSiblings(self);
    XAbstractButton_toggled_signal(self, checked);
}

void XAbstractButton_toggle(XAbstractButton* self)
{
    if (!self || !self->m_checkable)
        return;
    XAbstractButton_nextCheckState_base(self);
}

bool XAbstractButton_isDown(const XAbstractButton* self)
{
    return self ? self->m_down : false;
}

void XAbstractButton_setDown(XAbstractButton* self, bool down)
{
    if (!self || self->m_down == down)
        return;

    self->m_down = down;
    XWidget_repaint((XWidget*)self);
    if (down && self->m_autoRepeat)
        abstractbutton_startRepeatTimer(self, self->m_autoRepeatDelay);
    else if (!down)
        abstractbutton_stopRepeatTimer(self);
}

bool XAbstractButton_autoRepeat(const XAbstractButton* self)
{
    return self ? self->m_autoRepeat : false;
}

void XAbstractButton_setAutoRepeat(XAbstractButton* self, bool repeat)
{
    if (!self || self->m_autoRepeat == repeat)
        return;

    self->m_autoRepeat = repeat;
    if (repeat && self->m_down)
        abstractbutton_startRepeatTimer(self, self->m_autoRepeatDelay);
    else if (!repeat)
        abstractbutton_stopRepeatTimer(self);
}

int XAbstractButton_autoRepeatDelay(const XAbstractButton* self)
{
    return self ? self->m_autoRepeatDelay : 0;
}

void XAbstractButton_setAutoRepeatDelay(XAbstractButton* self, int delay)
{
    if (self)
        self->m_autoRepeatDelay = delay;
}

int XAbstractButton_autoRepeatInterval(const XAbstractButton* self)
{
    return self ? self->m_autoRepeatInterval : 0;
}

void XAbstractButton_setAutoRepeatInterval(XAbstractButton* self,
                                           int interval)
{
    if (self)
        self->m_autoRepeatInterval = interval;
}

bool XAbstractButton_autoExclusive(const XAbstractButton* self)
{
    return self ? self->m_autoExclusive : false;
}

void XAbstractButton_setAutoExclusive(XAbstractButton* self, bool exclusive)
{
    if (self)
        self->m_autoExclusive = exclusive;
}

void XAbstractButton_click(XAbstractButton* self)
{
    abstractbutton_clickInternal(self, true);
}

void XAbstractButton_animateClick(XAbstractButton* self)
{
    if (!self || !XWidget_isEnabled((XWidget*)self))
        return;

    XAbstractButton_setDown(self, true);
    if (self->m_animateTimer == XTIMER_INVALID_ID)
        XAbstractButton_pressed_signal(self);
    abstractbutton_startAnimateTimer(self);
}

/* ==================== 保护虚函数 ==================== */

void XAbstractButton_checkStateSet(XAbstractButton* self)
{
    (void)self;
}

void XAbstractButton_checkStateSet_base(XAbstractButton* self)
{
    XAbstractButtonCheckStateSetSlot slot;

    if (!self || !XClassGetVtable(self) ||
        XVtable_size(XClassGetVtable(self)) <=
            EXAbstractButton_CheckStateSet) {
        return;
    }
    slot = XClassGetVirtualFunc(self, EXAbstractButton_CheckStateSet,
                                XAbstractButtonCheckStateSetSlot);
    if (slot)
        slot(self);
}

void XAbstractButton_nextCheckState(XAbstractButton* self)
{
    if (!self || !self->m_checkable)
        return;
    XAbstractButton_setChecked(self, !self->m_checked);
}

void XAbstractButton_nextCheckState_base(XAbstractButton* self)
{
    XAbstractButtonNextCheckStateSlot slot;

    if (!self || !XClassGetVtable(self) ||
        XVtable_size(XClassGetVtable(self)) <=
            EXAbstractButton_NextCheckState) {
        return;
    }
    slot = XClassGetVirtualFunc(self, EXAbstractButton_NextCheckState,
                                XAbstractButtonNextCheckStateSlot);
    if (slot)
        slot(self);
}

bool XAbstractButton_hitButton(const XAbstractButton* self,
                               const XPoint* pos)
{
    XRect rect;

    if (!self || !pos)
        return false;
    rect = XWidget_rect((const XWidget*)self);
    return XRect_contains(&rect, pos->x, pos->y);
}

bool XAbstractButton_hitButton_base(const XAbstractButton* self,
                                    const XPoint* pos)
{
    XAbstractButtonHitButtonSlot slot;

    if (!self || !pos || !XClassGetVtable(self) ||
        XVtable_size(XClassGetVtable(self)) <= EXAbstractButton_HitButton) {
        return false;
    }
    slot = XClassGetVirtualFunc(self, EXAbstractButton_HitButton,
                                XAbstractButtonHitButtonSlot);
    return slot ? slot(self, pos) : false;
}

void XAbstractButton_contentChanged(XAbstractButton* self)
{
    (void)self;
}

void XAbstractButton_contentChanged_base(XAbstractButton* self)
{
    XAbstractButtonContentChangedSlot slot;

    if (!self || !XClassGetVtable(self) ||
        XVtable_size(XClassGetVtable(self)) <=
            EXAbstractButton_ContentChanged) {
        return;
    }
    slot = XClassGetVirtualFunc(self, EXAbstractButton_ContentChanged,
                                XAbstractButtonContentChangedSlot);
    if (slot)
        slot(self);
}

/* ==================== XWidget 事件虚槽 ==================== */

static bool abstractbutton_ignoreDisabledEvent(XWidget* self, XEvent* event)
{
    XEventType type;

    if (!self || XWidget_isEnabled(self))
        return false;
    type = event ? XEvent_type(event) : XEVENT_TYPE_NONE;
    switch (type) {
    case XEVENT_TYPE_MOUSE_BUTTON_PRESS:
    case XEVENT_TYPE_MOUSE_BUTTON_RELEASE:
    case XEVENT_TYPE_MOUSE_BUTTON_DBL_CLICK:
    case XEVENT_TYPE_MOUSE_MOVE:
    case XEVENT_TYPE_CONTEXT_MENU:
    case XEVENT_TYPE_HOVER_ENTER:
    case XEVENT_TYPE_HOVER_LEAVE:
    case XEVENT_TYPE_HOVER_MOVE:
        if (event)
            XEvent_accept(event);
        return true;
    default:
        return false;
    }
}

static bool VXAbstractButton_event(XWidget* self, XEvent* event)
{
    if (abstractbutton_ignoreDisabledEvent(self, event))
        return true;
    return XClass_Parent(XWidget, EXObject_Event,
                         bool(*)(XWidget*, XEvent*))((XWidget*)self, event);
}

static void VXAbstractButton_changeEvent(XWidget* self, XEvent* event)
{
    XAbstractButton* button;
    XEventType type;

    if (!self || !event)
        return;
    button = (XAbstractButton*)self;
    type = XEvent_type(event);
    if (type == XEVENT_TYPE_ENABLED_CHANGE &&
        !XWidget_isEnabled(self) && button->m_down) {
        button->m_pressed = false;
        abstractbutton_setDownFromInput(button, false);
    }
    XClass_Parent(XWidget, EXWidget_ChangeEvent,
                  void(*)(XWidget*, XEvent*))((XWidget*)self, event);
}

static void VXAbstractButton_mousePressCommon(XWidget* self, XEvent* event)
{
    XAbstractButton* button = (XAbstractButton*)self;
    XMouseEvent* mouseEvent;
    XPoint position;

    if (!button || !event)
        return;
    if (XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_PRESS &&
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_DBL_CLICK) {
        return;
    }

    mouseEvent = (XMouseEvent*)event;
    if (XMouseEvent_button(mouseEvent) != XMouseButton_LeftButton) {
        XEvent_ignore(event);
        return;
    }

    position = XMouseEvent_position(mouseEvent);
    if (XAbstractButton_hitButton_base(button, &position)) {
        button->m_pressed = true;
        abstractbutton_setDownFromInput(button, true);
        XEvent_accept(event);
        return;
    }

    button->m_pressed = false;
    if (button->m_down)
        abstractbutton_setDownFromInput(button, false);
    XEvent_ignore(event);
}

static void VXAbstractButton_mousePressEvent(XWidget* self, XEvent* event)
{
    VXAbstractButton_mousePressCommon(self, event);
}

static void VXAbstractButton_mouseDoubleClickEvent(XWidget* self,
                                                    XEvent* event)
{
    VXAbstractButton_mousePressCommon(self, event);
}

static void VXAbstractButton_mouseReleaseEvent(XWidget* self, XEvent* event)
{
    XAbstractButton* button = (XAbstractButton*)self;
    XMouseEvent* mouseEvent;
    XPoint position;

    if (!button || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_RELEASE) {
        return;
    }

    mouseEvent = (XMouseEvent*)event;
    if (XMouseEvent_button(mouseEvent) != XMouseButton_LeftButton) {
        XEvent_ignore(event);
        return;
    }

    button->m_pressed = false;
    abstractbutton_stopRepeatTimer(button);
    if (!button->m_down) {
        XEvent_ignore(event);
        return;
    }

    position = XMouseEvent_position(mouseEvent);
    if (XAbstractButton_hitButton_base(button, &position)) {
        /*
         * 按下阶段已经发射 pressed；释放阶段只完成 nextCheckState、
         * released 和 clicked。
         */
        abstractbutton_clickInternal(button, false);
        XEvent_accept(event);
        return;
    }

    abstractbutton_setDownFromInput(button, false);
    XEvent_ignore(event);
}

static void VXAbstractButton_mouseMoveEvent(XWidget* self, XEvent* event)
{
    XAbstractButton* button = (XAbstractButton*)self;
    XMouseEvent* mouseEvent;
    XPoint position;
    bool hit;

    if (!button || !event || XEvent_type(event) != XEVENT_TYPE_MOUSE_MOVE)
        return;

    mouseEvent = (XMouseEvent*)event;
    if ((XMouseEvent_buttons(mouseEvent) & XMouseButton_LeftButton) == 0 ||
        !button->m_pressed) {
        XEvent_ignore(event);
        return;
    }

    position = XMouseEvent_position(mouseEvent);
    hit = XAbstractButton_hitButton_base(button, &position);
    if (hit != button->m_down) {
        abstractbutton_setDownFromInput(button, hit);
        XEvent_accept(event);
        return;
    }

    if (hit)
        XEvent_accept(event);
    else
        XEvent_ignore(event);
}

static void VXAbstractButton_keyPressEvent(XWidget* self, XEvent* event)
{
    XAbstractButton* button = (XAbstractButton*)self;
    XKeyEvent* keyEvent;
    int key;

    if (!button || !event || XEvent_type(event) != XEVENT_TYPE_KEY_PRESS)
        return;

    keyEvent = (XKeyEvent*)event;
    key = XKeyEvent_key(keyEvent);
    if (key == XKey_Space && !XKeyEvent_autoRepeat(keyEvent)) {
        button->m_pressed = true;
        abstractbutton_setDownFromInput(button, true);
        XEvent_accept(event);
        return;
    }
    if (key == XKey_Escape && button->m_down) {
        button->m_pressed = false;
        abstractbutton_setDownFromInput(button, false);
        XEvent_accept(event);
        return;
    }

    XClass_Parent(XWidget, EXWidget_KeyPressEvent,
                  void(*)(XWidget*, XEvent*))((XWidget*)self, event);
}

static void VXAbstractButton_keyReleaseEvent(XWidget* self, XEvent* event)
{
    XAbstractButton* button = (XAbstractButton*)self;
    XKeyEvent* keyEvent;

    if (!button || !event || XEvent_type(event) != XEVENT_TYPE_KEY_RELEASE)
        return;

    keyEvent = (XKeyEvent*)event;
    if (XKeyEvent_key(keyEvent) == XKey_Space &&
        !XKeyEvent_autoRepeat(keyEvent) && button->m_down) {
        button->m_pressed = false;
        abstractbutton_stopRepeatTimer(button);
        abstractbutton_clickInternal(button, false);
        XEvent_accept(event);
        return;
    }

    XEvent_ignore(event);
}

static void VXAbstractButton_focusInEvent(XWidget* self, XEvent* event)
{
    if (self && event) {
        XClass_Parent(XWidget, EXWidget_FocusInEvent,
                      void(*)(XWidget*, XEvent*))((XWidget*)self, event);
    }
}

static void VXAbstractButton_focusOutEvent(XWidget* self, XEvent* event)
{
    XAbstractButton* button = (XAbstractButton*)self;
    XFocusReason reason = XFocusReason_Other;
    bool popupFocus = false;

    if (!self || !event)
        return;
#if XWINDOWEVENT_ON
    if (XFocusEvent_lostFocus((const XFocusEvent*)event))
        reason = XFocusEvent_reason((const XFocusEvent*)event);
#endif /* XWINDOWEVENT_ON */

    popupFocus = reason == XFocusReason_Popup;
    if (button && button->m_down && !popupFocus) {
        button->m_pressed = false;
        abstractbutton_setDownFromInput(button, false);
    }
    XClass_Parent(XWidget, EXWidget_FocusOutEvent,
                  void(*)(XWidget*, XEvent*))((XWidget*)self, event);
}

/* ==================== XClass 生命周期虚槽 ==================== */

static void VXAbstractButton_copy(XAbstractButton* self,
                                  const XAbstractButton* other)
{
    if (!self || !other || self == other)
        return;

    abstractbutton_stopRepeatTimer(self);
    abstractbutton_stopAnimateTimer(self);
    if (XClassIsVtableNull(self))
        XAbstractButton_init(self, NULL, 0);

    XClass_Parent(XWidget, EXClass_Copy,
                  void(*)(XWidget*, const XWidget*))(
                      (XWidget*)self, (const XWidget*)other);
    if (self->m_text) {
        XString_delete_base((XClass*)self->m_text);
        self->m_text = NULL;
    }
    XIcon_deinit_base(&self->m_icon);
    self->m_text = other->m_text ? XString_create_copy(other->m_text)
                                 : XString_create();
    XCopy(&self->m_icon, &other->m_icon);
    self->m_iconSize = other->m_iconSize;
    self->m_checkable = other->m_checkable;
    self->m_checked = other->m_checked;
    self->m_down = other->m_down;
    self->m_pressed = other->m_pressed;
    self->m_autoRepeat = other->m_autoRepeat;
    self->m_autoExclusive = other->m_autoExclusive;
    self->m_autoRepeatDelay = other->m_autoRepeatDelay;
    self->m_autoRepeatInterval = other->m_autoRepeatInterval;
    self->m_repeatTimer = XTIMER_INVALID_ID;
    self->m_animateTimer = XTIMER_INVALID_ID;
}

static void VXAbstractButton_move(XAbstractButton* self,
                                  XAbstractButton* other)
{
    if (!self || !other || self == other)
        return;

    abstractbutton_stopRepeatTimer(self);
    abstractbutton_stopAnimateTimer(self);
    abstractbutton_stopRepeatTimer(other);
    abstractbutton_stopAnimateTimer(other);
    if (XClassIsVtableNull(self))
        XAbstractButton_init(self, NULL, 0);

    XClass_Parent(XWidget, EXClass_Move,
                  void(*)(XWidget*, XWidget*))((XWidget*)self,
                                                (XWidget*)other);
    if (self->m_text) {
        XString_delete_base((XClass*)self->m_text);
        self->m_text = NULL;
    }
    XIcon_deinit_base(&self->m_icon);
    self->m_text = other->m_text;
    other->m_text = XString_create();
    XMove(&self->m_icon, &other->m_icon);
    self->m_iconSize = other->m_iconSize;
    XSize_init(&other->m_iconSize, 0, 0);
    self->m_checkable = other->m_checkable;
    self->m_checked = other->m_checked;
    self->m_down = other->m_down;
    self->m_pressed = other->m_pressed;
    self->m_autoRepeat = other->m_autoRepeat;
    self->m_autoExclusive = other->m_autoExclusive;
    self->m_autoRepeatDelay = other->m_autoRepeatDelay;
    self->m_autoRepeatInterval = other->m_autoRepeatInterval;
    self->m_repeatTimer = XTIMER_INVALID_ID;
    self->m_animateTimer = XTIMER_INVALID_ID;

    other->m_checkable = false;
    other->m_checked = false;
    other->m_down = false;
    other->m_pressed = false;
    other->m_autoRepeat = false;
    other->m_autoExclusive = false;
    other->m_autoRepeatDelay = 300;
    other->m_autoRepeatInterval = 100;
    other->m_repeatTimer = XTIMER_INVALID_ID;
    other->m_animateTimer = XTIMER_INVALID_ID;
}

static void VXAbstractButton_deinit(XAbstractButton* self)
{
    if (!self)
        return;

    abstractbutton_stopRepeatTimer(self);
    abstractbutton_stopAnimateTimer(self);
    if (self->m_text) {
        XString_delete_base((XClass*)self->m_text);
        self->m_text = NULL;
    }
    XIcon_deinit_base(&self->m_icon);
    XClass_Deinit_Parent(XWidget, (XWidget*)self);
}

static void VXAbstractButton_timerEvent(XObject* object,
                                        XTimerEvent* event)
{
    XAbstractButton* self = (XAbstractButton*)object;

    if (self && event &&
        XTimerEvent_timerId(event) == self->m_repeatTimer) {
        XTimerId timerId = self->m_repeatTimer;

        XObject_killTimer((XObject*)self, timerId);
        self->m_repeatTimer = XTIMER_INVALID_ID;
        if (self->m_down && self->m_autoRepeat)
            abstractbutton_startRepeatTimer(self,
                                            self->m_autoRepeatInterval);
        abstractbutton_repeatTimeout(self);
        XEvent_accept((XEvent*)event);
        return;
    }

    if (self && event &&
        XTimerEvent_timerId(event) == self->m_animateTimer) {
        XTimerId timerId = self->m_animateTimer;

        XObject_killTimer((XObject*)self, timerId);
        self->m_animateTimer = XTIMER_INVALID_ID;
        abstractbutton_stopRepeatTimer(self);
        self->m_down = false;
        self->m_pressed = false;
        XWidget_repaint((XWidget*)self);
        if (self->m_checkable)
            XAbstractButton_nextCheckState_base(self);
        XAbstractButton_released_signal(self);
        XAbstractButton_clicked_signal(self, self->m_checked);
        XEvent_accept((XEvent*)event);
        return;
    }

    XClass_Parent(XObject, EXObject_TimerEvent,
                  void(*)(XObject*, XTimerEvent*))(object, event);
}

/* ==================== 类初始化与生命周期 ==================== */

XVtable* XAbstractButton_class_init(void)
{
    void* protectedSlots[] = {
        XAbstractButton_checkStateSet,
        XAbstractButton_nextCheckState,
        XAbstractButton_hitButton,
        XAbstractButton_contentChanged
    };

    XVTABLE_INIT_DEFAULT(XAbstractButton)
    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_ADD_FUNC_LIST_DEFAULT(protectedSlots);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_Event, VXAbstractButton_event);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ChangeEvent,
                             VXAbstractButton_changeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MousePressEvent,
                             VXAbstractButton_mousePressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseReleaseEvent,
                             VXAbstractButton_mouseReleaseEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseMoveEvent,
                             VXAbstractButton_mouseMoveEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseDoubleClickEvent,
                             VXAbstractButton_mouseDoubleClickEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_KeyPressEvent,
                             VXAbstractButton_keyPressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_KeyReleaseEvent,
                             VXAbstractButton_keyReleaseEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_FocusInEvent,
                             VXAbstractButton_focusInEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_FocusOutEvent,
                             VXAbstractButton_focusOutEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_TimerEvent,
                             VXAbstractButton_timerEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXAbstractButton_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXAbstractButton_move);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXAbstractButton_deinit);
    return XVTABLE_DEFAULT;
}

void XAbstractButton_init(XAbstractButton* self, XWidget* parent,
                          XWidgetFlags flags)
{
    if (!self)
        return;

    memset(self, 0, sizeof(XAbstractButton));
    XWidget_init(&self->m_base, parent, flags);
    XClassSetVtable(self, XAbstractButton);
    self->m_text = XString_create();
    XIcon_init(&self->m_icon);
    XSize_init(&self->m_iconSize, 0, 0);
    self->m_autoRepeatDelay = 300;
    self->m_autoRepeatInterval = 100;
    self->m_repeatTimer = XTIMER_INVALID_ID;
    self->m_animateTimer = XTIMER_INVALID_ID;
    XWidget_setForegroundRole((XWidget*)self,
                              XPaletteColorRole_ButtonText);
    XWidget_setBackgroundRole((XWidget*)self, XPaletteColorRole_Button);
    XWidget_setFocusPolicy((XWidget*)self, XWidgetFocusPolicy_StrongFocus);
}

XAbstractButton* XAbstractButton_create_ex(XMemoryType memory,
                                           XWidget* parent,
                                           XWidgetFlags flags)
{
    XAbstractButton* self = (XAbstractButton*)XMemory_malloc(
        sizeof(XAbstractButton), memory);

    if (!self)
        return NULL;
    memset(self, 0, sizeof(XAbstractButton));
    XAbstractButton_init(self, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

#endif /* XWIDGET_ON && XABSTRACTBUTTON_ON */
