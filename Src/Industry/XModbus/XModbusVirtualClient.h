/**
 * @file       XModbusVirtualClient.h
 * @brief      虚拟 Modbus 客户端：进程内应答，**不打开任何串口/套接字**。
 * @details    [这个模块解决什么]
 *              Python 侧有 `hsfsrv/virtualdev.py`，把「设备物理层」换成内存
 *              寄存器模型，业务层（换算/状态字/故障表/写命令字）全用真实实现，
 *              于是设备拆了也能联调、CI 里也能跑。C 侧此前没有等价物：
 *              想联调只能接真设备，或者起一个 TCP 从站（依赖网络栈与环回）。
 *
 *              本类把 Modbus 主站的**传输层**替换为进程内应答：
 *
 *                  真实：设备站 ──> XModbusTcpClient/RtuSerialClient ──> 串口/TCP
 *                  本类：设备站 ──> XModbusVirtualClient ──> 进程内寄存器模型
 *
 *              上层（HsfServerIo*、XModbusHub、XModbusLink、设备站）**一行不改**：
 *              它们只认 XModbusClient* 与 XModbusHub，本类就继承 XModbusClient
 *              并重载 Open/Close/SendRawRequest。于是：
 *                · Hub 的缓存块/读组/写合并/失败重发全部照跑；
 *                · 链路层的 role 映射、通道组标度、设备层判读全部照跑；
 *                · 电磁阀的"离线→报错"、超时告警、新鲜度判定全部照跑。
 *              **变的只有"电信号从哪来"。**
 *
 *             [怎么接] 装配时按开关选择客户端类型：
 *
 *                  XModbusClient* c = sim
 *                      ? (XModbusClient*)XModbusVirtualClient_create()
 *                      : (XModbusClient*)XModbusTcpClient_create();
 *                  // 之后 XModbusHub_addDevice_2 的用法完全一致。
 *
 *             [模拟哪些设备] 由 XModbusVirtualModel 描述，见
 *             XModbusVirtualModel.h 的各设备模型（M512 变频器 / MT3A 数字量 /
 *             中盛模拟量 / SHT30 温湿度 / LD 流量计）。模型只实现"寄存器读写字"，
 *             不含任何业务语义——业务语义在真实设备类里，只有一份。
 *
 *             [与"起一个 TCP 从站"的区别]
 *               · 不起端口、不占 502、不依赖环回（Windows 环回有停滞问题，
 *                 见 verify/windows_loopback_stall_repro.c）；
 *               · 不需要端口冲突管理，多个站可各用各的模型并存；
 *               · 但**测不到传输层**（帧格式/超时/分片）。要测传输层请用
 *                 XModbusTcpServer（现有测试即如此）。
 *
 *             线程模型：与框架一致单线程事件循环。应答在本函数内**同步**
 *             完成（Reply 直接置 Finished），故不引入定时器、不占用事件循环。
 */
#ifndef XMODBUSVIRTUALCLIENT_H
#define XMODBUSVIRTUALCLIENT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#include "XModbusClient.h"
#include <stdbool.h>
#include <stdint.h>

#if XMODBUS_ON && XMODBUS_CLIENT_ON

struct XModbusVirtualModel;

/** @brief 一条 485 上可挂的从站模型槽数上限。 */
#define XMODBUSVIRTUAL_UNIT_CAP 16

/**
 * @brief 「长帧」应答的数据区下限（字节）。
 * @details 站 5 流量计 22 寄存器一帧 = 44 字节数据 + 5 字节帧头尾 = 49 字节
 *          （现场唯一的长帧）；普通读（站 1/2 浊度 2~4 寄存器）数据区只有
 *          4~8 字节。取 40 字节（20 寄存器）为界，两边都干净分开。
 */
#define XMODBUSVIRTUAL_LONG_FRAME_MIN_BYTES 40

XCLASS_DEFINE_BEGING(XModbusVirtualClient)
XCLASS_DEFINE_EXTEND_END(XModbusVirtualClient, XModbusClient)

/**
 * @brief 虚拟 Modbus 客户端（进程内应答，无真实传输）。
 */
typedef struct XModbusVirtualClient {
    XModbusClient m_base;                     /**< 基类（第一成员）。 */
    struct XModbusVirtualModel* m_model;      /**< 缺省模型（借用，不拥有）。 */
    /**
     * @brief 按从站号分派的模型表（借用，不拥有）。
     * @details [为什么必须支持多从站] 一条 485 上同时挂多个从站是常态：
     *          现场路 A 就是变频器（站 2）+ 温湿度（站 4）共用一条线。
     *          真实 RTU 靠帧里的从站地址做总线仲裁，进程内应答没有总线，
     *          只能按请求里的 serverAddress 显式查表——否则两个站会读到
     *          **同一个**寄存器模型（变频器读到温湿度的温度字，且不报错）。
     *          表空时回落到 m_model（单站场景的既有用法不受影响）。
     */
    struct XModbusVirtualModel* m_units[XMODBUSVIRTUAL_UNIT_CAP];
    /**
     * @brief 模拟"无应答"的概率（0~1；0=恒应答）。
     * @details [为什么需要] 现场实测该数字量模块会间歇性空应答（无间隔连发
     *          10 次里 1~2 次收到 0 字节）。要验证"单次空应答不该被判成设备
     *          失联"，就必须能确定性地注入这种抖动——真设备上做不到可控复现。
     */
    double m_dropRate;
    /**
     * @brief 是否强制"离线"（true ⇒ Open 失败，模拟模块不在线）。
     * @details 用来验证降级/报错路径：`.13` 掉线时电磁阀必须报"无法控制"，
     *          而不是伪装成"无到位反馈"。真设备上只能靠拔网线，不可重复。
     */
    bool m_forceOffline;
    /** @brief 已应答的请求计数（诊断/断言用）。 */
    uint64_t m_requestCount;
    /** @brief 已丢弃（模拟空应答）的请求计数。 */
    uint64_t m_dropCount;
    /**
     * @brief 各从站的应答时延下限（毫秒；下标 = 从站号，0 = 不加时延）。
     * @details [2026-11-09 ttyS4 实测] 真实仪表应答不是零时延：站 1
     *          37~43ms、站 2 31~33ms、站 5 68~69ms（docs/HSF板端ttyS4三设备
     *          取数实测总表 §1.1「响应时延」列）。虚拟从站在**应答路径**
     *          里按站号休眠区间内随机值，主站等待 semantics 与真串口一致。
     */
    int m_latencyLoMs[XMODBUSVIRTUAL_UNIT_CAP];
    /** @brief 各从站的应答时延上限（毫秒；与 loMs 成对）。 */
    int m_latencyHiMs[XMODBUSVIRTUAL_UNIT_CAP];
    /**
     * @brief 「长帧从站」站号（-1 = 恢复期建模关闭）。
     * @details 站 5 流量计一帧读 22 寄存器 = 49 字节长帧（现场唯一的长帧）。
     */
    int m_longFrameUnit;
    /** @brief 长帧后恢复期内**无应答**的受影响站号（现场 = 站 1 浊度仪）。 */
    int m_blockedUnit;
    /** @brief 恢复期窗口（毫秒；实测 ≥100~125ms 才恢复，0 = 关闭闸）。 */
    int m_recoveryWindowMs;
    /** @brief 最近一次长帧应答收口时刻（epoch 毫秒；0 = 还没收口过）。 */
    int64_t m_longFrameEndMs;
    /** @brief 因恢复期被拒（无应答）的请求数（诊断/断言用）。 */
    uint64_t m_recoveryDropCount;
} XModbusVirtualClient;

/* ==================== 类初始化 / 生命周期 ==================== */

/**
 * @brief 初始化虚函数表（单例）。
 */
XVtable* XModbusVirtualClient_class_init(void);

/**
 * @brief 堆上创建虚拟客户端。
 * @param model 寄存器模型（**借用**，调用方保证生命周期覆盖本客户端）。
 * @return 新客户端；失败返回 NULL。
 * @note 模型不随客户端释放——同一个模型可挂多个客户端（多站共用一个
 *       物理设备的情形），生命周期由装配方统一管理。
 */
XModbusVirtualClient* XModbusVirtualClient_create_ex(XMemoryType memory,
                                                    struct XModbusVirtualModel* model);
#define XModbusVirtualClient_create(model) \
    XModbusVirtualClient_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (model))

/**
 * @brief 栈上初始化虚拟客户端。
 * @param client 目标对象；不能为 NULL。
 * @param model 寄存器模型（借用；可为 NULL，此时所有读回异常应答）。
 */
void XModbusVirtualClient_init(XModbusVirtualClient* client,
                               struct XModbusVirtualModel* model);

/* ==================== 模拟开关 ==================== */

/**
 * @brief 设置"空应答"概率（0~1；超出范围夹紧）。
 * @param client 目标客户端。
 * @param rate 概率。
 */
void XModbusVirtualClient_setDropRate(XModbusVirtualClient* client, double rate);

/**
 * @brief 强制在线/离线（离线时 Open 失败，模拟模块不在线）。
 * @param client 目标客户端。
 * @param offline true=离线。
 */
void XModbusVirtualClient_setOffline(XModbusVirtualClient* client, bool offline);

/**
 * @brief 设某从站的应答时延区间（毫秒；应答路径内按站号休眠）。
 * @param client 目标客户端。
 * @param unitId 从站号（0..XMODBUSVIRTUAL_UNIT_CAP-1；越界忽略）。
 * @param loMs 下限（<0 夹 0；lo>hi 时取 lo=hi）。
 * @param hiMs 上限。
 * @details [时延放在从站应答路径，不放主站 sleep] 真实 RTU 里主站发出请求
 *          后**等待**从站应答，等待时长由仪表决定；虚拟链路里等价位置就是
 *          虚拟客户端构造应答之前。缺省 0（不时延）——既有测试的零时延
 *          口径一字不改；模拟装配处按 2026-11-09 实测口径配（站 1 37~43、
 *          站 2 31~33、站 5 68~69，宏见 HsfServerConfig.h 的 HSF_SIM_*）。
 */
void XModbusVirtualClient_setUnitReplyLatency(struct XModbusVirtualClient* client,
                                              int unitId, int loMs, int hiMs);

/**
 * @brief 取某从站的应答时延区间（诊断/断言用）。
 * @return true=取到（outLoMs/outHiMs 非空时写入）；未配置返回 false。
 */
bool XModbusVirtualClient_unitReplyLatency(
    const struct XModbusVirtualClient* client, int unitId,
    int* outLoMs, int* outHiMs);

/**
 * @brief 建「长帧后恢复期」闸：长帧从站收口后窗口内，受影响站无应答。
 * @param client 目标客户端。
 * @param longFrameUnit 发 49 字节级长帧的从站号（现场 = 站 5 流量计）；
 *                      <0 = 关闭建模。
 * @param blockedUnit 窗口内对任何请求都无应答的从站号（现场 = 站 1 浊度仪）。
 * @param windowMs 恢复期窗口毫秒（实测 ≥100~125ms；0 = 关闭闸）。
 * @details [为什么建模] 站 5 的长帧读完后**立刻**问站 1 → 几乎零应答
 *          （0ms 间隙 2/10、75ms 0/10、125ms 起 10/10，实测文档 §8.3）——
 *          主站的「站间 200ms 间隙 / 发帧前静默闸」正是为规避它存在的。
 *          模拟它，主站的间隙/补齐逻辑才能在长稳里被真实锻炼。
 *          长帧判据：长帧站读寄存器成功且数据 ≥ XMODBUSVIRTUAL_LONG_FRAME_
 *          MIN_BYTES 字节（22 寄存器 = 44 字节数据 ⇒ 49 字节帧）。
 */
void XModbusVirtualClient_setRecoveryGate(struct XModbusVirtualClient* client,
                                          int longFrameUnit, int blockedUnit,
                                          int windowMs);

/** @brief 恢复期窗口毫秒（诊断/断言用；0 = 未建模）。 */
int XModbusVirtualClient_recoveryWindowMs(
    const struct XModbusVirtualClient* client);

/** @brief 因恢复期被拒（无应答）的请求数（诊断/断言用）。 */
uint64_t XModbusVirtualClient_recoveryDropCount(
    const struct XModbusVirtualClient* client);

/**
 * @brief 绑定某个从站号的寄存器模型（多从站共一条链路时使用）。
 * @param client 目标客户端。
 * @param unitId 从站号（0..247）。
 * @param model 该从站的模型（**借用**；NULL=解除绑定）。
 * @return 绑定成功返回 true；unitId 越界或表满返回 false。
 * @note  分派规则：请求里的 serverAddress 命中绑定表就路由到该模型；未命中
 *        则回落到 XModbusVirtualClient_create 传入的缺省模型。因此单站用法
 *        （只需 create(model)）完全不受影响。
 */
bool XModbusVirtualClient_bindUnit(struct XModbusVirtualClient* client,
                                   int unitId,
                                   struct XModbusVirtualModel* model);

/**
 * @brief 取某从站号绑定的模型（借用）。
 * @return 模型指针；未绑定返回 NULL。
 */
struct XModbusVirtualModel* XModbusVirtualClient_unitModel(
    const struct XModbusVirtualClient* client, int unitId);

/** @brief 已应答请求数。 */
uint64_t XModbusVirtualClient_requestCount(const XModbusVirtualClient* client);
/** @brief 已丢弃请求数（模拟空应答）。 */
uint64_t XModbusVirtualClient_dropCount(const XModbusVirtualClient* client);
/** @brief 建模对象（借用）。 */
struct XModbusVirtualModel* XModbusVirtualClient_model(
    const XModbusVirtualClient* client);

#endif /* XMODBUS_ON && XMODBUS_CLIENT_ON */

#ifdef __cplusplus
}
#endif
#endif /* XMODBUSVIRTUALCLIENT_H */
