/**
 * @file       XVXYModelMapper.h
 * @brief      纵向 XY 模型映射器（对标 Qt Charts 6.8 QVXYModelMapper）。
 * @details    继承 XXYModelMapper（对应 QVXYModelMapper : QXYModelMapper）。
 *             纵向语义：xColumn/yColumn 指定取值所在**列**，firstRow/rowCount
 *             沿**行**推进。
 * @note       模块总开关 XCHARTS_ON。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XVXYMODELMAPPER_H
#define XVXYMODELMAPPER_H

#if XGUI_ON
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XXYModelMapper.h"

#if XCHARTS_ON

XCLASS_DEFINE_BEGING(XVXYModelMapper)
XCLASS_DEFINE_EXTEND_END(XVXYModelMapper, XXYModelMapper)

/** @brief 纵向 XY 模型映射器（对标 QVXYModelMapper）。 */
typedef struct XVXYModelMapper
{
    XXYModelMapper m_base; /**< 基类成员；必须是第一个。 */
} XVXYModelMapper;

/** @brief 初始化类虚函数表。 @return 共享虚函数表指针。 */
XVtable* XVXYModelMapper_class_init(void);
/** @brief 初始化嵌入式纵向 XY 映射器。 @param self 目标映射器指针。 @return 无返回值。 */
void XVXYModelMapper_init(XVXYModelMapper* self);
/** @brief 堆上创建纵向 XY 映射器。 @param memory 内存类型。 @return 新建对象；失败 NULL。 */
XVXYModelMapper* XVXYModelMapper_create_ex(XMemoryType memory);
#define XVXYModelMapper_create() \
    XVXYModelMapper_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

/* ==================== V 向专有参数（对标 QVXYModelMapper） ==================== */

/** @brief 设置 x 所在列（对标 setXColumn）。 @param self 目标映射器指针。 @param xColumn 列下标。 @return 无返回值。 */
void XVXYModelMapper_setXColumn(XVXYModelMapper* self, int xColumn);
/** @brief 查询 x 所在列（对标 xColumn）。 @param self 目标映射器指针。 @return 列下标；未设置返回 -1。 */
int XVXYModelMapper_xColumn(const XVXYModelMapper* self);
/** @brief 设置 y 所在列（对标 setYColumn）。 @param self 目标映射器指针。 @param yColumn 列下标。 @return 无返回值。 */
void XVXYModelMapper_setYColumn(XVXYModelMapper* self, int yColumn);
/** @brief 查询 y 所在列（对标 yColumn）。 @param self 目标映射器指针。 @return 列下标；未设置返回 -1。 */
int XVXYModelMapper_yColumn(const XVXYModelMapper* self);
/** @brief 设置起始行（对标 setFirstRow）。 @param self 目标映射器指针。 @param firstRow 行下标。 @return 无返回值。 */
void XVXYModelMapper_setFirstRow(XVXYModelMapper* self, int firstRow);
/** @brief 查询起始行（对标 firstRow）。 @param self 目标映射器指针。 @return 行下标。 */
int XVXYModelMapper_firstRow(const XVXYModelMapper* self);
/** @brief 设置行数量（对标 setRowCount）。 @param self 目标映射器指针。 @param rowCount 数量（-1=按模型推导）。 @return 无返回值。 */
void XVXYModelMapper_setRowCount(XVXYModelMapper* self, int rowCount);
/** @brief 查询行数量（对标 rowCount）。 @param self 目标映射器指针。 @return 数量；-1 表示按模型推导。 */
int XVXYModelMapper_rowCount(const XVXYModelMapper* self);

/* ==================== 信号（对标 QVXYModelMapper Q_SIGNALS） ==================== */

/** @brief seriesReplaced 信号（无载荷）。 */
void* XVXYModelMapper_seriesReplaced_signal(XVXYModelMapper* self);
/** @brief modelReplaced 信号（无载荷）。 */
void* XVXYModelMapper_modelReplaced_signal(XVXYModelMapper* self);
/** @brief xColumnChanged 信号（无载荷）。 */
void* XVXYModelMapper_xColumnChanged_signal(XVXYModelMapper* self);
/** @brief yColumnChanged 信号（无载荷）。 */
void* XVXYModelMapper_yColumnChanged_signal(XVXYModelMapper* self);
/** @brief firstRowChanged 信号（无载荷）。 */
void* XVXYModelMapper_firstRowChanged_signal(XVXYModelMapper* self);
/** @brief rowCountChanged 信号（无载荷）。 */
void* XVXYModelMapper_rowCountChanged_signal(XVXYModelMapper* self);

#endif /* XCHARTS_ON */
#ifdef __cplusplus
}
#endif

#endif /* XGUI_ON */

#endif /* XVXYMODELMAPPER_H */
