/**
 * @file       XSpinBox.h
 * @brief      XSpinBox 整数微调框控件（对齐 Qt 6.8 QSpinBox 公共 API）。
 * @details    内嵌 XLineEdit 子控件承担文本编辑（数值输入/回退），
 *             右侧自绘上/下箭头（点击递增/递减 singleStep）；文本
 *             变化实时解析（合法则更新 value 并发射 valueChanged，
 *             非法保持原值），Return/失焦提交（按 correctionMode
 *             修正或恢复）。支持 prefix/suffix 前后缀、displayIntegerBase
 *             进制显示（2/8/10/16）、groupSeparatorShown 千分位
 *             （继承基类）、specialValueText（继承基类）、wrapping 循环
 *             （继承基类）、Home/End 边界跳转。
 *             readOnly/alignment/keyboardTracking 等转发内嵌 XLineEdit，
 *             全部继承自 XAbstractSpinBox（对齐 Qt 继承关系）。
 * @note       模块总开关 XSPINBOX_ON 定义于 XGuiConfig.h；=0 时裁剪
 *             全部公共 API。依赖 XWIDGET_ON、XLINEEDIT_ON、
 *             XABSTRACTSPINBOX_ON、XPALETTE_ON、XPAINTER_ON。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XSPINBOX_H
#define XSPINBOX_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XWidget.h"
#include "XAbstractSpinBox.h"

#if XWIDGET_ON && XSPINBOX_ON && XLINEEDIT_ON && XABSTRACTSPINBOX_ON

/* ==================== 类定义（自有虚槽：ValueFromText/TextFromValue） ==================== */
XCLASS_DEFINE_BEGING(XSpinBox)
XCLASS_DEFINE_ENUM(XSpinBox, ValueFromText) = XCLASS_VTABLE_GET_SIZE(XAbstractSpinBox),
XCLASS_DEFINE_ENUM(XSpinBox, TextFromValue),
XCLASS_DEFINE_END(XSpinBox)

/**
 * @brief      XSpinBox 微调框对象；m_base 必须是第一个成员。
 * @details    字段含义：
 *             - m_min/m_max：数值范围（默认 0/99）；
 *             - m_value：当前值（默认 0）；
 *             - m_singleStep：单步（默认 1，上/下箭头与文本提交
 *               解析共用）；
 *             - m_stepType：步进类型（默认 DefaultStepType；字段保留，
 *               自适应步长行为为后续扩展）；
 *             - m_displayIntegerBase：显示进制（默认 10，有效 2~36）；
 *             - m_prefix/m_suffix：前缀/后缀文本（拥有）；
 *             - m_textDirty：文本与当前值不一致（编辑中）标志。
 *             调用者不得手工修改字段；一律走公开 API。
 */
typedef struct XSpinBox
{
    XAbstractSpinBox m_base;         /**< 基类成员；必须是第一个。 */
    int     m_min;                   /**< 范围下限。 */
    int     m_max;                   /**< 范围上限。 */
    int     m_value;                 /**< 当前值。 */
    int     m_singleStep;            /**< 单步。 */
    int     m_stepType;              /**< 步进类型（XAbstractSpinBoxStepType）。 */
    int     m_displayIntegerBase;    /**< 显示进制（默认 10）。 */
    char*   m_prefix;                /**< 前缀（拥有；NULL=空）。 */
    char*   m_suffix;                /**< 后缀（拥有；NULL=空）。 */
    bool    m_textDirty;             /**< 文本编辑中标志。 */
} XSpinBox;

/* ==================== 生命周期 ==================== */

XVtable* XSpinBox_class_init(void);
void XSpinBox_init(XSpinBox* self, XWidget* parent, XWidgetFlags flags);
#define XSpinBox_create(parent, flags) XSpinBox_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))
XSpinBox* XSpinBox_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags);
#define XSpinBox_deinit_base(self) XWidget_deinit_base((XWidget*)(self))
#define XSpinBox_delete_base(self) XWidget_delete_base((XWidget*)(self))

/* ==================== 父类 API 宏转发（对齐库内 XRadioButton 惯例） ==================== */

#define XSpinBox_lineEdit(self) XAbstractSpinBox_lineEdit((const XAbstractSpinBox*)(self))
#define XSpinBox_setLineEdit(self, edit) XAbstractSpinBox_setLineEdit((XAbstractSpinBox*)(self), (edit))
#define XSpinBox_buttonSymbols(self) XAbstractSpinBox_buttonSymbols((const XAbstractSpinBox*)(self))
#define XSpinBox_setButtonSymbols(self, symbols) XAbstractSpinBox_setButtonSymbols((XAbstractSpinBox*)(self), (symbols))
#define XSpinBox_correctionMode(self) XAbstractSpinBox_correctionMode((const XAbstractSpinBox*)(self))
#define XSpinBox_setCorrectionMode(self, mode) XAbstractSpinBox_setCorrectionMode((XAbstractSpinBox*)(self), (mode))
#define XSpinBox_hasAcceptableInput(self) XAbstractSpinBox_hasAcceptableInput((const XAbstractSpinBox*)(self))
#define XSpinBox_text(self) XAbstractSpinBox_text((const XAbstractSpinBox*)(self))
#define XSpinBox_specialValueText(self) XAbstractSpinBox_specialValueText((const XAbstractSpinBox*)(self))
#define XSpinBox_setSpecialValueText(self, text) XAbstractSpinBox_setSpecialValueText((XAbstractSpinBox*)(self), (text))
#define XSpinBox_wrapping(self) XAbstractSpinBox_wrapping((const XAbstractSpinBox*)(self))
#define XSpinBox_setWrapping(self, w) XAbstractSpinBox_setWrapping((XAbstractSpinBox*)(self), (w))
#define XSpinBox_isReadOnly(self) XAbstractSpinBox_isReadOnly((const XAbstractSpinBox*)(self))
#define XSpinBox_setReadOnly(self, r) XAbstractSpinBox_setReadOnly((XAbstractSpinBox*)(self), (r))
#define XSpinBox_keyboardTracking(self) XAbstractSpinBox_keyboardTracking((const XAbstractSpinBox*)(self))
#define XSpinBox_setKeyboardTracking(self, kt) XAbstractSpinBox_setKeyboardTracking((XAbstractSpinBox*)(self), (kt))
#define XSpinBox_alignment(self) XAbstractSpinBox_alignment((const XAbstractSpinBox*)(self))
#define XSpinBox_setAlignment(self, a) XAbstractSpinBox_setAlignment((XAbstractSpinBox*)(self), (a))
#define XSpinBox_hasFrame(self) XAbstractSpinBox_hasFrame((const XAbstractSpinBox*)(self))
#define XSpinBox_setFrame(self, on) XAbstractSpinBox_setFrame((XAbstractSpinBox*)(self), (on))
#define XSpinBox_setAccelerated(self, on) XAbstractSpinBox_setAccelerated((XAbstractSpinBox*)(self), (on))
#define XSpinBox_isAccelerated(self) XAbstractSpinBox_isAccelerated((const XAbstractSpinBox*)(self))
#define XSpinBox_setGroupSeparatorShown(self, s) XAbstractSpinBox_setGroupSeparatorShown((XAbstractSpinBox*)(self), (s))
#define XSpinBox_isGroupSeparatorShown(self) XAbstractSpinBox_isGroupSeparatorShown((const XAbstractSpinBox*)(self))
#define XSpinBox_interpretText(self) XAbstractSpinBox_interpretText((XAbstractSpinBox*)(self))
#define XSpinBox_stepUp(self) XAbstractSpinBox_stepUp((XAbstractSpinBox*)(self))
#define XSpinBox_stepDown(self) XAbstractSpinBox_stepDown((XAbstractSpinBox*)(self))
#define XSpinBox_selectAll(self) XAbstractSpinBox_selectAll((XAbstractSpinBox*)(self))
#define XSpinBox_stepBy_base(self, steps) XAbstractSpinBox_stepBy_base((XAbstractSpinBox*)(self), (steps))
#define XSpinBox_validate_base(self, input, pos) XAbstractSpinBox_validate_base((XAbstractSpinBox*)(self), (input), (pos))
#define XSpinBox_fixup_base(self, input, size) XAbstractSpinBox_fixup_base((XAbstractSpinBox*)(self), (input), (size))
#define XSpinBox_clear_base(self) XAbstractSpinBox_clear_base((XAbstractSpinBox*)(self))
#define XSpinBox_stepEnabled_base(self) XAbstractSpinBox_stepEnabled_base((XAbstractSpinBox*)(self))
#define XSpinBox_interpret_base(self) XAbstractSpinBox_interpret_base((XAbstractSpinBox*)(self))
#define XSpinBox_updateEdit_base(self) XAbstractSpinBox_updateEdit_base((XAbstractSpinBox*)(self))
#define XSpinBox_editingFinished_signal(self) XAbstractSpinBox_editingFinished_signal((XAbstractSpinBox*)(self))

/* ==================== 数值 API（对标 QSpinBox public API） ==================== */

/** @brief 查询数值下限（默认 0；对标 QSpinBox::minimum）。 */
int XSpinBox_minimum(const XSpinBox* self);
/** @brief 设置数值下限（超过上限时上限同步调整；对标 QSpinBox::setMinimum）。 */
void XSpinBox_setMinimum(XSpinBox* self, int min);
/** @brief 查询数值上限（默认 99；对标 QSpinBox::maximum）。 */
int XSpinBox_maximum(const XSpinBox* self);
/** @brief 设置数值上限（低于下限时下限同步调整；对标 QSpinBox::setMaximum）。 */
void XSpinBox_setMaximum(XSpinBox* self, int max);
/** @brief 设置数值范围（min>max 时按 Qt 语义取 (min,min)；值重钳位；对标 setRange）。 */
void XSpinBox_setRange(XSpinBox* self, int min, int max);
/** @brief 查询当前值（对标 QSpinBox::value）。 */
int XSpinBox_value(const XSpinBox* self);
/** @brief 设置当前值（钳位；变化时更新文本并发射 valueChanged；对标 QSpinBox::setValue）。 */
void XSpinBox_setValue(XSpinBox* self, int value);
/** @brief 查询单步（默认 1；对标 QSpinBox::singleStep）。 */
int XSpinBox_singleStep(const XSpinBox* self);
/** @brief 设置单步（<0 忽略；对标 QSpinBox::setSingleStep）。 */
void XSpinBox_setSingleStep(XSpinBox* self, int step);
/** @brief 查询前缀（借用指针；未设置为空串；对标 QSpinBox::prefix）。 */
const char* XSpinBox_prefix(const XSpinBox* self);
/** @brief 设置前缀（显示 = 前缀 + 数值 + 后缀；对标 QSpinBox::setPrefix）。 */
void XSpinBox_setPrefix(XSpinBox* self, const char* prefix);
/** @brief 查询后缀（借用指针；未设置为空串；对标 QSpinBox::suffix）。 */
const char* XSpinBox_suffix(const XSpinBox* self);
/** @brief 设置后缀（对标 QSpinBox::setSuffix）。 */
void XSpinBox_setSuffix(XSpinBox* self, const char* suffix);
/**
 * @brief      获取纯净文本（无前缀/后缀/千分位/首尾空白；对标 QSpinBox::cleanText）。
 * @return     新分配的字符串（调用者用 XFree_System 释放；失败返回 NULL）。
 */
char* XSpinBox_cleanText(const XSpinBox* self);
/** @brief 查询步进类型（对标 QSpinBox::stepType）。 */
int XSpinBox_stepType(const XSpinBox* self);
/** @brief 设置步进类型（字段级；自适应步长行为为后续扩展；对标 QSpinBox::setStepType）。 */
void XSpinBox_setStepType(XSpinBox* self, int stepType);
/** @brief 查询显示进制（默认 10；对标 QSpinBox::displayIntegerBase）。 */
int XSpinBox_displayIntegerBase(const XSpinBox* self);
/** @brief 设置显示进制（2~36，越界回退 10；对标 QSpinBox::setDisplayIntegerBase）。 */
void XSpinBox_setDisplayIntegerBase(XSpinBox* self, int base);

/* ==================== 虚槽（多态调度入口） ==================== */

/**
 * @brief      文本转值虚槽（对标 QSpinBox::valueFromText）。
 * @details    解析完整显示文本（剥离前缀/后缀/千分位后按 displayIntegerBase
 *             解析）返回数值；解析失败或与特殊值文本相等时返回 minimum。
 * @param      self 微调框对象。
 * @param      text 完整显示文本（含前缀/后缀；只读借用）。
 * @return     解析得到的数值（失败返回 minimum）。
 */
int XSpinBox_valueFromText_base(const XSpinBox* self, const char* text);
/**
 * @brief      值转文本虚槽（对标 QSpinBox::textFromValue）。
 * @details    生成完整显示文本（前缀 + 按 displayIntegerBase/千分位格式化的
 *             数值 + 后缀）；value==minimum 且设置了特殊值文本时整段返回
 *             特殊值文本（无前缀/后缀）。
 * @param      self 微调框对象。
 * @param      val  要显示的数值。
 * @return     新分配的显示文本（调用者用 XFree_System 释放；失败返回 NULL）。
 */
char* XSpinBox_textFromValue_base(const XSpinBox* self, int val);

/* ==================== 信号 ==================== */

/** @brief valueChanged(int) 信号标识（对标 QSpinBox::valueChanged）。 */
void* XSpinBox_valueChanged_signal(XSpinBox* self);
/** @brief textChanged(const char*) 信号标识（编辑框文本变化转发；对标 QSpinBox::textChanged）。 */
void* XSpinBox_textChanged_signal(XSpinBox* self);

#ifdef __cplusplus
}
#endif
#endif /* XWIDGET_ON && XSPINBOX_ON && XLINEEDIT_ON && XABSTRACTSPINBOX_ON */

#ifdef __cplusplus
}
#endif
#endif /* XSPINBOX_H */
