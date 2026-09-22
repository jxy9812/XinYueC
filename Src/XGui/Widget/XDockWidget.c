#include "XDockWidget.h"
#include "XStyle.h"
#include "XStyleOption.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XVarList.h"
#include "XGuiConfig.h"
#include "XAction.h"
#include "XMainWindow_Protected.h"

#include "XAlgorithm.h"
#include "XPainter.h"
#include "XWidget_Protected.h"

#if XWIDGET_ON && XDOCKWIDGET_ON

/* ==================== 内部常量 ==================== */

/** @brief 标题条交互高度：绘制 20 像素标题 + 1 像素底部分隔线；
 *         setWidget 的内容区从该高度起（与既有 setWidget/绘制口径一致）。 */
#define XDW_TITLE_H 21
/** @brief 标题条右侧关闭按钮命中区宽度（对标 Qt 标题条关闭按钮）。 */
#define XDW_CLOSE_BOX 18

/** @brief 调用 XWidget 基类事件实现（经 XClass_Parent 取基类虚表槽位）。
 * @note  不可用 XWidget_*_base 入口转发：该入口按对象虚表再分派，会
 *        重新命中本类重载形成自递归（与 XToolBar 的
 *        XClass_Parent 转发同口径）。 */
#define xdw_callParent(self, eventSlot, eventArg)                      \
    XClass_Parent(XWidget, EXWidget_##eventSlot,                      \
                  void (*)(XWidget*, XEvent*))((self), (eventArg))

/* ==================== 内部工具 ==================== */

/** @brief 宿主销毁槽前向声明（定义见保护接口一节；deinit 先用到）。 */
static void xdw_hostDestroyedSlot(XObject* receiver, XVarList* args);

static void xdw_emitInt(XDockWidget* self, size_t signal, int value)
{
    XVarList* args = XVarList_Create(XVar(int, value));
    if (!args) return;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

static void xdw_emitBool(XDockWidget* self, size_t signal, bool value)
{
    XVarList* args = XVarList_Create(XVar(bool, value));
    if (!args) return;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

/**
 * @brief      判断标题条局部坐标是否命中关闭按钮。
 * @param      dock 目标停靠面板；可为 NULL。
 * @param      pos 面板局部坐标；可为 NULL。
 * @return     命中标题条右侧关闭区返回 true。
 */
static bool xdw_closeHit(const XDockWidget* dock, const XPoint* pos)
{
    int w;
    if (!dock || !pos) return false;
    w = XWidget_width((XWidget*)dock);
    return pos->y >= 0 && pos->y < XDW_TITLE_H &&
           pos->x >= w - XDW_CLOSE_BOX && pos->x < w;
}

/**
 * @brief      判断局部坐标是否落在标题条内。
 * @param      pos 面板局部坐标；可为 NULL。
 * @return     纵向位于标题条高度内返回 true。
 */
static bool xdw_titleHit(const XPoint* pos)
{
    return pos && pos->y >= 0 && pos->y < XDW_TITLE_H;
}

/**
 * @brief      广播可见性变化并联动宿主与切换动作。
 * @details    对标 Qt：QDockWidget::visibilityChanged 随真实显隐发射，
 *             同时按 QDockWidgetPrivate::syncViewAction 同步
 *             toggleViewAction 的 checked 位；显隐改变停靠区占位时经
 *             宿主回链请求主窗口重排（对标 QMainWindowLayout::update）。
 * @param      dock 目标停靠面板；可为 NULL。
 * @param      visible 最新生效可见状态。
 * @return     无返回值。
 */
static void xdw_announceVisible(XDockWidget* dock, bool visible)
{
    if (!dock) return;
    if (dock->m_toggleAction)
        XAction_setChecked(dock->m_toggleAction, visible);
    if (dock->m_announcedVisible == visible) return;
    dock->m_announcedVisible = visible;
    xdw_emitBool(dock, (size_t)XDockWidget_visibilityChanged_signal, visible);
    if (dock->m_host)
        XMainWindow_updateDockLayout((XMainWindow*)dock->m_host);
}

/**
 * @brief      把内容控件摆到标题条以下全部区域。
 * @details    对标 Qt：QDockWidget 的内容控件恒填充标题条（含分隔线，
 *             XDW_TITLE_H）以下的全部客户区，停靠/浮动/缩放三态都跟随
 *             （QDockWidgetLayout 总是把 contents 重设为标题栏下方整块）。
 *             高度不足标题条时钳位 0（与既有 setWidget 口径一致）。
 * @param      dock 目标停靠面板；可为 NULL。
 * @return     无返回值。
 */
static void xdw_layoutContent(XDockWidget* dock)
{
    XRect r;
    int h;
    if (!dock || !dock->m_widget) return;
    h = XWidget_height((XWidget*)dock);
    XRect_init(&r, 0, XDW_TITLE_H,
               XWidget_width((XWidget*)dock),
               h > XDW_TITLE_H ? h - XDW_TITLE_H : 0);
    XWidget_setGeometryRect(dock->m_widget, &r);
}

/**
 * @brief      把自定义标题条摆满标题条区（复扫 R-83 配套）。
 * @details    对标 Qt QDockWidgetLayout：自定义标题条由面板布局接管，
 *             置于标题条区呈现。本框架标题条区高度恒 XDW_TITLE_H（与
 *             内容区起点同口径），自定义条沿该区铺满宽度；控件自身已
 *             有有效高度（0<h<=XDW_TITLE_H）时尊重其高度。
 * @param      dock 目标停靠面板；可为 NULL。
 * @return     无返回值。
 */
static void xdw_layoutTitleBar(XDockWidget* dock)
{
    XRect r;
    int w;
    int h;
    if (!dock || !dock->m_titleBar) return;
    w = XWidget_width((XWidget*)dock);
    h = XWidget_height(dock->m_titleBar);
    if (h < 1 || h > XDW_TITLE_H) h = XDW_TITLE_H;
    XRect_init(&r, 0, 0, w, h);
    XWidget_setGeometryRect(dock->m_titleBar, &r);
}

/* ==================== 事件处理 ==================== */

static void VX_dockWidget_paintEvent(XWidget* self, XEvent* event)
{
    XDockWidget* dock = (XDockWidget*)self;
    XPainter painter;
    XImage* image;
    XPoint offset;
    XRect head;
    XRect line;
    uint32_t highlight;
    uint32_t windowText;
    int w;
    if (!dock || !event) return;
    w = XWidget_width(self);
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
        XColor c = XPalette_color(&palette, XPaletteColorGroup_Current,
                                  XPaletteColorRole_Highlight);
        highlight = XColor_rgba(&c);
        c = XPalette_color(&palette, XPaletteColorGroup_Current,
                           XPaletteColorRole_WindowText);
        windowText = XColor_rgba(&c);
    }
#else
    highlight = 0xFF3080C0u;
    windowText = 0xFF000000u;
#endif /* XPALETTE_ON */
#if XSTYLE_ON
    /* 复扫 R-83：自定义标题条接管呈现时，默认标题带不再由本控件绘制
     * （对标 Qt QDockWidgetLayout——自定义条即标题区的唯一呈现者），
     * 该区域交由自定义条子控件自绘。 */
    if (XStyle_defaultStyle() != NULL && !dock->m_titleBar) {
        /* Fusion/公共风格接管：标题栏走 CE_DockWidgetTitle
         * （highlight 标题条 + 标题文本 + 底部分隔线）。 */
        XStyle* style = XStyle_defaultStyle();
        XStyleOption opt;
        XStyleOption_init(&opt, XStyleCE_DockWidgetTitle);
        XRect_init(&opt.m_rect, 0, 0, w, 20);
        opt.m_state = XWidget_isEnabled((XWidget*)dock)
            ? XStyleState_Enabled : 0;
        opt.m_text = dock->m_title ? XString_toUtf8(dock->m_title) : "";
        opt.m_closable = (dock->m_features & 0x1) != 0;
        opt.m_movable = (dock->m_features & 0x2) != 0;
        opt.m_floatable = (dock->m_features & 0x4) != 0;
#if XPALETTE_ON
        opt.m_palette = XWidget_palette((XWidget*)dock);
#endif
        XStyle_drawControl(style, XStyleCE_DockWidgetTitle, &opt, &painter,
                           (XWidget*)dock);
        XPainter_deinit(&painter);
        return;
    }
#endif /* XSTYLE_ON */
    if (dock->m_titleBar) {
        /* 自定义标题条接管：跳过默认标题带/分隔线绘制。 */
        XPainter_deinit(&painter);
        return;
    }
    XRect_init(&head, 0, 0, w, 20);
    XPainter_fillRect(&painter, &head, highlight);
    XPainter_drawText(&painter, 6, 14,
                      dock->m_title ? XString_toUtf8(dock->m_title) : "",
                      windowText);
    if (dock->m_features & 0x1 /* Closable：绘制关闭标记（对标 Qt 标题条） */) {
        XPainter_drawText(&painter, w - XDW_CLOSE_BOX + 4, 14, "×",
                          windowText);
    }
    XRect_init(&line, 0, 20, w, 1);
    XPainter_fillRect(&painter, &line, windowText);
    XPainter_deinit(&painter);
}

/**
 * @brief      鼠标按下：浮动窗口标题栏的关闭/拖动命中（对标 Qt 浮动
 *             QDockWidget 标题条交互）。
 * @details    关闭按钮命中且 Closable 特性开启时隐藏面板（任务裁定：
 *             close 槽回归停靠区语义简化为隐藏，面板保持登记可复显）；
 *             标题条命中且 Movable 特性开启时开始拖动（记录抓取偏移并
 *             提升窗口）。TODO：拖拽重停靠（拖回主窗/拖到其它停靠区）
 *             未实现，停靠态标题条按下暂不启动拖动。
 * @param      self 目标控件。
 * @param      event 鼠标按下事件。
 * @return     无返回值。
 */
static void VX_dockWidget_mousePressEvent(XWidget* self, XEvent* event)
{
    XDockWidget* dock = (XDockWidget*)self;
    XMouseEvent* me;
    XPoint pos;
    if (!dock || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_PRESS) {
        xdw_callParent(self, MousePressEvent, event);
        return;
    }
    me = (XMouseEvent*)event;
    pos = XMouseEvent_position(me);
    /* 复扫 R-83：自定义标题条接管标题区交互——关闭钮/拖动属默认标题
     * 条行为；自定义条在位时按下事件归其子控件（子 ignore 才落到本
     * 处理），此处一律不再按默认条命中处理。 */
    if (!dock->m_titleBar && xdw_closeHit(dock, &pos) &&
        (dock->m_features & 0x1)) {
        /* 对标 Qt：标题条关闭按钮关闭面板；简化为隐藏（保持登记）。 */
        XWidget_setVisible(self, false);
        XEvent_accept(event);
        return;
    }
    if (!dock->m_titleBar && dock->m_floating && xdw_titleHit(&pos) &&
        (dock->m_features & 0x2 /* Movable：标题条可拖动 */) &&
        XMouseEvent_button(me) == XMouseButton_LeftButton) {
        XPoint g = XMouseEvent_globalPosition(me);
        dock->m_dragging = true;
        dock->m_dragOffset.x = g.x - XWidget_x(self);
        dock->m_dragOffset.y = g.y - XWidget_y(self);
        XWidget_raise(self); /* 对标 Qt：拖动前激活提升浮动窗口 */
        XEvent_accept(event);
        return;
    }
    xdw_callParent(self, MousePressEvent, event);
}

/**
 * @brief      鼠标移动：拖动中的浮动窗口跟随全局坐标平移（对标 Qt 拖动
 *             浮动 QDockWidget 移动顶层窗口）。
 * @param      self 目标控件。
 * @param      event 鼠标移动事件。
 * @return     无返回值。
 */
static void VX_dockWidget_mouseMoveEvent(XWidget* self, XEvent* event)
{
    XDockWidget* dock = (XDockWidget*)self;
    XMouseEvent* me;
    XPoint g;
    if (!dock || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_MOVE) {
        xdw_callParent(self, MouseMoveEvent, event);
        return;
    }
    if (dock->m_floating && dock->m_dragging) {
        me = (XMouseEvent*)event;
        if (!(XMouseEvent_buttons(me) & XMouseButton_LeftButton)) {
            dock->m_dragging = false; /* 按键已释放：安全终止拖动 */
            xdw_callParent(self, MouseMoveEvent, event);
            return;
        }
        g = XMouseEvent_globalPosition(me);
        XWidget_move(self, g.x - dock->m_dragOffset.x,
                     g.y - dock->m_dragOffset.y);
        XEvent_accept(event);
        return;
    }
    xdw_callParent(self, MouseMoveEvent, event);
}

/**
 * @brief      鼠标释放：结束标题栏拖动。
 * @param      self 目标控件。
 * @param      event 鼠标释放事件。
 * @return     无返回值。
 */
static void VX_dockWidget_mouseReleaseEvent(XWidget* self, XEvent* event)
{
    XDockWidget* dock = (XDockWidget*)self;
    if (dock && event &&
        XEvent_type(event) == XEVENT_TYPE_MOUSE_BUTTON_RELEASE &&
        dock->m_dragging) {
        dock->m_dragging = false;
        XEvent_accept(event);
        return;
    }
    xdw_callParent(self, MouseReleaseEvent, event);
}

/** @brief 显示事件：转发父类后广播 visibilityChanged(true)。 */
static void VX_dockWidget_showEvent(XWidget* self, XEvent* event)
{
    xdw_callParent(self, ShowEvent, event);
    if (self)
        xdw_announceVisible((XDockWidget*)self, XWidget_isVisible(self));
}

/** @brief 隐藏事件：转发父类后广播 visibilityChanged(false)。 */
static void VX_dockWidget_hideEvent(XWidget* self, XEvent* event)
{
    xdw_callParent(self, HideEvent, event);
    if (self)
        xdw_announceVisible((XDockWidget*)self, false);
}

/**
 * @brief      尺寸变更事件：内容控件重摆到标题条以下全部区域。
 * @details    对标 Qt：QDockWidget 内容恒随面板尺寸跟随（停靠态主窗口
 *             重排、浮动态顶层缩放、回归停靠三态的几何变化都经
 *             ResizeEvent 到达），修复此前 setWidget 仅一次性摆位、
 *             之后内容矩形不随面板更新的缺陷。
 * @param      self 目标控件。
 * @param      event 尺寸变更事件。
 * @return     无返回值。
 */
static void VX_dockWidget_resizeEvent(XWidget* self, XEvent* event)
{
    xdw_callParent(self, ResizeEvent, event);
    if (self) {
        xdw_layoutTitleBar((XDockWidget*)self);
        xdw_layoutContent((XDockWidget*)self);
    }
}

/* ==================== 生命周期与虚表 ==================== */

static void VX_dockWidget_deinit(XDockWidget* self)
{
    if (!self) return;
    if (self->m_widget) {
        XWidget_delete_base(self->m_widget);
        self->m_widget = NULL;
    }
    if (self->m_title) {
        XString_delete_base(self->m_title);
        self->m_title = NULL;
    }
    if (self->m_toggleAction) {
        /* 切换动作归面板所有（对标 Qt toggleViewAction 归 dock 所有）。 */
        XAction_delete_base(self->m_toggleAction);
        self->m_toggleAction = NULL;
    }
    if (self->m_titleBar) {
        /* 复扫 R-83 配套：自定义标题条为借用承载——析构级联删子前先
         * 摘除父链，控件归还调用方（面板不删除，见 setter 注）。 */
        if (XWidget_parentWidget(self->m_titleBar) == (XWidget*)self)
            XWidget_setParent(self->m_titleBar, NULL, 0);
        self->m_titleBar = NULL;
    }
    if (self->m_host) {
        /* 析构时摘除宿主销毁监听（setHost 建立的回链）。 */
        XObject_disconnect_1((XObject*)self->m_host,
                             XSignal(XObject_destroyed_signal),
                             (XObject*)self, xdw_hostDestroyedSlot);
    }
    self->m_host = NULL;
    XClass_Deinit_Parent(XWidget, (XWidget*)self);
}

XVtable* XDockWidget_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XDockWidget)
    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VX_dockWidget_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MousePressEvent,
                             VX_dockWidget_mousePressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseMoveEvent,
                             VX_dockWidget_mouseMoveEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseReleaseEvent,
                             VX_dockWidget_mouseReleaseEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ResizeEvent, VX_dockWidget_resizeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ShowEvent, VX_dockWidget_showEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_HideEvent, VX_dockWidget_hideEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VX_dockWidget_deinit);
    return XVTABLE_DEFAULT;
}

void XDockWidget_init(XDockWidget* self, const char* utf8Title,
                      XWidget* parent, XWidgetFlags flags)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XWidget_init(&self->m_base, parent, flags);
    XClassSetVtable(self, XDockWidget);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    self->m_title = XString_create_utf8(utf8Title ? utf8Title : "");
    self->m_features = 0x1 | 0x2 | 0x4; /* Closable|Movable|Floatable */
    self->m_allowedAreas = (int)XDockWidgetArea_All;
    self->m_floating = false;
}

XDockWidget* XDockWidget_create_ex(XMemoryType memory,
                                   const char* utf8Title,
                                   XWidget* parent, XWidgetFlags flags)
{
    XDockWidget* self =
        (XDockWidget*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XDockWidget_init(self, utf8Title, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 公共 API ==================== */

void XDockWidget_setWidget(XDockWidget* self, XWidget* widget)
{
    if (!self || self->m_widget == widget) return;
    if (self->m_widget) {
        /* 对标 Qt setWidget 的替换语义：已有内容时先摘除旧控件（转独立
         * 顶层即脱离本面板父链），所有权转移给调用方、由调用方决定释放
         * （Qt 中旧 widget 脱离 dock 后归调用方管理，dock 不再删除）。 */
        XWidget_setParent(self->m_widget, NULL, 0);
    }
    self->m_widget = widget;
    if (widget)
        XWidget_setParent(widget, (XWidget*)self, 0);
    /* 对标 Qt：新内容立即摆到标题条以下全部区域。 */
    xdw_layoutContent(self);
}

XWidget* XDockWidget_widget(const XDockWidget* self)
{
    return self ? self->m_widget : NULL;
}

void XDockWidget_setFeatures(XDockWidget* self, int features)
{
    if (!self || self->m_features == features) return;
    self->m_features = features;
    xdw_emitInt(self, (size_t)XDockWidget_featuresChanged_signal,
                features);
}

int XDockWidget_features(const XDockWidget* self)
{
    return self ? self->m_features : 0;
}

void XDockWidget_setFloating(XDockWidget* self, bool floating)
{
    XWidget* selfw;
    XWidget* host;
    XPoint origin;
    XPoint globalPos;
    int w;
    int h;
    bool wasVisible;
    if (!self || self->m_floating == floating) return;
    selfw = (XWidget*)self;
    host = self->m_host;
    /* 记录当前几何与全局位置（对标 Qt：浮动时窗口保持屏幕位置尺寸）。
     * 必须在重设父对象前完成，子控件坐标经父链映射才有意义。 */
    XPoint_init(&origin, 0, 0);
    globalPos = XWidget_mapToGlobal(selfw, &origin);
    w = XWidget_width(selfw);
    h = XWidget_height(selfw);
    wasVisible = XWidget_isVisible(selfw);
    self->m_floating = floating;
    self->m_dragging = false;
    if (floating) {
        /* 对标 Qt：setFloating(true) 脱离主窗布局，转成独立顶层窗口
         * （Qt::Window），保留尺寸并映射到原全局位置。 */
        XWidget_setParent(selfw, NULL,
                          (XWidgetFlags)XWindowType_Window);
        XWidget_setWindowTitle(selfw, self->m_title);
        if (w <= 0) w = 200; /* 无宿主几何时的兜底尺寸 */
        if (h <= 0) h = 150;
        XWidget_resize(selfw, w, h);
        XWidget_move(selfw, globalPos.x, globalPos.y);
        if (wasVisible) {
            XWidget_show(selfw);
            XWidget_raise(selfw);
            XWidget_activateWindow(selfw); /* 对标 Qt：浮动窗获得焦点 */
        }
    } else if (host) {
        /* 对标 Qt：setFloating(false) 回归停靠区，重新挂回宿主主窗口，
         * 几何交还主窗口停靠布局（随后统一重排）。 */
        XWidget_setParent(selfw, host, 0);
        if (wasVisible) XWidget_show(selfw);
    }
    if (host)
        XMainWindow_updateDockLayout((XMainWindow*)host);
    xdw_emitBool(self, (size_t)XDockWidget_topLevelChanged_signal,
                 floating);
}

bool XDockWidget_isFloating(const XDockWidget* self)
{
    return self ? self->m_floating : false;
}

void XDockWidget_setAllowedAreas(XDockWidget* self, int areas)
{
    if (!self || self->m_allowedAreas == areas) return;
    self->m_allowedAreas = areas;
    xdw_emitInt(self, (size_t)XDockWidget_allowedAreasChanged_signal,
                areas);
}

int XDockWidget_allowedAreas(const XDockWidget* self)
{
    return self ? self->m_allowedAreas : 0;
}

bool XDockWidget_isAreaAllowed(const XDockWidget* self, int area)
{
    return self ? (self->m_allowedAreas & area) != 0 : false;
}

void XDockWidget_setTitleBarWidget(XDockWidget* self, XWidget* widget)
{
    XWidget* old;
    if (!self || self->m_titleBar == widget) return;
    old = self->m_titleBar;
    if (old) {
        /* 借用承载（头文件契约 m_titleBar 为借用）：旧自定义标题条
         * 不删除，若曾挂到本面板则摘父链归还调用方（同 setWidget 的
         * 摘除语义）。 */
        XWidget_setVisible(old, false);
        if (XWidget_parentWidget(old) == (XWidget*)self)
            XWidget_setParent(old, NULL, 0);
    }
    self->m_titleBar = widget;
    if (widget) {
        /* 复扫 R-83：此前纯存储——自定义标题条不 reparent/不入标题条
         * 区布局/paint 不消费，永不可见。现对标 Qt QDockWidget::
         * setTitleBarWidget 的接管呈现：挂为本面板子控件、铺满标题条
         * 区、显式 show（框架显式 show 语义）；绘制分流与标题区鼠标
         * 门禁见 paintEvent/mousePressEvent。传 NULL 恢复默认标题条。 */
        XWidget_setParent(widget, (XWidget*)self, 0);
        xdw_layoutTitleBar(self);
        XWidget_show(widget);
    }
    XWidget_update((XWidget*)self);
}

XWidget* XDockWidget_titleBarWidget(const XDockWidget* self)
{
    return self ? self->m_titleBar : NULL;
}

/** @brief toggleViewAction 槽：翻转面板可见性（对标 QDockWidget
 *         toggleViewAction 的 triggered→setVisible 桥接）。 */
static void xdw_toggleViewSlot(XObject* receiver, XVarList* args)
{
    XDockWidget* dock = (XDockWidget*)receiver;
    (void)args;
    if (!dock) return;
    XWidget_setVisible((XWidget*)dock, !XWidget_isVisible((XWidget*)dock));
}

XAction* XDockWidget_toggleViewAction(XDockWidget* self)
{
#if XACTION_ON
    XAction* action;
    if (!self) return NULL;
    if (self->m_toggleAction) return self->m_toggleAction;
    action = XAction_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL, NULL);
    if (!action) return NULL;
    self->m_toggleAction = action;
    XAction_setCheckable(action, true); /* 对标 Qt：切换动作可选中 */
    XAction_setText_2(action,
                      self->m_title ? XString_toUtf8(self->m_title) : "");
    XAction_setChecked(action, XWidget_isVisible((XWidget*)self));
    XObject_connect_1((XObject*)action, XSignal(XAction_triggered_signal),
                      (XObject*)self, xdw_toggleViewSlot,
                      XConnectionType_Direct);
    return self->m_toggleAction;
#else
    /* XACTION_ON 裁剪时保留遗留回退（无动作子系统）。 */
    (void)self;
    return NULL;
#endif /* XACTION_ON */
}

/* ==================== 保护接口（XDockWidget_Protected.h） ==================== */

/** @brief 宿主销毁槽：宿主主窗口释放时摘除回链（对标 Qt 的 QPointer
 *         防悬空语义；浮动面板已脱离宿主控件树，必须显式摘链）。 */
static void xdw_hostDestroyedSlot(XObject* receiver, XVarList* args)
{
    XDockWidget* dock = (XDockWidget*)receiver;
    (void)args;
    if (dock) dock->m_host = NULL;
}

void XDockWidget_setHost(XDockWidget* self, XWidget* host)
{
    if (!self) return;
    if (self->m_host) {
        XObject_disconnect_1((XObject*)self->m_host,
                             XSignal(XObject_destroyed_signal),
                             (XObject*)self, xdw_hostDestroyedSlot);
    }
    self->m_host = host;
    if (host) {
        XObject_connect_1((XObject*)host,
                          XSignal(XObject_destroyed_signal),
                          (XObject*)self, xdw_hostDestroyedSlot,
                          XConnectionType_Direct);
    }
}

XWidget* XDockWidget_host(const XDockWidget* self)
{
    return self ? self->m_host : NULL;
}

/* ==================== 信号 ==================== */

void* XDockWidget_featuresChanged_signal(XDockWidget* self, int features)
{
    (void)self; (void)features;
    return (void*)(size_t)XDockWidget_featuresChanged_signal;
}

void* XDockWidget_topLevelChanged_signal(XDockWidget* self, bool topLevel)
{
    (void)self; (void)topLevel;
    return (void*)(size_t)XDockWidget_topLevelChanged_signal;
}

void* XDockWidget_allowedAreasChanged_signal(XDockWidget* self, int areas)
{
    (void)self; (void)areas;
    return (void*)(size_t)XDockWidget_allowedAreasChanged_signal;
}

void* XDockWidget_visibilityChanged_signal(XDockWidget* self, bool visible)
{
    (void)self; (void)visible;
    return (void*)(size_t)XDockWidget_visibilityChanged_signal;
}
void* XDockWidget_dockLocationChanged_signal(XDockWidget* self, int area)
{
    (void)self; (void)area;
    return (void*)(size_t)XDockWidget_dockLocationChanged_signal;
}














#endif /* XWIDGET_ON && XDOCKWIDGET_ON */