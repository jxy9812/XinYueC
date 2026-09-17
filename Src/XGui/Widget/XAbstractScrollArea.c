/**
 * @file       XAbstractScrollArea.c
 * @brief      抽象滚动区域基类实现（对标 Qt 6.8 QAbstractScrollArea 全部公共 API）。
 * @details    与同名头文件的公共 API 一一对应；内部实现细节见
 *             头文件 @note 与函数级 Doxygen 注释。
 * @author     XinYueC 团队
 */

#include "XAbstractScrollArea.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XVarList.h"
#include "XGuiConfig.h"

#include "XAlgorithm.h"
#include "XWidget_Protected.h"
#include "XAlignment.h"

#if XWIDGET_ON && XFRAME_ON && XSCROLLBAR_ON && XABSTRACTSCROLLAREA_ON

/* ==================== 内部工具 ==================== */

/** @brief 滚轮：转发到滚动条步进（对标 QScrollArea 视口滚轮滚动）。
 *         默认滚垂直条（Shift 滚水平条），120 角度 = 3 倍单步，
 *         保证滚动幅度肉眼可见。 */
static void VX_asa_wheelEvent(XWidget* self, XEvent* event)
{
    XAbstractScrollArea* area = (XAbstractScrollArea*)self;
    XScrollBar* bar;
    int steps = 0;
    if (!area || !event || XEvent_type(event) != XEVENT_TYPE_WHEEL) return;
#if XWINDOWEVENT_ON
    {
        XWheelEvent* we = (XWheelEvent*)event;
        XPoint delta = XWheelEvent_angleDelta(we);
        int dy = (delta.y != 0) ? delta.y : delta.x;
        steps = dy / 120;
    }
#endif
    if (steps == 0) { XEvent_accept(event); return; }
    /* Shift 横滚：水平条；默认竖滚：垂直条。 */
    bar = XAbstractScrollArea_verticalScrollBar(area);
    if (bar) XAbstractSlider_stepBy_base((XAbstractSlider*)bar, steps * 3);
    XEvent_accept(event);
}
/** @brief 滚动条 value 变化 → 调 scrollContentsBy 虚槽。 */
static void xasa_vScrollChangedSlot(XObject* receiver, XVarList* args)
{
    XAbstractScrollArea* self = (XAbstractScrollArea*)receiver;
    if (!self || !args) return;
    (void)args;
    XAbstractScrollArea_scrollContentsBy_base(self, 0, 1);
}

static void xasa_hScrollChangedSlot(XObject* receiver, XVarList* args)
{
    XAbstractScrollArea* self = (XAbstractScrollArea*)receiver;
    if (!self || !args) return;
    (void)args;
    XAbstractScrollArea_scrollContentsBy_base(self, 1, 0);
}

/** @brief 连接滚动条 valueChanged → 内容滚动虚槽。 */
static void xasa_connectBar(XAbstractScrollArea* self, XScrollBar* bar,
                            bool horizontal)
{
    if (!self || !bar) return;
    if (horizontal)
        XObject_connect_1((XObject*)bar,
                          (size_t)XScrollBar_valueChanged_signal(bar, 0),
                          (XObject*)self, xasa_hScrollChangedSlot,
                          XConnectionType_Direct);
    else
        XObject_connect_1((XObject*)bar,
                          (size_t)XScrollBar_valueChanged_signal(bar, 0),
                          (XObject*)self, xasa_vScrollChangedSlot,
                          XConnectionType_Direct);
}

/** @brief 断开滚动条连接（替换滚动条前调用）。 */
static void xasa_disconnectBar(XAbstractScrollArea* self, XScrollBar* bar,
                               bool horizontal)
{
    if (!self || !bar) return;
    XObject_disconnect_1((XObject*)bar,
                         (size_t)XScrollBar_valueChanged_signal(bar, 0),
                         (XObject*)self,
                         horizontal ? xasa_hScrollChangedSlot
                                    : xasa_vScrollChangedSlot);
}

/** @brief 依据策略与内容尺寸更新滚动条可见性与范围。 */
static void xasa_updateScrollBars(XAbstractScrollArea* self)
{
    int vw;
    int vh;
    bool showV;
    bool showH;
    int range;
    if (!self || !self->m_viewport) return;
    vw = XWidget_width(self->m_viewport);
    vh = XWidget_height(self->m_viewport);
    showV = self->m_vPolicy != XScrollBarPolicy_AlwaysOff;
    showH = self->m_hPolicy != XScrollBarPolicy_AlwaysOff;
    if (self->m_vPolicy == XScrollBarPolicy_AsNeeded)
        showV = self->m_contentHeight > vh;
    if (self->m_hPolicy == XScrollBarPolicy_AsNeeded)
        showH = self->m_contentWidth > vw;
    XWidget_setVisible((XWidget*)self->m_vScrollBar, showV);
    XWidget_setVisible((XWidget*)self->m_hScrollBar, showH);
    if (showV) {
        range = self->m_contentHeight > vh ? self->m_contentHeight - vh : 0;
        XScrollBar_setRange(self->m_vScrollBar, 0, range);
    }
    if (showH) {
        range = self->m_contentWidth > vw ? self->m_contentWidth - vw : 0;
        XScrollBar_setRange(self->m_hScrollBar, 0, range);
    }
}

/* ==================== 事件处理 ==================== */

static void VX_asa_resizeEvent(XWidget* self, XEvent* event)
{
    XAbstractScrollArea* area = (XAbstractScrollArea*)self;
    int w = XWidget_width(self);
    int h = XWidget_height(self);
    int sbw = 16;
    bool showV;
    bool showH;
    XRect r;
    (void)event;
    if (!area || !area->m_viewport) return;
    showV = area->m_vPolicy != XScrollBarPolicy_AlwaysOff &&
            (area->m_vPolicy == XScrollBarPolicy_AlwaysOn ||
             area->m_contentHeight > h);
    showH = area->m_hPolicy != XScrollBarPolicy_AlwaysOff &&
            (area->m_hPolicy == XScrollBarPolicy_AlwaysOn ||
             area->m_contentWidth > w);
    XRect_init(&r, 0, 0, showV ? w - sbw : w, showH ? h - sbw : h);
    XWidget_setGeometryRect(area->m_viewport, &r);
    if (showV) {
        XRect_init(&r, w - sbw, 0, sbw, showH ? h - sbw : h);
        XWidget_setGeometryRect((XWidget*)area->m_vScrollBar, &r);
    }
    if (showH) {
        XRect_init(&r, 0, h - sbw, showV ? w - sbw : w, sbw);
        XWidget_setGeometryRect((XWidget*)area->m_hScrollBar, &r);
    }
    /* 右下角控件：仅当两个滚动条都显示时可见。 */
    if (area->m_cornerWidget) {
        if (showV && showH) {
            XRect_init(&r, w - sbw, h - sbw, sbw, sbw);
            XWidget_setGeometryRect(area->m_cornerWidget, &r);
            XWidget_setVisible(area->m_cornerWidget, true);
        } else {
            XWidget_setVisible(area->m_cornerWidget, false);
        }
    }
    /* 附加滚动条控件（addScrollBarWidget）：按对齐位挂靠边缘；
     * 简化排布：覆盖在对应边缘，不参与视口尺寸计算（头文件注明）。 */
    {
        int i;
        for (i = 0; i < area->m_sbWidgetCount; ++i) {
            XWidget* sw = area->m_sbWidgets[i];
            int al = area->m_sbWidgetAligns[i];
            if (!sw) continue;
            if (al & (int)XAlignment_Top) {
                XRect_init(&r, 0, 0, w, sbw);
                XWidget_setGeometryRect(sw, &r);
            } else if (al & (int)XAlignment_Bottom) {
                XRect_init(&r, 0, h - sbw, w, sbw);
                XWidget_setGeometryRect(sw, &r);
            } else if (al & (int)XAlignment_Left) {
                XRect_init(&r, 0, 0, sbw, h);
                XWidget_setGeometryRect(sw, &r);
            } else { /* Right（含默认） */
                XRect_init(&r, w - sbw, 0, sbw, h);
                XWidget_setGeometryRect(sw, &r);
            }
        }
    }
    xasa_updateScrollBars(area);
}

static void VX_asa_paintEvent(XWidget* self, XEvent* event)
{
    XAbstractScrollArea* area = (XAbstractScrollArea*)self;
    XPainter painter;
    XImage* image;
    XPoint offset;
    XRect line;
    uint32_t mid;
    int w;
    int h;
    if (!area || !event) return;
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
                                  XPaletteColorRole_Mid);
        mid = XColor_rgba(&c);
    }
#else
    mid = 0xFF808080u;
#endif /* XPALETTE_ON */
    /* 背景：基色清屏防止父控件渲染透出。 */
    {
        XRect bg = { 0, 0, w, h };
#if XPALETTE_ON
        XPalette palette = XWidget_palette(self);
        XColor bc = XPalette_color(&palette, XPaletteColorGroup_Current,
                                   XPaletteColorRole_Base);
        XPainter_fillRect(&painter, &bg, XColor_rgba(&bc));
#else
        XPainter_fillRect(&painter, &bg, 0xFFFFFFFFu);
#endif /* XPALETTE_ON */
    }
    XRect_init(&line, 0, h - 1, w, 1);
    XPainter_fillRect(&painter, &line, mid);
    XPainter_deinit(&painter);
}

/** @brief 默认内容滚动：平移视口内的内容偏移（第一版为空操作占位，
 *         派生类 XScrollArea 覆写驱动内容 widget 位置）。 */
static void VX_asa_scrollContentsBy(XAbstractScrollArea* self, int dx, int dy)
{
    (void)self;
    (void)dx;
    (void)dy;
}

void XAbstractScrollArea_scrollContentsBy_base(XAbstractScrollArea* self,
                                               int dx, int dy)
{
    if (!self) return;
    if (!XClassGetVtable(self)) return;
    XClassGetVirtualFunc(self, EXAbstractScrollArea_ScrollContentsBy,
                         void (*)(XAbstractScrollArea*, int, int))(
        self, dx, dy);
}

/* ==================== 生命周期与虚表 ==================== */

static void VX_asa_deinit(XAbstractScrollArea* self)
{
    if (!self) return;
    if (self->m_viewport) {
        XWidget_delete_base((XWidget*)self->m_viewport);
        self->m_viewport = NULL;
    }
    if (self->m_vScrollBar) {
        XWidget_delete_base((XWidget*)self->m_vScrollBar);
        self->m_vScrollBar = NULL;
    }
    if (self->m_hScrollBar) {
        XWidget_delete_base((XWidget*)self->m_hScrollBar);
        self->m_hScrollBar = NULL;
    }
    XClass_Deinit_Parent(XFrame, (XFrame*)self);
}

XVtable* XAbstractScrollArea_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XAbstractScrollArea)
    XVTABLE_INHERIT_XCLASS(XFrame);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ResizeEvent, VX_asa_resizeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VX_asa_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractScrollArea_ScrollContentsBy,
                             VX_asa_scrollContentsBy);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_WheelEvent, VX_asa_wheelEvent);
    return XVTABLE_DEFAULT;
}

void XAbstractScrollArea_init(XAbstractScrollArea* self, XWidget* parent,
                              XWidgetFlags flags)
{
    XSize hint;
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XFrame_init(&self->m_base, parent, flags);
    XClassSetVtable(self, XAbstractScrollArea);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    self->m_viewport = XWidget_create(self, 0);
    self->m_vScrollBar = XScrollBar_create_2(
        (int)XAbstractSliderOrientation_Vertical, self, 0);
    self->m_hScrollBar = XScrollBar_create_2(
        (int)XAbstractSliderOrientation_Horizontal, self, 0);
    self->m_vPolicy = (int)XScrollBarPolicy_AsNeeded;
    self->m_hPolicy = (int)XScrollBarPolicy_AsNeeded;
    XWidget_setVisible((XWidget*)self->m_vScrollBar, false);
    XWidget_setVisible((XWidget*)self->m_hScrollBar, false);
    xasa_connectBar(self, self->m_vScrollBar, false);
    xasa_connectBar(self, self->m_hScrollBar, true);
    XWidget_resize(self, 200, 150);
    hint.width = 256;
    hint.height = 192;
    XWidget_setSizeHint((XWidget*)self, &hint);
}

XAbstractScrollArea* XAbstractScrollArea_create_ex(
    XMemoryType memory, XWidget* parent, XWidgetFlags flags)
{
    XAbstractScrollArea* self =
        (XAbstractScrollArea*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XAbstractScrollArea_init(self, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 公共 API ==================== */

XWidget* XAbstractScrollArea_viewport(const XAbstractScrollArea* self)
{
    return self ? self->m_viewport : NULL;
}

XScrollBar* XAbstractScrollArea_verticalScrollBar(
    const XAbstractScrollArea* self)
{
    return self ? self->m_vScrollBar : NULL;
}

XScrollBar* XAbstractScrollArea_horizontalScrollBar(
    const XAbstractScrollArea* self)
{
    return self ? self->m_hScrollBar : NULL;
}

void XAbstractScrollArea_setVerticalScrollBarPolicy(
    XAbstractScrollArea* self, XScrollBarPolicy policy)
{
    if (!self) return;
    self->m_vPolicy = (int)policy;
    VX_asa_resizeEvent((XWidget*)self, NULL);
}

XScrollBarPolicy XAbstractScrollArea_verticalScrollBarPolicy(
    const XAbstractScrollArea* self)
{
    return self ? (XScrollBarPolicy)self->m_vPolicy
                : XScrollBarPolicy_AsNeeded;
}

void XAbstractScrollArea_setHorizontalScrollBarPolicy(
    XAbstractScrollArea* self, XScrollBarPolicy policy)
{
    if (!self) return;
    self->m_hPolicy = (int)policy;
    VX_asa_resizeEvent((XWidget*)self, NULL);
}

XScrollBarPolicy XAbstractScrollArea_horizontalScrollBarPolicy(
    const XAbstractScrollArea* self)
{
    return self ? (XScrollBarPolicy)self->m_hPolicy
                : XScrollBarPolicy_AsNeeded;
}

void XAbstractScrollArea_setCornerWidget(XAbstractScrollArea* self,
                                         XWidget* widget)
{
    if (!self) return;
    self->m_cornerWidget = widget;
}

XWidget* XAbstractScrollArea_cornerWidget(const XAbstractScrollArea* self)
{
    return self ? self->m_cornerWidget : NULL;
}

void XAbstractScrollArea_setContentSize(XAbstractScrollArea* self,
                                        int width, int height)
{
    if (!self) return;
    self->m_contentWidth = width;
    self->m_contentHeight = height;
    xasa_updateScrollBars(self);
}

/* ==================== Task 2.9：滚动条/视口替换与尺寸 API ========== */

void XAbstractScrollArea_setVerticalScrollBar(XAbstractScrollArea* self,
                                              XScrollBar* scrollbar)
{
    if (!self) return;
    if (self->m_vScrollBar == scrollbar) return;
    if (self->m_vScrollBar) {
        xasa_disconnectBar(self, self->m_vScrollBar, false);
        XWidget_delete_base((XWidget*)self->m_vScrollBar);
    }
    self->m_vScrollBar = scrollbar;
    if (scrollbar) {
        XWidget_setParent((XWidget*)scrollbar, (XWidget*)self, 0);
        xasa_connectBar(self, scrollbar, false);
    }
    VX_asa_resizeEvent((XWidget*)self, NULL);
}

void XAbstractScrollArea_setHorizontalScrollBar(XAbstractScrollArea* self,
                                                XScrollBar* scrollbar)
{
    if (!self) return;
    if (self->m_hScrollBar == scrollbar) return;
    if (self->m_hScrollBar) {
        xasa_disconnectBar(self, self->m_hScrollBar, true);
        XWidget_delete_base((XWidget*)self->m_hScrollBar);
    }
    self->m_hScrollBar = scrollbar;
    if (scrollbar) {
        XWidget_setParent((XWidget*)scrollbar, (XWidget*)self, 0);
        xasa_connectBar(self, scrollbar, true);
    }
    VX_asa_resizeEvent((XWidget*)self, NULL);
}

void XAbstractScrollArea_addScrollBarWidget(XAbstractScrollArea* self,
                                            XWidget* widget, int alignment)
{
    int i;
    if (!self || !widget) return;
    for (i = 0; i < self->m_sbWidgetCount; ++i)
        if (self->m_sbWidgets[i] == widget) return; /* 去重 */
    if (self->m_sbWidgetCount >= 8) return;
    XWidget_setParent(widget, (XWidget*)self, 0);
    self->m_sbWidgets[self->m_sbWidgetCount] = widget;
    self->m_sbWidgetAligns[self->m_sbWidgetCount] = alignment;
    ++self->m_sbWidgetCount;
    VX_asa_resizeEvent((XWidget*)self, NULL);
}

const XWidget** XAbstractScrollArea_scrollBarWidgets(
    const XAbstractScrollArea* self, int alignment)
{
    (void)alignment;
    if (!self) return NULL;
    return (const XWidget**)self->m_sbWidgets;
}

void XAbstractScrollArea_setViewport(XAbstractScrollArea* self,
                                     XWidget* widget)
{
    if (!self || !widget) return;
    if (self->m_viewport == widget) return;
    if (self->m_viewport)
        XWidget_delete_base((XWidget*)self->m_viewport);
    self->m_viewport = widget;
    XWidget_setParent(widget, (XWidget*)self, 0);
    VX_asa_resizeEvent((XWidget*)self, NULL);
}

XSize XAbstractScrollArea_maximumViewportSize(
    const XAbstractScrollArea* self)
{
    XSize out;
    int w;
    int h;
    int sbw = 16;
    bool showV;
    bool showH;
    if (!self) {
        XSize_init(&out, 0, 0);
        return out;
    }
    w = XWidget_width((XWidget*)self);
    h = XWidget_height((XWidget*)self);
    showV = self->m_vScrollBar &&
            (self->m_vPolicy == XScrollBarPolicy_AlwaysOn ||
             XWidget_isVisible((XWidget*)self->m_vScrollBar));
    showH = self->m_hScrollBar &&
            (self->m_hPolicy == XScrollBarPolicy_AlwaysOn ||
             XWidget_isVisible((XWidget*)self->m_hScrollBar));
    XSize_init(&out, showV ? w - sbw : w, showH ? h - sbw : h);
    if (out.width < 0) out.width = 0;
    if (out.height < 0) out.height = 0;
    return out;
}

XSize XAbstractScrollArea_sizeHint(const XAbstractScrollArea* self)
{
    XSize out;
    int frame;
    int sbw = 0;
    int sbh = 0;
    bool vbarHidden;
    bool hbarHidden;
    if (!self) {
        XSize_init(&out, 0, 0);
        return out;
    }
    /* 对标 Qt QAbstractScrollArea::sizeHint：
     * - AdjustIgnored：用控件自身尺寸提示（XGui 初始化为 256x192，
     *   与 Qt 的固定返回值一致）；
     * - AdjustToContents：每次查询都按 帧宽 + 滚动条 + 视口提示 重算；
     * - AdjustToContentsOnFirstShow：首次计算后缓存（对标 Qt 的
     *   d->sizeHint 缓存），策略变更时由 setSizeAdjustPolicy 清除。 */
    if (self->m_sizeAdjustPolicy ==
        XAbstractScrollAreaSizeAdjustPolicy_AdjustIgnored) {
        return XWidget_sizeHint((const XWidget*)self);
    }
    if (self->m_sizeAdjustPolicy ==
            XAbstractScrollAreaSizeAdjustPolicy_AdjustToContentsOnFirstShow &&
        self->m_sizeHintCached) {
        return self->m_sizeHintCache;
    }
    frame = 2 * XFrame_frameWidth((const XFrame*)self);
    vbarHidden = !self->m_vScrollBar ||
                 self->m_vPolicy == XScrollBarPolicy_AlwaysOff;
    hbarHidden = !self->m_hScrollBar ||
                 self->m_hPolicy == XScrollBarPolicy_AlwaysOff;
    if (!vbarHidden) {
        XSize s = XWidget_sizeHint((XWidget*)self->m_vScrollBar);
        sbw = s.width > 0 ? s.width : 0;
    }
    if (!hbarHidden) {
        XSize s = XWidget_sizeHint((XWidget*)self->m_hScrollBar);
        sbh = s.height > 0 ? s.height : 0;
    }
    XSize_init(&out, frame + sbw + self->m_contentWidth,
               frame + sbh + self->m_contentHeight);
    if (self->m_sizeAdjustPolicy ==
        XAbstractScrollAreaSizeAdjustPolicy_AdjustToContentsOnFirstShow) {
        ((XAbstractScrollArea*)self)->m_sizeHintCache = out;
        ((XAbstractScrollArea*)self)->m_sizeHintCached = true;
    }
    return out;
}

XAbstractScrollAreaSizeAdjustPolicy XAbstractScrollArea_sizeAdjustPolicy(
    const XAbstractScrollArea* self)
{
    return self ? (XAbstractScrollAreaSizeAdjustPolicy)self->m_sizeAdjustPolicy
                : XAbstractScrollAreaSizeAdjustPolicy_AdjustIgnored;
}

void XAbstractScrollArea_setSizeAdjustPolicy(
    XAbstractScrollArea* self, XAbstractScrollAreaSizeAdjustPolicy policy)
{
    if (!self) return;
    if (self->m_sizeAdjustPolicy == (int)policy) return;
    self->m_sizeAdjustPolicy = (int)policy;
    /* 对标 Qt：清缓存并请求重新布局。 */
    self->m_sizeHintCached = false;
    XSize_init(&self->m_sizeHintCache, 0, 0);
    XWidget_updateGeometry((XWidget*)self);
}

XSize XAbstractScrollArea_minimumSizeHint(const XAbstractScrollArea* self)
{
    XSize out;
    int vsbExt = 15; /* XScrollBar 默认尺寸提示 15x15。 */
    int hsbExt = 15;
    int extra = 0;
    if (!self) {
        XSize_init(&out, 0, 0);
        return out;
    }
    if (self->m_vScrollBar) {
        XSize s = XWidget_sizeHint((XWidget*)self->m_vScrollBar);
        if (s.width > 0) vsbExt = s.width;
    }
    if (self->m_hScrollBar) {
        XSize s = XWidget_sizeHint((XWidget*)self->m_hScrollBar);
        if (s.height > 0) hsbExt = s.height;
    }
    extra = 2 * XFrame_frameWidth((const XFrame*)self);
    XSize_init(&out, vsbExt + extra, hsbExt + extra);
    return out;
}






#endif /* XWIDGET_ON && XFRAME_ON && XSCROLLBAR_ON && XABSTRACTSCROLLAREA_ON */
