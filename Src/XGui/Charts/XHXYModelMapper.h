/**
 * @file       XHXYModelMapper.h
 * @brief      横向 XY 模型映射器（对标 Qt Charts 6.8 QHXYModelMapper）。
 * @details    继承 XXYModelMapper（对应 QHXYModelMapper : QXYModelMapper）。
 *             横向语义：xRow/yRow 指定取值所在**行**，firstColumn/columnCount
 *             沿**列**推进。
 * @note       模块总开关 XCHARTS_ON。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XHXYMODELMAPPER_H
#define XHXYMODELMAPPER_H

#if XGUI_ON
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XAbstractItemModel.h"
#include "XModelMapper_Protected.h"
#include "XXYModelMapper.h"

#if XCHARTS_ON

XCLASS_DEFINE_BEGING(XHXYModelMapper)
XCLASS_DEFINE_EXTEND_END(XHXYModelMapper, XXYModelMapper)

/** @brief 横向 XY 模型映射器（对标 QHXYModelMapper）。 */
typedef struct XHXYModelMapper
{
    XXYModelMapper m_base; /**< 基类成员；必须是第一个。 */
} XHXYModelMapper;

/** @brief 初始化类虚函数表。 @return 共享虚函数表指针。 */
XVtable* XHXYModelMapper_class_init(void);
/** @brief 初始化嵌入式横向 XY 映射器。 @param self 目标映射器指针。 @return 无返回值。 */
void XHXYModelMapper_init(XHXYModelMapper* self);
/** @brief 堆上创建横向 XY 映射器。 @param memory 内存类型。 @return 新建对象；失败 NULL。 */
XHXYModelMapper* XHXYModelMapper_create_ex(XMemoryType memory);
#define XHXYModelMapper_create() \
    XHXYModelMapper_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

/* ==================== H 向专有参数（对标 QHXYModelMapper） ==================== */

/** @brief 设置 x 所在行（对标 setXRow）。 @param self 目标映射器指针。 @param xRow 行下标。 @return 无返回值。 */
void XHXYModelMapper_setXRow(XHXYModelMapper* self, int xRow);
/** @brief 查询 x 所在行（对标 xRow）。 @param self 目标映射器指针。 @return 行下标；未设置返回 -1。 */
int XHXYModelMapper_xRow(const XHXYModelMapper* self);
/** @brief 设置 y 所在行（对标 setYRow）。 @param self 目标映射器指针。 @param yRow 行下标。 @return 无返回值。 */
void XHXYModelMapper_setYRow(XHXYModelMapper* self, int yRow);
/** @brief 查询 y 所在行（对标 yRow）。 @param self 目标映射器指针。 @return 行下标；未设置返回 -1。 */
int XHXYModelMapper_yRow(const XHXYModelMapper* self);
/** @brief 设置起始列（对标 setFirstColumn）。 @param self 目标映射器指针。 @param firstColumn 列下标。 @return 无返回值。 */
void XHXYModelMapper_setFirstColumn(XHXYModelMapper* self, int firstColumn);
/** @brief 查询起始列（对标 firstColumn）。 @param self 目标映射器指针。 @return 列下标。 */
int XHXYModelMapper_firstColumn(const XHXYModelMapper* self);
/** @brief 设置列数量（对标 setColumnCount）。 @param self 目标映射器指针。 @param columnCount 数量（-1=按模型推导）。 @return 无返回值。 */
void XHXYModelMapper_setColumnCount(XHXYModelMapper* self, int columnCount);
/** @brief 查询列数量（对标 columnCount）。 @param self 目标映射器指针。 @return 数量；-1 表示按模型推导。 */
int XHXYModelMapper_columnCount(const XHXYModelMapper* self);

/* ==================== 信号（对标 QHXYModelMapper Q_SIGNALS） ==================== */

/** @brief seriesReplaced 信号（无载荷）。 */
void* XHXYModelMapper_seriesReplaced_signal(XHXYModelMapper* self);
/** @brief modelReplaced 信号（无载荷）。 */
void* XHXYModelMapper_modelReplaced_signal(XHXYModelMapper* self);
/** @brief xRowChanged 信号（无载荷）。 */
void* XHXYModelMapper_xRowChanged_signal(XHXYModelMapper* self);
/** @brief yRowChanged 信号（无载荷）。 */
void* XHXYModelMapper_yRowChanged_signal(XHXYModelMapper* self);
/** @brief firstColumnChanged 信号（无载荷）。 */
void* XHXYModelMapper_firstColumnChanged_signal(XHXYModelMapper* self);
/** @brief columnCountChanged 信号（无载荷）。 */
void* XHXYModelMapper_columnCountChanged_signal(XHXYModelMapper* self);

#endif /* XCHARTS_ON */
#ifdef __cplusplus
}
#endif

#endif /* XGUI_ON */

#endif /* XHXYMODELMAPPER_H */
