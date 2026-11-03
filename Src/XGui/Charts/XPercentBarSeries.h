/**
 * @file       XPercentBarSeries.h
 * @brief      XPercentBarSeries 百分比柱状序列（对标 Qt Charts 6.8 QPercentBarSeries）。
 * @details    继承 XAbstractBarSeries（对应 QPercentBarSeries : QAbstractBarSeries）；
 *             每个类别的各柱组按占比归一化到 100% 后堆叠，用于展示构成比例。
 * @note       模块总开关 XCHARTS_ON；实现只依赖 XinYueC 抽象层，
 *             禁止调用 Win32、POSIX、Qt 或其他平台 API。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XPERCENTBARSERIES_H
#define XPERCENTBARSERIES_H

#if XGUI_ON
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XAbstractBarSeries.h"

#if XCHARTS_ON

XCLASS_DEFINE_BEGING(XPercentBarSeries)
XCLASS_DEFINE_EXTEND_END(XPercentBarSeries, XAbstractBarSeries)

/** @brief 百分比柱状序列（对标 QPercentBarSeries）。 */
typedef struct XPercentBarSeries
{
    XAbstractBarSeries m_base;  /**< 基类成员；必须是第一个。 */
} XPercentBarSeries;

/** @brief 初始化类虚函数表。 @return 共享虚函数表指针。 */
XVtable* XPercentBarSeries_class_init(void);

/**
 * @brief 初始化嵌入式百分比柱状序列。
 * @param self 目标序列指针，不能为空。
 * @return 无返回值。
 */
void XPercentBarSeries_init(XPercentBarSeries* self);

/**
 * @brief 堆上创建百分比柱状序列。
 * @param memory 内存类型。
 * @return 序列指针；分配失败返回 NULL。
 */
XPercentBarSeries* XPercentBarSeries_create_ex(XMemoryType memory);
#define XPercentBarSeries_create() XPercentBarSeries_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

/** @brief 查询序列类型（对标 QPercentBarSeries::type）。 @param self 目标序列指针。 @return 固定返回 XChartSeriesType_PercentBar。 */
int XPercentBarSeries_type(const XPercentBarSeries* self);

#endif /* XCHARTS_ON */
#ifdef __cplusplus
}
#endif

#endif /* XGUI_ON */

#endif /* XPERCENTBARSERIES_H */
