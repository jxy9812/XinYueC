#include "XAbstractItemView.h"

#include "XAlgorithm.h"
#include "XStringUtils.h"
#include "XDateTime.h"
#include "XMemory.h"
#include "XClass.h"
#include "XVarList.h"
#include "XEvent.h"
#include "XEventType.h"
#include "XItemDelegate.h"
#include "XCoreApplication.h"

#if XWIDGET_ON && XTABLEWIDGET_ON

static void VXAbstractItemView_deinit(XAbstractItemView* self);
static bool VXAbstractItemView_visualRect(const XAbstractItemView* self,
                                          int row, int col, XRect* out);
static void VXAbstractItemView_mousePressEvent(XWidget* self, XEvent* event);
static void VXAbstractItemView_mouseReleaseEvent(XWidget* self,
                                                 XEvent* event);
static void VXAbstractItemView_mouseDoubleClickEvent(XWidget* self,
                                                     XEvent* event);
static void VXAbstractItemView_mouseMoveEvent(XWidget* self, XEvent* event);
static void VXAbstractItemView_keyPressEvent(XWidget* self, XEvent* event);
static void VXAbstractItemView_leaveEvent(XWidget* self, XEvent* event);
static bool VXAbstractItemView_indexAt(const XAbstractItemView* self,
                                       int x, int y,
                                       int* outRow, int* outCol);

/** @brief 仅移动当前索引（不触碰选择集合；对标 Ctrl+方向键的移动）。 */
static void xaiv_setCurrentPreservingSelection(XAbstractItemView* view,
                                               int row, int col)
{
    if (!view) return;
    view->m_currentRow = row;
    view->m_currentColumn = col;
    if (view->m_selectionModel)
        XItemSelectionModel_setCurrentIndex(view->m_selectionModel, row,
                                            col);
    XWidget_update((XWidget*)view);
}

/** @brief 选中/取消一个"单元"（按 selectionBehavior 展开整行/整列）。 */
static void xaiv_selectCellExpanded(XAbstractItemView* view, int row,
                                    int col, bool selected)
{
    XItemSelectionModel* sm = view ? view->m_selectionModel : NULL;
    XAbstractItemModel* model = view ? view->m_model : NULL;
    if (!sm) return;
    if (selected &&
        view->m_selectionBehavior ==
            XAbstractItemViewSelectionBehavior_SelectRows && model) {
        int c;
        for (c = 0; c < model->m_cols; ++c)
            XItemSelectionModel_select(sm, row, c, true);
        return;
    }
    if (selected &&
        view->m_selectionBehavior ==
            XAbstractItemViewSelectionBehavior_SelectColumns && model) {
        int r;
        for (r = 0; r < model->m_rows; ++r)
            XItemSelectionModel_select(sm, r, col, true);
        return;
    }
    XItemSelectionModel_select(sm, row, col, selected);
}

/** @brief 清空选择并选中矩形区域（锚点→当前，行列归一）。 */
static void xaiv_selectRect(XAbstractItemView* view, int r1, int c1,
                            int r2, int c2)
{
    XItemSelectionModel* sm = view ? view->m_selectionModel : NULL;
    XAbstractItemModel* model = view ? view->m_model : NULL;
    int r;
    int c;
    if (!sm || !view) return;
    XItemSelectionModel_clear(sm);
    if (r1 > r2) { int t = r1; r1 = r2; r2 = t; }
    if (c1 > c2) { int t = c1; c1 = c2; c2 = t; }
    for (r = r1; r <= r2; ++r) {
        for (c = c1; c <= c2; ++c) {
            if (model && (r >= model->m_rows || c >= model->m_cols))
                continue;
            xaiv_selectCellExpanded(view, r, c, true);
        }
    }
}

static void VXAbstractItemView_mousePressEvent(XWidget* self, XEvent* event);
static void VXAbstractItemView_mouseReleaseEvent(XWidget* self,
                                                 XEvent* event);
static void VXAbstractItemView_mouseDoubleClickEvent(XWidget* self,
                                                     XEvent* event);
static void VXAbstractItemView_mouseMoveEvent(XWidget* self, XEvent* event);
static void VXAbstractItemView_keyPressEvent(XWidget* self, XEvent* event);
static bool VXAbstractItemView_indexAt(const XAbstractItemView* self,
                                       int x, int y,
                                       int* outRow, int* outCol);

/* ==================== 选择应用（键盘导航/修饰键点击共用） ==================== */

/* 平行表（持久编辑器标记/条目控件指针）使用的内存池类型：与本类堆创建
 * 默认类型一致（XAbstractItemView_create_ex 默认走该类型），分配与释放
 * 使用同一常量保证配对。 */
#define XAIV_TABLE_MEMORY XCLASS_DEFAULT_MEMORY_TYPE

/* ==================== 悬停差分旁表（entered/viewportEntered 判重） ==================== */

/* 头文件无悬停字段的替代承载：视图 → 上次悬停索引 的单链旁表
 * （对照 XListWidget m_enteredRow 差分范式，行列成对并含失效态）。
 * 节点仅随首次悬停创建、随视图析构移除（VXAbstractItemView_deinit
 * 为全路径唯一析构入口）与 LeaveEvent 复位，无泄漏与悬垂键。 */
typedef struct XAivHoverEntry
{
    struct XAivHoverEntry* m_next;   /**< 链表后继。 */
    const XAbstractItemView* m_view; /**< 键（视图借用指针）。 */
    int m_row;                       /**< 上次悬停行；-1=悬停失效（空白/未知）。 */
    int m_col;                       /**< 上次悬停列；-1=同上。 */
} XAivHoverEntry;

static XAivHoverEntry* xaiv_hoverEntries = NULL;

/** @brief 读视图的上次悬停索引；无记录时保持出参 (-1,-1)。 */
static void xaiv_hoverGet(const XAbstractItemView* view, int* row, int* col)
{
    const XAivHoverEntry* e;
    for (e = xaiv_hoverEntries; e; e = e->m_next) {
        if (e->m_view == view) {
            *row = e->m_row;
            *col = e->m_col;
            return;
        }
    }
}

/** @brief 写视图的上次悬停索引（无记录则头插建节点；分配失败忽略——
 *         下次判定仍按无记录基准，最多退化为重复发射，不误吞信号）。 */
static void xaiv_hoverSet(const XAbstractItemView* view, int row, int col)
{
    XAivHoverEntry* e;
    for (e = xaiv_hoverEntries; e; e = e->m_next) {
        if (e->m_view == view) {
            e->m_row = row;
            e->m_col = col;
            return;
        }
    }
    e = (XAivHoverEntry*)XMemory_calloc(1, sizeof(*e), XAIV_TABLE_MEMORY);
    if (!e) return;
    e->m_next = xaiv_hoverEntries;
    e->m_view = view;
    e->m_row = row;
    e->m_col = col;
    xaiv_hoverEntries = e;
}

/** @brief 移除视图的悬停记录（LeaveEvent 复位与析构路径）。 */
static void xaiv_hoverRelease(const XAbstractItemView* view)
{
    XAivHoverEntry** p = &xaiv_hoverEntries;
    while (*p) {
        if ((*p)->m_view == view) {
            XAivHoverEntry* dead = *p;
            *p = dead->m_next;
            XMemory_free(dead, XAIV_TABLE_MEMORY);
            return;
        }
        p = &(*p)->m_next;
    }
}

static void xaiv_persistentRelease(XAbstractItemView* self);
static void xaiv_indexWidgetRelease(XAbstractItemView* self);
static void xaiv_delegateRelease(XAbstractItemView* self);
static void xaiv_persistentResetTable(XAbstractItemView* self);
static void xaiv_indexWidgetResetTable(XAbstractItemView* self);
static void xaiv_roleRelease(XAbstractItemView* self);

XVtable* XAbstractItemView_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XAbstractItemView)
    XVTABLE_INHERIT_XCLASS(XAbstractScrollArea);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXAbstractItemView_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MousePressEvent,
                             VXAbstractItemView_mousePressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseReleaseEvent,
                             VXAbstractItemView_mouseReleaseEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseDoubleClickEvent,
                             VXAbstractItemView_mouseDoubleClickEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseMoveEvent,
                             VXAbstractItemView_mouseMoveEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_KeyPressEvent,
                             VXAbstractItemView_keyPressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_LeaveEvent,
                             VXAbstractItemView_leaveEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractItemView_IndexAt,
                             VXAbstractItemView_indexAt);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractItemView_VisualRect,
                             VXAbstractItemView_visualRect);
    return XVTABLE_DEFAULT;
}

void XAbstractItemView_init(XAbstractItemView* self, XWidget* parent,
                            XWidgetFlags flags)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XAbstractScrollArea_init(&self->m_base, parent, flags);
    XClassSetVtable(self, XAbstractItemView);
    self->m_currentRow = -1;
    self->m_currentColumn = -1;
    self->m_selectionMode = XAbstractItemViewSelectionMode_ExtendedSelection;
    self->m_selectionBehavior =
        XAbstractItemViewSelectionBehavior_SelectItems;
    /* 对标 Qt 6.8 QAbstractItemView::editTriggers 缺省
     * DoubleClicked|EditKeyPressed（文档：除 QTableView 外全部视图的
     * 默认值；QTableView/QTreeView 为 DoubleClicked|AnyKeyPressed）。
     * 此前多出 CurrentChanged 使单击按压即进编辑，非 Qt 默认行为。 */
    self->m_editTriggers = XAbstractItemViewEditTrigger_DoubleClicked |
        XAbstractItemViewEditTrigger_EditKeyPressed;
    self->m_alternatingRowColors = false;
    self->m_autoScroll = true;
    self->m_model = NULL;
    self->m_selectionModel = XItemSelectionModel_create();
    self->m_selectionAnchorRow = -1;
    self->m_selectionAnchorCol = -1;
    self->m_rootRow = -1;
    self->m_rootCol = -1;
    self->m_iconW = 16;
    self->m_iconH = 16;
    self->m_keyboardSearch = true;
    self->m_tabKeyNavigation = false;
    self->m_autoScrollMargin = 16;
    self->m_showDropIndicator = true;
    self->m_defaultDropAction = 0; /* 0=IgnoreAction，对标 Qt::IgnoreAction。 */
    self->m_dragDropMode = XAbstractItemViewDragDropMode_NoDragDrop;
    self->m_dragEnabled = false;
    self->m_dragDropOverwriteMode = false;
    self->m_textElideMode = XAbstractItemViewTextElideMode_ElideRight;
    self->m_verticalScrollMode =
        XAbstractItemViewScrollMode_ScrollPerItem;
    self->m_horizontalScrollMode =
        XAbstractItemViewScrollMode_ScrollPerItem;
    self->m_itemDelegate = NULL;
    self->m_persistentFlags = NULL;
    self->m_persistentRows = 0;
    self->m_persistentCols = 0;
    self->m_indexWidgets = NULL;
    self->m_indexWidgetRows = 0;
    self->m_indexWidgetCols = 0;
    self->m_columnDelegates = NULL;
    self->m_columnDelegateCount = 0;
    self->m_rowDelegates = NULL;
    self->m_rowDelegateCount = 0;
    /* 编辑闭环会话状态（对标 Qt 视图内部 editor/editorIndex 会话）。 */
    self->m_editor = NULL;
    self->m_editRow = -1;
    self->m_editCol = -1;
    self->m_editCommitted = false;
    self->m_editClosing = false;
    self->m_itemsEditable = false;
    self->m_defaultDelegate = NULL;
    self->m_roleTable = NULL;
    self->m_roleRows = 0;
    self->m_roleCols = 0;
}

XAbstractItemView* XAbstractItemView_create_ex(XMemoryType memory,
                                               XWidget* parent,
                                               XWidgetFlags flags)
{
    XAbstractItemView* self =
        (XAbstractItemView*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XAbstractItemView_init(self, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

static void VXAbstractItemView_deinit(XAbstractItemView* self)
{
    if (!self) return;
    /* 悬停差分旁表节点随视图析构移除（防悬垂键；旁表头为静态承载）。 */
    xaiv_hoverRelease(self);
    /* 平行表（持久编辑器标记/条目控件指针）随视图析构释放（全路径唯一
     * 析构入口；派生视图经 XClass_Deinit_Parent 分派至此）。 */
    xaiv_persistentRelease(self);
    xaiv_indexWidgetRelease(self);
    xaiv_delegateRelease(self);
    xaiv_roleRelease(self);
    /* 编辑器为视图子控件：随控件树统一析构（若已 deleteLater 挂起，
     * 其析构会撤销挂起事件，双路均安全）；此处仅清引用。 */
    self->m_editor = NULL;
    self->m_editRow = -1;
    self->m_editCol = -1;
    /* 默认委托为视图拥有的独立对象（无父对象）：显式析构。 */
    if (self->m_defaultDelegate) {
        XItemDelegate_delete_base(self->m_defaultDelegate);
        self->m_defaultDelegate = NULL;
    }
    if (self->m_selectionModel) {
        XItemSelectionModel_delete_base(self->m_selectionModel);
        self->m_selectionModel = NULL;
    }
    XClass_Deinit_Parent(XAbstractScrollArea, (XAbstractScrollArea*)self);
}

void XAbstractItemView_setCurrentIndex(XAbstractItemView* self, int row,
                                       int column)
{
    XItemSelectionModel* selection;
    if (!self) return;
    self->m_currentRow = row;
    self->m_currentColumn = column;
    /* SelectCurrent 语义：同步选择模型当前索引；非 NoSelection 且索引
     * 有效时同时选中该单元格（同 Qt current 变更携带 Select 命令）。 */
    selection = self->m_selectionModel;
    if (!selection) return;
    XItemSelectionModel_setCurrentIndex(selection, row, column);
    if (row >= 0 && column >= 0 &&
        self->m_selectionMode !=
            XAbstractItemViewSelectionMode_NoSelection) {
        XItemSelectionModel_select(selection, row, column, true);
    }
}

int XAbstractItemView_currentRow(const XAbstractItemView* self)
{ return self ? self->m_currentRow : -1; }
int XAbstractItemView_currentColumn(const XAbstractItemView* self)
{ return self ? self->m_currentColumn : -1; }

bool XAbstractItemView_currentIndex(const XAbstractItemView* self,
                                    int* outRow, int* outCol)
{
    if (outRow) *outRow = self ? self->m_currentRow : -1;
    if (outCol) *outCol = self ? self->m_currentColumn : -1;
    return self && self->m_currentRow >= 0 && self->m_currentColumn >= 0;
}

/* ==================== 重置与布局 ==================== */

void XAbstractItemView_reset(XAbstractItemView* self)
{
    if (!self) return;
    /* 对齐 Qt 6.8 reset：当前索引、选择（含选择模型当前索引）、根索引
     * 全部复位；持久编辑器标记与条目控件承载清空（索引随模型重建失效，
     * 同 Qt 关闭全部编辑器），平行表容量保留复用。 */
    /* 活动编辑器随 reset 收敛：放弃分支直接关闭不写模型（模型重建后
     * 原编辑坐标失效，提交无意义；对标 Qt reset/_q_modelReset 对打开
     * 编辑器的收回。setModel 路径此前已有同款收敛，此处补齐 reset
     * 路径）。 */
    if (self->m_editor)
        XAbstractItemView_closeEditor(self, self->m_editor,
                                      XItemDelegateEndEditHint_RevertModelCache);
    self->m_currentRow = -1;
    self->m_currentColumn = -1;
    self->m_rootRow = -1;
    self->m_rootCol = -1;
    if (self->m_selectionModel) {
        XItemSelectionModel_setCurrentIndex(self->m_selectionModel, -1, -1);
        XItemSelectionModel_clear(self->m_selectionModel);
    }
    xaiv_persistentResetTable(self);
    xaiv_indexWidgetResetTable(self);
    XWidget_update((XWidget*)self);
}

void XAbstractItemView_doItemsLayout(XAbstractItemView* self)
{
    if (!self) return;
    /* 平铺模型简化：条目几何由固定网格推导，无独立布局阶段与几何计算，
     * 这里仅对齐 Qt 的全量重绘效果（updateGeometries + viewport update
     * → 一次视图 update）；滚动条范围仍由派生视图维护。 */
    XWidget_update((XWidget*)self);
}

void XAbstractItemView_setSelectionMode(XAbstractItemView* self, int mode)
{ if (self) self->m_selectionMode = mode; }
int XAbstractItemView_selectionMode(const XAbstractItemView* self)
{ return self ? self->m_selectionMode : 0; }

void XAbstractItemView_setSelectionBehavior(XAbstractItemView* self,
                                            int behavior)
{ if (self) self->m_selectionBehavior = behavior; }
int XAbstractItemView_selectionBehavior(const XAbstractItemView* self)
{ return self ? self->m_selectionBehavior : 0; }

void XAbstractItemView_setEditTriggers(XAbstractItemView* self, int triggers)
{ if (self) self->m_editTriggers = triggers; }
int XAbstractItemView_editTriggers(const XAbstractItemView* self)
{ return self ? self->m_editTriggers : 0; }

/* ==================== 编辑闭环（对标 QAbstractItemView 编辑器会话） ==================== */

/** @brief 委托 commitData(editor) 信号槽：转发视图提交（对标 Qt 视图
 *         连接委托 commitData 信号的私有槽）。 */
static void xaiv_delegateCommitSlot(XObject* receiver, XVarList* args)
{
    XAbstractItemView* view = (XAbstractItemView*)receiver;
    XWidget* editor;
    if (!view || !args) return;
    XVarList_start(args);
    editor = XVarList_arg(args, XWidget*);
    XAbstractItemView_commitData(view, editor);
}

/** @brief 委托 closeEditor(editor, hint) 信号槽：转发视图关闭（对标 Qt
 *         视图连接委托 closeEditor 信号的私有槽）。 */
static void xaiv_delegateCloseSlot(XObject* receiver, XVarList* args)
{
    XAbstractItemView* view = (XAbstractItemView*)receiver;
    XWidget* editor;
    int hint;
    if (!view || !args) return;
    XVarList_start(args);
    editor = XVarList_arg(args, XWidget*);
    hint = XVarList_arg(args, int);
    XAbstractItemView_closeEditor(view, editor, hint);
}

/** @brief 解析编辑生效委托（行级→列级→默认；全部未设置时懒创建默认
 *         委托，对标 Qt 视图恒持有缺省 QStyledItemDelegate）。 */
static XItemDelegate* xaiv_resolveDelegate(XAbstractItemView* self,
                                           int row, int col)
{
    void* delegate;
    if (!self) return NULL;
    delegate = XAbstractItemView_itemDelegateForIndex(self, row, col);
    if (delegate) return (XItemDelegate*)delegate;
    if (!self->m_defaultDelegate)
        self->m_defaultDelegate = XItemDelegate_create();
    return self->m_defaultDelegate;
}

/** @brief 挂接委托编辑信号到视图槽（断旧再连保证幂等，规避重复连接
 *         叠加；对标 Qt 视图对所用委托的信号挂接）。 */
static void xaiv_wireDelegate(XAbstractItemView* self,
                              XItemDelegate* delegate)
{
    XObject* d;
    if (!self || !delegate) return;
    d = (XObject*)delegate;
    if (d->m_signalSlot) {
        XObject_disconnect_1(d, (size_t)XItemDelegate_commitData_signal,
                             (XObject*)self, xaiv_delegateCommitSlot);
        XObject_disconnect_1(d, (size_t)XItemDelegate_closeEditor_signal,
                             (XObject*)self, xaiv_delegateCloseSlot);
    }
    XObject_connect_1(d, (size_t)XItemDelegate_commitData_signal,
                      (XObject*)self, xaiv_delegateCommitSlot,
                      XConnectionType_Direct);
    XObject_connect_1(d, (size_t)XItemDelegate_closeEditor_signal,
                      (XObject*)self, xaiv_delegateCloseSlot,
                      XConnectionType_Direct);
}

/** @brief 提交核心：委托 setModelData → 模型 setData（commitData/
 *         closeEditor 提交分支共用；会话级幂等，同 Qt 提交一次性）。 */
static void xaiv_commitCore(XAbstractItemView* self)
{
    XItemDelegate* delegate;
    if (!self || !self->m_editor || self->m_editCommitted) return;
    delegate = xaiv_resolveDelegate(self, self->m_editRow, self->m_editCol);
    if (!delegate) return;
    XItemDelegate_setModelData(delegate, self->m_editor, self,
                               self->m_editRow, self->m_editCol);
    self->m_editCommitted = true;
}

bool XAbstractItemView_edit(XAbstractItemView* self, int row, int column)
{
    XAbstractItemModel* model;
    XItemDelegate* delegate;
    XWidget* editor;
    XRect rect;
    if (!self || row < 0 || column < 0) return false;
    model = self->m_model;
    if (!model || row >= model->m_rows || column >= model->m_cols)
        return false;
    /* 同 Qt edit(index, AllEditTriggers, nullptr)：触发集不含任何编辑
     * 触发时判定不通过（NoEditTriggers 直接失败）。 */
    if (self->m_editTriggers ==
        XAbstractItemViewEditTrigger_NoEditTriggers) return false;
    /* 对标 model flags ItemIsEditable 门禁（本库以视图开关承载）。 */
    if (!self->m_itemsEditable) return false;
    /* 同格已在编辑：直接成功（同 Qt 重复 edit(index) 语义）。 */
    if (self->m_editor && self->m_editRow == row &&
        self->m_editCol == column)
        return true;
    /* 他格在编辑：先按提交分支收敛（同 Qt 单编辑器会话）。 */
    if (self->m_editor)
        XAbstractItemView_closeEditor(self, self->m_editor,
                                      XItemDelegateEndEditHint_NoHint);
    delegate = xaiv_resolveDelegate(self, row, column);
    if (!delegate) return false;
    xaiv_wireDelegate(self, delegate);
    editor = XItemDelegate_createEditor(delegate, self, row, column);
    if (!editor) return false;
    /* 先滚动保证目标格可见再取几何（滚动偏移影响 visualRect 坐标）。 */
    XAbstractItemView_scrollTo(self, row, column);
    if (!XAbstractItemView_visualRect(self, row, column, &rect) ||
        rect.width <= 0 || rect.height <= 0) {
        /* 条目不可见（隐藏行/列/空几何）：不开编辑器（同 Qt 不可见
         * 条目无条目矩形可布局）。 */
        XObject_deleteLater((XObject*)editor);
        return false;
    }
    XItemDelegate_setEditorData(delegate, editor, self, row, column);
    XItemDelegate_updateEditorGeometry(delegate, editor, &rect);
    /* 会话状态先于显示/授焦点登记（显示期间的同步焦点事件须能看到
     * 有效会话）。 */
    self->m_editor = editor;
    self->m_editRow = row;
    self->m_editCol = column;
    self->m_editCommitted = false;
    self->m_editClosing = false;
    XWidget_show(editor);
    XWidget_raise(editor);
    XWidget_setFocus(editor);
    XWidget_update((XWidget*)self);
    return true;
}

bool XAbstractItemView_isEditing(const XAbstractItemView* self)
{ return self && self->m_editor != NULL; }

void XAbstractItemView_commitData(XAbstractItemView* self, XWidget* editor)
{
    if (!self || !editor) return;
    /* 编辑器不是当前会话编辑器：忽略（对标 Qt 校验会话归属）。 */
    if (self->m_editor != editor) return;
    xaiv_commitCore(self);
}

void XAbstractItemView_closeEditor(XAbstractItemView* self, XWidget* editor,
                                   int hint)
{
    bool submit;
    if (!self || !editor) return;
    if (self->m_editor != editor) return;
    /* 两分支（对齐 Qt）：RevertModelCache=放弃（不写模型）；其余提示
     * 为提交分支（尚未提交时先落库再关闭）。 */
    submit = (hint != XItemDelegateEndEditHint_RevertModelCache);
    /* 关闭门禁：抑制隐藏/延迟析构编辑器引发的失焦回调重入。 */
    self->m_editClosing = true;
    if (submit && !self->m_editCommitted) xaiv_commitCore(self);
    XWidget_hide(editor);
    /* 延迟析构（对标 Qt releaseEditor 的 deleteLater）：编辑器可能仍
     * 处于自身按键回调栈内，立即析构有悬空风险。 */
    XObject_deleteLater((XObject*)editor);
    self->m_editor = NULL;
    self->m_editRow = -1;
    self->m_editCol = -1;
    self->m_editCommitted = false;
    self->m_editClosing = false;
    XWidget_update((XWidget*)self);
    /* 编辑链提示（对标 Qt closeEditor 的 EditNextItem/EditPreviousItem
     * 处理）：移动当前项并按需重开编辑（Tab 逐格编辑链）。 */
    if (submit &&
        (hint == XItemDelegateEndEditHint_EditNextItem ||
         hint == XItemDelegateEndEditHint_EditPreviousItem)) {
        XAbstractItemModel* model = self->m_model;
        int row = self->m_currentRow +
                  (hint == XItemDelegateEndEditHint_EditNextItem ? 1 : -1);
        int col = self->m_currentColumn;
        if (model && col >= 0 && col < model->m_cols &&
            row >= 0 && row < model->m_rows) {
            XAbstractItemView_setCurrentIndex(self, row, col);
            if (self->m_itemsEditable)
                XAbstractItemView_edit(self, row, col);
        }
    }
}

void XAbstractItemView_setItemsEditable(XAbstractItemView* self,
                                        bool editable)
{ if (self) self->m_itemsEditable = editable; }

bool XAbstractItemView_itemsEditable(const XAbstractItemView* self)
{ return self ? self->m_itemsEditable : false; }

/* ==================== 委托（不透明承载） ==================== */

void XAbstractItemView_setItemDelegate(XAbstractItemView* self,
                                       void* delegate)
{
    if (!self || self->m_itemDelegate == delegate) return;
    /* 委托体系未建：仅存指针（借用，不析构、不虚分派）；
     * 同 Qt 委托变化后刷新视口（本库简化为整个视图 update）。 */
    self->m_itemDelegate = delegate;
    XWidget_update((XWidget*)self);
}

void* XAbstractItemView_itemDelegate(const XAbstractItemView* self)
{ return self ? self->m_itemDelegate : NULL; }

/* ==================== 行/列级委托（不透明承载） ==================== */

/* 行/列级委托指针表最小容量（首配即预留，之后倍增摊还）。 */
#define XAIV_DELEGATE_MIN_CAPACITY 8

/**
 * @brief 按需扩容委托指针表使 index 可直接寻址（只增不减，保留旧内容）。
 *
 * @param table 表指针的指针（*table 可为 NULL，表示首次分配）。
 * @param count 当前已分配容量（出/入参）。
 * @param index 需可写的下标（需 >=0）。
 * @return 已满足或扩容成功返回 true；参数非法或分配失败返回 false
 *         （失败时不改动旧表与容量）。
 */
static bool xaiv_delegateEnsure(void*** table, int* count, int index)
{
    void** fresh;
    int newCount;
    if (index < *count) return true;
    if (index < 0) return false;
    newCount = index + 1;
    if (newCount < XAIV_DELEGATE_MIN_CAPACITY)
        newCount = XAIV_DELEGATE_MIN_CAPACITY;
    if (*count > 0 && newCount < *count * 2) newCount = *count * 2;
    fresh = (void**)XMemory_calloc((size_t)newCount, sizeof(void*),
                                   XAIV_TABLE_MEMORY);
    if (!fresh) return false;
    if (*table) {
        XMemcpy(fresh, *table, (size_t)*count * sizeof(void*));
        XMemory_free(*table, XAIV_TABLE_MEMORY);
    }
    *table = fresh;
    *count = newCount;
    return true;
}

void XAbstractItemView_setItemDelegateForColumn(XAbstractItemView* self,
                                                int column, void* delegate)
{
    void** table;
    if (!self || column < 0) return;
    table = self->m_columnDelegates;
    if (!xaiv_delegateEnsure(&table, &self->m_columnDelegateCount, column))
        return;
    self->m_columnDelegates = table;
    if (self->m_columnDelegates[column] == delegate) return;
    /* 委托体系未建：仅存指针（借用，不析构、不虚分派）；NULL=清除该列
     * 覆盖回落默认委托。同 Qt 委托变化后刷新视口。 */
    self->m_columnDelegates[column] = delegate;
    XWidget_update((XWidget*)self);
}

void* XAbstractItemView_itemDelegateForColumn(const XAbstractItemView* self,
                                              int column)
{
    if (!self || column < 0) return NULL;
    if (!self->m_columnDelegates || column >= self->m_columnDelegateCount)
        return NULL;
    return self->m_columnDelegates[column];
}

void XAbstractItemView_setItemDelegateForRow(XAbstractItemView* self,
                                             int row, void* delegate)
{
    void** table;
    if (!self || row < 0) return;
    table = self->m_rowDelegates;
    if (!xaiv_delegateEnsure(&table, &self->m_rowDelegateCount, row)) return;
    self->m_rowDelegates = table;
    if (self->m_rowDelegates[row] == delegate) return;
    /* 行级优先于列级（同 Qt itemDelegate(index) 解析顺序）；当前仅承载。 */
    self->m_rowDelegates[row] = delegate;
    XWidget_update((XWidget*)self);
}

void* XAbstractItemView_itemDelegateForRow(const XAbstractItemView* self,
                                           int row)
{
    if (!self || row < 0) return NULL;
    if (!self->m_rowDelegates || row >= self->m_rowDelegateCount) return NULL;
    return self->m_rowDelegates[row];
}

void* XAbstractItemView_itemDelegateForIndex(const XAbstractItemView* self,
                                             int row, int col)
{
    void* delegate;
    if (!self) return NULL;
    /* 解析优先级（同 Qt itemDelegate(index)）：行级 → 列级 → 默认；
       行/列号 <0 时行/列级查询内部返回 NULL，自然回落下一层级。 */
    delegate = XAbstractItemView_itemDelegateForRow(self, row);
    if (delegate) return delegate;
    delegate = XAbstractItemView_itemDelegateForColumn(self, col);
    if (delegate) return delegate;
    return self->m_itemDelegate;
}

/* ==================== 平行表（持久编辑器标记 / 条目控件指针） ==================== */

/**
 * @brief 按需扩容行主序扁平平行表至 rows x cols（只增不减，保留旧内容）。
 *
 *        目标尺寸不大于当前尺寸时为空操作；否则分配零初始化的更大表，
 *        逐元素搬移旧内容（列数变化会改变行距，不能整体搬移）后释放旧表。
 *
 * @param table 表指针的指针（*table 可为 NULL，表示首次分配）。
 * @param curRows 当前已分配行数（出/入参）。
 * @param curCols 当前已分配列数（出/入参）。
 * @param rows 目标行数（需 >=1）。
 * @param cols 目标列数（需 >=1）。
 * @param elemSize 单元素字节数。
 * @return 已满足或扩容成功返回 true；参数非法或分配失败返回 false
 *         （失败时不改动旧表与容量）。
 */
static bool xaiv_tableEnsure(void** table, int* curRows, int* curCols,
                             int rows, int cols, size_t elemSize)
{
    char* fresh;
    int newRows;
    int newCols;
    int r;
    int c;
    size_t oldCols;
    size_t newColsZ;
    if (rows <= *curRows && cols <= *curCols) return true;
    if (rows <= 0 || cols <= 0) return false;
    newRows = rows > *curRows ? rows : *curRows;
    newCols = cols > *curCols ? cols : *curCols;
    fresh = (char*)XMemory_calloc((size_t)newRows * (size_t)newCols,
                                  elemSize, XAIV_TABLE_MEMORY);
    if (!fresh) return false;
    if (*table) {
        oldCols = (size_t)*curCols;
        newColsZ = (size_t)newCols;
        for (r = 0; r < *curRows; ++r) {
            for (c = 0; c < *curCols; ++c) {
                XMemcpy(fresh + ((size_t)r * newColsZ + (size_t)c) * elemSize,
                        (char*)*table +
                            ((size_t)r * oldCols + (size_t)c) * elemSize,
                        elemSize);
            }
        }
        XMemory_free(*table, XAIV_TABLE_MEMORY);
    }
    *table = fresh;
    *curRows = newRows;
    *curCols = newCols;
    return true;
}

/** @brief 清空持久编辑器打开标记（内容全部置 false；容量保留复用）。 */
static void xaiv_persistentResetTable(XAbstractItemView* self)
{
    if (self->m_persistentFlags && self->m_persistentRows > 0 &&
        self->m_persistentCols > 0) {
        XMemset(self->m_persistentFlags, 0,
                (size_t)self->m_persistentRows *
                    (size_t)self->m_persistentCols * sizeof(bool));
    }
}

/** @brief 清空条目控件承载（指针全部置 NULL；容量保留复用）。 */
static void xaiv_indexWidgetResetTable(XAbstractItemView* self)
{
    if (self->m_indexWidgets && self->m_indexWidgetRows > 0 &&
        self->m_indexWidgetCols > 0) {
        XMemset(self->m_indexWidgets, 0,
                (size_t)self->m_indexWidgetRows *
                    (size_t)self->m_indexWidgetCols * sizeof(void*));
    }
}

/** @brief 释放持久编辑器标记表并归零容量（setModel 行/列变化入口与析构同步）。 */
static void xaiv_persistentRelease(XAbstractItemView* self)
{
    if (self->m_persistentFlags) {
        XMemory_free(self->m_persistentFlags, XAIV_TABLE_MEMORY);
        self->m_persistentFlags = NULL;
    }
    self->m_persistentRows = 0;
    self->m_persistentCols = 0;
}

/** @brief 释放条目控件表并归零容量（setModel 行/列变化入口与析构同步）。 */
static void xaiv_indexWidgetRelease(XAbstractItemView* self)
{
    if (self->m_indexWidgets) {
        XMemory_free(self->m_indexWidgets, XAIV_TABLE_MEMORY);
        self->m_indexWidgets = NULL;
    }
    self->m_indexWidgetRows = 0;
    self->m_indexWidgetCols = 0;
}

/** @brief 释放行/列级委托指针表并归零容量（仅析构同步；行/列级委托
 *         不随 setModel 失效，同 Qt 跨模型保留）。 */
static void xaiv_delegateRelease(XAbstractItemView* self)
{
    if (self->m_columnDelegates) {
        XMemory_free(self->m_columnDelegates, XAIV_TABLE_MEMORY);
        self->m_columnDelegates = NULL;
    }
    self->m_columnDelegateCount = 0;
    if (self->m_rowDelegates) {
        XMemory_free(self->m_rowDelegates, XAIV_TABLE_MEMORY);
        self->m_rowDelegates = NULL;
    }
    self->m_rowDelegateCount = 0;
}

/* ==================== role 叠加存储（对标 model data(role) 的非文本维度） ==================== */

/**
 * @brief 单格 role 叠加条目。
 * @note  Qt 以 model data(index, role) 承载各 role；本库模型通路
 *        （XAbstractItemModel data/setData）无 role 参数仅承载文本，
 *        非文本 role 由视图基类按 (row,col) 叠加存储（基类扩展，
 *        XItemDataRole 语义不变）。文本 role（Display/Edit）不经此表，
 *        直连模型文本通路。
 */
typedef struct XAivRoleEntry
{
    int m_checkState;          /**< CheckStateRole（XItemCheckState；-1=未设置）。 */
    int m_alignment;           /**< TextAlignmentRole（XAlignment 位组合；0=未设置）。 */
    const XFont* m_font;       /**< FontRole（借用指针；NULL=未设置）。 */
    const void* m_decoration;  /**< DecorationRole（借用指针；NULL=未设置）。 */
} XAivRoleEntry;

/** @brief 清空 role 叠加存储并释放容量（setModel 行/列变化入口与析构同步）。 */
static void xaiv_roleRelease(XAbstractItemView* self)
{
    if (self->m_roleTable) {
        XMemory_free(self->m_roleTable, XAIV_TABLE_MEMORY);
        self->m_roleTable = NULL;
    }
    self->m_roleRows = 0;
    self->m_roleCols = 0;
}

/** @brief 只读定位单格 role 条目（未分配、越界或 self 为空返回 NULL）。 */
static const XAivRoleEntry* xaiv_roleEntry(const XAbstractItemView* self,
                                           int row, int col)
{
    if (!self || !self->m_roleTable || row < 0 || col < 0 ||
        row >= self->m_roleRows || col >= self->m_roleCols)
        return NULL;
    return (const XAivRoleEntry*)self->m_roleTable +
        (size_t)row * (size_t)self->m_roleCols + (size_t)col;
}

/** @brief 定位（按需扩容）单格 role 条目；写入口专用。 */
static XAivRoleEntry* xaiv_roleEntryForWrite(XAbstractItemView* self,
                                             int row, int col)
{
    void* table;
    if (!self || row < 0 || col < 0) return NULL;
    table = self->m_roleTable;
    if (!xaiv_tableEnsure(&table, &self->m_roleRows, &self->m_roleCols,
                          row + 1, col + 1, sizeof(XAivRoleEntry)))
        return NULL;
    self->m_roleTable = table;
    return (XAivRoleEntry*)self->m_roleTable +
        (size_t)row * (size_t)self->m_roleCols + (size_t)col;
}

const char* XAbstractItemView_itemText(const XAbstractItemView* self,
                                       int row, int col, int role)
{
    if (!self) return "";
    /* 文本 role 走模型文本通路（基类平铺模型单文本承载，Display/Edit
     * 同通道，同 Qt 默认模型两 role 同值语义）；其余 role 非文本。 */
    if (role != XItemDataRole_DisplayRole && role != XItemDataRole_EditRole)
        return "";
    return XAbstractItemModel_data_2(self->m_model, row, col);
}

bool XAbstractItemView_setItemText(XAbstractItemView* self, int row, int col,
                                   int role, const char* text)
{
    if (!self || !self->m_model) return false;
    if (role != XItemDataRole_DisplayRole && role != XItemDataRole_EditRole)
        return false;
    /* 转发模型 setData（发射 dataChanged 驱动视图刷新，对标 Qt 提交链）。 */
    return XAbstractItemModel_setData_2(self->m_model, row, col, text);
}

int XAbstractItemView_itemCheckState(const XAbstractItemView* self,
                                     int row, int col)
{
    const XAivRoleEntry* entry = xaiv_roleEntry(self, row, col);
    return entry ? entry->m_checkState : -1;
}

void XAbstractItemView_setItemCheckState(XAbstractItemView* self,
                                         int row, int col, int state)
{
    XAivRoleEntry* entry = xaiv_roleEntryForWrite(self, row, col);
    if (!entry) return;
    if (state < 0 || state > XItemCheckState_Checked) state = -1;
    if (entry->m_checkState == state) return;
    entry->m_checkState = state;
    XWidget_update((XWidget*)self);
}

int XAbstractItemView_itemTextAlignment(const XAbstractItemView* self,
                                        int row, int col)
{
    const XAivRoleEntry* entry = xaiv_roleEntry(self, row, col);
    return entry ? entry->m_alignment : 0;
}

void XAbstractItemView_setItemTextAlignment(XAbstractItemView* self,
                                            int row, int col, int alignment)
{
    XAivRoleEntry* entry = xaiv_roleEntryForWrite(self, row, col);
    if (!entry) return;
    if (entry->m_alignment == alignment) return;
    entry->m_alignment = alignment;
    XWidget_update((XWidget*)self);
}

const XFont* XAbstractItemView_itemFont(const XAbstractItemView* self,
                                        int row, int col)
{
    const XAivRoleEntry* entry = xaiv_roleEntry(self, row, col);
    return entry ? entry->m_font : NULL;
}

void XAbstractItemView_setItemFont(XAbstractItemView* self, int row, int col,
                                   const XFont* font)
{
    XAivRoleEntry* entry = xaiv_roleEntryForWrite(self, row, col);
    if (!entry) return;
    if (entry->m_font == font) return;
    entry->m_font = font; /* 借用承载：生命周期归调用方（对标 role 变体）。 */
    XWidget_update((XWidget*)self);
}

const void* XAbstractItemView_itemDecoration(const XAbstractItemView* self,
                                             int row, int col)
{
    const XAivRoleEntry* entry = xaiv_roleEntry(self, row, col);
    return entry ? entry->m_decoration : NULL;
}

void XAbstractItemView_setItemDecoration(XAbstractItemView* self, int row,
                                         int col, const void* decoration)
{
    XAivRoleEntry* entry = xaiv_roleEntryForWrite(self, row, col);
    if (!entry) return;
    if (entry->m_decoration == decoration) return;
    entry->m_decoration = decoration; /* 借用承载（通常为 XImage*）。 */
    XWidget_update((XWidget*)self);
}

void XAbstractItemView_openPersistentEditor(XAbstractItemView* self,
                                            int row, int col)
{
    void* table;
    bool* slot;
    if (!self || row < 0 || col < 0) return;
    table = self->m_persistentFlags;
    if (!xaiv_tableEnsure(&table, &self->m_persistentRows,
                          &self->m_persistentCols,
                          row + 1, col + 1, sizeof(bool)))
        return;
    self->m_persistentFlags = (bool*)table;
    slot = self->m_persistentFlags +
        (size_t)row * (size_t)self->m_persistentCols + (size_t)col;
    if (!*slot) {
        /* 编辑器本体不在范围（简化为"打开标记"），仅标记并请求重绘。 */
        *slot = true;
        XWidget_update((XWidget*)self);
    }
}

void XAbstractItemView_closePersistentEditor(XAbstractItemView* self,
                                             int row, int col)
{
    bool* slot;
    if (!self || row < 0 || col < 0) return;
    if (!self->m_persistentFlags || row >= self->m_persistentRows ||
        col >= self->m_persistentCols) return;
    slot = self->m_persistentFlags +
        (size_t)row * (size_t)self->m_persistentCols + (size_t)col;
    if (*slot) {
        *slot = false;
        XWidget_update((XWidget*)self);
    }
}

bool XAbstractItemView_isPersistentEditorOpen(const XAbstractItemView* self,
                                              int row, int col)
{
    if (!self || row < 0 || col < 0) return false;
    if (!self->m_persistentFlags || row >= self->m_persistentRows ||
        col >= self->m_persistentCols) return false;
    return self->m_persistentFlags[
        (size_t)row * (size_t)self->m_persistentCols + (size_t)col];
}

void XAbstractItemView_setIndexWidget(XAbstractItemView* self,
                                      int row, int col, void* widget)
{
    void* table;
    void** slot;
    if (!self || row < 0 || col < 0) return;
    table = self->m_indexWidgets;
    if (!xaiv_tableEnsure(&table, &self->m_indexWidgetRows,
                          &self->m_indexWidgetCols,
                          row + 1, col + 1, sizeof(void*)))
        return;
    self->m_indexWidgets = (void**)table;
    slot = self->m_indexWidgets +
        (size_t)row * (size_t)self->m_indexWidgetCols + (size_t)col;
    if (*slot == widget) return;
    /* 不接管所有权：widget 为借用指针（NULL=清除），几何摆放与显示
     * 交给调用方（简化承载）。 */
    *slot = widget;
    XWidget_update((XWidget*)self);
}

void* XAbstractItemView_indexWidget(const XAbstractItemView* self,
                                    int row, int col)
{
    if (!self || row < 0 || col < 0) return NULL;
    if (!self->m_indexWidgets || row >= self->m_indexWidgetRows ||
        col >= self->m_indexWidgetCols) return NULL;
    return self->m_indexWidgets[
        (size_t)row * (size_t)self->m_indexWidgetCols + (size_t)col];
}

/* ==================== 键盘与导航 ==================== */

void XAbstractItemView_setKeyboardSearch(XAbstractItemView* self, bool enable)
{ if (self) self->m_keyboardSearch = enable; }
bool XAbstractItemView_keyboardSearch(const XAbstractItemView* self)
{ return self ? self->m_keyboardSearch : true; }

/* ==================== 键盘搜索（对标 keyboardSearch 行为本体） ==================== */

/* 键盘搜索累积前缀（简化语义，见头文件 @note）：
 * - 每视图实例状态（m_searchPrefix/m_searchPrefixLen/m_searchLastMs，
 *   对标 Qt 视图私有的键盘搜索前缀成员；此前为全库共享静态缓冲，
 *   多视图交替键入时前缀互相污染）；
 * - 容量 XABSTRACTITEMVIEW_SEARCH_CAPACITY 字节（含结尾 NUL），超出
 *   容量的追加字符被丢弃；
 * - 2000ms 内连续调用累积前缀，超时自动重置为本次文本。 */
#define XAIV_SEARCH_INTERVAL_MS 2000

/** @brief 清空本视图的键盘搜索累积前缀。 */
static void xaiv_searchReset(XAbstractItemView* self)
{
    self->m_searchPrefix[0] = '\0';
    self->m_searchPrefixLen = 0;
}

/** @brief 追加文本到本视图累积前缀（容量受限，超出部分丢弃）。 */
static void xaiv_searchAppend(XAbstractItemView* self, const char* text,
                              size_t len)
{
    size_t i;
    for (i = 0; i < len; ++i) {
        if (self->m_searchPrefixLen + 1 >=
            XABSTRACTITEMVIEW_SEARCH_CAPACITY)
            break;
        self->m_searchPrefix[self->m_searchPrefixLen] = text[i];
        ++self->m_searchPrefixLen;
    }
    self->m_searchPrefix[self->m_searchPrefixLen] = '\0';
}

/** @brief 从当前行下一行起环形遍历当前列，返回前缀匹配命中行；无命中 -1。 */
static int xaiv_searchHitRow(const XAbstractItemView* self, int col)
{
    int rows;
    int start;
    int i;
    int row;
    if (!self->m_model || self->m_searchPrefixLen == 0) return -1;
    rows = self->m_model->m_rows;
    if (rows <= 0) return -1;
    if (col < 0 || col >= self->m_model->m_cols) col = 0;
    start = self->m_currentRow + 1;
    if (start < 0 || start >= rows) start = 0;
    for (i = 0; i < rows; ++i) {
        const char* cell;
        row = start + i;
        if (row >= rows) row -= rows;
        cell = XAbstractItemModel_data_2(self->m_model, row, col);
        if (!cell) continue;
        /* XStrstr 返回命中起始位置：等于串首即前缀匹配。 */
        if (XStrstr(cell, self->m_searchPrefix) == cell) return row;
    }
    return -1;
}

bool XAbstractItemView_keyboardSearch_2(XAbstractItemView* self,
                                        const char* text)
{
    size_t textLen;
    int64_t nowMs;
    int col;
    int hitRow;
    if (!self) return false;
    if (!text || text[0] == '\0') {
        xaiv_searchReset(self);
        return false;
    }
    if (!self->m_keyboardSearch) return false;
    textLen = XStrlen(text);
    nowMs = XDateTime_currentMSecsSinceEpoch();
    if (self->m_searchPrefixLen > 0 &&
        nowMs - self->m_searchLastMs > XAIV_SEARCH_INTERVAL_MS)
        xaiv_searchReset(self);
    xaiv_searchAppend(self, text, textLen);
    self->m_searchLastMs = nowMs;
    col = self->m_currentColumn;
    if (col < 0) col = 0; /* 无当前列时在首列搜索（与命中后移动保持一致）。 */
    hitRow = xaiv_searchHitRow(self, col);
    if (hitRow < 0 && self->m_searchPrefixLen > textLen) {
        /* 同 Qt：累积前缀无命中时回退为仅本次键入文本重新搜索。 */
        xaiv_searchReset(self);
        xaiv_searchAppend(self, text, textLen);
        hitRow = xaiv_searchHitRow(self, col);
    }
    if (hitRow < 0) return false;
    XAbstractItemView_setCurrentIndex(self, hitRow, col);
    XAbstractItemView_scrollTo(self, hitRow, col);
    return true;
}

void XAbstractItemView_setTabKeyNavigation(XAbstractItemView* self,
                                           bool enable)
{ if (self) self->m_tabKeyNavigation = enable; }
bool XAbstractItemView_tabKeyNavigation(const XAbstractItemView* self)
{ return self ? self->m_tabKeyNavigation : false; }

void XAbstractItemView_setAlternatingRowColors(XAbstractItemView* self,
                                               bool enable)
{ if (self) self->m_alternatingRowColors = enable; }
bool XAbstractItemView_alternatingRowColors(const XAbstractItemView* self)
{ return self ? self->m_alternatingRowColors : false; }

void XAbstractItemView_setAutoScroll(XAbstractItemView* self, bool enable)
{ if (self) self->m_autoScroll = enable; }
bool XAbstractItemView_hasAutoScroll(const XAbstractItemView* self)
{ return self ? self->m_autoScroll : true; }

void XAbstractItemView_setAutoScrollMargin(XAbstractItemView* self, int margin)
{ if (self) self->m_autoScrollMargin = margin; }
int XAbstractItemView_autoScrollMargin(const XAbstractItemView* self)
{ return self ? self->m_autoScrollMargin : 16; }

void XAbstractItemView_setTextElideMode(XAbstractItemView* self, int mode)
{ if (self) self->m_textElideMode = mode; }
int XAbstractItemView_textElideMode(const XAbstractItemView* self)
{ return self ? self->m_textElideMode
              : (int)XAbstractItemViewTextElideMode_ElideRight; }

void XAbstractItemView_setVerticalScrollMode(XAbstractItemView* self, int mode)
{ if (self) self->m_verticalScrollMode = mode; }
int XAbstractItemView_verticalScrollMode(const XAbstractItemView* self)
{ return self ? self->m_verticalScrollMode
              : (int)XAbstractItemViewScrollMode_ScrollPerItem; }

void XAbstractItemView_setHorizontalScrollMode(XAbstractItemView* self,
                                               int mode)
{ if (self) self->m_horizontalScrollMode = mode; }
int XAbstractItemView_horizontalScrollMode(const XAbstractItemView* self)
{ return self ? self->m_horizontalScrollMode
              : (int)XAbstractItemViewScrollMode_ScrollPerItem; }

void XAbstractItemView_resetVerticalScrollMode(XAbstractItemView* self)
{
    if (!self) return;
    /* Qt 经样式 Hint 取缺省；本库缺省恒为 ScrollPerItem（字段初值）。 */
    if (self->m_verticalScrollMode ==
        XAbstractItemViewScrollMode_ScrollPerItem) return;
    self->m_verticalScrollMode =
        XAbstractItemViewScrollMode_ScrollPerItem;
    XWidget_update((XWidget*)self);
}

void XAbstractItemView_resetHorizontalScrollMode(XAbstractItemView* self)
{
    if (!self) return;
    if (self->m_horizontalScrollMode ==
        XAbstractItemViewScrollMode_ScrollPerItem) return;
    self->m_horizontalScrollMode =
        XAbstractItemViewScrollMode_ScrollPerItem;
    XWidget_update((XWidget*)self);
}

/* ==================== 拖放属性（状态承载） ==================== */

void XAbstractItemView_setDragEnabled(XAbstractItemView* self, bool enable)
{ if (self) self->m_dragEnabled = enable; }
bool XAbstractItemView_dragEnabled(const XAbstractItemView* self)
{ return self ? self->m_dragEnabled : false; }

void XAbstractItemView_setDragDropMode(XAbstractItemView* self, int mode)
{ if (self) self->m_dragDropMode = mode; }
int XAbstractItemView_dragDropMode(const XAbstractItemView* self)
{ return self ? self->m_dragDropMode
              : (int)XAbstractItemViewDragDropMode_NoDragDrop; }

void XAbstractItemView_setDragDropOverwriteMode(XAbstractItemView* self,
                                                bool overwrite)
{ if (self) self->m_dragDropOverwriteMode = overwrite; }
bool XAbstractItemView_dragDropOverwriteMode(const XAbstractItemView* self)
{ return self ? self->m_dragDropOverwriteMode : false; }

void XAbstractItemView_setDefaultDropAction(XAbstractItemView* self,
                                            int action)
{ if (self) self->m_defaultDropAction = action; }
int XAbstractItemView_defaultDropAction(const XAbstractItemView* self)
{ return self ? self->m_defaultDropAction : 0; }

void XAbstractItemView_setDropIndicatorShown(XAbstractItemView* self,
                                             bool enable)
{ if (self) self->m_showDropIndicator = enable; }
bool XAbstractItemView_showDropIndicator(const XAbstractItemView* self)
{ return self ? self->m_showDropIndicator : true; }

/* ==================== 尺寸提示 ==================== */

int XAbstractItemView_sizeHintForColumn(const XAbstractItemView* self,
                                        int column)
{
    XRect rect;
    int row;
    int max;
    if (!self || column < 0) return -1;
    if (!self->m_model || column >= self->m_model->m_cols) return -1;
    max = 0;
    for (row = 0; row < self->m_model->m_rows; ++row) {
        if (!XAbstractItemView_visualRect(self, row, column, &rect)) continue;
        if (rect.width > max) max = rect.width;
    }
    return max;
}

int XAbstractItemView_sizeHintForRow(const XAbstractItemView* self, int row)
{
    XRect rect;
    int col;
    int max;
    if (!self || row < 0) return -1;
    if (!self->m_model || row >= self->m_model->m_rows) return -1;
    max = 0;
    for (col = 0; col < self->m_model->m_cols; ++col) {
        if (!XAbstractItemView_visualRect(self, row, col, &rect)) continue;
        if (rect.height > max) max = rect.height;
    }
    return max;
}

bool XAbstractItemView_sizeHintForIndex(const XAbstractItemView* self,
                                        int row, int column,
                                        int* outWidth, int* outHeight)
{
    XRect rect;
    if (outWidth) *outWidth = 0;
    if (outHeight) *outHeight = 0;
    if (!self) return false;
    if (!XAbstractItemView_visualRect(self, row, column, &rect)) return false;
    /* 基类基于 visualRect 固定网格几何（80x24）返回；Qt 以 QSize 承载，
     * 本库按项目惯例以双 int 输出宽/高。 */
    if (outWidth) *outWidth = rect.width;
    if (outHeight) *outHeight = rect.height;
    return true;
}

void XAbstractItemView_scrollTo(XAbstractItemView* self, int row, int column)
{
    XAbstractItemView_scrollToHint(self, row, column,
                                   XAbstractItemViewScrollHint_EnsureVisible);
}

void XAbstractItemView_scrollToHint(XAbstractItemView* self, int row,
                                    int column, int hint)
{
    XAbstractScrollArea* area;
    XWidget* viewport;
    XRect rect;
    XScrollBar* vbar;
    XScrollBar* hbar;
    int visibleW;
    int visibleH;
    int value;
    int target;
    if (!self || row < 0 || column < 0) return;
    if (!XAbstractItemView_visualRect(self, row, column, &rect)) return;
    area = (XAbstractScrollArea*)self;
    viewport = XAbstractScrollArea_viewport(area);
    visibleW = viewport ? XWidget_width(viewport) : 0;
    visibleH = viewport ? XWidget_height(viewport) : 0;
    if (visibleW < 0) visibleW = 0;
    if (visibleH < 0) visibleH = 0;
    vbar = XAbstractScrollArea_verticalScrollBar(area);
    hbar = XAbstractScrollArea_horizontalScrollBar(area);
    /* 垂直方向：EnsureVisible 区分目标在可视区上方（IndexAbove → 顶端
     * 对齐）与下方（IndexBelow → 底端对齐）；其余提示按字面定位。
     * setValue 内部收敛进 [minimum, maximum]，内容尺寸由派生视图维护。 */
    if (vbar) {
        value = XScrollBar_value(vbar);
        target = value;
        switch (hint) {
        case XAbstractItemViewScrollHint_PositionAtTop:
            target = rect.y;
            break;
        case XAbstractItemViewScrollHint_PositionAtBottom:
            target = rect.y + rect.height - visibleH;
            break;
        case XAbstractItemViewScrollHint_PositionAtCenter:
            target = rect.y + rect.height / 2 - visibleH / 2;
            break;
        case XAbstractItemViewScrollHint_EnsureVisible:
        default:
            if (rect.y < value)
                target = rect.y;
            else if (rect.y + rect.height > value + visibleH)
                target = rect.y + rect.height - visibleH;
            break;
        }
        XScrollBar_setValue(vbar, target);
    }
    /* 水平方向：语义同垂直方向（左右对称替换上下）。 */
    if (hbar) {
        value = XScrollBar_value(hbar);
        target = value;
        switch (hint) {
        case XAbstractItemViewScrollHint_PositionAtTop:
            target = rect.x;
            break;
        case XAbstractItemViewScrollHint_PositionAtBottom:
            target = rect.x + rect.width - visibleW;
            break;
        case XAbstractItemViewScrollHint_PositionAtCenter:
            target = rect.x + rect.width / 2 - visibleW / 2;
            break;
        case XAbstractItemViewScrollHint_EnsureVisible:
        default:
            if (rect.x < value)
                target = rect.x;
            else if (rect.x + rect.width > value + visibleW)
                target = rect.x + rect.width - visibleW;
            break;
        }
        XScrollBar_setValue(hbar, target);
    }
}

void XAbstractItemView_scrollToTop(XAbstractItemView* self)
{
    XScrollBar* bar;
    if (!self) return;
    bar = XAbstractScrollArea_verticalScrollBar((XAbstractScrollArea*)self);
    if (bar) XScrollBar_setValue(bar, XScrollBar_minimum(bar));
}

void XAbstractItemView_scrollToBottom(XAbstractItemView* self)
{
    XScrollBar* bar;
    if (!self) return;
    bar = XAbstractScrollArea_verticalScrollBar((XAbstractScrollArea*)self);
    if (bar) XScrollBar_setValue(bar, XScrollBar_maximum(bar));
}

/* ==================== 模型与选择 ==================== */

XAbstractItemModel* XAbstractItemView_model(const XAbstractItemView* self)
{ return self ? self->m_model : NULL; }

static void xaiv_modelRefreshSlot(XObject* receiver, XVarList* args);

void XAbstractItemView_setModel(XAbstractItemView* self,
                                XAbstractItemModel* model)
{
    if (!self) return;
    /* 模型替换前收敛打开的编辑器（提交分支落库旧模型后关闭；对标 Qt
     * setModel 关闭全部编辑器）。 */
    if (self->m_editor)
        XAbstractItemView_closeEditor(self, self->m_editor,
                                      XItemDelegateEndEditHint_NoHint);
    /* 对标 Qt：模型替换时断开旧模型信号、连接新模型信号（此前不
     * 连接，外部改模型后视图不刷新）。 */
    if (self->m_model) {
        XObject* m = (XObject*)self->m_model;
        XObject_disconnect_1(m, (size_t)XAbstractItemModel_dataChanged_signal(
                                   self->m_model, 0, 0),
                             (XObject*)self, xaiv_modelRefreshSlot);
        XObject_disconnect_1(m, (size_t)XAbstractItemModel_rowsInserted_signal(
                                    self->m_model, 0, 0),
                             (XObject*)self, xaiv_modelRefreshSlot);
        XObject_disconnect_1(m, (size_t)XAbstractItemModel_rowsRemoved_signal(
                                    self->m_model, 0, 0),
                             (XObject*)self, xaiv_modelRefreshSlot);
        XObject_disconnect_1(m, (size_t)XAbstractItemModel_modelReset_signal(
                                   self->m_model),
                             (XObject*)self, xaiv_modelRefreshSlot);
    }
    self->m_model = model;
    /* 行/列变化入口同步：模型替换使全部 (row,col) 失效，释放持久编辑器
     * 标记表、条目控件表与 role 叠加存储表（指针归调用方，仅解除承载）。 */
    xaiv_persistentRelease(self);
    xaiv_indexWidgetRelease(self);
    xaiv_roleRelease(self);
    if (model) {
        if (self->m_currentRow >= model->m_rows)
            self->m_currentRow = model->m_rows - 1;
        if (self->m_currentColumn >= model->m_cols)
            self->m_currentColumn = model->m_cols - 1;
    } else {
        self->m_currentRow = -1;
        self->m_currentColumn = -1;
    }
    if (model) {
        XObject* m = (XObject*)model;
#define XAIV_MODEL_CONNECT(sig) \
    XObject_connect_1(m, (size_t)(sig), (XObject*)self, \
                      xaiv_modelRefreshSlot, XConnectionType_Direct)
        XAIV_MODEL_CONNECT(XAbstractItemModel_dataChanged_signal(model, 0, 0));
        XAIV_MODEL_CONNECT(XAbstractItemModel_rowsInserted_signal(model,
                                                                   0, 0));
        XAIV_MODEL_CONNECT(XAbstractItemModel_rowsRemoved_signal(model,
                                                                   0, 0));
        XAIV_MODEL_CONNECT(XAbstractItemModel_modelReset_signal(model));
#undef XAIV_MODEL_CONNECT
    }
    XWidget_update((XWidget*)self);
}

/** @brief 模型信号统一刷新槽：收敛当前索引越界并重绘（对标 Qt 视图
 *         对 dataChanged/rowsInserted/rowsRemoved/modelReset 的自动
 *         响应；此前 setModel 不连接任何信号）。 */
static void xaiv_modelRefreshSlot(XObject* receiver, XVarList* args)
{
    XAbstractItemView* view = (XAbstractItemView*)receiver;
    XAbstractItemModel* model;
    if (!view) return;
    model = view->m_model;
    if (model) {
        if (view->m_currentRow >= model->m_rows)
            view->m_currentRow = model->m_rows - 1;
        if (view->m_currentColumn >= model->m_cols)
            view->m_currentColumn = model->m_cols - 1;
    }
    /* 编辑格随行/列删除失效：放弃分支关闭（不写已失效坐标，对标 Qt
     * 对删除索引处编辑器的收回）。 */
    if (view->m_editor && model &&
        (view->m_editRow >= model->m_rows ||
         view->m_editCol >= model->m_cols))
        XAbstractItemView_closeEditor(view, view->m_editor,
                                      XItemDelegateEndEditHint_RevertModelCache);
    XWidget_update((XWidget*)view);
}

XItemSelectionModel* XAbstractItemView_selectionModel(
    const XAbstractItemView* self)
{
    return self ? self->m_selectionModel : NULL;
}

void XAbstractItemView_setSelectionModel(XAbstractItemView* self,
                                         XItemSelectionModel* selectionModel)
{
    if (!self) return;
    if (self->m_selectionModel && self->m_selectionModel != selectionModel)
        XItemSelectionModel_delete_base(self->m_selectionModel);
    self->m_selectionModel = selectionModel;
    if (!self->m_selectionModel)
        self->m_selectionModel = XItemSelectionModel_create();
}

void XAbstractItemView_clearSelection(XAbstractItemView* self)
{
    if (!self || !self->m_selectionModel) return;
    XItemSelectionModel_clear(self->m_selectionModel);
}

void XAbstractItemView_selectAll(XAbstractItemView* self)
{
    XItemSelectionModel* selection;
    int row;
    int col;
    if (!self) return;
    selection = self->m_selectionModel;
    if (!selection || !self->m_model) return;
    if (self->m_selectionMode ==
        XAbstractItemViewSelectionMode_NoSelection) return;
    if (self->m_selectionMode ==
        XAbstractItemViewSelectionMode_SingleSelection) {
        /* 单选约束：仅选中当前项（无当前项时选首格）。 */
        row = self->m_currentRow >= 0 ? self->m_currentRow : 0;
        col = self->m_currentColumn >= 0 ? self->m_currentColumn : 0;
        if (row < self->m_model->m_rows && col < self->m_model->m_cols)
            XItemSelectionModel_select(selection, row, col, true);
        return;
    }
    for (row = 0; row < self->m_model->m_rows; ++row) {
        for (col = 0; col < self->m_model->m_cols; ++col)
            XItemSelectionModel_select(selection, row, col, true);
    }
}

int XAbstractItemView_rootRow(const XAbstractItemView* self)
{ return self ? self->m_rootRow : -1; }

int XAbstractItemView_rootColumn(const XAbstractItemView* self)
{ return self ? self->m_rootCol : -1; }

void XAbstractItemView_setRootIndex(XAbstractItemView* self, int row, int col)
{
    if (!self) return;
    self->m_rootRow = row;
    self->m_rootCol = col;
}

bool XAbstractItemView_rootIndex(const XAbstractItemView* self,
                                 int* outRow, int* outCol)
{
    /* 平铺模型无层级：根索引恒 (0,0)；setRootIndex 存储的树预留偏移
     * 不影响本查询（树形视图实现后再改为返回存储根）。 */
    if (outRow) *outRow = 0;
    if (outCol) *outCol = 0;
    return self != NULL;
}

/** @brief 基类命中实现：默认网格布局（行高 24、列宽 80）。 */
static bool VXAbstractItemView_indexAt(const XAbstractItemView* self,
                                       int x, int y,
                                       int* outRow, int* outCol)
{
    int row;
    int col;
    if (outRow) *outRow = -1;
    if (outCol) *outCol = -1;
    if (!self) return false;
    row = y / 24;
    col = x / 80;
    if (row < 0 || col < 0) return false;
    if (self->m_model) {
        if (row >= self->m_model->m_rows) return false;
        if (col >= self->m_model->m_cols) return false;
    }
    if (outRow) *outRow = row;
    if (outCol) *outCol = col;
    return true;
}

bool XAbstractItemView_indexAt_base(const XAbstractItemView* self, int x,
                                    int y, int* outRow, int* outCol)
{
    bool (*fn)(const XAbstractItemView*, int, int, int*, int*);
    if (!self) return false;
    fn = (bool (*)(const XAbstractItemView*, int, int, int*, int*))
        XVtableGetFunc(XClassGetVtable((XClass*)self),
                       EXAbstractItemView_IndexAt, void*);
    if (!fn) return false;
    return fn(self, x, y, outRow, outCol);
}

/** @brief 基类矩形实现：默认网格布局（行高 24、列宽 80）。 */
static bool VXAbstractItemView_visualRect(const XAbstractItemView* self,
                                          int row, int col, XRect* out)
{
    if (!self || !out || row < 0 || col < 0) return false;
    XRect_init(out, col * 80, row * 24, 80, 24);
    return true;
}

bool XAbstractItemView_visualRect(const XAbstractItemView* self,
                                  int row, int col, XRect* out)
{
    bool (*fn)(const XAbstractItemView*, int, int, XRect*);
    if (!self || !out) return false;
    /* 虚分派（对标 visualRect 为 QAbstractItemView 纯虚函数）：派生
       视图覆写条目几何；编辑器摆放/scrollTo/尺寸提示共用该通路。 */
    fn = (bool (*)(const XAbstractItemView*, int, int, XRect*))
        XVtableGetFunc(XClassGetVtable((XClass*)self),
                       EXAbstractItemView_VisualRect, void*);
    if (!fn) return false;
    return fn(self, row, col, out);
}

void XAbstractItemView_setIconSize(XAbstractItemView* self, int w, int h)
{
    XVarList* args;
    if (!self || w <= 0 || h <= 0) return;
    if (self->m_iconW == w && self->m_iconH == h) return;
    self->m_iconW = w;
    self->m_iconH = h;
    args = XVarList_Create(XVar(int, w), XVar(int, h));
    if (self && ((XObject*)self)->m_signalSlot && args) {
        XObject_emitSignal((XObject*)self,
                           (size_t)XAbstractItemView_iconSizeChanged_signal,
                           args, NULL, NULL, XEVENT_PRIORITY_NORMAL);
    } else if (args) {
        XVarList_delete(args);
    }
    XWidget_update((XWidget*)self);
}

int XAbstractItemView_iconWidth(const XAbstractItemView* self)
{ return self ? self->m_iconW : 0; }

int XAbstractItemView_iconHeight(const XAbstractItemView* self)
{ return self ? self->m_iconH : 0; }

void XAbstractItemView_iconSize(const XAbstractItemView* self,
                                int* outWidth, int* outHeight)
{
    /* 组合 getter：与 iconWidth/iconHeight 同一字段（Qt QSize 以双 int 承载）。 */
    if (outWidth) *outWidth = self ? self->m_iconW : 0;
    if (outHeight) *outHeight = self ? self->m_iconH : 0;
}

/* ==================== 信号发射助手 ==================== */

static void xaiv_emitIndex(XAbstractItemView* self, size_t signal,
                           int row, int col)
{
    XVarList* args = XVarList_Create(XVar(int, row), XVar(int, col));
    if (!args) return;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

static void xaiv_emitVoid(XAbstractItemView* self, size_t signal)
{
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, NULL, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    }
}

/* ==================== 鼠标事件（点击选择 + 信号） ==================== */

static void VXAbstractItemView_mousePressEvent(XWidget* self, XEvent* event)
{
    XAbstractItemView* view = (XAbstractItemView*)self;
    XMouseEvent* me;
    XPoint pos;
    int row;
    int col;
    if (!view || !event) return;
    me = (XMouseEvent*)event;
    pos = XMouseEvent_position(me);
    if (XMouseEvent_button(me) != XMouseButton_LeftButton) {
        XEvent_ignore(event);
        return;
    }
    if (XAbstractItemView_indexAt_base(view, pos.x, pos.y, &row, &col)) {
        XAbstractItemViewSelectionMode mode = view->m_selectionMode;
        /* 点击他格先按提交分支关闭编辑器（对标 Qt 点击编辑格以外的
         * 位置收敛打开的编辑器；点击编辑格本体由编辑器子控件承接）。 */
        if (view->m_editor &&
            (row != view->m_editRow || col != view->m_editCol)) {
            XAbstractItemView_closeEditor(view, view->m_editor,
                                          XItemDelegateEndEditHint_NoHint);
        }
        XItemSelectionModel* sm = view->m_selectionModel;
        bool ctrl = (XMouseEvent_modifiers(me) &
                     XKeyboardModifier_ControlModifier) != 0;
        bool shift = (XMouseEvent_modifiers(me) &
                      XKeyboardModifier_ShiftModifier) != 0;
        bool toggle = (mode == XAbstractItemViewSelectionMode_MultiSelection)
                      || (ctrl && mode !=
                              XAbstractItemViewSelectionMode_SingleSelection &&
                          mode != XAbstractItemViewSelectionMode_NoSelection);
        if (mode == XAbstractItemViewSelectionMode_NoSelection) {
            xaiv_setCurrentPreservingSelection(view, row, col);
        } else if (shift && mode != XAbstractItemViewSelectionMode_SingleSelection) {
            int ar = view->m_selectionAnchorRow;
            int ac = view->m_selectionAnchorCol;
            if (ar < 0 || ac < 0) { ar = row; ac = col; }
            xaiv_selectRect(view, ar, ac, row, col);
            view->m_currentRow = row;
            view->m_currentColumn = col;
            if (sm)
                XItemSelectionModel_setCurrentIndex(sm, row, col);
            XWidget_update((XWidget*)view);
        } else if (toggle && sm) {
            bool sel = XItemSelectionModel_isSelected(sm, row, col);
            xaiv_selectCellExpanded(view, row, col, !sel);
            view->m_currentRow = row;
            view->m_currentColumn = col;
            if (sm)
                XItemSelectionModel_setCurrentIndex(sm, row, col);
            XWidget_update((XWidget*)view);
        } else {
            XAbstractItemView_setCurrentIndex(view, row, col);
            view->m_selectionAnchorRow = row;
            view->m_selectionAnchorCol = col;
        }
        xaiv_emitIndex(view, (size_t)XAbstractItemView_pressed_signal,
                       row, col);
        /* CurrentChanged 触发位（对标 Qt mousePressEvent 尾部的编辑
         * 判定）：按压改变当前项后按触发位进入编辑。 */
        if ((view->m_editTriggers &
             XAbstractItemViewEditTrigger_CurrentChanged) &&
            view->m_itemsEditable)
            XAbstractItemView_edit(view, row, col);
    }
    XEvent_accept(event);
}

static void VXAbstractItemView_mouseReleaseEvent(XWidget* self,
                                                 XEvent* event)
{
    XAbstractItemView* view = (XAbstractItemView*)self;
    XMouseEvent* me;
    XPoint pos;
    int row;
    int col;
    if (!view || !event) return;
    me = (XMouseEvent*)event;
    pos = XMouseEvent_position(me);
    if (XMouseEvent_button(me) != XMouseButton_LeftButton) {
        XEvent_ignore(event);
        return;
    }
    if (XAbstractItemView_indexAt_base(view, pos.x, pos.y, &row, &col)) {
        xaiv_emitIndex(view, (size_t)XAbstractItemView_clicked_signal,
                       row, col);
        xaiv_emitIndex(view, (size_t)XAbstractItemView_activated_signal,
                       row, col);
        /* SelectedClicked 触发位：选中条目上单击释放进入编辑（对标 Qt
         * mouseReleaseEvent 的编辑判定；拖拽识别简化为选中即触发）。 */
        if ((view->m_editTriggers &
             XAbstractItemViewEditTrigger_SelectedClicked) &&
            view->m_itemsEditable && view->m_selectionModel &&
            XItemSelectionModel_isSelected(view->m_selectionModel, row, col))
            XAbstractItemView_edit(view, row, col);
    }
    XEvent_accept(event);
}

static void VXAbstractItemView_mouseDoubleClickEvent(XWidget* self,
                                                     XEvent* event)
{
    XAbstractItemView* view = (XAbstractItemView*)self;
    XMouseEvent* me;
    XPoint pos;
    int row;
    int col;
    if (!view || !event) return;
    me = (XMouseEvent*)event;
    pos = XMouseEvent_position(me);
    if (XAbstractItemView_indexAt_base(view, pos.x, pos.y, &row, &col)) {
        xaiv_emitIndex(view,
                       (size_t)XAbstractItemView_doubleClicked_signal,
                       row, col);
        /* DoubleClicked 触发位（对标 Qt mouseDoubleClickEvent：发射
         * doubleClicked 后进入编辑判定）。 */
        if ((view->m_editTriggers &
             XAbstractItemViewEditTrigger_DoubleClicked) &&
            view->m_itemsEditable)
            XAbstractItemView_edit(view, row, col);
    }
    XEvent_accept(event);
}

/** @brief 移动：悬停索引差分——进入新条目才发射 entered（边沿一次）；
 *         由条目移入视口空白（悬停失效）才发射 viewportEntered。 */
static void VXAbstractItemView_mouseMoveEvent(XWidget* self, XEvent* event)
{
    XAbstractItemView* view = (XAbstractItemView*)self;
    XMouseEvent* me;
    XPoint pos;
    int row = -1;
    int col = -1;
    int prevRow = -1;
    int prevCol = -1;
    if (!view || !event) return;
    me = (XMouseEvent*)event;
    pos = XMouseEvent_position(me);
    /* 前后索引差分（对照 XListWidget m_enteredRow 范式并升级为行列
     * 成对+失效态）：修正此前命中即发射——每次移动重复发 entered，
     * 且 viewportEntered 在条目上发射（与 Qt 相反：Qt 在悬停离开
     * 条目进入视口空白区的边沿发射）。 */
    (void)XAbstractItemView_indexAt_base(view, pos.x, pos.y, &row, &col);
    xaiv_hoverGet(view, &prevRow, &prevCol);
    if (row >= 0 && col >= 0) {
        if (row != prevRow || col != prevCol) {
            xaiv_emitIndex(view, (size_t)XAbstractItemView_entered_signal,
                           row, col);
            xaiv_hoverSet(view, row, col);
        }
    }
    else if (prevRow >= 0) {
        /* 悬停失效（移入空白/无效区）：上一次悬停在有效条目上才发射
         * （对标 Qt viewportEntered 的进入视口空白语义）并复位基准。 */
        xaiv_emitVoid(view,
                      (size_t)XAbstractItemView_viewportEntered_signal);
        xaiv_hoverSet(view, -1, -1);
    }
    XEvent_accept(event);
}

/** @brief 离开：复位悬停差分基准（不发射——对标 Qt HoverLeave 仅复位
 *         悬停索引；再次进入时 entered 重新发射）。 */
static void VXAbstractItemView_leaveEvent(XWidget* self, XEvent* event)
{
    if (!self) return;
    if (event && XEvent_type(event) == XEVENT_TYPE_LEAVE)
        xaiv_hoverRelease((XAbstractItemView*)self);
    XClass_Parent(XAbstractScrollArea, EXWidget_LeaveEvent,
                  void (*)(XWidget*, XEvent*))(self, event);
}

/* ==================== 键盘事件（Enter 激活 + 键盘搜索接入） ==================== */

/** @brief 键盘：Enter/Return 激活当前项（发射 activated）；可打印字符
 *         转入键盘搜索前缀匹配（对标 QAbstractItemView 键盘处理）。 */
static void VXAbstractItemView_keyPressEvent(XWidget* self, XEvent* event)
{
    XAbstractItemView* view = (XAbstractItemView*)self;
    XKeyEvent* ke;
    int key;
    XKeyboardModifiers modifiers;
    if (!view || !event ||
        XEvent_type(event) != XEVENT_TYPE_KEY_PRESS) return;
    ke = (XKeyEvent*)event;
    key = XKeyEvent_key(ke);
    modifiers = XKeyEvent_modifiers(ke);
    if (key == XKey_Return || key == XKey_Enter) {
        /* EditKeyPressed 触发位下 Return 先尝试编辑当前项（对标 Qt
         * keyPressEvent 的 Return/Enter 分支：先 edit(EditKeyPressed)
         * 拦截，开启失败——不可编辑/门禁关——再回落激活语义）。 */
        if (view->m_itemsEditable &&
            (view->m_editTriggers &
             XAbstractItemViewEditTrigger_EditKeyPressed) &&
            view->m_currentRow >= 0 && view->m_currentColumn >= 0 &&
            XAbstractItemView_edit(view, view->m_currentRow,
                                   view->m_currentColumn)) {
            XEvent_accept(event);
            return;
        }
        if (view->m_currentRow >= 0 && view->m_currentColumn >= 0) {
            xaiv_emitIndex(view,
                           (size_t)XAbstractItemView_activated_signal,
                           view->m_currentRow, view->m_currentColumn);
        }
        XEvent_accept(event);
        return;
    }
    /* F2（EditKeyPressed 触发位，对标 Qt 键盘编辑键）：编辑当前项。 */
    if (key == (int)XKey_F2 && view->m_itemsEditable &&
        (view->m_editTriggers &
         XAbstractItemViewEditTrigger_EditKeyPressed) &&
        view->m_currentRow >= 0 && view->m_currentColumn >= 0) {
        if (XAbstractItemView_edit(view, view->m_currentRow,
                                   view->m_currentColumn)) {
            XEvent_accept(event);
            return;
        }
    }
    /* 键盘导航（对标 QAbstractItemView）：方向键/翻页/Home/End 移动
       当前项；Shift 从锚点扩选；Ctrl 仅移动不改选择；无修饰按选择
       模式选中（此前键盘导航完全缺失）。 */
    {
        XAbstractItemModel* model = view->m_model;
        int rows = model ? model->m_rows : 0;
        int cols = model ? model->m_cols : 0;
        int deltaRow = 0;
        int deltaCol = 0;
        int pageRows;
        bool ctrl = (modifiers & XKeyboardModifier_ControlModifier) != 0;
        bool shift = (modifiers & XKeyboardModifier_ShiftModifier) != 0;
        switch (key) {
        case XKey_Left:  deltaCol = -1; break;
        case XKey_Right: deltaCol = 1; break;
        case XKey_Up:    deltaRow = -1; break;
        case XKey_Down:  deltaRow = 1; break;
        case XKey_PageUp:
            pageRows = XWidget_height(self) / 24;
            deltaRow = -(pageRows > 1 ? pageRows : 1);
            break;
        case XKey_PageDown:
            pageRows = XWidget_height(self) / 24;
            deltaRow = (pageRows > 1 ? pageRows : 1);
            break;
        case XKey_Home:
            deltaCol = ctrl ? -cols : -1;
            if (ctrl) deltaRow = -rows;
            break;
        case XKey_End:
            deltaCol = ctrl ? cols : 1;
            if (ctrl) deltaRow = rows;
            break;
        default:
            break;
        }
        if (deltaRow != 0 || deltaCol != 0) {
            int r = view->m_currentRow >= 0 ? view->m_currentRow : 0;
            int c = view->m_currentColumn >= 0 ? view->m_currentColumn : 0;
            if (rows > 0 && r >= rows) r = rows - 1;
            if (cols > 0 && c >= cols) c = cols - 1;
            r += deltaRow;
            c += deltaCol;
            if (r < 0) r = 0;
            if (c < 0) c = 0;
            if (rows > 0 && r >= rows) r = rows - 1;
            if (cols > 0 && c >= cols) c = cols - 1;
            if (rows <= 0 || cols <= 0) { XEvent_ignore(event); return; }
            if (ctrl && !shift) {
                xaiv_setCurrentPreservingSelection(view, r, c);
            } else if (shift &&
                       view->m_selectionMode !=
                           XAbstractItemViewSelectionMode_NoSelection) {
                int ar = view->m_selectionAnchorRow;
                int ac = view->m_selectionAnchorCol;
                if (ar < 0 || ac < 0) {
                    ar = view->m_currentRow >= 0 ? view->m_currentRow : 0;
                    ac = view->m_currentColumn >= 0
                             ? view->m_currentColumn
                             : 0;
                }
                xaiv_selectRect(view, ar, ac, r, c);
                view->m_currentRow = r;
                view->m_currentColumn = c;
                if (view->m_selectionModel)
                    XItemSelectionModel_setCurrentIndex(
                        view->m_selectionModel, r, c);
                XWidget_update((XWidget*)view);
            } else if (view->m_selectionMode !=
                       XAbstractItemViewSelectionMode_NoSelection) {
                XAbstractItemView_setCurrentIndex(view, r, c);
                view->m_selectionAnchorRow = r;
                view->m_selectionAnchorCol = c;
            } else {
                xaiv_setCurrentPreservingSelection(view, r, c);
            }
            XEvent_accept(event);
            return;
        }
    }
    /* 可打印 ASCII（0x20..0x7e）且无 Ctrl/Alt/Meta 修饰 → 键盘搜索。
     * 带修饰键的组合（快捷键）不参与搜索，交回父类/忽略。 */
    if (key >= XKey_Space && key <= XKey_AsciiTilde &&
        (modifiers & (XKeyboardModifier_ControlModifier |
                      XKeyboardModifier_AltModifier |
                      XKeyboardModifier_MetaModifier)) == 0) {
        char text[2];
        /* AnyKeyPressed 触发位（对标 Qt 键入即编辑；Qt 的
         * QTableView/QTreeView 缺省触发位即含 AnyKeyPressed）：打开
         * 当前项编辑器并转发本次按键给编辑器（键入字符进入编辑器）；
         * 开启失败（不可编辑/无当前项）时回落键盘搜索——同 Qt 在编辑
         * 未开启时不吞键入搜索。 */
        if (view->m_itemsEditable &&
            (view->m_editTriggers &
             XAbstractItemViewEditTrigger_AnyKeyPressed) &&
            view->m_currentRow >= 0 && view->m_currentColumn >= 0 &&
            XAbstractItemView_edit(view, view->m_currentRow,
                                   view->m_currentColumn)) {
            XCoreApplication_sendEvent((XObject*)view->m_editor, event);
            XEvent_accept(event);
            return;
        }
        if (view->m_keyboardSearch) {
            text[0] = (char)key;
            text[1] = '\0';
            XAbstractItemView_keyboardSearch_2(view, text);
        }
        XEvent_accept(event);
        return;
    }
}

/* ==================== 信号 ==================== */

void* XAbstractItemView_pressed_signal(XAbstractItemView* self,
                                       int row, int col)
{
    xaiv_emitIndex(self, (size_t)XAbstractItemView_pressed_signal, row, col);
    return (void*)(size_t)XAbstractItemView_pressed_signal;
}

void* XAbstractItemView_clicked_signal(XAbstractItemView* self,
                                       int row, int col)
{
    xaiv_emitIndex(self, (size_t)XAbstractItemView_clicked_signal, row, col);
    return (void*)(size_t)XAbstractItemView_clicked_signal;
}

void* XAbstractItemView_doubleClicked_signal(XAbstractItemView* self,
                                             int row, int col)
{
    xaiv_emitIndex(self, (size_t)XAbstractItemView_doubleClicked_signal,
                   row, col);
    return (void*)(size_t)XAbstractItemView_doubleClicked_signal;
}

void* XAbstractItemView_activated_signal(XAbstractItemView* self,
                                         int row, int col)
{
    xaiv_emitIndex(self, (size_t)XAbstractItemView_activated_signal,
                   row, col);
    return (void*)(size_t)XAbstractItemView_activated_signal;
}

void* XAbstractItemView_entered_signal(XAbstractItemView* self,
                                       int row, int col)
{
    xaiv_emitIndex(self, (size_t)XAbstractItemView_entered_signal, row, col);
    return (void*)(size_t)XAbstractItemView_entered_signal;
}

void* XAbstractItemView_viewportEntered_signal(XAbstractItemView* self)
{
    xaiv_emitVoid(self, (size_t)XAbstractItemView_viewportEntered_signal);
    return (void*)(size_t)XAbstractItemView_viewportEntered_signal;
}

void* XAbstractItemView_iconSizeChanged_signal(XAbstractItemView* self,
                                               int width, int height)
{
    XVarList* args;
    (void)width; (void)height;
    args = XVarList_Create(XVar(int, width), XVar(int, height));
    if (!args) return (void*)(size_t)XAbstractItemView_iconSizeChanged_signal;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self,
                           (size_t)XAbstractItemView_iconSizeChanged_signal,
                           args, NULL, NULL, XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
    return (void*)(size_t)XAbstractItemView_iconSizeChanged_signal;
}

#endif /* XWIDGET_ON && XTABLEWIDGET_ON */
