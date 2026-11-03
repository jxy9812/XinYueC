/**
 * @file       XHorizontalBarSeries.h
 * @brief      XHorizontalBarSeries 水平柱状序列（对标 Qt Charts 6.8 QHorizontalBarSeries）。
 * @details    继承 XAbstractBarSeries（对应 QHorizontalBarSeries : QAbstractBarSeries）；
 *             柱体沿水平方向排布（类别在纵轴），是 QBarSeries 的转置。
 * @note       模块总开关 XCHARTS_ON；实现只依赖 XinYueC 抽象层，
 *             禁止调用 Win32、POSIX、Qt 或其他平台 API。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XHORIZONTALBARSERIES_H
#define XHORIZONTALBARSERIES_H

#if XGUI_ON
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XAbstractBarSeries.h"

#if XCHARTS_ON

XCLASS_DEFINE_BEGING(XHorizontalBarSeries)
XCLASS_DEFINE_EXTEND_END(XHorizontalBarSeries, XAbstractBarSeries)

/** @brief 水平柱状序列（对标 QHorizontalBarSeries）。 */
typedef struct XHorizontalBarSeries
{
    XAbstractBarSeries m_base;  /**< 基类成员；必须是第一个。 */
} XHorizontalBarSeries;

/** @brief 初始化类虚函数表。 @return 共享虚函数表指针。 */
XVtable* XHorizontalBarSeries_class_init(void);

/**
 * @brief 初始化嵌入式水平柱状序列。
 * @param self 目标序列指针，不能为空。
 * @return 无返回值。
 */
void XHorizontalBarSeries_init(XHorizontalBarSeries* self);

/**
 * @brief 堆上创建水平柱状序列。
 * @param memory 内存类型。
 * @return 序列指针；分配失败返回 NULL。
 */
XHorizontalBarSeries* XHorizontalBarSeries_create_ex(XMemoryType memory);
#define XHorizontalBarSeries_create() XHorizontalBarSeries_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

/** @brief 查询序列类型（对标 QHorizontalBarSeries::type）。 @param self 目标序列指针。 @return 固定返回 XChartSeriesType_HorizontalBar。 */
int XHorizontalBarSeries_type(const XHorizontalBarSeries* self);

#endif /* XCHARTS_ON */
#ifdef __cplusplus
}
#endif

#endif /* XGUI_ON */

#endif /* XHORIZONTALBARSERIES_H */
