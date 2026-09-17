/**
 * @file       XTreeView.c
 * @brief      XTreeView 树视图实现（扁平 model 第一列渲染）。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XTreeView.h"

#include "XAlgorithm.h"
#include "XStringUtils.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XPainter.h"
#include "XVarList.h"
#include "XWidget_Protected.h"

#if XWIDGET_ON && XTABLEWIDGET_ON

#define XTREEVIEW_DEFAULT_ROW_H 24
#define XTREEVIEW_HEADER_H 20

static void VXTreeView_deinit(XTreeView* self);
static void VXTreeView_paintEvent(XWidget* self, XEvent* event);
static bool VXTreeView_indexAt(const XAbstractItemView* view, int x, int y,
                               int* outRow, int* outCol);
static void VXTreeView_copy(XTreeView* self, const XTreeView* other);
static void VXTreeView_move(XTreeView* self, XTreeView* other);

/* ==================== 内部辅助 ==================== */

/** @brief 发射 int 信号（载荷单值；参照 XComboBox/xcombo_emitInt）。 */
static void xtv_emitInt(XTreeView* self, size_t signal, int value)
{
    XVarList* arguments = XVarList_Create(XVar(int, value));
    if (!arguments) return;
    if (((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, arguments, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(arguments);
    }
}

/** @brief 查询模型行数（无模型返回 0）。 */
static int xtv_modelRows(const XTreeView* self)
{
    XAbstractItemModel* model;
    if (!self) return 0;
    model = self->m_base.m_model;
    return model ? model->m_rows : 0;
}

/** @brief 查询模型列数（无模型返回 0）。 */
static int xtv_modelCols(const XTreeView* self)
{
    XAbstractItemModel* model;
    if (!self) return 0;
    model = self->m_base.m_model;
    return model ? model->m_cols : 0;
}

/** @brief 行展开状态表与模型行数同步（扩容新增行默认折叠）。
 * @param self 目标视图。
 * @param count 目标长度（<0 视为 0）。
 * @note 同步入口：expand 族按行访问前、绘制前；copy/move/deinit
 *       全路径另行接管数组生命周期（参照 XHeaderView m_hidden 模式）。
 */
static void xtv_syncRowStates(XTreeView* self, int count)
{
    int old;
    if (!self) return;
    if (count < 0) count = 0;
    old = self->m_rowStateCount;
    if (count == old) return;
    if (count <= 0) {
        if (self->m_expanded) {
            XFree_System(self->m_expanded);
            self->m_expanded = NULL;
        }
        self->m_rowStateCount = 0;
        return;
    }
    self->m_expanded = (bool*)XRealloc_System(
        self->m_expanded, sizeof(bool) * (size_t)count);
    if (self->m_expanded) {
        if (count > old)
            XMemset(self->m_expanded + old, 0,
                    sizeof(bool) * (size_t)(count - old));
        self->m_rowStateCount = count;
    } else {
        self->m_rowStateCount = 0;
    }
}

/** @brief 列状态表（隐藏/列宽）与目标列数同步。
 * @param self 目标视图。
 * @param count 目标长度（取模型列数与按列访问需求的最大值）。
 */
static void xtv_syncColumnStates(XTreeView* self, int count)
{
    int old;
    if (!self) return;
    if (count < 0) count = 0;
    old = self->m_columnStateCount;
    if (count == old) return;
    if (count <= 0) {
        if (self->m_columnHidden) {
            XFree_System(self->m_columnHidden);
            self->m_columnHidden = NULL;
        }
        if (self->m_columnWidths) {
            XFree_System(self->m_columnWidths);
            self->m_columnWidths = NULL;
        }
        self->m_columnStateCount = 0;
        return;
    }
    self->m_columnHidden = (bool*)XRealloc_System(
        self->m_columnHidden, sizeof(bool) * (size_t)count);
    self->m_columnWidths = (int*)XRealloc_System(
        self->m_columnWidths, sizeof(int) * (size_t)count);
    if (self->m_columnHidden && self->m_columnWidths) {
        if (count > old) {
            XMemset(self->m_columnHidden + old, 0,
                    sizeof(bool) * (size_t)(count - old));
            XMemset(self->m_columnWidths + old, 0,
                    sizeof(int) * (size_t)(count - old));
        }
        self->m_columnStateCount = count;
    } else {
        self->m_columnHidden = NULL;
        self->m_columnWidths = NULL;
        self->m_columnStateCount = 0;
    }
}

/** @brief 行展开状态表与模型行数同步入口（行数=模型行数）。 */
static void xtv_refreshRowStates(XTreeView* self)
{ xtv_syncRowStates(self, xtv_modelRows(self)); }

/** @brief 列状态表与模型列数同步入口（列数=模型列数）。 */
static void xtv_refreshColumnStates(XTreeView* self)
{ xtv_syncColumnStates(self, xtv_modelCols(self)); }

XVtable* XTreeView_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XTreeView)
    XVTABLE_INHERIT_XCLASS(XAbstractItemView);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXTreeView_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXTreeView_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXTreeView_move);
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
    self->m_expanded = NULL;
    self->m_rowStateCount = 0;
    self->m_columnHidden = NULL;
    self->m_columnWidths = NULL;
    self->m_columnStateCount = 0;
    self->m_expandsOnDoubleClick = true;
    self->m_itemsExpandable = true;
    self->m_rootIsDecorated = true;
    self->m_sortingEnabled = false;
    self->m_uniformRowHeights = false;
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
    if (self->m_expanded) {
        XFree_System(self->m_expanded);
        self->m_expanded = NULL;
    }
    self->m_rowStateCount = 0;
    if (self->m_columnHidden) {
        XFree_System(self->m_columnHidden);
        self->m_columnHidden = NULL;
    }
    if (self->m_columnWidths) {
        XFree_System(self->m_columnWidths);
        self->m_columnWidths = NULL;
    }
    self->m_columnStateCount = 0;
    XClass_Deinit_Parent(XAbstractItemView, (XAbstractItemView*)self);
}

/** @brief 深拷贝：基类拷贝后复制标量与展开/列状态平行数组。 */
static void VXTreeView_copy(XTreeView* self, const XTreeView* other)
{
    int n;
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XTreeView_init(self, NULL, 0);
    XClass_Parent(XAbstractItemView, EXClass_Copy,
                  void(*)(XAbstractItemView*, const XAbstractItemView*))(
                      (XAbstractItemView*)self,
                      (const XAbstractItemView*)other);
    self->m_indentation = other->m_indentation;
    self->m_headerHidden = other->m_headerHidden;
    self->m_rowHeight = other->m_rowHeight;
    /* 行展开状态表深拷贝。 */
    if (self->m_expanded) {
        XFree_System(self->m_expanded);
        self->m_expanded = NULL;
    }
    self->m_rowStateCount = 0;
    n = other->m_rowStateCount;
    if (n > 0 && other->m_expanded) {
        self->m_expanded = (bool*)XMalloc_System(sizeof(bool) * (size_t)n);
        if (self->m_expanded) {
            XMemmove(self->m_expanded, other->m_expanded,
                     sizeof(bool) * (size_t)n);
            self->m_rowStateCount = n;
        }
    }
    /* 列状态表（隐藏/列宽）深拷贝。 */
    if (self->m_columnHidden) {
        XFree_System(self->m_columnHidden);
        self->m_columnHidden = NULL;
    }
    if (self->m_columnWidths) {
        XFree_System(self->m_columnWidths);
        self->m_columnWidths = NULL;
    }
    self->m_columnStateCount = 0;
    n = other->m_columnStateCount;
    if (n > 0 && other->m_columnHidden && other->m_columnWidths) {
        self->m_columnHidden = (bool*)XMalloc_System(sizeof(bool) * (size_t)n);
        self->m_columnWidths = (int*)XMalloc_System(sizeof(int) * (size_t)n);
        if (self->m_columnHidden && self->m_columnWidths) {
            XMemmove(self->m_columnHidden, other->m_columnHidden,
                     sizeof(bool) * (size_t)n);
            XMemmove(self->m_columnWidths, other->m_columnWidths,
                     sizeof(int) * (size_t)n);
            self->m_columnStateCount = n;
        }
    }
    self->m_expandsOnDoubleClick = other->m_expandsOnDoubleClick;
    self->m_itemsExpandable = other->m_itemsExpandable;
    self->m_rootIsDecorated = other->m_rootIsDecorated;
    self->m_sortingEnabled = other->m_sortingEnabled;
    self->m_uniformRowHeights = other->m_uniformRowHeights;
}

/** @brief 移动语义：基类移动后转移状态数组，源对象归默认值。 */
static void VXTreeView_move(XTreeView* self, XTreeView* other)
{
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XTreeView_init(self, NULL, 0);
    XClass_Parent(XAbstractItemView, EXClass_Move,
                  void(*)(XAbstractItemView*, XAbstractItemView*))(
                      (XAbstractItemView*)self, (XAbstractItemView*)other);
    self->m_indentation = other->m_indentation;
    self->m_headerHidden = other->m_headerHidden;
    self->m_rowHeight = other->m_rowHeight;
    self->m_expanded = other->m_expanded;
    self->m_rowStateCount = other->m_rowStateCount;
    other->m_expanded = NULL;
    other->m_rowStateCount = 0;
    self->m_columnHidden = other->m_columnHidden;
    self->m_columnWidths = other->m_columnWidths;
    self->m_columnStateCount = other->m_columnStateCount;
    other->m_columnHidden = NULL;
    other->m_columnWidths = NULL;
    other->m_columnStateCount = 0;
    self->m_expandsOnDoubleClick = other->m_expandsOnDoubleClick;
    self->m_itemsExpandable = other->m_itemsExpandable;
    self->m_rootIsDecorated = other->m_rootIsDecorated;
    self->m_sortingEnabled = other->m_sortingEnabled;
    self->m_uniformRowHeights = other->m_uniformRowHeights;
    other->m_indentation = 20;
    other->m_headerHidden = false;
    other->m_rowHeight = XTREEVIEW_DEFAULT_ROW_H;
    other->m_expandsOnDoubleClick = true;
    other->m_itemsExpandable = true;
    other->m_rootIsDecorated = true;
    other->m_sortingEnabled = false;
    other->m_uniformRowHeights = false;
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

/* ==================== 树展开族（对标 QTreeView） ==================== */

void XTreeView_expand(XTreeView* self, int row)
{
    if (!self || row < 0) return;
    xtv_refreshRowStates(self);
    if (row >= self->m_rowStateCount || !self->m_expanded) return;
    if (!self->m_expanded[row]) {
        self->m_expanded[row] = true;
        xtv_emitInt(self, (size_t)XTreeView_expanded_signal(self, row), row);
    }
}

void XTreeView_collapse(XTreeView* self, int row)
{
    if (!self || row < 0) return;
    xtv_refreshRowStates(self);
    if (row >= self->m_rowStateCount || !self->m_expanded) return;
    if (self->m_expanded[row]) {
        self->m_expanded[row] = false;
        xtv_emitInt(self, (size_t)XTreeView_collapsed_signal(self, row), row);
    }
}

bool XTreeView_isExpanded(const XTreeView* self, int row)
{
    if (!self || !self->m_expanded || row < 0) return false;
    if (row >= self->m_rowStateCount) return false;
    return self->m_expanded[row];
}

void XTreeView_setExpanded(XTreeView* self, int row, bool expand)
{
    if (!self || row < 0) return;
    xtv_refreshRowStates(self);
    if (row >= self->m_rowStateCount || !self->m_expanded) return;
    if (expand && !self->m_expanded[row]) {
        self->m_expanded[row] = true;
        xtv_emitInt(self, (size_t)XTreeView_expanded_signal(self, row), row);
    } else if (!expand && self->m_expanded[row]) {
        self->m_expanded[row] = false;
        xtv_emitInt(self, (size_t)XTreeView_collapsed_signal(self, row), row);
    }
}

void XTreeView_expandAll(XTreeView* self)
{
    int i;
    if (!self) return;
    xtv_refreshRowStates(self);
    /* 对标 Qt expandAll：批量置位不逐行发射 expanded。 */
    for (i = 0; i < self->m_rowStateCount; ++i)
        self->m_expanded[i] = true;
    XWidget_update((XWidget*)self);
}

void XTreeView_collapseAll(XTreeView* self)
{
    int i;
    if (!self) return;
    xtv_refreshRowStates(self);
    /* 对标 Qt collapseAll：批量置位不逐行发射 collapsed。 */
    for (i = 0; i < self->m_rowStateCount; ++i)
        self->m_expanded[i] = false;
    XWidget_update((XWidget*)self);
}

void XTreeView_expandToDepth(XTreeView* self, int depth)
{
    int i;
    (void)depth;
    if (!self) return;
    xtv_refreshRowStates(self);
    /* @note 扁平行模型无层次深度，简化为全部行置为展开。 */
    for (i = 0; i < self->m_rowStateCount; ++i)
        self->m_expanded[i] = true;
    XWidget_update((XWidget*)self);
}

void XTreeView_setExpandsOnDoubleClick(XTreeView* self, bool enable)
{
    if (self) self->m_expandsOnDoubleClick = enable;
}

bool XTreeView_expandsOnDoubleClick(const XTreeView* self)
{ return self ? self->m_expandsOnDoubleClick : true; }

void XTreeView_setItemsExpandable(XTreeView* self, bool enable)
{
    if (self) {
        self->m_itemsExpandable = enable;
        XWidget_update((XWidget*)self);
    }
}

bool XTreeView_itemsExpandable(const XTreeView* self)
{ return self ? self->m_itemsExpandable : true; }

void XTreeView_setRootIsDecorated(XTreeView* self, bool show)
{
    if (self) {
        self->m_rootIsDecorated = show;
        XWidget_update((XWidget*)self);
    }
}

bool XTreeView_rootIsDecorated(const XTreeView* self)
{ return self ? self->m_rootIsDecorated : true; }

void XTreeView_setSortingEnabled(XTreeView* self, bool enable)
{ if (self) self->m_sortingEnabled = enable; }

bool XTreeView_isSortingEnabled(const XTreeView* self)
{ return self ? self->m_sortingEnabled : false; }

void XTreeView_setUniformRowHeights(XTreeView* self, bool uniform)
{ if (self) self->m_uniformRowHeights = uniform; }

bool XTreeView_uniformRowHeights(const XTreeView* self)
{ return self ? self->m_uniformRowHeights : false; }

/* ==================== 列状态族（对标 QTreeView/QTreeWidget） ==================== */

void XTreeView_setColumnHidden(XTreeView* self, int column, bool hide)
{
    int need;
    if (!self || column < 0) return;
    need = xtv_modelCols(self);
    if (column + 1 > need) need = column + 1;
    xtv_syncColumnStates(self, need);
    if (column >= self->m_columnStateCount || !self->m_columnHidden) return;
    if (self->m_columnHidden[column] != hide) {
        self->m_columnHidden[column] = hide;
        XWidget_update((XWidget*)self);
    }
}

bool XTreeView_isColumnHidden(const XTreeView* self, int column)
{
    if (!self || !self->m_columnHidden || column < 0) return false;
    if (column >= self->m_columnStateCount) return false;
    return self->m_columnHidden[column];
}

void XTreeView_setColumnWidth(XTreeView* self, int column, int width)
{
    int need;
    if (!self || column < 0 || width < 0) return;
    need = xtv_modelCols(self);
    if (column + 1 > need) need = column + 1;
    xtv_syncColumnStates(self, need);
    if (column >= self->m_columnStateCount || !self->m_columnWidths) return;
    if (self->m_columnWidths[column] != width) {
        self->m_columnWidths[column] = width;
        XWidget_update((XWidget*)self);
    }
}

int XTreeView_columnWidth(const XTreeView* self, int column)
{
    if (!self || !self->m_columnWidths || column < 0) return 0;
    if (column >= self->m_columnStateCount) return 0;
    return self->m_columnWidths[column];
}

/* ==================== 信号（对标 QTreeView） ==================== */

void* XTreeView_expanded_signal(XTreeView* self, int row)
{
    (void)self; (void)row;
    return (void*)(size_t)XTreeView_expanded_signal;
}

void* XTreeView_collapsed_signal(XTreeView* self, int row)
{
    (void)self; (void)row;
    return (void*)(size_t)XTreeView_collapsed_signal;
}

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
    bool col0Hidden;
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
    xtv_refreshRowStates(tv);
    col0Hidden = XTreeView_isColumnHidden(tv, 0);
    if (!tv->m_headerHidden) {
        XRect hr = { 0, 0, r.width, XTREEVIEW_HEADER_H };
        const char* text = col0Hidden
                               ? NULL
                               : XAbstractItemModel_headerData_2(model, 0, 0);
        char buf[16];
        XPainter_fillRect(&painter, &hr, 0xFFF0F0F0u);
        if (!text || !text[0]) {
            XSnprintf(buf, sizeof(buf), "1");
            text = buf;
        }
        if (!col0Hidden) {
            XPainter_setPen(&painter, 0xFF444444u);
            XPainter_drawText(&painter, 4, XTREEVIEW_HEADER_H - 6, text, 0);
        }
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
        if (!col0Hidden) {
            const char* text = XAbstractItemModel_data_2(model, row, 0);
            if (tv->m_rootIsDecorated) {
                /* 展开控件列：展开画 "-"，折叠画 "+"（对标 rootIsDecorated）。 */
                int indent = (tv->m_indentation > 0)
                                 ? tv->m_indentation : 0;
                int cx = indent + 4;
                int cy = y + rh / 2;
                XPainter_setPen(&painter, 0xFF888888u);
                XPainter_drawLine(&painter, cx - 2, cy, cx + 2, cy);
                if (!XTreeView_isExpanded(tv, row))
                    XPainter_drawLine(&painter, cx, cy - 3, cx, cy + 3);
            }
            if (text && text[0]) {
                int indent = (tv->m_indentation > 0)
                                 ? tv->m_indentation : 0;
                XPainter_setPen(&painter, 0xFF000000u);
                XPainter_drawText(&painter, indent + 12, y + rh - 6, text, 0);
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
