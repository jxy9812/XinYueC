/**
 * @file       XListWidgetAlignTest.c
 * @brief      XListWidget × Qt 6.8 QListWidget 对齐契约测试。
 * @details    对齐依据：
 *             - 头文件 /opt/Qt/6.8.3/gcc_64/include/QtWidgets/qlistwidget.h
 *               （addItem/insertItem/addItems/insertItems/item/takeItem/
 *               count/currentItem/currentRow/setCurrentRow/itemAt/
 *               visualItemRect/sortItems/setSortingEnabled/findItems/
 *               itemWidget/setItemWidget/removeItemWidget/
 *               openPersistentEditor/closePersistentEditor/
 *               isPersistentEditorOpen 与 itemXxx 信号族）；
 *             - 本机 Qt 6.8.0 实测：① sortItems(desc) 后
 *               isSortingEnabled() 仍为 false（只排一次，不隐式使能）；
 *               ② setSortingEnabled(true) 不重排既有条目（b,a,c 保持），
 *               此后 addItem 才落位到排序位置；③ setCurrentRow(-1) 同时
 *               清除当前项**与选中集合**；④ setCurrentRow(越界) 被忽略。
 *
 *             覆盖：缺省值、条目增删（含行内插入与取出）、当前行/选中
 *             联动、排序（只排一次 / 使能只影响后续插入）、几何
 *             （itemAt/visualItemRect 同行高口径）、持久编辑器三件套、
 *             行级部件挂载、父子所有权。
 * @note       C99；控件/文本副本全部释放，漏删即运行器判 `[LEAK]`。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

#if XGUI_ON && XWIDGET_ON && XTABLEWIDGET_ON

#include "XListWidget.h"
#include "XAbstractItemModel.h"
#include "XItemSelectionModel.h"
#include "XString.h"
#include "XWidget.h"

/** @brief currentRowChanged 单 int 载荷探针（对齐 Qt qlistwidget.h:242）。 */
static int g_lwRowChangedHits;
static int g_lwRowChangedRow;

static void xqLw_rowChangedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    ++g_lwRowChangedHits;
    if (!args) return;
    XVarList_args_1(args, int, row);
    g_lwRowChangedRow = row;
}

/* ==================== 用例 ==================== */

/** @brief 缺省值与条目增删（addItem/insertItem/takeItem/clear）。 */
XQ_TEST(XListWidget, itemsAndDefaults)
{
    XListWidget* lw = XListWidget_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                            NULL, 0);
    XString* taken;
    XQ_REQUIRE(lw != NULL);

    XQ_EQ_I64(XListWidget_count(lw), 0);
    XQ_EQ_I64(XListWidget_currentItem(lw), -1);
    XQ_EQ_I64(XListWidget_currentRow(lw), -1);
    XQ_CHECK(!XListWidget_isSortingEnabled(lw));
    XQ_CHECK_PTR(XListWidget_model(lw)); /* 内建模型（对标 QListWidget::model）。 */

    /* addItem：返回新行号（Qt addItem 在末尾插入）。 */
    XQ_EQ_I64(XListWidget_addItem_2(lw, "甲"), 0);
    XQ_EQ_I64(XListWidget_addItem_2(lw, "乙"), 1);
    XQ_EQ_I64(XListWidget_count(lw), 2);
    XQ_EQ_STR(XListWidget_item_2(lw, 1), "乙");
    XQ_CHECK_PTR(XListWidget_item(lw, 1));
    XQ_CHECK(XListWidget_item(lw, 9) == NULL);
    XQ_EQ_STR(XListWidget_item_2(lw, 9), ""); /* 越界返回空串，同库内口径。 */

    /* insertItem 行内插入：原行及其后整体后移；负行最前、超界追加（Qt 同）。 */
    XQ_EQ_I64(XListWidget_insertItem_2(lw, 1, "插入"), 1);
    XQ_EQ_STR(XListWidget_item_2(lw, 1), "插入");
    XQ_EQ_STR(XListWidget_item_2(lw, 2), "乙");
    XQ_EQ_I64(XListWidget_insertItem_2(lw, -5, "置顶"), 0);
    XQ_EQ_STR(XListWidget_item_2(lw, 0), "置顶");
    XQ_EQ_I64(XListWidget_insertItem_2(lw, 99, "末尾"), 4);
    XQ_EQ_I64(XListWidget_count(lw), 5);

    /* item_new：独立副本（调用方释放）；row：首个精确命中。 */
    {
        XString* copy = XListWidget_item_new(lw, 0);
        XQ_REQUIRE(copy != NULL);
        XQ_EQ_STR(XString_toUtf8(copy), "置顶");
        XClassDelete((XClass*)copy);
    }
    XQ_EQ_I64(XListWidget_row(lw, "插入"), 2);
    XQ_EQ_I64(XListWidget_row(lw, "无此"), -1);

    /* takeItem：移除行并归还文本副本；其后条目前移。
       行序此时为 置顶/甲/插入/乙/末尾 ⇒ 取出行 1（甲）。 */
    taken = XListWidget_takeItem(lw, 1);
    XQ_REQUIRE(taken != NULL);
    XQ_EQ_STR(XString_toUtf8(taken), "甲");
    XClassDelete((XClass*)taken);
    XQ_EQ_I64(XListWidget_count(lw), 4);
    XQ_EQ_STR(XListWidget_item_2(lw, 1), "插入");
    XQ_CHECK(XListWidget_takeItem(lw, 99) == NULL);

    /* clear：条目与选中一起清空（Qt clear 移除全部条目）。 */
    XListWidget_setCurrentRow(lw, 1);
    XListWidget_clear(lw);
    XQ_EQ_I64(XListWidget_count(lw), 0);
    XQ_EQ_I64(XListWidget_currentRow(lw), -1);

    XClassDelete((XClass*)lw);
}

/** @brief 当前行/选中联动与几何（itemAt/visualItemRect 同行高口径）。 */
XQ_TEST(XListWidget, currentRowSelectionAndGeometry)
{
    XListWidget* lw = XListWidget_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                            NULL, 0);
    XItemSelectionModel* sm;
    XRect r;
    int hits[4];
    XQ_REQUIRE(lw != NULL);

    XWidget_resize((XWidget*)lw, 160, 120);
    XListWidget_addItems(lw, (const char* const[]){ "r0", "r1", "r2", NULL }, 3);
    XQ_EQ_I64(XListWidget_count(lw), 3);
    sm = XAbstractItemView_selectionModel(&lw->m_base.m_base);
    XQ_REQUIRE(sm != NULL);

    /* setCurrentRow：缺省 SingleSelection ⇒ ClearAndSelect（选中当前行）。 */
    XListWidget_setCurrentRow(lw, 1);
    XQ_EQ_I64(XListWidget_currentRow(lw), 1);
    XQ_EQ_I64(XListWidget_currentItem(lw), 1);
    XQ_CHECK(XItemSelectionModel_isSelected(sm, 1, 0));
    {
        int sel[4];
        XQ_EQ_I64(XListWidget_selectedItems(lw, sel, 4), 1);
        XQ_EQ_I64(sel[0], 1);
    }

    /* setCurrentRow(-1)：清当前项 **且清选中**（Qt 6.8.0 实测）。 */
    XListWidget_setCurrentRow(lw, -1);
    XQ_EQ_I64(XListWidget_currentRow(lw), -1);
    XQ_EQ_I64(XListWidget_currentItem(lw), -1);
    XQ_CHECK(!XItemSelectionModel_isSelected(sm, 1, 0));
    XQ_EQ_I64(XListWidget_selectedItems(lw, hits, 4), 0);

    /* setCurrentRow(越界)：**无效索引** ⇒ 清当前项与选中（Qt 6.8.0 实测
       setCurrentRow(99) 后 currentRow()==-1、选中清空、currentRowChanged(-1)；
       旧断言「越界忽略」已按 Qt 收敛）。 */
    XListWidget_setCurrentRow(lw, 0);
    XListWidget_setCurrentRow(lw, 99);
    XQ_EQ_I64(XListWidget_currentRow(lw), -1);
    XQ_EQ_I64(XListWidget_currentItem(lw), -1);
    XQ_CHECK(!XItemSelectionModel_isSelected(sm, 0, 0));

    /* 几何：行高 24、表头不占行带；越界返回全零矩形。 */
    XQ_EQ_I64(XListWidget_itemAt(lw, 10, 12), 0);
    XQ_EQ_I64(XListWidget_itemAt(lw, 10, 30), 1);
    XQ_EQ_I64(XListWidget_itemAt(lw, 10, -1), -1);
    r = XListWidget_visualItemRect(lw, 1);
    XQ_CHECK(r.y == 24 && r.height == 24 && r.width > 0);
    r = XListWidget_visualItemRect(lw, 99);
    XQ_CHECK(r.x == 0 && r.y == 0 && r.width == 0 && r.height == 0);

    /* 查找：精确/包含（区分大小写）。 */
    XQ_EQ_I64(XListWidget_findItems(lw, "r1", 1, hits, 4), 1);
    XQ_EQ_I64(hits[0], 1);
    XQ_EQ_I64(XListWidget_findItems(lw, "r", 0, hits, 4), 3);

    XClassDelete((XClass*)lw);
}

/** @brief 排序：sortItems 只排一次不改使能；使能只影响后续插入（Qt 实测）。 */
XQ_TEST(XListWidget, sortingSemantics)
{
    XListWidget* lw = XListWidget_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                            NULL, 0);
    XQ_REQUIRE(lw != NULL);

    XListWidget_addItems(lw, (const char* const[]){ "b", "a", "c", NULL }, 3);

    /* sortItems(0)：升序重排，**不**打开 sortingEnabled（Qt 6.8.0 实测
       sortItems 后 isSortingEnabled() 仍 false）。 */
    XListWidget_sortItems(lw, 0);
    XQ_EQ_STR(XListWidget_item_2(lw, 0), "a");
    XQ_EQ_STR(XListWidget_item_2(lw, 2), "c");
    XQ_CHECK(!XListWidget_isSortingEnabled(lw));

    /* sortItems(1)：降序重排，同样不改使能。 */
    XListWidget_sortItems(lw, 1);
    XQ_EQ_STR(XListWidget_item_2(lw, 0), "c");
    XQ_EQ_STR(XListWidget_item_2(lw, 2), "a");
    XQ_CHECK(!XListWidget_isSortingEnabled(lw));

    /* 使能开启不重排既有条目（Qt 实测 b,a,c 上开启后次序不变）。 */
    XListWidget_sortItems(lw, 0);            /* 先排成 a,b,c。 */
    XListWidget_setSortingEnabled(lw, true);
    XQ_CHECK(XListWidget_isSortingEnabled(lw));
    XQ_EQ_STR(XListWidget_item_2(lw, 0), "a");
    XQ_EQ_STR(XListWidget_item_2(lw, 1), "b");
    XQ_EQ_STR(XListWidget_item_2(lw, 2), "c");

    /* 使能下插入才落位（最近排序方向 = 升序 ⇒ "aa" 落在 "a" 之后）。 */
    XListWidget_addItem_2(lw, "aa");
    XQ_EQ_I64(XListWidget_count(lw), 4);
    XQ_EQ_STR(XListWidget_item_2(lw, 0), "a");
    XQ_EQ_STR(XListWidget_item_2(lw, 1), "aa");
    XQ_EQ_STR(XListWidget_item_2(lw, 3), "c");

    /* 关闭后插入追加到末尾（不落位）。 */
    XListWidget_setSortingEnabled(lw, false);
    XQ_CHECK(!XListWidget_isSortingEnabled(lw));
    XListWidget_addItem_2(lw, "0z");
    XQ_EQ_STR(XListWidget_item_2(lw, XListWidget_count(lw) - 1), "0z");

    XClassDelete((XClass*)lw);
}

/** @brief 持久编辑器三件套、行级部件挂载与父子所有权。 */
XQ_TEST(XListWidget, persistentEditorWidgetsAndOwnership)
{
    XWidget* host = XWidget_create(NULL, 0);
    XListWidget* lw;
    XWidget* cell = NULL;
    XQ_REQUIRE(host != NULL);

    lw = XListWidget_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, host, 0);
    XQ_REQUIRE(lw != NULL);
    XQ_CHECK(XWidget_parentWidget((XWidget*)lw) == host);
    XListWidget_addItems(lw, (const char* const[]){ "x", "y", NULL }, 2);

    /* 持久编辑器（对标 QListWidget 同名三件套；行模型恒列 0）。 */
    XQ_CHECK(!XListWidget_isPersistentEditorOpen(lw, 0));
    XListWidget_openPersistentEditor(lw, 0);
    XQ_CHECK(XListWidget_isPersistentEditorOpen(lw, 0));
    XQ_CHECK(!XListWidget_isPersistentEditorOpen(lw, 1));
    XListWidget_closePersistentEditor(lw, 0);
    XQ_CHECK(!XListWidget_isPersistentEditorOpen(lw, 0));
    /* 越界行安全（不开、不崩）。 */
    XListWidget_openPersistentEditor(lw, 9);
    XQ_CHECK(!XListWidget_isPersistentEditorOpen(lw, 9));

    /* 行级部件挂载：借用语义（列表不删除部件，调用方释放）。 */
    cell = XWidget_create(host, 0);
    XQ_REQUIRE(cell != NULL);
    XListWidget_setItemWidget(lw, 0, cell);
    XQ_CHECK(XListWidget_itemWidget(lw, 0) == cell);
    XQ_CHECK(XListWidget_itemWidget(lw, 1) == NULL);
    XListWidget_removeItemWidget(lw, 0);
    XQ_CHECK(XListWidget_itemWidget(lw, 0) == NULL);

    /* 子控件随 host 析构（列表 + 挂载部件），漏删即判 [LEAK]。 */
    XClassDelete((XClass*)host);
}

/** @brief currentRowChanged 单 int 载荷与 setCurrentRow(row, command) 重载
 *         （gap 146/147）。 */
XQ_TEST(XListWidget, currentRowChangedPayloadAndFlags)
{
    XListWidget* lw = XListWidget_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL, 0);
    XItemSelectionModel* sm;
    XQ_REQUIRE(lw != NULL);

    XListWidget_addItems(lw, (const char* const[]){ "r0", "r1", "r2", NULL }, 3);
    sm = XAbstractItemView_selectionModel(&lw->m_base.m_base);
    XQ_REQUIRE(sm != NULL);
    g_lwRowChangedHits = 0;
    g_lwRowChangedRow = -9;
    XObject_connect_1((XObject*)lw,
                      XSignal(XListWidget_currentRowChanged_signal),
                      (XObject*)lw, xqLw_rowChangedSlot,
                      XConnectionType_Direct);

    /* 单载荷：Qt currentRowChanged(int currentRow)（qlistwidget.h:242）。
       旧口径曾带 (current,previous) 两载荷，已按 Qt 收敛。 */
    XListWidget_setCurrentRow(lw, 1);
    XQ_EQ_I64(g_lwRowChangedHits, 1);
    XQ_EQ_I64(g_lwRowChangedRow, 1);
    /* 无效行同样发射，载荷 -1（Qt 实测 setCurrentRow(-1)/越界均发 -1）；
       当前行已经是 -1 时再次设无效行不再发射（Qt setCurrentIndex 对同
       索引早退，只施加选择命令）。 */
    XListWidget_setCurrentRow(lw, -1);
    XQ_EQ_I64(g_lwRowChangedHits, 2);
    XQ_EQ_I64(g_lwRowChangedRow, -1);
    XListWidget_setCurrentRow(lw, 99);
    XQ_EQ_I64(g_lwRowChangedHits, 2);
    XQ_EQ_I64(g_lwRowChangedRow, -1);

    /* setCurrentRow_2：显式选择命令直落选择模型。
       NoUpdate 只移当前项、不动选区；ClearAndSelect 先清再选。 */
    XListWidget_setCurrentRow_2(lw, 0,
                                XItemSelectionModelSelectionFlag_NoUpdate);
    XQ_EQ_I64(XListWidget_currentRow(lw), 0);
    XQ_CHECK(!XItemSelectionModel_isSelected(sm, 0, 0));
    XQ_EQ_I64(g_lwRowChangedHits, 3);
    XQ_EQ_I64(g_lwRowChangedRow, 0);
    XListWidget_setCurrentRow_2(
        lw, 1, XItemSelectionModelSelectionFlag_ClearAndSelect);
    XQ_EQ_I64(XListWidget_currentRow(lw), 1);
    XQ_CHECK(XItemSelectionModel_isSelected(sm, 1, 0));
    {
        int sel[4];
        XQ_EQ_I64(XListWidget_selectedItems(lw, sel, 4), 1);
        XQ_EQ_I64(sel[0], 1);
    }

    /* setCurrentItem(负值/越界)=无效条目 ⇒ 清当前项（Qt setCurrentItem(nullptr)）。 */
    XListWidget_setCurrentItem(lw, -1);
    XQ_EQ_I64(XListWidget_currentRow(lw), -1);
    XQ_EQ_I64(XListWidget_currentItem(lw), -1);

    XClassDelete((XClass*)lw);
}

#endif /* XGUI_ON && XWIDGET_ON && XTABLEWIDGET_ON */
