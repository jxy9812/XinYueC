/******************************************************************************
 * @file       XDockWidget_Protected.h
 * @brief      XDockWidget 停靠面板保护接口（仅供 XMainWindow 与
 *             XDockWidget 内部实现使用）。
 * @details    集中声明停靠面板与主窗口布局系统之间的内部回链接口；
 *             普通用户代码不应直接包含或调用（对标 Qt 中
 *             QDockWidget 与 QMainWindowLayout 的私有协作边界）。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XDOCKWIDGET_PROTECTED_H
#define XDOCKWIDGET_PROTECTED_H
#ifdef __cplusplus
extern "C" {
#endif

#include "XDockWidget.h"

#if XWIDGET_ON && XDOCKWIDGET_ON

/**
 * @brief      登记面板的宿主主窗口（内部接口；对标 Qt 中
 *             QDockWidget 回链 QMainWindowLayout 的私有指针）。
 * @details    由 XMainWindow_addDockWidget 在登记时调用、
 *             XMainWindow_removeDockWidget 在摘除时传入 NULL；
 *             浮动/显隐变化经该回链通知宿主重排停靠几何。
 * @param      self 目标停靠面板；可为 NULL，NULL 时不执行操作。
 * @param      host 宿主主窗口借用指针；可为 NULL 表示摘除回链。
 * @return     无返回值。
 */
void XDockWidget_setHost(XDockWidget* self, XWidget* host);

/**
 * @brief      查询面板的宿主主窗口（内部接口）。
 * @param      self 目标停靠面板；可为 NULL。
 * @return     宿主主窗口借用指针；未登记或参数无效返回 NULL。
 */
XWidget* XDockWidget_host(const XDockWidget* self);

#endif /* XWIDGET_ON && XDOCKWIDGET_ON */

#ifdef __cplusplus
}
#endif
#endif /* XDOCKWIDGET_PROTECTED_H */
