/**
 * @file       XHPieModelMapper.h
 * @brief      横向饼图模型映射器（对标 Qt Charts 6.8 QHPieModelMapper）。
 * @details    继承 XPieModelMapper（对应 QHPieModelMapper : QPieModelMapper）。
 *             横向语义：valuesRow/labelsRow 指定取值所在**行**，
 *             firstColumn/columnCount 沿**列**推进。
 * @note       模块总开关 XCHARTS_ON；实现只依赖 XinYueC 抽象层，
 *             禁止调用 Win32、POSIX、Qt 或其他平台 API。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XHPIEMODELMAPPER_H
#define XHPIEMODELMAPPER_H

#if XGUI_ON
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XAbstractItemModel.h"
#include "XModelMapper_Protected.h"
#include "XPieModelMapper.h"

#if XCHARTS_ON

XCLASS_DEFINE_BEGING(XHPieModelMapper)
XCLASS_DEFINE_EXTEND_END(XHPieModelMapper, XPieModelMapper)

/** @brief QHPieModelMapper 对应实现。 */
typedef struct XHPieModelMapper
{
    XPieModelMapper m_base; /**< 基类成员；必须是第一个。 */
} XHPieModelMapper;

/** @brief 初始化类虚函数表。 @return 共享虚函数表指针。 */
XVtable* XHPieModelMapper_class_init(void);
/** @brief 初始化嵌入式映射器。 @param self 目标映射器指针。 @return 无返回值。 */
void XHPieModelMapper_init(XHPieModelMapper* self);
/** @brief 堆上创建映射器。 @param memory 内存类型。 @return 新建对象；失败 NULL。 */
XHPieModelMapper* XHPieModelMapper_create_ex(XMemoryType memory);
#define XHPieModelMapper_create() XHPieModelMapper_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

/* ==================== 专有参数（对标 QHPieModelMapper） ==================== */
/** @brief 设置数值所在行（对标 valuesRow）。 @param self 目标映射器指针。 @param valuesRow 行/列下标。 @return 无返回值。 */
void XHPieModelMapper_setValuesRow(XHPieModelMapper* self, int valuesRow);
/** @brief 查询数值所在行（对标 valuesRow）。 @param self 目标映射器指针。 @return 行/列下标；未设置返回 -1。 */
int XHPieModelMapper_valuesRow(const XHPieModelMapper* self);
/** @brief 设置标签所在行（对标 labelsRow）。 @param self 目标映射器指针。 @param labelsRow 行/列下标。 @return 无返回值。 */
void XHPieModelMapper_setLabelsRow(XHPieModelMapper* self, int labelsRow);
/** @brief 查询标签所在行（对标 labelsRow）。 @param self 目标映射器指针。 @return 行/列下标；未设置返回 -1。 */
int XHPieModelMapper_labelsRow(const XHPieModelMapper* self);
/** @brief 设置起始列（对标 firstColumn）。 @param self 目标映射器指针。 @param firstColumn 行/列下标。 @return 无返回值。 */
void XHPieModelMapper_setFirstColumn(XHPieModelMapper* self, int firstColumn);
/** @brief 查询起始列（对标 firstColumn）。 @param self 目标映射器指针。 @return 行/列下标；未设置返回 -1。 */
int XHPieModelMapper_firstColumn(const XHPieModelMapper* self);
/** @brief 设置列数量（对标 columnCount）。 @param self 目标映射器指针。 @param columnCount 行/列下标。 @return 无返回值。 */
void XHPieModelMapper_setColumnCount(XHPieModelMapper* self, int columnCount);
/** @brief 查询列数量（对标 columnCount）。 @param self 目标映射器指针。 @return 行/列下标；未设置返回 -1。 */
int XHPieModelMapper_columnCount(const XHPieModelMapper* self);

/* ==================== 信号（对标 QHPieModelMapper Q_SIGNALS） ==================== */

/** @brief seriesReplaced 信号（无载荷）。 */
void* XHPieModelMapper_seriesReplaced_signal(XHPieModelMapper* self);
/** @brief modelReplaced 信号（无载荷）。 */
void* XHPieModelMapper_modelReplaced_signal(XHPieModelMapper* self);
/** @brief ValuesRowChanged 信号（无载荷）。 */
void* XHPieModelMapper_ValuesRowChanged_signal(XHPieModelMapper* self);
/** @brief LabelsRowChanged 信号（无载荷）。 */
void* XHPieModelMapper_LabelsRowChanged_signal(XHPieModelMapper* self);
/** @brief FirstColumnChanged 信号（无载荷）。 */
void* XHPieModelMapper_FirstColumnChanged_signal(XHPieModelMapper* self);
/** @brief ColumnCountChanged 信号（无载荷）。 */
void* XHPieModelMapper_ColumnCountChanged_signal(XHPieModelMapper* self);

#endif /* XCHARTS_ON */
#ifdef __cplusplus
}
#endif

#endif /* XGUI_ON */

#endif /* XHPIEMODELMAPPER_H */
