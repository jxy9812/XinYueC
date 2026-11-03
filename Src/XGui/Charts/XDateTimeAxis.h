/**
 * @file       XDateTimeAxis.h
 * @brief      XDateTimeAxis 日期时间轴（对标 Qt Charts 6.8 QDateTimeAxis）。
 * @details    以 m_base 组合继承 XAbstractAxis（可见性/网格/标签/标题等
 *             公共属性与信号在基类）；本类持有日期时间范围（以毫秒纪元
 *             整数存储，避免双精度丢精度）、刻度数与格式化模板。
 *
 *             Qt 用 QDateTime 作为 min/max 的类型；本库以 C 结构体
 *             `XDateTime`（按值）对外，内部统一转成 int64 毫秒纪元。
 *             对应关系：setMin(QDateTime) → setMin(XDateTime)；
 *             format() 用 Qt 的日期格式串语义（"hh:mm:ss" 等）。
 * @note       模块总开关 XCHARTS_ON；实现只依赖 XinYueC 抽象层，
 *             禁止调用 Win32、POSIX、Qt 或其他平台 API。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XDATETIMEAXIS_H
#define XDATETIMEAXIS_H

#if XGUI_ON
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XAbstractAxis.h"
#include "XDateTime.h"

#if XCHARTS_ON

/** @brief 日期时间轴（m_base 必须是第一个成员；对标 QDateTimeAxis : QAbstractAxis）。 */
typedef struct XDateTimeAxis
{
    XAbstractAxis m_base;      /**< 基类成员；必须是第一个（可见/网格/标题等）。 */
    XString* m_format;         /**< 刻度标签格式（对象拥有；Qt 日期格式语义）。 */
    int64_t m_minMSecs;        /**< 范围下限（毫秒纪元）。 */
    int64_t m_maxMSecs;        /**< 范围上限（毫秒纪元）。 */
    int m_tickCount;           /**< 刻度数（含端点，默认 5，对标 Qt 默认）。 */
} XDateTimeAxis;

/** @brief 初始化类虚函数表。 @return 共享虚函数表指针。 */
XVtable* XDateTimeAxis_class_init(void);
/** @brief 初始化日期时间轴。 @param self 目标轴指针。 @return 无返回值。 */
void XDateTimeAxis_init(XDateTimeAxis* self);
/** @brief 反初始化（释放格式串与基类资源）。 @param self 目标轴指针。 @return 无返回值。 */
void XDateTimeAxis_deinit_impl(XDateTimeAxis* self);
/** @brief 堆上创建日期时间轴。 @param memory 内存类型。 @return 新建对象；失败 NULL。 */
XDateTimeAxis* XDateTimeAxis_create_ex(XMemoryType memory);
#define XDateTimeAxis_create() XDateTimeAxis_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

/* ==================== 类型查询（对标 QDateTimeAxis） ==================== */

/** @brief 查询轴类型（对标 QDateTimeAxis::type）。 @param self 目标轴指针。 @return 固定返回 AxisTypeDateTime。 */
int XDateTimeAxis_type(const XDateTimeAxis* self);

/* ==================== 范围（对标 QDateTimeAxis） ==================== */

/** @brief 设置下限（对标 setMin(QDateTime)）。
 * @param self 目标轴指针。
 * @param min 下限（按值传入；内含毫秒纪元）。
 * @return 无返回值；min 晚于当前上限时轴不变。
 */
void XDateTimeAxis_setMin(XDateTimeAxis* self, XDateTime min);
/** @brief 查询下限（对标 min）。
 * @param self 目标轴指针。
 * @return 下限日期时间（按值）；未设置返回无效 XDateTime。
 */
XDateTime XDateTimeAxis_min(const XDateTimeAxis* self);
/** @brief 设置上限（对标 setMax(QDateTime)）。
 * @param self 目标轴指针。
 * @param max 上限（按值传入）。
 * @return 无返回值；max 早于当前下限时轴不变。
 */
void XDateTimeAxis_setMax(XDateTimeAxis* self, XDateTime max);
/** @brief 查询上限（对标 max）。
 * @param self 目标轴指针。
 * @return 上限日期时间（按值）。
 */
XDateTime XDateTimeAxis_max(const XDateTimeAxis* self);
/** @brief 设置范围（对标 setRange(QDateTime,QDateTime)）。
 * @param self 目标轴指针。
 * @param min 下限。
 * @param max 上限（须不早于 min）。
 * @return 无返回值；min > max 时轴不变。
 */
void XDateTimeAxis_setRange(XDateTimeAxis* self, XDateTime min, XDateTime max);
/** @brief 设置范围（毫秒纪元直接形态，避免 XDateTime 往返转换）。
 * @param self 目标轴指针。
 * @param minMSecs 下限（毫秒纪元）。
 * @param maxMSecs 上限（毫秒纪元）。
 * @return 无返回值；minMSecs > maxMSecs 时轴不变。
 */
void XDateTimeAxis_setRangeMSecs(XDateTimeAxis* self, int64_t minMSecs,
                                 int64_t maxMSecs);
/** @brief 查询下限（毫秒纪元）。
 * @param self 目标轴指针。
 * @return 下限毫秒纪元。
 */
int64_t XDateTimeAxis_minMSecs(const XDateTimeAxis* self);
/** @brief 查询上限（毫秒纪元）。
 * @param self 目标轴指针。
 * @return 上限毫秒纪元。
 */
int64_t XDateTimeAxis_maxMSecs(const XDateTimeAxis* self);

/* ==================== 标签格式（对标 QDateTimeAxis） ==================== */

/** @brief 设置刻度标签格式（XString 主版本；对标 setFormat）。
 * @param self 目标轴指针。
 * @param format 借用 XString*；NULL 恢复默认 "dd-MM-yyyy"。
 * @return 无返回值。
 */
void XDateTimeAxis_setFormat(XDateTimeAxis* self, const XString* format);
/** @brief 设置刻度标签格式（UTF-8 编码版本）。
 * @param self 目标轴指针。
 * @param format UTF-8 格式串（借用，不转移所有权）。
 * @return 无返回值。
 */
void XDateTimeAxis_setFormat_utf8(XDateTimeAxis* self, const char* format);
/** @brief 设置刻度标签格式（UTF-8 兼容别名，转发 _utf8 版本）。
 * @param self 目标轴指针。
 * @param format UTF-8 格式串（借用）。
 * @return 无返回值。
 */
void XDateTimeAxis_setFormat_2(XDateTimeAxis* self, const char* format);
/** @brief 查询刻度标签格式（借用；对标 format）。
 * @param self 目标轴指针。
 * @return 借用 XString*；归对象所有，调用者不得释放。
 */
const XString* XDateTimeAxis_format_const(const XDateTimeAxis* self);
/** @brief 查询刻度标签格式（兼容别名，转发 _const 借用版）。
 * @param self 目标轴指针。
 * @return 借用 XString*；调用者不得释放。
 */
const XString* XDateTimeAxis_format(const XDateTimeAxis* self);
/** @brief 查询刻度标签格式（UTF-8 借用视图）。
 * @param self 目标轴指针。
 * @return 借用 UTF-8 串；调用者不得释放。
 */
const char* XDateTimeAxis_format_toUtf8(const XDateTimeAxis* self);

/* ==================== 刻度数（对标 QDateTimeAxis） ==================== */

/** @brief 设置刻度数（对标 setTickCount）。 @param self 目标轴指针。 @param count 刻度数（<2 时按 2 钳位）。 @return 无返回值。 */
void XDateTimeAxis_setTickCount(XDateTimeAxis* self, int count);
/** @brief 查询刻度数（对标 tickCount）。 @param self 目标轴指针。 @return 刻度数。 */
int XDateTimeAxis_tickCount(const XDateTimeAxis* self);

/* ==================== 信号（对标 QDateTimeAxis Q_SIGNALS） ==================== */

/** @brief minChanged 信号（载荷：下限毫秒纪元；对标 minChanged(QDateTime)）。 */
void* XDateTimeAxis_minChanged_signal(XDateTimeAxis* self, int64_t minMSecs);
/** @brief maxChanged 信号（载荷：上限毫秒纪元）。 */
void* XDateTimeAxis_maxChanged_signal(XDateTimeAxis* self, int64_t maxMSecs);
/** @brief rangeChanged 信号（载荷：下限, 上限毫秒纪元）。 */
void* XDateTimeAxis_rangeChanged_signal(XDateTimeAxis* self, int64_t minMSecs,
                                       int64_t maxMSecs);
/** @brief formatChanged 信号（载荷：格式串 UTF-8）。 */
void* XDateTimeAxis_formatChanged_signal(XDateTimeAxis* self, const char* format);
/** @brief tickCountChanged 信号（载荷：刻度数）。 */
void* XDateTimeAxis_tickCountChanged_signal(XDateTimeAxis* self, int tickCount);

#endif /* XCHARTS_ON */
#ifdef __cplusplus
}
#endif

#endif /* XGUI_ON */

#endif /* XDATETIMEAXIS_H */
