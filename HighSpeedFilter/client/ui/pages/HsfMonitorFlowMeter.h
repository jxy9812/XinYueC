/**
 * @file       HsfMonitorFlowMeter.h
 * @brief      电磁流量计监视页：状态横幅 + 主读数 + 弧形仪表 + 数值卡 + 趋势。
 * @details    与变频器页（HsfMonitorVfd）同级的叶子页，复用同一套布局骨架与
 *             配色纪律，但**只读无控制**——流量计没有可下发动作，故没有
 *             操作行与控制闸（设备语义的自然结果）。
 *
 *             数据全部经 HsfFlowMeterProvider 函数表，页面不认 XLink/Modbus。
 */
#ifndef HSFMONITORFLOWMETER_H
#define HSFMONITORFLOWMETER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#include "XWidget.h"
#include "HsfPages.h"

#if XWIDGET_ON && XPUSHBUTTON_ON && XLABEL_ON

/**
 * @brief      创建电磁流量计叶子页（堆分配，挂 parent，随主窗级联释放）。
 * @param parent 父控件（堆栈宿主）。
 * @param navFn 导航回调（返回钮用）；可 NULL。
 * @param userData 透传回调的用户数据。
 * @return 页根控件借用指针；失败 NULL。
 */
XWidget* HsfMonitorFlowMeter_create(XWidget* parent, HsfMenuNavFn navFn,
                                    void* userData);

/**
 * @brief      形态自适应（横屏仪表横排/竖屏纵排）。
 * @param page 页根控件（HsfMonitorFlowMeter_create 返回值）。
 * @param compact true=窄屏/竖屏形态。
 * @note       由 HsfUi 帧泵每循环调用：必须幂等、廉价（只改布局参数）。
 */
void HsfMonitorFlowMeter_adapt(XWidget* page, bool compact);

/* ==================== 水质块（浊度仪 ×2 + 进水泵） ==================== */

/**
 * @brief 设某台浊度仪读数（index 从 0 起；online=false 显示 "--"）。
 * @param ntu 浊度值（NTU 原值）；≥20 NTU 由页面标红。
 */
void HsfMonitorFlowMeter_setTurbidity(int index, const char* name,
                                      double ntu, bool online);

/**
 * @brief 设进水泵状态（**命令状态**：现场无运行反馈点）。
 * @param on 输出是否已开；ready 输出是否可控；text 说明（可空）。
 */
void HsfMonitorFlowMeter_setPump(const char* name, bool on, bool ready,
                                  const char* text);

/**
 * @brief 注册泵控制出口（on=true 启动 / false 停止）。
 */
void HsfMonitorFlowMeter_setPumpWriteHandler(void (*fn)(void*, bool),
                                             void* userData);

/**
 * @brief 控制闸：整体启停泵按钮（对齐阀页 HsfMonitorValve_setControlEnabled）。
 * @param enabled false 时两个按钮一律置灰（不看 ready/on）。
 * @param blockedReason 置灰原因（页上常驻说明用；NULL/空=不显示）。
 * @details [为什么需要这道闸] 服务端 `hsfAppControlGate`
 *          （HsfServerApp.c:1560）会在**自动模式**拒绝一切设备控制，回
 *          "自动运行中：远程控制已被禁用"。客户端原先只有阀页提前置灰、
 *          泵页没接这道闸 ⇒ 自动模式下泵按钮仍是实心可点，用户点了才被
 *          服务端拒（截图实测：顶栏"模式: 自动"，泵"启动"仍可点）。
 *          置灰判据只看外部条件（连接/模式/权限）；点位 ready 仍由
 *          HsfMonitorFlowMeter_setPump 内部与闸门求交。
 */
void HsfMonitorFlowMeter_setControlEnabled(bool enabled,
                                           const char* blockedReason);

#endif /* XWIDGET_ON && XPUSHBUTTON_ON && XLABEL_ON */

#ifdef __cplusplus
}
#endif
#endif /* HSFMONITORFLOWMETER_H */
