#include "XTableWidget.h"
#include "XStringUtils.h"

#include "XAlgorithm.h"
#include "XStyle.h"
#include "XStyleOption.h"
#include "XMemory.h"
#include "XWidget_Protected.h"
#include "XEvent.h"
#include "XPainter.h"
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

/** @brief 取单元格（越界返回 NULL）。 */
static XTableWidgetItem* xtw_cell(const XTableWidget* self, int row, int col)
{
    if (!self || row < 0 || row >= self->m_rows ||
        col < 0 || col >= self->m_columns || !self->m_cells) return NULL;
    return &self->m_cells[row][col];
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
    self->m_rows = rows;
    if (self->m_base.m_base.m_currentRow >= rows) self->m_base.m_base.m_currentRow = rows - 1;
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
    self->m_columns = columns;
    if (self->m_base.m_base.m_currentColumn >= columns) self->m_base.m_base.m_currentColumn = columns - 1;
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
    XWidget_update((XWidget*)self);
    if (self->m_model)
        XAbstractItemModel_setDimension(self->m_model, self->m_rows,
                                        self->m_columns);
}

void XTableWidget_removeColumn(XTableWidget* self, int column)
{
    if (!self || column < 0 || column >= self->m_columns) return;
    self->m_columns--;
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
    if (count > self->m_vHeaderCapacity) {
        int cap = self->m_vHeaderCapacity > 0
            ? self->m_vHeaderCapacity : 256;
        XString** vh;
        while (cap < count) cap *= 2;
        vh = (XString**)XRealloc_System(self->m_vHeaders,
                                        sizeof(XString*) * (size_t)cap);
        if (!vh) return;
        self->m_vHeaders = vh;
        self->m_vHeaderCapacity = cap;
    }
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
    self->m_rows = 0;
    self->m_columns = 0;
    self->m_base.m_base.m_currentRow = -1;
    self->m_base.m_base.m_currentColumn = -1;
    XWidget_update((XWidget*)self);
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
    /* 简化：滚动条值按单元格位置换算（首个参数对齐 Qt ScrollHint）。 */
    XScrollBar* vbar;
    int y;
    if (!self || row < 0) return;
    vbar = XAbstractScrollArea_verticalScrollBar(
        (XAbstractScrollArea*)self);
    if (!vbar) return;
    y = self->m_headerHeight + row * self->m_base.m_rowHeight;
    XAbstractSlider_setValue((XAbstractSlider*)vbar,
                             y - self->m_headerHeight);
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

/** @brief 绘制：表头 → 网格 → 单元格文本 → 选中高亮 → 当前框。 */
static void VX_tableWidget_paintEvent(XWidget* self, XEvent* event)
{
    XTableWidget* tw = (XTableWidget*)self;
    XPainter painter;
    XImage* image;
    XPoint offset;
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
    /* 1) 底色。 */
    XPainter_fillRect(&painter,
        &(XRect){0, 0, w, h}, base);
    /* 2) 水平表头。 */
    XPainter_fillRect(&painter,
        &(XRect){0, 0, w, tw->m_headerHeight}, button);
    XPainter_setPen(&painter, dark);
    XPainter_drawLine(&painter, 0, tw->m_headerHeight - 1,
                      w, tw->m_headerHeight - 1);
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
    /* 3) 垂直表头 + 网格 + 单元格。 */
    for (row = 0; row < tw->m_rows; ++row) {
        int cy = tw->m_headerHeight + row * tw->m_base.m_rowHeight - vo;
        XTableWidgetItem* vItem;
        if (cy + tw->m_base.m_rowHeight < tw->m_headerHeight || cy > h) continue;
        vItem = NULL;
        XPainter_fillRect(&painter,
            &(XRect){0, cy, tw->m_headerWidth, tw->m_base.m_rowHeight}, button);
        XPainter_drawText(&painter, 4, cy + tw->m_base.m_rowHeight - 8,
                          XTableWidget_verticalHeaderItem(tw, row),
                          windowText);
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
        XPainter_drawLine(&painter, 0, cy + tw->m_base.m_rowHeight - 1,
                          tw->m_headerWidth - 1, cy + tw->m_base.m_rowHeight - 1);
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