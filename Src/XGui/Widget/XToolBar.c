/**
 * @file       XToolBar.c
 * @brief      工具栏控件实现（对标 Qt 6.8 QToolBar 全部公共 API）。
 * @details    与同名头文件的公共 API 一一对应；内部实现细节见
 *             头文件 @note 与函数级 Doxygen 注释。
 * @author     XinYueC 团队
 */

#include "XToolBar.h"
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
    XMemset(bridge, 0, sizeof(*bridge));
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

/**
 * @brief      发射无参信号（真正无参版本）。
 * @param      bar 目标工具栏；NULL 或无已连接槽时不发射。
 * @param      signal 信号标识。
 * @return     无返回值。
 */
static void xtb_emitSignalVoid(XToolBar* bar, size_t signal)
{
    XVarList* arguments = XVarList_create(0);
    if (!arguments) return;
    if (bar && ((XObject*)bar)->m_signalSlot) {
        XObject_emitSignal((XObject*)bar, signal, arguments, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(arguments);
    }
}

/**
 * @brief      发射 bool 载荷信号（visibilityChanged(bool) 等）。
 * @param      bar 目标工具栏；NULL 或无已连接槽时不发射。
 * @param      signal 信号标识。
 * @param      value bool 载荷。
 * @return     无返回值。
 */
static void xtb_emitBool(XToolBar* bar, size_t signal, bool value)
{
    XVarList* args = XVarList_Create(XVar(bool, value));
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
    int bw = 48;
    int bh = cross > 6 ? cross - 6 : 20;
    int x = 2;
    int y = 2;
    if (!bar || !bar->m_buttons) return;
    n = XVector_size_base((const XContainer*)bar->m_buttons);
    if (horiz && bw < 4) return;
    for (i = 0; i < n; ++i) {
        XWidget* wid = NULL;
        XToolButton** btn;
        XRect r;
        if (bar->m_widgets && i < (int64_t)XVector_size_base(
                                  (const XContainer*)bar->m_widgets))
            wid = *(XWidget**)XVector_at_base(
                      (const XContainer*)bar->m_widgets, i);
        if (wid) {
            int ww = XWidget_width(wid);
            if (horiz)
                XRect_init(&r, x, y, ww, bh);
            else
                XRect_init(&r, y, x, bh, ww);
            XWidget_setGeometryRect(wid, &r);
            x += ww + 4;
            continue;
        }
        btn = (XToolButton**)XVector_at_base(bar->m_buttons, i);
        if (!btn || !*btn) continue;
        if (horiz)
            XRect_init(&r, x, y, bw, bh);
        else
            XRect_init(&r, y, x, bh, bw);
        XWidget_setGeometryRect((XWidget*)*btn, &r);
        x += bw + 4;
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
#if XSTYLE_ON
    if (XStyle_defaultStyle() != NULL) {
        /* Fusion/公共风格接管：工具栏面板走 PE_PanelToolBar。 */
        XStyle* style = XStyle_defaultStyle();
        XStyleOption opt;
        XStyleOption_init(&opt, XStylePE_PanelToolBar);
        {
            XRect pr;
            XRect_init(&pr, 0, 0, w, h);
            opt.m_rect = pr;
        }
        opt.m_state = XWidget_isEnabled(self) ? XStyleState_Enabled : 0;
#if XPALETTE_ON
        opt.m_palette = XWidget_palette(self);
#endif
        XStyle_drawPrimitive(style, XStylePE_PanelToolBar, &opt, &painter,
                             self);
        XPainter_deinit(&painter);
        return;
    }
#endif /* XSTYLE_ON */
    XRect_init(&line, 0, h - 1, w, 1);
    XPainter_fillRect(&painter, &line, mid);
    XPainter_deinit(&painter);
}

void XToolBar_setTitle(XToolBar* self, const char* utf8)
{
    if (!self) return;
    XWidget_setWindowTitle_2((XWidget*)self, utf8 ? utf8 : "");
}

const char* XToolBar_title(const XToolBar* self)
{
    const XString* t;
    const char* text;
    if (!self) return "";
    t = XWidget_windowTitle((const XWidget*)self);
    if (!t) return "";
    text = XString_toUtf8(t);
    return text ? text : "";
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
    if (self->m_bridges) {
        n = XVector_size_base((const XContainer*)self->m_bridges);
        for (i = 0; i < n; ++i) {
            XTBBridge** b =
                (XTBBridge**)XVector_at_base(self->m_bridges, i);
            if (b && *b)
                XClass_delete_base((XClass*)*b);
        }
        XVector_delete_base(self->m_bridges);
        self->m_bridges = NULL;
    }
    if (self->m_widgets) {
        /* 附加控件归调用方，仅释放容器。 */
        XVector_delete_base(self->m_widgets);
        self->m_widgets = NULL;
    }
    if (self->m_toggleAction) {
        XAction_delete_base(self->m_toggleAction);
        self->m_toggleAction = NULL;
    }
    XClass_Deinit_Parent(XWidget, (XWidget*)self);
}

/** @brief 显示事件：发射 visibilityChanged(true) 后转发父类。 */
static void VX_toolBar_showEvent(XWidget* self, XEvent* event)
{
    if (self)
        xtb_emitBool((XToolBar*)self,
                     (size_t)XToolBar_visibilityChanged_signal, true);
    XClass_Parent(XWidget, EXWidget_ShowEvent,
                  void(*)(XWidget*, XEvent*))(self, event);
}

/** @brief 隐藏事件：发射 visibilityChanged(false) 后转发父类。 */
static void VX_toolBar_hideEvent(XWidget* self, XEvent* event)
{
    if (self)
        xtb_emitBool((XToolBar*)self,
                     (size_t)XToolBar_visibilityChanged_signal, false);
    XClass_Parent(XWidget, EXWidget_HideEvent,
                  void(*)(XWidget*, XEvent*))(self, event);
}

XVtable* XToolBar_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XToolBar)
    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ResizeEvent, VX_toolBar_resizeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VX_toolBar_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ShowEvent, VX_toolBar_showEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_HideEvent, VX_toolBar_hideEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VX_toolBar_deinit);
    return XVTABLE_DEFAULT;
}

void XToolBar_init(XToolBar* self, XWidget* parent, XWidgetFlags flags)
{
    XSize hint;
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XWidget_init(&self->m_base, parent, flags);
    XClassSetVtable(self, XToolBar);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    self->m_actions = XVector_Create(XAction*);
    self->m_buttons = XVector_Create(XToolButton*);
    self->m_bridges = XVector_Create(XTBBridge*);
    self->m_widgets = XVector_Create(XWidget*);
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
    if (!self || self->m_allowedAreas == areas) return;
    self->m_allowedAreas = areas;
    xtb_emitVoid(self, (size_t)XToolBar_allowedAreasChanged_signal, areas);
}

int XToolBar_allowedAreas(const XToolBar* self)
{
    return self ? self->m_allowedAreas : 0;
}

bool XToolBar_isAreaAllowed(const XToolBar* self, int area)
{
    return self ? (self->m_allowedAreas & area) != 0 : false;
}

bool XToolBar_isFloating(const XToolBar* self)
{
    return self ? self->m_floating : false;
}

void XToolBar_setIconSize(XToolBar* self, int size)
{
    if (!self || size <= 0 || self->m_iconSize == size) return;
    self->m_iconSize = size;
    xtb_relayout(self);
    if (self && ((XObject*)self)->m_signalSlot) {
        XVarList* args = XVarList_Create(XVar(int, size), XVar(int, size));
        if (args) {
            XObject_emitSignal((XObject*)self,
                               (size_t)XToolBar_iconSizeChanged_signal(
                                   self, size, size),
                               args, NULL, NULL, XEVENT_PRIORITY_NORMAL);
        }
    }
}

int XToolBar_iconSize(const XToolBar* self)
{
    return self ? self->m_iconSize : 16;
}

void XToolBar_setToolButtonStyle(XToolBar* self, int style)
{
    int64_t i;
    int64_t n;
    if (!self || self->m_buttonStyle == style) return;
    self->m_buttonStyle = style;
    /* 同步既有按钮（对标 QToolBar::setToolButtonStyle 更新全部按钮）。 */
    if (self->m_buttons) {
        n = XVector_size_base((const XContainer*)self->m_buttons);
        for (i = 0; i < n; ++i) {
            XToolButton** btn =
                (XToolButton**)XVector_at_base(self->m_buttons, i);
            if (btn && *btn)
                XToolButton_setToolButtonStyle(*btn,
                    (XToolButtonStyle)style);
        }
    }
    xtb_emitVoid(self, (size_t)XToolBar_toolButtonStyleChanged_signal,
                 style);
}

int XToolBar_toolButtonStyle(const XToolBar* self)
{
    return self ? self->m_buttonStyle : 0;
}

/* ==================== 动作与控件管理 ==================== */

/** @brief 查找动作索引；未找到返回 -1。 */
static int xtb_findIndex(const XToolBar* self, XAction* action)
{
    int64_t i;
    int64_t n;
    if (!self || !self->m_actions || !action) return -1;
    n = XVector_size_base((const XContainer*)self->m_actions);
    for (i = 0; i < n; ++i) {
        XAction** item =
            (XAction**)XVector_at_base(self->m_actions, i);
        if (item && *item == action) return (int)i;
    }
    return -1;
}

/** @brief 在指定位置插入 (action, button, bridge, widget) 平行元组；
 *         index<0 或越界时追加。widget 非 NULL 时不创建按钮/桥
 *         （占位动作，对标 QToolBar::insertWidget）。 */
static void xtb_insertAt(XToolBar* self, int index, XAction* action,
                         XWidget* widget)
{
    XToolButton* button = NULL;
    XTBBridge* bridge = NULL;
    int64_t n;
    if (!self || !action || !self->m_actions || !self->m_buttons ||
        !self->m_bridges || !self->m_widgets)
        return;
    if (widget == NULL) {
        button = XToolButton_create(self, 0);
        if (button) {
            const XString* atext;
            /* 工具栏按钮样式下发（对标 QToolBar 创建按钮时应用
             * toolButtonStyle）：默认 IconOnly 且无图标会画成空框。 */
            XToolButton_setToolButtonStyle(button,
                (XToolButtonStyle)self->m_buttonStyle);
            XToolButton_setDefaultAction(button, action);
            atext = XAction_text_const(action);
            if (atext && XString_length_base(atext) > 0) {
                XAbstractButton_setText_2((XAbstractButton*)button,
                                          XString_toUtf8(atext));
            }
        }
        bridge = xtb_bridgeCreate(self, action);
        if (bridge) {
            XObject_connect_1((XObject*)action,
                              XSignal(XAction_triggered_signal),
                              (XObject*)bridge, xtb_bridgeTriggeredSlot,
                              XConnectionType_Direct);
            XObject_connect_1((XObject*)action,
                              XSignal(XAction_hovered_signal),
                              (XObject*)bridge, xtb_bridgeHoveredSlot,
                              XConnectionType_Direct);
        }
    }
    n = XVector_size_base((const XContainer*)self->m_actions);
    if (index < 0 || index >= (int)n) {
        XVector_push_back_1_base(self->m_actions, &action);
        XVector_push_back_1_base(self->m_buttons, &button);
        XVector_push_back_1_base(self->m_bridges, &bridge);
        XVector_push_back_1_base(self->m_widgets, &widget);
    } else {
        XVector_Insert(self->m_actions, index, XAction*, action);
        XVector_Insert(self->m_buttons, index, XToolButton*, button);
        XVector_Insert(self->m_bridges, index, XTBBridge*, bridge);
        XVector_Insert(self->m_widgets, index, XWidget*, widget);
    }
    if (widget)
        XWidget_setParent(widget, (XWidget*)self, 0);
    xtb_relayout(self);
}

void XToolBar_addAction(XToolBar* self, XAction* action)
{
    if (!self || !action) return;
    xtb_insertAt(self, -1, action, NULL);
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

XAction* XToolBar_insertSeparator(XToolBar* self, XAction* before)
{
    XAction* action;
    int index;
    if (!self) return NULL;
    action = XAction_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL, NULL);
    if (!action) return NULL;
    XAction_setSeparator(action, true);
    index = xtb_findIndex(self, before);
    xtb_insertAt(self, index, action, NULL);
    return action;
}

void XToolBar_addWidget(XToolBar* self, XWidget* widget)
{
    XToolBar_insertWidget(self, NULL, widget);
}

void XToolBar_insertWidget(XToolBar* self, XAction* before,
                           XWidget* widget)
{
    XAction* placeholder;
    int index;
    if (!self || !widget) return;
    placeholder = XAction_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL, NULL);
    if (!placeholder) return;
    index = xtb_findIndex(self, before);
    xtb_insertAt(self, index, placeholder, widget);
}

XWidget* XToolBar_widgetForAction(XToolBar* self, XAction* action)
{
    int64_t i;
    int64_t n;
    if (!self || !action || !self->m_actions || !self->m_widgets)
        return NULL;
    n = XVector_size_base((const XContainer*)self->m_actions);
    for (i = 0; i < n; ++i) {
        XAction** item =
            (XAction**)XVector_at_base(self->m_actions, i);
        if (item && *item == action) {
            XWidget** w = (XWidget**)XVector_at_base(
                (const XContainer*)self->m_widgets, i);
            return w ? *w : NULL;
        }
    }
    return NULL;
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
            XToolButton** btn =
                (XToolButton**)XVector_at_base(self->m_buttons, i);
            XTBBridge** b =
                (XTBBridge**)XVector_at_base(self->m_bridges, i);
            if (btn && *btn)
                XClass_delete_base((XClass*)*btn);
            if (b && *b)
                XClass_delete_base((XClass*)*b);
            XVector_remove_base(self->m_actions, i, 1);
            XVector_remove_base(self->m_buttons, i, 1);
            XVector_remove_base(self->m_bridges, i, 1);
            if (self->m_widgets &&
                i < (int64_t)XVector_size_base(
                        (const XContainer*)self->m_widgets))
                XVector_remove_base(self->m_widgets, i, 1);
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
    if (self->m_bridges) {
        n = XVector_size_base((const XContainer*)self->m_bridges);
        for (i = 0; i < n; ++i) {
            XTBBridge** b =
                (XTBBridge**)XVector_at_base(self->m_bridges, i);
            if (b && *b)
                XClass_delete_base((XClass*)*b);
        }
        XVector_clear_base(self->m_bridges);
    }
    if (self->m_widgets) {
        XVector_clear_base(self->m_widgets);
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

/* ==================== Task 2.10：几何与开关动作 ===================== */

/** @brief 计算第 index 项的逻辑矩形（与 relayout 同一模型）。 */
static XRect xtb_actionRectAt(const XToolBar* bar, int64_t index)
{
    XRect out;
    int64_t i;
    int64_t n;
    int x = 2;
    int y = 2;
    int horiz = bar->m_orientation != 2;
    int h = XWidget_height((XWidget*)bar);
    int w = XWidget_width((XWidget*)bar);
    int cross = horiz ? h : w;
    int bh = cross > 6 ? cross - 6 : 20;
    XRect_init(&out, 0, 0, 0, 0);
    if (!bar || !bar->m_buttons) return out;
    n = XVector_size_base((const XContainer*)bar->m_buttons);
    if (index < 0 || index >= n) return out;
    for (i = 0; i <= index; ++i) {
        XWidget* wid = NULL;
        int iw = 48;
        if (bar->m_widgets && i < (int64_t)XVector_size_base(
                                  (const XContainer*)bar->m_widgets))
            wid = *(XWidget**)XVector_at_base(
                      (const XContainer*)bar->m_widgets, i);
        if (wid) iw = XWidget_width(wid);
        if (i == index) {
            XRect_init(&out, x, y, iw, bh);
            return out;
        }
        x += iw + 4;
    }
    return out;
}

XAction* XToolBar_actionAt(const XToolBar* self, const XPoint* pos)
{
    int64_t i;
    int64_t n;
    int x = 2;
    int y = 2;
    int horiz;
    int h;
    int w;
    int cross;
    int bh;
    if (!self || !pos || !self->m_buttons) return NULL;
    horiz = self->m_orientation != 2;
    h = XWidget_height((XWidget*)self);
    w = XWidget_width((XWidget*)self);
    cross = horiz ? h : w;
    bh = cross > 6 ? cross - 6 : 20;
    if (pos->y < 0 || pos->y >= h) return NULL;
    n = XVector_size_base((const XContainer*)self->m_buttons);
    for (i = 0; i < n; ++i) {
        XWidget* wid = NULL;
        XToolButton** btn;
        int iw = 48;
        if (self->m_widgets && i < (int64_t)XVector_size_base(
                                    (const XContainer*)self->m_widgets))
            wid = *(XWidget**)XVector_at_base(
                      (const XContainer*)self->m_widgets, i);
        if (wid) iw = XWidget_width(wid);
        if (pos->x >= x && pos->x < x + iw) {
            if (wid) {
                XAction** item = (XAction**)XVector_at_base(
                    (const XContainer*)self->m_actions, i);
                return item ? *item : NULL;
            }
            btn = (XToolButton**)XVector_at_base(self->m_buttons, i);
            if (btn && *btn) {
                XAction** item = (XAction**)XVector_at_base(
                    (const XContainer*)self->m_actions, i);
                return item ? *item : NULL;
            }
            return NULL;
        }
        x += iw + 4;
    }
    (void)y;
    (void)bh;
    return NULL;
}

XRect XToolBar_actionGeometry(const XToolBar* self, XAction* action)
{
    int64_t i;
    int64_t n;
    XRect out;
    XRect_init(&out, 0, 0, 0, 0);
    if (!self || !action || !self->m_actions) return out;
    n = XVector_size_base((const XContainer*)self->m_actions);
    for (i = 0; i < n; ++i) {
        XAction** item =
            (XAction**)XVector_at_base(self->m_actions, i);
        if (item && *item == action)
            return xtb_actionRectAt(self, i);
    }
    return out;
}

/** @brief toggleViewAction 槽：切换工具栏可见性。 */
static void xtb_toggleVisibilitySlot(XObject* receiver, XVarList* args)
{
    XToolBar* bar = (XToolBar*)receiver;
    (void)args;
    if (!bar) return;
    XWidget_setVisible((XWidget*)bar, !XWidget_isVisible((XWidget*)bar));
}

XAction* XToolBar_toggleViewAction(XToolBar* self)
{
    if (!self) return NULL;
    if (!self->m_toggleAction) {
        self->m_toggleAction =
            XAction_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL, NULL);
        if (!self->m_toggleAction) return NULL;
        XObject_connect_1((XObject*)self->m_toggleAction,
                          XSignal(XAction_triggered_signal),
                          (XObject*)self, xtb_toggleVisibilitySlot,
                          XConnectionType_Direct);
    }
    return self->m_toggleAction;
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

void* XToolBar_allowedAreasChanged_signal(XToolBar* self, int areas)
{
    (void)self;
    (void)areas;
    return (void*)(size_t)XToolBar_allowedAreasChanged_signal;
}

void* XToolBar_toolButtonStyleChanged_signal(XToolBar* self, int style)
{
    (void)self;
    (void)style;
    return (void*)(size_t)XToolBar_toolButtonStyleChanged_signal;
}







/**
 * @brief      发射 visibilityChanged(bool) 信号（对标 QToolBar::
 *             visibilityChanged）。
 * @details    显示/隐藏由 showEvent/hideEvent 驱动真发射；本函数供
 *             外部手动触发或连接使用。self 非 NULL 且有已连接槽时经
 *             XObject_emitSignal 同步通知，否则只返回信号标识。
 * @param      self 目标工具栏；可为 NULL。
 * @param      visible true 表示已显示，false 表示已隐藏。
 * @return     不透明的 visibilityChanged 信号标识；返回值不指向可释放
 *             对象，也不得解引用。
 */
void* XToolBar_visibilityChanged_signal(XToolBar* self, bool visible)
{
    if (!self)
        return (void*)(size_t)XToolBar_visibilityChanged_signal;
    xtb_emitBool(self, (size_t)XToolBar_visibilityChanged_signal, visible);
    return (void*)(size_t)XToolBar_visibilityChanged_signal;
}

/**
 * @brief      发射 topLevelChanged(bool) 信号（对标 QToolBar::
 *             topLevelChanged）。
 * @details    Qt 的发射点在停靠布局体系的 isFloating 变化路径；本实现
 *             停靠/拖拽体系未建（m_floating 恒为存储位默认 false），
 *             无内部自动发射路径，本函数作为句柄预留按"真发射"形态
 *             实现：self 非 NULL 且有已连接槽时经 XObject_emitSignal
 *             同步通知，否则只返回信号标识。
 * @param      self 目标工具栏；可为 NULL。
 * @param      topLevel true 表示变为顶层（浮动），false 表示已停靠。
 * @return     不透明的 topLevelChanged 信号标识；返回值不指向可释放
 *             对象，也不得解引用。
 */
void* XToolBar_topLevelChanged_signal(XToolBar* self, bool topLevel)
{
    if (!self)
        return (void*)(size_t)XToolBar_topLevelChanged_signal;
    xtb_emitBool(self, (size_t)XToolBar_topLevelChanged_signal, topLevel);
    return (void*)(size_t)XToolBar_topLevelChanged_signal;
}
void* XToolBar_iconSizeChanged_signal(XToolBar* self, int width, int height)
{
    (void)self; (void)width; (void)height;
    return (void*)(size_t)XToolBar_iconSizeChanged_signal;
}







#endif /* XWIDGET_ON && XACTION_ON && XTOOLBUTTON_ON && XTOOLBAR_ON */
