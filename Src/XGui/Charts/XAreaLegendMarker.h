/**
 * @file       XAreaLegendMarker.h
 * @brief      XAreaLegendMarker 面积序列图例标记（对标 Qt Charts 6.8 QAreaLegendMarker）。
 * @details    继承 XLegendMarker（对应 QAreaLegendMarker : QLegendMarker）；固定标记
 *             类型为 Area，并保留 Qt 中该类持有的归属对象。
 * @note       模块总开关 XCHARTS_ON。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XAREALEGENDMARKER_H
#define XAREALEGENDMARKER_H

#if XGUI_ON
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XLegendMarker.h"
#include "XAreaSeries.h"

#if XCHARTS_ON

XCLASS_DEFINE_BEGING(XAreaLegendMarker)
XCLASS_DEFINE_EXTEND_END(XAreaLegendMarker, XLegendMarker)

/** @brief QAreaLegendMarker 对应实现。 */
typedef struct XAreaLegendMarker
{
    XLegendMarker m_base;   /**< 基类成员；必须是第一个。 */

} XAreaLegendMarker;

/** @brief 初始化类虚函数表。 @return 共享虚函数表指针。 */
XVtable* XAreaLegendMarker_class_init(void);

/**
 * @brief 初始化嵌入式面积序列图例标记。
 * @param self 目标标记指针，不能为空。
 * @param series 归属序列（借用，不转移所有权）；可为 NULL。
 * @return 无返回值。
 */
void XAreaLegendMarker_init(XAreaLegendMarker* self, XAreaSeries* series);

/**
 * @brief 堆上创建面积序列图例标记。
 * @param memory 内存类型。
 * @param series 归属序列（借用）；可为 NULL。
 * @return 标记指针；分配失败返回 NULL。
 */
XAreaLegendMarker* XAreaLegendMarker_create_ex(XMemoryType memory, XAreaSeries* series);
#define XAreaLegendMarker_create(series) \
    XAreaLegendMarker_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, series)

/** @brief 查询标记类型（恒定 Area）。 @param self 目标标记指针。 @return XLegendMarker_LegendMarkerType_Area。 */
int XAreaLegendMarker_type(const XAreaLegendMarker* self);

#endif /* XCHARTS_ON */
#ifdef __cplusplus
}
#endif

#endif /* XGUI_ON */

#endif /* XAREALEGENDMARKER_H */
