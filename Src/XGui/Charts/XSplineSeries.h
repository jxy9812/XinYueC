/**
 * @file       XSplineSeries.h
 * @brief      XSplineSeries 样条序列（对标 Qt Charts 6.8 QSplineSeries）。
 * @details    继承折线点集；渲染时以 Catmull-Rom 插值平滑。
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
#include "XLineSeries.h"

#if XCHARTS_ON

/** @brief 样条序列（点集复用折线；渲染平滑插值）。 */
typedef struct XSplineSeries
{
    char m_name[64];          /**< 序列名。 */
    uint32_t m_color;         /**< 线色（0=主题色）。 */
    double m_width;           /**< 线宽（像素，默认 2）。 */
    XPointF* m_points;        /**< 控制点数组（堆）。 */
    int m_count;              /**< 控制点数。 */
    int m_capacity;           /**< 容量。 */
    bool m_visible;           /**< 可见（默认 true）。 */
} XSplineSeries;

XSplineSeries* XSplineSeries_create_ex(XMemoryType memory);
#define XSplineSeries_create() XSplineSeries_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)
void XSplineSeries_delete_base(XSplineSeries* self);

/** @brief 追加控制点。 @param self 目标序列指针。 @param x X 坐标。 @param y Y 坐标。 @return 无返回值。 */
void XSplineSeries_append(XSplineSeries* self, double x, double y);
/** @brief 查询控制点数。 @param self 目标序列指针。 @return 控制点数。 */
int XSplineSeries_count(const XSplineSeries* self);
/** @brief 设置线色。 @param self 目标序列指针。 @param color ARGB。 @return 无返回值。 */
void XSplineSeries_setColor(XSplineSeries* self, uint32_t color);
/** @brief 设置序列名。 @param self 目标序列指针。 @param name UTF-8 名称。 @return 无返回值。 */
void XSplineSeries_setName(XSplineSeries* self, const char* name);
/** @brief 清空。 @param self 目标序列指针。 @return 无返回值。 */
void XSplineSeries_clear(XSplineSeries* self);

#endif /* XCHARTS_ON */
#ifdef __cplusplus
}
#endif
#endif /* XSPLINESERIES_H */