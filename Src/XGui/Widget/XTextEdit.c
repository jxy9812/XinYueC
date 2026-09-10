/**
 * @file       XTextEdit.c
 * @brief      富文本编辑控件实现（对标 Qt 6.8 QTextEdit 核心公共 API）。
 * @details    与同名头文件的公共 API 一一对应；内部实现细节见
 *             头文件 @note 与函数级 Doxygen 注释。
 * @author     XinYueC 团队
 */

#include "XTextEdit.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XGuiConfig.h"
#include "XWidget_Protected.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#if XWIDGET_ON && XABSTRACTSCROLLAREA_ON && XPLAINTEXTEDIT_ON && XTEXTEDIT_ON

/* ==================== 生命周期与虚表 ==================== */

XVtable* XTextEdit_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XTextEdit)
    XVTABLE_INHERIT_XCLASS(XAbstractScrollArea);
    return XVTABLE_DEFAULT;
}

void XTextEdit_init(XTextEdit* self, XWidget* parent,
                           XWidgetFlags flags)
{
    if (!self) return;
    memset(self, 0, sizeof(*self));
    XAbstractScrollArea_init(&self->m_base, parent, flags);
    self->m_editor = XPlainTextEdit_create_ex(
        XCLASS_DEFAULT_MEMORY_TYPE, (XWidget*)self, 0);
    XClassSetVtable(self, XTextEdit);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    self->m_textColor = 0xFF000000u;
    self->m_alignment = 1; /* AlignLeft */
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

void XTextEdit_setHtml(XTextEdit* self, const char* html)
{
    const char* p;
    char plain[4096];
    size_t o = 0;
    bool bold = false, italic = false, underline = false;
    if (!self || !html) return;
    p = html;
    while (*p && o < sizeof(plain) - 1) {
        if (*p == '<') {
            ++p;
            if (strncmp(p, "b>", 2) == 0) { bold = true; xte_skipTag(&p); }
            else if (strncmp(p, "/b>", 3) == 0) { bold = false; xte_skipTag(&p); }
            else if (strncmp(p, "i>", 2) == 0) { italic = true; xte_skipTag(&p); }
            else if (strncmp(p, "/i>", 3) == 0) { italic = false; xte_skipTag(&p); }
            else if (strncmp(p, "u>", 2) == 0) { underline = true; xte_skipTag(&p); }
            else if (strncmp(p, "/u>", 3) == 0) { underline = false; xte_skipTag(&p); }
            else if (strncmp(p, "br", 2) == 0 || strncmp(p, "p", 1) == 0) { plain[o++] = '\n'; xte_skipTag(&p); }
            else xte_skipTag(&p);
        } else {
            plain[o++] = *p++;
        }
    }
    plain[o] = '\0';
    self->m_bold = bold;
    self->m_italic = italic;
    self->m_underline = underline;
    XPlainTextEdit_setPlainText(self->m_editor, plain);
}

char* XTextEdit_toHtml(const XTextEdit* self)
{
    char* plain;
    size_t cap;
    char* html;
    size_t o = 0;
    int i;
    if (!self) return NULL;
    plain = XPlainTextEdit_toPlainText(self->m_editor);
    if (!plain) return NULL;
    cap = strlen(plain) * 8 + 128;
    html = (char*)XMalloc_System(cap);
    if (!html) { XFree_System(plain); return NULL; }
    o = (size_t)snprintf(html, cap, "<html><body>");
    for (i = 0; plain[i]; ++i) {
        if (plain[i] == '\n') o += (size_t)snprintf(html + o, cap - o, "<br>");
        else if (plain[i] == '<') o += (size_t)snprintf(html + o, cap - o, "&lt;");
        else if (plain[i] == '>') o += (size_t)snprintf(html + o, cap - o, "&gt;");
        else if (plain[i] == '&') o += (size_t)snprintf(html + o, cap - o, "&amp;");
        else o += (size_t)snprintf(html + o, cap - o, "%c", plain[i]);
    }
    o += (size_t)snprintf(html + o, cap - o, "</body></html>");
    XFree_System(plain);
    return html;
}

/* ==================== 信号 ==================== */

void* XTextEdit_textChanged_signal(XTextEdit* self)
{
    (void)self;
    return (void*)(size_t)XTextEdit_textChanged_signal;
}

#endif /* XTEXTEDIT_ON */