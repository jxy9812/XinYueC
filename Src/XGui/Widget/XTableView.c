#include "XTableView.h"

#include "XAlgorithm.h"
#include "XMemory.h"
#include "XClass.h"
#include "XPainter.h"
#include "XEvent.h"

#if XWIDGET_ON && XTABLEWIDGET_ON

#define XTV_DEFAULT_COL_WIDTH 80
#define XTV_DEFAULT_ROW_HEIGHT 24

static void VXTableView_deinit(XTableView* self);
static void VXTableView_paintEvent(XWidget* self, XEvent* event);
static bool VXTableView_indexAt(const XAbstractItemView* view, int x, int y,
                                int* outRow, int* outCol);

/** @brief 表头高度（水平表头区在上方）。 */
#define XTV_HEADER_H 20

XVtable* XTableView_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XTableView)
    XVTABLE_INHERIT_XCLASS(XAbstractItemView);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXTableView_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VXTableView_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractItemView_IndexAt, VXTableView_indexAt);
    return XVTABLE_DEFAULT;
}

void XTableView_init(XTableView* self, XWidget* parent, XWidgetFlags flags)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XAbstractItemView_init(&self->m_base, parent, flags);
    XClassSetVtable(self, XTableView);
    self->m_rowHeight = XTV_DEFAULT_ROW_HEIGHT;
    self->m_gridVisible = true;
    self->m_sortingEnabled = false;
    self->m_sortColumn = -1;
    self->m_sortOrder = 0;
}

XTableView* XTableView_create_ex(XMemoryType memory, XWidget* parent,
                                 XWidgetFlags flags)
{
    XTableView* self = (XTableView*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XTableView_init(self, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

static void VXTableView_deinit(XTableView* self)
{
    if (!self) return;
    if (self->m_colWidths) {
        XFree_System(self->m_colWidths);
        self->m_colWidths = NULL;
    }
    XClass_Deinit_Parent(XAbstractItemView, (XAbstractItemView*)self);
}

void XTableView_setColumnWidth(XTableView* self, int column, int width)
{
    if (!self || column < 0) return;
    if (column >= self->m_colCapacity) {
        int cap = self->m_colCapacity > 0 ? self->m_colCapacity : 4;
        int* w;
        while (cap <= column) cap *= 2;
        w = (int*)XRealloc_System(self->m_colWidths,
                                  sizeof(int) * (size_t)cap);
        if (!w) return;
        self->m_colWidths = w;
        self->m_colCapacity = cap;
    }
    self->m_colWidths[column] = width;
}

int XTableView_columnWidth(const XTableView* self, int column)
{
    if (!self || column < 0 || column >= self->m_colCapacity ||
        !self->m_colWidths)
        return XTV_DEFAULT_COL_WIDTH;
    return self->m_colWidths[column];
}

void XTableView_setRowHeight(XTableView* self, int height)
{
    if (self && height > 0) self->m_rowHeight = height;
}

int XTableView_rowHeight(const XTableView* self)
{
    return self ? self->m_rowHeight : XTV_DEFAULT_ROW_HEIGHT;
}

void XTableView_setShowGrid(XTableView* self, bool show)
{ if (self) self->m_gridVisible = show ? 1 : 0; }
bool XTableView_showGrid(const XTableView* self)
{ return self ? (self->m_gridVisible != 0) : true; }

void XTableView_setSortingEnabled(XTableView* self, bool enable)
{ if (self) self->m_sortingEnabled = enable; }
bool XTableView_isSortingEnabled(const XTableView* self)
{ return self ? self->m_sortingEnabled : false; }

void XTableView_sortByColumn(XTableView* self, int column, int order)
{
    if (!self || column < 0) return;
    self->m_sortColumn = column;
    self->m_sortOrder = order ? 1 : 0;
}

void XTableView_selectRow(XTableView* self, int row)
{
    if (!self || row < 0) return;
    XAbstractItemView_setCurrentIndex(&self->m_base, row,
                                      self->m_base.m_currentColumn);
}

void XTableView_selectColumn(XTableView* self, int column)
{
    if (!self || column < 0) return;
    XAbstractItemView_setCurrentIndex(&self->m_base,
                                      self->m_base.m_currentRow, column);
}

/* ==================== 命中测试（行高/列宽感知） ==================== */

static bool VXTableView_indexAt(const XAbstractItemView* view, int x, int y,
                                int* outRow, int* outCol)
{
    XTableView* tv = (XTableView*)view;
    int row;
    int col;
    int yAcc;
    int rh;
    if (outRow) *outRow = -1;
    if (outCol) *outCol = -1;
    if (!tv) return false;
    rh = tv->m_rowHeight > 0 ? tv->m_rowHeight : 24;
    yAcc = y - 20; /* 减去表头高度 */
    if (yAcc < 0) return false;
    row = yAcc / rh;
    col = 0;
    /* 列命中：累计列宽。 */
    {
        int xAcc = 0;
        int c = 0;
        int cols = view->m_model ? view->m_model->m_cols : 0;
        for (c = 0; c < cols; ++c) {
            int w = XTableView_columnWidth(tv, c);
            if (x < xAcc + w) break;
            xAcc += w;
        }
        col = (c < cols) ? c : (cols > 0 ? cols - 1 : 0);
    }
    if (row < 0 || col < 0) return false;
    if (view->m_model) {
        if (row >= view->m_model->m_rows) return false;
        if (col >= view->m_model->m_cols) return false;
    }
    if (outRow) *outRow = row;
    if (outCol) *outCol = col;
    return true;
}

/* ==================== 绘制（model 数据通路） ==================== */

static void VXTableView_paintEvent(XWidget* self, XEvent* event)
{
    XTableView* tv = (XTableView*)self;
    XAbstractItemView* view = &tv->m_base;
    XAbstractItemModel* model = view->m_model;
    XPainter painter;
    XImage* image;
    XRect r;
    int row;
    int col;
    int cols;
    int rows;
    int x;
    int y;
    (void)event;
    if (!tv) return;
    image = XWidget_paintImage(self);
    if (!image) return;
    XRect_init(&r, 0, 0, XWidget_width(self), XWidget_height(self));
    XPainter_init(&painter, NULL);
    if (!XPainter_begin_image(&painter, image)) {
        XPainter_deinit(&painter);
        return;
    }
    XPainter_fillRect(&painter, &r, 0xFFFFFFFFu);
    if (!model) {
        /* 无模型：画空表头提示。 */
        XPainter_setPen(&painter, 0xFF888888u);
        XPainter_drawText(&painter, 4, 16, "（无模型）", 0);
        XPainter_end(&painter);
        XPainter_deinit(&painter);
        return;
    }
    rows = model->m_rows;
    cols = model->m_cols;
    /* 表头区。 */
    {
        XRect hr = { 0, 0, r.width, XTV_HEADER_H };
        XPainter_fillRect(&painter, &hr, 0xFFF0F0F0u);
        x = 0;
        for (col = 0; col < cols && x < r.width; ++col) {
            int w = XTableView_columnWidth(tv, col);
            const char* text = XAbstractItemModel_headerData_2(
                model, col, 0);
            if (!text || !text[0]) {
                char buf[16];
                XSnprintf(buf, sizeof(buf), "%d", col + 1);
                text = buf;
            }
            XPainter_setPen(&painter, 0xFF444444u);
            XPainter_drawText(&painter, x + 4, XTV_HEADER_H - 6, text, 0);
            XPainter_setPen(&painter, 0xFFCCCCCCu);
            XPainter_drawLine(&painter, x + w - 1, 1, x + w - 1,
                              XTV_HEADER_H - 1);
            x += w;
        }
    }
    /* 数据区网格。 */
    y = XTV_HEADER_H;
    for (row = 0; row < rows && y < r.height; ++row) {
        int rh = tv->m_rowHeight > 0 ? tv->m_rowHeight : 24;
        x = 0;
        for (col = 0; col < cols && x < r.width; ++col) {
            int w = XTableView_columnWidth(tv, col);
            bool sel = view->m_selectionModel &&
                       XItemSelectionModel_isSelected(
                           view->m_selectionModel, row, col);
            XRect cell = { x, y, w, rh };
            if (sel)
                XPainter_fillRect(&painter, &cell, 0xFFCCE4FFu);
            else if (view->m_alternatingRowColors && (row & 1))
                XPainter_fillRect(&painter, &cell, 0xFFF7F7F7u);
            {
                const char* text = XAbstractItemModel_data_2(model, row, col);
                if (text && text[0]) {
                    XPainter_setPen(&painter, 0xFF000000u);
                    XPainter_drawText(&painter, x + 4, y + rh - 6, text, 0);
                }
            }
            if (tv->m_gridVisible) {
                XPainter_setPen(&painter, 0xFFDDDDDDu);
                XPainter_drawLine(&painter, x + w - 1, y, x + w - 1,
                                  y + rh - 1);
                XPainter_drawLine(&painter, x, y + rh - 1, x + w - 1,
                                  y + rh - 1);
            }
            x += w;
        }
        y += rh;
    }
    XPainter_end(&painter);
    XPainter_deinit(&painter);
}

#endif /* XWIDGET_ON && XTABLEWIDGET_ON */
