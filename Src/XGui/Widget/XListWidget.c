/**
 * @file       XListWidget.c
 * @brief      XListWidget 列表控件实现（内建 model 桥）。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XListWidget.h"

#include "XAlgorithm.h"
#include "XMemory.h"

#if XWIDGET_ON && XTABLEWIDGET_ON

static void VXListWidget_deinit(XListWidget* self);

XVtable* XListWidget_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XListWidget)
    XVTABLE_INHERIT_XCLASS(XListView);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXListWidget_deinit);
    return XVTABLE_DEFAULT;
}

void XListWidget_init(XListWidget* self, XWidget* parent,
                      XWidgetFlags flags)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XListView_init(&self->m_base, parent, flags);
    XClassSetVtable(self, XListWidget);
    self->m_model = XAbstractItemModel_create();
    if (self->m_model) {
        XAbstractItemModel_setDimension(self->m_model, 0, 1);
        XAbstractItemView_setModel(&self->m_base.m_base, self->m_model);
    }
}

XListWidget* XListWidget_create_ex(XMemoryType memory, XWidget* parent,
                                   XWidgetFlags flags)
{
    XListWidget* self =
        (XListWidget*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XListWidget_init(self, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

static void VXListWidget_deinit(XListWidget* self)
{
    if (!self) return;
    if (self->m_model) {
        XAbstractItemModel_delete_base(self->m_model);
        self->m_model = NULL;
    }
    XClass_Deinit_Parent(XListView, (XListView*)self);
}

int XListWidget_addItem(XListWidget* self, const XString* text)
{
    if (!self || !self->m_model) return -1;
    return XListWidget_insertItem(self, self->m_model->m_rows, text);
}

int XListWidget_addItem_2(XListWidget* self, const char* text)
{
    XString* tmp = NULL;
    int row;
    if (!text) return -1;
    tmp = XString_create_utf8(text);
    if (!tmp) return -1;
    row = XListWidget_addItem(self, tmp);
    XString_delete_base(tmp);
    return row;
}

int XListWidget_insertItem(XListWidget* self, int row, const XString* text)
{
    int rows;
    int i;
    if (!self || !self->m_model || !text) return -1;
    rows = XAbstractItemModel_rowCount(self->m_model);
    if (row < 0) row = 0;
    if (row > rows) row = rows;
    /* 简单实现：追加到末尾（行内插入为后续扩展）。 */
    (void)i;
    XAbstractItemModel_setDimension(self->m_model, rows + 1, 1);
    if (!XAbstractItemModel_setData(self->m_model, row, 0, text)) {
        XAbstractItemModel_setDimension(self->m_model, rows, 1);
        return -1;
    }
    XWidget_update((XWidget*)self);
    return row;
}

int XListWidget_insertItem_2(XListWidget* self, int row, const char* text)
{
    XString* tmp = NULL;
    int out;
    if (!text) return -1;
    tmp = XString_create_utf8(text);
    if (!tmp) return -1;
    out = XListWidget_insertItem(self, row, tmp);
    XString_delete_base(tmp);
    return out;
}

int XListWidget_count(const XListWidget* self)
{
    return (self && self->m_model)
               ? XAbstractItemModel_rowCount(self->m_model) : 0;
}

const XString* XListWidget_item(const XListWidget* self, int row)
{
    return (self && self->m_model)
               ? XAbstractItemModel_data(self->m_model, row, 0) : NULL;
}

const char* XListWidget_item_2(const XListWidget* self, int row)
{
    return (self && self->m_model)
               ? XAbstractItemModel_data_2(self->m_model, row, 0) : "";
}

void XListWidget_takeItem(XListWidget* self, int row)
{
    int rows;
    if (!self || !self->m_model) return;
    rows = XAbstractItemModel_rowCount(self->m_model);
    if (row < 0 || row >= rows) return;
    /* 简单实现：清除该行文本并收缩行数（行内移动为后续扩展）。 */
    XAbstractItemModel_setDimension(self->m_model, rows - 1, 1);
    XWidget_update((XWidget*)self);
}

void XListWidget_clear(XListWidget* self)
{
    if (!self || !self->m_model) return;
    XAbstractItemModel_setDimension(self->m_model, 0, 1);
    XWidget_update((XWidget*)self);
}

int XListWidget_currentRow(const XListWidget* self)
{
    return self ? self->m_base.m_base.m_currentRow : -1;
}

void XListWidget_setCurrentRow(XListWidget* self, int row)
{
    if (!self) return;
    XAbstractItemView_setCurrentIndex(&self->m_base.m_base, row, 0);
    if (self->m_base.m_base.m_selectionModel && row >= 0) {
        XItemSelectionModel_select(self->m_base.m_base.m_selectionModel,
                                   row, 0, true);
        XItemSelectionModel_setCurrentIndex(
            self->m_base.m_base.m_selectionModel, row, 0);
    }
    XWidget_update((XWidget*)self);
}

XAbstractItemModel* XListWidget_model(const XListWidget* self)
{ return self ? self->m_model : NULL; }

#endif /* XWIDGET_ON && XTABLEWIDGET_ON */
