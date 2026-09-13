/**
 * @file       XLineSeries.h
 * @brief      XLineSeries 折线序列（对标 Qt Charts 6.8 QLineSeries）。
 * @details    (x,y) 点集 + 线色/线宽/序列名；渲染由 XChart 完成。
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
#include "XGeometry.h"
#include "XMemory.h"

#if XCHARTS_ON

/** @brief 数据点。 */
/* XPointF 复用框架定义（XData/XGeometry.h）。 */

/** @brief 折线序列（对标 QLineSeries）。 */
typedef struct XLineSeries
{
    char m_name[64];          /**< 序列名（图例显示）。 */
    uint32_t m_color;         /**< 线色（0=使用主题色）。 */
    double m_width;           /**< 线宽（像素，默认 2）。 */
    XPointF* m_points;        /**< 点数组（堆）。 */
    int m_count;              /**< 点数。 */
    int m_capacity;           /**< 点容量。 */
    bool m_visible;           /**< 可见（默认 true）。 */
} XLineSeries;

XLineSeries* XLineSeries_create_ex(XMemoryType memory);
#define XLineSeries_create() XLineSeries_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)
void XLineSeries_delete_base(XLineSeries* self);

/** @brief 设置序列名。 @param self 目标序列指针。 @param name UTF-8 名称。 @return 无返回值。 */
void XLineSeries_setName(XLineSeries* self, const char* name);
/** @brief 读取序列名。 @param self 目标序列指针。 @return 序列名。 */
const char* XLineSeries_name(const XLineSeries* self);
/** @brief 设置线色。 @param self 目标序列指针。 @param color ARGB 颜色。 @return 无返回值。 */
void XLineSeries_setColor(XLineSeries* self, uint32_t color);
/** @brief 查询线色。 @param self 目标序列指针。 @return ARGB 颜色（0=主题色）。 */
uint32_t XLineSeries_color(const XLineSeries* self);
/** @brief 设置线宽。 @param self 目标序列指针。 @param width 线宽像素。 @return 无返回值。 */
void XLineSeries_setWidth(XLineSeries* self, double width);
/** @brief 追加数据点（对标 append(x,y)）。 @param self 目标序列指针。 @param x X 坐标。 @param y Y 坐标。 @return 无返回值。 */
void XLineSeries_append(XLineSeries* self, double x, double y);
/** @brief 清空全部点。 @param self 目标序列指针。 @return 无返回值。 */
void XLineSeries_clear(XLineSeries* self);
/** @brief 查询点数。 @param self 目标序列指针。 @return 点数。 */
int XLineSeries_count(const XLineSeries* self);
/** @brief 按下标取点。 @param self 目标序列指针。 @param index 下标。 @return 点指针；越界返回 NULL。 */
const XPointF* XLineSeries_at(const XLineSeries* self, int index);
/** @brief 设置可见性。 @param self 目标序列指针。 @param visible true 显示。 @return 无返回值。 */
void XLineSeries_setVisible(XLineSeries* self, bool visible);

#endif /* XCHARTS_ON */
#ifdef __cplusplus
}
#endif
#endif /* XLINESERIES_H */