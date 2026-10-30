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

#if XGUI_ON
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

/**
 * @brief 选择命令位（对标 Qt 6.8 QItemSelectionModel::SelectionFlag，
 *        数值与 Qt 逐一一致：qitemselectionmodel.h:108-120）。
 *
 *        语义（对齐 Qt 6.8 qitemselectionmodel.cpp:1284-1330）：
 *        - Clear：先清空既有选区再施加后续命令（ClearAndSelect =
 *          Clear|Select）。
 *        - Select：把目标格并入选区（已选中为幂等，无变化不发射）。
 *        - Deselect：把目标格移出选区（未选中为幂等）。
 *        - Toggle：逐格翻转选中态（Qt 的 ToggleCurrent 同理）。
 *        - Current：仅表示"当前项相关的选择"（Qt 用它区分当前选择缓冲
 *          与常规选区）；本库平铺模型的选择集合与当前索引分离承载，
 *          Current 不改变集合内容，仅作为命令修饰位透传。
 *        - Rows / Columns：把目标格展开为整行 / 整列（对标 Qt 的
 *          expandSelection，qitemselectionmodel.cpp:1309-1311）。
 *        - NoUpdate：不改变任何状态（Qt 的空操作命令）。
 */
typedef enum XItemSelectionModelSelectionFlag
{
    XItemSelectionModelSelectionFlag_NoUpdate = 0x0000, /**< 不更新（空操作）。 */
    XItemSelectionModelSelectionFlag_Clear = 0x0001,    /**< 先清空既有选区。 */
    XItemSelectionModelSelectionFlag_Select = 0x0002,   /**< 并入选区。 */
    XItemSelectionModelSelectionFlag_Deselect = 0x0004, /**< 移出选区。 */
    XItemSelectionModelSelectionFlag_Toggle = 0x0008,   /**< 逐格翻转选中态。 */
    XItemSelectionModelSelectionFlag_Current = 0x0010,  /**< 当前项相关选择。 */
    XItemSelectionModelSelectionFlag_Rows = 0x0020,     /**< 展开为整行。 */
    XItemSelectionModelSelectionFlag_Columns = 0x0040,  /**< 展开为整列。 */
    XItemSelectionModelSelectionFlag_SelectCurrent = 0x0012, /**< Select|Current。 */
    XItemSelectionModelSelectionFlag_ToggleCurrent = 0x0018, /**< Toggle|Current。 */
    XItemSelectionModelSelectionFlag_ClearAndSelect = 0x0003 /**< Clear|Select。 */
} XItemSelectionModelSelectionFlag;

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

/** @brief 按选择命令施加于单个条目（对标 select(const QModelIndex&,
 *        SelectionFlags)；qitemselectionmodel.h:153）。
 *
 *        语义（对齐 Qt 6.8 qitemselectionmodel.cpp:1284-1330）：
 *        command == NoUpdate 时为空操作；Rows/Columns 先把目标格展开为
 *        整行/整列（对标 Qt expandSelection）；Clear 先清空既有选区；
 *        Select/Deselect/Toggle 逐格并入/移出/翻转（已处于目标态为
 *        幂等）。仅集合实际变化时发射一次 selectionChanged（对标 Qt
 *        单次发射）。
 *
 * @param self 目标对象。
 * @param row 行号（>=0）。
 * @param col 列号（>=0）。
 * @param command XItemSelectionModelSelectionFlag 位组合。
 * @return 集合变化返回 true。
 */
bool XItemSelectionModel_select(XItemSelectionModel* self, int row, int col,
                                int command);
/** @brief 按选择命令施加于矩形区间（对标 select(const QItemSelection&,
 *        SelectionFlags)；qitemselectionmodel.h:154）。
 *
 *        语义（对齐 Qt 6.8 qitemselectionmodel.cpp:1284-1330）：区间
 *        行列归一后逐格施加命令；Clear 先清空既有选区；
 *        Select/Deselect/Toggle 逐格并入/移出/翻转（已处于目标态为
 *        幂等）。仅集合实际变化时发射一次 selectionChanged（对标 Qt
 *        单次发射）。
 *
 * @param self 目标对象。
 * @param topRow 区间首行。
 * @param leftCol 区间首列。
 * @param bottomRow 区间末行。
 * @param rightCol 区间末列。
 * @param command XItemSelectionModelSelectionFlag 位组合。
 * @return 集合变化返回 true。
 */
bool XItemSelectionModel_selectSelection(XItemSelectionModel* self, int topRow,
                                         int leftCol, int bottomRow,
                                         int rightCol, int command);
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
/** @brief 清空全部选中（对标 QItemSelectionModel::clearSelection）。
 *
 *        语义（对齐 Qt 6.8 qitemselectionmodel.cpp:1369-1376）：仅清
 *        选择集合，**当前索引保持不变**（同 Qt）。
 *
 * @param self 目标对象。
 * @return 有变化返回 true（并发射 selectionChanged）。
 */
bool XItemSelectionModel_clearSelection(XItemSelectionModel* self);
/** @brief 清空选中与当前索引（对标 QItemSelectionModel::clear）。
 *
 *        语义（对齐 Qt 6.8 qitemselectionmodel.cpp:1335-1339）：
 *        clear = clearSelection() + clearCurrentIndex()（两者各自发射
 *        对应信号）。
 *
 * @param self 目标对象。
 * @return 选择集合有变化返回 true。
 */
bool XItemSelectionModel_clear(XItemSelectionModel* self);
/** @brief 查询是否有选中条目（对标 hasSelection）。 @param self 目标对象。 @return 有选中返回 true。 */
bool XItemSelectionModel_hasSelection(const XItemSelectionModel* self);
/** @brief 查询选中条目数。 @param self 目标对象。 @return 选中数。 */
int XItemSelectionModel_selectedCount(const XItemSelectionModel* self);

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
 *        语义（对齐 Qt 6.8 qitemselectionmodel.cpp:1389-1409）：
 *        - 索引与当前索引相同时：command != NoUpdate 则仍对该格施加
 *          选择命令（Qt 允许"保持当前格但改变选择"），随后返回，
 *          **不发射** currentChanged 族信号。
 *        - 索引变化时：先写入当前索引，再按 command 施加选择命令
 *          （NoUpdate 时不动选择集合），最后发射 currentChanged
 *          （及行/列分别变化时的 currentRowChanged/currentColumnChanged，
 *          载荷为当前/前值成对，对标 Qt QModelIndex 对）。
 *
 * @param self 目标对象。
 * @param row 行号（-1=无）。
 * @param col 列号（-1=无）。
 * @param command XItemSelectionModelSelectionFlag 位组合
 *                （NoUpdate=仅改当前索引；SelectCurrent=设当前并选中）。
 * @return 无返回值。
 */
void XItemSelectionModel_setCurrentIndex(XItemSelectionModel* self,
                                         int row, int col, int command);
/** @brief 清除当前索引（对标 QItemSelectionModel::clearCurrentIndex）。
 *
 *        语义（对齐 Qt 6.8 qitemselectionmodel.cpp:1344-1353）：当前
 *        索引复位为无效并发射 currentChanged/currentRowChanged/
 *        currentColumnChanged；原本无当前索引时为空操作。
 *
 * @param self 目标对象。
 * @return 无返回值。
 */
void XItemSelectionModel_clearCurrentIndex(XItemSelectionModel* self);
/**
 * @brief 查询当前索引（对标 QItemSelectionModel::currentIndex；
 *        qitemselectionmodel.h:129）。
 * @param self 目标对象。
 * @param outRow 输出行号（可空；无当前项置 -1）。
 * @param outCol 输出列号（可空；无当前项置 -1）。
 * @return 当前索引有效（行、列均 >=0）返回 true。
 */
bool XItemSelectionModel_currentIndex(const XItemSelectionModel* self,
                                      int* outRow, int* outCol);

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

#endif /* XGUI_ON */

#endif /* XITEMSELECTIONMODEL_H */
