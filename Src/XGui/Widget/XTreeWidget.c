/**
 * @file       XTreeWidget.c
 * @brief      XTreeWidget 树控件实现（树条目 + 递归渲染）。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XTreeWidget.h"

#include "XAlgorithm.h"
#include "XStringUtils.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XPainter.h"
#include "XVarList.h"
#include "XCursor.h"
#include "XWidget_Protected.h"

#if XWIDGET_ON && XTABLEWIDGET_ON

#define XTW_HEADER_H 20
#define XTW_ROW_H 24
#define XTW_INDIC_HIT 10  /**< 顶层展开指示器命中带宽（绘制位于 x∈[2,6]）。 */
/* 滚动条带宽：与 XAbstractScrollArea VX_asa_resizeEvent 的 sbw=16 同源
 * （该常量未导出，本类补布局滚动条几何时按同值对齐）。 */
#define XTW_SBW 16

static void VXTreeWidget_deinit(XTreeWidget* self);
static void VXTreeWidget_paintEvent(XWidget* self, XEvent* event);
static void VXTreeWidget_scrollContentsBy(XAbstractScrollArea* area, int dx,
                                          int dy);

/** @brief 读取垂直滚动偏移（视口原点在内容坐标中的 y）。 */
static int xtw_scrollOffsetY(const XTreeWidget* self)
{
    XScrollBar* vbar = XAbstractScrollArea_verticalScrollBar(
        (XAbstractScrollArea*)&self->m_base.m_base);
    return vbar ? XScrollBar_value(vbar) : 0;
}

/** @brief 表头带占用的视口顶部偏移（headerHidden 时 0；对标 QTreeWidget
 *         默认 headerVisible，表头绘制于视口顶部、行带整体下移）。
 * @param self 目标控件。
 * @return 表头带高度（像素）。
 * @note 绘制/命中/滚动三路共用同一偏移口径，行带几何与鼠标命中不脱节。 */
static int xtw_headerOffset(const XTreeWidget* self)
{
    return (self && !XTreeView_isHeaderHidden(&self->m_base)) ? XTW_HEADER_H
                                                              : 0;
}
static void VXTreeWidget_mousePressEvent(XWidget* self, XEvent* event);
static void VXTreeWidget_mouseMoveEvent(XWidget* self, XEvent* event);
static void VXTreeWidget_mouseReleaseEvent(XWidget* self, XEvent* event);
static void VXTreeWidget_mouseDoubleClickEvent(XWidget* self, XEvent* event);
static void VXTreeWidget_keyPressEvent(XWidget* self, XEvent* event);
static void VXTreeWidget_wheelEvent(XWidget* self, XEvent* event);

static void xtwitem_freeSubtree(XTreeWidgetItem* item);

/* ==================== 表头分隔线拖拽调宽手势（第八轮②） ==================== */

/** @brief 分隔线手柄命中热区半径（任务口径 ±3px；对标 Qt
 *         PM_HeaderGripMargin 样式近似，qheaderview.cpp:3316）。 */
#define XTW_HANDLE_HIT 3
/** @brief 拖拽最小列宽钳位（对标 QHeaderView::minimumSectionSize 缺省
 *         20 = XHEADERVIEW_DEFAULT_MINIMUM_SECTION_SIZE 同值）。 */
#define XTW_MIN_COL_W 20

/* 手势态承载：契约头不扩字段边界下按本文件既有文件级 static 三元组
 * 同款范式（xtw_g_curChild* 先例；多实例并存时同一时刻至多一路拖拽，
 * 串行交互场景无歧义）。字段对标 Qt QHeaderViewPrivate 的
 * state=ResizeSection + d->section + d->firstPos + d->originalSize。
 * 定义置于文件前部：VXTreeWidget_deinit 的析构清防在其之前。 */
static XTreeWidget* xtw_g_resizeOwner = NULL;
static int xtw_g_resizeSection = -1; /**< 拖拽列号；-1=无手势。 */
static int xtw_g_resizePressX = -1;  /**< 按下点 x（d->firstPos）。 */
static int xtw_g_resizeOrigW = 0;    /**< 按下时生效列宽（d->originalSize）。 */
static bool xtw_g_resizeCursor = false; /**< SplitH 光标已挂（WA_SetCursor 差分）。 */

/** @brief 发射单 int 载荷信号（行号族；无监听不分配载荷）。 */
static void xtw_emitRow(XTreeWidget* self, size_t signal, int row)
{
    XVarList* arguments;
    if (!self || row < 0 || !((XObject*)self)->m_signalSlot) return;
    arguments = XVarList_Create(XVar(int, row));
    if (!arguments) return;
    XObject_emitSignal((XObject*)self, signal, arguments, NULL, NULL,
                       XEVENT_PRIORITY_NORMAL);
}

/** @brief 发射 行号+条目 双载荷信号（itemPressed/itemClicked 族）。
 *         行号恒为首参：旧 XVarList_args_1(args, int) 槽读到顶层行号
 *         不变（向后兼容）；条目为借用指针，直连槽内即时消费有效。 */
static void xtw_emitRowItem(XTreeWidget* self, size_t signal, int row,
                            const XTreeWidgetItem* item)
{
    XVarList* arguments;
    if (!self || row < 0 || !((XObject*)self)->m_signalSlot) return;
    arguments = XVarList_Create(XVar(int, row),
                                XVar(const XTreeWidgetItem*, item));
    if (!arguments) return;
    XObject_emitSignal((XObject*)self, signal, arguments, NULL, NULL,
                       XEVENT_PRIORITY_NORMAL);
}

/** @brief 全量同步内建桥模型：行=顶层行、列 0..columnCount-1 =
 *         各列文本。条目级变更走增量（setCheckState/setTextAt 直写
         *），结构性变更（增删行/排序/列数变化）后调用本函数兜底。 */
static void xtw_bridgeSync(XTreeWidget* self)
{
    XAbstractItemModel* model;
    int row;
    int col;
    if (!self || !self->m_bridgeModel) return;
    XAbstractItemModel_setDimension(self->m_bridgeModel,
                                    self->m_topCount,
                                    XTreeWidget_columnCount(self));
    for (row = 0; row < self->m_topCount; ++row) {
        XTreeWidgetItem* item = self->m_topItems[row];
        if (!item) continue;
        for (col = 0; col < XTreeWidget_columnCount(self); ++col) {
            const XString* cell = XTreeWidgetItem_textAt(item, col);
            if (cell)
                XAbstractItemModel_setData(
                    self->m_bridgeModel, row, col, cell);
        }
    }
}

/** @brief 发射双 int 载荷信号（currentItemChanged：current, previous）。 */
static void xtw_emitRow2(XTreeWidget* self, size_t signal, int a, int b)
{
    XVarList* arguments;
    if (!self || !((XObject*)self)->m_signalSlot) return;
    arguments = XVarList_Create(XVar(int, a), XVar(int, b));
    if (!arguments) return;
    XObject_emitSignal((XObject*)self, signal, arguments, NULL, NULL,
                       XEVENT_PRIORITY_NORMAL);
}

/** @brief 发射无载荷信号（itemSelectionChanged）。 */
static void xtw_emitNone(XTreeWidget* self, size_t signal)
{
    if (!self || !((XObject*)self)->m_signalSlot) return;
    XObject_emitSignal((XObject*)self, signal, NULL, NULL, NULL,
                       XEVENT_PRIORITY_NORMAL);
}

/** @brief 条目 → 平铺顶层行号（线性查 m_topItems；子条目/未挂树 -1）。 */
static int xtw_topLevelRowOf(const XTreeWidget* self,
                             const XTreeWidgetItem* item)
{
    int i;
    if (!self || !item) return -1;
    for (i = 0; i < self->m_topCount; ++i) {
        if (self->m_topItems[i] == item) return i;
    }
    return -1;
}

XTreeWidgetItem* XTreeWidgetItem_create(const XString* text,
                                        XTreeWidgetItem* parent)
{
    XTreeWidgetItem* item =
        (XTreeWidgetItem*)XMalloc_System(sizeof(XTreeWidgetItem));
    if (!item) return NULL;
    XMemset(item, 0, sizeof(*item));
    item->text = text ? XString_create_copy(text) : XString_create();
    item->parent = parent;
    return item;
}

XTreeWidgetItem* XTreeWidgetItem_create_2(const char* text,
                                          XTreeWidgetItem* parent)
{
    XString* tmp = NULL;
    XTreeWidgetItem* item;
    if (text) {
        tmp = XString_create_utf8(text);
        if (!tmp) return NULL;
    }
    item = XTreeWidgetItem_create(tmp, parent);
    if (tmp) XString_delete_base((XClass*)tmp);
    return item;
}

void XTreeWidgetItem_delete(XTreeWidgetItem* item)
{
    if (!item) return;
    xtwitem_freeSubtree(item);
    XFree_System(item);
}

static void xtwitem_freeSubtree(XTreeWidgetItem* item)
{
    int i;
    if (!item) return;
    for (i = 0; i < item->childCount; ++i) {
        if (item->children[i]) {
            xtwitem_freeSubtree(item->children[i]);
            XFree_System(item->children[i]);
            item->children[i] = NULL;
        }
    }
    if (item->children) XFree_System(item->children);
    if (item->text) XString_delete_base((XClass*)item->text);
    if (item->extraTexts) {
        int eci;
        for (eci = 0; eci < item->extraTextCapacity; ++eci)
            if (item->extraTexts[eci])
                XString_delete_base((XClass*)item->extraTexts[eci]);
        XFree_System(item->extraTexts);
        item->extraTexts = NULL;
    }
    item->extraTextCapacity = 0;
    item->children = NULL;
    item->childCount = 0;
    item->childCapacity = 0;
    item->text = NULL;
}

const XString* XTreeWidgetItem_text(const XTreeWidgetItem* item)
{ return (item && item->text) ? item->text : NULL; }

const char* XTreeWidgetItem_text_2(const XTreeWidgetItem* item)
{
    const XString* s;
    s = XTreeWidgetItem_text(item);
    return s ? XString_toUtf8(s) : "";
}

void XTreeWidgetItem_setText(XTreeWidgetItem* item, const XString* text)
{
    if (!item) return;
    if (!item->text) item->text = XString_create();
    if (!item->text) return;
    if (text)
        XString_assign(item->text, text);
    else
        XString_assign_utf8(item->text, "");
    /* itemChanged(row) 真实发射点：顶层条目文本变化后按 owner 定位
     * 平铺行号发射；子条目（无子行号）与未挂树条目不发射。 */
    if (item->owner) {
        int row = xtw_topLevelRowOf(item->owner, item);
        if (row >= 0) XTreeWidget_itemChanged_signal(item->owner, row);
    }
}

void XTreeWidgetItem_setText_2(XTreeWidgetItem* item, const char* text)
{
    XString* tmp = NULL;
    if (text) {
        tmp = XString_create_utf8(text);
        if (!tmp) return;
    }
    XTreeWidgetItem_setText(item, tmp);
    if (tmp) XString_delete_base((XClass*)tmp);
}

const XString* XTreeWidgetItem_textAt(const XTreeWidgetItem* item,
                                      int column)
{
    if (!item || column < 0) return NULL;
    if (column == 0) return item->text;
    if (column - 1 >= item->extraTextCapacity) return NULL;
    return item->extraTexts[column - 1];
}

const char* XTreeWidgetItem_textAt_2(const XTreeWidgetItem* item,
                                     int column)
{
    const XString* s = XTreeWidgetItem_textAt(item, column);
    return s ? XString_toUtf8(s) : NULL;
}

/** @brief 列 1+ 文本槽定位（懒分配；失败返回 NULL）。
 *  @note  容量按需倍增（起点 4），分配失败返回 NULL 不部分写入。 */
static XString** xtwitem_ensureExtraSlot(XTreeWidgetItem* item, int column)
{
    int need = column; /* 列 col -> 下标 col-1，需容量 >= column。 */
    int capacity;
    XString** grown;
    if (item->extraTextCapacity >= need) return item->extraTexts;
    capacity = item->extraTextCapacity ? item->extraTextCapacity : 4;
    while (capacity < need) capacity *= 2;
    grown = (XString**)XRealloc_System(
        item->extraTexts,
        (size_t)capacity * sizeof(XString*));
    if (!grown) return NULL;
    XMemset(grown + item->extraTextCapacity, 0,
            (size_t)(capacity - item->extraTextCapacity) *
                sizeof(XString*));
    item->extraTexts = grown;
    item->extraTextCapacity = capacity;
    return item->extraTexts;
}

void XTreeWidgetItem_setTextAt(XTreeWidgetItem* item, int column,
                               const XString* text)
{
    XString** slots;
    if (!item || column < 0) return;
    if (column == 0) {
        XTreeWidgetItem_setText(item, text);
        return;
    }
    slots = xtwitem_ensureExtraSlot(item, column);
    if (!slots) return;
    if (!slots[column - 1]) {
        slots[column - 1] = XString_create();
        if (!slots[column - 1]) return;
    }
    if (text)
        XString_assign(slots[column - 1], text);
    else
        XString_assign_utf8(slots[column - 1], "");
    /* itemChanged(row)：与列 0 同口径（顶层挂载条目按 owner 定位行
     * 号发射；子条目与未挂树条目不发射）。 */
    if (item->owner) {
        XTreeWidget* owner = item->owner;
        int row = xtw_topLevelRowOf(owner, item);
        if (row >= 0) {
            XTreeWidget_itemChanged_signal(owner, row);
            /* 四期②：桥模型同格直写（dataChanged）。 */
            if (owner->m_bridgeModel)
                XAbstractItemModel_setData_2(owner->m_bridgeModel, row,
                                             column, text);
        }
    }
}

void XTreeWidgetItem_setTextAt_2(XTreeWidgetItem* item, int column,
                                 const char* text)
{
    XString* tmp = NULL;
    if (text) {
        tmp = XString_create_utf8(text);
        if (!tmp) return;
    }
    XTreeWidgetItem_setTextAt(item, column, tmp);
    if (tmp) XString_delete_base((XClass*)tmp);
}

int XTreeWidgetItem_checkState(const XTreeWidgetItem* item)
{
    return item ? item->checkState : 0; /* NULL 视为 Unchecked。 */
}

void XTreeWidgetItem_setCheckState(XTreeWidgetItem* item, int state)
{
    int row;
    if (!item) return;
    if (state != XItemCheckState_Unchecked &&
        state != XItemCheckState_PartiallyChecked &&
        state != XItemCheckState_Checked)
        state = XItemCheckState_Unchecked;
    if (item->checkState == state) return;
    item->checkState = state;
    /* itemChanged(row)：与文本 setter 同口径（顶层挂载条目按 owner
     * 定位行号发射；子条目与未挂树条目不发射）。 */
    if (item->owner) {
        row = xtw_topLevelRowOf(item->owner, item);
        if (row >= 0) XTreeWidget_itemChanged_signal(item->owner, row);
    }
}

bool XTreeWidgetItem_addChild(XTreeWidgetItem* item,
                              XTreeWidgetItem* child)
{
    XTreeWidgetItem** grown;
    if (!item || !child) return false;
    if (item->childCount >= item->childCapacity) {
        int cap = item->childCapacity > 0 ? item->childCapacity * 2 : 4;
        grown = (XTreeWidgetItem**)XRealloc_System(
            item->children, sizeof(XTreeWidgetItem*) * (size_t)cap);
        if (!grown) return false;
        item->children = grown;
        item->childCapacity = cap;
    }
    item->children[item->childCount++] = child;
    child->parent = item;
    if (item->owner) {
        XTreeWidget* owner = item->owner;
        if (owner->m_invisibleRoot == item) {
            /* 不可见根挂载：children 扩容后回写控件共享存储，
             * 新顶层条目挂 owner（对标 addTopLevelItem 语义）。 */
            owner->m_topItems = item->children;
            owner->m_topCount = item->childCount;
            owner->m_topCapacity = item->childCapacity;
            child->owner = owner;
            XWidget_update((XWidget*)owner);
        }
    }
    return true;
}

int XTreeWidgetItem_childCount(const XTreeWidgetItem* item)
{ return item ? item->childCount : 0; }

XTreeWidgetItem* XTreeWidgetItem_child(const XTreeWidgetItem* item,
                                       int index)
{
    if (!item || index < 0 || index >= item->childCount) return NULL;
    return item->children[index];
}

/* ==================== XTreeWidget ==================== */

/** @brief 不可见根条目与顶层存储双向同步（widget → 根方向）。
 * @param self 目标控件。
 * @note 根条目 children 借用 m_topItems；顶层存储指针/数量变化的
 *       入口（扩容/插入/移除/清空）末尾调用，保证根视图恒一致。 */
static void xtw_syncRoot(XTreeWidget* self)
{
    if (!self || !self->m_invisibleRoot) return;
    self->m_invisibleRoot->children = self->m_topItems;
    self->m_invisibleRoot->childCount = self->m_topCount;
    self->m_invisibleRoot->childCapacity = self->m_topCapacity;
}

/** @brief 创建表头条目骨架（columns 个空文本子节点）。
 * @param columns 列数（子节点数）。
 * @return 新表头条目；分配失败返回 NULL（子节点已建部分随 head 析构）。 */
static XTreeWidgetItem* xtw_createHeaderItem(int columns)
{
    XTreeWidgetItem* head = XTreeWidgetItem_create(NULL, NULL);
    int i;
    if (!head) return NULL;
    for (i = 0; i < columns; ++i) {
        XTreeWidgetItem* col = XTreeWidgetItem_create(NULL, NULL);
        if (!col || !XTreeWidgetItem_addChild(head, col)) {
            if (col) XTreeWidgetItem_delete(col);
            break;
        }
    }
    return head;
}

/** @brief 写入指定列表头文本（setHeaderItem 回填路径；扩容清零规则
 *         与 setHeaderLabels 一致）。 */
static void xtw_setHeaderLabelAt(XTreeWidget* self, int index,
                                 const char* text)
{
    if (!self || index < 0) return;
    if (index >= self->m_headerCapacity) {
        int cap = self->m_headerCapacity > 0 ? self->m_headerCapacity : 4;
        XString** grown;
        while (cap <= index) cap *= 2;
        grown = (XString**)XRealloc_System(
            self->m_headerLabels, sizeof(XString*) * (size_t)cap);
        if (!grown) return;
        XMemset(grown + self->m_headerCapacity, 0,
                sizeof(XString*) * (size_t)(cap - self->m_headerCapacity));
        self->m_headerLabels = grown;
        self->m_headerCapacity = cap;
    }
    if (!self->m_headerLabels[index])
        self->m_headerLabels[index] = XString_create();
    if (self->m_headerLabels[index])
        XString_assign_utf8(self->m_headerLabels[index],
                            text ? text : "");
    if (index + 1 > self->m_headerCount)
        self->m_headerCount = index + 1;
}

/** @brief 确保顶层条目数组与单元格部件表行容量（两者同倍增策略同步
 *         扩容，保证 [row] 下标对齐；新增区域清零）。 */
static void xtw_ensureTop(XTreeWidget* self, int need)
{
    XTreeWidgetItem** grown;
    XWidget*** cgrown;
    bool* egrown;
    int cap = self->m_topCapacity > 0 ? self->m_topCapacity : 4;
    if (need <= self->m_topCapacity) return;
    while (cap < need) cap *= 2;
    grown = (XTreeWidgetItem**)XRealloc_System(
        self->m_topItems, sizeof(XTreeWidgetItem*) * (size_t)cap);
    if (grown) {
        self->m_topItems = grown;
        self->m_topCapacity = cap;
    }
    /* 部件表行数组与条目数组同容量扩容（行下标一一对应）；失败时保持
     * NULL，后续挂载入口按需重试（列扩容参照 XTableWidget
     * xtw_ensureCols 模式）。 */
    cgrown = (XWidget***)XRealloc_System(
        self->m_cellWidgets, sizeof(XWidget**) * (size_t)cap);
    if (cgrown) {
        XMemset(cgrown + self->m_cellRowCapacity, 0,
                sizeof(XWidget**) * (size_t)(cap - self->m_cellRowCapacity));
        self->m_cellWidgets = cgrown;
        self->m_cellRowCapacity = cap;
    }
    /* 展开状态表同容量扩容（新增区清零=折叠；挂载入口对新行显式置
     * 展开，与历史"子树恒绘制"默认一致）。 */
    egrown = (bool*)XRealloc_System(
        self->m_topExpanded, sizeof(bool) * (size_t)cap);
    if (egrown) {
        XMemset(egrown + self->m_topExpCapacity, 0,
                sizeof(bool) * (size_t)(cap - self->m_topExpCapacity));
        self->m_topExpanded = egrown;
        self->m_topExpCapacity = cap;
    }
    /* 顶层存储指针可能已更换：同步不可见根借用视图。 */
    xtw_syncRoot(self);
}

/** @brief 确保单元格部件表列容量：对每行已分配的列指针表倍增扩容并
 *         清零新增区（对标 XTableWidget xtw_ensureCols；懒分配的行
 *         跳过，挂载时按当时容量补齐）。 */
static void xtw_ensureCellCols(XTreeWidget* self, int cols)
{
    int cap = self->m_cellColCapacity > 0 ? self->m_cellColCapacity : 4;
    int i;
    bool ok = true;
    if (cols <= self->m_cellColCapacity) return;
    while (cap < cols) cap *= 2;
    for (i = 0; i < self->m_topCount; ++i) {
        XWidget** row = self->m_cellWidgets ? self->m_cellWidgets[i] : NULL;
        XWidget** grown;
        if (!row) continue;
        grown = (XWidget**)XRealloc_System(
            row, sizeof(XWidget*) * (size_t)cap);
        if (!grown) { ok = false; continue; }
        XMemset(grown + self->m_cellColCapacity, 0,
                sizeof(XWidget*) * (size_t)(cap - self->m_cellColCapacity));
        self->m_cellWidgets[i] = grown;
    }
    /* 保守推进：任一行扩容失败则不更新全局列容量（已扩容的行表只会
     * 偏大，安全；下次调用对失败行重试），避免按旧小容量越界写入。 */
    if (!ok) return;
    self->m_cellColCapacity = cap;
}

/** @brief 取指定行已分配的部件列指针表（懒分配；失败返回 NULL）。 */
static XWidget** xtw_cellRowEnsure(XTreeWidget* self, int row)
{
    XWidget** table;
    if (!self->m_cellWidgets || row >= self->m_cellRowCapacity) return NULL;
    table = self->m_cellWidgets[row];
    if (!table) {
        table = (XWidget**)XMalloc_System(
            sizeof(XWidget*) * (size_t)(self->m_cellColCapacity > 0
                                            ? self->m_cellColCapacity
                                            : 1));
        if (!table) return NULL;
        XMemset(table, 0,
                sizeof(XWidget*) * (size_t)(self->m_cellColCapacity > 0
                                                ? self->m_cellColCapacity
                                                : 1));
        self->m_cellWidgets[row] = table;
    }
    return table;
}

/** @brief 滚动使指定平铺行可见（EnsureVisible 语义；几何取
 *         XTreeWidget_visualItemRect，滚动范围由滚动条自身收敛）。 */
static void xtw_scrollRowVisible(XTreeWidget* self, int row)
{
    XAbstractScrollArea* area = (XAbstractScrollArea*)&self->m_base.m_base;
    XScrollBar* vbar = XAbstractScrollArea_verticalScrollBar(area);
    XWidget* viewport = XAbstractScrollArea_viewport(area);
    XRect r;
    int visibleH;
    int value;
    int target;
    if (!vbar) return;
    visibleH = viewport ? XWidget_height(viewport) : 0;
    if (visibleH <= 0) return;
    r = XTreeWidget_visualItemRect(self, row);
    value = XScrollBar_value(vbar);
    target = value;
    /* 行带在表头带之下（screen y = headerOffset + 行带 y − 滚动值）：
     * 可见窗口为 [headerOffset, visibleH]，两边界条件随之收口。 */
    if (r.y + xtw_headerOffset(self) < value)
        target = r.y;
    else if (r.y + r.height > value + visibleH - xtw_headerOffset(self))
        target = r.y + r.height + xtw_headerOffset(self) - visibleH;
    if (target != value) XScrollBar_setValue(vbar, target);
}

/** @brief 查询行高有效值（未配置回退 24；同绘制/命中口径）。 */
static int xtw_effectiveRowHeight(const XTreeWidget* self)
{
    return (self && self->m_base.m_rowHeight > 0) ? self->m_base.m_rowHeight
                                                  : XTW_ROW_H;
}

/** @brief 统计条目子树的可见行数（含自身；条目树未接展开态，整树
 *         随顶层行展开/折叠整体显隐）。 */
static int xtw_subtreeRows(const XTreeWidgetItem* item)
{
    int i;
    int total = 1;
    if (!item) return 1;
    for (i = 0; i < item->childCount; ++i) {
        if (item->children[i])
            total += xtw_subtreeRows(item->children[i]);
    }
    return total;
}

/** @brief 查询顶层行展开状态（未同步/越界/表缺失回退 true，保持
 *         历史"子树恒绘制"默认）。 */
static bool xtw_isExpanded(const XTreeWidget* self, int row)
{
    if (!self || !self->m_topExpanded || row < 0) return true;
    if (row >= self->m_topExpCapacity) return true;
    return self->m_topExpanded[row];
}

/** @brief 顶层行 row 的视口 y（之前各行按展开态子树行数累计；与
 *         绘制/命中同一几何）。 */
static int xtw_topRowY(const XTreeWidget* self, int row)
{
    int rh;
    int i;
    int y = 0;
    if (!self) return 0;
    rh = xtw_effectiveRowHeight(self);
    for (i = 0; i < row && i < self->m_topCount; ++i) {
        const XTreeWidgetItem* item = self->m_topItems[i];
        if (!item) continue;
        y += rh * (xtw_isExpanded(self, i) ? xtw_subtreeRows(item) : 1);
    }
    return y;
}

/** @brief 视口 y → 平铺顶层行号（返回命中行首 y；未命中 -1）。 */
static int xtw_rowAtY(const XTreeWidget* self, int y, int* outRowY)
{
    int rh;
    int i;
    int top = 0;
    if (outRowY) *outRowY = -1;
    if (!self) return -1;
    /* 视口坐标 → 内容坐标：先扣除表头带（行带在表头之下，与绘制
     * 同一口径），再加滚动偏移；表头带内点击不命中任何行。 */
    y += xtw_scrollOffsetY(self) - xtw_headerOffset(self);
    if (y < 0) return -1;
    rh = xtw_effectiveRowHeight(self);
    for (i = 0; i < self->m_topCount; ++i) {
        const XTreeWidgetItem* item = self->m_topItems[i];
        int h;
        if (!item) continue;
        h = rh * (xtw_isExpanded(self, i) ? xtw_subtreeRows(item) : 1);
        if (y >= top && y < top + h) {
            if (outRowY) *outRowY = top;
            return i;
        }
        top += h;
    }
    return -1;
}

/** @brief 条目子树前序第 k 个条目（k 经指针跨递归共享消耗；越界
 *         返回 NULL）。
 *  @note  顺序 = 绘制顺序（自身 → 子条目递归），与 findItems 返回
 *         的全树前序序号同一约定；子条目点击据此定位真实命中条目。 */
static XTreeWidgetItem* xtw_preOrderItemAt(XTreeWidgetItem* item, int* k)
{
    int i;
    if (!item) return NULL;
    if (*k == 0) return item;
    --*k;
    for (i = 0; i < item->childCount; ++i) {
        XTreeWidgetItem* hit = xtw_preOrderItemAt(item->children[i], k);
        if (hit) return hit;
    }
    return NULL;
}

/** @brief 视口 y → 顶层行带内的命中条目本体（itemAt 的子条目级
 *         精化）：行带按行高细分为前序条目，与 xtw_rowAtY/绘制
 *         同一展开态几何；顶层行未展开或越界回退顶层条目本体。
 * @param outK 可选输出：命中条目在顶层行带内的前序序号（顶层本体
 *         0、子条目 ≥1、未命中 -1）；与 xtw_drawItem 的行偏移序号
 *         k 同一口径，子条目命中态（第二轮 #14）据此承载。 */
static XTreeWidgetItem* xtw_hitItemAt(XTreeWidget* self, int topRow, int y,
                                      int* outK)
{
    XTreeWidgetItem* top;
    XTreeWidgetItem* hit;
    int rowY = -1;
    int off;
    int hitK;
    int probe;
    if (outK) *outK = -1;
    if (!self || topRow < 0 || topRow >= self->m_topCount) return NULL;
    top = self->m_topItems[topRow];
    if (!top) return NULL;
    if (outK) *outK = 0; /* 顶层本体命中（未展开/无子条目/带外回退）。 */
    if (!xtw_isExpanded(self, topRow) || top->childCount <= 0) return top;
    xtw_rowAtY(self, y, &rowY);
    if (rowY < 0) return top;
    off = y + xtw_scrollOffsetY(self) - xtw_headerOffset(self) - rowY;
    if (off < 0) off = 0;
    hitK = off / xtw_effectiveRowHeight(self);
    /* xtw_preOrderItemAt 会消耗序号入参：探针副本递归，原值留给
     * outK 输出（命中子条目时输出其前序序号）。 */
    probe = hitK;
    hit = xtw_preOrderItemAt(top, &probe);
    if (hit && outK) *outK = hitK;
    return hit ? hit : top;
}

/* ==================== 子条目粒度当前命中态（第二轮 #14） ==================== */

/** @brief 子条目命中态承载（保守方案）：选择模型粒度保持顶层行
 *         （itemClicked 首参 row / selectedItems 等 apitest 契约不
 *         变），当前命中的子条目以文件级 static 三元组
 *         {owner, 顶层行号, 行带内前序序号} 承载——契约头
 *         （XTreeWidget.h）本批不可扩字段的边界下按任务书保守口径
 *         落地。不存条目指针：行号/序号越界即自动失效，无悬垂风险；
 *         多实例并存时仅最后点击的树呈现子条目命中行（文档化取舍，
 *         demo/autotest 场景均为单树串行交互）。 */
static XTreeWidget* xtw_g_curChildOwner = NULL;
static int xtw_g_curChildRow = -1;
static int xtw_g_curChildK = -1;

/** @brief 写入子条目命中态（row<0 / k<=0 = 清除，顶层行交给选择
 *         模型呈现——对标 Qt 当前索引唯一，点击索引即当前索引）。 */
static void xtw_setCurChild(XTreeWidget* self, int row, int k)
{
    xtw_g_curChildOwner = self;
    xtw_g_curChildRow = row;
    xtw_g_curChildK = k;
}

/** @brief (顶层行, 前序序号) 是否为当前命中的子条目（k>0 排除顶层
 *         本体；行号越界自动失效）。 */
static bool xtw_curChildHit(const XTreeWidget* self, int topRow, int k)
{
    if (!self || xtw_g_curChildOwner != self) return false;
    return xtw_g_curChildRow == topRow && xtw_g_curChildK == k && k > 0 &&
           topRow >= 0 && topRow < self->m_topCount;
}

/** @brief 顶层行带内是否存在子条目命中态（绘制路径抑制该顶层行本体
 *         Highlight，保持一行一选中）。 */
static bool xtw_bandHasChildCur(const XTreeWidget* self, int topRow)
{
    if (!self || xtw_g_curChildOwner != self) return false;
    if (xtw_g_curChildRow != topRow || xtw_g_curChildK <= 0) return false;
    if (topRow < 0 || topRow >= self->m_topCount) return false;
    return xtw_g_curChildK <
           xtw_subtreeRows(self->m_topItems[topRow]);
}

/** @brief 统一当前行写入：当前项变化发射 currentItemChanged，选择
 *         集合变化发射 itemSelectionChanged（SelectCurrent 语义）。 */
static void xtw_setCurrentRow(XTreeWidget* self, int row)
{
    XItemSelectionModel* selection;
    int previous;
    bool wasSelected = false;
    bool selectionChanged = false;
    if (!self || row < 0 || row >= self->m_topCount) return;
    selection = self->m_base.m_base.m_selectionModel;
    previous = self->m_base.m_base.m_currentRow;
    /* 当前顶层行变更（键盘/编程导航，区别于子条目点击的 row==previous
     * 路径）：子条目命中态失效——对标 Qt 当前索引唯一，移动后旧子
     * 条目不再呈现选中（qtreeview.cpp drawRow 只高亮 isSelected 的
     * 当前索引行）。 */
    if (row != previous && xtw_g_curChildOwner == self)
        xtw_setCurChild(self, -1, -1);
    if (selection)
        wasSelected = XItemSelectionModel_isSelected(selection, row, 0);
    /* setCurrentIndex 内部已按 SelectCurrent 先行写入选择模型，
     * 故以写入前后 isSelected 差分判定选择集合是否变化。 */
    XAbstractItemView_setCurrentIndex(&self->m_base.m_base, row, 0);
    if (selection && row != previous &&
        self->m_base.m_base.m_selectionMode !=
            XAbstractItemViewSelectionMode_NoSelection)
        selectionChanged = !wasSelected;
    if (row != previous)
        XTreeWidget_currentItemChanged_signal(self, row, previous);
    if (selectionChanged) XTreeWidget_itemSelectionChanged_signal(self);
    XWidget_update((XWidget*)self);
}

/** @brief 置顶层行展开状态并按变化方向发射 itemExpanded/itemCollapsed
 *         （有子条目才有切换语义；无状态表/状态不变即无操作）。
 *         expandItem/collapseItem 与指示器切换共用此承载。 */
static void xtw_setRowExpanded(XTreeWidget* self, int row, bool expand)
{
    XTreeWidgetItem* item;
    if (!self || row < 0 || row >= self->m_topCount) return;
    if (!self->m_topExpanded || row >= self->m_topExpCapacity) return;
    item = self->m_topItems[row];
    if (!item || item->childCount <= 0) return;
    if (xtw_isExpanded(self, row) == expand) return;
    self->m_topExpanded[row] = expand;
    XWidget_update((XWidget*)self);
    if (expand)
        XTreeWidget_itemExpanded_signal(self, row);
    else
        XTreeWidget_itemCollapsed_signal(self, row);
}

/** @brief 切换顶层行展开状态（方向取反；有子条目才有切换语义）。 */
static void xtw_toggleExpanded(XTreeWidget* self, int row)
{
    if (!self || row < 0 || row >= self->m_topCount) return;
    xtw_setRowExpanded(self, row, !xtw_isExpanded(self, row));
}

XVtable* XTreeWidget_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XTreeWidget)
    XVTABLE_INHERIT_XCLASS(XTreeView);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXTreeWidget_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VXTreeWidget_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MousePressEvent,
                             VXTreeWidget_mousePressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseMoveEvent,
                             VXTreeWidget_mouseMoveEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseReleaseEvent,
                             VXTreeWidget_mouseReleaseEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseDoubleClickEvent,
                             VXTreeWidget_mouseDoubleClickEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_KeyPressEvent,
                             VXTreeWidget_keyPressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_WheelEvent, VXTreeWidget_wheelEvent);
    return XVTABLE_DEFAULT;
}

void XTreeWidget_init(XTreeWidget* self, XWidget* parent,
                      XWidgetFlags flags)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XTreeView_init(&self->m_base, parent, flags);
    /* 四期②内建模型桥（对标 XTableWidget 范式）：便利类的顶层条目
       文本同步进模型（行=顶层行、列=列号），基类 setModel/indexAt/
       selectionModel 等 API 由此获得一致的数据视图；绘制/命中仍走
       自持展开态几何（xtw_drawItem/xtw_rowAtY），不受模型影响。 */
    self->m_bridgeModel = XAbstractItemModel_create();
    if (self->m_bridgeModel)
        XAbstractItemView_setModel(&self->m_base.m_base,
                                   self->m_bridgeModel);
    self->m_sortColumn = -1;
    self->m_sortOrder = 0;
    self->m_enteredRow = -2; /* itemEntered 差分基准（同 XListWidget）。 */
    self->m_columnCount = 1; /* 默认单列（同 Qt 新建 QTreeWidget）。 */
    /* 不可见根条目（Qt 语义：顶层条目的逻辑父节点）；children 借用
     * 顶层存储，owner 挂本控件供 addChild 钩子回写。 */
    self->m_invisibleRoot = XTreeWidgetItem_create(NULL, NULL);
    if (self->m_invisibleRoot)
        self->m_invisibleRoot->owner = self;
    /* 表头条目：单列空文本骨架（文本由 setHeaderLabels 镜像）。 */
    self->m_headerItem = xtw_createHeaderItem(self->m_columnCount);
    XClassSetVtable(self, XTreeWidget);
}

XTreeWidget* XTreeWidget_create_ex(XMemoryType memory, XWidget* parent,
                                   XWidgetFlags flags)
{
    XTreeWidget* self =
        (XTreeWidget*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XTreeWidget_init(self, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

static void VXTreeWidget_deinit(XTreeWidget* self)
{
    int i;
    if (!self) return;
    /* 子条目命中态析构清防（承载见 xtw_setCurChild）：static 三元组
     * 持本控件指针，析构后若同地址新树复用会继承陈旧命中行——析构
     * 即清，杜绝跨实例残留。 */
    if (xtw_g_curChildOwner == self) xtw_setCurChild(NULL, -1, -1);
    /* 拖拽调宽手势态同款析构清防（静态持本控件指针；拖拽中销毁树
     * 的悬垂路径）。 */
    if (xtw_g_resizeOwner == self) {
        xtw_g_resizeOwner = NULL;
        xtw_g_resizeSection = -1;
        xtw_g_resizePressX = -1;
        xtw_g_resizeOrigW = 0;
        xtw_g_resizeCursor = false;
    }
    for (i = 0; i < self->m_topCount; ++i) {
        if (self->m_topItems && self->m_topItems[i])
            XTreeWidgetItem_delete(self->m_topItems[i]);
        /* 部件为借用：只释放承载表，不删除部件本体。 */
        if (self->m_cellWidgets && self->m_cellWidgets[i])
            XFree_System(self->m_cellWidgets[i]);
    }
    if (self->m_topItems) XFree_System(self->m_topItems);
    self->m_topItems = NULL;
    self->m_topCount = 0;
    self->m_topCapacity = 0;
    if (self->m_invisibleRoot) {
        /* children 借用顶层存储：先解除借用（顶层条目已由上方
         * m_topItems 路径统一析构），再删除哨兵本体。 */
        self->m_invisibleRoot->children = NULL;
        self->m_invisibleRoot->childCount = 0;
        self->m_invisibleRoot->childCapacity = 0;
        self->m_invisibleRoot->owner = NULL;
        XTreeWidgetItem_delete(self->m_invisibleRoot);
        self->m_invisibleRoot = NULL;
    }
    if (self->m_headerItem) {
        XTreeWidgetItem_delete(self->m_headerItem);
        self->m_headerItem = NULL;
    }
    if (self->m_topExpanded) XFree_System(self->m_topExpanded);
    self->m_topExpanded = NULL;
    self->m_topExpCapacity = 0;
    if (self->m_cellWidgets) XFree_System(self->m_cellWidgets);
    self->m_cellWidgets = NULL;
    self->m_cellRowCapacity = 0;
    self->m_cellColCapacity = 0;
    /* 表头文本表：逐条释放 XString 后释放指针表。 */
    if (self->m_headerLabels) {
        for (i = 0; i < self->m_headerCapacity; ++i) {
            if (self->m_headerLabels[i])
                XString_delete_base((XClass*)(self->m_headerLabels[i]));
        }
        XFree_System(self->m_headerLabels);
    }
    self->m_headerLabels = NULL;
    self->m_headerCount = 0;
    self->m_headerCapacity = 0;
    XClass_Deinit_Parent(XTreeView, (XTreeView*)self);
}

bool XTreeWidget_addTopLevelItem(XTreeWidget* self, XTreeWidgetItem* item)
{
    if (!self || !item) return false;
    xtw_ensureTop(self, self->m_topCount + 1);
    if (self->m_cellWidgets) self->m_cellWidgets[self->m_topCount] = NULL;
    if (self->m_topExpanded && self->m_topCount < self->m_topExpCapacity)
        self->m_topExpanded[self->m_topCount] = true; /* 新行默认展开。 */
    self->m_topItems[self->m_topCount++] = item;
    item->parent = NULL;
    item->owner = self; /* itemChanged(row) 发射定位借用。 */
    xtw_syncRoot(self); /* 顶层存储数量变化：同步不可见根。 */
    xtw_bridgeSync(self); /* 四期②：模型桥行/列同步。 */
    XWidget_update((XWidget*)self);
    return true;
}

bool XTreeWidget_insertTopLevelItem(XTreeWidget* self, int index,
                                    XTreeWidgetItem* item)
{
    int i;
    if (!self || !item || index < 0 || index > self->m_topCount)
        return false;
    xtw_ensureTop(self, self->m_topCount + 1);
    for (i = self->m_topCount; i > index; --i) {
        self->m_topItems[i] = self->m_topItems[i - 1];
        /* 部件表行随条目同步后移（NULL 行表同样平移）。 */
        if (self->m_cellWidgets)
            self->m_cellWidgets[i] = self->m_cellWidgets[i - 1];
        /* 展开状态随条目同步后移（平行数组下标对齐）。 */
        if (self->m_topExpanded && self->m_topExpCapacity > i)
            self->m_topExpanded[i] = self->m_topExpanded[i - 1];
    }
    self->m_topItems[index] = item;
    if (self->m_cellWidgets) self->m_cellWidgets[index] = NULL;
    if (self->m_topExpanded && self->m_topExpCapacity > index)
        self->m_topExpanded[index] = true; /* 新行默认展开。 */
    item->parent = NULL;
    item->owner = self;
    self->m_topCount++;
    xtw_syncRoot(self); /* 顶层存储数量变化：同步不可见根。 */
    xtw_bridgeSync(self); /* 四期②：模型桥行/列同步。 */
    XWidget_update((XWidget*)self);
    return true;
}

XTreeWidgetItem* XTreeWidget_topLevelItem(const XTreeWidget* self, int index)
{
    if (!self || index < 0 || index >= self->m_topCount) return NULL;
    return self->m_topItems[index];
}

int XTreeWidget_topLevelItemCount(const XTreeWidget* self)
{ return self ? self->m_topCount : 0; }

XTreeWidgetItem* XTreeWidget_invisibleRootItem(const XTreeWidget* self)
{ return self ? self->m_invisibleRoot : NULL; }

XTreeWidgetItem* XTreeWidget_itemFromIndex(const XTreeWidget* self, int row)
{
    /* 索引=indexFromItem 约定的顶层行号：反查即 topLevelItem。 */
    return XTreeWidget_topLevelItem(self, row);
}

XTreeWidgetItem* XTreeWidget_takeTopLevelItem(XTreeWidget* self, int index)
{
    XTreeWidgetItem* item;
    int i;
    if (!self || index < 0 || index >= self->m_topCount) return NULL;
    item = self->m_topItems[index];
    for (i = index; i < self->m_topCount - 1; ++i) {
        self->m_topItems[i] = self->m_topItems[i + 1];
        /* 部件表行随条目同步前移；腾出的尾行清空（部件为借用，
         * 归还调用方，不删除）。 */
        if (self->m_cellWidgets)
            self->m_cellWidgets[i] = self->m_cellWidgets[i + 1];
        /* 展开状态随条目同步前移（平行数组下标对齐）。 */
        if (self->m_topExpanded && self->m_topExpCapacity > i + 1)
            self->m_topExpanded[i] = self->m_topExpanded[i + 1];
    }
    if (self->m_cellWidgets)
        self->m_cellWidgets[self->m_topCount - 1] = NULL;
    self->m_topCount--;
    item->parent = NULL;
    item->owner = NULL; /* 所有权归还调用方，退出 itemChanged 发射定位。 */
    xtw_syncRoot(self); /* 顶层存储数量变化：同步不可见根。 */
    xtw_bridgeSync(self); /* 四期②：模型桥行同步。 */
    XWidget_update((XWidget*)self);
    return item;
}

void XTreeWidget_clear(XTreeWidget* self)
{
    int i;
    int previous;
    bool selectionChanged = false;
    if (!self) return;
    for (i = 0; i < self->m_topCount; ++i) {
        if (self->m_topItems[i]) XTreeWidgetItem_delete(self->m_topItems[i]);
        /* 部件为借用：只释放行承载表（行数组容量保留复用）。 */
        if (self->m_cellWidgets && self->m_cellWidgets[i]) {
            XFree_System(self->m_cellWidgets[i]);
            self->m_cellWidgets[i] = NULL;
        }
    }
    self->m_topCount = 0;
    xtw_syncRoot(self); /* 顶层存储数量变化：同步不可见根。 */
    /* 条目全清：子条目命中态一并失效（行号/序号承载无悬垂，但重填
     * 后同位行会误继承陈旧高亮——clear 即清，同 deinit 防护口径）。 */
    if (xtw_g_curChildOwner == self) xtw_setCurChild(self, -1, -1);
    /* 当前项失效 + 选择清空的真实发射点（同 XListWidget clear 口径）。 */
    previous = XAbstractItemView_currentRow(&self->m_base.m_base);
    XAbstractItemView_setCurrentIndex(&self->m_base.m_base, -1, -1);
    if (self->m_base.m_base.m_selectionModel)
        selectionChanged = XItemSelectionModel_clear(
            self->m_base.m_base.m_selectionModel);
    if (previous != -1)
        XTreeWidget_currentItemChanged_signal(self, -1, previous);
    if (selectionChanged) XTreeWidget_itemSelectionChanged_signal(self);
    /* 模型桥清空放信号链之后：setDimension 发射 rowsRemoved 会先重
       置基类选择/当前状态，抢在信号发射前导致 current/selection 信
       号丢失（实测 clear 信号断言失败）。 */
    xtw_bridgeSync(self);
    XWidget_update((XWidget*)self);
}

/* ==================== 便捷族第一波（平铺行模型） ==================== */

int XTreeWidget_currentItem(const XTreeWidget* self)
{
    int row;
    if (!self) return -1;
    row = XAbstractItemView_currentRow(&self->m_base.m_base);
    if (row < 0 || row >= self->m_topCount) return -1;
    return row;
}

void XTreeWidget_setCurrentItem(XTreeWidget* self, int row)
{
    if (!self || row < 0 || row >= self->m_topCount) return;
    /* 统一当前行路径：currentItemChanged/itemSelectionChanged 发射。 */
    xtw_setCurrentRow(self, row);
    xtw_scrollRowVisible(self, row);
}

XWidget* XTreeWidget_itemWidget(const XTreeWidget* self, int row, int column)
{
    if (!self || row < 0 || row >= self->m_topCount || column < 0 ||
        column >= self->m_cellColCapacity || !self->m_cellWidgets)
        return NULL;
    return self->m_cellWidgets[row] ? self->m_cellWidgets[row][column]
                                    : NULL;
}

void XTreeWidget_setItemWidget(XTreeWidget* self, int row, int column,
                               XWidget* widget)
{
    XWidget** table;
    if (!self || row < 0 || row >= self->m_topCount || column < 0) return;
    if (column >= self->m_cellColCapacity)
        xtw_ensureCellCols(self, column + 1);
    table = xtw_cellRowEnsure(self, row);
    if (!table || column >= self->m_cellColCapacity) return;
    table[column] = widget; /* 借用：树不取得所有权，不负责删除。 */
    XWidget_update((XWidget*)self);
}

void XTreeWidget_removeItemWidget(XTreeWidget* self, int row, int column)
{
    if (!self || row < 0 || row >= self->m_topCount || column < 0 ||
        column >= self->m_cellColCapacity || !self->m_cellWidgets ||
        !self->m_cellWidgets[row])
        return;
    self->m_cellWidgets[row][column] = NULL; /* 置 NULL；不删除部件。 */
    XWidget_update((XWidget*)self);
}

int XTreeWidget_addTopLevelItems(XTreeWidget* self,
                                 const char* const* texts, int count)
{
    int added = 0;
    int i;
    if (!self || !texts || count <= 0) return 0;
    for (i = 0; i < count; ++i) {
        XTreeWidgetItem* item = XTreeWidgetItem_create_2(texts[i], NULL);
        if (!item) break;
        if (!XTreeWidget_addTopLevelItem(self, item)) {
            XTreeWidgetItem_delete(item);
            break;
        }
        ++added;
    }
    return added;
}

int XTreeWidget_insertTopLevelItems(XTreeWidget* self, int index,
                                    const char* const* texts, int count)
{
    int added = 0;
    int i;
    if (!self || !texts || count <= 0) return 0;
    if (index < 0 || index > self->m_topCount) return 0;
    for (i = 0; i < count; ++i) {
        XTreeWidgetItem* item = XTreeWidgetItem_create_2(texts[i], NULL);
        if (!item) break;
        if (!XTreeWidget_insertTopLevelItem(self, index + added, item)) {
            XTreeWidgetItem_delete(item);
            break;
        }
        ++added;
    }
    return added;
}

XRect XTreeWidget_visualItemRect(const XTreeWidget* self, int row)
{
    XRect r;
    XRect_init(&r, 0, 0, 0, 0);
    if (!self || row < 0 || row >= self->m_topCount) return r;
    /* 顶层行深度 0：x=0、宽=控件宽；缩进（indentation）为子行几何
     * 预留。y 按之前各行展开态子树行数累计（与自绘/命中同一几何，
     * 表头不占行带）。 */
    r.x = 0;
    r.y = xtw_topRowY(self, row);
    r.width = XWidget_width((XWidget*)self);
    r.height = xtw_effectiveRowHeight(self);
    return r;
}

int XTreeWidget_sortColumn(const XTreeWidget* self)
{ return self ? self->m_sortColumn : -1; }

/* ==================== 数据便捷族第二波（平铺行模型） ==================== */

int XTreeWidget_columnCount(const XTreeWidget* self)
{
    /* 列数状态承载（setColumnCount 写入；默认 1）。 */
    return (self && self->m_columnCount > 0) ? self->m_columnCount : 1;
}

void XTreeWidget_editItem(XTreeWidget* self, int row, int column)
{
    /* 句柄预留：无编辑器/委托机制，编辑触发无从落地（见 @note）；
     * 仅做入参校验。编辑路径建立后应在此发射 itemChanged(row)。 */
    if (!self || row < 0 || row >= self->m_topCount) return;
    if (column < 0 || column >= XTreeWidget_columnCount(self)) return;
}

int XTreeWidget_indexFromItem(const XTreeWidget* self,
                              const XTreeWidgetItem* item)
{
    /* 平铺行模型：行号即索引（恒等简化）。 */
    return xtw_topLevelRowOf(self, item);
}

void XTreeWidget_scrollToItem(XTreeWidget* self, int row)
{
    if (!self || row < 0 || row >= self->m_topCount) return;
    /* 与 setCurrentItem 同一滚动机制（EnsureVisible 语义）。 */
    xtw_scrollRowVisible(self, row);
}

int XTreeWidget_selectedItems(const XTreeWidget* self, int* outRows,
                              int maxCount)
{
    const XItemSelectionModel* selection;
    int i;
    int found = 0;
    if (!self || !outRows || maxCount <= 0) return 0;
    selection = self->m_base.m_base.m_selectionModel;
    for (i = 0; i < self->m_topCount && found < maxCount; ++i) {
        if (XItemSelectionModel_isSelected(selection, i, 0))
            outRows[found++] = i;
    }
    return found;
}

/* ==================== 便捷族二（查找/排序/命中/表头） ==================== */

/** @brief findItems 前序遍历上下文（命中累计 + 输出窗口一次扫全树）。 */
typedef struct XTwFindContext
{
    const char* text;         /**< 匹配文本（借用）。 */
    int flags;                /**< 匹配方式：1=精确相等，0=包含子串。 */
    int* outRows;             /**< 输出数组（借用；可为 NULL 只统计）。 */
    int maxCount;             /**< 输出容量（<=0 只统计）。 */
    int hits;                 /**< 命中总数（不受输出容量截断）。 */
    int written;              /**< 已写出行数。 */
    int index;                /**< 当前全树前序序号（访问即自增）。 */
} XTwFindContext;

/** @brief 条目子树前序遍历文本匹配（本条目 → 子条目递归；对标 Qt
 *         findItems 经 QAbstractItemModel::match 的全树语义）。 */
static void xtw_findMatches(const XTreeWidgetItem* item, XTwFindContext* ctx)
{
    int i;
    const XString* cell;
    bool hit;
    if (!item) return;
    cell = XTreeWidgetItem_text(item);
    if (cell) {
        /* flags 位 1：精确相等；否则包含子串（均区分大小写，
         * 同 XListWidget_findItems 口径）。 */
        if (ctx->flags & 1)
            hit = XString_equals_utf8(cell, ctx->text, XChar_CaseSensitive);
        else
            hit = XString_contains_utf8(cell, ctx->text, XChar_CaseSensitive);
        if (hit) {
            /* 输出超上限截断，但命中总数继续累计（返回值可 >
             * maxCount，调用方据此判断截断）。 */
            if (ctx->outRows && ctx->written < ctx->maxCount)
                ctx->outRows[ctx->written++] = ctx->index;
            ++ctx->hits;
        }
    }
    ++ctx->index;
    for (i = 0; i < item->childCount; ++i)
        xtw_findMatches(item->children[i], ctx);
}

int XTreeWidget_findItems(const XTreeWidget* self, const char* text,
                          int flags, int* outRows, int maxCount)
{
    XTwFindContext ctx;
    int i;
    if (!self || !text) return 0;
    ctx.text = text;
    ctx.flags = flags;
    ctx.outRows = outRows;
    ctx.maxCount = maxCount > 0 ? maxCount : 0;
    ctx.hits = 0;
    ctx.written = 0;
    ctx.index = 0;
    for (i = 0; i < self->m_topCount; ++i)
        xtw_findMatches(self->m_topItems[i], &ctx);
    return ctx.hits;
}

void XTreeWidget_sortItems(XTreeWidget* self, int column, int order)
{
    int i;
    int j;
    if (!self) return;
    if (column < 0 || column >= XTreeWidget_columnCount(self)) return;
    if (order != 0 && order != 1) return;
    /* 参照 XTableWidget_sortItems 冒泡：排序键 = 条目 UTF-8 文本
     * （空文本视为 ""）；稳定比较、相等不换位。 */
    for (i = 0; i < self->m_topCount; ++i) {
        for (j = 0; j < self->m_topCount - 1 - i; ++j) {
            const char* a = XTreeWidgetItem_textAt_2(self->m_topItems[j],
                                                     column);
            const char* b = XTreeWidgetItem_textAt_2(self->m_topItems[j + 1],
                                                     column);
            /* 未写入列 textAt_2 返回 NULL：按既有契约「NULL 视为空串
               最小」参与比较（实测悬垂 NULL 会直进 XStrcmp）。 */
            if (!a) a = "";
            if (!b) b = "";
            bool swap = (order == 0) ? (XStrcmp(a, b) > 0)
                                     : (XStrcmp(a, b) < 0);
            if (swap) {
                /* 整行数据随动：条目指针（连同其子树）、部件行表、
                 * 展开状态三组平行数组同步换位，行下标对齐不破坏。 */
                XTreeWidgetItem* item = self->m_topItems[j];
                self->m_topItems[j] = self->m_topItems[j + 1];
                self->m_topItems[j + 1] = item;
                if (self->m_cellWidgets) {
                    XWidget** cells = self->m_cellWidgets[j];
                    self->m_cellWidgets[j] = self->m_cellWidgets[j + 1];
                    self->m_cellWidgets[j + 1] = cells;
                }
                if (self->m_topExpanded && self->m_topExpCapacity > j + 1) {
                    bool expanded = self->m_topExpanded[j];
                    self->m_topExpanded[j] = self->m_topExpanded[j + 1];
                    self->m_topExpanded[j + 1] = expanded;
                }
                xtw_bridgeSync(self); /* 四期②：排序后模型桥行序同步。 */
            }
        }
    }
    /* 排序结果承载（sortColumn 读取）；当前项/选择按行号承载不随动。 */
    self->m_sortColumn = column;
    self->m_sortOrder = order;
    XWidget_update((XWidget*)self);
}

int XTreeWidget_itemAt(const XTreeWidget* self, int x, int y)
{
    if (!self || x < 0 || x >= XWidget_width((XWidget*)self) || y < 0)
        return -1;
    /* 与自绘/鼠标命中同一几何：xtw_rowAtY 按各行展开态子树行带累计
     * 反查（折叠行子树不占位）；y 超出行带总高返回 -1。基类
     * XTreeView 的 indexAt 按内部 m_model 固定网格反推，与本控件
     * 条目树几何不一致，故不走基类虚槽分派（见 @note）。 */
    return xtw_rowAtY(self, y, NULL);
}

void XTreeWidget_setHeaderLabels(XTreeWidget* self,
                                 const char* const* labels, int count)
{
    int i;
    if (!self || !labels || count <= 0) return;
    if (count > self->m_headerCapacity) {
        int cap = self->m_headerCapacity > 0 ? self->m_headerCapacity : 4;
        XString** grown;
        while (cap < count) cap *= 2;
        grown = (XString**)XRealloc_System(
            self->m_headerLabels, sizeof(XString*) * (size_t)cap);
        if (!grown) return;
        /* 新增区域必须清零：下方循环以 m_headerLabels[i] 是否为 NULL
         * 判定首次创建；realloc 的未初始化内存是野指针，直接复用会
         * 崩溃（同 XTableWidget 垂直表头扩容模式）。 */
        XMemset(grown + self->m_headerCapacity, 0,
                sizeof(XString*) * (size_t)(cap - self->m_headerCapacity));
        self->m_headerLabels = grown;
        self->m_headerCapacity = cap;
    }
    for (i = 0; i < count; ++i) {
        if (!self->m_headerLabels[i])
            self->m_headerLabels[i] = XString_create();
        if (self->m_headerLabels[i])
            XString_assign_utf8(self->m_headerLabels[i],
                                labels[i] ? labels[i] : "");
    }
    if (count > self->m_headerCount) self->m_headerCount = count;
    /* 表头条目子节点文本镜像（Qt 语义：表头即条目；列文本读子条目）。 */
    if (self->m_headerItem) {
        for (i = 0; i < count; ++i) {
            XTreeWidgetItem* col = XTreeWidgetItem_child(self->m_headerItem, i);
            if (!col) {
                col = XTreeWidgetItem_create(NULL, NULL);
                if (!col || !XTreeWidgetItem_addChild(self->m_headerItem, col)) {
                    if (col) XTreeWidgetItem_delete(col);
                    break;
                }
            }
            XTreeWidgetItem_setText_2(col, labels[i] ? labels[i] : "");
        }
    }
    XWidget_update((XWidget*)self);
}

void XTreeWidget_setHeaderLabel(XTreeWidget* self, const char* label)
{
    const char* labels[1];
    labels[0] = label; /* NULL 允许：批量族元素 NULL=空文本。 */
    /* 便捷转发：走 setHeaderLabels 同一承载（首次创建、重复覆写）。 */
    XTreeWidget_setHeaderLabels(self, labels, 1);
}

XTreeWidgetItem* XTreeWidget_headerItem(const XTreeWidget* self)
{ return self ? self->m_headerItem : NULL; }

const char* XTreeWidget_headerLabel(const XTreeWidget* self, int column)
{
    if (!self || column < 0 || column >= self->m_headerCount ||
        !self->m_headerLabels || !self->m_headerLabels[column])
        return "";
    return XString_toUtf8(self->m_headerLabels[column]);
}

void XTreeWidget_setHeaderItem(XTreeWidget* self, XTreeWidgetItem* item)
{
    int i;
    if (!self || !item) return;
    if (self->m_headerItem == item) return;
    if (self->m_headerItem) XTreeWidgetItem_delete(self->m_headerItem);
    self->m_headerItem = item; /* 移交所有权：随控件析构。 */
    /* 子节点文本回填标签承载（绘制路径读 m_headerLabels）。 */
    for (i = 0; i < item->childCount; ++i) {
        XTreeWidgetItem* col = XTreeWidgetItem_child(item, i);
        xtw_setHeaderLabelAt(self, i, col ? XTreeWidgetItem_text_2(col) : "");
    }
    XWidget_update((XWidget*)self);
}

/* ==================== 展开/折叠与结构便捷族（对标 QTreeWidget） ==================== */

void XTreeWidget_expandItem(XTreeWidget* self, int row)
{
    /* 展开状态承载（m_topExpanded）由折叠变展开：发射 itemExpanded，
     * 折叠行的子树恢复绘制（对接说明见 @note）。 */
    xtw_setRowExpanded(self, row, true);
}

void XTreeWidget_collapseItem(XTreeWidget* self, int row)
{
    /* 展开状态承载（m_topExpanded）由展开变折叠：发射 itemCollapsed，
     * 折叠后子树停止绘制（对接说明见 @note）。 */
    xtw_setRowExpanded(self, row, false);
}

void XTreeWidget_setTextAt_2(XTreeWidget* self, int row, int column,
                             const char* text)
{
    XTreeWidgetItem* item;
    if (!self) return;
    item = XTreeWidget_topLevelItem(self, row);
    if (!item) return;
    XTreeWidgetItem_setTextAt_2(item, column, text);
}

int XTreeWidget_indexOfTopLevelItem(const XTreeWidget* self,
                                    const char* text)
{
    int i;
    if (!self || !text) return -1;
    for (i = 0; i < self->m_topCount; ++i) {
        const XString* cell = XTreeWidgetItem_text(self->m_topItems[i]);
        /* 精确相等（区分大小写，同 findItems 精确模式口径），仅顶层
         * 条目参与匹配（对标 indexOfTopLevelItem 只查顶层）。 */
        if (cell && XString_equals_utf8(cell, text, XChar_CaseSensitive))
            return i;
    }
    return -1;
}

int XTreeWidget_itemAbove(const XTreeWidget* self, int row)
{
    /* 平铺顶层行模型：可视次序即行号次序，above = row - 1。 */
    if (!self || row <= 0 || row >= self->m_topCount) return -1;
    return row - 1;
}

int XTreeWidget_itemBelow(const XTreeWidget* self, int row)
{
    /* 平铺顶层行模型：可视次序即行号次序，below = row + 1。 */
    if (!self || row < 0 || row >= self->m_topCount - 1) return -1;
    return row + 1;
}

void XTreeWidget_setColumnCount(XTreeWidget* self, int count)
{
    if (!self || count < 1) return;
    /* 列容量对接：扩大列数时同步扩容单元格部件各行列容量，
     * 新列的 setItemWidget 挂载即时可用。 */
    if (count > self->m_cellColCapacity)
        xtw_ensureCellCols(self, count);
    self->m_columnCount = count;
    xtw_bridgeSync(self); /* 四期②：列数变化同步模型列数与内容。 */
    XWidget_update((XWidget*)self);
}

/* ==================== 信号（调用即发射并返回连接句柄） ==================== */

void* XTreeWidget_itemClicked_signal(XTreeWidget* self, int row)
{
    /* 载荷 (row, item)：真实发射点在 VXTreeWidget_mousePressEvent
     * （携带命中条目本体）；本句柄手动发射路径条目未知，以 NULL 占
     * 位保持双参载荷恒定。 */
    xtw_emitRowItem(self, (size_t)XTreeWidget_itemClicked_signal, row,
                    NULL);
    return (void*)(size_t)XTreeWidget_itemClicked_signal;
}

void* XTreeWidget_itemDoubleClicked_signal(XTreeWidget* self, int row)
{
    xtw_emitRow(self, (size_t)XTreeWidget_itemDoubleClicked_signal, row);
    return (void*)(size_t)XTreeWidget_itemDoubleClicked_signal;
}

void* XTreeWidget_itemPressed_signal(XTreeWidget* self, int row)
{
    /* 载荷 (row, item)：与 itemClicked 同为双参（见本函数真实发射
     * 点 VXTreeWidget_mousePressEvent；手动路径条目 NULL 占位）。 */
    xtw_emitRowItem(self, (size_t)XTreeWidget_itemPressed_signal, row,
                    NULL);
    return (void*)(size_t)XTreeWidget_itemPressed_signal;
}

void* XTreeWidget_itemActivated_signal(XTreeWidget* self, int row)
{
    xtw_emitRow(self, (size_t)XTreeWidget_itemActivated_signal, row);
    return (void*)(size_t)XTreeWidget_itemActivated_signal;
}

void* XTreeWidget_itemEntered_signal(XTreeWidget* self, int row)
{
    /* 真实发射点：VXTreeWidget_mouseMoveEvent 进入新顶层行（§8.0g16
     * 数据模型四期③；连接方亦可直接调用本句柄手动发射）。 */
    xtw_emitRow(self, (size_t)XTreeWidget_itemEntered_signal, row);
    return (void*)(size_t)XTreeWidget_itemEntered_signal;
}

void* XTreeWidget_itemChanged_signal(XTreeWidget* self, int row)
{
    xtw_emitRow(self, (size_t)XTreeWidget_itemChanged_signal, row);
    return (void*)(size_t)XTreeWidget_itemChanged_signal;
}

void* XTreeWidget_itemExpanded_signal(XTreeWidget* self, int row)
{
    xtw_emitRow(self, (size_t)XTreeWidget_itemExpanded_signal, row);
    return (void*)(size_t)XTreeWidget_itemExpanded_signal;
}

void* XTreeWidget_itemCollapsed_signal(XTreeWidget* self, int row)
{
    xtw_emitRow(self, (size_t)XTreeWidget_itemCollapsed_signal, row);
    return (void*)(size_t)XTreeWidget_itemCollapsed_signal;
}

void* XTreeWidget_currentItemChanged_signal(XTreeWidget* self, int current,
                                            int previous)
{
    xtw_emitRow2(self, (size_t)XTreeWidget_currentItemChanged_signal,
                 current, previous);
    return (void*)(size_t)XTreeWidget_currentItemChanged_signal;
}

void* XTreeWidget_itemSelectionChanged_signal(XTreeWidget* self)
{
    xtw_emitNone(self, (size_t)XTreeWidget_itemSelectionChanged_signal);
    return (void*)(size_t)XTreeWidget_itemSelectionChanged_signal;
}

/* ==================== 渲染 ==================== */

/** @brief 调色板取色助手（对照 XTableWidget.c xtw_color 范式；
 *  根因：自绘配色硬编码不读调色板，非默认调色板下仍白底黑字）。 */
static uint32_t xtw_color(const XTreeWidget* self, XPaletteColorRole role)
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

/** @brief 列 x/宽（行内容消费与表头同规则：显式列宽
 *         XTreeView_setColumnWidth>0 优先，其余列均摊剩余宽度）。
 *         超出视口宽的列回报 x=width/w=0。 */
static void xtw_columnSpan(const XTreeWidget* tw, int column, int width,
                           int* outX, int* outW)
{
    int c;
    int fixedSum = 0;
    int autoCount = 0;
    int autoShare = 0;
    int x = 0;
    if (!tw || column < 0 || width <= 0) {
        if (outX) *outX = 0;
        if (outW) *outW = 0;
        return;
    }
    for (c = 0; c < XTreeWidget_columnCount(tw); ++c) {
        int w = XTreeView_columnWidth(&tw->m_base, c);
        if (w > 0) fixedSum += w;
        else ++autoCount;
    }
    autoShare = (autoCount > 0 && width > fixedSum)
                    ? (width - fixedSum) / autoCount
                    : 0;
    for (c = 0; c <= column && x < width; ++c) {
        int w = XTreeView_columnWidth(&tw->m_base, c);
        if (w <= 0) w = autoShare;
        if (c == column) {
            if (outX) *outX = x;
            if (outW) *outW = w;
            return;
        }
        x += w;
    }
    if (outX) *outX = x;
    if (outW) *outW = 0;
}

/** @brief 列 0 勾选指示器绘制（checkState != Unchecked 时呈现；
 *         对标 QTreeWidgetItem 指示器：12x12 复选框，选中画对勾、
 *         部分选中画中横线，复用 XCheckBox 视觉口径）。 */
static void xtw_drawCheckIndicator(XTreeWidgetItem* item,
                                   XPainter* painter, int x, int y0,
                                   int rh, uint32_t windowText)
{
    XRect box;
    uint32_t frame;
    if (!item || item->checkState == XItemCheckState_Unchecked) return;
    frame = 0xFF7A7A7Au;
    box.x = x;
    box.y = y0 + (rh - 12) / 2;
    box.width = 12;
    box.height = 12;
    XPainter_fillRect(painter, &box, 0xFFFFFFFFu);
    XPainter_fillRect(painter, &(XRect){box.x, box.y, box.width, 1}, frame);
    XPainter_fillRect(painter, &(XRect){box.x, box.y, 1, box.height}, frame);
    XPainter_fillRect(painter, &(XRect){box.x, box.y + box.height - 1,
                                        box.width, 1}, frame);
    XPainter_fillRect(painter, &(XRect){box.x + box.width - 1, box.y,
                                        1, box.height}, frame);
    if (item->checkState == XItemCheckState_PartiallyChecked) {
        XPainter_setPen(painter, windowText);
        XPainter_drawLine(painter, box.x + 3, box.y + box.height / 2,
                          box.x + box.width - 3, box.y + box.height / 2);
    } else {
        XPainter_setPen(painter, windowText);
        XPainter_drawLine(painter, box.x + 3, box.y + 7,
                          box.x + 6, box.y + 10);
        XPainter_drawLine(painter, box.x + 6, box.y + 10,
                          box.x + 10, box.y + 3);
    }
}

/** @brief UTF-8 序列字节长（首字节判定；续字节/非法首字节按单字节
 *         兜底，省略切割只在字符边界落刀）。 */
static int xtw_utf8SeqLen(const char* s)
{
    unsigned char c = (unsigned char)s[0];
    if (c < 0x80) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;
}

/** @brief 右省略（W10 第三轮①：对标 Qt::ElideRight，Qt 以
 *         qstyleditemdelegate viewItemDrawText → fontMetrics::elidedText
 *         对超宽条目文本画「前缀…」；本库近似承载见 xcs_elideText
 *         mode 1 同款算法，此处按 UTF-8 字符边界切割——中文长名不得
 *         在序列中腰斩出替换乱字）。省略号视觉以 ASCII "..." 三点
 *         承载：Qt 原字为 U+2026 HORIZONTAL ELLIPSIS，但本库内置
 *         字体族均无该字形（XFont16x16 点阵 cmap 仅覆盖 32..126 与
 *         19968.. 两段；XFontOutlineCommon Latin/CJK 码点表亦无
 *         0x2026，缺字退化为单点残形——活体实测见 W10 复验），故以
 *         全字体覆盖的 "..." 同义呈现（视觉同 Qt 的三点省略号）。
 *         宽内原样返回入参；超宽返回栈缓冲内「前缀+...」（宽内放
 *         不下三点时退化为纯前缀硬截，同样不越界）。 */
static const char* xtw_elideText(char* buf, int bufCap, const char* text,
                                 const XFont* font, int maxW)
{
    static const char ell[] = "..."; /* 省略号（U+2026 缺字形，见上）。 */
    int fullW;
    int ellW;
    int w;
    int cut;
    int i;
    if (!text || !text[0] || maxW <= 0 || !font) return text;
    fullW = XPainter_textWidth(font, text);
    if (fullW <= maxW) return text;
    ellW = XPainter_textWidth(font, ell);
    w = (ellW <= maxW) ? ellW : 0; /* 连省略号都放不下：纯前缀。 */
    cut = 0;
    for (i = 0; text[i] != '\0';) {
        int len = xtw_utf8SeqLen(text + i);
        int cw;
        int j;
        for (j = 1; j < len; ++j)
            if (text[i + j] == '\0') { len = j; break; } /* 残序列收窄防越界读。 */
        cw = XPainter_textWidthRange(font, text, i, i + len);
        if (w + cw > maxW) break;
        w += cw;
        cut = i + len;
        i += len;
    }
    if (cut > bufCap - 5) cut = bufCap - 5; /* 预留 ...+NUL，防缓冲溢出。 */
    if (cut < 0) cut = 0;
    XMemcpy(buf, text, (size_t)cut);
    buf[cut] = '\0';
    if (ellW <= maxW) XStrncat(buf, ell, (size_t)bufCap - 1);
    return buf;
}

/** @brief 列 0 主文本绘制（N5-1/W10 根修；第八轮①再修）：多列树下
 *         列 0 超宽文本由硬 clip 升级为右省略——先按列 0 带内预算
 *         （带右缘 − 文本起点）测算 elide 文本「前缀…」再画（对标
 *         Qt::ElideRight，qstyleditemdelegate viewItemDrawText 的
 *         elidedText 路径），省略号自身在带内、不再出现第七轮右边界
 *         硬截断无点画面；残余超界仍由 save/clip/restore 兜底（与列
 *         1+ 同口径防长文本横穿「大小/类型」等列）。仅裁剪文本本身：
 *         勾选框（调用方先画）与展开缩进 +/- 指示器（调用方后画）
 *         不入本裁剪，树形结构视觉不受影响。单列树（columnCount==1，
 *         无列界可串）与列带退化（w<=0，如固定列宽挤出到视口外）回退
 *         既有无裁剪画法。 */
static void xtw_drawColumn0Text(const XTreeWidget* self, XPainter* painter,
                                int y0, int rh, int textX, const char* text,
                                uint32_t ink)
{
    int viewW = XWidget_width((XWidget*)self);
    int colX = 0;
    int colW = 0;
    XRect colRect;
    const char* shown = text;
    char elided[512];
    if (XTreeWidget_columnCount(self) <= 1 || viewW <= 0) {
        XPainter_drawText(painter, textX, y0 + rh - 6, text, ink);
        return;
    }
    xtw_columnSpan(self, 0, viewW, &colX, &colW);
    if (colW <= 0) {
        XPainter_drawText(painter, textX, y0 + rh - 6, text, ink);
        return;
    }
    if (colX + colW > textX) {
        shown = xtw_elideText(elided, (int)sizeof(elided), text,
                              XPainter_font(painter), colX + colW - textX);
    }
    colRect.x = colX;
    colRect.y = y0;
    colRect.width = colW;
    colRect.height = rh;
    XPainter_save(painter);
    XPainter_setClipRect(painter, &colRect,
                         XPainterClipOperation_IntersectClip);
    XPainter_drawText(painter, textX, y0 + rh - 6, shown, ink);
    XPainter_restore(painter);
}

/** @brief 绘制单个条目行（前序递归；k = 本条目在顶层行带内的前序
 *         序号：顶层本体 0、首个子条目 1…与 xtw_hitItemAt 输出同一
 *         口径，即行带内行偏移）。 */
static void xtw_drawItem(XTreeWidget* self, XTreeWidgetItem* item,
                         XPainter* painter, int depth, int* y, int maxY,
                         int topRow, int k)
{
    XTreeView* tv = &self->m_base;
    int rh = tv->m_rowHeight > 0 ? tv->m_rowHeight : XTW_ROW_H;
    int indent = (tv->m_indentation > 0 ? tv->m_indentation : 20);
    XRect cell;
    const char* text;
    uint32_t base;
    uint32_t windowText;
    uint32_t highlight;
    uint32_t highlightedText;
    bool selected;
    int y0 = *y;
    if (y0 >= maxY) return;
    base = xtw_color(self, XPaletteColorRole_Base);
    windowText = xtw_color(self, XPaletteColorRole_WindowText);
    highlight = xtw_color(self, XPaletteColorRole_Highlight);
    highlightedText = xtw_color(self, XPaletteColorRole_HighlightedText);
    /* 选中行整行 Highlight 填充、文字反转 HighlightedText（对标
     * QTreeView::drawRow qtreeview.cpp:1781 的 isSelected(modelIndex)
     * → State_Selected，经 PE_PanelItemViewRow 以 Highlight 刷填充
     * 整行背景，qcommonstyle.cpp:706）。
     * 粒度口径（第二轮 #14）：选择模型仍为顶层行（itemClicked 首参
     * row / selectedItems 等 apitest 契约不变）；子条目命中行按 Qt
     * 「点击索引即当前索引、isSelected 对子条目索引同样成立」的语义
     * 呈现 Highlight（qt QTreeView 点子节点只亮该子行、父行不亮），
     * 此时其顶层行本体不再重复刷 Highlight（一行一选中）。承载见
     * xtw_setCurChild（文件级 static，不占契约头字段）。 */
    selected =
        self->m_base.m_base.m_selectionMode !=
            XAbstractItemViewSelectionMode_NoSelection &&
        ((item->parent == NULL)
             ? (!xtw_bandHasChildCur(self, topRow) &&
                XItemSelectionModel_isSelected(
                    self->m_base.m_base.m_selectionModel, topRow, 0))
             : xtw_curChildHit(self, topRow, k));
    cell.x = 0;
    cell.y = y0;
    cell.width = XWidget_width((XWidget*)self);
    cell.height = rh;
    if (item->parent == NULL) {
        XPainter_fillRect(painter, &cell, base);
        if (selected) XPainter_fillRect(painter, &cell, highlight);
    } else if (selected) {
        /* 子条目命中行：与顶层行同口径整行 Highlight（原实现子条目
         * 从不填充，点子行高亮滞留顶层行——复扫 #14 partial 根因）。 */
        XPainter_fillRect(painter, &cell, highlight);
    }
    text = XTreeWidgetItem_text_2(item);
    if (item->checkState != XItemCheckState_Unchecked) {
        int ix = indent * depth + 12;
        xtw_drawCheckIndicator(item, painter, ix, y0, rh, windowText);
        if (text && text[0]) {
            XPainter_setPen(painter,
                            selected ? highlightedText : windowText);
            xtw_drawColumn0Text(self, painter, y0, rh, ix + 16, text,
                                selected ? highlightedText : windowText);
        }
    } else if (text && text[0]) {
        XPainter_setPen(painter,
                        selected ? highlightedText : windowText);
        /* drawText 第 4 参是墨水色：传 0=透明，条目文本任何路径都不
         * 出字；传 palette WindowText（对标 XTableWidget，此处实现
         * 与注释曾自相矛盾——注释自称传 windowText 实为硬编码黑）。
         * 选中行传 HighlightedText（对标 Qt 选中行文字反色）。
         * 绘制经 xtw_drawColumn0Text：多列树下列 0 带内裁剪
         * （N5-1/W10），单列树保持既有无裁剪画法。 */
        xtw_drawColumn0Text(self, painter, y0, rh, indent * depth + 12,
                            text,
                            selected ? highlightedText : windowText);
    }
    /* 列 1+ 文本消费（四期④）：各列画在 xtw_columnSpan 的列带内
     * （save/clip/restore 防长文本串列；列 0 主文本已由
     * xtw_drawColumn0Text 在列 0 带内同口径裁剪，展开缩进/指示器
     * 仍在文本裁剪之外独立绘制）。 */
    if (XTreeWidget_columnCount(self) > 1) {
        int viewW = XWidget_width((XWidget*)self);
        int col;
        for (col = 1; col < XTreeWidget_columnCount(self); ++col) {
            const char* colText = XTreeWidgetItem_textAt_2(item, col);
            int colX = 0;
            int colW = 0;
            XRect colRect;
            if (!colText || !colText[0]) continue;
            xtw_columnSpan(self, col, viewW, &colX, &colW);
            if (colW <= 8) continue;
            colRect.x = colX;
            colRect.y = y0;
            colRect.width = colW;
            colRect.height = rh;
            XPainter_save(painter);
            XPainter_setClipRect(painter, &colRect,
                                 XPainterClipOperation_IntersectClip);
            /* 列 1+ 文字与行选中态联动反色（同 Qt drawRow：一行内
             * 各列经同一 option 状态绘制）。 */
            XPainter_setPen(painter,
                            selected ? highlightedText : windowText);
            XPainter_drawText(painter, colX + 4, y0 + rh - 6, colText,
                              selected ? highlightedText : windowText);
            XPainter_restore(painter);
        }
    }
    /* 子节点指示（顶层行按展开态绘制 +/-：折叠补竖线）。 */
    if (item->childCount > 0) {
        int bx = indent * depth + 4;
        int by = y0 + rh / 2;
        bool expanded = (item->parent != NULL)
                            ? true
                            : xtw_isExpanded(self, topRow);
        XPainter_setPen(painter, 0xFF888888u);
        XPainter_drawLine(painter, bx - 2, by, bx + 2, by);
        if (!expanded)
            XPainter_drawLine(painter, bx, by - 3, bx, by + 3);
    }
    XPainter_setPen(painter, 0xFFDDDDDDu);
    XPainter_drawLine(painter, 0, y0 + rh - 1,
                      XWidget_width((XWidget*)self), y0 + rh - 1);
    *y += rh;
    /* 顶层行折叠：整棵子树停止绘制（itemCollapsed 路径的可视面）。 */
    if (item->parent == NULL && !xtw_isExpanded(self, topRow)) return;
    {
        int i;
        int childK = k + 1; /* 首个子条目前序序号 = 本条目 + 1。 */
        for (i = 0; i < item->childCount; ++i) {
            if (item->children[i]) {
                /* 后继兄弟序号 = 当前子条目序号 + 其子树行数
                 * （前序遍历中每条目恰占行带内一行）。 */
                xtw_drawItem(self, item->children[i], painter, depth + 1,
                             y, maxY, topRow, childK);
                childK += xtw_subtreeRows(item->children[i]);
            }
        }
    }
}

/** @brief 绘制视口顶部表头带（此前 setHeaderLabels/setHeaderItem 只
 *         存储不渲染；对标 QTreeWidget 默认 headerVisible，表头由
 *         QHeaderView 渲染于视口顶部）。列标签读 setHeaderLabels/
 *         setHeaderItem 回填的 m_headerLabels，缺省回退列号；列宽
 *         显式值（XTreeView_setColumnWidth，>0）优先、其余列均摊
 *         剩余宽度（对标 XTreeView 自动铺满口径的公开 API 近似）。 */
static void xtw_drawHeader(const XTreeWidget* tw, XPainter* painter,
                           int width)
{
    int cols;
    int fixedSum;
    int autoCount;
    int autoShare;
    int c;
    int x;
    XRect band;
    if (!tw || !painter) return;
    cols = XTreeWidget_columnCount(tw);
    if (cols <= 0 || width <= 0) return;
    fixedSum = 0;
    autoCount = 0;
    for (c = 0; c < cols; ++c) {
        int w = XTreeView_columnWidth(&tw->m_base, c);
        if (w > 0) fixedSum += w;
        else ++autoCount;
    }
    autoShare = (autoCount > 0 && width > fixedSum)
                    ? (width - fixedSum) / autoCount
                    : 0;
    XRect_init(&band, 0, 0, width, XTW_HEADER_H);
    XPainter_fillRect(painter, &band, 0xFFF0F0F0u);
    x = 0;
    for (c = 0; c < cols && x < width; ++c) {
        int w = XTreeView_columnWidth(&tw->m_base, c);
        const char* text;
        /* 声明在列循环层：buf 指针经 text 活到 drawText 之后
         * （块内声明出块即死，ASan stack-use-after-scope）。 */
        char buf[16];
        if (w <= 0) w = autoShare;
        if (w <= 0) continue; /* 零宽列（显式 0 且无均摊空间）跳过。 */
        text = NULL;
        if (c < tw->m_headerCount && tw->m_headerLabels &&
            tw->m_headerLabels[c])
            text = XString_toUtf8(tw->m_headerLabels[c]);
        if (!text || !text[0]) {
            XSnprintf(buf, sizeof(buf), "%d", c + 1);
            text = buf;
        }
        XPainter_setPen(painter, 0xFF444444u);
        {
            /* 表头标签右省略（第八轮②配套；对标 QHeaderView 样式层对
             * 段标签 elidedText——qstyleditemdelegate 同源路径）：拖窄
             * 后标签不得压过分隔线串到邻段（与列 0 行文本同 xtw_elideText
             * 承载，预算 = 段宽 − 左右各 4px 边距）。 */
            const XFont* hfont = XPainter_font(painter);
            char label[512];
            const char* shown =
                xtw_elideText(label, (int)sizeof(label), text, hfont, w - 8);
            XPainter_drawText(painter, x + 4, XTW_HEADER_H - 6, shown,
                              0xFF444444u);
        }
        XPainter_setPen(painter, 0xFFCCCCCCu);
        XPainter_drawLine(painter, x + w - 1, 1, x + w - 1,
                          XTW_HEADER_H - 1);
        x += w;
    }
}

static void VXTreeWidget_paintEvent(XWidget* self, XEvent* event)
{
    XTreeWidget* tw = (XTreeWidget*)self;
    XImage* image;
    XPainter painter;
    XRect r;
    XPoint offset;
    uint32_t base;
    int y;
    int i;
    int h;
    int offY;
    (void)event;
    if (!tw) return;
    image = XWidget_paintImage(self);
    if (!image) return;
    h = XWidget_height(self);
    XRect_init(&r, 0, 0, XWidget_width(self), h);
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
    /* 视图族配色消费调色板：视图底色走 Base（对标 Qt item view，
     * 默认调色板下与历史硬编码白一致，零视觉漂移）。 */
    base = xtw_color(tw, XPaletteColorRole_Base);
    XPainter_fillRect(&painter, &r, base);
    {
        /* 滚动范围维护 + 偏移平移（此前滚动条值变化不触发重绘）。
         * 表头带计入内容高度（对标 QHeaderView 占位视口顶部，
         * headerHidden 时为 0）。 */
        XScrollBar* vbar = XAbstractScrollArea_verticalScrollBar(
            (XAbstractScrollArea*)&tw->m_base.m_base);
        int headerOffset = xtw_headerOffset(tw);
        int rows = 0;
        int i2;
        for (i2 = 0; i2 < tw->m_topCount; ++i2)
            rows += (tw->m_topItems[i2] && xtw_isExpanded(tw, i2))
                        ? xtw_subtreeRows(tw->m_topItems[i2])
                        : 1;
        {
            /* 对标 Qt QAbstractScrollArea 滚动条按需呈现：内容尺寸经
             * setContentSize 上报后由 xasa_updateScrollBars 统一驱动
             * AsNeeded 可见性与范围（此前仅 setRange——范围有计算而
             * 滚动条从不显示；内容溢出被控件边缘硬裁）。showV 翻转
             * 发生在 resize 之后时布局器不再重跑，这里补一次滚动条
             * 几何（同 VX_asa_resizeEvent 的右缘 16px 带口径；
             * setGeometry 同值早退，稳态重绘零开销）。 */
            XAbstractScrollArea* area =
                (XAbstractScrollArea*)&tw->m_base.m_base;
            int contentH = headerOffset + rows * xtw_effectiveRowHeight(tw);
            XAbstractScrollArea_setContentSize(area, r.width, contentH);
            if (vbar && XWidget_isVisible((XWidget*)vbar)) {
                bool showH = area->m_hPolicy !=
                                 XScrollBarPolicy_AlwaysOff &&
                             (area->m_hPolicy ==
                                  XScrollBarPolicy_AlwaysOn ||
                              area->m_contentWidth > r.width);
                XRect barRect;
                XRect_init(&barRect, r.width - XTW_SBW, 0, XTW_SBW,
                           showH ? h - XTW_SBW : h);
                XWidget_setGeometryRect((XWidget*)vbar, &barRect);
            }
        }
        /* 表头带绘制于视口顶部（不随内容滚动；headerHidden 时不占位）。 */
        if (headerOffset > 0) xtw_drawHeader(tw, &painter, r.width);
        offY = xtw_scrollOffsetY(tw);
        /* 内容区钉顶裁剪（对标 QTreeView 表头常驻 + viewport 内容裁
         * 剪：qtreeview.cpp:2911-2913 updateGeometries 将表头作为子
         * 部件 setGeometry 钉在视口顶缘、setViewportMargins(0,height,
         * 0,0) 预留顶带，内容只在 viewport 带内滚动。本控件单画布自
         * 绘，等价实现 = 行带绘制前对「表头之下」区域取交集裁剪——
         * 否则上滚的行带（内容坐标 < offY 的行）直接画进表头带，视
         * 觉即"表头随内容上滚被裁"（复扫新问题②；此前仅靠表头先行
         * 绘制的 z 序，行带后绘反而覆写表头）。
         * 裁剪在平移前置（屏幕坐标 [headerOffset, h)），下缘与行绘制
         * 下限 h+offY-headerOffset 的映射一致。 */
        XPainter_save(&painter);
        {
            XRect clip;
            XRect_init(&clip, 0, headerOffset, r.width,
                       h - headerOffset);
            XPainter_setClipRect(&painter, &clip,
                                 XPainterClipOperation_IntersectClip);
        }
        if (offY != 0)
            XPainter_translate(&painter, 0.0f, -(float)offY);
        if (headerOffset != 0)
            XPainter_translate(&painter, 0.0f, (float)headerOffset);
        y = 0;
        for (i = 0; i < tw->m_topCount; ++i) {
            if (tw->m_topItems[i]) {
                /* 行绘制下限同步下移（跳过视口上方内容；行带下移表头
                 * 高度，内容坐标下限相应收窄）。顶层调用前序序号 k=0
                 * （子条目由递归按子树行数推进）。 */
                xtw_drawItem(tw, tw->m_topItems[i], &painter, 0, &y,
                             h + offY - headerOffset, i, 0);
            }
        }
        XPainter_restore(&painter);
        y = 0; /* 复位供后续逻辑（如有） */
    }
    XPainter_end(&painter);
    XPainter_deinit(&painter);
}

/** @brief 键盘导航（对标 QTreeWidget::moveCursor）：根因——条目存
 *  自有链表（m_topItems/m_topCount）而基类导航以 m_model 为界
 *  （rows=cols=0 一律 ignore），方向键/翻页/Home/End 整体失效；本类
 *  按平铺顶层行先行消费：Up/Down/翻页按行步进，Home/End 落首末行；
 *  Left 于展开行收拢（对标 QTreeView 折叠分支），Right 于折叠行展开，
 *  均无折叠可做时回退列横移（多列承载下对标 QAbstractItemView）；
 *  其余按键回落基类（Return/F2 编辑触发、可打印字符键盘搜索；基类
 *  导航分支因 m_model 为空自然 ignore，不产生越界移动）。 */
static void VXTreeWidget_keyPressEvent(XWidget* self, XEvent* event)
{
    XTreeWidget* tw = (XTreeWidget*)self;
    XWidget* viewport;
    int key;
    int current;
    int target = -1;
    int pageRows;
    bool handled = false;
    if (!tw || !event || XEvent_type(event) != XEVENT_TYPE_KEY_PRESS) return;
    key = XKeyEvent_key((XKeyEvent*)event);
    current = tw->m_base.m_base.m_currentRow;
    /* 翻页步幅按视口高度/行高（对标 Qt 翻页以视口计页）。 */
    viewport = XAbstractScrollArea_viewport(
        (XAbstractScrollArea*)&tw->m_base.m_base);
    pageRows = XWidget_height(viewport ? viewport : self) /
               xtw_effectiveRowHeight(tw);
    if (pageRows < 1) pageRows = 1;
    switch (key) {
    case XKey_Left: {
        XTreeWidgetItem* item =
            (current >= 0 && current < tw->m_topCount)
                ? tw->m_topItems[current]
                : NULL;
        if (item && item->childCount > 0 && xtw_isExpanded(tw, current)) {
            /* 展开的当前行收拢（发射 itemCollapsed，同指示器点击）。 */
            xtw_setRowExpanded(tw, current, false);
            target = current; /* 当前行不变，仅确保可见。 */
            handled = true;
        } else if (item && tw->m_base.m_base.m_currentColumn > 0) {
            /* 无折叠可做回退列左移（当前行仅列变化，行号不变）。 */
            XAbstractItemView_setCurrentIndex(
                &tw->m_base.m_base, current,
                tw->m_base.m_base.m_currentColumn - 1);
            XWidget_update(self);
            target = current;
            handled = true;
        }
        break;
    }
    case XKey_Right: {
        XTreeWidgetItem* item =
            (current >= 0 && current < tw->m_topCount)
                ? tw->m_topItems[current]
                : NULL;
        if (item && item->childCount > 0 && !xtw_isExpanded(tw, current)) {
            xtw_setRowExpanded(tw, current, true);
            target = current;
            handled = true;
        } else if (item && tw->m_base.m_base.m_currentColumn + 1 <
                               XTreeWidget_columnCount(tw)) {
            XAbstractItemView_setCurrentIndex(
                &tw->m_base.m_base, current,
                tw->m_base.m_base.m_currentColumn + 1);
            XWidget_update(self);
            target = current;
            handled = true;
        }
        break;
    }
    case XKey_Up:
        handled = true;
        target = (current >= 0) ? current - 1 : 0;
        break;
    case XKey_Down:
        handled = true;
        target = (current >= 0) ? current + 1 : 0;
        break;
    case XKey_PageUp:
        handled = true;
        target = (current >= 0) ? current - pageRows : 0;
        break;
    case XKey_PageDown:
        handled = true;
        target = (current >= 0) ? current + pageRows : 0;
        break;
    case XKey_Home:
        handled = true;
        target = 0;
        break;
    case XKey_End:
        handled = true;
        target = tw->m_topCount - 1;
        break;
    default:
        break;
    }
    if (handled) {
        if (tw->m_topCount > 0) {
            /* 收敛到合法顶层行带（同基类"增量后钳位"口径；Up/上翻页
             * 越过首行的负目标钳回首行）。 */
            if (target > tw->m_topCount - 1) target = tw->m_topCount - 1;
            if (target < 0) target = 0;
            /* 统一当前行路径：currentItemChanged/itemSelectionChanged
             * 发射 + SelectCurrent 选择联动；EnsureVisible 滚动跟随。 */
            xtw_setCurrentRow(tw, target);
            xtw_scrollRowVisible(tw, target);
        }
        XEvent_accept(event);
        return;
    }
    /* 静态取父类槽位回落（同 XDialog 对 XWidget 的 XClass_Parent
     * 口径：经对象虚表再分派会回到本重载形成自递归）。 */
    XClass_Parent(XTreeView, EXWidget_KeyPressEvent,
                  void (*)(XWidget*, XEvent*))(self, event);
}

/* ==================== 表头分隔线拖拽调宽手势（第八轮②） ==================== */

/** @brief 表头带内分隔线命中反查（对标 QHeaderViewPrivate::
 *         sectionHandleAt，qheaderview.cpp:3307）：列尾分隔线与
 *         xtw_drawHeader 画线同一几何（x+w-1），±3px 热区内返回该列
 *         号（拖拽改该列宽），未命中 -1。 */
static int xtw_headerHandleAt(const XTreeWidget* self, int x)
{
    int viewW;
    int cols;
    int c;
    if (!self || x < 0) return -1;
    viewW = XWidget_width((XWidget*)self);
    cols = XTreeWidget_columnCount(self);
    if (cols <= 0 || viewW <= 0) return -1;
    for (c = 0; c < cols; ++c) {
        int colX = 0;
        int colW = 0;
        int sep;
        xtw_columnSpan(self, c, viewW, &colX, &colW);
        if (colW <= 0) continue; /* 零宽列无线可拖（与绘制同口径）。 */
        sep = colX + colW - 1;
        if (x >= sep - XTW_HANDLE_HIT && x <= sep + XTW_HANDLE_HIT)
            return c;
    }
    return -1;
}

/** @brief 挂 SplitH 光标（对标 qheaderview.cpp:2640 水平头
 *         SplitHCursor；XLabel/XTextEdit 悬停光标同款挂法）。 */
static void xtw_resizeCursorSet(XTreeWidget* self)
{
    XCursor cursor;
    XCursor_init(&cursor);
    XCursor_setShape(&cursor, XCursor_SplitH);
    XWidget_setCursor((XWidget*)self, &cursor);
    xtw_g_resizeCursor = true;
}

/** @brief 摘本手势挂的光标（对标 qheaderview.cpp:2644 unsetCursor）。 */
static void xtw_resizeCursorClear(void)
{
    if (!xtw_g_resizeCursor) return;
    XWidget_unsetCursor((XWidget*)xtw_g_resizeOwner);
    xtw_g_resizeCursor = false;
}

/** @brief 结束拖拽手势（清静态态 + 释放鼠标抓取；XScrollBar 拖拽
 *         同款收尾）。 */
static void xtw_resizeGestureEnd(XTreeWidget* self)
{
    xtw_resizeCursorClear();
    xtw_g_resizeOwner = NULL;
    xtw_g_resizeSection = -1;
    xtw_g_resizePressX = -1;
    xtw_g_resizeOrigW = 0;
    XWidget_releaseMouse((XWidget*)self);
}

static void VXTreeWidget_mousePressEvent(XWidget* self, XEvent* event)
{
    XTreeWidget* tw = (XTreeWidget*)self;
    XMouseEvent* me;
    XPoint pos;
    int row;
    if (!tw || !event) return;
    me = (XMouseEvent*)event;
    if (XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_PRESS) return;
    pos = XMouseEvent_position(me);
    if (XMouseEvent_button(me) != XMouseButton_LeftButton) {
        XEvent_ignore(event);
        return;
    }
    /* 左键按压交付键盘焦点（对标 Qt QApplicationPrivate::
     * giveFocusAccordingToFocusPolicy 点击聚焦：视图族 StrongFocus
     * 策略下方向键导航可达；同 XAbstractItemView 按下路径一致，
     * 窗口型视图不抢焦点——弹层焦点归组合框自身机制）。 */
    if (!self->m_isWindow) XWidget_setFocus(self);
    /* 表头带内：分隔线 ±3px 热区进入拖拽调宽手势（记列/按下点/原宽
     * 并抓取鼠标，对标 qheaderview.cpp:2523-2528 的 ResizeSection 进
     * 手势分支）；段内点击不走行命中/排序语义（与分隔线命中互斥，
     * qheaderview.cpp:2511 先查手柄再发 sectionPressed 同序）。 */
    if (pos.y < xtw_headerOffset(tw)) {
        int handle = xtw_headerHandleAt(tw, pos.x);
        if (handle >= 0) {
            int colX = 0;
            int colW = 0;
            xtw_columnSpan(tw, handle, XWidget_width(self), &colX, &colW);
            xtw_g_resizeOwner = tw;
            xtw_g_resizeSection = handle;
            xtw_g_resizePressX = pos.x;
            xtw_g_resizeOrigW = colW;
            XWidget_grabMouse(self);
            xtw_resizeCursorSet(tw);
        }
        XEvent_accept(event);
        return;
    }
    /* 命中：按展开态几何反查平铺顶层行（子树行随顶层行显隐）。 */
    row = xtw_rowAtY(tw, pos.y, NULL);
    if (row >= 0) {
        XTreeWidgetItem* item = tw->m_topItems[row];
        if (item && item->childCount > 0 && pos.x < XTW_INDIC_HIT) {
            /* 展开指示器：切换展开并发射 itemExpanded/itemCollapsed。 */
            xtw_toggleExpanded(tw, row);
        } else if (item->checkState != XItemCheckState_Unchecked &&
                   pos.x >= XTW_INDIC_HIT &&
                   pos.x < XTW_INDIC_HIT + 16) {
            /* 勾选指示器命中（缩进 12px 起 12x12 框，余量到 28px）：
               三态循环 Unchecked→Checked→Unchecked（部分选中态仅在
               编程置位时出现，点击不产生）。 */
            XTreeWidgetItem_setCheckState(
                item, item->checkState == XItemCheckState_Checked
                          ? XItemCheckState_Unchecked
                          : XItemCheckState_Checked);
            xtw_setCurrentRow(tw, row);
        } else {
            /* itemPressed/itemClicked 真实发射点（先按压后点击，
             * 同 XTableWidget 约定；选中经 xtw_setCurrentRow 联动）。
             * 载荷携带真实命中条目（对标 Qt QTreeWidget::itemClicked
             * 传真实 item；XTableWidget itemClicked 同为 item 指针
             * 载荷）——此前仅发顶层行号，点子条目（网卡）状态行报
             * 父级（设备）。
             * 子条目命中态（第二轮 #14）：命中子条目 → 记 (顶层行,
             * 行带内前序序号)，xtw_drawItem 子条目分支对该行呈现
             * Highlight（点击子行→该子行亮，Qt 点击索引即当前索
             * 引语义）；命中顶层本体 → 显式清除，顶层行呈现交还
             * 选择模型（保持第一轮已绿的顶级行高亮）。首参 row 恒
             * 为顶层行号：apitest 树点击 row==0 注入断言不受影响。 */
            XTreeWidgetItem* hit;
            int hitK = -1;
            hit = xtw_hitItemAt(tw, row, pos.y, &hitK);
            xtw_setCurrentRow(tw, row);
            if (hit && hit != tw->m_topItems[row] && hitK > 0)
                xtw_setCurChild(tw, row, hitK);
            else
                xtw_setCurChild(tw, row, -1);
            xtw_emitRowItem(tw,
                            (size_t)XTreeWidget_itemPressed_signal,
                            row, hit);
            xtw_emitRowItem(tw,
                            (size_t)XTreeWidget_itemClicked_signal,
                            row, hit);
        }
    }
    XEvent_accept(event);
}

/** @brief 移动：拖拽手势中按 delta=pos-firstPos 实时改列宽（对标
 *         qheaderview.cpp:2566-2577 ResizeSection 分支——qBound 钳位
 *         后 resizeSection；本库列宽走 XTreeView_setColumnWidth，其内
 *         部 XWidget_update 驱动表头/行带即时重列重绘）；无手势时表
 *         头带内作分隔线光标提示（命中→SplitH、离开→unset，
 *         qheaderview.cpp:2636-2644 NoState 分支同款）；行带内维持既
 *         有 itemEntered 进入新行发射。 */
static void VXTreeWidget_mouseMoveEvent(XWidget* self, XEvent* event)
{
    XTreeWidget* tw = (XTreeWidget*)self;
    XMouseEvent* me;
    XPoint pos;
    int row;
    if (!tw) return;
    pos.x = 0;
    pos.y = 0;
    if (event && XEvent_type(event) == XEVENT_TYPE_MOUSE_MOVE) {
        me = (XMouseEvent*)event;
        pos = XMouseEvent_position(me);
        /* 拖拽中：实时调宽并吞事件（不做悬停发射）。 */
        if (xtw_g_resizeOwner == tw && xtw_g_resizeSection >= 0) {
            int newW = xtw_g_resizeOrigW + (pos.x - xtw_g_resizePressX);
            if (newW < XTW_MIN_COL_W) newW = XTW_MIN_COL_W; /* 最小列宽钳位。 */
            XTreeView_setColumnWidth(&tw->m_base, xtw_g_resizeSection, newW);
            XEvent_accept(event);
            return;
        }
        /* 表头带内：分隔线光标提示（与 qheaderview NoState 分支一致，
         * 经 WA_SetCursor 差分避免重复挂/摘）。 */
        if (pos.y < xtw_headerOffset(tw)) {
            if (xtw_headerHandleAt(tw, pos.x) >= 0) {
                if (!XWidget_testAttribute(self,
                                           XWidgetAttribute_SetCursor))
                    xtw_resizeCursorSet(tw);
            } else if (xtw_g_resizeCursor) {
                xtw_resizeCursorClear();
            }
            return; /* 表头带内不做行悬停发射。 */
        }
        if (xtw_g_resizeCursor) xtw_resizeCursorClear();
    }
    /* 基类移动路径：发射 XAbstractItemView entered 抽象信号。 */
    XClass_Parent(XTreeView, EXWidget_MouseMoveEvent,
                  void (*)(XWidget*, XEvent*))(self, event);
    if (!event || XEvent_type(event) != XEVENT_TYPE_MOUSE_MOVE) return;
    row = xtw_rowAtY(tw, pos.y, NULL);
    if (row < 0 || row >= tw->m_topCount) return;
    /* itemEntered 真实发射点：进入新行才发射（同 XListWidget
     * itemEntered 口径；-2=尚未进入任何行，行 0 首次进入须发射）。 */
    if (row != tw->m_enteredRow) {
        tw->m_enteredRow = row;
        XTreeWidget_itemEntered_signal(tw, row);
    }
}

/** @brief 释放：拖拽手势落定（清态 + 释放抓取 + 光标恢复；对标
 *         qheaderview.cpp:2726-2731 state 复位且不发 sectionClicked
 *         ——分隔线拖拽与段点击排序语义互斥）；其余释放回落基类链
 *         （XAbstractItemView 选择释放语义保持零回退）。 */
static void VXTreeWidget_mouseReleaseEvent(XWidget* self, XEvent* event)
{
    XTreeWidget* tw = (XTreeWidget*)self;
    if (!tw) return;
    if (xtw_g_resizeOwner == tw && xtw_g_resizeSection >= 0) {
        xtw_resizeGestureEnd(tw);
        XWidget_update(self);
        XEvent_accept(event);
        return;
    }
    /* 静态取父类槽位回落（同本文件 keyPressEvent 的 XClass_Parent
     * 口径：经对象虚表再分派会回到本重载形成自递归）。 */
    XClass_Parent(XTreeView, EXWidget_MouseReleaseEvent,
                  void (*)(XWidget*, XEvent*))(self, event);
}

static void VXTreeWidget_mouseDoubleClickEvent(XWidget* self, XEvent* event)
{
    XTreeWidget* tw = (XTreeWidget*)self;
    XMouseEvent* me;
    XPoint pos;
    int row;
    if (!tw || !event) return;
    me = (XMouseEvent*)event;
    if (XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_DBL_CLICK) return;
    pos = XMouseEvent_position(me);
    if (XMouseEvent_button(me) != XMouseButton_LeftButton) {
        XEvent_ignore(event);
        return;
    }
    row = xtw_rowAtY(tw, pos.y, NULL);
    if (row >= 0) {
        XTreeWidgetItem* item = tw->m_topItems[row];
        /* itemDoubleClicked 真实发射点。 */
        XTreeWidget_itemDoubleClicked_signal(tw, row);
        /* 对标 expandsOnDoubleClick：双击指示器之外区域切换展开
         * （指示器区域已由按下事件切换，避免二次翻转）。 */
        if (tw->m_base.m_expandsOnDoubleClick &&
            !(item && item->childCount > 0 && pos.x < XTW_INDIC_HIT))
            xtw_toggleExpanded(tw, row);
        /* itemActivated 真实发射点（对标平台双击激活语义）。 */
        XTreeWidget_itemActivated_signal(tw, row);
    }
    XEvent_accept(event);
}

/** @brief 滚轮 → 滚动条步进（复扫新问题①根修）。
 *  @note  对标 QAbstractScrollArea::wheelEvent（qabstractscrollarea.cpp:
 *         1170 主导轴选条：|x|>|y| 走水平条、否则垂直条）与
 *         QScrollBar::wheelEvent（qscrollbar.cpp:475 经
 *         scrollByDelta 消费 angleDelta：offset=delta/120，
 *         setValue(value-steps)，正角度=滚向内容开头）。步距取行粒
 *         度：Qt 条目视图滚动条为 ScrollPerItem 行单位、singleStep=1
 *         行（qtreeview.cpp:3846 setSingleStep(1)、qabstractitemview.cpp:
 *         1306），库内 XScrollBar 为像素单位、singleStep 缺省 1px
 *         （XAbstractSlider.c:397）——基类 VX_asa_wheelEvent 的
 *         stepBy(steps*3) 在像素条上每格仅滚 3px（不足 1/8 行），
 *         肉眼即"滚轮无响应、滚动条只能拖"。本类覆写按 120 角度=3
 *         倍单步的库口径（XAbstractScrollArea.c:23-25）×行高直接
 *         setValue（内部钳位到 [minimum,maximum]，XAbstractSlider.c:
 *         599-602；valueChanged → scrollContentsBy → 重绘）。
 *         水平条按 Qt 横向 delta 取反口径（qabstractslider.cpp
 *         scrollByDelta: orientation==Horizontal 时 delta=-delta）。 */
static void VXTreeWidget_wheelEvent(XWidget* self, XEvent* event)
{
    XTreeWidget* tw = (XTreeWidget*)self;
    XAbstractScrollArea* area;
    XScrollBar* bar;
    XPoint delta;
    int steps;
    int value;
    if (!tw || !event || XEvent_type(event) != XEVENT_TYPE_WHEEL) return;
#if XWINDOWEVENT_ON
    delta = XWheelEvent_angleDelta((XWheelEvent*)event);
#else
    delta.x = 0;
    delta.y = 0;
#endif
    {
        /* 主导轴分派（同基类口径：dy 优先，dy==0 回落 dx）。 */
        int ay = delta.y >= 0 ? delta.y : -delta.y;
        int ax = delta.x >= 0 ? delta.x : -delta.x;
        area = (XAbstractScrollArea*)&tw->m_base.m_base;
        if (ax > ay) {
            steps = -(delta.x / 120); /* 横向取反（Qt scrollByDelta）。 */
            bar = XAbstractScrollArea_horizontalScrollBar(area);
        } else {
            steps = delta.y / 120;
            bar = XAbstractScrollArea_verticalScrollBar(area);
        }
    }
    if (steps == 0) {
        XEvent_accept(event); /* 不足一格：消费不滚动（同基类口径）。 */
        return;
    }
    if (!bar) {
        XEvent_accept(event);
        return;
    }
    value = XScrollBar_value(bar) -
            steps * 3 * xtw_effectiveRowHeight(tw);
    XScrollBar_setValue(bar, value);
    XEvent_accept(event);
}

static void VXTreeWidget_scrollContentsBy(XAbstractScrollArea* area, int dx,
                                          int dy)
{
    (void)dx;
    (void)dy;
    if (area) XWidget_update((XWidget*)area);
}

#endif /* XWIDGET_ON && XTABLEWIDGET_ON */
