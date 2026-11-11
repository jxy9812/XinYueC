/**
 * @file       XTableWidgetAlignTest.c
 * @brief      XTableWidget × Qt 6.8 QTableWidget 对齐契约测试。
 * @details    对齐依据：
 *             - 头文件 /opt/Qt/6.8.3/gcc_64/include/QtWidgets/qtablewidget.h
 *               （rowCount/columnCount、item/setItem/takeItem、当前格族、
 *               selectedItems/selectedRanges、itemChanged/cellChanged 等
 *               信号；setCurrentCell/ setCurrentItem 的 SelectionFlags
 *               重载在 qtablewidget.h:151-152）；
 *             - 本机 Qt 6.8.0 实测（QT_QPA_PLATFORM=offscreen）：
 *               ① 缺省 rowCount=0、columnCount=0、sortingEnabled=false、
 *                  currentRow/currentColumn=-1；
 *               ② `setCurrentCell(row,col)` 走
 *                  `setCurrentIndex(index)`（qtablewidget.cpp:2222-2225），
 *                  后者按视图选择命令 = **ClearAndSelect** ⇒ 当前格被选中
 *                  （实测 item(1,1)->isSelected()==true、selectedItems=1）；
 *               ③ `setCurrentCell(r,c,NoUpdate)` 只移当前格、选区不变；
 *               ④ `setCurrentCell(r,c,ClearAndSelect)` 清旧选区后选中该格；
 *               ⑤ 负/越界坐标 ⇒ 无效索引 ⇒ currentRow/currentColumn=-1；
 *               ⑥ `setCurrentItem(item)` 等价 setCurrentCell(item 行列)；
 *                  `setCurrentItem(item, flags)` 直接走 selectionModel。
 *
 *             覆盖：缺省矩阵（含 wordWrap/表头 clickable 的 Qt 缺省）、
 *             条目所有权（setItem/takeItem）、当前格与选择命令
 *             （ClearAndSelect/NoUpdate/Rows/Toggle）、信号时序、
 *             隐藏行/列的绘制与命中几何（gap 160）、DisplayRole 数值文本化
 *             （gap 295）、父子所有权。
 * @note       C99；表格/条目全部释放，漏删即运行器判 `[LEAK]`。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include <stdio.h>

#include "XGuiConfig.h"

#if XGUI_ON && XWIDGET_ON && XTABLEWIDGET_ON

#include "XTableWidget.h"
#include "XAbstractItemView.h"
#include "XItemSelectionModel.h"
#include "XVariant.h"   /* DisplayRole 数值→文本口径断言（gap 295）。 */

/* ==================== 信号探针 ==================== */

static int g_twCurCellHits;  /**< currentCellChanged 命中次数。 */
static int g_twCurCellRow;   /**< 最近一次 currentCellChanged 的当前行。 */
static int g_twSelChanged;   /**< itemSelectionChanged 命中次数。 */
static int g_twItemChanged;  /**< itemChanged 命中次数。 */

/** @brief itemChanged(item) 槽（只计数）。 */
static void xqTw_itemChangedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++g_twItemChanged;
}

/** @brief currentCellChanged(curRow,curCol,prevRow,prevCol) 槽。 */
static void xqTw_curCellSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    ++g_twCurCellHits;
    if (!args) return;
    XVarList_args_4(args, int, curRow, int, curCol, int, prevRow, int, prevCol);
    (void)curCol;
    (void)prevRow;
    (void)prevCol;
    g_twCurCellRow = curRow;
}

/** @brief itemSelectionChanged() 槽。 */
static void xqTw_selSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++g_twSelChanged;
}

/** @brief 建一个 3x3、每格都有条目的表格（条目文本 = "r<c>"）。 */
static XTableWidget* xqTw_make3x3(void)
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

/* ==================== 缺省值矩阵 ==================== */

/** @brief 缺省行列数/当前格/排序开关（Qt 6.8.0 实测逐项）。 */
XQ_TEST(XTableWidget, defaults)
{
    XTableWidget* tw = XTableWidget_create(NULL, 0);
    XQ_REQUIRE(tw != NULL);

    XQ_EQ_I64(XTableWidget_rowCount(tw), 0);
    XQ_EQ_I64(XTableWidget_columnCount(tw), 0);
    XQ_EQ_I64(XTableWidget_currentRow(tw), -1);
    XQ_EQ_I64(XTableWidget_currentColumn(tw), -1);
    XQ_CHECK(XTableWidget_currentItem(tw) == NULL);
    XQ_CHECK(XTableWidget_item(tw, 0, 0) == NULL);
    XQ_CHECK(!XTableView_isSortingEnabled(&tw->m_base));
    XQ_CHECK(XTableWidget_selectedItems(tw, NULL, 0) == 0);
    /* 内建模型（Qt 的 QTableModel）：条目经模型桥参与 dataChanged。 */
    XQ_CHECK_PTR(XTableWidget_model(tw));
    /* wordWrap 继承 QTableView 缺省 true（gap 152）；表格自建表头
     * clickable/highlight 缺省 true（Qt qtableview.cpp:577-589）。 */
    XQ_CHECK(XTableView_wordWrap(&tw->m_base));
    XQ_CHECK(XHeaderView_sectionsClickable(
        XTableView_horizontalHeader(&tw->m_base)));

    XClassDelete((XClass*)tw);
}

/* ==================== 隐藏列/行的绘制与命中几何 ==================== */

/** @brief 隐藏列/行进入本表几何：命中跳过、视觉矩形为空、显示后恢复。 */
XQ_TEST(XTableWidget, hiddenGeometry)
{
    XTableWidget* tw = xqTw_make3x3();
    XTableWidgetItem* it;
    XRect r;
    XQ_REQUIRE(tw != NULL);
    /* 列宽 = XTW_DEFAULT_COL_WIDTH(90)、行号列宽 40、表头带高 24、
     * 行高 24 ⇒ 列 0=40..130、列 1=130..220、行 0=24..48。 */
    XQ_CHECK(XTableWidget_itemAt(tw, 140, 30) ==
             XTableWidget_item(tw, 0, 1));

    /* 隐藏列 1：命中跳过、列宽查询 0、视觉矩形为空（gap 160）。 */
    XTableView_setColumnHidden(&tw->m_base, 1, true);
    XQ_EQ_I64(XTableView_columnWidth(&tw->m_base, 1), 0);
    it = XTableWidget_itemAt(tw, 140, 30);
    XQ_CHECK_MSG(it == XTableWidget_item(tw, 0, 2),
                 "隐藏列后 x=140 落列 2（列 1 0 宽不占位）");
    r = XTableWidget_visualItemRect(tw, XTableWidget_item(tw, 0, 1));
    XQ_CHECK(r.width == 0 && r.height == 0);
    /* 表头段同步隐藏（列筛选经表头几何生效）。 */
    XQ_CHECK(XHeaderView_isSectionHidden(
        XTableView_horizontalHeader(&tw->m_base), 1));

    /* 显示后恢复：命中回到列 1。 */
    XTableView_setColumnHidden(&tw->m_base, 1, false);
    XQ_CHECK(XTableWidget_itemAt(tw, 140, 30) ==
             XTableWidget_item(tw, 0, 1));

    /* 隐藏行 0：命中跳过、行矩形为空（行 1 上移到 24..48）。 */
    XTableView_setRowHidden(&tw->m_base, 0, true);
    XQ_CHECK(XTableWidget_itemAt(tw, 50, 30) ==
             XTableWidget_item(tw, 1, 0));
    r = XTableWidget_visualItemRect(tw, XTableWidget_item(tw, 0, 0));
    XQ_CHECK(r.width == 0 && r.height == 0);
    XTableView_setRowHidden(&tw->m_base, 0, false);
    XQ_CHECK(XTableWidget_itemAt(tw, 50, 30) ==
             XTableWidget_item(tw, 0, 0));

    XClassDelete((XClass*)tw);
}

/* ==================== DisplayRole 文本化（gap 295） ==================== */

/** @brief 数值/布尔 DisplayRole 按 QVariant::toString 转文本。 */
XQ_TEST(XTableWidgetItem, displayRoleText)
{
    XTableWidgetItem* it =
        XTableWidgetItem_create_utf8("x", XTableWidgetItem_Type);
    XVariant* v;
    XString* s;
    XQ_REQUIRE(it != NULL);

    /* int 42 → "42"（本机 Qt 6.8.0 实测 QTableWidgetItem::text()）。 */
    v = XVariant_create_int32(42);
    XQ_REQUIRE(v != NULL);
    XTableWidgetItem_setData(it, XItemDataRole_DisplayRole, v);
    XClassDelete((XClass*)v);
    XQ_EQ_STR(XTableWidgetItem_text_toUtf8(it), "42");
    s = XTableWidgetItem_text(it);
    XQ_REQUIRE(s != NULL);
    XQ_EQ_STR(XString_toUtf8(s), "42");
    XClassDelete((XClass*)s);

    /* double 3.5 → "3.5"；最短往返表示（Qt 实测 3.14159265358979）。 */
    v = XVariant_create_double(3.5);
    XQ_REQUIRE(v != NULL);
    XTableWidgetItem_setData(it, XItemDataRole_DisplayRole, v);
    XClassDelete((XClass*)v);
    XQ_EQ_STR(XTableWidgetItem_text_toUtf8(it), "3.5");
    v = XVariant_create_double(3.14159265358979);
    XQ_REQUIRE(v != NULL);
    XTableWidgetItem_setData(it, XItemDataRole_DisplayRole, v);
    XClassDelete((XClass*)v);
    XQ_EQ_STR(XTableWidgetItem_text_toUtf8(it), "3.14159265358979");

    /* bool true → "true"（Qt 实测）。 */
    v = XVariant_create_bool(true);
    XQ_REQUIRE(v != NULL);
    XTableWidgetItem_setData(it, XItemDataRole_DisplayRole, v);
    XClassDelete((XClass*)v);
    XQ_EQ_STR(XTableWidgetItem_text_toUtf8(it), "true");

    /* 文本型仍原样返回。 */
    XTableWidgetItem_setText_utf8(it, "hello");
    XQ_EQ_STR(XTableWidgetItem_text_toUtf8(it), "hello");

    XClassDelete((XClass*)it);
}

/* ==================== 条目所有权与索引反查 ==================== */

/** @brief setItem 接管、takeItem 交还；row/column/tableWidget 往返。 */
XQ_TEST(XTableWidget, itemOwnership)
{
    XTableWidget* tw = XTableWidget_create(NULL, 0);
    XTableWidgetItem* it;
    XTableWidgetItem* taken;
    XQ_REQUIRE(tw != NULL);
    XTableWidget_setRowCount(tw, 2);
    XTableWidget_setColumnCount(tw, 2);

    it = XTableWidgetItem_create_utf8("A", XTableWidgetItem_Type);
    XQ_REQUIRE(it != NULL);
    XTableWidget_setItem(tw, 1, 0, it);
    XQ_CHECK(XTableWidget_item(tw, 1, 0) == it);
    XQ_CHECK(XTableWidget_item_const(tw, 1, 0) == it);
    XQ_CHECK(XTableWidgetItem_tableWidget(it) == tw);
    XQ_EQ_I64(XTableWidgetItem_row(it), 1);
    XQ_EQ_I64(XTableWidgetItem_column(it), 0);
    XQ_EQ_STR(XTableWidgetItem_text_toUtf8(it), "A");

    /* 索引反查（对齐 row/column/indexFromItem）。 */
    {
        int col = -9;
        XQ_EQ_I64(XTableWidget_indexFromItem(tw, it, &col), 1);
        XQ_EQ_I64(col, 0);
        XQ_CHECK(XTableWidget_itemFromIndex(tw, 1, 0) == it);
    }

    /* 条目数据变化：setText 经内建模型同步并发 itemChanged（对齐
     * QTableWidgetItem::setData → QTableModel::itemChanged → 视图信号）。 */
    g_twItemChanged = 0;
    XObject_connect_1((XObject*)tw, XSignal(XTableWidget_itemChanged_signal),
                      (XObject*)tw, xqTw_itemChangedSlot,
                      XConnectionType_Direct);
    XTableWidgetItem_setText_utf8(it, "B");
    XQ_EQ_STR(XTableWidgetItem_text_toUtf8(it), "B");
    XQ_EQ_I64(g_twItemChanged, 1);

    /* takeItem：条目移出表格（所有权交还调用方），文本保留。 */
    taken = XTableWidget_takeItem(tw, 1, 0);
    XQ_REQUIRE(taken == it);
    XQ_CHECK(XTableWidget_item(tw, 1, 0) == NULL);
    XQ_CHECK(XTableWidgetItem_tableWidget(taken) == NULL);
    XQ_EQ_STR(XTableWidgetItem_text_toUtf8(taken), "B");
    XClassDelete((XClass*)taken); /* takeItem 后所有权在调用方。 */

    XClassDelete((XClass*)tw);
}

/* ==================== 当前格 × 选择命令（Qt 重载对齐） ==================== */

/** @brief 2 参 setCurrentCell 缺省 ClearAndSelect；_2 支持 NoUpdate/Rows/Toggle。 */
XQ_TEST(XTableWidget, currentCellCommand)
{
    XTableWidget* tw = xqTw_make3x3();
    XQ_REQUIRE(tw != NULL);

    g_twCurCellHits = 0;
    g_twCurCellRow = -9;
    g_twSelChanged = 0;
    XObject_connect_1((XObject*)tw,
                      XSignal(XTableWidget_currentCellChanged_signal),
                      (XObject*)tw, xqTw_curCellSlot,
                      XConnectionType_Direct);
    XObject_connect_1((XObject*)tw,
                      XSignal(XTableWidget_itemSelectionChanged_signal),
                      (XObject*)tw, xqTw_selSlot, XConnectionType_Direct);

    /* ① 2 参入口：当前格落定且**被选中**（Qt 实测 ClearAndSelect）。 */
    XTableWidget_setCurrentCell(tw, 1, 1);
    XQ_EQ_I64(XTableWidget_currentRow(tw), 1);
    XQ_EQ_I64(XTableWidget_currentColumn(tw), 1);
    XQ_CHECK(XTableWidgetItem_isSelected(XTableWidget_item(tw, 1, 1)));
    XQ_EQ_I64(XTableWidget_selectedItems(tw, NULL, 0), 1);
    XQ_EQ_I64(g_twCurCellHits, 1);
    XQ_EQ_I64(g_twCurCellRow, 1);
    XQ_EQ_I64(g_twSelChanged, 1); /* 单次发射（选择变化不重复计） */
    XQ_CHECK(XTableWidget_currentItem(tw) == XTableWidget_item(tw, 1, 1));

    /* ② NoUpdate：只移动当前格，选区不动（Qt 实测 selCount 保持）。 */
    XTableWidget_setCurrentCell_2(tw, 2, 2,
                                  XItemSelectionModelSelectionFlag_NoUpdate);
    XQ_EQ_I64(XTableWidget_currentRow(tw), 2);
    XQ_EQ_I64(XTableWidget_currentColumn(tw), 2);
    XQ_CHECK(!XTableWidgetItem_isSelected(XTableWidget_item(tw, 2, 2)));
    XQ_CHECK(XTableWidgetItem_isSelected(XTableWidget_item(tw, 1, 1)));
    XQ_EQ_I64(XTableWidget_selectedItems(tw, NULL, 0), 1);

    /* ③ Rows + ClearAndSelect：整行并入，旧选区被清（Qt expandSelection）。 */
    XTableWidget_setCurrentCell_2(tw, 1, 0,
        XItemSelectionModelSelectionFlag_ClearAndSelect |
        XItemSelectionModelSelectionFlag_Rows);
    XQ_CHECK(XTableWidgetItem_isSelected(XTableWidget_item(tw, 1, 0)));
    XQ_CHECK(XTableWidgetItem_isSelected(XTableWidget_item(tw, 1, 1)));
    XQ_CHECK(XTableWidgetItem_isSelected(XTableWidget_item(tw, 1, 2)));
    XQ_CHECK(!XTableWidgetItem_isSelected(XTableWidget_item(tw, 2, 2)));
    XQ_EQ_I64(XTableWidget_selectedItems(tw, NULL, 0), 3);

    /* ④ Toggle：同格再施 Toggle 取消该格（幂等翻转）。 */
    XTableWidget_setCurrentCell_2(tw, 1, 0,
                                  XItemSelectionModelSelectionFlag_Toggle);
    XQ_CHECK(!XTableWidgetItem_isSelected(XTableWidget_item(tw, 1, 0)));
    XQ_EQ_I64(XTableWidget_selectedItems(tw, NULL, 0), 2);

    /* ⑤ 越界 ⇒ 无效索引：当前格清为 -1/-1（Qt 实测）。 */
    XTableWidget_setCurrentCell_2(tw, 9, 9,
                                  XItemSelectionModelSelectionFlag_NoUpdate);
    XQ_EQ_I64(XTableWidget_currentRow(tw), -1);
    XQ_EQ_I64(XTableWidget_currentColumn(tw), -1);

    /* ⑥ setCurrentItem_2 等同 setCurrentCell_2(item 行列, command)。 */
    XTableWidget_setCurrentItem_2(tw, XTableWidget_item(tw, 0, 2),
        XItemSelectionModelSelectionFlag_ClearAndSelect);
    XQ_EQ_I64(XTableWidget_currentRow(tw), 0);
    XQ_EQ_I64(XTableWidget_currentColumn(tw), 2);
    XQ_CHECK(XTableWidgetItem_isSelected(XTableWidget_item(tw, 0, 2)));
    XQ_EQ_I64(XTableWidget_selectedItems(tw, NULL, 0), 1);

    XClassDelete((XClass*)tw);
}

/* ==================== 父子所有权 ==================== */

/** @brief 表格连同全部条目随父控件析构（条目所有权归表）。 */
XQ_TEST(XTableWidget, parentOwnership)
{
    XWidget* host = XWidget_create(NULL, 0);
    XTableWidget* tw;
    XQ_REQUIRE(host != NULL);

    tw = XTableWidget_create(host, 0);
    XQ_REQUIRE(tw != NULL);
    XTableWidget_setRowCount(tw, 2);
    XTableWidget_setColumnCount(tw, 2);
    XTableWidget_setItem(tw, 0, 1,
        XTableWidgetItem_create_utf8("x", XTableWidgetItem_Type));
    XQ_CHECK(XWidget_parentWidget((XWidget*)tw) == host);
    XQ_CHECK(XWidget_isAncestorOf(host, (XWidget*)tw));
    /* 表与其条目随 host 析构，漏删即判 [LEAK]。 */
    XClassDelete((XClass*)host);
}

#endif /* XGUI_ON && XWIDGET_ON && XTABLEWIDGET_ON */
