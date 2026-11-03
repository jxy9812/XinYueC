/**
 * @file       XHBoxPlotModelMapper.h
 * @brief      横向盒须模型映射器（对标 Qt Charts 6.8 QHBoxPlotModelMapper）。
 * @details    继承 XBoxPlotModelMapper（对应 QHBoxPlotModelMapper :
 *             QBoxPlotModelMapper）。横向语义：firstBoxSetRow/lastBoxSetRow
 *             指定盒组所在**行**，firstColumn/columnCount 沿**列**推进。
 * @note       模块总开关 XCHARTS_ON；实现只依赖 XinYueC 抽象层，
 *             禁止调用 Win32、POSIX、Qt 或其他平台 API。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XHBOXPLOTMODELMAPPER_H
#define XHBOXPLOTMODELMAPPER_H

#if XGUI_ON
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XAbstractItemModel.h"
#include "XModelMapper_Protected.h"
#include "XBoxPlotModelMapper.h"

#if XCHARTS_ON

XCLASS_DEFINE_BEGING(XHBoxPlotModelMapper)
XCLASS_DEFINE_EXTEND_END(XHBoxPlotModelMapper, XBoxPlotModelMapper)

/** @brief QHBoxPlotModelMapper 对应实现。 */
typedef struct XHBoxPlotModelMapper
{
    XBoxPlotModelMapper m_base; /**< 基类成员；必须是第一个。 */
} XHBoxPlotModelMapper;

/** @brief 初始化类虚函数表。 @return 共享虚函数表指针。 */
XVtable* XHBoxPlotModelMapper_class_init(void);
/** @brief 初始化嵌入式映射器。 @param self 目标映射器指针。 @return 无返回值。 */
void XHBoxPlotModelMapper_init(XHBoxPlotModelMapper* self);
/** @brief 堆上创建映射器。 @param memory 内存类型。 @return 新建对象；失败 NULL。 */
XHBoxPlotModelMapper* XHBoxPlotModelMapper_create_ex(XMemoryType memory);
#define XHBoxPlotModelMapper_create() XHBoxPlotModelMapper_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

/* ==================== 专有参数（对标 QHBoxPlotModelMapper） ==================== */
/** @brief 设置首个盒组行（对标 firstBoxSetRow）。 @param self 目标映射器指针。 @param firstBoxSetRow 行/列下标。 @return 无返回值。 */
void XHBoxPlotModelMapper_setFirstBoxSetRow(XHBoxPlotModelMapper* self, int firstBoxSetRow);
/** @brief 查询首个盒组行（对标 firstBoxSetRow）。 @param self 目标映射器指针。 @return 行/列下标；未设置返回 -1。 */
int XHBoxPlotModelMapper_firstBoxSetRow(const XHBoxPlotModelMapper* self);
/** @brief 设置末个盒组行（对标 lastBoxSetRow）。 @param self 目标映射器指针。 @param lastBoxSetRow 行/列下标。 @return 无返回值。 */
void XHBoxPlotModelMapper_setLastBoxSetRow(XHBoxPlotModelMapper* self, int lastBoxSetRow);
/** @brief 查询末个盒组行（对标 lastBoxSetRow）。 @param self 目标映射器指针。 @return 行/列下标；未设置返回 -1。 */
int XHBoxPlotModelMapper_lastBoxSetRow(const XHBoxPlotModelMapper* self);
/** @brief 设置起始列（对标 firstColumn）。 @param self 目标映射器指针。 @param firstColumn 行/列下标。 @return 无返回值。 */
void XHBoxPlotModelMapper_setFirstColumn(XHBoxPlotModelMapper* self, int firstColumn);
/** @brief 查询起始列（对标 firstColumn）。 @param self 目标映射器指针。 @return 行/列下标；未设置返回 -1。 */
int XHBoxPlotModelMapper_firstColumn(const XHBoxPlotModelMapper* self);
/** @brief 设置列数量（对标 columnCount）。 @param self 目标映射器指针。 @param columnCount 行/列下标。 @return 无返回值。 */
void XHBoxPlotModelMapper_setColumnCount(XHBoxPlotModelMapper* self, int columnCount);
/** @brief 查询列数量（对标 columnCount）。 @param self 目标映射器指针。 @return 行/列下标；未设置返回 -1。 */
int XHBoxPlotModelMapper_columnCount(const XHBoxPlotModelMapper* self);

/* ==================== 信号（对标 QHBoxPlotModelMapper Q_SIGNALS） ==================== */

/** @brief seriesReplaced 信号（无载荷）。 */
void* XHBoxPlotModelMapper_seriesReplaced_signal(XHBoxPlotModelMapper* self);
/** @brief modelReplaced 信号（无载荷）。 */
void* XHBoxPlotModelMapper_modelReplaced_signal(XHBoxPlotModelMapper* self);
/** @brief FirstBoxSetRowChanged 信号（无载荷）。 */
void* XHBoxPlotModelMapper_FirstBoxSetRowChanged_signal(XHBoxPlotModelMapper* self);
/** @brief LastBoxSetRowChanged 信号（无载荷）。 */
void* XHBoxPlotModelMapper_LastBoxSetRowChanged_signal(XHBoxPlotModelMapper* self);
/** @brief FirstColumnChanged 信号（无载荷）。 */
void* XHBoxPlotModelMapper_FirstColumnChanged_signal(XHBoxPlotModelMapper* self);
/** @brief ColumnCountChanged 信号（无载荷）。 */
void* XHBoxPlotModelMapper_ColumnCountChanged_signal(XHBoxPlotModelMapper* self);

#endif /* XCHARTS_ON */
#ifdef __cplusplus
}
#endif

#endif /* XGUI_ON */

#endif /* XHBOXPLOTMODELMAPPER_H */
