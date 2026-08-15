/**
 * @file       XTuiTerminal.c
 * @brief      XTui ANSI 终端控制类实现。
 */

#include "XTuiTerminal.h"

#if XTUI_ON && XTUI_TERMINAL_ON
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

/* ==================== 内部工具 ==================== */

static void writeRaw(XTuiTerminal* t, const char* data, int length)
{
    if (!t || !t->m_write || !data)
        return;
    if (length < 0)
        length = (int)strlen(data);
    t->m_write(t->m_userData, data, (size_t)length);
}

static void writeFmt(XTuiTerminal* t, const char* fmt, ...)
{
    if (!t || !t->m_write)
        return;
    char buf[128];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n <= 0)
        return;
    if ((size_t)n >= sizeof(buf))
        n = (int)sizeof(buf) - 1;
    t->m_write(t->m_userData, buf, (size_t)n);
}

/* ==================== 虚函数实现 ==================== */

static void VXTuiTerminal_deinit(XTuiTerminal* self)
{
    if (!self)
        return;
    self->m_write = NULL;
    self->m_userData = NULL;
    self->m_width = 0;
    self->m_height = 0;
}

static void VXTuiTerminal_copy(XTuiTerminal* dest, const XTuiTerminal* src)
{
    if (!dest || !src)
        return;
    if (dest == src)
        return;
    if (XClassIsVtableNull(dest))
        XTuiTerminal_init(dest);
    dest->m_write = src->m_write;
    dest->m_userData = src->m_userData;
    dest->m_width = src->m_width;
    dest->m_height = src->m_height;
}

static void VXTuiTerminal_move(XTuiTerminal* dest, XTuiTerminal* src)
{
    if (!dest || !src)
        return;
    if (dest == src)
        return;
    if (XClassIsVtableNull(dest))
        XTuiTerminal_init(dest);
    dest->m_write = src->m_write;
    dest->m_userData = src->m_userData;
    dest->m_width = src->m_width;
    dest->m_height = src->m_height;
    src->m_write = NULL;
    src->m_userData = NULL;
    src->m_width = 0;
    src->m_height = 0;
}

/* ==================== 虚函数表 ==================== */

XVtable* XTuiTerminal_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XTuiTerminal)
    XVTABLE_INHERIT_XCLASS(XClass);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXTuiTerminal_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXTuiTerminal_move);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXTuiTerminal_deinit);
    XCLASS_SHOW_SIZE_DEFAULT(XTuiTerminal);
    return XVTABLE_DEFAULT;
}

/* ==================== 构造与析构 ==================== */

void XTuiTerminal_init(XTuiTerminal* terminal)
{
    if (!terminal)
        return;
    memset(((XClass*)terminal) + 1, 0, sizeof(XTuiTerminal) - sizeof(XClass));
    XClass_init(terminal);
    XClassGetVtable(terminal) = XTuiTerminal_class_init();
    terminal->m_width = 80;
    terminal->m_height = 24;
    terminal->m_hasColor = true;
}

XTuiTerminal* XTuiTerminal_create_ex(XMemoryType memory)
{
    XTuiTerminal* term = (XTuiTerminal*)XMemory_malloc(sizeof(XTuiTerminal), memory);
    if (!term)
        return NULL;
    XTuiTerminal_init(term);
    Set_Class_Memory(term, memory); Set_Class_IsHeap(term, true);
    return term;
}

/* ==================== 回调与尺寸 ==================== */

void XTuiTerminal_setWriteCallback(XTuiTerminal* terminal,
                                   XTuiTerminalWriteFunc write,
                                   void* userData)
{
    if (!terminal)
        return;
    terminal->m_write = write;
    terminal->m_userData = userData;
}

void XTuiTerminal_setSize(XTuiTerminal* terminal, int width, int height)
{
    if (!terminal)
        return;
    if (width > 0)
        terminal->m_width = width;
    if (height > 0)
        terminal->m_height = height;
}

int XTuiTerminal_width(const XTuiTerminal* terminal)
{
    return terminal ? terminal->m_width : 0;
}

int XTuiTerminal_height(const XTuiTerminal* terminal)
{
    return terminal ? terminal->m_height : 0;
}

void XTuiTerminal_setColorEnabled(XTuiTerminal* terminal, bool enabled)
{
    if (terminal)
        terminal->m_hasColor = enabled;
}

bool XTuiTerminal_colorEnabled(const XTuiTerminal* terminal)
{
    return terminal && terminal->m_hasColor;
}

bool XTuiTerminal_write(XTuiTerminal* terminal, const char* data, int length)
{
    if (!terminal || !terminal->m_write || !data)
        return false;
    if (length < 0)
        length = (int)strlen(data);
    return terminal->m_write(terminal->m_userData, data, (size_t)length);
}

/* ==================== ANSI 控制 ==================== */

void XTuiTerminal_clearScreen(XTuiTerminal* terminal)
{
    writeRaw(terminal, "\x1b[2J\x1b[H", -1);
}

void XTuiTerminal_clearLine(XTuiTerminal* terminal)
{
    writeRaw(terminal, "\x1b[2K", -1);
}

void XTuiTerminal_moveTo(XTuiTerminal* terminal, int x, int y)
{
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    writeFmt(terminal, "\x1b[%d;%dH", y + 1, x + 1);
}

void XTuiTerminal_moveUp(XTuiTerminal* terminal, int n)
{
    if (n <= 0)
        return;
    writeFmt(terminal, "\x1b[%dA", n);
}

void XTuiTerminal_moveDown(XTuiTerminal* terminal, int n)
{
    if (n <= 0)
        return;
    writeFmt(terminal, "\x1b[%dB", n);
}

void XTuiTerminal_moveRight(XTuiTerminal* terminal, int n)
{
    if (n <= 0)
        return;
    writeFmt(terminal, "\x1b[%dC", n);
}

void XTuiTerminal_moveLeft(XTuiTerminal* terminal, int n)
{
    if (n <= 0)
        return;
    writeFmt(terminal, "\x1b[%dD", n);
}

void XTuiTerminal_saveCursor(XTuiTerminal* terminal)
{
    writeRaw(terminal, "\x1b" "7", -1);
}

void XTuiTerminal_restoreCursor(XTuiTerminal* terminal)
{
    writeRaw(terminal, "\x1b" "8", -1);
}

void XTuiTerminal_showCursor(XTuiTerminal* terminal)
{
    writeRaw(terminal, "\x1b[?25h", -1);
}

void XTuiTerminal_hideCursor(XTuiTerminal* terminal)
{
    writeRaw(terminal, "\x1b[?25l", -1);
}

void XTuiTerminal_setForeground(XTuiTerminal* terminal, XColor color)
{
    if (!terminal || !terminal->m_hasColor)
        return;
    uint8_t index = XTuiColor_toIndex(&color);
    if (index == XTUI_COLOR_DEFAULT_INDEX)
        writeRaw(terminal, "\x1b[39m", -1);
    else if (index >= 8)
        writeFmt(terminal, "\x1b[%dm", 90 + (index - 8));
    else
        writeFmt(terminal, "\x1b[%dm", 30 + index);
}

void XTuiTerminal_setBackground(XTuiTerminal* terminal, XColor color)
{
    if (!terminal || !terminal->m_hasColor)
        return;
    uint8_t index = XTuiColor_toIndex(&color);
    if (index == XTUI_COLOR_DEFAULT_INDEX)
        writeRaw(terminal, "\x1b[49m", -1);
    else if (index >= 8)
        writeFmt(terminal, "\x1b[%dm", 100 + (index - 8));
    else
        writeFmt(terminal, "\x1b[%dm", 40 + index);
}

void XTuiTerminal_setAttributes(XTuiTerminal* terminal, int attrs)
{
    if (!terminal || !terminal->m_hasColor)
        return;
    if (attrs == 0) {
        writeRaw(terminal, "\x1b[0m", -1);
        return;
    }
    if (attrs & XTuiAttribute_Bold)
        writeRaw(terminal, "\x1b[1m", -1);
    if (attrs & XTuiAttribute_Underline)
        writeRaw(terminal, "\x1b[4m", -1);
    if (attrs & XTuiAttribute_Blink)
        writeRaw(terminal, "\x1b[5m", -1);
    if (attrs & XTuiAttribute_Reverse)
        writeRaw(terminal, "\x1b[7m", -1);
    if (attrs & XTuiAttribute_Hidden)
        writeRaw(terminal, "\x1b[8m", -1);
}

void XTuiTerminal_resetAttributes(XTuiTerminal* terminal)
{
    writeRaw(terminal, "\x1b[0m", -1);
}

void XTuiTerminal_enterAlternateScreen(XTuiTerminal* terminal)
{
    writeRaw(terminal, "\x1b[?1049h\x1b[2J\x1b[H", -1);
}

void XTuiTerminal_leaveAlternateScreen(XTuiTerminal* terminal)
{
    writeRaw(terminal, "\x1b[?1049l", -1);
}

#endif /* XTUI_ON && XTUI_TERMINAL_ON */
