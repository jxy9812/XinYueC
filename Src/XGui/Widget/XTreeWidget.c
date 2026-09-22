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
#include "XWidget_Protected.h"

#if XWIDGET_ON && XTABLEWIDGET_ON

#define XTW_HEADER_H 20
#define XTW_ROW_H 24
#define XTW_INDIC_HIT 10  /**< 顶层展开指示器命中带宽（绘制位于 x∈[2,6]）。 */

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
static void VXTreeWidget_mouseDoubleClickEvent(XWidget* self, XEvent* event);
static void VXTreeWidget_keyPressEvent(XWidget* self, XEvent* event);

static void xtwitem_freeSubtree(XTreeWidgetItem* item);

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
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseDoubleClickEvent,
                             VXTreeWidget_mouseDoubleClickEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_KeyPressEvent,
                             VXTreeWidget_keyPressEvent);
    return XVTABLE_DEFAULT;
}

void XTreeWidget_init(XTreeWidget* self, XWidget* parent,
                      XWidgetFlags flags)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XTreeView_init(&self->m_base, parent, flags);
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
    /* 当前项失效 + 选择清空的真实发射点（同 XListWidget clear 口径）。 */
    previous = XAbstractItemView_currentRow(&self->m_base.m_base);
    XAbstractItemView_setCurrentIndex(&self->m_base.m_base, -1, -1);
    if (self->m_base.m_base.m_selectionModel)
        selectionChanged = XItemSelectionModel_clear(
            self->m_base.m_base.m_selectionModel);
    if (previous != -1)
        XTreeWidget_currentItemChanged_signal(self, -1, previous);
    if (selectionChanged) XTreeWidget_itemSelectionChanged_signal(self);
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
            const char* a = XTreeWidgetItem_text_2(self->m_topItems[j]);
            const char* b = XTreeWidgetItem_text_2(self->m_topItems[j + 1]);
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
    XWidget_update((XWidget*)self);
}

/* ==================== 信号（调用即发射并返回连接句柄） ==================== */

void* XTreeWidget_itemClicked_signal(XTreeWidget* self, int row)
{
    xtw_emitRow(self, (size_t)XTreeWidget_itemClicked_signal, row);
    return (void*)(size_t)XTreeWidget_itemClicked_signal;
}

void* XTreeWidget_itemDoubleClicked_signal(XTreeWidget* self, int row)
{
    xtw_emitRow(self, (size_t)XTreeWidget_itemDoubleClicked_signal, row);
    return (void*)(size_t)XTreeWidget_itemDoubleClicked_signal;
}

void* XTreeWidget_itemPressed_signal(XTreeWidget* self, int row)
{
    xtw_emitRow(self, (size_t)XTreeWidget_itemPressed_signal, row);
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

static void xtw_drawItem(XTreeWidget* self, XTreeWidgetItem* item,
                         XPainter* painter, int depth, int* y, int maxY,
                         int topRow)
{
    XTreeView* tv = &self->m_base;
    int rh = tv->m_rowHeight > 0 ? tv->m_rowHeight : XTW_ROW_H;
    int indent = (tv->m_indentation > 0 ? tv->m_indentation : 20);
    XRect cell;
    const char* text;
    uint32_t base;
    uint32_t windowText;
    int y0 = *y;
    if (y0 >= maxY) return;
    base = xtw_color(self, XPaletteColorRole_Base);
    windowText = xtw_color(self, XPaletteColorRole_WindowText);
    cell.x = 0;
    cell.y = y0;
    cell.width = XWidget_width((XWidget*)self);
    cell.height = rh;
    if (item->parent == NULL) {
        XPainter_fillRect(painter, &cell, base);
    }
    text = XTreeWidgetItem_text_2(item);
    if (text && text[0]) {
        XPainter_setPen(painter, windowText);
        /* drawText 第 4 参是墨水色：传 0=透明，条目文本任何路径都不
         * 出字；传 palette WindowText（对标 XTableWidget，此处实现
         * 与注释曾自相矛盾——注释自称传 windowText 实为硬编码黑）。 */
        XPainter_drawText(painter, indent * depth + 12, y0 + rh - 6,
                          text, windowText);
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
        for (i = 0; i < item->childCount; ++i) {
            if (item->children[i])
                xtw_drawItem(self, item->children[i], painter, depth + 1,
                             y, maxY, topRow);
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
        XPainter_drawText(painter, x + 4, XTW_HEADER_H - 6, text,
                          0xFF444444u);
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
            int contentH = headerOffset + rows * xtw_effectiveRowHeight(tw);
            int vMax = contentH > h ? contentH - h : 0;
            if (vbar && XScrollBar_maximum(vbar) != vMax)
                XScrollBar_setRange(vbar, 0, vMax);
        }
        /* 表头带绘制于视口顶部（不随内容滚动；headerHidden 时不占位）。 */
        if (headerOffset > 0) xtw_drawHeader(tw, &painter, r.width);
        offY = xtw_scrollOffsetY(tw);
        if (offY != 0)
            XPainter_translate(&painter, 0.0f, -(float)offY);
        if (headerOffset != 0)
            XPainter_translate(&painter, 0.0f, (float)headerOffset);
        y = 0;
        for (i = 0; i < tw->m_topCount; ++i) {
            if (tw->m_topItems[i]) {
                /* 行绘制下限同步下移（跳过视口上方内容；行带下移表头
                 * 高度，内容坐标下限相应收窄）。 */
                xtw_drawItem(tw, tw->m_topItems[i], &painter, 0, &y,
                             h + offY - headerOffset, i);
            }
        }
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
    /* 命中：按展开态几何反查平铺顶层行（子树行随顶层行显隐）。 */
    row = xtw_rowAtY(tw, pos.y, NULL);
    if (row >= 0) {
        XTreeWidgetItem* item = tw->m_topItems[row];
        if (item && item->childCount > 0 && pos.x < XTW_INDIC_HIT) {
            /* 展开指示器：切换展开并发射 itemExpanded/itemCollapsed。 */
            xtw_toggleExpanded(tw, row);
        } else {
            /* itemPressed/itemClicked 真实发射点（先按压后点击，
             * 同 XTableWidget 约定；选中经 xtw_setCurrentRow 联动）。 */
            xtw_setCurrentRow(tw, row);
            XTreeWidget_itemPressed_signal(tw, row);
            XTreeWidget_itemClicked_signal(tw, row);
        }
    }
    XEvent_accept(event);
}

/** @brief 移动：进入新顶层行发射 itemEntered（m_enteredRow 差分判
 *         重；命中走 xtw_rowAtY——无模型便利类的展开态几何，与点击
 *         命中同口径，基类 indexAt 的模型行数校验不适用）。 */
static void VXTreeWidget_mouseMoveEvent(XWidget* self, XEvent* event)
{
    XTreeWidget* tw = (XTreeWidget*)self;
    XMouseEvent* me;
    XPoint pos;
    int row;
    if (!tw) return;
    /* 基类移动路径：发射 XAbstractItemView entered 抽象信号。 */
    XClass_Parent(XTreeView, EXWidget_MouseMoveEvent,
                  void (*)(XWidget*, XEvent*))(self, event);
    if (!event || XEvent_type(event) != XEVENT_TYPE_MOUSE_MOVE) return;
    me = (XMouseEvent*)event;
    pos = XMouseEvent_position(me);
    row = xtw_rowAtY(tw, pos.y, NULL);
    if (row < 0 || row >= tw->m_topCount) return;
    /* itemEntered 真实发射点：进入新行才发射（同 XListWidget
     * itemEntered 口径；-2=尚未进入任何行，行 0 首次进入须发射）。 */
    if (row != tw->m_enteredRow) {
        tw->m_enteredRow = row;
        XTreeWidget_itemEntered_signal(tw, row);
    }
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

static void VXTreeWidget_scrollContentsBy(XAbstractScrollArea* area, int dx,
                                          int dy)
{
    (void)dx;
    (void)dy;
    if (area) XWidget_update((XWidget*)area);
}

#endif /* XWIDGET_ON && XTABLEWIDGET_ON */
