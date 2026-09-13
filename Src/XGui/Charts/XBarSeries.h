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
#include "XClass.h"

#if XCHARTS_ON

/** @brief 柱状序列（对标 QBarSeries + 单 QBarSet）。 */
typedef struct XBarSeries
{
    char m_name[64];          /**< 序列名（图例显示）。 */
    double* m_values;         /**< 柱值数组（堆）。 */
    char (*m_categories)[64]; /**< 类别标签数组（堆）。 */
    int m_count;              /**< 柱数。 */
    int m_capacity;           /**< 容量。 */
    double m_barWidth;        /**< 组宽比例 0-1（默认 0.8）。 */
    uint32_t m_color;         /**< 柱色（0=主题色）。 */
    bool m_visible;           /**< 可见（默认 true）。 */
} XBarSeries;

XBarSeries* XBarSeries_create_ex(XMemoryType memory);
#define XBarSeries_create() XBarSeries_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)
void XBarSeries_delete_base(XBarSeries* self);

/** @brief 追加柱（label + value）。 @param self 目标序列指针。 @param label 类别标签。 @param value 柱值。 @return 下标；失败 -1。 */
int XBarSeries_append(XBarSeries* self, const char* label, double value);
/** @brief 查询柱数。 @param self 目标序列指针。 @return 柱数。 */
int XBarSeries_count(const XBarSeries* self);
/** @brief 查询柱值。 @param self 目标序列指针。 @param index 下标。 @return 柱值；越界 0。 */
double XBarSeries_value(const XBarSeries* self, int index);
/** @brief 查询类别标签。 @param self 目标序列指针。 @param index 下标。 @return 标签；越界空串。 */
const char* XBarSeries_category(const XBarSeries* self, int index);
/** @brief 设置组宽比例。 @param self 目标序列指针。 @param width 0-1。 @return 无返回值。 */
void XBarSeries_setBarWidth(XBarSeries* self, double width);
/** @brief 设置柱色。 @param self 目标序列指针。 @param color ARGB。 @return 无返回值。 */
void XBarSeries_setColor(XBarSeries* self, uint32_t color);
/** @brief 设置序列名。 @param self 目标序列指针。 @param name UTF-8 名称。 @return 无返回值。 */
void XBarSeries_setName(XBarSeries* self, const char* name);
/** @brief 查询序列名。 @param self 目标序列指针。 @return 序列名。 */
const char* XBarSeries_name(const XBarSeries* self);
/** @brief 清空。 @param self 目标序列指针。 @return 无返回值。 */
void XBarSeries_clear(XBarSeries* self);

#endif /* XCHARTS_ON */
#ifdef __cplusplus
}
#endif
#endif /* XBARSERIES_H */