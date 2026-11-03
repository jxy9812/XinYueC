/**
 * @file       XCandlestickLegendMarker.h
 * @brief      XCandlestickLegendMarker K 线序列图例标记（对标 Qt Charts 6.8 QCandlestickLegendMarker）。
 * @details    继承 XLegendMarker（对应 QCandlestickLegendMarker : QLegendMarker）；固定标记
 *             类型为 Candlestick，并保留 Qt 中该类持有的归属对象。
 * @note       模块总开关 XCHARTS_ON。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XCANDLESTICKLEGENDMARKER_H
#define XCANDLESTICKLEGENDMARKER_H

#if XGUI_ON
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XLegendMarker.h"
#include "XCandlestickSeries.h"

#if XCHARTS_ON

XCLASS_DEFINE_BEGING(XCandlestickLegendMarker)
XCLASS_DEFINE_EXTEND_END(XCandlestickLegendMarker, XLegendMarker)

/** @brief QCandlestickLegendMarker 对应实现。 */
typedef struct XCandlestickLegendMarker
{
    XLegendMarker m_base;   /**< 基类成员；必须是第一个。 */

} XCandlestickLegendMarker;

/** @brief 初始化类虚函数表。 @return 共享虚函数表指针。 */
XVtable* XCandlestickLegendMarker_class_init(void);

/**
 * @brief 初始化嵌入式K 线序列图例标记。
 * @param self 目标标记指针，不能为空。
 * @param series 归属序列（借用，不转移所有权）；可为 NULL。
 * @return 无返回值。
 */
void XCandlestickLegendMarker_init(XCandlestickLegendMarker* self, XCandlestickSeries* series);

/**
 * @brief 堆上创建K 线序列图例标记。
 * @param memory 内存类型。
 * @param series 归属序列（借用）；可为 NULL。
 * @return 标记指针；分配失败返回 NULL。
 */
XCandlestickLegendMarker* XCandlestickLegendMarker_create_ex(XMemoryType memory, XCandlestickSeries* series);
#define XCandlestickLegendMarker_create(series) \
    XCandlestickLegendMarker_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, series)

/** @brief 查询标记类型（恒定 Candlestick）。 @param self 目标标记指针。 @return XLegendMarker_LegendMarkerType_Candlestick。 */
int XCandlestickLegendMarker_type(const XCandlestickLegendMarker* self);

#endif /* XCHARTS_ON */
#ifdef __cplusplus
}
#endif

#endif /* XGUI_ON */

#endif /* XCANDLESTICKLEGENDMARKER_H */
