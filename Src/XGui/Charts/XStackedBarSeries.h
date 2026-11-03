/**
 * @file       XStackedBarSeries.h
 * @brief      XStackedBarSeries 堆叠柱状序列（对标 Qt Charts 6.8 QStackedBarSeries）。
 * @details    继承 XAbstractBarSeries（对应 QStackedBarSeries : QAbstractBarSeries）；
 *             所有柱组在同一位置堆叠（后一组叠在前一组之上），用于展示总量与构成。
 * @note       模块总开关 XCHARTS_ON；实现只依赖 XinYueC 抽象层，
 *             禁止调用 Win32、POSIX、Qt 或其他平台 API。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XSTACKEDBARSERIES_H
#define XSTACKEDBARSERIES_H

#if XGUI_ON
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XAbstractBarSeries.h"

#if XCHARTS_ON

XCLASS_DEFINE_BEGING(XStackedBarSeries)
XCLASS_DEFINE_EXTEND_END(XStackedBarSeries, XAbstractBarSeries)

/** @brief 堆叠柱状序列（对标 QStackedBarSeries）。 */
typedef struct XStackedBarSeries
{
    XAbstractBarSeries m_base;  /**< 基类成员；必须是第一个。 */
} XStackedBarSeries;

/** @brief 初始化类虚函数表。 @return 共享虚函数表指针。 */
XVtable* XStackedBarSeries_class_init(void);

/**
 * @brief 初始化嵌入式堆叠柱状序列。
 * @param self 目标序列指针，不能为空。
 * @return 无返回值。
 */
void XStackedBarSeries_init(XStackedBarSeries* self);

/**
 * @brief 堆上创建堆叠柱状序列。
 * @param memory 内存类型。
 * @return 序列指针；分配失败返回 NULL。
 */
XStackedBarSeries* XStackedBarSeries_create_ex(XMemoryType memory);
#define XStackedBarSeries_create() XStackedBarSeries_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

/** @brief 查询序列类型（对标 QStackedBarSeries::type）。 @param self 目标序列指针。 @return 固定返回 XChartSeriesType_StackedBar。 */
int XStackedBarSeries_type(const XStackedBarSeries* self);

#endif /* XCHARTS_ON */
#ifdef __cplusplus
}
#endif

#endif /* XGUI_ON */

#endif /* XSTACKEDBARSERIES_H */
