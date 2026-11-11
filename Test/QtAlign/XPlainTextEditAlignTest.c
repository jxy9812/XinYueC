/**
 * @file       XPlainTextEditAlignTest.c
 * @brief      XPlainTextEdit 与 Qt 6.8 QPlainTextEdit 的对齐契约测试。
 * @details    覆盖：
 *             - 构造默认值（QPlainTextEdit 文档口径：lineWrapMode
 *               WidgetWidth、wordWrapMode WordWrap、tabStopDistance 80（qreal，
 *               Qt 6.8 探针 80.0）、cursorWidth 1、只读 false、overwriteMode
 *               false、backgroundVisible true、centerOnScroll/centerCursor
 *               false、tabChangesFocus false、maximumBlockCount 0）；
 *             - setPlainText/appendPlainText/insertPlainText/clear 与
 *               blockCount/lineCount 的联动；
 *             - maximumBlockCount 超限丢弃最旧块（Qt
 *               QTextDocumentPrivate::ensureMaximumBlockCount 同口径）；
 *             - undo/redo 与 isUndoRedoEnabled；
 *             - **插入点取光标位置**（Qt 6.8.0 实测：setPlainText 后光标在
 *               文首 position 0，insertPlainText 从文首插入；undo 后回 0、
 *               redo 后回到重做串尾）；
 *             - 只读只拦键盘交互、不拦程序化插入（qplaintextedit.cpp:2690
 *               直通控制器的 insertPlainText，无 readOnly 守卫）；
 *             - find 命中/未命中；
 *             - **print 空承载**（本轮补齐的 API，qplaintextedit.h:148；
 *               无 QPagedPaintDevice 基础设施，调用不产生副作用）；
 *             - 光标行列往返与 cursorRect/cursorForPosition 一致性；
 *             - 父子析构。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XPlainTextEdit.h"
#include "XTextControl.h"
#include "XWidget.h"
#include "XClass.h"
#include "XMemory.h"

#include <stdio.h>
#include <string.h>

#if XWIDGET_ON && XABSTRACTSCROLLAREA_ON && XPLAINTEXTEDIT_ON

XQ_TEST(XPlainTextEdit, defaultsAndText)
{
    XPlainTextEdit* pte = XPlainTextEdit_create(NULL, 0);
    char* text;
    XQ_REQUIRE(pte != NULL);

    XQ_CHECK(!XPlainTextEdit_isReadOnly(pte));
    XQ_EQ_I64(XPlainTextEdit_lineWrapMode(pte),
              XPlainTextEditMode_WidgetWidth);
    XQ_EQ_I64(XPlainTextEdit_wordWrapMode(pte), XTextControlWrap_WordWrap);
    XQ_EQ_I64(XPlainTextEdit_maximumBlockCount(pte), 0);
    XQ_EQ_I64(XPlainTextEdit_blockCount(pte), 1);
    XQ_EQ_I64(XPlainTextEdit_lineCount(pte), 1);
    XQ_CHECK(XPlainTextEdit_isUndoRedoEnabled(pte));
    XQ_EQ_I64(XPlainTextEdit_cursorWidth(pte), 1);
    XQ_CHECK(XPlainTextEdit_tabStopDistance(pte) == 80.0); /* Qt 探针 80.0。 */
    XQ_CHECK(!XPlainTextEdit_centerOnScroll(pte));
    XQ_CHECK(!XPlainTextEdit_centerCursor(pte));
    XQ_CHECK(XPlainTextEdit_backgroundVisible(pte));
    XQ_CHECK(!XPlainTextEdit_tabChangesFocus(pte));
    XQ_CHECK(!XPlainTextEdit_overwriteMode(pte));

    text = XPlainTextEdit_toPlainText(pte);
    XQ_REQUIRE(text != NULL);
    XQ_EQ_STR(text, "");
    XFree_System(text);

    /* 多块往返：块数/行数/纯文本三者同源。 */
    XPlainTextEdit_setPlainText(pte, "a\nb\nc");
    XQ_EQ_I64(XPlainTextEdit_blockCount(pte), 3);
    XQ_EQ_I64(XPlainTextEdit_lineCount(pte), 3);
    text = XPlainTextEdit_toPlainText(pte);
    XQ_REQUIRE(text != NULL);
    XQ_EQ_STR(text, "a\nb\nc");
    XFree_System(text);

    /* appendPlainText：文末新起一段。 */
    XPlainTextEdit_appendPlainText(pte, "d");
    XQ_EQ_I64(XPlainTextEdit_blockCount(pte), 4);
    text = XPlainTextEdit_toPlainText(pte);
    XQ_REQUIRE(text != NULL);
    XQ_EQ_STR(text, "a\nb\nc\nd");
    XFree_System(text);

    /* clear：回到单空块。 */
    XPlainTextEdit_clear(pte);
    XQ_EQ_I64(XPlainTextEdit_blockCount(pte), 1);
    text = XPlainTextEdit_toPlainText(pte);
    XQ_REQUIRE(text != NULL);
    XQ_EQ_STR(text, "");
    XFree_System(text);

    XClassDelete((XClass*)pte);
}

XQ_TEST(XPlainTextEdit, maxBlockCountTrimsOldest)
{
    XPlainTextEdit* pte = XPlainTextEdit_create(NULL, 0);
    char* text;
    XQ_REQUIRE(pte != NULL);

    XPlainTextEdit_setPlainText(pte, "1\n2\n3\n4");
    XQ_EQ_I64(XPlainTextEdit_blockCount(pte), 4);
    XPlainTextEdit_setMaximumBlockCount(pte, 2);
    XQ_EQ_I64(XPlainTextEdit_maximumBlockCount(pte), 2);
    XQ_EQ_I64(XPlainTextEdit_blockCount(pte), 2);
    /* Qt 探针：setMaximumBlockCount(任意值) 后 document()->isUndoRedoEnabled()
       == 0（qtextdocument.cpp:518-524，无条件关闭）。 */
    XQ_CHECK(!XPlainTextEdit_isUndoRedoEnabled(pte));
    text = XPlainTextEdit_toPlainText(pte);
    XQ_REQUIRE(text != NULL);
    XQ_EQ_STR(text, "3\n4");
    XFree_System(text);

    /* 装载路径同样裁剪（setPlainText 走 xpe_tailStart 预处理）。 */
    XPlainTextEdit_setPlainText(pte, "a\nb\nc");
    XQ_EQ_I64(XPlainTextEdit_blockCount(pte), 2);
    text = XPlainTextEdit_toPlainText(pte);
    XQ_REQUIRE(text != NULL);
    XQ_EQ_STR(text, "b\nc");
    XFree_System(text);

    /* 负值原样保存（Qt 探针 setMaximumBlockCount(-4) 后返回 -4），
       且负值同样关闭撤销/重做（探针 undoRedo=0）。 */
    XPlainTextEdit_setUndoRedoEnabled(pte, true);
    XPlainTextEdit_setMaximumBlockCount(pte, -1);
    XQ_EQ_I64(XPlainTextEdit_maximumBlockCount(pte), -1);
    XQ_CHECK(!XPlainTextEdit_isUndoRedoEnabled(pte));

    /* 0 也关闭撤销/重做（探针 max=0 后 undoRedo=0），但不再裁剪。 */
    XPlainTextEdit_setPlainText(pte, "x\ny\nz");
    XPlainTextEdit_setUndoRedoEnabled(pte, true);
    XPlainTextEdit_setMaximumBlockCount(pte, 0);
    XQ_EQ_I64(XPlainTextEdit_maximumBlockCount(pte), 0);
    XQ_EQ_I64(XPlainTextEdit_blockCount(pte), 3);
    XQ_CHECK(!XPlainTextEdit_isUndoRedoEnabled(pte));

    XClassDelete((XClass*)pte);
}

XQ_TEST(XPlainTextEdit, tabStopDistanceIsReal)
{
    XPlainTextEdit* pte = XPlainTextEdit_create(NULL, 0);
    XQ_REQUIRE(pte != NULL);
    /* Qt 6.8：qreal 参数、浮点原样往返（探针 64.5 往返成立）、
       非正值忽略（探针 -5 保持 64.5）。 */
    XPlainTextEdit_setTabStopDistance(pte, 64.5);
    XQ_CHECK(XPlainTextEdit_tabStopDistance(pte) == 64.5);
    XPlainTextEdit_setTabStopDistance(pte, -5.0);
    XQ_CHECK(XPlainTextEdit_tabStopDistance(pte) == 64.5);
    XClassDelete((XClass*)pte);
}

/* ==================== 块数上限裁剪的 blockCountChanged ==================== */

static int s_pteBlockCount;     /**< 最近一次 blockCountChanged 的块数。 */
static int s_pteBlockCountN;    /**< blockCountChanged 次数。 */

static void xpe_slotBlockCount(XObject* receiver, XVarList* args)
{
    (void)receiver;
    if (!args) return;
    XVarList_args_1(args, int, count);
    s_pteBlockCount = count;
    ++s_pteBlockCountN;
}

/**
 * @brief maximumBlockCount 裁剪后块数通知取最终值（Qt 同口径）。
 * @details Qt：`QTextDocumentPrivate::ensureMaximumBlockCount` 在自己的编辑块
 *          里 `cursor.removeSelectedText()`（qtextdocument_p.cpp:1694-1707），
 *          收尾 `finishEdit` 发 `blockCountChanged(最终块数)`（qtextdocument_p
 *          .cpp:1220-1223）——本机探针 `pte.setMaximumBlockCount(2)`（4 块）
 *          ⇒ blockCountChanged(2)；未超限的设置**零信号**。
 *          本库裁剪在壳层 `xpe_enforceMaxBlockCount` 完成，块数通知经
 *          XTextControl 的 blockCountChanged 转发，取值同样是裁剪后块数。
 */
XQ_TEST(XPlainTextEdit, blockCountChangedOnTrim)
{
    XPlainTextEdit* pte = XPlainTextEdit_create(NULL, 0);
    char* text;
    XQ_REQUIRE(pte != NULL);
    s_pteBlockCount = 0;
    s_pteBlockCountN = 0;
    XObject_connect_1((XObject*)pte,
                      (size_t)XPlainTextEdit_blockCountChanged_signal(NULL, 0),
                      (XObject*)pte, xpe_slotBlockCount,
                      XConnectionType_Direct);

    /* 装载：4 块 ⇒ blockCountChanged(4)。 */
    XPlainTextEdit_setPlainText(pte, "1\n2\n3\n4");
    XQ_EQ_I64(XPlainTextEdit_blockCount(pte), 4);
    XQ_EQ_I64(s_pteBlockCount, 4);
    XQ_CHECK(s_pteBlockCountN >= 1);

    /* 设置上限到 2：裁剪最旧两块 ⇒ blockCountChanged(2)（最终块数）。 */
    s_pteBlockCount = 0;
    s_pteBlockCountN = 0;
    XPlainTextEdit_setMaximumBlockCount(pte, 2);
    XQ_EQ_I64(XPlainTextEdit_blockCount(pte), 2);
    XQ_CHECK_MSG(s_pteBlockCountN == 1 && s_pteBlockCount == 2,
                 "裁剪应发一次 blockCountChanged(2)（实际 %d 次，末值 %d）",
                 s_pteBlockCountN, s_pteBlockCount);
    text = XPlainTextEdit_toPlainText(pte);
    XQ_REQUIRE(text != NULL);
    XQ_EQ_STR(text, "3\n4");
    XFree_System(text);

    /* 上限放大到 5（不裁剪）⇒ 零通知（Qt 探针同口径）。 */
    s_pteBlockCount = 0;
    s_pteBlockCountN = 0;
    XPlainTextEdit_setMaximumBlockCount(pte, 5);
    XQ_EQ_I64(s_pteBlockCountN, 0);

    /* 追加一段使其超限：追加本身 + 裁剪仍以最终块数收尾（2）。 */
    s_pteBlockCount = 0;
    s_pteBlockCountN = 0;
    XPlainTextEdit_setMaximumBlockCount(pte, 2);
    XPlainTextEdit_appendPlainText(pte, "5");
    XQ_EQ_I64(XPlainTextEdit_blockCount(pte), 2);
    XQ_CHECK_MSG(s_pteBlockCount == 2,
                 "裁剪后末次 blockCountChanged 取最终块数 2（实际 %d）",
                 s_pteBlockCount);
    text = XPlainTextEdit_toPlainText(pte);
    XQ_REQUIRE(text != NULL);
    XQ_EQ_STR(text, "4\n5");
    XFree_System(text);

    XClassDelete((XClass*)pte);
}

XQ_TEST(XPlainTextEdit, undoRedoReadOnlyAndFind)
{
    XPlainTextEdit* pte = XPlainTextEdit_create(NULL, 0);
    char* text;
    XQ_REQUIRE(pte != NULL);

    /* Qt 6.8.0 探针：setPlainText 之后光标停在**文首**
     * （textCursor().position()==0），不是文末 ⇒ 紧接着的
     * insertPlainText 从文首插入，得 "dabc"。 */
    XPlainTextEdit_setPlainText(pte, "abc");
    XQ_EQ_I64(XPlainTextEdit_cursorLine(pte), 0);
    XQ_EQ_I64(XPlainTextEdit_cursorColumn(pte), 0);
    XPlainTextEdit_insertPlainText(pte, "d");
    text = XPlainTextEdit_toPlainText(pte);
    XQ_REQUIRE(text != NULL);
    XQ_EQ_STR(text, "dabc");
    XFree_System(text);

    XPlainTextEdit_undo(pte);
    text = XPlainTextEdit_toPlainText(pte);
    XQ_REQUIRE(text != NULL);
    XQ_EQ_STR(text, "abc");
    XFree_System(text);

    XPlainTextEdit_redo(pte);
    text = XPlainTextEdit_toPlainText(pte);
    XQ_REQUIRE(text != NULL);
    XQ_EQ_STR(text, "dabc");
    XFree_System(text);

    /* 撤销开关往返（Qt undoRedoEnabled）。 */
    XPlainTextEdit_setUndoRedoEnabled(pte, false);
    XQ_CHECK(!XPlainTextEdit_isUndoRedoEnabled(pte));
    XPlainTextEdit_setUndoRedoEnabled(pte, true);
    XQ_CHECK(XPlainTextEdit_isUndoRedoEnabled(pte));

    /* 只读不拦程序化插入（qplaintextedit.cpp:2690-2694 无 readOnly 守卫）。
     * redo 后光标停在重做插入串尾（position 1）⇒ 插入结果是 "dZabc"
     * （Qt 6.8.0 实测：undo 后 0、redo 后 1、插入 Z 后 2）。 */
    XPlainTextEdit_setReadOnly(pte, true);
    XQ_CHECK(XPlainTextEdit_isReadOnly(pte));
    XPlainTextEdit_insertPlainText(pte, "Z");
    text = XPlainTextEdit_toPlainText(pte);
    XQ_REQUIRE(text != NULL);
    XQ_EQ_STR(text, "dZabc");
    XFree_System(text);
    XPlainTextEdit_setReadOnly(pte, false);

    /* find：自当前选区尾向前查找（QTextDocument::find 起点口径），
       命中后选区即命中串；未命中返回 false。 */
    XPlainTextEdit_setPlainText(pte, "hello world");
    XPlainTextEdit_setTextCursor(pte, 0, 0);
    XQ_CHECK(XPlainTextEdit_find(pte, "world", 0));
    XQ_CHECK(XPlainTextEdit_hasSelectedText(pte));
    XQ_CHECK(!XPlainTextEdit_find(pte, "zzz", 0));

    XClassDelete((XClass*)pte);
}

XQ_TEST(XPlainTextEdit, printIsNoOpAndCursorRoundTrip)
{
    XPlainTextEdit* pte = XPlainTextEdit_create(NULL, 0);
    char* text;
    XRect rect;
    XPoint pos;
    XQ_REQUIRE(pte != NULL);

    XPlainTextEdit_setPlainText(pte, "line0\nline1");
    /* print 空承载：Qt 有该接口（qplaintextedit.h:148），本库无分页打印
       基础设施，调用不得改变文档内容、不得解引用 printer。 */
    XPlainTextEdit_print(pte, NULL);
    XPlainTextEdit_print(pte, (void*)pte);
    text = XPlainTextEdit_toPlainText(pte);
    XQ_REQUIRE(text != NULL);
    XQ_EQ_STR(text, "line0\nline1");
    XFree_System(text);

    /* 光标行列往返（文本坐标，行内 UTF-8 字节列）。 */
    XPlainTextEdit_setTextCursor(pte, 1, 2);
    XQ_CHECK(XPlainTextEdit_cursorLine(pte) == 1);
    XQ_CHECK(XPlainTextEdit_cursorColumn(pte) == 2);

    /* cursorRect 与 cursorForPosition 同一坐标系（往返命中同一行）。 */
    rect = XPlainTextEdit_cursorRect(pte);
    XQ_CHECK(rect.width > 0 && rect.height > 0);
    pos.x = rect.x;
    pos.y = rect.y;
    XQ_EQ_I64(XPlainTextEdit_cursorForPosition(pte, &pos).x, 1);

    XClassDelete((XClass*)pte);
}

XQ_TEST(XPlainTextEdit, parentChild)
{
    XWidget* parent = XWidget_create(NULL, 0);
    XPlainTextEdit* pte;
    XQ_REQUIRE(parent != NULL);
    pte = XPlainTextEdit_create(parent, 0);
    XQ_REQUIRE(pte != NULL);
    XQ_CHECK(XWidget_parentWidget((XWidget*)pte) == parent);
    XQ_EQ_I64(XPlainTextEdit_blockCount(pte), 1);
    XClassDelete((XClass*)parent);
}

#endif /* XWIDGET_ON && XABSTRACTSCROLLAREA_ON && XPLAINTEXTEDIT_ON */
