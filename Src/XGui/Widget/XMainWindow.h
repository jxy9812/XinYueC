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
    bool m_documentMode;       /**< 文档模式（对标 documentMode）。 */
    bool m_animated;           /**< 动画（对标 animated）。 */
    bool m_dockNestingEnabled; /**< 停靠嵌套（对标 dockNestingEnabled）。 */
    bool m_unifiedTitleAndToolBarOnMac; /**< 统一标题栏（属性存储）。 */
    int m_tabPosition;         /**< 页签位置（对标 tabPosition）。 */
    int m_tabShape;            /**< 页签形状（对标 tabShape）。 */
    bool m_separator;          /**< 工具栏分隔线（对标 setSeparator）。 */
    int m_corner;              /**< 角控件位置位集（对标 corner）。 */
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

/** @brief iconSizeChanged(int,int) 信号（对标 QMainWindow::iconSizeChanged；
 *         载荷：宽,高；setIconSize 接线见 Task 2.4）。 */
/* ==================== Task 2.4：QMainWindow 布局 API ==================== */

/** @brief 查询工具栏停靠区域（对标 QMainWindow::toolBarArea）。
 * @param self 目标主窗口。
 * @param toolbar 工具栏借用指针。
 * @return 区域码；未登记返回 0。
 */
int XMainWindow_toolBarArea(const XMainWindow* self, const XWidget* toolbar);
/** @brief 查询停靠面板区域（对标 QMainWindow::dockWidgetArea）。
 * @param self 目标主窗口。
 * @param dock 停靠面板借用指针。
 * @return 区域码；未登记返回 0。
 */
int XMainWindow_dockWidgetArea(const XMainWindow* self,
                               const XWidget* dock);
/** @brief 设置工具栏分隔线（对标 QMainWindow::setSeparator）。
 * @param self 目标主窗口。
 * @param sep true 显示分隔线。
 * @return 无返回值。
 */
void XMainWindow_setSeparator(XMainWindow* self, bool sep);
/** @brief 设置文档模式（对标 setDocumentMode）。
 * @param self 目标主窗口。
 * @param enable true 开启。
 * @return 无返回值。
 */
void XMainWindow_setDocumentMode(XMainWindow* self, bool enable);
/** @brief 查询文档模式。 @param self 目标主窗口。 @return 开启返回 true。 */
bool XMainWindow_documentMode(const XMainWindow* self);
/** @brief 设置停靠动画（对标 setAnimated）。
 * @param self 目标主窗口。
 * @param enable true 开启。
 * @return 无返回值。
 */
void XMainWindow_setAnimated(XMainWindow* self, bool enable);
/** @brief 查询停靠动画。 @param self 目标主窗口。 @return 开启返回 true。 */
bool XMainWindow_isAnimated(const XMainWindow* self);
/** @brief 设置停靠嵌套（对标 setDockNestingEnabled）。
 * @param self 目标主窗口。
 * @param enable true 开启。
 * @return 无返回值。
 */
void XMainWindow_setDockNestingEnabled(XMainWindow* self, bool enable);
/** @brief 查询停靠嵌套。 @param self 目标主窗口。 @return 开启返回 true。 */
bool XMainWindow_isDockNestingEnabled(const XMainWindow* self);
/** @brief 设置统一标题栏（属性存储；对标 setUnifiedTitleAndToolBarOnMac）。
 * @param self 目标主窗口。
 * @param enable true 开启。
 * @return 无返回值。
 */
void XMainWindow_setUnifiedTitleAndToolBarOnMac(XMainWindow* self,
                                                bool enable);
/** @brief 查询统一标题栏。 @param self 目标主窗口。 @return 开启返回 true。 */
bool XMainWindow_isUnifiedTitleAndToolBarOnMac(const XMainWindow* self);
/** @brief 设置页签位置（对标 setTabPosition）。
 * @param self 目标主窗口。
 * @param position 位置码。
 * @return 无返回值。
 */
void XMainWindow_setTabPosition(XMainWindow* self, int position);
/** @brief 查询页签位置。 @param self 目标主窗口。 @return 位置码。 */
int XMainWindow_tabPosition(const XMainWindow* self);
/** @brief 设置页签形状（对标 setTabShape）。
 * @param self 目标主窗口。
 * @param shape 形状码。
 * @return 无返回值。
 */
void XMainWindow_setTabShape(XMainWindow* self, int shape);
/** @brief 查询页签形状。 @param self 目标主窗口。 @return 形状码。 */
int XMainWindow_tabShape(const XMainWindow* self);
/** @brief 设置角控件位置（对标 setCorner）。
 * @param self 目标主窗口。
 * @param corner 角位码（Qt::Corner）。
 * @param area 停靠区域码。
 * @return 无返回值。
 */
void XMainWindow_setCorner(XMainWindow* self, int corner, int area);
/** @brief 查询角控件位置。 @param self 目标主窗口。 @param corner 角位码。 @return 区域码。 */
int XMainWindow_corner(const XMainWindow* self, int corner);
/** @brief 工具栏中断（对标 addToolBarBreak）。
 * @param self 目标主窗口。
 * @return 无返回值。
 */
void XMainWindow_addToolBarBreak(XMainWindow* self);
/** @brief 插入工具栏（对标 insertToolBar）。
 * @param self 目标主窗口。
 * @param before 插入基准工具栏借用指针。
 * @param toolbar 待插入工具栏借用指针。
 * @return 无返回值。
 */
void XMainWindow_insertToolBar(XMainWindow* self, XWidget* before,
                               XWidget* toolbar);
/** @brief 移除工具栏（对标 removeToolBar；不删除对象）。
 * @param self 目标主窗口。
 * @param toolbar 工具栏借用指针。
 * @return 无返回值。
 */
void XMainWindow_removeToolBar(XMainWindow* self, XWidget* toolbar);
/** @brief 保存窗口布局状态（对标 QMainWindow::saveState；返回新建
 *         XString* 布局快照，调用方负责 delete_base）。
 * @param self 目标主窗口。
 * @return 新建 XString*；失败返回 NULL。
 */
XString* XMainWindow_saveState(const XMainWindow* self);
/** @brief 恢复窗口布局状态（对标 QMainWindow::restoreState）。
 * @param self 目标主窗口。
 * @param state 借用 XString* 快照；可为 NULL（重置）。
 * @return 恢复成功返回 true。
 */
bool XMainWindow_restoreState(XMainWindow* self, const XString* state);

void* XMainWindow_iconSizeChanged_signal(XMainWindow* self, int width, int height);

#endif /* XWIDGET_ON && XMAINWINDOW_ON */

#endif /* XMAINWINDOW_H */