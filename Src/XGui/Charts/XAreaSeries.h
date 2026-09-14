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
#include "XAbstractSeries.h"
#include "XString.h"
#include "XLineSeries.h"

#if XCHARTS_ON

XCLASS_DEFINE_BEGING(XAreaSeries)
XCLASS_DEFINE_EXTEND_END(XAreaSeries, XAbstractSeries)

/** @brief 面积序列（对标 QAreaSeries）。 */
typedef struct XAreaSeries
{
    XAbstractSeries m_base;  /**< 基类成员；必须是第一个。 */
    XLineSeries* m_upper;     /**< 上边界折线（内部拥有）。 */
    XLineSeries* m_lower;     /**< 下边界折线（借用；NULL=基线）。 */
    double m_baseValue;       /**< 下边界基线值（默认 0）。 */
    uint32_t m_color;         /**< 填充色（0=主题色）。 */
    uint32_t m_borderColor;   /**< 边框色（0=填充色）。 */
    double m_borderWidth;     /**< 边框线宽（默认 1）。 */
    uint32_t m_brushColor;    /**< 画刷颜色（0=透明）。 */
    bool m_pointsVisible;     /**< 上边界点标记可见（默认 false）。 */
    bool m_pointLabelsVisible; /**< 点标签可见（默认 false）。 */
    XString* m_pointLabelsFormat; /**< 点标签格式（对象拥有；默认 "@xPoint, @yPoint"）。 */
    uint32_t m_pointLabelsColor; /**< 点标签颜色（0=默认）。 */
    XString* m_pointLabelsFontFamily; /**< 点标签字体族（对象拥有）。 */
    int m_pointLabelsFontSize; /**< 点标签字号（磅；0=默认）。 */
    bool m_pointLabelsClipping; /**< 点标签裁剪（默认 true）。 */
} XAreaSeries;

XVtable* XAreaSeries_class_init(void);

/**
 * @brief 初始化嵌入式序列。
 *
 * @param self 目标序列指针，不能为空。
 * @return 无返回值。
 */
void XAreaSeries_init(XAreaSeries* self);

XAreaSeries* XAreaSeries_create_ex(XMemoryType memory);
#define XAreaSeries_create() XAreaSeries_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

/** @brief 析构入口（查表分派父类析构）。 */
#define XAreaSeries_deinit_base(self) XClass_deinit_base((XClass*)(self))

/** @brief 删除堆上序列（查表分派析构并释放内存）。 */
#define XAreaSeries_delete_base(self) XClass_delete_base((XClass*)(self))

/** @brief 取上边界折线（追加数据点用）。 @param self 目标序列指针。 @return 折线指针（内部拥有）。 */
XLineSeries* XAreaSeries_upperSeries(const XAreaSeries* self);
/** @brief 设置上边界折线（对标 setUpperSeries；内部拥有）。 @param self 目标序列指针。 @param series 折线序列。 @return 无返回值。 */
void XAreaSeries_setUpperSeries(XAreaSeries* self, XLineSeries* series);
/** @brief 设置下边界折线（对标 setLowerSeries；借用）。 @param self 目标序列指针。 @param series 折线序列（NULL=基线）。 @return 无返回值。 */
void XAreaSeries_setLowerSeries(XAreaSeries* self, XLineSeries* series);
/** @brief 读取下边界折线。 @param self 目标序列指针。 @return 折线指针；NULL=基线。 */
XLineSeries* XAreaSeries_lowerSeries(const XAreaSeries* self);
/** @brief 设置边框色。 @param self 目标序列指针。 @param color ARGB。 @return 无返回值。 */
void XAreaSeries_setBorderColor(XAreaSeries* self, uint32_t color);
/** @brief 查询边框色。 @param self 目标序列指针。 @return ARGB（0=用填充色）。 */
uint32_t XAreaSeries_borderColor(const XAreaSeries* self);
/** @brief 设置上边界点标记可见。 @param self 目标序列指针。 @param visible true 显示。 @return 无返回值。 */
void XAreaSeries_setPointsVisible(XAreaSeries* self, bool visible);
/** @brief 查询上边界点标记可见。 @param self 目标序列指针。 @return 可见返回 true。 */
bool XAreaSeries_pointsVisible(const XAreaSeries* self);
/** @brief 设置点标签可见。 @param self 目标序列指针。 @param visible true 显示。 @return 无返回值。 */
void XAreaSeries_setPointLabelsVisible(XAreaSeries* self, bool visible);
/** @brief 查询点标签可见。 @param self 目标序列指针。 @return 可见返回 true。 */
bool XAreaSeries_pointLabelsVisible(const XAreaSeries* self);
/** @brief 设置点标签格式。 @param self 目标序列指针。 @param format 格式串（UTF-8）。 @return 无返回值。 */
void XAreaSeries_setPointLabelsFormat(XAreaSeries* self, const char* format);
/** @brief 查询点标签格式。 @param self 目标序列指针。 @return 格式串。 */
const char* XAreaSeries_pointLabelsFormat(const XAreaSeries* self);
/** @brief 设置点标签颜色。 @param self 目标序列指针。 @param color ARGB。 @return 无返回值。 */
void XAreaSeries_setPointLabelsColor(XAreaSeries* self, uint32_t color);
/** @brief 查询点标签颜色。 @param self 目标序列指针。 @return ARGB。 */
uint32_t XAreaSeries_pointLabelsColor(const XAreaSeries* self);
/** @brief 设置点标签字体。 @param self 目标序列指针。 @param family 字体族（NULL 保持）。 @param pointSize 字号（0 保持）。 @return 无返回值。 */
void XAreaSeries_setPointLabelsFont(XAreaSeries* self, const char* family,
                                    int pointSize);
/** @brief 查询点标签字体族。 @param self 目标序列指针。 @return 字体族。 */
const char* XAreaSeries_pointLabelsFontFamily(const XAreaSeries* self);
/** @brief 查询点标签字号。 @param self 目标序列指针。 @return 字号（磅）。 */
int XAreaSeries_pointLabelsFontSize(const XAreaSeries* self);
/** @brief 设置点标签裁剪。 @param self 目标序列指针。 @param clip true 裁剪。 @return 无返回值。 */
void XAreaSeries_setPointLabelsClipping(XAreaSeries* self, bool clip);
/** @brief 查询点标签裁剪。 @param self 目标序列指针。 @return 裁剪返回 true。 */
bool XAreaSeries_pointLabelsClipping(const XAreaSeries* self);
/** @brief 设置下边界基线值。 @param self 目标序列指针。 @param base 基线值。 @return 无返回值。 */
void XAreaSeries_setBaseValue(XAreaSeries* self, double base);
/** @brief 设置填充色。 @param self 目标序列指针。 @param color ARGB。 @return 无返回值。 */
void XAreaSeries_setColor(XAreaSeries* self, uint32_t color);
/** @brief 设置序列名。 @param self 目标序列指针。 @param name UTF-8 名称。 @return 无返回值。 */
void XAreaSeries_setName(XAreaSeries* self, const char* name);
/** @brief 读取序列名。 @param self 目标序列指针。 @return 序列名（UTF-8）。 */
const char* XAreaSeries_name(const XAreaSeries* self);

/** @brief 查询填充色。 @param self 目标序列指针。 @return ARGB（0=主题色）。 */
uint32_t XAreaSeries_color(const XAreaSeries* self);
/** @brief 设置画笔（颜色+线宽，对标 setPen(QPen)）。 @param self 目标序列指针。 @param color ARGB。 @param width 线宽。 @return 无返回值。 */
void XAreaSeries_setPen(XAreaSeries* self, uint32_t color, double width);
/** @brief 读取画笔（参数化，对标 pen()）。 @param self 目标序列指针。 @param color 输出颜色。 @param width 输出线宽。 @return 无返回值。 */
void XAreaSeries_pen(const XAreaSeries* self, uint32_t* color,
                     double* width);
/** @brief 设置画刷（颜色，对标 setBrush(QBrush)）。 @param self 目标序列指针。 @param color ARGB。 @return 无返回值。 */
void XAreaSeries_setBrush(XAreaSeries* self, uint32_t color);
/** @brief 读取画刷颜色（对标 brush()）。 @param self 目标序列指针。 @return ARGB。 */
uint32_t XAreaSeries_brush(const XAreaSeries* self);
/** @brief 读取点标签字体族（别名：对标 pointLabelsFont()）。 @param self 目标序列指针。 @return 字体族。 */
const char* XAreaSeries_pointLabelsFont(const XAreaSeries* self);

#endif /* XCHARTS_ON */
#ifdef __cplusplus
}
#endif
#endif /* XAREASERIES_H */