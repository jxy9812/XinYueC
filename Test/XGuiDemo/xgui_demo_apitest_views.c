/* xgui_demo_apitest_views.c —— 控件 API 测试族：views。
 *
 * 覆盖控件（对标 Qt 6.8.3）：XAbstractItemModel / XAbstractItemView /
 * XListView / XListWidget / XTreeView / XTreeWidget / XTableView /
 * XTableWidget / XHeaderView。
 *
 * 测试口径（见 xgui_demo_apitest.h 契约）：
 *  - 属性 setter/getter 往返一致；文档/实现确认的默认值直接断言，
 *    不确定的写注释不硬断言（防误报）；
 *  - 信号断言经 XObject_event_base 直发合成鼠标/键盘事件（与真实输入
 *    同路径，坐标为控件本地坐标），或直连公开信号函数计数；
 *  - 无头语义：控件不 show 直接调 API（全程未调用 XWidget_show，几何
 *    相关断言先 XWidget_setGeometry 固定视口尺寸）；
 *  - 每条断言中文注释标对标 Qt 的哪个行为；渲染/视觉效果不在断言职责。
 */
#include "xgui_demo_apitest.h"

#include "XObject.h"
#include "XEvent.h"
#include "XVarList.h"

#if XWIDGET_ON && XTABLEWIDGET_ON
#include "XAbstractItemModel.h"
#include "XAbstractItemView.h"
#include "XItemDelegate.h"
#include "XItemSelectionModel.h"
#include "XListView.h"
#include "XListWidget.h"
#include "XTreeView.h"
#include "XTreeWidget.h"
#include "XTableView.h"
#include "XTableWidget.h"
#include "XHeaderView.h"
#include "XLineEdit.h"
#include "XVector.h"
#if XByteArray_ON
#include "XByteArray.h"
#endif
#endif /* XWIDGET_ON && XTABLEWIDGET_ON */

#include <string.h>

#if XWIDGET_ON && XTABLEWIDGET_ON

/* ==================== 信号记录器（对标 QSignalSpy 的最小等价物） ==================== */

/** @brief 条目视图族信号计数与最近载荷（各族共用一份，断言前重置）。 */
typedef struct ViewsSigRec
{
    /* XAbstractItemModel 信号族（对标 QAbstractItemModel）。 */
    int dataChanged;           /**< dataChanged(row,col) 次数。 */
    int dcRow;                 /**< 最近 dataChanged 行。 */
    int dcCol;                 /**< 最近 dataChanged 列。 */
    int rowsInserted;          /**< rowsInserted(row,count) 次数。 */
    int riRow;                 /**< 最近 rowsInserted 起始行。 */
    int riCount;               /**< 最近 rowsInserted 行数。 */
    int rowsRemoved;           /**< rowsRemoved(row,count) 次数。 */
    int rrRow;                 /**< 最近 rowsRemoved 起始行。 */
    int rrCount;               /**< 最近 rowsRemoved 行数。 */
    /* XAbstractItemView 信号族。 */
    int iconSizeChanged;       /**< iconSizeChanged(w,h) 次数。 */
    int icW;                   /**< 最近 iconSizeChanged 宽。 */
    int icH;                   /**< 最近 iconSizeChanged 高。 */
    /* XListView 信号族。 */
    int indexesMoved;          /**< indexesMoved(rows,count) 次数。 */
    int imCount;               /**< 最近 indexesMoved 行数。 */
    /* XListWidget 信号族。 */
    int lwCurrentChanged;      /**< currentItemChanged(cur,prev) 次数。 */
    int lwCur;                 /**< 最近 currentItemChanged 当前行。 */
    int lwPrev;                /**< 最近 currentItemChanged 前行。 */
    int lwRowChanged;          /**< currentRowChanged(cur,prev) 次数。 */
    int lwTextChanged;         /**< currentTextChanged(text) 次数。 */
    char lwText[64];           /**< 最近 currentTextChanged 文本。 */
    int lwSelChanged;          /**< itemSelectionChanged 次数。 */
    int lwItemChanged;         /**< itemChanged(row) 次数。 */
    int lwICRow;               /**< 最近 itemChanged 行。 */
    int lwClicked;             /**< itemClicked(row) 次数。 */
    int lwClickedRow;          /**< 最近 itemClicked 行。 */
    int lwPressed;             /**< itemPressed 次数。 */
    int lwDoubleClicked;       /**< itemDoubleClicked 次数。 */
    int lwActivated;           /**< itemActivated 次数。 */
    /* XTreeView 信号族。 */
    int tvExpanded;            /**< expanded(row) 次数。 */
    int tvExpRow;              /**< 最近 expanded 行。 */
    int tvCollapsed;           /**< collapsed(row) 次数。 */
    int tvColRow;              /**< 最近 collapsed 行。 */
    /* XTreeWidget 信号族。 */
    int twItemExpanded;        /**< itemExpanded(row) 次数。 */
    int twItemCollapsed;       /**< itemCollapsed(row) 次数。 */
    int twItemClicked;         /**< itemClicked(row) 次数。 */
    int twItemClickedRow;      /**< 最近 itemClicked 行。 */
    int twItemDoubleClicked;   /**< itemDoubleClicked 次数。 */
    int twItemActivated;       /**< itemActivated 次数。 */
    int twItemEntered;         /**< itemEntered(row) 次数。 */
    int twEnteredRow;          /**< 最近 itemEntered 行。 */
    int twItemChanged;         /**< itemChanged(row) 次数。 */
    int twICRow;               /**< 最近 itemChanged 行。 */
    int twCurChanged;          /**< currentItemChanged 次数。 */
    int twSelChanged;          /**< itemSelectionChanged 次数。 */
    /* XHeaderView 信号族。 */
    int hvCountChanged;        /**< sectionCountChanged(old,new) 次数。 */
    int hvOldCount;            /**< 最近原段数。 */
    int hvNewCount;            /**< 最近新段数。 */
    int hvGeometries;          /**< geometriesChanged 次数。 */
    int hvSortIndicator;       /**< sortIndicatorChanged 次数。 */
    int hvSortSection;         /**< 最近 sortIndicatorChanged 段。 */
    int hvClearable;           /**< sortIndicatorClearableChanged 次数。 */
    int hvClearableOn;         /**< 最近 clearable 载荷。 */
    int hvSectionResized;      /**< sectionResized 次数。 */
    int hvSectionMoved;        /**< sectionMoved 次数。 */
    /* XTableWidget 信号族。 */
    int tblCellClicked;        /**< cellClicked(row,col) 次数。 */
    int tblCCRow;              /**< 最近 cellClicked 行。 */
    int tblCCCol;              /**< 最近 cellClicked 列。 */
    int tblCellPressed;        /**< cellPressed 次数。 */
    int tblCellDouble;         /**< cellDoubleClicked 次数。 */
    int tblCurCell;            /**< currentCellChanged 次数。 */
    int tblSelChanged;         /**< itemSelectionChanged 次数。 */
    int tblCellChanged;        /**< cellChanged(row,col) 次数。 */
    char order[8];             /**< 单击发射次序：'p'=cellPressed、
                                    'c'=cellClicked（同按先按压后点击）。 */
    int orderLen;              /**< order 已写入长度。 */
} ViewsSigRec;

static ViewsSigRec g_sig;

static void vsig_reset(void)
{
    memset(&g_sig, 0, sizeof(g_sig));
}

static void vw_modelDataChangedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_2(args, int, row, int, col);
    ++g_sig.dataChanged;
    g_sig.dcRow = row;
    g_sig.dcCol = col;
}

static void vw_modelRowsInsertedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_2(args, int, row, int, count);
    ++g_sig.rowsInserted;
    g_sig.riRow = row;
    g_sig.riCount = count;
}

static void vw_modelRowsRemovedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_2(args, int, row, int, count);
    ++g_sig.rowsRemoved;
    g_sig.rrRow = row;
    g_sig.rrCount = count;
}

/** @brief 模型三信号统一装配（sender==receiver 自连定式）。 */
static void vsig_connectModel(XObject* sender)
{
    XObject_connect_1(sender, XSignal(XAbstractItemModel_dataChanged_signal),
                      sender, vw_modelDataChangedSlot, XConnectionType_Direct);
    XObject_connect_1(sender, XSignal(XAbstractItemModel_rowsInserted_signal),
                      sender, vw_modelRowsInsertedSlot, XConnectionType_Direct);
    XObject_connect_1(sender, XSignal(XAbstractItemModel_rowsRemoved_signal),
                      sender, vw_modelRowsRemovedSlot, XConnectionType_Direct);
}

static void vw_iconSizeChangedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_2(args, int, w, int, h);
    ++g_sig.iconSizeChanged;
    g_sig.icW = w;
    g_sig.icH = h;
}

static void vw_indexesMovedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_2(args, const int*, rows, int, count);
    ++g_sig.indexesMoved;
    g_sig.imCount = count;
    (void)rows;
}

static void vw_lwCurrentChangedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_2(args, int, cur, int, prev);
    ++g_sig.lwCurrentChanged;
    g_sig.lwCur = cur;
    g_sig.lwPrev = prev;
}

static void vw_lwRowChangedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_2(args, int, cur, int, prev);
    (void)cur; (void)prev;
    ++g_sig.lwRowChanged;
}

static void vw_lwTextChangedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_1(args, const char*, text);
    ++g_sig.lwTextChanged;
    if (text) {
        size_t i;
        for (i = 0; i + 1 < sizeof(g_sig.lwText) && text[i]; ++i)
            g_sig.lwText[i] = text[i];
        g_sig.lwText[i] = '\0';
    } else {
        g_sig.lwText[0] = '\0';
    }
}

static void vw_lwSelChangedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    ++g_sig.lwSelChanged;
}

static void vw_lwItemChangedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_1(args, int, row);
    ++g_sig.lwItemChanged;
    g_sig.lwICRow = row;
}

static void vw_lwClickedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_1(args, int, row);
    ++g_sig.lwClicked;
    g_sig.lwClickedRow = row;
}

static void vw_lwPressedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    ++g_sig.lwPressed;
}

static void vw_lwDoubleClickedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    ++g_sig.lwDoubleClicked;
}

static void vw_lwActivatedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    ++g_sig.lwActivated;
}

/** @brief XListWidget 七信号统一装配。 */
static void vsig_connectList(XObject* sender)
{
    XObject_connect_1(sender, XSignal(XListWidget_currentItemChanged_signal),
                      sender, vw_lwCurrentChangedSlot, XConnectionType_Direct);
    XObject_connect_1(sender, XSignal(XListWidget_currentRowChanged_signal),
                      sender, vw_lwRowChangedSlot, XConnectionType_Direct);
    XObject_connect_1(sender, XSignal(XListWidget_currentTextChanged_signal),
                      sender, vw_lwTextChangedSlot, XConnectionType_Direct);
    XObject_connect_1(sender, XSignal(XListWidget_itemSelectionChanged_signal),
                      sender, vw_lwSelChangedSlot, XConnectionType_Direct);
    XObject_connect_1(sender, XSignal(XListWidget_itemChanged_signal),
                      sender, vw_lwItemChangedSlot, XConnectionType_Direct);
    XObject_connect_1(sender, XSignal(XListWidget_itemClicked_signal),
                      sender, vw_lwClickedSlot, XConnectionType_Direct);
    XObject_connect_1(sender, XSignal(XListWidget_itemPressed_signal),
                      sender, vw_lwPressedSlot, XConnectionType_Direct);
    XObject_connect_1(sender, XSignal(XListWidget_itemDoubleClicked_signal),
                      sender, vw_lwDoubleClickedSlot, XConnectionType_Direct);
    XObject_connect_1(sender, XSignal(XListWidget_itemActivated_signal),
                      sender, vw_lwActivatedSlot, XConnectionType_Direct);
}

static void vw_tvExpandedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_1(args, int, row);
    ++g_sig.tvExpanded;
    g_sig.tvExpRow = row;
}

static void vw_tvCollapsedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_1(args, int, row);
    ++g_sig.tvCollapsed;
    g_sig.tvColRow = row;
}

static void vw_twItemExpandedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_1(args, int, row);
    (void)row;
    ++g_sig.twItemExpanded;
}

static void vw_twItemCollapsedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_1(args, int, row);
    (void)row;
    ++g_sig.twItemCollapsed;
}

static void vw_twItemClickedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_1(args, int, row);
    ++g_sig.twItemClicked;
    g_sig.twItemClickedRow = row;
}

static void vw_twItemDoubleClickedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    ++g_sig.twItemDoubleClicked;
}

static void vw_twItemActivatedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    ++g_sig.twItemActivated;
}

static void vw_twItemEnteredSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_1(args, int, row);
    ++g_sig.twItemEntered;
    g_sig.twEnteredRow = row;
}

static void vw_twItemChangedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_1(args, int, row);
    ++g_sig.twItemChanged;
    g_sig.twICRow = row;
}

static void vw_twCurChangedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    ++g_sig.twCurChanged;
}

static void vw_twSelChangedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    ++g_sig.twSelChanged;
}

/** @brief XTreeWidget 点击/展开/变更信号统一装配。 */
static void vsig_connectTree(XObject* sender)
{
    XObject_connect_1(sender, XSignal(XTreeWidget_itemClicked_signal),
                      sender, vw_twItemClickedSlot, XConnectionType_Direct);
    XObject_connect_1(sender, XSignal(XTreeWidget_itemDoubleClicked_signal),
                      sender, vw_twItemDoubleClickedSlot, XConnectionType_Direct);
    XObject_connect_1(sender, XSignal(XTreeWidget_itemActivated_signal),
                      sender, vw_twItemActivatedSlot, XConnectionType_Direct);
    XObject_connect_1(sender, XSignal(XTreeWidget_itemEntered_signal),
                      sender, vw_twItemEnteredSlot, XConnectionType_Direct);
    XObject_connect_1(sender, XSignal(XTreeWidget_itemExpanded_signal),
                      sender, vw_twItemExpandedSlot, XConnectionType_Direct);
    XObject_connect_1(sender, XSignal(XTreeWidget_itemCollapsed_signal),
                      sender, vw_twItemCollapsedSlot, XConnectionType_Direct);
    XObject_connect_1(sender, XSignal(XTreeWidget_itemChanged_signal),
                      sender, vw_twItemChangedSlot, XConnectionType_Direct);
    XObject_connect_1(sender, XSignal(XTreeWidget_currentItemChanged_signal),
                      sender, vw_twCurChangedSlot, XConnectionType_Direct);
    XObject_connect_1(sender, XSignal(XTreeWidget_itemSelectionChanged_signal),
                      sender, vw_twSelChangedSlot, XConnectionType_Direct);
}

static void vw_hvCountChangedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_2(args, int, oldCount, int, newCount);
    ++g_sig.hvCountChanged;
    g_sig.hvOldCount = oldCount;
    g_sig.hvNewCount = newCount;
}

static void vw_hvGeometriesSlot(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    ++g_sig.hvGeometries;
}

static void vw_hvSortIndicatorSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_1(args, int, section);
    ++g_sig.hvSortIndicator;
    g_sig.hvSortSection = section;
}

static void vw_hvClearableSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_1(args, bool, clearable);
    ++g_sig.hvClearable;
    g_sig.hvClearableOn = clearable ? 1 : 0;
}

static void vw_hvSectionResizedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    ++g_sig.hvSectionResized;
}

static void vw_hvSectionMovedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    ++g_sig.hvSectionMoved;
}

/** @brief XHeaderView 几何/指示器信号统一装配。 */
static void vsig_connectHeader(XObject* sender)
{
    XObject_connect_1(sender, XSignal(XHeaderView_sectionCountChanged_signal),
                      sender, vw_hvCountChangedSlot, XConnectionType_Direct);
    XObject_connect_1(sender, XSignal(XHeaderView_geometriesChanged_signal),
                      sender, vw_hvGeometriesSlot, XConnectionType_Direct);
    XObject_connect_1(sender, XSignal(XHeaderView_sortIndicatorChanged_signal),
                      sender, vw_hvSortIndicatorSlot, XConnectionType_Direct);
    XObject_connect_1(sender,
                      XSignal(XHeaderView_sortIndicatorClearableChanged_signal),
                      sender, vw_hvClearableSlot, XConnectionType_Direct);
    XObject_connect_1(sender, XSignal(XHeaderView_sectionResized_signal),
                      sender, vw_hvSectionResizedSlot, XConnectionType_Direct);
    XObject_connect_1(sender, XSignal(XHeaderView_sectionMoved_signal),
                      sender, vw_hvSectionMovedSlot, XConnectionType_Direct);
}

static void vw_tblCellClickedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_2(args, int, row, int, col);
    ++g_sig.tblCellClicked;
    g_sig.tblCCRow = row;
    g_sig.tblCCCol = col;
    if (g_sig.orderLen < (int)sizeof(g_sig.order) - 1)
        g_sig.order[g_sig.orderLen++] = 'c';
}

static void vw_tblCellPressedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    ++g_sig.tblCellPressed;
    if (g_sig.orderLen < (int)sizeof(g_sig.order) - 1)
        g_sig.order[g_sig.orderLen++] = 'p';
}

static void vw_tblCellDoubleSlot(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    ++g_sig.tblCellDouble;
}

static void vw_tblCurCellSlot(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    ++g_sig.tblCurCell;
}

static void vw_tblSelChangedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    ++g_sig.tblSelChanged;
}

static void vw_tblCellChangedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    ++g_sig.tblCellChanged;
}

/** @brief XTableWidget 单击/双击/当前格/选区信号统一装配。 */
static void vsig_connectTable(XObject* sender)
{
    XObject_connect_1(sender, XSignal(XTableWidget_cellClicked_signal),
                      sender, vw_tblCellClickedSlot, XConnectionType_Direct);
    XObject_connect_1(sender, XSignal(XTableWidget_cellPressed_signal),
                      sender, vw_tblCellPressedSlot, XConnectionType_Direct);
    XObject_connect_1(sender, XSignal(XTableWidget_cellDoubleClicked_signal),
                      sender, vw_tblCellDoubleSlot, XConnectionType_Direct);
    XObject_connect_1(sender, XSignal(XTableWidget_currentCellChanged_signal),
                      sender, vw_tblCurCellSlot, XConnectionType_Direct);
    XObject_connect_1(sender,
                      XSignal(XTableWidget_itemSelectionChanged_signal),
                      sender, vw_tblSelChangedSlot, XConnectionType_Direct);
    XObject_connect_1(sender, XSignal(XTableWidget_cellChanged_signal),
                      sender, vw_tblCellChangedSlot, XConnectionType_Direct);
}

/* ==================== 合成事件注入（XObject_event_base 直发，与真实输入同路径） ==================== */

static void vw_injectMouse(XWidget* target, XEventType type, int x, int y)
{
    XMouseEvent me;
    XPoint pos;
    XPoint_init(&pos, x, y);
    XMouseEvent_init(&me, type, XMouseButton_LeftButton, 0, pos);
    /* m_buttons 为事件发生时按住的按键位掩码（对标 QMouseEvent::buttons）。 */
    me.m_buttons = XMouseButton_LeftButton;
    XObject_event_base((XObject*)target, (XEvent*)&me);
}

static void vw_injectPress(XWidget* target, int x, int y)
{
    vw_injectMouse(target, XEVENT_TYPE_MOUSE_BUTTON_PRESS, x, y);
}

static void vw_injectDblClick(XWidget* target, int x, int y)
{
    vw_injectMouse(target, XEVENT_TYPE_MOUSE_BUTTON_DBL_CLICK, x, y);
}

static void vw_injectMove(XWidget* target, int x, int y)
{
    vw_injectMouse(target, XEVENT_TYPE_MOUSE_MOVE, x, y);
}

static void vw_injectKey(XWidget* target, XEventType type, int key)
{
    XKeyEvent ke;
    XKeyEvent_init(&ke, type, key, 0);
    XObject_event_base((XObject*)target, (XEvent*)&ke);
}

/* ==================== 编辑器捕获委托（C 虚表子类，参照页面定式） ==================== */

XCLASS_DEFINE_BEGING(VwCapDelegate)
XCLASS_DEFINE_EXTEND_END(VwCapDelegate, XItemDelegate)

/** @brief 记录 createEditor 产物的委托子类（m_base 必须是第一个成员）。 */
typedef struct VwCapDelegate
{
    XItemDelegate m_base;   /**< 基类成员；必须是第一个。 */
    XWidget* m_lastEditor;  /**< 最近一次 createEditor 产物（借用）。 */
} VwCapDelegate;

static XWidget* VVwCapDelegate_createEditor(XItemDelegate* self,
                                            XAbstractItemView* view,
                                            int row, int col)
{
    VwCapDelegate* d = (VwCapDelegate*)self;
    d->m_lastEditor =
        XClass_Parent(XItemDelegate, EXItemDelegate_CreateEditor,
                      XWidget* (*)(XItemDelegate*, XAbstractItemView*,
                                   int, int))(self, view, row, col);
    return d->m_lastEditor;
}

XVtable* VwCapDelegate_class_init(void)
{
    XVTABLE_INIT_DEFAULT(VwCapDelegate)
    XVTABLE_INHERIT_XCLASS(XItemDelegate);
    XVTABLE_OVERLOAD_DEFAULT(EXItemDelegate_CreateEditor,
                             VVwCapDelegate_createEditor);
    return XVTABLE_DEFAULT;
}

static void VwCapDelegate_init(VwCapDelegate* self)
{
    if (!self) return;
    XItemDelegate_init(&self->m_base);
    self->m_lastEditor = NULL;
    XClassSetVtable(self, VwCapDelegate);
}

#endif /* XWIDGET_ON && XTABLEWIDGET_ON */

/* ==================== 入口 ==================== */

int xapi_views_run(void)
{
    int failures = 0;

#if XWIDGET_ON && XTABLEWIDGET_ON

    /* 委托捕获子类与哑指针（委托挂载测试的"非空借用"载体）。 */
    VwCapDelegate capDelegate;
    static int vw_dummyA;
    static int vw_dummyB;
    static int vw_dummyC;

    /* ================================================================
     * 1. XAbstractItemModel：内存二维模型（维度/数据/表头/信号/树预留）。
     * ================================================================ */
    {
        XAbstractItemModel model;

        vsig_reset();
        XAbstractItemModel_init(&model);
        vsig_connectModel((XObject*)&model);

        /* ---- 初始维度（Qt：新建 QAbstractItemModel 空模型） ---- */
        XAPI_EXPECT(XAbstractItemModel_rowCount(&model) == 0 &&
                    XAbstractItemModel_columnCount(&model) == 0,
                    "模型初始 0 行 0 列");
        /* ---- 维度扩展（对标 setDimension 直接扩展模型） ---- */
        XAbstractItemModel_setDimension(&model, 2, 3);
        XAPI_EXPECT(XAbstractItemModel_rowCount(&model) == 2 &&
                    XAbstractItemModel_columnCount(&model) == 3,
                    "setDimension(2,3) 后维度 2x3");
        XAPI_EXPECT(g_sig.rowsInserted == 1 && g_sig.riRow == 0 &&
                    g_sig.riCount == 2,
                    "维度扩展发射 rowsInserted(0,2)（row=旧行数）");
        /* ---- 数据读写往返（对标 setData → data） ---- */
        XAPI_EXPECT(XAbstractItemModel_setData_2(&model, 1, 2, "数据") &&
                    strcmp(xapi_cstr(XAbstractItemModel_data_2(&model, 1, 2)), "数据") == 0,
                    "setData/data(1,2) 往返一致");
        XAPI_EXPECT(g_sig.dataChanged == 1 && g_sig.dcRow == 1 &&
                    g_sig.dcCol == 2,
                    "setData 发射 dataChanged(1,2)");
        /* ---- 越界写拒绝且不发信号（Qt：setData 无效索引返回 false） ---- */
        XAPI_EXPECT(!XAbstractItemModel_setData_2(&model, 9, 9, "越界") &&
                    g_sig.dataChanged == 1,
                    "越界 setData 拒绝且不发 dataChanged");
        /* ---- 越界读：XString 版 NULL / UTF-8 版空串 ---- */
        XAPI_EXPECT(XAbstractItemModel_data(&model, 9, 9) == NULL &&
                    xapi_cstr(XAbstractItemModel_data_2(&model, 9, 9))[0] == '\0',
                    "越界 data 返回 NULL/data_2 返回空串");
        /* ---- value=NULL 清空（对标 setData(index, QVariant()) 清除） ---- */
        XAPI_EXPECT(XAbstractItemModel_setData_2(&model, 1, 2, NULL) &&
                    XAbstractItemModel_data(&model, 1, 2) == NULL,
                    "setData(NULL) 清空单元格");
        /* ---- 表头读写往返（对标 setHeaderData/headerData；0=水平 1=垂直） ---- */
        XAPI_EXPECT(XAbstractItemModel_setHeaderData_2(&model, 0, 0, "列A") &&
                    strcmp(XAbstractItemModel_headerData_2(&model, 0, 0),
                           "列A") == 0,
                    "水平表头 setHeaderData/headerData 往返");
        XAPI_EXPECT(XAbstractItemModel_setHeaderData_2(&model, 1, 1, "行B") &&
                    strcmp(XAbstractItemModel_headerData_2(&model, 1, 1),
                           "行B") == 0,
                    "垂直行头（orientation=1）往返");
        /* ---- 越界表头段拒绝（Qt：无效 section 返回 false） ---- */
        XAPI_EXPECT(!XAbstractItemModel_setHeaderData_2(&model, 9, 0, "越界") &&
                    XAbstractItemModel_headerData(&model, 9, 0) == NULL,
                    "越界表头段拒绝且读取为 NULL");
        /* ---- 表头 NULL 清空 ---- */
        XAPI_EXPECT(XAbstractItemModel_setHeaderData(&model, 0, 0, NULL) &&
                    XAbstractItemModel_headerData(&model, 0, 0) == NULL,
                    "setHeaderData(NULL) 清空表头");
        /* ---- 树形预留：扁平模型恒根（对标 parent() 根索引） ---- */
        XAPI_EXPECT(XAbstractItemModel_parent(&model, 0, 0) == -1 &&
                    XAbstractItemModel_parent(&model, 1, 1) == -1,
                    "扁平模型 parent 恒为根 -1");
        /* ---- 维度收缩：裁剪尾部 + rowsRemoved(row,count) ---- */
        XAbstractItemModel_setDimension(&model, 1, 1);
        XAPI_EXPECT(XAbstractItemModel_rowCount(&model) == 1 &&
                    XAbstractItemModel_columnCount(&model) == 1 &&
                    XAbstractItemModel_data(&model, 1, 2) == NULL,
                    "缩容到 1x1 后越界数据为 NULL");
        XAPI_EXPECT(g_sig.rowsRemoved == 1 && g_sig.rrRow == 1 &&
                    g_sig.rrCount == 1,
                    "缩容发射 rowsRemoved(1,1)");
        /* ---- 负值维度忽略（对标 setRowCount 负值无效） ---- */
        XAbstractItemModel_setRowCount(&model, -1);
        XAPI_EXPECT(XAbstractItemModel_rowCount(&model) == 1,
                    "setRowCount(-1) 负值忽略");
        /* ---- setColumnCount 补空列（对标 setColumnCount 扩展） ---- */
        XAbstractItemModel_setColumnCount(&model, 3);
        XAPI_EXPECT(XAbstractItemModel_columnCount(&model) == 3 &&
                    xapi_cstr(XAbstractItemModel_data_2(&model, 0, 2))[0] == '\0',
                    "setColumnCount(3) 补空列且新列为空");
        /* ---- setRowCount 扩行（不足补空行） ---- */
        XAbstractItemModel_setRowCount(&model, 4);
        XAPI_EXPECT(XAbstractItemModel_rowCount(&model) == 4 &&
                    xapi_cstr(XAbstractItemModel_data_2(&model, 3, 0))[0] == '\0',
                    "setRowCount(4) 补空行");

        XAbstractItemModel_deinit_base(&model);
    }

    /* ================================================================
     * 2. XAbstractItemView：视图基类公共合同（当前索引/选择/编辑触发/
     *    委托/role 数据/键盘搜索/编辑闭环）。
     * ================================================================ */
    {
        XAbstractItemView view;
        XAbstractItemModel* model = XAbstractItemModel_create();
        int r;
        int c;
        int w;
        int h;
        XRect rect;

        vsig_reset();
        XAbstractItemView_init(&view, NULL, 0);
        XObject_connect_1((XObject*)&view,
                          XSignal(XAbstractItemView_iconSizeChanged_signal),
                          (XObject*)&view, vw_iconSizeChangedSlot,
                          XConnectionType_Direct);
        if (model) {
            /* 信号装配：本节断言以 g_sig 计数验证编辑提交链（dataChanged），
               与第 1 节同样需要显式连接（套件计数器不跨节继承）。 */
            vsig_connectModel((XObject*)model);
            XAbstractItemModel_setDimension(model, 3, 2);
            XAbstractItemModel_setData_2(model, 0, 0, "alpha");
            XAbstractItemModel_setData_2(model, 1, 0, "beta");
            XAbstractItemModel_setData_2(model, 2, 0, "gamma");
            XAbstractItemModel_setData_2(model, 0, 1, "d0");
            XAbstractItemModel_setData_2(model, 1, 1, "d1");
            XAbstractItemModel_setData_2(model, 2, 1, "d2");
        }

        /* ---- 默认值（Qt 6.8 QAbstractItemView 构造默认） ---- */
        XAPI_EXPECT(XAbstractItemView_selectionMode(&view) ==
                    XAbstractItemViewSelectionMode_ExtendedSelection,
                    "默认 selectionMode=ExtendedSelection（Qt 文档缺省）");
        XAPI_EXPECT(XAbstractItemView_selectionBehavior(&view) ==
                    XAbstractItemViewSelectionBehavior_SelectItems,
                    "默认 selectionBehavior=SelectItems（Qt 缺省）");
        XAPI_EXPECT(XAbstractItemView_editTriggers(&view) ==
                    (XAbstractItemViewEditTrigger_DoubleClicked |
                     XAbstractItemViewEditTrigger_EditKeyPressed),
                    "默认 editTriggers=DoubleClicked|EditKeyPressed（Qt 缺省）");
        XAPI_EXPECT(!XAbstractItemView_alternatingRowColors(&view),
                    "默认 alternatingRowColors=false（Qt 缺省）");
        XAPI_EXPECT(XAbstractItemView_hasAutoScroll(&view),
                    "默认 autoScroll=true（Qt 缺省）");
        XAPI_EXPECT(XAbstractItemView_autoScrollMargin(&view) == 16,
                    "默认 autoScrollMargin=16（Qt 缺省）");
        /* 本库缺省 16x16；Qt iconSize 为样式相关值，不硬对标 Qt。 */
        XAPI_EXPECT(XAbstractItemView_iconWidth(&view) == 16 &&
                    XAbstractItemView_iconHeight(&view) == 16,
                    "默认 iconSize=16x16（本库缺省）");
        XAPI_EXPECT(XAbstractItemView_keyboardSearch(&view),
                    "默认 keyboardSearch 开启（本库缺省）");
        XAPI_EXPECT(!XAbstractItemView_tabKeyNavigation(&view),
                    "默认 tabKeyNavigation=false（Qt 缺省）");
        XAPI_EXPECT(XAbstractItemView_showDropIndicator(&view),
                    "默认 showDropIndicator=true（Qt 缺省）");
        XAPI_EXPECT(XAbstractItemView_defaultDropAction(&view) == 0,
                    "默认 defaultDropAction=IgnoreAction(0)（Qt 缺省）");
        XAPI_EXPECT(XAbstractItemView_dragDropMode(&view) ==
                    XAbstractItemViewDragDropMode_NoDragDrop,
                    "默认 dragDropMode=NoDragDrop（Qt 缺省）");
        XAPI_EXPECT(!XAbstractItemView_dragEnabled(&view),
                    "默认 dragEnabled=false（Qt 缺省）");
        /* Qt 基类默认 false（QTableView 子类覆写为 true）。 */
        XAPI_EXPECT(!XAbstractItemView_dragDropOverwriteMode(&view),
                    "默认 dragDropOverwriteMode=false（Qt 基类缺省）");
        XAPI_EXPECT(XAbstractItemView_textElideMode(&view) ==
                    XAbstractItemViewTextElideMode_ElideRight,
                    "默认 textElideMode=ElideRight（Qt 缺省）");
        XAPI_EXPECT(XAbstractItemView_verticalScrollMode(&view) ==
                    XAbstractItemViewScrollMode_ScrollPerItem &&
                    XAbstractItemView_horizontalScrollMode(&view) ==
                        XAbstractItemViewScrollMode_ScrollPerItem,
                    "默认垂直/水平滚动模式=ScrollPerItem（Qt 样式缺省）");
        XAPI_EXPECT(XAbstractItemView_currentRow(&view) == -1 &&
                    XAbstractItemView_currentColumn(&view) == -1,
                    "构造后无当前项（行/列=-1）");
        XAPI_EXPECT(XAbstractItemView_model(&view) == NULL &&
                    XAbstractItemView_selectionModel(&view) != NULL,
                    "初始无模型、选择模型已就绪");
        XAPI_EXPECT(XAbstractItemView_rootRow(&view) == -1 &&
                    XAbstractItemView_rootColumn(&view) == -1,
                    "初始根索引无效（-1）");

        /* ---- 属性往返 ---- */
        XAbstractItemView_setSelectionMode(&view,
            XAbstractItemViewSelectionMode_MultiSelection);
        XAPI_EXPECT(XAbstractItemView_selectionMode(&view) ==
                    XAbstractItemViewSelectionMode_MultiSelection,
                    "setSelectionMode 往返（MultiSelection）");
        XAbstractItemView_setSelectionBehavior(&view,
            XAbstractItemViewSelectionBehavior_SelectRows);
        XAPI_EXPECT(XAbstractItemView_selectionBehavior(&view) ==
                    XAbstractItemViewSelectionBehavior_SelectRows,
                    "setSelectionBehavior 往返（SelectRows）");
        XAbstractItemView_setEditTriggers(&view,
            XAbstractItemViewEditTrigger_NoEditTriggers);
        XAPI_EXPECT(XAbstractItemView_editTriggers(&view) ==
                    XAbstractItemViewEditTrigger_NoEditTriggers,
                    "setEditTriggers 往返（NoEditTriggers）");
        XAbstractItemView_setEditTriggers(&view,
            XAbstractItemViewEditTrigger_AllEditTriggers);
        XAPI_EXPECT(XAbstractItemView_itemsEditable(&view) == false,
                    "默认条目不可编辑门禁=false（对标 ItemIsEditable 未置位）");
        XAbstractItemView_setItemsEditable(&view, true);
        XAPI_EXPECT(XAbstractItemView_itemsEditable(&view),
                    "setItemsEditable 往返（对标 model flags 视图侧承载）");
        XAbstractItemView_setAlternatingRowColors(&view, true);
        XAbstractItemView_setAutoScroll(&view, false);
        XAbstractItemView_setAutoScrollMargin(&view, 32);
        XAPI_EXPECT(XAbstractItemView_alternatingRowColors(&view) &&
                    !XAbstractItemView_hasAutoScroll(&view) &&
                    XAbstractItemView_autoScrollMargin(&view) == 32,
                    "交替行色/自动滚动/边距 setter 往返");
        XAbstractItemView_setTextElideMode(&view,
            XAbstractItemViewTextElideMode_ElideMiddle);
        XAbstractItemView_setVerticalScrollMode(&view,
            XAbstractItemViewScrollMode_ScrollPerPixel);
        XAbstractItemView_setHorizontalScrollMode(&view,
            XAbstractItemViewScrollMode_ScrollPerPixel);
        XAPI_EXPECT(XAbstractItemView_textElideMode(&view) ==
                    XAbstractItemViewTextElideMode_ElideMiddle &&
                    XAbstractItemView_verticalScrollMode(&view) ==
                        XAbstractItemViewScrollMode_ScrollPerPixel,
                    "省略模式/垂直滚动模式 setter 往返");
        XAbstractItemView_resetVerticalScrollMode(&view);
        XAbstractItemView_resetHorizontalScrollMode(&view);
        XAPI_EXPECT(XAbstractItemView_verticalScrollMode(&view) ==
                    XAbstractItemViewScrollMode_ScrollPerItem &&
                    XAbstractItemView_horizontalScrollMode(&view) ==
                        XAbstractItemViewScrollMode_ScrollPerItem,
                    "reset 滚动模式复位 ScrollPerItem（对标 resetXXXScrollMode）");
        XAbstractItemView_setDragEnabled(&view, true);
        XAbstractItemView_setDragDropMode(&view,
            XAbstractItemViewDragDropMode_DragDrop);
        XAbstractItemView_setDragDropOverwriteMode(&view, true);
        XAbstractItemView_setDefaultDropAction(&view, 2 /* MoveAction */);
        XAbstractItemView_setDropIndicatorShown(&view, false);
        XAPI_EXPECT(XAbstractItemView_dragEnabled(&view) &&
                    XAbstractItemView_dragDropMode(&view) ==
                        XAbstractItemViewDragDropMode_DragDrop &&
                    XAbstractItemView_dragDropOverwriteMode(&view) &&
                    XAbstractItemView_defaultDropAction(&view) == 2 &&
                    !XAbstractItemView_showDropIndicator(&view),
                    "拖放族属性 setter 往返（drag/mode/overwrite/action/指示器）");
        XAbstractItemView_setTabKeyNavigation(&view, true);
        XAPI_EXPECT(XAbstractItemView_tabKeyNavigation(&view),
                    "setTabKeyNavigation 往返");
        /* ---- setIconSize 往返 + iconSizeChanged 信号 ---- */
        XAbstractItemView_setIconSize(&view, 24, 28);
        XAPI_EXPECT(XAbstractItemView_iconWidth(&view) == 24 &&
                    XAbstractItemView_iconHeight(&view) == 28,
                    "setIconSize(24,28) 往返");
        XAbstractItemView_iconSize(&view, &w, &h);
        XAPI_EXPECT(w == 24 && h == 28,
                    "iconSize 双输出查询与单项 getter 同源");
        XAPI_EXPECT(g_sig.iconSizeChanged == 1 && g_sig.icW == 24 &&
                    g_sig.icH == 28,
                    "图标尺寸变化发射 iconSizeChanged(24,28)");
        XAbstractItemView_setIconSize(&view, 0, 0);
        XAbstractItemView_setIconSize(&view, 24, 28);
        XAPI_EXPECT(XAbstractItemView_iconWidth(&view) == 24 &&
                    g_sig.iconSizeChanged == 1,
                    "非正尺寸拒绝、同值重复设置不重复发信号");
        XAbstractItemView_setKeyboardSearch(&view, false);
        XAbstractItemView_setKeyboardSearch(&view, true);
        XAPI_EXPECT(XAbstractItemView_keyboardSearch(&view),
                    "setKeyboardSearch 往返");

        /* ---- 模型挂接与当前索引（对标 setModel/setCurrentIndex） ---- */
        XAbstractItemView_setModel(&view, model);
        XAPI_EXPECT(XAbstractItemView_model(&view) == model,
                    "setModel/model 借用往返");
        XAbstractItemView_setCurrentIndex(&view, 1, 1);
        XAPI_EXPECT(XAbstractItemView_currentRow(&view) == 1 &&
                    XAbstractItemView_currentColumn(&view) == 1,
                    "setCurrentIndex(1,1) 后当前索引=(1,1)");
        XAPI_EXPECT(XAbstractItemView_selectionModel(&view) != NULL &&
                    XItemSelectionModel_isSelected(
                        XAbstractItemView_selectionModel(&view), 1, 1),
                    "当前项变更携带 Select 语义同步选中（SelectCurrent）");
        r = -9;
        c = -9;
        XAPI_EXPECT(XAbstractItemView_currentIndex(&view, &r, &c) &&
                    r == 1 && c == 1,
                    "currentIndex 双输出查询有效索引");
        XAbstractItemView_setCurrentIndex(&view, -1, -1);
        XAPI_EXPECT(XAbstractItemView_currentRow(&view) == -1 &&
                    !XAbstractItemView_currentIndex(&view, &r, &c),
                    "setCurrentIndex(-1,-1) 清除当前项");
        /* ---- selectAll 单选约束（Qt：单选仅选当前项） ---- */
        XAbstractItemView_setSelectionMode(&view,
            XAbstractItemViewSelectionMode_SingleSelection);
        XAbstractItemView_setCurrentIndex(&view, 0, 0);
        XAbstractItemView_clearSelection(&view);
        XAbstractItemView_selectAll(&view);
        XAPI_EXPECT(XAbstractItemView_selectionModel(&view) != NULL &&
                    XItemSelectionModel_selectedCount(
                        XAbstractItemView_selectionModel(&view)) == 1,
                    "单选模式 selectAll 仅选当前项（Qt 单选约束）");
        /* ---- selectAll 扩展模式全网格（Qt：全部可选单元格） ---- */
        XAbstractItemView_setSelectionMode(&view,
            XAbstractItemViewSelectionMode_ExtendedSelection);
        XAbstractItemView_selectAll(&view);
        XAPI_EXPECT(XItemSelectionModel_selectedCount(
                        XAbstractItemView_selectionModel(&view)) == 6,
                    "扩展模式 selectAll 全选 3x2=6 格");
        /* ---- clearSelection 保持当前项（Qt：clearSelection 不动当前） ---- */
        XAbstractItemView_clearSelection(&view);
        XAPI_EXPECT(XItemSelectionModel_selectedCount(
                        XAbstractItemView_selectionModel(&view)) == 0 &&
                    XAbstractItemView_currentRow(&view) == 0,
                    "clearSelection 清选中且保持当前项");
        /* ---- 选择模型替换（对标 setSelectionModel 接管所有权） ---- */
        {
            XItemSelectionModel* custom = XItemSelectionModel_create();
            XAbstractItemView_setSelectionModel(&view, custom);
            XAPI_EXPECT(XAbstractItemView_selectionModel(&view) == custom,
                        "setSelectionModel 替换为自定义选择模型");
            XAbstractItemView_setSelectionModel(&view, NULL);
            XAPI_EXPECT(XAbstractItemView_selectionModel(&view) != NULL,
                        "setSelectionModel(NULL) 重新懒创建");
        }
        /* ---- reset 复位（Qt：清当前/选择/根索引） ---- */
        XAbstractItemView_setCurrentIndex(&view, 2, 1);
        XAbstractItemView_setRootIndex(&view, 1, 1);
        XAbstractItemView_reset(&view);
        XAPI_EXPECT(XAbstractItemView_currentRow(&view) == -1 &&
                    XAbstractItemView_rootRow(&view) == -1,
                    "reset 复位当前索引与根索引");
        XAPI_EXPECT(XItemSelectionModel_selectedCount(
                        XAbstractItemView_selectionModel(&view)) == 0,
                    "reset 清空全部选中");
        /* ---- 根索引预留状态（rootIndex 平铺恒 (0,0)——头文件口径） ---- */
        XAbstractItemView_setRootIndex(&view, 2, 1);
        XAPI_EXPECT(XAbstractItemView_rootRow(&view) == 2 &&
                    XAbstractItemView_rootColumn(&view) == 1,
                    "setRootIndex 存储根偏移（树预留）");
        r = -9;
        c = -9;
        XAPI_EXPECT(XAbstractItemView_rootIndex(&view, &r, &c) &&
                    r == 0 && c == 0,
                    "平铺模型 rootIndex 恒 (0,0)");
        XAbstractItemView_setRootIndex(&view, -1, -1);

        /* ---- visualRect/indexAt 基类固定网格（80x24） ---- */
        XAPI_EXPECT(XAbstractItemView_visualRect(&view, 0, 0, &rect) &&
                    rect.x == 0 && rect.y == 0 && rect.width == 80 &&
                    rect.height == 24,
                    "基类 visualRect 固定网格 (0,0,80,24)");
        XAPI_EXPECT(!XAbstractItemView_visualRect(&view, -1, 0, &rect),
                    "visualRect 无效索引返回 false");
        {
            int hitRow = -9;
            int hitCol = -9;
            XAPI_EXPECT(XAbstractItemView_indexAt_base(&view, 10, 10,
                                                       &hitRow, &hitCol) &&
                        hitRow == 0 && hitCol == 0,
                        "indexAt(10,10) 命中 (0,0)");
            XAPI_EXPECT(XAbstractItemView_indexAt_base(&view, 85, 30,
                                                       &hitRow, &hitCol) &&
                        hitRow == 1 && hitCol == 1,
                        "indexAt(85,30) 命中 (1,1)（80x24 网格）");
            XAPI_EXPECT(!XAbstractItemView_indexAt_base(&view, 200, 10,
                                                        &hitRow, &hitCol),
                        "indexAt 列越界（>=模型列数）不命中");
        }
        /* ---- 尺寸提示（对标 sizeHintForColumn/Row/Index） ---- */
        XAPI_EXPECT(XAbstractItemView_sizeHintForColumn(&view, 0) == 80 &&
                    XAbstractItemView_sizeHintForRow(&view, 1) == 24,
                    "sizeHintForColumn/Row 返回网格 80x24");
        XAPI_EXPECT(XAbstractItemView_sizeHintForColumn(&view, 5) == -1,
                    "sizeHintForColumn 列越界返回 -1（同 Qt）");
        w = -9;
        h = -9;
        XAPI_EXPECT(XAbstractItemView_sizeHintForIndex(&view, 0, 1, &w, &h) &&
                    w == 80 && h == 24,
                    "sizeHintForIndex 输出建议宽高");
        w = -9;
        h = -9;
        XAPI_EXPECT(!XAbstractItemView_sizeHintForIndex(&view, -1, 0, &w, &h) &&
                    w == 0 && h == 0,
                    "sizeHintForIndex 无效索引失败且输出置 0");

        /* ---- 持久编辑器（对标 open/closePersistentEditor） ---- */
        XAbstractItemView_openPersistentEditor(&view, 0, 0);
        XAbstractItemView_openPersistentEditor(&view, 0, 1);
        XAPI_EXPECT(XAbstractItemView_isPersistentEditorOpen(&view, 0, 0) &&
                    XAbstractItemView_isPersistentEditorOpen(&view, 0, 1),
                    "openPersistentEditor 打开标记生效");
        XAbstractItemView_closePersistentEditor(&view, 0, 1);
        XAPI_EXPECT(!XAbstractItemView_isPersistentEditorOpen(&view, 0, 1) &&
                    XAbstractItemView_isPersistentEditorOpen(&view, 0, 0),
                    "closePersistentEditor 关闭且不影响其他格");
        XAbstractItemView_closePersistentEditor(&view, 0, 0);
        XAPI_EXPECT(!XAbstractItemView_isPersistentEditorOpen(&view, 0, 0),
                    "closePersistentEditor 幂等关闭");

        /* ---- 条目控件挂载（对标 setIndexWidget/indexWidget） ---- */
        {
            XWidget* iw = XWidget_create(NULL, 0);
            XAbstractItemView_setIndexWidget(&view, 0, 0, iw);
            XAPI_EXPECT(XAbstractItemView_indexWidget(&view, 0, 0) == iw,
                        "setIndexWidget/indexWidget 借用往返");
            XAbstractItemView_setIndexWidget(&view, 0, 0, NULL);
            XAPI_EXPECT(XAbstractItemView_indexWidget(&view, 0, 0) == NULL,
                        "setIndexWidget(NULL) 清除（同 Qt）");
            if (iw) XWidget_delete_base(iw);
        }
        /* ---- reset 清空持久编辑器与条目控件承载（Qt：reset 关闭全部） ---- */
        XAbstractItemView_openPersistentEditor(&view, 1, 1);
        XAbstractItemView_reset(&view);
        XAPI_EXPECT(!XAbstractItemView_isPersistentEditorOpen(&view, 1, 1),
                    "reset 清空持久编辑器打开标记");

        /* ---- role 数据（对标 data/setData 按 role） ---- */
        XAPI_EXPECT(
            XAbstractItemView_setItemText(&view, 0, 0,
                                          XItemDataRole_DisplayRole, "甲") &&
            strcmp(xapi_cstr(XAbstractItemView_itemText(
                       &view, 0, 0, XItemDataRole_DisplayRole)), "甲") == 0,
            "setItemText/itemText DisplayRole 转发模型通路");
        XAPI_EXPECT(strcmp(XAbstractItemView_itemText(
                               &view, 0, 0, XItemDataRole_EditRole),
                           "甲") == 0,
                    "EditRole 与 DisplayRole 同通道（同 Qt 默认模型同值）");
        XAPI_EXPECT(!XAbstractItemView_setItemText(
                        &view, 0, 1, XItemDataRole_CheckStateRole, "x"),
                    "非文本 role 写入拒绝（返回 false）");
        XAPI_EXPECT(strcmp(xapi_cstr(XAbstractItemView_itemText(
                               &view, 0, 0, XItemDataRole_FontRole)), "") == 0,
                    "非文本 role 文本通道返回空串");
        XAPI_EXPECT(XAbstractItemView_itemCheckState(&view, 0, 0) == -1,
                    "CheckState 未设置为 -1（本库以 -1 区分未勾选）");
        XAbstractItemView_setItemCheckState(
            &view, 0, 0, XItemCheckState_Checked);
        XAPI_EXPECT(XAbstractItemView_itemCheckState(&view, 0, 0) ==
                    XItemCheckState_Checked,
                    "setItemCheckState 往返（Checked=2，对标 Qt::Checked）");
        XAbstractItemView_setItemCheckState(&view, 0, 0, -1);
        XAPI_EXPECT(XAbstractItemView_itemCheckState(&view, 0, 0) == -1,
                    "setItemCheckState(-1) 清除该格设置");
        XAPI_EXPECT(XAbstractItemView_itemCheckState(&view, 9, 9) == -1,
                    "越界 itemCheckState 返回 -1");
        XAPI_EXPECT(XAbstractItemView_itemTextAlignment(&view, 0, 0) == 0,
                    "TextAlignment 未设置为 0");
        XAbstractItemView_setItemTextAlignment(&view, 0, 0,
                                               (int)XAlignment_HCenter);
        XAPI_EXPECT(XAbstractItemView_itemTextAlignment(&view, 0, 0) ==
                    (int)XAlignment_HCenter,
                    "setItemTextAlignment 往返（对标 TextAlignmentRole）");
        XAbstractItemView_setItemTextAlignment(&view, 0, 0, 0);
        XAPI_EXPECT(XAbstractItemView_itemTextAlignment(&view, 0, 0) == 0,
                    "setItemTextAlignment(0) 清除该格设置");
        XAPI_EXPECT(XAbstractItemView_itemFont(&view, 0, 0) == NULL,
                    "FontRole 未设置为 NULL");
        XAbstractItemView_setItemFont(&view, 0, 0,
                                      (const XFont*)(void*)&vw_dummyA);
        XAPI_EXPECT(XAbstractItemView_itemFont(&view, 0, 0) ==
                        (const XFont*)(void*)&vw_dummyA,
                    "setItemFont 借用往返（不转移所有权）");
        XAbstractItemView_setItemFont(&view, 0, 0, NULL);
        XAPI_EXPECT(XAbstractItemView_itemFont(&view, 0, 0) == NULL,
                    "setItemFont(NULL) 清除");
        XAPI_EXPECT(XAbstractItemView_itemDecoration(&view, 0, 0) == NULL,
                    "DecorationRole 未设置为 NULL");
        XAbstractItemView_setItemDecoration(&view, 0, 0,
                                            (const void*)&vw_dummyB);
        XAPI_EXPECT(XAbstractItemView_itemDecoration(&view, 0, 0) ==
                        (const void*)&vw_dummyB,
                    "setItemDecoration 借用往返");
        XAbstractItemView_setItemDecoration(&view, 0, 0, NULL);
        XAPI_EXPECT(XAbstractItemView_itemDecoration(&view, 0, 0) == NULL,
                    "setItemDecoration(NULL) 清除");

        /* ---- 键盘搜索（对标 keyboardSearch 前缀渐进匹配） ---- */
        vsig_reset();
        XAbstractItemView_setCurrentIndex(&view, -1, -1);
        XAPI_EXPECT(XAbstractItemView_keyboardSearch_2(&view, "ga") &&
                    XAbstractItemView_currentRow(&view) == 2,
                    "keyboardSearch(\"ga\") 前缀命中 gamma（行 2）");
        XAPI_EXPECT(!XAbstractItemView_keyboardSearch_2(&view, ""),
                    "keyboardSearch(\"\") 仅重置前缀并返回 false");
        XAbstractItemView_setCurrentIndex(&view, 2, 0);
        XAPI_EXPECT(XAbstractItemView_keyboardSearch_2(&view, "be") &&
                    XAbstractItemView_currentRow(&view) == 1,
                    "累积前缀无命中回退本次文本并环形命中 beta");
        XAbstractItemView_setKeyboardSearch(&view, false);
        XAPI_EXPECT(!XAbstractItemView_keyboardSearch_2(&view, "be"),
                    "开关关闭时 keyboardSearch 返回 false");
        XAbstractItemView_setKeyboardSearch(&view, true);

        /* ---- 委托承载（对标 itemDelegate 族与解析优先级） ---- */
        XAPI_EXPECT(XAbstractItemView_itemDelegate(&view) == NULL,
                    "未设置视图级委托时 itemDelegate 为 NULL");
        XAbstractItemView_setItemDelegate(&view, (void*)&vw_dummyA);
        XAPI_EXPECT(XAbstractItemView_itemDelegate(&view) == (void*)&vw_dummyA,
                    "setItemDelegate 借用往返");
        XAbstractItemView_setItemDelegate(&view, NULL);
        XAPI_EXPECT(XAbstractItemView_itemDelegate(&view) == NULL,
                    "setItemDelegate(NULL) 恢复默认委托承载");
        XAbstractItemView_setItemDelegateForColumn(&view, 1,
                                                   (void*)&vw_dummyB);
        XAPI_EXPECT(XAbstractItemView_itemDelegateForColumn(&view, 1) ==
                        (void*)&vw_dummyB &&
                    XAbstractItemView_itemDelegateForColumn(&view, 0) == NULL,
                    "列级委托按列承载（未设列为 NULL）");
        XAbstractItemView_setItemDelegateForColumn(&view, -1,
                                                   (void*)&vw_dummyB);
        XAPI_EXPECT(XAbstractItemView_itemDelegateForColumn(&view, -1) == NULL,
                    "列号 <0 的列级委托忽略");
        XAbstractItemView_setItemDelegateForRow(&view, 2, (void*)&vw_dummyC);
        XAPI_EXPECT(XAbstractItemView_itemDelegateForRow(&view, 2) ==
                        (void*)&vw_dummyC,
                    "行级委托按行承载");
        XAPI_EXPECT(XAbstractItemView_itemDelegateForIndex(&view, 2, 1) ==
                        (void*)&vw_dummyC,
                    "解析优先级：行级委托优先");
        XAPI_EXPECT(XAbstractItemView_itemDelegateForIndex(&view, 0, 1) ==
                        (void*)&vw_dummyB,
                    "解析优先级：无行级覆写回落列级");
        XAPI_EXPECT(XAbstractItemView_itemDelegateForIndex(&view, 0, 0) == NULL,
                    "解析优先级：全未设置回落视图默认（NULL）");
        XAbstractItemView_setItemDelegateForRow(&view, 2, NULL);
        XAbstractItemView_setItemDelegateForColumn(&view, 1, NULL);
        XAPI_EXPECT(XAbstractItemView_itemDelegateForIndex(&view, 0, 1) == NULL,
                    "传 NULL 清除行/列级覆盖（同 Qt）");

        /* ---- 编辑闭环（对标 edit/commitData/closeEditor 全链） ---- */
        VwCapDelegate_init(&capDelegate);
        XAbstractItemView_setItemDelegate(&view, &capDelegate);
        XAbstractItemView_setEditTriggers(&view,
            XAbstractItemViewEditTrigger_AllEditTriggers);
        XAbstractItemView_setItemsEditable(&view, false);
        XAPI_EXPECT(!XAbstractItemView_edit(&view, 0, 0),
                    "条目不可编辑门禁关闭时 edit 失败");
        XAbstractItemView_setItemsEditable(&view, true);
        XAbstractItemView_setEditTriggers(&view,
            XAbstractItemViewEditTrigger_NoEditTriggers);
        XAPI_EXPECT(!XAbstractItemView_edit(&view, 0, 0),
                    "NoEditTriggers 时 edit 判定失败（同 Qt 触发集判定）");
        XAbstractItemView_setEditTriggers(&view,
            XAbstractItemViewEditTrigger_AllEditTriggers);
        vsig_reset();
        XAPI_EXPECT(XAbstractItemView_edit(&view, 0, 0) &&
                    XAbstractItemView_isEditing(&view),
                    "edit(0,0) 开启编辑会话（委托 createEditor 产物）");
        XAPI_EXPECT(capDelegate.m_lastEditor != NULL,
                    "编辑器由委托 createEditor 创建并挂视图");
        XAPI_EXPECT(XAbstractItemView_edit(&view, 0, 0),
                    "同格重复 edit 幂等成功（同 Qt）");
        if (capDelegate.m_lastEditor) {
            XLineEdit_setText((XLineEdit*)capDelegate.m_lastEditor, "新值");
            XAbstractItemView_commitData(&view, capDelegate.m_lastEditor);
            XAPI_EXPECT(strcmp(XAbstractItemModel_data_2(model, 0, 0),
                               "新值") == 0,
                        "commitData 经委托 setModelData 落库（提交链）");
            {
                int before = g_sig.dataChanged;
                XAbstractItemView_commitData(&view, capDelegate.m_lastEditor);
                XAPI_EXPECT(g_sig.dataChanged == before,
                            "重复 commitData 幂等（提交一次性语义）");
            }
            XAbstractItemView_closeEditor(&view, capDelegate.m_lastEditor,
                                          XItemDelegateEndEditHint_NoHint);
            XAPI_EXPECT(!XAbstractItemView_isEditing(&view),
                        "closeEditor(NoHint) 关闭编辑会话");
        }
        vsig_reset();
        XAPI_EXPECT(XAbstractItemView_edit(&view, 1, 0) &&
                    capDelegate.m_lastEditor != NULL,
                    "再次 edit(1,0) 开启新会话");
        if (capDelegate.m_lastEditor) {
            XLineEdit_setText((XLineEdit*)capDelegate.m_lastEditor, "放弃");
            XAbstractItemView_closeEditor(&view, capDelegate.m_lastEditor,
                                          XItemDelegateEndEditHint_RevertModelCache);
            XAPI_EXPECT(!XAbstractItemView_isEditing(&view) &&
                        strcmp(XAbstractItemModel_data_2(model, 1, 0),
                               "beta") == 0,
                        "closeEditor(Revert) 放弃分支不写模型");
        }
        vsig_reset();
        XAbstractItemView_edit(&view, 2, 0);
        {
            int before = g_sig.dataChanged;
            XAbstractItemView_edit(&view, 0, 0);
            XAPI_EXPECT(XAbstractItemView_isEditing(&view) &&
                        g_sig.dataChanged == before + 1,
                        "他格编辑先按提交分支收敛旧会话（单编辑器收敛）");
        }
        XAbstractItemView_reset(&view);
        XAPI_EXPECT(!XAbstractItemView_isEditing(&view),
                    "reset 收敛活动编辑器（放弃分支）");
        XAbstractItemView_setItemDelegate(&view, NULL);
        XAbstractItemView_setItemsEditable(&view, false);

        /* ---- 滚动入口（滚动终值依赖派生视图内容尺寸，仅覆盖调用） ---- */
        XAbstractItemView_setModel(&view, model);
        XAbstractItemView_scrollTo(&view, 1, 1);
        XAbstractItemView_scrollToHint(&view, 2, 0,
                                       XAbstractItemViewScrollHint_PositionAtCenter);
        XAbstractItemView_scrollToTop(&view);
        XAbstractItemView_scrollToBottom(&view);
        XAbstractItemView_scrollTo(&view, -1, 0);
        XAPI_EXPECT(true, "scrollTo/scrollToHint/scrollToTop/Bottom 调用无崩溃");
        XAbstractItemView_doItemsLayout(&view);
        XAPI_EXPECT(XAbstractItemView_rootIndex(&view, &r, &c),
                    "doItemsLayout 后 rootIndex 查询仍有效（仅重绘请求）");

        if (model) XAbstractItemModel_delete_base(model);
        XAbstractItemView_deinit_base(&view);
    }

    /* ================================================================
     * 3. XListView：列表视图状态族（flow/grid/wrapping/viewMode 等）。
     * ================================================================ */
    {
        XListView lv;
        XAbstractItemModel* model = XAbstractItemModel_create();
        XRect rect;

        XListView_init(&lv, NULL, 0);
        XWidget_setGeometry((XWidget*)&lv, 0, 0, 200, 96);
        if (model) {
            XAbstractItemModel_setDimension(model, 3, 1);
            XAbstractItemModel_setData_2(model, 0, 0, "甲");
            XAbstractItemModel_setData_2(model, 1, 0, "乙");
            XAbstractItemModel_setData_2(model, 2, 0, "丙");
            XAbstractItemView_setModel(&lv.m_base, model);
        }

        /* ---- 默认值（Qt QListView 构造默认） ---- */
        XAPI_EXPECT(XListView_spacing(&lv) == 0,
                    "默认 spacing=0（Qt 缺省）");
        XAPI_EXPECT(XListView_modelColumn(&lv) == 0,
                    "默认 modelColumn=0（Qt 缺省）");
        XAPI_EXPECT(XListView_rowHeight(&lv) == 24,
                    "默认行高 24（本库扩展：统一行高）");
        XAPI_EXPECT(XListView_flow(&lv) == XListViewFlow_TopDown,
                    "默认 flow=TopDown（对标 Qt Flow_TopToBottom）");
        XAPI_EXPECT(XListView_gridSizeWidth(&lv) == -1 &&
                    XListView_gridSizeHeight(&lv) == -1,
                    "默认 gridSize 未启用（-1；Qt QSize(-1,-1) 未启用语义）");
        XAPI_EXPECT(!XListView_isWrapping(&lv),
                    "默认 wrapping=false（Qt 缺省）");
        XAPI_EXPECT(XListView_viewMode(&lv) == XListViewViewMode_ListMode,
                    "默认 viewMode=ListMode（Qt 缺省）");
        XAPI_EXPECT(XListView_resizeMode(&lv) == XListViewResizeMode_Static,
                    "默认 resizeMode=Static（Qt 缺省）");
        XAPI_EXPECT(XListView_layoutMode(&lv) ==
                    XListViewLayoutMode_SinglePass,
                    "默认 layoutMode=SinglePass（Qt 缺省）");
        XAPI_EXPECT(XListView_batchSize(&lv) == 100,
                    "默认 batchSize=100（Qt 缺省）");
        XAPI_EXPECT(XListView_movement(&lv) == XListViewMovement_Static,
                    "默认 movement=Static（Qt 缺省）");
        XAPI_EXPECT(!XListView_uniformItemSizes(&lv),
                    "默认 uniformItemSizes=false（Qt 缺省）");
        XAPI_EXPECT(XListView_itemAlignment(&lv) == 0,
                    "默认 itemAlignment=0（Qt 缺省无对齐覆写）");
        XAPI_EXPECT(!XListView_isSelectionRectVisible(&lv),
                    "默认 selectionRectVisible=false（Qt 缺省）");
        XAPI_EXPECT(!XListView_wordWrap(&lv),
                    "默认 wordWrap=false（Qt 缺省）");

        /* ---- 属性往返 ---- */
        XListView_setSpacing(&lv, 8);
        XListView_setModelColumn(&lv, 0);
        XListView_setRowHeight(&lv, 30);
        XAPI_EXPECT(XListView_spacing(&lv) == 8 &&
                    XListView_modelColumn(&lv) == 0 &&
                    XListView_rowHeight(&lv) == 30,
                    "spacing/modelColumn/rowHeight setter 往返");
        XListView_setFlow(&lv, XListViewFlow_LeftToRight);
        XAPI_EXPECT(XListView_flow(&lv) == XListViewFlow_LeftToRight,
                    "setFlow 往返（LeftToRight）");
        XListView_setFlow(&lv, 99);
        XAPI_EXPECT(XListView_flow(&lv) == XListViewFlow_LeftToRight,
                    "setFlow 非法值忽略（保持原值）");
        XListView_setGridSize(&lv, 60, 40);
        {
            int gw = -9;
            int gh = -9;
            XListView_gridSize(&lv, &gw, &gh);
            XAPI_EXPECT(XListView_gridSizeWidth(&lv) == 60 &&
                        XListView_gridSizeHeight(&lv) == 40 &&
                        gw == 60 && gh == 40,
                        "setGridSize/gridSize 往返（双输出同源）");
        }
        XListView_setGridSize(&lv, 0, -1);
        XAPI_EXPECT(XListView_gridSizeWidth(&lv) == -1 &&
                    XListView_gridSizeHeight(&lv) == -1,
                    "setGridSize <=0 归 -1（未启用语义）");
        XListView_setWrapping(&lv, true);
        XAPI_EXPECT(XListView_isWrapping(&lv),
                    "setWrapping 往返");
        XListView_setResizeMode(&lv, XListViewResizeMode_Adjust);
        XListView_setLayoutMode(&lv, XListViewLayoutMode_Batched);
        XListView_setMovement(&lv, XListViewMovement_Snap);
        XAPI_EXPECT(XListView_resizeMode(&lv) ==
                        XListViewResizeMode_Adjust &&
                    XListView_layoutMode(&lv) ==
                        XListViewLayoutMode_Batched &&
                    XListView_movement(&lv) == XListViewMovement_Snap,
                    "resizeMode/layoutMode/movement setter 往返");
        XListView_setBatchSize(&lv, 0);
        XAPI_EXPECT(XListView_batchSize(&lv) == 100,
                    "setBatchSize(<=0) 拒绝（Qt 拒绝非正值）");
        XListView_setBatchSize(&lv, 50);
        XAPI_EXPECT(XListView_batchSize(&lv) == 50,
                    "setBatchSize(50) 往返");
        XListView_setUniformItemSizes(&lv, true);
        XListView_setItemAlignment(&lv, (int)XAlignment_HCenter);
        XListView_setSelectionRectVisible(&lv, true);
        XListView_setWordWrap(&lv, true);
        XAPI_EXPECT(XListView_uniformItemSizes(&lv) &&
                    XListView_itemAlignment(&lv) == (int)XAlignment_HCenter &&
                    XListView_isSelectionRectVisible(&lv) &&
                    XListView_wordWrap(&lv),
                    "uniformItemSizes/itemAlignment/选区框/词换行往返");
        XListView_clearPropertyFlags(&lv);
        XAPI_EXPECT(XListView_wordWrap(&lv),
                    "clearPropertyFlags 为无操作（本库无属性标志体系）");

        /* ---- viewMode 联动（对标 Qt setViewMode 联动 wrapping/flow） ---- */
        XListView_setViewMode(&lv, XListViewViewMode_IconMode);
        XAPI_EXPECT(XListView_viewMode(&lv) == XListViewViewMode_IconMode &&
                    XListView_isWrapping(&lv) &&
                    XListView_flow(&lv) == XListViewFlow_LeftToRight,
                    "IconMode 联动 wrapping=true、flow=LeftToRight");
        XListView_setViewMode(&lv, XListViewViewMode_ListMode);
        XAPI_EXPECT(XListView_viewMode(&lv) == XListViewViewMode_ListMode &&
                    !XListView_isWrapping(&lv) &&
                    XListView_flow(&lv) == XListViewFlow_TopDown,
                    "ListMode 复位 wrapping=false、flow=TopDown");
        XListView_setViewMode(&lv, 42);
        XAPI_EXPECT(XListView_viewMode(&lv) == XListViewViewMode_ListMode,
                    "setViewMode 非法值忽略");

        /* ---- visualRect 与行隐藏（隐藏行不参与绘制/命中） ---- */
        /* 槽位节距 = 行高 + spacing（TopDown 累计口径）。上方 setter 往返
           留下 spacing=8，此处恢复缺省 0，使下方 y 期望值即纯行高累计。 */
        XListView_setSpacing(&lv, 0);
        rect = XListView_visualRect(&lv, 0);
        XAPI_EXPECT(rect.x == 0 && rect.y == 0 && rect.height == 30 &&
                    rect.width == 200,
                    "行 0 visualRect：槽高=行高、宽=视口宽");
        rect = XListView_visualRect(&lv, 2);
        XAPI_EXPECT(rect.y == 60,
                    "行 2 visualRect y=2x槽高（TopDown 槽位累计）");
        XAPI_EXPECT(XListView_visualRect(&lv, 9).width == 0,
                    "越界行 visualRect 为空矩形");
        XListView_setRowHidden(&lv, 0, true);
        XAPI_EXPECT(XListView_isRowHidden(&lv, 0),
                    "setRowHidden(0,true) 生效（对标 setRowHidden）");
        rect = XListView_visualRect(&lv, 2);
        XAPI_EXPECT(rect.y == 30,
                    "隐藏行不占位：行 2 y=30（跳过隐藏行 0）");
        XListView_setRowHidden(&lv, 9, true);
        XAPI_EXPECT(!XListView_isRowHidden(&lv, 9),
                    "行隐藏越界忽略且读取 false");
        XListView_setRowHidden(&lv, 0, false);
        XAPI_EXPECT(!XListView_isRowHidden(&lv, 0),
                    "setRowHidden(0,false) 恢复可见");

        /* ---- indexAt 命中与 indexesMoved 句柄 ---- */
        XObject_connect_1((XObject*)&lv,
                          XSignal(XListView_indexesMoved_signal),
                          (XObject*)&lv, vw_indexesMovedSlot,
                          XConnectionType_Direct);
        {
            int hitRow = -1;
            int hitCol = -1;
            XAPI_EXPECT(XAbstractItemView_indexAt_base(&lv.m_base, 10, 10,
                                                       &hitRow, &hitCol) &&
                        hitRow == 0,
                        "indexAt(10,10) 命中行 0（行高 30 槽位）");
            XAPI_EXPECT(XAbstractItemView_indexAt_base(&lv.m_base, 10, 70,
                                                       &hitRow, &hitCol) &&
                        hitRow == 2,
                        "indexAt(10,70) 命中行 2");
        }
        vsig_reset();
        {
            int moved[2] = { 0, 1 };
            XListView_indexesMoved_signal(&lv, moved, 2);
            XAPI_EXPECT(g_sig.indexesMoved == 1 && g_sig.imCount == 2,
                        "indexesMoved 句柄可手工触发（预留发射点）");
        }

        if (model) XAbstractItemModel_delete_base(model);
        XListView_deinit_base(&lv);
    }

    /* ================================================================
     * 4. XListWidget：条目增删改查/当前项/查找/排序/部件挂载/信号。
     * ================================================================ */
    {
        XListWidget lw;
        XAbstractItemModel* bridge;
        const char* texts[2] = { "樱桃", "榴莲" };
        int hits[4];
        int hitCount;
        XString* taken;

        vsig_reset();
        XListWidget_init(&lw, NULL, 0);
        XWidget_setGeometry((XWidget*)&lw, 0, 0, 200, 96);
        vsig_connectList((XObject*)&lw);

        XAPI_EXPECT(XListWidget_count(&lw) == 0,
                    "初始条目数 0（对标新建 QListWidget）");
        XAPI_EXPECT(XListWidget_model(&lw) != NULL,
                    "内建条目模型可用（对标 QListWidget::model）");
        XAPI_EXPECT(XListWidget_currentItem(&lw) == -1 &&
                    XListWidget_currentRow(&lw) == -1,
                    "初始无当前条目");
        XAPI_EXPECT(!XListWidget_isSortingEnabled(&lw),
                    "默认排序关闭（对标 sortingEnabled 缺省）");

        /* ---- 追加/批量追加（对标 addItem/addItems） ---- */
        XAPI_EXPECT(XListWidget_addItem_2(&lw, "苹果") == 0,
                    "addItem 返回新条目行号 0");
        XAPI_EXPECT(XListWidget_addItem_2(&lw, "香蕉") == 1 &&
                    XListWidget_count(&lw) == 2,
                    "追加后行号自增、count 同步");
        XAPI_EXPECT(XListWidget_addItems(&lw, texts, 2) == 2 &&
                    XListWidget_count(&lw) == 4 &&
                    strcmp(xapi_cstr(XListWidget_item_2(&lw, 2)), "樱桃") == 0,
                    "addItems 批量追加 2 条且文本就位");
        XAPI_EXPECT(XListWidget_addItems(&lw, texts, 0) == 0,
                    "addItems count<=0 返回 0");
        /* ---- 插入（对标 insertItem 真实行内插入） ---- */
        XAPI_EXPECT(XListWidget_insertItem_2(&lw, 1, "插入") == 1 &&
                    strcmp(xapi_cstr(XListWidget_item_2(&lw, 1)), "插入") == 0 &&
                    strcmp(xapi_cstr(XListWidget_item_2(&lw, 2)), "香蕉") == 0,
                    "insertItem 行内插入、原行整体后移");
        XAPI_EXPECT(XListWidget_insertItem_2(&lw, -5, "置顶") == 0 &&
                    strcmp(xapi_cstr(XListWidget_item_2(&lw, 0)), "置顶") == 0,
                    "insertItem 负行收敛最前（Qt 负数最前语义）");
        {
            int before = XListWidget_count(&lw);
            XAPI_EXPECT(XListWidget_insertItem_2(&lw, 999, "末尾") ==
                            before &&
                        XListWidget_count(&lw) == before + 1,
                        "insertItem 超出追加（同 insertItem 收敛口径）");
        }
        /* ---- 读取（对标 item/item_new 的借用与副本形态） ---- */
        XAPI_EXPECT(XListWidget_item(&lw, 99) == NULL,
                    "越界 item 返回 NULL");
        XAPI_EXPECT(xapi_cstr(XListWidget_item_2(&lw, 99))[0] == '\0',
                    "越界 item_2 返回空串");
        {
            XString* copy = XListWidget_item_new(&lw, 0);
            XAPI_EXPECT(copy != NULL &&
                        strcmp(xapi_u8(copy), "置顶") == 0,
                        "item_new 返回独立文本副本");
            if (copy) XString_delete_base((XClass*)copy);
        }
        /* ---- 当前项与信号（对标 setCurrentItem/setCurrentRow 联动） ---- */
        vsig_reset();
        XListWidget_setCurrentRow(&lw, 2);
        XAPI_EXPECT(XListWidget_currentRow(&lw) == 2 &&
                    XListWidget_currentItem(&lw) == 2,
                    "setCurrentRow 后当前行/条目=2");
        XAPI_EXPECT(g_sig.lwCurrentChanged == 1 && g_sig.lwCur == 2 &&
                    g_sig.lwPrev == -1,
                    "当前行变化发射 currentItemChanged(2,-1)");
        XAPI_EXPECT(g_sig.lwRowChanged == 1 &&
                    strcmp(g_sig.lwText, "插入") == 0,
                    "成对发射 currentRowChanged/currentTextChanged");
        XAPI_EXPECT(g_sig.lwSelChanged == 1,
                    "选择联动发射 itemSelectionChanged");
        {
            int sel[4];
            int selCount = XListWidget_selectedItems(&lw, sel, 4);
            XAPI_EXPECT(selCount == 1 && sel[0] == 2,
                        "selectedItems 返回选中行 {2}");
        }
        XListWidget_setCurrentRow(&lw, -1);
        XAPI_EXPECT(XListWidget_currentRow(&lw) == -1 &&
                    XListWidget_selectedItems(&lw, hits, 4) == 0,
                    "setCurrentRow(-1) 清除当前项与选中");
        XListWidget_setCurrentRow(&lw, 99);
        XAPI_EXPECT(XListWidget_currentRow(&lw) == -1,
                    "setCurrentRow 越界忽略");
        XListWidget_setCurrentItem(&lw, 1);
        XListWidget_scrollToItem(&lw, 1);
        XAPI_EXPECT(XListWidget_currentItem(&lw) == 1,
                    "setCurrentItem/scrollToItem 后当前条目=1");

        /* 此时行序：0=置顶 1=苹果 2=插入 3=香蕉 4=樱桃 5=榴莲 6=末尾。 */

        /* ---- 查找（对标 findItems 精确/包含，区分大小写） ---- */
        hitCount = XListWidget_findItems(&lw, "香蕉", 1, hits, 4);
        XAPI_EXPECT(hitCount == 1 && hits[0] == 3,
                    "findItems 精确命中香蕉（行 3）");
        hitCount = XListWidget_findItems(&lw, "果", 0, hits, 4);
        XAPI_EXPECT(hitCount == 1 && hits[0] == 1,
                    "findItems 包含模式仅命中苹果（行 1）");
        hitCount = XListWidget_findItems(&lw, "苹果", 1, hits, 4);
        XAPI_EXPECT(hitCount == 1,
                    "findItems 中文精确匹配");
        hitCount = XListWidget_findItems(&lw, NULL, 0, hits, 4);
        XAPI_EXPECT(hitCount == 0,
                    "findItems(NULL) 返回 0");

        /* ---- takeItem：移除行并归还文本所有权（对标 takeItem） ---- */
        vsig_reset();
        taken = XListWidget_takeItem(&lw, 1);
        XAPI_EXPECT(taken != NULL &&
                    strcmp(xapi_u8(taken), "苹果") == 0,
                    "takeItem 归还被取条目文本");
        XAPI_EXPECT(XListWidget_count(&lw) == 6 &&
                    strcmp(xapi_cstr(XListWidget_item_2(&lw, 1)), "插入") == 0,
                    "取出行 1 后其后条目整体前移");
        XAPI_EXPECT(XListWidget_currentItem(&lw) == -1 &&
                    g_sig.lwCurrentChanged >= 1 && g_sig.lwPrev == 1,
                    "当前项被移除失效并发射 currentItemChanged");
        if (taken) XString_delete_base((XClass*)taken);
        XAPI_EXPECT(XListWidget_takeItem(&lw, 99) == NULL,
                    "takeItem 越界返回 NULL");

        /* ---- 排序（对标 sortItems 升/降序、NULL 视为空串最小） ---- */
        XListWidget_clear(&lw);
        XListWidget_addItem_2(&lw, "b");
        XListWidget_addItem_2(&lw, "a");
        XListWidget_addItem_2(&lw, "c");
        XListWidget_sortItems(&lw, 0);
        XAPI_EXPECT(strcmp(xapi_cstr(XListWidget_item_2(&lw, 0)), "a") == 0 &&
                    strcmp(xapi_cstr(XListWidget_item_2(&lw, 2)), "c") == 0,
                    "sortItems(0) 升序重排");
        XListWidget_sortItems(&lw, 1);
        XAPI_EXPECT(strcmp(xapi_cstr(XListWidget_item_2(&lw, 0)), "c") == 0 &&
                    strcmp(xapi_cstr(XListWidget_item_2(&lw, 2)), "a") == 0,
                    "sortItems(1) 降序重排");
        /* ---- 排序使能（对标 setSortingEnabled 开启即排序+自动排序） ---- */
        XListWidget_sortItems(&lw, 0);
        XListWidget_setSortingEnabled(&lw, true);
        XAPI_EXPECT(XListWidget_isSortingEnabled(&lw),
                    "setSortingEnabled(true) 使能生效");
        XListWidget_addItem_2(&lw, "a0");
        XAPI_EXPECT(strcmp(xapi_cstr(XListWidget_item_2(&lw, 0)), "a") == 0 &&
                    strcmp(xapi_cstr(XListWidget_item_2(&lw, 1)), "a0") == 0 &&
                    strcmp(xapi_cstr(XListWidget_item_2(&lw, 3)), "c") == 0,
                    "排序使能下插入自动按当前序（升序）落位");
        XListWidget_setSortingEnabled(&lw, false);
        XAPI_EXPECT(!XListWidget_isSortingEnabled(&lw),
                    "setSortingEnabled(false) 关闭自动排序");

        /* ---- 几何（itemAt/visualItemRect 与绘制同一行几何） ---- */
        XListWidget_clear(&lw);
        XListWidget_addItem_2(&lw, "r0");
        XListWidget_addItem_2(&lw, "r1");
        XListWidget_addItem_2(&lw, "r2");
        XAPI_EXPECT(XListWidget_itemAt(&lw, 10, 12) == 0 &&
                    XListWidget_itemAt(&lw, 10, 30) == 1,
                    "itemAt 按行高 24 命中行 0/行 1");
        XAPI_EXPECT(XListWidget_itemAt(&lw, 10, -1) == -1,
                    "itemAt 负 y 未命中返回 -1（单列命中只看 y 向槽位）");
        {
            XRect ir = XListWidget_visualItemRect(&lw, 1);
            XAPI_EXPECT(ir.y == 24 && ir.height == 24 && ir.width > 0,
                        "visualItemRect(1) 行带 y=24、高=24（表头不占行带）");
        }
        {
            XRect ir = XListWidget_visualItemRect(&lw, 99);
            XAPI_EXPECT(ir.x == 0 && ir.y == 0 && ir.width == 0 &&
                        ir.height == 0,
                        "visualItemRect 越界返回全零矩形");
        }
        /* ---- 行级部件挂载（对标 setItemWidget/itemWidget，借用） ---- */
        {
            XWidget* rowWidget = XWidget_create(NULL, 0);
            XListWidget_setItemWidget(&lw, 0, rowWidget);
            XAPI_EXPECT(XListWidget_itemWidget(&lw, 0) == rowWidget,
                        "setItemWidget/itemWidget 借用往返");
            XListWidget_removeItemWidget(&lw, 0);
            XAPI_EXPECT(XListWidget_itemWidget(&lw, 0) == NULL,
                        "removeItemWidget 仅解除关联（置 NULL）");
            XAPI_EXPECT(XListWidget_itemWidget(&lw, 9) == NULL,
                        "itemWidget 越界返回 NULL");
            if (rowWidget) XWidget_delete_base(rowWidget);
        }
        /* ---- 索引反查（对标 indexFromItem/itemFromIndex/row） ---- */
        XAPI_EXPECT(XListWidget_indexFromItem(&lw, 0) == 0 &&
                    XListWidget_indexFromItem(&lw, -1) == -1,
                    "indexFromItem 恒等映射（越界 -1）");
        XAPI_EXPECT(XListWidget_itemFromIndex(&lw, 1) ==
                    XListWidget_item(&lw, 1),
                    "itemFromIndex 与 item 同一承载（对称 API）");
        XAPI_EXPECT(XListWidget_row(&lw, "r1") == 1 &&
                    XListWidget_row(&lw, "R1") == -1 &&
                    XListWidget_row(&lw, NULL) == -1,
                    "row 文本精确反查（区分大小写、NULL -1）");
        XListWidget_editItem(&lw, 0);
        XAPI_EXPECT(XListWidget_count(&lw) == 3,
                    "editItem 句柄预留为无操作（不改动数据）");

        /* ---- 内建模型桥（itemChanged 经 dataChanged 桥接） ---- */
        bridge = XListWidget_model(&lw);
        vsig_reset();
        XAbstractItemModel_setData_2(bridge, 0, 0, "改样");
        XAPI_EXPECT(g_sig.lwItemChanged == 1 && g_sig.lwICRow == 0,
                    "外部改模型 dataChanged 桥接 itemChanged(0)");
        XAPI_EXPECT(strcmp(xapi_cstr(XListWidget_item_2(&lw, 0)), "改样") == 0,
                    "模型写回反映到条目文本（同一数据通路）");

        /* ---- 清空（对标 clear；当前项失效信号） ---- */
        vsig_reset();
        XListWidget_setCurrentRow(&lw, 1);
        vsig_reset();
        XListWidget_clear(&lw);
        XAPI_EXPECT(XListWidget_count(&lw) == 0 &&
                    XListWidget_currentRow(&lw) == -1,
                    "clear 后条目数 0、当前项失效");
        XAPI_EXPECT(g_sig.lwCurrentChanged >= 1 && g_sig.lwSelChanged >= 1,
                    "clear 发射当前项/选择变化信号");

        XListWidget_deinit_base(&lw);
    }

    /* ================================================================
     * 5. XTreeView：树视图（缩进/表头隐藏/展开族/列状态/排序承载）。
     * ================================================================ */
    {
        XTreeView tv;
        XAbstractItemModel* model = XAbstractItemModel_create();
        XHeaderView* header = XHeaderView_create(NULL, 0, 0);
        XRect rect;

        vsig_reset();
        XTreeView_init(&tv, NULL, 0);
        XWidget_setGeometry((XWidget*)&tv, 0, 0, 200, 120);
        XObject_connect_1((XObject*)&tv, XSignal(XTreeView_expanded_signal),
                          (XObject*)&tv, vw_tvExpandedSlot,
                          XConnectionType_Direct);
        XObject_connect_1((XObject*)&tv, XSignal(XTreeView_collapsed_signal),
                          (XObject*)&tv, vw_tvCollapsedSlot,
                          XConnectionType_Direct);
        if (model) {
            XAbstractItemModel_setDimension(model, 3, 2);
            XAbstractItemView_setModel(&tv.m_base, model);
        }

        /* ---- 默认值（Qt QTreeView 构造默认） ---- */
        XAPI_EXPECT(XTreeView_indentation(&tv) == 20,
                    "默认缩进 20（本库常量；Qt 为样式测算值）");
        XAPI_EXPECT(!XTreeView_isHeaderHidden(&tv),
                    "默认 headerHidden=false（Qt 缺省）");
        XAPI_EXPECT(XTreeView_rowHeight(&tv) == 24,
                    "默认行高 24（本库扩展：统一行高）");
        XAPI_EXPECT(XTreeView_expandsOnDoubleClick(&tv),
                    "默认 expandsOnDoubleClick=true（Qt 缺省）");
        XAPI_EXPECT(XTreeView_itemsExpandable(&tv),
                    "默认 itemsExpandable=true（Qt 缺省）");
        XAPI_EXPECT(XTreeView_rootIsDecorated(&tv),
                    "默认 rootIsDecorated=true（Qt 缺省）");
        XAPI_EXPECT(!XTreeView_isSortingEnabled(&tv),
                    "默认 sortingEnabled=false（Qt 缺省）");
        XAPI_EXPECT(!XTreeView_uniformRowHeights(&tv),
                    "默认 uniformRowHeights=false（Qt 缺省）");
        XAPI_EXPECT(XTreeView_sortColumn(&tv) == -1,
                    "默认排序列 -1（未设置）");
        XAPI_EXPECT(!XTreeView_isSelectionRectVisible(&tv),
                    "默认 selectionRectVisible=false（Qt 缺省）");
        XAPI_EXPECT(!XTreeView_allColumnsShowFocus(&tv),
                    "默认 allColumnsShowFocus=false（Qt 缺省）");
        XAPI_EXPECT(XTreeView_treePosition(&tv) == 0,
                    "默认 treePosition=0（Qt 缺省）");
        XAPI_EXPECT(!XTreeView_isAnimated(&tv),
                    "默认 animated=false（Qt 缺省）");
        XAPI_EXPECT(!XTreeView_wordWrap(&tv),
                    "默认 wordWrap=false（Qt 缺省）");
        XAPI_EXPECT(XTreeView_autoExpandDelay(&tv) == -1,
                    "默认 autoExpandDelay=-1（Qt 缺省禁用）");
        XAPI_EXPECT(XTreeView_header(&tv) == NULL,
                    "初始无挂接表头对象");

        /* ---- 属性往返 ---- */
        XTreeView_setIndentation(&tv, 32);
        XAPI_EXPECT(XTreeView_indentation(&tv) == 32,
                    "setIndentation 往返");
        XTreeView_resetIndentation(&tv);
        XAPI_EXPECT(XTreeView_indentation(&tv) == 20,
                    "resetIndentation 复位常量 20（本库缺省口径）");
        XTreeView_setRowHeight(&tv, 28);
        XAPI_EXPECT(XTreeView_rowHeight(&tv) == 28,
                    "setRowHeight 往返");
        XTreeView_setExpandsOnDoubleClick(&tv, false);
        XTreeView_setItemsExpandable(&tv, false);
        XTreeView_setRootIsDecorated(&tv, false);
        XTreeView_setUniformRowHeights(&tv, true);
        XAPI_EXPECT(!XTreeView_expandsOnDoubleClick(&tv) &&
                    !XTreeView_itemsExpandable(&tv) &&
                    !XTreeView_rootIsDecorated(&tv) &&
                    XTreeView_uniformRowHeights(&tv),
                    "展开族布尔属性 setter 往返");
        XTreeView_setExpandsOnDoubleClick(&tv, true);
        XTreeView_setItemsExpandable(&tv, true);
        XTreeView_setRootIsDecorated(&tv, true);
        XTreeView_setSortingEnabled(&tv, true);
        XTreeView_setUniformRowHeights(&tv, false);
        XAPI_EXPECT(XTreeView_isSortingEnabled(&tv) &&
                    !XTreeView_uniformRowHeights(&tv),
                    "sortingEnabled/uniformRowHeights 往返");
        XTreeView_setSortingEnabled(&tv, false);
        XTreeView_setSelectionRectVisible(&tv, true);
        XTreeView_setAllColumnsShowFocus(&tv, true);
        XTreeView_setAnimated(&tv, true);
        XTreeView_setWordWrap(&tv, true);
        XAPI_EXPECT(XTreeView_isSelectionRectVisible(&tv) &&
                    XTreeView_allColumnsShowFocus(&tv) &&
                    XTreeView_isAnimated(&tv) && XTreeView_wordWrap(&tv),
                    "视图状态族 setter 往返");
        XTreeView_setTreePosition(&tv, -1);
        XAPI_EXPECT(XTreeView_treePosition(&tv) == -1,
                    "setTreePosition(-1) 跟随视觉第 0 列（Qt 语义）");
        XTreeView_setTreePosition(&tv, 0);
        XTreeView_setAutoExpandDelay(&tv, 500);
        XAPI_EXPECT(XTreeView_autoExpandDelay(&tv) == 500,
                    "setAutoExpandDelay 往返");

        /* ---- 展开族（对标 expand/collapse/isExpanded 与信号） ---- */
        vsig_reset();
        XTreeView_expand(&tv, 0);
        XAPI_EXPECT(XTreeView_isExpanded(&tv, 0) &&
                    g_sig.tvExpanded == 1 && g_sig.tvExpRow == 0,
                    "expand(0) 置位并发射 expanded(0)");
        XTreeView_expand(&tv, 0);
        XAPI_EXPECT(g_sig.tvExpanded == 1,
                    "重复 expand 不重复发射（状态由折叠变展开才发射）");
        XTreeView_collapse(&tv, 0);
        XAPI_EXPECT(!XTreeView_isExpanded(&tv, 0) &&
                    g_sig.tvCollapsed == 1 && g_sig.tvColRow == 0,
                    "collapse(0) 复位并发射 collapsed(0)");
        XTreeView_collapse(&tv, 0);
        XAPI_EXPECT(g_sig.tvCollapsed == 1,
                    "重复 collapse 不重复发射");
        XTreeView_setExpanded(&tv, 1, true);
        XAPI_EXPECT(XTreeView_isExpanded(&tv, 1),
                    "setExpanded(1,true) 状态承载");
        XTreeView_collapse(&tv, 1);
        vsig_reset();
        XTreeView_expandAll(&tv);
        XAPI_EXPECT(XTreeView_isExpanded(&tv, 0) &&
                    XTreeView_isExpanded(&tv, 1) &&
                    XTreeView_isExpanded(&tv, 2) &&
                    g_sig.tvExpanded == 0,
                    "expandAll 全展开且不逐行发射 expanded（对标 Qt）");
        XTreeView_collapseAll(&tv);
        XAPI_EXPECT(!XTreeView_isExpanded(&tv, 0) &&
                    !XTreeView_isExpanded(&tv, 2) &&
                    g_sig.tvCollapsed == 0,
                    "collapseAll 全收起且不逐行发射 collapsed");
        XTreeView_expandToDepth(&tv, 0);
        XAPI_EXPECT(XTreeView_isExpanded(&tv, 1),
                    "expandToDepth 平铺简化为全展开");
        XTreeView_collapseAll(&tv);
        XTreeView_expandRecursively(&tv, 0);
        XAPI_EXPECT(XTreeView_isExpanded(&tv, 0),
                    "expandRecursively 平铺等效展开全部");
        XAPI_EXPECT(XTreeView_isExpanded(&tv, 9) == false,
                    "越界行 isExpanded 返回 false（默认折叠）");
        /* ---- indexAbove/indexBelow（对标平铺行序相邻） ---- */
        XAPI_EXPECT(XTreeView_indexAbove(&tv, 1) == 0 &&
                    XTreeView_indexAbove(&tv, 0) == -1,
                    "indexAbove 平铺 row-1（首行无上行 -1）");
        XAPI_EXPECT(XTreeView_indexBelow(&tv, 1) == 2 &&
                    XTreeView_indexBelow(&tv, 2) == -1,
                    "indexBelow 平铺 row+1（末行无下行 -1）");
        XAPI_EXPECT(XTreeView_indexAbove(&tv, 9) == -1,
                    "indexAbove 越界返回 -1");

        /* ---- 列状态族（对标 setColumnHidden/Width、hide/showColumn） ---- */
        XAPI_EXPECT(!XTreeView_isColumnHidden(&tv, 0) &&
                    XTreeView_columnWidth(&tv, 0) == 0,
                    "默认列可见、列宽 0（自动铺满）");
        XTreeView_setColumnWidth(&tv, 0, 100);
        XTreeView_setColumnWidth(&tv, 1, 80);
        XAPI_EXPECT(XTreeView_columnWidth(&tv, 0) == 100 &&
                    XTreeView_columnWidth(&tv, 1) == 80,
                    "setColumnWidth 往返");
        XTreeView_setColumnWidth(&tv, 0, -5);
        XAPI_EXPECT(XTreeView_columnWidth(&tv, 0) == 100,
                    "setColumnWidth 负值忽略");
        XTreeView_setColumnWidth(&tv, 0, 0);
        XAPI_EXPECT(XTreeView_columnWidth(&tv, 0) == 0,
                    "setColumnWidth(0) 恢复自动铺满");
        XTreeView_setColumnWidth(&tv, 0, 100);
        XTreeView_hideColumn(&tv, 0);
        XAPI_EXPECT(XTreeView_isColumnHidden(&tv, 0),
                    "hideColumn 转发隐藏（对标 hideColumn）");
        XTreeView_showColumn(&tv, 0);
        XAPI_EXPECT(!XTreeView_isColumnHidden(&tv, 0),
                    "showColumn 恢复显示");
        XTreeView_setColumnHidden(&tv, 1, true);
        XAPI_EXPECT(XTreeView_isColumnHidden(&tv, 1) &&
                    !XTreeView_isColumnHidden(&tv, 9),
                    "setColumnHidden 往返、越界读取 false");
        XTreeView_setColumnHidden(&tv, 1, false);

        /* ---- 几何（visualRect 含表头带 20px；列宽累计） ---- */
        rect = XTreeView_visualRect(&tv, 0, 0);
        XAPI_EXPECT(rect.x == 0 && rect.y == 20 && rect.width == 100 &&
                    rect.height == 28,
                    "visualRect(0,0)：y 扣表头 20、宽=显式列宽");
        rect = XTreeView_visualRect(&tv, 1, 1);
        XAPI_EXPECT(rect.x == 100 && rect.y == 20 + 28,
                    "visualRect(1,1)：x=可见列宽累计、y=表头+行高");
        XAPI_EXPECT(XTreeView_columnViewportPosition(&tv, 0) == 0 &&
                    XTreeView_columnViewportPosition(&tv, 1) == 100,
                    "columnViewportPosition 按可见列宽累计");
        XTreeView_hideColumn(&tv, 0);
        XAPI_EXPECT(XTreeView_columnViewportPosition(&tv, 1) == 0,
                    "隐藏列跳过：列 1 位置=0（宽度按 0 计）");
        XAPI_EXPECT(XTreeView_visualRect(&tv, 0, 0).width == 0,
                    "隐藏列 visualRect 为空矩形");
        XTreeView_showColumn(&tv, 0);
        XAPI_EXPECT(XTreeView_rowAt(&tv, 20 + 14) == 0 &&
                    XTreeView_rowAt(&tv, 20 + 28 + 14) == 1,
                    "rowAt 扣表头区后按行高反查（对标 rowAt）");
        XAPI_EXPECT(XTreeView_rowAt(&tv, 10) == -1,
                    "rowAt 表头区返回 -1");
        XAPI_EXPECT(XTreeView_columnAt(&tv, 50) == 0 &&
                    XTreeView_columnAt(&tv, 150) == 1,
                    "columnAt 按可见列宽反查（对标 columnAt）");
        XTreeView_resizeColumnToContents(&tv, 0);
        XAPI_EXPECT(XTreeView_columnWidth(&tv, 0) == 100,
                    "resizeColumnToContents 内容测算未接（列宽保持）");

        /* ---- 表头对象挂接（对标 setHeader/header + headerHidden 镜像） ---- */
        XTreeView_setHeaderHidden(&tv, true);
        XTreeView_setHeader(&tv, header);
        XAPI_EXPECT(XTreeView_header(&tv) == header,
                    "setHeader/header 借用挂接");
        XAPI_EXPECT(XTreeView_isHeaderHidden(&tv),
                    "挂接后视图侧 headerHidden 状态保持");
        XTreeView_setHeaderHidden(&tv, false);
        XAPI_EXPECT(!XWidget_isHidden((XWidget*)header),
                    "headerHidden(false) 镜像到表头对象可见性");
        XTreeView_setHeaderHidden(&tv, true);
        XAPI_EXPECT(XWidget_isHidden((XWidget*)header),
                    "headerHidden(true) 镜像到表头对象（对标 setHidden）");
        XTreeView_setHeader(&tv, NULL);
        XTreeView_setHeaderHidden(&tv, false);

        /* ---- 排序承载（对标 sortByColumn/sortColumn/指示方向） ---- */
        XTreeView_sortByColumn(&tv, 1, 1);
        XAPI_EXPECT(XTreeView_sortColumn(&tv) == 1 &&
                    XTreeView_sortIndicatorOrder(&tv) == 1,
                    "sortByColumn 记录排序列与方向（状态承载）");
        XTreeView_sortByColumn(&tv, -1, 0);
        XAPI_EXPECT(XTreeView_sortColumn(&tv) == -1,
                    "sortByColumn(-1) 恢复自然顺序并清除排序列");
        XTreeView_sortByColumn(&tv, 0, 0);
        XAPI_EXPECT(XTreeView_sortIndicatorOrder(&tv) == 0,
                    "默认排序方向升序（0）");

        /* ---- 行隐藏与跨列合并 ---- */
        XTreeView_setRowHidden(&tv, 0, true);
        XAPI_EXPECT(XTreeView_isRowHidden(&tv, 0) &&
                    !XTreeView_isRowHidden(&tv, 9),
                    "setRowHidden 往返、越界 false");
        XTreeView_setRowHidden(&tv, 0, false);
        XTreeView_setFirstColumnSpanned(&tv, 0, true);
        XAPI_EXPECT(!XTreeView_isFirstColumnSpanned(&tv, 0),
                    "平铺模型无跨列能力：isFirstColumnSpanned 恒 false");
        /* ---- dataChanged 槽（逆序区间忽略，仅覆盖调用） ---- */
        XTreeView_dataChanged(&tv, 1, 1, 0, 0);
        XTreeView_dataChanged(&tv, 0, 0, 0, 0);
        XAPI_EXPECT(true, "dataChanged 槽调用无崩溃（无效区间丢弃）");

        if (header) XHeaderView_delete_base(header);
        if (model) XAbstractItemModel_delete_base(model);
        XTreeView_deinit_base(&tv);
    }

    /* ================================================================
     * 6. XTreeWidget：条目树（顶层增删/不可见根/表头条目/展开/便捷族）。
     * ================================================================ */
    {
        XTreeWidget tree;
        XTreeWidgetItem* top0;
        XTreeWidgetItem* top1;
        XTreeWidgetItem* taken;
        XTreeWidgetItem* child;
        const char* batch[2] = { "Gamma", "Delta" };
        const char* inserts[1] = { "Epsilon" };
        int hits[8];
        int hitCount;
        XRect rect;

        vsig_reset();
        XTreeWidget_init(&tree, NULL, 0);
        XWidget_setGeometry((XWidget*)&tree, 0, 0, 160, 130);
        vsig_connectTree((XObject*)&tree);

        XAPI_EXPECT(XTreeWidget_topLevelItemCount(&tree) == 0,
                    "初始顶层条目数 0");
        XAPI_EXPECT(XTreeWidget_columnCount(&tree) == 1,
                    "默认列数 1（Qt 新建 QTreeWidget 默认）");
        XAPI_EXPECT(XTreeWidget_currentItem(&tree) == -1,
                    "初始无当前条目");
        XAPI_EXPECT(XTreeWidget_invisibleRootItem(&tree) != NULL,
                    "不可见根条目可用（对标 invisibleRootItem）");
        XAPI_EXPECT(XTreeWidget_headerItem(&tree) != NULL,
                    "表头条目懒创建可用（对标 headerItem）");
        XAPI_EXPECT(xapi_cstr(XTreeWidget_headerLabel(&tree, 0))[0] == '\0',
                    "未设置表头文本返回空串");
        XAPI_EXPECT(XTreeWidget_sortColumn(&tree) == -1,
                    "初始未排序（sortColumn=-1）");

        /* ---- 顶层增删（对标 addTopLevelItem/insertTopLevelItem） ---- */
        top0 = XTreeWidgetItem_create_2("Alpha", NULL);
        child = XTreeWidgetItem_create_2("beta", top0);
        XAPI_EXPECT(XTreeWidgetItem_addChild(top0, child),
                    "addChild 追加子条目");
        XAPI_EXPECT(XTreeWidgetItem_childCount(top0) == 1 &&
                    XTreeWidgetItem_child(top0, 0) == child &&
                    XTreeWidgetItem_child(top0, 9) == NULL,
                    "childCount/child 读取（越界 NULL）");
        XAPI_EXPECT(strcmp(xapi_cstr(XTreeWidgetItem_text_2(child)), "beta") == 0,
                    "条目文本 UTF-8 读取（对标 QTreeWidgetItem::text）");
        XTreeWidgetItem_setText_2(child, "beta2");
        XAPI_EXPECT(strcmp(xapi_cstr(XTreeWidgetItem_text_2(child)), "beta2") == 0,
                    "setText 后文本更新");
        XTreeWidgetItem_setText(child, NULL);
        XAPI_EXPECT(xapi_cstr(XTreeWidgetItem_text_2(child))[0] == '\0',
                    "setText(NULL) 清空文本");
        XTreeWidgetItem_setText_2(child, "beta2");
        XAPI_EXPECT(XTreeWidget_addTopLevelItem(&tree, top0),
                    "addTopLevelItem 挂接顶层条目（所有权转移）");
        XAPI_EXPECT(XTreeWidget_topLevelItemCount(&tree) == 1 &&
                    XTreeWidget_topLevelItem(&tree, 0) == top0,
                    "topLevelItemCount/topLevelItem 读取");
        XAPI_EXPECT(!XTreeWidget_addTopLevelItem(&tree, NULL),
                    "addTopLevelItem(NULL) 拒绝");
        top1 = XTreeWidgetItem_create_2("Zeta", NULL);
        XTreeWidget_addTopLevelItem(&tree, top1);
        XAPI_EXPECT(XTreeWidget_insertTopLevelItem(&tree, 0,
                                                   XTreeWidgetItem_create_2(
                                                       "Mu", NULL)),
                    "insertTopLevelItem(0) 插入成功");
        XAPI_EXPECT(strcmp(XTreeWidgetItem_text_2(
                               XTreeWidget_topLevelItem(&tree, 0)), "Mu") == 0,
                    "插入 0 位后原条目后移");
        XAPI_EXPECT(XTreeWidget_addTopLevelItems(&tree, batch, 2) == 2,
                    "addTopLevelItems 批量追加 2 行");
        XAPI_EXPECT(XTreeWidget_insertTopLevelItems(&tree, 1, inserts, 1) == 1,
                    "insertTopLevelItems 行内插入");
        XAPI_EXPECT(XTreeWidget_topLevelItemCount(&tree) == 6,
                    "批量增删后顶层行数 6");
        XAPI_EXPECT(XTreeWidget_insertTopLevelItems(&tree, 99, inserts, 1) == 0,
                    "insertTopLevelItems 越界拒绝（返回 0）");

        /* ---- 索引反查（对标 itemFromIndex/indexFromItem 恒等映射） ---- */
        XAPI_EXPECT(XTreeWidget_itemFromIndex(&tree, 0) ==
                    XTreeWidget_topLevelItem(&tree, 0),
                    "itemFromIndex 恒等于 topLevelItem");
        XAPI_EXPECT(XTreeWidget_indexFromItem(&tree, top0) == 2 &&
                    XTreeWidget_indexFromItem(&tree, child) == -1,
                    "indexFromItem 仅顶层参与（子条目 -1）");
        /* ---- 不可见根（对标 invisibleRootItem 承载顶层） ---- */
        XAPI_EXPECT(XTreeWidgetItem_childCount(
                        XTreeWidget_invisibleRootItem(&tree)) ==
                    XTreeWidget_topLevelItemCount(&tree),
                    "不可见根 child 数=顶层条目数");
        XAPI_EXPECT(XTreeWidgetItem_child(
                        XTreeWidget_invisibleRootItem(&tree), 0) ==
                    XTreeWidget_topLevelItem(&tree, 0),
                    "不可见根 child(i)=topLevelItem(i)");
        /* ---- 当前条目与信号（对标 setCurrentItem） ---- */
        vsig_reset();
        XTreeWidget_setCurrentItem(&tree, 2);
        XAPI_EXPECT(XTreeWidget_currentItem(&tree) == 2,
                    "setCurrentItem(2) 当前行=2（固定第 0 列）");
        XAPI_EXPECT(g_sig.twCurChanged >= 1 && g_sig.twSelChanged >= 1,
                    "当前项变化发射 currentItemChanged/itemSelectionChanged");
        {
            int sel[4];
            XAPI_EXPECT(XTreeWidget_selectedItems(&tree, sel, 4) == 1 &&
                        sel[0] == 2,
                        "selectedItems 返回选中行 {2}");
        }
        XTreeWidget_setCurrentItem(&tree, 99);
        XAPI_EXPECT(XTreeWidget_currentItem(&tree) == 2,
                    "setCurrentItem 越界忽略");
        XTreeWidget_scrollToItem(&tree, 0);
        XAPI_EXPECT(true, "scrollToItem 调用无崩溃（EnsureVisible 语义）");
        /* ---- 点击/双击注入（与真实输入同路径） ---- */
        /* P1 批次起 XTreeWidget 绘制 20px 表头带：行带整体下移，
         * 注入/反查坐标相应 +20（行 0 带中心 = 20+12）。 */
        vw_injectPress((XWidget*)&tree, 40, 32);
        XAPI_EXPECT(g_sig.twItemClicked == 1 && g_sig.twItemClickedRow == 0,
                    "点击行带发射 itemClicked(0)");
        XAPI_EXPECT(XTreeWidget_currentItem(&tree) == 0,
                    "点击命中行写入当前项");
        vsig_reset();
        vw_injectDblClick((XWidget*)&tree, 40, 32);
        XAPI_EXPECT(g_sig.twItemDoubleClicked == 1 &&
                    g_sig.twItemActivated == 1,
                    "双击发射 itemDoubleClicked + itemActivated");
        /* ---- itemEntered：悬停进入新行差分发射（§8.0g16 四期③） ---- */
        {
            XRect er = XTreeWidget_visualItemRect(&tree, 1);
            /* visualItemRect 为内容坐标：注入用控件局部坐标须加回
               20px 表头带（同点击注入 +20 口径）。 */
            vsig_reset();
            vw_injectMove((XWidget*)&tree, 40, er.y + 20 + 12);
            XAPI_EXPECT(g_sig.twItemEntered == 1 && g_sig.twEnteredRow == 1,
                        "悬停进入新行发射 itemEntered(1)");
            vsig_reset();
            vw_injectMove((XWidget*)&tree, 40, er.y + 20 + 12);
            XAPI_EXPECT(g_sig.twItemEntered == 0,
                        "同行悬停移动差分判重不重发");
        }
        /* ---- 展开便捷族（对接 m_topExpanded 承载；新行默认展开） ---- */
        /* 行序：0=Mu 1=Epsilon 2=Alpha(子 beta2) 3=Zeta 4=Gamma 5=Delta。 */
        vsig_reset();
        XTreeWidget_collapseItem(&tree, 2);
        XAPI_EXPECT(g_sig.twItemCollapsed == 1,
                    "collapseItem(2) 由展开变折叠发射 itemCollapsed");
        rect = XTreeWidget_visualItemRect(&tree, 3);
        XAPI_EXPECT(rect.y == 72,
                    "折叠行子树不占位：行 3 y=72（前 3 顶层各 1 行）");
        XTreeWidget_expandItem(&tree, 2);
        XAPI_EXPECT(g_sig.twItemExpanded == 1,
                    "expandItem(2) 由折叠变展开发射 itemExpanded");
        rect = XTreeWidget_visualItemRect(&tree, 3);
        XAPI_EXPECT(rect.y == 96,
                    "展开行子树占位：行 3 y=96（Alpha 子树 2 行）");
        {
            int before = g_sig.twItemCollapsed;
            XTreeWidget_collapseItem(&tree, 3);
            XAPI_EXPECT(g_sig.twItemCollapsed == before,
                        "无子条目行 collapse 为无操作（不发射）");
        }
        /* ---- 几何与位置反查（对标 itemAt/visualItemRect） ---- */
        rect = XTreeWidget_visualItemRect(&tree, 0);
        XAPI_EXPECT(rect.y == 0 && rect.height == 24 && rect.width == 160,
                    "visualItemRect(0) 行带 y=0、宽=控件宽");
        XAPI_EXPECT(XTreeWidget_itemAt(&tree, 40, 32) == 0 &&
                    XTreeWidget_itemAt(&tree, 40, 56) == 1,
                    "itemAt 按展开态行带命中行 0/1（含 20px 表头带偏移）");
        XAPI_EXPECT(XTreeWidget_itemAt(&tree, -1, 0) == -1 &&
                    XTreeWidget_itemAt(&tree, 40, 9999) == -1,
                    "itemAt 越界返回 -1");
        XAPI_EXPECT(XTreeWidget_itemAbove(&tree, 1) == 0 &&
                    XTreeWidget_itemAbove(&tree, 0) == -1 &&
                    XTreeWidget_itemBelow(&tree, 0) == 1 &&
                    XTreeWidget_itemBelow(&tree, 5) == -1,
                    "itemAbove/Below 平铺 row±1（边界 -1）");
        /* ---- 查找（前序遍历全树；行号为全树前序序号） ---- */
        /* 前序行号：Mu=0 Epsilon=1 Alpha=2 beta2=3 Zeta=4 Gamma=5 Delta=6。 */
        hitCount = XTreeWidget_findItems(&tree, "a", 0, hits, 8);
        XAPI_EXPECT(hitCount == 5,
                    "findItems 包含模式前序遍历全树（Alpha/beta2/Zeta/Gamma/Delta 含 a）");
        hitCount = XTreeWidget_findItems(&tree, "Mu", 1, hits, 8);
        XAPI_EXPECT(hitCount == 1 && hits[0] == 0,
                    "findItems 精确命中 Mu（前序行 0）");
        hitCount = XTreeWidget_findItems(&tree, "mu", 1, hits, 8);
        XAPI_EXPECT(hitCount == 0,
                    "findItems 区分大小写");
        /* ---- 排序（对标 sortItems；结果写入 sortColumn） ---- */
        XTreeWidget_sortItems(&tree, 0, 0);
        XAPI_EXPECT(XTreeWidget_sortColumn(&tree) == 0 &&
                    strcmp(XTreeWidgetItem_text_2(
                               XTreeWidget_topLevelItem(&tree, 0)),
                           "Alpha") == 0,
                    "sortItems(0,0) 升序并记录排序列");
        XTreeWidget_sortItems(&tree, 0, 1);
        XAPI_EXPECT(strcmp(XTreeWidgetItem_text_2(
                               XTreeWidget_topLevelItem(&tree, 0)),
                           "Zeta") == 0,
                    "sortItems(0,1) 降序重排");
        /* ---- 表头文本（对标 setHeaderLabels/setHeaderLabel/headerItem） ---- */
        {
            const char* labels[3] = { "列一", "列二", "列三" };
            XTreeWidget_setHeaderLabels(&tree, labels, 3);
            XAPI_EXPECT(strcmp(xapi_cstr(XTreeWidget_headerLabel(&tree, 0)), "列一") == 0 &&
                        strcmp(xapi_cstr(XTreeWidget_headerLabel(&tree, 2)), "列三") == 0,
                        "setHeaderLabels 批量写入与读取");
        }
        XAPI_EXPECT(xapi_cstr(XTreeWidget_headerLabel(&tree, 5))[0] == '\0',
                    "未设置列表头返回空串（越界安全）");
        XTreeWidget_setHeaderLabel(&tree, "唯一");
        XAPI_EXPECT(strcmp(xapi_cstr(XTreeWidget_headerLabel(&tree, 0)), "唯一") == 0,
                    "setHeaderLabel 单标签覆写列 0");
        {
            XTreeWidgetItem* hi = XTreeWidget_headerItem(&tree);
            XAPI_EXPECT(strcmp(XTreeWidgetItem_text_2(
                                   XTreeWidgetItem_child(hi, 1)), "列二") == 0,
                        "表头条目子节点文本镜像标签表");
        }
        {
            /* 实现口径（XTreeWidget.h 契约）：表头条目列文本由子条目承载
               （child(i)↔列 i，Qt 表头即条目的 C 模型映射），条目自身文本
               不参与列标签；setHeaderItem 接管所有权并回填标签承载。 */
            XTreeWidgetItem* newHeader = XTreeWidgetItem_create_2(NULL, NULL);
            XTreeWidgetItem_addChild(newHeader,
                                     XTreeWidgetItem_create_2("H0", NULL));
            XTreeWidgetItem_addChild(newHeader,
                                     XTreeWidgetItem_create_2("H1", NULL));
            XTreeWidget_setHeaderItem(&tree, newHeader);
            XAPI_EXPECT(strcmp(xapi_cstr(XTreeWidget_headerLabel(&tree, 0)), "H0") == 0 &&
                        strcmp(xapi_cstr(XTreeWidget_headerLabel(&tree, 1)), "H1") == 0,
                        "setHeaderItem 接管并回填标签承载");
        }
        /* ---- 部件挂载与列数（对标 setItemWidget/setColumnCount） ---- */
        {
            XWidget* cellWidget = XWidget_create(NULL, 0);
            XTreeWidget_setItemWidget(&tree, 0, 0, cellWidget);
            XAPI_EXPECT(XTreeWidget_itemWidget(&tree, 0, 0) == cellWidget,
                        "setItemWidget/itemWidget 借用往返");
            XTreeWidget_removeItemWidget(&tree, 0, 0);
            XAPI_EXPECT(XTreeWidget_itemWidget(&tree, 0, 0) == NULL,
                        "removeItemWidget 置 NULL");
            if (cellWidget) XWidget_delete_base(cellWidget);
        }
        XTreeWidget_setColumnCount(&tree, 3);
        XAPI_EXPECT(XTreeWidget_columnCount(&tree) == 3,
                    "setColumnCount(3) 列数承载");
        XTreeWidget_setColumnCount(&tree, 0);
        XAPI_EXPECT(XTreeWidget_columnCount(&tree) == 3,
                    "setColumnCount(<1) 忽略");
        XTreeWidget_editItem(&tree, 0, 0);
        XAPI_EXPECT(true, "editItem 句柄预留为无操作（编辑路径未建）");
        /* ---- 条目文本变化信号（owner 定位行号） ---- */
        vsig_reset();
        XTreeWidgetItem_setText_2(XTreeWidget_topLevelItem(&tree, 0), "改名");
        XAPI_EXPECT(g_sig.twItemChanged == 1 && g_sig.twICRow == 0,
                    "顶层条目 setText 发射 itemChanged(0)");
        /* ---- takeTopLevelItem（所有权归还） ---- */
        taken = XTreeWidget_takeTopLevelItem(&tree, 0);
        XAPI_EXPECT(taken != NULL &&
                    strcmp(xapi_cstr(XTreeWidgetItem_text_2(taken)), "改名") == 0,
                    "takeTopLevelItem 归还条目所有权");
        XAPI_EXPECT(XTreeWidget_takeTopLevelItem(&tree, 99) == NULL,
                    "takeTopLevelItem 越界返回 NULL");
        XAPI_EXPECT(XTreeWidget_indexOfTopLevelItem(&tree, "Alpha") >= 0 &&
                    XTreeWidget_indexOfTopLevelItem(&tree, "无此") == -1,
                    "indexOfTopLevelItem 文本反查（仅顶层）");
        if (taken) XTreeWidgetItem_delete(taken);
        /* ---- clear（对标 clear；表头文本保留——Qt 同语义） ---- */
        vsig_reset();
        XTreeWidget_setCurrentItem(&tree, 0);
        vsig_reset();
        XTreeWidget_clear(&tree);
        XAPI_EXPECT(XTreeWidget_topLevelItemCount(&tree) == 0 &&
                    XTreeWidget_currentItem(&tree) == -1,
                    "clear 清空条目并复位当前项");
        XAPI_EXPECT(strcmp(xapi_cstr(XTreeWidget_headerLabel(&tree, 0)), "H0") == 0,
                    "clear 不清除表头文本（Qt clear 同语义）");
        XAPI_EXPECT(g_sig.twCurChanged >= 1 && g_sig.twSelChanged >= 1,
                    "clear 发射当前项失效/选择清空信号");

        XTreeWidget_deinit_base(&tree);
    }

    /* ================================================================
     * 7. XTableView：表格视图（行列尺寸/网格/隐藏/合并/表头挂接）。
     * ================================================================ */
    {
        XTableView table;
        XAbstractItemModel* model = XAbstractItemModel_create();
        XHeaderView* hHeader = XHeaderView_create(NULL, 0, 0);
        XHeaderView* vHeader = XHeaderView_create(NULL, 0, 1);
        XRect rect;

        XTableView_init(&table, NULL, 0);
        XWidget_setGeometry((XWidget*)&table, 0, 0, 300, 240);
        if (model) {
            XAbstractItemModel_setDimension(model, 3, 2);
            XAbstractItemView_setModel(&table.m_base, model);
        }

        /* ---- 默认值 ---- */
        XAPI_EXPECT(XTableView_rowHeight(&table) == 24,
                    "默认行高 24");
        XAPI_EXPECT(XTableView_columnWidth(&table, 0) == 80,
                    "默认列宽 80（未显式设宽）");
        XAPI_EXPECT(XTableView_showGrid(&table) &&
                    XTableView_gridStyle(&table) == XTABLEVIEW_GRID_SOLID,
                    "默认显示网格且实线（Qt showGrid=true/SolidLine）");
        XAPI_EXPECT(!XTableView_wordWrap(&table),
                    "默认 wordWrap=false（本库缺省；Qt QTableView 缺省 true，注释差异）");
        XAPI_EXPECT(XTableView_isCornerButtonEnabled(&table),
                    "默认左上角按钮启用（Qt cornerButtonEnabled 缺省 true）");
        XAPI_EXPECT(!XTableView_isSortingEnabled(&table),
                    "默认排序关闭（Qt 缺省）");
        XAPI_EXPECT(!XTableView_isRowHidden(&table, 0) &&
                    !XTableView_isColumnHidden(&table, 0),
                    "默认行/列均可见");
        XAPI_EXPECT(XTableView_rowSpan(&table, 0, 0) == 1 &&
                    XTableView_columnSpan(&table, 0, 0) == 1,
                    "默认无合并（跨行/跨列=1）");
        XAPI_EXPECT(XTableView_horizontalHeader(&table) == NULL &&
                    XTableView_verticalHeader(&table) == NULL,
                    "初始无挂接表头对象");

        /* ---- 尺寸往返 ---- */
        XTableView_setRowHeight(&table, 30);
        XTableView_setColumnWidth(&table, 0, 120);
        XTableView_setColumnWidth(&table, 1, 80);
        XAPI_EXPECT(XTableView_rowHeight(&table) == 30 &&
                    XTableView_columnWidth(&table, 0) == 120,
                    "setRowHeight/setColumnWidth 往返");
        XAPI_EXPECT(XTableView_columnWidth(&table, 5) == 80,
                    "越界列宽返回默认宽 80");

        /* ---- 网格开关与风格联动 ---- */
        XTableView_setShowGrid(&table, false);
        XAPI_EXPECT(!XTableView_showGrid(&table) &&
                    XTableView_gridStyle(&table) == XTABLEVIEW_GRID_NO,
                    "showGrid(false) 网格风格置 NoGrid");
        XTableView_setGridStyle(&table, XTABLEVIEW_GRID_DASH);
        XAPI_EXPECT(XTableView_showGrid(&table) &&
                    XTableView_gridStyle(&table) == XTABLEVIEW_GRID_DASH,
                    "setGridStyle(DASH) 恢复显示且风格虚线");
        XTableView_setGridStyle(&table, XTABLEVIEW_GRID_SOLID);
        XAPI_EXPECT(XTableView_gridStyle(&table) == XTABLEVIEW_GRID_SOLID,
                    "setGridStyle(SOLID) 往返");
        XTableView_setWordWrap(&table, true);
        XTableView_setCornerButtonEnabled(&table, false);
        XAPI_EXPECT(XTableView_wordWrap(&table) &&
                    !XTableView_isCornerButtonEnabled(&table),
                    "wordWrap/cornerButton setter 往返");
        XTableView_setWordWrap(&table, false);
        XTableView_setCornerButtonEnabled(&table, true);

        /* ---- 行/列隐藏（对标 setRowHidden/hideRow/showRow 族） ---- */
        XTableView_setRowHidden(&table, 1, true);
        XAPI_EXPECT(XTableView_isRowHidden(&table, 1) &&
                    !XTableView_isRowHidden(&table, 9),
                    "setRowHidden 往返、越界 false");
        XTableView_hideColumn(&table, 0);
        XAPI_EXPECT(XTableView_isColumnHidden(&table, 0),
                    "hideColumn 转发隐藏");
        XTableView_showColumn(&table, 0);
        XAPI_EXPECT(!XTableView_isColumnHidden(&table, 0),
                    "showColumn 恢复显示");
        XTableView_hideRow(&table, 2);
        XAPI_EXPECT(XTableView_isRowHidden(&table, 2),
                    "hideRow 便捷接口生效");
        XTableView_showRow(&table, 2);
        /* 基座 API 恢复可见：后续视口位置/rowAt 断言需要行 2 可见
           （行 1 保持隐藏用于隐藏占位与空矩形探测）。 */
        XTableView_setRowHidden(&table, 2, false);

        /* ---- 视口位置（表头 20px + 可见行高/列宽累计，隐藏占 0） ---- */
        XAPI_EXPECT(XTableView_rowViewportPosition(&table, 0) == 20,
                    "rowViewportPosition(0)=表头高 20");
        XAPI_EXPECT(XTableView_rowViewportPosition(&table, 2) == 20 + 30,
                    "隐藏行占高 0：行 2 位置=20+30（跳过行 1）");
        XAPI_EXPECT(XTableView_columnViewportPosition(&table, 0) == 0 &&
                    XTableView_columnViewportPosition(&table, 1) == 120,
                    "columnViewportPosition 按可见列宽累计");
        XAPI_EXPECT(XTableView_rowViewportPosition(&table, 9) == -1,
                    "行越界 rowViewportPosition 返回 -1");
        /* ---- 位置反查（对标 rowAt/columnAt，与位置查询互逆） ---- */
        XAPI_EXPECT(XTableView_rowAt(&table, 20 + 15) == 0,
                    "rowAt(35)=行 0（扣表头后按行高判定）");
        XAPI_EXPECT(XTableView_rowAt(&table, 20 + 30 + 15) == 2,
                    "rowAt 跳过隐藏行 1 命中行 2");
        XAPI_EXPECT(XTableView_rowAt(&table, 10) == -1,
                    "rowAt 表头区返回 -1");
        XAPI_EXPECT(XTableView_columnAt(&table, 60) == 0 &&
                    XTableView_columnAt(&table, 150) == 1,
                    "columnAt 按可见列宽反查");
        XTableView_showColumn(&table, 0);
        /* ---- visualRect（隐藏单元格返回空矩形，对标 Qt 不可见条目） ---- */
        rect = XTableView_visualRect(&table, 0, 0);
        XAPI_EXPECT(rect.x == 0 && rect.y == 20 && rect.width == 120 &&
                    rect.height == 30,
                    "visualRect(0,0) 位置/尺寸与位置查询同口径");
        XAPI_EXPECT(XTableView_visualRect(&table, 1, 0).width == 0 &&
                    XTableView_visualRect(&table, 1, 0).height == 0,
                    "隐藏行单元格 visualRect 为空矩形");
        /* ---- 按内容调整（隐藏行不参与测算，统一行高承载） ---- */
        XTableView_resizeColumnToContents(&table, 0);
        XTableView_resizeColumnsToContents(&table);
        XTableView_resizeRowToContents(&table, 0);
        XTableView_resizeRowsToContents(&table);
        XAPI_EXPECT(XTableView_rowHeight(&table) > 0,
                    "resizeToContents 族调用后行高保持正值");
        /* ---- 跨行/列合并（对标 setSpan/rowSpan/columnSpan/clearSpans） ---- */
        XTableView_setSpan(&table, 0, 0, 2, 2);
        XAPI_EXPECT(XTableView_rowSpan(&table, 0, 0) == 2 &&
                    XTableView_columnSpan(&table, 0, 0) == 2,
                    "setSpan(0,0,2,2) 合并区间生效");
        XAPI_EXPECT(XTableView_rowSpan(&table, 1, 1) == 2,
                    "被覆盖格（非原点）返回所属区间跨行数（同 Qt）");
        XAPI_EXPECT(XTableView_rowSpan(&table, 2, 0) == 1,
                    "未合并格跨行数=1");
        XTableView_setSpan(&table, 0, 0, 1, 1);
        XAPI_EXPECT(XTableView_rowSpan(&table, 0, 0) == 1,
                    "rowSpan/columnSpan 均 1 移除合并（同 Qt）");
        XTableView_setSpan(&table, 0, 0, 0, 2);
        XAPI_EXPECT(XTableView_rowSpan(&table, 0, 0) == 1,
                    "任一跨度 <=0 忽略");
        XTableView_setSpan(&table, 0, 0, 2, 2);
        XTableView_setSpan(&table, 0, 0, 3, 2);
        XAPI_EXPECT(XTableView_rowSpan(&table, 0, 0) == 3,
                    "同原点重复设置替换旧值");
        XTableView_clearSpans(&table);
        XAPI_EXPECT(XTableView_rowSpan(&table, 0, 0) == 1,
                    "clearSpans 清空全部合并");

        /* ---- 表头对象挂接（对标 setHorizontalHeader/VerticalHeader） ---- */
        XTableView_setHorizontalHeader(&table, hHeader);
        XTableView_setVerticalHeader(&table, vHeader);
        XAPI_EXPECT(XTableView_horizontalHeader(&table) == hHeader &&
                    XTableView_verticalHeader(&table) == vHeader,
                    "水平/垂直表头对象挂接往返");
        XTableView_setHorizontalHeader(&table, vHeader);
        XAPI_EXPECT(XTableView_horizontalHeader(&table) == hHeader,
                    "方向不符（垂直头挂水平位）忽略");
        XTableView_setHorizontalHeader(&table, NULL);
        XAPI_EXPECT(XTableView_horizontalHeader(&table) == NULL,
                    "setHorizontalHeader(NULL) 清除挂接");

        /* ---- 排序与行/列选择 ---- */
        XTableView_setSortingEnabled(&table, true);
        XAPI_EXPECT(XTableView_isSortingEnabled(&table),
                    "setSortingEnabled 往返");
        XTableView_setSortingEnabled(&table, false);
        XTableView_sortByColumn(&table, 1, 1);
        XAPI_EXPECT(true, "sortByColumn 调用无崩溃（状态承载）");
        XTableView_selectRow(&table, 1);
        XAPI_EXPECT(XAbstractItemView_currentRow(&table.m_base) == 1,
                    "selectRow(1) 当前行=1（当前项承载）");
        XTableView_selectColumn(&table, 1);
        XAPI_EXPECT(XAbstractItemView_currentColumn(&table.m_base) == 1,
                    "selectColumn(1) 当前列=1");

        if (hHeader) XHeaderView_delete_base(hHeader);
        if (vHeader) XHeaderView_delete_base(vHeader);
        if (model) XAbstractItemModel_delete_base(model);
        XTableView_deinit_base(&table);
    }

    /* ================================================================
     * 8. XTableWidget：表格控件（维度/单元格/表头/查找/排序/选区/信号）。
     * ================================================================ */
    {
        XTableWidget table;
        const char* hLabels[2] = { "名称", "数量" };
        const char* vLabels[2] = { "甲行", "乙行" };
        int rows[4];
        int cols[4];
        int count;
        int hitRow;
        int hitCol;
        XString* taken;

        vsig_reset();
        XTableWidget_init(&table, NULL, 0);
        XWidget_setGeometry((XWidget*)&table, 0, 0, 300, 240);
        vsig_connectTable((XObject*)&table);

        XAPI_EXPECT(XTableWidget_rowCount(&table) == 0 &&
                    XTableWidget_columnCount(&table) == 0,
                    "初始 0 行 0 列（对标新建 QTableWidget(0,0)）");
        XAPI_EXPECT(XTableWidget_currentRow(&table) == -1 &&
                    XTableWidget_currentColumn(&table) == -1,
                    "初始无当前单元格");
        XAPI_EXPECT(XTableWidget_model(&table) != NULL,
                    "内建数据模型桥可用（对标 QTableWidget::model）");
        XAPI_EXPECT(XTableWidget_itemPrototype(&table) == NULL,
                    "初始无原型单元格（对标 itemPrototype 缺省 NULL）");
        XAPI_EXPECT(XTableView_rowHeight((XTableView*)&table) == 24 &&
                    XTableView_showGrid((XTableView*)&table),
                    "默认行高 24、网格可见（init 契约）");

        /* ---- 维度（对标 setRowCount/setColumnCount） ---- */
        XTableWidget_setRowCount(&table, 3);
        XTableWidget_setColumnCount(&table, 2);
        XAPI_EXPECT(XTableWidget_rowCount(&table) == 3 &&
                    XTableWidget_columnCount(&table) == 2,
                    "setRowCount/setColumnCount 维度 3x2");
        XTableWidget_setRowCount(&table, -1);
        XAPI_EXPECT(XTableWidget_rowCount(&table) == 3,
                    "setRowCount 负值忽略");
        /* ---- 行列插入/移除（对标 insertRow/insertColumn/removeRow） ---- */
        XTableWidget_setText(&table, 0, 0, "甲");
        XTableWidget_setText(&table, 1, 0, "乙");
        XTableWidget_insertRow(&table, 1);
        XAPI_EXPECT(XTableWidget_rowCount(&table) == 4 &&
                    strcmp(xapi_cstr(XTableWidget_text(&table, 2, 0)), "乙") == 0,
                    "insertRow(1) 原行 1 后移到行 2");
        XTableWidget_removeRow(&table, 1);
        XAPI_EXPECT(XTableWidget_rowCount(&table) == 3 &&
                    strcmp(xapi_cstr(XTableWidget_text(&table, 1, 0)), "乙") == 0,
                    "removeRow(1) 后续行前移");
        XTableWidget_removeRow(&table, 99);
        XAPI_EXPECT(XTableWidget_rowCount(&table) == 3,
                    "removeRow 越界无操作");
        XTableWidget_insertColumn(&table, 1);
        XAPI_EXPECT(XTableWidget_columnCount(&table) == 3,
                    "insertColumn(1) 列数 +1");
        XTableWidget_removeColumn(&table, 1);
        XAPI_EXPECT(XTableWidget_columnCount(&table) == 2,
                    "removeColumn(1) 列数 -1");

        /* ---- 单元格文本（对标 setText/text 快捷族） ---- */
        XTableWidget_setText(&table, 0, 0, "苹果");
        XTableWidget_setText(&table, 0, 1, "3");
        XTableWidget_setText(&table, 1, 0, "香蕉");
        XTableWidget_setText(&table, 1, 1, "5");
        XTableWidget_setText(&table, 2, 0, "樱桃");
        XTableWidget_setText(&table, 2, 1, "8");
        XAPI_EXPECT(strcmp(xapi_cstr(XTableWidget_text(&table, 0, 0)), "苹果") == 0 &&
                    strcmp(xapi_cstr(XTableWidget_text(&table, 2, 1)), "8") == 0,
                    "setText/text 往返一致");
        XAPI_EXPECT(strcmp(xapi_cstr(XTableWidget_text(&table, 9, 9)), "") == 0,
                    "越界 text 返回空串");
        XAPI_EXPECT(strcmp(XAbstractItemModel_data_2(XTableWidget_model(&table),
                                                     0, 0), "苹果") == 0,
                    "单元格文本与内建模型同步");
        /* ---- setItem 拷贝全字段（对标 setItem/item） ---- */
        {
            XTableWidgetItem src;
            const XTableWidgetItem* got;
            memset(&src, 0, sizeof(src));
            src.text = XString_create_utf8("整格");
            src.selected = true;
            src.checkState = 2;
            src.foreground = 0xFF0000FFu;
            src.background = 0xFF00FF00u;
            XTableWidget_setItem(&table, 2, 1, &src);
            XString_delete_base((XClass*)src.text);
            got = XTableWidget_item(&table, 2, 1);
            XAPI_EXPECT(got != NULL &&
                        strcmp(xapi_u8(got->text), "整格") == 0 &&
                        got->selected && got->checkState == 2,
                        "setItem 拷贝全字段、item 只读读取");
            XAPI_EXPECT(XTableWidget_item(&table, 9, 9) == NULL,
                        "越界 item 返回 NULL");
        }
        /* ---- 当前单元格与信号（对标 setCurrentCell/currentCellChanged） ---- */
        vsig_reset();
        XTableWidget_setCurrentCell(&table, 1, 1);
        XAPI_EXPECT(XTableWidget_currentRow(&table) == 1 &&
                    XTableWidget_currentColumn(&table) == 1,
                    "setCurrentCell(1,1) 当前行列=(1,1)");
        XAPI_EXPECT(g_sig.tblCurCell == 1,
                    "setCurrentCell 发射 currentCellChanged");
        XAPI_EXPECT(g_sig.tblSelChanged == 1,
                    "跟踪选区变化发射 itemSelectionChanged");
        XAPI_EXPECT(XTableWidget_currentItem(&table) ==
                    XTableWidget_item(&table, 1, 1),
                    "currentItem 恒等返回当前单元格指针");
        /* ---- setCurrentItem（要求目标格存在；同 setCurrentCell 差异） ---- */
        XTableWidget_setCurrentCell(&table, 0, 0);
        vsig_reset();
        XTableWidget_setCurrentItem(&table, 2, 0);
        XAPI_EXPECT(XTableWidget_currentRow(&table) == 2 &&
                    XTableWidget_currentColumn(&table) == 0,
                    "setCurrentItem 转发 setCurrentCell");
        /* ---- 点击注入（对标 cellClicked/cellPressed；坐标扣表头 24/40） ---- */
        vsig_reset();
        vw_injectPress((XWidget*)&table, 40 + 40, 24 + 12);
        XAPI_EXPECT(g_sig.tblCellClicked == 1 && g_sig.tblCCRow == 0 &&
                    g_sig.tblCCCol == 0,
                    "点击 (0,0) 发射 cellClicked(0,0)");
        XAPI_EXPECT(g_sig.orderLen >= 2 && g_sig.order[0] == 'p' &&
                    g_sig.order[1] == 'c',
                    "先 cellPressed 后 cellClicked（按压在前的次序）");
        XAPI_EXPECT(XTableWidget_currentRow(&table) == 0 &&
                    XTableWidget_currentColumn(&table) == 0,
                    "点击命中写入当前单元格");
        vsig_reset();
        vw_injectDblClick((XWidget*)&table, 40 + 40, 24 + 12);
        XAPI_EXPECT(g_sig.tblCellDouble == 1,
                    "双击发射 cellDoubleClicked");
        vsig_reset();
        vw_injectMove((XWidget*)&table, 40 + 120, 24 + 36);
        vw_injectMove((XWidget*)&table, 40 + 40, 24 + 12);
        XAPI_EXPECT(true, "cellEntered 差分判重路径调用无崩溃");
        vw_injectKey((XWidget*)&table, XEVENT_TYPE_KEY_PRESS, 0);
        XAPI_EXPECT(true, "键盘事件路径调用无崩溃");
        /* ---- 位置反查（对标 itemAt；表头区 -1） ---- */
        XTableWidget_itemAt(&table, 40 + 40, 24 + 12, &hitRow, &hitCol);
        XAPI_EXPECT(hitRow == 0 && hitCol == 0,
                    "itemAt 数据区命中 (0,0)");
        XTableWidget_itemAt(&table, 10, 24 + 12, &hitRow, &hitCol);
        XAPI_EXPECT(hitRow == -1 && hitCol == -1,
                    "itemAt 表头区返回 -1/-1");
        /* ---- 表头标签（对标 setHorizontal/VerticalHeaderLabels 族） ---- */
        XTableWidget_setHorizontalHeaderLabels(&table, hLabels, 2);
        XTableWidget_setVerticalHeaderLabels(&table, vLabels, 2);
        XAPI_EXPECT(strcmp(XTableWidget_horizontalHeaderItem(&table, 0),
                           "名称") == 0 &&
                    strcmp(XTableWidget_verticalHeaderItem(&table, 1),
                           "乙行") == 0,
                    "表头标签批量写入与读取");
        XAPI_EXPECT(strcmp(XTableWidget_horizontalHeaderItem(&table, 9),
                           "") == 0,
                    "越界表头标签返回空串");
        {
            XString* single = XString_create_utf8("单列头");
            XTableWidget_setHorizontalHeaderItem(&table, 1, single);
            XString_delete_base((XClass*)single);
            XAPI_EXPECT(strcmp(XTableWidget_horizontalHeaderItem(&table, 1),
                               "单列头") == 0,
                        "setHorizontalHeaderItem 单列覆写（表格拷贝文本）");
        }
        taken = XTableWidget_takeHorizontalHeaderItem(&table, 1);
        XAPI_EXPECT(taken != NULL &&
                    strcmp(xapi_u8(taken), "单列头") == 0 &&
                    xapi_cstr(XTableWidget_horizontalHeaderItem(&table, 1))[0] == '\0',
                    "takeHorizontalHeaderItem 所有权转移并置空");
        if (taken) XString_delete_base((XClass*)taken);
        taken = XTableWidget_takeVerticalHeaderItem(&table, 0);
        XAPI_EXPECT(taken != NULL &&
                    strcmp(xapi_u8(taken), "甲行") == 0,
                    "takeVerticalHeaderItem 取出行表头文本");
        if (taken) XString_delete_base((XClass*)taken);
        /* ---- 查找（对标 findItems 精确/包含） ---- */
        count = XTableWidget_findItems(&table, "苹果", 1, rows, cols, 4);
        XAPI_EXPECT(count == 1 && rows[0] == 0 && cols[0] == 0,
                    "findItems 精确命中 (0,0)");
        count = XTableWidget_findItems(&table, "", 1, rows, cols, 4);
        XAPI_EXPECT(count == 0,
                    "空表无空文本命中（NULL 按空串处理）");
        /* ---- items 按行搜索（对标 items 行号承载） ---- */
        {
            XVector* found = XTableWidget_items(&table, "香蕉");
            XAPI_EXPECT(found != NULL &&
                        (int)XVector_size_base((const XContainer*)found) == 1 &&
                        XVector_At_Base(found, 0, int) == 1,
                        "items 精确反查命中行 1");
            if (found) XVector_delete_base((XClass*)found);
        }
        /* ---- 排序（对标 sortItems 整行重排；ASCII 键避免字节序歧义） ---- */
        XTableWidget_setText(&table, 0, 0, "b");
        XTableWidget_setText(&table, 1, 0, "a");
        XTableWidget_setText(&table, 2, 0, "c");
        XTableWidget_sortItems(&table, 0, 0);
        XAPI_EXPECT(strcmp(xapi_cstr(XTableWidget_text(&table, 0, 0)), "a") == 0 &&
                    strcmp(xapi_cstr(XTableWidget_text(&table, 2, 0)), "c") == 0,
                    "sortItems(0,0) 按列 0 升序整行重排");
        XTableWidget_sortItems(&table, 0, 1);
        XAPI_EXPECT(strcmp(xapi_cstr(XTableWidget_text(&table, 0, 0)), "c") == 0 &&
                    strcmp(xapi_cstr(XTableWidget_text(&table, 2, 0)), "a") == 0,
                    "sortItems(0,1) 降序整行重排");
        /* ---- 选区（对标 setRangeSelected/selectedIndexes/Ranges） ---- */
        vsig_reset();
        XTableWidget_setRangeSelected(&table, 0, 0, 1, 1, true);
        XAPI_EXPECT(g_sig.tblSelChanged >= 1,
                    "setRangeSelected 有变化发射 itemSelectionChanged");
        count = XTableWidget_selectedIndexes(&table, rows, cols, 4);
        XAPI_EXPECT(count >= 4,
                    "selectedIndexes 输出范围选中的 4 格");
        {
            int top[4];
            int left[4];
            int bottom[4];
            int right[4];
            count = XTableWidget_selectedRanges(&table, top, left, bottom,
                                                right, 4);
            XAPI_EXPECT(count >= 1 && top[0] == 0 && bottom[0] == 1,
                        "selectedRanges 归并连续行为一条范围");
        }
        XTableWidget_setRangeSelected(&table, 1, 1, 0, 0, false);
        XAPI_EXPECT(true, "setRangeSelected 倒置范围自动归一化（取消路径）");
        count = XTableWidget_selectedItems(&table, rows, cols, 4);
        XAPI_EXPECT(count >= 0,
                    "selectedItems 转发 selectedIndexes（语义重合）");
        /* ---- 单元格部件挂载（对标 setCellWidget/cellWidget） ---- */
        {
            XWidget* cellWidget = XWidget_create(NULL, 0);
            XTableWidget_setCellWidget(&table, 0, 1, cellWidget);
            XAPI_EXPECT(XTableWidget_cellWidget(&table, 0, 1) == cellWidget,
                        "setCellWidget/cellWidget 借用往返");
            XTableWidget_removeCellWidget(&table, 0, 1);
            XAPI_EXPECT(XTableWidget_cellWidget(&table, 0, 1) == NULL,
                        "removeCellWidget 解除关联");
            XTableWidget_setCellWidget(&table, 0, 1, cellWidget);
            XTableWidget_setCellWidget(&table, 0, 1, NULL);
            XAPI_EXPECT(XTableWidget_cellWidget(&table, 0, 1) == NULL,
                        "setCellWidget(NULL) 等价 removeCellWidget");
            if (cellWidget) XWidget_delete_base(cellWidget);
        }
        /* ---- takeItem（对标 takeItem：文本所有权转移） ---- */
        vsig_reset();
        taken = XTableWidget_takeItem(&table, 2, 1);
        XAPI_EXPECT(taken != NULL,
                    "takeItem 归还单元格文本所有权");
        XAPI_EXPECT(xapi_cstr(XTableWidget_text(&table, 2, 1))[0] == '\0' &&
                    g_sig.tblCellChanged >= 1,
                    "取出后置空并发射 cellChanged");
        if (taken) XString_delete_base((XClass*)taken);
        XAPI_EXPECT(XTableWidget_takeItem(&table, 2, 1) == NULL,
                    "空单元格 takeItem 返回 NULL（不发信号）");
        /* ---- 索引反查/恒等承载（对标 indexFromItem/row/column） ---- */
        {
            const XTableWidgetItem* cur = XTableWidget_currentItem(&table);
            XAPI_EXPECT(cur != NULL &&
                        XTableWidget_indexFromItem(&table, cur, &hitCol) >= 0,
                        "indexFromItem 按地址反查行号");
        }
        XAPI_EXPECT(XTableWidget_row(&table, 1) == 1 &&
                    XTableWidget_row(&table, 99) == -1 &&
                    XTableWidget_column(&table, 1) == 1 &&
                    XTableWidget_column(&table, 99) == -1,
                    "row/column 恒等映射（有效原样、越界 -1）");
        {
            XString* rowText = XTableWidget_itemFromIndex(&table, 0);
            XAPI_EXPECT(rowText != NULL,
                        "itemFromIndex 返回行首列文本副本");
            if (rowText) XString_delete_base((XClass*)rowText);
        }
        /* ---- 视觉序（平铺模型视觉序=逻辑序） ---- */
        XAPI_EXPECT(XTableWidget_visualRow(&table, 2) == 2 &&
                    XTableWidget_visualColumn(&table, 1) == 1,
                    "visualRow/visualColumn 恒等（平铺无重排映射）");
        /* ---- 原型承载（对标 setItemPrototype 不透明记录） ---- */
        {
            XTableWidgetItem proto;
            memset(&proto, 0, sizeof(proto));
            XTableWidget_setItemPrototype(&table, &proto);
            XAPI_EXPECT(XTableWidget_itemPrototype(&table) == &proto,
                        "setItemPrototype 不透明借用记录");
            XTableWidget_setItemPrototype(&table, NULL);
            XAPI_EXPECT(XTableWidget_itemPrototype(&table) == NULL,
                        "setItemPrototype(NULL) 清除");
        }
        /* ---- scrollToItem / editItem / clearSpans（便捷转发） ---- */
        XTableWidget_scrollToItem(&table, 2, 1);
        XTableWidget_editItem(&table, 0, 0);
        XTableWidget_clearSpans(&table);
        XAPI_EXPECT(true, "scrollToItem/editItem/clearSpans 便捷转发无崩溃");
        /* ---- clearContents：保留行列数、清空内容（对标 clearContents） ---- */
        XTableWidget_clearContents(&table);
        XAPI_EXPECT(XTableWidget_rowCount(&table) == 3 &&
                    strcmp(xapi_cstr(XTableWidget_text(&table, 0, 0)), "") == 0,
                    "clearContents 清内容且保留 3 行");
        XAPI_EXPECT(strcmp(XAbstractItemModel_data_2(XTableWidget_model(&table),
                                                     0, 0), "") == 0,
                    "clearContents 同步内建模型为空");
        /* ---- clear：行列归零、表头一并清空（对标 clear） ---- */
        XTableWidget_setHorizontalHeaderLabels(&table, hLabels, 2);
        XTableWidget_clear(&table);
        XAPI_EXPECT(XTableWidget_rowCount(&table) == 0 &&
                    XTableWidget_columnCount(&table) == 0,
                    "clear 后行列数归零");
        XAPI_EXPECT(xapi_cstr(XTableWidget_horizontalHeaderItem(&table, 0))[0] == '\0',
                    "clear 同时移除表头文本（Qt clear 同语义）");
        XAPI_EXPECT(XTableWidget_currentRow(&table) == -1,
                    "clear 复位当前单元格");

        /* 栈对象：delete_base 经 IsHeap=false 仅析构不释放。 */
        XTableWidget_delete_base(&table);
    }

    /* ================================================================
     * 9. XHeaderView：表头段几何（段数/尺寸/隐藏/移动/指示器/持久化）。
     * ================================================================ */
    {
        XHeaderView header;
        XHeaderView vertical;
        XHeaderView donor;
        XHeaderView acceptor;
        int oldGeometries;

        vsig_reset();
        XHeaderView_init(&header, NULL, 0, 0);
        XHeaderView_init(&vertical, NULL, 0, 1);
        vsig_connectHeader((XObject*)&header);

        XAPI_EXPECT(XHeaderView_orientation(&header) == 0 &&
                    XHeaderView_orientation(&vertical) == 1,
                    "构造方向 0=水平 1=垂直（对标 Qt::Orientation）");
        XAPI_EXPECT(XHeaderView_count(&header) == 0,
                    "初始段数 0");
        XAPI_EXPECT(XHeaderView_defaultSectionSize(&header) == 30,
                    "默认段尺寸 30（本库常量；Qt 为样式相关值）");
        XAPI_EXPECT(XHeaderView_minimumSectionSize(&header) == 20,
                    "最小区间尺寸缺省 20（Qt 按字体测算，本库固定）");
        XAPI_EXPECT(XHeaderView_maximumSectionSize(&header) == 1048575,
                    "最大区间尺寸缺省 1048575（Qt 同值）");
        XAPI_EXPECT(XHeaderView_sectionResizeMode(&header) ==
                    XHeaderViewResizeMode_Interactive,
                    "全局调整模式缺省 Interactive（Qt 缺省）");
        XAPI_EXPECT(!XHeaderView_stretchLastSection(&header) &&
                    !XHeaderView_isStretchLastSection(&header),
                    "末尾拉伸缺省 false（Qt 缺省；双命名同源）");
        XAPI_EXPECT(XHeaderView_stretchSectionCount(&header) == 0,
                    "无拉伸段时 stretchSectionCount=0");
        XAPI_EXPECT(XHeaderView_defaultAlignment(&header) ==
                    (int)XAlignment_Center,
                    "水平头缺省对齐居中（对标 setDefaultValues）");
        XAPI_EXPECT(XHeaderView_defaultAlignment(&vertical) ==
                    ((int)XAlignment_Left | (int)XAlignment_VCenter),
                    "垂直头缺省对齐左+垂直居中");
        XAPI_EXPECT(!XHeaderView_sectionsClickable(&header) &&
                    !XHeaderView_sectionsMovable(&header),
                    "可点击/可拖动缺省 false（Qt movable 缺省 true，注释差异）");
        XAPI_EXPECT(XHeaderView_sortIndicatorSection(&header) == -1 &&
                    XHeaderView_sortIndicatorOrder(&header) == 0 &&
                    !XHeaderView_isSortIndicatorShown(&header),
                    "排序指示器缺省无（-1/升序/不显示）");
        XAPI_EXPECT(!XHeaderView_isSortIndicatorClearable(&header),
                    "排序指示器可清除缺省 false（Qt 6.1+ 缺省）");
        XAPI_EXPECT(!XHeaderView_sectionsHidden(&header) &&
                    XHeaderView_hiddenSectionCount(&header) == 0,
                    "初始无隐藏段");
        XAPI_EXPECT(XHeaderView_offset(&header) == 0,
                    "滚动偏移初值 0");
        XAPI_EXPECT(XHeaderView_length(&header) == 0,
                    "空表头总长 0");
        XAPI_EXPECT(XHeaderView_viewport(&header) == (XWidget*)&header,
                    "viewport 收敛返回自身（整个表头即视口）");

        /* ---- 属性往返 ---- */
        XHeaderView_setStretchLastSection(&header, true);
        XAPI_EXPECT(XHeaderView_stretchLastSection(&header) &&
                    XHeaderView_isStretchLastSection(&header) &&
                    XHeaderView_stretchSectionCount(&header) == 1,
                    "stretchLast 往返且 stretchSectionCount=1");
        XHeaderView_setStretchLastSection(&header, false);
        XHeaderView_setDefaultAlignment(&header, (int)XAlignment_Left);
        XAPI_EXPECT(XHeaderView_defaultAlignment(&header) ==
                    (int)XAlignment_Left,
                    "setDefaultAlignment 往返");
        XHeaderView_setSectionsClickable(&header, true);
        XHeaderView_setSectionsMovable(&header, true);
        XAPI_EXPECT(XHeaderView_sectionsClickable(&header) &&
                    XHeaderView_sectionsMovable(&header),
                    "sectionsClickable/Movable 往返");
        XAPI_EXPECT(XHeaderView_isFirstSectionMovable(&header),
                    "首段可动需 sectionsMovable 同时为真（缺省首段状态 true）");
        XHeaderView_setFirstSectionMovable(&header, false);
        XAPI_EXPECT(!XHeaderView_isFirstSectionMovable(&header),
                    "setFirstSectionMovable(false) 生效");
        XHeaderView_setSectionsClickable(&header, false);
        XHeaderView_setSectionsMovable(&header, false);
        XHeaderView_setHighlightSections(&header, true);
        XHeaderView_setCascadingSectionResizes(&header, true);
        XHeaderView_setResizeContentsPrecision(&header, 50);
        XAPI_EXPECT(XHeaderView_highlightSections(&header) &&
                    XHeaderView_cascadingSectionResizes(&header) &&
                    XHeaderView_resizeContentsPrecision(&header) == 50,
                    "highlight/cascading/precision setter 往返");
        XHeaderView_setHighlightSections(&header, false);
        XHeaderView_setCascadingSectionResizes(&header, false);
        XHeaderView_setResizeContentsPrecision(&header, 1000);

        /* ---- 段数与新增默认尺寸 ---- */
        XHeaderView_setDefaultSectionSize(&header, 50);
        XAPI_EXPECT(XHeaderView_defaultSectionSize(&header) == 50,
                    "setDefaultSectionSize 往返");
        vsig_reset();
        XHeaderView_setCount(&header, 3);
        XAPI_EXPECT(XHeaderView_count(&header) == 3 &&
                    XHeaderView_sectionSize(&header, 0) == 50 &&
                    XHeaderView_sectionSize(&header, 2) == 50,
                    "setCount 新增段用默认尺寸 50");
        XAPI_EXPECT(g_sig.hvCountChanged == 1 && g_sig.hvOldCount == 0 &&
                    g_sig.hvNewCount == 3 && g_sig.hvGeometries == 1,
                    "段数变化发射 sectionCountChanged+geometriesChanged");
        XHeaderView_setCount(&header, 3);
        XAPI_EXPECT(g_sig.hvCountChanged == 1,
                    "段数未变不重复发信号（对标 Qt）");
        XHeaderView_setCount(&header, -1);
        XAPI_EXPECT(XHeaderView_count(&header) == 3,
                    "setCount 负值忽略");
        XHeaderView_resetDefaultSectionSize(&header);
        XAPI_EXPECT(XHeaderView_defaultSectionSize(&header) == 30,
                    "resetDefaultSectionSize 复位常量 30（本库缺省）");

        /* ---- resizeSection 钳制（先 [min,max] 再落库，对标 resizeSection） ---- */
        XHeaderView_setMaximumSectionSize(&header, 100);
        XHeaderView_setSectionSize(&header, 1, 4000);
        XAPI_EXPECT(XHeaderView_sectionSize(&header, 1) == 4000,
                    "setSectionSize 原样存值（程序化钳制入口为 resizeSection）");
        XHeaderView_resizeSection(&header, 0, 10);
        XAPI_EXPECT(XHeaderView_sectionSize(&header, 0) == 20,
                    "resizeSection 先钳制到最小 20");
        XHeaderView_resizeSection(&header, 0, 500);
        XAPI_EXPECT(XHeaderView_sectionSize(&header, 0) == 100,
                    "resizeSection 先钳制到最大 100");
        XHeaderView_resizeSection(&header, 0, 0);
        XAPI_EXPECT(XHeaderView_sectionSize(&header, 0) == 100,
                    "resizeSection 0 拒绝（沿用 >0 约束；Qt 允许 0，注释差异）");
        XHeaderView_resizeSection(&header, 9, 30);
        XAPI_EXPECT(XHeaderView_sectionSize(&header, 9) == 30,
                    "越界段读取默认尺寸（30），写入忽略");
        XHeaderView_setSectionSize(&header, 0, 5000);
        XHeaderView_setMaximumSectionSize(&header, 100);
        XAPI_EXPECT(XHeaderView_sectionSize(&header, 0) == 100 &&
                    XHeaderView_sectionSize(&header, 1) == 100,
                    "设置 max 时超限既有段立即钳制（Qt 亦同）");
        XHeaderView_setMaximumSectionSize(&header, -1);
        XAPI_EXPECT(XHeaderView_maximumSectionSize(&header) == 1048575,
                    "setMaximumSectionSize(-1) 重置硬上限（Qt 语义）");
        XHeaderView_setSectionSize(&header, 2, 60);
        XAPI_EXPECT(g_sig.hvSectionResized >= 1,
                    "setSectionSize 尺寸变化发射 sectionResized");
        /* ---- 段位置与位置反查 ---- */
        XHeaderView_setSectionSize(&header, 0, 20);
        XHeaderView_setSectionSize(&header, 1, 100);
        XHeaderView_setSectionSize(&header, 2, 60);
        XAPI_EXPECT(XHeaderView_sectionPosition(&header, 0) == 0 &&
                    XHeaderView_sectionPosition(&header, 1) == 20 &&
                    XHeaderView_sectionPosition(&header, 2) == 120,
                    "sectionPosition 逐段累加（对标 sectionPosition）");
        XAPI_EXPECT(XHeaderView_sectionViewportPosition(&header, 2) ==
                    XHeaderView_sectionPosition(&header, 2),
                    "sectionViewportPosition 与表头坐标系一致（收敛）");
        XAPI_EXPECT(XHeaderView_logicalIndexAt(&header, 10) == 0 &&
                    XHeaderView_logicalIndexAt(&header, 50) == 1,
                    "logicalIndexAt 位置反查段号");
        XAPI_EXPECT(XHeaderView_logicalIndexAt(&header, 1000) == -1 &&
                    XHeaderView_logicalIndexAt(&header, -1) == -1,
                    "logicalIndexAt 越界/负位置返回 -1");
        XAPI_EXPECT(XHeaderView_length(&header) == 180,
                    "length=可见段尺寸和 20+100+60");
        /* ---- 隐藏段（对标 hideSection/showSection/isSectionHidden） ---- */
        oldGeometries = g_sig.hvGeometries;
        XHeaderView_hideSection(&header, 1);
        XAPI_EXPECT(XHeaderView_isSectionHidden(&header, 1) &&
                    XHeaderView_hiddenSectionCount(&header) == 1 &&
                    XHeaderView_sectionsHidden(&header),
                    "hideSection 隐藏生效且计数/存在性同步");
        XAPI_EXPECT(XHeaderView_length(&header) == 80,
                    "隐藏段长度按 0 计（20+60）");
        XAPI_EXPECT(g_sig.hvGeometries == oldGeometries + 1,
                    "隐藏状态变化发射 geometriesChanged");
        XHeaderView_showSection(&header, 1);
        XAPI_EXPECT(!XHeaderView_isSectionHidden(&header, 1) &&
                    XHeaderView_hiddenSectionCount(&header) == 0,
                    "showSection 恢复显示");
        XHeaderView_setSectionHidden(&header, 1, true);
        XAPI_EXPECT(XHeaderView_isSectionHidden(&header, 1),
                    "setSectionHidden(true) 转发 hideSection");
        XHeaderView_setSectionHidden(&header, 1, false);
        XAPI_EXPECT(!XHeaderView_isSectionHidden(&header, 9) &&
                    XHeaderView_sectionSize(&header, 9) == 30,
                    "越界段隐藏读取 false（尺寸回默认 30）");
        /* ---- 移动/交换（移动即重排，逻辑=视觉；信号发射） ---- */
        vsig_reset();
        XHeaderView_moveSection(&header, 0, 2);
        XAPI_EXPECT(XHeaderView_sectionSize(&header, 0) == 100 &&
                    XHeaderView_sectionSize(&header, 2) == 20,
                    "moveSection(0,2) 段序整体平移");
        XAPI_EXPECT(g_sig.hvSectionMoved == 1 && g_sig.hvGeometries >= 1,
                    "实际移动发射 sectionMoved+geometriesChanged");
        XHeaderView_swapSections(&header, 0, 1);
        XAPI_EXPECT(XHeaderView_sectionSize(&header, 0) == 60 &&
                    XHeaderView_sectionSize(&header, 1) == 100,
                    "swapSections 交换两段（[100,60,20]→[60,100,20]）");
        XAPI_EXPECT(g_sig.hvSectionMoved == 3,
                    "swap 发射两次 sectionMoved（双向各一）");
        XAPI_EXPECT(XHeaderView_visualIndex(&header, 1) == 1 &&
                    XHeaderView_visualIndexAt(&header, 1) == 1 &&
                    XHeaderView_logicalIndex(&header, 1) == 1,
                    "移动即重排：逻辑序与视觉序恒一（恒等映射）");
        /* ---- 尺寸提示与批量重设 ---- */
        XAPI_EXPECT(XHeaderView_sectionSizeHint(&header, 0) ==
                    XHeaderView_defaultSectionSize(&header),
                    "sectionSizeHint 无内容感知返回缺省尺寸");
        XHeaderView_resizeSections(&header, XHeaderViewResizeMode_Stretch);
        XAPI_EXPECT(XHeaderView_sectionSize(&header, 0) ==
                        XHeaderView_sectionSize(&header, 1),
                    "resizeSections(Stretch) 以当前 length 均分可见段");
        XHeaderView_resizeSections(&header, XHeaderViewResizeMode_Interactive);
        XAPI_EXPECT(XHeaderView_count(&header) == 3,
                    "resizeSections(Interactive) 保持段数（仅重绘）");
        /* ---- 单段/全局调整模式（对标单段重载与全局覆写清除） ---- */
        XHeaderView_setSectionResizeModeAt(&header, 1,
                                           XHeaderViewResizeMode_Fixed);
        XAPI_EXPECT(XHeaderView_sectionResizeModeAt(&header, 1) ==
                        XHeaderViewResizeMode_Fixed &&
                    XHeaderView_sectionResizeModeAt(&header, 0) ==
                        XHeaderViewResizeMode_Interactive,
                    "单段模式覆写、无覆写段回落全局");
        XHeaderView_setSectionResizeModeAt(&header, 1, -1);
        XAPI_EXPECT(XHeaderView_sectionResizeModeAt(&header, 1) ==
                    XHeaderViewResizeMode_Interactive,
                    "-1 哨兵清除覆写恢复跟随全局");
        XHeaderView_setSectionResizeMode(&header, XHeaderViewResizeMode_Stretch);
        XAPI_EXPECT(XHeaderView_sectionResizeMode(&header) ==
                    XHeaderViewResizeMode_Stretch,
                    "全局模式设置为 Stretch");
        XHeaderView_setSectionResizeMode(&header, 99);
        XAPI_EXPECT(XHeaderView_sectionResizeMode(&header) ==
                    XHeaderViewResizeMode_Stretch,
                    "全局模式越界值忽略");
        XHeaderView_setSectionResizeMode(&header,
                                         XHeaderViewResizeMode_Interactive);
        /* ---- 排序指示器（对标 setSortIndicator/Shown/Clearable） ---- */
        vsig_reset();
        XHeaderView_setSortIndicator(&header, 1,
                                     XHeaderViewSortOrder_Descending);
        XAPI_EXPECT(XHeaderView_sortIndicatorSection(&header) == 1 &&
                    XHeaderView_sortIndicatorOrder(&header) ==
                        XHeaderViewSortOrder_Descending &&
                    XHeaderView_isSortIndicatorShown(&header),
                    "setSortIndicator 置位并显示指示器");
        XAPI_EXPECT(g_sig.hvSortIndicator == 1 && g_sig.hvSortSection == 1,
                    "排序指示器变化发射 sortIndicatorChanged(1)");
        XHeaderView_setSortIndicator(&header, 1,
                                     XHeaderViewSortOrder_Descending);
        XAPI_EXPECT(g_sig.hvSortIndicator == 1,
                    "同段同序重复设置不重复发射");
        XHeaderView_setSortIndicatorShown(&header, false);
        XAPI_EXPECT(!XHeaderView_isSortIndicatorShown(&header),
                    "setSortIndicatorShown(false) 仅隐指示器");
        vsig_reset();
        XHeaderView_setSortIndicatorClearable(&header, true);
        XAPI_EXPECT(XHeaderView_isSortIndicatorClearable(&header) &&
                    g_sig.hvClearable == 1 && g_sig.hvClearableOn == 1,
                    "clearable 变化发射 sortIndicatorClearableChanged(true)");
        XHeaderView_setSortIndicatorClearable(&header, true);
        XAPI_EXPECT(g_sig.hvClearable == 1,
                    "同值重复设置不重复发射");
        XHeaderView_setSortIndicatorClearable(&header, false);
        /* ---- 偏移承载（对标 setOffset 族；滚动联动未接） ---- */
        XHeaderView_setOffset(&header, 120);
        XAPI_EXPECT(XHeaderView_offset(&header) == 120,
                    "setOffset 存储承载往返");
        XHeaderView_setOffsetToSectionPosition(&header, 2);
        XAPI_EXPECT(XHeaderView_offset(&header) ==
                    XHeaderView_sectionPosition(&header, 2),
                    "setOffsetToSectionPosition 偏移=段起点");
        XHeaderView_setOffsetToLastSection(&header);
        XAPI_EXPECT(XHeaderView_offset(&header) == XHeaderView_length(&header),
                    "setOffsetToLastSection 退化为 length（视口未接）");
        XHeaderView_setModel(&header, NULL);
        XHeaderView_doItemsLayout(&header);
        XAPI_EXPECT(true, "setModel/doItemsLayout 调用无崩溃（承载/重绘）");
        /* ---- reset（对标 reset 就地恢复缺省） ---- */
        XHeaderView_hideSection(&header, 0);
        XHeaderView_setSortIndicator(&header, 0, XHeaderViewSortOrder_Descending);
        vsig_reset();
        XHeaderView_reset(&header);
        XAPI_EXPECT(XHeaderView_sectionSize(&header, 0) ==
                        XHeaderView_defaultSectionSize(&header) &&
                    XHeaderView_sectionSize(&header, 2) ==
                        XHeaderView_defaultSectionSize(&header),
                    "reset 全部段恢复 defaultSectionSize");
        XAPI_EXPECT(XHeaderView_hiddenSectionCount(&header) == 0 &&
                    XHeaderView_sortIndicatorSection(&header) == -1 &&
                    !XHeaderView_isSortIndicatorShown(&header),
                    "reset 取消隐藏并清空排序指示器");
        XAPI_EXPECT(g_sig.hvGeometries == 1,
                    "reset 有段时发射一次 geometriesChanged");

        /* ---- 状态持久化（对标 saveState/restoreState） ---- */
#if XByteArray_ON
        {
            XHeaderView_init(&donor, NULL, 0, 0);
            XHeaderView_init(&acceptor, NULL, 0, 0);
            XHeaderView_setCount(&donor, 3);
            XHeaderView_setSectionSize(&donor, 0, 30);
            XHeaderView_setSectionSize(&donor, 1, 90);
            XHeaderView_setSectionSize(&donor, 2, 60);
            XHeaderView_hideSection(&donor, 2);
            XHeaderView_setSortIndicator(&donor, 1,
                                         XHeaderViewSortOrder_Descending);
            XHeaderView_setStretchLastSection(&donor, true);
            {
                XByteArray* state = XHeaderView_saveState(&donor);
                XByteArray* bad = XByteArray_create_utf8("垃圾数据");
                XAPI_EXPECT(state != NULL,
                            "saveState 产出状态字节串");
                XAPI_EXPECT(!XHeaderView_restoreState(&acceptor, bad),
                            "魔数校验：非法状态整体拒绝");
                XAPI_EXPECT(XHeaderView_restoreState(&acceptor, state),
                            "restoreState 接受合法状态");
                XAPI_EXPECT(XHeaderView_count(&acceptor) == 3 &&
                            XHeaderView_sectionSize(&acceptor, 1) == 90 &&
                            XHeaderView_isSectionHidden(&acceptor, 2),
                            "段数/尺寸/隐藏状态恢复");
                XAPI_EXPECT(XHeaderView_sortIndicatorSection(&acceptor) == 1 &&
                            XHeaderView_sortIndicatorOrder(&acceptor) ==
                                XHeaderViewSortOrder_Descending &&
                            XHeaderView_stretchLastSection(&acceptor),
                            "指示器/末尾拉伸恢复");
                XAPI_EXPECT(!XHeaderView_restoreState(&vertical, state),
                            "方向不符整体拒绝（水平态入垂直头）");
                if (state) XByteArray_delete_base((XClass*)state);
                if (bad) XByteArray_delete_base((XClass*)bad);
            }
            XHeaderView_deinit_base(&donor);
            XHeaderView_deinit_base(&acceptor);
        }
#endif /* XByteArray_ON */

        XHeaderView_deinit_base(&header);
        XHeaderView_deinit_base(&vertical);
    }

#else /* XWIDGET_ON && XTABLEWIDGET_ON */

/* 条目视图族整体裁剪时的非空翻译单元哨兵。 */
typedef int xgui_demo_apitest_views_disabled_sentinel;

#endif /* XWIDGET_ON && XTABLEWIDGET_ON */

    XPrintf("XGuiApiTest: [条目视图族 views] %s\n",
            failures == 0 ? "PASS" : "FAIL");
    return failures;
}
