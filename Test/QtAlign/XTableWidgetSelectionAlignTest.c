/**
 * @file       XTableWidgetSelectionAlignTest.c
 * @brief      XTableWidget selectionModel 双向桥 × Qt 6.8 对齐契约测试（§B4）。
 * @details    对齐依据：
 *             - 原文 qtablewidget.cpp：
 *               · `QTableWidgetItem::isSelected()` 直读
 *                 `view->selectionModel()->isSelected(index)`
 *                 （qtablewidget.cpp:1087-1096）；
 *               · `QTableWidgetItem::setSelected(bool)` 直写
 *                 `view->selectionModel()->select(index, Select|Deselect)`
 *                 （qtablewidget.cpp:1100-1112）；
 *               · `QTableWidget::setCurrentCell(r,c,command)` 走
 *                 `d->selectionModel->setCurrentIndex(...)`
 *                 （qtablewidget.cpp:2237-2242）；
 *               · `QTableWidget::setRangeSelected` 经
 *                 `selectionModel()->select(QItemSelection(tl, br), ...)`
 *                 （qtablewidget.cpp:2362-2373），倒置角由
 *                 QItemSelection::select 归一（qitemselectionmodel.cpp:387-406）；
 *               · `selectedRanges()` 直接返回
 *                 `selectionModel()->selection()` 的真实范围（qtablewidget.cpp
 *                 :2381-2392，**不补洞**）；
 *               · `selectedItems()` 取 `selectedIndexes()` 并跳过隐藏索引
 *                 与空格（qtablewidget.cpp:2408-2422）；
 *               · `findItems(text, flags)` 逐列 match，flags 按
 *                 Qt::MatchFlag 位语义（qtablewidget.cpp:2424-2437、
 *                 qnamespace.h:1530-1541、qabstractitemmodel.cpp:2513-2611）。
 *             - 本机 Qt 6.8.3 运行时实测（QT_QPA_PLATFORM=offscreen，探针
 *               程序见提交说明）：
 *               ① 直接 `selectionModel()->select()` 后
 *                  `item(r,c)->isSelected()==true`、`selectedItems()==1`；
 *               ② `item->setSelected(true)` 发 `itemSelectionChanged` 一次；
 *                  `setSelected(false)` 在未选中格上信号数 0（幂等早退）；
 *               ③ 一次 `setCurrentCell(1,1)` 的信号次序为
 *                  itemSelectionChanged → currentItemChanged →
 *                  currentCellChanged；
 *               ④ 洞形选区（3×3 去掉中心）⇒ `selectedRanges()` **4 条**；
 *                  选 (0,0)+(0,2) ⇒ 2 条（不补洞）；2×2 全选 ⇒ 1 条；
 *               ⑤ `setRangeSelected(2,2,1,1)`（倒置）⇒ 归一后选中 4 格；
 *               ⑥ `findItems("", MatchContains)`=6（含空格）、
 *                  `findItems("", MatchExactly)`=0；
 *                  Contains("Alpha")=3 / +CS=2；StartsWith=2 / +CS=1；
 *                  EndsWith=2；Exactly("Alpha")=1、Exactly("alpha")=0；
 *                  FixedString("alpha")=1、FixedString|CS=0。
 *
 *             覆盖：模型→条目镜像、条目→模型、setCurrentCell 三信号次序、
 *             setRangeSelected（含倒置归一）、selectedRanges 不补洞、
 *             selectedItems、findItems 的 MatchFlags 全矩阵、防回环闸、
 *             行列增删后的选区重映射、以及既有 K-69/K-73/Stretch 视觉管线
 *             不回归（表头高亮/末段余数仍按选择模型读）。
 *             [B15perf 2026-11-17] 追加：sortingEnabled 下 setItem 触发行
 *             搬移后的选区/当前格持久迁移（选择模型 + 离屏像素双证、迁移
 *             静默不发信号、向下/向上两个方向）、sortItems 稳定排序
 *             （等键保序、空条目稳定沉底）、500 行排序相对旧 O(R²) 实现的
 *             耗时探针（不慢于 2 倍基线）。
 * @note       C99；表格/条目全部释放，漏删即运行器判 `[LEAK]`。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include <stdio.h>
#include <string.h>
#include <time.h> /* clock()：500 行排序性能探针（与 XSelectionRangeAlignTest 同口径）。 */

#include "XGuiConfig.h"

#if XGUI_ON && XWIDGET_ON && XTABLEWIDGET_ON

#include "XTableWidget.h"
#include "XAbstractItemView.h"
#include "XItemSelectionModel.h"
#include "XHeaderView.h" /* 排序指示段（sortedInsert 的门闸输入）。 */
#include "XImage.h"      /* XWidget_grab 离屏像素（选中底双证）。 */
#include "XPalette.h"    /* 探针调色板：Highlight 置可判色。 */
#include "XColor.h"
#include "XMemory.h"     /* 性能探针本地数组走框架分配器（泄漏门计账）。 */

/* ==================== 信号探针 ==================== */

static int g_twselSelHits;     /**< itemSelectionChanged 命中次数。 */
static int g_twselCurItemHits; /**< currentItemChanged 命中次数。 */
static int g_twselCurCellHits; /**< currentCellChanged 命中次数。 */
static int g_twselCurCellRow;  /**< 最近 currentCellChanged 的当前行。 */
static char g_twselOrder[64];  /**< 信号次序串（i=选区、I=当前项、C=当前格）。 */

/** @brief itemSelectionChanged() 槽。 */
static void xqTwSel_selSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++g_twselSelHits;
    if (strlen(g_twselOrder) + 2 < sizeof(g_twselOrder))
        strcat(g_twselOrder, "i");
}

/** @brief currentItemChanged(cur,prev) 槽。 */
static void xqTwSel_curItemSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++g_twselCurItemHits;
    if (strlen(g_twselOrder) + 2 < sizeof(g_twselOrder))
        strcat(g_twselOrder, "I");
}

/** @brief currentCellChanged(curRow,curCol,prevRow,prevCol) 槽。 */
static void xqTwSel_curCellSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    ++g_twselCurCellHits;
    if (!args) return;
    {
        XVarList_args_4(args, int, vr, int, vc, int, pr, int, pc);
        (void)vc;
        (void)pr;
        (void)pc;
        g_twselCurCellRow = vr;
    }
    if (strlen(g_twselOrder) + 2 < sizeof(g_twselOrder))
        strcat(g_twselOrder, "C");
}

/** @brief 复位全部探针。 */
static void xqTwSel_resetProbes(void)
{
    g_twselSelHits = 0;
    g_twselCurItemHits = 0;
    g_twselCurCellHits = 0;
    g_twselCurCellRow = -9;
    g_twselOrder[0] = '\0';
}

/** @brief 挂接三个信号探针（缺省 Direct）。 */
static void xqTwSel_attach(XTableWidget* tw)
{
    XObject_connect((XObject*)tw, XSignal(XTableWidget_itemSelectionChanged_signal),
                    (XObject*)tw, xqTwSel_selSlot, XConnectionType_Direct);
    XObject_connect((XObject*)tw, XSignal(XTableWidget_currentItemChanged_signal),
                    (XObject*)tw, xqTwSel_curItemSlot, XConnectionType_Direct);
    XObject_connect((XObject*)tw, XSignal(XTableWidget_currentCellChanged_signal),
                    (XObject*)tw, xqTwSel_curCellSlot, XConnectionType_Direct);
}

/** @brief 建 3x3、每格都有条目的表格（文本 "r<c>"）。 */
static XTableWidget* xqTwSel_make3x3(void)
{
    XTableWidget* tw = XTableWidget_create(NULL, 0);
    int r;
    int c;
    if (!tw) return NULL;
    XTableWidget_setRowCount(tw, 3);
    XTableWidget_setColumnCount(tw, 3);
    for (r = 0; r < 3; ++r) {
        for (c = 0; c < 3; ++c) {
            char buf[8];
            XTableWidgetItem* it;
            snprintf(buf, sizeof(buf), "r%dc%d", r, c);
            it = XTableWidgetItem_create_utf8(buf, XTableWidgetItem_Type);
            if (it) XTableWidget_setItem(tw, r, c, it);
        }
    }
    return tw;
}

/* ==================== ① 模型 → 条目镜像 ==================== */

/** @brief 直接经选择模型选中 → 条目 isSelected/渲染位/selectedItems 一致。 */
XQ_TEST(XTableWidgetSelection, modelToItemMirror)
{
    XTableWidget* tw = xqTwSel_make3x3();
    XItemSelectionModel* sm;
    XTableWidgetItem* sel[8];
    XQ_REQUIRE(tw != NULL);

    sm = XAbstractItemView_selectionModel(&tw->m_base.m_base);
    XQ_REQUIRE(sm != NULL);
    /* 选择模型必须已绑定本表内建模型（否则 select 是空操作）。 */
    XQ_CHECK(XItemSelectionModel_model(sm) == XTableWidget_model(tw));

    /* Qt 实测：直接 selectionModel()->select() 后 item->isSelected()==true。 */
    XQ_CHECK(XItemSelectionModel_select(
        sm, 2, 2, XItemSelectionModelSelectionFlag_ClearAndSelect));
    XQ_CHECK(XTableWidgetItem_isSelected(XTableWidget_item(tw, 2, 2)));
    XQ_CHECK(!XTableWidgetItem_isSelected(XTableWidget_item(tw, 0, 0)));
    XQ_EQ_I64(XTableWidget_selectedItems(tw, sel, 8), 1);
    XQ_CHECK(sel[0] == XTableWidget_item(tw, 2, 2));

    /* 再补选一格 ⇒ 两格都在镜像里。 */
    XQ_CHECK(XItemSelectionModel_select(
        sm, 0, 1, XItemSelectionModelSelectionFlag_Select));
    XQ_CHECK(XTableWidgetItem_isSelected(XTableWidget_item(tw, 0, 1)));
    XQ_EQ_I64(XTableWidget_selectedItems(tw, NULL, 0), 2);

    /* 清空 ⇒ 镜像全部复位。 */
    XItemSelectionModel_clearSelection(sm);
    XQ_CHECK(!XTableWidgetItem_isSelected(XTableWidget_item(tw, 2, 2)));
    XQ_CHECK(!XTableWidgetItem_isSelected(XTableWidget_item(tw, 0, 1)));
    XQ_EQ_I64(XTableWidget_selectedItems(tw, NULL, 0), 0);

    XClassDelete((XClass*)tw);
}

/* ==================== ② 条目 → 模型 ==================== */

/** @brief item->setSelected 经选择模型生效，并逐次发 itemSelectionChanged。 */
XQ_TEST(XTableWidgetSelection, itemToModel)
{
    XTableWidget* tw = xqTwSel_make3x3();
    XItemSelectionModel* sm;
    XTableWidgetItem* item;
    XQ_REQUIRE(tw != NULL);
    sm = XAbstractItemView_selectionModel(&tw->m_base.m_base);
    XQ_REQUIRE(sm != NULL);

    xqTwSel_attach(tw);
    xqTwSel_resetProbes();

    /* ① setSelected(true) ⇒ 选择模型里该格选中、信号一次（Qt 实测 1）。 */
    item = XTableWidget_item(tw, 1, 2);
    XQ_REQUIRE(item != NULL);
    XTableWidgetItem_setSelected(item, true);
    XQ_CHECK(XItemSelectionModel_isSelected(sm, 1, 2));
    XQ_CHECK(XTableWidgetItem_isSelected(item));
    XQ_EQ_I64(g_twselSelHits, 1);

    /* ② 同值重设 ⇒ 幂等早退，不再多发（Qt 实测 setSelected(false) 于
     * 未选中格信号数 0）。 */
    xqTwSel_resetProbes();
    XTableWidgetItem_setSelected(item, true);
    XQ_EQ_I64(g_twselSelHits, 0);

    /* ③ setSelected(false) ⇒ 从模型移出。 */
    XTableWidgetItem_setSelected(item, false);
    XQ_CHECK(!XItemSelectionModel_isSelected(sm, 1, 2));
    XQ_CHECK(!XTableWidgetItem_isSelected(item));
    XQ_EQ_I64(g_twselSelHits, 1);

    /* ④ 游离条目（未入表）恒 false 且 setSelected 为空操作（Qt gap 296）。 */
    {
        XTableWidgetItem* orphan =
            XTableWidgetItem_create_utf8("orphan", XTableWidgetItem_Type);
        XQ_REQUIRE(orphan != NULL);
        XTableWidgetItem_setSelected(orphan, true);
        XQ_CHECK(!XTableWidgetItem_isSelected(orphan));
        XClassDelete((XClass*)orphan);
    }

    XClassDelete((XClass*)tw);
}

/* ==================== ③ setCurrentCell 三信号次序 ==================== */

/** @brief setCurrentCell 走选择模型，且三信号次序与 Qt 实测一致。 */
XQ_TEST(XTableWidgetSelection, setCurrentCellSignalOrder)
{
    XTableWidget* tw = xqTwSel_make3x3();
    XItemSelectionModel* sm;
    XQ_REQUIRE(tw != NULL);
    sm = XAbstractItemView_selectionModel(&tw->m_base.m_base);
    XQ_REQUIRE(sm != NULL);
    xqTwSel_attach(tw);
    xqTwSel_resetProbes();

    /* 2 参入口：当前格落定 + 被选中（ClearAndSelect），三信号各一次。 */
    XTableWidget_setCurrentCell(tw, 1, 1);
    XQ_EQ_I64(XTableWidget_currentRow(tw), 1);
    XQ_EQ_I64(XTableWidget_currentColumn(tw), 1);
    XQ_CHECK(XItemSelectionModel_currentIndex(sm, NULL, NULL));
    {
        int r = -1;
        int c = -1;
        XQ_CHECK(XItemSelectionModel_currentIndex(sm, &r, &c));
        XQ_EQ_I64(r, 1);
        XQ_EQ_I64(c, 1);
    }
    XQ_CHECK(XTableWidgetItem_isSelected(XTableWidget_item(tw, 1, 1)));
    XQ_EQ_I64(XTableWidget_selectedItems(tw, NULL, 0), 1);
    /* 本机 Qt 6.8.3 实测次序：i → I → C，各一次。 */
    XQ_EQ_STR(g_twselOrder, "iIC");
    XQ_EQ_I64(g_twselSelHits, 1);
    XQ_EQ_I64(g_twselCurItemHits, 1);
    XQ_EQ_I64(g_twselCurCellHits, 1);
    XQ_EQ_I64(g_twselCurCellRow, 1);

    /* NoUpdate：只移当前格，选区不动（Qt 实测 selCount 保持）。 */
    xqTwSel_resetProbes();
    XTableWidget_setCurrentCell_2(tw, 2, 2,
        XItemSelectionModelSelectionFlag_NoUpdate);
    XQ_EQ_I64(XTableWidget_currentRow(tw), 2);
    XQ_CHECK(!XTableWidgetItem_isSelected(XTableWidget_item(tw, 2, 2)));
    XQ_CHECK(XTableWidgetItem_isSelected(XTableWidget_item(tw, 1, 1)));
    XQ_EQ_I64(XTableWidget_selectedItems(tw, NULL, 0), 1);
    /* 无选区变化 ⇒ 只有当前格两信号。 */
    XQ_EQ_STR(g_twselOrder, "IC");
    XQ_EQ_I64(g_twselSelHits, 0);

    XClassDelete((XClass*)tw);
}

/* ==================== ④ setRangeSelected（含倒置归一） ==================== */

/** @brief 范围选中只做 Select/Deselect；倒置角按 Qt 归一化后施加。 */
XQ_TEST(XTableWidgetSelection, setRangeSelected)
{
    XTableWidget* tw = xqTwSel_make3x3();
    XTableWidgetSelectionRange r;
    XQ_REQUIRE(tw != NULL);

    /* ① 2x2 范围选中 ⇒ 4 格。 */
    r = XTableWidgetSelectionRange_create(0, 0, 1, 1);
    XTableWidget_setRangeSelected(tw, &r, true);
    XQ_EQ_I64(XTableWidget_selectedItems(tw, NULL, 0), 4);
    XQ_CHECK(XTableWidgetItem_isSelected(XTableWidget_item(tw, 1, 1)));

    /* ② 不清既有选区（只 Select）。 */
    r = XTableWidgetSelectionRange_create(2, 2, 2, 2);
    XTableWidget_setRangeSelected(tw, &r, true);
    XQ_EQ_I64(XTableWidget_selectedItems(tw, NULL, 0), 5);

    /* ③ 范围取消 ⇒ 回到 1 格。 */
    r = XTableWidgetSelectionRange_create(0, 0, 1, 1);
    XTableWidget_setRangeSelected(tw, &r, false);
    XQ_EQ_I64(XTableWidget_selectedItems(tw, NULL, 0), 1);
    XQ_CHECK(XTableWidgetItem_isSelected(XTableWidget_item(tw, 2, 2)));

    /* ④ 倒置角（Qt 实测 setRangeSelected(2,2,1,1) 选中 4 格）。 */
    XClassDelete((XClass*)tw);
    tw = xqTwSel_make3x3();
    XQ_REQUIRE(tw != NULL);
    r = XTableWidgetSelectionRange_create(2, 2, 1, 1);
    XTableWidget_setRangeSelected(tw, &r, true);
    XQ_EQ_I64(XTableWidget_selectedItems(tw, NULL, 0), 4);
    XQ_CHECK(XTableWidgetItem_isSelected(XTableWidget_item(tw, 1, 1)));
    XQ_CHECK(XTableWidgetItem_isSelected(XTableWidget_item(tw, 2, 2)));
    XQ_CHECK(!XTableWidgetItem_isSelected(XTableWidget_item(tw, 0, 0)));

    /* ⑤ 越界整体忽略（hasIndex 双向校验，Qt 同）。 */
    XClassDelete((XClass*)tw);
    tw = xqTwSel_make3x3();
    XQ_REQUIRE(tw != NULL);
    r = XTableWidgetSelectionRange_create(0, 0, 5, 5);
    XTableWidget_setRangeSelected(tw, &r, true);
    XQ_EQ_I64(XTableWidget_selectedItems(tw, NULL, 0), 0);

    XClassDelete((XClass*)tw);
}

/* ==================== ⑤ selectedRanges 不补洞 ==================== */

/** @brief selectedRanges 返回真实矩形，行内离散列**不并入 [min,max]**。 */
XQ_TEST(XTableWidgetSelection, selectedRangesNoHoleFill)
{
    XTableWidget* tw = xqTwSel_make3x3();
    XItemSelectionModel* sm;
    XTableWidgetSelectionRange rngs[8];
    int n;
    XQ_REQUIRE(tw != NULL);
    sm = XAbstractItemView_selectionModel(&tw->m_base.m_base);
    XQ_REQUIRE(sm != NULL);

    /* ① 洞形：选 (0,0) 与 (0,2) ⇒ Qt 实测 2 条（不补 [0,0,0,2]）。 */
    XItemSelectionModel_select(sm, 0, 0,
                               XItemSelectionModelSelectionFlag_ClearAndSelect);
    XItemSelectionModel_select(sm, 0, 2,
                               XItemSelectionModelSelectionFlag_Select);
    n = XTableWidget_selectedRanges(tw, rngs, 8);
    XQ_EQ_I64(n, 2);
    XQ_EQ_I64(XTableWidgetSelectionRange_topRow(&rngs[0]), 0);
    XQ_EQ_I64(XTableWidgetSelectionRange_leftColumn(&rngs[0]), 0);
    XQ_EQ_I64(XTableWidgetSelectionRange_rightColumn(&rngs[0]), 0);
    XQ_EQ_I64(XTableWidgetSelectionRange_leftColumn(&rngs[1]), 2);
    XQ_EQ_I64(XTableWidgetSelectionRange_rightColumn(&rngs[1]), 2);

    /* ② 2x2 全选 ⇒ 1 条 [0,0,1,1]。 */
    XItemSelectionModel_clearSelection(sm);
    XItemSelectionModel_selectSelection(sm, 0, 0, 1, 1,
                                        XItemSelectionModelSelectionFlag_Select);
    n = XTableWidget_selectedRanges(tw, rngs, 8);
    XQ_EQ_I64(n, 1);
    XQ_EQ_I64(XTableWidgetSelectionRange_topRow(&rngs[0]), 0);
    XQ_EQ_I64(XTableWidgetSelectionRange_bottomRow(&rngs[0]), 1);
    XQ_EQ_I64(XTableWidgetSelectionRange_rightColumn(&rngs[0]), 1);

    /* ③ 洞形（3x3 减中心）⇒ Qt 实测 4 条。 */
    XItemSelectionModel_clearSelection(sm);
    XItemSelectionModel_selectSelection(sm, 0, 0, 2, 2,
                                        XItemSelectionModelSelectionFlag_Select);
    XItemSelectionModel_select(sm, 1, 1,
                               XItemSelectionModelSelectionFlag_Deselect);
    n = XTableWidget_selectedRanges(tw, rngs, 8);
    XQ_EQ_I64(n, 4);

    /* ④ 空选区 ⇒ 0 条。 */
    XItemSelectionModel_clearSelection(sm);
    XQ_EQ_I64(XTableWidget_selectedRanges(tw, NULL, 0), 0);

    XClassDelete((XClass*)tw);
}

/* ==================== ⑥ findItems 的 MatchFlags 全矩阵 ==================== */

/** @brief findItems 按 Qt::MatchFlag 位语义匹配（列优先，大小写口径同 Qt）。 */
XQ_TEST(XTableWidgetSelection, findItemsMatchFlags)
{
    XTableWidget* tw = XTableWidget_create(NULL, 0);
    XTableWidgetItem* hits[8];
    XQ_REQUIRE(tw != NULL);
    XTableWidget_setRowCount(tw, 3);
    XTableWidget_setColumnCount(tw, 2);
    /* 列 0：Alpha / alphaBeta / betaAlpha（Qt 探针同款夹具）。 */
    XTableWidget_setItem(tw, 0, 0,
        XTableWidgetItem_create_utf8("Alpha", XTableWidgetItem_Type));
    XTableWidget_setItem(tw, 1, 0,
        XTableWidgetItem_create_utf8("alphaBeta", XTableWidgetItem_Type));
    XTableWidget_setItem(tw, 2, 0,
        XTableWidgetItem_create_utf8("betaAlpha", XTableWidgetItem_Type));

    /* MatchContains=1（Qt 实测 3；带 CaseSensitive 2）。 */
    XQ_EQ_I64(XTableWidget_findItems(tw, "Alpha", XMatchFlag_MatchContains,
                                     hits, 8), 3);
    XQ_EQ_I64(XTableWidget_findItems(
                  tw, "Alpha",
                  XMatchFlag_MatchContains | XMatchFlag_MatchCaseSensitive,
                  hits, 8), 2);
    /* MatchStartsWith=2（2 / +CS 1）。 */
    XQ_EQ_I64(XTableWidget_findItems(tw, "Alpha", XMatchFlag_MatchStartsWith,
                                     hits, 8), 2);
    XQ_EQ_I64(XTableWidget_findItems(
                  tw, "Alpha",
                  XMatchFlag_MatchStartsWith | XMatchFlag_MatchCaseSensitive,
                  hits, 8), 1);
    /* MatchEndsWith=3（2 / +CS 2）。 */
    XQ_EQ_I64(XTableWidget_findItems(tw, "Alpha", XMatchFlag_MatchEndsWith,
                                     hits, 8), 2);
    XQ_EQ_I64(XTableWidget_findItems(
                  tw, "Alpha",
                  XMatchFlag_MatchEndsWith | XMatchFlag_MatchCaseSensitive,
                  hits, 8), 2);
    /* MatchExactly=0：区分大小写（"Alpha"=1、"alpha"=0）。 */
    XQ_EQ_I64(XTableWidget_findItems(tw, "Alpha", XMatchFlag_MatchExactly,
                                     hits, 8), 1);
    XQ_EQ_I64(XTableWidget_findItems(tw, "alpha", XMatchFlag_MatchExactly,
                                     hits, 8), 0);
    /* MatchFixedString=8：缺省不敏感（1），带 CS 为 0。 */
    XQ_EQ_I64(XTableWidget_findItems(tw, "alpha", XMatchFlag_MatchFixedString,
                                     hits, 8), 1);
    XQ_EQ_I64(XTableWidget_findItems(
                  tw, "alpha",
                  XMatchFlag_MatchFixedString | XMatchFlag_MatchCaseSensitive,
                  hits, 8), 0);
    /* [§selrange 2026-11-16] 正则/通配接框架 XRegularExpression 后改为
     * **真命中**（本机 Qt 6.8 探针：RE("Al.*")=3、WC("Al*")=2）；
     * 完整矩阵见 XSelectionRangeAlignTest.c 的 findItemsRegexWildcard。 */
    XQ_EQ_I64(XTableWidget_findItems(tw, "Al.*",
                                     XMatchFlag_MatchRegularExpression,
                                     hits, 8), 3);
    XQ_EQ_I64(XTableWidget_findItems(tw, "Al*", XMatchFlag_MatchWildcard,
                                     hits, 8), 2);
    /* 空格语义：非 Exactly 下空串命中**所有格**（有文本 + 空格），
     * Exactly 下不命中。本夹具 3x2：列 0 三格有文本、列 1 三格为空 ⇒
     * 6 条（本机 Qt 6.8.3 实测 3x2 全空表 findItems("", MatchContains)=6，
     * 有文本格同样命中——QString::contains("") 恒 true）。 */
    XQ_EQ_I64(XTableWidget_findItems(tw, "", XMatchFlag_MatchContains,
                                     hits, 8), 6);
    XQ_EQ_I64(XTableWidget_findItems(tw, "", XMatchFlag_MatchExactly,
                                     hits, 8), 0);
    /* 列优先：列 0 的命中全部先于列 1。 */
    {
        int n = XTableWidget_findItems(tw, "Alpha", XMatchFlag_MatchContains,
                                       hits, 8);
        XQ_EQ_I64(n, 3);
        XQ_EQ_I64(XTableWidget_column(tw, hits[0]), 0);
    }

    XClassDelete((XClass*)tw);
}

/* ==================== ⑦ 防回环闸 ==================== */

/** @brief 条目位写入不回写模型：镜像期一次变化只产生一次发射。 */
XQ_TEST(XTableWidgetSelection, noFeedbackLoop)
{
    XTableWidget* tw = xqTwSel_make3x3();
    XItemSelectionModel* sm;
    XQ_REQUIRE(tw != NULL);
    sm = XAbstractItemView_selectionModel(&tw->m_base.m_base);
    XQ_REQUIRE(sm != NULL);
    xqTwSel_attach(tw);
    xqTwSel_resetProbes();

    /* 一次 ClearAndSelect 覆盖 2x2 ⇒ 镜像写 4 格，但只发一次通知。 */
    XItemSelectionModel_selectSelection(sm, 0, 0, 1, 1,
                                        XItemSelectionModelSelectionFlag_ClearAndSelect);
    XQ_EQ_I64(XTableWidget_selectedItems(tw, NULL, 0), 4);
    XQ_EQ_I64(g_twselSelHits, 1);
    /* 同步闸在事务外已归零（无残留重入）。 */
    XQ_EQ_I64(tw->m_selSyncDepth, 0);

    /* 反复 select 同集合 ⇒ 模型无变化 ⇒ 不发。 */
    xqTwSel_resetProbes();
    XItemSelectionModel_selectSelection(sm, 0, 0, 1, 1,
                                        XItemSelectionModelSelectionFlag_Select);
    XQ_EQ_I64(g_twselSelHits, 0);

    XClassDelete((XClass*)tw);
}

/* ==================== ⑧ 行列增删后的选区重映射 ==================== */

/** @brief 插入/删除行列后选区跟随（对齐 Qt 持久索引平移）。 */
XQ_TEST(XTableWidgetSelection, selectionFollowsRowColumnChanges)
{
    XTableWidget* tw = xqTwSel_make3x3();
    XItemSelectionModel* sm;
    XQ_REQUIRE(tw != NULL);
    sm = XAbstractItemView_selectionModel(&tw->m_base.m_base);
    XQ_REQUIRE(sm != NULL);

    /* 选 (1,1)。 */
    XItemSelectionModel_select(sm, 1, 1,
                               XItemSelectionModelSelectionFlag_ClearAndSelect);
    XQ_CHECK(XItemSelectionModel_isSelected(sm, 1, 1));

    /* 行 0 前插一行 ⇒ 选中格下移到 (2,1)。 */
    XTableWidget_insertRow(tw, 0);
    XQ_CHECK(XItemSelectionModel_isSelected(sm, 2, 1));
    XQ_CHECK(!XItemSelectionModel_isSelected(sm, 1, 1));
    XQ_CHECK(XTableWidgetItem_isSelected(XTableWidget_item(tw, 2, 1)));

    /* 列 0 前插一列 ⇒ 选中格右移到 (2,2)。 */
    XTableWidget_insertColumn(tw, 0);
    XQ_CHECK(XItemSelectionModel_isSelected(sm, 2, 2));

    /* 删除行 0 ⇒ 选中格上移到 (1,2)。 */
    XTableWidget_removeRow(tw, 0);
    XQ_CHECK(XItemSelectionModel_isSelected(sm, 1, 2));

    /* 删除被选中的那一行 ⇒ 该格丢弃。 */
    XTableWidget_removeRow(tw, 1);
    XQ_EQ_I64(XItemSelectionModel_selectedCount(sm), 0);

    XClassDelete((XClass*)tw);
}

/* ==================== ⑨ 既有视觉管线不回归 ==================== */

/** @brief K-69 表头高亮仍按选择模型判定（桥接后同源不断链）。 */
XQ_TEST(XTableWidgetSelection, headerStateStillReadsSelectionModel)
{
    XTableWidget* tw = XTableWidget_create(NULL, 0);
    XItemSelectionModel* sm;
    int r;
    int c;
    XQ_REQUIRE(tw != NULL);
    XTableWidget_setRowCount(tw, 2);
    XTableWidget_setColumnCount(tw, 2);
    /* 必须装入条目：表头高亮来自选择模型，而条目镜像只能落在**存在条目**
     * 的格上（空格无位可承载，见 xtw_selMirror 头注）。 */
    for (r = 0; r < 2; ++r) {
        for (c = 0; c < 2; ++c) {
            char buf[8];
            snprintf(buf, sizeof(buf), "r%dc%d", r, c);
            XTableWidget_setItem(tw, r, c,
                XTableWidgetItem_create_utf8(buf, XTableWidgetItem_Type));
        }
    }
    sm = XAbstractItemView_selectionModel(&tw->m_base.m_base);
    XQ_REQUIRE(sm != NULL);

    /* 列 0 只选 1/2 格 ⇒ 相交但非整列。 */
    XItemSelectionModel_select(sm, 0, 0,
                               XItemSelectionModelSelectionFlag_ClearAndSelect);
    XQ_CHECK(XItemSelectionModel_columnIntersectsSelection(sm, 0));
    XQ_CHECK(!XItemSelectionModel_isColumnSelected(sm, 0));

    /* 补满列 0 ⇒ 整列选中（表头 State_On|Sunken 的判定输入）。 */
    XItemSelectionModel_select(sm, 1, 0,
                               XItemSelectionModelSelectionFlag_Select);
    XQ_CHECK(XItemSelectionModel_isColumnSelected(sm, 0));
    /* 条目镜像同步跟上（表头/表体同源）。 */
    XQ_CHECK(XTableWidgetItem_isSelected(XTableWidget_item(tw, 0, 0)));
    XQ_CHECK(XTableWidgetItem_isSelected(XTableWidget_item(tw, 1, 0)));

    XClassDelete((XClass*)tw);
}

/* ==================== ⑩ clear / clearContents ==================== */

/** @brief clear 与 clearContents 都清选择模型（Qt 实测当前索引也归 -1）。 */
XQ_TEST(XTableWidgetSelection, clearResetsSelectionModel)
{
    XTableWidget* tw = xqTwSel_make3x3();
    XItemSelectionModel* sm;
    XQ_REQUIRE(tw != NULL);
    sm = XAbstractItemView_selectionModel(&tw->m_base.m_base);
    XQ_REQUIRE(sm != NULL);

    /* ① clearContents：选区与当前索引都清，但行列数与条目槽仍在。 */
    XTableWidget_setCurrentCell(tw, 1, 1);
    XQ_EQ_I64(XItemSelectionModel_selectedCount(sm), 1);
    XTableWidget_clearContents(tw);
    XQ_EQ_I64(XItemSelectionModel_selectedCount(sm), 0);
    XQ_CHECK(!XItemSelectionModel_currentIndex(sm, NULL, NULL));
    XQ_EQ_I64(XTableWidget_rowCount(tw), 3);
    XQ_EQ_I64(XTableWidget_columnCount(tw), 3);
    XQ_CHECK(XTableWidget_currentItem(tw) == NULL);

    /* ② clear：选区与当前索引清、行列数归零。 */
    XClassDelete((XClass*)tw);
    tw = xqTwSel_make3x3();
    XQ_REQUIRE(tw != NULL);
    sm = XAbstractItemView_selectionModel(&tw->m_base.m_base);
    XQ_REQUIRE(sm != NULL);
    XTableWidget_setCurrentCell(tw, 2, 0);
    XTableWidget_clear(tw);
    XQ_EQ_I64(XItemSelectionModel_selectedCount(sm), 0);
    XQ_CHECK(!XItemSelectionModel_currentIndex(sm, NULL, NULL));
    XQ_EQ_I64(XTableWidget_rowCount(tw), 0);
    XQ_EQ_I64(XTableWidget_currentRow(tw), -1);

    XClassDelete((XClass*)tw);
}

/* ==================== ⑪ 既有调用方口径（sharedEdits 的回归锁） ==================== */

/** @brief findItems 的 flags 语义与 Qt 逐值一致（旧调用方 1=精确 的口径已废）。
 *
 *  @details 本用例锁住 §B4 语义变更的**调用方契约**：`XTableWidget_findItems`
 *           的 flags 现在按 Qt::MatchFlag 取值（0=MatchExactly、
 *           1=MatchContains），与 qtablewidget.h 的 `Qt::MatchFlags` 同源。
 *           旧实现把 1 当精确、其余当包含（与 Qt 数值相反），仓库内两处
 *           调用方（`xgui_demo_apitest_views.c:3377-3389`、
 *           `xgui_regression_test.c:34450`）按旧口径传 1；本用例以同等
 *           夹具把新口径下的期望钉住，供那两处改传 0（MatchExactly）后的
 *           断言自证。
 *  @note 该锁与 `findItemsMatchFlags` 互补：后者锁 Qt 语义矩阵，本用例锁
 *        「旧写法在新语义下的实际结果」，避免调用方被静默改变行为。
 */
XQ_TEST(XTableWidgetSelection, findItemsLegacyCallSiteSemantics)
{
    XTableWidget* tw = XTableWidget_create(NULL, 0);
    XTableWidgetItem* hits[8];
    int n;
    XQ_REQUIRE(tw != NULL);
    XTableWidget_setRowCount(tw, 3);
    XTableWidget_setColumnCount(tw, 2);
    /* 与 demo 套件同款夹具：3x2、6 格全有文本。 */
    XTableWidget_setItem(tw, 0, 0,
        XTableWidgetItem_create_utf8("苹果", XTableWidgetItem_Type));
    XTableWidget_setItem(tw, 0, 1,
        XTableWidgetItem_create_utf8("3", XTableWidgetItem_Type));
    XTableWidget_setItem(tw, 1, 0,
        XTableWidgetItem_create_utf8("香蕉", XTableWidgetItem_Type));
    XTableWidget_setItem(tw, 1, 1,
        XTableWidgetItem_create_utf8("5", XTableWidgetItem_Type));
    XTableWidget_setItem(tw, 2, 0,
        XTableWidgetItem_create_utf8("樱桃", XTableWidgetItem_Type));
    XTableWidget_setItem(tw, 2, 1,
        XTableWidgetItem_create_utf8("8", XTableWidgetItem_Type));

    /* 「苹果」：Exactly(0) 与 Contains(1) 在唯一命中格上结果相同（1 条）。 */
    XQ_EQ_I64(XTableWidget_findItems(tw, "苹果", XMatchFlag_MatchExactly,
                                     hits, 8), 1);
    XQ_CHECK(hits[0] == XTableWidget_item(tw, 0, 0));
    XQ_EQ_I64(XTableWidget_findItems(tw, "苹果", XMatchFlag_MatchContains,
                                     hits, 8), 1);

    /* 「香蕉」同上（Exactly 命中行 1）。 */
    n = XTableWidget_findItems(tw, "香蕉", XMatchFlag_MatchExactly, hits, 8);
    XQ_EQ_I64(n, 1);
    XQ_EQ_I64(XTableWidget_row(tw, hits[0]), 1);

    /* **关键差异**：空串在 Exactly 下 0 条（Qt 实测 0），在 Contains 下
     * 命中全部 6 个非空格（Qt 实测 3x2 空表返回 6）。旧调用方断言
     * 「空串命中 0」⇒ 必须传 MatchExactly(0)，传 1(Contains) 会得到 6。 */
    XQ_EQ_I64(XTableWidget_findItems(tw, "", XMatchFlag_MatchExactly,
                                     hits, 8), 0);
    XQ_EQ_I64(XTableWidget_findItems(tw, "", XMatchFlag_MatchContains,
                                     hits, 8), 6);

    XClassDelete((XClass*)tw);
}

/* ==================== ⑫ 复核修订回归锁（2026-11-15） ==================== */

/** @brief 排序把选区与当前格随条目搬移（对齐 changePersistentIndexList）。
 *
 *  @details 复核实测的现场缺陷（**严重**）：选中文本 "a" 的行后
 *           `sortItems` 升序，行序变 a/b/c，但选择模型仍说 row1 选中，
 *           而 row1 现在装的是 "b" ⇒ 高亮落到别的数据行。Qt 用
 *           changePersistentIndexList 让选区跟条目走（qtablewidget.cpp:
 *           533-537、QTableModel::sort）。现场调用点：
 *           HighSpeedFilter/client/ui/HsfTableSort.c:47-52 的
 *           HsfTableSort_apply（HsfDataHistory.c:495、HsfDataLog.c:279 每页
 *           刷新都调）。
 */
XQ_TEST(XTableWidgetSelection, sortFollowsItemNotRowIndex)
{
    XTableWidget* tw = XTableWidget_create(NULL, 0);
    XItemSelectionModel* sm;
    XTableWidgetItem* it;
    XQ_REQUIRE(tw != NULL);
    XTableWidget_setRowCount(tw, 3);
    XTableWidget_setColumnCount(tw, 1);
    /* 行序 b / a / c，选中文本 "a" 的行（row 1）。 */
    XTableWidget_setItem(tw, 0, 0,
        XTableWidgetItem_create_utf8("b", XTableWidgetItem_Type));
    XTableWidget_setItem(tw, 1, 0,
        XTableWidgetItem_create_utf8("a", XTableWidgetItem_Type));
    XTableWidget_setItem(tw, 2, 0,
        XTableWidgetItem_create_utf8("c", XTableWidgetItem_Type));
    sm = XAbstractItemView_selectionModel(&tw->m_base.m_base);
    XQ_REQUIRE(sm != NULL);
    XItemSelectionModel_select(sm, 1, 1 - 1,
                               XItemSelectionModelSelectionFlag_ClearAndSelect);
    XQ_CHECK(XItemSelectionModel_isSelected(sm, 1, 0));

    /* 升序排序 ⇒ 行序 a / b / c，"a" 移到 row 0 ⇒ 选区必须跟到 row 0。 */
    XTableWidget_sortItems(tw, 0, 0);
    it = XTableWidget_item(tw, 0, 0);
    XQ_REQUIRE(it != NULL);
    XQ_EQ_STR(XTableWidgetItem_text_toUtf8(it), "a");
    XQ_CHECK_MSG(XItemSelectionModel_isSelected(sm, 0, 0),
                 "排序后选区随条目走：选中仍在文本 a 的新行 0");
    XQ_CHECK_MSG(!XItemSelectionModel_isSelected(sm, 1, 0),
                 "旧行号 1（现装 b）不得再报选中");
    XQ_CHECK_MSG(XTableWidgetItem_isSelected(it),
                 "条目 isSelected 与模型同源（新行 0）");
    XQ_CHECK(!XTableWidgetItem_isSelected(XTableWidget_item(tw, 1, 0)));

    /* 当前格同样随条目搬移。 */
    XClassDelete((XClass*)tw);
    tw = XTableWidget_create(NULL, 0);
    XQ_REQUIRE(tw != NULL);
    XTableWidget_setRowCount(tw, 2);
    XTableWidget_setColumnCount(tw, 1);
    XTableWidget_setItem(tw, 0, 0,
        XTableWidgetItem_create_utf8("z", XTableWidgetItem_Type));
    XTableWidget_setItem(tw, 1, 0,
        XTableWidgetItem_create_utf8("a", XTableWidgetItem_Type));
    XTableWidget_setCurrentCell(tw, 1, 0);
    XQ_EQ_I64(XTableWidget_currentRow(tw), 1);
    XTableWidget_sortItems(tw, 0, 0);
    XQ_EQ_STR(XTableWidgetItem_text_toUtf8(XTableWidget_item(tw, 0, 0)), "a");
    XQ_CHECK_MSG(XTableWidget_currentRow(tw) == 0,
                 "排序后当前格随条目搬到新行 0");

    XClassDelete((XClass*)tw);
}

/** @brief 只选中空格时 itemSelectionChanged 也发（Qt 由范围变化驱动）。
 *
 *  @details 复核实测：2×2 表里选中空格 (1,1) ⇒ 模型 isSelected=1、
 *           selectedRanges=1 条，但 itemSelectionChanged=0。根因是旧实现
 *           拿「条目位是否翻转」当判据，而空格没有条目位。Qt 的
 *           selectionChanged 直连 itemSelectionChanged
 *           （qtablewidget.cpp:1636-1637），与格上有没有 item 无关。
 */
XQ_TEST(XTableWidgetSelection, emptyCellSelectionStillNotifies)
{
    XTableWidget* tw = XTableWidget_create(NULL, 0);
    XItemSelectionModel* sm;
    XQ_REQUIRE(tw != NULL);
    XTableWidget_setRowCount(tw, 2);
    XTableWidget_setColumnCount(tw, 2); /* 全空：不 setItem。 */
    sm = XAbstractItemView_selectionModel(&tw->m_base.m_base);
    XQ_REQUIRE(sm != NULL);
    xqTwSel_attach(tw);
    xqTwSel_resetProbes();

    XQ_CHECK(XItemSelectionModel_select(sm, 1, 1,
                                        XItemSelectionModelSelectionFlag_ClearAndSelect));
    XQ_CHECK(XItemSelectionModel_isSelected(sm, 1, 1));
    XQ_EQ_I64(XTableWidget_selectedRanges(tw, NULL, 0), 1);
    XQ_EQ_I64(XTableWidget_selectedItems(tw, NULL, 0), 0); /* 空格无条目。 */
    XQ_EQ_I64(g_twselSelHits, 1); /* 信号仍要发一次。 */

    /* 两个纯空格选区之间切换同样要发。 */
    xqTwSel_resetProbes();
    XQ_CHECK(XItemSelectionModel_select(sm, 0, 0,
                                        XItemSelectionModelSelectionFlag_ClearAndSelect));
    XQ_EQ_I64(g_twselSelHits, 1);

    XClassDelete((XClass*)tw);
}

/** @brief clear/clearContents 三个信号都要发（Qt 1/1/1，旧实现 0/0/0）。 */
XQ_TEST(XTableWidgetSelection, clearEmitsSignals)
{
    XTableWidget* tw = xqTwSel_make3x3();
    XQ_REQUIRE(tw != NULL);
    xqTwSel_attach(tw);

    /* clearContents：先选中并置当前格，再清 ⇒ 三族都应有信号。 */
    XTableWidget_setCurrentCell(tw, 1, 1);
    xqTwSel_resetProbes();
    XTableWidget_clearContents(tw);
    XQ_CHECK_MSG(g_twselSelHits >= 1, "clearContents 发 itemSelectionChanged");
    XQ_CHECK_MSG(g_twselCurCellHits >= 1, "clearContents 发 currentCellChanged");
    XQ_CHECK_MSG(g_twselCurItemHits >= 1, "clearContents 发 currentItemChanged");
    XQ_EQ_I64(XTableWidget_currentRow(tw), -1);
    XQ_EQ_I64(XTableWidget_selectedItems(tw, NULL, 0), 0);

    /* clear 同理。 */
    XClassDelete((XClass*)tw);
    tw = xqTwSel_make3x3();
    XQ_REQUIRE(tw != NULL);
    xqTwSel_attach(tw);
    XTableWidget_setCurrentCell(tw, 2, 0);
    xqTwSel_resetProbes();
    XTableWidget_clear(tw);
    XQ_CHECK_MSG(g_twselSelHits >= 1, "clear 发 itemSelectionChanged");
    XQ_CHECK_MSG(g_twselCurCellHits >= 1, "clear 发 currentCellChanged");
    XQ_EQ_I64(XTableWidget_rowCount(tw), 0);

    XClassDelete((XClass*)tw);
}

/** @brief setRowCount/setColumnCount 收缩必须裁剪选区（不得留幽灵选中）。 */
XQ_TEST(XTableWidgetSelection, dimensionShrinkTrimsSelection)
{
    XTableWidget* tw = XTableWidget_create(NULL, 0);
    XItemSelectionModel* sm;
    XQ_REQUIRE(tw != NULL);
    XTableWidget_setRowCount(tw, 6);
    XTableWidget_setColumnCount(tw, 2);
    sm = XAbstractItemView_selectionModel(&tw->m_base.m_base);
    XQ_REQUIRE(sm != NULL);

    /* 选 (5,1) → 收缩到 2 行 → 再扩回 6 行：越界索引不得复活。 */
    XItemSelectionModel_select(sm, 5, 1,
                               XItemSelectionModelSelectionFlag_ClearAndSelect);
    XTableWidget_setRowCount(tw, 2);
    XQ_EQ_I64(XItemSelectionModel_selectedCount(sm), 0);
    XQ_EQ_I64(XTableWidget_selectedItems(tw, NULL, 0), 0);
    XTableWidget_setRowCount(tw, 6);
    XQ_EQ_I64(XItemSelectionModel_selectedCount(sm), 0); /* 幽灵不复活。 */

    /* 列同理。 */
    XTableWidget_setRowCount(tw, 6);
    XItemSelectionModel_select(sm, 1, 1,
                               XItemSelectionModelSelectionFlag_ClearAndSelect);
    XTableWidget_setColumnCount(tw, 1);
    XQ_EQ_I64(XItemSelectionModel_selectedCount(sm), 0);

    XClassDelete((XClass*)tw);
}

/** @brief 换选择模型后桥自动重挂（对齐 Qt setSelectionModel 重连）。 */
XQ_TEST(XTableWidgetSelection, setSelectionModelRearmsBridge)
{
    XTableWidget* tw = xqTwSel_make3x3();
    XItemSelectionModel* fresh;
    XQ_REQUIRE(tw != NULL);
    xqTwSel_attach(tw);

    /* 换成全新的选择模型：新模型的选区变化必须仍能驱动条目镜像与信号。 */
    fresh = XItemSelectionModel_create();
    XQ_REQUIRE(fresh != NULL);
    XAbstractItemView_setSelectionModel(&tw->m_base.m_base, fresh);
    XQ_CHECK(XAbstractItemView_selectionModel(&tw->m_base.m_base) == fresh);
    /* 换模型后先做一次表格侧查询，触发桥惰性重挂（本库 setSelectionModel
     * 不是虚槽、在基类文件里，重挂由 xtw_selModel 的指针比对自愈完成；
     * Qt 则在 setSelectionModel 内部直接 connect，qabstractitemview.cpp:
     * 790-860）。 */
    (void)XTableWidget_selectedItems(tw, NULL, 0);

    xqTwSel_resetProbes();
    XQ_CHECK(XItemSelectionModel_select(fresh, 2, 1,
                                        XItemSelectionModelSelectionFlag_ClearAndSelect));
    XQ_EQ_I64(g_twselSelHits, 1);
    XQ_CHECK_MSG(XTableWidgetItem_isSelected(XTableWidget_item(tw, 2, 1)),
                 "新选择模型的选区要镜像进条目位");

    /* 换回 NULL（懒重建）后仍工作。 */
    XAbstractItemView_setSelectionModel(&tw->m_base.m_base, NULL);
    XQ_CHECK(XAbstractItemView_selectionModel(&tw->m_base.m_base) != NULL);
    (void)XTableWidget_selectedItems(tw, NULL, 0); /* 触发重挂。 */
    xqTwSel_resetProbes();
    XQ_CHECK(XItemSelectionModel_select(
        XAbstractItemView_selectionModel(&tw->m_base.m_base), 0, 2,
        XItemSelectionModelSelectionFlag_ClearAndSelect));
    XQ_EQ_I64(g_twselSelHits, 1);
    XQ_CHECK(XTableWidgetItem_isSelected(XTableWidget_item(tw, 0, 2)));

    XClassDelete((XClass*)tw);
}

/** @brief selectedRanges 不过滤隐藏行列（Qt 只有 selectedItems 才过滤）。 */
XQ_TEST(XTableWidgetSelection, selectedRangesIgnoresHidden)
{
    XTableWidget* tw = XTableWidget_create(NULL, 0);
    XItemSelectionModel* sm;
    int n;
    XQ_REQUIRE(tw != NULL);
    XTableWidget_setRowCount(tw, 2);
    XTableWidget_setColumnCount(tw, 4);
    sm = XAbstractItemView_selectionModel(&tw->m_base.m_base);
    XQ_REQUIRE(sm != NULL);

    /* 全选 2×4 ⇒ 1 条（[0,0,1,3]）。 */
    XItemSelectionModel_selectSelection(sm, 0, 0, 1, 3,
                                        XItemSelectionModelSelectionFlag_ClearAndSelect);
    n = XTableWidget_selectedRanges(tw, NULL, 0);
    XQ_EQ_I64(n, 1);

    /* 隐藏列 1 ⇒ 范围仍是 1 条（Qt 不因隐藏切碎；只有 selectedItems 过滤）。 */
    XTableView_setColumnHidden(&tw->m_base, 1, true);
    n = XTableWidget_selectedRanges(tw, NULL, 0);
    XQ_EQ_I64(n, 1);
    /* selectedItems 侧则过滤隐藏列：2 行 × 3 可见列 = 6。 */
    {
        int r;
        int c;
        for (r = 0; r < 2; ++r)
            for (c = 0; c < 4; ++c)
                if (XTableWidget_item(tw, r, c) == NULL)
                    XTableWidget_setItem(tw, r, c,
                        XTableWidgetItem_create_utf8("x", XTableWidgetItem_Type));
        XQ_EQ_I64(XTableWidget_selectedItems(tw, NULL, 0), 6);
    }

    XClassDelete((XClass*)tw);
}

/** @brief findItems 对数值 DisplayRole 也按文本化匹配（非 Exactly 通道）。
 *
 *  @details 复核实测：DisplayRole=int32(42) 的格，Exactly("42")=1 但
 *           Contains("4")=0、StartsWith("4")=0 —— 旧实现把非 Exactly 分支
 *           走 XTableWidgetItem_text_const（只覆盖 String 型，数值返回
 *           NULL）。Qt 对所有非 Exactly 分支一律 v.toString()
 *           （qabstractitemmodel.cpp:2569-2595），应命中。
 */
XQ_TEST(XTableWidgetSelection, findItemsNumericTextChannel)
{
    XTableWidget* tw = XTableWidget_create(NULL, 0);
    XVariant* v;
    XTableWidgetItem* hits[8];
    XQ_REQUIRE(tw != NULL);
    XTableWidget_setRowCount(tw, 1);
    XTableWidget_setColumnCount(tw, 2);
    {
        XTableWidgetItem* it =
            XTableWidgetItem_create_utf8("", XTableWidgetItem_Type);
        XQ_REQUIRE(it != NULL);
        v = XVariant_create_int(42);
        XQ_REQUIRE(v != NULL);
        XTableWidgetItem_setData(it, XItemDataRole_DisplayRole, v);
        XClassDelete((XClass*)v);
        XTableWidget_setItem(tw, 0, 0, it);
    }
    XQ_EQ_STR(XTableWidgetItem_text_toUtf8(XTableWidget_item(tw, 0, 0)), "42");

    XQ_EQ_I64(XTableWidget_findItems(tw, "42", XMatchFlag_MatchExactly,
                                     hits, 8), 1);
    XQ_EQ_I64(XTableWidget_findItems(tw, "4", XMatchFlag_MatchContains,
                                     hits, 8), 1);
    XQ_EQ_I64(XTableWidget_findItems(tw, "4", XMatchFlag_MatchStartsWith,
                                     hits, 8), 1);
    XQ_EQ_I64(XTableWidget_findItems(tw, "2", XMatchFlag_MatchEndsWith,
                                     hits, 8), 1);

    XClassDelete((XClass*)tw);
}

/* ==================== ⑬ 返工回归锁（2026-11-15 返工①~⑥） ==================== */

/** @brief 返工②：setItem 到已选中格必须补刷条目镜像位（双向失真都纠正）。
 *
 *  @details 复核缺陷②的镜像失真面：选择模型的选中态挂在**索引**上（空格
 *           同样可选中），条目 m_selected 位是派生镜像、只在选区事务收尾
 *           刷新 ⇒ 「先选中空格再 setItem」会让新条目位(0)与模型(1)失真，
 *           isSelected/绘制高亮直到下一次选区变化才自愈。Qt 的 isSelected
 *           直读 selectionModel（qtablewidget.cpp:1087-1096）无此窗口。
 */
XQ_TEST(XTableWidgetSelection, setItemMirrorsModelSelection)
{
    XTableWidget* tw = XTableWidget_create(NULL, 0);
    XItemSelectionModel* sm;
    XTableWidgetItem* it;
    XQ_REQUIRE(tw != NULL);
    XTableWidget_setRowCount(tw, 2);
    XTableWidget_setColumnCount(tw, 2);
    sm = XAbstractItemView_selectionModel(&tw->m_base.m_base);
    XQ_REQUIRE(sm != NULL);

    /* ① 选中空格 (1,1) → setItem：新条目必须直接呈现选中态。 */
    XQ_CHECK(XItemSelectionModel_select(
        sm, 1, 1, XItemSelectionModelSelectionFlag_ClearAndSelect));
    it = XTableWidgetItem_create_utf8("late", XTableWidgetItem_Type);
    XQ_REQUIRE(it != NULL);
    XTableWidget_setItem(tw, 1, 1, it);
    XQ_CHECK_MSG(XTableWidgetItem_isSelected(it),
                 "选中空格后 setItem，条目镜像位必须补刷为选中");
    XQ_EQ_I64(XTableWidget_selectedItems(tw, NULL, 0), 1);

    /* ② 反向：把选中态条目覆盖掉（模型选区不动，索引仍选中）——
     * 新条目同样必须呈现选中。 */
    it = XTableWidgetItem_create_utf8("over", XTableWidgetItem_Type);
    XQ_REQUIRE(it != NULL);
    XTableWidget_setItem(tw, 1, 1, it);
    XQ_CHECK(XItemSelectionModel_isSelected(sm, 1, 1));
    XQ_CHECK_MSG(XTableWidgetItem_isSelected(it),
                 "覆盖选中格的 setItem 不改变模型选区，新条目仍选中");

    /* ③ 复用条目残留位纠正：takeItem 出来的条目带着位 1，重入到
     * 模型未选中的格 ⇒ 位必须被对齐为 0。 */
    {
        XTableWidgetItem* taken = XTableWidget_takeItem(tw, 1, 1);
        XQ_REQUIRE(taken != NULL);
        XQ_CHECK(!XTableWidgetItem_isSelected(taken)); /* 游离恒 false。 */
        XQ_CHECK(XItemSelectionModel_isSelected(sm, 1, 1)); /* 空格仍选中。 */
        XTableWidget_setItem(tw, 0, 0, taken); /* 重入到未选格 (0,0)。 */
        XQ_CHECK_MSG(!XTableWidgetItem_isSelected(taken),
                     "重入条目的残留选中位必须按模型对齐为未选中");
        XQ_CHECK(!XItemSelectionModel_isSelected(sm, 0, 0));
    }

    XClassDelete((XClass*)tw);
}

/** @brief 返工③④：行列增删/收缩的选区重建只发**一次** itemSelectionChanged。
 *
 *  @details 复核缺陷③（信号风暴）：旧实现在闸外逐格 select，N 格选区
 *           N 次 selectionChanged ⇒ N 次 itemSelectionChanged。返工后
 *           remap/trim 全部走事务闸内 rebuildSelection（单次发射）+ 尾部
 *           一次收口。m_selSyncDepth 必须无残留。
 */
XQ_TEST(XTableWidgetSelection, remapAndTrimEmitOnce)
{
    XTableWidget* tw = xqTwSel_make3x3();
    XItemSelectionModel* sm;
    XQ_REQUIRE(tw != NULL);
    sm = XAbstractItemView_selectionModel(&tw->m_base.m_base);
    XQ_REQUIRE(sm != NULL);
    xqTwSel_attach(tw);

    /* 选整行 0（3 格）。 */
    XItemSelectionModel_selectSelection(sm, 0, 0, 0, 2,
                                        XItemSelectionModelSelectionFlag_Select);
    xqTwSel_resetProbes();

    /* 插入一行 ⇒ 整行选中搬到行 1，恰好一次发射。 */
    XTableWidget_insertRow(tw, 0);
    XQ_EQ_I64(g_twselSelHits, 1);
    XQ_EQ_I64(XItemSelectionModel_selectedCount(sm), 3);
    XQ_CHECK(XItemSelectionModel_isSelected(sm, 1, 0));
    XQ_CHECK(XItemSelectionModel_isSelected(sm, 1, 2));
    XQ_EQ_I64(tw->m_selSyncDepth, 0);

    /* 删除行 0（未选中行）⇒ 选区回到行 0，又恰好一次。 */
    xqTwSel_resetProbes();
    XTableWidget_removeRow(tw, 0);
    XQ_EQ_I64(g_twselSelHits, 1);
    XQ_CHECK(XItemSelectionModel_isSelected(sm, 0, 1));
    XQ_EQ_I64(tw->m_selSyncDepth, 0);

    /* setRowCount 收缩到 1（选中行 0 保留）⇒ 选区不变 ⇒ 0 次。 */
    xqTwSel_resetProbes();
    XTableWidget_setRowCount(tw, 1);
    XQ_EQ_I64(g_twselSelHits, 0);
    XQ_EQ_I64(XItemSelectionModel_selectedCount(sm), 3);

    /* setColumnCount 收缩裁掉选中列 ⇒ 裁剪 + 一次发射。 */
    xqTwSel_resetProbes();
    XTableWidget_setColumnCount(tw, 1); /* 只留列 0（选中 1 格）。 */
    XQ_EQ_I64(g_twselSelHits, 1);
    XQ_EQ_I64(XItemSelectionModel_selectedCount(sm), 1);
    XQ_EQ_I64(tw->m_selSyncDepth, 0);
    xqTwSel_resetProbes();
    XTableWidget_setColumnCount(tw, 0); /* 最后的选中格也被裁 ⇒ 一次发射。 */
    XQ_EQ_I64(g_twselSelHits, 1);
    XQ_EQ_I64(XItemSelectionModel_selectedCount(sm), 0);
    XQ_EQ_I64(tw->m_selSyncDepth, 0);

    XClassDelete((XClass*)tw);
}

/** @brief 返工④⑤：当前格在被删区间内 ⇒ 就近迁移并发 currentCellChanged；
 *         区间外 ⇒ 不动不发（Qt rowsAboutToBeRemoved 直译口径）。
 *
 *  @details Qt 6.8 qitemselectionmodel.cpp:654-670：current 只在落在
 *           被删区间才处理——上邻 start-1 优先，否则下邻（end+1），
 *           整轴删空才无效；并发射 currentChanged。旧实现两路分叉：
 *           removeRow 对「>=row 全平移」（区间外也动、删当前行清 -1）、
 *           setRowCount 只 clamp 视图不发信号 ⇒ 删当前行后当前格凭空
 *           消失（Qt 保留上邻）。
 */
XQ_TEST(XTableWidgetSelection, currentMigratesOnRangeRemoval)
{
    XTableWidget* tw;
    XItemSelectionModel* sm;
    int r = -1;
    int c = -1;

    /* ① setRowCount 尾部收缩：current=(4,0) → 迁上邻 (1,0)（=rows-1）
     *    并发射一次 currentCellChanged。 */
    tw = XTableWidget_create(NULL, 0);
    XQ_REQUIRE(tw != NULL);
    XTableWidget_setRowCount(tw, 5);
    XTableWidget_setColumnCount(tw, 2);
    sm = XAbstractItemView_selectionModel(&tw->m_base.m_base);
    XQ_REQUIRE(sm != NULL);
    xqTwSel_attach(tw);
    XTableWidget_setCurrentCell(tw, 4, 0);
    xqTwSel_resetProbes();
    XTableWidget_setRowCount(tw, 2);
    XQ_EQ_I64(XTableWidget_currentRow(tw), 1);
    XQ_EQ_I64(g_twselCurCellHits, 1);
    XQ_EQ_I64(g_twselCurCellRow, 1);
    XQ_CHECK(XItemSelectionModel_currentIndex(sm, &r, &c));
    XQ_EQ_I64(r, 1);
    XQ_EQ_I64(c, 0);
    XClassDelete((XClass*)tw);

    /* ② removeRow 删的正是当前行 ⇒ 迁上邻（Qt：start>0 ⇒ start-1）。 */
    tw = XTableWidget_create(NULL, 0);
    XQ_REQUIRE(tw != NULL);
    XTableWidget_setRowCount(tw, 3);
    XTableWidget_setColumnCount(tw, 1);
    sm = XAbstractItemView_selectionModel(&tw->m_base.m_base);
    XQ_REQUIRE(sm != NULL);
    xqTwSel_attach(tw);
    XTableWidget_setCurrentCell(tw, 1, 0);
    xqTwSel_resetProbes();
    XTableWidget_removeRow(tw, 1);
    XQ_CHECK_MSG(XTableWidget_currentRow(tw) == 0,
                 "删当前行：当前格迁上邻（Qt 就近迁移），不得清 -1");
    XQ_EQ_I64(g_twselCurCellHits, 1);
    XQ_EQ_I64(g_twselCurCellRow, 0);
    XQ_CHECK(XItemSelectionModel_currentIndex(sm, &r, &c));
    XQ_EQ_I64(r, 0);
    XClassDelete((XClass*)tw);

    /* ③ removeRow 删非当前行（区间外）⇒ 当前格不动、不发。 */
    tw = XTableWidget_create(NULL, 0);
    XQ_REQUIRE(tw != NULL);
    XTableWidget_setRowCount(tw, 3);
    XTableWidget_setColumnCount(tw, 1);
    sm = XAbstractItemView_selectionModel(&tw->m_base.m_base);
    XQ_REQUIRE(sm != NULL);
    xqTwSel_attach(tw);
    XTableWidget_setCurrentCell(tw, 1, 0);
    xqTwSel_resetProbes();
    XTableWidget_removeRow(tw, 0);
    XQ_EQ_I64(XTableWidget_currentRow(tw), 1);
    XQ_EQ_I64(g_twselCurCellHits, 0);
    /* 三处一致（返工⑤单一事实源巡检）：视图 == 模型 == 发射快照。 */
    XQ_CHECK(XItemSelectionModel_currentIndex(sm, &r, &c));
    XQ_EQ_I64(r, XTableWidget_currentRow(tw));
    XQ_EQ_I64(tw->m_selectionRow, XTableWidget_currentRow(tw));
    XClassDelete((XClass*)tw);

    /* ④ 整轴删空 ⇒ 当前格清无效并发一次。 */
    tw = XTableWidget_create(NULL, 0);
    XQ_REQUIRE(tw != NULL);
    XTableWidget_setRowCount(tw, 1);
    XTableWidget_setColumnCount(tw, 2);
    sm = XAbstractItemView_selectionModel(&tw->m_base.m_base);
    XQ_REQUIRE(sm != NULL);
    xqTwSel_attach(tw);
    XTableWidget_setCurrentCell(tw, 0, 1);
    xqTwSel_resetProbes();
    XTableWidget_setRowCount(tw, 0);
    XQ_EQ_I64(XTableWidget_currentRow(tw), -1);
    XQ_EQ_I64(g_twselCurCellHits, 1);
    XQ_CHECK(!XItemSelectionModel_currentIndex(sm, NULL, NULL));
    XClassDelete((XClass*)tw);
}

/** @brief 返工⑤：removeColumn 同一条就近迁移路径 + 当前格三处恒一致。
 */
XQ_TEST(XTableWidgetSelection, removeColumnMigratesCurrent)
{
    XTableWidget* tw = XTableWidget_create(NULL, 0);
    XItemSelectionModel* sm;
    int r = -1;
    int c = -1;
    XQ_REQUIRE(tw != NULL);
    XTableWidget_setRowCount(tw, 2);
    XTableWidget_setColumnCount(tw, 4);
    sm = XAbstractItemView_selectionModel(&tw->m_base.m_base);
    XQ_REQUIRE(sm != NULL);
    xqTwSel_attach(tw);

    /* 删当前列 ⇒ 迁左邻（Qt：start>0 ⇒ start-1）+ 发射。 */
    XTableWidget_setCurrentCell(tw, 1, 2);
    xqTwSel_resetProbes();
    XTableWidget_removeColumn(tw, 2);
    XQ_EQ_I64(XTableWidget_currentRow(tw), 1);
    XQ_EQ_I64(XTableWidget_currentColumn(tw), 1);
    XQ_EQ_I64(g_twselCurCellHits, 1);
    XQ_CHECK(XItemSelectionModel_currentIndex(sm, &r, &c));
    XQ_EQ_I64(r, 1);
    XQ_EQ_I64(c, 1);
    XQ_EQ_I64(tw->m_selectionColumn, 1);

    /* 删非当前列（区间外）⇒ 当前格**不动**、不发（Qt：QModelIndex 按
     * 删除后列号重解释，仅落在被删区间才就近迁移）。删除前 current 在
     * 列 1 ⇒ 删列 0 后数据左移一格，但 current 行号仍是列 1（Qt 口径）。 */
    xqTwSel_resetProbes();
    XTableWidget_removeColumn(tw, 0);
    XQ_EQ_I64(XTableWidget_currentColumn(tw), 1);
    XQ_EQ_I64(g_twselCurCellHits, 0);
    XQ_CHECK(XItemSelectionModel_currentIndex(sm, &r, &c));
    XQ_EQ_I64(c, 1);
    XQ_EQ_I64(tw->m_selectionColumn, XTableWidget_currentColumn(tw));

    XClassDelete((XClass*)tw);
}

/** @brief 返工①③：多格选区排序后随条目搬移且恰好一次发射（含当前格）。
 *
 *  @details 复核缺陷①的加强锁：两格离散选区 + 当前格在排序后必须各跟
 *           各的条目到新行（changePersistentIndexList 语义），发射恰一次
 *           itemSelectionChanged（批量重建单次发射），事务闸无残留；
 *           同序重复排序是幂等 ⇒ 0 发射。
 */
XQ_TEST(XTableWidgetSelection, sortFollowsMultiCellSelection)
{
    XTableWidget* tw = XTableWidget_create(NULL, 0);
    XItemSelectionModel* sm;
    XTableWidgetItem* it;
    int r = -1;
    int c = -1;
    XQ_REQUIRE(tw != NULL);
    XTableWidget_setRowCount(tw, 4);
    XTableWidget_setColumnCount(tw, 2);
    /* 行序 d / b / c / a ⇒ 升序后 a/b/c/d。 */
    XTableWidget_setItem(tw, 0, 0,
        XTableWidgetItem_create_utf8("d", XTableWidgetItem_Type));
    XTableWidget_setItem(tw, 1, 0,
        XTableWidgetItem_create_utf8("b", XTableWidgetItem_Type));
    XTableWidget_setItem(tw, 2, 0,
        XTableWidgetItem_create_utf8("c", XTableWidgetItem_Type));
    XTableWidget_setItem(tw, 3, 0,
        XTableWidgetItem_create_utf8("a", XTableWidgetItem_Type));
    sm = XAbstractItemView_selectionModel(&tw->m_base.m_base);
    XQ_REQUIRE(sm != NULL);
    xqTwSel_attach(tw);
    /* 选 "d"（行 0）与 "a"（行 3），当前格 "a"。当前格移动用 NoUpdate：
     * 2 参入口缺省 ClearAndSelect 会清掉两格选区（Qt 同），破坏本用例
     * 「多格选区」的前提。 */
    XItemSelectionModel_select(sm, 0, 0,
                               XItemSelectionModelSelectionFlag_ClearAndSelect);
    XItemSelectionModel_select(sm, 3, 0,
                               XItemSelectionModelSelectionFlag_Select);
    XTableWidget_setCurrentCell_2(tw, 3, 0,
                                  XItemSelectionModelSelectionFlag_NoUpdate);

    xqTwSel_resetProbes();
    XTableWidget_sortItems(tw, 0, 0);
    /* 升序后行序 a/b/c/d：选区跟到 "a"(行0) 与 "d"(行3)。 */
    XQ_CHECK_MSG(XItemSelectionModel_isSelected(sm, 0, 0), "a 的选中跟到行 0");
    XQ_CHECK_MSG(XItemSelectionModel_isSelected(sm, 3, 0), "d 的选中跟到行 3");
    XQ_CHECK_MSG(!XItemSelectionModel_isSelected(sm, 1, 0) &&
                 !XItemSelectionModel_isSelected(sm, 2, 0),
                 "中间行不得误报选中");
    XQ_EQ_I64(XTableWidget_selectedItems(tw, NULL, 0), 2);
    /* 当前格跟到 "a" 的新行。 */
    XQ_EQ_I64(XTableWidget_currentRow(tw), 0);
    XQ_CHECK(XItemSelectionModel_currentIndex(sm, &r, &c));
    XQ_EQ_I64(r, 0);
    it = XTableWidget_currentItem(tw);
    XQ_REQUIRE(it != NULL);
    XQ_EQ_STR(XTableWidgetItem_text_toUtf8(it), "a");
    /* 一次发射 + 无闸残留。 */
    XQ_EQ_I64(g_twselSelHits, 1);
    XQ_EQ_I64(tw->m_selSyncDepth, 0);

    /* 同序重复排序：集合与当前格都不变 ⇒ 0 发射（幂等）。 */
    xqTwSel_resetProbes();
    XTableWidget_sortItems(tw, 0, 0);
    XQ_EQ_I64(g_twselSelHits, 0);
    XQ_EQ_I64(g_twselCurCellHits, 0);
    XQ_EQ_I64(XTableWidget_selectedItems(tw, NULL, 0), 2);

    XClassDelete((XClass*)tw);
}

/** @brief 返工③：大选区 + 行列增删的行为级回归（批量重建 O(N) 的正确性面）。
 *
 *  @details 500×16 全选后插入/删除一行：选中集合应整体平移且计数守恒。
 *           性能面（探针实测）见 DomainResult openNotes——本用例锁语义。
 */
XQ_TEST(XTableWidgetSelection, largeSelectionRemapIntegrity)
{
    XTableWidget* tw = XTableWidget_create(NULL, 0);
    XItemSelectionModel* sm;
    XQ_REQUIRE(tw != NULL);
    XTableWidget_setRowCount(tw, 500);
    XTableWidget_setColumnCount(tw, 16);
    sm = XAbstractItemView_selectionModel(&tw->m_base.m_base);
    XQ_REQUIRE(sm != NULL);
    XItemSelectionModel_selectSelection(sm, 0, 0, 499, 15,
                                        XItemSelectionModelSelectionFlag_Select);
    XQ_EQ_I64(XItemSelectionModel_selectedCount(sm), 500 * 16);

    /* 头插一行 ⇒ 全部下移一行，计数守恒、首尾行号正确。 */
    XTableWidget_insertRow(tw, 0);
    XQ_EQ_I64(XItemSelectionModel_selectedCount(sm), 500 * 16);
    /* 行 0 前插 ⇒ 原 r → r+1；新行 0 未选中、原末行 499 → 500。 */
    XQ_CHECK(!XItemSelectionModel_isSelected(sm, 0, 0));
    XQ_CHECK(!XItemSelectionModel_isSelected(sm, 0, 15));
    XQ_CHECK(XItemSelectionModel_isSelected(sm, 1, 0));
    XQ_CHECK(XItemSelectionModel_isSelected(sm, 500, 15));
    XQ_CHECK(!XItemSelectionModel_isSelected(sm, 501, 0)); /* 越界无幽灵。 */

    /* 删掉新行 0 ⇒ 全部回到原位。 */
    XTableWidget_removeRow(tw, 0);
    XQ_EQ_I64(XItemSelectionModel_selectedCount(sm), 500 * 16);
    XQ_CHECK(XItemSelectionModel_isSelected(sm, 0, 0));
    XQ_CHECK(XItemSelectionModel_isSelected(sm, 499, 15));
    XQ_CHECK(!XItemSelectionModel_isSelected(sm, 500, 0));

    /* 删掉选中行 7（中段）⇒ 计数减 16，后续行前移（原 8 → 7，仍选中）。 */
    XTableWidget_removeRow(tw, 7);
    XQ_EQ_I64(XItemSelectionModel_selectedCount(sm), 499 * 16);
    XQ_CHECK(XItemSelectionModel_isSelected(sm, 6, 0));
    XQ_CHECK(XItemSelectionModel_isSelected(sm, 7, 0));
    XQ_CHECK(XItemSelectionModel_isSelected(sm, 7, 15));
    XQ_CHECK(!XItemSelectionModel_isSelected(sm, 499, 0)); /* 原 499→498。 */

    XClassDelete((XClass*)tw);
}

/* ==================== ⑭ B15perf：排序插入持久迁移 + 稳定排序（2026-11-17） ==================== */

/* 对齐依据（Qt 6.8.3 原文 + 本机 Qt 6.8.0 offscreen 探针）：
 * - `QTableModel::setItem` 的 sorted 分支走 `moveRows(...)`
 *   （qtablewidget.cpp:178-198）⇒ 选择模型的持久索引随行搬移；
 * - `QTableModel::updateRowIndexes/changePersistentIndexList`
 *   （qtablewidget.cpp:600-611、533-537）给出「from→to」映射口径；
 * - 探针实测（sortingEnabled 下 setItem 触发行搬移）：
 *   rowsMoved=1、itemSelectionChanged=0、currentItemChanged=0、
 *   currentCellChanged=0，且选区/当前格**都跟到新行**；
 *   `QTableWidget::sortItems` 同（sel=cur=0，选区跟条目）。
 *   向上（row3→row1）/向下（row0→row2）两方向均如此。
 * - `QTableModel::sort` 用 `std::stable_sort`（qtablewidget.cpp:503-514），
 *   空条目 unsortable 行按原序追加末尾（:506-511、526-529）；探针输出
 *   （键 2,1,2,1,3,1 + 标签 A..F）：
 *   升序标签 B,D,F,A,C,E；降序 E,A,C,B,D,F；空行夹具（NULL/b/NULL/a/b +
 *   R0..R4）升序标签 R3,R1,R4,R0,R2（非空排序、空行按原序沉底）。
 * 注：本库 `XTableWidget_sortItems` 保留 §B4 既有契约「排序后发一次
 * itemSelectionChanged」（回归锁 sortFollowsMultiCellSelection），与 Qt
 * 实测 0 发射是**已知偏差**；本组新用例只锁「选区/当前格跟随」与稳定序，
 * 不改变该既有发射口径。 */

/** @brief 建 DisplayRole=int 的条目（数值键走整型档比较，稳定/性能用例夹具）。 */
static XTableWidgetItem* xqTwSel_makeIntItem(int value)
{
    XTableWidgetItem* it =
        XTableWidgetItem_create_utf8("", XTableWidgetItem_Type);
    XVariant* v;
    if (!it) return NULL;
    v = XVariant_create_int(value);
    if (!v) {
        XClassDelete((XClass*)it);
        return NULL;
    }
    XTableWidgetItem_setData(it, XItemDataRole_DisplayRole, v);
    XClassDelete((XClass*)v);
    return it;
}

/** @brief 断言某列自上而下的文本序列（逐行给出行号便于定位）。 */
static void xqTwSel_checkTexts(XTableWidget* tw, int column,
                               const char* const* want, int n)
{
    int i;
    for (i = 0; i < n; ++i) {
        const XTableWidgetItem* it = XTableWidget_item_const(tw, i, column);
        const char* got = it ? XTableWidgetItem_text_toUtf8(it) : NULL;
        XQ_CHECK_MSG(got != NULL && strcmp(got, want[i]) == 0,
                     "row %d col %d: got \"%s\" want \"%s\"", i, column,
                     got ? got : "(null)", want[i]);
    }
}

/** @brief 已升序的 3 行 2 列表格 + sortingEnabled（列 0 升序指示段）。
 *  @details 夹具行序 b/d/x（已是升序）⇒ `setSortingEnabled(true)` 的立即
 *           排序不改变次序；随后 setItem 新行即走 sortedInsert 分支。 */
static XTableWidget* xqTwSel_makeSortedTable(void)
{
    XTableWidget* tw = XTableWidget_create(NULL, 0);
    XHeaderView* hh;
    if (!tw) return NULL;
    XTableWidget_setRowCount(tw, 3);
    XTableWidget_setColumnCount(tw, 2);
    XTableWidget_setItem(tw, 0, 0,
        XTableWidgetItem_create_utf8("b", XTableWidgetItem_Type));
    XTableWidget_setItem(tw, 0, 1,
        XTableWidgetItem_create_utf8("B", XTableWidgetItem_Type));
    XTableWidget_setItem(tw, 1, 0,
        XTableWidgetItem_create_utf8("d", XTableWidgetItem_Type));
    XTableWidget_setItem(tw, 1, 1,
        XTableWidgetItem_create_utf8("D", XTableWidgetItem_Type));
    XTableWidget_setItem(tw, 2, 0,
        XTableWidgetItem_create_utf8("x", XTableWidgetItem_Type));
    XTableWidget_setItem(tw, 2, 1,
        XTableWidgetItem_create_utf8("X", XTableWidgetItem_Type));
    hh = XTableView_horizontalHeader(&tw->m_base);
    if (!hh) return tw;
    /* 先落排序指示段再开 sortingEnabled（Qt setSortingEnabled(true) 立即
     * 按指示段排一次，qtableview.cpp:2744-2746）。 */
    XHeaderView_setSortIndicator(hh, 0, 0);
    XTableWidget_setSortingEnabled(tw, true);
    return tw;
}

/** @brief sortingEnabled 下 setItem 触发行上移：选区/当前格跟条目、静默。
 *
 *  @details 旧实现 `xtw_moveRow` 只搬行指针，选择模型的 (row,col) 索引留在
 *           原行号 ⇒ 高亮落到别的数据行。Qt 由 moveRows 的持久索引迁移
 *           跟随（qtablewidget.cpp:194-197）。像素侧同时验证：搬移后的新
 *           行真的画了 Highlight 底、旧行没有（不只是模型状态自洽）。 */
XQ_TEST(XTableWidgetSelection, sortingEnabledInsertFollowsSelection)
{
    XTableWidget* tw = xqTwSel_makeSortedTable();
    XItemSelectionModel* sm;
    XTableWidgetItem* xItem;
    XPalette palette;
    XImage* img;
    XRect rect;
    const uint32_t highlight = 0xFFFF00FFu; /* 探针高亮色（洋红）。 */
    static const char* const want[] = { "b", "c", "d", "x" };
    XQ_REQUIRE(tw != NULL);
    XQ_CHECK(XTableView_isSortingEnabled(&tw->m_base));
    sm = XAbstractItemView_selectionModel(&tw->m_base.m_base);
    XQ_REQUIRE(sm != NULL);
    xqTwSel_attach(tw);

    /* 选中 "x" 行第 1 列，当前格同格（NoUpdate：不改选区）。 */
    XQ_CHECK(XItemSelectionModel_select(
        sm, 2, 1, XItemSelectionModelSelectionFlag_ClearAndSelect));
    XTableWidget_setCurrentCell_2(tw, 2, 1,
                                  XItemSelectionModelSelectionFlag_NoUpdate);
    XQ_EQ_I64(XTableWidget_currentRow(tw), 2);
    xqTwSel_resetProbes();

    /* 追加一行写入 "c"：sortedInsert 把 row3 整行搬到 row1（b,c,d,x）。 */
    XTableWidget_setRowCount(tw, 4);
    XTableWidget_setItem(tw, 3, 1,
        XTableWidgetItem_create_utf8("C", XTableWidgetItem_Type));
    XTableWidget_setItem(tw, 3, 0,
        XTableWidgetItem_create_utf8("c", XTableWidgetItem_Type));
    xqTwSel_checkTexts(tw, 0, want, 4);

    /* ① 选择模型：选区随条目搬到 row3（"x" 行），旧行 2 不残留。 */
    XQ_CHECK_MSG(XItemSelectionModel_isSelected(sm, 3, 1),
                 "搬移后选区跟条目到新行 3");
    XQ_CHECK_MSG(!XItemSelectionModel_isSelected(sm, 2, 1),
                 "旧行 2（现装 d）不得再报选中");
    XQ_EQ_I64(XTableWidget_selectedItems(tw, NULL, 0), 1);
    xItem = XTableWidget_item(tw, 3, 1);
    XQ_REQUIRE(xItem != NULL);
    XQ_EQ_STR(XTableWidgetItem_text_toUtf8(xItem), "X");
    XQ_CHECK_MSG(XTableWidgetItem_isSelected(xItem),
                 "条目镜像位与模型同源（新行 3）");
    XQ_CHECK(!XTableWidgetItem_isSelected(XTableWidget_item(tw, 2, 1)));

    /* ② 当前格随条目搬移。 */
    XQ_EQ_I64(XTableWidget_currentRow(tw), 3);
    XQ_EQ_I64(XTableWidget_currentColumn(tw), 1);
    {
        int r = -1;
        int c = -1;
        XQ_CHECK(XItemSelectionModel_currentIndex(sm, &r, &c));
        XQ_EQ_I64(r, 3);
        XQ_EQ_I64(c, 1);
    }
    /* 「上次已发射的当前格」快照同样随迁移对齐（否则下一次真实
     * currentChanged 会带着搬移前的旧值当 prev）。 */
    XQ_EQ_I64(tw->m_selectionRow, 3);
    XQ_EQ_I64(tw->m_selectionColumn, 1);

    /* ③ 静默迁移（Qt rowsMoved 口径）：三族信号 0，事务闸无残留。 */
    XQ_EQ_I64(g_twselSelHits, 0);
    XQ_EQ_I64(g_twselCurItemHits, 0);
    XQ_EQ_I64(g_twselCurCellHits, 0);
    XQ_EQ_I64(tw->m_selSyncDepth, 0);

    /* ④ 像素双证：新行底色 == Highlight，旧行 != Highlight。 */
    palette = XPalette_create();
    XPalette_setColor(&palette, XPaletteColorGroup_Current,
                      XPaletteColorRole_Highlight,
                      XColor_create_rgb(255, 0, 255, 255));
    XWidget_setPalette((XWidget*)tw, &palette);
    XWidget_resize((XWidget*)tw, 240, 200);
    img = XWidget_grab((XWidget*)tw);
    XQ_REQUIRE(img != NULL);
    rect = XTableWidget_visualItemRect(tw, xItem);
    XQ_CHECK_MSG(!XRect_isEmpty(&rect), "选中条目应有非空视觉矩形");
    XQ_EQ_I64(XImage_pixel(img, rect.x + rect.width - 6,
                           rect.y + rect.height / 2),
              (long long)highlight);
    {
        XRect oldRect =
            XTableWidget_visualItemRect(tw, XTableWidget_item(tw, 2, 1));
        XQ_CHECK(!XRect_isEmpty(&oldRect));
        XQ_CHECK_MSG(XImage_pixel(img, oldRect.x + oldRect.width - 6,
                                  oldRect.y + oldRect.height / 2)
                         != highlight,
                     "旧行不得残留选中底（高亮必须跟着数据走）");
    }

    XClassDelete((XClass*)img);
    XClassDelete((XClass*)tw);
}

/** @brief 反向搬移（from<to）：改值使整行下移，选区/当前格仍跟条目。
 *
 *  @details 上移与下移的置换式不同（Qt updateRowIndexes 的两个分支，
 *           qtablewidget.cpp:600-611）；本用例锁 from<to 分支：
 *           行序 a/b/c，把 row0 的键改成 "z" ⇒ 整行搬到 row2。 */
XQ_TEST(XTableWidgetSelection, sortingEnabledInsertMovesRowDown)
{
    XTableWidget* tw = xqTwSel_makeSortedTable();
    XItemSelectionModel* sm;
    static const char* const wantCol0[] = { "b", "c", "z" };
    static const char* const wantCol1[] = { "B", "C", "A" };
    XQ_REQUIRE(tw != NULL);
    sm = XAbstractItemView_selectionModel(&tw->m_base.m_base);
    XQ_REQUIRE(sm != NULL);
    xqTwSel_attach(tw);

    /* 本用例要的是 a/b/c 行序：直接改三行文本（sortingEnabled 只对 setItem
     * 的排序段生效；后写的两行会各自被 sortedInsert 落位）。 */
    XTableWidget_setItem(tw, 0, 0,
        XTableWidgetItem_create_utf8("a", XTableWidgetItem_Type));
    XTableWidget_setItem(tw, 1, 0,
        XTableWidgetItem_create_utf8("b", XTableWidgetItem_Type));
    XTableWidget_setItem(tw, 2, 0,
        XTableWidgetItem_create_utf8("c", XTableWidgetItem_Type));
    XTableWidget_setItem(tw, 0, 1,
        XTableWidgetItem_create_utf8("A", XTableWidgetItem_Type));
    XTableWidget_setItem(tw, 1, 1,
        XTableWidgetItem_create_utf8("B", XTableWidgetItem_Type));
    XTableWidget_setItem(tw, 2, 1,
        XTableWidgetItem_create_utf8("C", XTableWidgetItem_Type));
    {
        /* 列 0 已按 a/b/c 就位（sortedInsert 幂等不动），列 1 只随行搬。 */
        static const char* const col0[] = { "a", "b", "c" };
        xqTwSel_checkTexts(tw, 0, col0, 3);
    }

    /* 选中 row0 第 1 列 + 当前格（同一格）。 */
    XQ_CHECK(XItemSelectionModel_select(
        sm, 0, 1, XItemSelectionModelSelectionFlag_ClearAndSelect));
    XTableWidget_setCurrentCell_2(tw, 0, 1,
                                  XItemSelectionModelSelectionFlag_NoUpdate);
    xqTwSel_resetProbes();

    /* 改 row0 键为 "z" ⇒ 该行应排到 row2（b,c,z）⇒ 整行下移。 */
    XTableWidget_setItem(tw, 0, 0,
        XTableWidgetItem_create_utf8("z", XTableWidgetItem_Type));
    xqTwSel_checkTexts(tw, 0, wantCol0, 3);
    xqTwSel_checkTexts(tw, 1, wantCol1, 3);

    /* 选区/当前格跟着原 row0 的数据（"A"）到 row2。 */
    XQ_CHECK_MSG(XItemSelectionModel_isSelected(sm, 2, 1),
                 "下移后选区跟条目到新行 2");
    XQ_CHECK(!XItemSelectionModel_isSelected(sm, 0, 1));
    {
        XTableWidgetItem* moved = XTableWidget_item(tw, 2, 1);
        XQ_REQUIRE(moved != NULL);
        XQ_EQ_STR(XTableWidgetItem_text_toUtf8(moved), "A");
        XQ_CHECK(XTableWidgetItem_isSelected(moved));
    }
    XQ_EQ_I64(XTableWidget_currentRow(tw), 2);
    XQ_EQ_I64(XTableWidget_currentColumn(tw), 1);
    /* 静默迁移同口径。 */
    XQ_EQ_I64(g_twselSelHits, 0);
    XQ_EQ_I64(g_twselCurCellHits, 0);

    XClassDelete((XClass*)tw);
}

/** @brief sortItems 稳定排序：等键保持原相对顺序、空条目按原序沉底。
 *
 *  @details Qt `QTableModel::sort` 用 std::stable_sort
 *           （qtablewidget.cpp:503-514）；空条目 unsortable 行按原序追加在
 *           末尾（:506-511、526-529）。旧实现是 O(R²) 邻位交换（也稳定），
 *           换稳定归并后此语义必须原样保留——等键次序错乱会让历史/日志页
 *           同值行的显示顺序抖动。 */
XQ_TEST(XTableWidgetSelection, sortItemsStableForEqualKeys)
{
    XTableWidget* tw = XTableWidget_create(NULL, 0);
    static const char* const ascLabels[] = { "B", "D", "F", "A", "C", "E" };
    static const char* const descLabels[] = { "E", "A", "C", "B", "D", "F" };
    static const char* const keyLabels[] = { "A", "B", "C", "D", "E", "F" };
    static const int keys[] = { 2, 1, 2, 1, 3, 1 };
    int i;
    XQ_REQUIRE(tw != NULL);
    XTableWidget_setRowCount(tw, 6);
    XTableWidget_setColumnCount(tw, 2);
    /* 列 0：整型键 2,1,2,1,3,1（等键三组）；列 1：身份标签 A..F。 */
    for (i = 0; i < 6; ++i) {
        XTableWidgetItem* it = xqTwSel_makeIntItem(keys[i]);
        XQ_REQUIRE(it != NULL);
        XTableWidget_setItem(tw, i, 0, it);
        XTableWidget_setItem(tw, i, 1,
            XTableWidgetItem_create_utf8(keyLabels[i], XTableWidgetItem_Type));
    }

    /* 升序：1(B,D,F) → 2(A,C) → 3(E)；组内保持原相对顺序。 */
    XTableWidget_sortItems(tw, 0, 0);
    xqTwSel_checkTexts(tw, 1, ascLabels, 6);

    /* 降序：3(E) → 2(A,C) → 1(B,D,F)；组内同样保序。 */
    XTableWidget_sortItems(tw, 0, 1);
    xqTwSel_checkTexts(tw, 1, descLabels, 6);

    /* 空条目（该列无 item）按原序沉底，不随排序方向翻转。 */
    XClassDelete((XClass*)tw);
    tw = XTableWidget_create(NULL, 0);
    XQ_REQUIRE(tw != NULL);
    XTableWidget_setRowCount(tw, 5);
    XTableWidget_setColumnCount(tw, 2);
    XTableWidget_setItem(tw, 0, 1,
        XTableWidgetItem_create_utf8("R0", XTableWidgetItem_Type));
    XTableWidget_setItem(tw, 1, 0,
        XTableWidgetItem_create_utf8("b", XTableWidgetItem_Type));
    XTableWidget_setItem(tw, 1, 1,
        XTableWidgetItem_create_utf8("R1", XTableWidgetItem_Type));
    XTableWidget_setItem(tw, 2, 1,
        XTableWidgetItem_create_utf8("R2", XTableWidgetItem_Type));
    XTableWidget_setItem(tw, 3, 0,
        XTableWidgetItem_create_utf8("a", XTableWidgetItem_Type));
    XTableWidget_setItem(tw, 3, 1,
        XTableWidgetItem_create_utf8("R3", XTableWidgetItem_Type));
    XTableWidget_setItem(tw, 4, 0,
        XTableWidgetItem_create_utf8("b", XTableWidgetItem_Type));
    XTableWidget_setItem(tw, 4, 1,
        XTableWidgetItem_create_utf8("R4", XTableWidgetItem_Type));
    XTableWidget_sortItems(tw, 0, 0);
    {
        static const char* const col0[] = { "a", "b", "b", "", "" };
        static const char* const col1[] = { "R3", "R1", "R4", "R0", "R2" };
        int r;
        for (r = 0; r < 5; ++r) {
            const XTableWidgetItem* it = XTableWidget_item_const(tw, r, 0);
            if (r < 3) {
                XQ_CHECK_MSG(it != NULL &&
                                 strcmp(XTableWidgetItem_text_toUtf8(it),
                                        col0[r]) == 0,
                             "row %d 应装 %s", r, col0[r]);
            } else {
                XQ_CHECK_MSG(it == NULL, "空条目行 %d 应沉底", r);
            }
        }
        xqTwSel_checkTexts(tw, 1, col1, 5);
    }

    XClassDelete((XClass*)tw);
}

/** @brief 旧 sortItems 的排序本体（O(R²) 邻位交换；**仅作性能基线**）。
 *  @details 与旧实现的比较/交换式逐行同构（升序 `lessThan(next,cur)`），
 *           只用于本用例的时间基准，不参与生产路径。 */
static void xqTwSel_legacyBubble(XTableWidgetItem** items, int n, int order)
{
    int i;
    int j;
    for (i = 0; i < n; ++i) {
        for (j = 0; j < n - 1 - i; ++j) {
            bool swap = (order == 0)
                ? XTableWidgetItem_lessThan(items[j + 1], items[j])
                : XTableWidgetItem_lessThan(items[j], items[j + 1]);
            if (swap) {
                XTableWidgetItem* t = items[j];
                items[j] = items[j + 1];
                items[j + 1] = t;
            }
        }
    }
}

/** @brief 500 行排序耗时不超过旧 O(R²) 基线的 2 倍（探针留证）。
 *
 *  @details 旧实现 500 行×100 次实测 27.9ms/次（§B4 台账）。为免机器相关
 *           的硬阈值，本用例在同一进程内跑**旧算法本体**作基线（同数据、
 *           同比较器、同迭代数），断言新旧耗时比 ≤ 2。两算法都随机器快慢
 *           等比缩放，阈值不随机器漂移；耗时数据打到 stdout 留证。 */
XQ_TEST(XTableWidgetSelection, sortItemsPerformanceVsLegacy)
{
    enum { XQ_TW_SORT_N = 500, XQ_TW_SORT_ITERS = 10 };
    XTableWidget* tw = XTableWidget_create(NULL, 0);
    XTableWidgetItem** items;
    XTableWidgetItem** base;
    clock_t t0;
    clock_t t1;
    double legacyMs;
    double newMs;
    int i;
    XQ_REQUIRE(tw != NULL);
    XTableWidget_setRowCount(tw, XQ_TW_SORT_N);
    XTableWidget_setColumnCount(tw, 1);
    /* 本地数组走框架分配器：运行器泄漏门按框架计数，malloc 数组不计账。 */
    items = (XTableWidgetItem**)XMalloc_System(sizeof(XTableWidgetItem*) *
                                               (size_t)XQ_TW_SORT_N);
    base = (XTableWidgetItem**)XMalloc_System(sizeof(XTableWidgetItem*) *
                                              (size_t)XQ_TW_SORT_N);
    XQ_REQUIRE(items != NULL);
    XQ_REQUIRE(base != NULL);
    for (i = 0; i < XQ_TW_SORT_N; ++i) {
        /* 173 与 500 互素 ⇒ (i*173)%500 是 0..499 的一个置换（无重复、
         * 打乱），整型档比较快且结果可判。 */
        XTableWidgetItem* it = xqTwSel_makeIntItem((i * 173) % XQ_TW_SORT_N);
        XQ_REQUIRE(it != NULL);
        XTableWidget_setItem(tw, i, 0, it);
        items[i] = it;
    }
    memcpy(base, items, sizeof(XTableWidgetItem*) * (size_t)XQ_TW_SORT_N);

    t0 = clock();
    for (i = 0; i < XQ_TW_SORT_ITERS; ++i)
        xqTwSel_legacyBubble(base, XQ_TW_SORT_N, 0);
    t1 = clock();
    legacyMs = (double)(t1 - t0) * 1000.0 / (double)CLOCKS_PER_SEC;

    t0 = clock();
    for (i = 0; i < XQ_TW_SORT_ITERS; ++i)
        XTableWidget_sortItems(tw, 0, 0);
    t1 = clock();
    newMs = (double)(t1 - t0) * 1000.0 / (double)CLOCKS_PER_SEC;

    fprintf(stdout,
            "[tw-sort-perf] 500 行 × %d 次：旧 O(R²) 基线 %.3f ms，"
            "新稳定归并 %.3f ms（比值 %.3fx）\n",
            XQ_TW_SORT_ITERS, legacyMs, newMs,
            legacyMs > 0.0 ? newMs / legacyMs : 0.0);
    fflush(stdout);
    XQ_CHECK_MSG(legacyMs > 0.0, "基线耗时应可测（时钟分辨率问题）");
    XQ_CHECK_MSG(newMs <= legacyMs * 2.0,
                 "新排序 %.3f ms 不得慢于旧基线 %.3f ms 的 2 倍",
                 newMs, legacyMs);

    /* 结果有序（排序正确性面）。 */
    for (i = 1; i < XQ_TW_SORT_N; ++i) {
        const XVariant* a = XTableWidgetItem_data(
            XTableWidget_item_const(tw, i - 1, 0), XItemDataRole_DisplayRole);
        const XVariant* b = XTableWidgetItem_data(
            XTableWidget_item_const(tw, i, 0), XItemDataRole_DisplayRole);
        if (!(XVariant_toInt64(a) < XVariant_toInt64(b))) {
            XQ_CHECK_MSG(false, "row %d 排序失序：%lld !< %lld", i,
                         (long long)XVariant_toInt64(a),
                         (long long)XVariant_toInt64(b));
            break;
        }
    }
    XQ_EQ_I64(XVariant_toInt64(XTableWidgetItem_data(
                  XTableWidget_item_const(tw, 0, 0),
                  XItemDataRole_DisplayRole)), 0);
    XQ_EQ_I64(XVariant_toInt64(XTableWidgetItem_data(
                  XTableWidget_item_const(tw, XQ_TW_SORT_N - 1, 0),
                  XItemDataRole_DisplayRole)), XQ_TW_SORT_N - 1);

    XFree_System(base);
    XFree_System(items);
    XClassDelete((XClass*)tw);
}

#endif /* XGUI_ON && XWIDGET_ON && XTABLEWIDGET_ON */
