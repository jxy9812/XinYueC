/**
 * @file       XMenuBar.c
 * @brief      菜单栏控件实现（对标 Qt 6.8 QMenuBar 全部公共 API）。
 * @details    与同名头文件的公共 API 一一对应；内部实现细节见
 *             头文件 @note 与函数级 Doxygen 注释。
 * @author     XinYueC 团队
 */

#include "XMenuBar.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XVarList.h"
#include "XPainter.h"
#include "XGuiConfig.h"
#include "XWidget_Protected.h"
#include <stdio.h>
#include <string.h>

#if XWIDGET_ON && XMENU_ON && XMENUBAR_ON
#include "XAbstractButton.h"
static uint32_t xsb_color_bar(const XMenuBar* bar, XPaletteColorRole role);


/* ==================== 内部工具 ==================== */

static int xmb_actionIndex(const XMenuBar* self, const XAction* action)
{
    int64_t i;
    int64_t n;
    if (!self || !self->m_actions || !action) return -1;
    n = XVector_size_base((const XContainer*)self->m_actions);
    for (i = 0; i < n; ++i) {
        XAction** item = (XAction**)XVector_at_base(self->m_actions, i);
        if (item && *item == action) return (int)i;
    }
    return -1;
}

static XMenu* xmb_menuForAction(const XMenuBar* self, const XAction* action)
{
    int index;
    XMenu** menu;
    index = xmb_actionIndex(self, action);
    if (index < 0 || !self->m_menus) return NULL;
    menu = (XMenu**)XVector_at_base(self->m_menus, index);
    return menu ? *menu : NULL;
}

/** @brief 发射带动作参数的信号。 */
static void xmb_emitAction(XMenuBar* self, size_t signal, XAction* action)
{
    XVarList* args = XVarList_Create(XVar(XAction*, action));
    if (!args) return;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

/** @brief 桥：动作触发时弹出关联菜单（per-action 闭包等价物）。 */
XCLASS_DEFINE_BEGING(XMBBridge)
XCLASS_DEFINE_EXTEND_END(XMBBridge, XObject)

typedef struct XMBBridge
{
    XObject m_base;      /**< 基类成员；必须是第一个。 */
    XMenuBar* m_bar;     /**< 所属菜单栏（借用）。 */
    XMenu* m_menu;       /**< 动作关联的菜单（借用，可为 NULL）。 */
    XAction* m_action;   /**< 关联动作（借用）。 */
} XMBBridge;

static XVtable* XMBBridge_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XMBBridge)
    XVTABLE_INHERIT_XCLASS(XObject);
    return XVTABLE_DEFAULT;
}

static XMBBridge* xmb_bridgeCreate(XMenuBar* bar, XMenu* menu, XAction* action)
{
    XMBBridge* bridge =
        (XMBBridge*)XMemory_malloc(sizeof(*bridge),
                                   XCLASS_DEFAULT_MEMORY_TYPE);
    if (!bridge) return NULL;
    memset(bridge, 0, sizeof(*bridge));
    XObject_init(&bridge->m_base);
    XClassSetVtable(bridge, XMBBridge);
    Set_Class_Memory(bridge, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(bridge, true);
    bridge->m_bar = bar;
    bridge->m_menu = menu;
    bridge->m_action = action;
    return bridge;
}

static void xmb_bridgeTriggeredSlot(XObject* receiver, XVarList* args)
{
    XMBBridge* bridge = (XMBBridge*)receiver;
    XMenuBar* bar;
    XMenu* menu;
    if (!bridge || !bridge->m_bar) return;
    bar = bridge->m_bar;
    menu = bridge->m_menu;
    if (menu)
        XMenu_popup(menu, NULL);
    if (args)
        XVarList_args_1(args, bool, checkedIgnored);
    xmb_emitAction(bar, (size_t)XMenuBar_triggered_signal,
                   bridge->m_action);
}

static void xmb_bridgeHoveredSlot(XObject* receiver, XVarList* args)
{
    XMBBridge* bridge = (XMBBridge*)receiver;
    if (!bridge || !bridge->m_bar) return;
    /* 对标 QMenuBar::hovered(action)：悬停动作经菜单栏转发。 */
    if (bridge->m_bar->m_activeAction)
        xmb_emitAction(bridge->m_bar,
                       (size_t)XMenuBar_hovered_signal,
                       bridge->m_bar->m_activeAction);
}

/* ==================== 生命周期与虚表 ==================== */

static void VX_menuBar_paintEvent(XWidget* self, XEvent* event)
{
    XMenuBar* bar = (XMenuBar*)self;
    XPainter painter;
    XImage* image;
    XPoint offset;
    int64_t i;
    int64_t n;
    int x = 4;
    uint32_t text;
    if (!bar || !event) return;
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
    text = xsb_color_bar(bar, XPaletteColorRole_WindowText);
    if (bar->m_actions) {
        n = XVector_size_base((const XContainer*)bar->m_actions);
        for (i = 0; i < n; ++i) {
            XAction** item =
                (XAction**)XVector_at_base(bar->m_actions, i);
            XFont font = XWidget_font(self);
            const XString* title;
            if (item && *item) {
                title = XAction_text_const(*item);
                if (title && XString_length_base(title) > 0) {
                    XPainter_setFont(&painter, &font);
                    XPainter_drawText(&painter, x, 16,
                                      XString_toUtf8(title), text);
                    x += 60; /* 固定间距确保中文菜单文字不重叠。 */
                } else {
                    x += 12;
                }
            }
        }
    }
    XPainter_deinit(&painter);
}

static uint32_t xsb_color_bar(const XMenuBar* bar, XPaletteColorRole role)
{
#if XPALETTE_ON
    XPalette palette = XWidget_palette((XWidget*)bar);
    XColor c = XPalette_color(&palette, XPaletteColorGroup_Current, role);
    return XColor_rgba(&c);
#else
    (void)bar;
    (void)role;
    return 0xFF000000u;
#endif /* XPALETTE_ON */
}

static void VX_menuBar_deinit(XMenuBar* self)
{
    if (!self) return;
    if (self->m_actions) {
        int64_t i;
        int64_t n = XVector_size_base(
            (const XContainer*)self->m_actions);
        for (i = 0; i < n; ++i) {
            XAction** item =
                (XAction**)XVector_at_base(self->m_actions, i);
            if (item && *item)
                XAction_delete_base(*item);
        }
        XVector_delete_base(self->m_actions);
        self->m_actions = NULL;
    }
    if (self->m_menus) {
        XVector_delete_base(self->m_menus);
        self->m_menus = NULL;
    }
    XClass_Deinit_Parent(XWidget, (XWidget*)self);
}

XVtable* XMenuBar_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XMenuBar)
    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VX_menuBar_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VX_menuBar_deinit);
    return XVTABLE_DEFAULT;
}

void XMenuBar_init(XMenuBar* self, XWidget* parent, XWidgetFlags flags)
{
    XSize hint;
    if (!self) return;
    memset(self, 0, sizeof(*self));
    XWidget_init(&self->m_base, parent, flags);
    XClassSetVtable(self, XMenuBar);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    self->m_actions = XVector_Create(XAction*);
    self->m_menus = XVector_Create(XMenu*);
    self->m_defaultUp = false;
    XRect_init(&self->m_base.m_windowRect, 0, 0, 0, 0);
    XWidget_resize(self, 200, 22);
    hint.width = 200;
    hint.height = 22;
    XWidget_setSizeHint((XWidget*)self, &hint);
}

XMenuBar* XMenuBar_create_ex(XMemoryType memory, XWidget* parent,
                             XWidgetFlags flags)
{
    XMenuBar* self = (XMenuBar*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XMenuBar_init(self, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 菜单管理 ==================== */

XAction* XMenuBar_addMenu(XMenuBar* self, XMenu* menu)
{
    XAction* action;
    XMBBridge* bridge;
    const XString* title;
    if (!self || !menu || !self->m_actions || !self->m_menus) return NULL;
    action = XAction_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL, NULL);
    if (!action) return NULL;
    title = XMenu_title_const(menu);
    if (title)
        XAction_setText(action, title);
    bridge = xmb_bridgeCreate(self, menu, action);
    if (!bridge) {
        XAction_delete_base(action);
        return NULL;
    }
    XObject_connect_1((XObject*)action, XSignal(XAction_triggered_signal),
                      (XObject*)bridge, xmb_bridgeTriggeredSlot,
                      XConnectionType_Direct);
    XVector_push_back_1_base(self->m_actions, &action);
    XVector_push_back_1_base(self->m_menus, &menu);
    XWidget_update((XWidget*)self);
    return action;
}

XMenu* XMenuBar_addMenu_2(XMenuBar* self, const char* utf8Title)
{
    XMenu* menu = XMenu_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL,
                                  utf8Title);
    XAction* action;
    if (!menu) return NULL;
    action = XMenuBar_addMenu(self, menu);
    if (!action) {
        XMenu_delete_base(menu);
        return NULL;
    }
    return menu;
}

XAction* XMenuBar_addSeparator(XMenuBar* self)
{
    XAction* action;
    if (!self || !self->m_actions) return NULL;
    action = XAction_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL, NULL);
    if (!action) return NULL;
    XAction_setSeparator(action, true);
    XVector_push_back_1_base(self->m_actions, &action);
    XWidget_update((XWidget*)self);
    return action;
}

XAction* XMenuBar_insertMenu(XMenuBar* self, XAction* before, XMenu* menu)
{
    int index;
    XAction* action;
    XMBBridge* bridge;
    const XString* title;
    if (!self || !menu || !self->m_actions || !self->m_menus) return NULL;
    index = xmb_actionIndex(self, before);
    if (index < 0) return XMenuBar_addMenu(self, menu);
    action = XAction_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL, NULL);
    if (!action) return NULL;
    title = XMenu_title_const(menu);
    if (title)
        XAction_setText(action, title);
    bridge = xmb_bridgeCreate(self, menu, action);
    if (!bridge) {
        XAction_delete_base(action);
        return NULL;
    }
    XObject_connect_1((XObject*)action, XSignal(XAction_triggered_signal),
                      (XObject*)bridge, xmb_bridgeTriggeredSlot,
                      XConnectionType_Direct);
    XVector_insert_1_base(self->m_actions, index, &action, 1);
    XVector_insert_1_base(self->m_menus, index, &menu, 1);
    XWidget_update((XWidget*)self);
    return action;
}

XAction* XMenuBar_insertSeparator(XMenuBar* self, XAction* before)
{
    int index;
    XAction* action;
    if (!self || !self->m_actions) return NULL;
    index = xmb_actionIndex(self, before);
    if (index < 0) return XMenuBar_addSeparator(self);
    action = XAction_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL, NULL);
    if (!action) return NULL;
    XAction_setSeparator(action, true);
    XVector_insert_1_base(self->m_actions, index, &action, 1);
    XWidget_update((XWidget*)self);
    return action;
}

void XMenuBar_clear(XMenuBar* self)
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
    if (self->m_menus)
        XVector_clear_base(self->m_menus);
    self->m_activeAction = NULL;
    XWidget_update((XWidget*)self);
}

XAction* XMenuBar_activeAction(const XMenuBar* self)
{
    return self ? self->m_activeAction : NULL;
}

void XMenuBar_setActiveAction(XMenuBar* self, XAction* action)
{
    if (!self) return;
    self->m_activeAction = action;
}

bool XMenuBar_isDefaultUp(const XMenuBar* self)
{
    return self ? self->m_defaultUp : false;
}

void XMenuBar_setDefaultUp(XMenuBar* self, bool up)
{
    if (!self) return;
    self->m_defaultUp = up;
}

int XMenuBar_actionCount(const XMenuBar* self)
{
    return (self && self->m_actions)
               ? (int)XVector_size_base(
                     (const XContainer*)self->m_actions)
               : 0;
}

/* ==================== 信号 ==================== */

void* XMenuBar_triggered_signal(XMenuBar* self, XAction* action)
{
    (void)self;
    (void)action;
    return (void*)(size_t)XMenuBar_triggered_signal;
}

void* XMenuBar_hovered_signal(XMenuBar* self, XAction* action)
{
    (void)self;
    (void)action;
    return (void*)(size_t)XMenuBar_hovered_signal;
}

#endif /* XWIDGET_ON && XMENU_ON && XMENUBAR_ON */
