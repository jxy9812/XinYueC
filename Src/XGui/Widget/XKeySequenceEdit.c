/**
 * @file       XKeySequenceEdit.c
 * @brief      快捷键捕获控件实现（对标 Qt 6.8 QKeySequenceEdit 全部公共 API）。
 * @details    与同名头文件的公共 API 一一对应；内部实现细节见
 *             头文件 @note 与函数级 Doxygen 注释。
 * @author     XinYueC 团队
 */

#include "XKeySequenceEdit.h"
#include "XStringUtils.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XVarList.h"
#include "XPainter.h"
#include "XGuiConfig.h"

#include "XAlgorithm.h"
#include "XWidget_Protected.h"

#if XWIDGET_ON && XKEYSEQUENCEEDIT_ON

/* ==================== 内部工具 ==================== */

/** @brief 将修饰键掩码转为前缀文本（对标 QKeySequence::toString）。 */
static void xks_modPrefix(XKeyboardModifiers mods, char* out, size_t cap)
{
    out[0] = 0;
    if (mods & XKeyboardModifier_ControlModifier)
        XStrncat(out, "Ctrl+", cap - XStrlen(out) - 1);
    if (mods & XKeyboardModifier_ShiftModifier)
        XStrncat(out, "Shift+", cap - XStrlen(out) - 1);
    if (mods & XKeyboardModifier_AltModifier)
        XStrncat(out, "Alt+", cap - XStrlen(out) - 1);
    if (mods & XKeyboardModifier_MetaModifier)
        XStrncat(out, "Meta+", cap - XStrlen(out) - 1);
}

/** @brief 将键码转为可读名称（对标 QKeySequence 的键名映射；可打印键
 *         统一大写，对标 qkeysequence.cpp:1276 keyName 的
 *         QChar::fromUcs2(key).toUpper()）。 */
static const char* xks_keyName(int key)
{
    static char buf[8];
    if (key >= 0x20 && key <= 0x7E) {
        char c = (char)key;
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        buf[0] = c;
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
    for (i = 0; i < seq->count && (size_t)cap > XStrlen(out) + 32; ++i) {
        char prefix[32];
        if (i > 0) XStrncat(out, ", ", cap - XStrlen(out) - 1);
        xks_modPrefix(seq->combos[i].modifiers, prefix, sizeof(prefix));
        XStrncat(out, prefix, cap - XStrlen(out) - 1);
        XStrncat(out, xks_keyName(seq->combos[i].key), cap - XStrlen(out) - 1);
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

/* ==================== 捕获态机（对标 Qt 6.8 录制语义） ==================== */

/** @brief 捕获空闲超时（毫秒）：捕获期间持续无输入达此时长自动结束
 *         捕获，防捕获态滞留形成永久键盘陷阱（本批猎获 ②）。 */
#define XKEYSEQUENCEEDIT_CAPTURE_IDLE_MS 5000

/** @brief 启动/重启捕获空闲定时器（每次输入活动后调用）。 */
static void xkse_rearmIdleTimer(XKeySequenceEdit* edit)
{
    XObject* object = (XObject*)edit;
    if (edit->m_idleTimer != XTIMER_INVALID_ID)
        XObject_killTimer(object, edit->m_idleTimer);
    edit->m_idleTimer = XObject_startTimer_ms(
        object, XKEYSEQUENCEEDIT_CAPTURE_IDLE_MS, XTimerType_CoarseTimer);
}

/** @brief 停止捕获空闲定时器。 */
static void xkse_disarmIdleTimer(XKeySequenceEdit* edit)
{
    if (edit->m_idleTimer == XTIMER_INVALID_ID) return;
    XObject_killTimer((XObject*)edit, edit->m_idleTimer);
    edit->m_idleTimer = XTIMER_INVALID_ID;
}

/** @brief 进入捕获态（对标 Qt：recording 在控件获得焦点时发起）。
 *  @note  幂等：已在捕获中则仅刷新空闲计时。 */
static void xkse_startCapture(XKeySequenceEdit* edit)
{
    edit->m_capturing = true;
    xkse_rearmIdleTimer(edit);
    XWidget_update((XWidget*)edit);
}

/** @brief 退出捕获态（Esc/失焦/空闲超时共用路径）。此后控件不再吞
 *         键：Tab 交窗口级走链、其余键沿父链上抛（XWidget.c:2791）。 */
static void xkse_stopCapture(XKeySequenceEdit* edit)
{
    if (!edit->m_capturing && edit->m_idleTimer == XTIMER_INVALID_ID)
        return;
    edit->m_capturing = false;
    xkse_disarmIdleTimer(edit);
    XWidget_update((XWidget*)edit);
}

/** @brief 结束编辑（对标 Qt finishEditing）：有序列时保留旧序列并发射
 *         editingFinished；捕获态保持——对标 Qt 6.8，聚焦期间结束键
 *         （Tab/Backtab）可反复 finish；真正退出捕获由 Esc/失焦/空闲
 *         超时承担（本批猎获 ② 的结束条件）。 */
static void xkse_finishEditing(XKeySequenceEdit* edit)
{
    if (edit->m_sequence.count > 0) {
        edit->m_oldSequence = edit->m_sequence;
        xkse_emitFinished(edit);
    }
}

/* ==================== 事件处理 ==================== */

/** @brief 左键按下：点击聚焦后进入按键捕获（对标 QKeySequenceEdit 的
 *         Qt::StrongFocus 点击聚焦语义——Qt 中点击控件即获焦点并开始
 *         捕获；XGui 的点击聚焦由控件 mousePress 自请焦点（XLineEdit
 *         同款模式），此前未重载 MousePressEvent 且默认 NoFocus，点击
 *         不聚焦、Ctrl+O 等组合永不被捕获（night #31）。 */
static void VX_kse_mousePressEvent(XWidget* self, XEvent* event)
{
    if (!self || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_PRESS) return;
    if (XMouseEvent_button((XMouseEvent*)event) ==
        XMouseButton_LeftButton) {
        XWidget_setFocusReason(self, XFocusReason_Mouse);
        XWidget_update(self);
        XEvent_accept(event);
        return;
    }
    XEvent_ignore(event);
}

/** @brief 获得焦点：发起捕获（对标 Qt 文档 "The recording is initiated
 *         when the widget receives the focus"；night #31 的点击聚焦修复
 *         使点击进入同时启动捕获，捕获指示随即可见）。 */
static void VX_kse_focusInEvent(XWidget* self, XEvent* event)
{
    XKeySequenceEdit* edit = (XKeySequenceEdit*)self;
    if (!edit || !event ||
        XEvent_type(event) != XEVENT_TYPE_FOCUS_IN) return;
    xkse_startCapture(edit);
    XClass_Parent(XWidget, EXWidget_FocusInEvent,
                  void (*)(XWidget*, XEvent*)) (self, event);
}

/** @brief 失去焦点：结束捕获并收口编辑（对标 Qt 6.8 focusOutEvent→
 *         finishEditing，弹窗焦点豁免——Qt PopupFocusReason 同口径）；
 *         此后按键不再被吞，Tab 恢复走链。 */
static void VX_kse_focusOutEvent(XWidget* self, XEvent* event)
{
    XKeySequenceEdit* edit = (XKeySequenceEdit*)self;
    if (!edit || !event ||
        XEvent_type(event) != XEVENT_TYPE_FOCUS_OUT) return;
    if (((XFocusEvent*)event)->m_reason != XFocusReason_Popup) {
        xkse_finishEditing(edit);
        xkse_stopCapture(edit);
    }
    XClass_Parent(XWidget, EXWidget_FocusOutEvent,
                  void (*)(XWidget*, XEvent*)) (self, event);
}

/** @brief 捕获空闲超时：捕获期间持续无输入达
 *         XKEYSEQUENCEEDIT_CAPTURE_IDLE_MS 自动结束捕获（防永久键盘
 *         陷阱；对标 Qt 以释放键定时器收口录制的思路）。 */
static void VX_kse_timerEvent(XObject* object, XTimerEvent* event)
{
    XKeySequenceEdit* edit = (XKeySequenceEdit*)object;
    if (!edit || !event) return;
    if (event->timerId == edit->m_idleTimer) {
        /* 先注销再清 id（对标 XStatusBar 范式：周期定时器不会到期自清，
         * 顺序颠倒会使 killTimer 永不执行、定时器按周期持续投递）。 */
        XObject_killTimer(object, edit->m_idleTimer);
        edit->m_idleTimer = XTIMER_INVALID_ID;
        xkse_stopCapture(edit);
    }
}

static void VX_kse_keyPressEvent(XWidget* self, XEvent* event)
{
    XKeySequenceEdit* edit = (XKeySequenceEdit*)self;
    XKeyEvent* ke;
    XKeyboardModifiers mods;
    int key;
    if (!edit || !event ||
        XEvent_type(event) != XEVENT_TYPE_KEY_PRESS) return;
    /* 捕获态门控（本批猎获 ② 根修）：非捕获态不消费任何按键——Tab 交
     * 窗口级 focusNextPrevChild 走链（XWidget.c:2791），其余键沿父链
     * 上抛；对标 Qt 6.8「录制发起于获得焦点」，未聚焦/已退出捕获的
     * 控件不再吞键，杜绝捕获态滞留陷阱。 */
    if (!edit->m_capturing) {
        XEvent_ignore(event);
        return;
    }
    ke = (XKeyEvent*)event;
    key = ke->m_key;
    mods = ke->m_modifiers &
           (XKeyboardModifier_ControlModifier |
            XKeyboardModifier_ShiftModifier |
            XKeyboardModifier_AltModifier |
            XKeyboardModifier_MetaModifier);
    /* 对标 Qt qkeysequenceedit.cpp:328 keyPressEvent：纯修饰键按下只
     * 累积状态、不产生分组——按 XKey_* 键码（0x01000020..23）识别。
     * night #31 根修：旧码误比 XKeyboardModifier_* 位掩码（0x01..0x08，
     * 是 modifiers 属性的取值），键码永远不等于它，修饰键按下遂落入
     * 下方记录分支被记成 keyName()=="?" 的多余分组（「?, Ctrl+o」）。 */
    if (key == (int)XKey_Control ||
        key == (int)XKey_Shift ||
        key == (int)XKey_Meta ||
        key == (int)XKey_Alt ||
        key == (int)XKey_None /* 与 Qt::Key_unknown 口径同：未知键不记录。 */) {
        xkse_rearmIdleTimer(edit);
        XEvent_accept(event);
        return;
    }
    /* 对标 Qt 6.8：结束键组合（默认 Tab/Backtab）结束编辑并发射
     * editingFinished()；捕获态保持（聚焦期间可反复 finish，退出捕获
     * 走 Esc/失焦/空闲超时——见 xkse_finishEditing）。 */
    {
        int fi;
        for (fi = 0; fi < edit->m_finishingCount; ++fi) {
            if (edit->m_finishing[fi].key == key &&
                edit->m_finishing[fi].modifiers == mods) {
                xkse_finishEditing(edit);
                xkse_rearmIdleTimer(edit);
                XEvent_accept(event);
                return;
            }
        }
    }
    /* 对标 Qt：Return/Enter 确认序列（发射 editingFinished，捕获态
     * 保持，可继续补录）。 */
    if (key == (int)XKey_Return || key == (int)XKey_Enter) {
        xkse_finishEditing(edit);
        xkse_rearmIdleTimer(edit);
        XEvent_accept(event);
        return;
    }
    /* 对标 Qt：Esc 清空并结束捕获（不录 Esc）——任务要求的反陷阱
     * 最低保障：Esc 后按键即刻恢复走链/上抛。 */
    if (key == (int)XKey_Escape && mods == XKeyboardModifier_NoModifier) {
        XKeySequenceEdit_clear(edit);
        xkse_stopCapture(edit);
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
        xkse_rearmIdleTimer(edit);
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
    xkse_rearmIdleTimer(edit);
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
    uint32_t focusCol;
    int w;
    int h;
    if (!edit || !event) return;
    w = XWidget_width(self);
    h = XWidget_height(self);
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
#if XPALETTE_ON
    {
        XPalette palette = XWidget_palette(self);
        XColor c = XPalette_color(&palette, XPaletteColorGroup_Current,
                                  XPaletteColorRole_WindowText);
        textCol = XColor_rgba(&c);
        c = XPalette_color(&palette, XPaletteColorGroup_Current,
                           XPaletteColorRole_Highlight);
        focusCol = XColor_rgba(&c);
    }
#else
    textCol = 0xFF000000u;
    focusCol = 0xFF3080D8u;
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
    /* 焦点/捕获指示（对标 QKeySequenceEdit 聚焦态与 Qt 虚线焦点框，
     * 本批猎获 ①）：聚焦即画虚线焦点框——捕获中用高亮色（点击进入
     * 捕获指示即刻可见），非捕获聚焦态用灰色（空态聚焦不再零指示）；
     * 捕获中且序列为空时另绘 "Press shortcut" 占位提示（对标 Qt
     * QKeySequenceEdit 的 placeholderText 默认文案）。 */
    if (XWidget_hasFocus(self)) {
        XRect focusRect;
        XRect_init(&focusRect, 2, 2, w - 4, h - 4);
        XPainter_setPen(&painter,
                        edit->m_capturing ? focusCol : 0xFF606060u);
        XPainter_setPenStyle(&painter, XPainterPenStyle_DashLine);
        XPainter_drawRect(&painter, &focusRect);
    }
    if (edit->m_capturing && display[0] == '\0') {
        XFont hintFont = XWidget_font(self);
        XPainter_setFont(&painter, &hintFont);
        XPainter_drawText(&painter, 6, h / 2 + 5, "Press shortcut",
                          0xFF909090u);
        XFont_deinit_base(&hintFont);
    }
    if (display[0] != '\0') {
        XFont font = XWidget_font(self);
        XPainter_setFont(&painter, &font);
        XPainter_drawText(&painter, 6, h / 2 + 5, display, textCol);
        XFont_deinit_base(&font);
    }
    XPainter_deinit(&painter);
}

/* ==================== 生命周期与虚表 ==================== */

/** @brief 析构：注销捕获空闲定时器，防止对象释放后定时器悬空派发。 */
static void VX_kse_deinit(XKeySequenceEdit* self)
{
    if (!self) return;
    xkse_disarmIdleTimer(self);
    XClass_Deinit_Parent(XWidget, (XWidget*)self);
}

XVtable* XKeySequenceEdit_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XKeySequenceEdit)
    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_KeyPressEvent, VX_kse_keyPressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MousePressEvent, VX_kse_mousePressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VX_kse_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_FocusInEvent, VX_kse_focusInEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_FocusOutEvent, VX_kse_focusOutEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_TimerEvent, VX_kse_timerEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VX_kse_deinit);
    return XVTABLE_DEFAULT;
}

void XKeySequenceEdit_init(XKeySequenceEdit* self, XWidget* parent,
                           XWidgetFlags flags)
{
    XSize hint;
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XWidget_init(&self->m_base, parent, flags);
    XClassSetVtable(self, XKeySequenceEdit);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    self->m_maxLength = XKEYSEQUENCEEDIT_MAX_LENGTH;
    self->m_clearButton = false;
    self->m_capturing = false;
    self->m_idleTimer = XTIMER_INVALID_ID;
    /* 对标 Qt 6.8：默认结束键组合 {Qt::Key_Tab, Qt::Key_Backtab}。 */
    self->m_finishingCount = 2;
    self->m_finishing[0].modifiers = XKeyboardModifier_NoModifier;
    self->m_finishing[0].key = (int)XKey_Tab;
    self->m_finishing[1].modifiers = XKeyboardModifier_NoModifier;
    self->m_finishing[1].key = (int)XKey_Backtab;
    /* 对标 QKeySequenceEditPrivate::init 的 setFocusPolicy(Qt::StrongFocus)：
     * 可经 Tab 与点击取得焦点（night #31：默认 NoFocus 使点击/Tab 均
     * 无法聚焦，捕获功能整体不可用）。 */
    XWidget_setFocusPolicy((XWidget*)self, XWidgetFocusPolicy_StrongFocus);
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
    XMemset(&self->m_sequence, 0, sizeof(XKeySequence));
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

/* ============ 捕获态控制（对标 Qt 6.7+ capturing API） ============ */

void XKeySequenceEdit_startCapturing(XKeySequenceEdit* self)
{
    if (!self) return;
    xkse_startCapture(self);
}

void XKeySequenceEdit_stopCapturing(XKeySequenceEdit* self)
{
    if (!self) return;
    xkse_finishEditing(self);
    xkse_stopCapture(self);
}

void XKeySequenceEdit_cancelCapturing(XKeySequenceEdit* self)
{
    if (!self) return;
    XKeySequenceEdit_clear(self);
    xkse_stopCapture(self);
}

bool XKeySequenceEdit_isCapturing(const XKeySequenceEdit* self)
{
    return self ? self->m_capturing : false;
}

/* ============ 结束键组合（对标 Qt 6.8 finishingKeyCombinations） ============ */

int XKeySequenceEdit_finishingKeyCombinationCount(
    const XKeySequenceEdit* self)
{
    return self ? self->m_finishingCount : 0;
}

const XKeyCombination* XKeySequenceEdit_finishingKeyCombinations(
    const XKeySequenceEdit* self)
{
    return self ? self->m_finishing : NULL;
}

void XKeySequenceEdit_setFinishingKeyCombinations(
    XKeySequenceEdit* self, const XKeyCombination* combos, int count)
{
    int i;
    if (!self) return;
    if (!combos || count <= 0) {
        self->m_finishingCount = 0;
        XMemset(self->m_finishing, 0, sizeof(self->m_finishing));
        return;
    }
    if (count > XKEYSEQUENCEEDIT_MAX_FINISHING)
        count = XKEYSEQUENCEEDIT_MAX_FINISHING;
    for (i = 0; i < count; ++i)
        self->m_finishing[i] = combos[i];
    self->m_finishingCount = count;
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