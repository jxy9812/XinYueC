/**
 * @file       XHorizontalStackedBarSeries.h
 * @brief      XHorizontalStackedBarSeries 水平堆叠柱状序列（对标 Qt Charts 6.8 QHorizontalStackedBarSeries）。
 * @details    继承 XAbstractBarSeries（对应 QHorizontalStackedBarSeries : QAbstractBarSeries）；
 *             水平方向的堆叠柱状序列。
 * @note       模块总开关 XCHARTS_ON；实现只依赖 XinYueC 抽象层，
 *             禁止调用 Win32、POSIX、Qt 或其他平台 API。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XHORIZONTALSTACKEDBARSERIES_H
#define XHORIZONTALSTACKEDBARSERIES_H

#if XGUI_ON
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XAbstractBarSeries.h"

#if XCHARTS_ON

XCLASS_DEFINE_BEGING(XHorizontalStackedBarSeries)
XCLASS_DEFINE_EXTEND_END(XHorizontalStackedBarSeries, XAbstractBarSeries)

/** @brief 水平堆叠柱状序列（对标 QHorizontalStackedBarSeries）。 */
typedef struct XHorizontalStackedBarSeries
{
    XAbstractBarSeries m_base;  /**< 基类成员；必须是第一个。 */
} XHorizontalStackedBarSeries;

/** @brief 初始化类虚函数表。 @return 共享虚函数表指针。 */
XVtable* XHorizontalStackedBarSeries_class_init(void);

/**
 * @brief 初始化嵌入式水平堆叠柱状序列。
 * @param self 目标序列指针，不能为空。
 * @return 无返回值。
 */
void XHorizontalStackedBarSeries_init(XHorizontalStackedBarSeries* self);

/**
 * @brief 堆上创建水平堆叠柱状序列。
 * @param memory 内存类型。
 * @return 序列指针；分配失败返回 NULL。
 */
XHorizontalStackedBarSeries* XHorizontalStackedBarSeries_create_ex(XMemoryType memory);
#define XHorizontalStackedBarSeries_create() XHorizontalStackedBarSeries_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

/** @brief 查询序列类型（对标 QHorizontalStackedBarSeries::type）。 @param self 目标序列指针。 @return 固定返回 XChartSeriesType_HorizontalStackedBar。 */
int XHorizontalStackedBarSeries_type(const XHorizontalStackedBarSeries* self);

#endif /* XCHARTS_ON */
#ifdef __cplusplus
}
#endif

#endif /* XGUI_ON */

#endif /* XHORIZONTALSTACKEDBARSERIES_H */
