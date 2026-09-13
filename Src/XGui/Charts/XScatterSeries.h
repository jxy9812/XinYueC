/**
 * @file       XScatterSeries.h
 * @brief      XScatterSeries 散点序列（对标 Qt Charts 6.8 QScatterSeries）。
 * @details    点集 + 标记大小/颜色；渲染由 XChart 完成。
 * @note       模块总开关 XCHARTS_ON。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XSCATERSERIES_H
#define XSCATERSERIES_H
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XLineSeries.h"

#if XCHARTS_ON

/** @brief 散点序列（点存储复用 XPointF；对标 QScatterSeries）。 */
typedef struct XScatterSeries
{
    char m_name[64];          /**< 序列名。 */
    uint32_t m_color;         /**< 点色（0=主题色）。 */
    int m_markerSize;         /**< 标记直径（像素，默认 8）。 */
    XPointF* m_points;        /**< 点数组（堆）。 */
    int m_count;              /**< 点数。 */
    int m_capacity;           /**< 容量。 */
    bool m_visible;           /**< 可见（默认 true）。 */
} XScatterSeries;

XScatterSeries* XScatterSeries_create_ex(XMemoryType memory);
#define XScatterSeries_create() XScatterSeries_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)
void XScatterSeries_delete_base(XScatterSeries* self);

/** @brief 设置序列名。 @param self 目标序列指针。 @param name UTF-8 名称。 @return 无返回值。 */
void XScatterSeries_setName(XScatterSeries* self, const char* name);
/** @brief 设置点色。 @param self 目标序列指针。 @param color ARGB。 @return 无返回值。 */
void XScatterSeries_setColor(XScatterSeries* self, uint32_t color);
/** @brief 设置标记直径。 @param self 目标序列指针。 @param size 直径像素。 @return 无返回值。 */
void XScatterSeries_setMarkerSize(XScatterSeries* self, int size);
/** @brief 追加点。 @param self 目标序列指针。 @param x X 坐标。 @param y Y 坐标。 @return 无返回值。 */
void XScatterSeries_append(XScatterSeries* self, double x, double y);
/** @brief 查询点数。 @param self 目标序列指针。 @return 点数。 */
int XScatterSeries_count(const XScatterSeries* self);
/** @brief 清空。 @param self 目标序列指针。 @return 无返回值。 */
void XScatterSeries_clear(XScatterSeries* self);

#endif /* XCHARTS_ON */
#ifdef __cplusplus
}
#endif
#endif /* XSCATERSERIES_H */