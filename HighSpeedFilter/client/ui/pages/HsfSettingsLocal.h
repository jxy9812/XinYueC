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
 * @brief      创建本地设置叶子页（堆分配，挂 parent，随主窗级联释放）。
 * @param parent 父控件（堆栈宿主）。
 * @param navFn 导航回调（返回钮用）；可 NULL。
 * @param userData 透传回调的用户数据。
 * @return 页根控件借用指针；失败 NULL。
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
 * @brief      读「界面刷新周期」（数据刷新卡；各页轮询定时器建页时取此值）。
 * @return     刷新周期 ms（INI 值钳位 [100,2000]；缺省 200）。
 * @note       设置存储关闭（--no-settings）时恒返回缺省值。
 */
int HsfSettingsLocal_refreshMs(void);

#endif /* XWIDGET_ON && XPUSHBUTTON_ON && XLABEL_ON */

#ifdef __cplusplus
}
#endif

#endif /* HSFSETTINGSLOCAL_H */
