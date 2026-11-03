/**
 * @file       XPercentBarSeries.c
 * @brief      XPercentBarSeries 实现（对标 QPercentBarSeries）。
 * @author     XinYueC 团队
 ******************************************************************************/
#if XGUI_ON

#include "XPercentBarSeries.h"

#include "XAlgorithm.h"
#include "XMemory.h"

#if XCHARTS_ON

/* ==================== 虚函数实现 ==================== */

static void VXPercentBarSeries_deinit(XPercentBarSeries* self)
{
    if (!self) return;
    XClass_Deinit_Parent(XAbstractBarSeries, &self->m_base);
}

XVtable* XPercentBarSeries_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XPercentBarSeries)
    XVTABLE_INHERIT_XCLASS(XAbstractBarSeries);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXPercentBarSeries_deinit);
    return XVTABLE_DEFAULT;
}

/* ==================== 生命周期 ==================== */

void XPercentBarSeries_init(XPercentBarSeries* self)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XAbstractBarSeries_init(&self->m_base);
    XClassSetVtable(self, XPercentBarSeries);
    XAbstractSeries_setName_2(&self->m_base.m_base, "percentbar");
    self->m_base.m_base.m_type = XChartSeriesType_PercentBar;
}

XPercentBarSeries* XPercentBarSeries_create_ex(XMemoryType memory)
{
    XPercentBarSeries* self = (XPercentBarSeries*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XPercentBarSeries_init(self);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 类型查询 ==================== */

int XPercentBarSeries_type(const XPercentBarSeries* self)
{
    (void)self;
    return XChartSeriesType_PercentBar;
}

#endif /* XCHARTS_ON */

#endif /* XGUI_ON */
