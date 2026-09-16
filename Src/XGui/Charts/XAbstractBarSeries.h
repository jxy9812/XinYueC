/**
 * @file       XAbstractBarSeries.h
 * @brief      XAbstractBarSeries 柱状序列基类（对标 Qt Charts 6.8
 *             QAbstractBarSeries）。
 * @details    一序列 = 多个 XBarSet（每组一值列），多组并列成组显示；
 *             m_barSets 为拥有型集合；渲染统一走 barSets/barSetAt。
 * @note       模块总开关 XCHARTS_ON。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XABSTRACTBARSERIES_H
#define XABSTRACTBARSERIES_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XAbstractSeries.h"
#include "XBarSet.h"

#if XCHARTS_ON

/**
 * @brief      柱标签位置（对标 Qt Charts 6.8 QAbstractBarSeries::
 *             LabelsPosition，数值一致）。
 */
typedef enum XAbstractBarSeries_LabelsPosition
{
    XAbstractBarSeries_LabelsCenter = 0,      /**< 柱中央（对标 LabelsCenter）。 */
    XAbstractBarSeries_LabelsInsideEnd = 1,   /**< 柱内顶部（对标 LabelsInsideEnd）。 */
    XAbstractBarSeries_LabelsInsideBase = 2,  /**< 柱内底部（对标 LabelsInsideBase）。 */
    XAbstractBarSeries_LabelsOutsideEnd = 3   /**< 柱外顶部（对标 LabelsOutsideEnd）。 */
} XAbstractBarSeries_LabelsPosition;

XCLASS_DEFINE_BEGING(XAbstractBarSeries)
XCLASS_DEFINE_EXTEND_END(XAbstractBarSeries, XAbstractSeries)

/**
 * @brief 柱状序列基类（对标 QAbstractBarSeries）。
 *
 *        承载 XBarSet 集合（对象拥有）、组宽与柱标签显示属性及集合
 *        操作信号；XBarSeries 等具体柱状序列继承本类。
 */
typedef struct XAbstractBarSeries
{
    XAbstractSeries m_base;       /**< 基类成员；必须是第一个。 */
    XBarSet** m_barSets;          /**< 柱组指针数组（堆；对象拥有）。 */
    int m_barSetCount;            /**< 柱组数（对标 QAbstractBarSeries::count）。 */
    int m_barSetCapacity;         /**< 柱组容量。 */
    double m_barWidth;            /**< 组宽比例（默认 0.5，对标 Qt 6.8）。 */
    bool m_labelsVisible;         /**< 柱标签可见（默认 false）。 */
    XString* m_labelsFormat;      /**< 柱标签格式（对象拥有；默认空=渲染用 "@value"）。 */
    double m_labelsAngle;         /**< 柱标签角度（度，默认 0）。 */
    XAbstractBarSeries_LabelsPosition m_labelsPosition; /**< 标签位置（默认居中）。 */
    int m_labelsPrecision;        /**< 标签有效位数（默认 6，对标 Qt 6.8）。 */
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
#define XAbstractBarSeries_create()     XAbstractBarSeries_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

/** @brief 析构入口（查表分派父类析构）。 */
#define XAbstractBarSeries_deinit_base(self)     XClass_deinit_base((XClass*)(self))

/** @brief 删除堆上序列（查表分派析构并释放内存）。 */
#define XAbstractBarSeries_delete_base(self)     XClass_delete_base((XClass*)(self))

/* ==================== 柱组集合（对标 QAbstractBarSeries） ==================== */

/**
 * @brief 追加一个柱组并接管所有权（对标 append(QBarSet*)）。
 *
 * @param self 目标序列指针。
 * @param set  柱组指针；NULL 或已在序列中返回 false。
 * @return 追加成功返回 true。
 */
bool XAbstractBarSeries_append(XAbstractBarSeries* self, XBarSet* set);

/**
 * @brief 批量追加柱组并接管所有权（对标 append(QList<QBarSet*>)）。
 *
 * @param self  目标序列指针。
 * @param sets  柱组指针数组。
 * @param count 数量（须 >=1）。
 * @return 全部追加成功返回 true；任一为 NULL、重复或已存在则整体失败。
 */
bool XAbstractBarSeries_appendSets(XAbstractBarSeries* self,
                                   XBarSet* const* sets, int count);

/**
 * @brief 在指定位置插入柱组并接管所有权（对标 insert(int, QBarSet*)）。
 *
 * @param self  目标序列指针。
 * @param index 插入位置（0..count）。
 * @param set   柱组指针；NULL 或已存在返回 false。
 * @return 插入成功返回 true。
 */
bool XAbstractBarSeries_insert(XAbstractBarSeries* self, int index,
                               XBarSet* set);

/**
 * @brief 移除并释放柱组（对标 remove(QBarSet*)）。
 *
 * @param self 目标序列指针。
 * @param set  柱组指针。
 * @return 移除成功返回 true。
 */
bool XAbstractBarSeries_remove(XAbstractBarSeries* self, XBarSet* set);

/**
 * @brief 摘除柱组但保留所有权（对标 take(QBarSet*)；调用方负责释放）。
 *
 * @param self 目标序列指针。
 * @param set  柱组指针。
 * @return 摘除成功返回 true。
 */
bool XAbstractBarSeries_take(XAbstractBarSeries* self, XBarSet* set);

/**
 * @brief 清空并释放全部柱组（对标 clear()）。
 *
 * @param self 目标序列指针。
 * @return 无返回值。
 */
void XAbstractBarSeries_clear(XAbstractBarSeries* self);

/**
 * @brief 查询柱组数（对标 count()）。
 *
 * @param self 目标序列指针。
 * @return 柱组数。
 */
int XAbstractBarSeries_count(const XAbstractBarSeries* self);

/**
 * @brief 批量读取柱组指针（对标 barSets()；所有权仍在序列）。
 *
 * @param self     目标序列指针。
 * @param out      输出缓冲（至少 maxCount 项）。
 * @param maxCount 缓冲容量。
 * @return 实际柱组数。
 */
int XAbstractBarSeries_barSets(const XAbstractBarSeries* self,
                               XBarSet** out, int maxCount);

/**
 * @brief 按下标读取柱组（对标 QML at()）。
 *
 * @param self  目标序列指针。
 * @param index 下标。
 * @return 柱组指针；越界返回 NULL。
 */
XBarSet* XAbstractBarSeries_barSetAt(const XAbstractBarSeries* self,
                                     int index);

/* ==================== 外观（对标 QAbstractBarSeries） ==================== */

/**
 * @brief 设置组宽比例（对标 setBarWidth；负值按 0 处理）。
 *
 * @param self  目标序列指针。
 * @param width 组宽比例（>=0；0=一像素）。
 * @return 无返回值。
 */
void XAbstractBarSeries_setBarWidth(XAbstractBarSeries* self, double width);
/** @brief 查询组宽比例。 @param self 目标序列指针。 @return 组宽。 */
double XAbstractBarSeries_barWidth(const XAbstractBarSeries* self);
/** @brief 设置柱标签可见。 @param self 目标序列指针。 @param visible true 显示。 @return 无返回值。 */
void XAbstractBarSeries_setLabelsVisible(XAbstractBarSeries* self,
                                         bool visible);
/** @brief 查询柱标签可见。 @param self 目标序列指针。 @return 可见返回 true。 */
bool XAbstractBarSeries_isLabelsVisible(const XAbstractBarSeries* self);
/** @brief 设置柱标签格式（XString 主版本；空=渲染用 "@value"）。 @param self 目标序列指针。 @param format 格式串。 @return 无返回值。 */
void XAbstractBarSeries_setLabelsFormat(XAbstractBarSeries* self,
                                        const XString* format);
void XAbstractBarSeries_setLabelsFormat_2(XAbstractBarSeries* self,
                                          const char* format);
const XString* XAbstractBarSeries_labelsFormat(const XAbstractBarSeries* self);
const char* XAbstractBarSeries_labelsFormat_2(const XAbstractBarSeries* self);
/** @brief 设置柱标签角度。 @param self 目标序列指针。 @param angle 角度（度）。 @return 无返回值。 */
void XAbstractBarSeries_setLabelsAngle(XAbstractBarSeries* self,
                                       double angle);
/** @brief 查询柱标签角度。 @param self 目标序列指针。 @return 角度（度）。 */
double XAbstractBarSeries_labelsAngle(const XAbstractBarSeries* self);
/** @brief 设置柱标签位置。 @param self 目标序列指针。 @param position 位置枚举。 @return 无返回值。 */
void XAbstractBarSeries_setLabelsPosition(
    XAbstractBarSeries* self, XAbstractBarSeries_LabelsPosition position);
/** @brief 查询柱标签位置。 @param self 目标序列指针。 @return 位置枚举。 */
XAbstractBarSeries_LabelsPosition XAbstractBarSeries_labelsPosition(
    const XAbstractBarSeries* self);
/** @brief 设置柱标签精度。 @param self 目标序列指针。 @param precision 有效位数。 @return 无返回值。 */
void XAbstractBarSeries_setLabelsPrecision(XAbstractBarSeries* self,
                                           int precision);
/** @brief 查询柱标签精度。 @param self 目标序列指针。 @return 有效位数。 */
int XAbstractBarSeries_labelsPrecision(const XAbstractBarSeries* self);

/* ==================== 信号（对标 QAbstractBarSeries Q_SIGNALS） ==================== */

/** @brief clicked 信号地址（载荷：下标, 柱组）。 */
void* XAbstractBarSeries_clicked_signal(XAbstractBarSeries* self, int index,
                                        XBarSet* set);
/** @brief hovered 信号地址（载荷：状态, 下标, 柱组）。 */
void* XAbstractBarSeries_hovered_signal(XAbstractBarSeries* self,
                                        bool status, int index, XBarSet* set);
/** @brief pressed 信号地址（载荷：下标, 柱组）。 */
void* XAbstractBarSeries_pressed_signal(XAbstractBarSeries* self, int index,
                                        XBarSet* set);
/** @brief released 信号地址（载荷：下标, 柱组）。 */
void* XAbstractBarSeries_released_signal(XAbstractBarSeries* self, int index,
                                         XBarSet* set);
/** @brief doubleClicked 信号地址（载荷：下标, 柱组）。 */
void* XAbstractBarSeries_doubleClicked_signal(XAbstractBarSeries* self,
                                              int index, XBarSet* set);
/** @brief countChanged 信号地址（无载荷）。 */
void* XAbstractBarSeries_countChanged_signal(XAbstractBarSeries* self);
/** @brief labelsVisibleChanged 信号地址（无载荷）。 */
void* XAbstractBarSeries_labelsVisibleChanged_signal(XAbstractBarSeries* self);
/** @brief labelsFormatChanged 信号地址（载荷：格式串）。 */
void* XAbstractBarSeries_labelsFormatChanged_signal(XAbstractBarSeries* self,
                                                    const char* format);
/** @brief labelsPositionChanged 信号地址（载荷：位置枚举）。 */
void* XAbstractBarSeries_labelsPositionChanged_signal(XAbstractBarSeries* self,
                                                      int position);
/** @brief labelsAngleChanged 信号地址（载荷：角度）。 */
void* XAbstractBarSeries_labelsAngleChanged_signal(XAbstractBarSeries* self,
                                                   double angle);
/** @brief labelsPrecisionChanged 信号地址（载荷：精度）。 */
void* XAbstractBarSeries_labelsPrecisionChanged_signal(
    XAbstractBarSeries* self, int precision);
/** @brief barsetsAdded 信号地址（载荷：柱组数组, 数量）。 */
void* XAbstractBarSeries_barsetsAdded_signal(XAbstractBarSeries* self,
                                             XBarSet* const* sets, int count);
/** @brief barsetsRemoved 信号地址（载荷：柱组数组, 数量）。 */
void* XAbstractBarSeries_barsetsRemoved_signal(XAbstractBarSeries* self,
                                               XBarSet* const* sets,
                                               int count);

#endif /* XCHARTS_ON */
#ifdef __cplusplus
}
#endif
#endif /* XABSTRACTBARSERIES_H */
