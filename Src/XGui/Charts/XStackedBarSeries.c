/**
 * @file       XStackedBarSeries.c
 * @brief      XStackedBarSeries 实现（对标 QStackedBarSeries）。
 * @author     XinYueC 团队
 ******************************************************************************/
#if XGUI_ON

#include "XStackedBarSeries.h"

#include "XAlgorithm.h"
#include "XMemory.h"

#if XCHARTS_ON

/* ==================== 虚函数实现 ==================== */

static void VXStackedBarSeries_deinit(XStackedBarSeries* self)
{
    if (!self) return;
    XClass_Deinit_Parent(XAbstractBarSeries, &self->m_base);
}

XVtable* XStackedBarSeries_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XStackedBarSeries)
    XVTABLE_INHERIT_XCLASS(XAbstractBarSeries);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXStackedBarSeries_deinit);
    return XVTABLE_DEFAULT;
}

/* ==================== 生命周期 ==================== */

void XStackedBarSeries_init(XStackedBarSeries* self)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XAbstractBarSeries_init(&self->m_base);
    XClassSetVtable(self, XStackedBarSeries);
    XAbstractSeries_setName_2(&self->m_base.m_base, "stackedbar");
    self->m_base.m_base.m_type = XChartSeriesType_StackedBar;
}

XStackedBarSeries* XStackedBarSeries_create_ex(XMemoryType memory)
{
    XStackedBarSeries* self = (XStackedBarSeries*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XStackedBarSeries_init(self);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 类型查询 ==================== */

int XStackedBarSeries_type(const XStackedBarSeries* self)
{
    (void)self;
    return XChartSeriesType_StackedBar;
}

#endif /* XCHARTS_ON */

#endif /* XGUI_ON */
