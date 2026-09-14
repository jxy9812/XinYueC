#include "XTableView.h"

#include "XAlgorithm.h"
#include "XMemory.h"
#include "XClass.h"

#if XTABLEWIDGET_ON || 1

#define XTV_DEFAULT_COL_WIDTH 80
#define XTV_DEFAULT_ROW_HEIGHT 24

static void VXTableView_deinit(XTableView* self);

XVtable* XTableView_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XTableView)
    XVTABLE_INHERIT_XCLASS(XAbstractItemView);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXTableView_deinit);
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

#endif /* XTABLEWIDGET_ON || 1 */
