/**
 * @file       XAreaLegendMarker.c
 * @brief      XAreaLegendMarker 实现（对标 QAreaLegendMarker）。
 * @author     XinYueC 团队
 ******************************************************************************/
#if XGUI_ON

#include "XAreaLegendMarker.h"

#include "XAlgorithm.h"
#include "XMemory.h"

#if XCHARTS_ON

/* ==================== 虚函数实现 ==================== */

static void VXAreaLegendMarker_deinit(XAreaLegendMarker* self)
{
    if (!self) return;
    XClass_Deinit_Parent(XLegendMarker, &self->m_base);
}

XVtable* XAreaLegendMarker_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XAreaLegendMarker)
    XVTABLE_INHERIT_XCLASS(XLegendMarker);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXAreaLegendMarker_deinit);
    return XVTABLE_DEFAULT;
}

/* ==================== 生命周期 ==================== */

void XAreaLegendMarker_init(XAreaLegendMarker* self, XAreaSeries* series)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XLegendMarker_init(&self->m_base, (XAbstractSeries*)series,
                       XLegendMarker_LegendMarkerType_Area);
    XClassSetVtable(self, XAreaLegendMarker);
}

XAreaLegendMarker* XAreaLegendMarker_create_ex(XMemoryType memory, XAreaSeries* series)
{
    XAreaLegendMarker* self = (XAreaLegendMarker*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XAreaLegendMarker_init(self, series);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 类型查询 ==================== */

int XAreaLegendMarker_type(const XAreaLegendMarker* self)
{
    (void)self;
    return XLegendMarker_LegendMarkerType_Area;
}

#endif /* XCHARTS_ON */

#endif /* XGUI_ON */
