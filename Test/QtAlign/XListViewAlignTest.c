/**
 * @file       XListViewAlignTest.c
 * @brief      XListView × Qt 6.8 QListView 对齐契约测试。
 * @details    对齐依据：
 *             - 头文件 /opt/Qt/6.8.3/gcc_64/include/QtWidgets/qlistview.h
 *               （movement/flow/isWrapping/resizeMode/layoutMode/spacing/
 *               gridSize/viewMode/modelColumn/uniformItemSizes/batchSize/
 *               wordWrap/selectionRectVisible/itemAlignment 等属性）；
 *             - 本机 Qt 6.8.0 实测：① 缺省 spacing=0、batchSize=100、
 *               movement=Static、flow=TopToBottom、resizeMode=Fixed、
 *               layoutMode=SinglePass、viewMode=ListMode、gridSize=(-1,-1)、
 *               isWrapping=false、uniformItemSizes=false、
 *               isSelectionRectVisible=false、wordWrap=false、
 *               itemAlignment=0；② setModelColumn 在**空模型**与**列越界**
 *               时均被拒（空模型 columnCount=0 ⇒ 任何列都越界）；
 *               ③ setRowHidden 越界行无效；④ 构造即 SingleSelection。
 *
 *             覆盖：缺省矩阵、setModelColumn 的 Qt 拒绝口径、行隐藏与几何
 *             （visualRect/indexAt 同源）、viewMode 的 modeProperties 门控、
 *             gridSize/spacing、movement 联动 dragEnabled、indexesMoved
 *             信号、父子所有权。
 * @note       C99；视图/模型全部释放，漏删即运行器判 `[LEAK]`。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

#if XGUI_ON && XWIDGET_ON && XTABLEWIDGET_ON

#include "XListView.h"
#include "XAbstractItemView.h"
#include "XAbstractItemModel.h"
#include "XWidget.h"

/* ==================== 信号探针 ==================== */

static int g_lvMovedHits;   /**< indexesMoved 命中次数。 */
static int g_lvMovedCount;  /**< indexesMoved 载荷行号个数。 */

/** @brief indexesMoved(rows,count) 槽。 */
static void xqLv_indexesMovedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    ++g_lvMovedHits;
    if (!args) return;
    XVarList_args_2(args, const int*, rows, int, count);
    (void)rows;
    g_lvMovedCount = count;
}

/* ==================== 用例 ==================== */

/** @brief 缺省值矩阵（Qt 6.8.0 实测逐项）。 */
XQ_TEST(XListView, defaults)
{
    XListView* lv = XListView_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL, 0);
    int w = -9;
    int h = -9;
    XQ_REQUIRE(lv != NULL);

    XQ_EQ_I64(XListView_spacing(lv), 0);
    XQ_EQ_I64(XListView_modelColumn(lv), 0);
    XQ_EQ_I64(XListView_rowHeight(lv), 24); /* 本库扩展：统一行高。 */
    XQ_EQ_I64(XListView_flow(lv), XListViewFlow_TopToBottom);
    XQ_EQ_I64(XListView_viewMode(lv), XListViewViewMode_ListMode);
    XQ_EQ_I64(XListView_resizeMode(lv), XListViewResizeMode_Fixed);
    XQ_EQ_I64(XListView_layoutMode(lv), XListViewLayoutMode_SinglePass);
    XQ_EQ_I64(XListView_movement(lv), XListViewMovement_Static);
    XQ_EQ_I64(XListView_batchSize(lv), 100);
    XQ_CHECK(!XListView_isWrapping(lv));
    XQ_CHECK(!XListView_uniformItemSizes(lv));
    XQ_CHECK(!XListView_isSelectionRectVisible(lv));
    XQ_CHECK(!XListView_wordWrap(lv));
    XQ_EQ_I64(XListView_itemAlignment(lv), 0);
    XListView_gridSize(lv, &w, &h);
    XQ_CHECK(w == -1 && h == -1); /* Qt QSize(-1,-1)=未启用。 */

    /* gap 148：值**原样存储**（Qt 6.8.0 实测 setGridSize(0,10)→(0,10)、
       (-5,-7)→(-5,-7)），不做正数归一；非正维在绘制/命中侧按未启用处理。 */
    XListView_setGridSize(lv, 0, 10);
    XListView_gridSize(lv, &w, &h);
    XQ_CHECK(w == 0 && h == 10);
    XListView_setGridSize(lv, -5, -7);
    XListView_gridSize(lv, &w, &h);
    XQ_CHECK(w == -5 && h == -7);
    /* 还原缺省（后续用例依赖“未启用网格”的槽位口径）。 */
    XListView_setGridSize(lv, -1, -1);

    /* 对标 Qt QListView 构造即 setSelectionMode(SingleSelection)
       （qlistview.cpp:149/162；基类缺省 ExtendedSelection）。 */
    XQ_EQ_I64(XAbstractItemView_selectionMode(&lv->m_base),
              XAbstractItemViewSelectionMode_SingleSelection);

    XClassDelete((XClass*)lv);
}

/** @brief setModelColumn 的 Qt 拒绝口径（空模型/列越界均不改值）。 */
XQ_TEST(XListView, modelColumnRejectsInvalid)
{
    XListView* lv = XListView_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL, 0);
    XAbstractItemModel* m1 = XAbstractItemModel_create();
    XAbstractItemModel* m2 = XAbstractItemModel_create();
    XQ_REQUIRE(lv != NULL && m1 != NULL && m2 != NULL);

    /* ① 空模型：Qt 6.8.0 实测 setModelColumn(1) 后 modelColumn() 仍 0
       （空模型 columnCount=0 ⇒ 任何列都越界）。 */
    XListView_setModelColumn(lv, 1);
    XQ_EQ_I64(XListView_modelColumn(lv), 0);

    /* ② 单列模型：列 1 越界被拒。 */
    XAbstractItemModel_setDimension(m1, 3, 1);
    XAbstractItemView_setModel(&lv->m_base, m1);
    XListView_setModelColumn(lv, 1);
    XQ_EQ_I64(XListView_modelColumn(lv), 0);

    /* ③ 三列模型：列 1 合法。 */
    XAbstractItemModel_setDimension(m2, 3, 3);
    XAbstractItemView_setModel(&lv->m_base, m2);
    XListView_setModelColumn(lv, 1);
    XQ_EQ_I64(XListView_modelColumn(lv), 1);
    /* 越界列（=列数）仍被拒，保持 1。 */
    XListView_setModelColumn(lv, 3);
    XQ_EQ_I64(XListView_modelColumn(lv), 1);
    /* 负列被拒。 */
    XListView_setModelColumn(lv, -1);
    XQ_EQ_I64(XListView_modelColumn(lv), 1);

    XClassDelete((XClass*)lv);
    XClassDelete((XClass*)m1);
    XClassDelete((XClass*)m2);
}

/** @brief 行隐藏与几何：visualRect/indexAt 同一槽位口径 + indexesMoved 信号。 */
XQ_TEST(XListView, rowHiddenAndGeometry)
{
    XListView* lv = XListView_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL, 0);
    XAbstractItemModel* m = XAbstractItemModel_create();
    XRect r;
    int row = -9;
    int col = -9;
    XQ_REQUIRE(lv != NULL && m != NULL);

    XWidget_resize((XWidget*)lv, 200, 120);
    XAbstractItemModel_setDimension(m, 4, 1);
    XAbstractItemView_setModel(&lv->m_base, m);

    /* 行带：槽高 = max(行高, 网格高) + 间距（缺省 24 + 0）。 */
    r = XListView_visualRect(lv, 0);
    XQ_CHECK(r.x == 0 && r.y == 0 && r.height == 24 && r.width == 200);
    r = XListView_visualRect(lv, 2);
    XQ_EQ_I64(r.y, 48);
    /* 基类虚槽（编辑器摆放/scrollTo 共用）只在渲染列（modelColumn）有几何。 */
    XQ_CHECK(XAbstractItemView_visualRect(&lv->m_base, 2, 0, &r));
    XQ_EQ_I64(r.y, 48);
    XQ_CHECK(!XAbstractItemView_visualRect(&lv->m_base, 2, 1, &r));

    /* 命中：y=30 落在第 2 行（0..24 行 0、24..48 行 1）。 */
    XQ_CHECK(XAbstractItemView_indexAt_base(&lv->m_base, 10, 30, &row, &col));
    XQ_EQ_I64(row, 1);
    XQ_EQ_I64(col, 0);

    /* 隐藏行不占槽位（对标 QListView::setRowHidden）：隐藏行 1 后，
       行 2 的 y 上移到 24。 */
    XListView_setRowHidden(lv, 1, true);
    XQ_CHECK(XListView_isRowHidden(lv, 1));
    r = XListView_visualRect(lv, 1);
    XQ_CHECK(r.width == 0 && r.height == 0); /* 隐藏行无矩形。 */
    r = XListView_visualRect(lv, 2);
    XQ_EQ_I64(r.y, 24);
    /* 越界行隐藏无效（Qt 6.8.0 实测 setRowHidden(5) 后 isRowHidden(5)=false）。 */
    XListView_setRowHidden(lv, 9, true);
    XQ_CHECK(!XListView_isRowHidden(lv, 9));
    XListView_setRowHidden(lv, 1, false);
    XQ_CHECK(!XListView_isRowHidden(lv, 1));

    /* indexesMoved 信号（QListView::indexesMoved 的对标发射口）。 */
    g_lvMovedHits = 0;
    g_lvMovedCount = -1;
    XObject_connect_1((XObject*)lv, XSignal(XListView_indexesMoved_signal),
                      (XObject*)lv, xqLv_indexesMovedSlot,
                      XConnectionType_Direct);
    {
        int rows[2];
        rows[0] = 0;
        rows[1] = 2;
        XListView_indexesMoved_signal(lv, rows, 2);
    }
    XQ_EQ_I64(g_lvMovedHits, 1);
    XQ_EQ_I64(g_lvMovedCount, 2);

    XClassDelete((XClass*)lv);
    XClassDelete((XClass*)m);
}

/** @brief viewMode 的 modeProperties 门控与网格/移动属性往返。 */
XQ_TEST(XListView, viewModeAndProperties)
{
    XListView* lv = XListView_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL, 0);
    int w = -9;
    int h = -9;
    XQ_REQUIRE(lv != NULL);

    /* ListMode → IconMode：按 Qt qlistview.cpp:422-471 复位未显式设置的
       属性（IconMode：wrapping=true、flow=LeftToRight、movement=Free、
       selectionRectVisible=true）。 */
    XListView_setViewMode(lv, XListViewViewMode_IconMode);
    XQ_EQ_I64(XListView_viewMode(lv), XListViewViewMode_IconMode);
    XQ_CHECK(XListView_isWrapping(lv));
    XQ_EQ_I64(XListView_flow(lv), XListViewFlow_LeftToRight);
    XQ_EQ_I64(XListView_movement(lv), XListViewMovement_Free);
    XQ_CHECK(XListView_isSelectionRectVisible(lv));
    /* movement != Static 联动 dragEnabled（qlistview.cpp:465-469）。 */
    XQ_CHECK(XAbstractItemView_dragEnabled(&lv->m_base));

    /* 回 ListMode：wrapping=false、flow=TopToBottom、movement=Static。 */
    XListView_setViewMode(lv, XListViewViewMode_ListMode);
    XQ_CHECK(!XListView_isWrapping(lv));
    XQ_EQ_I64(XListView_flow(lv), XListViewFlow_TopToBottom);
    XQ_EQ_I64(XListView_movement(lv), XListViewMovement_Static);

    /* 显式设置过的属性被 modeProperties 记住，切模式不复位。 */
    XListView_setFlow(lv, XListViewFlow_LeftToRight);
    XListView_setViewMode(lv, XListViewViewMode_IconMode);
    XListView_setViewMode(lv, XListViewViewMode_ListMode);
    XQ_EQ_I64(XListView_flow(lv), XListViewFlow_LeftToRight);
    /* clearPropertyFlags 清掉标志 ⇒ 下次切模式重新复位。 */
    XListView_clearPropertyFlags(lv);
    XListView_setViewMode(lv, XListViewViewMode_IconMode);
    XListView_setViewMode(lv, XListViewViewMode_ListMode);
    XQ_EQ_I64(XListView_flow(lv), XListViewFlow_TopToBottom);

    /* 间距：Qt setSpacing 无校验（实测 spacing(-3) 存 -3）。 */
    XListView_setSpacing(lv, 5);
    XQ_EQ_I64(XListView_spacing(lv), 5);
    XListView_setSpacing(lv, -3);
    XQ_EQ_I64(XListView_spacing(lv), -3);
    XListView_setSpacing(lv, 0);

    /* 网格：高参与槽位（取与行高较大者），宽收缩单元。 */
    XListView_setGridSize(lv, 60, 30);
    XListView_gridSize(lv, &w, &h);
    XQ_CHECK(w == 60 && h == 30);

    /* 批量：<=0 拒绝（Qt setBatchSize 拒绝非正值）。 */
    XListView_setBatchSize(lv, 0);
    XQ_EQ_I64(XListView_batchSize(lv), 100);
    XListView_setBatchSize(lv, 7);
    XQ_EQ_I64(XListView_batchSize(lv), 7);

    /* 行高/对齐/换行往返。 */
    XListView_setRowHeight(lv, 18);
    XQ_EQ_I64(XListView_rowHeight(lv), 18);
    XListView_setRowHeight(lv, 0); /* 非正拒绝。 */
    XQ_EQ_I64(XListView_rowHeight(lv), 18);
    XListView_setItemAlignment(lv, 0x80 | 0x02); /* AlignHCenter|AlignRight 之类位组合。 */
    XQ_EQ_I64(XListView_itemAlignment(lv), 0x82);
    XListView_setWordWrap(lv, true);
    XQ_CHECK(XListView_wordWrap(lv));

    XClassDelete((XClass*)lv);
}

/** @brief 父子所有权：列表视图随父控件析构。 */
XQ_TEST(XListView, parentOwnership)
{
    XWidget* host = XWidget_create(NULL, 0);
    XListView* lv;
    XQ_REQUIRE(host != NULL);

    lv = XListView_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, host, 0);
    XQ_REQUIRE(lv != NULL);
    XQ_CHECK(XWidget_parentWidget((XWidget*)lv) == host);
    XQ_CHECK(XWidget_isAncestorOf(host, (XWidget*)lv));
    XQ_CHECK(XAbstractItemView_model(&lv->m_base) == NULL);
    /* 子控件随 host 析构，漏删即判 [LEAK]。 */
    XClassDelete((XClass*)host);
}

#endif /* XGUI_ON && XWIDGET_ON && XTABLEWIDGET_ON */
