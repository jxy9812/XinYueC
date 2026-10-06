/**
 * @file       XWindowDecoration.h
 * @brief      XWindowDecoration 框架级窗口装饰（自绘系统标题栏的判定/
 *             抑制/承载/输入拦截层，对标桌面 WM 标题栏功能全集）。
 * @details    无窗口管理器环境（fbdev 直写面板、X11 后端不可用回落）
 *             下，框架为可装饰的顶层窗口提供系统标题栏并接管其输入：
 *             窗口标题（超宽省略）+ 窗口图标/系统菜单钮 + 最小化/最大
 *             化(还原)/关闭三键 + 双击切换最大化 + 空白区拖拽移动 +
 *             边缘 8 向拖拽改尺寸 + 活动窗口配色。
 *             职责分层（本模块只做判定/抑制/承载/输入，绘制归控件）：
 *             - 归属判定问平台策略抽象
 *               XPlatformThemeDecoration_effectiveMode（Framework=
 *               框架自绘、System=交 WM、Auto=fbdev 探测，环境变量
 *               XGUI_CSD 经其一次性解析），逐窗再按窗口类型/提示位
 *               裁量；
 *             - 条带绘制由树内标题条控件 XTitleBar 自绘（原 CC_TitleBar
 *               样式绘制实现已迁入该控件，用户可继承定制）；本模块负责
 *               为被装饰顶层承载该条控件（用户经
 *               XWidget_setTitleBarWidget 挂自定义条则用之，否则创建
 *               默认 XTitleBar 实例）并随宿主几何重钉条区；默认条生命
 *               周期随装饰——装饰失活或宿主注销时经 XObject_deleteLater
 *               异步释放，自定义条为借用，只解挂不释放；
 *             - 框架自绘激活时置 CSD 抑制位（XWindow_setCsdFrameSuppressed），
 *               平台原生窗口后端据此抑制 _MOTIF_WM_HINTS 原生装饰，
 *               桌面强制自绘不再出现双标题栏；
 *             - 输入拦截先于控件命中（XWidget_dispatchPointerEvent 头
 *               部调用）：条带内事件三分流——条控件的子控件（用户按钮）
 *               放行树派发，空白区/样式按钮位（armed/拖拽移动/双击最大
 *               化/右键系统菜单/滚轮吞掉）由本模块接管，边缘 8 向改尺
 *               寸带仅在无子控件接住按下时接管（子控件优先，与桌面 WM
 *               帧外命中同效）。
 *             上层 API 与桌面完全一致（XWidget_setWindowFlags 的
 *             WindowTitleHint/WindowSystemMenuHint/
 *             WindowMinMaxButtonsHint/WindowCloseButtonHint/
 *             CustomizeWindowHint/FramelessWindowHint + setWindowTitle/
 *             setIcon + showMinimized/showMaximized/showNormal +
 *             XWindow_frameMargins），系统条模式（WM 绘制）与框架条
 *             模式下同样有效。
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
 * @brief      返回装饰保留边距（标题条计入顶部，其余为零）。
 * @details    条高取被装饰顶层挂载的标题条控件实际高度；条控件尚未
 *             承载（布局期预测）时回退 XTitleBar_defaultHeight，保证
 *             「句柄未建也能预测」的排布语义。
 * @param      top 顶层控件；可为 NULL。
 * @return     未装饰时返回全零边距。
 */
XMargins XWindowDecoration_marginsFor(const XWidget* top);

/**
 * @brief      桥接窗口建立/flags 变化后重算并落盘保留边距。
 * @details    由 XWidget_createWindow（建窗后）、
 *             XWidget_setWindowFlags（活窗动态改 flags 后）与
 *             XWidget_setTitleBarWidget（自定义条挂载/摘除后）调用；
 *             把 XWindow_frameMargins 从恒零占位变为真实保留边距，并
 *             在框架自绘激活时兜底刷新 CSD 抑制位（建窗前首次预置由
 *             XWidget_createWindow 负责）、确保被装饰顶层挂有标题条
 *             控件（用户自定义条或默认 XTitleBar 实例）。
 * @param      top 顶层控件；可为 NULL。
 */
void XWindowDecoration_syncWindow(XWidget* top);

/**
 * @brief      顶层窗口几何变化后同步重钉生效标题条（任意来源收口）。
 * @details    由控件级 RESIZE 事件唯一漏斗（XWidget.c VXWidget_event
 *             的 RESIZE 分支，槽派发之后）对被装饰顶层调用，一处覆盖
 *             全部几何来源：WM 拖边框/xdotool windowsize（ConfigureNotify
 *             → XWidget_applyWindowGeometry）、程序化 XWidget_setGeometry/
 *             XWindow_resize（XWidget_recomputeGeometry）、装饰自身的
 *             拖拽/改尺寸/最大化/卷起（其后自重钉，此处同值幂等）。
 *             零成本短路：非顶层、未装饰、无登记状态或未承载条控件时
 *             直接返回——本入口不反向创建状态或标题条（承载语义归
 *             XWindowDecoration_syncWindow）；重钉经既有条区钉制路径
 *             →XWidget_setGeometry(条)，同值幂等短路，条几何变化时随
 *             新旧双失效触发重绘；只钉条子控件——对顶层自身改几何会
 *             经平台同步→ConfigureNotify 回环递归，严禁。
 * @param      top 顶层控件；可为 NULL。
 */
void XWindowDecoration_syncBarGeometry(XWidget* top);

/**
 * @brief      兼容空壳：旧「绘制标题栏条带」入口（在顶层控件子树绘制
 *             完成后调用）。
 * @details    条带绘制已迁为树内标题条控件 XTitleBar 的 paintEvent 自
 *             绘（随 XWidget_paintTree 正常子控件绘制），本函数仅保留
 *             签名兼容既有调用点（XWidget_flushBackingStore 尾部），
 *             函数体短路零开销。
 * @param      top 顶层控件；可为 NULL。
 * @param      painter 未使用（保留签名兼容）。
 * @param      region 未使用（保留签名兼容）。
 */
void XWindowDecoration_draw(XWidget* top, XPainter* painter,
                            const XRegion* region);

/**
 * @brief      窗口装饰输入拦截（鼠标/滚轮/上下文菜单事件，窗口本地坐标）。
 * @details    由 XWidget_dispatchPointerEvent 在命中测试之前调用。
 *             条带内事件三分流：命中条控件的子控件（用户按钮）放行
 *             return false 交树派发；空白区/样式按钮位由装饰接管
 *             （按钮武装/拖拽移动/双击最大化/右键系统菜单/滚轮吞掉）；
 *             边缘改尺寸带仅在无子控件接住按下时接管（子控件优先，与
 *             桌面 WM 帧外命中同效）。默认条无子控件，行为与接管全条
 *             带的历史口径等价。
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

/**
 * @brief      当前是否有任一装饰顶层处于手势进行态（拖拽移动/改尺寸）。
 * @details    只读查询：扫装饰注册表任一 m_dragging/m_resizing，零状态
 *             新增。消费方=demo 性能悬浮层手势期冻结（恒可用）；
 *             CSD=0（WM 管理）下无手势态，恒 false，悬浮层行为不变。
 * @return     任一顶层手势进行中返回 true。
 */
bool XWindowDecoration_gestureActive(void);

/**
 * @brief      查询顶层窗口是否处于最小化卷起态（Shade：只剩标题条）。
 * @details    卷起态下窗口内容不可见，宿主挂靠的悬浮件（性能悬浮窗等）
 *             应随内容隐藏，防止重锚钳位后覆盖标题条（真机用户实测
 *             2026-10-06）。未装饰/无状态/桌面原生窗恒 false。
 * @param      top 顶层控件；可为 NULL。
 * @return     卷起态返回 true。
 */
bool XWindowDecoration_isShaded(const XWidget* top);

/**
 * @brief      进程内「拖动快照 blit」落地步累计（诊断自证用）。
 * @details    快照直写 fb 可见面每成功一步加一，恒单调不回退。消费方=
 *             离屏回归（XGuiDialogMoveTest 断言快照路径真实生效，防门
 *             控/条件失配静默回落旧路径的假绿）与真机诊断（env
 *             XGUI_GESTURE_PROF 报告的 snap 计数同源）。快照模式关闭
 *             （XGUI_DRAG_SNAPSHOT_BLIT=0）或环境不满足时恒不增长。
 * @return     累计步数。
 */
int XWindowDecoration_dragSnapshotStepCount(void);

/**
 * @brief      fbdev 面板矩形探测（无 WM 单屏语义；非 fbdev 环境恒 false）。
 * @details    显示驱动在位时输出整面板矩形（0,0,面板宽,面板高）并返回
 *             true——语义与装饰内部 xwd_panelRect(NULL,·) 一致（桌面 WM
 *             环境无驱动、XScreen 回退需窗口上下文，此处恒 false）。消费
 *             方=自绘拖移的顶层窗口（屏幕键盘紧凑悬浮拖移等）：拖动走廊
 *             按面板钳边，与 xwd_applyMove 同口径。
 * @param      outPanel 输出面板矩形；可 NULL（仅作环境探测）。
 * @return     fbdev 显示驱动在位返回 true。
 */
bool XWindowDecoration_fbdevPanelRect(XRect* outPanel);

/**
 * @brief      顶层窗口从 oldG 让出到 newG 的暴露条带按归属归位还原。
 * @details    装饰拖拽移动（xwd_applyMove）同款机制的公共出口，供非装饰
 *             拖移路径（屏幕键盘紧凑悬浮拖移等）复用：条带与各可见顶层
 *             几何的交集自各归属顶层的后备缓冲直搬两缓冲（Z 序低→高遍
 *             历，高层覆写），裸露面板余部填桌面底色。fbdev 无 WM 环境
 *             专用语义——blit/fill 在非 fbdev 平台为 no-op，调用方应以
 *             XWindowDecoration_fbdevPanelRect 为门。绝不逐帧注入 expose
 *             （EXPOSE 处理恒整窗重合成，高频拖拽烧穿单核）。
 * @param      oldG 移动前窗口几何（全局/面板坐标）。
 * @param      newG 移动后窗口几何（须先落几何再调用，条带还原以「本窗
 *             已离开旧位」为前提）。
 * @param      selfWindow 移动中的顶层窗口（归属遍历时剔除自身）；可 NULL。
 */
void XWindowDecoration_restoreExposeStrips(const XRect* oldG,
                                           const XRect* newG,
                                           XWindow* selfWindow);

#endif /* XWIDGET_ON && XWINDOW_ON && XSTYLE_ON && XWINDOWEVENT_ON */

#ifdef __cplusplus
}
#endif
#endif /* XWINDOWDECORATION_H */
