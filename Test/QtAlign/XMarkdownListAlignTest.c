/**
 * @file       XMarkdownListAlignTest.c
 * @brief      列表三属性（QTextListFormat Style/Start/NumberSuffix）× Qt 6.8
 *             对齐契约测试（§B5）。
 * @details    对齐依据（Qt 6.8.3 原文行号 + 本机 Qt 6.8.0 offscreen 探针
 *             逐值实测；探针源码 /tmp/qtprobe_list.cpp（无序标记映射、
 *             有序 start/suffix、嵌套）、/tmp/qtprobe_list2.cpp（嵌套形态与
 *             toHtml）、/tmp/qtprobe_list3.cpp（嵌套有序）、
 *             /tmp/qtprobe_list4.cpp（同列表续号规则）、
 *             /tmp/qtprobe_itemtext.cpp（QTextList::itemText 逐档输出））：
 *             - ①无序映射（qtextmarkdownimporter.cpp:222-231）：`'*'`→
 *               ListCircle、`'+'`→ListSquare、**default（含 `'-'`）**→
 *               ListDisc；探针 ul-dash/ul-star/ul-plus ⇒ -1/-2/-3。
 *             - ②有序（qtmi.cpp:242-250）：恒 ListDecimal +
 *               NumberSuffix=分隔符（`1.`⇒'.'、`1)`⇒')'）+ Start=起始号；
 *               探针 ol-dot/ol-paren/ol-start5/ol-start0（**0 是合法起始
 *               号**：start=0 ⇒ 首项「0.」）。
 *             - ③嵌套：探针 ul-nest4（`- a` / `    - b` / `    * c` /
 *               `    + d`）⇒ 缩进 1/2/2/2、样式 Disc/Disc/Circle/Square。
 *               本框架按内容列切层（既有口径），嵌套有序起始号用 HTML
 *               `<ol start>` 验（Qt 的 md4c 对 `1. a\n    5. b` 不认作嵌套
 *               ——探针 ol-nest-ol 把该行并进段落；本框架切层更宽，故
 *               markdown 侧只锁 Qt 同构的 `1.` 嵌套）。
 *             - ④toPlainText 逐字节对拍（Qt 列表标记不进纯文本）。
 *             - ⑤渲染消费：序号文本用 XTextDocument_listItemText 与
 *               QTextList::itemText 逐档对拍（Decimal/Alpha/Roman/后缀/
 *               start 偏移/越界回落）；无序三态用离屏 grab 的**像素形状**
 *               断言（Disc 实心圆=四角空+中心实、Circle 空心圆=四角空+中心
 *               空、Square 实心方块=四角实），几何口径见 Qt
 *               qtextdocumentlayout.cpp:2130-2235（size=lineSpacing/3、
 *               xoff=-spaceAdvance-size）。
 *             - ⑥泄漏门：运行器逐用例零容忍（--leak-tolerance=0）。
 *             - ⑦既有 XTextEdit/XTextDocument/XMarkdown 套件全绿：CLI
 *               过滤跑（见交付说明的命令）。
 * @note       只依赖公共 API（XTextDocument 的块级列表 setter/getter +
 *             XTextDocument_listItemText + XTextEdit 的 setMarkdown/
 *             setRichPreview + XWidget_grab 离屏渲染）。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "xq_align.h"
#include "XGuiConfig.h" /* 开关头先于门控包含：关断档（-DXGUI_ON=0 /
                           -DXTEXTDOCUMENT_ON=0）也必须能干净编译。 */

#if XGUI_ON && XTEXTDOCUMENT_ON

#include "XMarkdownParser.h"
#include "XTextDocument.h"
#include "XTextEdit.h"
#include "XStringUtils.h"
#include "XMemory.h"
#include "XWidget.h"
#include "XFont.h"
#include "XImage.h"

/* ==================== 断言工具 ==================== */

/** @brief 解析 Markdown（GitHub 档）到新文档。 */
static XTextDocument* xml_parse(const char* md)
{
    XTextDocument* doc = XTextDocument_create();
    if (!doc) return NULL;
    XMarkdownParser_parse_utf8(md, doc, (int)XMarkdownDialect_GitHub);
    return doc;
}

/** @brief 断言块 bi 的列表模型六元组（indent/ordered/style/start/suffix/
 *         fresh + isListItem）。 */
static void xml_listBlock(const XTextDocument* doc, int bi, int indent,
                          int ordered, int style, int start, char suffix,
                          int fresh, const char* what)
{
    const XTDBlock* b = XTextDocument_findBlockByNumber(doc, bi);
    if (!b) {
        xq_fail_at(__FILE__, __LINE__, "%s：块%d 不存在", what, bi);
        return;
    }
    xq_check_impl((int)b->isListItem == 1, "isListItem", __FILE__, __LINE__,
                  "%s：块%d isListItem=%d 期望 1", what, bi,
                  (int)b->isListItem);
    xq_check_impl((int)b->indentLevel == indent, "indentLevel",
                  __FILE__, __LINE__, "%s：块%d indent=%d 期望 %d", what, bi,
                  (int)b->indentLevel, indent);
    xq_check_impl((int)b->isOrdered == ordered, "isOrdered", __FILE__,
                  __LINE__, "%s：块%d isOrdered=%d 期望 %d", what, bi,
                  (int)b->isOrdered, ordered);
    xq_check_impl(XTextDocument_blockListStyle(doc, bi) == style, "listStyle",
                  __FILE__, __LINE__, "%s：块%d style=%d 期望 %d", what, bi,
                  XTextDocument_blockListStyle(doc, bi), style);
    xq_check_impl(XTextDocument_blockListStart(doc, bi) == start,
                  "listStart", __FILE__, __LINE__,
                  "%s：块%d start=%d 期望 %d", what, bi,
                  XTextDocument_blockListStart(doc, bi), start);
    xq_check_impl((int)XTextDocument_blockListSuffix(doc, bi) == (int)suffix,
                  "listSuffix", __FILE__, __LINE__,
                  "%s：块%d suffix=«%c» 期望 «%c»", what, bi,
                  XTextDocument_blockListSuffix(doc, bi), suffix);
    xq_check_impl((int)b->listFresh == fresh, "listFresh", __FILE__,
                  __LINE__, "%s：块%d listFresh=%d 期望 %d", what, bi,
                  (int)b->listFresh, fresh);
}

/** @brief 断言块 bi 在列表内第 idx 项（1 起）的标记文本。 */
static void xml_itemText(const XTextDocument* doc, int bi, int idx,
                         const char* want, const char* what)
{
    char buf[64];
    int n = XTextDocument_listItemText(doc, bi, idx, buf, (int)sizeof(buf));
    xq_check_impl(n == (int)XStrlen(want) && XStrcmp(buf, want) == 0,
                  "listItemText", __FILE__, __LINE__,
                  "%s：块%d 第%d项 itemText=«%s»(%d) 期望 «%s»", what, bi,
                  idx, buf, n, want);
}

/** @brief 断言纯文本与块数（Qt 探针期望直填）。 */
static void xml_expectPlain(const char* md, const char* plain, int blocks,
                            const char* what)
{
    XTextDocument* doc = xml_parse(md);
    char* got;
    if (!doc) {
        xq_fail_at(__FILE__, __LINE__, "%s：建文档失败", what);
        return;
    }
    got = XTextDocument_toPlainText(doc);
    xq_check_impl(got != NULL && XStrcmp(got, plain) == 0, "toPlainText",
                  __FILE__, __LINE__, "%s：toPlainText=«%s» 期望 «%s»", what,
                  got ? got : "(null)", plain);
    if (got) XFree_System(got);
    xq_check_impl(XTextDocument_blockCount(doc) == blocks, "blockCount",
                  __FILE__, __LINE__, "%s：blockCount=%d 期望 %d", what,
                  XTextDocument_blockCount(doc), blocks);
    XClassDelete((XClass*)doc);
}

/* ==================== ① 无序标记字符 → Style ==================== */

XQ_TEST(XMarkdownList, unorderedMarkerStyles)
{
    static const struct
    {
        const char* md;
        int style;
        const char* what;
    } cases[] = {
        { "- a\n- b", (int)XTDListStyle_Disc, "dash=Disc" },
        { "* a\n* b", (int)XTDListStyle_Circle, "star=Circle" },
        { "+ a\n+ b", (int)XTDListStyle_Square, "plus=Square" },
    };
    int i;
    for (i = 0; i < (int)(sizeof(cases) / sizeof(cases[0])); ++i) {
        XTextDocument* doc = xml_parse(cases[i].md);
        XQ_REQUIRE(doc != NULL);
        XQ_EQ_I64(XTextDocument_blockCount(doc), 2);
        /* 首项 listFresh=1（新列表首项）、次项 0（同列表续项）。 */
        xml_listBlock(doc, 0, 1, 0, cases[i].style, 1, '.', 1, cases[i].what);
        xml_listBlock(doc, 1, 1, 0, cases[i].style, 1, '.', 0, cases[i].what);
        XClassDelete((XClass*)doc);
    }
    /* 同层换标记字符 ⇒ 新列表（Qt 探针 ul-mixed-marks：`+ a\n- b` 两块
       各属独立 QTextList，样式 Square/Disc、序号各自从 1 起）。 */
    {
        XTextDocument* doc = xml_parse("+ a\n- b");
        XQ_REQUIRE(doc != NULL);
        XQ_EQ_I64(XTextDocument_blockCount(doc), 2);
        xml_listBlock(doc, 0, 1, 0, (int)XTDListStyle_Square, 1, '.', 1,
                      "mixed +then-");
        xml_listBlock(doc, 1, 1, 0, (int)XTDListStyle_Disc, 1, '.', 1,
                      "mixed +then-");
        XClassDelete((XClass*)doc);
    }
}

/* ==================== ② 有序：分隔符与起始号 ==================== */

XQ_TEST(XMarkdownList, orderedSuffixAndStart)
{
    XTextDocument* doc;

    /* `1.` ⇒ suffix '.'；start 缺省 1；itemText 1./2. */
    doc = xml_parse("1. a\n2. b");
    XQ_REQUIRE(doc != NULL);
    XQ_EQ_I64(XTextDocument_blockCount(doc), 2);
    xml_listBlock(doc, 0, 1, 1, (int)XTDListStyle_Decimal, 1, '.', 1, "ol-dot");
    xml_listBlock(doc, 1, 1, 1, (int)XTDListStyle_Decimal, 1, '.', 0, "ol-dot");
    xml_itemText(doc, 0, 1, "1.", "ol-dot");
    xml_itemText(doc, 1, 2, "2.", "ol-dot");
    XClassDelete((XClass*)doc);

    /* `1)` ⇒ suffix ')'（探针 ol-paren ⇒ itemText「1)」「2)」）。 */
    doc = xml_parse("1) a\n2) b");
    XQ_REQUIRE(doc != NULL);
    XQ_EQ_I64(XTextDocument_blockCount(doc), 2);
    xml_listBlock(doc, 0, 1, 1, (int)XTDListStyle_Decimal, 1, ')', 1,
                  "ol-paren");
    xml_listBlock(doc, 1, 1, 1, (int)XTDListStyle_Decimal, 1, ')', 0,
                  "ol-paren");
    xml_itemText(doc, 0, 1, "1)", "ol-paren");
    xml_itemText(doc, 1, 2, "2)", "ol-paren");
    XClassDelete((XClass*)doc);

    /* `5.` ⇒ start=5（两项同 start，itemText 5./6.——探针 ol-start5-run）。 */
    doc = xml_parse("5. a\n6. b");
    XQ_REQUIRE(doc != NULL);
    XQ_EQ_I64(XTextDocument_blockCount(doc), 2);
    xml_listBlock(doc, 0, 1, 1, (int)XTDListStyle_Decimal, 5, '.', 1,
                  "ol-start5");
    xml_listBlock(doc, 1, 1, 1, (int)XTDListStyle_Decimal, 5, '.', 0,
                  "ol-start5");
    xml_itemText(doc, 0, 1, "5.", "ol-start5");
    xml_itemText(doc, 1, 2, "6.", "ol-start5");
    XClassDelete((XClass*)doc);

    /* `0.` ⇒ start=0（**0 是合法起始号**，探针 ol-start0 ⇒「0.」「1.」）。 */
    doc = xml_parse("0. a\n1. b");
    XQ_REQUIRE(doc != NULL);
    xml_listBlock(doc, 0, 1, 1, (int)XTDListStyle_Decimal, 0, '.', 1,
                  "ol-start0");
    xml_itemText(doc, 0, 1, "0.", "ol-start0");
    XClassDelete((XClass*)doc);

    /* 同列表续项：编号不参与「新列表」判定（探针 start1-gap-5：
       `1. a\n\n5. b` ⇒ 同一列表 start=1、第二项「2.」）。 */
    doc = xml_parse("1. a\n\n5. b");
    XQ_REQUIRE(doc != NULL);
    XQ_EQ_I64(XTextDocument_blockCount(doc), 2);
    xml_listBlock(doc, 0, 1, 1, (int)XTDListStyle_Decimal, 1, '.', 1,
                  "ol-gap5");
    xml_listBlock(doc, 1, 1, 1, (int)XTDListStyle_Decimal, 1, '.', 0,
                  "ol-gap5");
    xml_itemText(doc, 1, 2, "2.", "ol-gap5");
    XClassDelete((XClass*)doc);

    /* 分隔符变化 ⇒ 新列表（探针 delim-change-gap：两项各属独立列表，
       itemText 各「1.」「1)」）。 */
    doc = xml_parse("1. a\n1) b");
    XQ_REQUIRE(doc != NULL);
    xml_listBlock(doc, 0, 1, 1, (int)XTDListStyle_Decimal, 1, '.', 1,
                  "ol-delim-change");
    xml_listBlock(doc, 1, 1, 1, (int)XTDListStyle_Decimal, 1, ')', 1,
                  "ol-delim-change");
    xml_itemText(doc, 0, 1, "1.", "ol-delim-change");
    xml_itemText(doc, 1, 1, "1)", "ol-delim-change");
    XClassDelete((XClass*)doc);
}

/* ==================== ③ 嵌套各层 style/start ==================== */

XQ_TEST(XMarkdownList, nestedLevels)
{
    XTextDocument* doc;

    /* 探针 ul-nest4：缩进 1/2/2/2；样式 Disc/Disc/Circle/Square；三个
       缩进 2 的块各属独立列表（listFresh 全 1）。 */
    doc = xml_parse("- a\n    - b\n    * c\n    + d");
    XQ_REQUIRE(doc != NULL);
    XQ_EQ_I64(XTextDocument_blockCount(doc), 4);
    xml_listBlock(doc, 0, 1, 0, (int)XTDListStyle_Disc, 1, '.', 1,
                  "ul-nest4 a");
    xml_listBlock(doc, 1, 2, 0, (int)XTDListStyle_Disc, 1, '.', 1,
                  "ul-nest4 b");
    xml_listBlock(doc, 2, 2, 0, (int)XTDListStyle_Circle, 1, '.', 1,
                  "ul-nest4 c");
    xml_listBlock(doc, 3, 2, 0, (int)XTDListStyle_Square, 1, '.', 1,
                  "ul-nest4 d");
    XClassDelete((XClass*)doc);

    /* 探针 ol-nest1：`1. a\n    1. b\n    1. c\n2. d` ⇒ 缩进 1/2/2/1；
       子层首项 listFresh、次项续号、回到父层续号。 */
    doc = xml_parse("1. a\n    1. b\n    1. c\n2. d");
    XQ_REQUIRE(doc != NULL);
    XQ_EQ_I64(XTextDocument_blockCount(doc), 4);
    xml_listBlock(doc, 0, 1, 1, (int)XTDListStyle_Decimal, 1, '.', 1,
                  "ol-nest a");
    xml_listBlock(doc, 1, 2, 1, (int)XTDListStyle_Decimal, 1, '.', 1,
                  "ol-nest b");
    xml_listBlock(doc, 2, 2, 1, (int)XTDListStyle_Decimal, 1, '.', 0,
                  "ol-nest c");
    xml_listBlock(doc, 3, 1, 1, (int)XTDListStyle_Decimal, 1, '.', 0,
                  "ol-nest d");
    xml_itemText(doc, 1, 1, "1.", "ol-nest b");
    xml_itemText(doc, 2, 2, "2.", "ol-nest c");
    xml_itemText(doc, 3, 2, "2.", "ol-nest d");
    XClassDelete((XClass*)doc);

    /* 列表后的非列表块不得残留列表格式（拆块继承的残值必须清回）。 */
    doc = xml_parse("- a\n\npara");
    XQ_REQUIRE(doc != NULL);
    XQ_EQ_I64(XTextDocument_blockCount(doc), 2);
    {
        const XTDBlock* b = XTextDocument_findBlockByNumber(doc, 1);
        XQ_REQUIRE(b != NULL);
        XQ_EQ_I64((int)b->isListItem, 0);
        XQ_EQ_I64(XTextDocument_blockListStyle(doc, 1), 0);
        XQ_EQ_I64((int)b->listStyleCode, 0);
        XQ_EQ_I64((int)b->listFresh, 0);
    }
    XClassDelete((XClass*)doc);
}

/* ==================== ④ toPlainText 与 Qt 探针对拍 ==================== */

XQ_TEST(XMarkdownList, plainTextParity)
{
    /* 全部取自探针输出（标记不进纯文本、块数一致）。 */
    xml_expectPlain("- a\n- b\n* c\n+ d", "a\nb\nc\nd", 4, "ul-mixed");
    xml_expectPlain("- a\n    - b\n    * c\n    + d", "a\nb\nc\nd", 4,
                    "ul-nest4");
    xml_expectPlain("1. a\n    1. b\n    1. c\n2. d", "a\nb\nc\nd", 4,
                    "ol-nest1");
    xml_expectPlain("- a\n\n  para\n- b", "a\npara\nb", 3, "loose-para");
    xml_expectPlain("- a\n- b\n\ntext\n\n* c\n* d", "a\nb\ntext\nc\nd", 5,
                    "two-lists");
    xml_expectPlain("5. a\n6. b\n7. c", "a\nb\nc", 3, "ol-start5-run");
    xml_expectPlain("0. a\n1. b", "a\nb", 2, "ol-start0");
}

/* ==================== ⑤a 序号文本（QTextList::itemText 对拍） ==================== */

XQ_TEST(XMarkdownList, itemTextQtParity)
{
    XTextDocument* doc = XTextDocument_create();
    XQ_REQUIRE(doc != NULL);
    XTextDocument_setPlainText(doc, "x");
    XQ_EQ_I64(XTextDocument_blockCount(doc), 1);

    /* 十进制 + start 偏移 + 两种后缀（探针 decimal*）。 */
    XTextDocument_setBlockListStyle(doc, 0, (int)XTDListStyle_Decimal);
    XTextDocument_setBlockListStart(doc, 0, 1);
    XTextDocument_setBlockListSuffix(doc, 0, '.');
    xml_itemText(doc, 0, 1, "1.", "decimal");
    xml_itemText(doc, 0, 3, "3.", "decimal");
    XTextDocument_setBlockListStart(doc, 0, 5);
    xml_itemText(doc, 0, 1, "5.", "decimal-start5");
    xml_itemText(doc, 0, 3, "7.", "decimal-start5");
    XTextDocument_setBlockListSuffix(doc, 0, ')');
    xml_itemText(doc, 0, 1, "5)", "decimal-start5-paren");
    XTextDocument_setBlockListSuffix(doc, 0, '.');
    XTextDocument_setBlockListStart(doc, 0, 0);
    XQ_EQ_I64(XTextDocument_blockListStart(doc, 0), 0);
    xml_itemText(doc, 0, 1, "0.", "decimal-start0");
    XTextDocument_setBlockListStart(doc, 0, -2);
    xml_itemText(doc, 0, 1, "-2.", "decimal-start-neg2");

    /* 小写字母 1..28（探针 loweralpha-1..28：a..z、aa、ab）。 */
    XTextDocument_setBlockListStyle(doc, 0, (int)XTDListStyle_LowerAlpha);
    XTextDocument_setBlockListStart(doc, 0, 1);
    XTextDocument_setBlockListSuffix(doc, 0, '.');
    xml_itemText(doc, 0, 1, "a.", "loweralpha-1");
    xml_itemText(doc, 0, 26, "z.", "loweralpha-26");
    xml_itemText(doc, 0, 27, "aa.", "loweralpha-27");
    xml_itemText(doc, 0, 28, "ab.", "loweralpha-28");
    /* itemNumber<1 回落十进制（探针 loweralpha-neg2 ⇒「-2.」「-1.」）。 */
    XTextDocument_setBlockListStart(doc, 0, -2);
    xml_itemText(doc, 0, 1, "-2.", "loweralpha-neg2");
    xml_itemText(doc, 0, 2, "-1.", "loweralpha-neg2");

    /* 大写字母 start=24 ⇒ X./Y./Z./AA./AB.（探针 upperalpha-start24）。 */
    XTextDocument_setBlockListStyle(doc, 0, (int)XTDListStyle_UpperAlpha);
    XTextDocument_setBlockListStart(doc, 0, 24);
    xml_itemText(doc, 0, 1, "X.", "upperalpha-24");
    xml_itemText(doc, 0, 3, "Z.", "upperalpha-26");
    xml_itemText(doc, 0, 4, "AA.", "upperalpha-27");
    xml_itemText(doc, 0, 5, "AB.", "upperalpha-28");

    /* 罗马数字（探针 lowerroman-1..10 / upperroman-4..6 / 4998..5000）。 */
    XTextDocument_setBlockListStyle(doc, 0, (int)XTDListStyle_LowerRoman);
    XTextDocument_setBlockListStart(doc, 0, 1);
    xml_itemText(doc, 0, 1, "i.", "lowerroman-1");
    xml_itemText(doc, 0, 4, "iv.", "lowerroman-4");
    xml_itemText(doc, 0, 9, "ix.", "lowerroman-9");
    xml_itemText(doc, 0, 10, "x.", "lowerroman-10");
    XTextDocument_setBlockListStyle(doc, 0, (int)XTDListStyle_UpperRoman);
    XTextDocument_setBlockListStart(doc, 0, 4);
    xml_itemText(doc, 0, 1, "IV.", "upperroman-4");
    xml_itemText(doc, 0, 2, "V.", "upperroman-5");
    xml_itemText(doc, 0, 3, "VI.", "upperroman-6");
    XTextDocument_setBlockListStyle(doc, 0, (int)XTDListStyle_LowerRoman);
    XTextDocument_setBlockListStart(doc, 0, 4998);
    xml_itemText(doc, 0, 1, "mmmmcmxcviii.", "lowerroman-4998");
    xml_itemText(doc, 0, 2, "mmmmcmxcix.", "lowerroman-4999");
    xml_itemText(doc, 0, 3, "?.", "lowerroman-5000"); /* Qt：≥5000 返回 "?"。 */

    /* 无序三态：无序号文本（Qt 渲染层画几何标记、不调 itemText）。 */
    XTextDocument_setBlockListStyle(doc, 0, (int)XTDListStyle_Disc);
    XQ_EQ_I64(XTextDocument_listItemText(doc, 0, 1, NULL, 0), 0);
    {
        char buf[8];
        XQ_EQ_I64(XTextDocument_listItemText(doc, 0, 1, buf,
                                             (int)sizeof(buf)), 0);
        XQ_EQ_I64((int)buf[0], 0);
    }

    /* 截断契约：返回值 = **实际写入**字节数（不含 NUL），不是 snprintf 的
       「应有长度」——复核探针实测 start=123、cap=2 时旧实现返回 3 而缓冲
       只有 "1"。 */
    XTextDocument_setBlockListStyle(doc, 0, (int)XTDListStyle_Decimal);
    XTextDocument_setBlockListStart(doc, 0, 123);
    {
        char small[2];
        int n;
        small[0] = 'X';
        small[1] = 'Y';
        n = XTextDocument_listItemText(doc, 0, 1, small, 2);
        XQ_EQ_I64(n, 1);
        XQ_EQ_I64((int)small[0], '1');
        XQ_EQ_I64((int)small[1], 0); /* 恒 NUL 结尾。 */
    }
    XClassDelete((XClass*)doc);
}

/* ==================== ⑤b 模型 API 语义 ==================== */

XQ_TEST(XMarkdownList, modelApiSemantics)
{
    XTextDocument* doc = XTextDocument_create();
    XTextDocument* clone;
    XQ_REQUIRE(doc != NULL);
    XTextDocument_setPlainText(doc, "x");

    /* 非法样式按「未设」清零；-1..-8 往返同值。 */
    XTextDocument_setBlockListStyle(doc, 0, 42);
    XQ_EQ_I64(XTextDocument_blockListStyle(doc, 0), 0);
    XTextDocument_setBlockListStyle(doc, 0, (int)XTDListStyle_UpperRoman);
    XQ_EQ_I64(XTextDocument_blockListStyle(doc, 0), -8);
    /* 显式 Undefined（无标记）经块字段可见、getter 仍读 0。 */
    XTextDocument_setBlockListStyle(doc, 0, (int)XTDListStyle_Undefined);
    XQ_EQ_I64(XTextDocument_blockListStyle(doc, 0), 0);
    {
        const XTDBlock* b = XTextDocument_findBlockByNumber(doc, 0);
        XQ_REQUIRE(b != NULL);
        XQ_EQ_I64((int)b->listStyleCode, (int)XTD_LIST_STYLE_CODE_UNDEFINED);
    }

    /* 后缀只认 '.'/')'（其余按 '.'）。 */
    XTextDocument_setBlockListSuffix(doc, 0, ')');
    XQ_EQ_I64((int)XTextDocument_blockListSuffix(doc, 0), ')');
    XTextDocument_setBlockListSuffix(doc, 0, 'x');
    XQ_EQ_I64((int)XTextDocument_blockListSuffix(doc, 0), '.');

    /* start 未设读 1（Qt QTextListFormat 构造即 setStart(1)）。 */
    XTextDocument_setPlainText(doc, "y");
    XQ_EQ_I64(XTextDocument_blockListStart(doc, 0), 1);
    XTextDocument_setBlockListStart(doc, 0, -32768);
    XQ_EQ_I64(XTextDocument_blockListStart(doc, 0), -32768);

    /* 越界块号一律安全读（不崩、按缺省返回）。 */
    XQ_EQ_I64(XTextDocument_blockListStyle(doc, 99), 0);
    XQ_EQ_I64(XTextDocument_blockListStart(doc, -1), 1);
    XQ_EQ_I64((int)XTextDocument_blockListSuffix(doc, 99), '.');

    /* clone 逐字段继承。 */
    XTextDocument_setBlockListItem(doc, 0, true, true);
    XTextDocument_setBlockListFormat(doc, 0, (int)XTDListStyle_UpperAlpha, 27,
                                     ')', true);
    clone = XTextDocument_clone(doc);
    XQ_REQUIRE(clone != NULL);
    xml_listBlock(clone, 0, 0, 1, (int)XTDListStyle_UpperAlpha, 27, ')', 1,
                  "clone");
    xml_itemText(clone, 0, 1, "AA)", "clone");
    XClassDelete((XClass*)clone);
    XClassDelete((XClass*)doc);
}

/* ==================== ⑤c 无序标记像素形状 ==================== */

/** @brief 标记带扫描结果（等级 1 列表标记：内容盒左缘之前的窄带）。 */
typedef struct XmlInk
{
    int count;      /**< 墨点像素数。 */
    int minX, maxX, minY, maxY;
    int centerInk;  /**< 外接框中心像素是否上墨。 */
    int cornerInk;  /**< 外接框四角是否**全部**上墨。 */
} XmlInk;

/**
 * @brief 抓取控件并统计列表标记带的墨点。
 * @param bandRight 逻辑坐标右边界（正文起笔列 = XTE_RICH_LEFT(2) +
 *        等级 × XTE_LIST_INDENT(24)；带内只有标记，左让 1px 视口边框）。
 * @details 图像按 dpr 缩放后逐像素统计物理坐标。
 */
static void xml_scanMarkerBand(XTextEdit* te, int bandRight, XmlInk* ink)
{
    XImage* img;
    int iw, ih, w, s, x, y;
    XMemset(ink, 0, sizeof(*ink));
    XQ_REQUIRE(te != NULL);
    img = XWidget_grab((XWidget*)te);
    if (!img) {
        xq_fail_at(__FILE__, __LINE__, "XWidget_grab 失败");
        return;
    }
    w = XWidget_width((XWidget*)te);
    iw = XImage_width(img);
    ih = XImage_height(img);
    s = (w > 0 && iw >= w) ? (iw + w - 1) / w : 1; /* dpr 取整倍数。 */
    ink->minX = iw;
    ink->minY = ih;
    ink->maxX = -1;
    ink->maxY = -1;
    for (y = s; y < ih; y += 1) {
        for (x = s; x < bandRight * s && x < iw; x += 1) {
            if (XImage_pixel(img, x, y) == 0xFFFFFFFFu) continue;
            ink->count++;
            if (x < ink->minX) ink->minX = x;
            if (x > ink->maxX) ink->maxX = x;
            if (y < ink->minY) ink->minY = y;
            if (y > ink->maxY) ink->maxY = y;
        }
    }
    if (ink->count > 0) {
        int cx = (ink->minX + ink->maxX) / 2;
        int cy = (ink->minY + ink->maxY) / 2;
        ink->centerInk = XImage_pixel(img, cx, cy) != 0xFFFFFFFFu;
        ink->cornerInk = XImage_pixel(img, ink->minX, ink->minY) != 0xFFFFFFFFu &&
                         XImage_pixel(img, ink->maxX, ink->minY) != 0xFFFFFFFFu &&
                         XImage_pixel(img, ink->minX, ink->maxY) != 0xFFFFFFFFu &&
                         XImage_pixel(img, ink->maxX, ink->maxY) != 0xFFFFFFFFu;
    }
    XClassDelete((XClass*)img);
}

/** @brief 建一个 320×140 的预览态 XTextEdit（像素级 24，标记尺寸足够分辨
 *         形状）；md 为 Markdown 源文。 */
static XTextEdit* xml_makeEditor(const char* md)
{
    XTextEdit* te = XTextEdit_create(NULL, 0);
    XFont f;
    if (!te) return NULL;
    f = XWidget_font((XWidget*)te);
    XFont_setPixelSize(&f, 24);
    XWidget_setFont((XWidget*)te, &f);
    XClassDeinit(&f);
    XWidget_resize((XWidget*)te, 320, 140);
    XTextEdit_setMarkdown(te, md);
    XTextEdit_setRichPreview(te, true);
    return te;
}

XQ_TEST(XMarkdownList, bulletShapePixels)
{
    XTextEdit* te;
    XmlInk ink;

    /* 无序标记尺寸 = 行距/3（24px 字号下 ≥6px），形状可分辨：
       Disc=实心圆（四角空、中心实）；Circle=空心圆（四角空、中心空）；
       Square=实心方块（四角实）。 */
    te = xml_makeEditor("- a");
    XQ_REQUIRE(te != NULL);
    xml_scanMarkerBand(te, 26, &ink); /* 等级 1：正文起笔列 = 2+24。 */
    XQ_CHECK_MSG(ink.count > 0, "Disc 标记未上墨（count=%d）", ink.count);
    XQ_CHECK_MSG(!ink.cornerInk, "Disc 应为圆形：外接框四角不得上墨");
    XQ_CHECK_MSG(ink.centerInk, "Disc 应为实心：外接框中心须上墨");
    XClassDelete((XClass*)te);

    te = xml_makeEditor("* a");
    XQ_REQUIRE(te != NULL);
    xml_scanMarkerBand(te, 26, &ink); /* 等级 1：正文起笔列 = 2+24。 */
    XQ_CHECK_MSG(ink.count > 0, "Circle 标记未上墨（count=%d）", ink.count);
    XQ_CHECK_MSG(!ink.cornerInk, "Circle 应为圆形：外接框四角不得上墨");
    XQ_CHECK_MSG(!ink.centerInk, "Circle 应为空心：外接框中心不得上墨");
    XClassDelete((XClass*)te);

    te = xml_makeEditor("+ a");
    XQ_REQUIRE(te != NULL);
    xml_scanMarkerBand(te, 26, &ink); /* 等级 1：正文起笔列 = 2+24。 */
    XQ_CHECK_MSG(ink.count > 0, "Square 标记未上墨（count=%d）", ink.count);
    XQ_CHECK_MSG(ink.cornerInk, "Square 应为实心方块：外接框四角须上墨");
    XQ_CHECK_MSG(ink.centerInk, "Square 应为实心：外接框中心须上墨");
    XClassDelete((XClass*)te);
}

/* ==================== ⑤d 有序序号渲染消费（start 生效） ==================== */

XQ_TEST(XMarkdownList, orderedMarkerRendersStart)
{
    XmlInk one;
    XmlInk five;
    XmlInk ten;
    XTextEdit* te;
    XTextDocument* doc;

    /* 像素证据：不同 start ⇒ 标记带墨点分布不同（渲染层真的取了 start，
       不是恒定画「1.」）；两位数（10.）的标记更宽 ⇒ 更靠左。
       标记右缘贴内容盒左缘、左缘受视口左留白钳位 ⇒ 等级 1（缩进 24px）
       时两位数会顶到钳位、宽度差被吃掉；故本用例把块缩进抬到等级 2
       （xLeft=2+48）再量宽度差。 */
    te = xml_makeEditor("1. a");
    XQ_REQUIRE(te != NULL);
    doc = XTextEdit_document(te);
    XQ_REQUIRE(doc != NULL);
    XTextDocument_setBlockIndentLevel(doc, 0, 2);

    xml_scanMarkerBand(te, 50, &one); /* 等级 2：正文起笔列 = 2+2×24。 */
    XTextDocument_setBlockListStart(doc, 0, 5);
    xml_scanMarkerBand(te, 50, &five);
    XTextDocument_setBlockListStart(doc, 0, 10);
    xml_scanMarkerBand(te, 50, &ten);
    XClassDelete((XClass*)te);

    XQ_CHECK_MSG(one.count > 0 && five.count > 0 && ten.count > 0,
                 "有序标记未上墨（%d/%d/%d）", one.count, five.count,
                 ten.count);
    /* 「1.」与「5.」同宽不同字形 ⇒ 墨点集合不同（像素差异 > 0）。 */
    XQ_CHECK_MSG(!(one.minX == five.minX && one.minY == five.minY &&
                   one.maxX == five.maxX && one.maxY == five.maxY &&
                   one.count == five.count),
                 "start=5 的标记与 start=1 逐像素相同 ⇒ 渲染层没消费 start");
    /* 「10.」比「1.」多一位 ⇒ 右对齐到内容盒左缘 ⇒ 左缘更靠左。 */
    XQ_CHECK_MSG(ten.minX < one.minX,
                 "两位数序号应更宽：ten.minX=%d 期望 < one.minX=%d",
                 ten.minX, one.minX);
}

/* ==================== ⑤e 显式 Undefined（<ul type="none">）无标记 ==================== */

XQ_TEST(XMarkdownList, undefinedStyleHasNoMarker)
{
    XTextDocument* doc = XTextDocument_create();
    XmlInk ink;
    XTextEdit* te;
    XQ_REQUIRE(doc != NULL);

    XTextDocument_setHtml(doc,
                          "<ul type=\"none\"><li>a</li></ul>");
    xml_listBlock(doc, 0, 1, 0, 0 /* Undefined */, 1, '.', 1, "ul-none");
    XClassDelete((XClass*)doc);

    /* 渲染层对显式 Undefined 不画任何标记（Qt drawList 的
       case ListStyleUndefined: return;）。 */
    te = xml_makeEditor("- a");
    XQ_REQUIRE(te != NULL);
    {
        XTextDocument* d = XTextEdit_document(te);
        XQ_REQUIRE(d != NULL);
        XTextDocument_setBlockListStyle(d, 0, (int)XTDListStyle_Undefined);
    }
    xml_scanMarkerBand(te, 26, &ink); /* 等级 1：正文起笔列 = 2+24。 */
    XQ_CHECK_MSG(ink.count == 0, "显式 Undefined 不应画标记（墨点 %d）",
                 ink.count);
    XClassDelete((XClass*)te);
}

/* ==================== HTML <ul type>/<ol start> 同管线 ==================== */

XQ_TEST(XMarkdownList, htmlListAttributes)
{
    XTextDocument* doc = XTextDocument_create();
    XQ_REQUIRE(doc != NULL);

    /* <ul> 缺省 Disc；<ol> 缺省 Decimal；首项 fresh。 */
    XTextDocument_setHtml(doc, "<ul><li>a</li><li>b</li></ul>");
    XQ_EQ_I64(XTextDocument_blockCount(doc), 2);
    xml_listBlock(doc, 0, 1, 0, (int)XTDListStyle_Disc, 1, '.', 1, "html-ul");
    xml_listBlock(doc, 1, 1, 0, (int)XTDListStyle_Disc, 1, '.', 0, "html-ul");
    XTextDocument_setHtml(doc, "<ol><li>a</li><li>b</li></ol>");
    XQ_EQ_I64(XTextDocument_blockCount(doc), 2);
    xml_listBlock(doc, 0, 1, 1, (int)XTDListStyle_Decimal, 1, '.', 1,
                  "html-ol");
    xml_itemText(doc, 1, 2, "2.", "html-ol");

    /* <ul type> 逐档（Qt qtexthtmlparser.cpp:1720-1744）。 */
    XTextDocument_setHtml(doc, "<ul type=\"circle\"><li>a</li></ul>");
    xml_listBlock(doc, 0, 1, 0, (int)XTDListStyle_Circle, 1, '.', 1,
                  "html-type-circle");
    XTextDocument_setHtml(doc, "<ul type=\"square\"><li>a</li></ul>");
    xml_listBlock(doc, 0, 1, 0, (int)XTDListStyle_Square, 1, '.', 1,
                  "html-type-square");
    XTextDocument_setHtml(doc, "<ul type=\"disc\"><li>a</li></ul>");
    xml_listBlock(doc, 0, 1, 0, (int)XTDListStyle_Disc, 1, '.', 1,
                  "html-type-disc");
    /* 未识别的 type 保持标签缺省（Qt：无分支 ⇒ 不改写 listStyle）。 */
    XTextDocument_setHtml(doc, "<ul type=\"bogus\"><li>a</li></ul>");
    xml_listBlock(doc, 0, 1, 0, (int)XTDListStyle_Disc, 1, '.', 1,
                  "html-type-bogus");

    /* 嵌套 <ul> 未写 type：**每层都是 Disc**（不是逐级推进）。Qt 6.8 在
       initNode 给 ul/ol 注入缺省 list-style 声明（qtexthtmlparser.cpp:
       2169-2180 → setListStyle:1118-1138 置 hasOwnListStyle=true）⇒
       qtextdocumentfragment.cpp:686 的 nextListStyle 分支是死代码。本机
       Qt 6.8.0 offscreen 探针 /tmp/b5rev_probe.cpp 实测：三层嵌套
       <ul> ⇒ style=-1/-1/-1。 */
    XTextDocument_setHtml(doc,
                          "<ul><li>a<ul><li>b<ul><li>c</li></ul></li>"
                          "</ul></li></ul>");
    XQ_EQ_I64(XTextDocument_blockCount(doc), 3);
    xml_listBlock(doc, 0, 1, 0, (int)XTDListStyle_Disc, 1, '.', 1,
                  "html-nest-ul");
    xml_listBlock(doc, 1, 2, 0, (int)XTDListStyle_Disc, 1, '.', 1,
                  "html-nest-ul");
    xml_listBlock(doc, 2, 3, 0, (int)XTDListStyle_Disc, 1, '.', 1,
                  "html-nest-ul");
    /* 父层显式 type=square、子层无 type ⇒ 子层仍 Disc（同探针
       h-square-then-ul：-3/-1）。 */
    XTextDocument_setHtml(doc,
                          "<ul type=\"square\"><li>a<ul><li>b</li></ul>"
                          "</li></ul>");
    XQ_EQ_I64(XTextDocument_blockCount(doc), 2);
    xml_listBlock(doc, 0, 1, 0, (int)XTDListStyle_Square, 1, '.', 1,
                  "html-square-then-ul");
    xml_listBlock(doc, 1, 2, 0, (int)XTDListStyle_Disc, 1, '.', 1,
                  "html-square-then-ul");

    /* <ol start>/<ol type>：start 落全列表；type=a ⇒ LowerAlpha。 */
    XTextDocument_setHtml(doc, "<ol start=\"5\"><li>a</li><li>b</li></ol>");
    XQ_EQ_I64(XTextDocument_blockCount(doc), 2);
    xml_listBlock(doc, 0, 1, 1, (int)XTDListStyle_Decimal, 5, '.', 1,
                  "html-ol-start");
    xml_listBlock(doc, 1, 1, 1, (int)XTDListStyle_Decimal, 5, '.', 0,
                  "html-ol-start");
    xml_itemText(doc, 0, 1, "5.", "html-ol-start");
    xml_itemText(doc, 1, 2, "6.", "html-ol-start");
    XTextDocument_setHtml(doc, "<ol type=\"a\" start=\"4\"><li>a</li></ol>");
    xml_listBlock(doc, 0, 1, 1, (int)XTDListStyle_LowerAlpha, 4, '.', 1,
                  "html-ol-type-a");
    xml_itemText(doc, 0, 1, "d.", "html-ol-type-a");
    XTextDocument_setHtml(doc, "<ol type=\"I\"><li>a</li></ol>");
    xml_listBlock(doc, 0, 1, 1, (int)XTDListStyle_UpperRoman, 1, '.', 1,
                  "html-ol-type-I");
    xml_itemText(doc, 0, 1, "I.", "html-ol-type-I");

    /* 嵌套：<ol start=5> 内嵌 <ul type=square>，回到父层仍是 start=5。 */
    XTextDocument_setHtml(doc,
                          "<ol start=\"5\"><li>a<ul type=\"square\">"
                          "<li>b</li></ul></li><li>c</li></ol>");
    XQ_EQ_I64(XTextDocument_blockCount(doc), 3);
    xml_listBlock(doc, 0, 1, 1, (int)XTDListStyle_Decimal, 5, '.', 1,
                  "html-nest-ol-ul");
    xml_listBlock(doc, 1, 2, 0, (int)XTDListStyle_Square, 1, '.', 1,
                  "html-nest-ol-ul");
    xml_listBlock(doc, 2, 1, 1, (int)XTDListStyle_Decimal, 5, '.', 0,
                  "html-nest-ol-ul");
    xml_itemText(doc, 2, 2, "6.", "html-nest-ol-ul");

    XClassDelete((XClass*)doc);
}

/* ==================== XTextEdit 接线（列表格式随 setMarkdown 落文档） ==================== */

XQ_TEST(XMarkdownList, textEditWiring)
{
    XTextEdit* te = XTextEdit_create(NULL, 0);
    XTextDocument* doc;
    XQ_REQUIRE(te != NULL);
    doc = XTextEdit_document(te);
    XQ_REQUIRE(doc != NULL);
    XTextEdit_setMarkdown(te, "5. a\n6. b\n\n* c");
    XQ_EQ_I64(XTextDocument_blockCount(doc), 3);
    xml_listBlock(doc, 0, 1, 1, (int)XTDListStyle_Decimal, 5, '.', 1,
                  "te ol");
    xml_listBlock(doc, 1, 1, 1, (int)XTDListStyle_Decimal, 5, '.', 0,
                  "te ol");
    xml_listBlock(doc, 2, 1, 0, (int)XTDListStyle_Circle, 1, '.', 1,
                  "te ul-star");
    xml_itemText(doc, 0, 1, "5.", "te ol");
    XClassDelete((XClass*)te);
}

/* ==================== 列表边界与 listFresh 判定 ==================== */

XQ_TEST(XMarkdownList, listBoundaries)
{
    XTextDocument* doc;

    /* 空行不结束列表（loose）：Qt 探针 loose 是一块 2 项的 QTextList
       ⇒ 第二项不是新列表首项（listFresh=0）、序号续。 */
    doc = xml_parse("- a\n\n- b");
    XQ_REQUIRE(doc != NULL);
    XQ_EQ_I64(XTextDocument_blockCount(doc), 2);
    xml_listBlock(doc, 0, 1, 0, (int)XTDListStyle_Disc, 1, '.', 1, "loose a");
    xml_listBlock(doc, 1, 1, 0, (int)XTDListStyle_Disc, 1, '.', 0, "loose b");
    XClassDelete((XClass*)doc);

    /* 普通段落隔断 ⇒ 前后是两个列表（Qt：独立 QTextList）⇒ 第二个列表
       首项 listFresh=1。 */
    doc = xml_parse("- a\n\npara\n\n- c");
    XQ_REQUIRE(doc != NULL);
    XQ_EQ_I64(XTextDocument_blockCount(doc), 3);
    xml_listBlock(doc, 0, 1, 0, (int)XTDListStyle_Disc, 1, '.', 1,
                  "para-split a");
    xml_listBlock(doc, 2, 1, 0, (int)XTDListStyle_Disc, 1, '.', 1,
                  "para-split c");
    XClassDelete((XClass*)doc);

    /* 同标记跨空档的有序列表续号：第二项 itemText「2.」（探针
       start5-gap-same：一块 2 项、start=5 ⇒ 5./6.）。 */
    doc = xml_parse("5. a\n\n5. b");
    XQ_REQUIRE(doc != NULL);
    XQ_EQ_I64(XTextDocument_blockCount(doc), 2);
    xml_listBlock(doc, 1, 1, 1, (int)XTDListStyle_Decimal, 5, '.', 0,
                  "start5-gap");
    xml_itemText(doc, 1, 2, "6.", "start5-gap");
    XClassDelete((XClass*)doc);

    /* ---- 列表项内部续段：**不结束列表**（复核实测，探针
       /tmp/b5rev_probe2.cpp G/F）----
       `1. a\n\n   para\n1. b`：段落缩进到内容列（3）⇒ 属列表项内部续段，
       第二项仍是同一列表的第 2 项（Qt itemText「2.」，非列表的 para 块
       夹在中间）。 */
    doc = xml_parse("1. a\n\n   para\n1. b");
    XQ_REQUIRE(doc != NULL);
    XQ_EQ_I64(XTextDocument_blockCount(doc), 3);
    xml_listBlock(doc, 0, 1, 1, (int)XTDListStyle_Decimal, 1, '.', 1,
                  "ol-para-inside a");
    xml_listBlock(doc, 2, 1, 1, (int)XTDListStyle_Decimal, 1, '.', 0,
                  "ol-para-inside b");
    xml_itemText(doc, 2, 2, "2.", "ol-para-inside b"); /* 该层第 2 项。 */
    XClassDelete((XClass*)doc);

    /* 同形但第二项写 9：编号不参与列表归属 ⇒ 仍是「2.」（探针 H）。 */
    doc = xml_parse("1. a\n\n   para\n9. b");
    XQ_REQUIRE(doc != NULL);
    xml_listBlock(doc, 2, 1, 1, (int)XTDListStyle_Decimal, 1, '.', 0,
                  "ol-para-inside-9");
    xml_itemText(doc, 2, 2, "2.", "ol-para-inside-9");
    XClassDelete((XClass*)doc);

    /* 无序同形（探针 F：`- a\n\n  para\n- b` ⇒ 同一列表第 2 项）。 */
    doc = xml_parse("- a\n\n  para\n- b");
    XQ_REQUIRE(doc != NULL);
    XQ_EQ_I64(XTextDocument_blockCount(doc), 3);
    xml_listBlock(doc, 2, 1, 0, (int)XTDListStyle_Disc, 1, '.', 0,
                  "ul-para-inside");
    XClassDelete((XClass*)doc);

    /* 段落回到**外层列**（0）⇒ 列表结束，后续同标记是新列表（探针 D/E）：
       第二块是新列表首项（fresh=1、序号回到 1）。 */
    doc = xml_parse("1. a\n\npara\n1. b");
    XQ_REQUIRE(doc != NULL);
    XQ_EQ_I64(XTextDocument_blockCount(doc), 3);
    xml_listBlock(doc, 2, 1, 1, (int)XTDListStyle_Decimal, 1, '.', 1,
                  "ol-para-outside");
    xml_itemText(doc, 2, 1, "1.", "ol-para-outside");
    XClassDelete((XClass*)doc);

    /* 嵌套层被外层项关闭后再开 ⇒ 新嵌套列表（探针 I：
       `1. a\n\n    5. b\n2. c\n\n    7. d` ⇒ b='5.'、c='2.'、d='7.'，
       d 与 b 的层条目描述符相同但属不同列表）。 */
    doc = xml_parse("1. a\n\n    5. b\n2. c\n\n    7. d");
    XQ_REQUIRE(doc != NULL);
    XQ_EQ_I64(XTextDocument_blockCount(doc), 4);
    xml_listBlock(doc, 0, 1, 1, (int)XTDListStyle_Decimal, 1, '.', 1,
                  "nest-reopen a");
    xml_listBlock(doc, 1, 2, 1, (int)XTDListStyle_Decimal, 5, '.', 1,
                  "nest-reopen b");
    xml_listBlock(doc, 2, 1, 1, (int)XTDListStyle_Decimal, 1, '.', 0,
                  "nest-reopen c");
    xml_listBlock(doc, 3, 2, 1, (int)XTDListStyle_Decimal, 7, '.', 1,
                  "nest-reopen d");
    xml_itemText(doc, 1, 1, "5.", "nest-reopen b");
    xml_itemText(doc, 2, 2, "2.", "nest-reopen c");
    xml_itemText(doc, 3, 1, "7.", "nest-reopen d");
    XClassDelete((XClass*)doc);
}

/* ==================== ⑤f 续段列表的渲染续号（像素） ==================== */

/** @brief 两图在列区间 [x0,x1) 内的差异像素数（同一 y 范围取交集）。 */
static int xml_diffColumns(const XImage* a, const XImage* b, int x0, int x1)
{
    int w, h, x, y, diff = 0;
    if (!a || !b) return 0;
    w = XImage_width(a);
    h = XImage_height(a);
    if (XImage_width(b) < w) w = XImage_width(b);
    if (XImage_height(b) < h) h = XImage_height(b);
    if (x1 > w) x1 = w;
    for (y = 0; y < h; ++y) {
        for (x = x0 < 0 ? 0 : x0; x < x1; ++x) {
            if (XImage_pixel(a, x, y) != XImage_pixel(b, x, y)) ++diff;
        }
    }
    return diff;
}

XQ_TEST(XMarkdownList, looseListRendering)
{
    XTextEdit* teInside;
    XTextEdit* teOutside;
    XImage* imgInside;
    XImage* imgOutside;
    int diffMarker;
    int diffBody;
    int width;

    /* 同一纯文本、同一块结构，唯一差别是「中间段落属列表项内部续段（缩进
       到内容列）还是外层段落」：
       - 内部续段 ⇒ 列表未结束 ⇒ 第二项序号「2.」（Qt 探针 G）；
       - 外层段落 ⇒ 前后两个列表 ⇒ 第二项序号「1.」（Qt 探针 D）。
       两份文档除标记外像素必须完全一致 ⇒ 标记带差异 > 0 且正文区差异为 0
       就是「渲染层按 listFresh 续号/重起」的像素级证据（比只对模型断言强：
       若渲染层在非列表块清计数，两份会画成同一个「1.」，差异为 0）。 */
    teInside = xml_makeEditor("1. a\n\n   para\n1. b");
    teOutside = xml_makeEditor("1. a\n\npara\n1. b");
    XQ_REQUIRE(teInside != NULL && teOutside != NULL);
    imgInside = XWidget_grab((XWidget*)teInside);
    imgOutside = XWidget_grab((XWidget*)teOutside);
    XQ_REQUIRE(imgInside != NULL && imgOutside != NULL);
    width = XImage_width(imgInside);
    diffMarker = xml_diffColumns(imgInside, imgOutside, 1, 26);
    diffBody = xml_diffColumns(imgInside, imgOutside, 26, width);
    XQ_CHECK_MSG(diffMarker > 0,
                 "内部续段应续号（2.）、外层段落应重起（1.）：标记带无差异"
                 "（diffMarker=%d）", diffMarker);
    XQ_CHECK_MSG(diffBody == 0,
                 "两份文档正文应逐像素一致（差异只在标记带）：diffBody=%d",
                 diffBody);
    XClassDelete((XClass*)imgInside);
    XClassDelete((XClass*)imgOutside);
    XClassDelete((XClass*)teInside);
    XClassDelete((XClass*)teOutside);
}

/* ==================== 导入的撤销粒度（块内只压一次快照） ==================== */

XQ_TEST(XMarkdownList, undoSingleStep)
{
    XTextDocument* doc = XTextDocument_create();
    char* got;
    XQ_REQUIRE(doc != NULL);
    XTextDocument_setUndoRedoEnabled(doc, true);
    /* Qt import()：doc->clear() + beginEditBlock…endEditBlock ⇒ 整次导入
       恰好一步撤销（回导入前空文档）。列表块的 setBlockListItem /
       setBlockListFormat 在编辑块内不得各压一步快照——否则一次撤销只
       回到中途态。 */
    XMarkdownParser_parse_utf8("1. a\n2. b\n\n- c\n- d", doc,
                               (int)XMarkdownDialect_GitHub);
    XQ_EQ_I64(XTextDocument_blockCount(doc), 4);
    XQ_EQ_I64(XTextDocument_availableUndoSteps(doc), 1);
    XTextDocument_undo(doc);
    got = XTextDocument_toPlainText(doc);
    XQ_REQUIRE(got != NULL);
    XQ_EQ_STR(got, "");
    XFree_System(got);
    XClassDelete((XClass*)doc);
}

#endif /* XGUI_ON && XTEXTDOCUMENT_ON */
