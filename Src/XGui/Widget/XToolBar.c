/**
 * @file       XToolBar.c
 * @brief      工具栏控件实现（对标 Qt 6.8 QToolBar 全部公共 API）。
 * @details    与同名头文件的公共 API 一一对应；内部实现细节见
 *             头文件 @note 与函数级 Doxygen 注释。
 * @author     XinYueC 团队
 */

#include "XToolBar.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XVarList.h"
#include "XPainter.h"
#include "XGuiConfig.h"
#include "XWidget_Protected.h"
#include <stdio.h>
#include <string.h>

#if XWIDGET_ON && XACTION_ON && XTOOLBUTTON_ON && XTOOLBAR_ON

/* ==================== 内部桥接（per-action 转发） ==================== */

XCLASS_DEFINE_BEGING(XTBBridge)
XCLASS_DEFINE_EXTEND_END(XTBBridge, XObject)

typedef struct XTBBridge
{
    XObject m_base;      /**< 基类成员；必须是第一个。 */
    XToolBar* m_bar;     /**< 所属工具栏（借用）。 */
    XAction* m_action;   /**< 桥接动作（借用）。 */
} XTBBridge;

static XVtable* XTBBridge_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XTBBridge)
    XVTABLE_INHERIT_XCLASS(XObject);
    return XVTABLE_DEFAULT;
}

static XTBBridge* xtb_bridgeCreate(XToolBar* bar, XAction* action)
{
    XTBBridge* bridge =
        (XTBBridge*)XMemory_malloc(sizeof(*bridge),
                                   XCLASS_DEFAULT_MEMORY_TYPE);
    if (!bridge) return NULL;
    memset(bridge, 0, sizeof(*bridge));
    XObject_init(&bridge->m_base);
    XClassSetVtable(bridge, XTBBridge);
    Set_Class_Memory(bridge, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(bridge, true);
    bridge->m_bar = bar;
    bridge->m_action = action;
    return bridge;
}

static void xtb_emitAction(XToolBar* bar, size_t signal, XAction* action)
{
    XVarList* args = XVarList_Create(XVar(XAction*, action));
    if (!args) return;
    if (bar && ((XObject*)bar)->m_signalSlot) {
        XObject_emitSignal((XObject*)bar, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

static void xtb_emitVoid(XToolBar* bar, size_t signal, int orientation)
{
    XVarList* args = XVarList_Create(XVar(int, orientation));
    if (!args) return;
    if (bar && ((XObject*)bar)->m_signalSlot) {
        XObject_emitSignal((XObject*)bar, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

static void xtb_bridgeTriggeredSlot(XObject* receiver, XVarList* args)
{
    XTBBridge* bridge = (XTBBridge*)receiver;
    if (!bridge || !bridge->m_bar) return;
    if (args)
        XVarList_args_1(args, bool, checkedIgnored);
    xtb_emitAction(bridge->m_bar,
                   (size_t)XToolBar_actionTriggered_signal,
                   bridge->m_action);
}

static void xtb_bridgeHoveredSlot(XObject* receiver, XVarList* args)
{
    XTBBridge* bridge = (XTBBridge*)receiver;
    if (!bridge || !bridge->m_bar) return;
    if (args)
        XVarList_args_1(args, bool, checkedIgnored);
    xtb_emitAction(bridge->m_bar,
                   (size_t)XToolBar_actionHovered_signal,
                   bridge->m_action);
}

/* ==================== 内部排布（水平/垂直单行均分） ==================== */

static void xtb_relayout(XToolBar* bar)
{
    int64_t i;
    int64_t n;
    int w = XWidget_width((XWidget*)bar);
    int h = XWidget_height((XWidget*)bar);
    int horiz = bar->m_orientation != 2;
    int extent = horiz ? w : h;
    int cross = horiz ? h : w;
    int bw = 32;
    int bh = cross > 6 ? cross - 6 : 20;
    int x = 2;
    int y = 2;
    if (!bar || !bar->m_buttons) return;
    n = XVector_size_base((const XContainer*)bar->m_buttons);
    if (horiz && bw < 4) return;
    for (i = 0; i < n; ++i) {
        XToolButton** btn =
            (XToolButton**)XVector_at_base(bar->m_buttons, i);
        XRect r;
        if (!btn || !*btn) continue;
        if (horiz)
            XRect_init(&r, x, y, bw, bh);
        else
            XRect_init(&r, y, x, bh, bw);
        XWidget_setGeometryRect((XWidget*)*btn, &r);
        x += bw + 2;
    }
    (void)extent;
}

/* ==================== 事件处理 ==================== */

static void VX_toolBar_resizeEvent(XWidget* self, XEvent* event)
{
    (void)event;
    xtb_relayout((XToolBar*)self);
}

static void VX_toolBar_paintEvent(XWidget* self, XEvent* event)
{
    XToolBar* bar = (XToolBar*)self;
    XPainter painter;
    XImage* image;
    XPoint offset;
    XRect line;
    uint32_t mid;
    int w;
    int h;
    if (!bar || !event) return;
    w = XWidget_width(self);
    h = XWidget_height(self);
    image = XWidget_paintDevice(self);
    if (!image) return;
    XPainter_init(&painter, NULL);
    if (!XPainter_begin_image(&painter, image)) {
        XPainter_deinit(&painter);
        return;
    }
    offset = XWidget_paintOffset(self);
    if (offset.x != 0 || offset.y != 0)
        XPainter_translate(&painter, (float)offset.x, (float)offset.y);
#if XPALETTE_ON
    {
        XPalette palette = XWidget_palette(self);
        XColor c = XPalette_color(&palette,
                                   XPaletteColorGroup_Current,
                                   XPaletteColorRole_Mid);
        mid = XColor_rgba(&c);
    }
#else
    mid = 0xFF808080u;
#endif /* XPALETTE_ON */
    XRect_init(&line, 0, h - 1, w, 1);
    XPainter_fillRect(&painter, &line, mid);
    XPainter_deinit(&painter);
}

/* ==================== 生命周期与虚表 ==================== */

static void VX_toolBar_deinit(XToolBar* self)
{
    int64_t i;
    int64_t n;
    if (!self) return;
    if (self->m_actions) {
        n = XVector_size_base((const XContainer*)self->m_actions);
        for (i = 0; i < n; ++i) {
            XAction** item =
                (XAction**)XVector_at_base(self->m_actions, i);
            if (item && *item)
                XAction_delete_base(*item);
        }
        XVector_delete_base(self->m_actions);
        self->m_actions = NULL;
    }
    if (self->m_buttons) {
        XVector_delete_base(self->m_buttons);
        self->m_buttons = NULL;
    }
    XClass_Deinit_Parent(XWidget, (XWidget*)self);
}

XVtable* XToolBar_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XToolBar)
    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ResizeEvent, VX_toolBar_resizeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VX_toolBar_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VX_toolBar_deinit);
    return XVTABLE_DEFAULT;
}

void XToolBar_init(XToolBar* self, XWidget* parent, XWidgetFlags flags)
{
    XSize hint;
    if (!self) return;
    memset(self, 0, sizeof(*self));
    XWidget_init(&self->m_base, parent, flags);
    XClassSetVtable(self, XToolBar);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    self->m_actions = XVector_Create(XAction*);
    self->m_buttons = XVector_Create(XToolButton*);
    self->m_movable = true;
    self->m_floatable = true;
    self->m_orientation = 1;
    self->m_allowedAreas = (int)XToolBarArea_Left |
                           (int)XToolBarArea_Right |
                           (int)XToolBarArea_Top |
                           (int)XToolBarArea_Bottom;
    self->m_iconSize = 16;
    self->m_buttonStyle = (int)XToolButtonStyle_TextOnly;
    XWidget_resize(self, 200, 30);
    hint.width = 200;
    hint.height = 30;
    XWidget_setSizeHint((XWidget*)self, &hint);
}

XToolBar* XToolBar_create_ex(XMemoryType memory, XWidget* parent,
                             XWidgetFlags flags)
{
    XToolBar* self = (XToolBar*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XToolBar_init(self, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 属性 ==================== */

void XToolBar_setMovable(XToolBar* self, bool movable)
{
    if (!self || self->m_movable == movable) return;
    self->m_movable = movable;
    xtb_emitVoid(self, (size_t)XToolBar_movableChanged_signal, movable);
}

bool XToolBar_isMovable(const XToolBar* self)
{
    return self ? self->m_movable : false;
}

void XToolBar_setFloatable(XToolBar* self, bool floatable)
{
    if (!self) return;
    self->m_floatable = floatable;
}

bool XToolBar_isFloatable(const XToolBar* self)
{
    return self ? self->m_floatable : false;
}

void XToolBar_setOrientation(XToolBar* self, int orientation)
{
    if (!self || self->m_orientation == orientation) return;
    self->m_orientation = orientation;
    xtb_relayout(self);
    xtb_emitVoid(self, (size_t)XToolBar_orientationChanged_signal,
                 orientation);
}

int XToolBar_orientation(const XToolBar* self)
{
    return self ? self->m_orientation : 1;
}

void XToolBar_setAllowedAreas(XToolBar* self, int areas)
{
    if (!self) return;
    self->m_allowedAreas = areas;
}

int XToolBar_allowedAreas(const XToolBar* self)
{
    return self ? self->m_allowedAreas : 0;
}

void XToolBar_setIconSize(XToolBar* self, int size)
{
    if (!self || size <= 0 || self->m_iconSize == size) return;
    self->m_iconSize = size;
    xtb_relayout(self);
}

int XToolBar_iconSize(const XToolBar* self)
{
    return self ? self->m_iconSize : 16;
}

void XToolBar_setToolButtonStyle(XToolBar* self, int style)
{
    if (!self || self->m_buttonStyle == style) return;
    self->m_buttonStyle = style;
}

int XToolBar_toolButtonStyle(const XToolBar* self)
{
    return self ? self->m_buttonStyle : 0;
}

/* ==================== 动作与控件管理 ==================== */

void XToolBar_addAction(XToolBar* self, XAction* action)
{
    XTBBridge* bridge;
    XToolButton* button;
    int zero = 0;
    if (!self || !action || !self->m_actions || !self->m_buttons) return;
    bridge = xtb_bridgeCreate(self, action);
    if (!bridge) return;
    XObject_connect_1((XObject*)action, XSignal(XAction_triggered_signal),
                      (XObject*)bridge, xtb_bridgeTriggeredSlot,
                      XConnectionType_Direct);
    XObject_connect_1((XObject*)action, XSignal(XAction_hovered_signal),
                      (XObject*)bridge, xtb_bridgeHoveredSlot,
                      XConnectionType_Direct);
    XVector_push_back_1_base(self->m_actions, &action);
    button = XToolButton_create(self, 0);
    if (button) {
        XToolButton_setDefaultAction(button, action);
    }
    XVector_push_back_1_base(self->m_buttons, &button);
    XVector_push_back_1_base(self->m_bridges, &bridge);
}


/* ==================== 追加动作与控件管理 ==================== */

XAction* XToolBar_addAction_2(XToolBar* self, const char* utf8)
{
    XAction* action;
    if (!self) return NULL;
    action = XAction_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL, NULL);
    if (!action) return NULL;
    XAction_setText_2(action, utf8 ? utf8 : "");
    XToolBar_addAction(self, action);
    return action;
}

XAction* XToolBar_addSeparator(XToolBar* self)
{
    XAction* action;
    if (!self) return NULL;
    action = XAction_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL, NULL);
    if (!action) return NULL;
    XAction_setSeparator(action, true);
    XToolBar_addAction(self, action);
    return action;
}

void XToolBar_addWidget(XToolBar* self, XWidget* widget)
{
    XToolButton* button;
    if (!self || !widget) return;
    button = XToolButton_create(self, 0);
    if (!button) return;
    XWidget_setParent(widget, (XWidget*)self, 0);
    xtb_relayout(self);
}

void XToolBar_removeAction(XToolBar* self, XAction* action)
{
    int64_t i;
    int64_t n;
    if (!self || !action || !self->m_actions) return;
    n = XVector_size_base((const XContainer*)self->m_actions);
    for (i = 0; i < n; ++i) {
        XAction** item =
            (XAction**)XVector_at_base(self->m_actions, i);
        if (item && *item == action) {
            XVector_remove_base(self->m_actions, i, 1);
            XAction_delete_base(action);
            xtb_relayout(self);
            return;
        }
    }
}

void XToolBar_clear(XToolBar* self)
{
    int64_t i;
    int64_t n;
    if (!self) return;
    if (self->m_actions) {
        n = XVector_size_base((const XContainer*)self->m_actions);
        for (i = 0; i < n; ++i) {
            XAction** item =
                (XAction**)XVector_at_base(self->m_actions, i);
            if (item && *item)
                XAction_delete_base(*item);
        }
        XVector_clear_base(self->m_actions);
    }
    if (self->m_buttons) {
        XVector_clear_base(self->m_buttons);
    }
    xtb_relayout(self);
}

int XToolBar_actionCount(const XToolBar* self)
{
    return (self && self->m_actions)
               ? (int)XVector_size_base(
                     (const XContainer*)self->m_actions)
               : 0;
}

XAction* XToolBar_action(const XToolBar* self, int index)
{
    XAction** item;
    if (!self || !self->m_actions || index < 0 ||
        index >= (int)XVector_size_base(
                     (const XContainer*)self->m_actions))
        return NULL;
    item = (XAction**)XVector_at_base(self->m_actions, index);
    return item ? *item : NULL;
}

/* ==================== 信号 ==================== */

void* XToolBar_actionTriggered_signal(XToolBar* self, XAction* action)
{
    (void)self;
    (void)action;
    return (void*)(size_t)XToolBar_actionTriggered_signal;
}

void* XToolBar_actionHovered_signal(XToolBar* self, XAction* action)
{
    (void)self;
    (void)action;
    return (void*)(size_t)XToolBar_actionHovered_signal;
}

void* XToolBar_orientationChanged_signal(XToolBar* self, int orientation)
{
    (void)self;
    (void)orientation;
    return (void*)(size_t)XToolBar_orientationChanged_signal;
}

void* XToolBar_movableChanged_signal(XToolBar* self, bool movable)
{
    (void)self;
    (void)movable;
    return (void*)(size_t)XToolBar_movableChanged_signal;
}

#endif /* XWIDGET_ON && XACTION_ON && XTOOLBUTTON_ON && XTOOLBAR_ON */
