#if XGUI_ON

#include "XAbstractItemView.h"

#include "XAlgorithm.h"
#include "XStringUtils.h"
#include "XDateTime.h"
#include "XMemory.h"
#include "XClass.h"
#include "XVarList.h"
#include "XVector.h"
#include "XEvent.h"
#include "XEventType.h"
#include "XItemDelegate.h"
#include "XCommonStyle.h" /* [省略消费 2026-11-03] XCommonStyle_elideText。 */
#include "XCoreApplication.h"
#include "XVariant.h" /* 输入法查询槽的 XVariant 载荷（P3-15）。 */
#if XWINDOWEVENT_ON && XAPPLICATION_ON && XGUIAPPLICATION_ON
#include "XApplication.h" /* §B6：XApplication_startDragDistance（拖出阈值，
                           * 对标 QApplication::startDragDistance）。与
                           * XWidget.c:3485-3494 同款条件包含——裁剪档不引入
                           * 该符号，避免 implicit declaration 与链接期未定义。 */
#endif

#if XWIDGET_ON && XTABLEWIDGET_ON

static void VXAbstractItemView_deinit(XAbstractItemView* self);
static bool VXAbstractItemView_visualRect(const XAbstractItemView* self,
                                          int row, int col, XRect* out);
static bool VXAbstractItemView_shouldEdit(const XAbstractItemView* self,
                                          int row, int col);
static void VXAbstractItemView_focusInEvent(XWidget* self, XEvent* event);
static void VXAbstractItemView_mousePressEvent(XWidget* self, XEvent* event);
static void VXAbstractItemView_mouseReleaseEvent(XWidget* self,
                                                 XEvent* event);
static void VXAbstractItemView_mouseDoubleClickEvent(XWidget* self,
                                                     XEvent* event);
static void VXAbstractItemView_mouseMoveEvent(XWidget* self, XEvent* event);
static void VXAbstractItemView_timerEvent(XObject* object, XTimerEvent* event);
static void VXAbstractItemView_keyPressEvent(XWidget* self, XEvent* event);
static void VXAbstractItemView_leaveEvent(XWidget* self, XEvent* event);
#if XWINDOWEVENT_ON
static void VXAbstractItemView_dragEnterEvent(XWidget* self, XEvent* event);
static void VXAbstractItemView_dragMoveEvent(XWidget* self, XEvent* event);
static void VXAbstractItemView_dragLeaveEvent(XWidget* self, XEvent* event);
static void VXAbstractItemView_dropEvent(XWidget* self, XEvent* event);
static void xaiv_startDrag(XAbstractItemView* view, int supportedActions);
static bool xaiv_hasDraggableSelection(const XAbstractItemView* view);
static int xaiv_startDragDistance(void);
#endif /* XWINDOWEVENT_ON */
static bool VXAbstractItemView_indexAt(const XAbstractItemView* self,
                                       int x, int y,
                                       int* outRow, int* outCol);
static void VXAbstractItemView_setModel(XAbstractItemView* self,
                                        XAbstractItemModel* model);

static void xaiv_dataChangedSlot(XObject* receiver, XVarList* args);
static void xaiv_modelRefreshSlot(XObject* receiver, XVarList* args);

/** @brief 仅移动当前索引（不触碰选择集合；对标 Ctrl+方向键的移动）。 */
static void xaiv_setCurrentPreservingSelection(XAbstractItemView* view,
                                               int row, int col)
{
    if (!view) return;
    view->m_currentRow = row;
    view->m_currentColumn = col;
    if (view->m_selectionModel)
        XItemSelectionModel_setCurrentIndex(
            view->m_selectionModel, row, col,
            XItemSelectionModelSelectionFlag_NoUpdate);
    XWidget_update((XWidget*)view);
}

/** @brief 选择行为标志（对标 Qt
 *        QAbstractItemViewPrivate::selectionBehaviorFlags，
 *        qabstractitemview_p.h:128-134）：SelectRows→Rows、
 *        SelectColumns→Columns、SelectItems→NoUpdate）。 */
static int xaiv_behaviorFlags(const XAbstractItemView* view)
{
    if (!view) return XItemSelectionModelSelectionFlag_NoUpdate;
    switch (view->m_selectionBehavior) {
    case XAbstractItemViewSelectionBehavior_SelectRows:
        return XItemSelectionModelSelectionFlag_Rows;
    case XAbstractItemViewSelectionBehavior_SelectColumns:
        return XItemSelectionModelSelectionFlag_Columns;
    default:
        return XItemSelectionModelSelectionFlag_NoUpdate;
    }
}

/** @brief 选择命令分派（对标 Qt QAbstractItemView::selectionCommand(
 *        index, nullptr)，qabstractitemview.cpp:4140-4181）：
 *        - NoSelection → NoUpdate（绝不修改选择模型）；
 *        - SingleSelection → ClearAndSelect|behaviorFlags；
 *        - MultiSelection → multiSelectionCommand(index, nullptr) 尾段
 *          的 Toggle|behaviorFlags（qabstractitemview.cpp:4227）；
 *        - ExtendedSelection / ContiguousSelection → 尾段的
 *          ClearAndSelect|behaviorFlags（qabstractitemview.cpp:4277、
 *          4335）。
 *        索引无效（行/列 <0）时返回 NoUpdate：仅清当前项、不动选择
 *        集合（XListWidget/XTreeWidget 的 clear 路径依赖此口径——其
 *        在本调用之后显式清选择并按返回值发射 itemSelectionChanged）。 */
static int xaiv_selectionCommand(const XAbstractItemView* view, int row,
                                 int col)
{
    int flags;
    if (!view) return XItemSelectionModelSelectionFlag_NoUpdate;
    if (row < 0 || col < 0) return XItemSelectionModelSelectionFlag_NoUpdate;
    flags = xaiv_behaviorFlags(view);
    switch (view->m_selectionMode) {
    case XAbstractItemViewSelectionMode_NoSelection:
        return XItemSelectionModelSelectionFlag_NoUpdate;
    case XAbstractItemViewSelectionMode_MultiSelection:
        return XItemSelectionModelSelectionFlag_Toggle | flags;
    case XAbstractItemViewSelectionMode_SingleSelection:
    case XAbstractItemViewSelectionMode_ExtendedSelection:
    case XAbstractItemViewSelectionMode_ContiguousSelection:
    default:
        return XItemSelectionModelSelectionFlag_ClearAndSelect | flags;
    }
}

/** @brief 按选择命令施加于一个"单元"（行为标志由本视图的
 *        selectionBehavior 决定，整行/整列展开在选择模型内完成，
 *        对标 Qt 的 select(index, command|Rows|Columns)）。 */
static void xaiv_selectCell(XAbstractItemView* view, int row, int col,
                             int command)
{
    XItemSelectionModel* sm = view ? view->m_selectionModel : NULL;
    if (!sm || row < 0 || col < 0) return;
    XItemSelectionModel_select(sm, row, col, command |
                                          xaiv_behaviorFlags(view));
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
    /* 对标 Qt 橡皮筋选择：覆盖旧选区但**保留当前索引**
     * （clearSelection 语义，qitemselectionmodel.cpp:1369-1376）。 */
    XItemSelectionModel_clearSelection(sm);
    if (r1 > r2) { int t = r1; r1 = r2; r2 = t; }
    if (c1 > c2) { int t = c1; c1 = c2; c2 = t; }
    for (r = r1; r <= r2; ++r) {
        for (c = c1; c <= c2; ++c) {
            if (model && (r >= model->m_rows || c >= model->m_cols))
                continue;
            xaiv_selectCell(view, r, c,
                            XItemSelectionModelSelectionFlag_Select);
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

/* ==================== 悬停差分旁表（entered/viewportEntered 判重） ==================== */

/* 头文件无悬停字段的替代承载：视图 → 上次悬停索引 的旁表
 * （对照 XListWidget m_enteredRow 差分范式，行列成对并含失效态）。
 * 条目仅随首次悬停插入、随视图析构移除（VXAbstractItemView_deinit
 * 为全路径唯一析构入口）与 LeaveEvent 复位，无泄漏与悬垂键；
 * 存储复用库内 XVector（元素为键值结构，非自写链表/裸数组）。 */
typedef struct XAivHoverEntry
{
    const XAbstractItemView* m_view; /**< 键（视图借用指针）。 */
    int m_row;                       /**< 上次悬停行；-1=悬停失效（空白/未知）。 */
    int m_col;                       /**< 上次悬停列；-1=同上。 */
} XAivHoverEntry;

/**
 * @brief 单格 role 叠加条目。
 * @note  Qt 以 model data(index, role) 承载各 role；本库模型通路
 *        （XAbstractItemModel data/setData）无 role 参数仅承载文本，
 *        非文本 role 由视图基类按 (row,col) 叠加存储（基类扩展，
 *        XItemDataRole 语义不变）。文本 role（Display/Edit）不经此表，
 *        直连模型文本通路。条目为 POD（借用指针承载），随 role 表
 *        XVector 的 resize 补零/搬移。
 */
typedef struct XAivRoleEntry
{
    int m_checkState;          /**< CheckStateRole（XItemCheckState；-1=未设置）。 */
    int m_alignment;           /**< TextAlignmentRole（XAlignment 位组合；0=未设置）。 */
    const XFont* m_font;       /**< FontRole（借用指针；NULL=未设置）。 */
    const void* m_decoration;  /**< DecorationRole（借用指针；NULL=未设置）。 */
} XAivRoleEntry;

static XVector xaiv_hoverEntries;

/** @brief 取悬停旁表（文件级静态存储，首次使用时初始化一次）。 */
static XVector* xaiv_hoverTable(void)
{
    if (XClassIsVtableNull(&xaiv_hoverEntries))
        XVector_init(&xaiv_hoverEntries, sizeof(XAivHoverEntry), false);
    return &xaiv_hoverEntries;
}

/** @brief 读视图的上次悬停索引；无记录时保持出参 (-1,-1)。 */
static void xaiv_hoverGet(const XAbstractItemView* view, int* row, int* col)
{
    XVector* table = xaiv_hoverTable();
    size_t i;
    size_t n = XVector_size_base(table);
    for (i = 0; i < n; ++i) {
        const XAivHoverEntry* e =
            (const XAivHoverEntry*)XVector_at_base(table, (int64_t)i);
        if (e->m_view == view) {
            *row = e->m_row;
            *col = e->m_col;
            return;
        }
    }
}

/** @brief 写视图的上次悬停索引（无记录则尾插建条目；插入失败忽略——
 *         下次判定仍按无记录基准，最多退化为重复发射，不误吞信号）。 */
static void xaiv_hoverSet(const XAbstractItemView* view, int row, int col)
{
    XVector* table = xaiv_hoverTable();
    XAivHoverEntry entry;
    size_t i;
    size_t n = XVector_size_base(table);
    for (i = 0; i < n; ++i) {
        XAivHoverEntry* e = (XAivHoverEntry*)XVector_at_base(table,
                                                             (int64_t)i);
        if (e->m_view == view) {
            e->m_row = row;
            e->m_col = col;
            return;
        }
    }
    entry.m_view = view;
    entry.m_row = row;
    entry.m_col = col;
    XVector_push_back_1_base(table, &entry);
}

/** @brief 移除视图的悬停记录（LeaveEvent 复位与析构路径）。 */
static void xaiv_hoverRelease(const XAbstractItemView* view)
{
    XVector* table = xaiv_hoverTable();
    size_t i;
    size_t n = XVector_size_base(table);
    for (i = 0; i < n; ++i) {
        const XAivHoverEntry* e =
            (const XAivHoverEntry*)XVector_at_base(table, (int64_t)i);
        if (e->m_view == view) {
            XVector_removeAt_base(table, (int64_t)i);
            return;
        }
    }
}

static void xaiv_persistentRelease(XAbstractItemView* self);
static void xaiv_indexWidgetRelease(XAbstractItemView* self);
static void xaiv_delegateRelease(XAbstractItemView* self);
static void xaiv_persistentResetTable(XAbstractItemView* self);
static void xaiv_indexWidgetResetTable(XAbstractItemView* self);
static void xaiv_roleRelease(XAbstractItemView* self);

#if XINPUTMETHOD_ON
/**
 * @brief 输入法查询虚槽（对标 QAbstractItemView 视口内的焦点代理口径）。
 *
 * @details 两段语义：
 *          ① **编辑中**：查询转发当前临时编辑器（`m_editor`，由委托
 *             createEditor 产物，编辑闭环已维护）。Qt 的焦点此时在编辑器
 *             上，平台输入法拿到的是编辑器的光标矩形/环绕文本；本库壳
 *             重载虚槽后同样把查询交给编辑器自己的
 *             `XWidget_inputMethodQuery_base`（虚分派，编辑器子类重载生效）。
 *          ② **未编辑**：`ImCursorRectangle` 按当前索引的
 *             `visualRect`（条目几何）给出光标矩形——条目选择态下的输入法
 *             候选窗锚点应指向当前条目；无当前索引/几何不可得时链回
 *             基类缺省（`XClass_Parent(XWidget, EXWidget_InputMethodQuery)`
 *             = `XWidget_inputMethodQuery_default`：Qt 的
 *             QRect(width()/2, 0, 1, height())）。
 *          其余查询项一律链回基类缺省（Qt 基类同样只实现这几项）。
 *
 * @note      `XWidget_inputMethodQuery_base` 的 ImEnabled 基类实现读
 *            WA_InputMethodEnabled；视图壳不置该位 ⇒ 未编辑时 ImEnabled
 *            为 false（Qt 的 QAbstractItemView 不覆写 inputMethodQuery，
 *            同样不给 ImEnabled 真值）。编辑中转发编辑器时以编辑器的
 *            属性位为准，矩形也是**编辑器局部坐标**（Qt 里此时焦点对象
 *            就是编辑器，平台输入法拿到的正是编辑器坐标系；本槽只是让
 *            「焦点在视图上却问视图」的调用方也拿到编辑器口径，不另行
 *            做坐标平移）。
 */
static XVariant* VXAbstractItemView_inputMethodQuery(const XWidget* self,
                                                     XInputMethodQuery query)
{
    const XAbstractItemView* view = (const XAbstractItemView*)self;
    if (!view) return NULL;
    if (view->m_editor)
        return XWidget_inputMethodQuery_base(view->m_editor, query);
    if (query == XInputMethodQuery_ImCursorRectangle &&
        view->m_currentRow >= 0 && view->m_currentColumn >= 0) {
        XRect rect;
        if (XAbstractItemView_visualRect_base(view, view->m_currentRow,
                                         view->m_currentColumn, &rect) &&
            rect.width > 0 && rect.height > 0) {
            XRectF rectF;
            rectF.x = (float)rect.x;
            rectF.y = (float)rect.y;
            rectF.width = (float)rect.width;
            rectF.height = (float)rect.height;
            return XVariant_create(&rectF, sizeof(rectF), XVariantType_User);
        }
    }
    return XClass_Parent(XWidget, EXWidget_InputMethodQuery,
                         XWidgetInputMethodQuerySlot)(self, query);
}
#endif /* XINPUTMETHOD_ON */

XVtable* XAbstractItemView_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XAbstractItemView)
    XVTABLE_INHERIT_XCLASS(XAbstractScrollArea);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXAbstractItemView_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_FocusInEvent,
                             VXAbstractItemView_focusInEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MousePressEvent,
                             VXAbstractItemView_mousePressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseReleaseEvent,
                             VXAbstractItemView_mouseReleaseEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseDoubleClickEvent,
                             VXAbstractItemView_mouseDoubleClickEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseMoveEvent,
                             VXAbstractItemView_mouseMoveEvent);
    /* [自动滚动 2026-11-03] 边缘自动滚动拍（对齐 QAbstractItemView 的
     * autoScrollTimer → doAutoScroll）。 */
    XVTABLE_OVERLOAD_DEFAULT(EXObject_TimerEvent,
                             VXAbstractItemView_timerEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_KeyPressEvent,
                             VXAbstractItemView_keyPressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_LeaveEvent,
                             VXAbstractItemView_leaveEvent);
#if XWINDOWEVENT_ON
    /* §B6 拖放目标侧四槽（对标 Qt dragEnter/Move/Leave/Drop）。投递由
     * XWidget_dispatchDragDropEvent 定位到 registered acceptDrops 目标后
     * 经 XWidget 基类分发到这四个虚槽；本视图重载后据模型 acceptDrops +
     * mimeTypes 决定接受、计算落点指示矩形与落点位置。 */
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_DragEnterEvent,
                             VXAbstractItemView_dragEnterEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_DragMoveEvent,
                             VXAbstractItemView_dragMoveEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_DragLeaveEvent,
                             VXAbstractItemView_dragLeaveEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_DropEvent,
                             VXAbstractItemView_dropEvent);
#endif /* XWINDOWEVENT_ON */
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractItemView_IndexAt,
                             VXAbstractItemView_indexAt);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractItemView_VisualRect,
                             VXAbstractItemView_visualRect);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractItemView_ShouldEdit,
                             VXAbstractItemView_shouldEdit);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractItemView_SetModel,
                             VXAbstractItemView_setModel);
#if XINPUTMETHOD_ON
    /* 输入法查询虚槽（P3-15）：编辑中转发编辑器、未编辑按 visualRect
       给光标矩形、其余链回基类缺省。 */
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_InputMethodQuery,
                             VXAbstractItemView_inputMethodQuery);
#endif /* XINPUTMETHOD_ON */
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
    self->m_selectionMode = (uint8_t)XAbstractItemViewSelectionMode_ExtendedSelection;
    self->m_selectionBehavior =
        XAbstractItemViewSelectionBehavior_SelectItems;
    /* 对标 Qt 6.8 QAbstractItemView::editTriggers 缺省
     * DoubleClicked|EditKeyPressed（文档：除 QTableView 外全部视图的
     * 默认值；QTableView/QTreeView 为 DoubleClicked|AnyKeyPressed）。
     * 此前多出 CurrentChanged 使单击按压即进编辑，非 Qt 默认行为。 */
    self->m_editTriggers = (uint8_t)(XAbstractItemViewEditTrigger_DoubleClicked |
        XAbstractItemViewEditTrigger_EditKeyPressed);
    self->m_alternatingRowColors = false;
    self->m_autoScroll = true;
    self->m_model = NULL;
    self->m_selectionModel = XItemSelectionModel_create();
    self->m_selectionAnchorRow = -1;
    self->m_selectionAnchorCol = -1;
    /* [自动滚动 2026-11-03] 拖选/拖放边缘自动滚动状态。 */
    self->m_dragPosX = -1;
    self->m_dragPosY = -1;
    self->m_autoScrollTimerId = XTIMER_INVALID_ID;
    self->m_dragSelecting = 0;
    self->m_autoScrollCount = 0;
    self->m_rootRow = -1;
    self->m_rootCol = -1;
    self->m_iconW = 16; /* 小像素值，int16 承载。 */
    self->m_iconH = 16;
    self->m_tabKeyNavigation = false;
    self->m_autoScrollMargin = 16;
    self->m_showDropIndicator = true;
    self->m_defaultDropAction = 0; /* 0=IgnoreAction，对标 Qt::IgnoreAction。 */
    self->m_dragDropMode = (uint8_t)XAbstractItemViewDragDropMode_NoDragDrop;
    self->m_dragEnabled = false;
    self->m_dragDropOverwriteMode = false;
    /* [§B6 拖放状态机 2026-11-15] 拖放状态机与指示线初值：NoState / OnItem /
     * 无有效指示线矩形（XMemset 已归零，此处显式点名，规避位置初始化器
     * 串位风险——结构体铁律：新增字段尾部追加并用指定初始化器语义赋值）。 */
    self->m_state = (uint8_t)XAbstractItemViewDragState_NoState;
    self->m_dropIndicatorPosition =
        (uint8_t)XAbstractItemViewDropIndicatorPosition_OnItem;
    self->m_hasDropRect = 0;
    self->m_dropRectX = 0;
    self->m_dropRectY = 0;
    self->m_dropRectW = 0;
    self->m_dropRectH = 0;
    self->m_dragPressX = 0;
    self->m_dragPressY = 0;
    self->m_textElideMode = (uint8_t)XAbstractItemViewTextElideMode_ElideRight;
    self->m_verticalScrollMode =
        XAbstractItemViewScrollMode_ScrollPerItem;
    self->m_horizontalScrollMode =
        XAbstractItemViewScrollMode_ScrollPerItem;
    self->m_itemDelegate = NULL;
    /* 平行表（持久编辑器标记/条目控件/role 叠加）与行/列级委托表：
     * 存储一律由库内 XVector 承载（对象拥有，元素 POD，无元素回调）。 */
    XVector_init(&self->m_persistentFlags, sizeof(bool), false);
    self->m_persistentRows = 0;
    self->m_persistentCols = 0;
    XVector_init(&self->m_indexWidgets, sizeof(void*), false);
    self->m_indexWidgetRows = 0;
    self->m_indexWidgetCols = 0;
    XVector_init(&self->m_columnDelegates, sizeof(void*), false);
    XVector_init(&self->m_rowDelegates, sizeof(void*), false);
    /* 编辑闭环会话状态（对标 Qt 视图内部 editor/editorIndex 会话）。 */
    self->m_editor = NULL;
    self->m_editRow = -1;
    self->m_editCol = -1;
    self->m_editCommitted = false;
    self->m_editClosing = false;
    self->m_itemsEditable = false;
    self->m_defaultDelegate = NULL;
    XVector_init(&self->m_roleTable, sizeof(XAivRoleEntry), false);
    self->m_roleRows = 0;
    self->m_roleCols = 0;
    /* 对标 Qt QAbstractScrollArea 构造函数 qabstractscrollarea.cpp:273
     * 的 setFocusPolicy(Qt::StrongFocus)：条目视图可经 Tab 与鼠标点击
     * 获得键盘焦点，方向键导航（keyPressEvent）才可达（修复点击选中
     * 后 Down/Up 方向键不动当前项——焦点滞留原处、键事件不达视图）。 */
    XWidget_setFocusPolicy((XWidget*)self, XWidgetFocusPolicy_StrongFocus);
    /* 悬停外观 opt-in 不声明（脏区收窄 2026-10-15）：条目视图族没有任何
     * paintEvent 消费悬停态（entered/viewportEntered 信号由 mouseMove
     * 差分直接发射，与本位无关）——声明 Hover 只会让指针每次进出视图
     * 触发 ENTER/LEAVE 整件标脏（真机大表=视口带整刷，触摸阵发大头
     * 之一）。若未来引入悬停行高亮，改为差分行带 updateRect 而非整件。 */
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
    /* [自动滚动 2026-11-03] 析构前必须停表：周期定时器只覆盖 id 不 kill
     * = 定时器泄漏（分发器 startTimer 恒 singleShot=false，到期重挂）。 */
    if (self->m_autoScrollTimerId != XTIMER_INVALID_ID) {
        XObject_killTimer((XObject*)self, self->m_autoScrollTimerId);
        self->m_autoScrollTimerId = XTIMER_INVALID_ID;
    }
    if (!self) return;
    /* 悬停差分旁表节点随视图析构移除（防悬垂键；旁表头为静态承载）。 */
    xaiv_hoverRelease(self);
    /* 平行表（持久编辑器标记/条目控件指针）随视图析构释放（全路径唯一
     * 析构入口；派生视图经 XClass_Deinit_Parent 分派至此）。 */
    xaiv_persistentRelease(self);
    xaiv_indexWidgetRelease(self);
    xaiv_delegateRelease(self);
    xaiv_roleRelease(self);
    /* 内嵌平行表（持久编辑器标记/条目控件/行级·列级委托/role 叠加）的
     * **存储本体**随视图析构释放：上面的 *Release 只清内容并保留容量
     * （setModel 复用的需要），若不在此补一次容器析构，XVector 的堆块
     * 就随对象一起漏掉（实测每视图 24 字节，第二轮仍增长）。五个成员
     * 是本对象内嵌值，父类析构不会代管。 */
    XClassDeinit(&self->m_persistentFlags);
    XClassDeinit(&self->m_indexWidgets);
    XClassDeinit(&self->m_columnDelegates);
    XClassDeinit(&self->m_rowDelegates);
    XClassDeinit(&self->m_roleTable);
    /* 编辑器为视图子控件：随控件树统一析构（若已 deleteLater 挂起，
     * 其析构会撤销挂起事件，双路均安全）；此处仅清引用。 */
    self->m_editor = NULL;
    self->m_editRow = -1;
    self->m_editCol = -1;
    /* 默认委托为视图拥有的独立对象（无父对象）：显式析构。 */
    if (self->m_defaultDelegate) {
        XClassDelete(self->m_defaultDelegate);
        self->m_defaultDelegate = NULL;
    }
    if (self->m_selectionModel) {
        XClassDelete(self->m_selectionModel);
        self->m_selectionModel = NULL;
    }
    XClass_Deinit_Parent(XAbstractScrollArea, (XAbstractScrollArea*)self);
}

void XAbstractItemView_setCurrentIndex(XAbstractItemView* self, int row,
                                       int column)
{
    XItemSelectionModel* selection;
    int command;
    if (!self) return;
    self->m_currentRow = row;
    self->m_currentColumn = column;
    /* SelectCurrent 语义：同步选择模型当前索引；非 NoSelection 且索引
     * 有效时按模式分派选择命令（对标 Qt
     * QAbstractItemView::setCurrentIndex(index) →
     * selectionCommand(index, nullptr) 的模式分派，
     * qabstractitemview.cpp:1098-1106 + 4140-4181：
     *   - Single/Extended/Contiguous → ClearAndSelect|behaviorFlags
     *     （先清空旧选区再选中当前项——修复列表/视图点击换行后旧行
     *     高亮残留的双选缺陷）；
     *   - MultiSelection → multiSelectionCommand(index, nullptr) 尾段
     *     返回 Toggle|behaviorFlags（qabstractitemview.cpp:4227）：
     *     已选中则取消、未选中则追加，不清空既有选择）。
     * 索引无效（-1）仅清当前项、不动选择集合：XListWidget/XTreeWidget
     * 的 clear 路径在本调用之后显式 XItemSelectionModel_clear 并依赖
     * 其返回值发射 itemSelectionChanged，此处不得抢清。 */
    selection = self->m_selectionModel;
    if (!selection) return;
    command = xaiv_selectionCommand(self, row, column);
    XItemSelectionModel_setCurrentIndex(selection, row, column, command);
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
        /* 对标 Qt reset：选择模型整体复位（清选择 + 清当前索引，
         * qitemselectionmodel.cpp:1335-1339）。 */
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

void XAbstractItemView_update(XAbstractItemView* self, int row, int col)
{
    XWidget* viewport;
    XRect rect;
    if (!self || row < 0 || col < 0) return;
    /* 对标 QAbstractItemView::update(const QModelIndex&)（Qt 6.8
     * qabstractitemview.cpp）：条目可视矩形与视口相交才做局部重绘
     * （避免不可见条目的无效脏区合并，Qt 同款性能门禁）。 */
    if (!XAbstractItemView_visualRect_base(self, row, col, &rect)) return;
    viewport = XAbstractScrollArea_viewport((XAbstractScrollArea*)self);
    if (!viewport) {
        /* 本库视口懒建：无视口时退化为整控件重绘请求（Qt 无此分支）。 */
        XWidget_update((XWidget*)self);
        return;
    }
    {
        XRect vr;
        XRect_init(&vr, 0, 0, XWidget_width(viewport),
                   XWidget_height(viewport));
        if (rect.x + rect.width < 0 || rect.y + rect.height < 0 ||
            rect.x > vr.x + vr.width || rect.y > vr.y + vr.height)
            return;
    }
    XWidget_updateRect(viewport, &rect);
}

void XAbstractItemView_setSelectionMode(XAbstractItemView* self, int mode)
{ if (self) self->m_selectionMode = (uint8_t)mode; }
int XAbstractItemView_selectionMode(const XAbstractItemView* self)
{ return self ? self->m_selectionMode : 0; }

void XAbstractItemView_setSelectionBehavior(XAbstractItemView* self,
                                            int behavior)
{ if (self) self->m_selectionBehavior = (uint8_t)behavior; }
int XAbstractItemView_selectionBehavior(const XAbstractItemView* self)
{ return self ? self->m_selectionBehavior : 0; }

void XAbstractItemView_setEditTriggers(XAbstractItemView* self, int triggers)
{ if (self) self->m_editTriggers = (uint8_t)triggers; }
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
        XObject_disconnect(d, (size_t)XItemDelegate_commitData_signal,
                             (XObject*)self, xaiv_delegateCommitSlot);
        XObject_disconnect(d, (size_t)XItemDelegate_closeEditor_signal,
                             (XObject*)self, xaiv_delegateCloseSlot);
    }
    XObject_connect(d, (size_t)XItemDelegate_commitData_signal,
                      (XObject*)self, xaiv_delegateCommitSlot,
                      XConnectionType_Direct);
    XObject_connect(d, (size_t)XItemDelegate_closeEditor_signal,
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
    XItemDelegate_setModelData_base(delegate, self->m_editor, self,
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
    /* 同 Qt edit(index, AllEditTriggers, nullptr)（qabstractitemview.cpp:
     * 1231-1238 公共槽显式以 AllEditTriggers 调三参版；shouldEdit :4390-4391
     * `trigger == AllEditTriggers` 直接 force editing）——**程序化 edit
     * 不受 editTriggers 拦截**（K-51 定案对齐）：是否真开编辑器由
     * ShouldEdit（itemsEditable 总闸 ∪ 模型 flags ItemIsEditable，见
     * VXAbstractItemView_shouldEdit）决定，editTriggers 只管**交互触发**
     * （双击/单击/按键路径先按位与过滤后才调本入口）。2026-11-15 起
     * 移除旧版 `m_editTriggers == NoEditTriggers ⇒ false` 门禁，旧锁
     * xgui_demo_apitest_views.c 同款断言已按「救旧锁附 Qt 原文」改判。 */
    {
        bool (*shouldEdit)(const XAbstractItemView*, int, int) =
            (bool (*)(const XAbstractItemView*, int, int))
            XVtableGetFunc(XClassGetVtable((XClass*)self),
                           EXAbstractItemView_ShouldEdit, void*);
        if (!shouldEdit || !shouldEdit(self, row, column)) return false;
    }
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
    editor = XItemDelegate_createEditor_base(delegate, self, row, column);
    if (!editor) return false;
    /* 先滚动保证目标格可见再取几何（滚动偏移影响 visualRect 坐标）。 */
    XAbstractItemView_scrollTo(self, row, column);
    if (!XAbstractItemView_visualRect_base(self, row, column, &rect) ||
        rect.width <= 0 || rect.height <= 0) {
        /* 条目不可见（隐藏行/列/空几何）：不开编辑器（同 Qt 不可见
         * 条目无条目矩形可布局）。 */
        XObject_deleteLater((XObject*)editor);
        return false;
    }
    XItemDelegate_setEditorData_base(delegate, editor, self, row, column);
    XItemDelegate_updateEditorGeometry_base(delegate, editor, &rect);
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
    /* 委托以借用指针承载，参与**编辑路径**的委托解析
       （行级 → 列级 → 本指针 → 内建默认委托，见 xaiv_resolveDelegate）。
       绘制面是否消费由各派生视图的 paintEvent 自行决定（本基类无 paintEvent）。
       同 Qt 委托变化后刷新视口（本库简化为整个视图 update）。 */
    self->m_itemDelegate = delegate;
    XWidget_update((XWidget*)self);
}

void* XAbstractItemView_itemDelegate(const XAbstractItemView* self)
{ return self ? self->m_itemDelegate : NULL; }

/* ==================== 行/列级委托（不透明承载） ==================== */

/**
 * @brief 按需扩容委托指针表使 index 可直接寻址（只增不减；新增槽位由
 *        XVector resize 补 NULL，保留旧内容）。
 *
 * @param table 委托指针向量。
 * @param index 需可写的下标（需 >=0）。
 * @return 已满足或扩容成功返回 true；参数非法或分配失败返回 false
 *         （失败时不改动旧表内容）。
 */
static bool xaiv_delegateEnsure(XVector* table, int index)
{
    if (index < 0) return false;
    if ((size_t)index < XVector_size_base(table)) return true;
    return XVector_resize_base(table, (size_t)index + 1);
}

void XAbstractItemView_setItemDelegateForColumn(XAbstractItemView* self,
                                                int column, void* delegate)
{
    void** slot;
    if (!self || column < 0) return;
    if (!xaiv_delegateEnsure(&self->m_columnDelegates, column)) return;
    slot = (void**)XVector_at_base(&self->m_columnDelegates, column);
    if (*slot == delegate) return;
    /* 委托体系未建：仅存指针（借用，不析构、不虚分派）；NULL=清除该列
     * 覆盖回落默认委托。同 Qt 委托变化后刷新视口。 */
    *slot = delegate;
    XWidget_update((XWidget*)self);
}

void* XAbstractItemView_itemDelegateForColumn(const XAbstractItemView* self,
                                              int column)
{
    if (!self || column < 0) return NULL;
    if ((size_t)column >= XVector_size_base(&self->m_columnDelegates))
        return NULL;
    return *(void**)XVector_at_base(&self->m_columnDelegates, column);
}

void XAbstractItemView_setItemDelegateForRow(XAbstractItemView* self,
                                             int row, void* delegate)
{
    void** slot;
    if (!self || row < 0) return;
    if (!xaiv_delegateEnsure(&self->m_rowDelegates, row)) return;
    slot = (void**)XVector_at_base(&self->m_rowDelegates, row);
    if (*slot == delegate) return;
    /* 行级优先于列级（同 Qt itemDelegate(index) 解析顺序）；当前仅承载。 */
    *slot = delegate;
    XWidget_update((XWidget*)self);
}

void* XAbstractItemView_itemDelegateForRow(const XAbstractItemView* self,
                                           int row)
{
    if (!self || row < 0) return NULL;
    if ((size_t)row >= XVector_size_base(&self->m_rowDelegates)) return NULL;
    return *(void**)XVector_at_base(&self->m_rowDelegates, row);
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

XItemDelegate* XAbstractItemView_resolveDelegate(XAbstractItemView* self,
                                                 int row, int col)
{
    /* 编辑器创建路径的委托解析入口：行级 → 列级 → 视图默认委托 →
     * 懒创建内建默认委托。返回 NULL 仅当 self 为 NULL。
     * （绘制面是否也用它由各派生视图 paintEvent 自行决定，本基类不绘制条目。） */
    return xaiv_resolveDelegate(self, row, col);
}

/* ==================== 平行表（持久编辑器标记 / 条目控件指针） ==================== */

/**
 * @brief 按需扩容行主序扁平平行表至 rows x cols（只增不减，保留旧内容）。
 *
 *        目标尺寸不大于当前尺寸时为空操作；否则经 XVector 扩容（新增
 *        区域补零），列数变化改变行距时从尾行向前逐元素搬到新行距位置
 *        （同行槽位整体右移、源槽位必在已写目标槽位之前，原地搬移安全；
 *        0 行为无位移）。搬移经 XMemmove：单元素源/目标可重叠。
 *
 * @param table 平行表向量。
 * @param curRows 当前逻辑行数（出/入参）。
 * @param curCols 当前逻辑列数（出/入参）。
 * @param rows 目标行数（需 >=1）。
 * @param cols 目标列数（需 >=1）。
 * @param elemSize 单元素字节数。
 * @return 已满足或扩容成功返回 true；参数非法或分配失败返回 false
 *         （失败时不改动旧表与逻辑维度）。
 */
static bool xaiv_tableEnsure(XVector* table, int* curRows, int* curCols,
                             int rows, int cols, size_t elemSize)
{
    char* base;
    int newRows;
    int newCols;
    size_t r;
    size_t c;
    size_t oldCols;
    if (rows <= *curRows && cols <= *curCols) return true;
    if (rows <= 0 || cols <= 0) return false;
    newRows = rows > *curRows ? rows : *curRows;
    newCols = cols > *curCols ? cols : *curCols;
    if (!XVector_resize_base(table, (size_t)newRows * (size_t)newCols))
        return false;
    if (*curRows > 0 && *curCols > 0 && newCols != *curCols) {
        base = (char*)XVector_at_base(table, 0);
        oldCols = (size_t)*curCols;
        for (r = (size_t)*curRows; r-- > 0; ) {
            for (c = oldCols; c-- > 0; ) {
                XMemmove(base + (r * (size_t)newCols + c) * elemSize,
                         base + (r * oldCols + c) * elemSize, elemSize);
            }
        }
    }
    *curRows = newRows;
    *curCols = newCols;
    return true;
}

/** @brief 清空持久编辑器打开标记（内容全部置 false；容量保留复用）。 */
static void xaiv_persistentResetTable(XAbstractItemView* self)
{
    size_t n = XVector_size_base(&self->m_persistentFlags);
    if (n > 0) {
        XMemset(XVector_at_base(&self->m_persistentFlags, 0), 0,
                n * sizeof(bool));
    }
}

/** @brief 清空条目控件承载（指针全部置 NULL；容量保留复用）。 */
static void xaiv_indexWidgetResetTable(XAbstractItemView* self)
{
    size_t n = XVector_size_base(&self->m_indexWidgets);
    if (n > 0) {
        XMemset(XVector_at_base(&self->m_indexWidgets, 0), 0,
                n * sizeof(void*));
    }
}

/** @brief 释放持久编辑器标记表并归零逻辑维度（setModel 行/列变化入口与析构同步）。 */
static void xaiv_persistentRelease(XAbstractItemView* self)
{
    XVector_clear_base(&self->m_persistentFlags);
    self->m_persistentRows = 0;
    self->m_persistentCols = 0;
}

/** @brief 释放条目控件表并归零逻辑维度（setModel 行/列变化入口与析构同步）。 */
static void xaiv_indexWidgetRelease(XAbstractItemView* self)
{
    XVector_clear_base(&self->m_indexWidgets);
    self->m_indexWidgetRows = 0;
    self->m_indexWidgetCols = 0;
}

/** @brief 释放行/列级委托指针表（仅析构同步；行/列级委托
 *         不随 setModel 失效，同 Qt 跨模型保留）。 */
static void xaiv_delegateRelease(XAbstractItemView* self)
{
    XVector_clear_base(&self->m_columnDelegates);
    XVector_clear_base(&self->m_rowDelegates);
}

/* ==================== role 叠加存储（对标 model data(role) 的非文本维度） ==================== */

/** @brief 清空 role 叠加存储并归零逻辑维度（setModel 行/列变化入口与析构同步）。 */
static void xaiv_roleRelease(XAbstractItemView* self)
{
    XVector_clear_base(&self->m_roleTable);
    self->m_roleRows = 0;
    self->m_roleCols = 0;
}

/** @brief 只读定位单格 role 条目（未使用、越界或 self 为空返回 NULL）。 */
static const XAivRoleEntry* xaiv_roleEntry(const XAbstractItemView* self,
                                           int row, int col)
{
    if (!self || row < 0 || col < 0 ||
        row >= self->m_roleRows || col >= self->m_roleCols)
        return NULL;
    return (const XAivRoleEntry*)XVector_at_base(
        &self->m_roleTable, (int64_t)row * self->m_roleCols + col);
}

/** @brief 定位（按需扩容）单格 role 条目；写入口专用。 */
static XAivRoleEntry* xaiv_roleEntryForWrite(XAbstractItemView* self,
                                             int row, int col)
{
    if (!self || row < 0 || col < 0) return NULL;
    if (!xaiv_tableEnsure(&self->m_roleTable, &self->m_roleRows,
                          &self->m_roleCols,
                          row + 1, col + 1, sizeof(XAivRoleEntry)))
        return NULL;
    return (XAivRoleEntry*)XVector_at_base(
        &self->m_roleTable, (int64_t)row * self->m_roleCols + col);
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
    bool* slot;
    if (!self || row < 0 || col < 0) return;
    if (!xaiv_tableEnsure(&self->m_persistentFlags, &self->m_persistentRows,
                          &self->m_persistentCols,
                          row + 1, col + 1, sizeof(bool)))
        return;
    slot = (bool*)XVector_at_base(
        &self->m_persistentFlags, (int64_t)row * self->m_persistentCols + col);
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
    if (row >= self->m_persistentRows || col >= self->m_persistentCols)
        return;
    slot = (bool*)XVector_at_base(
        &self->m_persistentFlags, (int64_t)row * self->m_persistentCols + col);
    if (*slot) {
        *slot = false;
        XWidget_update((XWidget*)self);
    }
}

bool XAbstractItemView_isPersistentEditorOpen(const XAbstractItemView* self,
                                              int row, int col)
{
    if (!self || row < 0 || col < 0) return false;
    if (row >= self->m_persistentRows || col >= self->m_persistentCols)
        return false;
    return *(const bool*)XVector_at_base(
        &self->m_persistentFlags, (int64_t)row * self->m_persistentCols + col);
}

void XAbstractItemView_setIndexWidget(XAbstractItemView* self,
                                      int row, int col, void* widget)
{
    void** slot;
    if (!self || row < 0 || col < 0) return;
    if (!xaiv_tableEnsure(&self->m_indexWidgets, &self->m_indexWidgetRows,
                          &self->m_indexWidgetCols,
                          row + 1, col + 1, sizeof(void*)))
        return;
    slot = (void**)XVector_at_base(
        &self->m_indexWidgets, (int64_t)row * self->m_indexWidgetCols + col);
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
    if (row >= self->m_indexWidgetRows || col >= self->m_indexWidgetCols)
        return NULL;
    return *(void* const*)XVector_at_base(
        &self->m_indexWidgets, (int64_t)row * self->m_indexWidgetCols + col);
}

/* ==================== 键盘与导航 ==================== */

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

bool XAbstractItemView_keyboardSearch(XAbstractItemView* self,
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
    if (hitRow < 0 && (size_t)self->m_searchPrefixLen > textLen) {
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
{ if (self) self->m_autoScrollMargin = (int16_t)margin; }
int XAbstractItemView_autoScrollMargin(const XAbstractItemView* self)
{ return self ? self->m_autoScrollMargin : 16; }

void XAbstractItemView_setTextElideMode(XAbstractItemView* self, int mode)
{ if (self) self->m_textElideMode = (uint8_t)mode; }
int XAbstractItemView_textElideMode(const XAbstractItemView* self)
{ return self ? self->m_textElideMode
              : (int)XAbstractItemViewTextElideMode_ElideRight; }

/* ==================== 文本省略（对齐 elidedText 消费） ==================== */

const char* XAbstractItemView_elidedText(const XAbstractItemView* self,
                                          char* buf, int bufCap,
                                          const char* text, int maxW,
                                          const XFont* font)
{
    if (!text || !text[0] || !buf || bufCap <= 0) return text ? text : "";
    if (maxW <= 0) return text;
    if (!font) return text; /* 无字体无法测量：不省略（安全退化）。 */
#if XSTYLE_ON
    /* [对齐 Qt 2026-11-03] 两套编号已合并：
     * XCommonStyle_elideText 的 mode 参数现已按
     * Qt::TextElideMode 数值（0=Left 1=Right 2=Middle 3=None），
     * 与本枚举一致，无需映射。 */
    return XCommonStyle_elideText(
        buf, bufCap, text,
        self ? (int)self->m_textElideMode
             : (int)XAbstractItemViewTextElideMode_ElideRight,
        maxW, font);
#else
    (void)self;
    return text; /* XSTYLE_ON=0：省略实现随样式层裁剪，退化为不省略。 */
#endif
}

/* ==================== 边缘自动滚动（对齐 QAbstractItemView） ====================
 * 2026-11-13（P2-07~11）：Qt 对应为 QAbstractItemViewPrivate 私有实现，
 * 全仓无外部调用 ⇒ 本组函数收窄为本文件内 static，公开声明已自
 * XAbstractItemView.h 删除。 */

static bool XAbstractItemView_shouldAutoScroll(const XAbstractItemView* self,
                                               int x, int y)
{
    int margin;
    int w;
    int h;
    if (!self || !self->m_autoScroll) return false;
    margin = self->m_autoScrollMargin;
    if (margin <= 0) return false;
    w = XWidget_width((const XWidget*)self);
    h = XWidget_height((const XWidget*)self);
    /* 四边任一命中即 true（对齐 Qt shouldAutoScroll 的 `||` 连接）。 */
    return (y < margin) || (h - y < margin) ||
           (x < margin) || (w - x < margin);
}

static void XAbstractItemView_stopAutoScroll(XAbstractItemView* self)
{
    if (!self) return;
    if (self->m_autoScrollTimerId != XTIMER_INVALID_ID) {
        XObject_killTimer((XObject*)self, self->m_autoScrollTimerId);
        self->m_autoScrollTimerId = XTIMER_INVALID_ID;
    }
    self->m_autoScrollCount = 0;
}

static void XAbstractItemView_startAutoScroll(XAbstractItemView* self)
{
    int interval;
    if (!self || !self->m_autoScroll) return;
    /* 对齐 Qt QAbstractItemViewPrivate::startAutoScroll：按滚动模式选拍
     * 间隔与视觉颗粒度对应（逐条目 150ms / 逐像素 50ms）。 */
    interval = (self->m_verticalScrollMode ==
                XAbstractItemViewScrollMode_ScrollPerItem) ? 150 : 50;
    /* 先停旧表：周期定时器只覆盖 id 不 kill = 泄漏。 */
    XAbstractItemView_stopAutoScroll(self);
    self->m_autoScrollTimerId =
        XObject_startTimer((XObject*)self, (uint64_t)interval,
                              XTimerType_CoarseTimer);
    self->m_autoScrollCount = 0;
}

/** @brief 取单元格（row,col）的高（像素）；无法测量返回 -1。
 *  @details 对齐 Qt QListViewPrivate::itemSize：逐条目步进量取该行的实际高度。
 *           本库的 visualRect 已含行高/表头/滚动偏移口径，
 *           取第 0 列的高即可（各列行高一致）。 */
static int xaiv_itemExtentV(const XAbstractItemView* self)
{
    XRect rect;
    if (!self) return -1;
    /* 不要求 model 非空：visualRect 虚槽自行判定（安全退化）。 */
    if (!XAbstractItemView_visualRect_base(self, 0, 0, &rect)) return -1;
    return rect.height > 0 ? rect.height : -1;
}

/** @brief 取单元格（row,col）的宽（像素）；无法测量返回 -1。 */
static int xaiv_itemExtentH(const XAbstractItemView* self)
{
    XRect rect;
    if (!self) return -1;
    /* 同 xaiv_itemExtentV：不要求 model 非空。 */
    if (!XAbstractItemView_visualRect_base(self, 0, 0, &rect)) return -1;
    return rect.width > 0 ? rect.width : -1;
}

/** @brief 按当前滚动模式把条目尺寸同步到滚动条的 singleStep。
 *  @details 对齐 Qt：
 *           - ScrollPerItem：`itemviewChangeSingleStep(itemExtent)`，即
 *             singleStep = 条目高/宽（像素），滚轮/箭头逐条目跳
 *             （qabstractslider.cpp:932-937）；
 *           - ScrollPerPixel：`setSingleStep(-1)` 交绔样式取缺省步进
 *             （qabstractitemview.cpp:1305-1310），本库取 1px。
 *           **滚动条范围始终以像素计**（Qt 同），模式只影响步进量。
 *  @note  本函数幂等，可在布局/滚动模式变更时反复调用。 */
void XAbstractItemView_syncScrollSteps(XAbstractItemView* self)
{
    XScrollBar* vbar;
    XScrollBar* hbar;
    int vStep;
    int hStep;
    if (!self) return;
    vbar = XAbstractScrollArea_verticalScrollBar((XAbstractScrollArea*)self);
    hbar = XAbstractScrollArea_horizontalScrollBar((XAbstractScrollArea*)self);
    if (vbar) {
        if (self->m_verticalScrollMode ==
            XAbstractItemViewScrollMode_ScrollPerItem) {
            vStep = xaiv_itemExtentV(self);
            if (vStep > 0)
                XAbstractSlider_setSingleStep((XAbstractSlider*)vbar, vStep);
        } else {
            XAbstractSlider_setSingleStep((XAbstractSlider*)vbar, 1);
        }
    }
    if (hbar) {
        if (self->m_horizontalScrollMode ==
            XAbstractItemViewScrollMode_ScrollPerItem) {
            hStep = xaiv_itemExtentH(self);
            if (hStep > 0)
                XAbstractSlider_setSingleStep((XAbstractSlider*)hbar, hStep);
        } else {
            XAbstractSlider_setSingleStep((XAbstractSlider*)hbar, 1);
        }
    }
}

static void XAbstractItemView_doAutoScroll(XAbstractItemView* self)
{
    XScrollBar* vbar;
    XScrollBar* hbar;
    int vStep;
    int hStep;
    int margin;
    int vValue;
    int hValue;
    int limit;
    int x;
    int y;
    int w;
    int h;
    int advance;
    if (!self || !self->m_autoScroll) return;
    vbar = XAbstractScrollArea_verticalScrollBar((XAbstractScrollArea*)self);
    hbar = XAbstractScrollArea_horizontalScrollBar((XAbstractScrollArea*)self);
    vStep = vbar ? XAbstractSlider_pageStep((XAbstractSlider*)vbar) : 0;
    hStep = hbar ? XAbstractSlider_pageStep((XAbstractSlider*)hbar) : 0;
    limit = vStep > hStep ? vStep : hStep;
    /* 对齐 Qt：累计拍数每拍 +1，上限 = 两方向 pageStep 的较大者——
     * 越靠近边缘滚得越快（线性加速）。 */
    if ((int)self->m_autoScrollCount < limit &&
        self->m_autoScrollCount < 0x7F)
        ++self->m_autoScrollCount;
    if (self->m_autoScrollCount == 0) self->m_autoScrollCount = 1;

    margin = self->m_autoScrollMargin;
    w = XWidget_width((const XWidget*)self);
    h = XWidget_height((const XWidget*)self);
    x = self->m_dragPosX;
    y = self->m_dragPosY;
    if (x < 0 && y < 0) return; /* 从未记录指针位置：无事可做。 */

    /* 每拍推进量 = 拍数 × singleStep。
     * - ScrollPerItem：singleStep 已同步为条目尺寸，因此按整条目步进；
     * - ScrollPerPixel：singleStep = 1，因此按像素步进。
     * 对齐 Qt doAutoScroll：`setValue(value ± autoScrollCount)`——它本身按
     * 像素推，因此此处乘 singleStep 后的行为在 ScrollPerItem
     * 下等价于「每拍一个条目」，在 ScrollPerPixel 下与 Qt 完全一致。 */
    {
        int vs = vbar ? XAbstractSlider_singleStep((XAbstractSlider*)vbar) : 1;
        int hs = hbar ? XAbstractSlider_singleStep((XAbstractSlider*)hbar) : 1;
        if (vs < 1) vs = 1;
        if (hs < 1) hs = 1;
        advance = (int)self->m_autoScrollCount;
        vValue = vbar ? XAbstractSlider_value((XAbstractSlider*)vbar) : 0;
        hValue = hbar ? XAbstractSlider_value((XAbstractSlider*)hbar) : 0;
        if (vbar) {
            if (y < margin)
                XAbstractSlider_setValue((XAbstractSlider*)vbar,
                                         vValue - advance * vs);
            else if (h - y < margin)
                XAbstractSlider_setValue((XAbstractSlider*)vbar,
                                         vValue + advance * vs);
        }
        if (hbar) {
            if (x < margin)
                XAbstractSlider_setValue((XAbstractSlider*)hbar,
                                         hValue - advance * hs);
            else if (w - x < margin)
                XAbstractSlider_setValue((XAbstractSlider*)hbar,
                                         hValue + advance * hs);
        }
    }
    /* 两方向都没动（已到边界）：停表（对齐 Qt 的 unchanged 分支）。 */
    if (vbar && XAbstractSlider_value((XAbstractSlider*)vbar) == vValue &&
        hbar && XAbstractSlider_value((XAbstractSlider*)hbar) == hValue) {
        XAbstractItemView_stopAutoScroll(self);
    }
}

static void XAbstractItemView_updateAutoScrollPointer(XAbstractItemView* self,
                                                      int x, int y)
{
    if (!self) return;
    self->m_dragPosX = x;
    self->m_dragPosY = y;
    if (XAbstractItemView_shouldAutoScroll(self, x, y)) {
        if (self->m_autoScrollTimerId == XTIMER_INVALID_ID)
            XAbstractItemView_startAutoScroll(self);
    } else {
        XAbstractItemView_stopAutoScroll(self);
    }
}

void XAbstractItemView_setVerticalScrollMode(XAbstractItemView* self, int mode)
{
    if (!self) return;
    if (self->m_verticalScrollMode == (uint8_t)mode) return;
    self->m_verticalScrollMode = (uint8_t)mode;
    /* 对齐 Qt setVerticalScrollMode（qabstractitemview.cpp:1297-1312）：
     * 切换模式后重新同步 singleStep（逐条目 = 条目尺寸）。 */
    XAbstractItemView_syncScrollSteps(self);
}
int XAbstractItemView_verticalScrollMode(const XAbstractItemView* self)
{ return self ? self->m_verticalScrollMode
              : (int)XAbstractItemViewScrollMode_ScrollPerItem; }

void XAbstractItemView_setHorizontalScrollMode(XAbstractItemView* self,
                                               int mode)
{
    if (!self) return;
    if (self->m_horizontalScrollMode == (uint8_t)mode) return;
    self->m_horizontalScrollMode = (uint8_t)mode;
    /* 同 setVerticalScrollMode：切换后重新同步 singleStep。 */
    XAbstractItemView_syncScrollSteps(self);
}
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
{
    if (!self) return;
    /* 对标 Qt 6.8 QAbstractItemView::setDragDropMode
     * （qabstractitemview.cpp:1536-1550）：模式决定"拖出"与"接受拖入"两路
     * 开关——DragOnly/DragDrop/InternalMove 开启拖出；DropOnly/DragDrop/
     * InternalMove 经 acceptDrops 接受拖入。InternalMove 同时门控两路。 */
    if (mode != XAbstractItemViewDragDropMode_NoDragDrop &&
        mode != XAbstractItemViewDragDropMode_DragOnly &&
        mode != XAbstractItemViewDragDropMode_DropOnly &&
        mode != XAbstractItemViewDragDropMode_DragDrop &&
        mode != XAbstractItemViewDragDropMode_InternalMove)
        mode = XAbstractItemViewDragDropMode_NoDragDrop;
    self->m_dragDropMode = (uint8_t)mode;
    /* 模式切换作废在途拖会话（本框架扩展契约，Qt setter 无状态语义——
     * 合成事件序列里 dragLeave 不必然到达，锁在 XViewMimeDragAlignTest
     * dragEnterAcceptsOnFormatMatch：模式复位后 state 必须回 NoState）。 */
    self->m_state = (uint8_t)XAbstractItemViewDragState_NoState;
    self->m_dragEnabled =
        (uint8_t)(mode == XAbstractItemViewDragDropMode_DragOnly ||
                  mode == XAbstractItemViewDragDropMode_DragDrop ||
                  mode == XAbstractItemViewDragDropMode_InternalMove);
    XWidget_setAcceptDrops(
        (XWidget*)self,
        (mode == XAbstractItemViewDragDropMode_DropOnly ||
         mode == XAbstractItemViewDragDropMode_DragDrop ||
         mode == XAbstractItemViewDragDropMode_InternalMove));
}
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
{ if (self) self->m_defaultDropAction = (uint16_t)action; }
int XAbstractItemView_defaultDropAction(const XAbstractItemView* self)
{ return self ? self->m_defaultDropAction : 0; }

void XAbstractItemView_setDropIndicatorShown(XAbstractItemView* self,
                                             bool enable)
{ if (self) self->m_showDropIndicator = enable; }
bool XAbstractItemView_showDropIndicator(const XAbstractItemView* self)
{ return self ? self->m_showDropIndicator : true; }

/* ==================== 拖放状态机与指示线（§B6 视图侧） ==================== */

int XAbstractItemView_state(const XAbstractItemView* self)
{ return self ? (int)self->m_state : (int)XAbstractItemViewDragState_NoState; }

int XAbstractItemView_dropIndicatorPosition(const XAbstractItemView* self)
{
    return self ? (int)self->m_dropIndicatorPosition
                : (int)XAbstractItemViewDropIndicatorPosition_OnItem;
}

bool XAbstractItemView_dropIndicatorRect(const XAbstractItemView* self,
                                         int* outX, int* outY,
                                         int* outW, int* outH)
{
    if (outX) *outX = 0;
    if (outY) *outY = 0;
    if (outW) *outW = 0;
    if (outH) *outH = 0;
    if (!self || !self->m_hasDropRect) return false;
    if (outX) *outX = self->m_dropRectX;
    if (outY) *outY = self->m_dropRectY;
    if (outW) *outW = self->m_dropRectW;
    if (outH) *outH = self->m_dropRectH;
    return true;
}

int XAbstractItemView_supportedDropActions(const XAbstractItemView* self)
{
    /* 对标 Qt **QAbstractItemModel::supportedDropActions**
     * （qabstractitemmodel.h:298；QAbstractItemView 本身没有该成员 ——
     * 视图侧只做转发），见头文件同名入口的 @note。
     * 有模型时取模型侧 supportedDropActions，无模型回退 Copy。
     * 动作族来自 XDropAction（XWindowEvent.h:141-146），随 XWINDOWEVENT_ON
     * 裁剪 —— 该开关关闭时本域的四个拖放槽整段不存在，本访问器一并回退
     * 字面常量 1（= CopyAction），保持裁剪档可编译。 */
#if XWINDOWEVENT_ON
    /* 2026-11-15 B6R 返工：改走模型侧虚槽入口（_base）——派生模型重载
     * supportedDropActions 后本访问器与 dropOn/canDrop 门控同步生效。 */
    if (self && self->m_model)
        return XAbstractItemModel_supportedDropActions_base(self->m_model);
    return (int)XDropAction_CopyAction;
#else
    (void)self;
    return 1; /* XDropAction_CopyAction 的数值（见 XWindowEvent.h:144）。 */
#endif /* XWINDOWEVENT_ON */
}

int XAbstractItemView_supportedDragActions(const XAbstractItemView* self)
{
    /* 对标 qabstractitemview.cpp:4768 `model->supportedDragActions()`
     * （**拖出**用这套，不是 supportedDropActions——两者在模型侧可不同）。
     * 裁剪档处理同上（XWINDOWEVENT_ON=0 时整段拖放不存在）。 */
#if XWINDOWEVENT_ON
    if (self && self->m_model)
        return XAbstractItemModel_supportedDragActions_base(self->m_model);
    return (int)XDropAction_CopyAction;
#else
    (void)self;
    return 1; /* XDropAction_CopyAction 的数值（见 XWindowEvent.h:144）。 */
#endif /* XWINDOWEVENT_ON */
}

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
        if (!XAbstractItemView_visualRect_base(self, row, column, &rect)) continue;
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
        if (!XAbstractItemView_visualRect_base(self, row, col, &rect)) continue;
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
    if (!XAbstractItemView_visualRect_base(self, row, column, &rect)) return false;
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
    if (!XAbstractItemView_visualRect_base(self, row, column, &rect)) return;
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

/** @brief setModel 虚槽基类缺省实现（派生未重载时的分派目标；2026-11-14
 *         契约收口：业务真身以 static VX* 直挂
 *         XVTABLE_OVERLOAD_DEFAULT(EXAbstractItemView_SetModel, ...)，
 *         对外只留 XAbstractItemView_setModel_base 分派入口）。 */
static void VXAbstractItemView_setModel(XAbstractItemView* self,
                                        XAbstractItemModel* model)
{
    if (!self) return;
    /* 对标 Qt QAbstractItemView::setModel 首行 `if (model == d->model)
     * return;`：同模型重复设置为空操作（不重连信号、不重置当前索引）。 */
    if (self->m_model == model) return;
    /* 模型替换前收敛打开的编辑器（提交分支落库旧模型后关闭；对标 Qt
     * setModel 关闭全部编辑器）。 */
    if (self->m_editor)
        XAbstractItemView_closeEditor(self, self->m_editor,
                                      XItemDelegateEndEditHint_NoHint);
    /* 对标 Qt：模型替换时断开旧模型信号、连接新模型信号（此前不
     * 连接，外部改模型后视图不刷新）。 */
    if (self->m_model) {
        XObject* m = (XObject*)self->m_model;
        XObject_disconnect(m, (size_t)XAbstractItemModel_dataChanged_signal(
                                   self->m_model, 0, 0, 0, 0, 0),
                             (XObject*)self, xaiv_modelRefreshSlot);
        XObject_disconnect(m, (size_t)XAbstractItemModel_rowsInserted_signal(
                                    self->m_model, 0, 0),
                             (XObject*)self, xaiv_modelRefreshSlot);
        XObject_disconnect(m, (size_t)XAbstractItemModel_rowsRemoved_signal(
                                    self->m_model, 0, 0),
                             (XObject*)self, xaiv_modelRefreshSlot);
        XObject_disconnect(m, (size_t)XAbstractItemModel_modelReset_signal(
                                   self->m_model),
                             (XObject*)self, xaiv_modelRefreshSlot);
        XObject_disconnect(m, (size_t)XAbstractItemModel_columnsInserted_signal(
                                    self->m_model, 0, 0),
                             (XObject*)self, xaiv_modelRefreshSlot);
        XObject_disconnect(m, (size_t)XAbstractItemModel_columnsRemoved_signal(
                                    self->m_model, 0, 0),
                             (XObject*)self, xaiv_modelRefreshSlot);
        XObject_disconnect(m, (size_t)XAbstractItemModel_layoutChanged_signal(
                                   self->m_model,
                                   XLayoutChangeHint_NoLayoutChangeHint),
                             (XObject*)self, xaiv_modelRefreshSlot);
        XObject_disconnect(m, (size_t)XAbstractItemModel_rowsMoved_signal(
                                    self->m_model, 0, 0, 0),
                             (XObject*)self, xaiv_modelRefreshSlot);
        XObject_disconnect(m, (size_t)XAbstractItemModel_columnsMoved_signal(
                                    self->m_model, 0, 0, 0),
                             (XObject*)self, xaiv_modelRefreshSlot);
    }
    self->m_model = model;
    /* 行/列变化入口同步：模型替换使全部 (row,col) 失效，释放持久编辑器
     * 标记表、条目控件表与 role 叠加存储表（指针归调用方，仅解除承载）。 */
    xaiv_persistentRelease(self);
    xaiv_indexWidgetRelease(self);
    xaiv_roleRelease(self);
    /* 选择模型侧同步绑定（对标 Qt QAbstractItemView::setModel 时选择
     * 模型随视图换挂模型；Qt 为 QItemSelectionModel 构造传模型，本库
     * 以借用指针同步）。 */
    if (self->m_selectionModel)
        XItemSelectionModel_setModel(self->m_selectionModel, model);
    /* 换模型后此刻的**旧选择/旧当前索引全部失效**：Qt 6.8.0 实测
     * （QListView/QTreeView，先 setCurrentIndex 再 setModel）——
     * currentIndex().row() 变 -1、selectedIndexes().clear()；同源实测
     * QItemSelectionModel::setModel 自身亦复位当前索引与选择集合
     * （cur=1→-1、sel=1→0）。本库的选择模型 setModel 只换绑定，故由
     * 视图入口按 Qt 观测口径补齐复位（此前把旧行/列夹到新模型边界，
     * 与 Qt「当前索引失效」不符）。 */
    self->m_currentRow = -1;
    self->m_currentColumn = -1;
    if (self->m_selectionModel) XItemSelectionModel_clear(self->m_selectionModel);
    if (model) {
        XObject* m = (XObject*)model;
#define XAIV_MODEL_CONNECT(sig, slotFn) \
    XObject_connect(m, (size_t)(sig), (XObject*)self, \
                      slotFn, XConnectionType_Direct)
        XAIV_MODEL_CONNECT(XAbstractItemModel_dataChanged_signal(model, 0, 0,
                                                                 0, 0, 0),
                           xaiv_dataChangedSlot);
        XAIV_MODEL_CONNECT(XAbstractItemModel_rowsInserted_signal(model,
                                                                   0, 0),
                           xaiv_modelRefreshSlot);
        XAIV_MODEL_CONNECT(XAbstractItemModel_rowsRemoved_signal(model,
                                                                   0, 0),
                           xaiv_modelRefreshSlot);
        XAIV_MODEL_CONNECT(XAbstractItemModel_modelReset_signal(model),
                           xaiv_modelRefreshSlot);
        XAIV_MODEL_CONNECT(XAbstractItemModel_columnsInserted_signal(model,
                                                                      0, 0),
                           xaiv_modelRefreshSlot);
        XAIV_MODEL_CONNECT(XAbstractItemModel_columnsRemoved_signal(model,
                                                                      0, 0),
                           xaiv_modelRefreshSlot);
        XAIV_MODEL_CONNECT(XAbstractItemModel_layoutChanged_signal(
                               model, XLayoutChangeHint_NoLayoutChangeHint),
                           xaiv_modelRefreshSlot);
        XAIV_MODEL_CONNECT(XAbstractItemModel_rowsMoved_signal(model, 0, 0, 0),
                           xaiv_modelRefreshSlot);
        XAIV_MODEL_CONNECT(XAbstractItemModel_columnsMoved_signal(model, 0, 0,
                                                                  0),
                           xaiv_modelRefreshSlot);
#undef XAIV_MODEL_CONNECT
    }
    XWidget_update((XWidget*)self);
}

/** @brief setModel 虚槽对外唯一入口：直接按虚表槽分派（用户 2026-11-14
 *         定案）。_base 只做空/虚表守卫 + 一次 XClassGetVirtualFunc 查槽
 *         分派，不承载业务；不提供无后缀同名公开入口，也不通过另一函数
 *         二级转发。派生视图槽内回落基类口径用 XClass_Parent。 */
void XAbstractItemView_setModel_base(XAbstractItemView* self,
                                     XAbstractItemModel* model)
{
    if (!self || !XClassGetVtable((XClass*)self)) return;
    XClassGetVirtualFunc(self, EXAbstractItemView_SetModel,
                         void (*)(XAbstractItemView*,
                                  XAbstractItemModel*))(self, model);
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

/** @brief dataChanged 窄化槽：单格内容变更只失效该格可视矩形。
 *  @details 对标 Qt QAbstractItemView::dataChanged → d->update(变更
 *           索引区间)；此前与结构信号共用整视图刷新槽——模型里写一格
 *           （真机轮询 10ms 逐格写）就把整表拖成视口带整刷，触摸阵发
 *           与稳态 CPU 的大头之一。格滚出视口/越界（visualRect 失败）
 *           即免失效；当前索引越界收敛与编辑器失效是结构变化语义，
 *           留在共享刷新槽（rows/cols/reset/layout）不在此重复。 */
static void xaiv_dataChangedSlot(XObject* receiver, XVarList* args)
{
    XAbstractItemView* view = (XAbstractItemView*)receiver;
    XRect topLeft;
    XRect bottomRight;
    XRect dirty;
    int topRow = -1;
    int topCol = -1;
    int bottomRow = -1;
    int bottomCol = -1;
    if (!view || !args) return;
    /* Qt 五参数形状：左上 (row,col) + 右下 (row,col) + roles（roles 本槽
       不参与失效判定）。 */
    XVarList_args_5(args, int, r1, int, c1, int, r2, int, c2, int, roles);
    (void)roles;
    topRow = r1;
    topCol = c1;
    bottomRow = r2;
    bottomCol = c2;
    if (topRow < 0 || topCol < 0) return;
    if (bottomRow < topRow) bottomRow = topRow;
    if (bottomCol < topCol) bottomCol = topCol;
    if (!XAbstractItemView_visualRect_base(view, topRow, topCol, &topLeft)) return;
    if (!XAbstractItemView_visualRect_base(view, bottomRow, bottomCol,
                                      &bottomRight)) return;
    /* 变化区间取两角并集（对标 QAbstractItemView::dataChanged 按
       topLeft..bottomRight 区间失效）——单格时两角相同，与旧口径等价。 */
    dirty.x = topLeft.x < bottomRight.x ? topLeft.x : bottomRight.x;
    dirty.y = topLeft.y < bottomRight.y ? topLeft.y : bottomRight.y;
    {
        int right1 = topLeft.x + topLeft.width;
        int right2 = bottomRight.x + bottomRight.width;
        int bottom1 = topLeft.y + topLeft.height;
        int bottom2 = bottomRight.y + bottomRight.height;
        dirty.width = (right1 > right2 ? right1 : right2) - dirty.x;
        dirty.height = (bottom1 > bottom2 ? bottom1 : bottom2) - dirty.y;
    }
    XWidget_updateRect((XWidget*)view, &dirty);
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
        XClassDelete(self->m_selectionModel);
    self->m_selectionModel = selectionModel;
    if (!self->m_selectionModel)
        self->m_selectionModel = XItemSelectionModel_create();
    /* 对标 Qt setSelectionModel 的模型一致性：选择模型随视图绑定当前
     * 模型（Qt 校验 selectionModel->model() == view model()，本库直接
     * 同步写入，见 XItemSelectionModel_setModel）。 */
    XItemSelectionModel_setModel(self->m_selectionModel, self->m_model);
}

void XAbstractItemView_clearSelection(XAbstractItemView* self)
{
    if (!self || !self->m_selectionModel) return;
    /* 对标 Qt QAbstractItemView::clearSelection（qabstractitemview.cpp
     * :1243-1248）：仅清选择集合，**当前索引不变**
     * （转发选择模型的 clearSelection，qitemselectionmodel.cpp
     * :1369-1376）。 */
    XItemSelectionModel_clearSelection(self->m_selectionModel);
}

void XAbstractItemView_selectAll(XAbstractItemView* self)
{
    XItemSelectionModel* selection;
    int command;
    if (!self) return;
    selection = self->m_selectionModel;
    if (!selection || !self->m_model) return;
    /* 对标 Qt 6.8 QAbstractItemView::selectAll（qabstractitemview.cpp
     * :1199-1218）：
     *   - MultiSelection/ExtendedSelection → 一次性
     *     ClearAndSelect|behaviorFlags 原子命令（:1204-1208）；
     *   - NoSelection/ContiguousSelection → 走
     *     selectionCommand(index(0,0,root))（:1209-1213），即
     *     ClearAndSelect|behaviorFlags（NoSelection 时为 NoUpdate）；
     *   - SingleSelection → **什么都不做**（:1214-1215 `break;`）。 */
    switch (self->m_selectionMode) {
    case XAbstractItemViewSelectionMode_NoSelection:
        return;
    case XAbstractItemViewSelectionMode_SingleSelection:
        /* Qt 单选模式 selectAll 为空操作（不选中当前项）。 */
        return;
    default:
        command = XItemSelectionModelSelectionFlag_ClearAndSelect |
                  xaiv_behaviorFlags(self);
        break;
    }
    XItemSelectionModel_selectSelection(
        selection, 0, 0, self->m_model->m_rows - 1, self->m_model->m_cols - 1,
        command);
}

void XAbstractItemView_setRootIndex(XAbstractItemView* self, int row, int col)
{
    if (!self) return;
    self->m_rootRow = row;
    self->m_rootCol = col;
}

bool XAbstractItemView_rootIndex(const XAbstractItemView* self,
                                 int* outRow, int* outCol)
{
    /* 对标 Qt 6.8 QAbstractItemView::rootIndex（qabstractitemview.cpp
     * :1187-1190）：返回**存储的根索引**（d->root），缺省为无效索引；
     * reset() 经 setRootIndex(QModelIndex()) 复位为无效
     * （qabstractitemview.cpp:1146）。本库以 (row,col) 平面承载，
     * 无效根索引记为 (-1,-1)。 */
    if (outRow) *outRow = self ? self->m_rootRow : -1;
    if (outCol) *outCol = self ? self->m_rootCol : -1;
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

/** @brief 命中测试虚函数对外唯一入口：直接按虚表槽分派（用户 2026-11-14
 *         定案）。_base 只做空/虚表守卫 + 一次 XClassGetVirtualFunc 转发，
 *         不承载业务、不再有同名无后缀公开入口或二级转发桥。 */
bool XAbstractItemView_indexAt_base(const XAbstractItemView* self, int x,
                                    int y, int* outRow, int* outCol)
{
    if (ISNULL(self, "") || ISNULL(XClassGetVtable(self), "")) return false;
    return XClassGetVirtualFunc(self, EXAbstractItemView_IndexAt,
                                bool (*)(const XAbstractItemView*, int, int,
                                         int*, int*))(self, x, y, outRow,
                                                      outCol);
}

/** @brief 基类矩形实现：默认网格布局（行高 24、列宽 80）。 */
static bool VXAbstractItemView_visualRect(const XAbstractItemView* self,
                                          int row, int col, XRect* out)
{
    if (!self || !out || row < 0 || col < 0) return false;
    XRect_init(out, col * 80, row * 24, 80, 24);
    return true;
}

/** @brief 条目可编辑判定（edit() 门禁；对标 Qt model flags(index) &
 *  ItemIsEditable——Qt 6.8.0 实测：只读模型（flags 无 ItemIsEditable）
 *  上 edit(index) 不开编辑器）。
 *  @details 本库在 Qt 语义（模型 flags）之外**并上**视图级总闸
 *           m_itemsEditable（历史约定：视图缺省不编辑，接入层显式开启）。
 *           即：模型给出 ItemIsEditable 或视图总闸开启，二者其一即可编辑。
 *           表格派生（XTableWidget）另有覆写，按 per-item flags 细化。 */
static bool VXAbstractItemView_shouldEdit(const XAbstractItemView* self,
                                          int row, int col)
{
    if (!self) return false;
    if (self->m_itemsEditable) return true;
    return (XAbstractItemModel_flags_base(self->m_model, row, col) &
            XItemFlag_ItemIsEditable) != 0;
}

bool XAbstractItemView_visualRect_base(const XAbstractItemView* self,
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
    self->m_iconW = (int16_t)w;
    self->m_iconH = (int16_t)h;
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

/* ==================== 焦点指示（聚焦播种 current 行指示） ==================== */

/*
 * 隔夜台账猎获③（页5 四隐形站）：Tab 走查到条目视图（列表/列表视
 * 图/树/表格）时无任何可见焦点指示。对标 Qt 6.8.3
 * QAbstractItemView::focusInEvent（qabstractitemview.cpp:2297-2322）：
 * current 无效且非鼠标原因聚焦时，以 moveCursor(MoveNext) 取首可见
 * 索引播种 current（setCurrentIndex(index, NoUpdate) 不动选择集合），
 * 随后 viewport->update()。本实现播种坐标取 (0,0)（平铺模型的"首
 * 可见索引"等价位；列表/树/表 current 列口径均为 0）。
 *
 * 指示形态说明：Qt 中聚焦视图的 current 行焦点框（PE_FrameFocusRect，
 * qitemdelegate.cpp:703-722 drawFocus 的 State_HasFocus 门禁；
 * qtreeview.cpp:1855-1876 drawRow 整行焦点框）由条目绘制层（委托/
 * 派生视图）绘制。本库条目绘制分散在派生视图 TU（XListView/
 * XTreeWidget/XTableWidget 自绘，本轮禁改），且软件渲染的 PAINT 事件
 * 在顶层窗口被消化（XWidget.c flushBackingStore 直调最派生
 * paintEvent_base），不经控件级事件分派——基类无"派生条目渲染完成后
 * 叠画焦点框"的可达挂点。故四站可见指示收敛为"focusIn 播种 current
 * 的行/格高亮"（各派生绘制均已消费：XListView cur/选中行高亮、
 * XTableWidget current 格高亮、XTreeWidget 选中行高亮），与任务口径
 * 中树站既有"Down 高亮"指示同形态。
 */

/** @brief 聚焦进入时播种 current（对标 focusInEvent 播种分支）。
 *  @details 条件（对标 qabstractitemview.cpp:2305-2312）：模型非空、
 *           current 无效、非鼠标原因（鼠标点击聚焦不播种，点击本身
 *           会建立 current）。
 *           播种语义：已有选择时走 NoUpdate 等价（仅移 current 不触
 *           碰选择集合，对标 setCurrentIndex(index, NoUpdate)）；完全
 *           无选择时走 setCurrentIndex 的 ClearAndSelect 语义——树站
 *           绘制只高亮选中行，无选择播种若不动选择集合将依旧无指
 *           示（本库指示形态收敛的必要偏离，见上）。 */
static void xaiv_seedCurrentOnFocusIn(XAbstractItemView* view,
                                      XFocusReason reason)
{
    XAbstractItemModel* model;
    if (!view || reason == XFocusReason_Mouse) return;
    model = view->m_model;
    if (!model || model->m_rows <= 0 || model->m_cols <= 0) return;
    if (view->m_currentRow >= 0 && view->m_currentColumn >= 0) return;
    if (view->m_selectionModel &&
        XItemSelectionModel_selectedCount(view->m_selectionModel) > 0) {
        /* 已有选择但 current 无效（罕见）：仅移 current 不动选择。 */
        xaiv_setCurrentPreservingSelection(view, 0, 0);
        return;
    }
    XAbstractItemView_setCurrentIndex(view, 0, 0);
}

/** @brief 聚焦进入：播种 current（非鼠标原因）后走基类链（基类默认
 *         focusIn 槽携带整控件 update，指示行高亮随重绘出现）。 */
static void VXAbstractItemView_focusInEvent(XWidget* self, XEvent* event)
{
    XFocusReason reason = XFocusReason_Other;
#if XWINDOWEVENT_ON
    if (self && event && XEvent_type(event) == XEVENT_TYPE_FOCUS_IN &&
        XFocusEvent_gotFocus((const XFocusEvent*)event))
        reason = XFocusEvent_reason((const XFocusEvent*)event);
#endif /* XWINDOWEVENT_ON */
    if (self && event && XEvent_type(event) == XEVENT_TYPE_FOCUS_IN)
        xaiv_seedCurrentOnFocusIn((XAbstractItemView*)self, reason);
    XClass_Parent(XAbstractScrollArea, EXWidget_FocusInEvent,
                  void (*)(XWidget*, XEvent*))((XWidget*)self, event);
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
    /* 点击交付键盘焦点（对标 Qt QApplicationPrivate::
     * giveFocusAccordingToFocusPolicy，qapplication.cpp:3662：视图
     * StrongFocus 策略含 ClickFocus 位，左键按压即聚焦本视图，方向键
     * 导航随之可达；同 XLineEdit 点击聚焦的库内既有范式）。原因值
     * 用 MouseFocusReason（Qt 同款）：focusInEvent 的 current 播种
     * 门以"非鼠标原因"为条件（qabstractitemview.cpp:2312），鼠标
     * 点击聚焦不得播种 current。窗口型
     * 视图（XComboBox 补全/下拉弹层，本库未见 hide 后还焦机制）不
     * 在此抢焦点：弹层焦点归属组合框自身机制（对标 Qt 弹层由
     * QComboBox 私有机制管理焦点，不经通用点击聚焦路径），避免隐藏
     * 后焦点滞留悬空。 */
    if (!self->m_isWindow)
        XWidget_setFocus_2(self, XFocusReason_Mouse);
    /* §B6：记录左键按下起点（控件局部像素），供 mouseMove 比对
     * startDragDistance 决定何时进入 DraggingState 并发起拖出（对齐 Qt
     * mouseMoveEvent 的拖拽阈值判定）。 */
    view->m_dragPressX = pos.x;
    view->m_dragPressY = pos.y;
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
                XItemSelectionModel_setCurrentIndex(
                    sm, row, col, XItemSelectionModelSelectionFlag_NoUpdate);
            XWidget_update((XWidget*)view);
        } else if (toggle && sm) {
            /* 对标 Qt extendedSelectionCommand / multiSelectionCommand
             * 的 Toggle 分支（qabstractitemview.cpp:4227 / 4311-4315）：
             * 逐格翻转选中态（Ctrl 点按）。 */
            xaiv_selectCell(view, row, col,
                            XItemSelectionModelSelectionFlag_Toggle);
            view->m_currentRow = row;
            view->m_currentColumn = col;
            if (sm)
                XItemSelectionModel_setCurrentIndex(
                    sm, row, col, XItemSelectionModelSelectionFlag_NoUpdate);
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
    /* [自动滚动 2026-11-03] 松开按键即停表（对齐 Qt mouseReleaseEvent
     * 末尾的 stopAutoScroll 语义）。 */
    view->m_dragSelecting = 0;
    XAbstractItemView_stopAutoScroll(view);
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
/** @brief 边缘自动滚动拍（id 守卫：只消费本视图的自动滚动拍；其他业务
 *         定时器——如派生控件的闪烁/惯性拍——原样链回父类槽）。
 *  @details 对齐 Qt QAbstractItemViewPrivate::startAutoScroll 的
 *           autoScrollTimer 连接：到期调 doAutoScroll。 */
static void VXAbstractItemView_timerEvent(XObject* object, XTimerEvent* event)
{
    XAbstractItemView* view = (XAbstractItemView*)object;
    if (!view || !event) return;
    if (view->m_autoScrollTimerId == XTIMER_INVALID_ID ||
        XTimerEvent_timerId(event) != view->m_autoScrollTimerId) {
        XClass_Parent(XObject, EXObject_TimerEvent,
                      void (*)(XObject*, XTimerEvent*))(object, event);
        return;
    }
    XAbstractItemView_doAutoScroll(view);
    XEvent_accept((XEvent*)event);
}

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
    /* [自动滚动 2026-11-03] 左键按住拖选期间记录指针并驱动边缘自动滚动
     * （对齐 Qt mouseMoveEvent 的 DragSelectingState 分支：置状态 → 更新
     * 选区矩形 → 末了按 shouldAutoScroll 启停定时器）。 */
    if (XMouseEvent_buttons(me) & XMouseButton_LeftButton) {
        view->m_dragSelecting = 1;
        XAbstractItemView_updateAutoScrollPointer(view, pos.x, pos.y);
    } else if (view->m_dragSelecting) {
        view->m_dragSelecting = 0;
        XAbstractItemView_stopAutoScroll(view);
    }
    /* §B6 拖出触发（对标 qabstractitemview.cpp:1907-1922 mouseMoveEvent 的
     * 拖拽分支 + :4762-4771 maybeStartDrag）：
     *   `pressedIndex.isValid() && dragEnabled && state != DragSelectingState`
     *   `&& buttons != NoButton && !selectedDraggableIndexes().isEmpty()`
     *   ⇒ setState(DraggingState) + maybeStartDrag；
     *   maybeStartDrag 判据为 `manhattanLength > QApplication::startDragDistance()`
     *   （严格大于），拖完 `setState(NoState)` 并 stopAutoScroll。
     * 整段随 XWINDOWEVENT_ON 裁剪：startDrag 走 XPlatformDrag/XDropAction，
     * 该开关关闭时这些类型与函数定义都不存在（同 XWidget.c:3485-3494 的
     * 「裁剪档回退常量」范式）。 */
#if XWINDOWEVENT_ON
    if (view->m_state != XAbstractItemViewDragState_DraggingState &&
        view->m_state != XAbstractItemViewDragState_DragSelectingState &&
        view->m_dragEnabled && view->m_model && view->m_selectionModel &&
        (XMouseEvent_buttons(me) & XMouseButton_LeftButton)) {
        int dx = pos.x - view->m_dragPressX;
        int dy = pos.y - view->m_dragPressY;
        int dist = (dx > 0 ? dx : -dx) + (dy > 0 ? dy : -dy);
        int threshold = xaiv_startDragDistance();
        if (dist > threshold && xaiv_hasDraggableSelection(view)) {
            view->m_state =
                (uint8_t)XAbstractItemViewDragState_DraggingState;
            xaiv_startDrag(view, XAbstractItemView_supportedDragActions(view));
            /* startDrag 返回即拖放结束（qabstractitemview.cpp:4769-4770）。 */
            view->m_state = (uint8_t)XAbstractItemViewDragState_NoState;
            XAbstractItemView_stopAutoScroll(view);
        }
    }
#endif /* XWINDOWEVENT_ON */
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

/* ==================== 拖放（§B6，对标 Qt 6.8 qabstractitemview.cpp
 *                        dragEnterEvent/dragMoveEvent/dragLeaveEvent/
 *                        dropEvent/startDrag 与私有 helper） ==================== */

/* ---------------------------------------------------------------- §B6 差异登记
 * 本段是"视图侧"拖放。与 Qt 6.8 原文（qabstractitemview.cpp）逐条比对后
 * 曾有 5 处**结构层**差异；2026-11-15 本批收口①②③（XDropEvent 载荷
 * XByteArray 化 + source 字段 + 构造缺省 ignore，均已落码），④ 经复查
 * 维持登记，⑤⑥⑦ 状态见各自条目：
 *
 * ① 【2026-11-15 已收口】`XDropEvent` 已增 `m_source`（XObject*）+
 *    `XDropEvent_source()/setSource()`（XWindowEvent.{h,c}）；平台投递点
 *    `XWindowSystemInterface_handleDropEvent_bytes` 统一填**接收窗口自身**
 *    （单窗现实：平台层观测不到"发起视图"，接收窗口即本进程拖源的最近
 *    似对象；跨进程不可辨差异见 XWindowEvent.h 类注释）。本段
 *    droppingOnItself 的 source 项**仍按"自身源"恒真处理**（Qt
 *    `source == q` 的精确判等需要"发起视图"标识，窗口≠视图，平台口径
 *    恒填接收窗口 ⇒ 判等条件在本框架不可用）；InternalMove 门控与
 *    possibleActions/dropAction 两项照 Qt 保留不变。
 *
 * ② 【2026-11-15 已收口】`XDropEvent` 载荷已从 XString 改为 XByteArray
 *    （二进制透明）：模型 wire `application/x-qabstractitemmodeldatalist`
 *    的大端 int32 序列（含大量 0x00）原样直达模型——旧链
 *    XString_create_with_length_* 在首个 0x00 截断（XChar.c:174-185
 *    streamInputLenU8）导致载荷必空、端到端不可达的历史缺口已消除。
 *    本段 xaiv_mimeDataFromDrop 改按 XByteArray_data/size 直写 XMimeData，
 *    XString latin1 绕行已删除。真端到端回归锁：
 *    XViewMimeDragAlignTest 的 dropAppliesThroughModel（含 0x00 wire
 *    载荷经视图 drop 事件→模型落库 round-trip）。
 *
 * ③ 【2026-11-15 已收口】`dropOn` 首行 `if (event->isAccepted()) return
 *    false;`（:2218）已加回——前提是 XDropEvent_init 已对齐 QDropEvent
 *    构造缺省 ignore（qevent.cpp:3052 构造尾 `ignore()`；此前 XEvent_init
 *    缺省 accepted=true 使守卫会恒拦）。dragMove 的"默认 ignore，通过才
 *    接受"路径与守卫不冲突（守卫拦 accepted==true，默认 ignore 态不命中）。
 *    四个拖放槽在所有拒绝分支显式 XEvent_ignore 的兜底保留不变。
 *
 * ④ 树形落点**维持登记**（2026-11-15 复查）：Qt 的 dropOn 在 Above/Below
 *    时把 index 换成 index.parent()（:2240-2248）并把 parent 传给模型。
 *    模型侧接口已齐（XAbstractItemModel_indexTree/parentIndex/
 *    rowCountAt/appendTreeNode，§B2 落码），但**视图侧缺"显示行→
 *    XModelIndex（含 internalId）"转换入口**：indexAt_base 只给平铺
 *    (row,col)，XTreeView.c 亦未消费树形层级（无 display↔node 桥），
 *    拿不到落点条目的 XModelIndex 就查不了 parentIndex ⇒ parent 恒
 *    NULL（root 平铺，纯平铺模型上与 Qt 等价）。补桥属视图层新接口
 *    （indexAt 返回值对象或 display→internalId 入口），本批未扩接口，
 *    维持登记。
 *
 * ⑤ 【2026-11-15 B6R 返工已解决】`supportedDropActions`/`supportedDragActions`
 *    曾是**普通函数而非虚槽**（Qt 侧即 virtual，qabstractitemmodel.h:294/
 *    :298），派生模型无法重载成 Move/Link ⇒ InternalMove 端到端不可达。
 *    现已提为虚槽（XAbstractItemModel.h 类定义处
 *    EXAbstractItemModel_SupportedDropActions/SupportedDragActions 两槽，
 *    缺省业务挂表、分派入口 XAbstractItemModel_supportedDropActions_base/
 *    supportedDragActions_base，同 Flags 契约；本段 dropOn/canDrop/
 *    resolveDefaultAction 与视图访问器全部改走 _base）。回归锁：
 *    XViewMimeDragAlignTest 的 supportedActionsAreVirtualSlots 与
 *    dropAppliesThroughModel（B 段 InternalMove 的 dragEnter(Move) 真放行）。
 *
 * ⑥ 平台层**从不填** `dropAction`/`possibleActions`：XDropEvent_init 把它们置
 *    IgnoreAction/0（XWindowEvent.c:702-703），全仓唯一生产者
 *    Drive/Posix/Graphics/XPlatformNativeWindow_posix.c:4549-4574 只传
 *    mimeType/data，不调 setDropAction/setPossibleActions（Qt 侧由平台
 *    drag 的 defaultAction 填入）。⇒ 真实拖入时 action=0，本段动作门控
 *    一律判"不可落"（这是**保守正确**的一侧：绝不误接受；但要真正支持拖入，
 *    必须由平台层填动作）。已登记 openNotes。
 *
 * ⑦ `drag->target()` 不可观测：Qt startDrag 的 InternalMove 豁免
 *    `if (dragDropMode() != InternalMove || drag->target() == viewport())`
 *    （qabstractitemview.cpp:3880-3881）读拖放目标；XPlatformDrag 契约
 *    （XPlatformDrag.h）无 target 查询入口 ⇒ 本段 InternalMove 档
 *    clearOrRemove **保守不删**（取 Qt 该条件的保守分支语义：拖丢到视图
 *    外不得删源，防数据丢失）；DragDrop 档 exec==Moved 恒删（对齐 Qt 非
 *    InternalMove 分支）。补 target 观测需要动 XPlatformDrag（Drive/
 *    Platform 层，非本域 owned），已登记 openNotes。
 *    另：Qt 的 dropEventMoved 置位只在子类 dropEvent（qlistview.cpp:941/
 *    qtableview.cpp:3169/qtreewidget.cpp:3349），基类 QAbstractItemView::
 *    dropEvent 不置位 ⇒ 本框架基类视图条件退化为 exec==Moved（与 Qt 基类
 *    行为逐字一致，非缺件）。
 * ---------------------------------------------------------------- */

#if XWINDOWEVENT_ON

/** @brief 指示线位置推导（对标
 *        QAbstractItemViewPrivate::position(pos,rect,index)，
 *        qabstractitemview.cpp:2266-2288）：按落点距矩形上下边距
 *        （qBound(2, qRound(h/5.5), 12)）判定 Above/Below/On；overwrite
 *        模式（本框架以 m_dragDropOverwriteMode 对应 Qt 的 overwrite）下
 *        贴边即 OnItem。末尾按 :2283-2284 做 **OnItem 降级**：条目 flags
 *        不含 ItemIsDropEnabled 时，OnItem 改为"落在上半 ⇒ AboveItem /
 *        落在下半 ⇒ BelowItem"（否则缺省模型上每个落点都会画满格框）。
 *
 * @param view 目标视图（借用）。
 * @param posX 落点 X（控件局部像素）。
 * @param posY 落点 Y（控件局部像素）。
 * @param rect 命中条目矩形（借用；不可空）。
 * @param row  命中条目行（用于 flags 查询）。
 * @param col  命中条目列（用于 flags 查询）。
 * @return XAbstractItemViewDropIndicatorPosition 枚举值。
 */
static bool xaiv_isIndexDropEnabled(const XAbstractItemView* view,
                                    int row, int col);
static bool xaiv_visualRectOf(const XAbstractItemView* view, int row, int col,
                              XRect* out);
static int  xaiv_viewportWidth(const XAbstractItemView* view);
static int xaiv_dropIndicatorPosition(XAbstractItemView* view, int posX,
                                      int posY, const XRect* rect, int row,
                                      int col)
{
    int margin;
    int r;
    if (!view || !rect) return XAbstractItemViewDropIndicatorPosition_OnViewport;
    if (!view->m_dragDropOverwriteMode) {
        /* 插入模式：距上/下边 <margin 即 Above/Below，否则命中为 OnItem。 */
        margin = (rect->height > 0)
                 ? (int)((double)rect->height / 5.5 + 0.5) : 2;
        if (margin < 2) margin = 2;
        if (margin > 12) margin = 12;
        if (posY - rect->y < margin)
            r = XAbstractItemViewDropIndicatorPosition_AboveItem;
        else if (rect->y + rect->height - posY < margin)
            r = XAbstractItemViewDropIndicatorPosition_BelowItem;
        else if (posX >= rect->x && posX <= rect->x + rect->width &&
                 posY >= rect->y && posY <= rect->y + rect->height)
            r = XAbstractItemViewDropIndicatorPosition_OnItem;
        else
            r = XAbstractItemViewDropIndicatorPosition_OnViewport;
    } else {
        /* overwrite 模式：贴边即 OnItem（Qt :2278-2280 touchingRect）。 */
        if (posX >= rect->x - 1 && posX <= rect->x + rect->width + 1 &&
            posY >= rect->y - 1 && posY <= rect->y + rect->height + 1)
            r = XAbstractItemViewDropIndicatorPosition_OnItem;
        else
            r = XAbstractItemViewDropIndicatorPosition_OnViewport;
    }
    /* qabstractitemview.cpp:2283-2284 OnItem 降级（条目不可 drop 时）。 */
    if (r == XAbstractItemViewDropIndicatorPosition_OnItem &&
        row >= 0 && col >= 0 && !xaiv_isIndexDropEnabled(view, row, col))
        r = (posY < rect->y + rect->height / 2)
                ? XAbstractItemViewDropIndicatorPosition_AboveItem
                : XAbstractItemViewDropIndicatorPosition_BelowItem;
    return r;
}

/** @brief 安全取条目矩形（先清零、再查 visualRect 虚槽）。
 *
 *        为什么要包装：`XAbstractItemView_visualRect_base` 在**失败时不写
 *        out**（XListView.c:465-466 对 col != m_modelColumn 直接 return
 *        false），调用点若不判返回值就用 rect 会读到栈垃圾（铁律：局部
 *        结构体交给框架前先清零）。
 *
 * @param view 目标视图（借用）。
 * @param row  条目行。
 * @param col  条目列。
 * @param out  输出矩形（非空；失败时保持全 0）。
 * @return 取到有效矩形返回 true；越界/视图不支持该格返回 false。
 */
static bool xaiv_visualRectOf(const XAbstractItemView* view, int row, int col,
                              XRect* out)
{
    if (!out) return false;
    XRect_init(out, 0, 0, 0, 0);
    if (!view) return false;
    return XAbstractItemView_visualRect_base(view, row, col, out);
}

/** @brief 视口宽度（对标 Qt `viewport()->width()`，qabstractitemview.cpp:2082）。
 *
 *        指示线横线/整行拓宽按**视口**宽算，不是控件本体宽——本体宽包含
 *        纵向滚动条，直接用会多出滚动条宽度。XAbstractScrollArea 有真视口
 *        子控件（XAbstractScrollArea.h:138），取不到时回退本体宽。
 *
 * @param view 目标视图（借用）。
 * @return 视口宽（像素）；self 为空返回 0。
 */
static int xaiv_viewportWidth(const XAbstractItemView* view)
{
    XWidget* vp;
    if (!view) return 0;
    vp = XAbstractScrollArea_viewport((const XAbstractScrollArea*)view);
    if (vp) {
        int w = XWidget_width(vp);
        if (w > 0) return w;
    }
    return XWidget_width((const XWidget*)view);
}

/** @brief 条目是否允许作为 drop 落点（对标
 *        QAbstractItemViewPrivate::isIndexDropEnabled，qabstractitemview_p.h
 *        :262：`model->flags(index) & Qt::ItemIsDropEnabled`）。
 *
 *        消费点两处：① `xaiv_dropIndicatorPosition` 的 OnItem 降级
 *        （qabstractitemview.cpp:2283-2284）；② `xaiv_dropOn` 的
 *        OnViewport 分支按 rootIndex 可落判定（:2110-2113，本框架 root 平铺
 *        ⇒ 以 row/col<0 表示 root，此时查模型"根可落"为无条目可查，按
 *        不可落处理）。
 * @param view 目标视图（借用）。
 * @param row  条目行（<0 表示 root）。
 * @param col  条目列（<0 表示 root）。
 * @return 可落返回 true。
 * @note 本框架无树形祖先链（Qt 沿 parent 逐级查），单条目判定即完整语义。
 */
static bool xaiv_isIndexDropEnabled(const XAbstractItemView* view,
                                    int row, int col)
{
    if (!view || !view->m_model) return false;
    if (row < 0 || col < 0) return false; /* root：无条目可查 ⇒ 不可落。 */
    return (XAbstractItemModel_flags_base(view->m_model, row, col) &
            XItemFlag_ItemIsDropEnabled) != 0;
}

/** @brief 当前拖放是否落在自身选择集合上（对标
 *        QAbstractItemViewPrivate::droppingOnItself，qabstractitemview.cpp:
 *        2135-2153）。Qt 的门控是三段与：
 *          `event->source() == q && (possibleActions & MoveAction) && dropAction == MoveAction`
 *        ⇒ 才逐祖先查落点是否在选中集合内。本框架 XDropEvent **无 source
 *        字段**（跨进程/同进程不可辨），故 source 项视为恒真（等价"自身源"），
 *        其余两项照 Qt 逐字保留——这样外部 Copy 落到已选行不会被误拦，而
 *        InternalMove/Move 自拖自拦仍然生效。
 * @param view 目标视图（借用）。
 * @param drop 拖放事件（借用；读 possibleActions/dropAction）。
 * @param row  落点行（<0 无落点）。
 * @param col  落点列（<0 无落点）。
 * @return 落在自身选择集合上返回 true（调用方据此不落数据/不接受）。
 */
static bool xaiv_droppingOnItself(const XAbstractItemView* view,
                                  const XDropEvent* drop, int row, int col)
{
    XItemSelectionModel* sm;
    int i;
    int n;
    int* rows;
    int* cols;
    int dropAction;
    if (!view || !drop) return false;
    if (row < 0 || col < 0) return false;
    /* InternalMove 下动作恒视为 MoveAction（:2139-2140）。 */
    dropAction = (view->m_dragDropMode ==
                  (uint8_t)XAbstractItemViewDragDropMode_InternalMove)
                     ? (int)XDropAction_MoveAction
                     : XDropEvent_dropAction(drop);
    /* `event->source() == q` 本框架不可辨，按"自身源"处理；
     * 其余两项照 Qt：possibleActions & MoveAction 且 dropAction == MoveAction。 */
    if (!((XDropEvent_possibleActions(drop) & (int)XDropAction_MoveAction)))
        return false;
    if (dropAction != (int)XDropAction_MoveAction)
        return false;
    sm = view->m_selectionModel;
    if (!sm) return false;
    n = XItemSelectionModel_selectedIndexes(sm, NULL, NULL, 0);
    if (n <= 0) return false;
    rows = (int*)XMalloc_System(sizeof(int) * (size_t)n);
    cols = (int*)XMalloc_System(sizeof(int) * (size_t)n);
    if (!rows || !cols) {
        if (rows) XFree_System(rows);
        if (cols) XFree_System(cols);
        return false;
    }
    n = XItemSelectionModel_selectedIndexes(sm, rows, cols, n);
    for (i = 0; i < n; ++i) {
        if (rows[i] == row && cols[i] == col) {
            XFree_System(rows);
            XFree_System(cols);
            return true;
        }
    }
    XFree_System(rows);
    XFree_System(cols);
    return false;
}

/** @brief 计算落点 row/col 与指示线（对标
 *        QAbstractItemViewPrivate::dropOn，qabstractitemview.cpp:2215-2263）。
 *        ① 首行 `if (event->isAccepted()) return false;`（Qt :2218 同款）：
 *        "已接受"=已被上层处理，不再重复落。2026-11-15 起可安全照搬——
 *        XDropEvent_init 已对齐 QDropEvent 构造缺省 ignore（qevent.cpp:
 *        3052 构造尾 `ignore()`），事件进视图时是"未接受"，dragMove 的
 *        "默认 ignore，通过才接受"路径与守卫不冲突（守卫拦 accepted==true）。
 *        ② 动作不在 supportedDropActions 内 ⇒ false（Qt :2230 同款门控）；
 *        ③ 命中条目→按 position 求 Above/Below/OnItem，未命中→OnViewport(root)。
 *        本框架未建树形层级：parent 恒为 NULL（root），Above/Below 只回
 *        row/col（Qt 同时把 index 换成 parent，此处等价于 root 平铺）。
 *        返回 true 表示可落（m_dropIndicatorPosition/m_dropRect 已缓存）。 */
static bool xaiv_dropOn(XAbstractItemView* view, const XDropEvent* drop,
                        int* outRow, int* outCol)
{
    int posX;
    int posY;
    int row = -1;
    int col = -1;
    XRect rect;
    int pos;
    if (!view || !drop) return false;
    /* Qt :2218 `if (event->isAccepted()) return false;`——已接受的 drop
     * 不再重复定位落点（差异③收口）。 */
    if (XEvent_isAccepted((const XEvent*)drop)) return false;
    *outRow = -1;
    *outCol = -1;
    posX = XDropEvent_position(drop).x;
    posY = XDropEvent_position(drop).y;
    if (!XAbstractItemView_indexAt_base(view, posX, posY, &row, &col)) {
        row = -1;
        col = -1;
    }
    /* ① qabstractitemview.cpp:2230 `if (model->supportedDropActions() &
     * event->dropAction())`——动作不受支持即不可落（不写指示线）。
     * 有效动作 = InternalMove 时恒 MoveAction（与 :2139-2140 的
     * droppingOnItself 同款归一）。
     * **IgnoreAction(0) 不得跳过门控**：Qt 里 `supported & 0` 为 0 ⇒ 不可落；
     * 而平台层当前从不填 dropAction（XDropEvent_init 置 IgnoreAction，
     * 全仓唯一生产者 XPlatformNativeWindow_posix.c:4549-4574 不调 setter）
     * ⇒ 若放行 0 就在真实路径上把最严格的落点门禁整段绕过。 */
    {
        int effAction = (view->m_dragDropMode ==
                         (uint8_t)XAbstractItemViewDragDropMode_InternalMove)
                            ? (int)XDropAction_MoveAction
                            : XDropEvent_dropAction(drop);
        if (view->m_model &&
            !(XAbstractItemModel_supportedDropActions_base(view->m_model) &
              effAction))
            return false;
    }
    /* 指示线位置与矩形（仅 OnItem 在命中条目上画框；Above/Below 画横线）。 */
    view->m_hasDropRect = 0;
    if (row >= 0 && col >= 0 && xaiv_visualRectOf(view, row, col, &rect)) {
        pos = xaiv_dropIndicatorPosition(view, posX, posY, &rect, row, col);
        view->m_dropIndicatorPosition = (uint8_t)pos;
        if (pos == XAbstractItemViewDropIndicatorPosition_OnItem) {
            view->m_hasDropRect = 1;
            view->m_dropRectX = rect.x;
            view->m_dropRectY = rect.y;
            view->m_dropRectW = rect.width;
            view->m_dropRectH = rect.height;
        } else if (pos == XAbstractItemViewDropIndicatorPosition_AboveItem ||
                   pos == XAbstractItemViewDropIndicatorPosition_BelowItem) {
            /* Qt :2082 用 viewport()->width()（视口宽，不含滚动条），
             * 不是控件本体宽。 */
            int w = xaiv_viewportWidth(view);
            int ry = (pos == XAbstractItemViewDropIndicatorPosition_AboveItem)
                         ? rect.y
                         : rect.y + rect.height;
            view->m_hasDropRect = 1;
            view->m_dropRectX = 0;
            view->m_dropRectY = ry;
            view->m_dropRectW = w;
            view->m_dropRectH = 0;
            *outRow = (pos == XAbstractItemViewDropIndicatorPosition_AboveItem)
                          ? row
                          : row + 1;
            *outCol = col;
        }
    } else {
        view->m_dropIndicatorPosition =
            (uint8_t)XAbstractItemViewDropIndicatorPosition_OnViewport;
        /* 命中视口空白：落点即 root（本框架树形层级未建，parent 恒 NULL）。
         * qabstractitemview.cpp:2110-2113：OnViewport 也要
         * `isIndexDropEnabled(rootIndex())` 才可落——root 无条目可查
         * （row/col<0）⇒ 不可落（缺省模型 flags 无 DropEnabled，与 Qt 同）。 */
        if (!xaiv_isIndexDropEnabled(view, -1, -1)) return false;
    }
    if (xaiv_droppingOnItself(view, drop, row, col)) return false;
    return true;
}

/** @brief 由框架 XDropEvent 重建 XMimeData（对标 Qt 拖放：QDropEvent 里
 *        携带的 QMimeData 与视图/模型层看到的是同一对象）。
 *
 *        本框架 XDropEvent 以 (mimeType, data=XByteArray) 承载（无多格式
 *        并行），此处据其重建一个 XMimeData 供模型侧 canDropMimeData/
 *        dropMimeData 消费（§B2 契约：模型侧接收 XMimeData）。
 *
 * @param drop 拖放事件（借用）。
 * @return 新建 XMimeData（调用方 XClassDelete）；drop 为空或分配失败返回 NULL。
 * @note   载荷二进制透明（2026-11-15 差异②收口后）：XDropEvent 载荷已是
 *         XByteArray，按字节原样直达（含 0x00 的模型 wire 不截断）；
 *         MIME 类型名仍走 XString_toUtf8。 */
static XMimeData* xaiv_mimeDataFromDrop(const XDropEvent* drop)
{
    XMimeData* md;
    XString* mt;
    XByteArray* dt;
    const char* m;
    const char* d;
    int dl;
    if (!drop) return NULL;
    md = XMimeData_create();
    if (!md) return NULL;
    mt = XDropEvent_mimeType(drop);
    dt = XDropEvent_data(drop);
    m = mt ? XString_toUtf8(mt) : "";
    /* 载荷二进制透明直达：XDropEvent 载荷已是 XByteArray（2026-11-15
     * 差异②收口），按字节原样进 XMimeData——模型 wire 大端首字节 0x00
     * 不再被 XString 通道截断。 */
    d = dt ? (const char*)XByteArray_data(dt) : "";
    dl = dt ? (int)XByteArray_size_base(dt) : 0;
    if (m && m[0])
        XMimeData_setData_bytes(md, m, (const unsigned char*)d, dl);
    if (mt) XClassDelete((XClass*)mt);
    if (dt) XClassDelete((XClass*)dt);
    return md;
}

/** @brief 是否存在可拖选中索引（对标 QAbstractItemViewPrivate::
 *        selectedDraggableIndexes 非空判定：选中且 flags 含 ItemIsDragEnabled）。
 *        本框架基类模型缺省 flags 为 Selectable|Enabled（无 DragEnabled），
 *        因此基类模型上恒 false——与 Qt 基类 QAbstractItemModel::flags 返回
 *        NoItemFlags 的同口径表现（可拖需派生模型加位）。 */
static bool xaiv_hasDraggableSelection(const XAbstractItemView* view)
{
    XItemSelectionModel* sm;
    int n;
    int i;
    int* rows;
    int* cols;
    bool any = false;
    if (!view || !view->m_model || !view->m_selectionModel) return false;
    sm = view->m_selectionModel;
    n = XItemSelectionModel_selectedIndexes(sm, NULL, NULL, 0);
    if (n <= 0) return false;
    rows = (int*)XMalloc_System(sizeof(int) * (size_t)n);
    cols = (int*)XMalloc_System(sizeof(int) * (size_t)n);
    if (!rows || !cols) {
        if (rows) XFree_System(rows);
        if (cols) XFree_System(cols);
        return false;
    }
    n = XItemSelectionModel_selectedIndexes(sm, rows, cols, n);
    for (i = 0; i < n; ++i) {
        if (XAbstractItemModel_flags_base(view->m_model, rows[i], cols[i]) &
            XItemFlag_ItemIsDragEnabled) { any = true; break; }
    }
    XFree_System(rows);
    XFree_System(cols);
    return any;
}

/** @brief 拖出启动距离 px（对标 QApplication::startDragDistance；
 *        XApplication 被裁剪时回退 Qt 缺省 10）。
 *
 * @return 像素距离；恒返回非负值。
 * @note 范式逐字对齐 Src/XGui/Widget/XWidget.c:3485-3494
 *       （xwidget_gestureStartDragDistance）：条件包含 + 常量回退，使
 *       XAPPLICATION_ON=0 / XGUIAPPLICATION_ON=0 的裁剪档可编译且不产生
 *       `U XApplication_startDragDistance` 未定义符号。 */
static int xaiv_startDragDistance(void)
{
#if XAPPLICATION_ON && XGUIAPPLICATION_ON
    int dist = XApplication_startDragDistance(); /* 内部已带 10 回退。 */
    if (dist >= 0) return dist;
#endif /* XAPPLICATION_ON && XGUIAPPLICATION_ON */
    return 10;
}

static bool xaiv_canDrop(XAbstractItemView* view, const XDropEvent* drop)
{
    int action;
    const char* mime;
    XString* mt;
    XStringList* types;
    int i;
    int n;
    int row = -1;
    int col = -1;
    bool hit = false;
    if (!view || !drop || !view->m_model) return false;
    action = XDropEvent_dropAction(drop);
    /* DragEnter：动作被 supportedDropActions 允许且待落格式命中即接受
     * （qabstractitemview_p.h:140-155：严格 dropAction() & supportedDropActions()，
     * 不做动作回落——回落会在"动作不匹配"时错误接受）。 */
    if (XEvent_type((const XEvent*)drop) == XEVENT_TYPE_DRAG_ENTER) {
        int supported = XAbstractItemModel_supportedDropActions_base(view->m_model);
        if (!(action & supported)) return false;
        if (action == 0) return false;
        mt = XDropEvent_mimeType(drop);
        mime = mt ? XString_toUtf8(mt) : "";
        types = XAbstractItemModel_mimeTypes(view->m_model);
        n = types ? (int)XStringList_size_base(types) : 0;
        for (i = 0; i < n; ++i) {
            const XString* t = (const XString*)XStringList_at_base(types, (int64_t)i);
            const char* ts = t ? XString_toUtf8(t) : "";
            if (mime && XStrcmp(mime, ts) == 0) { hit = true; break; }
        }
        if (types) XClassDelete((XClass*)types);
        if (mt) XClassDelete((XClass*)mt);
        return hit;
    }
    /* dragMove/drop：经 dropOn 算落点后查 canDropMimeData（§B2 契约）。 */
    if (!xaiv_dropOn(view, drop, &row, &col)) return false;
    if (view->m_dragDropMode ==
            (uint8_t)XAbstractItemViewDragDropMode_InternalMove)
        action = (int)XDropAction_MoveAction;
    {
        XMimeData* md = xaiv_mimeDataFromDrop(drop);
        if (!md) return false;
        /* §B2 契约：模型侧 canDropMimeData(data,action,row,col,parent)；本框架
         * 未建树形层级，parent 以 NULL 承载（root）。 */
        hit = XAbstractItemModel_canDropMimeData(view->m_model, md, action,
                                                row, col, NULL);
        XClassDelete((XClass*)md);
    }
    return hit;
}

/** @brief 默认拖放动作协商（对标 QAbstractItemView::startDrag 的
 *        defaultDropAction 推导，qabstractitemview.cpp:3876-3883）。
 *
 *        Qt 原文：
 *          `if (dragDropMode() == InternalMove) supportedActions &= ~CopyAction;`
 *          `if (d->defaultDropAction != IgnoreAction && (supportedActions & d->defaultDropAction))`
 *          `    defaultDropAction = d->defaultDropAction;`
 *          `else if (supportedActions & CopyAction && dragDropMode() != InternalMove)`
 *          `    defaultDropAction = CopyAction;`
 *        ⇒ 返回值恒为**单一** DropAction（Ignore/Copy/Move/Link 之一），
 *        绝不返回多动作掩码——`XDropEvent_setDropAction` 只接受单一动作
 *        （Qt `QDropEvent::setDropAction` 文档：「只能取 proposedActions 中
 *        的一个」）。
 *
 * @param view     目标视图（借用）。
 * @param possible 事件可用动作位组合（XDropAction 按位或；用于与
 *                 defaultDropAction 求交，Qt 的 supportedActions 形参）。
 * @return 单一 XDropAction 值；无可协商时返回 IgnoreAction。
 */
static int xaiv_resolveDefaultAction(XAbstractItemView* view, int possible)
{
    int supported;
    if (!view || !view->m_model) return (int)XDropAction_IgnoreAction;
    supported = XAbstractItemModel_supportedDropActions_base(view->m_model);
    /* Qt :3876-3877：InternalMove 先剥掉 CopyAction。 */
    if (view->m_dragDropMode ==
        (uint8_t)XAbstractItemViewDragDropMode_InternalMove)
        supported &= ~(int)XDropAction_CopyAction;
    /* Qt :3878-3879：defaultDropAction 在 (supported ∩ possible) 内即采用。 */
    if (view->m_defaultDropAction != 0 &&
        (supported & (int)view->m_defaultDropAction) &&
        (possible & (int)view->m_defaultDropAction))
        return (int)view->m_defaultDropAction;
    /* Qt :3881：非 InternalMove 且 supported 含 Copy ⇒ Copy。 */
    if ((supported & (int)XDropAction_CopyAction) &&
        view->m_dragDropMode !=
            (uint8_t)XAbstractItemViewDragDropMode_InternalMove)
        return (int)XDropAction_CopyAction;
    /* 其余：在 (supported ∩ possible) 里取第一个单一动作（Move→Link 序）。
     * 绝不返回组合掩码（setDropAction 只接受单一动作）。 */
    if ((supported & possible & (int)XDropAction_MoveAction))
        return (int)XDropAction_MoveAction;
    if ((supported & possible & (int)XDropAction_LinkAction))
        return (int)XDropAction_LinkAction;
    return (int)XDropAction_IgnoreAction;
}

/** @brief 拖动进入（对标 QAbstractItemView::dragEnterEvent，
 *        qabstractitemview.cpp:2036-2052）：InternalMove 且（源非自身或
 *        Move 不被允许）直接返回；canDrop 通过则 accept + setState(
 *        DraggingState)，否则 ignore。 */
static void VXAbstractItemView_dragEnterEvent(XWidget* self, XEvent* event)
{
    XAbstractItemView* view = (XAbstractItemView*)self;
    XDropEvent* drop = (XDropEvent*)event;
    if (!view || !event) return;
    /* 内部移动：Move 不被允许时拒绝（Qt 同口径；源判定本端口无 source，
     * 见本段 §B6 差异登记①）。
     * Qt 此处是 bare `return;`（事件保持 QDropEvent 的缺省"未接受"）；本框架
     * XEvent 缺省 accepted=true，故必须显式 ignore 才等价——否则平台层
     * XWindowSystemInterface.c:707 会把"被门控拒绝"当成功回给 XDND。 */
    if (view->m_dragDropMode ==
            (uint8_t)XAbstractItemViewDragDropMode_InternalMove &&
        !((XDropEvent_possibleActions(drop) & (int)XDropAction_MoveAction))) {
        XEvent_ignore(event);
        return;
    }
    if (xaiv_canDrop(view, drop)) {
        XEvent_accept(event);
        /* qabstractitemview.cpp:2046 setState(DraggingState)。 */
        view->m_state = (uint8_t)XAbstractItemViewDragState_DraggingState;
    } else {
        XEvent_ignore(event);
    }
}

/** @brief 拖动移动（对标 QAbstractItemView::dragMoveEvent，
 *        qabstractitemview.cpp:2058-2127）：默认忽略 → InternalMove 早退 →
 *        canDrop（内部 dropOn 已算指示线位置/矩形，含 droppingOnItself 过滤）
 *        → 通过则接受建议动作 + SelectRows 整行拓宽 → 边缘自动滚动。 */
static void VXAbstractItemView_dragMoveEvent(XWidget* self, XEvent* event)
{
    XAbstractItemView* view = (XAbstractItemView*)self;
    XDropEvent* drop = (XDropEvent*)event;
    XPoint pos;
    int action;
    XRect r;
    if (!view || !event) return;
    pos = XDropEvent_position(drop);
    view->m_dragPosX = pos.x;
    view->m_dragPosY = pos.y;
    if (view->m_dragDropMode ==
            (uint8_t)XAbstractItemViewDragDropMode_InternalMove &&
        !((XDropEvent_possibleActions(drop) & (int)XDropAction_MoveAction))) {
        /* 同 dragEnterEvent：Qt bare return ⇒ 本框架须显式 ignore。 */
        XEvent_ignore(event);
        return;
    }
    XEvent_ignore(event); /* 默认忽略，canDrop 通过才接受。 */
    if (!xaiv_canDrop(view, drop)) {
        /* 不可落：清指示线并重绘（Qt 的 dropIndicatorRect = QRect() 分支）。 */
        view->m_hasDropRect = 0;
        view->m_dropIndicatorPosition =
            (uint8_t)XAbstractItemViewDropIndicatorPosition_OnViewport;
        XWidget_update((XWidget*)view);
        return;
    }
    action = XDropEvent_dropAction(drop);
    if (action == (int)XDropAction_IgnoreAction)
        action = xaiv_resolveDefaultAction(view,
                                          XDropEvent_possibleActions(drop));
    /* qabstractitemview.cpp:2074：`if (index.isValid() && d->showDropIndicator)`
     * ——关掉指示线时既不写 m_dropRect 也不保留位置（把位置归 OnViewport，
     * 语义等价 Qt 的 dropIndicatorRect=QRect() 分支，只是不画）。
     * 此前 m_showDropIndicator 只有 setter/getter 无人读 = 死旋钮。 */
    if (!view->m_showDropIndicator) {
        view->m_hasDropRect = 0;
        view->m_dropIndicatorPosition =
            (uint8_t)XAbstractItemViewDropIndicatorPosition_OnViewport;
        XDropEvent_setDropAction(drop, action);
        XEvent_accept(event);
        XWidget_update((XWidget*)view);
        if (XAbstractItemView_shouldAutoScroll(view, pos.x, pos.y))
            XAbstractItemView_startAutoScroll(view);
        return;
    }
    /* SelectRows 且 Above/Below 时把指示线矩形拓宽到视口整行
     * （qabstractitemview.cpp:2078-2084）。 */
    if (view->m_selectionBehavior ==
            (uint8_t)XAbstractItemViewSelectionBehavior_SelectRows &&
        view->m_dropIndicatorPosition !=
            (uint8_t)XAbstractItemViewDropIndicatorPosition_OnViewport &&
        view->m_dropIndicatorPosition !=
            (uint8_t)XAbstractItemViewDropIndicatorPosition_OnItem) {
        int row = -1, col = -1;
        if (XAbstractItemView_indexAt_base(view, pos.x, pos.y, &row, &col)
            && row >= 0 && xaiv_visualRectOf(view, row, 0, &r)) {
            view->m_dropRectX = r.x;
            view->m_dropRectY =
                (view->m_dropIndicatorPosition ==
                 (uint8_t)XAbstractItemViewDropIndicatorPosition_AboveItem)
                    ? r.y : r.y + r.height;
            /* Qt :2082 `viewport()->width() - 1 - rect.x()`。 */
            view->m_dropRectW = xaiv_viewportWidth(view) - 1 - r.x;
            view->m_dropRectH = 0;
        }
    }
    XDropEvent_setDropAction(drop, action);
    XEvent_accept(event);
    XWidget_update((XWidget*)view);
    /* 边缘自动滚动（对标 Qt 的 shouldAutoScroll→startAutoScroll）。 */
    if (XAbstractItemView_shouldAutoScroll(view, pos.x, pos.y))
        XAbstractItemView_startAutoScroll(view);
}

/** @brief 拖动离开（对标 QAbstractItemView::dragLeaveEvent，
 *        qabstractitemview.cpp:2177）：停自动滚动、复位状态、清指示线、重绘。 */
static void VXAbstractItemView_dragLeaveEvent(XWidget* self, XEvent* event)
{
    XAbstractItemView* view = (XAbstractItemView*)self;
    (void)event;
    if (!view) return;
    XAbstractItemView_stopAutoScroll(view);
    view->m_state = (uint8_t)XAbstractItemViewDragState_NoState;
    view->m_dropIndicatorPosition =
        (uint8_t)XAbstractItemViewDropIndicatorPosition_OnItem;
    view->m_hasDropRect = 0;
    XWidget_update((XWidget*)view);
}

/** @brief 放置（对标 QAbstractItemView::dropEvent，
 *        qabstractitemview.cpp:2177）：定位落点→模型 dropMimeData→接受/忽略。 */
static void VXAbstractItemView_dropEvent(XWidget* self, XEvent* event)
{
    XAbstractItemView* view = (XAbstractItemView*)self;
    XDropEvent* drop = (XDropEvent*)event;
    int action;
    int row = -1;
    int col = -1;
    if (!view || !event || !view->m_model) return;
    if (view->m_dragDropMode ==
            (uint8_t)XAbstractItemViewDragDropMode_InternalMove) {
        if (!((XDropEvent_possibleActions(drop) & (int)XDropAction_MoveAction))) {
            /* 同 dragEnterEvent：Qt bare return ⇒ 本框架须显式 ignore。 */
            XEvent_ignore(event);
            return;
        }
    }
    /* 协商动作（Qt：InternalMove→MoveAction；否则取事件当前动作或默认）。 */
    action = (view->m_dragDropMode ==
               (uint8_t)XAbstractItemViewDragDropMode_InternalMove)
                 ? (int)XDropAction_MoveAction
                 : XDropEvent_dropAction(drop);
    if (action == (int)XDropAction_IgnoreAction)
        action = xaiv_resolveDefaultAction(view,
                                          XDropEvent_possibleActions(drop));
    if (!xaiv_dropOn(view, drop, &row, &col)) {
        /* 不可落：显式 ignore（**不能只靠缺省态**）。DQ：本框架
         * XEvent_init 缺省 m_accepted=true（XEvent.c:78）且平台路径
         * XWindowSystemInterface.c:697-706 靠 `handled && isAccepted()` 判成功，
         * 若不 ignore 则"模型拒绝/格式不符"会被上层当成功回给 XDND。 */
        XEvent_ignore(event);
        XAbstractItemView_stopAutoScroll(view);
        view->m_state = (uint8_t)XAbstractItemViewDragState_NoState;
        view->m_hasDropRect = 0;
        XWidget_update((XWidget*)view);
        return;
    }
    /* §B2 契约：模型侧 dropMimeData(data,action,row,col,parent)。本端口由
     * 框架 XDropEvent 的 (mimeType,data) 重建 XMimeData 后传入（见
     * xaiv_mimeDataFromDrop）；本框架未建树形层级，parent 以 NULL 承载（root）。 */
    {
        XMimeData* md = xaiv_mimeDataFromDrop(drop);
        bool applied = false;
        if (md) {
            applied = XAbstractItemModel_dropMimeData(view->m_model, md,
                                                    action, row, col, NULL);
            XClassDelete((XClass*)md);
        }
        if (applied) {
            XDropEvent_setDropAction(drop, action);
            XEvent_accept(event);
        } else {
            /* 模型拒绝：显式 ignore（同上门禁理由）。 */
            XEvent_ignore(event);
        }
    }
    XAbstractItemView_stopAutoScroll(view);
    view->m_state = (uint8_t)XAbstractItemViewDragState_NoState;
    view->m_hasDropRect = 0;
    XWidget_update((XWidget*)view);
}

/** @brief 拖出结束后的清源（对标 QAbstractItemViewPrivate::clearOrRemove，
 *        qabstractitemview.cpp:4539-4566）：
 *        - 非 overwrite（本框架 m_dragDropOverwriteMode==false，Qt 缺省同值
 *          qabstractitemview.cpp:69）：对「整行覆盖」的选中范围逐段
 *          removeRows——Qt 判据是选中范围 left==0 且 right==columnCount-1
 *          （:4546-4548）；本框架 selectionModel 无范围对象，按**行聚合**
 *          等价判定：该行全部列都在选中集合内才算整行覆盖。连续满行合并成
 *          一段一次 removeRows（对齐 Qt 的 range 粒度），段**从大到小**处理，
 *          避免删行后行号前移错位。
 *        - overwrite（true）：不能删行就重置条目（Qt :4556-4565 逐索引
 *          itemData 清空回写；本框架等价原语为模型的 clearItemData，
 *          全角色清空）。
 *
 * @param view 目标视图（借用）。
 * @note 调用时机由 startDrag 把守（exec 返回 Move 且源未被本视图 drop
 *       处理——Qt 的 dropEventMoved 基类恒 false，置位只发生在
 *       QListWidget/QTableWidget/QTreeWidget 的子类 dropEvent
 *       （qlistview.cpp:941 / qtableview.cpp:3169 / qtreewidget.cpp:3349），
 *       本框架基类视图无置位通道 ⇒ 条件退化为 exec==Moved）。
 */
static void xaiv_clearOrRemove(XAbstractItemView* view)
{
    XItemSelectionModel* sm;
    int n;
    int i;
    int colCount;
    int rowCount;
    int* rows = NULL;
    int* cols = NULL;
    int* selCount = NULL;
    unsigned char* fullRow = NULL;
    if (!view || !view->m_model || !view->m_selectionModel) return;
    sm = view->m_selectionModel;
    colCount = XAbstractItemModel_columnCount(view->m_model);
    rowCount = XAbstractItemModel_rowCount(view->m_model);
    if (colCount <= 0 || rowCount <= 0) return;
    n = XItemSelectionModel_selectedIndexes(sm, NULL, NULL, 0);
    if (n <= 0) return;
    rows = (int*)XMalloc_System(sizeof(int) * (size_t)n);
    cols = (int*)XMalloc_System(sizeof(int) * (size_t)n);
    selCount = (int*)XMalloc_System(sizeof(int) * (size_t)rowCount);
    fullRow = (unsigned char*)XMalloc_System((size_t)rowCount);
    if (!rows || !cols || !selCount || !fullRow) goto out;
    n = XItemSelectionModel_selectedIndexes(sm, rows, cols, n);
    XMemset(selCount, 0, sizeof(int) * (size_t)rowCount);
    XMemset(fullRow, 0, (size_t)rowCount);
    for (i = 0; i < n; ++i) {
        int r = rows[i];
        int c = cols[i];
        if (r < 0 || r >= rowCount || c < 0 || c >= colCount) continue;
        ++selCount[r];
    }
    for (i = 0; i < rowCount; ++i)
        if (selCount[i] >= colCount) fullRow[i] = 1;
    if (!view->m_dragDropOverwriteMode) {
        /* 删除段（连续满行合并）：倒序扫描记段起点/终点，段闭区间
         * [segStart..segEnd] 一次 removeRows；从大到小删，删行后行号
         * 前移不影响尚未处理的更小行号。 */
        int segStart = -1;
        int segEnd = -1;
        for (i = rowCount - 1; i >= -1; --i) {
            bool isFull = (i >= 0) ? (fullRow[i] != 0) : false;
            if (isFull) {
                if (segEnd < 0) segEnd = i; /* 段终点只记一次。 */
                segStart = i;
            } else if (segEnd >= 0) {
                (void)XAbstractItemModel_removeRows(view->m_model,
                                                    segStart,
                                                    segEnd - segStart + 1);
                segStart = -1;
                segEnd = -1;
            }
        }
    } else {
        /* Qt :4556-4565：overwrite 档不能删行 ⇒ 逐选中索引清空条目。
         * 删除/清空前快照已取好（rows/cols 是删除前的选中集合），Qt 同。 */
        for (i = 0; i < n; ++i) {
            if (rows[i] < 0 || rows[i] >= rowCount ||
                cols[i] < 0 || cols[i] >= colCount)
                continue;
            (void)XAbstractItemModel_clearItemData(view->m_model,
                                                   rows[i], cols[i]);
        }
    }
out:
    if (rows) XFree_System(rows);
    if (cols) XFree_System(cols);
    if (selCount) XFree_System(selCount);
    if (fullRow) XFree_System(fullRow);
}

/** @brief 发起拖出（对标 QAbstractItemView::startDrag，
 *        qabstractitemview.cpp:3856-3893）：
 *        ① selectedDraggableIndexes（仅 ItemIsDragEnabled 的选中索引）；
 *        ② model->mimeData(indexes)；
 *        ③ QDrag::exec(supportedActions, defaultDropAction)——本端口经
 *           XPlatformDrag_exec；InternalMove 时 `supportedActions &= ~CopyAction`
 *           （:3880 同款）；exec 返回 Move 且源未被本视图 drop 处理时
 *           clearOrRemove（:3879-3882 同款，见 xaiv_clearOrRemove）；
 *        ④ 结束后清指示线（dropIndicatorRect 空、dropIndicatorPosition=OnItem）。
 * @note   本框架缺件导致未落地的两小项（见本段 §B6 差异登记）：
 *         ① `drag->setPixmap/renderToPixmap` 拖拽缩略图渲染——框架无对应
 *            QDrag 渲染通道（XPlatformDrag 契约只有 mimeType+data+actions）；
 *         ② `drag->setHotSpot(pressedPosition - rect.topLeft())`——随①同缺。
 *         既有替代：无拖拽缩略图（源条目原地可见），与 Qt 无 pixmap 时
 *         的表现一致（QDrag 缺省画空 pixmap）。
 * @note   Qt :3880-3881 `dragDropMode() != InternalMove || drag->target()
 *         == viewport()`：InternalMove 档只有 drop 目标是本视口才删源
 *         （拖丢到视图外不删，防数据丢失）。本框架 XPlatformDrag 契约无
 *         target 查询 ⇒ InternalMove 档**保守不删**（取该条件的保守分支
 *         语义）；DragDrop 档 exec==Moved 恒删（对齐 Qt 非 InternalMove
 *         分支）。差异已登记（§B6 差异登记⑦）。 */
static void xaiv_startDrag(XAbstractItemView* view, int supportedActions)
{
    XMimeData* data;
#if XPLATFORMINTEGRATION_ON
    XPlatformDrag* drag; /* 类型只在 XPLATFORMINTEGRATION_ON 下存在
                          * （XPlatformDrag.h:26-29）。 */
#endif
    uint32_t actions;
    if (!view || !view->m_model || !view->m_selectionModel) return;
    /* ① 无选中可拖条目则不发起拖出（selectedDraggableIndexes 同义）。 */
    if (!xaiv_hasDraggableSelection(view)) return;
    /* InternalMove 去掉 CopyAction（qabstractitemview.cpp:3879-3880）。 */
    if (view->m_dragDropMode ==
        (uint8_t)XAbstractItemViewDragDropMode_InternalMove)
        supportedActions &= ~(int)XDropAction_CopyAction;
    actions = (uint32_t)supportedActions;
    if (actions == 0) return;
#if XMIMEDATA_ON
    /* ② §B2 契约：模型侧 mimeData 按选中索引编码。取 selectionModel 全部
     * 选中索引作为拖出源（flags 不含 ItemIsDragEnabled 的以 (-1,-1) 剔除，
     * 与 Qt selectedDraggableIndexes 同口径）。 */
    {
        int n, i;
        int* rows;
        int* cols;
        XItemSelectionModel* sm = view->m_selectionModel;
        n = XItemSelectionModel_selectedIndexes(sm, NULL, NULL, 0);
        if (n <= 0) return;
        rows = (int*)XMalloc_System(sizeof(int) * (size_t)n);
        cols = (int*)XMalloc_System(sizeof(int) * (size_t)n);
        if (!rows || !cols) {
            if (rows) XFree_System(rows);
            if (cols) XFree_System(cols);
            return;
        }
        n = XItemSelectionModel_selectedIndexes(sm, rows, cols, n);
        for (i = 0; i < n; ++i) {
            if (!((XAbstractItemModel_flags_base(view->m_model, rows[i], cols[i]) &
                   XItemFlag_ItemIsDragEnabled))) {
                rows[i] = -1; cols[i] = -1;
            }
        }
        data = XAbstractItemModel_mimeData(view->m_model,
                                           (const int*)rows,
                                           (const int*)cols, n);
        XFree_System(rows);
        XFree_System(cols);
        if (!data) return;
    }
    /* ③ 出站拖放引擎（XPlatformDrag 自带 create/exec/delete，不依赖应用层
     * 平台集成指针；嵌入式后端安全返回 Unsupported ≤ m_platformIntegration
     * 缺失时 XPlatformDrag_create 返回 NULL，本段整体空转）。
     * 随 XPLATFORMINTEGRATION_ON 裁剪：该开关关闭时 XPlatformDrag 类型与
     * 三个入口都不存在。 */
#if XPLATFORMINTEGRATION_ON
    drag = XPlatformDrag_create();
    if (drag) {
        XPlatformDragResult result =
            XPlatformDrag_exec(drag, XWidget_nativeWindow((XWidget*)view),
                               data, actions);
        XPlatformDrag_delete(drag);
        /* qabstractitemview.cpp:3879-3882：exec 返回 MoveAction 且
         * !dropEventMoved ⇒ clearOrRemove。dropEventMoved 基类恒 false
         * （置位只在 QListWidget:941/QTableWidget:3169/QTreeWidget:3349
         * 的子类 dropEvent；本框架基类视图无置位通道）⇒ 条件退化为
         * exec==Moved。Qt :3880-3881 的 InternalMove target==viewport
         * 豁免：target 不可观测 ⇒ InternalMove 档保守不删（防拖丢删源
         * 的数据丢失），见本函数 @note 与 §B6 差异登记⑦。 */
        if (result == XPlatformDragResult_Moved &&
            view->m_dragDropMode !=
                (uint8_t)XAbstractItemViewDragDropMode_InternalMove) {
            xaiv_clearOrRemove(view);
        }
    }
#else
    (void)actions; /* 无出站引擎档：actions 无消费者（避免 -Wunused）。 */
#endif /* XPLATFORMINTEGRATION_ON */
    XClassDelete((XClass*)data);
    /* ④ 复位指示线（qabstractitemview.cpp:3889-3891）。 */
    view->m_hasDropRect = 0;
    view->m_dropIndicatorPosition =
        (uint8_t)XAbstractItemViewDropIndicatorPosition_OnItem;
    XWidget_update((XWidget*)view);
#else
    (void)view;
    (void)actions;
#endif /* XMIMEDATA_ON */
}


#endif /* XWINDOWEVENT_ON */



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
                        view->m_selectionModel, r, c,
                        XItemSelectionModelSelectionFlag_NoUpdate);
                XWidget_update((XWidget*)view);
            } else if (view->m_selectionMode ==
                       XAbstractItemViewSelectionMode_MultiSelection) {
                /* MultiSelection 无修饰键导航（对标 Qt
                 * multiSelectionCommand 的 KeyPress 分支——非
                 * Space/Select 键回落 NoUpdate，qabstractitemview.cpp
                 * :4190-4196：键盘导航不触碰选择集合；本实现仍移动
                 * 当前项以保导航可用，偏离 Qt 的整键忽略语义）。 */
                xaiv_setCurrentPreservingSelection(view, r, c);
            } else if (view->m_selectionMode !=
                       XAbstractItemViewSelectionMode_NoSelection) {
                XAbstractItemView_setCurrentIndex(view, r, c);
                view->m_selectionAnchorRow = r;
                view->m_selectionAnchorCol = c;
                /* ClearAndSelect 分支重绘补齐：setCurrentIndex 只写当前
                 * 项/选择模型不触发 update（Ctrl 分支经
                 * xaiv_setCurrentPreservingSelection、Shift 分支在上方
                 * 各自补绘，唯本分支漏画）——活体实证（:140 私有重链，
                 * gdb 断点证 Down 两次达 XListWidget keyPress 并 accept
                 * 而 XAbstractItemView.c:316-341 全程无 XWidget_update）：
                 * 高亮钉死原行，第六轮"Down 纹丝不动"的本车道根因。
                 * 对标 Qt setCurrentIndex→d->viewport update 的重绘语义。
                 * update 幂等（脏区合并），Shift 分支的双调用无害。 */
                XWidget_update((XWidget*)view);
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
        /* Qt keyPressEvent 无开关：可打印键一律进 keyboardSearch。 */
        text[0] = (char)key;
        text[1] = '\0';
        XAbstractItemView_keyboardSearch(view, text);
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

#endif /* XGUI_ON */
