/**
 * @file       XLegend.c
 * @brief      XLegend 图表图例实现（对标 QLegend）。
 * @details    本库无 GraphicsWidget 框架，XLlegend 以 XObject 派生，
 *             承担「标记集合 + 布局/外观参数」职责；绘制由 XChartView
 *             读取本类状态完成。
 * @author     XinYueC 团队
 ******************************************************************************/
#if XGUI_ON

#include "XLegend.h"

#include "XAlgorithm.h"
#include "XMemory.h"
#include "XClass.h"
#include "XVarList.h"
#include "XEventType.h"

#if XCHARTS_ON

/* ==================== 信号发射助手 ==================== */

static bool xlegend_canEmit(const XLegend* self)
{
    return self && ((const XObject*)self)->m_signalSlot;
}

static void xlegend_emitBool(XLegend* self, size_t signal, bool v)
{
    XVarList* args;
    if (!xlegend_canEmit(self)) return;
    args = XVarList_Create(XVar(bool, v));
    if (!args) return;
    XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                       XEVENT_PRIORITY_NORMAL);
}

static void xlegend_emitInt(XLegend* self, size_t signal, int v)
{
    XVarList* args;
    if (!xlegend_canEmit(self)) return;
    args = XVarList_Create(XVar(int, v));
    if (!args) return;
    XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                       XEVENT_PRIORITY_NORMAL);
}

static void xlegend_emitU32(XLegend* self, size_t signal, uint32_t v)
{
    XVarList* args;
    if (!xlegend_canEmit(self)) return;
    args = XVarList_Create(XVar(uint32_t, v));
    if (!args) return;
    XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                       XEVENT_PRIORITY_NORMAL);
}

/* ==================== 内部助手 ==================== */

static bool xlegend_grow(XLegend* self)
{
    if (!self) return false;
    if (self->m_count < self->m_capacity) return true;
    {
        int cap = self->m_capacity > 0 ? self->m_capacity * 2 : 8;
        XLegendMarker** a = (XLegendMarker**)XRealloc_System(
            self->m_markers, sizeof(XLegendMarker*) * (size_t)cap);
        if (!a) return false;
        self->m_markers = a;
        self->m_capacity = (uint16_t)cap;
    }
    return true;
}

/* ==================== 虚函数实现 ==================== */

static void VXLegend_deinit(XLegend* self)
{
    int i;
    if (!self) return;
    if (self->m_markers) {
        for (i = 0; i < (int)self->m_count; ++i) {
            if (self->m_markers[i]) {
                XClassDelete(self->m_markers[i]);
                self->m_markers[i] = NULL;
            }
        }
        XFree_System(self->m_markers);
        self->m_markers = NULL;
    }
    if (self->m_fontFamily) {
        XClassDelete(self->m_fontFamily);
        self->m_fontFamily = NULL;
    }
    self->m_count = 0;
    self->m_capacity = 0;
    XClass_Deinit_Parent(XObject, (XObject*)self);
}

XVtable* XLegend_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XLegend)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXLegend_deinit);
    return XVTABLE_DEFAULT;
}

/* ==================== 生命周期 ==================== */

void XLegend_init(XLegend* self)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XObject_init((XObject*)self);
    XClassSetVtable(self, XLegend);
    self->m_fontFamily = XString_create();
    self->m_penWidth = 1.0;
    self->m_markerShape = XLegend_MarkerShape_Default;
    self->m_backgroundVisible = true;
    self->m_reverseMarkers = false;
    self->m_showToolTips = true;
    self->m_interactive = false;
    self->m_attachedToChart = true;
    self->m_visible = true;
    /* Qt 默认对齐：底部居中。 */
    self->m_alignment = XLegend_Alignment_AlignBottom |
                        XLegend_Alignment_AlignHCenter;
}

XLegend* XLegend_create_ex(XMemoryType memory)
{
    XLegend* self = (XLegend*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XLegend_init(self);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 标记集合 ==================== */

bool XLegend_appendMarker(XLegend* self, XLegendMarker* marker)
{
    if (!self || !marker) return false;
    if (!xlegend_grow(self)) return false;
    self->m_markers[self->m_count++] = marker;
    return true;
}

bool XLegend_removeMarkerForSeries(XLegend* self, XAbstractSeries* series)
{
    int i;
    int idx = -1;
    if (!self || !series) return false;
    for (i = 0; i < (int)self->m_count; ++i) {
        if (self->m_markers[i] &&
            XLegendMarker_series(self->m_markers[i]) == series) {
            idx = i;
            break;
        }
    }
    if (idx < 0) return false;
    XClassDelete(self->m_markers[idx]);
    for (i = idx; i + 1 < (int)self->m_count; ++i)
        self->m_markers[i] = self->m_markers[i + 1];
    self->m_markers[--self->m_count] = NULL;
    return true;
}

void XLegend_clearMarkers(XLegend* self)
{
    int i;
    if (!self) return;
    for (i = 0; i < (int)self->m_count; ++i) {
        if (self->m_markers[i]) {
            XClassDelete(self->m_markers[i]);
            self->m_markers[i] = NULL;
        }
    }
    self->m_count = 0;
}

int XLegend_markerCount(const XLegend* self)
{ return self ? (int)self->m_count : 0; }

XLegendMarker* XLegend_markerAt(const XLegend* self, int index)
{
    if (!self || index < 0 || index >= (int)self->m_count) return NULL;
    return self->m_markers[index];
}

XLegendMarker* XLegend_marker(XLegend* self, XAbstractSeries* series)
{
    int i;
    if (!self) return NULL;
    if (!series) return (int)self->m_count > 0 ? self->m_markers[0] : NULL;
    for (i = 0; i < (int)self->m_count; ++i)
        if (self->m_markers[i] &&
            XLegendMarker_series(self->m_markers[i]) == series)
            return self->m_markers[i];
    return NULL;
}

/* ==================== 外观 ==================== */

void XLegend_setBrush(XLegend* self, uint32_t color)
{
    if (!self || self->m_brushColor == color) return;
    self->m_brushColor = color;
    xlegend_emitU32(self, (size_t)XLegend_colorChanged_signal, color);
}

uint32_t XLegend_brush(const XLegend* self)
{ return self ? self->m_brushColor : 0; }

void XLegend_setColor(XLegend* self, uint32_t color)
{
    if (!self || self->m_brushColor == color) return;
    self->m_brushColor = color;
    xlegend_emitU32(self, (size_t)XLegend_colorChanged_signal, color);
}

uint32_t XLegend_color(const XLegend* self)
{ return self ? self->m_brushColor : 0; }

void XLegend_setPen(XLegend* self, uint32_t color, double width)
{
    if (!self) return;
    if (width < 0.0) width = 0.0;
    if (self->m_penColor == color && self->m_penWidth == width) return;
    self->m_penColor = color;
    self->m_penWidth = width;
    xlegend_emitU32(self, (size_t)XLegend_borderColorChanged_signal, color);
}

void XLegend_pen(const XLegend* self, uint32_t* color, double* width)
{
    if (!self) return;
    if (color) *color = self->m_penColor;
    if (width) *width = self->m_penWidth;
}

void XLegend_setBorderColor(XLegend* self, uint32_t color)
{
    XLegend_setPen(self, color, self ? self->m_penWidth : 1.0);
}

uint32_t XLegend_borderColor(const XLegend* self)
{ return self ? self->m_penColor : 0; }

void XLegend_setFont(XLegend* self, const XString* family, int pointSize)
{
    if (!self) return;
    if (!self->m_fontFamily) self->m_fontFamily = XString_create();
    if (self->m_fontFamily) {
        if (family)
            XString_assign(self->m_fontFamily, family);
        else
            XString_assign_utf8(self->m_fontFamily, "");
    }
    if (pointSize < 0) pointSize = 0;
    self->m_fontSize = pointSize;
    xlegend_emitInt(self, (size_t)XLegend_fontChanged_signal, pointSize);
}

void XLegend_setFont_utf8(XLegend* self, const char* family, int pointSize)
{
    XString* tmp = NULL;
    if (family) {
        tmp = XString_create_utf8(family);
        if (!tmp) return;
    }
    XLegend_setFont(self, tmp, pointSize);
    if (tmp) XClassDelete(tmp);
}

void XLegend_setFont_2(XLegend* self, const char* family, int pointSize)
{ XLegend_setFont_utf8(self, family, pointSize); }

const XString* XLegend_fontFamily_const(const XLegend* self)
{ return (self && self->m_fontFamily) ? self->m_fontFamily : NULL; }

const XString* XLegend_fontFamily(const XLegend* self)
{ return XLegend_fontFamily_const(self); }

int XLegend_fontSize(const XLegend* self)
{ return self ? self->m_fontSize : 0; }

void XLegend_setLabelBrush(XLegend* self, uint32_t color)
{
    if (!self || self->m_labelBrushColor == color) return;
    self->m_labelBrushColor = color;
    xlegend_emitU32(self, (size_t)XLegend_labelColorChanged_signal, color);
}

uint32_t XLegend_labelBrush(const XLegend* self)
{ return self ? self->m_labelBrushColor : 0; }

void XLegend_setLabelColor(XLegend* self, uint32_t color)
{
    if (!self || self->m_labelBrushColor == color) return;
    self->m_labelBrushColor = color;
    xlegend_emitU32(self, (size_t)XLegend_labelColorChanged_signal, color);
}

uint32_t XLegend_labelColor(const XLegend* self)
{ return self ? self->m_labelBrushColor : 0; }

/* ==================== 对齐 / 附着 / 可见性 ==================== */

void XLegend_setAlignment(XLegend* self, uint32_t alignment)
{
    if (!self || self->m_alignment == alignment) return;
    self->m_alignment = alignment;
}

uint32_t XLegend_alignment(const XLegend* self)
{ return self ? self->m_alignment : 0; }

void XLegend_detachFromChart(XLegend* self)
{
    if (!self || !self->m_attachedToChart) return;
    self->m_attachedToChart = false;
    xlegend_emitBool(self, (size_t)XLegend_attachedToChartChanged_signal,
                     false);
}

void XLegend_attachToChart(XLegend* self)
{
    if (!self || self->m_attachedToChart) return;
    self->m_attachedToChart = true;
    xlegend_emitBool(self, (size_t)XLegend_attachedToChartChanged_signal,
                     true);
}

bool XLegend_isAttachedToChart(const XLegend* self)
{ return self ? self->m_attachedToChart : false; }

void XLegend_setVisible(XLegend* self, bool visible)
{
    if (!self || self->m_visible == visible) return;
    self->m_visible = visible;
}

bool XLegend_isVisible(const XLegend* self)
{ return self ? self->m_visible : false; }

void XLegend_setBackgroundVisible(XLegend* self, bool visible)
{
    if (!self || self->m_backgroundVisible == visible) return;
    self->m_backgroundVisible = visible;
    xlegend_emitBool(self,
                     (size_t)XLegend_backgroundVisibleChanged_signal, visible);
}

bool XLegend_isBackgroundVisible(const XLegend* self)
{ return self ? self->m_backgroundVisible : false; }

/* ==================== 反序 / 提示 / 交互 / 形状 ==================== */

void XLegend_setReverseMarkers(XLegend* self, bool reverseMarkers)
{
    if (!self || self->m_reverseMarkers == reverseMarkers) return;
    self->m_reverseMarkers = reverseMarkers;
    xlegend_emitBool(self, (size_t)XLegend_reverseMarkersChanged_signal,
                     reverseMarkers);
}

bool XLegend_reverseMarkers(const XLegend* self)
{ return self ? self->m_reverseMarkers : false; }

void XLegend_setShowToolTips(XLegend* self, bool show)
{
    if (!self || self->m_showToolTips == show) return;
    self->m_showToolTips = show;
    xlegend_emitBool(self, (size_t)XLegend_showToolTipsChanged_signal, show);
}

bool XLegend_showToolTips(const XLegend* self)
{ return self ? self->m_showToolTips : false; }

void XLegend_setInteractive(XLegend* self, bool interactive)
{
    if (!self || self->m_interactive == interactive) return;
    self->m_interactive = interactive;
    xlegend_emitBool(self, (size_t)XLegend_interactiveChanged_signal,
                     interactive);
}

bool XLegend_isInteractive(const XLegend* self)
{ return self ? self->m_interactive : false; }

void XLegend_setMarkerShape(XLegend* self, int shape)
{
    if (!self || (int)self->m_markerShape == shape) return;
    self->m_markerShape = (uint8_t)shape;
    xlegend_emitInt(self, (size_t)XLegend_markerShapeChanged_signal, shape);
}

int XLegend_markerShape(const XLegend* self)
{ return self ? (int)self->m_markerShape : 0; }

/* ==================== 信号 ==================== */

void* XLegend_backgroundVisibleChanged_signal(XLegend* self, bool visible)
{
    xlegend_emitBool(self, (size_t)XLegend_backgroundVisibleChanged_signal,
                     visible);
    return (void*)(size_t)XLegend_backgroundVisibleChanged_signal;
}

void* XLegend_colorChanged_signal(XLegend* self, uint32_t color)
{
    xlegend_emitU32(self, (size_t)XLegend_colorChanged_signal, color);
    return (void*)(size_t)XLegend_colorChanged_signal;
}

void* XLegend_borderColorChanged_signal(XLegend* self, uint32_t color)
{
    xlegend_emitU32(self, (size_t)XLegend_borderColorChanged_signal, color);
    return (void*)(size_t)XLegend_borderColorChanged_signal;
}

void* XLegend_fontChanged_signal(XLegend* self, int pointSize)
{
    xlegend_emitInt(self, (size_t)XLegend_fontChanged_signal, pointSize);
    return (void*)(size_t)XLegend_fontChanged_signal;
}

void* XLegend_labelColorChanged_signal(XLegend* self, uint32_t color)
{
    xlegend_emitU32(self, (size_t)XLegend_labelColorChanged_signal, color);
    return (void*)(size_t)XLegend_labelColorChanged_signal;
}

void* XLegend_reverseMarkersChanged_signal(XLegend* self, bool reverseMarkers)
{
    xlegend_emitBool(self, (size_t)XLegend_reverseMarkersChanged_signal,
                     reverseMarkers);
    return (void*)(size_t)XLegend_reverseMarkersChanged_signal;
}

void* XLegend_showToolTipsChanged_signal(XLegend* self, bool showToolTips)
{
    xlegend_emitBool(self, (size_t)XLegend_showToolTipsChanged_signal,
                     showToolTips);
    return (void*)(size_t)XLegend_showToolTipsChanged_signal;
}

void* XLegend_markerShapeChanged_signal(XLegend* self, int shape)
{
    xlegend_emitInt(self, (size_t)XLegend_markerShapeChanged_signal, shape);
    return (void*)(size_t)XLegend_markerShapeChanged_signal;
}

void* XLegend_attachedToChartChanged_signal(XLegend* self, bool attachedToChart)
{
    xlegend_emitBool(self, (size_t)XLegend_attachedToChartChanged_signal,
                     attachedToChart);
    return (void*)(size_t)XLegend_attachedToChartChanged_signal;
}

void* XLegend_interactiveChanged_signal(XLegend* self, bool interactive)
{
    xlegend_emitBool(self, (size_t)XLegend_interactiveChanged_signal,
                     interactive);
    return (void*)(size_t)XLegend_interactiveChanged_signal;
}

#endif /* XCHARTS_ON */

#endif /* XGUI_ON */
