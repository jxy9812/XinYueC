#include "XTableWidget.h"
#include "XStringUtils.h"
#include "XVector.h"

#include "XAlgorithm.h"
#include "XStyle.h"
#include "XStyleOption.h"
#include "XMemory.h"
#include "XWidget_Protected.h"
#include "XEvent.h"
#include "XPainter.h"
#include "XWindowEvent.h"
#include <stdio.h>


#if XTABLEWIDGET_ON

#define XTW_DEFAULT_COL_WIDTH  90
#define XTW_DEFAULT_ROW_HEIGHT 24
#define XTW_HEADER_H           24
#define XTW_HEADER_W           40

/* ==================== 内部辅助 ==================== */

static void VX_tableWidget_paintEvent(XWidget* self, XEvent* event);
static void VX_tableWidget_mousePressEvent(XWidget* self, XEvent* event);
static void VX_tableWidget_mouseDoubleClickEvent(XWidget* self, XEvent* event);
static void VX_tableWidget_mouseMoveEvent(XWidget* self, XEvent* event);
static void VX_tableWidget_keyPressEvent(XWidget* self, XEvent* event);
static void VX_tableWidget_scrollContentsBy(XAbstractScrollArea* self,
                                            int dx, int dy);
/** @brief 调色板取色助手。 */
static uint32_t xtw_color(const XTableWidget* self, XPaletteColorRole role);
/** @brief 坐标 → 单元格（返回行列；表头/越界返回 -1）。 */
static void xtw_cellAt(const XTableWidget* self, int x, int y,
                       int* row, int* col);
/** @brief 列 x 起点（visualItemRect 使用；实现见渲染与交互节）。 */
static int xtw_colX(const XTableWidget* self, int col);
/** @brief 滚动偏移（visualItemRect 使用；实现见渲染与交互节）。 */
static int xtw_vOffset(const XTableWidget* self);
static int xtw_hOffset(const XTableWidget* self);
/** @brief 按当前行列尺寸同步滚动范围（对标 QTableViewPrivate::updateScrollBars）。 */
static void xtw_updateContentSize(XTableWidget* self);
/** @brief 确保垂直表头指针数组容量（新增区域清零）。 */
static bool xtw_ensureVHeaders(XTableWidget* self, int count);
/** @brief 挂载部件表：按 (row,col) 查找条目下标；无则返回 -1。 */
static int xtw_cwFind(const XTableWidget* self, int row, int col);

/** @brief 确保行指针数组容量（保留现有行内容）。 */
static void xtw_ensureRows(XTableWidget* self, int rows)
{
    XTableWidgetItem** p;
    int cap = self->m_rowCapacity > 0 ? self->m_rowCapacity : 4;
    if (rows <= self->m_rowCapacity) return;
    while (cap < rows) cap *= 2;
    p = (XTableWidgetItem**)XRealloc_System(self->m_cells,
        sizeof(XTableWidgetItem*) * (size_t)cap);
    if (!p) return;
    self->m_cells = p;
    self->m_rowCapacity = cap;
}

/** @brief 确保列容量：扩列宽数组/表头/每行单元格数组。 */
static void xtw_ensureCols(XTableWidget* self, int cols)
{
    int* w;
    XString** hh;
    int cap = self->m_base.m_colCapacity > 0 ? self->m_base.m_colCapacity : 4;
    int i;
    if (cols <= self->m_base.m_colCapacity) return;
    while (cap < cols) cap *= 2;
    w = (int*)XRealloc_System(self->m_base.m_colWidths, sizeof(int) * (size_t)cap);
    if (w) { self->m_base.m_colWidths = w;
        for (i = self->m_base.m_colCapacity; i < cap; ++i) self->m_base.m_colWidths[i] = XTW_DEFAULT_COL_WIDTH; }
    hh = (XString**)XRealloc_System(self->m_hHeaders,
                                    sizeof(XString*) * (size_t)cap);
    if (hh) { self->m_hHeaders = hh;
        for (i = self->m_base.m_colCapacity; i < cap; ++i)
            self->m_hHeaders[i] = NULL; }
    for (i = 0; i < self->m_rows; ++i) {
        XTableWidgetItem* row = self->m_cells ? self->m_cells[i] : NULL;
        XTableWidgetItem* nr = (XTableWidgetItem*)XRealloc_System(row,
            sizeof(XTableWidgetItem) * (size_t)cap);
        if (nr) { self->m_cells[i] = nr;
            XMemset(nr + self->m_base.m_colCapacity, 0,
                   sizeof(XTableWidgetItem) * (size_t)(cap - self->m_base.m_colCapacity)); }
    }
    self->m_base.m_colCapacity = cap;
}

/** @brief 分配一个全零单元格行。 */
static XTableWidgetItem* xtw_newRow(int cols)
{
    XTableWidgetItem* row =
        (XTableWidgetItem*)XMalloc_System(sizeof(XTableWidgetItem) * (size_t)cols);
    if (row) XMemset(row, 0, sizeof(XTableWidgetItem) * (size_t)cols);
    return row;
}

/** @brief 确保垂直表头指针数组容量（新增区域清零；成功 true）。 */
static bool xtw_ensureVHeaders(XTableWidget* self, int count)
{
    int cap;
    XString** vh;
    if (count <= self->m_vHeaderCapacity) return true;
    cap = self->m_vHeaderCapacity > 0 ? self->m_vHeaderCapacity : 256;
    while (cap < count) cap *= 2;
    vh = (XString**)XRealloc_System(self->m_vHeaders,
                                    sizeof(XString*) * (size_t)cap);
    if (!vh) return false;
    /* 新增区域必须清零：调用方以 m_vHeaders[i] 是否为 NULL 判定
       首次创建；realloc 的未初始化内存是野指针，直接复用会崩溃。 */
    XMemset(vh + self->m_vHeaderCapacity, 0,
            sizeof(XString*) * (size_t)(cap - self->m_vHeaderCapacity));
    self->m_vHeaders = vh;
    self->m_vHeaderCapacity = cap;
    return true;
}

/** @brief 取单元格（越界返回 NULL）。 */
static XTableWidgetItem* xtw_cell(const XTableWidget* self, int row, int col)
{
    if (!self || row < 0 || row >= self->m_rows ||
        col < 0 || col >= self->m_columns || !self->m_cells) return NULL;
    return &self->m_cells[row][col];
}

/* ==================== 挂载部件表助手（借用语义） ==================== */

/** @brief 按 (row,col) 查找挂载条目下标；无则返回 -1。 */
static int xtw_cwFind(const XTableWidget* self, int row, int col)
{
    int i;
    for (i = 0; i < self->m_cellWidgetCount; ++i)
        if (self->m_cellWidgets[i].row == row &&
            self->m_cellWidgets[i].column == col) return i;
    return -1;
}

/** @brief 确保挂载条目容量至少 need（成功 true）。 */
static bool xtw_cwReserve(XTableWidget* self, int need)
{
    XTableWidgetCellWidget* p;
    int cap = self->m_cellWidgetCapacity > 0 ? self->m_cellWidgetCapacity : 4;
    if (need <= self->m_cellWidgetCapacity) return true;
    while (cap < need) cap *= 2;
    p = (XTableWidgetCellWidget*)XRealloc_System(self->m_cellWidgets,
        sizeof(XTableWidgetCellWidget) * (size_t)cap);
    if (!p) return false;
    self->m_cellWidgets = p;
    self->m_cellWidgetCapacity = cap;
    return true;
}

/** @brief 删除下标 i 处挂载条目（后续条目前移；借用部件不销毁）。 */
static void xtw_cwRemoveAt(XTableWidget* self, int i)
{
    for (; i < self->m_cellWidgetCount - 1; ++i)
        self->m_cellWidgets[i] = self->m_cellWidgets[i + 1];
    self->m_cellWidgetCount--;
}

/** @brief 全部解除挂载（部件为借用，仅解除关联不销毁）。 */
static void xtw_cwClear(XTableWidget* self)
{
    if (self) self->m_cellWidgetCount = 0;
}

/** @brief 行插入后平移挂载表：行号 >= row 的条目行号加一。 */
static void xtw_cwOnRowInserted(XTableWidget* self, int row)
{
    int i;
    if (!self) return;
    for (i = 0; i < self->m_cellWidgetCount; ++i)
        if (self->m_cellWidgets[i].row >= row) self->m_cellWidgets[i].row++;
}

/** @brief 列插入后平移挂载表：列号 >= column 的条目列号加一。 */
static void xtw_cwOnColumnInserted(XTableWidget* self, int column)
{
    int i;
    if (!self) return;
    for (i = 0; i < self->m_cellWidgetCount; ++i)
        if (self->m_cellWidgets[i].column >= column)
            self->m_cellWidgets[i].column++;
}

/** @brief 行删除后平移挂载表：row 行条目解除，>row 行条目行号减一。 */
static void xtw_cwOnRowRemoved(XTableWidget* self, int row)
{
    int i;
    if (!self) return;
    for (i = self->m_cellWidgetCount - 1; i >= 0; --i) {
        if (self->m_cellWidgets[i].row == row) xtw_cwRemoveAt(self, i);
        else if (self->m_cellWidgets[i].row > row)
            self->m_cellWidgets[i].row--;
    }
}

/** @brief 列删除后平移挂载表：column 列条目解除，>column 列条目列号减一。 */
static void xtw_cwOnColumnRemoved(XTableWidget* self, int column)
{
    int i;
    if (!self) return;
    for (i = self->m_cellWidgetCount - 1; i >= 0; --i) {
        if (self->m_cellWidgets[i].column == column) xtw_cwRemoveAt(self, i);
        else if (self->m_cellWidgets[i].column > column)
            self->m_cellWidgets[i].column--;
    }
}

/** @brief 行数收缩：解除行号 >= keepRows 的挂载条目。 */
static void xtw_cwTruncateRows(XTableWidget* self, int keepRows)
{
    int i;
    if (!self) return;
    for (i = self->m_cellWidgetCount - 1; i >= 0; --i)
        if (self->m_cellWidgets[i].row >= keepRows) xtw_cwRemoveAt(self, i);
}

/** @brief 列数收缩：解除列号 >= keepColumns 的挂载条目。 */
static void xtw_cwTruncateColumns(XTableWidget* self, int keepColumns)
{
    int i;
    if (!self) return;
    for (i = self->m_cellWidgetCount - 1; i >= 0; --i)
        if (self->m_cellWidgets[i].column >= keepColumns)
            xtw_cwRemoveAt(self, i);
}

/** @brief 发射 (int,int) 双整型信号（cellClicked/currentCellChanged）。 */
static void xtw_emitCellSignal(XTableWidget* self, size_t signal, int a, int b)
{
    int va = a;
    int vb = b;
    XVarList* args = XVarList_Create(XVar(int, va), XVar(int, vb));
    if (!args) return;
    if (self && ((XObject*)self)->m_signalSlot)
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    else
        XVarList_delete(args);
}

/** @brief 发射 item 载荷信号。 */
static void xtw_emitItemSignal(XTableWidget* self, size_t signal,
                               const XTableWidgetItem* item)
{
    XVarList* args = XVarList_Create(XVar(const XTableWidgetItem*, item));
    if (!args) return;
    if (self && ((XObject*)self)->m_signalSlot)
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    else
        XVarList_delete(args);
}

/** @brief 发射无载荷信号。 */
static void xtw_emit0(XTableWidget* self, size_t signal)
{
    XVarList* args = XVarList_create(0);
    if (!args) return;
    if (self && ((XObject*)self)->m_signalSlot)
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    else
        XVarList_delete(args);
}

/* ==================== 生命周期 ==================== */

static void VXTableWidget_deinit(XTableWidget* self);

XVtable* XTableWidget_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XTableWidget)
    XVTABLE_INHERIT_XCLASS(XTableView);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXTableWidget_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VX_tableWidget_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MousePressEvent,
                             VX_tableWidget_mousePressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseDoubleClickEvent,
                             VX_tableWidget_mouseDoubleClickEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_KeyPressEvent,
                             VX_tableWidget_keyPressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractScrollArea_ScrollContentsBy,
                             VX_tableWidget_scrollContentsBy);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseMoveEvent,
                             VX_tableWidget_mouseMoveEvent);
    return XVTABLE_DEFAULT;
}

/** @brief 释放一行单元格（含 XString 文本）。 */
static void xtw_freeRowItems(XTableWidgetItem* row, int cols)
{
    int i;
    if (!row) return;
    for (i = 0; i < cols; ++i)
        if (row[i].text) {
            XString_delete_base(row[i].text);
            row[i].text = NULL;
        }
    XFree_System(row);
}

static void VXTableWidget_deinit(XTableWidget* self)
{
    int i;
    if (!self) return;
    if (self->m_cells) {
        for (i = 0; i < self->m_rows; ++i)
            xtw_freeRowItems(self->m_cells[i], self->m_base.m_colCapacity);
        XFree_System(self->m_cells);
        self->m_cells = NULL;
    }
    if (self->m_hHeaders) {
        for (i = 0; i < self->m_base.m_colCapacity; ++i) {
            if (self->m_hHeaders[i]) {
                XString_delete_base(self->m_hHeaders[i]);
                self->m_hHeaders[i] = NULL;
            }
        }
        XFree_System(self->m_hHeaders);
        self->m_hHeaders = NULL;
    }
    if (self->m_vHeaders) {
        for (i = 0; i < self->m_vHeaderCapacity; ++i) {
            if (self->m_vHeaders[i]) {
                XString_delete_base(self->m_vHeaders[i]);
                self->m_vHeaders[i] = NULL;
            }
        }
        XFree_System(self->m_vHeaders);
        self->m_vHeaders = NULL;
    }
    if (self->m_model) {
        XClass_delete_base((XClass*)self->m_model);
        self->m_model = NULL;
    }
    if (self->m_cellWidgets) {
        /* 条目数组本身归表格所有；widget 为借用，不在此删除。 */
        XFree_System(self->m_cellWidgets);
        self->m_cellWidgets = NULL;
    }
    self->m_cellWidgetCount = 0;
    self->m_cellWidgetCapacity = 0;
    XClass_Deinit_Parent(XTableView, (XTableView*)self);
}

void XTableWidget_init(XTableWidget* self, XWidget* parent, XWidgetFlags flags)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XTableView_init(&self->m_base, parent, flags);
    self->m_model = XAbstractItemModel_create();
    if (self->m_model)
        XAbstractItemView_setModel(&self->m_base.m_base, self->m_model);
    XClassSetVtable(self, XTableWidget);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    self->m_base.m_base.m_currentRow = -1;
    self->m_base.m_base.m_currentColumn = -1;
    self->m_base.m_rowHeight = XTW_DEFAULT_ROW_HEIGHT;
    self->m_headerHeight = XTW_HEADER_H;
    self->m_headerWidth = XTW_HEADER_W;
    self->m_base.m_gridVisible = true;
    self->m_base.m_sortOrder = 0;
    self->m_enteredRow = -2;
    self->m_enteredColumn = -2;
    self->m_selectionRow = -1;
    self->m_selectionColumn = -1;
}

XTableWidget* XTableWidget_create_ex(XMemoryType memory, XWidget* parent,
                                     XWidgetFlags flags)
{
    XTableWidget* self = (XTableWidget*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XTableWidget_init(self, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 尺寸 ==================== */

XAbstractItemModel* XTableWidget_model(const XTableWidget* self)
{
    return self ? self->m_model : NULL;
}

void XTableWidget_setRowCount(XTableWidget* self, int rows)
{
    int i;
    if (!self || rows < 0 || rows == self->m_rows) return;
    if (rows > self->m_rows) {
        xtw_ensureRows(self, rows);
        xtw_ensureCols(self, self->m_columns > 0 ? self->m_columns : 1);
        for (i = self->m_rows; i < rows; ++i)
            self->m_cells[i] = xtw_newRow(self->m_base.m_colCapacity > 0 ?
                                         self->m_base.m_colCapacity : 1);
    }
    if (rows < self->m_rows) xtw_cwTruncateRows(self, rows); /* 收缩：解除越界挂载。 */
    self->m_rows = rows;
    if (self->m_base.m_base.m_currentRow >= rows) self->m_base.m_base.m_currentRow = rows - 1;
    xtw_updateContentSize(self);
    XWidget_update((XWidget*)self);
}

int XTableWidget_rowCount(const XTableWidget* self) { return self ? self->m_rows : 0; }

void XTableWidget_setColumnCount(XTableWidget* self, int columns)
{
    int i;
    if (!self || columns < 0 || columns == self->m_columns) return;
    xtw_ensureRows(self, self->m_rows > 0 ? self->m_rows : 1);
    if (self->m_rows == 0) self->m_rows = 0;
    if (columns > self->m_columns) {
        xtw_ensureCols(self, columns);
        for (i = 0; i < self->m_rows; ++i) {
            if (self->m_cells[i] == NULL)
                self->m_cells[i] = xtw_newRow(columns);
            XMemset(&self->m_cells[i][self->m_columns], 0,
                   sizeof(XTableWidgetItem) * (size_t)(columns - self->m_columns));
        }
    }
    if (columns < self->m_columns) xtw_cwTruncateColumns(self, columns); /* 收缩：解除越界挂载。 */
    self->m_columns = columns;
    if (self->m_base.m_base.m_currentColumn >= columns) self->m_base.m_base.m_currentColumn = columns - 1;
    xtw_updateContentSize(self);
    XWidget_update((XWidget*)self);
    if (self->m_model)
        XAbstractItemModel_setDimension(self->m_model, self->m_rows,
                                        self->m_columns);
}

int XTableWidget_columnCount(const XTableWidget* self) { return self ? self->m_columns : 0; }

void XTableWidget_insertRow(XTableWidget* self, int row)
{
    int i;
    if (!self || row < 0 || row > self->m_rows) return;
    xtw_ensureRows(self, self->m_rows + 1);
    xtw_ensureCols(self, self->m_columns > 0 ? self->m_columns : 1);
    self->m_cells[self->m_rows] = xtw_newRow(self->m_base.m_colCapacity);
    for (i = self->m_rows; i > row; --i) self->m_cells[i] = self->m_cells[i-1];
    self->m_cells[row] = xtw_newRow(self->m_base.m_colCapacity);
    self->m_rows++;
    xtw_cwOnRowInserted(self, row); /* 挂载表随行号平移。 */
    xtw_updateContentSize(self);
    XWidget_update((XWidget*)self);
    if (self->m_model)
        XAbstractItemModel_setDimension(self->m_model, self->m_rows,
                                        self->m_columns);
}

void XTableWidget_insertColumn(XTableWidget* self, int column)
{
    if (!self || column < 0 || column > self->m_columns) return;
    self->m_columns++;
    xtw_ensureCols(self, self->m_columns);
    xtw_cwOnColumnInserted(self, column); /* 挂载表随列号平移。 */
    xtw_updateContentSize(self);
    XWidget_update((XWidget*)self);
    if (self->m_model)
        XAbstractItemModel_setDimension(self->m_model, self->m_rows,
                                        self->m_columns);
}

void XTableWidget_removeRow(XTableWidget* self, int row)
{
    int i;
    if (!self || row < 0 || row >= self->m_rows) return;
    for (i = row; i < self->m_rows - 1; ++i)
        self->m_cells[i] = self->m_cells[i+1];
    if (self->m_cells[self->m_rows - 1]) {
        xtw_freeRowItems(self->m_cells[self->m_rows - 1],
                         self->m_base.m_colCapacity);
        self->m_cells[self->m_rows - 1] = NULL;
    }
    self->m_rows--;
    xtw_cwOnRowRemoved(self, row); /* 挂载表随行号平移/解除。 */
    xtw_updateContentSize(self);
    XWidget_update((XWidget*)self);
    if (self->m_model)
        XAbstractItemModel_setDimension(self->m_model, self->m_rows,
                                        self->m_columns);
}

void XTableWidget_removeColumn(XTableWidget* self, int column)
{
    if (!self || column < 0 || column >= self->m_columns) return;
    self->m_columns--;
    xtw_cwOnColumnRemoved(self, column); /* 挂载表随列号平移/解除。 */
    xtw_updateContentSize(self);
    XWidget_update((XWidget*)self);
    if (self->m_model)
        XAbstractItemModel_setDimension(self->m_model, self->m_rows,
                                        self->m_columns);
}

/* ==================== 单元格 ==================== */

void XTableWidget_setItem(XTableWidget* self, int row, int column,
                          const XTableWidgetItem* item)
{
    XTableWidgetItem* cell = xtw_cell(self, row, column);
    if (!cell || !item) return;
    if (cell->text) {
        XString_delete_base(cell->text);
        cell->text = NULL;
    }
    if (item->text)
        cell->text = XString_create_copy(item->text);
    cell->selected = item->selected;
    cell->foreground = item->foreground;
    cell->background = item->background;
    cell->checkState = item->checkState;
    XWidget_update((XWidget*)self);
    if (self->m_model)
        XAbstractItemModel_setData(self->m_model, row, column, item->text);
}

const XTableWidgetItem* XTableWidget_item(const XTableWidget* self,
                                          int row, int column)
{
    return xtw_cell(self, row, column);
}

void XTableWidget_setText(XTableWidget* self, int row, int column,
                          const char* utf8)
{
    XTableWidgetItem* cell = xtw_cell(self, row, column);
    if (!cell || !utf8) return;
    if (!cell->text) cell->text = XString_create();
    if (cell->text)
        XString_assign_utf8(cell->text, utf8);
    xtw_emitCellSignal(self, (size_t)XTableWidget_cellChanged_signal,
                       row, column);
    xtw_emitItemSignal(self, (size_t)XTableWidget_itemChanged_signal, cell);
    XWidget_update((XWidget*)self);
    if (self->m_model)
        XAbstractItemModel_setData_2(self->m_model, row, column, utf8);
}

const char* XTableWidget_text(const XTableWidget* self, int row, int column)
{
    XTableWidgetItem* cell = xtw_cell(self, row, column);
    const char* text;
    if (!cell || !cell->text) return "";
    text = XString_toUtf8(cell->text);
    return text ? text : "";
}

void XTableWidget_setCurrentCell(XTableWidget* self, int row, int column)
{
    int prevR;
    int prevC;
    if (!self) return;
    prevR = self->m_base.m_base.m_currentRow;
    prevC = self->m_base.m_base.m_currentColumn;
    self->m_base.m_base.m_currentRow = row;
    self->m_base.m_base.m_currentColumn = column;
    XTableWidget_scrollToItem(self, row, column);
    xtw_emitCellSignal(self,
        (size_t)XTableWidget_currentCellChanged_signal, row, column);
    if (row != prevR || column != prevC) {
        xtw_emitItemSignal(self,
            (size_t)XTableWidget_currentItemChanged_signal,
            xtw_cell(self, row, column));
        if (self->m_selectionRow != row ||
            self->m_selectionColumn != column) {
            self->m_selectionRow = row;
            self->m_selectionColumn = column;
            xtw_emit0(self,
                (size_t)XTableWidget_itemSelectionChanged_signal);
        }
    }
    XWidget_update((XWidget*)self);
}

int XTableWidget_currentRow(const XTableWidget* self) { return self ? self->m_base.m_base.m_currentRow : -1; }
int XTableWidget_currentColumn(const XTableWidget* self) { return self ? self->m_base.m_base.m_currentColumn : -1; }

const XTableWidgetItem* XTableWidget_currentItem(const XTableWidget* self)
{
    if (!self) return NULL;
    /* 行/列号为 -1（无当前）时 xtw_cell 直接返回 NULL。 */
    return xtw_cell(self, self->m_base.m_base.m_currentRow,
                    self->m_base.m_base.m_currentColumn);
}

void XTableWidget_setCurrentItem(XTableWidget* self, int row, int column)
{
    /* 目标格不存在（无 item）时忽略；存在则转发 setCurrentCell
       （滚动可见并发射 currentCellChanged 等信号）。 */
    if (!xtw_cell(self, row, column)) return;
    XTableWidget_setCurrentCell(self, row, column);
}

/* ==================== 单元格部件挂载（借用语义） ==================== */

void XTableWidget_setCellWidget(XTableWidget* self, int row, int column,
                                XWidget* widget)
{
    int idx;
    if (!self || !xtw_cell(self, row, column)) return;
    idx = xtw_cwFind(self, row, column);
    if (!widget) {
        /* NULL 等价 removeCellWidget。 */
        if (idx >= 0) xtw_cwRemoveAt(self, idx);
        return;
    }
    if (idx >= 0) {
        /* 重复挂载覆盖旧指针（旧部件仅解除关联，不被删除）。 */
        self->m_cellWidgets[idx].widget = widget;
        return;
    }
    if (!xtw_cwReserve(self, self->m_cellWidgetCount + 1)) return;
    self->m_cellWidgets[self->m_cellWidgetCount].row = row;
    self->m_cellWidgets[self->m_cellWidgetCount].column = column;
    self->m_cellWidgets[self->m_cellWidgetCount].widget = widget;
    self->m_cellWidgetCount++;
}

XWidget* XTableWidget_cellWidget(const XTableWidget* self, int row, int column)
{
    int idx = self ? xtw_cwFind(self, row, column) : -1;
    return idx >= 0 ? self->m_cellWidgets[idx].widget : NULL;
}

void XTableWidget_removeCellWidget(XTableWidget* self, int row, int column)
{
    int idx;
    if (!self) return;
    idx = xtw_cwFind(self, row, column);
    if (idx >= 0) xtw_cwRemoveAt(self, idx);
}

/* ==================== 表头 ==================== */

void XTableWidget_setHorizontalHeaderLabels(XTableWidget* self,
                                            const char* const* labels, int count)
{
    int i;
    if (!self || !labels) return;
    xtw_ensureCols(self, count);
    for (i = 0; i < count && i < self->m_base.m_colCapacity; ++i) {
        if (!self->m_hHeaders[i])
            self->m_hHeaders[i] = XString_create();
        if (self->m_hHeaders[i])
            XString_assign_utf8(self->m_hHeaders[i], labels[i]);
        if (self->m_model)
            XAbstractItemModel_setHeaderData_2(self->m_model, i, 0,
                                               labels[i]);
    }
    XWidget_update((XWidget*)self);
}

void XTableWidget_setVerticalHeaderLabels(XTableWidget* self,
                                          const char* const* labels, int count)
{
    int i;
    if (!self || !labels) return;
    if (!xtw_ensureVHeaders(self, count)) return;
    for (i = 0; i < count; ++i) {
        if (!self->m_vHeaders[i])
            self->m_vHeaders[i] = XString_create();
        if (self->m_vHeaders[i])
            XString_assign_utf8(self->m_vHeaders[i], labels[i]);
    }
    XWidget_update((XWidget*)self);
}

const char* XTableWidget_horizontalHeaderItem(const XTableWidget* self, int column)
{
    const char* text;
    if (!self || column < 0 || column >= self->m_columns ||
        !self->m_hHeaders || !self->m_hHeaders[column])
        return "";
    text = XString_toUtf8(self->m_hHeaders[column]);
    return text ? text : "";
}

const char* XTableWidget_verticalHeaderItem(const XTableWidget* self, int row)
{
    const char* text;
    if (!self || row < 0 || !self->m_vHeaders ||
        row >= self->m_vHeaderCapacity || !self->m_vHeaders[row])
        return "";
    text = XString_toUtf8(self->m_vHeaders[row]);
    return text ? text : "";
}

void XTableWidget_setHorizontalHeaderItem(XTableWidget* self, int column,
                                          const XString* text)
{
    const char* utf8;
    if (!self || column < 0) return;
    xtw_ensureCols(self, column + 1); /* 确保表头/列宽/单元格槽位容量。 */
    if (column >= self->m_base.m_colCapacity || !self->m_hHeaders) return;
    if (!self->m_hHeaders[column])
        self->m_hHeaders[column] = XString_create();
    if (!self->m_hHeaders[column]) return;
    utf8 = text ? XString_toUtf8(text) : NULL; /* NULL 等价清空该列。 */
    XString_assign_utf8(self->m_hHeaders[column], utf8 ? utf8 : "");
    XWidget_update((XWidget*)self);
    if (self->m_model)
        XAbstractItemModel_setHeaderData_2(self->m_model, column, 0,
                                           utf8 ? utf8 : "");
}

void XTableWidget_setVerticalHeaderItem(XTableWidget* self, int row,
                                        const XString* text)
{
    const char* utf8;
    if (!self || row < 0) return;
    if (!xtw_ensureVHeaders(self, row + 1)) return;
    if (!self->m_vHeaders[row])
        self->m_vHeaders[row] = XString_create();
    if (!self->m_vHeaders[row]) return;
    utf8 = text ? XString_toUtf8(text) : NULL; /* NULL 等价清空该行。 */
    XString_assign_utf8(self->m_vHeaders[row], utf8 ? utf8 : "");
    XWidget_update((XWidget*)self);
}

XString* XTableWidget_takeHorizontalHeaderItem(XTableWidget* self, int column)
{
    XString* taken;
    if (!self || column < 0 || !self->m_hHeaders ||
        column >= self->m_base.m_colCapacity || !self->m_hHeaders[column])
        return NULL;
    taken = self->m_hHeaders[column];
    self->m_hHeaders[column] = NULL; /* 所有权移交调用方（XString_delete_base 释放）。 */
    XWidget_update((XWidget*)self);
    if (self->m_model)
        XAbstractItemModel_setHeaderData_2(self->m_model, column, 0, "");
    return taken;
}

XString* XTableWidget_takeVerticalHeaderItem(XTableWidget* self, int row)
{
    XString* taken;
    if (!self || row < 0 || !self->m_vHeaders ||
        row >= self->m_vHeaderCapacity || !self->m_vHeaders[row])
        return NULL;
    taken = self->m_vHeaders[row];
    self->m_vHeaders[row] = NULL; /* 所有权移交调用方（XString_delete_base 释放）。 */
    XWidget_update((XWidget*)self);
    return taken;
}

/* ==================== 行为 ==================== */

void XTableWidget_sortItems(XTableWidget* self, int column, int order)
{
    int i;
    int j;
    if (!self || column < 0 || column >= self->m_columns) return;
    for (i = 0; i < self->m_rows; ++i) {
        for (j = 0; j < self->m_rows - 1 - i; ++j) {
            const char* a = self->m_cells[j][column].text
                ? XString_toUtf8(self->m_cells[j][column].text) : "";
            const char* b = self->m_cells[j+1][column].text
                ? XString_toUtf8(self->m_cells[j+1][column].text) : "";
            bool swap = (order == 0) ? (XStrcmp(a, b) > 0) : (XStrcmp(a, b) < 0);
            if (swap) {
                XTableWidgetItem tmp;
                int k;
                for (k = 0; k < self->m_columns; ++k) {
                    tmp = self->m_cells[j][k];
                    self->m_cells[j][k] = self->m_cells[j+1][k];
                    self->m_cells[j+1][k] = tmp;
                }
            }
        }
    }
    self->m_base.m_sortColumn = column;
    self->m_base.m_sortOrder = order;
    XWidget_update((XWidget*)self);
}

void XTableWidget_clear(XTableWidget* self)
{
    int i;
    if (!self) return;
    if (self->m_cells) {
        /* 只遍历有效行（m_rows 内）；容量区的行指针未初始化，
           不得访问（xtw_ensureRows 未清零新指针区）。 */
        for (i = 0; i < self->m_rows; ++i) {
            int k;
            if (!self->m_cells[i]) continue;
            for (k = 0; k < self->m_base.m_colCapacity; ++k) {
                if (self->m_cells[i][k].text) {
                    XString_delete_base(self->m_cells[i][k].text);
                    self->m_cells[i][k].text = NULL;
                }
            }
            XFree_System(self->m_cells[i]);
            self->m_cells[i] = NULL;
        }
        XFree_System(self->m_cells);
        self->m_cells = NULL;
        self->m_rowCapacity = 0;
    }
    /* 表头文本一并清空（对标 Qt clear()：同时移除表头）；
       数组容量保留，文本指针置空。 */
    if (self->m_hHeaders) {
        for (i = 0; i < self->m_base.m_colCapacity; ++i) {
            if (self->m_hHeaders[i]) {
                XString_delete_base(self->m_hHeaders[i]);
                self->m_hHeaders[i] = NULL;
            }
        }
    }
    if (self->m_vHeaders) {
        for (i = 0; i < self->m_vHeaderCapacity; ++i) {
            if (self->m_vHeaders[i]) {
                XString_delete_base(self->m_vHeaders[i]);
                self->m_vHeaders[i] = NULL;
            }
        }
    }
    self->m_rows = 0;
    self->m_columns = 0;
    self->m_base.m_base.m_currentRow = -1;
    self->m_base.m_base.m_currentColumn = -1;
    self->m_selectionRow = -1;      /* 选区跟踪一并复位。 */
    self->m_selectionColumn = -1;
    xtw_cwClear(self);              /* 挂载条目全部解除（借用部件不销毁）。 */
    XWidget_update((XWidget*)self);
    if (self->m_model)
        XAbstractItemModel_setDimension(self->m_model, 0, 0);
}

void XTableWidget_clearContents(XTableWidget* self)
{
    int i;
    if (!self) return;
    for (i = 0; i < self->m_rows; ++i) {
        int k;
        if (!self->m_cells[i]) continue;
        for (k = 0; k < self->m_base.m_colCapacity; ++k) {
            if (self->m_cells[i][k].text) {
                XString_delete_base(self->m_cells[i][k].text);
                self->m_cells[i][k].text = NULL;
            }
        }
        XMemset(self->m_cells[i], 0,
               sizeof(XTableWidgetItem) * (size_t)self->m_base.m_colCapacity);
    }
    XWidget_update((XWidget*)self);
    if (self->m_model)
        XAbstractItemModel_setDimension(self->m_model, self->m_rows,
                                        self->m_columns);
}

void XTableWidget_scrollToItem(XTableWidget* self, int row, int column)
{
    /* EnsureVisible 语义（对标 QTableWidget::scrollToItem 默认提示）：
       目标行已完整可见则不动；否则最小滚动使其贴视口顶/底，并把
       偏移钳位到内容范围内。此前直接把 vo 设为 row*rowHeight，目标
       行被顶到视口顶部、上一行压进表头带。 */
    XScrollBar* vbar;
    int viewportH;
    int rowTop;
    int rowBottom;
    int maxVo;
    int vo;
    (void)column;
    if (!self || row < 0) return;
    vbar = XAbstractScrollArea_verticalScrollBar(
        (XAbstractScrollArea*)self);
    if (!vbar) return;
    viewportH = XWidget_height((XWidget*)self) - self->m_headerHeight;
    if (viewportH <= 0) return;
    rowTop = self->m_headerHeight + row * self->m_base.m_rowHeight;
    rowBottom = rowTop + self->m_base.m_rowHeight;
    vo = xtw_vOffset(self);
    if (rowBottom - vo > XWidget_height((XWidget*)self))
        vo = rowBottom - XWidget_height((XWidget*)self);
    if (rowTop - vo < self->m_headerHeight)
        vo = rowTop - self->m_headerHeight;
    /* 滚动条范围由内容尺寸驱动，是可滚范围的权威表达：内容不满时
       max=0，本函数收敛为空操作（对标 Qt value 恒在 [0, max] 内）。 */
    maxVo = XAbstractSlider_maximum((XAbstractSlider*)vbar);
    if (vo > maxVo) vo = maxVo;
    if (vo < 0) vo = 0;
    XAbstractSlider_setValue((XAbstractSlider*)vbar, vo);
}

/* ==================== 便捷族（对标 QTableWidget 便捷接口） ==================== */

/** @brief 命中坐标写入调用方双数组（行/列同下标推进；容量受限）。 */
static bool xtw_storeIndex(int row, int col, int* outRows, int* outColumns,
                           int maxCount, int* written)
{
    if (*written >= maxCount || (!outRows && !outColumns)) return false;
    if (outRows) outRows[*written] = row;
    if (outColumns) outColumns[*written] = col;
    ++(*written);
    return true;
}

int XTableWidget_findItems(const XTableWidget* self, const char* text,
                           int flags, int* outRows, int* outColumns,
                           int maxCount)
{
    int total = 0;
    int written = 0;
    int row;
    int col;
    if (!self) return 0;
    for (row = 0; row < self->m_rows; ++row) {
        for (col = 0; col < self->m_columns; ++col) {
            const XTableWidgetItem* cell = xtw_cell(self, row, col);
            const char* cellText = "";
            bool hit;
            if (cell && cell->text) {
                const char* s = XString_toUtf8(cell->text);
                if (s) cellText = s;
            }
            if (flags == 1) /* 精确相等（对标 Qt::MatchExactly）。 */
                hit = (XStrcmp(cellText, text ? text : "") == 0);
            else            /* 包含子串（对标 Qt::MatchContains）。 */
                hit = (XStrstr(cellText, text ? text : "") != NULL);
            if (!hit) continue;
            xtw_storeIndex(row, col, outRows, outColumns, maxCount, &written);
            ++total;
        }
    }
    return total;
}

void XTableWidget_itemAt(const XTableWidget* self, int x, int y,
                         int* row, int* column)
{
    int hitRow = -1;
    int hitCol = -1;
    if (row) *row = -1;
    if (column) *column = -1;
    if (!self) return;
    xtw_cellAt(self, x, y, &hitRow, &hitCol);
    if (row) *row = hitRow;
    if (column) *column = hitCol;
}

int XTableWidget_selectedIndexes(const XTableWidget* self, int* outRows,
                                 int* outColumns, int maxCount)
{
    int total = 0;
    int written = 0;
    int row;
    int col;
    bool trackedListed = false;
    if (!self) return 0;
    /* 1) per-cell selected 标记（setItem 拷入时保留）。 */
    for (row = 0; row < self->m_rows; ++row) {
        for (col = 0; col < self->m_columns; ++col) {
            const XTableWidgetItem* cell = xtw_cell(self, row, col);
            if (!cell || !cell->selected) continue;
            if (row == self->m_selectionRow &&
                col == self->m_selectionColumn)
                trackedListed = true;
            xtw_storeIndex(row, col, outRows, outColumns, maxCount, &written);
            ++total;
        }
    }
    /* 2) 当前跟踪选区未被标记覆盖时补录（点击/键盘选择路径）。 */
    if (!trackedListed && self->m_selectionRow >= 0 &&
        self->m_selectionRow < self->m_rows &&
        self->m_selectionColumn >= 0 &&
        self->m_selectionColumn < self->m_columns) {
        xtw_storeIndex(self->m_selectionRow, self->m_selectionColumn,
                       outRows, outColumns, maxCount, &written);
        ++total;
    }
    return total;
}

int XTableWidget_selectedItems(const XTableWidget* self, int* outRows,
                               int* outCols, int maxCount)
{
    /* 平铺模型每格恒有 item 记录（无独立 item 生命周期）：
       selectedItems 与 selectedIndexes 语义重合，直接转发。 */
    return XTableWidget_selectedIndexes(self, outRows, outCols, maxCount);
}

/** @brief 选中范围写入调用方四数组（同下标对应同一范围；容量受限）。 */
static void xtw_writeRange(int idx, int top, int left, int bottom, int right,
                           int* outTop, int* outLeft, int* outBottom,
                           int* outRight, int maxCount)
{
    if (idx >= maxCount) return;
    if (!outTop && !outLeft && !outBottom && !outRight) return;
    if (outTop) outTop[idx] = top;
    if (outLeft) outLeft[idx] = left;
    if (outBottom) outBottom[idx] = bottom;
    if (outRight) outRight[idx] = right;
}

int XTableWidget_selectedRanges(const XTableWidget* self, int* outTop,
                                int* outLeft, int* outBottom, int* outRight,
                                int maxCount)
{
    int total = 0;
    int row;
    int col;
    bool trackedListed = false;
    int lastLeft = -1;
    int lastBottom = -2;
    int lastRight = -1;
    if (!self) return 0;
    /* 1) per-cell selected 标记：逐行取 [最小,最大] 选中列为跨度，
       行连续且跨度一致的范围原地合并（对标 QTableWidgetSelectionRange）。 */
    for (row = 0; row < self->m_rows; ++row) {
        int left = -1;
        int right = -1;
        for (col = 0; col < self->m_columns; ++col) {
            const XTableWidgetItem* cell = xtw_cell(self, row, col);
            if (!cell || !cell->selected) continue;
            if (left < 0) left = col;
            right = col;
            if (row == self->m_selectionRow &&
                col == self->m_selectionColumn)
                trackedListed = true;
        }
        if (left < 0) continue;
        if (lastBottom == row - 1 && lastLeft == left && lastRight == right) {
            /* 与最近范围可合并：扩展结束行（已写出者同步改写）。 */
            lastBottom = row;
            if (outBottom && total - 1 < maxCount) outBottom[total - 1] = row;
            continue;
        }
        lastLeft = left;
        lastBottom = row;
        lastRight = right;
        xtw_writeRange(total, row, left, row, right,
                       outTop, outLeft, outBottom, outRight, maxCount);
        ++total;
    }
    /* 2) 当前跟踪选区未被标记覆盖时补录 1x1 范围（同 selectedIndexes 口径）。 */
    if (!trackedListed && self->m_selectionRow >= 0 &&
        self->m_selectionRow < self->m_rows &&
        self->m_selectionColumn >= 0 &&
        self->m_selectionColumn < self->m_columns) {
        xtw_writeRange(total, self->m_selectionRow, self->m_selectionColumn,
                       self->m_selectionRow, self->m_selectionColumn,
                       outTop, outLeft, outBottom, outRight, maxCount);
        ++total;
    }
    return total;
}

void XTableWidget_clearSpans(XTableWidget* self)
{
    if (!self) return;
    XTableView_clearSpans((XTableView*)self);
}

XString* XTableWidget_takeItem(XTableWidget* self, int row, int column)
{
    XTableWidgetItem* cell = xtw_cell(self, row, column);
    XString* taken;
    if (!cell || !cell->text) return NULL;
    taken = cell->text;
    cell->text = NULL; /* 所有权移交调用方（以 XString_delete_base 释放）。 */
    xtw_emitCellSignal(self, (size_t)XTableWidget_cellChanged_signal,
                       row, column);
    xtw_emitItemSignal(self, (size_t)XTableWidget_itemChanged_signal, cell);
    XWidget_update((XWidget*)self);
    if (self->m_model)
        XAbstractItemModel_setData_2(self->m_model, row, column, "");
    return taken;
}

int XTableWidget_indexFromItem(const XTableWidget* self,
                               const XTableWidgetItem* item, int* column)
{
    int row;
    int col;
    if (column) *column = -1;
    if (!self || !item || !self->m_cells) return -1;
    /* 单元格内嵌存储：指针恒等于 &m_cells[row][col]，按地址查表定位。 */
    for (row = 0; row < self->m_rows; ++row) {
        if (!self->m_cells[row]) continue;
        for (col = 0; col < self->m_columns; ++col) {
            if (&self->m_cells[row][col] == item) {
                if (column) *column = col;
                return row;
            }
        }
    }
    return -1;
}

/* ==================== 索引反查/编辑（对标 row/column/editItem/itemFromIndex/items） ==================== */

int XTableWidget_column(const XTableWidget* self, int column)
{
    if (!self || column < 0 || column >= self->m_columns) return -1;
    return column; /* 恒等映射：索引即坐标（见头文件 @note）。 */
}

int XTableWidget_row(const XTableWidget* self, int row)
{
    if (!self || row < 0 || row >= self->m_rows) return -1;
    return row; /* 恒等映射：索引即坐标（见头文件 @note）。 */
}

void XTableWidget_editItem(XTableWidget* self, int row, int column)
{
    /* 对齐 Qt editItem(item) → edit(index)：坐标有效时转发基类编辑
       触发判定（编辑器体系未建，判定后预留返回，见 @note）；
       坐标越界或该格不存在时整体忽略。 */
    if (!self || !xtw_cell(self, row, column)) return;
    (void)XAbstractItemView_edit(&self->m_base.m_base, row, column);
}

XString* XTableWidget_itemFromIndex(const XTableWidget* self, int row)
{
    const XTableWidgetItem* cell;
    XString* copy;
    if (!self || row < 0 || row >= self->m_rows) return NULL;
    cell = xtw_cell(self, row, 0); /* “行文本”取该行首列（列 0）。 */
    copy = XString_create();
    if (!copy) return NULL;
    if (cell && cell->text) {
        const char* utf8 = XString_toUtf8(cell->text);
        if (utf8) XString_assign_utf8(copy, utf8);
    }
    return copy;
}

XVector* XTableWidget_items(const XTableWidget* self, const char* text)
{
    XVector* rows;
    int row;
    int col;
    if (!self) return NULL;
    rows = XVector_create(sizeof(int));
    if (!rows) return NULL;
    for (row = 0; row < self->m_rows; ++row) {
        for (col = 0; col < self->m_columns; ++col) {
            const XTableWidgetItem* cell = xtw_cell(self, row, col);
            const char* cellText = "";
            int rowNo;
            if (cell && cell->text) {
                const char* s = XString_toUtf8(cell->text);
                if (s) cellText = s;
            }
            /* 精确相等（区分大小写）；行内任一列命中即整行命中。 */
            if (XStrcmp(cellText, text ? text : "") != 0) continue;
            rowNo = row;
            if (!XVector_push_back_1_base(rows, &rowNo)) {
                /* 分配失败：容器归还调用方语义不成立，整体置空返回 NULL。 */
                XVector_delete_base(rows);
                return NULL;
            }
            break; /* 每行至多输出一次。 */
        }
    }
    return rows;
}

/* ==================== 原型（体系未建：不透明承载） ==================== */

const XTableWidgetItem* XTableWidget_itemPrototype(const XTableWidget* self)
{
    return self ? self->m_itemPrototype : NULL;
}

void XTableWidget_setItemPrototype(XTableWidget* self,
                                   const XTableWidgetItem* prototype)
{
    /* 借用 + 不透明记录：不拷贝、不参与行为（详见头文件 @note）。 */
    if (self) self->m_itemPrototype = prototype;
}

/* ==================== 视觉序与几何 ==================== */

int XTableWidget_visualRow(const XTableWidget* self, int visualRow)
{
    (void)self;
    return visualRow; /* 平铺模型无隐藏/重排映射：视觉序与逻辑序恒等。 */
}

int XTableWidget_visualColumn(const XTableWidget* self, int visualColumn)
{
    (void)self;
    return visualColumn; /* 平铺模型无隐藏/重排映射：视觉序与逻辑序恒等。 */
}

XRect XTableWidget_visualItemRect(const XTableWidget* self,
                                  const XTableWidgetItem* item)
{
    XRect rect;
    int column = -1;
    int row;
    XRect_init(&rect, 0, 0, 0, 0);
    if (!self || !item) return rect;
    row = XTableWidget_indexFromItem(self, item, &column);
    if (row < 0 || column < 0) return rect;
    XRect_init(&rect,
               xtw_colX(self, column) - xtw_hOffset(self),
               self->m_headerHeight + row * self->m_base.m_rowHeight
                   - xtw_vOffset(self),
               self->m_base.m_colWidths[column],
               self->m_base.m_rowHeight);
    return rect;
}

void XTableWidget_setRangeSelected(XTableWidget* self, int topRow,
                                   int leftCol, int bottomRow, int rightCol,
                                   bool select)
{
    int tmp;
    int row;
    int col;
    bool changed = false;
    if (!self) return;
    /* 归一化倒置范围（对标 QTableWidgetSelectionRange）。 */
    if (topRow > bottomRow) { tmp = topRow; topRow = bottomRow; bottomRow = tmp; }
    if (leftCol > rightCol) { tmp = leftCol; leftCol = rightCol; rightCol = tmp; }
    for (row = topRow; row <= bottomRow; ++row) {
        for (col = leftCol; col <= rightCol; ++col) {
            XTableWidgetItem* cell = xtw_cell(self, row, col);
            if (!cell || cell->selected == select) continue;
            cell->selected = select;
            changed = true;
        }
    }
    if (!changed) return;
    xtw_emit0(self, (size_t)XTableWidget_itemSelectionChanged_signal);
    XWidget_update((XWidget*)self);
}

/* ==================== 信号 ==================== */

void* XTableWidget_cellClicked_signal(XTableWidget* self)
{ (void)self; return (void*)(size_t)XTableWidget_cellClicked_signal; }
void* XTableWidget_cellDoubleClicked_signal(XTableWidget* self)
{ (void)self; return (void*)(size_t)XTableWidget_cellDoubleClicked_signal; }
void* XTableWidget_currentCellChanged_signal(XTableWidget* self)
{ (void)self; return (void*)(size_t)XTableWidget_currentCellChanged_signal; }
void* XTableWidget_itemChanged_signal(XTableWidget* self)
{ (void)self; return (void*)(size_t)XTableWidget_itemChanged_signal; }
void* XTableWidget_itemClicked_signal(XTableWidget* self)
{ (void)self; return (void*)(size_t)XTableWidget_itemClicked_signal; }
void* XTableWidget_itemDoubleClicked_signal(XTableWidget* self)
{ (void)self; return (void*)(size_t)XTableWidget_itemDoubleClicked_signal; }
void* XTableWidget_itemPressed_signal(XTableWidget* self)
{ (void)self; return (void*)(size_t)XTableWidget_itemPressed_signal; }
void* XTableWidget_itemEntered_signal(XTableWidget* self)
{ (void)self; return (void*)(size_t)XTableWidget_itemEntered_signal; }
void* XTableWidget_itemActivated_signal(XTableWidget* self)
{ (void)self; return (void*)(size_t)XTableWidget_itemActivated_signal; }
void* XTableWidget_cellPressed_signal(XTableWidget* self)
{ (void)self; return (void*)(size_t)XTableWidget_cellPressed_signal; }
void* XTableWidget_cellEntered_signal(XTableWidget* self)
{ (void)self; return (void*)(size_t)XTableWidget_cellEntered_signal; }
void* XTableWidget_cellActivated_signal(XTableWidget* self)
{ (void)self; return (void*)(size_t)XTableWidget_cellActivated_signal; }
void* XTableWidget_cellChanged_signal(XTableWidget* self)
{ (void)self; return (void*)(size_t)XTableWidget_cellChanged_signal; }
void* XTableWidget_currentItemChanged_signal(XTableWidget* self)
{ (void)self; return (void*)(size_t)XTableWidget_currentItemChanged_signal; }
void* XTableWidget_itemSelectionChanged_signal(XTableWidget* self)
{ (void)self; return (void*)(size_t)XTableWidget_itemSelectionChanged_signal; }


/* ==================== 渲染与交互 ==================== */

/** @brief 计算列 x 起点（含水平滚动偏移）。 */
static int xtw_colX(const XTableWidget* self, int col)
{
    int i;
    int x = self->m_headerWidth;
    for (i = 0; i < col; ++i) x += self->m_base.m_colWidths[i];
    return x;
}

/** @brief 取滚动偏移（垂直/水平条值）。 */
static int xtw_vOffset(const XTableWidget* self)
{
    XScrollBar* bar = XAbstractScrollArea_verticalScrollBar(
        (XAbstractScrollArea*)self);
    return bar ? XAbstractSlider_value((XAbstractSlider*)bar) : 0;
}

static int xtw_hOffset(const XTableWidget* self)
{
    XScrollBar* bar = XAbstractScrollArea_horizontalScrollBar(
        (XAbstractScrollArea*)self);
    return bar ? XAbstractSlider_value((XAbstractSlider*)bar) : 0;
}

/** @brief 按当前行列尺寸同步滚动范围。
 *  @details 内容 = 表头带 + 全部行高 / 行号列 + 全部列宽。此前从不
 *           上报内容尺寸：滚动条按需隐藏，但范围停留在默认 0..99，
 *           滚轮仍可把内容滚出视口（内容未满也滚动、无滚动条可滚）。 */
static void xtw_updateContentSize(XTableWidget* self)
{
    int w;
    int h;
    int i;
    if (!self) return;
    h = self->m_headerHeight + self->m_rows * self->m_base.m_rowHeight;
    w = self->m_headerWidth;
    for (i = 0; i < self->m_columns; ++i)
        w += self->m_base.m_colWidths[i];
    XAbstractScrollArea_setContentSize((XAbstractScrollArea*)self, w, h);
}

/** @brief 绘制：表头 → 网格 → 单元格文本 → 选中高亮 → 当前框。 */
static void VX_tableWidget_paintEvent(XWidget* self, XEvent* event)
{
    XTableWidget* tw = (XTableWidget*)self;
    XPainter painter;
    XImage* image;
    XPoint offset;
    XRect dirty;
    XRect dataClip;
    uint32_t base;
    uint32_t dark;
    uint32_t light;
    uint32_t highlight;
    uint32_t highlightedText;
    uint32_t windowText;
    uint32_t button;
    int vo;
    int ho;
    int w;
    int h;
    int row;
    int col;
    if (!tw || !event) return;
    w = XWidget_width(self);
    h = XWidget_height(self);
    if (w <= 2 || h <= 2) return;
    image = XWidget_paintImage(self);
    if (!image) return;
    XPainter_init(&painter, NULL);
    if (!XPainter_begin_image(&painter, image)) {
        XPainter_deinit(&painter);
        return;
    }
    offset = XWidget_paintOffset(self);
    if (offset.x != 0 || offset.y != 0)
        XPainter_translate(&painter, (float)offset.x, (float)offset.y);
    base  = xtw_color(tw, XPaletteColorRole_Base);
    dark  = xtw_color(tw, XPaletteColorRole_Dark);
    light = xtw_color(tw, XPaletteColorRole_Light);
    highlight = xtw_color(tw, XPaletteColorRole_Highlight);
    highlightedText = xtw_color(tw, XPaletteColorRole_HighlightedText);
    windowText = xtw_color(tw, XPaletteColorRole_WindowText);
    button = xtw_color(tw, XPaletteColorRole_Button);
    vo = xtw_vOffset(tw);
    ho = xtw_hOffset(tw);
    /* 脏区裁剪（对标 Qt drawWidget 的 systemClip）：非 PAINT 入口
       （repaint/grab）按整控件处理。稳态小脏区刷新不再全表重绘。 */
    if (XEvent_type(event) == XEVENT_TYPE_PAINT)
        dirty = XPaintEvent_rect((const XPaintEvent*)event);
    else
        XRect_init(&dirty, 0, 0, w, h);
#if XPAINTER_CLIP_ON
    XPainter_setClipRect(&painter, &dirty,
                         XPainterClipOperation_ReplaceClip);
#endif /* XPAINTER_CLIP_ON */
    /* 1) 底色（裁剪已把填充限制在脏区内）。 */
    XPainter_fillRect(&painter, &dirty, base);
    /* 2) 水平表头。 */
    XPainter_fillRect(&painter,
        &(XRect){0, 0, w, tw->m_headerHeight}, button);
    XPainter_setPen(&painter, dark);
    XPainter_drawLine(&painter, 0, tw->m_headerHeight - 1,
                      w, tw->m_headerHeight - 1);
    if (dirty.y < tw->m_headerHeight) {
    for (col = 0; col < tw->m_columns; ++col) {
        int cx = xtw_colX(tw, col) - ho;
        int cw = tw->m_base.m_colWidths[col];
        XStyle* style = NULL;
        if (cx + cw < 0 || cx > w) continue;
#if XSTYLE_ON
        style = XStyle_defaultStyle();
#endif
        if (style != NULL) {
            /* Fusion/公共风格接管：列头走 CE_HeaderSection/HeaderLabel。 */
            XStyleOption hs;
            XStyleOption_init(&hs, XStyleCE_HeaderSection);
            XRect_init(&hs.m_rect, cx, 0, cw, tw->m_headerHeight);
            hs.m_state = XWidget_isEnabled((XWidget*)tw)
                ? XStyleState_Enabled : 0;
            hs.m_text = XTableWidget_horizontalHeaderItem(tw, col);
#if XPALETTE_ON
            hs.m_palette = XWidget_palette((XWidget*)tw);
#endif
            XStyle_drawControl(style, XStyleCE_HeaderSection, &hs,
                               &painter, (XWidget*)tw);
            hs.m_type = XStyleCE_HeaderLabel;
            XStyle_drawControl(style, XStyleCE_HeaderLabel, &hs,
                               &painter, (XWidget*)tw);
            continue;
        }
        XPainter_drawText(&painter, cx + 4, tw->m_headerHeight - 8,
                          XTableWidget_horizontalHeaderItem(tw, col),
                          windowText);
        XPainter_drawLine(&painter, cx + cw - 1, 0,
                          cx + cw - 1, tw->m_headerHeight - 1);
    }
    } /* 脏区不在表头带时整段列头跳过 */
    /* 3) 数据区：裁剪到表头带之下——滚出视口顶的行（部分行）只画
       出表头以下部分，不得覆盖表头文字（对标 Qt 表头/内容分域）。 */
    XRect_init(&dataClip, 0, tw->m_headerHeight, w,
               h - tw->m_headerHeight);
#if XPAINTER_CLIP_ON
    if (dataClip.height > 0)
        XPainter_setClipRect(&painter, &dataClip,
                             XPainterClipOperation_IntersectClip);
#endif /* XPAINTER_CLIP_ON */
    if (dataClip.height > 0) {
    {
        /* 可视行范围按脏区 y 计算：小脏区刷新不再遍历全部行。 */
        int first = (dirty.y + vo - tw->m_headerHeight) /
                    (tw->m_base.m_rowHeight > 0 ? tw->m_base.m_rowHeight : 1);
        int last = (dirty.y + dirty.height + vo - tw->m_headerHeight) /
                   (tw->m_base.m_rowHeight > 0 ? tw->m_base.m_rowHeight : 1);
        if (first < 0) first = 0;
        if (last > tw->m_rows - 1) last = tw->m_rows - 1;
        row = first < 0 ? 0 : first;
        for (; row <= last && row < tw->m_rows; ++row) {
        int cy = tw->m_headerHeight + row * tw->m_base.m_rowHeight - vo;
        XTableWidgetItem* vItem;
        if (cy + tw->m_base.m_rowHeight < tw->m_headerHeight || cy > h) continue;
        vItem = NULL;
        for (col = 0; col < tw->m_columns; ++col) {
            XTableWidgetItem* cell = xtw_cell(tw, row, col);
            int cx = xtw_colX(tw, col) - ho;
            int cw = tw->m_base.m_colWidths[col];
            XRect cellRect;
            XRect_init(&cellRect, cx, cy, cw, tw->m_base.m_rowHeight);
            if (cellRect.x + cellRect.width < 0 || cellRect.x > w) continue;
            if (cell && (cell->selected ||
                (row == tw->m_base.m_base.m_currentRow && col == tw->m_base.m_base.m_currentColumn))) {
                XPainter_fillRect(&painter, &cellRect, highlight);
            }
            if (cell && cell->background != 0)
                XPainter_fillRect(&painter, &cellRect, cell->background);
            if (cell && cell->text && XString_toUtf8(cell->text) &&
                XString_toUtf8(cell->text)[0])
                XPainter_drawText(&painter, cellRect.x + 4,
                                  cellRect.y + tw->m_base.m_rowHeight - 8,
                                  XString_toUtf8(cell->text),
                                  (cell->selected ||
                                   (row == tw->m_base.m_base.m_currentRow &&
                                    col == tw->m_base.m_base.m_currentColumn))
                                      ? highlightedText
                                      : (cell->foreground != 0
                                           ? cell->foreground : windowText));
            if (tw->m_base.m_gridVisible) {
                XPainter_setPen(&painter, dark);
                XPainter_drawLine(&painter, cellRect.x,
                                  cellRect.y + cellRect.height - 1,
                                  cellRect.x + cellRect.width - 1,
                                  cellRect.y + cellRect.height - 1);
                XPainter_drawLine(&painter, cellRect.x + cellRect.width - 1,
                                  cellRect.y, cellRect.x + cellRect.width - 1,
                                  cellRect.y + cellRect.height - 1);
            }
        }
        /* 垂直表头在单元格之后绘制：横向滚动时单元格平移不会盖住
           行号列（行号列钉在内容区左缘，对标 Qt 表头子控件层级）。 */
        XPainter_fillRect(&painter,
            &(XRect){0, cy, tw->m_headerWidth, tw->m_base.m_rowHeight}, button);
        XPainter_drawText(&painter, 4, cy + tw->m_base.m_rowHeight - 8,
                          XTableWidget_verticalHeaderItem(tw, row),
                          windowText);
        XPainter_drawLine(&painter, 0, cy + tw->m_base.m_rowHeight - 1,
                          tw->m_headerWidth - 1, cy + tw->m_base.m_rowHeight - 1);
    }
    }
    }
    XPainter_end(&painter);
    XPainter_deinit(&painter);
}

/** @brief 滚动内容变化：重绘（网格随滚动条偏移平移）。 */
static void VX_tableWidget_scrollContentsBy(XAbstractScrollArea* self,
                                            int dx, int dy)
{
    (void)dx; (void)dy;
    if (self) XWidget_update((XWidget*)self);
}

/** @brief 坐标 → 单元格（返回行列；表头/越界返回 -1）。 */
static void xtw_cellAt(const XTableWidget* self, int x, int y,
                       int* row, int* col)
{
    int i;
    int acc;
    *row = -1; *col = -1;
    if (x < self->m_headerWidth || y < self->m_headerHeight) return;
    acc = self->m_headerWidth - xtw_hOffset(self);
    for (i = 0; i < self->m_columns; ++i) {
        int cw = self->m_base.m_colWidths[i];
        if (x >= acc && x < acc + cw) { *col = i; break; }
        acc += cw;
    }
    acc = self->m_headerHeight - xtw_vOffset(self);
    for (i = 0; i < self->m_rows; ++i) {
        if (y >= acc && y < acc + self->m_base.m_rowHeight) { *row = i; break; }
        acc += self->m_base.m_rowHeight;
    }
}

/** @brief 按下：选中单元格 + cellClicked。 */
static void VX_tableWidget_mousePressEvent(XWidget* self, XEvent* event)
{
    XTableWidget* tw = (XTableWidget*)self;
    XMouseEvent* me = (XMouseEvent*)event;
    int row;
    int col;
    if (!tw || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_PRESS) return;
    if (XMouseEvent_button(me) != XMouseButton_LeftButton) return;
    xtw_cellAt(tw, XMouseEvent_position(me).x, XMouseEvent_position(me).y,
               &row, &col);
    if (row >= 0 && col >= 0) {
        XTableWidget_setCurrentCell(tw, row, col);
        xtw_emitCellSignal(tw,
            (size_t)XTableWidget_cellPressed_signal, row, col);
        xtw_emitItemSignal(tw,
            (size_t)XTableWidget_itemClicked_signal,
            xtw_cell(tw, row, col));
        xtw_emitCellSignal(tw,
            (size_t)XTableWidget_cellClicked_signal, row, col);
    }
    XEvent_accept(event);
}

/** @brief 移动：进入新单元格发射 cellEntered/itemEntered。 */
static void VX_tableWidget_mouseMoveEvent(XWidget* self, XEvent* event)
{
    XTableWidget* tw = (XTableWidget*)self;
    XMouseEvent* me = (XMouseEvent*)event;
    int row;
    int col;
    if (!tw || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_MOVE) return;
    xtw_cellAt(tw, XMouseEvent_position(me).x, XMouseEvent_position(me).y,
               &row, &col);
    if (row >= 0 && col >= 0) {
        if (row != tw->m_enteredRow || col != tw->m_enteredColumn) {
            tw->m_enteredRow = row;
            tw->m_enteredColumn = col;
            xtw_emitCellSignal(tw,
                (size_t)XTableWidget_cellEntered_signal, row, col);
            xtw_emitItemSignal(tw,
                (size_t)XTableWidget_itemEntered_signal,
                xtw_cell(tw, row, col));
        }
    }
    XEvent_accept(event);
}

/** @brief 双击：cellDoubleClicked。 */
static void VX_tableWidget_mouseDoubleClickEvent(XWidget* self, XEvent* event)
{
    XTableWidget* tw = (XTableWidget*)self;
    XMouseEvent* me = (XMouseEvent*)event;
    int row;
    int col;
    if (!tw || !event || XEvent_type(event) !=
        XEVENT_TYPE_MOUSE_BUTTON_DBL_CLICK) return;
    xtw_cellAt(tw, XMouseEvent_position(me).x, XMouseEvent_position(me).y,
               &row, &col);
    if (row >= 0 && col >= 0)
        xtw_emitCellSignal(tw,
            (size_t)XTableWidget_cellDoubleClicked_signal, row, col);
    XEvent_accept(event);
}

/** @brief 键盘：方向键移动当前单元格（对标 QTableView 键盘导航）。 */
static void VX_tableWidget_keyPressEvent(XWidget* self, XEvent* event)
{
    XTableWidget* tw = (XTableWidget*)self;
    XKeyEvent* ke = (XKeyEvent*)event;
    int row;
    int col;
    int key;
    if (!tw || !event || XEvent_type(event) != XEVENT_TYPE_KEY_PRESS) return;
    key = XKeyEvent_key(ke);
    row = tw->m_base.m_base.m_currentRow;
    col = tw->m_base.m_base.m_currentColumn;
    if (row < 0 || col < 0) return;
    switch (key) {
    case XKey_Up:    row = row > 0 ? row - 1 : 0; break;
    case XKey_Down:  row = row < tw->m_rows - 1 ? row + 1 : row; break;
    case XKey_Left:  col = col > 0 ? col - 1 : 0; break;
    case XKey_Right: col = col < tw->m_columns - 1 ? col + 1 : col; break;
    default: return;
    }
    XTableWidget_setCurrentCell(tw, row, col);
    XEvent_accept(event);
}

static void VX_tableWidget_paintEvent(XWidget* self, XEvent* event);
static void VX_tableWidget_mousePressEvent(XWidget* self, XEvent* event);
static void VX_tableWidget_mouseDoubleClickEvent(XWidget* self, XEvent* event);
static void VX_tableWidget_mouseMoveEvent(XWidget* self, XEvent* event);
static void VX_tableWidget_keyPressEvent(XWidget* self, XEvent* event);
/** @brief 调色板取色助手。 */
static uint32_t xtw_color(const XTableWidget* self, XPaletteColorRole role)
{
#if XPALETTE_ON
    XPalette palette = XWidget_palette((XWidget*)self);
    XColor c = XPalette_color(&palette, XPaletteColorGroup_Current, role);
    return XColor_rgba(&c);
#else
    (void)self; (void)role;
    return 0xFF000000u;
#endif
}

#endif /* XTABLEWIDGET_ON */