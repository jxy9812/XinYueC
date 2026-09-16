/**
 * @file       XTreeView.c
 * @brief      XTreeView 树视图实现（扁平 model 第一列渲染）。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XTreeView.h"

#include "XAlgorithm.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XPainter.h"

#if XWIDGET_ON && XTABLEWIDGET_ON

#define XTREEVIEW_DEFAULT_ROW_H 24
#define XTREEVIEW_HEADER_H 20

static void VXTreeView_deinit(XTreeView* self);
static void VXTreeView_paintEvent(XWidget* self, XEvent* event);
static bool VXTreeView_indexAt(const XAbstractItemView* view, int x, int y,
                               int* outRow, int* outCol);

XVtable* XTreeView_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XTreeView)
    XVTABLE_INHERIT_XCLASS(XAbstractItemView);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXTreeView_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VXTreeView_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractItemView_IndexAt, VXTreeView_indexAt);
    return XVTABLE_DEFAULT;
}

void XTreeView_init(XTreeView* self, XWidget* parent, XWidgetFlags flags)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XAbstractItemView_init(&self->m_base, parent, flags);
    XClassSetVtable(self, XTreeView);
    self->m_indentation = 20;
    self->m_headerHidden = false;
    self->m_rowHeight = XTREEVIEW_DEFAULT_ROW_H;
}

XTreeView* XTreeView_create_ex(XMemoryType memory, XWidget* parent,
                               XWidgetFlags flags)
{
    XTreeView* self = (XTreeView*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XTreeView_init(self, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

static void VXTreeView_deinit(XTreeView* self)
{
    if (!self) return;
    XClass_Deinit_Parent(XAbstractItemView, (XAbstractItemView*)self);
}

void XTreeView_setIndentation(XTreeView* self, int indentation)
{
    if (self && indentation >= 0) {
        self->m_indentation = indentation;
        XWidget_update((XWidget*)self);
    }
}

int XTreeView_indentation(const XTreeView* self)
{ return self ? self->m_indentation : 0; }

void XTreeView_setHeaderHidden(XTreeView* self, bool hidden)
{
    if (self) {
        self->m_headerHidden = hidden;
        XWidget_update((XWidget*)self);
    }
}

bool XTreeView_isHeaderHidden(const XTreeView* self)
{ return self ? self->m_headerHidden : false; }

void XTreeView_setRowHeight(XTreeView* self, int height)
{
    if (self && height > 0) {
        self->m_rowHeight = height;
        XWidget_update((XWidget*)self);
    }
}

int XTreeView_rowHeight(const XTreeView* self)
{ return self ? self->m_rowHeight : XTREEVIEW_DEFAULT_ROW_H; }

static bool VXTreeView_indexAt(const XAbstractItemView* view, int x, int y,
                               int* outRow, int* outCol)
{
    XTreeView* tv = (XTreeView*)view;
    int yAcc;
    int rh;
    if (outRow) *outRow = -1;
    if (outCol) *outCol = -1;
    if (!tv) return false;
    rh = tv->m_rowHeight > 0 ? tv->m_rowHeight : XTREEVIEW_DEFAULT_ROW_H;
    yAcc = y - (tv->m_headerHidden ? 0 : XTREEVIEW_HEADER_H);
    if (yAcc < 0) return false;
    if (outRow) *outRow = yAcc / rh;
    if (outCol) *outCol = 0;
    return true;
}

static void VXTreeView_paintEvent(XWidget* self, XEvent* event)
{
    XTreeView* tv = (XTreeView*)self;
    XAbstractItemView* view = &tv->m_base;
    XAbstractItemModel* model = view->m_model;
    XImage* image;
    XPainter painter;
    XRect r;
    int row;
    int rows;
    int y;
    int rh;
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
        XPainter_end(&painter);
        XPainter_deinit(&painter);
        return;
    }
    if (!tv->m_headerHidden) {
        XRect hr = { 0, 0, r.width, XTREEVIEW_HEADER_H };
        const char* text = XAbstractItemModel_headerData_2(model, 0, 0);
        char buf[16];
        XPainter_fillRect(&painter, &hr, 0xFFF0F0F0u);
        if (!text || !text[0]) {
            XSnprintf(buf, sizeof(buf), "1");
            text = buf;
        }
        XPainter_setPen(&painter, 0xFF444444u);
        XPainter_drawText(&painter, 4, XTREEVIEW_HEADER_H - 6, text, 0);
    }
    rows = model->m_rows;
    rh = tv->m_rowHeight > 0 ? tv->m_rowHeight : XTREEVIEW_DEFAULT_ROW_H;
    y = tv->m_headerHidden ? 0 : XTREEVIEW_HEADER_H;
    for (row = 0; row < rows && y < r.height; ++row) {
        XRect cell = { 0, y, r.width, rh };
        bool sel = view->m_selectionModel &&
                   XItemSelectionModel_isSelected(
                       view->m_selectionModel, row, 0);
        bool cur = (view->m_currentRow == row && view->m_currentColumn == 0);
        if (sel)
            XPainter_fillRect(&painter, &cell, 0xFFCCE4FFu);
        else if (view->m_alternatingRowColors && (row & 1))
            XPainter_fillRect(&painter, &cell, 0xFFF7F7F7u);
        if (cur && !sel)
            XPainter_fillRect(&painter, &cell, 0xFFE8F1FFu);
        {
            const char* text = XAbstractItemModel_data_2(model, row, 0);
            if (text && text[0]) {
                int indent = (tv->m_indentation > 0)
                                 ? tv->m_indentation : 0;
                XPainter_setPen(&painter, 0xFF000000u);
                XPainter_drawText(&painter, indent + 12, y + rh - 6, text, 0);
                /* 树枝指示（扁平模型统一画箭头占位）。 */
                XPainter_setPen(&painter, 0xFF888888u);
                XPainter_drawLine(&painter, indent + 4, y + rh / 2 - 3,
                                  indent + 4, y + rh / 2 + 3);
                XPainter_drawLine(&painter, indent + 2, y + rh / 2,
                                  indent + 6, y + rh / 2);
            }
        }
        XPainter_setPen(&painter, 0xFFDDDDDDu);
        XPainter_drawLine(&painter, 0, y + rh - 1, r.width, y + rh - 1);
        y += rh;
    }
    XPainter_end(&painter);
    XPainter_deinit(&painter);
}

#endif /* XWIDGET_ON && XTABLEWIDGET_ON */
