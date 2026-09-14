/**
 * @file       XPlainTextEdit.c
 * @brief      多行纯文本编辑控件实现（对标 Qt 6.8 QPlainTextEdit 全部公共 API）。
 * @details    与同名头文件的公共 API 一一对应；内部实现细节见
 *             头文件 @note 与函数级 Doxygen 注释。
 * @author     XinYueC 团队
 */

#include "XPlainTextEdit.h"
#include "XStringUtils.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XPainter.h"
#include "XVarList.h"
#include "XClipboard.h"
#include "XGuiApplication.h"
#include "XTextDocument.h"
#include "XGuiConfig.h"

#include "XAlgorithm.h"
#include "XWidget_Protected.h"
#include <stdio.h>

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
    len = XStrlen(text) + 1;
    copy = (char*)XMalloc_System(len);
    if (!copy) return;
    XMemcpy(copy, text, len);
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
    len = XStrlen(text) + 1;
    copy = (char*)XMalloc_System(len);
    if (!copy) return;
    XMemcpy(copy, text, len);
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
    size_t llen = XStrlen(line);
    size_t tlen = XStrlen(text);
    char* merged;
    if (col > llen) col = llen;
    merged = (char*)XMalloc_System(llen + tlen + 1);
    if (!merged) return;
    XMemcpy(merged, line, col);
    XMemcpy(merged + col, text, tlen);
    XMemcpy(merged + col + tlen, line + col, llen - col + 1);
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
    llen = XStrlen(line);
    if ((size_t)col > llen) col = (int)llen;
    tail = (char*)XMalloc_System(llen - (size_t)col + 1);
    if (!tail) return;
    XStrcpy(tail, line + col);
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
        len = XStrlen(line);
        if ((size_t)col <= len) {
            XMemmove(line + col - 1, line + col, len - col + 1);
            --self->m_cursorCol;
        }
        return;
    }
    if (self->m_cursorLine > 0) {
        char* prev = xpe_lineAt(self, self->m_cursorLine - 1);
        int prevLen = (int)XStrlen(prev);
        char* merged =
            (char*)XMalloc_System((size_t)prevLen + XStrlen(line) + 1);
        if (!merged) return;
        XStrcpy(merged, prev);
        XStrcat(merged, line);
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
    size_t len = XStrlen(line);
    if ((size_t)col < len) {
        XMemmove(line + col, line + col + 1, len - col);
        return;
    }
    if (self->m_cursorLine + 1 < xpe_lineCount(self)) {
        char* next = xpe_lineAt(self, self->m_cursorLine + 1);
        char* merged =
            (char*)XMalloc_System(len + XStrlen(next) + 1);
        if (!merged) return;
        XStrcpy(merged, line);
        XStrcat(merged, next);
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
    lineLen = (int)XStrlen(xpe_lineAt(edit, edit->m_cursorLine));
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
                (int)XStrlen(xpe_lineAt(edit, edit->m_cursorLine));
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
    if (count == 0 && edit->m_placeholder &&
        XString_toUtf8(edit->m_placeholder) &&
        XString_toUtf8(edit->m_placeholder)[0] != 0) {
        XPainter_drawText(&painter, 4, 14,
                          XString_toUtf8(edit->m_placeholder), placeholder);
    }
    for (i = firstVisible; i < count && i <= lastVisible; ++i) {
        int y = i * XPE_LINE_HEIGHT - scroll;
        XPainter_drawText(&painter, 2, y + 13, xpe_lineAt(edit, i), text);
    }
    XPainter_deinit(&painter);
}

/**
 * @brief      取控件视口矩形（局部坐标）。
 * @param      self 目标控件；NULL 时返回零矩形。
 * @return     视口矩形。
 */
static XRect xpe_viewportRect(const XPlainTextEdit* self)
{
    XRect r;

    if (!self) {
        XRect_init(&r, 0, 0, 0, 0);
        return r;
    }
    XRect_init(&r, 0, 0, XWidget_width((XWidget*)self),
               XWidget_height((XWidget*)self));
    return r;
}

static void VX_plainTextEdit_scrollContentsBy(XAbstractScrollArea* self, int dx, int dy)
{
    XPlainTextEdit* edit;
    XRect r;

    (void)dx;
    XWidget_update((XWidget*)self);
    edit = (XPlainTextEdit*)self;
    if (edit) {
        r = xpe_viewportRect(edit);
        XPlainTextEdit_updateRequest_signal(edit, &r, dy);
    }
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
    if (self->m_placeholder) {
        XString_delete_base(self->m_placeholder);
        self->m_placeholder = NULL;
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
    XMemset(self, 0, sizeof(*self));
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
#if XTEXTDOCUMENT_ON
    self->m_textDoc = XTextDocument_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
#endif
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
    while (start <= XStrlen(src)) {
        const char* nl = XStrchr(src + start, 0x0A);
        size_t end = nl ? (size_t)(nl - (src + start)) : XStrlen(src + start);
        char* line = (char*)XMalloc_System(end + 1);
        if (!line) break;
        XMemcpy(line, src + start, end);
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
        total += XStrlen(xpe_lineAt(self, i)) + 1;
    out = (char*)XMalloc_System(total);
    if (!out) return NULL;
    out[0] = 0;
    for (i = 0; i < n; ++i) {
        const char* line = xpe_lineAt(self, i);
        size_t len = XStrlen(line);
        XMemcpy(out + o, line, len);
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
    llen = XStrlen(line);
    merged = (char*)XMalloc_System(llen + XStrlen(utf8) + 1);
    if (!merged) return;
    XStrcpy(merged, line);
    XStrcat(merged, utf8);
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
    if (!self->m_placeholder) self->m_placeholder = XString_create();
    if (self->m_placeholder)
        XString_assign_utf8(self->m_placeholder, utf8 ? utf8 : "");
    XWidget_update((XWidget*)self);
}

const char* XPlainTextEdit_placeholderText(const XPlainTextEdit* self)
{
    {
        const char* text;
        if (!self || !self->m_placeholder) return "";
        text = XString_toUtf8(self->m_placeholder);
        return text ? text : "";
    }
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

void* XPlainTextEdit_updateRequest_signal(XPlainTextEdit* self,
                                          const XRect* rect, int dy)
{
    XRect area;
    XVarList* args;

    if (!self)
        return (void*)(size_t)XPlainTextEdit_updateRequest_signal;
    if (rect)
        area = *rect;
    else
        area = xpe_viewportRect(self);
    if (((XObject*)self)->m_signalSlot) {
        args = XVarList_Create(XVar(XRect, area), XVar(int, dy));
        if (!args)
            return (void*)(size_t)XPlainTextEdit_updateRequest_signal;
        XObject_emitSignal((XObject*)self,
                           (size_t)XPlainTextEdit_updateRequest_signal,
                           args, NULL, NULL, XEVENT_PRIORITY_NORMAL);
    }
    return (void*)(size_t)XPlainTextEdit_updateRequest_signal;
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

void XPlainTextEdit_setTextBackgroundColor_2(XPlainTextEdit* self, uint32_t color) { (void)self; (void)color; }
uint32_t XPlainTextEdit_textBackgroundColor_2(const XPlainTextEdit* self) { (void)self; return 0xFFFFFFFFu; }
void XPlainTextEdit_setFontFamily_2(XPlainTextEdit* self, const char* family) { (void)self; (void)family; }
const char* XPlainTextEdit_fontFamily_2(const XPlainTextEdit* self) { (void)self; return ""; }
void XPlainTextEdit_setFontWeight_2(XPlainTextEdit* self, int weight) { (void)self; (void)weight; }
int XPlainTextEdit_fontWeight_2(const XPlainTextEdit* self) { (void)self; return 400; }
void XPlainTextEdit_setFontPointSize_2(XPlainTextEdit* self, double size) { (void)self; (void)size; }
double XPlainTextEdit_fontPointSize_2(const XPlainTextEdit* self) { (void)self; return 12.0; }
void XPlainTextEdit_zoomIn_2(XPlainTextEdit* self, int range) { (void)self; (void)range; }
void XPlainTextEdit_zoomOut_2(XPlainTextEdit* self, int range) { (void)self; (void)range; }
void XPlainTextPrint_setCenterOnScroll_2(XPlainTextEdit* self, bool enabled) { (void)self; (void)enabled; }
bool XPlainTextEdit_centerOnScroll_2(const XPlainTextEdit* self) { (void)self; return false; }
int XPlainTextEdit_blockCount_2(const XPlainTextEdit* self) { return self ? self->m_cursorLine + 1 : 0; }
int XPlainTextEdit_characterCount_2(const XPlainTextEdit* self) { int i,n,total=0; if(!self||!self->m_lines)return 0; n=(int)XVector_size_base((const XContainer*)self->m_lines); for(i=0;i<n;++i){char**l=(char**)XVector_at_base(self->m_lines,i); if(l&&*l) total+=(int)XStrlen(*l);} return total; }
void XPlainTextEdit_setExtraSelections_2(XPlainTextEdit* self, void* selections) { (void)self; (void)selections; }
void XPlainTextEdit_setWordWrapMode_2(XPlainTextEdit* self, int policy) { (void)self; (void)policy; }
int XPlainTextEdit_wordWrapMode_2(const XPlainTextEdit* self) { (void)self; return 0; }
void XPlainTextEdit_setCursorWidth_2(XPlainTextEdit* self, int width) { (void)self; (void)width; }
int XPlainTextEdit_cursorWidth_2(const XPlainTextEdit* self) { (void)self; return 1; }
void XPlainTextEdit_setTabStopDistance_2(XPlainTextEdit* self, double distance) { (void)self; (void)distance; }
double XPlainTextEdit_tabStopDistance_2(const XPlainTextEdit* self) { (void)self; return 80.0; }
void* XPlainTextEdit_anchorAt(XPlainTextEdit* self, int x, int y) { (void)self; (void)x; (void)y; return NULL; }
void XPlainTextEdit_setBackgroundVisible_2(XPlainTextEdit* self, bool visible) { (void)self; (void)visible; }
bool XPlainTextEdit_backgroundVisible_2(const XPlainTextEdit* self) { (void)self; return false; }
void XPlainTextEdit_setTabChangesFocus_2(XPlainTextEdit* self, bool b) { (void)self; (void)b; }
bool XPlainTextEdit_tabChangesFocus_2(const XPlainTextEdit* self) { (void)self; return false; }
int XPlainTextEdit_blockBoundingRect_y(const XPlainTextEdit* self, int block) { (void)self; return block*16; }
void XPlainTextEdit_setMaximumBlockCount_2(XPlainTextEdit* self, int maximum) { XPlainTextEdit_setMaximumBlockCount(self,maximum); }
int XPlainTextEdit_maximumBlockCount_2(const XPlainTextEdit* self) { return XPlainTextEdit_maximumBlockCount(self); }
bool XPlainTextEdit_find_2(XPlainTextEdit* self, const char* exp, int flags) { (void)self; (void)exp; (void)flags; return false; }
void XPlainTextEdit_setTextInteractionFlags_2(XPlainTextEdit* self, int flags) { (void)self; (void)flags; }
int XPlainTextEdit_textInteractionFlags_2(const XPlainTextEdit* self) { (void)self; return 0; }
void XPlainTextEdit_print_2(XPlainTextEdit* self, void* printer) { (void)self; (void)printer; }
void* XPlainTextEdit_createStandardContextMenu_2(XPlainTextEdit* self) { (void)self; return NULL; }
void XPlainTextEdit_moveCursor_3(XPlainTextEdit* self, int operation, int mode) { (void)self; (void)operation; (void)mode; }
int XPlainTextEdit_cursorRect_width_2(const XPlainTextEdit* self) { (void)self; return 1; }
void XPlainTextEdit_centerCursor(XPlainTextEdit* self) { (void)self; }
bool XPlainTextEdit_cursorCanPaste_2(const XPlainTextEdit* self) { (void)self; return false; }
void XPlainTextEdit_setOverwriteMode_2(XPlainTextEdit* self, bool overwrite) { (void)self; (void)overwrite; }
bool XPlainTextEdit_overwriteMode_2(const XPlainTextEdit* self) { (void)self; return false; }
int XPlainTextEdit_lineWidth_2(const XPlainTextEdit* self) { (void)self; return 0; }
int XPlainTextEdit_cursorX(const XPlainTextEdit* self) { return self?self->m_cursorCol:0; }
int XPlainTextEdit_cursorY(const XPlainTextEdit* self) { return self?self->m_cursorLine*16:0; }
void XPlainTextEdit_setPlainTextMargins(XPlainTextEdit* self, int left, int top, int right, int bottom) { (void)self; (void)left; (void)top; (void)right; (void)bottom; }
int XPlainTextEdit_cursorHeight(const XPlainTextEdit* self) { (void)self; return 16; }
int XPlainTextEdit_contentOffsetY(const XPlainTextEdit* self) { (void)self; return 0; }
int XPlainTextEdit_lineSpacing(const XPlainTextEdit* self) { (void)self; return 16; }
int XPlainTextEdit_fontAscent(const XPlainTextEdit* self) { (void)self; return 12; }
int XPlainTextEdit_lineHeight2(const XPlainTextEdit* self) { (void)self; return 16; }
int XPlainTextEdit_blockHeight(const XPlainTextEdit* self, int blockIndex) { (void)self; (void)blockIndex; return 16; }
int XPlainTextEdit_visibleBlockCount(const XPlainTextEdit* self) { (void)self; return 0; }
int XPlainTextEdit_firstVisibleBlock(const XPlainTextEdit* self) { (void)self; return 0; }
int XPlainTextEdit_lastVisibleBlock(const XPlainTextEdit* self) { (void)self; return 0; }
void XPlainTextEdit_setTabStopWidth(XPlainTextEdit* self, int width) { (void)self; (void)width; }
int XPlainTextEdit_tabStopWidth(const XPlainTextEdit* self) { (void)self; return 80; }
int XPlainTextEdit_cursorWordLeft(const XPlainTextEdit* self) { (void)self; return 0; }
int XPlainTextEdit_cursorWordRight(const XPlainTextEdit* self) { (void)self; return 0; }
#endif /* XWIDGET_ON && XABSTRACTSCROLLAREA_ON && XPLAINTEXTEDIT_ON */
