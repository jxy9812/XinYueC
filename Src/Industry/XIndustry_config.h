/**
 * @file       XIndustry_config.h
 * @brief      工业设备层（Industry 目录）总配置：模块总开关与各子模块开关。
 * @details    产品适配口径与 Src/XDevice/XDevice_config.h 一致：
 *              - **XINDUSTRY_ON** 是工业设备层总开关；不需要工业设备的产品
 *                置 0，整层（通道接口 XChannel、电磁阀 XSolenoidValve 等）连同
 *                各适配器一并裁掉；
 *              - **XINDUSTRY_CHANNEL_ON** 是通道接口子模块开关（XChannel：
 *                XIoChannel + 四接口）；适配器开关再由各自依赖的底层模块
 *                开关派生（如 Modbus 适配器默认跟随 XProtocol_config.h 的
 *                XMODBUS_ON），底层被裁掉时适配器一并裁掉，不产生未定义
 *                符号；
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

/** @brief 变频器设备层（XVfd）子模块开关；随工业设备层与通道接口派生。
 *  @note   派生点必须留在本文件（而非 XVfd/XVfd_config.h）：XVFD_ON 是**全局
 *          可见**的模块开关——Test 侧 main.c / XTestCommand.c / XProtocolTest.h
 *          的 XVfd 闭环测试派发块用 `#if XPROTOCOL_ON && XVFD_ON` 裁剪，派发
 *          一旦被裁掉，三个闭环测试会「静默不再运行」（2026-11-02 实锅：
 *          XVfd 迁出 XProtocol 时 XVfd_config.h 不再随 XProtocol_config.h
 *          进入全局包含链，XVFD_ON 不可见 → 派发块整体编译掉 → 派发串在
 *          二进制里消失 → `--test xvfd-atv61` 报「未知测试命令」）。
 *          另：派生表达式惰性求值，故本处只需保证 XINDUSTRY_ON 与
 *          XINDUSTRY_CHANNEL_ON 均已定义（排在其后即可），设备级细开关
 *          （XVFD_DEVICE_ON / XVFD_ATV61_ON / XVFD_M512_ON）仍在
 *          XVfd/XVfd_config.h 内按本开关派生。 */
#ifndef XVFD_ON
#define XVFD_ON (XINDUSTRY_ON && XINDUSTRY_CHANNEL_ON)
#endif

/** @brief 电磁阀设备（XSolenoidValve）；依赖 XINDUSTRY_CHANNEL_ON（经 XChannel 通道接口）。 */
#ifndef XSOLENOID_VALVE_ON
#define XSOLENOID_VALVE_ON 1
#endif

/** @brief 温湿度变送器设备（XTempHumTransmitter）；依赖 XINDUSTRY_CHANNEL_ON。 */
#ifndef XTEMP_HUM_ON
#define XTEMP_HUM_ON 1
#endif

/** @brief 压差计设备（XDiffPressureGauge）；依赖 XINDUSTRY_CHANNEL_ON。 */
#ifndef XPRESS_DIFF_ON
#define XPRESS_DIFF_ON 1
#endif

/** @brief 电磁流量计设备（XFlowMeter）；依赖 XINDUSTRY_CHANNEL_ON。
 *  @note   设备层只认通道组（XIoPoint role），Modbus/TCP/CAN 等传输由装配期
 *          注入，故本开关不看 XINDUSTRY_MODBUS_ON——换成 CAN 链路照常可用。 */
#ifndef XFLOW_METER_ON
#define XFLOW_METER_ON 1
#endif

/** @brief 按钮组设备（XButtonStation：急停/启动/停止/复位 + 手自动转换开关 +
 *         按钮指示灯）；依赖 XINDUSTRY_CHANNEL_ON。
 *  @note   读侧是 DI 触点、写侧只有指示灯 DO，两侧都不认识链路类型；触点接在
 *          GPIO 干接点还是 Modbus 离散输入由装配期决定，本开关不看
 *          XINDUSTRY_MODBUS_ON。 */
#ifndef XBUTTON_STATION_ON
#define XBUTTON_STATION_ON 1
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
