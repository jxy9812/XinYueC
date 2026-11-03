/**
 * @file       XHorizontalPercentBarSeries.c
 * @brief      XHorizontalPercentBarSeries 实现（对标 QHorizontalPercentBarSeries）。
 * @author     XinYueC 团队
 ******************************************************************************/
#if XGUI_ON

#include "XHorizontalPercentBarSeries.h"

#include "XAlgorithm.h"
#include "XMemory.h"

#if XCHARTS_ON

/* ==================== 虚函数实现 ==================== */

static void VXHorizontalPercentBarSeries_deinit(XHorizontalPercentBarSeries* self)
{
    if (!self) return;
    XClass_Deinit_Parent(XAbstractBarSeries, &self->m_base);
}

XVtable* XHorizontalPercentBarSeries_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XHorizontalPercentBarSeries)
    XVTABLE_INHERIT_XCLASS(XAbstractBarSeries);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXHorizontalPercentBarSeries_deinit);
    return XVTABLE_DEFAULT;
}

/* ==================== 生命周期 ==================== */

void XHorizontalPercentBarSeries_init(XHorizontalPercentBarSeries* self)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XAbstractBarSeries_init(&self->m_base);
    XClassSetVtable(self, XHorizontalPercentBarSeries);
    XAbstractSeries_setName_2(&self->m_base.m_base, "horpercentbar");
    self->m_base.m_base.m_type = XChartSeriesType_HorizontalPercentBar;
}

XHorizontalPercentBarSeries* XHorizontalPercentBarSeries_create_ex(XMemoryType memory)
{
    XHorizontalPercentBarSeries* self = (XHorizontalPercentBarSeries*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XHorizontalPercentBarSeries_init(self);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 类型查询 ==================== */

int XHorizontalPercentBarSeries_type(const XHorizontalPercentBarSeries* self)
{
    (void)self;
    return XChartSeriesType_HorizontalPercentBar;
}

#endif /* XCHARTS_ON */

#endif /* XGUI_ON */
