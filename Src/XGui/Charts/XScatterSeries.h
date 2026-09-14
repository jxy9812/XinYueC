/**
 * @file       XScatterSeries.h
 * @brief      XScatterSeries 散点序列（对标 Qt Charts 6.8 QScatterSeries）。
 * @details    继承 XXYSeries（QXYSeries 对齐）；自有标记形状/边框色。
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
#include "XXYSeries.h"

#if XCHARTS_ON

/**
 * @brief      标记形状（对标 Qt Charts 6.8 QScatterSeries::MarkerShape）。
 */
typedef enum XScatterSeriesMarkerShape
{
    XScatterSeriesMarkerShape_Circle = 0,  /**< 圆形。 */
    XScatterSeriesMarkerShape_Rectangle = 1 /**< 矩形。 */
} XScatterSeriesMarkerShape;

XCLASS_DEFINE_BEGING(XScatterSeries)
XCLASS_DEFINE_EXTEND_END(XScatterSeries, XXYSeries)

/** @brief 散点序列（对标 QScatterSeries；数据/外观在 XXYSeries）。 */
typedef struct XScatterSeries
{
    XXYSeries m_base;       /**< 基类成员；必须是第一个。 */
    int m_markerShape;      /**< XScatterSeriesMarkerShape（默认 Circle）。 */
    uint32_t m_borderColor; /**< 标记边框色（0=默认）。 */
} XScatterSeries;

XVtable* XScatterSeries_class_init(void);

/**
 * @brief 初始化嵌入式散点序列。
 *
 * @param self 目标序列指针，不能为空。
 * @return 无返回值。
 */
void XScatterSeries_init(XScatterSeries* self);

XScatterSeries* XScatterSeries_create_ex(XMemoryType memory);
#define XScatterSeries_create() XScatterSeries_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

/** @brief 析构入口（查表分派父类析构）。 */
#define XScatterSeries_deinit_base(self) XClass_deinit_base((XClass*)(self))

/** @brief 删除堆上序列（查表分派析构并释放内存）。 */
#define XScatterSeries_delete_base(self) XClass_delete_base((XClass*)(self))

/** @brief 设置标记形状。 @param self 目标序列指针。 @param shape 形状枚举。 @return 无返回值。 */
void XScatterSeries_setMarkerShape(XScatterSeries* self, int shape);
/** @brief 查询标记形状。 @param self 目标序列指针。 @return 形状枚举。 */
int XScatterSeries_markerShape(const XScatterSeries* self);
/** @brief 设置标记边框色。 @param self 目标序列指针。 @param color ARGB。 @return 无返回值。 */
void XScatterSeries_setBorderColor(XScatterSeries* self, uint32_t color);
/** @brief 查询标记边框色。 @param self 目标序列指针。 @return ARGB。 */
uint32_t XScatterSeries_borderColor(const XScatterSeries* self);

#endif /* XCHARTS_ON */
#ifdef __cplusplus
}
#endif
#endif /* XSCATERSERIES_H */
