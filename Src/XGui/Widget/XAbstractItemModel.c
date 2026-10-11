/**
 * @file       XAbstractItemModel.c
 * @brief      XAbstractItemModel 条目模型实现（内存二维模型）。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XAbstractItemModel.h"

#include "XAlgorithm.h"
#include "XMemory.h"
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

static void xaim_emitDataChanged(XAbstractItemModel* self, int row, int col)
{
    XVarList* args = XVarList_Create(XVar(int, row), XVar(int, col));
    if (!args) return;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self,
                           (size_t)XAbstractItemModel_dataChanged_signal,
                           args, NULL, NULL, XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

static void xaim_emitRows(XAbstractItemModel* self, size_t signal,
                          int row, int count)
{
    XVarList* args = XVarList_Create(XVar(int, row), XVar(int, count));
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

XVtable* XAbstractItemModel_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XAbstractItemModel)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXAbstractItemModel_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXAbstractItemModel_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXAbstractItemModel_move);
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

void XAbstractItemModel_setDimension(XAbstractItemModel* self,
                                     int rows, int cols)
{
    size_t oldCells;
    int oldRows;
    int oldCols;
    if (!self || rows < 0 || cols < 0) return;
    oldRows = self->m_rows;
    oldCols = self->m_cols;
    if (rows == oldRows && cols == oldCols) return;
    oldCells = XVector_size_base(&self->m_cells);
    /* 单元格：行主序一维向量，尺寸恒等于 rows*cols；缩小区间经元素
       回调释放被裁剪格，新增区间补 NULL（原扩列错位问题随裸数组一并
       消除）。表头向量按新维度同步裁剪/补位。任一分配失败即回滚已
       完成的 resize（保持原维度状态）。 */
    if (!XVector_resize_base(&self->m_cells,
                             (size_t)rows * (size_t)cols))
        return;
    if (!XVector_resize_base(&self->m_hHeader, (size_t)cols)) {
        XVector_resize_base(&self->m_cells, oldCells);
        return;
    }
    if (!XVector_resize_base(&self->m_vHeader, (size_t)rows)) {
        XVector_resize_base(&self->m_hHeader, (size_t)oldCols);
        XVector_resize_base(&self->m_cells, oldCells);
        return;
    }
    self->m_rows = rows;
    self->m_cols = cols;
    if (rows > oldRows)
        xaim_emitRows(self, (size_t)XAbstractItemModel_rowsInserted_signal,
                      oldRows, rows - oldRows);
    else if (rows < oldRows)
        xaim_emitRows(self, (size_t)XAbstractItemModel_rowsRemoved_signal,
                      rows, oldRows - rows);
}

void XAbstractItemModel_setRowCount(XAbstractItemModel* self, int rows)
{
    if (!self) return;
    XAbstractItemModel_setDimension(self, rows, self->m_cols);
}

void XAbstractItemModel_setColumnCount(XAbstractItemModel* self, int cols)
{
    if (!self) return;
    XAbstractItemModel_setDimension(self, self->m_rows, cols);
}

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
    xaim_emitDataChanged(self, row, col);
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

/* ==================== 信号 ==================== */

void* XAbstractItemModel_dataChanged_signal(XAbstractItemModel* self,
                                            int row, int col)
{
    xaim_emitDataChanged(self, row, col);
    return (void*)(size_t)XAbstractItemModel_dataChanged_signal;
}

void* XAbstractItemModel_modelReset_signal(XAbstractItemModel* self)
{
    xaim_emitReset(self);
    return (void*)(size_t)XAbstractItemModel_modelReset_signal;
}

void* XAbstractItemModel_rowsInserted_signal(XAbstractItemModel* self,
                                             int row, int count)
{
    xaim_emitRows(self, (size_t)XAbstractItemModel_rowsInserted_signal,
                  row, count);
    return (void*)(size_t)XAbstractItemModel_rowsInserted_signal;
}

void* XAbstractItemModel_rowsRemoved_signal(XAbstractItemModel* self,
                                            int row, int count)
{
    xaim_emitRows(self, (size_t)XAbstractItemModel_rowsRemoved_signal,
                  row, count);
    return (void*)(size_t)XAbstractItemModel_rowsRemoved_signal;
}

#endif /* XWIDGET_ON && XTABLEWIDGET_ON */
