/**
 * @file XConsoleShell_XIODevice.h
 * @brief XConsoleShell 到 XIODevice 的库级传输适配器。
 * @details
 * 适配器只保存调用方借用的 XIODevice 指针，将读、写和刷新回调转换为
 * XIODevice 公共 API。适配器不拥有设备、不负责打开或关闭设备，也不包含
 * POSIX、Windows、串口或网络平台头文件。线程安全由调用方保证。
 */

#ifndef XCONSOLE_SHELL_XIODEVICE_H
#define XCONSOLE_SHELL_XIODEVICE_H

#include "XConsoleShell.h"
#include "XIODevice.h"

#if XCONSOLE_SHELL_ON && XCONSOLE_SHELL_IO_ON && XCONSOLE_SHELL_XIODEVICE_BACKEND_ON

#ifdef __cplusplus
extern "C" {
#endif

/** @brief XIODevice 传输适配器；device 为借用指针，生命周期必须长于 io。 */
typedef struct XConsoleShellXIODeviceAdapter {
    XIODevice* device; /**< 借用的 XIODevice，不由适配器释放。 */
} XConsoleShellXIODeviceAdapter;

/**
 * @brief 初始化适配器并绑定借用的设备。
 * @param adapter 待初始化适配器；不能为空。
 * @param device XIODevice 借用指针；必须在适配器使用期间保持有效。
 */
void XConsoleShellXIODeviceAdapter_init(XConsoleShellXIODeviceAdapter* adapter,
                                        XIODevice* device);
/**
 * @brief 将适配器转换为 Shell I/O 回调集合。
 * @param adapter 已初始化且绑定设备的适配器。
 * @param io 输出回调集合；由调用方提供存储。
 * @return 转换成功返回 true；适配器、设备或 io 为空返回 false。
 */
bool XConsoleShellXIODeviceAdapter_makeIo(
    XConsoleShellXIODeviceAdapter* adapter, XConsoleShellIo* io);

#ifdef __cplusplus
}
#endif

#endif /* XCONSOLE_SHELL_ON && XCONSOLE_SHELL_IO_ON && XCONSOLE_SHELL_XIODEVICE_BACKEND_ON */
#endif /* XCONSOLE_SHELL_XIODEVICE_H */
