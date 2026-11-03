/**
 * @file       XBoxPlotSeries.h
 * @brief      XBoxPlotSeries 盒须图序列（对标 Qt Charts 6.8 QBoxPlotSeries）。
 * @details    一序列 = 多个 XBoxSet（每组一个盒须）；持有盒宽、轮廓可见性
 *             与画笔/画刷。Qt 的 QPen/QBrush 以「颜色 + 线宽」参数化承载。
 * @note       模块总开关 XCHARTS_ON；实现只依赖 XinYueC 抽象层，
 *             禁止调用 Win32、POSIX、Qt 或其他平台 API。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XBOXPLOTSERIES_H
#define XBOXPLOTSERIES_H

#if XGUI_ON
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XAbstractSeries.h"
#include "XBoxSet.h"

#if XCHARTS_ON

XCLASS_DEFINE_BEGING(XBoxPlotSeries)
XCLASS_DEFINE_EXTEND_END(XBoxPlotSeries, XAbstractSeries)

/** @brief 盒须图序列（对标 QBoxPlotSeries）。 */
typedef struct XBoxPlotSeries
{
    XAbstractSeries m_base;    /**< 基类成员；必须是第一个。 */

    /* ---- 8 字节对齐组 ---- */
    XBoxSet** m_boxSets;       /**< 盒组指针数组（堆；对象拥有）。 */
    double m_boxWidth;         /**< 盒宽比例（默认 0.5，对标 Qt）。 */
    double m_penWidth;         /**< 画笔线宽（像素；默认 1）。 */

    /* ---- 4/2/1 字节组 ---- */
    uint32_t m_penColor;       /**< 画笔颜色（ARGB；0=主题默认）。 */
    uint32_t m_brushColor;     /**< 画刷颜色（ARGB；0=主题默认）。 */
    uint16_t m_count;          /**< 盒组数。 */
    uint16_t m_capacity;       /**< 盒组容量。 */
    bool m_boxOutlineVisible;  /**< 盒轮廓可见（默认 true）。 */
} XBoxPlotSeries;

/** @brief 初始化类虚函数表。 @return 共享虚函数表指针。 */
XVtable* XBoxPlotSeries_class_init(void);

/**
 * @brief 初始化嵌入式盒须图序列。
 * @param self 目标序列指针，不能为空。
 * @return 无返回值。
 */
void XBoxPlotSeries_init(XBoxPlotSeries* self);

/**
 * @brief 堆上创建盒须图序列。
 * @param memory 内存类型。
 * @return 序列指针；分配失败返回 NULL。
 */
XBoxPlotSeries* XBoxPlotSeries_create_ex(XMemoryType memory);
#define XBoxPlotSeries_create() XBoxPlotSeries_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

/* ==================== 类型查询（对标 QBoxPlotSeries） ==================== */

/** @brief 查询序列类型（对标 QBoxPlotSeries::type）。 @param self 目标序列指针。 @return 固定返回 BoxPlot。 */
int XBoxPlotSeries_type(const XBoxPlotSeries* self);

/* ==================== 盒组集合（对标 QBoxPlotSeries） ==================== */

/** @brief 追加盒组并接管所有权（对标 append）。
 * @param self 目标序列指针。
 * @param box 盒组指针；NULL 或已存在返回 false。
 * @return 追加成功返回 true。
 */
bool XBoxPlotSeries_append(XBoxPlotSeries* self, XBoxSet* box);

/** @brief 批量追加盒组并接管所有权（对标 append(QList<QBoxSet*>)）。
 * @param self 目标序列指针。
 * @param boxes 盒组指针数组。
 * @param count 数量（须 >=1）。
 * @return 全部成功返回 true。
 */
bool XBoxPlotSeries_appendList(XBoxPlotSeries* self, XBoxSet* const* boxes,
                               int count);

/** @brief 在指定位置插入盒组并接管所有权（对标 insert）。
 * @param self 目标序列指针。
 * @param index 插入位置（越界按 [0,count] 钳位）。
 * @param box 盒组指针。
 * @return 插入成功返回 true。
 */
bool XBoxPlotSeries_insert(XBoxPlotSeries* self, int index, XBoxSet* box);

/** @brief 移除并释放盒组（对标 remove）。
 * @param self 目标序列指针。
 * @param box 盒组指针。
 * @return 移除成功返回 true。
 */
bool XBoxPlotSeries_remove(XBoxPlotSeries* self, XBoxSet* box);

/** @brief 摘除盒组但保留所有权（对标 take）。
 * @param self 目标序列指针。
 * @param box 盒组指针。
 * @return 摘除成功返回 true。
 */
bool XBoxPlotSeries_take(XBoxPlotSeries* self, XBoxSet* box);

/** @brief 清空并释放全部盒组（对标 clear）。 @param self 目标序列指针。 @return 无返回值。 */
void XBoxPlotSeries_clear(XBoxPlotSeries* self);

/** @brief 查询盒组数（对标 count）。 @param self 目标序列指针。 @return 盒组数。 */
int XBoxPlotSeries_count(const XBoxPlotSeries* self);

/** @brief 按下标读取盒组（对标 boxSets().at()）。
 * @param self 目标序列指针。
 * @param index 下标。
 * @return 盒组指针；越界返回 NULL。
 */
XBoxSet* XBoxPlotSeries_boxSetAt(const XBoxPlotSeries* self, int index);

/** @brief 批量读取盒组指针（对标 boxSets()；所有权仍在序列）。
 * @param self 目标序列指针。
 * @param out 输出缓冲（调用方提供）。
 * @param maxCount 缓冲容量。
 * @return 实际盒组数。
 */
int XBoxPlotSeries_boxSets(const XBoxPlotSeries* self, XBoxSet** out,
                           int maxCount);

/* ==================== 外观（对标 QBoxPlotSeries） ==================== */

/** @brief 设置盒轮廓可见（对标 setBoxOutlineVisible）。 @param self 目标序列指针。 @param visible true 显示。 @return 无返回值。 */
void XBoxPlotSeries_setBoxOutlineVisible(XBoxPlotSeries* self, bool visible);
/** @brief 查询盒轮廓可见（对标 boxOutlineVisible）。 @param self 目标序列指针。 @return 可见返回 true。 */
bool XBoxPlotSeries_boxOutlineVisible(const XBoxPlotSeries* self);
/** @brief 设置盒宽比例（对标 setBoxWidth）。 @param self 目标序列指针。 @param width 比例（<0 钳位 0）。 @return 无返回值。 */
void XBoxPlotSeries_setBoxWidth(XBoxPlotSeries* self, double width);
/** @brief 查询盒宽比例（对标 boxWidth）。 @param self 目标序列指针。 @return 比例。 */
double XBoxPlotSeries_boxWidth(const XBoxPlotSeries* self);
/** @brief 设置画笔（颜色 + 线宽；对标 setPen）。 @param self 目标序列指针。 @param color ARGB。 @param width 线宽。 @return 无返回值。 */
void XBoxPlotSeries_setPen(XBoxPlotSeries* self, uint32_t color, double width);
/** @brief 读取画笔（参数化；对标 pen）。 @param self 目标序列指针。 @param color 输出颜色；可空。 @param width 输出线宽；可空。 @return 无返回值。 */
void XBoxPlotSeries_pen(const XBoxPlotSeries* self, uint32_t* color,
                        double* width);
/** @brief 设置画刷颜色（对标 setBrush）。 @param self 目标序列指针。 @param color ARGB。 @return 无返回值。 */
void XBoxPlotSeries_setBrush(XBoxPlotSeries* self, uint32_t color);
/** @brief 读取画刷颜色（对标 brush）。 @param self 目标序列指针。 @return ARGB。 */
uint32_t XBoxPlotSeries_brush(const XBoxPlotSeries* self);

/* ==================== 信号（对标 QBoxPlotSeries Q_SIGNALS） ==================== */

/** @brief clicked 信号（载荷：盒组）。 */
void* XBoxPlotSeries_clicked_signal(XBoxPlotSeries* self, XBoxSet* boxset);
/** @brief hovered 信号（载荷：状态, 盒组）。 */
void* XBoxPlotSeries_hovered_signal(XBoxPlotSeries* self, bool status,
                                    XBoxSet* boxset);
/** @brief pressed 信号（载荷：盒组）。 */
void* XBoxPlotSeries_pressed_signal(XBoxPlotSeries* self, XBoxSet* boxset);
/** @brief released 信号（载荷：盒组）。 */
void* XBoxPlotSeries_released_signal(XBoxPlotSeries* self, XBoxSet* boxset);
/** @brief doubleClicked 信号（载荷：盒组）。 */
void* XBoxPlotSeries_doubleClicked_signal(XBoxPlotSeries* self, XBoxSet* boxset);
/** @brief countChanged 信号（无载荷）。 */
void* XBoxPlotSeries_countChanged_signal(XBoxPlotSeries* self);
/** @brief penChanged 信号（载荷：颜色, 线宽）。 */
void* XBoxPlotSeries_penChanged_signal(XBoxPlotSeries* self, uint32_t color,
                                       double width);
/** @brief brushChanged 信号（载荷：颜色）。 */
void* XBoxPlotSeries_brushChanged_signal(XBoxPlotSeries* self, uint32_t color);
/** @brief boxOutlineVisibilityChanged 信号（载荷：可见性）。 */
void* XBoxPlotSeries_boxOutlineVisibilityChanged_signal(XBoxPlotSeries* self,
                                                        bool visible);
/** @brief boxWidthChanged 信号（载荷：盒宽）。 */
void* XBoxPlotSeries_boxWidthChanged_signal(XBoxPlotSeries* self, double width);
/** @brief boxsetsAdded 信号（载荷：盒组数组, 数量）。 */
void* XBoxPlotSeries_boxsetsAdded_signal(XBoxPlotSeries* self,
                                         XBoxSet* const* sets, int count);
/** @brief boxsetsRemoved 信号（载荷：盒组数组, 数量）。 */
void* XBoxPlotSeries_boxsetsRemoved_signal(XBoxPlotSeries* self,
                                           XBoxSet* const* sets, int count);

#endif /* XCHARTS_ON */
#ifdef __cplusplus
}
#endif

#endif /* XGUI_ON */

#endif /* XBOXPLOTSERIES_H */
