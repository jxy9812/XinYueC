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
    self->m_rows = other->m_rows;
    self->m_cols = other->m_cols;
    other->m_rows = 0;
    other->m_cols = 0;
}

/* ==================== 类初始化 ==================== */

static int VXAbstractItemModel_flags(const XAbstractItemModel* self, int row,
                                     int col);

XVtable* XAbstractItemModel_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XAbstractItemModel)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXAbstractItemModel_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXAbstractItemModel_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXAbstractItemModel_move);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractItemModel_Flags,
                             VXAbstractItemModel_flags);
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

/* ==================== 树形预留 ==================== */

int XAbstractItemModel_parent(const XAbstractItemModel* self,
                              int row, int col)
{
    (void)self; (void)row; (void)col;
    return -1;
}

/* ==================== 结构操作 ==================== */

bool XAbstractItemModel_hasChildren(const XAbstractItemModel* self)
{
    /* 扁平模型收敛：hasChildren(root) ⇔ 行数、列数均大于 0（对标 Qt
       hasChildren 缺省按 rowCount/columnCount 判定的扁平退化）。 */
    return self && self->m_rows > 0 && self->m_cols > 0;
}

int XAbstractItemModel_flags_base(const XAbstractItemModel* self, int row,
                                  int col)
{
    /* 对标 Qt 6.8 基类缺省 flags：无效索引 NoItemFlags；有效索引
       Selectable|Enabled（缺省不含 DropEnabled——拖放基础设施未建，
       见头文件注）。 */
    if (!xaim_cellSlot(self, row, col)) return XItemFlag_NoItemFlags;
    return XItemFlag_ItemIsSelectable | XItemFlag_ItemIsEnabled;
}

static int VXAbstractItemModel_flags(const XAbstractItemModel* self, int row,
                                     int col)
{
    return XAbstractItemModel_flags_base(self, row, col);
}

int XAbstractItemModel_flags(const XAbstractItemModel* self, int row, int col)
{
    /* 虚槽分派（对标 QAbstractItemModel::flags 虚函数）：派生模型可
       重载 EXAbstractItemModel_Flags 给出 ItemIsEditable 等位。 */
    int (*fn)(const XAbstractItemModel*, int, int);
    if (!self) return XItemFlag_NoItemFlags;
    fn = (int (*)(const XAbstractItemModel*, int, int))
        XVtableGetFunc(XClassGetVtable((XClass*)self),
                       EXAbstractItemModel_Flags, void*);
    if (!fn) return XAbstractItemModel_flags_base(self, row, col);
    return fn(self, row, col);
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

void XAbstractItemModel_sort(XAbstractItemModel* self, int column, int order)
{
    int i;
    int j;
    if (!self || column < 0 || column >= self->m_cols || self->m_rows < 2)
        return;
    /* 布局重排前置信号（对标 Qt layoutAboutToBeChanged：视图借此保存
       持久索引/几何快照）。 */
    xaim_emitVoid(self,
                  (size_t)XAbstractItemModel_layoutAboutToBeChanged_signal);
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
    xaim_emitVoid(self, (size_t)XAbstractItemModel_layoutChanged_signal);
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
    XAbstractItemModel* self)
{
    xaim_emitVoid(self,
                  (size_t)XAbstractItemModel_layoutAboutToBeChanged_signal);
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

void* XAbstractItemModel_layoutChanged_signal(XAbstractItemModel* self)
{
    xaim_emitVoid(self, (size_t)XAbstractItemModel_layoutChanged_signal);
    return (void*)(size_t)XAbstractItemModel_layoutChanged_signal;
}

#endif /* XWIDGET_ON && XTABLEWIDGET_ON */

#endif /* XGUI_ON */
