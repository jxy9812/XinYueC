/**
 * @file       XLegendMarker.h
 * @brief      XLegendMarker 图例标记（对标 Qt Charts 6.8 QLegendMarker）。
 * @details    一个标记对应一条序列（系列）在图例中的条目；持有标签、
 *             画刷/画笔、可见性与形状。Qt 的 QBrush/QPen/QFont 在本库以
 *             「颜色 + 线宽 / 字体族 + 字号」参数化承载。
 *
 *             本类为抽象基类（Qt 中 type()/series() 为纯虚）；
 *             C 版以 XLegendMarker_Type 返回具体标记类型、
 *             XLegendMarker_series 返回归属序列。
 * @note       模块总开关 XCHARTS_ON；实现只依赖 XinYueC 抽象层，
 *             禁止调用 Win32、POSIX、Qt 或其他平台 API。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XLEGENDMARKER_H
#define XLEGENDMARKER_H

#if XGUI_ON
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XObject.h"
#include "XString.h"
#include "XAbstractSeries.h"

#if XCHARTS_ON

/**
 * @brief      图例标记类型（对标 Qt Charts 6.8 QLegendMarker::LegendMarkerType，
 *             数值一致）。
 */
typedef enum XLegendMarker_LegendMarkerType
{
    XLegendMarker_LegendMarkerType_Area = 0,        /**< 面积序列标记（对标 LegendMarkerTypeArea）。 */
    XLegendMarker_LegendMarkerType_Bar = 1,         /**< 柱状序列标记（对标 LegendMarkerTypeBar）。 */
    XLegendMarker_LegendMarkerType_Pie = 2,         /**< 饼图序列标记（对标 LegendMarkerTypePie）。 */
    XLegendMarker_LegendMarkerType_XY = 3,          /**< XY 序列标记（对标 LegendMarkerTypeXY）。 */
    XLegendMarker_LegendMarkerType_BoxPlot = 4,     /**< 盒须序列标记（对标 LegendMarkerTypeBoxPlot）。 */
    XLegendMarker_LegendMarkerType_Candlestick = 5  /**< K 线序列标记（对标 LegendMarkerTypeCandlestick）。 */
} XLegendMarker_LegendMarkerType;

XCLASS_DEFINE_BEGING(XLegendMarker)
XCLASS_DEFINE_EXTEND_END(XLegendMarker, XObject)

/** @brief 图例标记对象（对标 QLegendMarker）。 */
typedef struct XLegendMarker
{
    XObject m_base;            /**< 基类成员；必须是第一个。 */

    /* ---- 8 字节对齐组 ---- */
    XString* m_label;          /**< 标签文本（对象拥有；默认取序列名）。 */
    XAbstractSeries* m_series; /**< 归属序列（借用；不拥有）。 */
    double m_penWidth;         /**< 画笔线宽（像素；默认 1）。 */

    /* ---- 4 字节对齐组 ---- */
    uint32_t m_labelBrushColor;/**< 标签画刷颜色（ARGB）。 */
    uint32_t m_penColor;       /**< 画笔颜色（ARGB）。 */
    uint32_t m_brushColor;     /**< 画刷颜色（ARGB）。 */
    int m_fontSize;            /**< 字体字号（磅；0=默认）。 */

    /* ---- 1 字节组 ---- */
    uint8_t m_markerType;      /**< XLegendMarker_LegendMarkerType（0-5）。 */
    bool m_visible;            /**< 标记可见（默认 true）。 */
} XLegendMarker;

/** @brief 初始化类虚函数表。 @return 共享虚函数表指针。 */
XVtable* XLegendMarker_class_init(void);

/**
 * @brief 初始化嵌入式图例标记（标签取序列名，不可见性默认可见）。
 * @param self 目标标记指针，不能为空。
 * @param series 归属序列（借用，不转移所有权）；可为 NULL。
 * @param type 标记类型（XLegendMarker_LegendMarkerType）。
 * @return 无返回值。
 */
void XLegendMarker_init(XLegendMarker* self, XAbstractSeries* series, int type);

/**
 * @brief 堆上创建图例标记。
 * @param memory 内存类型。
 * @param series 归属序列（借用）；可为 NULL。
 * @param type 标记类型。
 * @return 标记指针；分配失败返回 NULL。
 */
XLegendMarker* XLegendMarker_create_ex(XMemoryType memory,
                                       XAbstractSeries* series, int type);
#define XLegendMarker_create(series, type) \
    XLegendMarker_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, series, type)

/* ==================== 类型 / 归属（对标 QLegendMarker） ==================== */

/** @brief 查询标记类型（对标 type）。 @param self 目标标记指针。 @return 标记类型枚举。 */
int XLegendMarker_type(const XLegendMarker* self);
/** @brief 查询归属序列（对标 series；借用，不转移所有权）。
 * @param self 目标标记指针。
 * @return 借用 XAbstractSeries*；无归属返回 NULL。调用者不得释放。
 */
XAbstractSeries* XLegendMarker_series(const XLegendMarker* self);

/* ==================== 标签（对标 QLegendMarker） ==================== */

/** @brief 设置标签文本（XString 主版本；对标 setLabel）。 @param self 目标标记指针。 @param label 借用 XString*；NULL 清空。 @return 无返回值。 */
void XLegendMarker_setLabel(XLegendMarker* self, const XString* label);
/** @brief 设置标签文本（UTF-8 编码版本）。 @param self 目标标记指针。 @param label UTF-8 标签（借用）。 @return 无返回值。 */
void XLegendMarker_setLabel_utf8(XLegendMarker* self, const char* label);
/** @brief 设置标签文本（UTF-8 兼容别名）。 @param self 目标标记指针。 @param label UTF-8 标签（借用）。 @return 无返回值。 */
void XLegendMarker_setLabel_2(XLegendMarker* self, const char* label);
/** @brief 查询标签文本（借用；对标 label）。 @param self 目标标记指针。 @return 借用 XString*；调用者不得释放。 */
const XString* XLegendMarker_label_const(const XLegendMarker* self);
/** @brief 查询标签文本（兼容别名）。 @param self 目标标记指针。 @return 借用 XString*。 */
const XString* XLegendMarker_label(const XLegendMarker* self);
/** @brief 查询标签文本（UTF-8 借用视图）。 @param self 目标标记指针。 @return UTF-8 串。 */
const char* XLegendMarker_label_toUtf8(const XLegendMarker* self);

/* ==================== 外观（对标 QLegendMarker） ==================== */

/** @brief 设置标签画刷颜色（对标 setLabelBrush）。 @param self 目标标记指针。 @param color ARGB。 @return 无返回值。 */
void XLegendMarker_setLabelBrush(XLegendMarker* self, uint32_t color);
/** @brief 查询标签画刷颜色（对标 labelBrush）。 @param self 目标标记指针。 @return ARGB。 */
uint32_t XLegendMarker_labelBrush(const XLegendMarker* self);
/** @brief 设置字体（字体族 + 字号；对标 setFont(QFont)）。
 * @param self 目标标记指针。
 * @param family 借用 XString*；NULL 保持默认。
 * @param pointSize 字号（磅；0=保持默认）。
 * @return 无返回值。 */
void XLegendMarker_setFont(XLegendMarker* self, const XString* family,
                           int pointSize);
/** @brief 设置字体（UTF-8 编码版本）。 @param self 目标标记指针。 @param family UTF-8 字体族（借用）。 @param pointSize 字号。 @return 无返回值。 */
void XLegendMarker_setFont_utf8(XLegendMarker* self, const char* family,
                                int pointSize);
/** @brief 设置字体（UTF-8 兼容别名）。 @param self 目标标记指针。 @param family UTF-8 字体族（借用）。 @param pointSize 字号。 @return 无返回值。 */
void XLegendMarker_setFont_2(XLegendMarker* self, const char* family,
                             int pointSize);
/** @brief 查询字体字号（对标 font().pointSize）。 @param self 目标标记指针。 @return 字号（磅；0=默认）。 */
int XLegendMarker_fontSize(const XLegendMarker* self);
/** @brief 设置画笔（颜色 + 线宽；对标 setPen）。 @param self 目标标记指针。 @param color ARGB。 @param width 线宽。 @return 无返回值。 */
void XLegendMarker_setPen(XLegendMarker* self, uint32_t color, double width);
/** @brief 读取画笔（参数化；对标 pen）。 @param self 目标标记指针。 @param color 输出颜色；可空。 @param width 输出线宽；可空。 @return 无返回值。 */
void XLegendMarker_pen(const XLegendMarker* self, uint32_t* color,
                       double* width);
/** @brief 设置画刷颜色（对标 setBrush）。 @param self 目标标记指针。 @param color ARGB。 @return 无返回值。 */
void XLegendMarker_setBrush(XLegendMarker* self, uint32_t color);
/** @brief 查询画刷颜色（对标 brush）。 @param self 目标标记指针。 @return ARGB。 */
uint32_t XLegendMarker_brush(const XLegendMarker* self);

/* ==================== 可见性（对标 QLegendMarker） ==================== */

/** @brief 设置标记可见（对标 setVisible）。 @param self 目标标记指针。 @param visible true 显示。 @return 无返回值。 */
void XLegendMarker_setVisible(XLegendMarker* self, bool visible);
/** @brief 查询标记可见（对标 isVisible）。 @param self 目标标记指针。 @return 可见返回 true。 */
bool XLegendMarker_isVisible(const XLegendMarker* self);

/* ==================== 形状（对标 QLegendMarker::shape） ==================== */

/** @brief 设置标记形状（对标 setShape(QLegend::MarkerShape)）。
 * @param self 目标标记指针。
 * @param shape 形状枚举（见 XLegend_MarkerShape）。
 * @return 无返回值。
 */
void XLegendMarker_setShape(XLegendMarker* self, int shape);
/** @brief 查询标记形状（对标 shape）。
 * @param self 目标标记指针。
 * @return 形状枚举（见 XLegend_MarkerShape）。
 */
int XLegendMarker_shape(const XLegendMarker* self);

/* ==================== 信号（对标 QLegendMarker Q_SIGNALS） ==================== */

/** @brief clicked 信号（无载荷）。 */
void* XLegendMarker_clicked_signal(XLegendMarker* self);
/** @brief hovered 信号（载荷：状态）。 */
void* XLegendMarker_hovered_signal(XLegendMarker* self, bool status);
/** @brief labelChanged 信号（无载荷）。 */
void* XLegendMarker_labelChanged_signal(XLegendMarker* self);
/** @brief labelBrushChanged 信号（载荷：颜色）。 */
void* XLegendMarker_labelBrushChanged_signal(XLegendMarker* self, uint32_t color);
/** @brief fontChanged 信号（载荷：字号）。 */
void* XLegendMarker_fontChanged_signal(XLegendMarker* self, int pointSize);
/** @brief penChanged 信号（载荷：颜色, 线宽）。 */
void* XLegendMarker_penChanged_signal(XLegendMarker* self, uint32_t color,
                                      double width);
/** @brief brushChanged 信号（载荷：颜色）。 */
void* XLegendMarker_brushChanged_signal(XLegendMarker* self, uint32_t color);
/** @brief visibleChanged 信号（载荷：可见性）。 */
void* XLegendMarker_visibleChanged_signal(XLegendMarker* self, bool visible);
/** @brief shapeChanged 信号（载荷：形状）。 */
void* XLegendMarker_shapeChanged_signal(XLegendMarker* self, int shape);

#endif /* XCHARTS_ON */
#ifdef __cplusplus
}
#endif

#endif /* XGUI_ON */

#endif /* XLEGENDMARKER_H */
