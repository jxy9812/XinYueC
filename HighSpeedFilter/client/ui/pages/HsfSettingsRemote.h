/**
 * @file       HsfSettingsRemote.h
 * @brief      远端设置叶子页：运行模式 / 自动运行 / 采集配置 / 数据存储 / 登录策略。
 * @details    [2026-11-10] 本页早已实装**（不是占位）：模式切换、自动
 *             运行启停、7 行采集开关+周期（刷新/应用）、数据存储 6 项 +
 *             容量占用条 + 用量实况、登录策略 3 项 + 应用、按服务端快照
 *             回填。原先页头写的「占位/真实内容后续替换」是历史遗留，会
 *             误导接手的人。
 */
#ifndef HSFSETTINGSREMOTE_H
#define HSFSETTINGSREMOTE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#include "XWidget.h"
#include "HsfPages.h"

#if XWIDGET_ON && XPUSHBUTTON_ON && XLABEL_ON

/**
 * @brief      创建远端设置叶子页（堆分配，挂 parent，随主窗级联释放）。
 * @param parent 父控件（堆栈宿主）。
 * @param navFn 导航回调（返回钮用）；可 NULL。
 * @param userData 透传回调的用户数据。
 * @return 页根控件借用指针；失败 NULL。
 */
XWidget* HsfSettingsRemote_create(XWidget* parent, HsfMenuNavFn navFn,
                           void* userData);

/**
 * @brief      远端设置页窄宽自适应（帧泵/切页/形态切换驱动，幂等）。
 * @param      page 页根控件（= create 返回值，内部强转回本页结构）。
 * @param      compact true=窄档（窗口 <HSF_COMPACT_MAX_W）：收边距+压缩策略下拉。
 * @return     无。
 * @note       2026-11-09 竖屏适配批次补齐（原「固定五卡，无窄宽分档」登记缺口）。
 */
void HsfSettingsRemote_adapt(XWidget* page, bool compact);

#endif /* XWIDGET_ON && XPUSHBUTTON_ON && XLABEL_ON */

#ifdef __cplusplus
}
#endif

#endif /* HSFSETTINGSREMOTE_H */
