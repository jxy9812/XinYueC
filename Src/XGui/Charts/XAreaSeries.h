/**
 * @file       XAreaSeries.h
 * @brief      XAreaSeries 面积序列（对标 Qt Charts 6.8 QAreaSeries）。
 * @details    以一条 XLineSeries 为上边界，下边界为基线（y=base）；
 *             渲染填充上下界之间区域并描上边界线。
 * @note       模块总开关 XCHARTS_ON。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XAREASERIES_H
#define XAREASERIES_H
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XLineSeries.h"

#if XCHARTS_ON

/** @brief 面积序列（对标 QAreaSeries）。 */
typedef struct XAreaSeries
{
    char m_name[64];          /**< 序列名。 */
    XLineSeries* m_upper;     /**< 上边界折线（内部拥有）。 */
    double m_baseValue;       /**< 下边界基线值（默认 0）。 */
    uint32_t m_color;         /**< 填充色（0=主题色）。 */
    bool m_visible;           /**< 可见（默认 true）。 */
} XAreaSeries;

XAreaSeries* XAreaSeries_create_ex(XMemoryType memory);
#define XAreaSeries_create() XAreaSeries_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)
void XAreaSeries_delete_base(XAreaSeries* self);

/** @brief 取上边界折线（追加数据点用）。 @param self 目标序列指针。 @return 折线指针（内部拥有）。 */
XLineSeries* XAreaSeries_upperSeries(const XAreaSeries* self);
/** @brief 设置下边界基线值。 @param self 目标序列指针。 @param base 基线值。 @return 无返回值。 */
void XAreaSeries_setBaseValue(XAreaSeries* self, double base);
/** @brief 设置填充色。 @param self 目标序列指针。 @param color ARGB。 @return 无返回值。 */
void XAreaSeries_setColor(XAreaSeries* self, uint32_t color);
/** @brief 设置序列名。 @param self 目标序列指针。 @param name UTF-8 名称。 @return 无返回值。 */
void XAreaSeries_setName(XAreaSeries* self, const char* name);

#endif /* XCHARTS_ON */
#ifdef __cplusplus
}
#endif
#endif /* XAREASERIES_H */