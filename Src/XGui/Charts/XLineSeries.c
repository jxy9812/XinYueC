#include "XLineSeries.h"

#include "XAlgorithm.h"
#include "XXYSeries.h"
#include "XMemory.h"
#include "XClass.h"

#if XCHARTS_ON

static void VXLineSeries_deinit(XLineSeries* self);

static void VXLineSeries_deinit(XLineSeries* self)
{
    if (!self) return;
    XClass_Deinit_Parent(XXYSeries, &self->m_base);
}

XVtable* XLineSeries_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XLineSeries)
    XVTABLE_INHERIT_XCLASS(XXYSeries);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXLineSeries_deinit);
    return XVTABLE_DEFAULT;
}

void XLineSeries_init(XLineSeries* self)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XXYSeries_init(&self->m_base);
    XClassSetVtable(self, XLineSeries);
    XAbstractSeries_setName_2(&self->m_base.m_base, "line");
    self->m_base.m_base.m_type = XChartSeriesType_Line;
}

XLineSeries* XLineSeries_create_ex(XMemoryType memory)
{
    XLineSeries* self = (XLineSeries*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XLineSeries_init(self);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

#endif /* XCHARTS_ON */
