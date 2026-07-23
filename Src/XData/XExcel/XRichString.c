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

XRichString* XRichString_create(void)
{
    XRichString* self = (XRichString*)XMalloc_System(sizeof(XRichString));
    if (!self) return NULL;
    memset(self, 0, sizeof(XRichString));
    return self;
}

XRichString* XRichString_create_utf8(const char* text)
{
    XRichString* self = XRichString_create();
    if (!self) return NULL;
    if (text)
    {
        self->m_plainText = XString_create();
        if (self->m_plainText)
            XString_append_utf8(self->m_plainText, text);
    }
    return self;
}

void XRichString_copy(XRichString* self, const XRichString* other)
{
    if (!self || !other) return;
    if (other->m_plainText)
    {
        if (!self->m_plainText) self->m_plainText = XString_create();
        if (self->m_plainText)
            XString_append_utf8(self->m_plainText, XString_toUtf8_const(other->m_plainText));
    }
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
                    XRichString_addFragment(self, XString_toUtf8_const(src->m_text), src->m_format);
            }
        }
    }
}

void XRichString_delete(XRichString* self)
{
    if (!self) return;
    if (self->m_plainText) { XString_deinit_base(self->m_plainText); XFree_System(self->m_plainText); }
    if (self->m_fragments)
    {
        int count = (int)XVector_size_base(self->m_fragments);
        for (int i = 0; i < count; i++)
        {
            XRichStringFragment* frag = (XRichStringFragment*)XVector_at_base(self->m_fragments, i);
            if (frag)
            {
                if (frag->m_text) { XString_deinit_base(frag->m_text); XFree_System(frag->m_text); }
                if (frag->m_format) XFormat_delete(frag->m_format);
            }
        }
        XVector_clear_base(self->m_fragments);
        XFree_System(self->m_fragments);
    }
    XFree_System(self);
}

bool XRichString_isRichString(const XRichString* self)
{
    return self && self->m_fragments && XVector_size_base(self->m_fragments) > 1;
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

const char* XRichString_toPlainString(const XRichString* self)
{
    if (!self) return "";
    if (self->m_plainText) return XString_toUtf8_const(self->m_plainText);
    /* 从片段拼接 */
    if (self->m_fragments && XVector_size_base(self->m_fragments) > 0)
    {
        /* 创建临时字符串 */
        XString* tmp = XString_create();
        if (!tmp) return "";
        int count = (int)XVector_size_base(self->m_fragments);
        for (int i = 0; i < count; i++)
        {
            XRichStringFragment* frag = (XRichStringFragment*)XVector_at_base(self->m_fragments, i);
            if (frag && frag->m_text)
                XString_append_utf8(tmp, XString_toUtf8_const(frag->m_text));
        }
        /* 缓存到 plainText */
        ((XRichString*)self)->m_plainText = tmp;
        return XString_toUtf8_const(tmp);
    }
    return "";
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
            const char* text = XString_toUtf8_const(frag->m_text);
            if (frag->m_format && !XFormat_isEmpty(frag->m_format))
            {
                XString_append_utf8(&result, "<span style="");
                if (XFormat_fontBold(frag->m_format)) XString_append_utf8(&result, "font-weight:bold;");
                if (XFormat_fontItalic(frag->m_format)) XString_append_utf8(&result, "font-style:italic;");
                int size = XFormat_fontSize(frag->m_format);
                if (size > 0)
                {
                    char buf[32];
                    snprintf(buf, sizeof(buf), "font-size:%dpt;", size);
                    XString_append_utf8(&result, buf);
                }
                const char* name = XFormat_fontName(frag->m_format);
                if (name && *name)
                {
                    XString_append_utf8(&result, "font-family:");
                    XString_append_utf8(&result, name);
                    XString_append_utf8(&result, ";");
                }
                XString_append_utf8(&result, "">");
                XString_append_utf8(&result, text);
                XString_append_utf8(&result, "</span>");
            }
            else
            {
                XString_append_utf8(&result, text);
            }
        }
    }
    else if (self->m_plainText)
    {
        XString_append_utf8(&result, XString_toUtf8_const(self->m_plainText));
    }
    return result;
}

void XRichString_setHtml(XRichString* self, const char* text)
{
    if (!self) return;
    /* 简单 HTML 解析：将 <span> 标签解析为片段 */
    if (!self->m_plainText) self->m_plainText = XString_create();
    if (self->m_plainText && text) XString_append_utf8(self->m_plainText, text);
}

int XRichString_fragmentCount(const XRichString* self)
{
    if (!self || !self->m_fragments) return 0;
    return (int)XVector_size_base(self->m_fragments);
}

void XRichString_addFragment(XRichString* self, const char* text, const XFormat* format)
{
    if (!self || !text) return;
    if (!self->m_fragments)
        self->m_fragments = XVector_Create(XRichStringFragment);
    if (!self->m_fragments) return;

    XRichStringFragment frag;
    memset(&frag, 0, sizeof(frag));
    frag.m_text = XString_create();
    if (frag.m_text) XString_append_utf8(frag.m_text, text);
    if (format)
    {
        frag.m_format = XFormat_create();
        if (frag.m_format) XFormat_copy(frag.m_format, format);
    }
    XVector_push_back_1_base(self->m_fragments, &frag);
}

const char* XRichString_fragmentText(const XRichString* self, int index)
{
    if (!self || !self->m_fragments) return "";
    XRichStringFragment* frag = (XRichStringFragment*)XVector_at_base(self->m_fragments, index);
    return (frag && frag->m_text) ? XString_toUtf8_const(frag->m_text) : "";
}

const XFormat* XRichString_fragmentFormat(const XRichString* self, int index)
{
    if (!self || !self->m_fragments) return NULL;
    XRichStringFragment* frag = (XRichStringFragment*)XVector_at_base(self->m_fragments, index);
    return frag ? frag->m_format : NULL;
}
