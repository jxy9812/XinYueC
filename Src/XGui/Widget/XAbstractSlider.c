/**
 * @file       XAbstractSlider.c
 * @brief      XAbstractSlider 滑块抽象基类实现（对齐 Qt 6.8
 *             QAbstractSlider）。
 * @details    基类职责：
 *             - 范围/数值/位置属性：setRange 按 Qt qMax 语义收敛
 *               （min 超过 max 时 max 收敛为 min）；setValue 钳位并
 *               同步位置（按下时发射 sliderMoved）；setSliderPosition
 *               与 value 分离（tracking=false 时仅动位置）；
 *             - 动作：triggerAction 按 SliderAction 调整位置 →
 *               actionTriggered → setValue 提交；setRepeatAction 存
 *               字段（长按重复 timer 为后续扩展项）；
 *             - 虚槽：StepBy（基类默认钳位步进 + 发射 valueChanged/
 *               sliderMoved）、SliderChange（默认仅刷新显示）、
 *               StepEnabled（按当前值边界返回位组合）；
 *             - 事件：event 转发父类、keyPressEvent（Up/Right 增、
 *               Down/Left 减、PageUp/PageDown 翻页、Home/End 边界，
 *               invertedControls 翻转方向，经 triggerAction 执行）、
 *               wheelEvent（120 角度=1 单步，余数累积，经 stepBy 执行）、
 *               timerEvent 转发父类、changeEvent（禁用时清重复并抬
 *               起滑块后转发父类）；
 *             - 信号：valueChanged/sliderPressed/sliderMoved/
 *               sliderReleased/rangeChanged/actionTriggered。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "CXinYueConfig.h"
#if XWIDGET_ON && XABSTRACTSLIDER_ON

#include "XAbstractSlider.h"
#include "XEvent.h"
#include "XWidget_Protected.h"
#if XWINDOWEVENT_ON
#include "XWindowEvent.h"
#endif /* XWINDOWEVENT_ON */
#include <stdint.h>

/* ==================== 前向声明 ==================== */
static void VXAbstractSlider_keyPressEvent(XWidget* self, XEvent* event);
static void VXAbstractSlider_timerEvent(XObject* self, XTimerEvent* event);
static void VXAbstractSlider_wheelEvent(XWidget* self, XEvent* event);
static void VXAbstractSlider_changeEvent(XWidget* self, XEvent* event);
static void VXAbstractSlider_stepBy(XAbstractSlider* self, int steps);
static void VXAbstractSlider_sliderChange(XAbstractSlider* self, int change);
static int  VXAbstractSlider_stepEnabled(XAbstractSlider* self);
static void VXAbstractSlider_copy(XAbstractSlider* self,
                                  const XAbstractSlider* other);
static void VXAbstractSlider_move(XAbstractSlider* self,
                                  XAbstractSlider* other);

/* ==================== 内部辅助 ==================== */

/** @brief 发射 void 参数信号（args 传 NULL，对标 XEmitSignal 的
 *         XObject_destroyed_signal 模式；XVarList_Create(0) 会因奇数
 *         参数个数返回 NULL，不能用于空参列表）。 */
static void xslider_emitVoidSignal(XAbstractSlider* self, size_t signal)
{
    if (!self) return;
    if (((XObject*)self)->m_signalSlot)
        XObject_emitSignal((XObject*)self, signal, NULL, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
}

/** @brief 发射 int 参数信号。 */
static void xslider_emitIntSignal(XAbstractSlider* self, size_t signal,
                                  int value)
{
    XVarList* arguments;
    if (!self) return;
    arguments = XVarList_Create(XVar(int, value));
    if (!arguments) return;
    if (((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, arguments, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(arguments);
    }
}

/** @brief 发射 (int,int) 双参数信号（rangeChanged）。 */
static void xslider_emitInt2Signal(XAbstractSlider* self, size_t signal,
                                   int a, int b)
{
    XVarList* arguments;
    if (!self) return;
    arguments = XVarList_Create(XVar(int, a), XVar(int, b));
    if (!arguments) return;
    if (((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, arguments, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(arguments);
    }
}

/** @brief 把值钳位到 [min,max]（setRange 保证 max >= min 恒成立）。 */
static int xslider_clamp(const XAbstractSlider* self, int value)
{
    if (value < self->m_min) return self->m_min;
    if (value > self->m_max) return self->m_max;
    return value;
}

/** @brief 溢出安全的加法：以 int64 累加后钳位到范围（对标 Qt
 *         QAbstractSliderPrivate::overflowSafeAdd）。 */
static int xslider_overflowSafeAdd(const XAbstractSlider* self, int add)
{
    int64_t newValue = (int64_t)self->m_value + (int64_t)add;
    if (add > 0 && newValue < self->m_value) newValue = self->m_max;
    else if (add < 0 && newValue > self->m_value) newValue = self->m_min;
    return xslider_clamp(self, (int)newValue);
}

/* ==================== 事件虚函数 ==================== */

/** @brief 事件总入口：转发父类（对标 QAbstractSlider::event）。 */


/** @brief 键盘按下：方向键单步、PageUp/PageDown 翻页、Home/End 边界；
 *         invertedControls 翻转方向；经 triggerAction 执行（对标
 *         QAbstractSlider::keyPressEvent，不含 RTL 分支）。 */
static void VXAbstractSlider_keyPressEvent(XWidget* self, XEvent* event)
{
    XAbstractSlider* slider = (XAbstractSlider*)self;
    XKeyEvent* ke;
    int key;
    int action = XAbstractSliderSliderAction_NoAction;
    if (!slider || !event ||
        XEvent_type(event) != XEVENT_TYPE_KEY_PRESS) return;
    ke = (XKeyEvent*)event;
    key = XKeyEvent_key(ke);

    switch (key) {
    case XKey_Left:
        action = slider->m_invertedControls
                     ? XAbstractSliderSliderAction_SingleStepAdd
                     : XAbstractSliderSliderAction_SingleStepSub;
        break;
    case XKey_Right:
        action = slider->m_invertedControls
                     ? XAbstractSliderSliderAction_SingleStepSub
                     : XAbstractSliderSliderAction_SingleStepAdd;
        break;
    case XKey_Up:
        action = slider->m_invertedControls
                     ? XAbstractSliderSliderAction_SingleStepSub
                     : XAbstractSliderSliderAction_SingleStepAdd;
        break;
    case XKey_Down:
        action = slider->m_invertedControls
                     ? XAbstractSliderSliderAction_SingleStepAdd
                     : XAbstractSliderSliderAction_SingleStepSub;
        break;
    case XKey_PageUp:
        action = slider->m_invertedControls
                     ? XAbstractSliderSliderAction_PageStepSub
                     : XAbstractSliderSliderAction_PageStepAdd;
        break;
    case XKey_PageDown:
        action = slider->m_invertedControls
                     ? XAbstractSliderSliderAction_PageStepAdd
                     : XAbstractSliderSliderAction_PageStepSub;
        break;
    case XKey_Home:
        action = XAbstractSliderSliderAction_ToMinimum;
        break;
    case XKey_End:
        action = XAbstractSliderSliderAction_ToMaximum;
        break;
    default:
        XEvent_ignore(event);
        break;
    }
    if (action != XAbstractSliderSliderAction_NoAction) {
        XAbstractSlider_triggerAction(slider, action);
        XEvent_accept(event);
    }
}

/** @brief 定时器事件：转发父类（长按自动重复的 timer 为后续扩展项）。 */
static void VXAbstractSlider_timerEvent(XObject* self, XTimerEvent* event)
{
    if (self && event) {
        XClass_Parent(XObject, EXObject_TimerEvent,
                      void(*)(XObject*, XTimerEvent*))(self, event);
    }
}

/** @brief 滚轮事件：角度增量累积，每 120 角度=1 单步（余数保留），
 *         invertedControls 翻转方向；经 stepBy 虚槽执行（对标
 *         QAbstractSlider::wheelEvent 的简化实现）。 */
static void VXAbstractSlider_wheelEvent(XWidget* self, XEvent* event)
{
    XAbstractSlider* slider = (XAbstractSlider*)self;
    int steps = 0;
    if (!slider || !event || XEvent_type(event) != XEVENT_TYPE_WHEEL) return;
#if XWINDOWEVENT_ON
    {
        XWheelEvent* we = (XWheelEvent*)event;
        XPoint delta = XWheelEvent_angleDelta(we);
        int dy = (delta.y != 0) ? delta.y : delta.x;
        slider->m_wheelDeltaRemainder += dy;
        steps = slider->m_wheelDeltaRemainder / 120;
        slider->m_wheelDeltaRemainder -= steps * 120;
        if (slider->m_invertedControls) steps = -steps;
    }
#endif /* XWINDOWEVENT_ON */
    if (steps != 0) XAbstractSlider_stepBy_base(slider, steps);
    XEvent_accept(event);
}

/** @brief 变更事件：禁用时清除重复动作并抬起滑块，再转发父类（对标
 *         QAbstractSlider::changeEvent）。 */
static void VXAbstractSlider_changeEvent(XWidget* self, XEvent* event)
{
    XAbstractSlider* slider = (XAbstractSlider*)self;
    XEventType type = event ? XEvent_type(event) : XEVENT_TYPE_NONE;
    if (type == XEVENT_TYPE_ENABLED_CHANGE && !XWidget_isEnabled(self)) {
        slider->m_repeatAction = XAbstractSliderSliderAction_NoAction;
        XAbstractSlider_setSliderDown(slider, false);
    }
    XClass_Parent(XWidget, EXWidget_ChangeEvent,
                  void(*)(XWidget*, XEvent*))((XWidget*)self, event);
}

/* ==================== 虚槽实现 ==================== */

/** @brief StepBy 默认实现：钳位步进 + 同步位置 + 按下时 sliderMoved +
 *         变化时 valueChanged（子类可重载）。 */
static void VXAbstractSlider_stepBy(XAbstractSlider* self, int steps)
{
    int newValue;
    if (!self) return;
    newValue = xslider_overflowSafeAdd(self, steps);
    newValue = xslider_clamp(self, newValue);
    if (newValue == self->m_value) return;
    self->m_value = newValue;
    self->m_position = newValue;
    if (self->m_sliderDown)
        xslider_emitIntSignal(self,
                              (size_t)XAbstractSlider_sliderMoved_signal(self),
                              newValue);
    XAbstractSlider_sliderChange_base(
        self, XAbstractSliderSliderChange_ValueChange);
    xslider_emitIntSignal(self,
                          (size_t)XAbstractSlider_valueChanged_signal(self),
                          newValue);
    XWidget_update((XWidget*)self);
}

/** @brief SliderChange 默认实现：仅刷新显示（忽略 change 参数，对标
 *         QAbstractSlider::sliderChange 默认行为）。 */
static void VXAbstractSlider_sliderChange(XAbstractSlider* self, int change)
{
    (void)change;
    if (self) XWidget_update((XWidget*)self);
}

/** @brief StepEnabled 默认实现：按当前值边界返回位组合。 */
static int VXAbstractSlider_stepEnabled(XAbstractSlider* self)
{
    int flags = XAbstractSliderStepEnabledFlag_StepNone;
    if (!self) return flags;
    if (self->m_value > self->m_min)
        flags |= XAbstractSliderStepEnabledFlag_StepDownEnabled;
    if (self->m_value < self->m_max)
        flags |= XAbstractSliderStepEnabledFlag_StepUpEnabled;
    return flags;
}

/** @brief 深拷贝：基类 XWidget 深拷贝后复制全部滑块标量字段。 */
static void VXAbstractSlider_copy(XAbstractSlider* self,
                                  const XAbstractSlider* other)
{
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XAbstractSlider_init(self, NULL, 0);
    XClass_Parent(XWidget, EXClass_Copy,
                  void(*)(XWidget*, const XWidget*))((XWidget*)self,
                                                     (const XWidget*)other);
    self->m_min = other->m_min;
    self->m_max = other->m_max;
    self->m_value = other->m_value;
    self->m_position = other->m_position;
    self->m_orientation = other->m_orientation;
    self->m_pageStep = other->m_pageStep;
    self->m_singleStep = other->m_singleStep;
    self->m_tracking = other->m_tracking;
    self->m_sliderDown = other->m_sliderDown;
    self->m_invertedAppearance = other->m_invertedAppearance;
    self->m_invertedControls = other->m_invertedControls;
    self->m_repeatAction = other->m_repeatAction;
    self->m_repeatActionThreshold = other->m_repeatActionThreshold;
    self->m_repeatActionTime = other->m_repeatActionTime;
    self->m_wheelDeltaRemainder = other->m_wheelDeltaRemainder;
    self->m_blockTracking = other->m_blockTracking;
}

/** @brief 移动语义：基类移动后转移字段，源对象归构造默认值。 */
static void VXAbstractSlider_move(XAbstractSlider* self,
                                  XAbstractSlider* other)
{
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XAbstractSlider_init(self, NULL, 0);
    XClass_Parent(XWidget, EXClass_Move,
                  void(*)(XWidget*, XWidget*))((XWidget*)self,
                                               (XWidget*)other);
    self->m_min = other->m_min;
    self->m_max = other->m_max;
    self->m_value = other->m_value;
    self->m_position = other->m_position;
    self->m_orientation = other->m_orientation;
    self->m_pageStep = other->m_pageStep;
    self->m_singleStep = other->m_singleStep;
    self->m_tracking = other->m_tracking;
    self->m_sliderDown = other->m_sliderDown;
    self->m_invertedAppearance = other->m_invertedAppearance;
    self->m_invertedControls = other->m_invertedControls;
    self->m_repeatAction = other->m_repeatAction;
    self->m_repeatActionThreshold = other->m_repeatActionThreshold;
    self->m_repeatActionTime = other->m_repeatActionTime;
    self->m_wheelDeltaRemainder = other->m_wheelDeltaRemainder;
    self->m_blockTracking = other->m_blockTracking;
    other->m_min = 0;
    other->m_max = 99;
    other->m_value = 0;
    other->m_position = 0;
    other->m_orientation = XAbstractSliderOrientation_Horizontal;
    other->m_pageStep = 10;
    other->m_singleStep = 1;
    other->m_tracking = true;
    other->m_sliderDown = false;
    other->m_invertedAppearance = false;
    other->m_invertedControls = false;
    other->m_repeatAction = XAbstractSliderSliderAction_NoAction;
    other->m_repeatActionThreshold = 0;
    other->m_repeatActionTime = 0;
    other->m_wheelDeltaRemainder = 0;
    other->m_blockTracking = false;
}

/* ==================== 生命周期 ==================== */

XVtable* XAbstractSlider_class_init(void)
{
    /* 新增虚槽（StepBy/SliderChange/StepEnabled）必须经 ADD_FUNC_LIST
       追加到 XWidget 槽位之后并计入 size：XVTABLE_OVERLOAD 只写槽位
       不更新 size，派生类（XSlider）经 append_vtable 继承时会把新槽
       遗漏为 NULL（对标 XAbstractButton_class_init 的 protectedSlots
       模式）。 */
    void* protectedSlots[] = {
        VXAbstractSlider_stepBy,
        VXAbstractSlider_sliderChange,
        VXAbstractSlider_stepEnabled
    };

    XVTABLE_INIT_DEFAULT(XAbstractSlider)
    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_ADD_FUNC_LIST_DEFAULT(protectedSlots);

    XVTABLE_OVERLOAD_DEFAULT(EXWidget_KeyPressEvent, VXAbstractSlider_keyPressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_TimerEvent, VXAbstractSlider_timerEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_WheelEvent, VXAbstractSlider_wheelEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ChangeEvent, VXAbstractSlider_changeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXAbstractSlider_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXAbstractSlider_move);

    return XVTABLE_DEFAULT;
}

void XAbstractSlider_init(XAbstractSlider* self, XWidget* parent,
                          XWidgetFlags flags)
{
    if (!self) return;
    XWidget_init((XWidget*)self, parent, flags);
    XClassSetVtable(self, XAbstractSlider);

    self->m_min = 0;
    self->m_max = 99;
    self->m_value = 0;
    self->m_position = 0;
    self->m_orientation = XAbstractSliderOrientation_Horizontal;
    self->m_pageStep = 10;
    self->m_singleStep = 1;
    self->m_tracking = true;
    self->m_sliderDown = false;
    self->m_invertedAppearance = false;
    self->m_invertedControls = false;
    self->m_repeatAction = XAbstractSliderSliderAction_NoAction;
    self->m_repeatActionThreshold = 0;
    self->m_repeatActionTime = 0;
    self->m_wheelDeltaRemainder = 0;
    self->m_blockTracking = false;
}

XAbstractSlider* XAbstractSlider_create_ex(XMemoryType memory,
                                           XWidget* parent,
                                           XWidgetFlags flags)
{
    XAbstractSlider* self =
        (XAbstractSlider*)XMemory_malloc(sizeof(XAbstractSlider), memory);
    if (!self) return NULL;
    XAbstractSlider_init(self, parent, flags);
    Set_Class_Memory(self, memory); Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 属性（对标 QAbstractSlider public API） ==================== */

int XAbstractSlider_orientation(const XAbstractSlider* self)
{
    return self ? self->m_orientation : XAbstractSliderOrientation_Horizontal;
}

void XAbstractSlider_setOrientation(XAbstractSlider* self, int orientation)
{
    if (!self || self->m_orientation == orientation) return;
    if (orientation != XAbstractSliderOrientation_Horizontal &&
        orientation != XAbstractSliderOrientation_Vertical)
        return;
    self->m_orientation = orientation;
    XWidget_update((XWidget*)self);
}

int XAbstractSlider_minimum(const XAbstractSlider* self)
{
    return self ? self->m_min : 0;
}

void XAbstractSlider_setMinimum(XAbstractSlider* self, int min)
{
    if (!self) return;
    /* Qt 语义：newMax = qMax(max, min) 后 setRange。 */
    XAbstractSlider_setRange(self, min,
                             self->m_max > min ? self->m_max : min);
}

int XAbstractSlider_maximum(const XAbstractSlider* self)
{
    return self ? self->m_max : 99;
}

void XAbstractSlider_setMaximum(XAbstractSlider* self, int max)
{
    if (!self) return;
    /* Qt 语义：newMin = qMin(min, max) 后 setRange。 */
    XAbstractSlider_setRange(self,
                             self->m_min < max ? self->m_min : max, max);
}

void XAbstractSlider_setRange(XAbstractSlider* self, int min, int max)
{
    int oldMin;
    int oldMax;
    if (!self) return;
    oldMin = self->m_min;
    oldMax = self->m_max;
    self->m_min = min;
    /* Qt 语义：maximum = qMax(min, max)；min 超过 max 时 max 收敛为 min。 */
    self->m_max = min > max ? min : max;
    if (oldMin != self->m_min || oldMax != self->m_max) {
        XAbstractSlider_sliderChange_base(
            self, XAbstractSliderSliderChange_RangeChange);
        xslider_emitInt2Signal(self,
                               (size_t)XAbstractSlider_rangeChanged_signal(self),
                               self->m_min, self->m_max);
        XAbstractSlider_setValue(self, self->m_value); /* 重新钳位 */
    }
}

int XAbstractSlider_singleStep(const XAbstractSlider* self)
{
    return self ? self->m_singleStep : 1;
}

void XAbstractSlider_setSingleStep(XAbstractSlider* self, int step)
{
    if (!self) return;
    if (step < 0) step = -step; /* Qt：setSteps 取 qAbs。 */
    if (self->m_singleStep == step) return;
    self->m_singleStep = step;
    XAbstractSlider_sliderChange_base(
        self, XAbstractSliderSliderChange_StepsChange);
}

int XAbstractSlider_pageStep(const XAbstractSlider* self)
{
    return self ? self->m_pageStep : 10;
}

void XAbstractSlider_setPageStep(XAbstractSlider* self, int step)
{
    if (!self) return;
    if (step < 0) step = -step; /* Qt：setSteps 取 qAbs。 */
    if (self->m_pageStep == step) return;
    self->m_pageStep = step;
    XAbstractSlider_sliderChange_base(
        self, XAbstractSliderSliderChange_StepsChange);
}

bool XAbstractSlider_hasTracking(const XAbstractSlider* self)
{
    return self ? self->m_tracking : true;
}

void XAbstractSlider_setTracking(XAbstractSlider* self, bool enable)
{
    if (self) self->m_tracking = enable;
}

void XAbstractSlider_setSliderDown(XAbstractSlider* self, bool down)
{
    bool doEmit;
    if (!self) return;
    doEmit = (self->m_sliderDown != down);
    self->m_sliderDown = down;
    if (doEmit) {
        if (down)
            xslider_emitVoidSignal(
                self, (size_t)XAbstractSlider_sliderPressed_signal(self));
        else
            xslider_emitVoidSignal(
                self, (size_t)XAbstractSlider_sliderReleased_signal(self));
    }
    /* 抬起且位置≠值：提交位置到值（tracking=false 场景）。 */
    if (!down && self->m_position != self->m_value)
        XAbstractSlider_triggerAction(self,
                                      XAbstractSliderSliderAction_Move);
}

bool XAbstractSlider_isSliderDown(const XAbstractSlider* self)
{
    return self ? self->m_sliderDown : false;
}

int XAbstractSlider_sliderPosition(const XAbstractSlider* self)
{
    return self ? self->m_position : 0;
}

void XAbstractSlider_setSliderPosition(XAbstractSlider* self, int position)
{
    if (!self) return;
    position = xslider_clamp(self, position);
    if (position == self->m_position) return;
    self->m_position = position;
    if (!self->m_tracking) XWidget_update((XWidget*)self);
    if (self->m_sliderDown)
        xslider_emitIntSignal(self,
                              (size_t)XAbstractSlider_sliderMoved_signal(self),
                              position);
    if (self->m_tracking && !self->m_blockTracking)
        XAbstractSlider_triggerAction(self, XAbstractSliderSliderAction_Move);
}

bool XAbstractSlider_invertedAppearance(const XAbstractSlider* self)
{
    return self ? self->m_invertedAppearance : false;
}

void XAbstractSlider_setInvertedAppearance(XAbstractSlider* self,
                                           bool inverted)
{
    if (!self || self->m_invertedAppearance == inverted) return;
    self->m_invertedAppearance = inverted;
    XWidget_update((XWidget*)self);
}

bool XAbstractSlider_invertedControls(const XAbstractSlider* self)
{
    return self ? self->m_invertedControls : false;
}

void XAbstractSlider_setInvertedControls(XAbstractSlider* self, bool inverted)
{
    if (self) self->m_invertedControls = inverted;
}

int XAbstractSlider_value(const XAbstractSlider* self)
{
    return self ? self->m_value : 0;
}

void XAbstractSlider_setValue(XAbstractSlider* self, int value)
{
    bool emitValueChanged;
    if (!self) return;
    value = xslider_clamp(self, value);
    if (self->m_value == value && self->m_position == value) return;
    emitValueChanged = (value != self->m_value);
    self->m_value = value;
    if (self->m_position != value) {
        self->m_position = value;
        if (self->m_sliderDown)
            xslider_emitIntSignal(
                self, (size_t)XAbstractSlider_sliderMoved_signal(self),
                self->m_position);
    }
    XAbstractSlider_sliderChange_base(
        self, XAbstractSliderSliderChange_ValueChange);
    if (emitValueChanged)
        xslider_emitIntSignal(self,
                              (size_t)XAbstractSlider_valueChanged_signal(self),
                              value);
}

/* ==================== 动作（对标 QAbstractSlider） ==================== */

void XAbstractSlider_triggerAction(XAbstractSlider* self, int action)
{
    if (!self) return;
    self->m_blockTracking = true;
    switch (action) {
    case XAbstractSliderSliderAction_SingleStepAdd:
        XAbstractSlider_setSliderPosition(self,
                                          xslider_overflowSafeAdd(
                                              self, self->m_singleStep));
        break;
    case XAbstractSliderSliderAction_SingleStepSub:
        XAbstractSlider_setSliderPosition(self,
                                          xslider_overflowSafeAdd(
                                              self, -self->m_singleStep));
        break;
    case XAbstractSliderSliderAction_PageStepAdd:
        XAbstractSlider_setSliderPosition(self,
                                          xslider_overflowSafeAdd(
                                              self, self->m_pageStep));
        break;
    case XAbstractSliderSliderAction_PageStepSub:
        XAbstractSlider_setSliderPosition(self,
                                          xslider_overflowSafeAdd(
                                              self, -self->m_pageStep));
        break;
    case XAbstractSliderSliderAction_ToMinimum:
        XAbstractSlider_setSliderPosition(self, self->m_min);
        break;
    case XAbstractSliderSliderAction_ToMaximum:
        XAbstractSlider_setSliderPosition(self, self->m_max);
        break;
    case XAbstractSliderSliderAction_Move:
    case XAbstractSliderSliderAction_NoAction:
    default:
        break;
    }
    xslider_emitIntSignal(self,
                          (size_t)XAbstractSlider_actionTriggered_signal(self),
                          action);
    self->m_blockTracking = false;
    /* 位置提交到值（发射 valueChanged）。 */
    XAbstractSlider_setValue(self, self->m_position);
}

void XAbstractSlider_setRepeatAction(XAbstractSlider* self, int action,
                                     int thresholdTime, int repeatTime)
{
    if (!self) return;
    self->m_repeatAction = action;
    if (action == XAbstractSliderSliderAction_NoAction) {
        self->m_repeatActionThreshold = 0;
        self->m_repeatActionTime = 0;
    } else {
        self->m_repeatActionThreshold = thresholdTime;
        self->m_repeatActionTime = repeatTime;
    }
}

int XAbstractSlider_repeatAction(const XAbstractSlider* self)
{
    return self ? self->m_repeatAction
                : XAbstractSliderSliderAction_NoAction;
}

/* ==================== 虚槽调度入口（*_base） ==================== */

void XAbstractSlider_stepBy_base(XAbstractSlider* self, int steps)
{
    if (!self) return;
    XClassGetVirtualFunc(self, EXAbstractSlider_StepBy,
                         void(*)(XAbstractSlider*, int))(self, steps);
}

void XAbstractSlider_sliderChange_base(XAbstractSlider* self, int change)
{
    if (!self) return;
    if (!XClassGetVirtualFunc(self, EXAbstractSlider_SliderChange, bool))
        return;
    XClassGetVirtualFunc(self, EXAbstractSlider_SliderChange,
                         void(*)(XAbstractSlider*, int))(self, change);
}

int XAbstractSlider_stepEnabled_base(XAbstractSlider* self)
{
    if (!self) return XAbstractSliderStepEnabledFlag_StepNone;
    if (!XClassGetVirtualFunc(self, EXAbstractSlider_StepEnabled, bool))
        return XAbstractSliderStepEnabledFlag_StepNone;
    return XClassGetVirtualFunc(self, EXAbstractSlider_StepEnabled,
                                int(*)(XAbstractSlider*))(self);
}

/* ==================== 信号 ==================== */

void* XAbstractSlider_valueChanged_signal(XAbstractSlider* self)
{
    (void)self;
    return (void*)(size_t)XAbstractSlider_valueChanged_signal;
}

void* XAbstractSlider_sliderPressed_signal(XAbstractSlider* self)
{
    (void)self;
    return (void*)(size_t)XAbstractSlider_sliderPressed_signal;
}

void* XAbstractSlider_sliderMoved_signal(XAbstractSlider* self)
{
    (void)self;
    return (void*)(size_t)XAbstractSlider_sliderMoved_signal;
}

void* XAbstractSlider_sliderReleased_signal(XAbstractSlider* self)
{
    (void)self;
    return (void*)(size_t)XAbstractSlider_sliderReleased_signal;
}

void* XAbstractSlider_rangeChanged_signal(XAbstractSlider* self)
{
    (void)self;
    return (void*)(size_t)XAbstractSlider_rangeChanged_signal;
}

void* XAbstractSlider_actionTriggered_signal(XAbstractSlider* self)
{
    (void)self;
    return (void*)(size_t)XAbstractSlider_actionTriggered_signal;
}

#endif /* XWIDGET_ON && XABSTRACTSLIDER_ON */
