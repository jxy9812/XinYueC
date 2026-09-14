/**
 * @file       XBarSeries.h
 * @brief      XBarSeries 柱状序列（对标 Qt Charts 6.8 QBarSeries + QBarSet）。
 * @details    单组柱：values 值数组 + categories 类别标签 + 条宽；
 *             渲染由 XChart 完成（对标 QBarSet 数据语义）。
 * @note       模块总开关 XCHARTS_ON。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XBARSERIES_H
#define XBARSERIES_H
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XString.h"
#include "XClass.h"
#include "XObject.h"
#include "XAbstractBarSeries.h"

#if XCHARTS_ON

XCLASS_DEFINE_BEGING(XBarSeries)
XCLASS_DEFINE_EXTEND_END(XBarSeries, XAbstractBarSeries)

/** @brief 柱状序列（对标 QBarSeries；单组柱，数据在 XAbstractBarSeries）。 */
typedef struct XBarSeries
{
    XAbstractBarSeries m_base; /**< 基类成员；必须是第一个。 */
    uint32_t m_color;          /**< 柱色（0=主题色）。 */
} XBarSeries;

XVtable* XBarSeries_class_init(void);

/**
 * @brief 初始化嵌入式柱状序列。
 *
 * @param self 目标序列指针，不能为空。
 * @return 无返回值。
 */
void XBarSeries_init(XBarSeries* self);

XBarSeries* XBarSeries_create_ex(XMemoryType memory);
#define XBarSeries_create() XBarSeries_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

/** @brief 析构入口（查表分派父类析构）。 */
#define XBarSeries_deinit_base(self) XClass_deinit_base((XClass*)(self))

/** @brief 删除堆上序列（查表分派析构并释放内存）。 */
#define XBarSeries_delete_base(self) XClass_delete_base((XClass*)(self))

/** @brief 设置柱色。 @param self 目标序列指针。 @param color ARGB。 @return 无返回值。 */
void XBarSeries_setColor(XBarSeries* self, uint32_t color);
/** @brief 查询柱色。 @param self 目标序列指针。 @return ARGB。 */
uint32_t XBarSeries_color(const XBarSeries* self);

#endif /* XCHARTS_ON */
#ifdef __cplusplus
}
#endif
#endif /* XBARSERIES_H */