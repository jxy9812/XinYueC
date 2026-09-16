/**
 * @file       XAbstractItemModel.c
 * @brief      XAbstractItemModel 条目模型实现（内存二维模型）。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XAbstractItemModel.h"

#include "XAlgorithm.h"
#include "XMemory.h"
#include "XVarList.h"
#include "XEventType.h"

#if XWIDGET_ON && XTABLEWIDGET_ON

/* ==================== 内部工具 ==================== */

static XString** xaim_getRow(const XAbstractItemModel* self, int row)
{
    return (self && self->m_cells && row >= 0 && row < self->m_rows)
               ? self->m_cells[row] : NULL;
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

/* ==================== 容量 ==================== */

static bool xaim_growRows(XAbstractItemModel* self, int needRows)
{
    XString*** grown;
    int newCap;
    int r;
    if (needRows <= self->m_capRows) return true;
    newCap = self->m_capRows > 0 ? self->m_capRows : 4;
    while (newCap < needRows) newCap *= 2;
    grown = (XString***)XRealloc_System(self->m_cells,
                                        sizeof(XString**) * (size_t)newCap);
    if (!grown) return false;
    self->m_cells = grown;
    for (r = self->m_capRows; r < newCap; ++r)
        self->m_cells[r] = NULL;
    self->m_capRows = newCap;
    return true;
}

static bool xaim_growCols(XAbstractItemModel* self, int row, int needCols)
{
    XString** grown;
    int newCap;
    int r;
    int c;
    if (needCols <= self->m_capCols) return true;
    newCap = self->m_capCols > 0 ? self->m_capCols : 4;
    while (newCap < needCols) newCap *= 2;
    for (r = 0; r < self->m_capRows; ++r) {
        if (!self->m_cells[r]) continue;
        grown = (XString**)XRealloc_System(self->m_cells[r],
                                           sizeof(XString*) * (size_t)newCap);
        if (!grown) return false;
        self->m_cells[r] = grown;
        for (c = self->m_capCols; c < newCap; ++c)
            self->m_cells[r][c] = NULL;
    }
    self->m_capCols = newCap;
    return true;
}

/* ==================== 虚函数实现 ==================== */

static void VXAbstractItemModel_deinit(XAbstractItemModel* self)
{
    int r, c;
    if (!self) return;
    for (r = 0; r < self->m_rows; ++r) {
        if (!self->m_cells || !self->m_cells[r]) continue;
        for (c = 0; c < self->m_cols; ++c) {
            if (self->m_cells[r][c]) XString_delete_base(self->m_cells[r][c]);
            self->m_cells[r][c] = NULL;
        }
        if (self->m_cells[r]) XFree_System(self->m_cells[r]);
    }
    if (self->m_cells) {
        XFree_System(self->m_cells);
        self->m_cells = NULL;
    }
    for (c = 0; c < self->m_cols; ++c) {
        if (self->m_hHeader && self->m_hHeader[c]) {
            XString_delete_base(self->m_hHeader[c]);
            self->m_hHeader[c] = NULL;
        }
    }
    if (self->m_hHeader) {
        XFree_System(self->m_hHeader);
        self->m_hHeader = NULL;
    }
    for (r = 0; r < self->m_rows; ++r) {
        if (self->m_vHeader && self->m_vHeader[r]) {
            XString_delete_base(self->m_vHeader[r]);
            self->m_vHeader[r] = NULL;
        }
    }
    if (self->m_vHeader) {
        XFree_System(self->m_vHeader);
        self->m_vHeader = NULL;
    }
    self->m_rows = 0;
    self->m_cols = 0;
    self->m_capRows = 0;
    self->m_capCols = 0;
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
                if (other->m_cells && other->m_cells[r] &&
                    other->m_cells[r][c]) {
                    XAbstractItemModel_setData(self, r, c,
                                               other->m_cells[r][c]);
                }
            }
            if (other->m_vHeader && other->m_vHeader[r])
                XAbstractItemModel_setHeaderData(
                    self, r, 1, other->m_vHeader[r]);
        }
        for (c = 0; c < other->m_cols; ++c) {
            if (other->m_hHeader && other->m_hHeader[c])
                XAbstractItemModel_setHeaderData(
                    self, c, 0, other->m_hHeader[c]);
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
    self->m_cells = other->m_cells;
    self->m_rows = other->m_rows;
    self->m_cols = other->m_cols;
    self->m_capRows = other->m_capRows;
    self->m_capCols = other->m_capCols;
    self->m_hHeader = other->m_hHeader;
    self->m_vHeader = other->m_vHeader;
    other->m_cells = NULL;
    other->m_rows = 0;
    other->m_cols = 0;
    other->m_capRows = 0;
    other->m_capCols = 0;
    other->m_hHeader = NULL;
    other->m_vHeader = NULL;
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
    int oldRows;
    int oldCols;
    int r;
    int c;
    if (!self || rows < 0 || cols < 0) return;
    oldRows = self->m_rows;
    oldCols = self->m_cols;
    if (rows == oldRows && cols == oldCols) return;
    /* 扩容：先保证容量。 */
    if (!xaim_growRows(self, rows)) return;
    for (r = 0; r < rows; ++r) {
        if (!self->m_cells[r]) {
            self->m_cells[r] = (XString**)XCalloc_System(
                (size_t)cols, sizeof(XString*));
            if (!self->m_cells[r]) return;
        }
        if (!xaim_growCols(self, r, cols)) return;
    }
    /* 缩容：释放被裁剪的列/行。 */
    for (r = 0; r < rows && r < oldRows; ++r) {
        int c;
        for (c = cols; c < oldCols; ++c) {
            if (self->m_cells[r] && self->m_cells[r][c]) {
                XString_delete_base(self->m_cells[r][c]);
                self->m_cells[r][c] = NULL;
            }
        }
    }
    for (r = rows; r < oldRows; ++r) {
        int c;
        if (!self->m_cells[r]) continue;
        for (c = 0; c < oldCols; ++c) {
            if (self->m_cells[r][c]) XString_delete_base(self->m_cells[r][c]);
        }
        XFree_System(self->m_cells[r]);
        self->m_cells[r] = NULL;
    }
    /* 表头数组同步（列头/行头）。 */
    {
        XString** newH = NULL;
        XString** newV = NULL;
        if (cols > 0) {
            newH = (XString**)XCalloc_System((size_t)cols, sizeof(XString*));
            if (!newH) return;
            for (c = 0; c < cols && c < oldCols; ++c)
                newH[c] = self->m_hHeader ? self->m_hHeader[c] : NULL;
        }
        if (rows > 0) {
            newV = (XString**)XCalloc_System((size_t)rows, sizeof(XString*));
            if (!newV) { if (newH) XFree_System(newH); return; }
            for (r = 0; r < rows && r < oldRows; ++r)
                newV[r] = self->m_vHeader ? self->m_vHeader[r] : NULL;
        }
        if (self->m_hHeader) XFree_System(self->m_hHeader);
        if (self->m_vHeader) XFree_System(self->m_vHeader);
        self->m_hHeader = newH;
        self->m_vHeader = newV;
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
    XString** cells;
    if (!self) return NULL;
    cells = xaim_getRow(self, row);
    if (!cells || col < 0 || col >= self->m_cols) return NULL;
    return cells[col];
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
    XString** cells;
    XString* repl;
    if (!self) return false;
    cells = xaim_getRow(self, row);
    if (!cells || col < 0 || col >= self->m_cols) return false;
    repl = value ? XString_create_copy(value) : NULL;
    if (value && !repl) return false;
    if (cells[col]) XString_delete_base(cells[col]);
    cells[col] = repl;
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
    if (tmp) XString_delete_base(tmp);
    return ok;
}

/* ==================== 表头 ==================== */

const XString* XAbstractItemModel_headerData(const XAbstractItemModel* self,
                                             int section, int orientation)
{
    if (!self) return NULL;
    if (orientation == 0) { /* 水平列头 */
        if (section < 0 || section >= self->m_cols || !self->m_hHeader)
            return NULL;
        return self->m_hHeader[section];
    }
    /* 垂直行头 */
    if (section < 0 || section >= self->m_rows || !self->m_vHeader)
        return NULL;
    return self->m_vHeader[section];
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
    XString** arr;
    XString* repl;
    int count;
    if (!self) return false;
    if (orientation == 0) {
        arr = self->m_hHeader;
        count = self->m_cols;
    } else {
        arr = self->m_vHeader;
        count = self->m_rows;
    }
    if (section < 0 || section >= count || !arr) return false;
    repl = value ? XString_create_copy(value) : NULL;
    if (value && !repl) return false;
    if (arr[section]) XString_delete_base(arr[section]);
    arr[section] = repl;
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
    if (tmp) XString_delete_base(tmp);
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
