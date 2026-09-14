#ifndef XABSTRACTBARSERIES_H
#define XABSTRACTBARSERIES_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XAbstractSeries.h"
#include "XString.h"

#if XCHARTS_ON

XCLASS_DEFINE_BEGING(XAbstractBarSeries)
XCLASS_DEFINE_EXTEND_END(XAbstractBarSeries, XAbstractSeries)

/**
 * @brief 柱状序列基类（对标 Qt Charts 6.8 QAbstractBarSeries）。
 *
 *        承载柱数据（值数组 + 类别标签）、组宽、标签显示等属性与
 *        集合操作信号；QBarSeries 等具体柱状序列继承本类。
 */
typedef struct XAbstractBarSeries
{
    XAbstractSeries m_base;      /**< 基类成员；必须是第一个。 */
    double* m_values;            /**< 柱值数组（堆）。 */
    XString** m_categories;      /**< 类别标签数组（对象拥有；堆）。 */
    int m_count;                 /**< 柱数。 */
    int m_capacity;              /**< 容量。 */
    double m_barWidth;           /**< 组宽比例 0-1（默认 0.8）。 */
    bool m_labelsVisible;        /**< 柱标签可见（默认 false）。 */
    XString* m_labelsFormat;     /**< 柱标签格式（对象拥有；默认 "@value"）。 */
    double m_labelsAngle;        /**< 柱标签角度（度，默认 0）。 */
    int m_labelsPosition;        /**< 柱标签位置（0 居中/1 外侧/2 内侧）。 */
    int m_labelsPrecision;       /**< 柱标签精度（小数位，默认 1）。 */
} XAbstractBarSeries;

XVtable* XAbstractBarSeries_class_init(void);

/**
 * @brief 初始化嵌入式柱状序列基类。
 *
 * @param self 目标序列指针，不能为空。
 * @return 无返回值。
 */
void XAbstractBarSeries_init(XAbstractBarSeries* self);

/**
 * @brief 堆上创建柱状序列基类（一般由派生类创建）。
 *
 * @param memory 内存类型。
 * @return 序列指针；分配失败返回 NULL。
 */
XAbstractBarSeries* XAbstractBarSeries_create_ex(XMemoryType memory);
#define XAbstractBarSeries_create() \
    XAbstractBarSeries_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

/** @brief 析构入口（查表分派父类析构）。 */
#define XAbstractBarSeries_deinit_base(self) \
    XClass_deinit_base((XClass*)(self))

/** @brief 删除堆上序列（查表分派析构并释放内存）。 */
#define XAbstractBarSeries_delete_base(self) \
    XClass_delete_base((XClass*)(self))

/* ==================== 数据操作（对标 QAbstractBarSeries 集合语义） ==================== */

/** @brief 追加一柱（label + value）。 @param self 目标序列指针。 @param label 类别标签。 @param value 柱值。 @return 下标；失败 -1。 */
int XAbstractBarSeries_append(XAbstractBarSeries* self, const char* label,
                              double value);
/** @brief 按下标移除一柱。 @param self 目标序列指针。 @param index 下标。 @return 移除成功返回 true。 */
bool XAbstractBarSeries_remove(XAbstractBarSeries* self, int index);
/** @brief 在指定位置插入一柱。 @param self 目标序列指针。 @param index 插入位置。 @param label 类别标签。 @param value 柱值。 @return 插入成功返回 true。 */
bool XAbstractBarSeries_insert(XAbstractBarSeries* self, int index,
                               const char* label, double value);
/** @brief 按下标取出并移除一柱（对标 take）。 @param self 目标序列指针。 @param index 下标。 @param labelOut 输出类别标签（调用方释放；可为 NULL）。 @return 柱值；越界返回 0。 */
double XAbstractBarSeries_take(XAbstractBarSeries* self, int index,
                               char** labelOut);
/** @brief 批量读取柱值（对标 barSets 简化：全部柱值）。 @param self 目标序列指针。 @param out 输出缓冲。 @param maxCount 容量。 @return 实际柱数。 */
int XAbstractBarSeries_barSets(XAbstractBarSeries* self, double* out,
                               int maxCount);
/** @brief 查询柱数。 @param self 目标序列指针。 @return 柱数。 */
int XAbstractBarSeries_count(const XAbstractBarSeries* self);
/** @brief 清空全部柱。 @param self 目标序列指针。 @return 无返回值。 */
void XAbstractBarSeries_clear(XAbstractBarSeries* self);

/* ==================== 数据读取 ==================== */

/** @brief 查询柱值。 @param self 目标序列指针。 @param index 下标。 @return 柱值；越界 0。 */
double XAbstractBarSeries_value(const XAbstractBarSeries* self, int index);
/** @brief 查询类别标签。 @param self 目标序列指针。 @param index 下标。 @return 标签；越界空串。 */
const char* XAbstractBarSeries_category(const XAbstractBarSeries* self,
                                        int index);

/* ==================== 外观（对标 QAbstractBarSeries） ==================== */

/** @brief 设置组宽比例。 @param self 目标序列指针。 @param width 0-1。 @return 无返回值。 */
void XAbstractBarSeries_setBarWidth(XAbstractBarSeries* self, double width);
/** @brief 查询组宽比例。 @param self 目标序列指针。 @return 组宽。 */
double XAbstractBarSeries_barWidth(const XAbstractBarSeries* self);
/** @brief 设置柱标签可见。 @param self 目标序列指针。 @param visible true 显示。 @return 无返回值。 */
void XAbstractBarSeries_setLabelsVisible(XAbstractBarSeries* self,
                                         bool visible);
/** @brief 查询柱标签可见。 @param self 目标序列指针。 @return 可见返回 true。 */
bool XAbstractBarSeries_isLabelsVisible(const XAbstractBarSeries* self);
/** @brief 设置柱标签格式。 @param self 目标序列指针。 @param format 格式串（UTF-8）。 @return 无返回值。 */
void XAbstractBarSeries_setLabelsFormat(XAbstractBarSeries* self,
                                        const char* format);
/** @brief 查询柱标签格式。 @param self 目标序列指针。 @return 格式串。 */
const char* XAbstractBarSeries_labelsFormat(const XAbstractBarSeries* self);
/** @brief 设置柱标签角度。 @param self 目标序列指针。 @param angle 角度（度）。 @return 无返回值。 */
void XAbstractBarSeries_setLabelsAngle(XAbstractBarSeries* self,
                                       double angle);
/** @brief 设置柱标签位置。 @param self 目标序列指针。 @param position 位置枚举（0 居中/1 外侧/2 内侧）。 @return 无返回值。 */
void XAbstractBarSeries_setLabelsPosition(XAbstractBarSeries* self,
                                          int position);
/** @brief 查询柱标签位置。 @param self 目标序列指针。 @return 位置枚举。 */
int XAbstractBarSeries_labelsPosition(const XAbstractBarSeries* self);
/** @brief 设置柱标签精度。 @param self 目标序列指针。 @param precision 小数位数。 @return 无返回值。 */
void XAbstractBarSeries_setLabelsPrecision(XAbstractBarSeries* self,
                                           int precision);
/** @brief 查询柱标签精度。 @param self 目标序列指针。 @return 小数位数。 */
int XAbstractBarSeries_labelsPrecision(const XAbstractBarSeries* self);
/** @brief 查询柱标签角度。 @param self 目标序列指针。 @return 角度（度）。 */
double XAbstractBarSeries_labelsAngle(const XAbstractBarSeries* self);

/* ==================== 信号（对标 QAbstractBarSeries Q_SIGNALS） ==================== */

/** @brief countChanged 信号地址（无载荷）。 */
void* XAbstractBarSeries_countChanged_signal(XAbstractBarSeries* self);
/** @brief labelsVisibleChanged 信号地址（无载荷）。 */
void* XAbstractBarSeries_labelsVisibleChanged_signal(XAbstractBarSeries* self);
/** @brief labelsFormatChanged 信号地址（载荷：格式串）。 */
void* XAbstractBarSeries_labelsFormatChanged_signal(XAbstractBarSeries* self,
                                                    const char* format);

#endif /* XCHARTS_ON */
#ifdef __cplusplus
}
#endif
#endif /* XABSTRACTBARSERIES_H */
