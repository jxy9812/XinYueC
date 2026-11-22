#ifndef HSFSETTINGSLOCAL_H
#define HSFSETTINGSLOCAL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#include "XWidget.h"
#include "HsfPages.h"

#if XWIDGET_ON && XPUSHBUTTON_ON && XLABEL_ON

/**
 * @brief      创建本地设置叶子页（三分区合并页；堆分配，挂 parent，随主窗级联释放）。
 * @param parent 父控件（堆栈宿主）。
 * @param navFn 导航回调（返回钮用）；可 NULL。
 * @param userData 透传回调的用户数据。
 * @return 页根控件借用指针；失败 NULL。
 * @details    [2026-11-13 三页合并] 页内三分区：①界面设置 ②悬浮窗
 *             ③窗口与屏幕；原独立页 16（悬浮窗设置）/17（窗口设置）作废。
 */
XWidget* HsfSettingsLocal_create(XWidget* parent, HsfMenuNavFn navFn,
                           void* userData);

/**
 * @brief      窄宽/竖屏适配（帧泵与切页路径调用；幂等）。
 * @param page 页根（HsfSettingsLocal_create 返回值）。
 * @param compact true=窄档（收边距）。
 */
void HsfSettingsLocal_adapt(XWidget* page, bool compact);

/**
 * @brief      取「悬浮窗」分区根（自测钩子用；NULL=分区未建）。
 * @param page 页根（HsfSettingsLocal_create 返回值）。
 * @return 分区根控件借用指针。
 */
XWidget* HsfSettingsLocal_overlaySection(XWidget* page);

/**
 * @brief      取「窗口与屏幕」分区根（自测钩子用；NULL=分区未建）。
 * @param page 页根（HsfSettingsLocal_create 返回值）。
 * @return 分区根控件借用指针。
 */
XWidget* HsfSettingsLocal_windowSection(XWidget* page);

/**
 * @brief      读「界面刷新周期」（数据刷新卡；各页轮询定时器建页时取此值）。
 * @return     刷新周期 ms（INI 值钳位 [100,2000]；缺省 200）。
 * @note       设置存储关闭（--no-settings）时恒返回缺省值。
 */
int HsfSettingsLocal_refreshMs(void);

/**
 * @brief      本页周期刷新（主窗帧泵 1s 节拍调；仅本页可见时）。
 * @param page 页根（HsfSettingsLocal_create 返回值）。
 * @details    ① 提交端口改动（微调框值 ≠ 已生效端口 ⇒ 落盘 + 重启网页服
 *             务子进程换监听口）；放在 1s 节拍而不是 valueChanged 槽里——
 *             边打字边重启进程会把机器折腾得没完。
 *             ② 刷新「网页服务」状态行（子进程退出时状态要变）。
 */
void HsfSettingsLocal_refresh(XWidget* page);

#endif /* XWIDGET_ON && XPUSHBUTTON_ON && XLABEL_ON */

#ifdef __cplusplus
}
#endif

#endif /* HSFSETTINGSLOCAL_H */
