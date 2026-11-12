#if XGUI_ON

#include "XTextCursor.h"
#include "XTextLayout.h"
#include "XStringUtils.h"
#include "XMemory.h"
#include "XObject.h"

#include <string.h>

#if XTEXTDOCUMENT_ON

/* ==================== 内部工具（文档模型直读：XTDBlock/XTDFragment 与
 *    XTextDocument 结构都是公共头内平铺，借用指针逐字段读，不复制逻辑） === */

/** @brief 释放字符格式字符串字段（值对象清理；语义同 XTextDocument.c 的
 *         xtd_formatClear——跨文件私有，本文件按值重录两件套）。 */
static void xtc_formatClear(XTDCharFormat* fmt)
{
    if (!fmt) return;
    if (fmt->fontFamily) {
        XClassDelete(fmt->fontFamily);
        fmt->fontFamily = NULL;
    }
    if (fmt->anchorHref) {
        XClassDelete(fmt->anchorHref);
        fmt->anchorHref = NULL;
    }
}

/** @brief 深拷贝字符格式（含字符串字段；dst 旧载荷先释放）。 */
static void xtc_formatAssign(XTDCharFormat* dst, const XTDCharFormat* src)
{
    if (!dst || !src || dst == src) return;
    xtc_formatClear(dst);
    XMemset(dst, 0, sizeof(*dst));
    if (src->fontFamily) dst->fontFamily = XString_create_copy(src->fontFamily);
    if (src->anchorHref) dst->anchorHref = XString_create_copy(src->anchorHref);
    dst->bold = src->bold;
    dst->italic = src->italic;
    dst->underline = src->underline;
    dst->strikeOut = src->strikeOut;
    dst->superScript = src->superScript;
    dst->subScript = src->subScript;
    dst->fgColor = src->fgColor;
    dst->bgColor = src->bgColor;
    dst->fontPointSize = src->fontPointSize;
}

/** @brief 单块文本字节长（不含块分隔符；空片段安全）。 */
static int xtc_blockCharCount(const XTDBlock* blk)
{
    int j;
    int total = 0;
    if (!blk) return 0;
    for (j = 0; j < blk->fragmentCount; ++j) {
        const char* t = (blk->fragments[j].text)
                            ? XString_toUtf8(blk->fragments[j].text)
                            : NULL;
        total += (int)(t ? XStrlen(t) : 0);
    }
    return total;
}

/** @brief 块 bi 的文档起始位置（= Σ_{i<bi}(块文本字节+1)）。 */
static int xtc_blockStart(const XTextDocument* doc, int bi)
{
    int i;
    int pos = 0;
    if (!doc || !doc->m_blocks || bi <= 0) return 0;
    for (i = 0; i < bi && i < doc->m_blockCount; ++i)
        pos += xtc_blockCharCount(&doc->m_blocks[i]) + 1;
    return pos;
}

/** @brief 文档字符数（各块文本字节 + 块数——每块 1 个分隔符口径）。 */
static int xtc_charCount(const XTextDocument* doc)
{
    int i;
    int total = 0;
    if (!doc || !doc->m_blocks) return 0;
    for (i = 0; i < doc->m_blockCount; ++i)
        total += xtc_blockCharCount(&doc->m_blocks[i]) + 1;
    return total;
}

/** @brief 文档位置 → (块号, 块内偏移)；偏移可达块文本长（分隔符位）。
 *         语义与 XTextDocument.c 的 xtd_posLocate 同口径（借用读）。 */
static void xtc_posLocate(const XTextDocument* doc, int pos, int* bi,
                          int* off)
{
    int i;
    int acc = 0;
    if (bi) *bi = 0;
    if (off) *off = 0;
    if (!doc || !doc->m_blocks || doc->m_blockCount <= 0) return;
    if (pos < 0) pos = 0;
    for (i = 0; i < doc->m_blockCount; ++i) {
        int n = xtc_blockCharCount(&doc->m_blocks[i]);
        if (pos < acc + n + 1) {
            if (bi) *bi = i;
            if (off) *off = pos - acc;
            return;
        }
        acc += n + 1;
    }
    i = doc->m_blockCount - 1;
    if (bi) *bi = i;
    if (off) *off = xtc_blockCharCount(&doc->m_blocks[i]);
}

/** @brief 块内偏移处的字节（分隔符位/越界返回 0）。 */
static char xtc_byteAt(const XTDBlock* blk, int off)
{
    int fi;
    int acc = 0;
    if (!blk || off < 0) return 0;
    for (fi = 0; fi < blk->fragmentCount; ++fi) {
        const char* t = (blk->fragments[fi].text)
                            ? XString_toUtf8(blk->fragments[fi].text)
                            : NULL;
        int flen = t ? (int)XStrlen(t) : 0;
        if (off < acc + flen) return t[off - acc];
        acc += flen;
    }
    return 0;
}

/** @brief 文档位置 pos 处一个 UTF-8 码点的字节长（1-4；分隔符位/残序列
 *         按 1——xtd_utf8SeqLen 的文档位置版，本文件按块直读）。 */
static int xtc_seqLenAt(const XTextDocument* doc, int pos)
{
    int bi;
    int off;
    int blockLen;
    unsigned char c;
    int seq = 1;
    if (!doc || !doc->m_blocks || doc->m_blockCount <= 0 || pos < 0) return 1;
    xtc_posLocate(doc, pos, &bi, &off);
    if (bi < 0 || bi >= doc->m_blockCount) return 1;
    blockLen = xtc_blockCharCount(&doc->m_blocks[bi]);
    if (off >= blockLen) return 1; /* 分隔符位 = 单字节口径。 */
    c = (unsigned char)xtc_byteAt(&doc->m_blocks[bi], off);
    if (c >= 0xC2 && c < 0xE0) seq = 2;
    else if (c >= 0xE0 && c < 0xF0) seq = 3;
    else if (c >= 0xF0 && c < 0xF5) seq = 4;
    if (off + seq > blockLen) seq = 1; /* 序列越过块尾截断。 */
    return seq;
}

/** @brief doc/块 光标移动的统一落位（moved 语义 = position 实际变化；
 *         MoveAnchor 折叠选区 + 插入格式缓存失效；文档活动光标同步——
 *         Qt finishEdit 的 cursorPositionChanged 承载）。 */
static bool xtc_setPositionImpl(XTextCursor* self, int newPosition,
                                int mode)
{
    XTextDocument* doc = self->m_document;
    bool moved;
    if (newPosition < 0) return false;
    moved = (self->m_position != newPosition);
    if (moved) {
        self->m_position = newPosition;
        self->m_hasInsertFormat = 0; /* Qt currentCharFormat=-1 失效口径 */
        if (doc)
            XTextDocument_setCursorPosition(doc, newPosition);
    }
    if (mode == (int)XTextCursorMode_MoveAnchor) {
        self->m_anchor = self->m_position;
    } else if (doc) {
        /* KeepAnchor：只动 position（Qt adjustCursor 的表格分支不在子集）；
           文档活动光标同步跟随 position。 */
        XTextDocument_setCursorPosition(doc, self->m_position);
    }
    return moved;
}

/** @brief 非 Up/Down 移动路径的 sticky x 失效（Qt adjustX=true → setX；
 *         惰性化：置 -1，下次垂直移动按当前列现算）。 */
static void xtc_invalidateX(XTextCursor* self)
{
    if (self) self->m_x = -1;
}

/** @brief 块文本导出到调用方缓冲（UTF-8；cap 含 NUL 位；截断安全）。 */
static void xtc_blockText(const XTDBlock* blk, char* out, int cap)
{
    int fi;
    int o = 0;
    if (!blk || !out || cap <= 0) return;
    out[0] = '\0';
    for (fi = 0; fi < blk->fragmentCount && o < cap - 1; ++fi) {
        const char* t = (blk->fragments[fi].text)
                            ? XString_toUtf8(blk->fragments[fi].text)
                            : NULL;
        int flen = t ? (int)XStrlen(t) : 0;
        if (flen > cap - 1 - o) flen = cap - 1 - o;
        if (flen > 0) {
            XMemcpy(out + o, t, (size_t)flen);
            o += flen;
        }
    }
    out[o] = '\0';
}

/** @brief 布局行上下文（栈上建 XTextLayout——XObject 值形态 init/deinit；
 *         文本块内 UTF-8、宽度 0 = 不软换行、缺省字体——W1 行表直读）。 */
typedef struct XtcLineCtx
{
    XTextLayout layout;   /**< 栈上布局对象（用完 XObject_Deinit）。 */
    char text[2048];      /**< 块文本缓冲（有界子集：块文本超长截断）。 */
} XtcLineCtx;

static bool xtc_lineCtxInit(XtcLineCtx* ctx, const XTextDocument* doc, int bi)
{
    if (!ctx || !doc || bi < 0 || bi >= doc->m_blockCount) return false;
    XTextLayout_init(&ctx->layout);
    xtc_blockText(&doc->m_blocks[bi], ctx->text, (int)sizeof(ctx->text));
    XTextLayout_setText_utf8(&ctx->layout, ctx->text, -1);
    XTextLayout_setLineWidth(&ctx->layout, 0);
    return true;
}

static void xtc_lineCtxDeinit(XtcLineCtx* ctx)
{
    if (!ctx) return;
    XClassDeinit((XClass*)&ctx->layout); /* 栈对象：仅反初始化不释放。 */
}

/* ==================== Qt 词边界（qtextengine.cpp:2845-2886 词分隔符集合
 *    + QChar::isSpace 的 ASCII 子集；>=0x80 字节视为词内字符——CJK 不误断，
 *    与既有 xtd_findIsWordChar 口径一致） ==================== */

static bool xtc_isSpace(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' ||
           c == '\f';
}

static bool xtc_isWordSep(char c)
{
    switch (c) {
    case '.': case ',': case '?': case '!': case '@': case '#': case '$':
    case ':': case ';': case '-': case '<': case '>': case '[': case ']':
    case '(': case ')': case '{': case '}': case '=': case '/': case '+':
    case '%': case '&': case '^': case '*': case '\'': case '"': case '`':
    case '~': case '|': case '\\':
        return true;
    default:
        return false;
    }
}

static bool xtc_isWordChar(char c)
{
    unsigned char u = (unsigned char)c;
    return u >= 0x80 || !xtc_isSpace(c) && !xtc_isWordSep(c);
}

/* ==================== 生命周期 ==================== */

void XTextCursor_init(XTextCursor* self, XTextDocument* doc)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    self->m_document = doc;
    /* Qt QTextCursor(doc) 恒定位于 0（fromPosition(d, 0)），不跟随文档
     * 既有活动光标——需要衔接时调用方 setPosition 显式定位。 */
    self->m_position = 0;
    self->m_anchor = 0;
    self->m_hasInsertFormat = 0;
    self->m_x = -1; /* 惰性（Qt x=-1 delayed init 同口径） */
}

XTextCursor XTextCursor_make(XTextDocument* doc)
{
    XTextCursor c;
    XTextCursor_init(&c, doc);
    return c;
}

void XTextCursor_copy(XTextCursor* dst, const XTextCursor* src)
{
    if (!dst) return;
    if (!src) {
        XTextCursor_deinit(dst);
        XMemset(dst, 0, sizeof(*dst));
        dst->m_x = -1;
        return;
    }
    if (dst == src) return;
    xtc_formatAssign(&dst->m_insertFormat, &src->m_insertFormat);
    dst->m_document = src->m_document;
    dst->m_position = src->m_position;
    dst->m_anchor = src->m_anchor;
    dst->m_hasInsertFormat = src->m_hasInsertFormat;
    dst->m_x = src->m_x;
}

void XTextCursor_move(XTextCursor* dst, XTextCursor* src)
{
    if (!dst || !src || dst == src) return;
    xtc_formatClear(&dst->m_insertFormat);
    *dst = *src;          /* 载荷（格式字符串指针）转移 */
    XMemset(src, 0, sizeof(*src));
    src->m_x = -1;
}

void XTextCursor_deinit(XTextCursor* self)
{
    if (!self) return;
    xtc_formatClear(&self->m_insertFormat);
    XMemset(self, 0, sizeof(*self));
    self->m_x = -1;
    /* m_document 是借用（不拥有）——置 NULL 由 XMemset 完成。 */
}

/* ==================== 状态 ==================== */

bool XTextCursor_isNull(const XTextCursor* self)
{
    return !self || !self->m_document;
}

int XTextCursor_position(const XTextCursor* self)
{
    return (self && self->m_document) ? self->m_position : -1;
}

int XTextCursor_anchor(const XTextCursor* self)
{
    return (self && self->m_document) ? self->m_anchor : -1;
}

int XTextCursor_blockNumber(const XTextCursor* self)
{
    int bi;
    int off;
    if (!self || !self->m_document) return 0;
    xtc_posLocate(self->m_document, self->m_position, &bi, &off);
    return bi;
}

void XTextCursor_setPosition(XTextCursor* self, int pos, int mode)
{
    if (!self || !self->m_document) return;
    /* Qt setPosition：越界忽略（qtextcursor.cpp:1126-1129 qWarning +
     * return；探针 P10：pos==characterCount 与负值均不动）。 */
    if (pos < 0 || pos >= xtc_charCount(self->m_document)) return;
    xtc_invalidateX(self); /* Qt setPosition 尾部 setX 的惰性等价。 */
    xtc_setPositionImpl(self, pos, mode);
}

int XTextCursor_selectionStart(const XTextCursor* self)
{
    if (!self || !self->m_document) return -1;
    return (self->m_position < self->m_anchor) ? self->m_position
                                               : self->m_anchor;
}

int XTextCursor_selectionEnd(const XTextCursor* self)
{
    if (!self || !self->m_document) return -1;
    return (self->m_position > self->m_anchor) ? self->m_position
                                               : self->m_anchor;
}

bool XTextCursor_hasSelection(const XTextCursor* self)
{
    return self && self->m_document && self->m_position != self->m_anchor;
}

bool XTextCursor_hasComplexSelection(const XTextCursor* self)
{
    (void)self;
    /* 复杂选区=表格单元格选区；本子集无表格——Qt complexSelectionTable
     * 的空表分支恒 NULL（qtextcursor.cpp:1583-1589），false 是真值。 */
    return false;
}

char* XTextCursor_selectedText(const XTextCursor* self)
{
    static const char kParaSep[3] = { '\xE2', '\x80', '\xA9' }; /* U+2029 */
    const XTextDocument* doc;
    int start;
    int end;
    int bi;
    int off;
    int firstBlock;
    int total = 0;
    char* out;
    int o = 0;
    if (!self || !self->m_document) return NULL;
    if (self->m_position == self->m_anchor) return NULL;
    doc = self->m_document;
    start = XTextCursor_selectionStart(self);
    end = XTextCursor_selectionEnd(self);
    xtc_posLocate(doc, start, &bi, &off);
    firstBlock = bi;
    /* 两遍式（先量后填）；每块贡献：
     * - 文本字节 [from, min(take, blockLen))（from=首块的块内偏移，否则 0）；
     * - 块分隔符位（stop = 块长+1）被 take 覆盖时记 U+2029 三字节——末块
     *   的「分隔符」是文档尾（position charCount-1 为最大合法位），选区
     *   终点钳位后到不了它 ⇒ 恒不含（Qt 探针 P1 口径）。 */
    for (; bi < doc->m_blockCount; ++bi) {
        const XTDBlock* blk = &doc->m_blocks[bi];
        int acc = xtc_blockStart(doc, bi);
        int blen = xtc_blockCharCount(blk);
        int stop = (bi == doc->m_blockCount - 1) ? blen : blen + 1;
        int from = (bi == firstBlock) ? off : 0;
        int take = end - acc;
        int textEnd;
        if (take > stop) take = stop;
        textEnd = (take < blen) ? take : blen;
        if (textEnd > from) total += textEnd - from;
        if (take == stop && stop > blen) total += 3;
        if (acc + stop >= end) break;
    }
    out = (char*)XMalloc_System((size_t)total + 1);
    if (!out) return NULL;
    bi = firstBlock;
    for (; bi < doc->m_blockCount; ++bi) {
        const XTDBlock* blk = &doc->m_blocks[bi];
        int acc = xtc_blockStart(doc, bi);
        int blen = xtc_blockCharCount(blk);
        int stop = (bi == doc->m_blockCount - 1) ? blen : blen + 1;
        int from = (bi == firstBlock) ? off : 0;
        int take = end - acc;
        int textEnd;
        int k;
        if (take > stop) take = stop;
        textEnd = (take < blen) ? take : blen;
        for (k = from; k < textEnd && o < total; ++k)
            out[o++] = xtc_byteAt(blk, k);
        if (take == stop && stop > blen && o + 3 <= total) {
            XMemcpy(out + o, kParaSep, 3);
            o += 3;
        }
        if (acc + stop >= end) break;
    }
    out[o] = '\0';
    return out;
}

void XTextCursor_clearSelection(XTextCursor* self)
{
    if (!self || !self->m_document) return;
    /* Qt clearSelection：anchor=adjusted_anchor=position 且**无条件**
     * currentCharFormat=-1（qtextcursor.cpp:1620-1626）——插入格式缓存
     * 一并失效（[复核 2026-11-16 P3] 补齐：此前只折叠 anchor）；不动文档
     * （不重定位）。 */
    self->m_anchor = self->m_position;
    self->m_hasInsertFormat = 0;
}

/* ==================== 移动 ==================== */

/** @brief 位置 pos 的 (块, 偏移) 是否恰为块尾分隔符位（= 块界光标位）。 */
static bool xtc_atBlockEndPos(const XTextDocument* doc, int pos, int bi,
                              int off)
{
    return bi < doc->m_blockCount &&
           off >= xtc_blockCharCount(&doc->m_blocks[bi]);
}

/** @brief 位置 (bi, off) 前一个码点的文档位置（块内 UTF-8 回退；跨续字节）。 */
static int xtc_prevCodepointPos(const XTextDocument* doc, int bi, int off)
{
    const XTDBlock* blk;
    int p;
    if (!doc || bi < 0 || bi >= doc->m_blockCount) return -1;
    blk = &doc->m_blocks[bi];
    p = off - 1;
    if (p < 0) return -1;
    while (p > 0) {
        unsigned char c = (unsigned char)xtc_byteAt(blk, p);
        if ((c & 0xC0) != 0x80) break; /* 非续字节 = 码点起点 */
        --p;
    }
    return xtc_blockStart(doc, bi) + p;
}

bool XTextCursor_movePosition(XTextCursor* self, int op, int mode, int n)
{
    XTextDocument* doc;
    int bi;
    int off;
    int blockLen;
    int blockStartPos;
    int newPosition;
    if (!self || !self->m_document) return false;
    doc = self->m_document;
    if (doc->m_blockCount <= 0) return false;
    if (n < 1) n = 1;
    /* Qt movePosition：定位操作强制 n=1（qtextcursor.cpp:1226-1234）。 */
    if (op == (int)XTextCursorMove_Start ||
        op == (int)XTextCursorMove_StartOfLine ||
        op == (int)XTextCursorMove_End ||
        op == (int)XTextCursorMove_EndOfLine)
        n = 1;
    while (n > 0) {
        bool moved;
        xtc_posLocate(doc, self->m_position, &bi, &off);
        blockLen = xtc_blockCharCount(&doc->m_blocks[bi]);
        blockStartPos = xtc_blockStart(doc, bi);
        newPosition = self->m_position;
        switch (op) {
        case (int)XTextCursorMove_NoMove:
            return true; /* Qt：NoMove 恒 true（:353-354） */

        case (int)XTextCursorMove_Start:
            newPosition = 0;
            break;
        case (int)XTextCursorMove_End:
            newPosition = xtc_charCount(doc) - 1;
            break;
        case (int)XTextCursorMove_StartOfLine:
        case (int)XTextCursorMove_StartOfBlock:
            /* 无折行子集：行=块（Qt blockLayout 无软换行时 textStart=0）。 */
            newPosition = blockStartPos;
            break;
        case (int)XTextCursorMove_EndOfLine:
        case (int)XTextCursorMove_EndOfBlock:
            /* 分隔符前（Qt blockIt.position()+length()-1；:452-470/
             * :488-492）。 */
            newPosition = blockStartPos + blockLen;
            break;
        case (int)XTextCursorMove_PreviousBlock:
            if (bi == 0) return false; /* Qt：首块返回 false（:370-377）。 */
            newPosition = xtc_blockStart(doc, bi - 1);
            break;
        case (int)XTextCursorMove_NextBlock:
            if (bi + 1 >= doc->m_blockCount) return false; /* :493-500。 */
            newPosition = xtc_blockStart(doc, bi + 1);
            break;
        case (int)XTextCursorMove_PreviousCharacter: {
            /* 有选区且 MoveAnchor：折叠到左端（qtextcursor.cpp:378-383，
               探针 P4 PrevCharCollapse）。 */
            if (mode == (int)XTextCursorMode_MoveAnchor &&
                self->m_position != self->m_anchor) {
                newPosition = (self->m_position < self->m_anchor)
                                  ? self->m_position : self->m_anchor;
                break;
            }
            /* doc 级 previousCursorPosition（qtextdocument_p.cpp:1346-1357）：
               块界跨到分隔符位，块内退一个码点。 */
            if (self->m_position == 0) return false;
            if (off == 0)
                newPosition = blockStartPos - 1;
            else
                newPosition = xtc_prevCodepointPos(doc, bi, off);
            break;
        }
        case (int)XTextCursorMove_NextCharacter: {
            /* 有选区且 MoveAnchor：折叠到右端（qtextcursor.cpp:501-506）。 */
            if (mode == (int)XTextCursorMode_MoveAnchor &&
                self->m_position != self->m_anchor) {
                newPosition = (self->m_position > self->m_anchor)
                                  ? self->m_position : self->m_anchor;
                break;
            }
            if (self->m_position >= xtc_charCount(doc) - 1) return false;
            if (off >= blockLen)
                newPosition = blockStartPos + blockLen + 1; /* 跨块界 */
            else
                newPosition = self->m_position + xtc_seqLenAt(doc,
                                                              self->m_position);
            break;
        }
        case (int)XTextCursorMove_Left:
            /* 逻辑左（visualMovement=false 缺省）：有选区折叠到左端。 */
            if (mode == (int)XTextCursorMode_MoveAnchor &&
                self->m_position != self->m_anchor) {
                newPosition = (self->m_position < self->m_anchor)
                                  ? self->m_position : self->m_anchor;
                break;
            }
            if (self->m_position == 0) return false;
            if (off == 0)
                newPosition = blockStartPos - 1;
            else
                newPosition = xtc_prevCodepointPos(doc, bi, off);
            break;
        case (int)XTextCursorMove_Right:
            if (mode == (int)XTextCursorMode_MoveAnchor &&
                self->m_position != self->m_anchor) {
                newPosition = (self->m_position > self->m_anchor)
                                  ? self->m_position : self->m_anchor;
                break;
            }
            if (self->m_position >= xtc_charCount(doc) - 1) return false;
            if (off >= blockLen)
                newPosition = blockStartPos + blockLen + 1;
            else
                newPosition = self->m_position + xtc_seqLenAt(doc,
                                                              self->m_position);
            break;
        case (int)XTextCursorMove_StartOfWord: {
            /* qtextcursor.cpp:392-411：relativePos==0 原地（moved=false）；
               块尾且前一字符为空白/词分隔符返回 false；否则进一位后按
               PreviousWord 跳词。 */
            if (off == 0) break; /* newPosition = position → moved=false */
            if (off == blockLen && blockLen > 0 &&
                (xtc_isSpace(xtc_byteAt(&doc->m_blocks[bi], blockLen - 1)) ||
                 xtc_isWordSep(xtc_byteAt(&doc->m_blocks[bi], blockLen - 1))))
                return false;
            {
                int p = (off < blockLen) ? off + 1 : off;
                while (p > 0 &&
                       xtc_isSpace(xtc_byteAt(&doc->m_blocks[bi], p - 1)))
                    --p;
                if (p > 0 &&
                    xtc_isWordSep(xtc_byteAt(&doc->m_blocks[bi], p - 1))) {
                    --p;
                    while (p > 0 &&
                           xtc_isWordSep(xtc_byteAt(&doc->m_blocks[bi], p - 1)))
                        --p;
                } else {
                    while (p > 0 && xtc_isWordChar(
                                        xtc_byteAt(&doc->m_blocks[bi], p - 1)))
                        --p;
                }
                newPosition = blockStartPos + p;
            }
            break;
        }
        case (int)XTextCursorMove_EndOfWord: {
            /* qtextcursor.cpp:471-487：relativePos >= 块长返回 false；
               词分隔符串越过、词内越过（空白/分隔符止步）。 */
            int p = off;
            if (p >= blockLen) return false;
            if (xtc_isWordSep(xtc_byteAt(&doc->m_blocks[bi], p))) {
                ++p;
                while (p < blockLen &&
                       xtc_isWordSep(xtc_byteAt(&doc->m_blocks[bi], p)))
                    ++p;
            } else {
                while (p < blockLen &&
                       xtc_isWordChar(xtc_byteAt(&doc->m_blocks[bi], p)))
                    ++p;
            }
            newPosition = blockStartPos + p;
            break;
        }
        case (int)XTextCursorMove_PreviousWord: {
            /* QTextLayout::previousCursorPosition（qtextlayout.cpp:686-713）
               的块内版 + doc 级块界跨界（qtextdocument_p.cpp:1346-1357）。 */
            int p;
            if (self->m_position == 0) return false;
            if (off == 0) {
                newPosition = blockStartPos - 1;
                break;
            }
            p = off;
            while (p > 0 &&
                   xtc_isSpace(xtc_byteAt(&doc->m_blocks[bi], p - 1)))
                --p;
            if (p > 0 && xtc_isWordSep(xtc_byteAt(&doc->m_blocks[bi], p - 1))) {
                --p;
                while (p > 0 &&
                       xtc_isWordSep(xtc_byteAt(&doc->m_blocks[bi], p - 1)))
                    --p;
            } else {
                while (p > 0 && xtc_isWordChar(
                                    xtc_byteAt(&doc->m_blocks[bi], p - 1)))
                    --p;
            }
            newPosition = blockStartPos + p;
            break;
        }
        case (int)XTextCursorMove_NextWord: {
            /* QTextLayout::nextCursorPosition（qtextlayout.cpp:650-677）
               的块内版 + doc 级块界跨界。 */
            int p;
            if (self->m_position >= xtc_charCount(doc) - 1) return false;
            if (off >= blockLen) {
                newPosition = blockStartPos + blockLen + 1;
                break;
            }
            p = off;
            if (xtc_isWordSep(xtc_byteAt(&doc->m_blocks[bi], p))) {
                ++p;
                while (p < blockLen &&
                       xtc_isWordSep(xtc_byteAt(&doc->m_blocks[bi], p)))
                    ++p;
            } else {
                while (p < blockLen &&
                       xtc_isWordChar(xtc_byteAt(&doc->m_blocks[bi], p)))
                    ++p;
            }
            while (p < blockLen &&
                   xtc_isSpace(xtc_byteAt(&doc->m_blocks[bi], p)))
                ++p;
            newPosition = blockStartPos + p;
            break;
        }
        case (int)XTextCursorMove_Up:
        case (int)XTextCursorMove_Down: {
            /* 视觉行移动（qtextcursor.cpp:412-447/:520-557）：sticky x 经
             * XTextLayout colToX/xToCol 映射；块首上移/末块下移 false；
             * m_x 跨多次垂直移动保持（adjustX=false）。 */
            int target;
            XtcLineCtx curCtx;
            XtcLineCtx dstCtx;
            int x;
            int col;
            if (self->m_x < 0) {
                /* 现算 sticky x（Qt setX：lineForTextPosition+cursorToX）。 */
                if (!xtc_lineCtxInit(&curCtx, doc, bi)) return false;
                x = XTextLayout_colToX(&curCtx.layout, 0, off);
                xtc_lineCtxDeinit(&curCtx);
                self->m_x = x;
            }
            x = self->m_x;
            if (op == (int)XTextCursorMove_Up) {
                if (bi == 0) return false;
                target = bi - 1;
            } else {
                if (bi + 1 >= doc->m_blockCount) return false;
                target = bi + 1;
            }
            if (!xtc_lineCtxInit(&dstCtx, doc, target)) return false;
            col = XTextLayout_xToCol(&dstCtx.layout, 0, x);
            xtc_lineCtxDeinit(&dstCtx);
            newPosition = xtc_blockStart(doc, target) + col;
            moved = xtc_setPositionImpl(self, newPosition, mode);
            /* 垂直移动不刷新 sticky x（adjustX=false；也同步活动光标）。 */
            if (doc)
                XTextDocument_setCursorPosition(doc, self->m_position);
            n--;
            continue;
        }
        default:
            return false;
        }
        if (newPosition < 0 ||
            newPosition >= xtc_charCount(doc))
            return false;
        xtc_invalidateX(self); /* 非垂直移动：sticky x 失效（adjustX=true）。 */
        moved = xtc_setPositionImpl(self, newPosition, mode);
        if (!moved) return false;
        n--;
    }
    return true;
}

/* ==================== 插入 / 删除 ==================== */

/** @brief 插入格式解析：显式缓存优先，否则 charFormat 继承规则现算
 *         （文档级 insertTextAt 的 NULL 格式分支承载同一规则）。 */
static const XTDCharFormat* xtc_resolveInsertFormat(XTextCursor* self,
                                                    XTDCharFormat* scratch)
{
    if (self->m_hasInsertFormat) return &self->m_insertFormat;
    XTextDocument_charFormatAt(self->m_document, self->m_position, scratch);
    return scratch;
}

void XTextCursor_insertText(XTextCursor* self, const char* utf8)
{
    XTextDocument* doc;
    int start;
    int hasSel;
    XTDCharFormat scratch;
    const XTDCharFormat* fmt;
    int added;
    if (!self || !self->m_document) return;
    doc = self->m_document;
    hasSel = (self->m_position != self->m_anchor);
    start = XTextCursor_selectionStart(self);
    XMemset(&scratch, 0, sizeof(scratch)); /* 空壳（缓存路径不清栈垃圾）。 */
    /* Qt insertText：有选区先在同一编辑块里删除（qtextcursor.cpp
     * :1408-1414）；op 按 position 侧定（探针 R1/R2 undo 光标口径）。 */
    if (hasSel) XTextDocument_beginEditBlock(doc);
    if (hasSel) {
        XTextDocument_removeRangeOp(doc, start,
                                    XTextCursor_selectionEnd(self) - start,
                                    (self->m_position > self->m_anchor)
                                        ? (int)XTDUndoOp_MoveCursor
                                        : (int)XTDUndoOp_KeepCursor);
    }
    fmt = xtc_resolveInsertFormat(self, &scratch);
    added = XTextDocument_insertTextAtFmt(doc, start, utf8 ? utf8 : "", fmt);
    if (hasSel) XTextDocument_endEditBlock(doc);
    if (added > 0 || hasSel) {
        /* 光标/锚点落插入文本末尾（Qt：insert 随 op=MoveCursor 骑行；
           remove 后已折叠在起点）。 */
        self->m_position = start + added;
        self->m_anchor = self->m_position;
        self->m_hasInsertFormat = 0; /* Qt adjustPosition currentCharFormat=-1 */
        self->m_x = -1;
        XTextDocument_setCursorPosition(doc, self->m_position);
    }
    xtc_formatClear(&scratch); /* 继承槽的字符串字段归本栈帧，用完即清。 */
}

void XTextCursor_insertTextFormat(XTextCursor* self, const char* utf8,
                                  const XTextCharFormat* fmt)
{
    XTextDocument* doc;
    int start;
    int hasSel;
    XTDCharFormat converted;
    int added;
    if (!self || !self->m_document || !fmt) {
        XTextCursor_insertText(self, utf8);
        return;
    }
    doc = self->m_document;
    hasSel = (self->m_position != self->m_anchor);
    start = XTextCursor_selectionStart(self);
    XMemset(&converted, 0, sizeof(converted)); /* 空壳（栈垃圾防御） */
    XTextDocument_textFormatToCharFormat(&converted, fmt);
    if (hasSel) XTextDocument_beginEditBlock(doc);
    if (hasSel) {
        XTextDocument_removeRangeOp(doc, start,
                                    XTextCursor_selectionEnd(self) - start,
                                    (self->m_position > self->m_anchor)
                                        ? (int)XTDUndoOp_MoveCursor
                                        : (int)XTDUndoOp_KeepCursor);
    }
    added = XTextDocument_insertTextAtFmt(doc, start, utf8 ? utf8 : "",
                                          &converted);
    if (hasSel) XTextDocument_endEditBlock(doc);
    if (added > 0) {
        /* 记插入格式缓存（Qt：插入后 charFormat() 即该格式——片段继承链
           的等价承载）；先深拷贝再清本栈帧载荷。 */
        xtc_formatAssign(&self->m_insertFormat, &converted);
        self->m_hasInsertFormat = 1;
    }
    xtc_formatClear(&converted);
    if (added > 0 || hasSel) {
        self->m_position = start + added;
        self->m_anchor = self->m_position;
        self->m_x = -1;
        XTextDocument_setCursorPosition(doc, self->m_position);
    }
}

void XTextCursor_insertBlock(XTextCursor* self)
{
    XTextDocument* doc;
    int start;
    int hasSel;
    if (!self || !self->m_document) return;
    doc = self->m_document;
    /* Qt insertBlock(format, charFormat)（qtextcursor.cpp:2040-2042）：
     * **无条件** beginEditBlock → d->remove() → insertBlock ——有选区时
     * 先删选区并折叠 anchor=position（remove 的 :115-142 口径），无选区
     * remove 为 no-op。 */
    hasSel = (self->m_position != self->m_anchor);
    start = XTextCursor_selectionStart(self);
    if (hasSel) XTextDocument_beginEditBlock(doc);
    if (hasSel) {
        XTextDocument_removeRangeOp(doc, start,
                                    XTextCursor_selectionEnd(self) - start,
                                    (self->m_position > self->m_anchor)
                                        ? (int)XTDUndoOp_MoveCursor
                                        : (int)XTDUndoOp_KeepCursor);
    }
    if (XTextDocument_insertBlockAt(doc, start) > 0) {
        /* 光标/锚点落新块块首（探针 P9/N3）：分隔符占据插入位 ⇒ 新块起于
         * 插入位+1（有选区时插入位=选区起点——Qt 探针 select(1,4)+
         * insertBlock ⇒ "a\nef"、pos=2）。 */
        self->m_position = start + 1;
        self->m_anchor = self->m_position;
        self->m_hasInsertFormat = 0;
        self->m_x = -1;
        XTextDocument_setCursorPosition(doc, self->m_position);
    } else if (hasSel) {
        /* 拆块失败（块数上限）但选区已删：光标折叠到删除起点（remove
         * 口径；insertText 同款）。 */
        self->m_position = start;
        self->m_anchor = start;
        self->m_hasInsertFormat = 0;
        self->m_x = -1;
        XTextDocument_setCursorPosition(doc, self->m_position);
    }
    if (hasSel) XTextDocument_endEditBlock(doc);
}

void XTextCursor_removeSelectedText(XTextCursor* self)
{
    XTextDocument* doc;
    int start;
    int len;
    if (!self || !self->m_document) return;
    if (self->m_position == self->m_anchor) return;
    doc = self->m_document;
    start = XTextCursor_selectionStart(self);
    len = XTextCursor_selectionEnd(self) - start;
    XTextDocument_removeRangeOp(doc, start, len,
                                (self->m_position > self->m_anchor)
                                    ? (int)XTDUndoOp_MoveCursor
                                    : (int)XTDUndoOp_KeepCursor);
    /* 光标与锚点折叠到选区起点（两方向同值，探针 P6/P6b）。 */
    self->m_position = start;
    self->m_anchor = start;
    self->m_hasInsertFormat = 0;
    self->m_x = -1;
    XTextDocument_setCursorPosition(doc, self->m_position);
}

void XTextCursor_deleteChar(XTextCursor* self)
{
    XTextDocument* doc;
    if (!self || !self->m_document) return;
    if (self->m_position != self->m_anchor) {
        XTextCursor_removeSelectedText(self);
        return;
    }
    doc = self->m_document;
    if (self->m_position >= xtc_charCount(doc) - 1) return; /* 文档尾 no-op */
    {
        int seq = xtc_seqLenAt(doc, self->m_position);
        XTextDocument_removeRangeOp(doc, self->m_position, seq,
                                    (int)XTDUndoOp_KeepCursor);
        /* 光标不动（KeepCursor；探针 P5a：pos 不变）。 */
        self->m_hasInsertFormat = 0;
        self->m_x = -1;
    }
}

void XTextCursor_deletePreviousChar(XTextCursor* self)
{
    XTextDocument* doc;
    int prev;
    if (!self || !self->m_document) return;
    if (self->m_position != self->m_anchor) {
        XTextCursor_removeSelectedText(self);
        return;
    }
    doc = self->m_document;
    if (self->m_anchor < 1) return; /* 文档首 no-op（Qt anchor<1；P5a） */
    prev = self->m_position - 1;
    {
        int bi;
        int off;
        int start;
        int seq;
        xtc_posLocate(doc, prev, &bi, &off);
        if (off >= xtc_blockCharCount(&doc->m_blocks[bi])) {
            /* 前一字符是块分隔符（删跨块界：分隔符位删除）。 */
            start = prev;
            seq = 1;
        } else {
            /* 码点起点回退：byte[s] 是续字节则继续前移（byte[s] 为起点的
             * 前一轮已把 s 推到它）——删除区间 = [码点起点, position)。 */
            const XTDBlock* blk = &doc->m_blocks[bi];
            int s = off;
            while (s > 0) {
                unsigned char c = (unsigned char)xtc_byteAt(blk, s);
                if ((c & 0xC0) != 0x80) break; /* 码点起点。 */
                s--;
            }
            start = xtc_blockStart(doc, bi) + s;
            seq = self->m_position - start;
        }
        XTextDocument_removeRangeOp(doc, start, seq,
                                    (int)XTDUndoOp_MoveCursor);
        /* 光标回退到删除起点（op=MoveCursor；探针 P5a：pos-1）。 */
        self->m_position = start;
        self->m_anchor = start;
        self->m_hasInsertFormat = 0;
        self->m_x = -1;
        XTextDocument_setCursorPosition(doc, self->m_position);
    }
}

/* ==================== 格式 ==================== */

void XTextCursor_charFormatInline(const XTextCursor* self, XTDCharFormat* out)
{
    if (!out) return;
    XMemset(out, 0, sizeof(*out));
    if (!self || !self->m_document) return;
    XTextDocument_charFormatAt(self->m_document, self->m_position, out);
}

void XTextCursor_charFormat(const XTextCursor* self, XTextCharFormat* out)
{
    XTDCharFormat inlineFmt;
    if (!out) return;
    XMemset(out, 0, sizeof(*out)); /* 栈值先清零（deinit 不防垃圾指针）。 */
    XTextCursor_charFormatInline(self, &inlineFmt);
    XTextDocument_charFormatToTextFormat(out, &inlineFmt);
    xtc_formatClear(&inlineFmt);
}

void XTextCursor_setCharFormat(XTextCursor* self, const XTextCharFormat* fmt)
{
    XTDCharFormat converted;
    if (!self || !self->m_document || !fmt) return;
    XMemset(&converted, 0, sizeof(converted)); /* 空壳（栈垃圾防御） */
    XTextDocument_textFormatToCharFormat(&converted, fmt);
    if (self->m_position == self->m_anchor) {
        /* 无选区：只记插入格式缓存（Qt setCharFormat 空选区分支
           :1898-1907；不动文档、无信号——探针 P2c/N5）。 */
        xtc_formatAssign(&self->m_insertFormat, &converted);
        self->m_hasInsertFormat = 1;
        xtc_formatClear(&converted);
        return;
    }
    XTextDocument_setCharFormatRange(self->m_document,
                                     XTextCursor_selectionStart(self),
                                     XTextCursor_selectionEnd(self) -
                                         XTextCursor_selectionStart(self),
                                     fmt, false);
    xtc_formatClear(&converted);
}

void XTextCursor_mergeCharFormat(XTextCursor* self, const XTextCharFormat* modifier)
{
    if (!self || !self->m_document || !modifier) return;
    if (self->m_position == self->m_anchor) {
        /* Qt mergeCharFormat 空选区分支（:1917-1926）：charFormat()
           merge(modifier) 后进缓存。 */
        XTDCharFormat base;
        XTextDocument_charFormatAt(self->m_document, self->m_position, &base);
        {
            XTextFormat asFmt;
            XTextDocument_charFormatToTextFormat(&asFmt, &base);
            XTextFormat_merge(&asFmt, modifier);
            xtc_formatClear(&self->m_insertFormat);
            XMemset(&self->m_insertFormat, 0, sizeof(XTDCharFormat));
            XTextDocument_textFormatToCharFormat(&self->m_insertFormat, &asFmt);
            XTextFormat_deinit(&asFmt);
        }
        xtc_formatClear(&base);
        self->m_hasInsertFormat = 1;
        return;
    }
    XTextDocument_setCharFormatRange(self->m_document,
                                     XTextCursor_selectionStart(self),
                                     XTextCursor_selectionEnd(self) -
                                         XTextCursor_selectionStart(self),
                                     modifier, true);
}

void XTextCursor_blockFormat(const XTextCursor* self, XTextBlockFormat* out)
{
    int bi;
    int off;
    const XTDBlock* blk;
    if (!out) return;
    XMemset(out, 0, sizeof(*out)); /* 栈值先清零（deinit 不防垃圾指针）。 */
    XTextFormat_init(out, XTextFormatType_BlockFormat);
    if (!self || !self->m_document || self->m_document->m_blockCount <= 0)
        return;
    xtc_posLocate(self->m_document, self->m_position, &bi, &off);
    blk = &self->m_document->m_blocks[bi];
    if (blk->alignment)
        XTextFormat_setProperty_int(out, XTextFormatProperty_BlockAlignment,
                                    blk->alignment);
    if (blk->indentLevel)
        XTextFormat_setProperty_int(out, XTextFormatProperty_BlockIndent,
                                    blk->indentLevel);
    if (blk->headingLevel)
        XTextFormat_setProperty_int(out, XTextFormatProperty_HeadingLevel,
                                    blk->headingLevel);
}

void XTextCursor_setBlockFormat(XTextCursor* self, const XTextBlockFormat* fmt)
{
    int align;
    int indent;
    int heading;
    if (!self || !self->m_document || !fmt) return;
    /* Qt setBlockFormat：整值替换（alignment 未设回落 AlignLeft=0x01——
       XTextBlockFormat_alignment 的缺省口径；探针 P12 三块同改）。 */
    align = XTextBlockFormat_alignment(fmt);
    indent = XTextFormat_intProperty(fmt, XTextFormatProperty_BlockIndent);
    heading = XTextFormat_intProperty(fmt, XTextFormatProperty_HeadingLevel);
    XTextDocument_setBlockFormatRange(self->m_document,
                                      XTextCursor_selectionStart(self),
                                      XTextCursor_selectionEnd(self) -
                                          XTextCursor_selectionStart(self),
                                      align, indent, heading);
}

/* ==================== 撤销事务 ==================== */

void XTextCursor_beginEditBlock(XTextCursor* self)
{
    if (!self) return;
    XTextDocument_beginEditBlock(self->m_document);
}

void XTextCursor_endEditBlock(XTextCursor* self)
{
    if (!self) return;
    XTextDocument_endEditBlock(self->m_document);
}

#endif /* XTEXTDOCUMENT_ON */
#endif /* XGUI_ON */
