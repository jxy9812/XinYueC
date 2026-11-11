/**
 * @file       XTextEditAlignTest.c
 * @brief      XTextEdit 与 Qt 6.8 QTextEdit 的对齐契约测试。
 * @details    覆盖：
 *             - 构造默认值（QTextEditPrivate 构造：lineWrapMode
 *               WidgetWidth、wordWrapMode WordBoundaryOrAnywhere、
 *               acceptRichText true、autoFormatting 0、cursorWidth 1、
 *               tabStopDistance 80、只读 false、overwriteMode false、
 *               placeholder 空）；
 *             - setPlainText/toPlainText 往返与 append；
 *             - setHtml 的"文档真源 + 编辑缓冲剥离标签"两端状态，以及
 *               **setHtml→toHtml 往返保留格式**（Qt：文档即真源）与编辑器
 *               改动后的纯文本回落口径；
 *             - **currentCharFormat getter**（本轮补齐；qtextedit.h:122
 *               `QTextCharFormat currentCharFormat() const`）与
 *               setCurrentCharFormat/mergeCurrentCharFormat 同源；
 *             - 只读/换行/自动格式化/覆盖模式/制表位的 setter-getter 往返；
 *             - **文档所有权**：setDocument 接管的外部文档不随控件析构
 *               （qwidgettextcontrol.cpp:877-892 只在 `doc->parent() ==
 *               this` 时 delete 旧文档 ⇒ 外部文档归调用方）；
 *             - 父子析构。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XTextEdit.h"
#include "XPlainTextEdit.h"
#if XTEXTDOCUMENT_ON
#include "XTextDocument.h"
#endif
#include "XWidget.h"
#include "XClass.h"
#include "XString.h"
#include "XStringUtils.h"
#include "XMemory.h"

#include <stdio.h>
#include <string.h>

#if XWIDGET_ON && XABSTRACTSCROLLAREA_ON && XPLAINTEXTEDIT_ON && XTEXTEDIT_ON

/** @brief 取 toPlainText 堆结果的借用视图（用完 XFree_System）。
 *  @note  XString_toUtf8 对空串返回 NULL（XString.c 的空串早退），
 *         这里统一按 "" 处理——断言语义看内容而不是底层指针约定。 */
static char* xte_plain(const XTextEdit* te)
{
    XString* s = XTextEdit_toPlainText(te);
    const char* u8;
    char* out;
    size_t n;
    if (!s) return NULL;
    u8 = XString_toUtf8(s);
    if (!u8) u8 = "";
    n = XStrlen(u8);
    out = (char*)XMalloc_System(n + 1);
    if (out) {
        if (n) XMemcpy(out, u8, n);
        out[n] = '\0';
    }
    XClassDelete((XClass*)s);
    return out;
}

XQ_TEST(XTextEdit, defaultsAndPlainText)
{
    XTextEdit* te = XTextEdit_create(NULL, 0);
    char* plain;
    XQ_REQUIRE(te != NULL);

    /* Qt 默认：readOnly false、acceptRichText true、autoFormatting 0、
       cursorWidth 1、tabStopDistance 80、centerOnScroll false、
       backgroundVisible true、overwriteMode false、lineWrap WidgetWidth。 */
    XQ_CHECK(!XTextEdit_isReadOnly_2(te));
    XQ_CHECK(XTextEdit_acceptRichText(te));
    XQ_EQ_I64(XTextEdit_autoFormatting(te), 0);
    XQ_EQ_I64(XTextEdit_cursorWidth(te), 1);
    XQ_CHECK(XTextEdit_tabStopDistance(te) == 80.0);
    XQ_CHECK(!XTextEdit_centerOnScroll(te));
    XQ_CHECK(XTextEdit_backgroundVisible(te));
    XQ_CHECK(!XTextEdit_overwriteMode(te));
    XQ_EQ_I64(XTextEdit_lineWrapMode(te), XPlainTextEditMode_WidgetWidth);
    XQ_EQ_I64(XTextEdit_lineWrapColumnOrWidth(te), 0);
    XQ_EQ_I64(XTextEdit_currentCharFormat(te), XTextEditCharFormat_None);

    plain = xte_plain(te);
    XQ_REQUIRE(plain != NULL);
    XQ_EQ_STR(plain, "");
    XFree_System(plain);

    XTextEdit_setPlainText(te, "a\nb");
    plain = xte_plain(te);
    XQ_REQUIRE(plain != NULL);
    XQ_EQ_STR(plain, "a\nb");
    XFree_System(plain);

    /* appendPlainText：文末新起一段（Qt QPlainTextEdit::appendPlainText）。 */
    XTextEdit_append(te, "c");
    plain = xte_plain(te);
    XQ_REQUIRE(plain != NULL);
    XQ_EQ_STR(plain, "a\nb\nc");
    XFree_System(plain);

    /* 属性往返（Qt 属性 WRITE/READ 成对）。 */
    XTextEdit_setReadOnly_2(te, true);
    XQ_CHECK(XTextEdit_isReadOnly_2(te));
    XTextEdit_setOverwriteMode(te, true);
    XQ_CHECK(XTextEdit_overwriteMode(te));
    XTextEdit_setAutoFormatting(te, 0x00000001);
    XQ_EQ_I64(XTextEdit_autoFormatting(te), 0x00000001);
    XTextEdit_setCursorWidth(te, 3);
    XQ_EQ_I64(XTextEdit_cursorWidth(te), 3);
    XTextEdit_setTabStopDistance(te, 40.0);
    XQ_CHECK(XTextEdit_tabStopDistance(te) == 40.0);
    XTextEdit_setLineWrapColumnOrWidth(te, 120);
    XQ_EQ_I64(XTextEdit_lineWrapColumnOrWidth(te), 120);
    XTextEdit_setLineWrapMode(te, XPlainTextEditMode_NoWrap);
    XQ_EQ_I64(XTextEdit_lineWrapMode(te), XPlainTextEditMode_NoWrap);

    XClassDelete((XClass*)te);
}

XQ_TEST(XTextEdit, charFormatAndHtml)
{
    XTextEdit* te = XTextEdit_create(NULL, 0);
    char* plain;
    char* html;
    XQ_REQUIRE(te != NULL);

    /* currentCharFormat 与 setCurrentCharFormat 同源（本轮补齐的 getter）。 */
    XTextEdit_setCurrentCharFormat(te,
                                   XTextEditCharFormat_Bold |
                                       XTextEditCharFormat_Italic);
    XQ_EQ_I64(XTextEdit_currentCharFormat(te),
              XTextEditCharFormat_Bold | XTextEditCharFormat_Italic);
    XQ_CHECK(XTextEdit_isBold(te));
    XQ_CHECK(XTextEdit_isItalic(te));
    XQ_CHECK(!XTextEdit_isUnderline(te));

    /* mergeCurrentCharFormat 只置位不清除（Qt merge 语义）。 */
    XTextEdit_mergeCurrentCharFormat(te, XTextEditCharFormat_Underline);
    XQ_EQ_I64(XTextEdit_currentCharFormat(te),
              XTextEditCharFormat_Bold | XTextEditCharFormat_Italic |
                  XTextEditCharFormat_Underline);

    /* setCurrentCharFormat 整体替换。 */
    XTextEdit_setCurrentCharFormat(te, XTextEditCharFormat_None);
    XQ_EQ_I64(XTextEdit_currentCharFormat(te), XTextEditCharFormat_None);

    /* setHtml：文档收富文本、编辑缓冲剥离标签（所见即所存）。 */
    XTextEdit_setHtml(te, "<b>Hi</b>");
    plain = xte_plain(te);
    XQ_REQUIRE(plain != NULL);
    XQ_EQ_STR(plain, "Hi");
    XFree_System(plain);
#if XTEXTDOCUMENT_ON
    XQ_CHECK(XTextDocument_blockCount(XTextEdit_document(te)) >= 1);
#endif
    html = XTextEdit_toHtml(te);
    XQ_REQUIRE(html != NULL);
    XQ_CHECK_MSG(strstr(html, "Hi") != NULL, "toHtml 含正文文本：%s", html);
    /* Qt 语义（qtextedit.cpp QTextEdit::toHtml -> document()->toHtml()）：
       文档即真源 ⇒ setHtml→toHtml 往返保留格式（旧实现以编辑器纯文本
       重建文档，丢 <b>）。 */
    XQ_CHECK_MSG(strstr(html, "<b>") != NULL,
                 "setHtml→toHtml 往返保留粗体标记：%s", html);
    XFree_System(html);

    /* 编辑器被改动后文档不再是真源：toHtml 回落纯文本导出（平铺模型
       无法把键入内容并入格式文档，见 gaps）。 */
    XTextEdit_setPlainText(te, "plain");
    html = XTextEdit_toHtml(te);
    XQ_REQUIRE(html != NULL);
    XQ_CHECK_MSG(strstr(html, "plain") != NULL && strstr(html, "<b>") == NULL,
                 "编辑器改动后 toHtml 以编辑器纯文本为准：%s", html);
    XFree_System(html);

    XTextEdit_setMarkdown(te, "# Title");
    XQ_EQ_STR(XTextEdit_markdown(te), "# Title");
    plain = xte_plain(te);
    XQ_REQUIRE(plain != NULL);
    XQ_EQ_STR(plain, "# Title");
    XFree_System(plain);

    XClassDelete((XClass*)te);
}

XQ_TEST(XTextEdit, documentOwnership)
{
    XTextEdit* te = XTextEdit_create(NULL, 0);
    XQ_REQUIRE(te != NULL);

#if XTEXTDOCUMENT_ON
    {
        XTextDocument* doc = XTextDocument_create();
        XQ_REQUIRE(doc != NULL);
        /* setDocument 接管显示但不获得所有权（Qt：外部文档归调用方）。 */
        XTextEdit_setDocument(te, doc);
        XQ_CHECK(XTextEdit_document(te) == doc);
        XTextDocument_setPlainText(doc, "external");
        /* 控件析构后外部文档仍可用（未被控件释放）。 */
        XClassDelete((XClass*)te);
        XQ_EQ_I64(XTextDocument_blockCount(doc), 1);
        XQ_CHECK(!XTextDocument_isEmpty(doc));
        XClassDelete((XClass*)doc);
    }
    {
        /* setDocument(NULL) 建内部默认文档（Qt setDocument(nullptr) 同）。 */
        XTextEdit* te2 = XTextEdit_create(NULL, 0);
        XQ_REQUIRE(te2 != NULL);
        XTextEdit_setDocument(te2, NULL);
        XQ_CHECK(XTextEdit_document(te2) != NULL);
        XClassDelete((XClass*)te2);
    }
#else
    XClassDelete((XClass*)te);
#endif
}

XQ_TEST(XTextEdit, parentChild)
{
    XWidget* parent = XWidget_create(NULL, 0);
    XTextEdit* te;
    XQ_REQUIRE(parent != NULL);
    te = XTextEdit_create(parent, 0);
    XQ_REQUIRE(te != NULL);
    XQ_CHECK(XWidget_parentWidget((XWidget*)te) == parent);
    XQ_EQ_I64(XTextEdit_lineWrapMode(te), XPlainTextEditMode_WidgetWidth);
    XClassDelete((XClass*)parent);
}

#if XMIMEDATA_ON
XQ_TEST(XTextEdit, mimeSelectionAndInsert)
{
    XTextEdit* te = XTextEdit_create(NULL, 0);
    XMimeData* mime;
    XString* s;
    XMimeData* htmlMime;
    XString* htmlText;
    char* plain;
    XQ_REQUIRE(te != NULL);

    /* createMimeDataFromSelection：选区纯文本进 text/plain
       （qtextedit.cpp:2181-2185 -> QWidgetTextControl:2693-2698）。 */
    XTextEdit_setPlainText(te, "hello");
    XTextEdit_selectAll_2(te);
    mime = XTextEdit_createMimeDataFromSelection(te);
    XQ_REQUIRE(mime != NULL);
    XQ_CHECK(XMimeData_hasText(mime));
    s = XMimeData_text(mime);
    XQ_REQUIRE(s != NULL);
    XQ_EQ_STR(XString_toUtf8(s), "hello");
    XClassDelete((XClass*)s);
    XClassDelete((XClass*)mime);

    /* canInsertFromMimeData（qwidgettextcontrol.cpp:2700-2710）：
       非空纯文本可插；空 MIME 不可插。 */
    mime = XMimeData_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    XQ_REQUIRE(mime != NULL);
    XQ_CHECK(!XTextEdit_canInsertFromMimeData(te, mime));
    s = XString_create_utf8("X");
    XQ_REQUIRE(s != NULL);
    XMimeData_setText(mime, s);
    XClassDelete((XClass*)s);
    XQ_CHECK(XTextEdit_canInsertFromMimeData(te, mime));

    /* insertFromMimeData：插入到当前光标处（qwidgettextcontrol.cpp:2712）。 */
    XTextEdit_setPlainText(te, "ab");
    XTextEdit_insertFromMimeData(te, mime);
    plain = xte_plain(te);
    XQ_REQUIRE(plain != NULL);
    XQ_CHECK_MSG(strstr(plain, "X") != NULL, "MIME 文本已插入：%s", plain);
    XFree_System(plain);

    /* 只读时 insertFromMimeData 无操作（TextEditable 门禁）。 */
    XTextEdit_setReadOnly_2(te, true);
    {
        char* before = xte_plain(te);
        XTextEdit_insertFromMimeData(te, mime);
        plain = xte_plain(te);
        XQ_REQUIRE(before != NULL && plain != NULL);
        XQ_EQ_STR(plain, before);
        XFree_System(before);
        XFree_System(plain);
    }
    XTextEdit_setReadOnly_2(te, false);
    XClassDelete((XClass*)mime);

    /* acceptRichText=false 时 HTML-only 数据不可插（qwidgettextcontrol.cpp:
       2703-2709 的 else 分支）。 */
    htmlMime = XMimeData_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    XQ_REQUIRE(htmlMime != NULL);
    htmlText = XString_create_utf8("<b>x</b>");
    XQ_REQUIRE(htmlText != NULL);
    XMimeData_setHtml(htmlMime, htmlText);
    XClassDelete((XClass*)htmlText);
    XTextEdit_setAcceptRichText(te, true);
    XQ_CHECK(XTextEdit_canInsertFromMimeData(te, htmlMime));
    XTextEdit_setAcceptRichText(te, false);
    XQ_CHECK(!XTextEdit_canInsertFromMimeData(te, htmlMime));
    XClassDelete((XClass*)htmlMime);

    XClassDelete((XClass*)te);
}
#endif /* XMIMEDATA_ON */

#endif /* XWIDGET_ON && XABSTRACTSCROLLAREA_ON && XPLAINTEXTEDIT_ON && XTEXTEDIT_ON */
