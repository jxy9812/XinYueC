#include "XTextDocument.h"
#include "XStringUtils.h"
#include "XMemory.h"
#include "XObject.h"
#include "XEvent.h"
#include "XVarList.h"
#include "XGuiConfig.h"

#include "XAlgorithm.h"
#include <stdarg.h>

#if XTEXTDOCUMENT_ON

static void xtd_formatAssign(XTDCharFormat* dst, const XTDCharFormat* src);
static void xtd_formatClear(XTDCharFormat* fmt);
static bool xtd_formatEqual(const XTDCharFormat* a, const XTDCharFormat* b);
static void xtd_fragClear(XTDFragment* frag);
static const char* xtd_fragText(const XTDFragment* frag);

/* ==================== 生命周期 ==================== */

static void VX_td_deinit(XTextDocument* self);

XVtable* XTextDocument_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XTextDocument)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VX_td_deinit);
    return XVTABLE_DEFAULT;
}

static void VX_td_deinit(XTextDocument* self)
{
    int i, j;
    if (!self) return;
    if (self->m_blocks) {
        for (i = 0; i < self->m_blockCount; ++i) {
            for (j = 0; j < self->m_blocks[i].fragmentCount; ++j)
                xtd_fragClear(&self->m_blocks[i].fragments[j]);
            if (self->m_blocks[i].blockFormat) {
                XString_delete_base(self->m_blocks[i].blockFormat);
                self->m_blocks[i].blockFormat = NULL;
            }
        }
        XFree_System(self->m_blocks);
        self->m_blocks = NULL;
    }
    if (self->m_title) {
        XString_delete_base(self->m_title);
        self->m_title = NULL;
    }
    if (self->m_url) {
        XString_delete_base(self->m_url);
        self->m_url = NULL;
    }
    XClass_Deinit_Parent(XObject, (XObject*)self);
}

void XTextDocument_init(XTextDocument* self)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XObject_init(&self->m_base);
    XClassSetVtable(self, XTextDocument);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    self->m_capacity = 16;
    self->m_blocks = (XTDBlock*)XMalloc_System(
        sizeof(XTDBlock) * (size_t)self->m_capacity);
    if (self->m_blocks) XMemset(self->m_blocks, 0,
        sizeof(XTDBlock) * (size_t)self->m_capacity);
    self->m_blockCount = 1; /* 至少一个空块。 */
    self->m_undoRedoEnabled = true;
    self->m_title = XString_create();
    self->m_url = XString_create();
}

XTextDocument* XTextDocument_create_ex(XMemoryType memory)
{
    XTextDocument* self = (XTextDocument*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XTextDocument_init(self);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 内部工具 ==================== */

static void xtd_ensureCapacity(XTextDocument* self, int minCap)
{
    int newCap;
    XTDBlock* p;
    if (!self || minCap <= self->m_capacity) return;
    newCap = self->m_capacity * 2;
    if (newCap < minCap) newCap = minCap;
    p = (XTDBlock*)XRealloc_System(self->m_blocks,
        sizeof(XTDBlock) * (size_t)newCap);
    if (!p) return;
    self->m_blocks = p;
    self->m_capacity = newCap;
}

static void xtd_emitVoid(XTextDocument* self, size_t signal)
{
    XVarList* args = XVarList_create(0);
    if (!args) return;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

/** @brief 深拷贝字符格式（含字符串字段）。 */
static void xtd_formatAssign(XTDCharFormat* dst, const XTDCharFormat* src)
{
    if (!dst || !src || dst == src) return;
    if (dst->fontFamily && src->fontFamily)
        XString_assign(dst->fontFamily, src->fontFamily);
    else if (!dst->fontFamily && src->fontFamily)
        dst->fontFamily = XString_create_copy(src->fontFamily);
    if (dst->anchorHref && src->anchorHref)
        XString_assign(dst->anchorHref, src->anchorHref);
    else if (!dst->anchorHref && src->anchorHref)
        dst->anchorHref = XString_create_copy(src->anchorHref);
    dst->bold = src->bold;
    dst->italic = src->italic;
    dst->underline = src->underline;
    dst->strikeOut = src->strikeOut;
    dst->fgColor = src->fgColor;
    dst->bgColor = src->bgColor;
    dst->fontPointSize = src->fontPointSize;
    dst->superScript = src->superScript;
    dst->subScript = src->subScript;
}

/** @brief 释放字符格式字符串字段。 */
static void xtd_formatClear(XTDCharFormat* fmt)
{
    if (!fmt) return;
    if (fmt->fontFamily) {
        XString_delete_base(fmt->fontFamily);
        fmt->fontFamily = NULL;
    }
    if (fmt->anchorHref) {
        XString_delete_base(fmt->anchorHref);
        fmt->anchorHref = NULL;
    }
}

/** @brief 格式相等比较（对标 memcmp 语义，字符串按内容比较）。 */
static bool xtd_formatEqual(const XTDCharFormat* a, const XTDCharFormat* b)
{
    if (!a || !b) return a == b;
    if (a->bold != b->bold || a->italic != b->italic ||
        a->underline != b->underline || a->strikeOut != b->strikeOut ||
        a->fgColor != b->fgColor || a->bgColor != b->bgColor ||
        a->fontPointSize != b->fontPointSize ||
        a->superScript != b->superScript || a->subScript != b->subScript)
        return false;
    if ((a->fontFamily == NULL) != (b->fontFamily == NULL)) return false;
    if (a->fontFamily && !XString_equals(a->fontFamily, b->fontFamily,
                                         XChar_CaseSensitive)) return false;
    if ((a->anchorHref == NULL) != (b->anchorHref == NULL)) return false;
    if (a->anchorHref && !XString_equals(a->anchorHref, b->anchorHref,
                                         XChar_CaseSensitive)) return false;
    return true;
}

/** @brief 释放片段全部字符串与图片并清零。 */
static void xtd_fragClear(XTDFragment* frag)
{
    if (!frag) return;
    if (frag->text) {
        XString_delete_base(frag->text);
        frag->text = NULL;
    }
    if (frag->image) {
        XImage_delete_base((XClass*)frag->image);
        frag->image = NULL;
    }
    xtd_formatClear(&frag->fmt);
}

/** @brief 读取片段文本（空串安全）。 */
static const char* xtd_fragText(const XTDFragment* frag)
{
    const char* t;
    if (!frag || !frag->text) return "";
    t = XString_toUtf8(frag->text);
    return t ? t : "";
}

static void xtd_changed(XTextDocument* self)
{
    if (self) {
        self->m_modified++;
        xtd_emitVoid(self, (size_t)XTextDocument_contentsChanged_signal);
    }
}

/* ==================== 块操作 ==================== */

void XTextDocument_clear(XTextDocument* self)
{
    int b;
    int j;
    if (!self) return;
    if (self->m_blocks) {
        /* 释放全部已用块（含 setHtml 填充的多块），不止 block0：
           此前只清 block0 导致多块 fragment text 泄漏（Phase 3.2）。 */
        for (b = 0; b < self->m_blockCount && b < self->m_capacity; ++b) {
            XTDBlock* blk = &self->m_blocks[b];
            for (j = 0; j < blk->fragmentCount; ++j)
                xtd_fragClear(&blk->fragments[j]);
            if (blk->blockFormat) {
                XString_delete_base(blk->blockFormat);
                blk->blockFormat = NULL;
            }
            XMemset(blk, 0, sizeof(XTDBlock));
        }
    }
    self->m_blockCount = 1;
    xtd_changed(self);
}

bool XTextDocument_isEmpty(const XTextDocument* self)
{
    int i, j;
    if (!self) return true;
    for (i = 0; i < self->m_blockCount; ++i) {
        for (j = 0; j < self->m_blocks[i].fragmentCount; ++j) {
            if (xtd_fragText(&self->m_blocks[i].fragments[j])[0]) return false;
        }
    }
    return true;
}

int XTextDocument_blockCount(const XTextDocument* self)
{
    return self ? self->m_blockCount : 0;
}

int XTextDocument_characterCount(const XTextDocument* self)
{
    int i, j, total = 0;
    if (!self) return 0;
    for (i = 0; i < self->m_blockCount; ++i)
        for (j = 0; j < self->m_blocks[i].fragmentCount; ++j)
            total += (int)XStrlen(xtd_fragText(&self->m_blocks[i].fragments[j]));
    return total;
}

/* ==================== 文本导出 ==================== */

char* XTextDocument_toPlainText(const XTextDocument* self)
{
    int i, j, total = 1;
    char* out;
    size_t o = 0;
    if (!self) return NULL;
    for (i = 0; i < self->m_blockCount; ++i)
        for (j = 0; j < self->m_blocks[i].fragmentCount; ++j)
            total += (int)XStrlen(xtd_fragText(&self->m_blocks[i].fragments[j]));
    total += self->m_blockCount; /* 换行符。 */
    out = (char*)XMalloc_System((size_t)total);
    if (!out) return NULL;
    out[0] = '\0';
    for (i = 0; i < self->m_blockCount; ++i) {
        for (j = 0; j < self->m_blocks[i].fragmentCount; ++j) {
            const char* t = xtd_fragText(&self->m_blocks[i].fragments[j]);
            size_t len = XStrlen(t);
            XMemcpy(out + o, t, len); o += len;
        }
        if (i + 1 < self->m_blockCount) out[o++] = '\n';
    }
    out[o] = '\0';
    return out;
}

void XTextDocument_setPlainText(XTextDocument* self, const char* utf8)
{
    const char* p;
    int blockIdx = 0;
    if (!self) return;
    XTextDocument_clear(self);
    xtd_emitVoid(self, (size_t)XTextDocument_documentLayoutChanged_signal);
    xtd_emitVoid(self, (size_t)XTextDocument_undoCommandAdded_signal);
    if (!utf8 || !utf8[0]) return;
    p = utf8;
    while (*p) {
        const char* nl = XStrchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) : XStrlen(p);
        if (blockIdx >= XTD_MAX_BLOCKS) break;
        if (blockIdx >= self->m_capacity) {
            /* 增长前必须补容量；realloc 不清零，新增槽位逐块置零，
               否则旧实现直接抬 m_blockCount 会让析构/clear 读到
               未初始化块的垃圾 fragment 指针（越界读）。 */
            int bi;
            int oldCap = self->m_capacity;
            xtd_ensureCapacity(self, blockIdx + 1);
            if (blockIdx >= self->m_capacity) break; /* 分配失败，截断 */
            for (bi = oldCap; bi < self->m_capacity; ++bi)
                XMemset(&self->m_blocks[bi], 0, sizeof(XTDBlock));
        }
        {
            XTDFragment* frag = &self->m_blocks[blockIdx].fragments[0];
            if (frag->text) XString_delete_base(frag->text);
            frag->text = (len > 0)
                ? XString_create_with_length_utf8(p, len)
                : XString_create_utf8("");
            self->m_blocks[blockIdx].fragmentCount = 1;
        }
        blockIdx++;
        if (!nl) break;
        p = nl + 1;
    }
    if (blockIdx > self->m_blockCount) self->m_blockCount = blockIdx;
    xtd_changed(self);
}


/* ==================== HTML 解析（富文本渲染子集） ==================== */

/* 内联标签嵌套上限两层：栈深 3（如 <b><i><u> 三层格式同时生效），更深
 * 层的开启标签整体忽略，其闭合标签按溢出计数平衡丢弃（任务边界）。 */
#define XTD_INLINE_STACK 3

/* 嵌套列表深度上限（§8.0g5）：超深 li 钳制在本层渲染（有界简化）。 */
#define XTD_LIST_MAX_DEPTH 4

/** @brief 列表上下文（嵌套栈元素：该层是否有序）。 */
typedef struct XTDListCtx
{
    bool ordered;
} XTDListCtx;

/** @brief HTML <font size> 1-7 标准磅值表（对标 Qt QTextHtmlParser 的
 *         字号梯度近似：8/10/12/14/18/24/36 磅）。 */
static const int xtd_fontSizeTable[7] = { 8, 10, 12, 14, 18, 24, 36 };

/** @brief 大小写不敏感匹配 p 处的标签/属性名（名称边界：后随字符须非
 *         字母数字，防止 "br" 被 "b" 误配；对标 HTML 标签大小写不敏感）。 */
static bool xtd_nameAt(const char* p, const char* name)
{
    while (*name) {
        char c = *p;
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if (c != *name) return false;
        ++p;
        ++name;
    }
    if (((*p >= 'a') && (*p <= 'z')) || ((*p >= 'A') && (*p <= 'Z')) ||
        ((*p >= '0') && (*p <= '9')))
        return false;
    return true;
}

/** @brief 定位标签结束 '>'（引号内的 '>' 不算；对标属性值含 '>' 场景）。 */
static const char* xtd_tagEnd(const char* p)
{
    char quote = 0;
    while (*p) {
        if (quote) {
            if (*p == quote) quote = 0;
        } else if (*p == '"' || *p == '\'') {
            quote = *p;
        } else if (*p == '>') {
            return p;
        }
        ++p;
    }
    return p;
}

/** @brief 在标签体内读取属性值（双引号/单引号/无引号；属性值不做实体
 *         展开——子集边界）。 */
static bool xtd_attrValue(const char* begin, const char* end,
                          const char* name, char* out, size_t cap)
{
    size_t nlen = XStrlen(name);
    const char* q;
    if (cap == 0 || !begin || !end || nlen == 0) return false;
    out[0] = '\0';
    q = begin;
    while (q < end) {
        /* 对齐到属性名起点（跳过空白）。 */
        while (q < end && (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n'))
            ++q;
        if (q >= end) break;
        if ((size_t)(end - q) > nlen && XStrncmp(q, name, nlen) == 0) {
            const char* v = q + nlen;
            bool hit = false;
            if (*v == '=') {
                hit = true;
            } else if (*v == ' ' || *v == '\t' || *v == '\r' || *v == '\n') {
                while (v < end && (*v == ' ' || *v == '\t' ||
                                   *v == '\r' || *v == '\n'))
                    ++v;
                hit = (v < end && *v == '=');
            }
            if (hit && v < end) {
                char quote = 0;
                const char* s;
                size_t n;
                ++v;
                while (v < end && (*v == ' ' || *v == '\t')) ++v;
                if (v < end && (*v == '"' || *v == '\'')) quote = *v++;
                s = v;
                while (v < end) {
                    if (quote) {
                        if (*v == quote) break;
                    } else if (*v == ' ' || *v == '\t' ||
                               *v == '\r' || *v == '\n') {
                        break;
                    }
                    ++v;
                }
                n = (size_t)(v - s);
                if (n >= cap) n = cap - 1;
                if (n > 0) XMemcpy(out, s, n);
                out[n] = '\0';
                return true;
            }
        }
        /* 跳过当前 token。 */
        while (q < end && *q != ' ' && *q != '\t' && *q != '\r' && *q != '\n')
            ++q;
    }
    return false;
}

/** @brief style 属性串中取指定 CSS 属性值（分号分隔 name:value 对，
 *         名比较大小写不敏感；值截到分号/空白；§8.0g5 span 子集）。 */
static bool xtd_styleValue(const char* style, const char* prop,
                           char* out, int outCap)
{
    int plen;
    const char* p;
    if (!style || !prop || !out || outCap <= 0) return false;
    plen = (int)XStrlen(prop);
    p = style;
    while (*p) {
        const char* q;
        while (*p == ' ' || *p == '\t' || *p == ';') ++p;
        if (!*p) break;
        q = p;
        while (*q && *q != ':') ++q;
        if (*q != ':') break;
        if ((int)(q - p) == plen) {
            bool match = true;
            int i;
            for (i = 0; i < plen; ++i) {
                char c = p[i], d = prop[i];
                if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
                if (d >= 'A' && d <= 'Z') d = (char)(d - 'A' + 'a');
                if (c != d) { match = false; break; }
            }
            if (match) {
                const char* v = q + 1;
                const char* e;
                int n;
                while (*v == ' ' || *v == '\t') ++v;
                e = v;
                while (*e && *e != ';' && *e != ' ' && *e != '\t') ++e;
                n = (int)(e - v);
                if (n > 0) {
                    if (n >= outCap) n = outCap - 1;
                    XMemcpy(out, v, (size_t)n);
                    out[n] = '\0';
                    return true;
                }
            }
        }
        p = (*q) ? q + 1 : q;
    }
    return false;
}

/** @brief 大小写不敏感字符串比较（HTML 属性值用；对标 HTML 大小写
 *         不敏感语义）。 */
static bool xtd_ieqStr(const char* a, const char* b)
{
    if (!a || !b) return false;
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
        if (ca != cb) return false;
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

/** @brief 颜色字面量解析：#RGB/#RRGGBB 与常用命名色（对标 Qt 颜色名
 *         解析的常量子集；解析失败返回 false 保持默认色）。 */
static bool xtd_parseColor(const char* s, uint32_t* out)
{
    static const struct XTDNamedColor
    {
        const char* name;
        uint32_t argb;
    } named[] = {
        { "black",   0xFF000000u }, { "white",   0xFFFFFFFFu },
        { "red",     0xFFFF0000u }, { "green",   0xFF008000u },
        { "blue",    0xFF0000FFu }, { "yellow",  0xFFFFFF00u },
        { "cyan",    0xFF00FFFFu }, { "magenta", 0xFFFF00FFu },
        { "gray",    0xFF808080u }, { "grey",    0xFF808080u },
        { "orange",  0xFFFFA500u }, { "purple",  0xFF800080u }
    };
    int i;
    if (!s || !s[0] || !out) return false;
    for (i = 0; i < (int)(sizeof(named) / sizeof(named[0])); ++i) {
        if (xtd_ieqStr(s, named[i].name)) {
            *out = named[i].argb;
            return true;
        }
    }
    if (*s == '#') {
        uint32_t r = 0, g = 0, b = 0;
        const char* hx = s + 1;
        int n = 0;
        while (n < 6 && hx[n]) {
            int d;
            char c = hx[n];
            if (c >= '0' && c <= '9') d = c - '0';
            else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
            else break;
            if (n < 2) r = r * 16 + (uint32_t)d;
            else if (n < 4) g = g * 16 + (uint32_t)d;
            else b = b * 16 + (uint32_t)d;
            ++n;
        }
        if (n == 6) {
            *out = 0xFF000000u | (r << 16) | (g << 8) | b;
            return true;
        }
        if (n == 3) {
            /* #RGB 短格式：半字节展开（对标 HTML 颜色简写）。 */
            r = (r << 4) | r;
            g = (g << 4) | g;
            b = (b << 4) | b;
            *out = 0xFF000000u | (r << 16) | (g << 8) | b;
            return true;
        }
    }
    return false;
}

/** @brief <font size> 值应用：1-7 查表（对标 Qt HTML 字号梯度）；带
 *         +/- 号为相对调整（以当前字号在表中的就近档位为基准）；超出
 *         1-7 的纯数字按磅值直接承载（与 toHtml 导出口径互逆）。 */
static void xtd_applyFontSize(XTDCharFormat* fmt, const char* val)
{
    const char* q = val;
    int n = 0;
    int sign = 0;
    int i;
    if (!fmt || !val || !val[0]) return;
    if (*q == '+') { sign = 1; ++q; }
    else if (*q == '-') { sign = -1; ++q; }
    while (*q >= '0' && *q <= '9') {
        n = n * 10 + (*q - '0');
        ++q;
    }
    if (q == val || (sign != 0 && q == val + 1)) return; /* 无数字。 */
    if (sign != 0) {
        int base = 0;
        i = 0;
        for (i = 0; i < 7; ++i) {
            if (xtd_fontSizeTable[i] <= fmt->fontPointSize) base = i;
        }
        i = base + sign * n;
        if (i < 0) i = 0;
        if (i > 6) i = 6;
        fmt->fontPointSize = xtd_fontSizeTable[i];
    } else if (n >= 1 && n <= 7) {
        fmt->fontPointSize = xtd_fontSizeTable[n - 1];
    } else {
        if (n < 1) n = 1;
        if (n > 96) n = 96;
        fmt->fontPointSize = n;
    }
}

/** @brief align 属性词映射（对标 Qt::Alignment 数值承载：Left=1/
 *         Right=2/HCenter=4/Justify=8）。 */
static int xtd_parseAlign(const char* val)
{
    if (!val || !val[0]) return 0;
    if (xtd_ieqStr(val, "center") || xtd_ieqStr(val, "middle"))
        return (int)XTDAlignment_HCenter;
    if (xtd_ieqStr(val, "right")) return (int)XTDAlignment_Right;
    if (xtd_ieqStr(val, "justify")) return (int)XTDAlignment_Justify;
    return (int)XTDAlignment_Left;
}

/** @brief 向当前块追加 len 字节（ASCII 时 len=1；UTF-8 多字节序列须
 *         整序列写入——XString 的 UTF-8 流转换对单字节残序列拒收）：
 *         同格式并入末片段；格式变化新建片段；末片段为图片片段时不可
 *         并入（文本新开片段——图片是原子承载）；片段槽满时一律并入
 *         末片段（含尾片段为图片的情形——格式差异与图片原子性丢弃、
 *         文本不丢——子集边界）。 */
static void xtd_appendSpan(XTextDocument* self, int blockIdx,
                           const char* s, int len,
                           const XTDCharFormat* cur)
{
    XTDBlock* blk;
    int fi;
    if (!self || !self->m_blocks || blockIdx < 0 ||
        blockIdx >= XTD_MAX_BLOCKS || len <= 0)
        return;
    xtd_ensureCapacity(self, blockIdx + 1);
    if (blockIdx >= self->m_capacity) return; /* 扩容失败：截断。 */
    if (blockIdx >= self->m_blockCount) self->m_blockCount = blockIdx + 1;
    blk = &self->m_blocks[blockIdx];
    fi = blk->fragmentCount;
    if (fi < 0) fi = 0; /* 防御边界：脏负计数钳回合法区，防负下标越界。 */
    if (fi >= XTD_MAX_FRAGMENTS_PER_BLOCK) {
        /* P0 根因修复（钳位）：旧实现池满且尾片段为图片时绕过合并分支
         * 直落新建片段分支，&blk->fragments[fi]（= fragmentCount 标量区
         * 起点）被 XMemset/formatAssign 越界写覆写，并置
         * fragmentCount=fi+1 级联越界；触发链：256 块上限致
         * advanceBlock 拒绝推进后同块继续追加。对标同文件
         * addFragment/insertImage 的池满钳位：满池不再新建片段，一律
         * 并入末片段保文本不丢（图片片段 text 本为空串，并入即顺延
         * 追加）。 */
        XTDFragment* pf =
            &blk->fragments[XTD_MAX_FRAGMENTS_PER_BLOCK - 1];
        if (!pf->text) pf->text = XString_create();
        if (pf->text) XString_append_with_length_utf8(pf->text, s, (size_t)len);
        return;
    }
    if (fi > 0 && !blk->fragments[fi - 1].image &&
        xtd_formatEqual(&blk->fragments[fi - 1].fmt, cur)) {
        XTDFragment* pf = &blk->fragments[fi - 1];
        if (!pf->text) pf->text = XString_create();
        if (pf->text) XString_append_with_length_utf8(pf->text, s, (size_t)len);
        return;
    }
    {
        /* 新建片段分支：fi < XTD_MAX_FRAGMENTS_PER_BLOCK 由上方钳位保证。 */
        XTDFragment* nf = &blk->fragments[fi];
        XMemset(nf, 0, sizeof(XTDFragment));
        xtd_formatAssign(&nf->fmt, cur);
        nf->text = XString_create();
        if (nf->text) XString_append_with_length_utf8(nf->text, s, (size_t)len);
        blk->fragmentCount = fi + 1;
    }
}

/** @brief 向当前块追加一个 ASCII 字节（xtd_appendSpan 的单字节形态）。 */
static void xtd_appendByte(XTextDocument* self, int blockIdx, char ch,
                           const XTDCharFormat* cur)
{
    xtd_appendSpan(self, blockIdx, &ch, 1, cur);
}

/** @brief p 处一个 UTF-8 序列的字节长度（前导字节定长；残序列/游离
 *         续字节按 1 处理——XString 校验拒收后丢弃，与既有口径一致）；
 *         序列越过多字节尾则截到 NUL 为止。 */
static int xtd_utf8SeqLen(const char* p)
{
    unsigned char c = (unsigned char)p[0];
    int seq = 1;
    if (c >= 0xC2 && c < 0xE0) seq = 2;
    else if (c >= 0xE0 && c < 0xF0) seq = 3;
    else if (c >= 0xF0 && c < 0xF5) seq = 4;
    while (seq > 1 && p[seq - 1] == '\0') --seq;
    return seq;
}

/** @brief 提交源空白折叠产生的待定空格：块级边界（文档起始/块级标签
 *         之后）的行首空白整段剥离，其余折叠为单个空格；随后清除块
 *         起始态，后续空格按词间隔保留。对标 QTextHtmlParser 的空白
 *         处理子集（连续空白折叠 + 块界换行剥离）；<pre> 内容区的空白
 *         保真由解析主循环的 inPre 分支逐字节直载，不经过本折叠路径。 */
static void xtd_flushPendingSpace(XTextDocument* self, int blockIdx,
                                  const XTDCharFormat* cur,
                                  bool* pendingSpace, bool* atBlockStart)
{
    if (*pendingSpace) {
        if (!*atBlockStart)
            xtd_appendByte(self, blockIdx, ' ', cur);
        *pendingSpace = false;
    }
    *atBlockStart = false;
}

/** @brief 另起新块（容量钳位；容量不足时保持原块不换——子集边界）。 */
static void xtd_advanceBlock(XTextDocument* self, int* blockIdx,
                             int alignment, int headingLevel,
                             bool listItem, bool ordered)
{
    if (!self || !blockIdx || !self->m_blocks) return;
    if (*blockIdx + 1 >= XTD_MAX_BLOCKS) return;
    xtd_ensureCapacity(self, *blockIdx + 2);
    if (*blockIdx + 1 >= self->m_capacity) return; /* 扩容失败。 */
    (*blockIdx)++;
    if (*blockIdx >= self->m_blockCount) self->m_blockCount = *blockIdx + 1;
    XMemset(&self->m_blocks[*blockIdx], 0, sizeof(XTDBlock));
    self->m_blocks[*blockIdx].alignment = alignment;
    self->m_blocks[*blockIdx].headingLevel = headingLevel;
    self->m_blocks[*blockIdx].isListItem = listItem;
    self->m_blocks[*blockIdx].isOrdered = ordered;
}

/** @brief h1-h6 标签名识别：命中返回级别 1-6，否则 0。 */
static int xtd_headingName(const char* p)
{
    static const char* const names[6] = { "h1", "h2", "h3", "h4", "h5", "h6" };
    int i;
    for (i = 0; i < 6; ++i) {
        if (xtd_nameAt(p, names[i])) return i + 1;
    }
    return 0;
}

/** @brief 开启内联标签：栈未满压栈（浅拷贝：标量复制+对象指针共享）后
 *         由调用方应用变更；超出嵌套上限两层则溢出计数（标签整体忽略）。 */
static bool xtd_openInline(XTDCharFormat* cur, XTDCharFormat* stack,
                           int* depth, int* overflow)
{
    if (!cur || !stack || !depth || !overflow) return false;
    if (*depth >= XTD_INLINE_STACK) {
        (*overflow)++;
        return false;
    }
    stack[(*depth)++] = *cur;
    return true;
}

/** @brief 闭合内联标签：溢出优先消耗；否则弹栈恢复整个格式状态。
 *         <a> 新建的 href 对象在恢复点释放（片段已深拷贝，不受影响）；
 *         弹栈恢复的指针若与当前相同则不释放（别名共享，避免双 freeing）。 */
static void xtd_closeInline(XTDCharFormat* cur, XTDCharFormat* stack,
                            int* depth, int* overflow)
{
    XTDCharFormat top;
    if (!cur || !stack || !depth || !overflow) return;
    if (*overflow > 0) {
        (*overflow)--;
        return;
    }
    if (*depth <= 0) return;
    top = stack[--(*depth)];
    if (cur->anchorHref != top.anchorHref && cur->anchorHref)
        XString_delete_base(cur->anchorHref);
    *cur = top;
}

/** @brief 文档起始的空首块占用（对标 Qt：文档起始的 <p>/<li> 直接使用
 *         首个空块，不产生前置空行；返回 true 表示已占用，调用方不再
 *         另起新块）。 */
static bool xtd_claimStartBlock(XTextDocument* self, int* blockIdx,
                                int alignment, int headingLevel,
                                bool listItem, bool ordered,
                                bool* startClaimed)
{
    if (!self || !self->m_blocks || !blockIdx || !startClaimed) return false;
    if (*startClaimed || *blockIdx != 0 || self->m_blockCount <= 0 ||
        self->m_capacity <= 0)
        return false;
    if (self->m_blocks[0].fragmentCount != 0 ||
        self->m_blocks[0].headingLevel != 0 || self->m_blocks[0].isListItem)
        return false;
    self->m_blocks[0].alignment = alignment;
    self->m_blocks[0].headingLevel = headingLevel;
    self->m_blocks[0].isListItem = listItem;
    self->m_blocks[0].isOrdered = ordered;
    *startClaimed = true;
    (void)blockIdx;
    return true;
}

/** @brief HTML 解析核心（setHtml 全量替换 / appendHtml 尾部追加共用）。
 * @details 渲染子集：b/strong、i/em、u、s/strike/del、sup/sub、
 *          font(color/size)、span(style background-color)、
 *          br、p/div(align)、h1-h6、ul/ol/li（嵌套分级，§8.0g5）、pre、
 *          a(href)；实体
 *          &amp;/&lt;/&gt;/&quot;/&apos;/&#39;/&nbsp;/&#NN;。嵌套上限
 *          两层（栈深 3，三层格式同时生效），更深层忽略并平衡闭合。源空白
 *          按 HTML 语义折叠（对标 QTextHtmlParser）：空格/制表/回车/
 *          换行的连续串折叠为单个空格，块级标签（p/div/h1-h6/li/br）
 *          边界的换行与缩进整段剥离（延迟提交实现：段尾/块界前未
 *          提交的空白自然丢弃）；<pre> 例外：内容区空白逐字节保真
 *          （不折叠、不剥离），字体族切为 monospace（对标
 *          QTextHtmlParser 的 Html_pre：WhiteSpacePre + monospace 字族，
 *          字体层不可解析时按默认字体回退），退出 pre 恢复折叠。 */
static void xtd_parseHtml(XTextDocument* self, const char* html,
                          bool clearFirst)
{
    const char* p;
    XTDCharFormat cur;
    XTDCharFormat stack[XTD_INLINE_STACK];
    int depth = 0;
    int overflow = 0;
    int blockIdx;
    XTDListCtx listStack[XTD_LIST_MAX_DEPTH]; /* 嵌套列表上下文栈。 */
    int listDepth = 0;
    int freshAtLevel = 0; /* 最近一次列表开标签所在层（li 序号重起标记）。 */
    bool startClaimed = false; /* 文档起始空首块已被块级标签占用。 */
    bool pendingSpace = false; /* 源空白延迟提交：连续空白折叠为单空格。 */
    bool atBlockStart = true;  /* 块级边界后（文档起始亦算）：行首空白剥离。 */
    bool inPre = false;        /* <pre> 内容区：空白逐字节保真（不折叠）。 */
    XString* preSavedFamily = NULL; /* 进入 pre 前的字体族（退出恢复用）。 */

    if (!self || !html || !self->m_blocks) return;
    XMemset(&cur, 0, sizeof(cur));
    if (clearFirst) {
        XTextDocument_clear(self);
        blockIdx = 0;
    } else {
        xtd_ensureCapacity(self, self->m_blockCount > 0 ? self->m_blockCount : 1);
        blockIdx = self->m_blockCount - 1;
        if (blockIdx < 0) blockIdx = 0;
        if (blockIdx >= self->m_capacity) return;
        /* 末块非空：另起新段追加（对标 QTextDocument::appendHtml）。 */
        if (self->m_blocks[blockIdx].fragmentCount > 0)
            xtd_advanceBlock(self, &blockIdx, 0, 0, false, false);
    }
    p = html;
    while (*p && blockIdx < XTD_MAX_BLOCKS) {
        if (*p == '<') {
            const char* tagEnd;
            ++p;
            if (*p == '/') {
                ++p;
                if (xtd_nameAt(p, "b") || xtd_nameAt(p, "strong") ||
                    xtd_nameAt(p, "i") || xtd_nameAt(p, "em") ||
                    xtd_nameAt(p, "u") || xtd_nameAt(p, "s") ||
                    xtd_nameAt(p, "strike") || xtd_nameAt(p, "del") ||
                    xtd_nameAt(p, "font") || xtd_nameAt(p, "a") ||
                    xtd_nameAt(p, "sup") || xtd_nameAt(p, "sub")) {
                    xtd_closeInline(&cur, stack, &depth, &overflow);
                } else if (xtd_nameAt(p, "ul") || xtd_nameAt(p, "ol")) {
                    /* 嵌套列表逐层弹栈（§8.0g5；越界闭合钳在 0）。 */
                    if (listDepth > 0) --listDepth;
                } else if (xtd_nameAt(p, "span")) {
                    xtd_closeInline(&cur, stack, &depth, &overflow);
                } else if (xtd_nameAt(p, "pre")) {
                    if (inPre) {
                        inPre = false;
                        /* 恢复进入 pre 前的字体族；monospace 串若仍被
                         * 内联栈条目别名共享则不释放（同 anchorHref 的
                         * 别名防双释放口径，宁漏不悬）。 */
                        if (cur.fontFamily != preSavedFamily) {
                            int si;
                            bool aliased = false;
                            for (si = 0; si < depth; ++si) {
                                if (stack[si].fontFamily == cur.fontFamily) {
                                    aliased = true;
                                    break;
                                }
                            }
                            if (!aliased && cur.fontFamily)
                                XString_delete_base(cur.fontFamily);
                            cur.fontFamily = preSavedFamily;
                        }
                        preSavedFamily = NULL;
                    }
                    pendingSpace = false; /* 块界：待定空白不带入后续文本。 */
                    atBlockStart = true;  /* 退出 pre 恢复折叠，行首空白剥离。 */
                }
                tagEnd = xtd_tagEnd(p);
                p = (*tagEnd) ? tagEnd + 1 : tagEnd;
            } else if (*p == '!' || *p == '?') {
                /* 注释/处理指令：跳到 '>'（不含 --> 特判——子集边界）。 */
                tagEnd = xtd_tagEnd(p);
                p = (*tagEnd) ? tagEnd + 1 : tagEnd;
            } else {
                const char* content = p;
                tagEnd = xtd_tagEnd(p);
                if (xtd_nameAt(p, "b") || xtd_nameAt(p, "strong")) {
                    if (xtd_openInline(&cur, stack, &depth, &overflow))
                        cur.bold = true;
                } else if (xtd_nameAt(p, "i") || xtd_nameAt(p, "em")) {
                    if (xtd_openInline(&cur, stack, &depth, &overflow))
                        cur.italic = true;
                } else if (xtd_nameAt(p, "u")) {
                    if (xtd_openInline(&cur, stack, &depth, &overflow))
                        cur.underline = true;
                } else if (xtd_nameAt(p, "s") || xtd_nameAt(p, "strike") ||
                           xtd_nameAt(p, "del")) {
                    if (xtd_openInline(&cur, stack, &depth, &overflow))
                        cur.strikeOut = true;
                } else if (xtd_nameAt(p, "sup")) {
                    if (xtd_openInline(&cur, stack, &depth, &overflow))
                        cur.superScript = true;
                } else if (xtd_nameAt(p, "sub")) {
                    if (xtd_openInline(&cur, stack, &depth, &overflow))
                        cur.subScript = true;
                } else if (xtd_nameAt(p, "font")) {
                    char buf[64];
                    if (xtd_openInline(&cur, stack, &depth, &overflow)) {
                        uint32_t color;
                        if (xtd_attrValue(content, tagEnd, "color", buf,
                                          sizeof(buf)) &&
                            xtd_parseColor(buf, &color))
                            cur.fgColor = color;
                        if (xtd_attrValue(content, tagEnd, "size", buf,
                                          sizeof(buf)))
                            xtd_applyFontSize(&cur, buf);
                    }
                } else if (xtd_nameAt(p, "a")) {
                    char buf[512];
                    if (xtd_openInline(&cur, stack, &depth, &overflow)) {
                        if (xtd_attrValue(content, tagEnd, "href", buf,
                                          sizeof(buf)) &&
                            buf[0]) {
                            /* href 对象独立新建（栈中旧指针不受影响）。 */
                            if (cur.anchorHref)
                                XString_delete_base(cur.anchorHref);
                            cur.anchorHref = XString_create_utf8(buf);
                        }
                    }
                } else if (xtd_nameAt(p, "br")) {
                    /* br 继承当前段对齐/标题（对标 Qt：同段内软换行）。 */
                    int align = 0;
                    int heading = 0;
                    if (blockIdx < self->m_blockCount &&
                        blockIdx < self->m_capacity) {
                        align = self->m_blocks[blockIdx].alignment;
                        heading = self->m_blocks[blockIdx].headingLevel;
                    }
                    xtd_advanceBlock(self, &blockIdx, align, heading,
                                     false, false);
                    atBlockStart = true; /* 块界：剥离后续行首空白。 */
                } else if (xtd_nameAt(p, "p") || xtd_nameAt(p, "div")) {
                    char buf[32];
                    int align = 0;
                    if (xtd_attrValue(content, tagEnd, "align", buf,
                                      sizeof(buf)))
                        align = xtd_parseAlign(buf);
                    if (!(clearFirst &&
                          xtd_claimStartBlock(self, &blockIdx, align, 0,
                                              false, false, &startClaimed)))
                        xtd_advanceBlock(self, &blockIdx, align, 0,
                                         false, false);
                    atBlockStart = true; /* 块界：剥离后续行首空白。 */
                } else if (xtd_headingName(p) > 0) {
                    char buf[32];
                    int level = xtd_headingName(p);
                    int align = 0;
                    if (xtd_attrValue(content, tagEnd, "align", buf,
                                      sizeof(buf)))
                        align = xtd_parseAlign(buf);
                    if (!(clearFirst &&
                          xtd_claimStartBlock(self, &blockIdx, align, level,
                                              false, false, &startClaimed)))
                        xtd_advanceBlock(self, &blockIdx, align, level,
                                         false, false);
                    atBlockStart = true; /* 块界：剥离后续行首空白。 */
                } else if (xtd_nameAt(p, "ul")) {
                    /* 嵌套列表（§8.0g5）：上下文栈逐层下压（超深钳制
                     * 在上限层——与内联栈同口径的有界简化）。 */
                    if (listDepth < XTD_LIST_MAX_DEPTH) {
                        listStack[listDepth].ordered = false;
                        ++listDepth;
                        freshAtLevel = listDepth;
                    }
                } else if (xtd_nameAt(p, "ol")) {
                    if (listDepth < XTD_LIST_MAX_DEPTH) {
                        listStack[listDepth].ordered = true;
                        ++listDepth;
                        freshAtLevel = listDepth;
                    }
                } else if (xtd_nameAt(p, "li")) {
                    bool liOrdered = listDepth > 0 &&
                                     listStack[listDepth - 1].ordered;
                    if (!(clearFirst &&
                          xtd_claimStartBlock(self, &blockIdx, 0, 0,
                                              listDepth > 0, liOrdered,
                                              &startClaimed)))
                        xtd_advanceBlock(self, &blockIdx, 0, 0,
                                         listDepth > 0, liOrdered);
                    if (listDepth > 0 && blockIdx < self->m_capacity) {
                        self->m_blocks[blockIdx].indentLevel = listDepth;
                        /* 本层列表的首个 li：序号计数重起标记（渲染侧
                         * 分级计数用）。 */
                        if (freshAtLevel == listDepth)
                            self->m_blocks[blockIdx].listFresh = true;
                    }
                    freshAtLevel = 0;
                    atBlockStart = true; /* 块界：剥离后续行首空白。 */
                } else if (xtd_nameAt(p, "span")) {
                    /* span 子集（§8.0g5）：仅解析 style 的
                     * background-color 片段背景；其余样式静默。恒入内联
                     * 栈保持闭合配对平衡。 */
                    char sbuf[128];
                    char cbuf[64];
                    if (xtd_openInline(&cur, stack, &depth, &overflow)) {
                        if (xtd_attrValue(content, tagEnd, "style", sbuf,
                                          sizeof(sbuf)) &&
                            xtd_styleValue(sbuf, "background-color", cbuf,
                                           sizeof(cbuf))) {
                            uint32_t bg;
                            if (xtd_parseColor(cbuf, &bg))
                                cur.bgColor = bg;
                        }
                    }
                } else if (xtd_nameAt(p, "pre")) {
                    /* pre 为块级元素（对标 Qt：Html_pre 建块）；开标签前
                     * 的待定空白按块界丢弃。 */
                    if (!(clearFirst &&
                          xtd_claimStartBlock(self, &blockIdx, 0, 0,
                                              false, false, &startClaimed)))
                        xtd_advanceBlock(self, &blockIdx, 0, 0,
                                         false, false);
                    pendingSpace = false;
                    atBlockStart = true;
                    if (!inPre) {
                        /* 字体族切为 monospace（对标 Qt Html_pre；字体层
                         * 解析不到时按默认字体回退）；原字体族留存退出
                         * 恢复。串创建失败则保持原字体（仅失等宽）。 */
                        preSavedFamily = cur.fontFamily;
                        cur.fontFamily = XString_create_utf8("monospace");
                        inPre = true;
                    }
                }
                /* 其余标签（span/img/table/hr 等）整体跳过——子集边界。 */
                p = (*tagEnd) ? tagEnd + 1 : tagEnd;
            }
        } else if (*p == '&') {
            /* 实体产物均为可见字符：先提交待定空格（词间隔语义；
             * 与实体展开、嵌套钳制互不影响）。 */
            xtd_flushPendingSpace(self, blockIdx, &cur,
                                  &pendingSpace, &atBlockStart);
            if (XStrncmp(p, "&amp;", 5) == 0) {
                xtd_appendByte(self, blockIdx, '&', &cur);
                p += 5;
            } else if (XStrncmp(p, "&lt;", 4) == 0) {
                xtd_appendByte(self, blockIdx, '<', &cur);
                p += 4;
            } else if (XStrncmp(p, "&gt;", 4) == 0) {
                xtd_appendByte(self, blockIdx, '>', &cur);
                p += 4;
            } else if (XStrncmp(p, "&quot;", 6) == 0) {
                xtd_appendByte(self, blockIdx, '"', &cur);
                p += 6;
            } else if (XStrncmp(p, "&apos;", 6) == 0) {
                xtd_appendByte(self, blockIdx, '\'', &cur);
                p += 6;
            } else if (XStrncmp(p, "&#39;", 5) == 0) {
                xtd_appendByte(self, blockIdx, '\'', &cur);
                p += 5;
            } else if (XStrncmp(p, "&nbsp;", 6) == 0) {
                /* nbsp 折叠为普通空格（对标 Qt 富文本行为）。 */
                xtd_appendByte(self, blockIdx, ' ', &cur);
                p += 6;
            } else if (p[1] == '#') {
                /* 数字字符引用 &#NN;：十进制 ASCII 范围直载（多字节
                 * 码点合成不做——子集边界）。 */
                int code = 0;
                int digits = 0;
                const char* q = p + 2;
                while (*q >= '0' && *q <= '9') {
                    code = code * 10 + (*q - '0');
                    ++digits;
                    ++q;
                    if (code > 0x10FFFF) break;
                }
                if (digits > 0 && *q == ';' && code > 0 && code < 128) {
                    xtd_appendByte(self, blockIdx, (char)code, &cur);
                    p = q + 1;
                } else {
                    xtd_appendByte(self, blockIdx, '&', &cur);
                    ++p;
                }
            } else {
                /* 未知实体：'&' 按字面量透传（后续字符正常解析）。 */
                xtd_appendByte(self, blockIdx, *p, &cur);
                ++p;
            }
        } else if (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
            if (inPre) {
                /* pre 内容区例外（对标 QTextHtmlParser 的 WhiteSpacePre）：
                 * 空白逐字节保真——不折叠、不剥离，缩进/换行原样承载
                 * （空白均为单字节 ASCII，直接整段承载）。 */
                xtd_appendSpan(self, blockIdx, p, 1, &cur);
                ++p;
            } else {
                /* 源空白折叠（对标 QTextHtmlParser 空白处理）：空格/制表/
                 * 回车/换行的连续串延迟合并为单个空格，待下一可见字符
                 * 提交——块界后剥离（atBlockStart）、段尾未提交自然丢弃；
                 * 行内标签两侧的单空格词间隔语义保留（不跨标签丢弃）。 */
                pendingSpace = true;
                ++p;
            }
        } else {
            xtd_flushPendingSpace(self, blockIdx, &cur,
                                  &pendingSpace, &atBlockStart);
            if (inPre) {
                /* pre 内容区：UTF-8 序列整段保真承载（多字节字符按序列
                 * 推进；等价逐字节不折叠——残序列单字节会被 XString 的
                 * UTF-8 流转换拒收，见 xtd_appendSpan 注）。 */
                int seq = xtd_utf8SeqLen(p);
                xtd_appendSpan(self, blockIdx, p, seq, &cur);
                p += seq;
            } else {
                /* 可见文本与 pre 同口径：UTF-8 序列整段承载。此前逐字节
                 * xtd_appendByte 会把多字节字符拆成残序列，XChar_
                 * fromUtf8Stream 拒收导致 setHtml/appendHtml 静默丢弃
                 * 全部非 ASCII 字符（P0：富文本预览只剩 ASCII 子串）。 */
                int seq = xtd_utf8SeqLen(p);
                xtd_appendSpan(self, blockIdx, p, seq, &cur);
                p += seq;
            }
        }
    }
    /* 收尾：释放解析器仍持有的 href 对象（片段已深拷贝，不受影响）。 */
    if (cur.anchorHref) XString_delete_base(cur.anchorHref);
    /* 块数钳位：保证 m_blockCount 不越过容量（此前的 <br> 连发路径存在
     * 越界读隐患，见 setPlainText 容量补零修复的同族问题）。 */
    if (self->m_blockCount < 1) self->m_blockCount = 1;
    if (self->m_blockCount > self->m_capacity)
        self->m_blockCount = self->m_capacity;
    xtd_changed(self);
}

/** @brief 解析 HTML 到块+片段结构（全量替换；对标 QTextDocument::setHtml
 *         的子集：b/i/u/s、font color/size、br、p align、a href；源空白
 *         按 HTML 语义折叠——连续空白并作单空格、块界换行剥离）。 */
void XTextDocument_setHtml(XTextDocument* self, const char* html)
{
    xtd_parseHtml(self, html, true);
}

/* ==================== HTML 生成 ==================== */

/** @brief 追加格式化片段到生成缓冲（snprintf 语义；写入越界时钳位，
 *         避免 o 越过容量造成 size_t 下溢）。 */
static void xtd_hPut(char* out, size_t cap, size_t* o, const char* fmt, ...)
{
    va_list ap;
    int n;
    if (!out || !o || cap < 2 || *o + 1 >= cap) return;
    va_start(ap, fmt);
    n = vsnprintf(out + *o, cap - *o, fmt, ap);
    va_end(ap);
    if (n > 0) *o += (size_t)n;
    if (*o >= cap) *o = cap - 1;
}

/** @brief 文本转义写入（& < > 与可选引号；对标 toHtml 实体转义）。 */
static void xtd_hEscape(char* out, size_t cap, size_t* o, const char* s,
                        bool escapeQuote)
{
    for (; s && *s; ++s) {
        switch (*s) {
        case '<':
            xtd_hPut(out, cap, o, "&lt;");
            break;
        case '>':
            xtd_hPut(out, cap, o, "&gt;");
            break;
        case '&':
            xtd_hPut(out, cap, o, "&amp;");
            break;
        case '"':
            if (escapeQuote) xtd_hPut(out, cap, o, "&quot;");
            else xtd_hPut(out, cap, o, "%c", *s);
            break;
        default:
            xtd_hPut(out, cap, o, "%c", *s);
            break;
        }
    }
}

char* XTextDocument_toHtml(const XTextDocument* self)
{
    int i, j;
    size_t cap = 256, o = 0;
    char* out;
    if (!self) return NULL;
    for (i = 0; i < self->m_blockCount; ++i) {
        cap += 96; /* 块级标签 + 对齐属性余量。 */
        for (j = 0; j < self->m_blocks[i].fragmentCount; ++j)
            cap += XStrlen(xtd_fragText(&self->m_blocks[i].fragments[j])) * 6
                   + 160; /* 内联标签 + 转义膨胀余量。 */
    }
    out = (char*)XMalloc_System(cap);
    if (!out) return NULL;
    o = (size_t)XSnprintf(out, cap, "<html><body>");
    for (i = 0; i < self->m_blockCount; ++i) {
        const XTDBlock* blk = &self->m_blocks[i];
        /* 块级开标签(先写名称与属性,末尾统一补 '>'): */
        if (blk->headingLevel >= 1 && blk->headingLevel <= 6)
            xtd_hPut(out, cap, &o, "<h%d", blk->headingLevel);
        else
            xtd_hPut(out, cap, &o, "<p");
        /* 对齐属性（与 xtd_parseAlign 映射互逆；left 为默认不写出）。 */
        if (blk->alignment & (int)XTDAlignment_HCenter)
            xtd_hPut(out, cap, &o, " align=\"center\"");
        else if (blk->alignment & (int)XTDAlignment_Right)
            xtd_hPut(out, cap, &o, " align=\"right\"");
        else if (blk->alignment & (int)XTDAlignment_Justify)
            xtd_hPut(out, cap, &o, " align=\"justify\"");
        xtd_hPut(out, cap, &o, ">");
        if (blk->isListItem) xtd_hPut(out, cap, &o, "<li>");
        for (j = 0; j < blk->fragmentCount; ++j) {
            const XTDFragment* f = &blk->fragments[j];
            const char* href = (f->fmt.anchorHref)
                                   ? XString_toUtf8(f->fmt.anchorHref)
                                   : NULL;
            if (f->image) {
                /* 图片片段：仅按原尺寸导出 <img>（无 src——按名取图的
                 * 资源体系未建，回灌降级为空，见 insertImage 注释）。 */
                xtd_hPut(out, cap, &o, "<img width=\"%d\" height=\"%d\">",
                         XImage_width(f->image), XImage_height(f->image));
                continue;
            }
            if (f->fmt.bgColor != 0u)
                xtd_hPut(out, cap, &o,
                         "<span style=\"background-color:#%06x\">",
                         (unsigned)(f->fmt.bgColor & 0xFFFFFFu));
            if (f->fmt.bold) xtd_hPut(out, cap, &o, "<b>");
            if (f->fmt.italic) xtd_hPut(out, cap, &o, "<i>");
            if (f->fmt.underline) xtd_hPut(out, cap, &o, "<u>");
            if (f->fmt.strikeOut) xtd_hPut(out, cap, &o, "<s>");
            if (f->fmt.superScript) xtd_hPut(out, cap, &o, "<sup>");
            if (f->fmt.subScript) xtd_hPut(out, cap, &o, "<sub>");
            if (f->fmt.fgColor != 0u && f->fmt.fgColor != 0xFF000000u)
                xtd_hPut(out, cap, &o, "<font color=\"#%06x\"",
                         (unsigned)(f->fmt.fgColor & 0xFFFFFFu));
            if (f->fmt.fontPointSize > 0) {
                /* 字号以磅直写（解析侧 1-7 之外按磅承载，互逆）。 */
                if (f->fmt.fgColor != 0u && f->fmt.fgColor != 0xFF000000u)
                    xtd_hPut(out, cap, &o, " size=\"%d\"",
                             f->fmt.fontPointSize);
                else
                    xtd_hPut(out, cap, &o, "<font size=\"%d\">",
                             f->fmt.fontPointSize);
            }
            if (f->fmt.fgColor != 0u && f->fmt.fgColor != 0xFF000000u)
                xtd_hPut(out, cap, &o, ">");
            if (href && href[0]) {
                xtd_hPut(out, cap, &o, "<a href=\"");
                xtd_hEscape(out, cap, &o, href, true);
                xtd_hPut(out, cap, &o, "\">");
            }
            xtd_hEscape(out, cap, &o, xtd_fragText(f), false);
            if (href && href[0]) xtd_hPut(out, cap, &o, "</a>");
            if ((f->fmt.fgColor != 0u && f->fmt.fgColor != 0xFF000000u) ||
                f->fmt.fontPointSize > 0)
                xtd_hPut(out, cap, &o, "</font>");
            if (f->fmt.strikeOut) xtd_hPut(out, cap, &o, "</s>");
            if (f->fmt.subScript) xtd_hPut(out, cap, &o, "</sub>");
            if (f->fmt.superScript) xtd_hPut(out, cap, &o, "</sup>");
            if (f->fmt.underline) xtd_hPut(out, cap, &o, "</u>");
            if (f->fmt.italic) xtd_hPut(out, cap, &o, "</i>");
            if (f->fmt.bold) xtd_hPut(out, cap, &o, "</b>");
            if (f->fmt.bgColor != 0u) xtd_hPut(out, cap, &o, "</span>");
        }
        if (blk->isListItem) xtd_hPut(out, cap, &o, "</li>");
        if (blk->headingLevel >= 1 && blk->headingLevel <= 6)
            xtd_hPut(out, cap, &o, "</h%d>", blk->headingLevel);
        else
            xtd_hPut(out, cap, &o, "</p>");
    }
    xtd_hPut(out, cap, &o, "</body></html>");
    out[o < cap ? o : cap - 1] = '\0';
    return out;
}

/* ==================== 块级格式 ==================== */

void XTextDocument_setBlockAlignment(XTextDocument* self, int blockIndex, int alignment)
{
    if (!self || blockIndex < 0 || blockIndex >= self->m_blockCount) return;
    self->m_blocks[blockIndex].alignment = alignment;
    xtd_changed(self);
}

int XTextDocument_blockAlignment(const XTextDocument* self, int blockIndex)
{
    if (!self || blockIndex < 0 || blockIndex >= self->m_blockCount) return 0;
    return self->m_blocks[blockIndex].alignment;
}

void XTextDocument_setBlockHeadingLevel(XTextDocument* self, int blockIndex, int level)
{
    if (!self || blockIndex < 0 || blockIndex >= self->m_blockCount) return;
    self->m_blocks[blockIndex].headingLevel = level;
    xtd_changed(self);
}

/* ==================== 片段操作 ==================== */

int XTextDocument_addFragment(XTextDocument* self, int blockIndex,
                              const char* text, const XTDCharFormat* fmt)
{
    XTDBlock* blk;
    int fi;
    if (!self || !text || blockIndex < 0 || blockIndex >= self->m_blockCount) return -1;
    blk = &self->m_blocks[blockIndex];
    fi = blk->fragmentCount;
    if (fi >= XTD_MAX_FRAGMENTS_PER_BLOCK) return -1;
    XMemset(&blk->fragments[fi], 0, sizeof(XTDFragment));
    blk->fragments[fi].text = XString_create_utf8(text);
    if (fmt) xtd_formatAssign(&blk->fragments[fi].fmt, fmt);
    blk->fragmentCount = fi + 1;
    xtd_changed(self);
    return fi;
}

int XTextDocument_fragmentCount(const XTextDocument* self, int blockIndex)
{
    if (!self || blockIndex < 0 || blockIndex >= self->m_blockCount) return 0;
    return self->m_blocks[blockIndex].fragmentCount;
}

const XTDFragment* XTextDocument_fragment(const XTextDocument* self, int blockIndex, int fragIndex)
{
    if (!self || blockIndex < 0 || blockIndex >= self->m_blockCount) return NULL;
    if (fragIndex < 0 || fragIndex >= self->m_blocks[blockIndex].fragmentCount) return NULL;
    return &self->m_blocks[blockIndex].fragments[fragIndex];
}

/* ==================== 文本插入 ==================== */

void XTextDocument_insertText(XTextDocument* self, int blockIndex,
                              int fragIndex, int charOffset,
                              const char* text, const XTDCharFormat* fmt)
{
    /* 简化：追加到指定片段末尾。 */
    (void)charOffset;
    XTextDocument_addFragment(self, blockIndex, text, fmt);
}

void XTextDocument_appendBlock(XTextDocument* self, const XTDCharFormat* fmt)
{
    xtd_ensureCapacity(self, self->m_blockCount + 1);
    if (self->m_blockCount < XTD_MAX_BLOCKS) {
        XMemset(&self->m_blocks[self->m_blockCount], 0, sizeof(XTDBlock));
        self->m_blockCount++;
    }
    (void)fmt;
}

void XTextDocument_appendText(XTextDocument* self, const char* text,
                              const XTDCharFormat* fmt)
{
    XTextDocument_addFragment(self, self->m_blockCount - 1, text, fmt);
}

void XTextDocument_appendHtml(XTextDocument* self, const char* html)
{
    /* 对标 QTextDocument::appendHtml：以既有解析器在文档尾部追加，
     * 不清空既有块（末块非空时另起新段）；子集口径与 setHtml 一致。 */
    xtd_parseHtml(self, html, false);
}

/* ==================== 图片片段（最小子集） ==================== */

int XTextDocument_insertImage(XTextDocument* self, const XImage* image)
{
    XTDBlock* blk;
    XTDFragment* frag;
    XRect full;
    int bi;
    int fi;
    if (!self || !self->m_blocks || !image) return -1;
    if (XImage_width(image) <= 0 || XImage_height(image) <= 0) return -1;
    bi = self->m_blockCount - 1;
    if (bi < 0) bi = 0;
    xtd_ensureCapacity(self, bi + 1);
    if (bi >= self->m_capacity) return -1; /* 扩容失败：截断。 */
    if (bi >= self->m_blockCount) self->m_blockCount = bi + 1;
    blk = &self->m_blocks[bi];
    fi = blk->fragmentCount;
    if (fi >= XTD_MAX_FRAGMENTS_PER_BLOCK) return -1;
    frag = &blk->fragments[fi];
    XMemset(frag, 0, sizeof(*frag));
    /* 图片片段：text 恒为空串（toPlainText/编辑器同步不含图片——子集
     * 边界），image 为文档持有的深拷贝（调用方保留原对象所有权）。 */
    frag->text = XString_create_utf8("");
    frag->image = XImage_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    if (!frag->text || !frag->image) {
        xtd_fragClear(frag);
        return -1;
    }
    XRect_init(&full, 0, 0, XImage_width(image), XImage_height(image));
    XImage_copyRect(image, &full, frag->image);
    if (XImage_isNull(frag->image)) {
        xtd_fragClear(frag);
        return -1;
    }
    blk->fragmentCount = fi + 1;
    xtd_changed(self);
    return fi;
}

/* ==================== 元信息 ==================== */

void XTextDocument_setMetaInformation(XTextDocument* self, int info, const char* value)
{
    if (!self || !value) return;
    if (info == 0) {
        if (!self->m_title) self->m_title = XString_create();
        if (self->m_title) XString_assign_utf8(self->m_title, value);
    } else if (info == 1) {
        if (!self->m_url) self->m_url = XString_create();
        if (self->m_url) XString_assign_utf8(self->m_url, value);
    }
}

const char* XTextDocument_metaInformation(const XTextDocument* self, int info)
{
    if (!self) return "";
    {
        const char* text;
        if (info == 0) {
            if (!self->m_title) return "";
            text = XString_toUtf8(self->m_title);
            return text ? text : "";
        }
        if (info == 1) {
            if (!self->m_url) return "";
            text = XString_toUtf8(self->m_url);
            return text ? text : "";
        }
        return "";
    }
}

/* ==================== 撤销/重做 ==================== */

void XTextDocument_setUndoRedoEnabled(XTextDocument* self, bool enable)
{ if (self) self->m_undoRedoEnabled = enable; }
bool XTextDocument_isUndoRedoEnabled(const XTextDocument* self)
{ return self ? self->m_undoRedoEnabled : false; }
bool XTextDocument_isUndoAvailable(const XTextDocument* self)
{
    /* 对标 Qt：undoEnabled 且撤销栈非空（此前误用修改计数）。 */
    return self ? (self->m_undoRedoEnabled && self->m_undoTop > 0) : false;
}
bool XTextDocument_isRedoAvailable(const XTextDocument* self)
{
    /* 对标 Qt：undoEnabled 且重做栈非空（此前恒 false）。 */
    return self ? (self->m_undoRedoEnabled && self->m_redoTop > 0) : false;
}

/* ==================== 默认格式 ==================== */

static XTDCharFormat g_tdDefaultFmt;

void XTextDocument_setDefaultFormat(XTextDocument* self, const XTDCharFormat* fmt)
{
    if (self && fmt) xtd_formatAssign(&g_tdDefaultFmt, fmt);
}
const XTDCharFormat* XTextDocument_defaultFormat(const XTextDocument* self)
{ (void)self; return &g_tdDefaultFmt; }

/* ==================== 信号 ==================== */

void* XTextDocument_contentsChanged_signal(XTextDocument* self)
{ (void)self; return (void*)(size_t)XTextDocument_contentsChanged_signal; }
void* XTextDocument_blockCountChanged_signal(XTextDocument* self, int newCount)
{ (void)self; (void)newCount; return (void*)(size_t)XTextDocument_blockCountChanged_signal; }
void* XTextDocument_modificationChanged_signal(XTextDocument* self, bool modified)
{ (void)self; (void)modified; return (void*)(size_t)XTextDocument_modificationChanged_signal; }
void* XTextDocument_baseUrlChanged_signal(XTextDocument* self)
{ (void)self; return (void*)(size_t)XTextDocument_baseUrlChanged_signal; }
void* XTextDocument_cursorPositionChanged_signal(XTextDocument* self)
{ (void)self; return (void*)(size_t)XTextDocument_cursorPositionChanged_signal; }
void* XTextDocument_documentLayoutChanged_signal(XTextDocument* self)
{ (void)self; return (void*)(size_t)XTextDocument_documentLayoutChanged_signal; }
void* XTextDocument_redoAvailable_signal(XTextDocument* self, bool available)
{ (void)self; (void)available; return (void*)(size_t)XTextDocument_redoAvailable_signal; }
void* XTextDocument_undoAvailable_signal(XTextDocument* self, bool available)
{ (void)self; (void)available; return (void*)(size_t)XTextDocument_undoAvailable_signal; }
void* XTextDocument_undoCommandAdded_signal(XTextDocument* self)
{ (void)self; return (void*)(size_t)XTextDocument_undoCommandAdded_signal; }


/* ==================== 撤销/重做栈 ==================== */

#define XTD_MAX_UNDO 50

static void xtd_saveSnapshot(XTextDocument* self)
{
    if (!self || !self->m_undoRedoEnabled) return;
    {
        char* snap = XTextDocument_toPlainText(self);
        if (!snap) return;
        if (self->m_undoTop < XTD_MAX_UNDO) {
            self->m_undoStack[self->m_undoTop++] = snap;
        } else {
            int i;
            XFree_System(self->m_undoStack[0]);
            for (i = 0; i < XTD_MAX_UNDO - 1; ++i)
                self->m_undoStack[i] = self->m_undoStack[i + 1];
            self->m_undoStack[XTD_MAX_UNDO - 1] = snap;
        }
        self->m_redoTop = 0;
    }
}

static void xtd_restoreSnapshot(XTextDocument* self, const char* text)
{
    XTextDocument_setPlainText(self, text);
}

/* ==================== 块级格式 API ==================== */

void XTextDocument_setBlockIndentLevel(XTextDocument* self, int blockIndex, int level)
{
    if (!self || blockIndex < 0 || blockIndex >= self->m_blockCount) return;
    self->m_blocks[blockIndex].indentLevel = level;
    xtd_changed(self);
}

int XTextDocument_blockIndentLevel(const XTextDocument* self, int blockIndex)
{
    if (!self || blockIndex < 0 || blockIndex >= self->m_blockCount) return 0;
    return self->m_blocks[blockIndex].indentLevel;
}

void XTextDocument_setBlockListItem(XTextDocument* self, int blockIndex, bool isItem, bool ordered)
{
    if (!self || blockIndex < 0 || blockIndex >= self->m_blockCount) return;
    self->m_blocks[blockIndex].isListItem = isItem;
    self->m_blocks[blockIndex].isOrdered = ordered;
    xtd_changed(self);
}

/* ==================== 片段格式 API ==================== */

void XTextDocument_setFragmentBold(XTextDocument* self, int bi, int fi, bool bold)
{
    if (!self || bi < 0 || bi >= self->m_blockCount) return;
    if (fi < 0 || fi >= self->m_blocks[bi].fragmentCount) return;
    self->m_blocks[bi].fragments[fi].fmt.bold = bold;
    xtd_changed(self);
}

void XTextDocument_setFragmentItalic(XTextDocument* self, int bi, int fi, bool italic)
{
    if (!self || bi < 0 || bi >= self->m_blockCount) return;
    if (fi < 0 || fi >= self->m_blocks[bi].fragmentCount) return;
    self->m_blocks[bi].fragments[fi].fmt.italic = italic;
    xtd_changed(self);
}

void XTextDocument_setFragmentUnderline(XTextDocument* self, int bi, int fi, bool underline)
{
    if (!self || bi < 0 || bi >= self->m_blockCount) return;
    if (fi < 0 || fi >= self->m_blocks[bi].fragmentCount) return;
    self->m_blocks[bi].fragments[fi].fmt.underline = underline;
    xtd_changed(self);
}

void XTextDocument_setFragmentStrikeOut(XTextDocument* self, int bi, int fi, bool strikeOut)
{
    if (!self || bi < 0 || bi >= self->m_blockCount) return;
    if (fi < 0 || fi >= self->m_blocks[bi].fragmentCount) return;
    self->m_blocks[bi].fragments[fi].fmt.strikeOut = strikeOut;
    xtd_changed(self);
}

void XTextDocument_setFragmentFgColor(XTextDocument* self, int bi, int fi, uint32_t color)
{
    if (!self || bi < 0 || bi >= self->m_blockCount) return;
    if (fi < 0 || fi >= self->m_blocks[bi].fragmentCount) return;
    self->m_blocks[bi].fragments[fi].fmt.fgColor = color;
    xtd_changed(self);
}

void XTextDocument_setFragmentBgColor(XTextDocument* self, int bi, int fi, uint32_t color)
{
    if (!self || bi < 0 || bi >= self->m_blockCount) return;
    if (fi < 0 || fi >= self->m_blocks[bi].fragmentCount) return;
    self->m_blocks[bi].fragments[fi].fmt.bgColor = color;
    xtd_changed(self);
}

void XTextDocument_setFragmentFontFamily(XTextDocument* self, int bi, int fi, const char* family)
{
    if (!self || bi < 0 || bi >= self->m_blockCount) return;
    if (fi < 0 || fi >= self->m_blocks[bi].fragmentCount) return;
    if (!family) return;
    if (!self->m_blocks[bi].fragments[fi].fmt.fontFamily)
        self->m_blocks[bi].fragments[fi].fmt.fontFamily = XString_create();
    if (self->m_blocks[bi].fragments[fi].fmt.fontFamily)
        XString_assign_utf8(self->m_blocks[bi].fragments[fi].fmt.fontFamily,
                            family);
    xtd_changed(self);
}

void XTextDocument_setFragmentFontSize(XTextDocument* self, int bi, int fi, int size)
{
    if (!self || bi < 0 || bi >= self->m_blockCount) return;
    if (fi < 0 || fi >= self->m_blocks[bi].fragmentCount) return;
    self->m_blocks[bi].fragments[fi].fmt.fontPointSize = size;
    xtd_changed(self);
}

/* ==================== 撤销/重做公共 API ==================== */

void XTextDocument_setCursorPosition(XTextDocument* self, int position)
{
    if (!self) return;
    if (position < 0) position = 0;
    if (self->m_cursorPosition == position) return;
    self->m_cursorPosition = position;
    xtd_emitVoid(self, (size_t)XTextDocument_cursorPositionChanged_signal);
}

int XTextDocument_cursorPosition(const XTextDocument* self)
{ return self ? self->m_cursorPosition : 0; }

int XTextDocument_find(const XTextDocument* self, const char* text)
{
    char* plain;
    const char* hit;
    int result;
    if (!self || !text) return -1;
    plain = XTextDocument_toPlainText(self);
    if (!plain) return -1;
    hit = XStrstr(plain, text);
    if (!hit) {
        XFree_System(plain);
        return -1;
    }
    result = (int)(hit - plain);
    XFree_System(plain);
    return result;
}

char XTextDocument_characterAt(const XTextDocument* self, int position)
{
    char* plain;
    char result;
    if (!self || position < 0) return '\0';
    plain = XTextDocument_toPlainText(self);
    if (!plain) return '\0';
    if (position >= (int)XStrlen(plain)) {
        XFree_System(plain);
        return '\0';
    }
    result = plain[position];
    XFree_System(plain);
    return result;
}

void XTextDocument_undo(XTextDocument* self)
{
    if (!self || !self->m_undoRedoEnabled || self->m_undoTop == 0) return;
    {
        char* snap = self->m_undoStack[--self->m_undoTop];
        if (self->m_redoTop < XTD_MAX_UNDO)
            self->m_redoStack[self->m_redoTop++] =
                XTextDocument_toPlainText(self);
        xtd_restoreSnapshot(self, snap);
        XFree_System(snap);
    }
}

void XTextDocument_redo(XTextDocument* self)
{
    if (!self || !self->m_undoRedoEnabled || self->m_redoTop == 0) return;
    {
        char* snap = self->m_redoStack[--self->m_redoTop];
        if (self->m_undoTop < XTD_MAX_UNDO)
            self->m_undoStack[self->m_undoTop++] =
                XTextDocument_toPlainText(self);
        xtd_restoreSnapshot(self, snap);
        XFree_System(snap);
    }
}

/* ==================== HTML 解析增强 ==================== */

void XTextDocument_setHtmlEnhanced(XTextDocument* self, const char* html)
{
    XTextDocument_setHtml(self, html);
}

/* ==================== HTML 生成增强 ==================== */

char* XTextDocument_toHtmlEnhanced(const XTextDocument* self)
{
    return XTextDocument_toHtml(self);
}

#endif /* XTEXTDOCUMENT_ON */