/**
 * @file       XAbstractSpinBox.h
 * @brief      XAbstractSpinBox 微调框抽象基类（对齐 Qt 6.8
 *             QAbstractSpinBox 完整公共 API）。
 * @details    提供数值微调框的公共骨架：
 *             - 内嵌 XLineEdit 编辑区（拥有，可整体替换 setLineEdit）；
 *             - 按钮符号（UpDownArrows=0/PlusMinus=1/NoButtons=2，数值与
 *               Qt 一致）、frame 边框、keyboardTracking 键盘跟踪、
 *               correctionMode 提交修正、wrapping 循环、accelerated 加速、
 *               groupSeparatorShown 千分位分隔（字段+显示刷新）；
 *             - specialValueText 特殊值文本（value==minimum 时整段替换显示）；
 *             - 虚槽：StepBy 步进、Validate 校验、Fixup 修正、Clear 清空、
 *               StepEnabled 步进使能（子类必须重载）；Interpret/UpdateEdit
 *               为内部提交/显示刷新钩子；
 *             - 事件：keyPressEvent（Up/Down/PageUp/PageDown 步进、
 *               Home/End 在 Shift 组合下选到前缀后/后缀前、其余交内嵌
 *               编辑框移动光标、Return 提交并转发 returnPressed）、
 *               wheelEvent（滚轮步进，120 角度增量=1 步）、
 *               focusOutEvent 失焦提交 editingFinished、close/hide/show/
 *               timer 转发父类（阈值/重复两枚步进连发定时器由 timerEvent
 *               消费，见 spinButtonPress）；
 *             - 步进按钮长按连发（对标 Qt spinClickThresholdTimer/
 *               spinClickTimer）：子类在鼠标按下/抬起时调用
 *               XAbstractSpinBox_spinButtonPress/_Release，阈值（样式
 *               SH_SpinBox_ClickAutoRepeatThreshold=500ms）后按
 *               SH_SpinBox_ClickAutoRepeatRate（150ms）重复步进，
 *               accelerated=true 时每拍缩短 5%（下限 10ms）；
 *             - 信号：editingFinished（内嵌编辑框 Return/失焦转发）。
 * @note       模块总开关 XABSTRACTSPINBOX_ON 定义于 XGuiConfig.h；
 *             =0 时裁剪全部公共 API。依赖 XWIDGET_ON、XLINEEDIT_ON、
 *             XWINDOWEVENT_ON（滚轮）。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XABSTRACTSPINBOX_H
#define XABSTRACTSPINBOX_H

#if XGUI_ON
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XGuiConfig.h"
#include "XWidget.h"
#include "XString.h"
#include "XLineEdit.h"

#if XWIDGET_ON && XABSTRACTSPINBOX_ON && XLINEEDIT_ON

/* ==================== 枚举（对标 QAbstractSpinBox / QValidator 数值一致） ==================== */

/** @brief 按钮符号（对标 QAbstractSpinBox::ButtonSymbols；数值与 Qt 一致）。 */
typedef enum XAbstractSpinBoxButtonSymbols
{
    XAbstractSpinBoxButtonSymbols_UpDownArrows = 0,  /**< 上/下箭头（默认）。 */
    XAbstractSpinBoxButtonSymbols_PlusMinus = 1,     /**< 加/减号。 */
    XAbstractSpinBoxButtonSymbols_NoButtons = 2      /**< 无按钮。 */
} XAbstractSpinBoxButtonSymbols;

/** @brief 步进使能位（对标 QAbstractSpinBox::StepEnabledFlag；可按位组合）。 */
typedef enum XAbstractSpinBoxStepEnabledFlag
{
    XAbstractSpinBoxStepEnabledFlag_StepNone = 0x00,       /**< 不可步进。 */
    XAbstractSpinBoxStepEnabledFlag_StepUpEnabled = 0x01,  /**< 可上步进。 */
    XAbstractSpinBoxStepEnabledFlag_StepDownEnabled = 0x02 /**< 可下步进。 */
} XAbstractSpinBoxStepEnabledFlag;

/** @brief 步进使能位组合类型（对标 QAbstractSpinBox::StepEnabled）。 */
typedef int XAbstractSpinBoxStepEnabled;

/** @brief 提交修正模式（对标 QAbstractSpinBox::CorrectionMode）。 */
typedef enum XAbstractSpinBoxCorrectionMode
{
    XAbstractSpinBoxCorrectionMode_CorrectToPreviousValue = 0, /**< 回退到上次有效值（默认）。 */
    XAbstractSpinBoxCorrectionMode_CorrectToNearestValue = 1  /**< 修正到最近合法值。 */
} XAbstractSpinBoxCorrectionMode;

/** @brief 步进类型（对标 QAbstractSpinBox::StepType）。 */
typedef enum XAbstractSpinBoxStepType
{
    XAbstractSpinBoxStepType_DefaultStepType = 0,        /**< 固定 singleStep（默认）。 */
    XAbstractSpinBoxStepType_AdaptiveDecimalStepType = 1 /**< 自适应十进制步长（字段保留；行为为后续扩展项）。 */
} XAbstractSpinBoxStepType;

/** @brief 文本校验状态（对标 QValidator::State；数值与 Qt 一致）。 */
typedef enum XValidatorState
{
    XValidatorState_Invalid = 0,       /**< 非法输入。 */
    XValidatorState_Intermediate = 1,  /**< 中间状态（未完成/越界但可修正）。 */
    XValidatorState_Acceptable = 2     /**< 可接受输入。 */
} XValidatorState;

/** @brief 步进按钮状态位（对标 Qt 私有 Button 枚举，数值一致）。
 * @details 只读查询走 XAbstractSpinBox_buttonState；位含义：
 *          Keyboard=键盘触发、Mouse=鼠标按住、Wheel=滚轮、
 *          Up/Down=方向。长按连发期间为 Mouse|Up / Mouse|Down。 */
typedef enum XAbstractSpinBoxButtonState
{
    XAbstractSpinBoxButtonState_None = 0x000,
    XAbstractSpinBoxButtonState_Keyboard = 0x001,
    XAbstractSpinBoxButtonState_Mouse = 0x002,
    XAbstractSpinBoxButtonState_Wheel = 0x004,
    /* Qt 6.8 qabstractspinbox_p.h 的 ButtonMask = Keyboard|Mouse|Wheel
     * = 0x007（不是独立位）。isSpinButtonActive 以
     * `m_buttonState & ButtonMask` 判定「是否有输入源按住」——写成 0x008
     * 与 Keyboard/Mouse/Wheel 全不相交 ⇒ 按住期间恒报 false（实测
     * 按下步进按钮后 isSpinButtonActive()==false，长按连发状态机也
     * 无法从查询侧观察到）。 */
    XAbstractSpinBoxButtonState_ButtonMask = 0x007,
    XAbstractSpinBoxButtonState_Up = 0x010,
    XAbstractSpinBoxButtonState_Down = 0x020,
    XAbstractSpinBoxButtonState_DirectionMask = 0x040
} XAbstractSpinBoxButtonState;

/* ==================== 虚函数表（StepBy + 校验/修正/步进/提交钩子） ==================== */
XCLASS_DEFINE_BEGING(XAbstractSpinBox)
XCLASS_DEFINE_ENUM(XAbstractSpinBox, StepBy) = XCLASS_VTABLE_GET_SIZE(XWidget),
XCLASS_DEFINE_ENUM(XAbstractSpinBox, Validate),
XCLASS_DEFINE_ENUM(XAbstractSpinBox, Fixup),
XCLASS_DEFINE_ENUM(XAbstractSpinBox, Clear),
XCLASS_DEFINE_ENUM(XAbstractSpinBox, StepEnabled),
XCLASS_DEFINE_ENUM(XAbstractSpinBox, Interpret),
XCLASS_DEFINE_ENUM(XAbstractSpinBox, UpdateEdit),
XCLASS_DEFINE_ENUM(XAbstractSpinBox, PrefixText),
XCLASS_DEFINE_ENUM(XAbstractSpinBox, SuffixText),
XCLASS_DEFINE_END(XAbstractSpinBox)

/**
 * @brief      XAbstractSpinBox 抽象基类对象；m_base 必须是第一个成员。
 * @details    字段含义：
 *             - m_lineEdit：内嵌编辑框（拥有，setLineEdit 可替换）；
 *             - m_buttonSymbols：按钮符号（默认 UpDownArrows）；
 *             - m_keyboardTracking：键盘跟踪（默认 true）；
 *             - m_frame：边框开关（默认 true）；
 *             - m_correctionMode：提交修正模式（默认 CorrectToPreviousValue）；
 *             - m_wrapping：循环步进开关（默认 false）；
 *             - m_accelerated：按住按钮加速（驱动重复定时器逐拍缩短间隔）；
 *             - m_groupSeparatorShown：千分位分隔显示（默认 false）；
 *             - m_specialValueText：特殊值文本（拥有；value==minimum 时显示）；
 *             - m_cleared：内部标志（clear() 后待解释状态，对标 Qt 私有
 *               cleared：interpret/updateEdit 在其置位期间早退，setValue
 *               或用户重新键入才复位）；
 *             - m_wheelDeltaRemainder：滚轮角度累积余数（内部）；
 *             - m_buttonState：步进按钮状态位（XAbstractSpinBoxButtonState）；
 *             - m_spinClickThresholdTimerId/m_spinClickTimerId：长按阈值/
 *               重复定时器（对标 Qt 同名私有字段）；
 *             - m_effectiveSpinRepeatRate：本次按住的基础重复间隔（ms）；
 *             - m_acceleration：加速累计量（Qt `acceleration`）；
 *             - m_stepModifierDown：按住期间 Ctrl 是否按下（×10 步进）；
 *             - m_cachedSizeHint/m_cachedMinimumSizeHint：建议尺寸缓存
 *               （对标 Qt 私有 QAbstractSpinBoxPrivate::cachedSizeHint/
 *               cachedMinimumSizeHint，qabstractspinbox_p.h:97；width<0
 *               表示未缓存）。
 *             调用者不得手工修改字段；一律走公开 API。
 */
typedef struct XAbstractSpinBox
{
    XWidget m_base;                    /**< 基类成员；必须是第一个。 */
    XLineEdit* m_lineEdit;             /**< 内嵌编辑框（拥有）。 */
    XString* m_specialValueText;       /**< 特殊值文本（对象拥有；NULL=未设置）。 */
    XSize    m_cachedSizeHint;         /**< 内部：sizeHint 缓存（width<0=未缓存）。 */
    XSize    m_cachedMinimumSizeHint;  /**< 内部：minimumSizeHint 缓存（width<0=未缓存）。 */
    XTimerId m_spinClickThresholdTimerId; /**< 内部：长按阈值定时器。 */
    XTimerId m_spinClickTimerId;       /**< 内部：长按重复定时器。 */
    int     m_effectiveSpinRepeatRate; /**< 内部：基础重复间隔（ms）。 */
    int     m_acceleration;            /**< 内部：加速累计量（ms）。 */
    int     m_wheelDeltaRemainder;     /**< 内部：滚轮角度累积余数（内部累加器，保持 int）。 */
    uint8_t m_buttonSymbols;           /**< 按钮符号（XAbstractSpinBoxButtonSymbols
                                           0..2；setter 校验后写入）。 */
    uint8_t m_correctionMode;          /**< 提交修正模式（XAbstractSpinBoxCorrectionMode
                                           0..1；setter 校验后写入）。 */
    uint8_t m_buttonState;             /**< 内部：步进按钮状态位。 */
    uint8_t m_keyboardTracking : 1;    /**< 键盘跟踪。 */
    uint8_t m_frame : 1;               /**< 边框开关。 */
    uint8_t m_wrapping : 1;            /**< 循环步进开关。 */
    uint8_t m_accelerated : 1;         /**< 加速开关（长按连发逐拍缩短间隔）。 */
    uint8_t m_groupSeparatorShown : 1; /**< 千分位分隔显示。 */
    uint8_t m_cleared : 1;             /**< 内部：clear() 后待解释标志。 */
    uint8_t m_stepModifierDown : 1;    /**< 内部：按住期间步进修饰键（Ctrl）。 */
} XAbstractSpinBox;

/* ==================== 生命周期 ==================== */

XVtable* XAbstractSpinBox_class_init(void);
/** @brief XAbstractSpin盒init（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param parent 父控件指针；可为 NULL。
 * @param flags 窗口标志位组合。
 * @return 无返回值。
 */
void XAbstractSpinBox_init(XAbstractSpinBox* self, XWidget* parent, XWidgetFlags flags);
/** @brief XAbstractSpin盒createex（对标 Qt 同名接口）。
 * @param memory XMemoryType 参数。
 * @param parent 父控件指针；可为 NULL。
 * @param flags 窗口标志位组合。
 * @return 返回对象指针；无效时返回 NULL。
 */
XAbstractSpinBox* XAbstractSpinBox_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags);

/* ==================== 父类 XWidget API 宏转发（对齐库内 XRadioButton 惯例） ==================== */

#define XAbstractSpinBox_setEnabled(self, enabled) XWidget_setEnabled((XWidget*)(self), (enabled))
#define XAbstractSpinBox_isEnabled(self) XWidget_isEnabled((const XWidget*)(self))
#define XAbstractSpinBox_setVisible(self, visible) XWidget_setVisible((XWidget*)(self), (visible))
#define XAbstractSpinBox_isVisible(self) XWidget_isVisible((const XWidget*)(self))
#define XAbstractSpinBox_show(self) XWidget_show((XWidget*)(self))
#define XAbstractSpinBox_hide(self) XWidget_hide((XWidget*)(self))
#define XAbstractSpinBox_raise(self) XWidget_raise((XWidget*)(self))
#define XAbstractSpinBox_lower(self) XWidget_lower((XWidget*)(self))
#define XAbstractSpinBox_setGeometry(self, x, y, w, h) XWidget_setGeometry((XWidget*)(self), (x), (y), (w), (h))
#define XAbstractSpinBox_setGeometryRect(self, rect) XWidget_setGeometryRect((XWidget*)(self), (rect))
#define XAbstractSpinBox_x(self) XWidget_x((const XWidget*)(self))
#define XAbstractSpinBox_y(self) XWidget_y((const XWidget*)(self))
#define XAbstractSpinBox_width(self) XWidget_width((const XWidget*)(self))
#define XAbstractSpinBox_height(self) XWidget_height((const XWidget*)(self))
#define XAbstractSpinBox_resize(self, w, h) XWidget_resize((XWidget*)(self), (w), (h))
#define XAbstractSpinBox_move(self, x, y) XWidget_move((XWidget*)(self), (x), (y))
#define XAbstractSpinBox_update(self) XWidget_update((XWidget*)(self))
#define XAbstractSpinBox_updateRect(self, rect) XWidget_updateRect((XWidget*)(self), (rect))
#define XAbstractSpinBox_setParent(self, parent, flags) XWidget_setParent((XWidget*)(self), (parent), (flags))
#define XAbstractSpinBox_parentWidget(self) XWidget_parentWidget((const XWidget*)(self))
#define XAbstractSpinBox_setFocus(self) XWidget_setFocus((XWidget*)(self))
#define XAbstractSpinBox_clearFocus(self) XWidget_clearFocus((XWidget*)(self))
#define XAbstractSpinBox_hasFocus(self) XWidget_hasFocus((const XWidget*)(self))
#define XAbstractSpinBox_setFocusPolicy(self, policy) XWidget_setFocusPolicy((XWidget*)(self), (policy))
#define XAbstractSpinBox_setAttribute(self, attribute, on) XWidget_setAttribute((XWidget*)(self), (attribute), (on))
#define XAbstractSpinBox_testAttribute(self, attribute) XWidget_testAttribute((const XWidget*)(self), (attribute))
#define XAbstractSpinBox_setContentsMargins(self, l, t, r, b) XWidget_setContentsMargins((XWidget*)(self), (l), (t), (r), (b))
#define XAbstractSpinBox_contentsMargins(self) XWidget_contentsMargins((const XWidget*)(self))
#define XAbstractSpinBox_setWindowFlags(self, flags) XWidget_setWindowFlags((XWidget*)(self), (flags))
#define XAbstractSpinBox_updateGeometry(self) XWidget_updateGeometry((XWidget*)(self))


/* ==================== 属性（对标 QAbstractSpinBox public API） ==================== */

/** @brief 获取内嵌编辑框（借用指针；对标 QAbstractSpinBox::lineEdit）。 */
XLineEdit* XAbstractSpinBox_lineEdit(const XAbstractSpinBox* self);
/** @brief 替换内嵌编辑框（旧对象释放；lineEdit 为 NULL 时内部新建；对标 setLineEdit）。 */
void XAbstractSpinBox_setLineEdit(XAbstractSpinBox* self, XLineEdit* lineEdit);
/** @brief 查询按钮符号（对标 QAbstractSpinBox::buttonSymbols）。 */
int XAbstractSpinBox_buttonSymbols(const XAbstractSpinBox* self);
/** @brief 设置按钮符号并重绘（对标 QAbstractSpinBox::setButtonSymbols）。 */
void XAbstractSpinBox_setButtonSymbols(XAbstractSpinBox* self, int symbols);
/** @brief 设置提交修正模式（对标 QAbstractSpinBox::setCorrectionMode）。 */
void XAbstractSpinBox_setCorrectionMode(XAbstractSpinBox* self, int mode);
/** @brief 查询提交修正模式（对标 QAbstractSpinBox::correctionMode）。 */
int XAbstractSpinBox_correctionMode(const XAbstractSpinBox* self);
/** @brief 当前文本是否满足校验（对标 QAbstractSpinBox::hasAcceptableInput）。 */
bool XAbstractSpinBox_hasAcceptableInput(const XAbstractSpinBox* self);
/** @brief 获取编辑框当前文本（借用指针；含前缀/后缀，对标 QAbstractSpinBox::text）。 */
const char* XAbstractSpinBox_text(const XAbstractSpinBox* self);
/** @brief 查询特殊值文本（借用指针；未设置为空串；对标 specialValueText）。 */
const char* XAbstractSpinBox_specialValueText(const XAbstractSpinBox* self);
/** @brief 设置特殊值文本（value==minimum 时整段替换显示；空串关闭；对标 setSpecialValueText）。 */
void XAbstractSpinBox_setSpecialValueText(XAbstractSpinBox* self, const char* text);
/** @brief 查询循环步进开关（对标 QAbstractSpinBox::wrapping）。 */
bool XAbstractSpinBox_wrapping(const XAbstractSpinBox* self);
/** @brief 设置循环步进开关（越过边界绕回；对标 QAbstractSpinBox::setWrapping）。 */
void XAbstractSpinBox_setWrapping(XAbstractSpinBox* self, bool wrapping);
/** @brief 查询只读（转发内嵌编辑框；对标 QAbstractSpinBox::isReadOnly）。 */
bool XAbstractSpinBox_isReadOnly(const XAbstractSpinBox* self);
/** @brief 设置只读（转发内嵌编辑框；对标 QAbstractSpinBox::setReadOnly）。 */
void XAbstractSpinBox_setReadOnly(XAbstractSpinBox* self, bool readOnly);
/** @brief 查询键盘跟踪（对标 QAbstractSpinBox::keyboardTracking）。 */
bool XAbstractSpinBox_keyboardTracking(const XAbstractSpinBox* self);
/** @brief 设置键盘跟踪（true：每次键入提交文本；对标 setKeyboardTracking）。 */
void XAbstractSpinBox_setKeyboardTracking(XAbstractSpinBox* self, bool tracking);
/** @brief 查询文本对齐（转发内嵌编辑框；对标 QAbstractSpinBox::alignment）。 */
int XAbstractSpinBox_alignment(const XAbstractSpinBox* self);
/** @brief 设置文本对齐（转发内嵌编辑框；对标 QAbstractSpinBox::setAlignment）。 */
void XAbstractSpinBox_setAlignment(XAbstractSpinBox* self, int alignment);
/** @brief 查询边框开关（默认 true；对标 QAbstractSpinBox::hasFrame）。 */
bool XAbstractSpinBox_hasFrame(const XAbstractSpinBox* self);
/** @brief 设置边框开关并重绘（对标 QAbstractSpinBox::setFrame）。 */
void XAbstractSpinBox_setFrame(XAbstractSpinBox* self, bool on);
/** @brief 设置加速开关（按住步进按钮时逐拍缩短重复间隔；对标 setAccelerated）。 */
void XAbstractSpinBox_setAccelerated(XAbstractSpinBox* self, bool on);
/** @brief 查询加速开关（对标 QAbstractSpinBox::isAccelerated）。 */
bool XAbstractSpinBox_isAccelerated(const XAbstractSpinBox* self);
/** @brief 设置千分位分隔显示（字段 + 显示刷新；对标 setGroupSeparatorShown）。 */
void XAbstractSpinBox_setGroupSeparatorShown(XAbstractSpinBox* self, bool shown);
/** @brief 查询千分位分隔显示（对标 QAbstractSpinBox::isGroupSeparatorShown）。 */
bool XAbstractSpinBox_isGroupSeparatorShown(const XAbstractSpinBox* self);
/** @brief 提交编辑文本（解析/修正后更新显示并发射信号；对标 QAbstractSpinBox::interpretText）。 */
void XAbstractSpinBox_interpretText(XAbstractSpinBox* self);

/* ==================== 公共槽（对标 QAbstractSpinBox public slots） ==================== */

/** @brief 上步进一次（等价 stepBy(+1)；对标 QAbstractSpinBox::stepUp）。 */
void XAbstractSpinBox_stepUp(XAbstractSpinBox* self);
/** @brief 下步进一次（等价 stepBy(-1)；对标 QAbstractSpinBox::stepDown）。 */
void XAbstractSpinBox_stepDown(XAbstractSpinBox* self);
/** @brief 全选编辑框文本（转发内嵌编辑框；对标 QAbstractSpinBox::selectAll）。 */
void XAbstractSpinBox_selectAll(XAbstractSpinBox* self);
/**
 * @brief      清空编辑框（对标 QAbstractSpinBox::clear 公共虚槽）。
 * @details    Qt 语义（qabstractspinbox.cpp:510-517）：
 *             `d->edit->setText(d->prefix + d->suffix)` + 光标移到前缀末 +
 *             置内部 cleared 标志（**置位在 setText 之后**——否则文本变化
 *             回调会把标志冲掉）。随后的 updateEdit/interpret 在 cleared
 *             期间早退（文本保持清空态，直到 setValue 或用户重新键入）。
 *             前缀/后缀经 PrefixText/SuffixText 虚槽取得（XSpinBox 返回
 *             自有前后缀文本，其余子类为空）。
 *             本函数经 EXAbstractSpinBox_Clear 虚槽分派；XDateTimeEdit 重载
 *             为「只清当前分段」（对标 QDateTimeEdit::clear → clearSection）。
 * @param      self 微调框对象；NULL 时零操作。
 * @return     无返回值。
 */
void XAbstractSpinBox_clear(XAbstractSpinBox* self);

/* ==================== 步进按钮长按连发（对标 Qt spinClick 定时器族） ==================== */

/**
 * @brief      步进按钮按下（步进一次 + 布置长按阈值/重复定时器）。
 * @details    对标 Qt QAbstractSpinBoxPrivate::updateState(true/false,
 *             fromKeyboard=false)：已处于同向按住态时幂等返回；否则先
 *             复位连发状态，再按 stepEnabled 判定方向可用后置
 *             buttonState=Mouse|Up/Down、立即 stepBy(±1)（Ctrl 按住时
 *             ±10）、启动阈值定时器（样式
 *             SH_SpinBox_ClickAutoRepeatThreshold，缺省 500ms）。阈值到
 *             期后以 SH_SpinBox_ClickAutoRepeatRate（缺省 150ms）重复
 *             步进；accelerated=true 时每拍把间隔缩短
 *             `interval*0.05` ms（下限 10ms）。
 *             子类（XSpinBox）在鼠标命中步进按钮时调用；XDateTimeEdit
 *             的步进按钮点击经父类链落到同一入口。
 * @param      self 微调框对象；NULL 时零操作。
 * @param      up   true=上按钮，false=下按钮。
 * @return     无返回值。
 */
void XAbstractSpinBox_spinButtonPress(XAbstractSpinBox* self, bool up);
/**
 * @brief      步进按钮抬起/移出（停止长按连发并复位按钮态）。
 * @details    对标 Qt QAbstractSpinBoxPrivate::reset() 的定时器部分。
 * @param      self 微调框对象；NULL 时零操作。
 * @return     无返回值。
 */
void XAbstractSpinBox_spinButtonRelease(XAbstractSpinBox* self);
/**
 * @brief      是否有步进按钮处于按住态（含连发期间）。
 * @param      self 微调框对象；NULL 返回 false。
 * @return     按住返回 true。
 */
bool XAbstractSpinBox_isSpinButtonActive(const XAbstractSpinBox* self);
/**
 * @brief      查询步进按钮状态位（XAbstractSpinBoxButtonState 位组合）。
 * @param      self 微调框对象；NULL 返回 None。
 * @return     状态位组合。
 */
int XAbstractSpinBox_buttonState(const XAbstractSpinBox* self);
/**
 * @brief      记录按住期间步进修饰键（Ctrl）是否按下（连发拍 ×10）。
 * @details    对标 Qt 私有 `keyboardModifiers` 在 mousePress/Move/Release
 *             时采样、timerEvent 读它决定 ×10。子类在按下/移动时调用。
 * @param      self 微调框对象；NULL 时零操作。
 * @param      down true=Ctrl 按住。
 * @return     无返回值。
 */
void XAbstractSpinBox_setStepModifierDown(XAbstractSpinBox* self, bool down);

/* ==================== 虚槽（子类重载；*_base 为多态调度入口） ==================== */

/**
 * @brief      步进虚槽（对标 QAbstractSpinBox::stepBy）。
 * @details    子类重载：按 steps（可为负）调整当前值并保持显示同步；
 *             基类默认实现为空操作。steps 的绝对值极大（≥ INT_MAX/4）
 *             在 XSpinBox 子类中保留「边界跳转」语义供程序性调用
 *             （键盘 Home/End 已按 Qt 交内嵌编辑框处理，不再触发）。
 * @param      self  微调框对象。
 * @param      steps 步数（正=增，负=减）。
 * @return     无返回值。
 */
void XAbstractSpinBox_stepBy_base(XAbstractSpinBox* self, int steps);
/**
 * @brief      文本校验虚槽（对标 QAbstractSpinBox::validate）。
 * @details    子类重载返回 XValidatorState；基类默认恒为 Acceptable。
 * @param      self  微调框对象。
 * @param      input 待校验的完整显示文本（只读借用；含前缀/后缀）。
 * @param      pos   光标位置（输入输出；本实现仅占位，不修改）。
 * @return     校验状态（XValidatorState_Invalid/Intermediate/Acceptable）。
 */
XValidatorState XAbstractSpinBox_validate_base(XAbstractSpinBox* self,
                                               const char* input, int* pos);
/**
 * @brief      文本修正虚槽（对标 QAbstractSpinBox::fixup）。
 * @details    子类重载把非法文本就地修正为可校验文本（只删除字符，
 *             不扩容）；基类默认实现为空操作。
 * @param      self     微调框对象。
 * @param      input    就地修正的文本缓冲（调用方提供，可被修改）。
 * @param      capacity input 缓冲容量（字节）。
 * @return     无返回值。
 */
void XAbstractSpinBox_fixup_base(XAbstractSpinBox* self, char* input,
                                 size_t capacity);
/**
 * @brief      清空虚槽（对标 QAbstractSpinBox::clear）。
 * @details    基类默认清空编辑框并置内部 cleared 标志；XSpinBox 重载为
 *             保留前缀/后缀清空数值部分。
 * @param      self 微调框对象。
 * @return     无返回值。
 */
void XAbstractSpinBox_clear_base(XAbstractSpinBox* self);
/**
 * @brief      步进使能虚槽（对标 QAbstractSpinBox::stepEnabled）。
 * @details    子类重载按当前值是否到边界返回 XAbstractSpinBoxStepEnabled
 *             位组合；基类默认返回 StepNone（抽象基类无范围语义）。
 * @param      self 微调框对象。
 * @return     步进使能位组合。
 */
int XAbstractSpinBox_stepEnabled_base(XAbstractSpinBox* self);
/**
 * @brief      内部提交虚槽：解释编辑框文本并应用值语义（内部钩子，
 *             对标 Qt 私有 QAbstractSpinBoxPrivate::interpret）。
 * @details    供 XAbstractSpinBox_interpretText 分派；子类重载实现
 *             解析/修正/钳位/信号；基类默认空操作。
 * @param      self 微调框对象。
 * @return     无返回值。
 */
void XAbstractSpinBox_interpret_base(XAbstractSpinBox* self);
/**
 * @brief      内部显示刷新虚槽（内部钩子，对标 Qt 私有 updateEdit）。
 * @details    值/属性变化后刷新编辑框显示文本；基类默认仅重绘，
 *             XSpinBox 重载为按 textFromValue 刷新。cleared 置位期间
 *             实现方必须早退（Qt updateEdit 的 `|| cleared` 分支）。
 * @param      self 微调框对象。
 * @return     无返回值。
 */
void XAbstractSpinBox_updateEdit_base(XAbstractSpinBox* self);

/**
 * @brief      前缀文本虚槽调度（借用；基类空串，XSpinBox 返回其前缀）。
 * @details    对标 Qt QAbstractSpinBoxPrivate::prefix——首页/末页光标
 *             选区与 clear() 的组合文本都依赖它，Qt 把它放在基类私有
 *             字段；本库前缀归 XSpinBox 所有，故以虚槽取出（借用指针，
 *             调用方不得释放；NULL 控件返回空串）。
 * @param      self 微调框对象。
 * @return     UTF-8 前缀文本（借用）。
 */
const char* XAbstractSpinBox_prefixText_base(const XAbstractSpinBox* self);
/**
 * @brief      后缀文本虚槽调度（借用；基类空串）。
 * @param      self 微调框对象。
 * @return     UTF-8 后缀文本（借用）。
 */
const char* XAbstractSpinBox_suffixText_base(const XAbstractSpinBox* self);

/* ==================== 尺寸提示族共用预算（XSpinBox/XDateTimeEdit 共用） ==================== */

/**
 * @brief      按给定文本探针宽预算整框宽度（步进列+外框的「绘制同源」
 *             不动点解）。
 * @details    步进按钮列宽公式随**控件最终高度**变化（列高 bh=h/2−fw、
 *             列宽 bw=max(16, min(bh*8/5, w/4))），且 w/4 项会把 hint
 *             加进来的每一分余量又吃掉 1/4——按内容高度一次代入会系统性
 *             预算不足（走查实锅：HSF 各页 setFixedHeight(40) 的框
 *             「1440 rpm」首位被裁成竖条）。本函数用样式自己的
 *             subControlRect（与绘制同一条几何路径）做 4 轮不动点迭代：
 *             以最小列 16px 起步，逐步收敛到「该宽度下绘制侧真会占用的
 *             列宽」。高度基线取内嵌编辑框提示高，再夹进控件的
 *             min/max 高度约束（setFixedHeight ⇒ 约束生效）。
 * @param      self       微调族控件（XSpinBox/XDateTimeEdit…）。
 * @param      probeWidth 文本探针宽（调用方度量；**须含内嵌编辑框自身
 *                        框边余量 chrome**——编辑框内部那 2×(frame?4:2)
 *                        px 从编辑区里扣，样式几何不含它）。
 * @return     整框建议宽度（>= probeWidth；无样式时退回
 *             probeWidth+16+2px 兜底）。
 */
int XAbstractSpinBox_hintWidthFor(const XAbstractSpinBox* self,
                                  int probeWidth);

/**
 * @brief      取微调族外框宽（样式 PM_SpinBoxFrameWidth；与绘制同源）。
 * @details    供调用方把高度补成「内容高 + 2×外框」（hintWidthFor 只
 *             管宽度）。无样式时回退 1px。
 */
int XAbstractSpinBox_frameWidthFor(const XAbstractSpinBox* self);

/* ==================== 信号 ==================== */

/** @brief editingFinished() 信号标识（内嵌编辑框 Return/失焦转发）。 */
void* XAbstractSpinBox_editingFinished_signal(XAbstractSpinBox* self);

#ifdef __cplusplus
}
#endif
#endif /* XWIDGET_ON && XABSTRACTSPINBOX_ON && XLINEEDIT_ON */

#endif /* XGUI_ON */

#endif /* XABSTRACTSPINBOX_H */
