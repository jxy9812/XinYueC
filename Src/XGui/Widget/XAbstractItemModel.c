/**
 * @file       XAbstractItemModel.c
 * @brief      XAbstractItemModel 条目模型实现（内存二维模型）。
 * @author     XinYueC 团队
 ******************************************************************************/
#if XGUI_ON

#include "XAbstractItemModel.h"

#include "XAlgorithm.h"
#include "XMemory.h"
#include "XStringUtils.h"
#include "XVarList.h"
#include "XVector.h"
#include "XEventType.h"
#include "XMap.h"
#include "XDataStream.h"
#include "XMimeData.h"
#include "XModelIndex.h"
#include "XItemSelection.h"
#include "XStringList.h"
#include "XAbstractItemView.h"
#include "XCompare.h"
#include "XPlatformDrag.h"
#include <limits.h>

#if XWIDGET_ON && XTABLEWIDGET_ON

/* ==================== 内部工具 ==================== */

/** @brief 单元格/表头元素深拷贝（XString* 元素的复制回调）。 */
static void xaim_cellCopy(void* data, const void* sData)
{
    XString* const* src = (XString* const*)sData;
    *(XString**)data = *src ? XString_create_copy(*src) : NULL;
}

/** @brief 单元格/表头元素释放（XString* 元素的清理回调）。 */
static void xaim_cellDeinit(void* data)
{
    XString** cell = (XString**)data;
    if (cell && *cell) {
        XClassDelete(*cell);
        *cell = NULL;
    }
}

/** @brief 初始化对象拥有的 XString* 元素向量（单元格/表头共用）。 */
static void xaim_vectorInit(XVector* vec)
{
    XVector_init(vec, sizeof(XString*), false);
    XContainerSetDataCopyMethod(vec, xaim_cellCopy);
    XContainerSetDataDeinitMethod(vec, xaim_cellDeinit);
}

/** @brief 定位单元格槽位（行主序；越界或 self 为空返回 NULL）。 */
static XString** xaim_cellSlot(const XAbstractItemModel* self, int row,
                               int col)
{
    if (!self || row < 0 || row >= self->m_rows ||
        col < 0 || col >= self->m_cols)
        return NULL;
    return (XString**)XVector_at_base(&self->m_cells,
                                      (int64_t)row * self->m_cols + col);
}

/** @brief 定位表头槽位（orientation 0=列头 1=行头；越界返回 NULL）。 */
static XString** xaim_headerSlot(const XAbstractItemModel* self,
                                 int section, int orientation)
{
    const XVector* vec;
    if (!self) return NULL;
    vec = (orientation == 0) ? &self->m_hHeader : &self->m_vHeader;
    if (section < 0 || (size_t)section >= XVector_size_base(vec))
        return NULL;
    return (XString**)XVector_at_base(vec, section);
}

/** @brief 单元格文本变化涉及的角色位掩码：DisplayRole(0)|EditRole(2)。
 *  @note 本库模型 Display/Edit 同通道（同 Qt 缺省模型）；role 编号见
 *        XAbstractItemView.h 的 XItemDataRole（与 Qt::ItemDataRole 同值，
 *        本文件不反向依赖视图层故以字面量注释）。 */
#define XAIM_TEXT_ROLES_MASK 0x5

/** @brief 发射 dataChanged 区间信号（Qt 6.8 五参数形状：
 *         topLeft(row,col) + bottomRight(row,col) + roles 位掩码）。 */
static void xaim_emitDataChanged(XAbstractItemModel* self, int topRow,
                                 int topCol, int bottomRow, int bottomCol,
                                 int roles)
{
    XVarList* args = XVarList_Create(XVar(int, topRow), XVar(int, topCol),
                                     XVar(int, bottomRow),
                                     XVar(int, bottomCol), XVar(int, roles));
    if (!args) return;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self,
                           (size_t)XAbstractItemModel_dataChanged_signal,
                           args, NULL, NULL, XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

/** @brief 发射双 int 结构信号（rows/columns 族；载荷 first,last 闭区间）。 */
static void xaim_emitRows(XAbstractItemModel* self, size_t signal,
                          int first, int last)
{
    XVarList* args = XVarList_Create(XVar(int, first), XVar(int, last));
    if (!args) return;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

/** @brief 发射三 int 移动信号（first,last,destination）。 */
static void xaim_emitMoved(XAbstractItemModel* self, size_t signal, int first,
                           int last, int destination)
{
    XVarList* args = XVarList_Create(XVar(int, first), XVar(int, last),
                                     XVar(int, destination));
    if (!args) return;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

static void xaim_emitReset(XAbstractItemModel* self)
{
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self,
                           (size_t)XAbstractItemModel_modelReset_signal,
                           NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
    }
}

/** @brief 发射无载荷信号（layoutChanged 等共用）。 */
static void xaim_emitVoid(XAbstractItemModel* self, size_t signal)
{
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, NULL, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    }
}

/** @brief 发射单 int 载荷信号（layout 族的 XLayoutChangeHint）。
 *  @note self 为 NULL 时**先短路**——XSignal(XObject_signalIdOf) 以单参
 *        NULL 调用信号函数采集 id，hint 形参此时是寄存器/栈残值。 */
static void xaim_emitHint(XAbstractItemModel* self, size_t signal, int hint)
{
    XVarList* args;
    if (!self || !((XObject*)self)->m_signalSlot) return;
    args = XVarList_Create(XVar(int, hint));
    if (!args) return;
    XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                       XEVENT_PRIORITY_NORMAL);
}

static void xaim_emitHeaderDataChanged(XAbstractItemModel* self,
                                       int orientation, int first, int last)
{
    XVarList* args = XVarList_Create(XVar(int, orientation), XVar(int, first),
                                     XVar(int, last));
    if (!args) return;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self,
                           (size_t)XAbstractItemModel_headerDataChanged_signal,
                           args, NULL, NULL, XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

/* ==================== 虚函数实现 ==================== */

static void VXAbstractItemModel_deinit(XAbstractItemModel* self)
{
    if (!self) return;
    /* 元素（XString*）经注册回调逐个释放后回收向量存储。 */
    XClassDeinit(&self->m_cells);
    XClassDeinit(&self->m_hHeader);
    XClassDeinit(&self->m_vHeader);
    XClassDeinit(&self->m_nodes);   /* §B2 树节点表（元素为 POD 值，无回调）。 */
    self->m_rows = 0;
    self->m_cols = 0;
    XClass_Deinit_Parent(XObject, (XObject*)self);
}

static void VXAbstractItemModel_copy(XAbstractItemModel* self,
                                     const XAbstractItemModel* other)
{
    int r, c;
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XAbstractItemModel_init(self);
    XClass_Parent(XObject, EXClass_Copy,
                  void(*)(XObject*, const XObject*))((XObject*)self,
                                                     (const XObject*)other);
    /* 清空 self 现有数据。 */
    VXAbstractItemModel_deinit(self);
    XAbstractItemModel_init(self);
    if (other->m_rows > 0 || other->m_cols > 0) {
        XAbstractItemModel_setDimension(self, other->m_rows, other->m_cols);
        for (r = 0; r < other->m_rows; ++r) {
            for (c = 0; c < other->m_cols; ++c) {
                XString** cell = xaim_cellSlot(other, r, c);
                if (cell && *cell)
                    XAbstractItemModel_setData(self, r, c, *cell);
            }
            {
                XString** head = xaim_headerSlot(other, r, 1);
                if (head && *head)
                    XAbstractItemModel_setHeaderData(self, r, 1, *head);
            }
        }
        for (c = 0; c < other->m_cols; ++c) {
            XString** head = xaim_headerSlot(other, c, 0);
            if (head && *head)
                XAbstractItemModel_setHeaderData(self, c, 0, *head);
        }
    }
}

static void VXAbstractItemModel_move(XAbstractItemModel* self,
                                     XAbstractItemModel* other)
{
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XAbstractItemModel_init(self);
    XClass_Parent(XObject, EXClass_Move,
                  void(*)(XObject*, XObject*))((XObject*)self,
                                               (XObject*)other);
    VXAbstractItemModel_deinit(self);
    XAbstractItemModel_init(self);
    /* 容器整体移动（转移底层存储；other 留为已初始化空容器）。 */
    XClassMove(&self->m_cells, &other->m_cells);
    XClassMove(&self->m_hHeader, &other->m_hHeader);
    XClassMove(&self->m_vHeader, &other->m_vHeader);
    XClassMove(&self->m_nodes, &other->m_nodes);  /* §B2 树节点表一并转移。 */
    self->m_rows = other->m_rows;
    self->m_cols = other->m_cols;
    other->m_rows = 0;
    other->m_cols = 0;
    /* other 的 m_nodes 已被移走且未重建；补一次重建（other 留空容器，符合
       XClassMove 后的契约：other 的向量内部指针已转向 self 的原存储）。 */
    XVector_init(&other->m_nodes, sizeof(XAbstractItemNode), false);
}

/* ==================== 类初始化 ==================== */

static int VXAbstractItemModel_flags(const XAbstractItemModel* self, int row,
                                     int col);
static int VXAbstractItemModel_supportedDropActions(
    const XAbstractItemModel* self);
static int VXAbstractItemModel_supportedDragActions(
    const XAbstractItemModel* self);

XVtable* XAbstractItemModel_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XAbstractItemModel)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXAbstractItemModel_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXAbstractItemModel_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXAbstractItemModel_move);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractItemModel_Flags,
                             VXAbstractItemModel_flags);
    /* 2026-11-15 B6R 返工：supportedDropActions/supportedDragActions 从
     * 普通函数提为虚槽（Qt 侧两函数即 virtual，qabstractitemmodel.h:298/
     * :294）——普通函数版派生模型无法重载给出 Move/Link 位 ⇒ InternalMove
     * 端到端不可达。缺省业务挂表、分派入口走 _base（同 Flags 契约）。
     * 挂表随 #if XMIMEDATA_ON 段裁剪（枚举槽位在类定义处恒在，static
     * 缺省实现与 _base 入口都在下方 XMIMEDATA_ON 段内；开关关闭时两槽
     * 不挂表——XClassGetVirtualFunc 查不到槽位即返回 NULL，而两槽的
     * 消费者（视图拖放段）整段不存在 ⇒ 无悬空调用。 */
#if XMIMEDATA_ON
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractItemModel_SupportedDropActions,
                             VXAbstractItemModel_supportedDropActions);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractItemModel_SupportedDragActions,
                             VXAbstractItemModel_supportedDragActions);
#endif /* XMIMEDATA_ON */
    return XVTABLE_DEFAULT;
}

void XAbstractItemModel_init(XAbstractItemModel* self)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XObject_init((XObject*)self);
    XClassSetVtable(self, XAbstractItemModel);
    xaim_vectorInit(&self->m_cells);
    xaim_vectorInit(&self->m_hHeader);
    xaim_vectorInit(&self->m_vHeader);
    /* §B2 树节点表：元素 XAbstractItemNode 是 POD（无字符串/指针成员），
       不需要 copy/deinit 回调——但**必须**在此初始化：上面 XMemset 把
       整个对象清零，m_nodes 的 vtable 会成 NULL，后续任何
       XVector_* 调用都会打印「XClassGetVtable(this_vector)是NULL」并
       直接失败（appendTreeNode 恒 -1，树形 API 整体失效）。 */
    XVector_init(&self->m_nodes, sizeof(XAbstractItemNode), false);
}

XAbstractItemModel* XAbstractItemModel_create_ex(XMemoryType memory)
{
    XAbstractItemModel* self =
        (XAbstractItemModel*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XAbstractItemModel_init(self);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== §B2 树形层级 ==================== */

static int xaim_nodeParentId(const XAbstractItemModel* self,
                              const XModelIndex* parent)
{
    /* 从父索引的 internalId 取父节点下标；parent 无效/无模型 ⇒ -1（根）。 */
    if (!self || !parent || !XModelIndex_isValid(parent))
        return -1;
    return (int)XModelIndex_internalId(parent);
}

static int xaim_findNode(const XAbstractItemModel* self, int parentId,
                          int row, int col)
{
    /* 在 m_nodes 中查 parentId 匹配、行匹配、列匹配（平铺根 parentId=-1）
       的节点，返回其 internalId；未命中返回 -1。 */
    int64_t i;
    int64_t n;
    if (!self) return -1;
    n = XVector_size_base(&self->m_nodes);
    for (i = 0; i < n; ++i) {
        const XAbstractItemNode* node =
            (const XAbstractItemNode*)XVector_at_base(&self->m_nodes, i);
        if (node && node->m_parentId == parentId &&
            node->m_row == row && node->m_col == col)
            return node->m_internalId;
    }
    return -1;
}

static int xaim_countChildren(const XAbstractItemModel* self, int parentId)
{
    int64_t i;
    int64_t n;
    int count = 0;
    if (!self) return 0;
    n = XVector_size_base(&self->m_nodes);
    for (i = 0; i < n; ++i) {
        const XAbstractItemNode* node =
            (const XAbstractItemNode*)XVector_at_base(&self->m_nodes, i);
        if (node && node->m_parentId == parentId) ++count;
    }
    return count;
}

int XAbstractItemModel_appendTreeNode(XAbstractItemModel* self,
                                      const XModelIndex* parent, int row,
                                      int col)
{
    XAbstractItemNode node;
    int parentId;
    if (!self) return -1;
    parentId = xaim_nodeParentId(self, parent);
    node.m_row = row;
    node.m_col = col;
    node.m_parentId = parentId;
    node.m_internalId = (int)XVector_size_base(&self->m_nodes);
    if (!XVector_push_back_1_base(&self->m_nodes, &node))
        return -1;
    return node.m_internalId;
}

int XAbstractItemModel_removeTreeNode(XAbstractItemModel* self,
                                      int internalId)
{
    int64_t i;
    int64_t n;
    int removed = 0;
    uint8_t* doomed;
    if (!self || internalId < 0) return 0;
    n = (int64_t)XVector_size_base(&self->m_nodes);
    if ((int64_t)internalId >= n) return 0;
    /* 待删集合 = 目标节点 + 其全部后代（沿 parentId 求传递闭包）。
       用位图而非「边删边比较 parentId」：后者在目标被删后 parentId 判据
       会命中 m_parentId == -1 的**无关根级兄弟节点**（曾被误删）。 */
    doomed = (uint8_t*)XCalloc_System((size_t)n, 1);
    if (!doomed) return 0;
    doomed[internalId] = 1;
    {
        int changed;
        do {
            changed = 0;
            for (i = 0; i < n; ++i) {
                const XAbstractItemNode* node =
                    (const XAbstractItemNode*)XVector_at_base(&self->m_nodes, i);
                if (!node || doomed[i]) continue;
                if (node->m_parentId >= 0 && (int64_t)node->m_parentId < n &&
                    doomed[node->m_parentId]) {
                    doomed[i] = 1;
                    changed = 1;
                }
            }
        } while (changed);
    }
    /* 从大到小删除（大下标先删，小下标不受左移影响），每次删完把
       internalId / parentId 大于被删下标的节点各减 1——维持
       「internalId == 向量下标」不变式（parentIndex/indexTree/
       rowCountOfId 全靠它当下标用；此前漏了重排，删一个兄弟后
       其余节点的父链全部解错位）。 */
    for (i = n - 1; i >= 0; --i) {
        int64_t j;
        if (!doomed[i]) continue;
        XVector_remove_base(&self->m_nodes, i, 1);
        ++removed;
        for (j = 0; j < (int64_t)XVector_size_base(&self->m_nodes); ++j) {
            XAbstractItemNode* node =
                (XAbstractItemNode*)XVector_at_base(&self->m_nodes, j);
            if (!node) continue;
            if (node->m_internalId > (int)i) node->m_internalId -= 1;
            if (node->m_parentId > (int)i) node->m_parentId -= 1;
        }
    }
    XFree_System(doomed);
    return removed;
}

XModelIndex XAbstractItemModel_parentIndex(const XAbstractItemModel* self,
                                            const XModelIndex* index)
{
    const XAbstractItemNode* node;
    int parentId;
    /* 平铺模型无树节点 ⇒ 恒无效（根级子项的父是无效 QModelIndex，对齐 Qt）。 */
    if (!self || !index || !XModelIndex_isValid(index))
        return XModelIndex_invalid();
    if (XVector_size_base(&self->m_nodes) == 0)
        return XModelIndex_invalid();
    {
        int64_t selfId = (int64_t)XModelIndex_internalId(index);
        if (selfId < 0 || selfId >= (int64_t)XVector_size_base(&self->m_nodes))
            return XModelIndex_invalid();
        node = (const XAbstractItemNode*)XVector_at_base(&self->m_nodes,
                                                          selfId);
    }
    if (!node) return XModelIndex_invalid();
    parentId = node->m_parentId;
    if (parentId < 0) return XModelIndex_invalid();  /* 根 */
    if ((int64_t)parentId >= (int64_t)XVector_size_base(&self->m_nodes))
        return XModelIndex_invalid();
    {
        const XAbstractItemNode* pnode =
            (const XAbstractItemNode*)XVector_at_base(&self->m_nodes,
                                                       (int64_t)parentId);
        if (!pnode) return XModelIndex_invalid();
        return XModelIndex_make(pnode->m_row, pnode->m_col,
                                (uintptr_t)pnode->m_internalId, self);
    }
}

XModelIndex XAbstractItemModel_indexTree(const XAbstractItemModel* self, int row,
                                         int column, const XModelIndex* parent)
{
    int parentId;
    int id;
    if (!self) return XModelIndex_invalid();
    if (row < 0 || column < 0) return XModelIndex_invalid();
    parentId = xaim_nodeParentId(self, parent);
    id = xaim_findNode(self, parentId, row, column);
    if (id < 0 || (int64_t)id >= (int64_t)XVector_size_base(&self->m_nodes))
        return XModelIndex_invalid();
    {
        const XAbstractItemNode* node =
            (const XAbstractItemNode*)XVector_at_base(&self->m_nodes,
                                                       (int64_t)id);
        if (!node) return XModelIndex_invalid();
        return XModelIndex_make(node->m_row, node->m_col,
                                (uintptr_t)node->m_internalId, self);
    }
}

int XAbstractItemModel_parent(const XAbstractItemModel* self, int row, int col)
{
    /* 整型 parent 口径：从 (row,col) 反查节点父级；平铺模型无节点 ⇒ -1。 */
    int id;
    const XAbstractItemNode* node;
    if (!self) return -1;
    id = xaim_findNode(self, -1, row, col);
    if (id < 0) return -1;
    node = (const XAbstractItemNode*)XVector_at_base(&self->m_nodes,
                                                      (int64_t)id);
    if (!node) return -1;
    return node->m_parentId;
}

int XAbstractItemModel_rowCountAt(const XAbstractItemModel* self,
                                   const XModelIndex* parent)
{
    int parentId;
    if (!self) return 0;
    parentId = xaim_nodeParentId(self, parent);
    if (parentId < 0) return self->m_rows;  /* 根 = 顶层行数 */
    return xaim_countChildren(self, parentId);
}

int XAbstractItemModel_rowCountOfId(const XAbstractItemModel* self,
                                    int parentId)
{
    if (!self) return 0;
    if (parentId < 0) return self->m_rows;
    return xaim_countChildren(self, parentId);
}

int XAbstractItemModel_columnCountAt(const XAbstractItemModel* self,
                                      const XModelIndex* parent)
{
    int parentId;
    if (!self) return 0;
    parentId = xaim_nodeParentId(self, parent);
    if (parentId < 0) return self->m_cols;  /* 根 = 顶层列数 */
    /* 层级父下：有子节点取首子列号（本库节点列号恒 0），否则 0；仅维度占位。 */
    if (xaim_countChildren(self, parentId) > 0)
        return 1;
    return 0;
}

bool XAbstractItemModel_hasChildrenAtIndex(const XAbstractItemModel* self,
                                           const XModelIndex* parent)
{
    if (!self) return false;
    if (!parent || !XModelIndex_isValid(parent))
        return self->m_rows > 0 && self->m_cols > 0;  /* 根：退化 hasChildren */
    return xaim_countChildren(self, xaim_nodeParentId(self, parent)) > 0;
}

/* ==================== 维度 ==================== */

int XAbstractItemModel_rowCount(const XAbstractItemModel* self)
{ return self ? self->m_rows : 0; }

int XAbstractItemModel_columnCount(const XAbstractItemModel* self)
{ return self ? self->m_cols : 0; }

/* ==================== 索引契约（对标 QAbstractItemModel） ==================== */

bool XAbstractItemModel_hasIndex(const XAbstractItemModel* self, int row,
                                 int column)
{
    /* 对齐 Qt 6.8 hasIndex（qabstractitemmodel.h:270 + 实测）：
       row/column 任一为负即无效；否则必须分别小于行数/列数。 */
    if (!self || row < 0 || column < 0) return false;
    return row < self->m_rows && column < self->m_cols;
}

bool XAbstractItemModel_index(const XAbstractItemModel* self, int row,
                              int column, int* outRow, int* outCol)
{
    if (outRow) *outRow = -1;
    if (outCol) *outCol = -1;
    if (!XAbstractItemModel_hasIndex(self, row, column)) return false;
    if (outRow) *outRow = row;
    if (outCol) *outCol = column;
    return true;
}

bool XAbstractItemModel_sibling(const XAbstractItemModel* self, int row,
                                int column, int fromRow, int fromCol,
                                int* outRow, int* outCol)
{
    /* 平面模型父恒为根：sibling = index(row, column, parent(idx))，与
       index() 同源；来源索引只作合法性说明（不参与计算）。 */
    (void)fromRow;
    (void)fromCol;
    return XAbstractItemModel_index(self, row, column, outRow, outCol);
}

bool XAbstractItemModel_checkIndex(const XAbstractItemModel* self, int row,
                                   int col, int options)
{
    /* 逐条对齐 Qt 6.8 QAbstractItemModel::checkIndex
       （qabstractitemmodel.cpp:3586-3641）：
       ① 无效索引（Qt 的 !index.isValid()，本库以 row<0 || col<0 表达）
          只有在声明了 IndexIsValid 时才算违规；
       ② 有效索引的行/列仍须非负（Qt 的 has negative row/column 分支）；
       ③ 未声明 DoNotUseParent 时：ParentIsInvalid 要求父索引无效，
          随后按父索引的 rowCount/columnCount 做区间校验——Qt 的区间
          校验整段位于该 if 内，故 DoNotUseParent 会**连同区间检查一起
          跳过**（不是只跳父链）。本库平铺模型的 parent 恒为根（-1），
          故 ③ 的父链条件恒满足、区间即 self 的行/列数。 */
    if (row < 0 || col < 0) {
        /* ① 无效索引：Qt 无条件信任（除非调用方声明 IndexIsValid）。 */
        if (options & XCheckIndexOption_IndexIsValid) return false;
        return true;
    }
    if (!(options & XCheckIndexOption_DoNotUseParent)) {
        int parentRow;
        if (options & XCheckIndexOption_ParentIsInvalid) {
            parentRow = XAbstractItemModel_parent(self, row, col);
            if (parentRow != -1) return false;
        }
        if (!self) return false; /* 无模型 ⇒ 无可校验的行/列数。 */
        if (row >= XAbstractItemModel_rowCount(self)) return false;
        if (col >= XAbstractItemModel_columnCount(self)) return false;
    }
    return true;
}

void XAbstractItemModel_setDimension(XAbstractItemModel* self,
                                     int rows, int cols)
{
    size_t oldCells;
    int oldRows;
    int oldCols;
    XVector newCells;
    bool cellsRebuilt = false;
    if (!self || rows < 0 || cols < 0) return;
    oldRows = self->m_rows;
    oldCols = self->m_cols;
    if (rows == oldRows && cols == oldCols) return;
    oldCells = XVector_size_base(&self->m_cells);
    /* 单元格：行主序一维向量，尺寸恒等于 rows*cols。列数变化时**必须按新
       行距重排**——直接 resize 会把 row1col0 留在下标 1（新行距下那已是
       row0col1），扩列与缩列都会错位（历史注记「扩列错位已消除」与实现
       不符，此处按 (row,col) 逐格搬迁修正）。缩容裁剪的格经元素回调释放，
       新增格补 NULL。表头向量按新维度裁剪/补位。任一分配失败即保持原
       维度（已成功的表头 resize 尽量回滚）。 */
    if (cols != oldCols && oldCells > 0) {
        int r;
        int c;
        xaim_vectorInit(&newCells);
        if (!XVector_resize_base(&newCells, (size_t)rows * (size_t)cols)) {
            XClassDeinit(&newCells);
            return;
        }
        for (r = 0; r < rows && r < oldRows; ++r) {
            for (c = 0; c < cols && c < oldCols; ++c) {
                XString** src = xaim_cellSlot(self, r, c);
                if (!src || !*src) continue;
                /* 指针搬迁（所有权转移）：旧槽置 NULL，旧向量析构只释放
                   被裁掉的格，不会二次释放。 */
                *(XString**)XVector_at_base(
                    &newCells, (int64_t)r * cols + c) = *src;
                *src = NULL;
            }
        }
        cellsRebuilt = true;
    }
    if (!XVector_resize_base(&self->m_hHeader, (size_t)cols)) {
        if (cellsRebuilt) XClassDeinit(&newCells);
        return;
    }
    if (!XVector_resize_base(&self->m_vHeader, (size_t)rows)) {
        if (cellsRebuilt) XClassDeinit(&newCells);
        XVector_resize_base(&self->m_hHeader, (size_t)oldCols);
        return;
    }
    if (cellsRebuilt) {
        /* 旧表（含被裁格格）经析构回调释放后换成重排好的新表。 */
        XClassDeinit(&self->m_cells);
        xaim_vectorInit(&self->m_cells);
        XClassMove(&self->m_cells, &newCells);
        XClassDeinit(&newCells);
    } else if (!XVector_resize_base(&self->m_cells,
                                    (size_t)rows * (size_t)cols)) {
        XVector_resize_base(&self->m_vHeader, (size_t)oldRows);
        XVector_resize_base(&self->m_hHeader, (size_t)oldCols);
        return;
    }
    /* 结构变化协议（对标 Qt begin/end 两段式）：先发 aboutTo（闭区间），
       改维度与数据后发事后信号。行/列各自的区间按旧维度解释：
       增行 = [oldRows, rows-1]，减行 = [rows, oldRows-1]（Qt 的删除区间
       也是闭区间）。 */
    if (rows > oldRows)
        xaim_emitRows(self,
                      (size_t)XAbstractItemModel_rowsAboutToBeInserted_signal,
                      oldRows, rows - 1);
    else if (rows < oldRows)
        xaim_emitRows(self,
                      (size_t)XAbstractItemModel_rowsAboutToBeRemoved_signal,
                      rows, oldRows - 1);
    if (cols > oldCols)
        xaim_emitRows(
            self,
            (size_t)XAbstractItemModel_columnsAboutToBeInserted_signal,
            oldCols, cols - 1);
    else if (cols < oldCols)
        xaim_emitRows(
            self,
            (size_t)XAbstractItemModel_columnsAboutToBeRemoved_signal,
            cols, oldCols - 1);
    self->m_rows = rows;
    self->m_cols = cols;
    if (rows > oldRows)
        xaim_emitRows(self, (size_t)XAbstractItemModel_rowsInserted_signal,
                      oldRows, rows - 1);
    else if (rows < oldRows)
        xaim_emitRows(self, (size_t)XAbstractItemModel_rowsRemoved_signal,
                      rows, oldRows - 1);
    /* 列数变化同样要发结构变化信号（对标 Qt columnsInserted/
       columnsRemoved）：此前 setDimension 只发行族，视图挂在
       columnsInserted 上的刷新槽收不到通知。 */
    if (cols > oldCols)
        xaim_emitRows(self, (size_t)XAbstractItemModel_columnsInserted_signal,
                      oldCols, cols - 1);
    else if (cols < oldCols)
        xaim_emitRows(self, (size_t)XAbstractItemModel_columnsRemoved_signal,
                      cols, oldCols - 1);
}

/* 【冗余 API 清理 2026-11-11】setRowCount/setColumnCount 纯包装已删除，
 * 维度变化统一走 XAbstractItemModel_setDimension（见头文件说明）。 */

/* ==================== 数据 ==================== */

const XString* XAbstractItemModel_data(const XAbstractItemModel* self,
                                       int row, int col)
{
    XString** slot = xaim_cellSlot(self, row, col);
    return (slot && *slot) ? *slot : NULL;
}

const char* XAbstractItemModel_data_2(const XAbstractItemModel* self,
                                      int row, int col)
{
    const XString* s;
    s = XAbstractItemModel_data(self, row, col);
    return s ? XString_toUtf8(s) : "";
}

bool XAbstractItemModel_setData(XAbstractItemModel* self, int row, int col,
                                const XString* value)
{
    XString** slot;
    XString* repl;
    if (!self) return false;
    slot = xaim_cellSlot(self, row, col);
    if (!slot) return false;
    /* 同对象守卫：setItemData 的入表值是单元格 XString 的**借用指针**
     * （itemData 注：借用、禁止释放），逐 role 写回时 Display/Edit 两表
     * 项指向同一对象——首条 setData 在此处早退后对象仍存活，后续表项
     * 同样命中守卫；若无此守卫，首条的 XClassDelete(*slot) 会释放
     * value 本体，下一条的 XString_create_copy(value) 即堆 UAF
     * （ASan heap-use-after-free，qt-align dataRolesAndMime 实证）。 */
    if (value && value == *slot) return true;
    repl = value ? XString_create_copy(value) : NULL;
    if (value && !repl) return false;
    if (*slot) XClassDelete(*slot);
    *slot = repl;
    /* 单格写入：Qt 五参数 dataChanged 形状（左上==右下），roles 取
       Display|Edit（本模型两角色同通道）。 */
    xaim_emitDataChanged(self, row, col, row, col, XAIM_TEXT_ROLES_MASK);
    return true;
}

bool XAbstractItemModel_setData_2(XAbstractItemModel* self, int row, int col,
                                  const char* value)
{
    XString* tmp = NULL;
    bool ok;
    if (value) {
        tmp = XString_create_utf8(value);
        if (!tmp) return false;
    }
    ok = XAbstractItemModel_setData(self, row, col, tmp);
    if (tmp) XClassDelete(tmp);
    return ok;
}

/* ==================== §B2 多角色数据 / MIME ==================== */

const XString* XAbstractItemModel_dataRole(const XAbstractItemModel* self,
                                          int row, int col, int role)
{
    /* 本内存模型 Display(0)/Edit(2) 同通道走 m_cells；其余角色无承载。 */
    if (role != XItemDataRole_DisplayRole && role != XItemDataRole_EditRole)
        return NULL;
    return XAbstractItemModel_data(self, row, col);
}

XMap* XAbstractItemModel_itemData(const XAbstractItemModel* self, int row,
                                  int col)
{
    XMap* map;
    int role;
    /* 对标 Qt itemData：遍历 role < UserRole 逐一 data，有效者入表。本内存
       模型仅 Display/Edit 同通道有效（对应 0/2）。map 的 value 为借用 XString*，
       禁止释放（见头文件注）。 */
    map = XMap_create(sizeof(int), sizeof(const XString*), Xint_compare);
    if (!map) return NULL;
    for (role = 0; role < XItemDataRole_UserRole; ++role) {
        const XString* v = XAbstractItemModel_dataRole(self, row, col, role);
        if (v) {
            XMap_insert_base(map, &role, &v);
        }
    }
    return map;
}

void XAbstractItemModel_multiData(const XAbstractItemModel* self, int row,
                                  int col, XModelRoleData* span, int count)
{
    int i;
    if (!span || count <= 0) return;
    for (i = 0; i < count; ++i) span[i].m_data = NULL;
    if (!self) return;
    for (i = 0; i < count; ++i)
        span[i].m_data = XAbstractItemModel_dataRole(self, row, col,
                                                     span[i].m_role);
}

bool XAbstractItemModel_setItemData(XAbstractItemModel* self, int row,
                                    int col, const XMap* map)
{
    XMap_iterator it;
    if (!self || !map) return false;
    /* 逐一 setData；任一无承载角色（非 Display/Edit）⇒ 失败。 */
    it = XMap_begin((XMap*)map);
    while (!XMap_iterator_isEnd(&it)) {
        XPair* pair = XMap_iterator_data(&it);
        const int* key = pair ? (const int*)XPair_first(pair) : NULL;
        const XString* const* val =
            pair ? (const XString* const*)XPair_second(pair) : NULL;
        int role = key ? *key : -1;
        if (role != XItemDataRole_DisplayRole &&
            role != XItemDataRole_EditRole)
            return false;  /* Qt 基类缺省：无承载角色 setData 失败 ⇒ 整体 false。 */
        if (!XAbstractItemModel_setData(self, row, col,
                                         val && *val ? *val : NULL))
            return false;
        XMap_iterator_add((XMap*)map, &it);
    }
    return true;
}

bool XAbstractItemModel_clearItemData(XAbstractItemModel* self, int row,
                                      int col)
{
    XString* nullStr = NULL;
    if (!self) return false;
    /* 等价清单元格文本（本内存模型无多角色存储）。 */
    return XAbstractItemModel_setData(self, row, col, nullStr);
}

XStringList* XAbstractItemModel_mimeTypes(const XAbstractItemModel* self)
{
    XStringList* list;
    (void)self;
    /* 对标 Qt 缺省：单元素 application/x-qabstractitemmodeldatalist。 */
    list = XStringList_create();
    if (!list) return NULL;
    XStringList_push_back_utf8(list,
        "application/x-qabstractitemmodeldatalist");
    return list;
}

#if XMIMEDATA_ON   /* 与 .h 声明段同域裁剪（XMimeData 仅此开关下有类型定义） */

XMimeData* XAbstractItemModel_mimeData(const XAbstractItemModel* self,
                                       const int* rows, const int* cols,
                                       int nRows)
{
    XStringList* types;
    XByteArray* encoded;
    XMimeData* data;
    XDataStream streamOnStack;      /* 栈版：create_ex/delete 组合实测每轮泄漏
                                       72B（sizeof(XDataStream)）；栈版
                                       init/deinit 与框架其它用法一致且零泄漏。 */
    XDataStream* stream = &streamOnStack;
    int i;
    int n;
    if (!self || nRows <= 0) return NULL;
    types = XAbstractItemModel_mimeTypes(self);
    /* XStringList ≠ XVector（前者的 XVector 是内嵌成员）：_base 族宏直通
       XVector_*，第一参必须显式取内嵌向量（XImagePluginRegistry.c:897
       同款 (XVector*) 转换）——直传 XStringList* 是指针类型不匹配
       （2026-11-15 返工 ② 收口）。 */
    if (!types || XStringList_size_base((const XVector*)types) == 0) {
        if (types) XClassDelete(types);
        return NULL;
    }
    XClassDelete(types);
    /* 编码：逐索引写 (row, col, itemData)。itemData 本内存模型仅 Display/Edit，
       以 XMap<int,const XString*> 承载（与 XAbstractItemModel_itemData 同款），
       编码为 role/文本对（role < UserRole 且有效者）。 */
    encoded = XByteArray_create();
    if (!encoded) return NULL;
    XDataStream_init_array(stream, encoded);
    n = nRows;
    for (i = 0; i < n; ++i) {
        int r = rows ? rows[i] : -1;
        int c = cols ? cols[i] : -1;
        /* 负行/负列 = 「不可拖」占位（本仓库 XAbstractItemView.c:3159 就用
           (-1,-1) 表达该索引未启用拖放）。Qt 的 encodeData 由调用方只挑
           可拖索引，本库接口一次收全量数组 ⇒ 必须在编码侧剔除，否则流里
           会留下 (-1,-1) 垃圾三元组（并在对端 drop 时触发负下标）。 */
        if (r < 0 || c < 0) continue;
        XDataStream_writeInt32(stream, r);
        XDataStream_writeInt32(stream, c);
        {
            XMap* map = XAbstractItemModel_itemData(self, r, c);
            if (map) {
                XMap_iterator it = XMap_begin(map);
                int role;
                /* 项数（角色数 < UserRole=256，远小于 2^31，写 int 即可）。 */
                XDataStream_writeInt32(stream, (int)XMap_size_base(map));
                while (!XMap_iterator_isEnd(&it)) {
                    XPair* pair = XMap_iterator_data(&it);
                    const int* key =
                        pair ? (const int*)XPair_first(pair) : NULL;
                    const XString* const* val =
                        pair ? (const XString* const*)XPair_second(pair) : NULL;
                    role = key ? *key : -1;
                    XDataStream_writeInt32(stream, role);
                    XDataStream_writeString(stream,
                        val && *val ? *val : NULL);
                    XMap_iterator_add(map, &it);
                }
                XClassDelete(map);
            } else {
                XDataStream_writeInt32(stream, 0);
            }
        }
    }
    XClassDeinit((XClass*)stream);   /* 栈版流：只反初始化，不释放结构体。 */
    data = XMimeData_create();
    if (!data) { XClassDelete(encoded); return NULL; }
    {
        /* 写进自定义字段（二进制透明通道）。 */
        const uint8_t* bytes =
            (const uint8_t*)XByteArray_data(encoded);
        int len = (int)XByteArray_size_base(encoded);
        XMimeData_setData_bytes(data,
            "application/x-qabstractitemmodeldatalist", bytes, len);
    }
    XClassDelete(encoded);
    return data;
}

bool XAbstractItemModel_canDropMimeData(const XAbstractItemModel* self,
                                        const XMimeData* data, int action,
                                        int row, int column,
                                        const XModelIndex* parent)
{
    XStringList* types;
    int i;
    int n;
    (void)row; (void)column; (void)parent;  /* 基类不做位置语义判定。 */
    if (!self || !data) return false;
    /* action 必须是 supportedDropActions 之一（对标 Qt 基类校验；
     * 2026-11-15 B6R 返工改走虚槽入口——派生模型重载后本校验同步生效）。 */
    if (!(action & XAbstractItemModel_supportedDropActions_base(self)))
        return false;
    types = XAbstractItemModel_mimeTypes(self);
    if (!types) return false;
    /* 同 mimeData：XStringList 第一参显式取内嵌 XVector（指针类型收口）。 */
    n = (int)XStringList_size_base((const XVector*)types);
    for (i = 0; i < n; ++i) {
        const XString* fmt =
            (const XString*)XStringList_at_base((const XVector*)types, i);
        const char* utf8 = fmt ? XString_toUtf8(fmt) : "";
        if (XMimeData_hasFormat(data, utf8)) { XClassDelete(types); return true; }
    }
    XClassDelete(types);
    return false;
}

bool XAbstractItemModel_dropMimeData(XAbstractItemModel* self,
                                     const XMimeData* data, int action,
                                     int row, int column,
                                     const XModelIndex* parent)
{
    int i;
    int top = INT_MAX;
    int left = INT_MAX;
    int bottom = 0;
    int right = 0;
    int n;
    int rows[256];
    int cols[256];
    XMap* datas[256];
    int dragRowCount;
    int dragColumnCount;
    int* rowsToInsert;
    XByteArray* blob;
    XByteArray* ro;
    int blobLen;
    XDataStream streamOnStack;      /* 栈版理由同 mimeData（create_ex/delete 每轮
                                       泄漏 sizeof(XDataStream)）。 */
    XDataStream* stream = &streamOnStack;
    int colCount;
    (void)parent;
    if (!self || !data) return false;
    /* 仅受理 Copy/Move（与 Qt 同）；action 不支持 ⇒ false。 */
    if (!(action == XPlatformDragAction_Copy ||
          action == XPlatformDragAction_Move))
        return false;
    /* 落点归一（对齐 Qt qabstractitemmodel.cpp:2226-2231）：row 超出父级
       行数 ⇒ 夹到行数；row == -1 ⇒ 行数；column == -1 ⇒ 0。这一段必须在
       解码前做，否则 row=-1 会一路传到 insertRows 的负数下标。 */
    {
        int parentRows = XAbstractItemModel_rowCountAt(self, parent);
        if (row > parentRows) row = parentRows;
        if (row == -1) row = parentRows;
        if (column == -1) column = 0;
    }
    /* 解码 application/x-qabstractitemmodeldatalist（对标 Qt decodeData）。 */
    blob = XMimeData_data_bytes(data,
        "application/x-qabstractitemmodeldatalist");
    if (!blob) return false;
    blobLen = (int)XByteArray_size_base(blob);
    /* 只读流借用**存活到解码结束**的数组：ro 必须在 XClassDelete(stream)
       之后才释放（init_arrayReadOnly 只记借用指针，XDataStream.c:82）。 */
    ro = XByteArray_create_with_data(
        (const char*)XByteArray_data(blob), (size_t)blobLen);
    if (!ro) { XClassDelete(blob); return false; }
    XDataStream_init_arrayReadOnly(stream, ro);
    n = 0;
    while (!XDataStream_atEnd(stream)) {
        int r = 0, c = 0, count = 0;
        if (!XDataStream_readInt32(stream, &r)) break;
        if (!XDataStream_readInt32(stream, &c)) break;
        if (!XDataStream_readInt32(stream, &count)) break;
        /* 负行/负列是「不可拖」占位（本仓库 XAbstractItemView.c:3159 就
           这么写），不进范围统计也不参与落点计算，直接丢弃该条。 */
        if (r < 0 || c < 0 || count < 0) {
            int skip;
            for (skip = 0; skip < count; ++skip) {
                int skipRole = 0;
                XString* junk;
                if (!XDataStream_readInt32(stream, &skipRole)) break;
                junk = XDataStream_readString(stream);
                if (junk) XClassDelete(junk);  /* readString 返回所有权。 */
            }
            continue;
        }
        if (n >= 256) break;
        rows[n] = r; cols[n] = c;
        datas[n] = XMap_create(sizeof(int), sizeof(const XString*),
                                Xint_compare);
        {
            int k;
            for (k = 0; k < count && k < XItemDataRole_UserRole; ++k) {
                int role = 0;
                XString* s = NULL;
                if (!XDataStream_readInt32(stream, &role)) break;
                s = XDataStream_readString(stream);
                /* 头文件明写 readString 归调用方；入表后必须随 map 一起
                   释放（见下方 datas[i] 回收段），否则每轮固定泄漏。 */
                if (s && datas[n]) XMap_insert_base(datas[n], &role, &s);
                else if (s) XClassDelete(s);
            }
        }
        if (r < top) top = r;
        if (c < left) left = c;
        if (r > bottom) bottom = r;
        if (c > right) right = c;
        ++n;
    }
    XClassDeinit((XClass*)stream);   /* 栈版流；必须先于 ro：只读流借用其缓冲。 */
    XClassDelete(ro);
    XClassDelete(blob);
    if (n <= 0) return false;
    /* 行号重排（逐条对齐 Qt qabstractitemmodel.cpp:2790-2804）：dragColumnCount
       是跨度列数；rowsToInsert 是「首次出现的行 → 连续序号」，不是出现次数。 */
    dragColumnCount = right - left + 1;
    rowsToInsert = (int*)XCalloc_System((size_t)(bottom + 1), sizeof(int));
    if (!rowsToInsert) {
        for (i = 0; i < n; ++i) if (datas[i]) XClassDelete(datas[i]);
        return false;
    }
    dragRowCount = 0;
    for (i = 0; i < n; ++i) rowsToInsert[rows[i]] = 1;
    for (i = 0; i < bottom + 1; ++i) {
        if (rowsToInsert[i] == 1) {
            rowsToInsert[i] = dragRowCount;
            ++dragRowCount;
        }
    }
    for (i = 0; i < n; ++i) rows[i] = top + rowsToInsert[rows[i]];
    XFree_System(rowsToInsert);
    colCount = XAbstractItemModel_columnCount(self);
    if (colCount == 0) {
        XAbstractItemModel_insertColumns(self, 0, dragColumnCount);
        colCount = XAbstractItemModel_columnCount(self);
    }
    if (!XAbstractItemModel_insertRows(self, row, dragRowCount)) {
        /* 插入失败即不写数据（对齐 Qt：decodeData 在 insertRows 失败后仍
           返回 true，但本库不静默「报成功没插入」）。 */
        for (i = 0; i < n; ++i) if (datas[i]) XClassDelete(datas[i]);
        return false;
    }
    if (row < 0) row = 0;
    if (column < 0) column = 0;
    /* 写回数据（Qt decodeData：对有效索引 setItemData）。本内存模型只写
       Display/Edit 同通道（map 内 role 0/2）。 */
    for (i = 0; i < n; ++i) {
        int dr = rows[i] - top + row;
        int dc = cols[i] - left + column;
        if (dr >= 0 && dr < XAbstractItemModel_rowCount(self) &&
            dc >= 0 && dc < XAbstractItemModel_columnCount(self) &&
            datas[i]) {
            XMap_iterator it = XMap_begin(datas[i]);
            while (!XMap_iterator_isEnd(&it)) {
                XPair* pair = XMap_iterator_data(&it);
                const int* key =
                    pair ? (const int*)XPair_first(pair) : NULL;
                const XString* const* val =
                    pair ? (const XString* const*)XPair_second(pair) : NULL;
                int role = key ? *key : -1;
                if (role == XItemDataRole_DisplayRole ||
                    role == XItemDataRole_EditRole)
                    XAbstractItemModel_setData(self, dr, dc,
                        val && *val ? *val : NULL);
                XMap_iterator_add(datas[i], &it);
            }
        }
        /* 释放 map 内 XString*（XDataStream_readString 的所有权在本侧）。
           **必须无条件执行**：落在上面 if 之外——否则越界索引那一轮的
           字符串随 map 壳一起丢掉 ⇒ 每轮固定泄漏（实测 1KB/轮线性）。 */
        if (datas[i]) {
            XMap_iterator vit = XMap_begin(datas[i]);
            while (!XMap_iterator_isEnd(&vit)) {
                XPair* pair = XMap_iterator_data(&vit);
                XString** vp = pair ? (XString**)XPair_second(pair) : NULL;
                if (vp && *vp) { XClassDelete(*vp); *vp = NULL; }
                XMap_iterator_add(datas[i], &vit);
            }
            XClassDelete(datas[i]);
        }
    }
    return true;
}

/** @brief supportedDropActions 虚槽对外唯一入口（同 flags_base 契约，
 *         2026-11-15 B6R 返工）：_base 只做空/虚表守卫 + 一次查槽分派，
 *         缺省业务在本类 class_init 挂表的 static
 *         VXAbstractItemModel_supportedDropActions；派生模型槽内回落
 *         父类口径用 XClass_Parent(XAbstractItemModel,
 *         EXAbstractItemModel_SupportedDropActions, int(*)(const
 *         XAbstractItemModel*))。 */
int XAbstractItemModel_supportedDropActions_base(const XAbstractItemModel* self)
{
    if (!self || !XClassGetVtable((XClass*)self))
        return XPlatformDragAction_Copy;
    return XClassGetVirtualFunc(self, EXAbstractItemModel_SupportedDropActions,
                                int (*)(const XAbstractItemModel*))(
        self);
}

/** @brief supportedDragActions 虚槽对外唯一入口（分派契约同上）。 */
int XAbstractItemModel_supportedDragActions_base(const XAbstractItemModel* self)
{
    if (!self || !XClassGetVtable((XClass*)self))
        return XPlatformDragAction_Copy;
    return XClassGetVirtualFunc(self, EXAbstractItemModel_SupportedDragActions,
                                int (*)(const XAbstractItemModel*))(
        self);
}

/** @brief supportedDropActions 虚槽基类缺省实现（对标
 *         qabstractitemmodel.cpp:2251-2257：基类恒 CopyAction）。 */
static int VXAbstractItemModel_supportedDropActions(
    const XAbstractItemModel* self)
{
    (void)self;
    return XPlatformDragAction_Copy;  /* 对标 Qt 基类缺省 CopyAction。 */
}

/** @brief supportedDragActions 虚槽基类缺省实现（对标
 *         qabstractitemmodel.cpp:2267-2269：缺省 = supportedDropActions）。 */
static int VXAbstractItemModel_supportedDragActions(
    const XAbstractItemModel* self)
{
    return XAbstractItemModel_supportedDropActions_base(self);
}

#endif /* XMIMEDATA_ON */

XMap* XAbstractItemModel_roleNames(const XAbstractItemModel* self)
{
    XMap* map;
    /* 逐项对齐 Qt 6.8 qabstractitemmodel.cpp:741-750 的 qDefaultRoleNames：
       恰 6 项，且**含 decoration**（本实现早期漏了它又多出 6 项，属超出
       缺省集合的臆造）。顺序无关（QHash），值必须逐字同。 */
    static const struct { int role; const char* name; } rn[] = {
        { XItemDataRole_DisplayRole,    "display"    },
        { XItemDataRole_DecorationRole, "decoration" },
        { XItemDataRole_EditRole,       "edit"       },
        { XItemDataRole_ToolTipRole,    "toolTip"    },
        { XItemDataRole_StatusTipRole,  "statusTip"  },
        { XItemDataRole_WhatsThisRole,  "whatsThis"  },
    };
    size_t k;
    (void)self;
    map = XMap_create(sizeof(int), sizeof(const char*), Xint_compare);
    if (!map) return NULL;
    for (k = 0; k < sizeof(rn) / sizeof(rn[0]); ++k)
        XMap_insert_base(map, &rn[k].role, &rn[k].name);
    return map;
}

/* ==================== 表头 ==================== */

const XString* XAbstractItemModel_headerData(const XAbstractItemModel* self,
                                             int section, int orientation)
{
    XString** slot = xaim_headerSlot(self, section, orientation);
    return (slot && *slot) ? *slot : NULL;
}

const char* XAbstractItemModel_headerData_2(const XAbstractItemModel* self,
                                            int section, int orientation)
{
    const XString* s;
    s = XAbstractItemModel_headerData(self, section, orientation);
    return s ? XString_toUtf8(s) : "";
}

bool XAbstractItemModel_setHeaderData(XAbstractItemModel* self, int section,
                                      int orientation, const XString* value)
{
    XString** slot;
    XString* repl;
    if (!self) return false;
    slot = xaim_headerSlot(self, section, orientation);
    if (!slot) return false;
    repl = value ? XString_create_copy(value) : NULL;
    if (value && !repl) return false;
    if (*slot) XClassDelete(*slot);
    *slot = repl;
    /* 对标 Qt：表头数据落库成功即发射 headerDataChanged（首末段同为
       本段，单段口径），驱动表头视图重绘（XHeaderView 经 setModel
       接线接收）。 */
    xaim_emitHeaderDataChanged(self, orientation, section, section);
    return true;
}

bool XAbstractItemModel_setHeaderData_2(XAbstractItemModel* self, int section,
                                        int orientation, const char* value)
{
    XString* tmp = NULL;
    bool ok;
    if (value) {
        tmp = XString_create_utf8(value);
        if (!tmp) return false;
    }
    ok = XAbstractItemModel_setHeaderData(self, section, orientation, tmp);
    if (tmp) XClassDelete(tmp);
    return ok;
}

/* ==================== 结构操作 ==================== */

bool XAbstractItemModel_hasChildren(const XAbstractItemModel* self)
{
    /* 扁平模型收敛：hasChildren(root) ⇔ 行数、列数均大于 0（对标 Qt
       hasChildren 缺省按 rowCount/columnCount 判定的扁平退化）。 */
    return self && self->m_rows > 0 && self->m_cols > 0;
}

/** @brief flags 虚槽对外唯一入口：直接按虚表槽分派（用户 2026-11-14
 *         定案）。_base 只做空/虚表守卫 + 一次 XClassGetVirtualFunc 查槽
 *         分派，不承载业务；原「基类缺省实现」语义移入本文件 static
 *         VXAbstractItemModel_flags（XVTABLE_OVERLOAD_DEFAULT 挂表），
 *         派生模型槽内回落父类口径用 XClass_Parent(XAbstractItemModel,
 *         EXAbstractItemModel_Flags, ...)。 */
int XAbstractItemModel_flags_base(const XAbstractItemModel* self, int row,
                                  int col)
{
    if (!self || !XClassGetVtable((XClass*)self))
        return XItemFlag_NoItemFlags;
    return XClassGetVirtualFunc(self, EXAbstractItemModel_Flags,
                                int (*)(const XAbstractItemModel*, int,
                                        int))(self, row, col);
}

/** @brief flags 虚槽基类缺省实现（无效索引 NoItemFlags；有效索引
 *         Selectable|Enabled；缺省不含 DropEnabled——拖放基础设施未建，
 *         见头文件注）。 */
static int VXAbstractItemModel_flags(const XAbstractItemModel* self, int row,
                                     int col)
{
    if (!xaim_cellSlot(self, row, col)) return XItemFlag_NoItemFlags;
    return XItemFlag_ItemIsSelectable | XItemFlag_ItemIsEnabled;
}

bool XAbstractItemModel_hasChildrenAt(const XAbstractItemModel* self,
                                      int row, int col)
{
    /* 对齐 Qt hasChildren(index) 缺省实现：parent 无效 ⇒ rowCount(root)；
       有效 ⇒ rowCount(parent) > 0。本库平铺模型任何索引下无子行 ⇒ false。 */
    if (!XAbstractItemModel_hasIndex(self, row, col))
        return XAbstractItemModel_hasChildren(self);
    return false;
}

bool XAbstractItemModel_insertRows(XAbstractItemModel* self, int row,
                                   int count)
{
    int64_t at;
    if (!self || row < 0 || row > self->m_rows || count <= 0) return false;
    /* 单元格：在 row*cols 处插入 count*cols 个空槽（元素回调保证复制
       语义安全；空槽以 NULL 填充）。0 列模型无单元格可插——行数照常
       增长（Qt 的 rowCount/columnCount 相互独立，0 列模型仍可有行）。 */
    at = (int64_t)row * self->m_cols;
    if (self->m_cols > 0) {
        XString** blanks = (XString**)XCalloc_System(
            (size_t)count * (size_t)self->m_cols, sizeof(XString*));
        if (!blanks) return false;
        if (!XVector_insert_1_base(&self->m_cells, at, blanks,
                                   (size_t)count * (size_t)self->m_cols)) {
            XFree_System(blanks);
            return false;
        }
        XFree_System(blanks);
    }
    /* 行头：在 row 处插入 count 个空槽，**与旧行数无关**——此前门限
       `m_rows > 0` 使空模型插行后行头表长度为 0，headerData(row,1)
       永远越界（行头长度恒等于行数的不变式被破坏）。失败则回滚单元格
       插入，保持不变式。 */
    {
        XString** headBlanks = (XString**)XCalloc_System((size_t)count,
                                                         sizeof(XString*));
        bool ok = headBlanks != NULL;
        if (ok) {
            ok = XVector_insert_1_base(&self->m_vHeader, row, headBlanks,
                                       (size_t)count);
            XFree_System(headBlanks);
        }
        if (!ok) {
            if (self->m_cols > 0)
                XVector_remove_base(&self->m_cells, at,
                                    (int64_t)count * self->m_cols);
            return false;
        }
    }
    /* begin/end 两段式（闭区间 first..last）：aboutTo 在行数生效前发。 */
    xaim_emitRows(
        self, (size_t)XAbstractItemModel_rowsAboutToBeInserted_signal, row,
        row + count - 1);
    self->m_rows += count;
    xaim_emitRows(self, (size_t)XAbstractItemModel_rowsInserted_signal, row,
                  row + count - 1);
    return true;
}

bool XAbstractItemModel_removeRows(XAbstractItemModel* self, int row,
                                   int count)
{
    int64_t at;
    if (!self || row < 0 || row >= self->m_rows || count <= 0) return false;
    if (count > self->m_rows - row) count = self->m_rows - row;
    xaim_emitRows(
        self, (size_t)XAbstractItemModel_rowsAboutToBeRemoved_signal, row,
        row + count - 1);
    at = (int64_t)row * self->m_cols;
    /* 元素回调对被裁剪格逐个释放（XString*），无泄漏。 */
    if (self->m_cols > 0)
        XVector_remove_base(&self->m_cells, at,
                            (int64_t)count * self->m_cols);
    XVector_remove_base(&self->m_vHeader, row, count);
    self->m_rows -= count;
    xaim_emitRows(self, (size_t)XAbstractItemModel_rowsRemoved_signal, row,
                  row + count - 1);
    return true;
}

bool XAbstractItemModel_insertColumns(XAbstractItemModel* self, int column,
                                      int count)
{
    int k;
    if (!self || column < 0 || column > self->m_cols || count <= 0)
        return false;
    xaim_emitRows(
        self, (size_t)XAbstractItemModel_columnsAboutToBeInserted_signal,
        column, column + count - 1);
    /* 逐列插入（单列一步）：行主序扁平向量上，从末行向前在每行
       column 位插入一个空槽（行距随已插入列数同步加一），避免整段
       搬移的索引换算。 */
    for (k = 0; k < count; ++k) {
        int r;
        XString* blank = NULL;
        for (r = self->m_rows - 1; r >= 0; --r) {
            XVector_insert_2(&self->m_cells,
                             (int64_t)r * (self->m_cols + 1) + column,
                             &blank);
        }
        ++self->m_cols;
    }
    /* 列头：在 column 处插入 count 个空槽。 */
    if (self->m_cols > 0) {
        XString** headBlanks = (XString**)XCalloc_System(
            (size_t)count, sizeof(XString*));
        if (headBlanks) {
            XVector_insert_1_base(&self->m_hHeader, column, headBlanks,
                                  (size_t)count);
            XFree_System(headBlanks);
        }
    }
    xaim_emitRows(self, (size_t)XAbstractItemModel_columnsInserted_signal,
                  column, column + count - 1);
    return true;
}

bool XAbstractItemModel_removeColumns(XAbstractItemModel* self, int column,
                                      int count)
{
    int k;
    if (!self || column < 0 || column >= self->m_cols || count <= 0)
        return false;
    if (count > self->m_cols - column) count = self->m_cols - column;
    xaim_emitRows(
        self, (size_t)XAbstractItemModel_columnsAboutToBeRemoved_signal,
        column, column + count - 1);
    for (k = 0; k < count; ++k) {
        int r;
        for (r = self->m_rows - 1; r >= 0; --r) {
            /* 元素回调对被移除格逐个释放；行距随已移除列数同步减一。 */
            XVector_remove_base(&self->m_cells,
                                (int64_t)r * (self->m_cols - 1) + column, 1);
        }
        --self->m_cols;
    }
    XVector_remove_base(&self->m_hHeader, column, count);
    xaim_emitRows(self, (size_t)XAbstractItemModel_columnsRemoved_signal,
                  column, column + count - 1);
    return true;
}

/* ==================== 单行/单列便捷形态（对标 Qt insertRow/removeRow 等） ==================== */

bool XAbstractItemModel_insertRow(XAbstractItemModel* self, int row)
{ return XAbstractItemModel_insertRows(self, row, 1); }

bool XAbstractItemModel_removeRow(XAbstractItemModel* self, int row)
{ return XAbstractItemModel_removeRows(self, row, 1); }

bool XAbstractItemModel_insertColumn(XAbstractItemModel* self, int column)
{ return XAbstractItemModel_insertColumns(self, column, 1); }

bool XAbstractItemModel_removeColumn(XAbstractItemModel* self, int column)
{ return XAbstractItemModel_removeColumns(self, column, 1); }

/* ---- 单行/单列移动形态（对标 moveRow/moveColumn：Qt 自身就是
 *      moveRows/moveColumns 的单元素转发，qabstractitemmodel.h:329/331） ---- */

bool XAbstractItemModel_moveRow(XAbstractItemModel* self, int sourceRow,
                                int destinationChild)
{ return XAbstractItemModel_moveRows(self, sourceRow, 1, destinationChild); }

bool XAbstractItemModel_moveColumn(XAbstractItemModel* self,
                                   int sourceColumn, int destinationChild)
{ return XAbstractItemModel_moveColumns(self, sourceColumn, 1,
                                       destinationChild); }

void XAbstractItemModel_sort(XAbstractItemModel* self, int column, int order)
{
    int i;
    int j;
    if (!self || column < 0 || column >= self->m_cols || self->m_rows < 2)
        return;
    /* 布局重排前置信号（对标 Qt layoutAboutToBeChanged：视图借此保存
       持久索引/几何快照）。hint 取 VerticalSortHint——按列对整行重排即
       Qt 的「竖向排序」提示（qsortfilterproxymodel.cpp:609/619 的
       `layoutAboutToBeChanged(..., VerticalSortHint)` / `layoutChanged(
       ..., VerticalSortHint)` 同款；本库 move 族只发 rowsMoved/
       columnsMoved，不发布局族信号，与 Qt 一致）。 */
    xaim_emitHint(
        self, (size_t)XAbstractItemModel_layoutAboutToBeChanged_signal,
        XLayoutChangeHint_VerticalSortHint);
    /* 稳定交换排序（同 XTableWidget_sortItems 口径）：按 UTF-8 字典序
       比较 column 列文本，order 0=升序、非 0=降序；整行单元格交换。 */
    for (i = 0; i < self->m_rows; ++i) {
        for (j = 0; j < self->m_rows - 1 - i; ++j) {
            XString** a = xaim_cellSlot(self, j, column);
            XString** b = xaim_cellSlot(self, j + 1, column);
            const char* sa = (a && *a) ? XString_toUtf8(*a) : "";
            const char* sb = (b && *b) ? XString_toUtf8(*b) : "";
            bool swap = (order == 0) ? (XStrcmp(sa, sb) > 0)
                                     : (XStrcmp(sa, sb) < 0);
            if (swap) {
                int c;
                for (c = 0; c < self->m_cols; ++c) {
                    XString** x = xaim_cellSlot(self, j, c);
                    XString** y = xaim_cellSlot(self, j + 1, c);
                    XString* tmp = *x;
                    *x = *y;
                    *y = tmp;
                }
            }
        }
    }
    xaim_emitHint(self, (size_t)XAbstractItemModel_layoutChanged_signal,
                  XLayoutChangeHint_VerticalSortHint);
}

/* ==================== 增量取数 / 移动 / 搜索 / 其它缺省 ==================== */

bool XAbstractItemModel_canFetchMore(const XAbstractItemModel* self,
                                     int row, int col)
{
    /* 基类缺省 false（Qt QAbstractItemModel::canFetchMore 缺省 false）。 */
    (void)self; (void)row; (void)col;
    return false;
}

void XAbstractItemModel_fetchMore(XAbstractItemModel* self, int row, int col)
{
    /* 基类缺省空操作（Qt 同）。 */
    (void)self; (void)row; (void)col;
}

bool XAbstractItemModel_moveRows(XAbstractItemModel* self, int sourceRow,
                                 int count, int destinationChild)
{
    int cols;
    size_t rowBytes;
    XString** tmpCells = NULL;
    XString** tmpHead = NULL;
    XString** cells;
    XString** heads;
    if (!self || count <= 0 || sourceRow < 0) return false;
    if (sourceRow + count > self->m_rows) return false;
    if (destinationChild < 0 || destinationChild > self->m_rows) return false;
    /* Qt：目标==源首或源尾是空操作 ⇒ false（qabstractitemmodel.cpp 的
       beginMoveRows 判定）。 */
    if (destinationChild == sourceRow || destinationChild == sourceRow + count)
        return false;
    cols = self->m_cols;
    rowBytes = (size_t)cols * sizeof(XString*);
    if (cols > 0) {
        tmpCells = (XString**)XCalloc_System((size_t)count * (size_t)cols,
                                             sizeof(XString*));
        if (!tmpCells) return false;
    }
    tmpHead = (XString**)XCalloc_System((size_t)count, sizeof(XString*));
    if (!tmpHead) {
        if (tmpCells) XFree_System(tmpCells);
        return false;
    }
    cells = cols > 0 ? (XString**)XVector_at_base(&self->m_cells, 0) : NULL;
    heads = (XString**)XVector_at_base(&self->m_vHeader, 0);
    /* 先取源块，再平移夹在中间的段，最后把源块写回目标位（Qt 的
       destinationChild 按移动前下标解释）。 */
    if (cols > 0)
        XMemcpy(tmpCells, cells + (int64_t)sourceRow * cols,
                (size_t)count * rowBytes);
    XMemcpy(tmpHead, heads + sourceRow, (size_t)count * sizeof(XString*));
    xaim_emitMoved(self,
                   (size_t)XAbstractItemModel_rowsAboutToBeMoved_signal,
                   sourceRow, sourceRow + count - 1, destinationChild);
    if (destinationChild > sourceRow) {
        int shift = destinationChild - (sourceRow + count);
        if (cols > 0) {
            if (shift > 0)
                XMemmove(cells + (int64_t)sourceRow * cols,
                         cells + (int64_t)(sourceRow + count) * cols,
                         (size_t)shift * rowBytes);
            XMemcpy(cells + (int64_t)(destinationChild - count) * cols,
                    tmpCells, (size_t)count * rowBytes);
        }
        if (shift > 0)
            XMemmove(heads + sourceRow, heads + sourceRow + count,
                     (size_t)shift * sizeof(XString*));
        XMemcpy(heads + (destinationChild - count), tmpHead,
                (size_t)count * sizeof(XString*));
    } else {
        int shift = sourceRow - destinationChild;
        if (cols > 0) {
            XMemmove(cells + (int64_t)(destinationChild + count) * cols,
                     cells + (int64_t)destinationChild * cols,
                     (size_t)shift * rowBytes);
            XMemcpy(cells + (int64_t)destinationChild * cols, tmpCells,
                    (size_t)count * rowBytes);
        }
        XMemmove(heads + destinationChild + count, heads + destinationChild,
                 (size_t)shift * sizeof(XString*));
        XMemcpy(heads + destinationChild, tmpHead,
                (size_t)count * sizeof(XString*));
    }
    XFree_System(tmpCells);
    XFree_System(tmpHead);
    xaim_emitMoved(self, (size_t)XAbstractItemModel_rowsMoved_signal,
                   sourceRow, sourceRow + count - 1, destinationChild);
    return true;
}

bool XAbstractItemModel_moveColumns(XAbstractItemModel* self, int sourceColumn,
                                    int count, int destinationChild)
{
    int cols;
    int r;
    XString** tmp = NULL;
    XString** cells;
    if (!self || count <= 0 || sourceColumn < 0) return false;
    if (sourceColumn + count > self->m_cols) return false;
    if (destinationChild < 0 || destinationChild > self->m_cols) return false;
    if (destinationChild == sourceColumn ||
        destinationChild == sourceColumn + count)
        return false;
    tmp = (XString**)XCalloc_System((size_t)count, sizeof(XString*));
    if (!tmp) return false;
    cols = self->m_cols;
    cells = (XString**)XVector_at_base(&self->m_cells, 0);
    xaim_emitMoved(self,
                   (size_t)XAbstractItemModel_columnsAboutToBeMoved_signal,
                   sourceColumn, sourceColumn + count - 1, destinationChild);
    for (r = 0; r < self->m_rows; ++r) {
        XString** row = cells + (int64_t)r * cols;
        XMemcpy(tmp, row + sourceColumn, (size_t)count * sizeof(XString*));
        if (destinationChild > sourceColumn) {
            int shift = destinationChild - (sourceColumn + count);
            XMemmove(row + sourceColumn, row + sourceColumn + count,
                     (size_t)shift * sizeof(XString*));
            XMemcpy(row + (destinationChild - count), tmp,
                    (size_t)count * sizeof(XString*));
        } else {
            int shift = sourceColumn - destinationChild;
            XMemmove(row + destinationChild + count, row + destinationChild,
                     (size_t)shift * sizeof(XString*));
            XMemcpy(row + destinationChild, tmp,
                    (size_t)count * sizeof(XString*));
        }
    }
    {
        XString** heads = (XString**)XVector_at_base(&self->m_hHeader, 0);
        XMemcpy(tmp, heads + sourceColumn, (size_t)count * sizeof(XString*));
        if (destinationChild > sourceColumn) {
            int shift = destinationChild - (sourceColumn + count);
            XMemmove(heads + sourceColumn, heads + sourceColumn + count,
                     (size_t)shift * sizeof(XString*));
            XMemcpy(heads + (destinationChild - count), tmp,
                    (size_t)count * sizeof(XString*));
        } else {
            int shift = sourceColumn - destinationChild;
            XMemmove(heads + destinationChild + count, heads + destinationChild,
                     (size_t)shift * sizeof(XString*));
            XMemcpy(heads + destinationChild, tmp,
                    (size_t)count * sizeof(XString*));
        }
    }
    XFree_System(tmp);
    xaim_emitMoved(self, (size_t)XAbstractItemModel_columnsMoved_signal,
                   sourceColumn, sourceColumn + count - 1, destinationChild);
    return true;
}

int XAbstractItemModel_match(const XAbstractItemModel* self, int startRow,
                             int startCol, int role, const char* value,
                             int flags, int* outRows, int* outCols,
                             int maxHits)
{
    int rows;
    int found = 0;
    int mode;
    bool caseSensitive;
    bool wrap;
    XChar_CaseSensitivity cs;
    if (!self) return 0;
    /* 正则/通配需要正则子系统（本库无 QRegularExpression 对应物）：
       不降级成子串匹配，直接 0 条（见头文件注）。类型掩码是
       flags & MatchTypeMask(0x0F)，值 4/5（Qt 6.8 qnamespace.h:1535-1536）。 */
    mode = flags & XMatchFlag_MatchTypeMask;
    if (mode == XMatchFlag_MatchRegularExpression ||
        mode == XMatchFlag_MatchWildcard)
        return 0;
    /* 本模型只有文本通道：DisplayRole(0)/EditRole(2) 可匹配，其余角色
       无承载 ⇒ 0 条（不伪造空匹配）。 */
    if (role != 0 && role != 2) return 0;
    rows = self->m_rows;
    if (rows <= 0 || self->m_cols <= 0) return 0;
    if (!value) value = "";
    caseSensitive = (flags & XMatchFlag_MatchCaseSensitive) != 0;
    cs = caseSensitive ? XChar_CaseSensitive : XChar_CaseInsensitive;
    wrap = (flags & XMatchFlag_MatchWrap) != 0;
    if (startRow < 0) startRow = 0;
    if (startCol < 0) startCol = 0;
    if (startRow >= rows) startRow = rows - 1;
    if (startCol >= self->m_cols) startCol = self->m_cols - 1;
    {
        /* 对齐 Qt 6.8：只扫 start 所在**列**的行（qabstractitemmodel.cpp:
         * 2529-2538），MatchWrap 在尾后从第 0 行续扫到 startRow-1。 */
        int n;
        int total = rows;
        for (n = 0; n < total; ++n) {
            int r = startRow + n;
            const XString* cell;
            bool hit = false;
            if (r >= rows) {
                if (!wrap) break;
                r -= rows;
            }
            cell = XAbstractItemModel_data(self, r, startCol);
            /* 空单元格：非 Exactly 模式下空 needle 命中（Qt 把无效值
               toString() 成空串，contains/startsWith "" 为真）；Exactly
               按 QVariant 相等比较（无效值 ≠ 空 QString）⇒ 不命中。 */
            if (!cell) {
                hit = (value[0] == '\0') &&
                      mode != XMatchFlag_MatchExactly;
            } else {
                switch (mode) {
                case XMatchFlag_MatchContains:
                    hit = XString_contains_utf8(cell, value, cs);
                    break;
                case XMatchFlag_MatchStartsWith:
                    hit = XString_startsWith_utf8(cell, value, cs);
                    break;
                case XMatchFlag_MatchEndsWith:
                    hit = XString_endsWith_utf8(cell, value, cs);
                    break;
                case XMatchFlag_MatchFixedString:
                    hit = XString_equals_utf8(cell, value, cs);
                    break;
                case XMatchFlag_MatchExactly:
                default:
                    /* Qt 的 MatchExactly 走 QVariant 相等（文本即区分大小写，
                       不受 MatchCaseSensitive 影响）。 */
                    hit = XString_equals_utf8(cell, value,
                                              XChar_CaseSensitive);
                    break;
                }
            }
            if (hit) {
                if (outRows && found < maxHits) outRows[found] = r;
                if (outCols && found < maxHits) outCols[found] = startCol;
                ++found;
            }
        }
    }
    return found;
}

bool XAbstractItemModel_span(const XAbstractItemModel* self, int row, int col,
                             int* outRowSpan, int* outColSpan)
{
    /* Qt 基类缺省 QSize(1,1)（qabstractitemmodel.cpp）。 */
    if (outRowSpan) *outRowSpan = 1;
    if (outColSpan) *outColSpan = 1;
    return XAbstractItemModel_hasIndex(self, row, col);
}

bool XAbstractItemModel_submit(XAbstractItemModel* self)
{
    /* Qt 基类 submit 返回 true（无编辑暂存区）。 */
    return self != NULL;
}

void XAbstractItemModel_revert(XAbstractItemModel* self)
{
    (void)self; /* Qt 基类空操作。 */
}

bool XAbstractItemModel_buddy(const XAbstractItemModel* self, int row,
                              int col, int* outRow, int* outCol)
{
    (void)self; (void)row; (void)col;
    /* Qt 基类返回无效 QModelIndex。 */
    if (outRow) *outRow = -1;
    if (outCol) *outCol = -1;
    return false;
}

void XAbstractItemModel_beginResetModel(XAbstractItemModel* self)
{
    xaim_emitVoid(self,
                  (size_t)XAbstractItemModel_modelAboutToBeReset_signal);
}

void XAbstractItemModel_endResetModel(XAbstractItemModel* self)
{
    xaim_emitReset(self);
}

/* ==================== 信号 ==================== */

void* XAbstractItemModel_dataChanged_signal(XAbstractItemModel* self,
                                            int topRow, int topCol,
                                            int bottomRow, int bottomCol,
                                            int roles)
{
    xaim_emitDataChanged(self, topRow, topCol, bottomRow, bottomCol, roles);
    return (void*)(size_t)XAbstractItemModel_dataChanged_signal;
}

void* XAbstractItemModel_modelReset_signal(XAbstractItemModel* self)
{
    xaim_emitReset(self);
    return (void*)(size_t)XAbstractItemModel_modelReset_signal;
}

void* XAbstractItemModel_modelAboutToBeReset_signal(XAbstractItemModel* self)
{
    xaim_emitVoid(self,
                  (size_t)XAbstractItemModel_modelAboutToBeReset_signal);
    return (void*)(size_t)XAbstractItemModel_modelAboutToBeReset_signal;
}

void* XAbstractItemModel_layoutAboutToBeChanged_signal(
    XAbstractItemModel* self, int hint)
{
    /* hint 只在 self 非空时进入载荷；self==NULL（XSignal 的单参 NULL
       调用）时 xaim_emitHint 先短路，hint 残值不被读取。 */
    xaim_emitHint(self,
                  (size_t)XAbstractItemModel_layoutAboutToBeChanged_signal,
                  hint);
    return (void*)(size_t)XAbstractItemModel_layoutAboutToBeChanged_signal;
}

void* XAbstractItemModel_rowsAboutToBeInserted_signal(
    XAbstractItemModel* self, int first, int last)
{
    xaim_emitRows(
        self, (size_t)XAbstractItemModel_rowsAboutToBeInserted_signal, first,
        last);
    return (void*)(size_t)XAbstractItemModel_rowsAboutToBeInserted_signal;
}

void* XAbstractItemModel_rowsAboutToBeRemoved_signal(
    XAbstractItemModel* self, int first, int last)
{
    xaim_emitRows(
        self, (size_t)XAbstractItemModel_rowsAboutToBeRemoved_signal, first,
        last);
    return (void*)(size_t)XAbstractItemModel_rowsAboutToBeRemoved_signal;
}

void* XAbstractItemModel_columnsAboutToBeInserted_signal(
    XAbstractItemModel* self, int first, int last)
{
    xaim_emitRows(
        self, (size_t)XAbstractItemModel_columnsAboutToBeInserted_signal,
        first, last);
    return (void*)(size_t)XAbstractItemModel_columnsAboutToBeInserted_signal;
}

void* XAbstractItemModel_columnsAboutToBeRemoved_signal(
    XAbstractItemModel* self, int first, int last)
{
    xaim_emitRows(
        self, (size_t)XAbstractItemModel_columnsAboutToBeRemoved_signal, first,
        last);
    return (void*)(size_t)XAbstractItemModel_columnsAboutToBeRemoved_signal;
}

void* XAbstractItemModel_rowsInserted_signal(XAbstractItemModel* self,
                                             int first, int last)
{
    xaim_emitRows(self, (size_t)XAbstractItemModel_rowsInserted_signal, first,
                  last);
    return (void*)(size_t)XAbstractItemModel_rowsInserted_signal;
}

void* XAbstractItemModel_rowsRemoved_signal(XAbstractItemModel* self,
                                            int first, int last)
{
    xaim_emitRows(self, (size_t)XAbstractItemModel_rowsRemoved_signal, first,
                  last);
    return (void*)(size_t)XAbstractItemModel_rowsRemoved_signal;
}

void* XAbstractItemModel_headerDataChanged_signal(XAbstractItemModel* self,
                                                  int orientation, int first,
                                                  int last)
{
    xaim_emitHeaderDataChanged(self, orientation, first, last);
    return (void*)(size_t)XAbstractItemModel_headerDataChanged_signal;
}

void* XAbstractItemModel_columnsInserted_signal(XAbstractItemModel* self,
                                                int first, int last)
{
    xaim_emitRows(self, (size_t)XAbstractItemModel_columnsInserted_signal,
                  first, last);
    return (void*)(size_t)XAbstractItemModel_columnsInserted_signal;
}

void* XAbstractItemModel_columnsRemoved_signal(XAbstractItemModel* self,
                                               int first, int last)
{
    xaim_emitRows(self, (size_t)XAbstractItemModel_columnsRemoved_signal,
                  first, last);
    return (void*)(size_t)XAbstractItemModel_columnsRemoved_signal;
}

void* XAbstractItemModel_rowsAboutToBeMoved_signal(XAbstractItemModel* self,
                                                   int first, int last,
                                                   int destinationRow)
{
    xaim_emitMoved(self,
                   (size_t)XAbstractItemModel_rowsAboutToBeMoved_signal, first,
                   last, destinationRow);
    return (void*)(size_t)XAbstractItemModel_rowsAboutToBeMoved_signal;
}

void* XAbstractItemModel_rowsMoved_signal(XAbstractItemModel* self, int first,
                                          int last, int destinationRow)
{
    xaim_emitMoved(self, (size_t)XAbstractItemModel_rowsMoved_signal, first,
                   last, destinationRow);
    return (void*)(size_t)XAbstractItemModel_rowsMoved_signal;
}

void* XAbstractItemModel_columnsAboutToBeMoved_signal(
    XAbstractItemModel* self, int first, int last, int destinationColumn)
{
    xaim_emitMoved(self,
                   (size_t)XAbstractItemModel_columnsAboutToBeMoved_signal,
                   first, last, destinationColumn);
    return (void*)(size_t)XAbstractItemModel_columnsAboutToBeMoved_signal;
}

void* XAbstractItemModel_columnsMoved_signal(XAbstractItemModel* self,
                                             int first, int last,
                                             int destinationColumn)
{
    xaim_emitMoved(self, (size_t)XAbstractItemModel_columnsMoved_signal, first,
                   last, destinationColumn);
    return (void*)(size_t)XAbstractItemModel_columnsMoved_signal;
}

void* XAbstractItemModel_layoutChanged_signal(XAbstractItemModel* self,
                                              int hint)
{
    xaim_emitHint(self, (size_t)XAbstractItemModel_layoutChanged_signal, hint);
    return (void*)(size_t)XAbstractItemModel_layoutChanged_signal;
}

#endif /* XWIDGET_ON && XTABLEWIDGET_ON */

#endif /* XGUI_ON */
