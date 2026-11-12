/**
 * @file       XItemDelegateAlignTest.c
 * @brief      XItemDelegate × Qt 6.8 QAbstractItemDelegate 对齐契约测试。
 * @details    对齐依据：
 *             - 头文件 /opt/Qt/6.8.3/gcc_64/include/QtWidgets/
 *               qabstractitemdelegate.h：虚函数 createEditor/destroyEditor/
 *               setEditorData/setModelData/updateEditorGeometry 与信号
 *               commitData/closeEditor/sizeHintChanged、枚举 EndEditHint；
 *             - Qt 源码 qabstractitemdelegate.cpp:237-241 —— 缺省
 *               destroyEditor 即 `editor->deleteLater();`（本库对应
 *               XObject_deleteLater，见 XItemDelegate.c 的缺省虚槽）；
 *             - 本库口头契约（XItemDelegate.h）：编辑器以视图为父控件、
 *               所有权移交视图；委托为借用承载（视图不析构用户委托）。
 *
 *             覆盖：缺省/空参安全性、编辑器全生命周期（create →
 *             setEditorData → updateEditorGeometry → setModelData →
 *             destroyEditor）、commitData/closeEditor/sizeHintChanged
 *             三信号、编辑器随视图父析构的所有权关系。
 * @note       C99；委托/视图/条目全部释放，漏删即运行器判 `[LEAK]`。
 *             destroyEditor 走延迟销毁：用例内先 processEvents 排空
 *             延迟删除事件，再删视图，避免悬垂投递。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

#if XGUI_ON && XWIDGET_ON && XTABLEWIDGET_ON

#include "XItemDelegate.h"
#include "XTableWidget.h"
#include "XLineEdit.h"
#include "XWidget.h"
#include "XCoreApplication.h"
#include "XEventLoop.h"

/* ==================== 信号探针 ==================== */

static int g_idCommitHits;   /**< commitData 命中次数。 */
static int g_idCloseHits;    /**< closeEditor 命中次数。 */
static int g_idCloseHint;    /**< closeEditor 的 EndEditHint 载荷。 */
static int g_idHintHits;     /**< sizeHintChanged 命中次数。 */
static int g_idHintRow;      /**< sizeHintChanged 的行载荷。 */
static int g_idHintCol;      /**< sizeHintChanged 的列载荷。 */

/** @brief commitData(editor) 槽。 */
static void xqId_commitSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++g_idCommitHits;
}

/** @brief closeEditor(editor,hint) 槽。 */
static void xqId_closeSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    ++g_idCloseHits;
    if (!args) return;
    XVarList_args_2(args, XWidget*, editor, int, hint);
    (void)editor;
    g_idCloseHint = hint;
}

/** @brief sizeHintChanged(row,col) 槽。 */
static void xqId_hintSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    ++g_idHintHits;
    if (!args) return;
    XVarList_args_2(args, int, row, int, col);
    g_idHintRow = row;
    g_idHintCol = col;
}

/* ==================== 缺省/空参安全 ==================== */

/** @brief 缺省构造与空参调用安全（Qt：基类 createEditor 返回 nullptr）。 */
XQ_TEST(XItemDelegate, defaults)
{
    XItemDelegate* d = XItemDelegate_create();
    XQ_REQUIRE(d != NULL);

    /* 无视图 ⇒ 无法承载编辑器父控件，缺省虚槽返回 NULL（Qt 基类
     * QAbstractItemDelegate::createEditor 亦返回 nullptr）。 */
    XQ_CHECK(XItemDelegate_createEditor(d, NULL, 0, 0) == NULL);
    /* 空参不崩溃（setEditorData/setModelData/updateEditorGeometry/
     * destroyEditor 均按空操作收口）。 */
    XItemDelegate_setEditorData(d, NULL, NULL, 0, 0);
    XItemDelegate_setModelData(d, NULL, NULL, 0, 0);
    XItemDelegate_updateEditorGeometry(d, NULL, NULL);
    XItemDelegate_destroyEditor(d, NULL);
    XQ_CHECK(true);

    XClassDelete((XClass*)d);
}

/* ==================== 编辑器全生命周期（集成关系） ==================== */

/** @brief create→setEditorData→updateEditorGeometry→setModelData→destroy。 */
XQ_TEST(XItemDelegate, editorLifecycle)
{
    XTableWidget* view = XTableWidget_create(NULL, 0);
    XItemDelegate* d = XItemDelegate_create();
    XWidget* editor;
    XRect rect;
    XRect got;
    XQ_REQUIRE(view != NULL && d != NULL);

    XTableWidget_setRowCount(view, 2);
    XTableWidget_setColumnCount(view, 2);
    XTableWidget_setItem(view, 0, 0,
        XTableWidgetItem_create_utf8("hello", XTableWidgetItem_Type));
    XQ_REQUIRE(XTableWidget_item(view, 0, 0) != NULL);

    /* 创建：缺省路径造内嵌行编辑器，父控件 = 视图（Qt createEditor 的
     * parent 约定；编辑器生命周期归视图）。 */
    editor = XItemDelegate_createEditor(d, (XAbstractItemView*)view, 0, 0);
    XQ_CHECK_PTR(editor);
    XQ_REQUIRE(editor != NULL);
    XQ_CHECK(XWidget_parentWidget(editor) == (XWidget*)view);

    /* 装载初值：EditRole 文本 + 全选（对标 QExpandingLineEdit）。 */
    XItemDelegate_setEditorData(d, editor, (XAbstractItemView*)view, 0, 0);
    XQ_EQ_STR(XLineEdit_text((XLineEdit*)editor), "hello");
    XQ_CHECK(XLineEdit_hasSelectedText((XLineEdit*)editor));

    /* 几何：整格摆放（对标 option.rect）。 */
    rect.x = 5;
    rect.y = 7;
    rect.width = 60;
    rect.height = 22;
    XItemDelegate_updateEditorGeometry(d, editor, &rect);
    got = XWidget_geometry(editor);
    XQ_CHECK(got.x == 5 && got.y == 7 && got.width == 60 && got.height == 22);

    /* 回写：编辑器文本经 EditRole 通路进模型（视图 itemText 读回）。 */
    XLineEdit_setText((XLineEdit*)editor, "world");
    XItemDelegate_setModelData(d, editor, (XAbstractItemView*)view, 0, 0);
    XQ_EQ_STR(XTableWidgetItem_text_toUtf8(XTableWidget_item(view, 0, 0)),
              "world");

    /* 销毁：缺省 destroyEditor 延迟销毁（Qt deleteLater 同款）；
     * 先排空延迟事件再删视图，避免悬垂投递。 */
    XItemDelegate_destroyEditor(d, editor);
    (void)XCoreApplication_processEvents(XEventLoop_AllEvents);

    XClassDelete((XClass*)d);
    XClassDelete((XClass*)view);
}

/* ==================== 三信号 ==================== */

/** @brief commitData / closeEditor / sizeHintChanged 发射与载荷。 */
XQ_TEST(XItemDelegate, signals)
{
    XTableWidget* view = XTableWidget_create(NULL, 0);
    XItemDelegate* d = XItemDelegate_create();
    XWidget* editor;
    XQ_REQUIRE(view != NULL && d != NULL);
    XTableWidget_setRowCount(view, 1);
    XTableWidget_setColumnCount(view, 1);
    XTableWidget_setItem(view, 0, 0,
        XTableWidgetItem_create_utf8("abc", XTableWidgetItem_Type));
    editor = XItemDelegate_createEditor(d, (XAbstractItemView*)view, 0, 0);
    XQ_REQUIRE(editor != NULL);

    g_idCommitHits = 0;
    g_idCloseHits = 0;
    g_idCloseHint = -9;
    g_idHintHits = 0;
    g_idHintRow = -9;
    g_idHintCol = -9;
    XObject_connect((XObject*)d, XSignal(XItemDelegate_commitData_signal),
                      (XObject*)d, xqId_commitSlot, XConnectionType_Direct);
    XObject_connect((XObject*)d, XSignal(XItemDelegate_closeEditor_signal),
                      (XObject*)d, xqId_closeSlot, XConnectionType_Direct);
    XObject_connect((XObject*)d,
                      XSignal(XItemDelegate_sizeHintChanged_signal),
                      (XObject*)d, xqId_hintSlot, XConnectionType_Direct);

    /* 提交链：commitData → closeEditor(RevertModelCache 分支的放弃提示)；
     * 这里按框架既有口径手动发射两信号验证载荷（真实发射点在编辑器
     * 键/失焦处理器里，见 XItemDelegate.c 的 VXItemLineEditor_*）。 */
    (void)XItemDelegate_commitData_signal(d, editor);
    XQ_EQ_I64(g_idCommitHits, 1);
    (void)XItemDelegate_closeEditor_signal(
        d, editor, XItemDelegateEndEditHint_RevertModelCache);
    XQ_EQ_I64(g_idCloseHits, 1);
    XQ_EQ_I64(g_idCloseHint, (int)XItemDelegateEndEditHint_RevertModelCache);

    /* sizeHintChanged：缺省 setModelData 完成回写后发射（行/列载荷 = 目标格）。 */
    (void)XItemDelegate_setModelData(d, editor, (XAbstractItemView*)view, 0, 0);
    XQ_EQ_I64(g_idHintHits, 1);
    XQ_EQ_I64(g_idHintRow, 0);
    XQ_EQ_I64(g_idHintCol, 0);

    XItemDelegate_destroyEditor(d, editor);
    (void)XCoreApplication_processEvents(XEventLoop_AllEvents);
    XClassDelete((XClass*)d);
    XClassDelete((XClass*)view);
}

/* ==================== 父子所有权 ==================== */

/** @brief 编辑器是视图子控件：不显式 destroy 也随视图析构。 */
XQ_TEST(XItemDelegate, parentOwnership)
{
    XTableWidget* view = XTableWidget_create(NULL, 0);
    XItemDelegate* d = XItemDelegate_create();
    XWidget* editor;
    XQ_REQUIRE(view != NULL && d != NULL);
    XTableWidget_setRowCount(view, 1);
    XTableWidget_setColumnCount(view, 1);
    XTableWidget_setItem(view, 0, 0,
        XTableWidgetItem_create_utf8("x", XTableWidgetItem_Type));

    editor = XItemDelegate_createEditor(d, (XAbstractItemView*)view, 0, 0);
    XQ_REQUIRE(editor != NULL);
    XQ_CHECK(XWidget_isAncestorOf((XWidget*)view, editor));
    /* 委托为借用承载、编辑器归视图：先删委托（不影响编辑器），再删视图
     * 带走编辑器；漏删即判 [LEAK]。 */
    XClassDelete((XClass*)d);
    XClassDelete((XClass*)view);
}

#endif /* XGUI_ON && XWIDGET_ON && XTABLEWIDGET_ON */
