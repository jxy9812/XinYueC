/**
 * @file       XKeySequenceEdit.h
 * @brief      XKeySequenceEdit 快捷键捕获控件（对标 Qt 6.8
 *             QKeySequenceEdit 核心公共 API）。
 * @details    功能范围：
 *             - 键盘捕获：控件获得焦点后，用户按下的修饰键+非修饰键
 *               组合被记录为快捷键序列（最多 4 组，对标默认值）；
 *             - 捕获态机（对标 Qt 6.8 录制语义）：捕获发起于获得焦点，
 *               退出捕获 = Esc（清空且不录 Esc）/ 失去焦点（弹窗焦点
 *               豁免）/ 空闲超时（5s 无输入，防捕获态滞留键盘陷阱）；
 *               非捕获态不消费任何按键（Tab 走链、其余上抛）；
 *             - keySequence/setKeySequence/clear；
 *             - maximumSequenceLength/setMaximumSequenceLength；
 *             - finishingKey：Return/Enter 确认序列，Esc 清空并结束
 *               捕获，Backspace 删除最后一组（对标 Qt 行为）；
 *             - 信号：keySequenceChanged(XKeySequence*)/
 *               editingFinished()；
 *             - 绘制：当前序列文本居中显示（如 "Ctrl+S"）；聚焦态绘
 *               虚线焦点框（捕获中高亮色/非捕获灰），捕获中且空序列
 *               时绘 "Press shortcut" 占位（对标 Qt placeholderText）；
 *             - XKeySequence 结构：{modifiers, key} 数组 + 计数，
 *               对标 QKeySequence 的 MultiKey 语义。
 * @note       模块总开关 XKEYSEQUENCEEDIT_ON 定义于 XGuiConfig.h。
 * @author     XinYueC 团队
 */
#ifndef XKEYSEQUENCEEDIT_H
#define XKEYSEQUENCEEDIT_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XGuiConfig.h"
#include "XWidget.h"

#if XWIDGET_ON && XKEYSEQUENCEEDIT_ON

/** @brief 快捷键序列最大长度（对标 QKeySequenceEdit 默认值 4）。 */
#define XKEYSEQUENCEEDIT_MAX_LENGTH 4
/** @brief 结束键组合最大条目数（对标 Qt 默认 2 项：Tab/Backtab；留余量）。 */
#define XKEYSEQUENCEEDIT_MAX_FINISHING 8

/** @brief 单组快捷键组合（修饰键 + 非修饰键）。 */
typedef struct XKeyCombination
{
    XKeyboardModifiers modifiers; /**< 修饰键位掩码。 */
    int key;                     /**< 非修饰键键码。 */
} XKeyCombination;

/** @brief 快捷键序列（多组组合的有序集合，对标 QKeySequence）。 */
typedef struct XKeySequence
{
    XKeyCombination combos[XKEYSEQUENCEEDIT_MAX_LENGTH];
    int count; /**< 有效组合数（0 = 空）。 */
} XKeySequence;

XCLASS_DEFINE_BEGING(XKeySequenceEdit)
XCLASS_DEFINE_EXTEND_END(XKeySequenceEdit, XWidget)

typedef struct XKeySequenceEdit
{
    XWidget m_base;          /**< 基类成员；必须是第一个。 */
    XKeySequence m_sequence; /**< 当前快捷键序列。 */
    XKeySequence m_oldSequence; /**< 确认前的旧序列。 */
    int m_maxLength;         /**< 序列最大长度（默认 4）。 */
    bool m_clearButton;      /**< 清除按钮开关（默认 false）。 */
    bool m_capturing;        /**< 捕获态机：true=捕获中（获得焦点发起，
                                  Esc/失焦/空闲超时退出）。 */
    XKeyCombination m_finishing[XKEYSEQUENCEEDIT_MAX_FINISHING]; /**< 结束编辑的键组合
                                  （对标 finishingKeyCombinations；默认 Tab/Backtab）。 */
    int m_finishingCount;    /**< 结束键组合数量（默认 2）。 */
    XTimerId m_idleTimer;    /**< 捕获空闲超时定时器（XTIMER_INVALID_ID=未启动；
                                  捕获期间 X 毫秒无输入自动结束捕获，防键盘陷阱）。 */
} XKeySequenceEdit;

/** @brief XKeySequenceEditclassinit（对标 Qt 同名接口）。
 * @return 返回对象指针；无效时返回 NULL。
 */
XVtable* XKeySequenceEdit_class_init(void);
void XKeySequenceEdit_init(XKeySequenceEdit* self, XWidget* parent,
                           XWidgetFlags flags);
#define XKeySequenceEdit_create(parent, flags) XKeySequenceEdit_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))
/** @brief XKeySequenceEditcreateex（对标 Qt 同名接口）。
 * @param memory XMemoryType 参数。
 * @param parent 父控件指针；可为 NULL。
 * @param flags 窗口标志位组合。
 * @return 返回对象指针；无效时返回 NULL。
 */
XKeySequenceEdit* XKeySequenceEdit_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags);
#define XKeySequenceEdit_deinit_base(self) XWidget_deinit_base((XWidget*)(self))
#define XKeySequenceEdit_delete_base(self) XClass_delete_base((XClass*)(self))

/**
 * @brief      获取当前快捷键序列（对标 keySequence）。
 */
const XKeySequence* XKeySequenceEdit_keySequence(const XKeySequenceEdit* self);
/**
 * @brief      设置快捷键序列（对标 setKeySequence）。
 */
void XKeySequenceEdit_setKeySequence(XKeySequenceEdit* self,
                                     const XKeySequence* sequence);
/**
 * @brief      清空内容（对标 Qt 同名槽）。
 */
void XKeySequenceEdit_clear(XKeySequenceEdit* self);
/**
 * @brief      获取最大序列长度。
 */
int XKeySequenceEdit_maximumSequenceLength(const XKeySequenceEdit* self);
/**
 * @brief      设置最大序列长度。
 */
void XKeySequenceEdit_setMaximumSequenceLength(XKeySequenceEdit* self, int count);
/**
 * @brief      获取清除按钮开关。
 */
bool XKeySequenceEdit_isClearButtonEnabled(const XKeySequenceEdit* self);
/**
 * @brief      设置清除按钮开关。
 */
void XKeySequenceEdit_setClearButtonEnabled(XKeySequenceEdit* self, bool enable);

/* ==================== 捕获态控制（对标 Qt 6.7+ capturing API） ============ */

/**
 * @brief      程序化进入捕获态（对标 startCapturing）。
 *
 * @details    捕获通常由获得焦点自动发起（对标 Qt「recording is initiated
 *             when the widget receives the focus」）；本接口供 Esc 退出/
 *             空闲超时后重新武装捕获（等价用户再次点击控件）。
 */
void XKeySequenceEdit_startCapturing(XKeySequenceEdit* self);

/**
 * @brief      结束捕获并提交（对标 stopCapturing）：有序列时发射
 *             editingFinished，随后退出捕获态。
 */
void XKeySequenceEdit_stopCapturing(XKeySequenceEdit* self);

/**
 * @brief      取消捕获并清空序列（对标 cancelCapturing；Esc 按键路径
 *             同款语义：清空且不录 Esc）。
 */
void XKeySequenceEdit_cancelCapturing(XKeySequenceEdit* self);

/**
 * @brief      查询捕获态（对标 isRecording）。
 * @return     true=捕获中；self 为 NULL 时返回 false。
 */
bool XKeySequenceEdit_isCapturing(const XKeySequenceEdit* self);

/* ==================== 结束键组合（对标 Qt 6.8 finishingKeyCombinations） ==== */

/**
 * @brief      获取结束键组合数量（对标 finishingKeyCombinations().size()）。
 *
 * @param      self 目标控件；可为 NULL。
 * @return     组合数量；self 为 NULL 时返回 0。
 */
int XKeySequenceEdit_finishingKeyCombinationCount(
    const XKeySequenceEdit* self);

/**
 * @brief      获取结束键组合数组（对标 finishingKeyCombinations）。
 *
 * @details    XGui 无 QList：返回内部借用数组，元素个数由
 *             XKeySequenceEdit_finishingKeyCombinationCount 给出；默认
 *             为 Qt 的 {Tab, Backtab}（无修饰键）。任何命中该列表的组合
 *             按下时结束编辑并发射 editingFinished()。
 *
 * @param      self 目标控件；可为 NULL。
 * @return     借用只读数组指针；self 为 NULL 时返回 NULL。不得释放。
 */
const XKeyCombination* XKeySequenceEdit_finishingKeyCombinations(
    const XKeySequenceEdit* self);

/**
 * @brief      设置结束键组合（对标 setFinishingKeyCombinations）。
 *
 * @details    XGui 无 QList<QKeyCombination>：以 (数组, 数量) 承载，超出
 *             XKEYSEQUENCEEDIT_MAX_FINISHING 的条目被截断；count<=0 或
 *             combos 为 NULL 时清空（此后仅 Return/Enter 结束编辑）。
 *
 * @param      self 目标控件。
 * @param      combos 组合数组（借用读取，不接管所有权）；可为 NULL。
 * @param      count 组合数量。
 * @return     无返回值。
 */
void XKeySequenceEdit_setFinishingKeyCombinations(
    XKeySequenceEdit* self, const XKeyCombination* combos, int count);

/* ==================== 信号 ==================== */

/**
 * @brief      快捷键序列变化信号（真发射）。
 */
void* XKeySequenceEdit_keySequenceChanged_signal(XKeySequenceEdit* self,
                                                 const XKeySequence* sequence);
/**
 * @brief      编辑完成信号（真发射）。
 */
void* XKeySequenceEdit_editingFinished_signal(XKeySequenceEdit* self);

#endif /* XWIDGET_ON && XKEYSEQUENCEEDIT_ON */

#endif /* XKEYSEQUENCEEDIT_H */