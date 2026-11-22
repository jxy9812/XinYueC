/**
 * @file       HsfOverlayPage.h
 * @brief      悬浮窗设置**分区**（原独立页 16，已并入本地设置页 13）。
 * @details    [2026-11-13 用户定案 三页合并] 原「悬浮窗设置」独立页
 *             （页号 16）作废，其全部控件内容并入「本地设置」页（页号
 *             13）的「悬浮窗」分区；本模块改为**分区构建器**：内容一字
 *             不减（显示总开关/FPS/帧耗时/CPU·GPU/网络/内存/内存显示
 *             种类/数据来源/预设位置/固定位置/允许拖动/字号/自适应尺寸/
 *             复位默认），已真兑现的开关继续原路径兑现（悬浮层 getter
 *             + INI 落盘 + 即时生效）。
 *             页面主体代码移植自 XGuiWindowDemo 对应页（悬浮窗设置），
 *             经 HsfDemoCompat 兼容层收敛主窗访问器依赖。
 */
#ifndef HSFOVERLAYPAGE_H
#define HSFOVERLAYPAGE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#include "XWidget.h"
#include "HsfPages.h"

#if XWIDGET_ON && XPUSHBUTTON_ON && XLABEL_ON

/**
 * @brief      创建「悬浮窗」分区（堆分配，挂 parent，随本地设置页级联释放）。
 * @param parent 父控件（本地设置页根）。
 * @param userData 透传回调的用户数据（主窗指针）。
 * @return 分区根控件借用指针；失败 NULL。
 */
XWidget* HsfOverlayPage_buildSection(XWidget* parent, void* userData);

#endif /* XWIDGET_ON && XPUSHBUTTON_ON && XLABEL_ON */

#ifdef __cplusplus
}
#endif

#endif /* HSFOVERLAYPAGE_H */
