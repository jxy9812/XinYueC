/**
 * @file XFreeRTOSCompat.h
 * @brief FreeRTOS V7.5.2 兼容层：补齐 V9/V10 才有的接口。
 * @details
 * 仓库的 Drive/FreeRTOS/Sync 按 V9/V10 的 API 编写，而 ESP8266 RTOS SDK 只
 * 提供 V7.5.2（V7 与 V9 的内核符号互斥，同时链接会产生 42 处重复定义，详见
 * esp8266/CMakeLists.txt 的说明）。缺的是三个接口：
 *
 *   pdMS_TO_TICKS                          —— V8 引入的毫秒转节拍宏
 *   vTaskSetThreadLocalStoragePointer      —— V10 的任务 TLS
 *   pvTaskGetThreadLocalStoragePointer
 *
 * 本文件只在 XFREERTOS_V7_COMPAT=1 时提供这些符号；ARM 构建用 V9.0.0，
 * 原生具备，不受影响。ESP8266 裁剪档案（ESP8266Minimal.h）会定义该宏。
 *
 * @note V7.5.2 没有 xTaskGetState / eTaskState，无法查询任务运行态。
 *       XThread 的运行态判断改用其自有的 m_finished 标志（见
 *       XThreadFreeRTOS.c），不伪造一个任务状态 API。
 */

#ifndef XFREERTOS_COMPAT_H
#define XFREERTOS_COMPAT_H

/* SDK 要求 FreeRTOS.h 必须先于 task.h，否则触发 #error。 */
#include "FreeRTOS_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#if XFREERTOS_V7_COMPAT

#include <stdint.h>

/** @brief V8 才引入的毫秒→节拍换算。
 *  @note V7.5.2 的 portmacro.h 只提供 portTICK_RATE_MS（每个 tick 的毫秒数），
 *        既无 portTICK_RATE_HZ 也无 portTICK_PERIOD_MS，故按前者做除法。
 *        向下取整，与 V8 实现一致；调用方需自行保证结果不为 0。 */
#ifndef pdMS_TO_TICKS
#define pdMS_TO_TICKS(ms) \
    ((portTickType)((portTickType)(ms) / (portTickType)portTICK_RATE_MS))
#endif

/**
 * @brief 为任务设置线程局部存储指针。
 * @details V10 才有的 API。V7.5.2 的 TCB 没有 TLS 槽，这里用与线程对象
 *          一一对应的静态表实现：XThread 的 TLS 索引按对象分配，槽位数由
 *          XFREERTOS_TLS_SLOTS 限定。并发访问在临界区内完成。
 */
void vTaskSetThreadLocalStoragePointer(xTaskHandle task, uint32_t index, void* value);

/**
 * @brief 读取任务的线程局部存储指针；未设置时返回 NULL。
 */
void* pvTaskGetThreadLocalStoragePointer(xTaskHandle task, uint32_t index);

#endif /* XFREERTOS_V7_COMPAT */

#endif /* XFREERTOS_COMPAT_H */
