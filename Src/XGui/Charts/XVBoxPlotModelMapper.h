/**
 * @file       XVBoxPlotModelMapper.h
 * @brief      纵向盒须模型映射器（对标 Qt Charts 6.8 QVBoxPlotModelMapper）。
 * @details    继承 XBoxPlotModelMapper（对应 QVBoxPlotModelMapper :
 *             QBoxPlotModelMapper）。纵向语义：firstBoxSetColumn/
 *             lastBoxSetColumn 指定盒组所在**列**，firstRow/rowCount 沿**行**推进。
 * @note       模块总开关 XCHARTS_ON；实现只依赖 XinYueC 抽象层，
 *             禁止调用 Win32、POSIX、Qt 或其他平台 API。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XVBOXPLOTMODELMAPPER_H
#define XVBOXPLOTMODELMAPPER_H

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

XCLASS_DEFINE_BEGING(XVBoxPlotModelMapper)
XCLASS_DEFINE_EXTEND_END(XVBoxPlotModelMapper, XBoxPlotModelMapper)

/** @brief QVBoxPlotModelMapper 对应实现。 */
typedef struct XVBoxPlotModelMapper
{
    XBoxPlotModelMapper m_base; /**< 基类成员；必须是第一个。 */
} XVBoxPlotModelMapper;

/** @brief 初始化类虚函数表。 @return 共享虚函数表指针。 */
XVtable* XVBoxPlotModelMapper_class_init(void);
/** @brief 初始化嵌入式映射器。 @param self 目标映射器指针。 @return 无返回值。 */
void XVBoxPlotModelMapper_init(XVBoxPlotModelMapper* self);
/** @brief 堆上创建映射器。 @param memory 内存类型。 @return 新建对象；失败 NULL。 */
XVBoxPlotModelMapper* XVBoxPlotModelMapper_create_ex(XMemoryType memory);
#define XVBoxPlotModelMapper_create() XVBoxPlotModelMapper_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

/* ==================== 专有参数（对标 QVBoxPlotModelMapper） ==================== */
/** @brief 设置首个盒组列（对标 firstBoxSetColumn）。 @param self 目标映射器指针。 @param firstBoxSetColumn 行/列下标。 @return 无返回值。 */
void XVBoxPlotModelMapper_setFirstBoxSetColumn(XVBoxPlotModelMapper* self, int firstBoxSetColumn);
/** @brief 查询首个盒组列（对标 firstBoxSetColumn）。 @param self 目标映射器指针。 @return 行/列下标；未设置返回 -1。 */
int XVBoxPlotModelMapper_firstBoxSetColumn(const XVBoxPlotModelMapper* self);
/** @brief 设置末个盒组列（对标 lastBoxSetColumn）。 @param self 目标映射器指针。 @param lastBoxSetColumn 行/列下标。 @return 无返回值。 */
void XVBoxPlotModelMapper_setLastBoxSetColumn(XVBoxPlotModelMapper* self, int lastBoxSetColumn);
/** @brief 查询末个盒组列（对标 lastBoxSetColumn）。 @param self 目标映射器指针。 @return 行/列下标；未设置返回 -1。 */
int XVBoxPlotModelMapper_lastBoxSetColumn(const XVBoxPlotModelMapper* self);
/** @brief 设置起始行（对标 firstRow）。 @param self 目标映射器指针。 @param firstRow 行/列下标。 @return 无返回值。 */
void XVBoxPlotModelMapper_setFirstRow(XVBoxPlotModelMapper* self, int firstRow);
/** @brief 查询起始行（对标 firstRow）。 @param self 目标映射器指针。 @return 行/列下标；未设置返回 -1。 */
int XVBoxPlotModelMapper_firstRow(const XVBoxPlotModelMapper* self);
/** @brief 设置行数量（对标 rowCount）。 @param self 目标映射器指针。 @param rowCount 行/列下标。 @return 无返回值。 */
void XVBoxPlotModelMapper_setRowCount(XVBoxPlotModelMapper* self, int rowCount);
/** @brief 查询行数量（对标 rowCount）。 @param self 目标映射器指针。 @return 行/列下标；未设置返回 -1。 */
int XVBoxPlotModelMapper_rowCount(const XVBoxPlotModelMapper* self);

/* ==================== 信号（对标 QVBoxPlotModelMapper Q_SIGNALS） ==================== */

/** @brief seriesReplaced 信号（无载荷）。 */
void* XVBoxPlotModelMapper_seriesReplaced_signal(XVBoxPlotModelMapper* self);
/** @brief modelReplaced 信号（无载荷）。 */
void* XVBoxPlotModelMapper_modelReplaced_signal(XVBoxPlotModelMapper* self);
/** @brief FirstBoxSetColumnChanged 信号（无载荷）。 */
void* XVBoxPlotModelMapper_FirstBoxSetColumnChanged_signal(XVBoxPlotModelMapper* self);
/** @brief LastBoxSetColumnChanged 信号（无载荷）。 */
void* XVBoxPlotModelMapper_LastBoxSetColumnChanged_signal(XVBoxPlotModelMapper* self);
/** @brief FirstRowChanged 信号（无载荷）。 */
void* XVBoxPlotModelMapper_FirstRowChanged_signal(XVBoxPlotModelMapper* self);
/** @brief RowCountChanged 信号（无载荷）。 */
void* XVBoxPlotModelMapper_RowCountChanged_signal(XVBoxPlotModelMapper* self);

#endif /* XCHARTS_ON */
#ifdef __cplusplus
}
#endif

#endif /* XGUI_ON */

#endif /* XVBOXPLOTMODELMAPPER_H */
