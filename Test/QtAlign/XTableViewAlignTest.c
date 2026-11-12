/**
 * @file       XTableViewAlignTest.c
 * @brief      XTableView × Qt 6.8 QTableView 对齐契约测试。
 * @details    对齐依据：
 *             - 头文件 /opt/Qt/6.8.3/gcc_64/include/QtWidgets/qtableview.h
 *               （setRowHeight/rowHeight、setColumnWidth/columnWidth、
 *               setRowHidden/setColumnHidden、visualRect、rowAt/columnAt、
 *               rowViewportPosition/columnViewportPosition、setSpan、
 *               showGrid/gridStyle、sortingEnabled、cornerButtonEnabled、
 *               wordWrap）；
 *             - 本机 Qt 6.8.0 实测（QT_QPA_PLATFORM=offscreen）：
 *               ① 缺省 showGrid=true、gridStyle=1(SolidLine)、
 *                 sortingEnabled=false、cornerButtonEnabled=true、
 *                 wordWrap=**true**；② 构造即建 horizontalHeader/
 *                 verticalHeader 两个 QHeaderView 子控件，且二者
 *                 sectionsClickable/highlightSections 均为 true
 *                 （qtableview.cpp:577-589 显式置真，与独立表头缺省
 *                 false 相区分）；
 *               ③ setColumnHidden 转发 horizontalHeader->setSectionHidden
 *                 （qtableview.cpp:2705-2711），setRowHidden 转发
 *                 verticalHeader（qtableview.cpp:2680-2686）；
 *               ④ columnWidth = horizontalHeader->sectionSize
 *                 （qtableview.cpp:2658-2662），隐藏段返回 0；
 *                 setColumnWidth(0,10) ⇒ columnWidth 40（min 钳制）、
 *                 setRowHeight(0,10) ⇒ rowHeight 21；
 *               ⑤ 隐藏行/列不占视觉几何（rowAt/columnAt 跳过），
 *                 rowHeight(隐藏行)==0；
 *               ⑥ 表头与视图由 4 条信号回接（sectionResized/
 *                 sectionMoved/sectionCountChanged/geometriesChanged）；
 *               ⑦ setHorizontalHeader 删除旧子表头并 setParent(this)
 *                 （qtableview.cpp:1366-1399）。
 *
 *             覆盖：缺省矩阵、表头子控件挂接与列宽/行高转发、隐藏行/列的
 *             表头联动与几何（visualRect/rowAt/columnAt/indexAt 同源）、
 *             表头信号回接、表头所有权、合并区间吸附、父子所有权。
 * @note       C99；视图/模型/宿主全部释放，漏删即运行器判 `[LEAK]`。
 * @note       xgui_demo_apitest_views.c:2842 反向锁着 wordWrap=false 与
 *             :3507/3509/3614-3617 的旧段尺寸/0 尺寸/越界段口径，均属
 *             测试迁移台账（本轮已按 Qt 修实现）。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

#if XGUI_ON && XWIDGET_ON && XTABLEWIDGET_ON

#include "XTableView.h"
#include "XHeaderView.h"
#include "XAbstractItemView.h"
#include "XAbstractItemModel.h"

/* ==================== 缺省值矩阵 ==================== */

/** @brief 缺省属性（Qt 6.8.0 实测逐项；差异项注明登记状态）。 */
XQ_TEST(XTableView, defaults)
{
    XTableView* tv = XTableView_create(NULL, 0);
    XQ_REQUIRE(tv != NULL);

    XQ_CHECK(XTableView_showGrid(tv));                      /* Qt 实测 1 */
    XQ_EQ_I64(XTableView_gridStyle(tv), XTABLEVIEW_GRID_SOLID); /* SolidLine */
    XQ_CHECK(!XTableView_isSortingEnabled(tv));             /* Qt 实测 0 */
    XQ_CHECK(XTableView_isCornerButtonEnabled(tv));         /* Qt 实测 1 */
    /* Qt QTableView 缺省 wordWrap=true（qtableview_p.h:115 wrapItemText=true；
     * 本机实测 true），gap 152 已修。 */
    XQ_CHECK(XTableView_wordWrap(tv));
    XQ_EQ_I64(XTableView_rowHeight(tv, 0), 24);   /* XTV_DEFAULT_ROW_HEIGHT */
    XQ_EQ_I64(XTableView_columnWidth(tv, 0), 80); /* XTV_DEFAULT_COL_WIDTH */

    /* 对标 Qt：构造即建双表头子控件（QTableViewPrivate::init 建
     * horizontalHeader/verticalHeader，qtableview.cpp:577-589）。 */
    {
        XHeaderView* hh = XTableView_horizontalHeader(tv);
        XHeaderView* vh = XTableView_verticalHeader(tv);
        XQ_CHECK_PTR(hh);
        XQ_CHECK_PTR(vh);
        XQ_EQ_I64(XHeaderView_orientation(hh), 0);
        XQ_EQ_I64(XHeaderView_orientation(vh), 1);
        /* 表格自建表头 clickable/highlight 缺省 true（Qt 实测；独立表头
         * 为 false，见 XHeaderViewAligntest.defaults）。 */
        XQ_CHECK(XHeaderView_sectionsClickable(hh));
        XQ_CHECK(XHeaderView_sectionsClickable(vh));
        XQ_CHECK(XHeaderView_highlightSections(hh));
        XQ_CHECK(XHeaderView_highlightSections(vh));
        /* 表头是视图的子控件（Qt 造 QHeaderView(orientation, this)）。 */
        XQ_CHECK(XWidget_parentWidget((XWidget*)hh) == (XWidget*)tv);
        XQ_CHECK(XWidget_parentWidget((XWidget*)vh) == (XWidget*)tv);
    }

    XClassDelete((XClass*)tv);
}

/* ==================== 列宽/行高与表头转发（集成关系） ==================== */

/** @brief setColumnWidth/setRowHeight 转发表头段；隐藏列/行同步段状态。 */
XQ_TEST(XTableView, headerForwarding)
{
    XTableView* tv = XTableView_create(NULL, 0);
    XAbstractItemModel* model = XAbstractItemModel_create();
    XHeaderView* hh;
    XHeaderView* vh;
    XQ_REQUIRE(tv != NULL && model != NULL);
    /* 视口位置查询以模型列数为上界（同 Qt header 段数由模型驱动）。 */
    XAbstractItemModel_setDimension(model, 2, 3);
    XAbstractItemView_setModel(&tv->m_base, model);
    hh = XTableView_horizontalHeader(tv);
    vh = XTableView_verticalHeader(tv);
    XQ_REQUIRE(hh != NULL && vh != NULL);

    /* 列宽写入即扩段（Qt：列由模型驱动段数；本库 setColumnWidth 是列
     * 存在性写入点）。新段用 defaultSectionSize：水平缺省 100。 */
    XTableView_setColumnWidth(tv, 2, 120);
    XQ_EQ_I64(XHeaderView_count(hh), 3);
    XQ_EQ_I64(XTableView_columnWidth(tv, 2), 120);
    XQ_EQ_I64(XTableView_columnWidth(tv, 0), 100);

    /* 行高逐行覆写（对齐 setRowHeight：只影响该行）。 */
    XTableView_setRowHeight(tv, 1, 30);
    XQ_EQ_I64(XHeaderView_count(vh), 2);
    XQ_EQ_I64(XHeaderView_sectionSize(vh, 1), 30);
    XQ_EQ_I64(XTableView_rowHeight(tv, 1), 30);
    XQ_EQ_I64(XTableView_rowHeight(tv, 0), 24);

    /* 表头信号回接（gap 167）：直接调 XHeaderView_resizeSection 后视图
     * 数组/列宽查询/内容尺寸跟随（Qt qtableview.cpp:1385-1395）。 */
    XHeaderView_resizeSection(hh, 1, 70);
    XQ_EQ_I64(XTableView_columnWidth(tv, 1), 70);
    XQ_EQ_I64(tv->m_colWidths[1], 70);
    /* 越界 0 尺寸段：Qt resizeSection(0) 原样存储（gap 155）。 */
    XHeaderView_resizeSection(hh, 0, 0);
    XQ_EQ_I64(XHeaderView_sectionSize(hh, 0), 0);
    XQ_EQ_I64(tv->m_colWidths[0], 0);
    XHeaderView_resizeSection(hh, 0, 100);

    /* 隐藏列：视图状态表与表头段同步（qtableview.cpp:2705-2711），
     * 列宽按隐藏段计 0、可见列视口位置不计隐藏列宽。 */
    XTableView_hideColumn(tv, 1);
    XQ_CHECK(XTableView_isColumnHidden(tv, 1));
    XQ_CHECK(XHeaderView_isSectionHidden(hh, 1));
    XQ_EQ_I64(XTableView_columnWidth(tv, 1), 0);
    /* 列 0 = 100，列 1 隐藏占 0 ⇒ 列 2 起点 100。 */
    XQ_EQ_I64(XTableView_columnViewportPosition(tv, 2), 100);
    {
        /* 隐藏列不可见：视觉矩形为空（对标 Qt 对隐藏条目返回空矩形）。 */
        XRect empty = XTableView_visualRect(tv, 0, 1);
        XQ_CHECK(empty.width == 0 && empty.height == 0);
    }
    XTableView_showColumn(tv, 1);
    XQ_CHECK(!XHeaderView_isSectionHidden(hh, 1));
    XQ_EQ_I64(XTableView_columnWidth(tv, 1), 70); /* 隐藏期尺寸保留 */

    /* 隐藏行：verticalHeader 段同步，隐藏行不占高且 rowHeight 为 0
     * （gap 168；Qt 实测 setRowHidden 后 rowHeight==0）。 */
    XTableView_hideRow(tv, 0);
    XQ_CHECK(XTableView_isRowHidden(tv, 0));
    XQ_CHECK(XHeaderView_isSectionHidden(vh, 0));
    XQ_EQ_I64(XHeaderView_sectionSize(vh, 0), 0);
    XQ_EQ_I64(XTableView_rowHeight(tv, 0), 0);

    XClassDelete((XClass*)tv);
    XClassDelete((XClass*)model);
}

/* ==================== 命中/几何（rowAt/columnAt/visualRect/indexAt） ==================== */

/** @brief 命中与视觉矩形同源：行高覆写、隐藏行列、合并区间吸附。 */
XQ_TEST(XTableView, geometry)
{
    XTableView* tv = XTableView_create(NULL, 0);
    XAbstractItemModel* model = XAbstractItemModel_create();
    XRect r;
    int row = -9;
    int col = -9;
    XQ_REQUIRE(tv != NULL && model != NULL);

    XWidget_resize((XWidget*)tv, 320, 200);
    XAbstractItemModel_setDimension(model, 3, 2);
    XAbstractItemView_setModel(&tv->m_base, model);
    /* [夹具修正 2026-11-12 / P0-32] setModel 现在把模型下发给双表头并同步
     * 段数（对标 Qt QTableView::setModel 的 verticalHeader/horizontalHeader
     * ->setModel，qtableview.cpp:1284-1285）⇒ 列宽查询走表头段尺寸，缺省段
     * 尺寸是 defaultSectionSize=100（Qt 同口径，此前无段时回落视图数组 80）。
     * 本用例只测几何反查公式，故显式把两列设回 80 保持夹具常量。 */
    XTableView_setColumnWidth(tv, 0, 80);
    XTableView_setColumnWidth(tv, 1, 80);
    XQ_EQ_I64(XTableView_columnWidth(tv, 0), 80);

    /* 缺省几何：x 自 0（无左侧表头区）、y 自顶部内嵌表头带 20。 */
    r = XTableView_visualRect(tv, 0, 0);
    XQ_CHECK(r.x == 0 && r.y == 20 && r.width == 80 && r.height == 24);
    XQ_EQ_I64(XTableView_rowViewportPosition(tv, 1), 44);
    XQ_EQ_I64(XTableView_columnViewportPosition(tv, 1), 80);

    /* 命中：列 0=0..80、列 1=80..160；行 0=20..44、行 1=44..68。 */
    XQ_EQ_I64(XTableView_columnAt(tv, 85), 1);
    XQ_EQ_I64(XTableView_rowAt(tv, 49), 1);
    XQ_CHECK(XTableView_indexAt(tv, 85, 49, &row, &col));
    XQ_EQ_I64(row, 1);
    XQ_EQ_I64(col, 1);

    /* 逐行行高覆写后几何跟随（rowViewportPosition/visualRect 同源）。 */
    XTableView_setRowHeight(tv, 1, 40);
    XQ_EQ_I64(XTableView_visualRect(tv, 1, 0).height, 40);
    XQ_EQ_I64(XTableView_rowViewportPosition(tv, 2), 84);

    /* 合并区间：被覆盖格吸附到区间原点（对齐 QTableView::indexAt）。 */
    XTableView_setSpan(tv, 0, 0, 2, 1);
    XQ_CHECK(XTableView_indexAt(tv, 5, 60, &row, &col));
    XQ_EQ_I64(row, 0);
    XQ_EQ_I64(col, 0);
    XTableView_clearSpans(tv);

    /* 隐藏行：不占高、命中跳过、视觉矩形为空。 */
    XTableView_hideRow(tv, 0);
    XQ_EQ_I64(XTableView_rowAt(tv, 20), 1);
    XQ_EQ_I64(XTableView_rowViewportPosition(tv, 1), 20);
    r = XTableView_visualRect(tv, 0, 0);
    XQ_CHECK(r.width == 0 && r.height == 0);

    XClassDelete((XClass*)tv);
    XClassDelete((XClass*)model);
}

/* ==================== 表头所有权（setHorizontalHeader） ==================== */

static int g_tvOldHeaderDestroyed; /**< 旧表头 destroyed 命中次数。 */

/** @brief destroyed 槽（只计数）。 */
static void xqTv_destroyedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++g_tvOldHeaderDestroyed;
}

/** @brief 替换表头：旧内建表头被删除、新表头 setParent(this) 且信号重接。 */
XQ_TEST(XTableView, headerOwnership)
{
    XTableView* tv = XTableView_create(NULL, 0);
    XHeaderView* old;
    XHeaderView* fresh;
    XQ_REQUIRE(tv != NULL);
    old = XTableView_horizontalHeader(tv);
    fresh = XHeaderView_create(NULL, 0, 0);
    XQ_REQUIRE(old != NULL && fresh != NULL);
    XHeaderView_setCount(old, 2);

    g_tvOldHeaderDestroyed = 0;
    XObject_connect((XObject*)old, XSignal(XObject_destroyed_signal),
                      (XObject*)tv, xqTv_destroyedSlot, XConnectionType_Direct);
    /* Qt 6.8 setHorizontalHeader（qtableview.cpp:1366-1399）：旧表头
     * （父为本视图）删除、新表头 setParent(this)、首段可动置真、四条
     * 几何信号重接。 */
    XTableView_setHorizontalHeader(tv, fresh);
    XQ_CHECK(XTableView_horizontalHeader(tv) == fresh);
    XQ_EQ_I64(g_tvOldHeaderDestroyed, 1);
    XQ_CHECK(XWidget_parentWidget((XWidget*)fresh) == (XWidget*)tv);
    XQ_CHECK(XHeaderView_isFirstSectionMovable(fresh));

    /* 新表头的段尺寸成为视图列宽事实源（sectionResized 回接）。 */
    XHeaderView_setCount(fresh, 3);
    XHeaderView_resizeSection(fresh, 0, 90);
    XQ_EQ_I64(XTableView_columnWidth(tv, 0), 90);
    XHeaderView_setSectionHidden(fresh, 1, true);
    XQ_EQ_I64(XTableView_columnWidth(tv, 1), 0);

    /* NULL 是本库扩展：只解挂、不析构（Qt 头指针不可为空）。 */
    g_tvOldHeaderDestroyed = 0;
    XTableView_setHorizontalHeader(tv, NULL);
    XQ_CHECK(XTableView_horizontalHeader(tv) == NULL);
    XQ_EQ_I64(g_tvOldHeaderDestroyed, 0);
    XQ_CHECK(XWidget_parentWidget((XWidget*)fresh) == (XWidget*)tv);
    /* fresh 仍是视图子控件，随视图析构（漏删即判 [LEAK]）。 */
    XClassDelete((XClass*)tv);
}

/* ==================== 父子所有权 ==================== */

/** @brief 视图（连同其双表头子控件）随父控件析构。 */
XQ_TEST(XTableView, parentOwnership)
{
    XWidget* host = XWidget_create(NULL, 0);
    XTableView* tv;
    XQ_REQUIRE(host != NULL);

    tv = XTableView_create(host, 0);
    XQ_REQUIRE(tv != NULL);
    XQ_CHECK(XWidget_parentWidget((XWidget*)tv) == host);
    XQ_CHECK(XWidget_isAncestorOf(host, (XWidget*)tv));
    XQ_CHECK(XWidget_isAncestorOf(host,
                                  (XWidget*)XTableView_horizontalHeader(tv)));
    /* 子控件随 host 析构，漏删即判 [LEAK]。 */
    XClassDelete((XClass*)host);
}

/* ==================== setModel 下发双表头（P0-32） ==================== */

/** @brief setModel 把模型下发给双表头并同步段数（对标 Qt。
 *
 *  [P0-32 2026-11-12] QTableView::setModel（qtableview.cpp:1259-1287）在基类
 *  setModel 之前 `d->verticalHeader->setModel(model)` 与
 *  `d->horizontalHeader->setModel(model)`；本库对应 XHeaderView_setModel +
 *  setCount（段数由模型行列数驱动）。 */
XQ_TEST(XTableView, modelHeaderPropagation)
{
    XTableView* tv = XTableView_create(NULL, 0);
    XAbstractItemModel* m1 = XAbstractItemModel_create();
    XAbstractItemModel* m2 = XAbstractItemModel_create();
    XHeaderView* hh;
    XHeaderView* vh;
    XQ_REQUIRE(tv != NULL && m1 != NULL && m2 != NULL);
    hh = XTableView_horizontalHeader(tv);
    vh = XTableView_verticalHeader(tv);
    XQ_REQUIRE(hh != NULL && vh != NULL);

    /* 初始无模型 ⇒ 双表头 0 段。 */
    XQ_EQ_I64(XHeaderView_count(hh), 0);
    XQ_EQ_I64(XHeaderView_count(vh), 0);

    XAbstractItemModel_setDimension(m1, 3, 2);
    XAbstractItemView_setModel(&tv->m_base, m1);
    XQ_EQ_I64(XHeaderView_count(hh), 2); /* 列数 */
    XQ_EQ_I64(XHeaderView_count(vh), 3); /* 行数 */
    /* 表头关联同一模型（Qt header->setModel 的可观察效果）。 */
    XQ_CHECK(hh->m_model == (void*)m1);
    XQ_CHECK(vh->m_model == (void*)m1);

    /* 换模型：段数跟随新模型尺寸。 */
    XAbstractItemModel_setDimension(m2, 4, 1);
    XAbstractItemView_setModel(&tv->m_base, m2);
    XQ_EQ_I64(XHeaderView_count(hh), 1);
    XQ_EQ_I64(XHeaderView_count(vh), 4);
    XQ_CHECK(hh->m_model == (void*)m2);
    XQ_CHECK(vh->m_model == (void*)m2);

    /* 清模型：段数归零（表头解除关联）。 */
    XAbstractItemView_setModel(&tv->m_base, NULL);
    XQ_EQ_I64(XHeaderView_count(hh), 0);
    XQ_EQ_I64(XHeaderView_count(vh), 0);
    XQ_CHECK(hh->m_model == NULL);

    XClassDelete((XClass*)tv);
    XClassDelete((XClass*)m1);
    XClassDelete((XClass*)m2);
}

/* ==================== 合并区间随行列增删迁移（P0-32） ==================== */

/** @brief 行/列插入与删除后合并区间迁移（Qt QSpanCollection 同式）。
 *
 *  对齐 QTableViewPrivate::updateSpanInsertedRows/…（qtableview.cpp:978-1012）
 *  → QSpanCollection::updateInsertedRows（:185-229）/ updateInsertedColumns
 *  （:231-…）/ updateRemovedRows（:310-…）/ updateRemovedColumns（:435-…）。
 *  此前 span 只在 setSpan/clearSpans 增删 ⇒ 插/删行列后合并区间不迁移。 */
XQ_TEST(XTableView, spanMigration)
{
    XTableView* tv = XTableView_create(NULL, 0);
    XAbstractItemModel* m = XAbstractItemModel_create();
    XQ_REQUIRE(tv != NULL && m != NULL);
    XAbstractItemModel_setDimension(m, 6, 6);
    XAbstractItemView_setModel(&tv->m_base, m);

    /* ---- 行插入：整体在插入位之后的区间平移 ---- */
    XTableView_setSpan(tv, 2, 2, 2, 2); /* 原点 (2,2)，跨 2×2（行 2..3） */
    XTableView_shiftSpansForInsert(tv, true, 1, 1);
    XQ_EQ_I64(XTableView_rowSpan(tv, 2, 2), 1); /* 原原点已无区间 */
    XQ_EQ_I64(XTableView_rowSpan(tv, 3, 2), 2); /* 平移 +1 到行 3..4 */
    XQ_EQ_I64(XTableView_columnSpan(tv, 3, 2), 2); /* 列轴不受行插入影响 */

    /* ---- 行插入：插入位落在区间底边 ⇒ 区间被拉长（底边推进） ---- */
    XTableView_shiftSpansForInsert(tv, true, 4, 4); /* 行 3..4 → 3..5 */
    XQ_EQ_I64(XTableView_rowSpan(tv, 3, 2), 3);
    XTableView_shiftSpansForInsert(tv, true, 0, 0); /* 行 3..5 → 4..6 */
    XQ_EQ_I64(XTableView_rowSpan(tv, 4, 2), 3);

    /* ---- 行删除：整段落在删除区间的区间消失 ---- */
    XTableView_shiftSpansForDelete(tv, true, 4, 6); /* 区间 4..6 全删 */
    XQ_EQ_I64(XTableView_rowSpan(tv, 4, 2), 1);
    XQ_EQ_I64(XTableView_columnSpan(tv, 4, 2), 1);

    /* ---- 行删除：横跨删除段的区间跳过删除段（跨度减 delta） ---- */
    XTableView_setSpan(tv, 1, 1, 4, 1); /* 行 1..4 */
    XTableView_shiftSpansForDelete(tv, true, 2, 2); /* 删 1 行 */
    XQ_EQ_I64(XTableView_rowSpan(tv, 1, 1), 3); /* 4-1，行 1..3 */
    XTableView_shiftSpansForDelete(tv, true, 1, 1); /* 删原点行 ⇒ 贴上界 */
    XQ_EQ_I64(XTableView_rowSpan(tv, 1, 1), 2); /* 行 1..2 */

    /* ---- 列轴：插入/删除对称 ---- */
    XTableView_setSpan(tv, 0, 1, 1, 2); /* 原点 (0,1)，跨 1×2（列 1..2） */
    XTableView_shiftSpansForInsert(tv, false, 2, 2); /* 后半段插入：拉长 */
    XQ_EQ_I64(XTableView_columnSpan(tv, 0, 1), 3); /* 列 1..3 */
    XQ_EQ_I64(XTableView_columnSpan(tv, 0, 3), 3); /* 被覆盖格吸附同区间 */
    XTableView_shiftSpansForDelete(tv, false, 2, 2); /* 删回：跨列恢复 2 */
    XQ_EQ_I64(XTableView_columnSpan(tv, 0, 1), 2);

    /* ---- 缩到 1×1 的区间一并删除（qtableview.cpp:336-337） ---- */
    XTableView_clearSpans(tv);
    XTableView_setSpan(tv, 0, 0, 2, 1);
    XTableView_shiftSpansForDelete(tv, true, 1, 1); /* 2 跨行删 1 行 ⇒ 1×1 */
    XQ_EQ_I64(XTableView_rowSpan(tv, 0, 0), 1);
    XQ_EQ_I64(XTableView_columnSpan(tv, 0, 0), 1);

    /* 空表/非法入参安全。 */
    XTableView_shiftSpansForInsert(tv, true, -1, 0);
    XTableView_shiftSpansForDelete(tv, false, 2, 1);
    XQ_EQ_I64(XTableView_rowSpan(tv, 0, 0), 1);

    XClassDelete((XClass*)tv);
    XClassDelete((XClass*)m);
}

/* ==================== 排序（P0-33 / P0-73） ==================== */

/** @brief sortingEnabled 四步 + sortByColumn 落指示器并排模型。
 *
 *  [P0-73] Qt setSortingEnabled（qtableview.cpp:2738-2758）：① 指示器显隐；
 *  ② 断旧动态接线；③ enable 时先按当前指示段 sortByColumn 再接
 *  sortIndicatorChanged；④ disable 时接 sectionPressed/Entered →
 *  selectColumn。
 *  [P0-33] Qt sortByColumn（:3375-3384）：header->setSortIndicator + 未使能或
 *  同段同序时立即 model->sort。 */
XQ_TEST(XTableView, sorting)
{
    XTableView* tv = XTableView_create(NULL, 0);
    XAbstractItemModel* m = XAbstractItemModel_create();
    XHeaderView* hh;
    XQ_REQUIRE(tv != NULL && m != NULL);
    XAbstractItemModel_setDimension(m, 3, 2);
    XAbstractItemModel_setData_2(m, 0, 0, "b");
    XAbstractItemModel_setData_2(m, 1, 0, "c");
    XAbstractItemModel_setData_2(m, 2, 0, "a");
    XAbstractItemView_setModel(&tv->m_base, m);
    hh = XTableView_horizontalHeader(tv);
    XQ_REQUIRE(hh != NULL);

    /* 缺省关闭：指示器不显示、无排序。 */
    XQ_CHECK(!XTableView_isSortingEnabled(tv));
    XQ_CHECK(!XHeaderView_isSortIndicatorShown(hh));

    /* 开启：第一步就点亮指示器；④ 排序开关状态可读。 */
    XTableView_setSortingEnabled(tv, true);
    XQ_CHECK(XTableView_isSortingEnabled(tv));
    XQ_CHECK(XHeaderView_isSortIndicatorShown(hh));

    /* sortByColumn：落表头指示器 + 立即排序（指示段变化后同段同序强制排）。 */
    XTableView_sortByColumn(tv, 0, 0);
    XQ_EQ_I64(XHeaderView_sortIndicatorSection(hh), 0);
    XQ_EQ_I64(XHeaderView_sortIndicatorOrder(hh), 0);
    XQ_EQ_STR(XAbstractItemModel_data_2(m, 0, 0), "a");
    XQ_EQ_STR(XAbstractItemModel_data_2(m, 1, 0), "b");
    XQ_EQ_STR(XAbstractItemModel_data_2(m, 2, 0), "c");

    /* 真实信号链：setSortIndicator 是 sortIndicatorChanged 的发射点，
     * 开排序后接到排序虚槽 ⇒ 改序即排（降序）。 */
    XHeaderView_setSortIndicator(hh, 0, 1);
    XQ_EQ_I64(XHeaderView_sortIndicatorOrder(hh), 1);
    XQ_EQ_STR(XAbstractItemModel_data_2(m, 0, 0), "c");
    XQ_EQ_STR(XAbstractItemModel_data_2(m, 2, 0), "a");

    /* 关闭：指示器不显示；④ sectionPressed → selectColumn（当前列跟随）。 */
    XTableView_setSortingEnabled(tv, false);
    XQ_CHECK(!XTableView_isSortingEnabled(tv));
    XQ_CHECK(!XHeaderView_isSortIndicatorShown(hh));
    XHeaderView_sectionPressed_signal(hh, 1);
    XQ_EQ_I64(XAbstractItemView_currentColumn(&tv->m_base), 1);

    /* 反复开关不叠加槽：再开再关仍可观察同一状态（连接先断后接）。 */
    XTableView_setSortingEnabled(tv, true);
    XTableView_setSortingEnabled(tv, true);
    XTableView_setSortingEnabled(tv, false);
    XQ_CHECK(!XTableView_isSortingEnabled(tv));

    XClassDelete((XClass*)tv);
    XClassDelete((XClass*)m);
}

/* ==================== 整行/整列选择（P0-74 / P0-75） ==================== */

/** @brief selectRow/selectColumn 以 Rows/Columns 命令展开整行/整列。
 *
 *  [P0-74/75 2026-11-12] 此前只写「当前格」；Qt 是
 *  `selectionModel->select(..., command | Rows/Columns)`
 *  （qtableview.cpp:3247-3263 → QTableViewPrivate::selectRow/selectColumn
 *  的 `selectionModel->select(QItemSelection(upper, lower), command | Rows)`）。 */
XQ_TEST(XTableView, selectRowColumn)
{
    XTableView* tv = XTableView_create(NULL, 0);
    XAbstractItemModel* m = XAbstractItemModel_create();
    XItemSelectionModel* sm;
    XQ_REQUIRE(tv != NULL && m != NULL);
    XAbstractItemModel_setDimension(m, 3, 3);
    XAbstractItemView_setModel(&tv->m_base, m);
    sm = XAbstractItemView_selectionModel(&tv->m_base);
    XQ_REQUIRE(sm != NULL);

    /* selectRow(1)：当前格定到行 1，选区展开整行（3 格）。 */
    XTableView_selectRow(tv, 1);
    XQ_EQ_I64(XAbstractItemView_currentRow(&tv->m_base), 1);
    XQ_EQ_I64(XItemSelectionModel_selectedCount(sm), 3);
    XQ_CHECK(XItemSelectionModel_isSelected(sm, 1, 0));
    XQ_CHECK(XItemSelectionModel_isSelected(sm, 1, 1));
    XQ_CHECK(XItemSelectionModel_isSelected(sm, 1, 2));

    /* selectColumn(2)：整列选择清先再选（ClearAndSelect），仍是 3 格。 */
    XTableView_selectColumn(tv, 2);
    XQ_EQ_I64(XAbstractItemView_currentColumn(&tv->m_base), 2);
    XQ_EQ_I64(XItemSelectionModel_selectedCount(sm), 3);
    XQ_CHECK(XItemSelectionModel_isSelected(sm, 0, 2));
    XQ_CHECK(XItemSelectionModel_isSelected(sm, 1, 2));
    XQ_CHECK(XItemSelectionModel_isSelected(sm, 2, 2));
    XQ_CHECK(!XItemSelectionModel_isSelected(sm, 1, 0)); /* 旧的整行已清 */

    /* 越界：早退不改变当前格/选区。 */
    XTableView_selectRow(tv, -1);
    XTableView_selectColumn(tv, -1);
    XQ_EQ_I64(XAbstractItemView_currentColumn(&tv->m_base), 2);

    XClassDelete((XClass*)tv);
    XClassDelete((XClass*)m);
}

#endif /* XGUI_ON && XWIDGET_ON && XTABLEWIDGET_ON */
