/**
 * @file       XSelectionRangeAlignTest.c
 * @brief      §selrange 选择模型范围表 + selectedRanges 直读 + findItems
 *             正则/通配 × Qt 6.8 对齐契约测试。
 * @details    取证链：
 *             - Qt 6.8.3 原文 qitemselectionmodel.cpp:1284-1330（select 命令
 *               历史）、qitemselectionmodel_p.h:66-83（finalize/ranges/
 *               currentSelection/currentCommand）、:1816-1825（selection）、
 *               :1901-1907（emitSelectionChanged 幂等判定）、:1426-1456
 *               （isSelected 的 ranges + 当前命令叠加）；
 *             - Qt 6.8.3 原文 qtablewidget.cpp:2381-2392（selectedRanges 直读
 *               selection()）、:2424-2437（findItems 列优先）；
 *             - Qt 6.8.3 原文 qabstractitemmodel.cpp:2545-2577（正则/通配
 *               match 分支）、qregularexpression.cpp:1930-2033
 *               （wildcardToRegularExpression 的锚定与 NonPath 口径）；
 *             - 本机 Qt 6.8.0 探针（/tmp/selrange_probe/probe.cpp、
 *               find_probe2.cpp、find_probe3.cpp，QT_QPA_PLATFORM=offscreen）
 *               实测：① 先整列 0 再整行 0 ⇒ 两条 [1,0,4,0]+[0,0,0,4]；
 *               ② 洞形 3×3 减中心 ⇒ 4 条（上/下/左/右）；③ 逐格 Select
 *               三条 1×1；④ findItems("Al.*", MatchRegularExpression)=3、
 *               +CS=2、("Al*", MatchWildcard)=2、+CS=1、("*a*",WC)=3；
 *               ⑤ 空 needle：RE=6（全格）、WC=3（仅空格，全锚定）；
 *               ⑥ 非法 pattern "[" ⇒ 0 条（Qt 打印警告）。
 *
 *             覆盖：① 范围表 merge/select/clear 与 Qt 命令历史切分逐例一致
 *             （含 L 形/十字形/洞形/Toggle/累积序列）；② selectSelection
 *             8000 格 <30ms（探针留证）；③ findItems 正则/通配/大小写真命中
 *             （含空 needle、非法 pattern 边界）；④ §B4 镜像位/快照与范围表
 *             共存 + 单次发射；⑤ selectedRanges 直读范围表；⑥ selection()
 *             与 selectedIndexes 顺序同源。
 * @note       C99；自建对象全部显式释放，漏删即运行器判 `[LEAK]`。
 *             XItemSelection 值对象一律配对 init/deinit。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

#if XGUI_ON && XWIDGET_ON && XTABLEWIDGET_ON

#include <time.h>  /* clock()：8000 格性能探针。 */
#include <stdio.h>  /* fprintf：探针耗时留证（--log 亦留存）。 */
#include "XItemSelection.h"
#include "XItemSelectionModel.h"
#include "XAbstractItemModel.h"
#include "XAbstractItemView.h"
#include "XTableWidget.h"

/* ==================== 辅助 ==================== */

/** @brief 建 rows×cols 模型并绑定到选择模型。 */
static XAbstractItemModel* xqSr_makeModel(XItemSelectionModel* sm, int rows,
                                           int cols)
{
    XAbstractItemModel* m = XAbstractItemModel_create();
    if (m) {
        XAbstractItemModel_setDimension(m, rows, cols);
        XItemSelectionModel_setModel(sm, m);
    }
    return m;
}

/** @brief 取 selection() 第 i 条范围的四边，写到输出（越界全置 -1）。 */
static void xqSr_rangeAt(const XItemSelection* sel, int i, int* top,
                          int* left, int* bottom, int* right)
{
    XItemSelectionRange r = XItemSelection_at(sel, i);
    *top = XItemSelectionRange_top(&r);
    *left = XItemSelectionRange_left(&r);
    *bottom = XItemSelectionRange_bottom(&r);
    *right = XItemSelectionRange_right(&r);
}

/** @brief 断言第 i 条范围等于期望四边（带说明）。 */
#define XQ_SR_RANGE(sel, i, t, l, b, r)                                     \
    do {                                                                    \
        int xq_t_, xq_l_, xq_b_, xq_r_;                                     \
        xqSr_rangeAt((sel), (i), &xq_t_, &xq_l_, &xq_b_, &xq_r_);           \
        XQ_EQ_I64(xq_t_, (t));                                              \
        XQ_EQ_I64(xq_l_, (l));                                              \
        XQ_EQ_I64(xq_b_, (b));                                              \
        XQ_EQ_I64(xq_r_, (r));                                              \
    } while (0)

/* ==================== ① 命令历史切分（Qt 探针逐例） ==================== */

/** @brief 十字形/L 形 select 序列 ⇒ Qt 命令历史切分（不再逐行重建）。 */
XQ_TEST(XSelectionRange, qtCommandHistoryCross)
{
    XItemSelectionModel* sm = XItemSelectionModel_create();
    XAbstractItemModel* m;
    XItemSelection view;
    int n;
    XQ_REQUIRE(sm != NULL);
    m = xqSr_makeModel(sm, 5, 5);
    XQ_REQUIRE(m != NULL);

    /* Qt 探针 L1：先整列 0（行 0..4、列 0）⇒ 1 条 [0,0,4,0]。 */
    XQ_CHECK(XItemSelectionModel_selectSelection(
        sm, 0, 0, 4, 0, XItemSelectionModelSelectionFlag_Select));
    (void)XItemSelection_init(&view);
    n = XItemSelectionModel_selection(sm, &view);
    XQ_EQ_I64(n, 1);
    XQ_SR_RANGE(&view, 0, 0, 0, 4, 0);
    XItemSelection_deinit(&view);

    /* Qt 探针 L2-cross：再整行 0（列 0..4）⇒ 2 条，顺序敏感：
     * [1,0,4,0]（列 0 被 split 掉交点 (0,0) 后的下块）+ [0,0,0,4]（行 0）。
     * 旧的「逐行重建」会把列 0 拆成 4 条 1×1 或与行 0 合成别的形状，
     * 与 Qt 命令历史口径不一致。 */
    XQ_CHECK(XItemSelectionModel_selectSelection(
        sm, 0, 0, 0, 4, XItemSelectionModelSelectionFlag_Select));
    (void)XItemSelection_init(&view);
    n = XItemSelectionModel_selection(sm, &view);
    XQ_EQ_I64(n, 2);
    XQ_SR_RANGE(&view, 0, 1, 0, 4, 0);
    XQ_SR_RANGE(&view, 1, 0, 0, 0, 4);
    XItemSelection_deinit(&view);

    /* 反向序列（Qt 探针 R2-cross）：先整行 0 再整列 0 ⇒ 同样是 2 条，
     * 但顺序与切分块相反：[0,1,0,4] + [0,0,4,0]。 */
    XItemSelectionModel_clearSelection(sm);
    XQ_CHECK(XItemSelectionModel_selectSelection(
        sm, 0, 0, 0, 4, XItemSelectionModelSelectionFlag_Select));
    XQ_CHECK(XItemSelectionModel_selectSelection(
        sm, 0, 0, 4, 0, XItemSelectionModelSelectionFlag_Select));
    (void)XItemSelection_init(&view);
    n = XItemSelectionModel_selection(sm, &view);
    XQ_EQ_I64(n, 2);
    XQ_SR_RANGE(&view, 0, 0, 1, 0, 4);
    XQ_SR_RANGE(&view, 1, 0, 0, 4, 0);
    XItemSelection_deinit(&view);

    XClassDelete((XClass*)sm);
    XClassDelete((XClass*)m);
}

/** @brief 洞形（3×3 减中心）⇒ Qt split 上/下/左/右 4 条；累积/Clear/Toggle
 *         序列逐例一致；clear 后双空幂等。 */
XQ_TEST(XSelectionRange, qtCommandHistoryMergeClear)
{
    XItemSelectionModel* sm = XItemSelectionModel_create();
    XAbstractItemModel* m;
    XItemSelection view;
    int n;
    XQ_REQUIRE(sm != NULL);
    m = xqSr_makeModel(sm, 5, 5);
    XQ_REQUIRE(m != NULL);

    /* Qt 探针 H1：3×3 全选 ⇒ 1 条。 */
    XQ_CHECK(XItemSelectionModel_selectSelection(
        sm, 0, 0, 2, 2, XItemSelectionModelSelectionFlag_Select));
    (void)XItemSelection_init(&view);
    XQ_EQ_I64(XItemSelectionModel_selection(sm, &view), 1);
    XQ_SR_RANGE(&view, 0, 0, 0, 2, 2);
    XItemSelection_deinit(&view);

    /* Qt 探针 H2-hole：Deselect 中心 (1,1) ⇒ 4 条，split 顺序上/下/左/右。 */
    XQ_CHECK(XItemSelectionModel_select(
        sm, 1, 1, XItemSelectionModelSelectionFlag_Deselect));
    (void)XItemSelection_init(&view);
    n = XItemSelectionModel_selection(sm, &view);
    XQ_EQ_I64(n, 4);
    XQ_SR_RANGE(&view, 0, 0, 0, 0, 2);
    XQ_SR_RANGE(&view, 1, 2, 0, 2, 2);
    XQ_SR_RANGE(&view, 2, 1, 0, 1, 0);
    XQ_SR_RANGE(&view, 3, 1, 2, 1, 2);
    XItemSelection_deinit(&view);
    XQ_CHECK(!XItemSelectionModel_isSelected(sm, 1, 1));
    XQ_CHECK(XItemSelectionModel_isSelected(sm, 0, 0));

    /* Qt 探针 C1/C2/C3：ClearAndSelect 后连续 Select ⇒ 范围表逐条累积，
     * 顺序 = 选区建立顺序（幂等：重复 Select 同格不新增 range）。 */
    XQ_CHECK(XItemSelectionModel_select(
        sm, 1, 1, XItemSelectionModelSelectionFlag_ClearAndSelect));
    XQ_CHECK(XItemSelectionModel_select(
        sm, 3, 3, XItemSelectionModelSelectionFlag_Select));
    XQ_CHECK(XItemSelectionModel_select(
        sm, 2, 2, XItemSelectionModelSelectionFlag_Select));
    (void)XItemSelection_init(&view);
    n = XItemSelectionModel_selection(sm, &view);
    XQ_EQ_I64(n, 3);
    XQ_SR_RANGE(&view, 0, 1, 1, 1, 1);
    XQ_SR_RANGE(&view, 1, 3, 3, 3, 3);
    XQ_SR_RANGE(&view, 2, 2, 2, 2, 2);
    XItemSelection_deinit(&view);
    /* 幂等：重复 Select 已选中格 ⇒ 范围表不变、返回 false。 */
    XQ_CHECK(!XItemSelectionModel_select(
        sm, 3, 3, XItemSelectionModelSelectionFlag_Select));
    XQ_EQ_I64(XItemSelectionModel_selection(sm, NULL), 3);

    /* Qt 探针 T-two-toggle：两次 Toggle 独立格 ⇒ 2 条 1×1。 */
    XItemSelectionModel_clearSelection(sm);
    XQ_CHECK(XItemSelectionModel_select(
        sm, 0, 0, XItemSelectionModelSelectionFlag_Toggle));
    XQ_CHECK(XItemSelectionModel_select(
        sm, 0, 1, XItemSelectionModelSelectionFlag_Toggle));
    (void)XItemSelection_init(&view);
    n = XItemSelectionModel_selection(sm, &view);
    XQ_EQ_I64(n, 2);
    XQ_SR_RANGE(&view, 0, 0, 0, 0, 0);
    XQ_SR_RANGE(&view, 1, 0, 1, 0, 1);
    XItemSelection_deinit(&view);

    /* clear：范围表与平铺镜像双空；重复 clear 幂等（Qt emitSelectionChanged
     * 的 old/new 双空早退）。 */
    XQ_CHECK(XItemSelectionModel_clearSelection(sm));
    XQ_EQ_I64(XItemSelectionModel_selection(sm, NULL), 0);
    XQ_EQ_I64(XItemSelectionModel_selectedCount(sm), 0);
    XQ_CHECK(!XItemSelectionModel_clearSelection(sm));

    XClassDelete((XClass*)sm);
    XClassDelete((XClass*)m);
}

/* ==================== ② 8000 格性能探针 ==================== */

/** @brief selectSelection 8000 格 <30ms（范围 merge O(R)，退役逐格 O(N²)）。 */
XQ_TEST(XSelectionRange, largeSelectPerformance)
{
    XItemSelectionModel* sm = XItemSelectionModel_create();
    XAbstractItemModel* m;
    clock_t t0;
    clock_t t1;
    double ms;
    XQ_REQUIRE(sm != NULL);
    m = xqSr_makeModel(sm, 80, 100); /* 80×100 = 8000 格。 */
    XQ_REQUIRE(m != NULL);

    t0 = clock();
    XQ_CHECK(XItemSelectionModel_selectSelection(
        sm, 0, 0, 79, 99, XItemSelectionModelSelectionFlag_Select));
    t1 = clock();
    ms = (double)(t1 - t0) * 1000.0 / (double)CLOCKS_PER_SEC;
    /* 探针留证：实测耗时直接打到 stdout（运行器 --log 亦留存），断言复核。 */
    fprintf(stdout, "[selrange-perf] selectSelection 8000 格 = %.3f ms "
                    "(阈值 30ms，旧逐格 O(N²) 约 300ms)\n", ms);
    fflush(stdout);
    XQ_CHECK_MSG(ms < 30.0, "selectSelection 8000 格耗时 %.3f ms（阈值 30ms）",
                 ms);
    XQ_EQ_I64(XItemSelectionModel_selectedCount(sm), 8000);
    /* 一条矩形范围（不是 8000 条 1×1）——范围表为事实源。 */
    XQ_EQ_I64(XItemSelectionModel_selection(sm, NULL), 1);
    {
        XItemSelection view;
        (void)XItemSelection_init(&view);
        XQ_EQ_I64(XItemSelectionModel_selection(sm, &view), 1);
        XQ_SR_RANGE(&view, 0, 0, 0, 79, 99);
        XItemSelection_deinit(&view);
    }
    /* 8000 格 isSelected 抽样仍走范围表（O(R)=1）。 */
    XQ_CHECK(XItemSelectionModel_isSelected(sm, 40, 50));
    XQ_CHECK(!XItemSelectionModel_isSelected(sm, 80, 0));

    XClassDelete((XClass*)sm);
    XClassDelete((XClass*)m);
}

/* ==================== ③ findItems 正则/通配 ==================== */

/** @brief 建 §B4 同款 3×2 夹具（列 0 三行有文本，列 1 全空格）。 */
static XTableWidget* xqSr_makeFindTable(void)
{
    XTableWidget* tw = XTableWidget_create(NULL, 0);
    if (!tw) return NULL;
    XTableWidget_setRowCount(tw, 3);
    XTableWidget_setColumnCount(tw, 2);
    XTableWidget_setItem(tw, 0, 0,
        XTableWidgetItem_create_utf8("Alpha", XTableWidgetItem_Type));
    XTableWidget_setItem(tw, 1, 0,
        XTableWidgetItem_create_utf8("alphaBeta", XTableWidgetItem_Type));
    XTableWidget_setItem(tw, 2, 0,
        XTableWidgetItem_create_utf8("betaAlpha", XTableWidgetItem_Type));
    return tw;
}

/** @brief findItems 正则/通配真命中 + 大小写 + 空 needle + 非法 pattern
 *         边界，逐值对齐本机 Qt 6.8 探针。 */
XQ_TEST(XSelectionRange, findItemsRegexWildcard)
{
    XTableWidget* tw = xqSr_makeFindTable();
    XTableWidgetItem* hits[16];
    int n;
    XQ_REQUIRE(tw != NULL);

    /* Qt 探针：RE("Al.*")=3（部分匹配，betaAlpha 亦命中）、+CS=2。 */
    n = XTableWidget_findItems(tw, "Al.*",
        XMatchFlag_MatchRegularExpression, hits, 16);
    XQ_EQ_I64(n, 3);
    XQ_CHECK(hits[0] == XTableWidget_item(tw, 0, 0)); /* 列优先。 */
    XQ_CHECK(hits[2] == XTableWidget_item(tw, 2, 0));

    n = XTableWidget_findItems(tw, "Al.*",
        XMatchFlag_MatchRegularExpression | XMatchFlag_MatchCaseSensitive,
        hits, 16);
    XQ_EQ_I64(n, 2);

    /* RE("^alpha")+CS=1（锚点语法生效）；RE("alpha") 缺省不敏感 =3。 */
    XQ_EQ_I64(XTableWidget_findItems(tw, "^alpha",
        XMatchFlag_MatchRegularExpression | XMatchFlag_MatchCaseSensitive,
        hits, 16), 1);
    XQ_EQ_I64(XTableWidget_findItems(tw, "alpha",
        XMatchFlag_MatchRegularExpression, hits, 16), 3);
    XQ_EQ_I64(XTableWidget_findItems(tw, "^Alpha$",
        XMatchFlag_MatchRegularExpression, hits, 16), 1);

    /* Qt 探针：WC("Al*")=2（完整锚定，不含 betaAlpha）、+CS=1；
     * WC("*a*")=3；WC("Alph?")=1（? 单字符通配）。 */
    XQ_EQ_I64(XTableWidget_findItems(tw, "Al*",
        XMatchFlag_MatchWildcard, hits, 16), 2);
    XQ_EQ_I64(XTableWidget_findItems(tw, "Al*",
        XMatchFlag_MatchWildcard | XMatchFlag_MatchCaseSensitive, hits, 16), 1);
    XQ_EQ_I64(XTableWidget_findItems(tw, "*a*",
        XMatchFlag_MatchWildcard, hits, 16), 3);
    XQ_EQ_I64(XTableWidget_findItems(tw, "Alph?",
        XMatchFlag_MatchWildcard, hits, 16), 1);
    XQ_EQ_I64(XTableWidget_findItems(tw, "Alpha",
        XMatchFlag_MatchWildcard | XMatchFlag_MatchCaseSensitive, hits, 16), 1);

    /* 边界：空 needle。RE 空 pattern 部分匹配所有串 ⇒ 6（含 3 空格）；
     * WC 空 pattern 全锚定 `\A(?:)\z` 只匹配空串 ⇒ 3（仅空格）。 */
    XQ_EQ_I64(XTableWidget_findItems(tw, "",
        XMatchFlag_MatchRegularExpression, hits, 16), 6);
    XQ_EQ_I64(XTableWidget_findItems(tw, "",
        XMatchFlag_MatchWildcard, hits, 16), 3);

    /* 边界：非法 pattern "[" ⇒ 0 条（XRegularExpression 编译无效，
     * 对齐 Qt「contains() called on an invalid QRegularExpression」）。 */
    XQ_EQ_I64(XTableWidget_findItems(tw, "[",
        XMatchFlag_MatchRegularExpression, hits, 16), 0);
    XQ_EQ_I64(XTableWidget_findItems(tw, "[",
        XMatchFlag_MatchRegularExpression | XMatchFlag_MatchCaseSensitive,
        hits, 16), 0);

    /* 既有非正则语义不回归（MatchContains/Exactly/FixedString/CS 位）。 */
    XQ_EQ_I64(XTableWidget_findItems(tw, "Alpha",
        XMatchFlag_MatchContains, hits, 16), 3);
    XQ_EQ_I64(XTableWidget_findItems(tw, "Alpha",
        XMatchFlag_MatchContains | XMatchFlag_MatchCaseSensitive, hits, 16), 2);
    XQ_EQ_I64(XTableWidget_findItems(tw, "Alpha",
        XMatchFlag_MatchExactly, hits, 16), 1);
    XQ_EQ_I64(XTableWidget_findItems(tw, "alpha",
        XMatchFlag_MatchFixedString, hits, 16), 1);
    XQ_EQ_I64(XTableWidget_findItems(tw, "",
        XMatchFlag_MatchContains, hits, 16), 6);

    XClassDelete((XClass*)tw);
}

/* ==================== ④ §B4 镜像/快照共存 + 单次发射 ==================== */

static int g_srSelHits; /**< selectionChanged 命中次数。 */

static void xqSr_selSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++g_srSelHits;
}

/** @brief 范围表（事实源）与 §B4 镜像位/快照（绘制快速通道）共存：
 *         模型 isSelected 与条目镜像同源；单次发射不破。 */
XQ_TEST(XSelectionRange, mirrorAndSnapshotCoexist)
{
    XTableWidget* tw = XTableWidget_create(NULL, 0);
    XItemSelectionModel* sm;
    XTableWidgetItem* sel[8];
    int r;
    int c;
    XQ_REQUIRE(tw != NULL);
    XTableWidget_setRowCount(tw, 4);
    XTableWidget_setColumnCount(tw, 4);
    /* §B4 的条目镜像位只承载**有条目**的格（空格无位）；填满 4×4 才能
     * 验证镜像与范围表同源（同 xqTwSel_make3x3 夹具口径）。 */
    for (r = 0; r < 4; ++r)
        for (c = 0; c < 4; ++c)
            XTableWidget_setItem(tw, r, c,
                XTableWidgetItem_create_utf8("x", XTableWidgetItem_Type));
    sm = XAbstractItemView_selectionModel(&tw->m_base.m_base);
    XQ_REQUIRE(sm != NULL);

    g_srSelHits = 0;
    XObject_connect((XObject*)sm,
        XSignal(XItemSelectionModel_selectionChanged_signal),
        (XObject*)sm, xqSr_selSlot, XConnectionType_Direct);

    /* 十字形序列：范围表切 2 条；§B4 镜像位仍按格回填（O(1) 绘制通道）。 */
    XQ_CHECK(XItemSelectionModel_selectSelection(
        sm, 0, 0, 3, 0, XItemSelectionModelSelectionFlag_Select));
    XQ_CHECK(XItemSelectionModel_selectSelection(
        sm, 0, 0, 0, 3, XItemSelectionModelSelectionFlag_Select));
    XQ_EQ_I64(XItemSelectionModel_selection(sm, NULL), 2);
    /* 事实源（范围表）判定。 */
    XQ_CHECK(XItemSelectionModel_isSelected(sm, 2, 0));
    XQ_CHECK(XItemSelectionModel_isSelected(sm, 0, 3));
    XQ_CHECK(!XItemSelectionModel_isSelected(sm, 2, 2));
    /* 派生镜像（条目位）与事实源同源：交点/两臂都选中，非臂格不选中。 */
    XQ_CHECK(XTableWidgetItem_isSelected(XTableWidget_item(tw, 2, 0)));
    XQ_CHECK(XTableWidgetItem_isSelected(XTableWidget_item(tw, 0, 3)));
    XQ_CHECK(XTableWidgetItem_isSelected(XTableWidget_item(tw, 0, 0)));
    XQ_CHECK(!XTableWidgetItem_isSelected(XTableWidget_item(tw, 2, 2)));
    /* selectedItems 走平铺镜像（与范围表同源）：列 0 四格 + 行 0 三格
     * （交点只算一次）= 7；§B4 的 R×C 位图快照同理。 */
    XQ_EQ_I64(XTableWidget_selectedItems(tw, sel, 8), 7);
    /* 两次 select ⇒ 两次 selectionChanged（单次发射口径不破）。 */
    XQ_EQ_I64(g_srSelHits, 2);

    XClassDelete((XClass*)tw);
}

/* ==================== ⑤ selectedRanges 直读范围表 ==================== */

/** @brief XTableWidget_selectedRanges 直读 selection()：十字形/洞形的
 *         切分与 Qt 探针逐例一致，且不做隐藏行列过滤。 */
XQ_TEST(XSelectionRange, selectedRangesDirectRead)
{
    XTableWidget* tw = XTableWidget_create(NULL, 0);
    XItemSelectionModel* sm;
    XTableWidgetSelectionRange rngs[8];
    int n;
    XQ_REQUIRE(tw != NULL);
    XTableWidget_setRowCount(tw, 5);
    XTableWidget_setColumnCount(tw, 5);
    sm = XAbstractItemView_selectionModel(&tw->m_base.m_base);
    XQ_REQUIRE(sm != NULL);

    /* 十字形（Qt 探针 L2-cross）：先整列 0 再整行 0 ⇒ 2 条。 */
    XItemSelectionModel_selectSelection(
        sm, 0, 0, 4, 0, XItemSelectionModelSelectionFlag_Select);
    XItemSelectionModel_selectSelection(
        sm, 0, 0, 0, 4, XItemSelectionModelSelectionFlag_Select);
    n = XTableWidget_selectedRanges(tw, rngs, 8);
    XQ_EQ_I64(n, 2);
    XQ_EQ_I64(XTableWidgetSelectionRange_topRow(&rngs[0]), 1);
    XQ_EQ_I64(XTableWidgetSelectionRange_leftColumn(&rngs[0]), 0);
    XQ_EQ_I64(XTableWidgetSelectionRange_bottomRow(&rngs[0]), 4);
    XQ_EQ_I64(XTableWidgetSelectionRange_rightColumn(&rngs[0]), 0);
    XQ_EQ_I64(XTableWidgetSelectionRange_topRow(&rngs[1]), 0);
    XQ_EQ_I64(XTableWidgetSelectionRange_rightColumn(&rngs[1]), 4);

    /* 隐藏一列后仍按逻辑行列返回（§B4 口径：不过滤 isIndexHidden）。 */
    XTableView_setColumnHidden(&tw->m_base, 2, true);
    XQ_EQ_I64(XTableWidget_selectedRanges(tw, NULL, 0), 2);

    /* 洞形（Qt 探针 H2-hole）：4 条，split 上/下/左/右。 */
    XItemSelectionModel_clearSelection(sm);
    XItemSelectionModel_selectSelection(
        sm, 0, 0, 2, 2, XItemSelectionModelSelectionFlag_Select);
    XItemSelectionModel_select(
        sm, 1, 1, XItemSelectionModelSelectionFlag_Deselect);
    n = XTableWidget_selectedRanges(tw, rngs, 8);
    XQ_EQ_I64(n, 4);
    XQ_EQ_I64(XTableWidgetSelectionRange_bottomRow(&rngs[0]), 0);
    XQ_EQ_I64(XTableWidgetSelectionRange_topRow(&rngs[1]), 2);

    /* 空选区 ⇒ 0 条。 */
    XItemSelectionModel_clearSelection(sm);
    XQ_EQ_I64(XTableWidget_selectedRanges(tw, NULL, 0), 0);

    XClassDelete((XClass*)tw);
}

/* ==================== ⑥ selection() 与 selectedIndexes 顺序同源 ==================== */

/** @brief Qt 探针 IDX：(1,1) 再整行 2 ⇒ (1,1)(2,0)(2,1)(2,2)；
 *         范围表顺序、平铺镜像顺序、selectedIndexes 输出三者一致。 */
XQ_TEST(XSelectionRange, selectionAndIndexesCoherent)
{
    XItemSelectionModel* sm = XItemSelectionModel_create();
    XAbstractItemModel* m;
    int rows[8];
    int cols[8];
    int n;
    XItemSelection view;
    XQ_REQUIRE(sm != NULL);
    m = xqSr_makeModel(sm, 4, 3);
    XQ_REQUIRE(m != NULL);

    XQ_CHECK(XItemSelectionModel_select(
        sm, 1, 1, XItemSelectionModelSelectionFlag_Select));
    XQ_CHECK(XItemSelectionModel_selectSelection(
        sm, 2, 0, 2, 2, XItemSelectionModelSelectionFlag_Select));

    /* 范围表：2 条（1×1 + 整行 2）。 */
    (void)XItemSelection_init(&view);
    n = XItemSelectionModel_selection(sm, &view);
    XQ_EQ_I64(n, 2);
    XQ_SR_RANGE(&view, 0, 1, 1, 1, 1);
    XQ_SR_RANGE(&view, 1, 2, 0, 2, 2);
    XItemSelection_deinit(&view);

    /* 平铺镜像：4 格，顺序与范围表展开一致（Qt 实测）。 */
    XQ_EQ_I64(XItemSelectionModel_selectedCount(sm), 4);
    n = XItemSelectionModel_selectedIndexes(sm, rows, cols, 8);
    XQ_EQ_I64(n, 4);
    XQ_EQ_I64(rows[0], 1); XQ_EQ_I64(cols[0], 1);
    XQ_EQ_I64(rows[1], 2); XQ_EQ_I64(cols[1], 0);
    XQ_EQ_I64(rows[2], 2); XQ_EQ_I64(cols[2], 1);
    XQ_EQ_I64(rows[3], 2); XQ_EQ_I64(cols[3], 2);
    XQ_CHECK(XItemSelectionModel_hasSelection(sm));
    XQ_CHECK(XItemSelectionModel_isRowSelected(sm, 2));
    XQ_CHECK(!XItemSelectionModel_isRowSelected(sm, 1));

    /* 注：XObject 派生类对齐 Qt `Q_DISABLE_COPY(QObject)`，XClassCopy 对
     * 选择模型是空操作（框架既有语义），故不在本用例测 copy/move；范围表
     * 值对象的 deinit/copy/move 配对由运行器零泄漏门逐用例覆盖。 */

    XClassDelete((XClass*)sm);
    XClassDelete((XClass*)m);
}

#endif /* XGUI_ON && XWIDGET_ON && XTABLEWIDGET_ON */
