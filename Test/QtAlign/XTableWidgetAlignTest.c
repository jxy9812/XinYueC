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
 *             （gap 295）、父子所有权、K-68 表格体视觉序管线
 *             （moveSection 后命中/绘制/表头 position 跟随视觉序）。
 * @note       C99；表格/条目全部释放，漏删即运行器判 `[LEAK]`。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include <stdio.h>
#include <string.h>

#include "XGuiConfig.h"

#if XGUI_ON && XWIDGET_ON && XTABLEWIDGET_ON

#include "XTableWidget.h"
#include "XAbstractItemView.h"
#include "XItemSelectionModel.h"
#include "XVariant.h"   /* DisplayRole 数值→文本口径断言（gap 295）。 */
#include "XHeaderView.h"
#include "XHeaderView_Protected.h" /* K-69 按压段查询（保护入口注入）。 */
#include "XStyle.h"                /* K-69 捕获样式：CE_HeaderSection。 */
#include "XCommonStyle.h"          /* K-69 状态底色像素断言（缺省样式直绘）。 */
#include "XStyleOption.h"
#include "XImage.h"                /* 离屏渲染（XWidget_render）。 */
#include "XPainter.h"
#include "XMemory.h"               /* 捕获样式堆构造。 */
#include "XAbstractScrollArea.h"   /* 视口宽（K-73 可用口径）。 */

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
    XObject_connect((XObject*)tw, XSignal(XTableWidget_itemChanged_signal),
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
    XObject_connect((XObject*)tw,
                      XSignal(XTableWidget_currentCellChanged_signal),
                      (XObject*)tw, xqTw_curCellSlot,
                      XConnectionType_Direct);
    XObject_connect((XObject*)tw,
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

/* ==================== 合并区间随增删行列迁移（P0-32） ==================== */

/** @brief insert/removeRow/Column 后合并区间不漂移。
 *
 *  [P0-32 2026-11-12] 便利类的增删入口接 XTableView_shiftSpansFor{Insert,
 *  Delete}（对标 Qt QTableView::setModel 接的 rowsInserted/columnsInserted/
 *  rowsRemoved/columnsRemoved 四个 span 维护槽，qtableview.cpp:1267-1280）。
 *  此前 XTableWidget 全文无 m_spans 引用 ⇒ 增删行列后合并单元格错位。 */
XQ_TEST(XTableWidget, spanMigrationOnRowColumnChanges)
{
    XTableWidget* tw = XTableWidget_create_size(
        XCLASS_DEFAULT_MEMORY_TYPE, 4, 4, NULL, 0);
    XQ_REQUIRE(tw != NULL);

    /* 合并 (1,1) 起点，跨 1 行 × 2 列（列 1..2）。 */
    XTableView_setSpan(&tw->m_base, 1, 1, 1, 2);
    XQ_EQ_I64(XTableView_columnSpan(&tw->m_base, 1, 1), 2);

    /* 插行在区间之前 ⇒ 原点整体下移，跨列数不变。 */
    XTableWidget_insertRow(tw, 0);
    XQ_EQ_I64(XTableView_columnSpan(&tw->m_base, 2, 1), 2);
    XQ_EQ_I64(XTableView_rowSpan(&tw->m_base, 1, 1), 1); /* 原原点已无区间 */

    /* 插列落在区间底边 ⇒ 跨列数拉长（列 1..2 → 1..3）。 */
    XTableWidget_insertColumn(tw, 2);
    XQ_EQ_I64(XTableView_columnSpan(&tw->m_base, 2, 1), 3);
    XQ_EQ_I64(XTableView_columnSpan(&tw->m_base, 2, 3), 3); /* 被覆盖格 */

    /* 删同一列 ⇒ 跨列数恢复。 */
    XTableWidget_removeColumn(tw, 2);
    XQ_EQ_I64(XTableView_columnSpan(&tw->m_base, 2, 1), 2);

    /* 删区间所在的整行 ⇒ 区间消失（回退 1×1）。 */
    XTableWidget_removeRow(tw, 2);
    XQ_EQ_I64(XTableView_rowSpan(&tw->m_base, 2, 1), 1);
    XQ_EQ_I64(XTableView_columnSpan(&tw->m_base, 2, 1), 1);

    /* 无区间时增删行列安全（不越界、不崩）。 */
    XTableWidget_insertRow(tw, 0);
    XTableWidget_insertColumn(tw, 0);
    XTableWidget_removeRow(tw, 0);
    XTableWidget_removeColumn(tw, 0);
    XQ_EQ_I64(XTableView_rowSpan(&tw->m_base, 0, 0), 1);

    XClassDelete((XClass*)tw);
}

/* ==================== 排序开关与自动插位（P0-76） ==================== */

/** @brief setSortingEnabled 状态同步到基类字段 + setItem 自动排序插入。
 *
 *  [P0-76 2026-11-12] 此前 setter 只写 m_headerSortEnabled，而查询读继承的
 *  m_base.m_sortingEnabled ⇒ **开启后查询恒 false**；同一字段还是
 *  xtw_sortedInsert 的门闸 ⇒ setItem 的自动排序插入永不生效。 */
XQ_TEST(XTableWidget, sortingEnabledStateAndSortedInsert)
{
    XTableWidget* tw = XTableWidget_create_size(
        XCLASS_DEFAULT_MEMORY_TYPE, 3, 1, NULL, 0);
    XQ_REQUIRE(tw != NULL);

    /* 缺省关闭。 */
    XQ_CHECK(!XTableView_isSortingEnabled(&tw->m_base));

    /* 关闭态：即便设了排序指示段，setItem 也不自动插位。 */
    XTableWidget_sortItems(tw, 0, 0);
    XTableWidget_setItem(tw, 0, 0,
        XTableWidgetItem_create_utf8("b", XTableWidgetItem_Type));
    XTableWidget_setItem(tw, 1, 0,
        XTableWidgetItem_create_utf8("a", XTableWidgetItem_Type));
    XQ_EQ_STR(XTableWidgetItem_text_toUtf8(XTableWidget_item(tw, 0, 0)), "b");
    XQ_EQ_STR(XTableWidgetItem_text_toUtf8(XTableWidget_item(tw, 1, 0)), "a");

    /* 开启：① 查询不再恒 false（此前 setter 写的是另一字段）。 */
    XTableWidget_setSortingEnabled(tw, true);
    XQ_CHECK(XTableView_isSortingEnabled(&tw->m_base));

    /* ② 同一字段门闸打开后，setItem 自动按指示列插位：
     * 把 "aa" 写进行 2，应被搬移到 "a" 之后、"b" 之前。 */
    XTableWidget_setItem(tw, 2, 0,
        XTableWidgetItem_create_utf8("aa", XTableWidgetItem_Type));
    XQ_EQ_STR(XTableWidgetItem_text_toUtf8(XTableWidget_item(tw, 0, 0)), "a");
    XQ_EQ_STR(XTableWidgetItem_text_toUtf8(XTableWidget_item(tw, 1, 0)),
              "aa");
    XQ_EQ_STR(XTableWidgetItem_text_toUtf8(XTableWidget_item(tw, 2, 0)), "b");

    /* 关闭：状态复位（重复开关不叠加 sectionClicked 槽——先断后接）。 */
    XTableWidget_setSortingEnabled(tw, false);
    XQ_CHECK(!XTableView_isSortingEnabled(&tw->m_base));
    XTableWidget_setSortingEnabled(tw, true);
    XTableWidget_setSortingEnabled(tw, true);
    XTableWidget_setSortingEnabled(tw, false);
    XQ_CHECK(!XTableView_isSortingEnabled(&tw->m_base));

    XClassDelete((XClass*)tw);
}

/* ==================== K-69 highlightSections 绘制位（2026-11-14） ==================== */

/* 对齐依据：Qt 6.8.3 initStyleOptionForIndex（qheaderview.cpp:2868-2878）
 * ——clickable 开启时按压段置 State_Sunken；否则 highlightSections 开启时
 * 按选择模型补位：列内**相交** ⇒ State_On、**整列选中** ⇒ 再补
 * State_Sunken。QTableView 自建表头 clickable/highlight 缺省 true
 * （qtableview.cpp:577-589；本库 XTableView_init 显式开启）。 */

/* 捕获样式：只记 CE_HeaderSection 的 option state/section，不绘制任何
 * 内容（状态位断言不依赖像素）。 */
XCLASS_DEFINE_BEGING(XTableHeaderCaptureStyle)
XCLASS_DEFINE_EXTEND_END(XTableHeaderCaptureStyle, XStyle)

typedef struct XTableHeaderCaptureStyle
{
    XStyle m_base; /**< 基类；必须第一个。 */
} XTableHeaderCaptureStyle;

static int g_xqTwHdrState[4];        /**< 每段最近一次捕获的 option state。 */
static bool g_xqTwHdrSeen[4];        /**< 每段是否被捕获。 */
static int g_xqTwHdrHits;            /**< CE_HeaderSection 捕获总数。 */
static int g_xqTwHdrPos[4];          /**< [K-68] 每段最近一次捕获的 position。 */
static bool g_xqTwHdrPosSeen[4];     /**< [K-68] 每段 position 是否被捕获。 */

static void xqTwHdr_captureDrawControl(XStyle* self, int ce,
                                       const XStyleOption* option,
                                       XPainter* painter,
                                       const XWidget* widget)
{
    (void)self;
    (void)painter;
    (void)widget;
    if (ce == XStyleCE_HeaderSection && option &&
        option->m_optionType == XStyleOptionType_Header) {
        const XStyleOptionHeader* hdr = (const XStyleOptionHeader*)option;
        ++g_xqTwHdrHits;
        if (hdr->m_section >= 0 && hdr->m_section < 4) {
            g_xqTwHdrState[hdr->m_section] = (int)hdr->m_base.m_state;
            g_xqTwHdrSeen[hdr->m_section] = true;
            g_xqTwHdrPos[hdr->m_section] = (int)hdr->m_position;
            g_xqTwHdrPosSeen[hdr->m_section] = true;
        }
    }
}

static XVtable* XTableHeaderCaptureStyle_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XTableHeaderCaptureStyle)
    XVTABLE_INHERIT_XCLASS(XStyle);
    XVTABLE_OVERLOAD_DEFAULT(EXStyle_DrawControl, xqTwHdr_captureDrawControl);
    return XVTABLE_DEFAULT;
}

/** @brief 堆上建捕获样式（调用方 XClassDelete）。 */
static XTableHeaderCaptureStyle* xqTwHdr_createCaptureStyle(void)
{
    XTableHeaderCaptureStyle* style = (XTableHeaderCaptureStyle*)
        XMemory_malloc(sizeof(XTableHeaderCaptureStyle),
                       XCLASS_DEFAULT_MEMORY_TYPE);
    if (!style) return NULL;
    XStyle_init(&style->m_base);
    XClassSetVtable(style, XTableHeaderCaptureStyle);
    Set_Class_Memory(style, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(style, true);
    return style;
}

/** @brief 离屏抓整表一遍（XWidget_grab 同步重绘路径 ⇒ 派发 paint ⇒
 *         表头 option 构造被捕获）。 */
static bool xqTwHdr_renderOnce(XTableWidget* tw)
{
    XImage* grabbed;
    if (!tw) return false;
    grabbed = XWidget_grab((XWidget*)tw);
    if (grabbed) XClassDelete((XClass*)grabbed);
    return grabbed != NULL;
}

/** @brief highlightSections：按选择模型给表头段补 State_On/State_Sunken。 */
XQ_TEST(XTableWidget, headerHighlightSectionsState)
{
    XTableWidget* tw = XTableWidget_create(NULL, 0);
    XHeaderView* hh;
    XTableHeaderCaptureStyle* style;
    XItemSelectionModel* sm;
    XMouseEvent me;
    XPoint pos;
    XQ_REQUIRE(tw != NULL);
    XTableWidget_setRowCount(tw, 2);
    XTableWidget_setColumnCount(tw, 2);
    XWidget_resize((XWidget*)tw, 320, 160);
    hh = XTableView_horizontalHeader(&tw->m_base);
    XQ_REQUIRE(hh != NULL);
    /* 视图自建表头缺省：clickable/highlight 均 true（Qt 口径）。 */
    XQ_CHECK(XHeaderView_sectionsClickable(hh));
    XQ_CHECK(XHeaderView_highlightSections(hh));

    style = xqTwHdr_createCaptureStyle();
    XQ_REQUIRE(style != NULL);
    XWidget_setStyle((XWidget*)tw, (XStyle*)style);

    /* ① 相交未整列：选 (0,0)（2 行表 ⇒ 列 0 相交、非整列）。 */
    sm = XAbstractItemView_selectionModel(&tw->m_base.m_base);
    XQ_REQUIRE(sm != NULL);
    XQ_CHECK(XItemSelectionModel_select(sm, 0, 0,
                                        XItemSelectionModelSelectionFlag_Clear
                                            |
                                        XItemSelectionModelSelectionFlag_Select));
    g_xqTwHdrHits = 0;
    memset(g_xqTwHdrSeen, 0, sizeof(g_xqTwHdrSeen));
    memset(g_xqTwHdrState, 0, sizeof(g_xqTwHdrState));
    XQ_CHECK(xqTwHdr_renderOnce(tw));
    XQ_EQ_I64(g_xqTwHdrHits, 2);
    XQ_CHECK(g_xqTwHdrSeen[0] && g_xqTwHdrSeen[1]);
    XQ_CHECK((g_xqTwHdrState[0] & XStyleState_On) != 0);
    XQ_CHECK((g_xqTwHdrState[0] & XStyleState_Sunken) == 0);
    XQ_CHECK((g_xqTwHdrState[1] & XStyleState_On) == 0);
    XQ_CHECK((g_xqTwHdrState[1] & XStyleState_Sunken) == 0);

    /* ② 补选 (1,0) ⇒ 列 0 整列 ⇒ On | Sunken。 */
    XQ_CHECK(XItemSelectionModel_select(sm, 1, 0,
                                        XItemSelectionModelSelectionFlag_Select));
    g_xqTwHdrHits = 0;
    memset(g_xqTwHdrSeen, 0, sizeof(g_xqTwHdrSeen));
    memset(g_xqTwHdrState, 0, sizeof(g_xqTwHdrState));
    XQ_CHECK(xqTwHdr_renderOnce(tw));
    XQ_EQ_I64(g_xqTwHdrHits, 2);
    XQ_CHECK((g_xqTwHdrState[0] & XStyleState_On) != 0);
    XQ_CHECK((g_xqTwHdrState[0] & XStyleState_Sunken) != 0);
    XQ_CHECK((g_xqTwHdrState[1] & XStyleState_On) == 0);
    XQ_CHECK((g_xqTwHdrState[1] & XStyleState_Sunken) == 0);

    /* ③ 关 highlight ⇒ 选中列不再补位（Qt :2873 的 else if 门）。 */
    XHeaderView_setHighlightSections(hh, false);
    g_xqTwHdrHits = 0;
    memset(g_xqTwHdrState, 0, sizeof(g_xqTwHdrState));
    XQ_CHECK(xqTwHdr_renderOnce(tw));
    XQ_EQ_I64(g_xqTwHdrHits, 2);
    XQ_CHECK((g_xqTwHdrState[0] & XStyleState_On) == 0);
    XQ_CHECK((g_xqTwHdrState[0] & XStyleState_Sunken) == 0);

    /* ④ 按压段（clickable 门控）：highlight 关着也置 Sunken
     * （Qt :2870-2872 的 pressed 分支）。表头局部坐标：列宽缺省 90，
     * 列 1 = [90,180) ⇒ 按压 x=100；release 清账。 */
    XPoint_init(&pos, 100, 10);
    XMouseEvent_init(&me, XEVENT_TYPE_MOUSE_BUTTON_PRESS,
                     XMouseButton_LeftButton, 0, pos);
    XHeaderView_forwardPress(hh, (XEvent*)&me);
    g_xqTwHdrHits = 0;
    memset(g_xqTwHdrState, 0, sizeof(g_xqTwHdrState));
    XQ_CHECK(xqTwHdr_renderOnce(tw));
    XQ_EQ_I64(g_xqTwHdrHits, 2);
    XQ_CHECK((g_xqTwHdrState[1] & XStyleState_Sunken) != 0);
    XQ_CHECK((g_xqTwHdrState[0] & XStyleState_Sunken) == 0);
    XPoint_init(&pos, 100, 10);
    XMouseEvent_init(&me, XEVENT_TYPE_MOUSE_BUTTON_RELEASE,
                     XMouseButton_LeftButton, 0, pos);
    XHeaderView_forwardRelease(hh, (XEvent*)&me);

    XWidget_setStyle((XWidget*)tw, NULL);
    XClassDelete((XClass*)style);
    XClassDelete((XClass*)tw);
}

/* ==================== K-69 样式侧状态底色（2026-11-14） ==================== */

/* 对齐依据：Qt 6.8.3 qcommonstyle.cpp:2218-2222——QCommonStyle 的
 * CE_HeaderSection 只把 State_Sunken 转给 qDrawShadePanel（qdrawutil.cpp:
 * 329-410：sunken ⇒ 边线 pal.dark()/pal.light() 翻转、fill=Button 内缩
 * 1px），State_On 本体不消费（可见高亮在 Qt 里由样式侧承接）。本库
 * XCommonStyle 口径：State_On ⇒ Highlight 底、State_Sunken（无 On）⇒
 * Button 向 palette Dark 调暗（xcs_merged 45%）——与本文件
 * headerHighlightSectionsState 的状态位构造（On=列相交/On|Sunken=整列/
 * Sunken=按压）一一对应，此处用像素证明默认主题下高亮**可见**。 */

/** @brief 以给定调色板/状态直绘一次 CE_HeaderSection 到 60x20 ARGB32
 *         离屏图，返回探针像素（(30,10)=段心，避开底/右 1px 边线）。 */
static uint32_t xqTwHdr_renderSection(uint32_t state)
{
    XCommonStyle* style;
    XStyleOptionHeader opt;
    XPalette palette;
    XImage image;
    XPainter painter;
    uint32_t px = 0u;
    style = XCommonStyle_create();
    if (!style) return 0u;
    palette = XPalette_create();
    XPalette_setColor(&palette, XPaletteColorGroup_Current,
                      XPaletteColorRole_Button,
                      XColor_create_rgb(200, 200, 200, 255));
    XPalette_setColor(&palette, XPaletteColorGroup_Current,
                      XPaletteColorRole_Dark,
                      XColor_create_rgb(80, 80, 80, 255));
    XPalette_setColor(&palette, XPaletteColorGroup_Current,
                      XPaletteColorRole_Highlight,
                      XColor_create_rgb(255, 0, 0, 255));
    XStyleOptionHeader_init(&opt, XStyleCE_HeaderSection);
    opt.m_base.m_rect = (XRect){0, 0, 60, 20};
    opt.m_base.m_state = XStyleState_Enabled | XStyleState_Horizontal |
                         state;
    opt.m_base.m_palette = palette;
    XImage_init_ex(&image, 60, 20, XImageFormat_ARGB32);
    XImage_fill(&image, 0u);
    XPainter_init(&painter, NULL);
    if (XPainter_begin_image(&painter, &image)) {
        XStyle_drawControl_base((XStyle*)style, XStyleCE_HeaderSection,
                                &opt.m_base, &painter, NULL);
        XPainter_end(&painter);
        px = XImage_pixel(&image, 30, 10);
    }
    XPainter_deinit(&painter);
    XClassDeinit(&image);
    XClassDelete((XClass*)style);
    return px;
}

/** @brief CE_HeaderSection 消费 State_On/State_Sunken：默认主题下选中
 *         列段=Highlight 底、按压段=暗化 Button 底，两态像素可判。 */
XQ_TEST(XTableWidget, headerSectionStateColors)
{
    uint32_t plain = xqTwHdr_renderSection(0);
    uint32_t pressed = xqTwHdr_renderSection(XStyleState_Sunken);
    uint32_t selected = xqTwHdr_renderSection(XStyleState_On);
    uint32_t selectedAndPressed =
        xqTwHdr_renderSection(XStyleState_On | XStyleState_Sunken);
    /* 无状态位：Button 底（200,200,200）。 */
    XQ_EQ_I64(plain, 0xFFC8C8C8u);
    /* State_Sunken（按压）：Button 向 Dark(80) 调暗 45% ⇒ (146,146,146)
     * —— xcs_merged(200,80,45) 逐通道整数差量恰为 -54。 */
    XQ_EQ_I64(pressed, 0xFF929292u);
    /* State_On（列相交选中）：Highlight 底（255,0,0）。 */
    XQ_EQ_I64(selected, 0xFFFF0000u);
    /* On|Sunken（整列选中）：选中口径胜出，仍 Highlight 底。 */
    XQ_EQ_I64(selectedAndPressed, 0xFFFF0000u);
    /* 三态互异（像素证明高亮在默认主题下可见——K-69 的立案口径）。 */
    XQ_CHECK(plain != pressed);
    XQ_CHECK(plain != selected);
    XQ_CHECK(pressed != selected);
}

/* ==================== K-73 stretchLastSection 视图随动（2026-11-14） ==================== */

/* 对齐依据：Qt 6.8.3 探针（/tmp/qsprobe）——resize 事件即重排末段
 * （qheaderview.cpp:2817-2827）、程序化调末段抬快照下限（:851-852）、
 * 隐藏列后次末列吃满余量（:3502-3516）。本库由表格侧推送可用长
 * （视口宽 − 行号列宽 40），引擎在表头侧重排。 */
XQ_TEST(XTableWidget, stretchLastSectionViewportFollow)
{
    XTableWidget* tw = XTableWidget_create(NULL, 0);
    XHeaderView* hh;
    int avail;
    XQ_REQUIRE(tw != NULL);
    XTableWidget_setColumnCount(tw, 4);
    XTableView_setColumnWidth(&tw->m_base, 0, 100);
    XTableView_setColumnWidth(&tw->m_base, 1, 150);
    XTableView_setColumnWidth(&tw->m_base, 2, 200);
    XTableView_setColumnWidth(&tw->m_base, 3, 200);
    XWidget_resize((XWidget*)tw, 900, 200);
    hh = XTableView_horizontalHeader(&tw->m_base);
    XQ_REQUIRE(hh != NULL);

    /* 开启（resize 已把可用长推送进引擎）⇒ 末段 = 可用 − 450。
     * 可用 = 视口宽 − 行号列宽 40（与 Stretch 分配器同口径）。 */
    XHeaderView_setStretchLastSection(hh, true);
    avail = XWidget_width(XAbstractScrollArea_viewport(
              (XAbstractScrollArea*)tw)) - 40;
    XQ_EQ_I64(XTableView_columnWidth(&tw->m_base, 3), avail - 450);

    /* 程序化把末段抬到 500（> 余量）⇒ 快照托底 ⇒ 500。 */
    XTableView_setColumnWidth(&tw->m_base, 3, 500);
    XQ_EQ_I64(XTableView_columnWidth(&tw->m_base, 3), 500);
    /* 视口加宽 ⇒ 余量超过快照 ⇒ 末段回吃余量。 */
    XWidget_resize((XWidget*)tw, 1200, 200);
    avail = XWidget_width(XAbstractScrollArea_viewport(
              (XAbstractScrollArea*)tw)) - 40;
    XQ_EQ_I64(XTableView_columnWidth(&tw->m_base, 3), avail - 450);

    /* 隐藏中间列 1 ⇒ 末段仍是列 3，吃掉腾出的空间（余量 = 可用 − 300）。 */
    XTableView_setColumnHidden(&tw->m_base, 1, true);
    avail = XWidget_width(XAbstractScrollArea_viewport(
              (XAbstractScrollArea*)tw)) - 40;
    XQ_EQ_I64(XTableView_columnWidth(&tw->m_base, 1), 0);
    XQ_EQ_I64(XTableView_columnWidth(&tw->m_base, 2), 200);
    XQ_EQ_I64(XTableView_columnWidth(&tw->m_base, 3), avail - 300);

    /* 显示列 1 ⇒ 末段回到 avail − 450；列 2 保持 200。 */
    XTableView_setColumnHidden(&tw->m_base, 1, false);
    avail = XWidget_width(XAbstractScrollArea_viewport(
              (XAbstractScrollArea*)tw)) - 40;
    XQ_EQ_I64(XTableView_columnWidth(&tw->m_base, 3), avail - 450);
    XQ_EQ_I64(XTableView_columnWidth(&tw->m_base, 2), 200);

    /* 隐藏**末列** 3 ⇒ 末段身份切换（对标探针 D）：旧末段按快照复原
     * 500（隐藏列 sectionWidth 读 0，复原值由下方显示步间接证明——若未
     * 复原，重新显示后末段会是 860 而非 710）、新末段列 2 吃满余量。 */
    XTableView_setColumnHidden(&tw->m_base, 3, true);
    avail = XWidget_width(XAbstractScrollArea_viewport(
              (XAbstractScrollArea*)tw)) - 40;
    XQ_EQ_I64(XTableView_columnWidth(&tw->m_base, 3), 0);
    XQ_EQ_I64(XTableView_columnWidth(&tw->m_base, 2), avail - 250);

    /* 显示列 3 ⇒ 末段回到列 3：列 2 复原快照 200、列 3 回吃余量。 */
    XTableView_setColumnHidden(&tw->m_base, 3, false);
    avail = XWidget_width(XAbstractScrollArea_viewport(
              (XAbstractScrollArea*)tw)) - 40;
    XQ_EQ_I64(XTableView_columnWidth(&tw->m_base, 2), 200);
    XQ_EQ_I64(XTableView_columnWidth(&tw->m_base, 3), avail - 450);

    /* 全局 Stretch 共存：切模式即表头批量均分（XHeaderView_resizeSections
     * 批量路径按当前段合计均分）；末段走快照下限 qMax(均值, 快照 500)
     * （Qt 6.8 原文 qheaderview.cpp:3619-3623 对全局模式同样生效——
     * 与下方 K-68 残留 c 块同一口径，旧「末段不特判」注释已废）。 */
    XHeaderView_setSectionResizeMode(hh, XHeaderViewResizeMode_Stretch);
    avail = XWidget_width(XAbstractScrollArea_viewport(
              (XAbstractScrollArea*)tw)) - 40;
    XQ_EQ_I64(XTableView_columnWidth(&tw->m_base, 0), avail / 4);
    XQ_EQ_I64(XTableView_columnWidth(&tw->m_base, 3),
              (avail / 4) > 500 ? (avail / 4) : 500);
    /* [K-68 残留 c 2026-11-14] resize 事件驱动**视图侧**分摊器（Qt 全局
     * Stretch 池，qheaderview.cpp:3591-3594/3619-3626）：余数按**视觉序**
     * 前置分摊（不再收给尾列——旧口径=有意偏差已消除）、段长下限 =
     * minimumSectionSize、快照末段 qMax(stretchLen, 快照 500)。凑
     * avail ≡ 1 (mod 4) 后：col0 吃唯一余数 +1，col1/col2 = 均值，
     * col3 = qMax(均值, 快照 500)（快照=步骤②程序化抬账的 500，Qt
     * :3619-3623 对全局模式同样生效）。 */
    {
        XWidget* viewport = XAbstractScrollArea_viewport(
            (XAbstractScrollArea*)tw);
        int delta;
        int base;
        XQ_REQUIRE(viewport != NULL);
        avail = XWidget_width(viewport) - 40;
        delta = (4 - (avail % 4)) % 4 + 1; /* 凑新 avail ≡ 1 (mod 4)。 */
        XWidget_resize((XWidget*)tw, XWidget_width((XWidget*)tw) + delta,
                       200);
        avail = XWidget_width(viewport) - 40;
        XQ_EQ_I64(avail % 4, 1);
        base = avail / 4;
        XQ_EQ_I64(XTableView_columnWidth(&tw->m_base, 0), base + 1);
        XQ_EQ_I64(XTableView_columnWidth(&tw->m_base, 1), base);
        XQ_EQ_I64(XTableView_columnWidth(&tw->m_base, 2), base);
        XQ_EQ_I64(XTableView_columnWidth(&tw->m_base, 3),
                  base > 500 ? base : 500);
    }
    XClassDelete((XClass*)tw);
}

/* ==================== K-68 残留 c：Stretch 余数视觉序前置分摊（2026-11-14） ==================== */

/* 对齐依据：Qt 6.8.3 QHeaderViewPrivate::resizeSections 全局 Stretch 池
 * （qheaderview.cpp:3591-3594 hint/floor/余数、3619-3626 视觉序应用）——
 * 余数 pixelReminder 从视觉首段起逐段 +1（探针 A：638 视口 4 段 =
 * 160/160/159/159），不再整份收给逻辑末列（旧口径=有意偏差，已消除）；
 * 段长下限 = minimumSectionSize（:3593）；本用例不开 stretchLast ⇒
 * 无末段快照 ⇒ 下限不生效（qheaderview_p.h:71「Only trust when we
 * stretch last section」）。触发链：切模式走表头批量均分，视图分摊器
 * 在 resize 事件驱动（VX_tableWidget_resizeEvent）⇒ 切模式后 resize
 * 一次锁视觉序分布。 */
XQ_TEST(XTableWidget, stretchRemainderVisualOrder)
{
    XTableWidget* tw = XTableWidget_create(NULL, 0);
    XHeaderView* hh;
    XWidget* viewport;
    int avail;
    int base;
    int delta;
    XQ_REQUIRE(tw != NULL);
    XTableWidget_setColumnCount(tw, 4);
    XTableView_setColumnWidth(&tw->m_base, 0, 100);
    XTableView_setColumnWidth(&tw->m_base, 1, 150);
    XTableView_setColumnWidth(&tw->m_base, 2, 200);
    XTableView_setColumnWidth(&tw->m_base, 3, 250);
    XWidget_resize((XWidget*)tw, 900, 200);
    hh = XTableView_horizontalHeader(&tw->m_base);
    XQ_REQUIRE(hh != NULL);
    XQ_CHECK(!XHeaderView_stretchLastSection(hh));

    XHeaderView_setSectionResizeMode(hh, XHeaderViewResizeMode_Stretch);
    XWidget_resize((XWidget*)tw, 901, 200);
    viewport = XAbstractScrollArea_viewport((XAbstractScrollArea*)tw);
    XQ_REQUIRE(viewport != NULL);
    /* 凑 avail ≡ 1 (mod 4)：唯一余数滴在视觉首段（旧口径滴在逻辑末列）。 */
    avail = XWidget_width(viewport) - 40;
    delta = (4 - (avail % 4)) % 4 + 1;
    XWidget_resize((XWidget*)tw, XWidget_width((XWidget*)tw) + delta, 200);
    avail = XWidget_width(viewport) - 40;
    XQ_EQ_I64(avail % 4, 1);
    base = avail / 4;
    /* ① 恒等映射：col0 = 均值+1，其余 = 均值；合计仍 = avail。 */
    XQ_EQ_I64(XTableView_columnWidth(&tw->m_base, 0), base + 1);
    XQ_EQ_I64(XTableView_columnWidth(&tw->m_base, 1), base);
    XQ_EQ_I64(XTableView_columnWidth(&tw->m_base, 2), base);
    XQ_EQ_I64(XTableView_columnWidth(&tw->m_base, 3), base);
    XQ_EQ_I64(XTableView_columnWidth(&tw->m_base, 0) +
              XTableView_columnWidth(&tw->m_base, 1) +
              XTableView_columnWidth(&tw->m_base, 2) +
              XTableView_columnWidth(&tw->m_base, 3), avail);

    /* ② moveSection(0,3)（视觉序 [1,2,3,0]）后余数跟随**视觉**首段 =
     * 逻辑 1（+4 保 avail%4==1）；快照无 ⇒ 逻辑 3 无下限照常均分。 */
    XHeaderView_moveSection(hh, 0, 3);
    XWidget_resize((XWidget*)tw, XWidget_width((XWidget*)tw) + 4, 200);
    avail = XWidget_width(viewport) - 40;
    base = avail / 4;
    XQ_EQ_I64(avail % 4, 1);
    XQ_EQ_I64(XTableView_columnWidth(&tw->m_base, 1), base + 1);
    XQ_EQ_I64(XTableView_columnWidth(&tw->m_base, 0), base);
    XQ_EQ_I64(XTableView_columnWidth(&tw->m_base, 2), base);
    XQ_EQ_I64(XTableView_columnWidth(&tw->m_base, 3), base);

    XClassDelete((XClass*)tw);
}

/* ==================== K-68 残留 b：挂件定位跟随视觉序（2026-11-14） ==================== */

/* 对齐依据：Qt setIndexWidget 挂件的几何 = visualRect(index)，重摆触发
 * = horizontalHeader::geometriesChanged → QTableView::updateGeometries
 * （qtableview.cpp:1394-1395）尾段的 editor/index-widget 重摆——拖拽
 * 换位后挂件跟随**视觉**位置。本库 visualItemRect 已改走本类
 * visualRect 虚槽（视觉序几何 + 行号列/表头带偏移 + 隐藏格空矩形），
 * init 补挂 geometriesChanged → xtw_cellWidgetRelayout 承接触发链。
 * 布局：列宽 100/150/200、行号列 40、表头带 24、行高 24 ⇒ 未拖拽列
 * 0=[40,140)；换位（视觉序 [1,2,0]）后列 0=[390,490)。 */
XQ_TEST(XTableWidget, cellWidgetFollowsVisualOrder)
{
    XTableWidget* tw = xqTw_make3x3();
    XHeaderView* hh;
    XWidget* w;
    XRect r;
    XQ_REQUIRE(tw != NULL);
    XTableView_setColumnWidth(&tw->m_base, 0, 100);
    XTableView_setColumnWidth(&tw->m_base, 1, 150);
    XTableView_setColumnWidth(&tw->m_base, 2, 200);
    XWidget_resize((XWidget*)tw, 560, 200);
    hh = XTableView_horizontalHeader(&tw->m_base);
    XQ_REQUIRE(hh != NULL);
    w = XWidget_create(NULL, 0);
    XQ_REQUIRE(w != NULL);
    XTableWidget_setCellWidget(tw, 0, 0, w);

    /* ① 未拖拽：挂件在列 0 的视觉位（行号列右缘 40、表头带下 24）。 */
    r = XWidget_geometry(w);
    XQ_EQ_I64(r.x, 40);
    XQ_EQ_I64(r.y, 24);
    XQ_EQ_I64(r.width, 100);
    XQ_EQ_I64(r.height, 24);

    /* ② moveSection(0,2)（视觉序 [1,2,0]）⇒ 挂件随逻辑 0 移到视觉末段
     * [390,490)（geometriesChanged → 挂件重摆；visualItemRect 视觉序）。 */
    XHeaderView_moveSection(hh, 0, 2);
    r = XWidget_geometry(w);
    XQ_EQ_I64(r.x, 390);
    XQ_EQ_I64(r.y, 24);
    XQ_EQ_I64(r.width, 100);

    /* ③ 拖回恒等 ⇒ 挂件回 [40,140)（回归锁）。 */
    XHeaderView_moveSection(hh, 2, 0);
    r = XWidget_geometry(w);
    XQ_EQ_I64(r.x, 40);

    /* ④ 隐藏列格：挂件藏起（visualItemRect 隐藏口径 ⇒ relayout 藏）。 */
    XTableView_setColumnHidden(&tw->m_base, 0, true);
    XQ_CHECK(XWidget_isHidden(w));
    XTableView_setColumnHidden(&tw->m_base, 0, false);
    r = XWidget_geometry(w);
    XQ_EQ_I64(r.x, 40);

    XClassDelete((XClass*)tw); /* 挂件归表（deleteLater），不手工删 */
}

/* ==================== K-73a 滚动条显隐重推（2026-11-14） ==================== */

/* 对齐依据：Qt 里滚动区布局改变视口几何 ⇒ QTableView::updateGeometries
 * 以**视口几何**为事实源摆表头（qtableview.cpp:2301,2309），表头几何变
 * ⇒ Resize ⇒ resizeSections（qheaderview.cpp:2817-2827）。本库滚动条
 * 显隐（策略切换）改视口宽但**不带表格 resize、不带内容尺寸变化**——
 * 既有四处推送（init/resizeEvent/行号列显隐/内容尺寸）都覆盖不到；
 * 现由视口 Resize 事件经 EXAbstractScrollArea_ViewportEvent 覆写承接，
 * XHeaderView_syncViewportLength 同值闸保证仅长度真变时重排。 */
XQ_TEST(XTableWidget, stretchLastSectionScrollbarResync)
{
    XTableWidget* tw = XTableWidget_create(NULL, 0);
    XHeaderView* hh;
    int availBefore;
    int availAfter;
    XQ_REQUIRE(tw != NULL);
    XTableWidget_setColumnCount(tw, 3);
    XTableView_setColumnWidth(&tw->m_base, 0, 100);
    XTableView_setColumnWidth(&tw->m_base, 1, 150);
    XTableView_setColumnWidth(&tw->m_base, 2, 200);
    /* 30 行 ⇒ 内容高 > 视口 ⇒ AsNeeded 竖条显形（测试宿主从不 show
     * 控件树 ⇒ 用显式隐藏位判条显隐，isVisible 是父链生效可见、
     * 未 show 的顶层恒 false）。 */
    XTableWidget_setRowCount(tw, 30);
    XWidget_resize((XWidget*)tw, 900, 200);
    hh = XTableView_horizontalHeader(&tw->m_base);
    XQ_REQUIRE(hh != NULL);
    XQ_CHECK(!XWidget_isHidden((XWidget*)XAbstractScrollArea_verticalScrollBar(
        (XAbstractScrollArea*)tw)));
    XHeaderView_setStretchLastSection(hh, true);
    availBefore = XWidget_width(XAbstractScrollArea_viewport(
                  (XAbstractScrollArea*)tw)) - 40;
    XQ_EQ_I64(XTableView_columnWidth(&tw->m_base, 2), availBefore - 250);

    /* 策略改 AlwaysOff：条隐 ⇒ 视口加宽一个条宽 ⇒ 只有**视口 Resize**
     * 事件（无表格 resize、无内容尺寸变化）⇒ 视口事件钩重推 ⇒ 末段
     * 回吃释放的条宽。若无 K-73a 钩，末段滞留旧值（差一个条宽）。 */
    XAbstractScrollArea_setVerticalScrollBarPolicy(
        (XAbstractScrollArea*)tw, XScrollBarPolicy_AlwaysOff);
    XQ_CHECK(XWidget_isHidden((XWidget*)XAbstractScrollArea_verticalScrollBar(
        (XAbstractScrollArea*)tw)));
    availAfter = XWidget_width(XAbstractScrollArea_viewport(
                 (XAbstractScrollArea*)tw)) - 40;
    XQ_CHECK(availAfter > availBefore); /* 视口确实加宽（条宽释放）。 */
    XQ_EQ_I64(XTableView_columnWidth(&tw->m_base, 2), availAfter - 250);
    XClassDelete((XClass*)tw);
}

/* ==================== K-68 表格体视觉序管线（2026-11-14） ==================== */

/* 对齐依据：Qt 6.8.3——
 * ① QTableView::paintEvent 列遍历：外层视觉序 visualColumnIndex +
 *    horizontalHeader->logicalIndex 换算 + columnViewportPosition(logical)
 *    取格几何（qtableview.cpp:1600,1610-1611）；
 * ② QHeaderView::sectionPosition(logical) =
 *    headerSectionPosition(visualIndex(logical))，recalcSectionStartPos 按
 *    视觉序累加（qheaderview.cpp:625-633/3890-3898）；
 * ③ 命中 logicalIndexAt = visualIndexAt + logicalIndex（qheaderview.cpp:
 *    585-591）——屏幕位置跟随视觉序、返回逻辑列；
 * ④ initStyleOptionForIndex 的 position 按视觉序首末可见段
 *    （isFirstVisibleSection/isLastVisibleSection(visual)，qheaderview.cpp:
 *    3448-3462）。
 * 本库映射事实源 = 表头 m_visualToLogical（K-68 表头层 moveSection），
 * 经 xtw_logicalToVisual/xtw_visualToLogical 唯一入口；列宽事实源仍是
 * m_colWidths（按逻辑索引）。缺省行号列宽 40、表头带高 24、行高 24；
 * 列宽 100/150/200 ⇒ 未拖拽列 0=[40,140)、列 1=[140,290)、列 2=[290,490)。 */
XQ_TEST(XTableWidget, visualOrderFollowsMoveSection)
{
    XTableWidget* tw = XTableWidget_create(NULL, 0);
    XHeaderView* hh;
    XTableHeaderCaptureStyle* style;
    XImage* img;
    int r;
    int c;
    XQ_REQUIRE(tw != NULL);
    XTableWidget_setRowCount(tw, 3);
    XTableWidget_setColumnCount(tw, 3);
    XTableView_setColumnWidth(&tw->m_base, 0, 100);
    XTableView_setColumnWidth(&tw->m_base, 1, 150);
    XTableView_setColumnWidth(&tw->m_base, 2, 200);
    XWidget_resize((XWidget*)tw, 560, 200);
    hh = XTableView_horizontalHeader(&tw->m_base);
    XQ_REQUIRE(hh != NULL);
    /* 空文本条目 + 三列三色背景：背景像素即「列身份」探针（行 0 中心
     * y=36 不落文本基线 40、不落网格线 47/71）。 */
    for (r = 0; r < 3; ++r) {
        for (c = 0; c < 3; ++c) {
            XTableWidgetItem* it;
            XColor color;
            it = XTableWidgetItem_create_utf8("", XTableWidgetItem_Type);
            XQ_REQUIRE(it != NULL);
            XTableWidget_setItem(tw, r, c, it);
            if (c == 0) color = XColor_create_rgb(255, 0, 0, 255);
            else if (c == 1) color = XColor_create_rgb(0, 255, 0, 255);
            else color = XColor_create_rgb(0, 0, 255, 255);
            XTableWidgetItem_setBackground(it, &color);
        }
    }
    style = xqTwHdr_createCaptureStyle();
    XQ_REQUIRE(style != NULL);
    XWidget_setStyle((XWidget*)tw, (XStyle*)style);

    /* ① 未拖拽基准（映射恒等 ⇒ 与改前逐位一致）：命中按逻辑序。 */
    XQ_CHECK(XTableWidget_itemAt(tw, 90, 36) == XTableWidget_item(tw, 0, 0));
    XQ_CHECK(XTableWidget_itemAt(tw, 215, 36) == XTableWidget_item(tw, 0, 1));
    XQ_CHECK(XTableWidget_itemAt(tw, 390, 36) == XTableWidget_item(tw, 0, 2));
    /* 边界：段左闭右开（与改前 cellAt 同式）。 */
    XQ_CHECK(XTableWidget_itemAt(tw, 140, 36) == XTableWidget_item(tw, 0, 1));
    XQ_CHECK(XTableWidget_itemAt(tw, 289, 36) == XTableWidget_item(tw, 0, 1));

    /* ② 未拖拽绘制基准：背景像素 = 逻辑列颜色；position = 逻辑首/末。 */
    memset(g_xqTwHdrSeen, 0, sizeof(g_xqTwHdrSeen));
    memset(g_xqTwHdrPos, 0, sizeof(g_xqTwHdrPos));
    memset(g_xqTwHdrPosSeen, 0, sizeof(g_xqTwHdrPosSeen));
    img = XWidget_grab((XWidget*)tw);
    XQ_REQUIRE(img != NULL);
    XQ_EQ_I64(XImage_pixel(img, 90, 36), 0xFFFF0000u);  /* 列0 红 */
    XQ_EQ_I64(XImage_pixel(img, 215, 36), 0xFF00FF00u); /* 列1 绿 */
    XQ_EQ_I64(XImage_pixel(img, 390, 36), 0xFF0000FFu); /* 列2 蓝 */
    XClassDelete((XClass*)img);
    XQ_CHECK(g_xqTwHdrPosSeen[0] && g_xqTwHdrPosSeen[1] && g_xqTwHdrPosSeen[2]);
    XQ_EQ_I64(g_xqTwHdrPos[0], XStyleOptionHeaderSectionPosition_Beginning);
    XQ_EQ_I64(g_xqTwHdrPos[1], XStyleOptionHeaderSectionPosition_Middle);
    XQ_EQ_I64(g_xqTwHdrPos[2], XStyleOptionHeaderSectionPosition_End);

    /* ③ 程序化换位：moveSection(0,2)（参数=视觉序）⇒ 视觉序 [1,2,0]
     * （与 XHeaderViewAlignTest.moveSectionsAndFlag 同款载荷）；
     * 列宽事实源不被搬动（K-68 契约：尺寸/隐藏按逻辑段存放）。 */
    XHeaderView_moveSection(hh, 0, 2);
    XQ_EQ_I64(XHeaderView_visualIndex(hh, 1), 0);
    XQ_EQ_I64(XHeaderView_visualIndex(hh, 2), 1);
    XQ_EQ_I64(XHeaderView_visualIndex(hh, 0), 2);
    XQ_EQ_I64(XTableView_columnWidth(&tw->m_base, 0), 100);
    XQ_EQ_I64(XTableView_columnWidth(&tw->m_base, 1), 150);
    XQ_EQ_I64(XTableView_columnWidth(&tw->m_base, 2), 200);

    /* ④ 命中跟随视觉位置：视觉0=[40,190)=逻辑1、视觉1=[190,390)=逻辑2、
     * 视觉2=[390,490)=逻辑0；返回值是逻辑列（数据/信号按逻辑索引）。 */
    XQ_CHECK(XTableWidget_itemAt(tw, 100, 36) == XTableWidget_item(tw, 0, 1));
    XQ_CHECK(XTableWidget_itemAt(tw, 290, 36) == XTableWidget_item(tw, 0, 2));
    XQ_CHECK(XTableWidget_itemAt(tw, 440, 36) == XTableWidget_item(tw, 0, 0));
    /* 边界：旧列 0 区间 [40,140) 现整段属于逻辑 1（视觉序重排后宽 150）。 */
    XQ_CHECK(XTableWidget_itemAt(tw, 130, 36) == XTableWidget_item(tw, 0, 1));
    XQ_CHECK(XTableWidget_itemAt(tw, 189, 36) == XTableWidget_item(tw, 0, 1));
    XQ_CHECK(XTableWidget_itemAt(tw, 190, 36) == XTableWidget_item(tw, 0, 2));

    /* ⑤ 绘制像素序 = 视觉序（离屏）：列身份探针跟视觉位置走。 */
    memset(g_xqTwHdrSeen, 0, sizeof(g_xqTwHdrSeen));
    memset(g_xqTwHdrPos, 0, sizeof(g_xqTwHdrPos));
    memset(g_xqTwHdrPosSeen, 0, sizeof(g_xqTwHdrPosSeen));
    img = XWidget_grab((XWidget*)tw);
    XQ_REQUIRE(img != NULL);
    XQ_EQ_I64(XImage_pixel(img, 115, 36), 0xFF00FF00u); /* 视觉0→逻辑1 绿 */
    XQ_EQ_I64(XImage_pixel(img, 290, 36), 0xFF0000FFu); /* 视觉1→逻辑2 蓝 */
    XQ_EQ_I64(XImage_pixel(img, 440, 36), 0xFFFF0000u); /* 视觉2→逻辑0 红 */
    XClassDelete((XClass*)img);
    /* position 跟随视觉首末（Qt isFirstVisibleSection(visual) 口径）：
     * 视觉首=逻辑1 Beginning、视觉末=逻辑0 End、逻辑2 落 Middle。 */
    XQ_CHECK(g_xqTwHdrPosSeen[0] && g_xqTwHdrPosSeen[1] && g_xqTwHdrPosSeen[2]);
    XQ_EQ_I64(g_xqTwHdrPos[1], XStyleOptionHeaderSectionPosition_Beginning);
    XQ_EQ_I64(g_xqTwHdrPos[2], XStyleOptionHeaderSectionPosition_Middle);
    XQ_EQ_I64(g_xqTwHdrPos[0], XStyleOptionHeaderSectionPosition_End);

    /* ⑥ 拖回恒等：moveSection(2,0) ⇒ 映射恢复 [0,1,2]（表已建但恒等）
     * ⇒ 命中回原位——映射表存在时的恒等行为与改前逐位一致（回归锁）。 */
    XHeaderView_moveSection(hh, 2, 0);
    XQ_EQ_I64(XHeaderView_visualIndex(hh, 0), 0);
    XQ_EQ_I64(XHeaderView_visualIndex(hh, 1), 1);
    XQ_EQ_I64(XHeaderView_visualIndex(hh, 2), 2);
    XQ_CHECK(XTableWidget_itemAt(tw, 90, 36) == XTableWidget_item(tw, 0, 0));
    XQ_CHECK(XTableWidget_itemAt(tw, 215, 36) == XTableWidget_item(tw, 0, 1));
    XQ_CHECK(XTableWidget_itemAt(tw, 390, 36) == XTableWidget_item(tw, 0, 2));

    XWidget_setStyle((XWidget*)tw, NULL);
    XClassDelete((XClass*)style);
    XClassDelete((XClass*)tw);
}

/* ==================== K-68 合并区间吸附跟随视觉序（2026-11-14 复核②） ==================== */

/* 对齐依据：Qt 6.8.3 QTableView::indexAt 的 span 吸附段（qtableview.cpp:
 * 1695-1710，反查在 1702-1706）——d->span(r,c) 把覆盖格反查回
 * span.top()/left()，吸附是**纯数据**反查（span 表按逻辑行列存储，与
 * 几何无关）。本表吸附此前经 XTableView_indexAt 二次几何命中，其
 * columnAt 当时按逻辑序累加列宽、无视觉映射 ⇒ 拖拽后覆盖格吸附到
 * 错误原点。现改走 xtw_spanOrigin（xtw_cellAt 命中的逻辑格直接反查
 * 本表 span 表）；裸视图 columnAt 的视觉序映射已由 K-68 残留 a 补齐
 * （XTableView.c:1280-1314），吸附口径不受影响。
 * 布局：列宽 100/150/200；setSpan(0,1,1,2) 合并行 0 列 1..2（原点 (0,1)）；
 * 未拖拽列 0=[40,140)/列 1=[140,290)/列 2=[290,490)，拖拽后（视觉序
 * [1,2,0]）视觉 0=[40,190)=逻辑 1、视觉 1=[190,390)=逻辑 2。 */
XQ_TEST(XTableWidget, spanSnapFollowsVisualOrder)
{
    XTableWidget* tw = XTableWidget_create(NULL, 0);
    XHeaderView* hh;
    XQ_REQUIRE(tw != NULL);
    XTableWidget_setRowCount(tw, 3);
    XTableWidget_setColumnCount(tw, 3);
    XTableView_setColumnWidth(&tw->m_base, 0, 100);
    XTableView_setColumnWidth(&tw->m_base, 1, 150);
    XTableView_setColumnWidth(&tw->m_base, 2, 200);
    XWidget_resize((XWidget*)tw, 560, 200);
    hh = XTableView_horizontalHeader(&tw->m_base);
    XQ_REQUIRE(hh != NULL);
    /* 行 0 有条目（吸附目标），行 1 留空作对照。 */
    XTableWidget_setItem(tw, 0, 0,
        XTableWidgetItem_create_utf8("A", XTableWidgetItem_Type));
    XTableWidget_setItem(tw, 0, 1,
        XTableWidgetItem_create_utf8("B", XTableWidgetItem_Type));
    XTableWidget_setItem(tw, 0, 2,
        XTableWidgetItem_create_utf8("C", XTableWidgetItem_Type));
    XTableView_setSpan(&tw->m_base, 0, 1, 1, 2);
    XQ_EQ_I64(XTableView_columnSpan(&tw->m_base, 0, 1), 2);

    /* ① 未拖拽：原点格与覆盖格都吸附回 (0,1)（与改前行为一致）。 */
    XQ_CHECK(XTableWidget_itemAt(tw, 90, 36) == XTableWidget_item(tw, 0, 0));
    XQ_CHECK(XTableWidget_itemAt(tw, 215, 36) == XTableWidget_item(tw, 0, 1));
    XQ_CHECK(XTableWidget_itemAt(tw, 390, 36) == XTableWidget_item(tw, 0, 1));

    /* ② moveSection(0,2) ⇒ 视觉序 [1,2,0]：视觉 0=[40,190)=逻辑 1（span
     * 原点格）、视觉 1=[190,390)=逻辑 2（覆盖格）——两处命中都吸附回
     * 原点 (0,1)。改前经基类 indexAt 时 x=100 命中逻辑列 0（非覆盖格）
     * ⇒ 吸附成 (0,0)，正是复核②的错位路径。 */
    XHeaderView_moveSection(hh, 0, 2);
    XQ_EQ_I64(XHeaderView_visualIndex(hh, 1), 0);
    XQ_CHECK(XTableWidget_itemAt(tw, 100, 36) == XTableWidget_item(tw, 0, 1));
    XQ_CHECK(XTableWidget_itemAt(tw, 290, 36) == XTableWidget_item(tw, 0, 1));
    /* 非 span 列照常直返：视觉 2=[390,490)=逻辑 0 → 条目 A。 */
    XQ_CHECK(XTableWidget_itemAt(tw, 440, 36) == XTableWidget_item(tw, 0, 0));

    /* ③ 拖回恒等 ⇒ 吸附回未拖拽口径。 */
    XHeaderView_moveSection(hh, 2, 0);
    XQ_CHECK(XTableWidget_itemAt(tw, 215, 36) == XTableWidget_item(tw, 0, 1));
    XQ_CHECK(XTableWidget_itemAt(tw, 390, 36) == XTableWidget_item(tw, 0, 1));

    XClassDelete((XClass*)tw);
}

#endif /* XGUI_ON && XWIDGET_ON && XTABLEWIDGET_ON */
