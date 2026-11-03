/**
 * @file       XCandlestickSeries.h
 * @brief      XCandlestickSeries K 线图序列（对标 Qt Charts 6.8
 *             QCandlestickSeries）。
 * @details    一序列 = 多个 XCandlestickSet（每组一根 K 线）；持有柱宽、
 *             帽宽、涨跌配色与画笔/画刷。Qt 的 QPen/QBrush 以「颜色 +
 *             线宽」参数化承载。
 * @note       模块总开关 XCHARTS_ON；实现只依赖 XinYueC 抽象层，
 *             禁止调用 Win32、POSIX、Qt 或其他平台 API。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XCANDLESTICKSERIES_H
#define XCANDLESTICKSERIES_H

#if XGUI_ON
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XAbstractSeries.h"
#include "XCandlestickSet.h"

#if XCHARTS_ON

XCLASS_DEFINE_BEGING(XCandlestickSeries)
XCLASS_DEFINE_EXTEND_END(XCandlestickSeries, XAbstractSeries)

/** @brief K 线图序列（对标 QCandlestickSeries）。 */
typedef struct XCandlestickSeries
{
    XAbstractSeries m_base;          /**< 基类成员；必须是第一个。 */

    /* ---- 8 字节对齐组 ---- */
    XCandlestickSet** m_sets;        /**< K 线组指针数组（堆；对象拥有）。 */
    double m_maximumColumnWidth;     /**< 最大柱宽（对标 maximumColumnWidth；默认 1.0）。 */
    double m_minimumColumnWidth;     /**< 最小柱宽（对标 minimumColumnWidth；默认 0.0）。 */
    double m_bodyWidth;              /**< 实体宽比例（对标 bodyWidth；默认 0.8）。 */
    double m_capsWidth;              /**< 帽宽比例（对标 capsWidth；默认 0.5）。 */
    double m_penWidth;               /**< 画笔线宽（像素；默认 1）。 */

    /* ---- 4/2/1 字节组 ---- */
    uint32_t m_increasingColor;      /**< 上涨色（ARGB；对标 increasingColor）。 */
    uint32_t m_decreasingColor;      /**< 下跌色（ARGB；对标 decreasingColor）。 */
    uint32_t m_penColor;             /**< 画笔颜色（ARGB；0=主题默认）。 */
    uint32_t m_brushColor;           /**< 画刷颜色（ARGB；0=主题默认）。 */
    uint16_t m_count;                /**< K 线组数。 */
    uint16_t m_capacity;             /**< K 线组容量。 */
    bool m_bodyOutlineVisible;       /**< 实体轮廓可见（对标 bodyOutlineVisible；默认 true）。 */
    bool m_capsVisible;              /**< 帽可见（对标 capsVisible；默认 true）。 */
} XCandlestickSeries;

/** @brief 初始化类虚函数表。 @return 共享虚函数表指针。 */
XVtable* XCandlestickSeries_class_init(void);

/**
 * @brief 初始化嵌入式 K 线图序列。
 * @param self 目标序列指针，不能为空。
 * @return 无返回值。
 */
void XCandlestickSeries_init(XCandlestickSeries* self);

/**
 * @brief 堆上创建 K 线图序列。
 * @param memory 内存类型。
 * @return 序列指针；分配失败返回 NULL。
 */
XCandlestickSeries* XCandlestickSeries_create_ex(XMemoryType memory);
#define XCandlestickSeries_create() \
    XCandlestickSeries_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

/* ==================== 类型查询（对标 QCandlestickSeries） ==================== */

/** @brief 查询序列类型（对标 QCandlestickSeries::type）。 @param self 目标序列指针。 @return 固定返回 Candlestick。 */
int XCandlestickSeries_type(const XCandlestickSeries* self);

/* ==================== 集合（对标 QCandlestickSeries） ==================== */

/** @brief 追加 K 线组并接管所有权（对标 append(QCandlestickSet*)）。
 * @param self 目标序列指针。
 * @param set K 线组指针；NULL 或已存在返回 false。
 * @return 追加成功返回 true。
 */
bool XCandlestickSeries_append(XCandlestickSeries* self, XCandlestickSet* set);
/** @brief 批量追加（对标 append(QList)）。 @param self 目标序列指针。 @param sets 数组。 @param count 数量。 @return 全部成功返回 true。 */
bool XCandlestickSeries_appendList(XCandlestickSeries* self,
                                   XCandlestickSet* const* sets, int count);
/** @brief 在指定位置插入（对标 insert）。 @param self 目标序列指针。 @param index 位置。 @param set K 线组。 @return 成功返回 true。 */
bool XCandlestickSeries_insert(XCandlestickSeries* self, int index,
                               XCandlestickSet* set);
/** @brief 移除并释放（对标 remove）。 @param self 目标序列指针。 @param set K 线组。 @return 成功返回 true。 */
bool XCandlestickSeries_remove(XCandlestickSeries* self, XCandlestickSet* set);
/** @brief 批量移除并释放（对标 remove(QList)）。 @param self 目标序列指针。 @param sets 数组。 @param count 数量。 @return 全部成功返回 true。 */
bool XCandlestickSeries_removeList(XCandlestickSeries* self,
                                   XCandlestickSet* const* sets, int count);
/** @brief 摘除但保留所有权（对标 take）。 @param self 目标序列指针。 @param set K 线组。 @return 成功返回 true。 */
bool XCandlestickSeries_take(XCandlestickSeries* self, XCandlestickSet* set);
/** @brief 清空并释放全部（对标 clear）。 @param self 目标序列指针。 @return 无返回值。 */
void XCandlestickSeries_clear(XCandlestickSeries* self);
/** @brief 查询 K 线组数（对标 count）。 @param self 目标序列指针。 @return 组数。 */
int XCandlestickSeries_count(const XCandlestickSeries* self);
/** @brief 按下标读取 K 线组（对标 sets().at()）。 @param self 目标序列指针。 @param index 下标。 @return K 线组指针；越界 NULL。 */
XCandlestickSet* XCandlestickSeries_setAt(const XCandlestickSeries* self,
                                          int index);
/** @brief 批量读取 K 线组（对标 sets()）。 @param self 目标序列指针。 @param out 输出缓冲。 @param maxCount 容量。 @return 实际组数。 */
int XCandlestickSeries_sets(const XCandlestickSeries* self,
                            XCandlestickSet** out, int maxCount);

/* ==================== 外观（对标 QCandlestickSeries） ==================== */

/** @brief 设置最大柱宽（对标 setMaximumColumnWidth）。 @param self 目标序列指针。 @param width 宽度。 @return 无返回值。 */
void XCandlestickSeries_setMaximumColumnWidth(XCandlestickSeries* self,
                                              double width);
/** @brief 查询最大柱宽（对标 maximumColumnWidth）。 @param self 目标序列指针。 @return 宽度。 */
double XCandlestickSeries_maximumColumnWidth(const XCandlestickSeries* self);
/** @brief 设置最小柱宽（对标 setMinimumColumnWidth）。 @param self 目标序列指针。 @param width 宽度。 @return 无返回值。 */
void XCandlestickSeries_setMinimumColumnWidth(XCandlestickSeries* self,
                                              double width);
/** @brief 查询最小柱宽（对标 minimumColumnWidth）。 @param self 目标序列指针。 @return 宽度。 */
double XCandlestickSeries_minimumColumnWidth(const XCandlestickSeries* self);
/** @brief 设置实体宽比例（对标 setBodyWidth）。 @param self 目标序列指针。 @param width 比例 0-1。 @return 无返回值。 */
void XCandlestickSeries_setBodyWidth(XCandlestickSeries* self, double width);
/** @brief 查询实体宽比例（对标 bodyWidth）。 @param self 目标序列指针。 @return 比例。 */
double XCandlestickSeries_bodyWidth(const XCandlestickSeries* self);
/** @brief 设置实体轮廓可见（对标 setBodyOutlineVisible）。 @param self 目标序列指针。 @param visible true 显示。 @return 无返回值。 */
void XCandlestickSeries_setBodyOutlineVisible(XCandlestickSeries* self,
                                              bool visible);
/** @brief 查询实体轮廓可见（对标 bodyOutlineVisible）。 @param self 目标序列指针。 @return 可见返回 true。 */
bool XCandlestickSeries_bodyOutlineVisible(const XCandlestickSeries* self);
/** @brief 设置帽宽比例（对标 setCapsWidth）。 @param self 目标序列指针。 @param width 比例 0-1。 @return 无返回值。 */
void XCandlestickSeries_setCapsWidth(XCandlestickSeries* self, double width);
/** @brief 查询帽宽比例（对标 capsWidth）。 @param self 目标序列指针。 @return 比例。 */
double XCandlestickSeries_capsWidth(const XCandlestickSeries* self);
/** @brief 设置帽可见（对标 setCapsVisible）。 @param self 目标序列指针。 @param visible true 显示。 @return 无返回值。 */
void XCandlestickSeries_setCapsVisible(XCandlestickSeries* self, bool visible);
/** @brief 查询帽可见（对标 capsVisible）。 @param self 目标序列指针。 @return 可见返回 true。 */
bool XCandlestickSeries_capsVisible(const XCandlestickSeries* self);
/** @brief 设置上涨色（对标 setIncreasingColor）。 @param self 目标序列指针。 @param color ARGB。 @return 无返回值。 */
void XCandlestickSeries_setIncreasingColor(XCandlestickSeries* self,
                                           uint32_t color);
/** @brief 查询上涨色（对标 increasingColor）。 @param self 目标序列指针。 @return ARGB。 */
uint32_t XCandlestickSeries_increasingColor(const XCandlestickSeries* self);
/** @brief 设置下跌色（对标 setDecreasingColor）。 @param self 目标序列指针。 @param color ARGB。 @return 无返回值。 */
void XCandlestickSeries_setDecreasingColor(XCandlestickSeries* self,
                                           uint32_t color);
/** @brief 查询下跌色（对标 decreasingColor）。 @param self 目标序列指针。 @return ARGB。 */
uint32_t XCandlestickSeries_decreasingColor(const XCandlestickSeries* self);
/** @brief 设置画刷颜色（对标 setBrush）。 @param self 目标序列指针。 @param color ARGB。 @return 无返回值。 */
void XCandlestickSeries_setBrush(XCandlestickSeries* self, uint32_t color);
/** @brief 查询画刷颜色（对标 brush）。 @param self 目标序列指针。 @return ARGB。 */
uint32_t XCandlestickSeries_brush(const XCandlestickSeries* self);
/** @brief 设置画笔（颜色 + 线宽；对标 setPen）。 @param self 目标序列指针。 @param color ARGB。 @param width 线宽。 @return 无返回值。 */
void XCandlestickSeries_setPen(XCandlestickSeries* self, uint32_t color,
                               double width);
/** @brief 读取画笔（参数化；对标 pen）。 @param self 目标序列指针。 @param color 输出颜色；可空。 @param width 输出线宽；可空。 @return 无返回值。 */
void XCandlestickSeries_pen(const XCandlestickSeries* self, uint32_t* color,
                            double* width);

/* ==================== 信号（对标 QCandlestickSeries Q_SIGNALS） ==================== */

/** @brief clicked 信号（载荷：K 线组）。 */
void* XCandlestickSeries_clicked_signal(XCandlestickSeries* self,
                                        XCandlestickSet* set);
/** @brief hovered 信号（载荷：状态, K 线组）。 */
void* XCandlestickSeries_hovered_signal(XCandlestickSeries* self, bool status,
                                        XCandlestickSet* set);
/** @brief pressed 信号（载荷：K 线组）。 */
void* XCandlestickSeries_pressed_signal(XCandlestickSeries* self,
                                        XCandlestickSet* set);
/** @brief released 信号（载荷：K 线组）。 */
void* XCandlestickSeries_released_signal(XCandlestickSeries* self,
                                         XCandlestickSet* set);
/** @brief doubleClicked 信号（载荷：K 线组）。 */
void* XCandlestickSeries_doubleClicked_signal(XCandlestickSeries* self,
                                              XCandlestickSet* set);
/** @brief candlestickSetsAdded 信号（载荷：数组, 数量）。 */
void* XCandlestickSeries_candlestickSetsAdded_signal(XCandlestickSeries* self,
                                                     XCandlestickSet* const* sets,
                                                     int count);
/** @brief candlestickSetsRemoved 信号（载荷：数组, 数量）。 */
void* XCandlestickSeries_candlestickSetsRemoved_signal(
    XCandlestickSeries* self, XCandlestickSet* const* sets, int count);
/** @brief countChanged 信号（无载荷）。 */
void* XCandlestickSeries_countChanged_signal(XCandlestickSeries* self);
/** @brief maximumColumnWidthChanged 信号（载荷：宽度）。 */
void* XCandlestickSeries_maximumColumnWidthChanged_signal(
    XCandlestickSeries* self, double width);
/** @brief minimumColumnWidthChanged 信号（载荷：宽度）。 */
void* XCandlestickSeries_minimumColumnWidthChanged_signal(
    XCandlestickSeries* self, double width);
/** @brief bodyWidthChanged 信号（载荷：比例）。 */
void* XCandlestickSeries_bodyWidthChanged_signal(XCandlestickSeries* self,
                                                 double width);
/** @brief bodyOutlineVisibilityChanged 信号（载荷：可见性）。 */
void* XCandlestickSeries_bodyOutlineVisibilityChanged_signal(
    XCandlestickSeries* self, bool visible);
/** @brief capsWidthChanged 信号（载荷：比例）。 */
void* XCandlestickSeries_capsWidthChanged_signal(XCandlestickSeries* self,
                                                 double width);
/** @brief capsVisibilityChanged 信号（载荷：可见性）。 */
void* XCandlestickSeries_capsVisibilityChanged_signal(XCandlestickSeries* self,
                                                      bool visible);
/** @brief increasingColorChanged 信号（载荷：颜色）。 */
void* XCandlestickSeries_increasingColorChanged_signal(XCandlestickSeries* self,
                                                       uint32_t color);
/** @brief decreasingColorChanged 信号（载荷：颜色）。 */
void* XCandlestickSeries_decreasingColorChanged_signal(XCandlestickSeries* self,
                                                       uint32_t color);
/** @brief brushChanged 信号（载荷：颜色）。 */
void* XCandlestickSeries_brushChanged_signal(XCandlestickSeries* self,
                                             uint32_t color);
/** @brief penChanged 信号（载荷：颜色, 线宽）。 */
void* XCandlestickSeries_penChanged_signal(XCandlestickSeries* self,
                                           uint32_t color, double width);

#endif /* XCHARTS_ON */
#ifdef __cplusplus
}
#endif

#endif /* XGUI_ON */

#endif /* XCANDLESTICKSERIES_H */
