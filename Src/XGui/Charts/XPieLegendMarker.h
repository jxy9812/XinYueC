/**
 * @file       XPieLegendMarker.h
 * @brief      XPieLegendMarker 饼图序列图例标记（对标 Qt Charts 6.8 QPieLegendMarker）。
 * @details    继承 XLegendMarker（对应 QPieLegendMarker : QLegendMarker）；固定标记
 *             类型为 Pie，并保留 Qt 中该类持有的归属对象。
 * @note       模块总开关 XCHARTS_ON。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XPIELEGENDMARKER_H
#define XPIELEGENDMARKER_H

#if XGUI_ON
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XLegendMarker.h"
#include "XPieSeries.h"

#if XCHARTS_ON

XCLASS_DEFINE_BEGING(XPieLegendMarker)
XCLASS_DEFINE_EXTEND_END(XPieLegendMarker, XLegendMarker)

/** @brief QPieLegendMarker 对应实现。 */
typedef struct XPieLegendMarker
{
    XLegendMarker m_base;   /**< 基类成员；必须是第一个。 */

    XPieSlice* m_slice;   /**< 切片指针（借用；Qt 中一个 QPieSlice 对应一个标记）。（借用；不拥有）。 */

} XPieLegendMarker;

/** @brief 初始化类虚函数表。 @return 共享虚函数表指针。 */
XVtable* XPieLegendMarker_class_init(void);

/**
 * @brief 初始化嵌入式饼图序列图例标记。
 * @param self 目标标记指针，不能为空。
 * @param series 归属序列（借用，不转移所有权）；可为 NULL。
 * @return 无返回值。
 */
void XPieLegendMarker_init(XPieLegendMarker* self, XPieSeries* series);

/**
 * @brief 堆上创建饼图序列图例标记。
 * @param memory 内存类型。
 * @param series 归属序列（借用）；可为 NULL。
 * @return 标记指针；分配失败返回 NULL。
 */
XPieLegendMarker* XPieLegendMarker_create_ex(XMemoryType memory, XPieSeries* series);
#define XPieLegendMarker_create(series) \
    XPieLegendMarker_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, series)

/** @brief 查询标记类型（恒定 Pie）。 @param self 目标标记指针。 @return XLegendMarker_LegendMarkerType_Pie。 */
int XPieLegendMarker_type(const XPieLegendMarker* self);

/** @brief 查询归属切片（借用；对标 QPieLegendMarker::slice）。
 * @param self 目标标记指针。
 * @return 借用 XPieSlice*；未设置返回 NULL。调用者不得释放。
 */
XPieSlice* XPieLegendMarker_slice(const XPieLegendMarker* self);
/** @brief 绑定归属切片（Qt 经构造参数传入；C 侧补独立入口）。
 * @param self 目标标记指针。
 * @param slice 切片指针（借用，不转移所有权）；NULL 解除绑定。
 * @return 无返回值。
 */
void XPieLegendMarker_setSlice(XPieLegendMarker* self, XPieSlice* slice);

#endif /* XCHARTS_ON */
#ifdef __cplusplus
}
#endif

#endif /* XGUI_ON */

#endif /* XPIELEGENDMARKER_H */
