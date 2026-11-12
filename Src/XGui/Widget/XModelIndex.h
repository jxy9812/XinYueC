/**
 * @file       XModelIndex.h
 * @brief      XModelIndex 模型索引值对象（对标 Qt 6.8 QModelIndex）。
 * @details    纯值类型（POD，可 memcpy/按值传递），四字段对标 Qt 6.8
 *             qabstractitemmodel.h:118-170 的 `r,c,i,m`：
 *             - m_row / m_column：行列号（无效索引为负）；
 *             - m_internalId：模型内部标识（Qt 的 quintptr），可用
 *               internalPointer/constInternalPointer 重解释为指针；
 *             - m_model：所属模型借用指针（Qt 的 const QAbstractItemModel*）。
 *
 *             Qt 的 QModelIndex 不存父链，`parent()`/`sibling()`/`data()`/
 *             `flags()`/`multiData()` 全部经 `m` 委托给模型；本类型同款：
 *             XModelIndex_parent 委托 XAbstractItemModel_parentIndex，
 *             后者从模型的树节点表解码真实父级（§B2：parent() 不再恒 -1）。
 *
 *             isValid 逐条对齐 Qt（qabstractitemmodel.h:139）：
 *             `(r >= 0) && (c >= 0) && (m != nullptr)`——internalId 不参与。
 *             相等比较（comparesEqual，qabstractitemmodel.h:145-149）比较
 *             r/c/i/m 四字段；三路比较（compareThreeWay，:150-160）按
 *             r→c→i→m 次序。
 *
 * @note       模块总开关 XTABLEWIDGET_ON；本类型是值对象，非 XObject 派生。
 *             全部函数对 self==NULL / m_model==NULL 均安全（返回无效/空/0）。
 * @warning    值语义：按值拷贝即完整副本，无所有权；m_model 是借用指针，
 *             不拥有、不释放，调用方须保证模型存活期覆盖索引使用期。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XMODELINDEX_H
#define XMODELINDEX_H

#if XGUI_ON
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XString.h"

#if XWIDGET_ON && XTABLEWIDGET_ON

/** @brief 所属模型前向声明（避免与 XAbstractItemModel.h 形成包含环；
 *         本头只以指针使用不完整类型）。 */
struct XAbstractItemModel;

/**
 * @brief      模型角色数据槽（对标 Qt 6.8 QModelRoleData，
 *             qabstractitemmodel.h:16-35；供 multiData/itemData 复用）。
 * @details    Qt 的 QModelRoleData 持有 role + QVariant data；本库模型只有
 *             文本通道，故 m_data 收敛为借用 `const XString*`。
 * @warning    m_data 是**借用**指针（内部 XString 缓冲，随单元格下次修改
 *             失效），调用方禁止释放；NULL 表示该角色无数据。
 */
typedef struct XModelRoleData
{
    int             m_role;  /**< 角色编号（XItemDataRole；Display=0/Edit=2）。 */
    const XString*  m_data;  /**< 角色数据借用指针；无数据为 NULL。 */
} XModelRoleData;

/**
 * @brief      模型索引值对象；全部字段公开可读，但一律走访问器。
 * @warning    值类型：无所有权、无析构；m_model 借用，禁止释放。
 *             新增字段一律**尾部追加**并用指定初始化器（结构体初值铁律）。
 */
typedef struct XModelIndex
{
    int                                m_row;        /**< 行号；无效为负。 */
    int                                m_column;     /**< 列号；无效为负。 */
    uintptr_t                          m_internalId; /**< 内部标识（对 Qt quintptr）。 */
    const struct XAbstractItemModel*   m_model;      /**< 所属模型借用指针。 */
} XModelIndex;

/* ==================== 构造 / 值语义 ==================== */

/** @brief 构造索引值（对标 QAbstractItemModel::createIndex 的构造语义）。
 * @param row 行号。 @param column 列号。
 * @param internalId 内部标识（0 亦可，isValid 不看此值）。
 * @param model 所属模型借用指针；可为 NULL（构造出的索引恒无效）。
 * @return 值对象（按值返回，无分配）。
 * @warning model 借用，不接管、不释放。
 */
XModelIndex XModelIndex_make(int row, int column, uintptr_t internalId,
                             const struct XAbstractItemModel* model);
/** @brief 构造无效索引（对标 QModelIndex 默认构造：r=-1,c=-1,i=0,m=null）。
 * @return 无效索引值。
 */
XModelIndex XModelIndex_invalid(void);

/* ==================== 访问器（对标 QModelIndex 内联 getter） ==================== */

/** @brief 行号（对标 QModelIndex::row）。self 为 NULL 返回 -1。 */
int XModelIndex_row(const XModelIndex* self);
/** @brief 列号（对标 QModelIndex::column）。self 为 NULL 返回 -1。 */
int XModelIndex_column(const XModelIndex* self);
/** @brief 内部标识（对标 QModelIndex::internalId）。self 为 NULL 返回 0。 */
uintptr_t XModelIndex_internalId(const XModelIndex* self);
/** @brief 内部标识重解释为指针（对标 QModelIndex::internalPointer）。
 *  @return void* 视图；self 为 NULL 返回 NULL。 @warning 借用，非独立对象。 */
void* XModelIndex_internalPointer(const XModelIndex* self);
/** @brief 内部标识重解释为 const 指针（对标 constInternalPointer）。
 *  @return const void* 视图；self 为 NULL 返回 NULL。 */
const void* XModelIndex_constInternalPointer(const XModelIndex* self);
/** @brief 所属模型（对标 QModelIndex::model）。self 为 NULL 返回 NULL。
 *  @warning 借用指针，禁止释放。 */
const struct XAbstractItemModel* XModelIndex_model(const XModelIndex* self);
/** @brief 是否有效（对标 QModelIndex::isValid：r>=0 && c>=0 && m!=NULL）。
 *  @param self 目标索引；可为 NULL（恒 false）。 */
bool XModelIndex_isValid(const XModelIndex* self);

/* ==================== 比较（对标 Qt 6.8 强序关系） ==================== */

/** @brief 相等比较（对标 comparesEqual：r/c/i/m 四字段逐一相等）。
 * @param a 左值；可为 NULL（按无效索引参与）。
 * @param b 右值；可为 NULL。
 * @return 四字段全等返回 true。
 */
bool XModelIndex_equal(const XModelIndex* a, const XModelIndex* b);
/** @brief 三路比较（对标 compareThreeWay：row→column→internalId→model）。
 * @param a 左值；可为 NULL。 @param b 右值；可为 NULL。
 * @return a<b 返回 -1、a==b 返回 0、a>b 返回 1；model 指针以 uintptr_t
 *         大小比较（同进程地址序，仅作定序用）。
 */
int XModelIndex_compare(const XModelIndex* a, const XModelIndex* b);

/* ==================== 模型委托（对标 QModelIndex::parent/sibling/data 等） ==================== */

/** @brief 父索引（对标 QModelIndex::parent，`m ? m->parent(*this) : {}`，
 *         qabstractitemmodel.h:505-506）。
 *
 *         委托 XAbstractItemModel_parentIndex 从模型树节点表解码真实父级；
 *         根级/无模型/节点未登记时返回无效索引（§B2：不再是恒 -1 的占位）。
 * @param self 目标索引；可为 NULL。
 * @return 父索引值；无效时 isValid()==false。
 */
XModelIndex XModelIndex_parent(const XModelIndex* self);
/** @brief 兄弟索引（对标 QModelIndex::sibling，qabstractitemmodel.h:508-509）。
 *
 *         Qt 经模型 `m->sibling(row,column,*this)`（= index(row,column,
 *         parent(idx))）；本值类型直接以 self 为底改 row/column、保留
 *         internalId/model，得到同父同位的值（XModelIndex 是纯值、无需
 *         模型登记即可表达，映射口径见实现注）。行列不变时返回 *self。
 * @param self 目标索引；可为 NULL（返回无效）。
 * @param row 目标行。 @param column 目标列。
 * @return 兄弟索引值。
 */
XModelIndex XModelIndex_sibling(const XModelIndex* self, int row, int column);
/** @brief 同行列变（对标 siblingAtColumn，qabstractitemmodel.h:511-512）。 */
XModelIndex XModelIndex_siblingAtColumn(const XModelIndex* self, int column);
/** @brief 同列行变（对标 siblingAtRow，qabstractitemmodel.h:514-515）。 */
XModelIndex XModelIndex_siblingAtRow(const XModelIndex* self, int row);
/** @brief 角色数据（对标 QModelIndex::data(role)，qabstractitemmodel.h:517-518）。
 *
 *         委托 XAbstractItemModel_data；本库模型 Display(0)/Edit(2) 同通道，
 *         其余角色无承载返回 NULL。
 * @param self 目标索引；可为 NULL。
 * @param role 角色编号（XItemDataRole）。
 * @return 借用 `const XString*`；无数据/越界/无模型返回 NULL。
 * @warning 借用指针，禁止释放；随单元格下次修改失效。
 */
const XString* XModelIndex_data(const XModelIndex* self, int role);
/** @brief 条目标志（对标 QModelIndex::flags，qabstractitemmodel.h:523-524）。
 *         委托 XAbstractItemModel_flags_base。self 无效/无模型返回
 *         XItemFlag_NoItemFlags(0)。 @return XItemFlag 位组合。 */
int XModelIndex_flags(const XModelIndex* self);
/** @brief 批量取角色数据（对标 QModelIndex::multiData，
 *         qabstractitemmodel.h:520-521）。委托 XAbstractItemModel_multiData。
 * @param self 目标索引；可为 NULL（各槽 m_data 置 NULL）。
 * @param span 角色数据槽数组（借用；每个 m_role 为输入、m_data 为输出）。
 * @param count 槽数量（<=0 空操作）。
 * @warning span[i].m_data 为借用指针，禁止释放。
 */
void XModelIndex_multiData(const XModelIndex* self, XModelRoleData* span,
                           int count);

/* ==================== 值操作（供范围代数构造同父角标） ==================== */

/** @brief 以 self 为底、仅改 row/column 构造值（保留 internalId/model，
 *         即保留父级解码语义）。对标 Qt split/intersected 里
 *         `model->index(r,c,parent)` 的构造诉求，但本值类型无需回调模型。
 * @param self 基底索引；可为 NULL（返回无效）。
 * @param row 新行号。 @param column 新列号。
 * @return 新索引值。
 * @note   合成索引不对应模型树中的登记节点，但 parent() 解析与基底一致，
 *         足以承载选择范围的 contains/intersects 父级比较（§B2 映射口径）。
 */
XModelIndex XModelIndex_withRowColumn(const XModelIndex* self, int row,
                                      int column);

#endif /* XWIDGET_ON && XTABLEWIDGET_ON */

#ifdef __cplusplus
}
#endif

#endif /* XGUI_ON */

#endif /* XMODELINDEX_H */
