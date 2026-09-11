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
#include "XWidget_Protected.h"
#include <string.h>

#if XWIDGET_ON && XFRAME_ON && XSCROLLBAR_ON && XABSTRACTSCROLLAREA_ON

/* ==================== 内部工具 ==================== */

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
    return XVTABLE_DEFAULT;
}

void XAbstractScrollArea_init(XAbstractScrollArea* self, XWidget* parent,
                              XWidgetFlags flags)
{
    XSize hint;
    if (!self) return;
    memset(self, 0, sizeof(*self));
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
    XObject_connect_1((XObject*)self->m_vScrollBar,
                      XSignal(XScrollBar_valueChanged_signal(self)),
                      (XObject*)self, xasa_vScrollChangedSlot,
                      XConnectionType_Direct);
    XObject_connect_1((XObject*)self->m_hScrollBar,
                      XSignal(XScrollBar_valueChanged_signal(self)),
                      (XObject*)self, xasa_hScrollChangedSlot,
                      XConnectionType_Direct);
    XWidget_resize(self, 200, 150);
    hint.width = 200;
    hint.height = 150;
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

#endif /* XWIDGET_ON && XFRAME_ON && XSCROLLBAR_ON && XABSTRACTSCROLLAREA_ON */
