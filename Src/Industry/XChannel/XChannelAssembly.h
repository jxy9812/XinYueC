/**
 * @file       XChannelAssembly.h
 * @brief      通道装配器：把「语义点表 + 链路绑定表」装配成可用的通道组。
 * @details    [2026-11-02 XChannel 重写] 这是**唯一**知道「地址怎么变成点」的
 *             地方。设备层（如 XVfdDevice）只认 role，链路层（XIoLink）
 *             只认自己的原生地址；两者之间的翻译全部集中在本模块。
 *
 *             [为什么必须有这一层]
 *             旧实现里，这段装配逻辑住在测试脚手架 Test/XProtocolTest/
 *             xvfd_harness.h 中（550 行、满篇 XModbusMapping/XModbusRegisterType），
 *             产品代码里根本没有消费者——品牌层的三张知识表只有测试在读。
 *             本模块把它提成产品能力，测试改为调用它。
 *
 *             [职责]
 *             1) 按通信组切分链路映射段（每段一次 BindRole 批量登记）；
 *             2) 分配虚拟地址（或透传原生地址，identity 模式）；
 *             3) 按点表创建 XIoPoint、设 role/type/标度/signed；
 *             4) 把点登记进通道组、把 role 绑到链路、把点绑到设备；
 *             5) 把链路约束（XVfdUnitProtocol）翻译给链路实现。
 *
 *             [零遍历] 装配是**一次性**的启动动作，允许顺序处理点表（那不是
 *             数据路径）。装配产出的点表按 role 下标存放，运行期全程 O(1)。
 */
#ifndef XCHANNELASSEMBLY_H
#define XCHANNELASSEMBLY_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#include "XIndustry_config.h"
#include "XIoChannelGroup.h"
#include "XIoLink.h"
#include "XIoPoint.h"
#include <stdint.h>

#if XINDUSTRY_ON && XINDUSTRY_CHANNEL_ON

/**
 * @brief 一条点绑定：语义身份 → 链路原生地址。
 * @note 这是「绑定层」的最小单位；同一 role 在不同链路下可以有不同的 addr。
 */
typedef struct XChannelPointBinding {
    uint16_t role;         /**< 语义槽号（组内下标）。 */
    int32_t nativeAddr;    /**< 链路原生地址（Modbus 虚拟地址/CAN ID/GPIO 行号）。 */
    uint32_t aux;          /**< 链路自定义辅助参数（Modbus 区下标、CAN 位偏移等）。 */
    uint16_t type;         /**< 点类型（XIoPointType）。 */
    uint8_t signedValue;   /**< 原生量是否 16 位补码。 */
    int32_t scaleNum;      /**< 标度分子（0 按 1）。 */
    int32_t scaleDen;      /**< 标度分母（0 按 1）。 */
    int32_t offset;        /**< 工程值偏移。 */
    const char* tag;       /**< 点名（借用）；可为 NULL。 */
} XChannelPointBinding;

/**
 * @brief 装配请求：一组点绑定 + 装配选项。
 */
typedef struct XChannelAssemblySpec {
    const XChannelPointBinding* m_bindings; /**< 点绑定表（静态 const，借用）。 */
    int m_bindingCount;                     /**< 绑定条目数。 */
    bool m_identity;                        /**< true=原生地址即虚拟地址（单站直连）；
                                             *   false=按 role 顺序 bump 分配（多站隔离）。 */
} XChannelAssemblySpec;

/**
 * @brief 装配结果统计（供调用方核对）。
 */
typedef struct XChannelAssemblyResult {
    int m_boundCount;      /**< 成功绑定的点数。 */
    int m_failedRole;      /**< 首个失败点的 role；-1=全部成功。 */
} XChannelAssemblyResult;

/**
 * @brief 把一组点绑定装配到通道组 + 链路。
 * @param group 目标通道组（不能为 NULL；须已 setLink）。
 * @param link 目标链路（不能为 NULL；本函数会调 BindRole）。
 * @param spec 装配请求（不能为 NULL）。
 * @param outResult 输出统计；可为 NULL。
 * @return 全部成功 true；任一点失败返回 false（已建的点保留在组内）。
 * @details 每个绑定：
 *          1) 建 XIoPoint（堆，交给组 autoDelete）；
 *          2) 设 role/type/标度/signed/tag；
 *          3) XIoChannelGroup_addPoint 登记；
 *          4) XIoLink_bindRole_base 登记 (role → nativeAddr, aux)。
 *          点表顺序处理（这是一次性启动动作，不是数据路径）。
 */
bool XChannelAssembly_mount(XIoChannelGroup* group, XIoLink* link,
                            const XChannelAssemblySpec* spec,
                            XChannelAssemblyResult* outResult);

/**
 * @brief 装配一个参数点（按需惰性创建，单点）。
 * @param group 目标通道组。
 * @param link 目标链路。
 * @param binding 该参数的点绑定。
 * @return 成功 true；role 已占用或链路拒绝返回 false。
 * @note 参数点是低频按需操作，与批量装配分开，避免把全部参数组预建导致读帧暴涨。
 */
bool XChannelAssembly_mountOne(XIoChannelGroup* group, XIoLink* link,
                               const XChannelPointBinding* binding);

#endif /* XINDUSTRY_ON && XINDUSTRY_CHANNEL_ON */

#ifdef __cplusplus
}
#endif
#endif /* XCHANNELASSEMBLY_H */
