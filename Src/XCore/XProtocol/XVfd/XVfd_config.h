/**
 * @file XVfd_config.h
 * @brief 变频器协议子系统（XVfd）裁剪配置文件。
 * @details 通过本配置文件裁剪 XVfd 子系统内部的各个子功能：
 *   1. XVFD_DEVICE_ON - 变频器抽象父类（XVfdDevice：业务语义全集/状态快照/能力位图）
 *   2. XVFD_LINK_ON   - 总线链路（XVfdLink 计划表与健康模型 + XVfdModbusLink 适配器）
 *   3. XVFD_ATV61_ON  - 施耐德 ATV61 品牌子类
 *   4. XVFD_M512_ON   - 511M/512M/513M 系列品牌子类
 * 依赖 XModbus 客户端（链路层持有 XModbusClient），故随 XMODBUS_CLIENT_ON 联动；
 * XVfd 不知道 XModbus 之外的任何传输细节。
 */
#ifndef XVFD_CONFIG_H
#define XVFD_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#include "XProtocol_config.h"

#ifndef XVFD_ON
#define XVFD_ON (XPROTOCOL_ON && XMODBUS_ON)
#endif

#if XVFD_ON

/** @brief 变频器抽象父类（XVfdDevice） */
#ifndef XVFD_DEVICE_ON
#define XVFD_DEVICE_ON (XMODBUS_CLIENT_ON)
#endif

/** @brief 总线链路（XVfdLink + XVfdModbusLink + XVfdPollScheduler） */
#ifndef XVFD_LINK_ON
#define XVFD_LINK_ON (XVFD_DEVICE_ON && XMODBUS_CLIENT_ON)
#endif

/** @brief 施耐德 ATV61 品牌子类（XVfdAtv61） */
#ifndef XVFD_ATV61_ON
#define XVFD_ATV61_ON XVFD_DEVICE_ON
#endif

/** @brief 511M/512M/513M 系列品牌子类（XVfdM512） */
#ifndef XVFD_M512_ON
#define XVFD_M512_ON XVFD_DEVICE_ON
#endif

#endif /* XVFD_ON */

#ifdef __cplusplus
}
#endif

#endif /* XVFD_CONFIG_H */
