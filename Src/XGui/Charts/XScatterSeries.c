/**
 * @file       XScatterSeries.c
 * @brief      XScatterSeries 散点序列实现（对标 QScatterSeries）。
 * @details    Qt 中 QScatterSeries 覆写 setPen/setBrush/setColor 并在
 *             setColor 时同时更新画刷与画笔边色；本实现保持同语义。
 * @author     XinYueC 团队
 ******************************************************************************/
#if XGUI_ON

#include "XScatterSeries.h"

#include "XAlgorithm.h"
#include "XXYSeries.h"
#include "XMemory.h"
#include "XClass.h"
#include "XVarList.h"
#include "XEventType.h"

#if XCHARTS_ON

/* ==================== 信号发射助手 ==================== */

static bool xscat_canEmit(const XScatterSeries* self)
{
    return self && ((const XObject*)self)->m_signalSlot;
}

static void xscat_emitU32(XScatterSeries* self, size_t signal, uint32_t v)
{
    XVarList* args;
    if (!xscat_canEmit(self)) return;
    args = XVarList_Create(XVar(uint32_t, v));
    if (!args) return;
    XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                       XEVENT_PRIORITY_NORMAL);
}

static void xscat_emitInt(XScatterSeries* self, size_t signal, int v)
{
    XVarList* args;
    if (!xscat_canEmit(self)) return;
    args = XVarList_Create(XVar(int, v));
    if (!args) return;
    XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                       XEVENT_PRIORITY_NORMAL);
}

static void xscat_emitDouble(XScatterSeries* self, size_t signal, double v)
{
    XVarList* args;
    if (!xscat_canEmit(self)) return;
    args = XVarList_Create(XVar(double, v));
    if (!args) return;
    XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                       XEVENT_PRIORITY_NORMAL);
}

/* ==================== 类与对象生命周期 ==================== */

static void VXScatterSeries_deinit(XScatterSeries* self);

static void VXScatterSeries_deinit(XScatterSeries* self)
{
    if (!self) return;
    XClass_Deinit_Parent(XXYSeries, &self->m_base);
}

XVtable* XScatterSeries_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XScatterSeries)
    XVTABLE_INHERIT_XCLASS(XXYSeries);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXScatterSeries_deinit);
    return XVTABLE_DEFAULT;
}

void XScatterSeries_init(XScatterSeries* self)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XXYSeries_init(&self->m_base);
    XClassSetVtable(self, XScatterSeries);
    XAbstractSeries_setName_2(&self->m_base.m_base, "scatter");
    self->m_markerShape = XScatterSeriesMarkerShape_Circle;
    self->m_borderColor = 0;
    self->m_base.m_base.m_type = XChartSeriesType_Scatter;
}

XScatterSeries* XScatterSeries_create_ex(XMemoryType memory)
{
    XScatterSeries* self = (XScatterSeries*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XScatterSeries_init(self);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 类型查询 ==================== */

int XScatterSeries_type(const XScatterSeries* self)
{
    (void)self;
    return XChartSeriesType_Scatter;
}

/* ==================== 标记形状 / 尺寸 / 边框 ==================== */

void XScatterSeries_setMarkerShape(XScatterSeries* self, int shape)
{
    if (!self || self->m_markerShape == shape) return;
    self->m_markerShape = shape;
    xscat_emitInt(self, (size_t)XScatterSeries_markerShapeChanged_signal,
                  shape);
}

int XScatterSeries_markerShape(const XScatterSeries* self)
{ return self ? self->m_markerShape : XScatterSeriesMarkerShape_Circle; }

void XScatterSeries_setBorderColor(XScatterSeries* self, uint32_t color)
{
    if (!self || self->m_borderColor == color) return;
    self->m_borderColor = color;
    xscat_emitU32(self, (size_t)XScatterSeries_borderColorChanged_signal,
                  color);
}

uint32_t XScatterSeries_borderColor(const XScatterSeries* self)
{ return self ? self->m_borderColor : 0; }

void XScatterSeries_setMarkerSize(XScatterSeries* self, double size)
{
    if (!self) return;
    if (size < 0.0) size = 0.0;
    if (self->m_base.m_markerSize == size) return;
    self->m_base.m_markerSize = size;
    xscat_emitDouble(self, (size_t)XScatterSeries_markerSizeChanged_signal,
                     size);
}

double XScatterSeries_markerSize(const XScatterSeries* self)
{ return self ? self->m_base.m_markerSize : 0.0; }

/* ==================== 画笔 / 画刷 / 颜色 ==================== */

void XScatterSeries_setPen(XScatterSeries* self, uint32_t color, double width)
{
    if (!self) return;
    XXYSeries_setPen(&self->m_base, color, width);
}

void XScatterSeries_setBrush(XScatterSeries* self, uint32_t color)
{
    if (!self || self->m_base.m_brush == color) return;
    self->m_base.m_brush = color;
    xscat_emitU32(self, (size_t)XScatterSeries_colorChanged_signal, color);
}

uint32_t XScatterSeries_brush(const XScatterSeries* self)
{ return self ? self->m_base.m_brush : 0; }

void XScatterSeries_setColor(XScatterSeries* self, uint32_t color)
{
    if (!self || self->m_base.m_color == color) return;
    /* Qt：QScatterSeries::setColor 等价「画刷 = color，画笔边色 = color」。 */
    self->m_base.m_color = color;
    self->m_base.m_brush = color;
    self->m_borderColor = color;
    xscat_emitU32(self, (size_t)XScatterSeries_colorChanged_signal, color);
    xscat_emitU32(self, (size_t)XXYSeries_penChanged_signal, color);
}

uint32_t XScatterSeries_color(const XScatterSeries* self)
{ return self ? self->m_base.m_color : 0; }

/* ==================== 信号 ==================== */

void* XScatterSeries_colorChanged_signal(XScatterSeries* self, uint32_t color)
{
    xscat_emitU32(self, (size_t)XScatterSeries_colorChanged_signal, color);
    return (void*)(size_t)XScatterSeries_colorChanged_signal;
}

void* XScatterSeries_borderColorChanged_signal(XScatterSeries* self,
                                               uint32_t color)
{
    xscat_emitU32(self, (size_t)XScatterSeries_borderColorChanged_signal,
                  color);
    return (void*)(size_t)XScatterSeries_borderColorChanged_signal;
}

void* XScatterSeries_markerShapeChanged_signal(XScatterSeries* self, int shape)
{
    xscat_emitInt(self, (size_t)XScatterSeries_markerShapeChanged_signal,
                  shape);
    return (void*)(size_t)XScatterSeries_markerShapeChanged_signal;
}

void* XScatterSeries_markerSizeChanged_signal(XScatterSeries* self, double size)
{
    xscat_emitDouble(self, (size_t)XScatterSeries_markerSizeChanged_signal,
                     size);
    return (void*)(size_t)XScatterSeries_markerSizeChanged_signal;
}

#endif /* XCHARTS_ON */

#endif /* XGUI_ON */
