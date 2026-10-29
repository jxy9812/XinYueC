/**
 * @file       XIndustry_config.h
 * @brief      工业设备层（Industry 目录）总配置：模块总开关与各子模块开关。
 * @details    产品适配口径与 Src/XDevice/XDevice_config.h 一致：
 *              - **XINDUSTRY_ON** 是工业设备层总开关；不需要工业设备的产品
 *                置 0，整层（通道接口 XPoint、电磁阀 XSolenoidValve 等）连同
 *                各适配器一并裁掉；
 *              - **XPOINT_<后缀>_ON** 是通道接口子模块开关；适配器开关再由
 *                各自依赖的底层模块开关派生（如 Modbus 适配器默认跟随
 *                XProtocol_config.h 的 XMODBUS_ON），底层被裁掉时适配器
 *                一并裁掉，不产生未定义符号；
 *              - 各开关独立取默认值，互不反向推导，避免成环。
 *
 *             覆盖时机：产品配置以强制包含方式先于本文件生效时，产品可在
 *             自己的配置里直接 #define 这些开关覆盖默认值。
 */
#ifndef XINDUSTRY_CONFIG_H
#define XINDUSTRY_CONFIG_H

#include "CXinYueConfig.h"

/* ==========================================================================
 * 一、模块开关
 * ========================================================================== */

/** @brief 工业设备层总开关；置 0 时整层裁掉。 */
#ifndef XINDUSTRY_ON
#define XINDUSTRY_ON 1
#endif

/** @brief 通道接口子模块（XChannel：XIoChannel + 四接口）；工业设备的"插座标准"。 */
#ifndef XINDUSTRY_CHANNEL_ON
#define XINDUSTRY_CHANNEL_ON 1
#endif

/** @brief 电磁阀设备（XSolenoidValve）；依赖 XPOINT_ON。 */
#ifndef XSOLENOID_VALVE_ON
#define XSOLENOID_VALVE_ON 1
#endif

/** @brief GPIO 通道适配器；实际可用性还取决于平台后端（Drive/ 各平台 XGpio 实现）。 */
#ifndef XINDUSTRY_GPIO_ON
#define XINDUSTRY_GPIO_ON 1
#endif

/** @brief Modbus 通道适配器；默认跟随协议层 XMODBUS_ON 派生，协议层裁掉时一并裁掉。
 *  @note   本文件必须显式包含 XProtocol_config.h 再做派生：XMODBUS_ON 定义于
 *          该文件，而本文件常被 CXinYueConfig.h 中途包含（头保护挡住递归，
 *          派生点未必晚于 XProtocol_config.h 的包含点）——不显式包含会恒
 *          派生为 0、适配器被静默裁掉（2026-10-29 实锅）。 */
#ifndef XINDUSTRY_MODBUS_ON
#include "XProtocol_config.h"
#ifdef XMODBUS_ON
#define XINDUSTRY_MODBUS_ON 1
#else
#define XINDUSTRY_MODBUS_ON 0
#endif
#endif

#endif /* XINDUSTRY_CONFIG_H */
