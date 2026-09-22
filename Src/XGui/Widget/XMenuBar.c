/**
 * @file       XMenuBar.c
 * @brief      菜单栏控件实现（对标 Qt 6.8 QMenuBar 全部公共 API）。
 * @details    与同名头文件的公共 API 一一对应；内部实现细节见
 *             头文件 @note 与函数级 Doxygen 注释。
 * @author     XinYueC 团队
 */

#include "XMenuBar.h"
#include "XStyle.h"
#include "XStyleOption.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XVarList.h"
#include "XPainter.h"
#include "XGuiConfig.h"

#include "XAlgorithm.h"
#include "XWidget_Protected.h"
#include <stdio.h>

#if XWIDGET_ON && XMENU_ON && XMENUBAR_ON
#include "XAbstractButton.h"
static uint32_t xsb_color_bar(const XMenuBar* bar, XPaletteColorRole role);
static int xmenubar_itemWidth(const XMenuBar* bar, const XAction* action);


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

/** @brief 读取 index 处动作的所有权标记；标记缺失/不对齐时保守按
 *         「拥有」处理（与历史行为一致，避免借用动作被误判为借用而
 *         在析构时漏删菜单栏自建动作）。 */
static bool xmb_ownedAt(const XMenuBar* self, int64_t index)
{
    bool* flag;
    if (!self || !self->m_actionOwned || !self->m_actions) return true;
    if (XVector_size_base((const XContainer*)self->m_actionOwned) !=
        XVector_size_base((const XContainer*)self->m_actions))
        return true;
    flag = (bool*)XVector_at_base(self->m_actionOwned, index);
    return flag ? *flag : true;
}

/** @brief 追加一个空菜单占位，保持 m_menus 与 m_actions 一一对齐
 *         （普通动作/分隔条不关联子菜单，与 addMenu 的真实条目并存）。 */
static void xmb_pushMenuSlot(XMenuBar* self)
{
    XMenu* menu = NULL;
    if (!self || !self->m_menus) return;
    XVector_push_back_1_base(self->m_menus, &menu);
}

/** @brief 在 index 处插入一个空菜单占位；index 越界时退化为追加。 */
static void xmb_insertMenuSlot(XMenuBar* self, int index)
{
    XMenu* menu = NULL;
    int64_t n;
    if (!self || !self->m_menus) return;
    n = XVector_size_base((const XContainer*)self->m_menus);
    if (index < 0 || (int64_t)index >= n)
        XVector_push_back_1_base(self->m_menus, &menu);
    else
        XVector_insert_1_base(self->m_menus, index, &menu, 1);
}

/** @brief 追加动作所有权标记（与各创建路径的动作追加成对调用）。 */
static void xmb_pushOwnedFlag(XMenuBar* self, bool owned)
{
    if (!self || !self->m_actionOwned) return;
    XVector_push_back_1_base(self->m_actionOwned, &owned);
}

/** @brief 在 index 处插入动作所有权标记；index<0 或越界时追加。 */
static void xmb_insertOwnedFlag(XMenuBar* self, int index, bool owned)
{
    int64_t n;
    if (!self || !self->m_actionOwned) return;
    n = XVector_size_base((const XContainer*)self->m_actionOwned);
    if (index < 0 || (int64_t)index >= n)
        XVector_push_back_1_base(self->m_actionOwned, &owned);
    else
        XVector_insert_1_base(self->m_actionOwned, index, &owned, 1);
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
    XMemset(bridge, 0, sizeof(*bridge));
    XObject_init(&bridge->m_base);
    XClassSetVtable(bridge, XMBBridge);
    Set_Class_Memory(bridge, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(bridge, true);
    bridge->m_bar = bar;
    bridge->m_menu = menu;
    bridge->m_action = action;
    /* 桥挂为动作的 XObject 子（§8.0g8）：动作析构级联释放堆子，桥随
       其配对动作存亡——此前桥仅被信号连接引用、无持有者，逐 addMenu
       泄漏（ASan 归因 560B×5）。 */
    if (action)
        XObject_setParent(&bridge->m_base, (XObject*)action);
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
    if (menu) {
        /* 对标 QMenuBar:菜单弹出于触发动作的全局位置(动作矩形
         * 左下角),而非屏幕原点(0,0)。 */
        XRect r = XMenuBar_actionGeometry(bar, bridge->m_action);
        XPoint local;
        XPoint g;
        local.x = r.x;
        local.y = r.y + r.height;
        g = XWidget_mapToGlobal((XWidget*)bar, &local);
        XMenu_popup(menu, &g);
    }
    if (args)
        XVarList_args_1(args, bool, checkedIgnored);
    xmb_emitAction(bar, (size_t)XMenuBar_triggered_signal,
                   bridge->m_action);
}

static void xmb_bridgeHoveredSlot(XObject* receiver, XVarList* args)
{
    XMBBridge* bridge = (XMBBridge*)receiver;
    if (!bridge || !bridge->m_bar) return;
    /* 对标 QMenuBar::hovered(action)：载荷是被悬停的动作本身（此前
     * 取 m_activeAction，若悬停先于激活动作同步则载荷错位）。
     * XAction_hovered_signal 为无参信号（空 VarList），args 不可解包。 */
    (void)args;
    xmb_emitAction(bridge->m_bar,
                   (size_t)XMenuBar_hovered_signal,
                   bridge->m_action);
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
    image = XWidget_paintImage(self);
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
#if XSTYLE_ON
    if (XStyle_defaultStyle() != NULL) {
        /* Fusion/公共风格接管：面板 + 每项走 PE_PanelMenuBar/CE_MenuBarItem。 */
        XStyle* style = XStyle_defaultStyle();
        XStyleOption panel;
        XStyleOption_init(&panel, XStylePE_PanelMenuBar);
        {
            XRect pr;
            XRect_init(&pr, 0, 0, XWidget_width(self), XWidget_height(self));
            panel.m_rect = pr;
        }
        panel.m_state = XWidget_isEnabled(self) ? XStyleState_Enabled : 0;
#if XPALETTE_ON
        panel.m_palette = XWidget_palette(self);
#endif
        XStyle_drawPrimitive(style, XStylePE_PanelMenuBar, &panel, &painter,
                             self);
        if (bar->m_actions) {
            n = XVector_size_base((const XContainer*)bar->m_actions);
            for (i = 0; i < n; ++i) {
                XAction** item = (XAction**)XVector_at_base(bar->m_actions, i);
                XFont font = XWidget_font(self);
                const XString* title;
                if (item && *item) {
                    XStyleOption mi;
                    title = XAction_text_const(*item);
                    XStyleOption_init(&mi, XStyleCE_MenuBarItem);
                    {
                        int mw = title
                            ? XPainter_textWidth(&font, XString_toUtf8(title))
                              + 16 : 24;
                        XRect mr;
                        XRect_init(&mr, x - 8, 0, mw, XWidget_height(self));
                        mi.m_rect = mr;
                    }
                    mi.m_state = XWidget_isEnabled(self)
                        ? XStyleState_Enabled : 0;
                    if (*item == bar->m_activeAction) {
                        /* 悬停高亮（复扫 R-70）：激活条目按 Fusion 口径
                         * 置 selected+sunken（CE_MenuBarItem 据此画高亮
                         * 框，对标 Qt 菜单栏活动项）。 */
                        mi.m_state |= XStyleState_Selected |
                                      XStyleState_Sunken;
                        mi.m_selected = true;
                    }
                    mi.m_text = title ? XString_toUtf8(title) : "";
#if XPALETTE_ON
                    mi.m_palette = XWidget_palette(self);
#endif
                    XPainter_setFont(&painter, &font);
                    XStyle_drawControl(style, XStyleCE_MenuBarItem, &mi,
                                       &painter, self);
                    x += mi.m_rect.width;
                XFont_deinit_base(&font);
                }
            }
        }
        XPainter_deinit(&painter);
        return;
    }
#endif /* XSTYLE_ON */
    if (bar->m_actions) {
        n = XVector_size_base((const XContainer*)bar->m_actions);
        for (i = 0; i < n; ++i) {
            XAction** item =
                (XAction**)XVector_at_base(bar->m_actions, i);
            XFont font = XWidget_font(self);
            const XString* title;
            if (item && *item) {
                title = XAction_text_const(*item);
                if (*item == bar->m_activeAction) {
                    /* 悬停高亮（复扫 R-70）：高亮条与 actionAt 命中判定
                     * 同宽（xmenubar_itemWidth）。 */
                    XRect hl;
                    XRect_init(&hl, x, 0, xmenubar_itemWidth(bar, *item),
                               XWidget_height(self));
                    XPainter_fillRect(&painter, &hl, 0xFFB0C4DEu);
                }
                if (title && XString_length_base(title) > 0) {
                    XPainter_setFont(&painter, &font);
                    XPainter_drawText(&painter, x, 16,
                                      XString_toUtf8(title), text);
                    x += 60; /* 固定间距确保中文菜单文字不重叠。 */
                } else {
                    x += 12;
                }
            XFont_deinit_base(&font);
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
            /* 仅销毁菜单栏创建并拥有的动作；insertAction 注入的借用
             * 动作归调用方，析构时只摘除不释放。 */
            if (item && *item && xmb_ownedAt(self, i))
                XAction_delete_base(*item);
        }
        XVector_delete_base(self->m_actions);
        self->m_actions = NULL;
    }
    if (self->m_menus) {
        XVector_delete_base(self->m_menus);
        self->m_menus = NULL;
    }
    if (self->m_actionOwned) {
        XVector_delete_base((XClass*)self->m_actionOwned);
        self->m_actionOwned = NULL;
    }
    XClass_Deinit_Parent(XWidget, (XWidget*)self);
}

/** @brief 按下：命中动作即触发（triggered 桥接负责弹出对应菜单）。
 * @note  命中经 XMenuBar_actionAt 按条目宽度累计判定，与绘制布局
 *        同口径；未命中动作时忽略事件。 */
static void VX_menuBar_mousePressEvent(XWidget* self, XEvent* event)
{
    XMenuBar* bar = (XMenuBar*)self;
    XMouseEvent* me;
    XPoint pos;
    XAction* action;
    if (!bar || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_PRESS) return;
    me = (XMouseEvent*)event;
    pos = XMouseEvent_position(me);
    action = XMenuBar_actionAt(bar, &pos);
    if (action) {
        /* 对标 QMenuBar：按下即激活条目（高亮）再触发，triggered 桥接
         * 负责弹出关联菜单。 */
        if (action != bar->m_activeAction) {
            bar->m_activeAction = action;
            XWidget_update(self);
        }
        XAction_trigger(action);
        XEvent_accept(event);
        return;
    }
    XEvent_ignore(event);
}

/** @brief 悬停：高亮条目并发射 hovered(action)（复扫 R-70，对标
 *  QMenuBar 悬停驱动：内部 hover 处理对任意动作——含不关联菜单的普通
 *  动作——发 hovered；条目变化才发射，避免同条目内移动重复刷）。 */
static void VX_menuBar_mouseMoveEvent(XWidget* self, XEvent* event)
{
    XMenuBar* bar = (XMenuBar*)self;
    XMouseEvent* me;
    XPoint pos;
    XAction* action;
    if (!bar || !event || XEvent_type(event) != XEVENT_TYPE_MOUSE_MOVE)
        return;
    me = (XMouseEvent*)event;
    pos = XMouseEvent_position(me);
    action = XMenuBar_actionAt(bar, &pos);
    if (action && action != bar->m_activeAction) {
        bar->m_activeAction = action;
        XWidget_update(self);
        xmb_emitAction(bar, (size_t)XMenuBar_hovered_signal, action);
        XEvent_accept(event);
        return;
    }
    XEvent_ignore(event);
}

/** @brief 指针离开菜单栏：清除悬停高亮（对标 QMenuBar 移出条目去激活）。 */
static void VX_menuBar_leaveEvent(XWidget* self, XEvent* event)
{
    XMenuBar* bar = (XMenuBar*)self;
    if (!bar || !event) return;
    if (bar->m_activeAction) {
        bar->m_activeAction = NULL;
        XWidget_update(self);
    }
    XEvent_accept(event);
}

XVtable* XMenuBar_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XMenuBar)
    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VX_menuBar_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MousePressEvent,
                             VX_menuBar_mousePressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseMoveEvent,
                             VX_menuBar_mouseMoveEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_LeaveEvent, VX_menuBar_leaveEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VX_menuBar_deinit);
    return XVTABLE_DEFAULT;
}

void XMenuBar_init(XMenuBar* self, XWidget* parent, XWidgetFlags flags)
{
    XSize hint;
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XWidget_init(&self->m_base, parent, flags);
    XClassSetVtable(self, XMenuBar);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    self->m_actions = XVector_Create(XAction*);
    self->m_menus = XVector_Create(XMenu*);
    self->m_actionOwned = XVector_Create(bool);
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
    /* 悬停桥接（复扫 R-70）：桥槽此前已实现但从未连接，hovered 永不
     * 发射；动作被外部悬停驱动（XAction_hover）时经此转发菜单栏。 */
    XObject_connect_1((XObject*)action, XSignal(XAction_hovered_signal),
                      (XObject*)bridge, xmb_bridgeHoveredSlot,
                      XConnectionType_Direct);
    XVector_push_back_1_base(self->m_actions, &action);
    XVector_push_back_1_base(self->m_menus, &menu);
    /* 动作由本函数创建：登记为菜单栏拥有（对标 Qt 父子所有权）。 */
    xmb_pushOwnedFlag(self, true);
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
    xmb_pushMenuSlot(self);
    xmb_pushOwnedFlag(self, true);
    XWidget_update((XWidget*)self);
    return action;
}

XAction* XMenuBar_addAction(XMenuBar* self, const XString* text)
{
    XAction* action;
    if (!self || !self->m_actions) return NULL;
    action = XAction_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL, NULL);
    if (!action) return NULL;
    if (text) XAction_setText(action, text);
    XVector_push_back_1_base(self->m_actions, &action);
    xmb_pushMenuSlot(self);
    /* 对标 QMenuBar::addAction(text)：动作由菜单栏创建并持有。 */
    xmb_pushOwnedFlag(self, true);
    XWidget_update((XWidget*)self);
    return action;
}

XAction* XMenuBar_addAction_2(XMenuBar* self, const char* utf8Text)
{
    XAction* action;
    if (!self || !self->m_actions) return NULL;
    action = XAction_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL, NULL);
    if (!action) return NULL;
    XAction_setText_2(action, utf8Text ? utf8Text : "");
    XVector_push_back_1_base(self->m_actions, &action);
    xmb_pushMenuSlot(self);
    /* 对标 QMenuBar::addAction(text) 的字符串重载：动作由菜单栏持有。 */
    xmb_pushOwnedFlag(self, true);
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
    /* 悬停桥接（复扫 R-70）：与 addMenu 同款连接。 */
    XObject_connect_1((XObject*)action, XSignal(XAction_hovered_signal),
                      (XObject*)bridge, xmb_bridgeHoveredSlot,
                      XConnectionType_Direct);
    XVector_insert_1_base(self->m_actions, index, &action, 1);
    XVector_insert_1_base(self->m_menus, index, &menu, 1);
    /* 动作由本函数创建：登记为菜单栏拥有。 */
    xmb_insertOwnedFlag(self, index, true);
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
    xmb_insertMenuSlot(self, index);
    xmb_insertOwnedFlag(self, index, true);
    XWidget_update((XWidget*)self);
    return action;
}

XAction* XMenuBar_insertAction(XMenuBar* self, XAction* before,
                               XAction* action)
{
    int index;
    if (!self || !action || !self->m_actions || !self->m_menus ||
        !self->m_actionOwned)
        return NULL;
    /* 对标 Qt：动作已在栏内时先摘除再插入（移动语义）。 */
    if (xmb_actionIndex(self, action) >= 0)
        XMenuBar_removeAction(self, action);
    index = xmb_actionIndex(self, before);
    if (index < 0) {
        /* before 为 NULL 或不在栏内：等价追加到末尾（对标 Qt pos<0 分支）。 */
        XVector_push_back_1_base(self->m_actions, &action);
        xmb_pushMenuSlot(self);
        /* 借用语义：外部动作仅挂接，菜单栏不取得所有权。 */
        xmb_pushOwnedFlag(self, false);
    } else {
        XMenu* menuSlot = NULL;
        XVector_insert_1_base(self->m_actions, index, &action, 1);
        XVector_insert_1_base(self->m_menus, index, &menuSlot, 1);
        xmb_insertOwnedFlag(self, index, false);
    }
    XWidget_update((XWidget*)self);
    return action;
}

void XMenuBar_removeAction(XMenuBar* self, XAction* action)
{
    int index;
    int64_t nAct;
    if (!self || !action || !self->m_actions) return;
    index = xmb_actionIndex(self, action);
    if (index < 0) return;
    nAct = XVector_size_base((const XContainer*)self->m_actions);
    /* 平行容器按「移除前长度一致」守卫同步收缩，保持索引对齐。 */
    if (self->m_actionOwned &&
        XVector_size_base((const XContainer*)self->m_actionOwned) == nAct)
        XVector_remove_base(self->m_actionOwned, index, 1);
    if (self->m_menus &&
        XVector_size_base((const XContainer*)self->m_menus) == nAct)
        XVector_remove_base(self->m_menus, index, 1);
    XVector_remove_base(self->m_actions, index, 1);
    /* 摘除的是当前激活动作时清空引用，避免悬挂（对标 Qt 的
     * ActionRemoved 处理）。 */
    if (self->m_activeAction == action)
        self->m_activeAction = NULL;
    XWidget_update((XWidget*)self);
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
            /* 仅销毁菜单栏拥有的动作；借用动作仅随容器摘除。 */
            if (item && *item && xmb_ownedAt(self, i))
                XAction_delete_base(*item);
        }
        XVector_clear_base((XContainer*)self->m_actions);
    }
    if (self->m_menus)
        XVector_clear_base((XContainer*)self->m_menus);
    if (self->m_actionOwned)
        XVector_clear_base((XContainer*)self->m_actionOwned);
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

/* ==================== Task 2.10：几何/角落/尺寸 API ================== */

/** @brief 是否处于样式接管模式（决定条目宽度公式）。 */
static bool xmenubar_styleMode(const XMenuBar* bar)
{
#if XSTYLE_ON
    (void)bar;
    return XStyle_defaultStyle() != NULL;
#else
    (void)bar;
    return false;
#endif
}

/** @brief 条目逻辑宽度：样式模式=文本宽+16（空 24），否则固定 60（空 12）。 */
static int xmenubar_itemWidth(const XMenuBar* bar, const XAction* action)
{
    const XString* text;
    XFont font;
    if (!action) return xmenubar_styleMode(bar) ? 24 : 12;
    text = XAction_text_const(action);
    if (!text || XString_length_base(text) <= 0)
        return xmenubar_styleMode(bar) ? 24 : 12;
    if (xmenubar_styleMode(bar)) {
        int w;
        font = XWidget_font((XWidget*)bar);
        w = XPainter_textWidth(&font, XString_toUtf8(text)) + 16;
        XFont_deinit_base(&font);
        return w;
    }
    return 60;
}

/** @brief 计算第 index 个动作的逻辑矩形（与绘制同一累加模型）。 */
static XRect xmenubar_actionRectAt(const XMenuBar* bar, int64_t index)
{
    XRect out;
    int64_t i;
    int64_t n;
    int x = 4;
    int h;
    XRect_init(&out, 0, 0, 0, 0);
    if (!bar || !bar->m_actions) return out;
    n = XVector_size_base((const XContainer*)bar->m_actions);
    if (index < 0 || index >= n) return out;
    h = XWidget_height((XWidget*)bar);
    for (i = 0; i <= index; ++i) {
        XAction** item =
            (XAction**)XVector_at_base((const XContainer*)bar->m_actions,
                                       i);
        int w = xmenubar_itemWidth(bar, item ? *item : NULL);
        if (i == index) {
            XRect_init(&out, x, 0, w, h);
            return out;
        }
        x += w;
    }
    return out;
}

XAction* XMenuBar_actionAt(const XMenuBar* self, const XPoint* pos)
{
    int64_t i;
    int64_t n;
    int x;
    if (!self || !pos || !self->m_actions) return NULL;
    if (pos->y < 0 || pos->y >= XWidget_height((XWidget*)self))
        return NULL;
    x = 4;
    n = XVector_size_base((const XContainer*)self->m_actions);
    for (i = 0; i < n; ++i) {
        XAction** item =
            (XAction**)XVector_at_base((const XContainer*)self->m_actions,
                                       i);
        int w = xmenubar_itemWidth(self, item ? *item : NULL);
        if (pos->x >= x && pos->x < x + w)
            return item ? *item : NULL;
        x += w;
    }
    return NULL;
}

XRect XMenuBar_actionGeometry(const XMenuBar* self, XAction* action)
{
    int64_t i;
    int64_t n;
    if (!self || !action || !self->m_actions) {
        XRect out;
        XRect_init(&out, 0, 0, 0, 0);
        return out;
    }
    n = XVector_size_base((const XContainer*)self->m_actions);
    for (i = 0; i < n; ++i) {
        XAction** item =
            (XAction**)XVector_at_base((const XContainer*)self->m_actions,
                                       i);
        if (item && *item == action)
            return xmenubar_actionRectAt(self, i);
    }
    {
        XRect out;
        XRect_init(&out, 0, 0, 0, 0);
        return out;
    }
}

XWidget* XMenuBar_cornerWidget(const XMenuBar* self, int corner)
{
    if (!self) return NULL;
    return corner == (int)XMenuBarCorner_TopLeft ? self->m_cornerWidgetL
                                                 : self->m_cornerWidgetR;
}

void XMenuBar_setCornerWidget(XMenuBar* self, XWidget* widget, int corner)
{
    if (!self) return;
    if (corner == (int)XMenuBarCorner_TopLeft)
        self->m_cornerWidgetL = widget;
    else
        self->m_cornerWidgetR = widget;
    XWidget_update((XWidget*)self);
}

int XMenuBar_heightForWidth(const XMenuBar* self, int width)
{
    XSize hint;
    (void)width;
    if (!self) return 0;
    hint = XMenuBar_sizeHint(self);
    return hint.height;
}

XSize XMenuBar_sizeHint(const XMenuBar* self)
{
    XSize out;
    int64_t i;
    int64_t n;
    int w = 8;
    if (!self) {
        XSize_init(&out, 0, 0);
        return out;
    }
    if (self->m_actions) {
        n = XVector_size_base((const XContainer*)self->m_actions);
        for (i = 0; i < n; ++i) {
            XAction** item =
                (XAction**)XVector_at_base((const XContainer*)self->m_actions,
                                           i);
            w += xmenubar_itemWidth(self, item ? *item : NULL);
        }
    }
    XSize_init(&out, w > 8 ? w : 8, 30);
    return out;
}

XSize XMenuBar_minimumSizeHint(const XMenuBar* self)
{
    return XMenuBar_sizeHint(self);
}

bool XMenuBar_isNativeMenuBar(const XMenuBar* self)
{
    return self ? self->m_nativeMenuBar : false;
}

void XMenuBar_setNativeMenuBar(XMenuBar* self, bool nativeMenuBar)
{
    if (self) self->m_nativeMenuBar = nativeMenuBar;
}

void* XMenuBar_platformMenuBar(const XMenuBar* self)
{
    (void)self;
    return NULL;
}

#endif /* XWIDGET_ON && XMENU_ON && XMENUBAR_ON */
