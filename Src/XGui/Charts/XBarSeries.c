#include "XBarSeries.h"
#include "XAbstractBarSeries.h"
#include "XString.h"
#include "XMemory.h"
#include "XClass.h"
#include <string.h>

#if XCHARTS_ON

static void VXBarSeries_deinit(XBarSeries* self);

XVtable* XBarSeries_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XBarSeries)
    XVTABLE_INHERIT_XCLASS(XAbstractBarSeries);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXBarSeries_deinit);
    return XVTABLE_DEFAULT;
}

void XBarSeries_init(XBarSeries* self)
{
    if (!self) return;
    memset(self, 0, sizeof(*self));
    XAbstractBarSeries_init(&self->m_base);
    XClassSetVtable(self, XBarSeries);
    XAbstractSeries_setName(&self->m_base.m_base, "bar");
    self->m_base.m_base.m_type = XChartSeriesType_Bar;
}

XBarSeries* XBarSeries_create_ex(XMemoryType memory)
{
    XBarSeries* self = (XBarSeries*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XBarSeries_init(self);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

static void VXBarSeries_deinit(XBarSeries* self)
{
    if (!self) return;
    XClass_Deinit_Parent(XAbstractBarSeries, (XAbstractBarSeries*)self);
}

void XBarSeries_setColor(XBarSeries* self, uint32_t color)
{ if (self) self->m_color = color; }

uint32_t XBarSeries_color(const XBarSeries* self)
{ return self ? self->m_color : 0; }

#endif /* XCHARTS_ON */
