
/**
 * @file       HsfServerBus.h
 * @brief      服务端 485 总线容器：一路串口 = 一个 Hub + 一条 RTU 客户端。
 * @details    现场两路 485 各自独立采集：每路是**物理上不同**的串口，
 *             必须各自持有一个 XModbusHub——Hub 内部按「统一虚拟地址
 *             空间」编址，两路若共用一个 Hub，两边的输入寄存器 0x0000 段
 *             会直接撞车（XModbusMapping 拒绝重叠段），且单飞轮询会互相
 *             排队。
 *
 *             [本模块职责]
 *             只做「一条 485 物理链路」的建、启、停、查：
 *               - 建：创建 Hub + RTU 客户端 + 串口参数 + 超时/重试；
 *               - 启：Hub 接映射表 → 放开轮询区 → 设节拍 →
 *                 connectAll → startPolling；
 *               - 停：停轮询 → 断开 → 释放 Hub（客户端所有权在 Hub）。
 *             它**不认任何设备语义**：变频器/温湿度/浊度仪都由上层模块
 *             往这个 Hub 里挂映射段与通道组（见 HsfServerIoTempHum 等）。
 *
 *             [虚拟地址空间隔离] 每路一个 Hub ⇒ 每路一套 0..0xFFFF 独立
 *             地址空间，两路的 0x0000 各不相干。因此同一型号设备在 A/B
 *             两路的装配参数可以完全镜像（同站号、同远端地址）。
 *
 *             [失败容忍] 串口打不开（适配器没插/被占用）不阻断启动：
 *             本模块报错并保持「未打开」状态，其余总线照常工作；插回线后
 *             重启服务端即可（不做热插拔重连，与既有单路实现口径一致）。
 *
 *             线程模型：与框架一致单线程事件循环；全部 API 须在
 *             XCoreApplication 事件循环内调用。
 */
#ifndef HSFSERVERBUS_H
#define HSFSERVERBUS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#include <stdbool.h>
#include <stdint.h>

struct XModbusHub;
struct XModbusMapping;
struct XModbusRtuSerialClient;
/* 函数原型 HsfServerBus_openWithClient 用 struct XModbusClient*：缺此前向
 * 声明时该 tag 的作用域仅限原型（prototype scope），与实现侧 include
 * XModbusClient.h 后的全局 typedef 类型冲突（conflicting types，挡住
 * x86/armel 全量构建）。提到文件作用域即与 HsfServerIoAnalog/Digital
 * 头的同款惯例一致。 */
struct XModbusClient;

/**
 * @brief 一条 485 总线的运行态（值语义容器；由调用方持有）。
 * @details 全部成员为借用/句柄，生命周期由 HsfServerBus_* 管理；
 *          调用方只经 API 读取，不要直接改。
 */
typedef struct HsfServerBus {
    const char* m_name;                 /**< 总线显示名（静态常量借用；如 "485-A"）。 */
    const char* m_port;                 /**< 串口名（静态常量借用；如 "COM17"）。 */
    struct XModbusHub* m_hub;           /**< Modbus 聚合设备（本路独占；NULL=未建）。 */
    struct XModbusMapping* m_map;       /**< 映射表（提交给 Hub 后置 NULL）。 */
    struct XModbusRtuSerialClient* m_client; /**< RTU 客户端（所有权在 Hub）。 */
    int m_devId;                        /**< 该串口在 Hub 里的设备句柄；<0=未挂。 */
    bool m_started;                     /**< 映射已提交且轮询已拉起。 */
} HsfServerBus;

/**
 * @brief 建一条总线（Hub + RTU 客户端 + 打开串口），尚不启动轮询。
 * @param bus 目标总线（调用方提供存储；内部先清零）。
 * @param name 显示名（静态常量借用）。
 * @param port 串口名（借用；如 "COM17"）；NULL 或空串返回 false。
 * @param baud 波特率（bps）。
 * @param timeoutMs 单帧请求超时（毫秒）。
 * @param retries 重试次数（>=1 才能冲掉滞留的错误帧）。
 * @return 串口成功打开并挂上 Hub 返回 true；失败返回 false（bus 已清理）。
 * @note 失败时不留半成品：Hub/客户端都已释放，bus->m_hub 为 NULL。
 *       串口打不开（适配器缺失）是最常见的失败，调用方据此整路跳过。
 */
bool HsfServerBus_open(HsfServerBus* bus, const char* name, const char* port,
                       int baud, int timeoutMs, int retries);

/**
 * @brief 建一条总线，但**传输层由调用方给**（模拟设备/单测入口）。
 * @param bus 目标总线（内部先清零）。
 * @param name 显示名（静态常量借用）。
 * @param client 总线客户端（**所有权转移给本路 Hub**；虚拟客户端也走这里）。
 * @param timeoutMs 单帧请求超时（毫秒）。
 * @param retries 重试次数。
 * @return 客户端挂上 Hub 返回 true；失败返回 false（客户端已释放）。
 *
 * @details [为什么需要这个入口] 与 HsfServerIoDigital_mountWithClient 同理：
 *          把"物理链路"抽成 XModbusClient* 之后，**同一条装配代码路径**
 *          既能接真实 RTU 串口也能接进程内模拟设备。若不提供此口，
 *          模拟模式下 485 两路会被整路跳过（HsfServerBus_open 必然因
 *          "串口打不开"失败），变频器/温湿度/流量计三站全部无法联调。
 *
 *          [多从站] 一条 485 上挂多个从站（路 A = 变频器站 2 + 温湿度站 4）
 *          时，调用方应在此前用 XModbusVirtualClient_bindUnit 按站号绑定
 *          各自的模型；真实 RTU 则由帧内从站地址在物理总线上仲裁。
 */
bool HsfServerBus_openWithClient(HsfServerBus* bus, const char* name,
                                 struct XModbusClient* client,
                                 int timeoutMs, int retries);

/**
 * @brief 取本路 Hub（借用；建映射段/挂通道组链路的都经它）。
 * @param bus 目标总线；NULL 或未建返回 NULL。
 */
struct XModbusHub* HsfServerBus_hub(const HsfServerBus* bus);

/**
 * @brief 新建一张空映射表供各站登记映射段。
 * @return 新映射表（调用方持有，start 时移交 Hub）；失败返回 NULL。
 * @note 各站模块的 mount 在 start 之前调用，届时本函数返回的表已在
 *       编排层创建（HsfServerIo 编排顺序：open → 各站 mount → start）。
 */
struct XModbusMapping* HsfServerBus_createMapping(const HsfServerBus* bus);

/**
 * @brief 本路 Hub 里该串口设备的句柄（建映射段用）。
 * @param bus 目标总线；未打开返回 -1。
 */
int HsfServerBus_deviceId(const HsfServerBus* bus);

/**
 * @brief 提交映射表并启动本路轮询。
 * @param bus 目标总线；未打开返回 false。
 * @param map 映射表（**所有权转移**给本路 Hub；无论成败调用方都不得再引用）。
 * @param pollMs 本路轮询节拍（毫秒/读组）。
 * @return 启动成功返回 true。
 * @note 轮询区放开四区全开（DI/Coils/IR/HR）——本服务端的映射表里只
 *       登记了真正要读的段，不需要按区裁剪；只写段由段标志排除。
 */
bool HsfServerBus_start(HsfServerBus* bus, struct XModbusMapping* map,
                        int pollMs);

/**
 * @brief 停一条总线并释放（幂等；之后可重新 open）。
 * @param bus 目标总线；可为未初始化。
 * @note 顺序：停轮询 → 断开全部设备 → 释放 Hub（客户端随 Hub 释放）。
 *       **映射段归属的通道组/链路必须先于本函数释放**（组持有链路、
 *       链路订阅 Hub 信号），否则链路析构会撞上已释放的 Hub。
 */
void HsfServerBus_stop(HsfServerBus* bus);

/**
 * @brief 本路串口是否已打开（诊断用）。
 */
bool HsfServerBus_isOpen(const HsfServerBus* bus);

#ifdef __cplusplus
}
#endif
#endif /* HSFSERVERBUS_H */
