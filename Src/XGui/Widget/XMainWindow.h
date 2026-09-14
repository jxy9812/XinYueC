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
#include "XToolBar.h"
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
    int m_toolButtonStyle;       /**< 全局工具按钮样式（XToolButtonStyle 取值）。 */
    XWidget* m_activeTabifiedDock; /**< 最近激活的标签化停靠面板（借用）。 */
} XMainWindow;

/** @brief XMainWindowclassinit（对标 Qt 同名接口）。
 * @return 返回对象指针；无效时返回 NULL。
 */
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
/** @brief XMainWindowcentral控件（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 返回对象指针；无效时返回 NULL。
 */
XWidget* XMainWindow_centralWidget(const XMainWindow* self);
/** @brief XMainWindowtakeCentral控件（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 返回对象指针；无效时返回 NULL。
 */
XWidget* XMainWindow_takeCentralWidget(XMainWindow* self);
/** @brief XMainWindowadd工具条（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param area 区域枚举。
 * @param toolbar 工具栏指针。
 * @return 无返回值。
 */
void XMainWindow_addToolBar(XMainWindow* self, int area, XWidget* toolbar);
/** @brief XMainWindowadd工具条2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param utf8Title const char 参数。
 * @return 返回对象指针；无效时返回 NULL。
 */
XWidget* XMainWindow_addToolBar_2(XMainWindow* self, const char* utf8Title);

/* ==================== 停靠面板 ==================== */

void XMainWindow_addDockWidget(XMainWindow* self, int area,
                               XWidget* dock);
/** @brief XMainWindowremove停靠控件（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param dock 停靠窗指针。
 * @return 无返回值。
 */
void XMainWindow_removeDockWidget(XMainWindow* self, XWidget* dock);
/** @brief XMainWindowset停靠Options（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param options int 参数。
 * @return 无返回值。
 */
void XMainWindow_setDockOptions(XMainWindow* self, int options);
/** @brief XMainWindowdockOptions（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 返回对应数值；无效时返回 0 或 -1（视接口语义）。
 */
int XMainWindow_dockOptions(const XMainWindow* self);

#endif /* XWIDGET_ON && XMAINWINDOW_ON */

#ifdef __cplusplus
}
#endif
/** @brief XMainWindowtool条区域（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param toolbar 工具栏指针。
 * @return 返回对应数值；无效时返回 0 或 -1（视接口语义）。
 */
int XMainWindow_toolBarArea(const XMainWindow* self, XToolBar* toolbar);
/** @brief XMainWindowdock控件区域（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param dock 停靠窗指针。
 * @return 返回对应数值；无效时返回 0 或 -1（视接口语义）。
 */
int XMainWindow_dockWidgetArea(const XMainWindow* self, XDockWidget* dock);
/** @brief XMainWindowadd工具条Break（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param area 区域枚举。
 * @return 无返回值。
 */
void XMainWindow_addToolBarBreak(XMainWindow* self, int area);
/** @brief XMainWindowset文档模式（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param mode bool 模式开关。
 * @return 无返回值。
 */
void XMainWindow_setDocumentMode(XMainWindow* self, bool mode);
/** @brief XMainWindowdocument模式（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 条件成立返回 true，否则返回 false。
 */
bool XMainWindow_documentMode(const XMainWindow* self);
/** @brief XMainWindowset图标尺寸（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param size 尺寸（像素）。
 * @return 无返回值。
 */
void XMainWindow_setIconSize(XMainWindow* self, int size);
/** @brief XMainWindowicon尺寸（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 返回对应数值；无效时返回 0 或 -1（视接口语义）。
 */
int XMainWindow_iconSize(const XMainWindow* self);
/** @brief XMainWindowset角落（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param corner int 参数。
 * @param area 区域枚举。
 * @return 无返回值。
 */
void XMainWindow_setCorner(XMainWindow* self, int corner, int area);
/** @brief XMainWindowcorner（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param corner int 参数。
 * @return 返回对应数值；无效时返回 0 或 -1（视接口语义）。
 */
int XMainWindow_corner(const XMainWindow* self, int corner);
/** @brief XMainWindowset页签位置（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param area 区域枚举。
 * @param position int 参数。
 * @return 无返回值。
 */
void XMainWindow_setTabPosition(XMainWindow* self, int area, int position);
/** @brief XMainWindowtab位置（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param area 区域枚举。
 * @return 返回对应数值；无效时返回 0 或 -1（视接口语义）。
 */
int XMainWindow_tabPosition(const XMainWindow* self, int area);
/** @brief XMainWindowset页签形状（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param shape 形状枚举。
 * @return 无返回值。
 */
void XMainWindow_setTabShape(XMainWindow* self, int shape);
/** @brief XMainWindowtab形状（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 返回对应数值；无效时返回 0 或 -1（视接口语义）。
 */
int XMainWindow_tabShape(const XMainWindow* self);
/** @brief XMainWindowsetUnified标题And工具条OnMac（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param set bool 参数。
 * @return 无返回值。
 */
void XMainWindow_setUnifiedTitleAndToolBarOnMac(XMainWindow* self, bool set);
/** @brief XMainWindowisUnified标题And工具条OnMac（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 条件成立返回 true，否则返回 false。
 */
bool XMainWindow_isUnifiedTitleAndToolBarOnMac(const XMainWindow* self);
/** @brief XMainWindowsetAnimated（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param enabled bool 开关：true 启用。
 * @return 无返回值。
 */
void XMainWindow_setAnimated(XMainWindow* self, bool enabled);
/** @brief XMainWindowisAnimated（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 条件成立返回 true，否则返回 false。
 */
bool XMainWindow_isAnimated(const XMainWindow* self);
/** @brief XMainWindowset停靠Nesting启用（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param enabled bool 开关：true 启用。
 * @return 无返回值。
 */
void XMainWindow_setDockNestingEnabled(XMainWindow* self, bool enabled);
/** @brief XMainWindowis停靠Nesting启用（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 条件成立返回 true，否则返回 false。
 */
bool XMainWindow_isDockNestingEnabled(const XMainWindow* self);
/** @brief XMainWindowsetSeparator（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param area 区域枚举。
 * @return 无返回值。
 */
void XMainWindow_setSeparator(XMainWindow* self, int area);
/** @brief XMainWindowinsert工具条（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param before 参考位置对象指针。
 * @param toolbar 工具栏指针。
 * @return 无返回值。
 */
void XMainWindow_insertToolBar(XMainWindow* self, XToolBar* before, XToolBar* toolbar);
/** @brief XMainWindowremove工具条（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param toolbar 工具栏指针。
 * @return 无返回值。
 */
void XMainWindow_removeToolBar(XMainWindow* self, XToolBar* toolbar);
/** @brief XMainWindowicon尺寸变更 信号地址（发射经 XObject_emitSignal）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XMainWindow_iconSizeChanged_signal(XMainWindow* self);

/**
 * @brief      发射 toolButtonStyleChanged(Qt::ToolButtonStyle) 信号
 *             （对标 QMainWindow::toolButtonStyleChanged）。
 * @details    setToolButtonStyle 改变全局工具按钮样式时真发射；
 *             self 非 NULL 且有已连接槽时经 XObject_emitSignal 同步
 *             通知，否则只返回信号标识。
 * @param      self 目标主窗口指针；可为 NULL。
 * @param      toolButtonStyle 新的工具按钮样式（XToolButtonStyle 取值）。
 * @return     不透明的 toolButtonStyleChanged 信号标识；返回值不指向
 *             可释放对象，也不得解引用。
 */
void* XMainWindow_toolButtonStyleChanged_signal(XMainWindow* self, int toolButtonStyle);

/**
 * @brief      发射 tabifiedDockWidgetActivated(QDockWidget*) 信号
 *             （对标 QMainWindow::tabifiedDockWidgetActivated）。
 * @details    某停靠面板被激活并带出同组标签化停靠面板时真发射；
 *             本实现中停靠面板暂无标签化分组，信号保留 API 且仅在
 *             显式调用时发射。self 非 NULL 且有已连接槽时经
 *             XObject_emitSignal 同步通知，否则只返回信号标识。
 * @param      self 目标主窗口指针；可为 NULL。
 * @param      dockWidget 被激活的停靠面板；可为 NULL。
 * @return     不透明的 tabifiedDockWidgetActivated 信号标识；返回值
 *             不指向可释放对象，也不得解引用。
 */
void* XMainWindow_tabifiedDockWidgetActivated_signal(XMainWindow* self, XWidget* dockWidget);

/**
 * @brief      设置全局工具按钮样式并发射 toolButtonStyleChanged
 *             （对标 QMainWindow::setToolButtonStyle）。
 * @param      self 目标主窗口指针；NULL 时无操作。
 * @param      toolButtonStyle 工具按钮样式（XToolButtonStyle 取值）。
 * @return     无返回值。
 */
void XMainWindow_setToolButtonStyle(XMainWindow* self, int toolButtonStyle);

/**
 * @brief      读取全局工具按钮样式（对标 QMainWindow::toolButtonStyle）。
 * @param      self 目标主窗口指针；NULL 时返回 IconOnly。
 * @return     工具按钮样式（XToolButtonStyle 取值）。
 */
int XMainWindow_toolButtonStyle(const XMainWindow* self);
/** @brief XMainWindowinsert工具条Break2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XMainWindow_insertToolBarBreak_2(XMainWindow* self);
/** @brief XMainWindowremove工具条Break（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XMainWindow_removeToolBarBreak(XMainWindow* self);
/** @brief XMainWindowisSeparator2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XMainWindow_isSeparator_2(XMainWindow* self);
/** @brief XMainWindowremove工具条2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XMainWindow_removeToolBar_2(XMainWindow* self);
/** @brief XMainWindowsetCentral控件2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XMainWindow_setCentralWidget_2(XMainWindow* self);
/** @brief XMainWindowset菜单条2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XMainWindow_setMenuBar_2(XMainWindow* self);
/** @brief XMainWindowset状态条2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XMainWindow_setStatusBar_2(XMainWindow* self);
/** @brief XMainWindowset停靠Options2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XMainWindow_setDockOptions_2(XMainWindow* self);
/** @brief XMainWindowtakeCentral控件2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XMainWindow_takeCentralWidget_2(XMainWindow* self);
/** @brief XMainWindowcentral控件2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XMainWindow_centralWidget_2(XMainWindow* self);
/** @brief XMainWindowmenu条2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XMainWindow_menuBar_2(XMainWindow* self);
/** @brief XMainWindowstatus条2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XMainWindow_statusBar_2(XMainWindow* self);
/** @brief XMainWindowadd停靠控件2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XMainWindow_addDockWidget_2(XMainWindow* self);
/** @brief XMainWindowremove停靠控件2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XMainWindow_removeDockWidget_2(XMainWindow* self);
/** @brief XMainWindowset停靠控件区域（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XMainWindow_setDockWidgetArea(XMainWindow* self);
/** @brief XMainWindowdockOptions2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XMainWindow_dockOptions_2(XMainWindow* self);
/** @brief XMainWindowadd工具条3（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XMainWindow_addToolBar_3(XMainWindow* self);
/** @brief XMainWindowicon尺寸变更signal2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XMainWindow_iconSizeChanged_signal_2(XMainWindow* self);
#endif /* XMAINWINDOW_H */