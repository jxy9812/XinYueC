/**
 * @file       XLineSeries.h
 * @brief      XLineSeries 折线序列（对标 Qt Charts 6.8 QLineSeries）。
 * @details    继承 XXYSeries（QXYSeries 对齐）；无自有 API，
 *             数据/外观/信号全部由 XXYSeries 提供。
 * @note       模块总开关 XCHARTS_ON。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XLINESERIES_H
#define XLINESERIES_H
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XXYSeries.h"

#if XCHARTS_ON

XCLASS_DEFINE_BEGING(XLineSeries)
XCLASS_DEFINE_EXTEND_END(XLineSeries, XXYSeries)

/** @brief 折线序列（对标 QLineSeries；数据/外观在 XXYSeries）。 */
typedef struct XLineSeries
{
    XXYSeries m_base;  /**< 基类成员；必须是第一个。 */
} XLineSeries;

XVtable* XLineSeries_class_init(void);

/**
 * @brief 初始化嵌入式折线序列。
 *
 * @param self 目标序列指针，不能为空。
 * @return 无返回值。
 */
void XLineSeries_init(XLineSeries* self);

XLineSeries* XLineSeries_create_ex(XMemoryType memory);
#define XLineSeries_create() XLineSeries_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

/** @brief 析构入口（查表分派父类析构）。 */
#define XLineSeries_deinit_base(self) XClass_deinit_base((XClass*)(self))

/** @brief 删除堆上序列（查表分派析构并释放内存）。 */
#define XLineSeries_delete_base(self) XClass_delete_base((XClass*)(self))

#endif /* XCHARTS_ON */
#ifdef __cplusplus
}
#endif
#endif /* XLINESERIES_H */
