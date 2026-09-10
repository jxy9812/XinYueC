/**
 * @file       XAbstractSpinBox.c
 * @brief      XAbstractSpinBox 微调框抽象基类实现（对齐 Qt 6.8
 *             QAbstractSpinBox）。
 * @details    基类职责：
 *             - 内嵌 XLineEdit 编辑区（默认占满整个控件；子类可在
 *               重绘/布局时调整 geometry，或经 setLineEdit 整体替换）；
 *             - 按钮符号/键盘跟踪/边框/修正模式/循环/加速/千分位/
 *               特殊值文本等属性存取（数值与 Qt 对齐）；
 *             - 虚槽：StepBy（基类空操作）、Validate（默认 Acceptable）、
 *               Fixup（默认空操作）、Clear（清空编辑框）、StepEnabled
 *               （默认 StepNone）、Interpret/UpdateEdit（内部提交/刷新钩子）；
 *             - 事件：keyPressEvent（Up/Down/PageUp/PageDown 步进、
 *               Home/End 边界跳转、Return/Enter 提交并发射 editingFinished、
 *               其余键转发内嵌编辑框）、wheelEvent（120 角度=1 步，Ctrl×10）、
 *               focusIn/focusOut（失焦提交）、close/hide/show/timer 转发父类、
 *               mouseMove 转发父类（长按连续步进为后续扩展）；
 *             - editingFinished 信号（连接内嵌编辑框的 editingFinished
 *               转发，Return/失焦触发）。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "CXinYueConfig.h"
#if XWIDGET_ON && XABSTRACTSPINBOX_ON && XLINEEDIT_ON

#include "XAbstractSpinBox.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XCoreApplication.h"
#include <string.h>
#include <stdio.h>
#include <limits.h>
#if XWINDOWEVENT_ON
#include "XWindowEvent.h"
#endif /* XWINDOWEVENT_ON */

/** @brief Home/End 边界跳转步数阈值（|steps| ≥ 该值时跳转到范围边界）。 */
#define XABSTRACTSPINBOX_BOUNDARY_STEP (INT_MAX / 4)

/* ==================== 前向声明 ==================== */
static void  VXAbstractSpinBox_deinit(XClass* obj);
static void  VXAbstractSpinBox_copy(XAbstractSpinBox* self,
                                    const XAbstractSpinBox* other);
static void  VXAbstractSpinBox_move(XAbstractSpinBox* self,
                                    XAbstractSpinBox* other);
static void  VXAbstractSpinBox_stepBy(XAbstractSpinBox* self, int steps);
static XValidatorState VXAbstractSpinBox_validate(XAbstractSpinBox* self,
                                                  const char* input,
                                                  int* pos);
static void  VXAbstractSpinBox_fixup(XAbstractSpinBox* self, char* input,
                                     size_t capacity);
static void  VXAbstractSpinBox_clear(XAbstractSpinBox* self);
static int   VXAbstractSpinBox_stepEnabled(XAbstractSpinBox* self);
static void  VXAbstractSpinBox_interpret(XAbstractSpinBox* self);
static void  VXAbstractSpinBox_updateEdit(XAbstractSpinBox* self);
static void  VXAbstractSpinBox_resizeEvent(XWidget* self, XEvent* event);
static void  VXAbstractSpinBox_keyPressEvent(XWidget* self, XEvent* event);
static void  VXAbstractSpinBox_keyReleaseEvent(XWidget* self, XEvent* event);
static void  VXAbstractSpinBox_wheelEvent(XWidget* self, XEvent* event);
static void  VXAbstractSpinBox_focusInEvent(XWidget* self, XEvent* event);
static void  VXAbstractSpinBox_focusOutEvent(XWidget* self, XEvent* event);
static void  VXAbstractSpinBox_closeEvent(XWidget* self, XEvent* event);
static void  VXAbstractSpinBox_hideEvent(XWidget* self, XEvent* event);
static void  VXAbstractSpinBox_showEvent(XWidget* self, XEvent* event);
static void  VXAbstractSpinBox_mouseMoveEvent(XWidget* self, XEvent* event);
static void  VXAbstractSpinBox_timerEvent(XObject* self, XTimerEvent* event);

/* ==================== 内部辅助 ==================== */

static void spinbox_forwardEditingFinished(XObject* sender, XVarList* args);

/** @brief 发射 void 信号（无连接时释放参数列表）。 */
static void spinbox_emitVoidSignal(XAbstractSpinBox* self, size_t signal)
{
    XVarList* arguments;
    if (!self) return;
    arguments = XVarList_create(0);
    if (!arguments) return;
    if (((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, arguments, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(arguments);
    }
}

/** @brief 内部创建默认编辑框。 */
static XLineEdit* spinbox_createDefaultLineEdit(XAbstractSpinBox* self)
{
    XLineEdit* edit = XLineEdit_create((XWidget*)self, 0);
    if (!edit) return NULL;
    XWidget_setGeometry((XWidget*)edit, 0, 0,
                        XWidget_width((XWidget*)self),
                        XWidget_height((XWidget*)self));
    /* 子控件默认可见（QAbstractSpinBox 内嵌编辑框语义）。 */
    XWidget_show((XWidget*)edit);
    XObject_connect_2((XObject*)edit,
                      (size_t)XLineEdit_editingFinished_signal(edit),
                      spinbox_forwardEditingFinished);
    return edit;
}

/** @brief 内嵌编辑框 editingFinished 转发槽（转发为自身 editingFinished）。 */
static void spinbox_forwardEditingFinished(XObject* sender, XVarList* args)
{
    XAbstractSpinBox* self;
    (void)args;
    if (!sender) return;
    self = (XAbstractSpinBox*)XWidget_parentWidget((XWidget*)sender);
    /* sender 即内嵌编辑框；parent 即本控件 */
    if (self)
        spinbox_emitVoidSignal(
            self, (size_t)XAbstractSpinBox_editingFinished_signal(self));
}

/** @brief 复制字符串到自管缓冲（NULL 输入按空串）。 */
static char* spinbox_strdup(const char* text)
{
    return XMemory_strdup(text ? text : "");
}

/** @brief 释放自管字符串并置 NULL。 */
static void spinbox_strfree(char** ptext)
{
    if (ptext && *ptext) {
        XFree_System(*ptext);
        *ptext = NULL;
    }
}

/* ==================== 虚槽实现 ==================== */

/** @brief 尺寸变化：同步内嵌编辑框几何（占满减按钮区）。RESIZE 事件
 *         走 ResizeEvent 虚槽（不走 changeEvent），在此同步编辑框。 */
static void VXAbstractSpinBox_resizeEvent(XWidget* self, XEvent* event)
{
    XAbstractSpinBox* spin = (XAbstractSpinBox*)self;
    if (!spin || !event ||
        XEvent_type(event) != XEVENT_TYPE_RESIZE) return;
    if (spin->m_lineEdit) {
        int w = XWidget_width(self);
        int h = XWidget_height(self);
        if (spin->m_buttonSymbols !=
            XAbstractSpinBoxButtonSymbols_NoButtons)
            w -= 16;
        if (w < 1) w = 1;
        XWidget_setGeometry((XWidget*)spin->m_lineEdit, 0, 0, w, h);
    }
    XClass_Parent(XWidget, EXWidget_ResizeEvent,
                  void(*)(XWidget*, XEvent*))((XWidget*)self, event);
}

/** @brief StepBy 默认实现：空操作（子类重载）。 */
static void VXAbstractSpinBox_stepBy(XAbstractSpinBox* self, int steps)
{
    (void)self;
    (void)steps;
}

/** @brief Validate 默认实现：恒为 Acceptable（对标 Qt 基类默认）。 */
static XValidatorState VXAbstractSpinBox_validate(XAbstractSpinBox* self,
                                                  const char* input,
                                                  int* pos)
{
    (void)self;
    (void)input;
    (void)pos;
    return XValidatorState_Acceptable;
}

/** @brief Fixup 默认实现：空操作（子类重载）。 */
static void VXAbstractSpinBox_fixup(XAbstractSpinBox* self, char* input,
                                    size_t capacity)
{
    (void)self;
    (void)input;
    (void)capacity;
}

/** @brief Clear 默认实现：清空编辑框并置内部 cleared 标志。 */
static void VXAbstractSpinBox_clear(XAbstractSpinBox* self)
{
    if (!self) return;
    self->m_cleared = true;
    if (self->m_lineEdit)
        XLineEdit_setText(self->m_lineEdit, "");
}

/** @brief StepEnabled 默认实现：抽象基类无范围语义，返回 StepNone。 */
static int VXAbstractSpinBox_stepEnabled(XAbstractSpinBox* self)
{
    (void)self;
    return XAbstractSpinBoxStepEnabledFlag_StepNone;
}

/** @brief Interpret 默认实现：空操作（提交语义由子类承接）。 */
static void VXAbstractSpinBox_interpret(XAbstractSpinBox* self)
{
    (void)self;
}

/** @brief UpdateEdit 默认实现：仅重绘（显示文本刷新由子类承接）。 */
static void VXAbstractSpinBox_updateEdit(XAbstractSpinBox* self)
{
    if (self) XWidget_update((XWidget*)self);
}

/** @brief 键盘按下：Up/Down/PageUp/PageDown 步进、Home/End 边界跳转、
 *         Return/Enter 提交；其余按键转发内嵌编辑框。 */
static void VXAbstractSpinBox_keyPressEvent(XWidget* self, XEvent* event)
{
    XAbstractSpinBox* spin = (XAbstractSpinBox*)self;
    XKeyEvent* ke;
    int key;
    int steps;
    int flags;
    if (!spin || !event ||
        XEvent_type(event) != XEVENT_TYPE_KEY_PRESS) return;
    ke = (XKeyEvent*)event;
    key = XKeyEvent_key(ke);

    switch (key) {
    case XKey_Up:
    case XKey_Down:
    case XKey_PageUp:
    case XKey_PageDown: {
        bool up = (key == XKey_Up || key == XKey_PageUp);
        steps = (key == XKey_PageUp || key == XKey_PageDown) ? 10 : 1;
        flags = XAbstractSpinBox_stepEnabled_base(spin);
        if (flags & (up ? XAbstractSpinBoxStepEnabledFlag_StepUpEnabled
                        : XAbstractSpinBoxStepEnabledFlag_StepDownEnabled)) {
            XAbstractSpinBox_stepBy_base(spin, up ? steps : -steps);
            XEvent_accept(event);
            return;
        }
        XEvent_ignore(event);
        return;
    }
    case XKey_Home:
    case XKey_End: {
        /* 边界跳转：Home→minimum，End→maximum（经大步数 stepBy 语义）。
           Qt 将 Home/End 交给编辑框做光标移动；本实现按任务要求作为
           步进键处理，差异见文档。 */
        bool up = (key == XKey_End);
        flags = XAbstractSpinBox_stepEnabled_base(spin);
        if (flags & (up ? XAbstractSpinBoxStepEnabledFlag_StepUpEnabled
                        : XAbstractSpinBoxStepEnabledFlag_StepDownEnabled)) {
            XAbstractSpinBox_stepBy_base(spin,
                up ? XABSTRACTSPINBOX_BOUNDARY_STEP
                   : -XABSTRACTSPINBOX_BOUNDARY_STEP);
            XEvent_accept(event);
            return;
        }
        XEvent_ignore(event);
        return;
    }
    case XKey_Return:
    case XKey_Enter:
        /* 提交编辑：解释文本 + 全选 + 发射 editingFinished（对标 Qt）。 */
        XAbstractSpinBox_interpretText(spin);
        XAbstractSpinBox_selectAll(spin);
        XEvent_ignore(event);
        spinbox_emitVoidSignal(
            spin, (size_t)XAbstractSpinBox_editingFinished_signal(spin));
        return;
    default:
        break;
    }

    /* 其余按键转发内嵌编辑框（对标 Qt：d->edit->event(event)）。 */
    if (spin->m_lineEdit) {
        XObject_event_base((XObject*)spin->m_lineEdit, event);
        return;
    }
    XClass_Parent(XWidget, EXWidget_KeyPressEvent,
                  void(*)(XWidget*, XEvent*))((XWidget*)self, event);
}

/** @brief 键盘释放：转发内嵌编辑框（长按连续步进为后续扩展）。 */
static void VXAbstractSpinBox_keyReleaseEvent(XWidget* self, XEvent* event)
{
    XAbstractSpinBox* spin = (XAbstractSpinBox*)self;
    if (!spin || !event ||
        XEvent_type(event) != XEVENT_TYPE_KEY_RELEASE) return;
    if (spin->m_lineEdit) {
        XObject_event_base((XObject*)spin->m_lineEdit, event);
        return;
    }
    XClass_Parent(XWidget, EXWidget_KeyReleaseEvent,
                  void(*)(XWidget*, XEvent*))((XWidget*)self, event);
}

/** @brief 滚轮步进：角度增量累计，每 120 角度=1 步；Ctrl 时 ×10。 */
static void VXAbstractSpinBox_wheelEvent(XWidget* self, XEvent* event)
{
    XAbstractSpinBox* spin = (XAbstractSpinBox*)self;
    int steps = 0;
    int flags;
    if (!spin || !event || XEvent_type(event) != XEVENT_TYPE_WHEEL) return;
#if XWINDOWEVENT_ON
    {
        XWheelEvent* we = (XWheelEvent*)event;
        XPoint delta = XWheelEvent_angleDelta(we);
        XKeyboardModifiers mods = XWheelEvent_modifiers(we);
        spin->m_wheelDeltaRemainder += (delta.y != 0) ? delta.y : delta.x;
        steps = spin->m_wheelDeltaRemainder / 120;
        spin->m_wheelDeltaRemainder -= steps * 120;
        if (mods & XKeyboardModifier_ControlModifier) steps *= 10;
    }
#endif /* XWINDOWEVENT_ON */
    if (steps != 0) {
        flags = XAbstractSpinBox_stepEnabled_base(spin);
        if (flags & (steps > 0 ? XAbstractSpinBoxStepEnabledFlag_StepUpEnabled
                               : XAbstractSpinBoxStepEnabledFlag_StepDownEnabled))
            XAbstractSpinBox_stepBy_base(spin, steps);
    }
    XEvent_accept(event);
}

/** @brief 获得焦点：转发父类。 */
static void VXAbstractSpinBox_focusInEvent(XWidget* self, XEvent* event)
{
    if (self && event) {
        XClass_Parent(XWidget, EXWidget_FocusInEvent,
                      void(*)(XWidget*, XEvent*))((XWidget*)self, event);
    }
}

/** @brief 失去焦点：先解释当前文本，再转发父类并发射 editingFinished。 */
static void VXAbstractSpinBox_focusOutEvent(XWidget* self, XEvent* event)
{
    XAbstractSpinBox* spin = (XAbstractSpinBox*)self;
    if (!spin || !event) return;
    XAbstractSpinBox_interpretText(spin);
    XClass_Parent(XWidget, EXWidget_FocusOutEvent,
                  void(*)(XWidget*, XEvent*))((XWidget*)self, event);
    spinbox_emitVoidSignal(
        spin, (size_t)XAbstractSpinBox_editingFinished_signal(spin));
}

/** @brief 关闭事件：先解释当前文本，再转发父类。 */
static void VXAbstractSpinBox_closeEvent(XWidget* self, XEvent* event)
{
    XAbstractSpinBox* spin = (XAbstractSpinBox*)self;
    if (!spin || !event) return;
    XAbstractSpinBox_interpretText(spin);
    XClass_Parent(XWidget, EXWidget_CloseEvent,
                  void(*)(XWidget*, XEvent*))((XWidget*)self, event);
}

/** @brief 隐藏事件：先解释当前文本，再转发父类。 */
static void VXAbstractSpinBox_hideEvent(XWidget* self, XEvent* event)
{
    XAbstractSpinBox* spin = (XAbstractSpinBox*)self;
    if (!spin || !event) return;
    XAbstractSpinBox_interpretText(spin);
    XClass_Parent(XWidget, EXWidget_HideEvent,
                  void(*)(XWidget*, XEvent*))((XWidget*)self, event);
}

/** @brief 显示事件：刷新显示文本后转发父类。 */
static void VXAbstractSpinBox_showEvent(XWidget* self, XEvent* event)
{
    XAbstractSpinBox* spin = (XAbstractSpinBox*)self;
    if (!spin || !event) return;
    XAbstractSpinBox_updateEdit_base(spin);
    XClass_Parent(XWidget, EXWidget_ShowEvent,
                  void(*)(XWidget*, XEvent*))((XWidget*)self, event);
}

/** @brief 鼠标移动：转发父类（按住按钮连续步进的 timer 为后续扩展）。 */
static void VXAbstractSpinBox_mouseMoveEvent(XWidget* self, XEvent* event)
{
    if (self && event) {
        XClass_Parent(XWidget, EXWidget_MouseMoveEvent,
                      void(*)(XWidget*, XEvent*))((XWidget*)self, event);
    }
}

/** @brief 定时器事件：转发父类（长按连续步进的 timer 为后续扩展）。 */
static void VXAbstractSpinBox_timerEvent(XObject* self, XTimerEvent* event)
{
    if (self && event) {
        XClass_Parent(XObject, EXObject_TimerEvent,
                      void(*)(XObject*, XTimerEvent*))(self, event);
    }
}

/** @brief 深拷贝：基类深拷贝后复制编辑框指针语义（拷贝为重建默认编辑框）。 */
static void VXAbstractSpinBox_copy(XAbstractSpinBox* self,
                                   const XAbstractSpinBox* other)
{
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XAbstractSpinBox_init(self, NULL, 0);
    XClass_Parent(XWidget, EXClass_Copy,
                  void(*)(XWidget*, const XWidget*))((XWidget*)self,
                                                     (const XWidget*)other);
    self->m_buttonSymbols = other->m_buttonSymbols;
    self->m_keyboardTracking = other->m_keyboardTracking;
    self->m_frame = other->m_frame;
    self->m_correctionMode = other->m_correctionMode;
    self->m_wrapping = other->m_wrapping;
    self->m_accelerated = other->m_accelerated;
    self->m_groupSeparatorShown = other->m_groupSeparatorShown;
    self->m_cleared = other->m_cleared;
    self->m_wheelDeltaRemainder = other->m_wheelDeltaRemainder;
    spinbox_strfree(&self->m_specialValueText);
    self->m_specialValueText =
        spinbox_strdup(other->m_specialValueText);
    /* 编辑框不可复制：重建默认编辑框（父控件指针指向自身）。 */
    if (self->m_lineEdit)
        XLineEdit_delete_base(self->m_lineEdit);
    self->m_lineEdit = spinbox_createDefaultLineEdit(self);
}

/** @brief 移动语义：转移编辑框指针与字符串，源对象归构造默认值。 */
static void VXAbstractSpinBox_move(XAbstractSpinBox* self,
                                   XAbstractSpinBox* other)
{
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XAbstractSpinBox_init(self, NULL, 0);
    XClass_Parent(XWidget, EXClass_Move,
                  void(*)(XWidget*, XWidget*))((XWidget*)self,
                                               (XWidget*)other);
    if (self->m_lineEdit)
        XLineEdit_delete_base(self->m_lineEdit);
    self->m_lineEdit = other->m_lineEdit;
    other->m_lineEdit = NULL;
    spinbox_strfree(&self->m_specialValueText);
    self->m_specialValueText = other->m_specialValueText;
    other->m_specialValueText = NULL;
    self->m_buttonSymbols = other->m_buttonSymbols;
    self->m_keyboardTracking = other->m_keyboardTracking;
    self->m_frame = other->m_frame;
    self->m_correctionMode = other->m_correctionMode;
    self->m_wrapping = other->m_wrapping;
    self->m_accelerated = other->m_accelerated;
    self->m_groupSeparatorShown = other->m_groupSeparatorShown;
    self->m_cleared = other->m_cleared;
    self->m_wheelDeltaRemainder = other->m_wheelDeltaRemainder;
    other->m_buttonSymbols = XAbstractSpinBoxButtonSymbols_UpDownArrows;
    other->m_keyboardTracking = true;
    other->m_frame = true;
    other->m_correctionMode = XAbstractSpinBoxCorrectionMode_CorrectToPreviousValue;
    other->m_wrapping = false;
    other->m_accelerated = false;
    other->m_groupSeparatorShown = false;
    other->m_cleared = false;
    other->m_wheelDeltaRemainder = 0;
}

/** @brief 析构：释放内嵌编辑框与特殊值文本。 */
static void VXAbstractSpinBox_deinit(XClass* obj)
{
    XAbstractSpinBox* self = (XAbstractSpinBox*)obj;
    if (!self) return;
    if (self->m_lineEdit)
        XLineEdit_delete_base(self->m_lineEdit);
    self->m_lineEdit = NULL;
    spinbox_strfree(&self->m_specialValueText);
    XClass_Deinit_Parent(XWidget, (XWidget*)obj);
}

/* ==================== 生命周期 ==================== */

XVtable* XAbstractSpinBox_class_init(void)
{
    void* protectedSlots[] = {
        VXAbstractSpinBox_stepBy,
        VXAbstractSpinBox_validate,
        VXAbstractSpinBox_fixup,
        VXAbstractSpinBox_clear,
        VXAbstractSpinBox_stepEnabled,
        VXAbstractSpinBox_interpret,
        VXAbstractSpinBox_updateEdit
    };

    XVTABLE_INIT_DEFAULT(XAbstractSpinBox)
    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_ADD_FUNC_LIST_DEFAULT(protectedSlots);

    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ResizeEvent, VXAbstractSpinBox_resizeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_KeyPressEvent, VXAbstractSpinBox_keyPressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_KeyReleaseEvent, VXAbstractSpinBox_keyReleaseEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_WheelEvent, VXAbstractSpinBox_wheelEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_FocusInEvent, VXAbstractSpinBox_focusInEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_FocusOutEvent, VXAbstractSpinBox_focusOutEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_CloseEvent, VXAbstractSpinBox_closeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_HideEvent, VXAbstractSpinBox_hideEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ShowEvent, VXAbstractSpinBox_showEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseMoveEvent, VXAbstractSpinBox_mouseMoveEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_TimerEvent, VXAbstractSpinBox_timerEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractSpinBox_StepBy, VXAbstractSpinBox_stepBy);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractSpinBox_Validate, VXAbstractSpinBox_validate);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractSpinBox_Fixup, VXAbstractSpinBox_fixup);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractSpinBox_Clear, VXAbstractSpinBox_clear);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractSpinBox_StepEnabled, VXAbstractSpinBox_stepEnabled);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractSpinBox_Interpret, VXAbstractSpinBox_interpret);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractSpinBox_UpdateEdit, VXAbstractSpinBox_updateEdit);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXAbstractSpinBox_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXAbstractSpinBox_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXAbstractSpinBox_move);

    return XVTABLE_DEFAULT;
}

void XAbstractSpinBox_init(XAbstractSpinBox* self, XWidget* parent,
                           XWidgetFlags flags)
{
    if (!self) return;
    XWidget_init((XWidget*)self, parent, flags);
    XClassSetVtable(self, XAbstractSpinBox);

    self->m_lineEdit = NULL;
    self->m_buttonSymbols = XAbstractSpinBoxButtonSymbols_UpDownArrows;
    self->m_keyboardTracking = true;
    self->m_frame = true;
    self->m_correctionMode = XAbstractSpinBoxCorrectionMode_CorrectToPreviousValue;
    self->m_wrapping = false;
    self->m_accelerated = false;
    self->m_groupSeparatorShown = false;
    self->m_specialValueText = NULL;
    self->m_cleared = false;
    self->m_wheelDeltaRemainder = 0;
    self->m_lineEdit = spinbox_createDefaultLineEdit(self);
}

XAbstractSpinBox* XAbstractSpinBox_create_ex(XMemoryType memory,
                                             XWidget* parent,
                                             XWidgetFlags flags)
{
    XAbstractSpinBox* self =
        (XAbstractSpinBox*)XMemory_malloc(sizeof(XAbstractSpinBox), memory);
    if (!self) return NULL;
    XAbstractSpinBox_init(self, parent, flags);
    Set_Class_Memory(self, memory); Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 属性（对标 QAbstractSpinBox public API） ==================== */

XLineEdit* XAbstractSpinBox_lineEdit(const XAbstractSpinBox* self)
{
    return self ? self->m_lineEdit : NULL;
}

void XAbstractSpinBox_setLineEdit(XAbstractSpinBox* self, XLineEdit* lineEdit)
{
    if (!self) return;
    if (lineEdit == self->m_lineEdit) return;
    if (self->m_lineEdit)
        XLineEdit_delete_base(self->m_lineEdit);
    self->m_lineEdit = lineEdit;
    if (!self->m_lineEdit)
        self->m_lineEdit = spinbox_createDefaultLineEdit(self);
}

int XAbstractSpinBox_buttonSymbols(const XAbstractSpinBox* self)
{
    return self ? self->m_buttonSymbols
                : XAbstractSpinBoxButtonSymbols_UpDownArrows;
}

void XAbstractSpinBox_setButtonSymbols(XAbstractSpinBox* self, int symbols)
{
    if (!self || self->m_buttonSymbols == symbols) return;
    if (symbols != XAbstractSpinBoxButtonSymbols_NoButtons &&
        symbols != XAbstractSpinBoxButtonSymbols_UpDownArrows &&
        symbols != XAbstractSpinBoxButtonSymbols_PlusMinus)
        return;
    self->m_buttonSymbols = symbols;
    XWidget_update((XWidget*)self);
    if (self->m_lineEdit) {
        int w = XWidget_width((XWidget*)self) -
                (symbols != XAbstractSpinBoxButtonSymbols_NoButtons ? 16 : 0);
        if (w < 1) w = 1;
        XWidget_setGeometry((XWidget*)self->m_lineEdit, 0, 0, w,
                            XWidget_height((XWidget*)self));
    }
}

void XAbstractSpinBox_setCorrectionMode(XAbstractSpinBox* self, int mode)
{
    if (!self) return;
    if (mode != XAbstractSpinBoxCorrectionMode_CorrectToPreviousValue &&
        mode != XAbstractSpinBoxCorrectionMode_CorrectToNearestValue)
        return;
    self->m_correctionMode = mode;
}

int XAbstractSpinBox_correctionMode(const XAbstractSpinBox* self)
{
    return self ? self->m_correctionMode
                : XAbstractSpinBoxCorrectionMode_CorrectToPreviousValue;
}

bool XAbstractSpinBox_hasAcceptableInput(const XAbstractSpinBox* self)
{
    const char* text;
    int pos = 0;
    if (!self || !self->m_lineEdit) return false;
    text = XLineEdit_text(self->m_lineEdit);
    return XAbstractSpinBox_validate_base((XAbstractSpinBox*)self,
                                          text, &pos) ==
           XValidatorState_Acceptable;
}

const char* XAbstractSpinBox_text(const XAbstractSpinBox* self)
{
    if (!self || !self->m_lineEdit) return "";
    return XLineEdit_text(self->m_lineEdit);
}

const char* XAbstractSpinBox_specialValueText(const XAbstractSpinBox* self)
{
    return (self && self->m_specialValueText) ? self->m_specialValueText : "";
}

void XAbstractSpinBox_setSpecialValueText(XAbstractSpinBox* self,
                                          const char* text)
{
    if (!self) return;
    if (!text) text = "";
    if (self->m_specialValueText &&
        strcmp(self->m_specialValueText, text) == 0)
        return;
    spinbox_strfree(&self->m_specialValueText);
    self->m_specialValueText = spinbox_strdup(text);
    /* 值==minimum 时显示刷新（经 UpdateEdit 虚槽）。 */
    XAbstractSpinBox_updateEdit_base(self);
}

bool XAbstractSpinBox_wrapping(const XAbstractSpinBox* self)
{
    return self ? self->m_wrapping : false;
}

void XAbstractSpinBox_setWrapping(XAbstractSpinBox* self, bool wrapping)
{
    if (self) self->m_wrapping = wrapping;
}

bool XAbstractSpinBox_isReadOnly(const XAbstractSpinBox* self)
{
    return self && self->m_lineEdit
        ? XLineEdit_isReadOnly(self->m_lineEdit) : false;
}

void XAbstractSpinBox_setReadOnly(XAbstractSpinBox* self, bool readOnly)
{
    if (self && self->m_lineEdit)
        XLineEdit_setReadOnly(self->m_lineEdit, readOnly);
}

bool XAbstractSpinBox_keyboardTracking(const XAbstractSpinBox* self)
{
    return self ? self->m_keyboardTracking : true;
}

void XAbstractSpinBox_setKeyboardTracking(XAbstractSpinBox* self,
                                          bool tracking)
{
    if (self) self->m_keyboardTracking = tracking;
}

int XAbstractSpinBox_alignment(const XAbstractSpinBox* self)
{
    return self && self->m_lineEdit
        ? XLineEdit_alignment(self->m_lineEdit) : 0;
}

void XAbstractSpinBox_setAlignment(XAbstractSpinBox* self, int alignment)
{
    if (self && self->m_lineEdit)
        XLineEdit_setAlignment(self->m_lineEdit, alignment);
}

bool XAbstractSpinBox_hasFrame(const XAbstractSpinBox* self)
{
    return self ? self->m_frame : true;
}

void XAbstractSpinBox_setFrame(XAbstractSpinBox* self, bool on)
{
    if (self) self->m_frame = on;
}

void XAbstractSpinBox_setAccelerated(XAbstractSpinBox* self, bool on)
{
    if (self) self->m_accelerated = on;
}

bool XAbstractSpinBox_isAccelerated(const XAbstractSpinBox* self)
{
    return self ? self->m_accelerated : false;
}

void XAbstractSpinBox_setGroupSeparatorShown(XAbstractSpinBox* self,
                                             bool shown)
{
    if (!self || self->m_groupSeparatorShown == shown) return;
    self->m_groupSeparatorShown = shown;
    /* 显示文本刷新（经 UpdateEdit 虚槽）。 */
    XAbstractSpinBox_updateEdit_base(self);
}

bool XAbstractSpinBox_isGroupSeparatorShown(const XAbstractSpinBox* self)
{
    return self ? self->m_groupSeparatorShown : false;
}

void XAbstractSpinBox_interpretText(XAbstractSpinBox* self)
{
    if (!self) return;
    XAbstractSpinBox_interpret_base(self);
}

/* ==================== 公共槽 ==================== */

void XAbstractSpinBox_stepUp(XAbstractSpinBox* self)
{
    XAbstractSpinBox_stepBy_base(self, 1);
}

void XAbstractSpinBox_stepDown(XAbstractSpinBox* self)
{
    XAbstractSpinBox_stepBy_base(self, -1);
}

void XAbstractSpinBox_selectAll(XAbstractSpinBox* self)
{
    if (self && self->m_lineEdit)
        XLineEdit_selectAll(self->m_lineEdit);
}

/* ==================== 虚槽调度入口（*_base） ==================== */

void XAbstractSpinBox_stepBy_base(XAbstractSpinBox* self, int steps)
{
    if (!self) return;
    XClassGetVirtualFunc(self, EXAbstractSpinBox_StepBy,
                         void(*)(XAbstractSpinBox*, int))(self, steps);
}

XValidatorState XAbstractSpinBox_validate_base(XAbstractSpinBox* self,
                                               const char* input, int* pos)
{
    if (!self) return XValidatorState_Invalid;
    return XClassGetVirtualFunc(self, EXAbstractSpinBox_Validate,
                                XValidatorState(*)(XAbstractSpinBox*,
                                                   const char*, int*))(
        self, input, pos);
}

void XAbstractSpinBox_fixup_base(XAbstractSpinBox* self, char* input,
                                 size_t capacity)
{
    if (!self) return;
    if (!XClassGetVirtualFunc(self, EXAbstractSpinBox_Fixup, bool)) return;
    XClassGetVirtualFunc(self, EXAbstractSpinBox_Fixup,
                         void(*)(XAbstractSpinBox*, char*, size_t))(
        self, input, capacity);
}

void XAbstractSpinBox_clear_base(XAbstractSpinBox* self)
{
    if (!self) return;
    if (!XClassGetVirtualFunc(self, EXAbstractSpinBox_Clear, bool)) return;
    XClassGetVirtualFunc(self, EXAbstractSpinBox_Clear,
                         void(*)(XAbstractSpinBox*))(self);
}

int XAbstractSpinBox_stepEnabled_base(XAbstractSpinBox* self)
{
    if (!self) return XAbstractSpinBoxStepEnabledFlag_StepNone;
    if (!XClassGetVirtualFunc(self, EXAbstractSpinBox_StepEnabled, bool))
        return XAbstractSpinBoxStepEnabledFlag_StepNone;
    return XClassGetVirtualFunc(self, EXAbstractSpinBox_StepEnabled,
                                int(*)(XAbstractSpinBox*))(self);
}

void XAbstractSpinBox_interpret_base(XAbstractSpinBox* self)
{
    if (!self) return;
    if (!XClassGetVirtualFunc(self, EXAbstractSpinBox_Interpret, bool))
        return;
    XClassGetVirtualFunc(self, EXAbstractSpinBox_Interpret,
                         void(*)(XAbstractSpinBox*))(self);
}

void XAbstractSpinBox_updateEdit_base(XAbstractSpinBox* self)
{
    if (!self) return;
    if (!XClassGetVirtualFunc(self, EXAbstractSpinBox_UpdateEdit, bool))
        return;
    XClassGetVirtualFunc(self, EXAbstractSpinBox_UpdateEdit,
                         void(*)(XAbstractSpinBox*))(self);
}

/* ==================== 信号 ==================== */

void* XAbstractSpinBox_editingFinished_signal(XAbstractSpinBox* self)
{
    return (void*)(size_t)XAbstractSpinBox_editingFinished_signal;
}

#endif /* XWIDGET_ON && XABSTRACTSPINBOX_ON && XLINEEDIT_ON */
