/**
 * @file       XTuiTextEdit.c
 * @brief      XTui 单行文本编辑控件实现。
 */

#include "XTuiTextEdit.h"

#if XTUI_ON && XTUI_WIDGET_ON
#include <string.h>

/* ==================== UTF-8 工具 ==================== */

static size_t utf8CharLen(unsigned char c)
{
    if (c < 0x80)
        return 1;
    if ((c & 0xE0) == 0xC0)
        return 2;
    if ((c & 0xF0) == 0xE0)
        return 3;
    if ((c & 0xF8) == 0xF0)
        return 4;
    return 1;
}

static size_t prevChar(const char* s, size_t pos)
{
    if (pos == 0)
        return 0;
    size_t p = pos - 1;
    /* 回退直到找到首个字节。 */
    while (p > 0 && (((unsigned char)s[p]) & 0xC0) == 0x80)
        --p;
    return p;
}

static size_t nextChar(const char* s, size_t len, size_t pos)
{
    if (pos >= len)
        return len;
    size_t clen = utf8CharLen((unsigned char)s[pos]);
    pos += clen;
    return pos > len ? len : pos;
}

/* ==================== 虚函数实现 ==================== */

static bool VXTuiTextEdit_render(XTuiWidget* base, XTuiScreen* screen)
{
    XTuiTextEdit* edit = (XTuiTextEdit*)base;
    if (!edit || !screen)
        return false;
    XRect r = XTuiWidget_rect(base);
    if (r.width < 1 || r.height < 1)
        return false;

    int x = r.x;
    int y = r.y;
    size_t pos = 0;
    int col = 0;
    while (x < r.x + r.width && pos < edit->m_length) {
        size_t clen = utf8CharLen((unsigned char)edit->m_text[pos]);
        char buf[XTUI_CELL_UTF8_MAX + 1];
        memset(buf, 0, sizeof(buf));
        if (clen > XTUI_CELL_UTF8_MAX)
            clen = 1;
        memcpy(buf, edit->m_text + pos, clen);

        int attrs = (int)XTuiAttribute_None;
        if (((XTuiWidget*)edit)->m_focused && (int)pos == edit->m_cursor)
            attrs |= (int)XTuiAttribute_Reverse;
        if (edit->m_password)
            strcpy(buf, "*");
        XTuiScreen_setCell(screen, x, y, buf, XTUI_COLOR_DEFAULT, XTUI_COLOR_DEFAULT, attrs);
        pos = nextChar(edit->m_text, edit->m_length, pos);
        ++x;
        ++col;
    }
    /* 光标在行尾时的反显空格。 */
    if (((XTuiWidget*)edit)->m_focused && edit->m_cursor >= (int)edit->m_length && x < r.x + r.width) {
        XTuiScreen_setCell(screen, x, y, " ", XTUI_COLOR_DEFAULT, XTUI_COLOR_DEFAULT, (int)XTuiAttribute_Reverse);
    }
    (void)col;
    return true;
}

static bool VXTuiTextEdit_keyPress(XTuiWidget* base, const XTuiKeyEvent* event)
{
    XTuiTextEdit* edit = (XTuiTextEdit*)base;
    if (!edit || !event)
        return false;
    switch (event->m_keyType) {
    case XTuiKey_Char:
        return XTuiTextEdit_insertUtf8(edit, event->m_utf8, (int)strlen(event->m_utf8));
    case XTuiKey_Backspace:
        return XTuiTextEdit_backspace(edit);
    case XTuiKey_Delete:
        return XTuiTextEdit_deleteChar(edit);
    case XTuiKey_ArrowLeft:
        if (edit->m_cursor > 0) {
            edit->m_cursor = (int)prevChar(edit->m_text, (size_t)edit->m_cursor);
            return true;
        }
        return false;
    case XTuiKey_ArrowRight:
        if (edit->m_cursor < (int)edit->m_length) {
            edit->m_cursor = (int)nextChar(edit->m_text, edit->m_length, (size_t)edit->m_cursor);
            return true;
        }
        return false;
    case XTuiKey_Home:
        edit->m_cursor = 0;
        return true;
    case XTuiKey_End:
        edit->m_cursor = (int)edit->m_length;
        return true;
    default:
        return false;
    }
}

static void VXTuiTextEdit_deinit(XTuiTextEdit* self)
{
    if (!self)
        return;
    if (self->m_text) {
        XFree_System(self->m_text);
        self->m_text = NULL;
    }
    self->m_length = 0;
    self->m_capacity = 0;
    self->m_cursor = 0;
    XClass_Deinit_Parent(XTuiWidget, (XTuiWidget*)self);
}

static bool copyText(XTuiTextEdit* dest, const XTuiTextEdit* src)
{
    if (dest->m_capacity < src->m_length + 1) {
        char* text = (char*)XMalloc_System(src->m_length + 1);
        if (!text)
            return false;
        if (dest->m_text)
            XFree_System(dest->m_text);
        dest->m_text = text;
        dest->m_capacity = src->m_length + 1;
    }
    memcpy(dest->m_text, src->m_text, src->m_length + 1);
    dest->m_length = src->m_length;
    dest->m_cursor = src->m_cursor;
    dest->m_password = src->m_password;
    return true;
}

static void VXTuiTextEdit_copy(XTuiTextEdit* dest, const XTuiTextEdit* src)
{
    if (!dest || !src)
        return;
    if (dest == src)
        return;
    if (XClassIsVtableNull(dest))
        XTuiTextEdit_init(dest);
    XClass_Parent(XTuiWidget, EXClass_Copy, void(*)(XTuiWidget*, const XTuiWidget*))((XTuiWidget*)dest, (const XTuiWidget*)src);
    copyText(dest, src);
}

static void VXTuiTextEdit_move(XTuiTextEdit* dest, XTuiTextEdit* src)
{
    if (!dest || !src)
        return;
    if (dest == src)
        return;
    if (XClassIsVtableNull(dest))
        XTuiTextEdit_init(dest);
    XClass_Parent(XTuiWidget, EXClass_Move, void(*)(XTuiWidget*, XTuiWidget*))((XTuiWidget*)dest, (XTuiWidget*)src);
    if (dest->m_text)
        XFree_System(dest->m_text);
    dest->m_text = src->m_text;
    dest->m_length = src->m_length;
    dest->m_capacity = src->m_capacity;
    dest->m_cursor = src->m_cursor;
    dest->m_password = src->m_password;
    src->m_text = NULL;
    src->m_length = 0;
    src->m_capacity = 0;
    src->m_cursor = 0;
}

/* ==================== 虚函数表 ==================== */

XVtable* XTuiTextEdit_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XTuiTextEdit)
    XVTABLE_INHERIT_XCLASS(XTuiWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXTuiWidget_Render, VXTuiTextEdit_render);
    XVTABLE_OVERLOAD_DEFAULT(EXTuiWidget_KeyPress, VXTuiTextEdit_keyPress);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXTuiTextEdit_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXTuiTextEdit_move);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXTuiTextEdit_deinit);
    XCLASS_SHOW_SIZE_DEFAULT(XTuiTextEdit);
    return XVTABLE_DEFAULT;
}

/* ==================== 构造与析构 ==================== */

void XTuiTextEdit_init(XTuiTextEdit* edit)
{
    if (!edit)
        return;
    memset(((XClass*)edit) + 1, 0, sizeof(XTuiTextEdit) - sizeof(XClass));
    XTuiWidget_init((XTuiWidget*)edit);
    XClassGetVtable(edit) = XTuiTextEdit_class_init();
    edit->m_capacity = 32;
    edit->m_text = (char*)XMalloc_System(edit->m_capacity);
    if (edit->m_text)
        edit->m_text[0] = '\0';
    else
        edit->m_capacity = 0;
}

XTuiTextEdit* XTuiTextEdit_create_ex(XMemoryType memory)
{
    XTuiTextEdit* edit = (XTuiTextEdit*)XMemory_malloc(sizeof(XTuiTextEdit), memory);
    if (!edit)
        return NULL;
    XTuiTextEdit_init(edit);
    Set_Class_Memory(edit, memory); Set_Class_IsHeap(edit, true);
    return edit;
}

/* ==================== 属性 ==================== */

void XTuiTextEdit_setText(XTuiTextEdit* edit, const char* text)
{
    if (!edit)
        return;
    if (!text)
        text = "";
    size_t len = strlen(text);
    if (edit->m_capacity < len + 1) {
        char* copy = (char*)XMalloc_System(len + 1);
        if (!copy)
            return;
        if (edit->m_text)
            XFree_System(edit->m_text);
        edit->m_text = copy;
        edit->m_capacity = len + 1;
    }
    memcpy(edit->m_text, text, len + 1);
    edit->m_length = len;
    edit->m_cursor = (int)len;
}

const char* XTuiTextEdit_text(const XTuiTextEdit* edit)
{
    return edit ? (const char*)edit->m_text : "";
}

void XTuiTextEdit_clear(XTuiTextEdit* edit)
{
    if (!edit)
        return;
    if (edit->m_text)
        edit->m_text[0] = '\0';
    edit->m_length = 0;
    edit->m_cursor = 0;
}

void XTuiTextEdit_setPassword(XTuiTextEdit* edit, bool password)
{
    if (edit)
        edit->m_password = password;
}

bool XTuiTextEdit_isPassword(const XTuiTextEdit* edit)
{
    return edit ? edit->m_password : false;
}

size_t XTuiTextEdit_length(const XTuiTextEdit* edit)
{
    return edit ? edit->m_length : 0;
}

int XTuiTextEdit_cursor(const XTuiTextEdit* edit)
{
    return edit ? edit->m_cursor : 0;
}

bool XTuiTextEdit_insertUtf8(XTuiTextEdit* edit, const char* utf8, int length)
{
    if (!edit || !utf8 || length <= 0)
        return false;
    if ((int)edit->m_length + length + 1 > XTUI_TEXTEDIT_MAX_BYTES)
        return false;
    if (edit->m_capacity < edit->m_length + (size_t)length + 1) {
        size_t cap = edit->m_capacity ? edit->m_capacity : 32;
        while (cap < edit->m_length + (size_t)length + 1)
            cap *= 2;
        char* text = (char*)XMalloc_System(cap);
        if (!text)
            return false;
        if (edit->m_text && edit->m_length)
            memcpy(text, edit->m_text, edit->m_length);
        if (edit->m_text)
            XFree_System(edit->m_text);
        edit->m_text = text;
        edit->m_capacity = cap;
    }
    if (!edit->m_text)
        return false;
    memmove(edit->m_text + edit->m_cursor + length,
            edit->m_text + edit->m_cursor,
            edit->m_length - (size_t)edit->m_cursor + 1);
    memcpy(edit->m_text + edit->m_cursor, utf8, (size_t)length);
    edit->m_length += (size_t)length;
    edit->m_cursor += length;
    return true;
}

bool XTuiTextEdit_backspace(XTuiTextEdit* edit)
{
    if (!edit || edit->m_cursor <= 0)
        return false;
    size_t start = prevChar(edit->m_text, (size_t)edit->m_cursor);
    size_t clen = (size_t)edit->m_cursor - start;
    memmove(edit->m_text + start, edit->m_text + edit->m_cursor,
            edit->m_length - (size_t)edit->m_cursor + 1);
    edit->m_length -= clen;
    edit->m_cursor = (int)start;
    return true;
}

bool XTuiTextEdit_deleteChar(XTuiTextEdit* edit)
{
    if (!edit || edit->m_cursor >= (int)edit->m_length)
        return false;
    size_t start = nextChar(edit->m_text, edit->m_length, (size_t)edit->m_cursor);
    size_t clen = start - (size_t)edit->m_cursor;
    memmove(edit->m_text + edit->m_cursor, edit->m_text + start,
            edit->m_length - start + 1);
    edit->m_length -= clen;
    return true;
}

#endif /* XTUI_ON && XTUI_WIDGET_ON */
