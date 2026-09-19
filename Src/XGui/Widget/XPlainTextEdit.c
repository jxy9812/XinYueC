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
#include "XVariant.h"
#include "XGuiConfig.h"

#if XMENU_ON
#include "XMenu.h"
#include "XAction.h"
#endif /* XMENU_ON */

#include "XAlgorithm.h"
#include "XWidget_Protected.h"
#include "XWindowEvent.h"
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

/**
 * @brief      发射 selectionChanged 信号（对标 selectionChanged 真发射）。
 * @param      self 目标控件指针；NULL 时无操作。
 * @return     无返回值。
 */
static void xpe_emitSelectionChanged(XPlainTextEdit* self)
{
    XVarList* args;
    if (!self) return;
    args = XVarList_create(0);
    if (!args) return;
    if (((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self,
                           (size_t)XPlainTextEdit_selectionChanged_signal,
                           args, NULL, NULL, XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

/**
 * @brief      置选区激活状态并在翻转时发射 selectionChanged。
 * @details    所有选区状态变化的唯一入口：状态未翻转时不发射，保证
 *             信号语义与 Qt "选区变化才通知"一致。
 * @param      self 目标控件指针；NULL 时无操作。
 * @param      active 新的选区激活状态。
 * @return     无返回值。
 */
static void xpe_setSelectionActive(XPlainTextEdit* self, bool active)
{
    if (!self || self->m_selectionActive == active) return;
    self->m_selectionActive = active;
    xpe_emitSelectionChanged(self);
}

/**
 * @brief      撤销栈是否可用（对标 isUndoAvailable 的内部判定）。
 * @param      self 目标控件指针；可为 NULL。
 * @return     撤销快照栈非空返回 true。
 */
static bool xpe_canUndo(const XPlainTextEdit* self)
{
    return self && self->m_undoStack &&
           XVector_size_base((const XContainer*)self->m_undoStack) > 0;
}

/**
 * @brief      重做栈是否可用（对标 isRedoAvailable 的内部判定）。
 * @param      self 目标控件指针；可为 NULL。
 * @return     重做快照栈非空返回 true。
 */
static bool xpe_canRedo(const XPlainTextEdit* self)
{
    return self && self->m_redoStack &&
           XVector_size_base((const XContainer*)self->m_redoStack) > 0;
}

/**
 * @brief      计算 s 处 UTF-8 序列的字节长度（用于按码点推进列偏移）。
 * @details    按首字节前缀判别 1~4 字节序列；续字节缺失或非法首字节
 *             时按 1 字节推进（与 XPainter 解码口径一致：只推进不
 *             越界）。remain 为行内剩余字节数，钳制序列不越界。
 * @param      s 行内当前字节指针；不为 NULL 且 s[0] 有效。
 * @param      remain 行内自 s 起的剩余字节数（>0）。
 * @return     该码点的字节长度（1~4，钳位后）。
 */
static int xpe_utf8SeqLen(const char* s, int remain)
{
    unsigned char c0 = (unsigned char)s[0];
    int need = 1;
    if (c0 < 0x80u) return 1;
    if ((c0 & 0xE0u) == 0xC0u) need = 2;
    else if ((c0 & 0xF0u) == 0xE0u) need = 3;
    else if ((c0 & 0xF8u) == 0xF0u) need = 4;
    else need = 1; /* 续字节或非法首字节：按 1 字节推进。 */
    if (need > remain) need = remain;
    if (need < 1) need = 1;
    return need;
}

/** @brief 光标前一码点边界（UTF-8 感知；对标 xlineedit_nextBoundary
 *         的反向扫描：从 col-1 起跳过全部续字节）。 */
static int xpe_prevBoundary(const char* s, int col)
{
    int start = col > 0 ? col - 1 : 0;
    while (start > 0 &&
           ((unsigned char)s[start] & 0xC0u) == 0x80u)
        --start;
    return start;
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
            /* 按码点边界回退（对齐 xlineedit_nextBoundary 的反向语义）：
               此前按单字节回退，中文一次只咬掉 1 字节，残缺 UTF-8 序列
               渲染成空白且光标测宽错位（表现为光标"反方向"跳动）。 */
            int prev = xpe_prevBoundary(line, col);
            XMemmove(line + prev, line + col, len - (size_t)col + 1);
            self->m_cursorCol = prev;
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
        /* 按码点边界前进删除：整码点移除，不留残缺续字节
           （残序列渲染成空白、光标测宽错位，用户看到"光标反向"）。 */
        int seq = xpe_utf8SeqLen(line + col, (int)len - col);
        XMemmove(line + col, line + col + seq,
                 len - (size_t)col - (size_t)seq + 1);
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

/** @brief 鼠标按下：聚焦并把点击坐标映射为光标位置
 *         （对标 QPlainTextEdit 的点击定位 + XLineEdit 同款入口）。 */
static void VX_plainTextEdit_mousePressEvent(XWidget* self, XEvent* event)
{
    XPlainTextEdit* edit = (XPlainTextEdit*)self;
    XMouseEvent* me;
    XPoint pos;
    XPoint cur;
    if (!edit || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_PRESS)
        return;
    me = (XMouseEvent*)event;
    pos = XMouseEvent_position(me);
    XWidget_setFocus(self);
    cur = XPlainTextEdit_cursorForPosition(edit, &pos);
    edit->m_cursorLine = cur.x;
    edit->m_cursorCol = cur.y;
    XWidget_update(self);
    XEvent_accept(event);
}

/** @brief 焦点进出：重绘以显示/隐藏光标（对标 XLineEdit 焦点处理）。 */
static void VX_plainTextEdit_focusEvent(XWidget* self, XEvent* event)
{
    if (!self || !event) return;
    if (XEvent_type(event) == XEVENT_TYPE_FOCUS_IN ||
        XEvent_type(event) == XEVENT_TYPE_FOCUS_OUT) {
        XWidget_update(self);
        XEvent_accept(event);
    }
}

/** @brief 输入法事件：提交文本插入光标处（对标 QWidget::inputMethodEvent
 *         的 commitString 处理，与 XLineEdit 同口径；中文输入经此进入）。 */
static void VX_plainTextEdit_inputMethodEvent(XWidget* self, XEvent* event)
{
    XPlainTextEdit* edit = (XPlainTextEdit*)self;
    XInputMethodEvent* ime;
    const XString* commit;
    if (!edit || !event ||
        XEvent_type(event) != XEVENT_TYPE_INPUT_METHOD) return;
    ime = (XInputMethodEvent*)event;
    commit = ime->m_commitString; /* 事件拥有，借用。 */
    if (!commit || edit->m_readOnly) return;
    XPlainTextEdit_insertPlainText(edit, XString_toUtf8(commit));
    XEvent_accept(event);
}

#if XMENU_ON
/** @brief 右键菜单事件：弹出标准编辑菜单（createStandardContextMenu
 *         公开 API 此前未接线；popup 非阻塞 + DeleteOnClose 自删，
 *         与 XLineEdit/Qt 语义一致）。 */
static void VX_plainTextEdit_contextMenuEvent(XWidget* self, XEvent* event)
{
    XPlainTextEdit* edit = (XPlainTextEdit*)self;
    XContextMenuEvent* ctx;
    XMenu* menu;
    XPoint global;
    if (!edit || !event ||
        XEvent_type(event) != XEVENT_TYPE_CONTEXT_MENU) return;
    ctx = (XContextMenuEvent*)event;
    menu = XPlainTextEdit_createStandardContextMenu(edit);
    if (!menu) return;
    global = XContextMenuEvent_globalPosition(ctx);
    XWidget_setAttribute((XWidget*)menu, XWidgetAttribute_DeleteOnClose,
                         true);
    XMenu_popup(menu, &global);
    XEvent_accept(event);
}
#endif /* XMENU_ON */

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
        if (edit->m_cursorCol > 0) {
            const char* line2 = xpe_lineAt(edit, edit->m_cursorLine);
            edit->m_cursorCol = xpe_prevBoundary(line2, edit->m_cursorCol);
        }
        else if (edit->m_cursorLine > 0) {
            --edit->m_cursorLine;
            edit->m_cursorCol =
                (int)XStrlen(xpe_lineAt(edit, edit->m_cursorLine));
        }
        break;
    case XKey_Right: {
        const char* line2 = xpe_lineAt(edit, edit->m_cursorLine);
        int seq = (edit->m_cursorCol < lineLen)
            ? xpe_utf8SeqLen(line2 + edit->m_cursorCol,
                             lineLen - edit->m_cursorCol)
            : 0;
        if (seq > 0) edit->m_cursorCol += seq;
        else if (edit->m_cursorLine + 1 < lines) {
            ++edit->m_cursorLine;
            edit->m_cursorCol = 0;
        }
        break;
    }
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
    image = XWidget_paintImage(self);
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
    /* 绘制范围 = 事件脏区（非 PAINT 入口退化为整控件）：背景填充、
       边框、行绘制全部限幅在脏区内，避免小区域刷新（性能浮层/光标
       闪烁）触发整页文本重绘。 */
    {
        int pw = XWidget_width(self);
        int ph = XWidget_height(self);
        XRect clip;
        if (event && XEvent_type(event) == XEVENT_TYPE_PAINT)
            clip = XPaintEvent_rect((const XPaintEvent*)event);
        else
            XRect_init(&clip, 0, 0, pw, ph);
        if (clip.x < 0) { clip.width += clip.x; clip.x = 0; }
        if (clip.y < 0) { clip.height += clip.y; clip.y = 0; }
        if (clip.x + clip.width > pw) clip.width = pw - clip.x;
        if (clip.y + clip.height > ph) clip.height = ph - clip.y;
        if (clip.width > 0 && clip.height > 0)
        {
            XPainter_setClipRect(&painter, &clip,
                                 XPainterClipOperation_ReplaceClip);
            /* 背景：清屏防止父控件渲染透出。 */
            XPainter_fillRect(&painter, &clip, 0xFFFFFFFFu);
            /* 边框：上/左 dark、下/右 light 的凹陷框
               （对标 QAbstractScrollArea 默认 StyledPanel|Sunken，与
               XLineEdit 手绘回退同款）。 */
            {
                uint32_t dark = xpe_color(edit, XPaletteColorRole_Dark);
                uint32_t light = xpe_color(edit, XPaletteColorRole_Light);
                if (dark == 0u) dark = 0xFF808080u;
                if (light == 0u) light = 0xFFE0E0E0u;
                XPainter_fillRect(&painter, &(XRect){0, 0, pw, 1}, dark);
                XPainter_fillRect(&painter, &(XRect){0, 0, 1, ph}, dark);
                XPainter_fillRect(&painter,
                    &(XRect){0, ph - 1, pw, 1}, light);
                XPainter_fillRect(&painter,
                    &(XRect){pw - 1, 0, 1, ph}, light);
            }
        }
    }
    /* 行范围 = 滚动视口 ∩ 事件脏区：小区域刷新（光标闪烁/局部失效）
       只重绘脏区覆盖的行，而非整个视口。 */
    firstVisible = scroll / XPE_LINE_HEIGHT;
    lastVisible = firstVisible + XWidget_height(self) / XPE_LINE_HEIGHT + 1;
    count = xpe_lineCount(edit);
    if (event && XEvent_type(event) == XEVENT_TYPE_PAINT)
    {
        XRect dclip = XPaintEvent_rect((const XPaintEvent*)event);
        int fromLine = (dclip.y + scroll) / XPE_LINE_HEIGHT;
        int toLine = (dclip.y + dclip.height + scroll) / XPE_LINE_HEIGHT;
        if (fromLine > firstVisible) firstVisible = fromLine;
        if (toLine < lastVisible) lastVisible = toLine;
    }
    {
        XFont font = XWidget_font(self);
        XPainter_setFont(&painter, &font);
        XFont_deinit_base(&font);
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
    /* 光标：焦点内常显（与 XLineEdit 同策略），位置走 cursorRect
       的同一度量口径；此前 m_cursorWidth 字段存在但从不绘制。 */
    if (XWidget_hasFocus(self) && !edit->m_readOnly) {
        XRect cr = XPlainTextEdit_cursorRect(edit);
        if (cr.width > 0 && cr.height > 0)
            XPainter_fillRect(&painter, &cr, text);
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
    if (self->m_documentTitle) {
        XString_delete_base(self->m_documentTitle);
        self->m_documentTitle = NULL;
    }
    if (self->m_extraSelections) {
        /* 条目为纯值结构，无堆内成员，整体销毁即可。 */
        XVector_delete_base((XClass*)self->m_extraSelections);
        self->m_extraSelections = NULL;
    }
    if (self->m_textDoc) {
        XClass_delete_base((XClass*)self->m_textDoc);
        self->m_textDoc = NULL;
    }
    XClass_Deinit_Parent(XAbstractScrollArea, (XAbstractScrollArea*)self);
}

XVtable* XPlainTextEdit_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XPlainTextEdit)
    XVTABLE_INHERIT_XCLASS(XAbstractScrollArea);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_KeyPressEvent, VX_plainTextEdit_keyPressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VX_plainTextEdit_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MousePressEvent, VX_plainTextEdit_mousePressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_FocusInEvent, VX_plainTextEdit_focusEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_FocusOutEvent, VX_plainTextEdit_focusEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_InputMethodEvent, VX_plainTextEdit_inputMethodEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ContextMenuEvent, VX_plainTextEdit_contextMenuEvent);
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
    self->m_backgroundVisible = true;
    self->m_cursorWidth = 1;
    self->m_centerCursor = false;
    self->m_centerOnScroll = false;
    self->m_tabChangesFocus = false;
    self->m_tabStopDistance = 40;
    self->m_overwriteMode = false;
    self->m_wordWrapMode = 0;
    self->m_documentTitle = XString_create();
    self->m_textInteractionFlags = 0;
    self->m_modified = false;
    self->m_charFormat = 0;
    self->m_extraSelections = XVector_Create(XPlainTextEditExtraSelection);
    /* 对标 QPlainTextEditPrivate::init 的 StrongFocus（qplaintextedit.cpp:790）：
       无焦点策略时键盘事件永远到不了控件，编辑功能名存实亡。 */
    XWidget_setFocusPolicy((XWidget*)self, XWidgetFocusPolicy_StrongFocus);
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

/** @brief 重建行数组并复位光标（撤销快照压栈由调用方决定）。 */
static void xpe_applyTextNoUndo(XPlainTextEdit* self, const char* utf8)
{
    const char* src;
    size_t start = 0;
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
    /* 文本整体重建使既有选区失效（对标 Qt 文档重置路径）。 */
    xpe_setSelectionActive(self, false);
    xpe_afterChange(self);
}

void XPlainTextEdit_setPlainText(XPlainTextEdit* self, const char* utf8)
{
    if (!self) return;
    xpe_pushUndo(self);
    xpe_applyTextNoUndo(self, utf8);
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

/* ==================== 光标几何与查找（对标 QPlainTextEdit public API） ==== */

/**
 * @brief      行内自 from 字节偏移起查找 text 首次出现。
 * @param      line 行文本（NUL 结尾）；不为 NULL。
 * @param      text 查找串（非空）；不为 NULL。
 * @param      from 起始字节偏移；负值按 0 处理，越界返回 -1。
 * @return     命中起始字节偏移；未命中返回 -1。
 */
static int xpe_findFirstInLine(const char* line, const char* text, int from)
{
    const char* hit;
    int len = (int)XStrlen(line);
    if (from < 0) from = 0;
    if (from > len) return -1;
    hit = XStrstr(line + from, text);
    return hit ? (int)(hit - line) : -1;
}

/**
 * @brief      行内 [0, limit) 字节范围内查找 text 最后一次出现。
 * @param      line 行文本（NUL 结尾）；不为 NULL。
 * @param      text 查找串（非空）；不为 NULL。
 * @param      limit 排他上界字节偏移；超出行长按行长钳位，负值按 0。
 * @return     命中起始字节偏移；未命中返回 -1。
 */
static int xpe_findLastInLine(const char* line, const char* text, int limit)
{
    const char* hit;
    int last = -1;
    int from = 0;
    int len = (int)XStrlen(line);
    if (limit > len) limit = len;
    if (limit < 0) limit = 0;
    while (from <= limit) {
        hit = XStrstr(line + from, text);
        if (!hit) break;
        if ((int)(hit - line) >= limit) break;
        last = (int)(hit - line);
        from = last + 1;
    }
    return last;
}

XRect XPlainTextEdit_cursorRect(const XPlainTextEdit* self)
{
    XRect rect;
    XFont font;
    XScrollBar* vsb;
    const char* line;
    int scroll = 0;
    int col;
    int lineLen;
    if (!self) {
        XRect_init(&rect, 0, 0, 0, 0);
        return rect;
    }
    /* 与 paintEvent 同一口径：控件字体测量光标前列宽（字节偏移），
       行高 XPE_LINE_HEIGHT、行左留白 2px，Y 随垂直滚动条取值偏移；
       输出为控件局部坐标。 */
    font = XWidget_fontMetrics((const XWidget*)self);
    vsb = XAbstractScrollArea_verticalScrollBar((XAbstractScrollArea*)self);
    if (vsb) scroll = XScrollBar_value(vsb);
    line = xpe_lineAt(self, self->m_cursorLine);
    lineLen = (int)XStrlen(line);
    col = self->m_cursorCol;
    if (col < 0) col = 0;
    if (col > lineLen) col = lineLen;
    XRect_init(&rect,
               2 + XPainter_textWidthRange(&font, line, 0, col),
               self->m_cursorLine * XPE_LINE_HEIGHT - scroll,
               self->m_cursorWidth > 0 ? self->m_cursorWidth : 1,
               XPE_LINE_HEIGHT);
    return rect;
}

XString* XPlainTextEdit_anchorAt(const XPlainTextEdit* self,
                                 const XPoint* pos)
{
    /* 平铺纯文本无锚点：恒返回 0 长度字符串，仅为对标 Qt 接口存在性。 */
    (void)self;
    (void)pos;
    return XString_create_utf8("");
}

bool XPlainTextEdit_find(XPlainTextEdit* self, const char* text, int flags)
{
    int lineCount;
    int startLine;
    int startCol;
    int matchLen;
    int i;
    if (!self || !text || text[0] == '\0') return false;
    lineCount = xpe_lineCount(self);
    if (lineCount <= 0) return false;
    startLine = self->m_cursorLine;
    startCol = self->m_cursorCol;
    if (startLine < 0) {
        startLine = 0;
        startCol = 0;
    }
    if (startLine >= lineCount) {
        startLine = lineCount - 1;
        startCol = 0;
    }
    matchLen = (int)XStrlen(text);
    if ((flags & 1) == 0) {
        /* 向前：当前行自光标列起，其后各行自行首。 */
        for (i = startLine; i < lineCount; ++i) {
            int from = (i == startLine) ? startCol : 0;
            int pos = xpe_findFirstInLine(xpe_lineAt(self, i), text, from);
            if (pos >= 0) {
                self->m_cursorLine = i;
                self->m_cursorCol = pos + matchLen;
                XWidget_update((XWidget*)self);
                return true;
            }
        }
    } else {
        /* 向后：当前行限光标列之前，其上各行取行内最后一次出现。 */
        for (i = startLine; i >= 0; --i) {
            int limit = (i == startLine)
                            ? startCol
                            : (int)XStrlen(xpe_lineAt(self, i));
            int pos = xpe_findLastInLine(xpe_lineAt(self, i), text, limit);
            if (pos >= 0) {
                self->m_cursorLine = i;
                self->m_cursorCol = pos;
                XWidget_update((XWidget*)self);
                return true;
            }
        }
    }
    return false;
}

void XPlainTextEdit_setTextCursor(XPlainTextEdit* self, int line, int col)
{
    int lineCount;
    int lineLen;
    if (!self) return;
    lineCount = xpe_lineCount(self);
    if (lineCount <= 0) return;
    if (line < 0) line = 0;
    if (line >= lineCount) line = lineCount - 1;
    lineLen = (int)XStrlen(xpe_lineAt(self, line));
    if (col < 0) col = 0;
    if (col > lineLen) col = lineLen;
    self->m_cursorLine = line;
    self->m_cursorCol = col;
    XWidget_update((XWidget*)self);
}

int XPlainTextEdit_textCursorLine(const XPlainTextEdit* self)
{
    return XPlainTextEdit_cursorLine(self);
}

int XPlainTextEdit_textCursorColumn(const XPlainTextEdit* self)
{
    return XPlainTextEdit_cursorColumn(self);
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
    /* 恢复走免撤销路径:否则 setPlainText 又压快照,撤销栈永不清空。 */
    xpe_applyTextNoUndo(self, snapshot);
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
    /* 恢复走免撤销路径(同 undo)。 */
    xpe_applyTextNoUndo(self, snapshot);
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
    self->m_cursorLine = 0;
    self->m_cursorCol = 0;
    /* 置位并按需发射 selectionChanged（选区变化的真发射点）。 */
    xpe_setSelectionActive(self, true);
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






















int XPlainTextEdit_characterCount_2(const XPlainTextEdit* self) { int i,n,total=0; if(!self||!self->m_lines)return 0; n=(int)XVector_size_base((const XContainer*)self->m_lines); for(i=0;i<n;++i){char**l=(char**)XVector_at_base(self->m_lines,i); if(l&&*l) total+=(int)XStrlen(*l);} return total; }












































/* ==================== 状态族与信号（2026-09-18 批次） ==================== */

static void xpe_emitBool(XPlainTextEdit* self, size_t signal, bool value)
{
    XVarList* arguments = XVarList_Create(XVar(bool, value));
    if (!arguments) return;
    if (((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, arguments, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(arguments);
    }
}

int XPlainTextEdit_blockCount(const XPlainTextEdit* self)
{
    if (!self || !self->m_lines) return 0;
    return (int)XVector_size_base((const XContainer*)self->m_lines);
}

bool XPlainTextEdit_canPaste(const XPlainTextEdit* self)
{ return self ? !self->m_readOnly : false; }

void XPlainTextEdit_setCursorWidth(XPlainTextEdit* self, int width)
{ if (self && width > 0) { self->m_cursorWidth = width; XWidget_update((XWidget*)self); } }

int XPlainTextEdit_cursorWidth(const XPlainTextEdit* self)
{ return self ? self->m_cursorWidth : 1; }

void XPlainTextEdit_setCenterCursor(XPlainTextEdit* self, bool center)
{ if (self) self->m_centerCursor = center; }

bool XPlainTextEdit_centerCursor(const XPlainTextEdit* self)
{ return self ? self->m_centerCursor : false; }

void XPlainTextEdit_setCenterOnScroll(XPlainTextEdit* self, bool on)
{ if (self) self->m_centerOnScroll = on; }

bool XPlainTextEdit_centerOnScroll(const XPlainTextEdit* self)
{ return self ? self->m_centerOnScroll : false; }

void XPlainTextEdit_setBackgroundVisible(XPlainTextEdit* self, bool visible)
{ if (self) { self->m_backgroundVisible = visible; XWidget_update((XWidget*)self); } }

bool XPlainTextEdit_backgroundVisible(const XPlainTextEdit* self)
{ return self ? self->m_backgroundVisible : true; }

void XPlainTextEdit_setTabChangesFocus(XPlainTextEdit* self, bool change)
{ if (self) self->m_tabChangesFocus = change; }

bool XPlainTextEdit_tabChangesFocus(const XPlainTextEdit* self)
{ return self ? self->m_tabChangesFocus : false; }

void XPlainTextEdit_setTabStopDistance(XPlainTextEdit* self, int distance)
{ if (self && distance > 0) self->m_tabStopDistance = distance; }

int XPlainTextEdit_tabStopDistance(const XPlainTextEdit* self)
{ return self ? self->m_tabStopDistance : 40; }

void XPlainTextEdit_setOverwriteMode(XPlainTextEdit* self, bool overwrite)
{ if (self) self->m_overwriteMode = overwrite; }

bool XPlainTextEdit_overwriteMode(const XPlainTextEdit* self)
{ return self ? self->m_overwriteMode : false; }

void XPlainTextEdit_setWordWrapMode(XPlainTextEdit* self, int mode)
{ if (self) { self->m_wordWrapMode = mode; XWidget_update((XWidget*)self); } }

int XPlainTextEdit_wordWrapMode(const XPlainTextEdit* self)
{ return self ? self->m_wordWrapMode : 0; }

void XPlainTextEdit_setTextInteractionFlags(XPlainTextEdit* self, int flags)
{ if (self) self->m_textInteractionFlags = flags; }

int XPlainTextEdit_textInteractionFlags(const XPlainTextEdit* self)
{ return self ? self->m_textInteractionFlags : 0; }

void XPlainTextEdit_setDocumentTitle(XPlainTextEdit* self, const XString* title)
{
    if (!self) return;
    if (!self->m_documentTitle) {
        self->m_documentTitle = XString_create();
        if (!self->m_documentTitle) return;
    }
    if (title) XString_assign(self->m_documentTitle, title);
    else XString_assign_utf8(self->m_documentTitle, "");
}

void XPlainTextEdit_setDocumentTitle_2(XPlainTextEdit* self, const char* utf8)
{
    if (!self) return;
    if (!self->m_documentTitle) self->m_documentTitle = XString_create();
    if (!self->m_documentTitle) return;
    XString_assign_utf8(self->m_documentTitle, utf8 ? utf8 : "");
}

XString* XPlainTextEdit_documentTitle(const XPlainTextEdit* self)
{
    XString* out = XString_create();
    if (!out) return NULL;
    if (self && self->m_documentTitle) XString_assign(out, self->m_documentTitle);
    return out;
}

void XPlainTextEdit_moveCursor(XPlainTextEdit* self, int operation, int mode)
{
    (void)mode;
    if (!self || !self->m_lines) return;
    switch (operation) {
    case 1:
        if (self->m_cursorCol > 0) --self->m_cursorCol;
        else if (self->m_cursorLine > 0) --self->m_cursorLine;
        break;
    case 2: ++self->m_cursorCol; break;
    case 3: if (self->m_cursorLine > 0) --self->m_cursorLine; break;
    case 4:
        if (self->m_cursorLine < XPlainTextEdit_blockCount(self) - 1)
            ++self->m_cursorLine;
        break;
    case 5: self->m_cursorLine = 0; self->m_cursorCol = 0; break;
    case 6:
        self->m_cursorLine = XPlainTextEdit_blockCount(self) - 1;
        self->m_cursorCol = 0;
        break;
    default:
        return;
    }
    XWidget_update((XWidget*)self);
}

void XPlainTextEdit_appendHtml(XPlainTextEdit* self, const char* html)
{
    char buf[1024];
    size_t i = 0, o = 0;
    int tag = 0;
    if (!self || !html) return;
    for (; html[i] != '\0' && o < sizeof(buf) - 1; ++i) {
        if (html[i] == '<') { tag = 1; continue; }
        if (html[i] == '>') { tag = 0; continue; }
        if (!tag) buf[o++] = html[i];
    }
    buf[o] = '\0';
    XPlainTextEdit_appendPlainText(self, buf);
}

void* XPlainTextEdit_undoAvailable_signal(XPlainTextEdit* self, bool available)
{ (void)available; return (void*)(size_t)XPlainTextEdit_undoAvailable_signal; }

void* XPlainTextEdit_redoAvailable_signal(XPlainTextEdit* self, bool available)
{ (void)available; return (void*)(size_t)XPlainTextEdit_redoAvailable_signal; }

void* XPlainTextEdit_copyAvailable_signal(XPlainTextEdit* self, bool available)
{ (void)available; return (void*)(size_t)XPlainTextEdit_copyAvailable_signal; }

void* XPlainTextEdit_modificationChanged_signal(XPlainTextEdit* self, bool changed)
{ (void)changed; return (void*)(size_t)XPlainTextEdit_modificationChanged_signal; }

void* XPlainTextEdit_blockCountChanged_signal(XPlainTextEdit* self, int newCount)
{ (void)newCount; return (void*)(size_t)XPlainTextEdit_blockCountChanged_signal; }

/* ==================== 光标/格式/文档/额外选区/缩放（2026-09-17 批次） ==== */

/**
 * @brief      返回坐标 pos 处的光标位置（对标 QPlainTextEdit::cursorForPosition）。
 * @details    行反查：内容 Y = 行号 x XPE_LINE_HEIGHT，视口 Y = 内容 Y -
 *             垂直滚动值，故行 = (pos->y + scroll) / 行高，钳位到有效
 *             行区间。列反查：X 起点为行左留白 2px，按控件字体逐码点
 *             累加 XPainter_textWidthRange 字形宽，pos->x 落点之前的
 *             码点边界即为列（UTF-8 字节偏移），超出行宽钳位到行尾。
 *             与 cursorRect/paintEvent 同一度量口径。
 * @param      self 目标控件指针；NULL 时返回 {0,0}。
 * @param      pos 视口局部坐标点；NULL 时返回 {0,0}。
 * @return     x = 行号（0 起），y = 列（行内 UTF-8 字节偏移）。
 */
XPoint XPlainTextEdit_cursorForPosition(const XPlainTextEdit* self,
                                        const XPoint* pos)
{
    XPoint result;
    XScrollBar* vsb;
    XFont font;
    const char* text;
    int scroll = 0;
    int lineCount;
    int line;
    int lineLen;
    int col;
    int x;
    result.x = 0;
    result.y = 0;
    if (!self || !pos) return result;
    lineCount = xpe_lineCount(self);
    if (lineCount <= 0) return result;
    vsb = XAbstractScrollArea_verticalScrollBar((XAbstractScrollArea*)self);
    if (vsb) scroll = XScrollBar_value(vsb);
    /* 行反查：视口 Y 加回滚动值得内容 Y，再除行高并钳位。 */
    line = (pos->y + scroll) / XPE_LINE_HEIGHT;
    if (line < 0) line = 0;
    if (line >= lineCount) line = lineCount - 1;
    text = xpe_lineAt(self, line);
    lineLen = (int)XStrlen(text);
    col = 0;
    x = pos->x - 2; /* 扣除与绘制一致的行左留白。 */
    if (x > 0 && lineLen > 0) {
        int off = 0;
        font = XWidget_fontMetrics((const XWidget*)self);
        while (off < lineLen) {
            int seq = xpe_utf8SeqLen(text + off, lineLen - off);
            int w = XPainter_textWidthRange(&font, text, off, off + seq);
            if (w < 0) w = 0;
            if (x < w) break; /* 落点在本码点宽度内：停在边界前。 */
            x -= w;
            off += seq;
        }
        col = off;
        XFont_deinit_base((XClass*)&font);
    }
    result.x = line;
    result.y = col;
    return result;
}

#if XMENU_ON
/* ==================== 标准右键菜单（对标 QPlainTextEdit::
   createStandardContextMenu，参照 XLineEdit 同名实现） ============== */

/** @brief 菜单动作槽：撤销。 */
static void xpe_menuUndoSlot(XObject* receiver, XVarList* args)
{
    (void)args;
    XPlainTextEdit_undo((XPlainTextEdit*)receiver);
}

/** @brief 菜单动作槽：重做。 */
static void xpe_menuRedoSlot(XObject* receiver, XVarList* args)
{
    (void)args;
    XPlainTextEdit_redo((XPlainTextEdit*)receiver);
}

/** @brief 菜单动作槽：剪切。 */
static void xpe_menuCutSlot(XObject* receiver, XVarList* args)
{
    (void)args;
    XPlainTextEdit_cut((XPlainTextEdit*)receiver);
}

/** @brief 菜单动作槽：复制。 */
static void xpe_menuCopySlot(XObject* receiver, XVarList* args)
{
    (void)args;
    XPlainTextEdit_copy((XPlainTextEdit*)receiver);
}

/** @brief 菜单动作槽：粘贴。 */
static void xpe_menuPasteSlot(XObject* receiver, XVarList* args)
{
    (void)args;
    XPlainTextEdit_paste((XPlainTextEdit*)receiver);
}

/** @brief 菜单动作槽：全选。 */
static void xpe_menuSelectAllSlot(XObject* receiver, XVarList* args)
{
    (void)args;
    XPlainTextEdit_selectAll((XPlainTextEdit*)receiver);
}

/**
 * @brief      添加菜单动作并连接触发槽（返回动作便于设置启用态）。
 * @param      menu 目标菜单；不为 NULL。
 * @param      utf8 动作文本（UTF-8）。
 * @param      slot 触发槽；可为 NULL（不连接）。
 * @param      edit 动作接收者控件；不为 NULL。
 * @return     新建动作指针；创建失败返回 NULL。
 */
static XAction* xpe_addMenuAction(XMenu* menu, const char* utf8,
                                  XSlotFunc1 slot, XPlainTextEdit* edit)
{
    XAction* action = XMenu_addAction_2(menu, utf8);
    if (action && slot)
        XObject_connect_1((XObject*)action,
                          XSignal(XAction_triggered_signal),
                          (XObject*)edit, slot, XConnectionType_Direct);
    return action;
}

XMenu* XPlainTextEdit_createStandardContextMenu(XPlainTextEdit* self)
{
    XMenu* menu;
    XAction* action;
    XString* name;
    XClipboard* cb;
    XString* clip;
    const char* clipText;
    bool readOnly;
    bool hasSel;
    bool hasText;
    bool hasClip;
    if (!self) return NULL;
    menu = XMenu_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL, NULL);
    if (!menu) return NULL;
    /* 对标 Qt：qt_edit_menu 对象名供测试与样式查找。 */
    name = XString_create_utf8("qt_edit_menu");
    if (name) {
        XObject_setObjectName((XObject*)menu, name);
        XString_delete_base((XClass*)name);
    }
    readOnly = self->m_readOnly;
    hasSel = self->m_selectionActive;
    {
        int i;
        int n = xpe_lineCount(self);
        hasText = false;
        for (i = 0; i < n; ++i) {
            if (XStrlen(xpe_lineAt(self, i)) > 0) {
                hasText = true;
                break;
            }
        }
    }
    cb = XGuiApplication_clipboard();
    clip = cb ? XClipboard_text(cb, XClipboardMode_Clipboard) : NULL;
    clipText = clip ? XString_toUtf8(clip) : NULL;
    hasClip = clipText && clipText[0] != '\0';
    if (clip) XString_delete_base((XClass*)clip);

    if (!readOnly) {
        action = xpe_addMenuAction(menu, "撤销(&U)", xpe_menuUndoSlot, self);
        XAction_setEnabled(action, xpe_canUndo(self));
        action = xpe_addMenuAction(menu, "重做(&R)", xpe_menuRedoSlot, self);
        XAction_setEnabled(action, xpe_canRedo(self));
        XMenu_addSeparator(menu);
        action = xpe_addMenuAction(menu, "剪切(&T)", xpe_menuCutSlot, self);
        XAction_setEnabled(action, hasSel);
    }
    action = xpe_addMenuAction(menu, "复制(&C)", xpe_menuCopySlot, self);
    XAction_setEnabled(action, hasSel);
    if (!readOnly) {
        action = xpe_addMenuAction(menu, "粘贴(&P)", xpe_menuPasteSlot, self);
        XAction_setEnabled(action, hasClip);
    }
    XMenu_addSeparator(menu);
    action = xpe_addMenuAction(menu, "全选(&A)", xpe_menuSelectAllSlot, self);
    XAction_setEnabled(action, hasText && !hasSel);
    return menu;
}
#endif /* XMENU_ON */

int XPlainTextEdit_currentCharFormat(const XPlainTextEdit* self)
{ return self ? self->m_charFormat : 0; }

void XPlainTextEdit_setCurrentCharFormat(XPlainTextEdit* self, int format)
{ if (self) self->m_charFormat = format; }

void XPlainTextEdit_mergeCurrentCharFormat(XPlainTextEdit* self, int format)
{
    /* @note 对标简化：Qt 按属性粒度合并（仅覆盖显式置位属性），此处
       以位值按位或覆盖承载。 */
    if (self) self->m_charFormat |= format;
}

#if XTEXTDOCUMENT_ON
XTextDocument* XPlainTextEdit_document(const XPlainTextEdit* self)
{
    /* 借用语义：所有权仍归控件（析构统一释放）。 */
    return self ? self->m_textDoc : NULL;
}

void XPlainTextEdit_setDocument(XPlainTextEdit* self, XTextDocument* doc)
{
    char* text;
    if (!self || self->m_textDoc == doc) return;
    if (self->m_textDoc)
        XClass_delete_base((XClass*)self->m_textDoc);
    if (doc) {
        /* 接管所有权并镜像文档纯文本到平铺行存储。 */
        self->m_textDoc = doc;
        text = XTextDocument_toPlainText(doc);
        xpe_applyTextNoUndo(self, text ? text : "");
        if (text) XFree_System(text);
    } else {
        /* 对标 Qt：传 NULL 回退为新建空文档。 */
        self->m_textDoc = XTextDocument_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
        xpe_applyTextNoUndo(self, "");
    }
}
#endif /* XTEXTDOCUMENT_ON */

int XPlainTextEdit_extraSelections(const XPlainTextEdit* self,
                                   const XPlainTextEditExtraSelection** selections)
{
    int count;
    if (selections) *selections = NULL;
    if (!self || !self->m_extraSelections) return 0;
    count = (int)XVector_size_base(
        (const XContainer*)self->m_extraSelections);
    if (count > 0 && selections) {
        const XPlainTextEditExtraSelection* items =
            (const XPlainTextEditExtraSelection*)XVector_at_base(
                self->m_extraSelections, 0);
        *selections = items;
    }
    return count;
}

void XPlainTextEdit_setExtraSelections(XPlainTextEdit* self,
                                       const XPlainTextEditExtraSelection* selections,
                                       int count)
{
    int i;
    if (!self || !self->m_extraSelections) return;
    if (count < 0) count = 0;
    XVector_clear_base((XContainer*)self->m_extraSelections);
    for (i = 0; selections && i < count; ++i) {
        XPlainTextEditExtraSelection entry = selections[i];
        XVector_push_back_1_base(self->m_extraSelections, &entry);
    }
    /* @note 绘制联动（paintEvent 高亮渲染）暂未接入，此处仅承载并
       请求重绘。 */
    XWidget_update((XWidget*)self);
}

XVariant* XPlainTextEdit_loadResource(XPlainTextEdit* self, int type,
                                      const char* name)
{
    /* 平铺模型无资源存储：对标 Qt 默认实现"未找到返回无效 QVariant"，
       恒返回 NULL。type/name 仅保持签名一致。 */
    (void)self;
    (void)type;
    (void)name;
    return NULL;
}

XPoint XPlainTextEdit_textCursor(const XPlainTextEdit* self)
{
    XPoint result;
    result.x = self ? self->m_cursorLine : 0;
    result.y = self ? self->m_cursorCol : 0;
    return result;
}

/**
 * @brief      zoomIn/zoomOut 公共实现：按带符号增量调整字体大小。
 * @details    点大小与像素字号同步增减：点阵渲染路径按像素字号整倍
 *             缩放，仅改点大小不产生视觉变化，故两者同步钳位下限 1。
 * @param      self 目标控件指针；NULL 或 delta 为 0 时无操作。
 * @param      delta 带符号增量（点数/像素数）。
 * @return     无返回值。
 */
static void xpe_zoomApply(XPlainTextEdit* self, int delta)
{
    XFont font;
    int ps;
    int px;
    if (!self || delta == 0) return;
    font = XWidget_font((XWidget*)self);
    ps = XFont_pointSize(&font);
    if (ps <= 0) ps = (int)XFONT_DEFAULT_POINT_SIZE;
    ps += delta;
    if (ps < 1) ps = 1;
    XFont_setPointSize(&font, ps);
    px = XFont_bitmapPixelSize(&font, XPE_LINE_HEIGHT);
    px += delta;
    if (px < 1) px = 1;
    XFont_setPixelSize(&font, px);
    XWidget_setFont((XWidget*)self, &font);
    XFont_deinit_base((XClass*)&font);
}

void XPlainTextEdit_zoomIn(XPlainTextEdit* self, int range)
{
    /* 对标 Qt：range 可为负（zoomIn 负值即缩小），仅钳位避免无操作。 */
    xpe_zoomApply(self, range);
}

void XPlainTextEdit_zoomOut(XPlainTextEdit* self, int range)
{
    xpe_zoomApply(self, -range);
}

/* ==================== 选区查询（2026-09-17 补齐批次） ==================== */

bool XPlainTextEdit_hasSelectedText(const XPlainTextEdit* self)
{
    return self ? self->m_selectionActive : false;
}

char* XPlainTextEdit_selectedText(const XPlainTextEdit* self)
{
    const char* line;
    int col;
    int lineLen;
    size_t len;
    char* out;
    /* 平铺模型简化：选区激活时，选中文本为当前行起点至光标的片段。 */
    if (!self || !self->m_selectionActive) return NULL;
    line = xpe_lineAt(self, self->m_cursorLine);
    lineLen = (int)XStrlen(line);
    col = self->m_cursorCol;
    if (col < 0) col = 0;
    if (col > lineLen) col = lineLen;
    len = (size_t)col;
    out = (char*)XMalloc_System(len + 1);
    if (!out) return NULL;
    XMemcpy(out, line, len);
    out[len] = '\0';
    return out;
}

void* XPlainTextEdit_selectionChanged_signal(XPlainTextEdit* self)
{
    (void)self;
    return (void*)(size_t)XPlainTextEdit_selectionChanged_signal;
}

#endif /* XWIDGET_ON && XABSTRACTSCROLLAREA_ON && XPLAINTEXTEDIT_ON */
