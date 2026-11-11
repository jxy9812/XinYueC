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
 *             （继承基类）、长按步进按钮连发（继承基类 spinButtonPress）。
 *             readOnly/alignment/keyboardTracking 等转发内嵌 XLineEdit，
 *             全部继承自 XAbstractSpinBox（对齐 Qt 继承关系）。
 *             Home/End 与 Qt 6.8 同口径：不带 Shift 时交内嵌编辑框移动
 *             光标、值不变；带 Shift 时选到前缀后/后缀前。
 * @note       模块总开关 XSPINBOX_ON 定义于 XGuiConfig.h；=0 时裁剪
 *             全部公共 API。依赖 XWIDGET_ON、XLINEEDIT_ON、
 *             XABSTRACTSPINBOX_ON、XPALETTE_ON、XPAINTER_ON。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XSPINBOX_H
#define XSPINBOX_H

#if XGUI_ON
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XWidget.h"
#include "XAbstractSpinBox.h"
#include "XString.h"

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
 *             - m_stepType：步进类型（默认 DefaultStepType；自适应十进制
 *               步长按 Qt calculateAdaptiveDecimalStep 口径在 stepBy 中
 *               实算，见 XSpinBox_setStepType）；
 *             - m_displayIntegerBase：显示进制（默认 10，有效 2~36）；
 *             - m_prefix/m_suffix：前缀/后缀文本（拥有）；
 *             - m_textDirty：文本与当前值不一致（编辑中）标志。
 *             调用者不得手工修改字段；一律走公开 API。
 */
typedef struct XSpinBox
{
    XAbstractSpinBox m_base;         /**< 基类成员；必须是第一个。 */
    XString* m_prefix;               /**< 前缀（对象拥有；NULL=空）。 */
    XString* m_suffix;               /**< 后缀（对象拥有；NULL=空）。 */
    int     m_min;                   /**< 范围下限（数值域全 int，保留）。 */
    int     m_max;                   /**< 范围上限（数值域全 int，保留）。 */
    int     m_value;                 /**< 当前值（数值域全 int，保留）。 */
    int     m_singleStep;            /**< 单步（可任意 int，保留）。 */
    int     m_displayIntegerBase;    /**< 显示进制（默认 10；setter 往返任意值，保留）。 */
    /* [内存治理 2026-10-11] 标志位字：1 个 bool + 步进类型枚举
       （XAbstractSpinBoxStepType 0..1）合并 1 字。步进按钮按住态改由
       基类 m_buttonState 承载（对标 Qt 私有 buttonState，见
       XAbstractSpinBox）。 */
    uint32_t m_textDirty : 1;        /**< 文本编辑中标志。 */
    uint32_t m_stepType : 2;         /**< 步进类型（XAbstractSpinBoxStepType 0..1）。 */
    uint32_t m_spinBoxFlagsUnused : 29; /**< 保留位。 */
} XSpinBox;

/* ==================== 生命周期 ==================== */

/** @brief 初始化 XSpinBox 类虚函数表并返回共享表指针。 */
XVtable* XSpinBox_class_init(void);
/** @brief XSpin盒init（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param parent 父控件指针；可为 NULL。
 * @param flags 窗口标志位组合。
 * @return 无返回值。
 */
void XSpinBox_init(XSpinBox* self, XWidget* parent, XWidgetFlags flags);
/** @brief 使用默认内存类型创建微调框（默认构造；语义同 create_ex）。 */
#define XSpinBox_create(parent, flags) XSpinBox_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))
/** @brief XSpin盒createex（对标 Qt 同名接口）。
 * @param memory XMemoryType 参数。
 * @param parent 父控件指针；可为 NULL。
 * @param flags 窗口标志位组合。
 * @return 返回对象指针；无效时返回 NULL。
 */
XSpinBox* XSpinBox_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags);

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
/* setFrame 的立即重绘已在基类 setter 内（对标 QAbstractSpinBox::setFrame），
 * 此处纯转发。 */
#define XSpinBox_setFrame(self, on) XAbstractSpinBox_setFrame((XAbstractSpinBox*)(self), (on))
#define XSpinBox_setAccelerated(self, on) XAbstractSpinBox_setAccelerated((XAbstractSpinBox*)(self), (on))
#define XSpinBox_isAccelerated(self) XAbstractSpinBox_isAccelerated((const XAbstractSpinBox*)(self))
#define XSpinBox_setGroupSeparatorShown(self, s) XAbstractSpinBox_setGroupSeparatorShown((XAbstractSpinBox*)(self), (s))
#define XSpinBox_isGroupSeparatorShown(self) XAbstractSpinBox_isGroupSeparatorShown((const XAbstractSpinBox*)(self))
#define XSpinBox_interpretText(self) XAbstractSpinBox_interpretText((XAbstractSpinBox*)(self))
#define XSpinBox_stepUp(self) XAbstractSpinBox_stepUp((XAbstractSpinBox*)(self))
#define XSpinBox_stepDown(self) XAbstractSpinBox_stepDown((XAbstractSpinBox*)(self))
#define XSpinBox_selectAll(self) XAbstractSpinBox_selectAll((XAbstractSpinBox*)(self))
/* clear 是 QAbstractSpinBox 的公共虚槽：QSpinBox **不**重载它——Qt 的
 * 基类实现即 `edit->setText(prefix+suffix)` + 光标到前缀末 + cleared
 * 置位；本库同一口径（前缀/后缀经基类 PrefixText/SuffixText 虚槽取到
 * XSpinBox 的自有文本），故 XSpinBox 不再注册 Clear 重载。 */
#define XSpinBox_clear(self) XAbstractSpinBox_clear((XAbstractSpinBox*)(self))
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
 * @brief      获取纯净文本（剥前后缀与首尾空白；**保留千分位分隔符**；
 *             对标 QSpinBox::cleanText）。
 * @details    Qt QAbstractSpinBoxPrivate::stripped（qabstractspinbox.cpp:
 *             1451-1477）只在前缀/后缀确实匹配时剥掉它们并 trimmed，
 *             千分位分隔符**不剥**——实测 groupSeparatorShown 下
 *             cleanText 返回 '1,234,567'。特殊值文本整段匹配时原样返回
 *             （实测 'auto'）。
 * @return     新分配的字符串（调用者用 XFree_System 释放；失败返回 NULL）。
 */
char* XSpinBox_cleanText(const XSpinBox* self);
/** @brief 查询步进类型（对标 QSpinBox::stepType）。 */
int XSpinBox_stepType(const XSpinBox* self);
/**
 * @brief      设置步进类型（对标 QSpinBox::setStepType）。
 * @details    DefaultStepType=固定 singleStep；AdaptiveDecimalStepType=
 *             自适应十进制步长，按 Qt QSpinBoxPrivate::calculateAdaptiveDecimalStep
 *             （qspinbox.cpp:400-417）口径：
 *             |value|<100 时步长 1；否则 10^(floor(log10(|value| - 补偿)) - 1)
 *             （补偿=1 当 value 与 steps 异号，即向 0 方向步进）；
 *             实测 99→+1、100→+10、500→+10、999→+10、1000→+100、
 *             12345→+1000、-500→+10、-1000→+10、1000 stepBy(2)→+200。
 * @param      self 微调框对象。
 * @param      stepType 步进类型（XAbstractSpinBoxStepType）。
 * @return     无返回值。
 */
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
 * @details    Qt 语义（qspinbox.cpp:478-495）：返回**裸数值文本**（按
 *             displayIntegerBase 与千分位格式化），**不含**前缀/后缀，
 *             也**不**处理 specialValueText（后者由 updateEdit 单独整段
 *             替换，qabstractspinbox.cpp:1770-1773）。显示文本 =
 *             prefix + textFromValue + suffix 由本类内部组装。
 *             [契约变更 2026-11-11] 此前该虚槽在 value==minimum 时整段
 *             返回特殊值文本、且带前后缀，与 Qt 的虚槽契约不同（gap 96）。
 * @param      self 微调框对象。
 * @param      val  要显示的数值。
 * @return     新分配的裸数值文本（调用者用 XFree_System 释放；失败返回 NULL）。
 */
char* XSpinBox_textFromValue_base(const XSpinBox* self, int val);

/* ==================== 信号 ==================== */

/** @brief valueChanged(int) 信号标识（对标 QSpinBox::valueChanged）。 */
void* XSpinBox_valueChanged_signal(XSpinBox* self);
/** @brief textChanged(const char*) 信号标识（编辑框文本变化转发；对标 QSpinBox::textChanged）。 */
void* XSpinBox_textChanged_signal(XSpinBox* self);

#endif /* XWIDGET_ON && XSPINBOX_ON && XLINEEDIT_ON && XABSTRACTSPINBOX_ON */

#ifdef __cplusplus
}
#endif

#endif /* XGUI_ON */

#endif /* XSPINBOX_H */
