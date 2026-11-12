/**
 * @file       XTextLayout.c
 * @brief      XTextLayout 行级文本布局引擎实现（对标 Qt 6.8 QTextLayout
 *             + QTextEngine 的断行/制表/对齐/justify/hitTest 子集）。
 * @details    与同名头文件的公共 API 一一对应。行为对齐依据（Qt 6.8.3
 *             原文 + 本机 Qt 6.8.0 offscreen 探针，探针报告
 *             /tmp/xtextlayout_probe/report.txt，关键数值已抄入
 *             XTextLayoutAlignTest.c 断言注释）：
 *             - 断行主循环：qtextlayout.cpp:1821-2104
 *               QTextLine::layout_helper（tmpData=词 / spaceData=行尾
 *               空白串的两缓冲记账；词原子提交；空行整词吸收）；
 *             - 超长不可断词 WordWrap 溢出不硬断：qtextlayout.cpp
 *               :1600-1606 setLineWidth 文档 + :2105-2110 "no break
 *               available in line, adding temp"（探针："ABCDEFGHIJKLMNOP"
 *               @40px 单行 natW=161.109）；
 *             - WrapAtWordBoundaryOrAnywhere 逐行降级：
 *               qtextlayout.cpp:2158-2167（溢出行以 WrapAnywhere 重排）；
 *             - 行尾空白：软断行不计宽（:2157-2193 trailingSpace 仅
 *               IncludeTrailingSpaces 计入；行区间经 trailingSpaces
 *               跳过，createLine :802）；段末行计宽（:1767-1795
 *               checkFullOtherwiseExtend 终点提交）；
 *             - 制表：qtextengine.cpp:3200-3301 calculateTabWidth
 *               （显式表：首个 position > x 的表位；Right/Center/
 *               Delimiter 减后续段宽取位、取负回 0；无表回落
 *               tabStopDistance，<=0 缺省 80；缺省档
 *               nextTabPos = floor(x/stop+1)*stop）；
 *             - 对齐：qtextengine.cpp:3396-3411 alignLine（Right:
 *               width-自然宽；HCenter: 一半；Left/Justify 落 0）；
 *             - justify：qtextengine.cpp:2432-2619（恰等
 *               AlignHorizontal_Mask :2443；段末行不分摊 :2449-2453；
 *               溢出行不分摊 :2562-2566；去行尾空白 :2459-2466；
 *               Space 类分摊 add=need/n :2592-2616）——本文件按
 *               XTextControl.c xtc_rowJustifyOf 的整像素等价式复刻
 *               （算法复用，非重写；XTextControl.c 归 Text 域静态
 *               符号，不可跨文件链接）；
 *             - cursorToX/xToCursor：qtextlayout.cpp:2916-3067 /
 *               :3078-3268（码点边界吸附、最近边界、制表胞半宽二分、
 *               软换行启用时钳到可用宽 :3060-3063）。
 * @author     XinYueC 团队
 */

#if XGUI_ON

#include "XTextLayout.h"

#include "XMemory.h"
#include "XString.h"
#include "XPainter.h"
#include "XTextUtf8.h"

#include <string.h>

/* ==================== 常量 ==================== */

#define XTL_DEFAULT_LINE_HEIGHT 16  /**< 度量失败回退行高（XTextControl 同款）。 */
#define XTL_DEFAULT_BASELINE 13     /**< 度量失败回退基线（XTextControl 同款）。 */
/* Qt calculateTabWidth 缺省制表距（qtextengine.cpp:3294-3295
 * "if (tab <= 0) tab = 80"）。 */
#define XTL_DEFAULT_TAB_STOP 80

/* 水平对齐位（XTDAlignment 同值空间 = Qt::AlignmentFlag 水平子集；
 * 枚举本体在 Widget/XTextDocument.h 的 XTEXTDOCUMENT_ON 段内，本类
 * 不依赖该开关 ⇒ 常量本地承载，值域单一）：
 * Left=0x01/Right=0x02/HCenter=0x04/Justify=0x08；水平掩码 0x000F
 * （Qt::AlignHorizontal_Mask 同值）。 */
#define XTL_ALIGN_LEFT 0x01
#define XTL_ALIGN_RIGHT 0x02
#define XTL_ALIGN_HCENTER 0x04
#define XTL_ALIGN_JUSTIFY 0x08
#define XTL_ALIGN_HORIZONTAL_MASK 0x000F

/** @brief 反初始化虚槽前置声明（class_init 槽位引用；XTextControl 同款静态）。 */
static void VXTextLayout_deinit(XTextLayout* self);

/* ==================== 码点分类（断行类；XTextControl.c 同族算法复用） ==== */

/** @brief 解码 s 处一个 UTF-8 码点；seq 输出字节长（XTextUtf8 同口径）。 */
static uint32_t xtl_decodeCp(const char* s, int remain, int* seq)
{
    unsigned char c0 = (unsigned char)s[0];
    int n = XTextUtf8_seqLen(s, remain);
    *seq = n > 0 ? n : 1;
    if (*seq >= 2 && (c0 & 0xE0) == 0xC0)
        return ((uint32_t)(c0 & 0x1F) << 6) | (uint32_t)(s[1] & 0x3F);
    if (*seq >= 3 && (c0 & 0xF0) == 0xE0)
        return ((uint32_t)(c0 & 0x0F) << 12) |
               ((uint32_t)(s[1] & 0x3F) << 6) | (uint32_t)(s[2] & 0x3F);
    if (*seq >= 4 && (c0 & 0xF8) == 0xF0)
        return ((uint32_t)(c0 & 0x07) << 18) |
               ((uint32_t)(s[1] & 0x3F) << 12) |
               ((uint32_t)(s[2] & 0x3F) << 6) | (uint32_t)(s[3] & 0x3F);
    return c0;
}

/** @brief CJK 码点判定（UAX#14 简化承载；XTextControl.c 同表复用）。 */
static bool xtl_isCjkCp(uint32_t cp)
{
    return (cp >= 0x1100 && cp <= 0x11FF) ||   /* Hangul Jamo */
           (cp >= 0x2E80 && cp <= 0x9FFF) ||   /* CJK 部首/假名/统一表意 */
           (cp >= 0xAC00 && cp <= 0xD7A3) ||   /* Hangul 音节 */
           (cp >= 0xF900 && cp <= 0xFAFF) ||   /* 兼容表意 */
           (cp >= 0xFE30 && cp <= 0xFE4F) ||   /* CJK 兼容形式 */
           (cp >= 0xFF00 && cp <= 0xFF60) ||   /* 全角形式 */
           (cp >= 0xFFE0 && cp <= 0xFFE6) ||
           (cp >= 0x20000 && cp <= 0x3FFFD);   /* 扩展表意平面 */
}

/** @brief 码点断行类（对标 UAX#14 简化：空白/CJK/词/标点）。 */
typedef enum XtlCpClass
{
    XTL_CP_SPACE = 0, /**< ASCII 空白（含制表）。 */
    XTL_CP_CJK,       /**< CJK/全角（逐字可断）。 */
    XTL_CP_WORD,      /**< 词字符（字母数字下划线/其它非 CJK 文字）。 */
    XTL_CP_PUNCT      /**< 标点/符号。 */
} XtlCpClass;

/** @brief 码点断行类判定（'\t' 记空白类，advance 走制表口径）。 */
static int xtl_cpClass(uint32_t cp)
{
    if (cp == ' ' || cp == '\t' || cp == 0x0B || cp == 0x0C)
        return XTL_CP_SPACE;
    if (xtl_isCjkCp(cp)) return XTL_CP_CJK;
    if ((cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z') ||
        (cp >= '0' && cp <= '9') || cp == '_' || cp >= 0x80)
        return XTL_CP_WORD;
    return XTL_CP_PUNCT;
}

/** 行禁首字符（kinsoku：这些标点不落行首；XTextControl.c 同表复用）。 */
static const uint32_t xtl_kinsokuBefore[] = {
    0x3001, 0x3002, 0x3009, 0x300B, 0x300D, 0x300F, 0x3011, 0x3015,
    0x3017, 0x3019, 0x301B, 0xFF01, 0xFF09, 0xFF0C, 0xFF1A, 0xFF1B,
    0xFF1F, 0xFF5D, 0x2019, 0x201D, 0x2026, 0x2014
};
/** 行禁尾字符（开括号/引号不落行尾）。 */
static const uint32_t xtl_kinsokuAfter[] = {
    0x3008, 0x300A, 0x300C, 0x300E, 0x3010, 0x3014, 0xFF08, 0xFF5B,
    0x2018, 0x201C
};

/** @brief 禁则查表。 */
static bool xtl_inCpTable(const uint32_t* table, int count, uint32_t cp)
{
    int i;
    for (i = 0; i < count; ++i)
        if (table[i] == cp) return true;
    return false;
}

/** @brief 空白字节判定（UTF-8 多字节序列不含 0x09/0x0B/0x0C/0x20 字节，
 *         逐字节扫描安全；与 justify 空隙判定共用）。 */
static bool xtl_isSpaceByte(char c)
{
    return c == ' ' || c == '\t' || c == 0x0B || c == 0x0C;
}

/**
 * @brief      prev 与 cur 之间是否可断行（cur 落行首的断点判定）。
 * @details    WordWrap 口径（XTextControl.c xtc_canBreakBefore 同式复用）：
 *             行首不留空白；CJK 逐字可断；词界（空白之后）可断；
 *             CJK 禁则成对约束。WrapAnywhere 不走本表（任意码点边界
 *             皆可断，见 xtl_scanAnywhere）。
 */
static bool xtl_canBreakBefore(int mode, uint32_t prevCp, int prevClass,
                               uint32_t curCp, int curClass)
{
    if (curClass == XTL_CP_SPACE) return false; /* 行首不留空白。 */
    if (mode == (int)XTextOptionWrap_Anywhere) return true;
    if (xtl_inCpTable(xtl_kinsokuBefore,
                      (int)(sizeof(xtl_kinsokuBefore) / sizeof(uint32_t)),
                      curCp))
        return false;
    if (xtl_inCpTable(xtl_kinsokuAfter,
                      (int)(sizeof(xtl_kinsokuAfter) / sizeof(uint32_t)),
                      prevCp))
        return false;
    if (prevClass == XTL_CP_SPACE) return true;  /* 词界（空白之后）。 */
    if (curClass == XTL_CP_CJK) return true;     /* CJK 逐字可断。 */
    if (prevClass == XTL_CP_CJK) return true;    /* CJK 后接西文/标点可断。 */
    if (prevClass == XTL_CP_PUNCT) return true;  /* 逗号/分号等后可断。 */
    return false;                                /* 词内不可断。 */
}

/* ==================== 度量（框架 FT 链；禁自造字形缓存） ==================== */

/**
 * @brief      单码点步进（XPainter_textWidthRange 框架链；advance <0 钳 0）。
 * @details    内含 letterSpacing/wordSpacing 附加与步进缓存（XPainter
 *             内部），本类不自建缓存。
 */
static int xtl_glyphAdvance(const XFont* font, const char* text,
                            int off, int seq)
{
    int w = XPainter_textWidthRange(font, text, off, off + seq);
    return w > 0 ? w : 0;
}

/** @brief 段落终点（from 起首个 '\n' 位置，无则 len）。 */
static int xtl_paraEnd(const char* text, int len, int from)
{
    int p;
    for (p = from; p < len; ++p)
        if (text[p] == '\n') return p;
    return len;
}

/* ==================== 制表推进（Qt calculateTabWidth 子集） ==================== */

/**
 * @brief      计算单个 '\t' 的推进宽（qtextengine.cpp:3200-3301 子集）。
 * @details    ① 显式制表位表（XTextOption m_tabs/m_tabCount）：取首个
 *             position > x 的表位（:3238 严格大于）；LeftTab 直接推进
 *             到表位；Right/Center/Delimiter 先量 [tabOff+1, 段段宽]
 *             （Right/Center 到下一 '\t' 或段末 :3239-3250；Delimiter
 *             到分隔符码点后沿 :3251-3253 并减半分隔符步进 :3271-3272；
 *             Center 再折半 :3276-3277），表位减段宽取位、取负回 0
 *             （:3281-3283 "default to tab taking no space"）；表全用尽
 *             落 ②。② 缺省制表距 m_tabStopDistance（<=0 回 80，
 *             :3293-3295）；推进 = (floor(x/stop)+1)*stop - x
 *             （:3297-3298）。dpiScale 本引擎恒 1（px==px，无设备 DPI
 *             折算——对齐 Qt pdev==NULL 分支 :3204-3211）。
 * @param      option 文本选项（借用）。
 * @param      font 度量字体（借用；Right/Delimiter 段宽复算用）。
 * @param      text 布局文本（借用）。
 * @param      paraEnd 段落终点字节位（段内查表/量宽上界）。
 * @param      tabOff '\t' 字节位。
 * @param      x 当前行内笔位 px（'\t' 之前，含行尾空白串）。
 * @return     推进宽 px（>=0）。
 */
static int xtl_tabAdvance(const XTextOption* option, const XFont* font,
                          const char* text, int paraEnd, int tabOff, int x)
{
    int i;
    int sectionEnd;
    int length;
    double pos;
    int type;
    int tab;
    uint32_t delimCp;
    for (i = 0; i < option->m_tabCount; ++i) {
        if (option->m_tabs[i].position > (double)x) {
            /* 这就是我们要的表位（Qt :3238 严格大于）。 */
            pos = option->m_tabs[i].position;
            type = option->m_tabs[i].type;
            delimCp = option->m_tabs[i].delimiter;
            if (type == (int)XTextOptionTab_Left)
                return (int)pos > x ? (int)pos - x : 0;
            /* Right/Center/Delimiter：量 tab 之后的段宽。 */
            sectionEnd = paraEnd;
            if (type == (int)XTextOptionTab_Delimiter) {
                int p = tabOff + 1;
                while (p < paraEnd) {
                    int seq;
                    uint32_t cp = xtl_decodeCp(text + p, paraEnd - p, &seq);
                    if (cp == delimCp) {
                        sectionEnd = p + seq; /* 含分隔符（:3253 +1 语义）。 */
                        break;
                    }
                    p += seq;
                }
            } else {
                int p;
                for (p = tabOff + 1; p < paraEnd; ++p)
                    if (text[p] == '\t') {
                        sectionEnd = p; /* 到下一制表（:3244-3249）。 */
                        break;
                    }
            }
            length = 0;
            {
                int p = tabOff + 1;
                while (p < sectionEnd) {
                    int seq;
                    uint32_t cp =
                        xtl_decodeCp(text + p, sectionEnd - p, &seq);
                    if (cp != '\t')
                        length += xtl_glyphAdvance(font, text, p, seq);
                    p += seq;
                }
            }
            if (type == (int)XTextOptionTab_Delimiter && sectionEnd > tabOff + 1)
                length -= xtl_glyphAdvance(font, text, sectionEnd - 1, 1) / 2;
            if (type == (int)XTextOptionTab_Center) length /= 2;
            tab = (int)pos - length;
            if (tab < x) return 0; /* 取负回 0（Qt :3282-3283）。 */
            return tab - x;
        }
    }
    /* 缺省制表距档（Qt :3293-3298）。 */
    {
        int stop = (int)(option->m_tabStopDistance + 0.5);
        if (stop <= 0) stop = XTL_DEFAULT_TAB_STOP;
        if (x <= 0) return stop;
        return ((x / stop) + 1) * stop - x;
    }
}

/* ==================== 行表登记 ==================== */

/**
 * @brief      行条目入表（容量倍增扩容；高度/坐标在 finalize 统一回填）。
 * @return     成功 true；扩容失败 false（保持脏）。
 */
static bool xtl_pushRow(XTextLayout* self, int start, int end, int width)
{
    if (self->m_lineCount >= self->m_lineCap) {
        int newCap = self->m_lineCap > 0 ? self->m_lineCap * 2 : 8;
        XTextLayoutLine* grown = (XTextLayoutLine*)XRealloc_System(
            self->m_lines, (size_t)newCap * sizeof(XTextLayoutLine));
        if (!grown) return false;
        self->m_lines = grown;
        self->m_lineCap = newCap;
    }
    {
        XTextLayoutLine* row = &self->m_lines[self->m_lineCount];
        XMemset(row, 0, sizeof(*row));
        row->m_start = start;
        row->m_end = end;
        row->m_width = width;
        ++self->m_lineCount;
    }
    return true;
}

/** @brief 计一段自然宽（'\t' 按制表口径推进；段内无软断点判断）。 */
static int xtl_rangeWidth(const XTextLayout* self, const char* text,
                          int paraEnd, int start, int end)
{
    int p = start;
    int x = 0;
    while (p < end) {
        int seq;
        uint32_t cp = xtl_decodeCp(text + p, end - p, &seq);
        if (cp == '\t')
            x += xtl_tabAdvance(&self->m_option, &self->m_font, text,
                                paraEnd, p, x);
        else
            x += xtl_glyphAdvance(&self->m_font, text, p, seq);
        p += seq;
    }
    return x;
}

/* ==================== 原子贪心扫描（WordWrap / 模式 4 基扫描） ==================== */

/**
 * @brief      逐字贪心扫描一个段落区间（WrapAnywhere 口径：任意码点
 *             边界可断，Qt layout_helper breakany 路 :1841/:2020）。
 * @details    每码点独立判溢出；行首码点恒被接纳；空白码点不切行
 *             （行首不留空白——空白串累积为待定串，随下一非空白码点的
 *             溢出判定一起留在上一行的长度域，Qt createLine :802 跳
 *             trailingSpaces 同口径）；软断行尾空白串不计宽
 *             （IncludeTrailingSpaces 计入）。模式 4 降级也走本函数。
 * @param      isParaEnd 区间终点是否段落末（true：行尾空白串计宽——
 *             Qt 终点提交 :1767-1795；false（模式 4 降级子区间）：
 *             终点是行界，待定空白串留在上一行长度域不计宽）。
 * @return     成功 true；登记失败 false。
 */
static bool xtl_scanAnywhere(XTextLayout* self, int pStart, int pEnd,
                             bool isParaEnd)
{
    const char* text = self->m_text;
    const XFont* font = &self->m_font;
    int wrapW = self->m_lineWidth;
    bool includeFlag =
        (self->m_option.m_flags & XTextOptionFlag_IncludeTrailingSpaces) != 0;
    int off = pStart;
    int rowStart = pStart;
    int x = 0;
    int xSpaceRun = 0;
    while (off < pEnd) {
        uint32_t cp;
        int seq;
        int cls;
        int adv;
        cp = xtl_decodeCp(text + off, pEnd - off, &seq);
        cls = xtl_cpClass(cp);
        if (cp == '\t')
            adv = xtl_tabAdvance(&self->m_option, font, text, pEnd, off, x);
        else
            adv = xtl_glyphAdvance(font, text, off, seq);
        if (cls == XTL_CP_SPACE) {
            /* 空白不切行：累积待定串（行首不留空白）。 */
            x += adv;
            xSpaceRun += adv;
        } else if (wrapW > 0 && off > rowStart && x + adv > wrapW) {
            /* 软断：行尾待定空白串不计宽（IncludeTrailingSpaces 计入）。 */
            int cutW = x - xSpaceRun + (includeFlag ? xSpaceRun : 0);
            if (!xtl_pushRow(self, rowStart, off, cutW)) return false;
            rowStart = off;
            x = adv;
            xSpaceRun = 0;
        } else {
            x += adv;
            xSpaceRun = 0;
        }
        off += seq;
    }
    /* 终点行：段落末计行尾空白串宽；行界终点（降级子区间）不计。 */
    if (!isParaEnd && !includeFlag) x -= xSpaceRun;
    return xtl_pushRow(self, rowStart, pEnd, x);
}

/**
 * @brief      扫描完成一条候选行后的模式 4 降级判定（Qt
 *             qtextlayout.cpp:2158-2167）：行宽溢出 ⇒ 以 WrapAnywhere
 *             重排该行区间（替换为若干逐字子行）。Anywhere 模式本身
 *             不降级（width 判据只挂在模式 4 的基扫描产物上）。
 * @return     成功 true；登记失败 false。
 */
static bool xtl_maybeFallback(XTextLayout* self, int rowStart, int rowEnd,
                              int rowWidth, bool fromFallback)
{
    if (self->m_option.m_wrapMode !=
            (int)XTextOptionWrap_WordBoundaryOrAnywhere ||
        fromFallback || rowWidth <= self->m_lineWidth)
        return xtl_pushRow(self, rowStart, rowEnd, rowWidth);
    /* 溢出 ⇒ 逐字重排（Qt :2162-2164 setWrapMode(WrapAnywhere) 重跑）；
     * 子区间终点是行界而非段末（行尾空白串不计宽）。 */
    return xtl_scanAnywhere(self, rowStart, rowEnd, false);
}

/**
 * @brief      原子贪心扫描一个段落区间（对标 layout_helper 的 WordWrap
 *             主路；区间 [pStart, pEnd)）。
 * @details    **原子** = 相邻可断边界之间的最大码点串（Qt 的 tmpData
 *             在空白与 UAX#14 lineBreak 属性处结算，:2005-2014/2011；
 *             本引擎以 xtl_canBreakBefore 同表承载——CJK 逐字、Latin
 *             词、禁则成对）。记账同构 Qt LineBreakHelper
 *             （qtextlayout.cpp:1669-1701）：x=已提交行宽（含行尾空白
 *             串）；xSpaceRun=行尾待定空白串；atomX=当前原子宽（原子
 *             内不判溢出——Qt 词内簇只在词尾 sb_or_ws 处过
 *             checkFull，:1996-2084）。原子尾判溢出：
 *             行有词 ⇒ 在最近断点软断（行尾空白不计宽、字节域计入
 *             行长度侧，:2157-2193 + createLine :802 跳 trailingSpaces，
 *             断点后待定空白串不转新行）；行无词 ⇒ 整原子吸收
 *             （:2105-2110 "no break available in line, adding temp"
 *             ——超长不可断原子溢出不硬断）。
 * @return     成功 true；登记失败 false（保持脏）。
 */
static bool xtl_scanWord(XTextLayout* self, int pStart, int pEnd,
                         bool fromFallback)
{
    const char* text = self->m_text;
    const XFont* font = &self->m_font;
    int wrapW = self->m_lineWidth;
    int mode = (int)XTextOptionWrap_WordWrap; /* 基扫描断点口径恒 WordWrap
                                                   （模式 4 由降级管）。 */
    bool includeFlag =
        (self->m_option.m_flags & XTextOptionFlag_IncludeTrailingSpaces) != 0;
    int off = pStart;
    int rowStart = pStart;
    int x = 0;             /* 已提交行宽 + 待定空白串（当前原子不计入）。 */
    int xSpaceRun = 0;     /* 待定行尾空白串宽。 */
    int atomX = 0;         /* 当前原子宽（未提交）。 */
    int lastBreak = -1;    /* 最近断点（绝对字节位）。 */
    int lastBreakW = 0;    /* [rowStart, lastBreak) 宽（不含待定空白串）。 */
    int lastBreakSpaces = 0; /* 记录时待定空白串宽（IncludeTrailingSpaces）。 */
    bool rowHasWord = false; /* 本行已提交/吸收过原子。 */
    uint32_t prevCp = 0;
    int prevClass = -1;

    while (off < pEnd) {
        uint32_t cp;
        int seq;
        int cls;
        int adv;
        cp = xtl_decodeCp(text + off, pEnd - off, &seq);
        cls = xtl_cpClass(cp);
        if (cp == '\t')
            adv = xtl_tabAdvance(&self->m_option, font, text, pEnd, off, x);
        else
            adv = xtl_glyphAdvance(font, text, off, seq);
        if (prevClass >= 0 &&
            xtl_canBreakBefore(mode, prevCp, prevClass, cp, cls)) {
            /* 断点候选（原子首）：行尾待定空白串留给上一行。 */
            lastBreak = off;
            lastBreakW = x - xSpaceRun;
            lastBreakSpaces = xSpaceRun;
        }
        if (cls == XTL_CP_SPACE) {
            if (prevClass != XTL_CP_SPACE) xSpaceRun = 0; /* 新空白串。 */
            x += adv;
            xSpaceRun += adv;
            off += seq;
            prevCp = cp;
            prevClass = cls;
            continue;
        }
        /* 原子字符累宽；原子尾（下一码点可断/空白/段末）结算。 */
        atomX += adv;
        {
            int nextOff = off + seq;
            bool atomEnds = nextOff >= pEnd;
            if (!atomEnds) {
                int nseq;
                uint32_t ncp = xtl_decodeCp(text + nextOff, pEnd - nextOff,
                                            &nseq);
                int ncls = xtl_cpClass(ncp);
                atomEnds = ncls == XTL_CP_SPACE ||
                           xtl_canBreakBefore(mode, cp, cls, ncp, ncls);
            }
            if (!atomEnds) {
                off += seq;
                prevCp = cp;
                prevClass = cls;
                continue;
            }
            /* 原子尾结算（checkFullOtherwiseExtend 口径）。 */
            if (wrapW > 0 && x + atomX > wrapW && rowHasWord &&
                lastBreak > rowStart) {
                /* 软断：行 = [rowStart, lastBreak)，宽不含待定空白串；
                   断点后内容（含行尾空白串的字节域）随上一行的长度域，
                   当前原子转为新行的待定原子（Qt from += length +
                   trailingSpaces，:802）。 */
                int cutW = lastBreakW + (includeFlag ? lastBreakSpaces : 0);
                if (!xtl_maybeFallback(self, rowStart, lastBreak, cutW,
                                       fromFallback))
                    return false;
                rowStart = lastBreak;
                x = 0;
                xSpaceRun = 0;
                rowHasWord = false;
                lastBreak = -1;
                lastBreakW = 0;
                lastBreakSpaces = 0;
                /* prevCp/prevClass 保持（off 前字符未变）；atomX 保持
                   （待定原子落入下方重判）。 */
            }
            if (wrapW > 0 && x + atomX > wrapW && !rowHasWord) {
                /* 空行整原子吸收：超长不可断原子溢出不硬断（Qt
                   :2105-2110 "no break available in line"）。 */
                x += atomX;
                xSpaceRun = 0;
                rowHasWord = true;
            } else {
                /* 提交（待定空白串随原子折叠，Qt checkFull :1767-1795）。 */
                x += atomX;
                xSpaceRun = 0;
                rowHasWord = true;
            }
            atomX = 0;
        }
        off += seq;
        prevCp = cp;
        prevClass = cls;
    }
    /* 段末行：行尾空白计宽（Qt 终点提交 :1767-1795）。 */
    return xtl_maybeFallback(self, rowStart, pEnd, x, fromFallback);
}

/* ==================== justify 分摊（XtcRowJustify 整像素算法复用） ==== */

/**
 * @brief      可视行 justify 分摊上下文（XTextControl.c
 *             xtc_rowJustifyOf 同式复用；Qt qtextengine.cpp:2432-2619
 *             的整像素等价式）。
 * @details    水平位恰为 AlignJustify 才参与（:2443）；段末行不分摊
 *             （:2449-2453——下一行不存在或非同段即段末行）；行溢出不
 *             分摊（:2562-2566 need<0 早退）；可分摊空隙为词间空白
 *             （行尾空白不计 :2459-2466）；每空隙加分 = need/空隙数、
 *             余数归末尾空隙（:2605-2612 整像素等价式）。
 */
typedef struct XtlRowJustify
{
    bool active;     /**< 本行参与分摊。 */
    int  gapTotal;   /**< 可分摊空隙数（行尾空白除外）。 */
    int  bonus;      /**< 每空隙固定加分（px）。 */
    int  rem;        /**< 末 rem 个空隙各再 +1（余数）。 */
    int  justifyEnd; /**< 分摊止点（绝对字节位：去尾随空白后）。 */
} XtlRowJustify;

/** @brief 判行是否段末行（下一行不存在或行间有 '\n' 即段末）。 */
static bool xtl_rowIsParagraphLast(const XTextLayout* self, int rowIdx)
{
    const XTextLayoutLine* row;
    if (rowIdx < 0 || rowIdx >= self->m_lineCount) return true;
    row = &self->m_lines[rowIdx];
    if (rowIdx + 1 >= self->m_lineCount) return true;
    /* 软断行的下一行 start == 本行 end（同段连续）；跨段差 1（'\n'）。 */
    return self->m_lines[rowIdx + 1].m_start != row->m_end;
}

/** @brief 计算行的 justify 分摊上下文（无分摊时 active=false）。 */
static void xtl_rowJustifyOf(const XTextLayout* self, int rowIdx,
                             XtlRowJustify* jc)
{
    const XTextLayoutLine* row;
    int rowEnd;
    int p;
    int extra;
    XMemset(jc, 0, sizeof(*jc));
    if (!self || rowIdx < 0 || rowIdx >= self->m_lineCount) return;
    if ((self->m_option.m_alignment & XTL_ALIGN_HORIZONTAL_MASK) !=
        XTL_ALIGN_JUSTIFY)
        return; /* 水平位恰等才分摊（Qt :2443，组合位按其它位落）。 */
    if (xtl_rowIsParagraphLast(self, rowIdx)) return; /* 段末行不拉伸。 */
    row = &self->m_lines[rowIdx];
    if (self->m_lineWidth <= 0 || row->m_width >= self->m_lineWidth)
        return; /* 无可用宽/行溢出：不分摊（Qt :2562-2566）。 */
    rowEnd = row->m_end;
    /* 行尾空白不参与分摊（Qt :2459-2466 去尾随空白）。 */
    while (rowEnd > row->m_start && xtl_isSpaceByte(self->m_text[rowEnd - 1]))
        --rowEnd;
    jc->justifyEnd = rowEnd;
    for (p = row->m_start; p < rowEnd; ++p)
        if (xtl_isSpaceByte(self->m_text[p])) ++jc->gapTotal;
    if (jc->gapTotal <= 0) return; /* 无词间空隙：保持原宽。 */
    extra = self->m_lineWidth - row->m_width;
    jc->bonus = extra / jc->gapTotal;
    jc->rem = extra % jc->gapTotal;
    jc->active = true;
}

/** @brief 行内 [rowStart, col) 已越过的可分摊空隙数。 */
static int xtl_justifyGapsBefore(const XtlRowJustify* jc, const char* text,
                                 int rowStart, int col)
{
    int p;
    int k = 0;
    if (!jc->active) return 0;
    if (col > jc->justifyEnd) col = jc->justifyEnd;
    for (p = rowStart; p < col; ++p)
        if (xtl_isSpaceByte(text[p])) ++k;
    return k;
}

/**
 * @brief      行内列 col 的 justify 附加 x（前序空隙分摊量之和）。
 * @details    与 XTextControl.c xtc_justifyExtraAtCol 同式：空隙加分
 *             发生在该空格字胞之后（Qt 把 justification 加进空格字形
 *             advance，qtextlayout.cpp:2511-2519）。
 */
static int xtl_justifyExtraAtCol(const XtlRowJustify* jc, const char* text,
                                 int rowStart, int col)
{
    int k;
    int tail;
    if (!jc->active) return 0;
    k = xtl_justifyGapsBefore(jc, text, rowStart, col);
    tail = k - (jc->gapTotal - jc->rem);
    if (tail < 0) tail = 0;
    if (tail > jc->rem) tail = jc->rem;
    return jc->bonus * k + tail;
}

/* ==================== 对齐偏移（QTextEngine::alignLine 口径） ==================== */

/**
 * @brief      行起笔 x 偏移（qtextengine.cpp:3396-3411）：
 *             Right = 可用宽 - 行自然宽；HCenter = 一半；Left/Justify
 *             落 0（Justify 分摊在各列 x 上兑现）。可用宽 <=0（未启用
 *             软换行）时 Qt 以 QFIXED_MAX 行宽跳过对齐（:3400-3401），
 *             同口径落 0。溢出行不钳负（Qt 原样，Right+溢出行起笔为负
 *             ——与 Qt 逐位一致；消费端 colToX/xToCol 有 <0 钳口）。
 */
static int xtl_alignOffset(const XTextLayout* self, const XTextLayoutLine* row)
{
    int align;
    if (!self || !row || self->m_lineWidth <= 0) return 0;
    align = self->m_option.m_alignment;
    if (align & XTL_ALIGN_RIGHT)
        return self->m_lineWidth - row->m_width;
    if (align & XTL_ALIGN_HCENTER)
        return (self->m_lineWidth - row->m_width) / 2;
    return 0;
}

/* ==================== 行表重建 ==================== */

/** @brief 刷新字体行度量（setFont/init 与重建共用；XTextControl 同式）。 */
static void xtl_refreshMetrics(XTextLayout* self)
{
    int ascent = XPainter_textAscent(&self->m_font);
    int descent = XPainter_textDescent(&self->m_font);
    if (ascent <= 0) ascent = XTL_DEFAULT_BASELINE;
    if (descent <= 0) descent = XTL_DEFAULT_LINE_HEIGHT - XTL_DEFAULT_BASELINE;
    self->m_ascent = ascent;
    self->m_descent = descent;
    self->m_lineHeight = (ascent + descent) > 0 ? ascent + descent
                                                : XTL_DEFAULT_LINE_HEIGHT;
}

/** @brief 重建行表（脏标记置位时的唯一入口）。 */
static bool xtl_rebuild(XTextLayout* self)
{
    const char* text = self->m_text;
    int len = self->m_textLen;
    int mode = self->m_option.m_wrapMode;
    int wrapW = self->m_lineWidth;
    bool wrapActive =
        wrapW > 0 &&
        (mode == (int)XTextOptionWrap_WordWrap ||
         mode == (int)XTextOptionWrap_WordBoundaryOrAnywhere);
    bool anywhere =
        wrapW > 0 && mode == (int)XTextOptionWrap_Anywhere;
    int pStart = 0;
    int i;
    self->m_lineCount = 0;
    /* 空文本：单空行（Qt 探针 lineCount=1、行高取字体度量同款）。 */
    do {
        int pEnd = xtl_paraEnd(text, len, pStart);
        if (wrapActive) {
            if (!xtl_scanWord(self, pStart, pEnd, false)) return false;
        } else if (anywhere) {
            if (!xtl_scanAnywhere(self, pStart, pEnd, true)) return false;
        } else {
            /* NoWrap/ManualWrap：段落单行，宽含全部内容（含行尾空白）。 */
            int w = xtl_rangeWidth(self, text, pEnd, pStart, pEnd);
            if (!xtl_pushRow(self, pStart, pEnd, w)) return false;
        }
        if (pEnd >= len) break;
        pStart = pEnd + 1; /* 跳过 '\n'（该字节不属于任何行）。 */
    } while (pStart <= len);
    /* 终稿：等高行坐标 + 对齐偏移（无内联对象，行高恒字体行高）。 */
    for (i = 0; i < self->m_lineCount; ++i) {
        XTextLayoutLine* row = &self->m_lines[i];
        row->m_index = i;
        row->m_ascent = self->m_ascent;
        row->m_descent = self->m_descent;
        row->m_height = self->m_lineHeight;
        row->m_baseline = self->m_ascent;
        row->m_y = i * self->m_lineHeight;
        row->m_x = xtl_alignOffset(self, row);
    }
    return true;
}

/* ==================== 生命周期 ==================== */

XVtable* XTextLayout_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XTextLayout)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXTextLayout_deinit);
    return XVTABLE_DEFAULT;
}

void XTextLayout_init(XTextLayout* self)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XObject_init((XObject*)self);
    XClassSetVtable(self, XTextLayout);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    XFont_init(&self->m_font);
    XTextOption_init(&self->m_option);
    self->m_lineWidth = 0; /* 缺省不软换行。 */
    xtl_refreshMetrics(self);
    self->m_dirty = true; /* 空文本也建 1 空行（ensureLayout 惰性）。 */
}

XTextLayout* XTextLayout_create_ex(XMemoryType memory)
{
    XTextLayout* self = (XTextLayout*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XTextLayout_init(self);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

static void VXTextLayout_deinit(XTextLayout* self)
{
    if (!self) return;
    if (self->m_text) XFree_System(self->m_text);
    self->m_text = NULL;
    self->m_textLen = 0;
    self->m_textCap = 0;
    if (self->m_lines) XFree_System(self->m_lines);
    self->m_lines = NULL;
    self->m_lineCount = 0;
    self->m_lineCap = 0;
    self->m_dirty = false;
    XClassDeinit((XClass*)&self->m_font);
    XClass_Deinit_Parent(XObject, (XObject*)self);
}

/* ==================== 输入面 ==================== */

/** @brief 公共落文本入口（拷贝 + 失效布局）。 */
static void xtl_setTextBytes(XTextLayout* self, const char* utf8, int len)
{
    if (!self) return;
    if (len < 0) len = utf8 ? (int)strlen(utf8) : 0;
    if (len < 0) len = 0;
    if (len + 1 > self->m_textCap) {
        int newCap = len + 1 > 32 ? len + 1 : 32;
        char* grown = (char*)XRealloc_System(self->m_text, (size_t)newCap);
        if (!grown) return; /* 分配失败保持旧文本（脏不变）。 */
        self->m_text = grown;
        self->m_textCap = newCap;
    }
    if (len > 0 && utf8) XMemcpy(self->m_text, utf8, (size_t)len);
    self->m_text[len] = '\0';
    self->m_textLen = len;
    self->m_dirty = true;
}

void XTextLayout_setText(XTextLayout* self, const XString* text)
{
    if (!self) return;
    if (!text) {
        xtl_setTextBytes(self, NULL, 0);
        return;
    }
    /* XString 是内码缓冲：取 UTF-8 一律 toUtf8（AGENTS.md 铁律）。 */
    xtl_setTextBytes(self, XString_toUtf8(text),
                     (int)XString_toUtf8_length(text));
}

void XTextLayout_setText_utf8(XTextLayout* self, const char* utf8, int len)
{
    xtl_setTextBytes(self, utf8, len);
}

const char* XTextLayout_text_const(const XTextLayout* self)
{
    return self ? self->m_text : NULL;
}

int XTextLayout_textLength(const XTextLayout* self)
{
    return self ? self->m_textLen : 0;
}

void XTextLayout_setFont(XTextLayout* self, const XFont* font)
{
    XFont f;
    if (!self) return;
    if (font) {
        XMemcpy(&f, font, sizeof(XFont));
    } else {
        XFont_init(&f);
    }
    /* 深拷贝（XTextControl_setFont 同款：f 的家族串归调用方字体对象，
       XClassCopy 为 self 深拷贝新串，调用方稍后自行 deinit 其副本）。 */
    XClassDeinit((XClass*)&self->m_font);
    XFont_init(&self->m_font);
    XClassCopy(&self->m_font, &f);
    xtl_refreshMetrics(self);
    self->m_dirty = true;
}

void XTextLayout_font(const XTextLayout* self, XFont* out)
{
    if (!out) return;
    if (!self) {
        XFont_init(out);
        return;
    }
    XFont_init(out);
    XClassCopy(out, (XClass*)&self->m_font);
}

void XTextLayout_setTextOption(XTextLayout* self, const XTextOption* option)
{
    if (!self) return;
    if (option) {
        self->m_option = *option;
    } else {
        XTextOption_init(&self->m_option);
    }
    self->m_dirty = true;
}

void XTextLayout_textOption(const XTextLayout* self, XTextOption* out)
{
    if (!out) return;
    if (!self) {
        XTextOption_init(out);
        return;
    }
    *out = self->m_option;
}

void XTextLayout_setLineWidth(XTextLayout* self, int width)
{
    if (!self) return;
    if (self->m_lineWidth == width) return;
    self->m_lineWidth = width;
    self->m_dirty = true;
}

int XTextLayout_lineWidth(const XTextLayout* self)
{
    return self ? self->m_lineWidth : 0;
}

/* ==================== 行级结果 ==================== */

bool XTextLayout_ensureLayout(XTextLayout* self)
{
    if (!self || !self->m_dirty) return self != NULL;
    if (!xtl_rebuild(self)) return false; /* 失败保持脏（惰性重试）。 */
    self->m_dirty = false;
    return true;
}

int XTextLayout_lineCount(XTextLayout* self)
{
    if (!self) return 0;
    (void)XTextLayout_ensureLayout(self);
    return self->m_lineCount;
}

bool XTextLayout_lineAt(XTextLayout* self, int index, XTextLayoutLine* out)
{
    if (!self) return false;
    (void)XTextLayout_ensureLayout(self);
    if (index < 0 || index >= self->m_lineCount) return false;
    if (out) *out = self->m_lines[index];
    return true;
}

int XTextLayout_lineForTextPosition(XTextLayout* self, int pos,
                                    XTextLayoutLine* out)
{
    int lo;
    int hi;
    if (!self) return -1;
    (void)XTextLayout_ensureLayout(self);
    if (self->m_lineCount <= 0) return -1;
    if (pos < 0) pos = 0;
    if (pos > self->m_textLen) pos = self->m_textLen;
    /* 找最后一个 start <= pos 的行：'\n' 字节位 == 行尾 == 行尾光标位，
       归属前一行（二分行表按 start 单调）。 */
    lo = 0;
    hi = self->m_lineCount - 1;
    while (lo < hi) {
        int mid = lo + (hi - lo + 1) / 2;
        if (self->m_lines[mid].m_start <= pos) lo = mid;
        else hi = mid - 1;
    }
    if (out) *out = self->m_lines[lo];
    return lo;
}

void XTextLayout_positionToLineCol(XTextLayout* self, int pos, int* line,
                                   int* col)
{
    int idx;
    const XTextLayoutLine* row;
    if (line) *line = 0;
    if (col) *col = 0;
    if (!self) return;
    idx = XTextLayout_lineForTextPosition(self, pos, NULL);
    if (idx < 0) return;
    row = &self->m_lines[idx];
    if (line) *line = idx;
    if (col) {
        int c = pos - row->m_start;
        if (c < 0) c = 0;
        if (c > row->m_end - row->m_start) c = row->m_end - row->m_start;
        *col = c;
    }
}

int XTextLayout_lineColToPosition(XTextLayout* self, int line, int col)
{
    const XTextLayoutLine* row;
    if (!self) return 0;
    (void)XTextLayout_ensureLayout(self);
    if (line < 0) line = 0;
    if (line >= self->m_lineCount) line = self->m_lineCount - 1;
    if (line < 0) return 0;
    row = &self->m_lines[line];
    if (col < 0) col = 0;
    if (col > row->m_end - row->m_start) col = row->m_end - row->m_start;
    return row->m_start + col;
}

/**
 * @brief      行内笔位推进（colToX 与 xToCol 边界表共用的唯一口径）：
 *             每码点自然步进（'\t' 走制表口径），justify 行的空隙在该
 *             空格字胞之后加分（与绘制推进同源）。
 * @param      outGapIdx 逐空隙序号（justify 加分取第 gapIdx 个分摊量）；
 *             可为 NULL（不分摊场景）。
 */
static int xtl_advanceAt(const XTextLayout* self, const XtlRowJustify* jc,
                         const char* text, int paraEnd, int off, int seq,
                         uint32_t cp, int pen, int* outGapIdx)
{
    int adv;
    if (cp == '\t')
        adv = xtl_tabAdvance(&self->m_option, &self->m_font, text, paraEnd,
                             off, pen);
    else
        adv = xtl_glyphAdvance(&self->m_font, text, off, seq);
    if (outGapIdx && jc && jc->active && off < jc->justifyEnd &&
        xtl_isSpaceByte(text[off])) {
        int amount = jc->bonus +
                     (*outGapIdx >= jc->gapTotal - jc->rem ? 1 : 0);
        adv += amount;
        ++*outGapIdx;
    }
    return adv;
}

/** @brief 段内字节位是否码点首字节（向后吸附用）。 */
static bool xtl_isCpStart(unsigned char c)
{
    return (c & 0xC0) != 0x80;
}

int XTextLayout_colToX(XTextLayout* self, int line, int col)
{
    const XTextLayoutLine* row;
    const char* text;
    XtlRowJustify jc;
    int paraEnd;
    int gapIdx = 0;
    int p;
    int abs;
    int result;
    if (!self) return 0;
    (void)XTextLayout_ensureLayout(self);
    if (line < 0 || line >= self->m_lineCount) return 0;
    row = &self->m_lines[line];
    text = self->m_text;
    if (col < 0) col = 0;
    if (col > row->m_end - row->m_start) col = row->m_end - row->m_start;
    abs = row->m_start + col;
    /* 向后吸附到码点边界（Qt graphemeBoundary 吸附 :2937 的 UTF-8 版）。 */
    while (abs < row->m_end && !xtl_isCpStart((unsigned char)text[abs]))
        ++abs;
    xtl_rowJustifyOf(self, line, &jc);
    paraEnd = xtl_paraEnd(text, self->m_textLen, row->m_start);
    p = row->m_start;
    result = 0;
    while (p < abs) {
        int seq;
        uint32_t cp = xtl_decodeCp(text + p, row->m_end - p, &seq);
        result += xtl_advanceAt(self, &jc, text, paraEnd, p, seq, cp,
                                result, &gapIdx);
        p += seq;
    }
    result += row->m_x;
    /* 软换行启用时钳到可用宽（Qt :3060-3063；NoWrap 不钳）。 */
    if (self->m_lineWidth > 0 &&
        self->m_option.m_wrapMode != (int)XTextOptionWrap_NoWrap &&
        result > self->m_lineWidth)
        result = self->m_lineWidth;
    if (result < 0) result = 0; /* Qt :3062-3063 同款下钳。 */
    return result;
}

int XTextLayout_xToCol(XTextLayout* self, int line, int x)
{
    const XTextLayoutLine* row;
    const char* text;
    XtlRowJustify jc;
    int paraEnd;
    int gapIdx = 0;
    int p;
    int pen;
    int bestCol;
    int bestDist;
    if (!self) return 0;
    (void)XTextLayout_ensureLayout(self);
    if (line < 0 || line >= self->m_lineCount) return 0;
    row = &self->m_lines[line];
    if (row->m_end <= row->m_start) return 0; /* 空行恒列 0。 */
    text = self->m_text;
    xtl_rowJustifyOf(self, line, &jc);
    paraEnd = xtl_paraEnd(text, self->m_textLen, row->m_start);
    x -= row->m_x; /* 扣对齐偏移（Qt xToCursor :3100-3101）。 */
    if (x <= 0) return 0; /* 左缘外：列 0（Qt :3111-3115）。 */
    /* 最近码点边界（Qt CursorIsPosition :3197-3258 的 LTR 整数版；
       制表胞只有前后沿两个边界，最近边界 == 半宽二分 :3157-3165）。 */
    p = row->m_start;
    pen = 0;
    bestCol = 0;
    bestDist = x > 0 ? x : 0; /* |x - 0| */
    while (p < row->m_end) {
        int seq;
        uint32_t cp = xtl_decodeCp(text + p, row->m_end - p, &seq);
        int dist = x - pen;
        if (dist < 0) dist = -dist;
        if (dist < bestDist) {
            bestDist = dist;
            bestCol = p - row->m_start;
        }
        pen += xtl_advanceAt(self, &jc, text, paraEnd, p, seq, cp, pen,
                             &gapIdx);
        p += seq;
    }
    /* 行尾边界（Qt :3265-3268 右缘外 → 行长）。 */
    {
        int dist = x - pen;
        if (dist < 0) dist = -dist;
        if (dist < bestDist) bestCol = row->m_end - row->m_start;
    }
    return bestCol;
}

bool XTextLayout_hitTest(XTextLayout* self, int x, int y, int* outPos)
{
    int line;
    int col;
    int total;
    if (!self || !outPos) return false;
    (void)XTextLayout_ensureLayout(self);
    total = self->m_lineCount * self->m_lineHeight;
    line = self->m_lineHeight > 0 ? y / self->m_lineHeight : 0;
    if (y < 0) line = 0;
    if (y >= total) line = self->m_lineCount - 1;
    if (line < 0) line = 0;
    if (line >= self->m_lineCount) line = self->m_lineCount - 1;
    col = XTextLayout_xToCol(self, line, x);
    *outPos = self->m_lines[line].m_start + col;
    return true;
}

int XTextLayout_totalHeight(XTextLayout* self)
{
    if (!self) return 0;
    (void)XTextLayout_ensureLayout(self);
    return self->m_lineCount * self->m_lineHeight;
}

#endif /* XGUI_ON */
