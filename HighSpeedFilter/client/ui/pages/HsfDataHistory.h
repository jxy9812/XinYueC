/**
 * @file       HsfDataHistory.h
 * @brief      历史数据叶子页：服务端历史分页（0x0118）→ 表格 + 折线图。
 * @details    [2026-11-10] 本页**早已实装**（不是占位）：筛选/关键字/列
 *             筛选、翻页、表格↔图表双视图、导出（CSV/XLSX）、表头双击
 *             排序、悬浮气泡、缩放重采样、按页字号缩放。原先页头写的
 *             「占位/真实内容后续替换」是历史遗留，会误导接手的人。
 */
#ifndef HSFDATAHISTORY_H
#define HSFDATAHISTORY_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#include "XWidget.h"
#include "HsfPages.h"

#if XWIDGET_ON && XPUSHBUTTON_ON && XLABEL_ON

/**
 * @brief      创建历史数据叶子页（堆分配，挂 parent，随主窗级联释放）。
 * @param parent 父控件（堆栈宿主）。
 * @param navFn 导航回调（返回钮用）；可 NULL。
 * @param userData 透传回调的用户数据。
 * @return 页根控件借用指针；失败 NULL。
 */
XWidget* HsfDataHistory_create(XWidget* parent, HsfMenuNavFn navFn,
                           void* userData);

/**
 * @brief      窄宽/竖屏适配（帧泵与切页路径调用；幂等）。
 * @param page 页根（HsfDataHistory_create 返回值）。
 * @param compact true=窄档（收边距）。
 */
void HsfDataHistory_adapt(XWidget* page, bool compact);

#endif /* XWIDGET_ON && XPUSHBUTTON_ON && XLABEL_ON */

#ifdef __cplusplus
}
#endif

#endif /* HSFDATAHISTORY_H */
