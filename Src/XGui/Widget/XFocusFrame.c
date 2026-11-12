/**
 * @file       XFocusFrame.c
 * @brief      焦点指示框控件实现（对标 Qt 6.8 QFocusFrame 全部公共 API）。
 * @details    环绕目标控件绘制焦点指示框：setWidget 后框体挂到目标的
 *             父控件上（对标 d->frameParent = widget->parentWidget() 的
 *             非 frameAbove 路径），并按样式 PM_FocusFrameHMargin/VMargin
 *             外扩贴合；随后以 installEventFilter(目标) 实现跟随——
 *             与 Qt 6.8 QFocusFrame::eventFilter（qfocusframe.cpp:243-300）
 *             逐事件对位：
 *               Move/Resize      → updateSize（只贴合几何）
 *               Hide/StyleChange → hide
 *               ParentChange     → update（重挂父级 + 贴合 + 显隐）
 *               Show             → update + show
 *               PaletteChange    → setPalette(目标调色板)
 *               ZOrderChange     → raise
 *               Destroy          → setWidget(NULL)（本框架用 destroyed 信号）
 *             XWidget_sendEvent 已按 Qt QCoreApplication::notify 口径先跑
 *             事件过滤器，框架内部派发的 Move/Resize/Show/Hide/
 *             ParentChange 过滤器都能收到（此前注释所称「内部几何事件
 *             不经分发」已不成立，轮询定时器随之删除）。
 * @author     XinYueC 团队
 */

#if XGUI_ON

#include "XFocusFrame.h"
#include "XStyle.h"
#include "XStyleOption.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XPainter.h"
#include "XGuiConfig.h"

#include "XAlgorithm.h"
#include "XWidget_Protected.h"

#if XWIDGET_ON && XFOCUSFRAME_ON

/** @brief 焦点框外扩边距（对标 Qt 样式 PM_FocusFrameVMargin/HMargin；
 *         XCommonStyle/XFusionStyle 均返回 2，保留作无样式时的兜底）。 */
#define XFOCUSFRAME_MARGIN 2

/* ==================== 内部辅助 ==================== */

/** @brief 取样式焦点框边距（qfocusframe.cpp:37-58 updateSize 原文：
 *         `style()->pixelMetric(PM_FocusFrameVMargin/HMargin, &opt, q)`）。 */
static void xfocusframe_margins(const XFocusFrame* self, int* hmargin,
                                int* vmargin)
{
    int h = XFOCUSFRAME_MARGIN;
    int v = XFOCUSFRAME_MARGIN;
#if XSTYLE_ON
    if (self && XWidget_style((const XWidget*)self) != NULL) {
        XStyle* style = XWidget_style((const XWidget*)self);
        int m;
        m = XStyle_pixelMetric(style, XStylePM_FocusFrameHMargin, NULL);
        if (m >= 0) h = m;
        m = XStyle_pixelMetric(style, XStylePM_FocusFrameVMargin, NULL);
        if (m >= 0) v = m;
    }
#else
    (void)self;
#endif /* XSTYLE_ON */
    if (hmargin) *hmargin = h;
    if (vmargin) *vmargin = v;
}

/** @brief 按目标当前几何贴合（对标 QFocusFramePrivate::updateSize，
 *         qfocusframe.cpp:60-81：geom = widget 几何外扩样式边距）。 */
static void xfocusframe_updateSize(XFocusFrame* self)
{
    XRect geom;
    XRect cur;
    int hmargin;
    int vmargin;
    if (!self || !self->m_widget) return;
    xfocusframe_margins(self, &hmargin, &vmargin);
    XRect_init(&geom,
               XWidget_x(self->m_widget) - hmargin,
               XWidget_y(self->m_widget) - vmargin,
               XWidget_width(self->m_widget) + 2 * hmargin,
               XWidget_height(self->m_widget) + 2 * vmargin);
    if (geom.width < 1) geom.width = 1;
    if (geom.height < 1) geom.height = 1;
    /* 几何不变即早退（qfocusframe.cpp:76-78 `if (q->geometry() == geom)
     * return;`）——Move/Resize 事件里本函数会被反复调用，同值
     * setGeometry 会白白走一遍边界/事件流程。 */
    cur = XWidget_geometry((const XWidget*)self);
    if (cur.x == geom.x && cur.y == geom.y &&
        cur.width == geom.width && cur.height == geom.height)
        return;
    XWidget_setGeometryRect((XWidget*)self, &geom);
}

/** @brief 重挂父级并按目标几何/显隐同步（对标 QFocusFramePrivate::
 *         update，qfocusframe.cpp:31-46：setParent(frameParent) +
 *         updateSize + show/stackUnder）。 */
static void xfocusframe_update(XFocusFrame* self)
{
    XWidget* frameParent;
    bool showFrame;
    if (!self) return;
    if (!self->m_widget) {
        XWidget_hide((XWidget*)self);
        return;
    }
    if (XWidget_parentWidget((XWidget*)self) !=
        XWidget_parentWidget(self->m_widget))
        XWidget_setParent((XWidget*)self,
                          XWidget_parentWidget(self->m_widget), 0);
    xfocusframe_updateSize(self);
    frameParent = XWidget_parentWidget((XWidget*)self);
    /* 显隐判据对齐 Qt（qfocusframe.cpp:36-44）：目标未隐藏**且**框体与父
     * 控件矩形相交才显示，否则隐藏——目标被滚出视口/挪到父控件外时，
     * Qt 收框，本实现此前只按目标显隐位判定，框体留在父控件外仍被画出。 */
    showFrame = !XWidget_isHidden(self->m_widget);
    if (showFrame && frameParent) {
        XRect parentRect = XWidget_rect(frameParent);
        XRect frameGeom = XWidget_geometry((const XWidget*)self);
        showFrame = XRect_intersects(&parentRect, &frameGeom) != 0;
    }
    if (showFrame)
        XWidget_show((XWidget*)self); /* 框浮于目标上方（对标 raise）。 */
    else
        XWidget_hide((XWidget*)self);
    XWidget_update((XWidget*)self);
}

/** @brief 在目标上安装/摘除事件过滤器（对标 setWidget 的
 *         installEventFilter/removeEventFilter，qfocusframe.cpp:174-205）。 */
static void xfocusframe_setFilter(XFocusFrame* self, XWidget* widget,
                                  bool install)
{
    if (!self || !widget) return;
    if (install)
        XObject_installEventFilter((XObject*)widget, (XObject*)self);
    else
        XObject_removeEventFilter((XObject*)widget, (XObject*)self);
}

/* ==================== 虚槽实现 ==================== */

/** @brief 目标事件过滤器（对标 QFocusFrame::eventFilter，
 *         qfocusframe.cpp:243-300 逐事件对位）。
 * @return     恒 false（不吞事件；Qt 原文 `return false;`）。 */
static bool VXFocusFrame_eventFilter(XObject* object, XObject* watched,
                                     XEvent* event)
{
    XFocusFrame* ff = (XFocusFrame*)object;
    if (!ff || !watched || !event) return false;
    if ((const XWidget*)watched != ff->m_widget) return false;
    switch (XEvent_type(event)) {
    case XEVENT_TYPE_MOVE:
    case XEVENT_TYPE_RESIZE:
        xfocusframe_updateSize(ff);
        break;
    case XEVENT_TYPE_HIDE:
    case XEVENT_TYPE_STYLE_CHANGE:
        /* qfocusframe.cpp:254-256：目标隐藏或换样式时收框（样式切换可能
         * 改变焦点框外观，Qt 选择先隐藏、等下次 Show 重算）。 */
        XWidget_hide((XWidget*)ff);
        break;
    case XEVENT_TYPE_SHOW:
        xfocusframe_update(ff);
        XWidget_show((XWidget*)ff);
        break;
    case XEVENT_TYPE_PARENT_CHANGE:
        /* qfocusframe.cpp:257-265：目标换父 ⇒ 框体重挂到新父并重新贴合。
         * （showFrameAboveWidget 分支会先 setWidget(NULL)/setWidget(w)；
         * 本框架走非 above 路径，update() 等价。） */
        xfocusframe_update(ff);
        break;
    case XEVENT_TYPE_PALETTE_CHANGE:
        /* qfocusframe.cpp:271-273：`setPalette(d->widget->palette())`。 */
        {
            XPalette palette = XWidget_palette(ff->m_widget);
            XWidget_setPalette((XWidget*)ff, &palette);
        }
        break;
    case XEVENT_TYPE_Z_ORDER_CHANGE:
        /* qfocusframe.cpp:275-279：框体始终压在目标之上。 */
        XWidget_raise((XWidget*)ff);
        break;
    default:
        break;
    }
    return false;
}

/** @brief 框体自身被移动/缩放时重新贴合目标（对标 QFocusFrame::event，
 *         qfocusframe.cpp:302-322：跟踪期间不允许外部把框挪走）。
 * @details 外部批量移动子件（如 XAbstractScrollArea 平移内容）会把框体
 *          一起挪走；Qt 在收到自身的 Move/Resize 时调 updateSize 拉回。 */
static void VXFocusFrame_moveEvent(XWidget* self, XEvent* event)
{
    XFocusFrame* ff = (XFocusFrame*)self;
    if (ff && ff->m_widget)
        xfocusframe_updateSize(ff);
    XClass_Parent(XWidget, EXWidget_MoveEvent,
                  void (*)(XWidget*, XEvent*))(self, event);
}

static void VXFocusFrame_resizeEvent(XWidget* self, XEvent* event)
{
    XFocusFrame* ff = (XFocusFrame*)self;
    if (ff && ff->m_widget)
        xfocusframe_updateSize(ff);
    XClass_Parent(XWidget, EXWidget_ResizeEvent,
                  void (*)(XWidget*, XEvent*))(self, event);
}

/** @brief 目标销毁槽：解除关联并隐藏（对标 eventFilter 的 Destroy →
 *  setWidget(nullptr)；destroyed 在析构帧内同步发射，先于内存归还）。 */
static void xfocusframe_widgetDestroyedSlot(XObject* receiver,
                                            XVarList* args)
{
    XFocusFrame* ff = (XFocusFrame*)receiver;
    (void)args;
    if (!ff) return;
    /* 过滤器随目标析构自动摘除（XObject 析构清过滤器表），此处只需
     * 清借用指针并按 Qt 收框。 */
    ff->m_widget = NULL;
    XWidget_hide((XWidget*)ff);
}

static void VXFocusFrame_paintEvent(XWidget* self, XEvent* event)
{
    XFocusFrame* ff = (XFocusFrame*)self;
    XPainter painter;
    XImage* image;
    XPoint offset;
    XRect r;
    uint32_t highlight;
    int w;
    int h;
    if (!ff || !event) return;
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
        XColor c = XPalette_color(&palette, XPaletteColorGroup_Current,
                                  XPaletteColorRole_Highlight);
        highlight = XColor_rgba(&c);
    }
#else
    highlight = 0xFF3080C0u;
#endif /* XPALETTE_ON */
#if XSTYLE_ON
    if (XWidget_style((const XWidget*)self) != NULL) {
        /* Fusion/公共风格接管：焦点框走 PE_FrameFocusRect。 */
        XStyle* style = XWidget_style((const XWidget*)self);
        XStyleOption opt;
        XStyleOption_init(&opt, XStylePE_FrameFocusRect);
        XRect_init(&opt.m_rect, 0, 0, w, h);
        opt.m_state = XStyleState_Enabled | XStyleState_HasFocus;
#if XPALETTE_ON
        opt.m_palette = XWidget_palette(self);
#endif
        XStyle_drawPrimitive(style, XStylePE_FrameFocusRect, &opt,
                             &painter, self);
        XPainter_deinit(&painter);
        return;
    }
#endif /* XSTYLE_ON */
    XRect_init(&r, 0, 0, w, 2);
    XPainter_fillRect(&painter, &r, highlight);
    XRect_init(&r, 0, h - 2, w, 2);
    XPainter_fillRect(&painter, &r, highlight);
    XRect_init(&r, 0, 0, 2, h);
    XPainter_fillRect(&painter, &r, highlight);
    XRect_init(&r, w - 2, 0, 2, h);
    XPainter_fillRect(&painter, &r, highlight);
    XPainter_deinit(&painter);
}

static void VXFocusFrame_deinit(XFocusFrame* self)
{
    if (!self) return;
    if (self->m_widget) {
        xfocusframe_setFilter(self, self->m_widget, false);
        XObject_disconnect((XObject*)self->m_widget,
                             XSignal(XObject_destroyed_signal),
                             (XObject*)self,
                             xfocusframe_widgetDestroyedSlot);
        self->m_widget = NULL;
    }
    XClass_Deinit_Parent(XWidget, (XWidget*)self);
}

XVtable* XFocusFrame_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XFocusFrame)
    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VXFocusFrame_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MoveEvent, VXFocusFrame_moveEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ResizeEvent, VXFocusFrame_resizeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_EventFilter, VXFocusFrame_eventFilter);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXFocusFrame_deinit);
    return XVTABLE_DEFAULT;
}

void XFocusFrame_init(XFocusFrame* self, XWidget* parent, XWidgetFlags flags)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XWidget_init(&self->m_base, parent, flags);
    XClassSetVtable(self, XFocusFrame);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    /* 对标 QFocusFrame 构造（qfocusframe.cpp:124-129）：鼠标穿透 +
     * NoFocus——框体贴合目标后叠于其上，不得截获目标的点击/焦点。 */
    XWidget_setAttribute((XWidget*)self,
                         XWidgetAttribute_TransparentForMouseEvents, true);
    XWidget_setFocusPolicy((XWidget*)self, XWidgetFocusPolicy_NoFocus);
}

XFocusFrame* XFocusFrame_create_ex(XMemoryType memory, XWidget* parent,
                                   XWidgetFlags flags)
{
    XFocusFrame* self = (XFocusFrame*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XFocusFrame_init(self, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

void XFocusFrame_setWidget(XFocusFrame* self, XWidget* widget)
{
    if (!self) return;
    if (widget == self->m_widget) return;
    if (self->m_widget) {
        xfocusframe_setFilter(self, self->m_widget, false);
        XObject_disconnect((XObject*)self->m_widget,
                             XSignal(XObject_destroyed_signal),
                             (XObject*)self,
                             xfocusframe_widgetDestroyedSlot);
    }
    self->m_widget = NULL;
    /* 对标 qfocusframe.cpp:174-205 的跟踪条件：
     *   widget && !widget->isWindow() &&
     *   widget->parentWidget()->windowType() != Qt::SubWindow
     * 目标为顶层窗口或 MDI 子窗口时置空并隐藏（Qt 同分支）。 */
    if (widget && !XWidget_isWindow(widget) &&
        XWidget_parentWidget(widget) != NULL &&
        XWidget_windowType(XWidget_parentWidget(widget)) !=
            XWindowType_SubWindow) {
        self->m_widget = widget;
        /* 先装过滤器再 update：update 内的 setParent/show 也会产生事件。 */
        xfocusframe_setFilter(self, widget, true);
        XObject_connect((XObject*)widget,
                          XSignal(XObject_destroyed_signal),
                          (XObject*)self,
                          xfocusframe_widgetDestroyedSlot,
                          XConnectionType_Direct);
        xfocusframe_update(self);
    } else {
        XWidget_hide((XWidget*)self);
    }
    XWidget_update((XWidget*)self);
}

XWidget* XFocusFrame_widget(const XFocusFrame* self)
{
    return self ? self->m_widget : NULL;
}

#endif /* XWIDGET_ON && XFOCUSFRAME_ON */

#endif /* XGUI_ON */
