/**
 * @file       HsfSettingsRemote.h
 * @brief      远端设置叶子页（占位）：说明文字 + 返回钮。
 * @details    页面骨架先以 HsfPlaceHolder 占位，真实内容后续在本模块内替换。
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
