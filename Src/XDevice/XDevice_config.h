/**
 * @file       XDevice_config.h
 * @brief      设备层（XDevice 家族）总配置：设备总开关、各设备类开关与设备
 *             容量配置。
 * @details    产品适配口径（面向"同一套框架跑桌面 / 单片机裸机 / 精简板"
 *             这类场景）：
 *              - **XDEVICE_ON** 是设备层总开关，只在 CXinYueConfig.h 里出现
 *                一次（框架总配置不再散落设备开关）；不需要统一设备抽象的
 *                产品置 0，整层设备抽象连同各设备类一并裁掉；
 *              - **XDEVICE_<类名>_ON** 是每个设备类的独立开关，按产品需要
 *                逐个裁剪：文件 / 控制台 / 目录 / 网络 / 串口 / 定时器 /
 *                USB 主机 / USB 从机 / 无线。关掉某设备后，该设备类与其
 *                平台后端一并裁掉；直接依赖它的业务模块在各自头文件里
 *                显式声明该依赖（例如 XNetworkWiFi 要求 XDEVICE_WIFI_ON）；
 *              - **历史开关名**（XSERIALPORT_ON / XDEVICETIMER_ON /
 *                XDEVICEUSBHOST_ON / XDEVICEUSBGADGET_ON）已统一并入上述
 *                XDEVICE_<类名>_ON 一套命名，不再保留别名；
 *              - **设备容量配置**（如无线适配器槽位上限）也放这里，便于按
 *                目标硬件规模裁剪。
 *
 *             覆盖时机：产品配置（如 ESP8266Minimal.h）以强制包含方式先于
 *             本文件生效，故产品可在自己的配置里直接 #define 这些开关，
 *             覆盖本文件的默认值。
 */
#ifndef XDEVICE_CONFIG_H
#define XDEVICE_CONFIG_H

/* 业务开关（XFILE_ON / XNETWORK_ON 等）在 CXinYueConfig.h 定义；设备开关的
 * 默认值引用它们，故此处引入（CXinYueConfig.h 末尾会反向包含本文件，由
 * 头文件守卫化解循环）。 */
#include "CXinYueConfig.h"

/* ==========================================================================
 * 一、设备类开关
 * ==========================================================================
 * 各设备开关**独立取默认值**（不反向依赖业务开关），业务层再由设备开关
 * 派生（如 XFILE_ON = XDEVICE_ON && XDEVICE_FILE_ON），避免互相推导成环。
 */

/** @brief 文件设备开关。 */
#ifndef XDEVICE_FILE_ON
#define XDEVICE_FILE_ON 1
#endif

/** @brief 控制台设备开关；设备层自带，无业务依赖。 */
#ifndef XDEVICE_CONSOLE_ON
#define XDEVICE_CONSOLE_ON 1
#endif

/** @brief 目录设备开关；设备层自带，无业务依赖。 */
#ifndef XDEVICE_DIR_ON
#define XDEVICE_DIR_ON 1
#endif

/** @brief 网络设备开关。 */
#ifndef XDEVICE_NETWORK_ON
#define XDEVICE_NETWORK_ON 1
#endif

/** @brief 串口设备开关；业务层 XSerialPort 跟随本开关。 */
#ifndef XDEVICE_SERIALPORT_ON
#define XDEVICE_SERIALPORT_ON 1
#endif

/** @brief 定时器设备开关。 */
#ifndef XDEVICE_TIMER_ON
#define XDEVICE_TIMER_ON 1
#endif

/** @brief USB 主机设备开关。 */
#ifndef XDEVICE_USBHOST_ON
#define XDEVICE_USBHOST_ON 1
#endif

/** @brief USB 从机设备开关。 */
#ifndef XDEVICE_USBGADGET_ON
#define XDEVICE_USBGADGET_ON 1
#endif

/** @brief 无线（Wi-Fi）设备开关；独立子系统，业务层 XNetworkWiFi 跟随本开关。 */
#ifndef XDEVICE_WIFI_ON
#define XDEVICE_WIFI_ON 1
#endif

/* ==========================================================================
 * 二、设备容量配置
 * ========================================================================== */

/** @brief 无线适配器槽位上限（平台后端按此上界分配适配器状态表）。 */
#ifndef XDEVICE_WIFI_MAX_ADAPTERS
#define XDEVICE_WIFI_MAX_ADAPTERS 8
#endif

#endif /* XDEVICE_CONFIG_H */
