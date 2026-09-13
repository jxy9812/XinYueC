/**
 * @file       XPieSeries.h
 * @brief      XPieSeries 饼图序列（对标 Qt Charts 6.8 QPieSeries/QPieSlice）。
 * @details    切片集合：label/value/color；sum 为值总和（渲染用占比）。
 * @note       模块总开关 XCHARTS_ON。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XPIESERIES_H
#define XPIESERIES_H
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XMemory.h"

#if XCHARTS_ON

/** @brief 饼图切片（对标 QPieSlice）。 */
typedef struct XPieSlice
{
    char m_label[64];         /**< 切片标签。 */
    double m_value;           /**< 切片值。 */
    uint32_t m_color;         /**< 切片色（0=主题色）。 */
    bool m_exploded;          /**< 是否分离突出。 */
} XPieSlice;

/** @brief 饼图序列（对标 QPieSeries）。 */
typedef struct XPieSeries
{
    char m_name[64];          /**< 序列名。 */
    XPieSlice* m_slices;      /**< 切片数组（堆）。 */
    int m_count;              /**< 切片数。 */
    int m_capacity;           /**< 容量。 */
    double m_holeSize;        /**< 中心孔径比例 0-1（0=实心饼）。 */
    bool m_visible;           /**< 可见（默认 true）。 */
} XPieSeries;

XPieSeries* XPieSeries_create_ex(XMemoryType memory);
#define XPieSeries_create() XPieSeries_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)
void XPieSeries_delete_base(XPieSeries* self);

/** @brief 追加切片（对标 append(label,value)）。 @param self 目标序列指针。 @param label 切片标签。 @param value 切片值。 @return 切片下标；失败返回 -1。 */
int XPieSeries_append(XPieSeries* self, const char* label, double value);
/** @brief 查询切片数。 @param self 目标序列指针。 @return 切片数。 */
int XPieSeries_count(const XPieSeries* self);
/** @brief 按下标取切片。 @param self 目标序列指针。 @param index 下标。 @return 切片指针；越界返回 NULL。 */
XPieSlice* XPieSeries_slice(const XPieSeries* self, int index);
/** @brief 查询值总和（渲染占比用）。 @param self 目标序列指针。 @return 值总和。 */
double XPieSeries_sum(const XPieSeries* self);
/** @brief 设置中心孔径比例。 @param self 目标序列指针。 @param hole 0-1（0=实心）。 @return 无返回值。 */
void XPieSeries_setHoleSize(XPieSeries* self, double hole);
/** @brief 设置序列名。 @param self 目标序列指针。 @param name UTF-8 名称。 @return 无返回值。 */
void XPieSeries_setName(XPieSeries* self, const char* name);

#endif /* XCHARTS_ON */
#ifdef __cplusplus
}
#endif
#endif /* XPIESERIES_H */