/**
 * @file       XValueAxis.h
 * @brief      XValueAxis 数值轴（对标 Qt Charts 6.8 QValueAxis）。
 * @details    以 m_base 组合继承 XAbstractAxis（可见性/网格/标签/标题/
 *             范围等公共属性与信号在基类）；本类持有 tickCount/
 *             minorTickCount/tickAnchor/tickInterval/tickType/labelFormat
 *             专用状态。渲染由 XChart 完成。
 * @note       模块总开关 XCHARTS_ON；实现只依赖 XinYueC 抽象层，
 *             禁止调用 Win32、POSIX、Qt 或其他平台 API。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XVALUEAXIS_H
#define XVALUEAXIS_H

#if XGUI_ON
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XAbstractAxis.h"

#if XCHARTS_ON

/**
 * @brief      刻度类型（对标 Qt Charts 6.8 QValueAxis::TickType，数值一致）。
 */
typedef enum XValueAxis_TickType
{
    XValueAxis_TickType_Dynamic = 0, /**< 动态刻度（对标 TicksDynamic）。 */
    XValueAxis_TickType_Fixed = 1    /**< 固定刻度（对标 TicksFixed）。 */
} XValueAxis_TickType;

XCLASS_DEFINE_BEGING(XValueAxis)
XCLASS_DEFINE_EXTEND_END(XValueAxis, XAbstractAxis)

/** @brief 数值轴（m_base 必须是第一个成员；对标 QValueAxis : QAbstractAxis）。 */
typedef struct XValueAxis
{
    XAbstractAxis m_base;     /**< 基类成员；必须是第一个（范围/可见/标题等）。 */
    XString* m_labelFormat;   /**< 刻度标签格式（对象拥有；printf 风格）。 */
    double m_tickAnchor;      /**< 动态刻度锚点（对标 tickAnchor；Qt 5.12+）。 */
    double m_tickInterval;    /**< 动态刻度间隔（对标 tickInterval；Qt 5.12+）。 */
    int m_tickCount;          /**< 刻度数（含端点，默认 6）。 */
    int m_minorTickCount;     /**< 次刻度数（默认 0）。 */
    int m_tickType;           /**< XValueAxis_TickType（默认 Fixed）。 */
} XValueAxis;

/**
 * @brief 初始化嵌入式数值轴（对标 QValueAxis 构造）。
 *
 * @param self 目标轴指针，不能为空。
 * @return 无返回值。
 */
void XValueAxis_init(XValueAxis* self);
/** @brief 反初始化数值轴（释放标签格式与基类资源；结构体内存由调用方按其分配方式释放）。 @param self 目标轴指针。 @return 无返回值。 */
void XValueAxis_deinit_impl(XValueAxis* self);
/** @brief 初始化类虚函数表。 @return 共享虚函数表指针。 */
XVtable* XValueAxis_class_init(void);
/** @brief 堆上创建数值轴。 @param memory 内存类型。 @return 新建对象；失败 NULL。 */
XValueAxis* XValueAxis_create_ex(XMemoryType memory);
#define XValueAxis_create() XValueAxis_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

/* ==================== 类型查询（对标 QValueAxis） ==================== */

/** @brief 查询轴类型（对标 QValueAxis::type）。 @param self 目标轴指针。 @return 固定返回 AxisTypeValue。 */
int XValueAxis_type(const XValueAxis* self);

/* ==================== 范围（对标 QValueAxis） ==================== */

/** @brief 设置轴范围（对标 setRange）。 @param self 目标轴指针。 @param min 下限。 @param max 上限。 @return 无返回值。 */
void XValueAxis_setRange(XValueAxis* self, double min, double max);
/** @brief 设置轴最小值（对标 setMin）。 @param self 目标轴指针。 @param min 下限。 @return 无返回值。 */
void XValueAxis_setMin(XValueAxis* self, double min);
/** @brief 设置轴最大值（对标 setMax）。 @param self 目标轴指针。 @param max 上限。 @return 无返回值。 */
void XValueAxis_setMax(XValueAxis* self, double max);
/** @brief 查询轴最小值（对标 min）。 @param self 目标轴指针。 @return 最小值。 */
double XValueAxis_min(const XValueAxis* self);
/** @brief 查询轴最大值（对标 max）。 @param self 目标轴指针。 @return 最大值。 */
double XValueAxis_max(const XValueAxis* self);

/* ==================== 刻度（对标 QValueAxis） ==================== */

/** @brief 设置刻度数（对标 setTickCount）。 @param self 目标轴指针。 @param count 刻度数（≥2）。 @return 无返回值。 */
void XValueAxis_setTickCount(XValueAxis* self, int count);
/** @brief 查询刻度数（对标 tickCount）。 @param self 目标轴指针。 @return 刻度数。 */
int XValueAxis_tickCount(const XValueAxis* self);
/** @brief 设置次刻度数（对标 setMinorTickCount）。 @param self 目标轴指针。 @param count 次刻度数（≥0）。 @return 无返回值。 */
void XValueAxis_setMinorTickCount(XValueAxis* self, int count);
/** @brief 查询次刻度数（对标 minorTickCount）。 @param self 目标轴指针。 @return 次刻度数。 */
int XValueAxis_minorTickCount(const XValueAxis* self);
/** @brief 设置动态刻度锚点（对标 setTickAnchor；Qt 5.12+）。 @param self 目标轴指针。 @param anchor 锚点值。 @return 无返回值。 */
void XValueAxis_setTickAnchor(XValueAxis* self, double anchor);
/** @brief 查询动态刻度锚点（对标 tickAnchor）。 @param self 目标轴指针。 @return 锚点值。 */
double XValueAxis_tickAnchor(const XValueAxis* self);
/** @brief 设置动态刻度间隔（对标 setTickInterval；Qt 5.12+）。 @param self 目标轴指针。 @param interval 间隔（>0）。 @return 无返回值。 */
void XValueAxis_setTickInterval(XValueAxis* self, double interval);
/** @brief 查询动态刻度间隔（对标 tickInterval）。 @param self 目标轴指针。 @return 间隔。 */
double XValueAxis_tickInterval(const XValueAxis* self);
/** @brief 设置刻度类型（对标 setTickType；Qt 5.12+）。 @param self 目标轴指针。 @param type 刻度类型。 @return 无返回值。 */
void XValueAxis_setTickType(XValueAxis* self, XValueAxis_TickType type);
/** @brief 查询刻度类型（对标 tickType）。 @param self 目标轴指针。 @return 刻度类型。 */
XValueAxis_TickType XValueAxis_tickType(const XValueAxis* self);

/* ==================== 标签格式（对标 QValueAxis） ==================== */

/** @brief 设置刻度标签格式（XString 主版本；对标 setLabelFormat）。
 * @param self 目标轴指针。
 * @param fmt 借用 XString*；可为 NULL（恢复默认 "%g"）。
 * @return 无返回值。 */
void XValueAxis_setLabelFormat(XValueAxis* self, const XString* fmt);
/** @brief 设置刻度标签格式（UTF-8 编码版本；对标 setLabelFormat）。
 * @param self 目标轴指针。
 * @param fmt UTF-8 printf 风格格式串（借用）；NULL 恢复默认 "%g"。
 * @return 无返回值（变化时发射 labelFormatChanged）。 */
void XValueAxis_setLabelFormat_utf8(XValueAxis* self, const char* fmt);
/** @brief 设置刻度标签格式（UTF-8 兼容别名，转发 _utf8 版本）。
 * @param self 目标轴指针。
 * @param fmt UTF-8 格式串（借用）。
 * @return 无返回值。 */
void XValueAxis_setLabelFormat_2(XValueAxis* self, const char* fmt);
/** @brief 查询刻度标签格式（借用；对标 labelFormat）。
 * @param self 目标轴指针。
 * @return 借用 XString*；调用者不得释放。 */
const XString* XValueAxis_labelFormat(const XValueAxis* self);
/** @brief 查询刻度标签格式（UTF-8 借用视图）。
 * @param self 目标轴指针。
 * @return 借用 UTF-8 串；调用者不得释放。 */
const char* XValueAxis_labelFormat_2(const XValueAxis* self);

/* ==================== 自动刻度（对标 QValueAxis::applyNiceNumbers） ==================== */

/**
 * @brief 按「好看的数字」自动调整范围与刻度数（对标 applyNiceNumbers）。
 * @details 把 [min,max] 扩到 1/2/5×10^n 的整数倍刻度，tickCount 落在
 *          常用档位；与 Qt 同口径（QValueAxis::applyNiceNumbers）。
 * @param self 目标轴指针。
 * @return 无返回值。
 */
void XValueAxis_applyNiceNumbers(XValueAxis* self);

/* ==================== 信号（对标 QValueAxis Q_SIGNALS） ==================== */

/** @brief minChanged 信号（载荷：min）。 */
void* XValueAxis_minChanged_signal(XValueAxis* self, double min);
/** @brief maxChanged 信号（载荷：max）。 */
void* XValueAxis_maxChanged_signal(XValueAxis* self, double max);
/** @brief rangeChanged 信号（载荷：min, max）。 */
void* XValueAxis_rangeChanged_signal(XValueAxis* self, double min, double max);
/** @brief tickCountChanged 信号（载荷：tickCount）。 */
void* XValueAxis_tickCountChanged_signal(XValueAxis* self, int count);
/** @brief minorTickCountChanged 信号（载荷：minorTickCount）。 */
void* XValueAxis_minorTickCountChanged_signal(XValueAxis* self, int count);
/** @brief labelFormatChanged 信号（载荷：格式串）。 */
void* XValueAxis_labelFormatChanged_signal(XValueAxis* self, const char* format);
/** @brief tickIntervalChanged 信号（载荷：interval；Qt 5.12+）。 */
void* XValueAxis_tickIntervalChanged_signal(XValueAxis* self, double interval);
/** @brief tickAnchorChanged 信号（载荷：anchor；Qt 5.12+）。 */
void* XValueAxis_tickAnchorChanged_signal(XValueAxis* self, double anchor);
/** @brief tickTypeChanged 信号（载荷：tickType；Qt 5.12+）。 */
void* XValueAxis_tickTypeChanged_signal(XValueAxis* self, int type);

#endif /* XCHARTS_ON */
#ifdef __cplusplus
}
#endif

#endif /* XGUI_ON */

#endif /* XVALUEAXIS_H */
