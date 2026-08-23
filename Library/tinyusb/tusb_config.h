/*
 * @file       tusb_config.h
 * @brief      TinyUSB 配置模板（基于 0.21.0 版本）。
 * @details    XinYueC 项目的 TinyUSB 默认配置。
 *             实际嵌入式项目应根据 MCU、RAM、所需功能进行裁剪。
 *
 *             功能开关：
 *             - CFG_TUD_ENABLED : Device 栈
 *             - CFG_TUH_ENABLED : Host 栈
 *             由 CMake 根据 TINYUSB_DEVICE / TINYUSB_HOST 自动定义。
 *
 *             注意：TinyUSB 0.21.0 为纯静态分配，不使用 malloc/free，
 *             因此无需接入 XinYueC 的 XMemory。所有缓冲区大小由此
 *             文件中的宏在编译时确定。
 */

#ifndef TUSB_CONFIG_H
#define TUSB_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

/* --------------------------------------------------------------------
 * MCU 选择（嵌入式构建必须指定）
 * -------------------------------------------------------------------- */
/*
#ifndef CFG_TUSB_MCU
#define CFG_TUSB_MCU    OPT_MCU_INVALID
#endif
*/

/* --------------------------------------------------------------------
 * 通用配置
 * -------------------------------------------------------------------- */

/* 调试级别：0=关，1=错误，2=警告，3=信息，4=调试 */
#ifndef CFG_TUSB_DEBUG
#define CFG_TUSB_DEBUG      0
#endif

/* 操作系统：OPT_OS_NONE / OPT_OS_FREERTOS / OPT_OS_RTTHREAD 等 */
#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS         OPT_OS_NONE
#endif

/* 调试串口输出（debug > 0 时需要） */
#ifndef CFG_TUSB_DEBUG_PRINTF
/* #define CFG_TUSB_DEBUG_PRINTF  printf */
#endif

/* 端口数量（默认 1 个 USB 端口） */
#ifndef CFG_TUSB_RHPORT0_MODE
#define CFG_TUSB_RHPORT0_MODE   OPT_MODE_NONE
#endif
#ifndef CFG_TUSB_RHPORT1_MODE
#define CFG_TUSB_RHPORT1_MODE   OPT_MODE_NONE
#endif

/* --------------------------------------------------------------------
 * Device 栈配置
 * -------------------------------------------------------------------- */

#if CFG_TUD_ENABLED

/* EP0 最大包长：FS=64, HS=64 */
#ifndef CFG_TUD_ENDPOINT0_SIZE
#define CFG_TUD_ENDPOINT0_SIZE   64
#endif

/* CDC ACM 设备类 */
#ifndef CFG_TUD_CDC
#define CFG_TUD_CDC              0
#endif
#ifndef CFG_TUD_CDC_RX_BUFSIZE
#define CFG_TUD_CDC_RX_BUFSIZE   64
#endif
#ifndef CFG_TUD_CDC_TX_BUFSIZE
#define CFG_TUD_CDC_TX_BUFSIZE   64
#endif

/* MSC 大容量存储类 */
#ifndef CFG_TUD_MSC
#define CFG_TUD_MSC              0
#endif
#ifndef CFG_TUD_MSC_BUFSIZE
#define CFG_TUD_MSC_BUFSIZE      512
#endif
#ifndef CFG_TUD_MSC_EP_BUFSIZE
#define CFG_TUD_MSC_EP_BUFSIZE   512
#endif

/* HID 人机接口类 */
#ifndef CFG_TUD_HID
#define CFG_TUD_HID              0
#endif
#ifndef CFG_TUD_HID_EP_BUFSIZE
#define CFG_TUD_HID_EP_BUFSIZE   64
#endif
#ifndef CFG_TUD_HID_EPOUT_BUFSIZE
#define CFG_TUD_HID_EPOUT_BUFSIZE  64
#endif

/* DFU 固件升级类 */
#ifndef CFG_TUD_DFU_RUNTIME
#define CFG_TUD_DFU_RUNTIME      0
#endif
#ifndef CFG_TUD_DFU
#define CFG_TUD_DFU              0
#endif
#ifndef CFG_TUD_DFU_XFER_BUFSIZE
#define CFG_TUD_DFU_XFER_BUFSIZE   64
#endif

/* MIDI 音频类 */
#ifndef CFG_TUD_MIDI
#define CFG_TUD_MIDI             0
#endif
#ifndef CFG_TUD_MIDI_RX_BUFSIZE
#define CFG_TUD_MIDI_RX_BUFSIZE   64
#endif
#ifndef CFG_TUD_MIDI_TX_BUFSIZE
#define CFG_TUD_MIDI_TX_BUFSIZE   64
#endif

/* Audio 2.0 */
#ifndef CFG_TUD_AUDIO
#define CFG_TUD_AUDIO            0
#endif

/* Video */
#ifndef CFG_TUD_VIDEO
#define CFG_TUD_VIDEO            0
#endif

/* 网络类 */
#ifndef CFG_TUD_ECM_RNDIS
#define CFG_TUD_ECM_RNDIS        0
#endif
#ifndef CFG_TUD_NCM
#define CFG_TUD_NCM              0
#endif

/* Vendor 厂商类 */
#ifndef CFG_TUD_VENDOR
#define CFG_TUD_VENDOR           0
#endif
#ifndef CFG_TUD_VENDOR_RX_BUFSIZE
#define CFG_TUD_VENDOR_RX_BUFSIZE   64
#endif
#ifndef CFG_TUD_VENDOR_TX_BUFSIZE
#define CFG_TUD_VENDOR_TX_BUFSIZE   64
#endif

/* USB Test & Measurement Class */
#ifndef CFG_TUD_USBTMC
#define CFG_TUD_USBTMC           0
#endif

/* Bluetooth Host */
#ifndef CFG_TUD_BTH
#define CFG_TUD_BTH              0
#endif

/* MTP 媒体传输协议 */
#ifndef CFG_TUD_MTP
#define CFG_TUD_MTP              0
#endif

/* Printer 打印类 */
#ifndef CFG_TUD_PRINTER
#define CFG_TUD_PRINTER          0
#endif

#endif /* CFG_TUD_ENABLED */

/* --------------------------------------------------------------------
 * Host 栈配置
 * -------------------------------------------------------------------- */

#if CFG_TUH_ENABLED

/* Host 最大设备数 */
#ifndef CFG_TUH_DEVICE_MAX
#define CFG_TUH_DEVICE_MAX       4
#endif

/* Host 端点数（每设备） */
#ifndef CFG_TUH_ENDPOINT_MAX
#define CFG_TUH_ENDPOINT_MAX     8
#endif

/* Hub 支持 */
#ifndef CFG_TUH_HUB
#define CFG_TUH_HUB              0
#endif

/* HID Host */
#ifndef CFG_TUH_HID
#define CFG_TUH_HID              0
#endif
#ifndef CFG_TUH_HID_EPIN_BUFSIZE
#define CFG_TUH_HID_EPIN_BUFSIZE   64
#endif
#ifndef CFG_TUH_HID_EPOUT_BUFSIZE
#define CFG_TUH_HID_EPOUT_BUFSIZE  64
#endif

/* MSC Host */
#ifndef CFG_TUH_MSC
#define CFG_TUH_MSC              0
#endif

/* CDC Host */
#ifndef CFG_TUH_CDC
#define CFG_TUH_CDC              0
#endif
#ifndef CFG_TUH_CDC_RX_BUFSIZE
#define CFG_TUH_CDC_RX_BUFSIZE   64
#endif
#ifndef CFG_TUH_CDC_TX_BUFSIZE
#define CFG_TUH_CDC_TX_BUFSIZE   64
#endif

/* Vendor Host */
#ifndef CFG_TUH_VENDOR
#define CFG_TUH_VENDOR           0
#endif

/* MIDI Host */
#ifndef CFG_TUH_MIDI
#define CFG_TUH_MIDI             0
#endif

#endif /* CFG_TUH_ENABLED */

#ifdef __cplusplus
}
#endif

#endif /* TUSB_CONFIG_H */
