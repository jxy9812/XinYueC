/******************************************************************************
 * @file       XPushButton.c
 * @brief      XPushButton 按钮控件实现（对标 Qt 6.8 QPushButton / QAbstractButton）。
 * @details    实现要点：
 *             - 继承 XWidget，把 QAbstractButton 与 QPushButton 常用公共行为
 *               折叠进单类；生命周期走 XClass 体系：XPushButton_init/create_ex
 *               挂 XPushButton 虚表；
 *             - 文本/图标/选中/按下/自动重复/自动默认等状态位全部对齐
 *               Qt 6.8 默认值；setCheckable(false) 清空 checked，setDefault 只
 *               改 defaultButton，autoDefault() 在 Auto 三态下解析父对话框链；
 *             - 点击：click() 禁用直接返回，内部按下/释放流程按
 *               QAbstractButton::click 重放 pressed/released/clicked/toggled；
 *             - 鼠标左键命中按下/移出释放/移回按下按 QAbstractButton 语义
 *               处理；键盘 Space 按键模拟按下/释放，Return/Enter 仅在默认或
 *               autoDefault 生效时触发 click；
 *             - 绘制：XPainter 输出 raised/sunken/flat 外观，文本经当前
 *               XFont（内置 8x16 点阵字库回退），图标走 XIcon_paint；
 *             - 不依赖任何平台 API；嵌入式由 XPUSHBUTTON_ON 裁剪。
 * @note       近似边界：显式 QButtonGroup 登记未实现；同一父控件的
 *             autoExclusive 互斥已按 Qt 规则处理；样式 bevel、快捷键
 *             与真实平台菜单弹层未实现；菜单关联接口按 Qt 借用语义提供，
 *             showMenu 仅同步按下状态并重绘，不进入阻塞式弹层循环。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XPushButton.h"
#include "XWidget_Protected.h"
#include "XPainter.h"
#include "XPalette.h"
#include "XColor.h"
#include "XFont.h"
#include "XVarList.h"
#include "XWindowEvent.h"
#include "XMemory.h"
#include "XString.h"
#include "XAlignment.h"
#include "XVector.h"
#include <string.h>

#if XWIDGET_ON && XPUSHBUTTON_ON

/* ==================== 内部工具 ==================== */

static void pushbutton_stopRepeatTimer(XPushButton* self);
static void pushbutton_startRepeatTimer(XPushButton* self, int interval);
static void pushbutton_stopAnimateTimer(XPushButton* self);
static void pushbutton_startAnimateTimer(XPushButton* self);

/** @brief 从控件调色板读取颜色；调色板裁剪时回退黑色。 */
static uint32_t pushbutton_color(const XPushButton* self,
                                 XPaletteColorGroup group,
                                 XPaletteColorRole role)
{
#if XPALETTE_ON
    XPalette palette;
    XColor color;
    if (!self) return 0xFF000000u;
    palette = XWidget_palette((const XWidget*)self);
    color = XPalette_color(&palette, group, role);
    return XColor_rgba(&color);
#else
    (void)self;
    (void)group;
    (void)role;
    return 0xFF000000u;
#endif /* XPALETTE_ON */
}

/** @brief 内部按下状态切换（仅在状态变化时重绘并发射按下/释放信号）。 */
static void pushbutton_setDownInternal(XPushButton* self, bool down)
{
    if (!self || self->m_down == down) return;
    self->m_down = down;
    XWidget_repaint((XWidget*)self);
    if (down && self->m_autoRepeat)
        pushbutton_startRepeatTimer(self, self->m_autoRepeatDelay);
    else if (!down) {
        pushbutton_stopRepeatTimer(self);
    }
    if (down)
        XPushButton_pressed_signal(self);
    else
        XPushButton_released_signal(self);
}

/** @brief 停止按钮当前自动重复定时器。 */
static void pushbutton_stopRepeatTimer(XPushButton* self)
{
    if (!self || self->m_repeatTimer == XTIMER_INVALID_ID)
        return;
    XObject_killTimer((XObject*)self, self->m_repeatTimer);
    self->m_repeatTimer = XTIMER_INVALID_ID;
}

/** @brief 按指定毫秒启动自动重复定时器；非正间隔按当前调度器能力降级。 */
static void pushbutton_startRepeatTimer(XPushButton* self, int interval)
{
    uint64_t value;
    if (!self || !self->m_autoRepeat || !self->m_down)
        return;
    pushbutton_stopRepeatTimer(self);
    /* XAbstractEventDispatcher 当前拒绝零间隔；Qt 6.8 允许调用方保存
       原值，因此仅在真正注册时把非正值钳制为 1ms。 */
    value = interval > 0 ? (uint64_t)interval : 1u;
    self->m_repeatTimer = XObject_startTimer_ms((XObject*)self, value,
                                                 XTimerType_PreciseTimer);
}

/** @brief 停止按钮当前动画点击释放定时器。 */
static void pushbutton_stopAnimateTimer(XPushButton* self)
{
    if (!self || self->m_animateTimer == XTIMER_INVALID_ID)
        return;
    XObject_killTimer((XObject*)self, self->m_animateTimer);
    self->m_animateTimer = XTIMER_INVALID_ID;
}

/** @brief 启动或重置 100ms 动画点击释放定时器。 */
static void pushbutton_startAnimateTimer(XPushButton* self)
{
    if (!self)
        return;
    pushbutton_stopAnimateTimer(self);
    self->m_animateTimer = XObject_startTimer_ms((XObject*)self, 100u,
                                                  XTimerType_PreciseTimer);
}

/** @brief 发送一次自动重复点击，保持按钮仍处于按下状态。 */
static void pushbutton_repeatTimeout(XPushButton* self)
{
    bool checked;
    if (!self || !self->m_down || !XWidget_isEnabled((XWidget*)self))
        return;
    checked = self->m_checked;
    if (self->m_checkable)
        XPushButton_setChecked(self, !checked);
    XPushButton_released_signal(self);
    XPushButton_clicked_signal(self, self->m_checked);
    if (self->m_down)
        XPushButton_pressed_signal(self);
}

/** @brief 发射无参数信号；无接收者时立即释放参数列表。 */
static void pushbutton_emitVoid(XPushButton* self, size_t signal)
{
    XVarList* args = XVarList_create(0);
    if (!args) return;
    if (self && ((XObject*)self)->m_signalSlot)
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    else
        XVarList_delete(args);
}

/** @brief 发射携带 bool 参数的信号；无接收者时立即释放参数列表。 */
static void pushbutton_emitBool(XPushButton* self, size_t signal, bool value)
{
    XVarList* args = XVarList_Create(XVar(bool, value));
    if (!args) return;
    if (self && ((XObject*)self)->m_signalSlot)
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    else
        XVarList_delete(args);
}

/** @brief 是否三态 Auto 且当前上下文判定为对话框默认按钮。
 * @details 对标 Qt 6.8 QPushButtonPrivate::dialogParent/autoDefault：
 *          沿父链向上遍历到第一个窗口前，若命中窗口类型为 Dialog 的
 *          父控件则返回 true；按钮自身为窗口或父链无对话框返回 false。 */
static bool pushbutton_autoDefaultActive(const XPushButton* self)
{
    const XWidget* p = (const XWidget*)self;

    while (p && !XWidget_isWindow(p)) {
        p = XWidget_parentWidget(p);
        if (p && XWidget_windowType(p) == XWindowType_Dialog)
            return true;
    }
    return false;
}

/** @brief 是否把当前尺寸视为显式图标尺寸。 */
static bool pushbutton_hasIconSize(const XPushButton* self)
{
    return self && XSize_isValid(&self->m_iconSize) &&
           self->m_iconSize.width > 0 && self->m_iconSize.height > 0;
}

/** @brief 取消同一父控件下其它自动互斥按钮的选中状态。 */
static void pushbutton_uncheckAutoExclusiveSiblings(XPushButton* self)
{
    XWidget* parent;
    const XVector* children;
    int64_t count;
    int64_t i;
    if (!self || !self->m_autoExclusive || !self->m_checkable)
        return;
    parent = XWidget_parentWidget((const XWidget*)self);
    if (!parent)
        return;
    children = XObject_children((const XObject*)parent);
    if (!children)
        return;
    count = (int64_t)XVector_size_base((const XContainer*)children);
    for (i = 0; i < count; ++i)
    {
        XObject* object = *(XObject**)XVector_at_base(children, i);
        XPushButton* sibling;
        if (!object || object == (XObject*)self || !object->is_widget ||
            XClassGetVtable(object) != XPushButton_class_init())
            continue;
        sibling = (XPushButton*)object;
        if (sibling->m_autoExclusive && sibling->m_checkable &&
            sibling->m_checked)
            XPushButton_setChecked(sibling, false);
    }
}

/** @brief 判断按钮是否处于包含其它自动互斥兄弟的独占组。 */
static bool pushbutton_isOnlyAutoExclusiveMember(const XPushButton* self)
{
    XWidget* parent;
    const XVector* children;
    bool hasSibling = false;
    int64_t count;
    int64_t i;
    if (!self || !self->m_autoExclusive)
        return false;
    parent = XWidget_parentWidget((const XWidget*)self);
    /* Qt 无父控件时没有 autoExclusive 组，允许取消自身选中。 */
    if (!parent)
        return false;
    children = XObject_children((const XObject*)parent);
    if (!children)
        return false;
    count = (int64_t)XVector_size_base((const XContainer*)children);
    for (i = 0; i < count; ++i)
    {
        XObject* object = *(XObject**)XVector_at_base(children, i);
        XPushButton* sibling;
        if (!object || object == (XObject*)self || !object->is_widget ||
            XClassGetVtable(object) != XPushButton_class_init())
            continue;
        sibling = (XPushButton*)object;
        if (!sibling->m_autoExclusive)
            continue;
        hasSibling = true;
        /* 其它同组按钮已选中时，当前按钮不是 queryCheckedButton()，
           因此 Qt 允许当前按钮取消；只有自身是组内唯一选中项才禁止。 */
        if (sibling->m_checked)
            return false;
    }
    return hasSibling;
}

/** @brief 返回按钮图标用于尺寸提示的渲染尺寸（未显式设置按 PM_ButtonIconSize=16）。 */
static XSize pushbutton_hintIconSize(const XPushButton* self)
{
    XSize out;
    XSize_init(&out, 16, 16);
    if (pushbutton_hasIconSize(self))
        out = self->m_iconSize;
    return out;
}

/** @brief 按 Qt 6.8 QPushButton::sizeHint 内容语义计算建议尺寸。 */
static XSize pushbutton_computeSizeHint(const XPushButton* self)
{
    XSize out;
    XSize iconSize;
    const char* text;
    bool hasIcon;
    bool empty;
    int w = 0;
    int h = 0;
    XFont font;
    if (!self) { XSize_init(&out, -1, -1); return out; }
    hasIcon = !XIcon_isNull(&self->m_icon);
    if (hasIcon) {
        iconSize = pushbutton_hintIconSize(self);
        w += iconSize.width + 4;
        if (h < iconSize.height) h = iconSize.height;
    }
    font = XWidget_font((XWidget*)self);
    text = self->m_text ? XString_toUtf8(self->m_text) : NULL;
    if (!text) text = "";
    empty = text[0] == '\0';
    if (empty) {
        int placeholder;
        placeholder = XPainter_textWidth(&font, "XXXX");
        if (!w) w += placeholder;
        if (!h) h = XPainter_textHeight(&font);
    } else {
        w += XPainter_textWidth(&font, text);
        if (h < XPainter_textHeight(&font))
            h = XPainter_textHeight(&font);
    }
    XFont_deinit_base(&font);
    if (self->m_menu)
        w += 12; /* PM_MenuButtonIndicator */
    w += 6 + 4; /* PM_ButtonMargin + PM_DefaultFrameWidth * 2 */
    h += 6 + 4;
    XSize_init(&out, w, h);
    return out;
}

/** @brief 刷新基类 XWidget 的 sizeHint/minimumSizeHint 存储位。 */
static void pushbutton_refreshSizeHint(XPushButton* self)
{
    XSize hint;
    if (!self) return;
    hint = pushbutton_computeSizeHint(self);
    XWidget_setSizeHint((XWidget*)self, &hint);
    XWidget_setMinimumSizeHint((XWidget*)self, &hint);
}

/* ==================== 信号（对标 QAbstractButton/QPushButton） ==================== */

void* XPushButton_pressed_signal(XPushButton* self)
{
    if (!self) return (void*)(size_t)XPushButton_pressed_signal;
    pushbutton_emitVoid(self, (size_t)XPushButton_pressed_signal);
    return (void*)(size_t)XPushButton_pressed_signal;
}

void* XPushButton_released_signal(XPushButton* self)
{
    if (!self) return (void*)(size_t)XPushButton_released_signal;
    pushbutton_emitVoid(self, (size_t)XPushButton_released_signal);
    return (void*)(size_t)XPushButton_released_signal;
}

void* XPushButton_clicked_signal(XPushButton* self, bool checked)
{
    if (!self) return (void*)(size_t)XPushButton_clicked_signal;
    pushbutton_emitBool(self, (size_t)XPushButton_clicked_signal, checked);
    return (void*)(size_t)XPushButton_clicked_signal;
}

void* XPushButton_toggled_signal(XPushButton* self, bool checked)
{
    if (!self) return (void*)(size_t)XPushButton_toggled_signal;
    pushbutton_emitBool(self, (size_t)XPushButton_toggled_signal, checked);
    return (void*)(size_t)XPushButton_toggled_signal;
}

/* ==================== 生命周期（对标 QPushButton 构造/析构） ==================== */

static void VXPushButton_copy(XPushButton* self, const XPushButton* other);
static void VXPushButton_move(XPushButton* self, XPushButton* other);
static void VXPushButton_deinit(XPushButton* self);

void XPushButton_init(XPushButton* self, XWidget* parent, XWidgetFlags flags)
{
    XWidgetSizePolicy policy;
    if (!self) return;
    memset(self, 0, sizeof(XPushButton));
    XWidget_init(&self->m_base, parent, flags);
    XClassSetVtable(self, XPushButton);
    self->m_text = XString_create();
    XIcon_init(&self->m_icon);
    self->m_iconSize.width = 0;
    self->m_iconSize.height = 0;
    self->m_checkable = false;
    self->m_checked = false;
    self->m_down = false;
    self->m_pressed = false;
    self->m_autoRepeat = false;
    self->m_autoExclusive = false;
    self->m_flat = false;
    self->m_defaultButton = false;
    self->m_autoDefault = XPushButtonAutoDefault_Auto;
    self->m_autoRepeatDelay = 300;
    self->m_autoRepeatInterval = 100;
    self->m_repeatTimer = XTIMER_INVALID_ID;
    self->m_animateTimer = XTIMER_INVALID_ID;
    policy = XWidgetSizePolicy_create_ex(XWidgetSizePolicy_Minimum,
                                         XWidgetSizePolicy_Fixed,
                                         XWidgetSizePolicyControl_PushButton);
    XWidget_setSizePolicyFull((XWidget*)self, &policy);
    XWidget_setForegroundRole((XWidget*)self, XPaletteColorRole_ButtonText);
    XWidget_setBackgroundRole((XWidget*)self, XPaletteColorRole_Button);
    XWidget_setFocusPolicy((XWidget*)self, XWidgetFocusPolicy_StrongFocus);
    pushbutton_refreshSizeHint(self);
}

XPushButton* XPushButton_create_ex(XMemoryType memory, XWidget* parent,
                                   XWidgetFlags flags)
{
    XPushButton* self = (XPushButton*)XMemory_malloc(sizeof(XPushButton), memory);
    if (!self) return NULL;
    memset(self, 0, sizeof(XPushButton));
    XPushButton_init(self, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 文本（对标 QAbstractButton） ==================== */

const XString* XPushButton_text(const XPushButton* self)
{
    return self ? self->m_text : NULL;
}

void XPushButton_setText(XPushButton* self, const XString* text)
{
    XString* copy;
    if (!self) return;
    if (self->m_text && text &&
        XString_equals(self->m_text, text, XChar_CaseSensitive))
        return;
    copy = text ? XString_create_copy(text) : XString_create();
    if (!copy) return;
    if (self->m_text)
        XString_delete_base((XClass*)self->m_text);
    self->m_text = copy;
    pushbutton_refreshSizeHint(self);
    XWidget_updateGeometry((XWidget*)self);
    XWidget_update((XWidget*)self);
}

void XPushButton_setText_2(XPushButton* self, const char* utf8)
{
    XString* s;
    if (!self) return;
    s = XString_create_utf8(utf8 ? utf8 : "");
    if (!s) return;
    XPushButton_setText(self, s);
    XString_delete_base((XClass*)s);
}

/* ==================== 图标（对标 QAbstractButton） ==================== */

XIcon XPushButton_icon(const XPushButton* self)
{
    XIcon out;
    XIcon_init(&out);
    if (self)
        XIcon_copy_base(&out, &self->m_icon);
    return out;
}

void XPushButton_setIcon(XPushButton* self, const XIcon* icon)
{
    if (!self) return;
    XIcon_deinit_base(&self->m_icon);
    XIcon_init(&self->m_icon);
    if (icon && !XIcon_isNull(icon))
        XIcon_copy_base(&self->m_icon, icon);
    pushbutton_refreshSizeHint(self);
    XWidget_updateGeometry((XWidget*)self);
    XWidget_update((XWidget*)self);
}

XSize XPushButton_iconSize(const XPushButton* self)
{
    XSize out;
    out.width = 0;
    out.height = 0;
    if (!self) return out;
    out = self->m_iconSize;
    return out;
}

void XPushButton_setIconSize(XPushButton* self, const XSize* size)
{
    XSize s;
    if (!self) return;
    s.width = 0;
    s.height = 0;
    if (size && size->width > 0 && size->height > 0) {
        s.width = size->width;
        s.height = size->height;
    }
    if (self->m_iconSize.width == s.width &&
        self->m_iconSize.height == s.height)
        return;
    self->m_iconSize = s;
    pushbutton_refreshSizeHint(self);
    XWidget_updateGeometry((XWidget*)self);
    XWidget_update((XWidget*)self);
}

/* ==================== 选中/按下/自动重复（对标 QAbstractButton） ==================== */

bool XPushButton_isCheckable(const XPushButton* self)
{
    return self ? self->m_checkable : false;
}

void XPushButton_setCheckable(XPushButton* self, bool checkable)
{
    if (!self || self->m_checkable == checkable) return;
    self->m_checkable = checkable;
    /* QAbstractButton::setCheckable(false) 静默清除 checked，不发 toggled。 */
    if (!checkable)
        self->m_checked = false;
    XWidget_update((XWidget*)self);
}

bool XPushButton_isChecked(const XPushButton* self)
{
    return self ? self->m_checked : false;
}

void XPushButton_setChecked(XPushButton* self, bool checked)
{
    bool old;
    if (!self || !self->m_checkable) return;
    old = self->m_checked;
    if (old == checked) return;
    /* QAbstractButton 的独占/自动独占组不能把唯一已选中按钮取消；
       选中其它同组按钮时，旧按钮因已有新同组成员而允许清除。 */
    if (!checked && old && self->m_autoExclusive &&
        pushbutton_isOnlyAutoExclusiveMember(self))
        return;
    self->m_checked = checked;
    XWidget_update((XWidget*)self);
    if (checked)
        pushbutton_uncheckAutoExclusiveSiblings(self);
    pushbutton_emitBool(self, (size_t)XPushButton_toggled_signal, checked);
}

void XPushButton_toggle(XPushButton* self)
{
    if (!self || !self->m_checkable) return;
    XPushButton_setChecked(self, !self->m_checked);
}

bool XPushButton_isDown(const XPushButton* self)
{
    return self ? self->m_down : false;
}

void XPushButton_setDown(XPushButton* self, bool down)
{
    if (!self || self->m_down == down) return;
    self->m_down = down;
    XWidget_repaint((XWidget*)self);
    if (down && self->m_autoRepeat)
        pushbutton_startRepeatTimer(self, self->m_autoRepeatDelay);
    else if (!down) {
        pushbutton_stopRepeatTimer(self);
    }
}

bool XPushButton_autoRepeat(const XPushButton* self)
{
    return self ? self->m_autoRepeat : false;
}

void XPushButton_setAutoRepeat(XPushButton* self, bool repeat)
{
    if (!self || self->m_autoRepeat == repeat)
        return;
    self->m_autoRepeat = repeat;
    if (repeat && self->m_down)
        pushbutton_startRepeatTimer(self, self->m_autoRepeatDelay);
    else if (!repeat)
        pushbutton_stopRepeatTimer(self);
}

int XPushButton_autoRepeatDelay(const XPushButton* self)
{
    return self ? self->m_autoRepeatDelay : 0;
}

void XPushButton_setAutoRepeatDelay(XPushButton* self, int delay)
{
    if (self)
        self->m_autoRepeatDelay = delay;
}

int XPushButton_autoRepeatInterval(const XPushButton* self)
{
    return self ? self->m_autoRepeatInterval : 0;
}

void XPushButton_setAutoRepeatInterval(XPushButton* self, int interval)
{
    if (self)
        self->m_autoRepeatInterval = interval;
}

bool XPushButton_autoExclusive(const XPushButton* self)
{
    return self ? self->m_autoExclusive : false;
}

void XPushButton_setAutoExclusive(XPushButton* self, bool exclusive)
{
    if (self)
        self->m_autoExclusive = exclusive;
}

/* ==================== 点击/命中（对标 QAbstractButton） ==================== */

static void pushbutton_clickInternal(XPushButton* self, bool emitPressed)
{
    bool wasChecked;
    if (!self) return;
    if (!XWidget_isEnabled((XWidget*)self)) return;
    if (emitPressed) {
        /* QAbstractButton::click() 即使当前已 down 也会重新发 pressed。 */
        self->m_down = true;
        XWidget_repaint((XWidget*)self);
        XPushButton_pressed_signal(self);
    }
    self->m_down = false;
    XWidget_repaint((XWidget*)self);
    wasChecked = self->m_checked;
    if (self->m_checkable)
        XPushButton_setChecked(self, !wasChecked);
    XPushButton_released_signal(self);
    XPushButton_clicked_signal(self, self->m_checked);
}

void XPushButton_click(XPushButton* self)
{
    pushbutton_clickInternal(self, true);
}

void XPushButton_animateClick(XPushButton* self)
{
    if (!self || !XWidget_isEnabled((XWidget*)self))
        return;
    /* 对标 QAbstractButton::setDown(true)：不发射 pressed/released，
       但按下时按 autoRepeat 规则启动重复定时器。 */
    XPushButton_setDown(self, true);
    XWidget_repaint((XWidget*)self);
    if (self->m_animateTimer == XTIMER_INVALID_ID)
        XPushButton_pressed_signal(self);
    /* QBasicTimer::start(100, this) 会替换已有动画定时器，第二次调用
       因而只重置释放时刻而不重复发射 pressed。 */
    pushbutton_startAnimateTimer(self);
}

bool XPushButton_hitButton(const XPushButton* self, const XPoint* pos)
{
    XRect rect;
    if (!self || !pos) return false;
    rect = XWidget_rect((const XWidget*)self);
    return XRect_contains(&rect, pos->x, pos->y);
}

/* ==================== 默认/扁平（对标 QPushButton） ==================== */

bool XPushButton_autoDefault(const XPushButton* self)
{
    if (!self) return false;
    if (self->m_autoDefault == XPushButtonAutoDefault_On)
        return true;
    if (self->m_autoDefault == XPushButtonAutoDefault_Off)
        return false;
    return pushbutton_autoDefaultActive(self);
}

void XPushButton_setAutoDefault(XPushButton* self, bool enable)
{
    XPushButtonAutoDefault value;
    if (!self) return;
    value = enable ? XPushButtonAutoDefault_On : XPushButtonAutoDefault_Off;
    if (self->m_autoDefault == value) return;
    self->m_autoDefault = value;
    pushbutton_refreshSizeHint(self);
    XWidget_updateGeometry((XWidget*)self);
    XWidget_update((XWidget*)self);
}

bool XPushButton_isDefault(const XPushButton* self)
{
    return self ? self->m_defaultButton : false;
}

void XPushButton_setDefault(XPushButton* self, bool enable)
{
    if (!self || self->m_defaultButton == enable) return;
    self->m_defaultButton = enable;
    XWidget_update((XWidget*)self);
}

bool XPushButton_isFlat(const XPushButton* self)
{
    return self ? self->m_flat : false;
}

void XPushButton_setFlat(XPushButton* self, bool flat)
{
    if (!self || self->m_flat == flat) return;
    self->m_flat = flat;
    pushbutton_refreshSizeHint(self);
    XWidget_updateGeometry((XWidget*)self);
    XWidget_update((XWidget*)self);
}

/* ==================== 菜单（对标 QPushButton setMenu/menu/showMenu） ==================== */

void XPushButton_setMenu(XPushButton* self, XMenu* menu)
{
    if (!self || self->m_menu == menu) return;
    self->m_menu = menu;
    pushbutton_refreshSizeHint(self);
    XWidget_updateGeometry((XWidget*)self);
    XWidget_update((XWidget*)self);
}

XMenu* XPushButton_menu(const XPushButton* self)
{
    return self ? self->m_menu : NULL;
}

void XPushButton_showMenu(XPushButton* self)
{
    if (!self || !self->m_menu) return;
    if (!XWidget_isEnabled((XWidget*)self)) return;
    self->m_down = true;
    XWidget_repaint((XWidget*)self);
}

/* ==================== 尺寸（对标 QPushButton sizeHint/minimumSizeHint） ==================== */

XSize XPushButton_sizeHint(const XPushButton* self)
{
    return pushbutton_computeSizeHint(self);
}

XSize XPushButton_minimumSizeHint(const XPushButton* self)
{
    return pushbutton_computeSizeHint(self);
}

/* ==================== 绘制（对标 QPushButton::paintEvent 内容） ==================== */

void XPushButton_drawContents(XPushButton* self, XPainter* painter)
{
    XRect rect, content;
    uint32_t bg, textColor, light, dark, mid;
    XPaletteColorGroup group;
    bool pressed;
    XFont font;
    const char* text;
    bool drawIcon;
    int iconW, iconH, iconX, iconY;

    if (!self || !painter) return;
    if (!XPainter_save(painter)) return;
    rect = XWidget_rect((XWidget*)self);
    group = XWidget_isEnabled((XWidget*)self) ? XPaletteColorGroup_Current
                                              : XPaletteColorGroup_Disabled;
#if XPALETTE_ON
    light = pushbutton_color(self, group, XPaletteColorRole_Light);
    dark = pushbutton_color(self, group, XPaletteColorRole_Dark);
    mid = pushbutton_color(self, group, XPaletteColorRole_Mid);
#else
    light = 0xFFE0E0E0u;
    dark = 0xFF808080u;
    mid = 0xFFA0A0A0u;
#endif
    bg = pushbutton_color(self, group, XPaletteColorRole_Button);
    textColor = pushbutton_color(self, group, XPaletteColorRole_ButtonText);
    if (bg == 0u)
        bg = 0xFFCFCFCFu;
    pressed = self->m_down || self->m_checked;

    XPainter_fillRect(painter, &rect, bg);
    if (!self->m_flat && rect.width > 2 && rect.height > 2) {
        XRect edge;
        if (!pressed) {
            edge.x = rect.x; edge.y = rect.y;
            edge.width = rect.width; edge.height = 1;
            XPainter_fillRect(painter, &edge, light);
            edge.x = rect.x; edge.y = rect.y;
            edge.width = 1; edge.height = rect.height;
            XPainter_fillRect(painter, &edge, light);
            edge.x = rect.x; edge.y = rect.y + rect.height - 1;
            edge.width = rect.width; edge.height = 1;
            XPainter_fillRect(painter, &edge, dark);
            edge.x = rect.x + rect.width - 1; edge.y = rect.y;
            edge.width = 1; edge.height = rect.height;
            XPainter_fillRect(painter, &edge, mid);
        } else {
            edge.x = rect.x; edge.y = rect.y;
            edge.width = rect.width; edge.height = 1;
            XPainter_fillRect(painter, &edge, dark);
            edge.x = rect.x; edge.y = rect.y;
            edge.width = 1; edge.height = rect.height;
            XPainter_fillRect(painter, &edge, mid);
            edge.x = rect.x; edge.y = rect.y + rect.height - 1;
            edge.width = rect.width; edge.height = 1;
            XPainter_fillRect(painter, &edge, light);
            edge.x = rect.x + rect.width - 1; edge.y = rect.y;
            edge.width = 1; edge.height = rect.height;
            XPainter_fillRect(painter, &edge, light);
        }
    }

    font = XWidget_font((XWidget*)self);
    XPainter_setFont(painter, &font);
    text = XString_toUtf8(self->m_text ? self->m_text : NULL);
    drawIcon = !XIcon_isNull(&self->m_icon);
    iconW = 16;
    iconH = 16;
    if (pushbutton_hasIconSize(self)) {
        iconW = self->m_iconSize.width;
        iconH = self->m_iconSize.height;
    }
    content = rect;
    if (drawIcon) {
        iconX = rect.x + (rect.width - iconW) / 2;
        iconY = rect.y + (rect.height - iconH) / 2;
        if (text && text[0] != '\0') {
            iconX = rect.x + 4;
            iconY = rect.y + (rect.height - iconH) / 2;
            content.x += iconW + 4;
            content.width -= iconW + 4;
        }
        XIcon_paint(&self->m_icon, painter, iconX, iconY, iconW, iconH,
                    XAlignment_Center,
                    XWidget_isEnabled((XWidget*)self) ? XIconMode_Normal
                                                      : XIconMode_Disabled,
                    self->m_checked ? XIconState_On : XIconState_Off);
    }
    if (text && text[0] != '\0') {
        if (content.width < 4) content.width = 4;
#if XPAINTER_TEXTLAYOUT_ON
        XPainter_drawTextRect(painter, &content,
                              XPAINTER_TEXT_ALIGN_CENTER | XPAINTER_TEXT_SINGLE_LINE,
                              text, textColor);
#else
        /* 文本布局被裁剪时保留按钮可用性：用点阵字体度量做单行居中。 */
        {
            int textW = XPainter_textWidth(&font, text);
            int textH = XPainter_textHeight(&font);
            int ascent = XPainter_textAscent(&font);
            int textX = content.x + (content.width - textW) / 2;
            int baseline = content.y + (content.height - textH) / 2 + ascent;
            XPainter_drawText(painter, textX, baseline, text, textColor);
        }
#endif /* XPAINTER_TEXTLAYOUT_ON */
    }
    XFont_deinit_base(&font);
    if (self->m_menu && rect.width >= 12 && rect.height >= 7) {
        int arrowX = rect.x + rect.width - 9;
        int arrowY = rect.y + (rect.height / 2) - 2;
        XRect tri;
        tri.x = arrowX + 2; tri.y = arrowY; tri.width = 1; tri.height = 1;
        XPainter_fillRect(painter, &tri, textColor);
        tri.x = arrowX + 1; tri.y = arrowY + 1; tri.width = 3; tri.height = 1;
        XPainter_fillRect(painter, &tri, textColor);
        tri.x = arrowX; tri.y = arrowY + 2; tri.width = 5; tri.height = 1;
        XPainter_fillRect(painter, &tri, textColor);
        tri.x = arrowX + 1; tri.y = arrowY + 3; tri.width = 3; tri.height = 1;
        XPainter_fillRect(painter, &tri, textColor);
        tri.x = arrowX + 2; tri.y = arrowY + 4; tri.width = 1; tri.height = 1;
        XPainter_fillRect(painter, &tri, textColor);
    }
    XPainter_restore(painter);
}

/* ==================== 虚槽实现（对标 QAbstractButton/QPushButton 事件） ==================== */

static bool pushbutton_ignoreDisabledEvent(XWidget* self, XEvent* event)
{
    XEventType type;
    if (!self || XWidget_isEnabled(self)) return false;
    type = event ? XEvent_type(event) : XEVENT_TYPE_NONE;
    switch (type) {
    case XEVENT_TYPE_MOUSE_BUTTON_PRESS:
    case XEVENT_TYPE_MOUSE_BUTTON_RELEASE:
    case XEVENT_TYPE_MOUSE_BUTTON_DBL_CLICK:
    case XEVENT_TYPE_MOUSE_MOVE:
    case XEVENT_TYPE_CONTEXT_MENU:
    case XEVENT_TYPE_HOVER_ENTER:
    case XEVENT_TYPE_HOVER_LEAVE:
    case XEVENT_TYPE_HOVER_MOVE:
        if (event) XEvent_accept(event);
        return true;
    default:
        return false;
    }
}

static bool VXPushButton_event(XWidget* self, XEvent* event)
{
    if (pushbutton_ignoreDisabledEvent(self, event))
        return true;
    return XClass_Parent(XWidget, EXObject_Event,
                         bool(*)(XWidget*, XEvent*))((XWidget*)self, event);
}

static void VXPushButton_changeEvent(XWidget* self, XEvent* event)
{
    XPushButton* button;
    XEventType type;
    if (!self || !event) return;
    type = XEvent_type(event);
    button = (XPushButton*)self;
    if (type == XEVENT_TYPE_ENABLED_CHANGE &&
        !XWidget_isEnabled(self) && button->m_down) {
        pushbutton_stopRepeatTimer(button);
        button->m_down = false;
        XWidget_repaint(self);
        XPushButton_released_signal(button);
    }
    XClass_Parent(XWidget, EXWidget_ChangeEvent,
                  void(*)(XWidget*, XEvent*))((XWidget*)self, event);
}

static void VXPushButton_paintEvent(XWidget* self, XEvent* event)
{
#if !XPALETTE_ON || !XWINDOWEVENT_ON
    (void)self;
    (void)event;
    return;
#else
    XImage* image;
    XPoint offset;
    XPainter painter;
#if XPAINTER_CLIP_ON
    XPaintEvent* pe;
    XRect clip;
#endif
    if (!self || !event || XEvent_type(event) != XEVENT_TYPE_PAINT) return;
#if XPAINTER_CLIP_ON
    pe = (XPaintEvent*)event;
#endif
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
#if XPAINTER_CLIP_ON
    clip = XPaintEvent_rect(pe);
    XPainter_setClipRect(&painter, &clip, XPainterClipOperation_ReplaceClip);
#endif
    XPushButton_drawContents((XPushButton*)self, &painter);
    XPainter_end(&painter);
    XPainter_deinit(&painter);
#endif /* XPALETTE_ON && XWINDOWEVENT_ON */
}

static void VXPushButton_mousePressCommon(XWidget* self, XEvent* event)
{
    XPushButton* button = (XPushButton*)self;
    XMouseEvent* me;
    XPoint pos;
    bool hit;
    if (!button || !event) return;
    if (XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_PRESS &&
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_DBL_CLICK)
        return;
    me = (XMouseEvent*)event;
    if (me->m_button != XMouseButton_LeftButton) {
        XEvent_ignore(event);
        return;
    }
    pos = me->m_position;
    hit = XPushButton_hitButton((const XPushButton*)button, &pos);
    if (hit) {
        button->m_pressed = true;
        pushbutton_setDownInternal(button, true);
        XEvent_accept(event);
        return;
    }
    if (button->m_down)
        pushbutton_setDownInternal(button, false);
    XEvent_ignore(event);
}

static void VXPushButton_mousePressEvent(XWidget* self, XEvent* event)
{
    VXPushButton_mousePressCommon(self, event);
}

static void VXPushButton_mouseDoubleClickEvent(XWidget* self, XEvent* event)
{
    VXPushButton_mousePressCommon(self, event);
}

static void VXPushButton_mouseReleaseEvent(XWidget* self, XEvent* event)
{
    XPushButton* button = (XPushButton*)self;
    XMouseEvent* me;
    XPoint pos;
    bool hit;
    if (!button || !event) return;
    if (XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_RELEASE) return;
    me = (XMouseEvent*)event;
    button->m_pressed = false;
    pushbutton_stopRepeatTimer(button);
    if (!button->m_down) {
        XEvent_ignore(event);
        return;
    }
    pos = me->m_position;
    hit = XPushButton_hitButton((const XPushButton*)button, &pos);
    if (hit) {
        /* 鼠标释放前已经发过 pressed；Qt 的内部 click() 从 down=false
           开始，仅执行 nextCheckState、released、clicked。 */
        pushbutton_clickInternal(button, false);
        XEvent_accept(event);
        return;
    }
    pushbutton_setDownInternal(button, false);
    XEvent_ignore(event);
}

static void VXPushButton_mouseMoveEvent(XWidget* self, XEvent* event)
{
    XPushButton* button = (XPushButton*)self;
    XMouseEvent* me;
    XPoint pos;
    bool hit;
    if (!button || !event) return;
    if (XEvent_type(event) != XEVENT_TYPE_MOUSE_MOVE) return;
    me = (XMouseEvent*)event;
    if ((me->m_buttons & XMouseButton_LeftButton) == 0 || !button->m_pressed) {
        XEvent_ignore(event);
        return;
    }
    pos = me->m_position;
    hit = XPushButton_hitButton((const XPushButton*)button, &pos);
    if (hit != button->m_down) {
        pushbutton_setDownInternal(button, hit);
        XEvent_accept(event);
        return;
    }
    if (hit)
        XEvent_accept(event);
    else
        XEvent_ignore(event);
}

static void VXPushButton_keyPressEvent(XWidget* self, XEvent* event)
{
    XPushButton* button = (XPushButton*)self;
    XKeyEvent* ke;
    int key;
    if (!button || !event) return;
    if (XEvent_type(event) != XEVENT_TYPE_KEY_PRESS) return;
    ke = (XKeyEvent*)event;
    key = ke->m_key;
    if (key == XKey_Space && !ke->m_autoRepeat) {
        pushbutton_setDownInternal(button, true);
        XEvent_accept(event);
        return;
    }
    /* QAbstractButton::keyPressEvent 对取消键（默认对应 Escape）在按下
       状态时仅释放按钮，不执行 click，避免误发 clicked/toggled。 */
    if (key == XKey_Escape && button->m_down) {
        pushbutton_setDownInternal(button, false);
        XEvent_accept(event);
        return;
    }
    if (key == XKey_Return || key == XKey_Enter) {
        if (XPushButton_autoDefault(button) || XPushButton_isDefault(button)) {
            XPushButton_click(button);
            XEvent_accept(event);
            return;
        }
    }
    XClass_Parent(XWidget, EXWidget_KeyPressEvent,
                  void(*)(XWidget*, XEvent*))((XWidget*)self, event);
}

static void VXPushButton_keyReleaseEvent(XWidget* self, XEvent* event)
{
    XPushButton* button = (XPushButton*)self;
    XKeyEvent* ke;
    int key;
    if (!button || !event) return;
    if (XEvent_type(event) != XEVENT_TYPE_KEY_RELEASE) return;
    ke = (XKeyEvent*)event;
    key = ke->m_key;
    if (key == XKey_Space && !ke->m_autoRepeat && button->m_down) {
        /* 空格按下已发 pressed，释放阶段不重复发 pressed。 */
        pushbutton_stopRepeatTimer(button);
        pushbutton_clickInternal(button, false);
        XEvent_accept(event);
        return;
    }
    if (event) XEvent_ignore(event);
}

static void VXPushButton_focusInEvent(XWidget* self, XEvent* event)
{
    if (self && event)
        XClass_Parent(XWidget, EXWidget_FocusInEvent,
                      void(*)(XWidget*, XEvent*))((XWidget*)self, event);
}

static void VXPushButton_focusOutEvent(XWidget* self, XEvent* event)
{
    XPushButton* button = (XPushButton*)self;
    XFocusReason reason = XFocusReason_Other;
    bool popupFocus = false;
    if (!self || !event) return;
#if XWINDOWEVENT_ON
    if (XFocusEvent_lostFocus((const XFocusEvent*)event))
        reason = XFocusEvent_reason((const XFocusEvent*)event);
#endif /* XWINDOWEVENT_ON */
    /* QAbstractButton::focusOutEvent 保留 PopupFocusReason 下的 down 状态；
       弹出菜单切换时按钮仍应保持按下并继续自动重复，其他失焦原因才释放。 */
    popupFocus = reason == XFocusReason_Popup;
    if (button && button->m_down && !popupFocus) {
        pushbutton_stopRepeatTimer(button);
        button->m_down = false;
        XWidget_repaint((XWidget*)button);
        XPushButton_released_signal(button);
    }
    XClass_Parent(XWidget, EXWidget_FocusOutEvent,
                  void(*)(XWidget*, XEvent*))((XWidget*)self, event);
}

/* ==================== 复制/移动/析构（对标 XClass 生命周期） ==================== */

static void VXPushButton_copy(XPushButton* self, const XPushButton* other)
{
    if (!self || !other || self == other) return;
    pushbutton_stopRepeatTimer(self);
    pushbutton_stopAnimateTimer(self);
    if (XClassIsVtableNull(self)) XPushButton_init(self, NULL, 0);
    XClass_Parent(XWidget, EXClass_Copy,
                  void(*)(XWidget*, const XWidget*))((XWidget*)self,
                                                     (const XWidget*)other);
    if (self->m_text) {
        XString_delete_base((XClass*)self->m_text);
        self->m_text = NULL;
    }
    XIcon_deinit_base(&self->m_icon);
    XIcon_init(&self->m_icon);
    self->m_text = other->m_text ? XString_create_copy(other->m_text)
                                 : XString_create();
    XIcon_copy_base(&self->m_icon, &other->m_icon);
    self->m_iconSize = other->m_iconSize;
    self->m_checkable = other->m_checkable;
    self->m_checked = other->m_checked;
    self->m_down = other->m_down;
    self->m_pressed = other->m_pressed;
    self->m_autoRepeat = other->m_autoRepeat;
    self->m_autoExclusive = other->m_autoExclusive;
    self->m_flat = other->m_flat;
    self->m_defaultButton = other->m_defaultButton;
    self->m_autoDefault = other->m_autoDefault;
    self->m_autoRepeatDelay = other->m_autoRepeatDelay;
    self->m_autoRepeatInterval = other->m_autoRepeatInterval;
    self->m_repeatTimer = XTIMER_INVALID_ID;
    self->m_animateTimer = XTIMER_INVALID_ID;
    self->m_menu = other->m_menu;
}

static void VXPushButton_move(XPushButton* self, XPushButton* other)
{
    if (!self || !other || self == other) return;
    pushbutton_stopRepeatTimer(self);
    pushbutton_stopAnimateTimer(self);
    pushbutton_stopRepeatTimer(other);
    if (XClassIsVtableNull(self)) XPushButton_init(self, NULL, 0);
    XClass_Parent(XWidget, EXClass_Move,
                  void(*)(XWidget*, XWidget*))((XWidget*)self, (XWidget*)other);
    if (self->m_text) {
        XString_delete_base((XClass*)self->m_text);
        self->m_text = NULL;
    }
    XIcon_deinit_base(&self->m_icon);
    XIcon_init(&self->m_icon);
    self->m_text = other->m_text;
    XIcon_move_base(&self->m_icon, &other->m_icon);
    XIcon_init(&other->m_icon);
    other->m_text = XString_create();
    self->m_iconSize = other->m_iconSize;
    other->m_iconSize.width = 0;
    other->m_iconSize.height = 0;
    self->m_checkable = other->m_checkable;
    self->m_checked = other->m_checked;
    self->m_down = other->m_down;
    self->m_pressed = other->m_pressed;
    self->m_autoRepeat = other->m_autoRepeat;
    self->m_autoExclusive = other->m_autoExclusive;
    self->m_flat = other->m_flat;
    self->m_defaultButton = other->m_defaultButton;
    self->m_autoDefault = other->m_autoDefault;
    self->m_autoRepeatDelay = other->m_autoRepeatDelay;
    self->m_autoRepeatInterval = other->m_autoRepeatInterval;
    self->m_repeatTimer = XTIMER_INVALID_ID;
    self->m_animateTimer = XTIMER_INVALID_ID;
    self->m_menu = other->m_menu;
    other->m_menu = NULL;
    other->m_checkable = false;
    other->m_checked = false;
    other->m_down = false;
    other->m_pressed = false;
    other->m_autoRepeat = false;
    other->m_autoExclusive = false;
    other->m_flat = false;
    other->m_defaultButton = false;
    other->m_autoDefault = XPushButtonAutoDefault_Auto;
    other->m_autoRepeatDelay = 300;
    other->m_autoRepeatInterval = 100;
    other->m_repeatTimer = XTIMER_INVALID_ID;
    other->m_animateTimer = XTIMER_INVALID_ID;
}

static void VXPushButton_deinit(XPushButton* self)
{
    if (!self) return;
    pushbutton_stopRepeatTimer(self);
    pushbutton_stopAnimateTimer(self);
    if (self->m_text) {
        XString_delete_base((XClass*)self->m_text);
        self->m_text = NULL;
    }
    XIcon_deinit_base(&self->m_icon);
    self->m_menu = NULL;
    XClass_Deinit_Parent(XWidget, (XWidget*)self);
}

/** @brief 处理自动重复定时器事件（对标 QAbstractButton::timerEvent）。 */
static void VXPushButton_timerEvent(XObject* object, XTimerEvent* event)
{
    XPushButton* self = (XPushButton*)object;
    if (self && event && XTimerEvent_timerId(event) == self->m_repeatTimer) {
        XTimerId timerId = self->m_repeatTimer;
        XObject_killTimer((XObject*)self, timerId);
        self->m_repeatTimer = XTIMER_INVALID_ID;
        if (self->m_down && self->m_autoRepeat)
            pushbutton_startRepeatTimer(self, self->m_autoRepeatInterval);
        else
            pushbutton_stopRepeatTimer(self);
        pushbutton_repeatTimeout(self);
        XEvent_accept((XEvent*)event);
        return;
    }
    if (self && event && XTimerEvent_timerId(event) == self->m_animateTimer) {
        XTimerId timerId = self->m_animateTimer;
        bool wasChecked = self->m_checked;
        XObject_killTimer((XObject*)self, timerId);
        self->m_animateTimer = XTIMER_INVALID_ID;
        /* 对标 QAbstractButtonPrivate::click：动画定时器到期后不再
           发射 pressed，直接清除 down、执行 nextCheckState、发射
           released/clicked。 */
        self->m_down = false;
        XWidget_repaint((XWidget*)self);
        if (self->m_checkable)
            XPushButton_setChecked(self, !wasChecked);
        XPushButton_released_signal(self);
        XPushButton_clicked_signal(self, self->m_checked);
        XEvent_accept((XEvent*)event);
        return;
    }
    XClass_Parent(XObject, EXObject_TimerEvent,
                  void(*)(XObject*, XTimerEvent*))(object, event);
}

/* ==================== 类虚表 ==================== */

XVtable* XPushButton_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XPushButton)
    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_Event, VXPushButton_event);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VXPushButton_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ChangeEvent, VXPushButton_changeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MousePressEvent, VXPushButton_mousePressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseReleaseEvent, VXPushButton_mouseReleaseEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseMoveEvent, VXPushButton_mouseMoveEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseDoubleClickEvent, VXPushButton_mouseDoubleClickEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_KeyPressEvent, VXPushButton_keyPressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_KeyReleaseEvent, VXPushButton_keyReleaseEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_FocusInEvent, VXPushButton_focusInEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_FocusOutEvent, VXPushButton_focusOutEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_TimerEvent, VXPushButton_timerEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXPushButton_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXPushButton_move);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXPushButton_deinit);
    return XVTABLE_DEFAULT;
}

#endif /* XWIDGET_ON && XPUSHBUTTON_ON */
