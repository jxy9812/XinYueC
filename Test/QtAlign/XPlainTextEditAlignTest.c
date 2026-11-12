/**
 * @file       XPlainTextEditAlignTest.c
 * @brief      XPlainTextEdit 与 Qt 6.8 QPlainTextEdit 的对齐契约测试。
 * @details    覆盖：
 *             - 构造默认值（QPlainTextEdit 文档口径：lineWrapMode
 *               WidgetWidth、wordWrapMode WordWrap、tabStopDistance 80（qreal，
 *               Qt 6.8 探针 80.0）、cursorWidth 1、只读 false、overwriteMode
 *               false、backgroundVisible true、centerOnScroll false、
 *               tabChangesFocus false、maximumBlockCount 0）；
 *             - **centerCursor 的动作语义**（P0-29：Qt 的 `void centerCursor()`
 *               把光标行竖向居中，不是布尔属性）与两参
 *               `inputMethodQuery(query, argument)` 重载（P3-06）；
 *             - setDocument 不接管外部文档（P0-28 壳侧契约：换挂不释放、
 *               NULL 回退内部文档、外部文档归调用方）；
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
#include "XAbstractScrollArea.h"
#include "XScrollBar.h"
#if XTEXTDOCUMENT_ON
#include "XTextDocument.h"
#endif
#if XINPUTMETHOD_ON
#include "XInputMethod.h"
#include "XVariant.h"
#endif

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
    /* centerCursor 是**动作**不是属性（Qt 的 void centerCursor()）⇒ 无
       「默认值」可断言；行为断言见 centerCursorAndInputMethodQuery。 */
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
    XObject_connect((XObject*)pte,
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
    XQ_EQ_I64(XPlainTextEdit_textCursor(pte).x, 0);
    XQ_EQ_I64(XPlainTextEdit_textCursor(pte).y, 0);
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
    XQ_CHECK(XPlainTextEdit_textCursor(pte).x == 1);
    XQ_CHECK(XPlainTextEdit_textCursor(pte).y == 2);

    /* cursorRect 与 cursorForPosition 同一坐标系（往返命中同一行）。 */
    rect = XPlainTextEdit_cursorRect(pte);
    XQ_CHECK(rect.width > 0 && rect.height > 0);
    pos.x = rect.x;
    pos.y = rect.y;
    XQ_EQ_I64(XPlainTextEdit_cursorForPosition(pte, &pos).x, 1);

    XClassDelete((XClass*)pte);
}

/** @brief centerCursor 的居中动作（P0-29）与两参输入法查询（P3-06）。 */
XQ_TEST(XPlainTextEdit, centerCursorAndInputMethodQuery)
{
    XPlainTextEdit* pte = XPlainTextEdit_create(NULL, 0);
    XScrollBar* vsb;
    XRect cursorRect;
    int viewportH;
    int firstValue;
    XQ_REQUIRE(pte != NULL);

    XWidget_setGeometry((XWidget*)pte, 0, 0, 200, 120);
    XPlainTextEdit_setPlainText(pte,
                                "l0\nl1\nl2\nl3\nl4\nl5\nl6\nl7\nl8\nl9\n"
                                "l10\nl11\nl12\nl13\nl14\nl15\nl16\nl17\n"
                                "l18\nl19");
    vsb = XAbstractScrollArea_verticalScrollBar((XAbstractScrollArea*)pte);
    XQ_REQUIRE(vsb != NULL);
    /* 直接建立竖向滚动范围：居中判定只依赖「光标矩形 + 视口高 + 滚动条」，
       不依赖滚动区的自动量测（离屏无窗口时量测未必发生）。 */
    XScrollBar_setRange(vsb, 0, 800);
    XScrollBar_setValue(vsb, 0);

    /* centerCursor：把光标所在行竖向居中（Qt 的 ensureVisible(pos,
       center=true, forceCenter=true)）。 */
    XPlainTextEdit_setTextCursor(pte, 10, 0);
    XPlainTextEdit_centerCursor(pte);
    firstValue = XScrollBar_value(vsb);
    XQ_CHECK_MSG(firstValue > 0, "居中应向下滚动（实际 value=%d）", firstValue);
    cursorRect = XPlainTextEdit_cursorRect(pte);
    viewportH = XWidget_height((XWidget*)pte);
    {
        int delta = cursorRect.y + cursorRect.height / 2 - viewportH / 2;
        XQ_CHECK_MSG(delta >= -1 && delta <= 1,
                     "光标行中心应对齐视口中心（差 %d，rect.y=%d h=%d vh=%d）",
                     delta, cursorRect.y, cursorRect.height, viewportH);
    }
    /* 回到第 0 行再居中：滚动值钳位到 0（Qt 同：不能滚到负值）。 */
    XPlainTextEdit_setTextCursor(pte, 0, 0);
    XPlainTextEdit_centerCursor(pte);
    XQ_EQ_I64(XScrollBar_value(vsb), 0);
    /* NULL 安全（Qt 成员函数在空对象上不适用；本库按「NULL 无操作」）。 */
    XPlainTextEdit_centerCursor(NULL);

#if XINPUTMETHOD_ON
    {
        XVariant* var;
        XVariant* arg;
        int32_t pos = 3;
        bool enabled = false;
        int32_t absolute = -1;
        int32_t col = -1;
        char beforePlain[128];
        char beforeArg[128];
        XPointF pt;

        beforePlain[0] = '\0';
        beforeArg[0] = '\0';

        /* 无 argument：与单参虚槽同语义（ImEnabled = 可编辑）。 */
        var = XPlainTextEdit_inputMethodQuery_2(
            pte, (int)XInputMethodQuery_ImEnabled, NULL);
        XQ_CHECK_PTR(var);
        if (var) {
            if (XVariant_dataSize(var) == sizeof(bool) && XVariant_data(var))
                memcpy(&enabled, XVariant_data(var), sizeof(enabled));
            XQ_CHECK(enabled);
            XClassDelete((XClass*)var);
        }

        /* Int32 argument：ImAbsolutePosition 回该文档位置（透传查询口）。 */
        arg = XVariant_create(&pos, sizeof(pos), XVariantType_Int32);
        XQ_CHECK_PTR(arg);
        var = XPlainTextEdit_inputMethodQuery_2(
            pte, (int)XInputMethodQuery_ImAbsolutePosition, arg);
        XQ_CHECK_PTR(var);
        if (var) {
            if (XVariant_dataSize(var) == sizeof(absolute) &&
                XVariant_data(var))
                memcpy(&absolute, XVariant_data(var), sizeof(absolute));
            XQ_EQ_I64(absolute, 3);
            XClassDelete((XClass*)var);
        }
        if (arg) XClassDelete((XClass*)arg);

        /* User/XPointF argument：按 FuzzyHit 换成文档位置（Qt 的
           cursorForPosition 口径）；(0,0) 落在文首 ⇒ ImCursorPosition 的
           行内列号 0。 */
        pt.x = 0.0f;
        pt.y = 0.0f;
        arg = XVariant_create(&pt, sizeof(pt), XVariantType_User);
        XQ_CHECK_PTR(arg);
        var = XPlainTextEdit_inputMethodQuery_2(
            pte, (int)XInputMethodQuery_ImCursorPosition, arg);
        XQ_CHECK_PTR(var);
        if (var) {
            if (XVariant_dataSize(var) == sizeof(col) && XVariant_data(var))
                memcpy(&col, XVariant_data(var), sizeof(col));
            XQ_EQ_I64(col, 0);
            XClassDelete((XClass*)var);
        }
        if (arg) XClassDelete((XClass*)arg);

        /* 文本族查询**不消费** argument（控制器把 argument 当长度上限，
           传位置会错义）：传 Int32=0 与不传 argument 结果必须一致。 */
        XPlainTextEdit_setTextCursor(pte, 1, 2);
        var = XPlainTextEdit_inputMethodQuery_2(
            pte, (int)XInputMethodQuery_ImTextBeforeCursor, NULL);
        if (var) {
            const XString* s = XString_fromVariant_const(var);
            const char* utf8 = s ? XString_toUtf8(s) : NULL;
            if (utf8)
                strncpy(beforePlain, utf8, sizeof(beforePlain) - 1u);
            XClassDelete((XClass*)var);
        }
        {
            int32_t zero = 0;
            arg = XVariant_create(&zero, sizeof(zero), XVariantType_Int32);
            var = XPlainTextEdit_inputMethodQuery_2(
                pte, (int)XInputMethodQuery_ImTextBeforeCursor, arg);
            if (var) {
                const XString* s = XString_fromVariant_const(var);
                const char* utf8 = s ? XString_toUtf8(s) : NULL;
                if (utf8) strncpy(beforeArg, utf8, sizeof(beforeArg) - 1u);
                XClassDelete((XClass*)var);
            }
            if (arg) XClassDelete((XClass*)arg);
        }
        XQ_EQ_STR(beforeArg, beforePlain);
        XQ_CHECK_MSG(beforePlain[0] != '\0',
                     "光标前文本应非空（line1 col2 之前至少有 'l1'）");
    }
#endif /* XINPUTMETHOD_ON */

    XClassDelete((XClass*)pte);
}

#if XTEXTDOCUMENT_ON
/**
 * @brief setDocument 不接管外部文档（P0-28 的壳侧契约，与 Qt 同）。
 * @details 外部文档换入后：document() 回文档本体、纯文本镜像进行模型；
 *          再换入第二个文档时第一个**不得**被控制器释放（所有权仍在
 *          调用方，本用例末尾显式释放两个文档验证无二次删除）；传 NULL
 *          回退控制器自建的内部文档。
 */
XQ_TEST(XPlainTextEdit, setDocumentDoesNotOwnExternalDoc)
{
    XPlainTextEdit* pte = XPlainTextEdit_create(NULL, 0);
    XTextDocument* doc1 = XTextDocument_create();
    XTextDocument* doc2 = XTextDocument_create();
    char* text;
    XQ_REQUIRE(pte != NULL && doc1 != NULL && doc2 != NULL);

    XTextDocument_setPlainText(doc1, "ext doc");
    XPlainTextEdit_setDocument(pte, doc1);
    text = XPlainTextEdit_toPlainText(pte);
    XQ_REQUIRE(text != NULL);
    XQ_EQ_STR(text, "ext doc");
    XFree_System(text);
    XQ_CHECK(XPlainTextEdit_document(pte) == doc1);

    /* 换入第二个外部文档：第一个不被释放（可继续安全访问）。 */
    XTextDocument_setPlainText(doc2, "second");
    XPlainTextEdit_setDocument(pte, doc2);
    XQ_CHECK(XPlainTextEdit_document(pte) == doc2);
    XQ_EQ_I64(XTextDocument_blockCount(doc1), 1);
    XQ_EQ_I64(XTextDocument_characterCount(doc1), 8); /* "ext doc"+分隔符。 */
    text = XPlainTextEdit_toPlainText(pte);
    XQ_REQUIRE(text != NULL);
    XQ_EQ_STR(text, "second");
    XFree_System(text);

    /* setDocument(NULL)：回退内部默认文档（既不是 doc1 也不是 doc2）。 */
    XPlainTextEdit_setDocument(pte, NULL);
    XQ_CHECK(XPlainTextEdit_document(pte) != NULL);
    XQ_CHECK(XPlainTextEdit_document(pte) != doc1);
    XQ_CHECK(XPlainTextEdit_document(pte) != doc2);

    /* 外部文档归调用方：这里释放即验证控制器没有持有/二次释放
       （泄漏门 + 双重释放都会在此炸出）。 */
    XClassDelete((XClass*)doc1);
    XClassDelete((XClass*)doc2);
    XClassDelete((XClass*)pte);
}
#endif /* XTEXTDOCUMENT_ON */

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
