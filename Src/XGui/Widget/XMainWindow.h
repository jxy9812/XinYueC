/******************************************************************************
 * @file       XMainWindow.h
 * @brief      XMainWindow 主窗口控件（对标 Qt 6.8 QMainWindow 核心
 *             公共 API）。
 * @details    功能范围：
 *             - menuBar()/setMenuBar()（惰性创建内置 XMenuBar）；
 *             - statusBar()/setStatusBar()（惰性创建内置 XStatusBar）；
 *             - addToolBar(area, toolbar)/addToolBar_2(title)（创建）；
 *             - setCentralWidget/centralWidget/takeCentralWidget；
 *             - addDockWidget(area, dock)/removeDockWidget；
 *             - setDockOptions/dockOptions（存储）；
 *             - 布局：菜单栏(顶) → 工具栏区 → 中央控件 → 停靠区 →
 *               状态栏(底)，resizeEvent 触发重排。
 * @note       模块总开关 XMAINWINDOW_ON 定义于 XGuiConfig.h。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XMAINWINDOW_H
#define XMAINWINDOW_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XGuiConfig.h"
#include "XWidget.h"
#if XDOCKWIDGET_ON
#include "XDockWidget.h"
#endif

#if XWIDGET_ON && XMAINWINDOW_ON

/** @brief 主窗口停靠选项（对标 QMainWindow::DockOption，数值一致）。 */
typedef enum XMainWindowDockOption
{
    XMainWindowDockOption_AnimatedDocks = 0x01,
    XMainWindowDockOption_AllowNestedDocks = 0x02,
    XMainWindowDockOption_AllowTabbedDocks = 0x04,
    XMainWindowDockOption_ForceTabbedDocks = 0x08,
    XMainWindowDockOption_VerticalTabs = 0x10,
    XMainWindowDockOption_GroupedDragging = 0x20
} XMainWindowDockOption;

XCLASS_DEFINE_BEGING(XMainWindow)
XCLASS_DEFINE_EXTEND_END(XMainWindow, XWidget)

typedef struct XMainWindow
{
    XWidget m_base;              /**< 基类成员；必须是第一个。 */
    XWidget* m_menuBar;          /**< 菜单栏（XMenuBar*，拥有或外部）。 */
    bool m_menuBarOwned;         /**< 菜单栏是否内部创建。 */
    XWidget* m_statusBar;        /**< 状态栏（XStatusBar*，拥有或外部）。 */
    bool m_statusBarOwned;       /**< 状态栏是否内部创建。 */
    XWidget* m_central;          /**< 中央控件（借用）。 */
    XVector* m_toolBars;         /**< 工具栏数组（XToolBar*，借用）。 */
    XVector* m_toolBarAreas;     /**< 工具栏停靠区域（int）。 */
    XVector* m_docks;            /**< 停靠面板数组（XDockWidget*，借用）。 */
    XVector* m_dockAreas;        /**< 停靠面板区域（int）。 */
    int m_dockOptions;           /**< 停靠选项。 */
    int m_iconSize;              /**< 工具栏图标尺寸。 */
} XMainWindow;

XVtable* XMainWindow_class_init(void);
void XMainWindow_init(XMainWindow* self, XWidget* parent,
                      XWidgetFlags flags);
#define XMainWindow_create(parent, flags) XMainWindow_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))
XMainWindow* XMainWindow_create_ex(XMemoryType memory, XWidget* parent,
                                   XWidgetFlags flags);
#define XMainWindow_deinit_base(self) XWidget_deinit_base((XWidget*)(self))
#define XMainWindow_delete_base(self) XClass_delete_base((XClass*)(self))

/* ==================== 菜单栏与状态栏 ==================== */

/** @brief 返回内置菜单栏（不存在则惰性创建；对标 menuBar()）。 */
XWidget* XMainWindow_menuBar(XMainWindow* self);
/** @brief 设置外部菜单栏（对标 setMenuBar）。 */
void XMainWindow_setMenuBar(XMainWindow* self, XWidget* menuBar);
/** @brief 返回内置状态栏（不存在则惰性创建；对标 statusBar()）。 */
XWidget* XMainWindow_statusBar(XMainWindow* self);
/** @brief 设置外部状态栏（对标 setStatusBar）。 */
void XMainWindow_setStatusBar(XMainWindow* self, XWidget* statusBar);

/* ==================== 中央控件与工具栏 ==================== */

void XMainWindow_setCentralWidget(XMainWindow* self, XWidget* widget);
XWidget* XMainWindow_centralWidget(const XMainWindow* self);
XWidget* XMainWindow_takeCentralWidget(XMainWindow* self);
void XMainWindow_addToolBar(XMainWindow* self, int area, XWidget* toolbar);
XWidget* XMainWindow_addToolBar_2(XMainWindow* self, const char* utf8Title);

/* ==================== 停靠面板 ==================== */

void XMainWindow_addDockWidget(XMainWindow* self, int area,
                               XWidget* dock);
void XMainWindow_removeDockWidget(XMainWindow* self, XWidget* dock);
void XMainWindow_setDockOptions(XMainWindow* self, int options);
int XMainWindow_dockOptions(const XMainWindow* self);

#endif /* XWIDGET_ON && XMAINWINDOW_ON */

#ifdef __cplusplus
}
#endif
#endif /* XMAINWINDOW_H */