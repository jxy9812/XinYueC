/**
 * @file       HsfHomePage.h
 * @brief      主界面页：标题 + 副标题 + 快捷入口 + 状态行。
 * @details    首页是落地第一屏，按「层级清晰 + 横竖屏都舒服」排布：
 *             标题（粗）→ 副标题（弱）→ 快捷入口（横屏一行/竖屏一列，
 *             随形态切换）→ 底部状态行（运行态摘要）。
 *             快捷入口直接跳对应子树（监视/数据/设置），免去逐级点。
 */
#ifndef HSFHOMEPAGE_H
#define HSFHOMEPAGE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#include "XWidget.h"
#include "HsfPages.h"

#if XWIDGET_ON && XPUSHBUTTON_ON && XLABEL_ON

/**
 * @brief      创建主界面页（堆分配，挂 parent，随主窗级联释放）。
 * @param parent 父控件（堆叠宿主）。
 * @param navFn 导航回调（快捷入口用）。
 * @param userData 透传回调的用户数据。
 * @return 页根控件借用指针；失败 NULL。
 */
XWidget* HsfHomePage_create(XWidget* parent, HsfMenuNavFn navFn,
                            void* userData);

/**
 * @brief      形态自适应（横屏一行 / 竖屏一列的快捷入口排布）。
 * @param page 页根控件。
 * @param compact true=紧凑/竖屏形态。
 */
void HsfHomePage_adapt(XWidget* page, bool compact);

#endif /* XWIDGET_ON && XPUSHBUTTON_ON && XLABEL_ON */

#ifdef __cplusplus
}
#endif

#endif /* HSFHOMEPAGE_H */
