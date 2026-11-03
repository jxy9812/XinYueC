/**
 * @file       XLogValueAxis.h
 * @brief      XLogValueAxis 对数数值轴（对标 Qt Charts 6.8 QLogValueAxis）。
 * @details    以 m_base 组合继承 XAbstractAxis（可见性/网格/标签/标题等
 *             公共属性与信号在基类）；本类持有对数底数、标签格式与主/次
 *             刻度数。
 *
 *             Qt 中 QLogValueAxis 的 min/max 必须为正且非零、不能跨越 0；
 *             本实现按同一约束校验（拒绝非正值与 min > max）。
 * @note       模块总开关 XCHARTS_ON；实现只依赖 XinYueC 抽象层，
 *             禁止调用 Win32、POSIX、Qt 或其他平台 API。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XLOGVALUEAXIS_H
#define XLOGVALUEAXIS_H

#if XGUI_ON
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XAbstractAxis.h"

#if XCHARTS_ON

/** @brief 对数数值轴（m_base 必须是第一个成员；对标 QLogValueAxis : QAbstractAxis）。 */
typedef struct XLogValueAxis
{
    XAbstractAxis m_base;      /**< 基类成员；必须是第一个（可见/网格/标题等）。 */
    XString* m_labelFormat;    /**< 刻度标签格式（对象拥有；printf 风格，默认 "%g"）。 */
    double m_baseValue;        /**< 对数底数（默认 10；必须 >0 且 !=1）。 */
    int m_minorTickCount;      /**< 次刻度数（默认 0）。 */
    int m_tickCount;           /**< 主刻度数（只读派生；随范围/底数计算）。 */
} XLogValueAxis;

/** @brief 初始化类虚函数表。 @return 共享虚函数表指针。 */
XVtable* XLogValueAxis_class_init(void);
/** @brief 初始化对数数值轴。 @param self 目标轴指针。 @return 无返回值。 */
void XLogValueAxis_init(XLogValueAxis* self);
/** @brief 反初始化（释放格式串与基类资源）。 @param self 目标轴指针。 @return 无返回值。 */
void XLogValueAxis_deinit_impl(XLogValueAxis* self);
/** @brief 堆上创建对数数值轴。 @param memory 内存类型。 @return 新建对象；失败 NULL。 */
XLogValueAxis* XLogValueAxis_create_ex(XMemoryType memory);
#define XLogValueAxis_create() XLogValueAxis_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

/* ==================== 类型查询（对标 QLogValueAxis） ==================== */

/** @brief 查询轴类型（对标 QLogValueAxis::type）。 @param self 目标轴指针。 @return 固定返回 AxisTypeLogValue。 */
int XLogValueAxis_type(const XLogValueAxis* self);

/* ==================== 范围（对标 QLogValueAxis） ==================== */

/** @brief 设置下限（对标 setMin）。
 * @param self 目标轴指针。
 * @param min 下限（必须 >0 且 <= 当前上限，否则轴不变）。
 * @return 无返回值。
 */
void XLogValueAxis_setMin(XLogValueAxis* self, double min);
/** @brief 查询下限（对标 min）。 @param self 目标轴指针。 @return 下限。 */
double XLogValueAxis_min(const XLogValueAxis* self);
/** @brief 设置上限（对标 setMax）。
 * @param self 目标轴指针。
 * @param max 上限（必须 >0 且 >= 当前下限，否则轴不变）。
 * @return 无返回值。
 */
void XLogValueAxis_setMax(XLogValueAxis* self, double max);
/** @brief 查询上限（对标 max）。 @param self 目标轴指针。 @return 上限。 */
double XLogValueAxis_max(const XLogValueAxis* self);
/** @brief 设置范围（对标 setRange）。
 * @param self 目标轴指针。
 * @param min 下限（必须 >0）。
 * @param max 上限（必须 >= min）。
 * @return 无返回值；参数非法时轴不变。
 */
void XLogValueAxis_setRange(XLogValueAxis* self, double min, double max);

/* ==================== 标签格式（对标 QLogValueAxis） ==================== */

/** @brief 设置刻度标签格式（XString 主版本；对标 setLabelFormat）。
 * @param self 目标轴指针。
 * @param format 借用 XString*；NULL 恢复默认 "%g"。
 * @return 无返回值。
 */
void XLogValueAxis_setLabelFormat(XLogValueAxis* self, const XString* format);
/** @brief 设置刻度标签格式（UTF-8 编码版本）。
 * @param self 目标轴指针。
 * @param format UTF-8 printf 风格格式串（借用）。
 * @return 无返回值。
 */
void XLogValueAxis_setLabelFormat_utf8(XLogValueAxis* self, const char* format);
/** @brief 设置刻度标签格式（UTF-8 兼容别名，转发 _utf8 版本）。
 * @param self 目标轴指针。
 * @param format UTF-8 格式串（借用）。
 * @return 无返回值。
 */
void XLogValueAxis_setLabelFormat_2(XLogValueAxis* self, const char* format);
/** @brief 查询刻度标签格式（借用；对标 labelFormat）。
 * @param self 目标轴指针。
 * @return 借用 XString*；调用者不得释放。
 */
const XString* XLogValueAxis_labelFormat_const(const XLogValueAxis* self);
/** @brief 查询刻度标签格式（兼容别名，转发 _const 借用版）。
 * @param self 目标轴指针。
 * @return 借用 XString*；调用者不得释放。
 */
const XString* XLogValueAxis_labelFormat(const XLogValueAxis* self);
/** @brief 查询刻度标签格式（UTF-8 借用视图）。
 * @param self 目标轴指针。
 * @return 借用 UTF-8 串；调用者不得释放。
 */
const char* XLogValueAxis_labelFormat_toUtf8(const XLogValueAxis* self);

/* ==================== 底数（对标 QLogValueAxis） ==================== */

/** @brief 设置对数底数（对标 setBase）。
 * @param self 目标轴指针。
 * @param base 底数（必须 >0 且 !=1，否则轴不变）。
 * @return 无返回值。
 */
void XLogValueAxis_setBase(XLogValueAxis* self, double base);
/** @brief 查询对数底数（对标 base）。 @param self 目标轴指针。 @return 底数。 */
double XLogValueAxis_base(const XLogValueAxis* self);

/* ==================== 刻度（对标 QLogValueAxis） ==================== */

/** @brief 查询主刻度数（对标 tickCount；只读派生值）。
 * @details Qt 中 QLogValueAxis::tickCount 为只读，由范围与底数推导；
 *          本实现按 log_base(range)+1 计算。
 * @param self 目标轴指针。
 * @return 主刻度数。
 */
int XLogValueAxis_tickCount(const XLogValueAxis* self);
/** @brief 设置次刻度数（对标 setMinorTickCount）。
 * @param self 目标轴指针。
 * @param minorTickCount 次刻度数（<0 时按 0 钳位）。
 * @return 无返回值。
 */
void XLogValueAxis_setMinorTickCount(XLogValueAxis* self, int minorTickCount);
/** @brief 查询次刻度数（对标 minorTickCount）。 @param self 目标轴指针。 @return 次刻度数。 */
int XLogValueAxis_minorTickCount(const XLogValueAxis* self);

/* ==================== 信号（对标 QLogValueAxis Q_SIGNALS） ==================== */

/** @brief minChanged 信号（载荷：下限）。 */
void* XLogValueAxis_minChanged_signal(XLogValueAxis* self, double min);
/** @brief maxChanged 信号（载荷：上限）。 */
void* XLogValueAxis_maxChanged_signal(XLogValueAxis* self, double max);
/** @brief rangeChanged 信号（载荷：下限, 上限）。 */
void* XLogValueAxis_rangeChanged_signal(XLogValueAxis* self, double min, double max);
/** @brief labelFormatChanged 信号（载荷：格式串 UTF-8）。 */
void* XLogValueAxis_labelFormatChanged_signal(XLogValueAxis* self, const char* format);
/** @brief baseChanged 信号（载荷：底数）。 */
void* XLogValueAxis_baseChanged_signal(XLogValueAxis* self, double base);
/** @brief tickCountChanged 信号（载荷：主刻度数）。 */
void* XLogValueAxis_tickCountChanged_signal(XLogValueAxis* self, int tickCount);
/** @brief minorTickCountChanged 信号（载荷：次刻度数）。 */
void* XLogValueAxis_minorTickCountChanged_signal(XLogValueAxis* self, int minorTickCount);

#endif /* XCHARTS_ON */
#ifdef __cplusplus
}
#endif

#endif /* XGUI_ON */

#endif /* XLOGVALUEAXIS_H */
