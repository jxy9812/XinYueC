/**
 * @file       HsfServerIoSim.h
 * @brief      服务端模拟设备工厂：按现场站表装配进程内寄存器模型。
 * @details    [职责] 只回答一个问题：**这一路上有哪些从站、各自的寄存器地图
 *             是什么**。装配仍由各站模块自己完成（HsfServerBus / HsfServerIoVfd
 *             / HsfServerIoTempHum / …），本模块只提供模型与虚拟客户端。
 *
 *             [为什么单独一个模块] 模拟设备的"站表 + 模型参数"是**一套现场
 *             配置**（变频器最大频率 50Hz、温湿度初值 26.5℃、数字量 16 点），
 *             散落到 HsfServerIo.c 的装配分支里会让那份编排代码同时背负
 *             "怎么装"和"模拟成什么样"两件事。抽出来之后：
 *               · 编排层只有一个 if (sim) 选客户端类型；
 *               · 要改模拟现场（比如加一路浊度仪）只动本模块。
 *
 *             [模型所有权] 本模块持有全部模型对象，生命周期覆盖整个服务端
 *             运行期（模型被虚拟客户端**借用**）。因此提供显式的
 *             HsfServerIoSim_destroy，由 HsfServerIo_stop 在**客户端全部释放
 *             之后**调用——顺序反了就是悬垂。
 *
 *             线程模型：单线程事件循环；全部 API 须在 XCoreApplication
 *             事件循环内调用。
 */
#ifndef HSFSERVERIOSIM_H
#define HSFSERVERIOSIM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#include <stdbool.h>
#include <stdint.h>

#include "HsfServerConfig.h"

struct XModbusVirtualClient;
struct XVirtualModelVfd;
struct XVirtualModelDigital;
struct XVirtualModelAnalog;
struct XVirtualModelTempHum;
struct XVirtualModelFlow;
struct XVirtualModelTurbidity;

/**
 * @brief 模拟现场的模型集合（单例内部持有；外部只取借用指针）。
 */
typedef struct HsfServerIoSimModels {
    struct XVirtualModelVfd* m_vfd;            /**< 变频器（站 2）。 */
    struct XVirtualModelTempHum* m_tempHum;    /**< 温湿度（站 4）。 */
    struct XVirtualModelFlow* m_flowMeter;     /**< 流量计（站 5）。 */
    /** @brief 浊度仪 ×2（站 1 / 站 3；与流量计共用同一条 485）。 */
    struct XVirtualModelTurbidity* m_turbidity[2];
    struct XVirtualModelDigital* m_digMain;     /**< 数字量主模块（现场当前 = .13；逻辑点 0..15）。 */
    struct XVirtualModelDigital* m_digExt;      /**< 数字量扩展模块（仅配置了 HSF_DIG_HOST_EXT 时才挂载）。 */
    struct XVirtualModelAnalog* m_analog;      /**< 模拟量 8 路。 */
} HsfServerIoSimModels;

/**
 * @brief 建立全部模拟模型（幂等；重复调用返回已有集合）。
 * @return 模型集合（借用）；失败返回 NULL。
 */
const HsfServerIoSimModels* HsfServerIoSim_ensure(void);

/**
 * @brief 模拟心跳拍：推进全部时间驱动的模型状态。
 * @details [为什么需要] 模型的时间状态（变频器爬坡、流量计波动与累积量）
 *          若挂在"被读"上推进，加速/波动节奏会随轮询节拍漂移，且链路停了
 *          模拟现场就"冻结"——现场不会因为上位机不在读就静止。本拍由
 *          HsfServerApp 的 100ms 基准节拍（hsfAppTick）驱动，与 Python 侧
 *          App.interval_ms=100 的 on_tick 同构。阀行程/浊度波动内部本就按
 *          单调钟计算，无需在此推进。
 * @note  未装配（非 --sim 或未 ensure）时为空操作。须在事件循环内调用。
 */
void HsfServerIoSim_tick(void);

/**
 * @brief 覆盖模拟阀的行程参数（须在 HsfServerIoSim_ensure 之前调用）。
 * @param moveMs 行程毫秒；0=缺省（10000）；UINT32_MAX=阀卡死。
 * @param noFeedback true=行程开关未接线（DI 恒 0）。
 * @details 对齐 Python --sim-valve-move / --sim-valve-no-feedback 故障注入；
 *          只作用于**承载阀的那台**模型（按 HSF_VALVE_DO 宏判定，当前现场
 *          = 主模块）。幂等：ensure 前随时可改。
 */
void HsfServerIoSim_setValveOverride(uint32_t moveMs, bool noFeedback);

/**
 * @brief 释放全部模拟模型（幂等）。
 * @warning 必须在所有虚拟客户端（即各站 Hub）**释放之后**调用。
 */
void HsfServerIoSim_destroy(void);

/**
 * @brief 建一条 485 路的虚拟客户端，并按该路挂哪些站绑定从站模型。
 * @param hasVfd 是否挂变频器（绑定站 2）。
 * @param hasTempHum 是否挂温湿度（绑定站 4）。
 * @param hasFlowMeter 是否挂流量计（绑定站 5）。
 * @param hasTurbidity 是否挂浊度仪两台（绑定站 1 / 站 3）。
 * @return 虚拟客户端（所有权交给调用方/Hub）；失败返回 NULL。
 * @details [多从站] 现场路 A 是"变频器 + 温湿度"共用一条线，靠站号区分。
 *          虚拟客户端没有物理总线，必须显式 bindUnit 才能让两个站各自读到
 *          自己的寄存器——否则都会落到同一个缺省模型上（变频器读到温度字）。
 */
struct XModbusVirtualClient* HsfServerIoSim_createBusClient(bool hasVfd,
                                                            bool hasTempHum,
                                                            bool hasFlowMeter,
                                                            bool hasTurbidity);

/**
 * @brief 建数字量站的虚拟客户端（主模块 = 现场 HSF_DIG_HOST 那台）。
 * @return 虚拟客户端；失败返回 NULL。
 */
struct XModbusVirtualClient* HsfServerIoSim_createDigitalMain(void);

/**
 * @brief 建数字量扩展模块（HSF_DIG_HOST_EXT 那台）的虚拟客户端。
 * @return 虚拟客户端；失败返回 NULL。
 * @note  扩展模块在不在由调用方决定要不要建（不建 = 单台 16 点，与真实
 *        装配的降级路径同一条）。当前现场未配置扩展模块 ⇒ 不用本函数。
 */
struct XModbusVirtualClient* HsfServerIoSim_createDigitalExt(void);

/**
 * @brief 建模拟量站的虚拟客户端。
 * @return 虚拟客户端；失败返回 NULL。
 */
struct XModbusVirtualClient* HsfServerIoSim_createAnalog(void);

#ifdef __cplusplus
}
#endif
#endif /* HSFSERVERIOSIM_H */
