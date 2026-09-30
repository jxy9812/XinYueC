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

/**
 * @brief      拖动落点预览（对标 QMainWindowLayout::hover 的拖动中
 *             衔接点，qdockwidget.cpp:1051-1052）。
 * @details    供 XDockWidget 拖动链调用：按全局落点更新宿主主窗口的
 *             落点指示器（橡皮筋矩形，覆盖目标停靠分区）。落点在宿
 *             主客户区内且目标区域被 allowedAreas 允许时显示/移动指
 *             示器并返回目标区域码；落点在宿主中央带时返回
 *             XDockWidgetArea_All（调用方落地为 tab 化）；落点在宿
 *             主之外或区域不被允许时隐藏指示器并返回
 *             XDockWidgetArea_NoDockWidgetArea。重复调用安全（幂等）。
 * @param      self 目标主窗口；可为 NULL，NULL 时不执行操作。
 * @param      dock 拖动中的停靠面板（借用；用于 allowedAreas 过滤）。
 * @param      globalPos 拖动点全局坐标（应用全局坐标，非局部）。
 * @return     目标区域码：Left/Right/Top/Bottom、All=中央带（tab 化）、
 *             NoDockWidgetArea=无落点（保持浮动）。
 */
int XMainWindow_hoverDrop(XMainWindow* self, XWidget* dock,
                          const XPoint* globalPos);

/**
 * @brief      结束拖放：按当前落点把面板落位并隐藏指示器（内部接口，
 *             对标 QMainWindowLayout::plug）。
 * @details    落位含两种：分区落点按 addDockWidget 语义移动/回归（内
 *             部完成脱离浮动态）；中央带（hoverDrop 返回 All）按
 *             tabifyDockWidget 语义与现有可见面板编组（无可编组面板
 *             时回退分区落位）。无有效落点时不动作并返回
 *             XDockWidgetArea_NoDockWidgetArea（面板保持浮动态，由调
 *             用方收尾）。重复调用安全（落位幂等）。
 * @param      self 目标主窗口；可为 NULL，NULL 时不执行操作。
 * @param      dock 拖动中的停靠面板（借用）。
 * @param      globalPos 释放点全局坐标。
 * @return     实际落位区域码；未落位返回
 *             XDockWidgetArea_NoDockWidgetArea。
 */
int XMainWindow_finishDrop(XMainWindow* self, XWidget* dock,
                           const XPoint* globalPos);

#endif /* XWIDGET_ON && XMAINWINDOW_ON */

#ifdef __cplusplus
}
#endif
#endif /* XMAINWINDOW_PROTECTED_H */
