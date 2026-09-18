/**
 * @file       XListWidget.c
 * @brief      XListWidget 列表控件实现（内建 model 桥 + 条目便捷族 +
 *             行级部件挂载）。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XListWidget.h"

#include "XAlgorithm.h"
#include "XMemory.h"
#include "XVarList.h"
#include "XEventType.h"
#include "XEvent.h"

#if XWIDGET_ON && XTABLEWIDGET_ON

#define XLW_DEFAULT_ROW_H 24

static void VXListWidget_deinit(XListWidget* self);
static void VXListWidget_mousePressEvent(XWidget* self, XEvent* event);
static void VXListWidget_mouseDoubleClickEvent(XWidget* self,
                                               XEvent* event);
static void VXListWidget_mouseMoveEvent(XWidget* self, XEvent* event);
static void VXListWidget_keyPressEvent(XWidget* self, XEvent* event);

/** @brief 查询绘制/命中槽位高（网格高启用且大于行高时取网格高，
 *         与 XListView 自绘一致）。 */
static int xlw_slotHeight(const XListWidget* self)
{
    int rh = (self->m_base.m_rowHeight > 0) ? self->m_base.m_rowHeight
                                            : XLW_DEFAULT_ROW_H;
    if (self->m_base.m_gridHeight > rh) return self->m_base.m_gridHeight;
    return rh;
}

/** @brief 查询绘制/命中槽位宽（网格宽启用且小于控件宽时收缩）。 */
static int xlw_slotWidth(const XListWidget* self)
{
    int w = XWidget_width((XWidget*)self);
    if (self->m_base.m_gridWidth > 0 && self->m_base.m_gridWidth < w)
        return self->m_base.m_gridWidth;
    return w;
}

/** @brief 确保行级部件表容量 >= need（倍增扩容，新增区域清零；
 *         失败保持原容量，后续挂载入口按需重试）。 */
static void xlw_ensureRowWidgets(XListWidget* self, int need)
{
    XWidget** grown;
    int cap;
    if (!self || need <= 0) return;
    if (need <= self->m_rowWidgetCapacity) return;
    cap = self->m_rowWidgetCapacity > 0 ? self->m_rowWidgetCapacity : 4;
    while (cap < need) cap *= 2;
    grown = (XWidget**)XRealloc_System(
        self->m_rowWidgets, sizeof(XWidget*) * (size_t)cap);
    if (!grown) return;
    XMemset(grown + self->m_rowWidgetCapacity, 0,
            sizeof(XWidget*) * (size_t)(cap - self->m_rowWidgetCapacity));
    self->m_rowWidgets = grown;
    self->m_rowWidgetCapacity = cap;
}

/** @brief 行级部件表自 row 起整体前移一位并清尾（行移除同步；
 *         部件为借用，只平移指针不删除部件本体）。 */
static void xlw_rowWidgetsRemoveAt(XListWidget* self, int row, int rows)
{
    int i;
    int last;
    if (!self->m_rowWidgets || row < 0 || row >= self->m_rowWidgetCapacity)
        return;
    last = rows - 1;
    for (i = row; i < last && i + 1 < self->m_rowWidgetCapacity; ++i)
        self->m_rowWidgets[i] = self->m_rowWidgets[i + 1];
    if (last < self->m_rowWidgetCapacity) self->m_rowWidgets[last] = NULL;
}

/** @brief 滚动使指定行可见（EnsureVisible 语义；几何取
 *         XListWidget_visualItemRect，滚动范围由滚动条自身收敛）。 */
static void xlw_scrollRowVisible(XListWidget* self, int row)
{
    XAbstractScrollArea* area =
        (XAbstractScrollArea*)&self->m_base.m_base.m_base;
    XScrollBar* vbar = XAbstractScrollArea_verticalScrollBar(area);
    XWidget* viewport = XAbstractScrollArea_viewport(area);
    XRect r;
    int visibleH;
    int value;
    int target;
    if (!vbar) return;
    visibleH = viewport ? XWidget_height(viewport) : 0;
    if (visibleH <= 0) return;
    r = XListWidget_visualItemRect(self, row);
    value = XScrollBar_value(vbar);
    target = value;
    if (r.y < value)
        target = r.y;
    else if (r.y + r.height > value + visibleH)
        target = r.y + r.height - visibleH;
    if (target != value) XScrollBar_setValue(vbar, target);
}

/** @brief 条目文本比较（NULL 视为空串，参与排序且最小；字典序）。 */
static int xlw_compareCells(const XString* a, const XString* b)
{
    if (a == b) return 0;
    if (!a) return -1;
    if (!b) return 1;
    return (int)XString_compare(a, b);
}

/** @brief 行移除后的选择模型联动（选择变化路径；用于收敛
 *         itemSelectionChanged 发射点）：取消被移除行的选中，
 *         其后选中行整体前移一位。
 * @param self 目标控件。
 * @param row 被移除行号。
 * @param rows 移除前行数。
 * @return 选择集合有变化返回 true。
 * @note 升序遍历使移动目标（i-1）始终位于未处理区间之前，源选中
 *       状态不被先行改写；选择模型自身的 selectionChanged 随每次
 *       select 独立发射，本控件级 itemSelectionChanged 依据返回值
 *       收敛为一次。
 */
static bool xlw_selectionShiftAfterRemove(XListWidget* self, int row,
                                          int rows)
{
    XItemSelectionModel* selection = self->m_base.m_base.m_selectionModel;
    bool changed = false;
    int i;
    if (!selection) return false;
    if (XItemSelectionModel_isSelected(selection, row, 0)) {
        XItemSelectionModel_select(selection, row, 0, false);
        changed = true;
    }
    for (i = row + 1; i < rows; ++i) {
        if (XItemSelectionModel_isSelected(selection, i, 0)) {
            XItemSelectionModel_select(selection, i, 0, false);
            XItemSelectionModel_select(selection, i - 1, 0, true);
            changed = true;
        }
    }
    return changed;
}

/** @brief 发射单 int 载荷信号（行号族；无监听不分配载荷）。 */
static void xlw_emitRow(XListWidget* self, size_t signal, int row)
{
    XVarList* args;
    if (!self || !((XObject*)self)->m_signalSlot) return;
    args = XVarList_Create(XVar(int, row));
    if (!args) return;
    XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                       XEVENT_PRIORITY_NORMAL);
}

/** @brief 当前行变化成对发射：currentItemChanged + currentRowChanged +
 *         currentTextChanged（新当前行文本；无当前项为 ""）。 */
static void xlw_emitCurrentChanged(XListWidget* self, int row, int previous)
{
    XListWidget_currentItemChanged_signal(self, row, previous);
    XListWidget_currentRowChanged_signal(self, row, previous);
    XListWidget_currentTextChanged_signal(
        self, row >= 0 ? XListWidget_item_2(self, row) : "");
}

/** @brief 内建模型 dataChanged(row,col) → itemChanged(row) 桥接槽
 *         （init 时连接；仅列 0 且非内部批量改写期转发）。 */
static void xlw_modelDataChangedSlot(XObject* receiver, XVarList* args)
{
    XListWidget* self = (XListWidget*)receiver;
    int row;
    int col;
    if (!self || !args) return;
    row = XVarList_arg(args, int);
    col = XVarList_arg(args, int);
    /* 内部批量改写（插入平移/取出前移/排序写回）不逐行发
     * itemChanged；外部经模型 setData 的真实数据变化按列 0 定位。 */
    if (self->m_dataGuard) return;
    if (col == 0 && row >= 0 && row < XListWidget_count(self))
        XListWidget_itemChanged_signal(self, row);
}

XVtable* XListWidget_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XListWidget)
    XVTABLE_INHERIT_XCLASS(XListView);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXListWidget_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MousePressEvent,
                             VXListWidget_mousePressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseDoubleClickEvent,
                             VXListWidget_mouseDoubleClickEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseMoveEvent,
                             VXListWidget_mouseMoveEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_KeyPressEvent,
                             VXListWidget_keyPressEvent);
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
        /* itemChanged(row) 数据路径对接：内建模型 dataChanged 桥接
         * （新建模型 m_signalSlot 为空，此处取信号地址不会误发射）。 */
        XObject_connect_1(
            (XObject*)self->m_model,
            (size_t)XAbstractItemModel_dataChanged_signal(self->m_model, 0,
                                                          0),
            (XObject*)self, xlw_modelDataChangedSlot,
            XConnectionType_Direct);
    }
    /* itemEntered 差分基准：-2=尚未进入任何行（行 0 首次进入须发射）。 */
    self->m_enteredRow = -2;
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
    /* 行级部件为借用：deinit 全路径只释放承载表，不删除部件本体。 */
    if (self->m_rowWidgets) {
        XFree_System(self->m_rowWidgets);
        self->m_rowWidgets = NULL;
    }
    self->m_rowWidgetCapacity = 0;
    if (self->m_model) {
        XAbstractItemModel_delete_base(self->m_model);
        self->m_model = NULL;
    }
    XClass_Deinit_Parent(XListView, (XListView*)self);
}

int XListWidget_addItem(XListWidget* self, const XString* text)
{
    if (!self || !self->m_model || !text) return -1;
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

int XListWidget_addItems(XListWidget* self, const char* const* texts,
                         int count)
{
    int added = 0;
    int i;
    if (!self || !texts || count <= 0) return 0;
    for (i = 0; i < count; ++i) {
        /* 元素 NULL 视为空文本行（与 XTreeWidget 批量族一致）。 */
        if (XListWidget_addItem_2(self, texts[i] ? texts[i] : "") < 0)
            break;
        ++added;
    }
    return added;
}

int XListWidget_insertItem(XListWidget* self, int row, const XString* text)
{
    int rows;
    int i;
    if (!self || !self->m_model || !text) return -1;
    rows = XAbstractItemModel_rowCount(self->m_model);
    if (row < 0) row = 0;
    if (row > rows) row = rows;
    /* 真实行内插入：先扩一行，原 row 及其后条目整体后移（自尾向头
     * 平移，读 i-1 写 i 不互相覆盖），再写入新行文本。批量改写置
     * 数据保护（内部平移不逐行发 itemChanged）。 */
    self->m_dataGuard = true;
    XAbstractItemModel_setDimension(self->m_model, rows + 1, 1);
    for (i = rows; i > row; --i) {
        const XString* carry =
            XAbstractItemModel_data(self->m_model, i - 1, 0);
        XAbstractItemModel_setData(self->m_model, i, 0, carry);
    }
    if (!XAbstractItemModel_setData(self->m_model, row, 0, text)) {
        XAbstractItemModel_setDimension(self->m_model, rows, 1);
        self->m_dataGuard = false;
        return -1;
    }
    self->m_dataGuard = false;
    /* 部件表同步：row 位起整体后移腾位（借用指针平移）。 */
    xlw_ensureRowWidgets(self, rows + 1);
    if (self->m_rowWidgets && row < self->m_rowWidgetCapacity) {
        int top = rows < self->m_rowWidgetCapacity
                      ? rows : self->m_rowWidgetCapacity - 1;
        for (i = top; i > row; --i)
            self->m_rowWidgets[i] = self->m_rowWidgets[i - 1];
        self->m_rowWidgets[row] = NULL;
    }
    /* 排序使能开启：插入条目按当前排序序自动排序（对标 sortingEnabled
     * 语义；返回值为插入时行号，自动排序后实际行号可能变化）。 */
    if (self->m_sortingEnabled)
        XListWidget_sortItems(self, self->m_sortOrder);
    else
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

int XListWidget_insertItems(XListWidget* self, int index,
                            const char* const* texts, int count)
{
    int added = 0;
    int i;
    int rows;
    if (!self || !texts || count <= 0) return 0;
    /* 收敛口径同 insertItem：负数最前、超出当前行数追加。 */
    rows = XListWidget_count(self);
    if (index < 0) index = 0;
    if (index > rows) index = rows;
    for (i = 0; i < count; ++i) {
        /* 元素 NULL 视为空文本行（与 addItems 批量族一致）。 */
        if (XListWidget_insertItem_2(self, index + added,
                                     texts[i] ? texts[i] : "") < 0)
            break;
        ++added;
    }
    return added;
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

XString* XListWidget_item_new(const XListWidget* self, int row)
{
    const XString* cell;
    if (!self || row < 0 || row >= XListWidget_count(self)) return NULL;
    cell = XListWidget_item(self, row);
    /* 新建副本：与模型文本解耦（行文本为空返回空串对象）。 */
    return cell ? XString_create_copy(cell) : XString_create();
}

XString* XListWidget_takeItem(XListWidget* self, int row)
{
    int rows;
    int i;
    int previous;
    const XString* cell;
    XString* out;
    if (!self || !self->m_model) return NULL;
    rows = XAbstractItemModel_rowCount(self->m_model);
    if (row < 0 || row >= rows) return NULL;
    cell = XAbstractItemModel_data(self->m_model, row, 0);
    /* 文本所有权转移：返回模型文本的堆副本，由调用方释放。 */
    out = cell ? XString_create_copy(cell) : XString_create();
    if (!out) return NULL;
    /* 其后条目整体前移（读 i+1 写 i），再收缩尾行（批量改写置数据
     * 保护：内部平移不逐行发 itemChanged）。 */
    self->m_dataGuard = true;
    for (i = row; i < rows - 1; ++i) {
        const XString* carry =
            XAbstractItemModel_data(self->m_model, i + 1, 0);
        XAbstractItemModel_setData(self->m_model, i, 0, carry);
    }
    XAbstractItemModel_setDimension(self->m_model, rows - 1, 1);
    self->m_dataGuard = false;
    /* 部件表同步：借用指针前移并清尾（不删除部件本体）。 */
    xlw_rowWidgetsRemoveAt(self, row, rows);
    /* 选择联动：被移除行取消选中、其后选中行前移；变化时发射
     * itemSelectionChanged（选择变化路径真实发射点）。 */
    if (xlw_selectionShiftAfterRemove(self, row, rows))
        XListWidget_itemSelectionChanged_signal(self);
    /* 当前行联动：当前项被移除则失效；当前行在被移除行之后则前移
     * 一位（逻辑当前条目随行平移），两者均成对发射当前项族信号。 */
    previous = self->m_base.m_base.m_currentRow;
    if (previous == row) {
        XAbstractItemView_setCurrentIndex(&self->m_base.m_base, -1, -1);
        xlw_emitCurrentChanged(self, -1, previous);
    } else if (previous > row) {
        XAbstractItemView_setCurrentIndex(&self->m_base.m_base,
                                          previous - 1, 0);
        xlw_emitCurrentChanged(self, previous - 1, previous);
    }
    XWidget_update((XWidget*)self);
    return out;
}

void XListWidget_clear(XListWidget* self)
{
    XItemSelectionModel* selection;
    int previous;
    bool selectionChanged = false;
    if (!self || !self->m_model) return;
    selection = self->m_base.m_base.m_selectionModel;
    previous = self->m_base.m_base.m_currentRow;
    XAbstractItemModel_setDimension(self->m_model, 0, 1);
    /* 选择联动：全部选中随条目清空（变化时发射 itemSelectionChanged）。 */
    if (selection) selectionChanged = XItemSelectionModel_clear(selection);
    /* 当前行随条目清空失效（与 XTreeWidget_clear 同一复位模式）。 */
    if (previous >= 0)
        XAbstractItemView_setCurrentIndex(&self->m_base.m_base, -1, -1);
    /* 行级部件为借用：只释放承载表（部件本体由调用方管理）。 */
    if (self->m_rowWidgets) {
        XFree_System(self->m_rowWidgets);
        self->m_rowWidgets = NULL;
    }
    self->m_rowWidgetCapacity = 0;
    /* 真实发射点：清空使当前项失效（成对发射当前项族信号）、选择
     * 集合清空。 */
    if (previous >= 0) xlw_emitCurrentChanged(self, -1, previous);
    if (selectionChanged) XListWidget_itemSelectionChanged_signal(self);
    XWidget_update((XWidget*)self);
}

int XListWidget_currentRow(const XListWidget* self)
{
    return self ? self->m_base.m_base.m_currentRow : -1;
}

void XListWidget_setCurrentRow(XListWidget* self, int row)
{
    XItemSelectionModel* selection;
    int previous;
    bool selectionChanged = false;
    if (!self) return;
    /* 越界忽略（row 负数=清除当前项与选中）。 */
    if (row >= XListWidget_count(self)) return;
    selection = self->m_base.m_base.m_selectionModel;
    previous = self->m_base.m_base.m_currentRow;
    XAbstractItemView_setCurrentIndex(&self->m_base.m_base, row, 0);
    if (selection) {
        if (row >= 0) {
            /* 选择联动（行模型单选语义）：当前行写入选择模型。 */
            selectionChanged =
                XItemSelectionModel_select(selection, row, 0, true);
            XItemSelectionModel_setCurrentIndex(selection, row, 0);
        } else {
            /* row<0：清除当前项时同步清空选中（对标 Qt
             * setCurrentRow(-1) 的 ClearAndSelect 语义）。 */
            selectionChanged = XItemSelectionModel_clear(selection);
        }
    }
    /* 真实发射点：当前行变化成对发射 currentRowChanged/
     * currentTextChanged（及 currentItemChanged）；选择集合变化发射
     * itemSelectionChanged。 */
    if (row != previous) xlw_emitCurrentChanged(self, row, previous);
    if (selectionChanged) XListWidget_itemSelectionChanged_signal(self);
    XWidget_update((XWidget*)self);
}

void XListWidget_scrollToItem(XListWidget* self, int row)
{
    if (!self || row < 0 || row >= XListWidget_count(self)) return;
    /* 与 setCurrentItem 同一滚动机制（EnsureVisible 语义）。 */
    xlw_scrollRowVisible(self, row);
}

int XListWidget_selectedItems(const XListWidget* self, int* outRows,
                              int maxCount)
{
    const XItemSelectionModel* selection;
    int rows;
    int i;
    int found = 0;
    if (!self || !outRows || maxCount <= 0) return 0;
    selection = self->m_base.m_base.m_selectionModel;
    rows = XListWidget_count(self);
    for (i = 0; i < rows && found < maxCount; ++i) {
        if (XItemSelectionModel_isSelected(selection, i, 0))
            outRows[found++] = i;
    }
    return found;
}

int XListWidget_currentItem(const XListWidget* self)
{
    int row = -1;
    int col = -1;
    if (!self) return -1;
    if (!XAbstractItemView_currentIndex(&self->m_base.m_base, &row, &col))
        return -1;
    if (row < 0 || row >= XListWidget_count(self)) return -1;
    return row;
}

void XListWidget_setCurrentItem(XListWidget* self, int row)
{
    if (!self || row < 0 || row >= XListWidget_count(self)) return;
    XListWidget_setCurrentRow(self, row);
    XListWidget_scrollToItem(self, row);
}

int XListWidget_findItems(const XListWidget* self, const char* text,
                          int flags, int* outRows, int maxCount)
{
    int rows;
    int i;
    int found = 0;
    if (!self || !self->m_model || !text || !outRows || maxCount <= 0)
        return 0;
    rows = XAbstractItemModel_rowCount(self->m_model);
    for (i = 0; i < rows && found < maxCount; ++i) {
        const XString* cell = XAbstractItemModel_data(self->m_model, i, 0);
        bool hit = false;
        if (!cell) continue;
        /* flags 位 1：精确相等；否则包含子串（均区分大小写）。 */
        if (flags & 1)
            hit = XString_equals_utf8(cell, text, XChar_CaseSensitive);
        else
            hit = XString_contains_utf8(cell, text, XChar_CaseSensitive);
        if (hit) outRows[found++] = i;
    }
    return found;
}

void XListWidget_sortItems(XListWidget* self, int order)
{
    int rows;
    int i;
    int j;
    const XString** texts;
    XWidget** widgets;
    if (!self || !self->m_model) return;
    if (order != 0 && order != 1) return;
    /* 对标 Qt sortItems:显式排序隐式开启 sortingEnabled。 */
    self->m_sortingEnabled = true;
    rows = XAbstractItemModel_rowCount(self->m_model);
    if (rows <= 1) return;
    /* 承载快照：模型文本与挂载部件均为借用指针，排序只重排指针。 */
    texts = (const XString**)XMalloc_System(
        sizeof(XString*) * (size_t)rows);
    widgets = (XWidget**)XMalloc_System(sizeof(XWidget*) * (size_t)rows);
    if (!texts || !widgets) {
        if (texts) XFree_System(texts);
        if (widgets) XFree_System(widgets);
        return;
    }
    for (i = 0; i < rows; ++i) {
        texts[i] = XAbstractItemModel_data(self->m_model, i, 0);
        widgets[i] = (self->m_rowWidgets && i < self->m_rowWidgetCapacity)
                         ? self->m_rowWidgets[i] : NULL;
    }
    /* 稳定插入排序：升序右移较大者、降序右移较小者，相等不动。 */
    for (i = 1; i < rows; ++i) {
        const XString* keyText = texts[i];
        XWidget* keyWidget = widgets[i];
        j = i - 1;
        while (j >= 0) {
            int cmp = xlw_compareCells(texts[j], keyText);
            if (order == 0 ? cmp <= 0 : cmp >= 0) break;
            texts[j + 1] = texts[j];
            widgets[j + 1] = widgets[j];
            --j;
        }
        texts[j + 1] = keyText;
        widgets[j + 1] = keyWidget;
    }
    /* 写回：先深拷贝排序后的文本快照(避免逐行 setData 替换模型内部
     * XString 后借用指针失效),再逐行 setData + 部件表指针重排；
     * 批量改写置数据保护(内部重排不逐行发 itemChanged)。 */
    {
        char** snapshot = (char**)XMalloc_System(sizeof(char*) * (size_t)rows);
        if (!snapshot) {
            XFree_System(texts);
            XFree_System(widgets);
            return;
        }
        for (i = 0; i < rows; ++i) {
            size_t len = texts[i] ? XStrlen(XString_toUtf8(texts[i])) : 0;
            snapshot[i] = (char*)XMalloc_System(len + 1);
            if (snapshot[i]) {
                if (texts[i]) XMemcpy(snapshot[i], XString_toUtf8(texts[i]), len + 1);
                else snapshot[i][0] = '\0';
            }
        }
        self->m_dataGuard = true;
        for (i = 0; i < rows; ++i) {
            XAbstractItemModel_setData_2(self->m_model, i, 0, snapshot[i]);
            if (self->m_rowWidgets && i < self->m_rowWidgetCapacity)
                self->m_rowWidgets[i] = widgets[i];
        }
        for (i = 0; i < rows; ++i) XFree_System(snapshot[i]);
        XFree_System(snapshot);
    }
    XFree_System(texts);
    XFree_System(widgets);
    /* 排序序记录（setSortingEnabled(true) 立即排序按此序收敛）。 */
    self->m_sortOrder = order;
    XWidget_update((XWidget*)self);
}

bool XListWidget_isSortingEnabled(const XListWidget* self)
{ return self ? self->m_sortingEnabled : false; }

void XListWidget_setSortingEnabled(XListWidget* self, bool enable)
{
    if (!self) return;
    if (self->m_sortingEnabled == enable) return;
    self->m_sortingEnabled = enable;
    /* 开启即按当前排序序（最近 sortItems 记录，默认升序）排序一次，
     * 对标 Qt setSortingEnabled(true) 的立即排序语义。 */
    if (enable) XListWidget_sortItems(self, self->m_sortOrder);
}

int XListWidget_itemAt(const XListWidget* self, int x, int y)
{
    int row = -1;
    int col = -1;
    if (!self) return -1;
    /* 经基类虚槽分派到 XListView 命中实现（行高/间距/网格/行隐藏
     * 与绘制一致）。 */
    if (!XAbstractItemView_indexAt_base(&self->m_base.m_base, x, y,
                                        &row, &col))
        return -1;
    return row;
}

XRect XListWidget_visualItemRect(const XListWidget* self, int row)
{
    XRect r;
    int slotH;
    int y;
    int i;
    XRect_init(&r, 0, 0, 0, 0);
    if (!self || row < 0 || row >= XListWidget_count(self)) return r;
    slotH = xlw_slotHeight(self);
    /* 与自绘一致：y 为该行之前可见行的槽高+间距累加（隐藏行不占位）。 */
    y = 0;
    for (i = 0; i < row; ++i) {
        if (XListView_isRowHidden(&self->m_base, i)) continue;
        y += slotH + self->m_base.m_spacing;
    }
    r.x = 0;
    r.y = y;
    r.width = xlw_slotWidth(self);
    r.height = slotH;
    return r;
}

/* ==================== 索引反查/编辑（对标 QListWidget） ==================== */

int XListWidget_indexFromItem(const XListWidget* self, int row)
{
    /* 单列行模型：行号即索引（恒等映射，无独立 QModelIndex）。 */
    if (!self || row < 0 || row >= XListWidget_count(self)) return -1;
    return row;
}

const XString* XListWidget_itemFromIndex(const XListWidget* self, int row)
{
    /* 与 item(row) 同一承载（恒等映射的对称 API）。 */
    return XListWidget_item(self, row);
}

int XListWidget_row(const XListWidget* self, const char* text)
{
    int rows;
    int i;
    if (!self || !self->m_model || !text) return -1;
    rows = XAbstractItemModel_rowCount(self->m_model);
    for (i = 0; i < rows; ++i) {
        const XString* cell = XAbstractItemModel_data(self->m_model, i, 0);
        /* 精确相等（区分大小写），返回首个命中行。 */
        if (cell && XString_equals_utf8(cell, text, XChar_CaseSensitive))
            return i;
    }
    return -1;
}

void XListWidget_editItem(XListWidget* self, int row)
{
    /* 句柄预留：无编辑器/委托机制，编辑触发无从落地（见 @note）；
     * 仅做入参校验。编辑路径建立后应在此发射 itemChanged(row)。 */
    if (!self || row < 0 || row >= XListWidget_count(self)) return;
}

void XListWidget_setItemWidget(XListWidget* self, int row, XWidget* widget)
{
    if (!self || row < 0 || row >= XListWidget_count(self)) return;
    if (!widget) {
        XListWidget_removeItemWidget(self, row);
        return;
    }
    xlw_ensureRowWidgets(self, XListWidget_count(self));
    if (!self->m_rowWidgets || row >= self->m_rowWidgetCapacity) return;
    self->m_rowWidgets[row] = widget; /* 借用：列表不取得所有权。 */
    XWidget_update((XWidget*)self);
}

XWidget* XListWidget_itemWidget(const XListWidget* self, int row)
{
    if (!self || row < 0 || row >= XListWidget_count(self)) return NULL;
    if (!self->m_rowWidgets || row >= self->m_rowWidgetCapacity) return NULL;
    return self->m_rowWidgets[row];
}

void XListWidget_removeItemWidget(XListWidget* self, int row)
{
    if (!self || row < 0 || row >= XListWidget_count(self)) return;
    if (!self->m_rowWidgets || row >= self->m_rowWidgetCapacity) return;
    if (!self->m_rowWidgets[row]) return;
    self->m_rowWidgets[row] = NULL; /* 置 NULL；不删除部件。 */
    XWidget_update((XWidget*)self);
}

XAbstractItemModel* XListWidget_model(const XListWidget* self)
{ return self ? self->m_model : NULL; }

/* ==================== 事件（叠加行号信号发射，基类行为保留） ==================== */

/** @brief 按下：基类命中选中后按行号发射 itemPressed/itemClicked；
 *         当前行随点击变化时补发 currentRowChanged/currentTextChanged。 */
static void VXListWidget_mousePressEvent(XWidget* self, XEvent* event)
{
    XListWidget* lw = (XListWidget*)self;
    XMouseEvent* me;
    XPoint pos;
    int previous;
    int row;
    int col;
    if (!lw) return;
    previous = lw->m_base.m_base.m_currentRow;
    /* 基类按下路径（XListView 虚槽承载 XAbstractItemView 实现）：
     * 命中行写入当前索引/选择模型并发射抽象 pressed(row,col)。 */
    XClass_Parent(XListView, EXWidget_MousePressEvent,
                  void (*)(XWidget*, XEvent*))(self, event);
    if (!event || XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_PRESS)
        return;
    me = (XMouseEvent*)event;
    if (XMouseEvent_button(me) != XMouseButton_LeftButton) return;
    pos = XMouseEvent_position(me);
    if (!XAbstractItemView_indexAt_base(&lw->m_base.m_base, pos.x, pos.y,
                                        &row, &col))
        return;
    if (row < 0 || row >= XListWidget_count(lw)) return;
    /* 当前行随点击变化：补发射 currentRowChanged/currentTextChanged。 */
    if (row != previous) xlw_emitCurrentChanged(lw, row, previous);
    /* itemPressed/itemClicked 真实发射点（先按压后点击，同
     * XTreeWidget/XTableWidget 约定）。 */
    XListWidget_itemPressed_signal(lw, row);
    XListWidget_itemClicked_signal(lw, row);
}

/** @brief 双击：基类抽象 doubleClicked 后按行号发射
 *         itemDoubleClicked/itemActivated（双击激活语义）。 */
static void VXListWidget_mouseDoubleClickEvent(XWidget* self, XEvent* event)
{
    XListWidget* lw = (XListWidget*)self;
    XMouseEvent* me;
    XPoint pos;
    int row;
    int col;
    if (!lw) return;
    /* 基类双击路径：发射 XAbstractItemView doubleClicked 抽象信号。 */
    XClass_Parent(XListView, EXWidget_MouseDoubleClickEvent,
                  void (*)(XWidget*, XEvent*))(self, event);
    if (!event || XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_DBL_CLICK)
        return;
    me = (XMouseEvent*)event;
    if (XMouseEvent_button(me) != XMouseButton_LeftButton) return;
    pos = XMouseEvent_position(me);
    if (!XAbstractItemView_indexAt_base(&lw->m_base.m_base, pos.x, pos.y,
                                        &row, &col))
        return;
    if (row < 0 || row >= XListWidget_count(lw)) return;
    /* itemDoubleClicked/itemActivated 真实发射点（对标 Qt 平台双击
     * 激活语义，同 XTreeWidget 约定）。 */
    XListWidget_itemDoubleClicked_signal(lw, row);
    XListWidget_itemActivated_signal(lw, row);
}

/** @brief 移动：进入新行发射 itemEntered（m_enteredRow 差分判重）。 */
static void VXListWidget_mouseMoveEvent(XWidget* self, XEvent* event)
{
    XListWidget* lw = (XListWidget*)self;
    XMouseEvent* me;
    XPoint pos;
    int row;
    int col;
    if (!lw) return;
    /* 基类移动路径：发射 XAbstractItemView entered 抽象信号。 */
    XClass_Parent(XListView, EXWidget_MouseMoveEvent,
                  void (*)(XWidget*, XEvent*))(self, event);
    if (!event || XEvent_type(event) != XEVENT_TYPE_MOUSE_MOVE) return;
    me = (XMouseEvent*)event;
    pos = XMouseEvent_position(me);
    if (!XAbstractItemView_indexAt_base(&lw->m_base.m_base, pos.x, pos.y,
                                        &row, &col))
        return;
    if (row < 0 || row >= XListWidget_count(lw)) return;
    /* itemEntered 真实发射点：进入新行才发射（同 XTableWidget
     * cellEntered 口径；事件投递依赖窗口层移动事件派发）。 */
    if (row != lw->m_enteredRow) {
        lw->m_enteredRow = row;
        XListWidget_itemEntered_signal(lw, row);
    }
}

/** @brief 键盘：基类 Enter 激活/键盘搜索后对 Enter/Return 补发
 *         itemActivated（键盘激活真实发射点）。 */
static void VXListWidget_keyPressEvent(XWidget* self, XEvent* event)
{
    XListWidget* lw = (XListWidget*)self;
    int key;
    int row;
    if (!lw) return;
    /* 基类键盘路径：Enter/Return 发射抽象 activated、可打印字符转入
     * 键盘搜索。 */
    XClass_Parent(XListView, EXWidget_KeyPressEvent,
                  void (*)(XWidget*, XEvent*))(self, event);
    if (!event || XEvent_type(event) != XEVENT_TYPE_KEY_PRESS) return;
    key = XKeyEvent_key((XKeyEvent*)event);
    if (key != XKey_Return && key != XKey_Enter) return;
    row = lw->m_base.m_base.m_currentRow;
    if (row < 0 || row >= XListWidget_count(lw)) return;
    /* itemActivated 键盘激活真实发射点（对标 QAbstractItemView
     * Enter/Return 激活语义）。 */
    XListWidget_itemActivated_signal(lw, row);
}

/* ==================== 信号（对标 QListWidget） ==================== */

void* XListWidget_currentItemChanged_signal(XListWidget* self, int current,
                                            int previous)
{
    /* 载荷 (current, previous)：有监听时经事件队列发射（载荷所有权
     * 随事件转移）；无监听直接返回信号地址作连接句柄。 */
    if (self && ((XObject*)self)->m_signalSlot) {
        XVarList* args =
            XVarList_Create(XVar(int, current), XVar(int, previous));
        if (args) {
            XObject_emitSignal((XObject*)self,
                               (size_t)XListWidget_currentItemChanged_signal,
                               args, NULL, NULL, XEVENT_PRIORITY_NORMAL);
        }
    }
    return (void*)(size_t)XListWidget_currentItemChanged_signal;
}

void* XListWidget_itemSelectionChanged_signal(XListWidget* self)
{
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self,
                           (size_t)XListWidget_itemSelectionChanged_signal,
                           NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
    }
    return (void*)(size_t)XListWidget_itemSelectionChanged_signal;
}

void* XListWidget_currentRowChanged_signal(XListWidget* self, int current,
                                           int previous)
{
    /* 载荷 (current, previous)：有监听时经事件队列发射（载荷所有权
     * 随事件转移）；无监听直接返回信号地址作连接句柄。 */
    if (self && ((XObject*)self)->m_signalSlot) {
        XVarList* args =
            XVarList_Create(XVar(int, current), XVar(int, previous));
        if (args) {
            XObject_emitSignal((XObject*)self,
                               (size_t)XListWidget_currentRowChanged_signal,
                               args, NULL, NULL, XEVENT_PRIORITY_NORMAL);
        }
    }
    return (void*)(size_t)XListWidget_currentRowChanged_signal;
}

void* XListWidget_currentTextChanged_signal(XListWidget* self,
                                            const char* text)
{
    /* 载荷 text（UTF-8 借用指针，随事件投递；空文本收敛为 ""）。 */
    if (self && ((XObject*)self)->m_signalSlot) {
        XVarList* args;
        if (!text) text = "";
        args = XVarList_Create(XVar(const char*, text));
        if (args) {
            XObject_emitSignal(
                (XObject*)self,
                (size_t)XListWidget_currentTextChanged_signal, args, NULL,
                NULL, XEVENT_PRIORITY_NORMAL);
        }
    }
    return (void*)(size_t)XListWidget_currentTextChanged_signal;
}

void* XListWidget_itemActivated_signal(XListWidget* self, int row)
{
    xlw_emitRow(self, (size_t)XListWidget_itemActivated_signal, row);
    return (void*)(size_t)XListWidget_itemActivated_signal;
}

void* XListWidget_itemChanged_signal(XListWidget* self, int row)
{
    xlw_emitRow(self, (size_t)XListWidget_itemChanged_signal, row);
    return (void*)(size_t)XListWidget_itemChanged_signal;
}

void* XListWidget_itemClicked_signal(XListWidget* self, int row)
{
    xlw_emitRow(self, (size_t)XListWidget_itemClicked_signal, row);
    return (void*)(size_t)XListWidget_itemClicked_signal;
}

void* XListWidget_itemDoubleClicked_signal(XListWidget* self, int row)
{
    xlw_emitRow(self, (size_t)XListWidget_itemDoubleClicked_signal, row);
    return (void*)(size_t)XListWidget_itemDoubleClicked_signal;
}

void* XListWidget_itemEntered_signal(XListWidget* self, int row)
{
    xlw_emitRow(self, (size_t)XListWidget_itemEntered_signal, row);
    return (void*)(size_t)XListWidget_itemEntered_signal;
}

void* XListWidget_itemPressed_signal(XListWidget* self, int row)
{
    xlw_emitRow(self, (size_t)XListWidget_itemPressed_signal, row);
    return (void*)(size_t)XListWidget_itemPressed_signal;
}

#endif /* XWIDGET_ON && XTABLEWIDGET_ON */
