/**
 * @file       XTreeWidgetAlignTest.c
 * @brief      XTreeWidget × Qt 6.8 QTreeWidget 对齐契约测试。
 * @details    对齐依据：
 *             - 头文件 /opt/Qt/6.8.3/gcc_64/include/QtWidgets/qtreewidget.h
 *               （columnCount/setColumnCount、topLevelItem/insertTopLevelItem/
 *               addTopLevelItem/takeTopLevelItem/indexOfTopLevelItem、
 *               setHeaderItem/setHeaderLabels/setHeaderLabel、headerItem、
 *               currentColumn/setCurrentItem、itemAt/visualItemRect、
 *               sortItems/sortColumn、editItem、openPersistentEditor/
 *               closePersistentEditor/isPersistentEditorOpen、
 *               itemWidget/setItemWidget/removeItemWidget、
 *               expandItem/collapseItem 与 QTreeWidgetItem 的
 *               columnCount/indexOfChild/setExpanded/isExpanded）；
 *             - 本机 Qt 6.8.0 实测：① 缺省 columnCount=1、currentColumn=-1、
 *               sortColumn=0（本库以 -1 表「未排序」，差异见 gaps）；
 *               ② headerItem()/invisibleRootItem() 构造即存在；
 *               ③ **expandItem 对无子条目的叶子条目也置位**
 *               （QTreeWidgetItem::isExpanded()==true）；④ setCurrentItem(item)
 *               后 currentColumn()==0；⑤ clear() 后 currentColumn() 回到 -1。
 *
 *             覆盖：缺省值、条目增删与文本列、当前行/当前列、展开/折叠
 *             （含叶子行置位）、持久编辑器三件套、几何（itemAt 含表头带 /
 *             visualItemRect 内容坐标）、表头文本族、排序、父子所有权。
 * @note       C99；控件/条目/文本全部释放（条目随控件析构），漏删即
 *             运行器判 `[LEAK]`。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

#if XGUI_ON && XWIDGET_ON && XTABLEWIDGET_ON

#include "XTreeWidget.h"
#include "XAbstractItemView.h"
#include "XAbstractItemModel.h"
#include "XString.h"
#include "XWidget.h"

/* ==================== 信号探针 ==================== */

static int g_twExpandedHits;   /**< itemExpanded 命中次数。 */
static int g_twCollapsedHits;  /**< itemCollapsed 命中次数。 */
static int g_twExpandedRow;    /**< 最近一次 itemExpanded 行号。 */
static int g_twCurrentHits;    /**< currentItemChanged 命中次数。 */

/** @brief itemExpanded(row) 槽。 */
static void xqTw_itemExpandedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    ++g_twExpandedHits;
    if (args) {
        XVarList_args_1(args, int, row);
        g_twExpandedRow = row;
    }
}

/** @brief itemCollapsed(row) 槽。 */
static void xqTw_itemCollapsedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    ++g_twCollapsedHits;
    (void)args;
}

/** @brief currentItemChanged(current,previous) 槽。 */
static void xqTw_currentItemSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++g_twCurrentHits;
}

/* ==================== 用例 ==================== */

/** @brief 缺省值矩阵与表头/根条目（Qt 6.8.0 实测）。 */
XQ_TEST(XTreeWidget, defaults)
{
    XTreeWidget* tw = XTreeWidget_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                            NULL, 0);
    XQ_REQUIRE(tw != NULL);

    XQ_EQ_I64(XTreeWidget_columnCount(tw), 1);
    XQ_EQ_I64(XTreeWidget_topLevelItemCount(tw), 0);
    XQ_EQ_I64(XTreeWidget_currentItem(tw), -1);
    XQ_EQ_I64(XTreeWidget_currentColumn(tw), -1); /* Qt 实测：无当前条目 -1。 */
    XQ_EQ_I64(XTreeWidget_sortColumn(tw), 0);     /* Qt 缺省 0（qtreewidget.h:266）。 */
    XQ_CHECK_PTR(XTreeWidget_headerItem(tw));
    XQ_CHECK_PTR(XTreeWidget_invisibleRootItem(tw));
    XQ_EQ_STR(XTreeWidget_headerLabel(tw, 0), "");
    /* 表头即条目：列 0 标签取 headerItem()->text(0)（非子条目）。 */
    XQ_EQ_STR(XTreeWidgetItem_textAt_2(XTreeWidget_headerItem(tw), 0), "");
    XQ_EQ_I64(XTreeWidgetItem_columnCount(XTreeWidget_headerItem(tw)), 1);

    /* 不可见根条目与顶层存储同源（对标 invisibleRootItem 的 child(i) ==
       topLevelItem(i)）；对其 addChild 即成为顶层条目。 */
    {
        XTreeWidgetItem* viaRoot = XTreeWidgetItem_create_2("根挂载", NULL);
        XQ_REQUIRE(viaRoot != NULL);
        XQ_CHECK(XTreeWidgetItem_addChild(
            XTreeWidget_invisibleRootItem(tw), viaRoot));
        XQ_EQ_I64(XTreeWidget_topLevelItemCount(tw), 1);
        XQ_CHECK(XTreeWidget_invisibleRootItem(tw)->childCount == 1);
        XQ_CHECK(XTreeWidget_invisibleRootItem(tw)->children[0] == viaRoot);
    }

    XClassDelete((XClass*)tw);
}

/** @brief 条目族：文本/列/子条目/勾选与当前行/当前列。 */
XQ_TEST(XTreeWidget, itemsAndCurrent)
{
    XTreeWidget* tw = XTreeWidget_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                            NULL, 0);
    XTreeWidgetItem* top;
    XTreeWidgetItem* child;
    XQ_REQUIRE(tw != NULL);

    XWidget_resize((XWidget*)tw, 160, 120);
    top = XTreeWidgetItem_create_2("Alpha", NULL);
    child = XTreeWidgetItem_create_2("beta", NULL);
    XQ_REQUIRE(top != NULL && child != NULL);
    XQ_CHECK(XTreeWidgetItem_addChild(top, child));
    XQ_CHECK(XTreeWidget_addTopLevelItem(tw, top));
    XQ_EQ_I64(XTreeWidget_topLevelItemCount(tw), 1);
    XQ_CHECK(XTreeWidget_topLevelItem(tw, 0) == top);
    XQ_CHECK(XTreeWidget_topLevelItem(tw, 9) == NULL);
    XQ_CHECK(XTreeWidget_itemFromIndex(tw, 0) == top);
    XQ_EQ_I64(XTreeWidget_indexFromItem(tw, top), 0);
    XQ_EQ_I64(XTreeWidget_indexFromItem(tw, child), -1); /* 子条目不参与平铺行映射。 */

    /* 文本：列 0 主文本 + 列 1+ 逐列文本（QTreeWidgetItem::text(column)）。 */
    XQ_EQ_STR(XTreeWidgetItem_text_2(top), "Alpha");
    XQ_EQ_I64(XTreeWidgetItem_columnCount(top), 1);
    XTreeWidgetItem_setTextAt_2(top, 1, "第二列");
    XQ_EQ_STR(XTreeWidgetItem_textAt_2(top, 1), "第二列");
    XQ_EQ_I64(XTreeWidgetItem_columnCount(top), 2);
    /* 未写入列/负列：**UTF-8 访问返回空串**（Qt text(column) 返回空
       QString）；需要区分「无值」时用 XTreeWidgetItem_textAt 的 NULL
       （等价 Qt null QString）。旧断言锁的 NULL 已按 Qt 收敛（gap 150）。 */
    XQ_EQ_STR(XTreeWidgetItem_textAt_2(top, 9), "");
    XQ_EQ_STR(XTreeWidgetItem_textAt_2(top, -1), "");
    XQ_CHECK(XTreeWidgetItem_textAt(top, 9) == NULL);
    XQ_CHECK(XTreeWidgetItem_textAt(top, -1) == NULL);

    /* 子条目：childCount/child/indexOfChild（QTreeWidgetItem 同名 API）。 */
    XQ_EQ_I64(XTreeWidgetItem_childCount(top), 1);
    XQ_CHECK(XTreeWidgetItem_child(top, 0) == child);
    XQ_EQ_I64(XTreeWidgetItem_indexOfChild(top, child), 0);
    XQ_EQ_I64(XTreeWidgetItem_indexOfChild(top, child), 0);
    {
        XTreeWidgetItem* other = XTreeWidgetItem_create_2("别人", NULL);
        XQ_REQUIRE(other != NULL);
        XQ_EQ_I64(XTreeWidgetItem_indexOfChild(top, other), -1);
        XTreeWidgetItem_delete(other);
    }

    /* 勾选态（列 0 指示器承载）。 */
    XQ_EQ_I64(XTreeWidgetItem_checkState(top), XItemCheckState_Unchecked);
    XTreeWidgetItem_setCheckState(top, XItemCheckState_Checked);
    XQ_EQ_I64(XTreeWidgetItem_checkState(top), XItemCheckState_Checked);
    XTreeWidgetItem_setCheckState(top, 99); /* 越界钳为未选中。 */
    XQ_EQ_I64(XTreeWidgetItem_checkState(top), XItemCheckState_Unchecked);

    /* 当前行/当前列：setCurrentItem 写第 0 列（Qt 实测 currentColumn()==0）。 */
    g_twCurrentHits = 0;
    XObject_connect((XObject*)tw, XSignal(XTreeWidget_currentItemChanged_signal),
                      (XObject*)tw, xqTw_currentItemSlot,
                      XConnectionType_Direct);
    XTreeWidget_setCurrentItem(tw, 0);
    XQ_EQ_I64(XTreeWidget_currentItem(tw), 0);
    XQ_EQ_I64(XTreeWidget_currentColumn(tw), 0);
    XQ_EQ_I64(g_twCurrentHits, 1);

    /* clear：条目与当前项一起复位（Qt 实测 clear 后 currentColumn 回 -1）。 */
    XTreeWidget_clear(tw);
    XQ_EQ_I64(XTreeWidget_topLevelItemCount(tw), 0);
    XQ_EQ_I64(XTreeWidget_currentItem(tw), -1);
    XQ_EQ_I64(XTreeWidget_currentColumn(tw), -1);

    XClassDelete((XClass*)tw);
}

/** @brief 展开/折叠：叶子行同样置位（Qt 6.8.0 实测③）+ 几何口径。 */
XQ_TEST(XTreeWidget, expandCollapseAndGeometry)
{
    XTreeWidget* tw = XTreeWidget_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                            NULL, 0);
    XTreeWidgetItem* top;
    XTreeWidgetItem* leaf;
    XRect r;
    XQ_REQUIRE(tw != NULL);

    XWidget_resize((XWidget*)tw, 160, 120);
    top = XTreeWidgetItem_create_2("top", NULL);
    leaf = XTreeWidgetItem_create_2("leaf", NULL);
    XQ_REQUIRE(top != NULL && leaf != NULL);
    XQ_CHECK(XTreeWidgetItem_addChild(top, leaf)); /* top 有 1 个子条目。 */
    XQ_CHECK(XTreeWidget_addTopLevelItem(tw, top));

    XObject_connect((XObject*)tw, XSignal(XTreeWidget_itemExpanded_signal),
                      (XObject*)tw, xqTw_itemExpandedSlot,
                      XConnectionType_Direct);
    XObject_connect((XObject*)tw, XSignal(XTreeWidget_itemCollapsed_signal),
                      (XObject*)tw, xqTw_itemCollapsedSlot,
                      XConnectionType_Direct);
    g_twExpandedHits = 0;
    g_twCollapsedHits = 0;
    g_twExpandedRow = -1;

    /* **项目有意偏差（保留）**：本库新插入的顶层行缺省展开，Qt 缺省折叠
       （QTreeWidgetItem::isExpanded() 实测 false）。依据：演示页的行带几何/
       命中用例按「新行默认展开」锁定（Test/XGuiDemo/xgui_demo_apitest_views.c
       "展开便捷族（…；新行默认展开）" 段与 visualItemRect y 值断言 48/96），
       且 XTreeWidget.c 的挂载入口注释同此口径；改成折叠会整体改变这些页面
       的行带布局。已计入 intentional（gap 144）。 */
    XTreeWidget_collapseItem(tw, 0);
    XQ_EQ_I64(g_twCollapsedHits, 1);
    XTreeWidget_expandItem(tw, 0);
    XQ_EQ_I64(g_twExpandedHits, 1);
    XQ_EQ_I64(g_twExpandedRow, 0);
    XTreeWidget_expandItem(tw, 0); /* 幂等：已展开不重发。 */
    XQ_EQ_I64(g_twExpandedHits, 1);

    /* 叶子行（无子条目）同样可置位：Qt 6.8.0 实测 expandItem(叶子) 后
       QTreeWidgetItem::isExpanded()==true。本库以「先折叠再展开」观测状态
       迁移（新行缺省展开）。 */
    {
        XTreeWidgetItem* solo = XTreeWidgetItem_create_2("solo", NULL);
        XQ_REQUIRE(solo != NULL);
        XQ_CHECK(XTreeWidget_addTopLevelItem(tw, solo));
    }
    g_twExpandedHits = 0;
    g_twCollapsedHits = 0;
    XTreeWidget_collapseItem(tw, 1);   /* 叶子行折叠：置位并发射。 */
    XQ_EQ_I64(g_twCollapsedHits, 1);
    XTreeWidget_expandItem(tw, 1);     /* 叶子行展开：置位并发射。 */
    XQ_EQ_I64(g_twExpandedHits, 1);
    XQ_EQ_I64(g_twExpandedRow, 1);
    /* 越界行安全。 */
    XTreeWidget_expandItem(tw, 9);
    XQ_EQ_I64(g_twExpandedHits, 1);

    /* 几何：visualItemRect 为**内容坐标**（行 0 y=0，表头带另计）；itemAt
       按控件局部坐标（先扣 20px 表头带）。top 展开时其子树占 2 行 ⇒ 行 1
       的 y=48。 */
    r = XTreeWidget_visualItemRect(tw, 0);
    XQ_CHECK(r.x == 0 && r.y == 0 && r.height == 24 && r.width == 160);
    r = XTreeWidget_visualItemRect(tw, 1);
    XQ_EQ_I64(r.y, 48);
    XQ_CHECK(XTreeWidget_itemAt(tw, 40, 20 + 12) == 0);
    XQ_CHECK(XTreeWidget_itemAt(tw, 40, 20 + 60) == 1);
    XQ_CHECK(XTreeWidget_itemAt(tw, -1, 20 + 12) == -1);
    XQ_CHECK(XTreeWidget_itemAt(tw, 40, 5) == -1); /* 表头带内不命中行。 */
    /* 折叠 top 后子树不占位：行 1 y=24。 */
    XTreeWidget_collapseItem(tw, 0);
    r = XTreeWidget_visualItemRect(tw, 1);
    XQ_EQ_I64(r.y, 24);

    XClassDelete((XClass*)tw);
}

/** @brief 持久编辑器、表头文本族、排序与父子所有权。 */
XQ_TEST(XTreeWidget, persistentEditorsHeaderSortAndOwnership)
{
    XWidget* host = XWidget_create(NULL, 0);
    XTreeWidget* tw;
    XTreeWidgetItem* header;
    XQ_REQUIRE(host != NULL);

    tw = XTreeWidget_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, host, 0);
    XQ_REQUIRE(tw != NULL);
    XQ_CHECK(XWidget_parentWidget((XWidget*)tw) == host);
    XQ_CHECK(XWidget_isAncestorOf(host, (XWidget*)tw));

    XTreeWidget_addTopLevelItems(tw, (const char* const[]){ "b", "a", NULL },
                                 2);
    XQ_EQ_I64(XTreeWidget_topLevelItemCount(tw), 2);

    /* 持久编辑器三件套（对标 QTreeWidget(item, column) 形态）。 */
    XQ_CHECK(!XTreeWidget_isPersistentEditorOpen(tw, 0, 0));
    XTreeWidget_openPersistentEditor(tw, 0, 0);
    XQ_CHECK(XTreeWidget_isPersistentEditorOpen(tw, 0, 0));
    XQ_CHECK(!XTreeWidget_isPersistentEditorOpen(tw, 0, 1));
    XTreeWidget_closePersistentEditor(tw, 0, 0);
    XQ_CHECK(!XTreeWidget_isPersistentEditorOpen(tw, 0, 0));
    XTreeWidget_openPersistentEditor(tw, 9, 0); /* 越界安全。 */
    XQ_CHECK(!XTreeWidget_isPersistentEditorOpen(tw, 9, 0));

    /* 排序：列 0 升/降 + sortColumn 记录排序列。 */
    XTreeWidget_sortItems(tw, 0, 0);
    XQ_EQ_STR(XTreeWidgetItem_text_2(XTreeWidget_topLevelItem(tw, 0)), "a");
    XQ_EQ_I64(XTreeWidget_sortColumn(tw), 0);
    XTreeWidget_sortItems(tw, 0, 1);
    XQ_EQ_STR(XTreeWidgetItem_text_2(XTreeWidget_topLevelItem(tw, 0)), "b");
    XQ_EQ_I64(XTreeWidget_sortColumn(tw), 0);

    /* 表头文本族：批量/单条/读取 + 表头条目（gap 142 已按 Qt 收敛）。
       Qt 语义：headerItem()->text(column) 就是各列标签，列 0 是条目自身
       文本（qtreewidget.h:252）；不是子条目。setHeaderLabels 不足列数时
       增长 columnCount（qtreewidget.cpp:2818-2826）。 */
    XQ_EQ_I64(XTreeWidget_columnCount(tw), 1); /* 默认 1 列。 */
    XTreeWidget_setHeaderLabels(tw, (const char* const[]){ "列一", "列二", NULL },
                                2);
    XQ_EQ_I64(XTreeWidget_columnCount(tw), 2); /* 列数随标签增长。 */
    XQ_EQ_STR(XTreeWidget_headerLabel(tw, 0), "列一");
    XQ_EQ_STR(XTreeWidget_headerLabel(tw, 1), "列二");
    XQ_EQ_STR(XTreeWidget_headerLabel(tw, 9), ""); /* 越界安全。 */
    XTreeWidget_setHeaderLabel(tw, "唯一");
    XQ_EQ_STR(XTreeWidget_headerLabel(tw, 0), "唯一");
    {
        XTreeWidgetItem* hi = XTreeWidget_headerItem(tw);
        XQ_REQUIRE(hi != NULL);
        /* 列文本在条目**自身**（子条目数为 0）。 */
        XQ_EQ_STR(XTreeWidgetItem_textAt_2(hi, 0), "唯一");
        XQ_EQ_STR(XTreeWidgetItem_textAt_2(hi, 1), "列二");
        XQ_EQ_I64(XTreeWidgetItem_childCount(hi), 0);
    }
    /* setHeaderItem：控件接管所有权；条目**自身逐列文本**成为各列表头，
       列数按条目列数同步（Qt setHeaderItem，qtreewidget.cpp:2788-2807）。 */
    header = XTreeWidgetItem_create_2(NULL, NULL);
    XQ_REQUIRE(header != NULL);
    XTreeWidgetItem_setTextAt_2(header, 0, "表头0");
    XTreeWidgetItem_setTextAt_2(header, 1, "表头1");
    XTreeWidget_setHeaderItem(tw, header);
    XQ_CHECK(XTreeWidget_headerItem(tw) == header);
    XQ_EQ_STR(XTreeWidget_headerLabel(tw, 0), "表头0");
    XQ_EQ_STR(XTreeWidget_headerLabel(tw, 1), "表头1");
    XQ_EQ_I64(XTreeWidget_columnCount(tw), 2);

    /* setColumnCount(0) 合法（Qt qtreewidget.cpp:2637 只拒负值）。 */
    XTreeWidget_setColumnCount(tw, 0);
    XQ_EQ_I64(XTreeWidget_columnCount(tw), 0);
    XQ_EQ_STR(XTreeWidget_headerLabel(tw, 0), "");
    XTreeWidget_setColumnCount(tw, 2); /* 还原，后续列相关调用可用。 */
    XQ_EQ_I64(XTreeWidget_columnCount(tw), 2);
    /* 负列数仍被拒。 */
    XTreeWidget_setColumnCount(tw, -1);
    XQ_EQ_I64(XTreeWidget_columnCount(tw), 2);

    /* setCurrentItem_2：显式列与选择命令（gap 147）。 */
    {
        XItemSelectionModel* sm = XAbstractItemView_selectionModel(
            &tw->m_base.m_base);
        XQ_REQUIRE(sm != NULL);
        XTreeWidget_setCurrentItem_2(tw, 1, 1,
                                     XItemSelectionModelSelectionFlag_ClearAndSelect);
        XQ_EQ_I64(XTreeWidget_currentItem(tw), 1);
        XQ_EQ_I64(XTreeWidget_currentColumn(tw), 1);
        XQ_CHECK(XItemSelectionModel_isSelected(sm, 1, 1));
        XTreeWidget_setCurrentItem_2(tw, 0, 0,
                                     XItemSelectionModelSelectionFlag_NoUpdate);
        XQ_EQ_I64(XTreeWidget_currentColumn(tw), 0);
        /* NoUpdate 不动选区（行 1 列 1 仍选中）。 */
        XQ_CHECK(XItemSelectionModel_isSelected(sm, 1, 1));
        XTreeWidget_setCurrentItem(tw, 0);
    }

    /* 查找/反查。 */
    XQ_EQ_I64(XTreeWidget_indexOfTopLevelItem(tw, "b"), 0);
    XQ_EQ_I64(XTreeWidget_indexOfTopLevelItem(tw, "无此"), -1);
    XQ_EQ_I64(XTreeWidget_itemAbove(tw, 1), 0);
    XQ_EQ_I64(XTreeWidget_itemAbove(tw, 0), -1);
    XQ_EQ_I64(XTreeWidget_itemBelow(tw, 0), 1);
    XQ_EQ_I64(XTreeWidget_itemBelow(tw, 1), -1);

    /* 取出顶层条目：所有权归还调用方（其后条目释放由调用方负责）。 */
    {
        XTreeWidgetItem* taken = XTreeWidget_takeTopLevelItem(tw, 0);
        XQ_REQUIRE(taken != NULL);
        XQ_EQ_I64(XTreeWidget_topLevelItemCount(tw), 1);
        XTreeWidgetItem_delete(taken);
    }

    /* 子控件随 host 析构，漏删即判 [LEAK]。 */
    XClassDelete((XClass*)host);
}

/* ==================== editItem 编辑闭环（P0-30） ==================== */

/** @brief editItem 转发编辑闭环（此前为空操作桩）。
 *
 *  [P0-30 2026-11-12] 头注「本控件尚无编辑器/委托机制」已过期：编辑闭环
 *  建于 XAbstractItemView_edit，姊妹便利类都已接上（XListWidget_editItem /
 *  XTableWidget_editItem）。本用例锁：缺省门禁不开 → 打开视图总闸后经
 *  editItem 真开编辑器 → 越界行列不开新会话 → reset 收口。 */
XQ_TEST(XTreeWidget, editItemOpensEditor)
{
    XTreeWidget* tw = XTreeWidget_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL,
                                            0);
    XTreeWidgetItem* item;
    XQ_REQUIRE(tw != NULL);

    /* 视口要有宽高：编辑器几何取 visualRect，列宽自动铺满需要正视口宽。 */
    XWidget_resize((XWidget*)tw, 240, 200);
    XTreeWidget_setColumnCount(tw, 1);
    item = XTreeWidgetItem_create_2("alpha", NULL);
    XQ_REQUIRE(item != NULL);
    XQ_CHECK(XTreeWidget_addTopLevelItem(tw, item));
    XQ_EQ_I64(XTreeWidget_topLevelItemCount(tw), 1);

    /* 缺省不可编辑：itemsEditable=false 且桥模型 flags 无 ItemIsEditable
     * （对齐 Qt 只读模型上 edit 不开编辑器）。 */
    XQ_CHECK(!XAbstractItemView_itemsEditable(&tw->m_base.m_base));
    XTreeWidget_editItem(tw, 0, 0);
    XQ_CHECK(!XAbstractItemView_isEditing(&tw->m_base.m_base));

    /* 打开视图总闸 ⇒ editItem 真开编辑器（P0-30 的可见效果）。 */
    XAbstractItemView_setItemsEditable(&tw->m_base.m_base, true);
    XTreeWidget_editItem(tw, 0, 0);
    XQ_CHECK(XAbstractItemView_isEditing(&tw->m_base.m_base));

    /* 越界行/列：早退，不再开新会话（原会话仍在）。 */
    XTreeWidget_editItem(tw, 9, 0);
    XQ_CHECK(XAbstractItemView_isEditing(&tw->m_base.m_base));
    XTreeWidget_editItem(tw, 0, 9);
    XQ_CHECK(XAbstractItemView_isEditing(&tw->m_base.m_base));

    /* 复位收敛活动编辑器（放弃分支，不写模型）。 */
    XAbstractItemView_reset(&tw->m_base.m_base);
    XQ_CHECK(!XAbstractItemView_isEditing(&tw->m_base.m_base));

    XClassDelete((XClass*)tw);
}

#endif /* XGUI_ON && XWIDGET_ON && XTABLEWIDGET_ON */
