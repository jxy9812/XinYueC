/******************************************************************************
 * @file       XProgressDialog.h
 * @brief      XProgressDialog 进度对话框控件（对标 Qt 6.8 QProgressDialog
 *            : QDialog）。
 * @details    继承 XDialog，提供带取消能力的进度对话框公共 API 面：
 *             - 范围/值：setRange/setMinimum/setMaximum/minimum/maximum/
 *               setValue/value（value 达 maximum 且 autoReset 时复位，
 *               Qt 语义）；reset() 复位到 minimum；
 *             - 文本：setLabelText/labelText、setCancelButtonText、
 *               setBar（简化：借用 XProgressBar 指针存储）；
 *             - 行为：setAutoReset/autoReset（默认 true）、
 *               setAutoClose/autoClose（默认 true）、
 *               setMinimumDuration/minimumDuration（默认 4000ms）、
 *               cancel()/wasCanceled、forceShow；
 *             - 信号：canceled()（空参；cancel() 触发时发射）。
 * @note       模块总开关 XDIALOG_ON（XWIDGET_ON && XDIALOG_ON 有效）。
 * @note       本实现不创建真实的子控件布局；setBar 仅存储借用指针，
 *             渲染与原生进度面板为后续扩展。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XPROGRESSDIALOG_H
#define XPROGRESSDIALOG_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XWidget.h"
#include "XString.h"
#if XDIALOG_ON
#include "XDialog.h"
#endif /* XDIALOG_ON */

#if XWIDGET_ON && XDIALOG_ON

/** @brief XProgressBar 前向声明（setBar 借用指针；完整定义见 XProgressBar.h）。 */
typedef struct XProgressBar XProgressBar;
/** @brief XLabel 前向声明（setLabel 所有权转移；完整定义见 XLabel.h）。 */
typedef struct XLabel XLabel;
/** @brief XPushButton 前向声明（setCancelButton 所有权转移；完整定义见
 *         XPushButton.h）。 */
typedef struct XPushButton XPushButton;

XCLASS_DEFINE_BEGING(XProgressDialog)
XCLASS_DEFINE_EXTEND_END(XProgressDialog, XDialog)

/**
 * @brief      XProgressDialog 进度对话框对象；m_base 必须是第一个成员。
 * @details    默认范围 [0,100]、值 0、autoReset/autoClose 为 true、
 *             minimumDuration 4000ms；字符串字段为拥有型 XString*。
 */
typedef struct XProgressDialog
{
    XDialog m_base;              /**< 基类成员；必须是第一个。 */
    int m_minimum;               /**< 最小值；默认 0。 */
    int m_maximum;               /**< 最大值；默认 100。 */
    int m_value;                 /**< 当前值；默认 0。 */
    int m_minimumDuration;       /**< 显示最小时长（ms）；默认 4000。 */
    bool m_wasCanceled;          /**< 是否已取消（对标 wasCanceled）。 */
    bool m_autoReset;            /**< 达最大值自动复位；默认 true。 */
    bool m_autoClose;            /**< 达最大值/复位自动隐藏；默认 true。 */
    XString* m_labelText;        /**< 提示文本（拥有）。 */
    XString* m_cancelButtonText; /**< 取消按钮文本（拥有）。 */
    XProgressBar* m_bar;         /**< 进度条（借用；setBar 简化存储）。 */
    XLabel* m_label;             /**< 自定义标签控件（拥有；对标 setLabel）。 */
    XPushButton* m_cancelButton; /**< 自定义取消按钮（拥有；对标 setCancelButton）。 */
} XProgressDialog;

/**
 * @brief      XProgressDialog 类虚函数表初始化（对标 Qt 同名接口）。
 * @return     共享 XVtable 指针。
 */
XVtable* XProgressDialog_class_init(void);

/**
 * @brief      初始化 XProgressDialog（对标 QProgressDialog 构造）。
 * @param      self 目标对象指针；不可为 NULL。
 * @param      parent 父控件借用指针；可为 NULL。
 * @param      flags 窗口标志位组合。
 * @return     无返回值。
 */
void XProgressDialog_init(XProgressDialog* self, XWidget* parent,
                          XWidgetFlags flags);
/**
 * @brief      以完整参数初始化 XProgressDialog（对标 QProgressDialog
 *             (labelText, cancelButtonText, minimum, maximum, parent)）。
 * @param      self 目标对象指针；不可为 NULL。
 * @param      labelText 提示文本；可为 NULL。
 * @param      cancelButtonText 取消按钮文本；可为 NULL。
 * @param      minimum 最小值。
 * @param      maximum 最大值。
 * @param      parent 父控件借用指针；可为 NULL。
 * @return     无返回值。
 */
void XProgressDialog_init_full(XProgressDialog* self, const XString* labelText,
                               const XString* cancelButtonText, int minimum,
                               int maximum, XWidget* parent);
#define XProgressDialog_create(parent, flags) XProgressDialog_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))
/**
 * @brief      使用指定内存类型创建 XProgressDialog。
 * @param      memory 对象内存类型。
 * @param      parent 父控件借用指针；可为 NULL。
 * @param      flags 窗口标志位组合。
 * @return     新对象指针；失败返回 NULL。
 */
XProgressDialog* XProgressDialog_create_ex(XMemoryType memory, XWidget* parent,
                                           XWidgetFlags flags);
#define XProgressDialog_deinit_base(self) XWidget_deinit_base((XWidget*)(self))
#define XProgressDialog_delete_base(self) XClass_delete_base((XClass*)(self))

/* ==================== 范围与值（对标 QProgressDialog） ==================== */

/**
 * @brief      设置取值范围（对标 QProgressDialog::setRange）。
 * @details    minimum 大于 maximum 时自动交换（Qt 语义）。
 * @param      self 目标对话框。
 * @param      minimum 最小值。
 * @param      maximum 最大值。
 * @return     无返回值。
 */
void XProgressDialog_setRange(XProgressDialog* self, int minimum, int maximum);
/**
 * @brief      设置最小值（对标 QProgressDialog::setMinimum）。
 * @param      self 目标对话框。
 * @param      minimum 最小值。
 * @return     无返回值。
 */
void XProgressDialog_setMinimum(XProgressDialog* self, int minimum);
/**
 * @brief      设置最大值（对标 QProgressDialog::setMaximum）。
 * @param      self 目标对话框。
 * @param      maximum 最大值。
 * @return     无返回值。
 */
void XProgressDialog_setMaximum(XProgressDialog* self, int maximum);
/**
 * @brief      获取最小值（对标 QProgressDialog::minimum）。
 * @param      self 目标对话框；可为 NULL。
 * @return     最小值；无效返回 0。
 */
int XProgressDialog_minimum(const XProgressDialog* self);
/**
 * @brief      获取最大值（对标 QProgressDialog::maximum）。
 * @param      self 目标对话框；可为 NULL。
 * @return     最大值；无效返回 100。
 */
int XProgressDialog_maximum(const XProgressDialog* self);
/**
 * @brief      设置当前值（对标 QProgressDialog::setValue）。
 * @details    越界钳位到 [minimum, maximum]；当入参等于 maximum 且
 *             autoReset 为 true 时执行 reset()（Qt 语义：值复位到
 *             minimum、wasCanceled 复位、autoClose 时隐藏）。
 * @param      self 目标对话框。
 * @param      progress 新值。
 * @return     无返回值。
 */
void XProgressDialog_setValue(XProgressDialog* self, int progress);
/**
 * @brief      获取当前值（对标 QProgressDialog::value）。
 * @param      self 目标对话框；可为 NULL。
 * @return     当前值；无效返回 0。
 */
int XProgressDialog_value(const XProgressDialog* self);
/**
 * @brief      复位进度（对标 QProgressDialog::reset）。
 * @details    值复位到 minimum、wasCanceled 置 false；autoClose 为 true
 *             时隐藏对话框。
 * @param      self 目标对话框。
 * @return     无返回值。
 */
void XProgressDialog_reset(XProgressDialog* self);

/* ==================== 文本与取消（对标 QProgressDialog） ==================== */

/**
 * @brief      设置提示文本（对标 QProgressDialog::setLabelText）。
 * @param      self 目标对话框。
 * @param      text 提示文本；可为 NULL 清空。
 * @return     无返回值。
 */
void XProgressDialog_setLabelText(XProgressDialog* self, const XString* text);
/**
 * @brief      获取提示文本副本（对标 QProgressDialog::labelText）。
 * @param      self 目标对话框；可为 NULL。
 * @return     新建的 XString 拷贝，调用方拥有，须 XString_delete_base；
 *             无效时返回空串。
 */
XString* XProgressDialog_labelText(const XProgressDialog* self);
/**
 * @brief      设置取消按钮文本（对标 QProgressDialog::setCancelButtonText）。
 * @param      self 目标对话框。
 * @param      text 取消按钮文本；可为 NULL 清空。
 * @return     无返回值。
 */
void XProgressDialog_setCancelButtonText(XProgressDialog* self,
                                         const XString* text);

#if XFRAME_ON && XLABEL_ON
/**
 * @brief      设置自定义标签控件（对标 QProgressDialog::setLabel）。
 * @details    Qt 语义：对话框接管标签所有权，旧标签被删除；传入 NULL
 *             清除自定义标签（回到内建文本承载）。标签成为对话框子控件
 *             并显示在顶部文本行。
 * @param      self 目标对话框。
 * @param      label 新标签（所有权转移给对话框）；可为 NULL 清除。
 * @return     无返回值。
 */
void XProgressDialog_setLabel(XProgressDialog* self, XLabel* label);
/**
 * @brief      获取自定义标签控件（对标 QProgressDialog::label）。
 * @param      self 目标对话框；可为 NULL。
 * @return     标签借用指针；未设置自定义标签时为 NULL。
 */
XLabel* XProgressDialog_label(const XProgressDialog* self);
#endif /* XFRAME_ON && XLABEL_ON */

#if XABSTRACTBUTTON_ON && XPUSHBUTTON_ON
/**
 * @brief      设置自定义取消按钮（对标 QProgressDialog::setCancelButton）。
 * @details    Qt 语义：对话框接管按钮所有权，旧按钮被删除；按钮 clicked
 *             经内部槽调用 cancel()（等价 Qt 的 clicked→canceled→cancel
 *             链路）；传入 NULL 表示不再显示取消按钮（无法取消）。
 * @param      self 目标对话框。
 * @param      button 新取消按钮（所有权转移给对话框）；可为 NULL 移除。
 * @return     无返回值。
 */
void XProgressDialog_setCancelButton(XProgressDialog* self,
                                     XPushButton* button);
/**
 * @brief      获取自定义取消按钮（对标 QProgressDialog::cancelButton）。
 * @param      self 目标对话框；可为 NULL。
 * @return     按钮借用指针；未设置自定义取消按钮时为 NULL。
 */
XPushButton* XProgressDialog_cancelButton(const XProgressDialog* self);
#endif /* XABSTRACTBUTTON_ON && XPUSHBUTTON_ON */
/**
 * @brief      设置进度条（对标 QProgressDialog::setBar；简化：仅存储借用
 *             指针，不建立真实布局）。
 * @param      self 目标对话框。
 * @param      bar 进度条借用指针；可为 NULL 清除。
 * @return     无返回值。
 */
void XProgressDialog_setBar(XProgressDialog* self, XProgressBar* bar);
/**
 * @brief      请求取消（对标 QProgressDialog::cancel）。
 * @details    发射 canceled() 信号、置 wasCanceled 为 true，并复位进度
 *             （值到 minimum、autoClose 时隐藏），与 Qt 取消按钮点击的
 *             可观察效果一致。
 * @param      self 目标对话框。
 * @return     无返回值。
 */
void XProgressDialog_cancel(XProgressDialog* self);
/**
 * @brief      查询是否已取消（对标 QProgressDialog::wasCanceled）。
 * @param      self 目标对话框；可为 NULL。
 * @return     已取消返回 true；无效返回 false。
 */
bool XProgressDialog_wasCanceled(const XProgressDialog* self);

/* ==================== 行为属性（对标 QProgressDialog） ==================== */

/**
 * @brief      设置是否在达最大值时自动复位（对标 QProgressDialog::setAutoReset）。
 * @param      self 目标对话框。
 * @param      reset true=自动复位（默认）。
 * @return     无返回值。
 */
void XProgressDialog_setAutoReset(XProgressDialog* self, bool reset);
/**
 * @brief      查询是否自动复位（对标 QProgressDialog::autoReset）。
 * @param      self 目标对话框；可为 NULL。
 * @return     自动复位返回 true；无效返回 true（Qt 默认）。
 */
bool XProgressDialog_autoReset(const XProgressDialog* self);
/**
 * @brief      设置是否在复位时自动隐藏（对标 QProgressDialog::setAutoClose）。
 * @param      self 目标对话框。
 * @param      close true=自动隐藏（默认）。
 * @return     无返回值。
 */
void XProgressDialog_setAutoClose(XProgressDialog* self, bool close);
/**
 * @brief      查询是否自动隐藏（对标 QProgressDialog::autoClose）。
 * @param      self 目标对话框；可为 NULL。
 * @return     自动隐藏返回 true；无效返回 true（Qt 默认）。
 */
bool XProgressDialog_autoClose(const XProgressDialog* self);
/**
 * @brief      设置显示最小时长（对标 QProgressDialog::setMinimumDuration）。
 * @param      self 目标对话框。
 * @param      ms 毫秒数；默认 4000。
 * @return     无返回值。
 */
void XProgressDialog_setMinimumDuration(XProgressDialog* self, int ms);
/**
 * @brief      获取显示最小时长（对标 QProgressDialog::minimumDuration）。
 * @param      self 目标对话框；可为 NULL。
 * @return     毫秒数；无效返回 4000。
 */
int XProgressDialog_minimumDuration(const XProgressDialog* self);
/**
 * @brief      强制立即显示对话框（对标 QProgressDialog::forceShow；
 *             show() 且隐藏状态位清除）。
 * @param      self 目标对话框。
 * @return     无返回值。
 */
void XProgressDialog_forceShow(XProgressDialog* self);

/* ==================== 信号（对标 QProgressDialog） ==================== */

/**
 * @brief      取消信号（对标 QProgressDialog::canceled；空参；
 *             cancel() 触发时发射，可手动触发供测试）。
 * @param      self 目标对话框。
 * @return     信号标识。
 */
void* XProgressDialog_canceled_signal(XProgressDialog* self);

#endif /* XWIDGET_ON && XDIALOG_ON */

#endif /* XPROGRESSDIALOG_H */
