/**
 * @file       HsfDataMenu.h
 * @brief      数据子菜单页：历史数据 / 日志 / 报警。
 */
#ifndef HSFDATAMENU_H
#define HSFDATAMENU_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#include "XWidget.h"
#include "HsfPages.h"

#if XWIDGET_ON && XPUSHBUTTON_ON && XLABEL_ON

/**
 * @brief      创建数据子菜单页（堆分配，挂 parent，随主窗级联释放）。
 * @param parent 父控件（堆栈宿主）。
 * @param navFn 导航回调；可 NULL。
 * @param userData 透传回调的用户数据。
 * @return 页根控件借用指针；失败 NULL。
 */
XWidget* HsfDataMenu_create(XWidget* parent, HsfMenuNavFn navFn,
                            void* userData);

#endif /* XWIDGET_ON && XPUSHBUTTON_ON && XLABEL_ON */

#ifdef __cplusplus
}
#endif

#endif /* HSFDATAMENU_H */
