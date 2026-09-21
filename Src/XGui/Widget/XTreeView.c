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
#define XTREEVIEW_DEFAULT_INDENTATION 20

static void VXTreeView_deinit(XTreeView* self);
static void VXTreeView_paintEvent(XWidget* self, XEvent* event);
static void VXTreeView_scrollContentsBy(XAbstractScrollArea* area, int dx,
                                        int dy)
{
    (void)dx;
    (void)dy;
    if (area) XWidget_update((XWidget*)area);
}

static bool VXTreeView_indexAt(const XAbstractItemView* view, int x, int y,
                               int* outRow, int* outCol);
static bool VXTreeView_visualRect(const XAbstractItemView* view, int row,
                                  int col, XRect* out);
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

/** @brief 行状态表（展开/行隐藏）与模型行数同步（扩容新增行默认折叠且可见）。
 * @param self 目标视图。
 * @param count 目标长度（<0 视为 0）。
 * @note 同步入口：expand 族/行隐藏族按行访问前、绘制前；copy/move/deinit
 *       全路径另行接管数组生命周期（参照 XHeaderView m_hidden 模式）。
 *       展开（m_expanded）与行隐藏（m_rowHidden）两表共用同一行数入口，
 *       长度恒同步；任一表扩容失败时两表整体回退为未分配（同
 *       xtv_syncColumnStates 的失败收敛口径）。
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
        if (self->m_rowHidden) {
            XFree_System(self->m_rowHidden);
            self->m_rowHidden = NULL;
        }
        self->m_rowStateCount = 0;
        self->m_rowHiddenCount = 0;
        return;
    }
    self->m_expanded = (bool*)XRealloc_System(
        self->m_expanded, sizeof(bool) * (size_t)count);
    self->m_rowHidden = (bool*)XRealloc_System(
        self->m_rowHidden, sizeof(bool) * (size_t)count);
    if (self->m_expanded && self->m_rowHidden) {
        if (count > old) {
            XMemset(self->m_expanded + old, 0,
                    sizeof(bool) * (size_t)(count - old));
            XMemset(self->m_rowHidden + old, 0,
                    sizeof(bool) * (size_t)(count - old));
        }
        self->m_rowStateCount = count;
        self->m_rowHiddenCount = count;
    } else {
        self->m_expanded = NULL;
        self->m_rowHidden = NULL;
        self->m_rowStateCount = 0;
        self->m_rowHiddenCount = 0;
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

/** @brief 计算表头占用的视口顶部偏移（隐藏表头为 0）。
 * @param self 目标视图。
 * @return 表头高度（像素）。
 */
static int xtv_headerOffset(const XTreeView* self)
{ return (self && !self->m_headerHidden) ? XTREEVIEW_HEADER_H : 0; }

/** @brief 计算统一行高（未设置或非法值回落默认行高）。
 * @param self 目标视图。
 * @return 行高（像素）。
 */
static int xtv_effectiveRowHeight(const XTreeView* self)
{
    return (self && self->m_rowHeight > 0) ? self->m_rowHeight
                                           : XTREEVIEW_DEFAULT_ROW_H;
}

/** @brief 计算未显式设宽可见列的自动铺满均摊份额。
 * @param self 目标视图。
 * @param cols 参与计算的列数（通常为模型列数）。
 * @return 均摊份额（像素；无自动列或无剩余空间为 0）。
 * @note 口径：份额=(视口宽度-显式列宽合计)/自动列数，向下取整；
 *       视口宽度取控件宽度（与绘制整宽铺满口径一致）。
 */
static int xtv_autoColumnShare(const XTreeView* self, int cols)
{
    int viewport;
    int fixedSum;
    int autoCount;
    int c;
    if (!self || cols <= 0) return 0;
    viewport = XWidget_width((const XWidget*)self);
    if (viewport <= 0) return 0;
    fixedSum = 0;
    autoCount = 0;
    for (c = 0; c < cols; ++c) {
        int w;
        if (XTreeView_isColumnHidden(self, c)) continue;
        w = (self->m_columnWidths && c < self->m_columnStateCount
             && self->m_columnWidths[c] > 0)
                ? self->m_columnWidths[c] : 0;
        if (w > 0) fixedSum += w;
        else ++autoCount;
    }
    if (autoCount <= 0) return 0;
    viewport -= fixedSum;
    return viewport > 0 ? viewport / autoCount : 0;
}

/** @brief 查询指定列生效列宽：显式列宽（>0）优先，否则取自动均摊份额。
 * @param self 目标视图。
 * @param column 列号。
 * @param autoShare 自动铺满均摊份额（由 xtv_autoColumnShare 计算）。
 * @return 生效列宽（像素）。
 */
static int xtv_columnEffectiveWidth(const XTreeView* self, int column,
                                    int autoShare)
{
    if (self->m_columnWidths && column < self->m_columnStateCount
        && self->m_columnWidths[column] > 0)
        return self->m_columnWidths[column];
    return autoShare > 0 ? autoShare : 0;
}

XVtable* XTreeView_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XTreeView)
    XVTABLE_INHERIT_XCLASS(XAbstractItemView);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXTreeView_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXTreeView_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXTreeView_move);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VXTreeView_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractScrollArea_ScrollContentsBy,
                             VXTreeView_scrollContentsBy);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractItemView_IndexAt, VXTreeView_indexAt);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractItemView_VisualRect,
                             VXTreeView_visualRect);
    return XVTABLE_DEFAULT;
}

void XTreeView_init(XTreeView* self, XWidget* parent, XWidgetFlags flags)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XAbstractItemView_init(&self->m_base, parent, flags);
    XClassSetVtable(self, XTreeView);
    self->m_indentation = XTREEVIEW_DEFAULT_INDENTATION;
    self->m_headerHidden = false;
    self->m_rowHeight = XTREEVIEW_DEFAULT_ROW_H;
    self->m_expanded = NULL;
    self->m_rowStateCount = 0;
    self->m_rowHidden = NULL;
    self->m_rowHiddenCount = 0;
    self->m_columnHidden = NULL;
    self->m_columnWidths = NULL;
    self->m_columnStateCount = 0;
    self->m_expandsOnDoubleClick = true;
    self->m_itemsExpandable = true;
    self->m_rootIsDecorated = true;
    self->m_sortingEnabled = false;
    self->m_uniformRowHeights = false;
    self->m_sortColumn = -1;
    self->m_sortOrder = 0;
    self->m_selectionRectVisible = false;
    self->m_allColumnsShowFocus = false;
    self->m_treePosition = 0;
    self->m_autoExpandDelay = -1;
    self->m_animated = false;
    self->m_wordWrap = false;
    self->m_header = NULL;
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
    if (self->m_rowHidden) {
        XFree_System(self->m_rowHidden);
        self->m_rowHidden = NULL;
    }
    self->m_rowHiddenCount = 0;
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
    /* 行隐藏状态表深拷贝（与展开表同长同入口）。 */
    if (self->m_rowHidden) {
        XFree_System(self->m_rowHidden);
        self->m_rowHidden = NULL;
    }
    self->m_rowHiddenCount = 0;
    n = other->m_rowHiddenCount;
    if (n > 0 && other->m_rowHidden) {
        self->m_rowHidden = (bool*)XMalloc_System(sizeof(bool) * (size_t)n);
        if (self->m_rowHidden) {
            XMemmove(self->m_rowHidden, other->m_rowHidden,
                     sizeof(bool) * (size_t)n);
            self->m_rowHiddenCount = n;
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
    self->m_sortColumn = other->m_sortColumn;
    self->m_sortOrder = other->m_sortOrder;
    self->m_selectionRectVisible = other->m_selectionRectVisible;
    self->m_allColumnsShowFocus = other->m_allColumnsShowFocus;
    self->m_treePosition = other->m_treePosition;
    self->m_autoExpandDelay = other->m_autoExpandDelay;
    self->m_animated = other->m_animated;
    self->m_wordWrap = other->m_wordWrap;
    /* 表头对象借用指针：拷贝沿用同款借用指针（同 XTableView 先例）。 */
    self->m_header = other->m_header;
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
    self->m_rowHidden = other->m_rowHidden;
    self->m_rowHiddenCount = other->m_rowHiddenCount;
    other->m_rowHidden = NULL;
    other->m_rowHiddenCount = 0;
    self->m_columnHidden = other->m_columnHidden;
    self->m_columnWidths = other->m_columnWidths;
    self->m_columnStateCount = other->m_columnStateCount;
    other->m_columnHidden = NULL;
    other->m_columnWidths = NULL;
    other->m_columnStateCount = 0;
    self->m_header = other->m_header;
    other->m_header = NULL;
    self->m_expandsOnDoubleClick = other->m_expandsOnDoubleClick;
    self->m_itemsExpandable = other->m_itemsExpandable;
    self->m_rootIsDecorated = other->m_rootIsDecorated;
    self->m_sortingEnabled = other->m_sortingEnabled;
    self->m_uniformRowHeights = other->m_uniformRowHeights;
    other->m_indentation = XTREEVIEW_DEFAULT_INDENTATION;
    other->m_headerHidden = false;
    other->m_rowHeight = XTREEVIEW_DEFAULT_ROW_H;
    other->m_expandsOnDoubleClick = true;
    other->m_itemsExpandable = true;
    other->m_rootIsDecorated = true;
    other->m_sortingEnabled = false;
    other->m_uniformRowHeights = false;
    other->m_sortColumn = -1;
    other->m_sortOrder = 0;
    other->m_selectionRectVisible = false;
    other->m_allColumnsShowFocus = false;
    other->m_treePosition = 0;
    other->m_autoExpandDelay = -1;
    other->m_animated = false;
    other->m_wordWrap = false;
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

void XTreeView_resetIndentation(XTreeView* self)
{
    if (self) {
        self->m_indentation = XTREEVIEW_DEFAULT_INDENTATION;
        XWidget_update((XWidget*)self);
    }
}

void XTreeView_setHeaderHidden(XTreeView* self, bool hidden)
{
    if (self) {
        self->m_headerHidden = hidden;
        /* 对接表头对象：已挂接时镜像其可见性（对标 Qt headerHidden 即
         * 表头 setHidden 的状态承载）。 */
        if (self->m_header)
            XWidget_setHidden((XWidget*)self->m_header, hidden);
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

/* ==================== 模型变化槽（对标 QAbstractItemView::dataChanged） ==================== */

void XTreeView_dataChanged(XTreeView* self, int topRow, int leftCol,
                           int bottomRow, int rightCol)
{
    XAbstractItemModel* model;
    if (!self) return;
    if (topRow < 0 || leftCol < 0 || bottomRow < topRow || rightCol < leftCol)
        return; /* 无效区间（逆序/负值）：对标 Qt 丢弃语义。 */
    model = self->m_base.m_model;
    if (model && (topRow >= model->m_rows || leftCol >= model->m_cols))
        return; /* 区间完全越出模型范围：无有效单元格。 */
    /* 视图绘制管线为全量帧：区间校验后整体重绘（Qt 局部刷新的收敛）。 */
    XWidget_update((XWidget*)self);
}

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

void XTreeView_expandRecursively(XTreeView* self, int row)
{
    (void)row;
    /* @note 扁平行模型无子层级：递归展开任意行等效于展开全部行
     *       （对标 Qt expandRecursively 需子层级的语义在此收敛）。 */
    XTreeView_expandAll(self);
}

/* ==================== 索引导航族（对标 QTreeView indexAbove/indexBelow） ==================== */

int XTreeView_indexAbove(const XTreeView* self, int row)
{
    if (!self || row < 0) return -1;
    if (row >= xtv_modelRows(self)) return -1;
    /* 平铺模型：上方即平铺行序的 row-1（首行无上行，返回 -1）。 */
    return row - 1;
}

int XTreeView_indexBelow(const XTreeView* self, int row)
{
    if (!self || row < 0) return -1;
    if (row + 1 >= xtv_modelRows(self)) return -1;
    /* 平铺模型：下方即平铺行序的 row+1（末行无下行，返回 -1）。 */
    return row + 1;
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

void XTreeView_hideColumn(XTreeView* self, int column)
{ XTreeView_setColumnHidden(self, column, true); }

void XTreeView_showColumn(XTreeView* self, int column)
{ XTreeView_setColumnHidden(self, column, false); }

void XTreeView_resizeColumnToContents(XTreeView* self, int column)
{
    (void)column;
    /* @note 简化承载：内容宽度测算未接（同 XHeaderView 的
     *       ResizeToContents 模式），仅调度一次全量重绘，列宽保持不变；
     *       精确测算由派生 XTreeWidget 接线。 */
    if (self) XWidget_update((XWidget*)self);
}

/* ==================== 排序族（对标 QTreeView） ==================== */

void XTreeView_sortByColumn(XTreeView* self, int column, int order)
{
    if (!self || column < -1) return;
    if (self->m_sortColumn == column && self->m_sortOrder == order) return;
    /* @note 排序本体未接：仅记录排序列与方向（对标 sortByColumn 状态承载）。 */
    self->m_sortColumn = column;
    self->m_sortOrder = order;
    XWidget_update((XWidget*)self);
}

int XTreeView_sortColumn(const XTreeView* self)
{ return self ? self->m_sortColumn : -1; }

int XTreeView_sortIndicatorOrder(const XTreeView* self)
{ return self ? self->m_sortOrder : 0; }

/* ==================== 滚动偏移（对标 QTreeView 视口滚动） ==================== */

/** @brief 读取垂直滚动偏移（视口原点在内容坐标中的 y）。 */
static int xtvw_scrollOffsetY(const XTreeView* tv)
{
    XScrollBar* vsb = XAbstractScrollArea_verticalScrollBar(
        (const XAbstractScrollArea*)&tv->m_base);
    return vsb ? XScrollBar_value(vsb) : 0;
}

/** @brief 按内容高度维护垂直滚动条范围（值变化才写）。 */
static void xtvw_updateScrollRange(XTreeView* tv)
{
    XAbstractItemModel* model = tv->m_base.m_model;
    XScrollBar* vsb = XAbstractScrollArea_verticalScrollBar(
        (const XAbstractScrollArea*)&tv->m_base);
    int rows = model ? model->m_rows : 0;
    int rh = tv->m_rowHeight > 0 ? tv->m_rowHeight : XTREEVIEW_DEFAULT_ROW_H;
    int viewH = XWidget_height((XWidget*)tv);
    int contentH = (tv->m_headerHidden ? 0 : XTREEVIEW_HEADER_H) +
                   rows * rh;
    int vMax = contentH > viewH ? contentH - viewH : 0;
    if (vsb && XScrollBar_maximum(vsb) != vMax)
        XScrollBar_setRange(vsb, 0, vMax);
}

/* ==================== 几何查询族（行高/列宽反推，同 indexAt 口径） ==================== */

int XTreeView_rowAt(const XTreeView* self, int y)
{
    int yAcc;
    if (!self || y < 0) return -1;
    yAcc = y - xtv_headerOffset(self) + xtvw_scrollOffsetY(self);
    if (yAcc < 0) return -1;
    return yAcc / xtv_effectiveRowHeight(self);
}

int XTreeView_columnAt(const XTreeView* self, int x)
{
    int cols;
    int autoShare;
    int cursor;
    int c;
    if (!self || x < 0) return -1;
    cols = xtv_modelCols(self);
    if (cols <= 0) return -1;
    autoShare = xtv_autoColumnShare(self, cols);
    cursor = 0;
    for (c = 0; c < cols; ++c) {
        int w;
        if (XTreeView_isColumnHidden(self, c)) continue;
        w = xtv_columnEffectiveWidth(self, c, autoShare);
        if (x >= cursor && x < cursor + w) return c;
        cursor += w;
    }
    return -1;
}

XRect XTreeView_visualRect(const XTreeView* self, int row, int column)
{
    XRect r;
    int cols;
    int autoShare;
    int x;
    int c;
    r.x = 0;
    r.y = 0;
    r.width = 0;
    r.height = 0;
    if (!self || row < 0 || column < 0) return r;
    cols = xtv_modelCols(self);
    if (column >= cols) return r;
    if (XTreeView_isColumnHidden(self, column)) return r;
    autoShare = xtv_autoColumnShare(self, cols);
    x = 0;
    for (c = 0; c < column; ++c) {
        if (XTreeView_isColumnHidden(self, c)) continue;
        x += xtv_columnEffectiveWidth(self, c, autoShare);
    }
    r.x = x;
    r.y = xtv_headerOffset(self) + row * xtv_effectiveRowHeight(self) -
          xtvw_scrollOffsetY(self);
    r.width = xtv_columnEffectiveWidth(self, column, autoShare);
    r.height = xtv_effectiveRowHeight(self);
    return r;
}

int XTreeView_columnViewportPosition(const XTreeView* self, int column)
{
    int cols;
    int autoShare;
    int x;
    int c;
    if (!self || column < 0) return -1;
    cols = xtv_modelCols(self);
    if (column >= cols) return -1;
    autoShare = xtv_autoColumnShare(self, cols);
    /* 口径同 visualRect 的 x 向累计：可见列生效列宽求和，隐藏列跳过
     * （宽度按 0 计）；目标列自身隐藏时返回其 0 宽度位置。 */
    x = 0;
    for (c = 0; c < column; ++c) {
        if (XTreeView_isColumnHidden(self, c)) continue;
        x += xtv_columnEffectiveWidth(self, c, autoShare);
    }
    return x;
}

/* ==================== 视图状态族（对标 QTreeView/QAbstractItemView） ==================== */

void XTreeView_setSelectionRectVisible(XTreeView* self, bool visible)
{
    if (self) {
        self->m_selectionRectVisible = visible;
        XWidget_update((XWidget*)self);
    }
}

bool XTreeView_isSelectionRectVisible(const XTreeView* self)
{ return self ? self->m_selectionRectVisible : false; }

void XTreeView_setAllColumnsShowFocus(XTreeView* self, bool enable)
{
    if (self) {
        self->m_allColumnsShowFocus = enable;
        XWidget_update((XWidget*)self);
    }
}

bool XTreeView_allColumnsShowFocus(const XTreeView* self)
{ return self ? self->m_allColumnsShowFocus : false; }

void XTreeView_setTreePosition(XTreeView* self, int column)
{
    if (!self || self->m_treePosition == column) return;
    /* @note 树位置绘制未接：扁平行模型恒按第 0 列绘制，仅记录状态。 */
    self->m_treePosition = column;
    XWidget_update((XWidget*)self);
}

int XTreeView_treePosition(const XTreeView* self)
{ return self ? self->m_treePosition : 0; }

void XTreeView_setAnimated(XTreeView* self, bool enable)
{ if (self) self->m_animated = enable; }

bool XTreeView_isAnimated(const XTreeView* self)
{ return self ? self->m_animated : false; }

void XTreeView_setWordWrap(XTreeView* self, bool on)
{ if (self) self->m_wordWrap = on; }

bool XTreeView_wordWrap(const XTreeView* self)
{ return self ? self->m_wordWrap : false; }

void XTreeView_setAutoExpandDelay(XTreeView* self, int delay)
{ if (self) self->m_autoExpandDelay = delay; }

int XTreeView_autoExpandDelay(const XTreeView* self)
{ return self ? self->m_autoExpandDelay : -1; }

/* ==================== 行隐藏族（对标 QTreeView setRowHidden/isRowHidden） ==================== */

void XTreeView_setRowHidden(XTreeView* self, int row, bool hide)
{
    if (!self || row < 0) return;
    xtv_refreshRowStates(self);
    if (row >= self->m_rowHiddenCount || !self->m_rowHidden) return;
    if (self->m_rowHidden[row] != hide) {
        self->m_rowHidden[row] = hide;
        XWidget_update((XWidget*)self);
    }
}

bool XTreeView_isRowHidden(const XTreeView* self, int row)
{
    if (!self || !self->m_rowHidden || row < 0) return false;
    if (row >= self->m_rowHiddenCount) return false;
    return self->m_rowHidden[row];
}

/* ==================== 跨列合并族（对标 QTreeView firstColumnSpanned） ==================== */

void XTreeView_setFirstColumnSpanned(XTreeView* self, int row, bool span)
{
    (void)row;
    (void)span;
    /* @note 扁平行模型恒单列渲染：跨列合并不可表达，空操作保留（同
     *       XTableView setRowSpan/setColumnSpan 先例），不存储、不发信号。 */
    (void)self;
}

bool XTreeView_isFirstColumnSpanned(const XTreeView* self, int row)
{
    (void)self;
    (void)row;
    /* 平铺模型无跨列能力：恒返回 false（同 Qt 对未合并单元格）。 */
    return false;
}

/* ==================== 表头对象挂接（对标 QTreeView setHeader/header） ==================== */

void XTreeView_setHeader(XTreeView* self, XHeaderView* header)
{
    if (!self) return;
    /* 方向校验（对标 Qt 断言语义）：树视图仅挂水平表头（方向 0），不符忽略。 */
    if (header && header->m_orientation != 0) return;
    if (self->m_header == header) return;
    self->m_header = header;
    /* 对接 headerHidden 机制：挂接时以视图侧隐藏状态镜像表头可见性。 */
    if (header) XWidget_setHidden((XWidget*)header, self->m_headerHidden);
    XWidget_update((XWidget*)self);
}

XHeaderView* XTreeView_header(const XTreeView* self)
{ return self ? self->m_header : NULL; }

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
    yAcc = y - (tv->m_headerHidden ? 0 : XTREEVIEW_HEADER_H) +
           xtvw_scrollOffsetY(tv);
    if (yAcc < 0) return false;
    if (outRow) *outRow = yAcc / rh;
    if (outCol) *outCol = 0;
    return true;
}

/* 条目几何虚槽：转发到树视图既有 (row,col) 几何（含表头区/行高/列宽/
 * 滚动偏移口径；供基类编辑器摆放/scrollTo/尺寸提示虚分派，对标
 * QTreeView::visualRect 对 QAbstractItemView::visualRect 的覆写）。 */
static bool VXTreeView_visualRect(const XAbstractItemView* view, int row,
                                  int col, XRect* out)
{
    XTreeView* tv = (XTreeView*)view;
    if (!tv || !out) return false;
    *out = XTreeView_visualRect(tv, row, col);
    return out->width > 0 && out->height > 0;
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
    xtvw_updateScrollRange(tv);
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
            XPainter_drawText(&painter, 4, XTREEVIEW_HEADER_H - 6, text,
                              0xFF444444u);
        }
    }
    rows = model->m_rows;
    rh = tv->m_rowHeight > 0 ? tv->m_rowHeight : XTREEVIEW_DEFAULT_ROW_H;
    y = (tv->m_headerHidden ? 0 : XTREEVIEW_HEADER_H) -
        xtvw_scrollOffsetY(tv);
    for (row = 0; row < rows && y < r.height; ++row) {
        XRect cell = { 0, y, r.width, rh };
        bool sel;
        bool cur;
        /* 行隐藏生效：隐藏行不绘制（对标 Qt 隐藏行不出现在视口）。 */
        if (XTreeView_isRowHidden(tv, row)) continue;
        sel = view->m_selectionModel &&
              XItemSelectionModel_isSelected(
                  view->m_selectionModel, row, 0);
        cur = (view->m_currentRow == row && view->m_currentColumn == 0);
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
                XPainter_drawText(&painter, indent + 12, y + rh - 6, text,
                              0xFF000000u);
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
