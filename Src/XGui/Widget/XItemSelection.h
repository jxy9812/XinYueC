/**
 * @file       XItemSelection.h
 * @brief      XItemSelectionRange / XItemSelection 值对象（对标 Qt 6.8
 *             QItemSelectionRange / QItemSelection）。
 * @details    QItemSelection 在 Qt 里是 QList<QItemSelectionRange> 的派生，
 *             承载若干「矩形选择范围」并做 range 代数（merge/split/
 *             intersects/intersected/contains/indexes）。本库同款：
 *             - XItemSelectionRange：两个 XModelIndex（tl/br）组成的矩形，
 *               纯值对象，字段公开、无所有权；
 *             - XItemSelection：栈可构造的 range 列表（内嵌 XVector 存
 *               XItemSelectionRange 值），须显式 `_init`/`_deinit`（或
 *               `_create`/`_delete` 堆版）管理存储。
 *
 *             取证：Qt 6.8 qitemselectionmodel.h:17-85（range 声明）、
 *             :180-196（selection 声明）；qitemselectionmodel.cpp:183-214
 *             （intersects/intersected）、:296-325（isEmpty/indexes）、
 *             :375-555（select/contains/indexes/merge/split）。
 *
 * @note       模块总开关 XTABLEWIDGET_ON。
 * @warning    XItemSelectionRange 是值对象（POD 语义的无所有权结构）；
 *             XItemSelection 持有 XVector 存储，**必须**配对 init/deinit。
 *             两个类型的 XModelIndex 成员 m_model 均为借用指针。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XITEMSELECTION_H
#define XITEMSELECTION_H

#if XGUI_ON
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XVector.h"
#include "XModelIndex.h"

#if XWIDGET_ON && XTABLEWIDGET_ON

/* ==================== XItemSelectionRange（值对象） ==================== */

/**
 * @brief      选择范围矩形（对标 QItemSelectionRange，
 *             qitemselectionmodel.h:17-85）。
 * @details    以左上 tl / 右下 br 两个 XModelIndex 表达；top/left 取 tl，
 *             bottom/right 取 br，parent() 取 tl.parent()。isValid() 逐条
 *             对齐 Qt（:66-70）：两端索引有效、同父、top<=bottom、
 *             left<=right。
 * @warning    值对象：无所有权、无析构；按值拷贝即完整副本。内部
 *             XModelIndex 的 m_model 借用，禁止释放。
 */
typedef struct XItemSelectionRange
{
    XModelIndex m_topLeft;      /**< 左上角索引（借用其 model）。 */
    XModelIndex m_bottomRight;  /**< 右下角索引（借用其 model）。 */
} XItemSelectionRange;

/** @brief 构造空（无效）范围（对标 QItemSelectionRange 默认构造）。 */
XItemSelectionRange XItemSelectionRange_invalid(void);
/** @brief 由两个角标构造范围（对标 QItemSelectionRange(topL,bottomR)）。
 * @param topLeft 左上索引（按值借用其 model 指针）。
 * @param bottomRight 右下索引。
 * @return 范围值。 @warning 不接管任何指针。
 */
XItemSelectionRange XItemSelectionRange_make(XModelIndex topLeft,
                                              XModelIndex bottomRight);
/** @brief 由单个索引构造 1×1 范围（对标 explicit QItemSelectionRange(index)）。 */
XItemSelectionRange XItemSelectionRange_fromIndex(XModelIndex index);
/** @brief 由行列四边构造（tl 的 model/internalId 决定父级与所属模型）。 */
XItemSelectionRange XItemSelectionRange_makeRect(XModelIndex base, int top,
                                                  int left, int bottom,
                                                  int right);

/* ---- 访问器（对标 QItemSelectionRange inline getter，:27-44） ---- */
int XItemSelectionRange_top(const XItemSelectionRange* self);
int XItemSelectionRange_left(const XItemSelectionRange* self);
int XItemSelectionRange_bottom(const XItemSelectionRange* self);
int XItemSelectionRange_right(const XItemSelectionRange* self);
int XItemSelectionRange_width(const XItemSelectionRange* self);
int XItemSelectionRange_height(const XItemSelectionRange* self);
XModelIndex XItemSelectionRange_topLeft(const XItemSelectionRange* self);
XModelIndex XItemSelectionRange_bottomRight(const XItemSelectionRange* self);
XModelIndex XItemSelectionRange_parent(const XItemSelectionRange* self);
const struct XAbstractItemModel* XItemSelectionRange_model(
    const XItemSelectionRange* self);

/** @brief 范围是否有效（对标 isValid，qitemselectionmodel.h:66-70）。
 * @return 两角标有效 + 同父 + top<=bottom + left<=right 时 true。
 */
bool XItemSelectionRange_isValid(const XItemSelectionRange* self);
/** @brief 是否为空（对标 isEmpty，qitemselectionmodel.cpp:296-312）。
 *
 *        Qt 语义：无效/无模型 ⇒ true；否则逐格查 flags 的可选/可用位，
 *        任一格可选且可用 ⇒ false（否则 true）。本库模型只有文本通道，
 *        flags 缺省 Selectable|Enabled（有效索引）⇒ 等价于「有效范围
 *        即非空」；仍逐格走 flags 以保持派生模型重载 flags 的语义。
 * @return 无可选且可用条目返回 true。
 */
bool XItemSelectionRange_isEmpty(const XItemSelectionRange* self);
/** @brief 是否包含某索引（对标 contains(index)，:53-58）：同父且四边内。 */
bool XItemSelectionRange_contains(const XItemSelectionRange* self,
                                  XModelIndex index);
/** @brief 是否包含某格（对标 contains(row,col,parent)，:60-64）。
 * @param parentIndex 期望的父索引（借用）；NULL 按无效父级处理。 */
bool XItemSelectionRange_containsCell(const XItemSelectionRange* self, int row,
                                      int column,
                                      const XModelIndex* parentIndex);
/** @brief 是否与另一范围相交（对标 intersects，qitemselectionmodel.cpp:183-193）。 */
bool XItemSelectionRange_intersects(const XItemSelectionRange* self,
                                    const XItemSelectionRange* other);
/** @brief 交集范围（对标 intersected，qitemselectionmodel.cpp:204-214）。
 *        不同模型/不同父级/不相交时返回无效范围。
 */
XItemSelectionRange XItemSelectionRange_intersected(
    const XItemSelectionRange* self, const XItemSelectionRange* other);
/** @brief 相等比较（对标 comparesEqual：两角标分别相等）。 */
bool XItemSelectionRange_equal(const XItemSelectionRange* a,
                               const XItemSelectionRange* b);
/** @brief 枚举范围内全部索引（对标 indexes，qitemselectionmodel.cpp:315-318
 *        → indexesFromRange:263-278）。
 *
 *        逐行（top..bottom）逐列（left..right）输出 XModelIndex 值；与 Qt
 *        同款只枚举 flags 含 Selectable|Enabled 的格（isSelectableAndEnabled
 *        判据，:257）——派生模型重载 flags 后禁选格不进结果（2026-11-15
 *        返工对齐）。只写 min(count,maxCount) 项，返回总数（可能大于
 *        maxCount）。
 * @param out 输出数组（借用；可为 NULL 表示只统计）。
 * @param maxCount 输出容量（<=0 只统计）。
 * @return 索引总数。
 */
int XItemSelectionRange_indexes(const XItemSelectionRange* self,
                                XModelIndex* out, int maxCount);

/* ==================== XItemSelection（range 列表） ==================== */

/**
 * @brief      选择集合（对标 QItemSelection : QList<QItemSelectionRange>）。
 * @details    内嵌 XVector 存 XItemSelectionRange 值（POD，无元素回调需求）；
 *             须显式 init/deinit。
 * @warning    m_ranges 由本对象拥有；调用方不得手工改字段。
 *             用完必须 XItemSelection_deinit（堆版 XItemSelection_delete）。
 */
typedef struct XItemSelection
{
    XVector m_ranges;      /**< XItemSelectionRange 值向量（对象拥有）。 */
    int     m_memoryType;  /**< 堆版 create_ex 记录的 XMemoryType（供 delete
                                 以同一分配器释放；栈版 init 置 SYSTEM，
                                 delete 不使用）。尾部追加字段。 */
} XItemSelection;

/** @brief 初始化空选择集合（对标 QItemSelection 默认构造）。
 * @param self 目标对象；不可为 NULL。
 * @return 成功 true；向量初始化失败返回 false。
 * @warning 必须配对 XItemSelection_deinit。
 */
bool XItemSelection_init(XItemSelection* self);
/** @brief 释放选择集合存储（不释放对象本身；栈对象用）。
 * @param self 目标对象；可为 NULL。
 */
void XItemSelection_deinit(XItemSelection* self);
/** @brief 堆版创建（对标 new QItemSelection）。
 * @param memory 内存类型。
 * @return 新建对象；失败 NULL。 @warning 用 XItemSelection_delete 释放。
 */
XItemSelection* XItemSelection_create_ex(XMemoryType memory);
#define XItemSelection_create() \
    XItemSelection_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)
/** @brief 删除堆版对象（先 deinit 再释放对象内存）。 */
void XItemSelection_delete(XItemSelection* self);
/** @brief 由单矩形构造堆版集合（对标 QItemSelection(topLeft,bottomRight)）。
 * @return 新建对象；失败 NULL。 @warning 用 XItemSelection_delete 释放。
 */
XItemSelection* XItemSelection_createFromRect(XModelIndex topLeft,
                                               XModelIndex bottomRight);

/** @brief 范围数（对标 QList::size）。self 为 NULL 返回 0。 */
int XItemSelection_count(const XItemSelection* self);
/** @brief 是否为空集合（对标 QList::isEmpty；指 range 列表为空，
 *         与 XItemSelectionRange_isEmpty 的「无可选条目」不同）。 */
bool XItemSelection_isEmpty(const XItemSelection* self);
/** @brief 按序号取范围（对标 QList::at；越界返回无效范围值）。
 * @warning 返回的是**值副本**，非借用指针。
 */
XItemSelectionRange XItemSelection_at(const XItemSelection* self, int index);
/** @brief 追加一个范围（对标 QList::append）。
 * @return 成功 true。 @warning 入参按值拷贝，不接管其 model 指针。
 */
bool XItemSelection_append(XItemSelection* self, XItemSelectionRange range);
/** @brief 移除第 index 个范围（对标 QList::removeAt；越界空操作）。 */
void XItemSelection_removeAt(XItemSelection* self, int index);
/** @brief 清空全部范围（对标 QList::clear，保留存储）。 */
void XItemSelection_clear(XItemSelection* self);

/** @brief 追加矩形范围（对标 QItemSelection::select，
 *         qitemselectionmodel.cpp:387-411）。
 *
 *         Qt 语义：两角标必须有效、同模型、同父；无效/异父/异模型仅告警
 *         不追加；当 topLeft 在 bottomRight 之下/之右时，先归一四边再追加。
 * @param self 目标集合。 @param topLeft 左上索引。 @param bottomRight 右下索引。
 * @return 实际追加返回 true。
 */
bool XItemSelection_select(XItemSelection* self, XModelIndex topLeft,
                           XModelIndex bottomRight);
/** @brief 是否包含某索引（对标 QItemSelection::contains，:415-424）。
 *         与 Qt 同款先以索引自身 flags（Selectable|Enabled，
 *         isSelectableAndEnabled）短路：不可选/不可用直接 false，不扫
 *         范围；否则逐范围查任一 contains 即 true。
 */
bool XItemSelection_contains(const XItemSelection* self, XModelIndex index);
/** @brief 枚举全部索引（对标 QItemSelection::indexes，:430-433）。
 *        按范围顺序、范围内按行主序追加；不保唯一（Qt 同：直接拼接）。
 * @param out 输出数组（借用；可为 NULL 表示只统计）。
 * @param maxCount 输出容量（<=0 只统计）。
 * @return 索引总数。
 */
int XItemSelection_indexes(const XItemSelection* self, XModelIndex* out,
                           int maxCount);
/** @brief 合并集合（对标 QItemSelection::merge，:453-497）。
 *
 *        Qt 语义：只受理 Select/Deselect/Toggle 命令；收集 other 各 range
 *        与本集合各 range 的交集，用 split 把本集合（及 Toggle 时的
 *        newSelection）中相交的 range 切成不相交碎片，最后按命令决定
 *        是否并入 newSelection（Deselect 不并入）。结果保证无重叠 range。
 *
 * @param self 目标集合（就地修改）。 @param other 待合并集合（借用）。
 * @param command XItemSelectionModelSelectionFlag 位组合。
 * @return 实际执行了合并返回 true（other 空或命令无 Select/Deselect/Toggle
 *         返回 false，对齐 Qt 的早退）。
 * @note   命令位数值与 XItemSelectionModel.h 同名枚举一致（本头不反向
 *         依赖选择模型，故以 int 承载、字面量注释）。
 */
bool XItemSelection_merge(XItemSelection* self, const XItemSelection* other,
                          int command);
/** @brief 从 range 中扣掉 other 范围、把剩余矩形碎片追加到 result
 *        （对标 QItemSelection::split 静态函数，:507-555）。
 *
 *        Qt 语义：不同父/不同模型 ⇒ 直接返回；否则按 other 的上/下/左/右
 *        四边切出最多 4 块矩形 append 到 result（顺序：上、下、左、右）。
 * @param range 被切范围。 @param other 扣除范围。
 * @param result 输出集合（追加；不可为 NULL）。
 */
void XItemSelection_split(XItemSelectionRange range,
                          XItemSelectionRange other,
                          XItemSelection* result);

/** @brief 集合相等（range 数相同且逐项相等，顺序敏感）。
 * @note   本库扩展（Qt 的 QItemSelection 未定义 operator==）；供回归锁用。
 */
bool XItemSelection_equal(const XItemSelection* a, const XItemSelection* b);

#endif /* XWIDGET_ON && XTABLEWIDGET_ON */

#ifdef __cplusplus
}
#endif

#endif /* XGUI_ON */

#endif /* XITEMSELECTION_H */
