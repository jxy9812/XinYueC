/**
 * @file       XPieLegendMarker.c
 * @brief      XPieLegendMarker 实现（对标 QPieLegendMarker）。
 * @author     XinYueC 团队
 ******************************************************************************/
#if XGUI_ON

#include "XPieLegendMarker.h"

#include "XAlgorithm.h"
#include "XMemory.h"

#if XCHARTS_ON

/* ==================== 虚函数实现 ==================== */

static void VXPieLegendMarker_deinit(XPieLegendMarker* self)
{
    if (!self) return;
    XClass_Deinit_Parent(XLegendMarker, &self->m_base);
}

XVtable* XPieLegendMarker_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XPieLegendMarker)
    XVTABLE_INHERIT_XCLASS(XLegendMarker);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXPieLegendMarker_deinit);
    return XVTABLE_DEFAULT;
}

/* ==================== 生命周期 ==================== */

void XPieLegendMarker_init(XPieLegendMarker* self, XPieSeries* series)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XLegendMarker_init(&self->m_base, (XAbstractSeries*)series,
                       XLegendMarker_LegendMarkerType_Pie);
    XClassSetVtable(self, XPieLegendMarker);
}

XPieLegendMarker* XPieLegendMarker_create_ex(XMemoryType memory, XPieSeries* series)
{
    XPieLegendMarker* self = (XPieLegendMarker*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XPieLegendMarker_init(self, series);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 类型查询 ==================== */

int XPieLegendMarker_type(const XPieLegendMarker* self)
{
    (void)self;
    return XLegendMarker_LegendMarkerType_Pie;
}

XPieSlice* XPieLegendMarker_slice(const XPieLegendMarker* self)
{
    return self ? self->m_slice : NULL;
}

void XPieLegendMarker_setSlice(XPieLegendMarker* self, XPieSlice* slice)
{
    if (self) self->m_slice = slice;
}

#endif /* XCHARTS_ON */

#endif /* XGUI_ON */
