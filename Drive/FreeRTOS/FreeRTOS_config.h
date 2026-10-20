/**
 * @file FreeRTOS_config.h
 * @brief FreeRTOS 平台差异裁剪配置。
 * @details 由本模块的公开头 XFreeRTOSCompat.h 包含，产品配置（如 ESP8266 裁剪档案）
 *          通过在编译命令中预先定义同名宏即可覆盖，本文件只提供默认值。
 */

#ifndef XFREERTOSCONFIG_H
#define XFREERTOSCONFIG_H

/** @brief 启用 FreeRTOS V7 兼容层。
 *  @details ESP8266 RTOS SDK 只提供 FreeRTOS V7.5.2，而仓库
 *           Drive/FreeRTOS/Sync 按 V9/V10 API 编写（pdMS_TO_TICKS 与任务
 *           TLS）。打开本开关后由 XFreeRTOSCompat.h 补齐这些接口。
 *           ARM 构建用 V9.0.0 原生具备，保持 0。 */
#ifndef XFREERTOS_V7_COMPAT
#define XFREERTOS_V7_COMPAT 0
#endif


#endif /* XFREERTOSCONFIG_H */
