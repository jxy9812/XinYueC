/**
 * @file       XSplineSeries.h
 * @brief      XSplineSeries 样条序列（对标 Qt Charts 6.8 QSplineSeries）。
 * @details    继承 XXYSeries（QXYSeries 对齐）；无自有 API，
 *             渲染时以 Catmull-Rom 插值平滑。
 * @note       模块总开关 XCHARTS_ON。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XSPLINESERIES_H
#define XSPLINESERIES_H
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XXYSeries.h"

#if XCHARTS_ON

XCLASS_DEFINE_BEGING(XSplineSeries)
XCLASS_DEFINE_EXTEND_END(XSplineSeries, XXYSeries)

/** @brief 样条序列（对标 QSplineSeries；数据/外观在 XXYSeries）。 */
typedef struct XSplineSeries
{
    XXYSeries m_base;  /**< 基类成员；必须是第一个。 */
} XSplineSeries;

XVtable* XSplineSeries_class_init(void);

/**
 * @brief 初始化嵌入式样条序列。
 *
 * @param self 目标序列指针，不能为空。
 * @return 无返回值。
 */
void XSplineSeries_init(XSplineSeries* self);

XSplineSeries* XSplineSeries_create_ex(XMemoryType memory);
#define XSplineSeries_create() XSplineSeries_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

/** @brief 析构入口（查表分派父类析构）。 */
#define XSplineSeries_deinit_base(self) XClass_deinit_base((XClass*)(self))

/** @brief 删除堆上序列（查表分派析构并释放内存）。 */
#define XSplineSeries_delete_base(self) XClass_delete_base((XClass*)(self))

#endif /* XCHARTS_ON */
#ifdef __cplusplus
}
#endif
#endif /* XSPLINESERIES_H */
