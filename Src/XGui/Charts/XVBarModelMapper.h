/**
 * @file       XVBarModelMapper.h
 * @brief      纵向柱状模型映射器（对标 Qt Charts 6.8 QVBarModelMapper）。
 * @details    继承 XBarModelMapper（对应 QVBarModelMapper : QBarModelMapper）。
 *             纵向语义：firstBarSetColumn/lastBarSetColumn 指定柱组所在**列**，
 *             firstRow/rowCount 沿**行**推进。
 * @note       模块总开关 XCHARTS_ON；实现只依赖 XinYueC 抽象层，
 *             禁止调用 Win32、POSIX、Qt 或其他平台 API。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XVBARMODELMAPPER_H
#define XVBARMODELMAPPER_H

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

XCLASS_DEFINE_BEGING(XVBarModelMapper)
XCLASS_DEFINE_EXTEND_END(XVBarModelMapper, XBarModelMapper)

/** @brief QVBarModelMapper 对应实现。 */
typedef struct XVBarModelMapper
{
    XBarModelMapper m_base; /**< 基类成员；必须是第一个。 */
} XVBarModelMapper;

/** @brief 初始化类虚函数表。 @return 共享虚函数表指针。 */
XVtable* XVBarModelMapper_class_init(void);
/** @brief 初始化嵌入式映射器。 @param self 目标映射器指针。 @return 无返回值。 */
void XVBarModelMapper_init(XVBarModelMapper* self);
/** @brief 堆上创建映射器。 @param memory 内存类型。 @return 新建对象；失败 NULL。 */
XVBarModelMapper* XVBarModelMapper_create_ex(XMemoryType memory);
#define XVBarModelMapper_create() XVBarModelMapper_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

/* ==================== 专有参数（对标 QVBarModelMapper） ==================== */
/** @brief 设置首个柱组列（对标 firstBarSetColumn）。 @param self 目标映射器指针。 @param firstBarSetColumn 行/列下标。 @return 无返回值。 */
void XVBarModelMapper_setFirstBarSetColumn(XVBarModelMapper* self, int firstBarSetColumn);
/** @brief 查询首个柱组列（对标 firstBarSetColumn）。 @param self 目标映射器指针。 @return 行/列下标；未设置返回 -1。 */
int XVBarModelMapper_firstBarSetColumn(const XVBarModelMapper* self);
/** @brief 设置末个柱组列（对标 lastBarSetColumn）。 @param self 目标映射器指针。 @param lastBarSetColumn 行/列下标。 @return 无返回值。 */
void XVBarModelMapper_setLastBarSetColumn(XVBarModelMapper* self, int lastBarSetColumn);
/** @brief 查询末个柱组列（对标 lastBarSetColumn）。 @param self 目标映射器指针。 @return 行/列下标；未设置返回 -1。 */
int XVBarModelMapper_lastBarSetColumn(const XVBarModelMapper* self);
/** @brief 设置起始行（对标 firstRow）。 @param self 目标映射器指针。 @param firstRow 行/列下标。 @return 无返回值。 */
void XVBarModelMapper_setFirstRow(XVBarModelMapper* self, int firstRow);
/** @brief 查询起始行（对标 firstRow）。 @param self 目标映射器指针。 @return 行/列下标；未设置返回 -1。 */
int XVBarModelMapper_firstRow(const XVBarModelMapper* self);
/** @brief 设置行数量（对标 rowCount）。 @param self 目标映射器指针。 @param rowCount 行/列下标。 @return 无返回值。 */
void XVBarModelMapper_setRowCount(XVBarModelMapper* self, int rowCount);
/** @brief 查询行数量（对标 rowCount）。 @param self 目标映射器指针。 @return 行/列下标；未设置返回 -1。 */
int XVBarModelMapper_rowCount(const XVBarModelMapper* self);

/* ==================== 信号（对标 QVBarModelMapper Q_SIGNALS） ==================== */

/** @brief seriesReplaced 信号（无载荷）。 */
void* XVBarModelMapper_seriesReplaced_signal(XVBarModelMapper* self);
/** @brief modelReplaced 信号（无载荷）。 */
void* XVBarModelMapper_modelReplaced_signal(XVBarModelMapper* self);
/** @brief FirstBarSetColumnChanged 信号（无载荷）。 */
void* XVBarModelMapper_FirstBarSetColumnChanged_signal(XVBarModelMapper* self);
/** @brief LastBarSetColumnChanged 信号（无载荷）。 */
void* XVBarModelMapper_LastBarSetColumnChanged_signal(XVBarModelMapper* self);
/** @brief FirstRowChanged 信号（无载荷）。 */
void* XVBarModelMapper_FirstRowChanged_signal(XVBarModelMapper* self);
/** @brief RowCountChanged 信号（无载荷）。 */
void* XVBarModelMapper_RowCountChanged_signal(XVBarModelMapper* self);

#endif /* XCHARTS_ON */
#ifdef __cplusplus
}
#endif

#endif /* XGUI_ON */

#endif /* XVBARMODELMAPPER_H */
