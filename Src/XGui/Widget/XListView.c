/**
 * @file       XListView.c
 * @brief      XListView 列表视图实现（model 单列垂直渲染）。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XListView.h"

#include "XAlgorithm.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XPainter.h"

#if XWIDGET_ON && XTABLEWIDGET_ON

#define XLISTVIEW_DEFAULT_ROW_H 24

static void VXListView_deinit(XListView* self);
static void VXListView_paintEvent(XWidget* self, XEvent* event);

/* 命中测试覆盖：列表按行渲染（列固定 modelColumn）。 */
static bool xlv_indexAt(XListView* self, int x, int y,
                        int* outRow, int* outCol)
{
    int row;
    int rh;
    (void)x;
    if (!self) return false;
    rh = self->m_rowHeight > 0 ? self->m_rowHeight
                               : XLISTVIEW_DEFAULT_ROW_H;
    row = y / (rh + self->m_spacing);
    if (outRow) *outRow = row;
    if (outCol) *outCol = self->m_modelColumn;
    return row >= 0;
}

static void VXListView_paintEvent(XWidget* self, XEvent* event)
{
    XListView* lv = (XListView*)self;
    XAbstractItemView* view = &lv->m_base;
    XAbstractItemModel* model = view->m_model;
    XImage* image;
    XPainter painter;
    XRect r;
    int row;
    int rows;
    int y;
    int rh;
    (void)event;
    if (!lv) return;
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
        XPainter_end(&painter);
        XPainter_deinit(&painter);
        return;
    }
    rows = model->m_rows;
    rh = lv->m_rowHeight > 0 ? lv->m_rowHeight : XLISTVIEW_DEFAULT_ROW_H;
    y = 0;
    for (row = 0; row < rows && y < r.height; ++row) {
        int h = rh + lv->m_spacing;
        XRect cell = { 0, y, r.width, rh };
        bool sel = view->m_selectionModel &&
                   XItemSelectionModel_isSelected(
                       view->m_selectionModel, row, lv->m_modelColumn);
        bool cur = (view->m_currentRow == row &&
                    view->m_currentColumn == lv->m_modelColumn);
        if (sel)
            XPainter_fillRect(&painter, &cell, 0xFFCCE4FFu);
        else if (view->m_alternatingRowColors && (row & 1))
            XPainter_fillRect(&painter, &cell, 0xFFF7F7F7u);
        if (cur && !sel)
            XPainter_fillRect(&painter, &cell, 0xFFE8F1FFu);
        {
            const char* text = XAbstractItemModel_data_2(
                model, row, lv->m_modelColumn);
            if (text && text[0]) {
                XPainter_setPen(&painter, 0xFF000000u);
                XPainter_drawText(&painter, 4, y + rh - 6, text, 0);
            }
        }
        if (lv->m_spacing > 0) {
            XPainter_setPen(&painter, 0xFFDDDDDDu);
            XPainter_drawLine(&painter, 0, y + rh, r.width, y + rh);
        }
        y += h;
    }
    XPainter_end(&painter);
    XPainter_deinit(&painter);
}

/* 命中测试虚槽：列表视图重写基类 indexAt（基类为网格 80x24）。 */
static bool VXListView_indexAt(const XAbstractItemView* view, int x, int y,
                               int* outRow, int* outCol)
{
    return xlv_indexAt((XListView*)view, x, y, outRow, outCol);
}

XVtable* XListView_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XListView)
    XVTABLE_INHERIT_XCLASS(XAbstractItemView);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXListView_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VXListView_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractItemView_IndexAt, VXListView_indexAt);
    return XVTABLE_DEFAULT;
}

void XListView_init(XListView* self, XWidget* parent, XWidgetFlags flags)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XAbstractItemView_init(&self->m_base, parent, flags);
    XClassSetVtable(self, XListView);
    self->m_spacing = 0;
    self->m_modelColumn = 0;
    self->m_rowHeight = XLISTVIEW_DEFAULT_ROW_H;
}

XListView* XListView_create_ex(XMemoryType memory, XWidget* parent,
                               XWidgetFlags flags)
{
    XListView* self = (XListView*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XListView_init(self, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

static void VXListView_deinit(XListView* self)
{
    if (!self) return;
    XClass_Deinit_Parent(XAbstractItemView, (XAbstractItemView*)self);
}

void XListView_setSpacing(XListView* self, int spacing)
{
    if (self && spacing >= 0) {
        self->m_spacing = spacing;
        XWidget_update((XWidget*)self);
    }
}

int XListView_spacing(const XListView* self)
{ return self ? self->m_spacing : 0; }

void XListView_setModelColumn(XListView* self, int column)
{
    if (self && column >= 0) {
        self->m_modelColumn = column;
        XWidget_update((XWidget*)self);
    }
}

int XListView_modelColumn(const XListView* self)
{ return self ? self->m_modelColumn : 0; }

void XListView_setRowHeight(XListView* self, int height)
{
    if (self && height > 0) {
        self->m_rowHeight = height;
        XWidget_update((XWidget*)self);
    }
}

int XListView_rowHeight(const XListView* self)
{
    return self ? self->m_rowHeight : XLISTVIEW_DEFAULT_ROW_H;
}

#endif /* XWIDGET_ON && XTABLEWIDGET_ON */
