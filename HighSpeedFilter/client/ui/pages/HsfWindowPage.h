/**
 * @file       HsfWindowPage.h
 * @brief      窗口与屏幕设置**分区**（原独立页 17，已并入本地设置页 13）。
 * @details    [2026-11-13 用户定案 三页合并] 原「窗口设置」独立页（页号
 *             17）作废，其全部控件内容并入「本地设置」页（页号 13）的
 *             「窗口与屏幕」分区；本模块改为**分区构建器**：内容一字
 *             不减（主题/显示边框/显示状态栏/最大化/全屏/显示鼠标光标/
 *             鼠标外形/画布旋转/窗口方向/自动熄屏/自动休眠/立即熄屏/
 *             唤醒方式三项/复位默认），**屏幕休眠与电源策略**原样搬入
 *             （含平台能力探测：不支持项置灰并如实说明）。
 *             页面主体代码移植自 XGuiWindowDemo 对应页（窗口设置），
 *             经 HsfDemoCompat 兼容层收敛主窗访问器依赖。
 */
#ifndef HSFWINDOWPAGE_H
#define HSFWINDOWPAGE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#include "XWidget.h"
#include "HsfPages.h"

#if XWIDGET_ON && XPUSHBUTTON_ON && XLABEL_ON

/**
 * @brief      创建「窗口与屏幕」分区（堆分配，挂 parent，随本地设置页级联释放）。
 * @param parent 父控件（本地设置页根）。
 * @param userData 透传回调的用户数据（主窗指针）。
 * @return 分区根控件借用指针；失败 NULL。
 */
XWidget* HsfWindowPage_buildSection(XWidget* parent, void* userData);

#endif /* XWIDGET_ON && XPUSHBUTTON_ON && XLABEL_ON */

#ifdef __cplusplus
}
#endif

#endif /* HSFWINDOWPAGE_H */
