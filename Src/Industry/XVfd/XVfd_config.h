/**
 * @file XVfd_config.h
 * @brief 变频器设备层（XVfd）裁剪配置文件。
 * @details [2026-11-01 迁入 Industry + 通道化重构] 本子系统已从
 *          Src/XCore/XProtocol 迁至 Src/Industry，与 XChannel 平级：
 *            - 依赖方向改为 **XVfd → XChannel**（XIoChannelGroup/XIoPoint），
 *              XVfd 不再包含任何 XModbus 头、不持总线链路、不自建轮询——
 *              数据来源与写通道全部由通道点在装配期注入（借用）；
 *            - 具体传输（Modbus RTU/TCP、GPIO、CAN…）由 XChannel 的适配器
 *              点子类（如 XModbusPoint）承担，XVfd 对其一无所知，
 *              换传输只换点、不改设备层。
 *          子功能裁剪开关：
 *   1. XVFD_DEVICE_ON - 变频器抽象父类（XVfdDevice：语义槽/状态快照/能力位图）
 *   2. XVFD_ATV61_ON  - 施耐德 ATV61 品牌子类
 *   3. XVFD_M512_ON   - 511M/512M/513M 系列品牌子类
 * @note 开关一律由工业设备层总开关派生，不反向引用协议层开关（避免环）。
 */
#ifndef XVFD_CONFIG_H
#define XVFD_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#include "XIndustry_config.h"

/* 变频器设备层总开关 XVFD_ON 定义在 XIndustry_config.h（全局可见的模块
 * 开关，Test 侧闭环测试派发块按它裁剪）——本文件只管设备级与品牌级细开关。
 * 依赖 XIndustry_config.h 在下方 #if 处求值前已定义好 XVFD_ON，故此处的
 * #if XVFD_ON 与细开关派生顺序恒定。 */

#if XVFD_ON

/** @brief 变频器抽象父类（XVfdDevice）。 */
#ifndef XVFD_DEVICE_ON
#define XVFD_DEVICE_ON 1
#endif

/** @brief 施耐德 ATV61 品牌子类（XVfdAtv61）。 */
#ifndef XVFD_ATV61_ON
#define XVFD_ATV61_ON XVFD_DEVICE_ON
#endif

/** @brief 511M/512M/513M 系列品牌子类（XVfdM512）。 */
#ifndef XVFD_M512_ON
#define XVFD_M512_ON XVFD_DEVICE_ON
#endif

#endif /* XVFD_ON */

#ifdef __cplusplus
}
#endif

#endif /* XVFD_CONFIG_H */
