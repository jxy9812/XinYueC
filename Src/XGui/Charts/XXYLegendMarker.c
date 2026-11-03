/**
 * @file       XXYLegendMarker.c
 * @brief      XXYLegendMarker 实现（对标 QXYLegendMarker）。
 * @author     XinYueC 团队
 ******************************************************************************/
#if XGUI_ON

#include "XXYLegendMarker.h"

#include "XAlgorithm.h"
#include "XMemory.h"

#if XCHARTS_ON

/* ==================== 虚函数实现 ==================== */

static void VXXYLegendMarker_deinit(XXYLegendMarker* self)
{
    if (!self) return;
    XClass_Deinit_Parent(XLegendMarker, &self->m_base);
}

XVtable* XXYLegendMarker_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XXYLegendMarker)
    XVTABLE_INHERIT_XCLASS(XLegendMarker);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXXYLegendMarker_deinit);
    return XVTABLE_DEFAULT;
}

/* ==================== 生命周期 ==================== */

void XXYLegendMarker_init(XXYLegendMarker* self, XXYSeries* series)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XLegendMarker_init(&self->m_base, (XAbstractSeries*)series,
                       XLegendMarker_LegendMarkerType_XY);
    XClassSetVtable(self, XXYLegendMarker);
}

XXYLegendMarker* XXYLegendMarker_create_ex(XMemoryType memory, XXYSeries* series)
{
    XXYLegendMarker* self = (XXYLegendMarker*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XXYLegendMarker_init(self, series);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 类型查询 ==================== */

int XXYLegendMarker_type(const XXYLegendMarker* self)
{
    (void)self;
    return XLegendMarker_LegendMarkerType_XY;
}

#endif /* XCHARTS_ON */

#endif /* XGUI_ON */
