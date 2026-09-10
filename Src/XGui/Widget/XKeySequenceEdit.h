/**
 * @file       XKeySequenceEdit.h
 * @brief      XKeySequenceEdit 快捷键捕获控件（对标 Qt 6.8
 *             QKeySequenceEdit 核心公共 API）。
 * @details    功能范围：
 *             - 键盘捕获：控件获得焦点后，用户按下的修饰键+非修饰键
 *               组合被记录为快捷键序列（最多 4 组，对标默认值）；
 *             - keySequence/setKeySequence/clear；
 *             - maximumSequenceLength/setMaximumSequenceLength；
 *             - finishingKey：Return/Enter 确认序列，Esc 清空，
 *               Backspace 删除最后一组（对标 Qt 行为）；
 *             - 信号：keySequenceChanged(XKeySequence*)/
 *               editingFinished()；
 *             - 绘制：当前序列文本居中显示（如 "Ctrl+S"）；
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
    bool m_capturing;        /**< 正在捕获（有部分输入）。 */
} XKeySequenceEdit;

XVtable* XKeySequenceEdit_class_init(void);
void XKeySequenceEdit_init(XKeySequenceEdit* self, XWidget* parent,
                           XWidgetFlags flags);
#define XKeySequenceEdit_create(parent, flags) XKeySequenceEdit_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))
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

#ifdef __cplusplus
}
#endif
#endif /* XKEYSEQUENCEEDIT_H */