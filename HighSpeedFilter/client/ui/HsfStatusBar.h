/**
 * @file       HsfStatusBar.h
 * @brief      底部黑色状态栏：纯黑底白字状态文本（高 26 定高）。
 * @details    XLabel 子类自绘黑底——底色自带（不依赖根背景）：中段内容
 *             控件随窗口缩放可能向下越界，状态栏若只靠根背景着色会被
 *             后画内容盖住；配合 raise 后永可见。
 */
#ifndef HSFSTATUSBAR_H
#define HSFSTATUSBAR_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#include "XWidget.h"
#include "XLabel.h"

#if XWIDGET_ON && XFRAME_ON && XLABEL_ON

/** @brief 状态栏高度（chrome 度量单一事实源：主窗布局/悬浮层锚定共用）。 */
#define HSF_STATUS_BAR_H 26

XCLASS_DEFINE_BEGING(HsfStatusBar)
XCLASS_DEFINE_EXTEND_END(HsfStatusBar, XLabel)

/** @brief 底部黑色状态栏对象；m_base 必须是第一个成员。 */
typedef struct HsfStatusBar
{
    XLabel m_base; /**< XLabel 基类；必须是第一个成员。 */
} HsfStatusBar;

/**
 * @brief      初始化 HsfStatusBar 类虚函数表。
 * @return     共享 XVtable 指针（调用方不得释放）。
 */
XVtable* HsfStatusBar_class_init(void);

/**
 * @brief      初始化黑色状态栏（高 26 定高、白字、左缩进 16）。
 * @param self 待初始化对象；不可为 NULL。
 * @param parent 父控件（通常为主窗根）。
 * @param flags XLabel 创建标志。
 */
void HsfStatusBar_init(HsfStatusBar* self, XWidget* parent,
                       XWidgetFlags flags);

/**
 * @brief      设置状态文本（UTF-8）并触发重绘。
 * @param self 目标状态栏。
 * @param utf8 状态文本（借用指针）。
 */
void HsfStatusBar_setText(HsfStatusBar* self, const char* utf8);

#endif /* XWIDGET_ON && XFRAME_ON && XLABEL_ON */

#ifdef __cplusplus
}
#endif

#endif /* HSFSTATUSBAR_H */
