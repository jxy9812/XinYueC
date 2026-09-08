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
 *               Home/End 边界跳转、Return 提交）、wheelEvent（滚轮步进，
 *               120 角度增量=1 步）、focusOutEvent 失焦提交 editingFinished、
 *               close/hide/show/timer 转发父类；
 *             - 信号：editingFinished（内嵌编辑框 Return/失焦转发）。
 * @note       模块总开关 XABSTRACTSPINBOX_ON 定义于 XGuiConfig.h；
 *             =0 时裁剪全部公共 API。依赖 XWIDGET_ON、XLINEEDIT_ON、
 *             XWINDOWEVENT_ON（滚轮）。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XABSTRACTSPINBOX_H
#define XABSTRACTSPINBOX_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XGuiConfig.h"
#include "XWidget.h"
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

/* ==================== 虚函数表（StepBy + 校验/修正/步进/提交钩子） ==================== */
XCLASS_DEFINE_BEGING(XAbstractSpinBox)
XCLASS_DEFINE_ENUM(XAbstractSpinBox, StepBy) = XCLASS_VTABLE_GET_SIZE(XWidget),
XCLASS_DEFINE_ENUM(XAbstractSpinBox, Validate),
XCLASS_DEFINE_ENUM(XAbstractSpinBox, Fixup),
XCLASS_DEFINE_ENUM(XAbstractSpinBox, Clear),
XCLASS_DEFINE_ENUM(XAbstractSpinBox, StepEnabled),
XCLASS_DEFINE_ENUM(XAbstractSpinBox, Interpret),
XCLASS_DEFINE_ENUM(XAbstractSpinBox, UpdateEdit),
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
 *             - m_accelerated：按住按钮加速（字段保留，行为后续扩展）；
 *             - m_groupSeparatorShown：千分位分隔显示（默认 false）；
 *             - m_specialValueText：特殊值文本（拥有；value==minimum 时显示）；
 *             - m_cleared：内部标志（clear() 后待解释状态，对标 Qt 私有 cleared）；
 *             - m_wheelDeltaRemainder：滚轮角度累积余数（内部）。
 *             调用者不得手工修改字段；一律走公开 API。
 */
typedef struct XAbstractSpinBox
{
    XWidget m_base;                    /**< 基类成员；必须是第一个。 */
    XLineEdit* m_lineEdit;             /**< 内嵌编辑框（拥有）。 */
    int     m_buttonSymbols;           /**< 按钮符号。 */
    bool    m_keyboardTracking;        /**< 键盘跟踪。 */
    bool    m_frame;                   /**< 边框开关。 */
    int     m_correctionMode;          /**< 提交修正模式。 */
    bool    m_wrapping;                /**< 循环步进开关。 */
    bool    m_accelerated;             /**< 加速开关（行为后续扩展）。 */
    bool    m_groupSeparatorShown;     /**< 千分位分隔显示。 */
    char*   m_specialValueText;        /**< 特殊值文本（拥有；NULL=未设置）。 */
    bool    m_cleared;                 /**< 内部：clear() 后待解释标志。 */
    int     m_wheelDeltaRemainder;     /**< 内部：滚轮角度累积余数。 */
} XAbstractSpinBox;

/* ==================== 生命周期 ==================== */

XVtable* XAbstractSpinBox_class_init(void);
void XAbstractSpinBox_init(XAbstractSpinBox* self, XWidget* parent, XWidgetFlags flags);
XAbstractSpinBox* XAbstractSpinBox_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags);
#define XAbstractSpinBox_deinit_base(self) XWidget_deinit_base((XWidget*)(self))
#define XAbstractSpinBox_delete_base(self) XWidget_delete_base((XWidget*)(self))

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
/** @brief 设置加速开关（字段级；长按连续步进为后续扩展；对标 setAccelerated）。 */
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

/* ==================== 虚槽（子类重载；*_base 为多态调度入口） ==================== */

/**
 * @brief      步进虚槽（对标 QAbstractSpinBox::stepBy）。
 * @details    子类重载：按 steps（可为负）调整当前值并保持显示同步；
 *             基类默认实现为空操作。steps 的绝对值极大（≥ INT_MAX/4，
 *             由 Home/End 键触发）时表示边界跳转语义。
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
 *             XSpinBox 重载为按 textFromValue 刷新。
 * @param      self 微调框对象。
 * @return     无返回值。
 */
void XAbstractSpinBox_updateEdit_base(XAbstractSpinBox* self);

/* ==================== 信号 ==================== */

/** @brief editingFinished() 信号标识（内嵌编辑框 Return/失焦转发）。 */
void* XAbstractSpinBox_editingFinished_signal(XAbstractSpinBox* self);

#ifdef __cplusplus
}
#endif
#endif /* XWIDGET_ON && XABSTRACTSPINBOX_ON && XLINEEDIT_ON */

#ifdef __cplusplus
}
#endif
#endif /* XABSTRACTSPINBOX_H */
