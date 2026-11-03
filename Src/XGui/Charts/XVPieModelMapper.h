/**
 * @file       XVPieModelMapper.h
 * @brief      纵向饼图模型映射器（对标 Qt Charts 6.8 QVPieModelMapper）。
 * @details    继承 XPieModelMapper（对应 QVPieModelMapper : QPieModelMapper）。
 *             纵向语义：valuesColumn/labelsColumn 指定取值所在**列**，
 *             firstRow/rowCount 沿**行**推进。
 * @note       模块总开关 XCHARTS_ON；实现只依赖 XinYueC 抽象层，
 *             禁止调用 Win32、POSIX、Qt 或其他平台 API。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XVPIEMODELMAPPER_H
#define XVPIEMODELMAPPER_H

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

XCLASS_DEFINE_BEGING(XVPieModelMapper)
XCLASS_DEFINE_EXTEND_END(XVPieModelMapper, XPieModelMapper)

/** @brief QVPieModelMapper 对应实现。 */
typedef struct XVPieModelMapper
{
    XPieModelMapper m_base; /**< 基类成员；必须是第一个。 */
} XVPieModelMapper;

/** @brief 初始化类虚函数表。 @return 共享虚函数表指针。 */
XVtable* XVPieModelMapper_class_init(void);
/** @brief 初始化嵌入式映射器。 @param self 目标映射器指针。 @return 无返回值。 */
void XVPieModelMapper_init(XVPieModelMapper* self);
/** @brief 堆上创建映射器。 @param memory 内存类型。 @return 新建对象；失败 NULL。 */
XVPieModelMapper* XVPieModelMapper_create_ex(XMemoryType memory);
#define XVPieModelMapper_create() XVPieModelMapper_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

/* ==================== 专有参数（对标 QVPieModelMapper） ==================== */
/** @brief 设置数值所在列（对标 valuesColumn）。 @param self 目标映射器指针。 @param valuesColumn 行/列下标。 @return 无返回值。 */
void XVPieModelMapper_setValuesColumn(XVPieModelMapper* self, int valuesColumn);
/** @brief 查询数值所在列（对标 valuesColumn）。 @param self 目标映射器指针。 @return 行/列下标；未设置返回 -1。 */
int XVPieModelMapper_valuesColumn(const XVPieModelMapper* self);
/** @brief 设置标签所在列（对标 labelsColumn）。 @param self 目标映射器指针。 @param labelsColumn 行/列下标。 @return 无返回值。 */
void XVPieModelMapper_setLabelsColumn(XVPieModelMapper* self, int labelsColumn);
/** @brief 查询标签所在列（对标 labelsColumn）。 @param self 目标映射器指针。 @return 行/列下标；未设置返回 -1。 */
int XVPieModelMapper_labelsColumn(const XVPieModelMapper* self);
/** @brief 设置起始行（对标 firstRow）。 @param self 目标映射器指针。 @param firstRow 行/列下标。 @return 无返回值。 */
void XVPieModelMapper_setFirstRow(XVPieModelMapper* self, int firstRow);
/** @brief 查询起始行（对标 firstRow）。 @param self 目标映射器指针。 @return 行/列下标；未设置返回 -1。 */
int XVPieModelMapper_firstRow(const XVPieModelMapper* self);
/** @brief 设置行数量（对标 rowCount）。 @param self 目标映射器指针。 @param rowCount 行/列下标。 @return 无返回值。 */
void XVPieModelMapper_setRowCount(XVPieModelMapper* self, int rowCount);
/** @brief 查询行数量（对标 rowCount）。 @param self 目标映射器指针。 @return 行/列下标；未设置返回 -1。 */
int XVPieModelMapper_rowCount(const XVPieModelMapper* self);

/* ==================== 信号（对标 QVPieModelMapper Q_SIGNALS） ==================== */

/** @brief seriesReplaced 信号（无载荷）。 */
void* XVPieModelMapper_seriesReplaced_signal(XVPieModelMapper* self);
/** @brief modelReplaced 信号（无载荷）。 */
void* XVPieModelMapper_modelReplaced_signal(XVPieModelMapper* self);
/** @brief ValuesColumnChanged 信号（无载荷）。 */
void* XVPieModelMapper_ValuesColumnChanged_signal(XVPieModelMapper* self);
/** @brief LabelsColumnChanged 信号（无载荷）。 */
void* XVPieModelMapper_LabelsColumnChanged_signal(XVPieModelMapper* self);
/** @brief FirstRowChanged 信号（无载荷）。 */
void* XVPieModelMapper_FirstRowChanged_signal(XVPieModelMapper* self);
/** @brief RowCountChanged 信号（无载荷）。 */
void* XVPieModelMapper_RowCountChanged_signal(XVPieModelMapper* self);

#endif /* XCHARTS_ON */
#ifdef __cplusplus
}
#endif

#endif /* XGUI_ON */

#endif /* XVPIEMODELMAPPER_H */
