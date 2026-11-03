/**
 * @file       XBoxPlotLegendMarker.h
 * @brief      XBoxPlotLegendMarker 盒须序列图例标记（对标 Qt Charts 6.8 QBoxPlotLegendMarker）。
 * @details    继承 XLegendMarker（对应 QBoxPlotLegendMarker : QLegendMarker）；固定标记
 *             类型为 BoxPlot，并保留 Qt 中该类持有的归属对象。
 * @note       模块总开关 XCHARTS_ON。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XBOXPLOTLEGENDMARKER_H
#define XBOXPLOTLEGENDMARKER_H

#if XGUI_ON
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XLegendMarker.h"
#include "XBoxPlotSeries.h"

#if XCHARTS_ON

XCLASS_DEFINE_BEGING(XBoxPlotLegendMarker)
XCLASS_DEFINE_EXTEND_END(XBoxPlotLegendMarker, XLegendMarker)

/** @brief QBoxPlotLegendMarker 对应实现。 */
typedef struct XBoxPlotLegendMarker
{
    XLegendMarker m_base;   /**< 基类成员；必须是第一个。 */

} XBoxPlotLegendMarker;

/** @brief 初始化类虚函数表。 @return 共享虚函数表指针。 */
XVtable* XBoxPlotLegendMarker_class_init(void);

/**
 * @brief 初始化嵌入式盒须序列图例标记。
 * @param self 目标标记指针，不能为空。
 * @param series 归属序列（借用，不转移所有权）；可为 NULL。
 * @return 无返回值。
 */
void XBoxPlotLegendMarker_init(XBoxPlotLegendMarker* self, XBoxPlotSeries* series);

/**
 * @brief 堆上创建盒须序列图例标记。
 * @param memory 内存类型。
 * @param series 归属序列（借用）；可为 NULL。
 * @return 标记指针；分配失败返回 NULL。
 */
XBoxPlotLegendMarker* XBoxPlotLegendMarker_create_ex(XMemoryType memory, XBoxPlotSeries* series);
#define XBoxPlotLegendMarker_create(series) \
    XBoxPlotLegendMarker_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, series)

/** @brief 查询标记类型（恒定 BoxPlot）。 @param self 目标标记指针。 @return XLegendMarker_LegendMarkerType_BoxPlot。 */
int XBoxPlotLegendMarker_type(const XBoxPlotLegendMarker* self);

#endif /* XCHARTS_ON */
#ifdef __cplusplus
}
#endif

#endif /* XGUI_ON */

#endif /* XBOXPLOTLEGENDMARKER_H */
