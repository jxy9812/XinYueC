/**
 * @file       XDeviceUsbHost_unsupported.c
 * @brief      未知平台 USB 统一设备存根。
 * @details    未实现平台明确返回“不支持”，避免把失败误报为设备已连接。
 */
#include "XDeviceUsbHost.h"
#include "XDeviceUsbGadget.h"

#if !defined(_WIN32) && !defined(__linux__) && !defined(__APPLE__) && !defined(__BSD__)
#include "../../XDeviceUsb_platform_stub.inc"
#endif
