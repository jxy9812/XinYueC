#include "XSplineSeries.h"

#include "XAlgorithm.h"
#include "XXYSeries.h"
#include "XMemory.h"
#include "XClass.h"

#if XCHARTS_ON

static void VXSplineSeries_deinit(XSplineSeries* self);

static void VXSplineSeries_deinit(XSplineSeries* self)
{
    if (!self) return;
    XClass_Deinit_Parent(XXYSeries, &self->m_base);
}

XVtable* XSplineSeries_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XSplineSeries)
    XVTABLE_INHERIT_XCLASS(XXYSeries);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXSplineSeries_deinit);
    return XVTABLE_DEFAULT;
}

void XSplineSeries_init(XSplineSeries* self)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XXYSeries_init(&self->m_base);
    XClassSetVtable(self, XSplineSeries);
    XAbstractSeries_setName(&self->m_base.m_base, "spline");
    self->m_base.m_base.m_type = XChartSeriesType_Spline;
}

XSplineSeries* XSplineSeries_create_ex(XMemoryType memory)
{
    XSplineSeries* self = (XSplineSeries*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XSplineSeries_init(self);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

#endif /* XCHARTS_ON */
