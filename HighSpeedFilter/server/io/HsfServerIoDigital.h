/**
 * @file       HsfServerIoDigital.h
 * @brief      服务端数字量站：MT3A-IO1632（16DI/16DO，Modbus-TCP 从站）。
 * @details    与 485 两路总线无关，本模块自带一个 XModbusHub（TCP 客户端），
 *             只负责 16 路离散输入（FC02）+ 16 路线圈输出（FC01/FC05）的
 *             装配、位图读取与单点写入。
 *
 *             [点位口径·2026-11-05 真机实测]
 *               DI = FC02 @ 0x0000，16 点
 *               DO = FC01（读）/ FC05（写单点）@ 0x0000，16 点
 *               起始地址 ≥ 0x0010、或起始 + 点数跨出 16 ⇒ **一律 EXC02**。
 *               故本站的 startAddr 只能是 0，channels ≤ 16。
 *
 *             [为什么整组写必须 FC05 逐点] 该模块对 FC0F（写多线圈）会
 *             回**两次相同应答**，且事务号恒 0 —— 第二次应答会被当成下一
 *             次请求的应答，造成后续帧全错位。单点 FC05 没有这个问题。
 *             （Python 侧 modbus.py 头注释记录同一条实锅。）
 *
 *             [应答分片] qty=16 时模块可能把 2 字节数据拆成两个 TCP 段返回
 *             （实测先回 0202 再补 00）。读路径必须按"期望字节数"凑满整帧，
 *             不能收到一段就判完。
 *
 *             线程模型：单线程事件循环；全部 API 须在 XCoreApplication
 *             事件循环内调用。
 */
#ifndef HSFSERVERIODIGITAL_H
#define HSFSERVERIODIGITAL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#include <stdbool.h>
#include <stdint.h>

#include "XObject.h"
#include "XhsfLinkProto.h"   /* HSF_DIGITAL_CHANNELS（点数上限）。 */

struct XModbusHub;
struct XModbusClient;
struct XModbusLink;
struct XIoChannelGroup;
struct XModbusVirtualModel;

/* ==================== 虚函数表定义（继承 XObject，不新增虚槽） ==================== */
XCLASS_DEFINE_BEGING(HsfServerIoDigital)
XCLASS_DEFINE_EXTEND_END(HsfServerIoDigital, XObject)

/**
 * @brief 数字量站运行态（本模块拥有其内部资源）。
 * @details 成员禁止直接访问，一律经 API 操作。
 *
 * @note  [角色号约定] 组内 role 空间按类型分段，与点类型一起唯一确定点：
 *          DI 通道 n → role = n              （0..31）
 *          DO 通道 n → role = 32 + n         （32..63）
 *        调用方只按"通道号 + 方向"操作，段基址由本模块翻译，外部不可见。
 */
typedef struct HsfServerIoDigital {
    XObject m_class;                    /**< 第一成员：基类 XObject。 */
    struct XModbusHub* m_hub;           /**< 本站独占 Hub（自有）。 */
    struct XModbusClient* m_client;     /**< 主模块(.12)客户端（所有权在 Hub；TCP 或虚拟）。 */
    struct XModbusClient* m_clientExt;  /**< 扩展模块(.13)客户端（所有权在 Hub；NULL=未装）。 */
    struct XIoChannelGroup* m_group;    /**< 通道组（自有）。 */
    struct XModbusLink* m_link;         /**< 链路（自有）。 */
    int m_devId;                        /**< 主模块设备句柄；<0=未挂。 */
    int m_devIdExt;                     /**< 扩展模块设备句柄；<0=未挂（=降级 16 点）。 */
    int m_channelCount;                 /**< 对外实际点数（16 或 32）。 */
    int m_channelsPerDev;               /**< 单台模块点数（每台 16）。 */
    int m_startAddr;                    /**< 远端起始地址（实测只能 0）。 */
    bool m_mounted;                     /**< 装配完成标志。 */
} HsfServerIoDigital;

/* ==================== 类初始化 / 构造与析构 ==================== */

/**
 * @brief 初始化 HsfServerIoDigital 类的虚函数表（单例）。
 * @return 类虚拟函数表指针；不可释放。
 */
XVtable* HsfServerIoDigital_class_init(void);

/**
 * @brief 栈上初始化数字量站（未装配）。
 * @param st 目标对象；不能为 NULL，可为未初始化内存（内部先清零）。
 */
void HsfServerIoDigital_init(HsfServerIoDigital* st);

/* ==================== 装配 ==================== */

/**
 * @brief 装配并启动数字量站（TCP 客户端 + 16DI/16DO 点 + 轮询）。
 * @param st 目标站；不能为 NULL。
 * @param host 模块 IP（借用）。
 * @param port TCP 端口（502）。
 * @param unitId 从站 Unit ID（实测 1）。
 * @param channels 点数（1..16；超出按 16 夹紧）。
 * @param startAddr 远端起始地址（实测只能 0；>0 且会越界时夹回 0）。
 * @param pollMs 轮询周期（毫秒；缺省 200）。
 * @param timeoutMs 单帧超时（毫秒；该模块应答 0.4~0.8s，缺省 2000）。
 * @return 装配成功返回 true。
 * @note TCP 连不上**不**阻断装配：Hub 自带重连，插回网线后自愈。
 *       只有内存/装配本身失败才返回 false。
 */
bool HsfServerIoDigital_mount(HsfServerIoDigital* st, const char* host,
                              int port, int unitId, int channels,
                              int startAddr, int pollMs, int timeoutMs);

/**
 * @brief 用**外部给的客户端**装配数字量站（模拟设备/单测入口）。
 * @param st 目标站。
 * @param client Modbus 客户端（所有权转移给 Hub）。
 * @param channels 点数（1..HSF_DIG_CHANNELS）。
 * @param startAddr 远端起始地址（实测只能 0）。
 * @param pollMs 轮询周期（毫秒）。
 * @param timeoutMs 单帧超时（毫秒）。
 * @return 装配成功返回 true。
 *
 * @details [为什么需要这个入口] 把"设备物理层"抽象出来的关键在于：站模块
 *          不该关心客户端背后是 TCP、RTU 还是进程内模拟。本接口直接接收
 *          一个 XModbusClient*（XModbusVirtualClient 也是它），于是
 *          **同一个装配代码路径**既能接真设备也能接模拟设备——测试与
 *          现场用的是同一份装配逻辑，不会出现"测试通过但现场装配不同"。
 *
 *          [与 mount 的区别] mount 内部自己造 TCP 客户端并按 .13 探测结果
 *          决定是否聚合成 32 点；本接口不探测、不建第二个客户端，点数与
 *          客户端完全由调用方决定（模拟场景由模型决定能力）。
 */
bool HsfServerIoDigital_mountWithClient(HsfServerIoDigital* st,
                                        struct XModbusClient* client,
                                        int channels, int startAddr,
                                        int pollMs, int timeoutMs);

/**
 * @brief 用外部给的**主 + 扩展**两个客户端装配（32 点聚合的模拟/单测入口）。
 * @param st 目标站。
 * @param client 主模块(.12)客户端（所有权转移给 Hub）。
 * @param extClient 扩展模块(.13)客户端（所有权转移给 Hub；NULL=不聚合）。
 * @param channels 对外总点数（<= HSF_DIGITAL_CHANNELS；未超单台能力时
 *                 extClient 会被忽略并与 mount 的降级口径一致）。
 * @param startAddr 远端起始地址（实测只能 0）。
 * @param pollMs 轮询周期（毫秒）。
 * @param timeoutMs 单帧超时（毫秒）。
 * @return 装配成功返回 true。
 *
 * @details [为什么需要它] 32 点是**两台模块聚合**出来的。没有这个入口，
 *          模拟模式只能装 16 点，而电磁阀的六路点位全部落在上半区
 *          （逻辑点 16..27）——那六路会一直报"未配置/离线"，模拟联调
 *          等于没覆盖到阀。
 */
bool HsfServerIoDigital_mountWithClients(HsfServerIoDigital* st,
                                        struct XModbusClient* client,
                                        struct XModbusClient* extClient,
                                        int channels, int startAddr,
                                        int pollMs, int timeoutMs);

/**
 * @brief 拆除并释放全部内部资源（幂等；之后可重新 mount）。
 */
void HsfServerIoDigital_unmount(HsfServerIoDigital* st);

/* ==================== 读写 ==================== */

/**
 * @brief 读全部 DI（位图）。
 * @param st 目标站；NULL/未装配返回 false。
 * @param outMask 输出位图（bit n = DI 通道 n；不能为 NULL）。
 * @param outValid 输出是否至少有一路有效（可为 NULL）。
 * @return 至少读到一路有效数据返回 true。
 */
bool HsfServerIoDigital_readDi(const HsfServerIoDigital* st, uint32_t* outMask,
                               bool* outValid);

/**
 * @brief 读全部 DO（位图，读回线圈当前值）。
 * @param st 目标站。
 * @param outMask 输出位图（bit n = DO 通道 n；不能为 NULL）。
 * @param outValid 输出是否至少有一路有效（可为 NULL）。
 * @return 至少读到一路有效数据返回 true。
 */
bool HsfServerIoDigital_readDo(const HsfServerIoDigital* st, uint32_t* outMask,
                               bool* outValid);

/**
 * @brief 写单路 DO（走 FC05 单点写；**绝不**用 FC0F 整组写）。
 * @param st 目标站。
 * @param channel DO 通道号（0..channels-1）。
 * @param on 目标值（true=线圈置位）。
 * @return 写意图已受理返回 true；通道越界/未装配/链路拒绝返回 false。
 * @note  受理 ≠ 落定；落定经链路 writeDone 冒泡（同一事件循环内很快到）。
 *        调用方需要确认时，稍后再 readDo 回读。
 */
bool HsfServerIoDigital_writeDo(HsfServerIoDigital* st, int channel, bool on);

/**
 * @brief 读单路 DI 的**新鲜度**（该点现在是否仍可信）。
 * @param st 目标站；NULL/未装配返回 false。
 * @param channel DI 通道号（0..channels-1）。
 * @return true=已读到值 且 链路判定未陈旧。
 *
 * @details [为什么需要单点新鲜度而不是整站 bool] 32 点是**两台模块聚合**
 *          出来的（.12 提供 0..15、.13 提供 16..31）。两台各自的在线状态
 *          互相独立：.13 掉线时 .12 仍然正常应答，整站层面"有数据"恒为真，
 *          但逻辑点 16..31 已不可信。电磁阀正好挂在这半区，若拿整站
 *          "有数据"当判据，就会得到"模块在线"的错误结论。
 */
bool HsfServerIoDigital_isDiFresh(const HsfServerIoDigital* st, int channel);

/**
 * @brief 读单路 DI（便捷；内部仍走组缓存，不额外发帧）。
 */
bool HsfServerIoDigital_readDiBit(const HsfServerIoDigital* st, int channel,
                                  bool* outOn);

/* ==================== 查询 ==================== */

/** @brief 是否装配完成。 */
bool HsfServerIoDigital_isMounted(const HsfServerIoDigital* st);

/** @brief 实际点数。 */
int HsfServerIoDigital_channelCount(const HsfServerIoDigital* st);

/** @brief 取站点 Hub（借用；抓帧/诊断用）。@warning 禁释放。 */
struct XModbusHub* HsfServerIoDigital_hub(const HsfServerIoDigital* st);

/**
 * @brief 取本站**模拟设备模型**（借用；仅当客户端是 XModbusVirtualClient 时非 NULL）。
 * @return 模型指针；真实设备或未装配返回 NULL。@warning 禁释放。
 * @details [用途] 模拟联调/自动化测试需要动态改模型的现场参数，例如把阀
 *          行程调短（不然一次开阀要等 10s）、模拟"行程开关没接线"、
 *          模拟"阀卡死"。这些是**模型的实验旋钮**，不是设备语义。
 * @note  通过 XModbusVirtualClient_model() 取；返回的是 .12 主模块的模型
 *        （32 点聚合时 .13 是另一份，需要时用 simModelExt）。
 */
struct XModbusVirtualModel* HsfServerIoDigital_simModel(
    const HsfServerIoDigital* st);

/** @brief 取扩展模块(.13)的模拟模型（借用）；未装扩展或非模拟返回 NULL。 */
struct XModbusVirtualModel* HsfServerIoDigital_simModelExt(
    const HsfServerIoDigital* st);

#ifdef __cplusplus
}
#endif
#endif /* HSFSERVERIODIGITAL_H */
