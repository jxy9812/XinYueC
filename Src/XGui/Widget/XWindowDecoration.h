/**
 * @file       XWindowDecoration.h
 * @brief      XWindowDecoration 框架级窗口装饰（自绘系统标题栏，对标
 *             桌面 WM 标题栏功能全集）。
 * @details    无窗口管理器环境（fbdev 直写面板、X11 后端不可用回落）
 *             下，框架为可装饰的顶层窗口绘制系统标题栏并接管其输入：
 *             窗口标题（超宽省略）+ 窗口图标/系统菜单钮 + 最小化/最大
 *             化(还原)/关闭三键 + 双击切换最大化 + 空白区拖拽移动 +
 *             边缘 8 向拖拽改尺寸 + 活动窗口配色。绘制经样式系统
 *             CC_TitleBar（调色板/标准图标/PM 度量），与桌面由 WM 绘
 *             制的标题栏共用同一套风格开关；上层 API 与桌面完全一致
 *             （XWidget_setWindowFlags 的 WindowTitleHint/
 *             WindowSystemMenuHint/WindowMinMaxButtonsHint/
 *             WindowCloseButtonHint/CustomizeWindowHint/
 *             FramelessWindowHint + setWindowTitle/setIcon +
 *             showMinimized/showMaximized/showNormal + XWindow_
 *             frameMargins）。
 *             桌面（X11 原生窗受 WM 管理）自动停用：flags 经
 *             _MOTIF_WM_HINTS 交给 WM，行为与自绘路径同语义。环境变量
 *             XGUI_CSD=1 可在桌面强制启用自绘（目验/调试；真 WM 下会
 *             双重标题栏），XGUI_CSD=0 强制停用。
 * @note       本模块无独立开关，随 XWIDGET_ON/XWINDOW_ON/XSTYLE_ON/
 *             XWINDOWEVENT_ON 生效；活动判定为假时全部入口零开销短路。
 * @author     XinYueC 团队
 */
#ifndef XWINDOWDECORATION_H
#define XWINDOWDECORATION_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XGuiConfig.h"
#include "XWidget.h"

#if XWIDGET_ON && XWINDOW_ON && XSTYLE_ON && XWINDOWEVENT_ON

/**
 * @brief      判断顶层控件当前是否由框架绘制窗口装饰。
 * @details    判据：控件为顶层窗口且已建桥接窗口，类型可装饰
 *             （Window/Dialog/Tool；Popup/ToolTip/SplashScreen 等瞬态
 *             类型永不装饰），未设 FramelessWindowHint，且无原生窗口
 *             （fbdev/X11 不可用回落）或 XGUI_CSD=1 强制。
 * @param      top 顶层控件；可为 NULL。
 * @return     由框架绘制返回 true。
 */
bool XWindowDecoration_activeFor(const XWidget* top);

/**
 * @brief      返回装饰保留边距（标题栏计入顶部，其余为零）。
 * @param      top 顶层控件；可为 NULL。
 * @return     未装饰时返回全零边距。
 */
XMargins XWindowDecoration_marginsFor(const XWidget* top);

/**
 * @brief      桥接窗口建立/flags 变化后重算并落盘保留边距。
 * @details    由 XWidget_createWindow（建窗后）与
 *             XWidget_setWindowFlags（活窗动态改 flags 后）调用；把
 *             XWindow_frameMargins 从恒零占位变为真实保留边距。
 * @param      top 顶层控件；可为 NULL。
 */
void XWindowDecoration_syncWindow(XWidget* top);

/**
 * @brief      绘制标题栏条带（在顶层控件子树绘制完成后调用）。
 * @details    由 XWidget_flushBackingStore 在 paintTree 之后调用；
 *             画前经 region 外接矩形与条带的相交短路，不相交零开销。
 * @param      top 顶层控件；可为 NULL。
 * @param      painter 已 begin 到窗口后备图像的画笔。
 * @param      region 本次刷新区域（窗口本地坐标；可为 NULL=整窗）。
 */
void XWindowDecoration_draw(XWidget* top, XPainter* painter,
                            const XRegion* region);

/**
 * @brief      窗口装饰输入拦截（鼠标形状事件，窗口本地坐标）。
 * @details    由 XWidget_dispatchPointerEvent 在命中测试之前调用：
 *             标题栏条带内事件全部由装饰接管（按钮武装/拖拽移动/双击
 *             最大化/右键系统菜单/滚轮吞掉）；边缘改尺寸带仅在无子控
 *             件接住按下时接管（子控件优先，与桌面 WM 帧外命中同效）。
 * @param      top 顶层控件。
 * @param      event 鼠标/滚轮/上下文菜单事件。
 * @return     事件已消费返回 true（调用方不再向控件树派发）。
 */
bool XWindowDecoration_handlePointer(XWidget* top, XEvent* event);

/**
 * @brief      指针离开顶层窗口时清理悬停态（LEAVE 分支调用）。
 * @param      top 顶层控件；可为 NULL。
 */
void XWindowDecoration_handleLeave(XWidget* top);

/**
 * @brief      窗口注销钩子：丢弃装饰状态（应用 removeWindow 时调用）。
 * @param      win 已注销的桥接窗口；可为 NULL。
 */
void XWindowDecoration_notifyWindowDestroyed(XWindow* win);

/**
 * @brief      焦点窗口切换钩子：刷新新旧活动窗的标题栏配色。
 * @details    由 XGuiApplication_setFocusWindow 对新旧焦点窗口调用；
 *             窗口未装饰时零开销。
 * @param      win 状态可能变化的窗口；可为 NULL。
 */
void XWindowDecoration_notifyActivation(XWindow* win);

/**
 * @brief      标题/状态等外观相关属性变化后刷新条带（轻量重绘）。
 * @param      top 顶层控件；可为 NULL。
 */
void XWindowDecoration_notifyAppearanceChanged(XWidget* top);

#endif /* XWIDGET_ON && XWINDOW_ON && XSTYLE_ON && XWINDOWEVENT_ON */

#ifdef __cplusplus
}
#endif
#endif /* XWINDOWDECORATION_H */
