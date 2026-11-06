/**
 * @file       HsfDataAlarm.h
 * @brief      报警叶子页（占位）：说明文字 + 返回钮。
 * @details    页面骨架先以 HsfPlaceHolder 占位，真实内容后续在本模块内替换。
 */
#ifndef HSFDATAALARM_H
#define HSFDATAALARM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#include "XWidget.h"
#include "HsfPages.h"

#if XWIDGET_ON && XPUSHBUTTON_ON && XLABEL_ON

/**
 * @brief      创建报警叶子页（堆分配，挂 parent，随主窗级联释放）。
 * @param parent 父控件（堆栈宿主）。
 * @param navFn 导航回调（返回钮用）；可 NULL。
 * @param userData 透传回调的用户数据。
 * @return 页根控件借用指针；失败 NULL。
 */
XWidget* HsfDataAlarm_create(XWidget* parent, HsfMenuNavFn navFn,
                           void* userData);

/**
 * @brief      窄宽/竖屏适配（帧泵与切页路径调用；幂等）。
 * @param page 页根（HsfDataAlarm_create 返回值）。
 * @param compact true=窄档（收边距）。
 */
void HsfDataAlarm_adapt(XWidget* page, bool compact);

#endif /* XWIDGET_ON && XPUSHBUTTON_ON && XLABEL_ON */

#ifdef __cplusplus
}
#endif

#endif /* HSFDATAALARM_H */
