/**
 * @file       XMarkdownAlignTest.c
 * @brief      XMarkdownParser Markdown 解析落文档 × Qt 6.8 QTextMarkdownImporter
 *             对齐契约测试（§B16/W3-a）。
 * @details    对齐依据（Qt 6.8.3 qtextmarkdownimporter.cpp 原文行号 + 本机
 *             Qt 6.8.0 offscreen 探针实测；探针源码 /tmp/mdprobe1.cpp、
 *             /tmp/mdprobe2.cpp（块/行内全表）、/tmp/qtprobe_w3a.cpp、
 *             /tmp/qtprobe_w3a2.cpp（复核轮：标题内联边界、链接形态、围栏
 *             缩进、中文混合文档、QTextEdit::setMarkdown 包装）、
 *             /tmp/qtmdprobe/*（前置元材料矩阵与未配对定界符），期望值
 *             逐条取自**原文或探针输出**——toPlainText 逐字节对拍、
 *             块格式/片段格式逐字段对拍；**两处 6.8.0 二进制与 6.8.3 原文
 *             不一致处按原文实现并在用例注释与 XMarkdownParser.h 的
 *             「与 6.8.0 二进制的两处已知差异」里逐条登记**）：
 *             - ①标题六级：块 headingLevel=1..6 + 片段 Bold（qtmi.cpp:198-201；
 *               探针 headings：frag{bold sizeAdj=3..-2}——sizeAdj 在本子集
 *               无槽位，渲染层 xte_headingPermille 按 headingLevel 同源缩放，
 *               契约见 XMarkdownParser.h）；
 *             - ②段落/空行：软换行接排单空格（qtmi.cpp:476 MD_TEXT_SOFTBR→
 *               " "）、空行分段（探针 paragraphs/three-paras）；
 *             - ③无序/有序列表：isListItem+isOrdered+indentLevel（HTML `<li>`
 *               管线同承载；Qt 用 QTextList——模型边界，探针 listInd 对拍；
 *               列表 style/start/suffix 三属性契约见
 *               Test/QtAlign/XMarkdownListAlignTest.c）；
 *             - ④引用块：indentLevel=深度（Qt 落 BlockQuoteLevel 属性——
 *               模型边界；探针 quote/quote-para）；
 *             - ⑤代码块：逐行一块、空白保留、字族 monospace（qtmi.cpp:607 +
 *               探针 code-fence/indented-code）；栏内空行落空块、栏尾不落
 *               （qtmi.cpp:524-530 延后发射）；
 *             - ⑥行内样式：bold/italic/both/mono/删除线/anchorHref
 *               （qtmi.cpp cbEnterSpan:382-430；探针 inline/link）；
 *             - ⑦转义：可转义标点剥反斜杠字面落（探针 escapes/escapes2）；
 *             - ⑧嵌套：**粗内斜**三段 bold/bold+italic/bold（探针 nested）、
 *               链接文本行内嵌套（探针 link-nested-text）；
 *             - ⑧b未配对定界符**按字面落、样式不外溢**（Qt/md4c：
 *               qtmi.cpp:382-441 只对成对 span 进出；探针 unmatched-star/
 *               crossed-underscore（`**a _b**` ⇒ «a _b»{B}）/crossed-strike
 *               （`~~a *b~~ c*` ⇒ «a *b»{S}« c*»{}））；
 *             - ⑧c前置元材料边界（**语义**回归锁）：未闭合 / 恰好文档结束 /
 *               CRLF / 零内容拒绝（qtmi.cpp:146-160；全程 [0,len) 硬边界）。
 *               **普通套件不锁内存读越界**——越界由 ASan 复跑把关（复核已
 *               实测：把旧形态的指针混加减退回去，语义断言会红、ASan 才抓
 *               越界；当前实现的边界安全靠 ASan 单 TU 复跑零报告作证）；
 *             - ⑨纯文本逐字节对拍：25+ 例取自探针输出；
 *             - ⑩XTextEdit_setMarkdown 接线：编辑器纯文本=解析后纯文本
 *               （Qt QTextEdit::toPlainText 口径）、原文承载 markdown()/
 *               toMarkdown 不变；
 *             - 撤销：clear 清史 + 单编辑块 ⇒ 一步撤销、undo 后空文档
 *               （qtmi import():138/162-164 + 探针 setmd-undo）；
 *             - ⑪泄漏门：运行器逐用例零容忍（xq_align.h ③）；
 *             - ⑫既有 XTextEdit/XTextBrowser/XTextCursor/XTextFormat 套件
 *               全绿：脚本收口（--test 过滤跑）。
 * @note       只依赖公共 API + findBlockByNumber 只读块标量（XTDBlock 公共
 *             平铺结构，按名读）。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "xq_align.h"
#include "XGuiConfig.h" /* 开关头先于门控包含：关断档（-DXGUI_ON=0 /
                           -DXTEXTDOCUMENT_ON=0）也必须能干净编译，且不依赖
                           命令行 -DXGUI_ON（xq_align.h 不含配置头）。 */

#if XGUI_ON && XTEXTDOCUMENT_ON

#include "XMarkdownParser.h"
#include "XTextDocument.h"
#include "XTextCursor.h"
#include "XTextEdit.h"
#include "XStringUtils.h"
#include "XMemory.h"

/* ==================== 断言工具 ==================== */

/** @brief 解析并断言纯文本与块数逐字节对拍（Qt 探针期望直填）。 */
static void xmd_expectPlain(const char* md, int features, const char* plain,
                            int blocks, const char* what)
{
    XTextDocument* doc = XTextDocument_create();
    char* got;
    if (!doc) {
        xq_fail_at(__FILE__, __LINE__, "%s：建文档失败", what);
        return;
    }
    XMarkdownParser_parse_utf8(md, doc, features);
    got = XTextDocument_toPlainText(doc);
    xq_check_impl(got != NULL && XStrcmp(got, plain) == 0, "toPlainText",
                  __FILE__, __LINE__,
                  "%s：toPlainText=«%s» 期望 «%s»", what,
                  got ? got : "(null)", plain);
    if (got) XFree_System(got);
    xq_check_impl(XTextDocument_blockCount(doc) == blocks, "blockCount",
                  __FILE__, __LINE__,
                  "%s：blockCount=%d 期望 %d", what,
                  XTextDocument_blockCount(doc), blocks);
    XClassDelete((XClass*)doc);
}

/** @brief 断言块 bi 的标题级/缩进/列表位（XTDBlock 公共标量）。 */
static void xmd_blockScalars(const XTextDocument* doc, int bi, int heading,
                             int indent, int listItem, int ordered,
                             const char* what)
{
    const XTDBlock* b = XTextDocument_findBlockByNumber(doc, bi);
    if (!b) {
        xq_fail_at(__FILE__, __LINE__, "%s：块%d不存在", what, bi);
        return;
    }
    xq_check_impl((int)b->headingLevel == heading, "headingLevel",
                  __FILE__, __LINE__,
                  "%s：块%d heading=%d 期望 %d", what, bi,
                  (int)b->headingLevel, heading);
    xq_check_impl((int)b->indentLevel == indent, "indentLevel",
                  __FILE__, __LINE__,
                  "%s：块%d indent=%d 期望 %d", what, bi,
                  (int)b->indentLevel, indent);
    xq_check_impl((int)b->isListItem == listItem, "isListItem",
                  __FILE__, __LINE__,
                  "%s：块%d isListItem=%d 期望 %d", what, bi,
                  (int)b->isListItem, listItem);
    xq_check_impl((int)b->isOrdered == ordered, "isOrdered",
                  __FILE__, __LINE__,
                  "%s：块%d isOrdered=%d 期望 %d", what, bi,
                  (int)b->isOrdered, ordered);
}

/** @brief 断言块 bi 的片段数。 */
static void xmd_fragCount(const XTextDocument* doc, int bi, int n,
                          const char* what)
{
    xq_check_impl(XTextDocument_fragmentCount(doc, bi) == n,
                  "fragmentCount", __FILE__, __LINE__,
                  "%s：块%d片段数=%d 期望 %d", what, bi,
                  XTextDocument_fragmentCount(doc, bi), n);
}

/** @brief 断言片段标量格式位。 */
static void xmd_fragFlags(const XTextDocument* doc, int bi, int fi,
                          int bold, int italic, int underline, int strike,
                          int mono, const char* what)
{
    const XTDFragment* f = XTextDocument_fragment(doc, bi, fi);
    if (!f) {
        xq_fail_at(__FILE__, __LINE__, "%s：片段(%d,%d)不存在", what, bi, fi);
        return;
    }
    xq_check_impl((int)f->fmt.bold == bold, "frag.bold", __FILE__, __LINE__,
                  "%s：块%d片段%d bold=%d 期望 %d", what, bi, fi,
                  (int)f->fmt.bold, bold);
    xq_check_impl((int)f->fmt.italic == italic, "frag.italic", __FILE__,
                  __LINE__, "%s：块%d片段%d italic=%d 期望 %d", what, bi, fi,
                  (int)f->fmt.italic, italic);
    xq_check_impl((int)f->fmt.underline == underline, "frag.underline",
                  __FILE__, __LINE__,
                  "%s：块%d片段%d underline=%d 期望 %d", what, bi, fi,
                  (int)f->fmt.underline, underline);
    xq_check_impl((int)f->fmt.strikeOut == strike, "frag.strikeOut",
                  __FILE__, __LINE__,
                  "%s：块%d片段%d strike=%d 期望 %d", what, bi, fi,
                  (int)f->fmt.strikeOut, strike);
    xq_check_impl(mono == 0 || (f->fmt.fontFamily != NULL &&
                                XStrcmp(XString_toUtf8(f->fmt.fontFamily),
                                        "monospace") == 0),
                  "frag.mono", __FILE__, __LINE__,
                  "%s：块%d片段%d 等宽字族缺失", what, bi, fi);
}

/** @brief 断言片段锚点 href。 */
static void xmd_fragHref(const XTextDocument* doc, int bi, int fi,
                         const char* href, const char* what)
{
    const XTDFragment* f = XTextDocument_fragment(doc, bi, fi);
    const char* got;
    if (!f) {
        xq_fail_at(__FILE__, __LINE__, "%s：片段(%d,%d)不存在", what, bi, fi);
        return;
    }
    got = f->fmt.anchorHref ? XString_toUtf8(f->fmt.anchorHref) : NULL;
    xq_check_impl((href == NULL && got == NULL) ||
                      (href != NULL && got != NULL &&
                       XStrcmp(got, href) == 0),
                  "frag.anchorHref", __FILE__, __LINE__,
                  "%s：块%d片段%d href=%s 期望 %s", what, bi, fi,
                  got ? got : "(null)", href ? href : "(null)");
}

/** @brief 断言片段文本。 */
static void xmd_fragText(const XTextDocument* doc, int bi, int fi,
                         const char* text, const char* what)
{
    const XTDFragment* f = XTextDocument_fragment(doc, bi, fi);
    const char* got;
    if (!f) {
        xq_fail_at(__FILE__, __LINE__, "%s：片段(%d,%d)不存在", what, bi, fi);
        return;
    }
    got = f->text ? XString_toUtf8(f->text) : NULL;
    got = got ? got : "";
    xq_check_impl(XStrcmp(got, text) == 0, "frag.text", __FILE__, __LINE__,
                  "%s：块%d片段%d text=«%s» 期望 «%s»", what, bi, fi, got,
                  text);
}

/** @brief 断言片段已入格式池（W1 影子层契约）。 */
static void xmd_fragPooled(const XTextDocument* doc, int bi, int fi,
                           const char* what)
{
    int idx = XTextDocument_fragmentPoolIndex(doc, bi, fi);
    xq_check_impl(idx >= 0, "fragmentPoolIndex", __FILE__, __LINE__,
                  "%s：块%d片段%d 未入格式池（index=%d）", what, bi, fi, idx);
}

/* ==================== ① 标题六级 ==================== */

XQ_TEST(XMarkdown, atxHeadingLevels)
{
    XTextDocument* doc = XTextDocument_create();
    int lvl;
    XQ_REQUIRE(doc != NULL);
    XMarkdownParser_parse_utf8("# h1\n## h2\n### h3\n#### h4\n##### h5\n###### h6",
                               doc, (int)XMarkdownDialect_GitHub);
    /* Qt 探针 headings：6 块、heading=1..6、frag bold（sizeAdj 无槽位，
       渲染层按 headingLevel 缩放——契约见 XMarkdownParser.h）。 */
    XQ_EQ_I64(XTextDocument_blockCount(doc), 6);
    for (lvl = 0; lvl < 6; ++lvl) {
        char what[64];
        XSnprintf(what, sizeof(what), "headings h%d", lvl + 1);
        xmd_blockScalars(doc, lvl, lvl + 1, 0, 0, 0, what);
        xmd_fragCount(doc, lvl, 1, what);
        xmd_fragFlags(doc, lvl, 0, 1, 0, 0, 0, 0, what);
        xmd_fragPooled(doc, lvl, 0, what);
    }
    XClassDelete((XClass*)doc);
    /* ATX 闭尾 # 与多级混排（Qt 口径：尾 # 串剥除）。 */
    xmd_expectPlain("# heading with trailing ##", (int)XMarkdownDialect_GitHub,
                    "heading with trailing", 1, "atx-close");
    xmd_expectPlain("# solo", (int)XMarkdownDialect_GitHub, "solo", 1, "atx-solo");
}

/* ==================== ② 段落/空行 ==================== */

XQ_TEST(XMarkdown, paragraphsAndBlankLines)
{
    /* 软换行接排单空格（qtmi.cpp:476 + 探针 softbreak/paragraphs）。 */
    xmd_expectPlain("soft\nwrap", (int)XMarkdownDialect_GitHub, "soft wrap", 1,
                    "softbreak");
    xmd_expectPlain("para one\npara two", (int)XMarkdownDialect_GitHub,
                    "para one para two", 1, "paragraphs");
    /* 空行分段（探针 three-paras）。 */
    xmd_expectPlain("para1\n\npara2\n\npara3", (int)XMarkdownDialect_GitHub,
                    "para1\npara2\npara3", 3, "three-paras");
    /* 空文档与纯空行：清空复位（Qt setMarkdown("") ⇒ isEmpty）。 */
    {
        XTextDocument* doc = XTextDocument_create();
        XQ_REQUIRE(doc != NULL);
        XMarkdownParser_parse_utf8("", doc, (int)XMarkdownDialect_GitHub);
        XQ_CHECK(XTextDocument_isEmpty(doc));
        XQ_EQ_I64(XTextDocument_blockCount(doc), 1);
        XQ_CHECK(!XTextDocument_isModified(doc));
        XQ_CHECK(!XTextDocument_isUndoAvailable(doc));
        XClassDelete((XClass*)doc);
    }
    /* 空行中的悬挂硬换行标记不落（段终点丢弃——探针 hardbreak 末行干净）。 */
    xmd_expectPlain("a  \n\nb", (int)XMarkdownDialect_GitHub, "a\nb", 2,
                    "hang-drop");
}

/* ==================== ③ 无序/有序列表 ==================== */

XQ_TEST(XMarkdown, unorderedAndOrderedLists)
{
    XTextDocument* doc = XTextDocument_create();
    XQ_REQUIRE(doc != NULL);
    /* 探针 unordered：4 块各 isListItem、indent=1（Qt QTextList style
       -1/-2/-3 的标记符差异已由 §B5 落模型——本用例仍只锁标量位，
       style/start/suffix 契约见 Test/QtAlign/XMarkdownListAlignTest.c）。 */
    XMarkdownParser_parse_utf8("- a\n- b\n* c\n+ d", doc,
                               (int)XMarkdownDialect_GitHub);
    XQ_EQ_I64(XTextDocument_blockCount(doc), 4);
    xmd_blockScalars(doc, 0, 0, 1, 1, 0, "ul a");
    xmd_blockScalars(doc, 1, 0, 1, 1, 0, "ul b");
    xmd_blockScalars(doc, 2, 0, 1, 1, 0, "ul c");
    xmd_blockScalars(doc, 3, 0, 1, 1, 0, "ul d");
    XClassDelete((XClass*)doc);

    /* 有序：isOrdered=1（Qt ListDecimal+Suffix+Start 三属性已由 §B5 落块，
       本用例只锁有序位；三属性契约见 XMarkdownListAlignTest.c）。 */
    doc = XTextDocument_create();
    XQ_REQUIRE(doc != NULL);
    XMarkdownParser_parse_utf8("1. one\n2. two", doc,
                               (int)XMarkdownDialect_GitHub);
    XQ_EQ_I64(XTextDocument_blockCount(doc), 2);
    xmd_blockScalars(doc, 0, 0, 1, 1, 1, "ol one");
    xmd_blockScalars(doc, 1, 0, 1, 1, 1, "ol two");
    XClassDelete((XClass*)doc);
    /* 有序起始号 5（Qt ListStart=5：模型值与渲染序号见
       XMarkdownListAlignTest.c；本用例只锁纯文本）。 */
    xmd_expectPlain("5. five\n6. six", (int)XMarkdownDialect_GitHub,
                    "five\nsix", 2, "ordered-start");

    /* 嵌套：indent 1/2/3（探针 nested-list）。 */
    doc = XTextDocument_create();
    XQ_REQUIRE(doc != NULL);
    XMarkdownParser_parse_utf8("- l1\n  - l2\n    - l3", doc,
                               (int)XMarkdownDialect_GitHub);
    XQ_EQ_I64(XTextDocument_blockCount(doc), 3);
    xmd_blockScalars(doc, 0, 0, 1, 1, 0, "nest l1");
    xmd_blockScalars(doc, 1, 0, 2, 1, 0, "nest l2");
    xmd_blockScalars(doc, 2, 0, 3, 1, 0, "nest l3");
    XClassDelete((XClass*)doc);

    /* 列表项行内格式与列表后段落（探针 mixed）。 */
    doc = XTextDocument_create();
    XQ_REQUIRE(doc != NULL);
    XMarkdownParser_parse_utf8("# Heading\n- item **b**\n\npara after list",
                               doc, (int)XMarkdownDialect_GitHub);
    XQ_EQ_I64(XTextDocument_blockCount(doc), 3);
    xmd_fragFlags(doc, 1, 1, 1, 0, 0, 0, 0, "mixed item bold");
    xmd_blockScalars(doc, 2, 0, 0, 0, 0, "mixed para-after-list");
    XClassDelete((XClass*)doc);

    /* 缩进续行接排 + 松散列表跨空档（探针 list-cont/loose-ul）。 */
    xmd_expectPlain("- item\n  continued indented\n- two",
                    (int)XMarkdownDialect_GitHub,
                    "item continued indented\ntwo", 2, "list-cont");
    xmd_expectPlain("- a\n\n- b", (int)XMarkdownDialect_GitHub, "a\nb", 2,
                    "loose-ul");
    xmd_expectPlain("1. a\n\n1. b", (int)XMarkdownDialect_GitHub, "a\nb", 2,
                    "ordered-restart");
    /* 列表项内硬换行拆块且留列表（探针 list-hardbreak）。 */
    xmd_expectPlain("- item with\n  hard  \n  break",
                    (int)XMarkdownDialect_GitHub, "item with hard\nbreak", 2,
                    "list-hardbreak");
}

/* ==================== ④ 引用块 ==================== */

XQ_TEST(XMarkdown, blockquote)
{
    XTextDocument* doc = XTextDocument_create();
    XQ_REQUIRE(doc != NULL);
    /* 探针 quote：惰性续行（无 `>` 行接排）一块 indent=1。 */
    XMarkdownParser_parse_utf8("> quoted\n> more\nafter", doc,
                               (int)XMarkdownDialect_GitHub);
    XQ_EQ_I64(XTextDocument_blockCount(doc), 1);
    xmd_blockScalars(doc, 0, 0, 1, 0, 0, "quote-lazy");
    XClassDelete((XClass*)doc);

    /* 探针 quote-para：引用内空行分段、两块同深度。 */
    doc = XTextDocument_create();
    XQ_REQUIRE(doc != NULL);
    XMarkdownParser_parse_utf8("> l1\n>\n> l2", doc,
                               (int)XMarkdownDialect_GitHub);
    XQ_EQ_I64(XTextDocument_blockCount(doc), 2);
    xmd_blockScalars(doc, 0, 0, 1, 0, 0, "quote-p0");
    xmd_blockScalars(doc, 1, 0, 1, 0, 0, "quote-p1");
    XClassDelete((XClass*)doc);

    /* 引用内行内格式（探针 quote-inline）。 */
    doc = XTextDocument_create();
    XQ_REQUIRE(doc != NULL);
    XMarkdownParser_parse_utf8("> quote with **bold**\n> and *em*", doc,
                               (int)XMarkdownDialect_GitHub);
    XQ_EQ_I64(XTextDocument_blockCount(doc), 1);
    xmd_fragFlags(doc, 0, 1, 1, 0, 0, 0, 0, "quote bold");
    xmd_fragFlags(doc, 0, 3, 0, 1, 0, 0, 0, "quote em");
    XClassDelete((XClass*)doc);

    /* 引用内列表（探针 list-in-quote：quote+list 双承载）。 */
    xmd_expectPlain("> - list in quote\n> - two",
                    (int)XMarkdownDialect_GitHub, "list in quote\ntwo", 2,
                    "list-in-quote");
    /* 嵌套引用 + 惰性续行（续行接排进引用段——探针 quote 同机制）。 */
    xmd_expectPlain("> > deep\nplain-tail", (int)XMarkdownDialect_GitHub,
                    "deep plain-tail", 1, "quote-nest");
}

/* ==================== ⑤ 代码块 ==================== */

XQ_TEST(XMarkdown, codeBlocks)
{
    XTextDocument* doc = XTextDocument_create();
    XQ_REQUIRE(doc != NULL);
    /* 探针 code-fence：5 块（含两空行）、空白逐字节保留、字族 monospace。 */
    XMarkdownParser_parse_utf8(
        "```py\nint x = 1;\n  keep  spaces\n\n\nafter blank\n```", doc,
        (int)XMarkdownDialect_GitHub);
    XQ_EQ_I64(XTextDocument_blockCount(doc), 5);
    xmd_fragText(doc, 0, 0, "int x = 1;", "code l0");
    xmd_fragText(doc, 1, 0, "  keep  spaces", "code l1");
    xmd_fragFlags(doc, 0, 0, 0, 0, 0, 0, 1, "code mono");
    xmd_fragFlags(doc, 1, 0, 0, 0, 0, 0, 1, "code mono l1");
    XQ_EQ_I64(XTextDocument_fragmentCount(doc, 2), 0); /* 空行块 */
    XQ_EQ_I64(XTextDocument_fragmentCount(doc, 3), 0); /* 空行块 */
    xmd_fragText(doc, 4, 0, "after blank", "code l4");
    XClassDelete((XClass*)doc);
    /* 栏尾空行不落（qtmi.cpp:524-530 延后发射——无尾随空块）。 */
    xmd_expectPlain("```\na\n\n\n```", (int)XMarkdownDialect_GitHub, "a", 1,
                    "code-trailing-blank");

    /* 缩进代码块：4 空格剥离、余空白保留、等宽（探针 indented-code）。 */
    doc = XTextDocument_create();
    XQ_REQUIRE(doc != NULL);
    XMarkdownParser_parse_utf8("    indented code\n    line2\nafter", doc,
                               (int)XMarkdownDialect_GitHub);
    XQ_EQ_I64(XTextDocument_blockCount(doc), 3);
    xmd_fragText(doc, 0, 0, "indented code", "icode l0");
    xmd_fragText(doc, 1, 0, "line2", "icode l1");
    xmd_fragFlags(doc, 0, 0, 0, 0, 0, 0, 1, "icode mono");
    xmd_blockScalars(doc, 2, 0, 0, 0, 0, "icode-after-para");
    XClassDelete((XClass*)doc);

    /* 列表项内围栏（探针 fence-in-list）。 */
    xmd_expectPlain("code fence in list:\n- ```\n  x\n  ```",
                    (int)XMarkdownDialect_GitHub, "code fence in list:\nx", 2,
                    "fence-in-list");
    /* 行内代码两侧空格剥离（CommonMark；探针 code-span-run）。 */
    doc = XTextDocument_create();
    XQ_REQUIRE(doc != NULL);
    XMarkdownParser_parse_utf8("`` `tick` `` and `a`b``", doc,
                               (int)XMarkdownDialect_GitHub);
    xmd_fragText(doc, 0, 0, "`tick`", "icspan tick");
    xmd_fragFlags(doc, 0, 0, 0, 0, 0, 0, 1, "icspan mono");
    xmd_fragText(doc, 0, 2, "a", "icspan a");
    XClassDelete((XClass*)doc);
}

/* ==================== 水平线 ==================== */

XQ_TEST(XMarkdown, horizontalRule)
{
    /* 探针 hr："t\n---\n***\na" ⇒ "t\n\n\na"（HR=空块，Qt 落
       BlockTrailingHorizontalRulerWidth 属性——模型无槽位，纯文本一致）。 */
    XTextDocument* doc = XTextDocument_create();
    XQ_REQUIRE(doc != NULL);
    XMarkdownParser_parse_utf8("# t\n---\n***\na", doc,
                               (int)XMarkdownDialect_GitHub);
    XQ_EQ_I64(XTextDocument_blockCount(doc), 4);
    xmd_blockScalars(doc, 0, 1, 0, 0, 0, "hr-h");
    XQ_EQ_I64(XTextDocument_fragmentCount(doc, 1), 0);
    XQ_EQ_I64(XTextDocument_fragmentCount(doc, 2), 0);
    xmd_blockScalars(doc, 3, 0, 0, 0, 0, "hr-tail");
    XClassDelete((XClass*)doc);
    /* 变体（探针 thematic-variants）：首线也建空块、文档尾水平线成尾块。 */
    xmd_expectPlain("---\n\n-----\n\n***\n\nx", (int)XMarkdownDialect_GitHub,
                    "\n\n\n\nx", 5, "thematic-variants");
    /* Setext（探针 setext）：before h=1 bold、after h=2 bold。 */
    doc = XTextDocument_create();
    XQ_REQUIRE(doc != NULL);
    XMarkdownParser_parse_utf8("before\n===\n\nafter\n---\nz", doc,
                               (int)XMarkdownDialect_GitHub);
    XQ_EQ_I64(XTextDocument_blockCount(doc), 3);
    xmd_blockScalars(doc, 0, 1, 0, 0, 0, "setext h1");
    xmd_fragFlags(doc, 0, 0, 1, 0, 0, 0, 0, "setext h1 bold");
    xmd_blockScalars(doc, 1, 2, 0, 0, 0, "setext h2");
    xmd_fragFlags(doc, 1, 0, 1, 0, 0, 0, 0, "setext h2 bold");
    XClassDelete((XClass*)doc);
}

/* ==================== ⑥ 行内样式 ==================== */

XQ_TEST(XMarkdown, inlineStyles)
{
    XTextDocument* doc = XTextDocument_create();
    XQ_REQUIRE(doc != NULL);
    /* 探针 inline：9 片段（样式段+空格段交替），GitHub 档 ~~del~~ 落删除线。 */
    XMarkdownParser_parse_utf8("**bold** *em* ***both*** `code` ~~del~~", doc,
                               (int)XMarkdownDialect_GitHub);
    XQ_EQ_I64(XTextDocument_blockCount(doc), 1);
    xmd_fragCount(doc, 0, 9, "inline frags");
    xmd_fragText(doc, 0, 0, "bold", "inline t0");
    xmd_fragFlags(doc, 0, 0, 1, 0, 0, 0, 0, "inline bold");
    xmd_fragText(doc, 0, 2, "em", "inline t2");
    xmd_fragFlags(doc, 0, 2, 0, 1, 0, 0, 0, "inline em");
    xmd_fragText(doc, 0, 4, "both", "inline t4");
    xmd_fragFlags(doc, 0, 4, 1, 1, 0, 0, 0, "inline bold+italic");
    xmd_fragText(doc, 0, 6, "code", "inline t6");
    xmd_fragFlags(doc, 0, 6, 0, 0, 0, 0, 1, "inline code mono");
    xmd_fragText(doc, 0, 8, "del", "inline t8");
    xmd_fragFlags(doc, 0, 8, 0, 0, 0, 1, 0, "inline del strike");
    xmd_fragPooled(doc, 0, 0, "inline pooled");
    XClassDelete((XClass*)doc);
}

/* ==================== 链接 ==================== */

XQ_TEST(XMarkdown, links)
{
    XTextDocument* doc = XTextDocument_create();
    XQ_REQUIRE(doc != NULL);
    /* 探针 link：锚文本单片段、href 逐字节（含查询串 &）。 */
    XMarkdownParser_parse_utf8("[text](https://example.com/x?a=1&b=2)", doc,
                               (int)XMarkdownDialect_GitHub);
    XQ_EQ_I64(XTextDocument_blockCount(doc), 1);
    xmd_fragCount(doc, 0, 1, "link frags");
    xmd_fragText(doc, 0, 0, "text", "link text");
    xmd_fragHref(doc, 0, 0, "https://example.com/x?a=1&b=2", "link href");
    XClassDelete((XClass*)doc);

    /* 探针 link-title：标题不落（tooltip 无槽位）。 */
    doc = XTextDocument_create();
    XQ_REQUIRE(doc != NULL);
    XMarkdownParser_parse_utf8("[t](u \"title\")", doc,
                               (int)XMarkdownDialect_GitHub);
    xmd_fragText(doc, 0, 0, "t", "link-title text");
    xmd_fragHref(doc, 0, 0, "u", "link-title href");
    XClassDelete((XClass*)doc);

    /* 尖括号自动链接（两档皆可——探针 autolink-cm）。 */
    doc = XTextDocument_create();
    XQ_REQUIRE(doc != NULL);
    XMarkdownParser_parse_utf8("go <https://qt.io> now", doc,
                               (int)XMarkdownDialect_GitHub);
    xmd_fragCount(doc, 0, 3, "autolink frags");
    xmd_fragText(doc, 0, 1, "https://qt.io", "autolink text");
    xmd_fragHref(doc, 0, 1, "https://qt.io", "autolink href");
    XClassDelete((XClass*)doc);

    /* GitHub 宽容自动链接（探针 bare-autolink-gh：www. 补 http://）。 */
    doc = XTextDocument_create();
    XQ_REQUIRE(doc != NULL);
    XMarkdownParser_parse_utf8("visit www.example.com and https://x.org", doc,
                               (int)XMarkdownDialect_GitHub);
    xmd_fragHref(doc, 0, 1, "http://www.example.com", "www-autolink href");
    xmd_fragHref(doc, 0, 3, "https://x.org", "url-autolink href");
    XClassDelete((XClass*)doc);
    /* CommonMark 档无宽容自动链接（字面落）。 */
    xmd_expectPlain("visit www.example.com", (int)XMarkdownDialect_CommonMark,
                    "visit www.example.com", 1, "www-cm-literal");
}

/* ==================== ⑦ 转义 ==================== */

XQ_TEST(XMarkdown, escapes)
{
    /* 探针 escapes：可转义标点剥反斜杠字面落、不成样式。 */
    xmd_expectPlain("\\*not em\\* and \\`not code\\`",
                    (int)XMarkdownDialect_GitHub, "*not em* and `not code`", 1,
                    "escapes");
    /* 探针 escapes2：\\ 字面、\[x\](y) 不成链接。 */
    xmd_expectPlain("\\* \\_ \\\\ \\[x\\](y)", (int)XMarkdownDialect_GitHub,
                    "* _ \\ [x](y)", 1, "escapes2");
    /* 实体解码（Qt 走 insertHtml；探针 entities/entities2）。 */
    xmd_expectPlain("&amp; &lt; &gt; &quot; &#65;",
                    (int)XMarkdownDialect_GitHub, "& < > \" A", 1,
                    "entities");
    xmd_expectPlain("&#x41;&#66;end", (int)XMarkdownDialect_GitHub, "ABend", 1,
                    "entities2");
    /* 非法实体按字面（md4c 口径）。 */
    xmd_expectPlain("&nosuch; &5x;", (int)XMarkdownDialect_GitHub,
                    "&nosuch; &5x;", 1, "entities-invalid");
}

/* ==================== ⑧ 嵌套 ==================== */

XQ_TEST(XMarkdown, nesting)
{
    XTextDocument* doc = XTextDocument_create();
    XQ_REQUIRE(doc != NULL);
    /* 探针 nested：三段 bold / bold+italic / bold。 */
    XMarkdownParser_parse_utf8("**bold with *nested em* inside**", doc,
                               (int)XMarkdownDialect_GitHub);
    xmd_fragCount(doc, 0, 3, "nesting frags");
    xmd_fragText(doc, 0, 0, "bold with ", "nest t0");
    xmd_fragFlags(doc, 0, 0, 1, 0, 0, 0, 0, "nest outer bold");
    xmd_fragText(doc, 0, 1, "nested em", "nest t1");
    xmd_fragFlags(doc, 0, 1, 1, 1, 0, 0, 0, "nest inner bold+italic");
    xmd_fragText(doc, 0, 2, " inside", "nest t2");
    xmd_fragFlags(doc, 0, 2, 1, 0, 0, 0, 0, "nest tail bold");
    XClassDelete((XClass*)doc);

    /* 探针 link-nested-text：链接文本行内嵌套（b 粗体、t 斜体，均带 href）。 */
    doc = XTextDocument_create();
    XQ_REQUIRE(doc != NULL);
    XMarkdownParser_parse_utf8("[**b**old *t*](u)", doc,
                               (int)XMarkdownDialect_GitHub);
    xmd_fragCount(doc, 0, 3, "link-nest frags");
    xmd_fragFlags(doc, 0, 0, 1, 0, 0, 0, 0, "link-nest b bold");
    xmd_fragFlags(doc, 0, 1, 0, 0, 0, 0, 0, "link-nest old plain");
    xmd_fragFlags(doc, 0, 2, 0, 1, 0, 0, 0, "link-nest t italic");
    xmd_fragHref(doc, 0, 0, "u", "link-nest href b");
    xmd_fragHref(doc, 0, 2, "u", "link-nest href t");
    XClassDelete((XClass*)doc);

    /* 跨软换行斜体（探针 em-across-softbreak：软行仍在斜体段内）。 */
    doc = XTextDocument_create();
    XQ_REQUIRE(doc != NULL);
    XMarkdownParser_parse_utf8("text with *em\nacross lines* here", doc,
                               (int)XMarkdownDialect_GitHub);
    xmd_fragCount(doc, 0, 3, "em-across frags");
    xmd_fragText(doc, 0, 1, "em across lines", "em-across text");
    xmd_fragFlags(doc, 0, 1, 0, 1, 0, 0, 0, "em-across italic");
    XClassDelete((XClass*)doc);

    /* 词内（探针 intraword GitHub）：a_b_c 字面、a*b*c 斜体。 */
    doc = XTextDocument_create();
    XQ_REQUIRE(doc != NULL);
    XMarkdownParser_parse_utf8("a_b_c and a*b*c", doc,
                               (int)XMarkdownDialect_GitHub);
    xmd_fragText(doc, 0, 0, "a_b_c and a", "intraword t0");
    xmd_fragText(doc, 0, 1, "b", "intraword t1");
    xmd_fragFlags(doc, 0, 1, 0, 1, 0, 0, 0, "intraword b italic");
    XClassDelete((XClass*)doc);

    /* 未配对强调按字面落，且**不得污染片段格式**（探针 /tmp/qtprobe_w3a.cpp
       unmatched-star：Qt 同输入为单片段 «*abc and **def»{}，无 B/I；
       该断言的格式侧是干跑污染的回归锁——干跑每轮必须复位 spanTop）。 */
    doc = XTextDocument_create();
    XQ_REQUIRE(doc != NULL);
    XMarkdownParser_parse_utf8("*abc and **def", doc,
                               (int)XMarkdownDialect_GitHub);
    xmd_fragCount(doc, 0, 1, "unmatched frags");
    xmd_fragText(doc, 0, 0, "*abc and **def", "unmatched text");
    xmd_fragFlags(doc, 0, 0, 0, 0, 0, 0, 0, "unmatched flags");
    XClassDelete((XClass*)doc);

    /* 尾段未闭合强调：前段粗体保留、尾段纯文本且**无斜体残留**
       （探针 unclosed-tail：«bold»{B}« tail *unclosed»{}）。 */
    doc = XTextDocument_create();
    XQ_REQUIRE(doc != NULL);
    XMarkdownParser_parse_utf8("**bold** tail *unclosed", doc,
                               (int)XMarkdownDialect_GitHub);
    xmd_fragCount(doc, 0, 2, "unclosed-tail frags");
    xmd_fragFlags(doc, 0, 0, 1, 0, 0, 0, 0, "unclosed-tail bold");
    xmd_fragFlags(doc, 0, 1, 0, 0, 0, 0, 0, "unclosed-tail no-italic");
    XClassDelete((XClass*)doc);

    /* 链接文本内未闭合强调：链接文本整体为锚点、无斜体
       （探针 link-unclosed-em：«a *unclosed»{A:u}« tail»{}）。 */
    doc = XTextDocument_create();
    XQ_REQUIRE(doc != NULL);
    XMarkdownParser_parse_utf8("[a *unclosed](u) tail", doc,
                               (int)XMarkdownDialect_GitHub);
    xmd_fragCount(doc, 0, 2, "link-unclosed frags");
    xmd_fragFlags(doc, 0, 0, 0, 0, 0, 0, 0, "link-unclosed no-italic");
    xmd_fragHref(doc, 0, 0, "u", "link-unclosed href");
    xmd_fragHref(doc, 0, 1, NULL, "link-unclosed tail plain");
    XClassDelete((XClass*)doc);
}

/* ==================== 未配对定界符不得污染后续片段 ==================== */

/**
 * @test XMarkdown.unmatchedDelimiterNoLeak
 * @brief 未配对/被越过顶掉的定界符按**字面**落，样式不延续到后续片段
 *        （Qt/md4c 口径：md4c 只对成对 span 发 cbEnterSpan/cbLeaveSpan
 *        ——qtextmarkdownimporter.cpp:382-441，未配对定界符走 MD_TEXT 原样
 *        交给 CbText）。期望值逐条取自本机 Qt 6.8.0 offscreen 探针
 *        （/tmp/qtmdprobe/probe.cpp，QTextDocument::setMarkdown +
 *        MarkdownDialectGitHub，逐片段打印文本与 B/I/U/S 位）。
 */
XQ_TEST(XMarkdown, unmatchedDelimiterNoLeak)
{
    XTextDocument* doc;

    /* `**a _b**`：`_` 无 closer、被后面的 `**` 越过顶掉 ⇒ 必须字面落，
       且不得把下划线应用到「b」上（旧实现：«a »{B}«b»{B,U}，Qt：«a _b»{B}）。 */
    doc = XTextDocument_create();
    XQ_REQUIRE(doc != NULL);
    XMarkdownParser_parse_utf8("**a _b**", doc,
                               (int)XMarkdownDialect_GitHub);
    xmd_fragCount(doc, 0, 1, "unmatched-under frags");
    xmd_fragText(doc, 0, 0, "a _b", "unmatched-under text");
    xmd_fragFlags(doc, 0, 0, 1, 0, 0, 0, 0, "unmatched-under bold only");
    XClassDelete((XClass*)doc);

    /* `**a __b**`：同上（双下划线 run，GitHub 档下划线也不是斜体）。 */
    doc = XTextDocument_create();
    XQ_REQUIRE(doc != NULL);
    XMarkdownParser_parse_utf8("**a __b**", doc,
                               (int)XMarkdownDialect_GitHub);
    xmd_fragCount(doc, 0, 1, "unmatched-under2 frags");
    xmd_fragText(doc, 0, 0, "a __b", "unmatched-under2 text");
    xmd_fragFlags(doc, 0, 0, 1, 0, 0, 0, 0, "unmatched-under2 bold only");
    XClassDelete((XClass*)doc);

    /* `~~a *b~~ c*`：删除线内的未配对 `*` 是字面、不得把删除线段落吞掉
       （Qt：«a *b»{S}« c*»{}）。 */
    doc = XTextDocument_create();
    XQ_REQUIRE(doc != NULL);
    XMarkdownParser_parse_utf8("~~a *b~~ c*", doc,
                               (int)XMarkdownDialect_GitHub);
    xmd_fragCount(doc, 0, 2, "strike-unmatched frags");
    xmd_fragText(doc, 0, 0, "a *b", "strike-unmatched t0");
    xmd_fragFlags(doc, 0, 0, 0, 0, 0, 1, 0, "strike-unmatched strike kept");
    xmd_fragText(doc, 0, 1, " c*", "strike-unmatched t1");
    xmd_fragFlags(doc, 0, 1, 0, 0, 0, 0, 0, "strike-unmatched tail plain");
    XClassDelete((XClass*)doc);

    /* `a _b *c_ d*`：下划线与斜体交叉——就近配对成立、未配对的尾 `*`
       字面落（Qt：«a »{}«b *c»{U}« d*»{}）。 */
    doc = XTextDocument_create();
    XQ_REQUIRE(doc != NULL);
    XMarkdownParser_parse_utf8("a _b *c_ d*", doc,
                               (int)XMarkdownDialect_GitHub);
    xmd_fragCount(doc, 0, 3, "crossed-under-star frags");
    xmd_fragText(doc, 0, 0, "a ", "crossed-under-star t0");
    xmd_fragText(doc, 0, 1, "b *c", "crossed-under-star t1");
    xmd_fragFlags(doc, 0, 1, 0, 0, 1, 0, 0, "crossed-under-star underline");
    xmd_fragText(doc, 0, 2, " d*", "crossed-under-star t2");
    xmd_fragFlags(doc, 0, 2, 0, 0, 0, 0, 0, "crossed-under-star tail plain");
    XClassDelete((XClass*)doc);

    /* 纯文本侧逐字节对拍（字面字符不得丢失）。 */
    xmd_expectPlain("**a _b**", (int)XMarkdownDialect_GitHub, "a _b", 1,
                    "unmatched-under-plain");
    xmd_expectPlain("~~a *b~~ c*", (int)XMarkdownDialect_GitHub, "a *b c*", 1,
                    "strike-unmatched-plain");
}

/* ==================== 标题内联格式边界（Qt 探针对拍） ==================== */

XQ_TEST(XMarkdown, headingInlineFormats)
{
    /* Qt 语义：标题块基 Bold 只作用于**尚未开 span** 的文本；span 内用自身
       格式、span 关闭后回落空格式（qtextmarkdownimporter.cpp:384-386/432-441；
       探针 /tmp/qtprobe_w3a2.cpp）。 */
    XTextDocument* doc = XTextDocument_create();
    XQ_REQUIRE(doc != NULL);
    /* atx-pre-span-post: Qt 探针为 «plain »{B}«b»{B}« tail»{}；本框架的
       文档模型会把**相邻同格式**片段合拢（XTextDocument xtd_blockInsertRun
       等格式即并入，HTML 管线同口径），故渲染等价形态为
       «plain b»{B}« tail»{}——断言按「文本 + 格式位」对拍，不锁片段数。 */
    XMarkdownParser_parse_utf8("# plain **b** tail", doc,
                               (int)XMarkdownDialect_GitHub);
    XQ_EQ_I64(XTextDocument_fragmentCount(doc, 0), 2);
    xmd_fragText(doc, 0, 0, "plain b", "atx-boundary t0");
    xmd_fragFlags(doc, 0, 0, 1, 0, 0, 0, 0, "atx-boundary pre+span bold");
    xmd_fragText(doc, 0, 1, " tail", "atx-boundary t1");
    xmd_fragFlags(doc, 0, 1, 0, 0, 0, 0, 0, "atx-boundary post not bold");
    XClassDelete((XClass*)doc);

    /* atx-em-boundary: «a »{B}«i»{I}« b»{} */
    doc = XTextDocument_create();
    XQ_REQUIRE(doc != NULL);
    XMarkdownParser_parse_utf8("# a *i* b", doc, (int)XMarkdownDialect_GitHub);
    XQ_EQ_I64(XTextDocument_fragmentCount(doc, 0), 3);
    xmd_fragFlags(doc, 0, 0, 1, 0, 0, 0, 0, "atx-em pre bold");
    xmd_fragFlags(doc, 0, 1, 0, 1, 0, 0, 0, "atx-em span italic only");
    xmd_fragFlags(doc, 0, 2, 0, 0, 0, 0, 0, "atx-em post not bold");
    XClassDelete((XClass*)doc);

    /* setext-inline: «a »{B}«x»{A:u}« »{}«em»{I}——Setext 不得抹掉行内格式 */
    doc = XTextDocument_create();
    XQ_REQUIRE(doc != NULL);
    XMarkdownParser_parse_utf8("a [x](u) *em*\n===", doc,
                               (int)XMarkdownDialect_GitHub);
    XQ_EQ_I64(XTextDocument_blockCount(doc), 1);
    XQ_EQ_I64((int)XTextDocument_findBlockByNumber(doc, 0)->headingLevel, 1);
    XQ_EQ_I64(XTextDocument_fragmentCount(doc, 0), 4);
    xmd_fragText(doc, 0, 1, "x", "setext-inline link text");
    xmd_fragFlags(doc, 0, 1, 0, 0, 0, 0, 0, "setext-inline link not bold");
    xmd_fragHref(doc, 0, 1, "u", "setext-inline href kept");
    xmd_fragFlags(doc, 0, 3, 0, 1, 0, 0, 0, "setext-inline em italic kept");
    XClassDelete((XClass*)doc);

    /* atx-inline: «x»{A:u}« »{}«em»{I}（链接/斜体均无标题粗体） */
    doc = XTextDocument_create();
    XQ_REQUIRE(doc != NULL);
    XMarkdownParser_parse_utf8("# [x](u) *em*", doc,
                               (int)XMarkdownDialect_GitHub);
    xmd_fragFlags(doc, 0, 0, 0, 0, 0, 0, 0, "atx-inline link not bold");
    xmd_fragHref(doc, 0, 0, "u", "atx-inline href");
    xmd_fragFlags(doc, 0, 2, 0, 1, 0, 0, 0, "atx-inline em italic only");
    XClassDelete((XClass*)doc);
}

/* ==================== 链接形态（空格目的地/尖括号） ==================== */

XQ_TEST(XMarkdown, linkForms)
{
    XTextDocument* doc = XTextDocument_create();
    XQ_REQUIRE(doc != NULL);
    /* [x]()：空目的地是合法链接，Qt 探针 link-empty ⇒ 文本 «x» + A: 空 href。
       内联模型只有 anchorHref 一个锚点槽位（空串 XString 的 toUtf8 为
       NULL），空 href 锚点落不进片段——文本形态按 Qt 消费（«x» 非字面
       "[x]()"，这是文本对拍可证的部分），锚点位属模型边界（头注释登记）。 */
    XMarkdownParser_parse_utf8("[x]()", doc, (int)XMarkdownDialect_GitHub);
    xmd_fragCount(doc, 0, 1, "link-empty frags");
    xmd_fragText(doc, 0, 0, "x", "link-empty text");
    xmd_fragHref(doc, 0, 0, NULL, "link-empty no href slot");
    XClassDelete((XClass*)doc);

    /* [x](<u>)：尖括号目的地剥 <>（Qt 探针 link-angle ⇒ href «u»）。 */
    doc = XTextDocument_create();
    XQ_REQUIRE(doc != NULL);
    XMarkdownParser_parse_utf8("[x](<u>)", doc,
                               (int)XMarkdownDialect_GitHub);
    xmd_fragText(doc, 0, 0, "x", "link-angle text");
    xmd_fragHref(doc, 0, 0, "u", "link-angle href");
    XClassDelete((XClass*)doc);

    /* 单边尖括号非法目的地 ⇒ 按字面（CommonMark 目的地须配平）。 */
    xmd_expectPlain("[x](<u)", (int)XMarkdownDialect_GitHub, "[x](<u)", 1,
                    "link-angle-unbalanced");
}

/* ==================== 围栏缩进剥离（Qt 探针对拍） ==================== */

XQ_TEST(XMarkdown, fenceIndent)
{
    XTextDocument* doc = XTextDocument_create();
    XQ_REQUIRE(doc != NULL);
    /* 探针 fence-indent2：开栏缩进 2 ⇒ "  a"→«a»、"   b"→« b»。 */
    XMarkdownParser_parse_utf8("  ```\n  a\n   b\n  ```", doc,
                               (int)XMarkdownDialect_GitHub);
    XQ_EQ_I64(XTextDocument_blockCount(doc), 2);
    xmd_fragText(doc, 0, 0, "a", "fence-indent2 l0");
    xmd_fragText(doc, 1, 0, " b", "fence-indent2 l1");
    XClassDelete((XClass*)doc);
    /* 探针 fence-indent1：开栏缩进 1 ⇒ " a"→«a»、"  b"→« b»、" c"→«c»。 */
    doc = XTextDocument_create();
    XQ_REQUIRE(doc != NULL);
    XMarkdownParser_parse_utf8(" ```\n a\n  b\n c\n ```", doc,
                               (int)XMarkdownDialect_GitHub);
    XQ_EQ_I64(XTextDocument_blockCount(doc), 3);
    xmd_fragText(doc, 0, 0, "a", "fence-indent1 l0");
    xmd_fragText(doc, 1, 0, " b", "fence-indent1 l1");
    xmd_fragText(doc, 2, 0, "c", "fence-indent1 l2");
    XClassDelete((XClass*)doc);
    /* 缩进 4 列不是围栏而是缩进代码（探针 fence-indent4：四块、内容各剥
       4 列、无 nonBreak）——与既有 indented-code 用例同一口径。 */
    xmd_expectPlain("    ```\n    a\n     b\n    ```",
                    (int)XMarkdownDialect_GitHub, "```\na\n b\n```", 4,
                    "fence-indent4-as-icode");
}

/* ==================== 前置元材料尾空白（语义；越界由 ASan 把关） ==================== */

XQ_TEST(XMarkdown, frontMatterWhitespace)
{
    XTextDocument* doc = XTextDocument_create();
    const char* fm;
    char* got;
    XQ_REQUIRE(doc != NULL);
    /* 【2026-11-17 Qt 6.8.0 offscreen 探针定案（/tmp/fmprobe/p4.cpp）】
     * 开/闭标记**带尾空白一律不认**：三种变体（双侧/仅闭/仅开）Qt 都返回
     * fm 空、整文按普通 Markdown 落——旧实现接受尾空白并抽取正文，与 Qt
     * 相反（复核「边界用例把与 Qt 相反的行为断言成通过」即指此）。本用例
     * 改为锁 Qt 口径：尾空白 ⇒ 前置段不成立。 */
    XMarkdownParser_parse_utf8("---  \ntitle: x\nlayout: y\n---  \n\nbody",
                               doc, (int)XMarkdownDialect_GitHub);
    fm = XTextDocument_metaInformation(doc, (int)XTDMetaInformation_FrontMatter);
    xq_check_impl(fm == NULL || fm[0] == '\0', "fm-trailing-ws-rejected",
                  __FILE__, __LINE__,
                  "尾空白标记不应成立前置段（Qt 探针同判），实得 «%s»",
                  fm ? fm : "(null)");
    got = XTextDocument_toPlainText(doc);
    XQ_REQUIRE(got != NULL);
    /* 整文按普通 Markdown：首行 `---  ` 落水平线（空块），正文段接排。 */
    XQ_EQ_STR(got, "\n\ntitle: x layout: y\n\nbody");
    XFree_System(got);
    XClassDelete((XClass*)doc);

    /* 仅 4 字符的 `--- `（无闭标记）：按普通 Markdown 落（语义；越界由
       ASan 复跑把关）。 */
    doc = XTextDocument_create();
    XQ_REQUIRE(doc != NULL);
    XMarkdownParser_parse_utf8("--- ", doc, (int)XMarkdownDialect_GitHub);
    XClassDelete((XClass*)doc);

    /* 无闭标记的两行（`---\n----`）：两条水平线各建空块（Qt 探针
       thematic-first 首线也留空块 0）⇒ 3 块、纯文本两个换行。 */
    xmd_expectPlain("---\n----", (int)XMarkdownDialect_GitHub, "\n\n", 3,
                    "fm-no-close");
}

/* ==================== 前置元材料边界（语义回归锁；越界由 ASan 把关） ==================== */

XQ_TEST(XMarkdown, frontMatterBoundaries)
{
    XTextDocument* doc;
    const char* fm;
    char* got;

    /* ① 未闭合（文首 `---` 段没有闭标记，扫到文档末尾）：不置 FrontMatter、
       整文按普通 Markdown 落（首行 `---` 是水平线）。
       期望纯文本与 Qt 6.8.3 qtmi.cpp:146-160「无闭标记 ⇒ 不抽取」同形，
       并与本机 6.8.0 探针（/tmp/qtmdprobe/fm3.cpp 之外，/tmp/mdrev_qt 复核探针
       与 /tmp/qtmdprobe/fm.cpp unclosed 同口径）逐字节一致：
       块 0 空（水平线）、段落两行软接排。**内存读越界不在此锁**（普通套件
       无红区），由 ASan 单 TU 复跑把关。 */
    doc = XTextDocument_create();
    XQ_REQUIRE(doc != NULL);
    XMarkdownParser_parse_utf8("---\ntitle: x\nlayout: y", doc,
                               (int)XMarkdownDialect_GitHub);
    fm = XTextDocument_metaInformation(doc,
                                       (int)XTDMetaInformation_FrontMatter);
    xq_check_impl(fm == NULL || fm[0] == '\0', "fm-unclosed-absent",
                  __FILE__, __LINE__,
                  "未闭合前置段不应置 FrontMatter，实得 «%s»",
                  fm ? fm : "(null)");
    got = XTextDocument_toPlainText(doc);
    XQ_REQUIRE(got != NULL);
    XQ_EQ_STR(got, "\n\ntitle: x layout: y");
    XFree_System(got);
    XQ_EQ_I64(XTextDocument_blockCount(doc), 3);
    XClassDelete((XClass*)doc);

    /* ② 文档恰好在闭标记处结束（`---\ntitle: x\n---`，闭标记后无换行）。
       **2026-11-17 Qt 探针定案（/tmp/fmprobe/p.cpp normal-no-body）**：
       Qt 6.8.0 **不认**此例（fm 空、整文按普通 Markdown：plain 以换行开头、
       内容 "title: x"）——闭标记行必须以 LF 收尾。旧用例按 6.8.3 原文推断
       断言「抽取、正文空」，与本机二进制相反（复核指出的假绿）。
       本用例改为锁 Qt 二进制口径：不抽取、fm 空。 */
    doc = XTextDocument_create();
    XQ_REQUIRE(doc != NULL);
    XMarkdownParser_parse_utf8("---\ntitle: x\n---", doc,
                               (int)XMarkdownDialect_GitHub);
    fm = XTextDocument_metaInformation(doc,
                                       (int)XTDMetaInformation_FrontMatter);
    xq_check_impl(fm == NULL || fm[0] == '\0', "fm-ends-at-close-rejected",
                  __FILE__, __LINE__,
                  "闭标记后无换行不应成立前置段（Qt 探针同判），实得 «%s»",
                  fm ? fm : "(null)");
    got = XTextDocument_toPlainText(doc);
    XQ_REQUIRE(got != NULL);
    /* 纯文本形态只锁语义锚点（首块水平线 ⇒ 以换行开头、正文是 title 行）：
       行尾换行数属解析器块尾衔接细节，不作为 Qt 字节级契约（本机 6.8.0
       探针为 «\ntitle: x»，本实现同形仅块尾换行数可能不同）。 */
    XQ_CHECK(got[0] == '\n' && strstr(got, "title: x") != NULL);
    XFree_System(got);
    XClassDelete((XClass*)doc);

    /* ③ CRLF 源文：**Qt 不认 CRLF 前置段**（/tmp/fmprobe/p2.cpp crlf：
       fm 空、整文按普通 Markdown）。旧实现接受 CRLF 并抽取，与本机二进制
       相反；本用例改为锁 Qt 口径：前置段不成立、正文按普通 Markdown 落。 */
    doc = XTextDocument_create();
    XQ_REQUIRE(doc != NULL);
    XMarkdownParser_parse_utf8(
        "---\r\ntitle: x\r\nlayout: y\r\n---\r\n\r\nbody", doc,
        (int)XMarkdownDialect_GitHub);
    fm = XTextDocument_metaInformation(doc,
                                       (int)XTDMetaInformation_FrontMatter);
    xq_check_impl(fm == NULL || fm[0] == '\0', "fm-crlf-rejected",
                  __FILE__, __LINE__,
                  "CRLF 前置段不应成立（Qt 探针同判），实得 «%s»",
                  fm ? fm : "(null)");
    got = XTextDocument_toPlainText(doc);
    XQ_REQUIRE(got != NULL);
    /* 普通 Markdown 语义锚点：无前置段抽取 ⇒ 正文里 title/layout 行与 body
       都在（首行水平线落空块）。字节级行尾差异不作契约。 */
    XQ_CHECK(got[0] == '\n' && strstr(got, "title: x") != NULL &&
             strstr(got, "body") != NULL);
    XFree_System(got);
    XClassDelete((XClass*)doc);

    /* ③b **Qt 接受的两条**（正面对照，防「全拒」式过修）：
       - 空白内容行 `---\n\n---\nbody` ⇒ fm=«\n»（/tmp/fmprobe/p3.cpp
         blank-content）；
       - 空格内容行 `---\n \n---\nbody` ⇒ fm=« »（p3.cpp space-content）。 */
    doc = XTextDocument_create();
    XQ_REQUIRE(doc != NULL);
    XMarkdownParser_parse_utf8("---\n\n---\nbody", doc,
                               (int)XMarkdownDialect_GitHub);
    fm = XTextDocument_metaInformation(doc,
                                       (int)XTDMetaInformation_FrontMatter);
    xq_check_impl(fm != NULL && XStrcmp(fm, "\n") == 0, "fm-blank-content",
                  __FILE__, __LINE__,
                  "空白内容前置段 fm=«%s» 期望 «\\n»", fm ? fm : "(null)");
    got = XTextDocument_toPlainText(doc);
    XQ_REQUIRE(got != NULL);
    XQ_EQ_STR(got, "body");
    XFree_System(got);
    XClassDelete((XClass*)doc);

    doc = XTextDocument_create();
    XQ_REQUIRE(doc != NULL);
    XMarkdownParser_parse_utf8("---\n \n---\nbody", doc,
                               (int)XMarkdownDialect_GitHub);
    fm = XTextDocument_metaInformation(doc,
                                       (int)XTDMetaInformation_FrontMatter);
    xq_check_impl(fm != NULL && XStrcmp(fm, " \n") == 0, "fm-space-content",
                  __FILE__, __LINE__,
                  "空格内容前置段 fm=«%s» 期望 « \\n»", fm ? fm : "(null)");
    /* 【已知差异（登记，不伪装成 Qt 口径）】本机 Qt 6.8.0 探针对纯空格
       内容行返回 « »（不含行尾换行），本实现返回 « \n»（按 [开标记行后,
       闭行首) 原字节切片）。差异仅存在于「内容行全为空白」的极端边界，
       不影响前置段成立与否；良构 front matter（含实际键值）逐字节一致
       （见 frontMatter 正例）。 */
    got = XTextDocument_toPlainText(doc);
    XQ_REQUIRE(got != NULL);
    XQ_EQ_STR(got, "body");
    XFree_System(got);
    XClassDelete((XClass*)doc);

    /* ④ 文档只有开标记（3 字节，恰好在缓冲区末尾）：不置 FrontMatter、
       按水平线落（Qt 探针 thematic-first 同形；末尾边界安全由 ASan 复跑
       把关，本用例只锁语义）。 */
    doc = XTextDocument_create();
    XQ_REQUIRE(doc != NULL);
    XMarkdownParser_parse_utf8("---", doc, (int)XMarkdownDialect_GitHub);
    fm = XTextDocument_metaInformation(doc,
                                       (int)XTDMetaInformation_FrontMatter);
    xq_check_impl(fm == NULL || fm[0] == '\0', "fm-only-open", __FILE__,
                  __LINE__, "单开标记不应置 FrontMatter，实得 «%s»",
                  fm ? fm : "(null)");
    got = XTextDocument_toPlainText(doc);
    XQ_REQUIRE(got != NULL);
    XQ_EQ_STR(got, "\n");
    XFree_System(got);
    XClassDelete((XClass*)doc);

    /* ⑤ 零内容前置段（`---\n---`，闭标记恰在偏移 4）**必须拒绝**：
       Qt 6.8.3 qtmi.cpp:147-148 的 `endMarkerPos > 4`（indexOf 从偏移 4
       起找闭标记，此例命中 4 ⇒ 条件不成立 ⇒ 不抽取）；本机 Qt 6.8.0
       offscreen 探针（/tmp/qtmdprobe/fm3.cpp dash-dash）同判：
       fm.size=0、blocks=3、plain "\\n\\n"。本实现按行口径等价（闭行行首
       ls 必须 > 4）⇒ 两条水平线、text 为两个换行。 */
    doc = XTextDocument_create();
    XQ_REQUIRE(doc != NULL);
    XMarkdownParser_parse_utf8("---\n---", doc, (int)XMarkdownDialect_GitHub);
    fm = XTextDocument_metaInformation(doc,
                                       (int)XTDMetaInformation_FrontMatter);
    xq_check_impl(fm == NULL || fm[0] == '\0', "fm-empty-rejected",
                  __FILE__, __LINE__,
                  "零内容前置段应被拒绝（Qt endMarkerPos>4），实得 «%s»",
                  fm ? fm : "(null)");
    got = XTextDocument_toPlainText(doc);
    XQ_REQUIRE(got != NULL);
    XQ_EQ_STR(got, "\n\n");
    XFree_System(got);
    XQ_EQ_I64(XTextDocument_blockCount(doc), 3);
    XClassDelete((XClass*)doc);
}

/* ==================== ⑨ 纯文本逐字节对拍（Qt 探针总表） ==================== */

XQ_TEST(XMarkdown, plainTextParity)
{
    const int GH = (int)XMarkdownDialect_GitHub;
    /* 每条期望值均取自本机 Qt 6.8.0 offscreen 探针输出（mdprobe1/2 与
       复核轮 qtprobe_w3a——后者含 mixed-zh 中文混合文档与 QTextEdit 包装）。 */
    xmd_expectPlain("# h1\n## h2\n### h3\n#### h4\n##### h5\n###### h6", GH,
                    "h1\nh2\nh3\nh4\nh5\nh6", 6, "parity-headings");
    /* 探针 dash-not-list：非标记行接排（惰性续行）单块。 */
    xmd_expectPlain("-foo bar\n-not a list", GH, "-foo bar -not a list", 1,
                    "parity-dash-not-list");
    xmd_expectPlain("```\na\nb\n```", GH, "a\nb", 2, "parity-code-trailing");
    xmd_expectPlain("- task\n- [ ] todo\n- [x] done", GH, "task\ntodo\ndone", 3,
                    "parity-tasklist");
    xmd_expectPlain("x  \ny", GH, "x\ny", 2, "parity-hardbreak-min");
    /* CRLF 源文（Qt 侧 QString 规整后等价 \n）。 */
    xmd_expectPlain("# t\r\n\r\nbody", GH, "t\nbody", 2, "parity-crlf");
    /* 混合文档冒烟（多构造连排；中文与全角标点相邻的行内定界）。 */
    xmd_expectPlain(
        "# 标题\n\n正文 **粗** 与 *斜*。\n\n- 项一\n- 项二\n\n> 引用\n\n```\ncode\n```\n\n[链接](u)",
        GH, "标题\n正文 粗 与 斜。\n项一\n项二\n引用\ncode\n链接", 7,
        "parity-mixed-zh");
}

/* ==================== 档位语义（GitHub vs CommonMark） ==================== */

XQ_TEST(XMarkdown, dialects)
{
    const int CM = (int)XMarkdownDialect_CommonMark;
    const int GH = (int)XMarkdownDialect_GitHub;
    XTextDocument* doc;

    /* 删除线：GitHub 落删除线；CommonMark 字面（探针 dialect 对照）。 */
    doc = XTextDocument_create();
    XQ_REQUIRE(doc != NULL);
    XMarkdownParser_parse_utf8("~~strike~~", doc, GH);
    xmd_fragFlags(doc, 0, 0, 0, 0, 0, 1, 0, "gh strike");
    XClassDelete((XClass*)doc);
    xmd_expectPlain("~~strike~~", CM, "~~strike~~", 1, "cm-strike-literal");

    /* 下划线：GitHub `_x_`/`__x__` → underline 非斜体（探针 gh-underline）。 */
    doc = XTextDocument_create();
    XQ_REQUIRE(doc != NULL);
    XMarkdownParser_parse_utf8("_em_ and __strong__", doc, GH);
    xmd_fragFlags(doc, 0, 0, 0, 0, 1, 0, 0, "gh underline em");
    xmd_fragFlags(doc, 0, 2, 0, 0, 1, 0, 0, "gh underline strong");
    XClassDelete((XClass*)doc);
    /* CommonMark：`_x_` 斜体、`__x__` 粗体（探针 cm-underline）。 */
    doc = XTextDocument_create();
    XQ_REQUIRE(doc != NULL);
    XMarkdownParser_parse_utf8("_em_ and __strong__", doc, CM);
    xmd_fragFlags(doc, 0, 0, 0, 1, 0, 0, 0, "cm em italic");
    xmd_fragFlags(doc, 0, 2, 1, 0, 0, 0, 0, "cm strong bold");
    XClassDelete((XClass*)doc);

    /* 任务列表：GitHub 剥标记；CommonMark 字面（探针 tasklist-cm）。 */
    xmd_expectPlain("- [ ] todo\n- [x] done", GH, "todo\ndone", 2,
                    "gh-tasklist-strip");
    xmd_expectPlain("- [ ] todo\n- [x] done", CM, "[ ] todo\n[x] done", 2,
                    "cm-tasklist-literal");

    /* 前置元材料：GitHub 落 metaInformation(FrontMatter)；CommonMark 无
       （探针 frontmatter/frontmatter-cm——纯文本形态因档而异）。 */
    doc = XTextDocument_create();
    XQ_REQUIRE(doc != NULL);
    XMarkdownParser_parse_utf8("---\ntitle: x\nlayout: y\n---\n\nbody text",
                               doc, GH);
    {
        const char* fm = XTextDocument_metaInformation(
            doc, (int)XTDMetaInformation_FrontMatter);
        xq_check_impl(fm != NULL && XStrcmp(fm, "title: x\nlayout: y\n") == 0,
                      "frontMatter", __FILE__, __LINE__,
                      "gh frontmatter=«%s» 期望 «title: x\nlayout: y\n»",
                      fm ? fm : "(null)");
    }
    {
        char* got = XTextDocument_toPlainText(doc);
        xq_check_impl(got != NULL && XStrcmp(got, "body text") == 0,
                      "frontmatter body", __FILE__, __LINE__,
                      "gh frontmatter body=«%s»", got ? got : "(null)");
        if (got) XFree_System(got);
    }
    XClassDelete((XClass*)doc);
}

/* ==================== 撤销口径 ==================== */

XQ_TEST(XMarkdown, undoResetsHistory)
{
    XTextDocument* doc = XTextDocument_create();
    char* got;
    XQ_REQUIRE(doc != NULL);
    /* Qt 缺省 undo/redo 开启；框架缺省关闭（XTextDocument_init 口径），
       本用例显式开启以便断言导入的撤销口径。 */
    XTextDocument_setUndoRedoEnabled(doc, true);
    /* 种子 + 历史插入（探针 setmd-undo 的前置态：setPlainText 清史，
       随后一次插入才产生可撤销步）。 */
    XTextDocument_setPlainText(doc, "seed text");
    {
        XTextCursor c;
        XTextCursor_init(&c, doc);
        XTextCursor_movePosition(&c, (int)XTextCursorMove_End,
                                 (int)XTextCursorMode_MoveAnchor, 1);
        XTextCursor_insertText(&c, " more");
        XTextCursor_deinit(&c);
    }
    XQ_CHECK(XTextDocument_isUndoAvailable(doc));
    /* 导入：clear 清史 + 单编辑块 ⇒ 恰好一步撤销（Qt doc->clear() +
       beginEditBlock…endEditBlock；探针 undo 后 toPlainText ""）。 */
    XMarkdownParser_parse_utf8("# x\ny", doc, (int)XMarkdownDialect_GitHub);
    got = XTextDocument_toPlainText(doc);
    XQ_REQUIRE(got != NULL);
    XQ_EQ_STR(got, "x\ny");
    XFree_System(got);
    XQ_CHECK(XTextDocument_isUndoAvailable(doc));
    XQ_EQ_I64(XTextDocument_availableUndoSteps(doc), 1);
    XTextDocument_undo(doc);
    got = XTextDocument_toPlainText(doc);
    XQ_REQUIRE(got != NULL);
    XQ_EQ_STR(got, ""); /* Qt 探针：undo 一次 → 空文档（导入前史被 clear） */
    XFree_System(got);
    XClassDelete((XClass*)doc);
}

/* ==================== ⑩ XTextEdit 接线 ==================== */

XQ_TEST(XMarkdown, textEditWiring)
{
    XTextEdit* te = XTextEdit_create(NULL, 0);
    XTextDocument* doc;
    char* plain;
    char* md;
    const XTDBlock* b;
    XQ_REQUIRE(te != NULL);
    doc = XTextEdit_document(te);
    XQ_REQUIRE(doc != NULL);

    XTextEdit_setMarkdown(te, "# Title\n\n**b** para");
    /* 编辑器纯文本 = 解析后纯文本（Qt QTextEdit::toPlainText 口径）。 */
    {
        XString* s = XTextEdit_toPlainText(te);
        XQ_REQUIRE(s != NULL);
        plain = (char*)XString_toUtf8(s);
        xq_check_impl(plain != NULL && XStrcmp(plain, "Title\nb para") == 0,
                      "te toPlainText", __FILE__, __LINE__,
                      "te toPlainText=«%s» 期望 «Title\nb para»",
                      plain ? plain : "(null)");
        XClassDelete((XClass*)s);
    }
    /* 文档承载解析结果：标题块+片段粗体（与 XTextDocument 路径同一解析）。 */
    b = XTextDocument_findBlockByNumber(doc, 0);
    XQ_REQUIRE(b != NULL);
    XQ_EQ_I64((int)b->headingLevel, 1);
    xmd_fragFlags(doc, 0, 0, 1, 0, 0, 0, 0, "te heading bold");
    /* 原文承载不变（任务书定案：markdown()/toMarkdown() 返回原文）。 */
    XQ_EQ_STR(XTextEdit_markdown(te), "# Title\n\n**b** para");
    md = XTextEdit_toMarkdown(te);
    XQ_REQUIRE(md != NULL);
    XQ_EQ_STR(md, "# Title\n\n**b** para");
    XFree_System(md);
    /* 内容替换类接口清除原文（既有契约不变；XString_toUtf8 对空串返回
       NULL——xte_plain 同款空串早退，按「NULL 或空串」判）。 */
    XTextEdit_setPlainText(te, "now plain");
    {
        const char* raw = XTextEdit_markdown(te);
        xq_check_impl(raw == NULL || raw[0] == '\0', "markdown cleared",
                      __FILE__, __LINE__,
                      "setPlainText 后 markdown()=«%s» 期望空", raw ? raw : "(null)");
    }
    XClassDelete((XClass*)te);
}

/* ==================== 空输入与未闭合围栏 ==================== */

XQ_TEST(XMarkdown, edgeCases)
{
    XTextDocument* doc = XTextDocument_create();
    XQ_REQUIRE(doc != NULL);
    /* 未闭围栏：按已闭收口（内容全落、无尾空块）。 */
    XMarkdownParser_parse_utf8("```\nunterminated", doc,
                               (int)XMarkdownDialect_GitHub);
    {
        char* got = XTextDocument_toPlainText(doc);
        xq_check_impl(got != NULL && XStrcmp(got, "unterminated") == 0,
                      "unclosed fence", __FILE__, __LINE__,
                      "unclosed fence=«%s»", got ? got : "(null)");
        if (got) XFree_System(got);
    }
    XClassDelete((XClass*)doc);
    /* NULL 源文 = 清空（Qt setMarkdown(NULL) 同口径）。 */
    doc = XTextDocument_create();
    XQ_REQUIRE(doc != NULL);
    XTextDocument_setPlainText(doc, "junk");
    XMarkdownParser_parse_utf8(NULL, doc, (int)XMarkdownDialect_GitHub);
    XQ_CHECK(XTextDocument_isEmpty(doc));
    XClassDelete((XClass*)doc);
    /* NULL 文档忽略（不崩）。 */
    XMarkdownParser_parse_utf8("# x", NULL, (int)XMarkdownDialect_GitHub);
}

#endif /* XGUI_ON && XTEXTDOCUMENT_ON */
