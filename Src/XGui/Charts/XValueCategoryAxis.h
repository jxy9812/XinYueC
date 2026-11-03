/**
 * @file       XValueCategoryAxis.h
 * @brief      XValueCategoryAxis 数值类别轴（对标 Qt Charts 6.8 QCategoryAxis）。
 * @details    与 XCategoryAxis（对标 QBarCategoryAxis）是**两个不同的类**：
 *             - QBarCategoryAxis：类别是纯字符串，按序均分，无边界值；
 *             - QCategoryAxis（本类）：继承 QValueAxis，每个类别带一个
 *               **结束值边界**，类别在数值域上占 [prevEnd, endValue] 区间。
 *
 *             本类一并继承 XValueAxis（对应 QCategoryAxis : QValueAxis），
 *             因此具备数值轴的全部属性（min/max/tickCount/labelFormat 等），
 *             并叠加类别边界表与标签位置策略。
 * @note       模块总开关 XCHARTS_ON；实现只依赖 XinYueC 抽象层，
 *             禁止调用 Win32、POSIX、Qt 或其他平台 API。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XVALUECATEGORYAXIS_H
#define XVALUECATEGORYAXIS_H

#if XGUI_ON
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XValueAxis.h"
#include "XString.h"

#if XCHARTS_ON

/**
 * @brief      类别标签位置（对标 Qt Charts 6.8 QCategoryAxis::AxisLabelsPosition，
 *             数值一致）。
 */
typedef enum XValueCategoryAxis_AxisLabelsPosition
{
    XValueCategoryAxis_AxisLabelsPosition_Center = 0x0,  /**< 居中于类别区间（对标 AxisLabelsPositionCenter）。 */
    XValueCategoryAxis_AxisLabelsPosition_OnValue = 0x1  /**< 落在类别结束值处（对标 AxisLabelsPositionOnValue）。 */
} XValueCategoryAxis_AxisLabelsPosition;

XCLASS_DEFINE_BEGING(XValueCategoryAxis)
XCLASS_DEFINE_EXTEND_END(XValueCategoryAxis, XValueAxis)

/** @brief 数值类别轴（m_base 必须是第一个成员；对标 QCategoryAxis）。 */
typedef struct XValueCategoryAxis
{
    XValueAxis m_base;         /**< 基类成员；必须是第一个（数值轴全部属性）。 */

    /* ---- 8 字节对齐组 ---- */
    XString** m_labels;        /**< 类别标签数组（堆；对象拥有）。 */
    double* m_endValues;       /**< 各类别结束值（堆；升序；对象拥有）。 */
    int m_count;               /**< 类别数。 */
    int m_capacity;            /**< 容量。 */

    /* ---- 4 字节对齐组 ---- */
    int m_labelsPosition;      /**< XValueCategoryAxis_AxisLabelsPosition。 */
} XValueCategoryAxis;

/** @brief 初始化类虚函数表。 @return 共享虚函数表指针。 */
XVtable* XValueCategoryAxis_class_init(void);
/** @brief 初始化嵌入式数值类别轴。 @param self 目标轴指针。 @return 无返回值。 */
void XValueCategoryAxis_init(XValueCategoryAxis* self);
/** @brief 反初始化（释放标签与边界数组）。 @param self 目标轴指针。 @return 无返回值。 */
void XValueCategoryAxis_deinit_impl(XValueCategoryAxis* self);
/** @brief 堆上创建数值类别轴。 @param memory 内存类型。 @return 新建对象；失败 NULL。 */
XValueCategoryAxis* XValueCategoryAxis_create_ex(XMemoryType memory);
#define XValueCategoryAxis_create() \
    XValueCategoryAxis_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

/* ==================== 类型查询（对标 QCategoryAxis） ==================== */

/** @brief 查询轴类型（对标 QCategoryAxis::type）。 @param self 目标轴指针。 @return 固定返回 AxisTypeCategory。 */
int XValueCategoryAxis_type(const XValueCategoryAxis* self);

/* ==================== 类别边界（对标 QCategoryAxis） ==================== */

/**
 * @brief 追加类别及其结束值（对标 append(label, categoryEndValue)）。
 * @details 结束值必须大于已有最大结束值（保持升序）；不满足时返回 false。
 * @param self 目标轴指针。
 * @param label 借用 XString*；不能为 NULL。
 * @param categoryEndValue 该类别在数值域上的结束值。
 * @return 追加成功返回 true。
 */
bool XValueCategoryAxis_append(XValueCategoryAxis* self, const XString* label,
                               double categoryEndValue);
/** @brief 追加类别（UTF-8 编码版本）。 @param self 目标轴指针。 @param label UTF-8 标签（借用）。 @param categoryEndValue 结束值。 @return 追加成功返回 true。 */
bool XValueCategoryAxis_append_utf8(XValueCategoryAxis* self, const char* label,
                                    double categoryEndValue);
/** @brief 追加类别（UTF-8 兼容别名）。 @param self 目标轴指针。 @param label UTF-8 标签。 @param categoryEndValue 结束值。 @return 追加成功返回 true。 */
bool XValueCategoryAxis_append_2(XValueCategoryAxis* self, const char* label,
                                 double categoryEndValue);

/**
 * @brief 移除类别（对标 remove(label)）。
 * @param self 目标轴指针。
 * @param label 借用 XString*；不能为 NULL。
 * @return 找到并移除返回 true。
 */
bool XValueCategoryAxis_remove(XValueCategoryAxis* self, const XString* label);
/** @brief 移除类别（UTF-8 编码版本）。 @param self 目标轴指针。 @param label UTF-8 标签。 @return 找到并移除返回 true。 */
bool XValueCategoryAxis_remove_utf8(XValueCategoryAxis* self, const char* label);

/**
 * @brief 重命名类别（对标 replaceLabel）。
 * @param self 目标轴指针。
 * @param oldLabel 借用 XString* 旧标签。
 * @param newLabel 借用 XString* 新标签。
 * @return 找到并替换返回 true。
 */
bool XValueCategoryAxis_replaceLabel(XValueCategoryAxis* self,
                                     const XString* oldLabel,
                                     const XString* newLabel);
/** @brief 重命名类别（UTF-8 编码版本）。 @param self 目标轴指针。 @param oldLabel UTF-8 旧标签。 @param newLabel UTF-8 新标签。 @return 找到并替换返回 true。 */
bool XValueCategoryAxis_replaceLabel_utf8(XValueCategoryAxis* self,
                                          const char* oldLabel,
                                          const char* newLabel);

/** @brief 清空全部类别（Qt 对应 clear 语义）。 @param self 目标轴指针。 @return 无返回值。 */
void XValueCategoryAxis_clear(XValueCategoryAxis* self);

/** @brief 查询类别数（对标 count）。 @param self 目标轴指针。 @return 类别数。 */
int XValueCategoryAxis_count(const XValueCategoryAxis* self);

/**
 * @brief 设置起始值（对标 setStartValue）。
 * @details 起始值即首个类别的左边界，等价于设置轴域下限；之后所有类别
 *          的区间随之平移。
 * @param self 目标轴指针。
 * @param min 起始值。
 * @return 无返回值。
 */
void XValueCategoryAxis_setStartValue(XValueCategoryAxis* self, double min);

/**
 * @brief 查询类别的起始值（对标 startValue）。
 * @details 首类别起始值 = 该轴 min；其余类别 = 前一个类别的结束值。
 *          传入 label 为空时返回轴域起点。
 * @param self 目标轴指针。
 * @param label 借用 XString*；NULL 或空表示查询轴域起点。
 * @return 起始值；类别不存在返回 0。
 */
double XValueCategoryAxis_startValue(const XValueCategoryAxis* self,
                                     const XString* label);
/**
 * @brief 查询类别的结束值（对标 endValue）。
 * @param self 目标轴指针。
 * @param label 借用 XString*；不能为 NULL。
 * @return 结束值；类别不存在返回 0。
 */
double XValueCategoryAxis_endValue(const XValueCategoryAxis* self,
                                   const XString* label);
/**
 * @brief 查询类别标签（按下标；对标 categoriesLabels 的逐项访问）。
 * @param self 目标轴指针。
 * @param index 下标。
 * @return 借用 XString*；越界返回 NULL。调用者不得释放。
 */
const XString* XValueCategoryAxis_labelAt_const(const XValueCategoryAxis* self,
                                                int index);
/** @brief 查询类别标签（UTF-8 借用视图）。 @param self 目标轴指针。 @param index 下标。 @return UTF-8 串；越界返回空串。 */
const char* XValueCategoryAxis_labelAt_2(const XValueCategoryAxis* self,
                                         int index);

/**
 * @brief 批量导出类别标签（对标 categoriesLabels() 的列表形态）。
 * @details C 侧以「输出缓冲 + 容量」表达 QStringList；返回实际写入数。
 * @param self 目标轴指针。
 * @param out 输出缓冲（调用方提供，至少 maxCount 项）。
 * @param maxCount 缓冲容量。
 * @return 实际写入的标签数。
 */
int XValueCategoryAxis_categoriesLabels(const XValueCategoryAxis* self,
                                        const XString** out, int maxCount);

/* ==================== 标签位置（对标 QCategoryAxis） ==================== */

/** @brief 设置标签位置（对标 setLabelsPosition）。 @param self 目标轴指针。 @param position 位置枚举。 @return 无返回值。 */
void XValueCategoryAxis_setLabelsPosition(
    XValueCategoryAxis* self, XValueCategoryAxis_AxisLabelsPosition position);
/** @brief 查询标签位置（对标 labelsPosition）。 @param self 目标轴指针。 @return 位置枚举。 */
XValueCategoryAxis_AxisLabelsPosition XValueCategoryAxis_labelsPosition(
    const XValueCategoryAxis* self);

/* ==================== 信号（对标 QCategoryAxis Q_SIGNALS） ==================== */

/** @brief categoriesChanged 信号（无载荷）。 */
void* XValueCategoryAxis_categoriesChanged_signal(XValueCategoryAxis* self);
/** @brief labelsPositionChanged 信号（载荷：位置枚举）。 */
void* XValueCategoryAxis_labelsPositionChanged_signal(
    XValueCategoryAxis* self, int position);

#endif /* XCHARTS_ON */
#ifdef __cplusplus
}
#endif

#endif /* XGUI_ON */

#endif /* XVALUECATEGORYAXIS_H */
