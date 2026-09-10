/**
 * @file       XKeySequenceEdit.c
 * @brief      快捷键捕获控件实现（对标 Qt 6.8 QKeySequenceEdit 全部公共 API）。
 * @details    与同名头文件的公共 API 一一对应；内部实现细节见
 *             头文件 @note 与函数级 Doxygen 注释。
 * @author     XinYueC 团队
 */

#include "XKeySequenceEdit.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XVarList.h"
#include "XPainter.h"
#include "XGuiConfig.h"
#include "XWidget_Protected.h"
#include <stdio.h>
#include <string.h>

#if XWIDGET_ON && XKEYSEQUENCEEDIT_ON

/* ==================== 内部工具 ==================== */

/** @brief 将修饰键掩码转为前缀文本（对标 QKeySequence::toString）。 */
static void xks_modPrefix(XKeyboardModifiers mods, char* out, size_t cap)
{
    out[0] = 0;
    if (mods & XKeyboardModifier_ControlModifier)
        strncat(out, "Ctrl+", cap - strlen(out) - 1);
    if (mods & XKeyboardModifier_ShiftModifier)
        strncat(out, "Shift+", cap - strlen(out) - 1);
    if (mods & XKeyboardModifier_AltModifier)
        strncat(out, "Alt+", cap - strlen(out) - 1);
    if (mods & XKeyboardModifier_MetaModifier)
        strncat(out, "Meta+", cap - strlen(out) - 1);
}

/** @brief 将键码转为可读名称（对标 QKeySequence 的键名映射）。 */
static const char* xks_keyName(int key)
{
    static char buf[8];
    if (key >= 0x20 && key <= 0x7E) {
        buf[0] = (char)key;
        buf[1] = 0;
        return buf;
    }
    switch (key) {
    case 0x01000000: return "Esc";
    case 0x01000001: return "Tab";
    case 0x01000003: return "Backspace";
    case 0x01000004: return "Return";
    case 0x01000007: return "Del";
    case 0x01000020: return "Home";
    case 0x01000022: return "End";
    case 0x01000014: return "Left";
    case 0x01000016: return "Up";
    case 0x01000015: return "Right";
    case 0x01000017: return "Down";
    default: return "?";
    }
}

/** @brief 将整个序列渲染为文本（多组以逗号分隔）。 */
static void xks_toString(const XKeySequence* seq, char* out, size_t cap)
{
    int i;
    out[0] = 0;
    if (!seq || seq->count == 0) return;
    for (i = 0; i < seq->count && (size_t)cap > strlen(out) + 32; ++i) {
        char prefix[32];
        if (i > 0) strncat(out, ", ", cap - strlen(out) - 1);
        xks_modPrefix(seq->combos[i].modifiers, prefix, sizeof(prefix));
        strncat(out, prefix, cap - strlen(out) - 1);
        strncat(out, xks_keyName(seq->combos[i].key), cap - strlen(out) - 1);
    }
}

static void xkse_emitChanged(XKeySequenceEdit* self)
{
    XKeySequence* seqPtr = &self->m_sequence;
    XVarList* args = XVarList_Create(
        XVar(XKeySequence*, seqPtr));
    if (!args) return;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self,
            (size_t)XKeySequenceEdit_keySequenceChanged_signal, args,
            NULL, NULL, XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

static void xkse_emitFinished(XKeySequenceEdit* self)
{
    XVarList* args = XVarList_create(0);
    if (!args) return;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self,
            (size_t)XKeySequenceEdit_editingFinished_signal, args,
            NULL, NULL, XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

/* ==================== 事件处理 ==================== */

static void VX_kse_keyPressEvent(XWidget* self, XEvent* event)
{
    XKeySequenceEdit* edit = (XKeySequenceEdit*)self;
    XKeyEvent* ke;
    XKeyboardModifiers mods;
    int key;
    if (!edit || !event ||
        XEvent_type(event) != XEVENT_TYPE_KEY_PRESS) return;
    ke = (XKeyEvent*)event;
    key = ke->m_key;
    mods = ke->m_modifiers &
           (XKeyboardModifier_ControlModifier |
            XKeyboardModifier_ShiftModifier |
            XKeyboardModifier_AltModifier |
            XKeyboardModifier_MetaModifier);
    /* 纯修饰键按下不记录（等非修饰键完成组合）。 */
    if (key == (int)XKeyboardModifier_ControlModifier ||
        key == (int)XKeyboardModifier_ShiftModifier ||
        key == (int)XKeyboardModifier_AltModifier ||
        key == (int)XKeyboardModifier_MetaModifier) {
        XEvent_accept(event);
        return;
    }
    /* 对标 Qt：Return/Enter 确认序列。 */
    if (key == (int)XKey_Return || key == (int)XKey_Enter) {
        if (edit->m_sequence.count > 0) {
            edit->m_oldSequence = edit->m_sequence;
            xkse_emitFinished(edit);
        }
        XEvent_accept(event);
        return;
    }
    /* 对标 Qt：Esc 清空。 */
    if (key == (int)XKey_Escape && mods == XKeyboardModifier_NoModifier) {
        XKeySequenceEdit_clear(edit);
        XEvent_accept(event);
        return;
    }
    /* 对标 Qt：Backspace 删除最后一组。 */
    if (key == (int)XKey_Backspace && mods == XKeyboardModifier_NoModifier) {
        if (edit->m_sequence.count > 0) {
            --edit->m_sequence.count;
            xkse_emitChanged(edit);
            XWidget_update(self);
        }
        XEvent_accept(event);
        return;
    }
    /* 记录组合：修饰键 + 非修饰键。 */
    if (edit->m_sequence.count < edit->m_maxLength) {
        int idx = edit->m_sequence.count;
        edit->m_sequence.combos[idx].modifiers = mods;
        edit->m_sequence.combos[idx].key = key;
        ++edit->m_sequence.count;
        xkse_emitChanged(edit);
        XWidget_update(self);
    }
    XEvent_accept(event);
}

static void VX_kse_paintEvent(XWidget* self, XEvent* event)
{
    XKeySequenceEdit* edit = (XKeySequenceEdit*)self;
    XPainter painter;
    XImage* image;
    XPoint offset;
    XRect frame;
    char display[256];
    uint32_t textCol;
    int w;
    int h;
    if (!edit || !event) return;
    w = XWidget_width(self);
    h = XWidget_height(self);
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
#if XPALETTE_ON
    {
        XPalette palette = XWidget_palette(self);
        XColor c = XPalette_color(&palette, XPaletteColorGroup_Current,
                                  XPaletteColorRole_WindowText);
        textCol = XColor_rgba(&c);
    }
#else
    textCol = 0xFF000000u;
#endif /* XPALETTE_ON */
    /* 边框（对标 QLineEdit 样式）。 */
    XRect_init(&frame, 0, 0, w, h);
    XPainter_fillRect(&painter, &frame, 0xFFFFFFFFu);
    {
        XRect top; XRect bottom; XRect left; XRect right;
        XRect_init(&top, 0, 0, w, 1);
        XRect_init(&bottom, 0, h - 1, w, 1);
        XRect_init(&left, 0, 0, 1, h);
        XRect_init(&right, w - 1, 0, 1, h);
        XPainter_fillRect(&painter, &top, 0xFF808080u);
        XPainter_fillRect(&painter, &bottom, 0xFF808080u);
        XPainter_fillRect(&painter, &left, 0xFF808080u);
        XPainter_fillRect(&painter, &right, 0xFF808080u);
    }
    xks_toString(&edit->m_sequence, display, sizeof(display));
    if (display[0] != '\0') {
        XFont font = XWidget_font(self);
        XPainter_setFont(&painter, &font);
        XPainter_drawText(&painter, 6, h / 2 + 5, display, textCol);
    }
    XPainter_deinit(&painter);
}

/* ==================== 生命周期与虚表 ==================== */

XVtable* XKeySequenceEdit_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XKeySequenceEdit)
    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_KeyPressEvent, VX_kse_keyPressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VX_kse_paintEvent);
    return XVTABLE_DEFAULT;
}

void XKeySequenceEdit_init(XKeySequenceEdit* self, XWidget* parent,
                           XWidgetFlags flags)
{
    XSize hint;
    if (!self) return;
    memset(self, 0, sizeof(*self));
    XWidget_init(&self->m_base, parent, flags);
    XClassSetVtable(self, XKeySequenceEdit);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    self->m_maxLength = XKEYSEQUENCEEDIT_MAX_LENGTH;
    self->m_clearButton = false;
    self->m_capturing = false;
    XWidget_resize(self, 120, 26);
    hint.width = 120;
    hint.height = 26;
    XWidget_setSizeHint((XWidget*)self, &hint);
}

XKeySequenceEdit* XKeySequenceEdit_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags)
{
    XKeySequenceEdit* self =
        (XKeySequenceEdit*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XKeySequenceEdit_init(self, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 公共 API ==================== */

const XKeySequence* XKeySequenceEdit_keySequence(const XKeySequenceEdit* self)
{
    return self ? &self->m_sequence : NULL;
}

void XKeySequenceEdit_setKeySequence(XKeySequenceEdit* self,
                                     const XKeySequence* sequence)
{
    if (!self || !sequence) return;
    self->m_sequence = *sequence;
    xkse_emitChanged(self);
    XWidget_update((XWidget*)self);
}

void XKeySequenceEdit_clear(XKeySequenceEdit* self)
{
    if (!self || self->m_sequence.count == 0) return;
    memset(&self->m_sequence, 0, sizeof(XKeySequence));
    xkse_emitChanged(self);
    XWidget_update((XWidget*)self);
}

int XKeySequenceEdit_maximumSequenceLength(const XKeySequenceEdit* self)
{
    return self ? self->m_maxLength : 0;
}

void XKeySequenceEdit_setMaximumSequenceLength(XKeySequenceEdit* self, int count)
{
    if (!self || count < 1 || count > XKEYSEQUENCEEDIT_MAX_LENGTH) return;
    self->m_maxLength = count;
}

bool XKeySequenceEdit_isClearButtonEnabled(const XKeySequenceEdit* self)
{
    return self ? self->m_clearButton : false;
}

void XKeySequenceEdit_setClearButtonEnabled(XKeySequenceEdit* self, bool enable)
{
    if (!self) return;
    self->m_clearButton = enable;
    XWidget_update((XWidget*)self);
}

/* ==================== 信号 ==================== */

void* XKeySequenceEdit_keySequenceChanged_signal(XKeySequenceEdit* self,
                                                 const XKeySequence* sequence)
{
    (void)self; (void)sequence;
    return (void*)(size_t)XKeySequenceEdit_keySequenceChanged_signal;
}

void* XKeySequenceEdit_editingFinished_signal(XKeySequenceEdit* self)
{
    (void)self;
    return (void*)(size_t)XKeySequenceEdit_editingFinished_signal;
}

#endif /* XWIDGET_ON && XKEYSEQUENCEEDIT_ON */