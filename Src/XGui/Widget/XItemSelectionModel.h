/**
 * @file       XItemSelectionModel.h
 * @brief      XItemSelectionModel 条目选择模型（对标 Qt 6.8 QItemSelectionModel）。
 * @details    维护 (row,col) 平面索引的选中集合与当前索引；供
 *             XAbstractItemView/XTableView/XTableWidget 使用。选择以
 *             (row<<16)|col 编码存入 XVector<int>。
 * @note       模块总开关 XTABLEWIDGET_ON；XObject 派生。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XITEMSELECTIONMODEL_H
#define XITEMSELECTIONMODEL_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XObject.h"
#include "XVector.h"

#if XWIDGET_ON && XTABLEWIDGET_ON

/* ==================== 类定义 ==================== */
XCLASS_DEFINE_BEGING(XItemSelectionModel)
XCLASS_DEFINE_EXTEND_END(XItemSelectionModel, XObject)

/** @brief 选择模型对象；m_base 必须是第一个成员（嵌 XObject）。 */
typedef struct XItemSelectionModel
{
    XObject m_base;          /**< 基类成员；必须是第一个，由 XClass 管理。 */
    XVector* m_selected;     /**< 选中索引集合（(row<<16)|col，拥有）。 */
    int m_currentRow;        /**< 当前行；-1=无。 */
    int m_currentCol;        /**< 当前列；-1=无。 */
} XItemSelectionModel;

/* ==================== 生命周期 ==================== */

XVtable* XItemSelectionModel_class_init(void);
/** @brief 初始化选择模型。 @param self 目标对象；不可为 NULL。 */
void XItemSelectionModel_init(XItemSelectionModel* self);
/** @brief 使用指定内存类型创建选择模型。
 * @param memory 内存类型。 @return 新建对象；失败 NULL。 */
XItemSelectionModel* XItemSelectionModel_create_ex(XMemoryType memory);
#define XItemSelectionModel_create() \
    XItemSelectionModel_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)
#define XItemSelectionModel_deinit_base(self) \
    XClass_deinit_base((XClass*)(self))
#define XItemSelectionModel_delete_base(self) \
    XClass_delete_base((XClass*)(self))

/* ==================== 选择操作 ==================== */

/** @brief 选中/取消单个条目。
 * @param self 目标对象。
 * @param row 行号（>=0）。
 * @param col 列号（>=0）。
 * @param selected true 选中；false 取消。
 * @return 状态变化返回 true（并发射 selectionChanged）。
 */
bool XItemSelectionModel_select(XItemSelectionModel* self, int row, int col,
                                bool selected);
/** @brief 查询条目是否选中。
 * @param self 目标对象。
 * @param row 行号。
 * @param col 列号。
 * @return 选中返回 true。
 */
bool XItemSelectionModel_isSelected(const XItemSelectionModel* self,
                                    int row, int col);
/** @brief 清空全部选中。
 * @param self 目标对象。
 * @return 有变化返回 true（并发射 selectionChanged）。
 */
bool XItemSelectionModel_clear(XItemSelectionModel* self);
/** @brief 查询选中条目数。 @param self 目标对象。 @return 选中数。 */
int XItemSelectionModel_selectedCount(const XItemSelectionModel* self);
/** @brief 读取第 index 个选中条目。
 * @param self 目标对象。
 * @param index 序号（0 起）。
 * @param outRow 输出行号。
 * @param outCol 输出列号。
 * @return 成功返回 true。
 */
bool XItemSelectionModel_selectedAt(const XItemSelectionModel* self,
                                    int index, int* outRow, int* outCol);

/* ==================== 当前索引 ==================== */

/** @brief 设置当前索引（对标 QItemSelectionModel::setCurrentIndex）。
 * @param self 目标对象。
 * @param row 行号（-1=无）。
 * @param col 列号（-1=无）。
 * @return 无返回值。
 */
void XItemSelectionModel_setCurrentIndex(XItemSelectionModel* self,
                                         int row, int col);
/** @brief 查询当前行。 @param self 目标对象。 @return 行号；无返回 -1。 */
int XItemSelectionModel_currentRow(const XItemSelectionModel* self);
/** @brief 查询当前列。 @param self 目标对象。 @return 列号；无返回 -1。 */
int XItemSelectionModel_currentColumn(const XItemSelectionModel* self);

/* ==================== 信号 ==================== */

/** @brief selectionChanged() 信号（选择集合变化时发射）。 */
void* XItemSelectionModel_selectionChanged_signal(
    XItemSelectionModel* self);

#ifdef __cplusplus
}
#endif
#endif /* XWIDGET_ON && XTABLEWIDGET_ON */
#endif /* XITEMSELECTIONMODEL_H */
