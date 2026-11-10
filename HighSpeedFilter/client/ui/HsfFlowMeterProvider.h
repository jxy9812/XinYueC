/**
 * @file       HsfFlowMeterProvider.h
 * @brief      电磁流量计页数据提供者接口：页面与后端之间的唯一接缝。
 * @details    与 HsfVfdProvider 同款纯 C 函数表设计，但**只读无控制**
 *             （流量计没有可下发动作，设备语义的自然结果）。
 *
 *             [为何独立一张表而不并入 VfdProvider] 流量计的物理量是 double
 *             （瞬时流量/累积量）、单位是报文带来并需查表的枚举、还有四类
 *             报警位——与变频器的定点频率/状态字口径完全不同。共用一张表
 *             会逼出一堆语义为空的槽位（start/stop/setFrequency 对流量计
 *             毫无意义）。
 *
 *             [侧别] 侧别是**服务端的事实**：设备挂在哪条 485 上由服务端
 *             装配决定，回答/推送帧里带 side，客户端据此知道活动侧。provider
 *             **不按侧寻址**，页面也只管要数据（早先页面写死 FM_SIDE=1、
 *             provider 每侧一张表 ⇒ 现场换接线就得改客户端）。
 *
 *             约定：所有回调入参 userData = m_userData；允许整表为 NULL
 *             或个别槽为 NULL（页面按 NULL 降级：数值显 "--"）。
 */
#ifndef HSFFLOWMETERPROVIDER_H
#define HSFFLOWMETERPROVIDER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief 流量计快照（provider 口径；与 HsfLinkFlowMeter 同形，解耦后端）。
 */
typedef struct HsfFlowMeterSnapshot {
    double   m_flow;         /**< 瞬时流量（单位由 m_flowUnit 给出）。 */
    double   m_speed;        /**< 瞬时流速（m/s）。 */
    double   m_forwardTotal; /**< 正向累积量（单位由 m_totalUnit 给出）。 */
    double   m_reverseTotal; /**< 反向累积量。 */
    int      m_flowUnit;     /**< 瞬时流量单位枚举（XFlowMeterFlowUnit）。 */
    int      m_totalUnit;    /**< 累积总量单位枚举（XFlowMeterTotalUnit）。 */
    int      m_alarms;       /**< 报警位（XFlowMeterAlarm 位掩码）。 */
    bool     m_valid;        /**< 数据有效（服务端质量 + 本地未陈旧）。 */
} HsfFlowMeterSnapshot;

/**
 * @brief 数据变化通知回调（provider 在快照更新后调用）。
 * @param provider 发起通知的 provider。
 * @param page 页面注册时传入的令牌（原样回传）。
 */
typedef void (*HsfFlowMeterNotifyFn)(const struct HsfFlowMeterProvider* provider,
                                     void* page);

/**
 * @brief 电磁流量计数据提供者（纯 C 函数表；页面零业务）。
 */
typedef struct HsfFlowMeterProvider {
    void* m_userData;                                       /**< 各回调首参。 */

    /** @brief 取最近快照（借用；不可用时 m_valid=false）。NULL=无数据源。 */
    const HsfFlowMeterSnapshot* (*snapshot)(void* userData);

    /** @brief 立即请求一拍快照（异步；连接未就绪返回 false）。可为 NULL。 */
    bool (*requestSnapshot)(void* userData);

    /** @brief 注册数据变化回调（同页重复注册按一次计）。 */
    bool (*subscribe)(void* userData, HsfFlowMeterNotifyFn cb, void* page);
    /** @brief 注销数据变化回调（页面对 provider 生命周期早于 provider 时必调）。 */
    void (*unsubscribe)(void* userData, void* page);
} HsfFlowMeterProvider;

/**
 * @brief 取远端（XLink）流量计 provider。
 * @return 静态 provider 表指针；禁释放。
 * @note  [不按侧寻址 2026-11-10] 设备挂在哪条 485 上是**服务端的装配
 *        事实**：服务端按装配位置回答并在帧里带 side，客户端据此知道
 *        活动侧（provider 内部走 HsfLinkClient_flowMeterActive）。早先
 *        provider 是「每侧一张表」、页面写死 FM_SIDE=1 ⇒ 现场换接线就
 *        得改客户端；现在页面只管要数据。
 */
const HsfFlowMeterProvider* HsfFlowMeterProvider_remote(void);

#ifdef __cplusplus
}
#endif
#endif /* HSFFLOWMETERPROVIDER_H */
