/**
 * @file       XAbstractItemViewAlignTest.c
 * @brief      XAbstractItemView × Qt 6.8 QAbstractItemView 对齐契约测试。
 * @details    对齐依据：
 *             - 头文件 /opt/Qt/6.8.3/gcc_64/include/QtWidgets/qabstractitemview.h
 *               （selectionMode/selectionBehavior/editTriggers/rootIndex/
 *               currentIndex/scrollMode/dragDrop/autoScroll/iconSize 等属性
 *               与 setModel 的换模型语义）；
 *             - 本机 Qt 6.8.0 实测：① QListView/QTreeView 上先
 *               setCurrentIndex 再 setModel，currentIndex().row() 变 -1、
 *               selectedIndexes() 清空，且选择模型对象同一（换挂新模型）；
 *               ② 只读模型（flags 无 ItemIsEditable）上 edit(index) 不开
 *               编辑器；③ setSelectionMode(NoSelection) **不清**已有选择。
 *
 *             覆盖：缺省值矩阵、setModel 换模型（当前索引失效 + 选择清空）
 *             与同模型早退、编辑门禁（模型 flags 只读 → 不开；视图总闸
 *             开启 → 开）、选择联动、iconSizeChanged 信号、父子所有权。
 * @note       C99；视图与模型全部释放（编辑器随视图/延迟删除回收），漏删
 *             即运行器判 `[LEAK]`。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

#if XGUI_ON && XWIDGET_ON && XTABLEWIDGET_ON

#include "XAbstractItemView.h"
#include "XAbstractItemModel.h"
#include "XItemSelectionModel.h"
#include "XObject.h"
#include "XWidget.h"

/* ==================== 信号探针 ==================== */

static int g_aivIconSizeHits;   /**< iconSizeChanged 命中次数。 */
static int g_aivIconW;          /**< 最近一次 iconSizeChanged 宽。 */
static int g_aivIconH;          /**< 最近一次 iconSizeChanged 高。 */

/** @brief iconSizeChanged(w,h) 槽。 */
static void xqAiv_iconSizeSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    ++g_aivIconSizeHits;
    if (!args) return;
    XVarList_args_2(args, int, w, int, h);
    g_aivIconW = w;
    g_aivIconH = h;
}

/* ==================== 用例 ==================== */

/** @brief 缺省值矩阵（对标 QAbstractItemView/QListView 构造缺省）。 */
XQ_TEST(XAbstractItemView, defaults)
{
    XAbstractItemView* v = XAbstractItemView_create_ex(
        XCLASS_DEFAULT_MEMORY_TYPE, NULL, 0);
    int row = -9;
    int col = -9;
    int w = -9;
    int h = -9;
    XQ_REQUIRE(v != NULL);

    /* 选择：基类 ExtendedSelection + SelectItems（qabstractitemview.h 枚举缺省）。 */
    XQ_EQ_I64(XAbstractItemView_selectionMode(v),
              XAbstractItemViewSelectionMode_ExtendedSelection);
    XQ_EQ_I64(XAbstractItemView_selectionBehavior(v),
              XAbstractItemViewSelectionBehavior_SelectItems);
    /* 编辑触发缺省 DoubleClicked|EditKeyPressed（Qt 除 QTableView/QTreeView
       外全部视图的缺省；Qt 6.8 文档口径）。 */
    XQ_EQ_I64(XAbstractItemView_editTriggers(v),
              XAbstractItemViewEditTrigger_DoubleClicked |
                  XAbstractItemViewEditTrigger_EditKeyPressed);
    XQ_CHECK(!XAbstractItemView_itemsEditable(v));

    /* 外观/滚动/拖放缺省。 */
    XQ_CHECK(!XAbstractItemView_alternatingRowColors(v));
    XQ_CHECK(XAbstractItemView_hasAutoScroll(v));
    XQ_EQ_I64(XAbstractItemView_autoScrollMargin(v), 16);
    XQ_EQ_I64(XAbstractItemView_textElideMode(v),
              XAbstractItemViewTextElideMode_ElideRight);
    XQ_EQ_I64(XAbstractItemView_verticalScrollMode(v),
              XAbstractItemViewScrollMode_ScrollPerItem);
    XQ_EQ_I64(XAbstractItemView_horizontalScrollMode(v),
              XAbstractItemViewScrollMode_ScrollPerItem);
    XQ_CHECK(!XAbstractItemView_dragEnabled(v));
    XQ_EQ_I64(XAbstractItemView_dragDropMode(v),
              XAbstractItemViewDragDropMode_NoDragDrop);
    XQ_CHECK(XAbstractItemView_showDropIndicator(v));
    XQ_EQ_I64(XAbstractItemView_defaultDropAction(v), 0); /* IgnoreAction。 */
    XQ_CHECK(!XAbstractItemView_tabKeyNavigation(v));
    XQ_CHECK(!XAbstractItemView_dragDropOverwriteMode(v));

    /* 图标尺寸 16x16（Qt 缺省 QSize(16,16)）；双输出与 setter 同源。 */
    XAbstractItemView_iconSize(v, &w, &h);
    XQ_CHECK(w == 16 && h == 16);

    /* 当前索引与根索引：无当前项、根为无效（平面模型记 (-1,-1)）。 */
    XQ_CHECK(!XAbstractItemView_currentIndex(v, &row, &col));
    XQ_CHECK(row == -1 && col == -1);
    XQ_CHECK(XAbstractItemView_rootIndex(v, &row, &col));
    XQ_CHECK(row == -1 && col == -1);

    /* 选择模型恒存在（Qt 视图构造即建 QItemSelectionModel）。 */
    XQ_CHECK_PTR(XAbstractItemView_selectionModel(v));
    XQ_CHECK(XAbstractItemView_model(v) == NULL);

    XClassDelete((XClass*)v);
}

/** @brief setModel：换模型使当前索引失效、选择清空；同模型早退（Qt 实测）。 */
XQ_TEST(XAbstractItemView, setModelResetsCurrentAndSelection)
{
    XAbstractItemView* v = XAbstractItemView_create_ex(
        XCLASS_DEFAULT_MEMORY_TYPE, NULL, 0);
    XAbstractItemModel* m1 = XAbstractItemModel_create();
    XAbstractItemModel* m2 = XAbstractItemModel_create();
    XItemSelectionModel* sm;
    int row = -9;
    int col = -9;
    XQ_REQUIRE(v != NULL && m1 != NULL && m2 != NULL);

    XAbstractItemModel_setDimension(m1, 4, 2);
    XAbstractItemModel_setDimension(m2, 2, 1);
    XAbstractItemView_setSelectionMode(
        v, XAbstractItemViewSelectionMode_SingleSelection);
    XAbstractItemView_setModel(v, m1);
    sm = XAbstractItemView_selectionModel(v);
    XQ_REQUIRE(sm != NULL);

    /* 建立当前索引 + 选择。 */
    XAbstractItemView_setCurrentIndex(v, 2, 0);
    XQ_CHECK(XAbstractItemView_currentIndex(v, &row, &col));
    XQ_CHECK(row == 2 && col == 0);
    XQ_CHECK(XItemSelectionModel_isSelected(sm, 2, 0));

    /* 同模型重复设置：Qt 首行 `if (model == d->model) return;` ⇒ 状态保持。 */
    XAbstractItemView_setModel(v, m1);
    XQ_CHECK(XAbstractItemView_currentIndex(v, &row, &col));
    XQ_CHECK(row == 2 && col == 0);
    XQ_CHECK(XItemSelectionModel_isSelected(sm, 2, 0));

    /* 换模型：当前索引失效（-1）、选择清空、选择模型对象同一且换挂新模型。
       （Qt 6.8.0 实测：setModel 后 currentIndex().row()==-1、
       selectedIndexes() 为空、selectionModel() 仍是同一对象。） */
    XAbstractItemView_setModel(v, m2);
    XQ_CHECK(XAbstractItemView_model(v) == m2);
    XQ_CHECK(!XAbstractItemView_currentIndex(v, &row, &col));
    XQ_CHECK(row == -1 && col == -1);
    XQ_EQ_I64(XItemSelectionModel_selectedCount(sm), 0);
    XQ_CHECK(XAbstractItemView_selectionModel(v) == sm);
    XQ_CHECK(XItemSelectionModel_model(sm) == m2);

    XClassDelete((XClass*)v);
    XClassDelete((XClass*)m1);
    XClassDelete((XClass*)m2);
}

/** @brief 编辑门禁：模型 flags 只读 ⇒ 不开；视图总闸开启 ⇒ 开（Qt 实测②）。 */
XQ_TEST(XAbstractItemView, editGateFollowsModelFlags)
{
    XAbstractItemView* v = XAbstractItemView_create_ex(
        XCLASS_DEFAULT_MEMORY_TYPE, NULL, 0);
    XAbstractItemModel* m = XAbstractItemModel_create();
    XQ_REQUIRE(v != NULL && m != NULL);

    XAbstractItemModel_setDimension(m, 2, 2);
    XAbstractItemView_setModel(v, m);
    /* 基类模型 flags 只给 Selectable|Enabled（无 ItemIsEditable）——与
       Qt 实测的只读模型同口径。 */
    XQ_EQ_I64(XAbstractItemModel_flags(m, 0, 0) &
                  XItemFlag_ItemIsEditable, 0);
    XQ_CHECK(!XAbstractItemView_edit(v, 0, 0));
    XQ_CHECK(!XAbstractItemView_isEditing(v));

    /* 视图总闸开启：本库在模型 flags 之上并上 m_itemsEditable（兼容既有
       接入），触发位取缺省的 DoubleClicked|EditKeyPressed ⇒ 可编辑。 */
    XAbstractItemView_setItemsEditable(v, true);
    XQ_CHECK(XAbstractItemView_edit(v, 0, 0));
    XQ_CHECK(XAbstractItemView_isEditing(v));
    /* 同格重复 edit 幂等成功（同 Qt）。 */
    XQ_CHECK(XAbstractItemView_edit(v, 0, 0));

    /* 越界索引：不开编辑器（Qt 对无效索引无条目矩形）。 */
    XQ_CHECK(!XAbstractItemView_edit(v, 5, 5));

    /* 复位收敛活动编辑器（放弃分支，不写模型）。 */
    XAbstractItemView_reset(v);
    XQ_CHECK(!XAbstractItemView_isEditing(v));

    /* 保留的既有口径（**已登记与 Qt 的差异**）：NoEditTriggers 时显式
       edit 判定失败。Qt 6.8.0 实测为「仍开编辑器」，差异见本批 gaps。 */
    XAbstractItemView_setEditTriggers(
        v, XAbstractItemViewEditTrigger_NoEditTriggers);
    XQ_CHECK(!XAbstractItemView_edit(v, 0, 0));

    XClassDelete((XClass*)v);
    XClassDelete((XClass*)m);
}

/** @brief 属性 setter/getter 往返、iconSizeChanged 信号与父子所有权。 */
XQ_TEST(XAbstractItemView, settersSignalsAndOwnership)
{
    XWidget* host = XWidget_create(NULL, 0);
    XAbstractItemView* v;
    int w = -9;
    int h = -9;
    XQ_REQUIRE(host != NULL);

    v = XAbstractItemView_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, host, 0);
    XQ_REQUIRE(v != NULL);
    /* 父子所有权：视图随宿主析构（qabstractitemview.h:16 `class
       QAbstractItemView : public QAbstractScrollArea` ⇒ QWidget 派生）。 */
    XQ_CHECK(XWidget_parentWidget((XWidget*)v) == host);
    XQ_CHECK(XWidget_isAncestorOf(host, (XWidget*)v));

    g_aivIconSizeHits = 0;
    XObject_connect_1((XObject*)v,
                      XSignal(XAbstractItemView_iconSizeChanged_signal),
                      (XObject*)v, xqAiv_iconSizeSlot,
                      XConnectionType_Direct);
    XAbstractItemView_setIconSize(v, 24, 28);
    XQ_EQ_I64(g_aivIconSizeHits, 1);
    XQ_CHECK(g_aivIconW == 24 && g_aivIconH == 28);
    XAbstractItemView_iconSize(v, &w, &h);
    XQ_CHECK(w == 24 && h == 28);
    /* 非正尺寸拒绝、同值不重复发（Qt 的 QSize 无效值不收）。 */
    XAbstractItemView_setIconSize(v, 0, 0);
    XAbstractItemView_setIconSize(v, 24, 28);
    XQ_EQ_I64(g_aivIconSizeHits, 1);

    /* 选择模式/行为/触发/外观往返。 */
    XAbstractItemView_setSelectionMode(
        v, XAbstractItemViewSelectionMode_MultiSelection);
    XQ_EQ_I64(XAbstractItemView_selectionMode(v),
              XAbstractItemViewSelectionMode_MultiSelection);
    XAbstractItemView_setSelectionBehavior(
        v, XAbstractItemViewSelectionBehavior_SelectRows);
    XQ_EQ_I64(XAbstractItemView_selectionBehavior(v),
              XAbstractItemViewSelectionBehavior_SelectRows);
    XAbstractItemView_setTextElideMode(
        v, XAbstractItemViewTextElideMode_ElideMiddle);
    XQ_EQ_I64(XAbstractItemView_textElideMode(v),
              XAbstractItemViewTextElideMode_ElideMiddle);
    XAbstractItemView_setAlternatingRowColors(v, true);
    XQ_CHECK(XAbstractItemView_alternatingRowColors(v));
    XAbstractItemView_setAutoScroll(v, false);
    XQ_CHECK(!XAbstractItemView_hasAutoScroll(v));
    XAbstractItemView_setAutoScrollMargin(v, 32);
    XQ_EQ_I64(XAbstractItemView_autoScrollMargin(v), 32);
    XAbstractItemView_setTabKeyNavigation(v, true);
    XQ_CHECK(XAbstractItemView_tabKeyNavigation(v));
    XAbstractItemView_setDragEnabled(v, true);
    XQ_CHECK(XAbstractItemView_dragEnabled(v));
    XAbstractItemView_setDragDropMode(v,
                                      XAbstractItemViewDragDropMode_DragOnly);
    XQ_EQ_I64(XAbstractItemView_dragDropMode(v),
              XAbstractItemViewDragDropMode_DragOnly);
    XAbstractItemView_setDropIndicatorShown(v, false);
    XQ_CHECK(!XAbstractItemView_showDropIndicator(v));
    XAbstractItemView_setDefaultDropAction(v, 2); /* MoveAction。 */
    XQ_EQ_I64(XAbstractItemView_defaultDropAction(v), 2);
    XAbstractItemView_setVerticalScrollMode(
        v, XAbstractItemViewScrollMode_ScrollPerPixel);
    XQ_EQ_I64(XAbstractItemView_verticalScrollMode(v),
              XAbstractItemViewScrollMode_ScrollPerPixel);
    XAbstractItemView_resetVerticalScrollMode(v);
    XQ_EQ_I64(XAbstractItemView_verticalScrollMode(v),
              XAbstractItemViewScrollMode_ScrollPerItem);

    /* 根索引：setRootIndex 存储 (row,col)，reset() 复位为无效。
       （Qt reset() 经 setRootIndex(QModelIndex()) 复位。） */
    XAbstractItemView_setRootIndex(v, 1, 2);
    XQ_CHECK(XAbstractItemView_rootIndex(v, &w, &h));
    XQ_CHECK(w == 1 && h == 2);
    XAbstractItemView_reset(v);
    XQ_CHECK(XAbstractItemView_rootIndex(v, &w, &h));
    XQ_CHECK(w == -1 && h == -1);

    /* 子控件不手动删除：随 host 析构，漏删即判 [LEAK]。 */
    XClassDelete((XClass*)host);
}

#endif /* XGUI_ON && XWIDGET_ON && XTABLEWIDGET_ON */
