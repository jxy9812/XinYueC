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
 *             - **字体属性真实下发**（P0-22/23/70/71/E2）：族/字重/斜体/
 *               下划线落内嵌编辑器字体，setBold/isBold 归一 m_fontWeight；
 *             - **外观覆盖下发**（P0-24/25）：文字色/文本底色下发控制器；
 *             - **整篇对齐**（P0-26）：对齐下发控制器，光标矩形/命中随
 *               对齐偏移（draw 像素级验证底色与正文色）；
 *             - **append 的 AutoText 探测**（P0-27/E3）：富文本追加到
 *               文档尾、探测统一走 XTextDocument_mightBeRichText；
 *             - **setHtml 超长输入不截断**（P0-E1）；
 *             - **inputMethodQuery 双参重载**（P3-05）：转发内嵌编辑器、
 *               argument 透传；
 *             - **XTextControl 文档所有权**（P0-28）与 **外开链接兜底**
 *               （P0-12）：控制器层契约（XLabel.h/.c 注释订正由 B5b 批改）。
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
#include "XEvent.h"
#include "XPainter.h"
#include "XImage.h"
#include "XVarList.h"
#if XINPUTMETHOD_ON
#include "XInputMethod.h"
#include "XVariant.h"
#endif

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
       cursorWidth 1、tabStopDistance 80、centerOnScroll false（P2-24/25
       后由内嵌编辑器承载）、backgroundVisible true、overwriteMode false、
       lineWrap WidgetWidth。 */
    XQ_CHECK(!XTextEdit_isReadOnly(te));
    XQ_CHECK(XTextEdit_acceptRichText(te));
    XQ_EQ_I64(XTextEdit_autoFormatting(te), 0);
    XQ_EQ_I64(XTextEdit_cursorWidth(te), 1);
    XQ_CHECK(XTextEdit_tabStopDistance(te) == 80.0);
    XQ_CHECK(!XPlainTextEdit_centerOnScroll(te->m_editor));
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
    XTextEdit_setReadOnly(te, true);
    XQ_CHECK(XTextEdit_isReadOnly(te));
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
    XTextEdit_selectAll(te);
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
    XTextEdit_setReadOnly(te, true);
    {
        char* before = xte_plain(te);
        XTextEdit_insertFromMimeData(te, mime);
        plain = xte_plain(te);
        XQ_REQUIRE(before != NULL && plain != NULL);
        XQ_EQ_STR(plain, before);
        XFree_System(before);
        XFree_System(plain);
    }
    XTextEdit_setReadOnly(te, false);
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

/* ==================== 本批新增（P0-12/22/23/24/25/26/27/28/70/71/
 *                       E1/E2/E3、P3-05）的公共工具 ==================== */

/** @brief 取内嵌编辑器控制器（外观覆盖/文档所有权契约的承载对象）。 */
static XTextControl* xte_ctl(const XTextEdit* te)
{
    return (te && te->m_editor) ? te->m_editor->m_control : NULL;
}

/** @brief 把控制器绘制到 w×h 白底图像并取样一点（XImage 用毕即释放）。 */
static uint32_t xte_ctlDrawPixel(XTextControl* ctl, int w, int h, int px,
                                 int py)
{
    XImage image;
    XPainter painter;
    uint32_t color;
    XImage_init_ex(&image, w, h, XImageFormat_ARGB32);
    XImage_fill(&image, 0xFFFFFFFFu);
    XPainter_init(&painter, NULL);
    if (!XPainter_begin_image(&painter, &image)) {
        XPainter_deinit(&painter);
        XClassDeinit((XClass*)&image);
        return 0u;
    }
    XTextControl_draw(ctl, &painter, NULL);
    XPainter_end(&painter);
    XPainter_deinit(&painter);
    color = XImage_pixel(&image, px, py);
    XClassDeinit((XClass*)&image);
    return color;
}

XQ_TEST(XTextEdit, fontPropertiesDeliverToEditor)
{
    XTextEdit* te = XTextEdit_create(NULL, 0);
    XFont font;
    XQ_REQUIRE(te != NULL);
    XQ_REQUIRE(te->m_editor != NULL);

    /* P0-22：setFontFamily 真实下发内嵌编辑器（旧行为只存族名）。 */
    XTextEdit_setFontFamily(te, "SimSun");
    font = XWidget_font((XWidget*)te->m_editor);
    XQ_EQ_STR(XFont_family(&font), "SimSun");
    XClassDeinit((XClass*)&font);

    /* P0-23/E2：setFontWeight 下发字重；setBold/isBold/currentFont 同源。 */
    XTextEdit_setFontWeight(te, 700);
    font = XWidget_font((XWidget*)te->m_editor);
    XQ_EQ_I64(XFont_weight(&font), 700);
    XClassDeinit((XClass*)&font);
    XQ_EQ_I64(XTextEdit_fontWeight(te), (int)XFont_Bold);
    XQ_CHECK(XTextEdit_isBold(te)); /* setBold/isBold 降为 weight 便捷入口 */
    {
        XFont cur = XTextEdit_currentFont(te);
        XQ_EQ_I64(XFont_weight(&cur), 700); /* currentFont 只读同源字段 */
        XClassDeinit((XClass*)&cur);
    }
    XTextEdit_setBold(te, false);
    XQ_EQ_I64(XTextEdit_fontWeight(te), (int)XFont_Normal);
    XQ_CHECK(!XTextEdit_isBold(te));
    XTextEdit_setBold(te, true);
    XQ_EQ_I64(XTextEdit_fontWeight(te), (int)XFont_Bold);
    XQ_CHECK(XTextEdit_isBold(te));

    /* P0-70/71：斜体/下划线（宏别名同路）真实下发编辑器字体。 */
    XTextEdit_setFontItalic(te, true);
    XTextEdit_setFontUnderline(te, true);
    font = XWidget_font((XWidget*)te->m_editor);
    XQ_CHECK(XFont_italic(&font));
    XQ_CHECK(XFont_underline(&font));
    XQ_CHECK(XTextEdit_fontItalic(te));
    XQ_CHECK(XTextEdit_fontUnderline(te));
    XClassDeinit((XClass*)&font);

    /* setPlainText 复位字符格式：粗体复位即字重复位（P0-E2）。 */
    XTextEdit_setPlainText(te, "reset");
    XQ_CHECK(!XTextEdit_isBold(te));
    XQ_CHECK(!XTextEdit_isItalic(te));
    XQ_CHECK(!XTextEdit_isUnderline(te));
    font = XWidget_font((XWidget*)te->m_editor);
    XQ_EQ_I64(XFont_weight(&font), (int)XFont_Normal);
    XQ_CHECK(!XFont_italic(&font));
    XQ_CHECK(!XFont_underline(&font));
    XClassDeinit((XClass*)&font);

    /* 族名 NULL/空串 = 回落当前字体（不改编辑器字体）。 */
    XTextEdit_setFontFamily(te, NULL);
    XQ_EQ_STR(XTextEdit_fontFamily(te), "");
    font = XWidget_font((XWidget*)te->m_editor);
    XQ_EQ_STR(XFont_family(&font), "SimSun");
    XClassDeinit((XClass*)&font);

    XClassDelete((XClass*)te);
}

XQ_TEST(XTextEdit, appearanceDeliverToControl)
{
    XTextEdit* te = XTextEdit_create(NULL, 0);
    XTextControl* ctl;
    XQ_REQUIRE(te != NULL);
    ctl = xte_ctl(te);
    XQ_REQUIRE(ctl != NULL);

    /* P0-24：文字色下发控制器覆盖（0 = 清除覆盖）。 */
    XTextEdit_setTextColor(te, 0xFF00FF00u);
    XQ_EQ_I64((long long)XTextControl_textColor(ctl), (long long)0xFF00FF00u);
    XTextEdit_setTextColor(te, 0u);
    XQ_EQ_I64((long long)XTextControl_textColor(ctl), 0);

    /* P0-25：文本底色下发控制器覆盖（0 = 无底色）。 */
    XTextEdit_setTextBackgroundColor(te, 0xFF102030u);
    XQ_EQ_I64((long long)XTextControl_textBackgroundColor(ctl),
              (long long)0xFF102030u);
    XTextEdit_setTextBackgroundColor(te, 0u);
    XQ_EQ_I64((long long)XTextControl_textBackgroundColor(ctl), 0);

    /* P0-26：对齐下发控制器（Qt::Alignment 数值直传）。 */
    XTextEdit_setAlignment(te, (int)XTextControlAlign_HCenter);
    XQ_EQ_I64(XTextControl_alignment(ctl), (int)XTextControlAlign_HCenter);
    XQ_EQ_I64(XTextEdit_alignment(te), (int)XTextControlAlign_HCenter);
    XTextEdit_setAlignment(te, (int)XTextControlAlign_Left);
    XQ_EQ_I64(XTextControl_alignment(ctl), (int)XTextControlAlign_Left);

    XClassDelete((XClass*)te);
}

#if XWINDOWEVENT_ON
XQ_TEST(XTextControl, appearanceDrawAndAlignGeometry)
{
    XTextControl* ctl = XTextControl_create();
    XFont font;
    int rowW;
    int lineH;
    XRect cursor;
    XPoint pt;
    XQ_REQUIRE(ctl != NULL);

    XTextControl_setPlainText(ctl, "MMMM");
    XTextControl_font(ctl, &font);
    rowW = XPainter_textWidthRange(&font, "MMMM", 0, 4);
    lineH = XPainter_textAscent(&font) + XPainter_textDescent(&font);
    if (lineH <= 0) lineH = 16;
    XClassDeinit((XClass*)&font);
    XQ_REQUIRE(rowW > 2 && rowW < 200);
    /* 折行宽 = 3 倍行宽：右/中对齐的余量按实测行宽推导（不写死像素）。 */
    XTextControl_setTextWidth(ctl, rowW * 3);

    /* P0-25：文本底色覆盖铺满行带（行宽 × 行高），行带外是宿主背景。 */
    XTextControl_setTextBackgroundColor(ctl, 0xFF112233u);
    XQ_CHECK_MSG(xte_ctlDrawPixel(ctl, rowW * 3, lineH, rowW - 1,
                                  lineH - 1) == 0xFF112233u,
                 "文本底色应铺满行带（取样点在字形下降部、非字形像素）");
    XQ_CHECK_MSG(xte_ctlDrawPixel(ctl, rowW * 3, lineH, rowW * 3 - 1,
                                  lineH - 1) == 0xFFFFFFFFu,
                 "行带之外保持宿主背景（底色只画到行宽）");
    XTextControl_setTextBackgroundColor(ctl, 0u);

    /* P0-24：正文色覆盖被 draw 消费——正文/组合下划线/光标共用同一
       取色变量（此处用光标 fillRect 取样，颜色必然精确）。 */
    XTextControl_setTextColor(ctl, 0xFF00FF00u);
    XTextControl_setFocus(ctl, true, XFocusReason_Mouse);
    XQ_CHECK_MSG(xte_ctlDrawPixel(ctl, rowW * 3, lineH, 0, lineH - 1) ==
                     0xFF00FF00u,
                 "左对齐时光标落在 x=0，取样点应为正文色覆盖（fillRect 精确）");
    XTextControl_setTextColor(ctl, 0u);
    XQ_CHECK_MSG(xte_ctlDrawPixel(ctl, rowW * 3, lineH, 0, lineH - 1) ==
                     0xFF000000u,
                 "清除覆盖后回落调色板 Text（缺省黑，不再是覆盖色）");
    XTextControl_setFocus(ctl, false, XFocusReason_Mouse);

    /* P0-26：对齐改行起笔 x；光标矩形、命中测试同源消费。 */
    XTextControl_setAlignment(ctl, (int)XTextControlAlign_Left);
    cursor = XTextControl_cursorRectAt(ctl, 0);
    XQ_EQ_I64(cursor.x, 0);
    pt.x = rowW * 3 - 2;
    pt.y = 2;
    XQ_CHECK_MSG(XTextControl_hitTest(ctl, &pt,
                 (int)XTextControlHitTestAccuracy_ExactHit) == -1,
                 "左对齐时文本区之外的点精确命中失败");

    XTextControl_setAlignment(ctl, (int)XTextControlAlign_Right);
    cursor = XTextControl_cursorRectAt(ctl, 0);
    XQ_CHECK_MSG(cursor.x == rowW * 2,
                 "右对齐光标 x = 3倍行宽 - 行宽 = 2倍行宽");
    XQ_CHECK_MSG(XTextControl_hitTest(ctl, &pt,
                 (int)XTextControlHitTestAccuracy_ExactHit) >= 0,
                 "右对齐后同一点落在文本区内（命中随对齐偏移）");

    XTextControl_setAlignment(ctl, (int)XTextControlAlign_HCenter);
    cursor = XTextControl_cursorRectAt(ctl, 0);
    XQ_CHECK_MSG(cursor.x == rowW, "居中光标 x = (3倍行宽 - 行宽)/2 = 行宽");

    XClassDelete((XClass*)ctl);
}
#endif /* XWINDOWEVENT_ON */

XQ_TEST(XTextEdit, appendAutoTextAndMightBeRichText)
{
    XTextEdit* te = XTextEdit_create(NULL, 0);
    char* plain;
    XQ_REQUIRE(te != NULL);

#if XTEXTDOCUMENT_ON
    /* P0-27：append 富文本分支追加到文档尾（不是显示标签原文）。 */
    XTextEdit_setPlainText(te, "L1");
    XTextEdit_append(te, "<b>L2</b>");
    plain = xte_plain(te);
    XQ_REQUIRE(plain != NULL);
    XQ_EQ_STR(plain, "L1\nL2");
    XFree_System(plain);
    {
        char* html = XTextEdit_toHtml(te);
        XQ_REQUIRE(html != NULL);
        /* 真源位口径与 setHtml 一致：追加后 toHtml 直接导出文档（含粗体）；
           若 m_docRichSource 未置位会回落编辑器纯文本导出、丢 <b>。 */
        XQ_CHECK_MSG(strstr(html, "<b>") != NULL,
                     "append 富文本后 toHtml 保留追加段格式：%s", html);
        XFree_System(html);
    }
    /* 纯文本分支不回归：AutoText 判定非富文本仍走 appendPlainText。 */
    XTextEdit_append(te, "L3");
    plain = xte_plain(te);
    XQ_REQUIRE(plain != NULL);
    XQ_EQ_STR(plain, "L1\nL2\nL3");
    XFree_System(plain);

    /* P0-E3：探测统一走公共口 XTextDocument_mightBeRichText——"&lt;"
       实体判富文本（旧私有副本判纯文本）。 */
    XTextEdit_setText(te, "a &lt; b");
    plain = xte_plain(te);
    XQ_REQUIRE(plain != NULL);
    XQ_EQ_STR(plain, "a &lt; b"); /* 编辑缓冲剥离口径不变（实体不展开） */
    XFree_System(plain);
    {
        char* docPlain = XTextDocument_toPlainText(XTextEdit_document(te));
        XQ_REQUIRE(docPlain != NULL);
        /* 富分支：文档按 HTML 解析展开实体（旧私有副本会把整串当纯文本
           写文档，得到 "a &lt; b"）。 */
        XQ_EQ_STR(docPlain, "a < b");
        XFree_System(docPlain);
    }
#else
    XTextEdit_setPlainText(te, "L1");
    XTextEdit_append(te, "<b>L2</b>");
    plain = xte_plain(te);
    XQ_REQUIRE(plain != NULL);
    XQ_EQ_STR(plain, "L1\nL2");
    XFree_System(plain);
#endif

    XClassDelete((XClass*)te);
}

XQ_TEST(XTextEdit, setHtmlLongInputNotTruncated)
{
    XTextEdit* te = XTextEdit_create(NULL, 0);
    const int kBodyLen = 5000; /* 旧实现 4096 栈缓冲：>4095 字节被截断。 */
    char* html;
    char* plain;
    int i;
    size_t off;
    XQ_REQUIRE(te != NULL);

    html = (char*)XMalloc_System((size_t)kBodyLen + 16);
    XQ_REQUIRE(html != NULL);
    off = 0;
    html[off++] = '<'; html[off++] = 'b'; html[off++] = '>';
    for (i = 0; i < kBodyLen; ++i) html[off++] = 'a';
    html[off++] = '<'; html[off++] = '/'; html[off++] = 'b'; html[off++] = '>';
    html[off] = '\0';

    XTextEdit_setHtml(te, html);
    XFree_System(html);
    XQ_CHECK(XTextEdit_isBold(te)); /* 行内格式终态不因长度变化 */

    plain = xte_plain(te);
    XQ_REQUIRE(plain != NULL);
    XQ_EQ_SIZE(XStrlen(plain), (size_t)kBodyLen); /* P0-E1：不静默截断 */
    for (i = 0; i < kBodyLen; ++i) {
        if (plain[i] != 'a') break;
    }
    XQ_EQ_I64(i, kBodyLen);
    XFree_System(plain);

    XClassDelete((XClass*)te);
}

#if XTEXTDOCUMENT_ON
XQ_TEST(XTextControl, documentOwnershipExternal)
{
    XTextControl* ctl;
    XTextDocument* doc;
    XTextDocument* doc2;

    /* P0-28：setDocument 不接管所有权（Qt QWidgetTextControl 同口径）：
       控件析构后外部文档仍归调用方可用。 */
    ctl = XTextControl_create();
    doc = XTextDocument_create();
    XQ_REQUIRE(ctl != NULL && doc != NULL);
    XTextControl_setDocument(ctl, doc);
    XQ_CHECK(XTextControl_document(ctl) == doc);
    XClassDelete((XClass*)ctl);
    XTextDocument_setPlainText(doc, "x"); /* 未被控件释放 ⇒ 仍可写 */
    XQ_EQ_I64(XTextDocument_blockCount(doc), 1);
    XQ_CHECK(!XTextDocument_isEmpty(doc));
    XClassDelete((XClass*)doc);

    /* 连续 setDocument 也不误删外部文档。 */
    ctl = XTextControl_create();
    doc = XTextDocument_create();
    doc2 = XTextDocument_create();
    XQ_REQUIRE(ctl != NULL && doc != NULL && doc2 != NULL);
    XTextControl_setDocument(ctl, doc);
    XTextControl_setDocument(ctl, doc2);
    XQ_CHECK(XTextControl_document(ctl) == doc2);
    XClassDelete((XClass*)ctl);
    XTextDocument_setPlainText(doc, "y");
    XQ_EQ_I64(XTextDocument_blockCount(doc), 1);
    XTextDocument_setPlainText(doc2, "z");
    XQ_EQ_I64(XTextDocument_blockCount(doc2), 1);
    XClassDelete((XClass*)doc);
    XClassDelete((XClass*)doc2);

    /* setDocument(NULL) 重置为内部默认文档（控制器拥有并在析构释放）。 */
    ctl = XTextControl_create();
    XQ_REQUIRE(ctl != NULL);
    XTextControl_setDocument(ctl, NULL);
    XQ_CHECK(XTextControl_document(ctl) != NULL);
    XClassDelete((XClass*)ctl);
}
#endif /* XTEXTDOCUMENT_ON */

/* ---- P0-12：openExternalLinks 外开判定 + 打开失败兜底 linkActivated ---- */

static int g_xqcLinkActivated;
static char g_xqcLastLink[64];

static void xqc_linkActivatedSlot(XObject* receiver, XVarList* args)
{
    XVarList_args_1(args, char*, link);
    (void)receiver;
    ++g_xqcLinkActivated;
    g_xqcLastLink[0] = '\0';
    if (link) {
        strncpy(g_xqcLastLink, link, sizeof(g_xqcLastLink) - 1u);
        g_xqcLastLink[sizeof(g_xqcLastLink) - 1u] = '\0';
    }
}

#if defined(__linux__) && !defined(__ANDROID__)
#include <stdlib.h>
#endif

XQ_TEST(XTextControl, linkOpenExternalFallback)
{
    XTextControl* ctl = XTextControl_create();
    XPoint pos;
    XMouseEvent press;
    XMouseEvent release;
    XQ_REQUIRE(ctl != NULL);

    XTextControl_setTextWidth(ctl, 200);
    XTextControl_setHtml(ctl, "<a href=\"xq://ctl\">CLICK</a>");
    XTextControl_setTextInteractionFlags(
        ctl, (int)XTextControlInteraction_TextBrowserInteraction);

    /* 开关往返（QLabel::setOpenExternalLinks 属性 WRITE/READ）。 */
    XQ_CHECK(!XTextControl_openExternalLinks(ctl));
    XTextControl_setOpenExternalLinks(ctl, true);
    XQ_CHECK(XTextControl_openExternalLinks(ctl));

    g_xqcLinkActivated = 0;
    g_xqcLastLink[0] = '\0';
    XObject_connect((XObject*)ctl,
                      (size_t)XTextControl_linkActivated_signal(NULL, NULL),
                      (XObject*)ctl, xqc_linkActivatedSlot,
                      XConnectionType_Direct);

    XPoint_init(&pos, 2, 2);
    XMouseEvent_init(&press, XEVENT_TYPE_MOUSE_BUTTON_PRESS,
                     XMouseButton_LeftButton, 0, pos);
    XMouseEvent_init(&release, XEVENT_TYPE_MOUSE_BUTTON_RELEASE,
                     XMouseButton_LeftButton, 0, pos);

#if defined(__linux__) && !defined(__ANDROID__)
    {
        /* posix 平台后端可用性由 DISPLAY/WAYLAND_DISPLAY 决定：摘掉环境
           ⇒ XPlatformServices_openUrl_2 恒 false ⇒ P0-12 兜底必须仍发
           linkActivated（内嵌端不得静默丢信号）。用毕恢复进程环境。 */
        const char* oldDisplay = getenv("DISPLAY");
        const char* oldWayland = getenv("WAYLAND_DISPLAY");
        char displayBuf[256];
        char waylandBuf[256];
        displayBuf[0] = '\0';
        waylandBuf[0] = '\0';
        if (oldDisplay) {
            strncpy(displayBuf, oldDisplay, sizeof(displayBuf) - 1u);
            displayBuf[sizeof(displayBuf) - 1u] = '\0';
        }
        if (oldWayland) {
            strncpy(waylandBuf, oldWayland, sizeof(waylandBuf) - 1u);
            waylandBuf[sizeof(waylandBuf) - 1u] = '\0';
        }
        unsetenv("DISPLAY");
        unsetenv("WAYLAND_DISPLAY");
        XTextControl_processEvent(ctl, (XEvent*)&press);
        XTextControl_processEvent(ctl, (XEvent*)&release);
        XQ_EQ_I64(g_xqcLinkActivated, 1);
        XQ_EQ_STR(g_xqcLastLink, "xq://ctl");
        if (displayBuf[0] != '\0') setenv("DISPLAY", displayBuf, 1);
        else unsetenv("DISPLAY");
        if (waylandBuf[0] != '\0') setenv("WAYLAND_DISPLAY", waylandBuf, 1);
        else unsetenv("WAYLAND_DISPLAY");
    }
#else
    /* 非 Linux（Windows ShellExecute 会真开浏览器）：只覆盖开关往返与
       事件路径不崩，激活兜底断言仅在 posix 平台执行。 */
#endif

    XClassDelete((XClass*)ctl);
}

#if XINPUTMETHOD_ON
XQ_TEST(XTextEdit, inputMethodQueryWithArgument)
{
    XTextEdit* te = XTextEdit_create(NULL, 0);
    XVariant* single;
    XVariant* dual;
    XVariant* arg;
    int32_t docPos = 3;
    XQ_REQUIRE(te != NULL);
    XTextEdit_setPlainText(te, "hello");

    /* P3-05：双参重载转发内嵌编辑器——与单参虚槽结果逐项一致。 */
    single = XWidget_inputMethodQuery(
        (const XWidget*)te, XInputMethodQuery_ImCursorRectangle);
    dual = XTextEdit_inputMethodQuery_2(
        te, (int)XInputMethodQuery_ImCursorRectangle, NULL);
    XQ_REQUIRE(single != NULL && dual != NULL);
    XQ_EQ_SIZE(XVariant_dataSize(single), XVariant_dataSize(dual));
    if (XVariant_dataSize(single) > 0) {
        XQ_CHECK(XMemcmp(XVariant_data(single), XVariant_data(dual),
                         XVariant_dataSize(single)) == 0);
    }
    XClassDelete((XClass*)single);
    XClassDelete((XClass*)dual);

    /* argument 透传：非消费查询携带 Int32（文档位置）结果不变。 */
    arg = XVariant_create(&docPos, sizeof(docPos), XVariantType_Int32);
    XQ_REQUIRE(arg != NULL);
    dual = XTextEdit_inputMethodQuery_2(
        te, (int)XInputMethodQuery_ImSurroundingText, arg);
    XQ_REQUIRE(dual != NULL);
    {
        const XString* text = XString_fromVariant_const(dual);
        XQ_REQUIRE(text != NULL);
        XQ_EQ_STR(XString_toUtf8(text), "hello");
    }
    XClassDelete((XClass*)dual);
    /* arg 保留存活：ImAbsolutePosition 消费查询还要透传同一个参数
       （借用语义，inputMethodQuery_2 不接管所有权），提前释放会变成
       悬垂指针 + 尾部双重删除。 */

    /* 消费查询：ImAbsolutePosition 带 Int32 = 文档绝对位置（透传到
       XTextControl_inputMethodQuery 的 argument）。 */
    dual = XTextEdit_inputMethodQuery_2(
        te, (int)XInputMethodQuery_ImAbsolutePosition, arg);
    XQ_REQUIRE(dual != NULL);
    XQ_EQ_I64(XVariant_toInt32(dual), 3);
    XClassDelete((XClass*)dual);
    XClassDelete((XClass*)arg);

    XClassDelete((XClass*)te);
}
#endif /* XINPUTMETHOD_ON */

#endif /* XWIDGET_ON && XABSTRACTSCROLLAREA_ON && XPLAINTEXTEDIT_ON && XTEXTEDIT_ON */
