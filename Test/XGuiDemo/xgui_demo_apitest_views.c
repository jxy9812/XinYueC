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
#include "XObject_Protected.h"

#include <string.h>

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
/* [三态契约适配 2026-11-05] XString* 堆拷贝 getter 的断言垫片：取值
 * 比对后立即释放（调用方释放契约）。此前 strcmp 直接把 XString 对象
 * 指针当 char* 解——读到对象头乱码且每次调用泄漏一个 XString。 */
static bool xapi_strEq(XString* owned, const char* expectUtf8)
{
    /* [空串保护 2026-11-05] XString_toUtf8 对空内容返回 NULL（既有
     * 契约）——strcmp(NULL,...) 段错误，与 xapi_u8 垫片同款兜底。 */
    const char* u = owned ? XString_toUtf8(owned) : NULL;
    const char* expect = expectUtf8 ? expectUtf8 : "";
    bool ok = u ? strcmp(u, expect) == 0
                : (expect[0] == '\0');
    if (owned) XClassDelete((XClass*)owned);
    return ok;
}


/* ===== [Qt 对齐迁移 2026-11-03] XTableWidget 文本便捷 helper =====
 * 旧 API XTableWidget_setText/XTableWidget_text 已删除（Qt 无对应物）。
 * Qt 等价写法：item->setText()/item->text()；空格需先创建条目。
 * 此处提供两个文件内 static helper，集中承担「空格自动建条目」这一
 * C 侧便捷语义，避免逐点展开样板代码。 */
#ifndef XTW_TEXT_HELPER_DEFINED
#define XTW_TEXT_HELPER_DEFINED
static void xTwSetTextUtf8(XTableWidget* tbl, int row, int col,
                           const char* utf8)
{
    XTableWidgetItem* item = XTableWidget_item(tbl, row, col);
    if (!item) {
        item = XTableWidgetItem_create_utf8(utf8, XTableWidgetItem_Type);
        if (!item) return;
        XTableWidget_setItem(tbl, row, col, item);
        return;
    }
    XTableWidgetItem_setText_utf8(item, utf8);
}
static const char* xTwTextUtf8(const XTableWidget* tbl, int row, int col)
{
    return XTableWidgetItem_text_toUtf8(XTableWidget_item(tbl, row, col));
}
#endif /* XTW_TEXT_HELPER_DEFINED */

#include "XAbstractScrollArea.h"
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
    int rowsInserted;          /**< rowsInserted(first,last) 次数（闭区间）。 */
    int riRow;                 /**< 最近 rowsInserted 首行 first。 */
    int riCount;               /**< 最近 rowsInserted 末行 last（闭区间载荷）。 */
    int rowsRemoved;           /**< rowsRemoved(first,last) 次数（闭区间）。 */
    int rrRow;                 /**< 最近 rowsRemoved 首行 first。 */
    int rrCount;               /**< 最近 rowsRemoved 末行 last（闭区间载荷）。 */
    int headerDataChanged;     /**< headerDataChanged(ori,first,last) 次数。 */
    int hdcOrientation;        /**< 最近 headerDataChanged 方向。 */
    int hdcFirst;              /**< 最近 headerDataChanged 首段。 */
    int hdcLast;               /**< 最近 headerDataChanged 末段。 */
    int columnsInserted;       /**< columnsInserted(first,last) 次数（闭区间）。 */
    int ciCol;                 /**< 最近 columnsInserted 首列 first。 */
    int ciCount;               /**< 最近 columnsInserted 末列 last。 */
    int columnsRemoved;        /**< columnsRemoved(first,last) 次数（闭区间）。 */
    int crCol;                 /**< 最近 columnsRemoved 首列 first。 */
    int crCount;               /**< 最近 columnsRemoved 末列 last。 */
    int layoutChanged;         /**< layoutChanged 次数。 */
    /* XAbstractItemView 信号族。 */
    int iconSizeChanged;       /**< iconSizeChanged(w,h) 次数。 */
    int icW;                   /**< 最近 iconSizeChanged 宽。 */
    int icH;                   /**< 最近 iconSizeChanged 高。 */
    /* XItemSelectionModel 信号族（对标 QItemSelectionModel）。 */
    int ismCurrentChanged;     /**< currentChanged(cur,prev) 次数。 */
    int ismRow;                /**< 最近 currentChanged 当前行。 */
    int ismCol;                /**< 最近 currentChanged 当前列。 */
    int ismPrevRow;            /**< 最近 currentChanged 前行。 */
    int ismPrevCol;            /**< 最近 currentChanged 前列。 */
    int ismRowChanged;         /**< currentRowChanged(cur,prev) 次数。 */
    int ismColChanged;         /**< currentColumnChanged(cur,prev) 次数。 */
    /* XListView 信号族。 */
    int indexesMoved;          /**< indexesMoved(rows,count) 次数。 */
    int imCount;               /**< 最近 indexesMoved 行数。 */
    /* XListWidget 信号族。 */
    int lwCurrentChanged;      /**< currentItemChanged(cur,prev) 次数。 */
    int lwCur;                 /**< 最近 currentItemChanged 当前行。 */
    int lwPrev;                /**< 最近 currentItemChanged 前行。 */
    int lwRowChanged;          /**< currentRowChanged(cur) 次数（单载荷）。 */
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
    int tvColResized;          /**< columnResized(col,old,new) 次数。 */
    int tvCRCol;               /**< 最近 columnResized 列号。 */
    int tvCROld;               /**< 最近 columnResized 原宽。 */
    int tvCRNew;               /**< 最近 columnResized 新宽。 */
    int tvColCountChanged;     /**< columnCountChanged(old,new) 次数。 */
    int tvCCOld;               /**< 最近 columnCountChanged 原列数。 */
    int tvCCNew;               /**< 最近 columnCountChanged 新列数。 */
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
    int hvHeaderData;          /**< headerDataChanged(section) 次数。 */
    int hvHDSection;           /**< 最近 headerDataChanged 段。 */
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
    int tblCellActivated;      /**< cellActivated(row,col) 次数。 */
    int tblItemActivated;      /**< itemActivated(item) 次数。 */
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
    /* 对标 Qt rowsInserted(parent,first,last)：本库信号去掉 parent，
     * 两载荷为闭区间 first..last（不是 row,count）。 */
    XVarList_args_2(args, int, first, int, last);
    ++g_sig.rowsInserted;
    g_sig.riRow = first;
    g_sig.riCount = last;
}

static void vw_modelRowsRemovedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    /* 对标 Qt rowsRemoved(parent,first,last)：闭区间 first..last。 */
    XVarList_args_2(args, int, first, int, last);
    ++g_sig.rowsRemoved;
    g_sig.rrRow = first;
    g_sig.rrCount = last;
}

static void vw_modelHeaderDataChangedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_3(args, int, orientation, int, first, int, last);
    ++g_sig.headerDataChanged;
    g_sig.hdcOrientation = orientation;
    g_sig.hdcFirst = first;
    g_sig.hdcLast = last;
}

static void vw_modelColumnsInsertedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    /* 对标 Qt columnsInserted(parent,first,last)：两载荷为闭区间。 */
    XVarList_args_2(args, int, first, int, last);
    ++g_sig.columnsInserted;
    g_sig.ciCol = first;
    g_sig.ciCount = last;
}

static void vw_modelColumnsRemovedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    /* 对标 Qt columnsRemoved(parent,first,last)：两载荷为闭区间。 */
    XVarList_args_2(args, int, first, int, last);
    ++g_sig.columnsRemoved;
    g_sig.crCol = first;
    g_sig.crCount = last;
}

static void vw_modelLayoutChangedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    ++g_sig.layoutChanged;
}

static void vw_ismCurrentChangedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_4(args, int, row, int, col, int, prevRow, int, prevCol);
    ++g_sig.ismCurrentChanged;
    g_sig.ismRow = row;
    g_sig.ismCol = col;
    g_sig.ismPrevRow = prevRow;
    g_sig.ismPrevCol = prevCol;
}

static void vw_ismRowChangedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_2(args, int, row, int, prevRow);
    ++g_sig.ismRowChanged;
    g_sig.ismRow = row;
    g_sig.ismPrevRow = prevRow;
}

static void vw_ismColChangedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_2(args, int, col, int, prevCol);
    ++g_sig.ismColChanged;
    g_sig.ismCol = col;
    g_sig.ismPrevCol = prevCol;
}

/** @brief 模型信号统一装配（sender==receiver 自连定式；含表头/列结构/
 *        布局信号，供结构操作与表头联动断言共用）。 */
static void vsig_connectModel(XObject* sender)
{
    XObject_connect(sender, XSignal(XAbstractItemModel_dataChanged_signal),
                      sender, vw_modelDataChangedSlot, XConnectionType_Direct);
    XObject_connect(sender, XSignal(XAbstractItemModel_rowsInserted_signal),
                      sender, vw_modelRowsInsertedSlot, XConnectionType_Direct);
    XObject_connect(sender, XSignal(XAbstractItemModel_rowsRemoved_signal),
                      sender, vw_modelRowsRemovedSlot, XConnectionType_Direct);
    XObject_connect(sender,
                      XSignal(XAbstractItemModel_headerDataChanged_signal),
                      sender, vw_modelHeaderDataChangedSlot,
                      XConnectionType_Direct);
    XObject_connect(sender,
                      XSignal(XAbstractItemModel_columnsInserted_signal),
                      sender, vw_modelColumnsInsertedSlot,
                      XConnectionType_Direct);
    XObject_connect(sender,
                      XSignal(XAbstractItemModel_columnsRemoved_signal),
                      sender, vw_modelColumnsRemovedSlot,
                      XConnectionType_Direct);
    XObject_connect(sender,
                      XSignal(XAbstractItemModel_layoutChanged_signal),
                      sender, vw_modelLayoutChangedSlot,
                      XConnectionType_Direct);
}

/** @brief 选择模型 current 族信号装配（sender==receiver 自连定式）。 */
static void vsig_connectSelectionModel(XObject* sender)
{
    XObject_connect(sender,
                      XSignal(XItemSelectionModel_currentChanged_signal),
                      sender, vw_ismCurrentChangedSlot,
                      XConnectionType_Direct);
    XObject_connect(sender,
                      XSignal(XItemSelectionModel_currentRowChanged_signal),
                      sender, vw_ismRowChangedSlot, XConnectionType_Direct);
    XObject_connect(sender,
                      XSignal(XItemSelectionModel_currentColumnChanged_signal),
                      sender, vw_ismColChangedSlot, XConnectionType_Direct);
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
    /* 对标 Qt 6.8 qlistwidget.h:242 currentRowChanged(int) 单载荷；
     * 旧写法按双载荷读 (current, previous) 会越界读第 2 个参数。 */
    XVarList_args_1(args, int, cur);
    (void)cur;
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
    XObject_connect(sender, XSignal(XListWidget_currentItemChanged_signal),
                      sender, vw_lwCurrentChangedSlot, XConnectionType_Direct);
    XObject_connect(sender, XSignal(XListWidget_currentRowChanged_signal),
                      sender, vw_lwRowChangedSlot, XConnectionType_Direct);
    XObject_connect(sender, XSignal(XListWidget_currentTextChanged_signal),
                      sender, vw_lwTextChangedSlot, XConnectionType_Direct);
    XObject_connect(sender, XSignal(XListWidget_itemSelectionChanged_signal),
                      sender, vw_lwSelChangedSlot, XConnectionType_Direct);
    XObject_connect(sender, XSignal(XListWidget_itemChanged_signal),
                      sender, vw_lwItemChangedSlot, XConnectionType_Direct);
    XObject_connect(sender, XSignal(XListWidget_itemClicked_signal),
                      sender, vw_lwClickedSlot, XConnectionType_Direct);
    XObject_connect(sender, XSignal(XListWidget_itemPressed_signal),
                      sender, vw_lwPressedSlot, XConnectionType_Direct);
    XObject_connect(sender, XSignal(XListWidget_itemDoubleClicked_signal),
                      sender, vw_lwDoubleClickedSlot, XConnectionType_Direct);
    XObject_connect(sender, XSignal(XListWidget_itemActivated_signal),
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

static void vw_tvColumnResizedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_3(args, int, column, int, oldSize, int, newSize);
    ++g_sig.tvColResized;
    g_sig.tvCRCol = column;
    g_sig.tvCROld = oldSize;
    g_sig.tvCRNew = newSize;
}

static void vw_tvColumnCountChangedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_2(args, int, oldCount, int, newCount);
    ++g_sig.tvColCountChanged;
    g_sig.tvCCOld = oldCount;
    g_sig.tvCCNew = newCount;
}

static void vw_hvHeaderDataSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_1(args, int, section);
    ++g_sig.hvHeaderData;
    g_sig.hvHDSection = section;
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
    XObject_connect(sender, XSignal(XTreeWidget_itemClicked_signal),
                      sender, vw_twItemClickedSlot, XConnectionType_Direct);
    XObject_connect(sender, XSignal(XTreeWidget_itemDoubleClicked_signal),
                      sender, vw_twItemDoubleClickedSlot, XConnectionType_Direct);
    XObject_connect(sender, XSignal(XTreeWidget_itemActivated_signal),
                      sender, vw_twItemActivatedSlot, XConnectionType_Direct);
    XObject_connect(sender, XSignal(XTreeWidget_itemEntered_signal),
                      sender, vw_twItemEnteredSlot, XConnectionType_Direct);
    XObject_connect(sender, XSignal(XTreeWidget_itemExpanded_signal),
                      sender, vw_twItemExpandedSlot, XConnectionType_Direct);
    XObject_connect(sender, XSignal(XTreeWidget_itemCollapsed_signal),
                      sender, vw_twItemCollapsedSlot, XConnectionType_Direct);
    XObject_connect(sender, XSignal(XTreeWidget_itemChanged_signal),
                      sender, vw_twItemChangedSlot, XConnectionType_Direct);
    XObject_connect(sender, XSignal(XTreeWidget_currentItemChanged_signal),
                      sender, vw_twCurChangedSlot, XConnectionType_Direct);
    XObject_connect(sender, XSignal(XTreeWidget_itemSelectionChanged_signal),
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
    XObject_connect(sender, XSignal(XHeaderView_sectionCountChanged_signal),
                      sender, vw_hvCountChangedSlot, XConnectionType_Direct);
    XObject_connect(sender, XSignal(XHeaderView_geometriesChanged_signal),
                      sender, vw_hvGeometriesSlot, XConnectionType_Direct);
    XObject_connect(sender, XSignal(XHeaderView_sortIndicatorChanged_signal),
                      sender, vw_hvSortIndicatorSlot, XConnectionType_Direct);
    XObject_connect(sender,
                      XSignal(XHeaderView_sortIndicatorClearableChanged_signal),
                      sender, vw_hvClearableSlot, XConnectionType_Direct);
    XObject_connect(sender, XSignal(XHeaderView_sectionResized_signal),
                      sender, vw_hvSectionResizedSlot, XConnectionType_Direct);
    XObject_connect(sender, XSignal(XHeaderView_sectionMoved_signal),
                      sender, vw_hvSectionMovedSlot, XConnectionType_Direct);
    XObject_connect(sender, XSignal(XHeaderView_headerDataChanged_signal),
                      sender, vw_hvHeaderDataSlot, XConnectionType_Direct);
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

static void vw_tblCellActivatedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_2(args, int, row, int, col);
    ++g_sig.tblCellActivated;
    g_sig.tblCCRow = row;
    g_sig.tblCCCol = col;
}

static void vw_tblItemActivatedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++g_sig.tblItemActivated;
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
    XObject_connect(sender, XSignal(XTableWidget_cellClicked_signal),
                      sender, vw_tblCellClickedSlot, XConnectionType_Direct);
    XObject_connect(sender, XSignal(XTableWidget_cellPressed_signal),
                      sender, vw_tblCellPressedSlot, XConnectionType_Direct);
    XObject_connect(sender, XSignal(XTableWidget_cellDoubleClicked_signal),
                      sender, vw_tblCellDoubleSlot, XConnectionType_Direct);
    XObject_connect(sender, XSignal(XTableWidget_currentCellChanged_signal),
                      sender, vw_tblCurCellSlot, XConnectionType_Direct);
    XObject_connect(sender,
                      XSignal(XTableWidget_itemSelectionChanged_signal),
                      sender, vw_tblSelChangedSlot, XConnectionType_Direct);
    XObject_connect(sender, XSignal(XTableWidget_cellChanged_signal),
                      sender, vw_tblCellChangedSlot, XConnectionType_Direct);
    XObject_connect(sender, XSignal(XTableWidget_cellActivated_signal),
                      sender, vw_tblCellActivatedSlot, XConnectionType_Direct);
    XObject_connect(sender, XSignal(XTableWidget_itemActivated_signal),
                      sender, vw_tblItemActivatedSlot, XConnectionType_Direct);
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
                    g_sig.riCount == 1,
                    "维度扩展发射 rowsInserted(0,1)（闭区间 first..last，"
                    "last=旧行数+新增-1；gap 133）");
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
        /* ---- 维度收缩：裁剪尾部 + rowsRemoved(first,last) ---- */
        XAbstractItemModel_setDimension(&model, 1, 1);
        XAPI_EXPECT(XAbstractItemModel_rowCount(&model) == 1 &&
                    XAbstractItemModel_columnCount(&model) == 1 &&
                    XAbstractItemModel_data(&model, 1, 2) == NULL,
                    "缩容到 1x1 后越界数据为 NULL");
        XAPI_EXPECT(g_sig.rowsRemoved == 1 && g_sig.rrRow == 1 &&
                    g_sig.rrCount == 1,
                    "缩容发射 rowsRemoved(first=1,last=1)");
        /* ---- 负值维度忽略（setDimension 负值无效） ---- */
        XAbstractItemModel_setDimension(
            &model, -1, XAbstractItemModel_columnCount(&model));
        XAPI_EXPECT(XAbstractItemModel_rowCount(&model) == 1,
                    "setDimension(-1,列) 负值忽略");
        /* ---- setDimension 补空列 ---- */
        XAbstractItemModel_setDimension(
            &model, XAbstractItemModel_rowCount(&model), 3);
        XAPI_EXPECT(XAbstractItemModel_columnCount(&model) == 3 &&
                    xapi_cstr(XAbstractItemModel_data_2(&model, 0, 2))[0] == '\0',
                    "setDimension(行,3) 补空列且新列为空");
        /* ---- setDimension 扩行（不足补空行） ---- */
        XAbstractItemModel_setDimension(
            &model, 4, XAbstractItemModel_columnCount(&model));
        XAPI_EXPECT(XAbstractItemModel_rowCount(&model) == 4 &&
                    xapi_cstr(XAbstractItemModel_data_2(&model, 3, 0))[0] == '\0',
                    "setDimension(4,列) 补空行");
        /* ---- flags（对标 QAbstractItemModel::flags 基类缺省：有效格
         *      Selectable|Enabled，无效格 NoItemFlags） ---- */
        XAPI_EXPECT(XAbstractItemModel_flags(&model, 0, 0) ==
                        (XItemFlag_ItemIsSelectable |
                         XItemFlag_ItemIsEnabled) &&
                    XAbstractItemModel_flags(&model, -1, 0) ==
                        XItemFlag_NoItemFlags &&
                    XAbstractItemModel_flags(&model, 9, 9) ==
                        XItemFlag_NoItemFlags,
                    "flags 有效格 Selectable|Enabled、越界格 NoItemFlags");
        /* ---- hasChildren（对标 hasChildren 的扁平收敛：行列均>0） ---- */
        XAPI_EXPECT(XAbstractItemModel_hasChildren(&model),
                    "4x3 模型 hasChildren 为 true");

        /* ---- 结构操作（对标 insertRows/removeRows/insertColumns/
         *      removeColumns；Qt 基类为虚占位，本内存模型为真实现） ---- */
        XAbstractItemModel_setDimension(&model, 2, 2);
        XAbstractItemModel_setData_2(&model, 0, 0, "r0c0");
        XAbstractItemModel_setData_2(&model, 0, 1, "r0c1");
        XAbstractItemModel_setData_2(&model, 1, 0, "r1c0");
        XAbstractItemModel_setData_2(&model, 1, 1, "r1c1");
        vsig_reset();
        XAPI_EXPECT(XAbstractItemModel_insertRows(&model, 1, 2) &&
                    XAbstractItemModel_rowCount(&model) == 4,
                    "insertRows(1,2) 后 4 行");
        XAPI_EXPECT(g_sig.rowsInserted == 1 && g_sig.riRow == 1 &&
                    g_sig.riCount == 2,
                    "insertRows(1,2) 发射 rowsInserted(first=1,last=2)");
        XAPI_EXPECT(strcmp(xapi_cstr(XAbstractItemModel_data_2(&model, 0, 0)),
                           "r0c0") == 0 &&
                    strcmp(xapi_cstr(XAbstractItemModel_data_2(&model, 3, 1)),
                           "r1c1") == 0,
                    "插入行后原数据整体后移（原 1 行 → 3 行）");
        XAPI_EXPECT(xapi_cstr(XAbstractItemModel_data_2(&model, 1, 0))[0] == '\0',
                    "插入的新行为空行");
        XAPI_EXPECT(!XAbstractItemModel_insertRows(&model, -1, 1) &&
                    !XAbstractItemModel_insertRows(&model, 0, 0),
                    "insertRows 负位置/零行数拒绝");
        vsig_reset();
        XAPI_EXPECT(XAbstractItemModel_removeRows(&model, 1, 2) &&
                    XAbstractItemModel_rowCount(&model) == 2,
                    "removeRows(1,2) 后回到 2 行");
        XAPI_EXPECT(g_sig.rowsRemoved == 1 && g_sig.rrRow == 1 &&
                    g_sig.rrCount == 2,
                    "removeRows 发射 rowsRemoved(first=1,last=2)");
        XAPI_EXPECT(strcmp(xapi_cstr(XAbstractItemModel_data_2(&model, 1, 1)),
                           "r1c1") == 0,
                    "移除中间行后原末行数据回到 1 行");
        vsig_reset();
        XAPI_EXPECT(XAbstractItemModel_insertColumns(&model, 1, 1) &&
                    XAbstractItemModel_columnCount(&model) == 3,
                    "insertColumns(1,1) 后 3 列");
        XAPI_EXPECT(g_sig.columnsInserted == 1 && g_sig.ciCol == 1 &&
                    g_sig.ciCount == 1,
                    "insertColumns 发射 columnsInserted(first=1,last=1)");
        XAPI_EXPECT(strcmp(xapi_cstr(XAbstractItemModel_data_2(&model, 0, 0)),
                           "r0c0") == 0 &&
                    strcmp(xapi_cstr(XAbstractItemModel_data_2(&model, 0, 2)),
                           "r0c1") == 0,
                    "插入列后原第 1 列数据右移到第 2 列");
        XAPI_EXPECT(xapi_cstr(XAbstractItemModel_data_2(&model, 0, 1))[0] == '\0',
                    "插入的新列为空列");
        vsig_reset();
        XAPI_EXPECT(XAbstractItemModel_removeColumns(&model, 1, 1) &&
                    XAbstractItemModel_columnCount(&model) == 2,
                    "removeColumns(1,1) 后回到 2 列");
        XAPI_EXPECT(g_sig.columnsRemoved == 1 && g_sig.crCol == 1 &&
                    g_sig.crCount == 1,
                    "removeColumns 发射 columnsRemoved(first=1,last=1)");
        /* ---- sort（对标 QAbstractItemModel::sort：Qt 基类为空实现、
         *      由模型覆写，本内存模型按列文本重排并发射 layoutChanged） */
        XAbstractItemModel_setDimension(&model, 3, 1);
        XAbstractItemModel_setData_2(&model, 0, 0, "pear");
        XAbstractItemModel_setData_2(&model, 1, 0, "apple");
        XAbstractItemModel_setData_2(&model, 2, 0, "cherry");
        vsig_reset();
        XAbstractItemModel_sort(&model, 0, 0);
        XAPI_EXPECT(strcmp(xapi_cstr(XAbstractItemModel_data_2(&model, 0, 0)),
                           "apple") == 0 &&
                    strcmp(xapi_cstr(XAbstractItemModel_data_2(&model, 1, 0)),
                           "cherry") == 0 &&
                    strcmp(xapi_cstr(XAbstractItemModel_data_2(&model, 2, 0)),
                           "pear") == 0,
                    "sort(0,升序) 按列文本字典序重排");
        XAPI_EXPECT(g_sig.layoutChanged == 1,
                    "sort 发射 layoutChanged");
        XAbstractItemModel_sort(&model, 0, 1);
        XAPI_EXPECT(strcmp(xapi_cstr(XAbstractItemModel_data_2(&model, 0, 0)),
                           "pear") == 0 &&
                    strcmp(xapi_cstr(XAbstractItemModel_data_2(&model, 2, 0)),
                           "apple") == 0,
                    "sort(0,降序) 反向重排");
        XAbstractItemModel_sort(&model, 9, 0);
        XAPI_EXPECT(strcmp(xapi_cstr(XAbstractItemModel_data_2(&model, 0, 0)),
                           "pear") == 0,
                    "sort 越界列忽略");
        /* ---- 空模型 hasChildren（扁平收敛的假分支） ---- */
        XAbstractItemModel_setDimension(&model, 0, 0);
        XAPI_EXPECT(!XAbstractItemModel_hasChildren(&model),
                    "0x0 模型 hasChildren 为 false");
        /* ---- headerDataChanged（对标 QAbstractItemModel::
         *      headerDataChanged 信号 qabstractitemmodel.h:358：
         *      setHeaderData 落库成功即发射，驱动表头重绘链路） ---- */
        XAbstractItemModel_setDimension(&model, 1, 2);
        vsig_reset();
        XAbstractItemModel_setHeaderData_2(&model, 1, 0, "列B");
        XAPI_EXPECT(g_sig.headerDataChanged == 1 &&
                    g_sig.hdcOrientation == 0 &&
                    g_sig.hdcFirst == 1 && g_sig.hdcLast == 1,
                    "setHeaderData 发射 headerDataChanged(0,1,1)");
        XAbstractItemModel_setHeaderData_2(&model, 0, 1, "行A");
        XAPI_EXPECT(g_sig.headerDataChanged == 2 &&
                    g_sig.hdcOrientation == 1 &&
                    g_sig.hdcFirst == 0 && g_sig.hdcLast == 0,
                    "行头变化发射 headerDataChanged(1,0,0)");
        XAbstractItemModel_setHeaderData_2(&model, 9, 0, "越界");
        XAPI_EXPECT(g_sig.headerDataChanged == 2,
                    "越界 setHeaderData 拒绝且不发 headerDataChanged");

        XClassDeinit(&model);
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
        XObject_connect((XObject*)&view,
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
        {
            int iw16 = 0, ih16 = 0;
            XAbstractItemView_iconSize(&view, &iw16, &ih16);
            XAPI_EXPECT(iw16 == 16 && ih16 == 16,
                        "默认 iconSize=16x16（本库缺省）");
        }
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
        {
            int rootR = 0, rootC = 0;
            XAbstractItemView_rootIndex(&view, &rootR, &rootC);
            XAPI_EXPECT(rootR == -1 && rootC == -1,
                        "初始根索引无效（-1）");
        }

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
        XAbstractItemView_iconSize(&view, &w, &h);
        XAPI_EXPECT(w == 24 && h == 28,
                    "setIconSize(24,28) 往返");
        XAPI_EXPECT(w == 24 && h == 28,
                    "iconSize 双输出查询与单项 getter 同源");
        XAPI_EXPECT(g_sig.iconSizeChanged == 1 && g_sig.icW == 24 &&
                    g_sig.icH == 28,
                    "图标尺寸变化发射 iconSizeChanged(24,28)");
        XAbstractItemView_setIconSize(&view, 0, 0);
        XAbstractItemView_setIconSize(&view, 24, 28);
        XAPI_EXPECT(g_sig.iconSizeChanged == 1,
                    "非正尺寸拒绝、同值重复设置不重复发信号");

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
        /* Qt 6.8 QAbstractItemView::selectAll：SingleSelection 分支
         * 直接 break（qabstractitemview.cpp:1214-1215）——单选模式
         * 全选**什么都不做**（既不选当前项也不清选区）。 */
        XAPI_EXPECT(XAbstractItemView_selectionModel(&view) != NULL &&
                    XItemSelectionModel_selectedCount(
                        XAbstractItemView_selectionModel(&view)) == 0,
                    "单选模式 selectAll 为空操作（Qt qabstractitemview.cpp:1214-1215）");
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
        {
            int rr = 0, rc = 0;
            XAbstractItemView_rootIndex(&view, &rr, &rc);
            XAPI_EXPECT(XAbstractItemView_currentRow(&view) == -1 &&
                        rr == -1 && rc == -1,
                        "reset 复位当前索引与根索引");
        }
        XAPI_EXPECT(XItemSelectionModel_selectedCount(
                        XAbstractItemView_selectionModel(&view)) == 0,
                    "reset 清空全部选中");
        /* ---- 根索引预留状态（rootIndex 平铺恒 (0,0)——头文件口径） ---- */
        XAbstractItemView_setRootIndex(&view, 2, 1);
        {
            int sr = 0, sc = 0;
            XAbstractItemView_rootIndex(&view, &sr, &sc);
            XAPI_EXPECT(sr == 2 && sc == 1,
                        "setRootIndex 存储根偏移（树预留）");
        }
        r = -9;
        c = -9;
        /* Qt 6.8 QAbstractItemView::rootIndex 返回**存储的**根索引
         * （qabstractitemview.cpp:1187-1190），缺省为无效索引；
         * 本库平铺承载为 (-1,-1) 表示无效。 */
        XAPI_EXPECT(XAbstractItemView_rootIndex(&view, &r, &c) &&
                    r == 2 && c == 1,
                    "rootIndex 返回存储的根偏移（Qt rootIndex 语义）");
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
            if (iw) XClassDelete(iw);
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
        XAPI_EXPECT(XAbstractItemView_keyboardSearch(&view, "ga") &&
                    XAbstractItemView_currentRow(&view) == 2,
                    "keyboardSearch(\"ga\") 前缀命中 gamma（行 2）");
        XAPI_EXPECT(!XAbstractItemView_keyboardSearch(&view, ""),
                    "keyboardSearch(\"\") 仅重置前缀并返回 false");
        XAbstractItemView_setCurrentIndex(&view, 2, 0);
        XAPI_EXPECT(XAbstractItemView_keyboardSearch(&view, "be") &&
                    XAbstractItemView_currentRow(&view) == 1,
                    "累积前缀无命中回退本次文本并环形命中 beta");

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

        if (model) XClassDelete(model);
        XClassDeinit(&view);
    }

    /* ================================================================
     * 2b. XItemSelectionModel 选择模型补齐面（current 信号族/区间选择/
     *     整行整列/清空区分/模型绑定）+ 视图 MultiSelection Toggle
     *     语义（对标 Qt setCurrentIndex → selectionCommand(nullptr)）。
     * ================================================================ */
    {
        XItemSelectionModel* sm = XItemSelectionModel_create();
        XAbstractItemModel* model = XAbstractItemModel_create();

        vsig_reset();
        if (model) XAbstractItemModel_setDimension(model, 3, 4);
        /* 对齐 Qt 6.8：未绑定模型的选择模型 select/setCurrentIndex 均为
         * 空操作（qitemselectionmodel.cpp:1284-1290/1389-1395，gap 164），
         * 故先绑定模型再验 current/selection 行为。 */
        if (sm) XItemSelectionModel_setModel(sm, model);
        if (sm) vsig_connectSelectionModel((XObject*)sm);

        /* ---- setCurrentIndex 的 current 族发射（对标
         *      QItemSelectionModel::setCurrentIndex：currentChanged 先
         *      行、行/列分别变化再发射 row/column 版，载荷为当前/前值） */
        XItemSelectionModel_setCurrentIndex(sm, 2, 3,
                                        XItemSelectionModelSelectionFlag_NoUpdate);
        XAPI_EXPECT(g_sig.ismCurrentChanged == 1 && g_sig.ismRow == 2 &&
                    g_sig.ismCol == 3 && g_sig.ismPrevRow == -1 &&
                    g_sig.ismPrevCol == -1,
                    "setCurrentIndex(2,3) 发射 currentChanged(2,3,-1,-1)");
        XAPI_EXPECT(g_sig.ismRowChanged == 1 && g_sig.ismRow == 2 &&
                    g_sig.ismPrevRow == -1,
                    "行变化发射 currentRowChanged(2,-1)");
        XAPI_EXPECT(g_sig.ismColChanged == 1 && g_sig.ismCol == 3 &&
                    g_sig.ismPrevCol == -1,
                    "列变化发射 currentColumnChanged(3,-1)");
        XItemSelectionModel_setCurrentIndex(sm, 2, 3,
                                        XItemSelectionModelSelectionFlag_NoUpdate);
        XAPI_EXPECT(g_sig.ismCurrentChanged == 1 && g_sig.ismRowChanged == 1 &&
                    g_sig.ismColChanged == 1,
                    "同格重设不发射（索引未变化无操作，同 Qt）");
        XItemSelectionModel_setCurrentIndex(sm, 2, 1,
                                        XItemSelectionModelSelectionFlag_NoUpdate);
        XAPI_EXPECT(g_sig.ismCurrentChanged == 2 && g_sig.ismRowChanged == 1 &&
                    g_sig.ismColChanged == 2 && g_sig.ismCol == 1 &&
                    g_sig.ismPrevCol == 3,
                    "仅列变化发射 currentChanged+currentColumnChanged");
        /* ---- selectRange（对标 select(QItemSelectionRange, Select)：
         *      区间全格置选、单次 selectionChanged 语义；本库以
         *      selectedCount 差分承载单次发射判定） ---- */
        vsig_reset();
        XAPI_EXPECT(XItemSelectionModel_selectSelection(sm, 0, 1, 2, 2,
                                              XItemSelectionModelSelectionFlag_Select) &&
                    XItemSelectionModel_selectedCount(sm) == 6,
                    "selectRange(0,1,2,2) 选中 3 行 x 2 列 = 6 格");
        XAPI_EXPECT(XItemSelectionModel_isSelected(sm, 0, 2) &&
                    XItemSelectionModel_isSelected(sm, 2, 1) &&
                    !XItemSelectionModel_isSelected(sm, 0, 0),
                    "区间端点选中、区间外未选");
        XAPI_EXPECT(!XItemSelectionModel_selectSelection(sm, 2, 2, 0, 1,
                                               XItemSelectionModelSelectionFlag_Select) &&
                    XItemSelectionModel_selectedCount(sm) == 6,
                    "逆序区间归一后为幂等重选（集合不变返回 false）");
        XAPI_EXPECT(XItemSelectionModel_hasSelection(sm),
                    "hasSelection 有选中为 true");
        /* ---- clearSelection 保留当前索引（对标 clearSelection 与
         *      clear 的语义区分：clear 同 Qt 清当前索引的既有收敛保留
         *      ——本库 clear 沿用不清当前的历史行为，见头文件注） ---- */
        XItemSelectionModel_setCurrentIndex(sm, 1, 1,
                                        XItemSelectionModelSelectionFlag_NoUpdate);
        vsig_reset();
        XAPI_EXPECT(XItemSelectionModel_clearSelection(sm) &&
                    XItemSelectionModel_selectedCount(sm) == 0,
                    "clearSelection 清空选择集合");
        {
            int curRow = -9;
            int curCol = -9;
            XAPI_EXPECT(XItemSelectionModel_currentIndex(sm, &curRow, &curCol) &&
                        curRow == 1 && curCol == 1,
                        "clearSelection 保留当前索引（同 Qt clearSelection）");
        }
        XAPI_EXPECT(!XItemSelectionModel_hasSelection(sm),
                    "清空后 hasSelection 为 false");
        /* 前段为验 current/selection 已绑定模型；此处先解绑，恢复「未绑定
         * 模型时整行/列选择拒绝」的 Qt 前置（gap 164）。 */
        XItemSelectionModel_setModel(sm, NULL);
        /* ---- 模型绑定 + 整行/列选择（P2-41/42 迁移：QItemSelectionModel
         *      无 selectRow/selectColumn（Qt 属 QTableView），改走
         *      selectSelection(整行 = (row,0,row,cols-1)、整列 =
         *      (0,col,rows-1,col)）；夹具模型为 3 行 × 4 列） */
        XAPI_EXPECT(!XItemSelectionModel_selectSelection(
                        sm, 0, 0, 0,
                        XAbstractItemModel_columnCount(model) - 1,
                        XItemSelectionModelSelectionFlag_Select),
                    "未绑定模型整行选择拒绝");
        XItemSelectionModel_setModel(sm, model);
        XAPI_EXPECT(XItemSelectionModel_model(sm) == model,
                    "setModel/model 绑定往返");
        vsig_reset();
        XAPI_EXPECT(XItemSelectionModel_selectSelection(
                        sm, 1, 0, 1,
                        XAbstractItemModel_columnCount(model) - 1,
                        XItemSelectionModelSelectionFlag_Select) &&
                    XItemSelectionModel_selectedCount(sm) == 4,
                    "行 1 整行选择（selectSelection 形）选中整行 4 格");
        XAPI_EXPECT(XItemSelectionModel_isSelected(sm, 1, 3) &&
                    !XItemSelectionModel_isSelected(sm, 0, 0),
                    "整行选择覆盖该行全部列");
        vsig_reset();
        XAPI_EXPECT(XItemSelectionModel_selectSelection(
                        sm, 0, 2, XAbstractItemModel_rowCount(model) - 1, 2,
                        XItemSelectionModelSelectionFlag_Select) &&
                    XItemSelectionModel_selectedCount(sm) == 4 + 3 - 1,
                    "列 2 整列选择追加（交格去重，共 6 格）");
        /* 注：原「selectRow 越界拒绝」断言已删——越界门控随
         * XItemSelectionModel_selectRow 一并移除（QItemSelectionModel 侧
         * 无此入口）；selectSelection 不做模型维度钳制（行/列越界由
         * QTableView::selectRow 承担，Qt 同）。 */
        XItemSelectionModel_setModel(sm, NULL);
        XAPI_EXPECT(XItemSelectionModel_model(sm) == NULL,
                    "setModel(NULL) 解除绑定");

        if (model) XClassDelete(model);
        if (sm) XClassDelete((XClass*)sm);
    }

    /* ================================================================
     * 2c. 视图 MultiSelection 选择命令分派（对标 Qt 6.8.3
     *     QAbstractItemView::setCurrentIndex → selectionCommand(index,
     *     nullptr)：MultiSelection 走 multiSelectionCommand 尾段 Toggle
     *     |behaviorFlags（qabstractitemview.cpp:4183-4229），其余模式
     *     ClearAndSelect）与 update(index) 局部刷新入口。
     * ================================================================ */
    {
        XAbstractItemView view;
        XAbstractItemModel* model = XAbstractItemModel_create();

        vsig_reset();
        XAbstractItemView_init(&view, NULL, 0);
        if (model) {
            XAbstractItemModel_setDimension(model, 3, 3);
            XAbstractItemView_setModel(&view, model);
        }

        /* ---- MultiSelection：setCurrentIndex 为 Toggle 语义 ---- */
        XAbstractItemView_setSelectionMode(
            &view, XAbstractItemViewSelectionMode_MultiSelection);
        XItemSelectionModel_select(XAbstractItemView_selectionModel(&view),
                                   1, 1, XItemSelectionModelSelectionFlag_Select);
        XAbstractItemView_setCurrentIndex(&view, 1, 1);
        {
            XItemSelectionModel* smv = XAbstractItemView_selectionModel(&view);
            XAPI_EXPECT(!XItemSelectionModel_isSelected(smv, 1, 1),
                        "MultiSelection setCurrentIndex 对已选项 Toggle 取消");
            XAbstractItemView_setCurrentIndex(&view, 2, 2);
            XItemSelectionModel_select(smv, 1, 1,
                                   XItemSelectionModelSelectionFlag_Select);
            XAPI_EXPECT(XItemSelectionModel_isSelected(smv, 1, 1) &&
                        XItemSelectionModel_isSelected(smv, 2, 2),
                        "MultiSelection setCurrentIndex 追加不清理既有选择");
        }
        /* ---- ExtendedSelection 回归：仍为 ClearAndSelect ---- */
        XAbstractItemView_setSelectionMode(
            &view, XAbstractItemViewSelectionMode_ExtendedSelection);
        XAbstractItemView_setCurrentIndex(&view, 2, 2);
        {
            XItemSelectionModel* smv = XAbstractItemView_selectionModel(&view);
            XAPI_EXPECT(XItemSelectionModel_isSelected(smv, 2, 2) &&
                        !XItemSelectionModel_isSelected(smv, 1, 1),
                        "ExtendedSelection setCurrentIndex 先清后选（回归）");
        }
        /* ---- MultiSelection 键盘导航不触碰选择集合（对标 Qt
         *      multiSelectionCommand 的 KeyPress 分支回落 NoUpdate；
         *      本实现仍移动当前项保导航可用） ---- */
        XAbstractItemView_setSelectionMode(
            &view, XAbstractItemViewSelectionMode_MultiSelection);
        XAbstractItemView_setCurrentIndex(&view, 0, 0);
        vsig_reset();
        vw_injectKey((XWidget*)&view, XEVENT_TYPE_KEY_PRESS, XKey_Down);
        XAPI_EXPECT(XAbstractItemView_currentRow(&view) == 1,
                    "MultiSelection Down 移动当前项");
        {
            XItemSelectionModel* smv = XAbstractItemView_selectionModel(&view);
            XAPI_EXPECT(XItemSelectionModel_isSelected(smv, 0, 0) &&
                        !XItemSelectionModel_isSelected(smv, 1, 0),
                        "MultiSelection 键盘导航不改选择集合（NoUpdate 语义）");
        }
        /* ---- update(index) 局部刷新入口（对标
         *      QAbstractItemView::update(const QModelIndex&)；渲染效果
         *      不在断言职责，覆盖有效/无效索引无崩溃与入口可达） ---- */
        XAbstractItemView_update(&view, 1, 1);
        XAbstractItemView_update(&view, -1, -1);
        XAbstractItemView_update(&view, 99, 99);
        XAPI_EXPECT(true, "update(index) 有效/越界索引调用无崩溃");

        if (model) XClassDelete(model);
        XClassDeinit(&view);
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
        XAPI_EXPECT(XListView_flow(&lv) == XListViewFlow_TopToBottom,
                    "默认 flow=TopToBottom（Qt Flow 缺省，qlistview.h:37）");
        {
            int gw = -9;
            int gh = -9;
            XListView_gridSize(&lv, &gw, &gh);
            XAPI_EXPECT(gw == -1 && gh == -1,
                        "默认 gridSize 未启用（-1；Qt QSize(-1,-1) 未启用语义）");
        }
        XAPI_EXPECT(!XListView_isWrapping(&lv),
                    "默认 wrapping=false（Qt 缺省）");
        XAPI_EXPECT(XListView_viewMode(&lv) == XListViewViewMode_ListMode,
                    "默认 viewMode=ListMode（Qt 缺省）");
        XAPI_EXPECT(XListView_resizeMode(&lv) == XListViewResizeMode_Fixed,
                    "默认 resizeMode=Fixed（Qt 缺省，qlistview.h:39）");
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
            XAPI_EXPECT(gw == 60 && gh == 40,
                        "setGridSize/gridSize 往返（双输出同源）");
        }
        XListView_setGridSize(&lv, 0, -1);
        {
            int gw = -9;
            int gh = -9;
            XListView_gridSize(&lv, &gw, &gh);
            XAPI_EXPECT(gw == 0 && gh == -1,
                        "setGridSize 原样存储（Qt 6.8 探针 setGridSize(0,10)"
                        "→(0,10)；旧实现 <=0 归 -1 与 Qt 不符，gap 148）");
        }
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
                    "clearPropertyFlags 清属性标志位（不改属性本身，"
                    "对标 qlistview.cpp:488-492）");

        /* ---- viewMode 联动（对标 Qt setViewMode 联动 wrapping/flow） ---- */
        XListView_setViewMode(&lv, XListViewViewMode_IconMode);
        XAPI_EXPECT(XListView_viewMode(&lv) == XListViewViewMode_IconMode &&
                    XListView_isWrapping(&lv) &&
                    XListView_flow(&lv) == XListViewFlow_LeftToRight,
                    "IconMode 联动 wrapping=true、flow=LeftToRight");
        XListView_setViewMode(&lv, XListViewViewMode_ListMode);
        XAPI_EXPECT(XListView_viewMode(&lv) == XListViewViewMode_ListMode &&
                    !XListView_isWrapping(&lv) &&
                    XListView_flow(&lv) == XListViewFlow_TopToBottom,
                    "ListMode 复位 wrapping=false、flow=TopToBottom");
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
        XObject_connect((XObject*)&lv,
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

        if (model) XClassDelete(model);
        XClassDeinit(&lv);
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
        XAPI_EXPECT(XAbstractItemView_model(&lw.m_base.m_base) != NULL,
                    "内建条目模型可用（对标 QListWidget::model；P2-33 迁基类口）");
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
        /* ---- 读取（对标 item 的借用形态 + XString_create_copy 独立副本
         *      （P2-32 迁移，替代已删除的 item_new）） ---- */
        XAPI_EXPECT(XListWidget_item(&lw, 99) == NULL,
                    "越界 item 返回 NULL");
        XAPI_EXPECT(xapi_cstr(XListWidget_item_2(&lw, 99))[0] == '\0',
                    "越界 item_2 返回空串");
        {
            XString* copy = XString_create_copy(XListWidget_item(&lw, 0));
            XAPI_EXPECT(copy != NULL &&
                        strcmp(xapi_u8(copy), "置顶") == 0,
                        "item 独立副本（XString_create_copy）返回文本副本");
            if (copy) XClassDelete((XClass*)copy);
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
        if (taken) XClassDelete((XClass*)taken);
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
            if (rowWidget) XClassDelete(rowWidget);
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

        /* ---- 内建模型桥（itemChanged 经 dataChanged 桥接；P2-33 迁基类口） ---- */
        bridge = XAbstractItemView_model(&lw.m_base.m_base);
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

        XClassDeinit(&lw);
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
        XObject_connect((XObject*)&tv, XSignal(XTreeView_expanded_signal),
                          (XObject*)&tv, vw_tvExpandedSlot,
                          XConnectionType_Direct);
        XObject_connect((XObject*)&tv, XSignal(XTreeView_collapsed_signal),
                          (XObject*)&tv, vw_tvCollapsedSlot,
                          XConnectionType_Direct);
        XObject_connect((XObject*)&tv,
                          XSignal(XTreeView_columnResized_signal),
                          (XObject*)&tv, vw_tvColumnResizedSlot,
                          XConnectionType_Direct);
        XObject_connect((XObject*)&tv,
                          XSignal(XTreeView_columnCountChanged_signal),
                          (XObject*)&tv, vw_tvColumnCountChangedSlot,
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
        /* P2-34/35（2026-11-13）迁移：排序状态承载在表头指示器（视图侧
         * sortColumn 读数口已删）；「未排序」哨兵 -1 同表头缺省。 */
        XAPI_EXPECT(XHeaderView_sortIndicatorSection(
                        XTreeView_header(&tv)) == -1,
                    "默认排序列 -1（未设置，表头指示器承载）");
        /* P2-37/38（2026-11-13）：默认 selectionRectVisible=false 断言已
         * 随 API 删除（状态仅存无消费且 Qt 属 QListView）。 */
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
        XAPI_EXPECT(XTreeView_header(&tv) != NULL,
                    "构造即建内建缺省表头（Qt qtreeview.cpp:3128-3132，"
                    "header() 恒非空；gap 138）");

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
        /* P2-37/38（2026-11-13）：setSelectionRectVisible 往返已随 API
         * 删除（状态仅存无消费）；剩余三态照常往返。 */
        XTreeView_setAllColumnsShowFocus(&tv, true);
        XTreeView_setAnimated(&tv, true);
        XTreeView_setWordWrap(&tv, true);
        XAPI_EXPECT(XTreeView_allColumnsShowFocus(&tv) &&
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
        /* P2-36（2026-11-13）迁移：rowAt → indexAt 模型索引口径（行号取
         * outRow；XTreeView 的 indexAt 不用 x、恒回列 0，与旧 rowAt 的
         * y 向除法在有效行/表头区两分支等价，且新增模型行界校验）。 */
        {
            int rowA = -1;
            int rowB = -1;
            int rowC = -1;
            int colAny = -1;
            (void)XAbstractItemView_indexAt(&tv.m_base, 0, 20 + 14,
                                            &rowA, &colAny);
            (void)XAbstractItemView_indexAt(&tv.m_base, 0, 20 + 28 + 14,
                                            &rowB, &colAny);
            XAPI_EXPECT(rowA == 0 && rowB == 1,
                        "indexAt 扣表头区后按行高反查行号（P2-36 迁 rowAt）");
            (void)XAbstractItemView_indexAt(&tv.m_base, 0, 10, &rowC, &colAny);
            XAPI_EXPECT(rowC == -1,
                        "indexAt 表头区不命中（行号 -1，P2-36 迁 rowAt）");
        }
        XAPI_EXPECT(XTreeView_columnAt(&tv, 50) == 0 &&
                    XTreeView_columnAt(&tv, 150) == 1,
                    "columnAt 按可见列宽反查（对标 columnAt）");
        /* [P0-72 2026-11-12] resizeColumnToContents 已接内容测算（此前是空操作桩
         * ⇒ 旧断言锁「列宽保持 100」，与 Qt resizeColumnToContents 语义相反）。
         * 写入长文本后按内容重算：列 0 左缘 indentation(20)+12 + 右内边距 4
         * = 36 起，宽度必变且大于下限（精确值取决于字体实测宽）。 */
        XAbstractItemModel_setData_2(model, 1, 0, "abcdefghijklmnop");
        XTreeView_resizeColumnToContents(&tv, 0);
        XAPI_EXPECT(XTreeView_columnWidth(&tv, 0) != 100 &&
                    XTreeView_columnWidth(&tv, 0) > 36,
                    "resizeColumnToContents 按内容重算列宽（P0-72）");
        /* 还原列宽（后续 columnResized(0,100,120) 断言依赖此夹具）。 */
        XTreeView_setColumnWidth(&tv, 0, 100);

        /* ---- 列信号（对标 Qt QTreeView 的 columnResized/
         *      columnCountChanged 受保护槽——Qt 6.8.3 qtreeview.h 中
         *      二者为受保护槽而非信号，由表头 sectionResized/
         *      sectionCountChanged 驱动；本实现列宽/列数由视图自持，
         *      以 setColumnWidth 实际变化、列状态表长度变化为发射点） */
        vsig_reset();
        XTreeView_setColumnWidth(&tv, 0, 120);
        XAPI_EXPECT(g_sig.tvColResized == 1 && g_sig.tvCRCol == 0 &&
                    g_sig.tvCROld == 100 && g_sig.tvCRNew == 120,
                    "setColumnWidth 实际变化发射 columnResized(0,100,120)");
        XTreeView_setColumnWidth(&tv, 0, 120);
        XAPI_EXPECT(g_sig.tvColResized == 1,
                    "同宽重设不重复发射 columnResized");
        XAPI_EXPECT(g_sig.tvColCountChanged == 0,
                    "列数未变不发射 columnCountChanged");
        XTreeView_setColumnWidth(&tv, 2, 60);
        XAPI_EXPECT(g_sig.tvColCountChanged == 1 && g_sig.tvCCOld == 2 &&
                    g_sig.tvCCNew == 3,
                    "按列访问扩列状态表发射 columnCountChanged(2,3)");
        XAPI_EXPECT(g_sig.tvColResized == 2 && g_sig.tvCRCol == 2 &&
                    g_sig.tvCROld == 0 && g_sig.tvCRNew == 60,
                    "新列首设列宽发射 columnResized(2,0,60)");
        /* 还原列状态（后续断言依赖 col0=100 的几何口径）。 */
        XTreeView_setColumnWidth(&tv, 0, 100);

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

        /* ---- 排序承载（P2-34/35 迁移：sortByColumn 同步写表头指示器，
         *      视图侧 sortColumn/sortIndicatorOrder 读数口已删，改经
         *      XHeaderView_sortIndicatorSection/Order 观察） ---- */
        XTreeView_sortByColumn(&tv, 1, 1);
        XAPI_EXPECT(XHeaderView_sortIndicatorSection(
                        XTreeView_header(&tv)) == 1 &&
                    XHeaderView_sortIndicatorOrder(
                        XTreeView_header(&tv)) == 1,
                    "sortByColumn 同步表头排序指示器（段/方向承载）");
        XTreeView_sortByColumn(&tv, -1, 0);
        XAPI_EXPECT(XHeaderView_sortIndicatorSection(
                        XTreeView_header(&tv)) == -1,
                    "sortByColumn(-1) 恢复自然顺序并清除指示器段");
        XTreeView_sortByColumn(&tv, 0, 0);
        XAPI_EXPECT(XHeaderView_sortIndicatorOrder(
                        XTreeView_header(&tv)) == 0,
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

        if (header) XClassDelete(header);
        if (model) XClassDelete(model);
        XClassDeinit(&tv);
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
        XAPI_EXPECT(XTreeWidget_sortColumn(&tree) == 0,
                    "sortColumn 缺省 0（qtreewidget.h:266；Qt 6.8 探针）");

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
            int beforeExpanded = g_sig.twItemExpanded;
            /* 本库新顶层行缺省展开（XTreeWidget.c 新行 m_topExpanded=true），
             * 故叶子行 3 此刻处于展开态：collapseItem 是 true→false 的真实
             * 状态变化 ⇒ 发射。Qt 6.8.0 探针口径（qtreewidget.cpp
             * expandItem/collapseItem）：门控是**展开状态是否变化**，不是
             * childCount——已折叠叶子再 collapse 不发射、已展开叶子
             * collapse 发射、叶子 expand 也发射。 */
            XTreeWidget_collapseItem(&tree, 3);
            XAPI_EXPECT(g_sig.twItemCollapsed == before + 1,
                        "程序化 collapseItem 对已展开叶子行置折叠并发信号"
                        "（Qt 6.8 探针：状态变化才发射）");
            XTreeWidget_collapseItem(&tree, 3);
            XAPI_EXPECT(g_sig.twItemCollapsed == before + 1,
                        "对已折叠叶子再 collapse 不发射（Qt 状态变化门控）");
            XTreeWidget_expandItem(&tree, 3);
            XAPI_EXPECT(g_sig.twItemExpanded == beforeExpanded + 1,
                        "叶子行 expandItem 置展开并发信号（Qt 6.8 探针）");
            XTreeWidget_collapseItem(&tree, 3);
            XAPI_EXPECT(g_sig.twItemCollapsed == before + 2,
                        "展开后折叠再次发射（状态变化门控）");
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
        /* ---- 逐列夹具（隔离实例：列 1 写入/未写入列探测） ---- */
        {
            XTreeWidget colTree;
            const char* colNames[3] = { "Mu", "Epsilon", "Alpha" };
            XTreeWidgetItem* r0;
            XTreeWidgetItem* r1;
            int ci;
            XTreeWidget_init(&colTree, NULL, 0);
            XTreeWidget_setColumnCount(&colTree, 3);
            for (ci = 0; ci < 3; ++ci) {
                XTreeWidgetItem* cItem =
                    XTreeWidgetItem_create_2(colNames[ci], NULL);
                XAPI_EXPECT(cItem != NULL, "逐列夹具条目构造");
                XAPI_EXPECT(XTreeWidget_addTopLevelItem(&colTree, cItem),
                            "逐列夹具条目挂树");
            }
            XAPI_EXPECT(XTreeWidget_topLevelItem(&colTree, 0) !=
                            XTreeWidget_topLevelItem(&colTree, 1) &&
                        XTreeWidget_topLevelItem(&colTree, 2) != NULL,
                        "逐列块顶层行访问有效");
            /* P2-39（2026-11-13）迁移：条目级入口直写
             * XTreeWidgetItem_setTextAt_2(XTreeWidget_topLevelItem(...))。 */
            XTreeWidgetItem_setTextAt_2(XTreeWidget_topLevelItem(&colTree, 0),
                                        1, "列1-Mu");
            XTreeWidgetItem_setTextAt_2(XTreeWidget_topLevelItem(&colTree, 1),
                                        1, "列1-Epsilon");
            XTreeWidgetItem_setTextAt_2(XTreeWidget_topLevelItem(&colTree, 2),
                                        1, "列1-Alpha");
            XAPI_EXPECT(strcmp(XTreeWidgetItem_textAt_2(
                                   XTreeWidget_topLevelItem(&colTree, 1), 1),
                               "列1-Epsilon") == 0,
                        "条目级 setTextAt_2 列 1 写入 textAt_2 往返"
                        "（P2-39 迁移）");
            XAPI_EXPECT(strcmp(xapi_cstr(XTreeWidgetItem_text_2(
                                   XTreeWidget_topLevelItem(&colTree, 1))),
                               "Epsilon") == 0,
                        "列 1 写入不影响列 0 兼容槽");
            XAPI_EXPECT(strcmp(XTreeWidgetItem_textAt_2(
                                   XTreeWidget_topLevelItem(&colTree, 0), 2),
                               "") == 0 &&
                        strcmp(XTreeWidgetItem_textAt_2(
                                   XTreeWidget_topLevelItem(&colTree, 0), -1),
                               "") == 0,
                        "未写入列/负列 textAt_2 返回空串（Qt text(column) "
                        "为空 QString；XString* 版仍可返回 NULL，gap 150）");
            /* 同缺口锁定 XString* 版本：未写入列/负列仍返回 NULL（等价
             * Qt 的 null QString 与空 QString 区分；_2 版才归一为空串）。 */
            XAPI_EXPECT(XTreeWidgetItem_textAt(
                            XTreeWidget_topLevelItem(&colTree, 0), 2) == NULL &&
                        XTreeWidgetItem_textAt(
                            XTreeWidget_topLevelItem(&colTree, 0), -1) == NULL,
                        "未写入列/负列 XString* 版 textAt 返回 NULL（gap 150）");
            vsig_reset();
            XTreeWidget_sortItems(&colTree, 1, 0);
            /* 升序按列 1 键：Alpha(原行 2) Epsilon(原行 1) Mu(原行 0)
             * ——整行随动（列 0 与列 1 同步）。 */
            r0 = XTreeWidget_topLevelItem(&colTree, 0);
            r1 = XTreeWidget_topLevelItem(&colTree, 1);
            XAPI_EXPECT(strcmp(xapi_cstr(XTreeWidgetItem_text_2(r0)),
                               "Alpha") == 0 &&
                        strcmp(XTreeWidgetItem_textAt_2(r0, 1),
                               "列1-Alpha") == 0 &&
                        strcmp(xapi_cstr(XTreeWidgetItem_text_2(r1)),
                               "Epsilon") == 0,
                        "sortItems(1,0) 按列 1 键升序整行随动");
            XClassDeinit(&colTree);
        }
        /* ---- 内建模型桥（§8.0g22 四期②）：行=顶层行、列=列号文本
         *      同步；排序后模型行序经桥同步跟随。本块 sortItems(0,0)
         *      后夹具升序，恰为下方排序段升序起点。 ---- */
        {
            const XAbstractItemView* bridgeView =
                (const XAbstractItemView*)&tree.m_base;
            XAbstractItemModel* bridge = XAbstractItemView_model(bridgeView);
            XTreeWidgetItem* rowItem;
            const char* cell;
            XAPI_EXPECT(bridge != NULL, "内建模型桥已挂基类");
            XAPI_EXPECT(bridge &&
                        XAbstractItemModel_rowCount(bridge) ==
                            XTreeWidget_topLevelItemCount(&tree) &&
                        XAbstractItemModel_columnCount(bridge) ==
                            XTreeWidget_columnCount(&tree),
                        "模型桥维度=顶层行数×列数");
            rowItem = XTreeWidget_topLevelItem(&tree, 0);
            cell = bridge ? XAbstractItemModel_data_2(bridge, 0, 0) : NULL;
            XAPI_EXPECT(cell && rowItem &&
                        strcmp(cell, xapi_cstr(
                                       XTreeWidgetItem_text_2(rowItem))) == 0,
                        "模型桥首格=首条目列 0 文本");
            XTreeWidget_sortItems(&tree, 0, 0);
            rowItem = XTreeWidget_topLevelItem(&tree, 0);
            cell = bridge ? XAbstractItemModel_data_2(bridge, 0, 0) : NULL;
            XAPI_EXPECT(cell && rowItem &&
                        strcmp(cell, xapi_cstr(
                                       XTreeWidgetItem_text_2(rowItem))) == 0,
                        "排序后模型桥首行仍与首条目一致");
        }
        /* ---- 勾选指示器（四期④：存储往返+itemChanged 发射） ---- */
        {
            XTreeWidgetItem* crow = XTreeWidget_topLevelItem(&tree, 1);
            XAPI_EXPECT(crow != NULL && XTreeWidgetItem_checkState(crow) ==
                            XItemCheckState_Unchecked,
                        "条目默认勾选态=Unchecked");
            XTreeWidgetItem_setCheckState(crow, XItemCheckState_Checked);
            XAPI_EXPECT(XTreeWidgetItem_checkState(crow) ==
                            XItemCheckState_Checked,
                        "setCheckState(Checked) 往返");
            vsig_reset();
            XTreeWidgetItem_setCheckState(crow,
                                          XItemCheckState_PartiallyChecked);
            XAPI_EXPECT(XTreeWidgetItem_checkState(crow) ==
                            XItemCheckState_PartiallyChecked &&
                        g_sig.twItemChanged >= 1,
                        "setCheckState 部分选中发射 itemChanged");
            vsig_reset();
            XTreeWidgetItem_setCheckState(crow,
                                          XItemCheckState_PartiallyChecked);
            XAPI_EXPECT(g_sig.twItemChanged == 0,
                        "同态 setCheckState 相等短路不重发");
            XTreeWidgetItem_setCheckState(crow, XItemCheckState_Unchecked);
        }
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
            XAPI_EXPECT(hi != NULL && XTreeWidgetItem_childCount(hi) == 0 &&
                        strcmp(XTreeWidgetItem_textAt_2(hi, 1), "列二") == 0,
                        "表头列文本存于条目自身（Qt headerItem()->text(column)，"
                        "不是子节点；gap 142）");
        }
        {
            /* 对标 Qt 6.8 qtreewidget.cpp:2788-2807：表头每列的标签由该
               条目自身对应列文本提供（"label for each column is supplied
               by the corresponding label in the item"），setHeaderItem
               接管所有权并按 item->columnCount() 同步列数。 */
            XTreeWidgetItem* newHeader = XTreeWidgetItem_create_2(NULL, NULL);
            XTreeWidgetItem_setTextAt_2(newHeader, 0, "H0");
            XTreeWidgetItem_setTextAt_2(newHeader, 1, "H1");
            XTreeWidget_setHeaderItem(&tree, newHeader);
            XAPI_EXPECT(strcmp(xapi_cstr(XTreeWidget_headerLabel(&tree, 0)), "H0") == 0 &&
                        strcmp(xapi_cstr(XTreeWidget_headerLabel(&tree, 1)), "H1") == 0,
                        "setHeaderItem 接管并按条目自身列文本回填表头");
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
            if (cellWidget) XClassDelete(cellWidget);
        }
        XTreeWidget_setColumnCount(&tree, 3);
        XAPI_EXPECT(XTreeWidget_columnCount(&tree) == 3,
                    "setColumnCount(3) 列数承载");
        XTreeWidget_setColumnCount(&tree, 0);
        XAPI_EXPECT(XTreeWidget_columnCount(&tree) == 0,
                    "setColumnCount(0) 合法（Qt 6.8 探针 setColumnCount(0)"
                    "→columnCount()==0；gap 145）");
        /* [P0-30 2026-11-12] editItem 已接编辑闭环（转发
         * XAbstractItemView_edit；此前是校验入参后的空操作桩，旧断言只写
         * `true` 无法观察行为）。列数 0 时列号校验早退，先恢复 1 列再判
         * 「缺省门禁不开编辑器 → 打开视图总闸后真开」。 */
        XTreeWidget_setColumnCount(&tree, 1);
        XTreeWidget_editItem(&tree, 0, 0);
        XAPI_EXPECT(!XAbstractItemView_isEditing(&tree.m_base.m_base),
                    "editItem 缺省门禁不开编辑器（itemsEditable=false 且"
                    "模型 flags 无 ItemIsEditable）");
        XAbstractItemView_setItemsEditable(&tree.m_base.m_base, true);
        XTreeWidget_editItem(&tree, 0, 0);
        XAPI_EXPECT(XAbstractItemView_isEditing(&tree.m_base.m_base),
                    "editItem 转发编辑闭环（P0-30：打开总闸后真开编辑器）");
        XAbstractItemView_reset(&tree.m_base.m_base);
        XAPI_EXPECT(!XAbstractItemView_isEditing(&tree.m_base.m_base),
                    "reset 收敛活动编辑器");
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
        /* setColumnCount(0) 已把表头列文本随列裁剪清空；先重建列 0 文本，
         * 再验 clear 不动表头（否则本段断言的是空串而非「保留」）。 */
        XTreeWidget_setHeaderLabel(&tree, "H0");
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

        XClassDeinit(&tree);
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
        XAPI_EXPECT(XTableView_rowHeight(&table, 0) == 24,
                    "默认行高 24");
        /* [断言修正 2026-11-12 / P0-32] setModel 现在把模型下发给水平表头并
         * 同步段数（对标 Qt QTableView::setModel，qtableview.cpp:1284-1285）
         * ⇒ columnWidth = horizontalHeader()->sectionSize（Qt 同口径），
         * 由模型建出的段用表头 defaultSectionSize=100（水平头缺省，见
         * XHeaderViewAlignTest）。此前无段时回落的视图数组缺省 80 只在
         * **无模型/段未覆盖**时可见（下一处越界列断言即该分支）。 */
        XAPI_EXPECT(XTableView_columnWidth(&table, 0) == 100,
                    "默认列宽 100（模型段数驱动表头缺省段尺寸；P0-32）");
        XAPI_EXPECT(XTableView_showGrid(&table) &&
                    XTableView_gridStyle(&table) == XTABLEVIEW_GRID_SOLID,
                    "默认显示网格且实线（Qt showGrid=true/SolidLine）");
        XAPI_EXPECT(XTableView_wordWrap(&table),
                    "默认 wordWrap=true（Qt QTableView 缺省；"
                    "qtableview_p.h:115 + Qt 6.8 探针，gap 152）");
        XAPI_EXPECT(XTableView_isCornerButtonEnabled(&table),
                    "默认左上角按钮启用（Qt cornerButtonEnabled 缺省 true）");
        XAPI_EXPECT(!XTableView_isSortingEnabled(&table),
                    "默认排序关闭（Qt 缺省）");
        XAPI_EXPECT(!XTableView_isRowHidden(&table, 0) &&
                    !XTableView_isColumnHidden(&table, 0),
                    "默认行/列均可见");
        XAPI_EXPECT(XTableView_rowSpan(&table, 0, 0) == 1 &&
                    XTableView_columnSpan(&table, 0, 0) == 1,
                    "默认无合并（rowSpan/columnSpan 均为 1，对齐 Qt）");
        XAPI_EXPECT(XTableView_horizontalHeader(&table) != NULL &&
                    XTableView_verticalHeader(&table) != NULL,
                    "构造内建双表头（对标 QTableViewPrivate::init）");

        /* ---- 尺寸往返 ---- */
        { int r; for (r = 0; r < 8; ++r) XTableView_setRowHeight(&table, r, 30); }
        XTableView_setColumnWidth(&table, 0, 120);
        XTableView_setColumnWidth(&table, 1, 80);
        XAPI_EXPECT(XTableView_rowHeight(&table, 0) == 30 &&
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
        XAPI_EXPECT(XTableView_rowHeight(&table, 0) > 0,
                    "resizeToContents 族调用后行高保持正值");
        /* ---- 跨行/列合并（对标 setSpan/spanAt/clearSpans） ---- */
        XTableView_setSpan(&table, 0, 0, 2, 2);
        XAPI_EXPECT(XTableView_rowSpan(&table, 0, 0) == 2 &&
                    XTableView_columnSpan(&table, 0, 0) == 2,
                    "setSpan(0,0,2,2) 合并区间生效（原点）");
        XAPI_EXPECT(XTableView_rowSpan(&table, 1, 1) == 2 &&
                    XTableView_columnSpan(&table, 1, 1) == 2,
                    "被覆盖格同样命中所属区间（对齐 Qt rowSpan/columnSpan）");
        XAPI_EXPECT(XTableView_rowSpan(&table, 2, 0) == 1,
                    "未合并格跨度=1");
        {
            int orow = -1;
            int ocol = -1;
            /* [断言口径修正 2026-11-05] 本库 indexAt/rowAt 坐标域含顶部
             * 表头区（rowAt(10)=-1 与 itemAt(80,36) 命中首格同口径），
             * 被覆盖格 (1,1) 须以内容系 y=表头+1 寻址。 */
            XAPI_EXPECT(XTableView_indexAt(&table, 1, 20 + 1, &orow, &ocol) &&
                        orow == 0 && ocol == 0,
                        "indexAt 命中被覆盖格时吸附到区间原点");
        }
        XTableView_setSpan(&table, 0, 0, 1, 1);
        XAPI_EXPECT(XTableView_rowSpan(&table, 0, 0) == 1,
                    "跨度均 1 移除合并（同 Qt）");
        XTableView_setSpan(&table, 0, 0, 0, 2);
        XAPI_EXPECT(XTableView_rowSpan(&table, 0, 0) == 1,
                    "跨度为 0 不覆盖任何单元格（对齐 Qt：仅负值拒绝）");
        XTableView_setSpan(&table, 0, 0, 2, 2);
        XTableView_setSpan(&table, 0, 0, 3, 2);
        XAPI_EXPECT(XTableView_rowSpan(&table, 0, 0) == 3,
                    "同原点重复设置替换旧值");
        XTableView_clearSpans(&table);
        XAPI_EXPECT(XTableView_rowSpan(&table, 0, 0) == 1,
                    "clearSpans 清空全部合并");

        /* ---- 滚动/偏移/视觉序（对标 scrollTo/horizontalOffset/
         *      verticalOffset/visualIndex） ---- */
        XTableView_scrollTo(&table, 5, 0,
                            XAbstractItemViewScrollHint_EnsureVisible);
        XAPI_EXPECT(true, "scrollTo 调用无崩溃（转发基类 EnsureVisible）");
        XAPI_EXPECT(XTableView_horizontalOffset(&table) == 0 &&
                    XTableView_verticalOffset(&table) == 0,
                    "未滚动偏移恒 0（横/纵滚动条值口径）");
        /* 对齐 Qt：QTableView 的 visualIndex(const QModelIndex&) 为 **private**
         * （qtableview.cpp:3624），本框架不导出对应公开 API。
         * 行/列的视觉序查询走表头（对齐 QHeaderView::visualIndex）。 */
        /* [断言口径修正 2026-11-05] 本表 2 列，visualIndex(2) 越界返回
         * -1——查询列改 1（无重排=逻辑序）。 */
        XAPI_EXPECT(XHeaderView_visualIndex(
                        XTableView_horizontalHeader(&table), 1) == 1 &&
                    XHeaderView_visualIndex(
                        XTableView_verticalHeader(&table), 1) == 1,
                    "visualIndex 经表头查询（对齐 QHeaderView::visualIndex）");

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

        XTableView_setVerticalHeader(&table, NULL); /* 删除前解挂，与 hHeader 口径一致 */
        if (hHeader) XClassDelete(hHeader);
        if (vHeader) XClassDelete(vHeader);
        if (model) XClassDelete(model);
        XClassDeinit(&table);
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
        XTableWidgetItem* hits[8];
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
        /* 对齐 Qt：QTableWidget::setModel 为 private（qtablewidget.h:311），
         * 本框架不导出对应公开 API。 */
        XAPI_EXPECT(XTableWidget_model(&table) != NULL,
                    "内建模型始终存在且不可替换（对齐 Qt setModel 为 private）");
        XAPI_EXPECT(XTableWidget_itemPrototype(&table) == NULL,
                    "初始无原型单元格（对标 itemPrototype 缺省 NULL）");
        XAPI_EXPECT(XTableView_rowHeight((XTableView*)&table, 0) == 24 &&
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
        xTwSetTextUtf8(&table, 0, 0, "甲");
        xTwSetTextUtf8(&table, 1, 0, "乙");
        XTableWidget_insertRow(&table, 1);
        XAPI_EXPECT(XTableWidget_rowCount(&table) == 4 &&
                    strcmp(xapi_cstr(xTwTextUtf8(&table, 2, 0)), "乙") == 0,
                    "insertRow(1) 原行 1 后移到行 2");
        XTableWidget_removeRow(&table, 1);
        XAPI_EXPECT(XTableWidget_rowCount(&table) == 3 &&
                    strcmp(xapi_cstr(xTwTextUtf8(&table, 1, 0)), "乙") == 0,
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
        /* ---- 持久编辑器三件套（对标 QTableWidget open/close/
         *      isPersistentEditorOpen(item) → 基类 index 形态转发） ---- */
        /* [夹具修正 2026-11-05] (1,1) 此前无条目（open 对空 index 无效
         * 为 Qt 语义）——改用已有条目 (0,0) 验开/关。 */
        XTableWidget_openPersistentEditor(&table,
                                          XTableWidget_item(&table, 0, 0));
        XAPI_EXPECT(XTableWidget_isPersistentEditorOpen(
                        &table, XTableWidget_item(&table, 0, 0)),
                    "openPersistentEditor(item) 后 isPersistentEditorOpen 为 true");
        XTableWidget_closePersistentEditor(&table,
                                           XTableWidget_item(&table, 0, 0));
        XAPI_EXPECT(!XTableWidget_isPersistentEditorOpen(
                        &table, XTableWidget_item(&table, 0, 0)) &&
                    !XTableWidget_isPersistentEditorOpen(&table, NULL),
                    "closePersistentEditor(item) 后关闭、NULL 条目 false");

        /* ---- 单元格文本（对标 setText/text 快捷族） ---- */
        xTwSetTextUtf8(&table, 0, 0, "苹果");
        xTwSetTextUtf8(&table, 0, 1, "3");
        xTwSetTextUtf8(&table, 1, 0, "香蕉");
        xTwSetTextUtf8(&table, 1, 1, "5");
        xTwSetTextUtf8(&table, 2, 0, "樱桃");
        xTwSetTextUtf8(&table, 2, 1, "8");
        XAPI_EXPECT(strcmp(xapi_cstr(xTwTextUtf8(&table, 0, 0)), "苹果") == 0 &&
                    strcmp(xapi_cstr(xTwTextUtf8(&table, 2, 1)), "8") == 0,
                    "setText/text 往返一致");
        XAPI_EXPECT(strcmp(xapi_cstr(xTwTextUtf8(&table, 9, 9)), "") == 0,
                    "越界 text 返回空串");
        XAPI_EXPECT(strcmp(XAbstractItemModel_data_2(XTableWidget_model(&table),
                                                     0, 0), "苹果") == 0,
                    "单元格文本与内建模型同步");
        /* ---- setItem 拷贝全字段（对标 setItem/item） ---- */
        {
            XTableWidgetItem* src = XTableWidgetItem_create_utf8("整格",
                                                                 XTableWidgetItem_Type);
            const XTableWidgetItem* got;
            XColor bg = XColor_create_rgb(0, 255, 0, 255);
            XColor fg = XColor_create_rgb(255, 0, 0, 255);
            XAPI_EXPECT(src != NULL, "item 工厂创建可用");
            if (src) {
                XTableWidgetItem_setCheckState(src, XItemCheckState_Checked);
                XTableWidgetItem_setBackground(src, &bg);
                XTableWidgetItem_setForeground(src, &fg);
                /* 对标 Qt 6.8 qtablewidget.cpp:1082-1111（gap 296）：条目
                 * 必须先入表（item->view 非空）才可选中；游离条目
                 * setSelected 为空操作——此处先断言该前置语义。 */
                XTableWidgetItem_setSelected(src, true);
                XAPI_EXPECT(!XTableWidgetItem_isSelected(src),
                            "游离条目 setSelected 为空操作（入表前不可选中；"
                            "gap 296）");
                XTableWidget_setItem(&table, 2, 1, src); /* 所有权转移给表格 */
                /* 入表（view 非空）后 setSelected 才生效。 */
                XTableWidgetItem_setSelected(src, true);
            }
            got = XTableWidget_item(&table, 2, 1);
            XAPI_EXPECT(got == src && got != NULL &&
                        strcmp(XTableWidgetItem_text_toUtf8(got), "整格") == 0 &&
                        XTableWidgetItem_isSelected(got) &&
                        XTableWidgetItem_checkState(got) == XItemCheckState_Checked,
                        "setItem 接管所有权；item 读回文本/选中/勾选");
            {
                XColor out;
                XAPI_EXPECT(XTableWidgetItem_background(got, &out) &&
                            XColor_rgba(&out) == XColor_rgba(&bg),
                            "BackgroundRole 往返");
                XAPI_EXPECT(XTableWidgetItem_foreground(got, &out) &&
                            XColor_rgba(&out) == XColor_rgba(&fg),
                            "ForegroundRole 往返");
            }
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
        XTableWidget_setCurrentItem(&table, XTableWidget_item(&table, 2, 0));
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
        XAPI_EXPECT(XTableWidget_itemAt(&table, 40 + 40, 24 + 12) ==
                        XTableWidget_item(&table, 0, 0),
                    "itemAt(表头后首格) 返回 (0,0) 条目");
        /* [断言口径修正 2026-11-05] itemAt 无行/列出参（原 hitRow/hitCol
         * 是从未被写的死变量，历史靠未初始化栈值碰运气）；语义改由
         * 返回值承载：数据区命中非 NULL、表头区 NULL。 */
        XAPI_EXPECT(XTableWidget_itemAt(&table, 40 + 40, 24 + 12) != NULL,
                    "itemAt 数据区命中返回条目（对标 Qt itemAt）");
        XAPI_EXPECT(XTableWidget_itemAt(&table, 10, 24 + 12) == NULL,
                    "itemAt 表头区返回 NULL");
        /* ---- 表头标签（对标 setHorizontal/VerticalHeaderLabels 族） ---- */
        XTableWidget_setHorizontalHeaderLabels(&table, hLabels, 2);
        XTableWidget_setVerticalHeaderLabels(&table, vLabels, 2);
        XAPI_EXPECT(xapi_strEq(XTableWidget_horizontalHeaderItem(&table, 0),
                               "名称") &&
                    xapi_strEq(XTableWidget_verticalHeaderItem(&table, 1),
                               "乙行"),
                    "表头标签批量写入与读取");
        XAPI_EXPECT(xapi_strEq(XTableWidget_horizontalHeaderItem(&table, 9),
                               ""),
                    "越界表头标签返回空串");
        {
            XString* single = XString_create_utf8("单列头");
            XTableWidget_setHorizontalHeaderItem(&table, 1, single);
            XClassDelete((XClass*)single);
            XAPI_EXPECT(xapi_strEq(
                            XTableWidget_horizontalHeaderItem(&table, 1),
                            "单列头"),
                        "setHorizontalHeaderItem 单列覆写（表格拷贝文本）");
        }
        taken = XTableWidget_takeHorizontalHeaderItem(&table, 1);
        XAPI_EXPECT(taken != NULL &&
                    strcmp(xapi_u8(taken), "单列头") == 0 &&
                    xapi_strEq(XTableWidget_horizontalHeaderItem(&table, 1),
                               "2"),
                    "takeHorizontalHeaderItem 所有权转移并置空（无 item 回落"
                    "section+1）");
        if (taken) XClassDelete((XClass*)taken);
        taken = XTableWidget_takeVerticalHeaderItem(&table, 0);
        XAPI_EXPECT(taken != NULL &&
                    strcmp(xapi_u8(taken), "甲行") == 0,
                    "takeVerticalHeaderItem 取出行表头文本");
        if (taken) XClassDelete((XClass*)taken);
        /* ---- 查找（对标 findItems 精确/包含） ---- */
        count = XTableWidget_findItems(&table, "苹果", 1, hits, 4);
        XAPI_EXPECT(count == 1 && hits[0] == XTableWidget_item(&table, 0, 0),
                    "findItems 精确命中 (0,0)");
        count = XTableWidget_findItems(&table, "", 1, hits, 4);
        XAPI_EXPECT(count == 0,
                    "空表无空文本命中（NULL 按空串处理）");
        /* ---- 按列优先搜索（对齐 QTableWidget::findItems 遍历顺序） ---- */
        {
            int found = XTableWidget_findItems(&table, "香蕉", 1, hits, 4);
            XAPI_EXPECT(found == 1 && XTableWidget_row(&table, hits[0]) == 1,
                        "findItems 精确命中行 1");
        }
        /* ---- 排序（对标 sortItems 整行重排；ASCII 键避免字节序歧义） ---- */
        xTwSetTextUtf8(&table, 0, 0, "b");
        xTwSetTextUtf8(&table, 1, 0, "a");
        xTwSetTextUtf8(&table, 2, 0, "c");
        XTableWidget_sortItems(&table, 0, 0);
        XAPI_EXPECT(strcmp(xapi_cstr(xTwTextUtf8(&table, 0, 0)), "a") == 0 &&
                    strcmp(xapi_cstr(xTwTextUtf8(&table, 2, 0)), "c") == 0,
                    "sortItems(0,0) 按列 0 升序整行重排");
        XTableWidget_sortItems(&table, 0, 1);
        XAPI_EXPECT(strcmp(xapi_cstr(xTwTextUtf8(&table, 0, 0)), "c") == 0 &&
                    strcmp(xapi_cstr(xTwTextUtf8(&table, 2, 0)), "a") == 0,
                    "sortItems(0,1) 降序整行重排");
        /* ---- 选区（对标 setRangeSelected/selectedIndexes/Ranges） ---- */
        vsig_reset();
        {
            XTableWidgetSelectionRange rng =
                XTableWidgetSelectionRange_create(0, 0, 1, 1);
            XTableWidget_setRangeSelected(&table, &rng, true);
        }
        XAPI_EXPECT(g_sig.tblSelChanged >= 1,
                    "setRangeSelected 有变化发射 itemSelectionChanged");
        count = XTableWidget_selectedItems(&table, hits, 4);
        XAPI_EXPECT(count >= 4,
                    "selectedItems 输出范围选中的 4 格");
        {
            XTableWidgetSelectionRange rngs[4];
            count = XTableWidget_selectedRanges(&table, rngs, 4);
            XAPI_EXPECT(count >= 1 &&
                        XTableWidgetSelectionRange_topRow(&rngs[0]) == 0 &&
                        XTableWidgetSelectionRange_bottomRow(&rngs[0]) == 1,
                        "selectedRanges 归并连续行为一条范围");
        }
        {
            XTableWidgetSelectionRange rng =
                XTableWidgetSelectionRange_create(0, 0, 1, 1);
            XTableWidget_setRangeSelected(&table, &rng, false);
        }
        XAPI_EXPECT(true, "setRangeSelected 倒置范围自动归一化（取消路径）");
        count = XTableWidget_selectedItems(&table, hits, 4);
        XAPI_EXPECT(count >= 0,
                    "selectedItems 对应 Qt 选中条目集");
        /* ---- 单元格部件挂载（对标 setCellWidget/cellWidget：表接管
         *      所有权——reparent 到视口+摆位+显示；removeCellWidget/
         *      setCellWidget(NULL)/同格重挂均 deleteLater 旧件） ---- */
        {
            XWidget* cellWidget = XWidget_create(NULL, 0);
            XTableWidget_setCellWidget(&table, 0, 1, cellWidget);
            XAPI_EXPECT(XTableWidget_cellWidget(&table, 0, 1) == cellWidget,
                        "setCellWidget/cellWidget 往返");
            XAPI_EXPECT(XWidget_parentWidget(cellWidget) ==
                            XAbstractScrollArea_viewport(
                                (const XAbstractScrollArea*)&table),
                        "setCellWidget reparent 到视口（Qt 同）");
            XTableWidget_removeCellWidget(&table, 0, 1);
            XAPI_EXPECT(XTableWidget_cellWidget(&table, 0, 1) == NULL,
                        "removeCellWidget 解除关联并 deleteLater 挂件");
            cellWidget = XWidget_create(NULL, 0);
            XTableWidget_setCellWidget(&table, 0, 1, cellWidget);
            XTableWidget_setCellWidget(&table, 0, 1, NULL);
            XAPI_EXPECT(XTableWidget_cellWidget(&table, 0, 1) == NULL,
                        "setCellWidget(NULL) 等价 removeCellWidget");
            cellWidget = XWidget_create(NULL, 0);
            XTableWidget_setCellWidget(&table, 0, 1, cellWidget);
            XTableWidget_setCellWidget(&table, 0, 1, cellWidget);
            XAPI_EXPECT(XTableWidget_cellWidget(&table, 0, 1) == cellWidget,
                        "同指针重挂幂等（Qt 同）");
            XTableWidget_setCellWidget(&table, 0, 1, NULL);
            /* 挂件生命周期归表：延迟删除事件在对象析构时统一清除，
               此处不再手工 XClassDelete（Qt 同：widget 由视图接管）。 */
        }
        /* ---- 挂件滚动联动（reparent 视口+摆位随滚动重算） ---- */
        {
            XWidget* cellWidget = XWidget_create(NULL, 0);
            XRect before;
            XRect after;
            XTableWidget_setCellWidget(&table, 2, 1, cellWidget);
            before = XWidget_geometry(cellWidget);
            /* 加行制造滚动空间→滚到第 19 行→row2 挂件应被藏起或移出视口。 */
            XTableWidget_setRowCount(&table, 20);
            XTableWidget_scrollToItem(
                &table, XTableWidget_item(&table, 19, 1),
                XAbstractItemViewScrollHint_EnsureVisible);
            after = XWidget_geometry(cellWidget);
            XAPI_EXPECT(!XWidget_isVisible(cellWidget) ||
                        after.y != before.y,
                        "挂件随滚动重摆/藏起（scrollContentsBy relayout）");
            XTableWidget_setRowCount(&table, 3);
            /* 无头环境视口保持构造默认 100x30（xasa_layout 随首次
               show 触发），row2(y=72) 在视口外被正确藏起：断言几何
               与 visualItemRect 一致即可（可见性=视口裁剪口径）。 */
            {
                XRect fin = XWidget_geometry(cellWidget);
                XRect want = XTableWidget_visualItemRect(&table,
                    XTableWidget_item(&table, 2, 1));
                XAPI_EXPECT(fin.x == want.x && fin.y == want.y &&
                            fin.width == want.width &&
                            fin.height == want.height,
                            "滚回后挂件几何与 visualItemRect 一致");
            }
            XTableWidget_setCellWidget(&table, 0, 1, NULL);
        }
        /* ---- activated 发射与 setItem(NULL)（Qt 行为） ---- */
        {
            vsig_reset();
            XTableWidget_setCurrentCell(&table, 1, 0);
            vw_injectKey((XWidget*)&table, XEVENT_TYPE_KEY_PRESS,
                         XKey_Return);
            XAPI_EXPECT(g_sig.tblCellActivated >= 1 &&
                        g_sig.tblItemActivated >= 1,
                        "Return 触发 cellActivated/itemActivated（Qt 同）");
        }
        {
            vsig_reset();
            xTwSetTextUtf8(&table, 0, 0, "临时值");
            XTableWidget_setItem(&table, 0, 0, NULL);
            XAPI_EXPECT(xapi_cstr(xTwTextUtf8(&table, 0, 0))[0] == '\0',
                        "setItem(NULL) 清空该格（takeItem+delete 同）");
        }
        /* ---- takeItem（对标 takeItem：文本所有权转移） ---- */
        {
            /* takeItem 返回的是 XTableWidgetItem*（不是表头那条 XString* 复用
             * 变量；Qt 中 QTableWidgetItem 与 QString 是不同类型）。 */
            XTableWidgetItem* takenItem;
            vsig_reset();
            takenItem = XTableWidget_takeItem(&table, 2, 1);
            XAPI_EXPECT(takenItem != NULL,
                        "takeItem 归还单元格文本所有权");
            XAPI_EXPECT(xapi_cstr(xTwTextUtf8(&table, 2, 1))[0] == '\0' &&
                        g_sig.tblCellChanged >= 1,
                        "取出后置空并发射 cellChanged");
            if (takenItem) XClassDelete((XClass*)takenItem);
            XAPI_EXPECT(XTableWidget_takeItem(&table, 2, 1) == NULL,
                        "空单元格 takeItem 返回 NULL（不发信号）");
        }
        /* ---- 索引反查/恒等承载（对标 indexFromItem/row/column） ---- */
        {
            const XTableWidgetItem* cur = XTableWidget_currentItem(&table);
            XAPI_EXPECT(cur != NULL &&
                        XTableWidget_indexFromItem(&table, cur, &hitCol) >= 0,
                        "indexFromItem 按地址反查行号");
        }
        {
            /* Qt 真语义：row(item)/column(item) 位置反查（item 必须
               来自本表格；外部位址/NULL 返回 -1）。
               [夹具修正 2026-11-05] (2,1) 已被上文 takeItem 拿走、
               (0,0) 已被 setItem(NULL) 清空——反查改用存活的 (1,0)
               （sortItems 后列 0 行 1="a"）。 */
            const XTableWidgetItem* it = XTableWidget_item(&table, 1, 0);
            XTableWidgetItem foreign;
            XMemset(&foreign, 0, sizeof(foreign));
            XAPI_EXPECT(it != NULL &&
                        XTableWidget_row(&table, it) == 1 &&
                        XTableWidget_column(&table, it) == 0,
                        "row(item)/column(item) 位置反查（Qt 同）");
            XAPI_EXPECT(XTableWidget_row(&table, NULL) == -1 &&
                        XTableWidget_column(&table, &foreign) == -1,
                        "row/column 外部位址与 NULL 返回 -1");
        }
        {
            /* 排序开关/持久编辑器转发（Qt 继承面在本层的显式转发）。
             * XTableWidget 首成员即 XTableView 基类（XTableWidget.h
             * struct 首字段 m_base），C 侧显式上转型——与同文件
             * XTableView_rowHeight((XTableView*)&table, ...) 写法一致。 */
            XTableView_setSortingEnabled((XTableView*)&table, true);
            XAPI_EXPECT(XTableView_isSortingEnabled((XTableView*)&table),
                        "setSortingEnabled/isSortingEnabled 往返");
            XTableView_setSortingEnabled((XTableView*)&table, false);
            XAPI_EXPECT(!XTableView_isSortingEnabled((XTableView*)&table),
                        "排序开关复位");
            XTableWidget_openPersistentEditor(
                &table, XTableWidget_item(&table, 0, 0));
            XTableWidget_closePersistentEditor(
                &table, XTableWidget_item(&table, 0, 0));
            XAPI_EXPECT(true, "open/closePersistentEditor 转发无崩溃");
        }
        {
            /* [夹具修正 2026-11-05] (0,0) 已被 setItem(NULL) 清空，改用
               存活的 (1,0)。 */
            const XTableWidgetItem* rowItem =
                XTableWidget_itemFromIndex(&table, 1, 0);
            XAPI_EXPECT(rowItem != NULL &&
                        rowItem == XTableWidget_item(&table, 1, 0),
                        "itemFromIndex(row,col) 返回该格条目（对齐 Qt）");
        }
        /* ---- 视觉序（平铺模型视觉序=逻辑序） ---- */
        XAPI_EXPECT(XTableWidget_visualRow(&table, 2) == 2 &&
                    XTableWidget_visualColumn(&table, 1) == 1,
                    "visualRow/visualColumn 恒等（平铺无重排映射）");
        /* ---- 原型（对齐 Qt：setItemPrototype 接管所有权） ---- */
        {
            XTableWidgetItem* proto = XTableWidgetItem_create_utf8(
                "原型", XTableWidgetItem_Type);
            XTableWidget_setItemPrototype(&table, proto);
            XAPI_EXPECT(XTableWidget_itemPrototype(&table) == proto,
                        "setItemPrototype 接管所有权并回读同一指针");
            XTableWidget_setItemPrototype(&table, NULL);
            XAPI_EXPECT(XTableWidget_itemPrototype(&table) == NULL,
                        "setItemPrototype(NULL) 清除（旧原型由表格释放）");
        }
        /* ---- scrollToItem / editItem / clearSpans（便捷转发） ---- */
        XTableWidget_scrollToItem(&table, XTableWidget_item(&table, 2, 1),
                                  XAbstractItemViewScrollHint_EnsureVisible);
        XTableWidget_editItem(&table, XTableWidget_item(&table, 0, 0));
        XTableView_clearSpans((XTableView*)&table);
        XAPI_EXPECT(true, "scrollToItem/editItem/clearSpans 便捷转发无崩溃");
        /* ---- clearContents：保留行列数、清空内容（对标 clearContents） ---- */
        XTableWidget_clearContents(&table);
        XAPI_EXPECT(XTableWidget_rowCount(&table) == 3 &&
                    strcmp(xapi_cstr(xTwTextUtf8(&table, 0, 0)), "") == 0,
                    "clearContents 清内容且保留 3 行");
        XAPI_EXPECT(strcmp(XAbstractItemModel_data_2(XTableWidget_model(&table),
                                                     0, 0), "") == 0,
                    "clearContents 同步内建模型为空");
        XAPI_EXPECT(XTableWidget_currentRow(&table) == -1 &&
                    XTableWidget_currentColumn(&table) == -1,
                    "clearContents 复位当前格（selectionModel clear 同）");
        /* ---- clear：行列归零、表头一并清空（对标 clear） ---- */
        XTableWidget_setHorizontalHeaderLabels(&table, hLabels, 2);
        XTableWidget_clear(&table);
        XAPI_EXPECT(XTableWidget_rowCount(&table) == 0 &&
                    XTableWidget_columnCount(&table) == 0,
                    "clear 后行列数归零");
        /* [断言口径修正 2026-11-05] clear 后 m_columns=0，getter(0) 走
         * 越界分支返回空串（"section+1" 回落仅对「有列无 item」生效）；
         * 表头文本确已随 clear 移除。 */
        XAPI_EXPECT(xapi_strEq(XTableWidget_horizontalHeaderItem(&table, 0),
                               ""),
                    "clear 同时移除表头文本（列归零后查询越界返回空串）");
        XAPI_EXPECT(XTableWidget_currentRow(&table) == -1,
                    "clear 复位当前单元格");

        /* 栈对象：delete_base 经 IsHeap=false 仅析构不释放。 */
        XClassDelete(&table);
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
        XAPI_EXPECT(XHeaderView_defaultSectionSize(&header) == 100,
                    "水平表头默认段尺寸 100（Qt 6.8 探针；gap 154）");
        XAPI_EXPECT(XHeaderView_minimumSectionSize(&header) == 40,
                    "水平表头最小区间尺寸缺省 40（Qt 6.8 探针；gap 154）");
        XAPI_EXPECT(XHeaderView_maximumSectionSize(&header) == 1048575,
                    "最大区间尺寸缺省 1048575（Qt 同值）");
        XAPI_EXPECT(XHeaderView_sectionResizeMode(&header) ==
                    XHeaderViewResizeMode_Interactive,
                    "全局调整模式缺省 Interactive（Qt 缺省）");
        XAPI_EXPECT(!XHeaderView_stretchLastSection(&header),
                    "末尾拉伸缺省 false（Qt 缺省；P2-40 删 is 前缀别名，"
                    "Qt 名 stretchLastSection 即唯一读取口）");
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
                    "独立表头 sectionsClickable/sectionsMovable 缺省 false"
                    "（Qt 6.8.0 探针：QHeaderView(Qt::Horizontal) 实测均为 "
                    "false；QTableView 自建表头另置 clickable=true）");
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
        /* [P0-77 2026-11-12] stretchSectionCount 统计的是「生效模式为
         * Stretch 的段数」（qheaderview.cpp:1291-1295 返回
         * d->stretchSections），**不含** stretchLastSection；本表头此时
         * count()==0 且全局 Interactive ⇒ 仍为 0（旧断言把它当
         * stretchLast 的镜像=1，与 Qt 相反）。 */
        XAPI_EXPECT(XHeaderView_stretchLastSection(&header) &&
                    XHeaderView_stretchSectionCount(&header) == 0,
                    "stretchLast 往返；stretchSectionCount 不含 stretchLast"
                    "（Qt 口径，count()==0 ⇒ 0）");
        XHeaderView_setStretchLastSection(&header, false);
        /* 全局 Stretch 时 stretchSectionCount == count()（同 P0-77）。 */
        XHeaderView_setCount(&header, 3);
        XHeaderView_setSectionResizeMode(&header,
                                         XHeaderViewResizeMode_Stretch);
        XAPI_EXPECT(XHeaderView_stretchSectionCount(&header) == 3,
                    "全局 Stretch 时 stretchSectionCount==count()==3");
        XHeaderView_setSectionResizeMode(
            &header, XHeaderViewResizeMode_Interactive);
        XAPI_EXPECT(XHeaderView_stretchSectionCount(&header) == 0,
                    "全局回 Interactive 后 stretchSectionCount=0");
        XHeaderView_setCount(&header, 0);
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
                    "isFirstSectionMovable 返回存储标志本身，缺省 true"
                    "（Qt 6.8.0 探针：独立表头 isFirstSectionMovable()==true；"
                    "真正可拖还需 sectionsMovable 为真，见 qheaderview.cpp:1120）");
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
        XAPI_EXPECT(XHeaderView_defaultSectionSize(&header) == 100,
                    "resetDefaultSectionSize 复位水平缺省 100（Qt 6.8 探针）");

        /* ---- resizeSection 钳制（先 [min,max] 再落库，对标 resizeSection） ---- */
        XHeaderView_setMaximumSectionSize(&header, 100);
        XHeaderView_setSectionSize(&header, 1, 4000);
        XAPI_EXPECT(XHeaderView_sectionSize(&header, 1) == 4000,
                    "setSectionSize 原样存值（程序化钳制入口为 resizeSection）");
        XHeaderView_resizeSection(&header, 0, 10);
        XAPI_EXPECT(XHeaderView_sectionSize(&header, 0) == 40,
                    "resizeSection 先钳制到最小 40（水平头缺省；gap 154）");
        XHeaderView_resizeSection(&header, 0, 500);
        XAPI_EXPECT(XHeaderView_sectionSize(&header, 0) == 100,
                    "resizeSection 先钳制到最大 100");
        XHeaderView_resizeSection(&header, 0, 0);
        XAPI_EXPECT(XHeaderView_sectionSize(&header, 0) == 0,
                    "resizeSection 0 原样存储（Qt 6.8 探针 resizeSection(0,0)"
                    "→0；gap 155）");
        XHeaderView_resizeSection(&header, 9, 30);
        XAPI_EXPECT(XHeaderView_sectionSize(&header, 9) == 0,
                    "越界段 sectionSize 返回 0，写入忽略（gap 156）");
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
                    XHeaderView_sectionSize(&header, 9) == 0,
                    "越界段隐藏读取 false、sectionSize 返回 0（gap 156）");
        /* ---- 移动/交换（视觉映射取代「移动即重排」；信号发射） ---- */
        vsig_reset();
        XHeaderView_moveSection(&header, 0, 2);
        XAPI_EXPECT(XHeaderView_sectionSize(&header, 0) == 20 &&
                    XHeaderView_sectionSize(&header, 1) == 100 &&
                    XHeaderView_sectionSize(&header, 2) == 60,
                    "moveSection 只改视觉映射，尺寸仍按逻辑段号持有"
                    "（gap 157）");
        XAPI_EXPECT(XHeaderView_sectionPosition(&header, 1) == 0 &&
                    XHeaderView_sectionPosition(&header, 2) == 100 &&
                    XHeaderView_sectionPosition(&header, 0) == 160,
                    "moveSection(0,2) 后视觉序 [1,2,0]（Qt 6.8 探针"
                    " sectionPosition 同值）");
        XAPI_EXPECT(g_sig.hvSectionMoved == 1 && g_sig.hvGeometries >= 1,
                    "实际移动发射 sectionMoved+geometriesChanged");
        XHeaderView_swapSections(&header, 0, 1);
        XAPI_EXPECT(XHeaderView_sectionSize(&header, 0) == 20 &&
                    XHeaderView_sectionSize(&header, 1) == 100 &&
                    XHeaderView_sectionSize(&header, 2) == 60,
                    "swapSections 交换视觉槽，逻辑段尺寸不变");
        XAPI_EXPECT(XHeaderView_sectionPosition(&header, 2) == 0 &&
                    XHeaderView_sectionPosition(&header, 1) == 60 &&
                    XHeaderView_sectionPosition(&header, 0) == 160,
                    "swapSections(0,1) 后视觉序 [2,1,0]（Qt 6.8 探针）");
        XAPI_EXPECT(g_sig.hvSectionMoved == 3,
                    "swap 发射两次 sectionMoved（双向各一）");
        XAPI_EXPECT(XHeaderView_visualIndex(&header, 0) == 2 &&
                    XHeaderView_visualIndex(&header, 1) == 1 &&
                    XHeaderView_visualIndex(&header, 2) == 0 &&
                    XHeaderView_logicalIndex(&header, 0) == 2 &&
                    XHeaderView_logicalIndex(&header, 2) == 0,
                    "视觉映射取代移动即重排（visualIndex/logicalIndex 互逆）");
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
        /* ---- 模型接线（对标 Qt 表头连接模型 headerDataChanged →
         *      headerDataChanged 槽：模型表头数据落库驱动表头通知与
         *      重绘；断言经表头句柄计数承载，重绘效果不在断言职责） */
        {
            XAbstractItemModel* hModel = XAbstractItemModel_create();
            if (hModel) {
                XAbstractItemModel_setDimension(hModel, 1, 3);
                vsig_reset();
                XHeaderView_setModel(&header, hModel);
                XAbstractItemModel_setHeaderData_2(hModel, 1, 0, "接线列");
                XAPI_EXPECT(g_sig.hvHeaderData == 1 &&
                            g_sig.hvHDSection == 1,
                            "模型 setHeaderData 经接线驱动表头 headerDataChanged(1)");
                XHeaderView_setModel(&header, NULL);
                vsig_reset();
                XAbstractItemModel_setHeaderData_2(hModel, 1, 0, "解绑后");
                XAPI_EXPECT(g_sig.hvHeaderData == 0,
                            "解除关联后模型表头变化不再转发表头");
            }
            if (hModel) XClassDelete((XClass*)hModel);
        }
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
                if (state) XClassDelete((XClass*)state);
                if (bad) XClassDelete((XClass*)bad);
            }
            XClassDeinit(&donor);
            XClassDeinit(&acceptor);
        }
#endif /* XByteArray_ON */

        XClassDeinit(&header);
        XClassDeinit(&vertical);
    }

#else /* XWIDGET_ON && XTABLEWIDGET_ON */

/* 条目视图族整体裁剪时的非空翻译单元哨兵。 */
typedef int xgui_demo_apitest_views_disabled_sentinel;

#endif /* XWIDGET_ON && XTABLEWIDGET_ON */

    XPrintf("XGuiApiTest: [条目视图族 views] %s\n",
            failures == 0 ? "PASS" : "FAIL");
    return failures;
}
