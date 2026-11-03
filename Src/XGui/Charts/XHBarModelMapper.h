/**
 * @file       XHBarModelMapper.h
 * @brief      横向柱状模型映射器（对标 Qt Charts 6.8 QHBarModelMapper）。
 * @details    继承 XBarModelMapper（对应 QHBarModelMapper : QBarModelMapper）。
 *             横向语义：firstBarSetRow/lastBarSetRow 指定柱组所在**行**，
 *             firstColumn/columnCount 沿**列**推进。
 * @note       模块总开关 XCHARTS_ON；实现只依赖 XinYueC 抽象层，
 *             禁止调用 Win32、POSIX、Qt 或其他平台 API。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XHBARMODELMAPPER_H
#define XHBARMODELMAPPER_H

#if XGUI_ON
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XAbstractItemModel.h"
#include "XModelMapper_Protected.h"
#include "XBarModelMapper.h"

#if XCHARTS_ON

XCLASS_DEFINE_BEGING(XHBarModelMapper)
XCLASS_DEFINE_EXTEND_END(XHBarModelMapper, XBarModelMapper)

/** @brief QHBarModelMapper 对应实现。 */
typedef struct XHBarModelMapper
{
    XBarModelMapper m_base; /**< 基类成员；必须是第一个。 */
} XHBarModelMapper;

/** @brief 初始化类虚函数表。 @return 共享虚函数表指针。 */
XVtable* XHBarModelMapper_class_init(void);
/** @brief 初始化嵌入式映射器。 @param self 目标映射器指针。 @return 无返回值。 */
void XHBarModelMapper_init(XHBarModelMapper* self);
/** @brief 堆上创建映射器。 @param memory 内存类型。 @return 新建对象；失败 NULL。 */
XHBarModelMapper* XHBarModelMapper_create_ex(XMemoryType memory);
#define XHBarModelMapper_create() XHBarModelMapper_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

/* ==================== 专有参数（对标 QHBarModelMapper） ==================== */
/** @brief 设置首个柱组行（对标 firstBarSetRow）。 @param self 目标映射器指针。 @param firstBarSetRow 行/列下标。 @return 无返回值。 */
void XHBarModelMapper_setFirstBarSetRow(XHBarModelMapper* self, int firstBarSetRow);
/** @brief 查询首个柱组行（对标 firstBarSetRow）。 @param self 目标映射器指针。 @return 行/列下标；未设置返回 -1。 */
int XHBarModelMapper_firstBarSetRow(const XHBarModelMapper* self);
/** @brief 设置末个柱组行（对标 lastBarSetRow）。 @param self 目标映射器指针。 @param lastBarSetRow 行/列下标。 @return 无返回值。 */
void XHBarModelMapper_setLastBarSetRow(XHBarModelMapper* self, int lastBarSetRow);
/** @brief 查询末个柱组行（对标 lastBarSetRow）。 @param self 目标映射器指针。 @return 行/列下标；未设置返回 -1。 */
int XHBarModelMapper_lastBarSetRow(const XHBarModelMapper* self);
/** @brief 设置起始列（对标 firstColumn）。 @param self 目标映射器指针。 @param firstColumn 行/列下标。 @return 无返回值。 */
void XHBarModelMapper_setFirstColumn(XHBarModelMapper* self, int firstColumn);
/** @brief 查询起始列（对标 firstColumn）。 @param self 目标映射器指针。 @return 行/列下标；未设置返回 -1。 */
int XHBarModelMapper_firstColumn(const XHBarModelMapper* self);
/** @brief 设置列数量（对标 columnCount）。 @param self 目标映射器指针。 @param columnCount 行/列下标。 @return 无返回值。 */
void XHBarModelMapper_setColumnCount(XHBarModelMapper* self, int columnCount);
/** @brief 查询列数量（对标 columnCount）。 @param self 目标映射器指针。 @return 行/列下标；未设置返回 -1。 */
int XHBarModelMapper_columnCount(const XHBarModelMapper* self);

/* ==================== 信号（对标 QHBarModelMapper Q_SIGNALS） ==================== */

/** @brief seriesReplaced 信号（无载荷）。 */
void* XHBarModelMapper_seriesReplaced_signal(XHBarModelMapper* self);
/** @brief modelReplaced 信号（无载荷）。 */
void* XHBarModelMapper_modelReplaced_signal(XHBarModelMapper* self);
/** @brief FirstBarSetRowChanged 信号（无载荷）。 */
void* XHBarModelMapper_FirstBarSetRowChanged_signal(XHBarModelMapper* self);
/** @brief LastBarSetRowChanged 信号（无载荷）。 */
void* XHBarModelMapper_LastBarSetRowChanged_signal(XHBarModelMapper* self);
/** @brief FirstColumnChanged 信号（无载荷）。 */
void* XHBarModelMapper_FirstColumnChanged_signal(XHBarModelMapper* self);
/** @brief ColumnCountChanged 信号（无载荷）。 */
void* XHBarModelMapper_ColumnCountChanged_signal(XHBarModelMapper* self);

#endif /* XCHARTS_ON */
#ifdef __cplusplus
}
#endif

#endif /* XGUI_ON */

#endif /* XHBARMODELMAPPER_H */
