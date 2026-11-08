
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
 *             [注入客户端自愈] 走 HsfServerBus_openWithClient 的链路（模拟/
 *             单测注入的 XModbusVirtualClient 等）带一套**服务端看门狗**
 *             （HsfServerBusHeal）：虚拟客户端没有"连接"这回事，底层也没有
 *             任何重连实现，一旦被判失联就再也回不来（现场实测：长时间运行
 *             + 万次连接压测后温湿度/流量计恒陈旧，重启服务端才恢复）。
 *             看门狗按"设备状态 + 最后一次成功应答时刻"两个可观测量做判据，
 *             失联则重连、在线但数据停滞则重挂轮询定时器，全部带退避与
 *             限频日志。**物理串口路径（HsfServerBus_open）不启用**，行为不变。
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
 * @brief 注入客户端自愈看门狗状态（值语义；随宿主容器零初始化）。
 * @details [为什么需要] 模拟/单测链路注入的是进程内客户端（XModbusVirtualClient）
 *          或调用方自建的客户端。虚拟客户端的 Open/Close 只是改状态字，
 *          **没有任何重连实现**：某次状态被判成 Unconnected 之后，Hub 的
 *          泵会被 `state != Connected` 门槛永久挡住，读组再也不提交——而上层
 *          （温湿度/流量计/数字量）的陈旧判定只会把它显示成"无数据"，
 *          除了重启服务端没有第二条恢复路径。本结构就是那条路径。
 *
 *          判据全部是**可观测量**（不猜内部实现）：
 *            · 设备状态：XModbusHub_deviceState（Hub 侧镜像，含跃迁）；
 *            · 数据新鲜度：XModbusHub_deviceLastSuccessMs（最后一次成功应答）。
 *          因此它同样能兜住"设备仍报 Connected 但轮询定时器已停/被误杀"
 *          这类故障（定时器 id 复用误杀：id 是全局槽位，杀错即静默停摆）。
 *
 *          [为什么放在本文件] 485 两路 + 数字量站 + 模拟量站（四条注入链路）
 *          共用同一套策略；实现集中在这里，避免各处各写一份漂移。
 */
typedef struct HsfServerBusHeal {
    bool m_on;                 /**< 已启用（**仅注入/模拟路径**置 true）。 */
    bool m_lost;               /**< 当前判定"链路失联"（跃迁沿日志用）。 */
    bool m_stalled;            /**< 当前判定"在线但数据停滞"（同上）。 */
    int m_pollMs;              /**< 本链路轮询节拍（重挂定时器的参数）。 */
    uint32_t m_attempts;       /**< 连续自愈尝试次数（退避档位）。 */
    uint64_t m_armedMs;        /**< 启用时刻（"从未成功过"时的宽限基准）。 */
    uint64_t m_sinceMs;        /**< 本次异常起点毫秒（0=当前正常）。 */
    uint64_t m_nextTryMs;      /**< 下次允许动作的时刻（退避闸）。 */
    uint64_t m_lastLogMs;      /**< 上次"重试中"日志时刻（限频）。 */
    uint32_t m_suppressed;     /**< 上次日志以来被限频折叠掉的失败次数。 */
} HsfServerBusHeal;

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
    bool m_injected;                    /**< 客户端由 openWithClient 注入（模拟/单测）。 */
    HsfServerBusHeal m_heal;            /**< 注入客户端的自愈看门狗（物理串口不用）。 */
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

/* ==================== 注入客户端自愈看门狗 ==================== */
/*
 * [适用范围] 只对**注入/模拟**客户端启用（物理串口/物理 TCP 的装配入口
 * 不调 HsfServerBusHeal_arm，行为一字不变）。485 两路、数字量站、模拟量站
 * 共用同一套实现，判据与节奏完全一致，便于现场按同一条日志口径排查。
 */

/**
 * @brief 启用一条链路的自愈看门狗（在设备已挂 Hub 且轮询参数已知后调用）。
 * @param heal 看门狗状态（宿主容器成员；内部先清零）。
 * @param client 该链路客户端（借用；同时给它打开 autoReconnect）。
 * @param pollMs 轮询节拍（毫秒；重挂设备定时器用；<=0 按 200 处理）。
 * @param nowMs 当前单调毫秒（XDateTime_currentMSecsSinceEpoch）。
 *
 * @details [为什么同时开 autoReconnect] 注入的客户端不一定是虚拟客户端
 *          （单测里也注入 RTU/TCP 客户端）：那两类自带 attemptReconnect 实现，
 *          开 autoReconnect 让底层先自己重试，服务端看门狗只做兜底。
 *          对 XModbusVirtualClient 而言该开关**是空操作**（它没有重连实现），
 *          所以真正的自愈只能由本看门狗提供——这正是本模块存在的原因。
 */
void HsfServerBusHeal_arm(HsfServerBusHeal* heal, struct XModbusClient* client,
                          int pollMs, uint64_t nowMs);

/**
 * @brief 跑一拍自愈检查（由服务端 tick 每拍调用；内部自带退避与限频）。
 * @param heal 看门狗状态；未启用（m_on=false）时立即返回 false。
 * @param hub 该链路 Hub（借用）。
 * @param devId 该链路设备句柄（Hub 内）。
 * @param client 该链路客户端（借用；仅用于 connectDevice/disconnectDevice）。
 * @param name 日志显示名（静态常量借用；如 "485-A"）。
 * @param nowMs 当前单调毫秒。
 * @return 本拍是否执行了自愈动作（重连/重挂定时器/重启轮询）。
 *
 * @details [动作阶梯] 由轻到重，全部幂等：
 *            ① Hub 轮询总闸不在 ⇒ startPolling（最便宜，无副作用）；
 *            ② 设备非 Connected ⇒ connectDevice（带指数退避）；
 *            ③ 设备 Connected 但最后一次成功应答超过停滞阈值 ⇒
 *               重挂该设备的轮询定时器（setPollInterval 内部只 kill+start
 *               定时器，**不动块表、不动在途事务**，因此不会与在途应答的
 *               块下标回填打架）；
 *            ④ 连续失败 4 次 ⇒ 断链再连：强制一次状态跃迁，让 Hub 的
 *               "上线续泵/补写"路径重跑。
 */
bool HsfServerBusHeal_step(HsfServerBusHeal* heal, struct XModbusHub* hub,
                           int devId, struct XModbusClient* client,
                           const char* name, uint64_t nowMs);

/**
 * @brief 关闭看门狗并清状态（拆除链路时调用；幂等）。
 */
void HsfServerBusHeal_disarm(HsfServerBusHeal* heal);

/**
 * @brief 本路总线的自愈拍（仅 openWithClient 注入的链路有效）。
 * @param bus 目标总线；未打开/物理串口/未启用时为空操作。
 * @param nowMs 当前单调毫秒。
 * @return 本拍是否执行了自愈动作。
 */
bool HsfServerBus_watchdog(HsfServerBus* bus, uint64_t nowMs);

#ifdef __cplusplus
}
#endif
#endif /* HSFSERVERBUS_H */
