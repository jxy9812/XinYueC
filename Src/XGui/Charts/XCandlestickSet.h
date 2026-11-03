/**
 * @file       XCandlestickSet.h
 * @brief      XCandlestickSet K 线数据组（对标 Qt Charts 6.8 QCandlestickSet）。
 * @details    一组 K 线固定 5 个量：时间戳、开盘、最高、最低、收盘。
 *             Qt 的 QPen/QBrush 在本库以「颜色 + 线宽」参数化承载。
 * @note       模块总开关 XCHARTS_ON；实现只依赖 XinYueC 抽象层，
 *             禁止调用 Win32、POSIX、Qt 或其他平台 API。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XCANDLESTICKSET_H
#define XCANDLESTICKSET_H

#if XGUI_ON
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XObject.h"

#if XCHARTS_ON

XCLASS_DEFINE_BEGING(XCandlestickSet)
XCLASS_DEFINE_EXTEND_END(XCandlestickSet, XObject)

/** @brief K 线组对象（对标 QCandlestickSet）。 */
typedef struct XCandlestickSet
{
    XObject m_base;        /**< 基类成员；必须是第一个。 */

    /* ---- 8 字节对齐组 ---- */
    double m_timestamp;    /**< 时间戳（对标 timestamp；单位由调用方约定）。 */
    double m_open;         /**< 开盘价（对标 open）。 */
    double m_high;         /**< 最高价（对标 high）。 */
    double m_low;          /**< 最低价（对标 low）。 */
    double m_close;        /**< 收盘价（对标 close）。 */
    double m_penWidth;     /**< 画笔线宽（像素；默认 1）。 */

    /* ---- 4 字节对齐组 ---- */
    uint32_t m_penColor;   /**< 画笔颜色（ARGB；0=主题默认）。 */
    uint32_t m_brushColor; /**< 画刷颜色（ARGB；0=主题默认）。 */
} XCandlestickSet;

/** @brief 初始化类虚函数表。 @return 共享虚函数表指针。 */
XVtable* XCandlestickSet_class_init(void);

/**
 * @brief 初始化 K 线组（全 0 值；对标 QCandlestickSet(timestamp) 的默认形态）。
 * @param self 目标 K 线组指针，不能为空。
 * @return 无返回值。
 */
void XCandlestickSet_init(XCandlestickSet* self);

/**
 * @brief 初始化并设置时间戳（对标 QCandlestickSet(timestamp)）。
 * @param self 目标 K 线组指针。
 * @param timestamp 时间戳。
 * @return 无返回值。
 */
void XCandlestickSet_init_ex(XCandlestickSet* self, double timestamp);

/**
 * @brief 初始化并设置全部 5 个量（对标 QCandlestickSet(o,h,l,c,ts)）。
 * @param self 目标 K 线组指针。
 * @param open 开盘价。
 * @param high 最高价。
 * @param low 最低价。
 * @param close 收盘价。
 * @param timestamp 时间戳。
 * @return 无返回值。
 */
void XCandlestickSet_init_ex_5(XCandlestickSet* self, double open, double high,
                               double low, double close, double timestamp);

/**
 * @brief 堆上创建 K 线组（对标 QCandlestickSet(timestamp)）。
 * @param memory 内存类型。
 * @param timestamp 时间戳。
 * @return 新建对象；分配失败返回 NULL。
 */
XCandlestickSet* XCandlestickSet_create_ex(XMemoryType memory, double timestamp);
#define XCandlestickSet_create(timestamp) \
    XCandlestickSet_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, timestamp)

/* ==================== 属性（对标 QCandlestickSet） ==================== */

/** @brief 设置时间戳（对标 setTimestamp）。 @param self 目标 K 线组指针。 @param timestamp 时间戳。 @return 无返回值。 */
void XCandlestickSet_setTimestamp(XCandlestickSet* self, double timestamp);
/** @brief 查询时间戳（对标 timestamp）。 @param self 目标 K 线组指针。 @return 时间戳。 */
double XCandlestickSet_timestamp(const XCandlestickSet* self);
/** @brief 设置开盘价（对标 setOpen）。 @param self 目标 K 线组指针。 @param open 开盘价。 @return 无返回值。 */
void XCandlestickSet_setOpen(XCandlestickSet* self, double open);
/** @brief 查询开盘价（对标 open）。 @param self 目标 K 线组指针。 @return 开盘价。 */
double XCandlestickSet_open(const XCandlestickSet* self);
/** @brief 设置最高价（对标 setHigh）。 @param self 目标 K 线组指针。 @param high 最高价。 @return 无返回值。 */
void XCandlestickSet_setHigh(XCandlestickSet* self, double high);
/** @brief 查询最高价（对标 high）。 @param self 目标 K 线组指针。 @return 最高价。 */
double XCandlestickSet_high(const XCandlestickSet* self);
/** @brief 设置最低价（对标 setLow）。 @param self 目标 K 线组指针。 @param low 最低价。 @return 无返回值。 */
void XCandlestickSet_setLow(XCandlestickSet* self, double low);
/** @brief 查询最低价（对标 low）。 @param self 目标 K 线组指针。 @return 最低价。 */
double XCandlestickSet_low(const XCandlestickSet* self);
/** @brief 设置收盘价（对标 setClose）。 @param self 目标 K 线组指针。 @param close 收盘价。 @return 无返回值。 */
void XCandlestickSet_setClose(XCandlestickSet* self, double close);
/** @brief 查询收盘价（对标 close）。 @param self 目标 K 线组指针。 @return 收盘价。 */
double XCandlestickSet_close(const XCandlestickSet* self);

/* ==================== 画笔 / 画刷（对标 QCandlestickSet） ==================== */

/** @brief 设置画笔（颜色 + 线宽；对标 setPen）。 @param self 目标 K 线组指针。 @param color ARGB。 @param width 线宽。 @return 无返回值。 */
void XCandlestickSet_setPen(XCandlestickSet* self, uint32_t color, double width);
/** @brief 读取画笔（参数化；对标 pen）。 @param self 目标 K 线组指针。 @param color 输出颜色；可空。 @param width 输出线宽；可空。 @return 无返回值。 */
void XCandlestickSet_pen(const XCandlestickSet* self, uint32_t* color,
                         double* width);
/** @brief 设置画刷颜色（对标 setBrush）。 @param self 目标 K 线组指针。 @param color ARGB。 @return 无返回值。 */
void XCandlestickSet_setBrush(XCandlestickSet* self, uint32_t color);
/** @brief 读取画刷颜色（对标 brush）。 @param self 目标 K 线组指针。 @return ARGB。 */
uint32_t XCandlestickSet_brush(const XCandlestickSet* self);

/* ==================== 信号（对标 QCandlestickSet Q_SIGNALS） ==================== */

/** @brief clicked 信号（无载荷）。 */
void* XCandlestickSet_clicked_signal(XCandlestickSet* self);
/** @brief hovered 信号（载荷：状态）。 */
void* XCandlestickSet_hovered_signal(XCandlestickSet* self, bool status);
/** @brief pressed 信号（无载荷）。 */
void* XCandlestickSet_pressed_signal(XCandlestickSet* self);
/** @brief released 信号（无载荷）。 */
void* XCandlestickSet_released_signal(XCandlestickSet* self);
/** @brief doubleClicked 信号（无载荷）。 */
void* XCandlestickSet_doubleClicked_signal(XCandlestickSet* self);
/** @brief timestampChanged 信号（载荷：时间戳）。 */
void* XCandlestickSet_timestampChanged_signal(XCandlestickSet* self, double timestamp);
/** @brief openChanged 信号（载荷：开盘价）。 */
void* XCandlestickSet_openChanged_signal(XCandlestickSet* self, double open);
/** @brief highChanged 信号（载荷：最高价）。 */
void* XCandlestickSet_highChanged_signal(XCandlestickSet* self, double high);
/** @brief lowChanged 信号（载荷：最低价）。 */
void* XCandlestickSet_lowChanged_signal(XCandlestickSet* self, double low);
/** @brief closeChanged 信号（载荷：收盘价）。 */
void* XCandlestickSet_closeChanged_signal(XCandlestickSet* self, double close);
/** @brief brushChanged 信号（载荷：颜色）。 */
void* XCandlestickSet_brushChanged_signal(XCandlestickSet* self, uint32_t color);
/** @brief penChanged 信号（载荷：颜色, 线宽）。 */
void* XCandlestickSet_penChanged_signal(XCandlestickSet* self, uint32_t color,
                                        double width);

#endif /* XCHARTS_ON */
#ifdef __cplusplus
}
#endif

#endif /* XGUI_ON */

#endif /* XCANDLESTICKSET_H */
