/**
 * @file XConsoleShell_XSerialPort.c
 * @brief XSerialPort 到 XConsoleShell 的公共 API 适配实现。
 */

#include "XConsoleShell_XSerialPort.h"

#if XCONSOLE_SHELL_ON && XCONSOLE_SHELL_IO_ON && XCONSOLE_SHELL_XSERIALPORT_BACKEND_ON

void XConsoleShellXSerialPortAdapter_init(XConsoleShellXSerialPortAdapter* adapter,
                                          XSerialPort* port)
{
    if (!adapter) return;
    /* XSerialPort 的 XIODevice 基类位于首地址，转换由其公开继承布局保证。 */
    XConsoleShellXIODeviceAdapter_init(&adapter->ioDevice, (XIODevice*)port);
}

bool XConsoleShellXSerialPortAdapter_makeIo(XConsoleShellXSerialPortAdapter* adapter,
                                            XConsoleShellIo* io)
{
    return adapter && XConsoleShellXIODeviceAdapter_makeIo(&adapter->ioDevice, io);
}

#endif /* XCONSOLE_SHELL_ON && XCONSOLE_SHELL_IO_ON && XCONSOLE_SHELL_XSERIALPORT_BACKEND_ON */
