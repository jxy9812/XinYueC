/**
 * @file       XBoxPlotLegendMarker.c
 * @brief      XBoxPlotLegendMarker 实现（对标 QBoxPlotLegendMarker）。
 * @author     XinYueC 团队
 ******************************************************************************/
#if XGUI_ON

#include "XBoxPlotLegendMarker.h"

#include "XAlgorithm.h"
#include "XMemory.h"

#if XCHARTS_ON

/* ==================== 虚函数实现 ==================== */

static void VXBoxPlotLegendMarker_deinit(XBoxPlotLegendMarker* self)
{
    if (!self) return;
    XClass_Deinit_Parent(XLegendMarker, &self->m_base);
}

XVtable* XBoxPlotLegendMarker_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XBoxPlotLegendMarker)
    XVTABLE_INHERIT_XCLASS(XLegendMarker);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXBoxPlotLegendMarker_deinit);
    return XVTABLE_DEFAULT;
}

/* ==================== 生命周期 ==================== */

void XBoxPlotLegendMarker_init(XBoxPlotLegendMarker* self, XBoxPlotSeries* series)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XLegendMarker_init(&self->m_base, (XAbstractSeries*)series,
                       XLegendMarker_LegendMarkerType_BoxPlot);
    XClassSetVtable(self, XBoxPlotLegendMarker);
}

XBoxPlotLegendMarker* XBoxPlotLegendMarker_create_ex(XMemoryType memory, XBoxPlotSeries* series)
{
    XBoxPlotLegendMarker* self = (XBoxPlotLegendMarker*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XBoxPlotLegendMarker_init(self, series);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 类型查询 ==================== */

int XBoxPlotLegendMarker_type(const XBoxPlotLegendMarker* self)
{
    (void)self;
    return XLegendMarker_LegendMarkerType_BoxPlot;
}

#endif /* XCHARTS_ON */

#endif /* XGUI_ON */
