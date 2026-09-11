/**
 * @file       XPlainTextEdit.c
 * @brief      多行纯文本编辑控件实现（对标 Qt 6.8 QPlainTextEdit 全部公共 API）。
 * @details    与同名头文件的公共 API 一一对应；内部实现细节见
 *             头文件 @note 与函数级 Doxygen 注释。
 * @author     XinYueC 团队
 */

#include "XPlainTextEdit.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XPainter.h"
#include "XVarList.h"
#include "XClipboard.h"
#include "XGuiApplication.h"
#include "XGuiConfig.h"
#include "XWidget_Protected.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#if XWIDGET_ON && XABSTRACTSCROLLAREA_ON && XPLAINTEXTEDIT_ON

/* ==================== 内部工具 ==================== */

#define XPE_LINE_HEIGHT 16

static char* xpe_lineAt(const XPlainTextEdit* self, int index)
{
    char** item;
    if (!self || !self->m_lines || index < 0 ||
        index >= (int)XVector_size_base(
                     (const XContainer*)self->m_lines))
        return (char*)"";
    item = (char**)XVector_at_base(self->m_lines, index);
    return (item && *item) ? *item : (char*)"";
}

static int xpe_lineCount(const XPlainTextEdit* self)
{
    return (self && self->m_lines)
               ? (int)XVector_size_base(
                     (const XContainer*)self->m_lines)
               : 0;
}

static void xpe_setLine(XPlainTextEdit* self, int index, const char* text)
{
    char** item;
    char* copy;
    size_t len;
    if (!self || !self->m_lines || index < 0 ||
        index >= (int)XVector_size_base((const XContainer*)self->m_lines))
        return;
    item = (char**)XVector_at_base(self->m_lines, index);
    if (!item) return;
    len = strlen(text) + 1;
    copy = (char*)XMalloc_System(len);
    if (!copy) return;
    memcpy(copy, text, len);
    if (*item) XFree_System(*item);
    *item = copy;
}

static void xpe_insertLineAt(XPlainTextEdit* self, int index, const char* text)
{
    char* copy;
    size_t len;
    if (!self || !self->m_lines || index < 0 ||
        index > (int)XVector_size_base((const XContainer*)self->m_lines))
        return;
    len = strlen(text) + 1;
    copy = (char*)XMalloc_System(len);
    if (!copy) return;
    memcpy(copy, text, len);
    XVector_insert_1_base(self->m_lines, index, &copy, 1);
}

static void xpe_removeLineAt(XPlainTextEdit* self, int index)
{
    char** item;
    if (!self || !self->m_lines || index < 0 ||
        index >= (int)XVector_size_base((const XContainer*)self->m_lines))
        return;
    item = (char**)XVector_at_base(self->m_lines, index);
    if (item && *item) XFree_System(*item);
    XVector_remove_base(self->m_lines, index, 1);
}

static uint32_t xpe_color(const XPlainTextEdit* self, XPaletteColorRole role)
{
#if XPALETTE_ON
    XPalette palette = XWidget_palette((XWidget*)self);
    XColor c = XPalette_color(&palette, XPaletteColorGroup_Current, role);
    return XColor_rgba(&c);
#else
    (void)self; (void)role;
    return 0xFF000000u;
#endif /* XPALETTE_ON */
}

static void xpe_emitChanged(XPlainTextEdit* self)
{
    XVarList* args = XVarList_create(0);
    if (!args) return;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self,
                           (size_t)XPlainTextEdit_textChanged_signal, args,
                           NULL, NULL, XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

static void xpe_pushUndo(XPlainTextEdit* self)
{
    char* snapshot;
    char* full;
    if (!self || !self->m_undoEnabled || !self->m_undoStack) return;
    snapshot = XPlainTextEdit_toPlainText(self);
    if (!snapshot) return;
    full = snapshot;
    XVector_push_back_1_base(self->m_undoStack, &full);
    if (self->m_redoStack) XVector_clear_base(self->m_redoStack);
}

static void xpe_afterChange(XPlainTextEdit* self)
{
    int64_t n;
    if (!self) return;
    if (self->m_maxBlockCount > 0 && self->m_lines &&
        (n = XVector_size_base((const XContainer*)self->m_lines)) >
            self->m_maxBlockCount) {
        while (XVector_size_base((const XContainer*)self->m_lines) >
               self->m_maxBlockCount) {
            char** item =
                (char**)XVector_at_base(self->m_lines, 0);
            if (item && *item) XFree_System(*item);
            XVector_remove_base(self->m_lines, 0, 1);
        }
    }
    XAbstractScrollArea_setContentSize((XAbstractScrollArea*)self, 0,
        xpe_lineCount(self) * XPE_LINE_HEIGHT + 4);
    XWidget_update((XWidget*)self);
    xpe_emitChanged(self);
}

/* ==================== 键盘编辑 ==================== */

static void xpe_insertAtCursor(XPlainTextEdit* self, const char* text)
{
    char* line = xpe_lineAt(self, self->m_cursorLine);
    size_t col = (size_t)self->m_cursorCol;
    size_t llen = strlen(line);
    size_t tlen = strlen(text);
    char* merged;
    if (col > llen) col = llen;
    merged = (char*)XMalloc_System(llen + tlen + 1);
    if (!merged) return;
    memcpy(merged, line, col);
    memcpy(merged + col, text, tlen);
    memcpy(merged + col + tlen, line + col, llen - col + 1);
    xpe_setLine(self, self->m_cursorLine, merged);
    self->m_cursorCol += (int)tlen;
    XFree_System(merged);
}

static void xpe_splitLineAtCursor(XPlainTextEdit* self)
{
    char* line = xpe_lineAt(self, self->m_cursorLine);
    int col = self->m_cursorCol;
    char* tail;
    size_t llen;
    if (col < 0) col = 0;
    llen = strlen(line);
    if ((size_t)col > llen) col = (int)llen;
    tail = (char*)XMalloc_System(llen - (size_t)col + 1);
    if (!tail) return;
    strcpy(tail, line + col);
    line[col] = '\0';
    xpe_setLine(self, self->m_cursorLine, line);
    xpe_insertLineAt(self, self->m_cursorLine + 1, tail);
    XFree_System(tail);
    ++self->m_cursorLine;
    self->m_cursorCol = 0;
}

static void xpe_backspace(XPlainTextEdit* self)
{
    char* line = xpe_lineAt(self, self->m_cursorLine);
    int col = self->m_cursorCol;
    size_t len;
    if (col > 0) {
        len = strlen(line);
        if ((size_t)col <= len) {
            memmove(line + col - 1, line + col, len - col + 1);
            --self->m_cursorCol;
        }
        return;
    }
    if (self->m_cursorLine > 0) {
        char* prev = xpe_lineAt(self, self->m_cursorLine - 1);
        int prevLen = (int)strlen(prev);
        char* merged =
            (char*)XMalloc_System((size_t)prevLen + strlen(line) + 1);
        if (!merged) return;
        strcpy(merged, prev);
        strcat(merged, line);
        xpe_setLine(self, self->m_cursorLine - 1, merged);
        XFree_System(merged);
        xpe_removeLineAt(self, self->m_cursorLine);
        --self->m_cursorLine;
        self->m_cursorCol = prevLen;
    }
}

static void xpe_deleteChar(XPlainTextEdit* self)
{
    char* line = xpe_lineAt(self, self->m_cursorLine);
    int col = self->m_cursorCol;
    size_t len = strlen(line);
    if ((size_t)col < len) {
        memmove(line + col, line + col + 1, len - col);
        return;
    }
    if (self->m_cursorLine + 1 < xpe_lineCount(self)) {
        char* next = xpe_lineAt(self, self->m_cursorLine + 1);
        char* merged =
            (char*)XMalloc_System(len + strlen(next) + 1);
        if (!merged) return;
        strcpy(merged, line);
        strcat(merged, next);
        xpe_setLine(self, self->m_cursorLine, merged);
        XFree_System(merged);
        xpe_removeLineAt(self, self->m_cursorLine + 1);
    }
}

static void VX_plainTextEdit_keyPressEvent(XWidget* self, XEvent* event)
{
    XPlainTextEdit* edit = (XPlainTextEdit*)self;
    XKeyEvent* ke;
    int key;
    int lines;
    int lineLen;
    if (!edit || !event ||
        XEvent_type(event) != XEVENT_TYPE_KEY_PRESS) return;
    ke = (XKeyEvent*)event;
    key = ke->m_key;
    if (edit->m_readOnly) {
        XEvent_ignore(event);
        return;
    }
    lines = xpe_lineCount(edit);
    lineLen = (int)strlen(xpe_lineAt(edit, edit->m_cursorLine));
    if (key == (int)XKey_Return || key == (int)XKey_Enter) {
        xpe_pushUndo(edit);
        xpe_splitLineAtCursor(edit);
        xpe_afterChange(edit);
        XEvent_accept(event);
        return;
    }
    if (key == (int)XKey_Backspace) {
        xpe_pushUndo(edit);
        xpe_backspace(edit);
        xpe_afterChange(edit);
        XEvent_accept(event);
        return;
    }
    if (key == (int)XKey_Delete) {
        xpe_pushUndo(edit);
        xpe_deleteChar(edit);
        xpe_afterChange(edit);
        XEvent_accept(event);
        return;
    }
    if (key >= 32 && key <= 126) {
        char ch[2];
        ch[0] = (char)key;
        ch[1] = '\0';
        xpe_pushUndo(edit);
        xpe_insertAtCursor(edit, ch);
        xpe_afterChange(edit);
        XEvent_accept(event);
        return;
    }
    switch (key) {
    case XKey_Left:
        if (edit->m_cursorCol > 0) --edit->m_cursorCol;
        else if (edit->m_cursorLine > 0) {
            --edit->m_cursorLine;
            edit->m_cursorCol =
                (int)strlen(xpe_lineAt(edit, edit->m_cursorLine));
        }
        break;
    case XKey_Right:
        if (edit->m_cursorCol < lineLen) ++edit->m_cursorCol;
        else if (edit->m_cursorLine + 1 < lines) {
            ++edit->m_cursorLine;
            edit->m_cursorCol = 0;
        }
        break;
    case XKey_Up:
        if (edit->m_cursorLine > 0) --edit->m_cursorLine;
        break;
    case XKey_Down:
        if (edit->m_cursorLine + 1 < lines) ++edit->m_cursorLine;
        break;
    case XKey_Home:
        edit->m_cursorCol = 0;
        break;
    case XKey_End:
        edit->m_cursorCol = lineLen;
        break;
    default:
        XEvent_ignore(event);
        return;
    }
    XEvent_accept(event);
}

static void VX_plainTextEdit_paintEvent(XWidget* self, XEvent* event)
{
    XPlainTextEdit* edit = (XPlainTextEdit*)self;
    XPainter painter;
    XImage* image;
    XPoint offset;
    XScrollBar* vsb;
    int scroll = 0;
    int i;
    int count;
    uint32_t text;
    uint32_t placeholder;
    int firstVisible;
    int lastVisible;
    if (!edit || !event) return;
    image = XWidget_paintDevice(self);
    if (!image) return;
    XPainter_init(&painter, NULL);
    if (!XPainter_begin_image(&painter, image)) {
        XPainter_deinit(&painter);
        return;
    }
    offset = XWidget_paintOffset(self);
    if (offset.x != 0 || offset.y != 0)
        XPainter_translate(&painter, (float)offset.x, (float)offset.y);
    vsb = XAbstractScrollArea_verticalScrollBar((XAbstractScrollArea*)self);
    if (vsb) scroll = XScrollBar_value(vsb);
    text = xpe_color(edit, XPaletteColorRole_Text);
    placeholder = xpe_color(edit, XPaletteColorRole_Mid);
    /* 背景：清屏防止父控件渲染透出。 */
    {
        XRect bg = { 0, 0, XWidget_width(self), XWidget_height(self) };
        XPainter_fillRect(&painter, &bg, 0xFFFFFFFFu);
    }
    firstVisible = scroll / XPE_LINE_HEIGHT;
    lastVisible = firstVisible + XWidget_height(self) / XPE_LINE_HEIGHT + 1;
    count = xpe_lineCount(edit);
    {
        XFont font = XWidget_font(self);
        XPainter_setFont(&painter, &font);
    }
    if (count == 0 && edit->m_placeholder[0] != 0) {
        XPainter_drawText(&painter, 4, 14, edit->m_placeholder, placeholder);
    }
    for (i = firstVisible; i < count && i <= lastVisible; ++i) {
        int y = i * XPE_LINE_HEIGHT - scroll;
        XPainter_drawText(&painter, 2, y + 13, xpe_lineAt(edit, i), text);
    }
    XPainter_deinit(&painter);
}

static void VX_plainTextEdit_scrollContentsBy(XAbstractScrollArea* self, int dx, int dy)
{
    (void)dx; (void)dy;
    XWidget_update((XWidget*)self);
}

static void VX_plainTextEdit_deinit(XPlainTextEdit* self)
{
    int64_t i;
    int64_t n;
    if (!self) return;
    if (self->m_lines) {
        n = XVector_size_base((const XContainer*)self->m_lines);
        for (i = 0; i < n; ++i) {
            char** item = (char**)XVector_at_base(self->m_lines, i);
            if (item && *item) XFree_System(*item);
        }
        XVector_delete_base(self->m_lines);
        self->m_lines = NULL;
    }
    if (self->m_undoStack) {
        n = XVector_size_base((const XContainer*)self->m_undoStack);
        for (i = 0; i < n; ++i) {
            char** item = (char**)XVector_at_base(self->m_undoStack, i);
            if (item && *item) XFree_System(*item);
        }
        XVector_delete_base(self->m_undoStack);
        self->m_undoStack = NULL;
    }
    if (self->m_redoStack) {
        n = XVector_size_base((const XContainer*)self->m_redoStack);
        for (i = 0; i < n; ++i) {
            char** item = (char**)XVector_at_base(self->m_redoStack, i);
            if (item && *item) XFree_System(*item);
        }
        XVector_delete_base(self->m_redoStack);
        self->m_redoStack = NULL;
    }
    XClass_Deinit_Parent(XAbstractScrollArea, (XAbstractScrollArea*)self);
}

XVtable* XPlainTextEdit_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XPlainTextEdit)
    XVTABLE_INHERIT_XCLASS(XAbstractScrollArea);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_KeyPressEvent, VX_plainTextEdit_keyPressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VX_plainTextEdit_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractScrollArea_ScrollContentsBy, VX_plainTextEdit_scrollContentsBy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VX_plainTextEdit_deinit);
    return XVTABLE_DEFAULT;
}

void XPlainTextEdit_init(XPlainTextEdit* self, XWidget* parent, XWidgetFlags flags)
{
    XSize hint;
    if (!self) return;
    memset(self, 0, sizeof(*self));
    XAbstractScrollArea_init(&self->m_base, parent, flags);
    XClassSetVtable(self, XPlainTextEdit);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    self->m_lines = XVector_Create(char*);
    self->m_undoStack = XVector_Create(char*);
    self->m_redoStack = XVector_Create(char*);
    self->m_wrapMode = (int)XPlainTextEditMode_WidgetWidth;
    self->m_undoEnabled = true;
    xpe_insertLineAt(self, 0, "");
    XWidget_resize(self, 240, 180);
    hint.width = 240;
    hint.height = 180;
    XWidget_setSizeHint((XWidget*)self, &hint);
}

XPlainTextEdit* XPlainTextEdit_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags)
{
    XPlainTextEdit* self = (XPlainTextEdit*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XPlainTextEdit_init(self, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 公共 API ==================== */

void XPlainTextEdit_setPlainText(XPlainTextEdit* self, const char* utf8)
{
    const char* src;
    size_t start = 0;
    if (!self) return;
    xpe_pushUndo(self);
    src = utf8 ? utf8 : "";
    /* 逐块按换行拆分重建行数组。 */
    if (self->m_lines) {
        int64_t i;
        int64_t n = XVector_size_base((const XContainer*)self->m_lines);
        for (i = 0; i < n; ++i) {
            char** item = (char**)XVector_at_base(self->m_lines, i);
            if (item && *item) XFree_System(*item);
        }
        XVector_clear_base(self->m_lines);
    }
    while (start <= strlen(src)) {
        const char* nl = strchr(src + start, 0x0A);
        size_t end = nl ? (size_t)(nl - (src + start)) : strlen(src + start);
        char* line = (char*)XMalloc_System(end + 1);
        if (!line) break;
        memcpy(line, src + start, end);
        line[end] = 0;
        {
            char* lp = line;
            XVector_push_back_1_base(self->m_lines, &lp);
        }
        if (!nl) break;
        start += end + 1;
    }
    self->m_cursorLine = 0;
    self->m_cursorCol = 0;
    xpe_afterChange(self);
}

char* XPlainTextEdit_toPlainText(const XPlainTextEdit* self)
{
    int i;
    int n = xpe_lineCount(self);
    size_t total = 1;
    char* out;
    size_t o = 0;
    for (i = 0; i < n; ++i)
        total += strlen(xpe_lineAt(self, i)) + 1;
    out = (char*)XMalloc_System(total);
    if (!out) return NULL;
    out[0] = 0;
    for (i = 0; i < n; ++i) {
        const char* line = xpe_lineAt(self, i);
        size_t len = strlen(line);
        memcpy(out + o, line, len);
        o += len;
        if (i + 1 < n) out[o++] = 0x0A;
    }
    out[o] = 0;
    return out;
}

void XPlainTextEdit_appendPlainText(XPlainTextEdit* self, const char* utf8)
{
    char* line = xpe_lineAt(self, xpe_lineCount(self) - 1);
    size_t llen;
    char* merged;
    if (!self || !utf8) return;
    llen = strlen(line);
    merged = (char*)XMalloc_System(llen + strlen(utf8) + 1);
    if (!merged) return;
    strcpy(merged, line);
    strcat(merged, utf8);
    xpe_setLine(self, xpe_lineCount(self) - 1, merged);
    XFree_System(merged);
    xpe_pushUndo(self);
    xpe_insertLineAt(self, xpe_lineCount(self), "");
    self->m_cursorLine = xpe_lineCount(self) - 1;
    self->m_cursorCol = 0;
    xpe_afterChange(self);
}

void XPlainTextEdit_insertPlainText(XPlainTextEdit* self, const char* utf8)
{
    if (!self || !utf8) return;
    xpe_pushUndo(self);
    xpe_insertAtCursor(self, utf8);
    xpe_afterChange(self);
}

void XPlainTextEdit_clear(XPlainTextEdit* self)
{
    if (!self) return;
    XPlainTextEdit_setPlainText(self, "");
}

bool XPlainTextEdit_isReadOnly(const XPlainTextEdit* self)
{
    return self ? self->m_readOnly : false;
}

void XPlainTextEdit_setReadOnly(XPlainTextEdit* self, bool readOnly)
{
    if (!self) return;
    self->m_readOnly = readOnly;
}

int XPlainTextEdit_lineWrapMode(const XPlainTextEdit* self)
{
    return self ? self->m_wrapMode : 0;
}

void XPlainTextEdit_setLineWrapMode(XPlainTextEdit* self, int mode)
{
    if (!self) return;
    self->m_wrapMode = mode;
    XWidget_update((XWidget*)self);
}

int XPlainTextEdit_maximumBlockCount(const XPlainTextEdit* self)
{
    return self ? self->m_maxBlockCount : 0;
}

void XPlainTextEdit_setMaximumBlockCount(XPlainTextEdit* self, int maximum)
{
    if (!self || maximum < 0) return;
    self->m_maxBlockCount = maximum;
    xpe_afterChange(self);
}

void XPlainTextEdit_setPlaceholderText(XPlainTextEdit* self, const char* utf8)
{
    if (!self) return;
    strncpy(self->m_placeholder, utf8 ? utf8 : "",
            sizeof(self->m_placeholder) - 1);
    self->m_placeholder[sizeof(self->m_placeholder) - 1] = 0;
    XWidget_update((XWidget*)self);
}

const char* XPlainTextEdit_placeholderText(const XPlainTextEdit* self)
{
    return self ? self->m_placeholder : "";
}

bool XPlainTextEdit_isUndoRedoEnabled(const XPlainTextEdit* self)
{
    return self ? self->m_undoEnabled : false;
}

void XPlainTextEdit_setUndoRedoEnabled(XPlainTextEdit* self, bool enable)
{
    if (!self) return;
    self->m_undoEnabled = enable;
}

int XPlainTextEdit_cursorLine(const XPlainTextEdit* self)
{
    return self ? self->m_cursorLine : 0;
}

int XPlainTextEdit_cursorColumn(const XPlainTextEdit* self)
{
    return self ? self->m_cursorCol : 0;
}

void XPlainTextEdit_undo(XPlainTextEdit* self)
{
    char* snapshot;
    int64_t n;
    if (!self || !self->m_undoStack) return;
    n = XVector_size_base((const XContainer*)self->m_undoStack);
    if (n == 0) return;
    snapshot = *(char**)XVector_at_base(self->m_undoStack, n - 1);
    XVector_remove_base(self->m_undoStack, n - 1, 1);
    if (self->m_redoStack) {
        char* cur = XPlainTextEdit_toPlainText(self);
        if (cur) XVector_push_back_1_base(self->m_redoStack, &cur);
    }
    XPlainTextEdit_setPlainText(self, snapshot);
    XFree_System(snapshot);
}

void XPlainTextEdit_redo(XPlainTextEdit* self)
{
    char* snapshot;
    int64_t n;
    if (!self || !self->m_redoStack) return;
    n = XVector_size_base((const XContainer*)self->m_redoStack);
    if (n == 0) return;
    snapshot = *(char**)XVector_at_base(self->m_redoStack, n - 1);
    XVector_remove_base(self->m_redoStack, n - 1, 1);
    XPlainTextEdit_setPlainText(self, snapshot);
    XFree_System(snapshot);
}

static char* xpe_selectedAllText(const XPlainTextEdit* self)
{
    return XPlainTextEdit_toPlainText(self);
}

void XPlainTextEdit_copy(XPlainTextEdit* self)
{
    char* text;
    XClipboard* cb;
    XString* str;
    if (!self) return;
    text = xpe_selectedAllText(self);
    if (!text) return;
    cb = XGuiApplication_clipboard();
    if (cb) {
        str = XString_create_utf8(text);
        if (str) {
            XClipboard_setText(cb, str, XClipboardMode_Clipboard);
            XString_delete_base((XClass*)str);
        }
    }
    XFree_System(text);
}

void XPlainTextEdit_cut(XPlainTextEdit* self)
{
    if (!self || self->m_readOnly) return;
    XPlainTextEdit_copy(self);
    XPlainTextEdit_setPlainText(self, "");
}

void XPlainTextEdit_paste(XPlainTextEdit* self)
{
    XClipboard* cb;
    XString* str;
    const char* utf8;
    if (!self || self->m_readOnly) return;
    cb = XGuiApplication_clipboard();
    if (!cb) return;
    str = XClipboard_text(cb, XClipboardMode_Clipboard);
    if (!str) return;
    utf8 = XString_toUtf8(str);
    if (utf8) XPlainTextEdit_insertPlainText(self, utf8);
    XString_delete_base((XClass*)str);
}

void XPlainTextEdit_selectAll(XPlainTextEdit* self)
{
    if (!self) return;
    self->m_selectionActive = true;
    self->m_cursorLine = 0;
    self->m_cursorCol = 0;
}

void XPlainTextEdit_ensureCursorVisible(XPlainTextEdit* self)
{
    XScrollBar* vsb;
    int target;
    if (!self) return;
    vsb = XAbstractScrollArea_verticalScrollBar(
        (XAbstractScrollArea*)self);
    if (!vsb) return;
    target = self->m_cursorLine * XPE_LINE_HEIGHT;
    XScrollBar_setValue(vsb, target);
}

void* XPlainTextEdit_textChanged_signal(XPlainTextEdit* self)
{
    (void)self;
    return (void*)(size_t)XPlainTextEdit_textChanged_signal;
}

void* XPlainTextEdit_cursorPositionChanged_signal(XPlainTextEdit* self)
{
    (void)self;
    return (void*)(size_t)XPlainTextEdit_cursorPositionChanged_signal;
}


void* XPlainTextEdit_blockCountChanged_signal(XPlainTextEdit* self)
{
    (void)self;
    return (void*)(size_t)XPlainTextEdit_blockCountChanged_signal;
}
void* XPlainTextEdit_copyAvailable_signal(XPlainTextEdit* self)
{
    (void)self;
    return (void*)(size_t)XPlainTextEdit_copyAvailable_signal;
}
void* XPlainTextEdit_modificationChanged_signal(XPlainTextEdit* self)
{
    (void)self;
    return (void*)(size_t)XPlainTextEdit_modificationChanged_signal;
}
void* XPlainTextEdit_redoAvailable_signal(XPlainTextEdit* self)
{
    (void)self;
    return (void*)(size_t)XPlainTextEdit_redoAvailable_signal;
}
void* XPlainTextEdit_selectionChanged_signal(XPlainTextEdit* self)
{
    (void)self;
    return (void*)(size_t)XPlainTextEdit_selectionChanged_signal;
}
void* XPlainTextEdit_undoAvailable_signal(XPlainTextEdit* self)
{
    (void)self;
    return (void*)(size_t)XPlainTextEdit_undoAvailable_signal;
}

#endif /* XWIDGET_ON && XABSTRACTSCROLLAREA_ON && XPLAINTEXTEDIT_ON */
