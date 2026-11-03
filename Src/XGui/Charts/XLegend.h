/**
 * @file       XLegend.h
 * @brief      XLegend 图表图例（对标 Qt Charts 6.8 QLegend）。
 * @details    图例持有标记集合（每序列一个 XLegendMarker）、对齐方式、
 *             背景可见性与外观。Qt 中 QLegend 继承 QGraphicsWidget；
 *             本库无 GraphicsWidget 框架，故以 XObject 派生并承担
 *             「模型 + 布局参数」职责，实际绘制由 XChartView 读取本类状态完成。
 *
 *             Qt 的 QBrush/QPen/QFont/QColor 在本库以「颜色 + 线宽 /
 *             字体族 + 字号」参数化承载。
 * @note       模块总开关 XCHARTS_ON；实现只依赖 XinYueC 抽象层，
 *             禁止调用 Win32、POSIX、Qt 或其他平台 API。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XLEGEND_H
#define XLEGEND_H

#if XGUI_ON
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XObject.h"
#include "XString.h"
#include "XLegendMarker.h"

#if XCHARTS_ON

/**
 * @brief      图例标记形状（对标 Qt Charts 6.8 QLegend::MarkerShape，数值一致）。
 */
typedef enum XLegend_MarkerShape
{
    XLegend_MarkerShape_Default = 0,          /**< 默认（随序列类型，对标 MarkerShapeDefault）。 */
    XLegend_MarkerShape_Rectangle = 1,        /**< 矩形（对标 MarkerShapeRectangle）。 */
    XLegend_MarkerShape_Circle = 2,           /**< 圆形（对标 MarkerShapeCircle）。 */
    XLegend_MarkerShape_FromSeries = 3,       /**< 取序列自身形状（对标 MarkerShapeFromSeries）。 */
    XLegend_MarkerShape_RotatedRectangle = 4, /**< 旋转矩形（对标 MarkerShapeRotatedRectangle）。 */
    XLegend_MarkerShape_Triangle = 5,         /**< 三角形（对标 MarkerShapeTriangle）。 */
    XLegend_MarkerShape_Star = 6,             /**< 星形（对标 MarkerShapeStar）。 */
    XLegend_MarkerShape_Pentagon = 7          /**< 五边形（对标 MarkerShapePentagon）。 */
} XLegend_MarkerShape;

/**
 * @brief      图例对齐方式位（对标 Qt::Alignment，数值与 Qt 一致）。
 * @details    可按位组合（如 Top|AlignLeft）；与 Qt::Alignment 同值，
 *             便于跨端对照。
 */
typedef enum XLegend_Alignment
{
    XLegend_Alignment_AlignLeft = 0x0001,    /**< 左对齐（对标 Qt::AlignLeft）。 */
    XLegend_Alignment_AlignRight = 0x0002,   /**< 右对齐（对标 Qt::AlignRight）。 */
    XLegend_Alignment_AlignHCenter = 0x0004, /**< 水平居中（对标 Qt::AlignHCenter）。 */
    XLegend_Alignment_AlignTop = 0x0020,     /**< 顶对齐（对标 Qt::AlignTop）。 */
    XLegend_Alignment_AlignBottom = 0x0040,  /**< 底对齐（对标 Qt::AlignBottom）。 */
    XLegend_Alignment_AlignVCenter = 0x0080  /**< 垂直居中（对标 Qt::AlignVCenter）。 */
} XLegend_Alignment;

XCLASS_DEFINE_BEGING(XLegend)
XCLASS_DEFINE_EXTEND_END(XLegend, XObject)

/** @brief 图例对象（对标 QLegend）。 */
typedef struct XLegend
{
    XObject m_base;             /**< 基类成员；必须是第一个。 */

    /* ---- 8 字节对齐组 ---- */
    XLegendMarker** m_markers;  /**< 标记数组（堆；对象拥有）。 */
    XString* m_fontFamily;      /**< 字体族（对象拥有）。 */
    double m_penWidth;          /**< 边框画笔线宽（像素；默认 1）。 */

    /* ---- 4 字节对齐组 ---- */
    uint32_t m_brushColor;      /**< 背景画刷颜色（ARGB；0=主题默认）。 */
    uint32_t m_penColor;        /**< 边框画笔颜色（ARGB；0=主题默认）。 */
    uint32_t m_labelBrushColor; /**< 标签画刷颜色（ARGB；0=主题默认）。 */
    uint32_t m_alignment;       /**< 对齐位集合（XLegend_Alignment 组合）。 */
    int m_fontSize;             /**< 字体字号（磅；0=默认）。 */

    /* ---- 2/1 字节组 ---- */
    uint16_t m_count;           /**< 标记数。 */
    uint16_t m_capacity;        /**< 标记容量。 */
    uint8_t m_markerShape;      /**< XLegend_MarkerShape（默认 Default）。 */
    bool m_backgroundVisible;   /**< 背景可见（默认 true）。 */
    bool m_reverseMarkers;      /**< 反序显示（默认 false）。 */
    bool m_showToolTips;        /**< 悬停提示（默认 true）。 */
    bool m_interactive;         /**< 可交互（默认 false）。 */
    bool m_attachedToChart;     /**< 已挂接图表（默认 true）。 */
    bool m_visible;             /**< 图例可见（默认 true；对标 QGraphicsWidget）。 */
} XLegend;

/** @brief 初始化类虚函数表。 @return 共享虚函数表指针。 */
XVtable* XLegend_class_init(void);

/**
 * @brief 初始化嵌入式图例。
 * @param self 目标图例指针，不能为空。
 * @return 无返回值。
 */
void XLegend_init(XLegend* self);

/**
 * @brief 堆上创建图例。
 * @param memory 内存类型。
 * @return 图例指针；分配失败返回 NULL。
 */
XLegend* XLegend_create_ex(XMemoryType memory);
#define XLegend_create() XLegend_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

/* ==================== 标记集合（对标 QLegend::markers） ==================== */

/** @brief 追加标记并接管所有权（内部使用；qt 由图表在 addSeries 时自动建）。
 * @param self 目标图例指针。
 * @param marker 标记指针。
 * @return 追加成功返回 true。
 */
bool XLegend_appendMarker(XLegend* self, XLegendMarker* marker);

/** @brief 移除并释放某序列对应的标记（对标序列移除时清理图例项）。
 * @param self 目标图例指针。
 * @param series 归属序列指针。
 * @return 找到并移除返回 true。
 */
bool XLegend_removeMarkerForSeries(XLegend* self, XAbstractSeries* series);

/** @brief 清空并释放全部标记（对标 removeAllSeries 时的图例清理）。
 * @param self 目标图例指针。
 * @return 无返回值。
 */
void XLegend_clearMarkers(XLegend* self);

/** @brief 查询标记数。 @param self 目标图例指针。 @return 标记数。 */
int XLegend_markerCount(const XLegend* self);
/** @brief 按下标取标记（借用；对标 markers().at()）。
 * @param self 目标图例指针。
 * @param index 下标。
 * @return 借用 XLegendMarker*；越界返回 NULL。调用者不得释放。
 */
XLegendMarker* XLegend_markerAt(const XLegend* self, int index);
/** @brief 按序列取标记（借用；对标 markers(series)）。
 * @param self 目标图例指针。
 * @param series 归属序列；NULL 表示取首个标记。
 * @return 借用 XLegendMarker*；未找到返回 NULL。
 */
XLegendMarker* XLegend_marker(XLegend* self, XAbstractSeries* series);

/* ==================== 外观（对标 QLegend） ==================== */

/** @brief 设置背景画刷颜色（对标 setBrush）。 @param self 目标图例指针。 @param color ARGB。 @return 无返回值。 */
void XLegend_setBrush(XLegend* self, uint32_t color);
/** @brief 查询背景画刷颜色（对标 brush）。 @param self 目标图例指针。 @return ARGB。 */
uint32_t XLegend_brush(const XLegend* self);
/** @brief 设置背景颜色（对标 setColor；等价设画刷色）。 @param self 目标图例指针。 @param color ARGB。 @return 无返回值。 */
void XLegend_setColor(XLegend* self, uint32_t color);
/** @brief 查询背景颜色（对标 color）。 @param self 目标图例指针。 @return ARGB。 */
uint32_t XLegend_color(const XLegend* self);
/** @brief 设置边框画笔（对标 setPen）。 @param self 目标图例指针。 @param color ARGB。 @param width 线宽。 @return 无返回值。 */
void XLegend_setPen(XLegend* self, uint32_t color, double width);
/** @brief 读取边框画笔（对标 pen）。 @param self 目标图例指针。 @param color 输出颜色；可空。 @param width 输出线宽；可空。 @return 无返回值。 */
void XLegend_pen(const XLegend* self, uint32_t* color, double* width);
/** @brief 设置边框颜色（对标 setBorderColor；等价设画笔色）。 @param self 目标图例指针。 @param color ARGB。 @return 无返回值。 */
void XLegend_setBorderColor(XLegend* self, uint32_t color);
/** @brief 查询边框颜色（对标 borderColor）。 @param self 目标图例指针。 @return ARGB。 */
uint32_t XLegend_borderColor(const XLegend* self);
/** @brief 设置字体（对标 setFont）。 @param self 目标图例指针。 @param family 借用 XString*；NULL 保持。 @param pointSize 字号（磅；0=默认）。 @return 无返回值。 */
void XLegend_setFont(XLegend* self, const XString* family, int pointSize);
/** @brief 设置字体（UTF-8 编码版本）。 @param self 目标图例指针。 @param family UTF-8 字体族（借用）。 @param pointSize 字号。 @return 无返回值。 */
void XLegend_setFont_utf8(XLegend* self, const char* family, int pointSize);
/** @brief 设置字体（UTF-8 兼容别名）。 @param self 目标图例指针。 @param family UTF-8 字体族（借用）。 @param pointSize 字号。 @return 无返回值。 */
void XLegend_setFont_2(XLegend* self, const char* family, int pointSize);
/** @brief 查询字体族（借用；对标 font().family()）。 @param self 目标图例指针。 @return 借用 XString*；调用者不得释放。 */
const XString* XLegend_fontFamily_const(const XLegend* self);
/** @brief 查询字体族（兼容别名）。 @param self 目标图例指针。 @return 借用 XString*。 */
const XString* XLegend_fontFamily(const XLegend* self);
/** @brief 查询字体字号（对标 font().pointSize）。 @param self 目标图例指针。 @return 字号（磅；0=默认）。 */
int XLegend_fontSize(const XLegend* self);
/** @brief 设置标签画刷颜色（对标 setLabelBrush）。 @param self 目标图例指针。 @param color ARGB。 @return 无返回值。 */
void XLegend_setLabelBrush(XLegend* self, uint32_t color);
/** @brief 查询标签画刷颜色（对标 labelBrush）。 @param self 目标图例指针。 @return ARGB。 */
uint32_t XLegend_labelBrush(const XLegend* self);
/** @brief 设置标签颜色（对标 setLabelColor；等价设标签画刷色）。 @param self 目标图例指针。 @param color ARGB。 @return 无返回值。 */
void XLegend_setLabelColor(XLegend* self, uint32_t color);
/** @brief 查询标签颜色（对标 labelColor）。 @param self 目标图例指针。 @return ARGB。 */
uint32_t XLegend_labelColor(const XLegend* self);

/* ==================== 对齐 / 附着 / 可见性（对标 QLegend） ==================== */

/** @brief 设置对齐方式（对标 setAlignment；XLegend_Alignment 位集合）。
 * @param self 目标图例指针。
 * @param alignment 对齐位集合。
 * @return 无返回值。
 */
void XLegend_setAlignment(XLegend* self, uint32_t alignment);
/** @brief 查询对齐方式（对标 alignment）。 @param self 目标图例指针。 @return 对齐位集合。 */
uint32_t XLegend_alignment(const XLegend* self);

/** @brief 从图表分离（对标 detachFromChart）。 @param self 目标图例指针。 @return 无返回值。 */
void XLegend_detachFromChart(XLegend* self);
/** @brief 重新挂接图表（对标 attachToChart）。 @param self 目标图例指针。 @return 无返回值。 */
void XLegend_attachToChart(XLegend* self);
/** @brief 查询是否已挂接图表（对标 isAttachedToChart）。 @param self 目标图例指针。 @return 已挂接返回 true。 */
bool XLegend_isAttachedToChart(const XLegend* self);

/* ==================== 可见性（对标 QGraphicsWidget::setVisible） ==================== */

/** @brief 设置图例可见（对标 QGraphicsWidget::setVisible；Qt 用 legend()->setVisible 隐藏图例）。
 * @param self 目标图例指针。
 * @param visible true 显示。
 * @return 无返回值。
 */
void XLegend_setVisible(XLegend* self, bool visible);
/** @brief 查询图例可见（对标 QGraphicsWidget::isVisible）。
 * @param self 目标图例指针。
 * @return 可见返回 true；self 为 NULL 返回 false。
 */
bool XLegend_isVisible(const XLegend* self);

/** @brief 设置背景可见（对标 setBackgroundVisible）。 @param self 目标图例指针。 @param visible true 显示。 @return 无返回值。 */
void XLegend_setBackgroundVisible(XLegend* self, bool visible);
/** @brief 查询背景可见（对标 isBackgroundVisible）。 @param self 目标图例指针。 @return 可见返回 true。 */
bool XLegend_isBackgroundVisible(const XLegend* self);

/* ==================== 反序 / 提示 / 交互 / 形状（对标 QLegend） ==================== */

/** @brief 设置标记反序（对标 setReverseMarkers）。 @param self 目标图例指针。 @param reverseMarkers true 反序。 @return 无返回值。 */
void XLegend_setReverseMarkers(XLegend* self, bool reverseMarkers);
/** @brief 查询标记反序（对标 reverseMarkers）。 @param self 目标图例指针。 @return 反序返回 true。 */
bool XLegend_reverseMarkers(const XLegend* self);
/** @brief 设置悬停提示（对标 setShowToolTips）。 @param self 目标图例指针。 @param show true 显示提示。 @return 无返回值。 */
void XLegend_setShowToolTips(XLegend* self, bool show);
/** @brief 查询悬停提示（对标 showToolTips）。 @param self 目标图例指针。 @return 显示提示返回 true。 */
bool XLegend_showToolTips(const XLegend* self);
/** @brief 设置可交互（对标 setInteractive）。 @param self 目标图例指针。 @param interactive true 可交互。 @return 无返回值。 */
void XLegend_setInteractive(XLegend* self, bool interactive);
/** @brief 查询可交互（对标 isInteractive）。 @param self 目标图例指针。 @return 可交互返回 true。 */
bool XLegend_isInteractive(const XLegend* self);
/** @brief 设置标记形状（对标 setMarkerShape）。 @param self 目标图例指针。 @param shape XLegend_MarkerShape。 @return 无返回值。 */
void XLegend_setMarkerShape(XLegend* self, int shape);
/** @brief 查询标记形状（对标 markerShape）。 @param self 目标图例指针。 @return XLegend_MarkerShape。 */
int XLegend_markerShape(const XLegend* self);

/* ==================== 信号（对标 QLegend Q_SIGNALS） ==================== */

/** @brief backgroundVisibleChanged 信号（载荷：可见性）。 */
void* XLegend_backgroundVisibleChanged_signal(XLegend* self, bool visible);
/** @brief colorChanged 信号（载荷：颜色）。 */
void* XLegend_colorChanged_signal(XLegend* self, uint32_t color);
/** @brief borderColorChanged 信号（载荷：颜色）。 */
void* XLegend_borderColorChanged_signal(XLegend* self, uint32_t color);
/** @brief fontChanged 信号（载荷：字号）。 */
void* XLegend_fontChanged_signal(XLegend* self, int pointSize);
/** @brief labelColorChanged 信号（载荷：颜色）。 */
void* XLegend_labelColorChanged_signal(XLegend* self, uint32_t color);
/** @brief reverseMarkersChanged 信号（载荷：反序）。 */
void* XLegend_reverseMarkersChanged_signal(XLegend* self, bool reverseMarkers);
/** @brief showToolTipsChanged 信号（载荷：显示提示）。 */
void* XLegend_showToolTipsChanged_signal(XLegend* self, bool showToolTips);
/** @brief markerShapeChanged 信号（载荷：形状）。 */
void* XLegend_markerShapeChanged_signal(XLegend* self, int shape);
/** @brief attachedToChartChanged 信号（载荷：已挂接）。 */
void* XLegend_attachedToChartChanged_signal(XLegend* self, bool attachedToChart);
/** @brief interactiveChanged 信号（载荷：可交互）。 */
void* XLegend_interactiveChanged_signal(XLegend* self, bool interactive);

#endif /* XCHARTS_ON */
#ifdef __cplusplus
}
#endif

#endif /* XGUI_ON */

#endif /* XLEGEND_H */
