/**
 * @file       XTui.c
 * @brief      XTui 通用 TUI 会话类实现。
 */

#include "XTui.h"

#if XTUI_ON && XTUI_SESSION_ON
#include <string.h>
#include <stdio.h>

/* ==================== 内部工具 ==================== */

static size_t utf8ExpectedLen(unsigned char c)
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

static void postCharEvent(XTui* self, const char* utf8, XKeyboardModifiers modifiers)
{
    XTuiKeyEvent ev;
    XTuiKeyEvent_init(&ev, XEVENT_TYPE_KEY_PRESS, XTuiKey_Char, modifiers);
    size_t len = strlen(utf8);
    if (len > XTUI_CELL_UTF8_MAX)
        len = XTUI_CELL_UTF8_MAX;
    memcpy(ev.m_utf8, utf8, len);
    ev.m_utf8[len] = '\0';
    XTui_handleKey(self, &ev);
}

static void postKeyEvent(XTui* self, XTuiKeyType type, XKeyboardModifiers modifiers)
{
    XTuiKeyEvent ev;
    XTuiKeyEvent_init(&ev, XEVENT_TYPE_KEY_PRESS, type, modifiers);
    XTui_handleKey(self, &ev);
}

static int firstCsiNumber(const char* buf, size_t len, int def)
{
    if (len == 0)
        return def;
    int value = 0;
    size_t i = 0;
    while (i < len && buf[i] >= '0' && buf[i] <= '9') {
        value = value * 10 + (buf[i] - '0');
        if (value > 100000)
            return def;
        ++i;
    }
    if (i == 0)
        return def;
    return value;
}

static void parseCsiFinal(XTui* self, char finalByte)
{
    const char* params = self->m_csiBuffer;
    size_t plen = self->m_csiLength;
    switch (finalByte) {
    case 'A': postKeyEvent(self, XTuiKey_ArrowUp, XKeyboardModifier_NoModifier); break;
    case 'B': postKeyEvent(self, XTuiKey_ArrowDown, XKeyboardModifier_NoModifier); break;
    case 'C': postKeyEvent(self, XTuiKey_ArrowRight, XKeyboardModifier_NoModifier); break;
    case 'D': postKeyEvent(self, XTuiKey_ArrowLeft, XKeyboardModifier_NoModifier); break;
    case 'H': postKeyEvent(self, XTuiKey_Home, XKeyboardModifier_NoModifier); break;
    case 'F': postKeyEvent(self, XTuiKey_End, XKeyboardModifier_NoModifier); break;
    case '~': {
        int n = firstCsiNumber(params, plen, 0);
        switch (n) {
        case 1: case 7: postKeyEvent(self, XTuiKey_Home, XKeyboardModifier_NoModifier); break;
        case 4: case 8: postKeyEvent(self, XTuiKey_End, XKeyboardModifier_NoModifier); break;
        case 5: postKeyEvent(self, XTuiKey_PageUp, XKeyboardModifier_NoModifier); break;
        case 6: postKeyEvent(self, XTuiKey_PageDown, XKeyboardModifier_NoModifier); break;
        case 2: postKeyEvent(self, XTuiKey_Insert, XKeyboardModifier_NoModifier); break;
        case 3: postKeyEvent(self, XTuiKey_Delete, XKeyboardModifier_NoModifier); break;
        case 11: postKeyEvent(self, XTuiKey_F1, XKeyboardModifier_NoModifier); break;
        case 12: postKeyEvent(self, XTuiKey_F2, XKeyboardModifier_NoModifier); break;
        case 13: postKeyEvent(self, XTuiKey_F3, XKeyboardModifier_NoModifier); break;
        case 14: postKeyEvent(self, XTuiKey_F4, XKeyboardModifier_NoModifier); break;
        case 15: postKeyEvent(self, XTuiKey_F5, XKeyboardModifier_NoModifier); break;
        case 17: postKeyEvent(self, XTuiKey_F6, XKeyboardModifier_NoModifier); break;
        case 18: postKeyEvent(self, XTuiKey_F7, XKeyboardModifier_NoModifier); break;
        case 19: postKeyEvent(self, XTuiKey_F8, XKeyboardModifier_NoModifier); break;
        case 20: postKeyEvent(self, XTuiKey_F9, XKeyboardModifier_NoModifier); break;
        case 21: postKeyEvent(self, XTuiKey_F10, XKeyboardModifier_NoModifier); break;
        case 23: postKeyEvent(self, XTuiKey_F11, XKeyboardModifier_NoModifier); break;
        case 24: postKeyEvent(self, XTuiKey_F12, XKeyboardModifier_NoModifier); break;
        default: break;
        }
        break;
    }
    default:
        break;
    }
}

static void parseOFinal(XTui* self, char finalByte)
{
    switch (finalByte) {
    case 'A': postKeyEvent(self, XTuiKey_ArrowUp, XKeyboardModifier_NoModifier); break;
    case 'B': postKeyEvent(self, XTuiKey_ArrowDown, XKeyboardModifier_NoModifier); break;
    case 'C': postKeyEvent(self, XTuiKey_ArrowRight, XKeyboardModifier_NoModifier); break;
    case 'D': postKeyEvent(self, XTuiKey_ArrowLeft, XKeyboardModifier_NoModifier); break;
    case 'H': postKeyEvent(self, XTuiKey_Home, XKeyboardModifier_NoModifier); break;
    case 'F': postKeyEvent(self, XTuiKey_End, XKeyboardModifier_NoModifier); break;
    case 'P': postKeyEvent(self, XTuiKey_F1, XKeyboardModifier_NoModifier); break;
    case 'Q': postKeyEvent(self, XTuiKey_F2, XKeyboardModifier_NoModifier); break;
    case 'R': postKeyEvent(self, XTuiKey_F3, XKeyboardModifier_NoModifier); break;
    case 'S': postKeyEvent(self, XTuiKey_F4, XKeyboardModifier_NoModifier); break;
    default: break;
    }
}

static int feedByte(XTui* self, unsigned char c)
{
    switch (self->m_parseState) {
    case XTuiParse_Normal:
        /* 继续累积 UTF-8 多字节字符。 */
        if (self->m_utf8Pos > 0 && self->m_utf8Pos < self->m_utf8Expected) {
            if (self->m_utf8Pos < XTUI_CELL_UTF8_MAX) {
                self->m_utf8Buf[self->m_utf8Pos++] = (char)c;
                if (self->m_utf8Pos >= self->m_utf8Expected) {
                    self->m_utf8Buf[self->m_utf8Pos] = '\0';
                    postCharEvent(self, self->m_utf8Buf, XKeyboardModifier_NoModifier);
                    self->m_utf8Pos = 0;
                    self->m_utf8Expected = 0;
                }
            } else {
                self->m_utf8Pos = 0;
                self->m_utf8Expected = 0;
            }
            return 1;
        }
        if (c == 0x1B) {
            self->m_lastByteCR = false;
            self->m_parseState = XTuiParse_Esc;
            return 1;
        }
        if (c == '\r') {
            /* CR 派发 Enter。CRLF 回车的 LF 由下一个字节处理时跳过。 */
            self->m_lastByteCR = true;
            postKeyEvent(self, XTuiKey_Enter, XKeyboardModifier_NoModifier);
            return 1;
        }
        if (c == '\n') {
            if (self->m_lastByteCR) {
                self->m_lastByteCR = false;
                return 1;
            }
            postKeyEvent(self, XTuiKey_Enter, XKeyboardModifier_NoModifier);
            return 1;
        }
        if (c == 0x7F || c == 0x08) {
            self->m_lastByteCR = false;
            postKeyEvent(self, XTuiKey_Backspace, XKeyboardModifier_NoModifier);
            return 1;
        }
        if (c == '\t') {
            self->m_lastByteCR = false;
            postKeyEvent(self, XTuiKey_Tab, XKeyboardModifier_NoModifier);
            return 1;
        }
        if (c >= 0x01 && c <= 0x1A) {
            self->m_lastByteCR = false;
            char ch[2];
            ch[0] = (char)('a' + c - 1);
            ch[1] = '\0';
            postCharEvent(self, ch, XKeyboardModifier_ControlModifier);
            return 1;
        }
        if (c >= 0x20 && c != 0x7F) {
            self->m_lastByteCR = false;
            self->m_utf8Expected = utf8ExpectedLen(c);
            self->m_utf8Pos = 0;
            self->m_utf8Buf[0] = (char)c;
            self->m_utf8Pos = 1;
            if (self->m_utf8Pos >= self->m_utf8Expected) {
                self->m_utf8Buf[self->m_utf8Pos] = '\0';
                postCharEvent(self, self->m_utf8Buf, XKeyboardModifier_NoModifier);
                self->m_utf8Pos = 0;
                self->m_utf8Expected = 0;
            }
            return 1;
        }
        return 1;

    case XTuiParse_Esc:
        if (c == '[') {
            self->m_parseState = XTuiParse_Csi;
            self->m_csiLength = 0;
            return 1;
        }
        if (c == 'O') {
            self->m_parseState = XTuiParse_O;
            return 1;
        }
        /* 单独的 ESC，然后按普通字节重新处理。 */
        postKeyEvent(self, XTuiKey_Escape, XKeyboardModifier_NoModifier);
        self->m_parseState = XTuiParse_Normal;
        return feedByte(self, c);

    case XTuiParse_Csi:
        if (c >= 0x40 && c <= 0x7E) {
            parseCsiFinal(self, (char)c);
            self->m_parseState = XTuiParse_Normal;
            return 1;
        }
        if (self->m_csiLength < sizeof(self->m_csiBuffer) - 1) {
            self->m_csiBuffer[self->m_csiLength++] = (char)c;
            self->m_csiBuffer[self->m_csiLength] = '\0';
        }
        return 1;

    case XTuiParse_O:
        if (c >= 0x40 && c <= 0x7E) {
            parseOFinal(self, (char)c);
            self->m_parseState = XTuiParse_Normal;
            return 1;
        }
        postKeyEvent(self, XTuiKey_Escape, XKeyboardModifier_NoModifier);
        self->m_parseState = XTuiParse_Normal;
        return feedByte(self, c);
    }
    return 1;
}

/* ==================== 虚函数实现 ==================== */

static void VXTui_deinit(XTui* self)
{
    if (!self)
        return;
    if (self->m_previousScreen) {
        XTuiScreen_delete_base(self->m_previousScreen);
        self->m_previousScreen = NULL;
    }
    self->m_screen = NULL;
    self->m_terminal = NULL;
    self->m_root = NULL;
    self->m_focus = NULL;
    self->m_running = false;
    self->m_lastByteCR = false;
}

static void VXTui_copy(XTui* dest, const XTui* src)
{
    if (!dest || !src)
        return;
    if (dest == src)
        return;
    if (XClassIsVtableNull(dest))
        XTui_init(dest);

    dest->m_screen = src->m_screen;
    dest->m_terminal = src->m_terminal;
    dest->m_root = src->m_root;
    dest->m_focus = src->m_focus;
    dest->m_running = src->m_running;
    dest->m_useAlternateScreen = src->m_useAlternateScreen;
    dest->m_lastByteCR = src->m_lastByteCR;
    dest->m_parseState = src->m_parseState;
    dest->m_csiLength = src->m_csiLength;
    memcpy(dest->m_csiBuffer, src->m_csiBuffer, sizeof(src->m_csiBuffer));
    dest->m_utf8Pos = src->m_utf8Pos;
    dest->m_utf8Expected = src->m_utf8Expected;
    memcpy(dest->m_utf8Buf, src->m_utf8Buf, sizeof(src->m_utf8Buf));

    if (src->m_previousScreen) {
        if (!dest->m_previousScreen)
            dest->m_previousScreen = XTuiScreen_create();
        if (dest->m_previousScreen)
            XTuiScreen_copyFrom(dest->m_previousScreen, src->m_previousScreen);
    } else if (dest->m_previousScreen) {
        XTuiScreen_delete_base(dest->m_previousScreen);
        dest->m_previousScreen = NULL;
    }
}

static void VXTui_move(XTui* dest, XTui* src)
{
    if (!dest || !src)
        return;
    if (dest == src)
        return;
    if (XClassIsVtableNull(dest))
        XTui_init(dest);

    dest->m_screen = src->m_screen;
    dest->m_terminal = src->m_terminal;
    dest->m_root = src->m_root;
    dest->m_focus = src->m_focus;
    dest->m_running = src->m_running;
    dest->m_useAlternateScreen = src->m_useAlternateScreen;
    dest->m_lastByteCR = src->m_lastByteCR;
    dest->m_parseState = src->m_parseState;
    dest->m_csiLength = src->m_csiLength;
    memcpy(dest->m_csiBuffer, src->m_csiBuffer, sizeof(src->m_csiBuffer));
    dest->m_utf8Pos = src->m_utf8Pos;
    dest->m_utf8Expected = src->m_utf8Expected;
    memcpy(dest->m_utf8Buf, src->m_utf8Buf, sizeof(src->m_utf8Buf));

    if (dest->m_previousScreen)
        XTuiScreen_delete_base(dest->m_previousScreen);
    dest->m_previousScreen = src->m_previousScreen;
    src->m_previousScreen = NULL;

    src->m_screen = NULL;
    src->m_terminal = NULL;
    src->m_root = NULL;
    src->m_focus = NULL;
    src->m_running = false;
}

/* ==================== 虚函数表 ==================== */

XVtable* XTui_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XTui)
    XVTABLE_INHERIT_XCLASS(XClass);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXTui_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXTui_move);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXTui_deinit);
    XCLASS_SHOW_SIZE_DEFAULT(XTui);
    return XVTABLE_DEFAULT;
}

/* ==================== 构造与析构 ==================== */

void XTui_init(XTui* tui)
{
    if (!tui)
        return;
    memset(((XClass*)tui) + 1, 0, sizeof(XTui) - sizeof(XClass));
    XClass_init(tui);
    XClassGetVtable(tui) = XTui_class_init();
    tui->m_useAlternateScreen = true;
    tui->m_lastByteCR = false;
}

XTui* XTui_create(void)
{
    XTui* tui = (XTui*)XMalloc_System(sizeof(XTui));
    if (!tui)
        return NULL;
    XTui_init(tui);
    Set_Class_MemoryFree(tui, XFree_System);
    return tui;
}

/* ==================== 装配 ==================== */

void XTui_setScreen(XTui* self, XTuiScreen* screen)
{
    if (self)
        self->m_screen = screen;
}

XTuiScreen* XTui_screen(const XTui* self)
{
    return self ? self->m_screen : NULL;
}

void XTui_setTerminal(XTui* self, XTuiTerminal* terminal)
{
    if (self)
        self->m_terminal = terminal;
}

XTuiTerminal* XTui_terminal(const XTui* self)
{
    return self ? self->m_terminal : NULL;
}

void XTui_setRootWidget(XTui* self, XTuiWidget* widget)
{
    if (self) {
        self->m_root = widget;
        if (!self->m_focus)
            self->m_focus = widget;
    }
}

XTuiWidget* XTui_rootWidget(const XTui* self)
{
    return self ? self->m_root : NULL;
}

void XTui_setFocusWidget(XTui* self, XTuiWidget* widget)
{
    if (!self || !widget)
        return;
    if (self->m_focus && self->m_focus != widget)
        XTuiWidget_focusOut_base(self->m_focus);
    self->m_focus = widget;
    XTuiWidget_focusIn_base(widget);
}

XTuiWidget* XTui_focusWidget(const XTui* self)
{
    return self ? self->m_focus : NULL;
}

void XTui_setUseAlternateScreen(XTui* self, bool enabled)
{
    if (self)
        self->m_useAlternateScreen = enabled;
}

/* ==================== 生命周期 ==================== */

bool XTui_start(XTui* self)
{
    if (!self || !self->m_terminal || !self->m_screen)
        return false;
    self->m_running = false;
    if (self->m_useAlternateScreen)
        XTuiTerminal_enterAlternateScreen(self->m_terminal);
    else
        XTuiTerminal_clearScreen(self->m_terminal);

    int tw = XTuiTerminal_width(self->m_terminal);
    int th = XTuiTerminal_height(self->m_terminal);
    if (tw > 0 && th > 0)
        XTuiScreen_resize(self->m_screen, tw, th);

    if (!self->m_previousScreen) {
        self->m_previousScreen = XTuiScreen_create();
        if (!self->m_previousScreen)
            return false;
    }
    XTuiScreen_resize(self->m_previousScreen, XTuiScreen_width(self->m_screen), XTuiScreen_height(self->m_screen));
    XTuiScreen_clear(self->m_previousScreen);

    self->m_running = true;
    return XTui_refresh(self);
}

void XTui_stop(XTui* self)
{
    if (!self)
        return;
    if (self->m_terminal) {
        XTuiTerminal_resetAttributes(self->m_terminal);
        if (self->m_useAlternateScreen)
            XTuiTerminal_leaveAlternateScreen(self->m_terminal);
    }
    self->m_running = false;
}

/* ==================== 渲染与输入 ==================== */

bool XTui_refresh(XTui* self)
{
    if (!self || !self->m_screen)
        return false;
    XTuiScreen_clear(self->m_screen);
    if (self->m_root)
        XTuiWidget_render_base(self->m_root, self->m_screen);
    return XTui_paint(self);
}

bool XTui_paint(XTui* self)
{
    if (!self || !self->m_screen || !self->m_terminal)
        return false;
    if (!self->m_previousScreen)
        return false;

    XTuiScreen* cur = self->m_screen;
    XTuiScreen* prev = self->m_previousScreen;
    int w = XTuiScreen_width(cur);
    int h = XTuiScreen_height(cur);
    if (XTuiScreen_width(prev) != w || XTuiScreen_height(prev) != h)
        XTuiScreen_resize(prev, w, h);

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const XTuiCell* c = XTuiScreen_cell(cur, x, y);
            const XTuiCell* p = XTuiScreen_cell(prev, x, y);
            if (!c)
                continue;
            if (p && memcmp(c, p, sizeof(XTuiCell)) == 0)
                continue;

            XTuiTerminal_moveTo(self->m_terminal, x, y);
            if (c->m_fg != XTUI_COLOR_DEFAULT_INDEX)
                XTuiTerminal_setForeground(self->m_terminal, XTuiColor_fromIndex(c->m_fg));
            if (c->m_bg != XTUI_COLOR_DEFAULT_INDEX)
                XTuiTerminal_setBackground(self->m_terminal, XTuiColor_fromIndex(c->m_bg));
            if (c->m_attrs)
                XTuiTerminal_setAttributes(self->m_terminal, c->m_attrs);
            XTuiTerminal_write(self->m_terminal, c->m_utf8, -1);
            XTuiTerminal_setAttributes(self->m_terminal, 0);
        }
    }

    XTuiScreen_copyFrom(self->m_previousScreen, cur);
    return true;
}

int XTui_feedInput(XTui* self, const char* data, int length)
{
    if (!self || !data || length <= 0)
        return 0;
    int consumed = 0;
    for (int i = 0; i < length; ++i) {
        feedByte(self, (unsigned char)data[i]);
        ++consumed;
    }
    return consumed;
}

bool XTui_handleKey(XTui* self, const XTuiKeyEvent* event)
{
    if (ISNULL(self, "XTui") || ISNULL(event, "XTuiKeyEvent"))
        return false;
    if (self->m_focus && XTuiWidget_keyPress_base(self->m_focus, event))
        return true;
    if (self->m_root && self->m_root != self->m_focus && XTuiWidget_keyPress_base(self->m_root, event))
        return true;
    return false;
}

#endif /* XTUI_ON && XTUI_SESSION_ON */
