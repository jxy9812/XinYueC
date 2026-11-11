/**
 * @file       XTableAlignTest.c
 * @brief      XTableView / XTableWidget 对齐专项测试实现。
 * @details    ① copy/move 深拷贝与泄漏；② 边界矩阵；③ 多组合参数化。
 *             所有断言以 `[TBL-FAIL]` 前缀输出。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XTableAlignTest.h"

#include "XTableWidget.h"
#include "XTableView.h"
#include "XAbstractItemView.h"
#include "XAbstractItemModel.h"
#include "XDataStream.h" /* 序列化健壮性用例需要完整定义。 */
#include "XByteArray.h"
#include "XMemory.h"
#include "XClass.h"
#include <stdio.h>
#include <string.h>

static int tbl_failures = 0;

static void tbl_expect(bool cond, const char* what)
{
    if (!cond) {
        fprintf(stderr, "[TBL-FAIL] %s\n", what ? what : "");
        ++tbl_failures;
    }
}

/** @brief 建一个 rows×cols 的空表（无模型依赖，直接走 XTableWidget）。 */
static XTableWidget* tbl_make(int rows, int cols)
{
    XTableWidget* tw = XTableWidget_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                              NULL, 0);
    if (!tw) return NULL;
    if (rows > 0) XTableWidget_setRowCount(tw, rows);
    if (cols > 0) XTableWidget_setColumnCount(tw, cols);
    return tw;
}

/* ==================== ① copy / move 深拷贝与泄漏 ==================== */

static void tbl_testCopyMove(void)
{
    XTableView* src;
    XTableView* dst;
    XTableView* moved;
    int i;

    /* --- XTableView copy：逐行行高表必须深拷贝 --- */
    src = XTableView_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL, 0);
    tbl_expect(src != NULL, "copymove: XTableView 创建");
    if (!src) return;
    /* 造 6 行逐行行高（覆写值 40/41/42...）。 */
    for (i = 0; i < 6; ++i)
        XTableView_setRowHeight(src, i, 40 + i);
    XTableView_setColumnWidth(src, 0, 111);
    XTableView_setColumnWidth(src, 1, 222);
    XTableView_setRowHidden(src, 1, true);
    XTableView_setColumnHidden(src, 0, true);
    XTableView_setSpan(src, 0, 0, 2, 2);

    dst = XTableView_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL, 0);
    tbl_expect(dst != NULL, "copymove: copy 目标创建");
    if (dst) {
        XClassCopy(dst, src);
        /* 逐行行高必须随拷贝保留（此前 copy 漏接该表）。隐藏行按 Qt
         * rowHeight=verticalHeader->sectionSize 返回 0（gap 168）。 */
        for (i = 0; i < 6; ++i)
            tbl_expect(XTableView_rowHeight(dst, i) ==
                           ((i == 1) ? 0 : 40 + i),
                       "copymove: copy 保留逐行行高（隐藏行 0）");
        tbl_expect(XTableView_columnWidth(dst, 0) == 111 &&
                   XTableView_columnWidth(dst, 1) == 222,
                   "copymove: copy 保留列宽");
        tbl_expect(XTableView_isRowHidden(dst, 1) &&
                   XTableView_isColumnHidden(dst, 0),
                   "copymove: copy 保留隐藏状态");
        tbl_expect(XTableView_rowSpan(dst, 0, 0) == 2 &&
                   XTableView_columnSpan(dst, 0, 0) == 2,
                   "copymove: copy 保留合并区间");
        /* 源对象不应被 copy 改动。 */
        tbl_expect(XTableView_rowHeight(src, 3) == 43,
                   "copymove: copy 不破坏源对象");
        XClassDelete((XClass*)dst);
    }

    /* --- XTableView move：资源转移且源归零 --- */
    moved = XTableView_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL, 0);
    tbl_expect(moved != NULL, "copymove: move 目标创建");
    if (moved) {
        /* 目标先塞一份自身数据：move 必须释放它（此前直接覆盖 → 泄漏）。 */
        XTableView_setRowHeight(moved, 0, 77);
        XTableView_setColumnWidth(moved, 0, 333);
        XTableView_setRowHidden(moved, 0, true);
        XTableView_setSpan(moved, 1, 1, 3, 3);

        XClassMove(moved, src);
        for (i = 0; i < 6; ++i)
            tbl_expect(XTableView_rowHeight(moved, i) ==
                           ((i == 1) ? 0 : 40 + i),
                       "copymove: move 转移逐行行高（隐藏行 0）");
        tbl_expect(XTableView_columnWidth(moved, 0) == 111,
                   "copymove: move 转移列宽（目标旧值 333 已释放）");
        tbl_expect(XTableView_rowSpan(moved, 0, 0) == 2,
                   "copymove: move 转移合并区间");
        /* 源：逐行行高应回落缺省（表已转出）。 */
        tbl_expect(XTableView_rowHeight(src, 0) == 24,
                   "copymove: move 后源逐行行高回落缺省");
        tbl_expect(XTableView_columnWidth(src, 0) == 80,
                   "copymove: move 后源列宽回落缺省");
        XClassDelete((XClass*)moved);
    }
    XClassDelete((XClass*)src);

    /* --- XTableWidget copy/move：条目对象深拷贝 --- */
    {
        XTableWidget* w = tbl_make(2, 2);
        XTableWidget* wc = tbl_make(0, 0);
        XTableWidgetItem* it;
        tbl_expect(w != NULL && wc != NULL, "copymove: table copy 载体创建");
        if (w && wc) {
            it = XTableWidgetItem_create_utf8("A", XTableWidgetItem_Type);
            if (it) XTableWidget_setItem(w, 0, 0, it);
            XClassCopy(wc, w);
            {
                const XTableWidgetItem* got = XTableWidget_item(wc, 0, 0);
                tbl_expect(got != NULL &&
                           strcmp(XTableWidgetItem_text_toUtf8(got), "A") == 0,
                           "copymove: table copy 保留条目文本");
                /* 深拷贝：改副本不影响源。 */
                if (got) XTableWidgetItem_setText_utf8(
                    (XTableWidgetItem*)got, "B");
                tbl_expect(strcmp(XTableWidgetItem_text_toUtf8(
                               XTableWidget_item(w, 0, 0)), "A") == 0,
                           "copymove: table copy 为深拷贝");
            }
            XClassDelete((XClass*)wc);
        }
        if (w) XClassDelete((XClass*)w);
    }
}

/* ==================== ② 边界矩阵 ==================== */

static void tbl_testBoundaries(void)
{
    /* --- 空表：0 行 0 列下所有查询安全 --- */
    {
        XTableWidget* tw = tbl_make(0, 0);
        tbl_expect(tw != NULL, "bound: 空表创建");
        if (tw) {
            tbl_expect(XTableWidget_rowCount(tw) == 0 &&
                       XTableWidget_columnCount(tw) == 0,
                       "bound: 空表行列数为 0");
            tbl_expect(XTableWidget_item(tw, 0, 0) == NULL,
                       "bound: 空表 item 返回 NULL");
            tbl_expect(XTableWidget_itemAt(tw, 5, 5) == NULL,
                       "bound: 空表 itemAt 返回 NULL");
            tbl_expect(XTableWidget_currentRow(tw) == -1 &&
                       XTableWidget_currentColumn(tw) == -1,
                       "bound: 空表当前格为 -1");
            tbl_expect(XTableWidget_currentItem(tw) == NULL,
                       "bound: 空表 currentItem 为 NULL");
            tbl_expect(XTableWidget_indexFromItem(tw, NULL, NULL) == -1,
                       "bound: 空表 indexFromItem(NULL) 为 -1");
            /* 空表上的删除/插入不崩溃。 */
            XTableWidget_removeRow(tw, 0);
            XTableWidget_removeColumn(tw, 0);
            XTableWidget_setCurrentCell(tw, 0, 0);
            XTableWidget_clear(tw);
            tbl_expect(XTableWidget_rowCount(tw) == 0,
                       "bound: 空表 clear 后仍为 0 行");
            XClassDelete((XClass*)tw);
        }
    }

    /* --- 删光：逐行删空后状态一致 --- */
    {
        XTableWidget* tw = tbl_make(3, 2);
        XTableWidgetItem* it;
        tbl_expect(tw != NULL, "bound: 删光载体创建");
        if (tw) {
            it = XTableWidgetItem_create_utf8("x", XTableWidgetItem_Type);
            if (it) XTableWidget_setItem(tw, 2, 1, it);
            while (XTableWidget_rowCount(tw) > 0)
                XTableWidget_removeRow(tw, 0);
            tbl_expect(XTableWidget_rowCount(tw) == 0,
                       "bound: 逐行删空后行数为 0");
            tbl_expect(XTableWidget_item(tw, 0, 0) == NULL,
                       "bound: 删空后取条目为 NULL");
            /* 删空后再加行应正常工作。 */
            XTableWidget_setRowCount(tw, 2);
            tbl_expect(XTableWidget_rowCount(tw) == 2,
                       "bound: 删空后可重新设行数");
            while (XTableWidget_columnCount(tw) > 0)
                XTableWidget_removeColumn(tw, 0);
            tbl_expect(XTableWidget_columnCount(tw) == 0,
                       "bound: 逐列删空后列数为 0");
            XClassDelete((XClass*)tw);
        }
    }

    /* --- 越界：所有入口的越界行为 --- */
    {
        XTableWidget* tw = tbl_make(2, 2);
        tbl_expect(tw != NULL, "bound: 越界载体创建");
        if (tw) {
            tbl_expect(XTableWidget_item(tw, -1, 0) == NULL &&
                       XTableWidget_item(tw, 0, -1) == NULL &&
                       XTableWidget_item(tw, 2, 0) == NULL &&
                       XTableWidget_item(tw, 0, 2) == NULL,
                       "bound: item 四向越界均为 NULL");
            /* setItem 越界：不崩溃且不入表。
             * 对齐 Qt 契约：越界时条目不入表且**表格不接管其所有权**
             * （Qt 同：只给 item->view 赋值，不存入 tableItems），
             * 因此调用方必须自行回收——否则每次越界 setItem
             * 都漏一个条目。测试此处显式释放以保持零漏洞。 */
            {
                XTableWidgetItem* it =
                    XTableWidgetItem_create_utf8("o", XTableWidgetItem_Type);
                if (it) {
                    XTableWidget_setItem(tw, 9, 9, it);
                    XClassDelete((XClass*)it); /* 越界未入表：所有权仍在本处 */
                }
                tbl_expect(XTableWidget_item(tw, 9, 9) == NULL,
                           "bound: setItem 越界不入表");
            }
            /* 越界取条目的行/列反查为 -1。 */
            tbl_expect(XTableWidget_indexFromItem(tw, NULL, NULL) == -1,
                       "bound: indexFromItem(NULL) 为 -1");
            /* 越界 setRangeSelected 整体忽略。 */
            {
                XTableWidgetSelectionRange r =
                    XTableWidgetSelectionRange_create(0, 0, 5, 5);
                XTableWidget_setRangeSelected(tw, &r, true);
                tbl_expect(XTableWidget_selectedItems(tw, NULL, 0) == 0,
                           "bound: 越界 setRangeSelected 整体忽略");
            }
            /* 越界 setCurrentCell 收敛为 -1。 */
            XTableWidget_setCurrentCell(tw, 9, 9);
            tbl_expect(XTableWidget_currentRow(tw) == -1,
                       "bound: 越界 setCurrentCell 收敛为 -1");
            XClassDelete((XClass*)tw);
        }
    }

    /* --- XTableView 无模型时的几何查询 --- */
    {
        XTableView* tv = XTableView_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                              NULL, 0);
        tbl_expect(tv != NULL, "bound: 无模型视图创建");
        if (tv) {
            int r = -99;
            int c = -99;
            tbl_expect(XTableView_rowAt(tv, 10) == -1,
                       "bound: 无模型 rowAt 为 -1");
            tbl_expect(XTableView_columnAt(tv, 10) == -1,
                       "bound: 无模型 columnAt 为 -1");
            tbl_expect(!XTableView_indexAt(tv, 10, 10, &r, &c) &&
                       r == -1 && c == -1,
                       "bound: 无模型 indexAt 为 false 且输出 -1");
            tbl_expect(XTableView_rowHeight(tv, 0) == 24,
                       "bound: 无模型 rowHeight 回落缺省 24");
            tbl_expect(XTableView_rowSpan(tv, 0, 0) == 1,
                       "bound: 无模型 rowSpan 缺省 1");
            XClassDelete((XClass*)tv);
        }
    }
}

/* ==================== ③ 多组合参数化 ==================== */

static void tbl_testSpanMatrix(void)
{
    /* 跨度组合矩阵：覆盖 0 / 1 / 2 / 超大 / 负值。 */
    static const struct { int rs; int cs; int wantR; int wantC; const char* d; }
    cases[] = {
        { 1,  1,  1,  1,  "span: 1x1 不建立区间" },
        { 2,  2,  2,  2,  "span: 2x2 建立" },
        { 3,  5,  3,  5,  "span: 3x5 建立" },
        { 100, 100, 100, 100, "span: 超大跨度可建立" },
        { 0,  2,  1,  1,  "span: rowSpan=0 不覆盖（保持缺省 1）" },
        { -1, 2,  1,  1,  "span: 负跨度被拒绝" },
    };
    size_t i;
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        XTableView* tv = XTableView_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                              NULL, 0);
        if (!tv) { tbl_expect(false, "span: 视图创建"); continue; }
        XTableView_setSpan(tv, 0, 0, cases[i].rs, cases[i].cs);
        tbl_expect(XTableView_rowSpan(tv, 0, 0) == cases[i].wantR &&
                   XTableView_columnSpan(tv, 0, 0) == cases[i].wantC,
                   cases[i].d);
        XClassDelete((XClass*)tv);
    }

    /* 重叠区间：后设的优先命中（集合语义由查找顺序决定）。 */
    {
        XTableView* tv = XTableView_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                              NULL, 0);
        if (tv) {
            XTableView_setSpan(tv, 0, 0, 2, 2);
            XTableView_setSpan(tv, 1, 1, 3, 3);
            /* (1,1) 同时属于两个区间：按实现返回先加入的那个。 */
            tbl_expect(XTableView_rowSpan(tv, 1, 1) == 2,
                       "span: 重叠区间命中先进先出");
            XTableView_clearSpans(tv);
            tbl_expect(XTableView_rowSpan(tv, 1, 1) == 1,
                       "span: clearSpans 后全部复位");
            XClassDelete((XClass*)tv);
        }
    }
}

static void tbl_testHideMatrix(void)
{
    int i;
    XTableView* tv = XTableView_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                          NULL, 0);
    XAbstractItemModel* m = XAbstractItemModel_create();
    tbl_expect(tv != NULL && m != NULL, "hide: 载体创建");
    if (!tv || !m) {
        if (tv) XClassDelete((XClass*)tv);
        if (m) XClassDelete((XClass*)m);
        return;
    }
    XAbstractItemModel_setDimension(m, 5, 3);
    XAbstractItemView_setModel(&tv->m_base, m);

    /* 全隐藏：rowAt/columnAt 应全部落空。 */
    for (i = 0; i < 5; ++i) XTableView_setRowHidden(tv, i, true);
    tbl_expect(XTableView_rowAt(tv, 50) == -1,
               "hide: 全行隐藏后 rowAt 为 -1");
    for (i = 0; i < 5; ++i) XTableView_setRowHidden(tv, i, false);
    tbl_expect(XTableView_rowAt(tv, 50) >= 0,
               "hide: 全行恢复后 rowAt 命中");

    /* 中间行隐藏：其后的行上移一行高。 */
    XTableView_setRowHidden(tv, 1, true);
    {
        int y0 = XTableView_rowViewportPosition(tv, 0);
        int y2 = XTableView_rowViewportPosition(tv, 2);
        tbl_expect(y2 - y0 == 24,
                   "hide: 隐藏行占高 0（行 2 紧跟行 0）");
    }
    XTableView_setRowHidden(tv, 1, false);

    /* 列隐藏同理。 */
    XTableView_setColumnHidden(tv, 0, true);
    {
        int x0 = XTableView_columnViewportPosition(tv, 0);
        int x1 = XTableView_columnViewportPosition(tv, 1);
        tbl_expect(x0 == x1,
                   "hide: 隐藏列与下一列同起点");
    }
    XTableView_setColumnHidden(tv, 0, false);

    /* 越界索引：隐藏状态查询安全。 */
    tbl_expect(!XTableView_isRowHidden(tv, -1) &&
               !XTableView_isRowHidden(tv, 999) &&
               !XTableView_isColumnHidden(tv, -1) &&
               !XTableView_isColumnHidden(tv, 999),
               "hide: 越界查询返回 false");

    XClassDelete((XClass*)tv);
    XClassDelete((XClass*)m);
}

static void tbl_testSortingMatrix(void)
{
    /* 排序：升/降 / 含空值 / 重复值。 */
    XTableWidget* tw = tbl_make(4, 1);
    tbl_expect(tw != NULL, "sort: 载体创建");
    if (!tw) return;
    XTableWidget_setItem(tw, 0, 0,
        XTableWidgetItem_create_utf8("c", XTableWidgetItem_Type));
    XTableWidget_setItem(tw, 1, 0,
        XTableWidgetItem_create_utf8("a", XTableWidgetItem_Type));
    /* 行 2 留空（无条目）。 */
    XTableWidget_setItem(tw, 3, 0,
        XTableWidgetItem_create_utf8("b", XTableWidgetItem_Type));

    XTableWidget_sortItems(tw, 0, 0); /* 升序 */
    {
        const char* t0 = XTableWidgetItem_text_toUtf8(
            XTableWidget_item(tw, 0, 0));
        const char* t1 = XTableWidgetItem_text_toUtf8(
            XTableWidget_item(tw, 1, 0));
        tbl_expect(strcmp(t0, "a") == 0 && strcmp(t1, "b") == 0,
                   "sort: 升序 a,b 在前");
        /* 空值沉底（对齐 Qt 的 unsortable 组排在最后）。 */
        tbl_expect(XTableWidget_item(tw, 3, 0) == NULL,
                   "sort: 空值沉底");
    }

    XTableWidget_sortItems(tw, 0, 1); /* 降序 */
    tbl_expect(strcmp(XTableWidgetItem_text_toUtf8(
                   XTableWidget_item(tw, 0, 0)), "c") == 0,
               "sort: 降序 c 在前");

    /* 越界列：忽略。 */
    XTableWidget_sortItems(tw, 99, 0);
    tbl_expect(true, "sort: 越界列不崩溃");

    XClassDelete((XClass*)tw);

    /* 重复值稳定排序。 */
    {
        XTableWidget* d = tbl_make(3, 1);
        if (d) {
            XTableWidget_setItem(d, 0, 0,
                XTableWidgetItem_create_utf8("k", XTableWidgetItem_Type));
            XTableWidget_setItem(d, 1, 0,
                XTableWidgetItem_create_utf8("k", XTableWidgetItem_Type));
            XTableWidget_setItem(d, 2, 0,
                XTableWidgetItem_create_utf8("k", XTableWidgetItem_Type));
            XTableWidget_sortItems(d, 0, 0);
            tbl_expect(XTableWidget_rowCount(d) == 3,
                       "sort: 全等值排序不丢行");
            XClassDelete((XClass*)d);
        }
    }
}

static void tbl_testElideMatrix(void)
{
    /* 省略模式矩阵：四种模式在「宽/窄」两档下的行为。 */
    static const int modes[] = {
        XAbstractItemViewTextElideMode_ElideLeft,
        XAbstractItemViewTextElideMode_ElideRight,
        XAbstractItemViewTextElideMode_ElideMiddle,
        XAbstractItemViewTextElideMode_ElideNone
    };
    XTableWidget* tw = tbl_make(1, 1);
    XFont font;
    int i;
    tbl_expect(tw != NULL, "elide: 载体创建");
    if (!tw) return;
    XFont_init(&font);

    for (i = 0; i < 4; ++i) {
        char buf[128];
        const char* out;
        XAbstractItemView_setTextElideMode(&tw->m_base.m_base, modes[i]);
        tbl_expect(XAbstractItemView_textElideMode(&tw->m_base.m_base) ==
                       modes[i],
                   "elide: 模式往返");
        /* 极宽：必须原样返回入参（不写 buf）。 */
        out = XAbstractItemView_elidedText(&tw->m_base.m_base, buf,
                                           (int)sizeof(buf),
                                           "abcdefgh", 10000, &font);
        tbl_expect(out != NULL && strcmp(out, "abcdefgh") == 0,
                   "elide: 宽度充足时各模式均原样返回");
        /* 窄：ElideNone 应无省略号，其余应含省略号。 */
        out = XAbstractItemView_elidedText(&tw->m_base.m_base, buf,
                                           (int)sizeof(buf),
                                           "abcdefghijklmnop", 60, &font);
        if (modes[i] == XAbstractItemViewTextElideMode_ElideNone) {
            tbl_expect(out != NULL && strstr(out, "\xe2\x80\xa6") == NULL,
                       "elide: ElideNone 无省略号");
        } else {
            tbl_expect(out != NULL && strstr(out, "\xe2\x80\xa6") != NULL,
                       "elide: 非 None 模式含省略号");
        }
    }

    /* 越界参数安全。 */
    tbl_expect(XAbstractItemView_elidedText(&tw->m_base.m_base, NULL, 0,
                                            "x", 10, &font) != NULL,
               "elide: buf 为 NULL 安全");
    tbl_expect(XAbstractItemView_elidedText(NULL, NULL, 0, NULL, 10,
                                            &font) != NULL,
               "elide: 全 NULL 安全");
    XClassDeinit((XClass*)&font);
    XClassDelete((XClass*)tw);
}

static void tbl_testScrollStepMatrix(void)
{
    /* 滚动步进：两种模式 × 两方向。 */
    XAbstractItemView* v = XAbstractItemView_create_ex(
        XCLASS_DEFAULT_MEMORY_TYPE, NULL, 0);
    XAbstractItemModel* m = XAbstractItemModel_create();
    XScrollBar* vb;
    XScrollBar* hb;
    tbl_expect(v != NULL && m != NULL, "step: 载体创建");
    if (!v) { if (m) XClassDelete((XClass*)m); return; }
    if (m) {
        XAbstractItemModel_setDimension(m, 2, 2);
        XAbstractItemView_setModel(v, m);
    }
    vb = XAbstractScrollArea_verticalScrollBar((XAbstractScrollArea*)v);
    hb = XAbstractScrollArea_horizontalScrollBar((XAbstractScrollArea*)v);
    if (vb && hb) {
        XAbstractItemView_setVerticalScrollMode(
            v, XAbstractItemViewScrollMode_ScrollPerPixel);
        XAbstractItemView_setHorizontalScrollMode(
            v, XAbstractItemViewScrollMode_ScrollPerPixel);
        tbl_expect(XAbstractSlider_singleStep((XAbstractSlider*)vb) == 1 &&
                   XAbstractSlider_singleStep((XAbstractSlider*)hb) == 1,
                   "step: ScrollPerPixel 两向 singleStep=1");

        XAbstractItemView_setVerticalScrollMode(
            v, XAbstractItemViewScrollMode_ScrollPerItem);
        XAbstractItemView_setHorizontalScrollMode(
            v, XAbstractItemViewScrollMode_ScrollPerItem);
        /* 基类网格 80x24：垂直=行高、水平=列宽。 */
        tbl_expect(XAbstractSlider_singleStep((XAbstractSlider*)vb) == 24,
                   "step: ScrollPerItem 垂直=行高 24");
        tbl_expect(XAbstractSlider_singleStep((XAbstractSlider*)hb) == 80,
                   "step: ScrollPerItem 水平=列宽 80");
        /* 显式再同步一次（幂等）。 */
        XAbstractItemView_syncScrollSteps(v);
        tbl_expect(XAbstractSlider_singleStep((XAbstractSlider*)vb) == 24,
                   "step: syncScrollSteps 幂等");
    }
    XClassDelete((XClass*)v);
    if (m) XClassDelete((XClass*)m);
}

/* ==================== 序列化健壮性 ==================== */

static void tbl_testSerializeRobustness(void)
{
    /* 截断数据 → read 失败且不改动目标条目。 */
    {
        XTableWidgetItem* src =
            XTableWidgetItem_create_utf8("robust", XTableWidgetItem_Type);
        XByteArray* bytes = XByteArray_create();
        if (src && bytes) {
            XDataStream ws;
            XDataStream_init_array(&ws, bytes);
            XTableWidgetItem_write(src, &ws);
            /* 只保留前 5 字节（远不足以容纳 role 表）。 */
            if (XByteArray_size_base(bytes) > 5)
                XVector_resize_base((XVector*)bytes, 5);
            {
                XTableWidgetItem* dst = XTableWidgetItem_create_utf8(
                    "keep", XTableWidgetItem_Type);
                XDataStream rs;
                if (dst) {
                    XDataStream_init_arrayReadOnly(&rs, bytes);
                    tbl_expect(!XTableWidgetItem_read(dst, &rs),
                               "ser: 截断数据 read 失败");
                    tbl_expect(strcmp(XTableWidgetItem_text_toUtf8(dst),
                                      "keep") == 0,
                               "ser: 读取失败时目标条目保持不变");
                    XClassDeinit((XClass*)&rs);
                    XClassDelete((XClass*)dst);
                }
            }
            XClassDeinit((XClass*)&ws);
        }
        if (bytes) XClassDelete((XClass*)bytes);
        if (src) XClassDelete((XClass*)src);
    }

    /* roleCount 超限 → SizeLimitExceeded。 */
    {
        XByteArray* bytes = XByteArray_create();
        if (bytes) {
            XDataStream ws;
            XDataStream rs;
            XTableWidgetItem* dst = XTableWidgetItem_create();
            XDataStream_init_array(&ws, bytes);
            XDataStream_writeUInt32(&ws, 99999u); /* 超 4096 上限 */
            if (dst) {
                XDataStream_init_arrayReadOnly(&rs, bytes);
                tbl_expect(!XTableWidgetItem_read(dst, &rs) &&
                           XDataStream_status(&rs) ==
                               XDataStream_Status_SizeLimitExceeded,
                           "ser: roleCount 超限报 SizeLimitExceeded");
                XClassDeinit((XClass*)&rs);
                XClassDelete((XClass*)dst);
            }
            XClassDeinit((XClass*)&ws);
            XClassDelete((XClass*)bytes);
        }
    }

    /* 空流读取 → 失败不崩溃。 */
    {
        XByteArray* bytes = XByteArray_create();
        XTableWidgetItem* dst = XTableWidgetItem_create();
        if (bytes && dst) {
            XDataStream rs;
            XDataStream_init_arrayReadOnly(&rs, bytes);
            tbl_expect(!XTableWidgetItem_read(dst, &rs),
                       "ser: 空流 read 失败");
            XClassDeinit((XClass*)&rs);
        }
        if (dst) XClassDelete((XClass*)dst);
        if (bytes) XClassDelete((XClass*)bytes);
    }

    /* NULL 参数安全。 */
    tbl_expect(!XTableWidgetItem_write(NULL, NULL), "ser: write(NULL) 为 false");
    tbl_expect(!XTableWidgetItem_read(NULL, NULL), "ser: read(NULL) 为 false");
}

/* ==================== 入口 ==================== */

int XTableAlignTest_runAll(void)
{
    tbl_failures = 0;
    tbl_testCopyMove();
    tbl_testBoundaries();
    tbl_testSpanMatrix();
    tbl_testHideMatrix();
    tbl_testSortingMatrix();
    tbl_testElideMatrix();
    tbl_testScrollStepMatrix();
    tbl_testSerializeRobustness();
    return tbl_failures;
}
