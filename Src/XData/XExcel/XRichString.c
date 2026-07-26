/******************************************************************************
 * @file       XRichString.c
 * @brief      XRichString 富文本字符串类实现
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XRichString.h"
#include "XMemory.h"
#include "XString.h"
#include "XVector.h"
#include <stdlib.h>

#include <string.h>

#include <stdio.h>

static void clear_fragments(XRichString* self)
{
    if (!self || !self->m_fragments) return;
    while (XVector_size_base(self->m_fragments) > 0) {
        XRichStringFragment* frag =
            (XRichStringFragment*)XVector_back_base(self->m_fragments);
        if (frag) {
            if (frag->m_text) XString_delete_base(frag->m_text);
            if (frag->m_format) XFormat_delete(frag->m_format);
        }
        XVector_pop_back_base(self->m_fragments);
    }
}

static void clear_plain_text(XRichString* self)
{
    if (!self || !self->m_plainText) return;
    XString_delete_base(self->m_plainText);
    self->m_plainText = NULL;
}

static void append_html_escaped(XString* output, const XString* text)
{
    const char* utf8 = text ? XString_toUtf8(text) : NULL;
    if (!output || !utf8) return;
    for (const char* p = utf8; *p; ++p) {
        switch (*p) {
            case '&': XString_append_utf8(output, "&amp;"); break;
            case '<': XString_append_utf8(output, "&lt;"); break;
            case '>': XString_append_utf8(output, "&gt;"); break;
            case '\'': XString_append_utf8(output, "&#39;"); break;
            case '"': XString_append_utf8(output, "&quot;"); break;
            default: {
                char value[2] = {*p, '\0'};
                XString_append_utf8(output, value);
                break;
            }
        }
    }
}

static XString* decode_html_text(const char* text, size_t length)
{
    XString* result = XString_create();
    if (!result) return NULL;
    size_t start = 0;
    for (size_t i = 0; i < length;) {
        if (text[i] != '&') {
            ++i;
            continue;
        }
        if (i > start) XString_append_with_length_utf8(result, text + start, i - start);
        const char* replacement = NULL;
        size_t consumed = 1;
        if (i + 5 <= length && memcmp(text + i, "&amp;", 5) == 0) {
            replacement = "&"; consumed = 5;
        } else if (i + 4 <= length && memcmp(text + i, "&lt;", 4) == 0) {
            replacement = "<"; consumed = 4;
        } else if (i + 4 <= length && memcmp(text + i, "&gt;", 4) == 0) {
            replacement = ">"; consumed = 4;
        } else if (i + 6 <= length && memcmp(text + i, "&quot;", 6) == 0) {
            replacement = "\""; consumed = 6;
        } else if (i + 6 <= length && memcmp(text + i, "&apos;", 6) == 0) {
            replacement = "'"; consumed = 6;
        } else if (i + 5 <= length && memcmp(text + i, "&#39;", 5) == 0) {
            replacement = "'"; consumed = 5;
        }
        if (replacement) XString_append_utf8(result, replacement);
        else XString_append_utf8(result, "&");
        i += consumed;
        start = i;
    }
    if (start < length) XString_append_with_length_utf8(result, text + start, length - start);
    return result;
}

static void add_html_fragment(XRichString* self, const char* text, size_t length,
                              const XFormat* format)
{
    if (!self || !text || length == 0) return;
    XString* decoded = decode_html_text(text, length);
    if (!decoded) return;
    if (!XString_isEmpty_base(decoded)) XRichString_addFragment(self, decoded, format);
    XString_delete_base(decoded);
}


XRichString* XRichString_create(void)
{
    XRichString* self = (XRichString*)XMalloc_System(sizeof(XRichString));
    if (!self) return NULL;
    memset(self, 0, sizeof(XRichString));
    return self;
}

XRichString* XRichString_create_utf8(const XString* text)
{
    XRichString* self = XRichString_create();
    if (!self) return NULL;
    if (text) XRichString_setText(self, text);
    return self;
}

void XRichString_copy(XRichString* self, const XRichString* other)
{
    if (!self || !other || self == other) return;
    clear_plain_text(self);
    clear_fragments(self);
    if (other->m_plainText) self->m_plainText = XString_create_copy(other->m_plainText);
    if (other->m_fragments)
    {
        if (!self->m_fragments)
            self->m_fragments = XVector_Create(XRichStringFragment);
        if (self->m_fragments)
        {
            int count = (int)XVector_size_base(other->m_fragments);
            for (int i = 0; i < count; i++)
            {
                XRichStringFragment* src = (XRichStringFragment*)XVector_at_base(other->m_fragments, i);
                if (src)
                    XRichString_addFragment(self, src->m_text, src->m_format);
            }
        }
    }
}

void XRichString_delete(XRichString* self)
{
    if (!self) return;
    clear_plain_text(self);
    if (self->m_fragments)
    {
        clear_fragments(self);
        XVector_delete_base(self->m_fragments);
    }
    XFree_System(self);
}

bool XRichString_isRichString(const XRichString* self)
{
    if (!self || !self->m_fragments) return false;
    size_t count = XVector_size_base(self->m_fragments);
    if (count > 1) return true;
    if (count == 1) {
        XRichStringFragment* frag =
            (XRichStringFragment*)XVector_at_base(self->m_fragments, 0);
        return frag && frag->m_format && !XFormat_isEmpty(frag->m_format);
    }
    return false;
}

bool XRichString_isNull(const XRichString* self)
{
    return !self || (!self->m_plainText && !self->m_fragments);
}

bool XRichString_isEmpty(const XRichString* self)
{
    if (!self) return true;
    if (self->m_plainText && XString_size_base(self->m_plainText) > 0) return false;
    if (self->m_fragments && XVector_size_base(self->m_fragments) > 0) return false;
    return true;
}

const XString* XRichString_toPlainString(const XRichString* self)
{
    if (!self) return NULL;
    if (self->m_plainText) return self->m_plainText;
    /* 从片段拼接 */
    if (self->m_fragments && XVector_size_base(self->m_fragments) > 0)
    {
        /* 创建临时字符串 */
        XString* tmp = XString_create();
        if (!tmp) return NULL;
        int count = (int)XVector_size_base(self->m_fragments);
        for (int i = 0; i < count; i++)
        {
            XRichStringFragment* frag = (XRichStringFragment*)XVector_at_base(self->m_fragments, i);
            if (frag && frag->m_text)
                XString_append(tmp, frag->m_text);
        }
        /* 缓存到 plainText */
        ((XRichString*)self)->m_plainText = tmp;
        return tmp;
    }
    return NULL;
}

XString XRichString_toHtml(const XRichString* self)
{
    XString result;
    XString_init(&result);
    if (!self) return result;
    if (self->m_fragments && XVector_size_base(self->m_fragments) > 0)
    {
        int count = (int)XVector_size_base(self->m_fragments);
        for (int i = 0; i < count; i++)
        {
            XRichStringFragment* frag = (XRichStringFragment*)XVector_at_base(self->m_fragments, i);
            if (!frag || !frag->m_text) continue;
            if (frag->m_format && !XFormat_isEmpty(frag->m_format))
            {
                XString_append_utf8(&result, "<span style='");
                if (XFormat_fontBold(frag->m_format)) XString_append_utf8(&result, "font-weight:bold;");
                if (XFormat_fontItalic(frag->m_format)) XString_append_utf8(&result, "font-style:italic;");
                int size = XFormat_fontSize(frag->m_format);
                if (size > 0)
                {
                    char buf[32];
                    snprintf(buf, sizeof(buf), "font-size:%dpt;", size);
                    XString_append_utf8(&result, buf);
                }
                const XString* name = XFormat_fontName(frag->m_format);
                if (name && XString_size_base(name) > 0)
                {
                    XString_append_utf8(&result, "font-family:");
                    append_html_escaped(&result, name);
                    XString_append_utf8(&result, ";");
                }
                XString_append_utf8(&result, "'>");
                append_html_escaped(&result, frag->m_text);
                XString_append_utf8(&result, "</span>");
            }
            else
            {
                append_html_escaped(&result, frag->m_text);
            }
        }
    }
    else if (self->m_plainText)
    {
        append_html_escaped(&result, self->m_plainText);
    }
    return result;
}

void XRichString_setHtml(XRichString* self, const XString* text)
{
    if (!self) return;
    clear_plain_text(self);
    clear_fragments(self);
    const char* html = text ? XString_toUtf8(text) : NULL;
    if (!html) return;

    const char* cursor = html;
    while (*cursor) {
        const char* tag = strchr(cursor, '<');
        if (!tag) {
            add_html_fragment(self, cursor, strlen(cursor), NULL);
            break;
        }
        if (tag > cursor) add_html_fragment(self, cursor, (size_t)(tag - cursor), NULL);
        const char* tagEnd = strchr(tag, '>');
        if (!tagEnd) {
            add_html_fragment(self, tag, strlen(tag), NULL);
            break;
        }

        XString* tagString = XString_create_with_length_utf8(tag, (size_t)(tagEnd - tag + 1));
        bool span = tagString &&
            (XString_startsWith_utf8(tagString, "<span", XChar_CaseSensitive) ||
             XString_startsWith_utf8(tagString, "<SPAN", XChar_CaseSensitive));
        bool bold = tagString &&
            (XString_startsWith_utf8(tagString, "<b>", XChar_CaseSensitive) ||
             XString_startsWith_utf8(tagString, "<strong>", XChar_CaseSensitive));
        bool italic = tagString &&
            (XString_startsWith_utf8(tagString, "<i>", XChar_CaseSensitive) ||
             XString_startsWith_utf8(tagString, "<em>", XChar_CaseSensitive));
        if (tagString) XString_delete_base(tagString);
        if (span || bold || italic) {
            const char* closeTag = span ? "</span>" : (bold ? "</b>" : "</i>");
            const char* close = strstr(tagEnd + 1, closeTag);
            if (!close && bold) close = strstr(tagEnd + 1, "</strong>");
            if (!close && italic) close = strstr(tagEnd + 1, "</em>");
            if (close) {
                XFormat* format = XFormat_create();
                if (format) {
                    if (bold) XFormat_setFontBold(format, true);
                    if (italic) XFormat_setFontItalic(format, true);
                    if (span) {
                        size_t tagLength = (size_t)(tagEnd - tag + 1);
                        char* style = (char*)XMalloc_System(tagLength + 1);
                        if (style) {
                            memcpy(style, tag, tagLength);
                            style[tagLength] = '\0';
                            if (strstr(style, "font-weight:bold")) XFormat_setFontBold(format, true);
                            if (strstr(style, "font-style:italic")) XFormat_setFontItalic(format, true);
                            const char* size = strstr(style, "font-size:");
                            if (size) XFormat_setFontSize(format, atoi(size + 10));
                            const char* family = strstr(style, "font-family:");
                            if (family) {
                                family += 12;
                                const char* singleQuote = strchr(family, '\'');
                                const char* doubleQuote = strchr(family, '\"');
                                const char* end = singleQuote;
                                if (!end || (doubleQuote && doubleQuote < end)) end = doubleQuote;
                                if (!end) end = family + strlen(family);
                                if (end > family && end[-1] == ';') --end;
                                if (end && end > family) {
                                    XString* name = XString_create_with_length_utf8(
                                        family, (size_t)(end - family));
                                    if (name) {
                                        XString* decodedName = decode_html_text(
                                            XString_toUtf8(name), strlen(XString_toUtf8(name)));
                                        if (decodedName) {
                                            XFormat_setFontName(format, decodedName);
                                            XString_delete_base(decodedName);
                                        }
                                        XString_delete_base(name);
                                    }
                                }
                            }
                            XFree_System(style);
                        }
                    }
                    add_html_fragment(self, tagEnd + 1,
                        (size_t)(close - tagEnd - 1), format);
                    XFormat_delete(format);
                }
                cursor = strchr(close, '>');
                cursor = cursor ? cursor + 1 : close + strlen(closeTag);
                continue;
            }
        } else {
            XString* brString = XString_create_with_length_utf8(tag, (size_t)(tagEnd - tag + 1));
            bool br = brString &&
                (XString_startsWith_utf8(brString, "<br", XChar_CaseSensitive) ||
                 XString_startsWith_utf8(brString, "<BR", XChar_CaseSensitive));
            if (brString) XString_delete_base(brString);
            if (br) {
                XString_Init_Utf8(newline, "\n");
                XRichString_addFragment(self, newline, NULL);
                XString_deinit_base(newline);
                cursor = tagEnd + 1;
                continue;
            }
        }
        cursor = tagEnd + 1;
    }
    (void)XRichString_toPlainString(self);
}

int XRichString_fragmentCount(const XRichString* self)
{
    if (!self || !self->m_fragments) return 0;
    return (int)XVector_size_base(self->m_fragments);
}

void XRichString_addFragment(XRichString* self, const XString* text, const XFormat* format)
{
    if (!self || !text) return;
    if (!self->m_fragments)
        self->m_fragments = XVector_Create(XRichStringFragment);
    if (!self->m_fragments) return;

    XRichStringFragment frag;
    memset(&frag, 0, sizeof(frag));
    frag.m_text = XString_create_copy(text);
    if (!frag.m_text) return;
    if (format)
    {
        frag.m_format = XFormat_create();
        if (!frag.m_format) {
            XString_delete_base(frag.m_text);
            return;
        }
        XFormat_copy(frag.m_format, format);
    }
    if (!XVector_push_back_1_base(self->m_fragments, &frag)) {
        XString_delete_base(frag.m_text);
        if (frag.m_format) XFormat_delete(frag.m_format);
        return;
    }
    clear_plain_text(self);
}

const XString* XRichString_fragmentText(const XRichString* self, int index)
{
    if (!self || !self->m_fragments || index < 0 ||
        (size_t)index >= XVector_size_base(self->m_fragments)) return NULL;
    XRichStringFragment* frag = (XRichStringFragment*)XVector_at_base(self->m_fragments, index);
    return (frag && frag->m_text) ? frag->m_text : NULL;
}

const XFormat* XRichString_fragmentFormat(const XRichString* self, int index)
{
    if (!self || !self->m_fragments || index < 0 ||
        (size_t)index >= XVector_size_base(self->m_fragments)) return NULL;
    XRichStringFragment* frag = (XRichStringFragment*)XVector_at_base(self->m_fragments, index);
    return frag ? frag->m_format : NULL;
}

/* ========== 便捷方法实现 ========== */

void XRichString_setText(XRichString* self, const XString* text)
{
    if (!self) return;
    XString* copy = text ? XString_create_copy(text) : XString_create();
    if (!copy) return;
    /* 1) 更新 m_plainText */
    clear_plain_text(self);
    self->m_plainText = copy;
    /* 2) 同时清空并重写第一个 fragment，对齐 QXlsx::RichString::setText */
    if (!self->m_fragments)
        self->m_fragments = XVector_Create(XRichStringFragment);
    if (self->m_fragments)
    {
        /* 清空现有 fragments（释放其中的 XString 和 XFormat） */
        clear_fragments(self);
        /* 压入一个空格式的新片段 */
        XRichStringFragment frag;
        memset(&frag, 0, sizeof(frag));
        frag.m_text = XString_create_copy(self->m_plainText);
        /* 不复制格式引用，setText 不带格式 */
        if (frag.m_text && !XVector_push_back_1_base(self->m_fragments, &frag))
            XString_delete_base(frag.m_text);
    }
}

const XString* XRichString_text(const XRichString* self)
{
    if (!self) return NULL;
    if (self->m_plainText) return self->m_plainText;
    return XRichString_toPlainString(self);
}

/* ========== UTF-8 便捷变体 ========== */

void XRichString_setText_utf8(XRichString* self, const char* text)
{
    XString* s = text ? XString_create_utf8(text) : NULL;
    XRichString_setText(self, s);
    if (s) XString_delete_base(s);
}

void XRichString_addFragment_utf8(XRichString* self, const char* text, const XFormat* format)
{
    XString* s = text ? XString_create_utf8(text) : NULL;
    XRichString_addFragment(self, s, format);
    if (s) XString_delete_base(s);
}
