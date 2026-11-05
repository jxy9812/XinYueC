
/**
 * @file       HsfServerIoAnalog.h
 * @brief      服务端模拟量站：中盛 8 路 4-20mA 模块（Modbus-TCP 从站）。
 * @details    与 485 两路总线无关，本模块自带一个 XModbusHub（TCP 客户端），
 *             只负责 8 路输入寄存器 → 8 个 XIoAnalogPoint 的装配与取值。
 *             取值口径：寄存器值 = 采样电压毫伏（固定 3 位小数），
 *             电流 I(mA) = mV / 249Ω；通道工程值取 μA 定点
 *             （eng = raw × 1000 / 249），对外 API 再折成 mA。
 *             详见 docs/模拟量输入模块_探测与验证_20261103.md。
 *
 *             线程模型：单线程事件循环；全部 API 须在 XCoreApplication
 *             事件循环内调用。
 */
#ifndef HSFSERVERIOANALOG_H
#define HSFSERVERIOANALOG_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#include <stdbool.h>
#include <stdint.h>

#include "XObject.h"

struct XModbusHub;
struct XModbusClient;
struct XModbusLink;
struct XIoChannelGroup;

/* ==================== 虚函数表定义（继承 XObject，不新增虚槽） ==================== */
XCLASS_DEFINE_BEGING(HsfServerIoAnalog)
XCLASS_DEFINE_EXTEND_END(HsfServerIoAnalog, XObject)

/**
 * @brief 模拟量站运行态（本模块拥有其内部资源）。
 * @details 成员禁止直接访问，一律经 API 操作。
 */
typedef struct HsfServerIoAnalog {
    XObject m_class;                    /**< 第一成员：基类 XObject。 */
    struct XModbusHub* m_hub;           /**< 本站独占 Hub（自有）。 */
    struct XModbusClient* m_client;     /**< 客户端（所有权在 Hub；TCP 或虚拟）。 */
    struct XIoChannelGroup* m_group;    /**< 通道组（自有）。 */
    struct XModbusLink* m_link;         /**< 链路（自有）。 */
    int m_devId;                        /**< Hub 设备句柄；<0=未挂。 */
    int m_channelCount;                 /**< 实际装配的通道数。 */
    int m_shuntOhm;                     /**< 采样电阻（Ω；标度反推用）。 */
    bool m_mounted;                     /**< 装配完成标志。 */
} HsfServerIoAnalog;

/* ==================== 类初始化 / 构造与析构 ==================== */

/**
 * @brief 初始化 HsfServerIoAnalog 类的虚函数表（单例）。
 * @return 类虚拟函数表指针；不可释放。
 */
XVtable* HsfServerIoAnalog_class_init(void);

/**
 * @brief 栈上初始化模拟量站（未装配）。
 * @param st 目标对象；不能为 NULL，可为未初始化内存（内部先清零）。
 */
void HsfServerIoAnalog_init(HsfServerIoAnalog* st);

/* ==================== 装配 ==================== */

/**
 * @brief 装配并启动模拟量站（TCP 客户端 + 8 路 AI 点 + 轮询）。
 * @param st 目标站；不能为 NULL。
 * @param host 模块 IP（借用）。
 * @param port TCP 端口。
 * @param unitId 从站 Unit ID。
 * @param channels 通道数（<= 8）。
 * @param shuntOhm 采样电阻（Ω；249.0 为实测值）。
 * @param pollMs 轮询周期（毫秒）。
 * @return 装配成功返回 true。
 * @note TCP 连不上**不**阻断装配：Hub 自带重连，插回网线后自愈。
 *       只有内存/装配本身失败才返回 false。
 */
bool HsfServerIoAnalog_mount(HsfServerIoAnalog* st, const char* host, int port,
                             int unitId, int channels, double shuntOhm,
                             int pollMs);

/**
 * @brief 用**外部给的客户端**装配模拟量站（模拟设备/单测入口）。
 * @param st 目标站。
 * @param client Modbus 客户端（所有权转移给本站 Hub）。
 * @param unitId 从站 Unit ID（映射段登记用）。
 * @param channels 通道数（<= 8）。
 * @param shuntOhm 采样电阻（Ω）。
 * @param pollMs 轮询周期（毫秒）。
 * @return 装配成功返回 true。
 *
 * @details [与 mount 的区别] 本接口不建 TCP 客户端、不需要 IP/端口，
 *          传输层完全由调用方决定（虚拟客户端也是 XModbusClient）。
 *          这样模拟模式与真设备**走同一份装配代码**，不会出现"测试装配
 *          与实际装配不同步"。
 */
bool HsfServerIoAnalog_mountWithClient(HsfServerIoAnalog* st,
                                       struct XModbusClient* client,
                                       int unitId, int channels,
                                       double shuntOhm, int pollMs);

/**
 * @brief 拆除本站（幂等）。
 * @param st 目标站。
 * @note 顺序：停轮询 → 删组 → 删链路 → 删 Hub。
 */
void HsfServerIoAnalog_unmount(HsfServerIoAnalog* st);

/* ==================== 查询 ==================== */

/**
 * @brief 读全部通道（工程值换算后）。
 * @param st 目标站。
 * @param outMA 输出数组（调用方提供，至少 channels 个；mA）。
 * @param outValid 输出各通道有效标志（可为 NULL）。
 * @param maxCount 输出数组容量。
 * @return 实际写入的通道数（0=未装配）。
 */
int HsfServerIoAnalog_values(const HsfServerIoAnalog* st, double* outMA,
                             bool* outValid, int maxCount);

/**
 * @brief 读全部通道原始寄存器值（毫伏口径，排查真相用）。
 * @param st 目标站。
 * @param outRaw 输出数组（调用方提供）。
 * @param outValid 输出各通道有效标志（可为 NULL）。
 * @param maxCount 输出数组容量。
 * @return 实际写入的通道数（0=未装配）。
 */
int HsfServerIoAnalog_rawValues(const HsfServerIoAnalog* st, int32_t* outRaw,
                                bool* outValid, int maxCount);

/**
 * @brief 取本站 Hub（借用；诊断用）。
 */
struct XModbusHub* HsfServerIoAnalog_hub(const HsfServerIoAnalog* st);

/**
 * @brief 本站是否已装配完成。
 */
bool HsfServerIoAnalog_isMounted(const HsfServerIoAnalog* st);

#ifdef __cplusplus
}
#endif
#endif /* HSFSERVERIOANALOG_H */
