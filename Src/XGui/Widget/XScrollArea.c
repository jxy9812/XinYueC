/**
 * @file       XScrollArea.c
 * @brief      滚动区域控件实现（对标 Qt 6.8 QScrollArea 全部公共 API）。
 * @details    与同名头文件的公共 API 一一对应；内部实现细节见
 *             头文件 @note 与函数级 Doxygen 注释。
 * @author     XinYueC 团队
 */

#include "XScrollArea.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XVarList.h"
#include "XGuiConfig.h"

#include "XAlgorithm.h"
#include "XAlignment.h"
#include "XWidget_Protected.h"

#if XWIDGET_ON && XFRAME_ON && XSCROLLBAR_ON && XABSTRACTSCROLLAREA_ON && XSCROLLAREA_ON

/* ==================== 内部工具 ==================== */

/** @brief 依据 resizable/alignment 调整内容控件几何。 */
static void xsa_updateWidgetGeometry(XScrollArea* self)
{
    XWidget* viewport;
    XWidget* content;
    int vw;
    int vh;
    int cw;
    int ch;
    int x = 0;
    int y = 0;
    if (!self || !self->m_widget) return;
    viewport = XAbstractScrollArea_viewport(self);
    if (!viewport) return;
    vw = XWidget_width(viewport);
    vh = XWidget_height(viewport);
    content = self->m_widget;
    if (self->m_resizable) {
        XRect r;
        XRect_init(&r, 0, 0, vw > 0 ? vw : 0, vh > 0 ? vh : 0);
        XWidget_setGeometryRect(content, &r);
        return;
    }
    cw = XWidget_width(content);
    ch = XWidget_height(content);
    if (self->m_alignment & (int)XAlignment_Right)
        x = cw > vw ? 0 : vw - cw;
    else if (self->m_alignment & (int)XAlignment_HCenter)
        x = (vw - cw) / 2;
    if (self->m_alignment & (int)XAlignment_Bottom)
        y = ch > vh ? 0 : vh - ch;
    else if (self->m_alignment & (int)XAlignment_VCenter)
        y = (vh - ch) / 2;
    {
        XRect r;
        XRect_init(&r, x, y, cw, ch);
        XWidget_setGeometryRect(content, &r);
    }
    /* 内容尺寸驱动滚动范围（负值=无水平滚动需求）。 */
    XAbstractScrollArea_setContentSize(
        (XAbstractScrollArea*)self,
        cw > vw ? cw : vw, ch > vh ? ch : vh);
}

/* ==================== 虚表与生命周期 ==================== */

/** @brief scrollContentsBy：按滚动条值平移内容控件（第一版简化：
 *         直接按绝对值定位；平移后刷新视口暴露区域）。 */
static void VX_scrollArea_scrollContentsBy(XAbstractScrollArea* self,
                                           int dx, int dy)
{
    XScrollArea* area = (XScrollArea*)self;
    XScrollBar* vsb;
    XScrollBar* hsb;
    XWidget* viewport;
    int vx = 0;
    int vy = 0;
    (void)dx;
    (void)dy;
    if (!area || !area->m_widget) return;
    vsb = XAbstractScrollArea_verticalScrollBar(self);
    hsb = XAbstractScrollArea_horizontalScrollBar(self);
    if (vsb) vy = -XScrollBar_value(vsb);
    if (hsb) vx = -XScrollBar_value(hsb);
    {
        XRect r;
        int cw = XWidget_width(area->m_widget);
        int ch = XWidget_height(area->m_widget);
        XRect_init(&r, vx, vy, cw, ch);
        XWidget_setGeometryRect(area->m_widget, &r);
    }
    /* 暴露区域重绘：内容平移后刷新视口。 */
    viewport = XAbstractScrollArea_viewport(self);
    if (viewport) XWidget_update(viewport);
}

static void VX_scrollArea_deinit(XScrollArea* self)
{
    if (!self) return;
    XClass_Deinit_Parent(XAbstractScrollArea,
                         (XAbstractScrollArea*)self);
}

/** @brief resizeEvent：先由基类完成视口/滚动条重排，再重排版内容。
 *  对标 QScrollArea::resizeEvent（qscrollarea.cpp:313-316 →
 *  updateScrollBars → updateWidgetPosition）：容器尺寸变化（如页签
 *  容器拉伸页面）后内容对齐位与滚动范围必须重算——此前缺失该重载，
 *  对齐/内容尺寸停留在旧视口尺寸上（页签4 内容呈现异常的组成部分）。 */
static void VX_scrollArea_resizeEvent(XWidget* self, XEvent* event)
{
    if (!self) return;
    XClass_Parent(XAbstractScrollArea, EXWidget_ResizeEvent,
                  XWidgetEventSlot)(self, event);
    xsa_updateWidgetGeometry((XScrollArea*)self);
}

/** @brief showEvent：以当前实际尺寸重排视口/滚动条与内容几何。
 *  对标 Qt「show 时补排版」语义（WA_PendingResizeEvent 在 show 统一
 *  刷新 + QScrollArea::eventFilter 对内容 Resize 的 updateScrollBars
 *  联动，qscrollarea.cpp:328）：构造期容器尚未显示，页签容器此后的
 *  拉伸虽同步派发 resize，但视口/内容的中间态几何（负宽夹 0 等）不
 *  会自发重算——显示瞬间按最终几何再断言一次，消除隐藏期遗留的
 *  陈旧视口尺寸（页签4 六行文本被裁成 ~1px 残条的收口）。 */
static void VX_scrollArea_showEvent(XWidget* self, XEvent* event)
{
    if (!self) return;
    XClass_Parent(XWidget, EXWidget_ShowEvent,
                  XWidgetEventSlot)(self, event);
    VX_scrollArea_resizeEvent(self, event);
}

/** @brief 滚轮 → 滚动条步进（复扫 r2 #48 根修）。
 *  @note  对标 QAbstractScrollArea::wheelEvent 的主导轴选条
 *         （qabstractscrollarea.cpp:1170-1176：|x|>|y| 事件走水平条、
 *         否则走垂直条）与 QScrollBarPrivate::scrollByDelta 的步进口径
 *         （qabstractslider.cpp:668 起：offset=delta/120，缺省
 *         wheelScrollLines=3 行/格，横向 delta 取反）；单步 20px 取
 *         QScrollArea 构造口径（qscrollarea.cpp:108/121 vbar/hbar
 *         setSingleStep(20)）。方向与 XTreeWidget 滚轮修复同口径
 *         （XTreeWidget.c:2197-2199）：value = 当前值 − steps*3*20，
 *         正角度（滚向内容开头）减小值、下滚值增大。此前依赖基类
 *         VX_asa_wheelEvent 的 stepBy(steps*3)：其符号与 scrollByDelta
 *         相反（下滚 steps=−1 → 值减 → 顶部钳位 0），且单步仅缺省
 *         1px（XAbstractSlider.c:397）——「下滚×3 视口 0 像素差、把手
 *         拖拽可滚」即此二因叠加（复扫 r2 lane1 #48 实证）。直接
 *         setValue 由内部钳位兜底（XAbstractSlider.c:599-602），
 *         valueChanged → scrollContentsBy → 重绘链既有。 */
static void VX_scrollArea_wheelEvent(XWidget* self, XEvent* event)
{
    XScrollArea* area = (XScrollArea*)self;
    XAbstractScrollArea* base;
    XScrollBar* bar;
    XPoint delta;
    int steps;
    int value;
    if (!area || !event || XEvent_type(event) != XEVENT_TYPE_WHEEL) return;
#if XWINDOWEVENT_ON
    delta = XWheelEvent_angleDelta((XWheelEvent*)event);
#else
    delta.x = 0;
    delta.y = 0;
#endif
    {
        /* 主导轴分派（同 XTreeWidget 口径：dy 优先，|x|>|y| 才走横向
         * 并按 Qt scrollByDelta 对横向 delta 取反）。 */
        int ay = delta.y >= 0 ? delta.y : -delta.y;
        int ax = delta.x >= 0 ? delta.x : -delta.x;
        base = (XAbstractScrollArea*)area;
        if (ax > ay) {
            steps = -(delta.x / 120); /* 横向取反（Qt scrollByDelta）。 */
            bar = XAbstractScrollArea_horizontalScrollBar(base);
        } else {
            steps = delta.y / 120;
            bar = XAbstractScrollArea_verticalScrollBar(base);
        }
    }
    if (steps == 0) {
        XEvent_accept(event); /* 不足一格：消费不滚动（同基类口径）。 */
        return;
    }
    if (!bar) {
        XEvent_accept(event);
        return;
    }
    /* 单步 20px × 3 行/格（qscrollarea.cpp:108/121 +
     * scrollByDelta wheelScrollLines）；setValue 内部钳位到
     * [minimum,maximum]，越界即自然停在端点。 */
    value = XScrollBar_value(bar) - steps * 3 * 20;
    XScrollBar_setValue(bar, value);
    XEvent_accept(event);
}

XVtable* XScrollArea_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XScrollArea)
    XVTABLE_INHERIT_XCLASS(XAbstractScrollArea);
    XVTABLE_OVERLOAD_DEFAULT(
        EXAbstractScrollArea_ScrollContentsBy,
        VX_scrollArea_scrollContentsBy);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ResizeEvent,
                             VX_scrollArea_resizeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ShowEvent,
                             VX_scrollArea_showEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_WheelEvent,
                             VX_scrollArea_wheelEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VX_scrollArea_deinit);
    return XVTABLE_DEFAULT;
}

void XScrollArea_init(XScrollArea* self, XWidget* parent,
                      XWidgetFlags flags)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XAbstractScrollArea_init(&self->m_base, parent, flags);
    XClassSetVtable(self, XScrollArea);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    self->m_resizable = false;
    self->m_alignment = 0;
}

XScrollArea* XScrollArea_create_ex(XMemoryType memory, XWidget* parent,
                                   XWidgetFlags flags)
{
    XScrollArea* self = (XScrollArea*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XScrollArea_init(self, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 内容管理 ==================== */

void XScrollArea_setWidget(XScrollArea* self, XWidget* widget)
{
    XWidget* viewport;
    if (!self || !widget) return;
    viewport = XAbstractScrollArea_viewport(self);
    if (!viewport) return;
    if (self->m_widget == widget) return;
    XWidget_setParent(widget, viewport, 0);
    XWidget_setVisible(widget, true);
    self->m_widget = widget;
    xsa_updateWidgetGeometry(self);
}

XWidget* XScrollArea_takeWidget(XScrollArea* self)
{
    XWidget* widget;
    if (!self || !self->m_widget) return NULL;
    widget = self->m_widget;
    XWidget_setParent(widget, NULL, 0);
    self->m_widget = NULL;
    return widget;
}

XWidget* XScrollArea_widget(const XScrollArea* self)
{
    return self ? self->m_widget : NULL;
}

bool XScrollArea_widgetResizable(const XScrollArea* self)
{
    return self ? self->m_resizable : false;
}

void XScrollArea_setWidgetResizable(XScrollArea* self, bool resizable)
{
    if (!self || self->m_resizable == resizable) return;
    self->m_resizable = resizable;
    xsa_updateWidgetGeometry(self);
}

int XScrollArea_alignment(const XScrollArea* self)
{
    return self ? self->m_alignment : 0;
}

void XScrollArea_setAlignment(XScrollArea* self, int alignment)
{
    if (!self) return;
    self->m_alignment = alignment;
    xsa_updateWidgetGeometry(self);
}

/** @brief 最小滚动使目标点连同边距可见（对标 QScrollArea::ensureVisible
 *         的可见性判定：已可见不动；越上/左缘滚到 点-边距，越下/右缘
 *         滚到 点+边距-视口。此前恒滚到原点+边距，已可见也跳动）。 */
void XScrollArea_ensureVisible(XScrollArea* self, int x, int y,
                               int xmargin, int ymargin)
{
    XScrollBar* hsb;
    XScrollBar* vsb;
    XWidget* viewport;
    int pw;
    int ph;
    int relX;
    int relY;
    if (!self) return;
    hsb = XAbstractScrollArea_horizontalScrollBar(
        (const XAbstractScrollArea*)self);
    vsb = XAbstractScrollArea_verticalScrollBar(
        (const XAbstractScrollArea*)self);
    viewport = XAbstractScrollArea_viewport(
        (const XAbstractScrollArea*)self);
    pw = viewport ? XWidget_width(viewport) : 0;
    ph = viewport ? XWidget_height(viewport) : 0;
    relX = x - (hsb ? XScrollBar_value(hsb) : 0);
    relY = y - (vsb ? XScrollBar_value(vsb) : 0);
    if (hsb) {
        if (relX < xmargin)
            XScrollBar_setValue(hsb, x - xmargin);
        else if (relX + xmargin > pw)
            XScrollBar_setValue(hsb, x + xmargin - pw);
    }
    if (vsb) {
        if (relY < ymargin)
            XScrollBar_setValue(vsb, y - ymargin);
        else if (relY + ymargin > ph)
            XScrollBar_setValue(vsb, y + ymargin - ph);
    }
}

/** @brief 最小滚动使子控件矩形连同边距可见（对标
 *         QScrollArea::ensureWidgetVisible：消费宽/高维度，左右/上下
 *         缘按矩形边界判定；此前只取子控件原点转发点版，恒滚原点）。 */
void XScrollArea_ensureWidgetVisible(XScrollArea* self,
                                     XWidget* childWidget,
                                     int xmargin, int ymargin)
{
    XScrollBar* hsb;
    XScrollBar* vsb;
    XWidget* viewport;
    XWidget* content;
    XRect geom;
    int pw;
    int ph;
    int left;
    int right;
    int top;
    int bottom;
    if (!self || !childWidget) return;
    content = self->m_widget;
    /* 对标 Qt：目标即内容控件本体时无可确保（整体恒可见），直接返回。 */
    if (childWidget == content) return;
    geom = XWidget_geometry(childWidget);
    /* 子控件几何逐级映射到内容控件坐标（Qt 仅支持直接子控件并告警；
     * 此处向上累加偏移，父链不达内容控件时整体忽略）。 */
    {
        XWidget* p = XWidget_parentWidget(childWidget);
        while (p && p != content) {
            XRect pg = XWidget_geometry(p);
            geom.x += pg.x;
            geom.y += pg.y;
            p = XWidget_parentWidget(p);
        }
        if (p != content) return;
    }
    viewport = XAbstractScrollArea_viewport(
        (const XAbstractScrollArea*)self);
    pw = viewport ? XWidget_width(viewport) : 0;
    ph = viewport ? XWidget_height(viewport) : 0;
    if (xmargin * 2 > pw) xmargin = pw / 2;
    if (ymargin * 2 > ph) ymargin = ph / 2;
    left = geom.x;
    top = geom.y;
    right = geom.x + geom.width - 1;
    bottom = geom.y + geom.height - 1;
    hsb = XAbstractScrollArea_horizontalScrollBar(
        (const XAbstractScrollArea*)self);
    vsb = XAbstractScrollArea_verticalScrollBar(
        (const XAbstractScrollArea*)self);
    if (hsb) {
        int value = XScrollBar_value(hsb);
        if (left < xmargin + value)
            XScrollBar_setValue(hsb, left - xmargin);
        else if (right + xmargin > value + pw)
            XScrollBar_setValue(hsb, right + xmargin - pw);
    }
    if (vsb) {
        int value = XScrollBar_value(vsb);
        if (top < ymargin + value)
            XScrollBar_setValue(vsb, top - ymargin);
        else if (bottom + ymargin > value + ph)
            XScrollBar_setValue(vsb, bottom + ymargin - ph);
    }
}

XSize XScrollArea_sizeHint(const XScrollArea* self)
{
    XWidget* viewport;
    XSize out;
    if (!self) return XAbstractScrollArea_sizeHint(
                       (const XAbstractScrollArea*)self);
    viewport = XAbstractScrollArea_viewport(
        (const XAbstractScrollArea*)self);
    if (viewport) {
        out = XWidget_sizeHint(viewport);
        if (out.width > 0 && out.height > 0) return out;
    }
    return XAbstractScrollArea_sizeHint(
        (const XAbstractScrollArea*)self);
}












#endif /* XWIDGET_ON && XFRAME_ON && XSCROLLBAR_ON && XABSTRACTSCROLLAREA_ON && XSCROLLAREA_ON */
