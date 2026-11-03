/**
 * @file       HsfMonitorVfd.h
 * @brief      变频器监视页：仪表组 + 状态横幅 + 给定/操作 + 趋势 + 控制闸。
 * @details    页面零业务：全部数据/动作经 HsfVfdProvider 函数表（本战役
 *             内置进程内模拟源，后端接入时换表零改动）。渲染只读
 *             XVfdDevice_status_const() 快照 + m_validMask 显隐。
 */
#ifndef HSFMONITORVFD_H
#define HSFMONITORVFD_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#include "XWidget.h"
#include "HsfPages.h"

#if XWIDGET_ON && XPUSHBUTTON_ON && XLABEL_ON

/**
 * @brief      创建变频器叶子页（堆分配，挂 parent，随主窗级联释放）。
 * @param parent 父控件（堆栈宿主）。
 * @param navFn 导航回调（返回钮用）；可 NULL。
 * @param userData 透传回调的用户数据。
 * @return 页根控件借用指针；失败 NULL。
 */
XWidget* HsfMonitorVfd_create(XWidget* parent, HsfMenuNavFn navFn,
                           void* userData);

/**
 * @brief      形态自适应（横屏仪表横排/竖屏纵排）。
 * @param page 页根控件（HsfMonitorVfd_create 返回值）。
 * @param compact true=窄屏/竖屏形态。
 * @note       由 HsfUi 帧泵每循环调用：必须幂等、廉价（只改布局参数）。
 */
void HsfMonitorVfd_adapt(XWidget* page, bool compact);

/**
 * @brief      设置交互模式（自动运行锁；对齐 HsfMonitorDigital 同名 API）。
 * @param interactive true=手动模式（勾选框可见，闸值=此前状态）；
 *                    false=自动运行（隐藏勾选框 + 控制闸强制关 +
 *                    全部操作控件禁用，界面只读，API 动作也被设备层
 *                    控制闸拒绝——XVfdError_ControlDisabled）。
 * @return     无。
 * @note       页未建时调用：只记意图，create 时按此初始化。
 *             典型接线：自动程序启动处 setInteractiveMode(false)，
 *             退出自动处 setInteractiveMode(true)。
 */
void HsfMonitorVfd_setInteractiveMode(bool interactive);

/**
 * @brief      查询交互模式。
 * @return     true=手动（界面可操作）；false=自动运行（只读）。
 * @note       页未建时返回建页前的模式意图（缺省 true=手动）。
 */
bool HsfMonitorVfd_interactiveMode(void);

#endif /* XWIDGET_ON && XPUSHBUTTON_ON && XLABEL_ON */

#ifdef __cplusplus
}
#endif

#endif /* HSFMONITORVFD_H */
