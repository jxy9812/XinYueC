/**
 * @file       XCandlestickLegendMarker.c
 * @brief      XCandlestickLegendMarker 实现（对标 QCandlestickLegendMarker）。
 * @author     XinYueC 团队
 ******************************************************************************/
#if XGUI_ON

#include "XCandlestickLegendMarker.h"

#include "XAlgorithm.h"
#include "XMemory.h"

#if XCHARTS_ON

/* ==================== 虚函数实现 ==================== */

static void VXCandlestickLegendMarker_deinit(XCandlestickLegendMarker* self)
{
    if (!self) return;
    XClass_Deinit_Parent(XLegendMarker, &self->m_base);
}

XVtable* XCandlestickLegendMarker_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XCandlestickLegendMarker)
    XVTABLE_INHERIT_XCLASS(XLegendMarker);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXCandlestickLegendMarker_deinit);
    return XVTABLE_DEFAULT;
}

/* ==================== 生命周期 ==================== */

void XCandlestickLegendMarker_init(XCandlestickLegendMarker* self, XCandlestickSeries* series)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XLegendMarker_init(&self->m_base, (XAbstractSeries*)series,
                       XLegendMarker_LegendMarkerType_Candlestick);
    XClassSetVtable(self, XCandlestickLegendMarker);
}

XCandlestickLegendMarker* XCandlestickLegendMarker_create_ex(XMemoryType memory, XCandlestickSeries* series)
{
    XCandlestickLegendMarker* self = (XCandlestickLegendMarker*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XCandlestickLegendMarker_init(self, series);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 类型查询 ==================== */

int XCandlestickLegendMarker_type(const XCandlestickLegendMarker* self)
{
    (void)self;
    return XLegendMarker_LegendMarkerType_Candlestick;
}

#endif /* XCHARTS_ON */

#endif /* XGUI_ON */
