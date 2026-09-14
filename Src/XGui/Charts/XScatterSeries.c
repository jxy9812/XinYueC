#include "XScatterSeries.h"
#include "XXYSeries.h"
#include "XMemory.h"
#include "XClass.h"
#include <string.h>

#if XCHARTS_ON

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
    memset(self, 0, sizeof(*self));
    XXYSeries_init(&self->m_base);
    XClassSetVtable(self, XScatterSeries);
    XAbstractSeries_setName(&self->m_base.m_base, "scatter");
    self->m_markerShape = XScatterSeriesMarkerShape_Circle;
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

void XScatterSeries_setMarkerShape(XScatterSeries* self, int shape)
{ if (self) self->m_markerShape = shape; }

int XScatterSeries_markerShape(const XScatterSeries* self)
{ return self ? self->m_markerShape : XScatterSeriesMarkerShape_Circle; }

void XScatterSeries_setBorderColor(XScatterSeries* self, uint32_t color)
{ if (self) self->m_borderColor = color; }

uint32_t XScatterSeries_borderColor(const XScatterSeries* self)
{ return self ? self->m_borderColor : 0; }

#endif /* XCHARTS_ON */
