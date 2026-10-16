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
#include "XAbstractItemModel.h"
#include "XVector.h"

#if XWIDGET_ON && XTABLEWIDGET_ON

/* ==================== 类定义 ==================== */
XCLASS_DEFINE_BEGING(XItemSelectionModel)
XCLASS_DEFINE_EXTEND_END(XItemSelectionModel, XObject)

/** @brief 选择模型对象；m_base 必须是第一个成员（嵌 XObject）。 */
typedef struct XItemSelectionModel
{
    XObject m_base;          /**< 基类成员；必须是第一个，由 XClass 管理。 */
    /* ---- 指针组（8 字节对齐，置顶） ---- */
    XVector* m_selected;     /**< 选中索引集合（(row<<16)|col，拥有）。 */
    XAbstractItemModel* m_model; /**< 绑定模型（借用；对标
                                      QItemSelectionModel::model()；可 NULL）。 */
    /* ---- 2 字节组 ---- */
    int16_t m_currentRow;    /**< 当前行；-1=无（负值哨兵，故有符号）。 */
    int16_t m_currentCol;    /**< 当前列；-1=无。 */
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
/** @brief 选中矩形区间（对标 select(QItemSelectionRange(topLeft,
 *        bottomRight), Select)；本库以四 int 承载 QItemSelection 区间）。
 *
 *        语义（对齐 Qt Select 命令）：区间行列归一后全格置为选中
 *        （已选中的格保持不变）；仅集合实际变化时发射一次
 *        selectionChanged（对标 Qt 单次发射）。
 *
 * @param self 目标对象。
 * @param topRow 区间首行。
 * @param leftCol 区间首列。
 * @param bottomRow 区间末行。
 * @param rightCol 区间末列。
 * @return 集合变化返回 true。
 */
bool XItemSelectionModel_selectRange(XItemSelectionModel* self, int topRow,
                                     int leftCol, int bottomRow,
                                     int rightCol);
/** @brief 选中整行（对标 selectRow(row)；需绑定模型取列数，见
 *        XItemSelectionModel_setModel；未绑定或行号越界返回 false）。
 * @param self 目标对象。
 * @param row 行号（>=0）。
 * @return 集合变化返回 true（并发射 selectionChanged）。
 */
bool XItemSelectionModel_selectRow(XItemSelectionModel* self, int row);
/** @brief 选中整列（对标 selectColumn(column)；需绑定模型取行数；
 *        未绑定或列号越界返回 false）。
 * @param self 目标对象。
 * @param column 列号（>=0）。
 * @return 集合变化返回 true（并发射 selectionChanged）。
 */
bool XItemSelectionModel_selectColumn(XItemSelectionModel* self, int column);
/** @brief 查询条目是否选中。
 * @param self 目标对象。
 * @param row 行号。
 * @param col 列号。
 * @return 选中返回 true。
 */
bool XItemSelectionModel_isSelected(const XItemSelectionModel* self,
                                    int row, int col);
/** @brief 清空全部选中（对标 QItemSelectionModel::clearSelection；
 *        当前索引保持不变，同 Qt）。 @param self 目标对象。
 * @return 有变化返回 true（并发射 selectionChanged）。
 */
bool XItemSelectionModel_clearSelection(XItemSelectionModel* self);
/** @brief 清空选中与当前索引（对标 QItemSelectionModel::clear 的
 *        本库既有收敛名；@note Qt 的 clear 同时复位当前索引，本函数
 *        沿用既有行为保留当前索引——Qt clearSelection 语义，既有调用
 *        方（视图 clear 路径）依赖此行为，不强行对齐）。
 * @param self 目标对象。
 * @return 有变化返回 true（并发射 selectionChanged）。
 */
bool XItemSelectionModel_clear(XItemSelectionModel* self);
/** @brief 查询是否有选中条目（对标 hasSelection）。 @param self 目标对象。 @return 有选中返回 true。 */
bool XItemSelectionModel_hasSelection(const XItemSelectionModel* self);
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

/* ==================== 模型绑定（对标 model()/setModel()） ==================== */

/** @brief 绑定模型（借用；对标 QItemSelectionModel::setModel）。
 *
 *        selectRow/selectColumn 经绑定的模型读取行/列数；视图
 *        （XAbstractItemView）在 setModel/setSelectionModel 时负责
 *        同步两侧绑定（对标 Qt 视图创建选择模型时传入模型的链路）。
 *
 * @param self 目标对象。
 * @param model 模型借用指针；可为 NULL（解除绑定）。
 * @return 无返回值。
 */
void XItemSelectionModel_setModel(XItemSelectionModel* self,
                                  XAbstractItemModel* model);
/** @brief 读取绑定模型（对标 model()）。 @param self 目标对象。 @return 模型借用指针；未绑定 NULL。 */
XAbstractItemModel* XItemSelectionModel_model(const XItemSelectionModel* self);

/* ==================== 当前索引 ==================== */

/** @brief 设置当前索引（对标 QItemSelectionModel::setCurrentIndex）。
 *
 *        索引变化时发射 currentChanged（及行/列分别变化时的
 *        currentRowChanged/currentColumnChanged，载荷为当前/前值成对，
 *        对标 Qt QModelIndex 对）；索引未变化为无操作（同 Qt）。
 *
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
/** @brief currentChanged(row,col,prevRow,prevCol) 信号（当前索引变化时
 *        发射；对标 QItemSelectionModel::currentChanged(current,
 *        previous)，Qt QModelIndex 对以四 int 平面承载）。 */
void* XItemSelectionModel_currentChanged_signal(XItemSelectionModel* self,
                                                int row, int col, int prevRow,
                                                int prevCol);
/** @brief currentRowChanged(row,prevRow) 信号（当前行变化时发射；对标
 *        QItemSelectionModel::currentRowChanged）。 */
void* XItemSelectionModel_currentRowChanged_signal(XItemSelectionModel* self,
                                                   int row, int prevRow);
/** @brief currentColumnChanged(col,prevCol) 信号（当前列变化时发射；
 *        对标 QItemSelectionModel::currentColumnChanged）。 */
void* XItemSelectionModel_currentColumnChanged_signal(
    XItemSelectionModel* self, int col, int prevCol);

#ifdef __cplusplus
}
#endif
#endif /* XWIDGET_ON && XTABLEWIDGET_ON */
#endif /* XITEMSELECTIONMODEL_H */
