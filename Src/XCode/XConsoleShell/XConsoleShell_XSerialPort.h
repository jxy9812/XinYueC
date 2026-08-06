/**
 * @file XConsoleShell_XSerialPort.h
 * @brief XConsoleShell 到 XSerialPort 的串口传输适配器。
 * @details
 * XSerialPort 的首成员为 XIODevice。本适配器仅将该已有公共继承关系传递给
 * XConsoleShellXIODeviceAdapter，不打开、关闭、配置或释放串口对象。调用方负责
 * 串口生命周期、事件循环和线程串行化；本文件不包含平台串口头文件。
 */

#ifndef XCONSOLE_SHELL_XSERIALPORT_H
#define XCONSOLE_SHELL_XSERIALPORT_H

#include "XConsoleShell_XIODevice.h"
#include "XSerialPort.h"

#if XCONSOLE_SHELL_ON && XCONSOLE_SHELL_IO_ON && XCONSOLE_SHELL_XSERIALPORT_BACKEND_ON

#ifdef __cplusplus
extern "C" {
#endif

/** @brief XSerialPort 适配器；serialPort 为借用指针，不由适配器释放。 */
typedef struct XConsoleShellXSerialPortAdapter {
    XConsoleShellXIODeviceAdapter ioDevice; /**< 复用的 XIODevice 适配器状态。 */
} XConsoleShellXSerialPortAdapter;

/**
 * @brief 初始化串口适配器。
 * @param adapter 待初始化适配器；不能为空。
 * @param port 已配置的 XSerialPort 借用指针，使用期间必须保持有效。
 */
void XConsoleShellXSerialPortAdapter_init(XConsoleShellXSerialPortAdapter* adapter,
                                          XSerialPort* port);
/**
 * @brief 生成 Shell I/O 回调。
 * @param adapter 已初始化的串口适配器。
 * @param io 输出 Shell I/O 回调集合，由调用方提供存储。
 * @return 生成成功返回 true；适配器、串口或 io 无效返回 false。
 */
bool XConsoleShellXSerialPortAdapter_makeIo(XConsoleShellXSerialPortAdapter* adapter,
                                            XConsoleShellIo* io);

#ifdef __cplusplus
}
#endif

#endif /* XCONSOLE_SHELL_ON && XCONSOLE_SHELL_IO_ON && XCONSOLE_SHELL_XSERIALPORT_BACKEND_ON */
#endif /* XCONSOLE_SHELL_XSERIALPORT_H */
