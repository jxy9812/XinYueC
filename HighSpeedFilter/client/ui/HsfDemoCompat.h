/**
 * @file       HsfDemoCompat.h
 * @brief      XGuiDemo 页面模块移植兼容层（契约 + 主窗访问器）。
 * @details    网络/悬浮窗/窗口设置三页自 XGuiDemo 移植（pages/
 *             HsfNetworkPage.c、HsfOverlayPage.c、HsfWindowPage.c）。
 *             原页面依赖 XGuiDemo 主窗的 demo_main_* 访问器（悬浮层实例/
 *             顶层控件/状态栏显隐/chrome 重排/场景失效），本头把这些
 *             依赖收敛为同一组访问器，由 HsfUi.c 针对 HsfWin 实现——
 *             页面代码本身零改动移植，后续演进可在 Hsf 侧单独维护。
 */
#ifndef HSFDEMOCOMPAT_H
#define HSFDEMOCOMPAT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#include "XWidget.h"

/** @brief 页面向主窗反馈交互结果的回调（user=归属主窗指针）。 */
typedef void (*DemoPageStatusFn)(void* user, const char* text);

/* ---- 主窗访问器（由 HsfUi.c 针对 HsfWin 实现） ---- */

/** @brief 主窗顶层控件借用指针（user=归属主窗）。 */
void* demo_main_toplevel(void* user);

/** @brief 主窗性能悬浮层实例借用指针（无悬浮层时 NULL）。 */
void* demo_main_overlay(void* user);

/** @brief 悬浮层九宫格预设位置（preset 取 XPerformanceOverlayPosition）。 */
void demo_main_overlay_applyPreset(void* user, int preset);

/** @brief 底部状态栏显隐切换（返回切换后可见性）。 */
bool demo_main_setStatusBarVisible(void* user, bool visible);

/** @brief 查询底部状态栏当前可见性。 */
bool demo_main_statusBarVisible(void* user);

/** @brief 静态场景缓存失效并整窗重绘（皮肤切换用）。 */
void demo_main_sceneInvalidate(void* user);

/** @brief 主窗 chrome 全量重排（边框开关/状态栏显隐用）。 */
void demo_main_refreshChrome(void* user);

/* ---- 移植页面契约 ---- */

/** @brief 构建网络设置页（枚举网卡/IPv4 配置/DHCP↔静态切换/静态参数应用）。 */
XWidget* hsf_page_network_build(XWidget* parent,
                                DemoPageStatusFn status, void* user);
/** @brief 网络设置页自适应重排（信息区伸缩 + 状态行贴底）。 */
void hsf_page_network_adapt(XWidget* page);
/** @brief 网络设置页切回可见钩子（无线会话恢复入口）。 */
void hsf_page_network_resume(void);

/** @brief 构建悬浮窗设置页（悬浮层显示项/九宫格位置/固定与拖动/字号）。 */
XWidget* hsf_page_overlay_build(XWidget* parent,
                                DemoPageStatusFn status, void* user);
/** @brief 悬浮窗设置页自适应重排。 */
void hsf_page_overlay_adapt(XWidget* page);
/**
 * @brief 悬浮窗设置持久化恢复：配置文件 → 悬浮层运行时 + 页面控件。
 * @note  在**主窗显示之前**调用（首帧即用户设定的形态，无中间态闪烁）。
 */
void hsf_page_overlay_restoreSettings(void);
/**
 * @brief 悬浮窗设置页自测（逐项切换 → getter 回读 → 复位默认态）。
 * @note  env 钩子用（XHSF_SETTINGS_AUTOTEST）：直呼槽 = 用户点击的等价
 *        路径，用于闭环验证「改动 → 落盘 → 复位」。
 */
int hsf_page_overlay_autotest(XWidget* page);

/** @brief 构建窗口设置页（皮肤/边框/状态栏/最大化/全屏/朝向/光标/休眠）。 */
XWidget* hsf_page_window_build(XWidget* parent,
                               DemoPageStatusFn status, void* user);
/** @brief 窗口设置页自适应重排。 */
void hsf_page_window_adapt(XWidget* page);
/** @brief 窗口状态改变时回读主窗当前态（全屏复选框同步）。 */
void hsf_page_window_syncFromWindow(void);
/**
 * @brief 窗口设置持久化恢复（**显示前**阶段）：皮肤/画布旋转/窗口方向/
 *        光标/电源策略/最大化全屏 + 页面控件回填。
 * @note  边框与状态栏属 chrome 重排项，走 hsf_page_window_restoreChrome。
 */
void hsf_page_window_restoreSettings(void);
/**
 * @brief 窗口设置持久化恢复（**显示后**阶段）：边框保留边距 + 状态栏显隐。
 * @note  必须放在窗口显示之后——这两项要跑 chrome 重排（含形态判定），
 *        显示前跑会让紧凑态侧栏被随后的 show 递归显示出来。
 */
void hsf_page_window_restoreChrome(void);
/**
 * @brief 窗口设置页自测（真实鼠标事件路径 → 逐项断言 → 复位默认态）。
 * @note  env 钩子用（XHSF_SETTINGS_AUTOTEST）：真实点击路径 = 用户操作
 *        的等价路径，用于闭环验证「改动 → 落盘 → 复位」。
 */
int hsf_page_window_autotest(XWidget* page);

#ifdef __cplusplus
}
#endif

#endif /* HSFDEMOCOMPAT_H */
