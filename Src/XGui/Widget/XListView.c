/**
 * @file       XListView.c
 * @brief      XListView 列表视图实现（model 单列垂直渲染 + QListView
 *             状态族存取与轻量绘制联动）。
 * @details    状态族（flow/gridSize/wrapping/viewMode/resizeMode/
 *             layoutMode/movement/batchSize/uniformItemSizes/
 *             itemAlignment/selectionRectVisible/wordWrap）对标
 *             Qt 6.8 QListView 同名属性：在平铺行模型下
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

/* role 渲染消费常量（对标 QStyledItemDelegate::paint 的条目布局子集，
 * 勾选框/装饰与文本的间距，像素）。 */
#define XLV_ROLE_CHECK_BOX 12
#define XLV_ROLE_CONTENT_GAP 4

static void VXListView_deinit(XListView* self);
static void VXListView_paintEvent(XWidget* self, XEvent* event);
static void VXListView_scrollContentsBy(XAbstractScrollArea* area, int dx,
                                        int dy);

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
 * @brief 绘制简笔勾选框（CheckStateRole 渲染消费；对标 Qt
 *        PE_IndicatorCheckBox 的简化笔画：外框 + 选中勾线/半选中横线）。
 * @param painter 绘制器。
 * @param cell 行单元矩形。
 * @param checkState XItemCheckState 值（0..2）。
 * @return 勾选框占用的内容宽度（框 + 右侧间距；供文本区推进）。
 */
static int xlv_drawCheckIndicator(XPainter* painter, const XRect* cell,
                                  int checkState)
{
    int box = XLV_ROLE_CHECK_BOX;
    int x = cell->x + 2;
    int y = cell->y + (cell->height - box) / 2;
    XRect frame;
    if (!painter || checkState < 0) return 0;
    XRect_init(&frame, x, y, box, box);
    XPainter_setPen(painter, 0xFF666666u);
    XPainter_drawRect(painter, &frame);
    if (checkState == XItemCheckState_Checked) {
        /* 选中：框内两段折线勾（对标 Qt 勾选标记）。 */
        XPainter_setPen(painter, 0xFF207F20u);
        XPainter_drawLine(painter, x + 2, y + box / 2,
                          x + box / 2, y + box - 3);
        XPainter_drawLine(painter, x + box / 2, y + box - 3,
                          x + box - 2, y + 2);
    } else if (checkState == XItemCheckState_PartiallyChecked) {
        /* 半选：框内中横线（对标 Qt 半选标记）。 */
        XPainter_setPen(painter, 0xFF207F20u);
        XPainter_drawLine(painter, x + 2, y + box / 2,
                          x + box - 2, y + box / 2);
    }
    return box + XLV_ROLE_CONTENT_GAP;
}

/**
 * @brief 绘制 DecorationRole 图像（渲染消费；对标 QStyledItemDelegate
 *        的 decoration 子集——原尺寸左置、随行高垂直居中）。
 * @param painter 绘制器。
 * @param cell 行单元矩形。
 * @param decoration 装饰借用指针（XImage*；NULL=未设置）。
 * @return 占用宽度（图像宽 + 右侧间距；未设置/越界/超出行高返回 0）。
 */
static int xlv_drawDecoration(XPainter* painter, const XRect* cell,
                              const void* decoration)
{
    const XImage* image = (const XImage*)decoration;
    int w;
    int h;
    if (!painter || !image) return 0;
    w = XImage_width(image);
    h = XImage_height(image);
    /* 超出行单元的装饰不绘制（本库无图标缩放承载，避免越行覆盖）。 */
    if (w <= 0 || h <= 0 || h > cell->height) return 0;
    XPainter_drawImage(painter, image, cell->x + 2,
                       cell->y + (cell->height - h) / 2);
    return w + XLV_ROLE_CONTENT_GAP;
}

/**
 * @brief 绘制行文本：无对齐/换行/role 状态走历史基线快速路径；否则走
 *        矩形布局路径（条目对齐按位参与、词换行按 TextWordWrap）。
 * @param painter 绘制器。
 * @param lv 目标视图。
 * @param cell 行单元矩形（含网格收缩后的宽度/槽位高度）。
 * @param text UTF-8 文本；NULL 或空串无操作。
 * @param roleAlign TextAlignmentRole 对齐（XAlignment 位组合；0=未设置；
 *                  设置时覆盖视图级 itemAlignment，对标 Qt 逐条目对齐
 *                  优先于视图布局默认）。
 * @param roleFont FontRole 字体（NULL=未设置；对标 Qt 逐条目字体，
 *                  仅本次绘制生效，用毕恢复默认字库）。
 * @param contentX 文本区左缘 x（绝对坐标；勾选框/装饰占位推进后的值；
 *                 等于 cell->x 时与历史渲染像素一致）。
 */
static void xlv_drawRowText(XPainter* painter, const XListView* lv,
                            const XRect* cell, const char* text,
                            int roleAlign, const XFont* roleFont,
                            int contentX, uint32_t ink)
{
    int flags;
    if (!painter || !lv || !cell || !text || text[0] == '\0') return;
    if (roleFont) XPainter_setFont(painter, roleFont);
    flags = roleAlign
                ? (roleAlign & (XPAINTER_TEXT_ALIGN_HORIZONTAL_MASK |
                                XPAINTER_TEXT_ALIGN_VERTICAL_MASK))
                : (lv->m_itemAlignment &
                   (XPAINTER_TEXT_ALIGN_HORIZONTAL_MASK |
                    XPAINTER_TEXT_ALIGN_VERTICAL_MASK));
    if (lv->m_wordWrap) flags |= XPAINTER_TEXT_WORD_WRAP;
    if (flags == 0) {
        /* 默认路径：与历史渲染一致（左缘 4px、基线 y+行高-6）。
         * 颜色必须显式传不透明色：XPainter_drawText 直接以该参数作
         * ink（透明色写入=无像素），setPen 不影响此路径；ink 由调用
         * 方按选中态给 HighlightedText/WindowText（对标 Qt）。 */
        XPainter_setPen(painter, ink);
        XPainter_drawText(painter, contentX + 4,
                          cell->y + xlv_effectiveRowHeight(lv) - 6,
                          text, ink);
        if (roleFont) XPainter_setFont(painter, NULL);
        return;
    }
    if (!(flags & XPAINTER_TEXT_ALIGN_HORIZONTAL_MASK))
        flags |= XPAINTER_TEXT_ALIGN_LEFT;
    if (!(flags & XPAINTER_TEXT_ALIGN_VERTICAL_MASK))
        flags |= XPAINTER_TEXT_ALIGN_BOTTOM;
    {
        /* 文本区=内容占位推进后的剩余矩形（含历史 cell 宽度收缩）。 */
        XRect textRect;
        XRect_init(&textRect, contentX, cell->y,
                   cell->x + cell->width - contentX, cell->height);
        XPainter_drawTextRect(painter, &textRect, (uint32_t)flags, text,
                              ink);
    }
    if (roleFont) XPainter_setFont(painter, NULL);
}

/* 命中测试覆盖：列表按行渲染（列固定 modelColumn），跳过隐藏行，
   槽位步进与绘制一致（网格高参与）。 */
/* ==================== 滚动偏移（对标 QListView 视口滚动） ==================== */

/** @brief 读取滚动偏移（视口原点在内容坐标中的位置）。 */
static void xlv_scrollOffsets(XListView* self, int* outX, int* outY)
{
    XScrollBar* vsb = XAbstractScrollArea_verticalScrollBar((const XAbstractScrollArea*)&self->m_base);
    XScrollBar* hsb = XAbstractScrollArea_horizontalScrollBar((const XAbstractScrollArea*)&self->m_base);
    if (outX) *outX = hsb ? XScrollBar_value(hsb) : 0;
    if (outY) *outY = vsb ? XScrollBar_value(vsb) : 0;
}

/** @brief 按内容尺寸维护滚动条范围（值变化才写，避免重绘回环）。 */
static void xlv_updateScrollRanges(XListView* self, int contentHeight,
                                   int contentWidth, int viewW, int viewH)
{
    XScrollBar* vsb = XAbstractScrollArea_verticalScrollBar((const XAbstractScrollArea*)&self->m_base);
    XScrollBar* hsb = XAbstractScrollArea_horizontalScrollBar((const XAbstractScrollArea*)&self->m_base);
    int vMax = contentHeight > viewH ? contentHeight - viewH : 0;
    int hMax = contentWidth > viewW ? contentWidth - viewW : 0;
    if (vsb && XScrollBar_maximum(vsb) != vMax)
        XScrollBar_setRange(vsb, 0, vMax);
    if (hsb && XScrollBar_maximum(hsb) != hMax)
        XScrollBar_setRange(hsb, 0, hMax);
}

static bool xlv_indexAt(XListView* self, int x, int y,
                        int* outRow, int* outCol)
{
    int row;
    int rows;
    int slotY;
    int slotH;
    int offX;
    int offY;
    (void)x;
    if (outRow) *outRow = -1;
    if (outCol) *outCol = self ? self->m_modelColumn : 0;
    if (!self || y < 0) return false;
    xlv_scrollOffsets(self, &offX, &offY);
    y += offY; /* 视口坐标 → 内容坐标（此前命中不含滚动偏移）。 */
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

/** @brief 调色板取色助手（对照 XTableWidget.c xtw_color 范式；
 *  根因：自绘配色硬编码不读调色板，非默认调色板下仍白底黑字）。 */
static uint32_t xlv_color(const XListView* self, XPaletteColorRole role)
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
    int offX;
    int offY;
    int bottom;
    XPoint offset;
    uint32_t base;
    uint32_t highlight;
    uint32_t highlightedText;
    uint32_t windowText;
    uint32_t alternateBase;
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
    /* paintImage 返回的是顶层窗口后备存储，须按控件偏移平移到局部
     * 原点（对标 XTableWidget；缺平移时内容直绘到窗口 (0,0)）。 */
    offset = XWidget_paintOffset(self);
    if (offset.x != 0 || offset.y != 0)
        XPainter_translate(&painter, (float)offset.x, (float)offset.y);
    /* 视图族配色消费调色板（对标 Qt item view 的
     * Base/Highlight/HighlightedText/WindowText/AlternateBase）：
     * 默认调色板下 AlternateBase=(247,247,247) 与历史硬编码逐位一致，
     * 零视觉漂移；非默认调色板下随板取色。 */
    base            = xlv_color(lv, XPaletteColorRole_Base);
    highlight       = xlv_color(lv, XPaletteColorRole_Highlight);
    highlightedText = xlv_color(lv, XPaletteColorRole_HighlightedText);
    windowText      = xlv_color(lv, XPaletteColorRole_WindowText);
    alternateBase   = xlv_color(lv, XPaletteColorRole_AlternateBase);
    XPainter_fillRect(&painter, &r, base);
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
    xlv_updateScrollRanges(lv, rows * (slotH + lv->m_spacing), slotW,
                           r.width, r.height);
    xlv_scrollOffsets(lv, &offX, &offY);
    /* 内容坐标绘制：painter 平移 −偏移，行循环覆盖视口∩内容。此前
     * 绘制不含滚动偏移，滚动条/scrollTo 均无视觉效果。 */
    if (offX != 0 || offY != 0)
        XPainter_translate(&painter, -(float)offX, -(float)offY);
    y = 0;
    bottom = offY + r.height;
    for (row = 0; row < rows && y <= bottom; ++row) {
        int h = slotH + lv->m_spacing;
        XRect cell = { 0, y, slotW, slotH };
        bool sel;
        bool cur;
        if (xlv_rowIsHidden(lv, row)) continue;
        if (y + slotH >= offY) {
            sel = view->m_selectionModel &&
                  XItemSelectionModel_isSelected(
                      view->m_selectionModel, row, lv->m_modelColumn);
            cur = (view->m_currentRow == row &&
                   view->m_currentColumn == lv->m_modelColumn);
            if (sel)
                XPainter_fillRect(&painter, &cell, highlight);
            else if (view->m_alternatingRowColors && (row & 1))
                XPainter_fillRect(&painter, &cell, alternateBase);
            if (cur && !sel)
                XPainter_fillRect(&painter, &cell, highlight);
            {
                /* role 叠加存储渲染消费（对标 QStyledItemDelegate::paint
                 * 的 role 消费子集；此前写入端零消费）：布局顺序同 Qt
                 * 风格条目——CheckStateRole 勾选框最左、DecorationRole
                 * 图像其次、剩余矩形承载文本（TextAlignmentRole 对齐/
                 * FontRole 字体）。 */
                int contentX = cell.x;
                int check = XAbstractItemView_itemCheckState(
                    view, row, lv->m_modelColumn);
                if (check >= 0)
                    contentX += xlv_drawCheckIndicator(&painter, &cell,
                                                       check);
                contentX += xlv_drawDecoration(
                    &painter, &cell,
                    XAbstractItemView_itemDecoration(view, row,
                                                     lv->m_modelColumn));
                xlv_drawRowText(
                    &painter, lv, &cell,
                    XAbstractItemModel_data_2(model, row,
                                              lv->m_modelColumn),
                    XAbstractItemView_itemTextAlignment(view, row,
                                                        lv->m_modelColumn),
                    XAbstractItemView_itemFont(view, row,
                                               lv->m_modelColumn),
                    contentX,
                    sel ? highlightedText : windowText);
            }
            if (lv->m_spacing > 0) {
                XPainter_setPen(&painter, 0xFFDDDDDDu);
                XPainter_drawLine(&painter, offX, y + rh,
                                  offX + r.width, y + rh);
            }
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

/* 条目几何虚槽：转发到列表既有 (row) 几何（单列渲染，非渲染列无矩形；
 * 供基类编辑器摆放/scrollTo/尺寸提示虚分派，对标 QListView::visualRect
 * 对 QAbstractItemView::visualRect 的覆写）。 */
static bool VXListView_visualRect(const XAbstractItemView* view, int row,
                                  int col, XRect* out)
{
    XListView* lv = (XListView*)view;
    if (!lv || !out) return false;
    if (col != lv->m_modelColumn) return false;
    *out = XListView_visualRect(lv, row);
    return out->width > 0 && out->height > 0;
}

XVtable* XListView_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XListView)
    XVTABLE_INHERIT_XCLASS(XAbstractItemView);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXListView_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VXListView_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractItemView_IndexAt, VXListView_indexAt);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractItemView_VisualRect,
                             VXListView_visualRect);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractScrollArea_ScrollContentsBy,
                             VXListView_scrollContentsBy);
    return XVTABLE_DEFAULT;
}

/** @brief 滚动内容变化：重绘视口（此前滚动条值变化不触发重绘）。 */
static void VXListView_scrollContentsBy(XAbstractScrollArea* area, int dx,
                                        int dy)
{
    (void)dx;
    (void)dy;
    if (area) XWidget_update((XWidget*)area);
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
    self->m_movement = XListViewMovement_Static;
    self->m_uniformItemSizes = false;
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

void XListView_gridSize(const XListView* self, int* width, int* height)
{
    /* 双输出 getter：未启用维度（<=0，按 -1 存储）与对象无效均输出 -1，
     * 任一输出指针可为 NULL（忽略该维）。 */
    int w = self ? self->m_gridWidth : -1;
    int h = self ? self->m_gridHeight : -1;
    if (width) *width = w;
    if (height) *height = h;
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

void XListView_setMovement(XListView* self, int movement)
{
    if (!self) return;
    if (movement != XListViewMovement_Static
        && movement != XListViewMovement_Free
        && movement != XListViewMovement_Snap)
        return;
    if (self->m_movement == movement) return;
    self->m_movement = movement;
    /* 对标 Qt：movement!=Static 联动 dragEnabled/acceptDrops 并延迟
       重排；XGui 平铺行模型下仅存状态并触发重绘，拖动移动本体未接。 */
    XWidget_update((XWidget*)self);
}

int XListView_movement(const XListView* self)
{ return self ? self->m_movement : XListViewMovement_Static; }

void XListView_setBatchSize(XListView* self, int batchSize)
{
    /* 对标 Qt：非法批量（<=0）拒绝并保持原值。 */
    if (self && batchSize > 0)
        self->m_batchSize = batchSize;
}

int XListView_batchSize(const XListView* self)
{ return self ? self->m_batchSize : 0; }

void XListView_setUniformItemSizes(XListView* self, bool enable)
{
    /* 对标 Qt：纯状态存取，不触发重排。 */
    if (self) self->m_uniformItemSizes = enable;
}

bool XListView_uniformItemSizes(const XListView* self)
{ return self ? self->m_uniformItemSizes : false; }

void XListView_clearPropertyFlags(XListView* self)
{
    /* 对标 QListView::clearPropertyFlags：XGui 项目无属性标志
       （modeProperties）体系，状态属性均为直接存取，无标志可清，
       实现为无操作（对标接口存在性）。 */
    (void)self;
}

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

/* ==================== 几何查询（对标 QListView::visualRect） ==================== */

XRect XListView_visualRect(const XListView* self, int row)
{
    XRect r;
    int rows;
    int i;
    int slotH;
    int slotW;
    int y;
    int offX;
    int offY;
    r.x = 0;
    r.y = 0;
    r.width = 0;
    r.height = 0;
    if (!self || row < 0) return r;
    rows = xlv_modelRows(self);
    if (row >= rows) return r;
    if (xlv_rowIsHidden(self, row)) return r;
    /* 与绘制/命中同一口径：槽位高 = max(行高, 网格高)，
       槽位宽 = 网格宽启用且小于视口宽时取网格宽（其余取视口宽）。 */
    slotH = xlv_slotHeight(self);
    slotW = xlv_slotWidth(self, XWidget_width((const XWidget*)self));
    /* y 累计目标行之前各可见行槽位步进（隐藏行占高 0 跳过）。 */
    y = 0;
    for (i = 0; i < row; ++i) {
        if (xlv_rowIsHidden(self, i)) continue;
        y += slotH + self->m_spacing;
    }
    xlv_scrollOffsets((XListView*)self, &offX, &offY);
    r.x = 0 - offX;
    r.y = y - offY;
    r.width = slotW;
    r.height = slotH;
    return r;
}

/* ==================== 信号（对标 QListView::indexesMoved） ==================== */

void* XListView_indexesMoved_signal(XListView* self, const int* rows,
                                    int count)
{
    XVarList* args;
    if (!rows || count < 0) count = 0;
    args = XVarList_Create(XVar(const int*, rows), XVar(int, count));
    if (!args) return (void*)(size_t)XListView_indexesMoved_signal;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self,
                           (size_t)XListView_indexesMoved_signal,
                           args, NULL, NULL, XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
    return (void*)(size_t)XListView_indexesMoved_signal;
}

#endif /* XWIDGET_ON && XTABLEWIDGET_ON */
