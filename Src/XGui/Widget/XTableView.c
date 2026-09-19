#include "XTableView.h"

#include "XAlgorithm.h"
#include "XStringUtils.h"
#include "XMemory.h"
#include "XClass.h"
#include "XPainter.h"
#include "XEvent.h"
#include "XHeaderView.h"
#include "XWidget_Protected.h"

#if XWIDGET_ON && XTABLEWIDGET_ON

#define XTV_DEFAULT_COL_WIDTH 80
#define XTV_DEFAULT_ROW_HEIGHT 24

static void VXTableView_deinit(XTableView* self);
static void VXTableView_paintEvent(XWidget* self, XEvent* event);
static bool VXTableView_indexAt(const XAbstractItemView* view, int x, int y,
                                int* outRow, int* outCol);
static void VXTableView_copy(XTableView* self, const XTableView* other);
static void VXTableView_move(XTableView* self, XTableView* other);

/** @brief 表头高度（水平表头区在上方）。 */
#define XTV_HEADER_H 20

/** @brief 按内容调整的单元格左右边距（与绘制 x+4 起笔一致）。 */
#define XTV_CONTENT_HMARGIN 4
/** @brief 按内容调整的单元格上下边距（单行文本简化预留）。 */
#define XTV_CONTENT_VMARGIN 2

/* ==================== 内部辅助（隐藏状态表；参照 XTreeView 模式） ==================== */

/** @brief 行/列隐藏布尔平行表按需扩容（仅增长，新增项默认 false）。
 * @param table 目标数组地址。
 * @param count 目标数组当前长度地址。
 * @param need 需要覆盖的下标数（<0 视为 0）。
 * @return 扩容成功（或已覆盖）返回 true；分配失败返回 false 且原表不变。
 * @note 只增长不收缩：模型缩小时保留原状态，模型恢复后隐藏态不丢失
 *       （对标 Qt 分段状态与模型尺寸解耦）；deinit/copy/move 全路径
 *       另行接管数组生命周期。 */
static bool xtv_ensureBoolTable(bool** table, int* count, int need)
{
    bool* grown;
    if (!table || !count) return false;
    if (need < 0) need = 0;
    if (need <= *count) return true;
    grown = (bool*)XRealloc_System(*table, sizeof(bool) * (size_t)need);
    if (!grown) return false;
    XMemset(grown + *count, 0, sizeof(bool) * (size_t)(need - *count));
    *table = grown;
    *count = need;
    return true;
}

/** @brief 行/列隐藏状态表与模型行/列数同步（行/列数变化生效入口）。
 * @param self 目标视图。
 * @note 在绘制与命中反查入口调用，保证状态表覆盖当前模型行/列数；
 *       表按需增长、不随模型缩小回收（状态保留语义见
 *       xtv_ensureBoolTable）。 */
static void xtv_syncHiddenTables(XTableView* self)
{
    XAbstractItemModel* model;
    int rows;
    int cols;
    if (!self) return;
    model = self->m_base.m_model;
    rows = model ? model->m_rows : 0;
    cols = model ? model->m_cols : 0;
    (void)xtv_ensureBoolTable(&self->m_rowHidden, &self->m_rowHiddenCount, rows);
    (void)xtv_ensureBoolTable(&self->m_colHidden, &self->m_colHiddenCount, cols);
}

/** @brief 读取隐藏状态（越界或未建表按未隐藏处理）。 */
static bool xtv_hiddenRaw(const bool* table, int count, int index)
{
    if (!table || index < 0 || index >= count) return false;
    return table[index];
}

/* ==================== 内部辅助（合并区间表） ==================== */

/** @brief 合并区间表按需扩容（倍增；原表内容保留）。
 * @param self 目标视图。
 * @param need 需要容纳的区间数。
 * @return 扩容成功（或已覆盖）返回 true；分配失败返回 false 且原表不变。 */
static bool xtv_ensureSpanTable(XTableView* self, int need)
{
    XTableViewSpan* grown;
    int cap;
    if (need <= self->m_spanCapacity) return true;
    cap = self->m_spanCapacity > 0 ? self->m_spanCapacity : 4;
    while (cap < need) cap *= 2;
    grown = (XTableViewSpan*)XRealloc_System(
        self->m_spans, sizeof(XTableViewSpan) * (size_t)cap);
    if (!grown) return false;
    self->m_spans = grown;
    self->m_spanCapacity = cap;
    return true;
}

/** @brief 查找包含 (row,column) 的合并区间（原点或被覆盖格均算）。
 * @return 命中区间只读指针；未合并返回 NULL。 */
static const XTableViewSpan* xtv_spanFind(const XTableView* self, int row,
                                          int column)
{
    int i;
    for (i = 0; i < self->m_spanCount; ++i) {
        const XTableViewSpan* s = &self->m_spans[i];
        if (row >= s->row && row < s->row + s->rowSpan &&
            column >= s->column && column < s->column + s->columnSpan)
            return s;
    }
    return NULL;
}


XVtable* XTableView_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XTableView)
    XVTABLE_INHERIT_XCLASS(XAbstractItemView);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXTableView_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXTableView_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXTableView_move);
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
    self->m_gridStyle = XTABLEVIEW_GRID_SOLID;
    self->m_gridVisible = 1;
    self->m_wordWrap = false;
    self->m_cornerButton = true;
    self->m_rowHidden = NULL;
    self->m_rowHiddenCount = 0;
    self->m_colHidden = NULL;
    self->m_colHiddenCount = 0;
    self->m_sortingEnabled = false;
    self->m_sortColumn = -1;
    self->m_sortOrder = 0;
    self->m_horizontalHeader = NULL;
    self->m_verticalHeader = NULL;
    self->m_spans = NULL;
    self->m_spanCount = 0;
    self->m_spanCapacity = 0;
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
    if (self->m_rowHidden) {
        XFree_System(self->m_rowHidden);
        self->m_rowHidden = NULL;
    }
    self->m_rowHiddenCount = 0;
    if (self->m_colHidden) {
        XFree_System(self->m_colHidden);
        self->m_colHidden = NULL;
    }
    self->m_colHiddenCount = 0;
    if (self->m_spans) {
        XFree_System(self->m_spans);
        self->m_spans = NULL;
    }
    self->m_spanCount = 0;
    self->m_spanCapacity = 0;
    /* 表头对象为借用指针：不析构，仅清空挂接（生命周期归调用方）。 */
    self->m_horizontalHeader = NULL;
    self->m_verticalHeader = NULL;
    XClass_Deinit_Parent(XAbstractItemView, (XAbstractItemView*)self);
}

/** @brief 深拷贝：基类拷贝后复制标量与行/列隐藏平行状态表。 */
static void VXTableView_copy(XTableView* self, const XTableView* other)
{
    int n;
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XTableView_init(self, NULL, 0);
    XClass_Parent(XAbstractItemView, EXClass_Copy,
                  void(*)(XAbstractItemView*, const XAbstractItemView*))(
                      (XAbstractItemView*)self,
                      (const XAbstractItemView*)other);
    self->m_rowHeight = other->m_rowHeight;
    self->m_gridStyle = other->m_gridStyle;
    self->m_gridVisible = other->m_gridVisible;
    self->m_wordWrap = other->m_wordWrap;
    self->m_cornerButton = other->m_cornerButton;
    /* 列宽容量表深拷贝。 */
    if (self->m_colWidths) {
        XFree_System(self->m_colWidths);
        self->m_colWidths = NULL;
    }
    self->m_colCapacity = 0;
    n = other->m_colCapacity;
    if (n > 0 && other->m_colWidths) {
        self->m_colWidths = (int*)XMalloc_System(sizeof(int) * (size_t)n);
        if (self->m_colWidths) {
            XMemmove(self->m_colWidths, other->m_colWidths,
                     sizeof(int) * (size_t)n);
            self->m_colCapacity = n;
        }
    }
    /* 行隐藏状态表深拷贝。 */
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
    /* 列隐藏状态表深拷贝。 */
    if (self->m_colHidden) {
        XFree_System(self->m_colHidden);
        self->m_colHidden = NULL;
    }
    self->m_colHiddenCount = 0;
    n = other->m_colHiddenCount;
    if (n > 0 && other->m_colHidden) {
        self->m_colHidden = (bool*)XMalloc_System(sizeof(bool) * (size_t)n);
        if (self->m_colHidden) {
            XMemmove(self->m_colHidden, other->m_colHidden,
                     sizeof(bool) * (size_t)n);
            self->m_colHiddenCount = n;
        }
    }
    /* 合并区间表深拷贝。 */
    if (self->m_spans) {
        XFree_System(self->m_spans);
        self->m_spans = NULL;
    }
    self->m_spanCount = 0;
    self->m_spanCapacity = 0;
    n = other->m_spanCount;
    if (n > 0 && other->m_spans) {
        self->m_spans = (XTableViewSpan*)XMalloc_System(
            sizeof(XTableViewSpan) * (size_t)n);
        if (self->m_spans) {
            XMemmove(self->m_spans, other->m_spans,
                     sizeof(XTableViewSpan) * (size_t)n);
            self->m_spanCount = n;
            self->m_spanCapacity = n;
        }
    }
    self->m_sortingEnabled = other->m_sortingEnabled;
    self->m_sortColumn = other->m_sortColumn;
    self->m_sortOrder = other->m_sortOrder;
    /* 表头对象借用指针：拷贝共享同一对象（所有权仍在调用方）。 */
    self->m_horizontalHeader = other->m_horizontalHeader;
    self->m_verticalHeader = other->m_verticalHeader;
}

/** @brief 移动语义：基类移动后转移列宽/隐藏状态数组，源对象归默认值。 */
static void VXTableView_move(XTableView* self, XTableView* other)
{
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XTableView_init(self, NULL, 0);
    XClass_Parent(XAbstractItemView, EXClass_Move,
                  void(*)(XAbstractItemView*, XAbstractItemView*))(
                      (XAbstractItemView*)self, (XAbstractItemView*)other);
    self->m_rowHeight = other->m_rowHeight;
    self->m_gridStyle = other->m_gridStyle;
    self->m_gridVisible = other->m_gridVisible;
    self->m_wordWrap = other->m_wordWrap;
    self->m_cornerButton = other->m_cornerButton;
    self->m_colWidths = other->m_colWidths;
    self->m_colCapacity = other->m_colCapacity;
    other->m_colWidths = NULL;
    other->m_colCapacity = 0;
    self->m_rowHidden = other->m_rowHidden;
    self->m_rowHiddenCount = other->m_rowHiddenCount;
    other->m_rowHidden = NULL;
    other->m_rowHiddenCount = 0;
    self->m_colHidden = other->m_colHidden;
    self->m_colHiddenCount = other->m_colHiddenCount;
    other->m_colHidden = NULL;
    other->m_colHiddenCount = 0;
    self->m_spans = other->m_spans;
    self->m_spanCount = other->m_spanCount;
    self->m_spanCapacity = other->m_spanCapacity;
    other->m_spans = NULL;
    other->m_spanCount = 0;
    other->m_spanCapacity = 0;
    self->m_sortingEnabled = other->m_sortingEnabled;
    self->m_sortColumn = other->m_sortColumn;
    self->m_sortOrder = other->m_sortOrder;
    /* 表头对象借用指针：随移动转移挂接，源对象解除挂接。 */
    self->m_horizontalHeader = other->m_horizontalHeader;
    self->m_verticalHeader = other->m_verticalHeader;
    other->m_horizontalHeader = NULL;
    other->m_verticalHeader = NULL;
    other->m_rowHeight = XTV_DEFAULT_ROW_HEIGHT;
    other->m_gridStyle = XTABLEVIEW_GRID_SOLID;
    other->m_gridVisible = 1;
    other->m_wordWrap = false;
    other->m_cornerButton = true;
    other->m_sortingEnabled = false;
    other->m_sortColumn = -1;
    other->m_sortOrder = 0;
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

/* ==================== 按内容调整（文本测量简化实现） ==================== */

/** @brief 测量指定列全部可见行单元格文本的最大像素宽。
 * @param self 目标视图。
 * @param column 列号。
 * @return 最大文本宽（空文本行按 0 计）；无模型或列号越界返回 -1。
 * @note 字体取视图当前字体（XWidget_font 深拷贝），用毕即释放；
 *       隐藏行不参与测算（对标 Qt 跳过隐藏行）。 */
static int xtv_measureColumnTextWidth(const XTableView* self, int column)
{
    XAbstractItemModel* model;
    XFont font;
    int rows;
    int row;
    int maxW = 0;
    if (!self) return -1;
    model = self->m_base.m_model;
    rows = model ? model->m_rows : 0;
    if (!model || column < 0 || column >= model->m_cols) return -1;
    font = XWidget_font((const XWidget*)self);
    for (row = 0; row < rows; ++row) {
        const char* text;
        int w;
        if (xtv_hiddenRaw(self->m_rowHidden, self->m_rowHiddenCount, row))
            continue; /* 隐藏行占高 0：不参与宽度测算。 */
        text = XAbstractItemModel_data_2(model, row, column);
        if (!text || !text[0]) continue;
        w = XPainter_textWidth(&font, text);
        if (w > maxW) maxW = w;
    }
    /* XFont_deinit_base 即 XClass_deinit_base 的别名，显式收窄基类指针。 */
    XFont_deinit_base((XClass*)&font);
    return maxW;
}

void XTableView_resizeColumnToContents(XTableView* self, int column)
{
    int content;
    if (!self) return;
    /* 行数变化生效入口：先同步隐藏状态表再跳过隐藏行测算。 */
    xtv_syncHiddenTables(self);
    content = xtv_measureColumnTextWidth(self, column);
    if (content < 0) return;
    XTableView_setColumnWidth(self, column,
                              content + 2 * XTV_CONTENT_HMARGIN);
    XWidget_update((XWidget*)self);
}

void XTableView_resizeColumnsToContents(XTableView* self)
{
    XAbstractItemModel* model;
    int cols;
    int col;
    if (!self) return;
    model = self->m_base.m_model;
    cols = model ? model->m_cols : 0;
    for (col = 0; col < cols; ++col)
        XTableView_resizeColumnToContents(self, col);
}

void XTableView_resizeRowToContents(XTableView* self, int row)
{
    XAbstractItemModel* model;
    XFont font;
    int content;
    if (!self) return;
    model = self->m_base.m_model;
    if (!model || row < 0 || row >= model->m_rows) return;
    font = XWidget_font((const XWidget*)self);
    content = XPainter_textHeight(&font);
    XFont_deinit_base((XClass*)&font);
    if (content <= 0) return;
    /* 统一行高承载：单行调整即整体行高（与 setRowHeight 同一存储）。 */
    XTableView_setRowHeight(self, content + 2 * XTV_CONTENT_VMARGIN);
    XWidget_update((XWidget*)self);
}

void XTableView_resizeRowsToContents(XTableView* self)
{
    XAbstractItemModel* model;
    int rows;
    int row;
    if (!self) return;
    model = self->m_base.m_model;
    rows = model ? model->m_rows : 0;
    for (row = 0; row < rows; ++row)
        XTableView_resizeRowToContents(self, row);
}

void XTableView_setShowGrid(XTableView* self, bool show)
{
    int style;
    if (!self) return;
    /* 联动规则：showGrid == (gridStyle != NoGrid)。开启时若当前无风格
     * 则回落实线；关闭时风格统一置 NoGrid，镜像位随之同步。 */
    style = show ? (self->m_gridStyle != XTABLEVIEW_GRID_NO
                        ? self->m_gridStyle
                        : XTABLEVIEW_GRID_SOLID)
                 : XTABLEVIEW_GRID_NO;
    if (self->m_gridStyle == style && self->m_gridVisible == (show ? 1 : 0))
        return;
    self->m_gridStyle = style;
    self->m_gridVisible = show ? 1 : 0;
    XWidget_update((XWidget*)self);
}

bool XTableView_showGrid(const XTableView* self)
{ return self ? (self->m_gridVisible != 0) : true; }

void XTableView_setGridStyle(XTableView* self, int style)
{
    if (!self) return;
    if (style != XTABLEVIEW_GRID_NO && style != XTABLEVIEW_GRID_SOLID &&
        style != XTABLEVIEW_GRID_DASH)
        return; /* 仅接受简化枚举值（数值对齐 Qt::PenStyle）。 */
    if (self->m_gridStyle == style) return;
    self->m_gridStyle = style;
    self->m_gridVisible = (style != XTABLEVIEW_GRID_NO) ? 1 : 0;
    XWidget_update((XWidget*)self);
}

int XTableView_gridStyle(const XTableView* self)
{ return self ? self->m_gridStyle : XTABLEVIEW_GRID_SOLID; }

void XTableView_setWordWrap(XTableView* self, bool wrap)
{
    if (!self || self->m_wordWrap == wrap) return;
    self->m_wordWrap = wrap;
    XWidget_update((XWidget*)self);
}

bool XTableView_wordWrap(const XTableView* self)
{ return self ? self->m_wordWrap : false; }

void XTableView_setCornerButtonEnabled(XTableView* self, bool enable)
{
    if (!self || self->m_cornerButton == enable) return;
    self->m_cornerButton = enable;
    XWidget_update((XWidget*)self);
}

bool XTableView_isCornerButtonEnabled(const XTableView* self)
{ return self ? self->m_cornerButton : true; }

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

/* ==================== 滚动偏移（对标 QTableView 视口滚动） ==================== */

/** @brief 读取滚动偏移（视口原点在内容坐标中的位置）。 */
static void xtv_scrollOffsets(const XTableView* tv, int* outX, int* outY)
{
    XScrollBar* vsb = XAbstractScrollArea_verticalScrollBar(
        (const XAbstractScrollArea*)&tv->m_base);
    XScrollBar* hsb = XAbstractScrollArea_horizontalScrollBar(
        (const XAbstractScrollArea*)&tv->m_base);
    if (outX) *outX = hsb ? XScrollBar_value(hsb) : 0;
    if (outY) *outY = vsb ? XScrollBar_value(vsb) : 0;
}

/** @brief 按内容尺寸维护滚动条范围（值变化才写，避免重绘回环）。 */
static void xtv_updateScrollRanges(XTableView* tv)
{
    XAbstractItemModel* model = tv->m_base.m_model;
    XScrollBar* vsb;
    XScrollBar* hsb;
    int rows = model ? model->m_rows : 0;
    int cols = model ? model->m_cols : 0;
    int i;
    int contentH = XTV_HEADER_H;
    int contentW = 0;
    int rh = tv->m_rowHeight > 0 ? tv->m_rowHeight : XTV_DEFAULT_ROW_HEIGHT;
    int vMax;
    int hMax;
    int viewW = XWidget_width((XWidget*)tv);
    int viewH = XWidget_height((XWidget*)tv);
    vsb = XAbstractScrollArea_verticalScrollBar(
        (const XAbstractScrollArea*)&tv->m_base);
    hsb = XAbstractScrollArea_horizontalScrollBar(
        (const XAbstractScrollArea*)&tv->m_base);
    for (i = 0; i < rows; ++i) {
        if (xtv_hiddenRaw(tv->m_rowHidden, tv->m_rowHiddenCount, i))
            continue;
        contentH += rh;
    }
    for (i = 0; i < cols; ++i) {
        if (xtv_hiddenRaw(tv->m_colHidden, tv->m_colHiddenCount, i))
            continue;
        contentW += XTableView_columnWidth(tv, i);
    }
    vMax = contentH > viewH ? contentH - viewH : 0;
    hMax = contentW > viewW ? contentW - viewW : 0;
    if (vsb && XScrollBar_maximum(vsb) != vMax)
        XScrollBar_setRange(vsb, 0, vMax);
    if (hsb && XScrollBar_maximum(hsb) != hMax)
        XScrollBar_setRange(hsb, 0, hMax);
}

/* ==================== 位置反查（行高/列宽/隐藏感知） ==================== */

int XTableView_rowAt(const XTableView* self, int y)
{
    XAbstractItemModel* model;
    int rows;
    int row;
    int yAcc;
    int rh;
    if (!self) return -1;
    model = self->m_base.m_model;
    rows = model ? model->m_rows : 0;
    if (y < 0 || rows <= 0) return -1;
    {
        int offX;
        int offY;
        xtv_scrollOffsets(self, &offX, &offY);
        y += offY; /* 视口坐标 → 内容坐标（此前命中不含滚动偏移）。 */
    }
    /* 与 indexAt 同一坐标系：扣除上方表头区高度。 */
    yAcc = y - XTV_HEADER_H;
    if (yAcc < 0) return -1;
    rh = self->m_rowHeight > 0 ? self->m_rowHeight : XTV_DEFAULT_ROW_HEIGHT;
    /* 隐藏行占高 0：累计可见高度后按统一行高逐行判定。 */
    for (row = 0; row < rows; ++row) {
        if (xtv_hiddenRaw(self->m_rowHidden, self->m_rowHiddenCount, row))
            continue;
        if (yAcc < rh) return row;
        yAcc -= rh;
    }
    return -1;
}

int XTableView_columnAt(const XTableView* self, int x)
{
    XAbstractItemModel* model;
    int cols;
    int col;
    int xAcc;
    if (!self) return -1;
    model = self->m_base.m_model;
    cols = model ? model->m_cols : 0;
    if (x < 0 || cols <= 0) return -1;
    {
        int offX;
        int offY;
        xtv_scrollOffsets(self, &offX, &offY);
        x += offX; /* 视口坐标 → 内容坐标。 */
    }
    xAcc = 0;
    /* 隐藏列占宽 0：直接跳过不参与累计。 */
    for (col = 0; col < cols; ++col) {
        int w;
        if (xtv_hiddenRaw(self->m_colHidden, self->m_colHiddenCount, col))
            continue;
        w = XTableView_columnWidth(self, col);
        if (x < xAcc + w) return col;
        xAcc += w;
    }
    return -1;
}

/* ==================== 行/列视口位置（表头区/隐藏感知） ==================== */

int XTableView_rowViewportPosition(const XTableView* self, int row)
{
    XAbstractItemModel* model;
    int rows;
    int i;
    int yAcc;
    int rh;
    if (!self || row < 0) return -1;
    model = self->m_base.m_model;
    rows = model ? model->m_rows : 0;
    if (row >= rows) return -1;
    rh = self->m_rowHeight > 0 ? self->m_rowHeight : XTV_DEFAULT_ROW_HEIGHT;
    /* 与 rowAt 互逆：顶部内嵌表头区高度计入起点；隐藏行占高 0 跳过。 */
    yAcc = XTV_HEADER_H;
    for (i = 0; i < row; ++i) {
        if (xtv_hiddenRaw(self->m_rowHidden, self->m_rowHiddenCount, i))
            continue;
        yAcc += rh;
    }
    return yAcc;
}

int XTableView_columnViewportPosition(const XTableView* self, int column)
{
    XAbstractItemModel* model;
    int cols;
    int col;
    int xAcc;
    if (!self || column < 0) return -1;
    model = self->m_base.m_model;
    cols = model ? model->m_cols : 0;
    if (column >= cols) return -1;
    /* 与 columnAt 互逆：无左侧垂直表头区，起点为 0；隐藏列占宽 0 跳过。 */
    xAcc = 0;
    for (col = 0; col < column; ++col) {
        if (xtv_hiddenRaw(self->m_colHidden, self->m_colHiddenCount, col))
            continue;
        xAcc += XTableView_columnWidth(self, col);
    }
    return xAcc;
}

/* ==================== 视觉矩形（表头区/隐藏/行列宽高感知） ==================== */

XRect XTableView_visualRect(const XTableView* self, int row, int column)
{
    XRect r;
    XAbstractItemModel* model;
    int rows;
    int cols;
    r.x = 0;
    r.y = 0;
    r.width = 0;
    r.height = 0;
    if (!self || row < 0 || column < 0) return r;
    model = self->m_base.m_model;
    rows = model ? model->m_rows : 0;
    cols = model ? model->m_cols : 0;
    if (row >= rows || column >= cols) return r;
    /* 隐藏单元格不可见：行/列任一隐藏返回空矩形（对标 Qt）。 */
    if (xtv_hiddenRaw(self->m_rowHidden, self->m_rowHiddenCount, row))
        return r;
    if (xtv_hiddenRaw(self->m_colHidden, self->m_colHiddenCount, column))
        return r;
    /* 与 rowAt/columnAt 同一坐标系：位置查询含表头区高度，宽高取
       统一行高与列宽（行/列已判界，位置查询必为有效值）。 */
    r.x = XTableView_columnViewportPosition(self, column);
    r.y = XTableView_rowViewportPosition(self, row);
    {
        int offX;
        int offY;
        xtv_scrollOffsets(self, &offX, &offY);
        r.x -= offX;
        r.y -= offY;
    }
    r.width = XTableView_columnWidth(self, column);
    r.height = XTableView_rowHeight(self);
    return r;
}

/* ==================== 行/列隐藏 ==================== */

void XTableView_setRowHidden(XTableView* self, int row, bool hide)
{
    if (!self || row < 0) return;
    /* 状态表按需扩容到 row+1（与列宽容量表同风格，可先于模型配置）。 */
    if (!xtv_ensureBoolTable(&self->m_rowHidden, &self->m_rowHiddenCount,
                             row + 1))
        return;
    if (self->m_rowHidden[row] == hide) return;
    self->m_rowHidden[row] = hide;
    XWidget_update((XWidget*)self);
}

bool XTableView_isRowHidden(const XTableView* self, int row)
{
    return self ? xtv_hiddenRaw(self->m_rowHidden, self->m_rowHiddenCount, row)
                : false;
}

void XTableView_setColumnHidden(XTableView* self, int column, bool hide)
{
    if (!self || column < 0) return;
    if (!xtv_ensureBoolTable(&self->m_colHidden, &self->m_colHiddenCount,
                             column + 1))
        return;
    if (self->m_colHidden[column] == hide) return;
    self->m_colHidden[column] = hide;
    XWidget_update((XWidget*)self);
}

bool XTableView_isColumnHidden(const XTableView* self, int column)
{
    return self ? xtv_hiddenRaw(self->m_colHidden, self->m_colHiddenCount,
                                column)
                : false;
}

void XTableView_hideRow(XTableView* self, int row)
{ XTableView_setRowHidden(self, row, true); }

void XTableView_showRow(XTableView* self, int row)
{ XTableView_setRowHidden(self, row, false); }

void XTableView_hideColumn(XTableView* self, int column)
{ XTableView_setColumnHidden(self, column, true); }

void XTableView_showColumn(XTableView* self, int column)
{ XTableView_setColumnHidden(self, column, false); }

/* ==================== 跨行/列合并（对标 QTableView::setSpan 族） ==================== */

void XTableView_setSpan(XTableView* self, int row, int column,
                        int rowSpan, int columnSpan)
{
    int i;
    if (!self || row < 0 || column < 0) return;
    if (rowSpan <= 0 || columnSpan <= 0) return;
    for (i = 0; i < self->m_spanCount; ++i) {
        XTableViewSpan* s = &self->m_spans[i];
        if (s->row != row || s->column != column) continue;
        if (rowSpan == 1 && columnSpan == 1) {
            /* 对标 Qt：均 1 即取消合并（后序区间前移补位）。 */
            if (i < self->m_spanCount - 1)
                XMemmove(&self->m_spans[i], &self->m_spans[i + 1],
                         sizeof(XTableViewSpan) *
                             (size_t)(self->m_spanCount - 1 - i));
            --self->m_spanCount;
        } else {
            s->rowSpan = rowSpan;
            s->columnSpan = columnSpan;
        }
        XWidget_update((XWidget*)self);
        return;
    }
    if (rowSpan == 1 && columnSpan == 1) return; /* 取消未存在的合并：无操作。 */
    if (!xtv_ensureSpanTable(self, self->m_spanCount + 1)) return;
    self->m_spans[self->m_spanCount].row = row;
    self->m_spans[self->m_spanCount].column = column;
    self->m_spans[self->m_spanCount].rowSpan = rowSpan;
    self->m_spans[self->m_spanCount].columnSpan = columnSpan;
    ++self->m_spanCount;
    XWidget_update((XWidget*)self);
}

int XTableView_rowSpan(const XTableView* self, int row, int column)
{
    const XTableViewSpan* s;
    if (!self || row < 0 || column < 0) return 1;
    s = xtv_spanFind(self, row, column);
    return s ? s->rowSpan : 1;
}

int XTableView_columnSpan(const XTableView* self, int row, int column)
{
    const XTableViewSpan* s;
    if (!self || row < 0 || column < 0) return 1;
    s = xtv_spanFind(self, row, column);
    return s ? s->columnSpan : 1;
}

void XTableView_clearSpans(XTableView* self)
{
    if (!self || self->m_spanCount == 0) return;
    self->m_spanCount = 0;
    XWidget_update((XWidget*)self);
}

/* ==================== 表头对象挂接（借用承载） ==================== */

void XTableView_setHorizontalHeader(XTableView* self, XHeaderView* header)
{
    if (!self) return;
    /* 方向校验（对标 Qt 断言语义）：水平表头须为 0=水平方向，不符忽略。 */
    if (header && header->m_orientation != 0) return;
    if (self->m_horizontalHeader == header) return;
    self->m_horizontalHeader = header;
    XWidget_update((XWidget*)self);
}

XHeaderView* XTableView_horizontalHeader(const XTableView* self)
{ return self ? self->m_horizontalHeader : NULL; }

void XTableView_setVerticalHeader(XTableView* self, XHeaderView* header)
{
    if (!self) return;
    /* 方向校验（对标 Qt 断言语义）：垂直表头须为 1=垂直方向，不符忽略。 */
    if (header && header->m_orientation != 1) return;
    if (self->m_verticalHeader == header) return;
    self->m_verticalHeader = header;
    XWidget_update((XWidget*)self);
}

XHeaderView* XTableView_verticalHeader(const XTableView* self)
{ return self ? self->m_verticalHeader : NULL; }

/* ==================== 命中测试（行高/列宽/隐藏感知） ==================== */

static bool VXTableView_indexAt(const XAbstractItemView* view, int x, int y,
                                int* outRow, int* outCol)
{
    XTableView* tv = (XTableView*)view;
    XAbstractItemModel* model;
    int row;
    int col;
    int cols;
    if (outRow) *outRow = -1;
    if (outCol) *outCol = -1;
    if (!tv) return false;
    /* 行/列数变化生效入口：先同步隐藏状态表覆盖当前模型尺寸。 */
    xtv_syncHiddenTables(tv);
    model = view->m_model;
    if (!model || model->m_rows <= 0 || model->m_cols <= 0) return false;
    cols = model->m_cols;
    row = XTableView_rowAt(tv, y);
    col = XTableView_columnAt(tv, x);
    if (row < 0) return false;
    /* 保留原钳制语义：横向越界时收敛到末列（x<0 收敛到首列）。 */
    if (col < 0) col = (x < 0) ? 0 : cols - 1;
    {
        /* 合并区间命中映射（对标 Qt）：被覆盖格反查回原点。 */
        const XTableViewSpan* span = xtv_spanFind(tv, row, col);
        if (span) {
            row = span->row;
            col = span->column;
        }
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
    int offX;
    int offY;
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
        XPainter_drawText(&painter, 4, 16, "（无模型）", 0xFF888888u);
        XPainter_end(&painter);
        XPainter_deinit(&painter);
        return;
    }
    rows = model->m_rows;
    cols = model->m_cols;
    /* 行/列数变化生效入口：先同步隐藏状态表覆盖当前模型尺寸。 */
    xtv_syncHiddenTables(tv);
    xtv_updateScrollRanges(tv);
    /* 表头区（水平随内容滚动）。 */
    {
        XRect hr = { 0, 0, r.width, XTV_HEADER_H };
        XPainter_fillRect(&painter, &hr, 0xFFF0F0F0u);
        xtv_scrollOffsets(tv, &offX, &offY);
        x = -offX;
        for (col = 0; col < cols && x < r.width; ++col) {
            int w;
            const char* text;
            /* 声明在列循环层：buf 指针经 text 活到 drawText 之后
             * （块内声明出块即死，ASan stack-use-after-scope）。 */
            char buf[16];
            if (xtv_hiddenRaw(tv->m_colHidden, tv->m_colHiddenCount, col))
                continue; /* 隐藏列占宽 0：跳过表头段。 */
            w = XTableView_columnWidth(tv, col);
            text = XAbstractItemModel_headerData_2(model, col, 0);
            if (!text || !text[0]) {
                XSnprintf(buf, sizeof(buf), "%d", col + 1);
                text = buf;
            }
            XPainter_setPen(&painter, 0xFF444444u);
            XPainter_drawText(&painter, x + 4, XTV_HEADER_H - 6, text,
                              0xFF444444u);
            XPainter_setPen(&painter, 0xFFCCCCCCu);
            XPainter_drawLine(&painter, x + w - 1, 1, x + w - 1,
                              XTV_HEADER_H - 1);
            x += w;
        }
    }
    /* 数据区网格（合并区间：原点格按合并宽高绘制，被覆盖格跳过）。 */
    y = XTV_HEADER_H - offY;
    for (row = 0; row < rows && y < r.height; ++row) {
        int rh = tv->m_rowHeight > 0 ? tv->m_rowHeight
                                     : XTV_DEFAULT_ROW_HEIGHT;
        if (xtv_hiddenRaw(tv->m_rowHidden, tv->m_rowHiddenCount, row))
            continue; /* 隐藏行占高 0：不绘制也不累计高度。 */
        x = -offX;
        for (col = 0; col < cols && x < r.width; ++col) {
            int w;
            int cellH = rh;
            bool sel;
            if (xtv_hiddenRaw(tv->m_colHidden, tv->m_colHiddenCount, col))
                continue; /* 隐藏列占宽 0：跳过单元格。 */
            w = XTableView_columnWidth(tv, col);
            {
                const XTableViewSpan* span = xtv_spanFind(tv, row, col);
                if (span && (span->row != row || span->column != col)) {
                    x += w;
                    continue; /* 被覆盖格不绘制：由原点格合并覆盖。 */
                }
                if (span) {
                    /* 原点格：合并宽高=区间内可见列宽/行高之和。 */
                    int i2;
                    w = 0;
                    cellH = 0;
                    for (i2 = col; i2 < col + span->columnSpan; ++i2)
                        if (!xtv_hiddenRaw(tv->m_colHidden,
                                           tv->m_colHiddenCount, i2))
                            w += XTableView_columnWidth(tv, i2);
                    for (i2 = row; i2 < row + span->rowSpan; ++i2)
                        if (!xtv_hiddenRaw(tv->m_rowHidden,
                                           tv->m_rowHiddenCount, i2))
                            cellH += tv->m_rowHeight > 0
                                         ? tv->m_rowHeight
                                         : XTV_DEFAULT_ROW_HEIGHT;
                }
            }
            if (w <= 0 || cellH <= 0) {
                x += XTableView_columnWidth(tv, col);
                continue; /* 区间整体不可见：占位推进后跳过。 */
            }
            sel = view->m_selectionModel &&
                  XItemSelectionModel_isSelected(
                      view->m_selectionModel, row, col);
            {
                XRect cell = { x, y, w, cellH };
                if (sel)
                    XPainter_fillRect(&painter, &cell, 0xFFCCE4FFu);
                else if (view->m_alternatingRowColors && (row & 1))
                    XPainter_fillRect(&painter, &cell, 0xFFF7F7F7u);
            }
            {
                const char* text = XAbstractItemModel_data_2(model, row, col);
                if (text && text[0]) {
                    XPainter_setPen(&painter, 0xFF000000u);
                    XPainter_drawText(&painter, x + 4, y + cellH - 6, text,
                                  0xFF000000u);
                }
            }
            if (tv->m_gridVisible) {
                XPainter_setPen(&painter, 0xFFDDDDDDu);
#if XPAINTER_PENSTYLE_ON
                /* setPen(颜色) 会重置为实线，虚线风格须在其后设置。 */
                if (tv->m_gridStyle == XTABLEVIEW_GRID_DASH)
                    XPainter_setPenStyle(&painter,
                                         XPainterPenStyle_DashLine);
#endif /* XPAINTER_PENSTYLE_ON */
                XPainter_drawLine(&painter, x + w - 1, y, x + w - 1,
                                  y + cellH - 1);
                XPainter_drawLine(&painter, x, y + cellH - 1, x + w - 1,
                                  y + cellH - 1);
            }
            x += w;
        }
        y += rh;
    }
    XPainter_end(&painter);
    XPainter_deinit(&painter);
}

#endif /* XWIDGET_ON && XTABLEWIDGET_ON */
