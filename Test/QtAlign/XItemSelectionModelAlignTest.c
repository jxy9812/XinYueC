/**
 * @file       XItemSelectionModelAlignTest.c
 * @brief      XItemSelectionModel × Qt 6.8 QItemSelectionModel 对齐契约测试。
 * @details    对齐依据：
 *             - 头文件 /opt/Qt/6.8.3/gcc_64/include/QtCore/
 *               qitemselectionmodel.h（isSelected/isRowSelected/
 *               isColumnSelected/rowIntersectsSelection/
 *               columnIntersectsSelection/selectedIndexes/selectedRows/
 *               selectedColumns/hasSelection、setModel/model、
 *               setCurrentIndex/clear/clearSelection/reset、
 *               modelChanged/selectionChanged/currentChanged 族信号）；
 *             - 本机 Qt 6.8.0 实测（QT_QPA_PLATFORM=offscreen）：
 *               ① 未绑定模型时 select 为空操作、isRowSelected 恒 false；
 *               ② 3 列表选中 2 格 ⇒ isRowSelected=false 而
 *                  rowIntersectsSelection=true；整行 2 选中 ⇒
 *                  selectedRows(1) 只含行 2；整列 2 选中 ⇒
 *                  selectedColumns(0) 只含列 2；
 *               ③ selectedIndexes 按选区建立顺序输出（实测
 *                  (1,1)(2,0)(2,1)(2,2)）；
 *               ④ setModel 同指针为空操作、不发 modelChanged；指针变化
 *                  时静默清空选区与当前索引后发一次 modelChanged
 *                  （selectionChanged/currentChanged 命中 0）；
 *               ⑤ reset() 静默清空（两信号命中 0）。
 *
 *             覆盖：缺省矩阵、选择命令与整行/整列查询、当前索引信号时序、
 *             setModel/modelChanged 与 reset 的静默口径、对象析构。
 * @note       C99；选择模型/模型全部释放，漏删即运行器判 `[LEAK]`。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

#if XGUI_ON && XWIDGET_ON && XTABLEWIDGET_ON

#include "XItemSelectionModel.h"

/* ==================== 信号探针 ==================== */

static int g_ismSelHits;      /**< selectionChanged 命中次数。 */
static int g_ismCurHits;      /**< currentChanged 命中次数。 */
static int g_ismRowHits;      /**< currentRowChanged 命中次数。 */
static int g_ismColHits;      /**< currentColumnChanged 命中次数。 */
static int g_ismModelHits;    /**< modelChanged 命中次数。 */
static int g_ismCurRow;       /**< currentChanged 当前行载荷。 */

/** @brief selectionChanged 槽。 */
static void xqIsm_selSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++g_ismSelHits;
}

/** @brief currentChanged(row,col,prevRow,prevCol) 槽。 */
static void xqIsm_curSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    ++g_ismCurHits;
    if (!args) return;
    XVarList_args_4(args, int, row, int, col, int, prevRow, int, prevCol);
    (void)col;
    (void)prevRow;
    (void)prevCol;
    g_ismCurRow = row;
}

/** @brief currentRowChanged(row,prevRow) 槽。 */
static void xqIsm_rowSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++g_ismRowHits;
}

/** @brief currentColumnChanged(col,prevCol) 槽。 */
static void xqIsm_colSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++g_ismColHits;
}

/** @brief modelChanged(model) 槽。 */
static void xqIsm_modelSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++g_ismModelHits;
}

/** @brief 建 4 行 3 列模型。 */
static XAbstractItemModel* xqIsm_makeModel(int rows, int cols)
{
    XAbstractItemModel* m = XAbstractItemModel_create();
    if (m) XAbstractItemModel_setDimension(m, rows, cols);
    return m;
}

/* ==================== 缺省值矩阵 ==================== */

/** @brief 未绑定模型的缺省态（Qt 实测：select 空操作、查询恒 false）。 */
XQ_TEST(XItemSelectionModel, defaults)
{
    XItemSelectionModel* sm = XItemSelectionModel_create();
    int row = -9;
    int col = -9;
    XQ_REQUIRE(sm != NULL);

    XQ_CHECK(XItemSelectionModel_model(sm) == NULL);
    XQ_CHECK(!XItemSelectionModel_hasSelection(sm));
    XQ_EQ_I64(XItemSelectionModel_selectedCount(sm), 0);
    XQ_CHECK(!XItemSelectionModel_currentIndex(sm, &row, &col));
    XQ_EQ_I64(row, -1);
    XQ_EQ_I64(col, -1);
    /* Qt 6.8 规定未绑定模型的 select 为空操作（qitemselectionmodel.cpp:
     * 1284-1290 `if (!d->model.value()) { qWarning("... Selecting when no
     * model has been set will result in a no-op."); return; }`；本机
     * Qt 6.8.0 实测打印该告警且选区不变）。gap 164 已按 Qt 修：旧实现
     * 允许无模型维护选区，演示套件 xgui_demo_apitest_views.c:1590 起
     * 反向锁着旧口径（迁移台账）。 */
    XQ_CHECK(!XItemSelectionModel_select(
        sm, 0, 0, XItemSelectionModelSelectionFlag_Select));
    XQ_EQ_I64(XItemSelectionModel_selectedCount(sm), 0);
    XQ_CHECK(!XItemSelectionModel_isSelected(sm, 0, 0));
    /* 未绑定模型的 setCurrentIndex 同样空操作（Qt :1392-1395 同款守卫）。 */
    XItemSelectionModel_setCurrentIndex(
        sm, 1, 1, XItemSelectionModelSelectionFlag_SelectCurrent);
    XQ_CHECK(!XItemSelectionModel_currentIndex(sm, &row, &col));
    XQ_EQ_I64(row, -1);
    XQ_EQ_I64(col, -1);
    /* 整行/整列判定需要模型取行列数 ⇒ 无模型恒 false（Qt 同）。 */
    XQ_CHECK(!XItemSelectionModel_isRowSelected(sm, 0));
    XQ_CHECK(!XItemSelectionModel_isColumnSelected(sm, 0));
    XQ_CHECK(!XItemSelectionModel_rowIntersectsSelection(sm, 0));
    XQ_CHECK(!XItemSelectionModel_columnIntersectsSelection(sm, 0));
    XQ_EQ_I64(XItemSelectionModel_selectedIndexes(sm, NULL, NULL, 0), 0);
    XQ_EQ_I64(XItemSelectionModel_selectedRows(sm, 0, NULL, 0), 0);
    XQ_EQ_I64(XItemSelectionModel_selectedColumns(sm, 0, NULL, 0), 0);
    XItemSelectionModel_reset(sm); /* 静默清空（含无模型态） */
    XQ_CHECK(!XItemSelectionModel_hasSelection(sm));

    XClassDelete((XClass*)sm);
}

/* ==================== 选择命令与整行/整列查询 ==================== */

/** @brief select/整行整列展开与 isRowSelected/selectedRows 等查询。 */
XQ_TEST(XItemSelectionModel, selectAndQueries)
{
    XItemSelectionModel* sm = XItemSelectionModel_create();
    XAbstractItemModel* m = xqIsm_makeModel(4, 3);
    int outRows[8];
    int outCols[8];
    int n;
    XQ_REQUIRE(sm != NULL && m != NULL);
    XItemSelectionModel_setModel(sm, m);
    XQ_CHECK(XItemSelectionModel_model(sm) == m);

    /* ① 单格并入：只 1 格选中，整行/整列均不成立（实测 2/3 格仍 false）。 */
    XQ_CHECK(XItemSelectionModel_select(
        sm, 1, 1, XItemSelectionModelSelectionFlag_Select));
    XQ_EQ_I64(XItemSelectionModel_selectedCount(sm), 1);
    XQ_CHECK(XItemSelectionModel_isSelected(sm, 1, 1));
    XQ_CHECK(!XItemSelectionModel_isRowSelected(sm, 1));
    XQ_CHECK(!XItemSelectionModel_isColumnSelected(sm, 1));
    XQ_CHECK(XItemSelectionModel_rowIntersectsSelection(sm, 1));
    XQ_CHECK(XItemSelectionModel_columnIntersectsSelection(sm, 1));

    /* ② Rows 展开：整行 2 并入（模型 3 列 ⇒ 追加 3 格）。 */
    XQ_CHECK(XItemSelectionModel_select(
        sm, 2, 0,
        XItemSelectionModelSelectionFlag_Select |
            XItemSelectionModelSelectionFlag_Rows));
    XQ_EQ_I64(XItemSelectionModel_selectedCount(sm), 4);
    XQ_CHECK(XItemSelectionModel_isRowSelected(sm, 2));
    XQ_CHECK(!XItemSelectionModel_isRowSelected(sm, 1));
    /* selectedRows：只列整行选中者（实测 selectedRows(1) 只含行 2）。 */
    n = XItemSelectionModel_selectedRows(sm, 1, outRows, 8);
    XQ_EQ_I64(n, 1);
    XQ_EQ_I64(outRows[0], 2);

    /* ③ 选区顺序：Qt 按选区建立顺序输出（实测 (1,1)(2,0)(2,1)(2,2)）。 */
    n = XItemSelectionModel_selectedIndexes(sm, outRows, outCols, 8);
    XQ_EQ_I64(n, 4);
    XQ_EQ_I64(outRows[0], 1);
    XQ_EQ_I64(outCols[0], 1);
    XQ_EQ_I64(outRows[1], 2);
    XQ_EQ_I64(outCols[1], 0);

    /* ④ Columns 展开：整列 2 并入（模型 4 行）⇒ 整列成立。 */
    XQ_CHECK(XItemSelectionModel_select(
        sm, 0, 2,
        XItemSelectionModelSelectionFlag_Select |
            XItemSelectionModelSelectionFlag_Columns));
    XQ_CHECK(XItemSelectionModel_isColumnSelected(sm, 2));
    n = XItemSelectionModel_selectedColumns(sm, 0, outCols, 8);
    XQ_EQ_I64(n, 1);
    XQ_EQ_I64(outCols[0], 2);

    /* ⑤ Deselect/Toggle：已选中格移出返回 true，重复移出幂等返回 false，
     * Toggle 再翻转回来（Qt QItemSelectionModel 的逐格语义）。 */
    XQ_CHECK(XItemSelectionModel_select(
        sm, 2, 2, XItemSelectionModelSelectionFlag_Deselect));
    XQ_CHECK(!XItemSelectionModel_isSelected(sm, 2, 2));
    XQ_CHECK(!XItemSelectionModel_isRowSelected(sm, 2)); /* 行 2 缺一格 */
    XQ_CHECK(!XItemSelectionModel_select(
        sm, 2, 2, XItemSelectionModelSelectionFlag_Deselect)); /* 幂等 */
    XQ_CHECK(XItemSelectionModel_select(
        sm, 2, 2, XItemSelectionModelSelectionFlag_Toggle));
    XQ_CHECK(XItemSelectionModel_isSelected(sm, 2, 2));
    XQ_CHECK(XItemSelectionModel_isRowSelected(sm, 2));

    /* ⑥ 区间 + ClearAndSelect：清空后只留区间。 */
    XQ_CHECK(XItemSelectionModel_selectSelection(
        sm, 0, 0, 1, 1, XItemSelectionModelSelectionFlag_ClearAndSelect));
    XQ_EQ_I64(XItemSelectionModel_selectedCount(sm), 4); /* 2 行 × 2 列 */
    XQ_CHECK(XItemSelectionModel_clearSelection(sm));
    XQ_CHECK(!XItemSelectionModel_hasSelection(sm));

    XClassDelete((XClass*)sm);
    XClassDelete((XClass*)m);
}

/* ==================== 当前索引信号时序 ==================== */

/** @brief setCurrentIndex 的 currentChanged 族发射与载荷。 */
XQ_TEST(XItemSelectionModel, currentIndexSignals)
{
    XItemSelectionModel* sm = XItemSelectionModel_create();
    XAbstractItemModel* m = xqIsm_makeModel(3, 3);
    int row = -9;
    int col = -9;
    XQ_REQUIRE(sm != NULL && m != NULL);
    XItemSelectionModel_setModel(sm, m);

    g_ismSelHits = 0;
    g_ismCurHits = 0;
    g_ismRowHits = 0;
    g_ismColHits = 0;
    XObject_connect_1((XObject*)sm,
                      XSignal(XItemSelectionModel_selectionChanged_signal),
                      (XObject*)sm, xqIsm_selSlot, XConnectionType_Direct);
    XObject_connect_1((XObject*)sm,
                      XSignal(XItemSelectionModel_currentChanged_signal),
                      (XObject*)sm, xqIsm_curSlot, XConnectionType_Direct);
    XObject_connect_1((XObject*)sm,
                      XSignal(XItemSelectionModel_currentRowChanged_signal),
                      (XObject*)sm, xqIsm_rowSlot, XConnectionType_Direct);
    XObject_connect_1((XObject*)sm,
                      XSignal(XItemSelectionModel_currentColumnChanged_signal),
                      (XObject*)sm, xqIsm_colSlot, XConnectionType_Direct);

    /* 首次设置：行列同时变化 ⇒ currentChanged + currentRowChanged +
     * currentColumnChanged 各一次。 */
    XItemSelectionModel_setCurrentIndex(
        sm, 1, 2, XItemSelectionModelSelectionFlag_SelectCurrent);
    XQ_CHECK(XItemSelectionModel_currentIndex(sm, &row, &col));
    XQ_EQ_I64(row, 1);
    XQ_EQ_I64(col, 2);
    XQ_EQ_I64(g_ismCurHits, 1);
    XQ_EQ_I64(g_ismCurRow, 1);
    XQ_EQ_I64(g_ismRowHits, 1);
    XQ_EQ_I64(g_ismColHits, 1);
    XQ_EQ_I64(g_ismSelHits, 1); /* SelectCurrent 同时并入选区 */

    /* 只换列：currentChanged + currentColumnChanged，不发 currentRowChanged。 */
    XItemSelectionModel_setCurrentIndex(
        sm, 1, 0, XItemSelectionModelSelectionFlag_NoUpdate);
    XQ_EQ_I64(g_ismCurHits, 2);
    XQ_EQ_I64(g_ismRowHits, 1);
    XQ_EQ_I64(g_ismColHits, 2);
    XQ_EQ_I64(g_ismSelHits, 1); /* NoUpdate 不动选区 */

    /* 同格重设：不发射 currentChanged 族（但可改变选区）。 */
    XItemSelectionModel_setCurrentIndex(
        sm, 1, 0, XItemSelectionModelSelectionFlag_Select);
    XQ_EQ_I64(g_ismCurHits, 2);
    XQ_EQ_I64(g_ismSelHits, 2);

    /* clearCurrentIndex：复位并发射三信号。 */
    XItemSelectionModel_clearCurrentIndex(sm);
    XQ_CHECK(!XItemSelectionModel_currentIndex(sm, &row, &col));
    XQ_EQ_I64(g_ismCurHits, 3);
    XQ_EQ_I64(g_ismRowHits, 2);
    XQ_EQ_I64(g_ismColHits, 3);

    XClassDelete((XClass*)sm);
    XClassDelete((XClass*)m);
}

/* ==================== setModel/modelChanged 与 reset 的静默口径 ==================== */

/** @brief setModel 变化才发 modelChanged 且静默清空；reset 静默清空。 */
XQ_TEST(XItemSelectionModel, modelChangedAndReset)
{
    XItemSelectionModel* sm = XItemSelectionModel_create();
    XAbstractItemModel* m1 = xqIsm_makeModel(3, 3);
    XAbstractItemModel* m2 = xqIsm_makeModel(2, 2);
    int row = 0;
    int col = 0;
    XQ_REQUIRE(sm != NULL && m1 != NULL && m2 != NULL);

    g_ismModelHits = 0;
    g_ismSelHits = 0;
    g_ismCurHits = 0;
    XObject_connect_1((XObject*)sm,
                      XSignal(XItemSelectionModel_modelChanged_signal),
                      (XObject*)sm, xqIsm_modelSlot, XConnectionType_Direct);
    XObject_connect_1((XObject*)sm,
                      XSignal(XItemSelectionModel_selectionChanged_signal),
                      (XObject*)sm, xqIsm_selSlot, XConnectionType_Direct);
    XObject_connect_1((XObject*)sm,
                      XSignal(XItemSelectionModel_currentChanged_signal),
                      (XObject*)sm, xqIsm_curSlot, XConnectionType_Direct);

    XItemSelectionModel_setModel(sm, m1);
    XQ_EQ_I64(g_ismModelHits, 1); /* NULL → m1 变化一次 */
    XItemSelectionModel_setModel(sm, m1);
    XQ_EQ_I64(g_ismModelHits, 1); /* 同指针空操作（实测 Qt 亦不发） */

    XItemSelectionModel_select(sm, 1, 1, XItemSelectionModelSelectionFlag_Select);
    XItemSelectionModel_setCurrentIndex(sm, 1, 1,
                                        XItemSelectionModelSelectionFlag_NoUpdate);
    XQ_EQ_I64(g_ismSelHits, 1);
    XQ_EQ_I64(g_ismCurHits, 1);

    /* 换模型：静默清空选区与当前索引，只发 modelChanged（实测口径）。 */
    XItemSelectionModel_setModel(sm, m2);
    XQ_EQ_I64(g_ismModelHits, 2);
    XQ_EQ_I64(g_ismSelHits, 1); /* 未发 selectionChanged */
    XQ_EQ_I64(g_ismCurHits, 1); /* 未发 currentChanged */
    XQ_CHECK(!XItemSelectionModel_hasSelection(sm));
    XQ_CHECK(!XItemSelectionModel_currentIndex(sm, &row, &col));

    /* reset：静默清空（Qt 实测两信号命中 0）。 */
    XItemSelectionModel_select(sm, 0, 0, XItemSelectionModelSelectionFlag_Select);
    XItemSelectionModel_setCurrentIndex(sm, 0, 0,
                                        XItemSelectionModelSelectionFlag_NoUpdate);
    g_ismSelHits = 0;
    g_ismCurHits = 0;
    XItemSelectionModel_reset(sm);
    XQ_CHECK(!XItemSelectionModel_hasSelection(sm));
    XQ_CHECK(!XItemSelectionModel_currentIndex(sm, &row, &col));
    XQ_EQ_I64(g_ismSelHits, 0);
    XQ_EQ_I64(g_ismCurHits, 0);

    XClassDelete((XClass*)sm);
    XClassDelete((XClass*)m1);
    XClassDelete((XClass*)m2);
}

#endif /* XGUI_ON && XWIDGET_ON && XTABLEWIDGET_ON */
