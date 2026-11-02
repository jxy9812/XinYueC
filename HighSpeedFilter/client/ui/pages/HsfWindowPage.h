/**
 * @file       HsfWindowPage.h
 * @brief      窗口设置页（自 XGuiDemo 移植）：页面实现 + 本框架创建入口。
 * @details    页面主体代码移植自 XGuiWindowDemo 对应页（窗口设置），
 *             经 HsfDemoCompat 兼容层收敛主窗访问器依赖。
 */
#ifndef HSFWINDOWPAGE_H
#define HSFWINDOWPAGE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#include "XWidget.h"
#include "HsfPages.h"

#if XWIDGET_ON && XPUSHBUTTON_ON && XLABEL_ON

/**
 * @brief      创建窗口设置页（堆分配，挂 parent，随主窗级联释放）。
 * @param parent 父控件（堆栈宿主）。
 * @param navFn 导航回调（返回钮用）；可 NULL。
 * @param userData 透传回调的用户数据。
 * @return 页根控件借用指针；失败 NULL。
 */
XWidget* HsfWindowPage_create(XWidget* parent, HsfMenuNavFn navFn,
                               void* userData);

#endif /* XWIDGET_ON && XPUSHBUTTON_ON && XLABEL_ON */

#ifdef __cplusplus
}
#endif

#endif /* HSFWINDOWPAGE_H */
