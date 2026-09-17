/**
 * @file       XListView.c
 * @brief      XListView 列表视图实现（model 单列垂直渲染 + QListView
 *             状态族存取与轻量绘制联动）。
 * @details    状态族（flow/gridSize/wrapping/viewMode/resizeMode/
 *             layoutMode/batchSize/itemAlignment/selectionRectVisible/
 *             wordWrap）对标 Qt 6.8 QListView 同名属性：在平铺行模型下
 *             以标量存储 + 触发重绘；其中网格高参与槽位高度、网格宽
 *             收缩条目单元、条目对齐/词换行参与文本绘制、行隐藏平行
 *             数组参与绘制与命中跳过（数组模式参照 XTreeView 行状态表）。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XListView.h"

#include "XAlgorithm.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XPainter.h"
#include "XWidget_Protected.h"

#if XWIDGET_ON && XTABLEWIDGET_ON

#define XLISTVIEW_DEFAULT_ROW_H 24
#define XLISTVIEW_DEFAULT_BATCH_SIZE 100

static void VXListView_deinit(XListView* self);
static void VXListView_paintEvent(XWidget* self, XEvent* event);

/** @brief 查询模型行数（无模型返回 0）。 */
static int xlv_modelRows(const XListView* self)
{
    XAbstractItemModel* model;
    if (!self) return 0;
    model = self->m_base.m_model;
    return model ? model->m_rows : 0;
}

/** @brief 查询有效行高（<=0 回退默认 24）。 */
static int xlv_effectiveRowHeight(const XListView* self)
{
    if (!self || self->m_rowHeight <= 0) return XLISTVIEW_DEFAULT_ROW_H;
    return self->m_rowHeight;
}

/** @brief 查询绘制/命中槽位高度（网格高启用时取与行高的较大值）。 */
static int xlv_slotHeight(const XListView* self)
{
    int rh = xlv_effectiveRowHeight(self);
    if (self && self->m_gridHeight > rh) return self->m_gridHeight;
    return rh;
}

/** @brief 查询条目单元宽度（网格宽启用且小于视口时收缩单元）。 */
static int xlv_slotWidth(const XListView* self, int viewportWidth)
{
    if (self && self->m_gridWidth > 0 && self->m_gridWidth < viewportWidth)
        return self->m_gridWidth;
    return viewportWidth;
}

/**
 * @brief 行隐藏状态表与模型行数同步（扩容新增行默认可见）。
 * @param self 目标视图。
 * @param count 目标长度（<0 视为 0）。
 * @note 同步入口：setRowHidden/isRowHidden 访问前、命中测试与绘制前；
 *       deinit 全路径另行释放数组（参照 XTreeView 行状态数组模式）。
 */
static void xlv_syncRowStates(XListView* self, int count)
{
    int old;
    if (!self) return;
    if (count < 0) count = 0;
    old = self->m_rowStateCount;
    if (count == old) return;
    if (count <= 0) {
        if (self->m_rowHidden) {
            XFree_System(self->m_rowHidden);
            self->m_rowHidden = NULL;
        }
        self->m_rowStateCount = 0;
        return;
    }
    self->m_rowHidden = (bool*)XRealloc_System(
        self->m_rowHidden, sizeof(bool) * (size_t)count);
    if (self->m_rowHidden) {
        if (count > old)
            XMemset(self->m_rowHidden + old, 0,
                    sizeof(bool) * (size_t)(count - old));
        self->m_rowStateCount = count;
    } else {
        self->m_rowStateCount = 0;
    }
}

/** @brief 行隐藏状态表与模型行数同步入口（行数=模型行数）。 */
static void xlv_refreshRowStates(XListView* self)
{ xlv_syncRowStates(self, xlv_modelRows(self)); }

/** @brief 查询行是否隐藏（越界、未同步或对象无效默认可见）。 */
static bool xlv_rowIsHidden(const XListView* self, int row)
{
    if (!self || !self->m_rowHidden || row < 0
        || row >= self->m_rowStateCount)
        return false;
    return self->m_rowHidden[row];
}

/**
 * @brief 绘制行文本：无对齐/换行状态走历史基线快速路径；否则走
 *        矩形布局路径（条目对齐按位参与、词换行按 TextWordWrap）。
 * @param painter 绘制器。
 * @param lv 目标视图。
 * @param cell 行单元矩形（含网格收缩后的宽度/槽位高度）。
 * @param text UTF-8 文本；NULL 或空串无操作。
 */
static void xlv_drawRowText(XPainter* painter, const XListView* lv,
                            const XRect* cell, const char* text)
{
    int flags;
    if (!painter || !lv || !cell || !text || text[0] == '\0') return;
    flags = lv->m_itemAlignment & (XPAINTER_TEXT_ALIGN_HORIZONTAL_MASK |
                                   XPAINTER_TEXT_ALIGN_VERTICAL_MASK);
    if (lv->m_wordWrap) flags |= XPAINTER_TEXT_WORD_WRAP;
    if (flags == 0) {
        /* 默认路径：与历史渲染一致（左缘 4px、基线 y+行高-6）。 */
        XPainter_setPen(painter, 0xFF000000u);
        XPainter_drawText(painter, 4,
                          cell->y + xlv_effectiveRowHeight(lv) - 6,
                          text, 0);
        return;
    }
    if (!(flags & XPAINTER_TEXT_ALIGN_HORIZONTAL_MASK))
        flags |= XPAINTER_TEXT_ALIGN_LEFT;
    if (!(flags & XPAINTER_TEXT_ALIGN_VERTICAL_MASK))
        flags |= XPAINTER_TEXT_ALIGN_BOTTOM;
    XPainter_drawTextRect(painter, cell, (uint32_t)flags, text,
                          0xFF000000u);
}

/* 命中测试覆盖：列表按行渲染（列固定 modelColumn），跳过隐藏行，
   槽位步进与绘制一致（网格高参与）。 */
static bool xlv_indexAt(XListView* self, int x, int y,
                        int* outRow, int* outCol)
{
    int row;
    int rows;
    int slotY;
    int slotH;
    (void)x;
    if (outRow) *outRow = -1;
    if (outCol) *outCol = self ? self->m_modelColumn : 0;
    if (!self || y < 0) return false;
    xlv_refreshRowStates(self);
    rows = xlv_modelRows(self);
    slotH = xlv_slotHeight(self) + self->m_spacing;
    if (slotH <= 0) return false;
    slotY = 0;
    for (row = 0; row < rows; ++row) {
        if (xlv_rowIsHidden(self, row)) continue;
        if (y < slotY + slotH) {
            if (outRow) *outRow = row;
            return true;
        }
        slotY += slotH;
    }
    return false;
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
    int slotH;
    int slotW;
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
    xlv_refreshRowStates(lv);
    rows = model->m_rows;
    rh = xlv_effectiveRowHeight(lv);
    slotH = xlv_slotHeight(lv);
    slotW = xlv_slotWidth(lv, r.width);
    y = 0;
    for (row = 0; row < rows && y < r.height; ++row) {
        int h = slotH + lv->m_spacing;
        XRect cell = { 0, y, slotW, slotH };
        bool sel;
        bool cur;
        if (xlv_rowIsHidden(lv, row)) continue;
        sel = view->m_selectionModel &&
              XItemSelectionModel_isSelected(
                  view->m_selectionModel, row, lv->m_modelColumn);
        cur = (view->m_currentRow == row &&
               view->m_currentColumn == lv->m_modelColumn);
        if (sel)
            XPainter_fillRect(&painter, &cell, 0xFFCCE4FFu);
        else if (view->m_alternatingRowColors && (row & 1))
            XPainter_fillRect(&painter, &cell, 0xFFF7F7F7u);
        if (cur && !sel)
            XPainter_fillRect(&painter, &cell, 0xFFE8F1FFu);
        xlv_drawRowText(
            &painter, lv, &cell,
            XAbstractItemModel_data_2(model, row, lv->m_modelColumn));
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
    self->m_flow = XListViewFlow_TopDown;
    self->m_gridWidth = -1;
    self->m_gridHeight = -1;
    self->m_wrapping = false;
    self->m_viewMode = XListViewViewMode_ListMode;
    self->m_resizeMode = XListViewResizeMode_Static;
    self->m_layoutMode = XListViewLayoutMode_SinglePass;
    self->m_batchSize = XLISTVIEW_DEFAULT_BATCH_SIZE;
    self->m_itemAlignment = 0;
    self->m_selectionRectVisible = false;
    self->m_wordWrap = false;
    self->m_rowHidden = NULL;
    self->m_rowStateCount = 0;
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
    if (self->m_rowHidden) {
        XFree_System(self->m_rowHidden);
        self->m_rowHidden = NULL;
    }
    self->m_rowStateCount = 0;
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

void XListView_setFlow(XListView* self, int flow)
{
    if (!self) return;
    if (flow != XListViewFlow_TopDown && flow != XListViewFlow_LeftToRight)
        return;
    if (self->m_flow == flow) return;
    self->m_flow = flow;
    XWidget_update((XWidget*)self);
}

int XListView_flow(const XListView* self)
{ return self ? self->m_flow : XListViewFlow_TopDown; }

void XListView_setGridSize(XListView* self, int width, int height)
{
    if (!self) return;
    self->m_gridWidth = width > 0 ? width : -1;
    self->m_gridHeight = height > 0 ? height : -1;
    XWidget_update((XWidget*)self);
}

int XListView_gridSizeWidth(const XListView* self)
{ return self ? self->m_gridWidth : -1; }

int XListView_gridSizeHeight(const XListView* self)
{ return self ? self->m_gridHeight : -1; }

void XListView_setWrapping(XListView* self, bool enable)
{
    if (!self) return;
    if (self->m_wrapping == enable) return;
    self->m_wrapping = enable;
    XWidget_update((XWidget*)self);
}

bool XListView_isWrapping(const XListView* self)
{ return self ? self->m_wrapping : false; }

void XListView_setViewMode(XListView* self, int mode)
{
    if (!self) return;
    if (mode != XListViewViewMode_ListMode
        && mode != XListViewViewMode_IconMode)
        return;
    if (self->m_viewMode == mode) return;
    self->m_viewMode = mode;
    /* 对齐 Qt setViewMode 联动：ListMode 复位换行/回退 TopDown；
       IconMode 打开换行/切 LeftToRight。 */
    if (mode == XListViewViewMode_ListMode) {
        self->m_wrapping = false;
        self->m_flow = XListViewFlow_TopDown;
    } else {
        self->m_wrapping = true;
        self->m_flow = XListViewFlow_LeftToRight;
    }
    XWidget_update((XWidget*)self);
}

int XListView_viewMode(const XListView* self)
{ return self ? self->m_viewMode : XListViewViewMode_ListMode; }

void XListView_setResizeMode(XListView* self, int mode)
{
    if (!self) return;
    if (mode != XListViewResizeMode_Static
        && mode != XListViewResizeMode_Adjust)
        return;
    self->m_resizeMode = mode;
}

int XListView_resizeMode(const XListView* self)
{ return self ? self->m_resizeMode : XListViewResizeMode_Static; }

void XListView_setLayoutMode(XListView* self, int mode)
{
    if (!self) return;
    if (mode != XListViewLayoutMode_SinglePass
        && mode != XListViewLayoutMode_Batched)
        return;
    self->m_layoutMode = mode;
}

int XListView_layoutMode(const XListView* self)
{ return self ? self->m_layoutMode : XListViewLayoutMode_SinglePass; }

void XListView_setBatchSize(XListView* self, int batchSize)
{
    /* 对标 Qt：非法批量（<=0）拒绝并保持原值。 */
    if (self && batchSize > 0)
        self->m_batchSize = batchSize;
}

int XListView_batchSize(const XListView* self)
{ return self ? self->m_batchSize : 0; }

void XListView_setItemAlignment(XListView* self, int alignment)
{
    if (!self) return;
    if (self->m_itemAlignment == alignment) return;
    self->m_itemAlignment = alignment;
    XWidget_update((XWidget*)self);
}

int XListView_itemAlignment(const XListView* self)
{ return self ? self->m_itemAlignment : 0; }

void XListView_setSelectionRectVisible(XListView* self, bool show)
{
    if (self) self->m_selectionRectVisible = show;
}

bool XListView_isSelectionRectVisible(const XListView* self)
{ return self ? self->m_selectionRectVisible : false; }

void XListView_setWordWrap(XListView* self, bool on)
{
    if (!self) return;
    if (self->m_wordWrap == on) return;
    self->m_wordWrap = on;
    XWidget_update((XWidget*)self);
}

bool XListView_wordWrap(const XListView* self)
{ return self ? self->m_wordWrap : false; }

void XListView_setRowHidden(XListView* self, int row, bool hide)
{
    if (!self || row < 0) return;
    xlv_refreshRowStates(self);
    if (row >= self->m_rowStateCount || !self->m_rowHidden) return;
    if (self->m_rowHidden[row] == hide) return;
    self->m_rowHidden[row] = hide;
    XWidget_update((XWidget*)self);
}

bool XListView_isRowHidden(const XListView* self, int row)
{
    return xlv_rowIsHidden(self, row);
}

#endif /* XWIDGET_ON && XTABLEWIDGET_ON */
