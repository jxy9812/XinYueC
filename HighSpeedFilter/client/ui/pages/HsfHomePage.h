/**
 * @file       HsfHomePage.h
 * @brief      主界面页：自动模式卡（首行）+ 总览卡片行 + 状态回显。
 * @details    首页是落地第一屏，2026-11-12 用户定案**删掉页头那行**
 *             （大标题「主界面」+ 副标题「高速过滤器监控系统」），改由
 *             **首行直接放自动模式卡**（操作员最常用的入口），其后是
 *             总览卡片行（温湿度/变频器/设备健康 → 压差/流量计/电动球阀 →
 *             浊度仪/按钮组），设备状态表点健康卡弹浮层。
 *             自动模式卡的序列逻辑全在服务端（客户端只发意图 + 回显
 *             服务端相位/报警/累计运行时长），详见 HsfHomePage.c。
 */
#ifndef HSFHOMEPAGE_H
#define HSFHOMEPAGE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#include "XWidget.h"
#include "HsfPages.h"

#if XWIDGET_ON && XPUSHBUTTON_ON && XLABEL_ON

/**
 * @brief      创建主界面页（堆分配，挂 parent，随主窗级联释放）。
 * @param parent 父控件（堆叠宿主）。
 * @param navFn 导航回调（快捷入口用）。
 * @param userData 透传回调的用户数据。
 * @return 页根控件借用指针；失败 NULL。
 */
XWidget* HsfHomePage_create(XWidget* parent, HsfMenuNavFn navFn,
                            void* userData);

/**
 * @brief      形态自适应（宽屏横排 / 窄屏竖排的卡片行）。
 * @param page 页根控件。
 * @param compact true=紧凑/竖屏形态。
 * @note       只翻**卡片行**的排布方向（第一行/第二行/第三行）；自动模式卡
 *             恒为单卡独行（首行），不参与翻排。
 */
void HsfHomePage_adapt(XWidget* page, bool compact);

/**
 * @brief      刷新看板数据（三卡数值 + 设备状态表）。
 * @param page 页根控件（HsfHomePage_create 返回值；NULL 安全）。
 * @note       由主窗帧泵在首页可见时按 200ms 节流调用；页自身不开定时器
 *             （避免页面销毁时的注销时序问题）。
 */
void HsfHomePage_refresh(XWidget* page);

#endif /* XWIDGET_ON && XPUSHBUTTON_ON && XLABEL_ON */

#ifdef __cplusplus
}
#endif

#endif /* HSFHOMEPAGE_H */
