/**
 * @file       XHorizontalStackedBarSeries.c
 * @brief      XHorizontalStackedBarSeries 实现（对标 QHorizontalStackedBarSeries）。
 * @author     XinYueC 团队
 ******************************************************************************/
#if XGUI_ON

#include "XHorizontalStackedBarSeries.h"

#include "XAlgorithm.h"
#include "XMemory.h"

#if XCHARTS_ON

/* ==================== 虚函数实现 ==================== */

static void VXHorizontalStackedBarSeries_deinit(XHorizontalStackedBarSeries* self)
{
    if (!self) return;
    XClass_Deinit_Parent(XAbstractBarSeries, &self->m_base);
}

XVtable* XHorizontalStackedBarSeries_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XHorizontalStackedBarSeries)
    XVTABLE_INHERIT_XCLASS(XAbstractBarSeries);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXHorizontalStackedBarSeries_deinit);
    return XVTABLE_DEFAULT;
}

/* ==================== 生命周期 ==================== */

void XHorizontalStackedBarSeries_init(XHorizontalStackedBarSeries* self)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XAbstractBarSeries_init(&self->m_base);
    XClassSetVtable(self, XHorizontalStackedBarSeries);
    XAbstractSeries_setName_2(&self->m_base.m_base, "horstackedbar");
    self->m_base.m_base.m_type = XChartSeriesType_HorizontalStackedBar;
}

XHorizontalStackedBarSeries* XHorizontalStackedBarSeries_create_ex(XMemoryType memory)
{
    XHorizontalStackedBarSeries* self = (XHorizontalStackedBarSeries*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XHorizontalStackedBarSeries_init(self);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 类型查询 ==================== */

int XHorizontalStackedBarSeries_type(const XHorizontalStackedBarSeries* self)
{
    (void)self;
    return XChartSeriesType_HorizontalStackedBar;
}

#endif /* XCHARTS_ON */

#endif /* XGUI_ON */
