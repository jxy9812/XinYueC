/**
 * @file       XTreeViewAlignTest.c
 * @brief      XTreeView × Qt 6.8 QTreeView 对齐契约测试。
 * @details    对齐依据：
 *             - 头文件 /opt/Qt/6.8.3/gcc_64/include/QtWidgets/qtreeview.h
 *               （indentation/resetIndentation/rootIsDecorated/
 *               uniformRowHeights/itemsExpandable/expandsOnDoubleClick/
 *               sortingEnabled/animated/allColumnsShowFocus/wordWrap/
 *               headerHidden/treePosition/autoExpandDelay、
 *               expand/collapse/setExpanded/isExpanded/expandAll/
 *               collapseAll/expandToDepth/indexAbove/indexBelow、
 *               setColumnHidden/isColumnHidden/columnWidth/setColumnWidth/
 *               columnAt/columnViewportPosition/resizeColumnToContents、
 *               setHeader/header、setRowHidden/isRowHidden、
 *               expanded/collapsed/columnResized/columnCountChanged 信号）；
 *             - 本机 Qt 6.8.0 实测：缺省 indentation=20（resetIndentation
 *               同样回到 20）、headerHidden=false、rootIsDecorated=true、
 *               expandsOnDoubleClick=true、itemsExpandable=true、
 *               animated=false、uniformRowHeights=false、wordWrap=false、
 *               allColumnsShowFocus=false、treePosition=0、
 *               autoExpandDelay=-1、sortingEnabled=false；构造即持有
 *               header()（本库为外部借用，差异见本批 gaps）。
 *
 *             覆盖：缺省矩阵、展开/折叠（信号次数与幂等）、expandAll/
 *             collapseAll 的批量口径、列隐藏/列宽与几何反查、表头挂接
 *             与可见性镜像、行隐藏、indexAbove/indexBelow、父子所有权。
 * @note       C99；视图/模型/表头全部释放，漏删即运行器判 `[LEAK]`。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

#if XGUI_ON && XWIDGET_ON && XTABLEWIDGET_ON

#include "XTreeView.h"
#include "XAbstractItemView.h"
#include "XAbstractItemModel.h"
#include "XHeaderView.h"
#include "XWidget.h"

/* ==================== 信号探针 ==================== */

static int g_tvExpandedHits;  /**< expanded 命中次数。 */
static int g_tvCollapsedHits; /**< collapsed 命中次数。 */
static int g_tvLastRow;       /**< 最近一次 expanded/collapsed 载荷行号。 */
static int g_tvResizedHits;   /**< columnResized 命中次数。 */
static int g_tvResizedCol;    /**< 最近一次 columnResized 列号。 */
static int g_tvResizedNew;    /**< 最近一次 columnResized 新宽。 */
static int g_tvCountHits;     /**< columnCountChanged 命中次数。 */

/** @brief expanded(row) 槽。 */
static void xqTv_expandedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    ++g_tvExpandedHits;
    if (args) {
        XVarList_args_1(args, int, row);
        g_tvLastRow = row;
    }
}

/** @brief collapsed(row) 槽。 */
static void xqTv_collapsedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    ++g_tvCollapsedHits;
    if (args) {
        XVarList_args_1(args, int, row);
        g_tvLastRow = row;
    }
}

/** @brief columnResized(column,oldSize,newSize) 槽。 */
static void xqTv_columnResizedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    ++g_tvResizedHits;
    if (args) {
        XVarList_args_3(args, int, column, int, oldSize, int, newSize);
        (void)oldSize;
        g_tvResizedCol = column;
        g_tvResizedNew = newSize;
    }
}

/** @brief columnCountChanged(old,new) 槽。 */
static void xqTv_columnCountSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++g_tvCountHits;
}

/* ==================== 用例 ==================== */

/** @brief 缺省值矩阵（Qt 6.8.0 实测逐项）。 */
XQ_TEST(XTreeView, defaults)
{
    XTreeView* tv = XTreeView_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL, 0);
    XQ_REQUIRE(tv != NULL);

    XQ_EQ_I64(XTreeView_indentation(tv), 20);
    XQ_CHECK(!XTreeView_isHeaderHidden(tv));
    XQ_EQ_I64(XTreeView_rowHeight(tv), 24); /* 本库扩展：统一行高。 */
    XQ_CHECK(XTreeView_expandsOnDoubleClick(tv));
    XQ_CHECK(XTreeView_itemsExpandable(tv));
    XQ_CHECK(XTreeView_rootIsDecorated(tv));
    XQ_CHECK(!XTreeView_isSortingEnabled(tv));
    XQ_CHECK(!XTreeView_uniformRowHeights(tv));
    XQ_CHECK(!XTreeView_isAnimated(tv));
    XQ_CHECK(!XTreeView_wordWrap(tv));
    XQ_CHECK(!XTreeView_allColumnsShowFocus(tv));
    XQ_CHECK(!XTreeView_isSelectionRectVisible(tv));
    XQ_EQ_I64(XTreeView_treePosition(tv), 0);
    XQ_EQ_I64(XTreeView_autoExpandDelay(tv), -1);
    XQ_EQ_I64(XTreeView_sortColumn(tv), -1); /* 本库「未排序」哨兵（Qt 无 sortColumn，
                                                QTreeWidget 缺省 0；见 gaps）。 */
    /* 无跨列合并、无外部表头时可安全查询。 */
    XQ_CHECK(!XTreeView_isFirstColumnSpanned(tv, 0));
    /* gap 138：构造即持有表头（Qt QTreeViewPrivate::initialize 建
       QHeaderView(Qt::Horizontal, q)；实测 header() 非空）。本库返回内建
       缺省表头（对象归视图，随视图析构）。 */
    XQ_CHECK_PTR(XTreeView_header(tv));
    XQ_EQ_I64(((XHeaderView*)XTreeView_header(tv))->m_orientation, 0);

    /* resetIndentation 回到缺省 20（Qt 6.8.0 实测同值）。 */
    XTreeView_setIndentation(tv, 33);
    XQ_EQ_I64(XTreeView_indentation(tv), 33);
    XTreeView_resetIndentation(tv);
    XQ_EQ_I64(XTreeView_indentation(tv), 20);

    XClassDelete((XClass*)tv);
}

/** @brief 展开/折叠：信号次数、幂等与批量口径。 */
XQ_TEST(XTreeView, expandCollapse)
{
    XTreeView* tv = XTreeView_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL, 0);
    XAbstractItemModel* m = XAbstractItemModel_create();
    XQ_REQUIRE(tv != NULL && m != NULL);

    XAbstractItemModel_setDimension(m, 3, 2);
    XAbstractItemView_setModel(&tv->m_base, m);
    XObject_connect_1((XObject*)tv, XSignal(XTreeView_expanded_signal),
                      (XObject*)tv, xqTv_expandedSlot,
                      XConnectionType_Direct);
    XObject_connect_1((XObject*)tv, XSignal(XTreeView_collapsed_signal),
                      (XObject*)tv, xqTv_collapsedSlot,
                      XConnectionType_Direct);
    g_tvExpandedHits = 0;
    g_tvCollapsedHits = 0;
    g_tvLastRow = -1;

    /* 缺省折叠（本库行状态表初值 false）。 */
    XQ_CHECK(!XTreeView_isExpanded(tv, 0));

    /* expand → 发 expanded(0)；重复 expand 幂等不重发。 */
    XTreeView_expand(tv, 0);
    XQ_CHECK(XTreeView_isExpanded(tv, 0));
    XQ_EQ_I64(g_tvExpandedHits, 1);
    XQ_EQ_I64(g_tvLastRow, 0);
    XTreeView_expand(tv, 0);
    XQ_EQ_I64(g_tvExpandedHits, 1);

    /* collapse → 发 collapsed(0)；重复幂等。 */
    XTreeView_collapse(tv, 0);
    XQ_CHECK(!XTreeView_isExpanded(tv, 0));
    XQ_EQ_I64(g_tvCollapsedHits, 1);
    XTreeView_collapse(tv, 0);
    XQ_EQ_I64(g_tvCollapsedHits, 1);

    /* setExpanded 双向等价于 expand/collapse。 */
    XTreeView_setExpanded(tv, 1, true);
    XQ_CHECK(XTreeView_isExpanded(tv, 1));
    XQ_EQ_I64(g_tvExpandedHits, 2);
    XTreeView_setExpanded(tv, 1, false);
    XQ_CHECK(!XTreeView_isExpanded(tv, 1));
    XQ_EQ_I64(g_tvCollapsedHits, 2);

    /* expandAll/collapseAll/expandToDepth：批量置位，**不逐行发单行信号**
       （对标 Qt expandAll/collapseAll）。 */
    XTreeView_expandAll(tv);
    XQ_CHECK(XTreeView_isExpanded(tv, 0) && XTreeView_isExpanded(tv, 1) &&
             XTreeView_isExpanded(tv, 2));
    XQ_EQ_I64(g_tvExpandedHits, 2);
    XTreeView_collapseAll(tv);
    XQ_CHECK(!XTreeView_isExpanded(tv, 0) && !XTreeView_isExpanded(tv, 2));
    XQ_EQ_I64(g_tvCollapsedHits, 2);
    XTreeView_expandToDepth(tv, 1);
    XQ_CHECK(XTreeView_isExpanded(tv, 2));

    /* 越界行安全。 */
    XTreeView_expand(tv, 99);
    XQ_CHECK(!XTreeView_isExpanded(tv, 99));

    XClassDelete((XClass*)tv);
    XClassDelete((XClass*)m);
}

/** @brief 列状态/几何/表头挂接：列隐藏、列宽、反查与信号。 */
XQ_TEST(XTreeView, columnsAndHeader)
{
    XTreeView* tv = XTreeView_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL, 0);
    XAbstractItemModel* m = XAbstractItemModel_create();
    XHeaderView* header;
    XRect r;
    XQ_REQUIRE(tv != NULL && m != NULL);

    XWidget_resize((XWidget*)tv, 300, 120);
    XAbstractItemModel_setDimension(m, 3, 3);
    XAbstractItemView_setModel(&tv->m_base, m);
    XObject_connect_1((XObject*)tv, XSignal(XTreeView_columnResized_signal),
                      (XObject*)tv, xqTv_columnResizedSlot,
                      XConnectionType_Direct);
    XObject_connect_1((XObject*)tv,
                      XSignal(XTreeView_columnCountChanged_signal),
                      (XObject*)tv, xqTv_columnCountSlot,
                      XConnectionType_Direct);
    g_tvResizedHits = 0;
    g_tvCountHits = 0;

    /* 列宽：显式列宽优先；0 = 恢复自动铺满；负值拒绝。 */
    XTreeView_setColumnWidth(tv, 0, 100);
    XQ_EQ_I64(XTreeView_columnWidth(tv, 0), 100);
    XQ_EQ_I64(g_tvResizedHits, 1);
    XQ_EQ_I64(g_tvResizedCol, 0);
    XQ_EQ_I64(g_tvResizedNew, 100);
    XTreeView_setColumnWidth(tv, 0, -5);
    XQ_EQ_I64(XTreeView_columnWidth(tv, 0), 100);
    XQ_EQ_I64(g_tvResizedHits, 1);

    /* 列起点/命中：列 0 宽 100 ⇒ 列 1 起点 100（其余列均摊剩余 200/2=100）。 */
    XQ_EQ_I64(XTreeView_columnViewportPosition(tv, 0), 0);
    XQ_EQ_I64(XTreeView_columnViewportPosition(tv, 1), 100);
    XQ_EQ_I64(XTreeView_columnAt(tv, 50), 0);
    XQ_EQ_I64(XTreeView_columnAt(tv, 150), 1);
    XQ_EQ_I64(XTreeView_columnAt(tv, -1), -1);

    /* 行几何：表头占 20px，行高 24（y = 20 + row*24 - 滚动偏移）。 */
    r = XTreeView_visualRect(tv, 0, 0);
    XQ_CHECK(r.x == 0 && r.y == 20 && r.width == 100 && r.height == 24);
    r = XTreeView_visualRect(tv, 1, 1);
    XQ_CHECK(r.x == 100 && r.y == 44 && r.height == 24);
    XQ_EQ_I64(XTreeView_rowAt(tv, 30), 0);
    XQ_EQ_I64(XTreeView_rowAt(tv, 60), 1);
    XQ_EQ_I64(XTreeView_rowAt(tv, 5), -1); /* 表头区不命中行。 */

    /* 列隐藏：隐藏列不参与 x 向累计（宽度按 0 计）。 */
    XTreeView_setColumnHidden(tv, 0, true);
    XQ_CHECK(XTreeView_isColumnHidden(tv, 0));
    XQ_EQ_I64(XTreeView_columnViewportPosition(tv, 1), 0);
    r = XTreeView_visualRect(tv, 0, 0);
    XQ_CHECK(r.width == 0 && r.height == 0);
    XTreeView_hideColumn(tv, 2);
    XQ_CHECK(XTreeView_isColumnHidden(tv, 2));
    XTreeView_showColumn(tv, 2);
    XQ_CHECK(!XTreeView_isColumnHidden(tv, 2));
    XTreeView_showColumn(tv, 0);
    XQ_CHECK(!XTreeView_isColumnHidden(tv, 0));

    /* 表头挂接（对标 setHeader/header）：外部表头借用；headerHidden 镜像
       可见性（内建缺省表头恒隐藏——本库表头带自绘）。 */
    header = XHeaderView_create(NULL, 0, 0); /* 0=水平。 */
    XQ_REQUIRE(header != NULL);
    XTreeView_setHeader(tv, header);
    XQ_CHECK(XTreeView_header(tv) == header);
    XTreeView_setHeaderHidden(tv, true);
    XQ_CHECK(XTreeView_isHeaderHidden(tv));
    XQ_CHECK(XWidget_isHidden((XWidget*)header));
    XTreeView_setHeaderHidden(tv, false);
    XQ_CHECK(!XWidget_isHidden((XWidget*)header));
    /* 行几何随表头显隐变化。 */
    r = XTreeView_visualRect(tv, 0, 0);
    XQ_EQ_I64(r.y, 20);
    XTreeView_setHeaderHidden(tv, true);
    r = XTreeView_visualRect(tv, 0, 0);
    XQ_EQ_I64(r.y, 0);
    /* 方向非水平（1=垂直）的表头被拒（Qt 断言水平方向的收敛）。 */
    {
        XHeaderView* vh = XHeaderView_create(NULL, 0, 1);
        XQ_REQUIRE(vh != NULL);
        XTreeView_setHeader(tv, vh);
        XQ_CHECK(XTreeView_header(tv) == header);
        XClassDelete((XClass*)vh);
    }
    /* 表头为借用：视图不删除，调用方释放。setHeader(NULL) 按 Qt 语义
       **不会**留下空 header（Qt 源码 `if (header == d->header || !header)
       return;`）——本库 C 借用模型下恢复内建缺省表头，避免外部表头释放后
       视图悬垂。先 detach 再释放，保证视图不持有已释放指针。 */
    XTreeView_setHeader(tv, NULL);
    XQ_CHECK(XTreeView_header(tv) != NULL);
    XQ_CHECK(XTreeView_header(tv) != header);
    XClassDelete((XClass*)header);

    /* indexAbove/indexBelow：平铺行序 row∓1。 */
    XQ_EQ_I64(XTreeView_indexAbove(tv, 2), 1);
    XQ_EQ_I64(XTreeView_indexAbove(tv, 0), -1);
    XQ_EQ_I64(XTreeView_indexBelow(tv, 0), 1);
    XQ_EQ_I64(XTreeView_indexBelow(tv, 2), -1);

    XClassDelete((XClass*)tv);
    XClassDelete((XClass*)m);
}

/** @brief 行隐藏、属性往返与父子所有权。 */
XQ_TEST(XTreeView, rowHiddenPropsAndOwnership)
{
    XWidget* host = XWidget_create(NULL, 0);
    XTreeView* tv;
    XAbstractItemModel* m;
    XQ_REQUIRE(host != NULL);

    tv = XTreeView_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, host, 0);
    m = XAbstractItemModel_create();
    XQ_REQUIRE(tv != NULL && m != NULL);
    XQ_CHECK(XWidget_parentWidget((XWidget*)tv) == host);
    XQ_CHECK(XWidget_isAncestorOf(host, (XWidget*)tv));

    XWidget_resize((XWidget*)tv, 200, 120);
    XAbstractItemModel_setDimension(m, 3, 1);
    XAbstractItemView_setModel(&tv->m_base, m);

    /* 行隐藏（Qt setRowHidden/isRowHidden）；越界与负值安全。 */
    XQ_CHECK(!XTreeView_isRowHidden(tv, 1));
    XTreeView_setRowHidden(tv, 1, true);
    XQ_CHECK(XTreeView_isRowHidden(tv, 1));
    XQ_CHECK(!XTreeView_isRowHidden(tv, 9));
    XQ_CHECK(!XTreeView_isRowHidden(tv, -1));
    XTreeView_setRowHidden(tv, 1, false);
    XQ_CHECK(!XTreeView_isRowHidden(tv, 1));

    /* 状态属性往返。 */
    XTreeView_setExpandsOnDoubleClick(tv, false);
    XQ_CHECK(!XTreeView_expandsOnDoubleClick(tv));
    XTreeView_setItemsExpandable(tv, false);
    XQ_CHECK(!XTreeView_itemsExpandable(tv));
    XTreeView_setRootIsDecorated(tv, false);
    XQ_CHECK(!XTreeView_rootIsDecorated(tv));
    XTreeView_setUniformRowHeights(tv, true);
    XQ_CHECK(XTreeView_uniformRowHeights(tv));
    XTreeView_setAnimated(tv, true);
    XQ_CHECK(XTreeView_isAnimated(tv));
    XTreeView_setWordWrap(tv, true);
    XQ_CHECK(XTreeView_wordWrap(tv));
    XTreeView_setAllColumnsShowFocus(tv, true);
    XQ_CHECK(XTreeView_allColumnsShowFocus(tv));
    XTreeView_setSelectionRectVisible(tv, true);
    XQ_CHECK(XTreeView_isSelectionRectVisible(tv));
    XTreeView_setTreePosition(tv, -1);
    XQ_EQ_I64(XTreeView_treePosition(tv), -1);
    XTreeView_setAutoExpandDelay(tv, 300);
    XQ_EQ_I64(XTreeView_autoExpandDelay(tv), 300);
    XTreeView_setRowHeight(tv, 20);
    XQ_EQ_I64(XTreeView_rowHeight(tv), 20);

    /* 视图不拥有模型：模型由调用方释放（Qt 同）。 */
    XClassDelete((XClass*)host);
    XClassDelete((XClass*)m);
}

/** @brief setModel 复位展开/行隐藏/列隐藏/列宽（gap 141）。
 *
 *  对齐 Qt 6.8.0 qtreeview.cpp:179-198：QTreeView::setModel 清
 *  viewItems/expandedIndexes/hiddenIndexes 并 header->setModel(model)；
 *  同模型重复设置首行早退（状态保持）。
 */
XQ_TEST(XTreeView, setModelClearsViewState)
{
    XTreeView* tv = XTreeView_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL, 0);
    XAbstractItemModel* m1 = XAbstractItemModel_create();
    XAbstractItemModel* m2 = XAbstractItemModel_create();
    XQ_REQUIRE(tv != NULL && m1 != NULL && m2 != NULL);

    XWidget_resize((XWidget*)tv, 300, 120);
    XAbstractItemModel_setDimension(m1, 3, 3);
    XAbstractItemModel_setDimension(m2, 2, 2);
    XAbstractItemView_setModel(&tv->m_base, m1);

    /* 建立非缺省视图状态。 */
    XTreeView_expand(tv, 1);
    XTreeView_setRowHidden(tv, 2, true);
    XTreeView_setColumnHidden(tv, 1, true);
    XTreeView_setColumnWidth(tv, 0, 77);
    XQ_CHECK(XTreeView_isExpanded(tv, 1));
    XQ_CHECK(XTreeView_isRowHidden(tv, 2));
    XQ_CHECK(XTreeView_isColumnHidden(tv, 1));
    XQ_EQ_I64(XTreeView_columnWidth(tv, 0), 77);

    /* 同模型重复设置：Qt 首行早退 ⇒ 状态保持。 */
    XAbstractItemView_setModel(&tv->m_base, m1);
    XQ_CHECK(XTreeView_isExpanded(tv, 1));
    XQ_CHECK(XTreeView_isRowHidden(tv, 2));
    XQ_CHECK(XTreeView_isColumnHidden(tv, 1));
    XQ_EQ_I64(XTreeView_columnWidth(tv, 0), 77);

    /* 换模型：展开/行隐藏/列隐藏/列宽全部复位（基类当前索引/选择
       复位已有独立用例覆盖）。 */
    XAbstractItemView_setModel(&tv->m_base, m2);
    XQ_CHECK(XAbstractItemView_model(&tv->m_base) == m2);
    XQ_CHECK(!XTreeView_isExpanded(tv, 1));
    XQ_CHECK(!XTreeView_isRowHidden(tv, 2));
    XQ_CHECK(!XTreeView_isColumnHidden(tv, 1));
    XQ_EQ_I64(XTreeView_columnWidth(tv, 0), 0);

    XClassDelete((XClass*)tv);
    XClassDelete((XClass*)m1);
    XClassDelete((XClass*)m2);
}

#endif /* XGUI_ON && XWIDGET_ON && XTABLEWIDGET_ON */
