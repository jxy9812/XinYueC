/**
 * @file       XDeviceUsb_tinyusb.h
 * @brief      TinyUSB 嵌入式后端公共定义。
 * @details    为 XDeviceUsbHost 和 XDeviceUsbGadget 提供基于 TinyUSB 的
 *             嵌入式平台实现。支持 STM32F4（DWC2）和 ESP32-S3（DWC2）。
 *
 *             使用条件：
 *             - 已启用 XINYUE_C_HAS_TINYUSB
 *             - 已指定 MCU（CFG_TUSB_MCU 在 tusb_config.h 中定义）
 *             - 已在板级初始化中配置 USB 时钟、GPIO、中断
 *
 *             注意：本后端仅实现 XinYueC USB 框架与 TinyUSB 之间的适配层。
 *             USB 硬件初始化（时钟、GPIO、NVIC、USB 外设复位）由 BSP 负责。
 */
#ifndef XDEVICEUSB_TINYUSB_H
#define XDEVICEUSB_TINYUSB_H

#include "XDeviceUsbHost.h"
#include "XDeviceUsbGadget.h"

#if defined(XINYUE_C_HAS_TINYUSB)

#include "tusb.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------
 * 平台支持检测
 * ------------------------------------------------------------------ */

/* STM32F4：OTG_FS / OTG_HS，DWC2 内核 */
#if defined(STM32F40_41xxx) || defined(STM32F427_437xx) || \
    defined(STM32F429_439xx) || defined(STM32F401xx) || \
    defined(STM32F411xx) || defined(STM32F446xx) || \
    defined(STM32F469xx) || defined(STM32F412xx) || \
    defined(STM32F413xx) || defined(STM32F407xx)
#define XINYUE_USB_TINYUSB_STM32F4   1
#define XINYUE_USB_TINYUSB_DWC2      1
#endif

/* ESP32-S3：USB OTG，DWC2 内核 */
#if defined(ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32S3) || \
    defined(ESP_PLATFORM)
#define XINYUE_USB_TINYUSB_ESP32S3   1
#define XINYUE_USB_TINYUSB_DWC2      1
#endif

/* ------------------------------------------------------------------
 * 错误映射
 * ------------------------------------------------------------------ */

XDeviceUsbError xTinyUsbMapError(tusb_error_t err);

#ifdef __cplusplus
}
#endif

#endif /* XINYUE_C_HAS_TINYUSB */
#endif /* XDEVICEUSB_TINYUSB_H */
