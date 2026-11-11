/******************************************************************************
 * @file       XProgressDialog.h
 * @brief      XProgressDialog 进度对话框控件（对标 Qt 6.8 QProgressDialog
 *            : QDialog）。
 * @details    继承 XDialog，提供带取消能力的进度对话框公共 API 面：
 *             - 范围/值：setRange/setMinimum/setMaximum/minimum/maximum/
 *               setValue/value（**值状态与 Qt 的 QProgressBar 同口径**：
 *               缺省/复位后为 `minimum-1` 的"忙态"哨兵，本机 Qt 6.8.3
 *               实测为 -1；setRange(min>max) 把 max 钳到 min；越界入参
 *               不改变当前值；值达 maximum 且 autoReset 时复位）；
 *             - 文本：setLabelText/labelText、setCancelButtonText、
 *               setBar（**接管所有权**，析构/替换时删除；对标 Qt）；
 *             - 行为：setAutoReset/autoReset（默认 true）、
 *               setAutoClose/autoClose（默认 true）、
 *               setMinimumDuration/minimumDuration（默认 4000ms）、
 *               cancel()/wasCanceled、forceShow；
 *             - 打开：open_2（对标经 using QDialog::open 再导出的
 *               open(receiver,member)：窗口模态非阻塞显示 + 关闭自动
 *               断开；无参 open 由基类 XDialog_open 承接）；
 *             - 信号：canceled()（空参；cancel() 触发时发射）。
 * @note       模块总开关 XDIALOG_ON（XWIDGET_ON && XDIALOG_ON 有效）。
 * @note       本实现不创建真实的子控件布局；setLabel/setCancelButton/
 *             setBar 建立的对象由对话框接管并参与尺寸/重排模型。
 *             项目有意偏差：cancel() 直接发射 canceled()（Qt 的
 *             cancel() 不发信号，是"canceled()→cancel()"内部连接；本库
 *             按既有公开契约保留 cancel() 发射，见 .c 注与 apitest 锁）。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XPROGRESSDIALOG_H
#define XPROGRESSDIALOG_H

#if XGUI_ON
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

/** @brief XProgressBar 前向声明（setBar 所有权转移；完整定义见 XProgressBar.h）。 */
typedef struct XProgressBar XProgressBar;
/** @brief XLabel 前向声明（setLabel 所有权转移；完整定义见 XLabel.h）。 */
typedef struct XLabel XLabel;
/** @brief XPushButton 前向声明（setCancelButton 所有权转移；完整定义见
 *         XPushButton.h）。 */
typedef struct XPushButton XPushButton;

/* Qt 的 QProgressDialog 公开 sizeHint()（qprogressdialog.h:47 的
 * `QSize sizeHint() const override`）；本库经 EXWidget_SizeHint 虚槽
 * 实现，公开面沿用 XProgressBar_sizeHint 同款宏（查询即按内容重算）。 */
#define XProgressDialog_sizeHint(self) \
    XWidget_sizeHint((const XWidget*)(self))

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
    /* 指针组（8B 对齐）。 */
    XString* m_labelText;        /**< 提示文本（拥有）。 */
    XString* m_cancelButtonText; /**< 取消按钮文本（拥有）。 */
    XProgressBar* m_bar;         /**< 进度条（**对象拥有**，对标
                                      QProgressDialog::setBar 接管所有权：
                                      替换/析构时删除；NULL=未设置）。 */
    XLabel* m_label;             /**< 自定义标签控件（拥有；对标 setLabel）。 */
    XPushButton* m_cancelButton; /**< 自定义取消按钮（拥有；对标 setCancelButton）。 */
    XObject* m_openReceiver;     /**< open_2 记录的接收对象（关闭时自动断开；
                                     对标 QDialog::open 的
                                     receiverToDisconnectOnClose）。 */
    XSlotFunc1 m_openMember;     /**< open_2 记录的槽函数。 */
    /* [内存治理 2026-10-11] 进度值/时长均为数值域 int（setRange/setValue/
       setMinimumDuration 往返任意值），不窄化。 */
    int m_minimum;               /**< 最小值；默认 0。 */
    int m_maximum;               /**< 最大值；默认 100。 */
    int m_value;                 /**< 当前值；缺省/复位后为 minimum-1
                                      （忙态哨兵，本机 Qt 6.8.3 实测构造、
                                      reset、autoReset 达峰后均 -1，对标
                                      QProgressBar::reset 置 minimum-1）。 */
    int m_minimumDuration;       /**< 显示最小时长（ms）；默认 4000。
                                      对标 Qt 的 int 属性：任意 int 原样
                                      往返（本机 6.8.3 实测 100000→100000、
                                      -5→-5），旧实现收 uint16_t 会把
                                      >65535 的值截断。 */
    XTimerId m_forceTimer;       /**< 最小时长自动显示定时器（对标
                                      QProgressDialogPrivate::forceTimer）；
                                      XTIMER_INVALID_ID＝未起。 */
    uint32_t m_wasCanceled : 1;  /**< 是否已取消（对标 wasCanceled）。 */
    uint32_t m_autoReset : 1;    /**< 达最大值自动复位；默认 true。 */
    uint32_t m_autoClose : 1;    /**< 达最大值/复位自动隐藏；默认 true。 */
    uint32_t m_shownOnce : 1;    /**< 已显示过（对标 QProgressDialogPrivate
                                      ::shownOnce）：超时/强显只生效一次，
                                      reset 后可再强显。 */
    uint32_t m_durationArmed : 1;/**< 首拍计时已起（对标 Qt 的
                                      setValueCalled）：仅首个有效
                                      setValue 起计时。 */
    uint32_t m_progressDialogFlagsUnused : 27; /**< 保留位。 */
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

/* ==================== 范围与值（对标 QProgressDialog） ==================== */

/**
 * @brief      设置取值范围（对标 QProgressDialog::setRange，经
 *             QProgressBar::setRange：**max 小于 min 时把 max 钳到
 *             min**，不是交换）。
 * @details    Qt 6.8 原文（qprogressbar.cpp:315-327）：
 *             `maximum = qMax(minimum, maximum)`；当前值落到
 *             `[minimum-1, maximum]` 之外时按 reset() 置为 `minimum-1`。
 *             本机实测 setRange(50,20) → min=max=50、value=49。
 * @param      self 目标对话框。
 * @param      minimum 最小值。
 * @param      maximum 最大值（小于 minimum 时被钳到 minimum）。
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
 * @brief      设置当前值（对标 QProgressDialog::setValue，经
 *             QProgressBar::setValue）。
 * @details    Qt 6.8 语义（qprogressdialog.cpp:613-664 +
 *             qprogressbar.cpp:277-294）：
 *             ① 已开始计时且新值等于当前值 → 直接返回；
 *             ② **越界值不改变当前值**（Qt 文档："attempting to change
 *                the current value to one outside the range has no
 *                effect"；本机实测 setValue(150)/setValue(-5) 保持原值，
 *                不是钳位）；
 *             ③ 入参（原始值）等于 maximum 且 autoReset 为 true 时执行
 *                reset()（复位到 minimum-1、清 wasCanceled、autoClose
 *                时隐藏）。
 * @param      self 目标对话框。
 * @param      progress 新值。
 * @return     无返回值。
 */
void XProgressDialog_setValue(XProgressDialog* self, int progress);
/**
 * @brief      获取当前值（对标 QProgressDialog::value = QProgressBar::value）。
 * @param      self 目标对话框；可为 NULL。
 * @return     当前值；无效返回 0。缺省/复位后为 minimum-1（忙态哨兵，
 *             缺省范围下即 -1）。
 */
int XProgressDialog_value(const XProgressDialog* self);
/**
 * @brief      复位进度（对标 QProgressDialog::reset）。
 * @details    值复位到 `minimum-1`（忙态哨兵；对标 QProgressBar::reset
 *             置 minimum-1，缺省范围下为 -1，本机实测）、wasCanceled
 *             置 false；autoClose 为 true 时隐藏对话框。
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
 * @return     新建的 XString 拷贝，调用方拥有，须 XClassDelete；
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
 * @brief      设置进度条（对标 QProgressDialog::setBar）。
 * @details    Qt 语义（qprogressdialog.cpp:425-444）：**对话框接管 bar
 *             所有权**——旧进度条被删除、新进度条成为对话框子控件并
 *             参与尺寸/布局，析构时随对话框删除；传入 NULL 为**无操作**
 *             （Qt 打印 "Cannot set a null progress bar" 后直接返回，
 *             不清除已有进度条）。设置后对话框的范围/值状态会同步到
 *             进度条。
 * @warning    不得传入栈上对象或调用方仍持有的对象（Qt 文档："do not
 *             use a progress bar allocated on the stack"）；所有权转移后
 *             由对话框释放。
 * @param      self 目标对话框。
 * @param      bar 进度条指针（所有权转移给对话框）；NULL 为无操作。
 * @return     无返回值。
 */
void XProgressDialog_setBar(XProgressDialog* self, XProgressBar* bar);
/**
 * @brief      请求取消（对标 QProgressDialog::cancel + 取消按钮链路）。
 * @details    置 wasCanceled 为 true、复位进度（值到 `minimum-1`）、
 *             **无条件隐藏**（对标 Qt cancel() 的 forceHide：即使
 *             autoClose=false 也隐藏）。
 * @note       项目有意偏差：本函数**发射** canceled()（Qt 的 cancel()
 *             不发信号，而是构造时 `connect(canceled, SLOT(cancel()))`
 *             反向连接）。保留依据：XProgressDialog.h 的公开契约、
 *             Test/XGuiDemo/xgui_demo_apitest_dialogs.c:1900-1905 与
 *             Test/QtAlign/XProgressDialogAlignTest.c 的既有断言；可观察
 *             效果（点取消按钮 → canceled + wasCanceled + 隐藏）与 Qt 一致。
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
 * @details    int 原样保存、不钳位（本机 Qt 6.8.3 实测 100000→100000、
 *             -5→-5；<=0 时首个 setValue 立即显示，等价 Qt 的 0 间隔
 *             定时器即时超时）。
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

/* ==================== 非阻塞打开（对标 QDialog::open） ==================== */

/**
 * @brief      以窗口模态显示对话框并立即返回，同时把关闭信号连接到
 *             receiver 的槽（对标 QProgressDialog 头文件经
 *             `using QDialog::open` 再导出的 open(QObject *receiver,
 *             const char *member)；open 的重载形态，按数字后缀约定
 *             命名 _2，XMessageBox_open_2 同型）。
 * @details    连接 finished(int)（载荷：结果码 int）；成员槽以 C 函数
 *             指针给定（Qt 的 const char* 槽名元对象反射无 C 对应）。
 *             无参 open 形态由基类 XDialog_open 承接。对话框关闭（关闭
 *             事件被接受收口）时自动断开该连接；cancel() 仅隐藏不发
 *             finished、不断开（对标 Qt：hide/setVisible 不发射
 *             finished，连接仅经关闭收口或下一次 open_2 断开）。
 * @param      self 目标对话框；NULL 或 member 空时不执行任何操作。
 * @param      receiver 槽所属对象；可为 NULL（此时不连接，仅显示）。
 * @param      member 槽函数（签名 void (*)(XObject*, XVarList*)）。
 * @return     无返回值。
 */
void XProgressDialog_open_2(XProgressDialog* self, XObject* receiver,
                            XSlotFunc1 member);

/* ==================== 信号（对标 QProgressDialog） ==================== */

/**
 * @brief      取消信号（对标 QProgressDialog::canceled；空参；
 *             cancel() 触发时发射，可手动触发供测试）。
 * @param      self 目标对话框。
 * @return     信号标识。
 */
void* XProgressDialog_canceled_signal(XProgressDialog* self);

#endif /* XWIDGET_ON && XDIALOG_ON */

#endif /* XGUI_ON */

#endif /* XPROGRESSDIALOG_H */
