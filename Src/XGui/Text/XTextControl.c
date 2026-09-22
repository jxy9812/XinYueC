/**
 * @file       XTextControl.c
 * @brief      XTextControl 私有文本控制器实现（对标 Qt 6.8
 *             QWidgetTextControl 的 API/功能/行为三层）。
 * @details    与同名头文件的公共 API 一一对应；平铺行模型（XTextControlLine
 *             定容缓冲行数组）承载 QTextDocument 的可见行为，实现细节见
 *             函数级 Doxygen 与头文件 @note（已知差异清单）。
 * @author     XinYueC 团队
 */

#include "XTextControl.h"
#include <string.h>
#include "XStringUtils.h"
#include "XMemory.h"
#include "XPainter.h"
#include "XVarList.h"
#include "XTextUtf8.h"
#include "XTextClipboard.h"

#if XINPUTMETHOD_ON
#include "XInputMethod.h"
#endif

#if XTEXTDOCUMENT_ON
#include "XTextDocument.h"
#endif

#if XMENU_ON
#include "XWidget.h"
#include "XMenu.h"
#include "XGuiApplication.h"
#include "XClipboard.h"
#include "XTextMenu.h"
#endif

/* ==================== 裁剪兜底（XGUI_ON=0 巡检，XPainter.c 先例） ======= */
/* 被裁剪子系统遗留使用点的纯值常量兜底：值口径与对应头文件枚举一致
   （XClipboardMode：Clipboard=0/Selection=1；XFocusReason：
   ActiveWindow=3/Popup=4——Mouse0/Tab1/Backtab2 之后）。类型与函数
   不做兜底，使用点按各自 X*_ON 分流。 */
#if !XCLIPBOARD_ON
#define XClipboardMode_Clipboard 0
#define XClipboardMode_Selection 1
#endif /* !XCLIPBOARD_ON */
#if !XWINDOWEVENT_ON
#define XFocusReason_ActiveWindow 3
#define XFocusReason_Popup 4
#endif /* !XWINDOWEVENT_ON */

#if XTEXTCONTROL_ON

/* ==================== 常量（对标 QApplication 缺省交互参数） ==================== */

#define XTC_CURSOR_FLASH_MS 1060        /**< 光标闪烁周期（对标默认 cursorFlashTime）。 */
#define XTC_TRIPLE_CLICK_MS 400         /**< 三击判定窗口（对标默认 doubleClickInterval）。 */
#define XTC_START_DRAG_DISTANCE 10      /**< 起拖距离（对标默认 startDragDistance）。 */
#define XTC_DEFAULT_LINE_HEIGHT 16      /**< 字体度量失败时的回退行高。 */
#define XTC_DEFAULT_BASELINE 13         /**< 字体度量失败时的回退基线偏移。 */
#define XTC_CAP_GROW 8                  /**< 数组容量步进。 */
#define XTC_MAX_DRAW_BOUNDS 64          /**< 行绘制分段边界上限（超出合并，绘制近似）。 */

/* ==================== 小工具 ==================== */

/** @brief 取行承载；越界返回 NULL。 */
static XTextControlLine* xtc_lineAt(const XTextControl* self, int index)
{
    if (!self || !self->m_lines || index < 0 || index >= self->m_lineCount)
        return NULL;
    return &self->m_lines[index];
}

/** @brief 行文本借用指针；越界返回 ""。 */
static const char* xtc_lineText(const XTextControl* self, int index)
{
    XTextControlLine* line = xtc_lineAt(self, index);
    return (line && line->data) ? line->data : "";
}

/** @brief 行长；越界返回 0。 */
static int xtc_lineLen(const XTextControl* self, int index)
{
    XTextControlLine* line = xtc_lineAt(self, index);
    return line ? line->len : 0;
}

/** @brief 文档长度（行内容 + 行间 '\n' 各 1 字节；对标 characterCount）。 */
static int xtc_documentLength(const XTextControl* self)
{
    int total = 0;
    int i;
    if (!self) return 0;
    for (i = 0; i < self->m_lineCount; ++i)
        total += self->m_lines[i].len;
    if (self->m_lineCount > 1)
        total += self->m_lineCount - 1;
    return total;
}

/** @brief 文档是否为空（对标 QTextDocument::isEmpty：单空行/零行）。 */
static bool xtc_isEmpty(const XTextControl* self)
{
    return !self || self->m_lineCount == 0 ||
           (self->m_lineCount == 1 && self->m_lines[0].len == 0);
}

/** @brief 绝对位置 → (行, 行内字节列)；位置钳位到文档末尾。 */
static void xtc_posToLineCol(const XTextControl* self, int pos, int* line, int* col)
{
    int i;
    int start = 0;
    if (line) *line = 0;
    if (col) *col = 0;
    if (!self || self->m_lineCount <= 0) return;
    if (pos < 0) pos = 0;
    for (i = 0; i < self->m_lineCount; ++i) {
        int len = self->m_lines[i].len;
        if (pos <= start + len || i == self->m_lineCount - 1) {
            if (line) *line = i;
            if (col) *col = pos - start > len ? len : pos - start;
            return;
        }
        start += len + 1;
    }
}

/** @brief (行, 列) → 绝对位置；行/列钳位。 */
static int xtc_lineColToPos(const XTextControl* self, int line, int col)
{
    int i;
    int start = 0;
    if (!self || self->m_lineCount <= 0) return 0;
    if (line < 0) line = 0;
    if (line >= self->m_lineCount) line = self->m_lineCount - 1;
    for (i = 0; i < line; ++i)
        start += self->m_lines[i].len + 1;
    {
        int len = self->m_lines[line].len;
        if (col < 0) col = 0;
        if (col > len) col = len;
        return start + col;
    }
}

/** @brief 是否正在 preedit 组合。 */
static bool xtc_isPreediting(const XTextControl* self)
{
    return self && self->m_preedit && self->m_preedit[0] != '\0';
}

/* ==================== preedit 视觉映射 ==================== */

/** @brief preedit 所在 (行, 列)；非组合态返回 false。 */
static bool xtc_preeditLineCol(const XTextControl* self, int* line, int* col)
{
    if (!xtc_isPreediting(self)) return false;
    xtc_posToLineCol(self, self->m_preeditPos, line, col);
    return true;
}

/** @brief preedit 字节长。 */
static int xtc_preeditLen(const XTextControl* self)
{
    return xtc_isPreediting(self) ? (int)XStrlen(self->m_preedit) : 0;
}

/** @brief 文档列 → 视觉列（组合行上考虑 preedit 占位）。 */
static int xtc_docColToVisualCol(const XTextControl* self, int line, int col)
{
    int pLine;
    int pCol;
    if (!xtc_preeditLineCol(self, &pLine, &pCol)) return col;
    if (line != pLine) return col;
    if (col >= pCol) return col + xtc_preeditLen(self);
    return col;
}

/** @brief 视觉列 → 文档列（命中测试逆映射；preedit 区间归并到插入点）。 */
static int xtc_visualColToDocCol(const XTextControl* self, int line, int col)
{
    int pLine;
    int pCol;
    int preLen;
    if (!xtc_preeditLineCol(self, &pLine, &pCol)) return col;
    if (line != pLine) return col;
    preLen = xtc_preeditLen(self);
    if (col <= pCol) return col;
    if (col <= pCol + preLen) return pCol;
    return col - preLen;
}

/* ==================== 软换行布局缓存（对标 QTextLayout 行内 wrap） ==== */

/** @brief 布局缓存失效（惰性重建；编辑/字体/宽度/模式变更时调用）。 */
static void xtc_invalidateLayout(XTextControl* self)
{
    if (self) self->m_layoutValid = false;
}

/** @brief 折行宽度（px）：仅 WidgetWidth 且 textWidth>0 且断行规则非
 *         NoWrap/ManualWrap 时生效（对标 QTextOption::NoWrap 关闭折行）；
 *         0 = 不折。 */
static int xtc_wrapWidthOf(const XTextControl* self)
{
    if (!self || self->m_lineWrapMode != (int)XTextControlLineWrap_WidgetWidth)
        return 0;
    if (self->m_wordWrapMode == (int)XTextControlWrap_NoWrap ||
        self->m_wordWrapMode == (int)XTextControlWrap_ManualWrap)
        return 0;
    return self->m_textWidth > 0 ? self->m_textWidth : 0;
}

/**
 * @brief      逻辑行"视觉文本"：preedit 组合行把组合串 splice 进文档行
 *             （IME 预编辑串计入布局，对标 Qt preeditArea 参与折行）。
 * @param      heap 输出堆缓冲（非组合行为 NULL，无需释放）。
 * @return     视觉文本借用/堆指针；长度 = 行长 + preedit 字节长。
 */
static const char* xtc_visualLineText(const XTextControl* self, int line,
                                      char** heap)
{
    int pLine;
    int pCol;
    *heap = NULL;
    const char* docText = xtc_lineText(self, line);
    int docLen = xtc_lineLen(self, line);
    if (!xtc_preeditLineCol(self, &pLine, &pCol) || pLine != line)
        return docText;
    {
        int preLen = xtc_preeditLen(self);
        if (pCol > docLen) pCol = docLen;
        *heap = (char*)XMalloc_System((size_t)docLen + (size_t)preLen + 1);
        if (!*heap) return docText;
        XMemcpy(*heap, docText, (size_t)pCol);
        XMemcpy(*heap + pCol, self->m_preedit, (size_t)preLen);
        XMemcpy(*heap + pCol + preLen, docText + pCol,
                (size_t)(docLen - pCol) + 1);
    }
    return *heap;
}

/** @brief 逻辑行视觉文本字节长（行长 + 组合行 preedit 占位）。 */
static int xtc_visualLineLen(const XTextControl* self, int line)
{
    int pLine;
    int pCol;
    int len = xtc_lineLen(self, line);
    if (xtc_preeditLineCol(self, &pLine, &pCol) && pLine == line)
        len += xtc_preeditLen(self);
    return len;
}

/** @brief 解码 s 处一个 UTF-8 码点；seq 输出字节长（与 XTextUtf8 同口径）。 */
static uint32_t xtc_decodeCp(const char* s, int remain, int* seq)
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

/** @brief CJK 码点判定（断行"逐字可断"类的简化 UAX#14 承载）。 */
static bool xtc_isCjkCp(uint32_t cp)
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
typedef enum XtcCpClass
{
    XTC_CP_SPACE = 0, /**< ASCII 空白。 */
    XTC_CP_CJK,       /**< CJK/全角（逐字可断）。 */
    XTC_CP_WORD,      /**< 词字符（字母数字下划线/其它非 CJK 文字）。 */
    XTC_CP_PUNCT      /**< 标点/符号。 */
} XtcCpClass;

/** @brief 码点断行类判定。 */
static int xtc_cpClass(uint32_t cp)
{
    if (cp == ' ' || cp == '\t' || cp == 0x0B || cp == 0x0C)
        return XTC_CP_SPACE;
    if (xtc_isCjkCp(cp)) return XTC_CP_CJK;
    if ((cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z') ||
        (cp >= '0' && cp <= '9') || cp == '_' || cp >= 0x80)
        return XTC_CP_WORD;
    return XTC_CP_PUNCT;
}

/** 行禁首字符（kinsoku：这些标点不落行首，对标中文排版禁则）。 */
static const uint32_t xtc_kinsokuBefore[] = {
    0x3001, 0x3002, 0x3009, 0x300B, 0x300D, 0x300F, 0x3011, 0x3015,
    0x3017, 0x3019, 0x301B, 0xFF01, 0xFF09, 0xFF0C, 0xFF1A, 0xFF1B,
    0xFF1F, 0xFF5D, 0x2019, 0x201D, 0x2026, 0x2014
};
/** 行禁尾字符（开括号/引号不落行尾）。 */
static const uint32_t xtc_kinsokuAfter[] = {
    0x3008, 0x300A, 0x300C, 0x300E, 0x3010, 0x3014, 0xFF08, 0xFF5B,
    0x2018, 0x201C
};

/** @brief 禁则查表。 */
static bool xtc_inCpTable(const uint32_t* table, int count, uint32_t cp)
{
    int i;
    for (i = 0; i < count; ++i)
        if (table[i] == cp) return true;
    return false;
}

/**
 * @brief      prev 与 cur 之间是否可断行（cur 落行首的断点判定）。
 * @details    对标 QTextOption::WrapMode：WordWrap 下 CJK 逐字可断、
 *             Latin 按词边界（空白后断）、长词由扫描兜底硬断；
 *             WrapAnywhere 逐字断；行首不留空白；CJK 禁则成对约束。
 */
static bool xtc_canBreakBefore(int mode, uint32_t prevCp, int prevClass,
                               uint32_t curCp, int curClass)
{
    if (curClass == XTC_CP_SPACE) return false; /* 行首不留空白（空格悬挂行尾）。 */
    if (mode == (int)XTextControlWrap_Anywhere) return true;
    if (xtc_inCpTable(xtc_kinsokuBefore,
                      (int)(sizeof(xtc_kinsokuBefore) / sizeof(uint32_t)),
                      curCp))
        return false;
    if (xtc_inCpTable(xtc_kinsokuAfter,
                      (int)(sizeof(xtc_kinsokuAfter) / sizeof(uint32_t)),
                      prevCp))
        return false;
    if (prevClass == XTC_CP_SPACE) return true;  /* 词界（空白之后）。 */
    if (curClass == XTC_CP_CJK) return true;     /* CJK 逐字可断。 */
    if (prevClass == XTC_CP_CJK) return true;    /* CJK 后接西文/标点可断。 */
    if (prevClass == XTC_CP_PUNCT) return true;  /* 逗号/分号等后可断。 */
    return false;                                /* 词内（WORD|WORD/标点粘前）。 */
}

/**
 * @brief      可视行入数组（容量不足倍增扩容）。
 * @details    全量重建与增量更新共用：arr/count/cap 既可以指向缓存本体
 *             （m_visualRows/m_visualCount/m_visualCap），也可以指向增量
 *             路径的暂存数组，保证两条路径产物逐条一致。
 */
static bool xtc_layoutPushRow(XTextControlVisualRow** arr, int* count,
                              int* cap, int line, int start, int len,
                              int width)
{
    if (*count >= *cap) {
        int newCap = *cap > 0 ? *cap * 2 : XTC_CAP_GROW;
        XTextControlVisualRow* grown = (XTextControlVisualRow*)XRealloc_System(
            *arr, (size_t)newCap * sizeof(XTextControlVisualRow));
        if (!grown) return false;
        *arr = grown;
        *cap = newCap;
    }
    (*arr)[*count].line = line;
    (*arr)[*count].start = start;
    (*arr)[*count].len = len;
    (*arr)[*count].width = width;
    ++*count;
    return true;
}

/**
 * @brief      单逻辑行断行扫描（宽度优先贪心，对标 QTextLayout 断行）。
 * @details    逐码点累宽；越界时优先回退到最近断点（词界/CJK 码点间），
 *             行内无断点则当前码点前硬断（长词超行宽，对标 Qt 长词
 *             hard-break）；行首码点恒被接纳（避免死循环）。行段写入
 *             arr/count/cap 指向的数组（全量/增量共用，见
 *             xtc_layoutPushRow）；任一段入队失败返回 false。
 */
static bool xtc_layoutLine(int wrapMode, const XFont* fontSrc,
                           XTextControlVisualRow** arr, int* count, int* cap,
                           int line, const char* text, int len, int wrapW)
{
    XFont font;
    int rowStart = 0;
    int x = 0;
    int off = 0;
    int lastBreak = -1;   /* 最近断点（视觉文本字节偏移，断在其前）。 */
    int lastBreakW = 0;   /* 断点前累计像素宽。 */
    uint32_t prevCp = 0;
    int prevClass = -1;
    int mode = wrapMode;
    /* 只读浅拷贝（与源字体共享字符串指针，禁止 deinit）。 */
    XMemcpy(&font, fontSrc, sizeof(XFont));
    while (off < len) {
        uint32_t cp;
        int seq;
        int cls;
        int w;
        cp = xtc_decodeCp(text + off, len - off, &seq);
        cls = xtc_cpClass(cp);
        w = XPainter_textWidthRange(&font, text, off, off + seq);
        if (w < 0) w = 0;
        /* 先记录本码点前的断点机会，再判断溢出：溢出发生时优先断在
           当前码点前（贪心最满行，对标 Qt 断行取最后可行断点）。 */
        if (prevClass >= 0 &&
            xtc_canBreakBefore(mode, prevCp, prevClass, cp, cls)) {
            lastBreak = off;
            lastBreakW = x;
        }
        if (x + w > wrapW && off > rowStart) {
            /* 越界：优先词界/码点断点，否则硬断当前码点前。 */
            int soft = lastBreak > rowStart;
            int cut = soft ? lastBreak : off;
            int cutW = soft ? lastBreakW : x;
            if (!xtc_layoutPushRow(arr, count, cap, line, rowStart,
                                   cut - rowStart, cutW))
                return false;
            rowStart = cut;
            /* 软断时 [cut, off) 已属新行：其像素宽结转入新行累计值
               （否则新行宽度漏计导致越界残留）；prevCp/prevClass 亦
               保持（off 的前码点未变）。硬断则新行自 off 重计。 */
            x = soft ? (x - lastBreakW) : 0;
            lastBreak = -1;
            lastBreakW = 0;
            if (!soft) prevClass = -1; /* 硬断新行首无前字符。 */
            continue;
        }
        x += w;
        off += seq;
        prevCp = cp;
        prevClass = cls;
    }
    return xtc_layoutPushRow(arr, count, cap, line, rowStart, len - rowStart,
                             x);
}

/** @brief 重建布局缓存（逻辑行 → 可视行平铺；编辑/度量变更后惰性调用）。 */
static void xtc_rebuildLayout(XTextControl* self)
{
    int i;
    int wrapW = xtc_wrapWidthOf(self);
    self->m_visualCount = 0;
    for (i = 0; i < self->m_lineCount; ++i) {
        char* heap = NULL;
        const char* text = xtc_visualLineText(self, i, &heap);
        int len = xtc_visualLineLen(self, i);
        if (wrapW <= 0) {
            /* 不折行：可视行与逻辑行 1:1（对标 NoWrap）。 */
            XFont font;
            int w;
            XMemcpy(&font, &self->m_font, sizeof(XFont));
            w = XPainter_textWidthRange(&font, text, 0, len);
            if (!xtc_layoutPushRow(&self->m_visualRows, &self->m_visualCount,
                                   &self->m_visualCap, i, 0, len,
                                   w > 0 ? w : 0))
                break;
        } else {
            if (!xtc_layoutLine(self->m_wordWrapMode, &self->m_font,
                                &self->m_visualRows, &self->m_visualCount,
                                &self->m_visualCap, i, text, len, wrapW))
                break;
        }
        if (heap) XFree_System(heap);
    }
    self->m_layoutValid = true;
}

/** @brief 确保布局缓存有效（全部几何/绘制查询的统一入口）。 */
static void xtc_ensureLayout(XTextControl* self)
{
    if (!self || self->m_layoutValid) return;
    xtc_rebuildLayout(self);
}

/** @brief 可视行数（确保布局后读取；0 逻辑行时为 0）。 */
static int xtc_visualCountOf(XTextControl* self)
{
    if (!self || self->m_lineCount <= 0) return 0;
    xtc_ensureLayout(self);
    return self->m_visualCount > 0 ? self->m_visualCount : 0;
}

/** @brief 内容高度（可视行数 x 行高；滚动范围/文档尺寸同源口径）。 */
static int xtc_contentHeight(XTextControl* self)
{
    return xtc_visualCountOf(self) * self->m_lineHeight;
}

/** @brief 首个 line >= L 的可视行下标（行序单调，二分）。 */
static int xtc_rowLowerBound(const XTextControl* self, int line)
{
    int lo = 0;
    int hi = self->m_visualCount;
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (self->m_visualRows[mid].line < line)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

/**
 * @brief      增量布局：单个逻辑行内容变化时仅重算该行的可视行段。
 * @details    前置：编辑前缓存有效（由调用方在整体失效前捕获），且变更
 *             仅落在单个逻辑行（编辑串不含 '\n'，逻辑行数不变）。做法：
 *             按当前内容（含 preedit 视觉串，
 *             与全量重建同一套 xtc_visualLineText/xtc_layoutLine 度量）
 *             重算该行新可视行段，整体替换旧可视行区间 [first,last)
 *             （相当于"保留前缀 + 变更行段 + 保留后缀"三段中的前/后缀
 *             原样保留、变更段重算）。start 为逻辑行内相对字节
 *             （XTextControlVisualRow 的承载口径），其它逻辑行的
 *             (line,start,len,width) 不受单行编辑影响，无需平移；本次
 *             编辑造成的字节差体现在替换段自身的 len/width 重算里。
 *             护栏：核对缓存覆盖行与当前文档行数一致（0 与 lineCount-1），
 *             setText 等绕过 rawInsert/rawRemove 直改行模型的场景回退
 *             全量重建。任一步失败保持缓存失效（惰性全量，正确性优先）。
 * @return     true = 缓存已增量更新并置有效；false = 保持失效。
 */
static bool xtc_incrementalLayoutLine(XTextControl* self, int line)
{
    XTextControlVisualRow* rows = NULL; /* 新行段暂存（独立扩容数组）。 */
    int count = 0;
    int cap = 0;
    int wrapW;
    int first;
    int last;
    int diff;
    char* heap = NULL;
    const char* text;
    int len;
    bool ok = false;

    if (!self) return false;
    /* 注意：进入本函数时 m_layoutValid 恒为 false（调用方 rawInsert/
       rawRemove 已先整体失效）；"编辑前缓存有效"由调用方以局部快照
       捕获并自行判定，此处不再重复检查。 */
    if (line < 0 || line >= self->m_lineCount) return false;
    if (self->m_visualCount <= 0 || !self->m_visualRows) return false;
    /* 护栏：缓存须恰好覆盖 0..lineCount-1 行（行序单调 + 首尾行号核对）。 */
    if (self->m_visualRows[0].line != 0 ||
        self->m_visualRows[self->m_visualCount - 1].line !=
            self->m_lineCount - 1)
        return false;
    wrapW = xtc_wrapWidthOf(self);
    text = xtc_visualLineText(self, line, &heap);
    len = xtc_visualLineLen(self, line);
    if (wrapW <= 0) {
        /* 不折行：该行可视行恒 1 条（与全量重建同口径）。 */
        XFont font;
        int w;
        XMemcpy(&font, &self->m_font, sizeof(XFont));
        w = XPainter_textWidthRange(&font, text, 0, len);
        if (!xtc_layoutPushRow(&rows, &count, &cap, line, 0, len,
                               w > 0 ? w : 0))
            goto done;
    } else {
        if (!xtc_layoutLine(self->m_wordWrapMode, &self->m_font, &rows,
                            &count, &cap, line, text, len, wrapW))
            goto done;
    }
    /* 定位旧可视行区间 [first, last) 并原位拼接。 */
    first = xtc_rowLowerBound(self, line);
    last = first;
    while (last < self->m_visualCount &&
           self->m_visualRows[last].line == line)
        ++last;
    diff = count - (last - first);
    if (diff > 0) {
        /* 行段变多：先一次性扩容再搬移（新行段变宽，尾部让位）。 */
        if (self->m_visualCount + diff > self->m_visualCap) {
            int newCap = self->m_visualCap > 0 ? self->m_visualCap
                                               : XTC_CAP_GROW;
            XTextControlVisualRow* grown;
            while (newCap < self->m_visualCount + diff) newCap *= 2;
            grown = (XTextControlVisualRow*)XRealloc_System(
                self->m_visualRows,
                (size_t)newCap * sizeof(XTextControlVisualRow));
            if (!grown) goto done;
            self->m_visualRows = grown;
            self->m_visualCap = newCap;
        }
    }
    if (diff != 0) {
        /* 尾段 [last, visualCount) 搬到新行段之后：目的地 = first + count
           （新行段宽度），增减两向通用；memmove 容忍区间重叠。 */
        XMemmove(&self->m_visualRows[first + count], &self->m_visualRows[last],
                 (size_t)(self->m_visualCount - last) *
                     sizeof(XTextControlVisualRow));
    }
    XMemcpy(&self->m_visualRows[first], rows,
            (size_t)count * sizeof(XTextControlVisualRow));
    self->m_visualCount += diff;
    self->m_layoutValid = true;
    ok = true;
done:
    if (heap) XFree_System(heap);
    if (rows) XFree_System(rows);
    return ok;
}

/**
 * @brief      绝对位置 → (可视行, 可视行内字节列)。
 * @details    位置 → 逻辑 (行,列) → preedit 视觉列 → 该逻辑行可视行段内
 *             查找归属行（软断点处光标归属前行行尾，对标 Qt）。
 */
static void xtc_posToVisualRowCol(XTextControl* self, int pos, int* outRow,
                                  int* outCol)
{
    int line;
    int col;
    int vcol;
    int row;
    if (outRow) *outRow = 0;
    if (outCol) *outCol = 0;
    if (!self || self->m_visualCount <= 0) return;
    xtc_posToLineCol(self, pos, &line, &col);
    vcol = xtc_docColToVisualCol(self, line, col);
    row = xtc_rowLowerBound(self, line);
    if (row >= self->m_visualCount) row = self->m_visualCount - 1;
    while (row + 1 < self->m_visualCount &&
           self->m_visualRows[row].line == line &&
           vcol > self->m_visualRows[row].start + self->m_visualRows[row].len)
        ++row;
    if (self->m_visualRows[row].line != line) {
        /* 位置钳到行外（越界入参兜底）：取该行首可视行。 */
        row = xtc_rowLowerBound(self, line);
        if (row >= self->m_visualCount) row = self->m_visualCount - 1;
        vcol = self->m_visualRows[row].start;
    }
    /* 软断点字节归属歧义：Home 触发的光标边沿把行首字节解析到下一
       可视行行首（对标 Qt 光标边沿跟踪）。 */
    if (pos == self->m_cursorPosition && self->m_cursorAtRowStart &&
        vcol == self->m_visualRows[row].start + self->m_visualRows[row].len &&
        vcol < xtc_visualLineLen(self, line) &&
        row + 1 < self->m_visualCount)
        ++row;
    if (outRow) *outRow = row;
    if (outCol) *outCol = vcol - self->m_visualRows[row].start;
}

/**
 * @brief      可视行 + X 像素 → 行内字节列（对标 hitTest 列扫描，行内钳位）。
 */
static int xtc_rowXToCol(XTextControl* self, int row, int x)
{
    XFont font;
    const char* text;
    char* heap = NULL;
    int line;
    int from;
    int to;
    int off;
    if (!self || row < 0 || row >= self->m_visualCount) return 0;
    line = self->m_visualRows[row].line;
    from = self->m_visualRows[row].start;
    to = from + self->m_visualRows[row].len;
    text = xtc_visualLineText(self, line, &heap);
    /* 只读浅拷贝（与 self->m_font 共享字符串指针，禁止 deinit）。 */
    XMemcpy(&font, &self->m_font, sizeof(XFont));
    off = from;
    if (x > 0) {
        while (off < to) {
            int seq = XTextUtf8_seqLen(text + off, to - off);
            int w = XPainter_textWidthRange(&font, text, off, off + seq);
            if (w < 0) w = 0;
            if (x < w) break;
            x -= w;
            off += seq;
        }
    }
    if (heap) XFree_System(heap);
    return off - self->m_visualRows[row].start;
}

/**
 * @brief      (可视行, 行内字节列) → 绝对位置（经视觉列→文档列逆映射）。
 */
static int xtc_visualRowColToPos(XTextControl* self, int row, int col)
{
    int line;
    int vcol;
    int docCol;
    int len;
    if (!self || row < 0 || row >= self->m_visualCount) return 0;
    line = self->m_visualRows[row].line;
    len = self->m_visualRows[row].len;
    if (col < 0) col = 0;
    if (col > len) col = len;
    vcol = self->m_visualRows[row].start + col;
    docCol = xtc_visualColToDocCol(self, line, vcol);
    return xtc_lineColToPos(self, line, docCol);
}

/** @brief 是否有选区。 */
static bool xtc_hasSelection(const XTextControl* self)
{
    return self && self->m_cursorPosition != self->m_cursorAnchor;
}

/** @brief 选区起点。 */
static int xtc_selectionStart(const XTextControl* self)
{
    return self->m_cursorPosition < self->m_cursorAnchor
               ? self->m_cursorPosition : self->m_cursorAnchor;
}

/** @brief 选区终点。 */
static int xtc_selectionEnd(const XTextControl* self)
{
    return self->m_cursorPosition < self->m_cursorAnchor
               ? self->m_cursorAnchor : self->m_cursorPosition;
}

/** @brief 矩形并集。 */
static void xtc_rectUnion(XRect* r, const XRect* other)
{
    int x1 = r->x + r->width < other->x + other->width
                 ? other->x + other->width : r->x + r->width;
    int y1 = r->y + r->height < other->y + other->height
                 ? other->y + other->height : r->y + r->height;
    if (other->x < r->x) r->x = other->x;
    if (other->y < r->y) r->y = other->y;
    r->width = x1 - r->x;
    r->height = y1 - r->y;
}

/** @brief 调色板取色（ARGB32；调色板模块裁剪时回退黑色）。 */
static uint32_t xtc_paletteColor(const XTextControl* self, int role)
{
#if XPALETTE_ON
    XColor c = XPalette_color((XPalette*)&self->m_palette,
                              XPaletteColorGroup_Active,
                              (XPaletteColorRole)role);
    return XColor_rgba(&c);
#else
    (void)self; (void)role;
    return 0xFF000000u;
#endif
}

/** @brief 堆分配拷贝（失败返回 NULL）。 */
static char* xtc_strdupN(const char* src, int len)
{
    char* out;
    if (len < 0) len = src ? (int)XStrlen(src) : 0;
    out = (char*)XMalloc_System((size_t)len + 1);
    if (!out) return NULL;
    if (len > 0 && src) XMemcpy(out, src, (size_t)len);
    out[len] = '\0';
    return out;
}

/** @brief ASCII 小写（查找大小写折叠的平铺近似）。 */
static char xtc_lowerChar(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

/** @brief 词字符判定：ASCII 字母数字/下划线或任意非 ASCII 字节。 */
static bool xtc_isWordByte(unsigned char c)
{
    if (c >= 0x80) return true;
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
        (c >= '0' && c <= '9') || c == '_')
        return true;
    return false;
}

/* ==================== 信号发射（XObject_emitSignal，真发射） ==================== */

/** @brief 无参信号发射。 */
static void xtc_emitVoid(XTextControl* self, void* (*signal)(XTextControl*))
{
    XVarList* args;
    if (!self) return;
    args = XVarList_create(0);
    if (!args) return;
    if (((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, (size_t)signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

/** @brief bool 单参信号发射。 */
static void xtc_emitBool(XTextControl* self, void* (*signal)(XTextControl*, bool),
                         bool value)
{
    XVarList* args;
    if (!self) return;
    args = XVarList_Create(XVar(bool, value));
    if (!args) return;
    if (((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, (size_t)signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

/** @brief int 单参信号发射。 */
static void xtc_emitInt(XTextControl* self, void* (*signal)(XTextControl*, int),
                        int value)
{
    XVarList* args;
    if (!self) return;
    args = XVarList_Create(XVar(int, value));
    if (!args) return;
    if (((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, (size_t)signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

/** @brief 字符串单参信号发射（拷贝进 XVarList，随槽调用后释放）。 */
static void xtc_emitString(XTextControl* self,
                           void* (*signal)(XTextControl*, const char*),
                           const char* value)
{
    XVarList* args;
    char* copy;
    if (!self) return;
    copy = xtc_strdupN(value, -1);
    if (!copy) return;
    args = XVarList_Create(XVar(char*, copy));
    if (!args) {
        XFree_System(copy);
        return;
    }
    if (((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, (size_t)signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
        XFree_System(copy);
    }
}

/** @brief 矩形单参信号发射（NULL 视为全文档矩形）。 */
static void xtc_emitRect(XTextControl* self,
                         void* (*signal)(XTextControl*, const XRect*),
                         const XRect* rect)
{
    XVarList* args;
    XRect area;
    if (!self) return;
    if (rect) {
        area = *rect;
    } else {
        int width = self->m_textWidth > 0 ? self->m_textWidth : 0;
        /* 全文档矩形高度按可视行数（软换行计入）。 */
        XRect_init(&area, 0, 0, width, xtc_contentHeight(self));
    }
    args = XVarList_Create(XVar(XRect, area));
    if (!args) return;
    if (((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, (size_t)signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

/** @brief 尺寸单参信号发射（NULL 视为当前文档尺寸）。 */
static void xtc_emitSize(XTextControl* self,
                         void* (*signal)(XTextControl*, const XSize*),
                         const XSize* size)
{
    XVarList* args;
    XSize value;
    if (!self) return;
    if (size) {
        value = *size;
    } else {
        value = XTextControl_size(self);
    }
    args = XVarList_Create(XVar(XSize, value));
    if (!args) return;
    if (((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, (size_t)signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

/* ==================== 通知辅助（对标 Private 通知路径） ==================== */

/** @brief 光标矩形 ±4 扩展（对标 cursorRectPlusUnicodeDirectionMarkers）。 */
static XRect xtc_cursorRepaintRect(const XTextControl* self)
{
    XRect r = XTextControl_cursorRect(self);
    r.x -= 4;
    r.width += 8;
    return r;
}

/** @brief 光标重绘请求（对标 repaintCursor）。 */
static void xtc_repaintCursor(XTextControl* self)
{
    XRect r = xtc_cursorRepaintRect(self);
    xtc_emitRect(self, XTextControl_updateRequest_signal, &r);
}

/**
 * @brief      选区变化通知（对标 QWidgetTextControlPrivate::selectionChanged）。
 * @details    force 时无条件发射 selectionChanged；否则按位置/锚点去重，
 *             选区有无翻转时发射 copyAvailable；收尾发射 microFocusChanged
 *             并记录 lastSelection。
 */
static void xtc_notifySelection(XTextControl* self, bool force)
{
    bool hadSelection;
    bool stateChange;
    if (!self) return;
    if (force)
        xtc_emitVoid(self, XTextControl_selectionChanged_signal);
    if (self->m_cursorPosition == self->m_lastSelPosition &&
        self->m_cursorAnchor == self->m_lastSelAnchor)
        return;
    hadSelection = (self->m_lastSelPosition != self->m_lastSelAnchor);
    stateChange = xtc_hasSelection(self) != hadSelection;
    if (stateChange)
        xtc_emitBool(self, XTextControl_copyAvailable_signal,
                     xtc_hasSelection(self));
    if (!force && (stateChange ||
                   (xtc_hasSelection(self) &&
                    (self->m_cursorPosition != self->m_lastSelPosition ||
                     self->m_cursorAnchor != self->m_lastSelAnchor))))
        xtc_emitVoid(self, XTextControl_selectionChanged_signal);
    xtc_emitVoid(self, XTextControl_microFocusChanged_signal);
    self->m_lastSelPosition = self->m_cursorPosition;
    self->m_lastSelAnchor = self->m_cursorAnchor;
}

/**
 * @brief      旧/新选区重绘请求（对标 repaintOldAndNewSelection 的平铺并集）。
 */
static void xtc_repaintOldAndNewSelection(XTextControl* self, int oldPos,
                                          int oldAnchor)
{
    XRect r;
    if (!self) return;
    r = XTextControl_cursorRect(self);
    if (oldPos != oldAnchor) {
        XRect oldSel = XTextControl_selectionRectAt(self, oldPos, oldAnchor);
        xtc_rectUnion(&r, &oldSel);
    }
    if (xtc_hasSelection(self)) {
        XRect sel = XTextControl_selectionRect(self);
        xtc_rectUnion(&r, &sel);
    }
    xtc_emitRect(self, XTextControl_updateRequest_signal, &r);
}

/**
 * @brief      当前字符格式通知（对标 updateCurrentCharFormat；位值去重）。
 */
static void xtc_notifyCharFormat(XTextControl* self)
{
    if (!self || self->m_charFormat == self->m_lastCharFormat) return;
    self->m_lastCharFormat = self->m_charFormat;
    xtc_emitInt(self, XTextControl_currentCharFormatChanged_signal,
                self->m_charFormat);
    xtc_emitVoid(self, XTextControl_microFocusChanged_signal);
}

/** @brief 光标位置变化通知（对标 _q_emitCursorPosChanged）。 */
static void xtc_notifyCursorPosition(XTextControl* self, int oldPos)
{
    if (!self || self->m_cursorPosition == oldPos) return;
    xtc_emitVoid(self, XTextControl_cursorPositionChanged_signal);
    xtc_emitVoid(self, XTextControl_microFocusChanged_signal);
}

/** @brief 设置光标可见开关（对标 setCursorVisible；联动闪烁定时器）。 */
static void xtc_setCursorVisible(XTextControl* self, bool visible)
{
    if (!self || self->m_cursorVisible == visible) return;
    self->m_cursorVisible = visible;
    if (self->m_cursorBlinkTimer != XTIMER_INVALID_ID) {
        XObject_killTimer((XObject*)self, self->m_cursorBlinkTimer);
        self->m_cursorBlinkTimer = XTIMER_INVALID_ID;
    }
    if (visible) {
        self->m_cursorBlinkTimer = XObject_startTimer_ms(
            (XObject*)self, XTC_CURSOR_FLASH_MS / 2, XTimerType_CoarseTimer);
    }
    self->m_cursorOn = visible;
    xtc_repaintCursor(self);
}

/** @brief 当前选区矩形（内容坐标；跨可视行逐行条带并集，含行内部分选中）。 */
static XRect xtc_selectionRectImpl(const XTextControl* self, int position,
                                   int anchor)
{
    XRect r;
    XFont font;
    int start;
    int end;
    int startRow;
    int startCol;
    int endRow;
    int endCol;
    int i;
    char* heap = NULL;
    int builtLine = -1;
    const char* builtText = NULL;
    if (!self) {
        XRect_init(&r, 0, 0, 0, 0);
        return r;
    }
    if (position == anchor)
        return XTextControl_cursorRectAt(self, position);
    start = position < anchor ? position : anchor;
    end = position < anchor ? anchor : position;
    xtc_ensureLayout((XTextControl*)self);
    xtc_posToVisualRowCol((XTextControl*)self, start, &startRow, &startCol);
    xtc_posToVisualRowCol((XTextControl*)self, end, &endRow, &endCol);
    /* 只读浅拷贝:与 self->m_font 共享 m_family/m_styleName,使用后禁止
       deinit(会释放控制器自有字符串,下一读点即踩悬垂指针)。 */
    XMemcpy(&font, &self->m_font, sizeof(XFont));
    XRect_init(&r, 0, 0, 0, 0);
    for (i = startRow; i <= endRow && i < self->m_visualCount; ++i) {
        const XTextControlVisualRow* row = &self->m_visualRows[i];
        const char* text;
        int from = (i == startRow) ? startCol : 0;
        int to = (i == endRow) ? endCol : row->len;
        XRect lineRect;
        if (row->line != builtLine) {
            if (heap) {
                XFree_System(heap);
                heap = NULL;
            }
            builtText = xtc_visualLineText(self, row->line, &heap);
            builtLine = row->line;
        }
        text = builtText;
        if (to > row->len) to = row->len;
        XRect_init(&lineRect,
                   XPainter_textWidthRange(&font, text, row->start,
                                           row->start + from),
                   i * self->m_lineHeight,
                   XPainter_textWidthRange(&font, text, row->start + from,
                                           row->start + to),
                   self->m_lineHeight);
        if (i == startRow)
            r = lineRect;
        else
            xtc_rectUnion(&r, &lineRect);
    }
    if (heap) XFree_System(heap);
    /* 浅拷贝不拥有堆串:无需释放(所有权在 self->m_font)。 */
    /* 对标 Qt：有效选区矩形 ±1 外扩。 */
    r.x -= 1;
    r.y -= 1;
    r.width += 2;
    r.height += 2;
    return r;
}

/* ==================== 撤销/重做（命令差量栈 + 组） ==================== */

/** @brief 释放单条命令的堆内文本。 */
static void xtc_commandClear(XTextControlUndoCommand* cmd)
{
    if (cmd->removed) {
        XFree_System(cmd->removed);
        cmd->removed = NULL;
    }
    if (cmd->inserted) {
        XFree_System(cmd->inserted);
        cmd->inserted = NULL;
    }
}

/** @brief 清空并释放命令栈。 */
static void xtc_commandStackClear(XTextControlUndoCommand** stack, int* count,
                                  int* cap)
{
    int i;
    if (*stack) {
        for (i = 0; i < *count; ++i)
            xtc_commandClear(&(*stack)[i]);
        XFree_System(*stack);
    }
    *stack = NULL;
    *count = 0;
    *cap = 0;
}

/** @brief 命令入栈（容量不足按步进扩容；失败返回 false）。 */
static bool xtc_commandPush(XTextControlUndoCommand** stack, int* count,
                            int* cap, const XTextControlUndoCommand* cmd)
{
    if (*count >= *cap) {
        int newCap = *cap + XTC_CAP_GROW;
        XTextControlUndoCommand* grown = (XTextControlUndoCommand*)XRealloc_System(
            *stack, (size_t)newCap * sizeof(XTextControlUndoCommand));
        if (!grown) return false;
        *stack = grown;
        *cap = newCap;
    }
    (*stack)[*count] = *cmd;
    ++(*count);
    return true;
}

/** @brief 通知 undo/redo 可用态（翻转时发射，对标 forward 信号）。 */
static void xtc_notifyUndoRedo(XTextControl* self)
{
    bool canUndo;
    bool canRedo;
    if (!self) return;
    canUndo = self->m_undoEnabled && self->m_undoCount > 0;
    canRedo = self->m_undoEnabled && self->m_redoCount > 0;
    if (canUndo != self->m_undoAvailable) {
        self->m_undoAvailable = canUndo;
        xtc_emitBool(self, XTextControl_undoAvailable_signal, canUndo);
    }
    if (canRedo != self->m_redoAvailable) {
        self->m_redoAvailable = canRedo;
        xtc_emitBool(self, XTextControl_redoAvailable_signal, canRedo);
    }
}

/** @brief 修改标志置位（翻转时发射 modificationChanged）。 */
static void xtc_setModifiedNotify(XTextControl* self, bool modified)
{
    if (!self || self->m_modified == modified) return;
    self->m_modified = modified;
    xtc_emitBool(self, XTextControl_modificationChanged_signal, modified);
}

/**
 * @brief      新编辑前截断重做栈（对标 Qt QUndoStack::push 的截断语义：
 *             undo 后产生的新命令使既有 redo 段全部过期——redo 段的
 *             pos/文本基于旧文档状态，重放会按过期位置错位修改，直接
 *             损坏文档）。
 * @details    逐条 xtc_commandClear 释放 removed/inserted 堆串防泄漏；
 *             仅清零计数、保留数组容量（undo→键入→undo 高频交替场景
 *             避免反复释放/重分配）；计数翻转后经 xtc_notifyUndoRedo
 *             发 redoAvailable(false)。
 */
static void xtc_truncateRedo(XTextControl* self)
{
    int i;
    if (!self || self->m_redoCount <= 0) return;
    for (i = 0; i < self->m_redoCount; ++i)
        xtc_commandClear(&self->m_redoStack[i]);
    self->m_redoCount = 0;
    xtc_notifyUndoRedo(self);
}

/**
 * @brief      记录一条撤销命令（自动分组时与栈顶相邻命令合并，对标
 *             QTextDocument 的键入/删除分组）。
 * @details    合并规则：同为纯插入且位置相接（pos == 上条 pos + 插入长）
 *             → 追加文本；同为纯删除且位置相接（向前/向后两个方向）
 *             → 合并删除串。显式组命令（group != 0）不与任何命令合并。
 *             P0 根因修复：所有新命令记录路径（含合并路径——合并等同
 *             一次新编辑）先截断重做栈；旧实现只压 undo 栈不清 redo
 *             栈，undo→输入→redo 时 redo() 按旧位置重放过期命令损坏
 *             文档。undo 栈唯一写点即本函数（undo/redo 搬移除外），
 *             editInsert/editRemove 及其上游（键入/粘贴/预编辑提交）
 *             全部经此收口。
 */
static void xtc_recordCommand(XTextControl* self, int pos, const char* removed,
                              const char* inserted, int group)
{
    XTextControlUndoCommand cmd;
    if (!self || !self->m_undoEnabled) return;
    xtc_truncateRedo(self);
    if (self->m_undoCount > 0 && group == 0) {
        XTextControlUndoCommand* last = &self->m_undoStack[self->m_undoCount - 1];
        if (last->group == 0 && last->inserted && !last->removed &&
            inserted && !removed) {
            int lastLen = (int)XStrlen(last->inserted);
            int addLen = (int)XStrlen(inserted);
            if (pos == last->pos + lastLen) {
                /* 分配与拷贝分离：strdupN(len=合并长) 会从旧串越界读
                   （ASan 基线扫查抓到的堆越界）。 */
                char* merged = (char*)XMalloc_System(
                    (size_t)lastLen + (size_t)addLen + 1);
                if (merged) {
                    XMemcpy(merged, last->inserted, (size_t)lastLen);
                    XMemcpy(merged + lastLen, inserted, (size_t)addLen);
                    merged[lastLen + addLen] = '\0';
                    XFree_System(last->inserted);
                    last->inserted = merged;
                }
                return;
            }
        }
        if (last->group == 0 && last->removed && !last->inserted &&
            removed && !inserted) {
            int lastLen = (int)XStrlen(last->removed);
            int remLen = (int)XStrlen(removed);
            /* 向后连续删除（退格）：新命令在旧命令左侧。 */
            if (pos + remLen == last->pos) {
                char* merged = (char*)XMalloc_System(
                    (size_t)remLen + (size_t)lastLen + 1);
                if (merged) {
                    XMemcpy(merged, removed, (size_t)remLen);
                    XMemcpy(merged + remLen, last->removed, (size_t)lastLen);
                    merged[remLen + lastLen] = '\0';
                    XFree_System(last->removed);
                    last->removed = merged;
                    last->pos = pos;
                }
                return;
            }
            /* 向前连续删除（Delete）：新命令在旧命令右侧。 */
            if (last->pos + lastLen == pos) {
                char* merged = (char*)XMalloc_System(
                    (size_t)remLen + (size_t)lastLen + 1);
                if (merged) {
                    XMemcpy(merged, last->removed, (size_t)lastLen);
                    XMemcpy(merged + lastLen, removed, (size_t)remLen);
                    merged[lastLen + remLen] = '\0';
                    XFree_System(last->removed);
                    last->removed = merged;
                }
                return;
            }
        }
    }
    cmd.pos = pos;
    cmd.removed = removed ? xtc_strdupN(removed, -1) : NULL;
    cmd.inserted = inserted ? xtc_strdupN(inserted, -1) : NULL;
    cmd.group = group;
    if ((removed && !cmd.removed) || (inserted && !cmd.inserted)) {
        xtc_commandClear(&cmd);
        return;
    }
    if (!xtc_commandPush(&self->m_undoStack, &self->m_undoCount,
                         &self->m_undoCap, &cmd)) {
        xtc_commandClear(&cmd);
        return;
    }
    /* 压栈即刷新撤销/重做可用态：纯 API 编辑（无编辑块包裹，group=0）
       不经过 groupEnd，缺失本通知会让 canUndo 哨兵永不翻转。 */
    xtc_notifyUndoRedo(self);
}

/** @brief 起始一层编辑块；返回组号（嵌套时沿用最外层组号）。 */
static int xtc_groupBegin(XTextControl* self)
{
    if (!self) return 0;
    if (self->m_editBlockDepth == 0)
        self->m_groupCounter = self->m_groupCounter < 0x7FFFFFF0
                                   ? self->m_groupCounter + 1 : 1;
    ++self->m_editBlockDepth;
    return self->m_groupCounter;
}

/** @brief 结束一层编辑块（最外层收尾时刷新撤销/重做可用通知）。 */
static void xtc_groupEnd(XTextControl* self)
{
    if (!self || self->m_editBlockDepth <= 0) return;
    --self->m_editBlockDepth;
    xtc_notifyUndoRedo(self);
}

/* ==================== 文档裸变更（无撤销/无信号） ==================== */

/**
 * @brief      在绝对位置 pos 插入 UTF-8 文本（可含 '\n'，按需拆行/建行）。
 * @details    布局失效策略：编辑前缓存有效且插入串不含 '\n'（单逻辑行
 *             编辑，绝大多数键入场景）时，增量重算该逻辑行的可视行段；
 *             含 '\n'（跨行/建行）或缓存本已失效时仅整体失效，惰性全量
 *             重建（正确性优先）。中途分配失败经早退跳过增量收尾，同样
 *             落入全量重建。
 */
static void xtc_rawInsert(XTextControl* self, int pos, const char* utf8)
{
    int line;
    int col;
    bool layoutWasValid;
    bool multiline = false;
    const char* src;
    XTextControlLine* target;
    if (!self || !utf8 || !utf8[0]) return;
    layoutWasValid = self->m_layoutValid; /* 增量前提：编辑前缓存有效。 */
    xtc_invalidateLayout(self); /* 文档变更 → 可视行缓存失效。 */
    if (self->m_lineCount <= 0) {
        /* 空文档兜底：保证至少一行。 */
        if (self->m_lineCap <= 0) {
            self->m_lineCap = XTC_CAP_GROW;
            self->m_lines = (XTextControlLine*)XMalloc_System(
                (size_t)self->m_lineCap * sizeof(XTextControlLine));
            if (!self->m_lines) {
                self->m_lineCap = 0;
                return;
            }
        }
        XMemset(&self->m_lines[0], 0, sizeof(XTextControlLine));
        self->m_lineCount = 1;
    }
    xtc_posToLineCol(self, pos, &line, &col);
    src = utf8;
    target = &self->m_lines[line];
    if (col > target->len) col = target->len;
    for (;;) {
        const char* nl = XStrchr(src, 0x0A);
        int segLen = nl ? (int)(nl - src) : (int)XStrlen(src);
        if (segLen > 0) {
            int need = target->len + segLen + 1;
            if (need > target->cap) {
                int newCap = target->cap > 0 ? target->cap : 16;
                char* grown;
                while (newCap < need) newCap *= 2;
                grown = (char*)XRealloc_System(target->data, (size_t)newCap);
                if (!grown) return;
                target->data = grown;
                target->cap = newCap;
                if (target->len == 0) target->data[0] = '\0';
            }
            XMemmove(target->data + col + segLen, target->data + col,
                     (size_t)(target->len - col) + 1);
            XMemcpy(target->data + col, src, (size_t)segLen);
            target->len += segLen;
            col += segLen;
        }
        if (!nl) break;
        /* '\n'：当前行 [col, len) 下移到新行，并在 col 处断开。 */
        multiline = true; /* 跨行编辑 → 增量不适用，回退全量。 */
        {
            int tailLen = target->len - col;
            char* tail = NULL;
            if (tailLen > 0) {
                tail = xtc_strdupN(target->data + col, tailLen);
                if (!tail) return;
                target->len = col;
                target->data[col] = '\0';
            }
            if (self->m_lineCount >= self->m_lineCap) {
                int newCap = self->m_lineCap + XTC_CAP_GROW;
                XTextControlLine* grown = (XTextControlLine*)XRealloc_System(
                    self->m_lines,
                    (size_t)newCap * sizeof(XTextControlLine));
                if (!grown) {
                    if (tail) XFree_System(tail);
                    return;
                }
                self->m_lines = grown;
                self->m_lineCap = newCap;
            }
            XMemmove(&self->m_lines[line + 2], &self->m_lines[line + 1],
                     (size_t)(self->m_lineCount - line - 1) *
                         sizeof(XTextControlLine));
            XMemset(&self->m_lines[line + 1], 0, sizeof(XTextControlLine));
            if (tail) {
                self->m_lines[line + 1].data = tail;
                self->m_lines[line + 1].len = tailLen;
                self->m_lines[line + 1].cap = tailLen + 1;
            }
            ++self->m_lineCount;
            ++line;
            col = 0;
            target = &self->m_lines[line];
        }
        src = nl + 1;
    }
    /* 单逻辑行插入：只重算该逻辑行的可视行段（失败保持失效 → 全量）。 */
    if (layoutWasValid && !multiline)
        xtc_incrementalLayoutLine(self, line);
}

/**
 * @brief      删除绝对区间 [pos, pos+len)（跨 '\n' 时合并/删除行）。
 * @details    布局失效策略与 xtc_rawInsert 对偶：编辑前缓存有效且删除
 *             未跨行（未触及行间 '\n'，逻辑行数不变）时，增量重算该
 *             逻辑行的可视行段；跨行合并或缓存本已失效时仅整体失效，
 *             惰性全量重建。区间钳位后实际未删除任何字节（pos 越界等）
 *             且缓存此前有效时，文档未变，缓存仍与文档一致，恢复有效
 *             标记避免无谓重建。
 */
static void xtc_rawRemove(XTextControl* self, int pos, int len)
{
    int total;
    int removedBytes = 0;   /* 实际删除的行内容字节（含被删的行间 '\n'）。 */
    int firstLine = -1;     /* 删除起点所在逻辑行（未跨行时即受影响行）。 */
    bool layoutWasValid;
    bool crossedLine = false;
    if (!self || len <= 0) return;
    layoutWasValid = self->m_layoutValid; /* 增量前提：编辑前缓存有效。 */
    xtc_invalidateLayout(self); /* 文档变更 → 可视行缓存失效。 */
    total = xtc_documentLength(self);
    if (pos < 0) {
        len += pos;
        pos = 0;
    }
    if (pos >= total) len = 0; /* 越界：无实际删除，走收尾恢复有效标记。 */
    if (pos + len > total) len = total - pos;
    while (len > 0) {
        int line;
        int col;
        XTextControlLine* target;
        xtc_posToLineCol(self, pos, &line, &col);
        if (firstLine < 0) firstLine = line;
        target = &self->m_lines[line];
        if (col < target->len) {
            int avail = target->len - col;
            int take = len < avail ? len : avail;
            XMemmove(target->data + col, target->data + col + take,
                     (size_t)(target->len - col - take) + 1);
            target->len -= take;
            removedBytes += take;
            len -= take;
            continue;
        }
        /* 列在行尾：删除行间 '\n'，与下一行合并。 */
        if (line + 1 >= self->m_lineCount) break;
        crossedLine = true; /* 跨行删除 → 增量不适用，回退全量。 */
        ++removedBytes;
        {
            XTextControlLine* next = &self->m_lines[line + 1];
            int need = target->len + next->len + 1;
            if (need > target->cap) {
                int newCap = target->cap > 0 ? target->cap : 16;
                char* grown;
                while (newCap < need) newCap *= 2;
                grown = (char*)XRealloc_System(target->data, (size_t)newCap);
                if (!grown) return;
                target->data = grown;
                target->cap = newCap;
            }
            if (next->len > 0 && next->data) {
                XMemcpy(target->data + target->len, next->data,
                        (size_t)next->len);
                target->len += next->len;
            }
            if (target->data) target->data[target->len] = '\0';
            if (next->data) {
                XFree_System(next->data);
                next->data = NULL;
            }
            next->len = 0;
            next->cap = 0;
            XMemmove(&self->m_lines[line + 1], &self->m_lines[line + 2],
                     (size_t)(self->m_lineCount - line - 2) *
                         sizeof(XTextControlLine));
            XMemset(&self->m_lines[self->m_lineCount - 1], 0,
                    sizeof(XTextControlLine));
            --self->m_lineCount;
            --len;
        }
    }
    /* 收尾：未跨行的实际删除增量重算受影响行；未删除且缓存此前有效则
       文档未变，恢复有效标记；其余保持失效（惰性全量重建）。 */
    if (layoutWasValid && !crossedLine) {
        if (removedBytes > 0 && firstLine >= 0)
            xtc_incrementalLayoutLine(self, firstLine);
        else if (removedBytes == 0)
            self->m_layoutValid = true;
    }
}

/** @brief 取文档绝对区间 [pos, pos+len) 的堆拷贝。 */
static char* xtc_getRange(const XTextControl* self, int pos, int len)
{
    int line;
    int col;
    char* out;
    int filled = 0;
    if (!self) return xtc_strdupN("", 0);
    if (len <= 0) return xtc_strdupN("", 0);
    if (pos < 0) {
        len += pos;
        pos = 0;
    }
    if (pos + len > xtc_documentLength(self))
        len = xtc_documentLength(self) - pos;
    if (len <= 0) return xtc_strdupN("", 0);
    out = xtc_strdupN("", 0);
    if (!out) return NULL;
    xtc_posToLineCol(self, pos, &line, &col);
    while (len > 0 && line < self->m_lineCount) {
        const char* text = xtc_lineText(self, line);
        int lineLen = xtc_lineLen(self, line);
        int take = lineLen - col;
        if (take > len) take = len;
        if (take > 0) {
            char* grown = (char*)XRealloc_System(out,
                                                 (size_t)filled + (size_t)take + 1);
            if (!grown) {
                XFree_System(out);
                return NULL;
            }
            out = grown;
            XMemcpy(out + filled, text + col, (size_t)take);
            filled += take;
            out[filled] = '\0';
            len -= take;
        }
        if (len > 0 && line + 1 < self->m_lineCount) {
            char* grown = (char*)XRealloc_System(out, (size_t)filled + 2);
            if (!grown) {
                XFree_System(out);
                return NULL;
            }
            out = grown;
            out[filled++] = '\n';
            out[filled] = '\0';
            --len;
        }
        ++line;
        col = 0;
    }
    return out;
}

/** @brief 平铺行 → 内部文档镜像（对标文档内容同步）。 */
static void xtc_syncDocumentMirror(XTextControl* self)
{
#if XTEXTDOCUMENT_ON
    char* text;
    if (!self || !self->m_textDoc) return;
    text = XTextControl_toPlainText(self);
    if (!text) return;
    XTextDocument_setPlainText(self->m_textDoc, text);
    XFree_System(text);
#endif
    /* 文档变更后字节位置整体位移：光标软行首边沿失效。 */
    self->m_cursorAtRowStart = false;
}

/**
 * @brief      内容变化收尾：镜像文档、发 textChanged/blockCountChanged/
 *             documentSizeChanged/modificationChanged/updateRequest、
 *             刷新字符格式与选区通知（对标 contentsChanged 汇聚路径）。
 */
static void xtc_afterContentsChanged(XTextControl* self, int oldLineCount,
                                     int oldHeight)
{
    XSize size;
    int newHeight;
    if (!self) return;
    xtc_syncDocumentMirror(self);
    xtc_emitVoid(self, XTextControl_textChanged_signal);
    if (self->m_lineCount != oldLineCount)
        xtc_emitInt(self, XTextControl_blockCountChanged_signal,
                    self->m_lineCount);
    /* 对标 Qt documentSize：高度按可视行数（软换行计入）。 */
    newHeight = xtc_contentHeight(self);
    size = XTextControl_size(self);
    if (newHeight != oldHeight)
        xtc_emitSize(self, XTextControl_documentSizeChanged_signal, &size);
    xtc_setModifiedNotify(self, true);
    xtc_emitRect(self, XTextControl_updateRequest_signal, NULL);
    xtc_notifyCharFormat(self);
    xtc_notifySelection(self, false);
}

/* ==================== 编辑管线（带撤销跟踪） ==================== */

/**
 * @brief      插入文本（撤销跟踪 + 内容收尾；group<0 时自动并入当前组）。
 */
static void xtc_editInsert(XTextControl* self, int pos, const char* utf8,
                           int group)
{
    int oldLineCount;
    int oldHeight;
    int useGroup;
    if (!self || !utf8 || !utf8[0]) return;
    oldLineCount = self->m_lineCount;
    oldHeight = xtc_contentHeight(self); /* 对标 Qt：高度按可视行数。 */
    useGroup = group >= 0 ? group
                          : (self->m_editBlockDepth > 0 ? self->m_groupCounter : 0);
    xtc_recordCommand(self, pos, NULL, utf8, useGroup);
    xtc_rawInsert(self, pos, utf8);
    xtc_afterContentsChanged(self, oldLineCount, oldHeight);
}

/**
 * @brief      删除文本（撤销跟踪 + 内容收尾；group<0 时自动并入当前组）。
 */
static void xtc_editRemove(XTextControl* self, int pos, int len, int group)
{
    int oldLineCount;
    int oldHeight;
    char* removed;
    int useGroup;
    if (!self || len <= 0) return;
    if (pos + len > xtc_documentLength(self))
        len = xtc_documentLength(self) - pos;
    if (len <= 0) return;
    oldLineCount = self->m_lineCount;
    oldHeight = xtc_contentHeight(self); /* 对标 Qt：高度按可视行数。 */
    removed = xtc_getRange(self, pos, len);
    useGroup = group >= 0 ? group
                          : (self->m_editBlockDepth > 0 ? self->m_groupCounter : 0);
    xtc_recordCommand(self, pos, removed, NULL, useGroup);
    if (removed) XFree_System(removed);
    xtc_rawRemove(self, pos, len);
    xtc_afterContentsChanged(self, oldLineCount, oldHeight);
}

/**
 * @brief      删除选中文本（对标 QTextCursor::removeSelectedText）。
 */
static void xtc_removeSelectedText(XTextControl* self, int group)
{
    int start;
    int len;
    if (!self || !xtc_hasSelection(self)) return;
    start = xtc_selectionStart(self);
    len = xtc_selectionEnd(self) - start;
    xtc_editRemove(self, start, len, group);
    self->m_cursorAnchor = self->m_cursorPosition = start;
}

/** @brief 清空撤销/重做栈并通知（对标 setUndoRedoEnabled(false) 语义）。 */
static void xtc_clearUndoHistory(XTextControl* self)
{
    if (!self) return;
    xtc_commandStackClear(&self->m_undoStack, &self->m_undoCount, &self->m_undoCap);
    xtc_commandStackClear(&self->m_redoStack, &self->m_redoCount, &self->m_redoCap);
    xtc_notifyUndoRedo(self);
}

/** @brief 清空锚点注册表。 */
static void xtc_clearAnchors(XTextControl* self)
{
    int i;
    if (!self) return;
    for (i = 0; i < self->m_anchorCount; ++i) {
        if (self->m_anchors[i].href) XFree_System(self->m_anchors[i].href);
    }
    self->m_anchorCount = 0;
}

/** @brief 登记锚点（容量不足扩容；href 堆拷贝）。 */
static void xtc_addAnchor(XTextControl* self, int start, int end, const char* href)
{
    if (!self || !href || !href[0] || end <= start) return;
    if (self->m_anchorCount >= self->m_anchorCap) {
        int newCap = self->m_anchorCap + XTC_CAP_GROW;
        XTextControlAnchor* grown = (XTextControlAnchor*)XRealloc_System(
            self->m_anchors, (size_t)newCap * sizeof(XTextControlAnchor));
        if (!grown) return;
        self->m_anchors = grown;
        self->m_anchorCap = newCap;
    }
    self->m_anchors[self->m_anchorCount].start = start;
    self->m_anchors[self->m_anchorCount].end = end;
    self->m_anchors[self->m_anchorCount].href = xtc_strdupN(href, -1);
    if (self->m_anchors[self->m_anchorCount].href)
        ++self->m_anchorCount;
}

/** @brief 清空 preedit 状态（文档与通知由调用方负责）。 */
static void xtc_clearPreeditState(XTextControl* self)
{
    if (!self) return;
    if (self->m_preedit) {
        XFree_System(self->m_preedit);
        self->m_preedit = NULL;
        /* 组合串参与视觉文本（计入折行布局）：清空即失效缓存。 */
        xtc_invalidateLayout(self);
    }
    self->m_preeditPos = 0;
    self->m_preeditCursor = 0;
    self->m_hideCursor = false;
}

/* ==================== 词边界（对标 QTextCursor 词移动的平铺近似） ==== */

/** @brief pos 处码点是否为词字符（pos 越界返回 false）。 */
static bool xtc_classAt(const XTextControl* self, int pos)
{
    int line;
    int col;
    int seq;
    const char* text;
    if (!self || pos < 0 || pos >= xtc_documentLength(self)) return false;
    xtc_posToLineCol(self, pos, &line, &col);
    text = xtc_lineText(self, line);
    seq = XTextUtf8_seqLen(text + col, xtc_lineLen(self, line) - col);
    {
        int i;
        bool word = false;
        for (i = 0; i < seq; ++i) {
            if (xtc_isWordByte((unsigned char)text[col + i])) {
                word = true;
                break;
            }
        }
        return word;
    }
}

/**
 * @brief      词移动定位（对标 WordLeft/WordRight 的平铺近似）。
 * @details    backward：光标在词内（含词尾）→ 词首；在分隔区 → 跳过分隔
 *             再越过前一个词。forward 对称。
 */
static int xtc_wordBoundary(const XTextControl* self, int pos, bool backward)
{
    int total = xtc_documentLength(self);
    int p;
    if (!self) return pos;
    if (backward) {
        p = pos;
        if (p > total) p = total;
        if (p > 0 && xtc_classAt(self, p - 1)) {
            while (p > 0 && xtc_classAt(self, p - 1))
                --p;
        } else {
            while (p > 0 && !xtc_classAt(self, p - 1))
                --p;
            while (p > 0 && xtc_classAt(self, p - 1))
                --p;
        }
        return p;
    }
    p = pos;
    if (p < 0) p = 0;
    if (p < total && xtc_classAt(self, p)) {
        while (p < total && xtc_classAt(self, p))
            ++p;
    } else {
        while (p < total && !xtc_classAt(self, p))
            ++p;
    }
    return p;
}

/**
 * @brief      光标所在词区间（双击选词；分隔符串成对选，对标
 *             QTextCursor::WordUnderCursor 的平铺近似）。
 */
static void xtc_wordRangeUnder(const XTextControl* self, int pos, int* start,
                               int* end)
{
    int total = xtc_documentLength(self);
    bool inWord;
    int s;
    int e;
    if (!self || total <= 0) {
        if (start) *start = 0;
        if (end) *end = 0;
        return;
    }
    if (pos > total) pos = total;
    if (pos < 0) pos = 0;
    inWord = (pos < total) ? xtc_classAt(self, pos)
                           : xtc_classAt(self, pos - 1);
    s = pos;
    while (s > 0 && xtc_classAt(self, s - 1) == inWord)
        --s;
    e = pos;
    if (pos < total && xtc_classAt(self, pos) == inWord) {
        while (e < total && xtc_classAt(self, e) == inWord)
            ++e;
    } else if (e < total) {
        /* pos 恰在词尾边界：向前纳入同类别串。 */
        while (e < total && xtc_classAt(self, e) == inWord)
            ++e;
    }
    if (start) *start = s;
    if (end) *end = e;
}

/* ==================== 光标定位/移动（对标 QTextCursor::movePosition） ==== */

/**
 * @brief      设置光标位置（对标 Private::setCursorPosition(pos, mode)）；
 *             MoveAnchor 时失效双击/三击记忆与垂直列目标。
 */
static void xtc_setCursorPos(XTextControl* self, int pos, int mode)
{
    if (!self) return;
    if (pos < 0) pos = 0;
    if (pos > xtc_documentLength(self)) pos = xtc_documentLength(self);
    self->m_cursorAtRowStart = false; /* 常规移动清边沿（仅 Home 置位）。 */
    if (mode == (int)XTextControlMoveMode_KeepAnchor) {
        self->m_cursorPosition = pos;
    } else {
        self->m_cursorPosition = pos;
        self->m_cursorAnchor = pos;
        self->m_wordSelStart = -1;
        self->m_wordSelEnd = -1;
        self->m_blockSelStart = -1;
        self->m_blockSelEnd = -1;
        self->m_goalCol = -1;
    }
}

/**
 * @brief      词选扩展（对标 extendWordwiseSelection）。
 * @param      suggestedPos 指针反查得到的目标位置。
 * @param      mouseX 指针 X（内容坐标；词外钳制判定用）。
 */
static void xtc_extendWordwise(XTextControl* self, int suggestedPos, int mouseX)
{
    int wordStart;
    int wordEnd;
    XFont font;
    int wordStartX;
    int wordEndX;
    bool selectable = (self->m_interactionFlags &
                       (int)XTextControlInteraction_TextSelectableByMouse) != 0;
    if (!self || self->m_wordSelStart < 0) return;
    /* 落在初始词内：保持原词选（对标 setTextCursor(selectedWordOnDoubleClick)）。 */
    if (suggestedPos >= self->m_wordSelStart && suggestedPos <= self->m_wordSelEnd) {
        self->m_cursorAnchor = self->m_wordSelStart;
        self->m_cursorPosition = self->m_wordSelEnd;
        if (selectable) xtc_notifySelection(self, true);
        return;
    }
    xtc_wordRangeUnder(self, suggestedPos, &wordStart, &wordEnd);
    if (wordEnd <= wordStart) return;
    /* 只读浅拷贝:与 self->m_font 共享 m_family/m_styleName,使用后禁止
       deinit(会释放控制器自有字符串,下一读点即踩悬垂指针)。 */
    XMemcpy(&font, &self->m_font, sizeof(XFont));
    /* 词界像素 X 按可视行内坐标度量（软换行后每行 X 归零，对标 Qt
       行内坐标比较）。 */
    {
        char* heap = NULL;
        int row;
        const char* text;
        int rowBase;
        int wsV;
        int weV;
        int line;
        int lineStart;
        xtc_ensureLayout(self);
        xtc_posToVisualRowCol(self, suggestedPos, &row, NULL);
        line = self->m_visualRows[row].line;
        rowBase = self->m_visualRows[row].start;
        text = xtc_visualLineText(self, line, &heap);
        xtc_posToLineCol(self, suggestedPos, &line, NULL);
        lineStart = xtc_lineColToPos(self, line, 0);
        wsV = xtc_docColToVisualCol(self, line, wordStart - lineStart);
        weV = xtc_docColToVisualCol(self, line, wordEnd - lineStart);
        if (wsV < rowBase) wsV = rowBase;
        if (weV > rowBase + self->m_visualRows[row].len)
            weV = rowBase + self->m_visualRows[row].len;
        wordStartX = XPainter_textWidthRange(&font, text, rowBase, wsV);
        wordEndX = XPainter_textWidthRange(&font, text, rowBase, weV);
        if (heap) XFree_System(heap);
    }
    /* 浅拷贝不拥有堆串:无需释放(所有权在 self->m_font)。 */
    if (!self->m_wordSelectionEnabled &&
        (mouseX < wordStartX || mouseX > wordEndX))
        return;
    if (suggestedPos < self->m_wordSelStart) {
        self->m_cursorPosition = self->m_wordSelEnd;
        self->m_cursorAnchor = wordStart;
    } else {
        self->m_cursorPosition = self->m_wordSelStart;
        self->m_cursorAnchor = wordEnd;
    }
    if (selectable) xtc_notifySelection(self, true);
}

/**
 * @brief      段选扩展（对标 extendBlockwiseSelection；含段尾分隔符）。
 */
static void xtc_extendBlockwise(XTextControl* self, int suggestedPos)
{
    bool selectable = (self->m_interactionFlags &
                       (int)XTextControlInteraction_TextSelectableByMouse) != 0;
    if (!self || self->m_blockSelStart < 0) return;
    /* 落在初始段内：保持原段选。 */
    if (suggestedPos >= self->m_blockSelStart && suggestedPos <= self->m_blockSelEnd) {
        self->m_cursorAnchor = self->m_blockSelStart;
        self->m_cursorPosition = self->m_blockSelEnd;
        if (selectable) xtc_notifySelection(self, true);
        return;
    }
    if (suggestedPos < self->m_blockSelStart) {
        int line;
        xtc_posToLineCol(self, suggestedPos, &line, NULL);
        self->m_cursorPosition = self->m_blockSelEnd;
        self->m_cursorAnchor = xtc_lineColToPos(self, line, 0);
    } else {
        int line;
        int blockEnd;
        xtc_posToLineCol(self, self->m_blockSelStart, &line, NULL);
        blockEnd = xtc_lineColToPos(self, line, xtc_lineLen(self, line));
        if (blockEnd < xtc_documentLength(self)) ++blockEnd;
        self->m_cursorPosition = self->m_blockSelStart;
        self->m_cursorAnchor = blockEnd;
    }
    if (selectable) xtc_notifySelection(self, true);
}

/**
 * @brief      光标移动操作（对标 cursorMoveKeyEvent 的 op/mode 分解）。
 * @details    Left/Right/Up/Down/Home/End 经"字节位置 ↔ (可视行, 列)"
 *             双向映射（软换行后可视行即光标移动单位，对标 Qt 在
 *             QTextLayout 行间移动）；StartOfBlock/EndOfBlock/Previous-
 *             Block/NextBlock 保持段（逻辑行）语义；Up/Down 以像素 X
 *             为列目标（对标 cursor x 保持）。
 * @return     操作被接受返回 true（位置变化或保持锚点的扩展）。
 */
static bool xtc_movePosition(XTextControl* self, int op, int mode)
{
    int total;
    int line;
    int col;
    bool keep = mode == (int)XTextControlMoveMode_KeepAnchor;
    int oldPos;
    if (!self) return false;
    total = xtc_documentLength(self);
    oldPos = self->m_cursorPosition;
    xtc_posToLineCol(self, self->m_cursorPosition, &line, &col);
    switch (op) {
    case (int)XTextControlMove_NoMove:
        return false;
    case (int)XTextControlMove_Left:
    case (int)XTextControlMove_PreviousCharacter: {
        /* 视觉列口径左移：软断点处自动落到上一可视行行尾。 */
        int row;
        int rowCol;
        xtc_ensureLayout(self);
        xtc_posToVisualRowCol(self, self->m_cursorPosition, &row, &rowCol);
        if (rowCol > 0) {
            char* heap = NULL;
            const char* text =
                xtc_visualLineText(self, self->m_visualRows[row].line, &heap);
            int base = self->m_visualRows[row].start + rowCol;
            int prev = (int)XTextUtf8_prevBoundary(text, (size_t)base);
            int pos = xtc_visualRowColToPos(self, row,
                                            prev - self->m_visualRows[row].start);
            if (heap) XFree_System(heap);
            xtc_setCursorPos(self, pos, mode);
        } else if (self->m_visualRows[row].start == 0 && line > 0) {
            /* 段首：跨 '\n' 到上一段尾（分隔符单字节）。 */
            xtc_setCursorPos(self, self->m_cursorPosition - 1, mode);
        }
        /* 软断点（rowCol==0 且 start>0）：绝对位置即上一行行尾，无移动。 */
        break;
    }
    case (int)XTextControlMove_Right:
    case (int)XTextControlMove_NextCharacter: {
        int row;
        int rowCol;
        xtc_ensureLayout(self);
        xtc_posToVisualRowCol(self, self->m_cursorPosition, &row, &rowCol);
        if (rowCol < self->m_visualRows[row].len) {
            char* heap = NULL;
            const char* text =
                xtc_visualLineText(self, self->m_visualRows[row].line, &heap);
            int base = self->m_visualRows[row].start + rowCol;
            int vlen = xtc_visualLineLen(self, self->m_visualRows[row].line);
            int seq = base < vlen
                          ? XTextUtf8_seqLen(text + base, vlen - base) : 0;
            int pos;
            if (heap) XFree_System(heap);
            pos = xtc_visualRowColToPos(self, row,
                                        rowCol + (seq > 0 ? seq : 1));
            xtc_setCursorPos(self, pos, mode);
        } else if (line + 1 < self->m_lineCount) {
            /* 段尾：跨 '\n' 到下一段首。 */
            xtc_setCursorPos(self, self->m_cursorPosition + 1, mode);
        }
        break;
    }
    case (int)XTextControlMove_Up:
    case (int)XTextControlMove_PreviousBlock: {
        int row;
        xtc_ensureLayout(self);
        xtc_posToVisualRowCol(self, self->m_cursorPosition, &row, NULL);
        if (op == (int)XTextControlMove_PreviousBlock) {
            /* 段移动保持逻辑行语义（对标 PreviousBlock）。 */
            if (line > 0) {
                int goal = self->m_goalCol >= 0 ? self->m_goalCol : col;
                xtc_setCursorPos(self, xtc_lineColToPos(self, line - 1, goal),
                                 mode);
                if (keep) self->m_goalCol = goal;
            } else if (keep) {
                xtc_setCursorPos(self, 0, mode);
            }
            break;
        }
        if (row > 0) {
            /* 上一可视行，像素 X 目标（对标 cursor x 保持）。 */
            int goal = self->m_goalCol >= 0
                           ? self->m_goalCol
                           : XTextControl_cursorRect(self).x;
            int goalCol = xtc_rowXToCol(self, row - 1, goal);
            xtc_setCursorPos(self, xtc_visualRowColToPos(self, row - 1,
                                                         goalCol),
                             mode);
            if (keep) self->m_goalCol = goal;
            /* X 目标落在软行首（像素 0）：边沿归属上一行行首。 */
            self->m_cursorAtRowStart = goalCol == 0;
        } else if (keep && op == (int)XTextControlMove_Up) {
            /* 对标 SelectPreviousLine 在首行退化为选到文档头。 */
            xtc_setCursorPos(self, 0, mode);
        }
        break;
    }
    case (int)XTextControlMove_Down:
    case (int)XTextControlMove_NextBlock: {
        int row;
        xtc_ensureLayout(self);
        xtc_posToVisualRowCol(self, self->m_cursorPosition, &row, NULL);
        if (op == (int)XTextControlMove_NextBlock) {
            /* 段移动保持逻辑行语义（对标 NextBlock）。 */
            if (line + 1 < self->m_lineCount) {
                int goal = self->m_goalCol >= 0 ? self->m_goalCol : col;
                xtc_setCursorPos(self, xtc_lineColToPos(self, line + 1, goal),
                                 mode);
                if (keep) self->m_goalCol = goal;
            } else if (keep) {
                xtc_setCursorPos(self, total, mode);
            }
            break;
        }
        if (row + 1 < self->m_visualCount) {
            /* 下一可视行，像素 X 目标（对标 cursor x 保持）。 */
            int goal = self->m_goalCol >= 0
                           ? self->m_goalCol
                           : XTextControl_cursorRect(self).x;
            int goalCol = xtc_rowXToCol(self, row + 1, goal);
            xtc_setCursorPos(self, xtc_visualRowColToPos(self, row + 1,
                                                         goalCol),
                             mode);
            if (keep) self->m_goalCol = goal;
            /* X 目标落在软行首（像素 0）：边沿归属下一行行首。 */
            self->m_cursorAtRowStart = goalCol == 0;
        } else if (keep && op == (int)XTextControlMove_Down) {
            /* 对标 SelectNextLine 在末行退化为选到文档尾。 */
            xtc_setCursorPos(self, total, mode);
        }
        break;
    }
    case (int)XTextControlMove_Start:
        xtc_setCursorPos(self, 0, mode);
        break;
    case (int)XTextControlMove_End:
        xtc_setCursorPos(self, total, mode);
        break;
    case (int)XTextControlMove_StartOfLine:
    case (int)XTextControlMove_StartOfBlock: {
        if (op == (int)XTextControlMove_StartOfLine) {
            /* 对标 StartOfLine：可视行行首（软换行行首）。 */
            int row;
            xtc_ensureLayout(self);
            xtc_posToVisualRowCol(self, self->m_cursorPosition, &row, NULL);
            xtc_setCursorPos(self, xtc_visualRowColToPos(self, row, 0), mode);
            /* 行首字节可能是软断点：置边沿让视觉/后续操作归属本行行首。 */
            self->m_cursorAtRowStart = true;
        } else {
            xtc_setCursorPos(self, xtc_lineColToPos(self, line, 0), mode);
        }
        break;
    }
    case (int)XTextControlMove_EndOfLine:
    case (int)XTextControlMove_EndOfBlock: {
        if (op == (int)XTextControlMove_EndOfLine) {
            /* 对标 EndOfLine：可视行行尾（软换行行尾）。 */
            int row;
            xtc_ensureLayout(self);
            xtc_posToVisualRowCol(self, self->m_cursorPosition, &row, NULL);
            xtc_setCursorPos(self,
                             xtc_visualRowColToPos(self, row,
                                                   self->m_visualRows[row].len),
                             mode);
        } else {
            xtc_setCursorPos(self, xtc_lineColToPos(self, line,
                                                    xtc_lineLen(self, line)),
                             mode);
        }
        break;
    }
    case (int)XTextControlMove_WordLeft:
    case (int)XTextControlMove_PreviousWord:
    case (int)XTextControlMove_StartOfWord:
        xtc_setCursorPos(self, xtc_wordBoundary(self, self->m_cursorPosition,
                                                true),
                         mode);
        break;
    case (int)XTextControlMove_WordRight:
    case (int)XTextControlMove_NextWord:
    case (int)XTextControlMove_EndOfWord:
        xtc_setCursorPos(self, xtc_wordBoundary(self, self->m_cursorPosition,
                                                false),
                         mode);
        break;
    case (int)XTextControlMove_NextCell:
    case (int)XTextControlMove_PreviousCell:
    case (int)XTextControlMove_NextRow:
    case (int)XTextControlMove_PreviousRow:
        /* 平铺模型无表格语义（对标差异）。 */
        return false;
    default:
        return false;
    }
    return self->m_cursorPosition != oldPos || keep;
}

/* ==================== 键盘事件（对标 keyPressEvent） ==================== */

/**
 * @brief      是否编辑器通用快捷键（对标 isCommonTextEditShortcut）。
 */
static bool xtc_isCommonShortcut(int key, int mods)
{
    if (!(mods & (int)XKeyboardModifier_ControlModifier)) return false;
    switch (key) {
    case (int)XKey_Left:
    case (int)XKey_Right:
    case (int)XKey_Up:
    case (int)XKey_Down:
    case (int)XKey_Home:
    case (int)XKey_End:
    case (int)XKey_PageUp:
    case (int)XKey_PageDown:
    case (int)XKey_Insert:
    case (int)XKey_Delete:
    case (int)XKey_Backspace:
    case 'A':
    case 'C':
    case 'V':
    case 'X':
    case 'Z':
    case 'Y':
        return true;
    default:
        return false;
    }
}

/** @brief 修饰键剥离 Shift 后是否干净（对标 Backspace 无修饰判定）。 */
static bool xtc_plainMods(int mods)
{
    /* 容忍 KeypadModifier（Qt 同款口径）：部分平台 NumLock 会给主键区
     * 按键补小键盘位，不忽略则 Backspace/Delete 的"无修饰"分支失效。 */
    return (mods & ~((int)XKeyboardModifier_ShiftModifier
                     | (int)XKeyboardModifier_KeypadModifier)) == 0;
}

/** @brief 修饰键是否恰为 Shift（对标 modifiers == Qt::ShiftModifier）。 */
static bool xtc_isShiftOnly(int mods)
{
    return mods == (int)XKeyboardModifier_ShiftModifier;
}

/**
 * @brief      键盘路由（对标 QWidgetTextControlPrivate::keyPressEvent 的
 *             分支顺序：全选/复制快捷键 → 键盘选区移动 → 键盘链接激活 →
 *             只读拒绝 → Backspace/回车/Delete → 撤销/重做/剪贴板/删词 →
 *             可接受输入插入（覆盖模式先行删除））。
 */
static void xtc_keyPressEvent(XTextControl* self, XKeyEvent* e)
{
    int key;
    int mods;
    int flags;
    if (!self || !e) return;
    key = e->m_key;
    mods = (int)e->m_modifiers;
    flags = self->m_interactionFlags;
    {
        bool ctrl = (mods & (int)XKeyboardModifier_ControlModifier) != 0;
        bool shift = (mods & (int)XKeyboardModifier_ShiftModifier) != 0;

        /* Ctrl+A 全选（对标 SelectAll 快捷键分支）。 */
        if (ctrl && !shift && (key == 'A' || key == 'a')) {
            XTextControl_selectAll(self);
            XEvent_accept((XEvent*)e);
            return;
        }
        /* Ctrl+C 复制（对标 Copy 快捷键分支）。 */
        if (ctrl && !shift && (key == 'C' || key == 'c')) {
            XTextControl_copy(self);
            XEvent_accept((XEvent*)e);
            return;
        }

        /* 键盘选区移动族（对标 cursorMoveKeyEvent）。 */
        if (flags & (int)XTextControlInteraction_TextSelectableByKeyboard) {
            int op = (int)XTextControlMove_NoMove;
            int mode = shift ? (int)XTextControlMoveMode_KeepAnchor
                             : (int)XTextControlMoveMode_MoveAnchor;
            switch (key) {
            case (int)XKey_Left:
                op = ctrl ? (int)XTextControlMove_WordLeft
                          : (int)XTextControlMove_Left;
                break;
            case (int)XKey_Right:
                op = ctrl ? (int)XTextControlMove_WordRight
                          : (int)XTextControlMove_Right;
                break;
            case (int)XKey_Up: op = (int)XTextControlMove_Up; break;
            case (int)XKey_Down: op = (int)XTextControlMove_Down; break;
            case (int)XKey_Home:
                op = ctrl ? (int)XTextControlMove_Start
                          : (int)XTextControlMove_StartOfLine;
                break;
            case (int)XKey_End:
                op = ctrl ? (int)XTextControlMove_End
                          : (int)XTextControlMove_EndOfLine;
                break;
            default: break;
            }
            if (op != (int)XTextControlMove_NoMove) {
                int oldPos = self->m_cursorPosition;
                int oldAnchor = self->m_cursorAnchor;
                XRect cr = xtc_cursorRepaintRect(self);
                xtc_emitRect(self, XTextControl_updateRequest_signal, &cr);
                xtc_movePosition(self, op, mode);
                XTextControl_ensureCursorVisible(self);
                xtc_notifySelection(self,
                                    mode == (int)XTextControlMoveMode_KeepAnchor);
                xtc_notifyCursorPosition(self, oldPos);
                xtc_repaintOldAndNewSelection(self, oldPos, oldAnchor);
                goto accepted;
            }
            /* Ctrl 编辑族（撤销/重做/剪切/粘贴/删词）。 */
            if (ctrl && !shift) {
                switch (key) {
                case 'Z': case 'z':
                    XTextControl_undo(self);
                    goto accepted;
                case 'Y': case 'y':
                    XTextControl_redo(self);
                    goto accepted;
                case 'X': case 'x':
                    XTextControl_cut(self);
                    goto accepted;
                case 'V': case 'v':
                    XTextControl_paste(self);
                    goto accepted;
                default:
                    break;
                }
            }
            if (ctrl) {
                if (key == (int)XKey_Backspace) {
                    /* 对标 DeleteStartOfWord。 */
                    if (!xtc_hasSelection(self))
                        xtc_movePosition(self, (int)XTextControlMove_WordLeft,
                                         (int)XTextControlMoveMode_KeepAnchor);
                    xtc_removeSelectedText(self, -1);
                    goto accepted;
                }
                if (key == (int)XKey_Delete) {
                    /* 对标 DeleteEndOfWord。 */
                    if (!xtc_hasSelection(self))
                        xtc_movePosition(self, (int)XTextControlMove_WordRight,
                                         (int)XTextControlMoveMode_KeepAnchor);
                    xtc_removeSelectedText(self, -1);
                    goto accepted;
                }
            }
        }

        /* 键盘链接激活（Return/Enter + 选区，对标 LinksAccessibleByKeyboard）。 */
        if ((flags & (int)XTextControlInteraction_LinksAccessibleByKeyboard) &&
            (key == (int)XKey_Return || key == (int)XKey_Enter) &&
            xtc_hasSelection(self)) {
            XString* href = XTextControl_anchorAtCursor(self);
            const char* utf8 = href ? XString_toUtf8(href) : NULL;
            if (utf8 && utf8[0]) {
                self->m_cursorIsFocusIndicator = self->m_hasFocus;
                xtc_emitString(self, XTextControl_linkActivated_signal, utf8);
            }
            if (href) XString_delete_base((XClass*)href);
            XEvent_accept((XEvent*)e);
            return;
        }

        /* 不可编辑：忽略（对标 e->ignore()）。 */
        if (!(flags & (int)XTextControlInteraction_TextEditable)) {
            XEvent_ignore((XEvent*)e);
            return;
        }

        /* 无修饰 Backspace：删前一码点（对标 Backspace 分支；块缩进/列表
           特例为富文本路径，平铺模型不适用）。行首退删段落分隔符。 */
        if (key == (int)XKey_Backspace && xtc_plainMods(mods)) {
            if (xtc_hasSelection(self)) {
                xtc_removeSelectedText(self, -1);
            } else if (self->m_cursorPosition > 0) {
                int pos = self->m_cursorPosition;
                int line;
                int col;
                int back;
                xtc_posToLineCol(self, pos, &line, &col);
                if (col > 0) {
                    const char* text = xtc_lineText(self, line);
                    back = col - (int)XTextUtf8_prevBoundary(text, (size_t)col);
                } else {
                    back = 1; /* '\n' 单字节。 */
                }
                xtc_editRemove(self, pos - back, back, -1);
                self->m_cursorAnchor = self->m_cursorPosition = pos - back;
                xtc_notifyCursorPosition(self, pos);
                xtc_notifySelection(self, false);
            }
            goto accepted;
        }

        /* 回车：插入段落分隔符（独立撤销组，对标
           InsertParagraphSeparator 的分隔符命令不并入键入组）。 */
        if (key == (int)XKey_Return || key == (int)XKey_Enter) {
            int pos;
            int group = xtc_groupBegin(self);
            xtc_removeSelectedText(self, group);
            pos = self->m_cursorPosition;
            xtc_editInsert(self, pos, "\n", group);
            self->m_cursorAnchor = self->m_cursorPosition = pos + 1;
            xtc_groupEnd(self);
            xtc_notifyCursorPosition(self, pos);
            xtc_notifySelection(self, false);
            goto accepted;
        }

        /* 无修饰 Delete：删后一码点（对标 Delete 快捷键）。行尾删段落
           分隔符。 */
        if (key == (int)XKey_Delete && xtc_plainMods(mods)) {
            if (xtc_hasSelection(self)) {
                xtc_removeSelectedText(self, -1);
            } else if (self->m_cursorPosition < xtc_documentLength(self)) {
                int pos = self->m_cursorPosition;
                int line;
                int col;
                int fwd;
                xtc_posToLineCol(self, pos, &line, &col);
                if (col < xtc_lineLen(self, line)) {
                    const char* text = xtc_lineText(self, line);
                    fwd = XTextUtf8_seqLen(text + col,
                                           xtc_lineLen(self, line) - col);
                } else {
                    fwd = 1; /* '\n' 单字节。 */
                }
                xtc_editRemove(self, pos, fwd, -1);
                xtc_notifySelection(self, false);
            }
            goto accepted;
        }

        /* 可接受输入：可打印 ASCII + 无 Ctrl/Meta/Alt（对标
           isAcceptableInput；覆盖模式先行删除未选中的下一码点）。 */
        if (key >= 0x20 && key <= 0x7E &&
            (mods & ((int)XKeyboardModifier_ControlModifier |
                     (int)XKeyboardModifier_MetaModifier |
                     (int)XKeyboardModifier_AltModifier)) == 0) {
            char ch[2];
            int pos;
            ch[0] = (char)key;
            ch[1] = '\0';
            if (self->m_overwriteMode && !xtc_hasSelection(self) &&
                self->m_cursorPosition < xtc_documentLength(self)) {
                int line;
                int col;
                xtc_posToLineCol(self, self->m_cursorPosition, &line, &col);
                if (col < xtc_lineLen(self, line)) {
                    /* 覆盖删除整码点（对标 overwrite 替换一字符）。 */
                    const char* text = xtc_lineText(self, line);
                    int lineLen = xtc_lineLen(self, line);
                    int fwd = XTextUtf8_seqLen(text + col, lineLen - col);
                    xtc_editRemove(self, self->m_cursorPosition, fwd, -1);
                }
            }
            xtc_removeSelectedText(self, -1);
            pos = self->m_cursorPosition;
            xtc_editInsert(self, pos, ch, -1);
            self->m_cursorAnchor = self->m_cursorPosition = pos + 1;
            xtc_notifyCursorPosition(self, pos);
            xtc_notifySelection(self, false);
            goto accepted;
        }
    }

    XEvent_ignore((XEvent*)e);
    return;

accepted:
    XEvent_accept((XEvent*)e);
    self->m_cursorOn = true;
    XTextControl_ensureCursorVisible(self);
    xtc_notifyCharFormat(self);
}

/* ==================== 链接（anchorAt / 激活） ==================== */

/** @brief 命中锚点查询；命中输出 href 堆拷贝（未命中 NULL）。 */
static char* xtc_anchorHit(const XTextControl* self, int pos)
{
    int i;
    if (!self) return NULL;
    for (i = 0; i < self->m_anchorCount; ++i) {
        if (pos >= self->m_anchors[i].start && pos < self->m_anchors[i].end)
            return xtc_strdupN(self->m_anchors[i].href, -1);
    }
    return NULL;
}

/** @brief 激活光标下链接（对标 activateLinkUnderCursor）。 */
static void xtc_activateLinkUnderCursor(XTextControl* self)
{
    char* href = xtc_anchorHit(self, xtc_selectionStart(self));
    if (!href) return;
    if (self->m_hasFocus) {
        self->m_cursorIsFocusIndicator = true;
    } else {
        self->m_cursorIsFocusIndicator = false;
        self->m_cursorAnchor = self->m_cursorPosition;
    }
    xtc_repaintOldAndNewSelection(self, self->m_cursorPosition,
                                  self->m_cursorPosition);
    /* 对标差异：XGui 无 QDesktopServices；openExternalLinks 不改变行为，
       统一经 linkActivated 由应用层决定外开。 */
    xtc_emitString(self, XTextControl_linkActivated_signal, href);
    XFree_System(href);
}

/* ==================== 鼠标事件（对标 mousePress/Move/Release/DblClick） ==== */

/** @brief 曼哈顿距离。 */
static int xtc_manhattan(const XPoint* a, const XPoint* b)
{
    int dx = a->x - b->x;
    int dy = a->y - b->y;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    return dx + dy;
}

/**
 * @brief      起拖（对标 startDrag 的平铺承载；真实拖拽环由宿主执行，
 *             自发起移动放置由 m_dragInProgress 标记）。
 */
static void xtc_startDrag(XTextControl* self)
{
    if (!self) return;
    self->m_mousePressed = false;
    self->m_dragInProgress = true;
}

/** @brief preedit 提交（对标 commitPreedit 的平铺回退：组合文本文档化）。 */
static void xtc_commitPreedit(XTextControl* self)
{
    int oldHeight;
    int oldLineCount;
    if (!xtc_isPreediting(self)) return;
    oldLineCount = self->m_lineCount;
    oldHeight = xtc_contentHeight(self); /* 对标 Qt：高度按可视行数。 */
    xtc_editInsert(self, self->m_preeditPos, self->m_preedit, -1);
    self->m_cursorAnchor = self->m_cursorPosition =
        self->m_preeditPos + xtc_preeditLen(self);
    xtc_clearPreeditState(self);
    xtc_emitVoid(self, XTextControl_microFocusChanged_signal);
    xtc_afterContentsChanged(self, oldLineCount, oldHeight);
}

/**
 * @brief      按指定剪贴板模式粘贴（xtc_mousePressEvent 中键分支前置声明；
 *             实现见 XTextControl_paste 处，对标 QWidgetTextControl::paste
 *             (QClipboard::Mode) 的模式参数化通道）。
 */
static void xtc_pasteFromMode(XTextControl* self, int mode);

/**
 * @brief      中键按下：光标落到点击处后从 Selection 选择缓冲（X11
 *             PRIMARY）粘贴。对标 Qt QWidgetTextControlPrivate::
 *             mousePressEvent 中键分支（TextEditable 且剪贴板
 *             supportsSelection 时 setCursorPosition(点击处) +
 *             paste(QClipboard::Selection)）。
 * @return     true 已处理（事件接收）；false 未处理（平台不支持选择区/
 *             不可编辑/未命中，保持既有 ignore 行为零回归）。
 * @note       粘贴复用 XTextControl_paste 既有通道（xtc_pasteFromMode），
 *             只读不写剪贴板，CLIPBOARD 内容不受影响（对标 Qt 中键粘贴
 *             语义）。
 */
static bool xtc_middleClickPaste(XTextControl* self, const XMouseEvent* e)
{
#if XCLIPBOARD_ON && XGUIAPPLICATION_ON
    XClipboard* clipObj;
#endif
    XPoint pos;
    int cursorPos;
    int oldPos;
    int oldAnchor;
    if (!self || !e ||
        !(self->m_interactionFlags &
          (int)XTextControlInteraction_TextEditable))
        return false;
#if XCLIPBOARD_ON && XGUIAPPLICATION_ON
    /* 对标 Qt：中键 Selection 粘贴仅当平台后端声明支持选择区
       （QGuiApplication::clipboard()->supportsSelection() 门禁）。 */
    clipObj = XGuiApplication_clipboard();
    if (!clipObj || !XClipboard_supportsSelection(clipObj))
        return false;
#else
    /* 剪贴板子系统裁剪：无系统门禁可查，直通共享层回退（与
       xtc_pasteFromMode 的 systemOnly=false 路径同语义）。 */
#endif
    pos = e->m_position;
    /* 对标 Qt cursorForPosition(点击处)：FuzzyHit 未命中（<0）不动作。 */
    cursorPos = XTextControl_hitTest(
        self, &pos, (int)XTextControlHitTestAccuracy_FuzzyHit);
    if (cursorPos < 0) return false;
    oldPos = self->m_cursorPosition;
    oldAnchor = self->m_cursorAnchor;
    if (xtc_isPreediting(self)) xtc_commitPreedit(self);
    /* 对标 Qt setCursorPosition(pos)：收起选区并把光标锚到点击处。 */
    xtc_setCursorPos(self, cursorPos, (int)XTextControlMoveMode_MoveAnchor);
    xtc_notifyCursorPosition(self, oldPos);
    xtc_repaintOldAndNewSelection(self, oldPos, oldAnchor);
    xtc_pasteFromMode(self, (int)XClipboardMode_Selection);
    return true;
}

/**
 * @brief      鼠标按下（对标 mousePressEvent 全分支：链接锚点记录、
 *             焦点指示清理、三击判定、Shift 扩展、起拖判定、preedit 提交）。
 */
static void xtc_mousePressEvent(XTextControl* self, XMouseEvent* e)
{
    int flags = self->m_interactionFlags;
    XPoint pos;
    int oldPos;
    int oldAnchor;
    int button;
    if (!self || !e) return;
    pos = e->m_position;
    button = (int)e->m_button;

    self->m_mousePressPos = pos;
    self->m_mightStartDrag = false;

    if (flags & (int)XTextControlInteraction_LinksAccessibleByMouse) {
        XString* anchor = XTextControl_anchorAt(self, &pos);
        const char* utf8 = anchor ? XString_toUtf8(anchor) : NULL;
        if (self->m_anchorOnMousePress) {
            XFree_System(self->m_anchorOnMousePress);
            self->m_anchorOnMousePress = NULL;
        }
        if (utf8 && utf8[0])
            self->m_anchorOnMousePress = xtc_strdupN(utf8, -1);
        if (anchor) XString_delete_base((XClass*)anchor);
        if (self->m_cursorIsFocusIndicator) {
            self->m_cursorIsFocusIndicator = false;
            self->m_cursorAnchor = self->m_cursorPosition;
        }
    }

    if (!(button & (int)XMouseButton_LeftButton) ||
        !((flags & (int)XTextControlInteraction_TextSelectableByMouse) ||
          (flags & (int)XTextControlInteraction_TextEditable))) {
        /* 对标 Qt mousePressEvent 中键分支：中键 Selection 粘贴见
           xtc_middleClickPaste；其余非左键/未处理场景保持既有 ignore。 */
        if ((button & (int)XMouseButton_MiddleButton) &&
            xtc_middleClickPaste(self, e))
            XEvent_accept((XEvent*)e);
        else
            XEvent_ignore((XEvent*)e);
        return;
    }

    self->m_cursorIsFocusIndicator = false;
    oldPos = self->m_cursorPosition;
    oldAnchor = self->m_cursorAnchor;
    self->m_mousePressed =
        (flags & (int)XTextControlInteraction_TextSelectableByMouse) ? true : false;

    xtc_commitPreedit(self);

    if (self->m_tripleClickTimer != XTIMER_INVALID_ID &&
        xtc_manhattan(&pos, &self->m_tripleClickPoint) < XTC_START_DRAG_DISTANCE) {
        /* 三击：整段选择（含段尾分隔符），不重设光标位置（对标）。 */
        int line;
        int blockStart;
        int blockEnd;
        xtc_posToLineCol(self, self->m_cursorPosition, &line, NULL);
        blockStart = xtc_lineColToPos(self, line, 0);
        blockEnd = xtc_lineColToPos(self, line, xtc_lineLen(self, line));
        if (blockEnd < xtc_documentLength(self)) ++blockEnd;
        self->m_cursorPosition = blockEnd;
        self->m_cursorAnchor = blockStart;
        self->m_blockSelStart = blockStart;
        self->m_blockSelEnd = blockEnd;
        if (self->m_anchorOnMousePress) {
            XFree_System(self->m_anchorOnMousePress);
            self->m_anchorOnMousePress = NULL;
        }
        XObject_killTimer((XObject*)self, self->m_tripleClickTimer);
        self->m_tripleClickTimer = XTIMER_INVALID_ID;
    } else {
        int cursorPos = XTextControl_hitTest(
            self, &pos, (int)XTextControlHitTestAccuracy_FuzzyHit);
        if (cursorPos < 0) {
            XEvent_ignore((XEvent*)e);
            return;
        }
        if (xtc_isShiftOnly((int)e->m_modifiers) &&
            (flags & (int)XTextControlInteraction_TextSelectableByMouse)) {
            if (self->m_wordSelectionEnabled && self->m_wordSelStart < 0) {
                xtc_setCursorPos(self, cursorPos,
                                 (int)XTextControlMoveMode_MoveAnchor);
                xtc_wordRangeUnder(self, cursorPos, &self->m_wordSelStart,
                                   &self->m_wordSelEnd);
                self->m_cursorPosition = self->m_wordSelEnd;
                self->m_cursorAnchor = self->m_wordSelStart;
            }
            if (self->m_blockSelStart >= 0)
                xtc_extendBlockwise(self, cursorPos);
            else if (self->m_wordSelStart >= 0)
                xtc_extendWordwise(self, cursorPos, pos.x);
            else if (!self->m_wordSelectionEnabled)
                xtc_setCursorPos(self, cursorPos,
                                 (int)XTextControlMoveMode_KeepAnchor);
        } else {
            if (self->m_dragEnabled && xtc_hasSelection(self) &&
                !self->m_cursorIsFocusIndicator &&
                cursorPos >= xtc_selectionStart(self) &&
                cursorPos <= xtc_selectionEnd(self) &&
                XTextControl_hitTest(self, &pos,
                                     (int)XTextControlHitTestAccuracy_ExactHit) >= 0) {
                self->m_mightStartDrag = true;
                return;
            }
            xtc_setCursorPos(self, cursorPos, (int)XTextControlMoveMode_MoveAnchor);
        }
    }

    if (flags & (int)XTextControlInteraction_TextEditable) {
        XTextControl_ensureCursorVisible(self);
        xtc_notifyCursorPosition(self, oldPos);
        xtc_notifyCharFormat(self);
        xtc_notifySelection(self, false);
    } else {
        xtc_notifyCursorPosition(self, oldPos);
        xtc_notifySelection(self, false);
    }
    xtc_repaintOldAndNewSelection(self, oldPos, oldAnchor);
    self->m_hadSelectionOnMousePress = xtc_hasSelection(self);
}

/**
 * @brief      鼠标移动（对标 mouseMoveEvent：linkHovered 去重发射、
 *             拖选扩展、起拖判定、preedit 起点变更提交）。
 */
static void xtc_mouseMoveEvent(XTextControl* self, XMouseEvent* e)
{
    int flags = self->m_interactionFlags;
    XPoint mousePos;
    if (!self || !e) return;
    mousePos = e->m_position;

    if (flags & (int)XTextControlInteraction_LinksAccessibleByMouse) {
        XString* anchor = XTextControl_anchorAt(self, &mousePos);
        const char* utf8 = anchor ? XString_toUtf8(anchor) : NULL;
        const char* current = self->m_highlightedAnchor;
        bool hadCurrent = current && current[0];
        bool hasNew = utf8 && utf8[0];
        bool same = (hadCurrent && hasNew) ? XStrcmp(utf8, current) == 0
                                           : (hadCurrent == hasNew);
        if (!same) {
            if (self->m_highlightedAnchor) {
                XFree_System(self->m_highlightedAnchor);
                self->m_highlightedAnchor = NULL;
            }
            if (hasNew)
                self->m_highlightedAnchor = xtc_strdupN(utf8, -1);
            xtc_emitString(self, XTextControl_linkHovered_signal,
                           hasNew ? utf8 : "");
        }
        if (anchor) XString_delete_base((XClass*)anchor);
    }

    if (e->m_buttons & (int)XMouseButton_LeftButton) {
        bool editable = (flags & (int)XTextControlInteraction_TextEditable) != 0;
        int oldPos;
        int oldAnchor;
        int newCursorPos;
        if (!(self->m_mousePressed || editable || self->m_mightStartDrag ||
              self->m_wordSelStart >= 0 || self->m_blockSelStart >= 0))
            return;
        oldPos = self->m_cursorPosition;
        oldAnchor = self->m_cursorAnchor;
        if (self->m_mightStartDrag) {
            if (xtc_manhattan(&mousePos, &self->m_mousePressPos) >
                XTC_START_DRAG_DISTANCE)
                xtc_startDrag(self);
            return;
        }
        newCursorPos = XTextControl_hitTest(
            self, &mousePos, (int)XTextControlHitTestAccuracy_FuzzyHit);
        if (xtc_isPreediting(self)) {
            XPoint pressPos = self->m_mousePressPos;
            int selectionStartPos = XTextControl_hitTest(
                self, &pressPos, (int)XTextControlHitTestAccuracy_FuzzyHit);
            if (newCursorPos != selectionStartPos) {
                xtc_commitPreedit(self);
                newCursorPos = XTextControl_hitTest(
                    self, &mousePos, (int)XTextControlHitTestAccuracy_FuzzyHit);
            }
        }
        if (newCursorPos < 0) return;
        if (self->m_mousePressed && self->m_wordSelectionEnabled &&
            self->m_wordSelStart < 0) {
            xtc_wordRangeUnder(self, self->m_cursorPosition,
                               &self->m_wordSelStart, &self->m_wordSelEnd);
        }
        if (self->m_blockSelStart >= 0)
            xtc_extendBlockwise(self, newCursorPos);
        else if (self->m_wordSelStart >= 0)
            xtc_extendWordwise(self, newCursorPos, mousePos.x);
        else if (self->m_mousePressed && !xtc_isPreediting(self))
            xtc_setCursorPos(self, newCursorPos,
                             (int)XTextControlMoveMode_KeepAnchor);

        xtc_notifyCursorPosition(self, oldPos);
        xtc_notifySelection(self, true);
        xtc_repaintOldAndNewSelection(self, oldPos, oldAnchor);
    }
}

/**
 * @brief      鼠标释放（对标 mouseReleaseEvent：起拖取消、拖选收尾、
 *             链接激活；中键粘贴不在 release 侧——见函数体内 R-33 注释，
 *             单点在 press 侧 xtc_middleClickPaste）。
 */
static void xtc_mouseReleaseEvent(XTextControl* self, XMouseEvent* e)
{
    int flags = self->m_interactionFlags;
    XPoint pos;
    int button;
    int oldPos;
    int oldAnchor;
    if (!self || !e) return;
    pos = e->m_position;
    button = (int)e->m_button;
    oldPos = self->m_cursorPosition;
    oldAnchor = self->m_cursorAnchor;

    if (self->m_mightStartDrag && (button & (int)XMouseButton_LeftButton)) {
        self->m_mousePressed = false;
        xtc_setCursorPos(self, XTextControl_hitTest(
                                   self, &pos,
                                   (int)XTextControlHitTestAccuracy_FuzzyHit),
                         (int)XTextControlMoveMode_MoveAnchor);
        self->m_cursorAnchor = self->m_cursorPosition;
        xtc_notifySelection(self, false);
    }
    if (self->m_mousePressed) {
        self->m_mousePressed = false;
        xtc_notifySelection(self, true);
    }
    /* 根因修复（R-33）：此处旧有中键遗留粘贴分支（共享剪贴板文本再插
     * 一次），与 press 侧 xtc_middleClickPaste 的 PRIMARY 单点粘贴对同
     * 一次中键各插一次，两份文本叠加入文档。对标 Qt 6.8.3
     * QWidgetTextControlPrivate::mouseReleaseEvent——中键粘贴仅存在于
     * mousePressEvent（Selection 模式 + supportsSelection 门禁），
     * release 侧无中键分支；故删除遗留路径，保留 press 侧单路径。 */

    xtc_repaintOldAndNewSelection(self, oldPos, oldAnchor);
    xtc_notifyCursorPosition(self, oldPos);

    if (flags & (int)XTextControlInteraction_LinksAccessibleByMouse) {
        XString* anchor;
        const char* utf8;
        if (!(button & (int)XMouseButton_LeftButton)) {
            XEvent_ignore((XEvent*)e);
            return;
        }
        anchor = XTextControl_anchorAt(self, &pos);
        utf8 = anchor ? XString_toUtf8(anchor) : NULL;
        if (!utf8 || !utf8[0]) {
            if (anchor) XString_delete_base((XClass*)anchor);
            XEvent_ignore((XEvent*)e);
            return;
        }
        if (!xtc_hasSelection(self) ||
            (self->m_anchorOnMousePress &&
             XStrcmp(utf8, self->m_anchorOnMousePress) == 0 &&
             self->m_hadSelectionOnMousePress)) {
            int anchorPos = XTextControl_hitTest(
                self, &pos, (int)XTextControlHitTestAccuracy_ExactHit);
            if (anchorPos < 0) {
                if (anchor) XString_delete_base((XClass*)anchor);
                XEvent_ignore((XEvent*)e);
                return;
            }
            xtc_setCursorPos(self, anchorPos,
                             (int)XTextControlMoveMode_MoveAnchor);
            if (self->m_anchorOnMousePress) {
                XFree_System(self->m_anchorOnMousePress);
                self->m_anchorOnMousePress = NULL;
            }
            xtc_activateLinkUnderCursor(self);
        }
        if (anchor) XString_delete_base((XClass*)anchor);
    }
}

/**
 * @brief      鼠标双击（对标 mouseDoubleClickEvent：词选 + 三击判定起表）。
 */
static void xtc_mouseDoubleClickEvent(XTextControl* self, XMouseEvent* e)
{
    int flags = self->m_interactionFlags;
    XPoint pos;
    int button;
    if (!self || !e) return;
    pos = e->m_position;
    button = (int)e->m_button;
    if (button == (int)XMouseButton_LeftButton &&
        (flags & (int)XTextControlInteraction_TextSelectableByMouse)) {
        int oldPos = self->m_cursorPosition;
        int oldAnchor = self->m_cursorAnchor;
        bool doEmit = false;
        self->m_mightStartDrag = false;
        xtc_commitPreedit(self);
        xtc_setCursorPos(self, XTextControl_hitTest(
                                   self, &pos,
                                   (int)XTextControlHitTestAccuracy_FuzzyHit),
                         (int)XTextControlMoveMode_MoveAnchor);
        {
            int line;
            xtc_posToLineCol(self, self->m_cursorPosition, &line, NULL);
            if (xtc_lineLen(self, line) > 0) {
                int ws = 0;
                int we = 0;
                xtc_wordRangeUnder(self, self->m_cursorPosition, &ws, &we);
                if (we > ws) {
                    self->m_cursorPosition = we;
                    self->m_cursorAnchor = ws;
                    doEmit = true;
                }
            }
        }
        xtc_repaintOldAndNewSelection(self, oldPos, oldAnchor);
        self->m_cursorIsFocusIndicator = false;
        if (xtc_hasSelection(self)) {
            self->m_wordSelStart = xtc_selectionStart(self);
            self->m_wordSelEnd = xtc_selectionEnd(self);
        } else {
            self->m_wordSelStart = -1;
            self->m_wordSelEnd = -1;
        }
        self->m_tripleClickPoint = pos;
        if (self->m_tripleClickTimer != XTIMER_INVALID_ID)
            XObject_killTimer((XObject*)self, self->m_tripleClickTimer);
        self->m_tripleClickTimer = XObject_startTimer_ms(
            (XObject*)self, XTC_TRIPLE_CLICK_MS, XTimerType_CoarseTimer);
        if (doEmit) {
            xtc_notifySelection(self, false);
            xtc_emitVoid(self, XTextControl_cursorPositionChanged_signal);
        }
    } else {
        XEvent_ignore((XEvent*)e);
    }
}

/* ==================== IME（对标 inputMethodEvent / commitPreedit） ==== */

/**
 * @brief      输入法事件（对标 inputMethodEvent：提交串插入、替换区间、
 *             preedit 生命周期、组合光标与 microFocus 通知）。
 * @details    preedit 不入文档（与 Qt 一致）：组合串旁路挂载在光标处，
 *             参与命中测试/绘制；提交串经撤销跟踪写入文档。
 */
#if XINPUTMETHOD_ON
static void xtc_inputMethodEvent(XTextControl* self, XInputMethodEvent* e)
{
    int flags;
    const char* commit = NULL;
    const char* preedit = NULL;
    bool preeditChanged;
    bool isGettingInput;
    int oldCursorPos;
    int oldPreeditCursor;
    if (!self || !e) return;
    flags = self->m_interactionFlags;
    if (!((flags & (int)XTextControlInteraction_TextEditable) ||
          (flags & (int)XTextControlInteraction_TextSelectableByMouse))) {
        XEvent_ignore((XEvent*)e);
        return;
    }
    if (e->m_commitString)
        commit = XString_toUtf8(e->m_commitString);
    if (e->m_preeditString)
        preedit = XString_toUtf8(e->m_preeditString);
    {
        const char* current = self->m_preedit;
        bool hadCurrent = current && current[0];
        bool hasNew = preedit && preedit[0];
        preeditChanged = (hadCurrent && hasNew) ? XStrcmp(preedit, current) != 0
                                                : (hadCurrent != hasNew);
    }
    isGettingInput = (commit && commit[0]) || preeditChanged ||
                     e->m_replacementLength > 0;
    if (!isGettingInput && !preeditChanged) {
        XEvent_ignore((XEvent*)e);
        return;
    }

    oldCursorPos = self->m_cursorPosition;
    oldPreeditCursor = self->m_preeditCursor;

    if (isGettingInput) {
        int group = xtc_groupBegin(self);
        xtc_removeSelectedText(self, group);
        if ((commit && commit[0]) || e->m_replacementLength != 0) {
            int base = self->m_cursorPosition;
            int start = base + e->m_replacementStart;
            int len = e->m_replacementLength;
            if (start < 0) {
                len += start;
                start = 0;
            }
            if (start > xtc_documentLength(self))
                start = xtc_documentLength(self);
            if (len > 0) {
                if (start + len > xtc_documentLength(self))
                    len = xtc_documentLength(self) - start;
                if (len > 0)
                    xtc_editRemove(self, start, len, group);
            }
            if (commit && commit[0]) {
                xtc_editInsert(self, start, commit, group);
                self->m_cursorPosition = self->m_cursorAnchor =
                    start + (int)XStrlen(commit);
            }
        }
        xtc_groupEnd(self);
    }

    /* preedit 生命周期：有组合串则挂到光标处，否则收尾。 */
    if (preedit && preedit[0]) {
        if (self->m_preedit) XFree_System(self->m_preedit);
        self->m_preedit = xtc_strdupN(preedit, -1);
        self->m_preeditPos = self->m_cursorPosition;
        self->m_preeditCursor = (e->m_cursorPosition >= 0)
                                    ? e->m_cursorPosition
                                    : (int)XStrlen(preedit);
        self->m_hideCursor = false;
        /* 组合串计入布局（IME 预编辑参与折行）：失效缓存并同步内容
           高度（组合串可能增减可视行数，对标 Qt preeditArea 布局）。 */
        {
            int oldHeight = xtc_contentHeight(self);
            xtc_invalidateLayout(self);
            {
                int newHeight = xtc_contentHeight(self);
                if (newHeight != oldHeight) {
                    XSize size = XTextControl_size(self);
                    xtc_emitSize(self,
                                 XTextControl_documentSizeChanged_signal,
                                 &size);
                }
            }
        }
    } else {
        xtc_clearPreeditState(self);
    }

    xtc_repaintCursor(self);
    xtc_notifyCursorPosition(self, oldCursorPos);
    if (oldPreeditCursor != self->m_preeditCursor)
        xtc_emitVoid(self, XTextControl_microFocusChanged_signal);
    XEvent_accept((XEvent*)e);
}
#endif /* XINPUTMETHOD_ON */

/* ==================== 拖放（对标 dragEnter/Move/Leave/Drop） ==== */

#if XWINDOWEVENT_ON
/**
 * @brief      拖放事件路由（对标 Private::dragEnterEvent / dragMoveEvent /
 *             dragLeaveEvent / dropEvent 的平铺承载）。
 */
static void xtc_dropEventRoute(XTextControl* self, XDropEvent* e)
{
    int type;
    const char* data = NULL;
    if (!self || !e) return;
    type = XEvent_type((XEvent*)e);
    if (e->m_data)
        data = XString_toUtf8(e->m_data);
    if (!(self->m_interactionFlags &
          (int)XTextControlInteraction_TextEditable) ||
        !XTextControl_canInsertFromMimeData(self, data)) {
        XEvent_ignore((XEvent*)e);
        return;
    }
    switch (type) {
    case XEVENT_TYPE_DRAG_ENTER:
        self->m_dndFeedbackPos = -1;
        XEvent_accept((XEvent*)e);
        break;
    case XEVENT_TYPE_DRAG_MOVE: {
        XPoint pos = XDropEvent_position(e);
        int cursorPos = XTextControl_hitTest(
            self, &pos, (int)XTextControlHitTestAccuracy_FuzzyHit);
        if (cursorPos >= 0) {
            XRect r;
            if (self->m_dndFeedbackPos >= 0) {
                r = XTextControl_cursorRectAt(self, self->m_dndFeedbackPos);
                xtc_emitRect(self, XTextControl_updateRequest_signal, &r);
            }
            self->m_dndFeedbackPos = cursorPos;
            r = XTextControl_cursorRectAt(self, cursorPos);
            xtc_emitRect(self, XTextControl_updateRequest_signal, &r);
        }
        XEvent_accept((XEvent*)e);
        break;
    }
    case XEVENT_TYPE_DRAG_LEAVE: {
        XRect r;
        if (self->m_dndFeedbackPos >= 0) {
            r = XTextControl_cursorRectAt(self, self->m_dndFeedbackPos);
            xtc_emitRect(self, XTextControl_updateRequest_signal, &r);
        }
        self->m_dndFeedbackPos = -1;
        break;
    }
    case XEVENT_TYPE_DROP: {
        XPoint pos = XDropEvent_position(e);
        int dropPos = XTextControl_cursorForPosition(self, &pos);
        int group;
        self->m_dndFeedbackPos = -1;
        group = xtc_groupBegin(self);
        /* 自发起移动放置：先删源选区（对标 MoveAction && source == self）。 */
        if ((e->m_dropAction == (int)XDropAction_MoveAction) &&
            self->m_dragInProgress)
            xtc_removeSelectedText(self, group);
        self->m_dragInProgress = false;
        xtc_setCursorPos(self, dropPos, (int)XTextControlMoveMode_MoveAnchor);
        XTextControl_insertFromMimeData(self, data);
        xtc_groupEnd(self);
        XTextControl_ensureCursorVisible(self);
        XEvent_accept((XEvent*)e);
        break;
    }
    default:
        break;
    }
}
#endif /* XWINDOWEVENT_ON */

/* ==================== 焦点 / 定时器（对标 focusEvent / timerEvent） ==== */

#if XWINDOWEVENT_ON
/**
 * @brief      焦点事件（对标 focusEvent：光标可见性、焦点指示选区清理）。
 */
static void xtc_focusEvent(XTextControl* self, XFocusEvent* e)
{
    XRect selRect;
    if (!self || !e) return;
    selRect = XTextControl_selectionRect(self);
    xtc_emitRect(self, XTextControl_updateRequest_signal, &selRect);
    if (XFocusEvent_gotFocus(e)) {
        self->m_cursorOn =
            (self->m_interactionFlags &
             ((int)XTextControlInteraction_TextSelectableByKeyboard |
              (int)XTextControlInteraction_TextEditable)) != 0;
        if (self->m_interactionFlags &
            (int)XTextControlInteraction_TextEditable)
            xtc_setCursorVisible(self, true);
    } else {
        xtc_setCursorVisible(self, false);
        self->m_cursorOn = false;
        if (self->m_cursorIsFocusIndicator &&
            XFocusEvent_reason(e) != XFocusReason_ActiveWindow &&
            XFocusEvent_reason(e) != XFocusReason_Popup &&
            xtc_hasSelection(self)) {
            self->m_cursorAnchor = self->m_cursorPosition;
        }
    }
    self->m_hasFocus = XFocusEvent_gotFocus(e);
}
#endif /* XWINDOWEVENT_ON */

/**
 * @brief      定时器事件（对标 timerEvent：光标闪烁 + 三击判定截止）。
 */
static void VXTextControl_timerEvent(XObject* object, XTimerEvent* event)
{
    XTextControl* self = (XTextControl*)object;
    if (!self || !event) return;
    if (XTimerEvent_timerId(event) == self->m_cursorBlinkTimer) {
        self->m_cursorOn = !self->m_cursorOn;
        xtc_repaintCursor(self);
        XEvent_accept((XEvent*)event);
        return;
    }
    if (XTimerEvent_timerId(event) == self->m_tripleClickTimer) {
        XObject_killTimer((XObject*)self, self->m_tripleClickTimer);
        self->m_tripleClickTimer = XTIMER_INVALID_ID;
        XEvent_accept((XEvent*)event);
        return;
    }
    XClass_Parent(XObject, EXObject_TimerEvent,
                  void (*)(XObject*, XTimerEvent*))(object, event);
}

/** @brief QObject::event 透传（对标 QWidgetTextControl::event）。
 * @details 必须经 XClass_Parent 调父类（XObject）实现——XObject_event_base
 *          会重新取最派生类虚槽（即本函数），构成无限递归栈溢出
 *          （进入编辑启动光标闪烁定时器，首个定时器事件即引爆，
 *          2026-09-19 多行编辑页实机复现）。 */
static bool VXTextControl_objectEvent(XObject* object, XEvent* event)
{
    return XClass_Parent(XObject, EXObject_Event,
                         bool (*)(XObject*, XEvent*))(object, event);
}

/* ==================== HTML 子集解析（对标 mightBeRichText / HTML 承载） ==== */

/** @brief Qt mightBeRichText 的平铺近似：首个非空白字符为 '<'。 */
static bool xtc_mightBeRichText(const char* text)
{
    int i = 0;
    if (!text) return false;
    while (text[i] == ' ' || text[i] == '\t' || text[i] == '\r' || text[i] == '\n')
        ++i;
    return text[i] == '<';
}

/** @brief HTML 转义（toHtml 承载）。 */
static char* xtc_escapeHtml(const char* src, int len)
{
    char* out = (char*)XMalloc_System((size_t)len * 6 + 1);
    int i = 0;
    int o = 0;
    if (!out) return NULL;
    while (i < len) {
        unsigned char c = (unsigned char)src[i];
        if (c == '&') {
            XMemcpy(out + o, "&amp;", 5);
            o += 5;
        } else if (c == '<') {
            XMemcpy(out + o, "&lt;", 4);
            o += 4;
        } else if (c == '>') {
            XMemcpy(out + o, "&gt;", 4);
            o += 4;
        } else if (c == '"') {
            XMemcpy(out + o, "&quot;", 6);
            o += 6;
        } else {
            out[o++] = (char)c;
        }
        ++i;
    }
    out[o] = '\0';
    return out;
}

/** @brief 实体解码：输出一个字节，返回消耗的输入字节数（0 = 未识别）。 */
static int xtc_decodeEntity(const char* src, char* out)
{
    if (src[0] != '&') return 0;
    if (XStrncmp(src, "&amp;", 5) == 0) { out[0] = '&'; return 5; }
    if (XStrncmp(src, "&lt;", 4) == 0) { out[0] = '<'; return 4; }
    if (XStrncmp(src, "&gt;", 4) == 0) { out[0] = '>'; return 4; }
    if (XStrncmp(src, "&quot;", 6) == 0) { out[0] = '"'; return 6; }
    if (XStrncmp(src, "&apos;", 6) == 0) { out[0] = '\''; return 6; }
    if (XStrncmp(src, "&nbsp;", 6) == 0) { out[0] = ' '; return 6; }
    return 0;
}

/** @brief HTML 解析锚点登记回调上下文（区间随 base 平移登记）。 */
typedef struct XtcAnchorCollect
{
    XTextControl* self;
    int base;
} XtcAnchorCollect;

/** @brief 锚点登记回调。 */
static void xtc_anchorCollectToControl(void* ud, int start, int end,
                                       const char* href)
{
    XtcAnchorCollect* ctx = (XtcAnchorCollect*)ud;
    xtc_addAnchor(ctx->self, ctx->base + start, ctx->base + end, href);
}

/**
 * @brief      HTML 子集解析为纯文本并回收锚点（对标 QTextDocumentFragment::
 *             fromHtml 的平铺近似）。
 * @details    支持：<br>（换行）、<p>/</p>/<div>/</div>/<li>/</li>（段落
 *             边界换行）、<a href="...">...</a>（经 collect 回调登记相对
 *             区间）、常用实体；其余标签剥离、注释整体跳过。
 * @param      collect 锚点回收回调；NULL 时忽略锚点。
 */
static char* xtc_parseHtmlSubset(const char* html,
                                 void (*collect)(void* ud, int start, int end,
                                                 const char* href),
                                 void* ud)
{
    size_t cap;
    char* out;
    int o = 0;
    int i = 0;
    int anchorStart = -1;
    char* pendingHref = NULL;
    cap = (XStrlen(html) + 1) * 2 + 16;
    out = (char*)XMalloc_System(cap);
    if (!out) return NULL;
    if (!html) {
        out[0] = '\0';
        return out;
    }
#define XTC_HTML_GROW()                                              \
    do {                                                             \
        char* grown_ = (char*)XRealloc_System(out, cap * 2);         \
        if (!grown_) {                                               \
            out[o] = '\0';                                           \
            if (pendingHref) XFree_System(pendingHref);              \
            return out;                                              \
        }                                                            \
        out = grown_;                                                \
        cap *= 2;                                                    \
    } while (0)
    while (html[i] != '\0') {
        if (html[i] == '<') {
            const char* close = XStrchr(html + i, '>');
            int tagLen;
            if (!close) break;
            tagLen = (int)(close - (html + i)) + 1;
            /* 注释整体跳过。 */
            if (XStrncmp(html + i, "<!--", 4) == 0) {
                const char* commentEnd = XStrstr(html + i, "-->");
                if (!commentEnd) break;
                i = (int)(commentEnd - html) + 3;
                continue;
            }
            if (XStrncmp(html + i, "<a ", 3) == 0 ||
                XStrncmp(html + i, "<a>", 3) == 0) {
                const char* href = XStrstr(html + i, "href");
                anchorStart = o;
                if (pendingHref) {
                    XFree_System(pendingHref);
                    pendingHref = NULL;
                }
                if (href) {
                    const char* eq = XStrchr(href, '=');
                    if (eq) {
                        char quote = eq[1];
                        const char* vstart = NULL;
                        const char* vend = NULL;
                        if (quote == '"' || quote == '\'') {
                            vstart = eq + 2;
                            vend = XStrchr(vstart, quote);
                        } else {
                            vstart = eq + 1;
                            vend = vstart;
                            while (*vend && *vend != ' ' && *vend != '>')
                                ++vend;
                        }
                        if (vstart && vend && vend > vstart)
                            pendingHref =
                                xtc_strdupN(vstart, (int)(vend - vstart));
                    }
                }
            } else if (XStrncmp(html + i, "</a", 3) == 0) {
                if (anchorStart >= 0 && o > anchorStart && collect)
                    collect(ud, anchorStart, o, pendingHref);
                anchorStart = -1;
                if (pendingHref) {
                    XFree_System(pendingHref);
                    pendingHref = NULL;
                }
            } else if (XStrncmp(html + i, "<br", 3) == 0) {
                if (o + 8 >= (int)cap) XTC_HTML_GROW();
                out[o++] = '\n';
            } else if (XStrncmp(html + i, "<p", 2) == 0 ||
                       XStrncmp(html + i, "</p", 3) == 0 ||
                       XStrncmp(html + i, "<div", 4) == 0 ||
                       XStrncmp(html + i, "</div", 5) == 0 ||
                       XStrncmp(html + i, "<li", 3) == 0 ||
                       XStrncmp(html + i, "</li", 4) == 0) {
                if (o > 0 && out[o - 1] != '\n') {
                    if (o + 8 >= (int)cap) XTC_HTML_GROW();
                    out[o++] = '\n';
                }
            }
            i += tagLen;
            continue;
        }
        if (html[i] == '&') {
            char decoded = 0;
            int used = xtc_decodeEntity(html + i, &decoded);
            if (used > 0) {
                if (o + 8 >= (int)cap) XTC_HTML_GROW();
                out[o++] = decoded;
                i += used;
                continue;
            }
        }
        if (o + 8 >= (int)cap) XTC_HTML_GROW();
        out[o++] = html[i];
        ++i;
    }
    out[o] = '\0';
    if (pendingHref) XFree_System(pendingHref);
    return out;
#undef XTC_HTML_GROW
}

/* ==================== 公共 API：命中测试 / 几何 ==================== */

int XTextControl_hitTest(const XTextControl* self, const XPoint* point,
                         int accuracy)
{
    XFont font;
    int row;
    int line;
    char* heap = NULL;
    const char* text;
    int base;
    int to;
    int off;
    int x;
    const XTextControlVisualRow* vr;
    if (!self || !point || self->m_lineCount <= 0) return -1;
    xtc_ensureLayout((XTextControl*)self);
    if (self->m_visualCount <= 0) return -1;
    /* 对标 Qt hitTest：y 反查可视行（软换行行），x 反查行内列。 */
    row = point->y / (self->m_lineHeight > 0 ? self->m_lineHeight : 1);
    if (row < 0) row = 0;
    if (row >= self->m_visualCount) row = self->m_visualCount - 1;
    vr = &self->m_visualRows[row];
    line = vr->line;
    base = vr->start;
    to = vr->start + vr->len;
    text = xtc_visualLineText(self, line, &heap);
    /* 只读浅拷贝:与 self->m_font 共享 m_family/m_styleName,使用后禁止
       deinit(会释放控制器自有字符串,下一读点即踩悬垂指针)。 */
    XMemcpy(&font, &self->m_font, sizeof(XFont));
    x = point->x;
    off = base;
    if (x > 0 && to > base) {
        while (off < to) {
            int seq = XTextUtf8_seqLen(text + off, to - off);
            int w = XPainter_textWidthRange(&font, text, off, off + seq);
            if (w < 0) w = 0;
            if (x < w) break;
            x -= w;
            off += seq;
        }
    }
    if (heap) XFree_System(heap);
    /* 浅拷贝不拥有堆串:无需释放(所有权在 self->m_font)。 */
    if (accuracy == (int)XTextControlHitTestAccuracy_ExactHit) {
        /* 精确命中：须落在文本区（[0, 可视行宽]），空行仅 x<=0。 */
        if (vr->len > 0) {
            if (point->x < 0 || point->x > vr->width) return -1;
        } else if (point->x > 0) {
            return -1;
        }
    }
    {
        int vcol = off;
        int docCol = xtc_visualColToDocCol(self, line, vcol);
        return xtc_lineColToPos(self, line, docCol);
    }
}

XRect XTextControl_blockBoundingRect(const XTextControl* self, int line)
{
    XRect r;
    int width;
    int row;
    int rowEnd;
    int rowsInBlock;
    if (!self || line < 0 || line >= self->m_lineCount) {
        XRect_init(&r, 0, 0, 0, 0);
        return r;
    }
    xtc_ensureLayout((XTextControl*)self);
    /* 对标 Qt blockBoundingRect：条带覆盖该块全部可视行（软换行计入）。 */
    row = xtc_rowLowerBound(self, line);
    rowEnd = row;
    while (rowEnd < self->m_visualCount &&
           self->m_visualRows[rowEnd].line == line)
        ++rowEnd;
    rowsInBlock = rowEnd - row;
    if (rowsInBlock < 1) rowsInBlock = 1;
    width = self->m_textWidth;
    XRect_init(&r, 0, row * self->m_lineHeight,
               width > 0 ? width : 0x7FFFFFF0,
               rowsInBlock * self->m_lineHeight);
    return r;
}

XRect XTextControl_cursorRectAt(const XTextControl* self, int position)
{
    XRect r;
    XFont font;
    int line;
    int col;
    const char* text;
    int lineLen;
    int visualCol;
    int x;
    int row;
    char* heap = NULL;
    const char* vtext;
    const XTextControlVisualRow* vr;
    if (!self) {
        XRect_init(&r, 0, 0, 0, 0);
        return r;
    }
    if (position < 0) position = 0;
    if (position > xtc_documentLength(self))
        position = xtc_documentLength(self);
    xtc_posToLineCol(self, position, &line, &col);
    text = xtc_lineText(self, line);
    lineLen = xtc_lineLen(self, line);
    /* rectForPosition 的 preedit 调整：插入点光标映射进组合区
       （relativePos == preeditPos → += preeditCursor）。 */
    {
        int pLine;
        int pCol;
        if (xtc_preeditLineCol(self, &pLine, &pCol) && pLine == line &&
            position == self->m_preeditPos)
            visualCol = pCol + self->m_preeditCursor;
        else
            visualCol = xtc_docColToVisualCol(self, line, col);
    }
    xtc_ensureLayout((XTextControl*)self);
    if (self->m_visualCount <= 0 || !self->m_visualRows) {
        /* 布局兜底（重建失败等）：退回逻辑行条带。 */
        XRect_init(&r, 0, line * self->m_lineHeight,
                   self->m_cursorWidth > 0 ? self->m_cursorWidth : 1,
                   self->m_lineHeight);
        return r;
    }
    row = xtc_rowLowerBound(self, line);
    while (row + 1 < self->m_visualCount &&
           self->m_visualRows[row].line == line &&
           visualCol > self->m_visualRows[row].start +
                           self->m_visualRows[row].len)
        ++row;
    if (row >= self->m_visualCount) row = self->m_visualCount > 0
                                              ? self->m_visualCount - 1 : 0;
    /* 软断点歧义：Home 边沿把行首字节渲染到下一可视行行首
       （与 posToVisualRowCol 同一归属规则）。 */
    if (position == self->m_cursorPosition && self->m_cursorAtRowStart &&
        row + 1 < self->m_visualCount &&
        visualCol == self->m_visualRows[row].start +
                         self->m_visualRows[row].len &&
        visualCol < xtc_visualLineLen(self, line))
        ++row;
    vr = &self->m_visualRows[row];
    vtext = xtc_visualLineText(self, line, &heap);
    /* 只读浅拷贝:与 self->m_font 共享 m_family/m_styleName,使用后禁止
       deinit(会释放控制器自有字符串,下一读点即踩悬垂指针)。 */
    XMemcpy(&font, &self->m_font, sizeof(XFont));
    /* X 按可视行内偏移度量（软换行行 X 归零，对标 Qt 行内光标 X）。 */
    x = XPainter_textWidthRange(&font, vtext, vr->start,
                                visualCol < vr->start ? vr->start : visualCol);
    if (self->m_overwriteMode && col < lineLen) {
        int seq = XTextUtf8_seqLen(text + col, lineLen - col);
        x += XPainter_textWidthRange(&font, text, col, col + seq);
    }
    if (heap) XFree_System(heap);
    /* 浅拷贝不拥有堆串:无需释放(所有权在 self->m_font)。 */
    XRect_init(&r, x, row * self->m_lineHeight,
               self->m_cursorWidth > 0 ? self->m_cursorWidth : 1,
               self->m_lineHeight);
    return r;
}

XRect XTextControl_cursorRect(const XTextControl* self)
{
    XRect r;
    if (!self) {
        XRect_init(&r, 0, 0, 0, 0);
        return r;
    }
    return XTextControl_cursorRectAt(self, self->m_cursorPosition);
}

XRect XTextControl_selectionRect(const XTextControl* self)
{
    if (!self) {
        XRect r;
        XRect_init(&r, 0, 0, 0, 0);
        return r;
    }
    return xtc_selectionRectImpl(self, self->m_cursorPosition,
                                 self->m_cursorAnchor);
}

XRect XTextControl_selectionRectAt(const XTextControl* self, int position,
                                   int anchor)
{
    return xtc_selectionRectImpl(self, position, anchor);
}

int XTextControl_cursorForPosition(const XTextControl* self, const XPoint* pos)
{
    return XTextControl_hitTest(self, pos,
                                (int)XTextControlHitTestAccuracy_FuzzyHit);
}

/* ==================== 公共 API：绘制（对标 drawContents） ==================== */

/** @brief 递增有序边界数组插入（去重；容量满时忽略）。 */
static void xtc_boundsAdd(int* bounds, int* count, int value)
{
    int i;
    int j;
    if (*count >= XTC_MAX_DRAW_BOUNDS) return;
    for (i = 0; i < *count; ++i) {
        if (bounds[i] == value) return;
        if (bounds[i] > value) break;
    }
    for (j = *count; j > i; --j)
        bounds[j] = bounds[j - 1];
    bounds[i] = value;
    ++(*count);
}

/** @brief 绘制一段文本（逐码点输出；返回结束 X）。 */
static int xtc_drawRun(XPainter* painter, const XFont* font, const char* text,
                       int from, int to, int x, int baseline, uint32_t color)
{
    int off = from;
    int cx = x;
    int len = (int)XStrlen(text);
    if (from < 0) from = 0;
    if (to > len) to = len;
    while (off < to) {
        int seq = XTextUtf8_seqLen(text + off, to - off);
        int w = XPainter_textWidthRange(font, text, off, off + seq);
        if (w < 0) w = 0;
        XPainter_drawGlyph(painter, cx, baseline, text + off, color);
        cx += w;
        off += seq > 0 ? seq : 1;
    }
    return cx;
}

/**
 * @brief      绘制入口（对标 drawContents → layout draw + PaintContext）。
 * @details    逐可视行渲染（软换行后一行逻辑行折为多可视行，对标
 *             QTextLayout 逐 Line 绘制）：组合行先把 preedit splice 进
 *             视觉缓冲；随后收集分段边界（选区边缘/锚点边缘/preedit
 *             边缘/行端），逐段两色文本 + 选区高亮 + 锚点下划线
 *             （Link 色）+ IME 组合下划线；每行裁剪到"文本区 ∩ 行带"
 *             （任何模式下文本绝不越出边框，视觉兜底）；末尾按 blink
 *             态绘制光标与拖放反馈光标。
 */
void XTextControl_draw(XTextControl* self, XPainter* painter, const XRect* rect)
{
    XFont font;
    uint32_t textColor;
    uint32_t highlight;
    uint32_t highlightedText;
    uint32_t linkColor;
    int firstRow;
    int lastRow;
    int selStart;
    int selEnd;
    int r;
    char* heap = NULL;
    int builtLine = -1;
    const char* builtText = NULL;
    if (!self || !painter || !XPainter_isActive(painter)) return;
    XPainter_save(painter);
#if XPAINTER_CLIP_ON
    if (rect && rect->width > 0 && rect->height > 0)
        XPainter_setClipRect(painter, rect, XPainterClipOperation_IntersectClip);
#endif /* XPAINTER_CLIP_ON */
    /* 只读浅拷贝:与 self->m_font 共享 m_family/m_styleName,使用后禁止
       deinit(会释放控制器自有字符串,下一读点即踩悬垂指针)。 */
    XMemcpy(&font, &self->m_font, sizeof(XFont));
    XPainter_setFont(painter, &font);
#if XPALETTE_ON
    textColor = xtc_paletteColor(self, XPaletteColorRole_Text);
    if (!textColor) textColor = 0xFF000000u;
    highlight = xtc_paletteColor(self, XPaletteColorRole_Highlight);
    if (!highlight) highlight = 0xFF308CC6u;
    highlightedText = xtc_paletteColor(self, XPaletteColorRole_HighlightedText);
    if (!highlightedText) highlightedText = 0xFFFFFFFFu;
    linkColor = xtc_paletteColor(self, XPaletteColorRole_Link);
    if (!linkColor) linkColor = 0xFF0000FFu;
#else
    /* 调色板子系统裁剪：直接取活动路径的常量回退值（与上方 if(!x)
       兜底同口径）。 */
    textColor = 0xFF000000u;
    highlight = 0xFF308CC6u;
    highlightedText = 0xFFFFFFFFu;
    linkColor = 0xFF0000FFu;
#endif /* XPALETTE_ON */

    selStart = 0;
    selEnd = 0;
    if (xtc_hasSelection(self)) {
        selStart = xtc_selectionStart(self);
        selEnd = xtc_selectionEnd(self);
    }

    firstRow = 0;
    lastRow = xtc_visualCountOf(self) - 1;
    if (rect) {
        int unit = self->m_lineHeight > 0 ? self->m_lineHeight : 1;
        firstRow = rect->y / unit;
        lastRow = (rect->y + rect->height) / unit;
        if (firstRow < 0) firstRow = 0;
        if (firstRow > lastRow) firstRow = lastRow;
        if (lastRow >= self->m_visualCount) lastRow = self->m_visualCount - 1;
    }

    for (r = firstRow; r <= lastRow && r >= 0 && r < self->m_visualCount; ++r) {
        const XTextControlVisualRow* row = &self->m_visualRows[r];
        int i = row->line;
        int rowStart = row->start;
        int rowEnd = row->start + row->len;
        int baseline = r * self->m_lineHeight + self->m_lineAscent;
        int pLine = -1;
        int pCol = -1;
        bool isPreeditLine = xtc_preeditLineCol(self, &pLine, &pCol) &&
                             pLine == i;
        int preLen = isPreeditLine ? xtc_preeditLen(self) : 0;
        const char* text;
        int lineStart = xtc_lineColToPos(self, i, 0);
        int lineEnd = lineStart + xtc_lineLen(self, i);
        int selVs = -1;
        int selVe = -1;

        /* 视觉文本（组合行 splice preedit；同行可视行复用缓冲）。 */
        if (i != builtLine) {
            if (heap) {
                XFree_System(heap);
                heap = NULL;
            }
            builtText = xtc_visualLineText(self, i, &heap);
            builtLine = i;
        }
        text = builtText;

        /* 0) 行带裁剪兜底：任何模式下文本绝不越出文本矩形（rect 为
           文本区内容坐标；NoWrap 长行在此被硬裁）。save/restore 防止
           行带裁剪跨行累积（IntersectClip 逐行收缩会清空后续行）。 */
        {
            XRect rowClip;
            XPainter_save(painter);
#if XPAINTER_CLIP_ON
            if (rect)
                XRect_init(&rowClip, rect->x, r * self->m_lineHeight,
                           rect->width, self->m_lineHeight);
            else
                XRect_init(&rowClip, 0, r * self->m_lineHeight, 0x7FFFFFF0,
                           self->m_lineHeight);
            XPainter_setClipRect(painter, &rowClip,
                                 XPainterClipOperation_IntersectClip);
#else
            (void)rowClip; /* 裁剪子系统裁剪：行带裁剪退化无操作。 */
#endif /* XPAINTER_CLIP_ON */
        }

        /* 文档选区 → 本行视觉区间（再钳到本可视行）。 */
        if (selEnd > selStart && lineEnd > lineStart) {
            int rs = selStart < lineStart ? lineStart : selStart;
            int re = selEnd > lineEnd ? lineEnd : selEnd;
            if (re > rs) {
                selVs = xtc_docColToVisualCol(self, i, rs - lineStart);
                selVe = xtc_docColToVisualCol(self, i, re - lineStart);
                if (selVs < rowStart) selVs = rowStart;
                if (selVe > rowEnd) selVe = rowEnd;
                if (selVe < rowStart) selVe = rowStart;
                if (selVs > rowEnd) selVs = rowEnd;
            }
        }

        /* 1) 选区背景（可视行内 X 自行首度量）。 */
        if (selVe > selVs && selVs >= 0) {
            XFont f = font;
            XRect bg;
            XRect_init(&bg,
                       XPainter_textWidthRange(&f, text, rowStart, selVs),
                       r * self->m_lineHeight,
                       XPainter_textWidthRange(&f, text, selVs, selVe),
                       self->m_lineHeight);
            XPainter_fillRect(painter, &bg, highlight);
        }
        /* 2) 额外选择集背景。 */
        {
            int k;
            for (k = 0; k < self->m_extraSelectionCount; ++k) {
                const XTextControlExtraSelection* es =
                    &self->m_extraSelections[k];
                int vs;
                int ve;
                if (es->end <= es->start) continue;
                if (es->end <= lineStart || es->start >= lineEnd) continue;
                {
                    int rs = es->start < lineStart ? lineStart : es->start;
                    int re = es->end > lineEnd ? lineEnd : es->end;
                    vs = xtc_docColToVisualCol(self, i, rs - lineStart);
                    ve = xtc_docColToVisualCol(self, i, re - lineStart);
                }
                if (vs < rowStart) vs = rowStart;
                if (ve > rowEnd) ve = rowEnd;
                if (ve < rowStart) ve = rowStart;
                if (vs > rowEnd) vs = rowEnd;
                if (ve > vs) {
                    XFont f = font;
                    XRect bg;
                    XRect_init(&bg,
                               XPainter_textWidthRange(&f, text, rowStart, vs),
                               r * self->m_lineHeight,
                               XPainter_textWidthRange(&f, text, vs, ve),
                               self->m_lineHeight);
                    XPainter_fillRect(painter, &bg, es->color);
                }
            }
        }

        /* 3) 文本分段着色 + 锚点/组合下划线（边界为行内字节坐标）。 */
        {
            int bounds[XTC_MAX_DRAW_BOUNDS];
            int boundCount = 0;
            int k;
            int x = 0;
            int b;
            xtc_boundsAdd(bounds, &boundCount, rowStart);
            xtc_boundsAdd(bounds, &boundCount, rowEnd);
            if (selVs > rowStart) xtc_boundsAdd(bounds, &boundCount, selVs);
            if (selVe > rowStart && selVe < rowEnd)
                xtc_boundsAdd(bounds, &boundCount, selVe);
            if (isPreeditLine) {
                if (pCol > rowStart && pCol < rowEnd)
                    xtc_boundsAdd(bounds, &boundCount, pCol);
                if (pCol + preLen > rowStart && pCol + preLen < rowEnd)
                    xtc_boundsAdd(bounds, &boundCount, pCol + preLen);
            }
            for (k = 0; k < self->m_anchorCount; ++k) {
                if (self->m_anchors[k].end <= lineStart ||
                    self->m_anchors[k].start >= lineEnd)
                    continue;
                {
                    int as = self->m_anchors[k].start > lineStart
                                 ? self->m_anchors[k].start : lineStart;
                    int ae = self->m_anchors[k].end < lineEnd
                                 ? self->m_anchors[k].end : lineEnd;
                    int vsA = xtc_docColToVisualCol(self, i, as - lineStart);
                    int veA = xtc_docColToVisualCol(self, i, ae - lineStart);
                    if (vsA > rowStart && vsA < rowEnd)
                        xtc_boundsAdd(bounds, &boundCount, vsA);
                    if (veA > rowStart && veA < rowEnd)
                        xtc_boundsAdd(bounds, &boundCount, veA);
                }
            }
            for (b = 0; b + 1 < boundCount; ++b) {
                int from = bounds[b];
                int to = bounds[b + 1];
                int docCol;
                int docPos;
                uint32_t color = textColor;
                bool inAnchor = false;
                bool inPreedit = isPreeditLine && from >= pCol &&
                                 to <= pCol + preLen;
                int xEnd;
                if (to <= from) continue;
                docCol = xtc_visualColToDocCol(self, i, from);
                docPos = xtc_lineColToPos(self, i, docCol);
                if (selVs >= 0 && from >= selVs && to <= selVe)
                    color = highlightedText;
                {
                    int a;
                    for (a = 0; a < self->m_anchorCount; ++a) {
                        if (docPos >= self->m_anchors[a].start &&
                            docPos < self->m_anchors[a].end) {
                            inAnchor = true;
                            color = linkColor;
                            break;
                        }
                    }
                }
                xEnd = xtc_drawRun(painter, &font, text, from, to, x, baseline,
                                   color);
                if (inAnchor) {
                    XRect underline;
                    XRect_init(&underline, x, baseline + 2, xEnd - x, 1);
                    XPainter_fillRect(painter, &underline, linkColor);
                }
                if (inPreedit) {
                    XRect underline;
                    XRect_init(&underline, x,
                               r * self->m_lineHeight + self->m_lineHeight - 2,
                               xEnd - x, 1);
                    XPainter_fillRect(painter, &underline, textColor);
                }
                x = xEnd;
            }
        }
        XPainter_restore(painter); /* 恢复行带裁剪（对标逐行绘制隔离）。 */
    }
    if (heap) XFree_System(heap);

    /* 4) 光标（组合态按 preedit 内偏移；对标 ctx.cursorPosition 语义）。 */
    if (self->m_cursorOn && self->m_isEnabled && !self->m_hideCursor) {
        XRect cr = XTextControl_cursorRect(self);
        XPainter_fillRect(painter, &cr, textColor);
    }
    /* 5) 拖放反馈光标（对标 dndFeedbackCursor）。 */
    if (self->m_dndFeedbackPos >= 0) {
        XRect cr = XTextControl_cursorRectAt(self, self->m_dndFeedbackPos);
        XPainter_fillRect(painter, &cr, textColor);
    }
    /* 浅拷贝不拥有堆串:无需释放(所有权在 self->m_font)。 */
    XPainter_restore(painter);
}

/* ==================== 公共 API：文档与光标 ==================== */

#if XTEXTDOCUMENT_ON
void XTextControl_setDocument(XTextControl* self, XTextDocument* doc)
{
    char* text;
    if (!self || self->m_textDoc == doc) return;
    if (self->m_textDoc)
        XClass_delete_base((XClass*)self->m_textDoc);
    if (doc) {
        self->m_textDoc = doc;
        text = XTextDocument_toPlainText(doc);
    } else {
        self->m_textDoc = XTextDocument_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
        text = NULL;
    }
    XTextControl_setPlainText(self, text ? text : "");
    if (text) XFree_System(text);
}

XTextDocument* XTextControl_document(const XTextControl* self)
{
    return self ? self->m_textDoc : NULL;
}
#endif

void XTextControl_setTextCursor(XTextControl* self, int position, int anchor,
                                bool selectionClipboard)
{
    int oldPos;
    int total;
    if (!self) return;
    (void)selectionClipboard; /* 对标差异：XGui 无 selection 剪贴板。 */
    total = xtc_documentLength(self);
    self->m_cursorIsFocusIndicator = false;
    self->m_cursorAtRowStart = false;
    oldPos = self->m_cursorPosition;
    if (position < 0) position = 0;
    if (position > total) position = total;
    if (anchor < 0) anchor = position;
    if (anchor > total) anchor = total;
    self->m_cursorPosition = position;
    self->m_cursorAnchor = anchor;
    self->m_cursorOn = self->m_hasFocus &&
                       (self->m_interactionFlags &
                        ((int)XTextControlInteraction_TextSelectableByKeyboard |
                         (int)XTextControlInteraction_TextEditable)) != 0;
    xtc_notifyCharFormat(self);
    xtc_notifySelection(self, false);
    XTextControl_ensureCursorVisible(self);
    xtc_repaintOldAndNewSelection(self, oldPos, oldPos);
    xtc_notifyCursorPosition(self, oldPos);
}

void XTextControl_textCursor(const XTextControl* self, int* position, int* anchor)
{
    if (position) *position = self ? self->m_cursorPosition : 0;
    if (anchor) *anchor = self ? self->m_cursorAnchor : 0;
}

void XTextControl_setTextInteractionFlags(XTextControl* self, int flags)
{
    if (!self || flags == self->m_interactionFlags) return;
    self->m_interactionFlags = flags;
    if (self->m_hasFocus)
        xtc_setCursorVisible(self,
                             (flags & (int)XTextControlInteraction_TextEditable) != 0);
}

int XTextControl_textInteractionFlags(const XTextControl* self)
{
    return self ? self->m_interactionFlags : 0;
}

void XTextControl_mergeCurrentCharFormat(XTextControl* self, int modifier)
{
    if (!self) return;
    self->m_charFormat |= modifier;
    xtc_notifyCharFormat(self);
}

void XTextControl_setCurrentCharFormat(XTextControl* self, int format)
{
    if (!self) return;
    self->m_charFormat = format;
    xtc_notifyCharFormat(self);
}

int XTextControl_currentCharFormat(const XTextControl* self)
{
    return self ? self->m_charFormat : 0;
}

/** @brief 文档串内区间匹配（大小写/全词标志；平铺承载 find 的判定核）。 */
static bool xtc_rangeMatches(const char* doc, int total, int p,
                             const char* exp, int expLen,
                             bool caseSensitive, bool wholeWords)
{
    int i;
    if (p < 0 || p + expLen > total) return false;
    if (wholeWords) {
        if (p > 0 && xtc_isWordByte((unsigned char)doc[p - 1])) return false;
        if (p + expLen < total && xtc_isWordByte((unsigned char)doc[p + expLen]))
            return false;
    }
    for (i = 0; i < expLen; ++i) {
        char a = doc[p + i];
        char b = exp[i];
        if (!caseSensitive) {
            a = xtc_lowerChar(a);
            b = xtc_lowerChar(b);
        }
        if (a != b) return false;
    }
    return true;
}

bool XTextControl_find(XTextControl* self, const char* exp, int options)
{
    int total;
    int expLen;
    char* doc;
    bool backward;
    bool caseSensitive;
    bool wholeWords;
    int from;
    int found = -1;
    if (!self || !exp || !exp[0]) return false;
    total = xtc_documentLength(self);
    doc = xtc_getRange(self, 0, total);
    if (!doc) return false;
    expLen = (int)XStrlen(exp);
    backward = (options & (int)XTextControlFindFlag_FindBackward) != 0;
    caseSensitive =
        (options & (int)XTextControlFindFlag_FindCaseSensitively) != 0;
    wholeWords = (options & (int)XTextControlFindFlag_FindWholeWords) != 0;
    /* 起点对标 QTextDocument::find：向前自选区尾、向后自选区头。 */
    from = backward ? xtc_selectionStart(self) - expLen
                    : xtc_selectionEnd(self);
    if (from < 0) from = 0;
    if (from > total) from = total;
    if (backward) {
        int p = from;
        while (p >= 0) {
            if (xtc_rangeMatches(doc, total, p, exp, expLen, caseSensitive,
                                 wholeWords)) {
                found = p;
                break;
            }
            --p;
        }
    } else {
        int p = from;
        while (p + expLen <= total) {
            if (xtc_rangeMatches(doc, total, p, exp, expLen, caseSensitive,
                                 wholeWords)) {
                found = p;
                break;
            }
            ++p;
        }
    }
    XFree_System(doc);
    if (found < 0) return false;
    {
        int oldPos = self->m_cursorPosition;
        int oldAnchor = self->m_cursorAnchor;
        self->m_cursorAnchor = found;
        self->m_cursorPosition = found + expLen;
        xtc_notifyCharFormat(self);
        xtc_notifySelection(self, false);
        XTextControl_ensureCursorVisible(self);
        xtc_emitVoid(self, XTextControl_cursorPositionChanged_signal);
        xtc_repaintOldAndNewSelection(self, oldPos, oldAnchor);
    }
    return true;
}

/* ==================== 公共 API：文本导出/编辑槽 ==================== */

char* XTextControl_toPlainText(const XTextControl* self)
{
    return self ? xtc_getRange(self, 0, xtc_documentLength(self))
                : xtc_strdupN("", 0);
}

char* XTextControl_toHtml(const XTextControl* self)
{
    char* out;
    size_t cap;
    int lineCount;
    int i;
    int o;
    if (!self) return xtc_strdupN("", 0);
    lineCount = self->m_lineCount;
    cap = 64;
    for (i = 0; i < lineCount; ++i)
        cap += (size_t)self->m_lines[i].len * 6 + 16;
    out = (char*)XMalloc_System(cap);
    if (!out) return NULL;
    XMemcpy(out, "<html><body>", 12);
    o = 12;
    for (i = 0; i < lineCount; ++i) {
        const char* line = xtc_lineText(self, i);
        char* escaped = xtc_escapeHtml(line, xtc_lineLen(self, i));
        int n;
        if (!escaped) break;
        n = (int)XStrlen(escaped);
        XMemcpy(out + o, "<p>", 3);
        o += 3;
        XMemcpy(out + o, escaped, (size_t)n);
        o += n;
        XMemcpy(out + o, "</p>", 4);
        o += 4;
        XFree_System(escaped);
    }
    XMemcpy(out + o, "</body></html>", 14);
    o += 14;
    out[o] = '\0';
    return out;
}

char* XTextControl_toMarkdown(const XTextControl* self)
{
    /* 对标差异：无 Markdown 写出器，平铺承载为纯文本透传（不做转义）。 */
    return XTextControl_toPlainText(self);
}

/**
 * @brief      文档整体重置（对标 setContent 的平铺承载）。
 * @details    清撤销/重做历史（装载期禁撤销语义）、清 preedit；行数组
 *             重置为 text 内容、光标归零、modified 复位；随后发射
 *             textChanged/blockCountChanged/documentSizeChanged/
 *             cursorPositionChanged/updateRequest。锚点由调用方决定
 *             （setPlainText 清、setHtml 重登记）。
 */
static void xtc_resetDocument(XTextControl* self, const char* text)
{
    int oldLineCount;
    int i;
    if (!self) return;
    oldLineCount = self->m_lineCount;
    xtc_clearUndoHistory(self);
    xtc_clearPreeditState(self);
    for (i = 0; i < self->m_lineCount; ++i) {
        if (self->m_lines[i].data) XFree_System(self->m_lines[i].data);
        XMemset(&self->m_lines[i], 0, sizeof(XTextControlLine));
    }
    self->m_lineCount = 1;
    /* 行模型已直改（绕过 rawInsert/rawRemove）：可视行缓存整体失效。
       （空文本时下方 rawInsert 早退，不会代为失效。） */
    xtc_invalidateLayout(self);
    self->m_cursorPosition = 0;
    self->m_cursorAnchor = 0;
    self->m_goalCol = -1;
    self->m_wordSelStart = -1;
    self->m_wordSelEnd = -1;
    self->m_blockSelStart = -1;
    self->m_blockSelEnd = -1;
    xtc_rawInsert(self, 0, text ? text : "");
    xtc_setModifiedNotify(self, false);
    xtc_notifyUndoRedo(self);
    xtc_syncDocumentMirror(self);
    xtc_emitVoid(self, XTextControl_textChanged_signal);
    if (self->m_lineCount != oldLineCount)
        xtc_emitInt(self, XTextControl_blockCountChanged_signal,
                    self->m_lineCount);
    {
        XSize size = XTextControl_size(self);
        xtc_emitSize(self, XTextControl_documentSizeChanged_signal, &size);
    }
    xtc_emitRect(self, XTextControl_updateRequest_signal, NULL);
    xtc_notifyCharFormat(self);
    xtc_notifySelection(self, false);
    XTextControl_ensureCursorVisible(self);
    xtc_emitVoid(self, XTextControl_cursorPositionChanged_signal);
}

void XTextControl_setPlainText(XTextControl* self, const char* text)
{
    if (!self) return;
    xtc_clearAnchors(self);
    xtc_resetDocument(self, text);
}

void XTextControl_setHtml(XTextControl* self, const char* text)
{
    char* plain;
    XtcAnchorCollect ctx;
    if (!self) return;
    /* 先解析（回收相对锚点），再重置文档，最后以 base 0 登记。 */
    ctx.self = NULL;
    ctx.base = 0;
    plain = xtc_parseHtmlSubset(text, NULL, &ctx);
    if (!plain) return;
    xtc_clearAnchors(self);
    xtc_resetDocument(self, plain);
    XFree_System(plain);
    ctx.self = self;
    ctx.base = 0;
    plain = xtc_parseHtmlSubset(text, xtc_anchorCollectToControl, &ctx);
    if (plain) XFree_System(plain);
}

void XTextControl_setMarkdown(XTextControl* self, const char* text)
{
    /* 对标差异：无 Markdown 读入器，平铺承载为纯文本装载。 */
    XTextControl_setPlainText(self, text);
}

void XTextControl_cut(XTextControl* self)
{
    if (!self || !(self->m_interactionFlags &
                   (int)XTextControlInteraction_TextEditable) ||
        !xtc_hasSelection(self))
        return;
    XTextControl_copy(self);
    xtc_removeSelectedText(self, -1);
}

void XTextControl_copy(XTextControl* self)
{
    char* text;
    if (!self || !xtc_hasSelection(self)) return;
    text = xtc_getRange(self, xtc_selectionStart(self),
                        xtc_selectionEnd(self) - xtc_selectionStart(self));
    if (!text) return;
    /* 对标 Qt：写 QGuiApplication::clipboard()（统一剪贴板抽象），
     * 跨控件/跨类复制粘贴共享同一数据源；XTextClipboard 镜像一份
     * 供未接 XGuiApplication 的环境回退。 */
#if XCLIPBOARD_ON && XGUIAPPLICATION_ON
    {
        XClipboard* clip = XGuiApplication_clipboard();
        if (clip) {
            XString* st = XString_create_utf8(text);
            if (st) {
                XClipboard_setText(clip, st, (int)XClipboardMode_Clipboard);
                XString_delete_base((XClass*)st);
            }
        }
    }
#endif /* XCLIPBOARD_ON && XGUIAPPLICATION_ON */
    XTextClipboard_setText(text);
    XFree_System(text);
}

/**
 * @brief      按指定剪贴板模式粘贴（对标 QWidgetTextControl::paste
 *             (QClipboard::Mode mode) 的模式参数化通道；插入复用
 *             insertFromMimeData 既有路径，UTF-8 字节口径不变）。
 * @note       只读不写剪贴板：中键 Selection 粘贴不影响 CLIPBOARD 内容
 *             （对标 Qt）。
 */
static void xtc_pasteFromMode(XTextControl* self, int mode)
{
    const char* clip = NULL;
    char* owned = NULL;
    bool systemOnly = false;
    if (!self || !(self->m_interactionFlags &
                   (int)XTextControlInteraction_TextEditable))
        return;
    /* 对标 Qt：优先统一剪贴板（QGuiApplication::clipboard()），
     * 无平台/应用剪贴板时回退 XTextClipboard 兼容层。 */
#if XCLIPBOARD_ON && XGUIAPPLICATION_ON
    {
        XClipboard* clipObj = XGuiApplication_clipboard();
        /* 对标 Qt 中键语义（QWidgetTextControl::paste(Selection) 与
           QWidgetLineControl::paste(Selection) 同护栏）：Selection 模式
           且系统后端可用并声明支持选择区（supportsSelection）时仅取
           系统 PRIMARY，读回为空则整段不动作——不回退共享层，避免 X11
           下中键在 PRIMARY 为空时粘出 CLIPBOARD 旧内容（串台，Qt 明确
           避免）。Clipboard 模式维持「系统剪贴板→共享层」回退链零回归；
           无系统后端（嵌入式裁剪）时 systemOnly 恒 false，Selection 仍
           走共享层回退（同 XLineControl_paste 的 systemOnly 护栏）。 */
        systemOnly = (clipObj != NULL &&
                      mode == (int)XClipboardMode_Selection &&
                      XClipboard_supportsSelection(clipObj));
        if (clipObj) {
            XString* st = XClipboard_text(clipObj, (XClipboardMode)mode);
            if (st) {
                const char* utf8 = XString_toUtf8(st);
                if (utf8 && utf8[0]) owned = xtc_strdupN(utf8, -1);
                XString_delete_base((XClass*)st);
            }
        }
    }
#endif /* XCLIPBOARD_ON && XGUIAPPLICATION_ON */
    clip = owned;
    if (!clip && !systemOnly) clip = XTextClipboard_getText();
    if (clip && clip[0])
        XTextControl_insertFromMimeData(self, clip);
    if (owned) XFree_System(owned);
}

void XTextControl_paste(XTextControl* self)
{
    /* 对标 Qt paste() 无参重载：默认 CLIPBOARD 模式。 */
    xtc_pasteFromMode(self, (int)XClipboardMode_Clipboard);
}

void XTextControl_undo(XTextControl* self)
{
    int end;
    int start;
    int group;
    int i;
    int oldPos;
    if (!self || !self->m_undoEnabled || self->m_undoCount <= 0) return;
    oldPos = self->m_cursorPosition;
    end = self->m_undoCount;
    start = end - 1;
    group = self->m_undoStack[start].group;
    while (start > 0 && group != 0 &&
           self->m_undoStack[start - 1].group == group)
        --start;
    /* 逆序回滚同组命令。 */
    for (i = end - 1; i >= start; --i) {
        XTextControlUndoCommand* cmd = &self->m_undoStack[i];
        if (cmd->inserted)
            xtc_rawRemove(self, cmd->pos, (int)XStrlen(cmd->inserted));
        if (cmd->removed)
            xtc_rawInsert(self, cmd->pos, cmd->removed);
        self->m_cursorPosition = self->m_cursorAnchor = cmd->pos;
    }
    /* 命令移入重做栈（保持顺序）。 */
    for (i = start; i < end; ++i) {
        if (!xtc_commandPush(&self->m_redoStack, &self->m_redoCount,
                             &self->m_redoCap, &self->m_undoStack[i]))
            xtc_commandClear(&self->m_undoStack[i]);
    }
    self->m_undoCount = start;
    xtc_syncDocumentMirror(self);
    xtc_emitVoid(self, XTextControl_textChanged_signal);
    xtc_emitSize(self, XTextControl_documentSizeChanged_signal, NULL);
    xtc_emitRect(self, XTextControl_updateRequest_signal, NULL);
    xtc_notifyUndoRedo(self);
    xtc_notifyCursorPosition(self, oldPos);
    XTextControl_ensureCursorVisible(self);
    xtc_emitVoid(self, XTextControl_microFocusChanged_signal);
}

void XTextControl_redo(XTextControl* self)
{
    int end;
    int start;
    int group;
    int i;
    int oldPos;
    if (!self || !self->m_undoEnabled || self->m_redoCount <= 0) return;
    oldPos = self->m_cursorPosition;
    end = self->m_redoCount;
    start = end - 1;
    group = self->m_redoStack[start].group;
    while (start > 0 && group != 0 &&
           self->m_redoStack[start - 1].group == group)
        --start;
    /* 正序重放同组命令。 */
    for (i = start; i < end; ++i) {
        XTextControlUndoCommand* cmd = &self->m_redoStack[i];
        if (cmd->removed)
            xtc_rawRemove(self, cmd->pos, (int)XStrlen(cmd->removed));
        if (cmd->inserted)
            xtc_rawInsert(self, cmd->pos, cmd->inserted);
        self->m_cursorPosition = self->m_cursorAnchor =
            cmd->pos + (cmd->inserted ? (int)XStrlen(cmd->inserted) : 0);
    }
    for (i = start; i < end; ++i) {
        if (!xtc_commandPush(&self->m_undoStack, &self->m_undoCount,
                             &self->m_undoCap, &self->m_redoStack[i]))
            xtc_commandClear(&self->m_redoStack[i]);
    }
    self->m_redoCount = start;
    xtc_syncDocumentMirror(self);
    xtc_emitVoid(self, XTextControl_textChanged_signal);
    xtc_emitSize(self, XTextControl_documentSizeChanged_signal, NULL);
    xtc_emitRect(self, XTextControl_updateRequest_signal, NULL);
    xtc_notifyUndoRedo(self);
    xtc_notifyCursorPosition(self, oldPos);
    XTextControl_ensureCursorVisible(self);
    xtc_emitVoid(self, XTextControl_microFocusChanged_signal);
}

void XTextControl_clear(XTextControl* self)
{
    /* 对标 clear()：清额外选择集并重置为空内容。 */
    if (!self) return;
    self->m_extraSelectionCount = 0;
    XTextControl_setPlainText(self, "");
}

void XTextControl_selectAll(XTextControl* self)
{
    int length;
    int oldPos;
    int oldLen;
    int newLen;
    if (!self) return;
    length = xtc_documentLength(self);
    oldPos = self->m_cursorPosition;
    oldLen = xtc_hasSelection(self)
                 ? xtc_selectionEnd(self) - xtc_selectionStart(self) : 0;
    self->m_cursorAnchor = 0;
    self->m_cursorPosition = length;
    newLen = length;
    /* 对标：选区长度变化时强制发射 selectionChanged。 */
    xtc_notifySelection(self, newLen != oldLen);
    self->m_cursorIsFocusIndicator = false;
    xtc_notifyCursorPosition(self, oldPos);
    xtc_emitRect(self, XTextControl_updateRequest_signal, NULL);
}

void XTextControl_insertPlainText(XTextControl* self, const char* text)
{
    int pos;
    if (!self || !text || !text[0]) return;
    xtc_removeSelectedText(self, -1);
    pos = self->m_cursorPosition;
    xtc_editInsert(self, pos, text, -1);
    self->m_cursorAnchor = self->m_cursorPosition = pos + (int)XStrlen(text);
}

void XTextControl_insertHtml(XTextControl* self, const char* text)
{
    char* plain;
    XtcAnchorCollect ctx;
    if (!self || !text || !text[0]) return;
    xtc_removeSelectedText(self, -1);
    ctx.self = self;
    ctx.base = self->m_cursorPosition;
    plain = xtc_parseHtmlSubset(text, xtc_anchorCollectToControl, &ctx);
    if (!plain) return;
    if (plain[0]) {
        int pos = self->m_cursorPosition;
        xtc_editInsert(self, pos, plain, -1);
        self->m_cursorAnchor = self->m_cursorPosition =
            pos + (int)XStrlen(plain);
    }
    XFree_System(plain);
}

void XTextControl_append(XTextControl* self, const char* text)
{
    if (!self || !text) return;
    if (self->m_acceptRichText && xtc_mightBeRichText(text))
        XTextControl_appendHtml(self, text);
    else
        XTextControl_appendPlainText(self, text);
}

void XTextControl_appendHtml(XTextControl* self, const char* html)
{
    char* plain;
    XtcAnchorCollect ctx;
    if (!self || !html) return;
    ctx.self = self;
    ctx.base = xtc_documentLength(self) + 1; /* 预留待插入的段分隔符。 */
    plain = xtc_parseHtmlSubset(html, xtc_anchorCollectToControl, &ctx);
    if (!plain) return;
    /* 首个段边界换行由 appendPlainText 统一插入，剥离解析器补的首换行。 */
    if (plain[0] == '\n')
        XMemmove(plain, plain + 1, XStrlen(plain));
    XTextControl_appendPlainText(self, plain);
    XFree_System(plain);
}

void XTextControl_appendPlainText(XTextControl* self, const char* text)
{
    int end;
    if (!self || !text) return;
    /* 对标 Private::append：文末新起一段插入；光标不动。 */
    end = xtc_documentLength(self);
    if (!xtc_isEmpty(self)) {
        xtc_editInsert(self, end, "\n", -1);
        end += 1;
    }
    if (text[0])
        xtc_editInsert(self, end, text, -1);
}

void XTextControl_adjustSize(XTextControl* self)
{
    XSize size;
    if (!self) return;
    size = XTextControl_size(self);
    xtc_emitSize(self, XTextControl_documentSizeChanged_signal, &size);
}

bool XTextControl_isModified(const XTextControl* self)
{
    return self ? self->m_modified : false;
}

void XTextControl_setModified(XTextControl* self, bool modified)
{
    xtc_setModifiedNotify(self, modified);
}

bool XTextControl_isUndoRedoEnabled(const XTextControl* self)
{
    return self ? self->m_undoEnabled : false;
}

void XTextControl_setUndoRedoEnabled(XTextControl* self, bool enable)
{
    if (!self || self->m_undoEnabled == enable) return;
    self->m_undoEnabled = enable;
    if (!enable)
        xtc_clearUndoHistory(self);
    else
        xtc_notifyUndoRedo(self);
}

/* ==================== 公共 API：属性族 ==================== */

bool XTextControl_overwriteMode(const XTextControl* self)
{
    return self ? self->m_overwriteMode : false;
}

void XTextControl_setOverwriteMode(XTextControl* self, bool overwrite)
{
    if (self) self->m_overwriteMode = overwrite;
}

int XTextControl_cursorWidth(const XTextControl* self)
{
    return self ? self->m_cursorWidth : 1;
}

void XTextControl_setCursorWidth(XTextControl* self, int width)
{
    if (!self) return;
    if (width < 0) width = 1; /* 对标 -1 → 风格缺省的平铺解析。 */
    self->m_cursorWidth = width;
    xtc_repaintCursor(self);
}

bool XTextControl_acceptRichText(const XTextControl* self)
{
    return self ? self->m_acceptRichText : false;
}

void XTextControl_setAcceptRichText(XTextControl* self, bool accept)
{
    if (self) self->m_acceptRichText = accept;
}

void XTextControl_setExtraSelections(XTextControl* self,
                                     const XTextControlExtraSelection* selections,
                                     int count)
{
    int i;
    if (!self) return;
    if (count < 0) count = 0;
    self->m_extraSelectionCount = 0;
    for (i = 0; selections && i < count; ++i) {
        if (self->m_extraSelectionCount >= self->m_extraSelectionCap) {
            int newCap = self->m_extraSelectionCap + XTC_CAP_GROW;
            XTextControlExtraSelection* grown =
                (XTextControlExtraSelection*)XRealloc_System(
                    self->m_extraSelections,
                    (size_t)newCap * sizeof(XTextControlExtraSelection));
            if (!grown) break;
            self->m_extraSelections = grown;
            self->m_extraSelectionCap = newCap;
        }
        self->m_extraSelections[self->m_extraSelectionCount++] = selections[i];
        {
            XRect r = xtc_selectionRectImpl(self, selections[i].start,
                                            selections[i].end);
            xtc_emitRect(self, XTextControl_updateRequest_signal, &r);
        }
    }
}

int XTextControl_extraSelections(const XTextControl* self,
                                 const XTextControlExtraSelection** selections)
{
    int count;
    if (selections) *selections = NULL;
    if (!self) return 0;
    count = self->m_extraSelectionCount;
    if (count > 0 && selections)
        *selections = self->m_extraSelections;
    return count;
}

void XTextControl_setTextWidth(XTextControl* self, int width)
{
    XSize size;
    if (!self || self->m_textWidth == width) return;
    self->m_textWidth = width;
    /* 宽度即 WidgetWidth 折行宽度来源：失效缓存后经 size() 重建。 */
    xtc_invalidateLayout(self);
    size = XTextControl_size(self);
    xtc_emitSize(self, XTextControl_documentSizeChanged_signal, &size);
    xtc_emitRect(self, XTextControl_updateRequest_signal, NULL);
}

int XTextControl_textWidth(const XTextControl* self)
{
    return self ? self->m_textWidth : 0;
}

XSize XTextControl_size(const XTextControl* self)
{
    XSize size;
    XFont font;
    int i;
    int contentWidth = 0;
    if (!self) {
        size.width = 0;
        size.height = 0;
        return size;
    }
    /* 只读浅拷贝:与 self->m_font 共享 m_family/m_styleName,使用后禁止
       deinit(会释放控制器自有字符串,下一读点即踩悬垂指针)。 */
    XMemcpy(&font, &self->m_font, sizeof(XFont));
    for (i = 0; i < self->m_lineCount; ++i) {
        int w = XPainter_textWidthRange(&font, xtc_lineText(self, i), 0,
                                        xtc_lineLen(self, i));
        if (w > contentWidth) contentWidth = w;
    }
    /* 浅拷贝不拥有堆串:无需释放(所有权在 self->m_font)。 */
    size.width = self->m_textWidth > 0 ? self->m_textWidth : contentWidth;
    /* 高度按可视行数（软换行计入，对标 Qt documentSize）。 */
    size.height = xtc_contentHeight((XTextControl*)self);
    return size;
}

/* ==================== 公共 API：换行模式（对标 QPlainTextEdit） ==== */

int XTextControl_lineWrapMode(const XTextControl* self)
{
    return self ? self->m_lineWrapMode : (int)XTextControlLineWrap_WidgetWidth;
}

void XTextControl_setLineWrapMode(XTextControl* self, int mode)
{
    XSize size;
    if (!self || self->m_lineWrapMode == mode) return;
    self->m_lineWrapMode = mode;
    xtc_invalidateLayout(self);
    size = XTextControl_size(self); /* 触发重布局并取新文档尺寸。 */
    xtc_emitSize(self, XTextControl_documentSizeChanged_signal, &size);
    xtc_emitRect(self, XTextControl_updateRequest_signal, NULL);
}

int XTextControl_wordWrapMode(const XTextControl* self)
{
    return self ? self->m_wordWrapMode : (int)XTextControlWrap_WordWrap;
}

void XTextControl_setWordWrapMode(XTextControl* self, int mode)
{
    XSize size;
    if (!self || self->m_wordWrapMode == mode) return;
    self->m_wordWrapMode = mode;
    xtc_invalidateLayout(self);
    size = XTextControl_size(self);
    xtc_emitSize(self, XTextControl_documentSizeChanged_signal, &size);
    xtc_emitRect(self, XTextControl_updateRequest_signal, NULL);
}

int XTextControl_lineCount(const XTextControl* self)
{
    if (!self || self->m_lineCount <= 0) return 0;
    return xtc_visualCountOf((XTextControl*)self);
}

int XTextControl_blockCount(const XTextControl* self)
{
    return self ? self->m_lineCount : 0;
}

void XTextControl_posToVisualLineCol(const XTextControl* self, int pos,
                                     int* line, int* col)
{
    int row = 0;
    int rowCol = 0;
    if (line) *line = 0;
    if (col) *col = 0;
    if (!self || self->m_lineCount <= 0) return;
    xtc_ensureLayout((XTextControl*)self);
    xtc_posToVisualRowCol((XTextControl*)self, pos, &row, &rowCol);
    if (line) *line = row;
    if (col) *col = rowCol;
}

int XTextControl_visualLineColToPos(const XTextControl* self, int line, int col)
{
    XTextControl* s = (XTextControl*)self;
    int rows;
    if (!s || s->m_lineCount <= 0) return 0;
    xtc_ensureLayout(s);
    rows = s->m_visualCount;
    if (rows <= 0) return 0;
    if (line < 0) line = 0;
    if (line >= rows) line = rows - 1;
    return xtc_visualRowColToPos(s, line, col);
}

void XTextControl_setOpenExternalLinks(XTextControl* self, bool open)
{
    if (self) self->m_openExternalLinks = open;
}

bool XTextControl_openExternalLinks(const XTextControl* self)
{
    return self ? self->m_openExternalLinks : false;
}

void XTextControl_setIgnoreUnusedNavigationEvents(XTextControl* self, bool ignore)
{
    if (self) self->m_ignoreUnusedNavigationEvents = ignore;
}

bool XTextControl_ignoreUnusedNavigationEvents(const XTextControl* self)
{
    return self ? self->m_ignoreUnusedNavigationEvents : false;
}

void XTextControl_moveCursor(XTextControl* self, int operation, int mode)
{
    int oldAnchor;
    bool moved;
    if (!self) return;
    oldAnchor = self->m_cursorAnchor;
    moved = xtc_movePosition(self, operation, mode);
    xtc_notifyCharFormat(self);
    xtc_notifySelection(self, false);
    XTextControl_ensureCursorVisible(self);
    xtc_repaintOldAndNewSelection(self, oldAnchor, oldAnchor);
    if (moved)
        xtc_emitVoid(self, XTextControl_cursorPositionChanged_signal);
}

bool XTextControl_canPaste(const XTextControl* self)
{
    const char* clip;
    if (!self || !(self->m_interactionFlags &
                   (int)XTextControlInteraction_TextEditable))
        return false;
    clip = XTextClipboard_getText();
    return clip && clip[0] != '\0';
}

void XTextControl_setCursorIsFocusIndicator(XTextControl* self, bool b)
{
    if (!self) return;
    self->m_cursorIsFocusIndicator = b;
    xtc_repaintCursor(self);
}

bool XTextControl_cursorIsFocusIndicator(const XTextControl* self)
{
    return self ? self->m_cursorIsFocusIndicator : false;
}

void XTextControl_setDragEnabled(XTextControl* self, bool enabled)
{
    if (self) self->m_dragEnabled = enabled;
}

bool XTextControl_isDragEnabled(const XTextControl* self)
{
    return self ? self->m_dragEnabled : false;
}

void XTextControl_setWordSelectionEnabled(XTextControl* self, bool enabled)
{
    if (self) self->m_wordSelectionEnabled = enabled;
}

bool XTextControl_isWordSelectionEnabled(const XTextControl* self)
{
    return self ? self->m_wordSelectionEnabled : false;
}

bool XTextControl_isPreediting(XTextControl* self)
{
    return xtc_isPreediting(self);
}

void XTextControl_ensureCursorVisible(XTextControl* self)
{
    XRect crect;
    if (!self) return;
    crect = XTextControl_cursorRectAt(self, self->m_cursorPosition);
    crect.x -= 5;
    crect.width += 10;
    xtc_emitRect(self, XTextControl_visibilityRequest_signal, &crect);
    xtc_emitVoid(self, XTextControl_microFocusChanged_signal);
}

XVariant* XTextControl_loadResource(XTextControl* self, int type, const char* name)
{
    /* 对标 Qt 默认实现：找不到资源返回无效 QVariant。 */
    (void)self;
    (void)type;
    (void)name;
    return NULL;
}

/* ==================== 公共 API：链接查询 ==================== */

XString* XTextControl_anchorAt(const XTextControl* self, const XPoint* pos)
{
    char* href;
    XString* out;
    int posHit;
    if (!self || !pos) return XString_create_utf8("");
    posHit = XTextControl_hitTest(self, pos,
                                  (int)XTextControlHitTestAccuracy_FuzzyHit);
    href = xtc_anchorHit(self, posHit);
    out = XString_create_utf8(href ? href : "");
    if (href) XFree_System(href);
    return out;
}

XPoint XTextControl_anchorPosition(const XTextControl* self, const char* name)
{
    XPoint result;
    int i;
    result.x = 0;
    result.y = 0;
    /* 平铺承载：命名锚点以注册表 href 同名近似（对标 anchorNames 命中，
       返回 (0, 行顶)）。 */
    if (!self || !name || !name[0]) return result;
    for (i = 0; i < self->m_anchorCount; ++i) {
        if (self->m_anchors[i].href &&
            XStrcmp(self->m_anchors[i].href, name) == 0) {
            int line;
            xtc_posToLineCol(self, self->m_anchors[i].start, &line, NULL);
            result.y = line * self->m_lineHeight;
            return result;
        }
    }
    return result;
}

XString* XTextControl_anchorAtCursor(const XTextControl* self)
{
    char* href;
    XString* out;
    if (!self || !xtc_hasSelection(self)) return XString_create_utf8("");
    href = xtc_anchorHit(self, xtc_selectionStart(self));
    out = XString_create_utf8(href ? href : "");
    if (href) XFree_System(href);
    return out;
}

int XTextControl_blockWithMarkerAt(const XTextControl* self, const XPoint* pos)
{
    /* 平铺模型无块标记（QTextBlockFormat::Marker），恒未命中。 */
    (void)self;
    (void)pos;
    return -1;
}

bool XTextControl_setFocusToAnchor(XTextControl* self, int position)
{
    int i;
    if (!self || !(self->m_interactionFlags &
                   (int)XTextControlInteraction_LinksAccessibleByKeyboard))
        return false;
    if (!xtc_anchorHit(self, position)) return false;
    for (i = 0; i < self->m_anchorCount; ++i) {
        if (position >= self->m_anchors[i].start &&
            position < self->m_anchors[i].end) {
            XRect crect = xtc_selectionRectImpl(self, self->m_cursorPosition,
                                                self->m_cursorAnchor);
            xtc_emitRect(self, XTextControl_updateRequest_signal, &crect);
            self->m_cursorPosition = self->m_anchors[i].end;
            self->m_cursorAnchor = self->m_anchors[i].start;
            self->m_cursorIsFocusIndicator = true;
            crect = xtc_selectionRectImpl(self, self->m_cursorPosition,
                                          self->m_cursorAnchor);
            xtc_emitRect(self, XTextControl_updateRequest_signal, &crect);
            xtc_emitRect(self, XTextControl_visibilityRequest_signal, &crect);
            return true;
        }
    }
    return false;
}

bool XTextControl_setFocusToNextOrPreviousAnchor(XTextControl* self, bool next)
{
    int newStart;
    int newEnd;
    if (!self || !(self->m_interactionFlags &
                   (int)XTextControlInteraction_LinksAccessibleByKeyboard))
        return false;
    {
        XRect crect = xtc_selectionRectImpl(self, self->m_cursorPosition,
                                            self->m_cursorAnchor);
        xtc_emitRect(self, XTextControl_updateRequest_signal, &crect);
    }
    if (!xtc_hasSelection(self)) {
        /* 无当前锚点：从头/尾起步（对标）。 */
        self->m_cursorAnchor = self->m_cursorPosition =
            next ? 0 : xtc_documentLength(self);
    }
    if (XTextControl_findNextPrevAnchor(self, self->m_cursorPosition, next,
                                        &newStart, &newEnd)) {
        self->m_cursorPosition = newEnd;
        self->m_cursorAnchor = newStart;
        self->m_cursorIsFocusIndicator = true;
    } else {
        self->m_cursorAnchor = self->m_cursorPosition;
    }
    if (xtc_hasSelection(self)) {
        XRect crect = xtc_selectionRectImpl(self, self->m_cursorPosition,
                                            self->m_cursorAnchor);
        xtc_emitRect(self, XTextControl_updateRequest_signal, &crect);
        xtc_emitRect(self, XTextControl_visibilityRequest_signal, &crect);
        return true;
    }
    return false;
}

bool XTextControl_findNextPrevAnchor(const XTextControl* self, int fromPos,
                                     bool next, int* newStart, int* newEnd)
{
    int i;
    if (newStart) *newStart = -1;
    if (newEnd) *newEnd = -1;
    if (!self) return false;
    if (next) {
        for (i = 0; i < self->m_anchorCount; ++i) {
            if (self->m_anchors[i].start >= fromPos &&
                self->m_anchors[i].end > self->m_anchors[i].start) {
                if (newStart) *newStart = self->m_anchors[i].start;
                if (newEnd) *newEnd = self->m_anchors[i].end;
                return true;
            }
        }
    } else {
        for (i = self->m_anchorCount - 1; i >= 0; --i) {
            if (self->m_anchors[i].end <= fromPos &&
                self->m_anchors[i].end > self->m_anchors[i].start) {
                if (newStart) *newStart = self->m_anchors[i].start;
                if (newEnd) *newEnd = self->m_anchors[i].end;
                return true;
            }
        }
    }
    return false;
}

/* ==================== 公共 API：MIME 虚入口 ==================== */

char* XTextControl_createMimeDataFromSelection(const XTextControl* self)
{
    if (!self || !xtc_hasSelection(self)) return xtc_strdupN("", 0);
    return xtc_getRange(self, xtc_selectionStart(self),
                        xtc_selectionEnd(self) - xtc_selectionStart(self));
}

bool XTextControl_canInsertFromMimeData(const XTextControl* self, const char* source)
{
    /* 平铺模型承载 text/plain：非空即可插入（富文本路径见
       insertFromMimeData 的 HTML 子集判定）。 */
    (void)self;
    return source && source[0] != '\0';
}

void XTextControl_insertFromMimeData(XTextControl* self, const char* source)
{
    if (!self || !(self->m_interactionFlags &
                   (int)XTextControlInteraction_TextEditable) ||
        !source || !source[0])
        return;
    if (self->m_acceptRichText && xtc_mightBeRichText(source))
        XTextControl_insertHtml(self, source);
    else
        XTextControl_insertPlainText(self, source);
    XTextControl_ensureCursorVisible(self);
}

/* ==================== 公共 API：控制方法 ==================== */

void XTextControl_processEvent(XTextControl* control, XEvent* event)
{
    int type;
    if (!control || !event) return;
    if (control->m_interactionFlags ==
        (int)XTextControlInteraction_NoTextInteraction) {
        XEvent_ignore(event);
        return;
    }
    type = XEvent_type(event);
    switch (type) {
    case XEVENT_TYPE_KEY_PRESS:
        xtc_keyPressEvent(control, (XKeyEvent*)event);
        break;
    case XEVENT_TYPE_MOUSE_BUTTON_PRESS:
        xtc_mousePressEvent(control, (XMouseEvent*)event);
        break;
    case XEVENT_TYPE_MOUSE_MOVE:
        xtc_mouseMoveEvent(control, (XMouseEvent*)event);
        break;
    case XEVENT_TYPE_MOUSE_BUTTON_RELEASE:
        xtc_mouseReleaseEvent(control, (XMouseEvent*)event);
        break;
    case XEVENT_TYPE_MOUSE_BUTTON_DBL_CLICK:
        xtc_mouseDoubleClickEvent(control, (XMouseEvent*)event);
        break;
#if XINPUTMETHOD_ON
    case XEVENT_TYPE_INPUT_METHOD:
        xtc_inputMethodEvent(control, (XInputMethodEvent*)event);
        break;
#endif /* XINPUTMETHOD_ON */
    case XEVENT_TYPE_CONTEXT_MENU: {
#if XMENU_ON
        XContextMenuEvent* ctx = (XContextMenuEvent*)event;
        XMenu* menu = XTextControl_createStandardContextMenu(control);
        if (menu) {
            XPoint global = XContextMenuEvent_globalPosition(ctx);
            XWidget_setAttribute((XWidget*)menu,
                                 XWidgetAttribute_DeleteOnClose, true);
            XMenu_popup(menu, &global);
        }
        XEvent_accept(event);
#else
        XEvent_ignore(event);
#endif
        break;
    }
#if XWINDOWEVENT_ON
    case XEVENT_TYPE_FOCUS_IN:
    case XEVENT_TYPE_FOCUS_OUT:
        xtc_focusEvent(control, (XFocusEvent*)event);
        break;
#endif /* XWINDOWEVENT_ON */
    case XEVENT_TYPE_ENABLED_CHANGE:
        control->m_isEnabled = XEvent_isAccepted(event);
        break;
    case XEVENT_TYPE_SHORTCUT_OVERRIDE:
        if (control->m_interactionFlags &
            (int)XTextControlInteraction_TextEditable) {
            XKeyEvent* ke = (XKeyEvent*)event;
            if (xtc_isCommonShortcut(ke->m_key, (int)ke->m_modifiers))
                XEvent_accept(event);
        }
        break;
#if XWINDOWEVENT_ON
    case XEVENT_TYPE_DRAG_ENTER:
    case XEVENT_TYPE_DRAG_MOVE:
    case XEVENT_TYPE_DRAG_LEAVE:
    case XEVENT_TYPE_DROP:
        xtc_dropEventRoute(control, (XDropEvent*)event);
        break;
#endif /* XWINDOWEVENT_ON */
    default:
        break;
    }
}

#if XWINDOWEVENT_ON
void XTextControl_setFocus(XTextControl* self, bool focus, XFocusReason reason)
{
    XFocusEvent ev;
    if (!self) return;
    XFocusEvent_init(&ev,
                     focus ? XEVENT_TYPE_FOCUS_IN : XEVENT_TYPE_FOCUS_OUT,
                     reason);
    XTextControl_processEvent(self, (XEvent*)&ev);
}
#endif /* XWINDOWEVENT_ON */

bool XTextControl_inputMethodQuery(const XTextControl* self, int property,
                                   int argument, XTextControlImValue* out)
{
    if (!out) return false;
    XMemset(out, 0, sizeof(*out));
    out->type = 0;
    if (!self) return false;
#if XINPUTMETHOD_ON
    switch (property) {
    case (int)XInputMethodQuery_ImEnabled:
        out->type = 2;
        out->b = (self->m_interactionFlags &
                  (int)XTextControlInteraction_TextEditable) != 0;
        return true;
    case (int)XInputMethodQuery_ImCursorRectangle:
        out->type = 4;
        out->rect = XTextControl_cursorRect(self);
        return true;
    case (int)XInputMethodQuery_ImAnchorRectangle:
        out->type = 4;
        out->rect = XTextControl_cursorRectAt(self, self->m_cursorAnchor);
        return true;
    case (int)XInputMethodQuery_ImCursorPosition: {
        int line;
        int col;
        int pos = argument >= 0 ? argument : self->m_cursorPosition;
        xtc_posToLineCol(self, pos, &line, &col);
        out->type = 1;
        out->i = col;
        return true;
    }
    case (int)XInputMethodQuery_ImAnchorPosition: {
        int line;
        int col;
        xtc_posToLineCol(self, self->m_cursorAnchor, &line, &col);
        out->type = 1;
        out->i = col;
        return true;
    }
    case (int)XInputMethodQuery_ImAbsolutePosition:
        out->type = 1;
        out->i = argument >= 0 ? argument : self->m_cursorPosition;
        return true;
    case (int)XInputMethodQuery_ImSurroundingText: {
        int line;
        int col;
        xtc_posToLineCol(self, self->m_cursorPosition, &line, &col);
        out->type = 3;
        out->text = xtc_strdupN(xtc_lineText(self, line), -1);
        return out->text != NULL;
    }
    case (int)XInputMethodQuery_ImCurrentSelection: {
        char* text = NULL;
        if (xtc_hasSelection(self))
            text = xtc_getRange(self, xtc_selectionStart(self),
                                xtc_selectionEnd(self) -
                                    xtc_selectionStart(self));
        out->type = 3;
        out->text = text ? text : xtc_strdupN("", 0);
        return out->text != NULL;
    }
    case (int)XInputMethodQuery_ImTextBeforeCursor: {
        int limit = argument > 0 ? argument : 1024;
        int from = self->m_cursorPosition - limit;
        if (from < 0) from = 0;
        out->type = 3;
        out->text = xtc_getRange(self, from, self->m_cursorPosition - from);
        return out->text != NULL;
    }
    case (int)XInputMethodQuery_ImTextAfterCursor: {
        int limit = argument > 0 ? argument : 1024;
        int total = xtc_documentLength(self);
        int to = self->m_cursorPosition + limit;
        if (to > total) to = total;
        out->type = 3;
        out->text = xtc_getRange(self, self->m_cursorPosition,
                                 to - self->m_cursorPosition);
        return out->text != NULL;
    }
    case (int)XInputMethodQuery_ImMaximumTextLength:
        /* 对标 Qt：无上限 → 无效返回。 */
        return false;
    default:
        /* ImFont/ImHints/ImPreferredLanguage/ImEnterKeyType 等富承载查询
           无平铺对应（对标差异）。 */
        return false;
    }
#else
    /* 输入法子系统裁剪：无 IM 承载，一切查询按不支持退化。 */
    (void)property;
    (void)argument;
    return false;
#endif /* XINPUTMETHOD_ON */
}

/* ==================== 公共 API：调色板 / 字体 ==================== */

void XTextControl_palette(const XTextControl* self, XPalette* out)
{
    if (!out) return;
    if (self)
        XPalette_copy(out, (XPalette*)&self->m_palette);
    else
        XPalette_init_default(out);
}

void XTextControl_setPalette(XTextControl* self, const XPalette* pal)
{
    if (!self) return;
    if (pal)
        XPalette_copy((XPalette*)&self->m_palette, pal);
    else
        XPalette_init_default((XPalette*)&self->m_palette);
    xtc_emitRect(self, XTextControl_updateRequest_signal, NULL);
}

void XTextControl_setFont(XTextControl* self, const XFont* font)
{
    XFont f;
    int ascent;
    int descent;
    int oldAscent;
    int oldDescent;
    const char* oldFamily;
    const char* newFamily;
    bool familyChanged;
    if (!self) return;
    if (font) {
        XMemcpy(&f, font, sizeof(XFont));
    } else {
        XFont_init(&f);
    }
    oldAscent = self->m_lineAscent;
    oldDescent = self->m_lineHeight - self->m_lineAscent;
    oldFamily = XFont_family(&self->m_font);
    newFamily = XFont_family(&f);
    /* 家族比较必须在释放旧字体之前完成：oldFamily 指向旧字体家族串的
       toUtf8 缓存，deinit 连缓存一起释放，事后 strcmp 是 use-after-free
       （ASan 基线扫查发现）。 */
    familyChanged = (oldFamily != newFamily) &&
                    (oldFamily == NULL || newFamily == NULL ||
                     strcmp(oldFamily, newFamily) != 0);
    XFont_deinit_base((XClass*)&self->m_font);
    /* 深拷贝（对标 XWidget_font 的 Phase 3.2 裁定）：XFont 值拷贝共享
       XString 指针，浅拷贝会在任一持有方 deinit 后留下悬空指针。 */
    XFont_init(&self->m_font);
    XCopy(&self->m_font, &f);
    /* 注意：f.m_family/f.m_styleName 归调用方字体对象所有（setFont
       无权释放）；XCopy 已为 self 深拷贝出新串，调用方稍后自行
       deinit 其副本。 */
    ascent = XPainter_textAscent(&self->m_font);
    descent = XPainter_textDescent(&self->m_font);
    if (ascent <= 0) ascent = XTC_DEFAULT_BASELINE;
    if (descent <= 0) descent = XTC_DEFAULT_LINE_HEIGHT - XTC_DEFAULT_BASELINE;
    self->m_lineAscent = ascent;
    self->m_lineHeight = (ascent + descent) > 0 ? ascent + descent
                                                : XTC_DEFAULT_LINE_HEIGHT;
    /* 度量或家族真变化才失效布局并请求重绘：壳在绘制/度量入口做无差别
       字体同步，无条件失效会使每次事件整篇重布局（含 paint→update
       重绘回路风险）。 */
    if (ascent != oldAscent || descent != oldDescent || familyChanged) {
        /* 字体度量变化 → 可视行像素宽/折行全部失效（重布局）。 */
        xtc_invalidateLayout(self);
        xtc_emitRect(self, XTextControl_updateRequest_signal, NULL);
    }
}

void XTextControl_font(const XTextControl* self, XFont* out)
{
    if (!out) return;
    XFont_init(out);
    if (self) {
        /* 文档契约：深拷贝（调用方配合 XFont_deinit_base 释放）。 */
        XCopy(out, &self->m_font);
    }
}

/* ==================== 公共 API：标准右键菜单 ==================== */

#if XMENU_ON

/** @brief 菜单适配槽族（对标 createStandardContextMenu 的动作集合）。 */
static void xtc_menuUndo(void* ud) { XTextControl_undo((XTextControl*)ud); }
static bool xtc_menuCanUndo(void* ud)
{ return ((XTextControl*)ud)->m_undoCount > 0; }
static void xtc_menuRedo(void* ud) { XTextControl_redo((XTextControl*)ud); }
static bool xtc_menuCanRedo(void* ud)
{ return ((XTextControl*)ud)->m_redoCount > 0; }
static void xtc_menuCut(void* ud) { XTextControl_cut((XTextControl*)ud); }
static bool xtc_menuCanCut(void* ud)
{ return xtc_hasSelection((XTextControl*)ud); }
static void xtc_menuCopy(void* ud) { XTextControl_copy((XTextControl*)ud); }
static bool xtc_menuCanCopy(void* ud)
{ return xtc_hasSelection((XTextControl*)ud); }
static void xtc_menuPaste(void* ud) { XTextControl_paste((XTextControl*)ud); }
static bool xtc_menuCanPaste(void* ud)
{ return XTextControl_canPaste((XTextControl*)ud); }
static void xtc_menuDelete(void* ud)
{ xtc_removeSelectedText((XTextControl*)ud, -1); }
static bool xtc_menuCanDelete(void* ud)
{ return xtc_hasSelection((XTextControl*)ud); }
static void xtc_menuSelectAll(void* ud)
{ XTextControl_selectAll((XTextControl*)ud); }
static bool xtc_menuCanSelectAll(void* ud)
{ return xtc_documentLength((XTextControl*)ud) > 0; }

XMenu* XTextControl_createStandardContextMenu(XTextControl* self)
{
    XTextMenuOps ops;
    bool editable;
    if (!self) return NULL;
    editable = (self->m_interactionFlags &
                (int)XTextControlInteraction_TextEditable) != 0;
    XMemset(&ops, 0, sizeof(ops));
    ops.ud = self;
    if (editable) {
        ops.undo = xtc_menuUndo;
        ops.canUndo = xtc_menuCanUndo;
        ops.redo = xtc_menuRedo;
        ops.canRedo = xtc_menuCanRedo;
        ops.cut = xtc_menuCut;
        ops.canCut = xtc_menuCanCut;
        ops.paste = xtc_menuPaste;
        ops.canPaste = xtc_menuCanPaste;
        ops.del = xtc_menuDelete;
        ops.canDel = xtc_menuCanDelete;
    }
    ops.copy = xtc_menuCopy;
    ops.canCopy = xtc_menuCanCopy;
    ops.selectAll = xtc_menuSelectAll;
    ops.canSelectAll = xtc_menuCanSelectAll;
    return XTextMenu_createStandard(&ops);
}

#endif /* XMENU_ON */

/* ==================== 信号函数（真发射门面） ==================== */

void* XTextControl_textChanged_signal(XTextControl* self)
{
    (void)self;
    return (void*)(size_t)XTextControl_textChanged_signal;
}

void* XTextControl_undoAvailable_signal(XTextControl* self, bool b)
{
    xtc_emitBool(self, XTextControl_undoAvailable_signal, b);
    return (void*)(size_t)XTextControl_undoAvailable_signal;
}

void* XTextControl_redoAvailable_signal(XTextControl* self, bool b)
{
    xtc_emitBool(self, XTextControl_redoAvailable_signal, b);
    return (void*)(size_t)XTextControl_redoAvailable_signal;
}

void* XTextControl_currentCharFormatChanged_signal(XTextControl* self, int format)
{
    xtc_emitInt(self, XTextControl_currentCharFormatChanged_signal, format);
    return (void*)(size_t)XTextControl_currentCharFormatChanged_signal;
}

void* XTextControl_copyAvailable_signal(XTextControl* self, bool b)
{
    xtc_emitBool(self, XTextControl_copyAvailable_signal, b);
    return (void*)(size_t)XTextControl_copyAvailable_signal;
}

void* XTextControl_selectionChanged_signal(XTextControl* self)
{
    xtc_emitVoid(self, XTextControl_selectionChanged_signal);
    return (void*)(size_t)XTextControl_selectionChanged_signal;
}

void* XTextControl_cursorPositionChanged_signal(XTextControl* self)
{
    xtc_emitVoid(self, XTextControl_cursorPositionChanged_signal);
    return (void*)(size_t)XTextControl_cursorPositionChanged_signal;
}

void* XTextControl_updateRequest_signal(XTextControl* self, const XRect* rect)
{
    xtc_emitRect(self, XTextControl_updateRequest_signal, rect);
    return (void*)(size_t)XTextControl_updateRequest_signal;
}

void* XTextControl_documentSizeChanged_signal(XTextControl* self, const XSize* size)
{
    xtc_emitSize(self, XTextControl_documentSizeChanged_signal, size);
    return (void*)(size_t)XTextControl_documentSizeChanged_signal;
}

void* XTextControl_blockCountChanged_signal(XTextControl* self, int newBlockCount)
{
    xtc_emitInt(self, XTextControl_blockCountChanged_signal, newBlockCount);
    return (void*)(size_t)XTextControl_blockCountChanged_signal;
}

void* XTextControl_visibilityRequest_signal(XTextControl* self, const XRect* rect)
{
    xtc_emitRect(self, XTextControl_visibilityRequest_signal, rect);
    return (void*)(size_t)XTextControl_visibilityRequest_signal;
}

void* XTextControl_microFocusChanged_signal(XTextControl* self)
{
    xtc_emitVoid(self, XTextControl_microFocusChanged_signal);
    return (void*)(size_t)XTextControl_microFocusChanged_signal;
}

void* XTextControl_linkActivated_signal(XTextControl* self, const char* link)
{
    xtc_emitString(self, XTextControl_linkActivated_signal, link);
    return (void*)(size_t)XTextControl_linkActivated_signal;
}

void* XTextControl_linkHovered_signal(XTextControl* self, const char* link)
{
    xtc_emitString(self, XTextControl_linkHovered_signal, link);
    return (void*)(size_t)XTextControl_linkHovered_signal;
}

void* XTextControl_blockMarkerHovered_signal(XTextControl* self, int block)
{
    xtc_emitInt(self, XTextControl_blockMarkerHovered_signal, block);
    return (void*)(size_t)XTextControl_blockMarkerHovered_signal;
}

void* XTextControl_modificationChanged_signal(XTextControl* self, bool m)
{
    xtc_emitBool(self, XTextControl_modificationChanged_signal, m);
    return (void*)(size_t)XTextControl_modificationChanged_signal;
}

/* ==================== 生命周期 ==================== */

/**
 * @brief      释放控件资源（行数组/撤销栈/锚点/字符串/字体/文档镜像/
 *             定时器），随后委托父类 Deinit。
 */
static void VXTextControl_deinit(XTextControl* self)
{
    int i;
    if (!self) return;
    if (self->m_cursorBlinkTimer != XTIMER_INVALID_ID)
        XObject_killTimer((XObject*)self, self->m_cursorBlinkTimer);
    if (self->m_tripleClickTimer != XTIMER_INVALID_ID)
        XObject_killTimer((XObject*)self, self->m_tripleClickTimer);
    for (i = 0; i < self->m_lineCount; ++i) {
        if (self->m_lines[i].data) XFree_System(self->m_lines[i].data);
    }
    if (self->m_lines) XFree_System(self->m_lines);
    self->m_lines = NULL;
    self->m_lineCount = 0;
    self->m_lineCap = 0;
    if (self->m_visualRows) XFree_System(self->m_visualRows);
    self->m_visualRows = NULL;
    self->m_visualCount = 0;
    self->m_visualCap = 0;
    self->m_layoutValid = false;
    xtc_commandStackClear(&self->m_undoStack, &self->m_undoCount,
                          &self->m_undoCap);
    xtc_commandStackClear(&self->m_redoStack, &self->m_redoCount,
                          &self->m_redoCap);
    xtc_clearAnchors(self);
    if (self->m_anchors) XFree_System(self->m_anchors);
    self->m_anchors = NULL;
    self->m_anchorCap = 0;
    if (self->m_extraSelections) XFree_System(self->m_extraSelections);
    self->m_extraSelections = NULL;
    self->m_extraSelectionCount = 0;
    self->m_extraSelectionCap = 0;
    if (self->m_anchorOnMousePress) XFree_System(self->m_anchorOnMousePress);
    if (self->m_highlightedAnchor) XFree_System(self->m_highlightedAnchor);
    if (self->m_linkToCopy) XFree_System(self->m_linkToCopy);
    self->m_anchorOnMousePress = NULL;
    self->m_highlightedAnchor = NULL;
    self->m_linkToCopy = NULL;
    xtc_clearPreeditState(self);
    XFont_deinit_base((XClass*)&self->m_font);
#if XTEXTDOCUMENT_ON
    if (self->m_textDoc) {
        XClass_delete_base((XClass*)self->m_textDoc);
        self->m_textDoc = NULL;
    }
#endif
    XClass_Deinit_Parent(XObject, (XObject*)self);
}

XVtable* XTextControl_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XTextControl)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_TimerEvent, VXTextControl_timerEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_Event, VXTextControl_objectEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXTextControl_deinit);
    return XVTABLE_DEFAULT;
}

void XTextControl_init(XTextControl* self)
{
    XPalette palette;
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XObject_init((XObject*)self);
    XClassSetVtable(self, XTextControl);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    /* 行数组：初始单空行（对标空 QTextDocument 的单空块）。 */
    self->m_lineCap = XTC_CAP_GROW;
    self->m_lines = (XTextControlLine*)XMalloc_System(
        (size_t)self->m_lineCap * sizeof(XTextControlLine));
    if (self->m_lines)
        XMemset(self->m_lines, 0,
                (size_t)self->m_lineCap * sizeof(XTextControlLine));
    self->m_lineCount = 1;
    /* 属性缺省（对标 QWidgetTextControlPrivate::init）。 */
    self->m_interactionFlags =
        (int)XTextControlInteraction_TextEditorInteraction;
    self->m_acceptRichText = true;
    self->m_dragEnabled = true;
    self->m_cursorWidth = 1;
    self->m_isEnabled = true;
    self->m_undoEnabled = true; /* 对标 init：undoRedoEnabled = 可编辑。 */
    self->m_cursorBlinkTimer = XTIMER_INVALID_ID;
    self->m_tripleClickTimer = XTIMER_INVALID_ID;
    self->m_wordSelStart = -1;
    self->m_wordSelEnd = -1;
    self->m_blockSelStart = -1;
    self->m_blockSelEnd = -1;
    self->m_dndFeedbackPos = -1;
    self->m_lineHeight = XTC_DEFAULT_LINE_HEIGHT;
    self->m_lineAscent = XTC_DEFAULT_BASELINE;
    /* 软换行缺省（对标 QPlainTextEdit 默认：WidgetWidth +
       QTextOption::WordWrap；textWidth 未设置（<=0）时不折行）。 */
    self->m_lineWrapMode = (int)XTextControlLineWrap_WidgetWidth;
    self->m_wordWrapMode = (int)XTextControlWrap_WordWrap;
    self->m_layoutValid = false;
    XFont_init(&self->m_font);
    XPalette_init_default(&palette);
    XPalette_copy((XPalette*)&self->m_palette, &palette);
#if XTEXTDOCUMENT_ON
    self->m_textDoc = XTextDocument_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
#endif
    xtc_notifyUndoRedo(self);
}

void XTextControl_init_2(XTextControl* self, const char* text)
{
    if (!self) return;
    XTextControl_init(self);
    XTextControl_setPlainText(self, text);
}

#if XTEXTDOCUMENT_ON
void XTextControl_init_3(XTextControl* self, XTextDocument* doc)
{
    if (!self) return;
    XTextControl_init(self);
    if (doc)
        XTextControl_setDocument(self, doc);
}
#endif

XTextControl* XTextControl_create_ex(XMemoryType memory)
{
    XTextControl* self = (XTextControl*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XTextControl_init(self);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

#endif /* XTEXTCONTROL_ON */
