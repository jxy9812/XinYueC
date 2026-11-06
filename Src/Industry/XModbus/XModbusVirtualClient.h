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
