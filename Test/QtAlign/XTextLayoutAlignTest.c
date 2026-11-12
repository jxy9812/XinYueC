/**
 * @file       XTextLayoutAlignTest.c
 * @brief      XTextLayout 与 Qt 6.8 QTextLayout 的对齐契约测试。
 * @details    对齐依据（本机 Qt 6.8.0 offscreen 探针 + Qt 6.8.3 原文）：
 *             - 探针程序与报告：/tmp/xtextlayout_probe/（probe_main.cpp/
 *               probe3.cpp + report.txt/report2.txt/report3.txt，字体经
 *               QFontDatabase::addApplicationFont 加载框架同一份
 *               Library/XFont/XFontOutlineCommon.otf，pixelSize=16，
 *               kerning=false）。**行宽配对用 PreferFullHinting 探针
 *               （report3.txt）**：Qt 整值步进（每字形 advance 取整）
 *               与框架度量链逐字形整值步进同口径——此配对下逐行宽
 *               0~1px 差；Qt 缺省 HintLight 为 26.6 定点小数步进，
 *               与框架整值步进逐字累差（如 "Hello" 39.250 vs 41），
 *               该差异属字形步进取整策略、非断行算法，见 openNotes。
 *             - 断行行为原文：qtextlayout.cpp:1600-1606（WordWrap
 *               超长词溢出不硬断）、:2158-2167（模式 4 逐行降级
 *               WrapAnywhere）、:2157-2193（软断行行尾空白不计宽，
 *               IncludeTrailingSpaces 计入）、:802（createLine 跳
 *               trailingSpaces）；qtextengine.cpp:3200-3301
 *               （calculateTabWidth）、:3396-3411（alignLine）、
 *               :2432-2619（justify）；qtextlayout.cpp:2916-3067/
 *               :3078-3268（cursorToX/xToCursor）。
 *             - 与 Qt 的登记差异：位置为 UTF-8 字节偏移（框架全树
 *               口径）；'\n' 按框架平铺行模型拆段（Qt 裸 QTextLayout
 *               不拆 '\n'——探针 newlines 行 lineCount=1，见
 *               XTextLayout.h 头注）；RTL/bidi 不在本子集。
 * @note       本文件与其它 QtAlign 用例一样自注册、无 main；泄漏门按
 *             xq_align_run.c 缺省零容忍（--leak-tolerance=0）执行。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

#if XGUI_ON

#include "XTextLayout.h"
#include "XTextOption.h"
#include "XClass.h"
#include "XMemory.h"
#include "XString.h"
#include "XTextUtf8.h"

#include <string.h>

/* 水平对齐位（XTDAlignment 同值空间 = Qt::AlignmentFlag 水平子集；
 * 枚举本体在 XTextDocument.h 的 XTEXTDOCUMENT_ON 段内，本文件不依赖
 * 该开关 ⇒ 常量本地承载）：Right=0x02/HCenter=0x04/Justify=0x08。 */
#define XTLQ_ALIGN_RIGHT 0x02
#define XTLQ_ALIGN_HCENTER 0x04
#define XTLQ_ALIGN_JUSTIFY 0x08

/* ==================== 测试脚手架 ==================== */

/** @brief 构造 16px 框架轮廓字体（与 Qt 探针同一份 .otf 同字号）。 */
static void xtlq_makeFont(XFont* font)
{
    XFont_init(font);
    XFont_setFamily(font, "XFontOutlineCommon");
    XFont_setPixelSize(font, 16);
}

/** @brief 构造已配好字体/文本/断行模式/宽度的布局对象（调用方删除）。 */
static XTextLayout* xtlq_makeLayout(const char* utf8, int width, int wrapMode)
{
    XTextLayout* self = XTextLayout_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    XFont font;
    XTextOption opt;
    if (!self) return NULL;
    xtlq_makeFont(&font);
    XTextLayout_setFont(self, &font);
    XClassDeinit((XClass*)&font);
    XTextLayout_setText_utf8(self, utf8, -1);
    XTextOption_init(&opt);
    XTextOption_setWrapMode(&opt, wrapMode);
    XTextLayout_setTextOption(self, &opt);
    XTextLayout_setLineWidth(self, width);
    return self;
}

/** @brief 断言行 i 的字节区间与自然宽（qtWidth>=0 时加 |diff|<=1 门）。 */
static void xtlq_expectRow(XTextLayout* self, int index, int start, int end,
                           int qtWidth, const char* what)
{
    XTextLayoutLine row;
    if (!XTextLayout_lineAt(self, index, &row)) {
        xq_fail_at(__FILE__, __LINE__, "%s：行 %d 越界（共 %d 行）", what,
                   index, XTextLayout_lineCount(self));
        return;
    }
    xq_check_impl(row.m_start == start && row.m_end == end,
                  "row.start/end", __FILE__, __LINE__,
                  "%s：行 %d 区间实际 [%d,%d) 期望 [%d,%d)", what, index,
                  row.m_start, row.m_end, start, end);
    if (qtWidth >= 0) {
        int d = row.m_width - qtWidth;
        if (d < 0) d = -d;
        xq_check_impl(d <= 1, "|row.width-qt|<=1", __FILE__, __LINE__,
                      "%s：行 %d 宽 fw=%d qt探针=%d（差 %d）", what, index,
                      row.m_width, qtWidth, d);
    }
}

/**
 * @brief      六向自洽往返（验收⑤）：行表内每个码点边界 pos 走
 *             lineForTextPosition → positionToLineCol → lineColToPosition、
 *             colToX → xToCol、hitTest 三条回路恒等。
 * @details    边界归属（引擎口径）：pos == 行尾 == 下一软断行首 →
 *             归属**下一行**（(l+1,0)；光标在软断点落行首，与
 *             lineForTextPosition 二分一致）；pos == 行间 '\n' 字节 →
 *             归属前行（行尾光标位）。col == 行长（行尾光标位）不做
 *             colToX/xToCol 恒等（溢出行 colToX 钳可用宽、xToCol 取
 *             最近界，Qt cursorToX/xToCursor 同不互逆——探针
 *             longword-ww cursorToX(4)=40.000，单独断言钳位）。
 */
static void xtlq_roundtrip(XTextLayout* self, const char* what)
{
    int len = XTextLayout_textLength(self);
    int lines = XTextLayout_lineCount(self);
    int lineHeight = XTextLayout_totalHeight(self) /
                     (lines > 0 ? lines : 1);
    int l;
    for (l = 0; l < lines; ++l) {
        XTextLayoutLine row;
        int col = 0;
        int rowLen;
        if (!XTextLayout_lineAt(self, l, &row)) continue;
        rowLen = row.m_end - row.m_start;
        while (col <= rowLen) {
            int p = XTextLayout_lineColToPosition(self, l, col);
            int l2 = -1;
            int c2 = -1;
            int l3 = -1;
            XTextLayoutLine rowForPos;
            bool atRowEnd = col == rowLen;
            bool softBoundary = atRowEnd && l + 1 < lines &&
                                XTextLayout_lineAt(self, l + 1, &rowForPos) &&
                                rowForPos.m_start == row.m_end;
            xq_check_impl(XTextLayout_lineColToPosition(self, l, col) == p,
                          "lineColToPosition", __FILE__, __LINE__,
                          "%s：(%d,%d) 逆映射实际 %d 期望 %d", what, l, col,
                          XTextLayout_lineColToPosition(self, l, col), p);
            /* 行内位置（非软断行尾）六向恒等。 */
            if (!softBoundary) {
                int x;
                int c3;
                int p2 = -1;
                bool hit;
                bool overflowEnd = atRowEnd &&
                                   row.m_width > XTextLayout_lineWidth(self);
                xq_check_impl(
                    XTextLayout_lineForTextPosition(self, p, NULL) == l,
                    "lineForTextPosition", __FILE__, __LINE__,
                    "%s：pos=%d 行号不一致（期望 %d）", what, p, l);
                XTextLayout_positionToLineCol(self, p, &l2, &c2);
                xq_check_impl(l2 == l && c2 == col, "positionToLineCol",
                              __FILE__, __LINE__,
                              "%s：pos=%d 实际 (%d,%d) 期望 (%d,%d)", what, p,
                              l2, c2, l, col);
                x = XTextLayout_colToX(self, l, col);
                c3 = XTextLayout_xToCol(self, l, x);
                /* 溢出行行尾列的 x 已钳到可用宽（Qt cursorToX :3060-3063），
                 * xToCol 按最近界回落到钳位前最后一界（Qt xToCursor 同
                 * 不互逆——探针 longword-ww cursorToX(4)=40.000），跳过
                 * 该格恒等、改断言钳位本身。 */
                if (!overflowEnd)
                    xq_check_impl(c3 == col, "colToX/xToCol", __FILE__,
                                  __LINE__,
                                  "%s：行 %d col=%d x=%d 回读列 %d", what, l,
                                  col, x, c3);
                else
                    xq_check_impl(x == XTextLayout_lineWidth(self) &&
                                      c3 <= col,
                                  "overflow-end-clamp", __FILE__, __LINE__,
                                  "%s：溢出行尾 x=%d 未钳到 %d 或回读列 %d "
                                  "越界", what, x,
                                  XTextLayout_lineWidth(self), c3);
                /* hitTest 先落 bool 再断言：xq_check_impl 各实参求值顺序
                    未指定，p2 不能同批读写。溢出行行尾列按钳位 x 命中到
                    最后界（c3），与 xToCol 同口径。 */
                hit = XTextLayout_hitTest(self, x,
                                          l * lineHeight + lineHeight / 2,
                                          &p2);
                xq_check_impl(hit && p2 == (overflowEnd
                                                ? row.m_start + c3
                                                : p),
                              "hitTest", __FILE__, __LINE__,
                              "%s：(%d,%d) 命中实际 %d 期望 %d", what, x,
                              l * lineHeight, p2,
                              overflowEnd ? row.m_start + c3 : p);
            } else {
                /* 软断行尾位：归属下一行首（光标边沿口径）。 */
                XTextLayout_positionToLineCol(self, p, &l2, &c2);
                xq_check_impl(l2 == l + 1 && c2 == 0,
                              "soft-boundary-pos", __FILE__, __LINE__,
                              "%s：软断位 pos=%d 实际 (%d,%d) 期望 (%d,0)",
                              what, p, l2, c2, l + 1);
                xq_check_impl(
                    XTextLayout_lineForTextPosition(self, p, NULL) == l + 1,
                    "soft-boundary-line", __FILE__, __LINE__,
                    "%s：软断位 pos=%d 行号实际 %d 期望 %d", what, p,
                    XTextLayout_lineForTextPosition(self, p, NULL), l + 1);
            }
            l3 = XTextLayout_lineForTextPosition(self, p, &rowForPos);
            xq_check_impl(l3 >= 0 && rowForPos.m_start <= p,
                          "lineForTextPosition(out)", __FILE__, __LINE__,
                          "%s：pos=%d 行投影 l=%d start=%d 越界", what, p,
                          l3, rowForPos.m_start);
            if (col >= rowLen) break;
            col += XTextUtf8_seqLen(XTextLayout_text_const(self) +
                                        row.m_start + col,
                                    rowLen - col);
        }
    }
    /* 行间 '\n' 字节：归属前行（行尾光标位）。 */
    {
        const char* text = XTextLayout_text_const(self);
        int i;
        for (i = 0; i < len; ++i) {
            if (text[i] == '\n') {
                int l4 = XTextLayout_lineForTextPosition(self, i, NULL);
                int l5 = -1;
                int c5 = -1;
                XTextLayout_positionToLineCol(self, i, &l5, &c5);
                xq_check_impl(l4 == l5 && l5 >= 0,
                              "newline-pos-line", __FILE__, __LINE__,
                              "%s：'\\n'@%d 行号 lineFor=%d posTo=%d", what,
                              i, l4, l5);
            }
        }
    }
}

/* ==================== ① NoWrap 单行 ==================== */

XQ_TEST(XTextLayout, noWrapSingleLine)
{
    XTextLayout* self = xtlq_makeLayout("Hi", 1000,
                                        (int)XTextOptionWrap_NoWrap);
    XTextLayoutLine row;
    XFont copy;
    XQ_REQUIRE(self != NULL);
    XQ_EQ_I64(XTextLayout_lineCount(self), 1);
    XQ_REQUIRE(XTextLayout_lineAt(self, 0, &row));
    XQ_EQ_I64(row.m_start, 0);
    XQ_EQ_I64(row.m_end, 2);
    /* Qt 探针（report3 nowrap-Hi）：natW=16.000。框架 H=12+i=4=16，差 0。 */
    XQ_EQ_I64(row.m_width, 16);
    /* 度量：框架实测 ascent=19/descent=5（adv_probe2）；Qt 探针（report1
     * LIGHT）ascent=18.563 descent=4.609 height=24.000（ceil 23.172）。
     * 行高 = ascent+descent = 24，与 Qt height 差 0；baseline == ascent。 */
    XQ_EQ_I64(row.m_ascent, 19);
    XQ_EQ_I64(row.m_descent, 5);
    XQ_EQ_I64(row.m_height, 24);
    XQ_EQ_I64(row.m_baseline, 19);
    XQ_EQ_I64(row.m_x, 0);
    XQ_EQ_I64(row.m_y, 0);
    XQ_EQ_I64(XTextLayout_totalHeight(self), 24);
    /* NoWrap 长行溢出仍单行（宽 = 全内容；Qt 探针 report3
     * nowrap-helloWorld natW=90.000，框架 Hello World=90，差 0）。 */
    XTextLayout_setText_utf8(self, "Hello World", -1);
    XQ_EQ_I64(XTextLayout_lineCount(self), 1);
    XQ_REQUIRE(XTextLayout_lineAt(self, 0, &row));
    XQ_EQ_I64(row.m_width, 90);
    /* 字体深拷贝回读（font() 拷出独立件：两份拷贝独立 init/copy/deinit
       不触碰布局内字体；不换家族以免触发进程级字库槽装载（家族槽只增
       不复用，属进程缓存非用例泄漏）。 */
    XTextLayout_font(self, &copy);
    XQ_EQ_STR(XFont_family(&copy), "XFontOutlineCommon");
    XQ_EQ_I64(XFont_pixelSize(&copy), 16);
    XClassDeinit((XClass*)&copy);
    XTextLayout_font(self, &copy);
    XQ_EQ_STR(XFont_family(&copy), "XFontOutlineCommon");
    XClassDeinit((XClass*)&copy);
    XClassDelete((XClass*)self);
}

/* ==================== ② WordWrap：词界断行（CJK 逐字 + 词中不断） ==== */

XQ_TEST(XTextLayout, wordWrapLatinAndCjk)
{
    XTextLayout* self = xtlq_makeLayout("Hello big World", 40,
                                        (int)XTextOptionWrap_WordWrap);
    XQ_REQUIRE(self != NULL);
    /* Qt 探针（report3 ww-helloBigWorld40，PreferFullHinting）：
     * L0 from=0 len=6 natW=41.000（"Hello␣"，行尾空白不计宽）
     * L1 from=6 len=4 natW=23.000（"big␣"）
     * L2 from=10 len=5 natW=45.000（"World"）
     * 框架逐行宽 41/23/45（adv_probe2 逐字和），三行差全 0。 */
    XQ_EQ_I64(XTextLayout_lineCount(self), 3);
    xtlq_expectRow(self, 0, 0, 6, 41, "ww-L0");
    xtlq_expectRow(self, 1, 6, 10, 23, "ww-L1");
    xtlq_expectRow(self, 2, 10, 15, 45, "ww-L2");
    XClassDelete((XClass*)self);

    /* CJK 逐字可断：Qt 探针（report3 ww-cjk48，UTF-16 位）：L0 [0,3)
     * natW=48.000、L1 [3,6) natW=48.000（每字 16px）。框架字节域
     * （每汉字 3 字节）等价区间 [0,9)/[9,18)，宽 48/48 差 0。 */
    self = xtlq_makeLayout("汉字断行测试", 48,
                           (int)XTextOptionWrap_WordWrap);
    XQ_REQUIRE(self != NULL);
    XQ_EQ_I64(XTextLayout_lineCount(self), 2);
    xtlq_expectRow(self, 0, 0, 9, 48, "cjk-L0");
    xtlq_expectRow(self, 1, 9, 18, 48, "cjk-L1");
    XClassDelete((XClass*)self);

    /* 混合词+CJK：Qt 探针（report3 ww-mixed40，UTF-16 位）：L0 [0,4)
     * 39.000、L1 [4,8) 39.000。框架字节域 [0,6) "go 深"=9+10+4+16=39、
     * [6,12) "入 go"=16+4+9+10=39，差 0。 */
    self = xtlq_makeLayout("go 深入 go", 40,
                           (int)XTextOptionWrap_WordWrap);
    XQ_REQUIRE(self != NULL);
    XQ_EQ_I64(XTextLayout_lineCount(self), 2);
    xtlq_expectRow(self, 0, 0, 6, 39, "mixed-L0");
    xtlq_expectRow(self, 1, 6, 12, 39, "mixed-L1");
    XClassDelete((XClass*)self);

    /* ASCII 词中间不断："aa bb cccccc"@20 → 行界 3/6（词界），无词中断。 */
    self = xtlq_makeLayout("aa bb cccccc", 20,
                           (int)XTextOptionWrap_WordWrap);
    XQ_REQUIRE(self != NULL);
    XQ_REQUIRE(XTextLayout_lineCount(self) == 3);
    xtlq_expectRow(self, 0, 0, 3, -1, "nowordcut-L0");
    xtlq_expectRow(self, 1, 3, 6, -1, "nowordcut-L1");
    xtlq_expectRow(self, 2, 6, 12, -1, "nowordcut-L2");
    XClassDelete((XClass*)self);
}

/* ==================== ③ WrapAnywhere：每字符可断 ==================== */

XQ_TEST(XTextLayout, wrapAnywherePerGlyph)
{
    XTextLayout* self = xtlq_makeLayout("Hello big", 16,
                                        (int)XTextOptionWrap_Anywhere);
    XQ_REQUIRE(self != NULL);
    /* Qt 探针（report3 wa-helloBig16）：5 行
     * [0,1) 12.000 / [1,3) 14.000 / [3,6) 15.000（"lo␣" 不含空白串）/
     * [6,8) 14.000 / [8,9) 9.000。框架逐行全 0 差。 */
    XQ_EQ_I64(XTextLayout_lineCount(self), 5);
    xtlq_expectRow(self, 0, 0, 1, 12, "wa-L0");
    xtlq_expectRow(self, 1, 1, 3, 14, "wa-L1");
    xtlq_expectRow(self, 2, 3, 6, 15, "wa-L2");
    xtlq_expectRow(self, 3, 6, 8, 14, "wa-L3");
    xtlq_expectRow(self, 4, 8, 9, 9, "wa-L4");
    XClassDelete((XClass*)self);
}

/* ==================== 模式 4：词界优先、溢出行降级逐字 ==================== */

XQ_TEST(XTextLayout, wordBoundaryOrAnywhereFallback)
{
    XTextLayout* self = xtlq_makeLayout("Hello biggish", 30,
                                        (int)XTextOptionWrap_WordBoundaryOrAnywhere);
    XQ_REQUIRE(self != NULL);
    /* Qt 探针（report3 wba-helloBiggish30）："Hello" 整词溢出（41>30）⇒
     * 行 0 降级逐字切 "Hel"；"biggish" 同 ⇒ "big|gish"：
     * L0 [0,3) 26.000 / L1 [3,6) 15.000 / L2 [6,9) 23.000 / L3 [9,13) 30.000。
     * 框架逐行全 0 差（qtextlayout.cpp:2158-2167 降级口径）。 */
    XQ_EQ_I64(XTextLayout_lineCount(self), 4);
    xtlq_expectRow(self, 0, 0, 3, 26, "wba-L0");
    xtlq_expectRow(self, 1, 3, 6, 15, "wba-L1");
    xtlq_expectRow(self, 2, 6, 9, 23, "wba-L2");
    xtlq_expectRow(self, 3, 9, 13, 30, "wba-L3");
    XClassDelete((XClass*)self);

    /* 超长不可断词：WordWrap 溢出不硬断（⑦；qtextlayout.cpp:1600-1606
     * "filled with additional characters to the next whitespace"；
     * 探针 report3 ww-longword40：单行 natW=163.000；框架整串 163，
     * 差 0（adv_probe2）。 */
    self = xtlq_makeLayout("ABCDEFGHIJKLMNOP", 40,
                           (int)XTextOptionWrap_WordWrap);
    XQ_REQUIRE(self != NULL);
    XQ_EQ_I64(XTextLayout_lineCount(self), 1);
    xtlq_expectRow(self, 0, 0, 16, 163, "longword-ww");
    /* 溢出行 colToX 钳到可用宽（Qt cursorToX :3060-3063；report1
     * longword-ww cursorToX(4)=40.000）。 */
    {
        int x4 = XTextLayout_colToX(self, 0, 4);
        XQ_CHECK_MSG(x4 == 40, "溢出行列 x 钳到可用宽（实际 %d）", x4);
    }
    XClassDelete((XClass*)self);

    /* 同词 mode4：逐字切满（report3 wba-longword40：5 行
     * [0,3) 31 / [3,7) 40 / [7,11) 37 / [11,14) 34 / [14,16) 22；
     * 框架 31/40/36/34/22——HIJK 行差 1（Qt-FULL H 步进 13 vs 框架 12，
     * 逐字整值取整的 1px 舍入差），其余 0。 */
    self = xtlq_makeLayout("ABCDEFGHIJKLMNOP", 40,
                           (int)XTextOptionWrap_WordBoundaryOrAnywhere);
    XQ_REQUIRE(self != NULL);
    XQ_EQ_I64(XTextLayout_lineCount(self), 5);
    xtlq_expectRow(self, 0, 0, 3, 31, "wba-lw-L0");
    xtlq_expectRow(self, 1, 3, 7, 40, "wba-lw-L1");
    xtlq_expectRow(self, 2, 7, 11, 37, "wba-lw-L2");
    xtlq_expectRow(self, 3, 11, 14, 34, "wba-lw-L3");
    xtlq_expectRow(self, 4, 14, 16, 22, "wba-lw-L4");
    XClassDelete((XClass*)self);
}

/* ==================== ④ 制表位推进 ==================== */

XQ_TEST(XTextLayout, tabStops)
{
    XTextLayout* self = xtlq_makeLayout("a\tbc\tdef", 1000,
                                        (int)XTextOptionWrap_NoWrap);
    XTextOption opt;
    XQ_REQUIRE(self != NULL);
    /* 缺省制表距 30：Qt 探针（report1 tabs，LIGHT）cursorToX(2)=30.000、
     * cursorToX(5)=60.000（calculateTabWidth 缺省档
     * (floor(x/stop)+1)*stop，qtextengine.cpp:3293-3298）。
     * 框架：a=9 → tab 推到 30；bc=18 → 48 → tab 推到 60；def=24 → 84。 */
    XTextOption_init(&opt);
    XTextOption_setTabStopDistance(&opt, 30.0);
    XTextLayout_setTextOption(self, &opt);
    XQ_EQ_I64(XTextLayout_lineCount(self), 1);
    XQ_CHECK_MSG(XTextLayout_colToX(self, 0, 2) == 30,
                 "tab@缺省30：col2 实际 %d（探针 30）",
                 XTextLayout_colToX(self, 0, 2));
    XQ_CHECK_MSG(XTextLayout_colToX(self, 0, 5) == 60,
                 "tab@缺省30：col5 实际 %d（探针 60）",
                 XTextLayout_colToX(self, 0, 5));
    /* 整行宽：框架 84 vs Qt 探针 natW=85.000（右承痕 1px 舍入），差 1。 */
    {
        XTextLayoutLine row;
        XQ_REQUIRE(XTextLayout_lineAt(self, 0, &row));
        XQ_CHECK_MSG(row.m_width >= 84 && row.m_width <= 85,
                     "tabs30 行宽实际 %d（Qt 探针 85）", row.m_width);
    }
    /* 显式制表位表（LeftTab 25/60）：Qt 探针（report1 tabs-explicit）
     * cursorToX(2)=25.000、cursorToX(5)=60.000（首个 position > x 的
     * 表位，qtextengine.cpp:3236-3238 严格大于）。 */
    {
        double tabs[2];
        tabs[0] = 25.0;
        tabs[1] = 60.0;
        XTextOption_init(&opt);
        XTextOption_setTabArray(&opt, tabs, 2);
        XTextLayout_setTextOption(self, &opt);
        XQ_CHECK_MSG(XTextLayout_colToX(self, 0, 2) == 25,
                     "tab@显式25：col2 实际 %d（探针 25）",
                     XTextLayout_colToX(self, 0, 2));
        XQ_CHECK_MSG(XTextLayout_colToX(self, 0, 5) == 60,
                     "tab@显式60：col5 实际 %d（探针 60）",
                     XTextLayout_colToX(self, 0, 5));
    }
    /* 右对齐制表位：文字尾沿落在表位（Qt :3281 tab=position-段宽；
       段宽 = 到段末 "bc\tdef"... 用 "a\tbcd" 显式 RightTab@40：
       段宽 bcd=10+8+10=28 → 起点 12 → x=12+9(a)=21？直接取位语义：
       tab 推进 = 40-28-9(a)=3 → col2 x=12。 */
    {
        XTextOptionTab tab;
        XTextOption_init(&opt);
        tab.position = 40.0;
        tab.type = (int)XTextOptionTab_Right;
        tab.delimiter = 0;
        XTextOption_setTabs(&opt, &tab, 1);
        XTextLayout_setText_utf8(self, "a\tbcd", -1);
        XTextLayout_setTextOption(self, &opt);
        /* 框架：a=9，段宽 bcd=28，推进 = 40-28-9 = 3 ⇒ col2 x=12。 */
        XQ_CHECK_MSG(XTextLayout_colToX(self, 0, 2) == 12,
                     "RightTab@40 段宽28：col2 实际 %d（语义 12）",
                     XTextLayout_colToX(self, 0, 2));
    }
    /* xToCol 对制表胞半宽取边（Qt TabOrObject 分支 :3157-3165）：缺省
       30 档 col2 x=30（胞 [9,30]），取中点 19 之前应落 col1、之后落 col2。 */
    XTextOption_init(&opt);
    XTextOption_setTabStopDistance(&opt, 30.0);
    XTextLayout_setText_utf8(self, "a\tbc\tdef", -1);
    XTextLayout_setTextOption(self, &opt);
    XQ_CHECK_MSG(XTextLayout_xToCol(self, 0, 19) == 1,
                 "tab 胞前半 xToCol 实际 %d（语义 1）",
                 XTextLayout_xToCol(self, 0, 19));
    XQ_CHECK_MSG(XTextLayout_xToCol(self, 0, 20) == 2,
                 "tab 胞后半 xToCol 实际 %d（语义 2）",
                 XTextLayout_xToCol(self, 0, 20));
    XClassDelete((XClass*)self);
}

/* ==================== ⑤ 六向自洽往返 ==================== */

XQ_TEST(XTextLayout, sixWayRoundtrip)
{
    XTextLayout* self = xtlq_makeLayout("Hello big World", 40,
                                        (int)XTextOptionWrap_WordWrap);
    XQ_REQUIRE(self != NULL);
    xtlq_roundtrip(self, "latin-ww");
    XClassDelete((XClass*)self);

    self = xtlq_makeLayout("汉字断行测试", 48,
                           (int)XTextOptionWrap_WordWrap);
    XQ_REQUIRE(self != NULL);
    xtlq_roundtrip(self, "cjk-ww");
    XClassDelete((XClass*)self);

    self = xtlq_makeLayout("Hello big", 16,
                           (int)XTextOptionWrap_Anywhere);
    XQ_REQUIRE(self != NULL);
    xtlq_roundtrip(self, "anywhere");
    XClassDelete((XClass*)self);

    /* 越界钳位：pos 钳 [0,len]、行外钳位。 */
    self = xtlq_makeLayout("Hello big World", 40,
                           (int)XTextOptionWrap_WordWrap);
    XQ_REQUIRE(self != NULL);
    {
        int l = -1;
        int c = -1;
        XTextLayout_positionToLineCol(self, -5, &l, &c);
        XQ_EQ_I64(l, 0);
        XQ_EQ_I64(c, 0);
        XTextLayout_positionToLineCol(self, 999, &l, &c);
        XQ_EQ_I64(l, XTextLayout_lineCount(self) - 1);
        XQ_EQ_I64(c, 5); /* 末行 "World" 长 5。 */
        XQ_EQ_I64(XTextLayout_xToCol(self, 0, -100), 0);
        XQ_EQ_I64(XTextLayout_xToCol(self, 0, 100000), 6);
        XQ_EQ_I64(XTextLayout_colToX(self, -1, 0), 0);
        XQ_EQ_I64(XTextLayout_colToX(self, 99, 0), 0);
        {
            int p = -1;
            XQ_CHECK(XTextLayout_hitTest(self, 0, -9, &p));
            XQ_EQ_I64(p, 0);
            XQ_CHECK(XTextLayout_hitTest(self, 0, 100000, &p));
            XQ_EQ_I64(p, 10); /* y 越界钳末行、x=0 → 末行行首。 */
            XQ_CHECK(XTextLayout_hitTest(self, 100000, 100000, &p));
            XQ_EQ_I64(p, 15); /* x/y 双越界 → 末行行尾。 */
            XQ_EQ_I64(p, 15);
            XQ_CHECK(!XTextLayout_hitTest(self, 0, 0, NULL)); /* outPos NULL。 */
        }
    }
    XClassDelete((XClass*)self);
}

/* ==================== ⑥ 行宽与 Qt 探针配对（≤1px） ==================== */

XQ_TEST(XTextLayout, widthParityWithQtProbe)
{
    /* 逐项（fw=框架实测，qt=report3 PreferFullHinting 探针）：
     * "Hi" 16/16.000、"Hello World" 90/90.000、"Hello big World"@40
     * 41/23/45 vs 41/23/45、"汉字断行测试"@48 48/48 vs 48/48、
     * "go 深入 go"@40 39/39 vs 39/39、"Hello big"@16 逐行全等、
     * "Hello biggish"@30 逐行全等、长词 163/163.000、
     * "abc"=27（Qt-FULL a9+b10+c8=27）——全部 |diff|<=1。 */
    XTextLayout* self = xtlq_makeLayout("abc", 1000,
                                        (int)XTextOptionWrap_NoWrap);
    XTextLayoutLine row;
    XQ_REQUIRE(self != NULL);
    XQ_REQUIRE(XTextLayout_lineAt(self, 0, &row));
    XQ_CHECK_MSG(row.m_width == 27, "\"abc\" 行宽实际 %d（探针 27）",
                 row.m_width);
    XClassDelete((XClass*)self);
    /* 其余配对已在 noWrapSingleLine/wordWrapLatinAndCjk/wrapAnywherePerGlyph/
       wordBoundaryOrAnywhereFallback 逐断言落地（xtlq_expectRow 的
       |diff|<=1 门），此处不重复建对象。 */
}

/* ==================== ⑦ 空文本/单字符/纯换行/边界 ==================== */

XQ_TEST(XTextLayout, edgeCases)
{
    XTextLayout* self = xtlq_makeLayout("", 100,
                                        (int)XTextOptionWrap_WordWrap);
    XTextLayoutLine row;
    XQ_REQUIRE(self != NULL);
    /* 空文本：1 空行（Qt 探针 report1 empty：lineCount=1、len=0、
     * natW=0、真实 ascent/descent——同口径）。 */
    XQ_EQ_I64(XTextLayout_lineCount(self), 1);
    XQ_REQUIRE(XTextLayout_lineAt(self, 0, &row));
    XQ_EQ_I64(row.m_start, 0);
    XQ_EQ_I64(row.m_end, 0);
    XQ_EQ_I64(row.m_width, 0);
    XQ_EQ_I64(row.m_height, 24);
    XQ_EQ_I64(XTextLayout_totalHeight(self), 24);
    {
        int p = -1;
        XQ_CHECK(XTextLayout_hitTest(self, 0, 0, &p));
        XQ_EQ_I64(p, 0);
    }
    XClassDelete((XClass*)self);

    /* 单字符。 */
    self = xtlq_makeLayout("x", 100, (int)XTextOptionWrap_WordWrap);
    XQ_REQUIRE(self != NULL);
    XQ_EQ_I64(XTextLayout_lineCount(self), 1);
    xtlq_expectRow(self, 0, 0, 1, 8, "onechar");
    XClassDelete((XClass*)self);

    /* 纯换行 "\n"：2 空行（框架平铺行模型；登记差异：Qt 裸
       QTextLayout 不拆 '\n'——探针 newlines lineCount=1，见头注）。 */
    self = xtlq_makeLayout("\n", 100, (int)XTextOptionWrap_WordWrap);
    XQ_REQUIRE(self != NULL);
    XQ_EQ_I64(XTextLayout_lineCount(self), 2);
    XQ_REQUIRE(XTextLayout_lineAt(self, 0, &row));
    XQ_EQ_I64(row.m_start, 0);
    XQ_EQ_I64(row.m_end, 0);
    XQ_REQUIRE(XTextLayout_lineAt(self, 1, &row));
    XQ_EQ_I64(row.m_start, 1);
    XQ_EQ_I64(row.m_end, 1);
    XQ_EQ_I64(XTextLayout_lineColToPosition(self, 1, 0), 1);
    XClassDelete((XClass*)self);

    /* 尾换行 "ab\n"：2 行（"ab" + 空）；'\n' 字节归属前行行尾光标位。 */
    self = xtlq_makeLayout("ab\n", 100, (int)XTextOptionWrap_WordWrap);
    XQ_REQUIRE(self != NULL);
    XQ_EQ_I64(XTextLayout_lineCount(self), 2);
    xtlq_expectRow(self, 0, 0, 2, -1, "trailnl-L0");
    XQ_REQUIRE(XTextLayout_lineAt(self, 1, &row));
    XQ_EQ_I64(row.m_start, 3);
    XQ_EQ_I64(row.m_end, 3);
    XQ_EQ_I64(XTextLayout_lineForTextPosition(self, 2, NULL), 0);
    XClassDelete((XClass*)self);

    /* 行尾空白串：软断行不计宽 / IncludeTrailingSpaces 计入
       （qtextlayout.cpp:2157-2189）。 */
    self = xtlq_makeLayout("Hello big World", 40,
                           (int)XTextOptionWrap_WordWrap);
    XQ_REQUIRE(self != NULL);
    xtlq_expectRow(self, 0, 0, 6, 41, "trailspace-off");
    {
        XTextOption opt;
        XTextOption_init(&opt);
        XTextOption_setWrapMode(&opt, (int)XTextOptionWrap_WordWrap);
        XTextOption_setFlags(&opt, (uint32_t)XTextOptionFlag_IncludeTrailingSpaces);
        XTextLayout_setTextOption(self, &opt);
        xtlq_expectRow(self, 0, 0, 6, -1, "trailspace-on");
        {
            XTextLayoutLine r2;
            XQ_REQUIRE(XTextLayout_lineAt(self, 0, &r2));
            XQ_EQ_I64(r2.m_width, 45); /* 41 + 空格 4。 */
        }
    }
    XClassDelete((XClass*)self);

    /* 段末行行尾空白计宽（Qt 终点提交 :1767-1795）：NoWrap "aa " → 22。 */
    self = xtlq_makeLayout("aa ", 1000, (int)XTextOptionWrap_NoWrap);
    XQ_REQUIRE(self != NULL);
    xtlq_expectRow(self, 0, 0, 3, 22, "final-trailing-space");
    XClassDelete((XClass*)self);

    /* useDesignMetrics 等值消费：选项往返保真，行宽不变（Qt 探针
       report1：designmetrics-on natW=87.203 == 缺省 87.203——
       HintLight 下两引擎均设计步进，等值）。 */
    self = xtlq_makeLayout("Hello World", 1000, (int)XTextOptionWrap_NoWrap);
    XQ_REQUIRE(self != NULL);
    {
        XTextOption opt;
        int w0;
        XTextLayoutLine r2;
        XQ_REQUIRE(XTextLayout_lineAt(self, 0, &r2));
        w0 = r2.m_width;
        XTextOption_init(&opt);
        XTextOption_setUseDesignMetrics(&opt, true);
        XTextLayout_setTextOption(self, &opt);
        XTextLayout_textOption(self, &opt);
        XQ_CHECK(XTextOption_useDesignMetrics(&opt));
        XQ_REQUIRE(XTextLayout_lineAt(self, 0, &r2));
        XQ_EQ_I64(r2.m_width, w0);
    }
    XClassDelete((XClass*)self);

    /* XString 重载与 UTF-8 长度重载同结果（内码缓冲 → toUtf8）。 */
    {
        XString* s = XString_create();
        XQ_REQUIRE(s != NULL);
        XString_assign_utf8(s, "汉字");
        self = xtlq_makeLayout("", 48, (int)XTextOptionWrap_WordWrap);
        XQ_REQUIRE(self != NULL);
        XTextLayout_setText(self, s);
        XQ_EQ_I64(XTextLayout_textLength(self), 6);
        XQ_EQ_I64(XTextLayout_lineCount(self), 1);
        xtlq_expectRow(self, 0, 0, 6, 32, "xstring-cjk");
        XClassDelete((XClass*)self);
        XClassDelete((XClass*)s);
    }
}

/* ==================== 对齐偏移与 Justify 分摊 ==================== */

XQ_TEST(XTextLayout, alignmentJustify)
{
    XTextLayout* self = xtlq_makeLayout("Hi", 40,
                                        (int)XTextOptionWrap_NoWrap);
    XTextOption opt;
    XTextLayoutLine row;
    XQ_REQUIRE(self != NULL);
    /* Right：偏移 = 可用宽 - 自然宽（qtextengine.cpp:3405-3406；Qt
       alignRight40 探针 cursorToX(0)=0.750 = 40-39.25 同式）。
       "Hi"@40：40-16=24。 */
    XTextOption_init(&opt);
    XTextOption_setAlignment(&opt, XTLQ_ALIGN_RIGHT);
    XTextLayout_setTextOption(self, &opt);
    XQ_REQUIRE(XTextLayout_lineAt(self, 0, &row));
    XQ_EQ_I64(row.m_x, 24);
    XQ_EQ_I64(XTextLayout_colToX(self, 0, 0), 24);
    XQ_EQ_I64(XTextLayout_colToX(self, 0, 2), 40);
    /* xToCol 扣对齐偏移（Qt xToCursor :3100-3101）；x=32 距列 1 边界
       (36) 4px、距列 0 (24) 8px → 列 1（x=30 是中点，平局取先界）。 */
    XQ_EQ_I64(XTextLayout_xToCol(self, 0, 32), 1);
    /* HCenter：一半（:3407-3408；探针 align-center cursorToX(0)=0.375
       同式）。40-16=24 → 12。 */
    XTextOption_init(&opt);
    XTextOption_setAlignment(&opt, XTLQ_ALIGN_HCENTER);
    XTextLayout_setTextOption(self, &opt);
    XQ_REQUIRE(XTextLayout_lineAt(self, 0, &row));
    XQ_EQ_I64(row.m_x, 12);
    /* 可用宽<=0（未启用软换行）不对齐（Qt QFIXED_MAX 跳过 :3400-3401）。 */
    XTextLayout_setLineWidth(self, 0);
    XQ_REQUIRE(XTextLayout_lineAt(self, 0, &row));
    XQ_EQ_I64(row.m_x, 0);
    XClassDelete((XClass*)self);

    /* Justify：词距分摊（qtextengine.cpp:2432-2619；整像素等价式复用
       XTextControl.c xtc_rowJustifyOf）。Qt 探针（report3 justify60，
       "aa bb cc dd"@60）：L0 [0,6) natW=42.000（1 空隙 → need=18 全记
       单空隙）、L1 [6,11) 段末行不拉伸 40.000。框架：行 0 自然宽 42、
       空格胞后加 18 → col3 x=40、行尾 60（=可用宽）；末行不拉伸。 */
    self = xtlq_makeLayout("aa bb cc dd", 60, (int)XTextOptionWrap_WordWrap);
    XQ_REQUIRE(self != NULL);
    XTextOption_init(&opt);
    XTextOption_setAlignment(&opt, XTLQ_ALIGN_JUSTIFY);
    XTextLayout_setTextOption(self, &opt);
    XQ_EQ_I64(XTextLayout_lineCount(self), 2);
    xtlq_expectRow(self, 0, 0, 6, 42, "justify-L0-natural");
    xtlq_expectRow(self, 1, 6, 11, 40, "justify-L1-natural");
    XQ_REQUIRE(XTextLayout_lineAt(self, 0, &row));
    XQ_EQ_I64(row.m_x, 0); /* Justify 行起笔恒左（分摊在列 x 上）。 */
    XQ_CHECK_MSG(XTextLayout_colToX(self, 0, 3) == 40,
                 "justify col3 实际 %d（语义 18+4+18）",
                 XTextLayout_colToX(self, 0, 3));
    XQ_CHECK_MSG(XTextLayout_colToX(self, 0, 5) == 60,
                 "justify 行尾实际 %d（撑满可用宽）",
                 XTextLayout_colToX(self, 0, 5));
    XQ_CHECK_MSG(XTextLayout_colToX(self, 1, 5) == 40,
                 "justify 末行不拉伸 实际 %d",
                 XTextLayout_colToX(self, 1, 5));
    /* 分摊后 xToCol 逆推（空隙加分在空格字胞之后）。 */
    XQ_EQ_I64(XTextLayout_xToCol(self, 0, 40), 3);
    XQ_EQ_I64(XTextLayout_xToCol(self, 0, 50), 4);
    /* Right|Justify 组合位按 Right 落位（Qt :2443 恰等比较）：
       行 x = 右对齐偏移，分摊不参与。 */
    XTextOption_init(&opt);
    XTextOption_setAlignment(&opt, XTLQ_ALIGN_RIGHT | XTLQ_ALIGN_JUSTIFY);
    XTextLayout_setTextOption(self, &opt);
    XQ_REQUIRE(XTextLayout_lineAt(self, 0, &row));
    XQ_EQ_I64(row.m_x, 60 - 42);
    XQ_CHECK_MSG(XTextLayout_colToX(self, 0, 3) == 18 + 4 + (60 - 42),
                 "Right|Justify 组合位不分摊 实际 %d",
                 XTextLayout_colToX(self, 0, 3));
    XClassDelete((XClass*)self);
}

/* ==================== 生命周期与输入面 ==================== */

XQ_TEST(XTextLayout, lifecycleAndInputs)
{
    XTextLayout* self = XTextLayout_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    XTextOption opt;
    XString* s;
    XQ_REQUIRE(self != NULL);
    /* 缺省：空文本 1 行、宽 0 不换行、选项缺省（QTextOption() 口径）。 */
    XQ_EQ_I64(XTextLayout_lineCount(self), 1);
    XQ_EQ_I64(XTextLayout_lineWidth(self), 0);
    XTextLayout_textOption(self, &opt);
    XQ_EQ_I64(opt.m_wrapMode, (int)XTextOptionWrap_WordWrap);
    XQ_CHECK(XTextOption_tabStopDistance(&opt) == -1.0);
    XQ_CHECK(!XTextOption_useDesignMetrics(&opt));
    /* setText_utf8(NULL) = 清空。 */
    XTextLayout_setText_utf8(self, "abc", -1);
    XQ_EQ_I64(XTextLayout_textLength(self), 3);
    XTextLayout_setText_utf8(self, NULL, -1);
    XQ_EQ_I64(XTextLayout_textLength(self), 0);
    XQ_EQ_I64(XTextLayout_lineCount(self), 1);
    /* len>=0 截断语义。 */
    XTextLayout_setText_utf8(self, "abcdef", 3);
    XQ_EQ_I64(XTextLayout_textLength(self), 3);
    XQ_EQ_STR(XTextLayout_text_const(self), "abc");
    /* setLineWidth 同值不标脏（getter 回读）。 */
    XTextLayout_setLineWidth(self, 50);
    XQ_EQ_I64(XTextLayout_lineWidth(self), 50);
    /* setFont(NULL) 回落缺省字体（XTextControl_setFont 同口径）。 */
    XTextLayout_setFont(self, NULL);
    XQ_EQ_I64(XTextLayout_lineCount(self), 1);
    /* XString 重载（内码缓冲 → toUtf8，跨码面一致）。 */
    s = XString_create();
    XQ_REQUIRE(s != NULL);
    XString_assign_utf8(s, "你好");
    XTextLayout_setText(self, s);
    XQ_EQ_I64(XTextLayout_textLength(self), 6);
    XClassDelete((XClass*)s);
    /* setLineWidth 负值 = 不换行。 */
    XTextLayout_setText_utf8(self, "one long line", -1);
    XTextLayout_setLineWidth(self, -5);
    XQ_EQ_I64(XTextLayout_lineCount(self), 1);
    XClassDelete((XClass*)self);
    /* NULL 对象全 API 守卫。 */
    XQ_EQ_I64(XTextLayout_lineCount(NULL), 0);
    XQ_EQ_I64(XTextLayout_textLength(NULL), 0);
    XQ_CHECK(XTextLayout_lineAt(NULL, 0, NULL) == false);
    XQ_EQ_I64(XTextLayout_lineForTextPosition(NULL, 0, NULL), -1);
    XQ_CHECK(XTextLayout_hitTest(NULL, 0, 0, NULL) == false);
    XQ_EQ_I64(XTextLayout_totalHeight(NULL), 0);
    XQ_EQ_I64(XTextLayout_colToX(NULL, 0, 0), 0);
    XQ_EQ_I64(XTextLayout_xToCol(NULL, 0, 0), 0);
    XTextLayout_setText_utf8(NULL, "x", -1);
    XTextLayout_setLineWidth(NULL, 10);
    XTextLayout_setFont(NULL, NULL);
    XTextLayout_setTextOption(NULL, NULL);
    XQ_CHECK(XTextLayout_ensureLayout(NULL) == false);
}

#endif /* XGUI_ON */
