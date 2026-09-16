#ifndef XPIESERIES_H
#define XPIESERIES_H
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XObject.h"
#include "XAbstractSeries.h"
#include "XString.h"
#include "XPieSlice.h"

#if XCHARTS_ON

XCLASS_DEFINE_BEGING(XPieSeries)
XCLASS_DEFINE_EXTEND_END(XPieSeries, XAbstractSeries)

/** @brief 饼图序列（对标 QPieSeries；切片集合经 XPieSlice* 管理）。 */
typedef struct XPieSeries
{
    XAbstractSeries m_base;   /**< 基类成员；必须是第一个。 */
    XPieSlice** m_slices;     /**< 切片指针数组（堆；对象拥有切片）。 */
    int m_count;              /**< 切片数。 */
    int m_capacity;           /**< 容量。 */
    double m_holeSize;        /**< 中心孔径比例 0-1（0=实心饼，默认 0）。 */
    double m_horizontalPosition; /**< 水平位置（0-1；对标 horizontalPosition）。 */
    double m_verticalPosition;   /**< 垂直位置（0-1；对标 verticalPosition）。 */
    double m_pieSize;            /**< 饼图尺寸比例（0-1；对标 pieSize）。 */
    double m_pieStartAngle;      /**< 起始角（度；对标 pieStartAngle）。 */
    double m_pieEndAngle;        /**< 结束角（度；对标 pieEndAngle）。 */
    bool m_labelsVisible;        /**< 全部切片标签可见（对标 setLabelsVisible）。 */
} XPieSeries;

XVtable* XPieSeries_class_init(void);

/**
 * @brief 初始化嵌入式饼图序列。
 *
 * @param self 目标序列指针，不能为空。
 * @return 无返回值。
 */
void XPieSeries_init(XPieSeries* self);

/**
 * @brief 堆上创建饼图序列。
 *
 * @param memory 内存类型。
 * @return 序列指针；分配失败返回 NULL。
 */
XPieSeries* XPieSeries_create_ex(XMemoryType memory);
#define XPieSeries_create() XPieSeries_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

/** @brief 析构入口（查表分派父类析构）。 */
#define XPieSeries_deinit_base(self) XClass_deinit_base((XClass*)(self))

/** @brief 删除堆上序列（查表分派析构并释放内存）。 */
#define XPieSeries_delete_base(self) XClass_delete_base((XClass*)(self))

/* ==================== 切片集合（对标 QPieSeries） ==================== */

/** @brief 追加切片对象（接管所有权；对标 append(QPieSlice*)）。 @param self 目标序列指针。 @param slice 切片指针。 @return 成功返回 true。 */
bool XPieSeries_appendSlice(XPieSeries* self, XPieSlice* slice);

/** @brief 追加切片（XString 主版本；对标 append(label, value)）。
 * @param self 目标序列指针。
 * @param label 借用 XString*；不能为 NULL。
 * @param value 切片值。
 * @return 切片指针；失败返回 NULL。 */
XPieSlice* XPieSeries_append(XPieSeries* self, const XString* label, double value);
/** @brief 追加切片（UTF-8 兼容重载，转发主版本）。 */
XPieSlice* XPieSeries_append_2(XPieSeries* self, const char* label, double value);

/** @brief 在指定位置插入切片（接管所有权；对标 insert）。 @param self 目标序列指针。 @param index 插入下标。 @param slice 切片指针。 @return 成功返回 true。 */
bool XPieSeries_insert(XPieSeries* self, int index, XPieSlice* slice);

/** @brief 移除并释放切片（对标 remove）。 @param self 目标序列指针。 @param slice 切片指针。 @return 移除成功返回 true。 */
bool XPieSeries_remove(XPieSeries* self, XPieSlice* slice);

/** @brief 摘除切片但保留所有权（对标 take；调用方负责释放）。 @param self 目标序列指针。 @param slice 切片指针。 @return 摘除成功返回 true。 */
bool XPieSeries_take(XPieSeries* self, XPieSlice* slice);

/** @brief 清空并释放全部切片（对标 clear）。 @param self 目标序列指针。 @return 无返回值。 */
void XPieSeries_clear(XPieSeries* self);

/** @brief 批量读取切片指针（对标 slices()）。 @param self 目标序列指针。 @param out 输出缓冲。 @param maxCount 容量。 @return 实际切片数。 */
int XPieSeries_slices(const XPieSeries* self, XPieSlice** out, int maxCount);
/** @brief 查询切片数。 @param self 目标序列指针。 @return 切片数。 */
int XPieSeries_count(const XPieSeries* self);
/** @brief 按下标取切片。 @param self 目标序列指针。 @param index 下标。 @return 切片指针；越界返回 NULL。 */
XPieSlice* XPieSeries_slice(const XPieSeries* self, int index);
/** @brief 查询是否为空（对标 isEmpty）。 @param self 目标序列指针。 @return 无切片返回 true。 */
bool XPieSeries_isEmpty(const XPieSeries* self);
/** @brief 查询值总和（渲染占比用）。 @param self 目标序列指针。 @return 值总和。 */
double XPieSeries_sum(const XPieSeries* self);

/* ==================== 几何属性（对标 QPieSeries 属性） ==================== */

/** @brief 设置中心孔径比例。 @param self 目标序列指针。 @param hole 0-1（0=实心，默认 0）。 @return 无返回值。 */
void XPieSeries_setHoleSize(XPieSeries* self, double hole);
/** @brief 读取中心孔径比例。 @param self 目标序列指针。 @return 孔径比例。 */
double XPieSeries_holeSize(const XPieSeries* self);

/** @brief 设置水平位置（0-1）。 @param self 目标序列指针。 @param relativePosition 相对位置。 @return 无返回值。 */
void XPieSeries_setHorizontalPosition(XPieSeries* self, double relativePosition);
/** @brief 读取水平位置。 @param self 目标序列指针。 @return 相对位置。 */
double XPieSeries_horizontalPosition(const XPieSeries* self);

/** @brief 设置垂直位置（0-1）。 @param self 目标序列指针。 @param relativePosition 相对位置。 @return 无返回值。 */
void XPieSeries_setVerticalPosition(XPieSeries* self, double relativePosition);
/** @brief 读取垂直位置。 @param self 目标序列指针。 @return 相对位置。 */
double XPieSeries_verticalPosition(const XPieSeries* self);

/** @brief 设置饼图尺寸比例（0-1）。 @param self 目标序列指针。 @param relativeSize 相对尺寸。 @return 无返回值。 */
void XPieSeries_setPieSize(XPieSeries* self, double relativeSize);
/** @brief 读取饼图尺寸比例。 @param self 目标序列指针。 @return 相对尺寸。 */
double XPieSeries_pieSize(const XPieSeries* self);

/** @brief 设置饼图起始角（度）。 @param self 目标序列指针。 @param startAngle 起始角。 @return 无返回值。 */
void XPieSeries_setPieStartAngle(XPieSeries* self, double startAngle);
/** @brief 读取饼图起始角。 @param self 目标序列指针。 @return 起始角（度）。 */
double XPieSeries_pieStartAngle(const XPieSeries* self);

/** @brief 设置饼图结束角（度）。 @param self 目标序列指针。 @param endAngle 结束角。 @return 无返回值。 */
void XPieSeries_setPieEndAngle(XPieSeries* self, double endAngle);
/** @brief 读取饼图结束角。 @param self 目标序列指针。 @return 结束角（度）。 */
double XPieSeries_pieEndAngle(const XPieSeries* self);

/** @brief 设置全部切片标签可见（对标 setLabelsVisible）。 @param self 目标序列指针。 @param visible true 显示。 @return 无返回值。 */
void XPieSeries_setLabelsVisible(XPieSeries* self, bool visible);
/** @brief 查询全部切片标签可见。 @param self 目标序列指针。 @return 可见返回 true。 */
bool XPieSeries_labelsVisible(const XPieSeries* self);

/** @brief 设置全部切片标签位置（对标 setLabelsPosition）。 @param self 目标序列指针。 @param position 位置枚举。 @return 无返回值。 */
void XPieSeries_setLabelsPosition(XPieSeries* self,
                                  XPieSlice_LabelPosition position);

/* ==================== 通用序列属性 ==================== */

/** @brief 查询序列类型（对标 QAbstractSeries::type）。 @param self 目标序列指针。 @return 固定返回饼图类型 5（XChartSeriesType_Pie）。 */
int XPieSeries_type(const XPieSeries* self);

/**
 * @brief 重算全部切片的占比与角度（布局用）。
 *
 * @param self 目标序列指针。
 * @return 无返回值。
 */
void XPieSeries_updateAngles(XPieSeries* self);

/* ==================== 信号（对标 QPieSeries Q_SIGNALS） ==================== */

/**
 * @brief 发射切片加入信号。
 *
 * @param self  序列指针；NULL 返回信号标识。
 * @param slice 加入的切片。
 * @return 信号标识指针。
 */
void* XPieSeries_added_signal(XPieSeries* self, XPieSlice* slice);

/**
 * @brief 发射切片移除信号。
 *
 * @param self  序列指针；NULL 返回信号标识。
 * @param slice 移除的切片。
 * @return 信号标识指针。
 */
void* XPieSeries_removed_signal(XPieSeries* self, XPieSlice* slice);

/**
 * @brief 发射切片点击信号。
 *
 * @param self  序列指针；NULL 返回信号标识。
 * @param slice 命中的切片。
 * @return 信号标识指针。
 */
void* XPieSeries_clicked_signal(XPieSeries* self, XPieSlice* slice);

/**
 * @brief 发射切片悬停信号。
 *
 * @param self  序列指针；NULL 返回信号标识。
 * @param slice 命中的切片。
 * @param state true 进入悬停。
 * @return 信号标识指针。
 */
void* XPieSeries_hovered_signal(XPieSeries* self, XPieSlice* slice, bool state);

/**
 * @brief 发射切片按压信号。
 *
 * @param self  序列指针；NULL 返回信号标识。
 * @param slice 命中的切片。
 * @return 信号标识指针。
 */
void* XPieSeries_pressed_signal(XPieSeries* self, XPieSlice* slice);

/**
 * @brief 发射切片释放信号。
 *
 * @param self  序列指针；NULL 返回信号标识。
 * @param slice 命中的切片。
 * @return 信号标识指针。
 */
void* XPieSeries_released_signal(XPieSeries* self, XPieSlice* slice);

/**
 * @brief 发射切片双击信号。
 *
 * @param self  序列指针；NULL 返回信号标识。
 * @param slice 命中的切片。
 * @return 信号标识指针。
 */
void* XPieSeries_doubleClicked_signal(XPieSeries* self, XPieSlice* slice);

/**
 * @brief 发射切片数变化信号。
 *
 * @param self 序列指针；NULL 返回信号标识。
 * @return 信号标识指针。
 */
void* XPieSeries_countChanged_signal(XPieSeries* self);

/**
 * @brief 发射值总和变化信号。
 *
 * @param self 序列指针；NULL 返回信号标识。
 * @return 信号标识指针。
 */
void* XPieSeries_sumChanged_signal(XPieSeries* self);

#endif /* XCHARTS_ON */
#ifdef __cplusplus
}
#endif
#endif /* XPIESERIES_H */
