/******************************************************************************
 * @file       XMainWindow_Protected.h
 * @brief      XMainWindow 主窗口保护接口（仅供 XDockWidget 与
 *             XMainWindow 内部实现使用）。
 * @details    集中声明停靠面板向宿主主窗口回触重排的内部接口；
 *             普通用户代码不应直接包含或调用（对标 Qt 中
 *             QDockWidget 触发 QMainWindowLayout::update 的私有路径）。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XMAINWINDOW_PROTECTED_H
#define XMAINWINDOW_PROTECTED_H
#ifdef __cplusplus
extern "C" {
#endif

#include "XMainWindow.h"

#if XWIDGET_ON && XMAINWINDOW_ON

/**
 * @brief      请求主窗口按当前停靠登记重排布局（内部接口）。
 * @details    停靠面板浮动/回归停靠、显隐变化时由 XDockWidget 经宿主
 *             回链调用；重复调用安全（重排幂等）。
 * @param      self 目标主窗口；可为 NULL，NULL 时不执行操作。
 * @return     无返回值。
 */
void XMainWindow_updateDockLayout(XMainWindow* self);

#endif /* XWIDGET_ON && XMAINWINDOW_ON */

#ifdef __cplusplus
}
#endif
#endif /* XMAINWINDOW_PROTECTED_H */
