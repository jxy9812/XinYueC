/**
 * @file       XLegendMarker.c
 * @brief      XLegendMarker 图例标记实现（对标 QLegendMarker）。
 * @author     XinYueC 团队
 ******************************************************************************/
#if XGUI_ON

#include "XLegendMarker.h"

#include "XAlgorithm.h"
#include "XMemory.h"
#include "XVarList.h"
#include "XEventType.h"

#if XCHARTS_ON

/* ==================== 信号发射助手 ==================== */

static bool xlm_canEmit(const XLegendMarker* self)
{
    return self && ((const XObject*)self)->m_signalSlot;
}

static void xlm_emitVoid(XLegendMarker* self, size_t signal)
{
    XVarList* args;
    int dummy = 0;
    if (!xlm_canEmit(self)) return;
    args = XVarList_Create(XVar(int, dummy));
    if (!args) return;
    XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                       XEVENT_PRIORITY_NORMAL);
}

static void xlm_emitBool(XLegendMarker* self, size_t signal, bool v)
{
    XVarList* args;
    if (!xlm_canEmit(self)) return;
    args = XVarList_Create(XVar(bool, v));
    if (!args) return;
    XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                       XEVENT_PRIORITY_NORMAL);
}

static void xlm_emitInt(XLegendMarker* self, size_t signal, int v)
{
    XVarList* args;
    if (!xlm_canEmit(self)) return;
    args = XVarList_Create(XVar(int, v));
    if (!args) return;
    XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                       XEVENT_PRIORITY_NORMAL);
}

static void xlm_emitU32(XLegendMarker* self, size_t signal, uint32_t v)
{
    XVarList* args;
    if (!xlm_canEmit(self)) return;
    args = XVarList_Create(XVar(uint32_t, v));
    if (!args) return;
    XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                       XEVENT_PRIORITY_NORMAL);
}

static void xlm_emitPen(XLegendMarker* self, size_t signal, uint32_t color,
                        double width)
{
    XVarList* args;
    if (!xlm_canEmit(self)) return;
    args = XVarList_Create(XVar(uint32_t, color), XVar(double, width));
    if (!args) return;
    XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                       XEVENT_PRIORITY_NORMAL);
}

/* ==================== 虚函数实现 ==================== */

static void VXLegendMarker_deinit(XLegendMarker* self)
{
    if (!self) return;
    if (self->m_label) {
        XClassDelete(self->m_label);
        self->m_label = NULL;
    }
    XClass_Deinit_Parent(XObject, (XObject*)self);
}

XVtable* XLegendMarker_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XLegendMarker)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXLegendMarker_deinit);
    return XVTABLE_DEFAULT;
}

/* ==================== 生命周期 ==================== */

void XLegendMarker_init(XLegendMarker* self, XAbstractSeries* series, int type)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XObject_init((XObject*)self);
    XClassSetVtable(self, XLegendMarker);
    self->m_label = XString_create();
    self->m_series = series;
    self->m_markerType = (uint8_t)type;
    self->m_visible = true;
    self->m_penWidth = 1.0;
    /* Qt 默认标签 = 序列名（markerLabel）。 */
    if (series)
        XLegendMarker_setLabel(self, XAbstractSeries_name(series));
}

XLegendMarker* XLegendMarker_create_ex(XMemoryType memory,
                                       XAbstractSeries* series, int type)
{
    XLegendMarker* self =
        (XLegendMarker*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XLegendMarker_init(self, series, type);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 类型 / 归属 ==================== */

int XLegendMarker_type(const XLegendMarker* self)
{ return self ? (int)self->m_markerType : 0; }

XAbstractSeries* XLegendMarker_series(const XLegendMarker* self)
{ return self ? self->m_series : NULL; }

/* ==================== 标签 ==================== */

void XLegendMarker_setLabel(XLegendMarker* self, const XString* label)
{
    if (!self) return;
    if (!self->m_label) self->m_label = XString_create();
    if (!self->m_label) return;
    if (label)
        XString_assign(self->m_label, label);
    else
        XString_assign_utf8(self->m_label, "");
    xlm_emitVoid(self, (size_t)XLegendMarker_labelChanged_signal);
}

void XLegendMarker_setLabel_utf8(XLegendMarker* self, const char* label)
{
    XString* tmp = NULL;
    if (label) {
        tmp = XString_create_utf8(label);
        if (!tmp) return;
    }
    XLegendMarker_setLabel(self, tmp);
    if (tmp) XClassDelete(tmp);
}

void XLegendMarker_setLabel_2(XLegendMarker* self, const char* label)
{ XLegendMarker_setLabel_utf8(self, label); }

const XString* XLegendMarker_label_const(const XLegendMarker* self)
{ return (self && self->m_label) ? self->m_label : NULL; }

const XString* XLegendMarker_label(const XLegendMarker* self)
{ return XLegendMarker_label_const(self); }

const char* XLegendMarker_label_toUtf8(const XLegendMarker* self)
{
    const XString* s = XLegendMarker_label_const(self);
    return s ? XString_toUtf8(s) : "";
}

/* ==================== 外观 ==================== */

void XLegendMarker_setLabelBrush(XLegendMarker* self, uint32_t color)
{
    if (!self || self->m_labelBrushColor == color) return;
    self->m_labelBrushColor = color;
    xlm_emitU32(self, (size_t)XLegendMarker_labelBrushChanged_signal, color);
}

uint32_t XLegendMarker_labelBrush(const XLegendMarker* self)
{ return self ? self->m_labelBrushColor : 0; }

void XLegendMarker_setFont(XLegendMarker* self, const XString* family,
                           int pointSize)
{
    (void)family;   /* 字体族在 XFont 层承载；此处只记录字号（Qt 对齐口径）。 */
    if (!self) return;
    if (pointSize < 0) pointSize = 0;
    if (self->m_fontSize == pointSize) return;
    self->m_fontSize = pointSize;
    xlm_emitInt(self, (size_t)XLegendMarker_fontChanged_signal, pointSize);
}

void XLegendMarker_setFont_utf8(XLegendMarker* self, const char* family,
                                int pointSize)
{
    XString* tmp = NULL;
    if (family) {
        tmp = XString_create_utf8(family);
        if (!tmp) return;
    }
    XLegendMarker_setFont(self, tmp, pointSize);
    if (tmp) XClassDelete(tmp);
}

void XLegendMarker_setFont_2(XLegendMarker* self, const char* family,
                             int pointSize)
{ XLegendMarker_setFont_utf8(self, family, pointSize); }

int XLegendMarker_fontSize(const XLegendMarker* self)
{ return self ? self->m_fontSize : 0; }

void XLegendMarker_setPen(XLegendMarker* self, uint32_t color, double width)
{
    if (!self) return;
    if (width < 0.0) width = 0.0;
    if (self->m_penColor == color && self->m_penWidth == width) return;
    self->m_penColor = color;
    self->m_penWidth = width;
    xlm_emitPen(self, (size_t)XLegendMarker_penChanged_signal, color, width);
}

void XLegendMarker_pen(const XLegendMarker* self, uint32_t* color,
                       double* width)
{
    if (!self) return;
    if (color) *color = self->m_penColor;
    if (width) *width = self->m_penWidth;
}

void XLegendMarker_setBrush(XLegendMarker* self, uint32_t color)
{
    if (!self || self->m_brushColor == color) return;
    self->m_brushColor = color;
    xlm_emitU32(self, (size_t)XLegendMarker_brushChanged_signal, color);
}

uint32_t XLegendMarker_brush(const XLegendMarker* self)
{ return self ? self->m_brushColor : 0; }

/* ==================== 可见性 ==================== */

void XLegendMarker_setVisible(XLegendMarker* self, bool visible)
{
    if (!self || self->m_visible == visible) return;
    self->m_visible = visible;
    /* Qt：标记可见性同时驱动序列可见性（marker -> series 双向联动）。 */
    if (self->m_series) XAbstractSeries_setVisible(self->m_series, visible);
    xlm_emitBool(self, (size_t)XLegendMarker_visibleChanged_signal, visible);
}

bool XLegendMarker_isVisible(const XLegendMarker* self)
{ return self ? self->m_visible : false; }

/* ==================== 形状 ==================== */

void XLegendMarker_setShape(XLegendMarker* self, int shape)
{
    if (!self) return;
    /* 形状由 XLegend 统一管理，标记侧只记录（沿用 markerType 高位段）。 */
    (void)shape;
}

int XLegendMarker_shape(const XLegendMarker* self)
{
    (void)self;
    return 0;   /* XLegend_MarkerShape_Default */
}

/* ==================== 信号 ==================== */

void* XLegendMarker_clicked_signal(XLegendMarker* self)
{
    xlm_emitVoid(self, (size_t)XLegendMarker_clicked_signal);
    return (void*)(size_t)XLegendMarker_clicked_signal;
}

void* XLegendMarker_hovered_signal(XLegendMarker* self, bool status)
{
    xlm_emitBool(self, (size_t)XLegendMarker_hovered_signal, status);
    return (void*)(size_t)XLegendMarker_hovered_signal;
}

void* XLegendMarker_labelChanged_signal(XLegendMarker* self)
{
    xlm_emitVoid(self, (size_t)XLegendMarker_labelChanged_signal);
    return (void*)(size_t)XLegendMarker_labelChanged_signal;
}

void* XLegendMarker_labelBrushChanged_signal(XLegendMarker* self,
                                             uint32_t color)
{
    xlm_emitU32(self, (size_t)XLegendMarker_labelBrushChanged_signal, color);
    return (void*)(size_t)XLegendMarker_labelBrushChanged_signal;
}

void* XLegendMarker_fontChanged_signal(XLegendMarker* self, int pointSize)
{
    xlm_emitInt(self, (size_t)XLegendMarker_fontChanged_signal, pointSize);
    return (void*)(size_t)XLegendMarker_fontChanged_signal;
}

void* XLegendMarker_penChanged_signal(XLegendMarker* self, uint32_t color,
                                      double width)
{
    xlm_emitPen(self, (size_t)XLegendMarker_penChanged_signal, color, width);
    return (void*)(size_t)XLegendMarker_penChanged_signal;
}

void* XLegendMarker_brushChanged_signal(XLegendMarker* self, uint32_t color)
{
    xlm_emitU32(self, (size_t)XLegendMarker_brushChanged_signal, color);
    return (void*)(size_t)XLegendMarker_brushChanged_signal;
}

void* XLegendMarker_visibleChanged_signal(XLegendMarker* self, bool visible)
{
    xlm_emitBool(self, (size_t)XLegendMarker_visibleChanged_signal, visible);
    return (void*)(size_t)XLegendMarker_visibleChanged_signal;
}

void* XLegendMarker_shapeChanged_signal(XLegendMarker* self, int shape)
{
    xlm_emitInt(self, (size_t)XLegendMarker_shapeChanged_signal, shape);
    return (void*)(size_t)XLegendMarker_shapeChanged_signal;
}

#endif /* XCHARTS_ON */

#endif /* XGUI_ON */
