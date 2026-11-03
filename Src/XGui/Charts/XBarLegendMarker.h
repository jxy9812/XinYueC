/**
 * @file       XBarLegendMarker.h
 * @brief      XBarLegendMarker 柱状序列图例标记（对标 Qt Charts 6.8 QBarLegendMarker）。
 * @details    继承 XLegendMarker（对应 QBarLegendMarker : QLegendMarker）；固定标记
 *             类型为 Bar，并保留 Qt 中该类持有的归属对象。
 * @note       模块总开关 XCHARTS_ON。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XBARLEGENDMARKER_H
#define XBARLEGENDMARKER_H

#if XGUI_ON
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XLegendMarker.h"
#include "XAbstractBarSeries.h"

#if XCHARTS_ON

XCLASS_DEFINE_BEGING(XBarLegendMarker)
XCLASS_DEFINE_EXTEND_END(XBarLegendMarker, XLegendMarker)

/** @brief QBarLegendMarker 对应实现。 */
typedef struct XBarLegendMarker
{
    XLegendMarker m_base;   /**< 基类成员；必须是第一个。 */

    XBarSet* m_barSet;   /**< 柱组指针（借用；Qt 中一个 QBarSet 对应一个标记）。（借用；不拥有）。 */

} XBarLegendMarker;

/** @brief 初始化类虚函数表。 @return 共享虚函数表指针。 */
XVtable* XBarLegendMarker_class_init(void);

/**
 * @brief 初始化嵌入式柱状序列图例标记。
 * @param self 目标标记指针，不能为空。
 * @param series 归属序列（借用，不转移所有权）；可为 NULL。
 * @return 无返回值。
 */
void XBarLegendMarker_init(XBarLegendMarker* self, XAbstractBarSeries* series);

/**
 * @brief 堆上创建柱状序列图例标记。
 * @param memory 内存类型。
 * @param series 归属序列（借用）；可为 NULL。
 * @return 标记指针；分配失败返回 NULL。
 */
XBarLegendMarker* XBarLegendMarker_create_ex(XMemoryType memory, XAbstractBarSeries* series);
#define XBarLegendMarker_create(series) \
    XBarLegendMarker_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, series)

/** @brief 查询标记类型（恒定 Bar）。 @param self 目标标记指针。 @return XLegendMarker_LegendMarkerType_Bar。 */
int XBarLegendMarker_type(const XBarLegendMarker* self);

/** @brief 查询归属柱组（借用；对标 QBarLegendMarker::barset）。
 * @param self 目标标记指针。
 * @return 借用 XBarSet*；未设置返回 NULL。调用者不得释放。
 */
XBarSet* XBarLegendMarker_barset(const XBarLegendMarker* self);
/** @brief 查询归属柱组（兼容别名，转发 barset）。 @param self 目标标记指针。 @return 借用 XBarSet*。 */
XBarSet* XBarLegendMarker_barSet(const XBarLegendMarker* self);
/** @brief 绑定归属柱组（Qt 经构造参数传入；C 侧补独立入口）。
 * @param self 目标标记指针。
 * @param set 柱组指针（借用，不转移所有权）；NULL 解除绑定。
 * @return 无返回值。
 */
void XBarLegendMarker_setBarSet(XBarLegendMarker* self, XBarSet* set);

#endif /* XCHARTS_ON */
#ifdef __cplusplus
}
#endif

#endif /* XGUI_ON */

#endif /* XBARLEGENDMARKER_H */
