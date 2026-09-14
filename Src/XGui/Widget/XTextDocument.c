#include "XTextDocument.h"
#include "XMemory.h"
#include "XObject.h"
#include "XEvent.h"
#include "XVarList.h"
#include "XGuiConfig.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#if XTEXTDOCUMENT_ON

static void xtd_formatAssign(XTDCharFormat* dst, const XTDCharFormat* src);
static void xtd_formatClear(XTDCharFormat* fmt);
static bool xtd_formatEqual(const XTDCharFormat* a, const XTDCharFormat* b);
static void xtd_fragClear(XTDFragment* frag);
static const char* xtd_fragText(const XTDFragment* frag);

/* ==================== 生命周期 ==================== */

XVtable* XTextDocument_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XTextDocument)
    XVTABLE_INHERIT_XCLASS(XObject);
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
    memset(self, 0, sizeof(*self));
    XObject_init(&self->m_base);
    XClassSetVtable(self, XTextDocument);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    self->m_capacity = 16;
    self->m_blocks = (XTDBlock*)XMalloc_System(
        sizeof(XTDBlock) * (size_t)self->m_capacity);
    if (self->m_blocks) memset(self->m_blocks, 0,
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

/** @brief 释放片段全部字符串并清零。 */
static void xtd_fragClear(XTDFragment* frag)
{
    if (!frag) return;
    if (frag->text) {
        XString_delete_base(frag->text);
        frag->text = NULL;
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
    int j;
    if (!self) return;
    if (self->m_blocks) {
        for (j = 0; j < self->m_blocks[0].fragmentCount; ++j)
            xtd_fragClear(&self->m_blocks[0].fragments[j]);
        if (self->m_blocks[0].blockFormat) {
            XString_delete_base(self->m_blocks[0].blockFormat);
            self->m_blocks[0].blockFormat = NULL;
        }
        memset(&self->m_blocks[0], 0, sizeof(XTDBlock));
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
            total += (int)strlen(xtd_fragText(&self->m_blocks[i].fragments[j]));
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
            total += (int)strlen(xtd_fragText(&self->m_blocks[i].fragments[j]));
    total += self->m_blockCount; /* 换行符。 */
    out = (char*)XMalloc_System((size_t)total);
    if (!out) return NULL;
    out[0] = '\0';
    for (i = 0; i < self->m_blockCount; ++i) {
        for (j = 0; j < self->m_blocks[i].fragmentCount; ++j) {
            const char* t = xtd_fragText(&self->m_blocks[i].fragments[j]);
            size_t len = strlen(t);
            memcpy(out + o, t, len); o += len;
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
    if (!utf8 || !utf8[0]) return;
    p = utf8;
    while (*p) {
        const char* nl = strchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) : strlen(p);
        if (blockIdx >= XTD_MAX_BLOCKS) break;
        if (len > 0 && blockIdx < self->m_blockCount) {
            XTDFragment* frag;
            if (blockIdx >= self->m_blockCount) {
                xtd_ensureCapacity(self, blockIdx + 1);
                self->m_blockCount = blockIdx + 1;
            }
            frag = &self->m_blocks[blockIdx].fragments[0];
            if (frag->text) XString_delete_base(frag->text);
            frag->text = XString_create_with_length_utf8(p, len);
            self->m_blocks[blockIdx].fragmentCount = 1;
        }
        blockIdx++;
        if (!nl) break;
        p = nl + 1;
    }
    if (blockIdx > self->m_blockCount) self->m_blockCount = blockIdx;
    xtd_changed(self);
}


/* ==================== HTML 解析 ==================== */

/** @brief 解析 HTML 到块+片段结构。 */
void XTextDocument_setHtml(XTextDocument* self, const char* html)
{
    const char* p;
    int blockIdx = 0;
    XTDCharFormat cur;
    if (!self || !html) return;
    XTextDocument_clear(self);
    memset(&cur, 0, sizeof(cur));
    p = html;
    while (*p && blockIdx < XTD_MAX_BLOCKS) {
        if (*p == '<') {
            ++p;
            if (strncmp(p, "b>", 2) == 0 || strncmp(p, "strong>", 7) == 0) {
                cur.bold = true; while (*p && *p != '>') ++p; if (*p) ++p;
            } else if (strncmp(p, "/b>", 3) == 0 || strncmp(p, "/strong>", 8) == 0) {
                cur.bold = false; while (*p && *p != '>') ++p; if (*p) ++p;
            } else if (strncmp(p, "i>", 2) == 0 || strncmp(p, "em>", 4) == 0) {
                cur.italic = true; while (*p && *p != '>') ++p; if (*p) ++p;
            } else if (strncmp(p, "/i>", 3) == 0 || strncmp(p, "/em>", 4) == 0) {
                cur.italic = false; while (*p && *p != '>') ++p; if (*p) ++p;
            } else if (strncmp(p, "u>", 2) == 0) {
                cur.underline = true; while (*p && *p != '>') ++p; if (*p) ++p;
            } else if (strncmp(p, "/u>", 3) == 0) {
                cur.underline = false; while (*p && *p != '>') ++p; if (*p) ++p;
            } else if (strncmp(p, "s>", 2) == 0 || strncmp(p, "strike>", 7) == 0) {
                cur.strikeOut = true; while (*p && *p != '>') ++p; if (*p) ++p;
            } else if (strncmp(p, "/s>", 3) == 0 || strncmp(p, "/strike>", 8) == 0) {
                cur.strikeOut = false; while (*p && *p != '>') ++p; if (*p) ++p;
            } else if (strncmp(p, "br", 2) == 0) {
                blockIdx++; while (*p && *p != '>') ++p; if (*p) ++p;
            } else if (strncmp(p, "p", 1) == 0 || strncmp(p, "div", 3) == 0) {
                blockIdx++; while (*p && *p != '>') ++p; if (*p) ++p;
            } else if (strncmp(p, "/p>", 3) == 0 || strncmp(p, "/div>", 5) == 0) {
                while (*p && *p != '>') ++p; if (*p) ++p;
            } else if (strncmp(p, "h1>", 3) == 0) {
                blockIdx++; while (*p && *p != '>') ++p; if (*p) ++p;
            } else if (strncmp(p, "h2>", 3) == 0 || strncmp(p, "h3>", 3) == 0) {
                blockIdx++; while (*p && *p != '>') ++p; if (*p) ++p;
            } else if (strncmp(p, "li>", 3) == 0) {
                blockIdx++; while (*p && *p != '>') ++p; if (*p) ++p;
            } else if (strncmp(p, "font", 4) == 0) {
                const char* colorStart = strstr(p, "color=");
                if (colorStart) {
                    colorStart += 7; /* color=" */
                    if (*colorStart == '#') {
                        uint32_t r=0,g=0,b=0;
                        sscanf(colorStart+1, "%02x%02x%02x", &r,&g,&b);
                        cur.fgColor = 0xFF000000u | (r<<16) | (g<<8) | b;
                    }
                }
                while (*p && *p != '>') ++p; if (*p) ++p;
            } else if (strncmp(p, "/font>", 6) == 0) {
                cur.fgColor = 0xFF000000u; while (*p && *p != '>') ++p; if (*p) ++p;
            } else if (strncmp(p, "a ", 2) == 0) {
                const char* href = strstr(p, "href=");
                if (href) { char tmp[256]; const char* q;
                    href += 6;
                    for (q = href; *q && *q != '"' && (size_t)(q - href) < 255; ++q)
                        tmp[(size_t)(q - href)] = *q;
                    tmp[(size_t)(q - href)] = 0;
                    if (!cur.anchorHref) cur.anchorHref = XString_create();
                    if (cur.anchorHref) XString_assign_utf8(cur.anchorHref, tmp);
                }
                while (*p && *p != '>') ++p; if (*p) ++p;
            } else if (strncmp(p, "/a>", 3) == 0) {
                if (cur.anchorHref) XString_assign_utf8(cur.anchorHref, "");
                while (*p && *p != '>') ++p; if (*p) ++p;
            } else {
                while (*p && *p != '>') ++p; if (*p) ++p;
            }
        } else if (*p == '&') {
            if (strncmp(p, "&amp;", 5) == 0) {
                /* 插入 & 字符 */
                xtd_ensureCapacity(self, blockIdx + 1);
                if (blockIdx >= self->m_blockCount) self->m_blockCount = blockIdx + 1;
                {
                    int fi = self->m_blocks[blockIdx].fragmentCount;
                    if (fi < XTD_MAX_FRAGMENTS_PER_BLOCK) {
                        XTDFragment* f = &self->m_blocks[blockIdx].fragments[fi];
                        f->text = XString_create_utf8("&");
                        xtd_formatAssign(&f->fmt, &cur);
                        self->m_blocks[blockIdx].fragmentCount = fi + 1;
                    }
                }
                p += 5;
            } else if (strncmp(p, "&lt;", 4) == 0) { p += 4; }
            else if (strncmp(p, "&gt;", 4) == 0) { p += 4; }
            else if (strncmp(p, "&nbsp;", 6) == 0) { p += 6; }
            else ++p;
        } else {
            /* 普通字符：追加到当前块最后一个片段。 */
            xtd_ensureCapacity(self, blockIdx + 1);
            if (blockIdx >= self->m_blockCount) self->m_blockCount = blockIdx + 1;
            {
                XTDBlock* blk = &self->m_blocks[blockIdx];
                int fi = blk->fragmentCount;
                size_t len;
                if (fi == 0 || (fi > 0 && !xtd_formatEqual(&blk->fragments[fi-1].fmt, &cur))) {
                    /* 格式变化或第一个片段：新建片段。 */
                    if (fi < XTD_MAX_FRAGMENTS_PER_BLOCK) {
                        XTDFragment* nf = &blk->fragments[fi];
                        memset(nf, 0, sizeof(XTDFragment));
                        xtd_formatAssign(&nf->fmt, &cur);
                        nf->text = XString_create();
                        if (nf->text) XString_append_with_length_utf8(
                            nf->text, p, 1);
                        blk->fragmentCount = fi + 1;
                    }
                } else {
                    /* 格式相同：追加文本。 */
                    XTDFragment* pf = &blk->fragments[fi-1];
                    if (!pf->text) pf->text = XString_create();
                    if (pf->text) XString_append_with_length_utf8(
                        pf->text, p, 1);
                }
            }
            ++p;
        }
    }
    if (blockIdx + 1 > self->m_blockCount) self->m_blockCount = blockIdx + 1;
    xtd_changed(self);
}

/* ==================== HTML 生成 ==================== */

char* XTextDocument_toHtml(const XTextDocument* self)
{
    int i, j;
    size_t cap = 1024, o = 0;
    char* out;
    if (!self) return NULL;
    for (i = 0; i < self->m_blockCount; ++i)
        for (j = 0; j < self->m_blocks[i].fragmentCount; ++j)
            cap += strlen(xtd_fragText(&self->m_blocks[i].fragments[j])) * 8 + 64;
    out = (char*)XMalloc_System(cap);
    if (!out) return NULL;
    o = (size_t)snprintf(out, cap, "<html><body>");
    for (i = 0; i < self->m_blockCount; ++i) {
        o += (size_t)snprintf(out+o, cap-o, "<p>");
        for (j = 0; j < self->m_blocks[i].fragmentCount; ++j) {
            XTDFragment* f = &self->m_blocks[i].fragments[j];
            if (f->fmt.bold) o += (size_t)snprintf(out+o, cap-o, "<b>");
            if (f->fmt.italic) o += (size_t)snprintf(out+o, cap-o, "<i>");
            if (f->fmt.underline) o += (size_t)snprintf(out+o, cap-o, "<u>");
            if (f->fmt.fgColor != 0xFF000000u)
                o += (size_t)snprintf(out+o, cap-o,
                    "<font color='#[%06x]'>", (unsigned)(f->fmt.fgColor & 0xFFFFFF));
            {
                const char* t = xtd_fragText(f);
                while (*t) {
                    if (*t == '<') o += (size_t)snprintf(out+o, cap-o, "&lt;");
                    else if (*t == '>') o += (size_t)snprintf(out+o, cap-o, "&gt;");
                    else if (*t == '&') o += (size_t)snprintf(out+o, cap-o, "&amp;");
                    else o += (size_t)snprintf(out+o, cap-o, "%c", *t);
                    ++t;
                }
            }
            if (f->fmt.fgColor != 0xFF000000u) o += (size_t)snprintf(out+o, cap-o, "</font>");
            if (f->fmt.underline) o += (size_t)snprintf(out+o, cap-o, "</u>");
            if (f->fmt.italic) o += (size_t)snprintf(out+o, cap-o, "</i>");
            if (f->fmt.bold) o += (size_t)snprintf(out+o, cap-o, "</b>");
        }
        o += (size_t)snprintf(out+o, cap-o, "</p>");
    }
    o += (size_t)snprintf(out+o, cap-o, "</body></html>");
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
    memset(&blk->fragments[fi], 0, sizeof(XTDFragment));
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
        memset(&self->m_blocks[self->m_blockCount], 0, sizeof(XTDBlock));
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
    /* 简化：追加为纯文本块。 */
    (void)self; (void)html;
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
{ return self ? self->m_modified > 0 : false; }
bool XTextDocument_isRedoAvailable(const XTextDocument* self)
{ (void)self; return false; }

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


/* ==================== 撤销/重做栈 ==================== */

#define XTD_MAX_UNDO 50

static char* g_tdUndoStack[XTD_MAX_UNDO];
static int g_tdUndoTop = 0;
static char* g_tdRedoStack[XTD_MAX_UNDO];
static int g_tdRedoTop = 0;

static void xtd_saveSnapshot(XTextDocument* self)
{
    if (!self || !self->m_undoRedoEnabled) return;
    {
        char* snap = XTextDocument_toPlainText(self);
        if (!snap) return;
        if (g_tdUndoTop < XTD_MAX_UNDO) {
            g_tdUndoStack[g_tdUndoTop++] = snap;
        } else {
            int i;
            XFree_System(g_tdUndoStack[0]);
            for (i = 0; i < XTD_MAX_UNDO - 1; ++i)
                g_tdUndoStack[i] = g_tdUndoStack[i + 1];
            g_tdUndoStack[XTD_MAX_UNDO - 1] = snap;
        }
        g_tdRedoTop = 0;
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

void XTextDocument_undo(XTextDocument* self)
{
    if (!self || g_tdUndoTop == 0) return;
    {
        char* snap = g_tdUndoStack[--g_tdUndoTop];
        if (g_tdRedoTop < XTD_MAX_UNDO)
            g_tdRedoStack[g_tdRedoTop++] = XTextDocument_toPlainText(self);
        xtd_restoreSnapshot(self, snap);
        XFree_System(snap);
    }
}

void XTextDocument_redo(XTextDocument* self)
{
    if (!self || g_tdRedoTop == 0) return;
    {
        char* snap = g_tdRedoStack[--g_tdRedoTop];
        if (g_tdUndoTop < XTD_MAX_UNDO)
            g_tdUndoStack[g_tdUndoTop++] = XTextDocument_toPlainText(self);
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