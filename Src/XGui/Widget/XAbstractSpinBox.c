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
#include "XStringUtils.h"

#include "XAlgorithm.h"
#if XWIDGET_ON && XABSTRACTSPINBOX_ON && XLINEEDIT_ON

#include "XAbstractSpinBox.h"
#include "XString.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XCoreApplication.h"
#if XSTYLE_ON
#include "XStyle.h"
#include "XStyleOption.h"
#endif /* XSTYLE_ON */
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
static void spinbox_syncEditFieldGeometry(XAbstractSpinBox* self);

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

/** @brief      按样式同步内嵌编辑框几何（SC_SpinBoxEditField 口径）。
 * @details    修复「SpinBox 聚焦无可见指示」第二段：此前 resizeEvent/
 *             setButtonSymbols 把编辑框写死 (0,0,w-16,h)，黄底样式表
 *             （XLineEdit{background:#FFFFE0}）铺满容器自身矩形，把容器
 *             frame/虚线聚焦环整圈盖住（实测 709/724 像素黄底）。现改经
 *             样式 subControlRect(CC_SpinBox, SC_SpinBoxEditField) 取编辑
 *             区矩形——样式侧按 PM_SpinBoxFrameWidth（XCommonStyle=2/
 *             Fusion=3）四周留边距，给 frame 斜面与聚焦虚线留出可见带；
 *             按钮列宽亦由样式按高度推导，与绘制侧同源。无样式时回退
 *             既有口径（占满减固定 16px 按钮列、无边距——非样式回退
 *             绘制路径本无 frame）。 */
static void spinbox_syncEditFieldGeometry(XAbstractSpinBox* self)
{
    int w;
    int h;
    if (!self || !self->m_lineEdit) return;
    w = XWidget_width((XWidget*)self);
    h = XWidget_height((XWidget*)self);
#if XSTYLE_ON
    if (XStyle_defaultStyle() != NULL) {
        XStyleOption opt;
        XRect field;
        XStyleOption_init(&opt, XStyleCC_SpinBox);
        XRect_init(&opt.m_rect, 0, 0, w, h);
        opt.m_state = XWidget_isEnabled((XWidget*)self)
            ? (uint32_t)XStyleState_Enabled : 0;
        /* frame/按钮符号对接基类属性（与绘制侧 XSpinBox paintEvent
           同口径：m_spinFrame 裁边距、m_spinSymbols=NoButtons 时
           EditField 全宽）。 */
        opt.m_spinFrame = self->m_frame;
        opt.m_spinSymbols = self->m_buttonSymbols;
        field = XStyle_subControlRect(XStyle_defaultStyle(),
                                      XStyleCC_SpinBox, &opt,
                                      XStyleSC_SpinBoxEditField,
                                      (XWidget*)self);
        if (field.width > 0 && field.height > 0) {
            XWidget_setGeometry((XWidget*)self->m_lineEdit,
                                field.x, field.y, field.width, field.height);
            return;
        }
    }
#endif /* XSTYLE_ON */
    {
        int ew = w;
        if (self->m_buttonSymbols !=
            XAbstractSpinBoxButtonSymbols_NoButtons)
            ew -= 16;
        if (ew < 1) ew = 1;
        XWidget_setGeometry((XWidget*)self->m_lineEdit, 0, 0, ew, h);
    }
}

/** @brief 内部创建默认编辑框。 */
static XLineEdit* spinbox_createDefaultLineEdit(XAbstractSpinBox* self)
{
    XLineEdit* edit = XLineEdit_create((XWidget*)self, 0);
    if (!edit) return NULL;
    /* 初创临时占满（此时 self->m_lineEdit 尚未挂上，样式同步须待
       resizeEvent/setButtonSymbols 收口）；真实几何随首次 RESIZE
       经 spinbox_syncEditFieldGeometry 收敛。 */
    XWidget_setGeometry((XWidget*)edit, 0, 0,
                        XWidget_width((XWidget*)self),
                        XWidget_height((XWidget*)self));
    /* 子控件默认可见（QAbstractSpinBox 内嵌编辑框语义）。 */
    XWidget_show((XWidget*)edit);
    XObject_connect_2((XObject*)edit,
                      (size_t)XLineEdit_editingFinished_signal(edit),
                      spinbox_forwardEditingFinished);
    /* 对标 Qt QAbstractSpinBoxPrivate::init 的
       d->edit->setFocusProxy(q)（qabstractspinbox.cpp:691）：容器注册
       焦点代理指向内嵌编辑框。修复「SpinBox 聚焦无可见指示」第一段：
       此前未注册，XWidget_hasFocus(容器) 只认 g_focusWidget==容器
       本体——Tab/点击后焦点实际落内嵌编辑框，容器 hasFocus 恒假，
       XSpinBox_paintEvent 的 HasFocus 位永远置不上。注册后
       hasFocus(容器) 经 deepestFocusProxy 与编辑框持焦同一真值。
       注意此处焦点代理方向与 Qt 相反（Qt 是编辑框代理到容器、焦点
       停容器本体；XGui 取容器代理到编辑框、焦点落编辑框——编辑框
       直持焦点使键入/选区/IME 走自身完整链路，容器经代理判据与
       宿主 update（XWidget_setFocusReason 三段配套）画出聚焦环）。 */
    XWidget_setFocusProxy((XWidget*)self, (XWidget*)edit);
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
    return XStrdup(text ? text : "");
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

/** @brief 尺寸变化：同步内嵌编辑框几何（样式 SC_SpinBoxEditField 边距
 *         口径，见 spinbox_syncEditFieldGeometry）。RESIZE 事件走
 *         ResizeEvent 虚槽（不走 changeEvent），在此同步编辑框。 */
static void VXAbstractSpinBox_resizeEvent(XWidget* self, XEvent* event)
{
    XAbstractSpinBox* spin = (XAbstractSpinBox*)self;
    if (!spin || !event ||
        XEvent_type(event) != XEVENT_TYPE_RESIZE) return;
    if (spin->m_lineEdit)
        spinbox_syncEditFieldGeometry(spin);
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
    int mods;
    int steps;
    int flags;
    if (!spin || !event ||
        XEvent_type(event) != XEVENT_TYPE_KEY_PRESS) return;
    ke = (XKeyEvent*)event;
    key = XKeyEvent_key(ke);
    mods = (int)XKeyEvent_modifiers(ke);

    switch (key) {
    case XKey_Up:
    case XKey_Down: {
        /* 对标 Qt：按住 stepModifier(Ctrl) 时 Up/Down 步进 ×10。 */
        bool up = (key == XKey_Up);
        steps = (mods & (int)XKeyboardModifier_ControlModifier) ? 10 : 1;
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
    case XKey_PageUp:
    case XKey_PageDown: {
        bool up = (key == XKey_PageUp);
        steps = 10;
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
    case XKey_Tab:
    case XKey_Backtab:
        /* Tab/Backtab 不转发内嵌编辑框（单行编辑从不插 Tab）：显式忽略，
         * 让框架焦点遍历迁焦到下一候选（对标 Qt QWidget::event 对 Tab 的
         * 焦点链拦截先于 keyPressEvent，qwidget.cpp:9324 前段）。此前被
         * 编辑框消费致 SpinBox→Slider 焦点链断岛（复扫-3 #41 残）。 */
        XEvent_ignore(event);
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

/** @brief 获得焦点：把焦点事件先转发内嵌编辑框再链父类（对标 Qt
 *         QAbstractSpinBox::focusInEvent 的 d->edit->event(event)）。
 *         编辑框收到焦点事件会同步焦点态并自行重绘——修复 night #40：
 *         Tab 进入时焦点落在微调框本体，父控件带焦重绘把子编辑框像素
 *         抹掉而无人重画，编辑区整白、值文本不可见；鼠标点击路径因
 *         mousePressEvent 直发编辑框（编辑框自取焦点重绘）故正常。
 *         随后 Tab/Backtab 原因进入时全选编辑框文本（对标 Qt 同函数
 *         的 selectAll 分支）。 */
static void VXAbstractSpinBox_focusInEvent(XWidget* self, XEvent* event)
{
    XAbstractSpinBox* spin = (XAbstractSpinBox*)self;
    if (!spin || !event ||
        XEvent_type(event) != XEVENT_TYPE_FOCUS_IN) return;
    if (spin->m_lineEdit)
        XObject_event_base((XObject*)spin->m_lineEdit, event);
    if (((XFocusEvent*)event)->m_reason == XFocusReason_Tab ||
        ((XFocusEvent*)event)->m_reason == XFocusReason_Backtab) {
        if (spin->m_lineEdit)
            XLineEdit_selectAll(spin->m_lineEdit);
    }
    XClass_Parent(XWidget, EXWidget_FocusInEvent,
                  void(*)(XWidget*, XEvent*))((XWidget*)self, event);
}

/** @brief 失去焦点：先解释当前文本，再刷新编辑框显示并转发父类、发射
 *         editingFinished。对标 Qt QAbstractSpinBox::focusOutEvent 的
 *         d->updateEdit()：失焦后父控件无焦重绘同样会抹掉子编辑框像素，
 *         经 updateEdit 链（子类刷新编辑框文本→编辑框重绘）恢复显示。
 *         注意不把失焦事件转发编辑框：编辑框失焦门禁会经既有转发线重复
 *         发射 editingFinished（Qt 中二者是不同信号，XGui 已把编辑框
 *         editingFinished 转发为本控件同名信号，详见
 *         spinbox_forwardEditingFinished）。 */
static void VXAbstractSpinBox_focusOutEvent(XWidget* self, XEvent* event)
{
    XAbstractSpinBox* spin = (XAbstractSpinBox*)self;
    if (!spin || !event) return;
    XAbstractSpinBox_interpretText(spin);
    XAbstractSpinBox_updateEdit_base(spin);
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
    if (self->m_specialValueText) {
        XString_delete_base(self->m_specialValueText);
        self->m_specialValueText = NULL;
    }
    if (other->m_specialValueText)
        self->m_specialValueText =
            XString_create_copy(other->m_specialValueText);
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
    if (self->m_specialValueText) {
        XString_delete_base(self->m_specialValueText);
        self->m_specialValueText = NULL;
    }
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
    if (self->m_specialValueText) {
        XString_delete_base(self->m_specialValueText);
        self->m_specialValueText = NULL;
    }
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
    /* 对标 QAbstractSpinBoxPrivate::init 的 setFocusPolicy（qabstractspinbox.cpp
     * :797 Qt::StrongFocus）：容器可经 Tab 到达，Tab/方向键由 keyPressEvent
     * 分工（Tab 显式 ignore 交焦点遍历，见 VXAbstractSpinBox_keyPressEvent）。
     * 此前默认 NoFocus，SpinBox 不入 Tab 候选集（复扫-3 #41 残）。 */
    XWidget_setFocusPolicy((XWidget*)self, XWidgetFocusPolicy_StrongFocus);

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
    if (self->m_lineEdit) {
        /* 旧编辑框可能在自身按键/输入法回调链中被换；延迟回收
           （对标 Qt QAbstractSpinBox::setLineEdit）。 */
        XObject_deleteLater((XObject*)self->m_lineEdit);
    }
    self->m_lineEdit = lineEdit;
    if (!self->m_lineEdit)
        self->m_lineEdit = spinbox_createDefaultLineEdit(self);
    else {
        /* 对标 Qt QAbstractSpinBox::setLineEdit 的
           d->edit->setFocusProxy(q)：替换编辑框后焦点代理跟迁，
           保持「容器 hasFocus ⇔ 编辑框持焦」判据（与
           spinbox_createDefaultLineEdit 注册点同口径）。 */
        spinbox_syncEditFieldGeometry(self);
        XWidget_setFocusProxy((XWidget*)self, (XWidget*)self->m_lineEdit);
    }
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
    /* 编辑框几何与绘制同源（样式 SC_SpinBoxEditField；NoButtons 全宽）。 */
    spinbox_syncEditFieldGeometry(self);
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
    const char* text;
    if (!self || !self->m_specialValueText) return "";
    text = XString_toUtf8(self->m_specialValueText);
    return text ? text : "";
}

void XAbstractSpinBox_setSpecialValueText(XAbstractSpinBox* self,
                                          const char* text)
{
    if (!self) return;
    if (!text) text = "";
    {
        const char* cur = self->m_specialValueText
            ? XString_toUtf8(self->m_specialValueText) : NULL;
        if (cur && XStrcmp(cur, text) == 0) return;
        if (!cur && text[0] == '\0') return;
    }
    if (!self->m_specialValueText)
        self->m_specialValueText = XString_create();
    if (self->m_specialValueText)
        XString_assign_utf8(self->m_specialValueText, text);
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
