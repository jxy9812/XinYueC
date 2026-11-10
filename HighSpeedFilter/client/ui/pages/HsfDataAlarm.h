/**
 * @file       HsfDataAlarm.h
 * @brief      报警叶子页：设备健康边沿触发 → 报警表 + 次数/分钟折线图。
 * @details    [2026-11-10] 本页**早已实装**（不是占位）：边沿记录（同一
 *             异常不重复刷）、筛选/清除、导出、重新加载、类型与列筛选、
 *             表格↔图表双视图、X 轴时:分刻度、按页字号缩放。原先页头
 *             写的「占位/真实内容后续替换」是历史遗留，会误导接手的人。
 */
#ifndef HSFDATAALARM_H
#define HSFDATAALARM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#include "XWidget.h"
#include "HsfPages.h"

#if XWIDGET_ON && XPUSHBUTTON_ON && XLABEL_ON

/**
 * @brief      创建报警叶子页（堆分配，挂 parent，随主窗级联释放）。
 * @param parent 父控件（堆栈宿主）。
 * @param navFn 导航回调（返回钮用）；可 NULL。
 * @param userData 透传回调的用户数据。
 * @return 页根控件借用指针；失败 NULL。
 */
XWidget* HsfDataAlarm_create(XWidget* parent, HsfMenuNavFn navFn,
                           void* userData);

/**
 * @brief      窄宽/竖屏适配（帧泵与切页路径调用；幂等）。
 * @param page 页根（HsfDataAlarm_create 返回值）。
 * @param compact true=窄档（收边距）。
 */
void HsfDataAlarm_adapt(XWidget* page, bool compact);

#endif /* XWIDGET_ON && XPUSHBUTTON_ON && XLABEL_ON */

#ifdef __cplusplus
}
#endif

#endif /* HSFDATAALARM_H */
