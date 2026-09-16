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

XVtable* XScrollArea_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XScrollArea)
    XVTABLE_INHERIT_XCLASS(XAbstractScrollArea);
    XVTABLE_OVERLOAD_DEFAULT(
        EXAbstractScrollArea_ScrollContentsBy,
        VX_scrollArea_scrollContentsBy);
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

void XScrollArea_ensureVisible(XScrollArea* self, int x, int y,
                               int xmargin, int ymargin)
{
    XScrollBar* hsb;
    XScrollBar* vsb;
    if (!self) return;
    hsb = XAbstractScrollArea_horizontalScrollBar(self);
    vsb = XAbstractScrollArea_verticalScrollBar(self);
    if (hsb) {
        int target = x - xmargin;
        if (target < 0) target = 0;
        XScrollBar_setValue(hsb, target);
    }
    if (vsb) {
        int target = y - ymargin;
        if (target < 0) target = 0;
        XScrollBar_setValue(vsb, target);
    }
}

void XScrollArea_ensureWidgetVisible(XScrollArea* self,
                                     XWidget* childWidget,
                                     int xmargin, int ymargin)
{
    XRect geom;
    if (!self || !childWidget) return;
    geom = XWidget_geometry(childWidget);
    XScrollArea_ensureVisible(self, geom.x, geom.y, xmargin, ymargin);
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
