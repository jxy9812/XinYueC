/**
 * @file       HsfMonitorValve.h
 * @brief      电磁阀监视/操作页：6 路阀卡片式看板 + 控制闸。
 * @details    与 Python 版 pages_valve.py 同口径：每路一张卡片
 *             （状态点 + 阀名 + 状态徽标 + 开到位/关到位反馈 + 开阀/关阀
 *              两钮 + 提示行），宽屏 3 列 × 2 行、窄屏 1 列。
 *
 *             [数据边界] 页面只认"6 路阀的状态"（服务端算好给的），
 *             不知道也不该知道哪路阀接在哪个 DO/DI、是单电控还是双电控、
 *             反馈是常开还是常闭——这些全在服务端配置里。接线一改，服务端
 *             改配置，本页零改动。
 *
 *             [控制闸] 与变频器/数字量同一套：自动模式拒绝、需 root、
 *             未配置点位不可操作。按钮禁用时在页面顶部说明原因，而不是
 *             让用户点了没反应。
 *
 *             [喂数据] 本页不直接依赖 HsfLinkClient（便于单测与离线演示）：
 *             由主窗在链路回调里调 HsfMonitorValve_applySnapshot 灌入，
 *             与控制经 HsfMonitorValve_setWriteHandler 出的口子对接。
 */
#ifndef HSFMONITORVALVE_H
#define HSFMONITORVALVE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#include "XWidget.h"
#include "HsfPages.h"

#if XWIDGET_ON && XPUSHBUTTON_ON && XLABEL_ON

/**
 * @brief      创建电磁阀页（堆分配，挂 parent，随主窗级联释放）。
 * @param parent 父控件（堆栈宿主）。
 * @param navFn 导航回调（保留）。
 * @param userData 透传回调的用户数据。
 * @return 页根控件借用指针；失败 NULL。
 */
XWidget* HsfMonitorValve_create(XWidget* parent, HsfMenuNavFn navFn,
                                void* userData);

/**
 * @brief 形态自适应（宽屏 3 列 / 窄屏 1 列；横幅与卡片边距随形态收敛）。
 * @param page 页根控件。
 * @param compact true=紧凑/竖屏形态。
 */
void HsfMonitorValve_adapt(XWidget* page, bool compact);

/* ==================== 数据喂入（网络层/测试用） ==================== */

/** @brief 路数（0..6；变化时重建卡片）。 */
void HsfMonitorValve_setCount(int count);

/** @brief 单路姓名（UTF-8；内部深拷贝；NULL=缺省"电磁阀N"）。 */
void HsfMonitorValve_setName(int index, const char* name);

/** @brief 单路到位反馈（openFb=开到位、closeFb=关到位）。 */
void HsfMonitorValve_setFeedback(int index, bool openFb, bool closeFb);

/** @brief 单路阀位状态（HsfValveState：0未知 1已关 2已开 3动作中 4故障）。 */
void HsfMonitorValve_setState(int index, int state);

/** @brief 单路点位是否配置齐全。 */
void HsfMonitorValve_setReady(int index, bool ready);

/** @brief 单路数据有效性。 */
void HsfMonitorValve_setValid(int index, bool valid);

/** @brief 单路提示文本（故障原因/排查提示；NULL/空=不显示）。 */
void HsfMonitorValve_setText(int index, const char* text);

/**
 * @brief 整帧喂入后一次性刷新（推荐路径：网络回调里逐路 set 完调本函数，
 *        避免逐字段各触发一次版面重排）。
 */
void HsfMonitorValve_refresh(void);

/* ==================== 控制闸 ==================== */

/**
 * @brief 设"是否允许操作"（自动模式/未连接/权限不足时置 false）。
 * @param blockedReason 非空时顶部横幅显示该原因（UTF-8）；
 *                      NULL/空串表示允许，横幅按"未配置路数"提示或隐藏。
 */
void HsfMonitorValve_setControlEnabled(bool enabled, const char* blockedReason);

/** @brief 当前是否允许操作。 */
bool HsfMonitorValve_controlEnabled(void);

/* ==================== 控制出口 / 状态栏 ==================== */

/**
 * @brief 操作请求回调。
 * @param userData 注册令牌；index 路序；wantOpen true=开阀。
 */
typedef void (*HsfValveWriteFn)(void* userData, int index, bool wantOpen);

/** @brief 注册操作回调（NULL=按钮禁用时不发；有闸但无出口时按钮仍可点）。 */
void HsfMonitorValve_setWriteHandler(HsfValveWriteFn fn, void* userData);

/** @brief 状态栏反馈接缝（NULL=不反馈）。 */
void HsfMonitorValve_attachStatus(void (*setStatus)(void*, const char*),
                                  void* userData);

/** @brief 最近一次操作被拒的原因（借用；空串=无）。 */
const char* HsfMonitorValve_lastError(void);

#endif /* XWIDGET_ON && XPUSHBUTTON_ON && XLABEL_ON */

#ifdef __cplusplus
}
#endif
#endif /* HSFMONITORVALVE_H */
