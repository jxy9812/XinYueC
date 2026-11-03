/**
 * @file       XScatterSeries.h
 * @brief      XScatterSeries 散点序列（对标 Qt Charts 6.8 QScatterSeries）。
 * @details    继承 XXYSeries（QXYSeries 对齐）；自有标记形状/边框色/
 *             标记尺寸/画刷。Qt 中 QScatterSeries 覆写 setPen/setBrush/
 *             setColor 并另发 colorChanged/borderColorChanged/
 *             markerShapeChanged/markerSizeChanged 信号。
 * @note       模块总开关 XCHARTS_ON；实现只依赖 XinYueC 抽象层，
 *             禁止调用 Win32、POSIX、Qt 或其他平台 API。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XSCATERSERIES_H
#define XSCATERSERIES_H

#if XGUI_ON
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XXYSeries.h"

#if XCHARTS_ON

/**
 * @brief      标记形状（对标 Qt Charts 6.8 QScatterSeries::MarkerShape，
 *             数值一致）。
 */
typedef enum XScatterSeriesMarkerShape
{
    XScatterSeriesMarkerShape_Circle = 0,           /**< 圆形（对标 MarkerShapeCircle）。 */
    XScatterSeriesMarkerShape_Rectangle = 1,        /**< 矩形（对标 MarkerShapeRectangle）。 */
    XScatterSeriesMarkerShape_RotatedRectangle = 2, /**< 旋转矩形（对标 MarkerShapeRotatedRectangle）。 */
    XScatterSeriesMarkerShape_Triangle = 3,         /**< 三角形（对标 MarkerShapeTriangle）。 */
    XScatterSeriesMarkerShape_Star = 4,             /**< 星形（对标 MarkerShapeStar）。 */
    XScatterSeriesMarkerShape_Pentagon = 5          /**< 五边形（对标 MarkerShapePentagon）。 */
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

/**
 * @brief 在堆上创建散点序列（对标 QScatterSeries 构造）。
 *
 * @param memory 内存类型。
 * @return 新序列指针；分配失败返回 NULL，调用方用 XClassDelete 释放。
 */
XScatterSeries* XScatterSeries_create_ex(XMemoryType memory);
#define XScatterSeries_create() XScatterSeries_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

/* ==================== 类型查询（对标 QScatterSeries） ==================== */

/** @brief 查询序列类型（对标 QScatterSeries::type）。 @param self 目标序列指针。 @return 固定返回 Scatter。 */
int XScatterSeries_type(const XScatterSeries* self);

/* ==================== 标记形状 / 尺寸 / 边框（对标 QScatterSeries） ==================== */

/** @brief 设置标记形状（对标 setMarkerShape）。 @param self 目标序列指针。 @param shape 形状枚举。 @return 无返回值。 */
void XScatterSeries_setMarkerShape(XScatterSeries* self, int shape);
/** @brief 查询标记形状（对标 markerShape）。 @param self 目标序列指针。 @return 形状枚举。 */
int XScatterSeries_markerShape(const XScatterSeries* self);
/** @brief 设置标记边框色（对标 setBorderColor）。 @param self 目标序列指针。 @param color ARGB。 @return 无返回值。 */
void XScatterSeries_setBorderColor(XScatterSeries* self, uint32_t color);
/** @brief 查询标记边框色（对标 borderColor）。 @param self 目标序列指针。 @return ARGB。 */
uint32_t XScatterSeries_borderColor(const XScatterSeries* self);

/** @brief 设置标记尺寸（对标 QScatterSeries::setMarkerSize）。
 * @param self 目标序列指针。 @param size 尺寸（像素；<0 钳位 0）。 @return 无返回值。 */
void XScatterSeries_setMarkerSize(XScatterSeries* self, double size);
/** @brief 查询标记尺寸（对标 QScatterSeries::markerSize）。 @param self 目标序列指针。 @return 尺寸。 */
double XScatterSeries_markerSize(const XScatterSeries* self);

/* ==================== 画笔 / 画刷 / 颜色（对标 QScatterSeries 覆写） ==================== */

/** @brief 设置画笔（颜色 + 线宽；对标 QScatterSeries::setPen 覆写）。
 * @param self 目标序列指针。 @param color ARGB。 @param width 线宽。 @return 无返回值。 */
void XScatterSeries_setPen(XScatterSeries* self, uint32_t color, double width);
/** @brief 设置画刷颜色（对标 QScatterSeries::setBrush 覆写）。
 * @param self 目标序列指针。 @param color ARGB。 @return 无返回值。 */
void XScatterSeries_setBrush(XScatterSeries* self, uint32_t color);
/** @brief 查询画刷颜色（对标 QScatterSeries::brush）。 @param self 目标序列指针。 @return ARGB。 */
uint32_t XScatterSeries_brush(const XScatterSeries* self);
/** @brief 设置填充色（对标 QScatterSeries::setColor 覆写）。
 * @param self 目标序列指针。 @param color ARGB。 @return 无返回值。 */
void XScatterSeries_setColor(XScatterSeries* self, uint32_t color);
/** @brief 查询填充色（对标 QScatterSeries::color）。 @param self 目标序列指针。 @return ARGB。 */
uint32_t XScatterSeries_color(const XScatterSeries* self);

/* ==================== 信号（对标 QScatterSeries Q_SIGNALS） ==================== */

/** @brief colorChanged 信号（载荷：颜色）。 */
void* XScatterSeries_colorChanged_signal(XScatterSeries* self, uint32_t color);
/** @brief borderColorChanged 信号（载荷：颜色）。 */
void* XScatterSeries_borderColorChanged_signal(XScatterSeries* self, uint32_t color);
/** @brief markerShapeChanged 信号（载荷：形状枚举）。 */
void* XScatterSeries_markerShapeChanged_signal(XScatterSeries* self, int shape);
/** @brief markerSizeChanged 信号（载荷：尺寸）。 */
void* XScatterSeries_markerSizeChanged_signal(XScatterSeries* self, double size);

#endif /* XCHARTS_ON */
#ifdef __cplusplus
}
#endif

#endif /* XGUI_ON */

#endif /* XSCATERSERIES_H */
