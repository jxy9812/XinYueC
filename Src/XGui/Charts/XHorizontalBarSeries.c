/**
 * @file       XHorizontalBarSeries.c
 * @brief      XHorizontalBarSeries 实现（对标 QHorizontalBarSeries）。
 * @author     XinYueC 团队
 ******************************************************************************/
#if XGUI_ON

#include "XHorizontalBarSeries.h"

#include "XAlgorithm.h"
#include "XMemory.h"

#if XCHARTS_ON

/* ==================== 虚函数实现 ==================== */

static void VXHorizontalBarSeries_deinit(XHorizontalBarSeries* self)
{
    if (!self) return;
    XClass_Deinit_Parent(XAbstractBarSeries, &self->m_base);
}

XVtable* XHorizontalBarSeries_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XHorizontalBarSeries)
    XVTABLE_INHERIT_XCLASS(XAbstractBarSeries);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXHorizontalBarSeries_deinit);
    return XVTABLE_DEFAULT;
}

/* ==================== 生命周期 ==================== */

void XHorizontalBarSeries_init(XHorizontalBarSeries* self)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XAbstractBarSeries_init(&self->m_base);
    XClassSetVtable(self, XHorizontalBarSeries);
    XAbstractSeries_setName_2(&self->m_base.m_base, "horbarseries");
    self->m_base.m_base.m_type = XChartSeriesType_HorizontalBar;
}

XHorizontalBarSeries* XHorizontalBarSeries_create_ex(XMemoryType memory)
{
    XHorizontalBarSeries* self = (XHorizontalBarSeries*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XHorizontalBarSeries_init(self);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 类型查询 ==================== */

int XHorizontalBarSeries_type(const XHorizontalBarSeries* self)
{
    (void)self;
    return XChartSeriesType_HorizontalBar;
}

#endif /* XCHARTS_ON */

#endif /* XGUI_ON */
