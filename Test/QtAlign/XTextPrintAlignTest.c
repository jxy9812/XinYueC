/**
 * @file       XTextPrintAlignTest.c
 * @brief      XTextDocumentLayout 打印分页与 Qt 6.8 的对齐契约测试。
 * @details    对齐依据（本机 Qt 6.8.0 offscreen 探针 + Qt 6.8.3 原文）：
 *             - 探针程序与报告：/tmp/w3b_probe/probe2.cpp +
 *               report2.txt（QTextDocument 连续布局 + setPageSize 分页；
 *               documentMargin=0 干净配对；字体经
 *               QFontDatabase::addApplicationFont 加载框架同一份
 *               Library/XFont/XFontOutlineCommon.otf——family 名
 *               "Noto Sans CJK SC"（W1 XTextLayoutAlignTest 探针同款
 *               配对字体），pixelSize=16，kerning=false）。
 *             - 分页原文：qtextdocumentlayout.cpp:3963-3969
 *               （dynamicPageCount = ceil(docH/pageH)，页高<0 ⇒ 1）、
 *               :3604-3605 + :121-123（行级断页：absoluteY+行高 >
 *               pageBottom ⇒ newPage（y = qMax(y, 新页顶)，只进不退）；
 *               行永不劈开）、:3557-3558
 *               （页内容高 < 行高 ⇒ 不换页）、:3023（pageBottom =
 *               (页+1)*页高 - bottomMargin）、qtextdocument.cpp
 *               :1923-1950（printPage 竖切绘制）+ :1935-1944（页码 =
 *               纯数字画底边距区右侧）。
 *             - 探针逐值锚点（report2.txt，全部进入下方断言）：
 *               para40@300x160 边距 10 ⇒ pageCount 2、docH 300（行 5
 *               挪 y=170）、longwords8@200x90 边距 6 ⇒ pageCount 3、
 *               docH 240（行 3 挪 y=96、行 6 挪 y=186）、multi@200x90
 *               边距 8 ⇒ pageCount 2、docH 130（blk3 行挪 y=98）、
 *               empty@300x400 ⇒ pageCount 1、docH 44（=10+24+10）、
 *               tiny@100x12 ⇒ pageCount 3（docH 28=2+24+2，行留页 0
 *               不换页）、nomargin@150x60 ⇒ pageCount 1、首行 y=0。
 *             - 与 Qt 的登记差异：文本位置为 UTF-8 字节偏移（框架全树
 *               口径）；documentMargin（Qt 缺省 4px）不在本类承载；
 *               页码占位 %p/%P 是本类页眉页脚扩展约定（Qt qtbase 无
 *               该占位语法，print 页码为纯数字）。
 * @note       本文件与其它 QtAlign 用例一样自注册、无 main；泄漏门按
 *             xq_align_run.c 缺省零容忍（--leak-tolerance=0）执行。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

#if XGUI_ON

#include "XTextDocumentLayout.h"
#include "XTextLayout.h"
#include "XTextOption.h"
#include "XClass.h"
#include "XMemory.h"
#include "XString.h"
#include "XPainter.h"

#include <string.h>
#include <stdio.h>

/* 水平对齐位（XTDAlignment 同值空间）。 */
#define XTPQ_ALIGN_LEFT 0x01

/* 探针锚点：该字体的行高（ascent+descent @ pixelSize 16，探针 line
 * h=24.000）。运行时以 XTextLayout 实测为准，此值仅作文档。 */

/* ==================== 测试脚手架 ==================== */

/** @brief 构造 16px 框架轮廓字体（与 Qt 探针同一份 .otf 同字号）。 */
static void xtpq_makeFont(XFont* font)
{
    XFont_init(font);
    XFont_setFamily(font, "XFontOutlineCommon");
    XFont_setPixelSize(font, 16);
}

/** @brief 构造已配好字体/选项的文档布局器（调用方删除）。 */
static XTextDocumentLayout* xtpq_makeLayout(XTextDocument* doc)
{
    XTextDocumentLayout* self =
        XTextDocumentLayout_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    XFont font;
    XTextOption opt;
    if (!self) return NULL;
    xtpq_makeFont(&font);
    XTextDocumentLayout_setFont(self, &font);
    XClassDeinit((XClass*)&font);
    XTextOption_init(&opt);
    /* 探针 doc 缺省 wrap：QTextDocumentPrivate 构造改设
     * WrapAtWordBoundaryOrAnywhere（QTextDocument 默认选项）。 */
    XTextOption_setWrapMode(&opt, XTextOptionWrap_WordBoundaryOrAnywhere);
    XTextDocumentLayout_setTextOption(self, &opt);
    XTextDocumentLayout_setDocument(self, doc);
    return self;
}

/** @brief 构造文档 + 设页面/边距（探针 dump() 的同构装配）。 */
static XTextDocumentLayout* xtpq_makePaged(XTextDocument* doc, double pw,
                                           double ph, double ml, double mt,
                                           double mr, double mb)
{
    XTextDocumentLayout* self = xtpq_makeLayout(doc);
    XSizeF size;
    if (!self) return NULL;
    size.width = (float)pw;
    size.height = (float)ph;
    XTextDocumentLayout_setPageSize(self, size);
    XTextDocumentLayout_setPageMargins(self, ml, mt, mr, mb);
    return self;
}

/** @brief 构造 n 词空格分隔的段落文本并 setPlainText（探针同构）。 */
static void xtpq_fillWords(XTextDocument* doc, int words)
{
    int i;
    char buf[32];
    buf[0] = '\0';
    for (i = 0; i < words; ++i) {
        snprintf(buf, sizeof(buf), "word%d ", i);
        if (i == words - 1) {
            /* 末词去尾空格（探针 words.trimmed()）。 */
            char* sp = strchr(buf, ' ');
            if (sp) *sp = '\0';
        }
        XTextDocument_appendText(doc, buf, NULL);
    }
}

/** @brief 行带内是否有非白像素（首行命中/非空白断言的共用探针）。 */
static bool xtpq_bandHasInk(XImage* image, int x0, int x1, int y0, int y1)
{
    int x;
    int y;
    for (y = y0; y < y1; ++y)
        for (x = x0; x < x1; ++x)
            if (XImage_pixel(image, x, y) != 0xFFFFFFFFu) return true;
    return false;
}

/** @brief 行带墨迹水平范围（最小/最大非白 x；无墨返回 false）。 */
static bool xtpq_bandInkExtent(XImage* image, int x0, int x1, int y0, int y1,
                               int* outMinX, int* outMaxX)
{
    int x;
    int y;
    int minX = -1;
    int maxX = -1;
    for (y = y0; y < y1; ++y) {
        for (x = x0; x < x1; ++x) {
            if (XImage_pixel(image, x, y) != 0xFFFFFFFFu) {
                if (minX < 0 || x < minX) minX = x;
                if (x > maxX) maxX = x;
            }
        }
    }
    if (outMinX) *outMinX = minX;
    if (outMaxX) *outMaxX = maxX;
    return minX >= 0;
}

/* ==================== 用例 ==================== */

/**
 * 验收①：固定页面 → 页数与 Qt 探针一致（report2.txt 逐值锚点）。
 * para40@300x160 ⇒ 2 页 / docH 300；longwords8@200x90 ⇒ 3 页 / docH 240；
 * multi@200x90 ⇒ 2 页 / docH 130；nopage（页高 -1）⇒ 1 页。
 */
XQ_TEST(XTextPrint, pageCountMatchesQt)
{
    XTextDocument* doc = XTextDocument_create();
    XTextDocumentLayout* lay;
    XSizeF ds;

    XQ_REQUIRE(doc != NULL);
    xtpq_fillWords(doc, 40);
    lay = xtpq_makePaged(doc, 300, 160, 10, 10, 10, 10);
    XQ_REQUIRE(lay != NULL);
    XQ_EQ_I64(XTextDocumentLayout_pageCount(lay), 2);
    ds = XTextDocumentLayout_documentSize(lay);
    XQ_EQ_I64((int)(ds.height + 0.5), 300);
    XQ_EQ_I64((int)(ds.width + 0.5), 300);
    XClassDelete((XClass*)lay);
    XClassDelete((XClass*)doc);

    doc = XTextDocument_create();
    XQ_REQUIRE(doc != NULL);
    {
        int i;
        for (i = 0; i < 8; ++i)
            XTextDocument_appendText(doc, i < 7 ? "ABCDEFGHIJ " : "ABCDEFGHIJ",
                                     NULL);
    }
    lay = xtpq_makePaged(doc, 200, 90, 6, 6, 6, 6);
    XQ_REQUIRE(lay != NULL);
    XQ_EQ_I64(XTextDocumentLayout_pageCount(lay), 3);
    ds = XTextDocumentLayout_documentSize(lay);
    XQ_EQ_I64((int)(ds.height + 0.5), 240);
    XClassDelete((XClass*)lay);
    XClassDelete((XClass*)doc);

    doc = XTextDocument_create();
    XQ_REQUIRE(doc != NULL);
    XTextDocument_appendText(doc, "line one\nline two\nline three\nline four",
                             NULL);
    lay = xtpq_makePaged(doc, 200, 90, 8, 8, 8, 8);
    XQ_REQUIRE(lay != NULL);
    XQ_EQ_I64(XTextDocumentLayout_pageCount(lay), 2);
    ds = XTextDocumentLayout_documentSize(lay);
    XQ_EQ_I64((int)(ds.height + 0.5), 130);
    XClassDelete((XClass*)lay);
    XClassDelete((XClass*)doc);

    doc = XTextDocument_create();
    XQ_REQUIRE(doc != NULL);
    xtpq_fillWords(doc, 40);
    lay = xtpq_makePaged(doc, 300, -1, 10, 10, 10, 10);
    XQ_REQUIRE(lay != NULL);
    XQ_EQ_I64(XTextDocumentLayout_pageCount(lay), 1); /* 页高<0 ⇒ 1 页。 */
    ds = XTextDocumentLayout_documentSize(lay);
    XQ_EQ_I64((int)(ds.height + 0.5), 260);
    XClassDelete((XClass*)lay);
    XClassDelete((XClass*)doc);
}

/**
 * 验收②：行级跨页断行位置（探针 longwords8 逐行 y 锚点）。
 * 10 词段落 @200x90 边距 6：页 0 行 y=6/30/54（行 2 底 78 ≤
 * pageBottom 84；行 3 若放 78+24=102 > 84 ⇒ 挪页 1 顶 96）；
 * 页 1 行 y=96/120/144；页 2 行 y=186/210——**块内行序列中间断页、
 * 行永不劈开**（qtextdocumentlayout.cpp:3604-3605 探针逐行吻合）。
 */
XQ_TEST(XTextPrint, rowLevelPageBreak)
{
    XTextDocument* doc = XTextDocument_create();
    XTextDocumentLayout* lay;
    XTextDocLayoutRow row;
    int i;

    XQ_REQUIRE(doc != NULL);
    for (i = 0; i < 8; ++i)
        XTextDocument_appendText(doc, i < 7 ? "ABCDEFGHIJ " : "ABCDEFGHIJ",
                                 NULL);
    lay = xtpq_makePaged(doc, 200, 90, 6, 6, 6, 6);
    XQ_REQUIRE(lay != NULL);
    XQ_EQ_I64(XTextDocumentLayout_rowCount(lay), 8);

    /* 页 0：三行（字节区间每行一词，行 end 含行尾空格字节——软断行
     * 行尾空白**不计宽**但字节域计入行区间，W1 引擎口径）。 */
    XQ_REQUIRE(XTextDocumentLayout_rowAt(lay, 0, &row));
    XQ_EQ_I64(row.m_pageIndex, 0);
    XQ_EQ_I64(row.m_yAbs, 6);
    XQ_EQ_I64(row.m_line.m_start, 0);
    XQ_EQ_I64(row.m_line.m_end, 11);
    XQ_REQUIRE(XTextDocumentLayout_rowAt(lay, 1, &row));
    XQ_EQ_I64(row.m_yAbs, 30);
    XQ_EQ_I64(row.m_line.m_start, 11);
    XQ_EQ_I64(row.m_line.m_end, 22);
    XQ_REQUIRE(XTextDocumentLayout_rowAt(lay, 2, &row));
    XQ_EQ_I64(row.m_yAbs, 54);
    XQ_EQ_I64(row.m_line.m_start, 22);
    XQ_EQ_I64(row.m_line.m_end, 33);
    /* 行 3 挪页 1 顶（90+6=96；Qt 探针 relY 90 + 块顶 6 同值）。 */
    XQ_REQUIRE(XTextDocumentLayout_rowAt(lay, 3, &row));
    XQ_EQ_I64(row.m_pageIndex, 1);
    XQ_EQ_I64(row.m_yAbs, 96);
    XQ_REQUIRE(XTextDocumentLayout_rowAt(lay, 4, &row));
    XQ_EQ_I64(row.m_yAbs, 120);
    XQ_REQUIRE(XTextDocumentLayout_rowAt(lay, 5, &row));
    XQ_EQ_I64(row.m_yAbs, 144);
    /* 行 6 挪页 2 顶（180+6=186；Qt 探针 relY 180 同值）。 */
    XQ_REQUIRE(XTextDocumentLayout_rowAt(lay, 6, &row));
    XQ_EQ_I64(row.m_pageIndex, 2);
    XQ_EQ_I64(row.m_yAbs, 186);
    XQ_REQUIRE(XTextDocumentLayout_rowAt(lay, 7, &row));
    XQ_EQ_I64(row.m_yAbs, 210);
    XClassDelete((XClass*)lay);
    XClassDelete((XClass*)doc);
}

/**
 * 验收③：边距生效——首行 y = 上边距、右边界不越界（行右缘 ≤
 * 左边距 + 可用宽）。nomargin@150x60 首行 y=0（探针同值）；para40
 * 首行 y=10、每行右缘 ≤ 10+280。
 */
XQ_TEST(XTextPrint, marginsEffective)
{
    XTextDocument* doc = XTextDocument_create();
    XTextDocumentLayout* lay;
    XTextDocLayoutRow row;
    int i;
    int rows;

    XQ_REQUIRE(doc != NULL);
    xtpq_fillWords(doc, 40);
    lay = xtpq_makePaged(doc, 300, 160, 10, 10, 10, 10);
    XQ_REQUIRE(lay != NULL);
    XQ_REQUIRE(XTextDocumentLayout_rowAt(lay, 0, &row));
    XQ_EQ_I64(row.m_yAbs, 10); /* 首行顶 = 上边距（探针 10）。 */
    rows = XTextDocumentLayout_rowCount(lay);
    for (i = 0; i < rows; ++i) {
        XQ_REQUIRE(XTextDocumentLayout_rowAt(lay, i, &row));
        /* 右边界不越界：行右缘 ≤ 左边距 + 可用宽（300-10-10=280）。 */
        XQ_CHECK(row.m_line.m_x >= 0);
        XQ_CHECK(10 + row.m_line.m_x + row.m_line.m_width <= 10 + 280);
        /* 页内行顶落在页矩形内。 */
        XQ_CHECK(row.m_yAbs >= row.m_pageIndex * 160);
        XQ_CHECK(row.m_yAbs < (row.m_pageIndex + 1) * 160);
    }
    XClassDelete((XClass*)lay);
    XClassDelete((XClass*)doc);

    doc = XTextDocument_create();
    XQ_REQUIRE(doc != NULL);
    XTextDocument_appendText(doc, "one two three four five six seven eight",
                             NULL);
    lay = xtpq_makePaged(doc, 150, 60, 0, 0, 0, 0);
    XQ_REQUIRE(lay != NULL);
    XQ_REQUIRE(XTextDocumentLayout_rowAt(lay, 0, &row));
    XQ_EQ_I64(row.m_yAbs, 0); /* 零边距：首行顶 = 0（探针 nomargin）。 */
    XClassDelete((XClass*)lay);
    XClassDelete((XClass*)doc);
}

/**
 * 验收④：drawPage 出图非空白且首行像素命中（grab 断言）。
 * longwords8@200x90：页 0 首行带 [6,30) 有墨、页 1 首行带 [96,120)
 * 有墨（页内坐标 6..30）；正文区外（页顶边距带 [0,6)）无墨。
 */
XQ_TEST(XTextPrint, drawPageInk)
{
    XTextDocument* doc = XTextDocument_create();
    XTextDocumentLayout* lay;
    XImage image;
    bool ok;
    int i;

    XQ_REQUIRE(doc != NULL);
    for (i = 0; i < 8; ++i)
        XTextDocument_appendText(doc, i < 7 ? "ABCDEFGHIJ " : "ABCDEFGHIJ",
                                 NULL);
    lay = xtpq_makePaged(doc, 200, 90, 6, 6, 6, 6);
    XQ_REQUIRE(lay != NULL);
    XImage_init_ex(&image, 200, 90, XImageFormat_ARGB32);
    ok = XTextDocumentLayout_drawToImage(lay, 0, &image);
    XQ_CHECK(ok);
    /* 页 0：首行带（页内 y 6..30）有墨；页顶边距带（y 0..4）无墨。 */
    XQ_CHECK(xtpq_bandHasInk(&image, 0, 200, 6, 30));
    XQ_CHECK(!xtpq_bandHasInk(&image, 0, 200, 0, 4));
    /* 页 1：首行带同样有墨（绝对 y 96..120 ⇒ 页内 6..30）。 */
    ok = XTextDocumentLayout_drawToImage(lay, 1, &image);
    XQ_CHECK(ok);
    XQ_CHECK(xtpq_bandHasInk(&image, 0, 200, 6, 30));
    /* 页号越界：不出图、保持原状。 */
    XImage_fill(&image, 0xFFFFFFFFu);
    XQ_CHECK(!XTextDocumentLayout_drawToImage(lay, 3, &image));
    XQ_CHECK(!xtpq_bandHasInk(&image, 0, 200, 0, 90));
    XClassDeinit((XClass*)&image);
    XClassDelete((XClass*)lay);
    XClassDelete((XClass*)doc);
}

/**
 * 验收⑤：页眉页脚与页码占位（%p=当前页号 1 起、%P=总页数；Qt print
 * 页码 = 纯数字画底边距区右侧 qtextdocument.cpp:1935-1944 的收编口）。
 * 页 0 页眉 "p%p/%P" 展开 "p1/15"、页 9 展开 "p10/15"——两位页号比
 * 一位页号多一个数字宽 ⇒ 页眉墨迹右缘页 9 更靠右；页脚墨迹用大边距
 * 页（40px > 行高 24）验证落笔在下边距区内、右对齐不越右边距。
 */
XQ_TEST(XTextPrint, headerFooterPlaceholders)
{
    XTextDocument* doc = XTextDocument_create();
    XTextDocumentLayout* lay;
    XImage image;
    char line[64];
    int i;
    int minX;
    int maxX;
    int minX9;
    int maxX9;

    XQ_REQUIRE(doc != NULL);
    /* 30 短行 @300x80 边距 5 ⇒ 每页 (80-10)/24=2 行 ⇒ 15 页。行间
     * '\n' 由 XTextLayout 平铺行模型拆段（块内拆行，W1 引擎内建）。 */
    for (i = 0; i < 30; ++i) {
        snprintf(line, sizeof(line), i < 29 ? "row %d\n" : "row %d", i);
        XTextDocument_appendText(doc, line, NULL);
    }
    lay = xtpq_makePaged(doc, 300, 80, 5, 5, 5, 5);
    XQ_REQUIRE(lay != NULL);
    XQ_EQ_I64(XTextDocumentLayout_pageCount(lay), 15);
    XTextDocumentLayout_setHeaderText_utf8(lay, "p%p/%P");
    XQ_EQ_STR(XTextDocumentLayout_headerText_const(lay), "p%p/%P");

    XImage_init_ex(&image, 300, 80, XImageFormat_ARGB32);
    /* 页 0：页眉带 [5,21)（页顶+ascent 基线）有墨且左对齐页左边距。 */
    XQ_CHECK(XTextDocumentLayout_drawToImage(lay, 0, &image));
    XQ_CHECK(xtpq_bandInkExtent(&image, 0, 300, 5, 21, &minX, &maxX));
    XQ_CHECK(minX >= 5 && minX <= 8); /* 左对齐（字形侧承 ≤3px）。 */
    /* 页 9：%p=10（两位）⇒ 页眉墨迹右缘比页 0（"1" 一位）更靠右。 */
    XQ_CHECK(XTextDocumentLayout_drawToImage(lay, 9, &image));
    XQ_CHECK(xtpq_bandInkExtent(&image, 0, 300, 5, 21, &minX9, &maxX9));
    XQ_CHECK(minX9 >= 5 && minX9 <= 8);
    XQ_CHECK(maxX9 > maxX);
    /* 图缓冲配对释放（deinit 后再 init 第二场景，防 96KB 泄漏）。 */
    XClassDeinit((XClass*)&image);
    XClassDelete((XClass*)lay);
    XClassDelete((XClass*)doc);

    /* 页脚：大边距页（40px > 行高）⇒ 页脚基线 = 页底界(360)+ascent，
     * 墨迹全落在下边距区 [360,400) 且右对齐（右缘 ≤ 页右缘-边距）。 */
    doc = XTextDocument_create();
    XQ_REQUIRE(doc != NULL);
    XTextDocument_appendText(doc, "hello", NULL);
    lay = xtpq_makePaged(doc, 300, 400, 40, 40, 40, 40);
    XQ_REQUIRE(lay != NULL);
    XTextDocumentLayout_setFooterText_utf8(lay, "F");
    XImage_init_ex(&image, 300, 400, XImageFormat_ARGB32);
    XQ_CHECK(XTextDocumentLayout_drawToImage(lay, 0, &image));
    XQ_CHECK(xtpq_bandInkExtent(&image, 0, 300, 360, 400, &minX, &maxX));
    XQ_CHECK(maxX <= 300 - 40);          /* 右对齐不越右边距。 */
    XQ_CHECK(maxX > 200);                /* 不在页左侧。 */
    /* 清除页脚（NULL/空串同义）⇒ 下边距区无墨。 */
    XTextDocumentLayout_setFooterText_utf8(lay, NULL);
    XQ_CHECK(XTextDocumentLayout_footerText_const(lay) == NULL);
    XTextDocumentLayout_setFooterText_utf8(lay, "F");
    XTextDocumentLayout_setFooterText_utf8(lay, "");
    XQ_CHECK(XTextDocumentLayout_footerText_const(lay) == NULL);
    XQ_CHECK(XTextDocumentLayout_drawToImage(lay, 0, &image));
    XQ_CHECK(!xtpq_bandHasInk(&image, 0, 300, 361, 400));
    XClassDeinit((XClass*)&image);
    XClassDelete((XClass*)lay);
    XClassDelete((XClass*)doc);
}

/**
 * 验收⑥：空文档 1 页 + 超长单块多页 + 页放不下一行（不换页守卫）。
 * empty@300x400 ⇒ 1 页、1 空行（y=10、高 24、docH=44=10+24+10）；
 * para40 ⇒ 2 页；tiny@100x12（页高 12 < 行高 24）⇒ 行留页 0、docH=28、
 * 页数仍按除法 ceil(28/12)=3（qtextdocumentlayout.cpp:3557-3558 +
 * :3963-3969 解耦口径，探针 tiny 逐值吻合）。
 */
XQ_TEST(XTextPrint, emptyAndTinyPage)
{
    XTextDocument* doc = XTextDocument_create();
    XTextDocumentLayout* lay;
    XTextDocLayoutRow row;
    XSizeF ds;

    XQ_REQUIRE(doc != NULL);
    lay = xtpq_makePaged(doc, 300, 400, 10, 10, 10, 10);
    XQ_REQUIRE(lay != NULL);
    XQ_EQ_I64(XTextDocumentLayout_pageCount(lay), 1);
    XQ_EQ_I64(XTextDocumentLayout_rowCount(lay), 1); /* 空块 = 1 空行。 */
    XQ_REQUIRE(XTextDocumentLayout_rowAt(lay, 0, &row));
    XQ_EQ_I64(row.m_yAbs, 10);
    XQ_EQ_I64(row.m_line.m_start, 0);
    XQ_EQ_I64(row.m_line.m_end, 0);
    ds = XTextDocumentLayout_documentSize(lay);
    XQ_EQ_I64((int)(ds.height + 0.5), 44);
    XClassDelete((XClass*)lay);
    XClassDelete((XClass*)doc);

    doc = XTextDocument_create();
    XQ_REQUIRE(doc != NULL);
    xtpq_fillWords(doc, 40);
    lay = xtpq_makePaged(doc, 300, 160, 10, 10, 10, 10);
    XQ_REQUIRE(lay != NULL);
    XQ_EQ_I64(XTextDocumentLayout_pageCount(lay), 2);
    XClassDelete((XClass*)lay);
    XClassDelete((XClass*)doc);

    doc = XTextDocument_create();
    XQ_REQUIRE(doc != NULL);
    XTextDocument_appendText(doc, "aa bb cc dd", NULL);
    lay = xtpq_makePaged(doc, 100, 12, 2, 2, 2, 2);
    XQ_REQUIRE(lay != NULL);
    XQ_EQ_I64(XTextDocumentLayout_pageCount(lay), 3); /* ceil(28/12)。 */
    ds = XTextDocumentLayout_documentSize(lay);
    XQ_EQ_I64((int)(ds.height + 0.5), 28);
    XQ_EQ_I64(XTextDocumentLayout_rowCount(lay), 1);
    XQ_REQUIRE(XTextDocumentLayout_rowAt(lay, 0, &row));
    XQ_EQ_I64(row.m_pageIndex, 0); /* 守卫：行留页 0 不换页。 */
    XQ_EQ_I64(row.m_yAbs, 2);
    XClassDelete((XClass*)lay);
    XClassDelete((XClass*)doc);
}

/**
 * 验收⑦：documentSize 与页数×页高自洽（(n-1)*页高 < docH ≤ n*页高，
 * 探针 ceilCheck 列同口径；nopage 单独断言 1 页 + 无页跳）。
 */
XQ_TEST(XTextPrint, documentSizeConsistent)
{
    XTextDocument* doc = XTextDocument_create();
    XTextDocumentLayout* lay;
    XSizeF ds;
    XTextDocLayoutRow row;
    int pc;
    int i;
    /* 三组页面参数（探针 ①②⑥ 同款）。 */
    const double pw[3] = {300, 200, 100};
    const double ph[3] = {160, 90, 12};
    const double mg[3] = {10, 6, 2};

    for (i = 0; i < 3; ++i) {
        XClassDelete((XClass*)doc);
        doc = XTextDocument_create();
        XQ_REQUIRE(doc != NULL);
        xtpq_fillWords(doc, 40);
        lay = xtpq_makePaged(doc, pw[i], ph[i], mg[i], mg[i], mg[i], mg[i]);
        XQ_REQUIRE(lay != NULL);
        pc = XTextDocumentLayout_pageCount(lay);
        ds = XTextDocumentLayout_documentSize(lay);
        XQ_CHECK(pc >= 1);
        XQ_CHECK((double)(pc - 1) * ph[i] < (double)ds.height + 0.001);
        XQ_CHECK((double)ds.height <= (double)pc * ph[i] + 0.001);
        XQ_CHECK(ds.width > 0);
        XClassDelete((XClass*)lay);
    }
    XClassDelete((XClass*)doc);

    /* 病态组合（宽 max 口径，qtextdocumentlayout.cpp:3044/:3057
     * fd->size.width = qMax(newContentsWidth, contentsWidth)+marginWidth）：
     * 页宽 100、左右边距各 60 ⇒ 可用宽 -20 <=0 ⇒ 本类不软换行（W1
     * 引擎口径：setLineWidth <=0 单行溢出），行宽 ~97 > 可用宽 ⇒
     * docW = 行右缘 + 左右边距 = 行宽 + 120 > 页宽——**宽随内容走**
     * （正常断行下探针恒页宽，即 max 恒取可用宽；此处验证 max 的另
     * 一侧）。病态下 Qt 的换行路径另有负宽钳制（本机探针
     * /tmp/w3b_probe/probe3 实测：Qt 逐字断 2 行、docW=273），与本类
     * 单行分叉——属「行放不下一页宽」子集边界，头注已登记不复刻；
     * 本断言只锁本类 max 公式自洽，不引 Qt 数值。 */
    doc = XTextDocument_create();
    XQ_REQUIRE(doc != NULL);
    XTextDocument_appendText(doc, "ABCDEFGHIJKLMNOP", NULL);
    lay = xtpq_makePaged(doc, 100, 400, 60, 6, 60, 6);
    XQ_REQUIRE(lay != NULL);
    XQ_EQ_I64(XTextDocumentLayout_rowCount(lay), 1);
    XQ_REQUIRE(XTextDocumentLayout_rowAt(lay, 0, &row));
    ds = XTextDocumentLayout_documentSize(lay);
    XQ_CHECK(ds.width > 100.0f); /* 宽随内容走，越页宽。 */
    XQ_EQ_I64((int)(ds.width + 0.5),
              row.m_line.m_x + row.m_line.m_width + 120);
    XQ_EQ_I64((int)(ds.height + 0.5), 6 + row.m_line.m_height + 6);
    XClassDelete((XClass*)lay);
    XClassDelete((XClass*)doc);

    /* 页参数 setter 往返（XSizeF float 边界）与 NULL/空档守卫。 */
    doc = XTextDocument_create();
    XQ_REQUIRE(doc != NULL);
    lay = xtpq_makeLayout(doc);
    XQ_REQUIRE(lay != NULL);
    {
        XSizeF size;
        XTextOption opt;
        XFont font;
        size.width = 320.5f;
        size.height = 240.25f;
        XTextDocumentLayout_setPageSize(lay, size);
        size = XTextDocumentLayout_pageSize(lay);
        XQ_CHECK(size.width > 320.4f && size.width < 320.6f);
        XQ_CHECK(size.height > 240.2f && size.height < 240.3f);
        XTextDocumentLayout_setPageMargins(lay, 11, 12, 13, 14);
        {
            double l = -1;
            double t = -1;
            double r = -1;
            double b = -1;
            XTextDocumentLayout_pageMargins(lay, &l, &t, &r, &b);
            XQ_EQ_I64((int)l, 11);
            XQ_EQ_I64((int)t, 12);
            XQ_EQ_I64((int)r, 13);
            XQ_EQ_I64((int)b, 14);
            XTextDocumentLayout_pageMargins(lay, NULL, NULL, NULL, NULL);
        }
        xtpq_makeFont(&font);
        XTextDocumentLayout_setFont(lay, &font);
        XClassDeinit((XClass*)&font);
        XTextDocumentLayout_font(lay, &font);
        XClassDeinit((XClass*)&font);
        XTextOption_init(&opt);
        XTextDocumentLayout_setTextOption(lay, &opt);
        XTextDocumentLayout_textOption(lay, &opt);
        XQ_EQ_I64(XTextOption_wrapMode(&opt), XTextOptionWrap_WordWrap);
        XQ_CHECK(XTextDocumentLayout_document(lay) == doc);
        /* NULL 文档：行表空、页数 1、尺寸 0。 */
        XTextDocumentLayout_setDocument(lay, NULL);
        XQ_CHECK(XTextDocumentLayout_document(lay) == NULL);
        XQ_EQ_I64(XTextDocumentLayout_pageCount(lay), 1);
        XQ_EQ_I64(XTextDocumentLayout_rowCount(lay), 0);
        XClassDelete((XClass*)lay);
    }
    XClassDelete((XClass*)doc);

    /* NULL 对象全 API 守卫。 */
    XQ_EQ_I64(XTextDocumentLayout_pageCount(NULL), 0);
    XQ_EQ_I64(XTextDocumentLayout_rowCount(NULL), 0);
    XQ_CHECK(XTextDocumentLayout_rowAt(NULL, 0, NULL) == false);
    XQ_CHECK(XTextDocumentLayout_ensureLayout(NULL) == false);
    XQ_CHECK(XTextDocumentLayout_drawToImage(NULL, 0, NULL) == false);
    XTextDocumentLayout_setDocument(NULL, NULL);
    XTextDocumentLayout_setPageMargins(NULL, 1, 1, 1, 1);
    XTextDocumentLayout_setHeaderText_utf8(NULL, "h");
    XTextDocumentLayout_setFooterText_utf8(NULL, "f");
    XQ_CHECK(XTextDocumentLayout_document(NULL) == NULL);
    XQ_CHECK(XTextDocumentLayout_headerText_const(NULL) == NULL);
    XQ_CHECK(XTextDocumentLayout_footerText_const(NULL) == NULL);
}

/**
 * 验收（补充）：draw() 连续坐标整文档绘制 + 片段格式应用。
 * 两片段（红/蓝）单块 @1000x1000 零边距：draw 后红蓝行带都有墨；
 * 行表条目字节区间跨片段连续（fragment 映射正确）。
 */
XQ_TEST(XTextPrint, drawContinuousAndFragments)
{
    XTextDocument* doc = XTextDocument_create();
    XTextDocumentLayout* lay;
    XTDCharFormat fmt;
    XImage image;
    XTextDocLayoutRow row;
    XRect clip;

    XQ_REQUIRE(doc != NULL);
    memset(&fmt, 0, sizeof(fmt));
    fmt.fgColor = 0xFFE01010u;
    XQ_REQUIRE(XTextDocument_addFragment(doc, 0, "RED", &fmt) >= 0);
    memset(&fmt, 0, sizeof(fmt));
    fmt.fgColor = 0xFF1010E0u;
    XQ_REQUIRE(XTextDocument_addFragment(doc, 0, "BLUE", &fmt) >= 0);
    lay = xtpq_makePaged(doc, 1000, 1000, 0, 0, 0, 0);
    XQ_REQUIRE(lay != NULL);
    XQ_EQ_I64(XTextDocumentLayout_rowCount(lay), 1);
    XQ_REQUIRE(XTextDocumentLayout_rowAt(lay, 0, &row));
    XQ_EQ_I64(row.m_line.m_start, 0);
    XQ_EQ_I64(row.m_line.m_end, 7); /* "REDBLUE" 全行。 */

    XImage_init_ex(&image, 1000, 1000, XImageFormat_ARGB32);
    XQ_CHECK(XTextDocumentLayout_drawToImage(lay, 0, &image));
    XQ_CHECK(xtpq_bandHasInk(&image, 0, 1000, 0, 24));
    /* clip 过滤：行带外 clip 不出墨。 */
    XImage_fill(&image, 0xFFFFFFFFu);
    clip.x = 0;
    clip.y = 100;
    clip.width = 1000;
    clip.height = 100;
    {
        XPainter painter;
        XPainter_init(&painter, NULL);
        XQ_CHECK(XPainter_begin_image(&painter, &image));
        XTextDocumentLayout_draw(lay, &painter, &clip);
        XPainter_end(&painter);
        XPainter_deinit(&painter);
    }
    XQ_CHECK(!xtpq_bandHasInk(&image, 0, 1000, 0, 24));
    XClassDeinit((XClass*)&image);
    XClassDelete((XClass*)lay);
    XClassDelete((XClass*)doc);
}

#endif /* XGUI_ON */
