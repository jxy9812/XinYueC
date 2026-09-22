/**
 * @file       XTextEdit.c
 * @brief      富文本编辑控件实现（对标 Qt 6.8 QTextEdit 核心公共 API）。
 * @details    与同名头文件的公共 API 一一对应；富文本渲染子集经
 *             xte_walkRich 逐块几何遍历（绘制/命中/滚动范围共用），
 *             只读预览态由壳 paintEvent 承载；内部实现细节见
 *             头文件 @note 与函数级 Doxygen 注释。
 * @author     XinYueC 团队
 */

#include "XTextEdit.h"
#include "XStringUtils.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XVarList.h"
#include "XPainter.h"
#include "XImage.h"
#include "XTextDocument.h"
#include "XGuiConfig.h"
#include "XCursor.h"
#if XINPUTMETHOD_ON
#include "XVariant.h"
#endif /* XINPUTMETHOD_ON */

#include "XAlgorithm.h"
#include "XWidget_Protected.h"
#include <stdio.h>

#if XWIDGET_ON && XABSTRACTSCROLLAREA_ON && XPLAINTEXTEDIT_ON && XTEXTEDIT_ON

/* ==================== 富文本渲染子集（只读预览，逐块格式切换绘制） ==== */

/** @brief 预览渲染左留白（与内嵌编辑器 XPE_TEXT_LEFT 同口径）。 */
#define XTE_RICH_LEFT 2

/** @brief 链接文字颜色（对标 Qt palette Link 链接蓝的常量近似；预览
 *         渲染无调色板注入通道）。 */
#define XTE_LINK_COLOR 0xFF0563C1u

/** @brief 悬停链接高亮底色（不透明浅蓝；对标 QLabel 悬停反馈的常量
 *         近似，避免依赖位图后端的半透明混合语义）。 */
#define XTE_LINK_HOVER_BG 0xFFD6E9FFu

/** @brief 链接下划线相对基线的偏移（像素）。 */
#define XTE_LINK_UNDERLINE_OFFSET 2

/** @brief 列表项每级缩进（像素；对标 Qt 列表缩进的常量近似）。 */
#define XTE_LIST_INDENT 24

/** @brief 上下标字号千分比（对标 QTextCharFormat verticalAlignment
 *         SubScript/SuperScript 的缩小呈现，约 2/3 字号）。 */
#define XTE_SUPSUB_PERMILLE 620

/** @brief 点尺寸转像素尺寸（与 XPainter 位图缩放推导同式：
 *         px = pt * 96 / 72，对标 Qt 高通 96dpi 换算）。 */
static int xte_pointToPixel(double pointSize)
{
    if (pointSize <= 0.0) return 0;
    return (int)(pointSize * (96.0 / 72.0) + 0.5);
}

/** @brief 读取字体生效像素字号（pixelSize 未设时由点尺寸推导）。 */
static int xte_fontPixelSize(const XFont* font)
{
    int px = font ? XFont_pixelSize(font) : 0;
    if (px <= 0 && font) px = xte_pointToPixel(XFont_pointSizeF(font));
    return px > 0 ? px : 16;
}

/** @brief 标题字号千分比（对标 Qt 默认标题字号梯度 h1 最大逐级递减）。 */
static int xte_headingPermille(int level)
{
    switch (level) {
    case 1: return 2000;
    case 2: return 1500;
    case 3: return 1300;
    case 4: return 1000;
    case 5: return 900;
    case 6: return 800;
    default: return 1000;
    }
}

/** @brief 构造片段绘制字体（控件字体基础上套用像素字号；粗/斜/下划
 *         线为格式属性承载，位图字库无合成字形，视觉粗体由绘制层双
 *         描边近似、下划线/删除线由绘制层划线近似——子集边界）。 */
static XFont xte_makeFragFont(const XTextEdit* self, int pixelSize)
{
    XFont font = XWidget_font((const XWidget*)self);
    if (pixelSize > 0 && pixelSize != xte_fontPixelSize(&font))
        XFont_setPixelSize(&font, pixelSize);
    return font;
}

#if XTEXTDOCUMENT_ON
/** @brief 片段字体（斜体接通，§8.0g11）：片段 italic 属性映射到
 *         XFont_style（字形引擎合成倾斜渲染；位图字库无真斜体字形，
 *         painter 侧按 shear 合成——对齐 Qt 无斜体字形时的行为）。 */
static XFont xte_makeFragFontStyled(const XTextEdit* self,
                                    const XTDFragment* f, int pixelSize)
{
    XFont font = xte_makeFragFont(self, pixelSize);
    if (f && f->fmt.italic)
        XFont_setItalic(&font, true);
    return font;
}
/** @brief 片段生效像素字号（度量/绘制共用同一口径）：fontPointSize
 *         优先、缺省用块字号；上下标缩至 XTE_SUPSUB_PERMILLE（对标
 *         QTextCharFormat verticalAlignment 的缩小呈现），下限 4px。 */
static int xte_fragPixelSize(const XTextEdit* self, const XTDFragment* f,
                             int blockPx)
{
    int px = f->fmt.fontPointSize > 0
                 ? xte_pointToPixel(f->fmt.fontPointSize)
                 : blockPx;
    (void)self;
    if (f->fmt.superScript || f->fmt.subScript)
        px = (px * XTE_SUPSUB_PERMILLE) / 1000;
    return px >= 4 ? px : 4;
}
#endif /* XTEXTDOCUMENT_ON */

#if XTEXTDOCUMENT_ON
/** @brief 片段文本安全读取（空串 XString 的 toUtf8 可能为 NULL，本库
 *         惯例统一兜底为 ""，见 XTextDocument xtd_fragText 同款）。 */
static const char* xte_fragTextSafe(const XTDFragment* f)
{
    const char* t;
    if (!f || !f->text) return "";
    t = XString_toUtf8(f->text);
    return t ? t : "";
}

/** @brief 按 UTF-8 首字节返回该码点的字节长度（非法首字节按 1 步进）。
 * @param lead 首字节。
 * @return 1-4 字节长度。
 */
static int xte_utf8CharLen(unsigned char lead)
{
    if ((lead & 0x80u) == 0) return 1;
    if ((lead & 0xE0u) == 0xC0u) return 2;
    if ((lead & 0xF0u) == 0xE0u) return 3;
    if ((lead & 0xF8u) == 0xF0u) return 4;
    return 1;
}

/** @brief 解码 UTF-8 码点（配合 xte_utf8CharLen 使用；非法序列兜底
 *         返回首字节，不越界读——len 由首字节推导且调用方保证缓冲内）。 */
static uint32_t xte_utf8Decode(const char* s)
{
    unsigned char c0 = (unsigned char)s[0];
    int n = xte_utf8CharLen(c0);
    if (n == 1) return c0;
    if (n == 2)
        return ((uint32_t)(c0 & 0x1Fu) << 6) | (uint32_t)(s[1] & 0x3Fu);
    if (n == 3)
        return ((uint32_t)(c0 & 0x0Fu) << 12) |
               ((uint32_t)(s[1] & 0x3Fu) << 6) | (uint32_t)(s[2] & 0x3Fu);
    return ((uint32_t)(c0 & 0x07u) << 18) |
           ((uint32_t)(s[1] & 0x3Fu) << 12) |
           ((uint32_t)(s[2] & 0x3Fu) << 6) | (uint32_t)(s[3] & 0x3Fu);
}

/** @brief CJK 码点判定（断行"逐字可断"类的简化 UAX#14 承载，区间与
 *         XTextControl xtc_isCjkCp 同口径）。 */
static bool xte_isCjkCp(uint32_t cp)
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

/** @brief 空白码点判定（断行词分隔；\n 不经此路径——文档按块切分）。 */
static bool xte_isSpaceCp(uint32_t cp)
{
    return cp == ' ' || cp == '\t' || cp == 0x0B || cp == 0x0C;
}
#endif /* XTEXTDOCUMENT_ON */

#if XTEXTDOCUMENT_ON
/** @brief 预览渲染右留白（与左留白对称，构成视口软换行可用宽度）。 */
#define XTE_RICH_RIGHT 2

/** @brief 片段几何（与绘制/命中共用同一套逐可视行度量）。 */
typedef struct XTERichGeom
{
    int x;                  /**< 片段左缘（内容坐标，未含滚动）。 */
    int baselineY;          /**< 基线 Y（内容坐标）。 */
    int top;                /**< 所在可视行顶 Y（内容坐标）。 */
    int lineH;              /**< 所在可视行行高。 */
    int width;              /**< 片段实测宽度。 */
    int fragOff;            /**< 本段在片段文本内的字节偏移（软换行把
                                 片段拆到多个可视行时非 0）。 */
    int fragLen;            /**< 本段字节长（图片片段为 0）。 */
    int blockIndex;
    int fragIndex;
    const XTDFragment* frag; /**< 所属片段；标记单元为 NULL。 */
    const XFont* font;      /**< 片段字体（访问期间有效，调用方释放）。 */
    const char* markerText; /**< 列表项标记文本（§8.0g4）：非 NULL 为标记
                                 绘制单元——frag==NULL、fragLen=0，宽度为
                                 标记实测宽；命中回调按 frag==NULL 忽略。 */
    int markerBullet;      /**< 无序列表方块标记（§8.0g4）：位图字库无
                                 "·" 字形，无序标记改绘 3×3 实心方块
                                 （不依赖字体字形）；与 markerText 互斥。 */
} XTERichGeom;

/** @brief 片段访问回调（walkRich 逐块逐可视行逐片段调用；NULL 跳过
 *         访问仅量高）。 */
typedef void (*XTEFragFn)(XTextEdit* self, const XTERichGeom* geom, void* ud);

/** @brief 断行词元（块内布局中间产物）。 */
typedef struct XTEWord
{
    int fragIdx;  /**< 所属片段索引。 */
    int start;    /**< 词起（片段内 UTF-8 字节偏移）。 */
    int len;      /**< 词长（字节数；图片片段为 0 的原子承载）。 */
    int width;    /**< 词实测宽（图片片段为图宽）。 */
} XTEWord;

/** @brief 可视行内片段段（绘制/命中的最小几何单元）。 */
typedef struct XTESeg
{
    int fragIdx;  /**< 所属片段索引。 */
    int start;    /**< 段起（片段内 UTF-8 字节偏移）。 */
    int len;      /**< 段长（字节数）。 */
    int xOff;     /**< 行内相对 X（含词间空格宽累计）。 */
    int width;    /**< 段实测宽。 */
} XTESeg;

/** @brief 可视行（按视口宽软换行后的一行）。 */
typedef struct XTEVisLine
{
    int segFirst; /**< 段池起始索引。 */
    int segCount; /**< 段数（0=空行占位）。 */
    int width;    /**< 行宽（含词间空格）。 */
    int lineH;    /**< 行高（行内片段字体高最大值）。 */
    int ascent;   /**< 基线上升（行内最大值）。 */
} XTEVisLine;

/** @brief 布局池（单次 walkRich 调用内共享，XMemory 堆承载、用毕即
 *         释放；锚点几何不落盘的既有边界保持不变）。 */
typedef struct XTELayout
{
    XTEWord* words;    /**< 词元池（逐块复用）。 */
    int wordCount;
    int wordCap;
    XTESeg* segs;      /**< 段池（跨块累计）。 */
    int segCount;
    int segCap;
    XTEVisLine* lines; /**< 可视行池（跨块累计）。 */
    int lineCount;
    int lineCap;
    bool oom;          /**< 任一池扩容失败（降级为部分布局，不崩溃）。 */
} XTELayout;

/** @brief 词元池压入（容量不足倍增；失败置 oom 并丢弃该词元——降级
 *         为缺词布局，不崩溃）。 */
static bool xte_pushWord(XTELayout* lay, int fragIdx, int start, int end,
                         int width)
{
    XTEWord* arr;
    int cap = lay->wordCap;
    XTEWord* p;
    if (lay->wordCount >= cap) {
        int newCap = cap > 0 ? cap * 2 : 32;
        arr = (XTEWord*)XRealloc_System(lay->words,
            sizeof(XTEWord) * (size_t)newCap);
        if (!arr) {
            lay->oom = true;
            return false;
        }
        lay->words = arr;
        lay->wordCap = newCap;
    }
    p = &lay->words[lay->wordCount];
    p->fragIdx = fragIdx;
    p->start = start;
    p->len = end - start;
    p->width = width;
    lay->wordCount++;
    return true;
}

/** @brief 段池压入（同词元池的失败降级语义）。 */
static bool xte_pushSeg(XTELayout* lay, int fragIdx, int start, int len,
                        int xOff, int width)
{
    XTESeg* arr;
    XTESeg* p;
    if (lay->segCount >= lay->segCap) {
        int newCap = lay->segCap > 0 ? lay->segCap * 2 : 32;
        arr = (XTESeg*)XRealloc_System(lay->segs,
            sizeof(XTESeg) * (size_t)newCap);
        if (!arr) {
            lay->oom = true;
            return false;
        }
        lay->segs = arr;
        lay->segCap = newCap;
    }
    p = &lay->segs[lay->segCount];
    p->fragIdx = fragIdx;
    p->start = start;
    p->len = len;
    p->xOff = xOff;
    p->width = width;
    lay->segCount++;
    return true;
}

/** @brief 可视行池压入（同词元池的失败降级语义）。 */
static bool xte_pushLine(XTELayout* lay, int segFirst, int width,
                         int lineH, int ascent)
{
    XTEVisLine* arr;
    XTEVisLine* p;
    if (lay->lineCount >= lay->lineCap) {
        int newCap = lay->lineCap > 0 ? lay->lineCap * 2 : 32;
        arr = (XTEVisLine*)XRealloc_System(lay->lines,
            sizeof(XTEVisLine) * (size_t)newCap);
        if (!arr) {
            lay->oom = true;
            return false;
        }
        lay->lines = arr;
        lay->lineCap = newCap;
    }
    p = &lay->lines[lay->lineCount];
    p->segFirst = segFirst;
    p->segCount = lay->segCount - segFirst;
    p->width = width;
    p->lineH = lineH;
    p->ascent = ascent;
    lay->lineCount++;
    return true;
}

/** @brief 视口软换行可用宽度（左右留白各一；视口过窄返回 0=不换行，
 *         退化为旧行为的整块单行）。 */
static int xte_wrapAvailWidth(const XTextEdit* self, int widgetW)
{
    int avail = widgetW - XTE_RICH_LEFT - XTE_RICH_RIGHT;
    (void)self;
    return avail > 0 ? avail : 0;
}

/** @brief 块内词元化（断行分类：空白分隔词 + CJK 逐字成词，分类思想
 *         与 XTextControl xtc_cpClass 同源、按预览子集简化：词内不再
 *         细分标点）。逐片段单字实测累宽；图片片段承载为不可断原子词。
 *         行高/基线上升/词间空格宽按片段缓存（fragH/fragA/fragSpaceW，
 *         调用方以 XTD_MAX_FRAGMENTS_PER_BLOCK 长度分配）。 */
static void xte_buildWords(const XTextEdit* self, const XTDBlock* blk,
                           int blockPx, XTELayout* lay,
                           int* fragH, int* fragA, int* fragSpaceW)
{
    int j;
    for (j = 0; j < blk->fragmentCount; ++j) {
        const XTDFragment* f = &blk->fragments[j];
        const char* txt;
        int len;
        int off;
        int wordStart = -1;
        int wordW = 0;
        XFont font;
        fragH[j] = 0;
        fragA[j] = 0;
        fragSpaceW[j] = 0;
        if (f->image) {
            /* 图片片段：原尺寸原子词（底边贴基线），不可断。 */
            fragH[j] = XImage_height(f->image);
            fragA[j] = fragH[j];
            if (!xte_pushWord(lay, j, 0, 0, XImage_width(f->image))) return;
            continue;
        }
        txt = xte_fragTextSafe(f);
        len = (int)XStrlen(txt);
        if (len <= 0) continue;
        font = xte_makeFragFontStyled(self, f,
                                      xte_fragPixelSize(self, f, blockPx));
        fragH[j] = XPainter_textHeight(&font);
        fragA[j] = XPainter_textAscent(&font);
        fragSpaceW[j] = XPainter_textWidthRange(&font, " ", 0, 1);
        off = 0;
        while (off < len) {
            int n = xte_utf8CharLen((unsigned char)txt[off]);
            uint32_t cp = xte_utf8Decode(txt + off);
            if (xte_isSpaceCp(cp)) {
                /* 词分隔：收口未完词（行首不留空白的语义在填行侧）。 */
                if (wordStart >= 0) {
                    if (!xte_pushWord(lay, j, wordStart, off, wordW)) {
                        XFont_deinit_base(&font);
                        return;
                    }
                    wordStart = -1;
                    wordW = 0;
                }
            } else {
                int w = XPainter_textWidthRange(&font, txt, off, off + n);
                if (xte_isCjkCp(cp)) {
                    /* CJK 逐字可断：收口前词、单字成词。 */
                    if (wordStart >= 0) {
                        if (!xte_pushWord(lay, j, wordStart, off, wordW)) {
                            XFont_deinit_base(&font);
                            return;
                        }
                        wordStart = -1;
                        wordW = 0;
                    }
                    if (!xte_pushWord(lay, j, off, off + n, w)) {
                        XFont_deinit_base(&font);
                        return;
                    }
                } else if (wordStart < 0) {
                    wordStart = off;
                    wordW = w;
                } else {
                    wordW += w;
                }
            }
            off += n;
        }
        if (wordStart >= 0)
            xte_pushWord(lay, j, wordStart, len, wordW);
        XFont_deinit_base(&font);
    }
}

/** @brief 词元贪心填行（宽度优先：词元不可再断，超宽词元单独成行硬
 *         溢出由绘制裁剪——简化子集，不做词内硬断）。词间空格宽取后词
 *         所属片段字体；行首不留空白（行首词不计前导空格宽）。行高/
 *         上升取行内片段最大者。@param blockH/blockA 空行兜底度量。 */
static void xte_fillLines(const XTDBlock* blk, XTELayout* lay, int availW,
                          const int* fragH, const int* fragA,
                          const int* fragSpaceW, int blockH, int blockA)
{
    int w;
    int segFirst = lay->segCount;
    int curW = 0;
    int maxH = 0;
    int maxA = 0;
    bool started = false;
    for (w = 0; w < lay->wordCount; ++w) {
        const XTEWord* wd = &lay->words[w];
        int gap = started ? fragSpaceW[wd->fragIdx] : 0;
        int fh = fragH[wd->fragIdx];
        int fa = fragA[wd->fragIdx];
        if (started && availW > 0 && curW + gap + wd->width > availW) {
            /* 换行：当前可视行落池，本词起行首（前导空格不计）。 */
            if (!xte_pushLine(lay, segFirst, curW, maxH, maxA)) return;
            segFirst = lay->segCount;
            curW = 0;
            maxH = 0;
            maxA = 0;
            started = false;
            gap = 0;
        }
        if (!xte_pushSeg(lay, wd->fragIdx, wd->start, wd->len,
                         curW + gap, wd->width))
            return;
        curW += gap + wd->width;
        if (fh > maxH) maxH = fh;
        if (fa > maxA) maxA = fa;
        started = true;
    }
    if (started) {
        xte_pushLine(lay, segFirst, curW, maxH, maxA);
    } else if (lay->wordCount == 0) {
        /* 空块/纯空白块（trailing <br>/<p>）按块度量占一行。 */
        xte_pushLine(lay, segFirst, 0, blockH, blockA);
    }
    (void)blk;
}

/** @brief 富文本逐块遍历（渲染子集核心：逐块格式切换绘制，不做整篇
 *         富文本布局引擎）。
 * @details 每块两阶段：先词元化（空白分隔词 + CJK 逐字成词 + 图片原
 *          子词，逐片段缓存行高/上升/空格宽），再按视口可用宽贪心填
 *          可视行；逐可视行按块对齐定位起笔 X 逐段访问。绘制、anchorAt
 *          命中、滚动范围三者共用本遍历同一几何源（行数=换行后可视行
 *          数）。对齐（Left/Right/HCenter）按可视行宽计算；justify 降
 *          级为左对齐。简化边界：词内不做硬断（超宽词元整词溢出裁剪）、
 *          不做行内基线混排（同行片段共享行基线）、锚点几何不落盘。
 * @return 文档内容总高度（像素）。
 */
static int xte_walkRich(const XTextEdit* self, XTEFragFn fn, void* ud)
{
    const XTextDocument* doc;
    XFont base;
    int basePx;
    int baseLineH;
    int baseAscent;
    int widgetW;
    int availW;
    XTELayout lay;
    int y = 0;
    int i;
    int listCount[8];       /* 各缩进层有序序号（1-8 层 → 下标 0-7）。 */
    int k;
    for (k = 0; k < 8; ++k) listCount[k] = 0;
    if (!self) return 0;
    doc = self->m_textDoc;
    if (!doc || !doc->m_blocks || doc->m_blockCount <= 0) return 0;
    widgetW = XWidget_width((const XWidget*)self);
    availW = xte_wrapAvailWidth(self, widgetW);
    base = XWidget_font((const XWidget*)self);
    basePx = xte_fontPixelSize(&base);
    baseLineH = XPainter_textHeight(&base);
    baseAscent = XPainter_textAscent(&base);
    if (baseLineH < 4) baseLineH = 4;
    if (baseAscent <= 0 || baseAscent > baseLineH) baseAscent = baseLineH;
    XMemset(&lay, 0, sizeof(lay));
    for (i = 0; i < doc->m_blockCount; ++i) {
        const XTDBlock* blk = &doc->m_blocks[i];
        int blockPx = (basePx * xte_headingPermille(blk->headingLevel)) / 1000;
        int blockH = 0;
        int blockA = 0;
        int blockLineFirst;
        int lineLast;
        int j;
        int fragH[XTD_MAX_FRAGMENTS_PER_BLOCK];
        int fragA[XTD_MAX_FRAGMENTS_PER_BLOCK];
        int fragSpaceW[XTD_MAX_FRAGMENTS_PER_BLOCK];
        /* 列表项呈现（§8.0g4/g5）：内容盒按缩进级别左移；序号按层独立
         * 计数——li 所在层计数 +1 并清更深层（嵌套子表已结束）；非列表
         * 块清全部层；listFresh（同层新列表首项）所在层重起 1。 */
        int indent = (blk->isListItem && blk->indentLevel > 0)
                         ? blk->indentLevel * XTE_LIST_INDENT
                         : 0;
        int blkAvailW = availW > indent ? availW - indent : 0;
        if (blk->isListItem) {
            int lvl = blk->indentLevel;
            if (lvl < 1) lvl = 1;
            if (lvl > 8) lvl = 8;
            for (k = lvl; k < 8; ++k) listCount[k] = 0;
            if (blk->listFresh) listCount[lvl - 1] = 0;
            ++listCount[lvl - 1];
        } else {
            for (k = 0; k < 8; ++k) listCount[k] = 0;
        }
        if (blockPx < 4) blockPx = 4;
        /* 空行兜底度量：块字号量一行（无片段时），或片段字体最大值
           （纯空白片段时保持旧口径的高度不塌陷）。 */
        if (blk->fragmentCount == 0) {
            XFont font = xte_makeFragFont(self, blockPx);
            blockH = XPainter_textHeight(&font);
            blockA = XPainter_textAscent(&font);
            XFont_deinit_base(&font);
        } else {
            for (j = 0; j < blk->fragmentCount; ++j) {
                const XTDFragment* f = &blk->fragments[j];
                if (f->image) {
                    int ih = XImage_height(f->image);
                    if (ih > blockH) blockH = ih;
                    if (ih > blockA) blockA = ih;
                } else {
                    const char* txt = xte_fragTextSafe(f);
                    if (txt[0]) {
                        XFont font = xte_makeFragFontStyled(
                            self, f, xte_fragPixelSize(self, f, blockPx));
                        int h = XPainter_textHeight(&font);
                        int a = XPainter_textAscent(&font);
                        XFont_deinit_base(&font);
                        if (h > blockH) blockH = h;
                        if (a > blockA) blockA = a;
                    }
                }
            }
        }
        if (blockH <= 0) blockH = baseLineH;
        if (blockA <= 0 || blockA > blockH) blockA = baseAscent;
        /* 阶段一：词元化（失败即用已收词元降级布局）。 */
        xte_buildWords(self, blk, blockPx, &lay, fragH, fragA, fragSpaceW);
        /* 阶段二：贪心填可视行（列表项内容盒扣减缩进）。 */
        blockLineFirst = lay.lineCount;
        xte_fillLines(blk, &lay, blkAvailW, fragH, fragA, fragSpaceW,
                      blockH, blockA);
        if (lay.oom) break; /* 池扩容失败：保留已布局部分。 */
        /* 阶段三：逐可视行对齐定位并逐段访问。 */
        lineLast = lay.lineCount;
        for (j = blockLineFirst; j < lineLast; ++j) {
            const XTEVisLine* line = &lay.lines[j];
            int xLeft = XTE_RICH_LEFT + indent;
            int xStart = xLeft;
            int s;
            /* 块对齐按可视行宽：Left=0x01 / Right=0x02 / HCenter=0x04
               （数值与 Qt::Alignment 一致）；justify 降级左对齐。右对齐
               沿视口右缘（列表缩进只影响左缘——简化子集）；居中在块
               内容盒（扣缩进后）内居中。 */
            if (blk->alignment & 0x02)
                xStart = widgetW - XTE_RICH_RIGHT - line->width;
            else if (blk->alignment & 0x04)
                xStart = xLeft + (blkAvailW - line->width) / 2;
            if (xStart < 0) xStart = 0;
            if (fn) {
                if (blk->isListItem && j == blockLineFirst) {
                    /* 列表项标记（块首行发射一次）：有序 "N." 文本标记
                     * （按所在层计数）、无序 3×3 方块（位图字库无 "·"
                     * 字形）；右对齐挂在内容盒左缘前 4px；命中回调按
                     * frag==NULL 忽略。 */
                    char numBuf[16];
                    const char* mk = NULL;
                    XFont font;
                    int mkW;
                    int lvl = blk->indentLevel;
                    XTERichGeom geom;
                    if (lvl < 1) lvl = 1;
                    if (lvl > 8) lvl = 8;
                    if (blk->isOrdered) {
                        snprintf(numBuf, sizeof(numBuf), "%d.",
                                 listCount[lvl - 1]);
                        mk = numBuf;
                    }
                    font = xte_makeFragFont(self, blockPx);
                    mkW = mk ? XPainter_textWidthRange(
                                   &font, mk, 0, (int)XStrlen(mk))
                             : 3;
                    XMemset(&geom, 0, sizeof(geom));
                    if (!mk) geom.markerBullet = 1;
                    geom.markerText = mk;
                    geom.font = &font;
                    geom.blockIndex = i;
                    geom.x = xLeft - 4 - mkW;
                    if (geom.x < XTE_RICH_LEFT) geom.x = XTE_RICH_LEFT;
                    geom.baselineY = y + line->ascent;
                    geom.top = y;
                    geom.lineH = line->lineH;
                    geom.width = mkW;
                    fn((XTextEdit*)self, &geom, ud);
                    XFont_deinit_base(&font);
                }
                for (s = 0; s < line->segCount; ++s) {
                    const XTESeg* seg = &lay.segs[line->segFirst + s];
                    const XTDFragment* f = &blk->fragments[seg->fragIdx];
                    XTERichGeom geom;
                    XFont font = xte_makeFragFontStyled(
                        self, f, xte_fragPixelSize(self, f, blockPx));
                    geom.x = xStart + seg->xOff;
                    geom.baselineY = y + line->ascent;
                    geom.top = y;
                    geom.lineH = line->lineH;
                    geom.width = seg->width;
                    geom.fragOff = seg->start;
                    geom.fragLen = seg->len;
                    geom.blockIndex = i;
                    geom.fragIndex = seg->fragIdx;
                    geom.frag = f;
                    geom.font = &font;
                    geom.markerText = NULL;
                    geom.markerBullet = 0;
                    fn((XTextEdit*)self, &geom, ud);
                    XFont_deinit_base(&font);
                }
            }
            y += line->lineH > 0 ? line->lineH : 1;
        }
        lay.wordCount = 0; /* 词元池逐块复用（容量保留）。 */
    }
    /* 布局池用毕即释放（锚点几何不落盘边界不变）。 */
    if (lay.words) XFree_System(lay.words);
    if (lay.segs) XFree_System(lay.segs);
    if (lay.lines) XFree_System(lay.lines);
    XFont_deinit_base(&base);
    return y;
}

/** @brief 预览绘制上下文。 */
typedef struct XTEPaintCtx
{
    XPainter* painter;
    const char* hover; /**< 悬停链接 URL（NULL=无；用于高亮）。 */
} XTEPaintCtx;

/** @brief 单片段绘制：图片原尺寸/字色/伪粗体/链接下划线/删除线/悬停
 *         高亮。软换行下片段可能拆到多个可视行，只绘制 geom 标注的字
 *         节子区间（fragOff/fragLen），其余行由对应段的回调绘制。 */
static void xte_paintFrag(XTextEdit* self, const XTERichGeom* geom, void* ud)
{
    XTEPaintCtx* ctx = (XTEPaintCtx*)ud;
    const XTDFragment* f = geom->frag;
    const char* full = "";
    const char* href = NULL;
    char buf[512];
    char* heap = NULL;
    char* txt;
    bool isLink;
    bool hovered = false;
    uint32_t fg;
    int baseline = geom->baselineY;
    (void)self;
    if (!ctx || !ctx->painter || geom->width <= 0) return;
    if (geom->markerBullet) {
        /* 无序列表方块标记：3×3 实心方块，光学居中于基线上方
         * （位图字库无 "·" 字形的字体无关近似）。 */
        XRect b;
        b.x = geom->x;
        b.y = geom->baselineY - 4;
        b.width = 3;
        b.height = 3;
        XPainter_fillRect(ctx->painter, &b, 0xFF000000u);
        return;
    }
    if (geom->markerText) {
        /* 列表项标记单元（frag==NULL）：块基字体、正文前景色绘制
         * （对标 Qt 列表项标记呈现）。 */
        XPainter_setFont(ctx->painter, geom->font);
        XPainter_drawText(ctx->painter, geom->x, geom->baselineY,
                          geom->markerText, 0xFF000000u);
        return;
    }
    full = xte_fragTextSafe(f);
    href = (f->fmt.anchorHref) ? XString_toUtf8(f->fmt.anchorHref) : NULL;
    if (f->image) {
        /* 图片片段：原尺寸绘制，底边贴基线（Qt 行内图片基线语义）。
         * width/height 缩放属性不做——子集边界，见 insertImage 注释。 */
        XPainter_drawImage(ctx->painter, f->image, geom->x,
                           geom->baselineY - XImage_height(f->image));
        return;
    }
    /* 取本段字节子区间为 NUL 串（栈缓冲优先，超长降级堆分配）。 */
    if (geom->fragLen < (int)sizeof(buf)) {
        XMemcpy(buf, full + geom->fragOff, (size_t)geom->fragLen);
        buf[geom->fragLen] = '\0';
        txt = buf;
    } else {
        heap = (char*)XMalloc_System((size_t)geom->fragLen + 1);
        if (!heap) return;
        XMemcpy(heap, full + geom->fragOff, (size_t)geom->fragLen);
        heap[geom->fragLen] = '\0';
        txt = heap;
    }
    isLink = (href && href[0] && txt[0]);
    if (!txt[0]) {
        if (heap) XFree_System(heap);
        return;
    }
    hovered = isLink && ctx->hover && XStrcmp(href, ctx->hover) == 0;
    /* 片段背景色（§8.0g5，span style background-color）：文本前铺行带
     * 背景矩形（建议不透明色——位图后端半透明混合语义子集边界，
     * 与悬停高亮同实现）。 */
    if (f->fmt.bgColor != 0u) {
        XRect bg;
        bg.x = geom->x;
        bg.y = geom->top;
        bg.width = geom->width;
        bg.height = geom->lineH;
        XPainter_fillRect(ctx->painter, &bg, f->fmt.bgColor);
    }
    /* 悬停高亮：链接片段带底色（对标 QLabel 悬停反馈）。 */
    if (hovered) {
        XRect bg;
        bg.x = geom->x;
        bg.y = geom->top;
        bg.width = geom->width;
        bg.height = geom->lineH;
        XPainter_fillRect(ctx->painter, &bg, XTE_LINK_HOVER_BG);
    }
    fg = f->fmt.fgColor != 0 ? f->fmt.fgColor : 0xFF000000u;
    if (isLink) fg = XTE_LINK_COLOR;
    /* 上下标基线偏移（对标 QTextCharFormat verticalAlignment：上标抬升
     * 约行高 2/5、下标下沉约行高 1/5 并钳在行盒内；字号缩放已在布局
     * 侧 xte_fragPixelSize 统一）。 */
    if (f->fmt.superScript) {
        baseline -= geom->lineH * 2 / 5;
    } else if (f->fmt.subScript) {
        baseline += geom->lineH / 5;
        if (baseline > geom->top + geom->lineH - 1)
            baseline = geom->top + geom->lineH - 1;
    }
    XPainter_setFont(ctx->painter, geom->font);
    XPainter_setPen(ctx->painter, fg);
    XPainter_drawText(ctx->painter, geom->x, baseline, txt, fg);
    if (f->fmt.bold) {
        /* 伪粗体：同基线 +1px 双描边（位图字库无粗体字形，对标 Qt
           无合成字体时的加粗近似）。 */
        XPainter_drawText(ctx->painter, geom->x + 1, baseline, txt, fg);
    }
    if (f->fmt.underline || isLink) {
        /* 链接恒下划线（对标 Qt 富文本链接呈现）；下划线钳在行盒内。 */
        int uy = geom->baselineY + XTE_LINK_UNDERLINE_OFFSET;
        if (uy > geom->top + geom->lineH - 1)
            uy = geom->top + geom->lineH - 1;
        XPainter_drawLine(ctx->painter, geom->x, uy,
                          geom->x + geom->width, uy);
    }
    if (f->fmt.strikeOut) {
        int sy = geom->top + geom->lineH / 2;
        XPainter_drawLine(ctx->painter, geom->x, sy,
                          geom->x + geom->width, sy);
    }
    if (heap) XFree_System(heap);
}
#endif /* XTEXTDOCUMENT_ON */

/** @brief 预览态垂直滚动范围刷新（内容高度实测；视口=控件高度）。 */
static void xte_updatePreviewScroll(XTextEdit* self)
{
#if XTEXTDOCUMENT_ON
    XScrollBar* vsb;
    int contentH;
    int viewH;
    if (!self || !self->m_richPreview) return;
    contentH = xte_walkRich(self, NULL, NULL);
    vsb = XAbstractScrollArea_verticalScrollBar(
        (const XAbstractScrollArea*)self);
    if (!vsb) return;
    viewH = XWidget_height((XWidget*)self);
    XAbstractSlider_setRange((XAbstractSlider*)vsb, 0,
                             contentH > viewH ? contentH - viewH : 0);
    XScrollBar_setPageStep(vsb, viewH > 0 ? viewH : 1);
#endif
}

/** @brief 清理预览态锚点交互残留（悬停/按压/手型光标）。 */
static void xte_resetAnchorState(XTextEdit* self)
{
    if (!self) return;
    if (self->m_hoverAnchor) {
        XString_delete_base(self->m_hoverAnchor);
        self->m_hoverAnchor = NULL;
    }
    if (self->m_pressedAnchor) {
        XString_delete_base(self->m_pressedAnchor);
        self->m_pressedAnchor = NULL;
    }
    XWidget_unsetCursor((XWidget*)self);
}

/** @brief 进入只读富文本预览（隐藏内嵌编辑器，壳逐块绘制文档）。 */
static bool xte_enterPreview(XTextEdit* self)
{
#if XTEXTDOCUMENT_ON
    if (!self || !self->m_textDoc) return false;
    if (!self->m_richPreview) {
        self->m_richPreview = true;
        xte_resetAnchorState(self);
        if (self->m_editor) XWidget_hide((XWidget*)self->m_editor);
        xte_updatePreviewScroll(self);
        XWidget_update((XWidget*)self);
    }
    return true;
#else
    (void)self;
    return false;
#endif
}

/** @brief 退出预览回到纯文本编辑态（编辑类接口统一入口：编辑保持纯
 *         文本语义，所见即所存）。 */
static void xte_exitPreview(XTextEdit* self)
{
    if (!self || !self->m_richPreview) return;
    self->m_richPreview = false;
    xte_resetAnchorState(self);
    if (self->m_editor) XWidget_show((XWidget*)self->m_editor);
    XWidget_update((XWidget*)self);
}

/* ============ 预览态链接交互（对标 QLabel 既有链接实现模式） ============ */

XString* XTextEdit_anchorAt(const XTextEdit* self, const XPoint* pos);

/** @brief 链接信号参数释放回调：释放列表内拷贝的 XString（对标
 *         XLabel 链接信号的所有权惯例）。 */
static void xte_linkSignal_del(XVarList* list)
{
    XVarList_args_1(list, XString*, link);
    if (link) XString_delete_base((XClass*)link);
}

/** @brief 发射携带 XString* 堆拷贝的链接信号；无接收者时释放参数。 */
static void xte_emitLinkSignal(XTextEdit* self, size_t signal, const char* url)
{
    XString* copy;
    XVarList* args;
    if (!self || !((XObject*)self)->m_signalSlot) return;
    copy = XString_create_utf8(url ? url : "");
    if (!copy) return;
    args = XVarList_Create(XVar(XString*, copy));
    if (!args) {
        XString_delete_base((XClass*)copy);
        return;
    }
    XObject_emitSignal((XObject*)self, signal, args, xte_linkSignal_del,
                       NULL, XEVENT_PRIORITY_NORMAL);
}

/** @brief 鼠标按下：记录按压链接（对标 QLabel 按下记录 m_pressedLink）。 */
static void VX_textEdit_mousePressEvent(XWidget* self, XEvent* event)
{
    XTextEdit* te = (XTextEdit*)self;
    XMouseEvent* me;
    XString* anchor;
    const char* url;
    if (!te || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_PRESS)
        return;
    if (!te->m_richPreview) return; /* 编辑态：子控件编辑器命中处理。 */
    me = (XMouseEvent*)event;
    anchor = XTextEdit_anchorAt(te, &me->m_position);
    url = (anchor && XString_toUtf8(anchor)) ? XString_toUtf8(anchor) : "";
    if (te->m_pressedAnchor) {
        XString_delete_base(te->m_pressedAnchor);
        te->m_pressedAnchor = NULL;
    }
    if (url[0]) {
        te->m_pressedAnchor = XString_create_utf8(url);
        XEvent_accept(event);
    }
    if (anchor) XString_delete_base(anchor);
}

/** @brief 鼠标释放：按下与释放命中同一链接时发 linkActivated（对标
 *         QLabel 释放同链激活）。 */
static void VX_textEdit_mouseReleaseEvent(XWidget* self, XEvent* event)
{
    XTextEdit* te = (XTextEdit*)self;
    XMouseEvent* me;
    XString* pressed;
    XString* anchor;
    if (!te || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_RELEASE)
        return;
    if (!te->m_richPreview) return;
    pressed = te->m_pressedAnchor;
    te->m_pressedAnchor = NULL;
    if (!pressed) return;
    me = (XMouseEvent*)event;
    anchor = XTextEdit_anchorAt(te, &me->m_position);
    {
        const char* pUrl = XString_toUtf8(pressed);
        const char* aUrl = (anchor && XString_toUtf8(anchor))
                               ? XString_toUtf8(anchor)
                               : "";
        if (pUrl && pUrl[0] && aUrl[0] && XStrcmp(pUrl, aUrl) == 0) {
            xte_emitLinkSignal(te,
                               (size_t)XTextEdit_linkActivated_signal,
                               pUrl);
            XEvent_accept(event);
        }
    }
    XString_delete_base(pressed);
    if (anchor) XString_delete_base(anchor);
}

/** @brief 鼠标移动：进出/切换链接（URL 去重）时发射 linkHovered、
 *         切换手型光标并请求高亮重绘（对标 QLabel 悬停链路；离开链接
 *         载荷为空串）。 */
static void VX_textEdit_mouseMoveEvent(XWidget* self, XEvent* event)
{
    XTextEdit* te = (XTextEdit*)self;
    XMouseEvent* me;
    XString* anchor;
    const char* url;
    const char* hover;
    bool hasUrl;
    bool hasHover;
    bool changed;
    if (!te || !event || XEvent_type(event) != XEVENT_TYPE_MOUSE_MOVE)
        return;
    if (!te->m_richPreview) return;
    me = (XMouseEvent*)event;
    anchor = XTextEdit_anchorAt(te, &me->m_position);
    url = (anchor && XString_toUtf8(anchor)) ? XString_toUtf8(anchor) : "";
    hover = (te->m_hoverAnchor && XString_toUtf8(te->m_hoverAnchor))
                ? XString_toUtf8(te->m_hoverAnchor)
                : "";
    hasUrl = (url[0] != '\0');
    hasHover = (hover[0] != '\0');
    changed = (hasUrl != hasHover) ||
              (hasUrl && hasHover && XStrcmp(url, hover) != 0);
    if (changed) {
        XCursor cursor;
        if (te->m_hoverAnchor) {
            XString_delete_base(te->m_hoverAnchor);
            te->m_hoverAnchor = NULL;
        }
        if (hasUrl) te->m_hoverAnchor = XString_create_utf8(url);
        xte_emitLinkSignal(te,
                           (size_t)XTextEdit_linkHovered_signal,
                           hasUrl ? url : "");
        /* 手型光标进出链接（对标 QLabel 悬停手型）。 */
        XCursor_init(&cursor);
        XCursor_setShape(&cursor, XCursor_PointingHand);
        XWidget_setCursor((XWidget*)te, &cursor);
        if (!hasUrl) XWidget_unsetCursor((XWidget*)te);
        XWidget_update((XWidget*)te);
    }
    if (anchor) XString_delete_base(anchor);
}

static void VX_textEdit_resizeEvent(XWidget* self, XEvent* event)
{
    XTextEdit* te = (XTextEdit*)self;
    if (!te || !te->m_editor) return;
    XWidget_setGeometry((XWidget*)te->m_editor, 0, 0,
                        XWidget_width(self), XWidget_height(self));
    /* 预览态视口高度变化：滚动范围随之刷新。 */
    xte_updatePreviewScroll(te);
}

static void VX_textEdit_paintEvent(XWidget* self, XEvent* event)
{
    XTextEdit* te = (XTextEdit*)self;
    XPainter painter;
    XImage* image;
    XPoint offset;
    XRect r;
#if XTEXTDOCUMENT_ON
    XScrollBar* vsb;
    int scroll = 0;
    XTEPaintCtx ctx;
    if (!te || !event) return;
    image = XWidget_paintImage(self);
    if (!image) return;
    XPainter_init(&painter, NULL);
    if (!XPainter_begin_image(&painter, image)) {
        XPainter_deinit(&painter);
        return;
    }
    offset = XWidget_paintOffset(self);
    if (offset.x != 0 || offset.y != 0)
        XPainter_translate(&painter, (float)offset.x, (float)offset.y);
    r.x = 0; r.y = 0;
    r.width = XWidget_width(self);
    r.height = XWidget_height(self);
    /* 白色背景 */
    XPainter_fillRect(&painter, &r, 0xFFFFFFFFu);
    if (!te->m_richPreview) {
        /* 编辑态：正文由内嵌编辑器子控件绘制，壳只铺底。 */
        XPainter_deinit(&painter);
        return;
    }
    /* 预览态：垂直滚动取值参与平移（与 xte_updatePreviewScroll 的
       范围口径一致）。 */
    vsb = XAbstractScrollArea_verticalScrollBar(
        (const XAbstractScrollArea*)te);
    if (vsb) scroll = XScrollBar_value(vsb);
    if (scroll != 0)
        XPainter_translate(&painter, 0.0f, (float)-scroll);
    /* 渲染子集：逐块格式切换绘制（b/i/u/s、色/号、对齐、链接）。 */
    ctx.painter = &painter;
    ctx.hover = (te->m_hoverAnchor && XString_toUtf8(te->m_hoverAnchor))
                    ? XString_toUtf8(te->m_hoverAnchor)
                    : NULL;
    xte_walkRich(te, xte_paintFrag, &ctx);
    XPainter_deinit(&painter);
#else
    if (!te || !event) return;
    image = XWidget_paintImage(self);
    if (!image) return;
    XPainter_init(&painter, NULL);
    if (!XPainter_begin_image(&painter, image)) return;
    offset = XWidget_paintOffset(self);
    if (offset.x != 0 || offset.y != 0)
        XPainter_translate(&painter, (float)offset.x, (float)offset.y);
    XRect_init(&r, 0, 0, XWidget_width(self), XWidget_height(self));
    XPainter_fillRect(&painter, &r, 0xFFFFFFFFu);
    XPainter_deinit(&painter);
#endif
}

/* ==================== 生命周期与虚表 ==================== */

#if XINPUTMETHOD_ON
/**
 * @brief      输入法查询虚槽：转发内嵌编辑器（对标 QTextEdit::
 *             inputMethodQuery 委托内建 QTextDocument/QTextControl）。
 * @details    本控件以内嵌 XPlainTextEdit 承载编辑能力（编辑器几何铺满
 *             壳、原点重合，局部坐标无需换算），经其虚表分派到已重载的
 *             查询槽（内部再委托 XTextControl 全枚举），使 ImCursorRectangle
 *             返回真实光标矩形、ImSurroundingText/ImCursorPosition/
 *             ImCurrentSelection 返回真实文本态；无内嵌编辑器返回 NULL
 *             （等价无效 QVariant）。
 */
static XVariant* VX_textEdit_inputMethodQuery(const XWidget* self,
                                              XInputMethodQuery query)
{
    XTextEdit* te = (XTextEdit*)self;
    if (!te || !te->m_editor) return NULL;
    return XWidget_inputMethodQuery((const XWidget*)te->m_editor, query);
}
#endif /* XINPUTMETHOD_ON */

static void VXTextEdit_deinit(XTextEdit* self)
{
    if (!self) return;
#if XTEXTDOCUMENT_ON
    /* 仅释放内部默认文档；setDocument 接管的外部文档所有权归调用方。 */
    if (self->m_textDoc && self->m_textDocOwned) {
        XClass_delete_base((XClass*)self->m_textDoc);
    }
    self->m_textDoc = NULL;
#endif
    /* 预览态锚点交互承载（对象拥有）。 */
    xte_resetAnchorState(self);
    if (self->m_fontFamily) {
        XString_delete_base(self->m_fontFamily);
        self->m_fontFamily = NULL;
    }
    if (self->m_documentTitle) {
        XString_delete_base(self->m_documentTitle);
        self->m_documentTitle = NULL;
    }
    if (self->m_markdown) {
        XString_delete_base(self->m_markdown);
        self->m_markdown = NULL;
    }
    if (self->m_editor) {
        XClass_delete_base((XClass*)self->m_editor);
        self->m_editor = NULL;
    }
    XClass_Deinit_Parent(XAbstractScrollArea, (XAbstractScrollArea*)self);
}

XVtable* XTextEdit_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XTextEdit)
    XVTABLE_INHERIT_XCLASS(XAbstractScrollArea);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ResizeEvent, VX_textEdit_resizeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VX_textEdit_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MousePressEvent, VX_textEdit_mousePressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseReleaseEvent, VX_textEdit_mouseReleaseEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseMoveEvent, VX_textEdit_mouseMoveEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXTextEdit_deinit);
#if XINPUTMETHOD_ON
    /* 输入法查询虚槽：转发内嵌编辑器（对标 QTextEdit::inputMethodQuery
       委托内建文档控制层；基类兜底只回居中假矩形）。 */
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_InputMethodQuery, VX_textEdit_inputMethodQuery);
#endif /* XINPUTMETHOD_ON */
    return XVTABLE_DEFAULT;
}

/* ==================== 内嵌编辑器信号桥（壳转接，真发射） ====================
 * 对标 Qt：QTextEdit 的 textChanged 等信号由内建设施发出。XTextEdit 以
 * 内嵌 XPlainTextEdit 承载编辑能力，此处把其信号转接为壳的同名信号。
 * 此前 8 个信号函数仅返回标识、无任何发射点（死信号）。 */

static void xte_fwdVoid(XObject* receiver, XVarList* args, size_t signal)
{
    XTextEdit* self = (XTextEdit*)receiver;
    if (!self) {
        if (args) XVarList_delete(args);
        return;
    }
    if (((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

static void xte_fwdTextChanged(XObject* receiver, XVarList* args)
{ xte_fwdVoid(receiver, args, (size_t)XTextEdit_textChanged_signal); }

static void xte_fwdCursorPositionChanged(XObject* receiver, XVarList* args)
{ xte_fwdVoid(receiver, args, (size_t)XTextEdit_cursorPositionChanged_signal); }

static void xte_fwdSelectionChanged(XObject* receiver, XVarList* args)
{ xte_fwdVoid(receiver, args, (size_t)XTextEdit_selectionChanged_signal); }

static void xte_fwdCopyAvailable(XObject* receiver, XVarList* args)
{ xte_fwdVoid(receiver, args, (size_t)XTextEdit_copyAvailable_signal); }

static void xte_fwdModificationChanged(XObject* receiver, XVarList* args)
{ xte_fwdVoid(receiver, args, (size_t)XTextEdit_modificationChanged_signal); }

static void xte_fwdUndoAvailable(XObject* receiver, XVarList* args)
{ xte_fwdVoid(receiver, args, (size_t)XTextEdit_undoAvailable_signal); }

static void xte_fwdRedoAvailable(XObject* receiver, XVarList* args)
{ xte_fwdVoid(receiver, args, (size_t)XTextEdit_redoAvailable_signal); }

static void xte_connectEditorSignals(XTextEdit* self)
{
    XObject* ed;
    if (!self || !self->m_editor) return;
    ed = (XObject*)self->m_editor;
#define XTE_CONNECT(sig, slot) \
    XObject_connect_1(ed, (size_t)(sig), (XObject*)self, (slot), \
                      XConnectionType_Direct)
    XTE_CONNECT(XPlainTextEdit_textChanged_signal(self->m_editor),
                xte_fwdTextChanged);
    XTE_CONNECT(XPlainTextEdit_cursorPositionChanged_signal(self->m_editor),
                xte_fwdCursorPositionChanged);
    XTE_CONNECT(XPlainTextEdit_selectionChanged_signal(self->m_editor),
                xte_fwdSelectionChanged);
    XTE_CONNECT(XPlainTextEdit_copyAvailable_signal(self->m_editor, true),
                xte_fwdCopyAvailable);
    XTE_CONNECT(XPlainTextEdit_modificationChanged_signal(self->m_editor,
                                                          false),
                xte_fwdModificationChanged);
    XTE_CONNECT(XPlainTextEdit_undoAvailable_signal(self->m_editor, false),
                xte_fwdUndoAvailable);
    XTE_CONNECT(XPlainTextEdit_redoAvailable_signal(self->m_editor, false),
                xte_fwdRedoAvailable);
#undef XTE_CONNECT
}

void XTextEdit_init(XTextEdit* self, XWidget* parent,
                           XWidgetFlags flags)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XAbstractScrollArea_init(&self->m_base, parent, flags);
    self->m_editor = XPlainTextEdit_create_ex(
        XCLASS_DEFAULT_MEMORY_TYPE, (XWidget*)self, 0);
    XWidget_resize((XWidget*)self->m_editor, 200, 100);
    xte_connectEditorSignals(self);
#if XTEXTDOCUMENT_ON
    self->m_textDoc = XTextDocument_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    self->m_textDocOwned = true; /* 内部默认文档：拥有并负责释放。 */
#endif
    XClassSetVtable(self, XTextEdit);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    self->m_textColor = 0xFF000000u;
    self->m_alignment = 1; /* AlignLeft */

    self->m_fontFamily = XString_create();
    self->m_documentTitle = XString_create();
    self->m_markdown = XString_create();
    self->m_fontWeight = 400;
    self->m_fontPointSize = 10.0;
    self->m_tabStopDistance = 80.0;
    self->m_cursorWidth = 1;
    self->m_lineWrapMode = 0;
    self->m_lineWrapColumnOrWidth = 0; /* 对标 Qt 默认值 0。 */
    self->m_wordWrapMode = 1;
    self->m_acceptRichText = true;
    self->m_autoFormatting = 0;
    self->m_centerOnScroll = false;
    self->m_textBackgroundColor = 0;
    self->m_richPreview = false;   /* 默认纯文本编辑态。 */
    self->m_hoverAnchor = NULL;
    self->m_pressedAnchor = NULL;
}

XTextEdit* XTextEdit_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags)
{
    XTextEdit* self = (XTextEdit*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XTextEdit_init(self, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 字符格式存取 ==================== */

void XTextEdit_setBold(XTextEdit* self, bool bold) { if (self) self->m_bold = bold; }
bool XTextEdit_isBold(const XTextEdit* self) { return self ? self->m_bold : false; }
void XTextEdit_setItalic(XTextEdit* self, bool italic) { if (self) self->m_italic = italic; }
bool XTextEdit_isItalic(const XTextEdit* self) { return self ? self->m_italic : false; }
void XTextEdit_setUnderline(XTextEdit* self, bool underline) { if (self) self->m_underline = underline; }
bool XTextEdit_isUnderline(const XTextEdit* self) { return self ? self->m_underline : false; }
void XTextEdit_setTextColor(XTextEdit* self, uint32_t color) { if (self) self->m_textColor = color; }
uint32_t XTextEdit_textColor(const XTextEdit* self) { return self ? self->m_textColor : 0; }
void XTextEdit_setAlignment(XTextEdit* self, int alignment) { if (self) self->m_alignment = alignment; }
int XTextEdit_alignment(const XTextEdit* self) { return self ? self->m_alignment : 0; }

/* ==================== HTML 解析（基础子集） ==================== */

static void xte_skipTag(const char** p) { while (**p && **p != '>') ++(*p); if (**p) ++(*p); }

/** @brief 清除 setMarkdown 承载的原文（内容被替换类接口重置后调用）。
 * @param self 目标控件指针；可为 NULL。
 * @return 无返回值。
 */
static void xte_resetMarkdown(XTextEdit* self)
{
    if (!self || !self->m_markdown) return;
    XString_assign_utf8(self->m_markdown, "");
}

/** @brief 以内嵌编辑器当前文本刷新富文本文档（纯文本口径同步）。
 * @details 与 toHtml 导出前的刷新同源：编辑器是文本写入源，文档按
 *          纯文本口径重建，保证文档渲染/导出反映显示内容。
 * @param self 目标控件指针；NULL、文档或内嵌编辑器缺失时无操作。
 * @return 无返回值。
 */
static void xte_syncDocFromEditor(XTextEdit* self)
{
#if XTEXTDOCUMENT_ON
    char* plain;
    if (!self || !self->m_textDoc || !self->m_editor) return;
    plain = XPlainTextEdit_toPlainText(self->m_editor);
    if (!plain) return;
    XTextDocument_setPlainText(self->m_textDoc, plain);
    XFree_System(plain);
#else
    (void)self;
#endif
}

/** @brief 剥离 HTML 标签得到纯文本（b/i/u/br/p 子集，setHtml 与
 *         insertHtml 共用同一口径）。
 * @details 识别 b/i/u 开闭标签跟踪行内格式终态；br/p 视为换行；其余
 *          标签整体丢弃；实体（&amp; 等）不展开原样拷贝；输出截断到
 *          cap-1 字节。
 * @param html 输入 HTML；不为 NULL。
 * @param plain 输出缓冲；不为 NULL。
 * @param cap 输出缓冲容量（含 NUL）。
 * @param bold 粗体终态输出；可为 NULL 忽略。
 * @param italic 斜体终态输出；可为 NULL 忽略。
 * @param underline 下划线终态输出；可为 NULL 忽略。
 * @return 无返回值。
 */
static void xte_htmlStripToPlain(const char* html, char* plain, size_t cap,
                                 bool* bold, bool* italic, bool* underline)
{
    const char* p = html;
    size_t o = 0;
    bool b = false, i = false, u = false;
    if (!html || !plain || cap == 0) return;
    while (*p && o < cap - 1) {
        if (*p == '<') {
            ++p;
            if (XStrncmp(p, "b>", 2) == 0) { b = true; xte_skipTag(&p); }
            else if (XStrncmp(p, "/b>", 3) == 0) { b = false; xte_skipTag(&p); }
            else if (XStrncmp(p, "i>", 2) == 0) { i = true; xte_skipTag(&p); }
            else if (XStrncmp(p, "/i>", 3) == 0) { i = false; xte_skipTag(&p); }
            else if (XStrncmp(p, "u>", 2) == 0) { u = true; xte_skipTag(&p); }
            else if (XStrncmp(p, "/u>", 3) == 0) { u = false; xte_skipTag(&p); }
            else if (XStrncmp(p, "br", 2) == 0 || XStrncmp(p, "p", 1) == 0) { plain[o++] = '\n'; xte_skipTag(&p); }
            else xte_skipTag(&p);
        } else {
            plain[o++] = *p++;
        }
    }
    plain[o] = '\0';
    if (bold) *bold = b;
    if (italic) *italic = i;
    if (underline) *underline = u;
}

void XTextEdit_setHtml(XTextEdit* self, const char* html)
{
    char plain[4096];
    bool bold = false, italic = false, underline = false;
    if (!self || !html) return;
    xte_resetMarkdown(self);
#if XTEXTDOCUMENT_ON
    /* 富文本真源：渲染子集解析写入富文本文档（供 toHtml/预览/锚点）。 */
    if (self->m_textDoc) XTextDocument_setHtml(self->m_textDoc, html);
#endif
    /* 编辑缓冲降级：剥离标签的纯文本（标签不展开进缓冲——所见即所
     * 存，对标 QPlainTextEdit 无 setHtml 的纯文本语义）。不自动进入
     * 预览：编辑态下 toHtml 保持"编辑器为文本源"的既有语义（回归
     * 契约），富文本呈现经 setRichPreview(true) 显式进入。 */
    xte_htmlStripToPlain(html, plain, sizeof(plain), &bold, &italic, &underline);
    self->m_bold = bold;
    self->m_italic = italic;
    self->m_underline = underline;
    if (self->m_editor) XPlainTextEdit_setPlainText(self->m_editor, plain);
}

void XTextEdit_setRichPreview(XTextEdit* self, bool on)
{
    if (!self) return;
    if (on)
        xte_enterPreview(self);
    else
        xte_exitPreview(self);
}

bool XTextEdit_isRichPreview(const XTextEdit* self)
{
    return self ? self->m_richPreview : false;
}

char* XTextEdit_toHtml(const XTextEdit* self)
{
#if XTEXTDOCUMENT_ON
    if (self && self->m_textDoc) {
        /* 预览态：文档即富文本真源，直接导出；编辑态：编辑器为文本
         * 源，先以编辑器纯文本刷新文档（既有口径）再导出。 */
        if (!self->m_richPreview && self->m_editor) {
            char* plain = XPlainTextEdit_toPlainText(self->m_editor);
            if (plain) {
                XTextDocument_setPlainText(self->m_textDoc, plain);
                XFree_System(plain);
            }
        }
        return XTextDocument_toHtml(self->m_textDoc);
    }
#endif
    {
        char* plain;
        size_t cap;
        char* html;
        size_t o = 0;
        int i;
        if (!self) return NULL;
        plain = XPlainTextEdit_toPlainText(self->m_editor);
        if (!plain) return NULL;
        cap = XStrlen(plain) * 8 + 128;
        html = (char*)XMalloc_System(cap);
        if (!html) { XFree_System(plain); return NULL; }
        o = (size_t)XSnprintf(html, cap, "<html><body>");
        for (i = 0; plain[i]; ++i) {
            if (plain[i] == '\n') o += (size_t)XSnprintf(html + o, cap - o, "<br>");
            else if (plain[i] == '<') o += (size_t)XSnprintf(html + o, cap - o, "&lt;");
            else if (plain[i] == '>') o += (size_t)XSnprintf(html + o, cap - o, "&gt;");
            else if (plain[i] == '&') o += (size_t)XSnprintf(html + o, cap - o, "&amp;");
            else o += (size_t)XSnprintf(html + o, cap - o, "%c", plain[i]);
        }
        o += (size_t)XSnprintf(html + o, cap - o, "</body></html>");
        XFree_System(plain);
        return html;
    }
}

/* ==================== 纯文本 / setText 自动探测 ==================== */

/** @brief Qt::mightBeRichText 子集启发式：判定文本是否"像"富文本。
 * @details 跳过前导空白后定位首个 '<'（遇 '\n' 放弃），检查其到首个 '>'
 *          之间是否构成标签状构造：'!'/'?' 开头视为注释/处理指令；或
 *          可选 '/' 后跟至少一个字母/数字；出现其他字符则判为普通文本。
 * @param text 待判定 UTF-8 文本；可为 NULL。
 * @return 判定为富文本返回 true，否则返回 false。
 */
static bool xte_mightBeRichText(const char* text)
{
    const char* open;
    const char* close;
    const char* q;
    bool sawName = false;
    if (!text) return false;
    while (*text == ' ' || *text == '\t' || *text == '\r' || *text == '\n')
        ++text;
    open = text;
    while (*open && *open != '<' && *open != '\n') ++open;
    if (*open != '<') return false;
    close = open + 1;
    while (*close && *close != '>') ++close;
    if (*close != '>') return false;
    q = open + 1;
    if (*q == '!' || *q == '?') return true;
    if (*q == '/') ++q;
    while (*q && *q != '>' && *q != ' ' && *q != '\t'
           && *q != '\r' && *q != '\n' && *q != '/') {
        if (((*q >= 'a') && (*q <= 'z')) || ((*q >= 'A') && (*q <= 'Z'))
            || (*q >= '0' && *q <= '9')) {
            sawName = true;
            ++q;
        } else {
            return false;
        }
    }
    return sawName;
}

XString* XTextEdit_toPlainText(const XTextEdit* self)
{
    XString* out = XString_create();
    char* plain;
    if (!out) return NULL;
    if (!self || !self->m_editor) return out;
    plain = XPlainTextEdit_toPlainText(self->m_editor);
    if (plain) {
        XString_assign_utf8(out, plain);
        XFree_System(plain);
    }
    return out;
}

void XTextEdit_setText(XTextEdit* self, const char* text)
{
    if (!self || !text) return;
    if (xte_mightBeRichText(text)) {
        XTextEdit_setHtml(self, text);
        return;
    }
    /* 纯文本分支：统一走 setPlainText（复位字符格式/清除 Markdown
     * 原文/富文本文档同步，语义与原实现一致）。 */
    XTextEdit_setPlainText(self, text);
}

/* ==================== 撤销 / 重做 ==================== */

void XTextEdit_undo(XTextEdit* self)
{
    if (!self || !self->m_editor) return;
    /* 编辑缓冲变化：预览态退出回编辑态。 */
    xte_exitPreview(self);
    XPlainTextEdit_undo(self->m_editor);
}

void XTextEdit_redo(XTextEdit* self)
{
    if (!self || !self->m_editor) return;
    xte_exitPreview(self);
    XPlainTextEdit_redo(self->m_editor);
}

bool XTextEdit_canUndo(const XTextEdit* self)
{
    if (!self || !self->m_editor || !self->m_editor->m_undoStack) return false;
    return XVector_size_base((const XContainer*)self->m_editor->m_undoStack) > 0;
}

bool XTextEdit_canRedo(const XTextEdit* self)
{
    if (!self || !self->m_editor || !self->m_editor->m_redoStack) return false;
    return XVector_size_base((const XContainer*)self->m_editor->m_redoStack) > 0;
}

/* ==================== 信号 ==================== */

void* XTextEdit_textChanged_signal(XTextEdit* self)
{
    (void)self;
    return (void*)(size_t)XTextEdit_textChanged_signal;
}


void* XTextEdit_copyAvailable_signal(XTextEdit* self)
{
    (void)self;
    return (void*)(size_t)XTextEdit_copyAvailable_signal;
}
void* XTextEdit_cursorPositionChanged_signal(XTextEdit* self)
{
    (void)self;
    return (void*)(size_t)XTextEdit_cursorPositionChanged_signal;
}
void* XTextEdit_modificationChanged_signal(XTextEdit* self)
{
    (void)self;
    return (void*)(size_t)XTextEdit_modificationChanged_signal;
}
void* XTextEdit_redoAvailable_signal(XTextEdit* self)
{
    (void)self;
    return (void*)(size_t)XTextEdit_redoAvailable_signal;
}
void* XTextEdit_selectionChanged_signal(XTextEdit* self)
{
    (void)self;
    return (void*)(size_t)XTextEdit_selectionChanged_signal;
}
void* XTextEdit_undoAvailable_signal(XTextEdit* self)
{
    (void)self;
    return (void*)(size_t)XTextEdit_undoAvailable_signal;
}

void XTextEdit_append(XTextEdit* self, const char* text)
{
    if (!self || !self->m_editor) return;
    xte_exitPreview(self);
    XPlainTextEdit_appendPlainText(&self->m_editor->m_base, text);
}
void XTextEdit_copy_2(XTextEdit* self) { XPlainTextEdit_copy(&self->m_editor->m_base); }
void XTextEdit_cut_2(XTextEdit* self)
{
    if (!self || !self->m_editor) return;
    xte_exitPreview(self);
    XPlainTextEdit_cut(&self->m_editor->m_base);
}
void XTextEdit_paste_2(XTextEdit* self)
{
    if (!self || !self->m_editor) return;
    xte_exitPreview(self);
    XPlainTextEdit_paste(&self->m_editor->m_base);
}
void XTextEdit_clear_2(XTextEdit* self)
{
    if (!self || !self->m_editor) return;
    /* 清空即内容整体替换：退出预览并同步清空富文本文档。 */
    xte_exitPreview(self);
    XPlainTextEdit_clear(&self->m_editor->m_base);
#if XTEXTDOCUMENT_ON
    if (self->m_textDoc) XTextDocument_clear(self->m_textDoc);
#endif
    XWidget_update((XWidget*)self);
}
void XTextEdit_selectAll_2(XTextEdit* self) { XPlainTextEdit_selectAll(&self->m_editor->m_base); }
bool XTextEdit_canPaste(XTextEdit* self) { return XPlainTextEdit_isReadOnly(&self->m_editor->m_base) ? false : true; }
void XTextEdit_setAcceptRichText(XTextEdit* self, bool accept) { if (self) self->m_acceptRichText = accept; }
bool XTextEdit_acceptRichText(const XTextEdit* self) { return self ? self->m_acceptRichText : true; }
void XTextEdit_setTextBackgroundColor(XTextEdit* self, uint32_t color) { if (self) self->m_textBackgroundColor = color; }
uint32_t XTextEdit_textBackgroundColor(const XTextEdit* self) { return self ? self->m_textBackgroundColor : 0; }
void XTextEdit_setFontFamily(XTextEdit* self, const char* family)
{
    if (!self) return;
    if (!self->m_fontFamily) self->m_fontFamily = XString_create();
    if (self->m_fontFamily)
        XString_assign_utf8(self->m_fontFamily, family ? family : "");
}
const char* XTextEdit_fontFamily(const XTextEdit* self)
{
    if (!self || !self->m_fontFamily) return "";
    return XString_toUtf8(self->m_fontFamily);
}
void XTextEdit_setFontWeight(XTextEdit* self, int weight) { if (self && weight > 0) self->m_fontWeight = weight; }
int XTextEdit_fontWeight(const XTextEdit* self) { return self ? self->m_fontWeight : 400; }
void XTextEdit_setFontPointSize(XTextEdit* self, double size) { if (self && size > 0) self->m_fontPointSize = size; }
double XTextEdit_fontPointSize(const XTextEdit* self) { return self ? self->m_fontPointSize : 10.0; }
void XTextEdit_setCurrentFont(XTextEdit* self, const char* family) { XTextEdit_setFontFamily(self, family); }

XFont XTextEdit_currentFont(const XTextEdit* self)
{
    XFont font;
    if (!self) {
        XFont_init(&font);
        return font;
    }
    /* 对标 QTextEdit::currentFont：Qt 返回光标处字符格式字体；本库为
       整篇单格式，即当前字体属性组合。字号按四舍五入收敛为整型点值
       （XFont_init_ex 口径）。 */
    XFont_init_ex(&font, XTextEdit_fontFamily(self),
                  (int)(self->m_fontPointSize > 0.0
                            ? self->m_fontPointSize + 0.5
                            : 10),
                  self->m_fontWeight, XTextEdit_isItalic(self));
    XFont_setUnderline(&font, XTextEdit_isUnderline(self));
    return font;
}
void XTextEdit_zoomIn(XTextEdit* self, int range) { if (self) { self->m_fontPointSize += (range > 0 ? range : 1); if (self->m_fontPointSize > 100) self->m_fontPointSize = 100; } }
void XTextEdit_zoomOut(XTextEdit* self, int range) { if (self) { self->m_fontPointSize -= (range > 0 ? range : 1); if (self->m_fontPointSize < 1) self->m_fontPointSize = 1; } }
void XTextEdit_setTabStopDistance(XTextEdit* self, double distance) { if (self && distance >= 0) self->m_tabStopDistance = distance; }
double XTextEdit_tabStopDistance(const XTextEdit* self) { return self ? self->m_tabStopDistance : 80.0; }
void XTextEdit_setAutoFormatting(XTextEdit* self, int features) { if (self) self->m_autoFormatting = features; }
int XTextEdit_autoFormatting(const XTextEdit* self) { return self ? self->m_autoFormatting : 0; }
void XTextEdit_setTabChangesFocus(XTextEdit* self, bool b) { (void)self; (void)b; /* 键盘焦点链由 XWidget 统一管理；存储位预留。 */ }
bool XTextEdit_tabChangesFocus(const XTextEdit* self) { (void)self; return false; }
void XTextEdit_setDocumentTitle(XTextEdit* self, const char* title)
{
    if (!self) return;
    if (!self->m_documentTitle) self->m_documentTitle = XString_create();
    if (self->m_documentTitle)
        XString_assign_utf8(self->m_documentTitle, title ? title : "");
}
const char* XTextEdit_documentTitle(const XTextEdit* self)
{
    if (!self || !self->m_documentTitle) return "";
    return XString_toUtf8(self->m_documentTitle);
}
void XTextEdit_setUndoRedoEnabled_2(XTextEdit* self, bool enable) { XPlainTextEdit_setUndoRedoEnabled(&self->m_editor->m_base, enable); }
bool XTextEdit_isUndoRedoEnabled_2(const XTextEdit* self) { return XPlainTextEdit_isUndoRedoEnabled(&self->m_editor->m_base); }
void XTextEdit_setLineWrapMode(XTextEdit* self, int mode) { if (self) self->m_lineWrapMode = mode; }
int XTextEdit_lineWrapMode(const XTextEdit* self) { return self ? self->m_lineWrapMode : 0; }
void XTextEdit_setWordWrapMode(XTextEdit* self, int policy) { if (self) self->m_wordWrapMode = policy; }
int XTextEdit_wordWrapMode(const XTextEdit* self) { return self ? self->m_wordWrapMode : 1; }
void XTextEdit_setReadOnly_2(XTextEdit* self, bool ro) { XPlainTextEdit_setReadOnly(&self->m_editor->m_base, ro); }
bool XTextEdit_isReadOnly_2(const XTextEdit* self) { return XPlainTextEdit_isReadOnly(&self->m_editor->m_base); }
void XTextEdit_setPlaceholderText_2(XTextEdit* self, const char* text) { XPlainTextEdit_setPlaceholderText(&self->m_editor->m_base, text); }
const char* XTextEdit_placeholderText_2(const XTextEdit* self) { return XPlainTextEdit_placeholderText(&self->m_editor->m_base); }
void XTextEdit_ensureCursorVisible_2(XTextEdit* self) { XPlainTextEdit_ensureCursorVisible(&self->m_editor->m_base); }
void XTextEdit_setCenterOnScroll(XTextEdit* self, bool enabled) { if (self) self->m_centerOnScroll = enabled; }
bool XTextEdit_centerOnScroll(const XTextEdit* self) { return self ? self->m_centerOnScroll : false; }
void XTextEdit_setExtraSelections(XTextEdit* self, void* selections) { (void)self; (void)selections; }
void XTextEdit_setBackgroundVisible(XTextEdit* self, bool visible) { (void)self; (void)visible; }
bool XTextEdit_backgroundVisible(const XTextEdit* self) { (void)self; return false; }
void XTextEdit_setTextCursor_2(XTextEdit* self, void* cursor) { (void)self; (void)cursor; }
void* XTextEdit_textCursor(const XTextEdit* self) { (void)self; return NULL; }
void XTextEdit_setCursorWidth(XTextEdit* self, int width) { if (self && width > 0) self->m_cursorWidth = width; }
int XTextEdit_cursorWidth(const XTextEdit* self) { return self ? self->m_cursorWidth : 1; }
/* ==================== 光标几何与查找（对标 QPlainTextEdit 同组 API） ==== */

bool XTextEdit_find_2(XTextEdit* self, const char* exp, int flags)
{
    bool hit;
    if (!self || !self->m_editor) return false;
    /* 委托内嵌编辑器：命中置光标并请求编辑器重绘（XPlainTextEdit_find
     * 内部已 update）；外层控件富文本文档渲染同步刷新一次。 */
    hit = XPlainTextEdit_find(self->m_editor, exp, flags);
    if (hit) XWidget_update((XWidget*)self);
    return hit;
}

XRect XTextEdit_cursorRect(const XTextEdit* self)
{
    /* 与内嵌编辑器 paintEvent 同口径：编辑器常驻 (0,0) 铺满本控件，
     * 其局部坐标即本控件局部坐标；NULL 逐层兜底返回零矩形。 */
    return XPlainTextEdit_cursorRect(self ? self->m_editor : NULL);
}

#if XTEXTDOCUMENT_ON
/** @brief 命中测试上下文（与预览绘制共用 xte_walkRich 同一口径几何）。 */
typedef struct XTEHitCtx
{
    XPoint pos;          /**< 控件局部坐标。 */
    int scroll;          /**< 预览垂直滚动取值（命中随内容平移）。 */
    XString* result;     /**< 命中 href 拷贝（NULL=未命中）。 */
} XTEHitCtx;

/** @brief 片段命中访问：pos 落在带 href 的片段矩形内即记录并截断。 */
static void xte_hitFrag(XTextEdit* self, const XTERichGeom* geom, void* ud)
{
    XTEHitCtx* ctx = (XTEHitCtx*)ud;
    const XTDFragment* f = geom->frag;
    const char* href;
    int py;
    (void)self;
    if (!ctx || ctx->result || !f) return;
    href = f->fmt.anchorHref ? XString_toUtf8(f->fmt.anchorHref) : NULL;
    if (!href || !href[0]) return;    py = ctx->pos.y + ctx->scroll;
    if (py >= geom->top && py < geom->top + geom->lineH &&
        ctx->pos.x >= geom->x && ctx->pos.x < geom->x + geom->width) {
        ctx->result = XString_create_copy(f->fmt.anchorHref);
    }
}
#endif /* XTEXTDOCUMENT_ON */

XString* XTextEdit_anchorAt(const XTextEdit* self, const XPoint* pos)
{
#if XTEXTDOCUMENT_ON
    XTEHitCtx ctx;
    XScrollBar* vsb;
    if (!self || !pos) return XString_create_utf8("");
    ctx.pos = *pos;
    vsb = XAbstractScrollArea_verticalScrollBar(
        (const XAbstractScrollArea*)self);
    ctx.scroll = vsb ? XScrollBar_value(vsb) : 0;
    ctx.result = NULL;
    /* 与预览绘制同一遍历（逐块格式切换几何，无落盘布局）：保证命中
       与所见一致。未命中返回空串对象（对标 anchorAt 契约）。 */
    xte_walkRich(self, xte_hitFrag, &ctx);
    if (ctx.result) return ctx.result;
    return XString_create_utf8("");
#else
    (void)self;
    (void)pos;
    return XString_create_utf8("");
#endif
}

void XTextEdit_setTextCursor(XTextEdit* self, int line, int col)
{
    if (!self || !self->m_editor) return;
    /* 委托内嵌编辑器钳位并置光标（内部已 update）；外层同步刷新。 */
    XPlainTextEdit_setTextCursor(self->m_editor, line, col);
    XWidget_update((XWidget*)self);
}

int XTextEdit_textCursorLine(const XTextEdit* self)
{
    if (!self || !self->m_editor) return 0;
    return XPlainTextEdit_textCursorLine(self->m_editor);
}

int XTextEdit_textCursorColumn(const XTextEdit* self)
{
    if (!self || !self->m_editor) return 0;
    return XPlainTextEdit_textCursorColumn(self->m_editor);
}

/* ==================== 文档/插入/资源/Markdown 补齐组（对标 QTextEdit） ==== */

/** @brief 内嵌编辑器行高（与 XPlainTextEdit.c 的 XPE_LINE_HEIGHT 同口径）。 */
#define XTE_LINE_HEIGHT 16
/** @brief 行左留白（与 XPlainTextEdit 绘制/cursorRect 口径一致）。 */
#define XTE_LEFT_MARGIN 2

/** @brief 读取内嵌编辑器第 index 行文本（只读借用，不转移所有权）。
 * @param editor 内嵌编辑器指针；可为 NULL。
 * @param index 行号（0 起；越界返回空串）。
 * @return 行文本借用指针（编辑器行数组元素）；无效输入返回 ""。
 */
static const char* xte_editorLineAt(const XPlainTextEdit* editor, int index)
{
    char** item;
    if (!editor || !editor->m_lines || index < 0 ||
        index >= (int)XVector_size_base((const XContainer*)editor->m_lines))
        return "";
    item = (char**)XVector_at_base(editor->m_lines, index);
    return (item && *item) ? *item : "";
}

/** @brief 读取内嵌编辑器垂直滚动条取值（像素口径，同 cursorRect）。
 * @param editor 内嵌编辑器指针；可为 NULL。
 * @return 垂直滚动取值；滚动条缺失返回 0。
 */
static int xte_editorScrollY(const XPlainTextEdit* editor)
{
    XScrollBar* vsb;
    if (!editor) return 0;
    vsb = XAbstractScrollArea_verticalScrollBar(
        (const XAbstractScrollArea*)editor);
    return vsb ? XScrollBar_value(vsb) : 0;
}

XPoint XTextEdit_cursorForPosition(const XTextEdit* self, const XPoint* pos)
{
    XPoint pt;
    const XPlainTextEdit* editor;
    const char* line;
    XFont font;
    int scroll;
    int lineCount;
    int lineIdx;
    int relX;
    int off;
    pt.x = 0;
    pt.y = 0;
    if (!self || !pos) return pt;
    editor = self->m_editor;
    if (!editor) return pt;
    lineCount = XPlainTextEdit_blockCount(editor);
    if (lineCount <= 0) return pt;
    /* Y 反查：与 cursorRect 的 y = 行号*行高 - 滚动取值 同口径求逆。 */
    scroll = xte_editorScrollY(editor);
    lineIdx = (pos->y + scroll) / XTE_LINE_HEIGHT;
    if (lineIdx < 0) lineIdx = 0;
    if (lineIdx > lineCount - 1) lineIdx = lineCount - 1;
    line = xte_editorLineAt(editor, lineIdx);
    /* X 反查：减去行左留白后逐字符累加宽度（字节偏移口径），定位
     * 光标列；与 setTextCursor 的钳位规则一致可往返配合。 */
    font = XWidget_fontMetrics((const XWidget*)editor);
    relX = pos->x - XTE_LEFT_MARGIN;
    off = 0;
    while (line[off] != '\0' && relX > 0) {
        int adv = xte_utf8CharLen((unsigned char)line[off]);
        int w = XPainter_textWidthRange(&font, line, off, off + adv);
        if (relX < w) break;
        relX -= w;
        off += adv;
    }
    pt.x = lineIdx; /* x 分量承载行号（0 起）。 */
    pt.y = off;     /* y 分量承载列（行内 UTF-8 字节偏移）。 */
    return pt;
}

void XTextEdit_insertPlainText(XTextEdit* self, const char* text)
{
    if (!self || !self->m_editor || !text) return;
    /* 编辑缓冲变化：预览态退出回编辑态（编辑保持纯文本语义）。 */
    xte_exitPreview(self);
    /* 委托内嵌编辑器在光标处插入（支持 \n 跨行），随后文档纯文本口径
     * 同步并请求重绘，保持文档与显示一致。 */
    XPlainTextEdit_insertPlainText(self->m_editor, text);
    xte_syncDocFromEditor(self);
    XWidget_update((XWidget*)self);
}

void XTextEdit_insertHtml(XTextEdit* self, const char* html)
{
    char plain[4096];
    if (!self || !html) return;
    /* 与 setHtml 同口径剥离标签，仅保留纯文本后按光标处插入。 */
    xte_htmlStripToPlain(html, plain, sizeof(plain), NULL, NULL, NULL);
    XTextEdit_insertPlainText(self, plain);
}

int XTextEdit_lineWrapColumnOrWidth(const XTextEdit* self)
{
    return self ? self->m_lineWrapColumnOrWidth : 0;
}

void XTextEdit_setLineWrapColumnOrWidth(XTextEdit* self, int w)
{
    if (!self) return;
    /* 状态承载：平铺模型第一版不换行绘制，仅存储不触发重排。 */
    self->m_lineWrapColumnOrWidth = w;
}

XVariant* XTextEdit_loadResource(XTextEdit* self, int type, const char* name)
{
    /* 资源体系未建：对标 Qt 默认实现返回无效 QVariant 的语义，以 NULL
     * 承载；type/name 仅保持签名一致。 */
    (void)self;
    (void)type;
    (void)name;
    return NULL;
}

void XTextEdit_mergeCurrentCharFormat(XTextEdit* self, int format)
{
    if (!self) return;
    /* 位集合并：仅置位方向的属性生效，未置位属性保持不变。 */
    if (format & XTextEditCharFormat_Bold) self->m_bold = true;
    if (format & XTextEditCharFormat_Italic) self->m_italic = true;
    if (format & XTextEditCharFormat_Underline) self->m_underline = true;
    /* 对标 Qt mergeCurrentCharFormat：格式入口发射 currentCharFormatChanged
     * （真发射；平铺模型无变化探测，入口恒发射为已声明简化）。 */
    XTextEdit_currentCharFormatChanged_signal(self);
}

void XTextEdit_setCurrentCharFormat(XTextEdit* self, int format)
{
    if (!self) return;
    /* 整体替换：置位属性开、未置位属性关（对标 setCharFormat）。 */
    self->m_bold = (format & XTextEditCharFormat_Bold) ? true : false;
    self->m_italic = (format & XTextEditCharFormat_Italic) ? true : false;
    self->m_underline = (format & XTextEditCharFormat_Underline) ? true : false;
    /* 对标 Qt setCurrentCharFormat：格式入口发射 currentCharFormatChanged
     * （真发射，见 currentCharFormatChanged_signal 发射点注释）。 */
    XTextEdit_currentCharFormatChanged_signal(self);
}

void XTextEdit_scrollToAnchor(XTextEdit* self, const char* anchor)
{
    /* 锚点几何未建：对标 Qt 接口存在性，当前为无操作。 */
    (void)self;
    (void)anchor;
}

#if XTEXTDOCUMENT_ON
void XTextEdit_setDocument(XTextEdit* self, XTextDocument* doc)
{
    if (!self || doc == self->m_textDoc) return;
    /* 释放原内部默认文档；外部接管文档所有权归调用方。 */
    if (self->m_textDoc && self->m_textDocOwned)
        XClass_delete_base((XClass*)self->m_textDoc);
    if (doc) {
        self->m_textDoc = doc;      /* 外部接管：不拥有。 */
        self->m_textDocOwned = false;
    } else {
        /* 对标 setDocument(nullptr)：重置为内部默认文档。 */
        self->m_textDoc = XTextDocument_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
        self->m_textDocOwned = true;
    }
    /* 显示以新文档为准：编辑器内容同步为新文档纯文本。 */
    if (self->m_textDoc && self->m_editor) {
        char* plain = XTextDocument_toPlainText(self->m_textDoc);
        if (plain) {
            XPlainTextEdit_setPlainText(self->m_editor, plain);
            XFree_System(plain);
        }
    }
    /* 预览态锚点交互承载随文档切换失效；滚动范围按新文档重算。 */
    xte_resetAnchorState(self);
    xte_updatePreviewScroll(self);
    XWidget_update((XWidget*)self);
}

XTextDocument* XTextEdit_document(const XTextEdit* self)
{
    return self ? self->m_textDoc : NULL;
}
#endif /* XTEXTDOCUMENT_ON */

void XTextEdit_setMarkdown(XTextEdit* self, const char* md)
{
    const char* text = (md != NULL) ? md : "";
    if (!self) return;
    /* Markdown 按纯文本降级显示：预览态退出回编辑态。 */
    xte_exitPreview(self);
    /* 原文承载：markdown()/toMarkdown() 返回该原文。 */
    if (!self->m_markdown) self->m_markdown = XString_create();
    if (self->m_markdown) XString_assign_utf8(self->m_markdown, text);
    /* 显示降级：Markdown 解析未建，按纯文本写入并同步文档。 */
    self->m_bold = false;
    self->m_italic = false;
    self->m_underline = false;
    if (self->m_editor) XPlainTextEdit_setPlainText(self->m_editor, text);
#if XTEXTDOCUMENT_ON
    if (self->m_textDoc) XTextDocument_setPlainText(self->m_textDoc, text);
#endif
    XWidget_update((XWidget*)self);
}

char* XTextEdit_toMarkdown(const XTextEdit* self)
{
    const char* raw;
    size_t len;
    char* out;
    if (self && self->m_markdown) {
        raw = XString_toUtf8(self->m_markdown);
        if (raw && raw[0] != '\0') {
            len = XStrlen(raw);
            out = (char*)XMalloc_System(len + 1);
            if (out) XStrcpy(out, raw);
            return out;
        }
    }
    /* 降级：当前纯文本本身是合法 Markdown（与 toPlainText 同源）。 */
    return XPlainTextEdit_toPlainText(self ? self->m_editor : NULL);
}

const char* XTextEdit_markdown(const XTextEdit* self)
{
    if (!self || !self->m_markdown) return "";
    return XString_toUtf8(self->m_markdown);
}

void XTextEdit_setPlainText(XTextEdit* self, const char* text)
{
    const char* body = (text != NULL) ? text : "";
    if (!self) return;
    /* 纯文本写入：预览态退出回编辑态（编辑保持纯文本语义）。 */
    xte_exitPreview(self);
    /* 与 setText 纯文本分支同口径：复位字符格式、清除 Markdown 原文、
     * 富文本文档同步。 */
    self->m_bold = false;
    self->m_italic = false;
    self->m_underline = false;
    xte_resetMarkdown(self);
    if (self->m_editor) XPlainTextEdit_setPlainText(self->m_editor, body);
#if XTEXTDOCUMENT_ON
    if (self->m_textDoc) XTextDocument_setPlainText(self->m_textDoc, body);
#endif
    XWidget_update((XWidget*)self);
}

void XTextEdit_print(XTextEdit* self, void* printer) { (void)self; (void)printer; }
void* XTextEdit_createStandardContextMenu(XTextEdit* self) { (void)self; return NULL; }
void XTextEdit_setTextInteractionFlags(XTextEdit* self, int flags) { (void)self; (void)flags; }
int XTextEdit_textInteractionFlags(const XTextEdit* self) { (void)self; return 0; }
void XTextEdit_setOverwriteMode(XTextEdit* self, bool overwrite) { (void)self; (void)overwrite; }
bool XTextEdit_overwriteMode(const XTextEdit* self) { (void)self; return false; }
int XTextEdit_cursorRect_width(const XTextEdit* self) { (void)self; return 1; }
void XTextEdit_moveCursor_2(XTextEdit* self, int operation, int mode) { (void)self; (void)operation; (void)mode; }
bool XTextEdit_cursorCanPaste(const XTextEdit* self) { (void)self; return false; }
void* XTextEdit_currentCharFormatChanged_signal(XTextEdit* self)
{
    if (!self || !((XObject*)self)->m_signalSlot)
        return (void*)(size_t)XTextEdit_currentCharFormatChanged_signal;
    /* 真发射点：setCurrentCharFormat/mergeCurrentCharFormat 入口显式调用
       触发（对标 Qt 信号函数即发射点，同 linkHovered/linkActivated 定
       式）。Qt 载荷 const QTextCharFormat& 在平铺格式模型下以无参简化
       承载（对标 XAbstractSlider void 信号 args=NULL 定式）。 */
    XObject_emitSignal((XObject*)self,
                       (size_t)XTextEdit_currentCharFormatChanged_signal,
                       NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
    return (void*)(size_t)XTextEdit_currentCharFormatChanged_signal;
}

void* XTextEdit_linkHovered_signal(XTextEdit* self, const char* url)
{
    if (!self || !((XObject*)self)->m_signalSlot)
        return (void*)(size_t)XTextEdit_linkHovered_signal;
    /* 真实发射点在预览态鼠标移动处理（VX_textEdit_mouseMoveEvent）；
       本入口同样可被上层显式调用触发（对标 Qt 信号函数即发射点）。 */
    xte_emitLinkSignal(self,
                       (size_t)XTextEdit_linkHovered_signal,
                       url);
    return (void*)(size_t)XTextEdit_linkHovered_signal;
}

void* XTextEdit_linkActivated_signal(XTextEdit* self, const char* url)
{
    if (!self || !((XObject*)self)->m_signalSlot)
        return (void*)(size_t)XTextEdit_linkActivated_signal;
    /* 真实发射点在预览态鼠标释放处理（VX_textEdit_mouseReleaseEvent）。 */
    xte_emitLinkSignal(self,
                       (size_t)XTextEdit_linkActivated_signal,
                       url);
    return (void*)(size_t)XTextEdit_linkActivated_signal;
}

#endif /* XTEXTEDIT_ON */