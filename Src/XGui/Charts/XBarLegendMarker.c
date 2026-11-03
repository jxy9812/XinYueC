/**
 * @file       XBarLegendMarker.c
 * @brief      XBarLegendMarker 实现（对标 QBarLegendMarker）。
 * @author     XinYueC 团队
 ******************************************************************************/
#if XGUI_ON

#include "XBarLegendMarker.h"

#include "XAlgorithm.h"
#include "XMemory.h"

#if XCHARTS_ON

/* ==================== 虚函数实现 ==================== */

static void VXBarLegendMarker_deinit(XBarLegendMarker* self)
{
    if (!self) return;
    XClass_Deinit_Parent(XLegendMarker, &self->m_base);
}

XVtable* XBarLegendMarker_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XBarLegendMarker)
    XVTABLE_INHERIT_XCLASS(XLegendMarker);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXBarLegendMarker_deinit);
    return XVTABLE_DEFAULT;
}

/* ==================== 生命周期 ==================== */

void XBarLegendMarker_init(XBarLegendMarker* self, XAbstractBarSeries* series)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XLegendMarker_init(&self->m_base, (XAbstractSeries*)series,
                       XLegendMarker_LegendMarkerType_Bar);
    XClassSetVtable(self, XBarLegendMarker);
    /* 柱组指针由调用方在 setMarkerBarSet 语义下绑定。 */
}

XBarLegendMarker* XBarLegendMarker_create_ex(XMemoryType memory, XAbstractBarSeries* series)
{
    XBarLegendMarker* self = (XBarLegendMarker*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XBarLegendMarker_init(self, series);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 类型查询 ==================== */

int XBarLegendMarker_type(const XBarLegendMarker* self)
{
    (void)self;
    return XLegendMarker_LegendMarkerType_Bar;
}

XBarSet* XBarLegendMarker_barset(const XBarLegendMarker* self)
{
    return self ? self->m_barSet : NULL;
}

XBarSet* XBarLegendMarker_barSet(const XBarLegendMarker* self)
{ return XBarLegendMarker_barset(self); }

void XBarLegendMarker_setBarSet(XBarLegendMarker* self, XBarSet* set)
{
    if (self) self->m_barSet = set;
}

#endif /* XCHARTS_ON */

#endif /* XGUI_ON */
